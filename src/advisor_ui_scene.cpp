#include "simcoaster/advisor_ui_scene.hpp"
#include "simcoaster/mesh_scene.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
std::uint32_t index(std::size_t value) {
    if (!std::in_range<std::uint32_t>(value))
        throw std::length_error("Advisor UI geometry exceeds its index domain");
    return static_cast<std::uint32_t>(value);
}

MeshPoint direction(const MeshMatrix& matrix, const MeshPoint& value) {
    MeshPoint result{};
    for (std::size_t axis = 0; axis < 3; ++axis)
        result[axis] = value[0] * matrix[axis] + value[1] * matrix[axis + 4] + value[2] * matrix[axis + 8];
    return result;
}
} //namespace

struct AdvisorUiScene::State {
    struct Resources {
        MeshModelLibrary library;
        MeshTextureLibrary textures;
        std::shared_ptr<const MeshPlaybackAssets> assets;

        static MeshModelLibrary open(const std::filesystem::path& path) {
            auto result = open_mesh_model_library(path);
            if (!result)
                throw std::runtime_error(describe_mesh_error(result.error()));
            return std::move(*result);
        }

        static MeshTextureSources
        sources(const MeshModelLibrary& primary, const std::filesystem::path& secondary, MeshTextureSet quality) {
            MeshTextureSources result;
            const auto name = quality == MeshTextureSet::small ? "stexture" : "textures";
            result.primary = { &primary, {}, name };
            result.secondary = MeshTextureSource{ nullptr, secondary, name };
            return result;
        }

        Resources(const std::filesystem::path& primary,
                  const std::filesystem::path& secondary,
                  const std::filesystem::path& root,
                  MeshTextureSet quality,
                  bool animated,
                  std::shared_ptr<const MeshPlaybackAssets> source)
            : library(open(primary)),
              textures(sources(library, secondary, quality), root, quality, animated),
              assets(std::move(source)) {}
    };

    WorldRuntime& c;
    std::filesystem::path root;
    std::string theme;
    MeshTextureSet quality = MeshTextureSet::full;
    std::array<std::unique_ptr<Resources>, 2> resources;
    MeshScene scene;
    std::vector<std::string> diagnostics;

    State(WorldRuntime& context, const std::filesystem::path& path) : c(context), root(path) {}

    MeshTextureLibrary& textures(std::size_t slot, const MeshPlaybackInstance& mesh) {
        const auto selected = c.resources.meshes ? c.resources.meshes->texture_set() : MeshTextureSet::full;
        if (theme != c.source.selected_resource_source_name || quality != selected) {
            for (auto& resource : resources)
                resource.reset();
            theme = c.source.selected_resource_source_name;
            quality = selected;
        }
        if (theme.empty())
            throw std::logic_error("Advisor texture theme is not bound");
        auto& resource = resources.at(slot);
        const bool animated = !c.resources.meshes || c.resources.meshes->animated_textures();
        if (!resource || resource->assets != mesh.assets) {
            const auto global = root / "Data/global/Advisor";
            const auto local = root / "Data/levels" / theme / "advisor.wad";
            //4B0170/464E50 register reciprocal texture directories: the global
            //mesh borrows theme art, and the theme mesh borrows global art.
            resource = std::make_unique<Resources>(slot ? local : global,
                                                   slot ? global : local,
                                                   root,
                                                   quality,
                                                   animated,
                                                   mesh.assets);
        }
        resource->textures.set_animated_textures(animated);
        return resource->textures;
    }

    void remember(const std::string& message) {
        if (std::ranges::find(diagnostics, message) == diagnostics.end())
            diagnostics.push_back(message);
    }
};

AdvisorUiScene::AdvisorUiScene(WorldRuntime& c, const std::filesystem::path& root)
    : state_(std::make_unique<State>(c, root)) {}

AdvisorUiScene::~AdvisorUiScene() = default;

bool AdvisorUiScene::registered() const {
    return std::ranges::any_of(state_->c.advisor.presentation.meshes, [](const auto& mesh) {
        return mesh && mesh->registered;
    });
}

const std::vector<std::string>& AdvisorUiScene::diagnostics() const {
    return state_->diagnostics;
}

void AdvisorUiScene::append(UiDrawList& output, const MeshLighting& baseline) {
    auto& s = *state_;
    if (!registered())
        return;
    MeshLighting lighting;
    lighting.ambient = baseline.ambient;
    lighting.diffuse_mask = baseline.diffuse_mask;
    //Both light contexts remain registered until slot asset release, even
    //while their corresponding animation is stopped. 6E38F0 excludes World
    //lights for screen-space meshes but retains these category1 directionals.
    for (const auto& mesh : s.c.advisor.presentation.meshes)
        if (mesh) {
            MeshLight light;
            light.direction = { 26.666668f, -26.666668f, 800.f };
            lighting.lights.push_back(light);
        }
    for (std::size_t slot = 0; slot < s.c.advisor.presentation.meshes.size(); ++slot) {
        const auto& mesh = s.c.advisor.presentation.meshes[slot];
        if (!mesh || !mesh->registered)
            continue;
        if (!mesh->assets)
            throw std::logic_error("Registered Advisor mesh has no assets");
        auto& textures = s.textures(slot, *mesh);
        s.scene.clear();
        //The live model already contains this frame's animation, mouth and award
        //substitutions. Do not advance it, refit its bounds or alter its root.
        //Matrix050 is identity in the native screen context: lighting operates
        //on the posed model before the independent screen placement matrix010.
        append_mesh_scene(s.scene, mesh->model, textures, lighting);
        const auto poses = evaluate_mesh_pose(mesh->model);
        if (!poses)
            throw std::runtime_error(describe_mesh_error(poses.error()));
        const auto& placement = s.c.advisor.presentation.render_transforms[slot];
        std::optional<std::size_t> previous_node;
        std::size_t group_index{};
        for (const auto& draw : s.scene.draws) {
            const auto& drawable = *mesh->model.nodes.at(draw.source_node).drawable;
            if (previous_node != draw.source_node) {
                previous_node = draw.source_node;
                group_index = 0;
            }
            while (group_index < drawable.groups.size() && !drawable.groups[group_index].triangle_count)
                ++group_index;
            if (group_index >= drawable.groups.size())
                throw std::logic_error("Advisor source triangle group is absent");
            const auto& group = drawable.groups[group_index++];
            const auto first_corner = drawable.triangles.at(group.first_triangle).corners[0];
            const auto source_base = s.scene.indices.at(draw.first_index) - first_corner;
            UiDrawCommand command;
            command.texture = draw.texture;
            command.secondary_texture = draw.secondary_texture;
            command.material = draw.material;
            command.depth_buffered = true;
            command.layer = UiDrawLayer::advisor;
            command.clip = { 0, 0, output.canvas_width, output.canvas_height };
            command.first_index = index(output.indices.size());
            command.index_count = draw.index_count;
            (void)index(output.vertices.size() + draw.index_count);
            for (std::size_t i = 0; i < draw.index_count; ++i) {
                const auto source_index = s.scene.indices.at(draw.first_index + i);
                const auto& source = s.scene.vertices.at(source_index);
                const auto positioned = mesh_transform::point(placement, source.position);
                UiDrawVertex vertex;
                //4B7280/6E2C30 append the screen Y flip; 6E35D3 selects mode2,
                //and 6E16C0 writes Z*0.1. W remains1: no World camera is involved.
                vertex.position = { (positioned[0] + 1.f) * output.canvas_width * .5f,
                                    (1.f - positioned[1]) * output.canvas_height * .5f,
                                    positioned[2] * .1f };
                if (!std::ranges::all_of(vertex.position, [](float value) {
                        return std::isfinite(value);
                    }))
                    throw std::runtime_error("Advisor screen geometry is nonfinite");
                vertex.uv = source.uv;
                vertex.color = draw.unlit ? draw.diffuse_mask : source.diffuse;
                if (command.material.generated_uv || command.material.secondary_texture) {
                    const auto corner = source_index - source_base;
                    const auto position = drawable.position_indices.at(corner);
                    const auto local_normal =
                        position < drawable.normals.size() ? drawable.normals[position] : MeshPoint{ 0, -1, 0 };
                    const auto normal = direction((*poses)[draw.source_node], local_normal);
                    vertex.reflection_uv = mesh_reflection_uv(positioned, normal);
                    if (command.material.generated_uv)
                        vertex.uv = vertex.reflection_uv;
                }
                output.indices.push_back(index(output.vertices.size()));
                output.vertices.push_back(vertex);
            }
            output.commands.push_back(std::move(command));
        }
        for (const auto& name : s.scene.missing_textures)
            s.remember("Missing original Advisor texture: " + name);
        for (const auto& error : textures.diagnostics())
            s.remember(describe_texture_error(error));
    }
}
} //namespace simcoaster
