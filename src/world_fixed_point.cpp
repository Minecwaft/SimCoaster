#include "simcoaster/world_fixed_point.hpp"
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace simcoaster::fixed {
namespace {
std::uint32_t root(std::uint32_t value) {
    return static_cast<std::uint32_t>(std::sqrt(static_cast<double>(value)));
}

std::int32_t quotient(std::int32_t value, std::int32_t divisor) {
    if (!divisor) {
        throw std::domain_error("zero fixed-point divisor");
    }
    const auto result = std::int64_t { value } * 65536 / divisor;
    if (result < std::numeric_limits<std::int32_t>::min() || result > std::numeric_limits<std::int32_t>::max()) {
        throw std::overflow_error("fixed-point quotient exceeds its range");
    }
    return static_cast<std::int32_t>(result);
}
} //namespace

std::int32_t truncate_integer(double value) {
    const auto truncated = std::trunc(value), limit = std::ldexp(1.0, 63);
    return std::isfinite(truncated) && truncated >= -limit && truncated < limit ? narrow(static_cast<std::int64_t>(truncated)) : 0;
}

FixedPointVector2 divide(FixedPointVector2 v, std::int32_t divisor) {
    return { quotient(v.x, divisor), quotient(v.y, divisor) };
}

std::int32_t approximate_length(FixedPointVector2 v) {
    const auto x = absolute(v.x), y = absolute(v.y);
    return subtract(add(x, y), std::min(x, y) >> 1);
}

std::int32_t magnitude(FixedPointVector2 v) {
    auto x = static_cast<std::uint32_t>(absolute(v.x)), y = static_cast<std::uint32_t>(absolute(v.y));
    const auto shift = (x | y) & 0xFFFFF000u ? ((x | y) & 0xFFC00000u ? 18u : 8u) : 0u;
    x >>= shift;
    y >>= shift;
    return word(root(x * x + y * y) << shift);
}

FixedPointVector2 normalize(FixedPointVector2 v) {
    const auto x = absolute(v.x), y = absolute(v.y);
    const auto bits = static_cast<std::uint32_t>(x | y);
    if (!(bits & 0xFFFFF000u)) {
        const auto length = root((static_cast<std::uint32_t>(x) * static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(y) * static_cast<std::uint32_t>(y)) << 6);
        const auto factor = length ? 524288u / length : 0u;
        return { word(static_cast<std::uint32_t>(v.x) * factor), word(static_cast<std::uint32_t>(v.y) * factor) };
    }
    const auto shift = bits & 0xFFC00000u ? 18u : 8u;
    const auto sx = static_cast<std::uint32_t>(x >> shift), sy = static_cast<std::uint32_t>(y >> shift), length = root(sx * sx + sy * sy);
    const auto factor = length ? (shift == 18 ? 16384u : 16777216u) / length : 0u;
    return scale(v, word(factor));
}

FixedPointVector2 clamp(FixedPointVector2 v, std::int32_t maximum) {
    const auto length = magnitude(v);
    return length >= maximum ? scale(v, quotient(maximum, length)) : v;
}

std::int32_t ray_circle_intersection(FixedPointVector2 origin, FixedPointVector2 direction, FixedPointVector2 center, std::int32_t radius) {
    const auto relative = subtract(origin, center);
    const auto a = add(multiply(direction.x, direction.x), multiply(direction.y, direction.y));
    const auto b = add(multiply(relative.x, direction.x), multiply(relative.y, direction.y));
    const auto twice_b = add(b, b), c = subtract(add(multiply(relative.x, relative.x), multiply(relative.y, relative.y)), multiply(radius, radius));
    const auto product = multiply(c, a);
    const auto discriminant = subtract(multiply(twice_b, twice_b), word(static_cast<std::uint32_t>(product) * 4u));
    if (discriminant <= 0) {
        return std::numeric_limits<std::int32_t>::min();
    }
    const auto square_root = word(root(static_cast<std::uint32_t>(discriminant)));
    //Preserve the executable's fixed-point quadratic approximation, including
    //its root scaling and strict rejection of a tangent discriminant.
    return std::min(negate(add(twice_b, square_root)), subtract(square_root, twice_b)) >> 1;
}
} //namespace simcoaster::fixed
