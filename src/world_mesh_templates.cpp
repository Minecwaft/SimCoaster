#include "simcoaster/object_catalog.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_research.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
[[noreturn]] void fail(std::size_t index = 0) {
    throw MeshError{ MeshFailure::configuration, index, {}, {} };
}

void require(bool valid, std::size_t index = 0) {
    if (!valid)
        fail(index);
}

bool has(std::uint32_t flags, MeshTemplateLoadFlag bit) {
    return (flags & std::to_underlying(bit)) != 0;
}

std::expected<MeshModelLibrary, MeshError> library_for(std::string_view directory) {
    auto text = std::string{ directory };
    while (!text.empty() && (text.back() == '/' || text.back() == '\\'))
        text.pop_back();
    auto path = std::filesystem::path{ text };
    if (std::filesystem::is_directory(path) || std::filesystem::is_regular_file(path))
        return open_mesh_model_library(path);
    path += ".wad";
    return open_mesh_model_library(path);
}

void mark_materials(MeshModel& model, std::size_t node_index, std::uint16_t bits) {
    require(node_index < model.nodes.size());
    const auto& node = model.nodes[node_index];
    if (!node.drawable || (node.flags & 0x200u))
        return;
    for (const auto& group : node.drawable->groups)
        if (group.texture_slot) {
            require(*group.texture_slot < model.textures.size());
            model.textures[*group.texture_slot].flags |= bits;
        }
}
} //namespace

namespace {
void prepare_assets(MeshPlaybackAssets& assets, std::uint32_t options, std::uint32_t& runtime_flags) {
    auto& model = assets.model;

    struct Effect {
        MeshTemplateLoadFlag option;
        std::uint32_t model, instance, root;
    };

    static constexpr std::array effects{
        Effect{ MeshTemplateLoadFlag::model_animation_mode, 0x800, 0, 0 },
        Effect{ MeshTemplateLoadFlag::always_animate, 0x20, 0x1000000, 0x80800 },
        Effect{ MeshTemplateLoadFlag::surface_geometry, 0x80, 0, 0 },
        Effect{ MeshTemplateLoadFlag::retire_when_complete, 0, 8, 0 },
        Effect{ MeshTemplateLoadFlag::unfiltered_base, 0x100, 0, 0 },
        Effect{ MeshTemplateLoadFlag::model_terrain_mode, 0x20000, 0, 0 },
        Effect{ MeshTemplateLoadFlag::preserve_transition_channels, 0, 0x100000, 0 },
        Effect{ MeshTemplateLoadFlag::pulsing_colors, 0, 0x2000000, 0 },
        Effect{ MeshTemplateLoadFlag::model_render_mode, 0x200000, 0, 0 },
        Effect{ MeshTemplateLoadFlag::head_processing, 0, 0x10000000, 0 },
    };
    for (const auto& effect : effects)
        if (has(options, effect.option)) {
            model.flags |= effect.model;
            runtime_flags |= effect.instance;
            if (effect.root) {
                require(model.root.has_value());
                model.nodes[*model.root].flags |= effect.root;
            }
        }
    if (has(options, MeshTemplateLoadFlag::surface_geometry))
        for (auto& node : model.nodes)
            if (node.drawable)
                node.flags |= 0x20000u;
    //Native normalizes UVs before marking materials used by UV animation.
    if (has(options, MeshTemplateLoadFlag::normalize_uvs))
        normalize_mesh_uvs(model);
    for (const auto& group : assets.animations.groups)
        for (const auto& loaded : group) {
            if (!loaded.animation.entries.empty())
                model.flags |= 0x20u;
            for (const auto& entry : loaded.animation.entries) {
                require(entry.target_node < model.nodes.size());
                if (entry.flags & 0x1000u)
                    model.flags |= 0x2000u;
                if (entry.flags & 0x10000u) {
                    model.nodes[entry.target_node].flags |= 0x80u;
                    mark_materials(model, entry.target_node, 0x8030);
                }
            }
        }
    const auto metadata = initialize_mesh_resource_nodes(model);
    if (!metadata)
        throw metadata.error();
}

void finalize_assets(MeshPlaybackAssets& assets) {
    auto& model = assets.model;
    for (const auto& resource : model.resources)
        if (resource.flags & 0x20000000u)
            model.nodes[resource.node_index].flags |= 0x8000u;
    for (auto& node : model.nodes) {
        if (node.flags & 0x200u)
            node.flags |= 0x80000010u;
        if (node.flags & 0x20040u)
            model.flags |= 0x80u;
        if (node.drawable && !(node.flags & 0x200u))
            for (auto& group : node.drawable->groups)
                if (group.texture_slot && (model.textures[*group.texture_slot].flags & 0x8000u))
                    group.flags |= 2u;
        if (node.first_child) {
            bool ignored = true;
            for (auto child = node.first_child; child; child = model.nodes[*child].next_sibling)
                if (!(model.nodes[*child].flags & 0x200u) || model.nodes[*child].first_child) {
                    ignored = false;
                    break;
                }
            if (ignored)
                node.flags |= 0x20u;
        }
    }
    for (auto& texture : model.textures) {
        if (texture.flags & 0xC0u)
            model.flags |= 0x1000u;
        if (texture.metadata_flags & 1u)
            texture.flags |= 0x40u;
    }
}
} //namespace

std::expected<std::uint32_t, MeshError> WorldMeshRuntime::load_template(WorldRuntime& c,
                                                                        const MeshDefinitionTemplateLoad& args) {
    try {
        last_error.reset();
        auto library = library_for(args.directory);
        if (!library)
            throw library.error();
        const auto options = args.load_options;
        auto loaded =
            read_mesh_playback_assets(*library, args.filename, has(options, MeshTemplateLoadFlag::relative_animation));
        if (!loaded)
            throw loaded.error();
        auto assets = std::make_shared<MeshPlaybackAssets>(std::move(*loaded));
        WorldMeshTemplate source;
        source.definition_key = args.object_definition_key;
        source.control_count = std::max(args.animation_control_count, 1u);
        source.runtime_flags = 1;
        source.load_options = options;
        const auto width = args.footprint_width_override
                             ? args.footprint_width_override
                             : static_cast<std::uint32_t>(args.placement_geometry ? args.placement_geometry->width : 0);
        const auto height =
            args.footprint_height_override
                ? args.footprint_height_override
                : static_cast<std::uint32_t>(args.placement_geometry ? args.placement_geometry->height : 0);
        require(std::in_range<std::int32_t>(width) && std::in_range<std::int32_t>(height));
        source.footprint_width = static_cast<std::int32_t>(width);
        source.footprint_height = static_cast<std::int32_t>(height);
        source.source_directory = std::string{ args.directory };
        source.resource_name = std::string{ args.filename };
        if (args.sign_text_override)
            source.sign_text_override = args.sign_text_override;
        if (args.sign_source_override)
            source.sign_source_override = args.sign_source_override;
        prepare_assets(*assets, options, source.runtime_flags);
        std::shared_ptr<MeshPlaybackAssets> alternate;
        if (has(options, MeshTemplateLoadFlag::companion)) {
            auto companion = read_mesh_playback_assets(*library, "p" + std::string{ args.filename });
            if (companion) {
                alternate = std::make_shared<MeshPlaybackAssets>(std::move(*companion));
                std::uint32_t unused_flags{};
                prepare_assets(*alternate, 0, unused_flags);
            }
        }
        if (has(options, MeshTemplateLoadFlag::procedural_base)) {
            require(c.construction.active_terrain_model_data && c.construction.active_terrain_model_data->terrain_grid);
            auto base = select_mesh_procedural_base(assets->model,
                                                    args.placement_geometry,
                                                    *c.construction.active_terrain_model_data->terrain_grid,
                                                    source.footprint_width,
                                                    source.footprint_height);
            if (!base)
                throw base.error();
            source.terrain_base = std::make_shared<MeshProceduralBase>(std::move(*base));
            apply_mesh_procedural_base_flags(assets->model, *source.terrain_base);
            auto tiles = build_mesh_base_tile_plan(assets->model,
                                                   *source.terrain_base,
                                                   args.placement_geometry,
                                                   *c.construction.active_terrain_model_data->terrain_grid,
                                                   width,
                                                   height);
            if (!tiles)
                throw tiles.error();
            if (!tiles->cells.empty())
                source.terrain_texture_plan = std::make_shared<MeshBaseTilePlan>(std::move(*tiles));
        }
        if (has(options, MeshTemplateLoadFlag::mark_base_materials)) {
            const auto selected = source.terrain_base && source.terrain_base->best_node ? source.terrain_base->best_node
                                                                                        : assets->model.root;
            if (selected)
                mark_materials(assets->model, *selected, 0x800);
            if (alternate && alternate->model.root)
                mark_materials(alternate->model, *alternate->model.root, 0x800);
        }
        if (has(options, MeshTemplateLoadFlag::hoarding) && args.hoarding_layout && source.terrain_base) {
            auto built = build_mesh_hoarding(assets->model,
                                             *source.terrain_base,
                                             *static_cast<const DefinitionGrid*>(args.hoarding_layout),
                                             c.source.selected_resource_source_name);
            if (!built)
                throw built.error();
            if (*built) {
                source.hoarding = std::make_shared<MeshHoardingTemplate>(std::move(**built));
                source.runtime_flags |= 2u;
            }
        }
        if (has(options, MeshTemplateLoadFlag::hide_clone_nodes))
            for (const auto& resource : assets->model.resources)
                if (resource.flags & 0x80u)
                    assets->model.nodes[resource.node_index].flags |= 0x80000010u;
        if (has(options, MeshTemplateLoadFlag::height_map)) {
            auto path = std::filesystem::path{ args.filename };
            path.replace_extension(".hmp");
            const auto bytes = library->read_resource(path.native_encoded_string());
            if (!bytes)
                throw bytes.error();
            std::optional<MeshHeightMap> height_map;
            if (*bytes) {
                auto parsed = parse_mesh_height_map(**bytes);
                if (parsed && parsed->width == width && parsed->height == height)
                    height_map = std::move(*parsed);
            }
            if (!height_map) {
                auto generated = generate_mesh_height_map(assets->model,
                                                          assets->animations,
                                                          width,
                                                          height,
                                                          args.placement_geometry,
                                                          has(options, MeshTemplateLoadFlag::bounds_height));
                if (!generated)
                    throw generated.error();
                height_map = std::move(*generated);
            }
            assets->model.bounds = height_map->bounds;
            source.height_map = std::make_shared<MeshHeightMap>(std::move(*height_map));
        }
        if (sign_texture_generation) {
            const auto first =
                source.sign_text_override
                    ? *source.sign_text_override
                    : (find_object_definition_by_id(&c.resources.object_catalog,
                                                    static_cast<std::uint16_t>(args.object_definition_key))
                           ? get_catalog_object_display_name(c, static_cast<std::uint16_t>(args.object_definition_key))
                           : std::u16string{});
            const auto second = source.sign_text_override && source.sign_source_override
                                  ? std::optional<std::u16string_view>{ *source.sign_source_override }
                                  : std::nullopt;
            const auto rendered = render_model_signs(assets->model, args.directory, args.filename, first, second);
            if (!rendered)
                throw rendered.error();
        }
        finalize_assets(*assets);
        source.assets = std::move(assets);
        source.alternate = std::move(alternate);
        //The optional p-prefixed template has no procedural-base builder in the
        //native load path. Give its geometry an empty plan for creation8's
        //wrapper.
        const auto empty_base = [](const MeshModel& model) {
            auto base = std::make_shared<MeshProceduralBase>();
            base->vertex_indices.resize(model.nodes.size());
            base->secondary_vertex_indices.resize(model.nodes.size());
            return base;
        };
        if (!source.terrain_base)
            source.terrain_base = empty_base(source.assets->model);
        if (source.alternate)
            source.alternate_terrain_base = empty_base(source.alternate->model);
        const auto registered = register_template(std::move(source));
        if (!registered)
            throw registered.error();
        return registered;
    } catch (const MeshError& error) {
        last_error = error;
        if (last_error->resource.empty())
            last_error->resource = std::string{ args.directory } + "/" + std::string{ args.filename };
        return std::unexpected(*last_error);
    }
}

std::uint32_t
world_operations::builtin::load_and_register_mesh_definition_template(WorldRuntime& c,
                                                                      const MeshDefinitionTemplateLoad& args) {
    if (!c.resources.meshes)
        throw std::logic_error("world mesh runtime is not bound");
    const auto result = c.resources.meshes->load_template(c, args);
    return result ? *result : 0;
}
} //namespace simcoaster
