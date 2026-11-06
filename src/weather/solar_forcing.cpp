#include "terraflux/weather/solar_forcing.hpp"

#include "utc_time.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace terraflux::weather {

namespace {

double interpolate(const std::vector<double>& x, const std::vector<double>& y, double at, double outside) {
    if (x.empty() || at < x.front() || at > x.back()) return outside;
    std::size_t k = 1;
    while (k < x.size() && x[k] < at) ++k;
    if (k >= x.size()) return y.back();
    const double w = (at - x[k - 1]) / (x[k] - x[k - 1]);
    return y[k - 1] + (y[k] - y[k - 1]) * w;
}

}  // namespace

double SolarForcingSeries::tsi_at(double year) const { return interpolate(years, tsi_w_m2, year, reference_tsi_w_m2); }
double SolarForcingSeries::warming_at(double year) const { return interpolate(years, surface_k, year, 0.0); }

SolarForcingSeries load_solar_forcing(const std::string& path) {
    using Json = nlohmann::json;
    std::ifstream file(path);
    if (!file) throw std::invalid_argument("cannot open the solar-cycle timeline: " + path);
    Json document;
    try { document = Json::parse(file); } catch (const Json::exception& error) { throw std::invalid_argument("invalid solar-cycle JSON " + path + ": " + error.what()); }
    const Json* e = &document;
    if (document.is_object() && document.contains("solar")) {
        const Json& s = document.at("solar");
        if (!s.is_object() || s.value("schema_version", "") != "terraflux.solar.cycle/v1" || !s.contains("forcing_export")) throw std::invalid_argument("the solar block of " + path + " is not terraflux.solar.cycle/v1 with a forcing_export");
        e = &s.at("forcing_export");
    }
    if (!e->is_object() || e->value("schema_version", "") != "terraflux.solar.forcing/v1") throw std::invalid_argument(path + " holds no terraflux.solar.forcing/v1 forcing");
    SolarForcingSeries out;
    out.source = path;
    const Json& years = e->at("years");
    const Json& tsi = e->at("tsi_w_m2");
    const Json& surface = e->at("surface_k");
    if (!years.is_array() || years.size() < 2 || tsi.size() != years.size() || surface.size() != years.size()) throw std::invalid_argument("the solar forcing needs matching annual years, tsi_w_m2 and surface_k");
    for (std::size_t k = 0; k < years.size(); ++k) {
        if (!years[k].is_number_integer() || (k > 0 && years[k].get<long long>() != years[k - 1].get<long long>() + 1)) throw std::invalid_argument("the solar forcing years must be consecutive integers");
        const double s = tsi[k].get<double>(), t = surface[k].get<double>();
        if (!(s > 1300.0 && s < 1400.0) || !(std::abs(t) < 2.0)) throw std::invalid_argument("solar forcing values out of range (TSI 1300-1400 W/m2, response within 2 K)");
        out.years.push_back(static_cast<double>(years[k].get<long long>()) + 0.5);
        out.tsi_w_m2.push_back(s);
        out.surface_k.push_back(t);
    }
    out.reference_tsi_w_m2 = e->at("reference_tsi_w_m2").get<double>();
    if (!(out.reference_tsi_w_m2 > 1300.0 && out.reference_tsi_w_m2 < 1400.0)) throw std::invalid_argument("the solar forcing's reference TSI must lie within 1300-1400 W/m2");
    return out;
}

double decimal_year_from_epoch(double epoch_seconds) {
    using detail::days_from_civil;
    const double days = std::floor(epoch_seconds / 86400.0);
    int year = 1970 + static_cast<int>(std::floor(days / 365.2425));
    while (static_cast<double>(days_from_civil(year, 1, 1)) > days) --year;
    while (static_cast<double>(days_from_civil(year + 1, 1, 1)) <= days) ++year;
    const double start = static_cast<double>(days_from_civil(year, 1, 1)) * 86400.0, end = static_cast<double>(days_from_civil(year + 1, 1, 1)) * 86400.0;
    return year + (epoch_seconds - start) / (end - start);
}

}  // namespace terraflux::weather
