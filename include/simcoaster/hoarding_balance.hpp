#pragma once
#include "sam_record.hpp"

namespace simcoaster {
struct HoardingClick {
    [[=sam::Field{ "X", sam::Kind::integer }]] std::int32_t x{};
    [[=sam::Field{ "Y", sam::Kind::integer }]] std::int32_t y{};
};

struct HoardingBalance {
    [[=sam::Rows{ "HoardingClicks" }]] std::array<HoardingClick, 64> points{};
    [[=sam::Extent{ "HoardingClicks" }]] std::uint32_t count{};
};

struct ThemeHoardingConfiguration {
    HoardingBalance main, cemetery, end;
};
} //namespace simcoaster
