#include "simcoaster/entity_identity.hpp"
#include "simcoaster/ui_object_preview.hpp"
#include "simcoaster/mesh_scene.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/simulation_sine.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_research.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace simcoaster {
namespace {
std::uint32_t index(std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("Park-object UI preview index capacity");
    return static_cast<std::uint32_t>(value);
}

std::string lower(std::string value) {
    for (auto& unit : value)
        if (unit >= 'A' && unit <= 'Z')
            unit = static_cast<char>(unit + ('a' - 'A'));
    return value;
}

std::array<std::optional<std::size_t>, 2> sign_slots(const MeshModel& model) {
    std::array<std::optional<std::size_t>, 2> slots;
    unsigned count{};
    for (std::size_t i = 0; i < model.textures.size(); ++i) {
        const auto& slot = model.textures[i];
        if (slot.metadata_flags & 8u) {
            slots[0] = i;
            ++count;
        } else if (slot.metadata_flags & 16u) {
            slots[1] = i;
            ++count;
        } else if (!(slot.metadata_flags & 2u) && slot.filenames.size() <= 1) {
            const auto name = lower(slot.filename);
            if (name == "sign1.tga" && !slots[0]) {
                slots[0] = i;
                ++count;
            } else if (name == "sign2.tga" && !slots[1]) {
                slots[1] = i;
                ++count;
            }
        }
    }
    return count == 2 ? slots : std::array<std::optional<std::size_t>, 2>{};
}

void borrow_sign_bindings(MeshModel& preview, const MeshModel& source) {
    const auto original = sign_slots(source), replacement = sign_slots(preview);
    if (!original[0] || !original[1] || !replacement[0] || !replacement[1])
        return;
    //4CC0E0 changes triangle-group bindings, not animated slot contents. A
    //private copy of each original source slot gives the same ownership without
    //native cross-instance resource pointers or a reverse-swap destructor.
    for (std::size_t bank = 0; bank < 2; ++bank) {
        const auto appended = preview.textures.size();
        preview.textures.push_back(source.textures[*original[bank]]);
        for (auto& node : preview.nodes)
            if (node.drawable)
                for (auto& group : node.drawable->groups)
                    if (group.texture_slot == replacement[bank])
                        group.texture_slot = appended;
    }
}

float sine(float radians) {
    return simulation_sine_at_index(static_cast<std::int32_t>(radians * simulation_sine_samples_per_radian));
}

void rotate_y(MeshMatrix& matrix, float cosine, float sin) {
    //4DA6B0 postmultiplies all four rows, including translation.
    for (std::size_t row = 0; row < 4; ++row) {
        const auto at = row * 4;
        const auto x = matrix[at], z = matrix[at + 2];
        matrix[at] = x * cosine + z * sin;
        matrix[at + 2] = z * cosine - x * sin;
    }
}

void rotate_x(MeshMatrix& matrix, float cosine, float sin) {
    //4DA4F0: M=M*Ry*Rx; no World camera or perspective crop participates.
    for (std::size_t row = 0; row < 4; ++row) {
        const auto at = row * 4;
        const auto y = matrix[at + 1], z = matrix[at + 2];
        matrix[at + 1] = y * cosine - z * sin;
        matrix[at + 2] = y * sin + z * cosine;
    }
}

MeshPoint direction(const MeshMatrix& matrix, const MeshPoint& normal) {
    MeshPoint result;
    for (std::size_t axis = 0; axis < 3; ++axis)
        result[axis] = normal[0] * matrix[axis] + normal[1] * matrix[axis + 4] + normal[2] * matrix[axis + 8];
    return result;
}
} //namespace

struct UiObjectPreview::State {
    WorldRuntime& c;
    std::filesystem::path root;
    const std::thread::id thread = std::this_thread::get_id();
    const World* world{};
    const WorldMeshRuntime* meshes{};
    const ThingHeader* identity{}; //Compare addresses only; never dereference.
    const ResearchCatalogDefinition* definition_identity{};
    bool definition_only{};
    ThingId id{};
    std::uint64_t creation_time{};
    std::uint16_t definition_id{};
    std::uint32_t template_id{}, animation_id = 5;
    std::shared_ptr<const MeshPlaybackAssets> source_assets;
    std::optional<MeshPlaybackInstance> playback;
    std::optional<MeshModelLibrary> library;
    std::unique_ptr<MeshTextureLibrary> textures;
    MeshLighting lighting;
    MeshBounds model_bounds{};
    MeshMatrix unscaled_root{}, fitted_root{};
    UiRect rectangle{};
    bool fitted{}, reset_clock = true;
    std::uint32_t last_tick{};
    float rotation{}, render_rotation{}, half_x{}, half_z{}, midpoint_distance{}, vertical_ratio{};
    MeshScene scene;
    std::vector<std::string> diagnostics;

    State(WorldRuntime& context, const std::filesystem::path& path) : c(context), root(path) {}

    void require_thread() const {
        if (thread != std::this_thread::get_id())
            throw std::logic_error("Park-object previews require their owning UI thread");
    }

    ParkObjectRuntimeView* object(ThingId candidate) const {
        return find_live_entity<ParkObjectRuntimeView, true>(c.entities, candidate);
    }

    void clear() {
        id = 0;
        identity = nullptr;
        definition_identity = nullptr;
        definition_only = false;
        world = nullptr;
        meshes = nullptr;
        playback.reset();
        source_assets.reset();
        textures.reset();
        library.reset();
        scene.clear();
        fitted = false;
        reset_clock = true;
        //Shared native preview storage deliberately preserves its spin phase
        //across replacement/release; no World mesh/resource slot remains owned.
    }

    bool reconcile() {
        if (definition_only) {
            if (!world || world != c.referenced_world() || !meshes || meshes != c.resources.meshes) {
                clear();
                return false;
            }
            const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, definition_id);
            const auto* source = meshes->definition(template_id);
            if (!definition
                || definition != definition_identity
                || !definition->preview_mesh_template_loaded
                || definition->preview_mesh_template_id != template_id
                || !source
                || (source->alternate ? source->alternate : source->assets) != source_assets) {
                clear();
                return false;
            }
            return true;
        }
        const auto* current = EntityIdentity { id, identity, creation_time }.get<ParkObjectRuntimeView, true>(c.entities);
        if (!current || !world || world != c.referenced_world() || !meshes || meshes != c.resources.meshes || current->definition_id != definition_id) {
            clear();
            return false;
        }
        const auto* definition = get_park_object_definition(c, current);
        const auto* source = meshes->definition(template_id);
        if (!definition
            || definition->preview_mesh_template_id != template_id
            || !source
            || (source->alternate ? source->alternate : source->assets) != source_assets) {
            clear();
            return false;
        }
        return true;
    }

    void prepare(const WorldMeshTemplate& source) {
        source_assets = source.alternate ? source.alternate : source.assets;
        if (!source_assets || !source.assets)
            throw std::logic_error("Park-object preview template has no model owner");
        auto flags = (source.runtime_flags & ~1u) | 4u;
        if (source.alternate)
            flags &= ~2u;
        auto clone = create_mesh_playback(source_assets, source.alternate ? 1 : source.control_count, flags, false);
        if (!clone)
            throw std::runtime_error(describe_mesh_error(clone.error()));
        playback = std::move(*clone);
        if (!playback->model.root)
            throw std::runtime_error("Original park-object preview has no model root");
        if (source.alternate)
            borrow_sign_bindings(playback->model, source.assets->model);
        const auto embedded = initialize_mesh_resource_nodes(playback->model);
        if (!embedded)
            throw std::runtime_error(describe_mesh_error(embedded.error()));
        if (source_assets->animations.find(0)) {
            const auto start = request_mesh_playback(*playback, 0, { { 0, 0 }, 0, 1 }, c.resources.meshes->clocks);
            if (!start)
                throw std::runtime_error(describe_mesh_error(start.error()));
            const auto hold = request_mesh_playback(*playback, 0, { { 13, 0 }, 0, 1 }, c.resources.meshes->clocks);
            if (!hold)
                throw std::runtime_error(describe_mesh_error(hold.error()));
        }
        unscaled_root = playback->model.nodes[*playback->model.root].local_transform;
        model_bounds = playback->model.bounds;
        std::string directory = source.source_directory;
        while (!directory.empty() && (directory.back() == '/' || directory.back() == '\\'))
            directory.pop_back();
        auto path = std::filesystem::path{ directory };
        if (!std::filesystem::is_directory(path) && !std::filesystem::is_regular_file(path))
            path += ".wad";
        auto opened = open_mesh_model_library(path);
        if (!opened)
            throw std::runtime_error(describe_mesh_error(opened.error()));
        library = std::move(*opened);
        textures = std::make_unique<MeshTextureLibrary>(*library,
                                                        root,
                                                        c.source.selected_resource_source_name,
                                                        std::filesystem::path{},
                                                        c.resources.meshes->texture_set(),
                                                        c.resources.meshes->animated_textures());
        lighting = load_installed_mesh_lighting(root, c.source.selected_resource_source_name);
        const auto requested =
            request_mesh_playback(*playback,
                                  0,
                                  { { animation_id, 0 }, std::to_underlying(MeshPlaybackTransitionFlag::loop), 1 },
                                  c.resources.meshes->clocks);
        if (!requested)
            throw std::runtime_error(describe_mesh_error(requested.error()));
        rotation = static_cast<float>(std::fmod(rotation, 6.283185482025146));
        reset_clock = true;
        fitted = false;
    }

    void fit(UiRect area) {
        const auto dx = model_bounds.maximum[0] - model_bounds.minimum[0];
        const auto dz = model_bounds.maximum[2] - model_bounds.minimum[2];
        const auto y = model_bounds.maximum[1];
        const auto diagonal = std::sqrt(dx * dx * .25f + dz * dz * .25f);
        const auto midpoint = std::sqrt(dx * dx * .25f + y * y + dz * dz * .25f);
        const auto extent = std::sqrt(dx * dx + y * y + dz * dz);
        if (!(diagonal > 0) || !(extent > 0) || !std::isfinite(diagonal) || !std::isfinite(extent))
            throw std::runtime_error("Original park-object preview has degenerate bounds");
        const auto width = static_cast<float>(area.width()) * .0009765625f;
        const auto height = static_cast<float>(area.height()) * .0013020834f;
        const auto scale = std::min(height / extent, width * .66666669f / diagonal);
        fitted_root = unscaled_root;
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t axis = 0; axis < 3; ++axis)
                fitted_root[row * 4 + axis] *= scale;
        half_x = dx * scale * .5f;
        half_z = dz * scale * .5f;
        midpoint_distance = midpoint * scale;
        vertical_ratio = midpoint / extent / (diagonal / extent + midpoint / extent);
        rectangle = area;
        fitted = true;
    }

    void advance() {
        if (!reconcile())
            return;
        const auto now = c.ports.clock.get_simulation_clock_time_ms(c);
        if (!reset_clock && now == last_tick)
            return;
        const auto result = advance_mesh_playback(*playback, c.resources.meshes->clocks);
        if (!result)
            throw std::runtime_error(describe_mesh_error(result.error()));
        if (reset_clock) {
            last_tick = now;
            reset_clock = false;
        }
        const auto elapsed = now - last_tick;
        last_tick = now;
        //4CC620 renders the current phase, then advances it for the next update.
        render_rotation = rotation;
        rotation = static_cast<float>(static_cast<double>(elapsed) * .00039999999 + rotation);
    }
};

UiObjectPreview::UiObjectPreview(WorldRuntime& c, const std::filesystem::path& root)
    : state_(std::make_unique<State>(c, root)) {}

UiObjectPreview::~UiObjectPreview() = default;

bool UiObjectPreview::bind(ThingId id, std::uint32_t animation_id) {
    auto& s = *state_;
    s.require_thread();
    const auto* object = s.object(id);
    if (!object) {
        s.clear();
        return false;
    }
    if (s.id == id && s.animation_id == animation_id && s.reconcile())
        return true;
    s.clear();
    if (!s.c.resources.meshes)
        throw std::logic_error("Park-object preview requires the World mesh owner");
    const auto* definition = get_park_object_definition(s.c, object);
    if (!definition || !definition->preview_mesh_template_loaded)
        throw std::runtime_error("Park-object preview definition has not loaded its model");
    const auto* source = s.c.resources.meshes->definition(definition->preview_mesh_template_id);
    if (!source)
        throw std::runtime_error("Park-object preview model template is missing");
    s.world = s.c.referenced_world();
    s.meshes = s.c.resources.meshes;
    s.id = id;
    s.identity = &object->base.header;
    s.creation_time = object->creation_file_time;
    s.definition_id = object->definition_id;
    s.template_id = definition->preview_mesh_template_id;
    s.animation_id = animation_id;
    try {
        s.prepare(*source);
    } catch (...) {
        s.clear();
        throw;
    }
    return true;
}

bool UiObjectPreview::bind_definition(std::uint16_t id, std::uint32_t animation_id) {
    auto& s = *state_;
    s.require_thread();
    if (s.definition_only && s.definition_id == id && s.animation_id == animation_id && s.reconcile())
        return true;
    s.clear();
    const auto* definition = id ? find_object_definition_by_id(&s.c.resources.object_catalog, id) : nullptr;
    if (!definition || !s.c.referenced_world())
        return false;
    if (!s.c.resources.meshes)
        throw std::logic_error("Definition preview requires the World mesh owner");
    if (!definition->preview_mesh_template_loaded)
        throw std::runtime_error("Definition preview model has not been loaded");
    const auto* source = s.c.resources.meshes->definition(definition->preview_mesh_template_id);
    if (!source)
        throw std::runtime_error("Definition preview model template is missing");
    s.world = s.c.referenced_world();
    s.meshes = s.c.resources.meshes;
    s.definition_identity = definition;
    s.definition_only = true;
    s.definition_id = id;
    s.template_id = definition->preview_mesh_template_id;
    s.animation_id = animation_id;
    try {
        s.prepare(*source);
    } catch (...) {
        s.clear();
        throw;
    }
    return true;
}

void UiObjectPreview::clear() {
    state_->require_thread();
    state_->clear();
}

void UiObjectPreview::retire(ThingId id) {
    state_->require_thread();
    if (state_->id == id)
        state_->clear();
}

bool UiObjectPreview::reconcile() {
    state_->require_thread();
    return state_->reconcile();
}

void UiObjectPreview::advance() {
    state_->require_thread();
    state_->advance();
}

const std::vector<std::string>& UiObjectPreview::diagnostics() const {
    state_->require_thread();
    return state_->diagnostics;
}

void UiObjectPreview::append(UiDrawList& output,
                             UiControl& control,
                             UiRect clip,
                             std::uint32_t pixel_width,
                             std::uint32_t pixel_height,
                             std::int32_t depth_offset) {
    auto& s = *state_;
    s.require_thread();
    if (!pixel_width || !pixel_height)
        throw std::invalid_argument("Park-object preview viewport is zero");
    if (!control.runtime || !control.runtime->owns(&control) || !control.visible() || !s.reconcile())
        return;
    const auto area = control.attachment_rect;
    clip = { std::max({ clip.left, area.left, 0 }),
             std::max({ clip.top, area.top, 0 }),
             std::min({ clip.right, area.right, 2048 }),
             std::min({ clip.bottom, area.bottom, 1536 }) };
    if (area.width() <= 0 || area.height() <= 0 || clip.width() <= 0 || clip.height() <= 0)
        return;
    if (!s.fitted
        || area.left != s.rectangle.left
        || area.top != s.rectangle.top
        || area.right != s.rectangle.right
        || area.bottom != s.rectangle.bottom)
        s.fit(area);
    s.advance();
    auto transform = s.fitted_root;
    const auto sin = sine(s.render_rotation), cosine = sine(s.render_rotation + 1.5707964f);
    rotate_y(transform, cosine, sin);
    rotate_x(transform, sine(.78539819f), sine(-.78539819f));
    const auto panel_x = static_cast<float>(area.left - 1024) * .0009765625f;
    const auto panel_y = static_cast<float>(768 - area.bottom) * .0013020834f;
    const auto panel_width = static_cast<float>(area.width()) * .0009765625f;
    const auto panel_height = static_cast<float>(area.height()) * .0013020834f;
    const auto corner_x = -s.half_z * sin - s.half_x * cosine;
    transform[12] += panel_width * .5f + corner_x * .75f + panel_x;
    transform[13] += panel_height * .5f
                   + panel_y
                   - s.midpoint_distance * s.vertical_ratio * .34999999f
                   + (-s.half_z * cosine + s.half_x * sin) * .69999999f;
    for (std::size_t row = 0; row < 4; ++row)
        transform[row * 4 + 2] *= .1f;
    for (std::size_t row = 0; row < 3; ++row)
        transform[row * 4] *= .75f;
    auto& model = s.playback->model;
    model.nodes[*model.root].local_transform = transform;
    const auto poses = evaluate_mesh_pose(model);
    if (!poses)
        throw std::runtime_error(describe_mesh_error(poses.error()));
    s.scene.clear();
    s.textures->set_animated_textures(s.meshes->animated_textures());
    append_mesh_scene(s.scene, model, *s.textures, s.lighting);
    const auto preview_depth = 1.f - static_cast<float>(control.render_depth + depth_offset) / 40.f;
    std::optional<std::size_t> previous_node;
    std::size_t group_index{};
    for (const auto& draw : s.scene.draws) {
        const auto& node = model.nodes.at(draw.source_node);
        const auto& drawable = *node.drawable;
        if (previous_node != draw.source_node) {
            previous_node = draw.source_node;
            group_index = 0;
        }
        while (group_index < drawable.groups.size() && !drawable.groups[group_index].triangle_count)
            ++group_index;
        if (group_index >= drawable.groups.size())
            throw std::logic_error("Park-object preview source group mismatch");
        const auto& group = drawable.groups[group_index++];
        const auto first_corner = drawable.triangles.at(group.first_triangle).corners[0];
        const auto source_base = s.scene.indices.at(draw.first_index) - first_corner;
        UiDrawCommand command;
        command.texture = draw.texture;
        command.secondary_texture = draw.secondary_texture;
        command.material = draw.material;
        command.depth_buffered = true;
        command.clip = { static_cast<float>(clip.left),
                         static_cast<float>(clip.top),
                         static_cast<float>(clip.right),
                         static_cast<float>(clip.bottom) };
        if (group.texture_slot) {
            const auto& slot = model.textures.at(*group.texture_slot);
            const auto& name = s.textures->filename(slot);
            if (!slot.image_override
                && std::ranges::find(s.scene.missing_textures, name) != s.scene.missing_textures.end()) {
                //Preserve the shared renderer's original NotFound source and its
                //derived material. A recorded lookup miss does not make the slot
                //unbound; the global missing source can itself be textured.
                const auto diagnostic =
                    "Missing original park-object preview texture: " + name + " (native missing-source policy)";
                if (std::ranges::find(s.diagnostics, diagnostic) == s.diagnostics.end())
                    s.diagnostics.push_back(diagnostic);
            }
        }
        command.first_index = index(output.indices.size());
        (void)index(output.vertices.size() + draw.index_count);
        for (std::size_t triangle = 0; triangle < draw.index_count; triangle += 3) {
            const auto& a = s.scene.vertices.at(s.scene.indices.at(draw.first_index + triangle)).position;
            const auto& b = s.scene.vertices.at(s.scene.indices.at(draw.first_index + triangle + 1)).position;
            const auto& c = s.scene.vertices.at(s.scene.indices.at(draw.first_index + triangle + 2)).position;
            //Native6EF310 rejects det(rows[x,y,w]) <=0 in both6EE050 paths.
            //This independent screen context is affine (w=1). Native4B7280 ->
            //6E2C30 negates context Y BEFORE that test, reversing the determinant.
            //The ordinary UI renderer intentionally has CULL_NONE.
            const auto facing = (static_cast<double>(b[0]) - a[0]) * (static_cast<double>(c[1]) - a[1])
                              - (static_cast<double>(b[1]) - a[1]) * (static_cast<double>(c[0]) - a[0]);
            if (!(facing < 0))
                continue;
            for (std::size_t corner_index = 0; corner_index < 3; ++corner_index) {
                const auto i = triangle + corner_index;
                const auto source_index = s.scene.indices.at(draw.first_index + i);
                const auto& source = s.scene.vertices.at(source_index);
                UiDrawVertex vertex;
                //4CC620 already compresses the posed model's Z column by0.1.
                //4CC0E0 adds the selector depth in the independent screen context;
                //model flag200 selects mode2 at6E35D3, whose final projection6E16C0
                //scales the complete resulting Z by0.1 again.
                vertex.position = { (source.position[0] + 1.f) * 1024.f,
                                    (1.f - source.position[1]) * 768.f,
                                    (source.position[2] + preview_depth) * .1f };
                vertex.uv = source.uv; //World model convention, not the UI MD2 adapter.
                vertex.color = draw.unlit ? draw.diffuse_mask : source.diffuse;
                if (command.material.generated_uv || command.material.secondary_texture) {
                    const auto corner = source_index - source_base;
                    const auto position = drawable.position_indices.at(corner);
                    const auto normal =
                        position < drawable.normals.size() ? drawable.normals[position] : MeshPoint{ 0, -1, 0 };
                    const auto view_normal = direction((*poses)[draw.source_node], normal);
                    auto view_position = source.position;
                    view_position[2] += preview_depth;
                    vertex.reflection_uv = mesh_reflection_uv(view_position, view_normal);
                    if (command.material.generated_uv)
                        vertex.uv = vertex.reflection_uv;
                }
                output.indices.push_back(index(output.vertices.size()));
                output.vertices.push_back(vertex);
            }
        }
        command.index_count = index(output.indices.size()) - command.first_index;
        if (command.index_count)
            output.commands.push_back(std::move(command));
    }
}
} //namespace simcoaster
