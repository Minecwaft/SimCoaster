#pragma once
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_domains_fwd.hpp"
#include "simcoaster/advisor_balance.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <limits>

namespace simcoaster::advisor_detail {
inline World& base(AdvisorWorld& c) {
    return require_bound(c.simulation.current, "Advisor base World");
}

inline World& primary(AdvisorWorld& c) {
    return require_bound(c.referenced_world(), "Advisor primary World");
}

template <class T>
T& record(AdvisorWorld& c, ThingId id) {
    auto* p = c.entities.at(id).payload;
    if (!p)
        throw std::logic_error("Advisor missing World record");
    return *reinterpret_cast<T*>(p);
}

inline ParkAnalyserState& analyser(AdvisorWorld& c) {
    return record<ParkAnalyserState>(c, primary(c).saved.park_analyser);
}

inline bool enabled(const AdvisorQuery& c) {
    return c.advisor.balance->GeneralAdvisor.UISelectionModeMessageFilter && !c.construction.mode;
}

inline std::int32_t product(std::int32_t a, std::int32_t b) {
    return fixed::word(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

inline std::int32_t quotient(std::int32_t a, std::int32_t b) {
    return numeric::quotient(a, b, "Advisor integer divisor");
}

inline std::uint8_t byte(float value) {
    return static_cast<std::uint8_t>(fixed::truncate_integer(value));
}

inline bool has(const ParkObjectRuntimeView& object, ParkObjectCapabilities capability) {
    return (std::to_underlying(object.capabilities) & std::to_underlying(capability)) != 0;
}

inline const ResearchCatalogDefinition& definition(AdvisorWorld& c, const ParkObjectRuntimeView& object) {
    const auto* d = get_park_object_definition(c, &object);
    if (!d || !d->balance)
        throw std::logic_error("Advisor object definition");
    return *d;
}

template <class F>
void active(AdvisorWorld& c, F&& visit) {
    for (auto* row = c.entities.active_head; row;) {
        auto* h = row->payload;
        if (!h)
            throw std::logic_error("Advisor null active record");
        visit(*h);
        row = c.entities.at(h->id).active_next;
    }
}

template <class F>
void objects(EconomyWorld& c, F&& visit) {
    active(c, [&](ThingHeader& h) {
        if (h.kind == ThingKind::park_object)
            visit(*reinterpret_cast<ParkObjectRuntimeView*>(&h));
    });
}

template <class F>
void linked_objects(AdvisorWorld& c, F&& visit) {
    for (auto id = base(c).saved.first_park_object; id;) {
        auto& o = record<ParkObjectRuntimeView>(c, id);
        visit(o);
        id = o.next_park_object;
    }
}

template <class T, std::size_t N>
std::int32_t available(const SampleHistory<T, N>& h) {
    return h.wrapped ? h.num_entries : fixed::add(h.current_entry, 1);
}

template <class T, std::size_t N>
std::optional<T> sample(const SampleHistory<T, N>& h, std::int32_t offset) {
    if (offset >= h.num_entries || (offset > h.current_entry && !h.wrapped))
        return {};
    const auto index = offset <= h.current_entry ? fixed::subtract(h.current_entry, offset)
                                                 : fixed::subtract(fixed::add(h.num_entries, h.current_entry), offset);
    return h.data.at(static_cast<std::size_t>(index));
}

template <class T, std::size_t N, class Project>
double sum(const SampleHistory<T, N>& h, std::int32_t maximum, Project project) {
    double value = 0;
    for (int i = 0, count = std::min(maximum, available(h)); i < count; ++i)
        if (auto v = sample(h, i))
            value += project(*v);
    return value;
}

inline AdvisorActionHistoryEntry& history(AdvisorWorld& c, std::int32_t action) {
    return get_or_create_theme_global_info_manager(c).history.at(static_cast<std::size_t>(action));
}

inline bool select(AdvisorWorld& c, ThingId id) {
    if (!id)
        return false;
    c.advisor.behavior.callback_thing_id = id;
    c.advisor.behavior.callback_secondary = record<ParkObjectRuntimeView>(c, id).definition_id;
    return true;
}
} //namespace simcoaster::advisor_detail
