#pragma once
#include "world_operations.hpp"
#include "world_port_types.hpp"

namespace simcoaster {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

struct AllocationPort {
    virtual ~AllocationPort() = default;
    virtual void* allocate(Allocation, std::size_t) = 0;
    virtual void release(Allocation, void*) = 0;
};

struct EntityEnginePort {
    virtual ~EntityEnginePort() = default;

    virtual TrackRideLayoutMetrics collect_track_ride_layout_metrics(WorldRuntime&, std::uint32_t ride_handle);

    virtual MobileEntityState& online_presence_mobile_state(WorldRuntime&, ThingHeader*);

    virtual void replay_tick(WorldRuntime&) {
        missing_runtime_service();
    }

    virtual void update_track_ride_interpolated_values(WorldRuntime&, std::uint32_t ride, std::int32_t speed) {
        missing_runtime_service();
    }
};

struct WorldUiHostPort {
    virtual ~WorldUiHostPort() = default;

    virtual void refresh_calendar_date_label(WorldRuntime&, CalendarReportFields) {}

    virtual void refresh_park_statistics_month(WorldRuntime&) {}

    virtual void refresh_financial_statistics_month(WorldRuntime&) {}

    virtual void refresh_outstanding_loans_month(WorldRuntime&) {}

    virtual void restore_persistent_roots_after_transition(WorldRuntime&) {
        missing_runtime_service();
    }

    virtual void start_mail_count_animation(WorldRuntime&, UiControl*, MailCountAnimation, int interval_ms) {
        missing_runtime_service();
    }

    virtual void set_selected_ui_checkbutton_id(WorldRuntime&, UiControl*, int) {
        missing_runtime_service();
    }

    virtual void dispatch_ui_control_event(WorldRuntime&, UiControl*, int event, int id, int value) {
        missing_runtime_service();
    }

    virtual void finish_intro_sequence(WorldRuntime&, std::uint8_t force_profile) {
        missing_runtime_service();
    }

    virtual void request_resource_source_change_overlay(WorldRuntime&, int source, std::uint8_t force) {
        missing_runtime_service();
    }

    virtual void open_objectives_panel(WorldRuntime&) {
        missing_runtime_service();
    }

    virtual std::intptr_t invoke_secondary_ui_event_callback(WorldRuntime&, UiControl*, int event, std::uint32_t id, std::intptr_t value) {
        missing_runtime_service();
    }

    virtual UiControl* find_ui_control(WorldRuntime&, UiControl* parent, std::uint32_t id) {
        missing_runtime_service();
    }

    virtual void send_ui_count_value(WorldRuntime&, UiControl*, std::int32_t value) {
        missing_runtime_service();
    }

    virtual void set_ui_visibility(WorldRuntime&, UiControl*, std::int16_t visible) {
        missing_runtime_service();
    }

    virtual void center_game_pointer(WorldRuntime&) {
        missing_runtime_service();
    }

    virtual int get_selected_ui_checkbutton_id(WorldRuntime&, UiControl*) {
        missing_runtime_service();
    }

    virtual void disable_command_families_and_set_disabled_flag(WorldRuntime&) {
        missing_runtime_service();
    }

    virtual void restore_command_families_state(WorldRuntime&, bool disabled) {
        (void)disabled;
        missing_runtime_service();
    }

    virtual void close_ui_roots_and_hide_persistent_roots(WorldRuntime&) {
        missing_runtime_service();
    }

    virtual void clear_control_particle_group(WorldRuntime&) {
        missing_runtime_service();
    }

    virtual void configure_ui_control_particles(WorldRuntime&, UiControl*) {
        missing_runtime_service();
    }

    virtual UiControl* create_world_animation_playback_window(WorldRuntime&) {
        missing_runtime_service();
    }

    virtual void set_primary_ui_event_callback(WorldRuntime&, UiControl*, std::intptr_t (*)(WorldRuntime&, UiControl*, int, std::uint32_t, std::intptr_t)) {
        missing_runtime_service();
    }

    virtual void set_focused_ui_control(WorldRuntime&, UiControl*, int) {
        missing_runtime_service();
    }
};

struct ConstructionHostPort {
    virtual ~ConstructionHostPort() = default;

    virtual void refresh_world_interaction_origin_state(WorldRuntime&, bool record_action) {
        missing_runtime_service();
    }

    virtual void set_world_interaction_mode(WorldRuntime&, WorldInteractionMode, bool record_action) {
        missing_runtime_service();
    }

    virtual std::uint8_t dispatch_world_interaction(WorldRuntime&, WorldCellCoordinates, WorldInputModifierMask, bool use_preview_coordinates) {
        missing_runtime_service();
    }

    virtual void remove_park_object_at_world_tile(WorldRuntime&, std::int32_t, std::int32_t, std::int32_t) {
        missing_runtime_service();
    }
};

struct AssetIoPort {
    virtual ~AssetIoPort() = default;

    virtual int selected_resource_source(WorldRuntime&) {
        missing_runtime_service();
    }

    virtual int load_quickload_index_files(WorldRuntime&, std::string_view, std::string_view, std::optional<std::string_view>) {
        missing_runtime_service();
    }

    virtual std::optional<std::string> read_balance_asset(WorldRuntime&, std::string_view path) {
        missing_runtime_service();
    }

    virtual std::vector<std::string> find_asset_files(WorldRuntime&, std::string_view pattern) {
        missing_runtime_service();
    }

    virtual std::unique_ptr<Stream> open_asset_stream(WorldRuntime&, std::string_view path) {
        missing_runtime_service();
    }
};

struct ParticlePort {
    virtual ~ParticlePort() = default;

    virtual void set_particle_effect_position(WorldRuntime&, std::uint32_t, const std::array<std::int32_t, 3>&) {
        missing_runtime_service();
    }

    virtual void set_particle_effect_direction(WorldRuntime&, std::uint32_t, const std::array<std::int32_t, 3>&) {
        missing_runtime_service();
    }

    virtual void set_particle_effect_lifetime(WorldRuntime&, std::uint32_t, std::int32_t lifetime) {
        missing_runtime_service();
    }

    virtual std::uint32_t create_particle_effect(WorldRuntime&, std::uint32_t, const std::array<std::int32_t, 3>&, std::int32_t lifetime) {
        missing_runtime_service();
    }

    virtual void set_particle_effect_spread(WorldRuntime&, std::uint32_t, const std::array<std::int32_t, 3>&) {
        missing_runtime_service();
    }

    virtual std::uint32_t create_particle_effect_with_direction(WorldRuntime&, std::uint32_t, const std::array<std::int32_t, 3>& position, const std::array<std::int32_t, 3>& direction) {
        missing_runtime_service();
    }

    virtual void set_particle_render_scale(WorldRuntime&, std::uint32_t, std::int16_t scale_milli) {
        missing_runtime_service();
    }
};

struct ResourceHostPort {
    virtual ~ResourceHostPort() = default;
    virtual std::uint32_t update_positioned_request_position(WorldRuntime&, std::uint32_t, const std::array<std::int32_t, 3>&);
    virtual void submit_resource_target_parameter_request(WorldRuntime&, std::uint32_t request);
    virtual void request_target_recycle(WorldRuntime&, std::uint32_t request);

    virtual std::uint32_t process_resource_operation(WorldRuntime&, std::uint32_t target, std::int32_t operation, std::int32_t value) {
        missing_runtime_service();
    }

    virtual void submit_mobile_entity_positioned_template_request(WorldRuntime&, PersonRuntimeView*, std::uint32_t, std::int32_t);
    virtual void submit_mobile_entity_positioned_template_request(WorldRuntime&, MobileEntityState&, std::uint32_t, std::int32_t);
};

struct AudioHostPort {
    virtual ~AudioHostPort() = default;

    virtual void stage_music_mute_override(WorldRuntime&, std::uint16_t muted) {
        missing_runtime_service();
    }

    virtual void submit_sound_target_level_request(WorldRuntime&, std::uint32_t target, std::uint32_t level);
    virtual void submit_sound_target_restart_request(WorldRuntime&, std::uint32_t target);
    virtual void submit_sound_target_pause_request(WorldRuntime&, std::uint32_t target);
    virtual std::uint32_t get_sound_target_sample_time(WorldRuntime&, std::uint32_t target);
    virtual std::uint32_t refresh_sound_target_position(WorldRuntime&, std::uint32_t target, const std::array<std::int32_t, 3>&);

    virtual void delete_rsse_sound_occlusion_region(WorldRuntime&, RsseSoundOcclusionRegion*) {
        missing_runtime_service();
    }

    virtual RsseSoundOcclusionRegion* create_rsse_sound_occlusion_region(WorldRuntime&, float x, float y, float z, float radius, float height) {
        missing_runtime_service();
    }

    virtual void submit_mail_notification_template(WorldRuntime&, std::uint32_t catalog_slot, std::uint32_t key) {
        missing_runtime_service();
    }

    virtual std::uint32_t process_positioned_sound_request(WorldRuntime&, std::uint32_t, const PositionedTemplateRequest&) {
        missing_runtime_service();
    }
};

struct MeshHostPort {
    virtual ~MeshHostPort() = default;

    virtual std::int32_t find_embedded_node_resource_record(WorldRuntime&, std::uint32_t mesh, std::uint32_t flags, std::int32_t node) {
        missing_runtime_service();
    }

    virtual void submit_world_cell_render_update(WorldRuntime&, std::int32_t, std::int32_t, const CellRenderData&) {
        missing_runtime_service();
    }

    virtual void prepare_park_object_animation(WorldRuntime&, ParkObjectRuntimeView*, std::int32_t, std::int32_t) {
        missing_runtime_service();
    }

    virtual void draw_stranded_mobile_cell(WorldRuntime&, const WorldVector3&);

    virtual void set_mesh_runtime_speed(WorldRuntime&, std::uint32_t map_id, std::int32_t speed) {
        missing_runtime_service();
    }

    virtual std::optional<RsseMeshBounds> get_ride_mesh_world_bounds(WorldRuntime&, std::uint32_t mesh) {
        missing_runtime_service();
    }

    virtual float get_mesh_root_translation_height(WorldRuntime&, std::uint32_t mesh) {
        missing_runtime_service();
    }

    virtual std::array<float, 3> get_mesh_root_translation(WorldRuntime&, std::uint32_t mesh) {
        missing_runtime_service();
    }

    virtual void prepare_mesh_surface_query_geometry(WorldRuntime&, std::uint32_t mesh) {
        missing_runtime_service();
    }

    virtual float query_prepared_mesh_surface_height(WorldRuntime&, float x, float z) {
        missing_runtime_service();
    }

    virtual void rebuild_terrain_cell_shading(WorldRuntime&) {
        missing_runtime_service();
    }
};

struct DiagnosticsPort {
    virtual ~DiagnosticsPort() = default;

    virtual void log_debug_message(WorldRuntime&, std::string_view) {
        missing_runtime_service();
    }
};

struct CameraPort {
    virtual ~CameraPort() = default;

    virtual void set_renderer_fog_distance_range(WorldRuntime&, float, float) {
        missing_runtime_service();
    }

    virtual void restore_world_view_from_special_modes(WorldRuntime&) {
        missing_runtime_service();
    }

    virtual void toggle_world_view_point_of_interest(WorldRuntime&) {
        missing_runtime_service();
    }

    virtual std::optional<WorldViewInspectionAttachment> world_view_inspection_attachment(WorldRuntime&) {
        missing_runtime_service();
    }

    virtual void retire_world_view_inspection_mesh(WorldRuntime&, std::uint32_t, std::uint64_t) {}

    virtual void focus_world_view_camera_on_park_object_center(WorldRuntime&, ParkObjectRuntimeView*) {
        missing_runtime_service();
    }
};

struct ClockPort {
    virtual ~ClockPort() = default;
    virtual void get_local_time(SystemTimeFields*);
    virtual std::uint32_t get_simulation_clock_time_ms(WorldRuntime&);
    virtual std::uint32_t get_ui_clock_time_ms(WorldRuntime&);
    virtual std::uint32_t read_advisor_ui_delay_clock(WorldRuntime&);
};

struct WorldPorts {
    WorldOperationOverrides operations;
    AllocationPort& memory;
    EntityEnginePort& entities;
    WorldUiHostPort& ui;
    ConstructionHostPort& construction;
    AssetIoPort& assets;
    ParticlePort& effects;
    ResourceHostPort& resources;
    AudioHostPort& audio;
    MeshHostPort& meshes;
    DiagnosticsPort& diagnostics;
    CameraPort& camera;
    ClockPort& clock;

    template <class Port, class Backend>
    static Port& bind(Backend& backend) {
        if constexpr (std::derived_from<Backend, Port>) {
            return backend;
        } else {
            static_assert(!std::same_as<Port, AllocationPort>, "an allocation backend is required");
            static Port unbound;
            return unbound;
        }
    }

    template <class Backend>
        requires(!std::same_as<std::remove_cvref_t<Backend>, WorldPorts>)
    explicit WorldPorts(Backend& backend)
        : operations(backend),
          memory(bind<AllocationPort>(backend)),
          entities(bind<EntityEnginePort>(backend)),
          ui(bind<WorldUiHostPort>(backend)),
          construction(bind<ConstructionHostPort>(backend)),
          assets(bind<AssetIoPort>(backend)),
          effects(bind<ParticlePort>(backend)),
          resources(bind<ResourceHostPort>(backend)),
          audio(bind<AudioHostPort>(backend)),
          meshes(bind<MeshHostPort>(backend)),
          diagnostics(bind<DiagnosticsPort>(backend)),
          camera(bind<CameraPort>(backend)),
          clock(bind<ClockPort>(backend)) {}
};

#pragma GCC diagnostic pop
} //namespace simcoaster
