#include "simcoaster/construction_preview.hpp"
#include "simcoaster/simulation_sine.hpp"
#include "simcoaster/world.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace simcoaster {
namespace {
float sine(float phase) {
    if (!std::isfinite(phase) || phase < -2147483648.f || phase >= 2147483648.f) {
        throw std::out_of_range("construction preview sine phase");
    }
    return simulation_sine_at_rounded_index(phase);
}
} //namespace

std::string_view construction_preview_texture(std::uint32_t material) {
    //Native texture names are logical TGA names; MeshTextureLibrary applies
    //the engine's FSH-before-TGA lookup to the installed assets.
    constexpr std::array names { "blue.tga", "red.tga", "orange.tga", "m_enter.tga", "m_exit.tga", "m_direct.tga", "m_front.tga", "m_inout.tga", "lightblue.tga", "patrol.tga", "brightred.tga", "m_link.tga", "m_break.tga", "m_cross.tga", "m_end.tga", "m_nocash.tga", "m_erase.tga", "red.tga", "gby_sur1.tga", "gby_sur2.tga", "gby_sur3.tga", "gte_lgo1.tga", "gte_lgo1.tga" };
    if (material >= names.size()) {
        throw std::out_of_range("construction preview material");
    }
    return names[material];
}

std::array<SceneVertex, 4> construction_preview_quad(WorldRuntime& c, const ConstructionFace& face, float phase, float heading, std::array<float, 2> offset) {
    (void)construction_preview_texture(face.material);
    if ((face.width != 10 && face.width != 20) || face.height != 10) {
        throw std::invalid_argument("construction preview native extent");
    }
    float x = face.position[0], y = face.position[1], z = face.position[2];
    if (!face.skip_coordinate_adjustment && face.material < 16) {
        x += offset[0];
        z += offset[1];
    }
    const auto end_x = x + face.width, end_z = z + face.width, top = y + 10;
    std::array<MeshPoint, 4> points;
    //Preserve the native winding, including the reversed far/left edges.
    match (face.direction) {
        case 0  => points = { { { x, y, z }, { end_x, y, z }, { x, y, end_z }, { end_x, y, end_z } } };
        case 1  => points = { { { x, y, z }, { end_x, y, z }, { x, top, z }, { end_x, top, z } } };
        case 4  => points = { { { end_x, y, z }, { end_x, y, end_z }, { end_x, top, z }, { end_x, top, end_z } } };
        case 16 => points = { { { end_x, y, end_z }, { x, y, end_z }, { end_x, top, end_z }, { x, top, end_z } } };
        case 64 => points = { { { x, y, end_z }, { x, y, z }, { x, top, end_z }, { x, top, z } } };
        case _  => throw std::invalid_argument("construction preview face direction");
    } std::array<std::array<float, 2>, 4>
        uv { { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } } };
    if (face.material >= 11 && face.material < 18) {
        const auto sin_heading = sine(heading * simulation_sine_samples_per_radian);
        const auto cos_heading = sine(static_cast<float>((static_cast<double>(heading) + 1.5707964f) * simulation_sine_samples_per_radian));
        constexpr std::array<std::array<float, 2>, 4> centered { { { -.4f, -.4f }, { .4f, -.4f }, { -.4f, .4f }, { .4f, .4f } } };
        for (std::size_t i = 0; i < uv.size(); ++i) {
            const auto u = centered[i][0], v = centered[i][1];
            const auto rotated_u = static_cast<float>(static_cast<double>(cos_heading) * u - static_cast<double>(v) * sin_heading);
            const auto rotated_v = static_cast<float>(static_cast<double>(v) * cos_heading + static_cast<double>(sin_heading) * u);
            uv[i] = { rotated_u + .5f, rotated_v + .5f };
        }
    }
    std::array<std::size_t, 4> order { 0, 1, 2, 3 };
    match (face.geometry_selector) {
        case 4  => order = { 2, 0, 3, 1 };
        case 16 => order = { 3, 2, 1, 0 };
        case 64 => order = { 1, 3, 0, 2 };
        case _  => {
        }
    } std::array<SceneVertex, 4>
        result;
    for (std::size_t i = 0; i < result.size(); ++i) {
        auto& vertex = result[i];
        vertex.position = points[i];
        vertex.uv = uv[order[i]];
        vertex.normal = { 0, 1, 0 };
        if (face.material < 18) {
            const auto ground = static_cast<float>(world_operations::sample_terrain_surface_height(c, vertex.position[0], vertex.position[2]) + points[i][1]);
            const auto wave_phase = static_cast<float>((static_cast<double>(phase) + vertex.position[0] + vertex.position[2]) * simulation_sine_samples_per_radian);
            const auto wave = sine(wave_phase);
            vertex.position[1] = ground + wave;
            vertex.normal = { 0, (wave + 1.f) * .5f, 0 };
        }
        vertex.reflection_position = vertex.position;
    }
    return result;
}
} //namespace simcoaster
