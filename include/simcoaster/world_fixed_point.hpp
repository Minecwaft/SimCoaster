#pragma once
#include "world_types.hpp"
#include "numeric_word.hpp"

namespace simcoaster::fixed {
using numeric::word;
using numeric::narrow;
using numeric::add;
using numeric::subtract;
using numeric::negate;
using numeric::absolute;

constexpr std::int32_t multiply(std::int32_t a, std::int32_t b) {
    return narrow((std::int64_t { a } * b) >> 16);
}

constexpr FixedPointVector2 add(FixedPointVector2 a, FixedPointVector2 b) {
    return { add(a.x, b.x), add(a.y, b.y) };
}

constexpr FixedPointVector2 subtract(FixedPointVector2 a, FixedPointVector2 b) {
    return { subtract(a.x, b.x), subtract(a.y, b.y) };
}

constexpr FixedPointVector2 scale(FixedPointVector2 a, std::int32_t s) {
    return { multiply(a.x, s), multiply(a.y, s) };
}

FixedPointVector2 divide(FixedPointVector2, std::int32_t divisor);
std::int32_t truncate_integer(double);
std::int32_t approximate_length(FixedPointVector2);
std::int32_t magnitude(FixedPointVector2);
FixedPointVector2 normalize(FixedPointVector2);
FixedPointVector2 clamp(FixedPointVector2, std::int32_t maximum);
std::int32_t ray_circle_intersection(FixedPointVector2 origin, FixedPointVector2 direction, FixedPointVector2 center, std::int32_t radius);
} //namespace simcoaster::fixed
