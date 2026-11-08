#include "simcoaster/world_grid.hpp"
#include <bit>
#include <span>
#include <stdexcept>

namespace simcoaster {
namespace {
enum class DashFilenamePolicy {
    load,
    skip
};

std::string mesh_directory(WorldRuntime& c, std::string_view suffix) {
    //Native sprintf reads the cache as a C string. Dynamic storage also avoids
    //the original260-byte stack path limit without altering normal path bytes.
    return std::string { build_selected_resource_levels_path(c).c_str() } + std::string { suffix };
}

void load_row(WorldRuntime& c, CellMeshTemplate& row, const std::string& directory, DefinitionGrid& geometry, std::uint32_t options, std::uint32_t key) {
    const auto definition = world_operations::load_and_register_mesh_definition_template(c, { directory, row.filename, &geometry, &geometry, options, key, 1, nullptr, nullptr, 0, 0 });
    row.definition_index = std::bit_cast<std::int32_t>(definition);
}

void load_cell_template_table(WorldRuntime& c, std::span<CellMeshTemplate> rows, std::string_view suffix, std::uint32_t key_base, std::uint32_t options, DashFilenamePolicy dash_policy) {
    auto& geometry = c.construction.cell_mesh_placement_geometry;
    geometry.width = geometry.height = 2;
    const auto directory = mesh_directory(c, suffix);
    int quickload_result = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        auto& row = rows[i];
        if (row.filename.empty()) {
            if (!i) {
                throw std::out_of_range("mesh alias has no previous template");
            }
            row.definition_index = rows[i - 1].definition_index;
        } else if (dash_policy == DashFilenamePolicy::skip && row.filename.front() == '-') {
            row.definition_index = 0;
        } else {
            if (!quickload_result) {
                quickload_result = c.ports.assets.load_quickload_index_files(c, row.filename, directory, std::nullopt);
            }
            //Re-read the row after quickload, which can reenter and edit it. A failed
            //quickload does not skip the definition load; only a nonzero result
            //suppresses later outer quickload calls in this one loader invocation.
            load_row(c, row, directory, geometry, options, key_base + static_cast<std::uint32_t>(i));
        }
    }
}

void release_template_slots(WorldRuntime& c, std::span<const CellMeshTemplate> rows, DashFilenamePolicy dash_policy) {
    for (const auto& row : rows) {
        if (!row.filename.empty() && (dash_policy == DashFilenamePolicy::load || row.filename.front() != '-')) {
            //These are the native calls to the RUNTIME slot destroyer, including
            //zero IDs and duplicates. Definition-bank entries are left untouched.
            world_operations::destroy_mesh(c, std::bit_cast<std::uint32_t>(row.definition_index));
        }
    }
}
} //namespace

const std::string& build_selected_resource_levels_path(WorldRuntime& c) {
    auto& path = c.source.selected_resource_levels_path_cache;
    path = c.source.data_directory;
    //Native normalization recognizes backslash only. Its repeated initial
    //ensure call is idempotent; a forward slash receives a backslash suffix.
    if (!path.ends_with('\\')) {
        path += '\\';
    }
    path += "levels\\";
    path += c.source.selected_resource_source_name;
    return path;
}

void load_queue_mesh_definition_templates(WorldRuntime& c) {
    auto& geometry = c.construction.queue_mesh_placement_geometry;
    geometry.width = geometry.height = 1;
    const auto directory = mesh_directory(c, "\\queue");
    int quickload_result = 0;
    for (std::size_t i = 0; i < c.construction.queue_mesh_templates.size(); ++i) {
        auto& row = c.construction.queue_mesh_templates[i];
        if (row.filename.empty()) {
            throw std::invalid_argument("null queue mesh filename");
        }
        if (!quickload_result) {
            quickload_result = c.ports.assets.load_quickload_index_files(c, row.filename, directory, std::nullopt);
        }
        const auto options = ((row.metadata >> 8) & 255u) ? 1123363u : 74787u;
        load_row(c, row, directory, geometry, options, 17000u + static_cast<std::uint32_t>(i));
    }
}

void load_gokart_mesh_definition_templates(WorldRuntime& c) {
    load_cell_template_table(c, c.construction.gokart_mesh_templates, "\\rides\\gokarts", 17100, 0x10422u, DashFilenamePolicy::skip);
}

void load_water_ride_mesh_definition_templates(WorldRuntime& c) {
    load_cell_template_table(c, c.construction.water_ride_mesh_templates, "\\rides\\wateride", 17200, 0x10422u, DashFilenamePolicy::skip);
}

void load_hoarding_mesh_definition_templates(WorldRuntime& c) {
    load_cell_template_table(c, c.construction.hoarding_mesh_templates, "\\hoarding", 17300, 0x422u, DashFilenamePolicy::load);
}

void destroy_mesh_slots_using_queue_template_indices(WorldRuntime& c) {
    release_template_slots(c, c.construction.queue_mesh_templates, DashFilenamePolicy::load);
}

void destroy_mesh_slots_using_gokart_template_indices(WorldRuntime& c) {
    release_template_slots(c, c.construction.gokart_mesh_templates, DashFilenamePolicy::skip);
}

void destroy_mesh_slots_using_water_ride_template_indices(WorldRuntime& c) {
    release_template_slots(c, c.construction.water_ride_mesh_templates, DashFilenamePolicy::load);
}

void destroy_mesh_slots_using_hoarding_template_indices(WorldRuntime& c) {
    release_template_slots(c, c.construction.hoarding_mesh_templates, DashFilenamePolicy::load);
}

void destroy_mesh_slots_using_world_template_indices(WorldRuntime& c) {
    destroy_mesh_slots_using_queue_template_indices(c);
    destroy_mesh_slots_using_gokart_template_indices(c);
    destroy_mesh_slots_using_water_ride_template_indices(c);
    destroy_mesh_slots_using_hoarding_template_indices(c);
}
} //namespace simcoaster
