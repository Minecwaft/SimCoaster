#pragma once
#include "world_port_types.hpp"
#include <meta>
#include <type_traits>
#include <utility>

namespace simcoaster {
struct WorldOperation {
    bool lifecycle_hook {};
};

namespace world_operations::builtin {
[[=WorldOperation { true }]] void record_event(WorldRuntime&, ThingHeader*, WorldEvent*);
[[=WorldOperation {}]] int cached_park_value(WorldRuntime&, ParkAnalyserFinanceView*, std::uint32_t max_age);
[[=WorldOperation {}]] void record_visitor_admission(WorldRuntime&, ParkAnalyserFinanceView*, std::int32_t fee);
[[=WorldOperation { true }]] void record_update(WorldRuntime&, RecordUpdate, ThingHeader*);
[[=WorldOperation { true }]] void record_cleanup(WorldRuntime&, RecordCleanup, ThingHeader*);
[[=WorldOperation {}]] void arrivals(WorldRuntime&, ArrivalState*);
[[=WorldOperation {}]] void update_arrival_vehicle(WorldRuntime&, World&, std::int32_t variant);
[[=WorldOperation {}]] void create_arrival_person(WorldRuntime&);
[[=WorldOperation {}]] void staff_pool(WorldRuntime&, World*);
[[=WorldOperation {}]] void set_challenge_restricted_objects_open(WorldRuntime&, bool open);
[[=WorldOperation {}]] bool try_complete_person_park_object_exit(WorldRuntime&, PersonRuntimeView*);
[[=WorldOperation {}]] ObjectControlRecord* find_object_control(WorldRuntime&, std::uint16_t definition_id);
[[=WorldOperation {}]] void enter_world_person_state(WorldRuntime&, PersonRuntimeView*, PersonState);
[[=WorldOperation {}]] ParkObjectRuntimeView* initialize_park_object_instance(WorldRuntime&, ParkObjectRuntimeView*, CellId, std::uint16_t, std::int32_t, std::uint32_t, std::int32_t);
[[=WorldOperation {}]] void set_staff_research_mode(WorldRuntime&, StaffRuntimeRecord*, std::uint8_t mode);

[[=WorldOperation {}]] void mark_shares_panel_refresh(WorldRuntime&);
[[=WorldOperation {}]] std::uint32_t normalize_special_ride_owner_vectors(WorldRuntime&);
[[=WorldOperation {}]] void refresh_active_park_object_meshes(WorldRuntime&);
[[=WorldOperation {}]] void complete_prebuilt_coaster_catalog(WorldRuntime&, ParkObjectRuntimeView&);
[[=WorldOperation {}]] void create_coaster_supports_and_close_runtime_context(WorldRuntime&);
[[=WorldOperation {}]] void clear_park_object_coaster_geometry_peer_links(WorldRuntime&, ParkObjectRuntimeView&);
[[=WorldOperation {}]] void ensure_world_interaction_handler_initialized(WorldRuntime&);
[[=WorldOperation {}]] std::int32_t world_interaction_handler_kind(WorldRuntime&, void*);
[[=WorldOperation {}]] void apply_set_ride_action(WorldRuntime&, const std::uint16_t* definition_id, std::uint32_t secondary);
[[=WorldOperation {}]] void select_object_definition_and_placement_grid(WorldRuntime&, std::uint16_t definition_id);
[[=WorldOperation {}]] void update_park_object_entry_exit_neighbor_flags(WorldRuntime&, ParkObjectRuntimeView&, bool clear);
[[=WorldOperation {}]] void begin_world_staff_pickup(WorldRuntime&, StaffRuntimeRecord&);
[[=WorldOperation {}]] void copy_catalog_object_sign_name(WorldRuntime&, std::uint16_t definition_id, bool component_b, std::span<char16_t> destination);
[[=WorldOperation {}]] void release_resource_pool_handle(WorldRuntime&, std::uint32_t handle);
[[=WorldOperation {}]] std::uint32_t allocate_resource_variant(WorldRuntime&, ResourceProgram, ResourceArguments);
[[=WorldOperation {}]] void restart_resource_variant(WorldRuntime&, std::uint32_t resource_handle, ResourceProgram);
[[=WorldOperation {}]] bool resource_variant_matches(WorldRuntime&, std::uint32_t resource_handle, ResourceProgram);
[[=WorldOperation {}]] void set_resource_variant_delay(WorldRuntime&, std::uint32_t resource_handle, std::uint32_t delay);
[[=WorldOperation {}]] void set_resource_variant_transform(WorldRuntime&, std::uint32_t resource_handle, const WorldVector3&, std::int32_t mode);
[[=WorldOperation {}]] MobileRenderPose get_resource_variant_pose(WorldRuntime&, std::uint32_t resource_handle);
[[=WorldOperation {}]] void bind_world_person_sprite_to_mesh_model_object(WorldRuntime&, std::uint32_t mesh, std::int32_t index, std::uint32_t person);
[[=WorldOperation {}]] bool get_person_sprite_gender_flag(WorldRuntime&, const PersonRuntimeView&);
[[=WorldOperation {}]] void update_mobile_entity_sprite_pose(WorldRuntime&, PersonRuntimeView&, const std::array<float, 3>&, std::uint32_t frame_rotation, MobileSpriteCategory);
[[=WorldOperation {}]] void release_mobile_entity_sprite_and_optional_person_effects(WorldRuntime&, PersonRuntimeView&, bool);
[[=WorldOperation {}]] void set_mobile_entity_thought_bubble(WorldRuntime&, MobileEntityThoughtHistory*, MobileThoughtCode, bool);
[[=WorldOperation {}]] std::int32_t submit_weather_sound(WorldRuntime&, WeatherSound, const std::array<std::int64_t, 3>& position);
[[=WorldOperation {}]] void stop_weather_sound(WorldRuntime&, std::int32_t handle);
[[=WorldOperation {}]] std::int32_t set_weather_sound_volume(WorldRuntime&, std::int32_t handle, std::int32_t volume);
[[=WorldOperation {}]] std::uint32_t play_advisor_speech_response(WorldRuntime&, std::int32_t response_id);
[[=WorldOperation {}]] const MeshRuntimeOpeningView* find_mesh_runtime_opening_state(WorldRuntime&, std::uint32_t mesh);
[[=WorldOperation {}]] RideRuntimeRatings get_mesh_runtime_ride_ratings(WorldRuntime&, std::uint32_t runtime_map_id);
[[=WorldOperation {}]] void destroy_mesh(WorldRuntime&, std::uint32_t mesh_slot);
[[=WorldOperation {}]] void clear_deferred_mesh_animation(WorldRuntime&, std::uint32_t mesh_slot, std::uint32_t control_index);
[[=WorldOperation {}]] MeshAnimationControlQuery get_mesh_animation_control(WorldRuntime&, std::uint32_t mesh_slot, std::uint32_t control_index);
[[=WorldOperation {}]] void set_park_object_mesh_normal_intensity(WorldRuntime&, std::uint32_t mesh_slot, float intensity, std::uint8_t selector, std::int32_t origin_x, std::int32_t origin_y);
[[=WorldOperation {}]] void select_park_object_mesh_animation_variant(WorldRuntime&, std::uint32_t mesh_slot, ParkObjectMeshVariant);
[[=WorldOperation {}]] double sample_terrain_surface_height(WorldRuntime&, float world_x, float world_z);
[[=WorldOperation {}]] void set_mesh_animation_divisor(WorldRuntime&, std::uint32_t mesh, float divisor);
[[=WorldOperation {}]] std::optional<std::array<float, 3>> get_mesh_clone_attachment_position(WorldRuntime&, std::uint32_t mesh, std::int32_t clone);
[[=WorldOperation {}]] std::array<float, 3> get_mesh_clone_attachment_forward(WorldRuntime&, std::uint32_t mesh, std::int32_t clone);
[[=WorldOperation {}]] void release_mesh_model_object_instance(WorldRuntime&, std::uint32_t mesh, std::int32_t index);
[[=WorldOperation {}]] std::optional<RsseCloneDirection> get_mesh_clone_direction(WorldRuntime&, std::uint32_t mesh, std::int32_t clone);
[[=WorldOperation {}]] std::uint32_t create_mesh_instance_and_register_slot(WorldRuntime&, const CellMeshRegistration&);
[[=WorldOperation {}]] std::uint32_t mark_or_find_matching_mesh_instance_for_refresh(WorldRuntime&, const CellMeshRefresh&);
[[=WorldOperation {}]] void bind_mesh_instance_to_rsse_state(WorldRuntime&, std::uint32_t mesh, std::uint32_t script);
[[=WorldOperation {}]] void configure_mesh_instance_deformation_animation(WorldRuntime&, std::uint32_t mesh);
[[=WorldOperation {}]] std::uint32_t load_and_register_mesh_definition_template(WorldRuntime&, const MeshDefinitionTemplateLoad&);
[[=WorldOperation {}]] std::int32_t request_mesh_animation_control_transition(WorldRuntime&, std::uint32_t, MeshAnimationRequest, std::uint32_t, MeshAnimationTransitionFlags, float, std::uint32_t);
[[=WorldOperation {}]] void process_spam_scheduler_day(WorldRuntime&);
[[=WorldOperation {}]] void send_annual_shares_report(WorldRuntime&, std::uint32_t previous_year, int source, int history_count);
[[=WorldOperation {}]] void advance_spam_scheduler_year(WorldRuntime&);
[[=WorldOperation {}]] void evaluate_awards(WorldRuntime&, void* receiver);
[[=WorldOperation {}]] RsseScriptState* find_script(WorldRuntime&, std::uint32_t script_id);
[[=WorldOperation {}]] int script_slot(WorldRuntime&, RsseScriptState*, int index);
[[=WorldOperation {}]] void set_script_slot(WorldRuntime&, RsseScriptState*, int index, int value);
[[=WorldOperation {}]] void destroy_rsse_script_state_by_id(WorldRuntime&, std::uint32_t script_id, RsseScriptCleanupFlags);
[[=WorldOperation {}]] void set_rsse_tour_duration(WorldRuntime&, std::uint32_t tour, std::int32_t duration_ms);
[[=WorldOperation {}]] void set_rsse_tour_speed(WorldRuntime&, std::uint32_t tour, std::int32_t speed);
[[=WorldOperation {}]] void destroy_rsse_tour(WorldRuntime&, std::uint32_t tour);
[[=WorldOperation {}]] void queue_scored_advisor_action(WorldRuntime&, int action_id);
[[=WorldOperation {}]] AdvisorQueueEntry make_advisor_action_candidate(WorldRuntime&, std::int32_t action);
[[=WorldOperation {}]] void load_advisor_behavior_configuration(WorldRuntime&);
[[=WorldOperation {}]] std::int32_t evaluate_advisor_action_score(WorldRuntime&, std::int32_t action);
[[=WorldOperation {}]] bool queue_advisor_action_candidate(WorldRuntime&, AdvisorRecord*, const AdvisorQueueEntry&);
[[=WorldOperation {}]] void cancel_advisor_action_by_id(WorldRuntime&, AdvisorRecord*, std::int32_t);
[[=WorldOperation {}]] void set_weather_icon(WorldRuntime&, WeatherCategory);
[[=WorldOperation {}]] void set_sky_layer_uv_velocity(WorldRuntime&, unsigned layer, const WorldVector3&);
[[=WorldOperation {}]] void load_weather_effect_textures(WorldRuntime&);
[[=WorldOperation {}]] void release_weather_effect_textures(WorldRuntime&);
[[=WorldOperation {}]] void trigger_weather_lightning(WorldRuntime&, const WorldVector3& ground, const WorldVector3& sky, std::int32_t duration);
} //namespace world_operations::builtin

namespace world_operation_detail {
constexpr auto functions = std::define_static_array([] consteval {
    std::vector<std::meta::info> result;
    for (auto function : std::meta::members_of(^^world_operations::builtin, std::meta::access_context::current())) {
        if (!std::meta::is_function(function)) {
            continue;
        }
        const auto annotations = std::meta::annotations_of_with_type(function, ^^WorldOperation);
        if (annotations.size() != 1) {
            throw "domain operation requires one annotation";
        }
        const auto parameters = std::meta::parameters_of(function);
        if (parameters.empty() || std::meta::type_of(parameters.front()) != ^^WorldRuntime&) {
            throw "domain operation requires a WorldRuntime reference";
        }
        for (auto previous : result) {
            if (std::meta::identifier_of(previous) == std::meta::identifier_of(function)) {
                throw "domain operations require unique names";
            }
        }
        result.push_back(function);
    }
    return result;
}());

consteval bool lifecycle_hook(std::meta::info function) {
    return std::meta::extract<WorldOperation>(std::meta::annotations_of_with_type(function, ^^WorldOperation).front()).lifecycle_hook;
}

template <class Signature>
struct Callback;

template <class Result, class... Args>
struct Callback<Result (*)(Args...)> {
    using Pointer = Result (*)(void*, Args...);

    template <auto Method, class Backend>
    static consteval Pointer bind() {
        return [](void* object, Args... args) -> Result {
            return (static_cast<Backend*>(object)->*Method)(std::forward<Args>(args)...);
        };
    }
};
} //namespace world_operation_detail

struct WorldOperationProbes;

//Borrow one backend for the lifecycle hooks. Test-only operation tables are
//immutable and shared by backend type; resolved production calls stay direct.
struct WorldOperationOverrides {
    void* target {};
    const WorldOperationProbes* probes {};
    WorldOperationOverrides() = default;
    consteval {
        std::meta::token_sequence fields, bindings;
        for (auto function : world_operation_detail::functions) {
            if (!world_operation_detail::lifecycle_hook(function)) {
                continue;
            }
            const auto name = std::meta::id(std::meta::identifier_of(function));
            const auto signature = std::meta::add_pointer(std::meta::type_of(function));
            fields += ^^{ typename world_operation_detail::Callback<${signature}>::Pointer ${name} {}; };
            bindings += ^^{
                if constexpr (requires { &Backend::${name}; }) {
                    if constexpr (std::is_member_function_pointer_v<decltype(&Backend::${name})>)
                        ${name} = world_operation_detail::Callback<${signature}>::template bind<&Backend::${name}, Backend>();
                }
            };
        }
        std::meta::queue_injection(^^{
            ${fields} template <class Backend>
                requires(!std::same_as<std::remove_cvref_t<Backend>, WorldOperationOverrides>)
            explicit WorldOperationOverrides(Backend& backend) : target(&backend) {
                if constexpr (requires { backend.world_operation_probes(); })
                    probes = backend.world_operation_probes();
                ${bindings}
            }
        });
    }
};

namespace world_operations {
consteval {
    for (auto function : world_operation_detail::functions) {
        const auto declaration = std::meta::set_name(std::meta::declaration_of(function), std::meta::identifier_of(function));
        std::meta::queue_injection(^^{ ${declaration}; });
    }
}
} //namespace world_operations
} //namespace simcoaster
