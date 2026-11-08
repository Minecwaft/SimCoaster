#pragma once
#include <meta>
#include <type_traits>
#include <utility>

namespace simcoaster::schema {
//Common declaration discovery and typed projection; wire/SAM policies own
//their distinct labels, validation, ordering and partial-transfer behavior.
consteval auto members_of(std::meta::info type, std::meta::access_context access) {
    return std::meta::nonstatic_data_members_of(type, access);
}

template <class Annotation>
consteval Annotation annotation(std::meta::info entity, const char* error) {
    const auto annotations = std::meta::annotations_of_with_type(entity, ^^Annotation);
    if (annotations.size() != 1) {
        throw error;
    }
    return std::meta::extract<Annotation>(std::meta::constant_of(annotations.front()));
}

template <std::meta::info Member, class T>
constexpr decltype(auto) member(T&& value) {
    return (std::forward<T>(value).[:Member:]);
}
} //namespace simcoaster::schema
