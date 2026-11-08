#include "simcoaster/rotation_tables.hpp"
#include "simcoaster/advisor_speech.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace simcoaster {
namespace {
MeshPlaybackInstance* selected_mesh(AdvisorWorld& c) {
    return c.advisor.presentation.meshes.at(c.advisor.speech.render_slot_index).get();
}

bool equal_name(std::string_view left, std::string_view right) {
    const auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
    };
    return left.size() == right.size() && std::ranges::equal(left, right, [&](char a, char b) {
               return lower(a) == lower(b);
           });
}

void flag_node(MeshModel& model, std::size_t index, bool hidden) {
    auto& flags = model.nodes.at(index).flags;
    if (hidden)
        flags |= 16u;
    else
        flags &= ~16u;
}

void flag_resource(MeshModel& model, std::int32_t id, bool hidden) {
    //4653A0/465440 add metadata_first_node_index to the returned resource
    //index. The host parser already records that resolved node explicitly;
    //metadata row0 is not node0 in any shipped Advisor model.
    if (const auto resource = model.find_resource(0x400u, static_cast<std::uint32_t>(id)))
        flag_node(model, model.resources.at(*resource).node_index, hidden);
}

void replace_texture(MeshModel& model, std::int32_t old_slot, std::int32_t new_slot) {
    if (old_slot == -1 || new_slot == -1)
        return;
    for (auto& node : model.nodes)
        if (node.drawable)
            for (auto& group : node.drawable->groups)
                if (group.texture_slot == static_cast<std::size_t>(old_slot))
                    group.texture_slot = static_cast<std::size_t>(new_slot);
}

void set_particle_position(AdvisorWorld& c, bool snap, float x, float y, float duration) {
    auto& motion = c.advisor.presentation.ticket_motion;
    motion.position_from = snap ? std::array{ x, y } : motion.position_to;
    motion.position_to = { x, y };
    if (duration != 0.f) {
        motion.start_ms = static_cast<float>(fixed::word(advisor_presentation_time_ms(c)));
        motion.duration_ms = duration;
    }
}

void set_particle_extents(AdvisorWorld& c, float x, float y, float phase) {
    auto& motion = c.advisor.presentation.ticket_motion;
    motion.extent_from = motion.extent_to;
    motion.extent_to = { x, y };
    motion.phase_scale = phase;
}
} //namespace

void set_advisor_render_transforms(AdvisorWorld& c, bool repositioned) {
    for (auto& transform : c.advisor.presentation.render_transforms) {
        transform = mesh_identity_matrix;
        transform[12] = .75f;
        transform[13] = repositioned ? -90.6f : -.6f;
        transform[14] = .9f;
    }
}

void load_advisor_mesh_resources(AdvisorWorld& c, const std::filesystem::path& root, std::string_view theme) {
    const std::string suffix = std::string{ theme.substr(0, 3) };
    const std::array directories{ root / "Data/global/Advisor", root / "Data/levels" / theme / "advisor.wad" };
    for (std::size_t i = 0; i < directories.size(); ++i) {
        auto library = open_mesh_model_library(directories[i]);
        if (!library)
            throw std::runtime_error(describe_mesh_error(library.error()));
        const auto filename = i ? "Advisor_" + suffix + ".md2" : std::string{ "Advisor.md2" };
        auto loaded = read_mesh_playback_assets(*library, filename);
        if (!loaded)
            throw std::runtime_error(describe_mesh_error(loaded.error()));
        auto assets = std::make_shared<MeshPlaybackAssets>(std::move(*loaded));
        //Native load50120: always animate and retire on completion. The render
        //slot also enables model context matrices; these clones stay outside World.
        assets->model.flags |= 0x220u;
        if (assets->model.root) {
            auto& node = assets->model.nodes.at(*assets->model.root);
            node.flags |= 0x80800u;
            auto& matrix = node.local_transform;
            constexpr std::array scales{ .01125f, .001f, .015f };
            for (std::size_t axis = 0; axis < 3; ++axis)
                for (std::size_t component = 0; component < 3; ++component)
                    matrix[axis * 4 + component] *= scales[axis];
        }
        auto playback = create_mesh_playback(std::move(assets), 1, 0x100000Cu, false, true);
        if (!playback)
            throw std::runtime_error(describe_mesh_error(playback.error()));
        c.advisor.presentation.meshes[i] = std::make_shared<MeshPlaybackInstance>(std::move(*playback));
    }
    set_advisor_render_transforms(c, false);
    initialize_advisor_speech_lookup(c);
}

void advance_advisor_speech_meshes(AdvisorWorld& c) {
    if (!c.resources.meshes)
        throw std::logic_error("advisor meshes need frame clocks");
    for (const auto& mesh : c.advisor.presentation.meshes)
        if (mesh) {
            const auto advanced = advance_mesh_playback(*mesh, c.resources.meshes->clocks, true);
            if (!advanced)
                throw std::runtime_error(describe_mesh_error(advanced.error()));
        }
    update_advisor_mesh_transition(c);
}

void resize_advisor_ticket_particles(AdvisorWorld& c, std::size_t count, std::uint32_t definition) {
    auto& particles = c.advisor.presentation.ticket_particles;
    while (particles.size() > count) {
        const auto handle = particles.back();
        particles.pop_back();
        if (handle)
            c.ports.effects.set_particle_effect_lifetime(c, handle, -2);
    }
    while (particles.size() < count)
        particles.push_back(c.ports.effects.create_particle_effect(c, definition, { 0, 0, 0 }, 0));
}

void update_advisor_ticket_particles(AdvisorWorld& c) {
    auto& view = c.advisor.presentation;
    const auto& motion = view.ticket_motion;
    if (view.ticket_particles.empty())
        return;
    const auto t = static_cast<float>(
        (static_cast<double>(fixed::word(advisor_presentation_time_ms(c))) - motion.start_ms) / motion.duration_ms);
    const float ext = t <= 1.f ? t : 1.f;
    const auto center = [&](std::size_t axis) {
        return static_cast<float>((static_cast<double>(motion.position_to[axis]) - motion.position_from[axis]) * t
                                  + motion.position_from[axis]);
    };
    const auto cx = center(0), cy = center(1);
    for (std::size_t i = 0; i < view.ticket_particles.size(); ++i) {
        double integral;
        const auto phase = static_cast<std::uint32_t>(
            fixed::truncate_integer(std::modf(static_cast<double>(i) / static_cast<double>(view.ticket_particles.size())
                                                  + static_cast<double>(t) * motion.phase_scale,
                                              &integral)
                                    * 512.0));
        const auto sn = static_cast<float>(particle_phase_sines()[phase & 511u]) / 256.f;
        const auto co = static_cast<double>(particle_phase_sines()[(phase + 128u) & 511u]) / 256.0;
        const auto ex =
            (static_cast<double>(motion.extent_to[0]) - motion.extent_from[0]) * ext + motion.extent_from[0];
        const auto ey =
            (static_cast<double>(motion.extent_to[1]) - motion.extent_from[1]) * ext + motion.extent_from[1];
        const auto offset = static_cast<float>(ex * sn), nx = static_cast<float>(static_cast<double>(offset) + cx);
        c.ports.effects.set_particle_effect_position(
            c,
            view.ticket_particles[i],
            { fixed::truncate_integer((static_cast<double>(nx) + 1.0) * 50000.0),
              0,
              fixed::truncate_integer((static_cast<double>(cy) - co * ey + 1.0) * 37500.0) });
    }
}

void initialize_advisor_speech_lookup(AdvisorWorld& c) {
    c.advisor.speech = {};
    auto& state = c.advisor.speech;
    auto& view = c.advisor.presentation;
    state.mouth_shape_count = 5;
    state.last_applied_mouth_shape_selector = -1;
    constexpr std::array<std::string_view, 6> names{ "mouth - normal", "mouth - aah", "mouth - eee",
                                                     "mouth - ooh",    "mouth - sss", "bug head" };
    MeshPlaybackInstance* selected = nullptr;
    for (std::size_t slot = 0; slot < view.meshes.size(); ++slot) {
        auto* mesh = view.meshes[slot].get();
        if (!mesh)
            continue;
        selected = mesh;
        for (std::size_t shape = 0; shape < names.size(); ++shape) {
            const auto found = std::ranges::find_if(mesh->model.nodes, [&](const auto& node) {
                return equal_name(node.name, names[shape]);
            });
            const auto index =
                found == mesh->model.nodes.end() ? 0 : static_cast<std::int32_t>(found - mesh->model.nodes.begin());
            if (shape < 5)
                state.mouth_shape_node_indices_by_slot[slot][shape] = index;
            else
                state.bug_head_node_index_by_slot[slot] = index;
        }
    }
    if (auto* award = view.meshes[1].get()) {
        selected = award;
        view.award_texture_index = 0;
        for (std::size_t group = 0; group < 2; ++group)
            for (std::size_t item = 0; item < 8; ++item) {
                const auto name = item ? std::format("awardtexture{}{}.tga", item + 1, group + 1)
                                       : std::format("awardtexture{}.tga", group + 1);
                const auto& textures = award->model.textures;
                const auto found = std::ranges::find_if(textures, [&](const auto& texture) {
                    return equal_name(texture.filename, name);
                });
                view.award_texture_slots[group][item] =
                    found == textures.end() ? -1 : static_cast<std::int32_t>(found - textures.begin());
            }
    }
    if (selected) {
        for (std::size_t i = 0; i < state.animation_duration_ms.size(); ++i)
            if (const auto* animation = selected->assets->animations.find(5, i))
                state.animation_duration_ms[i] = 1000u * animation->end_frame / 30u;
        state.animation_duration_ms[13] = 0;
        resize_advisor_ticket_particles(c, 0, 0);
        view.ticket_motion = {};
    }
}

void apply_advisor_mesh_command(AdvisorWorld& c, std::int32_t command) {
    auto* mesh = selected_mesh(c);
    if (!mesh)
        return;
    auto& model = mesh->model;
    auto& state = c.advisor.speech;
    auto& view = c.advisor.presentation;
    if (command >= 49 && command <= 56) {
        const auto replacement = static_cast<std::size_t>(command - 49);
        for (const auto& group : view.award_texture_slots)
            replace_texture(model, group.at(static_cast<std::size_t>(view.award_texture_index)), group[replacement]);
        view.award_texture_index = command - 49;
        return;
    }
    const bool hide_only = (state.animation_response_flags & 0x10000u) != 0;
    match (command) {
        case -1 => {
            for (std::size_t i = 0; i < 4; ++i) {
                if (state.advisor_mesh_node_ids_to_clear[i] && !hide_only)
                    flag_resource(model, state.advisor_mesh_node_ids_to_clear[i], false);
                if (state.advisor_mesh_node_ids_to_set[i])
                    flag_resource(model, state.advisor_mesh_node_ids_to_set[i], true);
            }
        }
        case 0 or 2 => {
            flag_resource(model, 19, false);
            flag_resource(model, 20, false);
        }
        case 3 or 4 or 5 or 6 or 7 or 9 or 10 or 11 or 13 or 24 => {
            flag_resource(model, 19, true);
            flag_resource(model, 20, true);
        }
        case 14 => flag_resource(model, 21, true);
        case _  => ;
    } if (command && !hide_only)
        flag_resource(model, command, false);
}

void update_advisor_mesh_state(AdvisorWorld& c) {
    auto* mesh = selected_mesh(c);
    auto& state = c.advisor.speech;
    auto& view = c.advisor.presentation;
    if (!mesh || view.repositioned_render_transforms)
        return;
    set_advisor_render_transforms(c, false);
    auto& model = mesh->model;
    flag_node(model, 32, view.fixed_pose_sequence);
    for (std::uint32_t i = 0; i < state.mouth_shape_count; ++i) {
        const auto node = state.mouth_shape_node_indices_by_slot.at(state.render_slot_index).at(i);
        if (node < 0)
            throw std::out_of_range("negative advisor mouth shape");
        flag_node(model, static_cast<std::size_t>(node), true);
    }
    state.last_applied_mouth_shape_selector = -1;
    if (!view.fixed_pose_sequence) {
        apply_advisor_mesh_command(c, state.initial_mesh_animation_command);
        if (state.secondary_mesh_animation_command)
            apply_advisor_mesh_command(c, state.secondary_mesh_animation_command);
    }
    if (state.animation_response_flags & 0x10000u)
        flag_node(model, static_cast<std::size_t>(state.bug_head_node_index_by_slot.at(state.render_slot_index)), true);
    const auto effect = state.animation_response_flags & 0x3E0000u;
    if (!effect)
        return;
    if (!state.response_active) {
        if (state.animation_response_flags & 0x200000u)
            resize_advisor_ticket_particles(c, 0, 0);
        return;
    }
    if (state.active_animation_duration_index < 0)
        throw std::out_of_range("negative advisor effect duration");
    const auto duration = static_cast<float>(
        fixed::word(state.animation_duration_ms.at(static_cast<std::size_t>(state.active_animation_duration_index))));
    match (effect) {
        case 0x20000u => {
            set_particle_position(c, true, 0, 0, duration);
            set_particle_extents(c, .2f, .2f, .2f);
        }
        case 0x40000u => {
            (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 197, 0, 0, 0);
            resize_advisor_ticket_particles(c, 2, 49);
            set_particle_position(c, false, 0, 0, duration);
            set_particle_extents(c, .6f, .4f, 2.f);
        }
        case 0x80000u => {
            resize_advisor_ticket_particles(c, 3, 49);
            set_particle_position(c, false, 0, 0, duration);
            set_particle_extents(c, .6f, .4f, 3.f);
        }
        case 0x100000u => {
            resize_advisor_ticket_particles(c, 0, 44);
            resize_advisor_ticket_particles(c, 2, 44);
            set_particle_position(c, false, -.8f, -.8f, duration);
            set_particle_extents(c, 0, 0, 2.f);
        }
        case 0x200000u => resize_advisor_ticket_particles(c, 0, 0);
        case _         => ;
    }
}
} //namespace simcoaster
