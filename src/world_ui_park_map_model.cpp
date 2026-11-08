#include "simcoaster/object_balance.hpp"
#include "simcoaster/world_challenge.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_special_rides.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_visitor_ui.hpp"
#include "world_ui_park_map_internal.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
std::uint32_t flags(const ParkObjectRuntimeView& object) {
    return std::to_underlying(object.capabilities);
}
} //namespace

bool WorldParkMapUiController::State::passes(const ParkObjectRuntimeView& object) const {
    if (primary_filter == 0 || primary_filter == 4)
        return true;
    auto* definition = get_park_object_definition(c, &object);
    if (!definition)
        throw std::runtime_error("Park Map object definition is absent");
    const auto f = flags(object);
    if (definition->which_ui_type == ObjectUiCategory::rides)
        return primary_filter == 2;
    if (definition->which_ui_type == ObjectUiCategory::facilities) {
        if ((f & 2) || (f & 1024) || ((f & 1) && primary_filter == 1))
            return true;
        return (f & 512) && primary_filter == 5;
    }
    if (definition->which_ui_type == ObjectUiCategory::features) {
        if (f & 64)
            return primary_filter == 1;
        if (f & 16)
            return primary_filter == 3;
        return (f & 4096) && primary_filter == 6;
    }
    return false;
}

void WorldParkMapUiController::State::build_cache() {
    if (!c.simulation.map_cell_records || !c.simulation.track_cell_records)
        throw std::runtime_error("Park Map cell arrays are not bound");
    cells.fill({});
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            auto& output = cells[y * 128 + x];
            const auto& map = c.simulation.map_cell_records[y * 128 + x];
            const auto& base = map.base;
            const auto type = base.common.type;
            const auto ref = world_track_reference_matches_type_set(c, base.index);
            auto* track = resolve_track_cell_for_grid_entry(c, &base);
            if (track && world_cell_is_track_child(&track->base)) {
                const auto parent = track->base.common.parent_id;
                track = parent ? &c.simulation.track_cell_records[parent - 1] : nullptr;
            }
            if (track && track->base.common.type == 25) {
                output.kind = CellClass::hoarding;
                continue;
            }
            if (type == 4 || type == 9 || type == 10 || ref || type == 0x15) {
                const auto parent = ref && track ? track->base.common.parent_id : base.common.parent_id;
                if (!parent || parent > map_cell_count)
                    continue;
                auto id = c.simulation.map_cell_records[parent - 1].who;
                for (std::size_t count = 0; id && count < c.entities.size(); ++count) {
                    const auto* header = c.entities.at(id).payload;
                    if (!header)
                        throw std::runtime_error("Park Map linked cell record is absent");
                    const auto& record = *reinterpret_cast<const MapObjectBase*>(header);
                    if (header->kind == ThingKind::park_object
                        && !header->retirement_pending
                        && !retired.contains(id)) {
                        const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(header);
                        const auto* definition = get_park_object_definition(c, &object);
                        if (!definition)
                            throw std::runtime_error("Park Map cached definition is absent");
                        (do {
                            match (definition->which_ui_type) {
                                case ObjectUiCategory::rides => {
                                    if (type != 4 && (ref || type == 0x15))
                                        output.kind = CellClass::track;
                                    else if (!park_object_definition_is_marked_special(c, &object)
                                             || is_special_ride_group_id_unregistered(c, id))
                                        output.kind = CellClass::ride;
                                    else {
                                        if (base.common.flags & 64u)
                                            output.kind = CellClass::outside;
                                        do_return;
                                    }
                                    output.object = id;
                                }
                                case ObjectUiCategory::shops => output = { CellClass::shop, id };
                                case ObjectUiCategory::sideshows => output = { CellClass::sideshow, id };
                                case ObjectUiCategory::facilities => output = { CellClass::facility, id };
                                case ObjectUiCategory::features => {
                                    const auto f = flags(object);
                                    output = { f & 0x41   ? CellClass::litter
                                               : f & 0x10 ? CellClass::security
                                               : f & 2    ? CellClass::chill
                                                          : CellClass::feature,
                                               id };
                                }
                                case _ => {}
                            }
                        });
                        if (output.object)
                            break;
                    }
                    id = record.map_child;
                }
            } else if (type == 1)
                output.kind = CellClass::path;
            else if (type == 3 || type == 9)
                output.kind = CellClass::queue;
            else if (base.common.flags & 64u)
                output.kind = CellClass::outside;
        }
}

void WorldParkMapUiController::State::build_markers() {
    markers.clear();
    hover.reset();
    hover_cell = 0;
    const auto add = [&](std::size_t texture,
                         std::int32_t source,
                         Filter filter,
                         std::uint32_t value,
                         float scale,
                         std::array<float, 2> position,
                         const ThingHeader* identity) {
        markers.push_back({ texture,
                            source,
                            filter,
                            value,
                            scale,
                            position,
                            identity ? identity->id : ThingId{},
                            identity,
                            false,
                            { 255, 255, 255, 255 } });
    };
    const auto object_marker =
        [&](ParkObjectRuntimeView& object, std::size_t texture, std::int32_t source, std::uint32_t value, bool adjust) {
            const auto& balance = get_park_object_balance(c, object);
            if (!balance.Info.Shape)
                throw std::runtime_error("Park Map object has no original shape");
            float dx = static_cast<float>(balance.Info.Shape->width) - 1.f;
            float dy = static_cast<float>(balance.Info.Shape->height) - 1.f;
            match (object.angle) {
                case 90 => {
                    const auto old = dx;
                    dx = dy;
                    dy = -old;
                }
                case 180 => {
                    dx = -dx;
                    dy = -dy;
                }
                case 270 => {
                    const auto old = dx;
                    dx = -dy;
                    dy = old;
                }
                case _ => {}
            }
            const auto index = static_cast<std::uint32_t>(object.top_left) - 1;
            std::array position{ static_cast<float>(index & 127u) + (dx + 1.f) * .5f,
                                 static_cast<float>(index >> 7) + (dy + 1.f) * .5f };
            auto scale = .40000001f;
            if (adjust) {
                if (dx > 1.f && dy > 1.f)
                    scale += .125f;
                if (dx > 2.f && dy > 2.f)
                    scale += .25f;
                else if (dx == 0.f && dy == 0.f)
                    position[0] += .1f;
            }
            add(texture, source, Filter::mask, value, scale, position, &object.base.header);
        };
    std::size_t count = 0;
    for (auto* link = c.entities.active_head; link; link = link->active_next) {
        if (++count > c.entities.size())
            throw std::runtime_error("Park Map active list is cyclic");
        auto* header = link->payload;
        if (!header || header->retirement_pending || retired.contains(header->id))
            continue;
        if (header->kind == ThingKind::person || is_staff_record_kind(header->kind)) {
            const auto& base = *reinterpret_cast<const MapObjectBase*>(header);
            const std::array position{ static_cast<float>(base.x) / 256.f, static_cast<float>(base.y) / 256.f };
            if (header->kind == ThingKind::person)
                add(0, 0, Filter::mask, 32, .30000001f, position, header);
            else {
                const auto role = world_staff_type_for_used_thing(*header);
                const std::array<std::uint32_t, 6> values{ 1, 2, 4, 3, 5, 6 };
                add(2, 2, Filter::primary, values.at(static_cast<std::size_t>(role)), .40000001f, position, header);
            }
            continue;
        }
        if (header->kind != ThingKind::park_object)
            continue;
        auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(header);
        const auto* definition = get_park_object_definition(c, &object);
        if (!definition)
            throw std::runtime_error("Park Map marker definition is absent");
        const auto stars =
            static_cast<std::size_t>(std::clamp(calculate_park_object_customer_satisfaction(object) / 20, 1, 5) + 13);
        (do {
            match (definition->which_ui_type) {
                case ObjectUiCategory::rides => {
                    if (park_object_definition_is_marked_special(c, &object)
                        && !is_special_ride_group_id_unregistered(c, header->id))
                        do_return;
                    object_marker(object, 11, 11, 1024, true);
                    if (object.current_upgrade)
                        object_marker(object, object.current_upgrade == 1 ? 19 : 20, 11, 512, false);
                    object_marker(object, stars, static_cast<std::int32_t>(stars), 256, false);
                    if (object.state == ParkObjectState::broken_down)
                        object_marker(object, 6, 6, 128, false);
                    else if (object.state == ParkObjectState::condemned)
                        object_marker(object, 7, 7, 128, false);
                    else if (!object.can_load)
                        object_marker(object, 8, 8, 128, true);
                }
                case ObjectUiCategory::shops => {
                    object_marker(object, 12, 12, 1024, true);
                    object_marker(object, stars, static_cast<std::int32_t>(stars), 256, false);
                    if (!object.can_load)
                        object_marker(object, 8, 8, 128, true);
                }
                case ObjectUiCategory::sideshows => {
                    object_marker(object, 13, 13, 1024, true);
                    object_marker(object, stars, static_cast<std::int32_t>(stars), 256, false);
                    if (!object.can_load)
                        object_marker(object, 8, 8, 128, true);
                }
                case ObjectUiCategory::features => object_marker(object, 10, 10, 1024, true);
                case ObjectUiCategory::facilities => {
                    object_marker(object, 9, 9, 1024, true);
                    if (flags(object) & 1)
                        object_marker(object, stars, static_cast<std::int32_t>(stars), 256, false);
                    if (!object.can_load)
                        object_marker(object, 8, 8, 128, true);
                }
                case _ => {}
            }
        });
    }
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto litter = static_cast<std::int32_t>(c.simulation.map_cell_records[y * 128 + x].litter_selector);
            if (!litter)
                continue;
            const auto* world = c.referenced_world();
            const auto radius = litter == 8 ? world->region_profiles[5].radius
                              : litter == 7 ? world->region_profiles[2].radius
                                            : 1;
            add(3,
                3,
                Filter::primary,
                1,
                static_cast<float>(radius) * .5f,
                { static_cast<float>(x) + .5f, static_cast<float>(y) + .5f },
                nullptr);
        }
    for (auto& marker : markers)
        update_marker(marker);
}

bool WorldParkMapUiController::State::admitted(const Marker& marker) const {
    if (marker.value & 32u)
        return true;
    return marker.filter == Filter::mask    ? (marker.value & mask) != 0
         : marker.filter == Filter::primary ? marker.value == primary_filter
                                            : marker.value == 1;
}

void WorldParkMapUiController::State::update_marker(Marker& marker) {
    marker.visible = admitted(marker);
    marker.color = { 255, 255, 255, 255 };
    if (marker.thing) {
        const auto* record = c.entities.at(marker.thing).payload;
        if (record != marker.identity || !record || record->retirement_pending || retired.contains(marker.thing)) {
            marker.visible = false;
            return;
        }
        if (marker.source_kind == 0 && record->kind == ThingKind::person) {
            const auto& person = *reinterpret_cast<const PersonRuntimeView*>(record);
            if (staff_choice == 1 && person.force_thought_31)
                marker.color = mesh_argb_color(std::bit_cast<std::uint32_t>(-16776961));
            else if (staff_choice == 2) {
                if (person.prank != PersonPrank::none)
                    marker.color = mesh_argb_color(std::bit_cast<std::uint32_t>(-62966));
                if (person.state == PersonState::caught_by_guard)
                    marker.color = mesh_argb_color(std::bit_cast<std::uint32_t>(-16776961));
                const auto cell = static_cast<std::size_t>(person.base.y >> 8) * 128 + (person.base.x >> 8);
                if (cell < map_cell_count
                    && c.simulation.region_effects.cells
                    && c.simulation.region_effects.cells[cell].security > 0)
                    marker.color = mesh_argb_color(std::bit_cast<std::uint32_t>(-16711681));
            }
        } else if (marker.source_kind > 5 && marker.source_kind <= 20 && record->kind == ThingKind::park_object)
            marker.visible = marker.visible && passes(*reinterpret_cast<const ParkObjectRuntimeView*>(record));
    }
}

std::array<float, 2> WorldParkMapUiController::State::project(std::array<float, 2> point) const {
    return { 1024.f
                 + (point[0] - center[0])
                       * (480.f / static_cast<float>(width))
                       * zoom
                       * 2048.f
                       / static_cast<float>(pixel_width),
             768.f
                 - (point[1] - center[1])
                       * (425.f / static_cast<float>(height))
                       * zoom
                       * 1536.f
                       / static_cast<float>(pixel_height) };
}

std::array<float, 2> WorldParkMapUiController::State::unproject(UiPoint point) const {
    return { (static_cast<float>(point.x) - 1024.f)
                     * static_cast<float>(pixel_width)
                     / 2048.f
                     / (480.f / static_cast<float>(width) * zoom)
                 + center[0],
             (768.f - static_cast<float>(point.y))
                     * static_cast<float>(pixel_height)
                     / 1536.f
                     / (425.f / static_cast<float>(height) * zoom)
                 + center[1] };
}
} //namespace simcoaster
