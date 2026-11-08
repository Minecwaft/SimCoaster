#include "simcoaster/save_stream.hpp"
#include "simcoaster/coaster_design.hpp"
#include "simcoaster/save_schema.hpp"
#include "simcoaster/world_save.hpp"
#include <limits>

namespace simcoaster {
namespace {
using save::read;
using save::write;
constexpr auto wire_count_limit = std::numeric_limits<std::uint16_t>::max();
} //namespace

std::expected<CoasterDesign, CoasterDesignError> read_coaster_design(Stream& stream) {
    CoasterDesign design;
    const auto transfer = [](std::size_t index = 0) {
        return std::unexpected(CoasterDesignError { CoasterDesignFailure::transfer, index });
    };
    if (!read(stream, design.version)) {
        return transfer();
    }
    for (std::size_t i = 0; i < design.name_a.size(); ++i) {
        if (!read(stream, design.name_a[i]) || !read(stream, design.name_b[i])) {
            return transfer();
        }
    }
    if (!read(stream, design.source_index) || !read(stream, design.definition_id) || !read(stream, design.build_cost) || !read(stream, design.placement_offset_x) || !read(stream, design.placement_offset_y) || !read(stream, design.grid_width) || !read(stream, design.grid_height)) {
        return transfer();
    }
    for (auto& cell : design.grid) {
        if (!read(stream, cell.code) || !read(stream, cell.attribute) || stream.read(cell.reserved) != 3) {
            return transfer();
        }
    }
    std::uint32_t count {};
    if (!read(stream, count)) {
        return transfer();
    }
    //Native 48FF00 uses an unchecked 64-pointer stack buffer. The rebuilt owner
    //uses vectors, bounded by the WORD geometry count in the real save format.
    if (count > wire_count_limit) {
        return std::unexpected(CoasterDesignError { CoasterDesignFailure::geometry_count, count });
    }
    design.geometry.resize(count);
    for (std::size_t i = 0; i < design.geometry.size(); ++i) {
        auto& geometry = design.geometry[i];
        std::uint8_t first {}, second {};
        if (!read(stream, geometry.tile_offset_x) || !read(stream, geometry.tile_offset_y) || !read(stream, first) || !read(stream, second)) {
            return transfer(i);
        }
        geometry.starts_two_piece = first != 0;
        geometry.axis_mode = second != 0;
    }
    for (std::size_t i = 0; i < design.geometry.size(); ++i) {
        auto& geometry = design.geometry[i];
        std::uint32_t peers {};
        if (!read(stream, geometry.flags) || !read(stream, geometry.link_slot) || !read(stream, geometry.heading) || !read(stream, geometry.loft) || !read(stream, geometry.secondary_parameter) || !read(stream, geometry.angle_b) || !read(stream, peers)) {
            return transfer(i);
        }
        if (peers > wire_count_limit) {
            return std::unexpected(CoasterDesignError { CoasterDesignFailure::peer_count, i });
        }
        geometry.peers.resize(peers);
        for (auto& peer : geometry.peers) {
            if (!read(stream, peer)) {
                return transfer(i);
            }
        }
    }
    return design;
}

std::expected<void, CoasterDesignError> write_coaster_design(Stream& stream, const CoasterDesign& design) {
    const auto transfer = [](std::size_t index = 0) {
        return std::unexpected(CoasterDesignError { CoasterDesignFailure::transfer, index });
    };
    if (design.geometry.size() > wire_count_limit) {
        return std::unexpected(CoasterDesignError { CoasterDesignFailure::geometry_count, design.geometry.size() });
    }
    for (std::size_t i = 0; i < design.geometry.size(); ++i) {
        if (design.geometry[i].peers.size() > wire_count_limit) {
            return std::unexpected(CoasterDesignError { CoasterDesignFailure::peer_count, i });
        }
    }
    if (!write(stream, design.version)) {
        return transfer();
    }
    for (std::size_t i = 0; i < design.name_a.size(); ++i) {
        if (!write(stream, design.name_a[i]) || !write(stream, design.name_b[i])) {
            return transfer();
        }
    }
    if (!write(stream, design.source_index) || !write(stream, design.definition_id) || !write(stream, design.build_cost) || !write(stream, design.placement_offset_x) || !write(stream, design.placement_offset_y) || !write(stream, design.grid_width) || !write(stream, design.grid_height)) {
        return transfer();
    }
    for (const auto& cell : design.grid) {
        if (!write(stream, cell.code) || !write(stream, cell.attribute) || stream.write(cell.reserved) != 3) {
            return transfer();
        }
    }
    if (!write(stream, static_cast<std::uint32_t>(design.geometry.size()))) {
        return transfer();
    }
    for (std::size_t i = 0; i < design.geometry.size(); ++i) {
        const auto& geometry = design.geometry[i];
        if (!write(stream, geometry.tile_offset_x) || !write(stream, geometry.tile_offset_y) || !write(stream, static_cast<std::uint8_t>(geometry.starts_two_piece)) || !write(stream, static_cast<std::uint8_t>(geometry.axis_mode))) {
            return transfer(i);
        }
    }
    for (std::size_t i = 0; i < design.geometry.size(); ++i) {
        const auto& geometry = design.geometry[i];
        if (!write(stream, geometry.flags) || !write(stream, geometry.link_slot) || !write(stream, geometry.heading) || !write(stream, geometry.loft) || !write(stream, geometry.secondary_parameter) || !write(stream, geometry.angle_b) || !write(stream, static_cast<std::uint32_t>(geometry.peers.size()))) {
            return transfer(i);
        }
        for (const auto peer : geometry.peers) {
            if (!write(stream, peer)) {
                return transfer(i);
            }
        }
    }
    return {};
}
} //namespace simcoaster
