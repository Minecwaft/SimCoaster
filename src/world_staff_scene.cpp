#include "simcoaster/frame_profile.hpp"
#include "simcoaster/world_staff_view.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <utility>

namespace simcoaster {
using Vec = std::array<float, 3>;

namespace {
void
append_cell_overlays(StaffWorld& c, MeshScene& scene, std::span<const StaffInteractionOverlay> overlays, bool held) {
    if (overlays.empty())
        return;
    //Native dynamic-face material slots0/1/9 name these installed textures.
    //The viewer owns GPU submission; logical mode selection stays in the
    //runtime.
    static thread_local std::map<std::filesystem::path, std::array<TextureImagePtr, 4>> cache;
    const auto directory = std::filesystem::path{ c.source.data_directory } / "generic/dynamic/textures";
    if (!cache.contains(directory)) {
        std::array<TextureImagePtr, 4> loaded;
        constexpr std::array names{ "blue.tga", "red.tga", "patrol.tga", "lightblue.tga" };
        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto path = directory / names[i];
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in)
                throw std::runtime_error("Missing staff overlay texture: " + path.native_encoded_string());
            const auto size = in.tellg();
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            in.seekg(0);
            if (!in.read(reinterpret_cast<char*>(bytes.data()), size))
                throw std::runtime_error("Cannot read staff overlay texture");
            auto image = decode_tga_image(bytes);
            if (!image)
                throw std::runtime_error(describe_texture_error(image.error()));
            loaded[i] = std::make_shared<TextureImage>(std::move(*image));
        }
        cache.emplace(directory, std::move(loaded));
    }
    for (const auto& overlay : overlays) {
        if (!overlay.cell || overlay.cell > map_cell_count)
            continue;
        const auto n = static_cast<std::uint32_t>(overlay.cell) - 1u;
        const auto x = held ? c.staff.interaction.cursor.ground_point[0] - 5.f : static_cast<float>(n & 127u) * 10.f,
                   z = held ? c.staff.interaction.cursor.ground_point[2] - 5.f : static_cast<float>(n >> 7) * 10.f;
        SceneDraw draw;
        draw.first_index = static_cast<std::uint32_t>(scene.indices.size());
        draw.index_count = 6;
        const auto slot = overlay.material == 0 ? 0u
                        : overlay.material == 1 ? 1u
                        : overlay.material == 8 ? 3u
                        : overlay.material == 9 ? 2u
                                                : throw std::out_of_range("staff overlay material");
        draw.texture = cache.at(directory)[slot];
        draw.unlit = true;
        const MeshTextureSlot texture_slot{};
        draw.material = derive_mesh_material(2, 0, &texture_slot, draw.texture.get());
        draw.center = { x + 5, 0, z + 5 };
        const auto first = static_cast<std::uint32_t>(scene.vertices.size());
        for (auto corner :
             std::array<Vec, 4>{ { { x, 0, z }, { x, 0, z + 10 }, { x + 10, 0, z + 10 }, { x + 10, 0, z } } }) {
            SceneVertex vertex;
            vertex.position = {
                corner[0],
                static_cast<float>(world_operations::sample_terrain_surface_height(c, corner[0], corner[2])) + 1.f,
                corner[2]
            };
            vertex.normal = { 0, 1, 0 };
            const auto at = scene.vertices.size() - first;
            constexpr std::array<std::array<float, 2>, 4> uv{ { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } } };
            vertex.uv = uv.at(at);
            scene.vertices.push_back(vertex);
        }
        for (const auto index : { 0u, 1u, 2u, 0u, 2u, 3u })
            scene.indices.push_back(first + index);
        scene.draws.push_back(std::move(draw));
    }
}
} //namespace

void append_staff_interaction_overlays(StaffWorld& c, MeshScene& scene) {
    const auto kind = world_staff_interaction_kind(c);
    append_cell_overlays(c,
                         scene,
                         c.staff.interaction.overlays,
                         kind == WorldInteractionHandlerKind::held_staff
                             || kind == WorldInteractionHandlerKind::hired_staff);
}

void append_world_runtime_overlays(StaffWorld& c, MeshScene& scene, std::span<const StaffInteractionOverlay> overlays) {
    append_cell_overlays(c, scene, overlays, false);
}

void WorldSpriteScene::load_shadow_texture(const std::filesystem::path& data_directory) {
    auto directory = data_directory.lexically_normal();
    while (!directory.empty() && directory.filename().empty()) {
        const auto parent = directory.parent_path();
        if (parent == directory)
            break;
        directory = parent;
    }
    if (shadow_data_directory_ != directory) {
        shadow_library_.reset();
        shadow_texture_.reset();
        shadow_texture_attempted_ = false;
        shadow_texture_diagnostics_.clear();
        shadow_data_directory_ = directory;
    }
    if (shadow_texture_attempted_)
        return;
    shadow_texture_attempted_ = true;
    if (directory.empty()) {
        shadow_texture_diagnostics_.push_back({ TextureFailure::missing, 0, "data/generic/shadow/alphkid.tga" });
        return;
    }
    //6A2830/6A2860 ->6FD3F0: this is a fixed full-resolution source,
    //independent of theme SharedTex or selected small texture namespaces.
    //The existing provider loads the shipped FSH before a TGA counterpart.
    shadow_library_ = std::make_unique<MeshTextureLibrary>(
        MeshTextureSources{ { nullptr, directory / "Generic/shadow", {} }, {}, false, false },
        directory.parent_path(),
        MeshTextureSet::full,
        false);
    auto loaded = shadow_library_->load("alphkid.tga");
    if (loaded)
        shadow_texture_ = *loaded;
    else {
        //Native shared acquisition uses the real global NotFound source. If
        //that source is also absent, its null result suppresses only Low's
        //override pass; it must never manufacture a silhouette or blob.
        const auto fallback = shadow_library_->missing_texture();
        if (fallback)
            shadow_texture_ = *fallback;
    }
    shadow_texture_diagnostics_ = shadow_library_->diagnostics();
}

void WorldSpriteScene::append(StaffWorld& c,
                              const SceneCamera& camera,
                              float aspect,
                              MeshScene& scene,
                              bool prepare_staff_poses,
                              const WorldSpriteSceneSettings& render_settings) {
    FrameTimer timer{ FrameStage::sprites };
    shadow_geometry_diagnostics_.clear();
    legacy_height_bias_fallback_count_ = 0;
    if (!(aspect > 0) || !std::isfinite(aspect))
        throw std::invalid_argument("sprite scene aspect ratio");
    const auto* catalog = c.resources.variants.catalog;
    if (!catalog)
        return;
    if (prepare_staff_poses)
        for (auto* row = c.entities.active_head; row; row = row->active_next)
            if (row->payload && is_staff_record_kind(row->payload->kind)) {
                auto& staff = *reinterpret_cast<StaffRuntimeRecord*>(row->payload);
                if (staff.mobile.animation.sprite_script)
                    update_mobile_entity_render_pose(c, staff.base, staff.mobile, 1.f);
            }
    if (pixels_.catalog != catalog) {
        pixels_.bind(*catalog);
        textures_.clear();
    }
    const auto camera_pose = camera.explicit_pose ? *camera.explicit_pose : mesh_transform::inverse(camera.view());
    auto first_person_heading = camera.yaw;
    if (c.view.flags & 0x16u) {
        //Native68E450 extracts from view_transform_source, the same
        //camera-to-world matrix built by467930 and inverted for renderer view.
        //4DA320 switches axes near a pitch pole; reading the inverse view, or
        //the requested look yaw alone, loses the selected attachment's yaw/roll.
        first_person_heading = static_cast<float>(
            camera_pose[9] > .89999998f || camera_pose[9] < -.89999998f
                ? std::atan2(-static_cast<double>(camera_pose[2]), static_cast<double>(camera_pose[0]))
                : std::atan2(static_cast<double>(camera_pose[8]), static_cast<double>(camera_pose[10])));
    }
    const ResourceSpritePassSettings pass_settings{ camera.yaw, first_person_heading, 32, 32 };
    if (c.resources.meshes) {
        const auto passengers = c.resources.meshes->prepare_passenger_sprites(c, camera_pose);
        if (!passengers)
            throw std::runtime_error(describe_mesh_error(passengers.error()));
    }
    build_resource_sprite_pass(c, pixels_, pass_, pass_settings);
    //The original sprite kernel uses downward screen Y and a fixed projection.
    //Adapt that coordinate convention to this viewer's adjustable D3D camera.
    auto projection = camera.view_projection(aspect);
    for (const auto i : { 1u, 5u, 9u, 13u })
        projection[i] = -projection[i];
    const auto focal = 1.f / std::tan(camera.vertical_fov * .5f);
    if (!(focal > 0) || !std::isfinite(focal))
        throw std::invalid_argument("sprite scene field of view");
    const auto* grid =
        c.construction.active_terrain_model_data ? c.construction.active_terrain_model_data->terrain_grid : nullptr;
    const SpriteTerrainSampling sampling{ grid,
                                          grid ? static_cast<int>(grid->width_cells) : 0,
                                          grid ? static_cast<int>(grid->height_cells) : 0,
                                          grid ? 1.f / grid->cell_scale_x : 0.f,
                                          grid ? 1.f / grid->cell_scale_y : 0.f,
                                          grid ? static_cast<std::size_t>(grid->width_cells) + 1 : 0 };
    const bool immediate = (c.view.flags & 0x16u) != 0;
    const auto view_height_bias = render_settings.height_bias.value_or(immediate ? .25f : .60000002f);
    //6F1DE0 initializes the native renderer coordinate scale to height/width;
    //6FBC30 also uses that scale for the World-space shadow width.
    const SpriteGeometrySettings settings{ projection,
                                           1.f / aspect,
                                           view_height_bias,
                                           sprite_camera_orientation_turns(camera_pose),
                                           immediate ? 0x2080u : 0u,
                                           true,
                                           &sampling,
                                           focal };
    const auto lookup = [&](SpriteFrameReference frame) -> std::optional<SpriteTextureState> {
        const auto key = std::pair{ frame.resource, frame.frame };
        if (!textures_.contains(key)) {
            const auto decoded = decode_sprite_image(pixels_.payload(frame.resource), frame.frame);
            if (!decoded)
                throw std::runtime_error("Unable to decode sprite frame at byte "
                                         + std::to_string(decoded.error().offset));
            auto image = std::make_shared<TextureImage>();
            image->width = decoded->width;
            image->height = decoded->height;
            bool cutout = false, partial = false;
            for (const auto pixel : decoded->pixels) {
                const auto rgba = mesh_argb_color(pixel);
                cutout |= rgba[3] == 0;
                partial |= rgba[3] != 0 && rgba[3] != 255;
                image->pixels.push_back(rgba);
            }
            image->alpha = partial ? TextureAlpha::translucent : cutout ? TextureAlpha::cutout : TextureAlpha::opaque;
            image->source_mode = partial ? TextureSourceMode::alpha_low
                               : cutout  ? TextureSourceMode::alpha_high
                                         : TextureSourceMode::opaque;
            textures_.emplace(key, std::move(image));
        }
        const auto& texture = textures_.at(key);
        return SpriteTextureState{ static_cast<std::uint32_t>(*texture->source_mode), 0, 0, 0, 1, 1 };
    };
    const auto geometry = build_resource_sprite_geometry(pass_, pixels_, settings, lookup);

    SpriteProjectionSet projections;
    const SpriteShadowPolicy shadow_policy{ render_settings.shadow_quality,
                                            immediate ? SpriteShadowOrder::immediate : SpriteShadowOrder::batched };
    if (shadow_policy.quality != SpriteShadowQuality::off) {
        for (std::size_t index = 0; index < render_settings.lights.size(); ++index) {
            const auto& light = render_settings.lights[index];
            //465580 get_data admits EXACT directional kind2. Point lights and
            //directional-point kind6 do not feed6F47F0, irrespective of colour.
            if (light.kind != MeshLightKind::directional)
                continue;
            const auto admitted = projections.add(light.direction);
            if (admitted == SpriteProjectionAdmission::full)
                break;
            if (admitted == SpriteProjectionAdmission::invalid)
                shadow_geometry_diagnostics_.push_back({ SpriteProjectionFailure::nonfinite, 0, index, 0 });
        }
    }
    SpriteTextureState low_source;
    const SpriteTextureState* low_source_pointer = nullptr;
    const bool low_override = shadow_policy.quality == SpriteShadowQuality::low && !immediate;
    if (low_override && !projections.entries().empty()) {
        load_shadow_texture(std::filesystem::path{ c.source.data_directory });
        if (shadow_texture_) {
            low_source.source_flags =
                static_cast<std::uint32_t>(shadow_texture_->source_mode.value_or(TextureSourceMode::opaque));
            low_source.u_extent = low_source.v_extent = 1;
            low_source_pointer = &low_source;
        }
        for (const auto& diagnostic : shadow_texture_diagnostics_)
            if (std::ranges::find(scene.missing_textures, diagnostic.resource) == scene.missing_textures.end())
                scene.missing_textures.push_back(diagnostic.resource);
    }
    auto shadow_settings = settings;
    shadow_settings.billboard_scale = 1;

    auto& output = scene.sprites;
    const auto append_quad = [&](const SpriteQuad& quad, const TextureImagePtr& texture, WorldFogEligibility fog) {
        if (!texture)
            return;
        if (output.vertices.size() > std::numeric_limits<std::uint32_t>::max() - 4u
            || output.indices.size() > std::numeric_limits<std::uint32_t>::max() - 6u)
            throw std::overflow_error("sprite scene geometry capacity");
        const auto first = static_cast<std::uint32_t>(output.vertices.size());
        const auto color = mesh_argb_color(quad.color);
        const std::array<std::array<float, 2>, 4> uv{ { { quad.uv[0], quad.uv[1] },
                                                        { quad.uv[2], quad.uv[1] },
                                                        { quad.uv[2], quad.uv[3] },
                                                        { quad.uv[0], quad.uv[3] } } };
        for (std::size_t i = 0; i < 4; ++i) {
            auto vertex = quad.vertices[i];
            vertex[1] = -vertex[1];
            EffectVertex value;
            value.position = vertex;
            value.diffuse = color;
            value.uv = uv[i];
            if (fog == WorldFogEligibility::native_geometry)
                value.fog_camera_depth = quad.vertices[i][3];
            output.vertices.push_back(value);
        }
        EffectDraw draw;
        draw.first_index = static_cast<std::uint32_t>(output.indices.size());
        draw.index_count = 6;
        draw.texture = texture;
        draw.material.flags = quad.render_state;
        draw.fog_eligibility = fog;
        for (const auto i : { 0u, 1u, 2u, 0u, 2u, 3u })
            output.indices.push_back(first + i);
        output.draws.push_back(std::move(draw));
    };
    const auto append_base = [&](const SpriteQuad& quad) {
        const auto& command = *pass_.commands.at(quad.handle);
        append_quad(quad,
                    textures_.at({ quad.frame.resource, quad.frame.frame }),
                    command.flags & std::to_underlying(SpriteCommandFlag::explicit_coordinates)
                        ? WorldFogEligibility::excluded
                        : WorldFogEligibility::native_geometry);
    };
    const auto append_shadow = [&](const SpriteQuad& base) {
        if (shadow_policy.quality == SpriteShadowQuality::off
            || projections.entries().empty()
            || (low_override && !low_source_pointer))
            return;
        const auto& command = *pass_.commands.at(base.handle);
        if (!(command.flags & std::to_underlying(SpriteCommandFlag::projected_frames)))
            return;
        const auto source = lookup(command.selected_frame);
        const auto& frame = pixels_.payload(command.selected_frame.resource).frames.at(command.selected_frame.frame);
        //690400,4E3460,4E46A0 never write the malloc-backed+20 field.
        //A ResourceVariant using flag800 therefore has no defined original
        //bias. Use a reported host0 repair; do not invent a script-slot mapping.
        if (command.flags & 0x800u)
            ++legacy_height_bias_fallback_count_;
        const auto shadows = build_sprite_shadow_quads(command,
                                                       frame,
                                                       projections,
                                                       shadow_policy,
                                                       shadow_settings,
                                                       { source ? &*source : nullptr, low_source_pointer },
                                                       0);
        if (!shadows) {
            shadow_geometry_diagnostics_.push_back(shadows.error());
            return;
        }
        for (const auto& shadow : shadows->entries())
            append_quad(shadow.geometry,
                        shadow.source == SpriteShadowSource::alphkid
                            ? shadow_texture_
                            : textures_.at({ shadow.geometry.frame.resource, shadow.geometry.frame.frame }),
                        WorldFogEligibility::excluded);
    };
    //6FD6E0: special views reverse the admitted direct queue and submit
    //each base immediately followed by its projected silhouette. Normal
    //views finish ALL base sprites before beginning any projected shadows.
    if (immediate) {
        for (auto quad = geometry.quads.rbegin(); quad != geometry.quads.rend(); ++quad) {
            append_base(*quad);
            append_shadow(*quad);
        }
    } else {
        for (const auto& quad : geometry.quads)
            append_base(quad);
        for (const auto& quad : geometry.quads)
            append_shadow(quad);
    }
}
} //namespace simcoaster
