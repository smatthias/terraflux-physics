// terraflux-run-solar-wind: the solar wind, the magnetosphere's polar caps and what they let in and out.
//
// Reads a terraflux.solar.wind-run/v1 request:
//   dipole            the geomagnetic dipole (moment, north pole)
//   polar_cap, thermosphere, hydrogen, ion_outflow, radiation, sep_atmosphere, chemistry   model parameters
//   storm             an hour-by-hour episode: background solar wind, a coronal mass ejection, a solar proton event
//   map               the global grid (and a land mask) of the frames
//   solar_cycle_path  optional: a terraflux-run-solar-cycle timeline; its monthly sunspot number and F10.7 drive
//   cycle             the monthly climatology over a window of years (solar wind, escape, cosmic rays, chemistry)
//   dipole_scenarios, storm_scenarios, methane_scenarios, cities, comparisons
// and writes a viewer timeline: frames every few hours of the storm (exospheric temperature as the frame's air
// temperature) and a `solar_wind` block (terraflux.solar.wind/v1) with per-cell maps of the aurora, the heavy-ion
// outflow, the radiation dose and the solar-proton ionisation, the hourly series, the cycle, the scenarios and the
// global budgets.
#include "terraflux/weather/atmospheric_escape.hpp"
#include "terraflux/weather/cosmic_dust.hpp"
#include "terraflux/weather/cosmic_radiation.hpp"
#include "terraflux/weather/magnetosphere.hpp"
#include "terraflux/weather/overturning_box.hpp"
#include "utc_time.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;
using namespace terraflux::weather;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kEarthRadius = 6.371e6;
constexpr double kYear = 365.25 * 86400.0;
constexpr double kAmu = 1.66053907e-27;
constexpr double kAvogadro = 6.02214076e23;
constexpr double kElectronVolt = 1.602176634e-19;

// ---- JSON helpers -------------------------------------------------------------------------------------------------

const Json& required(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) throw std::invalid_argument(std::string("run request is missing field: ") + key);
    return object.at(key);
}

double required_number(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_number() || !std::isfinite(value.get<double>())) throw std::invalid_argument(std::string("run request field must be a finite number: ") + key);
    return value.get<double>();
}

double optional_number(const Json& object, const char* key, double fallback) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    return required_number(object, key);
}

std::string optional_string(const Json& object, const char* key, const std::string& fallback) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const Json& value = object.at(key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) throw std::invalid_argument(std::string("run request field must be a non-empty string: ") + key);
    return value.get<std::string>();
}

long long optional_integer(const Json& object, const char* key, long long fallback, long long lo, long long hi) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const Json& value = object.at(key);
    if (!value.is_number_integer() || value.get<long long>() < lo || value.get<long long>() > hi)
        throw std::invalid_argument(std::string("run request field must be an integer within ") + std::to_string(lo) + "-" + std::to_string(hi) + ": " + key);
    return value.get<long long>();
}

void only_keys(const Json& object, const std::string& section, const std::set<std::string>& allowed) {
    if (!object.is_object()) throw std::invalid_argument("run request section must be an object: " + section);
    for (const auto& item : object.items())
        if (!allowed.count(item.key())) throw std::invalid_argument("unknown field in " + section + ": " + item.key());
}

// Reads the named numbers of a section (all optional; "note" is allowed and ignored).
void read_numbers(const Json& root, const char* section, std::initializer_list<std::pair<const char*, double*>> fields) {
    if (!root.contains(section)) return;
    const Json& s = root.at(section);
    std::set<std::string> allowed{"note"};
    for (const auto& f : fields) allowed.insert(f.first);
    only_keys(s, section, allowed);
    for (const auto& f : fields) *f.second = optional_number(s, f.first, *f.second);
}

// Rounded to significant digits (via text, so that the JSON shows the short form).
double tidy(double v, int digits = 4) {
    if (v == 0.0 || !std::isfinite(v)) return v;
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.*g", digits, v);
    return std::strtod(buffer, nullptr);
}

Json number(double v, int digits = 4) { return std::isfinite(v) ? Json(tidy(v, digits)) : Json(nullptr); }

Json tidy_array(const std::vector<double>& values, int digits = 4) {
    Json out = Json::array();
    for (double v : values) out.push_back(number(v, digits));
    return out;
}

// ---- The request --------------------------------------------------------------------------------------------------

struct RadiationModulation {
    double phi_min_gv{0.35};                     // phi = phi_min + per_sunspot * (12-month trailing mean sunspot number)
    double phi_per_sunspot_gv{0.004};
    double storm_cutoff_reduction_per_100nt{0.05};
    double max_cutoff_reduction{0.5};
};

struct Chemistry {
    double polar_noy_ppbv{15.0};                 // background NOy of the polar upper stratosphere (35-50 km)
    double nox_ozone_sensitivity{0.6};           // share of the ozone loss there that NOx catalyses
    double upper_stratosphere_ozone_share{0.12}; // of the ozone column
    double noy_lifetime_days{30.0};
    double aurora_nox_descent_fraction{0.02};    // of the thermospheric NO from aurora that reaches the stratosphere
    double polar_magnetic_latitude_deg{60.0};    // the region the polar NOy is spread over
    double n2o_noy_source_mol_yr{4.0e10};        // stratospheric NOy from N2O oxidation (comparison)
};

struct StormConfig {
    std::string start_utc{"2026-03-18T00:00:00Z"};
    int hours{96}, frame_every{2};
    std::uint64_t seed{7};
    double speed{420.0}, density{6.0}, field{5.0}, fluctuation{3.0}, correlation_h{2.0}, f107{150.0}, f107_81{145.0}, phi_gv{0.75};
    bool cme{true};
    double arrival_h{30.0}, sheath_h{6.0}, cloud_h{20.0}, shock_speed{950.0}, cloud_speed_end{650.0}, sheath_density{25.0}, cloud_density{8.0},
        sheath_field{25.0}, cloud_field{30.0}, cloud_bz_start{-30.0}, cloud_bz_end{12.0}, forbush_phi_gv{0.3}, forbush_recovery_h{48.0}, after_decay_h{24.0};
    bool sep{true};
    double sep_onset_h{6.0}, sep_rise_h{8.0}, sep_decay_h{24.0}, sep_peak_pfu{4000.0}, sep_efold_gv{0.12};
};

struct CycleConfig {
    int first_year{1996}, last_year{2040};
    double kp_monthly_sd{1.2};
    double sep_fluence_per_sunspot_cm2_sr_yr{1.5e7};   // >10 MeV fluence per year per unit sunspot number
    double sep_efold_gv{0.08};
    double field_base_nt{4.0}, field_per_sunspot_nt{0.022};
    double speed_base_km_s{410.0}, speed_declining_km_s{80.0}, declining_slope_per_yr{40.0};
    double density_cm3{6.0}, mean_dst_nt{-15.0};
};

struct Comparisons {
    double atmosphere_mass_kg{5.15e18};
    double oxygen_mass_kg{1.2e18};
    double ocean_mass_kg{1.4e21};
    double absorbed_sunlight_w{1.22e17};
    double earth_age_years{4.5e9};
    double natural_background_usv_yr{2400.0};
    double aircrew_flight_hours_yr{700.0};
    double cloud_forcing_constrained_w_m2_per_pct{0.001};
    double cloud_forcing_hypothesis_w_m2_per_pct{0.05};
    double aurora_visibility_deg{8.0};
};

struct DipoleScenario { std::string name; double ratio; std::string note; };
struct StormScenario { std::string name; double speed, density, by, bz, hours; };
struct MethaneScenario { std::string name; double methane_ppmv; };
struct SepScenario { std::string name; double peak_pfu, efold_gv, duration_h; };
struct City { std::string name; double lat, lon; };

struct Request {
    Json echo;
    GeomagneticDipole dipole;
    PolarCapConfig cap;
    ThermosphereConfig thermo;
    HydrogenEscapeConfig hydrogen;
    IonOutflowConfig ions;
    std::vector<RadiationResponse> responses = default_radiation_responses();
    RadiationModulation modulation;
    SepAtmosphereConfig sep_atmosphere;
    Chemistry chemistry;
    StormConfig storm;
    std::size_t nx{90}, ny{45};
    std::vector<unsigned char> land;
    std::string land_source;
    std::string solar_path;
    CycleConfig cycle;
    std::vector<DipoleScenario> dipoles;
    std::vector<StormScenario> storms;
    std::vector<MethaneScenario> methane;
    std::vector<SepScenario> seps;
    std::vector<City> cities;
    Comparisons cmp;
};

const RadiationResponse& response_named(const std::vector<RadiationResponse>& list, const std::string& name) {
    for (const auto& r : list) if (r.name == name) return r;
    throw std::invalid_argument("missing radiation response: " + name);
}

Request parse_request(const Json& j) {
    Request r;
    r.echo = j;
    only_keys(j, "request", {"schema_version", "dipole", "polar_cap", "thermosphere", "hydrogen", "ion_outflow", "radiation", "sep_atmosphere", "chemistry", "storm", "map",
                             "solar_cycle_path", "cycle", "dipole_scenarios", "storm_scenarios", "methane_scenarios", "sep_scenarios", "cities", "comparisons"});
    read_numbers(j, "dipole", {{"moment_a_m2", &r.dipole.moment_a_m2}, {"north_pole_latitude_deg", &r.dipole.north_pole_latitude_deg}, {"north_pole_longitude_deg", &r.dipole.north_pole_longitude_deg}});
    validate_dipole(r.dipole);
    auto& c = r.cap;
    read_numbers(j, "polar_cap", {{"base_open_flux_wb", &c.base_open_flux_wb}, {"tail_closure_h", &c.tail_closure_h}, {"reconnection_coefficient", &c.reconnection_coefficient},
                                  {"saturation_potential_v", &c.saturation_potential_v}, {"viscous_potential_v", &c.viscous_potential_v}, {"ionosphere_altitude_m", &c.ionosphere_altitude_m},
                                  {"midnight_offset_fraction", &c.midnight_offset_fraction}, {"oval_width_night_deg", &c.oval_width_night_deg},
                                  {"oval_width_night_per_kp_deg", &c.oval_width_night_per_kp_deg}, {"oval_width_day_deg", &c.oval_width_day_deg},
                                  {"oval_width_day_per_kp_deg", &c.oval_width_day_per_kp_deg}, {"ring_current_shift_deg_per_nt", &c.ring_current_shift_deg_per_nt},
                                  {"max_colatitude_deg", &c.max_colatitude_deg}, {"pedersen_conductance_s", &c.pedersen_conductance_s}});
    validate_polar_cap(c);
    read_numbers(j, "thermosphere", {{"diurnal_amplitude", &r.thermo.diurnal_amplitude}, {"peak_local_time_h", &r.thermo.peak_local_time_h},
                                     {"kp_response_h", &r.thermo.kp_response_h}, {"high_latitude_weight", &r.thermo.high_latitude_weight}});
    if (!(r.thermo.diurnal_amplitude >= 0.0 && r.thermo.diurnal_amplitude <= 1.0 && r.thermo.kp_response_h > 0.0 && r.thermo.kp_response_h <= 48.0 &&
          r.thermo.high_latitude_weight >= 0.0 && r.thermo.high_latitude_weight <= 2.5 && r.thermo.peak_local_time_h >= 0.0 && r.thermo.peak_local_time_h < 24.0))
        throw std::invalid_argument("thermosphere: diurnal_amplitude within 0-1, kp_response_h within 0-48, high_latitude_weight within 0-2.5, peak_local_time_h within 0-24");
    auto& h = r.hydrogen;
    read_numbers(j, "hydrogen", {{"water_ppmv", &h.water_ppmv}, {"methane_ppmv", &h.methane_ppmv}, {"hydrogen_ppmv", &h.hydrogen_ppmv},
                                 {"limiting_flux_coefficient", &h.limiting_flux_coefficient}, {"homopause_altitude_m", &h.homopause_altitude_m},
                                 {"exobase_altitude_m", &h.exobase_altitude_m}, {"jeans_share_at_1000k", &h.jeans_share_at_1000k}, {"polar_wind_velocity_m_s", &h.polar_wind_velocity_m_s}});
    validate_hydrogen_escape(h);
    read_numbers(j, "ion_outflow", {{"reference_rate_per_s", &r.ions.reference_rate_per_s}, {"kp_coefficient", &r.ions.kp_coefficient}, {"f107_coefficient", &r.ions.f107_coefficient},
                                    {"f107_reference", &r.ions.f107_reference}, {"escape_fraction", &r.ions.escape_fraction}, {"helium_rate_per_s", &r.ions.helium_rate_per_s}});
    validate_ion_outflow(r.ions);
    if (j.contains("radiation")) {
        const Json& rad = j.at("radiation");
        only_keys(rad, "radiation", {"responses", "modulation", "note"});
        if (rad.contains("responses")) {
            const Json& list = rad.at("responses");
            only_keys(list, "radiation.responses", {"sea_level_neutron", "flight_dose_12km", "pfotzer_ionisation", "cosmogenic_production"});
            for (auto& resp : r.responses) {
                if (!list.contains(resp.name)) continue;
                const Json& x = list.at(resp.name);
                only_keys(x, "radiation.responses." + resp.name, {"yield_exponent", "threshold_gv", "sharpness", "anchor", "reference_phi_gv", "unit", "note"});
                resp.yield_exponent = optional_number(x, "yield_exponent", resp.yield_exponent);
                resp.threshold_gv = optional_number(x, "threshold_gv", resp.threshold_gv);
                resp.sharpness = optional_number(x, "sharpness", resp.sharpness);
                resp.anchor = optional_number(x, "anchor", resp.anchor);
                resp.reference_phi_gv = optional_number(x, "reference_phi_gv", resp.reference_phi_gv);
                resp.unit = optional_string(x, "unit", resp.unit);
            }
        }
        read_numbers(rad, "modulation", {{"phi_min_gv", &r.modulation.phi_min_gv}, {"phi_per_sunspot_gv", &r.modulation.phi_per_sunspot_gv},
                                         {"storm_cutoff_reduction_per_100nt", &r.modulation.storm_cutoff_reduction_per_100nt}, {"max_cutoff_reduction", &r.modulation.max_cutoff_reduction}});
    }
    for (const auto& resp : r.responses) validate_radiation_response(resp);
    const auto& m = r.modulation;
    if (!(m.phi_min_gv >= 0.0 && m.phi_min_gv <= 2.0 && m.phi_per_sunspot_gv >= 0.0 && m.phi_per_sunspot_gv <= 0.05 && m.storm_cutoff_reduction_per_100nt >= 0.0 &&
          m.storm_cutoff_reduction_per_100nt <= 0.5 && m.max_cutoff_reduction >= 0.0 && m.max_cutoff_reduction < 1.0))
        throw std::invalid_argument("radiation.modulation: phi_min_gv within 0-2, phi_per_sunspot_gv within 0-0.05, storm_cutoff_reduction_per_100nt within 0-0.5, max_cutoff_reduction within 0-1");
    auto& sa = r.sep_atmosphere;
    read_numbers(j, "sep_atmosphere", {{"range_coefficient_g_cm2", &sa.range_coefficient_g_cm2}, {"range_exponent", &sa.range_exponent}, {"mean_secant", &sa.mean_secant},
                                       {"ion_pair_energy_ev", &sa.ion_pair_energy_ev}, {"nox_per_ion_pair", &sa.nox_per_ion_pair}, {"hox_per_ion_pair", &sa.hox_per_ion_pair}});
    validate_sep_atmosphere(sa);
    auto& ch = r.chemistry;
    read_numbers(j, "chemistry", {{"polar_noy_ppbv", &ch.polar_noy_ppbv}, {"nox_ozone_sensitivity", &ch.nox_ozone_sensitivity},
                                  {"upper_stratosphere_ozone_share", &ch.upper_stratosphere_ozone_share}, {"noy_lifetime_days", &ch.noy_lifetime_days},
                                  {"aurora_nox_descent_fraction", &ch.aurora_nox_descent_fraction}, {"polar_magnetic_latitude_deg", &ch.polar_magnetic_latitude_deg},
                                  {"n2o_noy_source_mol_yr", &ch.n2o_noy_source_mol_yr}});
    if (!(ch.polar_noy_ppbv > 0.0 && ch.polar_noy_ppbv <= 100.0 && ch.nox_ozone_sensitivity >= 0.0 && ch.nox_ozone_sensitivity <= 1.0 && ch.upper_stratosphere_ozone_share >= 0.0 &&
          ch.upper_stratosphere_ozone_share <= 1.0 && ch.noy_lifetime_days > 0.0 && ch.noy_lifetime_days <= 365.0 && ch.aurora_nox_descent_fraction >= 0.0 &&
          ch.aurora_nox_descent_fraction <= 1.0 && ch.polar_magnetic_latitude_deg >= 40.0 && ch.polar_magnetic_latitude_deg <= 80.0 && ch.n2o_noy_source_mol_yr > 0.0))
        throw std::invalid_argument("chemistry: polar_noy_ppbv within 0-100, sensitivities and shares within 0-1, noy_lifetime_days within 0-365, polar_magnetic_latitude_deg within 40-80");

    // The storm episode.
    auto& s = r.storm;
    const Json& st = required(j, "storm");
    only_keys(st, "storm", {"start_utc", "hours", "frame_every_hours", "seed", "background", "cme", "sep", "note"});
    s.start_utc = optional_string(st, "start_utc", s.start_utc);
    terraflux::weather::detail::utc_seconds(s.start_utc);
    s.hours = static_cast<int>(optional_integer(st, "hours", s.hours, 6, 480));
    s.frame_every = static_cast<int>(optional_integer(st, "frame_every_hours", s.frame_every, 1, 48));
    if (s.hours / s.frame_every + 1 > 120) throw std::invalid_argument("storm: at most 120 frames (hours / frame_every_hours)");
    s.seed = static_cast<std::uint64_t>(optional_integer(st, "seed", static_cast<long long>(s.seed), 0, 1LL << 40));
    read_numbers(st, "background", {{"speed_km_s", &s.speed}, {"density_cm3", &s.density}, {"field_nt", &s.field}, {"fluctuation_nt", &s.fluctuation},
                                    {"correlation_h", &s.correlation_h}, {"f107", &s.f107}, {"f107_81", &s.f107_81}, {"phi_gv", &s.phi_gv}});
    if (!(s.speed >= 200.0 && s.speed <= 3000.0 && s.density > 0.0 && s.density <= 200.0 && s.field >= 0.0 && s.field <= 200.0 && s.fluctuation >= 0.0 && s.fluctuation <= 100.0 &&
          s.correlation_h > 0.0 && s.correlation_h <= 48.0 && s.f107 >= 50.0 && s.f107 <= 400.0 && s.f107_81 >= 50.0 && s.f107_81 <= 400.0 && s.phi_gv >= 0.0 && s.phi_gv <= 2.0))
        throw std::invalid_argument("storm.background: speed 200-3000 km/s, density 0-200 cm^-3, fields 0-200 nT, correlation_h 0-48, F10.7 50-400, phi_gv 0-2");
    s.cme = st.contains("cme");
    if (s.cme) {
        read_numbers(st, "cme", {{"arrival_h", &s.arrival_h}, {"sheath_h", &s.sheath_h}, {"cloud_h", &s.cloud_h}, {"shock_speed_km_s", &s.shock_speed},
                                 {"cloud_speed_end_km_s", &s.cloud_speed_end}, {"sheath_density_cm3", &s.sheath_density}, {"cloud_density_cm3", &s.cloud_density},
                                 {"sheath_field_nt", &s.sheath_field}, {"cloud_field_nt", &s.cloud_field}, {"cloud_bz_start_nt", &s.cloud_bz_start},
                                 {"cloud_bz_end_nt", &s.cloud_bz_end}, {"forbush_phi_gv", &s.forbush_phi_gv}, {"forbush_recovery_h", &s.forbush_recovery_h}, {"after_decay_h", &s.after_decay_h}});
        if (!(s.arrival_h >= 0.0 && s.arrival_h < s.hours && s.sheath_h > 0.0 && s.sheath_h <= 48.0 && s.cloud_h > 0.0 && s.cloud_h <= 96.0 && s.shock_speed >= 200.0 &&
              s.shock_speed <= 3000.0 && s.cloud_speed_end >= 200.0 && s.cloud_speed_end <= 3000.0 && s.sheath_density > 0.0 && s.sheath_density <= 200.0 && s.cloud_density > 0.0 &&
              s.cloud_density <= 200.0 && s.sheath_field >= 0.0 && s.sheath_field <= 300.0 && s.cloud_field > 0.0 && s.cloud_field <= 300.0 && std::abs(s.cloud_bz_start) <= s.cloud_field &&
              std::abs(s.cloud_bz_end) <= s.cloud_field && s.forbush_phi_gv >= 0.0 && s.forbush_phi_gv <= 2.0 && s.forbush_recovery_h > 0.0 && s.after_decay_h > 0.0))
            throw std::invalid_argument("storm.cme: times within the episode, speeds 200-3000 km/s, densities 0-200 cm^-3, fields 0-300 nT with |Bz| <= the cloud field, forbush_phi_gv 0-2");
    }
    s.sep = st.contains("sep");
    if (s.sep) {
        read_numbers(st, "sep", {{"onset_h", &s.sep_onset_h}, {"rise_h", &s.sep_rise_h}, {"decay_h", &s.sep_decay_h}, {"peak_pfu", &s.sep_peak_pfu}, {"efold_rigidity_gv", &s.sep_efold_gv}});
        if (!(s.sep_onset_h >= 0.0 && s.sep_onset_h < s.hours && s.sep_rise_h > 0.0 && s.sep_decay_h > 0.0 && s.sep_peak_pfu >= 0.0 && s.sep_peak_pfu <= 1.0e6 &&
              s.sep_efold_gv >= 0.01 && s.sep_efold_gv <= 1.0))
            throw std::invalid_argument("storm.sep: onset within the episode, positive rise and decay, peak_pfu within 0-1e6, efold_rigidity_gv within 0.01-1");
    }

    // The map.
    const Json& mp = required(j, "map");
    only_keys(mp, "map", {"cells_east", "cells_north", "land_mask", "land_mask_source", "note"});
    r.nx = static_cast<std::size_t>(optional_integer(mp, "cells_east", 90, 12, 360));
    r.ny = static_cast<std::size_t>(optional_integer(mp, "cells_north", 45, 6, 180));
    if (r.nx * r.ny * static_cast<std::size_t>(s.hours / s.frame_every + 1) > 2000000) throw std::invalid_argument("map and storm exceed two million cell snapshots");
    if (mp.contains("land_mask")) {
        const Json& rows = mp.at("land_mask");
        if (!rows.is_array() || rows.size() != r.ny) throw std::invalid_argument("map.land_mask needs one string per row (north first)");
        r.land.assign(r.nx * r.ny, 0);
        for (std::size_t row = 0; row < r.ny; ++row) {
            if (!rows[row].is_string() || rows[row].get_ref<const std::string&>().size() != r.nx) throw std::invalid_argument("map.land_mask rows need one character per cell ('#' land, '.' sea)");
            const std::string& line = rows[row].get_ref<const std::string&>();
            for (std::size_t col = 0; col < r.nx; ++col) {
                if (line[col] != '#' && line[col] != '.') throw std::invalid_argument("map.land_mask characters must be '#' (land) or '.' (sea)");
                r.land[(r.ny - 1 - row) * r.nx + col] = line[col] == '#';
            }
        }
        r.land_source = optional_string(mp, "land_mask_source", "unlabelled");
    }

    if (j.contains("solar_cycle_path")) r.solar_path = optional_string(j, "solar_cycle_path", "");
    if (j.contains("cycle")) {
        const Json& cy = j.at("cycle");
        only_keys(cy, "cycle", {"first_year", "last_year", "kp_monthly_sd", "sep_fluence_per_sunspot_cm2_sr_yr", "sep_efold_rigidity_gv", "field_base_nt", "field_per_sunspot_nt",
                                "speed_base_km_s", "speed_declining_km_s", "declining_slope_per_yr", "density_cm3", "mean_dst_nt", "note"});
        auto& k = r.cycle;
        k.first_year = static_cast<int>(optional_integer(cy, "first_year", k.first_year, 1850, 2100));
        k.last_year = static_cast<int>(optional_integer(cy, "last_year", k.last_year, 1850, 2100));
        k.kp_monthly_sd = optional_number(cy, "kp_monthly_sd", k.kp_monthly_sd);
        k.sep_fluence_per_sunspot_cm2_sr_yr = optional_number(cy, "sep_fluence_per_sunspot_cm2_sr_yr", k.sep_fluence_per_sunspot_cm2_sr_yr);
        k.sep_efold_gv = optional_number(cy, "sep_efold_rigidity_gv", k.sep_efold_gv);
        k.field_base_nt = optional_number(cy, "field_base_nt", k.field_base_nt);
        k.field_per_sunspot_nt = optional_number(cy, "field_per_sunspot_nt", k.field_per_sunspot_nt);
        k.speed_base_km_s = optional_number(cy, "speed_base_km_s", k.speed_base_km_s);
        k.speed_declining_km_s = optional_number(cy, "speed_declining_km_s", k.speed_declining_km_s);
        k.declining_slope_per_yr = optional_number(cy, "declining_slope_per_yr", k.declining_slope_per_yr);
        k.density_cm3 = optional_number(cy, "density_cm3", k.density_cm3);
        k.mean_dst_nt = optional_number(cy, "mean_dst_nt", k.mean_dst_nt);
        if (!(k.first_year <= k.last_year && k.last_year - k.first_year <= 100 && k.kp_monthly_sd >= 0.0 && k.kp_monthly_sd <= 4.0 && k.sep_fluence_per_sunspot_cm2_sr_yr >= 0.0 &&
              k.sep_fluence_per_sunspot_cm2_sr_yr <= 1.0e10 && k.sep_efold_gv >= 0.01 && k.sep_efold_gv <= 1.0 && k.field_base_nt >= 0.0 && k.field_base_nt <= 50.0 &&
              k.field_per_sunspot_nt >= 0.0 && k.field_per_sunspot_nt <= 0.5 && k.speed_base_km_s >= 200.0 && k.speed_base_km_s <= 1000.0 && k.speed_declining_km_s >= 0.0 &&
              k.speed_declining_km_s <= 500.0 && k.declining_slope_per_yr > 0.0 && k.density_cm3 > 0.0 && k.density_cm3 <= 100.0 && k.mean_dst_nt <= 0.0 && k.mean_dst_nt >= -200.0))
            throw std::invalid_argument("cycle: first_year <= last_year within 100 years, kp_monthly_sd within 0-4, positive SEP and solar-wind parameters, mean_dst_nt within -200-0");
    }
    auto list_of = [&](const char* key, std::size_t max) -> const Json* {
        if (!j.contains(key)) return nullptr;
        const Json& list = j.at(key);
        if (!list.is_array() || list.empty() || list.size() > max) throw std::invalid_argument(std::string(key) + " must list 1-" + std::to_string(max) + " entries");
        return &list;
    };
    if (const Json* list = list_of("dipole_scenarios", 12))
        for (const Json& x : *list) {
            only_keys(x, "dipole_scenarios[]", {"name", "moment_ratio", "note"});
            DipoleScenario d{optional_string(x, "name", "scenario"), required_number(x, "moment_ratio"), x.contains("note") ? optional_string(x, "note", "") : ""};
            if (!(d.ratio >= 0.02 && d.ratio <= 5.0)) throw std::invalid_argument("dipole scenarios need a moment_ratio within 0.02-5");
            r.dipoles.push_back(d);
        }
    if (const Json* list = list_of("storm_scenarios", 12))
        for (const Json& x : *list) {
            only_keys(x, "storm_scenarios[]", {"name", "speed_km_s", "density_cm3", "by_nt", "bz_nt", "hours", "note"});
            StormScenario sc{optional_string(x, "name", "storm"), required_number(x, "speed_km_s"), required_number(x, "density_cm3"), optional_number(x, "by_nt", 0.0),
                             required_number(x, "bz_nt"), optional_number(x, "hours", 6.0)};
            if (!(sc.speed >= 200.0 && sc.speed <= 3000.0 && sc.density > 0.0 && sc.density <= 200.0 && std::abs(sc.by) <= 300.0 && std::abs(sc.bz) <= 300.0 && sc.hours >= 1.0 && sc.hours <= 72.0))
                throw std::invalid_argument("storm scenarios need a speed of 200-3000 km/s, a density of 0-200 cm^-3, fields within 300 nT and 1-72 hours");
            r.storms.push_back(sc);
        }
    if (const Json* list = list_of("methane_scenarios", 8))
        for (const Json& x : *list) {
            only_keys(x, "methane_scenarios[]", {"name", "methane_ppmv", "note"});
            MethaneScenario sc{optional_string(x, "name", "methane"), required_number(x, "methane_ppmv")};
            if (!(sc.methane_ppmv >= 0.0 && sc.methane_ppmv <= 100.0)) throw std::invalid_argument("methane scenarios need 0-100 ppmv");
            r.methane.push_back(sc);
        }
    if (const Json* list = list_of("sep_scenarios", 8))
        for (const Json& x : *list) {
            only_keys(x, "sep_scenarios[]", {"name", "peak_pfu", "efold_rigidity_gv", "duration_h", "note"});
            SepScenario sc{optional_string(x, "name", "event"), required_number(x, "peak_pfu"), required_number(x, "efold_rigidity_gv"), optional_number(x, "duration_h", 24.0)};
            if (!(sc.peak_pfu > 0.0 && sc.peak_pfu <= 1.0e6 && sc.efold_gv >= 0.01 && sc.efold_gv <= 1.0 && sc.duration_h > 0.0 && sc.duration_h <= 240.0))
                throw std::invalid_argument("SEP scenarios need peak_pfu within 0-1e6, efold_rigidity_gv within 0.01-1 and duration_h within 0-240");
            r.seps.push_back(sc);
        }
    if (const Json* list = list_of("cities", 40))
        for (const Json& x : *list) {
            only_keys(x, "cities[]", {"name", "latitude_deg", "longitude_deg"});
            City city{optional_string(x, "name", "city"), required_number(x, "latitude_deg"), required_number(x, "longitude_deg")};
            if (!(std::abs(city.lat) <= 90.0 && std::abs(city.lon) <= 180.0)) throw std::invalid_argument("cities need a latitude within +-90 and a longitude within +-180");
            r.cities.push_back(city);
        }
    auto& k = r.cmp;
    read_numbers(j, "comparisons", {{"atmosphere_mass_kg", &k.atmosphere_mass_kg}, {"oxygen_mass_kg", &k.oxygen_mass_kg}, {"ocean_mass_kg", &k.ocean_mass_kg},
                                    {"absorbed_sunlight_w", &k.absorbed_sunlight_w}, {"earth_age_years", &k.earth_age_years}, {"natural_background_usv_yr", &k.natural_background_usv_yr},
                                    {"aircrew_flight_hours_yr", &k.aircrew_flight_hours_yr}, {"cloud_forcing_constrained_w_m2_per_pct", &k.cloud_forcing_constrained_w_m2_per_pct},
                                    {"cloud_forcing_hypothesis_w_m2_per_pct", &k.cloud_forcing_hypothesis_w_m2_per_pct}, {"aurora_visibility_deg", &k.aurora_visibility_deg}});
    for (double v : {k.atmosphere_mass_kg, k.oxygen_mass_kg, k.ocean_mass_kg, k.absorbed_sunlight_w, k.earth_age_years, k.natural_background_usv_yr, k.aircrew_flight_hours_yr})
        if (!(v > 0.0)) throw std::invalid_argument("comparison quantities must be positive");
    if (!(k.cloud_forcing_constrained_w_m2_per_pct >= 0.0 && k.cloud_forcing_hypothesis_w_m2_per_pct >= 0.0 && k.aurora_visibility_deg >= 0.0 && k.aurora_visibility_deg <= 30.0))
        throw std::invalid_argument("comparisons: cloud sensitivities must not be negative, aurora_visibility_deg within 0-30");
    return r;
}

// ---- Geometry -----------------------------------------------------------------------------------------------------

struct SunPosition { double lat, lon; };

double days_since_j2000(double utc_seconds) {
    static const double j2000 = terraflux::weather::detail::utc_seconds("2000-01-01T12:00:00Z");
    return (utc_seconds - j2000) / 86400.0;
}

SunPosition subsolar_point(double d) {
    const double lambda = solar_ecliptic_longitude_deg(d) * kDeg, eps = 23.439 * kDeg;
    const double ra = std::atan2(std::cos(eps) * std::sin(lambda), std::cos(lambda)), dec = std::asin(std::sin(eps) * std::sin(lambda));
    const double lon = std::fmod(std::fmod(ra / kDeg - greenwich_sidereal_deg(d) + 180.0, 360.0) + 360.0, 360.0) - 180.0;
    return {dec / kDeg, lon};
}

std::string iso_hour(double utc_seconds) {
    const long long t = static_cast<long long>(std::floor(utc_seconds + 0.5));
    long long days = t / 86400, rem = t % 86400;
    if (rem < 0) { rem += 86400; --days; }
    // civil from days (Howard Hinnant)
    days += 719468;
    const long long era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long y = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    const unsigned mm = mp < 10 ? mp + 3 : mp - 9;
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%04lld-%02u-%02uT%02lld:%02lld:00Z", y + (mm <= 2), mm, dd, rem / 3600, (rem % 3600) / 60);
    return buffer;
}

struct Grid {
    std::size_t nx, ny, count;
    double dlon, dlat;
    std::vector<double> lat, lon, area, mlat, mlon, cutoff;
    double total_area{0.0};
};

Grid make_grid(const Request& r) {
    Grid g;
    g.nx = r.nx;
    g.ny = r.ny;
    g.count = r.nx * r.ny;
    g.dlon = 360.0 / static_cast<double>(r.nx);
    g.dlat = 180.0 / static_cast<double>(r.ny);
    for (std::size_t i = 0; i < g.count; ++i) {
        const double la = -90.0 + (static_cast<double>(i / r.nx) + 0.5) * g.dlat, lo = -180.0 + (static_cast<double>(i % r.nx) + 0.5) * g.dlon;
        g.lat.push_back(la);
        g.lon.push_back(lo);
        g.area.push_back(kEarthRadius * kEarthRadius * g.dlon * kDeg * (std::sin((la + 0.5 * g.dlat) * kDeg) - std::sin((la - 0.5 * g.dlat) * kDeg)));
        g.total_area += g.area.back();
        const auto mp = magnetic_position(la, lo, r.dipole);
        g.mlat.push_back(mp.latitude_deg);
        g.mlon.push_back(mp.longitude_deg);
        g.cutoff.push_back(stormer_cutoff_gv(mp.latitude_deg, r.dipole.moment_a_m2));
    }
    return g;
}

// Mean of the Newell clock-angle factor sin^p(theta/2) over uniformly distributed clock angles.
double mean_clock_factor(double p) {
    double s = 0.0;
    const int n = 720;
    for (int k = 0; k < n; ++k) s += std::pow(std::sin(0.5 * (k + 0.5) * 2.0 * kPi / n), p);
    return s / n;
}

double mean_exospheric_temperature(const Grid& g, const Request& r, double f107, double f81, double kp, double utc_seconds) {
    const SunPosition sun = subsolar_point(days_since_j2000(utc_seconds));
    const double ut = std::fmod(utc_seconds / 3600.0, 24.0);
    double s = 0.0;
    for (std::size_t i = 0; i < g.count; ++i) {
        const double lst = std::fmod(ut + g.lon[i] / 15.0 + 48.0, 24.0);
        s += g.area[i] * exospheric_temperature_k(f107, f81, kp, lst, g.lat[i], sun.lat, g.mlat[i], r.thermo);
    }
    return s / g.total_area;
}

// Responses as functions of the cutoff for one modulation (and SEP).
struct Radiation {
    CutoffResponse neutron, dose, pfotzer;
    Radiation(const Request& r, double phi, const SepSpectrum* sep)
        : neutron(response_named(r.responses, "sea_level_neutron"), phi, sep),
          dose(response_named(r.responses, "flight_dose_12km"), phi, sep),
          pfotzer(response_named(r.responses, "pfotzer_ionisation"), phi, sep) {}
};

// Area-weighted global mean of a response over magnetic latitude (a dipole: the cutoff depends on it only).
double global_mean_response(const CutoffResponse& response, double moment, double cutoff_factor = 1.0) {
    double s = 0.0, w = 0.0;
    for (int k = 0; k < 90; ++k) {
        const double lat = k + 0.5;
        const double weight = std::cos(lat * kDeg);
        s += weight * response(stormer_cutoff_gv(lat, moment) * cutoff_factor);
        w += weight;
    }
    return s / w;
}

// SEP deposition as a function of the cutoff (0-5 GV) for a unit >10 MeV flux.
struct SepTable {
    double step{0.05};
    std::vector<double> total, upper;            // ion pairs cm^-2 s^-1 per pfu: column, 35-50 km
    std::vector<std::vector<double>> layers;
    SepTable(double efold, const SepAtmosphereConfig& c) {
        const SepSpectrum unit{1.0, efold};
        for (int k = 0; k <= 100; ++k) {   // 0-5 GV
            const auto d = sep_deposition(k * step, unit, c);
            total.push_back(d.ion_pairs_cm2_s);
            std::vector<double> ip;
            for (double e : d.layer_mev_cm2_s) ip.push_back(e * 1.0e6 / c.ion_pair_energy_ev);
            upper.push_back(ip.size() > 1 ? ip[1] : 0.0);
            layers.push_back(ip);
        }
    }
    double lookup(const std::vector<double>& t, double cutoff) const {
        const double x = std::max(0.0, cutoff) / step;
        if (x >= static_cast<double>(t.size() - 1)) return 0.0;   // beyond 5 GV no solar protons arrive
        const std::size_t k = static_cast<std::size_t>(x);
        return t[k] + (x - static_cast<double>(k)) * (t[k + 1] - t[k]);
    }
};

double cutoff_factor_for(double dst, const RadiationModulation& m) {
    return 1.0 - std::min(m.max_cutoff_reduction, m.storm_cutoff_reduction_per_100nt * std::max(0.0, -dst) / 100.0);
}

// Air moles in the polar upper stratosphere (35-50 km) poleward of a magnetic latitude, one hemisphere.
double polar_upper_air_mol(const Grid& g, const Chemistry& ch) {
    double area = 0.0;
    for (std::size_t i = 0; i < g.count; ++i) if (g.mlat[i] >= ch.polar_magnetic_latitude_deg) area += g.area[i];
    const double column_kg_m2 = (standard_air_pressure_pa(35.0e3) - standard_air_pressure_pa(50.0e3)) / 9.80665;
    return column_kg_m2 / 0.028964 * area;
}

double ozone_change_pct(double added_noy_mol, double air_mol, const Chemistry& ch) {
    const double background = air_mol * ch.polar_noy_ppbv * 1.0e-9;
    const double x = std::max(0.0, added_noy_mol) / background;
    return (1.0 / (1.0 + ch.nox_ozone_sensitivity * x) - 1.0) * 100.0;
}

double maglat_of_colat(double colat) { return 90.0 - colat; }

void print_usage(const char* executable) { std::cerr << "Usage: " << executable << " <run-request.json> <output-timeline.json>\n"; }

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) { print_usage(argv[0]); return 2; }
    try {
        std::ifstream request_file(argv[1]);
        if (!request_file) throw std::runtime_error(std::string("cannot open run request: ") + argv[1]);
        Json request;
        try { request = Json::parse(request_file); } catch (const Json::exception& error) { throw std::invalid_argument(std::string("invalid run request JSON: ") + error.what()); }
        if (!request.is_object() || request.value("schema_version", "") != "terraflux.solar.wind-run/v1") throw std::invalid_argument("schema_version must be terraflux.solar.wind-run/v1");
        const Request r = parse_request(request);
        const Grid g = make_grid(r);
        const double moment = r.dipole.moment_a_m2;
        const double r_iono = kEarthRadius + r.cap.ionosphere_altitude_m, r_exo = kEarthRadius + r.hydrogen.exobase_altitude_m;
        const auto& s = r.storm;
        const auto& sa = r.sep_atmosphere;
        const auto& ch = r.chemistry;
        const double air_mol = polar_upper_air_mol(g, ch);
        const double exo_over_iono = (r_exo * r_exo) / (r_iono * r_iono);

        // A quiet reference: the open area and cap-and-oval area under a typical solar wind.
        const SolarWindState typical{420.0, 6.0, 5.0, 0.0};
        const OvalGeometry quiet_oval = oval_geometry(steady_open_flux_wb(typical, r.cap, 1.0), 2.0, -15.0, moment, r.cap);
        const double quiet_open_area = 2.0 * cap_area_m2(quiet_oval, r_iono) * exo_over_iono;

        // ---- The storm: the solar wind hour by hour ------------------------------------------------------------
        const double start = terraflux::weather::detail::utc_seconds(s.start_utc);
        std::vector<SolarWindState> wind(static_cast<std::size_t>(s.hours) + 1);
        std::vector<std::string> phase(wind.size(), "ambient");
        {
            NormalSequence random(s.seed);
            const double rho = std::exp(-1.0 / s.correlation_h), kick = std::sqrt(1.0 - rho * rho);
            double ay = random.next(), az = random.next();
            const double cloud_start = s.arrival_h + s.sheath_h, cloud_end = cloud_start + s.cloud_h;
            for (std::size_t k = 0; k < wind.size(); ++k) {
                const double t = static_cast<double>(k);
                if (k) { ay = rho * ay + kick * random.next(); az = rho * az + kick * random.next(); }
                SolarWindState w{s.speed, s.density, s.fluctuation * ay, s.fluctuation * az};
                if (s.cme && t >= s.arrival_h && t < cloud_start) {
                    w = {s.shock_speed, s.sheath_density, 0.6 * s.sheath_field * ay, s.sheath_field * (0.6 * az - 0.3)};
                    phase[k] = "sheath";
                } else if (s.cme && t >= cloud_start && t < cloud_end) {
                    const double u = (t - cloud_start) / s.cloud_h;
                    const double bz = s.cloud_bz_start + (s.cloud_bz_end - s.cloud_bz_start) * u;
                    w = {0.9 * s.shock_speed + (s.cloud_speed_end - 0.9 * s.shock_speed) * u, s.cloud_density, std::sqrt(std::max(0.0, s.cloud_field * s.cloud_field - bz * bz)), bz};
                    phase[k] = "magnetic cloud";
                } else if (s.cme && t >= cloud_end) {
                    w.speed_km_s = s.speed + (s.cloud_speed_end - s.speed) * std::exp(-(t - cloud_end) / s.after_decay_h);
                    phase[k] = "fast wind behind the CME";
                }
                wind[k] = w;
            }
        }
        auto forbush = [&](double t) {
            if (!s.cme || t < s.arrival_h) return 0.0;
            const double ramp = std::min(1.0, (t - s.arrival_h) / s.sheath_h);
            const double decay = t > s.arrival_h + s.sheath_h ? std::exp(-(t - s.arrival_h - s.sheath_h) / s.forbush_recovery_h) : 1.0;
            return s.forbush_phi_gv * ramp * decay;
        };
        auto sep_flux = [&](double t) {
            if (!s.sep || t < s.sep_onset_h) return 0.0;
            if (t < s.sep_onset_h + s.sep_rise_h) return s.sep_peak_pfu * (t - s.sep_onset_h) / s.sep_rise_h;
            return s.sep_peak_pfu * std::exp(-(t - s.sep_onset_h - s.sep_rise_h) / s.sep_decay_h);
        };
        const SepTable storm_sep(s.sep_efold_gv, sa);

        // ---- The storm: the magnetosphere, the atmosphere and the radiation hour by hour ------------------------
        std::map<std::string, std::vector<double>> series;
        auto push = [&](const char* key, double v) { series[key].push_back(v); };
        double dst_star = dst_star_from_dst(-10.0, dynamic_pressure_npa(wind[0]));
        double open_flux = steady_open_flux_wb(wind[0], r.cap, 1.0);
        double kp_lag = kp_from_solar_wind(wind[0]);
        const double t0 = mean_exospheric_temperature(g, r, s.f107, s.f107_81, kp_lag, start);
        double reservoir = hydrogen_escape_equilibrium(t0, quiet_open_area, r.hydrogen).reservoir_atoms;
        double noy_north = 0.0, noy_south = 0.0, sep_nox_north = 0.0, sep_nox_south = 0.0, sep_hox_total = 0.0;
        double oxygen_lost = 0.0, hydrogen_lost = 0.0, helium_lost = 0.0, hydrogen_quiet_lost = 0.0, energy_in = 0.0, flight_change = 0.0;
        const double quiet_hydrogen = hydrogen_escape_equilibrium(t0, quiet_open_area, r.hydrogen).total_per_s;
        const double quiet_oxygen = oxygen_escape_per_s(kp_from_solar_wind(wind[0]), s.f107, 1.0, r.ions);
        double oxygen_quiet_lost = 0.0;
        const Radiation quiet_rad(r, s.phi_gv, nullptr);
        Json frames = Json::array(), map_frames = Json::array();
        const double metres_per_degree = 2.0 * kPi * kEarthRadius / 360.0;
        const Json grid = {{"nx", g.nx}, {"ny", g.ny}, {"origin_east_m", -180.0 * metres_per_degree}, {"origin_north_m", -90.0 * metres_per_degree},
                           {"spacing_east_m", g.dlon * metres_per_degree}, {"spacing_north_m", g.dlat * metres_per_degree},
                           {"geographic_anchor_longitude_deg", 0.0}, {"geographic_anchor_latitude_deg", 0.0},
                           {"coordinate_system", "local_azimuthal_equidistant_m"}, {"array_order", "row_major_y_then_x"}};
        const std::vector<int> zeros(g.count, 0);
        double min_dst = 0.0, max_kp = 0.0, max_cap = 0.0, min_equatorward = 90.0, max_hp = 0.0, max_joule = 0.0, max_temp = 0.0, min_standoff = 100.0, max_oxygen = 0.0;
        double max_sep_pole = 0.0, max_dose_pole = 0.0, min_neutron_pole = 1e9, min_o3_upper = 0.0, min_o3_column = 0.0;
        double peak_hour_dst = 0.0, peak_hour_sep = 0.0;
        OvalGeometry widest_oval = quiet_oval;
        for (std::size_t k = 0; k < wind.size(); ++k) {
            const double t = static_cast<double>(k), now = start + t * 3600.0;
            const SolarWindState& w = wind[k];
            const double p = dynamic_pressure_npa(w), kp = kp_from_solar_wind(w), coupling = newell_coupling(w);
            PolarCapDrive drive;
            if (k) dst_star = step_dst_star(dst_star, w, 1.0);
            open_flux = step_open_flux_wb(open_flux, w, r.cap, 1.0, k ? 1.0 : 0.0, &drive);
            const double dst = dst_from_star(dst_star, p);
            const OvalGeometry oval = oval_geometry(open_flux, kp, dst, moment, r.cap);
            kp_lag += (kp - kp_lag) * (1.0 - std::exp(-1.0 / r.thermo.kp_response_h));
            const double hp = hemispheric_power_gw(coupling) * 1.0e9;
            const double joule = joule_heating_w(drive.cross_cap_potential_v, oval, r.cap.pedersen_conductance_s, r_iono);
            const MagnetopauseShape mp = shue_magnetopause(p, w.bz_nt, 1.0);
            const SunPosition sun = subsolar_point(days_since_j2000(now));
            const double sun_mlon = magnetic_position(sun.lat, sun.lon, r.dipole).longitude_deg;
            const double ut = std::fmod(t + std::fmod(start / 3600.0, 24.0), 24.0);
            // Exospheric temperature per cell and its global mean.
            std::vector<double> texo(g.count);
            double tmean = 0.0;
            for (std::size_t i = 0; i < g.count; ++i) {
                const double lst = std::fmod(ut + g.lon[i] / 15.0 + 48.0, 24.0);
                texo[i] = exospheric_temperature_k(s.f107, s.f107_81, kp_lag, lst, g.lat[i], sun.lat, g.mlat[i], r.thermo);
                tmean += texo[i] * g.area[i] / g.total_area;
            }
            const double open_area = 2.0 * cap_area_m2(oval, r_iono) * exo_over_iono;
            const HydrogenEscape hyd = k ? step_hydrogen_escape(reservoir, tmean, open_area, 1.0, r.hydrogen) : hydrogen_escape_equilibrium(tmean, open_area, r.hydrogen);
            reservoir = hyd.reservoir_atoms;
            const double o_up = oxygen_upflow_per_s(kp, s.f107, 1.0, r.ions), o_esc = oxygen_escape_per_s(kp, s.f107, 1.0, r.ions);
            const double he = helium_escape_per_s(open_area / quiet_open_area, s.f107, r.ions);
            // Cosmic rays: the modulation with the Forbush decrease, the storm-time cutoff, the solar protons.
            const double phi = s.phi_gv + forbush(t), factor = cutoff_factor_for(dst, r.modulation);
            const SepSpectrum sep{sep_flux(t), s.sep_efold_gv};
            const Radiation rad(r, phi, sep.flux_above_10mev_pfu > 0.0 ? &sep : nullptr);
            // Solar-proton ionisation per cell; NOx and HOx by hemisphere.
            double nox_n = 0.0, nox_s = 0.0, upper_n = 0.0, upper_s = 0.0, hox = 0.0;
            if (sep.flux_above_10mev_pfu > 0.0)
                for (std::size_t i = 0; i < g.count; ++i) {
                    const double c = g.cutoff[i] * factor;
                    const double ip = sep.flux_above_10mev_pfu * storm_sep.lookup(storm_sep.total, c);
                    const double area_cm2 = g.area[i] * 1.0e4;
                    const double nox = ip * area_cm2 * sa.nox_per_ion_pair / kAvogadro, upper = sep.flux_above_10mev_pfu * storm_sep.lookup(storm_sep.upper, c) * area_cm2 * sa.nox_per_ion_pair / kAvogadro;
                    hox += ip * area_cm2 * sa.hox_per_ion_pair / kAvogadro;
                    if (g.mlat[i] >= 0.0) { nox_n += nox; upper_n += upper; } else { nox_s += nox; upper_s += upper; }
                }
            const double keep = std::exp(-1.0 / (ch.noy_lifetime_days * 24.0));
            noy_north = noy_north * keep + upper_n * 3600.0;
            noy_south = noy_south * keep + upper_s * 3600.0;
            sep_nox_north += nox_n * 3600.0;
            sep_nox_south += nox_s * 3600.0;
            sep_hox_total += hox * 3600.0;
            const double o3_n = ozone_change_pct(noy_north, air_mol, ch), o3_s = ozone_change_pct(noy_south, air_mol, ch);
            const double dose_pole = rad.dose(0.0), dose_mid = rad.dose(stormer_cutoff_gv(50.0, moment) * factor), dose_eq = rad.dose(stormer_cutoff_gv(0.0, moment));
            flight_change += dose_pole - quiet_rad.dose(0.0);   // uSv over the hour at 12 km over the pole
            const double neutron_pole = rad.neutron.gcr(0.0) / quiet_rad.neutron.gcr(0.0) * 100.0 - 100.0;
            const double neutron_eq = rad.neutron.gcr(stormer_cutoff_gv(0.0, moment)) / quiet_rad.neutron.gcr(stormer_cutoff_gv(0.0, moment)) * 100.0 - 100.0;
            const double sep_pole = sep.flux_above_10mev_pfu * storm_sep.lookup(storm_sep.total, 0.0);
            if (k) {
                oxygen_lost += o_esc * 3600.0;
                hydrogen_lost += hyd.total_per_s * 3600.0;
                helium_lost += he * 3600.0;
                hydrogen_quiet_lost += quiet_hydrogen * 3600.0;
                oxygen_quiet_lost += quiet_oxygen * 3600.0;
                energy_in += 2.0 * (hp + joule) * 3600.0;
            }
            push("hour", t);
            push("speed_km_s", w.speed_km_s);
            push("density_cm3", w.density_cm3);
            push("by_nt", w.by_nt);
            push("bz_nt", w.bz_nt);
            push("pressure_npa", p);
            push("newell_coupling", coupling);
            push("kp", kp);
            push("dst_nt", dst);
            push("standoff_re", mp.standoff_re);
            push("reconnection_kv", drive.reconnection_v / 1e3);
            push("cross_cap_potential_kv", drive.cross_cap_potential_v / 1e3);
            push("open_flux_gwb", open_flux / 1e9);
            push("cap_boundary_mlat_deg", maglat_of_colat(oval.cap_colatitude_deg));
            push("oval_equatorward_midnight_mlat_deg", maglat_of_colat(oval.equatorward_colatitude_deg(0.0)));
            push("hemispheric_power_gw", hp / 1e9);
            push("joule_heating_gw", joule / 1e9);
            push("exospheric_k", tmean);
            push("density_400km_ratio", thermosphere_density_ratio(tmean, t0));
            push("hydrogen_escape_per_s", hyd.total_per_s);
            push("hydrogen_jeans_per_s", hyd.jeans_per_s);
            push("hydrogen_charge_exchange_per_s", hyd.charge_exchange_per_s);
            push("hydrogen_polar_wind_per_s", hyd.polar_wind_per_s);
            push("oxygen_upflow_per_s", o_up);
            push("oxygen_escape_per_s", o_esc);
            push("helium_escape_per_s", he);
            push("phi_gv", phi);
            push("neutron_pole_change_pct", neutron_pole);
            push("neutron_equator_change_pct", neutron_eq);
            push("sep_flux_pfu", sep.flux_above_10mev_pfu);
            push("sep_ion_pairs_pole_cm2_s", sep_pole);
            push("dose_pole_usv_h", dose_pole);
            push("dose_50deg_usv_h", dose_mid);
            push("dose_equator_usv_h", dose_eq);
            push("cutoff_factor", factor);
            push("sep_nox_north_mol_s", nox_n);
            push("sep_nox_south_mol_s", nox_s);
            push("ozone_upper_north_pct", o3_n);
            push("ozone_upper_south_pct", o3_s);
            min_dst = std::min(min_dst, dst);
            if (dst <= min_dst) peak_hour_dst = t;
            max_kp = std::max(max_kp, kp);
            max_cap = std::max(max_cap, oval.cap_colatitude_deg);
            if (oval.equatorward_colatitude_deg(0.0) > widest_oval.equatorward_colatitude_deg(0.0)) widest_oval = oval;
            min_equatorward = std::min(min_equatorward, maglat_of_colat(oval.equatorward_colatitude_deg(0.0)));
            max_hp = std::max(max_hp, hp);
            max_joule = std::max(max_joule, joule);
            max_temp = std::max(max_temp, tmean);
            min_standoff = std::min(min_standoff, mp.standoff_re);
            max_oxygen = std::max(max_oxygen, o_esc);
            if (sep_pole > max_sep_pole) { max_sep_pole = sep_pole; peak_hour_sep = t; }
            max_dose_pole = std::max(max_dose_pole, dose_pole);
            min_neutron_pole = std::min(min_neutron_pole, neutron_pole);
            min_o3_upper = std::min({min_o3_upper, o3_n, o3_s});
            min_o3_column = std::min(min_o3_column, std::min(o3_n, o3_s) * ch.upper_stratosphere_ozone_share);

            if (k % static_cast<std::size_t>(s.frame_every) != 0) continue;
            // ---- A frame: the maps ------------------------------------------------------------------------------
            std::vector<double> aurora(g.count, 0.0), outflow(g.count, 0.0);
            std::vector<int> region(g.count), air(g.count);
            std::vector<double> mlt(g.count);
            double aurora_sum[2] = {0.0, 0.0}, outflow_sum[2] = {0.0, 0.0};
            for (std::size_t i = 0; i < g.count; ++i) {
                mlt[i] = magnetic_local_time_h(g.mlon[i], sun_mlon);
                const double colat = 90.0 - std::abs(g.mlat[i]);
                const int hemi = g.mlat[i] >= 0.0 ? 0 : 1;
                region[i] = magnetic_region(colat, mlt[i], oval);
                aurora[i] = auroral_shape(colat, mlt[i], oval);
                outflow[i] = ion_outflow_shape(colat, mlt[i], oval);
                if (colat > 75.0) { aurora[i] = 0.0; outflow[i] = 0.0; }
                aurora_sum[hemi] += aurora[i] * g.area[i];
                outflow_sum[hemi] += outflow[i] * g.area[i] * 1.0e4;
                air[i] = static_cast<int>(std::lround(texo[i]));
            }
            for (std::size_t i = 0; i < g.count; ++i) {
                const int hemi = g.mlat[i] >= 0.0 ? 0 : 1;
                aurora[i] = aurora_sum[hemi] > 0.0 ? aurora[i] * hp / aurora_sum[hemi] * 1.0e3 : 0.0;                  // mW/m^2
                outflow[i] = outflow_sum[hemi] > 0.0 ? outflow[i] * 0.5 * o_up / outflow_sum[hemi] / 1.0e7 : 0.0;      // 1e7 ions cm^-2 s^-1
                if (aurora[i] < 0.02) aurora[i] = 0.0;
                if (outflow[i] < 0.01) outflow[i] = 0.0;
            }
            // The dose at 12 km and the solar-proton ionisation depend on the (storm-reduced) cutoff only: as tables.
            std::vector<double> dose_table, sep_table;
            for (int q = 0; q <= 60; ++q) dose_table.push_back(rad.dose(q * 0.25));
            for (int q = 0; q <= 40; ++q) sep_table.push_back(sep.flux_above_10mev_pfu * storm_sep.lookup(storm_sep.total, q * 0.05) / 1.0e9);
            const double elapsed = t * 3600.0;
            frames.push_back({{"schema_version", "terraflux.weather.forecast/v1"}, {"start_time_utc", s.start_utc}, {"elapsed_seconds", elapsed}, {"grid", grid},
                              {"observation_updates", Json::array()},
                              {"fields", {{"air_temperature", {{"units", "K"}, {"values", air}}}, {"wind_east", {{"units", "m/s"}, {"values", zeros}}},
                                          {"wind_north", {{"units", "m/s"}, {"values", zeros}}}}}});
            map_frames.push_back({{"hour", t}, {"time_utc", iso_hour(now)}, {"elapsed_seconds", elapsed}, {"phase", phase[k]},
                                  {"subsolar", {{"latitude_deg", tidy(sun.lat)}, {"longitude_deg", tidy(sun.lon)}, {"magnetic_longitude_deg", tidy(sun_mlon)}}},
                                  {"solar_wind", {{"speed_km_s", tidy(w.speed_km_s)}, {"density_cm3", tidy(w.density_cm3)}, {"by_nt", tidy(w.by_nt, 3)}, {"bz_nt", tidy(w.bz_nt, 3)},
                                                  {"pressure_npa", tidy(p, 3)}}},
                                  {"kp", tidy(kp, 3)}, {"dst_nt", tidy(dst, 4)}, {"g_scale", noaa_g_scale(kp)},
                                  {"magnetopause", {{"standoff_re", tidy(mp.standoff_re)}, {"flaring", tidy(mp.flaring)}}},
                                  {"oval", {{"cap_colatitude_deg", tidy(oval.cap_colatitude_deg)}, {"midnight_offset_deg", tidy(oval.midnight_offset_deg)},
                                            {"width_night_deg", tidy(oval.width_night_deg)}, {"width_day_deg", tidy(oval.width_day_deg)}, {"storm_shift_deg", tidy(oval.storm_shift_deg)}}},
                                  {"hemispheric_power_gw", tidy(hp / 1e9)}, {"joule_heating_gw", tidy(joule / 1e9)}, {"cross_cap_potential_kv", tidy(drive.cross_cap_potential_v / 1e3)},
                                  {"exospheric_k", tidy(tmean)}, {"oxygen_upflow_per_s", tidy(o_up)}, {"hydrogen_escape_per_s", tidy(hyd.total_per_s)},
                                  {"phi_gv", tidy(phi)}, {"sep_flux_pfu", tidy(sep.flux_above_10mev_pfu)}, {"cutoff_factor", tidy(factor)},
                                  {"aurora_mw_m2", tidy_array(aurora, 2)}, {"oxygen_outflow_1e7_cm2_s", tidy_array(outflow, 2)},
                                  {"dose_by_cutoff_usv_h", tidy_array(dose_table, 4)}, {"sep_by_cutoff_1e9_cm2_s", tidy_array(sep_table, 3)}, {"region", region}});
        }

        // ---- Static maps and the latitude profile ----------------------------------------------------------------
        Json static_map = {{"cell_degrees", {g.dlon, g.dlat}}, {"magnetic_latitude_deg", tidy_array(g.mlat, 4)}, {"magnetic_longitude_deg", tidy_array(g.mlon, 4)},
                           {"cutoff_gv", tidy_array(g.cutoff, 3)}};
        if (!r.land.empty()) {
            Json land = Json::array();
            for (unsigned char v : r.land) land.push_back(v != 0);
            static_map["land_mask"] = land;
            static_map["land_mask_source"] = r.land_source;
        } else {
            static_map["land_mask"] = nullptr;
            static_map["land_mask_source"] = nullptr;
        }
        const double phi_min = r.modulation.phi_min_gv, phi_max = r.modulation.phi_min_gv + r.modulation.phi_per_sunspot_gv * 170.0;
        const Radiation rad_min(r, phi_min, nullptr), rad_max(r, phi_max, nullptr);
        const SepSpectrum peak_sep{s.sep ? s.sep_peak_pfu : 0.0, s.sep_efold_gv};
        const Radiation rad_sep(r, s.phi_gv, s.sep ? &peak_sep : nullptr);
        Json profile = {{"magnetic_latitude_deg", Json::array()}, {"cutoff_gv", Json::array()}, {"dose_min_usv_h", Json::array()}, {"dose_max_usv_h", Json::array()},
                        {"dose_sep_peak_usv_h", Json::array()}, {"neutron_min", Json::array()}, {"neutron_max", Json::array()}, {"pfotzer_min", Json::array()},
                        {"pfotzer_max", Json::array()}, {"sep_ion_pairs_peak_cm2_s", Json::array()}};
        for (int lat = 0; lat <= 90; lat += 2) {
            const double c = stormer_cutoff_gv(lat, moment);
            profile["magnetic_latitude_deg"].push_back(lat);
            profile["cutoff_gv"].push_back(tidy(c));
            profile["dose_min_usv_h"].push_back(tidy(rad_min.dose(c)));
            profile["dose_max_usv_h"].push_back(tidy(rad_max.dose(c)));
            profile["dose_sep_peak_usv_h"].push_back(tidy(rad_sep.dose(c)));
            profile["neutron_min"].push_back(tidy(rad_min.neutron(c)));
            profile["neutron_max"].push_back(tidy(rad_max.neutron(c)));
            profile["pfotzer_min"].push_back(tidy(rad_min.pfotzer(c)));
            profile["pfotzer_max"].push_back(tidy(rad_max.pfotzer(c)));
            profile["sep_ion_pairs_peak_cm2_s"].push_back(tidy(peak_sep.flux_above_10mev_pfu * storm_sep.lookup(storm_sep.total, c)));
        }
        // Calibration diagnostics: the latitude effect and the solar-cycle change of each response.
        Json calibration = Json::array();
        const double c_eq = stormer_cutoff_gv(0.0, moment);
        for (const auto& resp : r.responses) {
            const CutoffResponse lo(resp, phi_min), hi(resp, phi_max), ref(resp, 0.4), high_ref(resp, 1.0);
            calibration.push_back({{"name", resp.name}, {"unit", resp.unit}, {"yield_exponent", resp.yield_exponent}, {"threshold_gv", resp.threshold_gv}, {"sharpness", resp.sharpness},
                                   {"anchor", resp.anchor}, {"reference_phi_gv", resp.reference_phi_gv},
                                   {"pole_over_equator", tidy(ref(0.0) / ref(c_eq), 3)}, {"pole_change_phi_04_to_10_pct", tidy((ref(0.0) / high_ref(0.0) - 1.0) * 100.0, 3)},
                                   {"equator_change_phi_04_to_10_pct", tidy((ref(c_eq) / high_ref(c_eq) - 1.0) * 100.0, 3)},
                                   {"pole_cycle_min", tidy(lo(0.0))}, {"pole_cycle_max", tidy(hi(0.0))}, {"equator_cycle_min", tidy(lo(c_eq))}, {"equator_cycle_max", tidy(hi(c_eq))}});
        }

        // ---- The solar cycle: monthly climatology ------------------------------------------------------------------
        Json cycle = nullptr;
        double mean_h = 0.0, mean_o = 0.0, mean_he = 0.0, mean_pw = 0.0, mean_energy = 0.0, mean_sep_nox = 0.0, mean_epp_nox = 0.0, mean_gcr_energy = 0.0;
        double cycle_ion_min = 1e300, cycle_ion_max = 0.0, tsi_forcing = 0.0;
        std::size_t months_used = 0;
        if (!r.solar_path.empty()) {
            std::ifstream file(r.solar_path);
            if (!file) throw std::invalid_argument("cannot open the solar-cycle timeline: " + r.solar_path);
            Json doc;
            try { doc = Json::parse(file); } catch (const Json::exception& e) { throw std::invalid_argument("invalid solar-cycle JSON " + r.solar_path + ": " + e.what()); }
            if (!doc.contains("solar") || doc["solar"].value("schema_version", "") != "terraflux.solar.cycle/v1") throw std::invalid_argument(r.solar_path + " is not a terraflux.solar.cycle/v1 timeline");
            const Json& sol = doc["solar"];
            tsi_forcing = sol.at("climate").at("forcing_at_reference_w_m2").get<double>();
            // Monthly sunspot number and F10.7: the record, then the central forecast.
            struct Month { int year, month; double ssn, f107; bool forecast; };
            std::vector<Month> all;
            auto add = [&](const std::string& first, const Json& ssn, const Json& f107, bool forecast) {
                int y = std::stoi(first.substr(0, 4)), m = std::stoi(first.substr(5, 2));
                for (std::size_t k = 0; k < ssn.size() && k < f107.size(); ++k) {
                    if (ssn[k].is_number() && f107[k].is_number()) all.push_back({y, m, ssn[k].get<double>(), f107[k].get<double>(), forecast});
                    if (++m > 12) { m = 1; ++y; }
                }
            };
            add(sol.at("record").at("first_month").get<std::string>(), sol.at("record").at("sunspot_number"), sol.at("record").at("f107_sfu"), false);
            const Json& fc = sol.at("forecast");
            add(fc.at("first_month").get<std::string>(), fc.at("sunspot").at("central"), fc.at("f107_sfu").at("p50"), true);
            const auto& cy = r.cycle;
            const double clock_newell = mean_clock_factor(8.0 / 3.0), clock_milan = mean_clock_factor(4.5);
            const SepTable cycle_sep(cy.sep_efold_gv, sa);
            // Per unit >10 MeV flux: NOx into each hemisphere (total and 35-50 km), mol/s per pfu.
            double sep_unit_nox = 0.0, sep_unit_upper = 0.0;
            for (std::size_t i = 0; i < g.count; ++i) {
                if (g.mlat[i] < 0.0) continue;
                sep_unit_nox += cycle_sep.lookup(cycle_sep.total, g.cutoff[i]) * g.area[i] * 1.0e4 * sa.nox_per_ion_pair / kAvogadro;
                sep_unit_upper += cycle_sep.lookup(cycle_sep.upper, g.cutoff[i]) * g.area[i] * 1.0e4 * sa.nox_per_ion_pair / kAvogadro;
            }
            const double production_ref = global_mean_response(CutoffResponse(response_named(r.responses, "cosmogenic_production"), 0.4), moment);
            std::vector<double> smooth(all.size());
            for (std::size_t k = 0; k < all.size(); ++k) {
                double sum = 0.0, w = 0.0;
                for (int d = -6; d <= 6; ++d) {
                    const long long q = static_cast<long long>(k) + d;
                    if (q < 0 || q >= static_cast<long long>(all.size())) continue;
                    const double weight = std::abs(d) == 6 ? 0.5 : 1.0;
                    sum += weight * all[static_cast<std::size_t>(q)].ssn;
                    w += weight;
                }
                smooth[k] = sum / w;
            }
            std::map<std::string, std::vector<double>> cs;
            std::map<int, std::pair<double, int>> year_ion;   // annual means of the global sea-level ionisation
            Json months = Json::array();
            std::vector<int> forecast_flags;
            for (std::size_t k = 0; k < all.size(); ++k) {
                const auto& mo = all[k];
                if (mo.year < cy.first_year || mo.year > cy.last_year) continue;
                ++months_used;
                const double slope = (smooth[std::min(all.size() - 1, k + 6)] - smooth[k >= 6 ? k - 6 : 0]) / 1.0;   // per year
                double trailing = 0.0;
                int n = 0;
                for (int d = 0; d < 12 && k >= static_cast<std::size_t>(d); ++d) { trailing += all[k - d].ssn; ++n; }
                trailing /= n;
                double f81 = 0.0;
                n = 0;
                for (int d = -1; d <= 1; ++d) {
                    const long long q = static_cast<long long>(k) + d;
                    if (q < 0 || q >= static_cast<long long>(all.size())) continue;
                    f81 += all[static_cast<std::size_t>(q)].f107;
                    ++n;
                }
                f81 /= n;
                const double field = cy.field_base_nt + cy.field_per_sunspot_nt * mo.ssn;
                const double speed = cy.speed_base_km_s + cy.speed_declining_km_s * std::clamp(-slope / cy.declining_slope_per_yr, 0.0, 1.0);
                const double coupling = std::pow(speed, 4.0 / 3.0) * std::pow(field, 2.0 / 3.0) * clock_newell;
                const double kp = std::clamp(0.05 + 2.244e-4 * coupling + 2.844e-6 * std::sqrt(cy.density_cm3) * speed * speed, 0.0, 9.0);
                const double raw = r.cap.reconnection_coefficient * std::pow(speed * 1e3, 4.0 / 3.0) * field * 1e-9 * clock_milan;
                const double day = raw / (1.0 + raw / r.cap.saturation_potential_v);
                const double flux = r.cap.base_open_flux_wb + day * r.cap.tail_closure_h * 3600.0;
                const OvalGeometry oval = oval_geometry(flux, kp, cy.mean_dst_nt, moment, r.cap);
                const double potential = r.cap.viscous_potential_v + day;
                const double hp = hemispheric_power_gw(coupling) * 1e9, joule = joule_heating_w(potential, oval, r.cap.pedersen_conductance_s, r_iono);
                // Global mean exospheric temperature in the middle of the month (two universal times).
                const double mid = terraflux::weather::detail::utc_seconds([&] { char b[32]; std::snprintf(b, sizeof b, "%04d-%02d-15T00:00:00Z", mo.year, mo.month); return std::string(b); }());
                const double texo = 0.5 * (mean_exospheric_temperature(g, r, mo.f107, f81, kp, mid) + mean_exospheric_temperature(g, r, mo.f107, f81, kp, mid + 12.0 * 3600.0));
                const double open_area = 2.0 * cap_area_m2(oval, r_iono) * exo_over_iono;
                const HydrogenEscape hyd = hydrogen_escape_equilibrium(texo, open_area, r.hydrogen);
                const double oxygen = oxygen_escape_monthly_per_s(kp, cy.kp_monthly_sd, mo.f107, 1.0, r.ions);
                const double helium = helium_escape_per_s(open_area / quiet_open_area, mo.f107, r.ions);
                const double phi = r.modulation.phi_min_gv + r.modulation.phi_per_sunspot_gv * trailing;
                const Radiation rad(r, phi, nullptr);
                const CutoffResponse production(response_named(r.responses, "cosmogenic_production"), phi);
                const double c50 = stormer_cutoff_gv(50.0, moment);
                const double ion_global = global_mean_response(rad.neutron, moment);
                year_ion[mo.year].first += ion_global;
                year_ion[mo.year].second += 1;
                // GCR energy flux into the atmosphere (global mean, W/m^2): pi * int J T dT above the cutoff.
                double gcr_energy = 0.0;
                {
                    double sw = 0.0;
                    for (int la = 0; la < 90; la += 3) {
                        const double c = stormer_cutoff_gv(la + 1.5, moment), wgt = std::cos((la + 1.5) * kDeg);
                        double e = 0.0;
                        const int steps = 200;
                        const double lo = std::max(c, 0.05), hi = 1000.0, dl = std::log(hi / lo) / steps;
                        for (int q = 0; q < steps; ++q) {
                            const double pr = lo * std::exp((q + 0.5) * dl);
                            e += gcr_flux_per_rigidity(pr, phi) * proton_kinetic_gev(pr) * pr * dl;
                        }
                        gcr_energy += wgt * kPi * e * 1.602176634e-10;   // GeV -> J
                        sw += wgt;
                    }
                    gcr_energy /= sw;
                }
                const double sep_flux_mean = cy.sep_fluence_per_sunspot_cm2_sr_yr * mo.ssn / kYear;   // time-mean pfu
                const double sep_nox = sep_flux_mean * sep_unit_nox, sep_upper = sep_flux_mean * sep_unit_upper;   // mol/s per hemisphere
                const double epp_nox = hp / (sa.ion_pair_energy_ev * kElectronVolt) * sa.nox_per_ion_pair / kAvogadro * ch.aurora_nox_descent_fraction;   // mol/s per hemisphere
                // Polar upper-stratosphere ozone from the solar-proton NOx (steady state with the NOy lifetime). The auroral
                // NOx that descends every winter is part of the climatological background NOy, so it is reported, not added.
                const double noy = sep_upper * ch.noy_lifetime_days * 86400.0;
                const double o3 = ozone_change_pct(noy, air_mol, ch);
                mean_h += hyd.total_per_s;
                mean_pw += hyd.polar_wind_per_s;
                mean_o += oxygen;
                mean_he += helium;
                mean_energy += 2.0 * (hp + joule);
                mean_sep_nox += 2.0 * sep_nox;
                mean_epp_nox += 2.0 * epp_nox;
                mean_gcr_energy += gcr_energy;
                char label[16];
                std::snprintf(label, sizeof label, "%04d-%02d", mo.year, mo.month);
                months.push_back(label);
                forecast_flags.push_back(mo.forecast ? 1 : 0);
                cs["sunspot_number"].push_back(mo.ssn);
                cs["f107_sfu"].push_back(mo.f107);
                cs["speed_km_s"].push_back(speed);
                cs["field_nt"].push_back(field);
                cs["kp_mean"].push_back(kp);
                cs["cross_cap_potential_kv"].push_back(potential / 1e3);
                cs["cap_boundary_mlat_deg"].push_back(maglat_of_colat(oval.cap_colatitude_deg));
                cs["oval_equatorward_midnight_mlat_deg"].push_back(maglat_of_colat(oval.equatorward_colatitude_deg(0.0)));
                cs["hemispheric_power_gw"].push_back(hp / 1e9);
                cs["joule_heating_gw"].push_back(joule / 1e9);
                cs["exospheric_k"].push_back(texo);
                cs["hydrogen_escape_per_s"].push_back(hyd.total_per_s);
                cs["hydrogen_jeans_per_s"].push_back(hyd.jeans_per_s);
                cs["hydrogen_charge_exchange_per_s"].push_back(hyd.charge_exchange_per_s);
                cs["hydrogen_polar_wind_per_s"].push_back(hyd.polar_wind_per_s);
                cs["oxygen_escape_per_s"].push_back(oxygen);
                cs["helium_escape_per_s"].push_back(helium);
                cs["mass_loss_kg_s"].push_back(hyd.total_per_s * 1.00784 * kAmu + oxygen * 15.999 * kAmu + helium * 4.0026 * kAmu);
                cs["phi_gv"].push_back(phi);
                cs["neutron_pole"].push_back(rad.neutron(0.0));
                cs["neutron_equator"].push_back(rad.neutron(c_eq));
                cs["dose_pole_usv_h"].push_back(rad.dose(0.0));
                cs["dose_50deg_usv_h"].push_back(rad.dose(c50));
                cs["dose_equator_usv_h"].push_back(rad.dose(c_eq));
                cs["pfotzer_pole"].push_back(rad.pfotzer(0.0));
                cs["pfotzer_equator"].push_back(rad.pfotzer(c_eq));
                cs["production_pct"].push_back(global_mean_response(production, moment) / production_ref * 100.0);
                cs["global_ionisation_relative"].push_back(ion_global);
                cs["gcr_energy_w_m2"].push_back(gcr_energy);
                cs["sep_nox_mol_yr"].push_back(2.0 * sep_nox * kYear);
                cs["epp_nox_mol_yr"].push_back(2.0 * epp_nox * kYear);
                cs["ozone_upper_pct"].push_back(o3);
            }
            if (!months_used) throw std::invalid_argument("cycle: no months of the solar-cycle timeline fall within first_year-last_year");
            for (const auto& [year, sum] : year_ion) {
                if (sum.second < 12) continue;
                cycle_ion_min = std::min(cycle_ion_min, sum.first / sum.second);
                cycle_ion_max = std::max(cycle_ion_max, sum.first / sum.second);
            }
            if (cycle_ion_max <= 0.0) { cycle_ion_min = 1.0; cycle_ion_max = 1.0; }
            const double nm = static_cast<double>(months_used);
            mean_h /= nm; mean_o /= nm; mean_he /= nm; mean_pw /= nm; mean_energy /= nm; mean_sep_nox /= nm; mean_epp_nox /= nm; mean_gcr_energy /= nm;
            Json out_series = Json::object();
            for (const auto& [key, values] : cs) out_series[key] = tidy_array(values, 4);
            cycle = {{"path", r.solar_path}, {"first_year", cy.first_year}, {"last_year", cy.last_year}, {"months", months}, {"forecast", forecast_flags},
                     {"data_origin", sol.value("data_origin", "")}, {"config", {{"kp_monthly_sd", cy.kp_monthly_sd}, {"sep_fluence_per_sunspot_cm2_sr_yr", cy.sep_fluence_per_sunspot_cm2_sr_yr},
                                                                                {"sep_efold_rigidity_gv", cy.sep_efold_gv}, {"field_base_nt", cy.field_base_nt}, {"field_per_sunspot_nt", cy.field_per_sunspot_nt},
                                                                                {"speed_base_km_s", cy.speed_base_km_s}, {"speed_declining_km_s", cy.speed_declining_km_s},
                                                                                {"density_cm3", cy.density_cm3}, {"mean_dst_nt", cy.mean_dst_nt}, {"newell_clock_factor", tidy(clock_newell)},
                                                                                {"milan_clock_factor", tidy(clock_milan)}}},
                     {"series", out_series}};
        } else {
            // No cycle: the budgets use the storm's quiet background.
            mean_h = quiet_hydrogen;
            mean_o = quiet_oxygen;
            mean_he = helium_escape_per_s(1.0, s.f107, r.ions);
            mean_pw = hydrogen_escape_equilibrium(t0, quiet_open_area, r.hydrogen).polar_wind_per_s;
            mean_energy = 2.0 * hemispheric_power_gw(newell_coupling(typical)) * 1e9;
        }

        // ---- Scenarios --------------------------------------------------------------------------------------------
        Json dipole_rows = Json::array();
        {
            const double phi = 0.6;
            const CutoffResponse production(response_named(r.responses, "cosmogenic_production"), phi), pfotzer(response_named(r.responses, "pfotzer_ionisation"), phi),
                dose(response_named(r.responses, "flight_dose_12km"), phi), neutron(response_named(r.responses, "sea_level_neutron"), phi);
            const double prod_today = global_mean_response(production, moment), ion_today = global_mean_response(pfotzer, moment), neu_today = global_mean_response(neutron, moment);
            const double area_today = cap_and_oval_area_m2(oval_geometry(steady_open_flux_wb(typical, r.cap, 1.0), 3.0, -20.0, moment, r.cap), r_iono);
            for (const auto& d : r.dipoles) {
                const double m = d.ratio, mom = moment * m;
                const SolarWindState wtyp = typical;
                const OvalGeometry oval = oval_geometry(steady_open_flux_wb(wtyp, r.cap, m), 3.0, -20.0, mom, r.cap);
                const double c_equator = stormer_cutoff_gv(0.0, mom);
                const double window = c_equator > 1.0 ? 1.0 - std::sin(std::acos(std::pow(1.0 / c_equator, 0.25))) : 1.0;
                const double area_ratio = cap_and_oval_area_m2(oval, r_iono) / area_today;
                dipole_rows.push_back({{"name", d.name}, {"moment_ratio", m}, {"moment_a_m2", tidy(mom)}, {"note", d.note},
                                       {"standoff_re", tidy(shue_magnetopause(2.0, 0.0, m).standoff_re, 3)},
                                       {"cap_boundary_mlat_deg", tidy(maglat_of_colat(oval.cap_colatitude_deg), 3)},
                                       {"oval_equatorward_midnight_mlat_deg", tidy(maglat_of_colat(oval.equatorward_colatitude_deg(0.0)), 3)},
                                       {"cutoff_equator_gv", tidy(c_equator, 3)}, {"cutoff_50deg_gv", tidy(stormer_cutoff_gv(50.0, mom), 3)},
                                       {"polar_window_share", tidy(window, 3)},
                                       {"global_ionisation_pct", tidy(global_mean_response(pfotzer, mom) / ion_today * 100.0, 3)},
                                       {"global_sea_level_pct", tidy(global_mean_response(neutron, mom) / neu_today * 100.0, 3)},
                                       {"production_pct", tidy(global_mean_response(production, mom) / prod_today * 100.0, 3)},
                                       {"dose_50deg_usv_h", tidy(dose(stormer_cutoff_gv(50.0, mom)), 3)}, {"dose_equator_usv_h", tidy(dose(c_equator), 3)},
                                       {"oxygen_escape_pct", tidy(area_ratio * 100.0, 3)}, {"hydrogen_escape_pct", 100.0}});
            }
        }
        Json storm_rows = Json::array();
        for (const auto& sc : r.storms) {
            const SolarWindState w{sc.speed, sc.density, sc.by, sc.bz};
            const double p = dynamic_pressure_npa(w), kp = kp_from_solar_wind(w);
            double star = dst_star_from_dst(-10.0, p);
            for (int q = 0; q < static_cast<int>(std::round(sc.hours)); ++q) star = step_dst_star(star, w, 1.0);
            const double dst = dst_from_star(star, p);
            const OvalGeometry oval = oval_geometry(steady_open_flux_wb(w, r.cap, 1.0), kp, dst, moment, r.cap);
            PolarCapDrive drive;
            step_open_flux_wb(steady_open_flux_wb(w, r.cap, 1.0), w, r.cap, 1.0, 0.0, &drive);
            const double hp = hemispheric_power_gw(newell_coupling(w)), joule = joule_heating_w(drive.cross_cap_potential_v, oval, r.cap.pedersen_conductance_s, r_iono) / 1e9;
            const double texo = mean_exospheric_temperature(g, r, s.f107, s.f107_81, kp, start), texo_quiet = mean_exospheric_temperature(g, r, s.f107, s.f107_81, 1.0, start);
            const double overhead = maglat_of_colat(oval.equatorward_colatitude_deg(0.0));
            storm_rows.push_back({{"name", sc.name}, {"speed_km_s", sc.speed}, {"density_cm3", sc.density}, {"bz_nt", sc.bz}, {"hours", sc.hours},
                                  {"kp", tidy(kp, 3)}, {"g_scale", noaa_g_scale(kp)}, {"dst_nt", tidy(dst, 3)}, {"standoff_re", tidy(shue_magnetopause(p, sc.bz, 1.0).standoff_re, 3)},
                                  {"cross_cap_potential_kv", tidy(drive.cross_cap_potential_v / 1e3, 3)}, {"cap_boundary_mlat_deg", tidy(maglat_of_colat(oval.cap_colatitude_deg), 3)},
                                  {"aurora_overhead_mlat_deg", tidy(overhead, 3)}, {"aurora_visible_mlat_deg", tidy(overhead - r.cmp.aurora_visibility_deg, 3)},
                                  {"hemispheric_power_gw", tidy(hp, 3)}, {"joule_heating_gw", tidy(joule, 3)}, {"exospheric_k", tidy(texo, 4)},
                                  {"density_400km_ratio", tidy(thermosphere_density_ratio(texo, texo_quiet), 3)},
                                  {"oxygen_escape_per_s", tidy(oxygen_escape_per_s(kp, s.f107, 1.0, r.ions), 3)}, {"oxygen_escape_kg_s", tidy(oxygen_escape_per_s(kp, s.f107, 1.0, r.ions) * 15.999 * kAmu, 3)}});
        }
        Json methane_rows = Json::array();
        for (const auto& sc : r.methane) {
            auto h = r.hydrogen;
            h.methane_ppmv = sc.methane_ppmv;
            const double rate = diffusion_limited_hydrogen_per_s(h);
            methane_rows.push_back({{"name", sc.name}, {"methane_ppmv", sc.methane_ppmv}, {"hydrogen_per_s", tidy(rate)}, {"hydrogen_kg_s", tidy(rate * 1.00784 * kAmu, 3)},
                                    {"relative_to_today_pct", tidy(rate / diffusion_limited_hydrogen_per_s(r.hydrogen) * 100.0, 3)}});
        }
        Json sep_rows = Json::array();
        for (const auto& sc : r.seps) {
            const SepTable table(sc.efold_gv, sa);
            const SepSpectrum spectrum{sc.peak_pfu, sc.efold_gv};
            const Radiation with(r, 0.6, &spectrum), without(r, 0.6, nullptr);
            double nox = 0.0, upper = 0.0;
            for (std::size_t i = 0; i < g.count; ++i) {
                if (g.mlat[i] < 0.0) continue;
                nox += table.lookup(table.total, g.cutoff[i]) * g.area[i] * 1.0e4 * sa.nox_per_ion_pair / kAvogadro;
                upper += table.lookup(table.upper, g.cutoff[i]) * g.area[i] * 1.0e4 * sa.nox_per_ion_pair / kAvogadro;
            }
            const double fluence_s = sc.peak_pfu * sc.duration_h * 3600.0;   // pfu seconds
            double reach = 90.0;
            for (double lat = 90.0; lat >= 30.0; lat -= 0.5)
                if (table.lookup(table.total, stormer_cutoff_gv(lat, moment)) >= 0.1 * table.total.front()) reach = lat;
            sep_rows.push_back({{"name", sc.name}, {"peak_pfu", sc.peak_pfu}, {"efold_rigidity_gv", sc.efold_gv}, {"duration_h", sc.duration_h},
                                {"flux_above_1gv_pfu", tidy(sep_flux_above_pfu(1.0, spectrum), 3)}, {"ion_pairs_pole_cm2_s", tidy(sc.peak_pfu * table.total.front(), 3)},
                                {"reach_mlat_deg", tidy(reach, 3)}, {"nox_per_hemisphere_mol", tidy(nox * fluence_s, 3)},
                                {"ozone_upper_pct", tidy(ozone_change_pct(upper * fluence_s, air_mol, ch), 3)},
                                {"dose_pole_usv_h", tidy(with.dose(0.0), 3)}, {"dose_pole_ratio", tidy(with.dose(0.0) / without.dose(0.0), 3)},
                                {"neutron_pole_increase_pct", tidy((with.neutron(0.0) / without.neutron(0.0) - 1.0) * 100.0, 3)}});
        }
        Json city_rows = Json::array();
        {
            const double overhead_deg = maglat_of_colat(widest_oval.equatorward_colatitude_deg(0.0));
            const double pole_deg = maglat_of_colat(widest_oval.poleward_colatitude_deg(0.0));   // the oval's poleward edge at midnight
            for (const auto& c : r.cities) {
                const auto mp = magnetic_position(c.lat, c.lon, r.dipole);
                const double am = std::abs(mp.latitude_deg), cutoff = stormer_cutoff_gv(mp.latitude_deg, moment);
                const std::string aurora = am > pole_deg ? "towards the equator (the city lies in the polar cap)" : am >= overhead_deg ? "overhead"
                                           : am >= overhead_deg - r.cmp.aurora_visibility_deg ? "low on the horizon" : "not visible";
                const std::string electrojet = am >= overhead_deg && am <= pole_deg ? "under the electrojet"
                                               : am >= overhead_deg - 5.0 && am <= pole_deg + 5.0 ? "near" : "far";
                const double sep_access = s.sep ? peak_sep.flux_above_10mev_pfu * storm_sep.lookup(storm_sep.total, cutoff) : 0.0;
                city_rows.push_back({{"name", c.name}, {"latitude_deg", c.lat}, {"longitude_deg", c.lon}, {"magnetic_latitude_deg", tidy(mp.latitude_deg, 3)},
                                     {"cutoff_gv", tidy(cutoff, 3)}, {"dose_min_usv_h", tidy(rad_min.dose(cutoff), 3)}, {"dose_max_usv_h", tidy(rad_max.dose(cutoff), 3)},
                                     {"dose_sep_peak_usv_h", tidy(rad_sep.dose(cutoff), 3)}, {"sep_ion_pairs_peak_cm2_s", tidy(sep_access, 3)},
                                     {"aurora_at_storm_peak", aurora}, {"electrojet_at_storm_peak", electrojet}});
            }
        }

        // ---- Budgets ----------------------------------------------------------------------------------------------
        const double h_kg = mean_h * 1.00784 * kAmu, o_kg = mean_o * 15.999 * kAmu, he_kg = mean_he * 4.0026 * kAmu, pw_kg = mean_pw * 1.00784 * kAmu;
        const double total_kg = h_kg + o_kg + he_kg, polar_kg = pw_kg + o_kg + he_kg;
        const double water_kg = h_kg * 18.015 / 2.016;
        const double storm_days = static_cast<double>(s.hours) / 24.0;
        Json budgets = {
            {"mean_escape", {{"hydrogen_per_s", tidy(mean_h)}, {"hydrogen_kg_s", tidy(h_kg)}, {"oxygen_per_s", tidy(mean_o)}, {"oxygen_kg_s", tidy(o_kg)},
                             {"helium_per_s", tidy(mean_he)}, {"helium_kg_s", tidy(he_kg)}, {"total_kg_s", tidy(total_kg)}, {"total_t_yr", tidy(total_kg * kYear / 1000.0)},
                             {"polar_kg_s", tidy(polar_kg)}, {"polar_share", tidy(polar_kg / total_kg)}, {"hydrogen_polar_wind_share", tidy(mean_pw / mean_h)},
                             {"diffusion_limit_per_s", tidy(diffusion_limited_hydrogen_per_s(r.hydrogen))}, {"source", months_used ? "cycle mean" : "storm background"}}},
            {"reservoirs", {{"atmosphere_share_per_million_years", tidy(total_kg * kYear * 1.0e6 / r.cmp.atmosphere_mass_kg)},
                            {"oxygen_share_per_million_years", tidy(o_kg * kYear * 1.0e6 / r.cmp.oxygen_mass_kg)},
                            {"oxygen_share_over_earth_history", tidy(o_kg * kYear * r.cmp.earth_age_years / r.cmp.oxygen_mass_kg)},
                            {"water_equivalent_kg_s", tidy(water_kg)}, {"ocean_share_per_million_years", tidy(water_kg * kYear * 1.0e6 / r.cmp.ocean_mass_kg)},
                            {"ocean_share_over_earth_history", tidy(water_kg * kYear * r.cmp.earth_age_years / r.cmp.ocean_mass_kg)}}},
            {"energy", {{"magnetospheric_input_w", tidy(mean_energy)}, {"share_of_sunlight", tidy(mean_energy / r.cmp.absorbed_sunlight_w)},
                        {"gcr_energy_w_m2", months_used ? number(mean_gcr_energy) : Json(nullptr)},
                        {"gcr_share_of_sunlight", months_used ? number(mean_gcr_energy * 4.0 * kPi * kEarthRadius * kEarthRadius / r.cmp.absorbed_sunlight_w) : Json(nullptr)}}},
            {"storm", {{"hours", s.hours}, {"min_dst_nt", tidy(min_dst)}, {"hour_of_min_dst", peak_hour_dst}, {"max_kp", tidy(max_kp, 3)}, {"g_scale", noaa_g_scale(max_kp)},
                       {"max_cap_colatitude_deg", tidy(max_cap)}, {"min_cap_boundary_mlat_deg", tidy(maglat_of_colat(max_cap))},
                       {"min_oval_equatorward_mlat_deg", tidy(min_equatorward)}, {"min_standoff_re", tidy(min_standoff)},
                       {"max_hemispheric_power_gw", tidy(max_hp / 1e9)}, {"max_joule_heating_gw", tidy(max_joule / 1e9)}, {"max_exospheric_k", tidy(max_temp)},
                       {"quiet_exospheric_k", tidy(t0)}, {"max_density_400km_ratio", tidy(thermosphere_density_ratio(max_temp, t0))},
                       {"energy_in_j", tidy(energy_in)}, {"oxygen_lost_kg", tidy(oxygen_lost * 15.999 * kAmu)}, {"oxygen_quiet_kg", tidy(oxygen_quiet_lost * 15.999 * kAmu)},
                       {"hydrogen_lost_kg", tidy(hydrogen_lost * 1.00784 * kAmu)}, {"hydrogen_quiet_kg", tidy(hydrogen_quiet_lost * 1.00784 * kAmu)},
                       {"helium_lost_kg", tidy(helium_lost * 4.0026 * kAmu)}, {"max_oxygen_escape_per_s", tidy(max_oxygen)},
                       {"oxygen_vs_mean_days", tidy((oxygen_lost - oxygen_quiet_lost) * 15.999 * kAmu / (o_kg * 86400.0))},
                       {"max_sep_ion_pairs_pole_cm2_s", tidy(max_sep_pole)}, {"hour_of_sep_peak", peak_hour_sep}, {"max_dose_pole_usv_h", tidy(max_dose_pole)},
                       {"min_neutron_pole_change_pct", tidy(min_neutron_pole, 3)}, {"sep_nox_north_mol", tidy(sep_nox_north)}, {"sep_nox_south_mol", tidy(sep_nox_south)},
                       {"sep_hox_mol", tidy(sep_hox_total)}, {"min_ozone_upper_pct", tidy(min_o3_upper, 3)}, {"min_ozone_column_pct", tidy(min_o3_column, 3)},
                       {"polar_dose_change_usv", tidy(flight_change, 3)}, {"days", tidy(storm_days)}}},
            {"chemistry", {{"sep_nox_mol_yr", months_used ? number(mean_sep_nox * kYear) : Json(nullptr)}, {"epp_nox_mol_yr", months_used ? number(mean_epp_nox * kYear) : Json(nullptr)},
                           {"n2o_noy_source_mol_yr", ch.n2o_noy_source_mol_yr}, {"polar_upper_air_mol", tidy(air_mol)},
                           {"sep_share_of_n2o_source", months_used ? number(mean_sep_nox * kYear / ch.n2o_noy_source_mol_yr) : Json(nullptr)}}},
            {"clouds", {{"global_ionisation_cycle_change_pct", months_used ? number((cycle_ion_max / cycle_ion_min - 1.0) * 100.0, 3) : Json(nullptr)},
                        {"forcing_constrained_w_m2", months_used ? number((cycle_ion_max / cycle_ion_min - 1.0) * 100.0 * r.cmp.cloud_forcing_constrained_w_m2_per_pct, 3) : Json(nullptr)},
                        {"forcing_hypothesis_w_m2", months_used ? number((cycle_ion_max / cycle_ion_min - 1.0) * 100.0 * r.cmp.cloud_forcing_hypothesis_w_m2_per_pct, 3) : Json(nullptr)},
                        {"tsi_cycle_forcing_w_m2", months_used ? Json(tsi_forcing) : Json(nullptr)}}},
            {"aviation", {{"dose_pole_min_usv_h", tidy(rad_min.dose(0.0), 3)}, {"dose_pole_max_usv_h", tidy(rad_max.dose(0.0), 3)}, {"dose_equator_min_usv_h", tidy(rad_min.dose(c_eq), 3)},
                          {"polar_flight_10h_usv", tidy(10.0 * rad_min.dose(0.0), 3)}, {"equatorial_flight_10h_usv", tidy(10.0 * rad_min.dose(c_eq), 3)},
                          {"aircrew_usv_yr_polar", tidy(r.cmp.aircrew_flight_hours_yr * rad_min.dose(0.0), 3)}, {"natural_background_usv_yr", r.cmp.natural_background_usv_yr}}},
        };
        Json comparisons = {{"atmosphere_mass_kg", r.cmp.atmosphere_mass_kg}, {"oxygen_mass_kg", r.cmp.oxygen_mass_kg}, {"ocean_mass_kg", r.cmp.ocean_mass_kg},
                            {"absorbed_sunlight_w", r.cmp.absorbed_sunlight_w}, {"earth_age_years", r.cmp.earth_age_years}, {"natural_background_usv_yr", r.cmp.natural_background_usv_yr},
                            {"aircrew_flight_hours_yr", r.cmp.aircrew_flight_hours_yr}, {"cloud_forcing_constrained_w_m2_per_pct", r.cmp.cloud_forcing_constrained_w_m2_per_pct},
                            {"cloud_forcing_hypothesis_w_m2_per_pct", r.cmp.cloud_forcing_hypothesis_w_m2_per_pct}, {"aurora_visibility_deg", r.cmp.aurora_visibility_deg}};

        Json storm_series = Json::object();
        for (const auto& [key, values] : series) storm_series[key] = tidy_array(values, 4);
        Json output = {
            {"schema_version", "terraflux.weather.timeline/v1"},
            {"frames", frames},
            {"solar_wind", {
                {"schema_version", "terraflux.solar.wind/v1"},
                {"method", "dipole_newell_obrien_milan_force_field"},
                {"source_kind", "engine_output"},
                {"request", r.echo},
                {"dipole", {{"moment_a_m2", moment}, {"north_pole_latitude_deg", r.dipole.north_pole_latitude_deg}, {"north_pole_longitude_deg", r.dipole.north_pole_longitude_deg},
                            {"equatorial_field_ut", tidy(dipole_equatorial_field_t(moment, kEarthRadius) * 1e6)}, {"polar_field_ut", tidy(2.0 * dipole_equatorial_field_t(moment, kEarthRadius) * 1e6)},
                            {"cutoff_equator_gv", tidy(c_eq)}}},
                {"static", static_map},
                {"storm", {{"start_utc", s.start_utc}, {"hours", s.hours}, {"frame_every_hours", s.frame_every}, {"cme", s.cme}, {"sep", s.sep},
                           {"phases", phase}, {"series", storm_series}, {"frames", map_frames}}},
                {"latitude_profile", profile},
                {"responses", calibration},
                {"cycle", cycle},
                {"dipole_scenarios", dipole_rows},
                {"storm_scenarios", storm_rows},
                {"methane_scenarios", methane_rows},
                {"sep_scenarios", sep_rows},
                {"cities", city_rows},
                {"budgets", budgets},
                {"comparisons", comparisons},
                {"limitations", Json::array({
                    "a centred, tilted dipole: no South Atlantic Anomaly or other non-dipole features; the cutoff is the vertical Stoermer value with a crude storm-time reduction",
                    "empirical coupling (Newell Kp, O'Brien-McPherron Dst, Milan reconnection with a saturation, Shue magnetopause): hourly, global indices, not a magnetohydrodynamic model",
                    "the oval and the outflow regions are analytic shapes around a shifted circle; the energy and the outflow are distributed over them and normalised to global rates",
                    "hydrogen escape is diffusion-limited; its split into Jeans escape, charge exchange and the polar wind is calibrated, not computed from the plasmasphere",
                    "O+ outflow follows an empirical exponential law in Kp and F10.7 with an assumed escaping fraction; nitrogen ions and molecular ions are not counted",
                    "cosmic-ray responses use effective yield functions calibrated to approximate latitude and solar-cycle effects, anchored to rounded values; no particle transport",
                    "the ozone response is a steady-state catalytic estimate for the polar upper stratosphere; no transport, no mesospheric HOx chemistry, no dynamics",
                    "the solar cycle's solar wind is a climatology from the sunspot number; the example's sunspot record is synthetic"})},
            }},
        };
        std::ofstream out(argv[2]);
        if (!out) throw std::runtime_error(std::string("cannot write output: ") + argv[2]);
        out << output.dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
