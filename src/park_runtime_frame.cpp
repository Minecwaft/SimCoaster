#include "park_runtime_codec.hpp"
#include "park_runtime_state.hpp"

namespace simcoaster {
using namespace park_detail;

void ParkRuntime::State::advance(std::chrono::milliseconds elapsed, bool full_world, bool paused) {
    frame.frame_external_paused = paused;
    //Host input is queued before the frame. Commit its panel mutations and
    //modal-pause ownership before deciding whether gameplay clocks may move.
    if (presentation.ui) {
        presentation.ui->begin_frame();
        presentation.ui->controls().drain();
    }
    auto& mesh_clocks = rendering.meshes.clocks;
    const bool ui_paused = frame.application_ui_paused || (presentation.ui && presentation.ui->simulation_paused());
    paused = paused || ui_paused;
    const auto frame_advance = frame.scheduler.advance_clocks(mesh_clocks,
                                                              elapsed,
                                                              simulation.world->timing.scale,
                                                              full_world,
                                                              paused,
                                                              ui_paused);
    rendering.meshes.simulation_delta = frame_advance.simulation_delta;
    const auto now = frame_advance.now;
    frame.scheduler.run_due_steps(
        now,
        [&] {
            update_particle_system(*simulation.particles);
            update_track_ride_simulations(*simulation.world);
            update_rsse_scripts(*simulation.world);
        },
        [&] {
            step_rsse_tours(*simulation.world);
            (void)process_due_resource_variant_scripts(*simulation.world);
        },
        [&] {
            if (full_world)
                run_world_simulation_tick(*simulation.world, simulation.world->simulation.current);
        },
        [&] {
            presentation.audio->world_step();
        });
    this->simulation.clocks.simulation_frame_time_ms = static_cast<std::uint32_t>(now);
    this->simulation.clocks.simulation_step_time_ms = static_cast<std::uint32_t>(*frame.scheduler.script_step_cursor);
    this->simulation.clocks.auxiliary_step_time_ms = static_cast<std::uint32_t>(*frame.scheduler.auxiliary_step_cursor);
    this->simulation.clocks.world_tick_boundary_ms = static_cast<std::uint32_t>(*frame.scheduler.world_step_cursor);
    this->simulation.clocks.simulation_substep_count = static_cast<std::uint32_t>(frame.scheduler.script_substeps);
    if (!simulation.world->mode.source_recreation)
        update_rsse_tour_bounds(*simulation.world);
    //Native687420 advances the overlay phase once per displayed frame and
    //eases the reference point halfway toward the current preview cell.
    if (const auto reference = std::exchange(tools.construction.state.preview_reference_reset, std::nullopt))
        frame.preview_reference = { static_cast<float>(reference->x) * 10.f, static_cast<float>(reference->y) * 10.f };
    const auto preview = tools.construction.state.preview_coordinates;
    if (preview.x >= 0 && preview.y >= 0) {
        frame.preview_reference[0] += (static_cast<float>(preview.x) * 10.f - frame.preview_reference[0]) * .5f;
        frame.preview_reference[1] += (static_cast<float>(preview.y) * 10.f - frame.preview_reference[1]) * .5f;
    }
    if (!paused && simulation.world->timing.scale > 0) {
        frame.preview_phase += .1f;
        const auto scale = std::clamp(simulation.world->timing.scale, 1., 2147483647.);
        frame.preview_blink_counter += static_cast<std::uint32_t>(scale);
        if (frame.preview_blink_counter > (tools.construction.state.show_red_faces ? 6u : 1u)) {
            tools.construction.state.show_red_faces = !tools.construction.state.show_red_faces;
            frame.preview_blink_counter = 0;
        }
    }
    presentation.audio->prepare_frame(paused);
    update_advisor_speech(*simulation.world);
    //Native6A4FD0: Advisor speech -> UI event30 (Intro slot3 polling) ->
    //attached people/tour interpolation -> selected mesh -> camera. Polling
    //Intro only after camera/render preparation changes its event ordering.
    const auto construction_quote = tools.construction.take_interaction_cost_quote(*simulation.world);
    if (tools.coaster_editor.geometry_interaction_active()) {
        const auto cost = tools.coaster_editor.geometry_interaction_cost();
        presentation.ui->set_interaction_cost(cost ? simulation.world->mode.game == GameMode::code_3 ? 0 : *cost
                                                   : -999);
    } else
        presentation.ui->set_interaction_cost(construction_quote.value_or(-999));
    presentation.ui->advance(elapsed);
    update_rsse_attached_people(*simulation.world);
    interpolate_track_ride_render_poses(
        *simulation.world,
        static_cast<float>(static_cast<double>(now - *frame.scheduler.script_step_cursor) * .032258064));
    render_rsse_tours(
        *simulation.world,
        static_cast<float>(static_cast<double>(now - *frame.scheduler.auxiliary_step_cursor) * .016129032));
    const auto updated = update_coaster_animation(*simulation.world, rendering.meshes);
    if (!updated)
        throw std::runtime_error(describe_mesh_error(updated.error()));
    //Native State10 advances inspected instances separately: background and
    //visibility passes skip their 0x3000 flags. Camera paths need this sample
    //before the attachment supplies the displayed camera transform.
    const auto selected = rendering.meshes.update(*simulation.world, WorldMeshUpdate::selected);
    if (!selected)
        throw std::runtime_error(describe_mesh_error(selected.error()));
    presentation.authoring->synchronize_from_world();
    //Camera attachment transforms now contain this frame's actual animation.
    //Advance RAW-history smoothing exactly once, then let every render, pick
    //and listener query consume the same cached camera-to-world pose.
    presentation.camera_input_result =
        presentation.camera_input_runtime.advance(*simulation.world, presentation.camera_input, elapsed);
    advance_world_camera_render(*simulation.world, elapsed);
    publish_render_camera(paused);
    //6A4FD0 advances both Advisor render slots and their transition after
    //updating the World camera, before submitting the displayed frame.
    advance_advisor_speech_meshes(*simulation.world);
    if (full_world)
        prepare_world_poses(
            static_cast<float>(static_cast<double>(now - *frame.scheduler.world_step_cursor) * .0040322579));
    else
        frame.mobile_poses_ready = false;
    bindings.update_control_particles(*simulation.world);
    presentation.audio->advance(elapsed);
}

void ParkRuntime::advance_coasters(std::chrono::milliseconds elapsed) {
    const auto advanced = advance_coaster_animation(*state_->simulation.world, state_->rendering.meshes, elapsed);
    if (!advanced)
        throw std::runtime_error(describe_mesh_error(advanced.error()));
}

void ParkRuntime::advance_scripted_rides(std::chrono::milliseconds elapsed) {
    state_->advance(elapsed, false, false);
}

void ParkRuntime::advance_frame(std::chrono::milliseconds elapsed, bool paused) {
    state_->advance(elapsed, true, paused);
}

void ParkRuntime::set_application_ui_paused(bool paused) {
    state_->frame.application_ui_paused = paused;
}
} //namespace simcoaster
