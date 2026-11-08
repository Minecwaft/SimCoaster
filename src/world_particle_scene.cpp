#include "simcoaster/rotation_tables.hpp"
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_particle_scene.hpp"
#include "simcoaster/sprite_geometry.hpp"
#include "simcoaster/world.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
using Position = std::array<std::int32_t, 3>;
using Quad = std::array<std::array<float, 4>, 4>;

Position render_position(const ParticleEffect& effect, const ParticleInstance& particle) {
    auto result = particle.position;
    if (effect.render_position_scale_permille != 1000) {
        for (std::size_t axis = 0; axis < result.size(); ++axis) {
            result[axis] = numeric::add(effect.position[axis], numeric::product(effect.render_position_scale_permille, numeric::subtract(result[axis], effect.position[axis])) / 1000);
        }
    }
    return result;
}

std::int32_t screen_y(std::int32_t z) {
    //Exact signed reciprocal-multiply from657CF9/657D42, including its
    //correction toward zero for a negative numerator.
    const auto numerator = numeric::subtract(numeric::product(z, 16), 37500);
    const auto wide = std::int64_t { 3714566311 } * numerator;
    const auto high = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(wide) >> 32));
    return (high < 0 ? 1 : 0) + (high >> 5);
}

void rotate(Quad& vertices, std::uint16_t rotation) {
    if (!rotation) {
        return;
    }
    //Native707490 quantizes to its256-entry sine table. Retain the float
    //relative-coordinate stores before the final double multiply/add.
    const auto turns = static_cast<float>(rotation * .000015258789);
    const auto cosine_index = static_cast<std::uint8_t>(64u - static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<double>(turns) * -256.)));
    const auto sine_index = static_cast<std::uint8_t>(static_cast<std::int32_t>(static_cast<double>(turns) * 256.));
    const double cosine = sprite_rotation_sines()[cosine_index];
    const double sine = sprite_rotation_sines()[sine_index];
    const auto x = (static_cast<double>(vertices[0][0]) + vertices[1][0]) * .5;
    const auto y = (static_cast<double>(vertices[0][1]) + vertices[2][1]) * .5;
    for (auto& vertex : vertices) {
        const auto dx = static_cast<float>(vertex[0] - x);
        const auto relative_y = static_cast<double>(vertex[1]) - y;
        const auto dy = static_cast<float>(relative_y);
        vertex[0] = static_cast<float>(relative_y * sine + dx * cosine + x);
        vertex[1] = static_cast<float>(cosine * dy - dx * sine + y);
    }
}

TextureImagePtr white() {
    static const TextureImagePtr image = std::make_shared<TextureImage>(TextureImage { 1, 1, { { 255, 255, 255, 255 } }, TextureAlpha::opaque, TextureSourceMode::opaque });
    return image;
}
} //namespace

void WorldParticleScene::append(WorldRuntime& c, const ParticleRuntime& runtime, const SceneCamera& camera, float aspect, MeshScene& scene) {
    append_effects(c, runtime, &camera, aspect, scene.sprites, false);
}

void WorldParticleScene::append_screen(WorldRuntime& c, const ParticleRuntime& runtime, float aspect, EffectScene& scene) {
    append_effects(c, runtime, nullptr, aspect, scene, true);
}

void WorldParticleScene::append_effects(WorldRuntime& c, const ParticleRuntime& runtime, const SceneCamera* camera, float aspect, EffectScene& output, bool screen_space) {
    if (!(aspect > 0) || !std::isfinite(aspect)) {
        throw std::invalid_argument("particle scene aspect ratio");
    }
    if (!runtime.initialized) {
        return;
    }
    auto projection = camera ? camera->view_projection(aspect) : MeshMatrix {};
    //The original quad and rotation kernels use downward screenY. Adapt once
    //before their work, and once on publication to the modern D3D clip space.
    for (const auto axis : { 1u, 5u, 9u, 13u }) {
        projection[axis] = -projection[axis];
    }
    const auto focal = camera ? 1.f / std::tan(camera->vertical_fov * .5f) : 1.f;
    if (!(focal > 0) || !std::isfinite(focal)) {
        throw std::invalid_argument("particle scene field of view");
    }
    for (std::size_t effect_index = 0; effect_index < runtime.state.effects.size(); ++effect_index) {
        const auto& effect = runtime.state.effects[effect_index];
        if (!effect.runtime_state || !effect.particle_count || effect.render_hidden || (effect.screen_space != 0) != screen_space) {
            continue;
        }
        std::size_t visited = 0;
        for (auto index = effect.particle_head; index >= 0;) {
            if (visited++ >= runtime.particles.size()) {
                throw std::runtime_error("cyclic particle render chain");
            }
            const auto& particle = runtime.particles.at(static_cast<std::size_t>(index));
            if (particle.effect_index < 0 || static_cast<std::size_t>(particle.effect_index) != effect_index) {
                throw std::runtime_error("particle render owner mismatch");
            }
            index = particle.links.next;
            const auto position = render_position(effect, particle);
            const auto flags = effect.render_flags | (effect.screen_space ? 0x54000u : 0x4000u);
            const auto size = static_cast<std::uint16_t>(effect.screen_space ? particle.size / 64 : 2 * particle.size);
            //657B90 publishes an unsignedWORD size;6E641A retains its0.5 factor.
            const auto half_size = static_cast<float>(size) * (1.f / 1024.f) * .5f;
            const auto half_height = half_size * (flags & 0x40000u ? 1.f : focal);
            auto half_width = half_height / aspect;
            auto material = (particle.argb >> 24) && (particle.argb >> 24) < 240 ? 1366u : 4694u;
            auto texture = white();
            std::array<std::array<float, 2>, 4> uv {};
            if (effect.sprite_frame_count) {
                const auto* catalog = c.resources.variants.catalog;
                if (!catalog) {
                    throw std::runtime_error("particle sprite catalog is not bound");
                }
                if (pixels_.catalog != catalog) {
                    pixels_.bind(*catalog);
                    textures_.clear();
                }
                //Native6908E0 always requests direction0, preserving all16 selector
                //lanes and the descriptor's base frame rather than flattening frames.
                const auto selector = effect.sprite_set_id;
                const auto& range = catalog->variants[17];
                if (selector < 0 || range.first_loaded_resource_index < 0 || selector / 16 >= range.loaded_resource_count) {
                    throw std::out_of_range("particle sprite selector " + std::to_string(selector));
                }
                const auto resource = static_cast<std::size_t>(range.first_loaded_resource_index + selector / 16);
                const auto& asset = catalog->resources.at(resource);
                const auto frame = static_cast<std::size_t>(asset.descriptor.frame_index_entries[static_cast<std::size_t>(selector % 16)].base_frame_index) + particle.sprite_frame_index;
                const auto& payload = pixels_.payload(resource);
                const auto& encoded = payload.frames.at(frame);
                const auto key = std::pair { resource, frame };
                if (!textures_.contains(key)) {
                    const auto decoded = decode_sprite_image(payload, frame);
                    if (!decoded) {
                        throw std::runtime_error("Unable to decode particle sprite " + asset.pixel_path + " frame " + std::to_string(frame) + " at byte " + std::to_string(decoded.error().offset));
                    }
                    auto image = std::make_shared<TextureImage>();
                    image->width = decoded->width;
                    image->height = decoded->height;
                    image->alpha = TextureAlpha::translucent;
                    //68F390 sets apply_flag0x200 for every category;68EA60/6FA350
                    //explicitly replace each frame's source-mode bits with alpha-low.
                    //Pixel alpha does not choose this render policy.
                    image->source_mode = TextureSourceMode::alpha_low;
                    image->pixels.reserve(decoded->pixels.size());
                    for (const auto color : decoded->pixels) {
                        image->pixels.push_back(mesh_argb_color(color));
                    }
                    textures_.emplace(key, std::move(image));
                }
                texture = textures_.at(key);
                material = sprite_texture_render_state(static_cast<std::uint32_t>(*texture->source_mode), true);
                half_width *= encoded.aspect_ratio;
                uv = { { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } };
            }
            if (flags & 4u) {
                material = flags & 0x2000u ? 2098514u : 2098466u;
            }
            if (flags & 0x20000u) {
                material |= 0x20000u;
            }
            if (flags & 0x4000u) {
                material |= 0x800u;
            }
            std::array<float, 4> center;
            if (effect.screen_space) {
                center = { static_cast<float>(numeric::product(numeric::subtract(position[0], 3125), 16) / 49) / 1024.f,
                           static_cast<float>(screen_y(position[2])) / 1024.f,
                           //Native6F1C60 configures the UI/default-Z scalar0.1;
                           //6E16C0 mode2 applies it independently of perspectiveW.
                           static_cast<float>(effect.screen_depth) / 1024.f * .1f,
                           1.f };
            } else {
                //RSSE sends authored mesh coordinates*1024, and creation stores>>4.
                //Native657B90 multiplies the internal value16, then6E655E divides
                //by1024. Thus internal/64 reproduces the actual authored marker.
                WorldVector3 world;
                for (std::size_t axis = 0; axis < world.size(); ++axis) {
                    world[axis] = static_cast<float>(numeric::product(position[axis], 16)) / 1024.f;
                }
                if (flags & 0x40000u) {
                    center = { world[0], world[1], world[2], 1.f };
                } else {
                    center = transform_sprite_position(projection, world);
                }
            }
            Quad vertices;
            vertices.fill(center);
            vertices[0][0] = vertices[3][0] = center[0] - half_width;
            vertices[1][0] = vertices[2][0] = center[0] + half_width;
            vertices[0][1] = vertices[1][1] = center[1] - half_height;
            vertices[2][1] = vertices[3][1] = center[1] + half_height;
            //Native leaves the scratch rotation unwritten for untextured frames.
            //Only the defined, textured rotation participates in modern rendering.
            if (effect.sprite_frame_count) {
                rotate(vertices, particle.rotation);
            }
            if (output.vertices.size() > std::numeric_limits<std::uint32_t>::max() - vertices.size() || output.indices.size() > std::numeric_limits<std::uint32_t>::max() - 6u) {
                throw std::overflow_error("particle scene geometry size");
            }
            const auto first = static_cast<std::uint32_t>(output.vertices.size());
            const auto color = mesh_argb_color(particle.argb);
            const bool world_projection = !effect.screen_space && !(flags & 0x40000u);
            for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex) {
                vertices[vertex][1] = -vertices[vertex][1];
                EffectVertex value { vertices[vertex], color, uv[vertex] };
                if (world_projection) {
                    value.fog_camera_depth = vertices[vertex][3];
                }
                output.vertices.push_back(value);
            }
            EffectDraw draw;
            draw.first_index = static_cast<std::uint32_t>(output.indices.size());
            draw.index_count = 6;
            draw.texture = std::move(texture);
            draw.material.flags = material;
            if (world_projection) {
                draw.fog_eligibility = WorldFogEligibility::native_geometry;
            }
            for (const auto corner : { 0u, 1u, 2u, 0u, 2u, 3u }) {
                output.indices.push_back(first + corner);
            }
            output.draws.push_back(std::move(draw));
        }
        if (visited != effect.particle_count) {
            throw std::runtime_error("particle render chain count mismatch");
        }
    }
}
} //namespace simcoaster
