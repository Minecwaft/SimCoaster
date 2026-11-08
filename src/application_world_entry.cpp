#include "simcoaster/application_world_entry.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_theme.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
template <class Record>
Record& required(WorldRuntime& c, ThingId id, ThingKind kind) {
    auto* record = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
    if (!record || record->retirement_pending || record->kind != kind) {
        throw std::runtime_error("Prebuilt World has an invalid manager/owner");
    }
    return *reinterpret_cast<Record*>(record);
}

void reset_analyser(WorldRuntime& c, ParkAnalyserState& analyser) {
    //5ACA30 resets counters and ring cursors, not their backing arrays,
    //capacities, short histories, cell caches or metric values.
    analyser.misbehaving_kids = 0;
    analyser.misbehaving_kids_that_got_away = 0;
    analyser.misbehaving_kids_ever = 0;
    analyser.lifetime_visitors = 0;
    analyser.visitors_this_month = 0;
    FileTime time {};
    calculate_world_calendar_filetime(c, &c.simulation.current->calendar, &time);
    const auto now = std::uint64_t { time.low } | (std::uint64_t { time.high } << 32);
    analyser.last_strike = analyser.last_puke = analyser.last_prank = analyser.park_last_opened = analyser.last_object_built = analyser.last_staff_hired = now;
    analyser.most_paid_for_ticket = 0;
    analyser.largest_bus_load = 0;
    analyser.longest_stay = 0;
    analyser.breakdowns_ever = 0;
    analyser.balloons_sold_ever = 0;
    analyser.costumes_sold_ever = 0;
    analyser.umbrella_sales_count = 0;
    const auto reset_ring = [](auto& ring) {
        ring.wrapped = 0;
        ring.current_entry = -1;
        ring.temporary = 0;
    };
    for (auto& ring : analyser.long_histories) {
        reset_ring(ring);
    }
    reset_ring(analyser.medium_history);
    reset_ring(analyser.tiny_history);
    for (auto& ring : analyser.byte_histories) {
        reset_ring(ring);
    }
}
} //namespace

void reset_application_prebuilt_world(ParkRuntime& park) {
    auto& c = park.context();
    if (!c.simulation.current || c.mode.game == GameMode::code_3 || c.ports.assets.selected_resource_source(c) != 1) {
        throw std::invalid_argument("First-prebuilt reset requires the actual ordinary Science World");
    }
    auto& world = *c.simulation.current;
    //Successful7468B0 runs64B940 before State10. Full restore alone is not
    //a new game: saved people, wages and financial statistics must be removed.
    world.saved.game_tick = 0;
    synchronize_world_calendar_month_year(c, &world.calendar);
    for (auto* entry = c.entities.active_head; entry;) {
        auto* next = entry->active_next;
        auto* record = entry->payload;
        if (record && !record->retirement_pending && is_world_used_thing_mobile(record)) {
            retire_world_used_thing(c, record);
        }
        entry = next;
    }
    drain_pending_world_used_thing_removals(c);
    if (world.saved.current_arrival_vehicle) {
        auto& vehicle = required<ParkObjectRuntimeView>(c, world.saved.current_arrival_vehicle, ThingKind::park_object);
        auto* script = world_operations::find_script(c, vehicle.script_id);
        if (!script) {
            throw std::runtime_error("Prebuilt arrival vehicle has no RSSE program");
        }
        world_operations::set_script_slot(c, script, 1, 0);
        world.saved.current_arrival_vehicle = 0;
    }
    reset_bank_starting_cash(c, &required<BankRecord>(c, world.saved.bank_account, ThingKind::bank_account));
    //7177B0 deliberately preserves last_played_world_tick and padding.
    for (auto& history : get_or_create_theme_global_info_manager(c).history) {
        history.last_response_variant_index = -1;
        history.is_active = 0;
        history.cancel_count = history.play_count = 0;
    }
    reset_analyser(c, required<ParkAnalyserState>(c, world.saved.park_analyser, ThingKind::park_analyser));
    //5CFE10 walks the actual catalog and only changes controls with a
    //positive signed live-instance count. Preserve other rows and all costs.
    for (auto* definition = first_object_catalog_entry(&c.resources.object_catalog); definition; definition = next_object_catalog_entry(&c.resources.object_catalog)) {
        if (auto* control = find_and_cache_world_object_control(world.object_controls, definition->id); control && std::bit_cast<std::int32_t>(control->live_instance_count) > 0) {
            control->first_instance_game_tick = 0;
        }
    }

    //State10 consumes B8C004 without clearing its pending flyby latch. These
    //catalogue name repairs precede the first flyby and campaign persistence.
    c.timing.first_hour_time = 0;
    for (auto id = world.saved.first_park_object; id;) {
        auto& object = required<ParkObjectRuntimeView>(c, id, ThingKind::park_object);
        world_operations::copy_catalog_object_sign_name(c, object.definition_id, false, std::span { object.name_a }.first(32));
        world_operations::copy_catalog_object_sign_name(c, object.definition_id, true, std::span { object.name_b }.first(32));
        id = object.next_park_object;
    }
    park.meshes().rebuild_world_sign_textures(c);
}

ApplicationWorldEntryResult apply_application_world_entry(ParkRuntime& park, ApplicationWorldEntry entry, bool post_load_chooser_pending) {
    auto& c = park.context();
    if (entry == ApplicationWorldEntry::none || !c.simulation.current || c.mode.game == GameMode::code_3) {
        return {};
    }
    const auto source = c.ports.assets.selected_resource_source(c);
    if (source == 3) {
        return {}; //The Intro4798 owner has its separate profile1/UI lifecycle.
    }
    if (entry == ApplicationWorldEntry::restored) {
        //Normal restoration clears B8C02C BEFORE either presentation. Even an
        //option-suppressed Tip must not then fall through to a campaign flyby.
        return { post_load_chooser_pending ? ApplicationWorldEntryPresentation::load_chooser : ApplicationWorldEntryPresentation::tip_of_the_day, 0 };
    }
    if (source < 0 || source > 2) {
        return {};
    }
    const auto* calendar_world = c.referenced_world();
    if (!calendar_world) {
        throw std::logic_error("Source flyby requires its actual calendar World");
    }
    const auto elapsed = std::bit_cast<std::int64_t>(scale_world_ticks_to_filetime_delta(calendar_world->calendar, c.simulation.current->saved.game_tick));
    if (elapsed / 864000000000LL >= 1) {
        return {};
    }
    constexpr std::array<std::uint16_t, 3> targets { 3798, 1798, 2798 };
    const auto target = find_first_park_object_record_id_by_definition_id(c, targets[static_cast<std::size_t>(source)]);
    if (!target) {
        return {}; //Native does not invent an initial-mission event here.
    }
    auto& animation = required<AnimationManagerRecord>(c, c.simulation.current->saved.animation_manager, ThingKind::animation_manager);
    //Science's matrix_in_out[14]=500 was a transient CURRENT-pose listener
    //seed before the first camera update. It was not focus, previous raw pose
    //or a flyby offset. The actual attachment owns modern rendering/audio.
    if (!start_world_animation_playback(c, &animation, target, 1, AnimationPlaybackProfile::code_2)) {
        return {};
    }
    //The existing animation owner dispatches the once-only mission event on
    //real profile2 completion, then opens Objectives on its native counter.
    return { ApplicationWorldEntryPresentation::campaign_flyby, target };
}
} //namespace simcoaster
