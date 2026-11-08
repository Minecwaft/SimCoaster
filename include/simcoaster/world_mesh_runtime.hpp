#pragma once
#include "game_restore.hpp"
#include "mesh_height_map.hpp"
#include "mesh_hoarding.hpp"
#include "mesh_playback.hpp"
#include "mesh_runtime_queries.hpp"
#include "mesh_texture_environment.hpp"
#include "world_domains_fwd.hpp"

namespace simcoaster {
struct CellMeshRegistration;
struct CellMeshRefresh;
struct MeshDefinitionTemplateLoad;
struct CoasterSaveSection;
struct WorldCoasterRuntime;
struct WorldCoasterDefinition;
struct ParkObjectRuntimeView;
struct WorldSignRuntime;
class WorldMeshScene;
enum class MeshTextureSet;
class WorldMeshRuntime;
enum class MeshTemplateLoadFlag : std::uint32_t {
    procedural_base = 1,
    surface_geometry = 2,
    unfiltered_base = 4,
    hoarding = 8,
    initialize_animations = 0x10,
    model_animation_mode = 0x20,
    always_animate = 0x40,
    hide_clone_nodes = 0x80,
    auxiliary_lookup = 0x100,
    retire_when_complete = 0x200,
    height_map = 0x400,
    bounds_height = 0x800,
    mark_base_materials = 0x2000,
    normalize_uvs = 0x4000,
    model_terrain_mode = 0x10000,
    companion = 0x20000,
    preserve_transition_channels = 0x40000,
    pulsing_colors = 0x100000,
    model_render_mode = 0x200000,
    head_processing = 0x400000,
    relative_animation = 0x1000000,
};

//Template handles and catalog keys are distinct namespaces. New registrations
//append even when a key repeats; restore finds the most recent registration.
struct WorldMeshTemplate {
    std::uint32_t definition_key {}, control_count { 1 }, runtime_flags {};
    std::int32_t footprint_width {}, footprint_height {};
    std::shared_ptr<const MeshPlaybackAssets> assets, alternate;
    std::shared_ptr<const MeshHoardingTemplate> hoarding {};
    std::shared_ptr<const MeshProceduralBase> terrain_base {};
    std::shared_ptr<const MeshProceduralBase> alternate_terrain_base {};
    std::shared_ptr<const MeshHeightMap> height_map {};
    std::string source_directory {}, resource_name {};
    std::optional<std::u16string> sign_text_override {}, sign_source_override {};
    std::uint32_t load_options {};
    std::shared_ptr<const WorldCoasterDefinition> coaster {};
    std::shared_ptr<const MeshBaseTilePlan> terrain_texture_plan {};
};

struct WorldMeshCellBounds {
    std::array<std::int64_t, 2> minimum {}, maximum {};
};

struct MeshPassengerResourceLease {
    std::uint32_t handle {};
    std::uint64_t generation {};
};

struct WorldMeshInstance {
    //Process-only allocation identity. Slot reuse and restored records receive
    //new identities; editor rollback never rewinds the owning counter.
    std::uint64_t generation {};
    std::optional<std::u16string> sign_text_override {}, sign_source_override {};
    MeshRuntimeOpeningView opening_view {};
    std::uint32_t template_id {};
    MeshPlaybackInstance playback;
    std::optional<MeshHoardingRuntime> hoarding;
    std::shared_ptr<const MeshProceduralBase> terrain_base;
    float placement_reference_height {};
    std::shared_ptr<const DefinitionGrid> placement_geometry;
    SavedMeshInstance metadata;
    std::optional<WorldMeshCellBounds> cell_bounds;
    std::vector<MeshEmbeddedState> embedded;
    std::vector<MeshPassengerResourceLease> passenger_resources;
    std::uint32_t embedded_flags {}, active_embedded_count {};
    //Save projections are temporary native-width staging, never runtime owners.
    mutable std::vector<std::uint32_t> wire_node_flags;
    mutable std::vector<MeshAnimationControl> wire_controls;
    MeshRestoreInstance restore_view;
    std::vector<MeshBaseTilePlacement> terrain_texture_cells;
    std::vector<std::pair<std::size_t, std::shared_ptr<const TextureImage>>> terrain_texture_originals;
    std::optional<MeshError> terrain_texture_error;
};

//A move-only token for a failed creation scope. Only this mesh owner can
//construct it; rollback verifies that every pre-existing instance survived and
//every new child was reclaimed before restoring the allocator and event tail.
class WorldMeshCreationCheckpoint {
    friend class WorldMeshRuntime;

    struct SlotIdentity {
        std::uint32_t slot {};
        const WorldMeshInstance* instance {};
    };

    const WorldMeshRuntime* owner_ {};
    MeshSlotAllocatorState allocator_ {};
    std::size_t slot_limit_ {}, event_count_ {}, retiring_count_ {};
    std::uint32_t selected_slot_ {};
    std::vector<SlotIdentity> original_live_;
    WorldMeshCreationCheckpoint() = default;

public:
    WorldMeshCreationCheckpoint(WorldMeshCreationCheckpoint&&) noexcept = default;
    WorldMeshCreationCheckpoint& operator=(WorldMeshCreationCheckpoint&&) noexcept = default;
    WorldMeshCreationCheckpoint(const WorldMeshCreationCheckpoint&) = delete;
    WorldMeshCreationCheckpoint& operator=(const WorldMeshCreationCheckpoint&) = delete;
};
enum class WorldMeshUpdate {
    selected = 2,
    background = 3
};

struct WorldMeshEvent {
    std::uint32_t slot {}, events {};
};
enum class WorldMeshHoardingEvent : std::uint32_t {
    registered = 8,
    unregistered = 16,
    pose_changed = 32
};

//Owns actual model clones, animation controls and stable one-based mesh slots.
//Bind WorldRuntime::mesh_runtime while this owner is alive. Main model and
//hoarding terrain geometry are owned here. Drawing, terrain texture blending
//and coaster runtime metrics remain separate renderer/simulation dependencies.
class WorldMeshRuntime final : public GameRestoreBackends, public GameSaveBackends {
    std::vector<WorldMeshTemplate> templates_;
    std::vector<std::unique_ptr<WorldMeshInstance>> slots_ { 1 };
    MeshSlotAllocatorState allocator_ {};
    std::vector<WorldMeshEvent> events_;
    std::unique_ptr<DefinitionGrid> fallback_storage_;
    std::unique_ptr<WorldCoasterRuntime> coasters_;
    std::vector<std::uint32_t> retiring_slots_;
    bool restoring_ {};
    std::unique_ptr<WorldSignRuntime> signs_;
    MeshTextureEnvironment* terrain_textures_ {}; //Bound production renderer, owned by ParkRuntime.
    std::uint64_t next_instance_generation_ {};
    void prepare_terrain_textures(WorldMeshInstance&, const WorldMeshTemplate&);
    void erase_coaster(AttractionWorld&, std::uint32_t);
    void deliver(WorldRuntime&, std::uint32_t, const MeshPlaybackResult&);
    void deliver_hoarding(WorldRuntime&, std::uint32_t, const MeshHoardingResult&);
    std::expected<void, MeshError> advance_hoarding(WorldRuntime&, std::uint32_t, WorldMeshInstance&);
    std::expected<void, MeshError> restore_passenger_resources(WorldRuntime&, WorldMeshInstance&);

public:
    MeshPlaybackClocks clocks;
    std::chrono::duration<float> simulation_delta {};
    std::optional<MeshError> last_error; //Details when the native 0/false boundary fails.
    std::uint32_t selected_slot {};
    //Transient503FD0/4CDDC0 hover override; generation rejects reused slots.
    std::uint32_t hovered_slot {};
    std::uint64_t hovered_generation {};
    //Native graphics bit4000. The production scene owns procedural pixels and
    //texture libraries; isolated geometry fixtures can leave this disabled.
    bool terrain_texture_blending {};
    bool sign_texture_generation {};
    WorldMeshRuntime();
    ~WorldMeshRuntime() override;
    WorldMeshRuntime(const WorldMeshRuntime&) = delete;
    WorldMeshRuntime& operator=(const WorldMeshRuntime&) = delete;
    void configure_sign_fonts(std::filesystem::path archive, std::string language = "American");
    void configure_terrain_textures(MeshTextureEnvironment&);
    MeshTextureSet texture_set() const;
    bool animated_textures() const;
    std::expected<bool, MeshError> render_model_signs(MeshModel&, std::string_view directory, std::string_view basename, std::u16string_view first, std::optional<std::u16string_view> second = {});
    bool set_object_sign_caption(WorldRuntime&, ParkObjectRuntimeView&, std::u16string_view first, std::u16string_view second);
    void rebuild_world_sign_textures(WorldRuntime&);
    std::expected<std::uint32_t, MeshError> register_template(WorldMeshTemplate);
    std::expected<std::uint32_t, MeshError> load_template(WorldRuntime&, const MeshDefinitionTemplateLoad&);
    std::expected<void, MeshError> prepare_terrain(WorldRuntime&, std::uint32_t, const DefinitionGrid* shape, bool allow_skirts = true);
    std::expected<void, MeshError> prepare_hoarding(WorldRuntime&, std::uint32_t, const DefinitionGrid* shape, const DefinitionGrid& layout, std::string_view theme, bool allow_skirts = true);
    const WorldMeshTemplate* definition(std::uint32_t) const;
    void configure_coaster_template(std::uint32_t, WorldCoasterDefinition);
    WorldMeshInstance* instance(std::uint32_t);
    const WorldMeshInstance* instance(std::uint32_t) const;

    std::size_t slot_limit() const {
        return slots_.size();
    } //One past the final slot, including holes.

    std::expected<std::uint32_t, MeshError> create(WorldRuntime&, const CellMeshRegistration&);
    WorldMeshCreationCheckpoint capture_creation_checkpoint(WorldRuntime&) const;
    //Reclaim all meshes created since capture first. Throws if the original
    //owners changed, new live slots remain, or restore is currently staging.
    void rollback_failed_creation(WorldRuntime&, const WorldMeshCreationCheckpoint&);
    std::expected<void, MeshError> refresh_terrain(WorldRuntime&, std::uint32_t);
    bool erase(WorldRuntime&, std::uint32_t);
    std::uint32_t mark_for_refresh(WorldRuntime&, const CellMeshRefresh&);
    //Retains the bounded save snapshot and validates its parent/child mesh
    //slots. Call after mesh/RSSE restoration and before the World repair walk.
    std::expected<void, MeshError> install_coaster_snapshot(AttractionWorld&, CoasterSaveSection);
    std::expected<CoasterSaveSection, MeshError> capture_coaster_snapshot() const;
    bool set_coaster_speed(AttractionWorld&, std::uint32_t map_id, std::uint32_t percent);
    bool set_coaster_capacity(std::uint32_t map_id, std::int32_t capacity);

    const WorldCoasterRuntime* coasters() const {
        return coasters_.get();
    }

    WorldCoasterRuntime* coasters() {
        return coasters_.get();
    }

    void bind_rsse(WorldRuntime&, std::uint32_t slot, std::uint32_t script);
    void set_registered(WorldRuntime&, std::uint32_t slot, bool);
    void force_animation_active(std::uint32_t slot);
    void release_embedded_person(WorldRuntime&, std::uint32_t slot, std::size_t resource_index);
    std::uint32_t bind_embedded_person(WorldRuntime&, std::uint32_t slot, std::size_t resource_index, std::uint32_t person);
    //Native49B140 projection after animation and final camera resolution.
    //Idempotent preparation for both drawing and picking; never ticks scripts.
    std::expected<void, MeshError> prepare_passenger_sprites(WorldRuntime&, const MeshMatrix& camera_to_world);
    std::expected<MeshPlaybackResult, MeshError> request(WorldRuntime&, std::uint32_t, std::size_t, const MeshPlaybackRequest&);
    std::expected<void, MeshError> update(WorldRuntime&, WorldMeshUpdate);
    std::expected<void, MeshError> activate(WorldRuntime&, std::uint32_t);
    //Model callback wrapper 0x4C3660; marks a slot already handled this frame.
    std::expected<void, MeshError> mark_visible(WorldRuntime&, std::uint32_t);
    std::expected<void, MeshError> select_hoarding_variant(WorldRuntime&, std::uint32_t, std::int32_t variant);
    void reverse_hoarding(std::uint32_t);
    void set_deferred_rates(std::uint32_t, float);
    void set_control_rate(std::uint32_t, std::size_t, float);
    std::vector<WorldMeshEvent> take_events();
    std::uint32_t high_resolution_time_ms() override;
    void restore_clock_snapshots(std::uint32_t simulation_ms, std::uint32_t unscaled_ms) override;
    void begin_mesh_restore(WorldRuntime&, const MeshSlotAllocatorState&) override;
    void update_mesh_animation_clock_sources(WorldRuntime&) override;
    std::int32_t find_mesh_template_by_key(WorldRuntime&, std::uint32_t) override;
    const DefinitionGrid* fallback_mesh_geometry(WorldRuntime&, std::int32_t, std::int32_t) override;
    MeshRestoreInstance& mesh_restore_instance(WorldRuntime&, std::uint32_t, const SavedMeshInstance&) override;
    void rebuild_mesh_deformation_after_restore(WorldRuntime&, std::uint32_t) override;
    std::optional<MeshAnimationTiming> mesh_animation_timing(WorldRuntime&, std::uint32_t, std::uint32_t, std::uint32_t) override;
    void invalidate_mesh_renderer_cache(WorldRuntime&) override;
    bool finish_mesh_restore_instance(WorldRuntime&, std::uint32_t, MeshRestoreInstance&) override;
    bool finish_mesh_restore(WorldRuntime&) override;
    MeshSlotAllocatorState mesh_slot_allocator_state(WorldRuntime&) const override;
    std::optional<MeshSaveInstance> mesh_save_instance(WorldRuntime&, std::uint32_t) const override;
};
} //namespace simcoaster
