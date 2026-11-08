#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
struct GridPathCell {
    std::uint8_t x{}, y{};
    bool operator==(const GridPathCell&) const = default;
};
enum class GridPathDirection : std::uint8_t {
    north = 0,
    east = 1,
    south = 2,
    west = 3
};

struct WorldGridRoute {
    GridPathCell start{}, target{};
    std::vector<GridPathCell> waypoints;
};

inline constexpr std::int32_t grid_route_failure = 1879048192;

struct GridRouteDiagnostics {
    std::uint8_t captured_waypoint_count = 255; //255 is the native no-capture sentinel.
    std::uint32_t captured_step_index{};
    GridPathDirection blocked_direction{};
    std::int8_t detour_side{};
};

//Direction selectors and mode values are distinct from the cell's edge mask.
bool is_world_cell_step_blocked(VisitorWorld&, GridPathCell, GridPathDirection, std::uint32_t navigation_mode);
std::int32_t build_smoothed_world_grid_route(VisitorWorld&,
                                             WorldGridRoute&,
                                             std::uint32_t navigation_mode,
                                             GridRouteDiagnostics* = nullptr);
std::int32_t calculate_world_grid_path_length_between_cells(VisitorWorld&,
                                                            CellId start,
                                                            CellId target,
                                                            std::uint32_t navigation_mode);
bool build_world_navigation_path(VisitorWorld&, MobileNavigationState&, FixedPointVector2 target, bool rebuild = false);
bool local_world_grid_unchanged_since_serial(VisitorWorld&, const MapObjectBase&, const MobileEntityState&);
bool request_mobile_entity_path_to_destination(VisitorWorld&,
                                               MapObjectBase&,
                                               MobileEntityState&,
                                               std::uint16_t x,
                                               std::uint16_t y);
bool set_mobile_entity_destination_and_path(VisitorWorld&,
                                            MapObjectBase&,
                                            MobileEntityState&,
                                            std::uint16_t x,
                                            std::uint16_t y);
bool is_cell_inside_staff_patrol_area(const StaffRuntimeRecord&, CellId);
bool try_set_staff_destination_inside_patrol_area(VisitorWorld&, StaffRuntimeRecord&);
bool try_set_mobile_entity_destination(VisitorWorld&, MapObjectBase&, MobileEntityState&);
void set_mobile_entity_stranded_state(VisitorWorld&, MobileEntityState&, bool);
std::int32_t get_world_navigation_path_progress(const MobileNavigationState&);
bool is_world_navigation_step_unblocked(WorldRuntime&,
                                        std::int32_t source,
                                        std::int32_t candidate,
                                        std::uint32_t navigation_mode);
FixedPointVector2 compute_world_navigation_steering(WorldRuntime&, SteeringBehavior, MobileNavigationState&);
std::int32_t step_world_navigation_state(WorldRuntime&, MobileNavigationState&);
std::uint16_t compute_world_sprite_angle_from_delta(std::int32_t delta_x, std::int32_t delta_y);
void move_world_map_object_to_coordinates(VisitorWorld&, MapObjectBase&, std::uint16_t x, std::uint16_t y);
void reset_mobile_entity_position(VisitorWorld&, MapObjectBase&, MobileEntityState&, std::uint16_t x, std::uint16_t y);
NavigationResult update_mobile_entity_navigation(VisitorWorld&, MapObjectBase&, MobileEntityState&);
} //namespace simcoaster
