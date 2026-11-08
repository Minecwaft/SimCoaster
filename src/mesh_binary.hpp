#pragma once
#include "simcoaster/mesh_model.hpp"

namespace simcoaster::mesh_binary {
//Wire declarations share the existing reflection-generated little-endian
//decoder. Their layout is independent of the native x64 runtime declarations.
struct ModelHeader {
    [[=save::Field{ "magic", 0 }]] std::uint32_t magic{};
    [[=save::Field{ "version", 1 }]] std::uint32_t version{};
    [[=save::Field{ "animation_version", 2 }]] std::uint32_t animation_version{};
    [[=save::Field{ "export_metadata", 3 }]] std::array<std::byte, 12> export_metadata{};
    [[=save::Field{ "name", 4 }]] std::array<char, 24> name{};
    [[=save::Field{ "flags", 5 }]] std::uint32_t flags{};
    [[=save::Field{ "directory_count", 6 }]] std::uint16_t directory_count{};
    [[=save::Field{ "slot_count", 7 }]] std::uint16_t slot_count{};
    [[=save::Field{ "position_block_count", 8 }]] std::uint16_t position_block_count{};
    [[=save::Field{ "uv_block_count", 9 }]] std::uint16_t uv_block_count{};
    [[=save::Field{ "group_count", 10 }]] std::uint16_t group_count{};
    [[=save::Field{ "triangle_count", 11 }]] std::uint16_t triangle_count{};
    [[=save::Field{ "curve_count", 12 }]] std::uint16_t curve_count{};
    [[=save::Field{ "node_count", 13 }]] std::uint16_t node_count{};
    [[=save::Field{ "drawable_count", 14 }]] std::uint16_t drawable_count{};
    [[=save::Field{ "first_metadata_node", 15 }]] std::uint16_t first_metadata_node{};
    [[=save::Field{ "metadata_count", 16 }]] std::uint16_t metadata_count{};
    [[=save::Field{ "normal_count", 17 }]] std::uint16_t normal_count{};
    [[=save::Field{ "directories", 18 }]] std::uint32_t directories{};
    [[=save::Field{ "slots", 19 }]] std::uint32_t slots{};
    [[=save::Field{ "slot_metadata", 20 }]] std::uint32_t slot_metadata{};
    [[=save::Field{ "positions", 21 }]] std::uint32_t positions{};
    [[=save::Field{ "normals", 22 }]] std::uint32_t normals{};
    [[=save::Field{ "uvs", 23 }]] std::uint32_t uvs{};
    [[=save::Field{ "groups", 24 }]] std::uint32_t groups{};
    [[=save::Field{ "triangles", 25 }]] std::uint32_t triangles{};
    [[=save::Field{ "terrain", 26 }]] std::uint32_t terrain{};
    [[=save::Field{ "drawables", 27 }]] std::uint32_t drawables{};
    [[=save::Field{ "transforms", 28 }]] std::uint32_t transforms{};
    [[=save::Field{ "root", 29 }]] std::uint32_t root{};
    [[=save::Field{ "metadata", 30 }]] std::uint32_t metadata{};
    [[=save::Field{ "bounds", 31 }]] std::array<float, 6> bounds{};
    [[=save::Field{ "auxiliary", 32 }]] std::uint32_t auxiliary{};
    [[=save::Field{ "original_model", 33 }]] std::uint32_t original_model{};
    [[=save::Field{ "surface_height", 34 }]] float surface_height{};
    [[=save::Field{ "reserved", 35 }]] std::uint32_t reserved{};
    [[=save::Field{ "tile_bounds", 36 }]] std::uint32_t tile_bounds{};
    [[=save::Field{ "curves", 37 }]] std::uint32_t curves{};
    [[=save::Field{ "allocation", 38 }]] std::uint32_t allocation{};
    [[=save::Field{ "instance", 39 }]] std::uint32_t instance{};
};

struct NodeHeader {
    [[=save::Field{ "flags", 0 }]] std::uint32_t flags{};
    [[=save::Field{ "parent", 1 }]] std::uint32_t parent{};
    [[=save::Field{ "next", 2 }]] std::uint32_t next{};
    [[=save::Field{ "child", 3 }]] std::uint32_t child{};
    [[=save::Field{ "transform", 4 }]] MeshMatrix transform{};
    [[=save::Field{ "index", 5 }]] std::uint16_t index{};
    [[=save::Field{ "curve", 6 }]] std::uint16_t curve{};
    [[=save::Field{ "name", 7 }]] std::uint32_t name{};
};

static_assert(save::record_width<ModelHeader> == 184 && save::record_width<NodeHeader> == 88);

struct TerrainHeader {
    [[=save::Field{ "unknown", 0 }]] std::uint32_t unknown{};
    [[=save::Field{ "sample_count", 1 }]] std::uint16_t sample_count{};
    [[=save::Field{ "cell_count", 2 }]] std::uint16_t cell_count{};
    [[=save::Field{ "origin", 3 }]] std::array<float, 2> origin{};
    [[=save::Field{ "scale", 4 }]] std::array<float, 2> scale{};
    [[=save::Field{ "dimensions", 5 }]] std::array<std::uint32_t, 2> dimensions{};
    [[=save::Field{ "height_range", 6 }]] std::array<float, 2> height_range{};
    [[=save::Field{ "samples", 7 }]] std::uint32_t samples{};
    [[=save::Field{ "cells", 8 }]] std::uint32_t cells{};
};

static_assert(save::record_width<TerrainHeader> == 48);

struct Reader {
    std::span<const std::byte> bytes;

    [[noreturn]] void fail(MeshFailure reason, std::size_t offset) const {
        throw MeshError{ reason, offset, {}, {} };
    }

    void range(std::size_t offset, std::size_t count, std::size_t stride = 1) const {
        if (!count)
            return;
        if (!offset || offset > bytes.size() || count > (bytes.size() - offset) / stride)
            fail(MeshFailure::bounds, offset);
    }

    template <class T>
    T get(std::size_t offset) const {
        constexpr auto width = save::value_width<T>;
        if (offset > bytes.size() || width > bytes.size() - offset)
            fail(MeshFailure::bounds, offset);
        T value{};
        save::detail::decode<T>(std::span<const std::byte, width>{ bytes.data() + offset, width }, value);
        return value;
    }

    template <class T>
    std::vector<T> array(std::size_t offset, std::size_t count) const {
        constexpr auto width = save::value_width<T>;
        static_assert(width > 0);
        range(offset, count, width);
        std::vector<T> result;
        result.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            result.push_back(get<T>(offset + i * width));
        return result;
    }

    std::string text(std::size_t offset) const {
        if (!offset)
            return {};
        range(offset, 1);
        const std::string_view rest{ reinterpret_cast<const char*>(bytes.data() + offset), bytes.size() - offset };
        const auto end = rest.find('\0');
        if (end == std::string_view::npos)
            fail(MeshFailure::unterminated_string, offset);
        return std::string{ rest.substr(0, end) };
    }
};

struct VectorCurveHeader {
    [[=save::Field{ "flags", 0 }]] std::uint32_t flags{};
    [[=save::Field{ "point_count", 1 }]] std::uint16_t point_count{};
    [[=save::Field{ "key_count", 2 }]] std::uint16_t key_count{};
    [[=save::Field{ "points", 3 }]] std::uint32_t points{};
    [[=save::Field{ "keys", 4 }]] std::uint32_t keys{};
};

inline MeshVectorCurve vector_curve(const Reader& r, std::size_t offset) {
    const auto h = r.get<VectorCurveHeader>(offset);
    return { h.flags,
             r.array<std::array<float, 3>>(h.points, h.point_count),
             r.array<MeshFrameKey>(h.keys, h.key_count) };
}
} //namespace simcoaster::mesh_binary
