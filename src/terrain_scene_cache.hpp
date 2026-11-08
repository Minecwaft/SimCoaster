#pragma once
#include "simcoaster/mesh_scene.hpp"
#include <algorithm>
#include <cmath>

namespace simcoaster {
bool render_bounds_intersect(const MeshBounds&, const MeshMatrix&, const MeshMatrix&);

class TerrainVisibilityCache {
    std::size_t width_ {}, across_ {};
    std::vector<MeshBounds> bounds_;
    std::vector<std::uint8_t> visible_;
    MeshMatrix projection_ {};
    bool valid_ {};

public:
    template <class Corners>
    void rebuild(std::size_t width, std::size_t height, Corners corners) {
        width_ = width;
        across_ = (width + 7) / 8;
        const MeshBounds empty { { INFINITY, INFINITY, INFINITY }, { -INFINITY, -INFINITY, -INFINITY } };
        bounds_.assign(across_ * ((height + 7) / 8), empty);
        visible_.resize(bounds_.size());
        for (std::size_t y = 0; y < height; ++y) {
            for (std::size_t x = 0; x < width; ++x) {
                auto& bounds = bounds_[x / 8 + (y / 8) * across_];
                for (const auto& point : corners(x, y)) {
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        if (!std::isfinite(point[axis])) {
                            bounds = { { -INFINITY, -INFINITY, -INFINITY }, { INFINITY, INFINITY, INFINITY } };
                            break;
                        }
                        bounds.minimum[axis] = std::min(bounds.minimum[axis], point[axis]);
                        bounds.maximum[axis] = std::max(bounds.maximum[axis], point[axis]);
                    }
                }
            }
        }
        valid_ = false;
    }

    void update(const MeshMatrix& projection) {
        if (valid_ && projection_ == projection) {
            return;
        }
        for (std::size_t i = 0; i < bounds_.size(); ++i) {
            visible_[i] = static_cast<std::uint8_t>(render_bounds_intersect(bounds_[i], mesh_identity_matrix, projection));
        }
        projection_ = projection;
        valid_ = true;
    }

    bool admits(std::size_t cell) const {
        return visible_.at((cell % width_) / 8 + ((cell / width_) / 8) * across_) != 0;
    }
};

//World-owned cache, never shared by unrelated terrain models or saved to disk.
class TerrainSceneCache {
    TerrainGridData grid_;
    MeshLighting lighting_;
    bool valid_ {};

public:
    std::vector<MeshPoint> positions, normals;
    std::vector<RgbaPixel> colors;
    TerrainVisibilityCache visibility;
    void prepare(const TerrainGridView&, const MeshLighting&);
};

void append_cached_terrain_scene(MeshScene&, const MeshModel&, MeshTextureLibrary&, const MeshLighting&, bool, const TerrainTextureAtlas*, std::span<const std::uint8_t>, TerrainSceneCache&, const MeshMatrix* view_projection);
//Only emission is culled. Pose/resource/animation producers still run.
void append_mesh_drawables(MeshScene&, const MeshModel&, MeshTextureLibrary&, const MeshLighting&, bool whiten_hover_vertices = false, const MeshMatrix* view_projection = nullptr);
bool render_bounds_intersect(const MeshBounds&, const MeshMatrix& model_to_world, const MeshMatrix& view_projection);
} //namespace simcoaster
