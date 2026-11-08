#pragma once
#include "world_ports.hpp"

namespace simcoaster {
struct WorldResourceState {
    WorldMeshRuntime* meshes{}; //Non-owning host binding; meshes own model storage.
    TrackRideManager* track_rides{};
    ResourceVariantRuntime variants{};
    RendererCacheValidity renderer_cache{};
    std::uint32_t sprite_resource_rendering_enabled = 1;
    std::span<const ResourceCategoryVariantRange> resource_category_variants{};
    std::uint32_t renderer_cache_generation{};
    ObjectCatalogManager object_catalog{};
    std::uint32_t ui_catalog_slot{};
    std::uint32_t resource_manager_feature_flag_0400{};
    std::int32_t resource_manager_dispatch_initialized{};
    std::uint32_t kids_catalog_slot{};
    std::uint32_t local_rides_catalog_slot{}, local_ambient_catalog_slot{}, global_rides_catalog_slot{},
        staff_catalog_slot{}, ambient_catalog_slot{}, global_speech_catalog_slot{};
};

struct WorldScriptState {
    RsseStateManager states{};
    RsseTourManager tours{};
};

struct WorldWeatherState {
    VegetationWeatherParameters vegetation_weather{}; //Independent gameplay_prng_state, full upper WORD
    //used by train boarding.
    WeatherParameters weather_balance{};
    WeatherEffectsParameters weather_effects_balance{};
    WindEffectRuntimeState wind_effects{};
    WeatherEffectsRuntime* weather_runtime{};
    WeatherHudState weather_hud{};
    std::uint8_t weather_refresh_requested = 1;
    std::array<std::array<std::int32_t, 4>, 4> weather_season_chances{};
    std::uint32_t weather_seasons_parsed_extent{};
    std::uint32_t map_heightfield_width{}, map_heightfield_height{};
    std::int32_t rain_intensity_sound_handle{};
};

struct WorldBalanceState {
    StandardSupplementBalance standard_supplement{};
    ThemeHoardingConfiguration hoarding_sam{};
    StandardUiBalance standard_ui_balance{};
    ParkAwardBalance award_balance{};
};

struct WorldSimulationState {
    World* current{};
    World* reference{}; //Native world_structure primary slot.
    //The sole native initializer binds world_structure_global_reference to
    //the primary slot. Its606 remaining references only read that stable alias.

    EventRegistry* event_registry{};
    MapCell *map_range_base{}, *map_range_end{};
    TrackCell *track_range_base{}, *track_range_end{};
    std::uint32_t map_record_stride{}, track_record_stride{};
    MapCell* map_cell_records{};
    WorldRegionEffectsView region_effects{};
    //Native track lookup uses a constant offset from the global map array.
    //Bind the parallel array explicitly instead of preserving x86 ABI
    //arithmetic.
    TrackCell* track_cell_records{};
    BlockSavePrefix world_block_saver{};
};

struct WorldMissionState {
    std::unique_ptr<ThemeGlobalInfoManager> owned_theme_manager{};
    ThemeGlobalInfoManager* theme_global_info_manager{};
    std::string missions_directory = "data/missions";
    MissionTransitionRuntime mission_transition;
};

struct WorldFinanceState {
    SharesFinanceState shares_finance{};
    AnnualReportPolicy annual_report_policy{};
    SpamBalance spam_balance{};
    SpamSchedulerState spam_scheduler{};
    std::int32_t company_shares_issued{};
    bool shares_panel_refresh_pending{};
    std::int32_t initial_cash{}; //Standard.sam BankAccountInfo.InitialCash binding.
    std::int32_t initial_admission_fee{};
    std::array<BankLoanOffer, 10> bank_loan_offers{};
    CalendarReportFields calendar_report_fields{};
};

struct WorldMessageState {
    //Standard.sam ShareInfo.CompanySharesIssued.
    std::array<TagMessageVariant, 9> tag_system_message_variants = default_tag_system_message_variants;
    MailLanguageResources mail_language{};
    std::vector<MailDialogNotice> modal_mail_notices{};
    std::uint32_t tag_mail_next_creation_sequence{};
    std::uint8_t tag_panel_refresh_requested{};
    ThingId congratulations_popup{};
};

struct WorldViewRuntimeState {
    ThingHeader* cursor_hit{};
    ThingHeader* secondary_cursor_record_snapshot{};
    float first_person_view_distance{}, fog_distance_start{}, fog_distance_end{};
    std::uint32_t flags{};
    WorldViewCameraState camera{};
    MobileRenderState mobile{};
};

struct WorldAttractionState {
    std::uint32_t park_object_record_count{};
    ParkObjectCreationBalance park_object_creation_balance{};
    std::uint8_t park_object_mode_initialized{};
    const MeshRuntimeOpeningView* active_mesh_runtime{};
    float park_object_queue_operating_speed_scale{};
    std::uint32_t special_ride_group_count{};
    bool force_special_ride_group_ready{}; //Native
    //debug_force_special_ride_group_ready.
    std::array<SpecialRideRegistryGroup, 4> special_ride_groups{};
};

struct WorldVisitorState {
    std::uint32_t count{};
    GuestFlowBalance flow_balance{};
    PersonCreationBalance creation_balance{};
    ArrivalParameters arrivals{};
    VisitorAdvisorBalance visitor_advisor_balance{};
    std::uint8_t visitor_advisor_initialized{};
    std::uint8_t arrival_mode_initialized{};
    PersonBalance needs_balance{};
    PersonEnvironmentBalance environment{};
    std::vector<WeightedSteeringBehavior> steering_behaviors{ default_steering_behaviors.begin(),
                                                              default_steering_behaviors.end() };
    bool navigation_step_rejection_seen{};
    std::uint8_t mobile_effects_mode_initialized{};
    std::uint32_t balloon_life_minimum = 25, balloon_life_maximum = 255;
};

struct WorldAdvisorState {
    std::uint8_t advisor_candidate_initialized{};
    AdvisorSpeechState speech{};
    AdvisorQueuePolicy queue_policy{};
    AdvisorBehaviorRuntime behavior{};
    std::shared_ptr<AdvisorBalance> balance;
    AdvisorPresentationState presentation{};
    //Native cancellation can index the no-action sentinel645 beyond the644
    //saved history entries. Preserve its bookkeeping without corrupting memory.
    AdvisorActionHistoryEntry no_action_history{};
    std::uint32_t scoring_saved_word{};
    std::int32_t advisor_help_override_response_id = 1014;
    std::int32_t advisor_help_requested_action_id = 645;
};

struct WorldModeState {
    std::uint32_t configuration{};
    //NativeA2738C graphics/profile flags are distinct from game-mode flags.
    std::uint32_t graphics{};
    GameMode game{};
    int scheduler{};
    std::uint8_t source_recreation{};
    std::uint8_t tick_mode_initialized{};
    std::uint8_t record_mode_initialized{};
    std::uint8_t award_mode_initialized{};
    std::uint8_t map_status_game_mode_initialized{};
};

struct WorldConstructionState {
    std::uint16_t coaster_design_name_object_id{};
    std::array<char16_t, 33> coaster_design_name_prefix{}, coaster_design_name_suffix{};
    void* handler{};
    std::int32_t mode{}, path_start_x = -1, path_start_y = -1;
    WorldInputModifierMask modifiers = WorldInputModifierMask::none;
    bool coaster_editor_active{};
    std::int32_t path_cell_base_cost{}, queue_cell_base_cost{};
    std::int32_t kart_track_cell_base_cost{}, water_track_cell_base_cost{};
    std::uint8_t cell_costs_suppressed{};
    std::int32_t action_recording_disabled{}, force_cell_removal{};
    Stream* action_record_stream{};
    NativeCoasterEditor* editor{};            //Borrowed from the park owner.
    CoasterCatalogRuntime* coaster_catalog{}; //Value owner in ParkRuntime.
    std::uint32_t action_record_streams_initialized{};
    std::array<std::uint8_t, 32> save_object_definition_mask{};
    std::uint16_t selected_object_definition_id{}, previous_object_definition_id{};
    std::uint32_t selected_track_type{};
    std::uint32_t track_type{}; //674750 raw selector, gated by674780 secondary selection.
    std::uint16_t placement_object_definition_id{};
    ResearchCatalogDefinition* selected_placement_definition{};
    DefinitionGrid* active_object_placement_grid{};
    DefinitionGrid coaster_source_picker_grid{}, prebuilt_coaster_placement_grid{};
    std::uint8_t catalog_mode_initialized{}, coaster_source_is_plain{}, coaster_source_grid_bypass{};
    std::uint8_t prebuilt_grid_override_valid{}, prebuilt_grid_override_consumed{};
    std::int32_t line_endpoint_dispatch{}, endpoint_stop_pending{}, last_successful_type{};
    std::uint32_t cell_line_first_dispatch_pending{}; //Distinct from
    //line_endpoint_dispatch.
    std::int32_t previous_path_step_x = 128, previous_path_step_y = 128;
    CellId origin_cell{};
    ParkObjectRuntimeView* park_object{};
    std::uint32_t grid_change_serial_counter{};
    std::uint8_t world_hoarding_mode{};
    //Native9D679C survives ordinary construction cancellation; not save data.
    bool world_hoarding_route_valid{};
    const ActiveTerrainModelDataView* active_terrain_model_data{};
    //Stable pre-placement terrain metadata; the host scene owns this storage.
    std::span<const TerrainCellMetadataRecord> original_terrain_cells{};
    std::int32_t track_endpoint_phase{}, track_endpoint_stop_pending{};
    std::int32_t track_anchor_removal_enabled{};
    std::array<std::int32_t, 2> interaction_cursor_anchor{ -1, -1 }, interaction_secondary_anchor{ -1, -1 };
    CellRenderData world_cell_render_scratch{};
    CellRenderData world_cell_mesh_render_scratch{};
    std::int32_t hoarding_variant_roll{}, previous_hoarding_variant{};
    std::array<CellMeshTemplate, 8> queue_mesh_templates = initial_queue_mesh_templates;
    std::array<CellMeshTemplate, 20> gokart_mesh_templates = initial_gokart_mesh_templates;
    std::array<CellMeshTemplate, 25> water_ride_mesh_templates = initial_water_ride_mesh_templates;
    std::array<CellMeshTemplate, 28> hoarding_mesh_templates = initial_hoarding_mesh_templates;
    DefinitionGrid queue_mesh_placement_geometry{}; //Queue loader sets its
    //separate1x1 grid.
    DefinitionGrid cell_mesh_placement_geometry{}; //Asset loaders set the shared2x2 grid.
    std::uint8_t tile_variant_toggle{};
    std::uint8_t hoarding_direction{};
    std::int32_t hoarding_chain_start_x = -1, hoarding_chain_start_y = -1;
    std::int32_t hoarding_target_x{}, hoarding_target_y{};
};

struct WorldTimingState {
    std::uint32_t last_tick_div4{};
    std::uint32_t first_hour_time{};
    std::int16_t speed_percent{};
    double scale{};
};

struct WorldAnimationState {
    ThingId thing{};
    AnimationControlFlags control{};
};

struct WorldUiBindings {
    UiControl* interaction_toolbar{};
    CommandMenuBuilderView* command_menu_builder{};
    UiControl *active_root{}, *persistent_root{};
    std::int32_t persistent_menu_category{};
    WorldUiState panels;
    UiControl* mail_counter_root{};
};

struct WorldDebugState {
    CheatCommandState cheat_commands{};
};

struct WorldStaffState {
    std::array<std::int32_t, 6> firing_line_for_rank_limits{};
    StaffWorkParameters work_balance{};
    std::optional<PendingStaffPlacement> placement;
    WorldStaffInteractionState interaction;
    std::uint32_t last_staff_wage_notice_elapsed_day{}; //Native process-global
    //AA07F4; not saved.
    StaffRecruitmentRuntime recruitment{};
};

struct WorldSourceState {
    std::string data_directory = "data";
    //Host binding for ResourceSourceManager.selected_source_name; distinct from
    //the theme-info manager's selected record. The path cache is overwritten on
    //every call, as in the native OwnedString implementation.
    std::string selected_resource_source_name, selected_resource_levels_path_cache;
};

struct WorldResearchState {
    ResearchBalanceInput balance{};
    std::int32_t max_researchers{};
};

struct WorldChallengeState {
    ChallengeBalance balance{};
    ChallengeDefinitions definitions{};
    bool initialized{}, debug_always_win_challenges{};
};

struct WorldRandomState {
    std::uint32_t crt_random_state{}; //Host binding for the shared CRT thread seed.
    std::uint32_t mesh_passenger_random_state{};
};

struct WorldStateOwners {
    template <class Backend>
    explicit WorldStateOwners(Backend& dependencies, EntityOwnership ownership = EntityOwnership::allocator)
        : ports(dependencies),
          entities(ownership) {}

    explicit WorldStateOwners(WorldPorts bindings, EntityOwnership ownership = EntityOwnership::allocator)
        : ports(bindings),
          entities(ownership) {}

    WorldStateOwners(const WorldStateOwners&) = delete;
    WorldStateOwners& operator=(const WorldStateOwners&) = delete;
    WorldStateOwners(WorldStateOwners&&) = delete;
    WorldStateOwners& operator=(WorldStateOwners&&) = delete;
    WorldPorts ports;
    WorldResourceState resources;
    WorldScriptState scripts;
    WorldWeatherState weather;
    WorldBalanceState balance;
    ThingStore entities;
    WorldSimulationState simulation;
    WorldMissionState missions;
    WorldFinanceState finance;
    WorldMessageState messages;
    WorldViewRuntimeState view;
    WorldAttractionState attractions;
    WorldVisitorState visitors;
    WorldAdvisorState advisor;
    WorldModeState mode;
    WorldConstructionState construction;
    WorldTimingState timing;
    WorldAnimationState animation;
    WorldUiBindings ui;
    WorldDebugState debug;
    WorldStaffState staff;
    WorldSourceState source;
    WorldResearchState research;
    WorldChallengeState challenges;
    WorldRandomState random;

    World* referenced_world() const {
        return simulation.reference;
    }
};
} //namespace simcoaster
