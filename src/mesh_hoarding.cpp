#include "simcoaster/mesh_hoarding.hpp"
#include "simcoaster/world_types.hpp"
#include "simcoaster/simulation_sine.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
[[noreturn]] void fail(MeshFailure reason, std::size_t index = 0) {
    throw MeshError { reason, index, {}, {} };
}

void require(bool value, std::size_t index = 0) {
    if (!value) {
        fail(MeshFailure::configuration, index);
    }
}

std::array<float, 3> transformed(const MeshMatrix& matrix, const std::array<float, 3>& point) {
    std::array<float, 3> result {};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result[axis] = static_cast<float>(static_cast<double>(point[0]) * matrix[axis] + static_cast<double>(point[2]) * matrix[axis + 8] + static_cast<double>(point[1]) * matrix[axis + 4] + matrix[axis + 12]);
    }
    return result;
}

void valid_grid(const DefinitionGrid& value) {
    require(value.width >= 0 && value.width <= 40 && value.height >= 0 && value.height <= 40);
}

float terrain_vertex(const TerrainGridView& grid, std::int64_t x, std::int64_t y) {
    require(x >= 0 && y >= 0 && x <= grid.width_cells && y <= grid.height_cells);
    const auto index = static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * (static_cast<std::size_t>(grid.width_cells) + 1);
    require(index < grid.height_samples.size(), index);
    return grid.height_samples[index];
}

struct Edge {
    std::uint32_t x, y;
    std::size_t side;
    std::uint8_t mask;
    float angle;
};

MeshDrawable& drawable(MeshHoardingRuntime& value) {
    require(value.assets && value.model.root && *value.model.root < value.model.nodes.size());
    auto& result = value.model.nodes[*value.model.root].drawable;
    require(result.has_value());
    return *result;
}

void selected_texture(MeshHoardingRuntime& value, std::uint32_t index) {
    require(index < value.model.textures.size(), index);
    auto& geometry = drawable(value);
    require(!geometry.groups.empty());
    geometry.groups[0].texture_slot = index;
}

void enable(MeshHoardingRuntime& value, const MeshModel& base, std::int32_t orientation) {
    require(base.root && *base.root < base.nodes.size());
    const auto& original = base.nodes[*base.root].local_transform;
    auto transform = mesh_identity_matrix;
    transform[12] = original[12];
    transform[13] = original[13];
    transform[14] = original[14];
    const auto radians = static_cast<float>(static_cast<double>(orientation) * static_cast<double>(0.017453292f));
    const auto index = [](float phase) {
        return static_cast<std::int32_t>(static_cast<std::int64_t>(phase) % static_cast<std::int64_t>(simulation_sine_sample_count));
    };
    const auto cosine = simulation_sine_at_index(index((radians + 1.5707964f) * simulation_sine_samples_per_radian));
    const auto sine = simulation_sine_at_index(index(radians * simulation_sine_samples_per_radian));
    transform[0] = cosine;
    transform[2] = -sine;
    transform[8] = sine;
    transform[10] = cosine;
    value.model.nodes[*value.model.root].local_transform = transform;
    value.model.bounds = base.bounds;
    value.registered = true;
}

void disable(MeshHoardingRuntime& value, std::uint32_t& flags) {
    selected_texture(value, 0);
    flags = (flags & ~0xF00u) | 0x100u;
    value.registered = false;
}
} //namespace

std::expected<std::optional<MeshHoardingTemplate>, MeshError> build_mesh_hoarding(const MeshModel& source, const MeshProceduralBase& base, const DefinitionGrid& layout, std::string_view theme) {
    try {
        valid_grid(layout);
        if (base.selected_vertex_count < 2 || !base.best_node) {
            return std::optional<MeshHoardingTemplate> {};
        }
        require(*base.best_node < source.nodes.size());
        const auto& skirt = source.nodes[*base.best_node];
        require(skirt.drawable.has_value());
        std::vector<std::array<float, 3>> points;
        for (const auto& point : skirt.drawable->positions) {
            const auto world = transformed(skirt.local_transform, point);
            for (float component : world) {
                require(std::isfinite(component));
            }
            points.push_back(world);
        }
        std::vector<Edge> edges;
        const auto center_x = static_cast<double>(layout.width) * 0.5, center_y = static_cast<double>(layout.height) * 0.5;
        constexpr std::array<std::uint8_t, 4> masks { 1, 4, 16, 64 };
        constexpr std::array<std::array<double, 2>, 4> midpoints { { { 0.5, 1 }, { 1, 0.5 }, { 0.5, 0 }, { 0, 0.5 } } };
        for (std::int32_t y = 0; y < layout.height; ++y) {
            for (std::int32_t x = 0; x < layout.width; ++x) {
                const auto mask = layout.cells[static_cast<std::size_t>(y * 40 + x)].attribute;
                for (std::size_t side = 0; side < 4; ++side) {
                    if (mask & masks[side]) {
                        const auto angle = static_cast<float>(std::fmod(std::atan2(y + midpoints[side][1] - center_y, x + midpoints[side][0] - center_x) + static_cast<double>(3.926991f), static_cast<double>(6.2831855f)));
                        edges.push_back({ static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), side, mask, angle });
                    }
                }
            }
        }
        std::stable_sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
            return a.angle < b.angle;
        });
        MeshHoardingTemplate result;
        result.width = static_cast<std::uint32_t>(layout.width);
        result.height = static_cast<std::uint32_t>(layout.height);
        auto& model = result.model;
        model.name = "Hoardings";
        model.flags = 0x21;
        model.root = 0;
        model.drawable_node_count = 1;
        model.nodes.resize(1);
        auto& node = model.nodes[0];
        node.flags = 0x80800;
        node.local_transform = mesh_identity_matrix;
        auto& geometry = node.drawable.emplace();
        geometry.bounds = { { -5, -15, -5 }, { static_cast<float>(layout.width) * 10 + 5, 25, static_cast<float>(layout.height) * 10 + 5 } };
        model.bounds = geometry.bounds;
        for (const auto filename : { "Closed.tga", "Hoarding.tga", "Condemn.tga", "Upgrade.tga" }) {
            model.textures.push_back({ 1, 0, 0, "data/levels/" + std::string { theme } + "/MiscMesh/textures/" + filename });
        }
        geometry.normals.resize(edges.size() * 5);
        for (std::size_t i = 0; i < edges.size(); ++i) {
            const auto& edge = edges[i];
            const auto x = static_cast<float>(edge.x) * 10, z = static_cast<float>(edge.y) * 10;
            const std::array<std::array<float, 2>, 4> corners { { { x, z + 10 }, { x + 10, z + 10 }, { x + 10, z }, { x, z } } };
            std::array<std::array<float, 2>, 4> offsets {};
            if (layout.width == 1 && layout.height == 1) {
                offsets = { { { 0.3f, -0.3f }, { -0.3f, -0.3f }, { 0.3f, 0.3f }, { 0.3f, 0.3f } } };
            } else {
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    float nearest = std::numeric_limits<float>::max(), second = nearest;
                    std::optional<std::size_t> first_index, second_index;
                    for (std::size_t p = 0; p < points.size(); ++p) {
                        const double dx = static_cast<double>(points[p][0]) - corners[corner][0], dz = static_cast<double>(points[p][2]) - corners[corner][1];
                        const auto distance = std::sqrt(dx * dx + dz * dz);
                        if (distance < nearest) {
                            second = nearest;
                            second_index = first_index;
                            nearest = static_cast<float>(distance);
                            first_index = p;
                        } else if (distance < second) {
                            second = static_cast<float>(distance);
                            second_index = p;
                        }
                    }
                    if (second_index) {
                        offsets[corner] = { static_cast<float>((static_cast<double>(points[*second_index][0]) - corners[corner][0]) * 0.5), static_cast<float>((static_cast<double>(points[*second_index][2]) - corners[corner][1]) * 0.5) };
                    }
                }
            }
            const auto a = offsets[0], b = offsets[1], c = offsets[2], d = offsets[3];
            std::array<float, 2> first {}, last {};
            std::array<float, 3> normal {};
            std::array<std::array<std::uint32_t, 2>, 2> samples {};
            match (edge.side) {
                case 0 => {
                    first = { x + 10 + ((edge.mask & 4) ? b[0] : 0), z + 10 + a[1] };
                    last = { x + ((edge.mask & 64) ? a[0] : 0), first[1] };
                    normal = { 0, 0, 1 };
                    samples = { { { edge.x + 1, edge.y + 1 }, { edge.x, edge.y + 1 } } };
                }
                case 1 => {
                    first = { x + 10 + b[0], z + ((edge.mask & 16) ? c[1] : 0) };
                    last = { first[0], z + 10 + ((edge.mask & 1) ? b[1] : 0) };
                    normal = { 1, 0, 0 };
                    samples = { { { edge.x + 1, edge.y }, { edge.x + 1, edge.y + 1 } } };
                }
                case 2 => {
                    first = { x + ((edge.mask & 64) ? d[0] : 0), z + d[1] };
                    last = { x + 10 + ((edge.mask & 4) ? c[0] : 0), first[1] };
                    normal = { 0, 0, -1 };
                    samples = { { { edge.x, edge.y }, { edge.x + 1, edge.y } } };
                }
                case _ => {
                    first = { x + a[0], z + 10 + ((edge.mask & 1) ? a[1] : 0) };
                    last = { first[0], z + ((edge.mask & 16) ? d[1] : 0) };
                    normal = { -1, 0, 0 };
                    samples = { { { edge.x, edge.y + 1 }, { edge.x, edge.y } } };
                }
            } result.samples.push_back(samples);
            const auto vertex = static_cast<std::uint16_t>(geometry.positions.size());
            geometry.positions.insert(geometry.positions.end(), { { first[0], 10, first[1] }, { last[0], 10, last[1] }, { first[0], 0, first[1] }, { last[0], 0, last[1] } });
            geometry.texture_coordinates.insert(geometry.texture_coordinates.end(), { { 0, 1 }, { 1, 1 }, { 0, 0 }, { 1, 0 } });
            for (std::size_t p = 0; p < 4; ++p) {
                geometry.position_indices.push_back(static_cast<std::uint16_t>(vertex + p));
                geometry.normals[i * 4 + p] = normal;
            }
            for (std::size_t axis = 0; axis < 3; ++axis) {
                geometry.normals[edges.size() * 4 + i][axis] = -normal[axis];
            }
            const auto back = static_cast<std::uint16_t>(edges.size() * 4 + i);
            geometry.triangles.push_back({ vertex, { vertex, static_cast<std::uint16_t>(vertex + 1), static_cast<std::uint16_t>(vertex + 2) } });
            geometry.triangles.push_back({ vertex, { static_cast<std::uint16_t>(vertex + 1), static_cast<std::uint16_t>(vertex + 3), static_cast<std::uint16_t>(vertex + 2) } });
            geometry.triangles.push_back({ back, { static_cast<std::uint16_t>(vertex + 2), static_cast<std::uint16_t>(vertex + 1), vertex } });
            geometry.triangles.push_back({ back, { static_cast<std::uint16_t>(vertex + 2), static_cast<std::uint16_t>(vertex + 3), static_cast<std::uint16_t>(vertex + 1) } });
        }
        geometry.groups.push_back({ 0, 0, static_cast<std::uint16_t>(geometry.triangles.size()), 0, static_cast<std::uint16_t>(geometry.positions.empty() ? 0 : geometry.positions.size() - 1), 0 });
        return std::optional { std::move(result) };
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

MeshHoardingRuntime create_mesh_hoarding(std::shared_ptr<const MeshHoardingTemplate> assets) {
    require(assets != nullptr);
    auto model = assets->model;
    return { std::move(assets), std::move(model), 0, false };
}

std::expected<void, MeshError> initialize_mesh_hoarding_terrain(MeshHoardingRuntime& value, const TerrainGridView& terrain, const std::array<std::int64_t, 2>& minimum, const std::array<std::int64_t, 2>& maximum, std::int32_t orientation, float root_height) {
    try {
        auto& geometry = drawable(value);
        require(value.assets->width && value.assets->height);
        for (std::size_t i = 0; i < value.assets->samples.size(); ++i) {
            for (std::size_t endpoint = 0; endpoint < 2; ++endpoint) {
                const auto& sample = value.assets->samples[i][endpoint];
                const auto x = static_cast<float>(static_cast<double>(sample[0]) / value.assets->width), y = static_cast<float>(static_cast<double>(sample[1]) / value.assets->height);
                const auto mapped = mesh_footprint_sample(minimum, maximum, orientation, x, y);
                if (!mapped) {
                    throw mapped.error();
                }
                const auto height = static_cast<float>((static_cast<double>(terrain_vertex(terrain, (*mapped)[0], (*mapped)[1])) - root_height) * 0.5);
                geometry.positions[i * 4 + endpoint][1] = static_cast<float>(static_cast<double>(height) + 10);
                geometry.positions[i * 4 + 2 + endpoint][1] = height;
            }
        }
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<void, MeshError> apply_mesh_hoarding_phase(MeshHoardingRuntime& value, float phase) {
    try {
        require(std::isfinite(phase));
        auto& geometry = drawable(value);
        const auto count = value.assets->samples.size();
        require(geometry.positions.size() == count * 4 && geometry.texture_coordinates.size() == count * 4);
        for (std::size_t i = 0; i < count; ++i) {
            const auto envelope = std::clamp(1.0 - (static_cast<double>(i) / static_cast<double>(count) * static_cast<double>(0.8f) - (static_cast<double>(phase) - 0.25)) * 4, 0.0, 1.0);
            const auto amount = (i % 2 ? static_cast<double>(0.8f) : 1.0) * envelope;
            for (std::size_t endpoint = 0; endpoint < 2; ++endpoint) {
                geometry.positions[i * 4 + endpoint][1] = static_cast<float>(geometry.positions[i * 4 + 2 + endpoint][1] + amount * 10);
                geometry.texture_coordinates[i * 4 + 2 + endpoint] = { static_cast<float>(endpoint), static_cast<float>(1 - amount) };
            }
        }
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<MeshHoardingResult, MeshError> select_mesh_hoarding_variant(MeshHoardingRuntime& value, std::uint32_t& flags, const MeshModel& base, std::int32_t orientation, std::int32_t variant) {
    try {
        const auto select = [&](std::uint32_t slot, std::uint32_t mask) {
            selected_texture(value, slot);
            flags = (flags & ~0xF00u) | mask;
        };
        match (variant) {
            case 1 => {
                if ((flags & 0x80u) || !(flags & 0xF00u)) {
                    select(0, 0x100);
                }
            }
            case 2 => {
                if (!(flags & 0x200u)) {
                    select(1, 0x200);
                }
            }
            case 4 => {
                if (!(flags & 0x400u)) {
                    select(2, 0x400);
                }
            }
            case 8 => {
                if (!(flags & 0x800u)) {
                    select(3, 0x800);
                }
            }
            case _ => {
            }
        } value.rate = 0.2f;
        MeshHoardingResult result;
        if (!(flags & 0x20u)) {
            enable(value, base, orientation);
            result.registered = true;
        }
        flags = (flags & ~0x80u) | 0x60u;
        return result;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

void reverse_mesh_hoarding(MeshHoardingRuntime& value, std::uint32_t& flags) {
    value.rate = -0.3f;
    if (flags & 0x20u) {
        flags = (flags & ~0x40u) | 0x80u;
    }
}

std::expected<MeshHoardingResult, MeshError> advance_mesh_hoarding(MeshHoardingRuntime& value, std::uint32_t& flags, float& phase, float delta) {
    try {
        require(std::isfinite(delta) && delta >= 0 && std::isfinite(phase));
        MeshHoardingResult result;
        if (!(flags & 0xC0u)) {
            return result;
        }
        const auto previous = phase;
        phase = std::clamp(static_cast<float>(static_cast<double>(delta) * value.rate + previous), 0.0f, 1.0f);
        if (phase == previous) {
            if (!(flags & 0x80u)) {
                if (phase >= 1) {
                    flags &= ~0x40u;
                }
            } else if (phase <= 0) {
                flags &= ~(0x20u | 0x80u);
                disable(value, flags);
                result.unregistered = true;
            }
        } else {
            const auto applied = apply_mesh_hoarding_phase(value, phase);
            if (!applied) {
                throw applied.error();
            }
            result.pose_changed = true;
        }
        return result;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<MeshHoardingResult, MeshError> restore_mesh_hoarding(MeshHoardingRuntime& value, std::uint32_t& flags, float phase, const MeshModel& base, std::int32_t orientation) {
    try {
        MeshHoardingResult result;
        if (flags & 0x20u) {
            enable(value, base, orientation);
            result.registered = true;
            if (flags & 0x40u) {
                value.rate = 0.2f;
            } else if (flags & 0x80u) {
                value.rate = -0.3f;
            }
        }
        const auto applied = apply_mesh_hoarding_phase(value, phase);
        if (!applied) {
            throw applied.error();
        }
        result.pose_changed = true;
        if (flags & 0x100u) {
            selected_texture(value, 0);
        } else if (flags & 0x200u) {
            selected_texture(value, 1);
        } else if (flags & 0x400u) {
            selected_texture(value, 2);
        } else if (flags & 0x800u) {
            selected_texture(value, 3);
        } else {
            selected_texture(value, 0);
            flags |= 0x100u;
        }
        return result;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
