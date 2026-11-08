#include "simcoaster/mesh_height_map.hpp"
#include "simcoaster/world_types.hpp"
#include "mesh_binary.hpp"
#include "mesh_traversal.hpp"
#include <algorithm>
#include <cmath>

namespace simcoaster {
namespace {
struct Header {
    [[=save::Field{ "magic", 0 }]] std::int32_t magic{ -1424097277 };
    [[=save::Field{ "version", 1 }]] std::uint32_t version{ 6553605 };
    [[=save::Field{ "width", 2 }]] std::uint16_t width{};
    [[=save::Field{ "height", 3 }]] std::uint16_t height{};
    [[=save::Field{ "fine", 4 }]] std::uint32_t fine{};
    [[=save::Field{ "cells", 5 }]] std::uint32_t cells{};
    [[=save::Field{ "mask", 6 }]] std::uint32_t mask{};
    [[=save::Field{ "bounds", 7 }]] std::array<float, 6> bounds{};
};

static_assert(save::record_width<Header> == 48);

[[noreturn]] void fail(std::size_t index = 0) {
    throw MeshError{ MeshFailure::height_map, index, {}, {} };
}

void require(bool valid, std::size_t index = 0) {
    if (!valid)
        fail(index);
}

std::size_t cell_count(std::uint32_t width, std::uint32_t height) {
    require(width <= std::numeric_limits<std::uint16_t>::max() && height <= std::numeric_limits<std::uint16_t>::max());
    const auto count = static_cast<std::size_t>(width) * height;
    require(count <= (std::numeric_limits<std::uint32_t>::max() - 48u) / 27u);
    return count;
}

struct Pixel {
    std::int64_t x{}, z{}, height{};
};

struct Span {
    std::int64_t left{ 1000000 }, right{ -1000000 }, left_height{}, right_height{};

    void include(std::int64_t x, std::int64_t height) {
        if (x < left) {
            left = x;
            left_height = height;
        }
        if (x > right) {
            right = x;
            right_height = height;
        }
    }
};

void triangle(MeshHeightMap& grid, const std::array<Pixel, 3>& points) {
    auto minimum = points[0].z, maximum = minimum;
    for (const auto& point : points) {
        minimum = std::min(minimum, point.z);
        maximum = std::max(maximum, point.z);
    }
    std::vector<Span> spans(static_cast<std::size_t>(maximum - minimum + 1));
    //Strict endpoint comparisons and reverse edge order preserve native ties.
    for (std::size_t reverse = 3; reverse > 0; --reverse) {
        const auto index = reverse - 1;
        const auto& start = points[index];
        const auto& end = points[(index + 1) % 3];
        const auto dx = end.x - start.x, dz = end.z - start.z, dh = end.height - start.height;
        const auto half = dz / 2, length = std::abs(dz);
        const std::int64_t step = dz < 0 ? -1 : 1;
        spans[static_cast<std::size_t>(start.z - minimum)].include(start.x, start.height);
        spans[static_cast<std::size_t>(end.z - minimum)].include(end.x, end.height);
        for (std::int64_t offset = 1; offset < length; ++offset)
            spans[static_cast<std::size_t>(start.z + offset * step - minimum)].include(
                start.x + (half + offset * dx) / length,
                start.height + offset * dh / length);
    }
    const auto pitch = static_cast<std::size_t>(grid.width) * 5;
    const auto put = [&](std::int64_t x, std::int64_t z, std::int64_t height) {
        require(
            x >= 0 && z >= 0 && x < static_cast<std::int64_t>(pitch) && z < static_cast<std::int64_t>(grid.height) * 5);
        auto& target = grid.fine_heights[static_cast<std::size_t>(x) + static_cast<std::size_t>(z) * pitch];
        if (height > target)
            target = static_cast<std::uint8_t>(height);
    };
    for (std::size_t row = 0; row < spans.size(); ++row) {
        const auto& span = spans[row];
        const auto z = minimum + static_cast<std::int64_t>(row), width = span.right - span.left;
        put(span.left, z, span.left_height);
        put(span.right, z, span.right_height);
        for (std::int64_t offset = 1; offset < width; ++offset)
            put(span.left + offset, z, span.left_height + offset * (span.right_height - span.left_height) / width);
    }
}

void rasterize(MeshHeightMap& grid, const MeshModel& model) {
    const auto poses = evaluate_mesh_pose(model);
    if (!poses)
        throw poses.error();
    std::vector<std::size_t> pending;
    std::vector<bool> seen(model.nodes.size());
    if (model.root)
        pending.push_back(*model.root);
    while (!pending.empty()) {
        const auto index = pending.back();
        pending.pop_back();
        require(index < model.nodes.size() && !seen[index], index);
        seen[index] = true;
        const auto& node = model.nodes[index];
        if (node.drawable && !(node.flags & 0x8210u)) {
            std::vector<Pixel> positions;
            positions.reserve(node.drawable->positions.size());
            const auto& matrix = (*poses)[index];
            for (const auto& source : node.drawable->positions) {
                std::array<float, 3> world{};
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    world[axis] = static_cast<float>(static_cast<double>(source[0]) * matrix[axis]
                                                     + static_cast<double>(source[2]) * matrix[axis + 8]
                                                     + static_cast<double>(source[1]) * matrix[axis + 4]
                                                     + matrix[axis + 12]);
                    require(std::isfinite(world[axis]));
                    grid.bounds.minimum[axis] = std::min(grid.bounds.minimum[axis], world[axis]);
                    grid.bounds.maximum[axis] = std::max(grid.bounds.maximum[axis], world[axis]);
                }
                for (const auto axis : { 0u, 2u }) {
                    const auto extent = static_cast<double>(axis == 0 ? grid.width : grid.height) * 10;
                    if (world[axis] < -1 || world[axis] > extent + 1)
                        grid.clipped = true;
                    world[axis] = static_cast<float>(std::clamp(static_cast<double>(world[axis]), 0.0, extent - 0.001));
                }
                if (world[1] >= 100)
                    grid.clipped = true;
                world[1] = std::clamp(world[1], 0.0f, 99.999001f);
                positions.push_back({ std::min(static_cast<std::int64_t>(static_cast<double>(world[0]) * 0.5),
                                               static_cast<std::int64_t>(grid.width) * 5 - 1),
                                      std::min(static_cast<std::int64_t>(static_cast<double>(world[2]) * 0.5),
                                               static_cast<std::int64_t>(grid.height) * 5 - 1),
                                      static_cast<std::int64_t>(static_cast<double>(world[1]) * 2.55) });
            }
            for (const auto& group : node.drawable->groups) {
                require(static_cast<std::size_t>(group.first_triangle) + group.triangle_count
                        <= node.drawable->triangles.size());
                for (std::size_t i = group.first_triangle;
                     i < static_cast<std::size_t>(group.first_triangle) + group.triangle_count;
                     ++i) {
                    std::array<Pixel, 3> points;
                    for (std::size_t corner = 0; corner < 3; ++corner) {
                        const auto render = node.drawable->triangles[i].corners[corner];
                        require(render < node.drawable->position_indices.size());
                        const auto vertex = node.drawable->position_indices[render];
                        require(vertex < positions.size());
                        points[corner] = positions[vertex];
                    }
                    triangle(grid, points);
                }
            }
        }
        append_mesh_children(model, node, pending, [](bool valid) {
            require(valid);
        });
    }
}
} //namespace

std::expected<MeshHeightMap, MeshError> parse_mesh_height_map(std::span<const std::byte> bytes) {
    try {
        mesh_binary::Reader reader{ bytes };
        require(bytes.size() >= save::record_width<Header>);
        const auto header = reader.get<Header>(0);
        require(header.magic == -1424097277 && header.version == 6553605);
        const auto count = cell_count(header.width, header.height);
        MeshHeightMap result;
        result.width = header.width;
        result.height = header.height;
        result.bounds = { { header.bounds[0], header.bounds[1], header.bounds[2] },
                          { header.bounds[3], header.bounds[4], header.bounds[5] } };
        for (float value : header.bounds)
            require(std::isfinite(value));
        for (const auto offset : { header.fine, header.cells, header.mask })
            require(!count || offset >= 48, offset);
        result.fine_heights = reader.array<std::uint8_t>(header.fine, count * 25);
        result.cell_heights = reader.array<std::uint8_t>(header.cells, count);
        result.placement_mask = reader.array<std::uint8_t>(header.mask, count);
        return result;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<std::vector<std::byte>, MeshError> encode_mesh_height_map(const MeshHeightMap& grid) {
    try {
        const auto count = cell_count(grid.width, grid.height);
        require(grid.fine_heights.size() == count * 25
                && grid.cell_heights.size() == count
                && grid.placement_mask.size() == count);
        const Header header{ -1424097277,
                             6553605,
                             grid.width,
                             grid.height,
                             48,
                             static_cast<std::uint32_t>(48 + count * 25),
                             static_cast<std::uint32_t>(48 + count * 26),
                             { grid.bounds.minimum[0],
                               grid.bounds.minimum[1],
                               grid.bounds.minimum[2],
                               grid.bounds.maximum[0],
                               grid.bounds.maximum[1],
                               grid.bounds.maximum[2] } };
        const auto wire = save::detail::encode(header);
        std::vector<std::byte> bytes(wire.begin(), wire.end());
        bytes.reserve(48 + count * 27);
        for (const auto* plane : { &grid.fine_heights, &grid.cell_heights, &grid.placement_mask })
            for (const auto value : *plane)
                bytes.push_back(static_cast<std::byte>(value));
        return bytes;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<MeshHeightMap, MeshError> generate_mesh_height_map(const MeshModel& source,
                                                                 const MeshAnimationLibrary& animations,
                                                                 std::uint32_t width,
                                                                 std::uint32_t height,
                                                                 const DefinitionGrid* shape,
                                                                 bool bounds_height) {
    try {
        const auto count = cell_count(width, height);
        if (shape)
            require(width <= 40
                    && height <= 40
                    && shape->width >= 0
                    && shape->width <= 40
                    && shape->height >= 0
                    && shape->height <= 40);
        MeshHeightMap result;
        result.width = static_cast<std::uint16_t>(width);
        result.height = static_cast<std::uint16_t>(height);
        result.generated = true;
        result.fine_heights.resize(count * 25);
        result.cell_heights.resize(count);
        result.placement_mask.resize(count);
        result.bounds = source.bounds;
        if (!count)
            return result;
        bool animated = false;
        for (std::size_t category = 2; category < animations.groups.size(); ++category)
            if (!animations.groups[category].empty())
                animated = true;
        if (animated) {
            //Assembly initializes maxima to00800000: positive FLT_MIN, not -FLT_MAX.
            result.bounds = { { { std::numeric_limits<float>::max(),
                                  std::numeric_limits<float>::max(),
                                  std::numeric_limits<float>::max() } },
                              { { std::numeric_limits<float>::min(),
                                  std::numeric_limits<float>::min(),
                                  std::numeric_limits<float>::min() } } };
            for (std::size_t category = 0; category < animations.groups.size(); ++category)
                for (std::size_t variant = 0; variant < animations.groups[category].size(); ++variant) {
                    if (category < 2 && !variant)
                        continue;
                    const auto& animation = animations.groups[category][variant].animation;
                    auto model = source;
                    for (std::uint64_t timestamp = 0;; timestamp += 30) {
                        const auto frame =
                            static_cast<float>(static_cast<double>(timestamp) * static_cast<double>(0.03f));
                        if (frame > animation.frame_span())
                            break;
                        model = source;
                        MeshAnimationState state;
                        const MeshAnimationLayer layer{ &animation, frame, {}, true };
                        const auto posed = evaluate_mesh_animation_layers(source,
                                                                          model,
                                                                          std::span{ &layer, 1 },
                                                                          std::span{ &state, 1 });
                        if (!posed)
                            throw posed.error();
                        rasterize(result, model);
                    }
                }
        } else
            rasterize(result, source);
        const auto pitch = static_cast<std::size_t>(width) * 5;
        for (std::size_t y = 0; y < height; ++y)
            for (std::size_t x = 0; x < width; ++x) {
                auto maximum = std::uint8_t{};
                for (std::size_t row = 0; row < 5; ++row)
                    for (std::size_t column = 0; column < 5; ++column)
                        maximum = std::max(maximum, result.fine_heights[x * 5 + column + (y * 5 + row) * pitch]);
                if (bounds_height) {
                    const auto value = static_cast<double>(source.bounds.maximum[1]) * 2.55;
                    require(std::isfinite(value)
                            && value >= static_cast<double>(std::numeric_limits<std::int64_t>::min())
                            && value < static_cast<double>(std::numeric_limits<std::int64_t>::max()));
                    maximum = static_cast<std::uint8_t>(static_cast<std::int64_t>(value));
                }
                result.cell_heights[x + y * width] = maximum;
                result.placement_mask[x + y * width] =
                    static_cast<std::uint8_t>(!shape || shape->cells[y * 40 + x].code != 0);
            }
        return result;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
