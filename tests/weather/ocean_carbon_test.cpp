#include "terraflux/weather/atmospheric_co2.hpp"
#include "terraflux/weather/marine_indicators.hpp"
#include "terraflux/weather/ocean_biogeochemistry.hpp"
#include "terraflux/weather/ocean_carbon.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace terraflux::weather;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual, double expected, const std::string& message, double tolerance) {
    if (!(std::abs(actual - expected) <= tolerance)) throw std::runtime_error(message + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

template <typename Function>
void require_rejected(Function&& function, const std::string& message) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, message);
}

constexpr double kDay = 86400.0;

// 1. Carbonate chemistry against the published constants and its own identities.
void carbonate_system() {
    const auto c = carbonate_constants(298.15, 35.0);
    require_near(-std::log10(c.k1), 5.8472, "pK1 (Lueker et al. 2000) at 25 degC, S 35", 2e-3);
    require_near(-std::log10(c.k2), 8.9660, "pK2 (Lueker et al. 2000)", 2e-3);
    require_near(std::log(c.k0), -3.5617, "ln K0 (Weiss 1974)", 2e-3);
    require_near(std::log(c.kb), -19.7964, "ln KB (Dickson 1990)", 2e-3);
    require_near(std::log(c.kw), -30.434, "ln KW (Millero 1995)", 5e-3);
    require_near(c.ksp_aragonite / 6.48e-7, 1.0, "aragonite solubility (Mucci 1983)", 0.01);
    require_near(c.ksp_calcite / 4.27e-7, 1.0, "calcite solubility (Mucci 1983)", 0.01);
    require_near(c.total_boron * 1e6, 416.0, "total boron (Uppstrom 1974)", 0.5);
    const auto s = solve_carbonate(2000.0, 2300.0, 298.15, 35.0);
    require_near(s.co2_umol_kg + s.bicarbonate_umol_kg + s.carbonate_umol_kg, 2000.0, "DIC = CO2 + HCO3 + CO3", 1e-9);
    const double h = std::pow(10.0, -s.ph_total);
    const double ta = s.bicarbonate_umol_kg + 2.0 * s.carbonate_umol_kg + 1e6 * (c.total_boron * c.kb / (c.kb + h) + c.kw / h - h);
    require_near(ta, 2300.0, "the solution reproduces the alkalinity", 1e-6);
    require(s.ph_total > 8.0 && s.ph_total < 8.2 && s.pco2_uatm > 250.0 && s.pco2_uatm < 450.0 && s.omega_aragonite > 3.0 && s.omega_aragonite < 4.5,
            "typical surface water: pH about 8.1, pCO2 of a few hundred uatm, aragonite 3-4.5 times supersaturated");
    // The inverse: DIC in equilibrium with a pCO2.
    const double dic = dic_at_equilibrium(420.0, 2300.0, 293.15, 35.0);
    require_near(solve_carbonate(dic, 2300.0, 293.15, 35.0).pco2_uatm, 420.0, "equilibrium DIC gives the pCO2 back", 0.01);
    // Acidification: doubling the pre-industrial CO2 lowers the pH by about 0.3 and aragonite saturation by a third.
    const double d280 = dic_at_equilibrium(280.0, 2300.0, 293.15, 35.0), d560 = dic_at_equilibrium(560.0, 2300.0, 293.15, 35.0);
    const auto a = solve_carbonate(d280, 2300.0, 293.15, 35.0), b = solve_carbonate(d560, 2300.0, 293.15, 35.0);
    require(a.ph_total - b.ph_total > 0.25 && a.ph_total - b.ph_total < 0.35, "doubling CO2 lowers the pH by about 0.3");
    require(b.omega_aragonite < 0.75 * a.omega_aragonite && b.omega_aragonite > 0.5 * a.omega_aragonite, "and aragonite saturation by about a third");
    require((d560 - d280) / d280 < 0.1 * ((560.0 - 280.0) / 280.0) * 1.5, "the buffer: a 100 % rise in pCO2 takes less than 15 % more DIC");
    const double rf = revelle_factor(dic, 2300.0, 293.15, 35.0);
    require(rf > 8.0 && rf < 14.0, "Revelle factor of surface water about 10");
    require(revelle_factor(d560, 2300.0, 293.15, 35.0) > revelle_factor(d280, 2300.0, 293.15, 35.0), "the buffer weakens as the ocean takes up CO2");
    // Warming at constant DIC and TA raises pCO2 by about 4.2 % per kelvin (Takahashi et al. 1993).
    const double p20 = solve_carbonate(2000.0, 2300.0, 293.15, 35.0).pco2_uatm, p21 = solve_carbonate(2000.0, 2300.0, 294.15, 35.0).pco2_uatm;
    require(p21 / p20 - 1.0 > 0.035 && p21 / p20 - 1.0 < 0.05, "warming raises pCO2 by about 4 %/K");
    require_rejected([&] { solve_carbonate(-5.0, 2300.0, 293.15, 35.0); }, "negative DIC rejected");
    require_rejected([&] { carbonate_constants(330.0, 35.0); }, "hot water rejected");
}

// 2. Gas exchange and the metabolic index.
void exchange_and_metabolism() {
    require_near(co2_schmidt_number(293.15), 668.0, "CO2 Schmidt number at 20 degC (Wanninkhof 2014)", 1.0);
    require_near(o2_schmidt_number(293.15), 568.0, "O2 Schmidt number at 20 degC", 1.0);
    require_near(gas_transfer_velocity_m_s(10.0, 660.0) * 3.6e5, 25.1, "k660 = 0.251 U^2 cm/h", 1e-9);
    require(gas_transfer_velocity_m_s(5.0, 660.0) < 0.3 * gas_transfer_velocity_m_s(10.0, 660.0), "calm seas exchange far less gas");
    MetabolicIndexConfig m;
    const double sat15 = oxygen_solubility_umol_kg(288.15, 35.0);
    require_near(oxygen_partial_pressure_kpa(sat15, 288.15, 35.0), 21.22, "saturated water carries the atmosphere's pO2", 0.01);
    require_near(metabolic_index(sat15, 288.15, 35.0, m), oxygen_partial_pressure_kpa(sat15, 288.15, 35.0) / 2.0, "at the reference temperature Phi = pO2 / pcrit", 1e-9);
    require(metabolic_index(60.0, 288.15, 35.0, m) < m.ecological_threshold, "hypoxic water cannot sustain activity");
    const double sat28 = oxygen_solubility_umol_kg(301.15, 35.0);
    require(metabolic_index(0.6 * sat28, 301.15, 35.0, m) < metabolic_index(0.6 * sat15, 288.15, 35.0, m) * 0.5, "warm water: less oxygen and a higher demand");
}

// A single-column (or strip) configuration of 1 km cells (one row of sea, one of land).
MarineBgcConfig column_config(std::size_t nx, double floor) {
    MarineBgcConfig c;
    const std::size_t width = std::max<std::size_t>(nx, 2);
    c.grid.nx = width; c.grid.ny = 2;   // the sea in the first nx cells of the first row, land elsewhere
    c.grid.spacing_east_m = 1000.0; c.grid.spacing_north_m = 1000.0;
    c.sea.assign(2 * width, 0);
    c.seafloor_depth_m.assign(2 * width, 0.0);
    for (std::size_t i = 0; i < nx; ++i) { c.sea[i] = 1; c.seafloor_depth_m[i] = floor; }
    c.level_bottoms_m = {5.0, 10.0, 20.0, 30.0, 40.0, 60.0, 100.0, 200.0};
    c.ventilation_depth_m = 5000.0;
    c.horizontal_diffusivity_m2_s = 0.0;
    c.vertical_diffusivity_m2_s = 1.0e-5;
    return c;
}

MarineBgcForcing column_forcing(const MarineBgcConfig& c, double surface_k, double bottom_k, double mld, double wind, double sun, double co2) {
    MarineBgcForcing f;
    const std::size_t n = c.sea.size(), L = c.level_bottoms_m.size();
    f.temperature_k.assign(n * L, 0.0);
    f.salinity_psu.assign(n * L, 35.0);
    for (std::size_t i = 0; i < n; ++i) for (std::size_t k = 0; k < L; ++k) f.temperature_k[i * L + k] = surface_k + (bottom_k - surface_k) * static_cast<double>(k) / static_cast<double>(L - 1);
    f.mixed_layer_depth_m.assign(n, mld);
    f.current_east_m_s.assign(n, 0.0);
    f.current_north_m_s.assign(n, 0.0);
    f.wind_speed_m_s.assign(n, wind);
    f.shortwave_w_m2.assign(n, sun);
    f.atmospheric_pco2_uatm.assign(n, co2);
    return f;
}

// 3. The column: conservation, production and respiration, stratification and dead zones, air-sea exchange.
void column_budgets_and_dead_zones() {
    // A 40 m shelf column without gas exchange: nitrogen, carbon and alkalinity are conserved; the O2 produced in
    // the light exceeds the O2 respired by exactly the demand nitrate (denitrification) or sulfide had to meet.
    auto c = column_config(1, 40.0);
    c.nitrate = {8.0, 20.0, 50.0};   // a eutrophied shelf
    c.light_attenuation_per_m = 0.15;  // turbid coastal water: the light does not reach the seafloor
    auto f = column_forcing(c, 301.15, 295.15, 8.0, 0.0, 250.0, 420.0);
    auto s = initialize_bgc(c, f);
    require(s.levels[0] == 5, "a 40 m shelf has five levels");
    const double n0 = s.nitrogen_inventory_mol, c0 = s.carbon_inventory_mol, o0 = s.oxygen_inventory_mol, bottom0 = s.oxygen[4];
    double ta0 = 0.0;
    for (std::size_t k = 0; k < 5; ++k) ta0 += s.alkalinity[k] * (k < 4 ? (k < 2 ? 5.0 : 10.0) : 10.0);
    for (int step = 0; step < 24 * 60; ++step) s = advance_bgc(s, f, c, 3600.0);
    require_near(s.nitrogen_inventory_mol + s.denitrified_nitrogen_mol, n0, "nitrogen: nitrate + denitrified N is conserved", 1e-9 * n0);
    require_near(s.carbon_inventory_mol, c0, "carbon is conserved (respiration and CaCO3 dissolution at the seafloor)", 1e-9 * c0);
    require_near(s.oxygen_inventory_mol - o0, s.denitrified_nitrogen_mol / c.denitrification_nitrate_per_oxygen + s.sulfidic_oxygen_demand_mol, "O2 made in the light and never respired = the demand nitrate and sulfide met instead", 1e-6 * o0);
    require(s.denitrified_nitrogen_mol > 0.0, "the bottom water turned to nitrate once its oxygen was gone");
    double ta = 0.0;
    for (std::size_t k = 0; k < 5; ++k) ta += s.alkalinity[k] * (k < 4 ? (k < 2 ? 5.0 : 10.0) : 10.0);
    require_near(ta, ta0, "alkalinity is conserved", 1e-9 * ta0);
    // Under a shallow, warm mixed layer the bottom water is starved: a dead zone; the surface stays oxygenated.
    require(s.oxygen[4] < c.hypoxia_umol_kg && s.oxygen[4] < 0.5 * bottom0, "two months of stratified production drain the bottom water of oxygen");
    require(s.oxygen[0] > 150.0, "the sunlit surface produces oxygen");
    require(s.production_mol_c_m2_s[0] > 0.0 && s.benthic_mol_c_m2_s[0] > 0.0 && s.export_mol_c_m2_s[0] == 0.0, "on a shelf the sinking matter is respired at the seafloor");
    const auto habitat = assess_habitat(s, f, c);
    require(habitat.dead_zone[0] == 1 && habitat.column_class[0] >= 2 && habitat.habitable_fraction[0] < 0.8, "the column is classified a dead zone");
    require(habitat.level_class[0] <= 1 && habitat.level_class[4] >= 2, "alive at the surface, dead at the bottom");
    // Winter: the mixed layer reaches the seafloor and the wind ventilates the whole column.
    auto winter = column_forcing(c, 290.15, 290.15, 40.0, 10.0, 50.0, 420.0);
    for (int step = 0; step < 24 * 20; ++step) s = advance_bgc(s, winter, c, 3600.0);
    require(s.oxygen[4] > 180.0, "deep winter mixing re-oxygenates the bottom water");
    require(assess_habitat(s, winter, c).dead_zone[0] == 0, "the dead zone is seasonal");
}

void air_sea_carbon_and_export() {
    // A deep open column under a high-CO2 atmosphere: the ocean takes up CO2 until its surface is in equilibrium.
    auto c = column_config(1, 3000.0);
    c.nitrate = {0.0, 0.0, 100.0};
    c.max_growth_per_day = 0.0;
    c.surface_dic_from_atmosphere = true;
    auto f = column_forcing(c, 293.15, 283.15, 30.0, 8.0, 200.0, 420.0);
    auto s = initialize_bgc(c, f);
    require_near(solve_carbonate(s.dic[0], s.alkalinity[0], 293.15, 35.0).pco2_uatm, 420.0, "the surface starts in equilibrium with the air", 0.5);
    auto high = column_forcing(c, 293.15, 283.15, 30.0, 8.0, 200.0, 800.0);
    s = advance_bgc(s, high, c, 3600.0);
    require(s.co2_flux_mol_m2_s[0] > 0.0, "a higher atmosphere pushes CO2 into the sea");
    for (int step = 0; step < 24 * 365; ++step) s = advance_bgc(s, high, c, 3600.0);
    const auto surface = solve_carbonate(s.dic[0], s.alkalinity[0], 293.15, 35.0);
    require_near(surface.pco2_uatm, 800.0, "after a year the mixed layer is close to equilibrium with the air (CO2 equilibrates over months)", 25.0);
    require(s.air_sea_carbon_mol > 0.0 && surface.ph_total < 7.9, "the uptake acidifies the sea");
    require_near(s.carbon_inventory_mol - initialize_bgc(c, f).carbon_inventory_mol, s.air_sea_carbon_mol, "the carbon gained is what crossed the surface", 1e-6 * s.carbon_inventory_mol);
    // Oxygen relaxes to saturation.
    require_near(s.oxygen[0], oxygen_solubility_umol_kg(293.15, 35.0), "the surface is saturated with oxygen", 0.5);
    // Export: in deep water the sinking matter leaves the column and takes its nitrogen along.
    auto p = column_config(1, 3000.0);
    p.nitrate = {10.0, 20.0, 100.0};
    auto fp = column_forcing(p, 293.15, 283.15, 30.0, 0.0, 250.0, 420.0);
    auto sp = initialize_bgc(p, fp);
    const double n0 = sp.nitrogen_inventory_mol;
    for (int step = 0; step < 24 * 30; ++step) sp = advance_bgc(sp, fp, p, 3600.0);
    require(sp.export_carbon_mol > 0.0 && sp.export_mol_c_m2_s[0] >= 0.0, "organic matter leaves the column bottom");
    require_near(sp.nitrogen_inventory_mol, n0 - sp.export_nitrogen_mol - sp.denitrified_nitrogen_mol, "the nitrogen lost is what sank out", 1e-6 * n0);
    require(sp.nitrate[0] < 0.5 * initialize_bgc(p, fp).nitrate[0], "the sunlit surface is depleted of nitrate");
}

void transport_and_rivers() {
    // Advective transport keeps a uniform field uniform even in a converging current, and carries a blob downstream.
    auto c = column_config(20, 30.0);
    c.horizontal_diffusivity_m2_s = 0.0;
    c.max_growth_per_day = 0.0;
    auto f = column_forcing(c, 293.15, 293.15, 30.0, 0.0, 0.0, 420.0);
    for (std::size_t i = 0; i < 20; ++i) f.current_east_m_s[i] = 0.1 - 0.004 * static_cast<double>(i);   // converging (the land row stays still)
    auto s = initialize_bgc(c, f);
    for (int step = 0; step < 48; ++step) s = advance_bgc(s, f, c, 3600.0);
    const std::size_t L = c.level_bottoms_m.size();
    for (std::size_t i = 1; i < 20; ++i) require_near(s.nitrate[i * L + 2], s.nitrate[2], "a uniform field stays uniform", 1e-9);
    // In a closed channel the current has nowhere to go (the transports are made free of divergence); open at
    // both ends, a uniform current flows through and carries a blob downstream.
    auto through = c;
    through.open_boundary.assign(c.sea.size(), 0.0);
    through.open_boundary[0] = through.open_boundary[19] = 1.0;
    auto blob = initialize_bgc(through, f);
    for (std::size_t k = 0; k < blob.levels[3]; ++k) blob.nitrate[3 * L + k] = 50.0;
    auto moving = column_forcing(c, 293.15, 293.15, 30.0, 0.0, 0.0, 420.0);
    for (std::size_t i = 0; i < 20; ++i) moving.current_east_m_s[i] = 0.05;
    for (int step = 0; step < 48; ++step) blob = advance_bgc(blob, moving, through, 3600.0);
    double centre = 0.0, mass = 0.0;
    for (std::size_t i = 0; i < 20; ++i) { const double excess = blob.nitrate[i * L] - c.nitrate.at(2.5); centre += excess * static_cast<double>(i); mass += excess; }
    require(centre / mass > 3.0 + 5.0, "the blob moves about 8.6 km downstream in two days at 5 cm/s");
    // A nitrate-rich river fertilises the sea next to its mouth.
    auto r = column_config(20, 30.0);
    r.horizontal_diffusivity_m2_s = 50.0;
    r.nitrate = {0.2, 5.0, 100.0};
    r.light_attenuation_per_m = 0.15;
    auto fr = column_forcing(r, 298.15, 293.15, 5.0, 3.0, 250.0, 420.0);
    MarineBgcRiver river;
    river.cell = 0;
    river.discharge_m3_s = 10.0;
    river.chemistry.nitrate_umol_kg = 120.0;
    fr.rivers.push_back(river);
    auto sr = initialize_bgc(r, fr);
    for (int step = 0; step < 24 * 20; ++step) sr = advance_bgc(sr, fr, r, 3600.0);
    require(sr.production_mol_c_m2_s[0] > 1.1 * sr.production_mol_c_m2_s[19] && sr.nitrate[0] > 10.0 * sr.nitrate[19 * L], "nitrate and production are highest at the river mouth");
    require(sr.river_nitrogen_mol > 0.0, "the river nitrogen is counted");
    const auto h = assess_habitat(sr, fr, r);
    require(h.bottom_oxygen_umol_kg[0] < h.bottom_oxygen_umol_kg[19] - 10.0, "the bottom water under the plume loses its oxygen first");
    require(sr.boundary_nitrogen_mol < 0.0, "in a closed channel the river water pushes the same volume of sea water out");
}

void transport_conservation() {
    // A shelf that deepens offshore (10 m to 200 m) under a current that crosses the isobaths and converges, with a
    // river at the coast and the open sea at the far end. The transport is conservative: every mole is accounted
    // for by the river and the exchange through the open boundary, and a uniform field stays uniform.
    MarineBgcConfig c = column_config(24, 10.0);
    for (std::size_t i = 0; i < 24; ++i) c.seafloor_depth_m[i] = 10.0 + 190.0 * static_cast<double>(i) / 23.0;
    c.horizontal_diffusivity_m2_s = 20.0;
    c.max_growth_per_day = 0.0;
    c.open_boundary.assign(c.sea.size(), 0.0);
    c.open_boundary[23] = 1.0;
    c.nitrate = {2.0, 30.0, 60.0};
    auto f = column_forcing(c, 293.15, 285.15, 10.0, 0.0, 0.0, 420.0);
    for (std::size_t i = 0; i < 24; ++i) f.current_east_m_s[i] = -0.08 + 0.003 * static_cast<double>(i);   // onshore, converging
    f.active_layer_thickness_m.assign(c.sea.size(), 60.0);
    MarineBgcRiver river;
    river.cell = 0;
    river.discharge_m3_s = 5.0;
    river.chemistry.nitrate_umol_kg = 100.0;
    f.rivers.push_back(river);
    auto s = initialize_bgc(c, f);
    const double n0 = s.nitrogen_inventory_mol, c0 = s.carbon_inventory_mol;
    for (int step = 0; step < 24 * 10; ++step) s = advance_bgc(s, f, c, 3600.0);
    require_near(s.nitrogen_inventory_mol - n0, s.river_nitrogen_mol + s.boundary_nitrogen_mol, "nitrogen: change = river + open boundary", 1e-9 * n0);
    require_near(s.carbon_inventory_mol - c0, s.river_carbon_mol + s.boundary_carbon_mol, "carbon: change = river + open boundary", 1e-9 * c0);
    require(std::abs(s.transport_nitrogen_mol) < 1e-9 * n0 && std::abs(s.transport_carbon_mol) < 1e-9 * c0, "no transport residual");
    require(s.river_nitrogen_mol > 0.0 && s.boundary_nitrogen_mol != 0.0, "the river and the open boundary both act");
    // Uniform water (river of the same water) stays uniform despite the convergence, the slope and the river.
    auto u = c;
    u.nitrate = {7.0, 7.0, 100.0}; u.dic = {2000.0, 2000.0, 100.0}; u.alkalinity = {2300.0, 2300.0, 100.0}; u.oxygen = {200.0, 200.0, 100.0};
    u.surface_dic_from_atmosphere = false;
    auto fu = f;
    fu.rivers[0].chemistry = {7.0, 2000.0, 2300.0, 1.0};
    fu.temperature_k.assign(fu.temperature_k.size(), 293.15);
    auto su = initialize_bgc(u, fu);
    const std::size_t L = u.level_bottoms_m.size();
    for (std::size_t ik = 0; ik < su.oxygen.size(); ++ik) su.oxygen[ik] = su.levels[ik / L] > ik % L ? 200.0 : 0.0;
    su.reference_oxygen = su.oxygen;
    for (int step = 0; step < 24 * 5; ++step) su = advance_bgc(su, fu, u, 3600.0);
    for (std::size_t i = 0; i < 24; ++i) {
        for (std::size_t k = 0; k < su.levels[i]; ++k) {
            require_near(su.nitrate[i * L + k], 7.0, "uniform nitrate stays uniform", 1e-6);
            require_near(su.dic[i * L + k], 2000.0, "uniform DIC stays uniform", 1e-6);
        }
    }
}

void atmosphere_co2() {
    AtmosphericCo2Config c;
    c.grid.nx = 30; c.grid.ny = 11;
    c.grid.spacing_east_m = 10000.0; c.grid.spacing_north_m = 10000.0;
    c.horizontal_diffusivity_m2_s = 0.0;
    const std::size_t count = 330;
    AtmosphericCo2Forcing f;
    f.background_ppm = 420.0;
    f.wind_east_m_s.assign(count, 0.0); f.wind_north_m_s.assign(count, 0.0);
    f.emission_mol_m2_s.assign(count, 0.0); f.land_uptake_mol_m2_s.assign(count, 0.0); f.ocean_uptake_mol_m2_s.assign(count, 0.0);
    const std::size_t city = 5 * 30 + 5;
    f.emission_mol_m2_s[city] = 2.0e-4;   // a large city on a 10 km cell: about 10 Mt CO2 per year
    auto s = initialize_atmospheric_co2(c, 420.0);
    for (int step = 0; step < 24 * 20; ++step) s = advance_atmospheric_co2(s, f, c, 3600.0);
    const double air = boundary_layer_air_mol_m2(c);
    const double add = 2.0e-4 * 3600.0 * 1.0e6 / air, keep = std::exp(-3600.0 / c.ventilation_time_s);
    require_near(s.ppm[city] - 420.0, add * keep / (1.0 - keep), "still air: the excess settles where emission balances ventilation (the step's exact balance)", 0.1);
    require_near(s.ppm[city] - 420.0, 2.0e-4 * c.ventilation_time_s * 1.0e6 / air, "about emission x ventilation time / air column", 0.015 * (s.ppm[city] - 420.0));
    require_near(s.emitted_mol - s.ventilation_mol - s.transport_mol, s.inventory_excess_mol, "the CO2 budget closes", 1e-6 * s.emitted_mol);
    // Wind carries the plume downstream; uniform background air stays uniform.
    f.wind_east_m_s.assign(count, 5.0);
    auto w = initialize_atmospheric_co2(c, 420.0);
    for (int step = 0; step < 24 * 5; ++step) w = advance_atmospheric_co2(w, f, c, 3600.0);
    require(w.ppm[city + 6] > 420.5 && w.ppm[city + 6] > w.ppm[city - 2] + 0.4, "the plume drifts downwind");
    require_near(w.ppm[2 * 30 + 20], 420.0, "air away from the plume keeps the background", 1e-9);
    require_near(w.emitted_mol - w.ventilation_mol + w.transport_mol, w.inventory_excess_mol, "with wind the budget closes through the boundary outflow", 1e-6 * w.emitted_mol);
    require(w.transport_mol < 0.0, "the wind carries part of the plume out of the region");
    // Ocean uptake lowers the CO2 over the sea.
    f.wind_east_m_s.assign(count, 0.0);
    f.emission_mol_m2_s.assign(count, 0.0);
    f.ocean_uptake_mol_m2_s.assign(count, 1.0e-7);
    auto o = initialize_atmospheric_co2(c, 420.0);
    for (int step = 0; step < 24 * 10; ++step) o = advance_atmospheric_co2(o, f, c, 3600.0);
    require(o.ppm[100] < 420.0 && o.ocean_uptake_mol > 0.0, "the sea draws CO2 out of the air above it");
    // A rising background (a scenario) lifts the regional air; the budget counts the shift of the reference.
    f.ocean_uptake_mol_m2_s.assign(count, 0.0);
    f.emission_mol_m2_s[city] = 2.0e-4;
    f.wind_east_m_s.assign(count, 3.0);
    auto r = initialize_atmospheric_co2(c, 420.0);
    for (int step = 0; step < 24 * 10; ++step) {
        f.background_ppm = 420.0 + 0.01 * step;
        r = advance_atmospheric_co2(r, f, c, 3600.0);
    }
    require_near(r.emitted_mol - r.ventilation_mol + r.transport_mol + r.background_shift_mol, r.inventory_excess_mol, "the budget closes under a rising background", 1e-6 * r.emitted_mol);
    require(r.background_shift_mol < 0.0 && r.ppm[2 * 30 + 20] > 421.5, "the air follows the rising background");
    require_rejected([&] { initialize_atmospheric_co2(c, 50.0); }, "background out of range");
}

void contract() {
    auto c = column_config(2, 30.0);
    require_rejected([&] { auto bad = c; bad.level_bottoms_m = {10.0, 5.0}; validate_bgc_config(bad); }, "levels must deepen");
    require_rejected([&] { auto bad = c; bad.seafloor_depth_m[0] = -3.0; validate_bgc_config(bad); }, "seafloor must be positive");
    require_rejected([&] { auto bad = c; bad.anoxia_umol_kg = 100.0; validate_bgc_config(bad); }, "anoxia below hypoxia");
    auto f = column_forcing(c, 293.15, 293.15, 10.0, 5.0, 100.0, 420.0);
    auto s = initialize_bgc(c, f);
    auto bad = f;
    bad.mixed_layer_depth_m.pop_back();
    require_rejected([&] { advance_bgc(s, bad, c, 3600.0); }, "forcing must match the grid");
    bad = f;
    MarineBgcRiver river;
    river.cell = 3;   // a land cell
    bad.rivers.push_back(river);
    require_rejected([&] { advance_bgc(s, bad, c, 3600.0); }, "rivers enter sea cells");
    require_rejected([&] { advance_bgc(s, f, c, 30.0 * kDay); }, "timestep too long");
    // A sliver of water above the seafloor joins the level above (levels 30-40 and 40-60 m here).
    for (const auto& [floor, expected] : std::vector<std::pair<double, std::size_t>>{{40.5, 5}, {44.0, 5}, {46.0, 6}, {60.0, 6}}) {
        auto g = column_config(1, floor);
        const auto state = initialize_bgc(g, column_forcing(g, 293.15, 293.15, 10.0, 5.0, 100.0, 420.0));
        require(state.levels[0] == expected, "thin bottom remainders merge into the level above");
    }
    require_rejected([&] { auto o = c; o.open_boundary.assign(c.sea.size(), 2.0); validate_bgc_config(o); }, "open boundary strength within 0-1");
}

}  // namespace

int main() {
    try {
        carbonate_system();
        exchange_and_metabolism();
        column_budgets_and_dead_zones();
        air_sea_carbon_and_export();
        transport_and_rivers();
    transport_conservation();
        atmosphere_co2();
        contract();
    } catch (const std::exception& error) {
        std::cerr << "ocean carbon test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ocean carbon cases passed\n";
    return 0;
}
