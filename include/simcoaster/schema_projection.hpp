#pragma once
#include "schema_metadata.hpp"
#include <vector>

namespace simcoaster::schema {
struct FieldPair {
    std::meta::info destination, source;
};

//Exact name AND type matching is intentional: native pointer markers and
//stateful conversions remain the codec's explicit responsibility.
template <class To, class From>
consteval auto matching_field_plan() {
    std::vector<FieldPair> result;
    const auto destinations = schema::members_of(^^To, std::meta::access_context::current());
    const auto sources = schema::members_of(^^From, std::meta::access_context::current());
    for (const auto destination : destinations) {
        for (const auto source : sources) {
            if (std::meta::identifier_of(destination) == std::meta::identifier_of(source) && std::meta::type_of(destination) == std::meta::type_of(source)) {
                result.push_back({ destination, source });
            }
        }
    }
    return result;
}

template <class To, class From>
inline constexpr auto matching_fields = std::define_static_array(matching_field_plan<To, From>());

template <class To, class From>
constexpr void copy_matching(To& to, const From& from) {
    template for (constexpr auto pair : matching_fields<To, From>) to.[:pair.destination:] = from.[:pair.source:];
}
} //namespace simcoaster::schema
