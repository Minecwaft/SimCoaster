#pragma once
#include "simcoaster/world.hpp"
#include <algorithm>

namespace simcoaster {
//Native5CD630 and constructor fast paths: the matching indexed row wins,
//otherwise scan the existing nine rows. No row is allocated or reserved.
inline TagMessageVariant* find_tag_system_message_variant_by_id(WorldRuntime& c, std::int32_t id) {
    if (id < 0 || static_cast<std::size_t>(id) >= c.messages.tag_system_message_variants.size()) {
        return nullptr;
    }
    auto& indexed = c.messages.tag_system_message_variants[static_cast<std::size_t>(id)];
    if (indexed.variant_id == id) {
        return &indexed;
    }
    const auto found = std::ranges::find(c.messages.tag_system_message_variants, id, &TagMessageVariant::variant_id);
    return found == c.messages.tag_system_message_variants.end() ? nullptr : &*found;
}
} //namespace simcoaster
