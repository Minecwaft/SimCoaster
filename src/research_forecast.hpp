#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace simcoaster::research_detail {
struct Forecast {
    std::int32_t days = -1;
    std::uint64_t steps{};
    std::uint32_t batches{};
};

//61E837 adds the float work increment repeatedly to the projection. Keep
//those ordered binary64 additions, not ceil((cost-done)/increment). A batch
//is exact only within one constant-spacing interval: after ties settle,
//each addition advances by the same integral number of representable units.
inline Forecast forecast(float completed, float required, float work) {
    Forecast result;
    double projection = completed;
    const double cost = required, increment = work;
    if (!(cost > projection)) {
        result.days = 0;
        return result;
    }
    if (!std::isfinite(cost) || !std::isfinite(projection) || !std::isfinite(increment) || increment <= 0)
        return result;
    constexpr float days_per_tick = 0.043478261679410934f;
    constexpr double day_limit = static_cast<double>(std::numeric_limits<std::int32_t>::max()) + 1;
    //Derived solely from the public result's range, not a gameplay/time budget.
    //Two extra steps put the bound safely beyond either division-rounding edge.
    constexpr auto step_limit = static_cast<std::uint64_t>(day_limit / (6.0 * days_per_tick)) + 2;
    const auto days = [&] {
        return std::trunc(static_cast<double>(result.steps * 6u) * days_per_tick);
    };
    while (projection < cost) {
        const auto next = projection + increment;
        if (next <= projection || result.steps == step_limit)
            return result;
        projection = next;
        ++result.steps;
        if (days() >= day_limit)
            return result;
        if (projection >= cost)
            break;

        const auto following = projection + increment;
        if (following <= projection)
            return result;
        const auto third = following + increment;
        int exponent{}, following_exponent{}, third_exponent{};
        (void)std::frexp(projection, &exponent);
        (void)std::frexp(following, &following_exponent);
        (void)std::frexp(third, &third_exponent);
        const auto delta = following - projection;
        if (!projection
            || (projection < 0) != (following < 0)
            || exponent != following_exponent
            || exponent != third_exponent
            || delta != third - following)
            continue;
        //Stop strictly before both the next spacing boundary and completion.
        //At either boundary, execute the original addition/comparison directly.
        const auto boundary = projection > 0 ? std::ldexp(1.0, exponent) : -std::ldexp(1.0, exponent - 1);
        const auto stop = std::min(std::nextafter(boundary, -std::numeric_limits<double>::infinity()),
                                   std::nextafter(cost, -std::numeric_limits<double>::infinity()));
        if (stop <= projection)
            continue;
        auto count = static_cast<std::uint64_t>(
            std::min(static_cast<double>(step_limit - result.steps), std::floor((stop - projection) / delta)));
        if (!count)
            continue;
        auto advanced = projection + static_cast<double>(count) * delta;
        //Division may round the candidate count up at an integer boundary. The
        //lattice products/sums inside this spacing interval are exact; trim it.
        if (advanced > stop) {
            --count;
            advanced = projection + static_cast<double>(count) * delta;
        }
        projection = advanced;
        result.steps += count;
        ++result.batches;
        if (days() >= day_limit)
            return result;
    }
    result.days = static_cast<std::int32_t>(days());
    return result;
}
} //namespace simcoaster::research_detail
