#pragma once

#include <string>
#include <vector>

namespace terraflux::weather {

// The solar-cycle forcing that other runs can take from a solar-cycle run (terraflux-run-solar-cycle): the annual
// means of its `forcing_export` (terraflux.solar.forcing/v1) - the total solar irradiance and the global-mean
// surface-temperature response of its energy balance, from the record and then the forecast median.
//
// Used as anomalies: outside the covered years the Sun is its mean (the TSI equals the reference, the response is
// zero). Between annual means both are interpolated linearly (each mean stands for the middle of its year).
struct SolarForcingSeries {
    std::string source;
    std::vector<double> years;                 // middle of each year (year + 0.5)
    std::vector<double> tsi_w_m2;
    std::vector<double> surface_k;
    double reference_tsi_w_m2{1361.0};         // the mean Sun of the solar run
    double tsi_at(double year) const;
    double tsi_anomaly_at(double year) const { return tsi_at(year) - reference_tsi_w_m2; }
    double warming_at(double year) const;
};

// Reads a solar timeline (its `solar.forcing_export`) or a bare terraflux.solar.forcing/v1 document. Throws
// std::invalid_argument for a missing file, another schema, non-consecutive years or values out of range.
SolarForcingSeries load_solar_forcing(const std::string& path);

// Decimal year of a time in seconds since 1970-01-01 00:00 UTC (calendar years of 365 or 366 days).
double decimal_year_from_epoch(double epoch_seconds);

}  // namespace terraflux::weather
