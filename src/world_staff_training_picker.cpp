#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/world_staff_interaction.hpp"
#include "world_staff_internal.hpp"
#include <format>

namespace simcoaster {
using namespace staff_detail;

namespace {
void start_training(StaffWorld& c, StaffTrainingPicker& picker) {
    auto& s = record<StaffRuntimeRecord>(c, picker.staff);
    if (s.going_to_learn != ThemeAbility::none) {
        for (auto& row : c.staff.interaction.training_pending) {
            if (row.staff == picker.staff) {
                row.ability = static_cast<ThemeAbility>(static_cast<std::uint16_t>(picker.selected));
            }
        }
    }
    s.going_to_learn = picker.selected;
    enter_world_staff_state(c, s, StaffState::walking_to_training_room);
    picker.confirmation_pending = false;
}
} //namespace

void open_staff_training_picker(StaffWorld& c, StaffRuntimeRecord& s) {
    StaffTrainingPicker picker;
    picker.staff = s.base.header.id;
    picker.open = true;
    const auto names = parse_bfst_literals(c.messages.mail_language.abilities, c.messages.mail_language.conversion);
    if (!names) {
        throw std::runtime_error(std::format("Staff ability labels: {}", names.error().reason));
    }
    for (int i = 0; i < 15; ++i) {
        const auto ability = static_cast<ThemeAbility>(i);
        if (can_world_staff_learn_theme_ability(c, s, ability)) {
            picker.rows.push_back({ ability, names->strings.at(static_cast<std::size_t>(i)) });
        }
    }
    std::ranges::stable_sort(picker.rows, {}, &StaffTrainingAbilityRow::name);
    c.staff.interaction.training_picker = std::move(picker);
}

StaffTrainingSelectionResult select_staff_training_ability(StaffWorld& c, ThemeAbility ability) {
    auto& picker = c.staff.interaction.training_picker;
    if (!picker.open) {
        throw std::logic_error("staff ability picker is closed");
    }
    if (std::ranges::none_of(picker.rows, [&](const auto& row) {
            return row.ability == ability;
        })) {
        throw std::out_of_range("ability is not in staff picker");
    }
    picker.selected = ability;
    const auto& s = record<StaffRuntimeRecord>(c, picker.staff);
    picker.open = false;
    if (s.first_known_ability == ability) {
        picker.message_row = 714;
        return StaffTrainingSelectionResult::already_known;
    }
    if (s.first_known_ability != ThemeAbility::none) {
        picker.message_row = 715;
        picker.confirmation_pending = true;
        return StaffTrainingSelectionResult::confirmation_required;
    }
    start_training(c, picker);
    return StaffTrainingSelectionResult::started;
}

bool confirm_staff_training_ability_change(StaffWorld& c) {
    auto& picker = c.staff.interaction.training_picker;
    if (!picker.confirmation_pending) {
        return false;
    }
    start_training(c, picker);
    return true;
}

void close_staff_training_picker(StaffWorld& c) {
    auto& picker = c.staff.interaction.training_picker;
    picker.open = false;
    record<AdvisorRecord>(c, primary(c).saved.advisor).from_ui_panel = 0;
    world_operations::cancel_advisor_action_by_id(c, &record<AdvisorRecord>(c, primary(c).saved.advisor), 445);
}
} //namespace simcoaster
