#include "terraflux/weather/pressure_wind.hpp"
#include "terraflux/weather/severe_convection.hpp"

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace terraflux::weather;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, const std::string& message, double tolerance) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
    }
}

template <typename Function>
void require_rejected(Function&& function, const std::string& message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, message);
}

TransportGrid2D make_grid(double latitude) {
    TransportGrid2D grid;
    grid.nx = 12;
    grid.ny = 10;
    grid.spacing_east_m = 50000.0;
    grid.spacing_north_m = 50000.0;
    grid.origin_east_m = -300000.0;
    grid.origin_north_m = -250000.0;
    grid.geographic_anchor_longitude_deg = -97.0;
    grid.geographic_anchor_latitude_deg = latitude;
    return grid;
}

// Balanced state: pressure falling northward (westerly geostrophic jet in
// the NH), warm air to the south, surface wind from the friction balance.
CoupledWindState jet_state(const TransportGrid2D& grid, double dpdy, double friction, double humidity, double gradient_t = 1.5e-5) {
    PressureWindConfig wind;
    wind.time = {"2026-06-01T00:00:00Z", 1.0, 1};
    wind.grid = grid;
    wind.atmosphere.air_density_kg_m3 = 1.2;
    wind.atmosphere.surface_friction_coefficient_s = friction;
    wind.atmosphere.coriolis_mode = "f_plane";
    CoupledWindState state;
    const std::size_t count = grid.nx * grid.ny;
    state.pressure_pa.resize(count);
    state.air_temperature_k.resize(count);
    state.specific_humidity_kg_kg.assign(count, humidity);
    for (std::size_t i = 0; i < count; ++i) {
        const double north = grid.origin_north_m + (i / grid.nx + 0.5) * grid.spacing_north_m;
        state.pressure_pa[i] = 101000.0 + dpdy * north;
        state.air_temperature_k[i] = 300.0 - gradient_t * north;
    }
    const auto balanced = balanced_wind(state.pressure_pa, wind);
    state.wind_east_m_s = balanced.east_m_s;
    state.wind_north_m_s = balanced.north_m_s;
    return state;
}

CloudFormationConfig loaded_gun_config(double inversion_k) {
    CloudFormationConfig config;
    config.column.layer_count = 24;
    config.column.layer_thickness_m = 500.0;
    config.column.inversion_base_m = 1500.0;
    config.column.inversion_depth_m = 500.0;
    config.column.inversion_strength_k = inversion_k;
    config.column.lapse_rate_above_inversion_k_m = 0.0085;
    return config;
}

void stp_formula_and_classes() {
    // Thompson et al. (2003): every term at its reference value gives 1.
    require_near(significant_tornado_parameter(1500.0, 1000.0, 150.0, 20.0, 50.0), 1.0, "STP reference values", 1.0e-12);
    require_near(significant_tornado_parameter(3000.0, 800.0, 300.0, 30.0, 0.0), 2.0 * 1.0 * 2.0 * 1.5 * 1.0, "STP with capped shear term", 1.0e-12);
    require(significant_tornado_parameter(3000.0, 2500.0, 300.0, 30.0, 0.0) == 0.0, "cloud bases above 2 km kill the tornado potential");
    require(significant_tornado_parameter(3000.0, 800.0, 300.0, 10.0, 0.0) == 0.0, "deep shear below 12.5 m/s kills it");
    require(significant_tornado_parameter(3000.0, 800.0, 300.0, 30.0, 250.0) == 0.0, "a cap stronger than 200 J/kg kills it");
    require_near(significant_tornado_parameter(1500.0, 1500.0, -150.0, 20.0, 125.0), 0.5 * 1.0 * 0.5, "LCL and CIN terms interpolate, helicity sign is ignored", 1.0e-12);
    require(significant_tornado_parameter(1500.0, NAN, 150.0, 20.0, 0.0) == 0.0, "no condensation level means no tornado potential");
    const SevereConvectionConfig config;
    require(tornado_risk_class(0.2, config) == "none" && tornado_risk_class(0.7, config) == "low" && tornado_risk_class(2.0, config) == "moderate" && tornado_risk_class(4.0, config) == "significant", "risk classes");
    require_rejected([] { (void)significant_tornado_parameter(-1.0, 1000.0, 100.0, 20.0, 0.0); }, "negative CAPE must be rejected");
}

void inversion_caps_the_energy_until_heating_breaks_it() {
    const auto grid = make_grid(40.0);
    const auto state = jet_state(grid, -1.7e-3, 3.0e-5, 0.015);
    const auto fronts_for = [&](const CloudFormationConfig& c) { return analyse_fronts(state, grid, c); };
    const auto uncapped = loaded_gun_config(0.0), capped = loaded_gun_config(2.0);
    const std::size_t i = 5 * grid.nx + 6;
    const auto quiet_cap = diagnose_severe_convection(state, grid, capped, diagnose_cloud_formation(state, grid, capped, 0.0, fronts_for(capped)), SevereConvectionConfig{});
    const auto quiet_free = diagnose_severe_convection(state, grid, uncapped, diagnose_cloud_formation(state, grid, uncapped, 0.0, fronts_for(uncapped)), SevereConvectionConfig{});
    require(quiet_cap.cap_strength_j_kg[i] > quiet_free.cap_strength_j_kg[i] + 100.0, "the inversion adds convective inhibition");
    require(quiet_cap.heated_cape_j_kg[i] > 1500.0, "the elevated mixed layer stores large CAPE behind the cap");
    require(quiet_cap.capped[i] == 1 && quiet_cap.cap_broken[i] == 0, "energy present but inhibited");
    double previous_cin = quiet_cap.heated_cin_j_kg[i];
    bool broken = false;
    for (double heating : {2.0, 4.0, 6.0, 8.0, 10.0}) {
        const auto s = diagnose_severe_convection(state, grid, capped, diagnose_cloud_formation(state, grid, capped, heating, fronts_for(capped)), SevereConvectionConfig{});
        require(s.heated_cin_j_kg[i] <= previous_cin, "heating erodes the cap");
        previous_cin = s.heated_cin_j_kg[i];
        if (s.cap_broken[i]) broken = true;
    }
    require(broken, "enough heating breaks the cap (explosive release of the stored CAPE)");
}

void helicity_and_storm_motion_mirror_between_hemispheres() {
    for (double latitude : {40.0, -40.0}) {
        const auto grid = make_grid(latitude);
        // Mirror image: pressure falling and warm air lying equatorward in both hemispheres.
        const auto state = jet_state(grid, latitude > 0.0 ? -3.5e-3 : 3.5e-3, 6.0e-5, 0.015, latitude > 0.0 ? 1.5e-5 : -1.5e-5);
        const auto config = loaded_gun_config(0.0);
        const auto s = diagnose_severe_convection(state, grid, config, diagnose_cloud_formation(state, grid, config, 0.0, analyse_fronts(state, grid, config)), SevereConvectionConfig{});
        const std::size_t i = 5 * grid.nx + 6;
        require(s.low_level_shear_m_s[i] > 10.0, "strong friction turning under a 31 m/s jet gives strong low-level shear");
        require(s.deep_shear_m_s[i] > s.low_level_shear_m_s[i], "the thermal wind adds deep shear");
        if (latitude > 0.0) {
            require(s.helicity_m2_s2[i] > 50.0, "veering low-level hodograph gives positive helicity in the NH");
            require(s.storm_motion_north_m_s[i] < 0.5 * (state.wind_north_m_s[i] + 0.0), "Bunkers right-mover deviates to the right of the mean wind");
        } else {
            require(s.helicity_m2_s2[i] < -50.0, "mirror image: negative helicity in the SH");
        }
    }
    // Thermal-wind magnitude check for a pure east-west jet (gradients aligned).
    const auto grid = make_grid(40.0);
    const auto state = jet_state(grid, -1.7e-3, 3.0e-5, 0.015, 1.5e-5);
    const auto config = loaded_gun_config(0.0);
    const auto s = diagnose_severe_convection(state, grid, config, diagnose_cloud_formation(state, grid, config, 0.0, analyse_fronts(state, grid, config)), SevereConvectionConfig{});
    const std::size_t i = 5 * grid.nx + 6;
    const double f = 2.0 * 7.2921159e-5 * std::sin(40.0 * M_PI / 180.0);
    const double thermal = 9.80665 * 6000.0 / (f * state.air_temperature_k[i]) * 1.5e-5;
    const double s1_e = -state.wind_east_m_s[i] + (1.7e-3 / (1.2 * f));
    const double s1_n = -state.wind_north_m_s[i];
    require_near(s.deep_shear_m_s[i], std::hypot(s1_e + thermal, s1_n), "deep shear = low-level shear + thermal wind", 1.0e-6);
}

void tornadoes_need_released_convection_and_rotation() {
    const auto grid = make_grid(40.0);
    const auto config = loaded_gun_config(1.0);
    const auto strong = jet_state(grid, -4.0e-3, 8.0e-5, 0.017);
    const auto weak = jet_state(grid, -0.5e-3, 2.0e-5, 0.017);
    const auto run = [&](const CoupledWindState& state, double heating) {
        const auto clouds = diagnose_cloud_formation(state, grid, config, heating, analyse_fronts(state, grid, config));
        return std::make_pair(clouds, diagnose_severe_convection(state, grid, config, clouds, SevereConvectionConfig{}));
    };
    const auto [strong_clouds, strong_day] = run(strong, 4.0);
    const auto [weak_clouds, weak_day] = run(weak, 4.0);
    require(strong_day.max_stp > weak_day.max_stp && strong_day.max_stp >= 1.0, "a low-level jet with friction turning raises the tornado parameter to at least moderate");
    require(weak_day.tornado_possible_cells == 0, "weak flow: no tornado potential even with storms");
    require(strong_day.tornado_possible_cells > 0, "strong flow: tornadoes possible where cumulonimbus form");
    for (std::size_t i = 0; i < strong_day.tornado_possible.size(); ++i) {
        if (strong_day.tornado_possible[i]) {
            require(strong_clouds.cloud_type[i] == "cumulonimbus" && (strong_day.tornado_risk[i] == "moderate" || strong_day.tornado_risk[i] == "significant"),
                    "tornado potential requires a cumulonimbus and at least a moderate class");
        }
    }
    const auto [night_clouds, night] = run(strong, 0.0);
    require(night.tornado_possible_cells <= strong_day.tornado_possible_cells, "without heating fewer cells release their convection");
}

void invalid_configurations_are_rejected() {
    require_rejected([] { SevereConvectionConfig c; c.stp_moderate = 0.1; validate_severe_convection_config(c); }, "unordered thresholds must be rejected");
    require_rejected([] { SevereConvectionConfig c; c.deep_shear_depth_m = 0.0; validate_severe_convection_config(c); }, "zero shear depth must be rejected");
    require_rejected([] { ColumnConfig c; c.inversion_strength_k = 2.0; validate_column_config(c); }, "an inversion without depth must be rejected");
    require_rejected([] {
        const auto grid = make_grid(40.0);
        auto state = jet_state(grid, -1.7e-3, 3.0e-5, 0.015);
        const auto config = loaded_gun_config(0.0);
        auto clouds = diagnose_cloud_formation(state, grid, config, 0.0, analyse_fronts(state, grid, config));
        clouds.cloud_type.pop_back();
        (void)diagnose_severe_convection(state, grid, config, clouds, SevereConvectionConfig{});
    }, "cloud field size mismatch must be rejected");
}

}  // namespace

int main() {
    try {
        stp_formula_and_classes();
        inversion_caps_the_energy_until_heating_breaks_it();
        helicity_and_storm_motion_mirror_between_hemispheres();
        tornadoes_need_released_convection_and_rotation();
        invalid_configurations_are_rejected();
    } catch (const std::exception& error) {
        std::cerr << "severe convection test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "severe convection cases passed\n";
    return 0;
}
