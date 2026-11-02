#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
namespace terraflux::weather::detail {
inline int digits(const std::string& value, std::size_t offset, std::size_t count) {
    int result = 0;
    for (std::size_t index = offset; index < offset + count; ++index) {
        if (index >= value.size() || value[index] < '0' || value[index] > '9') {
            throw std::invalid_argument("valid_time_utc must use ISO-8601 UTC format");
        }
        result = result * 10 + (value[index] - '0');
    }
    return result;
}

inline bool leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

inline std::int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned adjusted_month = month > 2 ? month - 3 : month + 9;
    const unsigned day_of_year =
        (153 * adjusted_month + 2) / 5 + day - 1;
    const unsigned day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return static_cast<std::int64_t>(era) * 146097 +
           static_cast<std::int64_t>(day_of_era) - 719468;
}

inline double utc_seconds(const std::string& value) {
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':') {
        throw std::invalid_argument("valid_time_utc must use ISO-8601 UTC format ending in Z");
    }
    const int year = digits(value, 0, 4);
    const int month = digits(value, 5, 2);
    const int day = digits(value, 8, 2);
    const int hour = digits(value, 11, 2);
    const int minute = digits(value, 14, 2);
    const int second = digits(value, 17, 2);
    if (year < 1 || month < 1 || month > 12 || hour > 23 ||
        minute > 59 || second > 59) {
        throw std::invalid_argument("valid_time_utc contains an out-of-range date or time");
    }
    static constexpr int month_days[] = {31, 28, 31, 30, 31, 30,
                                          31, 31, 30, 31, 30, 31};
    int maximum_day = month_days[month - 1];
    if (month == 2 && leap_year(year)) {
        maximum_day = 29;
    }
    if (day < 1 || day > maximum_day) {
        throw std::invalid_argument("valid_time_utc contains an invalid calendar day");
    }

    double fraction = 0.0;
    std::size_t suffix = 19;
    if (value[suffix] == '.') {
        const std::size_t fraction_begin = ++suffix;
        while (suffix < value.size() && value[suffix] >= '0' &&
               value[suffix] <= '9') {
            ++suffix;
        }
        if (suffix == fraction_begin || suffix - fraction_begin > 9) {
            throw std::invalid_argument(
                "valid_time_utc fractional seconds must contain 1 to 9 digits");
        }
        fraction = std::stod("0." + value.substr(fraction_begin, suffix - fraction_begin));
    }
    if (suffix + 1 != value.size() || value[suffix] != 'Z') {
        throw std::invalid_argument("valid_time_utc must end in Z and have no offset");
    }

    const std::int64_t days = days_from_civil(
        year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    return static_cast<double>(days) * 86400.0 +
           static_cast<double>(hour * 3600 + minute * 60 + second) + fraction;
}

}
