#include "simcoaster/world_object_placement.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_save.hpp"
#include "simcoaster/rsse_state.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_special_rides.hpp"
#include <algorithm>
#include <bit>
#include <memory>
#include <stdexcept>

namespace simcoaster {
namespace {
ResearchCatalogDefinition& require_definition(AttractionWorld& c, std::uint16_t id) {
    auto* definition = find_object_definition_by_id(&c.resources.object_catalog, id);
    if (!definition || !definition->balance)
        throw std::out_of_range("missing park object definition");
    return *definition;
}

template <class T>
T& require_record(AttractionWorld& c, ThingId id) {
    auto* payload = c.entities.at(id).payload;
    if (!payload)
        throw std::invalid_argument("missing park object construction manager");
    return *reinterpret_cast<T*>(payload);
}
} //namespace

std::expected<void, sam::Error>
load_park_object_creation_balance(ParkObjectCreationBalance& balance, std::string_view text, bool preserve) {
    return sam::parse_record<ParkObjectCreationBalance, true>(balance, text, preserve);
}

void begin_park_object_instance(AttractionWorld& c,
                                ParkObjectRuntimeView& object,
                                CellId cell,
                                std::uint16_t definition_id,
                                std::int32_t rotation,
                                std::uint32_t track_ride_handle) {
    if (!c.simulation.current)
        throw std::invalid_argument("park object construction requires a World");
    initialize_world_map_object_base(c, &object.base, cell, static_cast<std::uint32_t>(ThingKind::park_object));
    object.definition_id = definition_id;
    object.angle = static_cast<std::uint32_t>(rotation);
    object.creation_file_time = 0;
    object.mesh_instance_id = 0;
    object.script_id = 0;
    object.track_ride_handle = track_ride_handle;
    object.is_track_ride_valid = 1;
    object.upgrade_parent = 0;
    object.photo_booth = 0;
    object.capabilities = static_cast<ParkObjectCapabilities>(0);
    object.top_left = object.entry_cell = object.exit_cell = object.back_of_queue = 0;
    object.first_in_queue = 0;
    object.queue_size_in_cells = 0;
    object.repair_state = object.working_life = object.pv = 0;
    object.current_upgrade = 0;
    object.upgrade_balloon_sprite = 0;
    object.operating_speed = 0;
    object.operating_duration = object.operating_capacity = 0;
    object.assigned_staff_member = 0;
    object.time_marked_for_maintenance = 0;
    object.requested_service = 0;
    object.can_load = 0;
    object.person_being_loaded = 0;
    object.state = ParkObjectState::normal;
    object.num_walk_aways = 0;
    object.has_been_opened = 0;
    object.has_mesh_darkening_been_applied = 0;
    object.last_survey_reset_file_time = 0;
    object.promoted_attraction = 0;
    object.next_park_object = c.simulation.current->saved.first_park_object;
    c.simulation.current->saved.first_park_object = object.base.header.id;
    ++c.attractions.park_object_record_count;
    object.portion_size = 50;
    object.history = nullptr;
    for (const auto category : { 12, 11, 1, 33 })
        register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &object.base.header, category);
    WorldEvent event{ ParkObjectCreatedEvent{ object.base.header.id } };
    dispatch_world_event_to_subscribers(c,
                                        get_world_event_subscription_registry(c),
                                        &event,
                                        deliver_world_record_event,
                                        nullptr);
}

void initialize_park_object_definition_settings(AttractionWorld& c,
                                                ParkObjectRuntimeView& object,
                                                const ResearchCatalogDefinition& definition,
                                                CellId placement,
                                                std::int32_t rotation) {
    if (!definition.balance)
        throw std::invalid_argument("park object definition has no balance");
    const auto& balance = *definition.balance;
    const auto& info = balance.Info;
    const auto& usage = balance.UsageInfo;
    const auto add_offset = [](CellId origin, CellLocalOffset offset) {
        //The original stores each result in a WORD after adding two's-complement
        //coordinates; unsigned arithmetic also covers negative rotated offsets.
        return static_cast<CellId>(static_cast<std::uint32_t>(origin)
                                   + static_cast<std::uint32_t>(offset.x)
                                   + static_cast<std::uint32_t>(offset.y) * 128u);
    };
    object.booby_trap_owner = 0;
    object.gone_first_person = 0;
    object.top_left = add_offset(
        placement,
        rotate_park_object_local_cell_offset({ definition.cached_map_offset[0], definition.cached_map_offset[1] },
                                             fixed::add(rotation, 180)));
    object.entry_cell = add_offset(object.top_left,
                                   rotate_park_object_local_cell_offset(
                                       { definition.cached_entry_position[0], definition.cached_entry_position[1] },
                                       rotation));
    object.exit_cell = add_offset(
        object.top_left,
        rotate_park_object_local_cell_offset({ definition.cached_exit_position[0], definition.cached_exit_position[1] },
                                             rotation));
    object.repair_state = 100.0f;
    object.pv = 100.0f;
    object.working_life = info.WorkingLife ? static_cast<float>(info.WorkingLife) : 100.0f;
    object.requested_service = 0;
    object.current_upgrade = 0;
    object.upgrade_balloon_sprite = 0;
    object.cost_of_goods = static_cast<std::uint32_t>(usage.InitCostOfGoods);
    object.quality_of_goods = 50;
    object.chance_of_winning = 100u - static_cast<std::uint32_t>(usage.InitChanceOfLoosing);
    object.price_per_use = static_cast<std::uint32_t>(usage.InitPricePerUse);
    object.amount_of_special_ingredient = 50;
    object.running_cost = static_cast<std::uint32_t>(balance.Upgrades[0].RunningCost);
    if (object.running_cost || usage.ChillsYouOut || usage.HoldsLitter) {
        auto* storage = static_cast<ParkThingHistoryState*>(
            c.ports.memory.allocate(Allocation::park_history, sizeof(ParkThingHistoryState)));
        object.history = storage ? initialize_history_ring_buffers(std::construct_at(storage)) : nullptr;
    }
    object.person_being_loaded = 0;
    object.can_load = 1;
    object.activated = 0;
    object.flash_unconnected_exit = 0;
    const auto set = [&](ParkObjectCapabilities flag) {
        object.capabilities =
            static_cast<ParkObjectCapabilities>(std::to_underlying(object.capabilities) | std::to_underlying(flag));
    };
    const auto cell = static_cast<CellId>(((object.base.y >> 8) * 128u) + (object.base.x >> 8) + 1u);
    const auto profile = [&](RegionEffectProfileId id) {
        (void)add_world_re_cell_effect_profile(c.simulation.region_effects, id, cell);
    };
    if (usage.ProvidesRelief) {
        set(ParkObjectCapabilities::provides_relief);
        profile(RegionEffectProfileId::clean_toilet);
    }
    if (usage.RequiresWatering) {
        set(ParkObjectCapabilities::requires_watering);
        profile(RegionEffectProfileId::fresh_plant);
    }
    if (usage.IsEffectorFeature) {
        profile(RegionEffectProfileId::other_feature_effector);
        set(ParkObjectCapabilities::is_effector_feature);
    }
    if (usage.HoldsLitter) {
        set(ParkObjectCapabilities::holds_litter);
        profile(RegionEffectProfileId::clean_bin);
    }
    if (usage.ChillsYouOut)
        set(ParkObjectCapabilities::chills_you_out);
    if (info.IsChoosable)
        set(ParkObjectCapabilities::is_choosable);
    if (info.IsUsable)
        set(ParkObjectCapabilities::is_usable);
    if (usage.RideHandlesSprite)
        set(ParkObjectCapabilities::ride_handles_sprite);
    if (info.HasQueue)
        set(ParkObjectCapabilities::has_queue);
    if (usage.ProvidesSecurity) {
        set(ParkObjectCapabilities::provides_security);
        profile(RegionEffectProfileId::security_camera);
    }
    if (usage.IsFireworks) {
        set(ParkObjectCapabilities::is_fireworks);
        profile(RegionEffectProfileId::fireworks);
    }
    if (info.RunsContinuously)
        set(ParkObjectCapabilities::runs_continuously);
    if (usage.ThinkingSpace)
        set(ParkObjectCapabilities::thinking_space);
    if (usage.TrainingRoom)
        set(ParkObjectCapabilities::training_room);
    const auto usable_flags = std::to_underlying(ParkObjectCapabilities::is_choosable)
                            | std::to_underlying(ParkObjectCapabilities::is_usable);
    transition_park_object_state(c,
                                 object,
                                 (std::to_underlying(object.capabilities) & usable_flags)
                                     ? ParkObjectState::normal
                                     : ParkObjectState::non_usable);
}

void initialize_park_object_script_settings(AttractionWorld& c,
                                            ParkObjectRuntimeView& object,
                                            const ResearchCatalogDefinition& definition) {
    if (!definition.balance)
        throw std::invalid_argument("park object definition has no balance");
    const auto& balance = *definition.balance;
    const auto& usage = balance.UsageInfo;
    const auto& upgrade = balance.Upgrades[0];
    const auto has = [&](ParkObjectCapabilities flag) {
        return (std::to_underlying(object.capabilities) & std::to_underlying(flag)) != 0;
    };
    const auto slot = [&](int index, std::int32_t value) {
        auto* state = world_operations::find_script(c, object.script_id);
        world_operations::set_script_slot(c, state, index, value);
    };
    if (has(ParkObjectCapabilities::holds_litter)) {
        slot(3, c.attractions.park_object_creation_balance.LitterBin.Capacity);
        slot(11, c.attractions.park_object_creation_balance.LitterBin.Dirty);
        slot(12, c.attractions.park_object_creation_balance.LitterBin.FireRisk);
    }
    if (has(ParkObjectCapabilities::requires_watering)) {
        slot(2, c.attractions.park_object_creation_balance.Vegetation.TimeBetweenWaterings);
        slot(5, c.attractions.park_object_creation_balance.Vegetation.TestAnimation);
        slot(14, 1000);
    }
    if (upgrade.InitSpeed > 0) {
        auto* state = world_operations::find_script(c, object.script_id);
        set_rsse_script_state_operating_speed(
            c,
            state,
            std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(upgrade.InitSpeed)));
        object.operating_speed = static_cast<std::uint32_t>(upgrade.InitSpeed);
    }
    if (upgrade.InitCapacity > 0) {
        auto capacity = static_cast<std::uint8_t>(upgrade.InitCapacity);
        if (fixed::add(usage.MinCapacity, usage.MaxCapacity) > 0) {
            if (capacity < usage.MinCapacity)
                capacity = static_cast<std::uint8_t>(usage.MinCapacity);
            else if (capacity > usage.MaxCapacity)
                capacity = static_cast<std::uint8_t>(usage.MaxCapacity);
        }
        slot(2, capacity);
        object.operating_capacity = capacity;
    }
    if (upgrade.InitDuration > 0) {
        auto duration = static_cast<std::uint8_t>(upgrade.InitDuration);
        if (duration < usage.MinDuration)
            duration = static_cast<std::uint8_t>(usage.MinDuration);
        else if (duration > usage.MaxDuration)
            duration = static_cast<std::uint8_t>(usage.MaxDuration);
        slot(3, duration);
        object.operating_duration = duration;
    }
}

namespace {
void record_park_object_creation_for_id(AttractionWorld& c,
                                        ParkObjectRuntimeView& object,
                                        const ResearchCatalogDefinition& definition,
                                        std::uint16_t control_id) {
    if (!c.simulation.current)
        throw std::invalid_argument("park object creation requires a World");
    FileTime time{};
    calculate_world_calendar_filetime(c, &c.simulation.current->calendar, &time);
    object.creation_file_time = std::uint64_t{ time.low } | (std::uint64_t{ time.high } << 32);
    object.last_survey_reset_file_time = object.creation_file_time;
    auto* control = find_and_cache_world_object_control(c.simulation.current->object_controls, control_id);
    if (!control)
        throw std::out_of_range("park object definition has no object control");
    if (control->first_instance_game_tick == -1) {
        control->first_instance_game_tick = std::bit_cast<std::int32_t>(c.simulation.current->saved.game_tick);
        const auto tag = match (definition.which_ui_type) -> int {
            case ObjectUiCategory::rides      => 14;
            case ObjectUiCategory::shops      => 16;
            case ObjectUiCategory::sideshows  => 17;
            case ObjectUiCategory::features   => 18;
            case ObjectUiCategory::facilities => 19;
            case _                            => -1;
        };
        if (tag >= 0) {
            auto* system = c.entities.at_payload<TagSystemRecord>(c.simulation.current->saved.tag_system);
            if (!system)
                throw std::logic_error("park object creation requires a tag system");
            (void)mark_matching_mail_for_deletion(c, system, tag, definition.id);
        }
    }
    ++control->live_instance_count;
}
} //namespace

void record_park_object_creation_in_world(AttractionWorld& c,
                                          ParkObjectRuntimeView& object,
                                          const ResearchCatalogDefinition& definition) {
    record_park_object_creation_for_id(c, object, definition, object.definition_id);
}

void initialize_or_refresh_park_object_mesh(AttractionWorld& c, ParkObjectRuntimeView& object, bool refresh) {
    auto* definition = find_object_definition_by_id(&c.resources.object_catalog, object.definition_id);
    if (!definition)
        return; //Native silently returns for a missing definition.
    if (!definition->balance || !definition->preview_mesh_template_loaded)
        throw std::logic_error("park object preview mesh is not loaded");
    const auto& info = definition->balance->Info;
    CellMeshPlacement placement{ std::bit_cast<std::int32_t>(object.angle), 0, 0, info.Shape.get() };
    const auto definition_index = std::bit_cast<std::int32_t>(definition->preview_mesh_template_id);
    const auto fixed_origin = info.DontApplyOffset != 0;
    if (fixed_origin) {
        placement.tile_x = info.EngineMapOffsetOverrideX ? info.EngineMapOffsetOverrideX
                                                         : c.visitors.environment.MapInfo.FixedItemOriginX;
        placement.tile_y = info.EngineMapOffsetOverrideY ? info.EngineMapOffsetOverrideY
                                                         : c.visitors.environment.MapInfo.FixedItemOriginY;
    } else {
        const auto index = static_cast<std::uint32_t>(object.top_left) - 1u;
        placement.tile_x = static_cast<std::int32_t>(index & 127u);
        placement.tile_y = static_cast<std::int32_t>(index >> 7);
    }
    const auto flags = fixed_origin ? 865u : 815u;
    if (refresh) {
        const auto width =
            fixed_origin ? static_cast<std::uintptr_t>(static_cast<std::uint32_t>(info.EngineFootprintWidthOverride))
                         : 0u;
        const auto height =
            fixed_origin ? static_cast<std::uintptr_t>(static_cast<std::uint32_t>(info.EngineFootprintHeightOverride))
                         : 0u;
        (void)world_operations::mark_or_find_matching_mesh_instance_for_refresh(
            c,
            { object.mesh_instance_id,
              definition_index,
              flags,
              placement,
              0,
              &object,
              reinterpret_cast<const void*>(width),
              reinterpret_cast<const void*>(height) });
        if (!fixed_origin && object.mesh_instance_id)
            world_operations::bind_mesh_instance_to_rsse_state(c, object.mesh_instance_id, object.script_id);
    } else
        object.mesh_instance_id = world_operations::create_mesh_instance_and_register_slot(
            c,
            { definition_index,
              flags,
              placement,
              0,
              object.script_id,
              &object,
              fixed_origin ? info.EngineFootprintWidthOverride : 0,
              fixed_origin ? info.EngineFootprintHeightOverride : 0,
              -1,
              0 });
}

void refresh_type3_meshes_after_restore(AttractionWorld& c, World& world) {
    ThingId id = c.entities.active_head ? static_cast<ThingId>(c.entities.active_head->id_storage) : 0;
    std::size_t traversed = 0;
    while (id) {
        if (traversed++ >= c.entities.size())
            throw std::length_error("cyclic park object active list");
        auto* payload = c.entities.at(id).payload;
        if (!payload)
            throw std::out_of_range("missing active park object payload");
        if (!payload->retirement_pending && payload->kind == ThingKind::park_object)
            initialize_or_refresh_park_object_mesh(c, *reinterpret_cast<ParkObjectRuntimeView*>(payload), true);
        const auto* next = c.entities.at(payload->id).active_next;
        id = next ? static_cast<ThingId>(next->id_storage) : 0;
    }
    find_world_current_obstacle(c, &world);
}

namespace {
void initialize_script_creation_effect(AttractionWorld& c, ParkObjectRuntimeView& object, RsseScriptState* state) {
    if (!state || c.construction.action_recording_disabled || !state->owner_used_thing_id)
        return;
    const auto& definition = require_definition(c, object.definition_id);
    const auto effect = definition.balance->Info.CreateParticleEffect;
    const auto mesh = state->mesh_instance_handle;
    if (!effect || !mesh)
        return;
    const auto bounds = c.ports.meshes.get_ride_mesh_world_bounds(c, mesh);
    if (!bounds)
        return;
    const auto height = c.ports.meshes.get_mesh_root_translation_height(c, mesh);
    const auto x = static_cast<float>((static_cast<double>(bounds->max[0]) + bounds->min[0]) * 0.5);
    const auto z = static_cast<float>((static_cast<double>(bounds->max[2]) + bounds->min[2]) * 0.5);
    const auto particle =
        c.ports.effects.create_particle_effect(c,
                                               static_cast<std::uint32_t>(effect),
                                               { fixed::truncate_integer(static_cast<double>(x) * 1024.0),
                                                 fixed::truncate_integer(static_cast<double>(height) * 1024.0),
                                                 fixed::truncate_integer(static_cast<double>(z) * 1024.0) },
                                               0);
    const auto extents = c.ports.meshes.get_ride_mesh_world_bounds(c, mesh);
    if (!extents)
        return;
    const auto width = static_cast<float>(static_cast<double>(extents->max[0]) - extents->min[0]);
    const auto vertical = static_cast<float>(static_cast<double>(extents->max[1]) - extents->min[1]);
    const auto depth = static_cast<float>(static_cast<double>(extents->max[2]) - extents->min[2]);
    c.ports.effects.set_particle_effect_spread(c,
                                               particle,
                                               { fixed::truncate_integer(static_cast<double>(width) * 512.0),
                                                 fixed::truncate_integer(static_cast<double>(vertical) * 256.0),
                                                 fixed::truncate_integer(static_cast<double>(depth) * 512.0) });
    state->sound_occlusion_region =
        c.ports.audio.create_rsse_sound_occlusion_region(c,
                                                         x,
                                                         height,
                                                         z,
                                                         static_cast<float>((static_cast<double>(width) + depth) * 0.5),
                                                         vertical);
}

void copy_design_name(std::array<char16_t, 33>& destination, const std::array<char16_t, 33>& source) {
    bool ended = false;
    for (std::size_t i = 0; i < 32; ++i) {
        if (!source[i])
            ended = true;
        destination[i] = ended ? 0 : source[i];
    }
}

void finish_park_object_creation(AttractionWorld& c,
                                 ParkObjectRuntimeView& object,
                                 const ResearchCatalogDefinition& definition,
                                 std::uint16_t control_id,
                                 std::int32_t cost_override) {
    auto& world = *c.simulation.current;
    if (!(c.attractions.park_object_mode_initialized & 1u)) {
        c.attractions.park_object_mode_initialized |= 1u;
        initialize_game_mode_selector(c);
    }
    if (c.mode.game == GameMode::code_3 && object.definition_id == c.construction.coaster_design_name_object_id) {
        copy_design_name(object.name_a, c.construction.coaster_design_name_prefix);
        copy_design_name(object.name_b, c.construction.coaster_design_name_suffix);
    } else {
        world_operations::copy_catalog_object_sign_name(c,
                                                        object.definition_id,
                                                        false,
                                                        std::span{ object.name_a }.first(32));
        world_operations::copy_catalog_object_sign_name(c,
                                                        object.definition_id,
                                                        true,
                                                        std::span{ object.name_b }.first(32));
    }
    const auto category = require_definition(c, object.definition_id).which_ui_type;
    const bool closes = category == ObjectUiCategory::rides
                     || (world.saved.park_closed
                         && (category == ObjectUiCategory::shops || category == ObjectUiCategory::sideshows));
    if (closes) {
        object.unknown_324 = 5;
        object.can_load = 0;
        object.person_being_loaded = 0;
        auto* script = world_operations::find_script(c, object.script_id);
        world_operations::set_script_slot(c, script, 6, 1);
        world_operations::select_park_object_mesh_animation_variant(c,
                                                                    object.mesh_instance_id,
                                                                    ParkObjectMeshVariant::closed);
    } else
        object.has_been_opened = 1;
    auto& analyser = require_record<ParkAnalyserState>(c, world.saved.park_analyser);
    FileTime time{};
    calculate_world_calendar_filetime(c, &world.calendar, &time);
    analyser.last_object_built = std::uint64_t{ time.low } | (std::uint64_t{ time.high } << 32);
    auto* control = find_and_cache_world_object_control(world.object_controls, control_id);
    if (!control)
        throw std::out_of_range("missing park object control");
    analyser.long_histories[12].temporary += control->purchase_cost;
    const auto& current = require_definition(c, object.definition_id);
    if (current.balance->Info.IsSpecial == 1) {
        append_special_ride_registry_entry(c, object.base.header.id);
        world_operations::configure_mesh_instance_deformation_animation(c, object.mesh_instance_id);
        c.ports.construction.refresh_world_interaction_origin_state(c, false);
        auto& action_definition = require_definition(c, object.definition_id);
        world_operations::apply_set_ride_action(c, &action_definition.id, 0);
        world_operations::select_object_definition_and_placement_grid(c, object.definition_id);
        world_operations::update_park_object_entry_exit_neighbor_flags(c, object, false);
    } else if (current.balance->Info.IsObstacle != 1) {
        auto& bank = require_record<BankRecord>(c, world.saved.bank_account);
        (void)charge_world_bank_account_expense(c,
                                                &bank,
                                                cost_override ? cost_override
                                                              : definition.balance->Upgrades[0].CostOfUpgrade);
    }
    object.prebuilt_track_open = object.prebuilt_track_edited = object.been_around_at_least_a_month = 0;
    if (!c.simulation.reference)
        throw std::invalid_argument("missing primary World");
    const auto& weather = require_record<WeatherRecord>(c, c.simulation.reference->saved.weather);
    const auto& config = require_definition(c, object.definition_id).balance->WeatherModifier;
    object.closed_due_to_weather = static_cast<std::uint8_t>(match (weather.type) -> bool {
        case WeatherCategory::heatwave => config.ClosedDuringHeatWave == 1;
        case WeatherCategory::rain     => config.ClosedDuringRain == 1;
        case WeatherCategory::storm    => config.ClosedDuringStorm == 1;
        case WeatherCategory::snow     => config.ClosedDuringSnow == 1;
        case _                         => false;
    });
}
} //namespace

ParkObjectRuntimeView* world_operations::builtin::initialize_park_object_instance(WorldRuntime& c,
                                                                                  ParkObjectRuntimeView* object,
                                                                                  CellId cell,
                                                                                  std::uint16_t id,
                                                                                  std::int32_t rotation,
                                                                                  std::uint32_t track_handle,
                                                                                  std::int32_t cost_override) {
    if (!object
        || !c.simulation.current
        || !c.simulation.reference
        || !c.simulation.map_cell_records
        || !cell
        || cell > map_cell_count
        || !c.entities.free_head)
        throw std::invalid_argument("invalid park object construction input");
    bool settings_complete = false, counted = false;
    std::uint32_t created_script = 0;
    std::array<bool, 6> applied_profiles{};
    CellId profile_center = 0;
    ObjectControlRecord* counted_control = nullptr;
    std::int32_t prior_first_instance_tick = 0;
    try {
        begin_park_object_instance(c, *object, cell, id, rotation, track_handle);
        auto& definition = require_definition(c, object->definition_id);
        initialize_park_object_definition_settings(c, *object, definition, cell, rotation);
        settings_complete = true;
        const auto& usage = definition.balance->UsageInfo;
        applied_profiles = { usage.ProvidesRelief != 0, usage.RequiresWatering != 0, usage.IsEffectorFeature != 0,
                             usage.HoldsLitter != 0,    usage.ProvidesSecurity != 0, usage.IsFireworks != 0 };
        profile_center = static_cast<CellId>(((object->base.y >> 8) * 128u) + (object->base.x >> 8) + 1u);
        initialize_or_refresh_park_object_mesh(c, *object, false);
        auto loaded = create_installed_park_object_rsse_state(c, *object);
        if (!loaded)
            throw std::runtime_error("unable to load park object RSSE: " + loaded.error().path);
        auto* script = *loaded;
        created_script = object->script_id;
        object->script_id = 0;
        initialize_script_creation_effect(c, *object, script);
        object->script_id = created_script;
        if (object->mesh_instance_id)
            world_operations::bind_mesh_instance_to_rsse_state(c, object->mesh_instance_id, created_script);
        initialize_park_object_script_settings(c, *object, definition);
        counted_control = find_world_object_control(c.simulation.current->object_controls, id);
        if (counted_control)
            prior_first_instance_tick = counted_control->first_instance_game_tick;
        record_park_object_creation_for_id(c, *object, definition, id);
        counted = true;
        finish_park_object_creation(c, *object, definition, id, cost_override);
        return object;
    } catch (...) {
        //Callers own the allocation and free it on failure. Undo owned links and
        //resources first so their exception path cannot leave dangling IDs behind.
        const auto record_id = object->base.header.id;
        if (record_id && c.entities.at(record_id).payload == &object->base.header) {
            if (counted_control) {
                if (counted)
                    --counted_control->live_instance_count;
                counted_control->first_instance_game_tick = prior_first_instance_tick;
            }
            if (created_script)
                try {
                    world_operations::destroy_rsse_script_state_by_id(c, created_script, RsseScriptCleanupFlags::none);
                } catch (...) {}
            if (object->mesh_instance_id)
                try {
                    world_operations::destroy_mesh(c, object->mesh_instance_id);
                } catch (...) {}
            if (settings_complete) {
                constexpr std::array profiles{
                    RegionEffectProfileId::clean_toilet,           RegionEffectProfileId::fresh_plant,
                    RegionEffectProfileId::other_feature_effector, RegionEffectProfileId::clean_bin,
                    RegionEffectProfileId::security_camera,        RegionEffectProfileId::fireworks
                };
                for (std::size_t i = 0; i < profiles.size(); ++i)
                    if (applied_profiles[i])
                        (void)remove_world_re_cell_effect_profile(c.simulation.region_effects,
                                                                  profiles[i],
                                                                  profile_center);
            }
            if (object->history)
                c.ports.memory.release(Allocation::park_history, object->history);
            if (c.simulation.event_registry)
                (void)remove_world_used_thing_from_all_events(c, c.simulation.event_registry, &object->base.header);
            for (std::size_t i = 0;
                 i < c.attractions.special_ride_group_count && i < c.attractions.special_ride_groups.size();
                 ++i)
                if (c.attractions.special_ride_groups[i].thing_id == record_id) {
                    for (std::size_t j = i + 1;
                         j < c.attractions.special_ride_group_count && j < c.attractions.special_ride_groups.size();
                         ++j)
                        c.attractions.special_ride_groups[j - 1] = c.attractions.special_ride_groups[j];
                    --c.attractions.special_ride_group_count;
                    break;
                }
            //The roster link is established before any event subscriber is called.
            unlink_park_object_from_world_list(c, *c.simulation.current, *object);
            --c.attractions.park_object_record_count;
            (void)unlink_world_used_thing_from_map_cell(
                c,
                &c.simulation.map_cell_records[static_cast<std::size_t>(cell) - 1],
                record_id);
            auto& entry = c.entities[record_id];
            if (entry.active_next)
                entry.active_next->active_previous = entry.active_previous;
            if (entry.active_previous)
                entry.active_previous->active_next = entry.active_next;
            else
                c.entities.active_head = entry.active_next;
            entry.active_next = entry.active_previous = nullptr;
            entry.payload = nullptr;
            entry.free_next = c.entities.free_head;
            c.entities.free_head = &entry;
        }
        throw;
    }
}
} //namespace simcoaster
