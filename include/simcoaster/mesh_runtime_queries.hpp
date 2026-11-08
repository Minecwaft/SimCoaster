#pragma once
#include <cstdint>

namespace simcoaster {
struct MeshRuntimeOpeningView {
    std::uint32_t geometry_peer_reference_count, placement_flags;
};
} //namespace simcoaster
