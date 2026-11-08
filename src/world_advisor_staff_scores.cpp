#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/advisor_balance.hpp"
#include "simcoaster/world_analyser.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_special_rides.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/object_balance.hpp"
#include <vector>

namespace simcoaster {
namespace {
bool has(const ParkObjectRuntimeView& object, ParkObjectCapabilities flag) {
    return (std::to_underlying(object.capabilities) & std::to_underlying(flag)) != 0;
}

const ResearchCatalogDefinition& definition(AdvisorWorld& c, const ParkObjectRuntimeView& object) {
    const auto* value = get_park_object_definition(c, &object);
    if (!value || !value->balance) {
        throw std::logic_error("advisor object definition");
    }
    return *value;
}

struct Counts {
    std::int32_t total {}, closed {};
};

template <class Predicate>
Counts count_objects(AdvisorWorld& c, Predicate&& accept, bool exclude_special = false) {
    Counts result;
    for (auto* row = c.entities.active_head; row; row = row->active_next) {
        if (row->payload->kind != ThingKind::park_object) {
            continue;
        }
        const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(row->payload);
        if (!accept(object)) {
            continue;
        }
        //The original category total excludes special rides; the closed subset
        //does not. These intentionally are not complementary counters.
        if (!exclude_special || is_special_ride_group_id_unregistered(c, object.base.header.id)) {
            ++result.total;
        }
        if (!object.can_load) {
            ++result.closed;
        }
    }
    return result;
}

Counts category_counts(AdvisorWorld& c, ObjectUiCategory category) {
    return count_objects(
        c,
        [&](const auto& object) {
            return definition(c, object).which_ui_type == category;
        },
        true);
}

Counts capability_counts(AdvisorWorld& c, ParkObjectCapabilities flag) {
    return count_objects(c, [&](const auto& object) {
        return has(object, flag);
    });
}

bool enabled(const AdvisorQuery& c) {
    return c.advisor.balance->GeneralAdvisor.UISelectionModeMessageFilter && !c.construction.mode;
}

std::int32_t divide(std::int32_t value, std::int32_t denominator) {
    if (!denominator) {
        throw std::domain_error("advisor facility count division by zero");
    }
    return value / denominator;
}

std::array<CellId, 2> patrol_endpoints(const StaffRuntimeRecord& staff) {
    const auto& b = staff.area.bounds;
    return { static_cast<CellId>(static_cast<std::uint32_t>(b.start_x) + static_cast<std::uint32_t>(b.start_y) * 128u + 1u), static_cast<CellId>(static_cast<std::uint32_t>(b.end_x) + static_cast<std::uint32_t>(b.end_y) * 128u + 1u) };
}

bool custom_patrol(const StaffRuntimeRecord& staff) {
    const auto bounds = patrol_endpoints(staff);
    return bounds[0] != 1 || bounds[1] != map_cell_count;
}

bool has_plant_outside_gardener_bounds(AdvisorWorld& c) {
    std::vector<std::array<CellId, 2>> rectangles;
    for (auto* row = c.entities.active_head; row; row = row->active_next) {
        if (row->payload->kind == ThingKind::gardener) {
            const auto& staff = *reinterpret_cast<const StaffRuntimeRecord*>(row->payload);
            if (custom_patrol(staff)) {
                rectangles.push_back(patrol_endpoints(staff));
            }
        }
    }
    for (auto* row = c.entities.active_head; row; row = row->active_next) {
        if (row->payload->kind == ThingKind::park_object) {
            const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(row->payload);
            if (!has(object, ParkObjectCapabilities::requires_watering)) {
                continue;
            }
            const auto x = object.base.x >> 8, y = object.base.y >> 8;
            for (const auto& rectangle : rectangles) {
                const auto a = static_cast<std::uint32_t>(rectangle[0]) - 1u, b = static_cast<std::uint32_t>(rectangle[1]) - 1u;
                //Native tests outside ANY rectangle, rather than outside their union,
                //and does not normalize reversed corners. Preserve both decisions.
                if (x < static_cast<std::int32_t>(a & 127u) || x > static_cast<std::int32_t>(b & 127u) || y < static_cast<std::int32_t>(a >> 7) || y > static_cast<std::int32_t>(b >> 7)) {
                    return true;
                }
            }
        }
    }
    return false;
}

std::int32_t count_training_staff(AdvisorWorld& c) {
    std::int32_t count = 0;
    for (auto* row = c.entities.active_head; row; row = row->active_next) {
        if (is_staff_record_kind(row->payload->kind)) {
            const auto state = reinterpret_cast<const StaffRuntimeRecord*>(row->payload)->state;
            if (state == StaffState::walking_to_training_room || state == StaffState::training) {
                ++count;
            }
        }
    }
    return count;
}
} //namespace

std::optional<std::int32_t> evaluate_advisor_staff_and_facility_score(AdvisorWorld& c, std::int32_t action) {
    const auto& policy = *c.advisor.balance;
    if (action >= 220 && action <= 223) {
        auto* primary = c.referenced_world();
        if (!primary) {
            throw std::logic_error("advisor primary World");
        }
        if (!enabled(c) || primary->saved.park_closed) {
            return 0;
        }
        Counts counts;
        if (action == 223) {
            counts = capability_counts(c, ParkObjectCapabilities::provides_relief);
            auto* payload = c.entities.at(primary->saved.park_analyser).payload;
            if (!payload || !c.simulation.current) {
                throw std::logic_error("advisor toilet count cache");
            }
            reinterpret_cast<ParkAnalyserState*>(payload)->metrics[3] = { c.simulation.current->saved.game_tick, static_cast<std::uint32_t>(counts.total), 1 };
        } else if (action == 220) {
            counts = count_objects(c, [&](const auto& object) {
                return definition(c, object).which_ui_type == ObjectUiCategory::rides && is_special_ride_group_id_unregistered(c, object.base.header.id);
            });
        } else {
            counts = category_counts(c, action == 221 ? ObjectUiCategory::shops : ObjectUiCategory::sideshows);
        }
        if (!counts.total || (action == 220 && counts.total < policy.RidesClosed.AtLeastThisManyRides)) {
            return 0;
        }
        const auto threshold = action == 220 ? policy.RidesClosed.MoreThanThisPercentClosed : action == 221 ? policy.ShopsClosed.MoreThanThisPercentClosed : action == 222 ? policy.SideshowsClosed.MoreThanThisPercentClosed : policy.ToiletsClosed.MoreThanThisPercentClosed;
        return divide(numeric::product(100, counts.closed), counts.total) > threshold ? (action == 220 ? policy.RidesClosed.Score : action == 221 ? policy.ShopsClosed.Score : action == 222 ? policy.SideshowsClosed.Score : policy.ToiletsClosed.Score) : 0;
    }
    if (action == 141) {
        if (!enabled(c)) {
            return 0;
        }
        std::int32_t count = 0, patrolled = 0;
        for (auto* row = c.entities.active_head; row; row = row->active_next) {
            if (is_staff_record_kind(row->payload->kind)) {
                ++count;
                if (custom_patrol(*reinterpret_cast<const StaffRuntimeRecord*>(row->payload))) {
                    ++patrolled;
                }
            }
        }
        return count > policy.SetPatrolsGeneral.MinStaffForMessage && !patrolled ? policy.SetPatrolsGeneral.Score : 0;
    }
    if (action >= 104 && action <= 108) {
        if (!enabled(c)) {
            return 0;
        }
        const auto score = [&](ThingKind role, const auto& parameters) {
            const auto threshold = fixed::add(parameters.FudgeFactor, category_counts(c, ObjectUiCategory::rides).total);
            return count_linked_staff_by_kind(c, std::to_underlying(role)) > threshold ? parameters.Score : 0;
        };
        return match (action) -> std::int32_t {
            case 104 => score(ThingKind::handyman, policy.LotsHandyman);
            case 105 => score(ThingKind::entertainer, policy.LotsEntertainers);
            case 106 => score(ThingKind::mechanic, policy.LotsMechanics);
            case 107 => score(ThingKind::guard, policy.LotsSecurity);
            case _   => score(ThingKind::researcher, policy.LotsResearchers);
        };
    }
    if ((action >= 110 && action <= 114) || action == 336) {
        if (!enabled(c)) {
            return 0;
        }
        const auto& pool = c.staff.recruitment.balance.pool;
        const auto score = [&](ThingKind role, std::int32_t limit, const auto& configured, const auto& alternate) {
            return limit != configured.MoreThan && count_linked_staff_by_kind(c, std::to_underlying(role)) == limit ? alternate.Score : 0;
        };
        return match (action) -> std::int32_t {
            case 110 => score(ThingKind::handyman, pool.MaxHandymenInPark, policy.MaxHandymen, policy.MaxHandymenV2);
            case 111 => score(ThingKind::entertainer, pool.MaxEntertainersInPark, policy.MaxEntertainers, policy.MaxEntertainersV2);
            case 112 => score(ThingKind::mechanic, pool.MaxMechanicsInPark, policy.MaxMechanics, policy.MaxMechanicsV2);
            case 113 => score(ThingKind::guard, pool.MaxGuardsInPark, policy.MaxGuards, policy.MaxGuardsV2);
            case 114 => score(ThingKind::researcher, pool.MaxResearchersInPark, policy.MaxResearchers, policy.MaxResearchersV2);
            case _   => count_linked_staff_by_kind(c, std::to_underlying(ThingKind::gardener)) == pool.MaxGardenersInPark ? policy.MaxGardenersV2.Score : 0;
        };
    }
    if (action == 277 || action == 278 || action == 284) {
        if (!enabled(c)) {
            return 0;
        }
        const auto plants = capability_counts(c, ParkObjectCapabilities::requires_watering).total;
        if (action == 284) {
            const auto gardeners = count_linked_staff_by_kind(c, std::to_underlying(ThingKind::gardener));
            const auto& p = policy.LotsGardeners;
            return gardeners >= p.MinimumNumberOfGardeners && static_cast<double>(p.Ratio) <= static_cast<double>(gardeners) / plants ? p.Score : 0;
        }
        if (is_first_hour_of_gameplay(c)) {
            return 0;
        }
        std::int32_t patrolled = 0;
        for (auto* row = c.entities.active_head; row; row = row->active_next) {
            if (row->payload->kind == ThingKind::gardener && custom_patrol(*reinterpret_cast<const StaffRuntimeRecord*>(row->payload))) {
                ++patrolled;
            }
        }
        if (action == 277) {
            return plants && !patrolled && count_linked_staff_by_kind(c, std::to_underlying(ThingKind::gardener)) ? policy.StaffSetGardenerArea.Score : 0;
        }
        return patrolled && has_plant_outside_gardener_bounds(c) ? policy.StaffGardenerCoverage.Score : 0;
    }
    if (action < 301 || action > 313) {
        return {};
    }
    auto* primary = c.referenced_world();
    if (!primary) {
        throw std::logic_error("advisor primary World");
    }
    if (action <= 309 && primary->saved.park_closed) {
        return 0;
    }
    if (action <= 303) {
        const auto [total, closed] = category_counts(c, action == 301 ? ObjectUiCategory::rides : action == 302 ? ObjectUiCategory::shops : ObjectUiCategory::sideshows);
        const auto score = action == 301 ? policy.RidesClosed.AllScore : action == 302 ? policy.ShopsClosed.AllScore : policy.SideshowsClosed.AllScore;
        return total == closed ? score : 0; //Includes the original zero == zero case.
    }
    if (action <= 306) {
        const auto [total, closed] = count_objects(c, [&](const auto& object) {
            return has(object, ParkObjectCapabilities::provides_relief) && (action == 304 || definition(c, object).balance->Info.GenderSpecific == action - 304);
        });
        if (action == 304) {
            const auto* header = c.entities.at(primary->saved.park_analyser).payload;
            if (!header || !c.simulation.current) {
                throw std::logic_error("advisor toilet count cache");
            }
            reinterpret_cast<ParkAnalyserState*>(const_cast<ThingHeader*>(header))->metrics[3] = { c.simulation.current->saved.game_tick, static_cast<std::uint32_t>(total), 1 };
        }
        return total == closed ? (action == 304 ? policy.ToiletsClosed.AllScore : action == 305 ? policy.ToiletsClosed.AllMScore : policy.ToiletsClosed.AllFScore) : 0;
    }
    if (action <= 309) {
        const auto [total, closed] = count_objects(c, [&](const auto& object) {
            const auto& b = *definition(c, object).balance;
            return action == 309 ? b.Info.IsPhotoBooth == 1 : b.UsageInfo.IsInfoDesk == 1;
        });
        if (action == 308) {
            return total > 0 && total == closed ? policy.InfoKiosksAllClosed.Score : 0;
        }
        const auto minimum = action == 307 ? policy.InfoKiosks.MinimumNumber : policy.PhotoBooths.MinimumNumber;
        const auto percent = action == 307 ? policy.InfoKiosks.PercentageClosed : policy.PhotoBooths.PercentageClosed;
        //Integer division BEFORE multiplication is deliberate (307 and309).
        return total > minimum && numeric::product(closed, divide(100, total)) > percent ? (action == 307 ? policy.InfoKiosks.Score : policy.PhotoBooths.Score) : 0;
    }
    if (action <= 311) {
        const auto [total, closed] = capability_counts(c, ParkObjectCapabilities::chills_you_out);
        if (action == 311) {
            return total > 0 && total == closed ? policy.StaffroomsAllClosed.Score : 0;
        }
        const auto& p = policy.StaffroomsClosed;
        if (total < p.MoreThanThisNumber) {
            return 0;
        }
        std::int32_t staff = 0;
        for (auto* row = c.entities.active_head; row; row = row->active_next) {
            if (is_staff_record_kind(row->payload->kind)) {
                ++staff;
            }
        }
        const auto open_capacity = numeric::product(total - closed, c.staff.recruitment.balance.all.StaffRoomCapacity);
        //The native condition really is staff < available capacity.
        return divide(numeric::product(100, closed), total) > p.MoreThanThisPercentClosed && staff < open_capacity ? p.Score : 0;
    }
    if (action == 312) {
        if (!enabled(c) || is_first_hour_of_gameplay(c)) {
            return 0;
        }
        return !capability_counts(c, ParkObjectCapabilities::training_room).total && get_or_create_theme_global_info_manager(c).mission_number ? policy.TrainingRoomBuild.Score : 0;
    }
    const auto [total, closed] = capability_counts(c, ParkObjectCapabilities::training_room);
    return total == closed && count_training_staff(c) > 0 ? 10000 : 0;
}
} //namespace simcoaster
