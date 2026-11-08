#include "terrain_scene_cache.hpp"
#include "mesh_traversal.hpp"
#include "simcoaster/mesh_scene.hpp"
#include "simcoaster/coaster_mesh.hpp"
#include "simcoaster/mesh_transform.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;

void require(bool value) {
    if (!value)
        throw std::runtime_error("Invalid mesh scene geometry");
}

Point transformed(const MeshMatrix& matrix, const Point& point) {
    Point out;
    for (std::size_t axis = 0; axis < 3; ++axis)
        out[axis] = static_cast<float>(static_cast<double>(point[0]) * matrix[axis]
                                       + static_cast<double>(point[1]) * matrix[4 + axis]
                                       + static_cast<double>(point[2]) * matrix[8 + axis]
                                       + matrix[12 + axis]);
    return out;
}

Point direction(const MeshMatrix& matrix, const Point& value) {
    Point result{};
    for (std::size_t axis = 0; axis < 3; ++axis)
        result[axis] = value[0] * matrix[axis] + value[1] * matrix[4 + axis] + value[2] * matrix[8 + axis];
    return result;
}

TextureImagePtr white() {
    static const TextureImagePtr image =
        std::make_shared<TextureImage>(TextureImage{ 1, 1, { { 255, 255, 255, 255 } }, TextureAlpha::opaque });
    return image;
}

TextureImagePtr acquired_texture(MeshScene& scene,
                                 MeshTextureLibrary& textures,
                                 std::expected<TextureImagePtr, TextureError> loaded,
                                 std::string_view name) {
    if (loaded)
        return *loaded;
    if (std::ranges::find(scene.missing_textures, name) == scene.missing_textures.end())
        scene.missing_textures.emplace_back(name);
    //6DA260 substitutes the actual cache fallback even after an invalid source
    //file. 6D8CA0 can itself install a null fallback; retain that native state.
    const auto fallback = textures.missing_texture();
    return fallback ? *fallback : TextureImagePtr{};
}

void include(MeshScene& scene, const Point& point) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        require(std::isfinite(point[axis]));
        scene.bounds.minimum[axis] = std::min(scene.bounds.minimum[axis], point[axis]);
        scene.bounds.maximum[axis] = std::max(scene.bounds.maximum[axis], point[axis]);
    }
}
} //namespace

bool render_bounds_intersect(const MeshBounds& bounds, const MeshMatrix& pose, const MeshMatrix& projection) {
    std::array<double, 16> matrix{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            for (std::size_t k = 0; k < 4; ++k)
                matrix[row * 4 + column] += static_cast<double>(pose[row * 4 + k]) * projection[k * 4 + column];
    unsigned common = 63;
    for (unsigned corner = 0; corner < 8; ++corner) {
        std::array<double, 4> clip{};
        for (std::size_t column = 0; column < 4; ++column) {
            clip[column] = matrix[12 + column];
            for (std::size_t axis = 0; axis < 3; ++axis)
                clip[column] +=
                    (corner & (1u << axis) ? bounds.maximum[axis] : bounds.minimum[axis]) * matrix[axis * 4 + column];
        }
        if (!std::ranges::all_of(clip, [](double value) {
                return std::isfinite(value);
            }))
            return true;
        const auto epsilon = (std::abs(clip[3]) + 1.) * .00001;
        const std::array distances{ clip[3] + clip[0], clip[3] - clip[0], clip[3] + clip[1],
                                    clip[3] - clip[1], clip[2],           clip[3] - clip[2] };
        unsigned outside{};
        for (unsigned plane = 0; plane < distances.size(); ++plane)
            if (distances[plane] < -epsilon)
                outside |= 1u << plane;
        common &= outside;
        if (!common)
            return true;
    }
    return false;
}

TextureImagePtr untextured_mesh_texture() {
    return white();
}

TextureImagePtr resolve_mesh_texture(MeshScene& scene, MeshTextureLibrary& textures, const MeshTextureSlot& slot) {
    return acquired_texture(scene, textures, textures.load(slot), textures.filename(slot));
}

TextureImagePtr resolve_mesh_texture(MeshScene& scene, MeshTextureLibrary& textures, std::string_view name) {
    return acquired_texture(scene, textures, textures.load(name), name);
}

void MeshScene::clear() {
    atmosphere.reset();
    weather.reset();
    sprites.atmosphere.reset();
    sprites.vertices.clear();
    sprites.indices.clear();
    sprites.draws.clear();
    vertices.clear();
    indices.clear();
    draws.clear();
    missing_textures.clear();
    lighting_diagnostics.clear();
    bounds = {
        { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() },
        { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() }
    };
}

std::vector<std::size_t> scene_draw_order(const MeshScene& scene, const MeshPoint& eye) {
    std::vector<std::size_t> order(scene.draws.size());
    std::iota(order.begin(), order.end(), 0);
    const auto distance = [&](const SceneDraw& draw) {
        float squared{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto delta = draw.center[axis] - eye[axis];
            squared += delta * delta;
        }
        return squared;
    };
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
        const auto& first = scene.draws[a];
        const auto& second = scene.draws[b];
        if (first.stage != second.stage)
            return first.stage < second.stage;
        if (first.stage != SceneDrawStage::world)
            return false;
        const bool first_alpha = first.material.blended(), second_alpha = second.material.blended();
        if (first_alpha != second_alpha)
            return !first_alpha;
        return first_alpha ? distance(first) > distance(second) : false;
    });
    return order;
}

void append_mesh_scene(MeshScene& scene,
                       const MeshModel& model,
                       MeshTextureLibrary& textures,
                       const MeshLighting& lighting,
                       const TerrainTextureAtlas* terrain_textures,
                       std::span<const std::uint8_t> excluded_terrain_cells,
                       bool whiten_hover_vertices) {
    if (scene.indices.empty())
        scene.bounds = {
            { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() },
            { -std::numeric_limits<float>::max(),
              -std::numeric_limits<float>::max(),
              -std::numeric_limits<float>::max() }
        };
    if (model.terrain)
        append_terrain_scene(scene, model, textures, lighting, false, terrain_textures, excluded_terrain_cells);
    append_mesh_drawables(scene, model, textures, lighting, whiten_hover_vertices);
}

void append_mesh_drawables(MeshScene& scene,
                           const MeshModel& model,
                           MeshTextureLibrary& textures,
                           const MeshLighting& lighting,
                           bool whiten_hover_vertices,
                           const MeshMatrix* view_projection) {
    const auto poses = evaluate_mesh_pose(model);
    if (!poses)
        throw std::runtime_error(describe_mesh_error(poses.error()));
    std::vector<std::size_t> pending;
    std::vector<bool> visited(model.nodes.size());
    std::size_t roots = 0;
    for (auto root = model.root; root; root = model.nodes[*root].next_sibling) {
        require(*root < model.nodes.size() && roots++ < model.nodes.size());
        pending.push_back(*root);
    }
    std::reverse(pending.begin(), pending.end());
    while (!pending.empty()) {
        const auto index = pending.back();
        pending.pop_back();
        require(index < model.nodes.size() && !visited[index]);
        visited[index] = true;
        const auto& node = model.nodes[index];
        if (node.drawable
            && !(node.flags & 0x8210u)
            && std::ranges::any_of(node.drawable->groups,
                                   [](const auto& group) {
                                       return group.triangle_count != 0;
                                   })
            && (!view_projection
                || render_bounds_intersect(node.drawable->bounds, (*poses)[index], *view_projection))) {
            const auto& drawable = *node.drawable;
            const auto base = scene.vertices.size();
            require(base + drawable.position_indices.size() <= std::numeric_limits<std::uint32_t>::max());
            const bool whiten_colors = whiten_hover_vertices && !(node.flags & 0x4000u);
            std::optional<MeshLighting> local_lights;
            //Native6E38F0 bypasses precolored nodes. Group1 and hover white also
            //bypass computed colors at submission; never invert an unused pose.
            if (!whiten_colors
                && !(node.flags & 0x1000000u)
                && std::max(drawable.world_shadow_colors.size(), drawable.packed_vertex_colors.size())
                       < drawable.position_indices.size()) {
                bool needed = false;
                for (const auto& group : drawable.groups)
                    if (group.triangle_count && !(group.flags & 1u)) {
                        require(static_cast<std::size_t>(group.first_triangle) + group.triangle_count
                                <= drawable.triangles.size());
                        for (std::size_t triangle = group.first_triangle;
                             triangle < static_cast<std::size_t>(group.first_triangle) + group.triangle_count
                             && !needed;
                             ++triangle)
                            for (const auto corner : drawable.triangles[triangle].corners) {
                                require(corner < drawable.position_indices.size());
                                if (corner >= drawable.world_shadow_colors.size()
                                    && corner >= drawable.packed_vertex_colors.size()) {
                                    needed = true;
                                    break;
                                }
                            }
                        if (needed)
                            break;
                    }
                if (needed) {
                    auto transformed_lights = mesh_lighting_in_local_space(lighting, (*poses)[index]);
                    if (transformed_lights)
                        local_lights = std::move(*transformed_lights);
                    else {
                        scene.lighting_diagnostics.push_back(
                            { transformed_lights.error(), model.name, node.name, index, (*poses)[index] });
                        if (transformed_lights.error() == MeshLightingTransformFailure::nonfinite) {
                            std::ostringstream error;
                            error
                                << "Nonfinite mesh lighting transform: model="
                                << model.name
                                << " node="
                                << index
                                << " ("
                                << node.name
                                << ") matrix=";
                            for (const auto value : (*poses)[index])
                                error << value << ',';
                            throw std::runtime_error(error.str());
                        }
                        //Approved repair: native6E03C0/6E0110 has no safe singular
                        //inverse. Keep geometry and ambient/mask for this frame only.
                        local_lights = MeshLighting{ lighting.ambient, {}, lighting.diffuse_mask };
                    }
                }
            }
            for (std::size_t corner = 0; corner < drawable.position_indices.size(); ++corner) {
                const auto vertex = drawable.position_indices[corner];
                require(vertex < drawable.positions.size());
                SceneVertex output;
                output.position = transformed((*poses)[index], drawable.positions[vertex]);
                const auto corner_normal =
                    corner < drawable.normals.size() ? drawable.normals[corner] : Point{ 0, -1, 0 };
                //Reflection uses the POSITION remap to index normals; lighting uses
                //the sequential render-corner normal. Neither uses inverse-transpose.
                output.normal =
                    direction(node.local_transform,
                              vertex < drawable.normals.size() ? drawable.normals[vertex] : Point{ 0, -1, 0 });
                output.reflection_position = transformed(node.local_transform, drawable.positions[vertex]);
                //Native4CDDC0 replaces all eligible render-corner colors with white.
                //Keep immutable assets/normals owned instead of aliasing its scratch.
                output.diffuse = whiten_colors ? mesh_argb_color(0xFFFFFFFFu & lighting.diffuse_mask)
                               : corner < drawable.world_shadow_colors.size()
                                   ? mesh_argb_color(drawable.world_shadow_colors[corner] & lighting.diffuse_mask)
                               : corner < drawable.packed_vertex_colors.size()
                                   ? mesh_argb_color(drawable.packed_vertex_colors[corner] & lighting.diffuse_mask)
                               : local_lights
                                   ? mesh_vertex_lighting(*local_lights, drawable.positions[vertex], corner_normal)
                                   : mesh_argb_color(lighting.diffuse_mask);
                if (corner < drawable.texture_coordinates.size())
                    output.uv = drawable.texture_coordinates[corner];
                scene.vertices.push_back(output);
            }
            for (const auto& group : drawable.groups)
                if (group.triangle_count) {
                    require(static_cast<std::size_t>(group.first_triangle) + group.triangle_count
                            <= drawable.triangles.size());
                    SceneDraw draw;
                    draw.first_index = static_cast<std::uint32_t>(scene.indices.size());
                    draw.node_flags = node.flags;
                    draw.source_node = index;
                    draw.cull_back_faces = true;
                    draw.texture = white();
                    TextureImagePtr source_image;
                    const MeshTextureSlot* slot = nullptr;
                    if (group.texture_slot) {
                        require(*group.texture_slot < model.textures.size());
                        slot = &model.textures[*group.texture_slot];
                        draw.material_flags = slot->flags;
                        source_image =
                            slot->image_override ? slot->image_override : resolve_mesh_texture(scene, textures, *slot);
                        if (source_image)
                            draw.texture = source_image;
                    }
                    if (node.reflection_texture) {
                        require(*node.reflection_texture < 2);
                        draw.secondary_texture = resolve_mesh_texture(
                            scene,
                            textures,
                            *node.reflection_texture == 0 ? "data/sky/skytop.tga" : "data/sky/tableenv.tga");
                    }
                    draw.material = derive_mesh_material(node.flags,
                                                         group.flags,
                                                         slot,
                                                         source_image.get(),
                                                         draw.secondary_texture != nullptr);
                    draw.unlit = (group.flags & 1u) != 0;
                    draw.diffuse_mask = mesh_argb_color(lighting.diffuse_mask);
                    for (std::size_t triangle = group.first_triangle;
                         triangle < static_cast<std::size_t>(group.first_triangle) + group.triangle_count;
                         ++triangle)
                        for (const auto corner : drawable.triangles[triangle].corners) {
                            require(corner < drawable.position_indices.size()
                                    && scene.indices.size() < std::numeric_limits<std::uint32_t>::max());
                            scene.indices.push_back(static_cast<std::uint32_t>(base + corner));
                            include(scene, scene.vertices[base + corner].position);
                            for (std::size_t axis = 0; axis < 3; ++axis)
                                draw.center[axis] += scene.vertices[base + corner].position[axis];
                        }
                    draw.index_count = static_cast<std::uint32_t>(scene.indices.size() - draw.first_index);
                    for (auto& component : draw.center)
                        component /= static_cast<float>(draw.index_count);
                    scene.draws.push_back(std::move(draw));
                }
        }
        if (!(node.flags & 0x20u)) {
            append_mesh_children(model, node, pending, require);
        }
    }
}

void append_coaster_track_scene(MeshScene& scene,
                                const CoasterTrackMesh& mesh,
                                const CoasterMeshDefinition& definition,
                                MeshTextureLibrary& textures,
                                const MeshLighting& lighting,
                                MeshTextureLibrary* diagnostics) {
    require(mesh.texture_indices.size() == definition.textures.size()
            || mesh.texture_indices.size() == definition.textures.size() + coaster_diagnostic_texture_names.size());
    const auto base = scene.vertices.size();
    require(base + mesh.vertices.size() <= std::numeric_limits<std::uint32_t>::max());
    for (const auto& v : mesh.vertices) {
        SceneVertex output;
        output.position = v.position;
        output.normal = v.normal;
        output.reflection_position = v.position;
        output.uv = v.uv;
        output.diffuse = mesh_vertex_lighting(lighting, v.position, v.normal);
        scene.vertices.push_back(output);
    }
    for (std::size_t t = 0; t < mesh.texture_indices.size(); ++t) {
        const auto& indices = mesh.texture_indices[t];
        if (indices.empty())
            continue;
        const bool diagnostic = t >= definition.textures.size();
        MeshTextureSlot slot;
        //46D1D0 creates ordinary track descriptors2 (6 when unlit).
        //45C779..45C79F maps that bit to runtime30 and adds runtime1. These
        //generated strips require wrapping and zero-inset UVs on BOTH axes,
        //including static piping; ordinary model-slot2 clamps scrolling water
        //into a moving patch followed by a stretched border texel. Shared
        //diagnostic descriptors7 additionally map mask1 to runtime2.
        slot.flags = diagnostic ? 0x33u : 0x31u;
        slot.filename = diagnostic ? std::string{ coaster_diagnostic_texture_names.at(t - definition.textures.size()) }
                                   : definition.textures[t].filename;
        auto& library = diagnostic && diagnostics ? *diagnostics : textures;
        const auto texture = resolve_mesh_texture(scene, library, slot);
        SceneDraw draw;
        draw.first_index = static_cast<std::uint32_t>(scene.indices.size());
        draw.cull_back_faces = true;
        draw.texture = texture ? texture : white();
        draw.unlit = diagnostic || definition.textures[t].self_illuminating;
        draw.diffuse_mask = mesh_argb_color(lighting.diffuse_mask);
        draw.material = derive_mesh_material(0, draw.unlit ? 1u : 0u, &slot, texture.get());
        for (const auto index : indices) {
            require(index < mesh.vertices.size() && scene.indices.size() < std::numeric_limits<std::uint32_t>::max());
            const auto& point = mesh.vertices[index].position;
            scene.indices.push_back(static_cast<std::uint32_t>(base + index));
            include(scene, point);
            for (std::size_t axis = 0; axis < 3; ++axis)
                draw.center[axis] += point[axis];
        }
        draw.index_count = static_cast<std::uint32_t>(indices.size());
        for (auto& v : draw.center)
            v /= static_cast<float>(indices.size());
        scene.draws.push_back(std::move(draw));
    }
}
} //namespace simcoaster
