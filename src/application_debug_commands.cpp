#include "simcoaster/require_bound.hpp"
#include "simcoaster/advisor_balance.hpp"
#include "simcoaster/application_debug_selector.hpp"
#include "simcoaster/calendar_runtime.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_challenge.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include "simcoaster/world_theme.hpp"
#include "world_staff_internal.hpp"
#include "world_staff_mail.hpp"
#include <memory>

namespace simcoaster {
namespace {
World& current(WorldRuntime& c) {
    return require_bound(c.simulation.current, "Developer command has no World");
}

template <class T>
T& require(WorldRuntime& c, ThingId id, ThingKind kind) {
    auto* p = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
    if (!p || p->kind != kind || p->retirement_pending)
        throw std::runtime_error("Developer target is missing (id " + std::to_string(id) + ")");
    return *reinterpret_cast<T*>(p);
}

void emit(WorldRuntime& c, WorldEvent e) {
    dispatch_world_event_to_subscribers(c,
                                        get_world_event_subscription_registry(c),
                                        &e,
                                        deliver_world_record_event,
                                        nullptr);
}

template <class F>
void mail(WorldRuntime& c, F&& initialize) {
    auto* raw = static_cast<CompanyEmailMessage*>(
        c.ports.memory.allocate(Allocation::event_payload, sizeof(CompanyEmailMessage)));
    if (!raw)
        throw std::bad_alloc();
    std::construct_at(raw);
    try {
        initialize(*raw);
    } catch (...) {
        std::destroy_at(raw);
        c.ports.memory.release(Allocation::event_payload, raw);
        throw;
    }
    WorldEvent event{ HeapPayloadEvent{ raw } };
    try {
        emit(c, event);
    } catch (...) {
        destroy_heap_payload_event(c, &event.get<HeapPayloadEvent>());
        throw;
    }
    destroy_heap_payload_event(c, &event.get<HeapPayloadEvent>());
}
} //namespace

void bind_application_debug_state(WorldRuntime& c, const ApplicationDebugState& s) {
    c.challenges.debug_always_win_challenges = s.always_win;
    c.attractions.force_special_ride_group_ready = s.force_special;
}

bool execute_application_debug_command(WorldRuntime& c, ApplicationDebugState& s, ApplicationDebugCommand command) {
    using C = ApplicationDebugCommand;
    match (command) {
        case C::none => return true;
        case C::skip_mission => {
            process_mission_completion_directives(c,
                                                  &require<MissionControllerRecord>(c,
                                                                                    current(c).saved.mission_controller,
                                                                                    ThingKind::mission_controller));
            return true; //Handled command; transition failures have their own dialog.
        }
        case C::promote => {
            (void)require<MissionControllerRecord>(c,
                                                   current(c).saved.mission_controller,
                                                   ThingKind::mission_controller);
            std::int32_t mission{}, rank{};
            get_mission_promotion_progress_values(c, &mission, &rank);
            emit(c, WorldEvent{ PromotionEvent{ fixed::add(rank, 1), 2 } });
            return true;
        }
        case C::award or C::advisor95 => {
            auto candidate = world_operations::make_advisor_action_candidate(c, command == C::award ? 317 : 95);
            emit(c, WorldEvent{ AdvisorActionEvent{ candidate } });
            return true;
        }
        case C::offer_challenge => {
            offer_next_available_challenge(
                c,
                require<ChallengeManagerRecord>(c, current(c).saved.challenge_manager, ThingKind::challenge_manager));
            return true;
        }
        case C::open_drinks or C::close_drinks => {
            set_special_ingredient_objects_open(c, 3, command == C::open_drinks);
            return true;
        }
        case C::game_over_mail => {
            mail(c, [&](auto& e) {
                construct_game_over_company_email(c, &e, GameOverEmailReason::bankruptcy);
            });
            return true;
        }
        case C::reload_challenges => {
            load_challenge_records(
                require<ChallengeManagerRecord>(c, current(c).saved.challenge_manager, ThingKind::challenge_manager),
                c.challenges.definitions,
                c.challenges.balance);
            return true;
        }
        case C::rebuild_staff_slots => {
            rebuild_world_staff_hq_ability_slots(
                c,
                require<StaffHqRecord>(c, current(c).saved.staff_hq, ThingKind::staff_hq));
            return true;
        }
        case C::sound => {
            constexpr std::array<std::uint32_t, 15> keys{ 274, 267, 275, 276, 283, 280, 278, 279,
                                                          269, 281, 271, 273, 272, 270, 268 };
            (void)submit_positioned_template_request(c,
                                                     0,
                                                     c.resources.global_rides_catalog_slot,
                                                     keys[static_cast<std::size_t>(next_crt_random(c) % 15)],
                                                     50,
                                                     0,
                                                     50);
            return true;
        }
        case C::promotion_mail => {
            mail(c, [&](auto& e) {
                construct_tag_system_company_email(c, &e, 59, 15, 1);
                e.concrete_class = CompanyEmailClass::debug_promotion;
                e.append_signature = 0;
            });
            return true;
        }
        case C::spawn_guard or C::spawn_researcher => {
            (void)create_debug_world_staff_record(c,
                                                  c.staff.interaction.cursor.cell,
                                                  command == C::spawn_guard ? StaffType::guard : StaffType::researcher);
            return true;
        }
        case C::refresh_lighting => {
            if (c.resources.meshes)
                c.resources.meshes->invalidate_mesh_renderer_cache(c);
            world_operations::refresh_active_park_object_meshes(c);
            c.ports.meshes.rebuild_terrain_cell_shading(c);
            world_operations::normalize_special_ride_owner_vectors(c);
            return true;
        }
        case C::toggle_special => {
            s.force_special = !s.force_special;
            bind_application_debug_state(c, s);
            return true;
        }
        case C::toggle_challenges => {
            s.always_win = !s.always_win;
            bind_application_debug_state(c, s);
            return true;
        }
        case C::unlock_areas => {
            auto& manager = get_or_create_theme_global_info_manager(c);
            for (int i = 0; i < 18; ++i)
                unlock_lobby_area_for_mission(manager, i);
            return true;
        }
        case C::training_mail or C::wage_mail => {
            auto id = current(c).saved.first_handyman;
            if (!id)
                return true;
            auto& staff = require<StaffRuntimeRecord>(c, id, ThingKind::handyman);
            mail(c, [&](auto& email) {
                staff_mail_detail::initialize(
                    c,
                    email,
                    staff,
                    command == C::training_mail ? CompanyEmailMessageKind::staff_training_cancelled
                                                : CompanyEmailMessageKind::staff_wage_demand,
                    command == C::training_mail ? static_cast<std::uint32_t>(staff.currently_learning)
                                                : static_cast<std::uint32_t>(next_crt_random(c) % 200));
            });
            return true;
        }
        case C::win_game => {
            award_world_shares(c, fixed::word(51u * static_cast<std::uint32_t>(c.finance.company_shares_issued)) / 100);
            mail(c, [&](auto& e) {
                construct_tag_system_company_email(c, &e, 93, 6, 1);
            });
            return true;
        }
        case C::shares10 => {
            award_world_shares(c, 10);
            return true;
        }
        case C::silence_advisor => {
            if (!c.advisor.balance)
                throw std::runtime_error("Advisor configuration is missing");
            c.advisor.balance->GeneralAdvisor.MinScoreForConsideration = 65000;
            c.advisor.queue_policy.general.MinScoreForConsideration = 65000;
            return true;
        }
        case C::share_price5 => {
            adjust_world_share_price(c, 5);
            return true;
        }
        case C::annual_report => {
            const auto year = s.annual_report_year;
            s.annual_report_year = fixed::add(year, 1);
            world_operations::send_annual_shares_report(c,
                                                        static_cast<std::uint32_t>(year),
                                                        c.ports.assets.selected_resource_source(c),
                                                        3);
            return true;
        }
        case C::intro_mail => {
            mail(c, [&](auto& e) {
                construct_tag_system_company_email(c, &e, 92, 5, 1);
            });
            return true;
        }
        case C::objective_mail => {
            mail(c, [&](auto& e) {
                const auto row = s.objective_mail_index;
                s.objective_mail_index = fixed::add(row, 1);
                construct_objective_complete_company_email(c, &e, row);
            });
            return true;
        }
        case C::spam_test => {
            for (int i = 0; i < 10; ++i) {
                const auto& entry = c.finance.spam_scheduler.entries[static_cast<std::size_t>(next_crt_random(c) % 17)];
                dispatch_spam_email(c, entry.resource, entry.category);
            }
            c.finance.spam_scheduler.manual_send = !c.finance.spam_scheduler.manual_send;
            return true;
        }
        case C::ticket => {
            auto& manager = get_or_create_theme_global_info_manager(c);
            manager.golden_ticket_balance = fixed::add(manager.golden_ticket_balance, 1);
            return true;
        }
        case C::obstacle_message => {
            emit(c, WorldEvent{ CurrentObstacleEvent{ current(c).saved.current_obstacle } });
            return true;
        }
        case C::open_obstacle => {
            emit(c, WorldEvent{ ObstacleRemovedEvent{ current(c).saved.current_obstacle } });
            return true;
        }
        case C::send_entertainer => {
            auto& w = current(c);
            auto& staff = require<StaffRuntimeRecord>(c, w.saved.first_entertainer, ThingKind::entertainer);
            if (!resolve_mission_obstacle(c))
                throw std::runtime_error("The required mission obstacle is not available.");
            enter_world_staff_state(c, staff, StaffState::walking_to_obstacle);
            return true;
        }
        case _ => return false;
    }
}
} //namespace simcoaster
