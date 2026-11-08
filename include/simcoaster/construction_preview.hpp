#pragma once
#include "mesh_scene.hpp"
#include "park_construction.hpp"

namespace simcoaster {
//Native 0x689880/0x6896C0. Coordinates stay in native world space; only
//preview modes below16 receive the supplied smoothed cursor displacement.
//width selects the native10/20-unit extent; vertical faces remain10 high.
std::array<SceneVertex, 4> construction_preview_quad(WorldRuntime&, const ConstructionFace&, float animation_phase, float camera_heading, std::array<float, 2> coordinate_offset = {});
//Exact material ordering from world_dynamic_face_materials at0x9D6A98.
std::string_view construction_preview_texture(std::uint32_t material);
void append_construction_preview(WorldRuntime&, MeshScene&, std::span<const ConstructionFace>, MeshTextureLibrary&, const MeshLighting&, float animation_phase, float camera_heading, std::array<float, 2> coordinate_offset = {});
} //namespace simcoaster
