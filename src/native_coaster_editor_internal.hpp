#pragma once
#include "simcoaster/world_domains_fwd.hpp"
#include "coaster_editor_internal.hpp"
#include "park_construction_internal.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/rsse_actions.hpp"
#include "simcoaster/world_park_admission.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_special_rides.hpp"

namespace simcoaster {
namespace native_coaster_detail {
void capture_preview(NativeCoasterPreview&, const coaster_editor_detail::EditTransaction&, std::size_t ride_index);

inline void require(bool value, std::string_view detail) {
    if (!value)
        throw std::logic_error(std::string{ "Native coaster editor: " } + std::string{ detail });
}

inline void checked(std::expected<void, MeshError> value) {
    if (!value)
        throw std::runtime_error(describe_mesh_error(value.error()));
}

template <class T>
T checked(std::expected<T, MeshError> value) {
    if (!value)
        throw std::runtime_error(describe_mesh_error(value.error()));
    return std::move(*value);
}

inline void record(AttractionWorld& c, LayoutAction action) {
    if (c.construction.action_recording_disabled || !c.construction.action_record_streams_initialized)
        return;
    require(c.construction.action_record_stream != nullptr, "recording stream is not bound");
    const auto result = write_layout_action(*c.construction.action_record_stream, action);
    if (!result)
        throw std::runtime_error(describe_layout_error(result.error()));
}

inline WorldMeshRuntime& meshes(ConstructionWorld& c) {
    require(c.resources.meshes != nullptr, "mesh owner is missing");
    return *c.resources.meshes;
}

inline WorldCoasterRuntime& world(AttractionWorld& c) {
    auto* value = meshes(c).coasters();
    require(value != nullptr, "coaster owner is missing");
    return *value;
}

inline WorldCellCoordinates coordinate(const CoasterPlacement& owner) {
    return { owner.tile_x, owner.tile_y };
}

inline bool equal(WorldCellCoordinates a, WorldCellCoordinates b) {
    return a.x == b.x && a.y == b.y;
}

inline void mark(WorldCoasterBinding& ride) {
    ride.train_state.runtime_flags |= 0x1001u;
    ride.geometry_dirty = true;
}

inline bool special_available(const WorldCoasterBinding& ride) {
    //46DDB5..46DE21 sets native configuration bit2 for a special-pylon filename.
    return ride.definition && ride.definition->balance.pylons[1].pcMeshFilename.has_value();
}

inline bool fixed_owner(const WorldCoasterRuntime& world, const WorldCoasterBinding& ride, std::size_t node) {
    return (world.placements.at(ride.geometry_state.at(node).placement).flags & 2u) != 0;
}

inline void disable_admission(ConstructionWorld& c, ParkObjectRuntimeView& object) {
    object.can_load = 0;
    object.person_being_loaded = 0;
    world_operations::set_script_slot(c, world_operations::find_script(c, object.script_id), 6, 1);
    world_operations::select_park_object_mesh_animation_variant(c,
                                                                object.mesh_instance_id,
                                                                ParkObjectMeshVariant::closed);
}
} //namespace native_coaster_detail

struct NativeCoasterEditor::State {
    ParkConstructionRuntime& construction;

    explicit State(ParkConstructionRuntime& owner) : construction(owner) {}
    enum class Stage : std::uint32_t {
        idle = 0,
        normal = 1,
        special = 2
    };
    enum class Edit : std::uint32_t {
        none = 0,
        cleanup = 1,
        move = 2
    };

    struct Detached {
        WorldCellCoordinates tile;
        std::uint32_t mesh{};
    };

    std::optional<std::uint32_t> map;
    std::optional<std::size_t> current, preview, validation, range_anchor, range_cursor;
    std::optional<Detached> detached;
    std::optional<NativeCoasterPreview> geometry_preview;
    std::vector<std::uint32_t> preview_signature;
    MeshPlaybackClocks preview_clocks;
    bool preview_candidate_valid{};
    Stage stage{};
    Edit edit{};
    std::uint32_t interaction{}, remaining{};
    bool edit_event_pending{}, support_refresh_pending{}, advisor_followup{};
    std::uint32_t selected_tools{};
    std::optional<CoasterEditorEntryTool> entry;
    std::optional<CoasterEditorTool> active_tool;
    CoasterEditorInputRoute input_route = CoasterEditorInputRoute::world_default;
    bool geometry_action_enabled = true;
    std::array<float, 2> drag_sensitivity{};

    struct ParameterAxis {
        CoasterParameter parameter;
        CoasterParameterRange range;
        float value{};
    };

    struct ParameterDrag {
        std::uint32_t map{}, mesh_slot{};
        std::size_t ordinal{}, geometry_count{};
        std::array<float, 2> start_pixel{};
        std::array<float, 2> sensitivity{};
        std::array<std::optional<ParameterAxis>, 2> axes;
        bool camera_relative{};
        float track_heading{};
        std::optional<std::array<float, 3>> previous_attachment;
    };

    std::optional<ParameterDrag> parameter_drag;
    std::optional<std::size_t> geometry_anchor;
    void restore_snapshot(State&&);
    WorldCellCoordinates hover_tile{ -1, -1 }, original_tile{ -1, -1 };
    float original_height{};

    WorldCoasterBinding& ride(AttractionWorld& c) {
        native_coaster_detail::require(map.has_value(), "no active context");
        auto* value = native_coaster_detail::world(c).find_map(*map);
        native_coaster_detail::require(value != nullptr, "active context was retired");
        return *value;
    }

    std::size_t ride_index(AttractionWorld& c) {
        auto& w = native_coaster_detail::world(c);
        return static_cast<std::size_t>(&ride(c) - w.bindings.data());
    }

    std::optional<std::size_t> anchor(AttractionWorld& c) {
        auto& r = ride(c);
        if (interaction & 0x100u) {
            if (!preview || !*preview)
                return {};
            const auto index = *preview - 1;
            if ((r.train_state.placement_flags & 2u) && (r.geometry.at(index).geometry_flags & 0x10000u))
                return {};
            return index;
        }
        if (r.train_state.placement_flags & 2u)
            return current;
        if (r.geometry.empty())
            return {};
        return r.geometry.size() - 1;
    }

    void clear_range(AttractionWorld& c) {
        if (!map)
            return;
        auto& r = ride(c);
        if (range_anchor && range_cursor) {
            const auto low = std::min(*range_anchor, *range_cursor), high = std::max(*range_anchor, *range_cursor);
            native_coaster_detail::require(high < r.geometry.size(), "selection endpoint is stale");
            for (auto i = low; i < high; ++i)
                r.geometry[i].geometry_flags &= ~0x1004000u;
            native_coaster_detail::mark(r);
        }
        range_anchor.reset();
        range_cursor.reset();
        r.selection_texture_bias = 0;
    }

    void pulsing(AttractionWorld& c, bool enable) {
        auto& r = ride(c);
        auto& m = native_coaster_detail::meshes(c);
        if (r.pulsing_geometry_slot) {
            if (auto* row = m.instance(*r.pulsing_geometry_slot))
                row->playback.runtime_flags &= ~0x4000000u;
            r.pulsing_geometry_slot.reset();
        }
        if (!enable)
            return;
        if (r.park_object && park_object_definition_is_marked_special(c, r.park_object)) {
            bool registered = false;
            for (std::uint32_t i = 0; i < c.attractions.special_ride_group_count; ++i)
                if (c.attractions.special_ride_groups.at(i).thing_id == r.park_object->base.header.id) {
                    registered = true;
                    break;
                }
            if (!registered)
                return;
        }
        const auto index = (r.train_state.placement_flags & 2u)
                             ? current
                             : (r.geometry.empty() ? std::nullopt : std::optional{ r.geometry.size() - 1 });
        if (index)
            if (const auto slot = r.geometry_state.at(*index).mesh_slot) {
                auto* row = m.instance(slot);
                native_coaster_detail::require(row != nullptr, "pulsing model slot is missing");
                row->playback.runtime_flags |= 0x4000000u;
                r.pulsing_geometry_slot = slot;
            }
    }

    void restore_detached(AttractionWorld& c, ParkConstructionRuntime& editor) {
        if (!detached)
            return;
        const auto copy = *detached;
        for (const auto op : std::array{ 21u, 131u, 129u })
            editor.command_line(c, op, copy.tile, copy.tile);
        construction_detail::track(c, construction_detail::cell_id(copy.tile)).base.common.mesh_instance = copy.mesh;
        detached.reset();
    }

    void cleanup(AttractionWorld& c, ParkConstructionRuntime& editor, bool restore = true) {
        finish_parameter_drag(c, true);
        geometry_preview.reset();
        preview_signature.clear();
        if (!map)
            return;
        if (restore)
            restore_detached(c, editor);
        clear_range(c);
        auto& r = ride(c);
        if (edit == Edit::cleanup) {
            r.train_state.runtime_flags &= ~4u;
            native_coaster_detail::mark(r);
            pulsing(c, true);
        }
        interaction &= ~0x4140u;
        interaction &= ~0x100u;
        remaining = 0;
        stage = Stage::idle;
        edit = Edit::none;
        preview.reset();
        edit_event_pending = false;
        support_refresh_pending = false;
        native_coaster_detail::checked(
            finalize_coaster_geometry_edit(c, native_coaster_detail::meshes(c), ride_index(c)));
    }

    void rebuild_preview(WorldRuntime&, bool update_feedback);
    bool valid_destination(AttractionWorld&, WorldCellCoordinates, std::optional<std::size_t> moving = {});
    void draw_valid_region(AttractionWorld&, ParkConstructionRuntime&);
    void delete_range(AttractionWorld&, ParkConstructionRuntime&, std::uint16_t, std::uint16_t, bool record_action);
    void backtrack(AttractionWorld&, ParkConstructionRuntime&, bool record_action);
    void finish_parameter_drag(AttractionWorld&, bool commit);
    void rebuild_parameter_preview(AttractionWorld&, bool update_feedback);
    bool single_piece_endpoint(AttractionWorld&);
    void insert_intermediate_support(AttractionWorld&,
                                     std::size_t,
                                     std::optional<WorldCellCoordinates> chosen = {},
                                     bool recording = true);
    void restore_build_tools_after_delete(AttractionWorld&);
    bool place(AttractionWorld&,
               ParkConstructionRuntime&,
               WorldCellCoordinates,
               bool charge_cost = true,
               coaster_editor_detail::PlacementValidation = coaster_editor_detail::PlacementValidation::interactive);
    void commit_move(AttractionWorld&, ParkConstructionRuntime&, WorldCellCoordinates);
};
} //namespace simcoaster
