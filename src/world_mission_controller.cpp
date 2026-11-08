#include "simcoaster/require_bound.hpp"
#include "simcoaster/entity_identity.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include "company_email_internal.hpp"
#include "simcoaster/world_animation.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_save.hpp"
#include "simcoaster/park_construction.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
struct MissionExtentSnapshot final : Stream {
    std::span<const std::byte> bytes;
    std::size_t cursor{};

    explicit MissionExtentSnapshot(std::span<const std::byte> source) : bytes(source) {}

    std::int32_t read(std::span<std::byte> output) override {
        const auto count = std::min(output.size(), bytes.size() - cursor);
        std::ranges::copy(std::span{ bytes }.subspan(cursor, count), output.begin());
        cursor += count;
        return static_cast<std::int32_t>(count);
    }

    std::int32_t write(std::span<const std::byte>) override {
        return -1;
    }
};
} //namespace

namespace {
World& require_world(World* world) {
    return require_bound<std::invalid_argument>(world, "null mission world");
}

ThingHeader* require_payload(MissionWorld& c, ThingId id) {
    auto* payload = c.entities.at(id).payload;
    if (!payload)
        throw std::invalid_argument("null mission payload");
    return payload;
}

AdvisorRecord* current_advisor(MissionWorld& c) {
    return reinterpret_cast<AdvisorRecord*>(require_payload(c, require_world(c.simulation.current).saved.advisor));
}

AnimationManagerRecord* current_animation_manager(MissionWorld& c) {
    return reinterpret_cast<AnimationManagerRecord*>(
        require_payload(c, require_world(c.simulation.current).saved.animation_manager));
}

void queue_action(MissionWorld& c, std::int32_t action) {
    const auto candidate = world_operations::make_advisor_action_candidate(c, action);
    //Scoring may rebind the base World. Resolve its Advisor after construction.
    (void)world_operations::queue_advisor_action_candidate(c, current_advisor(c), candidate);
}

void dispatch(MissionWorld& c, WorldEvent& event) {
    dispatch_world_event_to_subscribers(c,
                                        get_world_event_subscription_registry(c),
                                        &event,
                                        deliver_world_record_event,
                                        nullptr);
}

template <class Construct>
CompanyEmailMessage* allocate_email(MissionWorld& c, Construct construct) {
    auto* raw = static_cast<CompanyEmailMessage*>(
        c.ports.memory.allocate(Allocation::event_payload, sizeof(CompanyEmailMessage)));
    if (!raw)
        return nullptr;
    try {
        return construct(std::construct_at(raw));
    } catch (...) {
        c.ports.memory.release(Allocation::event_payload, raw);
        throw;
    }
}

std::int32_t saved_view_distance(float distance) {
    const double value = distance;
    if (!std::isfinite(value) || value < std::numeric_limits<std::int32_t>::min() || value >= 2147483648.0)
        throw std::domain_error("animation distance outside integer save range");
    return static_cast<std::int32_t>(value);
}

bool advisor_playback_permitted(MissionWorld& c) {
    world_operations::ensure_world_interaction_handler_initialized(c);
    if (auto* handler = c.construction.handler) {
        const auto kind = world_operations::world_interaction_handler_kind(c, handler);
        if (kind != 0
            && kind != 1
            && (c.construction.mode != 1 || (c.construction.path_start_x != -1 && c.construction.path_start_y != -1)))
            return false;
    }
    if (c.ui.command_menu_builder && c.ui.command_menu_builder->visible)
        return false;
    if (c.ui.active_root)
        return false;
    if (c.ui.persistent_menu_category != 0)
        return false;
    if (c.view.flags & 0x16u)
        return false;
    if (c.construction.mode == 2)
        return false;
    if (c.construction.mode == 11)
        return false;
    return !c.construction.coaster_editor_active;
}

void raise_challenge_failures_to_rank_limit(MissionWorld& c, ThemeGlobalInfoManager* manager, std::int32_t rank) {
    manager->current_promotion_index = rank;
    const auto selected = get_or_create_theme_global_info_manager(c).current_promotion_index;
    if (selected < 0 || static_cast<std::size_t>(selected) >= c.staff.firing_line_for_rank_limits.size())
        throw std::out_of_range("promotion rank limit index");
    manager->challenge_failures_remaining =
        std::max(manager->challenge_failures_remaining,
                 c.staff.firing_line_for_rank_limits[static_cast<std::size_t>(selected)]);
}

ParkObjectRuntimeView* live_park_object(MissionWorld& c, ThingId id) {
    return find_live_entity<ParkObjectRuntimeView>(c.entities, id);
}

bool transition_failure(MissionWorld& c, MissionTransitionFailureKind kind, std::string resource, std::string reason) {
    c.missions.mission_transition.prepared.reset();
    c.missions.mission_transition.failure = MissionTransitionFailure{ kind, std::move(resource), std::move(reason) };
    return false;
}

std::expected<MissionResearchBalance, MissionTransitionFailure>
read_next_mission(MissionWorld& c, const ThemeGlobalInfoManager& manager, std::int32_t next) {
    const auto path = mission_balance_asset_path(c, next);
    auto balance = manager.mission_research;
    try {
        const auto text = c.ports.assets.read_balance_asset(c, path);
        if (!text)
            return std::unexpected(MissionTransitionFailure{ MissionTransitionFailureKind::missing_asset,
                                                             path,
                                                             "Mission configuration is missing." });
        //Empty files retain the previous receiver, as in74C050. Parse/reset and
        //partial writes occur in private storage until the whole transition is ready.
        if (!text->empty() && !sam::parse_integer_record(balance, *text))
            return std::unexpected(MissionTransitionFailure{ MissionTransitionFailureKind::malformed_asset,
                                                             path,
                                                             "Mission configuration could not be parsed." });
    } catch (const std::exception& error) {
        return std::unexpected(
            MissionTransitionFailure{ MissionTransitionFailureKind::missing_asset, path, error.what() });
    }
    return balance;
}

void commit_mission_advance(ThemeGlobalInfoManager& manager, std::int32_t next, const MissionResearchBalance& balance) {
    manager.mission_number = next;
    manager.mission_obstacle_research_complete = 0;
    manager.obstacle_removed_message_received = 0;
    for (auto& row : manager.objective_entries) {
        row.state = MissionObjectiveState::inactive;
        row.objective_kind = MissionObjectiveMetric::none;
        row.target_value = 0;
        row.deadline_days = 0;
        row.start_filetime_low = 0;
        row.start_filetime_high = 0;
        row.current_value = 0;
        row.elapsed_days = 0;
        row.source_filter_ordinal = MissionObjectiveSource::all_sources;
    }
    unlock_lobby_area_for_mission(manager, next);
    manager.mission_research = balance;
    project_mission_objective_configuration(manager);
}

bool prepared_target_is_live(MissionWorld& c, const PreparedMissionTransition& prepared) {
    if (prepared.target_invalidated)
        return false;
    if (prepared.target_consumed)
        return true;
    if (!prepared.target)
        return true;
    const auto* object = EntityIdentity { prepared.target, prepared.target_identity, prepared.target_creation_time }.get<ParkObjectRuntimeView>(c.entities);
    return object && object->definition_id == prepared.target_definition;
}
} //namespace

void cancel_prepared_mission_transition(MissionWorld& c) {
    c.missions.mission_transition.prepared.reset();
}

std::optional<MissionTransitionFailure> take_mission_transition_failure(MissionWorld& c) {
    return std::exchange(c.missions.mission_transition.failure, std::nullopt);
}

ParkObjectRuntimeView* resolve_mission_obstacle(MissionWorld& c) {
    if (!c.simulation.current)
        return nullptr;
    const auto type = get_or_create_theme_global_info_manager(c).mission_research.obstacle_type;
    auto* object = live_park_object(c, c.simulation.current->saved.current_obstacle);
    return type && object && object->definition_id == static_cast<std::uint16_t>(type) ? object : nullptr;
}

bool prepare_mission_completion(MissionWorld& c, ThingId target) {
    auto& runtime = c.missions.mission_transition;
    if (runtime.committing)
        return false;
    auto& manager = get_or_create_theme_global_info_manager(c);
    if (auto* prepared = runtime.prepared.get()) {
        if (prepared->world != c.simulation.current
            || prepared->world_epoch != runtime.world_epoch
            || prepared->manager != &manager
            || prepared->mission != manager.mission_number
            || (target && prepared->target && target != prepared->target)
            || !prepared_target_is_live(c, *prepared))
            return transition_failure(c,
                                      MissionTransitionFailureKind::stale_transition,
                                      {},
                                      "The mission or its completion target changed. Try the objective again.");
        if (prepared->unlock_ordinal >= 0
            && !manager.theme_info_records.contains(
                theme_global_source_names[static_cast<std::size_t>(prepared->unlock_ordinal)]))
            return transition_failure(c,
                                      MissionTransitionFailureKind::stale_transition,
                                      {},
                                      "The destination park record changed. Try the objective again.");
        if (target && !prepared->target && !prepared->target_consumed) {
            auto* object = live_park_object(c, target);
            if (!object)
                return transition_failure(c,
                                          MissionTransitionFailureKind::missing_target,
                                          {},
                                          "The mission completion object is no longer available.");
            prepared->target = target;
            prepared->target_identity = &object->base.header;
            prepared->target_definition = object->definition_id;
            prepared->target_creation_time = object->creation_file_time;
        }
        return true;
    }
    auto prepared = std::make_unique<PreparedMissionTransition>();
    prepared->world = c.simulation.current;
    prepared->world_epoch = runtime.world_epoch;
    prepared->manager = &manager;
    prepared->mission = manager.mission_number;
    prepared->directives = manager.mission_research;
    if (target) {
        auto* object = live_park_object(c, target);
        if (!object)
            return transition_failure(c,
                                      MissionTransitionFailureKind::missing_target,
                                      {},
                                      "The mission completion object is no longer available.");
        prepared->target = target;
        prepared->target_identity = &object->base.header;
        prepared->target_definition = object->definition_id;
        prepared->target_creation_time = object->creation_file_time;
    }
    const auto& balance = prepared->directives;
    const auto still_bound = [&] {
        return c.simulation.current == prepared->world
            && runtime.world_epoch == prepared->world_epoch
            && c.missions.theme_global_info_manager == prepared->manager
            && manager.mission_number == prepared->mission;
    };
    const auto stale = [&] {
        return transition_failure(c,
                                  MissionTransitionFailureKind::stale_transition,
                                  {},
                                  "The World changed while preparing mission completion. Try again.");
    };
    if (!manager.end_of_game_event_processed && balance.completion_load_new_hoardings == 1) {
        const auto path = (std::filesystem::path(c.source.data_directory)
                           / "extents"
                           / std::format("extent.{:03}", prepared->mission))
                              .generic_native_encoded_string();
        try {
            auto source = c.ports.assets.open_asset_stream(c, path);
            if (!source)
                return transition_failure(c,
                                          MissionTransitionFailureKind::missing_asset,
                                          path,
                                          "Mission extent map is missing.");
            prepared->extent.resize(map_cell_count);
            for (auto& byte : prepared->extent)
                if (source->read(std::span{ &byte, 1 }) != 1)
                    return transition_failure(c,
                                              MissionTransitionFailureKind::malformed_asset,
                                              path,
                                              "Mission extent map is truncated.");
        } catch (const std::exception& error) {
            return transition_failure(c, MissionTransitionFailureKind::missing_asset, path, error.what());
        }
    }
    if (!still_bound())
        return stale();
    if (!manager.end_of_game_event_processed && balance.completion_theme_opened == 1) {
        const auto ordinal = balance.completion_theme_number;
        if (ordinal < 0 || static_cast<std::size_t>(ordinal) >= theme_global_source_names.size())
            return transition_failure(c,
                                      MissionTransitionFailureKind::malformed_asset,
                                      {},
                                      "Invalid mission park-unlock ordinal.");
        const auto name = theme_global_source_names[static_cast<std::size_t>(ordinal)];
        const auto found = manager.theme_info_records.find(name);
        const auto path = "data/levels/" + std::string{ name } + "/global.sam";
        if (found == manager.theme_info_records.end())
            return transition_failure(c,
                                      MissionTransitionFailureKind::missing_asset,
                                      path,
                                      "The park-unlock record is missing.");
        //Stage even the optional receiver: failed parsing must not publish a
        //partially initialized cache entry or availability byte.
        auto staged = found->second;
        try {
            if (!staged.global_balance_fields) {
                //7509C0's receiver/cache and zero-byte gates, isolated from the live
                //record so both failure causes can be reported without partial writes.
                staged.global_balance_fields.emplace();
                const auto text = c.ports.assets.read_balance_asset(c, path);
                if (!text)
                    return transition_failure(c,
                                              MissionTransitionFailureKind::missing_asset,
                                              path,
                                              "Park configuration is missing.");
                if (!text->empty() && !sam::parse_integer_record(*staged.global_balance_fields, *text))
                    return transition_failure(c,
                                              MissionTransitionFailureKind::malformed_asset,
                                              path,
                                              "Park configuration could not be parsed.");
            }
        } catch (const std::exception& error) {
            return transition_failure(c, MissionTransitionFailureKind::missing_asset, path, error.what());
        }
        prepared->unlock_ordinal = ordinal;
        prepared->unlock_balance = std::move(staged.global_balance_fields);
    }
    if (!still_bound())
        return stale();
    if (!manager.end_of_game_event_processed && balance.completion_end_of_game != 1) {
        const auto next = static_cast<std::int64_t>(prepared->mission) + 1;
        if (next < 0 || next >= 18)
            return transition_failure(c,
                                      MissionTransitionFailureKind::malformed_asset,
                                      {},
                                      "Invalid next mission for the campaign lobby table.");
        prepared->next_mission = static_cast<std::int32_t>(next);
        auto loaded = read_next_mission(c, manager, prepared->next_mission);
        if (!loaded) {
            runtime.failure = std::move(loaded.error());
            return false;
        }
        prepared->next_balance = std::move(*loaded);
    }
    if (c.simulation.current != prepared->world
        || runtime.world_epoch != prepared->world_epoch
        || c.missions.theme_global_info_manager != prepared->manager
        || manager.mission_number != prepared->mission
        || !prepared_target_is_live(c, *prepared))
        return transition_failure(c,
                                  MissionTransitionFailureKind::stale_transition,
                                  {},
                                  "The World changed while preparing mission completion. Try again.");
    runtime.failure.reset();
    runtime.prepared = std::move(prepared);
    return true;
}

CompanyEmailMessage*
construct_objective_complete_company_email(MissionWorld& c, CompanyEmailMessage* email, std::int32_t text_row_index) {
    email->reason = CompanyEmailMessageKind::objective_complete;
    email->context_mode = 1;
    email->counter_slot = 0;
    email->default_resource = 151;
    email->append_signature = 1;
    email->tag_id = 151;
    email->selected_variant = find_tag_system_message_variant_by_id(c, 1);
    email->concrete_class = CompanyEmailClass::objective_complete;
    email->objective_text_row_index = text_row_index;
    //The native ObjectiveComplete constructor does not store tag_entry.
    return email;
}

bool start_world_animation_playback(MissionWorld& c,
                                    AnimationManagerRecord* record,
                                    ThingId target,
                                    std::int32_t script_slot0,
                                    AnimationPlaybackProfile profile) {
    if (c.animation.thing)
        return false;
    c.animation.control.skip_enabled = 0;
    c.animation.control.completion_requested = 0;
    c.animation.control.objectives_open_counter = 0;
    c.ports.ui.center_game_pointer(c);
    c.animation.thing = target;
    auto* object = reinterpret_cast<ParkObjectRuntimeView*>(require_payload(c, target));
    c.ports.camera.restore_world_view_from_special_modes(c);
    if (c.ui.interaction_toolbar) {
        auto* buttons = c.ports.ui.find_ui_control(c, c.ui.interaction_toolbar, 0x40u);
        record->saved_toolbar_button_id = c.ports.ui.get_selected_ui_checkbutton_id(c, buttons);
    }
    c.ports.meshes.prepare_park_object_animation(c, object, 0, 1);
    record->saved_first_person_distance = saved_view_distance(c.view.first_person_view_distance);
    record->saved_fog_start = saved_view_distance(c.view.fog_distance_start);
    record->saved_fog_end = saved_view_distance(c.view.fog_distance_end);
    record->playback_profile = profile;
    const bool first_profile = profile == AnimationPlaybackProfile::code_1;
    c.animation.control.skip_enabled = profile == AnimationPlaybackProfile::code_2;
    c.view.first_person_view_distance = first_profile ? 600.0f : 450.0f;
    c.ports.camera.set_renderer_fog_distance_range(c, first_profile ? 550.0f : 135.0f, first_profile ? 599.0f : 150.0f);
    c.animation.control.saved_speed_percent = c.timing.speed_percent;
    c.timing.speed_percent = 100;
    c.timing.scale = first_profile ? static_cast<double>(0.7f) : 1.0;
    c.ports.ui.disable_command_families_and_set_disabled_flag(c);
    c.ports.ui.close_ui_roots_and_hide_persistent_roots(c);
    c.ports.ui.clear_control_particle_group(c);
    record->playback_window = c.ports.ui.create_world_animation_playback_window(c);
    if (c.animation.control.skip_enabled) {
        c.ports.ui.set_primary_ui_event_callback(c,
                                                 record->playback_window,
                                                 handle_world_animation_playback_window_event);
        c.ports.ui.set_focused_ui_control(c, record->playback_window, 0);
    }
    auto* script = world_operations::find_script(c, object->script_id);
    world_operations::set_script_slot(c, script, 0, script_slot0);
    if (find_first_park_object_record_id_by_definition_id(c, 0x6A7u) == target) {
        auto* volatile controller = c.entities.at(require_world(c.referenced_world()).saved.mission_controller).payload;
        (void)controller;
        (void)query_mission_obstacle_research_status(c);
        queue_action(c, 621);
    }
    return true;
}

void tick_mission_progress_from_controller_record(MissionWorld& c, MissionControllerRecord* controller) {
    if (get_or_create_theme_global_info_manager(c).end_of_game_event_processed)
        return;
    if (controller->research_project_add_attempt_pending) {
        auto* lab = reinterpret_cast<ResearchLabRecord*>(
            require_payload(c, require_world(c.simulation.current).saved.research_lab));
        try_add_research_project_from_selected_source(c, lab);
        controller->research_project_add_attempt_pending = 0;
    }
    if (controller->mission_advisor_action_pending && advisor_playback_permitted(c) && resolve_mission_obstacle(c)) {
        refresh_theme_ability_activation_flags(&get_or_create_theme_global_info_manager(c));
        const auto mission = get_or_create_theme_global_info_manager(c).mission_number;
        std::int32_t action;
        match (mission) {
            case 3 => action = 491;
            case 6 => action = 492;
            case 9 => action = 493;
            case 12 => action = 494;
            case _ => {
                //Native getter result is discarded. A null row is permitted until
                //candidate scoring finishes and the actual queue receiver is read.
                auto* volatile advisor = c.entities.at(require_world(c.simulation.current).saved.advisor).payload;
                (void)advisor;
                action = get_or_create_theme_global_info_manager(c).history[0x19B].is_active ? 370 : 411;
            }
        }
        queue_action(c, action);
        controller->cutscene_state = MissionCutsceneState::obstacle_advisor_playback;
        const auto target = require_world(c.simulation.current).saved.current_obstacle;
        (void)start_world_animation_playback(c,
                                             current_animation_manager(c),
                                             target,
                                             1,
                                             AnimationPlaybackProfile::code_0);
        controller->mission_advisor_action_pending = 0;
    }
    auto& manager = get_or_create_theme_global_info_manager(c);
    if (!manager.mission_world_event_latch && manager.mission_number != 0) {
        const auto row = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(manager.mission_number) - 1u);
        dispatch_heap_payload_event(c, allocate_email(c, [&](CompanyEmailMessage* email) {
                                        return construct_objective_complete_company_email(c, email, row);
                                    }));
        get_or_create_theme_global_info_manager(c).mission_world_event_latch = 1;
    }
    update_mission_objective_progress(c);
}

bool try_spend_theme_golden_tickets(ThemeGlobalInfoManager* manager, std::int32_t amount) {
    if (amount > manager->golden_ticket_balance)
        return false;
    manager->golden_ticket_balance = std::bit_cast<std::int32_t>(
        std::bit_cast<std::uint32_t>(manager->golden_ticket_balance) - std::bit_cast<std::uint32_t>(amount));
    return true;
}

void unlock_lobby_area_for_mission(ThemeGlobalInfoManager& manager, std::int32_t mission) {
    constexpr std::array<std::int32_t, 18> lobby_flags{ -1, 13, 10, 8, 7, 1, 2, 0, -1, 12, 11, -1, 6, 5, 9, -1, 4, 3 };
    if (mission < 0 || static_cast<std::size_t>(mission) >= lobby_flags.size())
        throw std::out_of_range("mission lobby flag mapping index");
    const auto flag = lobby_flags[static_cast<std::size_t>(mission)];
    if (flag >= 0)
        manager.lobby_area_available_flags[static_cast<std::size_t>(flag)] = 1;
}

bool advance_to_next_mission(MissionWorld& c, ThemeGlobalInfoManager* manager) {
    const auto next = static_cast<std::int64_t>(manager->mission_number) + 1;
    if (next < 0 || next >= 18)
        return false;
    auto loaded = read_next_mission(c, *manager, static_cast<std::int32_t>(next));
    if (!loaded) {
        c.missions.mission_transition.failure = std::move(loaded.error());
        return false;
    }
    commit_mission_advance(*manager, static_cast<std::int32_t>(next), *loaded);
    c.missions.mission_transition.failure.reset();
    return true;
}

bool mark_theme_available_by_ordinal(MissionWorld& c, ThemeGlobalInfoManager* manager, std::int32_t ordinal) {
    if (ordinal < 0 || static_cast<std::size_t>(ordinal) >= theme_global_source_names.size())
        throw std::out_of_range("available theme ordinal");
    const auto found = manager->theme_info_records.find(theme_global_source_names[static_cast<std::size_t>(ordinal)]);
    if (found == manager->theme_info_records.end())
        return false;
    auto staged = found->second;
    if (!load_theme_global_balance_data(c, &staged))
        return false;
    found->second.global_balance_fields = std::move(staged.global_balance_fields);
    found->second.is_available = 1;
    return true;
}

int find_world_current_obstacle(MissionWorld& c, World* world) {
    ThingId id = c.entities.active_head ? static_cast<ThingId>(c.entities.active_head->id_storage) : 0;
    world->saved.state = WorldState::code_1;
    auto& balance = get_or_create_theme_global_info_manager(c).mission_research;
    //Approved repair of6476F0: no match must not retain a previous mission ID.
    world->saved.current_obstacle = 0;
    std::size_t traversed = 0;
    while (id) {
        if (traversed++ >= c.entities.size())
            throw std::length_error("cyclic mission active list");
        auto* payload = require_payload(c, id);
        if (payload->kind == ThingKind::park_object && !payload->retirement_pending) {
            const auto* definition =
                get_park_object_definition(c, reinterpret_cast<const ParkObjectRuntimeView*>(payload));
            if (!definition)
                throw std::out_of_range("mission obstacle definition");
            if (definition->id == static_cast<std::uint16_t>(balance.obstacle_type))
                world->saved.current_obstacle = id;
        }
        const auto* next = c.entities.at(payload->id).active_next;
        id = next ? static_cast<ThingId>(next->id_storage) : 0;
    }
    world->saved.state = WorldState::code_0;
    return 1;
}

bool process_mission_completion_directives(MissionWorld& c, MissionControllerRecord*) {
    if (get_or_create_theme_global_info_manager(c).end_of_game_event_processed) {
        cancel_prepared_mission_transition(c);
        return false;
    }
    if (!prepare_mission_completion(c))
        return false;
    auto prepared = std::move(c.missions.mission_transition.prepared);
    auto& manager = *prepared->manager;
    const auto& balance = prepared->directives;
    c.missions.mission_transition.committing = true;

    struct CommitGuard {
        bool& flag;

        ~CommitGuard() {
            flag = false;
        }
    } guard{ c.missions.mission_transition.committing };

    //Dependencies are owned, so none of the fallible asset reads occurs here.
    (void)try_spend_theme_golden_tickets(&manager, balance.obstacle_golden_tickets);
    if (!prepared->extent.empty()) {
        if (!is_world_map_coordinate_in_bounds(c.construction.hoarding_chain_start_x,
                                               c.construction.hoarding_chain_start_y))
            bind_land_boundary_anchors(c);
        MissionExtentSnapshot extent{ prepared->extent };
        if (!apply_hoardings_map_cells_to_world(c, extent))
            throw std::logic_error("Validated mission extent snapshot could not be applied");
    }
    if (prepared->unlock_ordinal >= 0) {
        auto& info = manager.theme_info_records.at(
            std::string{ theme_global_source_names[static_cast<std::size_t>(prepared->unlock_ordinal)] });
        info.global_balance_fields = std::move(prepared->unlock_balance);
        info.is_available = 1;
    }
    if (balance.completion_end_of_game == 1) {
        dispatch_heap_payload_event(c, allocate_email(c, [&](CompanyEmailMessage* email) {
                                        return construct_tag_system_company_email(c, email, 93, 6, 1);
                                    }));
        get_or_create_theme_global_info_manager(c).end_of_game_event_processed = 1;
        get_or_create_theme_global_info_manager(c).end_of_game_event_processed = 1;
    } else {
        commit_mission_advance(manager, prepared->next_mission, *prepared->next_balance);
        find_world_current_obstacle(c, &require_world(c.simulation.current));
        initialize_mission_objective_tracking(c);
        get_or_create_theme_global_info_manager(c).mission_world_event_latch = 0;
        WorldEvent event{ MissionAdvancedEvent{} };
        dispatch(c, event);
    }
    c.ports.meshes.rebuild_terrain_cell_shading(c);
    world_operations::refresh_active_park_object_meshes(c);
    return true;
}

bool start_mission_completion_presentation(MissionWorld& c, MissionControllerRecord* controller) {
    const auto ending = get_or_create_theme_global_info_manager(c).mission_research.ending_theme;
    const auto cutscene = static_cast<std::uint16_t>(
        get_or_create_theme_global_info_manager(c).mission_research.completion_cutscene_object);
    const auto obstacle = static_cast<std::uint16_t>(
        get_or_create_theme_global_info_manager(c).mission_research.completion_obstacle_opened);
    auto& manager = get_or_create_theme_global_info_manager(c);
    if (!manager.selected_theme_info)
        throw std::invalid_argument("null selected mission theme");
    auto* info = get_or_create_theme_global_info_for_ordinal(c, manager, manager.selected_theme_info->source_ordinal);
    if (!info)
        throw std::runtime_error("mission completion theme failed to load");
    if (static_cast<std::int32_t>(info->source_ordinal) != ending && ending != 3)
        return false;
    if (obstacle) {
        if (const auto id = find_first_park_object_record_id_by_definition_id(c, obstacle)) {
            if (!prepare_mission_completion(c, id))
                return false;
            controller->cutscene_object = id;
            controller->cutscene_state = MissionCutsceneState::completion_obstacle_playback;
            (void)start_world_animation_playback(c,
                                                 current_animation_manager(c),
                                                 id,
                                                 2,
                                                 AnimationPlaybackProfile::code_0);
            (void)get_or_create_theme_global_info_manager(c);
            if (!get_or_create_theme_global_info_manager(c).mission_number)
                queue_action(c, 620);
            return true;
        }
    } else if (cutscene) {
        auto* volatile reference_world = c.referenced_world();
        (void)reference_world;
        if (const auto id = find_first_park_object_record_id_by_definition_id(c, cutscene)) {
            if (!prepare_mission_completion(c, id))
                return false;
            controller->cutscene_state = MissionCutsceneState::completion_object_playback;
            //User-authorized repair of native5F08A0's stale target lifetime.
            controller->cutscene_object = id;
            (void)start_world_animation_playback(c,
                                                 current_animation_manager(c),
                                                 id,
                                                 2,
                                                 AnimationPlaybackProfile::code_0);
            return true;
        }
    }
    return process_mission_completion_directives(c, controller);
}

void handle_world_cutscene_event(MissionWorld& c, MissionControllerRecord* controller, WorldEvent* event) {
    (do {
        match (event->type()) {
            case 35 => controller->mission_advisor_action_pending = 1;
            case 36 => {
                if (c.missions.mission_transition.committing)
                    return;
                const auto target = event->get<ObstacleRemovedEvent>().placed_thing;
                if (!target
                    || !resolve_mission_obstacle(c)
                    || target != require_world(c.simulation.current).saved.current_obstacle) {
                    transition_failure(c,
                                       MissionTransitionFailureKind::missing_target,
                                       {},
                                       "The required mission obstacle is not available.");
                    return;
                }
                if (!prepare_mission_completion(c, target))
                    return;
                auto* volatile advisor = c.entities.at(require_world(c.simulation.current).saved.advisor).payload;
                (void)advisor;
                if (get_or_create_theme_global_info_manager(c).history[0x19C].is_active)
                    queue_action(c, 371);
                controller->cutscene_state = MissionCutsceneState::removed_obstacle_playback;
                controller->cutscene_object = target;
                (void)start_world_animation_playback(c,
                                                     current_animation_manager(c),
                                                     target,
                                                     2,
                                                     AnimationPlaybackProfile::code_0);
                mark_obstacle_removed_message_received(&get_or_create_theme_global_info_manager(c));
                if (get_or_create_theme_global_info_manager(c).mission_number == 1)
                    queue_action(c, 541);
            }
            case 37 => {
                const auto rank = event->get<PromotionEvent>().rank;
                auto& manager = get_or_create_theme_global_info_manager(c);
                raise_challenge_failures_to_rank_limit(c, &manager, rank);
                queue_action(c, 420);
            }
            case 41 => {
                if (c.missions.mission_transition.committing
                    || (c.missions.mission_transition.prepared && c.missions.mission_transition.prepared->applying))
                    return;
                const auto state = controller->cutscene_state;
                const auto finish_failed_playback = [&] {
                    controller->cutscene_state = MissionCutsceneState::idle;
                    controller->cutscene_object = 0;
                    if (state == MissionCutsceneState::removed_obstacle_playback)
                        get_or_create_theme_global_info_manager(c).obstacle_removed_message_received = 0;
                    for (auto action : { 370, 411, 483, 527, 528 })
                        world_operations::cancel_advisor_action_by_id(c, current_advisor(c), action);
                };
                if (state == MissionCutsceneState::removed_obstacle_playback
                    || state == MissionCutsceneState::completion_obstacle_playback
                    || state == MissionCutsceneState::completion_object_playback) {
                    if (!controller->cutscene_object || !prepare_mission_completion(c, controller->cutscene_object)) {
                        if (!controller->cutscene_object)
                            transition_failure(c,
                                               MissionTransitionFailureKind::missing_target,
                                               {},
                                               "The mission completion object is no longer available.");
                        //Event36 published this latch before playback. A failed completion
                        //remains retryable and must not masquerade as a removed obstacle.
                        finish_failed_playback();
                        do_return;
                    }
                    auto* object = live_park_object(c, controller->cutscene_object);
                    c.missions.mission_transition.prepared->applying = true;
                    //A repeated event from a synchronous subscriber sees idle rather
                    //than transforming or retiring the same object twice.
                    if (state == MissionCutsceneState::completion_object_playback) {
                        controller->cutscene_state = MissionCutsceneState::idle;
                        controller->cutscene_object = 0;
                        c.missions.mission_transition.prepared->target_consumed = true;
                        //Consume target validation before retirement; dependencies stay owned.
                        retire_world_used_thing(c, &object->base.header);
                    } else {
                        c.ports.camera.focus_world_view_camera_on_park_object_center(c, object);
                        if (controller->cutscene_object != object->base.header.id
                            || !prepare_mission_completion(c, controller->cutscene_object)) {
                            if (!c.missions.mission_transition.failure)
                                transition_failure(c,
                                                   MissionTransitionFailureKind::stale_transition,
                                                   {},
                                                   "The mission completion target changed during camera focus.");
                            finish_failed_playback();
                            do_return;
                        }
                        controller->cutscene_state = MissionCutsceneState::idle;
                        controller->cutscene_object = 0;
                        c.missions.mission_transition.prepared->target_consumed = true;
                        apply_object_definition_after_shape_grid(c, &object->base.header);
                    }
                    process_mission_completion_directives(c, controller);
                }
                controller->cutscene_state = MissionCutsceneState::idle;
                controller->cutscene_object = 0;
                for (auto action : { 370, 411, 483, 527, 528 })
                    world_operations::cancel_advisor_action_by_id(c, current_advisor(c), action);
            }
            case _ => {}
        }
    });
    WorldEvent progress{ MissionObjectiveProgressEvent{} };
    dispatch(c, progress);
    update_objectives_panel_and_queue_mission_advisor_action(c);
}
} //namespace simcoaster
