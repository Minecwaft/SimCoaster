#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_mail.hpp"
#include "world_staff_internal.hpp"

namespace simcoaster {
using namespace staff_detail;

namespace {
void due_wage_notice(StaffWorld& c, StaffRuntimeRecord& s) {
    (void)grant_world_staff_experience_from_activity_mix(c, s);
    if (fixed::word(s.wage_demand_day) <= 0)
        return;
    const auto elapsed =
        std::bit_cast<std::int64_t>(scale_world_ticks_to_filetime_delta(world(c).calendar, world(c).saved.game_tick));
    const auto days = static_cast<std::uint32_t>(elapsed / 864000000000LL);
    if (c.staff.last_staff_wage_notice_elapsed_day && days - c.staff.last_staff_wage_notice_elapsed_day < 30u)
        return;
    FileTime now;
    std::uint32_t day = 0;
    calculate_world_calendar_filetime(c, &primary(c).calendar, &now, nullptr, nullptr, &day);
    if (!day)
        throw std::domain_error("invalid staff wage calendar date");
    if (s.wage_demand_day != day)
        return;
    c.staff.last_staff_wage_notice_elapsed_day = days;
    const std::array<std::int32_t, 1> key{ 144 };
    (void)mark_tag_messages_by_key_set_and_format_word_for_deletion(
        c,
        &record<TagSystemRecord>(c, primary(c).saved.tag_system),
        key,
        s.base.header.id);
    queue_action(c, 456);
    const auto cause = evaluate_world_staff_strike_thresholds(c, hq(c), world_staff_type_for_used_thing(s.base.header));
    const auto months =
        std::bit_cast<std::int64_t>(scale_world_ticks_to_filetime_delta(world(c).calendar, world(c).saved.game_tick))
        / 25920000000000LL;
    post_staff_mail(c,
                    s,
                    cause == StaffStrikeCause::none || months >= c.staff.recruitment.balance.Strike.MinMonthsForStrike
                        ? CompanyEmailMessageKind::staff_wage_variant
                        : CompanyEmailMessageKind::staff_wage_demand,
                    s.wage_demand_pay);
    s.wage_demand_day = s.wage_demand_pay = 0;
}
} //namespace

void handle_world_staff_event(StaffWorld& c, StaffRuntimeRecord& s, const WorldEvent& event) {
    if (event.type() == 33 && s.base.header.kind == ThingKind::guard) {
        auto& g = reinterpret_cast<GuardRuntimeRecord&>(s);
        const auto id = event.get<RetirementEvent>().retired_id;
        if (g.perpetrator == id)
            g.perpetrator = 0;
        if (g.patient == id)
            g.patient = 0;
    }
    if (event.type() == 10) {
        const auto id = event.get<ParkObjectRemovedEvent>().park_object_id;
        const auto association = world_staff_association_state(s);
        match (s.base.header.kind) {
            //Role target IDs remain populated after some native completed jobs.
            //Clear those links on demolition without interrupting an unrelated
            //current rest/research/training association or its hidden sprite.
            case ThingKind::mechanic => {
                auto& m = reinterpret_cast<MechanicRuntimeRecord&>(s);
                if (m.object_to_repair == id) {
                    m.object_to_repair = 0;
                    if (association == StaffState::walking_to_breakdown || association == StaffState::repairing)
                        set_world_mechanic_state(c, m, StaffState::idle);
                }
            }
            case ThingKind::handyman => {
                auto& h = reinterpret_cast<HandymanRuntimeRecord&>(s);
                if (h.toilet_to_clean == id) {
                    h.toilet_to_clean = 0;
                    if (association == StaffState::walking_to_clean_toilet
                        || association == StaffState::cleaning_toilet)
                        set_world_staff_state_by_type(c, s, StaffState::idle);
                }
                if (h.bin_to_clean == id) {
                    h.bin_to_clean = 0;
                    if (association == StaffState::walking_to_clean_bin || association == StaffState::cleaning_bin)
                        set_world_staff_state_by_type(c, s, StaffState::idle);
                }
            }
            case ThingKind::gardener => {
                auto& g = reinterpret_cast<GardenerRuntimeRecord&>(s);
                if (g.plant_to_water == id) {
                    g.plant_to_water = 0;
                    if (association == StaffState::walking_to_water_plant || association == StaffState::watering_plant)
                        enter_world_staff_state(c, s, StaffState::idle);
                }
            }
            case _ => ;
        } if (s.research_room == id) {
            //Non-researcher consultants use the same room states. Native event10
            //handled researchers only, leaving the other five roles with stale IDs.
            if (association == StaffState::researching || association == StaffState::walking_to_research_room) {
                enter_world_staff_state(c, s, StaffState::idle);
            }
            s.research_room = 0;
        }
        if (s.rest_area == id) {
            if (association == StaffState::resting) {
                leave_staff_rest_area(c, s);
                const auto room = find_nearest_staff_rest_area(c, s);
                if (room && route(c, s, object(c, room).entry_cell)) {
                    s.rest_area = room;
                    enter_world_staff_state(c, s, StaffState::walking_to_rest_area);
                }
            } else {
                s.rest_area = 0;
                if (association == StaffState::walking_to_rest_area
                    || association == StaffState::waiting_for_empty_staff_room)
                    enter_world_staff_state(c, s, StaffState::idle);
            }
        }
        if (live_room(c, id, ParkObjectCapabilities::training_room)
            && (association == StaffState::training || association == StaffState::walking_to_training_room))
            cancel_staff_training_request(c, s, id);
    } else if (event.type() == 11) {
        due_wage_notice(c, s);
        (void)record_world_staff_activity(s);
    } else if (event.type() == 12) {
        (void)charge_world_bank_account_expense(c,
                                                &record<BankRecord>(c, world(c).saved.bank_account),
                                                fixed::word(s.salary));
        auto& history = record<ParkAnalyserState>(c, world(c).saved.park_analyser).long_histories[4];
        history.temporary += s.salary;
        update_world_staff_wage_state(c, s);
    }
}
} //namespace simcoaster
