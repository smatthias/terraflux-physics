#include "terraflux/weather/precipitation_accumulator.hpp"
#include "utc_time.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace terraflux::weather {
namespace {

using detail::utc_seconds;

using LocationKey = std::tuple<std::string, double, double>;
using SamplesAtLocation = std::map<double, const WeatherObservation*>;

}  // namespace

std::vector<WeatherObservation> accumulate_rainfall(
    const std::vector<WeatherObservation>& rate_samples) {
    if (rate_samples.empty()) {
        throw std::invalid_argument("at least two precipitation-rate timestamps are required");
    }

    std::map<double, std::string> times;
    std::map<LocationKey, SamplesAtLocation> by_location;
    const WeatherObservation& reference = rate_samples.front();

    for (const WeatherObservation& sample : rate_samples) {
        if (sample.variable != "precipitation_rate" || sample.units != "mm/h") {
            throw std::invalid_argument(
                "rainfall accumulation requires precipitation_rate observations in mm/h");
        }
        if (sample.source_id != reference.source_id ||
            sample.source_kind != reference.source_kind ||
            sample.product_id != reference.product_id ||
            sample.interpretation != reference.interpretation ||
            sample.derivation != reference.derivation) {
            throw std::invalid_argument(
                "all rate samples must have matching source and derivation metadata");
        }
        if (sample.location.crs.empty() || !std::isfinite(sample.location.x) ||
            !std::isfinite(sample.location.y)) {
            throw std::invalid_argument("rate sample location must have a CRS and finite coordinates");
        }
        const double time = utc_seconds(sample.valid_time_utc);
        times.emplace(time, sample.valid_time_utc);
        auto& samples = by_location[
            LocationKey{sample.location.crs, sample.location.x, sample.location.y}];
        const auto inserted = samples.emplace(time, &sample);
        if (!inserted.second) {
            inserted.first->second = nullptr;
        }
    }

    if (times.size() < 2) {
        throw std::invalid_argument("rainfall accumulation requires at least two distinct times");
    }
    const double total_seconds = times.rbegin()->first - times.begin()->first;
    if (!(total_seconds > 0.0) || !std::isfinite(total_seconds)) {
        throw std::invalid_argument("precipitation sample times must span a positive interval");
    }

    std::vector<double> ordered_times;
    std::vector<std::string> ordered_labels;
    ordered_times.reserve(times.size());
    ordered_labels.reserve(times.size());
    for (const auto& item : times) {
        ordered_times.push_back(item.first);
        ordered_labels.push_back(item.second);
    }

    std::vector<WeatherObservation> accumulated;
    accumulated.reserve(by_location.size());
    for (const auto& location_entry : by_location) {
        const auto& samples = location_entry.second;
        const WeatherObservation* base = nullptr;
        for (const auto& sample : samples) {
            if (sample.second != nullptr) {
                base = sample.second;
                break;
            }
        }
        if (base == nullptr) {
            throw std::invalid_argument("each location must have at least one unambiguous sample");
        }

        WeatherObservation output = *base;
        output.valid_time_utc = ordered_labels.back();
        output.period_start_utc = ordered_labels.front();
        output.variable = "precipitation_accumulation";
        output.units = "mm";
        output.interpretation = "derived_temporal_integration";
        output.confidence.reset();
        output.derivation =
            "trapezoidal integration of adjacent precipitation_rate samples in mm/h "
            "over [" + ordered_labels.front() + ", " + ordered_labels.back() + "]";
        output.value.reset();
        output.reason.clear();

        bool has_ambiguous = false;
        bool has_missing = false;
        std::string quality_reason;
        std::vector<double> rates;
        rates.reserve(ordered_times.size());
        for (std::size_t index = 0; index < ordered_times.size(); ++index) {
            const auto found = samples.find(ordered_times[index]);
            if (found == samples.end()) {
                has_missing = true;
                if (quality_reason.empty()) {
                    quality_reason = "no rate sample at " + ordered_labels[index];
                }
                continue;
            }
            if (found->second == nullptr) {
                has_ambiguous = true;
                quality_reason = "duplicate rate samples at " + ordered_labels[index];
                continue;
            }
            const WeatherObservation& sample = *found->second;
            if (sample.status == ObservationStatus::ambiguous) {
                has_ambiguous = true;
                quality_reason = sample.reason.empty()
                    ? "ambiguous rate sample at " + ordered_labels[index]
                    : sample.reason;
            } else if (sample.status == ObservationStatus::no_data) {
                has_missing = true;
                if (quality_reason.empty()) {
                    quality_reason = sample.reason.empty()
                        ? "no rate data at " + ordered_labels[index]
                        : sample.reason;
                }
            } else {
                if (!sample.value || !std::isfinite(*sample.value) || *sample.value < 0.0) {
                    throw std::invalid_argument(
                        "decoded precipitation rates must be finite and non-negative");
                }
                rates.push_back(*sample.value);
            }
        }

        if (has_ambiguous) {
            output.status = ObservationStatus::ambiguous;
            output.reason = quality_reason.empty()
                ? "ambiguous precipitation-rate interval"
                : quality_reason;
        } else if (has_missing || rates.size() != ordered_times.size()) {
            output.status = ObservationStatus::no_data;
            output.reason = quality_reason.empty()
                ? "precipitation-rate interval is incomplete"
                : quality_reason;
        } else {
            double depth_mm = 0.0;
            for (std::size_t index = 1; index < ordered_times.size(); ++index) {
                const double delta_hours =
                    (ordered_times[index] - ordered_times[index - 1]) / 3600.0;
                depth_mm += 0.5 * (rates[index - 1] + rates[index]) * delta_hours;
            }
            if (!std::isfinite(depth_mm)) {
                throw std::overflow_error("precipitation accumulation is not finite");
            }
            output.value = depth_mm;
            output.status = ObservationStatus::decoded;
        }
        accumulated.push_back(std::move(output));
    }

    return accumulated;
}

}  // namespace terraflux::weather
