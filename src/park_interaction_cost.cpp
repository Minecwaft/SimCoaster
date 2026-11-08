#include "park_construction_internal.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/world_staff_interaction.hpp"
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/ui_control.hpp"
#include <utility>

namespace simcoaster {
void ParkConstructionRuntime::update_cursor_feedback(ConstructionWorld& c, WorldCellCoordinates cursor) {
    //Native674400: preserve cursor override order, including insufficient
    //funds preceding the final mode-specific presentation override.
    int help = 0, presentation = -1;
    match (c.construction.mode) {
        case 1 => {
            if (state.queue_joined) {
                presentation = 5;
            } else if (state.existing_endpoint) {
                presentation = 18;
            } else {
                c.staff.interaction.cursor_presentation = 3;
            }
            if (!(construction_detail::map(c, construction_detail::cell_id(cursor)).base.common.flags & 64u)) {
                help = 147 + (c.construction.path_start_x != -1 || c.construction.path_start_y != -1);
            }
        }
        case 3 => {
            presentation = state.queue_joined ? 5 : state.existing_endpoint ? 18 : 4;
            help = 150;
        }
        case 4 or 59 => {
            presentation = 7;
            help = state.status_latch ? 308 : construction_detail::definition(c, c.construction.selected_object_definition_id).balance->Info.HasQueue ? 362 : 146;
        }
        case 11 or 16 => {
            presentation = state.queue_joined ? 5 : state.existing_endpoint ? 18 : 6;
            help = 152;
        }
        case 21 => {
            presentation = state.existing_endpoint ? 18 : 11;
            help = state.status_latch ? 314 : 154;
        }
        case 22 => {
            presentation = state.existing_endpoint + 17;
            help = 328;
        }
        case 57 => presentation = 3;
        case 63 => {
            presentation = 11;
            help = 154;
        }
        case _ => {
        }
    } if (state.status_latch) {
        c.staff.interaction.cursor_presentation = 8;
    }
    state.cursor_help.clear();
    if (help && !c.messages.mail_language.ui_help.empty()) {
        auto text = format_bfst_wide_string(c.messages.mail_language.ui_help, c.messages.mail_language.conversion, help, 16383);
        if (!text) {
            throw std::runtime_error("Invalid original construction help: " + text.error().reason);
        }
        state.cursor_help = std::move(*text);
    }
    if (state.insufficient_funds) {
        c.staff.interaction.cursor_presentation = 20;
    }
    if (presentation != -1) {
        c.staff.interaction.cursor_presentation = presentation;
    }
    if (c.ui.persistent_root) {
        c.ui.persistent_root->cursor_id = c.staff.interaction.cursor_presentation;
    }
}

namespace {
std::optional<std::uint32_t> coaster_map(const ConstructionQuery& c) {
    return c.construction.editor ? c.construction.editor->active_map() : std::nullopt;
}

std::optional<std::size_t> coaster_node(const ConstructionQuery& c) {
    return c.construction.editor ? c.construction.editor->active_node() : std::nullopt;
}
} //namespace

void ParkConstructionRuntime::invalidate_interaction_cost_quote() noexcept {
    state.pending_interaction_quote.reset();
}

bool ParkConstructionRuntime::preview(ConstructionWorld& c, WorldCellCoordinates cursor, WorldInputModifierMask modifiers) {
    invalidate_interaction_cost_quote();
    const auto mode = c.construction.mode;
    const auto definition = c.construction.selected_object_definition_id;
    const auto accepted = preview_impl(c, cursor, modifiers);
    if (construction_detail::bounds(cursor) && mode) {
        update_cursor_feedback(c, cursor);
    } else {
        state.cursor_help.clear();
    }
    //Rejected geometry or insufficient funds can still have a meaningful
    //quote. Only absent/out-of-map/inactive/stale input suppresses publication.
    //This bridge projects the separately owned modern construction frontend;
    //it does not claim its staff handler object has native kind3 or kind4.
    if (mode && c.construction.mode == mode && c.construction.selected_object_definition_id == definition && construction_detail::bounds(cursor) && construction_detail::bounds(state.preview_coordinates)) {
        state.pending_interaction_quote = ParkInteractionCostQuote { state.cost, mode, definition, state.preview_coordinates, coaster_map(c), coaster_node(c) };
    }
    return accepted;
}

std::optional<std::int32_t> ParkConstructionRuntime::take_interaction_cost_quote(const ConstructionQuery& c) {
    const auto quote = std::exchange(state.pending_interaction_quote, std::nullopt);
    if (!quote || !c.simulation.current || !quote->mode || quote->mode != c.construction.mode || quote->definition != c.construction.selected_object_definition_id || !construction_detail::bounds(quote->cursor) || quote->coaster_map != coaster_map(c) || quote->coaster_node != coaster_node(c)) {
        return std::nullopt;
    }
    //Native54AF60 excludes staff placement/carry/patrol handlers. Construction
    //and staff share a modern pointer frontend, so an actual staff takeover
    //invalidates even a quote published earlier in the same UI frame.
    if (c.staff.interaction.initialized && c.construction.handler == &c.staff.interaction.handler) {
        const auto kind = world_staff_interaction_kind(c);
        if (kind != WorldInteractionHandlerKind::default_ && kind != WorldInteractionHandlerKind::alternate) {
            return std::nullopt;
        }
    }
    //Native674900 projects0 only after its caller establishes eligibility.
    //Do not turn an absent quote into a spurious zero-dollar display in mode3.
    //Preserve raw cost:957/key39 is already dollars for positive small values;
    //963/key47 denotes negative ticket quotes; biased refunds use958/key44.
    return c.mode.game == GameMode::code_3 ? 0 : quote->value;
}
} //namespace simcoaster
