#include "park_runtime_codec.hpp"
#include "park_runtime_state.hpp"

namespace simcoaster {
using namespace park_detail;

void ParkRuntime::State::open(ParkRuntime& park, const std::filesystem::path& root, ParkRuntimeOpen& request) {
    auto& s = *this;
    const auto& theme = request.theme;
    const auto file = request.saved_park.value_or(std::filesystem::path{});
    if (request.saved_park) {
        auto preferences = load_ui_preferences(std::filesystem::absolute(root), std::filesystem::absolute(file));
        if (!preferences)
            throw std::runtime_error("UI profile "
                                     + preferences.error().path.native_encoded_string()
                                     + " at byte "
                                     + std::to_string(preferences.error().offset)
                                     + ": "
                                     + preferences.error().reason);
        s.configuration.preferences = std::move(*preferences);
    } else {
        auto defaults = load_native_ui_preferences(std::filesystem::absolute(root));
        if (!defaults)
            throw std::runtime_error(defaults.error().reason);
        s.configuration.preferences.options = *defaults;
    }
    if (request.campaign)
        s.configuration.preferences.options = request.campaign->options;
    s.initialize_assets(std::filesystem::absolute(root), theme);
    if (request.coaster_source)
        install_coaster_source_selection(*s.simulation.world, *request.coaster_source);
    if (request.campaign)
        apply_campaign_profile(*s.simulation.world, *request.campaign);
    //6A3150 -> 648070 constructs the source managers BEFORE either746660's
    //profile save or7468B0's first Science prebuilt is loaded. In particular,
    //648633 -> 5EEDD0 -> 5EEF60 starts inactive objectives using the source's
    //fresh calendar/bank/analyser, while preserving already tracked rows.
    //Skipping these constructors on restore left the first40-visitor objective
    //inactive forever. Use the production constructors and normal pre-load
    //retirement rather than fabricating objective baselines from saved values.
    initialize_world_runtime_state(*s.simulation.world,
                                   *s.simulation.world->simulation.current,
                                   s.configuration.source_startup);
    if (request.saved_park) {
        auto loaded = load_park_save_file(file, {});
        if (!loaded)
            throw std::runtime_error(std::string{ save_container_failure_name(loaded.error().reason) });
        auto prefix = read_park_save_body_prefix(loaded->sections);
        if (!prefix)
            throw std::runtime_error("Invalid action recording/World boundary");
        clear_world_records_before_save_restore(*s.simulation.world, s.simulation.world->simulation.current);
        s.configuration.header = loaded->header;
        const auto recording =
            s.tools.layout_replay.restore(*s.simulation.world, prefix->loaded_published_park, prefix->action_recording);
        if (!recording)
            throw std::runtime_error(describe_layout_error(recording.error()));
        MemoryByteStream input;
        input.bytes.assign(prefix->world_and_later_sections.begin(), prefix->world_and_later_sections.end());
        const auto failed = [&](std::string_view stage) {
            auto message = std::string{ stage }
                         + " ["
                         + std::filesystem::absolute(file).native_encoded_string()
                         + "] at section byte "
                         + std::to_string(input.cursor)
                         + ", last World field="
                         + s.bindings.last_field;
            if (s.rendering.meshes.last_error)
                message += "; " + describe_mesh_error(*s.rendering.meshes.last_error);
            if (const auto& failure = s.rendering.meshes.last_mesh_restore_failure; failure)
                message += "; mesh "
                         + std::string{ failure->stage }
                         + " at body byte "
                         + std::to_string(failure->body_offset)
                         + ", ordinal="
                         + std::to_string(failure->ordinal)
                         + ", slot="
                         + std::to_string(failure->slot)
                         + ", key="
                         + std::to_string(failure->definition_key);
            for (std::size_t i = s.rendering.meshes.slot_limit(); i > 1; --i)
                if (const auto* last = s.rendering.meshes.instance(static_cast<std::uint32_t>(i - 1))) {
                    message += "; last mesh slot="
                             + std::to_string(i - 1)
                             + " key="
                             + std::to_string(last->metadata.definition_key);
                    break;
                }
            return message;
        };
        if (!restore_world_through_camera(*s.simulation.world,
                                          *s.simulation.world->simulation.current,
                                          input,
                                          s.bindings,
                                          *s.simulation.particles,
                                          s.simulation.clocks,
                                          s.simulation.rides,
                                          s.rendering.meshes))
            throw std::runtime_error(failed("World-through-camera restore failed"));
        const CoasterLimitsResolver limits = [&](std::uint16_t slot) -> std::optional<CoasterTrainLimits> {
            const auto* instance = s.rendering.meshes.instance(slot);
            if (!instance)
                return std::nullopt;
            const auto* definition = s.rendering.meshes.definition(instance->template_id);
            if (!definition || !definition->coaster)
                return std::nullopt;
            const auto& trains = definition->coaster->trains;
            return CoasterTrainLimits{ trains.maximum_trains, trains.maximum_cars };
        };
        CoasterSaveSection coasters;
        if (!marker(input, 1262570821u) || !read_coaster_save_section(input, coasters, limits))
            throw std::runtime_error(failed("Coaster snapshot restore failed"));
        if (!marker(input, 1129267539u)
            || !restore_advisor_speech_save_section(input, s.simulation.world->advisor.speech))
            throw std::runtime_error(failed("Advisor restore failed"));
        if (!marker(input, 1094997587u) || !restore_sound_snapshot(input, s.simulation.sound))
            throw std::runtime_error(failed("Sound state restore failed"));
        s.presentation.audio->restore_globals(s.simulation.sound.global_state);
        if (s.simulation.sound.backend_present) {
            s.simulation.world->resources.resource_manager_feature_flag_0400 = s.simulation.sound.feature_flags;
            s.presentation.audio->restore_backend(s.simulation.sound.backend_state);
        }
        if (!marker(input, 1397708110u)
            || !restore_cheat_command_state(input, s.simulation.world->debug.cheat_commands))
            throw std::runtime_error(failed("Cheat restore failed"));
        if (!marker(input, 1128813651u)
            || !restore_advisor_scoring_word(input, s.simulation.world->advisor.scoring_saved_word)
            || !marker(input, 1094996803u))
            throw std::runtime_error(failed("Advisor scoring/UI delimiter restore failed"));
        s.simulation.world->advisor.behavior.descriptor_cursor = s.simulation.world->advisor.scoring_saved_word;
        if (input.cursor != input.bytes.size())
            throw std::runtime_error(failed("Unexpected trailing park data"));
        const auto coaster_bindings =
            s.rendering.meshes.install_coaster_snapshot(*s.simulation.world, std::move(coasters));
        if (!coaster_bindings)
            throw std::runtime_error(describe_mesh_error(coaster_bindings.error()));
        const auto coaster_geometry = restore_coaster_geometry(*s.simulation.world, s.rendering.meshes);
        if (!coaster_geometry)
            throw std::runtime_error(describe_mesh_error(coaster_geometry.error()));
        const auto train_poses = install_restored_coaster_train_poses(*s.simulation.world, s.rendering.meshes);
        if (!train_poses)
            throw std::runtime_error(describe_mesh_error(train_poses.error()));
        bind_restored_world_managers(*s.simulation.world, *s.simulation.world->simulation.current);
        refresh_rsse_tour_mesh_instances(*s.simulation.world);
        refresh_type3_meshes_after_restore(*s.simulation.world, *s.simulation.world->simulation.current);
        if (std::ranges::any_of(s.simulation.world->simulation.current->track_cells, [](const auto& cell) {
                return cell.base.common.type == 25;
            }))
            bind_land_boundary_anchors(*s.simulation.world);
        refresh_world_cell_meshes_after_restore(*s.simulation.world, *s.simulation.world->simulation.current);
        sync_land_boundary_meshes(*s.simulation.world);
        s.rendering.meshes.rebuild_world_sign_textures(*s.simulation.world);
        restore_pending_rsse_occlusion_regions(*s.simulation.world);
        populate_missing_world_object_controls(*s.simulation.world,
                                               s.simulation.world->simulation.current->object_controls);
        s.frame.statistics.restored_bytes = loaded->sections.size();
    } else {
        //648070 resets map storage. Apply the source status map afterward,
        //before672EB0's typed map/track reset and default source population.
        const auto base = s.bindings.read_asset(
            (std::filesystem::absolute(root) / "Data/levels" / theme / "terrain/Base.map").native_encoded_string());
        ensure(base && load_world_base_map(*s.simulation.world, *s.simulation.world->simulation.current, *base),
               "Fresh source Base.map initialization failed");
        initialize_world_source_interaction(*s.simulation.world,
                                            *s.simulation.world->simulation.current,
                                            s.tools.construction);
        populate_world_from_object_definition_catalog(*s.simulation.world,
                                                      *s.simulation.world->simulation.current,
                                                      s.tools.construction);
        (void)find_world_current_obstacle(*s.simulation.world, s.simulation.world->simulation.current);
        s.rendering.meshes.rebuild_world_sign_textures(*s.simulation.world);
        refresh_rsse_tour_mesh_instances(*s.simulation.world);
    }
    load_world_source_sam_storage(*s.simulation.world, std::filesystem::absolute(root));
    s.simulation.world->mode.scheduler = 10;
    //43F370 -> 43F900 marks every full compressed-save restore for a timing
    //reset. State10 (6A43A0) consumes that flag before its first frame: the
    //low-level timing codec's saved deltas are not live scheduling deadlines.
    //The original Arabian save, for example, carries a +4736ms delta which
    //would otherwise suppress newly hired actors' resource scripts for seconds.
    const auto restored_time = s.rendering.meshes.clocks.simulation.count();
    s.frame.scheduler.script_step_cursor = restored_time;
    s.frame.scheduler.auxiliary_step_cursor = restored_time;
    s.frame.scheduler.world_step_cursor = restored_time;
    s.frame.scheduler.script_substeps = 0;
    s.simulation.clocks.simulation_frame_time_ms = static_cast<std::uint32_t>(restored_time);
    s.simulation.clocks.simulation_step_time_ms = s.simulation.clocks.simulation_frame_time_ms;
    s.simulation.clocks.auxiliary_step_time_ms = s.simulation.clocks.simulation_frame_time_ms;
    s.simulation.clocks.world_tick_boundary_ms = s.simulation.clocks.simulation_frame_time_ms;
    s.simulation.clocks.simulation_substep_count = 0;
    s.simulation.clocks.scheduler_saved_reset_word = 0;
    s.presentation.audio->finish_restore(s.simulation.sound.backend_present != 0);
    //43F6DF ->43F9E5: ordinary post-restore finalization interrupts the saved
    //response after its audio targets are restored. Keeping its active latch
    //would reject the next response, despite having no live restored UI mesh.
    if (request.saved_park)
        stop_active_advisor_response(*s.simulation.world, AdvisorStopTargetAction::parameter_request);
    s.presentation.scape = std::make_unique<ScapeOmpRuntime>(
        *s.simulation.world,
        s.presentation.audio->runtime(),
        static_cast<std::uint32_t>(s.bindings.selected_resource_source(*s.simulation.world)));
    if (request.saved_park) {
        const auto imported = request.campaign
                                ? std::span<const CampaignAmbientRecord>{ request.campaign->auxiliary.ambient }
                                : std::span<const CampaignAmbientRecord>{};
        s.presentation.scape->adopt_saved_ambient(imported);
    } else
        s.presentation.scape->load(std::filesystem::absolute(root) / "Data/levels" / theme / "Scape.omp");
    (void)s.presentation.audio->runtime().enable_output();
    reconstruct_coaster_guest_queues(*s.simulation.world);
    s.tools.construction.reset_after_restore();
    s.tools.coaster_editor.reset_after_restore();
    s.tools.coaster_catalog.clear(*s.simulation.world);
    if (!s.simulation.world->scripts.tours.initialized)
        update_rsse_tour_bounds(*s.simulation.world);
    s.simulation.world->ui.panels.calendar_header.present = true;
    {
        FileTime time{};
        std::uint32_t year{}, month{}, day{}, weekday{}, hour{}, minute{}, second{};
        calculate_world_calendar_filetime(*s.simulation.world,
                                          &s.simulation.world->simulation.current->calendar,
                                          &time,
                                          &year,
                                          &month,
                                          &day,
                                          &weekday,
                                          &hour,
                                          &minute,
                                          &second);
        auto& report = s.simulation.world->finance.calendar_report_fields;
        report.year_since_1900 = std::bit_cast<std::int32_t>(year - 1900u);
        report.zero_based_month = std::bit_cast<std::int32_t>(month - 1u);
        report.day_of_month = static_cast<int>(day);
        report.day_of_week = static_cast<int>(weekday);
        report.hour = static_cast<int>(hour);
        report.minute = static_cast<int>(minute);
        report.second = static_cast<int>(second);
        (*s.simulation.world).ports.ui.refresh_calendar_date_label(*s.simulation.world, report);
    }
    s.presentation.authoring = std::make_unique<ParkAuthoringController>(park);
    s.presentation.ui = std::make_unique<WorldUiRuntime>(
        *s.simulation.world,
        root,
        [owner = &s] {
            owner->presentation.authoring->reset_for_ui();
        },
        &s.tools.construction);
    s.presentation.ui->bind_gameplay_pause([owner = &s] {
        return owner->frame.frame_external_paused
            || owner->frame.application_ui_paused
            || owner->presentation.ui->simulation_paused();
    });
    s.presentation.ui->bind_construction_actions(
        [owner = s.presentation.authoring.get()](std::uint16_t definition) {
            return owner->choose_build_object(definition);
        },
        [owner = s.presentation.authoring.get()](const auto& path, std::int32_t quoted_cost) {
            return owner->choose_build_design(path, quoted_cost);
        },
        [owner = s.presentation.authoring.get()](ThingId id) {
            owner->retire(id);
        });
    s.presentation.ui->bind_toolbar_actions([owner = s.presentation.authoring.get()](std::uint32_t id, bool selected) {
        return owner->select_toolbar_tool(id, selected);
    });
    s.presentation.ui->bind_object_actions(
        [owner = s.presentation.authoring.get()](ThingId id) {
            return owner->edit_object_queue(id);
        },
        [owner = s.presentation.authoring.get()](ThingId id) {
            return owner->edit_object_track(id);
        },
        [owner = s.presentation.authoring.get()](ThingId id) {
            owner->ride_object(id);
        },
        [owner = s.presentation.authoring.get()](ThingId id) {
            return owner->relocate_object(id);
        },
        [owner = s.presentation.authoring.get()](ThingId id, CoasterEditorEntryTool tool) {
            return owner->edit_coaster_tool(id, tool);
        });
    s.bindings.ui = s.presentation.ui.get();
    //A saved particle section owns its live graph. Reapply current creation
    //policy after import without resetting/reallocating that restored graph.
    s.apply_hardware_profile(s.configuration.hardware_profile);
    park.apply_preferences(s.configuration.preferences.options);
    if (request.coaster_source)
        park.stage_coaster_source(*request.coaster_source);
    if (const auto* coasters = s.rendering.meshes.coasters()) {
        s.frame.statistics.coaster_records = coasters->bindings.size();
        for (const auto& binding : coasters->bindings) {
            s.frame.statistics.train_slots += binding.train_state.trains.size();
            for (const auto& train : binding.train_state.trains) {
                s.frame.statistics.car_slots += train.cars.size();
                for (const auto& car : train.cars)
                    s.frame.statistics.passenger_references += car.persons_a.size() + car.persons_b.size();
            }
        }
    }
    for (auto* thing = s.simulation.world->entities.active_head; thing; thing = thing->active_next)
        ++s.frame.statistics.things;
    for (auto* script = s.simulation.world->scripts.states.head; script; script = script->next)
        ++s.frame.statistics.scripts;
    for (const auto& slot : s.simulation.world->simulation.event_registry->slots)
        s.frame.statistics.event_subscriptions += slot.subscribers.size();
    for (std::size_t i = 1; i < s.rendering.meshes.slot_limit(); ++i)
        if (s.rendering.meshes.instance(static_cast<std::uint32_t>(i)))
            ++s.frame.statistics.mesh_instances;
}
} //namespace simcoaster
