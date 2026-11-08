#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/simulation_sine.hpp"
#include "coaster_station_geometry.hpp"
#include <format>
#include <algorithm>
#include <cmath>
#include <limits>
#include <source_location>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;
constexpr std::array categories { 3u, 10u, 2u, 9u, 10u }, variants { 0u, 0u, 0u, 0u, 1u };

void require(bool value, std::size_t index = 0, std::source_location where = std::source_location::current()) {
    if (!value) {
        throw MeshError { MeshFailure::configuration, index, std::format("Coaster geometry restoration, line {}", where.line()), {} };
    }
}

WorldMeshInstance& mesh(WorldMeshRuntime& meshes, std::uint32_t slot) {
    auto* value = meshes.instance(slot);
    require(value != nullptr, slot);
    return *value;
}

Point position(const MeshMatrix& m) {
    return { m[12], m[13], m[14] };
}

float length(const Point& p) {
    return std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
}

float distance(const Point& a, const Point& b) {
    return length({ a[0] - b[0], a[1] - b[1], a[2] - b[2] });
}

float nonzero(float value, float fallback) {
    return value != 0 ? value : fallback;
}

float wrapped(float value) {
    double integral {};
    auto result = static_cast<float>(std::modf(value, &integral));
    return result < 0 ? result + 1 : result;
}

MeshMatrix attachment(const MeshModel& model, std::size_t resource) {
    auto poses = evaluate_mesh_pose(model);
    if (!poses) {
        throw poses.error();
    }
    auto result = mesh_attachment_transform(model, *poses, resource);
    if (!result) {
        throw result.error();
    }
    return *result;
}

void evaluate(MeshPlaybackInstance& playback) {
    auto result = evaluate_mesh_playback_pose(playback, true);
    if (!result) {
        throw result.error();
    }
}

void select_channels(WorldMeshRuntime& meshes, AttractionWorld& c, std::uint32_t slot, const CoasterPylonRuntime& data) {
    for (std::size_t i = 0; i < 5; ++i) {
        if (data.flags & (32u << i)) {
            auto requested = meshes.request(c, slot, i, { { categories[i], variants[i] }, 2, 1 });
            if (!requested) {
                throw requested.error();
            }
        }
    }
    mesh(meshes, slot).playback.runtime_flags |= 0x1000u;
}

void apply_channels(MeshPlaybackInstance& playback, const CoasterPylonRuntime& data, std::array<float, 5>& parameters, std::uint32_t flags) {
    require(playback.controls.size() >= 5);
    parameters[0] = std::clamp(parameters[0], 0.0f, 1.0f);
    parameters[4] = wrapped(parameters[4]);
    for (std::size_t i = 0; i < 5; ++i) {
        const auto& range = data.channels[i];
        auto input = parameters[i];
        if (i == 1 && (flags & 0x10000000u)) {
            input = std::fmod(input + 0.5f, 1.0f);
        }
        auto normalized = static_cast<double>(input) * range.scale + range.bias;
        if (i == 2 || i == 3) {
            normalized += 0.5;
        }
        normalized = std::clamp(normalized, static_cast<double>(range.minimum), static_cast<double>(range.maximum));
        auto& control = playback.controls[i];
        control.frame = static_cast<float>(normalized * control.total_frames);
        control.flags |= 16u;
    }
    evaluate(playback);
}

void load_channel_ranges(CoasterPylonRuntime& target, const DynamicTexturePylonBalance& input) {
    constexpr std::array<std::string_view, 5> prefixes { "fLoftAnim", "fRotateAnim", "fInclineAnim", "fBankAnim", "fLoopRotateAnim" };
    template for (constexpr auto channel : std::array<std::size_t, 5> { 0, 1, 2, 3, 4 }) {
        template for (constexpr auto member : std::define_static_array(std::meta::nonstatic_data_members_of(^^DynamicTexturePylonBalance, std::meta::access_context::current()))) {
            constexpr auto name = std::meta::identifier_of(member);
            if constexpr (name == std::string { prefixes[channel] } + "StartCap") {
                if (input.[:member:] != 0) {
                    target.channels[channel].minimum = input.[:member:];
                }
            }
            if constexpr (name == std::string { prefixes[channel] } + "EndCap") {
                if (input.[:member:] != 0) {
                    target.channels[channel].maximum = input.[:member:];
                }
            }
            if constexpr (name == std::string { prefixes[channel] } + "Multiplier") {
                if (input.[:member:] != 0) {
                    target.channels[channel].scale = input.[:member:];
                }
            }
            if constexpr (name == std::string { prefixes[channel] } + "Add") {
                if (input.[:member:] != 0) {
                    target.channels[channel].bias = input.[:member:];
                }
            }
        }
    }
}

struct GeometryBuilder {
    AttractionWorld& c;
    WorldMeshRuntime& meshes;
    WorldCoasterRuntime& world;

    WorldCoasterBinding& ride(std::size_t i) {
        return world.bindings.at(i);
    }

    CoasterNodeState& state(CoasterNodeReference ref) {
        return ride(ref.ride).geometry_state.at(ref.node);
    }

    CoasterGeometryObjectInput& node(CoasterNodeReference ref) {
        return ride(ref.ride).geometry.at(ref.node);
    }

    const CoasterPylonRuntime& pylon(const CoasterPlacement& owner) {
        require(owner.definition != nullptr);
        return owner.definition->pylons.at(owner.pylon);
    }

    std::size_t owner(std::size_t r, std::uint32_t flags, std::int32_t x, std::int32_t y, std::size_t type, std::optional<Point> explicit_position = {}) {
        if (!explicit_position) {
            for (std::size_t i = 0; i < world.placements.size(); ++i) {
                const auto& p = world.placements[i];
                if (!(p.flags & 5u) && p.tile_x == x && p.tile_y == y) {
                    return i;
                }
            }
        }
        CoasterPlacement p;
        p.flags = flags;
        p.tile_x = x;
        p.tile_y = y;
        p.pylon = type;
        p.definition = explicit_position ? nullptr : ride(r).definition;
        if (explicit_position) {
            p.position = *explicit_position;
            const auto* terrain = c.construction.active_terrain_model_data ? c.construction.active_terrain_model_data->terrain_grid : nullptr;
            require(terrain && terrain->width_cells && terrain->height_cells);
            require(std::isfinite(p.position[0]) && std::isfinite(p.position[1]) && std::isfinite(p.position[2]));
            //47174C derives and clamps these tiles even though its caller supplies0,0.
            p.tile_x = static_cast<std::int32_t>(std::clamp(static_cast<double>(p.position[0]) * 0.1f, 0.0, static_cast<double>(terrain->width_cells - 1)));
            p.tile_y = static_cast<std::int32_t>(std::clamp(static_cast<double>(p.position[2]) * 0.1f, 0.0, static_cast<double>(terrain->height_cells - 1)));
        } else {
            require(c.construction.active_terrain_model_data && c.construction.active_terrain_model_data->terrain_grid);
            auto height = mesh_placement_reference_height(*c.construction.active_terrain_model_data->terrain_grid, { x, y }, nullptr, 1, 1, 0, 0);
            if (!height) {
                throw height.error();
            }
            p.position = { static_cast<float>(x) * 10, *height, static_cast<float>(y) * 10 };
        }
        world.placements.push_back(std::move(p));
        return world.placements.size() - 1;
    }

    std::size_t add(std::size_t r, std::size_t owner_id, std::uint32_t flags, std::uint32_t slot, std::size_t link, std::array<float, 5> controls = {}, std::array<float, 4> angles = {}) {
        auto& binding = ride(r);
        auto& placement = world.placements.at(owner_id);
        if (placement.links.size() <= link) {
            placement.links.resize(link + 1);
        }
        require(!placement.links[link].has_value(), link);
        const auto index = binding.geometry.size();
        CoasterGeometryObjectInput input;
        input.geometry_index = static_cast<std::uint32_t>(index);
        input.geometry_flags = flags;
        input.inline_position = placement.position;
        if (!(placement.flags & 1u)) {
            const auto& data = pylon(placement);
            input.has_runtime_data = true;
            input.runtime_data_flags = data.flags;
            input.minimum_tangent_dot = data.minimum_tangent_dot;
            input.inline_position[0] += 5;
            input.inline_position[2] += 5;
            require(slot != 0);
            select_channels(meshes, c, slot, data);
            mesh(meshes, slot).playback.runtime_flags |= 0x80000u;
        }
        binding.geometry.push_back(std::move(input));
        binding.geometry_state.push_back({ owner_id, slot, controls, angles, {} });
        placement.links[link] = CoasterNodeReference { r, index };
        return index;
    }

    void implicit_nodes(std::size_t r) {
        auto& binding = ride(r);
        auto& parent = mesh(meshes, binding.mesh_slot).playback.model;
        const auto root_y = parent.root ? parent.nodes[*parent.root].local_transform[13] : 0;
        for (std::uint32_t id = 1; id <= 3; ++id) {
            if (const auto resource = parent.find_resource(0x400000u, id)) {
                auto point = position(attachment(parent, *resource));
                if (id == 2) {
                    auto o = owner(r, 19u, 0, 0, 0, point);
                    add(r, o, 0x1E00u, 0, 0);
                    continue;
                }
                const auto x = static_cast<std::int32_t>(static_cast<double>(point[0]) * 0.1f), y = static_cast<std::int32_t>(static_cast<double>(point[2]) * 0.1f);
                auto o = owner(r, 18u, x, y, 0);
                const auto& data = pylon(world.placements[o]);
                const auto template_id = binding.definition->pylon_templates[0];
                require(template_id != 0);
                const auto* source = meshes.definition(template_id);
                require(source != nullptr);
                DefinitionGrid shape {};
                shape.width = source->footprint_width;
                shape.height = source->footprint_height;
                const auto slot = meshes.mark_for_refresh(c, { 0, static_cast<std::int32_t>(template_id), 0x3FAu, { 0, x, y, &shape }, 0, nullptr, nullptr, nullptr });
                require(slot != 0, template_id);
                const auto span = data.height_maximum - data.height_minimum;
                require(span != 0, template_id);
                std::array<float, 5> parameters {};
                parameters[0] = coaster_station_detail::loft_parameter(data, point[1], root_y);
                const auto flags = id == 1 ? (0x80000u | 0x400u | 0x1800u) : (0x40000u | 0x200u);
                add(r, o, flags, slot, 0, parameters);
            }
        }
        require(binding.geometry.size() == 3, binding.mesh_slot);
    }

    void saved_nodes(std::size_t r) {
        auto& binding = ride(r);
        unsigned remaining = 0;
        for (std::size_t i = 0; i < binding.saved_geometry.geometry.size(); ++i) {
            const auto& saved = binding.saved_geometry.geometry[i].header;
            const auto packed = saved.packed_flags;
            if (packed & 8u) {
                remaining = 2;
            }
            const auto type = remaining ? 1u : 0u;
            const auto& definition = *binding.definition;
            require(definition.pylon_templates[type] != 0);
            std::uint32_t flags = packed & 7u;
            if (type) {
                flags |= remaining == 2 ? 0xE0u : 0x60u;
            }
            constexpr std::array wire { 0x100u, 0x200u, 0x40u, 0x80u, 0x400u, 0x800u }, runtime { 0x40000u, 0x80000u, 0x10000u, 0x20000u, 0x200000u, 0x400000u };
            for (std::size_t j = 0; j < wire.size(); ++j) {
                if (packed & wire[j]) {
                    flags |= runtime[j];
                }
            }
            auto o = owner(r, 0, static_cast<std::uint16_t>(saved.tile_x), static_cast<std::uint16_t>(saved.tile_y), type);
            std::array<float, 5> controls {};
            controls[0] = std::clamp(saved.loft, 0.0f, 1.0f);
            std::array<float, 4> angles {};
            angles[3] = saved.rotation;
            if (type) {
                controls[4] = saved.wobble;
            } else {
                angles[1] = saved.angle_a;
                angles[2] = saved.angle_b;
            }
            const auto index = add(r, o, flags, saved.mesh_slot, saved.link_slot, controls, angles);
            binding.geometry_state[index].saved_ordinal = i;
            //Physical saved nodes, including loop halves, establish placement bit4.
            //Default station supports use creation flags that bypass this bit.
            binding.train_state.placement_flags |= 4u;
            if (!type) {
                const auto& data = pylon(world.placements[o]);
                binding.default_attachment_height = static_cast<float>((static_cast<double>(data.height_maximum) - data.height_minimum) * controls[0] + data.height_minimum);
            }
            if (remaining) {
                --remaining;
            }
        }
    }

    void update_owners() {
        for (auto& owner : world.placements) {
            if (!(owner.flags & 1u)) {
                const auto& data = pylon(owner);
                float cumulative = 0;
                std::optional<CoasterNodeReference> previous;
                for (std::size_t i = 0; i < owner.links.size(); ++i) {
                    require(owner.links[i].has_value(), i);
                    const auto ref = *owner.links[i];
                    auto& input = node(ref);
                    auto& current = state(ref);
                    const bool nonterminal = i + 1 < owner.links.size();
                    if (nonterminal) {
                        input.geometry_flags |= 0x100u;
                    } else {
                        input.geometry_flags &= ~0x100u;
                    }
                    //The native control setter clamps before the attachment position is
                    //derived. Applying channels later must not leave a stale first height.
                    if (!(input.geometry_flags & 0x2000u)) {
                        current.controls[0] = std::clamp(current.controls[0], 0.0f, 1.0f);
                    }
                    input.inline_position = { owner.position[0] + 5, owner.position[1] + cumulative + (data.height_maximum - data.height_minimum) * current.controls[0] + data.height_minimum, owner.position[2] + 5 };
                    cumulative = input.inline_position[1] - owner.position[1] + data.height_offset;
                    if (!(input.geometry_flags & 0x2000u)) {
                        auto& live = mesh(meshes, current.mesh_slot);
                        auto& model = live.playback.model;
                        require(model.root.has_value());
                        auto height = owner.position[1];
                        if (previous) {
                            auto& prior = node(*previous);
                            if (prior.geometry_flags & 0x2000u) {
                                height = prior.inline_position[1] + data.height_offset;
                            } else {
                                require(data.stack_resource.has_value());
                                height = attachment(mesh(meshes, state(*previous).mesh_slot).playback.model, *data.stack_resource)[13];
                            }
                        }
                        model.nodes[*model.root].local_transform[13] = height;
                        apply_channels(live.playback, data, current.controls, input.geometry_flags);
                        if (live.terrain_base && live.terrain_base->best_node) {
                            auto& flags = model.nodes[*live.terrain_base->best_node].flags;
                            if (i && !(data.flags & 0x10u)) {
                                flags |= 0x80000010u;
                            } else if (!i) {
                                flags &= ~0x10u;
                            }
                        }
                        const auto toggle = [&](std::size_t cap, bool hidden) {
                            if (data.cap_nodes[cap]) {
                                auto& flags = model.nodes[*data.cap_nodes[cap]].flags;
                                if (hidden) {
                                    flags |= 0x80000010u;
                                } else {
                                    flags &= ~0x10u;
                                }
                            }
                        };
                        if (!nonterminal) {
                            toggle(0, true);
                            toggle(1, false);
                        } else {
                            require(owner.links[i + 1].has_value(), i + 1);
                            if (!(node(*owner.links[i + 1]).geometry_flags & 0x2000u)) {
                                toggle(0, false);
                            }
                            toggle(1, true);
                        }
                        toggle(2, i == 0);
                        toggle(3, i != 0);
                    }
                    previous = ref;
                }
            }
        }
    }

    void closing_node(std::size_t r) {
        auto& binding = ride(r);
        if (!(binding.train_state.placement_flags & 1u)) {
            return;
        }
        require(!binding.geometry_state.empty());
        auto& first = binding.geometry_state[0];
        const auto& data = pylon(world.placements[first.placement]);
        require(data.profile_resource.has_value(), first.mesh_slot);
        const auto point = position(attachment(mesh(meshes, first.mesh_slot).playback.model, *data.profile_resource));
        const auto o = owner(r, 19u, 0, 0, 0, point);
        add(r, o, 0x180000u, 0, 0);
    }

    void restore_edit_anchor(std::size_t r) {
        auto& binding = ride(r);
        if (!(binding.train_state.placement_flags & 2u)) {
            return;
        }
        require(binding.geometry.size() >= 3, binding.mesh_slot);
        //478465..478472 seeds current/validation from the third station node.
        //Saved bit40 replaces current;478995..4789B0 restores the implicit
        //station endpoint's50000 flags even though that node is not serialized.
        std::size_t current = 2;
        for (std::size_t i = 3; i < binding.geometry.size(); ++i) {
            if (binding.geometry[i].geometry_flags & 0x10000u) {
                current = i;
            }
        }
        if (current == 2) {
            binding.geometry[current].geometry_flags |= 0x50000u;
        }
    }

    void collect(std::size_t r) {
        auto& binding = ride(r);
        const auto& balance = binding.definition->balance.coaster;
        require(rebuild_coaster_design_splines(binding.geometry, nonzero(balance.fCurviness, 1.25f), nonzero(balance.fStraightDivisor, 1.25f), balance.bIsSuspendedCoaster != 0));
        float speed = 0;
        for (std::size_t i = 0; i < binding.geometry.size(); ++i) {
            auto& input = binding.geometry[i];
            auto& current = binding.geometry_state[i];
            const auto& owner = world.placements[current.placement];
            const auto& tangent = input.inline_outgoing_tangent;
            auto& angles = current.angles;
            const auto heading = static_cast<float>(std::atan2(tangent[0], tangent[2]));
            angles[0] = heading;
            if (!(input.geometry_flags & 1u)) {
                angles[3] = heading;
            }
            if (!(input.geometry_flags & 2u)) {
                const auto s = simulation_sine_at_rounded_index(-heading * simulation_sine_samples_per_radian), co = simulation_sine_at_rounded_index((-heading + 1.5707964f) * simulation_sine_samples_per_radian);
                const auto z = tangent[2] * co - tangent[0] * s;
                angles[2] = static_cast<float>(std::atan2(-tangent[1], z));
            }
            if (!(input.geometry_flags & 4u)) {
                angles[1] = 0;
            }
            if (!balance.bNoAutoBanking && !balance.bIsSuspendedCoaster && !(input.geometry_flags & 4u) && i + 1 < binding.geometry.size()) {
                //46DF93 initializes four;46D1D0 never binds the parsed SAM leaf.
                if (input.junction_angle != 0 && !(input.geometry_flags & 0x100u) && i) {
                    const auto total = distance(binding.geometry[i - 1].inline_position, input.inline_position) + distance(binding.geometry[i + 1].inline_position, input.inline_position);
                    require(total > 0);
                    const auto target = input.junction_angle < 0 ? -3.1415927f : 3.1415927f;
                    angles[1] = (target - input.junction_angle) * speed / 4.0f / total;
                }
                const auto y = binding.geometry[i + 1].inline_position[1], delta = (input.inline_position[1] - y) * binding.train_state.configuration.acceleration_per_height;
                const auto modifier = y <= input.inline_position[1] ? binding.train_state.configuration.downhill_modifier : binding.train_state.configuration.uphill_modifier;
                speed = std::max(speed + delta * modifier, binding.train_state.configuration.minimum_speed);
            }
            current.controls[1] = wrapped(angles[3] * 0.15915494f);
            current.controls[2] = angles[2] * 0.15915494f;
            current.controls[3] = wrapped(angles[1] * 0.15915494f + 0.5f) - 0.5f;
            input.animation_heading = current.controls[1];
            input.animation_wobble = current.controls[4];
            if (!(owner.flags & 1u) && !(input.geometry_flags & 0x2000u)) {
                const auto& data = pylon(owner);
                auto& live = mesh(meshes, current.mesh_slot);
                apply_channels(live.playback, data, current.controls, input.geometry_flags);
                auto pose = evaluate_mesh_pose(live.playback.model);
                if (!pose) {
                    throw pose.error();
                }
                const bool alternate = (input.geometry_flags & 0x100u) && (data.flags & 8u);
                const auto count = alternate ? 3u : data.attachment_count, first = alternate ? 1u : data.first_attachment;
                input.component_attachment_index = alternate ? 1u : data.component_attachment;
                const auto attached = bind_coaster_model_attachments(input, live.playback.model, *pose, first, count, angles[3]);
                if (!attached) {
                    throw attached.error();
                }
            } else {
                CoasterAttachmentInput row;
                row.brace_position = input.inline_position;
                row.mesh_position = input.inline_position;
                if (input.geometry_flags & 0x40u) {
                    const auto angle = angles[3];
                    row.mesh_position[0] -= simulation_sine_at_rounded_index(angle * simulation_sine_samples_per_radian);
                    row.mesh_position[2] -= simulation_sine_at_rounded_index((angle + 1.5707964f) * simulation_sine_samples_per_radian);
                } else {
                    row.mesh_position[1] += 1;
                }
                input.attachments = { row };
                input.component_attachment_index = 0;
            }
        }
        for (std::size_t i = 0; i < binding.geometry.size(); ++i) {
            auto& input = binding.geometry[i];
            const auto limit = input.component_attachment_index.value_or(0);
            for (std::size_t j = 0; j < input.attachments.size(); ++j) {
                auto& a = input.attachments[j];
                if (j == limit) {
                    a.geometry_fraction = 0;
                } else if (j < limit && i) {
                    const auto d = distance(input.inline_position, binding.geometry[i - 1].inline_position);
                    require(d > 0);
                    a.geometry_fraction = -distance(input.inline_position, a.brace_position) / d;
                } else if (j > limit && i + 1 < binding.geometry.size()) {
                    const auto d = distance(input.inline_position, binding.geometry[i + 1].inline_position);
                    require(d > 0);
                    a.geometry_fraction = std::clamp(distance(input.inline_position, a.brace_position) / d, 0.0f, 1.0f);
                }
            }
        }
        const auto settings = coaster_profile_settings(binding.definition->balance);
        CoasterAttachmentRows rows;
        CoasterRideTrackRuntime rebuilt;
        require(build_coaster_attachment_rows(binding.geometry, settings.track_curviness, rows));
        require(build_coaster_track_samples(binding.geometry, std::move(rows), binding.train_state.track_sample_spacing, settings.entrance_interval, rebuilt.track));
        require(rebuild_coaster_speed_profile(binding.geometry, rebuilt.track, binding.train_state.configuration, settings, binding.train_state.track_sample_spacing, binding.speed_cap, rebuilt.profile));
        rebuilt.use_sample_bank = settings.force_banking;
        binding.metrics = rebuilt.profile;
        require(balance.bIsSuspendedCoaster == 0 || rebuilt.use_sample_bank);
        require(binding.definition->balance.mesh_definition != nullptr);
        //Opening/editing both need current samples, peers and generated geometry.
        //A lossy saved graph is fully repaired here, before any published consumer.
        rebuild_coaster_geometry_peers(binding, world, rebuilt.track);
        auto generated = build_coaster_track_mesh(*binding.definition->balance.mesh_definition, binding.geometry, rebuilt, &binding.collision, binding.selection_texture_bias);
        if (!generated) {
            throw generated.error();
        }
        binding.generated_mesh = std::move(*generated);
        binding.track = std::move(rebuilt);
        binding.geometry_dirty = false;
        //Keep the restore/reseed flags until the live train consumer has run.
    }
};
} //namespace

std::expected<void, MeshError> prepare_coaster_pylons(AttractionWorld& c, WorldMeshRuntime& meshes, WorldCoasterDefinition& definition) {
    try {
        for (std::size_t i = 0; i < definition.pylon_templates.size(); ++i) {
            if (const auto id = definition.pylon_templates[i]) {
                auto& data = definition.pylons[i];
                const auto* source = meshes.definition(id);
                require(source != nullptr);
                const auto& model = source->assets->model;
                const auto& balance = definition.balance.pylons[i];
                load_channel_ranges(data, balance);
                data.flags = (i ? (definition.balance.coaster.bSpecialIsTrough ? 0x8004u : 0x4004u) : 4u) | (balance.bDontDisableBaseOnStack ? 0x10u : 0u);
                data.minimum_tangent_dot = nonzero(balance.fPylonStraightDotThreshold, i ? -0.80000001f : -0.99900001f);
                require(std::isfinite(balance.fCollisionPossibleRadius) && balance.fCollisionPossibleRadius > 0, id);
                data.collision_radius = balance.fCollisionPossibleRadius;
                for (std::size_t j = 0; j < 5; ++j) {
                    if (source->assets->animations.find(categories[j], variants[j])) {
                        data.flags |= 32u << j;
                    }
                }
                const auto mode = model.resource_indices(0x1000000u).size();
                if (mode > 1) {
                    data.flags |= 8u;
                }
                data.first_attachment = mode > 1 ? 2u : (mode ? 1u : 0u);
                data.attachment_count = mode > 1 ? 1u : static_cast<std::uint32_t>(model.resource_indices(0x400000u).size());
                const auto profiles = model.resource_indices(0x800000u);
                if (!profiles.empty()) {
                    data.profile_resource = profiles[0];
                    data.component_attachment = mode > 1 ? 0u : model.resources[profiles[0]].id - 1;
                }
                const auto stack = model.resource_indices(0x100000u);
                if (!stack.empty()) {
                    data.stack_resource = stack[0];
                }
                for (std::uint32_t cap = 1; cap <= 4; ++cap) {
                    if (const auto resource = model.find_resource(0x80000u, cap)) {
                        data.cap_nodes[cap - 1] = model.resources[*resource].node_index;
                    }
                }
                require(data.profile_resource.has_value(), id);
                DefinitionGrid shape {};
                shape.width = source->footprint_width;
                shape.height = source->footprint_height;
                const auto created = meshes.create(c, { static_cast<std::int32_t>(id), 0x3FAu, { 0, 0, 0, &shape }, 0, 0, nullptr, 0, 0, -1, 0 });
                if (!created) {
                    throw created.error();
                }
                const auto slot = *created;
                try {
                    select_channels(meshes, c, slot, data);
                    auto& live = mesh(meshes, slot).playback;
                    const auto loft = [&](float value) {
                        auto& control = live.controls[0];
                        const auto& range = data.channels[0];
                        control.frame = std::clamp(value * range.scale + range.bias, range.minimum, range.maximum) * control.total_frames;
                        control.flags |= 16u;
                        evaluate(live);
                    };
                    loft(0);
                    data.height_minimum = attachment(live.model, *data.profile_resource)[13];
                    if (data.stack_resource) {
                        data.height_offset = attachment(live.model, *data.stack_resource)[13] - data.height_minimum;
                    }
                    loft(1);
                    data.height_maximum = attachment(live.model, *data.profile_resource)[13];
                } catch (...) {
                    meshes.erase(c, slot);
                    throw;
                }
                meshes.erase(c, slot);
            }
        }
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<void, MeshError> restore_coaster_geometry(AttractionWorld& c, WorldMeshRuntime& meshes) {
    try {
        auto* runtime = meshes.coasters();
        require(runtime != nullptr);
        GeometryBuilder builder { c, meshes, *runtime };
        runtime->placements.clear();
        for (std::size_t r = 0; r < runtime->bindings.size(); ++r) {
            auto& binding = runtime->bindings[r];
            binding.geometry.clear();
            binding.geometry_state.clear();
            binding.collision = {};
            binding.track.reset();
            binding.generated_mesh.reset();
            builder.implicit_nodes(r);
            builder.saved_nodes(r);
        }
        builder.update_owners();
        for (std::size_t r = 0; r < runtime->bindings.size(); ++r) {
            builder.closing_node(r);
            builder.restore_edit_anchor(r);
            seed_coaster_geometry_peer_snapshot(runtime->bindings[r]);
        }
        for (std::size_t r = 0; r < runtime->bindings.size(); ++r) {
            builder.collect(r);
        }
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<void, MeshError> refresh_coaster_geometry(AttractionWorld& c, WorldMeshRuntime& meshes, std::size_t ride_index) {
    try {
        auto* runtime = meshes.coasters();
        require(runtime && ride_index < runtime->bindings.size(), ride_index);
        GeometryBuilder builder { c, meshes, *runtime };
        builder.collect(ride_index);
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<void, MeshError> refresh_coaster_placements(AttractionWorld& c, WorldMeshRuntime& meshes) {
    try {
        auto* runtime = meshes.coasters();
        require(runtime != nullptr);
        GeometryBuilder builder { c, meshes, *runtime };
        builder.update_owners();
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
