#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include <stdexcept>
#include <limits>

namespace simcoaster {
namespace {
ResearchCatalogDefinition& definition(WorldRuntime& c, const ParkObjectRuntimeView& object) {
    return require_bound<std::out_of_range>(get_park_object_definition(c, &object), "attraction definition");
}

ObjectControlRecord& control(WorldRuntime& c, const ParkObjectRuntimeView& object, bool cache = false) {
    auto& table = c.simulation.current->object_controls;
    auto* result = cache ? find_and_cache_world_object_control(table, object.definition_id)
                         : find_world_object_control(table, object.definition_id);
    return require_bound<std::out_of_range>(result, "attraction object control");
}

std::int32_t quotient(std::int32_t numerator, std::int32_t denominator) {
    if (!denominator)
        throw std::domain_error("zero attraction divisor");
    return fixed::narrow(static_cast<std::int64_t>(numerator) / denominator);
}
} //namespace

const MeshRuntimeOpeningView* world_operations::builtin::find_mesh_runtime_opening_state(WorldRuntime& c,
                                                                                         std::uint32_t mesh) {
    if (!mesh)
        return nullptr;
    if (!c.resources.meshes)
        throw std::logic_error("world mesh runtime is not bound");
    auto* instance = c.resources.meshes->instance(mesh);
    auto* coasters = c.resources.meshes->coasters();
    const auto* binding = coasters ? coasters->find(mesh) : nullptr;
    if (!instance || !binding)
        return nullptr;
    if (binding->collision.needs_full_rebuild)
        throw std::logic_error("Coaster peer restoration has not finished");
    const auto references = binding->collision.initialized
                              ? binding->collision.reference_count
                              : binding->saved_geometry.header.geometry_peer_reference_count;
    if (references > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("Coaster peer reference count");
    instance->opening_view = { static_cast<std::uint32_t>(references), binding->train_state.placement_flags };
    return &instance->opening_view;
}

bool is_track_type_3_park_object_runtime_openable(WorldRuntime& c, const ParkObjectRuntimeView* object) {
    if (!object)
        return false;
    if (c.construction.editor
        && c.construction.editor->active_map()
        && c.resources.meshes
        && c.resources.meshes->coasters()) {
        const auto* active = c.resources.meshes->coasters()->find_map(*c.construction.editor->active_map());
        if (active && active->mesh_slot == object->mesh_instance_id)
            return false;
    }
    const auto* runtime = world_operations::find_mesh_runtime_opening_state(c, object->mesh_instance_id);
    return runtime
        && !runtime->geometry_peer_reference_count
        && (runtime->placement_flags & 1u)
        && !(runtime->placement_flags & 2u)
        && runtime != c.attractions.active_mesh_runtime;
}

bool is_choosable_park_object_with_eligible_queue(WorldRuntime& c, ParkObjectRuntimeView& object) {
    if (!(std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::is_choosable))
        || object.state == ParkObjectState::broken_down
        || !object.can_load
        || object.state == ParkObjectState::condemned)
        return false;
    if (!get_back_of_queue(c, &object))
        return false;
    //The capacity comparison is unsigned, including the wrapped product.
    const auto count = static_cast<std::uint32_t>(get_person_park_object_queue_position(c, &object, 0));
    if (4u * object.queue_size_in_cells <= count)
        return false;
    if (definition(c, object).which_track_type == 3 && !is_track_type_3_park_object_runtime_openable(c, &object))
        return false;
    if (definition(c, object).which_track_type == 1 && !object.is_track_ride_valid)
        return false;
    return definition(c, object).which_track_type != 2 || object.is_track_ride_valid;
}

std::int64_t get_park_object_age_in_days(WorldRuntime& c, const ParkObjectRuntimeView& object) {
    FileTime now{};
    calculate_world_calendar_filetime(c, &c.simulation.current->calendar, &now);
    const auto bits = static_cast<std::uint64_t>(now.low) | (static_cast<std::uint64_t>(now.high) << 32);
    return std::bit_cast<std::int64_t>(bits - object.creation_file_time) / 864000000000ll;
}

std::int32_t calculate_park_attraction_value(WorldRuntime& c) {
    if (!c.simulation.current)
        throw std::logic_error("base World is required for attraction value");
    std::int32_t total = 0;
    auto id = c.simulation.current->saved.first_park_object;
    std::size_t visited = 0;
    while (id) {
        if (visited++ >= c.entities.size())
            throw std::length_error("cyclic attraction list");
        auto* payload = c.entities.at(id).payload;
        if (!payload)
            throw std::logic_error("missing attraction object");
        auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(payload);
        const auto category = definition(c, object).which_ui_type;
        if ((category == ObjectUiCategory::rides || category == ObjectUiCategory::sideshows)
            && is_choosable_park_object_with_eligible_queue(c, object)
            && std::bit_cast<std::int32_t>(control(c, object).live_instance_count) > 0) {
            std::int32_t divisor = 1, days;
            if (definition(c, object).which_track_type)
                days = fixed::narrow(get_park_object_age_in_days(c, object));
            else {
                divisor = std::bit_cast<std::int32_t>(control(c, object).live_instance_count);
                std::int64_t delta;
                write_scaled_global_clock_delta_qword(
                    c,
                    &c.simulation.current->calendar,
                    &delta,
                    static_cast<std::uint32_t>(control(c, object, true).first_instance_game_tick));
                days = fixed::narrow(delta / 864000000000ll);
            }
            const auto bucket =
                static_cast<std::uint32_t>(quotient(days, definition(c, object).new_attraction_decay_time));
            const auto bonus = bucket <= 2 ? definition(c, object).attraction_new_bonus[bucket] : 0;
            total = fixed::add(total, quotient(fixed::add(bonus, definition(c, object).attraction_value), divisor));
        }
        id = object.next_park_object;
    }
    return total;
}
} //namespace simcoaster
