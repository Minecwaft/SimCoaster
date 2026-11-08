#include "simcoaster/world_object_placement.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "coaster_catalog_recovery.hpp"
#include "simcoaster/coaster_construction.hpp"
#include "simcoaster/layout_world_runtime.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_object_controls.hpp"
#include <cmath>
#include <exception>
#include <limits>

namespace simcoaster {
namespace {
using namespace construction_detail;

void cardinal(int rotation) {
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)
        throw std::invalid_argument("coaster placement rotation is not cardinal");
}

WorldCellCoordinates translated(WorldCellCoordinates p, CellLocalOffset offset) {
    return { fixed::add(p.x, offset.x), fixed::add(p.y, offset.y) };
}

void checked_grid(const DefinitionGrid& grid) {
    if (grid.width <= 0 || grid.width > 40 || grid.height <= 0 || grid.height > 40)
        throw std::invalid_argument("invalid coaster placement grid");
}

//All native writers, including SETSPEED69 from the fresh mesh hook, write to
//this journal while the full World/RSSE/layout transaction remains fallible.
//Recording flags stay untouched: replay and economic branches remain native.
class ActionJournal {
    AttractionWorld& c_;
    Stream* destination_;
    LayoutActionStream pending_;
    bool active_;

public:
    explicit ActionJournal(AttractionWorld& c)
        : c_(c),
          destination_(c.construction.action_record_stream),
          active_(!c.construction.action_recording_disabled && c.construction.action_record_streams_initialized) {
        if (active_) {
            if (!destination_)
                throw std::logic_error("coaster recorder is not bound");
            c_.construction.action_record_stream = &pending_;
        }
    }

    ~ActionJournal() {
        c_.construction.action_record_stream = destination_;
    }

    void commit() {
        if (active_
            && !pending_.bytes().empty()
            && destination_->write(pending_.bytes()) != static_cast<std::int32_t>(pending_.bytes().size()))
            throw std::runtime_error("cannot publish coaster creation recording");
    }
};

struct SelectionSnapshot {
    std::uint16_t selected, previous, placement;
    std::uint32_t secondary, raw;
    ThingId owner;
    int mode;
    std::uint8_t bypass, consumed;
    std::array<std::uint8_t, 32> definition_mask;

    explicit SelectionSnapshot(AttractionWorld& c)
        : selected(c.construction.selected_object_definition_id),
          previous(c.construction.previous_object_definition_id),
          placement(c.construction.placement_object_definition_id),
          secondary(c.construction.selected_track_type),
          raw(c.construction.track_type),
          owner(c.view.secondary_cursor_record_snapshot ? c.view.secondary_cursor_record_snapshot->id : 0),
          mode(c.construction.mode),
          bypass(c.construction.coaster_source_grid_bypass),
          consumed(c.construction.prebuilt_grid_override_consumed),
          definition_mask(c.construction.save_object_definition_mask) {}

    void restore(AttractionWorld& c) const {
        c.construction.selected_object_definition_id = selected;
        c.construction.previous_object_definition_id = previous;
        c.construction.placement_object_definition_id = placement;
        c.construction.selected_track_type = secondary;
        c.construction.track_type = raw;
        c.construction.mode = mode;
        c.construction.coaster_source_grid_bypass = bypass;
        c.construction.prebuilt_grid_override_consumed = consumed;
        c.construction.save_object_definition_mask = definition_mask;
        c.construction.selected_placement_definition =
            find_object_definition_by_id(&c.resources.object_catalog, placement);
        c.construction.active_object_placement_grid = nullptr;
        c.view.secondary_cursor_record_snapshot = object(c, owner) ? &object(c, owner)->base.header : nullptr;
        c.construction.park_object = object(c, owner);
        select_object_definition_and_placement_grid(c, selected);
    }
};

std::uint32_t parent_map(AttractionWorld& c, ThingId id) {
    auto* owner = object(c, id);
    if (!owner || !c.resources.meshes)
        throw std::logic_error("coaster parent ownership is missing");
    const auto* binding =
        c.resources.meshes->coasters() ? c.resources.meshes->coasters()->find(owner->mesh_instance_id) : nullptr;
    if (!binding || binding->park_object != owner)
        throw std::logic_error("coaster map ownership is missing");
    return binding->map_id;
}

std::u16string name(const std::array<std::uint16_t, 33>& source) {
    std::u16string result;
    for (std::size_t i = 0; i < 32 && source[i]; ++i)
        result.push_back(static_cast<char16_t>(source[i]));
    return result;
}
} //namespace

bool CoasterCatalogRuntime::handles(AttractionWorld& c, std::uint16_t id) const {
    const auto* d = find_object_definition_by_id(&c.resources.object_catalog, id);
    return d && is_coaster_catalog_definition(*d);
}

CoasterCatalogRuntime::Selection& CoasterCatalogRuntime::resolve(AttractionWorld& c, std::uint16_t id) {
    if (selection_ && (selection_->catalog == id || selection_->base == id))
        return *selection_;
    auto& d = definition(c, id);
    if (!is_coaster_catalog_definition(d) || !d.balance->Info.Shape)
        throw std::invalid_argument("definition is not a coaster catalog parent");
    Selection value;
    value.catalog = value.base = id;
    if (d.balance->Prebuilt.IsPrebuiltCoaster) {
        value.base = static_cast<std::uint16_t>(d.balance->Prebuilt.PrebuiltCoasterBase);
        auto& base = definition(c, value.base);
        if (!is_coaster_catalog_definition(base) || !base.balance->Info.Shape.get())
            throw std::invalid_argument("predefined coaster base is not a coaster");
        auto found = predefined_cache_.find(id);
        if (found == predefined_cache_.end())
            found = predefined_cache_.emplace(id, load_predefined_coaster_balance(d)).first;
        if (!found->second)
            throw found->second.error();
        value.predefined = *found->second;
    }
    checked_grid(*d.balance->Info.Shape);
    value.preview_grid = *d.balance->Info.Shape;
    selection_ = std::move(value);
    return *selection_;
}

std::expected<void, CoasterCatalogError> CoasterCatalogRuntime::available(AttractionWorld& c, std::uint16_t id) {
    try {
        const auto* d = find_object_definition_by_id(&c.resources.object_catalog, id);
        if (!d || !d->balance)
            return std::unexpected(CoasterCatalogError{ CoasterCatalogFailure::missing_definition, {}, {} });
        if (!is_coaster_catalog_definition(*d))
            return std::unexpected(CoasterCatalogError{ CoasterCatalogFailure::invalid_source, {}, {} });
        if (d->balance->Prebuilt.IsPrebuiltCoaster) {
            auto found = predefined_cache_.find(id);
            if (found == predefined_cache_.end())
                found = predefined_cache_.emplace(id, load_predefined_coaster_balance(*d)).first;
            if (!found->second)
                return std::unexpected(found->second.error());
            const auto* base =
                find_object_definition_by_id(&c.resources.object_catalog,
                                             static_cast<std::uint16_t>(d->balance->Prebuilt.PrebuiltCoasterBase));
            if (!base || !base->balance || !base->balance->Info.Shape || !is_coaster_catalog_definition(*base))
                return std::unexpected(CoasterCatalogError{ CoasterCatalogFailure::missing_definition, {}, {} });
        }
        return {};
    } catch (const std::exception&) {
        return std::unexpected(CoasterCatalogError{ CoasterCatalogFailure::invalid_source, {}, {} });
    }
}

std::int32_t CoasterCatalogRuntime::cost(AttractionWorld& c, const Selection& selection) const {
    if (selection.build_quote_override && c.mode.game != GameMode::code_3)
        return *selection.build_quote_override;
    if (selection.design && c.mode.game != GameMode::code_3 && selection.design->build_cost)
        return static_cast<std::int32_t>(selection.design->build_cost);
    return definition(c, selection.catalog).balance->Upgrades[0].CostOfUpgrade;
}

void CoasterCatalogRuntime::begin(AttractionWorld& c, std::uint16_t id, int rotation) {
    cardinal(rotation);
    if (selection_ && selection_->catalog != id && selection_->base != id)
        clear(c);
    auto& selection = resolve(c, id);
    auto& d = definition(c, selection.catalog);
    if (c.construction.editor && c.construction.editor->active_map()) {
        c.construction.editor->cancel(c, construction_);
        c.construction.editor->activate(c, 0, true);
    }
    if (!selection.design) {
        c.construction.coaster_source_is_plain = 1;
        c.construction.prebuilt_grid_override_valid = c.construction.prebuilt_grid_override_consumed = 0;
    }
    RestoreValue selecting{ internal_selection_ };
    internal_selection_ = true;
    apply_set_ride_action(c, &d.id, 0);
    set_construction_track_type(c, d.which_track_type, true);
    select_object_definition_and_placement_grid(c, d.id);
    construction_.state.rotation_degrees = rotation;
    if (!c.construction.action_recording_disabled && c.construction.action_record_streams_initialized) {
        if (!c.construction.action_record_stream)
            throw std::logic_error("coaster recorder is not bound");
        const auto recorded =
            write_layout_action(*c.construction.action_record_stream,
                                { LayoutActionCode::set_rotation, LayoutWord{ static_cast<std::uint16_t>(rotation) } });
        if (!recorded)
            throw std::runtime_error(describe_layout_error(recorded.error()));
    }
    construction_.state.candidate_overwrite_priority = d.balance->Info.OverwritePriority;
    construction_.state.queue_placement_enabled = d.balance->Info.HasQueue != 0;
    construction_.set_mode(c, 4, true);
}

void CoasterCatalogRuntime::select_design(AttractionWorld& c,
                                          CoasterDesign design,
                                          int rotation,
                                          std::optional<std::int32_t> build_quote) {
    cardinal(rotation);
    if (design.definition_id > std::numeric_limits<std::uint16_t>::max()
        || design.build_cost > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
        || !design.grid_width
        || design.grid_width > 40
        || !design.grid_height
        || design.grid_height > 40
        || design.placement_offset_x < -127
        || design.placement_offset_x > 127
        || design.placement_offset_y < -127
        || design.placement_offset_y > 127)
        throw std::invalid_argument("invalid COS placement header");
    if (design.source_index != static_cast<std::uint32_t>(c.ports.assets.selected_resource_source(c)))
        throw std::invalid_argument("coaster design belongs to a different resource theme");
    const auto id = static_cast<std::uint16_t>(design.definition_id);
    auto& d = definition(c, id);
    if (!is_coaster_catalog_definition(d) || d.balance->Prebuilt.IsPrebuiltCoaster)
        throw std::invalid_argument("COS definition is not a base coaster");
    for (const auto& row : design.geometry)
        if (row.tile_offset_x < -127
            || row.tile_offset_x > 127
            || row.tile_offset_y < -127
            || row.tile_offset_y > 127
            || !std::isfinite(row.loft + row.heading + row.secondary_parameter + row.angle_b))
            throw std::invalid_argument("invalid COS geometry");
    clear(c);
    Selection selection;
    selection.build_quote_override = build_quote;
    selection.catalog = selection.base = id;
    selection.preview_grid.width = static_cast<int>(design.grid_width);
    selection.preview_grid.height = static_cast<int>(design.grid_height);
    for (std::size_t i = 0; i < design.grid.size(); ++i)
        selection.preview_grid.cells[i] = { std::bit_cast<std::int32_t>(design.grid[i].code),
                                            design.grid[i].attribute };
    selection.design = std::move(design);
    selection_ = std::move(selection);
    c.construction.prebuilt_coaster_placement_grid = selection_->preview_grid;
    c.construction.coaster_source_picker_grid = selection_->preview_grid;
    c.construction.prebuilt_grid_override_valid = 1;
    c.construction.prebuilt_grid_override_consumed = 0;
    c.construction.coaster_source_grid_bypass = 0;
    c.construction.coaster_source_is_plain = 0;
    begin(c, id, rotation);
}

void CoasterCatalogRuntime::clear(AttractionWorld& c) {
    selection_.reset();
    c.construction.prebuilt_grid_override_valid = c.construction.prebuilt_grid_override_consumed = 0;
    c.construction.coaster_source_grid_bypass = 0;
    c.construction.coaster_source_is_plain = 1;
    c.construction.active_object_placement_grid = nullptr;
}

void CoasterCatalogRuntime::observe_set_ride_action(AttractionWorld& c) {
    if (!internal_selection_)
        clear(c);
}

void CoasterCatalogRuntime::reset_after_restore() {
    selection_.reset();
    internal_selection_ = false;
}

bool CoasterCatalogRuntime::preview(AttractionWorld& c, std::uint16_t id, WorldCellCoordinates p, int rotation) {
    cardinal(rotation);
    auto& selection = resolve(c, id);
    auto& selected = definition(c, selection.catalog);
    auto& state = construction_.state;
    if (!bounds(p)) {
        state.validation_failed = state.status_latch = true;
        return false;
    }
    state.faces.clear();
    state.validation_failed = state.status_latch = state.stop = false;
    state.rotation_degrees = rotation;
    state.placement_from_origin = false;
    state.candidate_overwrite_priority = selected.balance->Info.OverwritePriority;
    state.queue_placement_enabled = selection.predefined || selected.balance->Info.HasQueue != 0;
    //Hover never emits SET_RIDE records or mutates the downloaded-definition
    //mask.
    c.construction.selected_object_definition_id = selected.id;
    c.construction.selected_track_type = 0;
    set_construction_track_type(c, 3, false);
    DefinitionGrid* grid = selection.design ? &selection.preview_grid
                         : selected.balance->DefaultCoaster.AddDefaultPylons
                             ? selected.balance->DefaultCoaster.CoasterPlacementShape.get()
                             : selected.balance->Info.Shape.get();
    if (!grid)
        throw std::invalid_argument("coaster preview shape is missing");
    checked_grid(*grid);
    bool allowed = true;
    const auto* control = find_world_object_control(world(c).object_controls, selection.catalog);
    if (selected.balance->UsageInfo.SingleInstance && control && control->live_instance_count)
        allowed = false;
    if (selected.balance->Info.IsSpecial == 1
        && c.attractions.special_ride_group_count >= c.attractions.special_ride_groups.size())
        allowed = false;
    const auto total = cost(c, selection);
    bool valid;
    {
        RestoreValue suppressed{ c.construction.cell_costs_suppressed };
        c.construction.cell_costs_suppressed = 1;
        valid = construction_.process_grid(c, p, rotation, total, false, true, grid).has_value()
             && !state.validation_failed;
        state.validation_failed = state.status_latch = state.stop = false;
        (void)construction_.process_grid(c, p, rotation, total, false, false, grid);
    }
    const bool funds = c.construction.action_recording_disabled || total <= bank(c).balance;
    state.cost = total;
    state.insufficient_funds = !funds;
    valid = valid && allowed && funds;
    state.validation_failed = state.status_latch = !valid;
    if (!valid)
        for (auto& face : state.faces)
            face.material = funds ? 1u : 15u;
    return valid;
}

std::optional<ParkPlacementResult>
CoasterCatalogRuntime::place(AttractionWorld& c, std::uint16_t id, WorldCellCoordinates cursor, int rotation) {
    cardinal(rotation);
    auto& selection = resolve(c, id);
    if (!preview(c, id, cursor, rotation))
        return std::nullopt;
    if (!c.construction.editor || !c.resources.meshes)
        throw std::logic_error("coaster catalog owners are not bound");
    const auto selected = selection; //Owned values survive catalog/source callbacks.
    auto& state = construction_.state;
    const auto preview_origin = state.placement_origin;
    const PlacementRecovery recovery{ c };
    const SelectionSnapshot snapshot{ c };
    const auto previous_state = state;
    auto* analyser_header = c.entities.at(world(c).saved.park_analyser).payload;
    if (!analyser_header)
        throw std::logic_error("coaster construction analyser is missing");
    auto& analyser = *reinterpret_cast<ParkAnalyserState*>(analyser_header);
    const auto old_built = analyser.last_object_built;
    const auto old_history = analyser.long_histories[12].temporary;
    const auto old_groups = c.attractions.special_ride_group_count;
    ActionJournal journal{ c };
    ThingId provisional{};
    try {
        c.construction.editor->cancel(c, construction_);
        if (c.construction.editor->active_map())
            c.construction.editor->activate(c, 0, true);
        const auto& catalog = definition(c, selected.catalog);
        auto& base = definition(c, selected.base);
        auto input = cursor;
        if (selected.design) {
            input = translated(preview_origin,
                               rotate_park_object_local_cell_offset(
                                   { selected.design->placement_offset_x, selected.design->placement_offset_y },
                                   rotation));
            state.placement_from_origin = true;
            c.construction.prebuilt_grid_override_consumed = 1;
            if (c.mode.game == GameMode::code_3)
                c.construction.coaster_source_grid_bypass = 1;
        } else if (selected.predefined) {
            input =
                translated(input,
                           { catalog.balance->Prebuilt.PlacementOffsetX, catalog.balance->Prebuilt.PlacementOffsetY });
        } else if (base.balance->DefaultCoaster.AddDefaultPylons) {
            input = translated(input,
                               rotate_park_object_local_cell_offset({ base.balance->DefaultCoaster.PlacementOffsetX,
                                                                      base.balance->DefaultCoaster.PlacementOffsetY },
                                                                    rotation));
            if (c.mode.game == GameMode::code_3)
                c.construction.coaster_source_grid_bypass = 1;
        }
        RestoreValue selecting{ internal_selection_ };
        internal_selection_ = true;
        apply_set_ride_action(c, &base.id, 0);
        set_construction_track_type(c, 3, true);
        state.queue_placement_enabled = selected.predefined || base.balance->Info.HasQueue != 0;
        state.candidate_overwrite_priority = base.balance->Info.OverwritePriority;
        state.validation_failed = state.status_latch = state.stop = false;
        RestoreValue suppressed{ c.construction.cell_costs_suppressed };
        c.construction.cell_costs_suppressed = 1;
        //The native preview grid and parent grid deliberately differ. Revalidate
        //the actual parent shape before any overwrite, ID, RSSE or bank mutation.
        if (!construction_
                 .process_grid(c, input, rotation, cost(c, selected), false, true, base.balance->Info.Shape.get())
            || state.validation_failed)
            throw std::invalid_argument("coaster parent footprint is not placeable");
        state.validation_failed = state.status_latch = state.stop = false;
        const auto placed =
            construction_
                .process_grid(c, input, rotation, cost(c, selected), true, false, base.balance->Info.Shape.get());
        if (!placed)
            throw std::logic_error("validated coaster parent commit failed");
        provisional = placed->created_object;
        state.placement_from_origin = false;
        (void)parent_map(c, placed->created_object);
        if (selected.design) {
            build_imported_coaster_catalog_layout(c, construction_, placed->created_object, *selected.design);
            if (c.mode.game == GameMode::code_3)
                c.construction.editor->complete_supports(c);
            else {
                c.construction.editor->clear_catalog_context(c);
                auto* owner = object(c, placed->created_object);
                owner->prebuilt_track_open = 1;
                const auto key = cell_id({ owner->base.x >> 8, owner->base.y >> 8 });
                apply_layout_object_name(c, LayoutActionCode::name_a, key, name(selected.design->name_a));
                apply_layout_object_name(c, LayoutActionCode::name_b, key, name(selected.design->name_b));
            }
        } else if (selected.predefined) {
            build_predefined_coaster_catalog_layout(c, construction_, placed->created_object, *selected.predefined);
            object(c, placed->created_object)->prebuilt_track_open = 1;
        } else if (base.balance->DefaultCoaster.AddDefaultPylons)
            build_default_coaster_catalog_layout(c, construction_, placed->created_object);
        state.result = *placed;
        construction_.finish_placement(c, *placed);
        c.construction.coaster_source_grid_bypass = 0;
        state.placement_from_origin = false;
        journal.commit();
        return placed;
    } catch (...) {
        const auto failure = std::current_exception();
        if (c.construction.editor->active_map()) {
            c.construction.editor->cancel(c, construction_);
            c.construction.editor->clear_catalog_context(c);
        }
        if (!provisional)
            provisional = state.result.created_object;
        if (auto* owner = object(c, provisional)) {
            (void)retire_world_used_thing(c, &owner->base.header);
            if (c.entities.removals.tail && c.entities.removals.tail->id == provisional) {
                ThingId removed{};
                pop_pending_world_used_thing_removal(c, &removed);
                unlink_and_free_world_used_thing(c, &c.entities.at(removed));
            }
        }
        recovery.restore(c);
        analyser.last_object_built = old_built;
        analyser.long_histories[12].temporary = old_history;
        while (c.attractions.special_ride_group_count > old_groups)
            c.attractions.special_ride_groups.at(--c.attractions.special_ride_group_count) = {};
        snapshot.restore(c);
        state = previous_state;
        state.result = {};
        state.placement_from_origin = false;
        construction_.refresh_origin(c, false);
        std::rethrow_exception(failure);
    }
}
} //namespace simcoaster
