#pragma once

namespace simcoaster {
enum class MeshTextureSet;
struct WorldMeshInstance;
struct WorldMeshTemplate;

struct MeshTextureEnvironment {
    virtual ~MeshTextureEnvironment() = default;
    virtual bool ready() const = 0;
    virtual MeshTextureSet texture_set() const = 0;
    virtual bool animated_textures() const = 0;
    virtual bool prepare_mesh_base(WorldMeshInstance&, const WorldMeshTemplate&, bool force) = 0;
};
} //namespace simcoaster
