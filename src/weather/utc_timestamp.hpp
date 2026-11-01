#pragma once

#include <cctype>
#include <stdexcept>
#include <string>

namespace terraflux::weather::detail {

inline bool is_leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

inline int parse_timestamp_digits(
    const std::string& value,
    std::size_t offset,
    std::size_t length) {
    int result = 0;
    for (std::size_t i = offset; i < offset + length; ++i) {
        const unsigned char character = static_cast<unsigned char>(value[i]);
        if (!std::isdigit(character)) {
            throw std::invalid_argument(
                "valid_time_utc must be an ISO-8601 UTC timestamp");
        }
        result = result * 10 + (character - '0');
    }
    return result;
}

// Parse canonical UTC timestamps without timezone offsets:
// YYYY-MM-DDTHH:MM:SSZ, optionally with one to nine fractional digits.
inline double utc_timestamp_seconds(const std::string& value) {
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':') {
        throw std::invalid_argument(
            "valid_time_utc must be an ISO-8601 UTC timestamp");
    }
    const int year = parse_timestamp_digits(value, 0, 4);
    const int month = parse_timestamp_digits(value, 5, 2);
    const int day = parse_timestamp_digits(value, 8, 2);
    const int hour = parse_timestamp_digits(value, 11, 2);
    const int minute = parse_timestamp_digits(value, 14, 2);
    const int second = parse_timestamp_digits(value, 17, 2);
    if (year < 1 || month < 1 || month > 12 || hour > 23 ||
        minute > 59 || second > 59) {
        throw std::invalid_argument(
            "valid_time_utc has an out-of-range date or time");
    }

    static constexpr int days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int month_days = days_per_month[month - 1];
    if (month == 2 && is_leap_year(year)) {
        ++month_days;
    }
    if (day < 1 || day > month_days) {
        throw std::invalid_argument(
            "valid_time_utc has an invalid calendar date");
    }

    std::size_t suffix = 19;
    double fraction = 0.0;
    if (suffix < value.size() && value[suffix] == '.') {
        ++suffix;
        std::size_t digits = 0;
        double scale = 0.1;
        while (suffix < value.size() && value[suffix] != 'Z') {
            const unsigned char character =
                static_cast<unsigned char>(value[suffix]);
            if (!std::isdigit(character) || digits == 9) {
                throw std::invalid_argument(
                    "valid_time_utc fractional seconds must have one to nine digits");
            }
            fraction += (character - '0') * scale;
            scale *= 0.1;
            ++digits;
            ++suffix;
        }
        if (digits == 0) {
            throw std::invalid_argument(
                "valid_time_utc fraction must contain digits");
        }
    }
    if (suffix + 1 != value.size() || value[suffix] != 'Z') {
        throw std::invalid_argument(
            "time-indexed observations require a UTC timestamp ending in Z");
    }

    long long days = 0;
    for (int y = 1970; y < year; ++y) {
        days += is_leap_year(y) ? 366 : 365;
    }
    for (int y = year; y < 1970; ++y) {
        days -= is_leap_year(y) ? 366 : 365;
    }
    for (int m = 1; m < month; ++m) {
        days += days_per_month[m - 1];
        if (m == 2 && is_leap_year(year)) {
            ++days;
        }
    }
    days += day - 1;

    return static_cast<double>(days) * 86400.0 + hour * 3600.0 +
           minute * 60.0 + second + fraction;
}

}  // namespace terraflux::weather::detail
