#include "park_runtime_codec.hpp"
#include "park_runtime_state.hpp"

namespace simcoaster {
using namespace park_detail;

void ParkRuntime::save(const std::filesystem::path& file) {
    auto& s = *state_;
    auto& c = *s.simulation.world;
    MemoryByteStream output;
    drain_pending_world_used_thing_removals(c);
    const bool coaster_edit = s.tools.coaster_editor.active_map().has_value();
    s.tools.coaster_editor.cancel(c, s.tools.construction);
    s.tools.coaster_editor.activate(c, 0, false);
    s.tools.coaster_catalog.clear(c);
    if (coaster_edit && (c.construction.mode == 21 || c.construction.mode == 22))
        s.tools.construction.set_mode(c, 0, false);
    const auto recording = s.tools.layout_replay.write_save_prefix(output);
    if (!recording)
        throw std::runtime_error(describe_layout_error(recording.error()));
    auto clocks = s.simulation.clocks;
    clocks.saved_simulation_snapshot_ms = static_cast<std::uint32_t>(s.rendering.meshes.clocks.simulation.count());
    clocks.saved_unscaled_snapshot_ms = static_cast<std::uint32_t>(s.rendering.meshes.clocks.unscaled.count());
    const auto frame = clocks.saved_simulation_snapshot_ms;
    clocks.auxiliary_step_time_ms = s.frame.scheduler.auxiliary_step_cursor
                                      ? static_cast<std::uint32_t>(*s.frame.scheduler.auxiliary_step_cursor)
                                      : frame;
    clocks.world_tick_boundary_ms =
        s.frame.scheduler.world_step_cursor ? static_cast<std::uint32_t>(*s.frame.scheduler.world_step_cursor) : frame;
    clocks.simulation_frame_time_ms = frame;
    clocks.simulation_step_time_ms = s.frame.scheduler.script_step_cursor
                                       ? static_cast<std::uint32_t>(*s.frame.scheduler.script_step_cursor)
                                       : frame;
    clocks.simulation_substep_count = static_cast<std::uint32_t>(s.frame.scheduler.script_substeps);
    {
        //The free viewer permits independent pitch/FOV and an absolute target;
        //the original camera wire format cannot represent that view exactly.
        //Preserve its native snapshot rather than mix a new viewer radius with
        //the saved legacy focus/heading. Restore live render inputs on errors too.
        struct RestoreZoom {
            float& target;
            float live;

            ~RestoreZoom() {
                target = live;
            }
        };

        RestoreZoom restore{ c.view.mobile.camera_zoom, c.view.mobile.camera_zoom };
        if (s.frame.native_camera_zoom)
            c.view.mobile.camera_zoom = *s.frame.native_camera_zoom;
        if (!write_world_through_camera(c,
                                        *c.simulation.current,
                                        output,
                                        s.bindings,
                                        *s.simulation.particles,
                                        clocks,
                                        s.simulation.rides,
                                        s.rendering.meshes))
            throw std::runtime_error("World-through-camera capture failed at " + s.bindings.last_field);
    }
    const auto limits = [&](std::uint16_t slot) -> std::optional<CoasterTrainLimits> {
        const auto* instance = s.rendering.meshes.instance(slot);
        const auto* definition = instance ? s.rendering.meshes.definition(instance->template_id) : nullptr;
        if (!definition || !definition->coaster)
            return std::nullopt;
        const auto& trains = definition->coaster->trains;
        return CoasterTrainLimits{ trains.maximum_trains, trains.maximum_cars };
    };
    auto coasters = s.rendering.meshes.capture_coaster_snapshot();
    if (!coasters)
        throw std::runtime_error(describe_mesh_error(coasters.error()));
    ensure(write(output, 1262570821u)
               && write_coaster_save_section(output, *coasters, limits)
               && write(output, 1129267539u)
               && write_advisor_speech_save_section(output, c.advisor.speech),
           "Coaster/Advisor capture failed");
    const auto sound_globals = s.presentation.audio->capture_globals();
    const auto sound_backend = s.presentation.audio->capture_backend();
    const auto sound_present = static_cast<std::uint32_t>(c.resources.resource_manager_dispatch_initialized != 0);
    ensure(write(output, 1094997587u)
               && write(output, sound_present)
               && write(output, c.resources.resource_manager_feature_flag_0400)
               && write(output, static_cast<std::uint32_t>(sound_globals.size()))
               && output.write(sound_globals) == static_cast<std::int32_t>(sound_globals.size()),
           "Sound snapshot capture failed");
    if (sound_present)
        ensure(output.write(sound_backend) == static_cast<std::int32_t>(sound_backend.size()),
               "Sound backend capture failed");
    ensure(write(output, 1397708110u)
               && write_cheat_command_state(output, c.debug.cheat_commands)
               && write(output, 1128813651u)
               && write_advisor_scoring_word(output, c.advisor.behavior.descriptor_cursor)
               && write(output, 1094996803u),
           "Park tail capture failed");
    auto header = s.configuration.header;
    header.version = 500;
    const auto result = write_park_save_file(file, header, output.bytes);
    if (!result)
        throw std::runtime_error(std::string{ save_container_failure_name(result.error().reason) }
                                 + " writing "
                                 + file.native_encoded_string());
}
} //namespace simcoaster
