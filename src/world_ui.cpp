#include "simcoaster/world_ui.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_staff.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <variant>

namespace simcoaster {
namespace {
ThingHeader* payload(WorldRuntime& c, ThingId id) {
    return c.entities.at(id).payload;
}

template <class T>
T& required_record(WorldRuntime& c, ThingId id) {
    auto* p = payload(c, id);
    if (!p) {
        throw std::logic_error("World UI event refers to a missing record");
    }
    return *reinterpret_cast<T*>(p);
}

std::u16string literal(WorldRuntime& c, const std::vector<std::byte>& table, std::uint32_t row) {
    auto result = format_bfst_wide_string(table, c.messages.mail_language.conversion, row, 16383);
    if (!result) {
        throw std::runtime_error("World UI caption: " + result.error().reason);
    }
    return std::move(*result);
}

std::u16string role_caption(WorldRuntime& c, const ThingHeader& record) {
    const auto type = world_staff_type_for_used_thing(record);
    if (type < StaffType::handyman || type > StaffType::gardener) {
        throw std::logic_error("World UI roster record is not staff");
    }
    return literal(c, c.messages.mail_language.staff_types, 2u * static_cast<std::uint32_t>(type));
}

std::u16string staff_name(const StaffRuntimeRecord& s) {
    return { s.name.begin(), std::ranges::find(s.name, char16_t {}) };
}

std::int32_t percentage_meter(float value) {
    //Native keeps the low BYTE after truncation, rather than clamping a percent.
    return (static_cast<std::uint8_t>(fixed::truncate_integer(value)) << 10) / 100;
}

using SortKey = std::variant<std::int32_t, std::u16string_view>;

SortKey key(const StaffRoomRosterRow& row, std::int16_t column) {
    match (column) {
        case 0 => return std::u16string_view { row.name };
        case 1 => return std::u16string_view { row.role };
        case 2 => return row.energy_meter;
        case _ => throw std::out_of_range("staff-room sort column");
    }
}

SortKey key(const VisitorListRow& row, std::int16_t column) {
    match (column) {
        case 0 => return std::bit_cast<std::int32_t>(row.arrival_index);
        case 1 => return row.generation_caption ? std::u16string_view { *row.generation_caption } : std::u16string_view {};
        case 2 => return row.happiness_meter;
        case 3 => return row.thought_index;
        case _ => throw std::out_of_range("visitor-list sort column");
    }
}

template <class Row>
void sort_and_refresh(WorldUiList<Row>& list) {
    //Native signed sort key0 leaves insertion order intact. The model encodes
    //that state as column-1 while retaining the last direction independently.
    if (list.sort_column >= 0 && (list.sort_direction == UiListSortDirection::ascending || list.sort_direction == UiListSortDirection::descending)) {
        std::ranges::stable_sort(list.rows, [&](const auto& a, const auto& b) {
            const auto lhs = key(a, list.sort_column), rhs = key(b, list.sort_column);
            return list.sort_direction == UiListSortDirection::ascending ? lhs < rhs : rhs < lhs;
        });
    }
    ++list.revision;
}

void ensure_selector(WorldRuntime& c) {
    if (!c.ui.panels.mode_selector_initialized) {
        c.ui.panels.mode_selector_initialized = true;
        initialize_game_mode_selector(c);
    }
}

void refresh_ride(WorldRuntime& c, ThingId id) {
    const auto& panel = c.ui.panels.ride_settings;
    if (panel.open) {
        if (const auto* p = payload(c, id); p && p == c.ui.panels.selected_record) {
            if (!panel.refresh_statistics_and_status) {
                throw std::logic_error("open ride-settings panel has no statistics binding");
            }
            const auto refresh = panel.refresh_statistics_and_status;
            refresh(c);
        }
    }
}

void remove_visitor(WorldRuntime& c, ThingId id) {
    const auto& person = required_record<ThingHeader>(c, id);
    if (person.kind != ThingKind::person) {
        return;
    }
    auto& profile = c.ui.panels.visitor_profile;
    if (profile.open && &person == c.ui.panels.selected_record) {
        if (!profile.close_panel) {
            throw std::logic_error("open visitor profile has no close-event binding");
        }
        const auto close = profile.close_panel;
        close(c);
    }
    remove_visitor_list_person(c, person);
    auto& locator = c.ui.panels.visitor_locator;
    if (locator.open && locator.selected_person == &person) {
        locator.selected_person = nullptr;
        locator.selected_filter = -1;
        refresh_visitor_locator_filter_buttons(c);
    }
}
} //namespace

void append_matching_staff_room_roster_member(WorldRuntime& c, StaffRuntimeRecord& staff) {
    auto& list = c.ui.panels.staff_room;
    if (!list.open) {
        return;
    }
    const auto* selected = c.ui.panels.selected_record;
    if (!selected) {
        throw std::logic_error("open staff-room roster has no selected room");
    }
    if (staff.state == StaffState::resting && staff.rest_area == selected->id) {
        list.rows.push_back({ &staff.base.header, staff.base.header.id, staff_name(staff), role_caption(c, staff.base.header), percentage_meter(staff.energy) });
    }
    //This refresh is unconditional once the panel is open, even on rejection.
    sort_and_refresh(list);
}

void remove_staff_room_roster_member(WorldRuntime& c, const ThingHeader& staff) {
    auto& list = c.ui.panels.staff_room;
    if (!list.open) {
        return;
    }
    const auto row = std::ranges::find(list.rows, &staff, &StaffRoomRosterRow::record);
    if (row != list.rows.end()) {
        list.rows.erase(row);
        ++list.revision;
    }
}

void refresh_training_room_rows_and_availability(WorldRuntime& c) {
    auto& panel = c.ui.panels.training_room;
    if (!panel.open) {
        return;
    }
    std::int32_t pending_count = 0;
    panel.rows = {};
    for (std::size_t i = 0; i < panel.rows.size(); ++i) {
        const auto& pending = c.staff.interaction.training_pending[i];
        if (!pending.staff) {
            continue;
        }
        //Native's hide cursor advances for every slot, but its display cursor
        //advances only for occupied slots (593817/5938EF). Rows compact without
        //moving the pending array; each widget's context retains its source slot.
        auto& display = panel.rows[static_cast<std::size_t>(pending_count++)];
        display.pending_index = static_cast<std::uint8_t>(i);
        const auto& staff = required_record<StaffRuntimeRecord>(c, pending.staff);
        display.visible = true;
        display.name = staff_name(staff);
        display.role = role_caption(c, staff.base.header);
        display.ability = literal(c, c.messages.mail_language.abilities, static_cast<std::uint32_t>(pending.ability));
        if (staff.state == StaffState::training || staff.state == StaffState::training_paused) {
            display.progress_text_row = 934;
            if (staff.state == StaffState::training) {
                const auto limit = c.staff.recruitment.balance.all.ProgessUpperLimit;
                const auto numerator = fixed::word(100u * staff.ability_progress);
                if (!limit || (numerator == std::numeric_limits<std::int32_t>::min() && limit == -1)) {
                    throw std::domain_error("invalid staff-training progress division");
                }
                display.progress_percent = numerator / limit;
            }
        }
    }
    panel.empty_notice_visible = panel.active_row_count == 0;
    //Native staff count scans the active registry, while these two exclusions
    //count their subtype chains. Do not substitute cached population totals.
    if (!c.referenced_world()) {
        throw std::logic_error("training-room UI requires World");
    }
    const auto count_chain = [&](ThingId id) {
        std::int32_t count = 0;
        for (; id; id = required_record<StaffRuntimeRecord>(c, id).next_staff) {
            ++count;
        }
        return count;
    };
    std::int32_t staff_count = 0;
    for (auto* node = c.entities.active_head; node; node = node->active_next) {
        if (is_staff_record_kind(node->payload->kind)) {
            ++staff_count;
        }
    }
    const auto& saved = c.referenced_world()->saved;
    const auto excluded = pending_count + count_chain(saved.first_entertainer) + count_chain(saved.first_researcher);
    panel.add_staff_enabled = excluded < staff_count && panel.active_row_count < 3 && staff_count != 0;
    ++panel.revision;
}

void remove_training_room_staff_record_if_panel_open(WorldRuntime& c, ThingId id) {
    const auto* staff = payload(c, id);
    auto& panel = c.ui.panels.training_room;
    if (!panel.open) {
        return;
    }
    for (auto& row : c.staff.interaction.training_pending) {
        if ((row.staff ? payload(c, row.staff) : nullptr) == staff) {
            row.staff = 0;
            panel.active_row_count = fixed::subtract(panel.active_row_count, 1);
        }
    }
    refresh_training_room_rows_and_availability(c);
}

void insert_visitor_list_person(WorldRuntime& c, const PersonRuntimeView& person) {
    auto& list = c.ui.panels.visitor_list;
    if (!list.open) {
        return;
    }
    std::optional<std::u16string> caption;
    match (person.generation) {
        case PersonGeneration::child       => caption = literal(c, c.messages.mail_language.ui_text, 373);
        case PersonGeneration::parent      => caption = literal(c, c.messages.mail_language.ui_text, 374);
        case PersonGeneration::grandparent => caption = literal(c, c.messages.mail_language.ui_text, 375);
        case _                             => {
        }
    } const auto thought = static_cast<std::int32_t>(person.mobile.thought_history.last_thought);
    list.rows.push_back({ &person.base.header, person.base.header.id, person.arrival_index, std::move(caption), percentage_meter(person.happiness), thought ? fixed::subtract(thought, 1) : 999 });
    sort_and_refresh(list);
}

void remove_visitor_list_person(WorldRuntime& c, const ThingHeader& person) {
    auto& list = c.ui.panels.visitor_list;
    if (!list.open) {
        return;
    }
    const auto row = std::ranges::find(list.rows, &person, &VisitorListRow::record);
    if (row != list.rows.end()) {
        list.rows.erase(row);
        --c.ui.panels.visitor_list_removal_balance;
        ++list.revision;
    }
}

void refresh_visitor_locator_filter_buttons(WorldRuntime& c) {
    auto& locator = c.ui.panels.visitor_locator;
    locator.filter_enabled.fill(false);
    for (auto* node = c.entities.active_head; node; node = node->active_next) {
        if (node->payload->kind != ThingKind::person) {
            continue;
        }
        const auto& person = *reinterpret_cast<const PersonRuntimeView*>(node->payload);
        const auto generation = static_cast<std::uint32_t>(person.generation);
        if (generation < 3) {
            const auto female = world_operations::get_person_sprite_gender_flag(c, person);
            locator.filter_enabled[2u * generation + (female ? 0u : 1u)] = true;
        }
    }
    ++locator.revision;
}

void handle_world_ui_message(WorldRuntime& c, const WorldEvent& event) {
    if (c.ui.panels.park_object_event) {
        const auto notify = c.ui.panels.park_object_event;
        notify(c, event);
    }
    //63A1E0: the kind17 receiver itself carries no state used by the callback.
    //Root guards and the shared selected-record identity belong to UI owners.
    match (event.type()) {
        case 7 => {
            refresh_ride(c, event.get<ParkObjectBrokenDownEvent>().park_object_id);
            return;
        }
        case 8 => {
            refresh_ride(c, event.get<ParkObjectCondemnedEvent>().park_object_id);
            return;
        }
        case 10 => {
            ensure_selector(c);
            return; //Native mode3 target is an actual no-op.
        }
        case 15 => {
            const auto& value = event.get<StaffRestAreaEvent>();
            auto* p = payload(c, value.staff);
            if (!c.ui.panels.staff_room.open) {
                return;
            }
            if (!p) {
                throw std::logic_error("staff-room event refers to a missing record");
            }
            if (value.entered) {
                append_matching_staff_room_roster_member(c, *reinterpret_cast<StaffRuntimeRecord*>(p));
            } else {
                remove_staff_room_roster_member(c, *p);
            }
            return;
        }
        case 19 => {
            if (event.get<Tag19Event>().tag == 5) {
                ensure_selector(c);
            }
            return;
        }
        case 33 => {
            const auto id = event.get<RetirementEvent>().retired_id;
            if (const auto* retiring = payload(c, id); retiring && (is_staff_record_kind(retiring->kind) || retiring->kind == ThingKind::park_object) && c.ui.panels.staff_retiring) {
                const auto notify = c.ui.panels.staff_retiring;
                notify(c, *retiring);
            }
            remove_visitor(c, id);
            if (!c.simulation.current) {
                throw std::logic_error("UI retirement requires base World");
            }
            auto& tags = required_record<TagSystemRecord>(c, c.simulation.current->saved.tag_system);
            (void)mark_tag_messages_by_format_word_for_deletion(c, &tags, id);
            return;
        }
        case 34 => {
            const auto id = event.get<PersonCreatedEvent>().person_id;
            if (c.ui.panels.visitor_list.open) {
                insert_visitor_list_person(c, required_record<PersonRuntimeView>(c, id));
            }
            return;
        }
        case 42 => {
            (void)payload(c, event.get<StaffTrainingStartedEvent>().staff);
            //Native nullsub_19.
        }
        case 43                         => remove_training_room_staff_record_if_panel_open(c, event.get<StaffTrainingCompletedEvent>().staff);
        case 35 or 36 or 37 or 38 or 39 => {
        }
        case _ => {
            return; //Includes the explicitly ignored event18.
        }
    } c.ui.panels.objectives_refresh_pending = true;
}
} //namespace simcoaster
