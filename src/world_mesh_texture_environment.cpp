#include "simcoaster/texture_image.hpp"
#include "simcoaster/world_mesh_runtime.hpp"

namespace simcoaster {
namespace {
void require(bool valid, std::string resource = {}) {
    if (!valid) {
        throw MeshError { MeshFailure::configuration, 0, std::move(resource), {} };
    }
}
} //namespace

MeshTextureSet WorldMeshRuntime::texture_set() const {
    return terrain_textures_ ? terrain_textures_->texture_set() : MeshTextureSet::full;
}

bool WorldMeshRuntime::animated_textures() const {
    return !terrain_textures_ || terrain_textures_->animated_textures();
}

void WorldMeshRuntime::configure_terrain_textures(MeshTextureEnvironment& scene) {
    require(scene.ready(), "terrain texture owner");
    terrain_textures_ = &scene;
    terrain_texture_blending = true;
}

void WorldMeshRuntime::prepare_terrain_textures(WorldMeshInstance& instance, const WorldMeshTemplate& source) {
    if (!terrain_texture_blending) {
        return;
    }
    require(terrain_textures_ != nullptr, "terrain texture owner is not configured");
    if (!terrain_textures_->prepare_mesh_base(instance, source, true)) {
        last_error = instance.terrain_texture_error;
    }
}
} //namespace simcoaster
