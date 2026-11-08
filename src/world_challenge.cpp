#include "simcoaster/numeric_word.hpp"
#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_challenge.hpp"
#include "company_email_internal.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr std::uint64_t day_ticks = 864000000000ull;

World& world(MissionWorld& c, bool primary = false) {
    return require_bound(primary ? c.referenced_world() : c.simulation.current, "challenge World is not bound");
}

template <class T>
T& record(MissionWorld& c, ThingId id) {
    auto* value = c.entities.at(id).payload;
    if (!value) {
        throw std::logic_error("missing challenge manager record");
    }
    return *reinterpret_cast<T*>(value);
}

std::uint64_t now(MissionWorld& c, bool primary = true) {
    auto& w = world(c, primary);
    FileTime time;
    calculate_world_calendar_filetime(c, &w.calendar, &time);
    return (std::uint64_t { time.high } << 32) | time.low;
}

std::int32_t divide(std::int32_t a, std::int32_t b) {
    return numeric::quotient(a, b, "challenge integer division fault");
}

std::int32_t percentage(std::int32_t a, std::int32_t b) {
    return divide(fixed::word(static_cast<std::uint32_t>(a) * 100u), b);
}

bool pending(const ChallengeRecord& row) {
    return row.status == ChallengeStatus::preparing || row.status == ChallengeStatus::active;
}

void notify(MissionWorld& c, WorldEvent event) {
    dispatch_world_event_to_subscribers(c, get_world_event_subscription_registry(c), &event, deliver_world_record_event, nullptr);
}

void sound(MissionWorld& c, std::uint32_t key = 198) {
    (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, key, 0, 0, 0);
}

AdvisorRecord& advisor(MissionWorld& c) {
    return record<AdvisorRecord>(c, world(c).saved.advisor);
}

void cancel_sender(MissionWorld& c, std::int32_t sender, bool offer = false) {
    //Original Category-27 message metadata: sender is an array index, not an
    //advisor action. Offer acceptance uses column0; termination uses column4.
    constexpr std::array offer_actions { 645, 645, 569, 604, 581, 613, 597 };
    constexpr std::array end_actions { 645, 645, 570, 605, 582, 614, 598 };
    if (sender < 0 || static_cast<std::size_t>(sender) >= end_actions.size()) {
        throw std::out_of_range("challenge message sender");
    }
    world_operations::cancel_advisor_action_by_id(c, &advisor(c), (offer ? offer_actions : end_actions)[static_cast<std::size_t>(sender)]);
}

void restore_restriction(MissionWorld& c, const ChallengeRecord& row) {
    if (row.type == ChallengeType::code_42) {
        world_operations::set_challenge_restricted_objects_open(c, true);
    }
}

void start_deadline(MissionWorld& c, ChallengeRecord& row, std::int32_t days) {
    row.started_at_world_time = now(c);
    row.finish_time = now(c) + day_ticks * static_cast<std::uint64_t>(static_cast<std::int64_t>(days));
}

void price_delta(MissionWorld& c, std::int32_t delta) {
    auto* price = c.finance.shares_finance.share_price_context.get();
    if (!price) {
        throw std::logic_error("challenge share-price context");
    }
    if (price->current_share_price && delta) {
        const auto change = fixed::word(static_cast<std::uint32_t>(delta) * static_cast<std::uint32_t>(price->current_share_price)) / 100;
        const auto next = fixed::add(price->current_share_price, change);
        if (next > 0) {
            price->current_share_price = next;
        }
        world_operations::mark_shares_panel_refresh(c);
    }
}

void award_shares(MissionWorld& c, std::int32_t amount) {
    auto* ownership = c.finance.shares_finance.ownership_context.get();
    if (!ownership) {
        throw std::logic_error("challenge ownership context");
    }
    if (amount > 0) {
        ownership->shares_owned = fixed::add(ownership->shares_owned, amount);
        ownership->ownership_percentage = percentage(ownership->shares_owned, c.finance.company_shares_issued);
    }
    world_operations::mark_shares_panel_refresh(c);
}

void mail(MissionWorld& c, CompanyEmailMessage* value) {
    WorldEvent event { HeapPayloadEvent { value } };
    try {
        notify(c, event);
    } catch (...) {
        destroy_heap_payload_event(c, &event.get<HeapPayloadEvent>());
        throw;
    }
    destroy_heap_payload_event(c, &event.get<HeapPayloadEvent>());
}

void promotion_mail(MissionWorld& c, std::int32_t rank, std::int32_t shares) {
    auto* storage = c.ports.memory.allocate(Allocation::event_payload, sizeof(CompanyEmailMessage));
    auto* email = storage ? std::construct_at(static_cast<CompanyEmailMessage*>(storage)) : nullptr;
    if (email) {
        email->reason = CompanyEmailMessageKind::promotion;
        email->concrete_class = CompanyEmailClass::promotion;
        email->context_mode = 1;
        email->default_resource = 151;
        email->append_signature = 1;
        email->tag_id = 151;
        email->selected_variant = find_tag_system_message_variant_by_id(c, 1);
        email->promotion_rank = rank;
        email->share_prize = shares;
    }
    mail(c, email);
}
} //namespace

void world_operations::builtin::mark_shares_panel_refresh(WorldRuntime& c) {
    c.finance.shares_panel_refresh_pending = true;
}

void award_world_shares(MissionWorld& c, std::int32_t amount) {
    award_shares(c, amount);
}

void adjust_world_share_price(MissionWorld& c, std::int32_t percentage) {
    price_delta(c, percentage);
}

ChallengeManagerRecord* initialize_challenge_manager(MissionWorld& c, ChallengeManagerRecord* manager) {
    if (!manager) {
        throw std::invalid_argument("null challenge manager");
    }
    initialize_world_map_object_base(c, &manager->base, 1, std::to_underlying(ThingKind::challenge_manager));
    for (auto& row : manager->records) {
        row.type = ChallengeType::code_0;
        row.challenge_lost = 0;
        row.offer_time = 0;
        row.value = 0;
        row.started_at_world_time = row.finish_time = 0;
        row.status = ChallengeStatus::na;
    }
    manager->next_challenge_delay_time = 0;
    manager->system_switched_on = manager->offer_latch = 0;
    for (const auto type : { 32, 11, 17, 7 }) {
        (void)register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &manager->base.header, type);
    }
    manager->actual_thing.fill(0);
    load_challenge_records(*manager, c.challenges.definitions, c.challenges.balance);
    return manager;
}

void initialize_challenge_record(ChallengeRecord& row, const ChallengeDefinitionParameters& d) {
    row.type = static_cast<ChallengeType>(d.Type);
    row.target_time = d.TargetTime;
    row.target_value = d.TargetVal;
    row.target_value2 = d.TargetVal2;
    row.target_object = static_cast<ThingId>(d.TargetObj);
    row.target_object2 = static_cast<ThingId>(d.TargetObj2);
    row.target_staff_type = d.TargetStaffType;
    row.cash_prize = d.CashPrize;
    row.gt_prize = std::max(1, d.GTPrize);
    row.share_prize = d.SharePrize;
    row.share_price_effect_if_won = d.SharePriceEffectWon;
    row.share_price_effect_if_lost = d.SharePriceEffectLost;
    row.double_or_nothing_followup = d.FollowupType;
    row.check_at_end_only = d.CheckAtEndOnly != 0;
    row.grace_period_limit = d.GracePeriodLimit;
    row.trigger_data = d.TriggerData;
    row.grace_target_value = d.GraceTargetVal;
    row.min_days_til_trigger = d.MinDaysTilTrigger;
    row.max_days_til_trigger = d.MaxDaysTilTrigger;
    row.delay = d.DaysDelay;
    row.sender = d.Sender;
    row.script_index = static_cast<ChallengeScriptIndex>(d.ScriptIndex);
    row.mission_needed = d.MissionNeededToActivate;
    row.promo_given = d.PromoGiven;
    row.challenge_lost = 0;
    row.offer_time_set = 0;
    row.offer_time = 0;
    row.value = row.value2 = 0;
    row.started_at_world_time = row.finish_time = 0;
    row.status = ChallengeStatus::na;
    row.thing_in_focus = 0;
    //Native construction leaves mSentTimeWarning and padding untouched.
}

void validate_challenge_followup_types(ChallengeManagerRecord& manager) {
    for (std::size_t i = 0; i < manager.records.size(); ++i) {
        auto& row = manager.records[i];
        if (row.type != ChallengeType::code_0 && row.double_or_nothing_followup) {
            bool found = false;
            for (std::size_t j = 0; j < manager.records.size(); ++j) {
                if (j != i && std::to_underlying(manager.records[j].type) == row.double_or_nothing_followup && manager.records[j].type != ChallengeType::code_0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                row.type = ChallengeType::code_0;
            }
        }
    }
}

void load_challenge_records(ChallengeManagerRecord& manager, const ChallengeDefinitions& definitions, const ChallengeBalance& balance) {
    for (auto& row : manager.records) {
        row.type = ChallengeType::code_0;
    }
    for (std::size_t i = 0; i < 10; ++i) {
        if (balance.level_rows[i].ChallengeType > 0) {
            const auto index = static_cast<std::size_t>(balance.level_rows[i].ChallengeType);
            if (index >= definitions.rows.size()) {
                throw std::out_of_range("challenge SAM row");
            }
            ChallengeRecord temporary {};
            initialize_challenge_record(temporary, definitions.rows[index]);
            const auto slot = std::ranges::find(manager.records, ChallengeType::code_0, &ChallengeRecord::type);
            if (slot != manager.records.end()) {
                *slot = temporary;
            }
        }
    }
    validate_challenge_followup_types(manager);
}

std::int64_t get_challenge_remaining_days(MissionWorld& c, const ChallengeRecord& row) {
    return std::bit_cast<std::int64_t>(row.finish_time - now(c)) / static_cast<std::int64_t>(day_ticks);
}

bool DidThisWinIt(MissionWorld& c, const ChallengeRecord& row, std::uint8_t at_end) {
    if (row.check_at_end_only && !at_end) {
        return false;
    }
    if (c.challenges.debug_always_win_challenges) {
        return true;
    }
    match (std::to_underlying(row.type)) {
        case 0                                                                                                     => return false;
        case 1 or 2 or 3 or 4 or 5 or 6 or 7 or 8 or 9 or 10 or 11 or 12 or 13 or 16 or 17 or 20 or 22 or 45 or 46 => {
            return row.value >= row.target_value;
        }
        case 19 => return read_challenge_metric(c, ChallengeMetric::maximum_vertical_special_count, row.target_object) >= 3;
        case 28 => {
            return read_challenge_metric(c, ChallengeMetric::maximum_upgrade_level, row.target_object) >= row.target_value;
        }
        case 41             => return !read_challenge_metric(c, ChallengeMetric::any_staff_strike);
        case 48             => return !row.value2 && row.value >= row.target_value;
        case 50             => return row.value != 0;
        case 51             => return row.value >= row.target_value && row.value2 != 0;
        case 52 or 53 or 56 => return row.value >= row.target_value && row.value2 >= row.target_value2;
        case 55             => return row.value >= row.target_value && !row.value2;
        case 57             => return row.value ? percentage(row.value2, row.value) >= row.target_value : row.target_value <= 0;
        case 58             => return row.value2 ? percentage(row.value, row.value2) >= row.target_value : row.target_value <= 0;
        case _              => {
            return true; //Includes the native diagnostic/default success path.
        }
    }
}

bool state3_challenge_failure_predicate(const MissionQuery& c, const ChallengeRecord& row) {
    if (c.challenges.debug_always_win_challenges) {
        return false;
    }
    match (std::to_underlying(row.type)) {
        case 10 or 25 or 34 or 35 or 36 or 37 or 46 => return row.value < row.target_value;
        case 38 or 39 or 40                         => return row.value > row.target_value;
        case 41 or 42 or 43                         => return row.value != 0;
        case 48                                     => return row.value2 != 0 || row.value < row.target_value;
        case 50                                     => return !row.value;
        case 52                                     => return row.value2 < row.target_value2;
        case 53 or 56                               => return row.value < row.target_value || row.value2 < row.target_value2;
        case 55                                     => return row.value < row.target_value || row.value2 != 0;
        case _                                      => return false;
    }
}

void UpdateCurrentValue(MissionWorld& c, ChallengeRecord& row) {
    ParkObjectRuntimeView* target = nullptr;
    if (row.target_object) {
        const auto id = find_first_park_object_record_id_by_definition_id(c, row.target_object);
        target = c.entities.at_payload<ParkObjectRuntimeView>(id);
    }
    const auto metric = [&](ChallengeMetric query, std::int32_t argument = 0) {
        return read_challenge_metric(c, query, argument);
    };
    match (std::to_underlying(row.type)) {
        case 9 or 10 => {
            const auto count = metric(ChallengeMetric::eligible_population);
            row.value = count ? percentage(metric(row.type == ChallengeType::code_9 ? ChallengeMetric::balloon_holders : ChallengeMetric::costume_wearers), count) : 0;
        }
        case 25 => row.value = read_challenge_metric(c, ChallengeMetric::minimum_staff_happiness, 0, &row.thing_in_focus);
        case 28 => row.value = metric(ChallengeMetric::maximum_upgrade_level, row.target_object);
        case 34 => row.value = metric(ChallengeMetric::recent_net_profit);
        case 35 => row.value = metric(ChallengeMetric::sideshow_satisfaction);
        case 36 => row.value = metric(ChallengeMetric::visitor_happiness);
        case 37 => row.value = fixed::word(count_eligible_person_records(c, 4));
        case 41 => row.value = metric(ChallengeMetric::any_staff_strike);
        case 42 => row.value = metric(ChallengeMetric::thought_in_top_three, 1);
        case 43 => row.value = metric(ChallengeMetric::thought_in_top_three, 7);
        case 46 => row.value = read_challenge_metric(c, ChallengeMetric::minimum_ride_repair, 0, &row.thing_in_focus);
        case 48 => {
            if (target) {
                row.value = calculate_park_object_customer_satisfaction(*target);
            }
        }
        case 50 => {
            row.value = metric(ChallengeMetric::thought_in_top_three, 9) || metric(ChallengeMetric::thought_in_top_three, 8);
        }
        case 51 => {
            ThingId focus;
            row.value = read_challenge_metric(c, ChallengeMetric::maximum_vertical_special_count, row.target_object, &focus);
            row.value2 = focus && record<ParkObjectRuntimeView>(c, focus).current_upgrade >= 2;
        }
        case 52 => {
            if (target) {
                row.value2 = truncate_park_object_condition(target->repair_state);
            }
        }
        case 53 => {
            if (target) {
                row.value = get_park_object_excitement(c, *target, false);
                row.value2 = truncate_park_object_condition(target->repair_state);
            }
        }
        case 55 => {
            if (target) {
                row.value = get_park_object_excitement(c, *target, false);
                row.value2 = metric(ChallengeMetric::thought_in_top_three, 7);
            }
        }
        case 56 => {
            if (target) {
                row.value = get_park_object_excitement(c, *target, false);
                row.value2 = calculate_park_object_customer_satisfaction(*target);
            }
        }
        case 58 => row.value2 = fixed::word(count_eligible_person_records(c, 4));
        case _  => {
            //Event-maintained counters and unhandled native selectors persist.
        }
    }
}

void CalcAvailTime(MissionWorld& c, ChallengeRecord& row) {
    if (row.offer_time_set) {
        return;
    }
    const auto metric = [&](ChallengeMetric query, std::int32_t argument = 0) {
        return read_challenge_metric(c, query, argument);
    };
    bool ready = false;
    match (std::to_underlying(row.type)) {
        case 1                                                        => ready = metric(ChallengeMetric::has_ingredient, 2) != 0;
        case 2 or 42                                                  => ready = metric(ChallengeMetric::has_ingredient, 1) != 0;
        case 3 or 36                                                  => ready = metric(ChallengeMetric::lifetime_visitors) >= row.trigger_data;
        case 4                                                        => ready = metric(ChallengeMetric::has_ingredient, 4) != 0;
        case 5                                                        => ready = metric(ChallengeMetric::has_shop_type, 1) != 0;
        case 6                                                        => ready = metric(ChallengeMetric::balloons_sold) >= row.trigger_data;
        case 8                                                        => ready = metric(ChallengeMetric::has_shop_type, 3) != 0;
        case 9                                                        => ready = metric(ChallengeMetric::has_shop_type, 6) && fixed::word(count_eligible_person_records(c, 0)) > 10;
        case 10                                                       => ready = metric(ChallengeMetric::costumes_sold) > row.trigger_data;
        case 17 or 48 or 50 or 51 or 52 or 53 or 55 or 56 or 57 or 58 => {
            const auto id = find_first_park_object_record_id_by_definition_id(c, row.target_object);
            if (id) {
                const auto& object = record<ParkObjectRuntimeView>(c, id);
                auto* definition = find_object_definition_by_id(&c.resources.object_catalog, row.target_object);
                if (!definition) {
                    throw std::logic_error("challenge definition");
                }
                ready = definition->is_special > 0 ? get_park_object_activation_flag(&object) != 0 : object.has_been_opened != 0;
            } else {
                ready = metric(ChallengeMetric::preferred_research_matches, row.target_object) != 0;
            }
        }
        case 25 => ready = metric(ChallengeMetric::maximum_staff_experience, 6) >= row.trigger_data;
        case 34 => ready = fixed::word(sum_current_park_analyser_history_values_7_to_11(c)) >= row.trigger_data;
        case 35 => {
            ready = static_cast<std::uint32_t>(metric(ChallengeMetric::completed_research_count, 2)) >= static_cast<std::uint32_t>(row.trigger_data);
        }
        case 37 => ready = fixed::word(count_eligible_person_records(c, 4)) >= row.trigger_data;
        case 38 => ready = metric(ChallengeMetric::eligible_population) >= row.trigger_data;
        case 39 => ready = metric(ChallengeMetric::misbehaving_kids_ever) != 0;
        case 40 => ready = metric(ChallengeMetric::misbehaving_kids_ever) >= row.trigger_data;
        case 41 => {
            ready = static_cast<std::uint32_t>(metric(ChallengeMetric::staff_count)) >= static_cast<std::uint32_t>(row.trigger_data);
        }
        case 43 => {
            ready = static_cast<std::uint32_t>(metric(ChallengeMetric::completed_research_count, 1)) >= static_cast<std::uint32_t>(row.trigger_data);
        }
        case 45 => {
            const auto sugar = metric(ChallengeMetric::has_ingredient, 4), ice = metric(ChallengeMetric::has_ingredient, 3);
            const auto count = count_eligible_person_records(c, 4);
            ready = sugar && ice && count > 25;
        }
        case 46 => ready = metric(ChallengeMetric::breakdowns_ever) >= row.trigger_data;
        case _  => {
        }
    } const auto elapsed_days = [&] {
        return static_cast<std::uint32_t>(std::bit_cast<std::int64_t>(scale_world_ticks_to_filetime_delta(world(c, true).calendar, world(c).saved.game_tick)) / static_cast<std::int64_t>(day_ticks));
    };
    bool triggered = elapsed_days() > static_cast<std::uint32_t>(row.min_days_til_trigger) && ready;
    if (row.max_days_til_trigger) {
        triggered |= elapsed_days() > static_cast<std::uint32_t>(row.max_days_til_trigger);
    }
    if (triggered && static_cast<std::uint32_t>(row.mission_needed) <= static_cast<std::uint32_t>(get_or_create_theme_global_info_manager(c).mission_number)) {
        row.offer_time = now(c) + day_ticks * static_cast<std::uint32_t>(row.delay);
        row.offer_time_set = 1;
    }
}

void begin_challenge_after_offer_period(MissionWorld& c, ChallengeRecord& row) {
    notify(c, WorldEvent { ChallengeNotificationEvent { &row, ChallengeNotificationCode::started } });
    row.status = ChallengeStatus::active;
    row.sent_time_warning = 0;
    start_deadline(c, row, row.target_time);
    if (row.type == ChallengeType::code_42) {
        world_operations::set_challenge_restricted_objects_open(c, false);
    }
    sound(c);
}

void accept_world_challenge(MissionWorld& c, ChallengeManagerRecord& manager, std::size_t slot) {
    auto& row = manager.records.at(slot);
    cancel_sender(c, row.sender, true);
    sound(c);
    row.value = row.value2 = 0;
    auto& tags = record<TagSystemRecord>(c, world(c).saved.tag_system);
    mark_matching_mail_for_deletion(c, &tags, 145, std::to_underlying(row.script_index));
    if (row.grace_period_limit) {
        row.status = ChallengeStatus::preparing;
        start_deadline(c, row, row.grace_period_limit);
    } else {
        row.status = ChallengeStatus::active;
        row.sent_time_warning = 0;
        start_deadline(c, row, row.target_time);
        if (row.type == ChallengeType::code_42) {
            world_operations::set_challenge_restricted_objects_open(c, false);
        }
        sound(c);
    }
}

void process_challenge_success(MissionWorld& c, ChallengeRecord& row) {
    sound(c);
    row.status = ChallengeStatus::complete;
    if (row.cash_prize > 0) {
        apply_bank_balance_and_profit_delta(c, &record<BankRecord>(c, world(c, true).saved.bank_account), row.cash_prize);
        sound(c);
    }
    if (row.gt_prize > 0) {
        auto& theme = get_or_create_theme_global_info_manager(c);
        theme.golden_ticket_balance = fixed::add(theme.golden_ticket_balance, row.gt_prize);
    }
    award_shares(c, row.share_prize);
    price_delta(c, row.share_price_effect_if_won);
    notify(c, WorldEvent { ChallengeNotificationEvent { &row, ChallengeNotificationCode::succeeded } });
    if (row.promo_given) {
        (void)get_or_create_theme_global_info_manager(c);
        notify(c, WorldEvent { PromotionEvent { row.promo_given, row.share_prize } });
        promotion_mail(c, row.promo_given, row.share_prize);
    }
    const auto& manager = record<ChallengeManagerRecord>(c, world(c, true).saved.challenge_manager);
    const bool all = std::ranges::all_of(manager.records, [](const ChallengeRecord& value) {
        return value.type == ChallengeType::code_0 || value.status == ChallengeStatus::complete;
    });
    auto& theme = get_or_create_theme_global_info_manager(c);
    if (!theme.selected_theme_info) {
        throw std::logic_error("challenge selected theme");
    }
    auto* info = get_or_create_theme_global_info_for_ordinal(c, theme, theme.selected_theme_info->source_ordinal);
    if (!info) {
        throw std::logic_error("challenge theme info");
    }
    info->challenges_done = fixed::add(info->challenges_done, 1);
    if (all) {
        info = get_or_create_theme_global_info_for_ordinal(c, theme, theme.selected_theme_info->source_ordinal);
        if (!info) {
            throw std::logic_error("challenge theme info");
        }
        info->all_challenges_done = 1;
    }
    notify(c, WorldEvent { ChallengeCompletionEvent { row.script_index } });
}

void process_challenge_failure(MissionWorld& c, ChallengeRecord& row) {
    sound(c, 175);
    row.status = ChallengeStatus::available;
    if (!row.challenge_lost) {
        auto& theme = get_or_create_theme_global_info_manager(c);
        theme.challenge_failures_remaining = fixed::subtract(theme.challenge_failures_remaining, 1);
        price_delta(c, fixed::negate(row.share_price_effect_if_lost));
    }
    row.challenge_lost = fixed::add(row.challenge_lost, 1);
    (void)record<ChallengeManagerRecord>(c, world(c, true).saved.challenge_manager);
    if (get_or_create_theme_global_info_manager(c).challenge_failures_remaining > 0) {
        notify(c, WorldEvent { ChallengeNotificationEvent { &row, ChallengeNotificationCode::failed } });
    } else {
        auto* storage = c.ports.memory.allocate(Allocation::event_payload, sizeof(GameOverCompanyEmail));
        auto* email = storage ? construct_game_over_company_email(c, std::construct_at(static_cast<GameOverCompanyEmail*>(storage)), GameOverEmailReason::failed_challenges) : nullptr;
        mail(c, email);
        const auto candidate = world_operations::make_advisor_action_candidate(c, 419);
        (void)world_operations::queue_advisor_action_candidate(c, &advisor(c), candidate);
    }
}

void EndChallenge(MissionWorld& c, ChallengeManagerRecord& manager, std::size_t slot) {
    auto& row = manager.records.at(slot);
    row.sent_time_warning = 0;
    restore_restriction(c, row);
    cancel_sender(c, row.sender);
    //Approved repair of 5C7ACB/5C8817: expiry must enforce the same
    //maintenance and park-open conditions as every preceding active update.
    if (!state3_challenge_failure_predicate(c, row) && !world(c).saved.park_closed && DidThisWinIt(c, row, 1)) {
        process_challenge_success(c, row);
    } else {
        process_challenge_failure(c, row);
        manager.actual_thing[slot] = 0;
    }
}

void offer_next_available_challenge(MissionWorld& c, ChallengeManagerRecord& manager) {
    if (std::ranges::any_of(manager.records, pending) || now(c, false) < manager.next_challenge_delay_time) {
        return;
    }
    for (auto& row : manager.records) {
        if (row.type != ChallengeType::code_0 && row.status == ChallengeStatus::na && row.offer_time_set && now(c) > row.offer_time) {
            notify(c, WorldEvent { ChallengeNotificationEvent { &row, ChallengeNotificationCode::offered } });
            row.status = ChallengeStatus::available;
            manager.offer_latch = 1;
            manager.next_challenge_delay_time = now(c, false) + day_ticks * static_cast<std::uint64_t>(static_cast<std::int64_t>(c.challenges.balance.schedule.DaysBetweenOffers));
            return;
        }
    }
}

void update_challenge_manager(MissionWorld& c, ChallengeManagerRecord& manager) {
    if (!c.challenges.initialized) {
        c.challenges.initialized = true;
        initialize_game_mode_selector(c);
    }
    if (c.mode.game != GameMode::code_0) {
        return;
    }
    if (!manager.system_switched_on) {
        if (world(c).saved.park_closed || get_or_create_theme_global_info_manager(c).mission_number < c.challenges.balance.schedule.MissionNumberForFirstChallenge) {
            return;
        }
        manager.system_switched_on = 1;
    }
    if (std::ranges::any_of(manager.records, pending)) {
        //Approved repair of 5C79F8/5C7AA6: compare the complete timestamps.
        //The native whole-day quotient remains available for UI formatting only.
        for (std::size_t i = 0; i < manager.records.size(); ++i) {
            auto& row = manager.records[i];
            if (row.status == ChallengeStatus::preparing) {
                UpdateCurrentValue(c, row);
                if (now(c) >= row.finish_time) {
                    begin_challenge_after_offer_period(c, row);
                }
                if (!world(c).saved.park_closed) {
                    continue;
                }
            } else if (row.status == ChallengeStatus::active) {
                UpdateCurrentValue(c, row);
                if (now(c) >= row.finish_time) {
                    EndChallenge(c, manager, i);
                    continue;
                }
                if (!state3_challenge_failure_predicate(c, row) && !world(c).saved.park_closed) {
                    if (DidThisWinIt(c, row, 0)) {
                        process_challenge_success(c, row);
                    }
                    continue;
                }
            } else {
                continue;
            }
            process_challenge_failure(c, row);
            manager.actual_thing[i] = 0;
        }
        return;
    }
    for (auto& row : manager.records) {
        if (row.type != ChallengeType::code_0 && row.status == ChallengeStatus::na) {
            CalcAvailTime(c, row);
        }
    }
    if (now(c, false) > manager.next_challenge_delay_time) {
        offer_next_available_challenge(c, manager);
    }
}

void handle_world_challenge_event(ChallengeManagerRecord& manager, const WorldEvent& event) {
    if (event.type() == 17) {
        const auto& availability = event.get<ResearchAvailabilityEvent>();
        if (availability.category == ResearchCategory::rides) {
            add_progress_to_matching_world_challenges(manager, ChallengeType::ride_research_events, 1, 0, false);
        }
    } else if (event.type() == 32) {
        const auto& progress = event.get<ChallengeProgressEvent>();
        add_progress_to_matching_world_challenges(manager, progress.type, progress.delta, progress.target, false);
    }
}
} //namespace simcoaster
