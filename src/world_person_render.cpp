#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_staff.hpp"
#include "world_person_internal.hpp"
#include <format>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr std::array<float, 20> balloon_vertical_curve { 0, .02f, .04f, .06f, .08f, .10f, .12f, .14f, .16f, .18f, .20f, .18f, .16f, .14f, .12f, .10f, .08f, .06f, .04f, 0 };

MobileRenderPose mobile_resource_pose(VisitorWorld& c, const MobileEntityState& mobile) {
    //Object-interaction state deliberately releases the sprite. Checkout can
    //still emit payment/trap cues before state19 recreates it. The old getter
    //left these callers' stack coordinates uninitialized for handle0. Define
    //that case using the same logical pose as the normal mobile renderer.
    if (!mobile.animation.sprite_script && mobile.no_floating_things_on_ride) {
        return interpolate_mobile_entity_render_position(mobile, 1.0f, true);
    }
    if (!mobile.animation.sprite_script) {
        for (auto* row = c.entities.active_head; row; row = row->active_next) {
            const auto* header = row->payload;
            if (!header || header->retirement_pending || !is_staff_record_kind(header->kind)) {
                continue;
            }
            const auto& staff = *reinterpret_cast<const StaffRuntimeRecord*>(header);
            if (&staff.mobile == &mobile && world_staff_has_hidden_base_sprite(staff)) {
                //629472 emits the dismissal effect before retirement. Rest/training/
                //research deliberately own no sprite; native 4E3D90 left the effect's
                //stack coordinates unwritten. Use the actor's actual logical pose,
                //while stale nonzero resource handles still fail below.
                return interpolate_mobile_entity_render_position(mobile, 1.0f, true);
            }
        }
    }
    try {
        return world_operations::get_resource_variant_pose(c, mobile.animation.sprite_script);
    } catch (const std::invalid_argument& error) {
        for (auto* row = c.entities.active_head; row; row = row->active_next) {
            const auto* h = row->payload;
            if (!h) {
                continue;
            }
            const auto* state = h->kind == ThingKind::person ? &reinterpret_cast<const PersonRuntimeView*>(h)->mobile : is_staff_record_kind(h->kind) ? &reinterpret_cast<const StaffRuntimeRecord*>(h)->mobile : nullptr;
            if (state == &mobile) {
                const auto activity = h->kind == ThingKind::person ? std::to_underlying(reinterpret_cast<const PersonRuntimeView*>(h)->state) : std::to_underlying(reinterpret_cast<const StaffRuntimeRecord*>(h)->state);
                throw std::invalid_argument(std::format("{}: thing {}, kind {}, state {}, resource {}", error.what(), h->id, std::to_underlying(h->kind), activity, mobile.animation.sprite_script));
            }
        }
        throw;
    }
}
} //namespace

//namespace
MobileRenderPose world_operations::builtin::get_resource_variant_pose(WorldRuntime& c, std::uint32_t handle) {
    const auto* state = find_resource_variant(c, handle);
    if (!state) {
        throw std::invalid_argument("resource pose requires a live handle");
    }
    return { { state->values[1].floating(), state->values[2].floating(), state->values[3].floating() }, state->values[15].bits };
}

double world_operations::builtin::sample_terrain_surface_height(WorldRuntime& c, float x, float y) {
    if (!c.construction.active_terrain_model_data || !c.construction.active_terrain_model_data->terrain_grid) {
        throw std::logic_error("terrain grid is not bound");
    }
    return simcoaster::sample_terrain_surface_height(*c.construction.active_terrain_model_data->terrain_grid, x, y);
}

void MeshHostPort::draw_stranded_mobile_cell(WorldRuntime&, const WorldVector3&) {
    throw std::logic_error("dynamic cell-face backend is required");
}

MobileRenderPose interpolate_mobile_entity_render_position(const MobileEntityState& entity, float factor, bool clamp) {
    const auto low = clamp ? 0.0f : -1.0f, high = clamp ? 1.0f : 2.0f;
    if (!(factor >= low)) {
        factor = low;
    } else if (factor > high) {
        factor = high;
    }
    const auto horizontal = [&](std::int32_t current, std::int32_t previous) {
        const auto integer = fixed::truncate_integer(static_cast<double>(fixed::subtract(current, previous)) * factor + previous);
        return static_cast<float>(static_cast<double>(integer) * 10.0 / 65536.0);
    };
    return { { horizontal(entity.navigation.position.x, entity.previous_x), 0, horizontal(entity.navigation.position.y, entity.previous_y) }, entity.sprite_angle };
}

void update_person_balloon_render_pose(VisitorWorld& c, PersonRuntimeView& person, const WorldVector3& pose, std::uint32_t angle) {
    if (person.balloon_script) {
        world_operations::set_resource_variant_transform(c, person.balloon_script, { person.last_position_x, pose[1], person.last_position_y }, std::bit_cast<std::int32_t>(angle));
        person.last_position_x = pose[0];
        person.last_position_y = pose[2];
    }
}

void update_person_umbrella_render_pose(VisitorWorld& c, PersonRuntimeView& person, const WorldVector3& pose, std::uint32_t angle) {
    if (person.umbrella_script) {
        world_operations::set_resource_variant_transform(c, person.umbrella_script, pose, std::bit_cast<std::int32_t>(angle));
    }
}

std::uint32_t create_runtime_template_at_mobile_entity_resource_position(VisitorWorld& c, const MobileEntityState& entity, std::uint32_t particle, std::int32_t offset, std::int32_t parameter) {
    const auto resource = mobile_resource_pose(c, entity);
    const auto x = static_cast<float>(static_cast<double>(c.visitors.environment.MapInfo.HeightfieldXStart) * 10.0 + resource.position[0]);
    const auto z = static_cast<float>(static_cast<double>(c.visitors.environment.MapInfo.HeightfieldYStart) * 10.0 + resource.position[2]);
    const auto y = static_cast<float>(static_cast<double>(world_operations::sample_terrain_surface_height(c, x, z)) + offset);
    return c.ports.effects.create_particle_effect(c, particle, { fixed::truncate_integer(static_cast<double>(x) * 1024.0), fixed::truncate_integer(static_cast<double>(y) * 1024.0), fixed::truncate_integer(static_cast<double>(z) * 1024.0) }, parameter);
}

void ResourceHostPort::submit_mobile_entity_positioned_template_request(WorldRuntime& c, PersonRuntimeView* person, std::uint32_t catalog, std::int32_t key) {
    if (!person) {
        throw std::invalid_argument("null mobile positioned-template receiver");
    }
    submit_mobile_entity_positioned_template_request(c, person->mobile, catalog, key);
}

void ResourceHostPort::submit_mobile_entity_positioned_template_request(WorldRuntime& c, MobileEntityState& mobile, std::uint32_t catalog, std::int32_t key) {
    //Native 0x60BE90 truncates the resource pose before the request's feature
    //gate. This path neither adds the map origin nor samples terrain height.
    const auto pose = mobile_resource_pose(c, mobile);
    (void)submit_positioned_template_request(c, 0, catalog, std::bit_cast<std::uint32_t>(key), fixed::truncate_integer(pose.position[0]), fixed::truncate_integer(pose.position[1]), fixed::truncate_integer(pose.position[2]));
}

void update_mobile_entity_render_pose(VisitorWorld& c, MapObjectBase& base, MobileEntityState& entity, float interpolation) {
    auto* person = base.header.kind == ThingKind::person ? reinterpret_cast<PersonRuntimeView*>(&base) : nullptr;
    bool umbrella = person && (person->umbrella_state == PersonUmbrellaState::removal_pending || person->umbrella_state == PersonUmbrellaState::active);
    const bool balloon = person && (person->balloon_script || person->remaining_balloon_life);
    const auto primary = interpolate_mobile_entity_render_position(entity, interpolation, true);
    MobileRenderPose secondary {}, umbrella_pose {};
    if (balloon) {
        secondary = interpolate_mobile_entity_render_position(entity, static_cast<float>(static_cast<double>(interpolation) - 0.35f), false);
    }
    if (umbrella) {
        const auto id = person_detail::cell_id(base);
        const auto& cell = *get_world_map_cell_record_by_id(c, id);
        if (world_track_reference_matches_type_set(c, cell.base.index) && world_cell_blocks_person_weather_appearance(c.simulation.track_cell_records[id - 1])) {
            release_world_person_umbrella_resource(c, *person);
            umbrella = false;
        } else {
            const auto& modifier = c.visitors.environment.PersonSpriteUmbrellaModifiers.at(person->exact_sprite_type_index);
            const auto projection = c.view.flags & 0x16 ? modifier.FPProjection : modifier.Projection;
            umbrella_pose = interpolate_mobile_entity_render_position(entity, static_cast<float>(static_cast<double>(interpolation) + projection), false);
        }
    }
    const auto sector = ((primary.angle - 896u) & 2047u) >> 8;
    if (!entity.sprite_under_ride_control) {
        world_operations::set_resource_variant_transform(c, entity.animation.sprite_script, primary.position, static_cast<std::int32_t>(sector));
    }
    if (next_world_grid_change_serial(c) < entity.stranded_time) {
        entity.stranded_time = 0;
    }
    if (entity.stranded_time) {
        const auto cell = person_detail::cell_id(base);
        const auto index = static_cast<std::uint32_t>(cell) - 1u;
        c.ports.meshes.draw_stranded_mobile_cell(c, { static_cast<float>((index & 127u) * 10u), 1, static_cast<float>((index >> 7) * 10u) });
    }
    const auto thought = entity.thought_history.thought_script;
    if (thought) {
        auto p = primary.position;
        p[1] = static_cast<float>(static_cast<double>(umbrella ? 6.0f : 2.5f) + p[1]);
        world_operations::set_resource_variant_transform(c, thought, p, 0);
    }
    if (balloon) {
        const auto phase = (c.view.mobile.balloon_phase + base.header.id) % 20u;
        WorldVector3 p;
        p[2] = static_cast<float>(static_cast<double>(1.5f) * 0.0 + secondary.position[2]);
        p[1] = static_cast<float>(static_cast<double>(1.0f) - (std::abs(static_cast<double>(secondary.position[0]) - primary.position[0]) + std::abs(static_cast<double>(secondary.position[2]) - primary.position[2])) / 0.5f + static_cast<double>(1.5f) * balloon_vertical_curve[phase] + secondary.position[1]);
        p[0] = static_cast<float>(static_cast<double>(1.5f) * 0.0 + secondary.position[0]);
        update_person_balloon_render_pose(c, *person, p, 0);
        if (!(c.view.mobile.balloon_phase_fraction >= 1.0f)) {
            c.view.mobile.balloon_phase_fraction = static_cast<float>(static_cast<double>(0.1f) + c.view.mobile.balloon_phase_fraction);
        } else {
            c.view.mobile.balloon_phase_fraction = 0;
            c.view.mobile.balloon_phase = (c.view.mobile.balloon_phase + 1u) % 20u;
        }
    }
    if (!umbrella) {
        return;
    }
    const auto& modifier = c.visitors.environment.PersonSpriteUmbrellaModifiers.at(person->exact_sprite_type_index);
    auto p = umbrella_pose.position;
    if (c.view.flags & 0x16) {
        p[0] += person->sprite_geometry_x.at(sector);
        p[2] += person->sprite_geometry_y.at(sector);
        p[2] += modifier.FPZ;
        p[1] += modifier.FPY;
        p[0] += modifier.FPX;
    } else {
        const auto& generic = c.visitors.environment.GenericUmbrellaModifiers;
        p[0] = static_cast<float>(p[0] - (static_cast<double>(p[0]) - c.view.mobile.camera_position[0]) * generic.XAdjustCamera);
        p[1] = static_cast<float>((static_cast<double>(c.view.mobile.camera_zoom) - 40.0) * generic.YAdjustCamera + p[1]);
        p[2] = static_cast<float>(p[2] - (static_cast<double>(p[2]) - c.view.mobile.camera_position[2]) * generic.ZAdjustCamera);
        p[1] += modifier.Y;
    }
    update_person_umbrella_render_pose(c, *person, p, 0);
}
} //namespace simcoaster
