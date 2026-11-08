#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_research.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
ObjectControlRecord* lookup(ObjectControlTable& table, std::uint16_t id, bool update_cache) {
    if (id == table.cached_definition_id) {
        return table.cached_record;
    }
    const auto count = std::bit_cast<std::int32_t>(table.count);
    for (std::int32_t i = 0; i < count; ++i) {
        auto& row = table.rows.at(static_cast<std::size_t>(i));
        if (row.definition_id == id) {
            if (update_cache) {
                table.cached_definition_id = id;
                table.cached_record = &row;
            }
            return &row;
        }
    }
    return nullptr;
}
} //namespace

ObjectControlRecord* find_and_cache_world_object_control(ObjectControlTable& table, std::uint16_t id) {
    return lookup(table, id, true);
}

ObjectControlRecord* find_world_object_control(ObjectControlTable& table, std::uint16_t id) {
    return lookup(table, id, false);
}

ObjectControlRecord* world_operations::builtin::find_object_control(WorldRuntime& c, std::uint16_t id) {
    if (!c.simulation.current) {
        throw std::invalid_argument("null object-control BASE World");
    }
    return find_and_cache_world_object_control(c.simulation.current->object_controls, id);
}

int populate_missing_world_object_controls(AttractionWorld& c, ObjectControlTable& table) {
    for (auto* definition = first_object_catalog_entry(&c.resources.object_catalog); definition; definition = next_object_catalog_entry(&c.resources.object_catalog)) {
        if (find_and_cache_world_object_control(table, definition->id)) {
            continue;
        }
        const auto index = table.count++; //Native publishes count before row stores.
        auto& row = table.rows.at(index);
        row.definition_id = definition->id;
        row.purchase_cost = definition->research.upgrades[0].cost_of_upgrade;
        row.copied_fatigue_effect = definition->usage_fatigue_effect;
        row.copied_rip_off_ok = definition->usage_rip_off_ok;
        row.research_option_index = 0;
        row.available = definition->research.upgrades[0].cost_of_research == 0;
        row.live_instance_count = 0;
        row.first_instance_game_tick = -1;
        row.placement_name_ordinal = 0;
        //Reserved row bytes and both cache fields deliberately survive appends.
    }
    return 0;
}

int reset_world_object_controls(AttractionWorld& c, ObjectControlTable& table) {
    table = {};
    return populate_missing_world_object_controls(c, table);
}
} //namespace simcoaster
