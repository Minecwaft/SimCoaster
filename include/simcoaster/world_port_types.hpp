#pragma once

#include "advisor_policy.hpp"
#include "awards_balance.hpp"
#include "calendar_runtime.hpp"
#include "cell_mesh_templates.hpp"
#include "challenge_balance.hpp"
#include "guest_flow_balance.hpp"
#include "later_save.hpp"
#include "mail_language.hpp"
#include "mission_transition.hpp"
#include "park_object_balance.hpp"
#include "person_balance.hpp"
#include "person_environment.hpp"
#include "resource_variant.hpp"
#include "rsse_tour.hpp"
#include "staff_balance.hpp"
#include "standard_balance.hpp"
#include "terrain_grid.hpp"
#include "thing_store.hpp"
#include "world_camera.hpp"
#include "world_event.hpp"
#include "world_staff_interaction.hpp"
#include "world_types.hpp"
#include "world_ui.hpp"
#include <concepts>
#include <mutex>
#include <source_location>
#include <stdexcept>

namespace simcoaster {
struct WorldRuntime;
struct EventRegistry; //Separate event subsystem; not embedded in World.
struct UiControl;     //Owned by the separate UI subsystem.
struct Stream;
struct WorldInteractionHandler;
class WorldMeshRuntime;
class NativeCoasterEditor;
class CoasterCatalogRuntime;
struct TrackRideManager;
struct WeatherEffectsRuntime;
struct AdvisorBalance;

struct TrackRideLayoutMetrics {
    std::int32_t segment_break_count, longest_supported_run, type_b_part_count, unconfigured_cd_part_count,
        visited_entry_count;
};

struct RideRuntimeRatings {
    float sickness, excitement;
};

struct CommandMenuBuilderView {
    std::int32_t visible{};
};
enum class MailCountAnimation {
    slot_2,
    slot_3,
    other
};

enum class Allocation {
    payload,
    removal_node,
    event_registry,
    removal_queue,
    mail_message,
    mail_counter,
    mail_counter_slots,
    pending_mail_free,
    event_tree_node,
    event_object,
    event_payload,
    world,
    park_history,
    staff_area_cells
};
enum class RecordUpdate {
    person_tick,
    person_state,
    park_object_tick,
    park_object_state,
    staff_tick,
    mechanic_state,
    handyman_idle,
    handyman_state,
    entertainer_tick,
    entertainer_state,
    guard_tick,
    guard_state,
    researcher_state,
    advisor,
    tag_system,
    research_lab,
    weather,
    online_effect,
    online_navigation,
    challenge,
    gardener_idle,
    gardener_state,
    mission,
    animation,
};
enum class RecordCleanup {
    person,
    park_object,
    mechanic,
    handyman,
    entertainer,
    guard,
    researcher,
    tag_system,
    weather,
    online_base,
    gardener,
};

//Integration boundary, not a claimed original game class. Every unresolved
//dependency is required: the reconstruction does not silently replace it by a
//successful no-op. Unbound leaves identify the required operation at runtime;
//production owners bind the supported engine paths and tests can override
//them.
[[noreturn]] inline void missing_runtime_service(std::source_location site = std::source_location::current()) {
    throw std::logic_error(std::string{ "Required runtime service: " } + site.function_name());
}
} //namespace simcoaster
