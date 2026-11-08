#pragma once
#include "asset_error.hpp"
#include <optional>
#include <string>

namespace simcoaster {
enum class MeshFailure {
    io,
    archive,
    missing,
    short_header,
    signature,
    version,
    animation_resource,
    bounds,
    unterminated_string,
    node_link,
    node_cycle,
    metadata,
    index,
    configuration,
    pose_size,
    attachment,
    deformed_attachment,
    animation_version,
    animation_frame,
    animation_track,
    base_resource,
    animation_binding,
    height_map,
};

struct MeshError {
    MeshFailure reason;
    std::size_t offset{};
    std::string resource;
    std::optional<AssetError> archive_error;
};
} //namespace simcoaster
