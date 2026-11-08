#include "simcoaster/world_person.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <cmath>
#include <stdexcept>

namespace simcoaster {
bool world_operations::builtin::get_person_sprite_gender_flag(WorldRuntime& c, const PersonRuntimeView& person) {
    const auto* catalog = c.resources.variants.catalog;
    if (!catalog) {
        throw std::logic_error("Person gender requires sprite metadata");
    }
    const auto first = catalog->variants.at(person.mobile.sprite_category).first_loaded_resource_index;
    return catalog->resources.at(first + person.mobile.sprite_id).descriptor.sprite_gender_flag != 0;
}

void world_operations::builtin::update_mobile_entity_sprite_pose(WorldRuntime& c, PersonRuntimeView& person, const std::array<float, 3>& position, std::uint32_t rotation, MobileSpriteCategory category) {
    auto& mobile = person.mobile;
    auto& animation = mobile.animation;
    const auto requested = static_cast<std::uint32_t>(category);
    if (!animation.sprite_script) {
        set_world_sprite_animation_resource(c, animation, requested, static_cast<std::int32_t>(mobile.sprite_category), static_cast<std::int32_t>(mobile.sprite_id));
    }
    const auto height = static_cast<float>(position[1] - world_operations::sample_terrain_surface_height(c, position[0], position[2]));
    world_operations::set_resource_variant_transform(c, animation.sprite_script, { position[0], height, position[2] }, std::bit_cast<std::int32_t>(rotation));
    if (!sprite_animation_matches_resource_or_next_anim(c, animation, requested)) {
        set_sprite_animation_next(animation, requested);
    }
}

std::uint32_t world_operations::builtin::allocate_resource_variant(WorldRuntime& c, ResourceProgram program, ResourceArguments arguments) {
    return simcoaster::allocate_resource_variant(c, resource_program_entry(program), arguments);
}

void world_operations::builtin::restart_resource_variant(WorldRuntime& c, std::uint32_t handle, ResourceProgram program) {
    refresh_resource_variant(c, handle, resource_program_entry(program));
}

bool world_operations::builtin::resource_variant_matches(WorldRuntime& c, std::uint32_t handle, ResourceProgram program) {
    return simcoaster::resource_variant_matches(c, handle, resource_program_entry(program));
}

void world_operations::builtin::set_resource_variant_delay(WorldRuntime& c, std::uint32_t handle, std::uint32_t delay) {
    if (auto* state = find_resource_variant(c, handle)) {
        state->delay = delay;
    }
}

void world_operations::builtin::set_resource_variant_transform(WorldRuntime& c, std::uint32_t handle, const WorldVector3& position, std::int32_t rotation) {
    if (auto* state = find_resource_variant(c, handle)) {
        for (std::size_t i = 0; i < 3; ++i) {
            state->values[i + 1].floating(position[i]);
        }
        state->values[15].bits = std::bit_cast<std::uint32_t>(rotation);
    }
}

void world_operations::builtin::release_resource_pool_handle(WorldRuntime& c, std::uint32_t handle) {
    release_resource_variant(c, handle);
}

void initialize_mobile_sprite_animation(MobileSpriteAnimationState& animation) {
    animation = {};
}

void set_sprite_animation_next(MobileSpriteAnimationState& animation, std::uint32_t next) {
    animation.next_animation = next;
}

bool sprite_animation_matches_resource_or_next_anim(VisitorWorld& c, MobileSpriteAnimationState& animation, std::uint32_t requested) {
    //Query first, even if the pending ID already matches. Read that pending ID
    //after the backend call, which may modify the same animation state.
    return world_operations::resource_variant_matches(c, animation.sprite_script, { ResourceProgramFamily::sprite_animation, requested }) || animation.next_animation == requested;
}

void set_world_sprite_animation_resource(VisitorWorld& c, MobileSpriteAnimationState& animation, std::uint32_t requested, std::int32_t a, std::int32_t b) {
    if (std::bit_cast<std::int32_t>(animation.sprite_script) > 0) {
        world_operations::release_resource_pool_handle(c, animation.sprite_script);
    }
    animation.sprite_script = world_operations::allocate_resource_variant(c, { ResourceProgramFamily::sprite_animation, requested }, { static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), 0, 0, 0, 0 });
}

void release_world_sprite_animation_resource_handle(VisitorWorld& c, MobileSpriteAnimationState& animation) {
    world_operations::release_resource_pool_handle(c, animation.sprite_script);
    animation.sprite_script = 0;
}

void synchronize_world_sprite_animation_resource_state(VisitorWorld& c, MobileSpriteAnimationState& animation) {
    if (animation.next_animation) {
        world_operations::restart_resource_variant(c, animation.sprite_script, { ResourceProgramFamily::sprite_animation, animation.next_animation });
        animation.last_animation = animation.current_animation;
        animation.current_animation = animation.next_animation;
        animation.next_animation = 0;
    }
    if (animation.next_service_interval) {
        world_operations::set_resource_variant_delay(c, animation.sprite_script, animation.next_service_interval);
        animation.next_service_interval = 0;
    }
}

std::uint32_t set_sprite_animation_service_interval_from_motion_speed(MobileSpriteAnimationState& animation, float speed) {
    const auto interval = fixed::truncate_integer(62.0 / static_cast<double>(speed));
    animation.next_service_interval = static_cast<std::uint32_t>(std::clamp(interval, 0, 250));
    return animation.next_service_interval;
}

void set_navigation_force_and_speed_limits(MobileNavigationState& navigation, float requested) {
    if (requested > 2.0f) {
        requested = 2.0f;
    }
    navigation.max_force = std::max(655, fixed::truncate_integer(static_cast<double>(requested) * 26214.4));
    navigation.max_speed = std::max(655, fixed::truncate_integer(static_cast<double>(requested) * 13107.2));
}

void update_mobile_entity_speed_and_cached_position(VisitorWorld& c, MobileEntityState& entity) {
    const auto total = static_cast<std::uint32_t>(entity.adjustor_speed) + entity.base_speed + entity.purpose_speed;
    const auto requested = static_cast<float>((static_cast<double>(total) / mobile_base_speed_variants[2] + static_cast<double>(entity.previous_speed) * 3.0) * 0.25);
    entity.previous_speed = requested;
    set_navigation_force_and_speed_limits(entity.navigation, requested);
    //Original IMUL writes CX: discard the product's upper bits before division.
    entity.adjustor_speed = static_cast<std::uint16_t>(static_cast<std::uint16_t>(99u * entity.adjustor_speed) / 100u);
    entity.previous_x = entity.navigation.position.x;
    entity.previous_y = entity.navigation.position.y;
    synchronize_world_sprite_animation_resource_state(c, entity.animation);
}

std::uint16_t set_mobile_entity_purpose_speed(MobileEntityState& entity, PurposeSpeedMode mode, std::optional<PersonUmbrellaState> umbrella) {
    if (mode != PurposeSpeedMode::normal && umbrella && (*umbrella == PersonUmbrellaState::active || *umbrella == PersonUmbrellaState::removal_pending || *umbrella == PersonUmbrellaState::creation_pending)) {
        mode = PurposeSpeedMode::normal;
    }
    entity.purpose_speed = mobile_purpose_speed_values.at(static_cast<std::uint32_t>(mode));
    return entity.purpose_speed;
}

namespace {
void insert_ride_history(std::array<ThingId, 4>& history, ThingId id) {
    //Approved repair of the overlapping DWORD/WORD copy at611CF1/611D31
    //and6118FC: preserve the old third entry before shifting the array.
    const auto previous = history;
    history = { id, previous[0], previous[1], previous[2] };
}
} //namespace

void insert_person_previous_ride_history_entry(PersonRuntimeView& person, ThingId id) {
    insert_ride_history(person.previous_rides, id);
}

void insert_person_temporary_ride_history_entry(PersonRuntimeView& person, ThingId id) {
    insert_ride_history(person.previous_temporary_rides, id);
}

void age_person_temporary_ride_history(VisitorWorld& c, PersonRuntimeView& person) {
    if (!c.simulation.current) {
        throw std::logic_error("World is required for ride history aging");
    }
    if (c.simulation.current->saved.game_tick % 20 == 0) {
        insert_person_temporary_ride_history_entry(person, 0);
    }
}
} //namespace simcoaster
