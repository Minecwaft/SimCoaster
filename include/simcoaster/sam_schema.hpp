#pragma once
#include "sam_record.hpp"
#include <span>
#include <vector>

namespace simcoaster {
struct SamFieldContract {
    const char* group;
    const char* name;
    std::int32_t kind, minimum, maximum;
    float lower, upper;
    std::size_t capacity;
    bool indexed;
    std::uint32_t subtype {};
    bool unsigned_storage {};
};

struct NamedSamContract {
    const char* schema;
    SamFieldContract field;
};

template <class T, bool Integer = false>
consteval auto compiled_sam_contracts() {
    std::vector<SamFieldContract> result;
    if constexpr (Integer) {
        template for (constexpr auto b : sam::detail::members<T>) result.push_back({ b.field.group, b.field.field, b.field.bounded ? 6 : b.field.nonnegative ? 5 : 4, b.field.minimum, b.field.maximum, 0, 0, b.capacity, b.indexed });
    } else {
        template for (constexpr auto b : sam::record_detail::bindings<T>) result.push_back({ b.group, b.field.name, static_cast<std::int32_t>(b.field.kind), b.field.minimum, b.field.maximum, b.field.lower, b.field.upper, b.capacity, b.indexed, b.field.kind == sam::Kind::grid ? static_cast<std::uint32_t>(b.field.symbols) : 0, std::meta::type_of(b.leaf) == ^^typename std::uint32_t });
    }
    return result;
}

//Metadata is derived from the actual compiled production declarations.
std::span<const NamedSamContract> sam_schema_catalog();
std::span<const SamFieldContract> coaster_mesh_sam_contracts();
std::span<const SamFieldContract> coaster_collision_sam_contracts();
} //namespace simcoaster
