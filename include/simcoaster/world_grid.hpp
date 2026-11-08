#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
struct CellLocalOffset {
    std::int32_t x, y;
};
enum class CellNeighborDirection : std::uint8_t {
    north = 1,
    northeast = 2,
    east = 4,
    southeast = 8,
    south = 16,
    southwest = 32,
    west = 64,
    northwest = 128
};
CellLocalOffset rotate_park_object_local_cell_offset(CellLocalOffset, std::int32_t rotation);
CellNeighborDirection get_cell_direction_toward_point(WorldCellCoordinates target, WorldCellCoordinates source);
bool is_world_map_coordinate_in_bounds(std::int32_t, std::int32_t);
bool is_source_grid_coordinate_eligible(const WorldRuntime&, std::uint32_t, std::uint32_t);
void reset_world_track_cell_defaults(WorldRuntime&, TrackCell*);
void apply_world_map_cell_status_flags(WorldRuntime&, MapCell*, bool);
std::int32_t reset_world_cells_in_grid(WorldRuntime&, std::int32_t, std::int32_t);
CellLocalOffset scale_world_cell_local_offset_pair(CellLocalOffset, std::int32_t);
MapCell* resolve_world_cell_by_scaled_local_offset(WorldRuntime&, const CellBase*, CellLocalOffset, std::int32_t);
MapCell* resolve_world_cell_from_selector_flag_byte(WorldRuntime&, const CellBase*, std::uint8_t);
MapCell* get_world_map_cell_record_by_id(WorldRuntime&, CellId);
TrackCell* resolve_track_cell_for_grid_entry(WorldRuntime&, const CellBase*);
bool world_track_reference_matches_type_set(WorldRuntime&, CellId reference);
bool world_cell_blocks_person_weather_appearance(const TrackCell&);
void initialize_world_litter_record(WorldRuntime&, MapCell&, LitterSelector);
TrackCell* resolve_track_cell_at_local_offset(WorldRuntime&, const CellBase*, CellLocalOffset, std::int32_t);
TrackCell* resolve_track_cell_in_direction(WorldRuntime&, const CellBase*, std::uint8_t, std::int32_t);
ThingId find_first_park_object_id_in_map_cell(WorldRuntime&, const MapCell*);
std::uint8_t opposite_world_cell_direction(std::uint8_t);
std::uint8_t advance_world_cell_direction(std::uint8_t, std::uint8_t);
std::uint8_t get_world_cell_neighbor_mask(const WorldRuntime&, const CellBase*);
void clear_world_cell_neighbor_mask_bits(const WorldRuntime&, CellBase*, std::uint8_t);
std::uint8_t or_world_cell_neighbor_mask_bits(const WorldRuntime&, CellBase*, std::uint8_t);
void update_cell_connections(WorldRuntime&, CellBase*, std::int32_t, std::int32_t, std::int32_t, bool, bool);
std::uint32_t submit_positioned_template_request(WorldRuntime&, std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::int32_t, std::int32_t);
std::int32_t count_world_cell_neighbor_bits(const WorldRuntime&, const CellBase*);
std::int32_t count_world_cell_cardinal_neighbor_bits(const WorldRuntime&, const CellBase*);
std::uint8_t get_world_cell_corner_neighbor_mask(const WorldRuntime&, const CellBase*);
std::uint8_t get_world_cell_branch_neighbor_mask(const WorldRuntime&, const CellBase*);
void set_world_cell_direction(CellBase*, std::uint8_t);
std::uint8_t get_world_cell_direction(const CellBase*);
CellId set_world_cell_parent_id_unchecked(CellBase*, CellId);
std::uint32_t clear_world_cell_flags_mask(CellBase*, std::uint32_t);
void clear_world_cell_flags(CellBase*, std::uint16_t);
std::uint16_t assign_world_cell_flags(CellBase*, std::uint16_t);
bool world_cell_has_track_segment_flags(const CellBase*);
bool world_cell_is_track_child(const CellBase*);
bool world_cell_is_track_root(const CellBase*);
std::uint32_t next_world_grid_change_serial(WorldRuntime&);
void store_world_grid_change_serial_for_cell_id(World*, std::uint32_t, CellId);
bool can_change_world_cell_type(const WorldRuntime&, const CellBase*, std::int32_t);
bool try_change_world_cell_type(WorldRuntime&, CellBase*, std::int32_t);
bool try_assign_world_cell_type(WorldRuntime&, CellBase*, std::int32_t);
std::int32_t get_park_object_scrap_value_percent(AttractionWorld&, ParkObjectRuntimeView*);
std::int32_t apply_world_re_cell_effect_profile(WorldRegionEffectsView, bool, RegionEffectProfileId, CellId);
std::int32_t apply_world_re_cell_effect_profile_with_radius(WorldRegionEffectsView, bool, RegionEffectProfileId, CellId, std::uint16_t radius);
std::int32_t add_world_re_cell_effect_profile(WorldRegionEffectsView, RegionEffectProfileId, CellId);
std::int32_t remove_world_re_cell_effect_profile(WorldRegionEffectsView, RegionEffectProfileId, CellId);
std::int32_t add_world_re_cell_effect_profile_with_radius(WorldRegionEffectsView, RegionEffectProfileId, CellId, std::uint16_t radius);
std::int32_t remove_world_re_cell_effect_profile_with_radius(WorldRegionEffectsView, RegionEffectProfileId, CellId, std::uint16_t radius);
std::int32_t sum_recent_park_thing_history_values(const AnalyserDwordHistory144&, double&, std::int32_t);
//Ordinary dispatch branch used by decoded definition-grid symbols. Full
//interactive preview/control dispatch is a separate, unported operation.
bool apply_definition_grid_cell_type(WorldRuntime&, CellBase*, std::int32_t);
void refresh_world_cell_render_state(WorldRuntime&, CellBase*);
CellRenderData calculate_world_cell_tile_data(WorldRuntime&, const CellBase*);
CellRenderData calculate_world_cell_render_data(WorldRuntime&, const CellBase*);
CellRenderData& calculate_world_cell_mesh_render_scratch(WorldRuntime&, const CellBase*);
void restore_world_map_cell_mesh(WorldRuntime&, MapCell*);
void update_world_track_cell_mesh(WorldRuntime&, TrackCell*);
void refresh_world_cell_meshes_after_restore(WorldRuntime&, World&);
const std::string& build_selected_resource_levels_path(WorldRuntime&);
void load_queue_mesh_definition_templates(WorldRuntime&);
void load_gokart_mesh_definition_templates(WorldRuntime&);
void load_water_ride_mesh_definition_templates(WorldRuntime&);
void load_hoarding_mesh_definition_templates(WorldRuntime&);
void destroy_mesh_slots_using_queue_template_indices(WorldRuntime&);
void destroy_mesh_slots_using_gokart_template_indices(WorldRuntime&);
void destroy_mesh_slots_using_water_ride_template_indices(WorldRuntime&);
void destroy_mesh_slots_using_hoarding_template_indices(WorldRuntime&);
void destroy_mesh_slots_using_world_template_indices(WorldRuntime&);
void update_world_cell_mesh(WorldRuntime&, CellBase*);
std::uint32_t create_or_refresh_gokart_track_mesh_instance(WorldRuntime&, std::int32_t, std::int32_t, CellRenderData, bool);
std::uint32_t create_or_refresh_queue_mesh_instance(WorldRuntime&, std::int32_t, std::int32_t, CellRenderData, bool);
std::uint32_t create_or_refresh_water_ride_track_mesh_instance(WorldRuntime&, std::int32_t, std::int32_t, CellRenderData, bool);
std::uint32_t create_or_refresh_hoarding_mesh_instance(WorldRuntime&, std::int32_t, std::int32_t, CellRenderData, bool);
void remove_map_cell_contents(WorldRuntime&, CellBase*, std::uint32_t, std::int32_t);
void remove_track_cell_contents(WorldRuntime&, CellBase*, std::int32_t, std::int32_t, std::int32_t);
//Restricted loader sequence: operation0 followed by129 on the cleared cell.
bool apply_hoarding_track_cell_command(WorldRuntime&, CellBase*, std::int32_t);
void apply_object_definition_after_shape_grid(WorldRuntime&, ThingHeader*);
void set_hoarding_chain_nomodify(WorldRuntime&, std::uint8_t);
bool apply_hoardings_map_cells_to_world(WorldRuntime&, std::string_view);
bool apply_hoardings_map_cells_to_world(WorldRuntime&, Stream&);
} //namespace simcoaster
