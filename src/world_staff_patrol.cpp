#include "simcoaster/world_staff_interaction.hpp"
#include "world_staff_internal.hpp"

namespace simcoaster {
using namespace staff_detail;

namespace {
std::array<CellId, 2> endpoints(const StaffAreaBounds& b) {
    return { static_cast<CellId>(static_cast<std::uint32_t>(b.start_x) + static_cast<std::uint32_t>(b.start_y) * 128u + 1u), static_cast<CellId>(static_cast<std::uint32_t>(b.end_x) + static_cast<std::uint32_t>(b.end_y) * 128u + 1u) };
}

bool custom_bounds(const StaffRuntimeRecord& s) {
    const auto pair = endpoints(s.area.bounds);
    return pair[0] != 1 || pair[1] != map_cell_count;
}

void emit_area(StaffWorld& c, const StaffAreaMembership& area, std::uint8_t material) {
    const auto& b = area.bounds;
    const auto x0 = std::min(b.start_x, b.end_x), x1 = std::max(b.start_x, b.end_x), y0 = std::min(b.start_y, b.end_y), y1 = std::max(b.start_y, b.end_y);
    const auto emit = [&](int x, int y) {
        if (x >= 0 && x < 128 && y >= 0 && y < 128) {
            c.staff.interaction.overlays.push_back({ static_cast<CellId>(x + y * 128 + 1), material });
        }
    };
    for (int x = x0; x <= x1; ++x) {
        emit(x, y0);
    }
    if (y0 < y1) {
        for (int x = x0; x <= x1; ++x) {
            emit(x, y1);
        }
    }
    if (y1 - y0 > 1) {
        for (int y = y0 + 1; y < y1; ++y) {
            emit(x0, y);
        }
        if (x0 < x1) {
            for (int y = y0 + 1; y < y1; ++y) {
                emit(x1, y);
            }
        }
    }
    if (area.begin) {
        for (auto* cell_id = area.begin; cell_id != area.end; ++cell_id) {
            c.staff.interaction.overlays.push_back({ *cell_id, material });
        }
    }
}
} //namespace

StaffAreaResizeHandle classify_staff_area_selection_resize_handle(const StaffAreaSelectionInteraction& e, std::int32_t x, std::int32_t y) {
    if (!e.selection_active) {
        return StaffAreaResizeHandle::none;
    }
    const auto& b = e.selection;
    if (x == b.start_x) {
        if (y == b.start_y) {
            return StaffAreaResizeHandle::left_top;
        }
        if (y == b.end_y) {
            return StaffAreaResizeHandle::left_bottom;
        }
        return y > b.start_y && y < b.end_y ? StaffAreaResizeHandle::left : StaffAreaResizeHandle::none;
    }
    if (x == b.end_x) {
        if (y == b.start_y) {
            return StaffAreaResizeHandle::right_top;
        }
        if (y == b.end_y) {
            return StaffAreaResizeHandle::right_bottom;
        }
        return y > b.start_y && y < b.end_y ? StaffAreaResizeHandle::right : StaffAreaResizeHandle::none;
    }
    if (y == b.start_y && x > b.start_x && x < b.end_x) {
        return StaffAreaResizeHandle::top;
    }
    if (y == b.end_y && x > b.start_x && x < b.end_x) {
        return StaffAreaResizeHandle::bottom;
    }
    return x > b.start_x && x < b.end_x && y > b.start_y && y < b.end_y ? StaffAreaResizeHandle::interior : StaffAreaResizeHandle::none;
}

std::uint32_t staff_area_resize_cursor(bool active, StaffAreaResizeHandle handle, float heading) {
    if (!active) {
        return 0;
    }
    double angle = heading;
    if (angle > 0) {
        angle -= std::bit_cast<float>(std::uint32_t { 1086918619 });
    }
    const auto quarter = static_cast<std::size_t>(integer(std::fabs((angle - std::bit_cast<float>(std::uint32_t { 1053364187 })) * std::bit_cast<float>(std::uint32_t { 1067645315 }))) % 4);
    // clang-format off
  return match(handle)->std::uint32_t {
    case StaffAreaResizeHandle::left_bottom or StaffAreaResizeHandle::right_top=>std::array<std::uint32_t,4>{24,23,25,22}[quarter];
    case StaffAreaResizeHandle::left_top or StaffAreaResizeHandle::right_bottom=>std::array<std::uint32_t,4>{25,22,24,23}[quarter];
    case StaffAreaResizeHandle::left or StaffAreaResizeHandle::right=>std::array<std::uint32_t,4>{22,24,23,25}[quarter];
    case StaffAreaResizeHandle::top or StaffAreaResizeHandle::bottom=>std::array<std::uint32_t,4>{23,25,22,24}[quarter];
    case StaffAreaResizeHandle::interior=>21;
    case _=>0;
  };
    // clang-format on
}

bool update_staff_area_selection_validity(StaffWorld& c, StaffAreaSelectionInteraction& e) {
    const auto& b = e.selection;
    unsigned blocked = 0;
    //Native checks every corner, including duplicates of one-cell selections.
    for (auto point : std::array<std::array<int, 2>, 4> { { { b.start_x, b.start_y }, { b.start_x, b.end_y }, { b.end_x, b.start_y }, { b.end_x, b.end_y } } }) {
        const auto id = static_cast<CellId>(static_cast<std::uint32_t>(point[0]) + static_cast<std::uint32_t>(point[1]) * 128u + 1u);
        if (!id || id > map_cell_count) {
            throw std::out_of_range("staff patrol corner");
        }
        blocked += (primary(c).map_cells[id - 1].base.common.flags & 64u) != 0;
    }
    e.confirm_enabled = blocked < 2;
    return e.confirm_enabled;
}

void render_staff_area_selection_overlay(StaffWorld& c, StaffAreaSelectionInteraction& e) {
    e.preview.end = e.preview.begin;
    e.preview.bounds = e.selection;
    refresh_world_staff_area_membership_for_bounds(c, e.preview);
    const auto valid = update_staff_area_selection_validity(c, e);
    //6259C0 performs a full mode transition for each overlay, followed
    //by 4D481E restoring the editor's mode. These are not raw writes:
    //they also invalidate pending construction input and tool state.
    c.ports.construction.set_world_interaction_mode(c, static_cast<WorldInteractionMode>(valid ? e.render_mode : 137u), false);
    emit_area(c, e.preview, valid ? 9u : 1u);
    for (const auto id : e.preview_staff) {
        const auto* payload = c.entities.at(id).payload;
        if (payload && is_staff_record_kind(payload->kind)) {
            c.ports.construction.set_world_interaction_mode(c, static_cast<WorldInteractionMode>(136), false);
            emit_area(c, reinterpret_cast<const StaffRuntimeRecord*>(payload)->area, 0);
        }
    }
    c.ports.construction.set_world_interaction_mode(c, static_cast<WorldInteractionMode>(e.render_mode), false);
}

void update_staff_area_selection_from_cursor(StaffWorld& c, StaffAreaSelectionInteraction& e, CellId cursor, bool dragging) {
    const auto index = static_cast<std::uint32_t>(cursor) - 1u;
    const auto x = static_cast<int>(index & 127u), y = static_cast<int>(index >> 7);
    auto& b = e.selection;
    if (dragging) {
        if (e.selection_active) {
            // clang-format off
          match(e.resize_handle) {
        case StaffAreaResizeHandle::left_bottom=>{b.start_x=x;
        b.end_y=y;}
        case StaffAreaResizeHandle::right_top=>{b.end_x=x;
        b.start_y=y;}
        case StaffAreaResizeHandle::left_top=>{b.start_x=x;
        b.start_y=y;}
        case StaffAreaResizeHandle::right_bottom=>{b.end_x=x;
        b.end_y=y;}
        case StaffAreaResizeHandle::left=>b.start_x=x;
        case StaffAreaResizeHandle::right=>b.end_x=x;
        case StaffAreaResizeHandle::top=>b.start_y=y;
        case StaffAreaResizeHandle::bottom=>b.end_y=y;
        case StaffAreaResizeHandle::interior=>{
          const auto previous=static_cast<std::uint32_t>(e.drag_origin)-1u;
          const auto dx=x-static_cast<int>(previous&127u),dy=y-static_cast<int>(previous>>7);
          const auto& a=e.drag_anchor;
          const StaffAreaBounds translated{a.start_x+dx,a.start_y+dy,a.end_x+dx,a.end_y+dy};
          if(translated.start_x>=0 && translated.end_x<128 && translated.start_y>=0 && translated.end_y<128)b=translated;
        }
        case _=>;
      }
            // clang-format on
            if (b.start_x > b.end_x)
                std::swap(b.start_x, b.end_x);
            if (b.start_y > b.end_y) {
                std::swap(b.start_y, b.end_y);
            }
        } else {
            //Native MOV[+58],X and MOV[+54],Y. The second write is the first Y
            //endpoint, rather than the tempting but incorrect +5C/end-Y field.
            b.end_x = x;
            b.start_y = y;
        }
    } else if (!e.selection_active) {
        b = { x, y, x, y };
    }
    e.resize_handle = classify_staff_area_selection_resize_handle(e, x, y);
    c.staff.interaction.cursor_presentation = staff_area_resize_cursor(e.selection_active, e.resize_handle, c.view.camera.heading_radians);
    render_staff_area_selection_overlay(c, e);
}

void begin_staff_area_selection(StaffWorld& c, StaffRuntimeRecord& s, std::uint32_t mode) {
    initialize_world_staff_interaction(c);
    if (!c.staff.interaction.cursor.cell && !custom_bounds(s)) {
        throw std::logic_error("staff patrol selection requires a cursor cell");
    }
    StaffAreaSelectionInteraction editor;
    editor.target = s.base.header.id;
    editor.render_mode = mode;
    enter_world_staff_state(c, s, StaffState::frozen_editing_patrol_area);
    cancel_world_staff_interaction(c);
    for (auto id = head(primary(c), world_staff_type_for_used_thing(s.base.header)); id;) {
        auto& other = record<StaffRuntimeRecord>(c, id);
        const auto next = other.next_staff;
        if (id != s.base.header.id && custom_bounds(other)) {
            editor.preview_staff.push_back(id);
            const auto pair = endpoints(other.area.bounds);
            set_world_object_staff_area_bounds_from_packed_cells(c, other, pair[0], pair[1]);
        }
        id = next;
    }
    if (custom_bounds(s)) {
        const auto pair = endpoints(s.area.bounds);
        const auto first = static_cast<std::uint32_t>(pair[0]) - 1u, last = static_cast<std::uint32_t>(pair[1]) - 1u;
        editor.selection = { static_cast<int>(first & 127u), static_cast<int>(first >> 7), static_cast<int>(last & 127u), static_cast<int>(last >> 7) };
        editor.selection_active = true;
        editor.caption_row = 108;
    } else {
        const auto cursor = static_cast<std::uint32_t>(c.staff.interaction.cursor.cell) - 1u;
        const auto x = static_cast<int>(cursor & 127u), y = static_cast<int>(cursor >> 7);
        editor.selection = { x, y, x, y };
    }
    editor.panel_open = true;
    c.staff.interaction.handler = { std::move(editor), false };
    c.construction.handler = &c.staff.interaction.handler;
    c.staff.interaction.left_button_held = false;
    c.staff.interaction.cursor_presentation = 0;
    c.staff.interaction.overlays.clear();
    render_staff_area_selection_overlay(c, std::get<StaffAreaSelectionInteraction>(c.staff.interaction.handler.value));
}

void commit_staff_area_selection_to_target(StaffWorld& c, StaffAreaSelectionInteraction& e) {
    if (!e.selection_active) {
        return;
    }
    auto& s = record<StaffRuntimeRecord>(c, e.target);
    if (update_staff_area_selection_validity(c, e)) {
        const auto pair = endpoints(e.selection);
        set_world_object_staff_area_bounds_from_packed_cells(c, s, pair[0], pair[1]);
    } else {
        reset_world_object_staff_area_bounds_to_full_map(s);
    }
}

void close_staff_area_selection_panel(StaffWorld& c, bool confirm) {
    auto* e = std::get_if<StaffAreaSelectionInteraction>(&c.staff.interaction.handler.value);
    if (!e || !e->panel_open) {
        return;
    }
    if (confirm) {
        commit_staff_area_selection_to_target(c, *e);
    }
    e->panel_open = false;
    if (e->target) {
        enter_world_staff_state(c, record<StaffRuntimeRecord>(c, e->target), StaffState::frozen);
    }
    //4D48F2 calls 673230(0,0), whose mode-zero branch replaces the active
    //handler. The shared modal context's old pointer is not active ownership.
    //This call can destroy *e; do not access it afterward.
    c.ports.construction.set_world_interaction_mode(c, WorldInteractionMode::none, false);
}
} //namespace simcoaster
