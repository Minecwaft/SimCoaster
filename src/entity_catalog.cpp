#include "simcoaster/entity_catalog.hpp"
#include "simcoaster/rsse_state.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_analyser.hpp"
#include "simcoaster/world_animation.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_challenge.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_save.hpp"
#include "simcoaster/world_save_fields.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_weather.hpp"
#include <format>
#include <memory>
#include <meta>
#include <utility>

namespace simcoaster {
namespace {
namespace entity_codec {
namespace {
template <class T>
bool prefix(T& payload, SaveArchive archive) {
    const bool base_ok = transfer_map_object_base_state(&payload.base, archive);
    //These helpers intentionally continue after a failed common-base transfer.
    constexpr bool ignores_base = std::same_as<T, MissionControllerRecord> || std::same_as<T, AnimationManagerRecord> || std::same_as<T, ChallengeManagerRecord>;
    return base_ok || ignores_base;
}
} //namespace

int body([[maybe_unused]] WorldRuntime& c, [[maybe_unused]] ResearchLabRecord& payload, SaveArchive archive) {
    auto& lab = payload;
    for (auto& entry : lab.projects) {
        if (archive.record(entry)) {
            return 1;
        }
    }
    for (auto& entry : lab.groups) {
        if (archive.record(entry)) {
            return 1;
        }
    }
    if (!archive.fields(lab)) {
        return 1;
    }
    for (unsigned i = 0; i < 20; ++i) {
        if (!archive.field(lab.primary_staff[i], "pristaf") || !archive.field(lab.secondary_staff[i], "secstaf")) {
            return 1;
        }
    }
    return !archive.field(lab.consultants[0], "mConsultants[0]") || !archive.field(lab.consultants[1], "mConsultants[1]");
}

int body([[maybe_unused]] WorldRuntime& c, [[maybe_unused]] AdvisorRecord& payload, SaveArchive archive) {
    auto& advisor = payload;
    if (!archive.fields<0, 2>(advisor)) {
        return 1;
    }
    for (auto& entry : advisor.queue) {
        if (archive.record(entry)) {
            return 1;
        }
    }
    if (!archive.fields<2, 5>(advisor) || archive.record(advisor.special_candidate) || !archive.fields<5>(advisor)) {
        return 1;
    }
    return 0;
}

int body([[maybe_unused]] WorldRuntime& c, [[maybe_unused]] ChallengeManagerRecord& payload, SaveArchive archive) {
    auto& manager = payload;
    //No second status check: native code proceeds after each failed child.
    for (auto& entry : manager.records) {
        (void)archive.record(entry);
    }
    if (!archive.field(manager.next_challenge_delay_time, "mNextChallengeDelayTime") || !archive.field(manager.system_switched_on, "mSystemSwitchedOn")) {
        return 1;
    }
    for (auto& thing : manager.actual_thing) {
        if (!archive.field(thing, "mActualThing[i]")) {
            return 1;
        }
    }
    return 0;
}

int body([[maybe_unused]] WorldRuntime& c, [[maybe_unused]] StaffHqRecord& payload, SaveArchive archive) {
    auto& hq = payload;
    if (!archive.field(hq.force_strike, "mForceStrike") || !archive.field(hq.staff_member_picked_up, "mStaffMemberPickedUp")) {
        return 1;
    }
    for (auto& strike : hq.strikes) {
        if (!archive.field(strike, "mStrikeLevel[i]")) {
            return 1;
        }
    }
    for (auto& budget : hq.training.budget) {
        if (!archive.field(budget, "mBudget[i]")) {
            return 1;
        }
    }
    if (!archive.field(hq.training.have_ever_trained, "mHaveEverTrained")) {
        return 1;
    }
    //The native format includes the entire ability row, including its two
    //padding bytes, but omits the training and picked-up-ID padding.
    for (auto& slot : hq.staff_slots) {
        if (!archive.field(slot, "mStaffSlots[i]")) {
            return 1;
        }
    }
    for (auto& trained : hq.trained_staff) {
        if (!archive.field(trained, "mTrainedStaff[i]")) {
            return 1;
        }
    }
    return 0;
}

int body([[maybe_unused]] WorldRuntime& c, [[maybe_unused]] BankRecord& payload, SaveArchive archive) {
    auto& bank = payload;
    if (!archive.fields(bank)) {
        return 1;
    }
    for (auto& loan : bank.loans) {
        if (!archive.fields(loan)) {
            return 1;
        }
    }
    return 0;
}

int body([[maybe_unused]] WorldRuntime& c, [[maybe_unused]] WeatherRecord& payload, SaveArchive archive) {
    auto& weather = payload;
    for (auto* entry : { &weather.pending, &weather.active }) {
        if (!archive.fields(*entry)) {
            return 1;
        }
    }
    if (!archive.fields(weather)) {
        return 1;
    }
    return 0;
}

int body([[maybe_unused]] WorldRuntime& c, [[maybe_unused]] MechanicHqRecord& payload, SaveArchive archive) {
    return archive.field(payload.next_object, "mNextObject") ? 0 : 1;
}

int body([[maybe_unused]] WorldRuntime& c, [[maybe_unused]] MissionControllerRecord& payload, SaveArchive archive) {
    auto& mission = payload;
    if (!archive.field(mission.cutscene_object, "mCutsceneObject") || !archive.field(mission.cutscene_state, "mCutsceneState")) {
        return 1;
    }
    if (!archive.writing) {
        mission.mission_advisor_action_pending = 0;
        mission.research_project_add_attempt_pending = 1;
    }
    return 0;
}

int body([[maybe_unused]] WorldRuntime& c, [[maybe_unused]] AnimationManagerRecord& payload, SaveArchive archive) {
    return archive.field(c.animation.thing, "animThing ") ? 0 : 1;
}

int transfer([[maybe_unused]] WorldRuntime& c, [[maybe_unused]] PersonRuntimeView& payload, SaveArchive archive) {
    return transfer_world_person_state(c, payload, archive);
}

int transfer([[maybe_unused]] WorldRuntime& c, [[maybe_unused]] ParkAnalyserState& payload, SaveArchive archive) {
    return transfer_park_analyser_state(payload, archive);
}

int transfer([[maybe_unused]] WorldRuntime& c, [[maybe_unused]] ParkObjectRuntimeView& payload, SaveArchive archive) {
    return transfer_park_thing_state(c, payload, archive);
}

int transfer([[maybe_unused]] WorldRuntime& c, [[maybe_unused]] TagSystemRecord& payload, SaveArchive archive) {
    return transfer_tag_system_state(c, &payload, archive);
}

template <class Payload>
int transfer_staff_record(WorldRuntime& c, Payload& payload, SaveArchive archive) {
    auto& staff = [&]() -> StaffRuntimeRecord& {
        if constexpr (requires { payload.staff; }) {
            return payload.staff;
        } else {
            return payload;
        }
    }();
    if (transfer_world_staff_base_state(c, staff, archive)) {
        return 1;
    }
    if (archive.schema > 1) {
        return 0;
    }
    if constexpr (requires { payload.staff; }) {
        if (!archive.fields(payload)) {
            return 1;
        }
    }
    return !archive.field(staff.next_staff, "mNext");
}

int transfer(WorldRuntime& c, StaffRuntimeRecord& payload, SaveArchive archive) {
    return transfer_staff_record(c, payload, archive);
}

//Every local body has the same prefix/schema gate. The exceptions to base
//failure propagation remain in prefix(); codecs with their own prefixes use
//the explicit overloads above.
template <class Payload>
int transfer(WorldRuntime& c, Payload& payload, SaveArchive archive) {
    if (!prefix(payload, archive)) {
        return 1;
    }
    if (archive.schema > 1) {
        return 0;
    }
    if constexpr (requires { body(c, payload, archive); }) {
        return body(c, payload, archive);
    } else {
        static_assert(std::same_as<Payload, UiMessageReceiverRecord> || std::same_as<Payload, Type23Record>);
        return 0; //Exactly the common-base payload.
    }
}

void prepare([[maybe_unused]] WorldRuntime& c, PersonRuntimeView& payload) {
    initialize_person_for_restore(payload);
}

void prepare([[maybe_unused]] WorldRuntime& c, StaffRuntimeRecord& payload) {
    initialize_staff_for_restore(payload);
}

void prepare([[maybe_unused]] WorldRuntime& c, ParkAnalyserState& payload) {
    initialize_park_analyser_for_restore(payload);
}

void prepare([[maybe_unused]] WorldRuntime& c, ParkObjectRuntimeView& payload) {
    payload.creation_file_time = 0;
    payload.repair_state = 0;
    payload.working_life = 0;
    payload.pv = 0;
    payload.last_survey_reset_file_time = 0;
}

void prepare([[maybe_unused]] WorldRuntime& c, ResearchLabRecord& payload) {
    for (auto& group : payload.groups) {
        initialize_world_research_group_entry(&group);
    }
}

void prepare([[maybe_unused]] WorldRuntime& c, AdvisorRecord& payload) {
    payload.time_signature = 0;
    for (auto& entry : payload.queue) {
        entry.valid = 0;
    }
    payload.special_candidate.valid = 0;
}

void prepare([[maybe_unused]] WorldRuntime& c, ChallengeManagerRecord& payload) {
    for (auto& entry : payload.records) {
        entry.type = ChallengeType::code_0;
        entry.challenge_lost = 0;
        entry.offer_time = 0;
        entry.value = 0;
        entry.started_at_world_time = 0;
        entry.finish_time = 0;
        entry.status = ChallengeStatus::na;
    }
    payload.next_challenge_delay_time = 0;
}

void prepare([[maybe_unused]] WorldRuntime& c, WeatherRecord& payload) {
    for (auto* entry : { &payload.pending, &payload.active }) {
        initialize_world_weather_schedule_entry(entry);
    }
}

void prepare([[maybe_unused]] WorldRuntime& c, StaffHqRecord& payload) {
    initialize_world_staff_training_budget_state(&payload.training);
    payload.strikes.fill({});
}

void prepare([[maybe_unused]] WorldRuntime& c, Type23Record& payload) {
    payload.unknown_020 = 3;
}

void prepare([[maybe_unused]] WorldRuntime& c, AnimationManagerRecord& payload) {
    c.animation.thing = 0;
    payload.playback_window = nullptr;
}

template <class T>
void prepare_record(WorldRuntime& c, ThingHeader* header) {
    auto& payload = *reinterpret_cast<T*>(header);
    if constexpr (requires { payload.staff; }) {
        prepare(c, payload.staff);
    } else if constexpr (requires { prepare(c, payload); }) {
        prepare(c, payload);
    }
}

template <class T>
int transfer_record(WorldRuntime& c, ThingHeader* header, SaveArchive archive) {
    auto& payload = *reinterpret_cast<T*>(header);
    if constexpr (requires { payload.staff; }) {
        return transfer_staff_record(c, payload, archive);
    } else {
        return transfer(c, payload, archive);
    }
}
} //namespace entity_codec

using std::meta::token_sequence;

//Tagged hooks expose operation probes, including unsupported null handlers.
//Direct hooks carry only their typed function.
template <class Operation>
struct LifecycleStep {
    Operation operation {};
    std::meta::info handler {};
    bool intercepted {};
    consteval LifecycleStep() = default;

    consteval LifecycleStep(Operation tag, std::meta::info function) : operation(tag), handler(function), intercepted(true) {}

    consteval LifecycleStep(std::meta::info function) : handler(function) {}

    constexpr explicit operator bool() const {
        return intercepted || handler != std::meta::info {};
    }
};

using UpdateStep = LifecycleStep<RecordUpdate>;
using CleanupStep = LifecycleStep<RecordCleanup>;

enum class UpdateMode {
    always,
    initialize_only,
    offline,
    online
};

struct EventHook {
    std::meta::info handler {};
    bool intercepted {};
    consteval EventHook() = default;

    consteval EventHook(std::meta::info function, bool intercept = true) : handler(function), intercepted(intercept) {}
};

struct EntityEntry {
    ThingKind kind;
    std::meta::info payload;
    std::uint32_t original_size, stream_token;
    std::array<UpdateStep, 2> stages {};
    CleanupStep release {};
    EventHook event {};
    UpdateMode mode = UpdateMode::always;
};

void event_required(ThingHeader* record, WorldEvent* event) {
    throw std::logic_error(std::format("World event handler is required: kind {}, event {}", record ? std::to_underlying(record->kind) : 0, event ? event->type() : -1));
}

void handle_mechanic_hq_event(WorldRuntime& c, MechanicHqRecord&, const WorldEvent& original) {
    const auto* event = &original;
    if (event->type() == 7) {
        const auto target = event->get<ParkObjectBrokenDownEvent>().park_object_id;
        const auto* payload = c.entities.at(target).payload;
        if (!payload) {
            throw std::logic_error("mechanic HQ target is missing");
        }
        const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(payload);
        std::int32_t action = c.simulation.current->saved.first_mechanic ? 66 : 65;
        for (auto id = c.simulation.current->saved.first_mechanic; id;) {
            const auto& m = *reinterpret_cast<const MechanicRuntimeRecord*>(c.entities.at(id).payload);
            if (m.object_to_repair == target) {
                if (object.requested_service) {
                    return;
                }
                action = 67;
                break;
            }
            if (!m.object_to_repair && classify_staff_activity(m.staff) != StaffActivityStatus::resting && classify_staff_activity(m.staff) != StaffActivityStatus::on_strike) {
                return;
            }
            id = m.staff.next_staff;
        }
        auto candidate = world_operations::make_advisor_action_candidate(c, action);
        candidate.thing_id = target;
        candidate.secondary = object.definition_id;
        (void)world_operations::queue_advisor_action_candidate(c, reinterpret_cast<AdvisorRecord*>(c.entities.at(c.simulation.current->saved.advisor).payload), candidate);
    }
}

template <class Payload>
ThingHeader& payload_header(Payload& payload) {
    if constexpr (std::same_as<Payload, MapObjectBase>) {
        return payload.header;
    } else if constexpr (requires { payload.staff; }) {
        return payload.staff.base.header;
    } else {
        return payload.base.header;
    }
}

template <auto Function, class Payload>
decltype(auto) invoke_payload(WorldRuntime& c, Payload& payload) {
    if constexpr (requires { Function(c, payload); }) {
        return Function(c, payload);
    } else if constexpr (requires { Function(c, &payload); }) {
        return Function(c, &payload);
    } else if constexpr (requires { Function(c, payload.staff); }) {
        return Function(c, payload.staff);
    } else {
        return Function(c, &payload_header(payload));
    }
}

template <auto Function, class Payload>
bool invoke_event(WorldRuntime& c, Payload& payload, WorldEvent* event) {
    auto invoke = [&]() -> decltype(auto) {
        if constexpr (requires { Function(c, payload, *event); }) {
            return Function(c, payload, *event);
        } else if constexpr (requires { Function(c, &payload, event); }) {
            return Function(c, &payload, event);
        } else if constexpr (requires { Function(c, payload.staff, *event); }) {
            return Function(c, payload.staff, *event);
        } else if constexpr (requires { Function(payload, *event); }) {
            return Function(payload, *event);
        } else {
            return Function(c, *event);
        }
    };
    if constexpr (std::is_void_v<decltype(invoke())>) {
        invoke();
        return true;
    } else {
        return invoke();
    }
}

void query_retired_record_kind_for_helper_event(WorldRuntime& c, Type23Record*, WorldEvent* event) {
    if (event->type() == 33) {
        const auto id = event->get<RetirementEvent>().retired_id;
        //The native comparisons converge, but the payload kind is actually read.
        const volatile auto kind = c.entities[id].payload->kind;
        (void)kind;
    }
}

//One compile-time catalog owns payload identity and all lifecycle routes.
//Each entry carries typed hooks, explicit persisted IDs and mode gates.
constexpr auto bindings = [] consteval {
    constexpr std::array catalog {
        EntityEntry { ThingKind::person, ^^PersonRuntimeView, 752, 1346720339, { UpdateStep { RecordUpdate::person_tick, ^^update_world_person_record }, UpdateStep { RecordUpdate::person_state, ^^update_person_state_machine } }, { RecordCleanup::person, ^^cleanup_world_person_record }, { ^^handle_person_park_object_event } },
        EntityEntry { ThingKind::park_object, ^^ParkObjectRuntimeView, 328, 1329744453, { UpdateStep { RecordUpdate::park_object_tick, ^^tick_world_park_object }, UpdateStep { RecordUpdate::park_object_state, ^^update_world_park_object_state } }, { RecordCleanup::park_object, ^^cleanup_world_park_object_instance }, { ^^handle_park_object_event } },
        EntityEntry { ThingKind::mechanic, ^^MechanicRuntimeRecord, 1504, 1296384840, { UpdateStep { RecordUpdate::staff_tick, ^^update_world_staff_tick }, UpdateStep { RecordUpdate::mechanic_state, ^^update_world_mechanic_state } }, { RecordCleanup::mechanic, ^^cleanup_world_staff_record }, { ^^handle_world_staff_event } },
        EntityEntry { ThingKind::handyman, ^^HandymanRuntimeRecord, 1504, 1212239428, { UpdateStep { RecordUpdate::handyman_idle, ^^update_world_handyman_idle_state }, UpdateStep { RecordUpdate::handyman_state, ^^update_world_handyman_state } }, { RecordCleanup::handyman, ^^cleanup_world_staff_record }, { ^^handle_world_staff_event } },
        EntityEntry { ThingKind::entertainer, ^^EntertainerRuntimeRecord, 1496, 1162761298, { UpdateStep { RecordUpdate::entertainer_tick, ^^tick_world_entertainer_record }, UpdateStep { RecordUpdate::entertainer_state, ^^update_world_entertainer_state } }, { RecordCleanup::entertainer, ^^cleanup_world_staff_record }, { ^^handle_world_staff_event } },
        EntityEntry { ThingKind::guard, ^^GuardRuntimeRecord, 1504, 1196769618, { UpdateStep { RecordUpdate::guard_tick, ^^relocate_world_guard_record }, UpdateStep { RecordUpdate::guard_state, ^^update_world_guard_state } }, { RecordCleanup::guard, ^^cleanup_world_staff_record }, { ^^handle_world_staff_event } },
        EntityEntry { ThingKind::researcher, ^^ResearcherRuntimeRecord, 1496, 1380275013, { UpdateStep { RecordUpdate::staff_tick, ^^update_world_staff_tick }, UpdateStep { RecordUpdate::researcher_state, ^^update_world_researcher_state } }, { RecordCleanup::researcher, ^^cleanup_world_staff_record }, { ^^handle_world_staff_event } },
        EntityEntry { ThingKind::staff_hq, ^^StaffHqRecord, 428, 1398030662, {}, { ^^unlink_world_used_thing_map_and_event_lists }, { ^^handle_world_staff_control_state_event }, UpdateMode::initialize_only },
        EntityEntry { ThingKind::mechanic_hq, ^^MechanicHqRecord, 24, 1296386129, {}, { ^^unlink_world_used_thing_map_and_event_lists }, { ^^handle_mechanic_hq_event } },
        EntityEntry { ThingKind::advisor, ^^AdvisorRecord, 280, 1094997577, { UpdateStep { RecordUpdate::advisor, ^^update_advisor_action_queue } }, { ^^unlink_world_used_thing_map_and_event_lists }, { ^^receive_world_advisor_message }, UpdateMode::offline },
        EntityEntry { ThingKind::tag_system, ^^TagSystemRecord, 2096, 1413564243, { UpdateStep { ^^update_tag_system_lifecycle } }, { ^^destroy_tag_system }, { ^^handle_world_tag_system_event } },
        EntityEntry { ThingKind::park_analyser, ^^ParkAnalyserState, 141432, 1346458187, {}, { ^^unlink_world_used_thing_map_and_event_lists }, { ^^handle_park_analyser_world_event } },
        EntityEntry { ThingKind::research_lab, ^^ResearchLabRecord, 9480, 1380729154, { UpdateStep { ^^update_world_research_lab_state } }, { ^^unlink_world_used_thing_map_and_event_lists }, { ^^handle_world_research_lab_event, false } },
        EntityEntry { ThingKind::weather, ^^WeatherRecord, 128, 1464156500, { UpdateStep { ^^update_world_weather_state } }, { ^^destroy_world_weather_state }, { ^^handle_world_weather_schedule_event, false } },
        EntityEntry { ThingKind::bank_account, ^^BankRecord, 304, 1111576139, {}, { ^^unlink_world_used_thing_map_and_event_lists }, { ^^handle_world_bank_account_event, false } },
        EntityEntry { ThingKind::ui_receiver, ^^UiMessageReceiverRecord, 20, 1430867283, {}, { ^^unlink_world_used_thing_map_and_event_lists }, { ^^handle_world_ui_message } },
        EntityEntry { ThingKind::chat_presence, ^^ChallengeManagerRecord, 1496, 1128810577, { UpdateStep { RecordUpdate::online_effect, std::meta::info {} }, UpdateStep { RecordUpdate::online_navigation, std::meta::info {} } }, { RecordCleanup::online_base, std::meta::info {} }, {}, UpdateMode::online },
        EntityEntry { ThingKind::challenge_manager, ^^ChallengeManagerRecord, 1496, 1128810577, { UpdateStep { RecordUpdate::challenge, ^^update_challenge_manager } }, {}, { ^^handle_world_challenge_event, false }, UpdateMode::offline },
        EntityEntry { ThingKind::gardener, ^^GardenerRuntimeRecord, 1504, 1195463236, { UpdateStep { RecordUpdate::gardener_idle, ^^update_world_staff_tick }, UpdateStep { RecordUpdate::gardener_state, ^^update_world_gardener_state } }, { RecordCleanup::gardener, ^^cleanup_world_staff_record }, { ^^handle_world_staff_event } },
        EntityEntry { ThingKind::mission_controller, ^^MissionControllerRecord, 28, 1329744456, { UpdateStep { ^^tick_mission_progress_from_controller_record } }, {}, { ^^handle_world_cutscene_event, false }, UpdateMode::offline },
        EntityEntry { ThingKind::animation_manager, ^^AnimationManagerRecord, 48, 1129665619, { UpdateStep { ^^update_world_animation_state } }, {}, {}, UpdateMode::offline },
        EntityEntry { ThingKind::helper_23, ^^Type23Record, 24, 1414680146, {}, { ^^unlink_world_used_thing_map_and_event_lists }, { ^^query_retired_record_kind_for_helper_event, false } },
    };
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        const auto& entry = catalog[i];
        if (!entry.original_size || !entry.stream_token) {
            throw "entity requires explicit stream identity";
        }
        for (std::size_t previous = 0; previous < i; ++previous) {
            if (catalog[previous].kind == entry.kind) {
                throw "duplicate entity kind";
            }
            if (catalog[previous].stream_token == entry.stream_token && (catalog[previous].payload != entry.payload || catalog[previous].original_size != entry.original_size)) {
                throw "incompatible entity stream alias";
            }
        }
    }
    for (auto kind : std::meta::enumerators_of(^^ThingKind)) {
        const auto value = std::meta::extract<ThingKind>(std::meta::constant_of(kind));
        if (value == ThingKind::unknown_0 || value == ThingKind::retired) {
            continue;
        }
        if (std::ranges::none_of(catalog, [&](const auto& entry) {
                return entry.kind == value;
            })) {
            throw "entity kind missing from catalog";
        }
    }
    return catalog;
}();

template <class T>
ThingHeader* construct_payload(void* storage) {
    static_assert(std::is_trivially_destructible_v<T>);
    static_assert(std::is_standard_layout_v<T>);
    static_assert(alignof(T) <= alignof(std::max_align_t));
    static_assert(offsetof(MapObjectBase, header) == 0);
    auto* record = std::construct_at(static_cast<T*>(storage));
    if constexpr (requires { record->staff; }) {
        static_assert(offsetof(T, staff) == 0);
        static_assert(offsetof(StaffRuntimeRecord, base) == 0);
    } else {
        static_assert(offsetof(T, base) == 0);
    }
    return &payload_header(*record);
}

constexpr auto descriptors = [] consteval {
    constexpr auto extent = [] consteval {
        std::size_t result {};
        for (const auto& binding : bindings) {
            result = std::max(result, std::size_t(std::to_underlying(binding.kind)) + 1);
        }
        return result;
    }();
    std::array<RecordDescriptor, extent> result {};
    template for (constexpr auto binding : bindings) {
        using Payload = [:binding.payload:];
        result[std::to_underlying(binding.kind)] = { binding.kind, sizeof(Payload), binding.stream_token, binding.original_size, &construct_payload<Payload>, &typeid(Payload), requires(Payload& p) { p.staff; } };
    }
    return result;
}();

void ensure_mode(WorldRuntime& c, std::uint8_t& initialized) {
    if (!(initialized & 1)) {
        initialized |= 1;
        initialize_game_mode_selector(c);
    }
}
} //namespace

const RecordDescriptor* record_descriptor(ThingKind kind) {
    const auto index = std::to_underlying(kind);
    return index < descriptors.size() && descriptors[index].construct ? &descriptors[index] : nullptr;
}

bool has_builtin_payload_codec(ThingKind kind) {
    return record_descriptor(kind) != nullptr;
}

consteval {
    token_sequence updates, cleanups, events, preparations, codecs, update_cases, event_cases;
    std::vector<UpdateStep> all_stages;
    for (const auto& binding : bindings) {
        const auto kind = binding.kind;
        const auto payload = binding.payload;
        preparations += ^^{
            case ${kind} => { entity_codec::prepare_record<${payload}>(c, record); return; }
        };
        codecs += ^^{
            case ${kind} => { return entity_codec::transfer_record<${payload}>(c, record, archive); }
        };
        token_sequence stages;
        for (const auto& step : binding.stages) {
            if (!step) {
                continue;
            }
            const auto operation = step.operation;
            const auto function = step.handler;
            token_sequence perform = function == std::meta::info {} ? ^^{
                throw std::logic_error(std::format("entity update has not been reconstructed: operation {}, kind {}, ID {}", std::to_underlying(${operation}), std::to_underlying(record->kind), record->id));
            }
                                                                    : ^^{ invoke_payload<(${function})>(c, typed); };
            if (step.intercepted) {
                stages += ^^{
                    if (c.ports.operations.record_update) [[unlikely]]
                        c.ports.operations.record_update(c.ports.operations.target, c, ${operation}, record);
                    else {
                        ${perform}
                    }
                };
                const auto previous = std::ranges::find(all_stages, operation, &UpdateStep::operation);
                if (previous != all_stages.end()) {
                    if (previous->handler != function) {
                        throw "entity update operation has incompatible handlers";
                    }
                } else {
                    all_stages.push_back(step);
                    if (function != std::meta::info {}) {
                        const auto parameters = std::meta::parameters_of(function);
                        const auto type = std::meta::remove_reference(std::meta::type_of(parameters[1]));
                        update_cases += ^^{
                            case ${operation} => { invoke_payload<(${function})>(c, *reinterpret_cast<${type}*>(record)); return; }
                        };
                    }
                }
            } else {
                stages += perform;
            }
        }
        match (binding.mode) {
            case UpdateMode::always => {
            }
            case UpdateMode::initialize_only => {
                if (!stages.empty()) {
                    throw "initialization-only entity must not have update hooks";
                }
                stages = ^^{ ensure_mode(c, c.mode.record_mode_initialized); };
            }
            case UpdateMode::offline or UpdateMode::online => {
                const auto condition = binding.mode == UpdateMode::online ? ^^{ c.mode.game == GameMode::online } : ^^{ c.mode.game != GameMode::online };
                stages = ^^{
                    ensure_mode(c, c.mode.record_mode_initialized);
                    if (${condition}) {
                        ${stages}
                    }
                };
            }
            case _ => {
                throw "invalid entity update mode";
            }
        } updates += ^^{
            case ${kind} => { [[maybe_unused]] auto& typed = *reinterpret_cast<${payload}*>(record); ${stages} return; }
        };
        const auto cleanup = binding.release;
        token_sequence release;
        if (cleanup) {
            const auto operation = cleanup.operation;
            const auto function = cleanup.handler;
            release = function == std::meta::info {} ? ^^{ world_operations::builtin::record_cleanup(c, ${operation}, record); } : ^^{ (void)invoke_payload<(${function})>(c, typed); };
            if (cleanup.intercepted) {
                release = ^^{
                    if (c.ports.operations.record_cleanup) [[unlikely]]
                        c.ports.operations.record_cleanup(c.ports.operations.target, c, ${operation}, record);
                    else {
                        ${release}
                    }
                };
            }
        }
        cleanups += ^^{
            case ${kind} => { [[maybe_unused]] auto& typed = *reinterpret_cast<${payload}*>(record); ${release} return; }
        };
        const auto handler = binding.event.handler;
        token_sequence deliver;
        if (handler != std::meta::info {}) {
            deliver = ^^{
                auto& typed = *reinterpret_cast<${payload}*>(record);
                (void)invoke_event<(${handler})>(c, typed, event);
            };
        }
        if (binding.event.intercepted) {
            if (handler == std::meta::info {}) {
                throw "intercepted entity event requires a handler";
            }
            deliver = ^^{
                if (!event)
                    event_required(record, event);
                auto& typed = *reinterpret_cast<${payload}*>(record);
                if (!invoke_event<(${handler})>(c, typed, event))
                    event_required(record, event);
            };
            event_cases += ^^{ case ${kind} => { ${deliver} return; } };
            deliver = ^^{
                if (c.ports.operations.record_event) [[unlikely]]
                    c.ports.operations.record_event(c.ports.operations.target, c, record, event);
                else {
                    ${deliver}
                }
            };
        }
        events += ^^{ case ${kind} => { ${deliver} return; } };
    }
    std::meta::queue_injection(^^{
        void prepare_builtin_payload(WorldRuntime& c, ThingHeader* record, ThingKind kind) {
            match (kind) {
                ${preparations} case _ => { return; }
            }
        }

        int transfer_builtin_payload_as(WorldRuntime& c, ThingKind kind, ThingHeader* record, SaveArchive archive) {
            match (kind) {
                ${codecs} case _ => {}
            } if (!transfer_map_object_base_state(reinterpret_cast<MapObjectBase*>(record), archive))
                return 1;
            return archive.schema > 1 ? 0 : 1;
        }

        void world_operations::builtin::record_update(WorldRuntime& c, RecordUpdate operation, ThingHeader* record) {
            match (operation) {
                ${update_cases} case _ => {}
            } throw std::logic_error(std::format("entity update has not been reconstructed: operation {}, kind {}, ID {}", std::to_underlying(operation), std::to_underlying(record->kind), record->id));
        }

        void world_operations::builtin::record_event(WorldRuntime& c, ThingHeader* record, WorldEvent* event) {
            if (record)
                match (record->kind) {
                    ${event_cases} case _ => {}
                } event_required(record, event);
        }

        void update_world_used_thing(WorldRuntime& c, ThingHeader* record) {
            match (record->kind) {
                ${updates} case _ => {}
            }
        }

        void cleanup_entity_payload(WorldRuntime& c, ThingHeader* record) {
            match (record->kind) {
                ${cleanups} case _ => {}
            }
        }

        void dispatch_world_used_thing_event(WorldRuntime& c, ThingHeader* record, WorldEvent* event) {
            match (record->kind) {
                ${events} case _ => { return; }
            }
        }
    });
}
} //namespace simcoaster
