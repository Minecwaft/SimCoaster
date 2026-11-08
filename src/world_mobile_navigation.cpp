#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "world_sprite_angles.hpp"
#include <cmath>
#include <stdexcept>

namespace simcoaster {
MobileEntityState& EntityEnginePort::online_presence_mobile_state(WorldRuntime&, ThingHeader*) {
    throw std::logic_error("online presence mobile layout has not been reconstructed");
}

std::uint16_t compute_world_sprite_angle_from_delta(std::int32_t x, std::int32_t y) {
    if (!x && !y) {
        return 0;
    }
    const auto ax = fixed::absolute(x), ay = fixed::absolute(y);
    const auto ratio = [](std::int32_t a, std::int32_t b) {
        if (!b) {
            throw std::domain_error("sprite angle divisor is zero");
        }
        return (static_cast<std::uint32_t>(a) << 8) / static_cast<std::uint32_t>(b);
    };
    std::int32_t angle;
    if (x < 0) {
        if (y < 0) {
            angle = ax < ay ? 2048 - sprite_angle_by_delta_ratio.at(ratio(ax, ay)) : 1536 + sprite_angle_by_delta_ratio.at(ratio(ay, ax));
        } else {
            angle = ax < y ? 1024 + sprite_angle_by_delta_ratio.at(ratio(ax, y)) : 1536 - sprite_angle_by_delta_ratio.at(ratio(y, ax));
        }
    } else if (y < 0) {
        angle = x < ay ? sprite_angle_by_delta_ratio.at(ratio(x, ay)) : 512 - sprite_angle_by_delta_ratio.at(ratio(ay, x));
    } else {
        angle = x < y ? 1024 - sprite_angle_by_delta_ratio.at(ratio(x, y)) : 512 + sprite_angle_by_delta_ratio.at(ratio(y, x));
    }
    return static_cast<std::uint16_t>(angle & 2047);
}

void move_world_map_object_to_coordinates(VisitorWorld& c, MapObjectBase& object, std::uint16_t x, std::uint16_t y) {
    if ((object.x >> 8) == (x >> 8) && (object.y >> 8) == (y >> 8)) {
        object.x = x;
        object.y = y;
        return;
    }
    if (!c.simulation.map_cell_records) {
        throw std::logic_error("movement needs map cells");
    }
    const auto cell = [](std::uint16_t px, std::uint16_t py) {
        const auto id = static_cast<CellId>(((py >> 8) << 7) + (px >> 8) + 1);
        if (!id || id > map_cell_count) {
            throw std::out_of_range("movement map-cell ID");
        }
        return id - 1u;
    };
    const auto id = object.header.id;
    unlink_world_used_thing_from_map_cell(c, &c.simulation.map_cell_records[cell(object.x, object.y)], id);
    object.x = x;
    object.y = y;
    link_world_used_thing_into_map_cell(c, &c.simulation.map_cell_records[cell(object.x, object.y)], id);
}

NavigationResult update_mobile_entity_navigation(VisitorWorld& c, MapObjectBase& base, MobileEntityState& entity) {
    const auto previous = entity.navigation.position;
    step_world_navigation_state(c, entity.navigation);
    const auto current = entity.navigation.position;
    move_world_map_object_to_coordinates(c, base, static_cast<std::uint16_t>(current.x >> 8), static_cast<std::uint16_t>(current.y >> 8));
    entity.stranded_time = 0;
    if (entity.navigation.cannot_reach_destination) {
        return NavigationResult::unreachable;
    }
    const auto progress = get_world_navigation_path_progress(entity.navigation);
    entity.sprite_angle = compute_world_sprite_angle_from_delta(fixed::subtract(previous.x, current.x), fixed::subtract(current.y, previous.y));
    if (progress == 65536) {
        return NavigationResult::arrived;
    }
    const auto is_child = [&] {
        return reinterpret_cast<const PersonRuntimeView*>(&base)->generation == PersonGeneration::child;
    };
    const bool run = entity.purpose_speed > mobile_purpose_speed_values[0] && ((base.header.kind == ThingKind::person && is_child()) || base.header.kind == ThingKind::guard);
    std::uint32_t walking_animation = 1;
    if (base.header.kind == ThingKind::guard && static_cast<std::int32_t>(reinterpret_cast<const StaffRuntimeRecord*>(&base)->state) == 28) {
        walking_animation = 31;
    }
    const auto animation = run ? 2u : walking_animation;
    if (!world_operations::resource_variant_matches(c, entity.animation.sprite_script, { ResourceProgramFamily::sprite_animation, animation })) {
        set_sprite_animation_next(entity.animation, animation);
    }
    const auto dx = fixed::subtract(current.x, previous.x), dy = fixed::subtract(current.y, previous.y);
    const auto squared = fixed::word(static_cast<std::uint32_t>(dx) * static_cast<std::uint32_t>(dx) + static_cast<std::uint32_t>(dy) * static_cast<std::uint32_t>(dy));
    constexpr auto conversion = std::bit_cast<float>(std::uint32_t { 941662369 });
    auto speed = static_cast<float>(std::sqrt(static_cast<double>(squared)) * conversion);
    if (!run) {
        speed += speed;
    }
    set_sprite_animation_service_interval_from_motion_speed(entity.animation, speed);
    return NavigationResult::moving;
}
} //namespace simcoaster
