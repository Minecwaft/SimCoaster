#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"
#include <stdexcept>

namespace simcoaster {
void receive_world_advisor_message(AdvisorWorld& c, AdvisorRecord& advisor, const WorldEvent& event) {
    const auto queue = [&](std::int32_t action, ThingId thing = 0, std::uint32_t secondary = 0) {
        auto candidate = world_operations::make_advisor_action_candidate(c, action);
        candidate.thing_id = thing;
        candidate.secondary = secondary;
        (void)world_operations::queue_advisor_action_candidate(c, &advisor, candidate);
    };
    const auto active = [&](std::size_t action) {
        return get_or_create_theme_global_info_manager(c).history.at(action).is_active != 0;
    };
    match (event.type()) {
        case 5 => {
            const auto& candidate = event.get<AdvisorActionEvent>().candidate;
            if (candidate.valid)
                (void)world_operations::queue_advisor_action_candidate(c, &advisor, candidate);
        }
        case 8  => queue(219, event.get<ParkObjectCondemnedEvent>().park_object_id);
        case 14 => {
            const auto& prank = event.get<PersonActionEvent>();
            const auto* payload = c.entities.at(prank.person_id).payload;
            if (!payload)
                throw std::logic_error("Advisor prank actor is missing");
            const auto& actor = *reinterpret_cast<const PersonRuntimeView*>(payload);
            const auto cell = static_cast<CellId>((actor.base.x >> 8) + ((actor.base.y >> 8) << 7) + 1u);
            queue(is_first_hour_of_gameplay(c) ? 516 : active(393) ? 292 : 393, prank.person_id, cell);
            match (prank.action) {
                case PersonActionCode::stink_bomb        => queue(active(386) ? 116 : 386, prank.person_id, cell);
                case PersonActionCode::toilet_booby_trap => queue(active(388) ? 285 : 388, prank.person_id, cell);
                case PersonActionCode::litter_bin_prank  => {
                    queue(active(389) ? 286 : 389, prank.person_id, cell);
                    queue(287, prank.person_id, cell);
                }
                case PersonActionCode::vegetation_fire_prank => {
                    queue(active(390) ? 288 : 390, prank.person_id, cell);
                    queue(289, prank.person_id, cell);
                }
                case PersonActionCode::scatter_litter         => queue(117, prank.person_id, cell);
                case PersonActionCode::balloon_prank          => queue(active(387) ? 118 : 387, prank.person_id, cell);
                case PersonActionCode::spread_vicious_rumours => queue(active(391) ? 290 : 391, prank.person_id, cell);
                case PersonActionCode::whinge_at_staff        => queue(active(392) ? 291 : 392, prank.person_id, cell);
                case _                                        => ;
            }
        }
        case 17 => {
            const auto& research = event.get<ResearchAvailabilityEvent>();
            const auto category = research.category;
            const auto action = match (category) -> std::int32_t {
                case ResearchCategory::rides      => 88;
                case ResearchCategory::shops      => 90;
                case ResearchCategory::sideshows  => 91;
                case ResearchCategory::features   => 92;
                case ResearchCategory::facilities => 283;
                case _                            => -1;
            };
            if (action != -1) {
                queue(action, 0, research.definition_id);
                if (category == ResearchCategory::facilities)
                    c.ports.diagnostics.log_debug_message(c,
                                                          "unknown research category in CAdvisor::ReceiveMessage.\n");
                return;
            }
            if (category != ResearchCategory::upgrades) {
                c.ports.diagnostics.log_debug_message(c, "unknown research category in CAdvisor::ReceiveMessage.\n");
                return;
            }
            if (!c.referenced_world())
                throw std::logic_error("Advisor primary World is missing");
            for (auto id = c.referenced_world()->saved.first_mechanic; id;) {
                const auto* staff = c.entities.at_payload<const StaffRuntimeRecord>(id);
                if (!staff)
                    throw std::logic_error("Advisor mechanic roster entry is missing");
                if (staff->first_known_ability == ThemeAbility::ride_upgrading) {
                    queue(89, 0, research.definition_id);
                    return;
                }
                id = staff->next_staff;
            }
            queue(active(396) ? 374 : 396, 0, research.definition_id);
        }
        case 19 => {
            match (event.get<Tag19Event>().tag) {
                case 0 => {
                    if (static_cast<std::int32_t>(c.mode.game) == 2)
                        queue(255);
                }
                case 2  => queue(102);
                case 3  => queue(119);
                case 4  => queue(120);
                case 10 => {
                    for (auto& history : get_or_create_theme_global_info_manager(c).history) {
                        history.last_response_variant_index = -1;
                        history.is_active = 0;
                        history.cancel_count = history.play_count = 0;
                    }
                }
                case _ => ;
            }
        }
        case 23 => {
            const auto& strike = event.get<StaffStrikeEvent>();
            auto base = match (strike.parameter) -> std::int32_t {
                case 2 => 34;
                case 3 => 477;
                case 4 => 465;
                case _ => -1;
            };
            if (base == -1)
                base = match (strike.code) -> std::int32_t {
                    case 2 => 40;
                    case 3 => 46;
                    case 5 => 52;
                    case _ => -1;
                };
            if (base != -1) {
                constexpr std::array<std::int32_t, 6> offsets{ 1, 0, 3, 2, 4, 5 };
                queue(base + offsets.at(static_cast<std::size_t>(strike.category)));
            }
        }
        case _ => ; //Native re-queries the event kind and invokes a no-op stub.
    }
}
} //namespace simcoaster
