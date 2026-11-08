#include "simcoaster/object_catalog.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world_challenge.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"
#include "world_ui_management_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char challenge_words[]{
#embed "ui_templates/app_challenges.bin"
};
//Exact original31-DWORD table; index0 is the disabled/default action645.
constexpr std::array<std::int32_t, 31> challenge_actions{ 645, 608, 573, 574, 585, 617, 575, 586, 576, 577, 609,
                                                          578, 601, 587, 610, 588, 579, 589, 618, 590, 591, 602,
                                                          592, 611, 593, 594, 595, 596, 612, 603, 580 };

std::int32_t script(const ChallengeRecord& record) {
    const auto index = std::to_underlying(record.script_index);
    if (index < 1 || index > 30)
        throw std::out_of_range("Challenge presentation script1..30");
    return index;
}

template <std::size_t N>
bool member(std::int32_t value, const std::array<std::int32_t, N>& values) {
    return std::ranges::find(values, value) != values.end();
}

std::u16string focused_name(WorldRuntime& c, ThingId id) {
    if (!id)
        return {};
    const auto* header = c.entities.at(id).payload;
    if (!header || header->retirement_pending)
        return {};
    const auto component = [](const auto& buffer) {
        return std::u16string{ buffer.begin(), std::ranges::find(buffer, char16_t{}) };
    };
    if (is_staff_record_kind(header->kind))
        return component(reinterpret_cast<const StaffRuntimeRecord*>(header)->name);
    if (header->kind == ThingKind::park_object) {
        const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(header);
        //5C6470/6A60 concatenate raw components, independently of display-name
        //hyphen composition and the ordinary32-character UI name limit.
        return component(object.name_a) + u' ' + component(object.name_b);
    }
    return {};
}

std::int32_t percentage(std::int32_t numerator, std::int32_t denominator) {
    if (!denominator)
        return 0;
    const auto product = fixed::narrow(static_cast<std::int64_t>(100) * numerator);
    return fixed::narrow(static_cast<std::int64_t>(product) / denominator);
}
} //namespace

ChallengeManagerRecord& WorldManagementUiController::State::challenges() const {
    auto* p = c.entities.at(world().saved.challenge_manager).payload;
    if (!p || p->kind != ThingKind::challenge_manager)
        throw std::logic_error("Challenges UI manager absent");
    return *reinterpret_cast<ChallengeManagerRecord*>(p);
}

void WorldManagementUiController::State::cancel_challenge_advisors(std::optional<std::int32_t> except) {
    for (std::size_t index = 1; index < challenge_actions.size(); ++index)
        if (!except || static_cast<std::int32_t>(index) != *except)
            cancel_advisor(challenge_actions[index]);
}

void WorldManagementUiController::State::queue_challenge_advisor() {
    if (!selected_challenge || challenge_advisor_latched)
        return;
    const auto& record = challenges().records.at(*selected_challenge);
    if (record.status != ChallengeStatus::available
        && record.status != ChallengeStatus::preparing
        && record.status != ChallengeStatus::active)
        return;
    const auto index = script(record);
    cancel_challenge_advisors(index);
    auto candidate =
        world_operations::make_advisor_action_candidate(c, challenge_actions[static_cast<std::size_t>(index)]);
    auto* p = c.entities.at(world().saved.advisor).payload;
    if (!p || p->kind != ThingKind::advisor)
        throw std::logic_error("Challenges Advisor absent");
    world_operations::queue_advisor_action_candidate(c, reinterpret_cast<AdvisorRecord*>(p), candidate);
    challenge_advisor_latched = true;
}

std::u16string WorldManagementUiController::State::challenge_briefing(const ChallengeRecord& record) const {
    const auto index = script(record);
    std::u16string result;
    if (member(std::to_underlying(record.type), std::array{ 17, 49, 52, 53, 55, 56, 48, 57, 58, 51 }))
        result =
            localized_text(challenge_generic, 17) + get_catalog_object_display_name(c, record.target_object) + u'\n';
    //5C5990's switch passes subsets of these exact keyed values. Supplying
    //the union is equivalent: the BFST row consumes only its referenced keys.
    const std::array arguments{ BfstSignedIntegerArgument{ 0, record.grace_period_limit },
                                BfstSignedIntegerArgument{ 1, record.target_time },
                                BfstSignedIntegerArgument{ 2, record.target_value },
                                BfstSignedIntegerArgument{ 5, record.target_value2 } };
    result += localized_text(challenge_specific, static_cast<std::uint32_t>(3 * index - 3), arguments);
    const auto sender =
        std::ranges::find(c.messages.tag_system_message_variants, record.sender, &TagMessageVariant::variant_id);
    if (sender == c.messages.tag_system_message_variants.end())
        throw std::logic_error("Challenge sender metadata absent");
    result +=
        u'\n'
        + localized_text(c.messages.mail_language.tag_system, static_cast<std::uint32_t>(sender->category_resource_id))
        + u'\n'
        + localized_text(c.messages.mail_language.tag_system,
                         static_cast<std::uint32_t>(sender->secondary_resource_id));
    return result;
}

std::u16string WorldManagementUiController::State::challenge_status(const ChallengeRecord& record,
                                                                    bool preparing,
                                                                    std::u16string unchanged) const {
    const auto index = script(record);
    //Preparation has no implementation for several scripts. Its original
    //caller has already written generic16 into the fragment; preserve that
    //exact unchanged-buffer behavior rather than invent progress text.
    //5C6910 is the original compressed switch map, indexed by script-1.
    //Its groups are time-only1/10/18/24, value3/4/11/13/17/20/28,
    //focused5/14, two-value21/27 and percentage26. Analyst enum names for
    //later scripts previously obscured these actual numeric cases.
    if (preparing && !member(index, std::array{ 1, 3, 4, 5, 10, 11, 13, 14, 17, 18, 20, 21, 24, 26, 27, 28 }))
        return unchanged;
    if ((index == 5 || index == 14) && !record.thing_in_focus)
        return unchanged;
    const auto days = fixed::narrow(get_challenge_remaining_days(c, record));
    auto progress = record.value;
    if (index == 26)
        progress = percentage(record.value2, record.value);
    else if (!preparing && index == 25)
        progress = percentage(record.value, record.value2);
    const auto name = focused_name(c, record.thing_in_focus);
    const std::array strings{ BfstWideStringArgument{ 4, name } };
    const std::array integers{ BfstSignedIntegerArgument{ preparing ? 0u : 1u, days },
                               BfstSignedIntegerArgument{ 2, progress },
                               BfstSignedIntegerArgument{ 3, record.target_value },
                               BfstSignedIntegerArgument{ 5, record.value2 },
                               BfstSignedIntegerArgument{ 6, record.target_value2 } };
    return localized_text(challenge_specific,
                          static_cast<std::uint32_t>(3 * index - (preparing ? 2 : 1)),
                          integers,
                          strings);
}

std::u16string WorldManagementUiController::State::challenge_details(const ChallengeRecord& record) const {
    const auto fragment = [&](std::uint32_t row, std::uint32_t key, std::int32_t value) {
        const std::array args{ BfstSignedIntegerArgument{ key, value } };
        return localized_text(challenge_generic, row, args);
    };
    auto result = record.gt_prize > 1 ? fragment(1, 0, record.gt_prize) : localized_text(challenge_generic, 0);
    if (record.share_prize > 0)
        result += fragment(2, 1, record.share_prize);
    if (record.cash_prize > 0)
        result += fragment(3, 2, record.cash_prize);
    if (record.share_price_effect_if_won > 0)
        result += fragment(4, 3, record.share_price_effect_if_won);
    if (record.share_price_effect_if_lost > 0)
        result += fragment(5, 4, record.share_price_effect_if_lost);
    if (record.promo_given)
        result += localized_text(challenge_generic, 6)
                + localized_text(c.messages.mail_language.promotions, static_cast<std::uint32_t>(record.promo_given));
    result += u'\n' + localized_text(challenge_generic, 8);
    match (record.status) {
        case ChallengeStatus::available => {
            result += record.challenge_lost > 1
                        ? fragment(10, 5, record.challenge_lost)
                        : localized_text(challenge_generic, record.challenge_lost == 1 ? 11 : 9);
        }
        case ChallengeStatus::preparing => {
            const auto prefix = localized_text(challenge_generic, 16);
            result += prefix + challenge_status(record, true, prefix);
        }
        case ChallengeStatus::active => {
            const auto prefix = localized_text(challenge_generic, 15);
            result += prefix + challenge_status(record, false, prefix);
        }
        case ChallengeStatus::complete => result += localized_text(challenge_generic, 14);
        case _ => {}
    }
    return result;
}

void WorldManagementUiController::State::open_challenges(std::optional<ChallengeScriptIndex> requested) {
    begin(Page::challenges);
    root = &parse(challenge_words, 791035);
    get_or_create_theme_global_info_manager(c).advisor_eligibility_flags[7] = 0;
    title(791057, text(462));
    caption(required(root, 791039), text(464), 7);
    caption(required(root, 791040), text(463), 7);
    ui.set_value(required(root, 791039), required(root, 791039).visual_flags | 1u);
    ui.set_enabled(required(root, 791039), false);
    ui.set_visible(required(root, 791055), false);
    ui.set_visible(required(root, 791056), false);
    caption(required(root, 791042), text(993), 7);
    required(root, 791042).text_constraints = { true, true };
    required(root, 791042).help_index = 101;
    caption(required(root, 791053), text(465), 7);
    required(root, 791053).text_constraints = { true, true };
    list_control = &required(root, 791054);
    constexpr std::array headings{ 0u, 0u };
    constexpr std::array formats{ UiListCellFormat::text, UiListCellFormat::text };
    list(*list_control, headings, formats, challenge_sort);
    caption(required(list_control, 16), localized_text(challenge_generic, 20), 8);
    caption(required(list_control, 17), localized_text(challenge_generic, 21), 8);
    list_control->column_presentations[1].horizontal_alignment = 1;
    selected_challenge.reset();
    challenge_advisor_latched = false;
    challenge_mode = 0;
    refresh_challenges(true);
    if (requested) {
        for (std::size_t slot = 0; slot < challenges().records.size(); ++slot)
            if (challenges().records[slot].script_index == *requested)
                if (auto row = ui.find_list_row(*list_control, static_cast<std::intptr_t>(slot))) {
                    ui.select_list_row(*list_control, static_cast<std::int32_t>(*row), false);
                    selected_challenge = slot;
                    break;
                }
    }
    ui.set_periodic(*root, 0, UiPeriodicEvent{ 16, 524416, 0, std::chrono::milliseconds{ 2000 } });
    refresh_challenge_details();
    queue_challenge_advisor();
    stage_advisor(439);
    activate();
}

void WorldManagementUiController::State::refresh_challenges(bool rebuild) {
    if (page != Page::challenges || !root)
        return;
    std::vector<UiListRow> rows;
    auto& manager = challenges();
    for (std::size_t slot = 0; slot < manager.records.size(); ++slot) {
        const auto& record = manager.records[slot];
        if (record.type == ChallengeType::code_0)
            continue;
        const auto state = std::to_underlying(record.status);
        auto status = state >= 0 && state <= 4
                        ? localized_text(challenge_generic, static_cast<std::uint32_t>(22 + state))
                        : std::u16string{};
        auto name =
            localized_text(c.messages.mail_language.challenge_titles, static_cast<std::uint32_t>(script(record)));
        rows.push_back({ static_cast<std::intptr_t>(slot), { std::move(name), std::move(status) } });
    }
    if (rebuild)
        ui.set_list_rows(*list_control, std::move(rows), false, false);
    else {
        for (const auto& row : rows)
            if (ui.find_list_row(*list_control, row.identity))
                ui.update_list_row(*list_control, row.identity, row.cells, 2, false);
            else
                ui.insert_list_row(*list_control, row, {}, false);
        const auto existing = list_control->rows;
        for (const auto& row : existing)
            if (std::ranges::none_of(rows, [&](const auto& candidate) {
                    return candidate.identity == row.identity;
                }))
                ui.remove_list_row(*list_control, row.identity, false);
    }
    if (auto identity = ui.selected_list_identity(*list_control))
        selected_challenge = static_cast<std::size_t>(*identity);
    else
        selected_challenge.reset();
    const auto failures = get_or_create_theme_global_info_manager(c).challenge_failures_remaining;
    const std::array args{ BfstSignedIntegerArgument{ 6, failures } };
    caption(required(root, 791041), localized_text(challenge_generic, failures == 1 ? 18 : 19, args), 7, 0, 1);
    refresh_challenge_details();
}

void WorldManagementUiController::State::refresh_challenge_details() {
    if (page != Page::challenges || !root)
        return;
    auto &body = required(root, 791043), &accept = required(root, 791042), &mode = required(root, 791052);
    const auto valid = selected_challenge && challenges().records.at(*selected_challenge).status != ChallengeStatus::na;
    if (!valid) {
        scroll_text(body, 791051, {});
        ui.set_visible(accept, false);
        ui.set_visible(mode, false);
        for (const auto id : { 791045u, 791048u, 791050u })
            ui.set_text(required(root, id), {});
        ui.set_visible(required(root, 791046), false);
        const auto any = std::ranges::any_of(challenges().records, [](const auto& r) {
            return r.status != ChallengeStatus::na;
        });
        ui.set_visible(required(root, 791053), !any);
        return;
    }
    const auto& record = challenges().records.at(*selected_challenge);
    for (const auto [id, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 3>{
             { { 791044, 856 }, { 791047, 857 }, { 791049, 855 } } }) {
        auto& control = required(root, id);
        caption(control, text(row), 6, 0, 1);
        control.color = { 0xEA, 0xEF, 0x66, 255 };
    }
    caption(required(root, 791048),
            localized_text(c.messages.mail_language.challenge_titles, static_cast<std::uint32_t>(script(record))),
            6,
            0,
            1);
    const auto variant =
        std::ranges::find(c.messages.tag_system_message_variants, record.sender, &TagMessageVariant::variant_id);
    if (variant == c.messages.tag_system_message_variants.end())
        throw std::logic_error("Challenges sender absent");
    caption(
        required(root, 791045),
        localized_text(c.messages.mail_language.tag_system, static_cast<std::uint32_t>(variant->category_resource_id)),
        6,
        0,
        1);
    auto& portrait = required(root, 791046);
    ui.set_visible(portrait, variant->variant_code != 0);
    if (variant->variant_code)
        ui.set_attachment_part(portrait, static_cast<std::uint16_t>(variant->variant_code - 1));
    if (!c.finance.shares_finance.ownership_context)
        throw std::logic_error("Challenges profile owner absent");
    caption(required(root, 791050), c.finance.shares_finance.ownership_context->current_profile_name, 6, 0, 1);
    ui.set_visible(required(root, 791053), false);
    ui.set_visible(mode, true);
    caption(mode, text(challenge_mode ? 991 : 990), 7);
    ui.set_value(mode, static_cast<std::int32_t>((mode.visual_flags & ~1u) | (challenge_mode ? 1u : 0u)));
    scroll_text(body, 791051, challenge_mode ? challenge_details(record) : challenge_briefing(record));
    body.color = { 0xE1, 0xE1, 255, 255 };
    ui.set_visible(accept, record.status == ChallengeStatus::available);
    ui.set_enabled(accept, world().saved.park_closed == 0);
}

std::intptr_t WorldManagementUiController::State::challenge_event(UiControl& sender, const UiEvent& e) {
    if (e.type == 16 && e.id == 524416) {
        refresh_challenges();
        return 0;
    }
    if (e.type == 1030) {
        challenge_sort = ui.list_sort_key(*list_control);
        return 0;
    }
    if ((e.type == 1024 || e.type == 1025) && e.id == 791054) {
        const auto index = static_cast<std::size_t>(e.value);
        if (e.value >= 0 && index < list_control->rows.size())
            selected_challenge = static_cast<std::size_t>(list_control->rows[index].identity);
        else
            selected_challenge.reset();
        challenge_mode = 0;
        challenge_advisor_latched = false;
        refresh_challenge_details();
        queue_challenge_advisor();
        return 0;
    }
    if (e.type == 2048 && e.id == 791051) {
        required(root, 791043).text_offset = { 0, -static_cast<std::int32_t>(e.value) };
        return 0;
    }
    if (e.type == 256) {
        if (e.id == 791036 || e.id == 4294967294u) {
            close();
            return 1;
        }
        if (e.id == 791058) {
            help_advisor(439);
            return 0;
        }
        if (e.id == 791052) {
            challenge_mode = !challenge_mode;
            refresh_challenge_details();
            return 1;
        }
        if (e.id == 791040) {
            if (!host.open_objectives)
                throw std::logic_error("Challenges Objectives route absent");
            close();
            host.open_objectives();
            return 1;
        }
        if (e.id == 791042 && selected_challenge) {
            //Native52A66B confuses sorted visible ordinal and manager slot. Stable
            //slot identity preserves the chosen record through sorting/sparse rows.
            const auto slot = *selected_challenge;
            if (challenges().records.at(slot).status == ChallengeStatus::available && !world().saved.park_closed) {
                accept_world_challenge(c, challenges(), slot);
                close();
            }
            return 1;
        }
    }
    return ui.secondary(sender, e);
}
} //namespace simcoaster
