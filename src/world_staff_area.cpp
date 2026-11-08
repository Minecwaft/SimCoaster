#include "world_staff_internal.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_queue.hpp"
#include <memory>

namespace simcoaster {
using namespace staff_detail;

namespace {
bool inside(const StaffAreaBounds& b, CellId id) {
    const auto n = static_cast<std::uint32_t>(id) - 1u;
    const auto x = static_cast<std::int32_t>(n & 127u), y = static_cast<std::int32_t>(n >> 7);
    return x >= std::min(b.start_x, b.end_x) && x <= std::max(b.start_x, b.end_x) && y >= std::min(b.start_y, b.end_y) && y <= std::max(b.start_y, b.end_y);
}

void append(StaffWorld& c, StaffAreaMembership& area, CellId id) {
    if (area.begin && std::find(area.begin, area.end, id) != area.end) {
        return;
    }
    if (area.end == area.capacity) {
        const auto count = area.begin ? static_cast<std::size_t>(area.end - area.begin) : 0;
        const auto capacity = std::max<std::size_t>(count + 1, count * 2);
        auto* replacement = static_cast<CellId*>(c.ports.memory.allocate(Allocation::staff_area_cells, capacity * sizeof(CellId)));
        if (!replacement) {
            throw std::bad_alloc();
        }
        if (count) {
            std::uninitialized_copy_n(area.begin, count, replacement);
        }
        if (area.begin) {
            c.ports.memory.release(Allocation::staff_area_cells, area.begin);
        }
        area.begin = replacement;
        area.end = replacement + count;
        area.capacity = replacement + capacity;
    }
    std::construct_at(area.end++, id);
}

void assign_bounds(StaffAreaMembership& area, CellId first, CellId last) {
    const auto a = static_cast<std::uint32_t>(first) - 1u, b = static_cast<std::uint32_t>(last) - 1u;
    area.bounds = { static_cast<std::int32_t>(a & 127u), static_cast<std::int32_t>(a >> 7), static_cast<std::int32_t>(b & 127u), static_cast<std::int32_t>(b >> 7) };
}

bool training_entry(StaffWorld& c, CellId id) {
    const auto& entry = primary(c).map_cells.at(id - 1).base;
    if (entry.common.type != 9) {
        return false;
    }
    const auto training = first_training_room(c);
    if (!training) {
        return false;
    }
    return find_first_park_object_id_in_map_cell(c, &primary(c).map_cells.at(entry.common.parent_id - 1)) == training;
}
} //namespace

void update_park_object_world_staff_area_membership(StaffWorld& c, StaffAreaMembership& area, ParkObjectRuntimeView& o) {
    const auto outside = [&](CellId id) {
        if (!inside(area.bounds, id)) {
            append(c, area, id);
        }
    };
    const auto origin = cell(o.base);
    if (has(o, ParkObjectCapabilities::provides_relief) || has(o, ParkObjectCapabilities::holds_litter)) {
        outside(origin);
        return;
    }
    if (has(o, ParkObjectCapabilities::requires_watering)) {
        const auto* shape = get_park_object_balance(c, o).Info.Shape.get();
        if (!shape) {
            throw std::logic_error("staff-area plant shape is missing");
        }
        std::int32_t width = shape->width, height = shape->height, dx = 1, dy = 1;
        match (o.angle) {
            case 0  => ;
            case 90 => {
                const auto saved = width;
                width = height;
                height = -saved;
                dy = -1;
            }
            case 180 => {
                width = -width;
                height = -height;
                dx = dy = -1;
            }
            case 270 => {
                const auto saved = width;
                width = -height;
                height = saved;
                dx = -1;
            }
            case _ => throw std::domain_error("staff-area native rotation leaves its stepping locals undefined");
        } const auto n = static_cast<std::uint32_t>(origin) - 1u;
        for (std::int32_t x = 0; x != width; x += dx) {
            for (std::int32_t y = 0; y != height; y += dy) {
                outside(static_cast<CellId>(static_cast<std::int64_t>(n & 127u) + x + (static_cast<std::int64_t>(n >> 7) + y) * 128 + 1));
            }
        }
        return;
    }
    if (!has(o, ParkObjectCapabilities::has_queue)) {
        return;
    }
    auto id = get_back_of_queue(c, &o);
    if (!id) {
        return;
    }
    const auto entry = resolve_queue_start_item_id(c, &o);
    outside(id);
    std::size_t visited = 0;
    while (id != entry) {
        if (!id || ++visited > map_cell_count) {
            throw std::logic_error("invalid staff-area queue path");
        }
        const auto& base = primary(c).map_cells.at(id - 1).base;
        const auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &base, get_world_cell_direction(&base));
        if (!neighbor) {
            throw std::logic_error("missing staff-area queue neighbor");
        }
        id = neighbor->base.index;
        outside(id);
    }
}

void refresh_world_staff_area_membership_for_bounds(StaffWorld& c, StaffAreaMembership& area) {
    const auto& b = area.bounds;
    const auto min_x = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::min(b.start_x, b.end_x)) - 1), max_x = std::min<std::int64_t>(127, static_cast<std::int64_t>(std::max(b.start_x, b.end_x)) + 1);
    const auto min_y = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::min(b.start_y, b.end_y)) - 1), max_y = std::min<std::int64_t>(127, static_cast<std::int64_t>(std::max(b.start_y, b.end_y)) + 1);
    for (auto y = min_y; y <= max_y; ++y) {
        for (auto x = min_x; x <= max_x; ++x) {
            const auto& map = primary(c).map_cells.at(static_cast<std::size_t>(x + y * 128));
            const auto parent = map.base.common.parent_id;
            if (!parent) {
                continue;
            }
            const auto id = find_first_park_object_id_in_map_cell(c, &primary(c).map_cells.at(parent - 1));
            if (!id || !c.entities.at(id).payload || c.entities[id].payload->kind != ThingKind::park_object) {
                continue;
            }
            auto& o = object(c, id);
            if ((map.base.common.type == 3 || map.base.common.type == 9 || has(o, ParkObjectCapabilities::requires_watering)) && staff_area_accepts_park_object(c, area.bounds, o)) {
                update_park_object_world_staff_area_membership(c, area, o);
            }
        }
    }
}

void reset_world_object_staff_area_bounds_to_full_map(StaffRuntimeRecord& s) {
    s.area.bounds = { 0, 0, 127, 127 };
    s.area.end = s.area.begin;
}

void set_world_object_staff_area_bounds_from_packed_cells(StaffWorld& c, StaffRuntimeRecord& s, CellId first, CellId last) {
    if (first == 1 || last == map_cell_count) {
        reset_world_object_staff_area_bounds_to_full_map(s);
    } else {
        assign_bounds(s.area, first, last);
        refresh_world_staff_area_membership_for_bounds(c, s.area);
    }
}

void release_staff_membership_storage(StaffWorld& c, StaffRuntimeRecord& s) {
    if (s.area.begin) {
        c.ports.memory.release(Allocation::staff_area_cells, s.area.begin);
    }
    s.area.begin = s.area.end = s.area.capacity = nullptr;
}

bool can_world_staff_learn_theme_ability(StaffWorld& c, const StaffRuntimeRecord& s, ThemeAbility ability) {
    const auto activated = get_or_create_theme_global_info_manager(c).theme_ability_activation_flags.at(static_cast<std::size_t>(ability));
    if (!activated) {
        return false;
    }
    const bool compatible = match (s.base.header.kind) -> bool {
        case ThingKind::mechanic => ability == ThemeAbility::construction || ability == ThemeAbility::demolition || ability == ThemeAbility::ride_upgrading || ability == ThemeAbility::rock_blasting || ability == ThemeAbility::tunnelling || ability == ThemeAbility::stonemasonry;
        case ThingKind::handyman => ability == ThemeAbility::rubble_removal || ability == ThemeAbility::firefighting;
        case ThingKind::guard    => ability == ThemeAbility::first_aid;
        case ThingKind::gardener => ability == ThemeAbility::excavation || ability == ThemeAbility::logging || ability == ThemeAbility::landscaping || ability == ThemeAbility::water_drainage || ability == ThemeAbility::landfill;
        case _                   => false;
    };
    return compatible && s.experience >= 10000u;
}

void handle_dropped_staff_member(StaffWorld& c, StaffRuntimeRecord& s, CellId destination) {
    const auto p = center(destination);
    reset_position(c, s.base, s.mobile, p[0], p[1]);
    set_world_sprite_animation_resource(c, s.mobile.animation, 3, static_cast<std::int32_t>(s.mobile.sprite_category), static_cast<std::int32_t>(s.mobile.sprite_id));
    if (training_entry(c, destination) && s.experience >= static_cast<std::uint32_t>(balance(c).MinExperienceForTraining) && can_world_staff_learn_theme_ability(c, s, ThemeAbility::construction)) {
        s.going_to_learn = ThemeAbility::construction;
        enter_world_staff_state(c, s, StaffState::walking_to_training_room);
        return;
    }
    const auto kind = s.base.header.kind;
    if (kind == ThingKind::mechanic || kind == ThingKind::handyman || kind == ThingKind::gardener) {
        const auto& b = s.area.bounds;
        const auto first = static_cast<CellId>(b.start_x + static_cast<std::int64_t>(b.start_y) * 128 + 1), last = static_cast<CellId>(b.end_x + static_cast<std::int64_t>(b.end_y) * 128 + 1);
        reset_world_object_staff_area_bounds_to_full_map(s);
        if (kind == ThingKind::mechanic) {
            for (auto id = world(c).saved.first_park_object; id;) {
                auto& o = object(c, id);
                if ((o.state == ParkObjectState::broken_down || o.requested_service || o.state == ParkObjectState::upgrade_requested) && o.assigned_staff_member) {
                    release_claim(c, o);
                }
                id = o.next_park_object;
            }
        }
        choose_staff_task_or_rest_area(c, s);
        if (first != 1 && last != map_cell_count) {
            assign_bounds(s.area, first, last);
            refresh_world_staff_area_membership_for_bounds(c, s.area);
        } else {
            reset_world_object_staff_area_bounds_to_full_map(s);
        }
    } else {
        enter_world_staff_state(c, s, StaffState::idle);
        s.time_started_idling = 0;
    }
}
} //namespace simcoaster
