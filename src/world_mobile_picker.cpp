#include "simcoaster/world_mobile_picker.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/mesh_material.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
bool mobile_kind(ThingKind kind) {
    return kind == ThingKind::person || is_staff_record_kind(kind);
}

struct SpriteSample {
    double u{}, v{}, w{};
};

std::optional<SpriteSample> sample_quad(const SpriteQuad& quad, double x, double y) {
    const std::array<std::array<double, 2>, 4> uv{ { { quad.uv[0], quad.uv[1] },
                                                     { quad.uv[2], quad.uv[1] },
                                                     { quad.uv[2], quad.uv[3] },
                                                     { quad.uv[0], quad.uv[3] } } };
    //Same two triangles as WorldSpriteScene, including mirrored winding and
    //camera-roll rotation. Perspective-correct UVs use the actual quad W.
    for (const auto indices : std::array<std::array<std::size_t, 3>, 2>{ { { 0, 1, 2 }, { 0, 2, 3 } } }) {
        std::array<std::array<double, 2>, 3> p;
        bool valid = true;
        for (std::size_t i = 0; i < 3; ++i) {
            const auto& vertex = quad.vertices[indices[i]];
            if (!(vertex[3] > 0) || !std::isfinite(vertex[3])) {
                valid = false;
                break;
            }
            p[i] = { vertex[0] / double(vertex[3]), vertex[1] / double(vertex[3]) };
        }
        if (!valid)
            continue;
        const auto determinant = (p[1][1] - p[2][1]) * (p[0][0] - p[2][0]) + (p[2][0] - p[1][0]) * (p[0][1] - p[2][1]);
        if (determinant == 0 || !std::isfinite(determinant))
            continue;
        const auto a = ((p[1][1] - p[2][1]) * (x - p[2][0]) + (p[2][0] - p[1][0]) * (y - p[2][1])) / determinant;
        const auto b = ((p[2][1] - p[0][1]) * (x - p[2][0]) + (p[0][0] - p[2][0]) * (y - p[2][1])) / determinant;
        const std::array weights{ a, b, 1. - a - b };
        if (std::ranges::any_of(weights, [](double value) {
                return !(value >= 0);
            }))
            continue;
        SpriteSample sample;
        double reciprocal{}, depth{};
        for (std::size_t i = 0; i < 3; ++i) {
            const auto index = indices[i];
            const auto weight = weights[i] / quad.vertices[index][3];
            reciprocal += weight;
            sample.u += weight * uv[index][0];
            sample.v += weight * uv[index][1];
            depth += weight * quad.vertices[index][2];
        }
        if (!(reciprocal > 0) || depth < 0 || depth > 1)
            continue;
        sample.u /= reciprocal;
        sample.v /= reciprocal;
        sample.w = 1. / reciprocal;
        return sample;
    }
    return {};
}

bool source_pixel_visible(const SpritePixelPayload& payload,
                          std::size_t frame,
                          const SpriteSample& sample,
                          std::uint32_t color) {
    const auto image = decode_sprite_image(payload, frame);
    if (!image)
        throw std::runtime_error("Invalid mobile pick sprite pixels");
    const auto x =
        static_cast<std::size_t>(std::clamp(std::floor(sample.u * image->width), 0., double(image->width - 1)));
    const auto y =
        static_cast<std::size_t>(std::clamp(std::floor(sample.v * image->height), 0., double(image->height - 1)));
    const auto alpha = double(image->pixels[y * image->width + x] >> 24) / 255. * double(color >> 24) / 255.;
    bool cutout = false, partial = false;
    for (const auto pixel : image->pixels) {
        cutout |= (pixel >> 24) == 0;
        partial |= (pixel >> 24) != 0 && (pixel >> 24) != 255;
    }
    const auto mode = partial ? TextureSourceMode::alpha_low
                    : cutout  ? TextureSourceMode::alpha_high
                              : TextureSourceMode::opaque;
    const MeshMaterial material{ sprite_texture_render_state(std::to_underlying(mode), true) };
    return alpha > 0 && alpha >= material.alpha_reference();
}
} //namespace

std::optional<WorldMobilePick> WorldMobilePicker::pick(VisitorWorld& c,
                                                       const SceneCamera& camera,
                                                       const TerrainGridView& terrain,
                                                       std::array<std::int32_t, 2> pixel,
                                                       std::uint32_t width,
                                                       std::uint32_t height) {
    if (!width
        || !height
        || pixel[0] < 0
        || pixel[1] < 0
        || static_cast<std::uint32_t>(pixel[0]) >= width
        || static_cast<std::uint32_t>(pixel[1]) >= height)
        return {};
    if (!c.referenced_world()
        || !c.resources.variants.initialized
        || !c.resources.variants.catalog
        || !c.resources.sprite_resource_rendering_enabled)
        return {};
    const auto* catalog = c.resources.variants.catalog;
    if (pixels_.catalog != catalog)
        pixels_.bind(*catalog);
    const auto aspect = static_cast<float>(width) / static_cast<float>(height);
    const auto camera_pose = camera.explicit_pose ? *camera.explicit_pose : mesh_transform::inverse(camera.view());
    auto special_heading = camera.yaw;
    if (c.view.flags & 0x16u) {
        //68E450/4DA320 use the camera-to-world attachment basis, including its
        //pole branch. Do not replace it with requested yaw or inverted view.
        special_heading = static_cast<float>(
            camera_pose[9] > .89999998f || camera_pose[9] < -.89999998f
                ? std::atan2(-static_cast<double>(camera_pose[2]), static_cast<double>(camera_pose[0]))
                : std::atan2(static_cast<double>(camera_pose[8]), static_cast<double>(camera_pose[10])));
    }
    const ResourceSpritePassSettings pass_settings{ camera.yaw, special_heading, 32, 32 };
    if (c.resources.meshes) {
        const auto passengers = c.resources.meshes->prepare_passenger_sprites(c, camera_pose);
        if (!passengers)
            throw std::runtime_error(describe_mesh_error(passengers.error()));
    }
    build_resource_sprite_pass(c, pixels_, pass_, pass_settings);
    auto projection = camera.view_projection(aspect);
    for (const auto index : { 1u, 5u, 9u, 13u })
        projection[index] = -projection[index];
    const auto focal = 1.f / std::tan(camera.vertical_fov * .5f);
    if (!(focal > 0) || !std::isfinite(focal))
        throw std::invalid_argument("mobile picker field of view");
    const SpriteTerrainSampling sampling{ &terrain,
                                          static_cast<std::int32_t>(terrain.width_cells),
                                          static_cast<std::int32_t>(terrain.height_cells),
                                          1.f / terrain.cell_scale_x,
                                          1.f / terrain.cell_scale_y,
                                          static_cast<std::size_t>(terrain.width_cells) + 1 };
    SpriteGeometrySettings geometry;
    geometry.transform = projection;
    geometry.coordinate_scale = 1.f / aspect;
    geometry.runtime_flags = (c.view.flags & 0x16u) ? 0x2080u : 0u;
    geometry.direct3d_state = true;
    geometry.terrain = &sampling;
    geometry.height_bias = (c.view.flags & 0x16u) ? .25f : .60000002f;
    geometry.orientation_turns = sprite_camera_orientation_turns(camera_pose);
    geometry.billboard_scale = focal;
    //The user approved replacing native6FB8C0's fixed circle and67A040's
    //terrain-neighborhood limit with the drawn person's actual sprite hit.
    //Use physical pixel centres rather than quantizing through logical UI.
    SpritePickState selected{ float((double(pixel[0]) + .5) * 2. / width - 1.),
                              float((double(pixel[1]) + .5) * 2. / height - 1.),
                              {},
                              0 };
    body_owners_.assign(pass_.commands.size(), nullptr);
    for (const auto& slot : c.entities) {
        const auto* record = slot.payload;
        if (!record || record->retirement_pending || !mobile_kind(record->kind))
            continue;
        const auto handle = record->kind == ThingKind::person
                              ? reinterpret_cast<const PersonRuntimeView*>(record)->mobile.animation.sprite_script
                              : reinterpret_cast<const StaffRuntimeRecord*>(record)->mobile.animation.sprite_script;
        if (handle && handle < body_owners_.size())
            body_owners_[handle] = record;
    }
    //Same admission/frame expansion as WorldSpriteScene; only covered front
    //candidates decode alpha. No complete draw list or script tick is needed.
    //Native pickable0x200 and nearest-W priority remain. A nearer unowned
    //effect masks a body only if its own drawn source pixel covers the click.
    const auto visit = [&](std::uint32_t handle) {
        if (handle >= pass_.commands.size() || !pass_.commands[handle])
            throw std::logic_error("Mobile picker sprite queue lost a live command");
        const auto& command = *pass_.commands[handle];
        if (!(command.flags & std::to_underlying(SpriteCommandFlag::projected_frames)))
            return;
        auto billboard = command;
        if (!(billboard.flags
              & (std::to_underlying(SpriteCommandFlag::world_quad)
                 | std::to_underlying(SpriteCommandFlag::explicit_coordinates))))
            billboard.scale *= geometry.billboard_scale;
        if (!prepare_sprite_geometry(billboard, geometry))
            return;
        const auto& frame = pixels_.payload(command.selected_frame.resource).frames.at(command.selected_frame.frame);
        constexpr SpriteTextureState texture{ 0, 0, 0, 0, 1, 1 };
        const auto quad = build_sprite_quad(billboard, frame, &texture, geometry);
        if (!quad)
            return;
        const auto sample = sample_quad(*quad, selected.x, selected.y);
        if (!sample)
            return;
        const auto depth = static_cast<float>(sample->w);
        if (selected.handle && selected.homogeneous_w <= depth)
            return;
        if (!source_pixel_visible(pixels_.payload(command.selected_frame.resource),
                                  command.selected_frame.frame,
                                  *sample,
                                  command.color))
            return;
        selected.handle = handle;
        selected.homogeneous_w = depth;
    };
    for (const auto& region : pass_.regions)
        for (const auto handle : region)
            visit(handle);
    for (const auto handle : pass_.special)
        visit(handle);
    if (!selected.handle)
        return {};
    const auto handle = *selected.handle;
    const auto* resource = find_resource_variant(c, handle);
    if (!resource || !resource->render_enabled)
        return {};
    if (const auto* owner = body_owners_[handle])
        return WorldMobilePick{ owner->id,
                                owner,
                                handle,
                                pass_.commands.at(handle)->selected_frame,
                                selected.homogeneous_w };
    return {};
}
} //namespace simcoaster
