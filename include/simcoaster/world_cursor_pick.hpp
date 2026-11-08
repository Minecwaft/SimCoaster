#pragma once
#include "mesh_scene.hpp"
#include "world_staff_interaction.hpp"
#include <optional>

namespace simcoaster {
struct WorldRuntime;

struct WorldCursorMeshHit {
    std::uint32_t mesh_slot {};
    std::array<float, 3> position {}, normal {};
};

struct WorldCursorPick {
    std::array<float, 3> position {}, normal {};
    //Zero when terrain wins the native terrain/mesh distance comparison.
    std::uint32_t surface_mesh_slot {};
    bool terrain_intersection_found {};
    //Native4B7BA0 retains its first mesh candidate independently of which
    //surface wins. Geometry tools consume this candidate's actual mesh slot.
    std::optional<WorldCursorMeshHit> mesh;
};

//Physical client pixels and the actual renderer camera, not logical UI units.
//Uses original grid/candidate/hierarchy order and first accepted triangles.
std::optional<WorldCursorPick> pick_world_cursor(WorldRuntime&, const SceneCamera&, const TerrainGridView&, std::array<std::int32_t, 2> pixel, std::uint32_t width, std::uint32_t height);
//World pointer handlers use the winning surface's bounded map cell and a
//normalized60-unit ray point, independently of the retained mesh candidate.
std::optional<StaffInteractionCursor> pick_world_interaction_cursor(WorldRuntime&, const SceneCamera&, const TerrainGridView&, std::array<std::int32_t, 2> pixel, std::uint32_t width, std::uint32_t height, WorldCursorPick* details = nullptr);
} //namespace simcoaster
