#pragma once
#include "simcoaster/mesh_model.hpp"
#include <algorithm>

namespace simcoaster {
//Stack traversal visits siblings in source order. Append directly to its
//retained buffer, then reverse only this node's children for LIFO traversal.
template <class Require>
void append_mesh_children(const MeshModel& model, const MeshNode& node, std::vector<std::size_t>& pending, Require require) {
    const auto first = pending.size();
    for (auto child = node.first_child; child; child = model.nodes[*child].next_sibling) {
        require(*child < model.nodes.size() && pending.size() - first < model.nodes.size());
        pending.push_back(*child);
    }
    std::reverse(pending.begin() + static_cast<std::ptrdiff_t>(first), pending.end());
}
} //namespace simcoaster
