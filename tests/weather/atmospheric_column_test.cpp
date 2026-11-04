#include "terraflux/weather/atmospheric_column.hpp"
#include "terraflux/weather/moist_air.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace terraflux::weather;
using namespace terraflux::weather::column;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, const std::string& message, double tolerance) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual));
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

ColumnConfig make_config(std::size_t layers, double thickness, double lapse = 0.0065, double scale_height = 2500.0) {
    ColumnConfig config;
    config.layer_count = layers;
    config.layer_thickness_m = thickness;
    config.environment_lapse_rate_k_m = lapse;
    config.humidity_scale_height_m = scale_height;
    return config;
}

double column_enthalpy(const AtmosphericColumn& c) {
    double total = 0.0;
    for (std::size_t k = 0; k < c.height_m.size(); ++k) {
        total += c.layer_mass_kg_m2[k] *
                 (kSpecificHeatDryAirJkgK * c.air_temperature_k[k] +
                  kLatentHeatVaporisationJkg * c.specific_humidity_kg_kg[k]);
    }
    return total;
}

double column_water(const AtmosphericColumn& c) {
    double total = 0.0;
    for (std::size_t k = 0; k < c.height_m.size(); ++k) {
        total += c.layer_mass_kg_m2[k] * (c.specific_humidity_kg_kg[k] + c.cloud_water_kg_kg[k]);
    }
    return total;
}

// 1. The dry column reproduces the analytic hydrostatic profile of a
//    linear-lapse atmosphere, thins with height, and matches the ISA table
//    to 0.1 % (the table uses geopotential height).
void dry_column_matches_analytic_hydrostatic_profile() {
    const auto config = make_config(40, 500.0);  // 0-20 km, tropopause at 11 km
    const auto c = build_hydrostatic_column(101325.0, 288.15, 0.0, config);
    const double exponent = kGravityMS2 / (moist_air::kDryAirGasConstantJkgK * 0.0065);
    for (std::size_t i = 0; i <= 22; ++i) {
        const double z = c.interface_height_m[i];
        const double expected = 101325.0 * std::pow(1.0 - 0.0065 * z / 288.15, exponent);
        require_near(c.interface_pressure_pa[i], expected, "troposphere pressure at " + std::to_string(z) + " m",
                     1.0e-9 * expected);
    }
    const double p11 = c.interface_pressure_pa[22];
    for (std::size_t i = 23; i <= 40; ++i) {
        const double dz = c.interface_height_m[i] - 11000.0;
        const double expected = p11 * std::exp(-kGravityMS2 * dz / (moist_air::kDryAirGasConstantJkgK * 216.65));
        require_near(c.interface_pressure_pa[i], expected, "isothermal stratosphere pressure", 1.0e-9 * expected);
    }
    require_near(c.interface_pressure_pa[10] / 100.0, 540.5, "ISA 5 km", 0.6);
    require_near(c.interface_pressure_pa[22] / 100.0, 226.3, "ISA 11 km", 0.3);
    require_near(moist_air_density_kg_m3(c.interface_pressure_pa[10], 255.65, 0.0), 0.7364, "ISA density at 5 km", 0.001);
    for (std::size_t k = 1; k < c.height_m.size(); ++k) {
        require(c.pressure_pa[k] < c.pressure_pa[k - 1] && c.air_density_kg_m3[k] < c.air_density_kg_m3[k - 1],
                "pressure and density must thin with height");
        require(c.potential_temperature_k[k] > c.potential_temperature_k[k - 1],
                "a 6.5 K/km atmosphere is statically stable (theta increases upward)");
    }
    double mass = 0.0;
    for (double m : c.layer_mass_kg_m2) mass += m;
    require_near(mass, (101325.0 - c.interface_pressure_pa.back()) / kGravityMS2, "layer masses sum to the column mass", 1.0e-6);
    require(c.interface_pressure_pa.back() / 101325.0 < 0.06, "about 94 % of the mass lies below 20 km");
}

// 2. Moist air is lighter, so a humid column decays more slowly in pressure.
void moist_column_is_lighter() {
    const auto config = make_config(20, 500.0);
    const auto dry = build_hydrostatic_column(100000.0, 300.0, 0.0, config);
    const auto moist = build_hydrostatic_column(100000.0, 300.0, 0.020, config);
    require(moist.interface_pressure_pa[10] > dry.interface_pressure_pa[10], "moist column keeps more pressure aloft");
    require(moist.air_density_kg_m3[0] < dry.air_density_kg_m3[0], "moist surface layer is lighter");
    require(moist.relative_humidity_fraction[0] > 0.6 && moist.relative_humidity_fraction[0] < 1.0, "moist surface layer is humid but unsaturated");
}

// 3. Thermodynamic helpers against reference values.
void thermodynamic_helpers_match_reference_values() {
    require_near(potential_temperature_k(288.15, kReferencePressurePa), 288.15, "theta at reference pressure", 1.0e-12);
    require_near(potential_temperature_k(250.0, 50000.0), 250.0 * std::pow(2.0, kKappa), "theta at 500 hPa", 1.0e-9);
    require_near(dry_adiabatic_lapse_rate_k_m() * 1000.0, 9.762, "dry adiabatic lapse rate", 0.002);
    const double warm = moist_adiabatic_lapse_rate_k_m(303.15, 100000.0);
    const double cold = moist_adiabatic_lapse_rate_k_m(223.15, 30000.0);
    require(warm * 1000.0 > 3.3 && warm * 1000.0 < 3.8, "moist lapse rate at 30 C is about 3.5 K/km");
    require(cold > 0.95 * dry_adiabatic_lapse_rate_k_m() && cold < dry_adiabatic_lapse_rate_k_m(),
            "moist lapse rate approaches the dry rate in very cold air");
    for (double t : {253.15, 273.15, 293.15, 308.15}) {
        require_near(dew_point_k(saturation_vapour_pressure_pa(t)), t, "dew point inverts saturation pressure", 1.0e-9);
    }
    require_near(equivalent_potential_temperature_k(288.15, 100000.0, 0.0), 288.15, "theta_e of dry air equals theta", 1.0e-12);
    require(equivalent_potential_temperature_k(288.15, 100000.0, 0.010) > 288.15 + 25.0, "theta_e rises by ~L q / c_p");
}

// 4. The moist ascent matches an independent fourth-order integration of the
//    pseudo-adiabat in pressure coordinates, dT/dp = Gamma_m R_d T / (g p),
//    from the LCL upward, and Bolton's equivalent potential temperature stays
//    within 2.5 K along the path (the residual of the simplified first law).
void moist_ascent_matches_independent_pressure_integration() {
    const auto config = make_config(30, 500.0);
    const auto c = build_hydrostatic_column(100000.0, 303.15, 0.024, config);
    const auto a = lift_surface_parcel(c, config);
    require(a.lcl_height_m && *a.lcl_height_m > 500.0 && *a.lcl_height_m < 650.0, "LCL of the 30 C / 24 g/kg parcel is about 560 m");

    const auto slope = [](double t, double p) {
        const double e = saturation_vapour_pressure_pa(t);
        const double r = moist_air::kEpsilon * e / (p - e);
        const double r_d = moist_air::kDryAirGasConstantJkgK;
        return r_d * t / p * (1.0 + kLatentHeatVaporisationJkg * r / (r_d * t)) /
               (kSpecificHeatDryAirJkgK + kLatentHeatVaporisationJkg * kLatentHeatVaporisationJkg * r * moist_air::kEpsilon / (r_d * t * t));
    };
    std::size_t start = 0;
    while (a.height_m[start] < *a.lcl_height_m) ++start;
    double t = a.temperature_k[start], p = a.pressure_pa[start];
    const double theta_e_start = equivalent_potential_temperature_k(t, p, a.specific_humidity_kg_kg[start]);
    for (std::size_t i = start + 1; i < a.height_m.size(); ++i) {
        const int n = 200;
        const double dp = (a.pressure_pa[i] - p) / n;
        for (int s = 0; s < n; ++s) {
            const double k1 = slope(t, p);
            const double k2 = slope(t + 0.5 * dp * k1, p + 0.5 * dp);
            const double k3 = slope(t + 0.5 * dp * k2, p + 0.5 * dp);
            const double k4 = slope(t + dp * k3, p + dp);
            t += dp * (k1 + 2.0 * k2 + 2.0 * k3 + k4) / 6.0;
            p += dp;
        }
        require_near(a.temperature_k[i], t, "moist ascent temperature at " + std::to_string(a.height_m[i]) + " m", 0.01);
        const double theta_e = equivalent_potential_temperature_k(a.temperature_k[i], a.pressure_pa[i], a.specific_humidity_kg_kg[i]);
        require_near(theta_e, theta_e_start, "Bolton theta_e along the pseudo-adiabat", 2.5);
        require(a.condensed_water_kg_kg[i] >= a.condensed_water_kg_kg[i - 1] - 1.0e-15, "condensate accumulates monotonically");
    }
    require(a.condensed_water_kg_kg.back() > 0.02, "most of the 24 g/kg condenses by the column top");
}

// 5. The LCL agrees with Bolton's (1980) closed form for the LCL
//    temperature, T_L = 1 / (1/(T_d - 56) + ln(T/T_d)/800) + 56, through
//    p_LCL = p (T_L / T)^(1/kappa).
void lcl_matches_bolton_formula() {
    for (double q : {0.008, 0.012, 0.016}) {
        const auto config = make_config(30, 500.0, 0.0065, 1.0e9);  // uniform humidity
        const auto c = build_hydrostatic_column(100000.0, 298.15, q, config);
        const auto a = lift_surface_parcel(c, config);
        require(a.lcl_height_m.has_value(), "a moist parcel reaches its LCL");
        const double t0 = c.air_temperature_k[0], p0 = c.pressure_pa[0];
        const double t_d = dew_point_k(vapour_pressure_pa(q, p0));
        const double t_l = 1.0 / (1.0 / (t_d - 56.0) + std::log(t0 / t_d) / 800.0) + 56.0;
        const double p_lcl = p0 * std::pow(t_l / t0, 1.0 / kKappa);
        std::size_t i = 0;
        while (a.height_m[i] < *a.lcl_height_m) ++i;
        const double p_found = i == 0 ? p0 : std::exp(std::log(a.pressure_pa[i - 1]) +
                                                      (std::log(a.pressure_pa[i]) - std::log(a.pressure_pa[i - 1])) *
                                                          (*a.lcl_height_m - a.height_m[i - 1]) / (a.height_m[i] - a.height_m[i - 1]));
        require_near(p_found, p_lcl, "LCL pressure vs Bolton for q = " + std::to_string(q), 0.004 * p_lcl);
        // Espy's rule of thumb: z_LCL ~ 125 m per kelvin of dew-point depression.
        require_near(*a.lcl_height_m - c.height_m[0], 125.0 * (t0 - t_d), "Espy estimate", 0.15 * 125.0 * (t0 - t_d));
    }
}

// 6. Convective indices behave physically: a dry stable column has no free
//    convection; a warm, humid column has CAPE of a few thousand J/kg with
//    LFC above the LCL and a high equilibrium level; CAPE grows with moisture;
//    a dry-adiabatic environment is neutral for a dry parcel.
void convective_indices_behave_physically() {
    const auto config = make_config(30, 500.0);
    const auto stable = lift_surface_parcel(build_hydrostatic_column(100000.0, 293.15, 0.002, config), config);
    require(!stable.lfc_height_m && stable.cape_j_kg == 0.0 && stable.convective_potential == "none",
            "dry stable column has no level of free convection");

    double previous = -1.0;
    for (double q : {0.012, 0.016, 0.018}) {
        const auto a = lift_surface_parcel(build_hydrostatic_column(100000.0, 298.15, q, config), config);
        require(a.lcl_height_m && a.lfc_height_m && a.equilibrium_level_m, "humid column has LCL, LFC and EL");
        require(*a.lfc_height_m >= *a.lcl_height_m && *a.equilibrium_level_m > *a.lfc_height_m, "LCL <= LFC < EL");
        require(a.cape_j_kg > previous, "CAPE increases with surface moisture");
        require(a.cin_j_kg >= 0.0, "CIN is reported as a non-negative energy");
        previous = a.cape_j_kg;
    }
    const auto humid = lift_surface_parcel(build_hydrostatic_column(100000.0, 298.15, 0.016, config), config);
    require(humid.cape_j_kg > 1000.0 && humid.cape_j_kg < 2500.0 && humid.convective_potential == "moderate",
            "25 C / 16 g/kg over a 6.5 K/km environment gives moderate CAPE");
    require(*humid.equilibrium_level_m > 9000.0, "deep convection reaches the upper troposphere");

    const auto neutral_config = make_config(20, 500.0, dry_adiabatic_lapse_rate_k_m());
    const auto neutral = lift_surface_parcel(build_hydrostatic_column(100000.0, 300.0, 0.0, neutral_config), neutral_config);
    for (double b : neutral.buoyancy_m_s2) {
        require(std::abs(b) < 2.0e-4, "a dry parcel in a dry-adiabatic environment is neutrally buoyant");
    }
    require(!neutral.lcl_height_m && neutral.cape_j_kg == 0.0, "a dry parcel never condenses");
}

// 7. Convective adjustment: an unstable, humid column is mixed to constant
//    potential temperature below the cloud, condenses where supersaturated,
//    conserves enthalpy and total water, and is idempotent. Stable columns
//    are untouched.
void convective_adjustment_mixes_condenses_and_conserves() {
    auto unstable = build_hydrostatic_column(100000.0, 300.0, 0.012, make_config(10, 500.0, 0.012, 1500.0));
    const double enthalpy_before = column_enthalpy(unstable);
    const double water_before = column_water(unstable);
    require(unstable.potential_temperature_k[1] < unstable.potential_temperature_k[0], "12 K/km is superadiabatic");
    const auto report = adjust_column_convection(unstable);
    require(report.dry_mixing_events > 0 && report.condensing_layers > 0, "mixing and condensation must occur");
    require(report.condensed_water_kg_m2 > 0.0 && report.latent_heating_j_m2 > 0.0, "condensation releases latent heat");
    require_near(column_enthalpy(unstable), enthalpy_before, "enthalpy is conserved", 1.0e-9 * enthalpy_before);
    require_near(column_water(unstable), water_before, "total water is conserved", 1.0e-12 * water_before);
    for (std::size_t k = 1; k < unstable.height_m.size(); ++k) {
        require(unstable.potential_temperature_k[k] >= unstable.potential_temperature_k[k - 1] * (1.0 - 1.0e-9),
                "adjusted column is statically stable");
        require(unstable.relative_humidity_fraction[k] <= 1.0 + 1.0e-6, "no layer stays supersaturated");
        if (unstable.cloud_water_kg_kg[k] > 0.0) {
            require_near(unstable.relative_humidity_fraction[k], 1.0, "cloud layers are exactly saturated", 1.0e-6);
        }
    }
    const auto again = adjust_column_convection(unstable);
    require(again.dry_mixing_events == 0 && again.condensing_layers == 0, "adjustment is idempotent");

    auto stable = build_hydrostatic_column(100000.0, 290.0, 0.004, make_config(10, 500.0));
    const auto untouched = adjust_column_convection(stable);
    require(untouched.dry_mixing_events == 0 && untouched.condensing_layers == 0 && untouched.condensed_water_kg_m2 == 0.0,
            "a stable unsaturated column is left unchanged");
}

// 8. Field diagnostics over several surface states, with NaN for missing levels.
void field_diagnostics_cover_dry_and_convective_cells() {
    const auto config = make_config(24, 500.0);
    const auto d = diagnose_column_field({100000.0, 100000.0, 98000.0}, {298.15, 298.15, 285.0}, {0.0, 0.016, 0.006}, config);
    require(d.cape_j_kg.size() == 3 && d.convective_potential.size() == 3, "one diagnostic per cell");
    require(std::isnan(d.lcl_height_m[0]) && d.cape_j_kg[0] == 0.0 && d.convective_potential[0] == "none", "dry cell has no LCL");
    require(d.lcl_height_m[1] > 0.0 && d.cape_j_kg[1] > 1000.0 && d.convective_potential[1] == "moderate", "humid cell is convective");
    require(d.lcl_height_m[2] > d.lcl_height_m[1] && d.cape_j_kg[2] == 0.0, "cool dry cell has a higher LCL and no CAPE");
    for (std::size_t i = 0; i < 3; ++i) {
        require(d.column_mass_kg_m2[i] > 0.0 && d.top_pressure_pa[i] > 0.0 && d.top_pressure_pa[i] < 30000.0, "column mass and top pressure are reported");
    }
}

// 9. Validation.
void invalid_inputs_are_rejected() {
    require_rejected([] { validate_column_config(make_config(1, 500.0)); }, "one layer must be rejected");
    require_rejected([] { validate_column_config(make_config(5, -1.0)); }, "negative thickness must be rejected");
    require_rejected([] { auto c = make_config(5, 500.0); c.parcel_step_m = 600.0; validate_column_config(c); }, "parcel step above the layer thickness must be rejected");
    require_rejected([] { (void)build_hydrostatic_column(100000.0, 250.0, 0.0, make_config(40, 500.0, 0.03)); }, "a lapse rate freezing the column must be rejected");
    require_rejected([] { (void)build_hydrostatic_column(100000.0, 290.0, 1.0, make_config(5, 500.0)); }, "humidity of 1 kg/kg must be rejected");
    require_rejected([] { (void)build_hydrostatic_column(0.0, 290.0, 0.0, make_config(5, 500.0)); }, "zero surface pressure must be rejected");
    require_rejected([] { (void)diagnose_column_field({1.0e5, 1.0e5}, {290.0}, {0.0, 0.0}, make_config(5, 500.0)); }, "mismatched field sizes must be rejected");
}

}  // namespace

int main() {
    try {
        dry_column_matches_analytic_hydrostatic_profile();
        moist_column_is_lighter();
        thermodynamic_helpers_match_reference_values();
        moist_ascent_matches_independent_pressure_integration();
        lcl_matches_bolton_formula();
        convective_indices_behave_physically();
        convective_adjustment_mixes_condenses_and_conserves();
        field_diagnostics_cover_dry_and_convective_cells();
        invalid_inputs_are_rejected();
    } catch (const std::exception& error) {
        std::cerr << "atmospheric column test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "atmospheric column cases passed\n";
    return 0;
}
