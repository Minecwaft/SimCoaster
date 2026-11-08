#include "simcoaster/construction_preview.hpp"
#include "simcoaster/world.hpp"
#include <algorithm>
#include <limits>
#include <numeric>

namespace simcoaster {
void append_construction_preview(WorldRuntime& c, MeshScene& scene, std::span<const ConstructionFace> faces, MeshTextureLibrary& textures, const MeshLighting& lighting, float phase, float heading, std::array<float, 2> offset) {
    if (faces.size() > 2054) {
        throw std::out_of_range("construction preview face capacity");
    }
    //Native packed keys sort by material, preserving insertion within a mode.
    std::vector<std::size_t> order(faces.size());
    std::iota(order.begin(), order.end(), std::size_t {});
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
        return faces[a].material < faces[b].material;
    });
    //Native BlueprintMesh has1536 quads even though the shared input queue
    //also serves other dynamic meshes and holds2054 records.
    if (order.size() > 1536) {
        order.resize(1536);
    }
    std::optional<SceneDraw> batch;
    std::uint32_t batch_material {};
    const auto finish_batch = [&] {
        if (!batch) {
            return;
        }
        const auto count = static_cast<float>(batch->index_count / 6u);
        for (auto& axis : batch->center) {
            axis /= count;
        }
        scene.draws.push_back(std::move(*batch));
        batch.reset();
    };
    for (const auto index : order) {
        const auto& face = faces[index];
        auto vertices = construction_preview_quad(c, face, phase, heading, offset);
        if (scene.vertices.size() > std::numeric_limits<std::uint32_t>::max() - 4u || scene.indices.size() > std::numeric_limits<std::uint32_t>::max() - 6u) {
            throw std::overflow_error("construction preview scene size");
        }
        const auto first_vertex = static_cast<std::uint32_t>(scene.vertices.size());
        if (!batch || batch_material != face.material) {
            finish_batch();
            batch.emplace();
            batch_material = face.material;
            batch->first_index = static_cast<std::uint32_t>(scene.indices.size());
            const auto name = construction_preview_texture(face.material);
            MeshTextureSlot slot;
            //Native45C779/78C/79F maps descriptor7 to runtime1|2|0x30.
            slot.flags = 0x33;
            slot.filename = name;
            const auto image = resolve_mesh_texture(scene, textures, slot);
            batch->texture = image ? image : untextured_mesh_texture();
            //Native6E237B marks every BlueprintMesh triangle group two-sided.
            batch->material = derive_mesh_material(0, 2, &slot, image.get());
        }
        for (auto& vertex : vertices) {
            vertex.diffuse = mesh_vertex_lighting(lighting, vertex.position, vertex.normal);
            scene.vertices.push_back(vertex);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                scene.bounds.minimum[axis] = std::min(scene.bounds.minimum[axis], vertex.position[axis]);
                scene.bounds.maximum[axis] = std::max(scene.bounds.maximum[axis], vertex.position[axis]);
            }
        }
        for (const auto corner : std::array<std::uint32_t, 6> { 1, 0, 3, 0, 2, 3 }) {
            scene.indices.push_back(first_vertex + corner);
        }
        batch->index_count += 6;
        for (const auto& vertex : vertices) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                batch->center[axis] += vertex.position[axis] * .25f;
            }
        }
    }
    finish_batch();
}
} //namespace simcoaster
