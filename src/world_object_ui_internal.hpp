#pragma once
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/coaster_construction.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/park_construction.hpp"
#include "simcoaster/park_special_ride_construction.hpp"
#include "simcoaster/rsse_state.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_challenge.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_object_ui_runtime.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_park_admission.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_special_rides.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <variant>

namespace simcoaster::object_ui_detail {
inline bool has(const ParkObjectRuntimeView& object, ParkObjectCapabilities capability) {
    return (std::to_underlying(object.capabilities) & std::to_underlying(capability)) != 0;
}

inline std::int32_t meter(std::int32_t value) {
    return fixed::word(static_cast<std::uint32_t>(value) << 10) / 100;
}

inline std::int32_t quotient(std::int32_t numerator, std::int32_t denominator) {
    return numeric::quotient(numerator, denominator, "Park-object UI invalid native division");
}

template <class T, std::size_t N>
std::optional<T> sample(const SampleHistory<T, N>& ring, std::int32_t offset) {
    if (ring.num_entries < 0 || ring.num_entries > static_cast<int>(N))
        throw std::out_of_range("Park-object history capacity");
    if (offset < 0 || offset >= ring.num_entries || (offset > ring.current_entry && !ring.wrapped))
        return {};
    const auto index = offset <= ring.current_entry
                         ? fixed::subtract(ring.current_entry, offset)
                         : fixed::subtract(fixed::add(ring.num_entries, ring.current_entry), offset);
    return ring.data.at(static_cast<std::size_t>(index));
}

template <class T, std::size_t N>
double sum(const SampleHistory<T, N>& ring, std::int32_t maximum, bool signed_values = false) {
    const auto count = std::min(maximum, ring.wrapped ? ring.num_entries : fixed::add(ring.current_entry, 1));
    double total{};
    for (std::int32_t i = 0; i < count; ++i)
        if (const auto value = sample(ring, i))
            total += signed_values ? static_cast<double>(fixed::word(*value)) : static_cast<double>(*value);
    return total;
}

template <class T, std::size_t N>
std::int32_t latest(const SampleHistory<T, N>& ring) {
    if (ring.num_entries <= 0 || (ring.current_entry < 0 && !ring.wrapped))
        return 0;
    const auto index = ring.current_entry >= 0 ? ring.current_entry : fixed::add(ring.num_entries, ring.current_entry);
    return fixed::word(ring.data.at(static_cast<std::size_t>(index)));
}

inline std::int32_t running_cost(const ParkObjectRuntimeView& object) {
    return object.state == ParkObjectState::broken_down || object.state == ParkObjectState::condemned
             ? 0
             : fixed::word(object.running_cost);
}

inline std::uint64_t filetime_bits(const FileTime& value) {
    return std::uint64_t{ value.low } | (std::uint64_t{ value.high } << 32);
}

inline std::int32_t profit(const ParkObjectRuntimeView& object) {
    return object.history ? latest(object.history->dword_history_144[4]) : 0;
}

inline std::int32_t total_profit(const ParkObjectRuntimeView& object) {
    return object.history ? fixed::truncate_integer(sum(object.history->dword_history_144[4], 144, true)) : 0;
}

std::int32_t value_for_money(float, float, float, float);
enum class Filter {
    rides,
    shops,
    sideshows,
    features,
    facilities,
    relief,
    photo,
    info,
    litter,
    security,
    chill,
    addons,
    upgrade_targets,
    special
};
} //namespace simcoaster::object_ui_detail

namespace simcoaster {
struct WorldParkObjectUiRuntime::State {
    WorldRuntime& c;
    ParkConstructionRuntime& construction;
    ParkObjectUiState view;
    std::function<void(ThingId)> retirement_observer;
    ThingId retiring{};
    std::vector<ThingId> navigation;
    const ThingHeader *settings_identity{}, *statistics_identity{}, *opinions_identity{}, *promotion_identity{},
        *upgrade_identity{}, *special_identity{};

    explicit State(WorldRuntime& context, ParkConstructionRuntime& editor) : c(context), construction(editor) {}

    World& world() const;
    ParkObjectRuntimeView* live(ThingId) const;
    ParkObjectRuntimeView& object(ThingId) const;
    ResearchCatalogDefinition& definition(const ParkObjectRuntimeView&) const;
    const ObjectDefinitionBalance& balance(const ParkObjectRuntimeView&) const;
    BankRecord& bank() const;
    std::u16string
    text(std::span<const std::byte>, std::uint32_t, std::span<const BfstSignedIntegerArgument> = {}) const;
    std::u16string text(std::uint32_t, std::span<const BfstSignedIntegerArgument> = {}) const;
    std::vector<ParkObjectRuntimeView*> objects(object_ui_detail::Filter) const;
    bool matches(const ParkObjectRuntimeView&, object_ui_detail::Filter) const;
    ParkObjectUiRoute route(ThingId) const;
    ParkObjectUiMetrics metrics(ParkObjectRuntimeView&, const ParkObjectUiStagedSettings&) const;
    void load_settings(ParkObjectRuntimeView&);
    void rebuild_navigation(ParkObjectRuntimeView&);
    void refresh_settings(bool load = false);
    void apply_settings_to(ParkObjectRuntimeView&, bool bulk);
    void clear_complaints(std::span<const std::int32_t>, std::uint16_t);
    ParkObjectListUiRow row(ParkObjectRuntimeView&) const;
    void refresh_list(bool rebuild = true);
    void refresh_list_details();
    void refresh_statistics();
    void refresh_opinions();
    void refresh_promotion(bool rebuild = false);
    void refresh_upgrade(bool rebuild = false);
    void refresh_special();
};
} //namespace simcoaster
