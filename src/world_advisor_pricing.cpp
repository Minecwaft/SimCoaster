#include "advisor_score_context.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_theme.hpp"

namespace simcoaster {
namespace {
using namespace advisor_detail;
enum class PricingGroup {
    rides,
    shops,
    sideshows,
    photo_booths,
    toilets,
    all
};

bool accepts(AdvisorWorld& c, const ParkObjectRuntimeView& object, PricingGroup group) {
    const auto& d = definition(c, object);
    match (group) {
        case PricingGroup::rides => {
            return d.which_ui_type == ObjectUiCategory::rides
                && (d.is_special != 1 || get_park_object_activation_flag(&object));
        }
        case PricingGroup::shops => return d.which_ui_type == ObjectUiCategory::shops && !d.balance->Info.IsPhotoBooth;
        case PricingGroup::sideshows => return d.which_ui_type == ObjectUiCategory::sideshows;
        case PricingGroup::photo_booths => return d.balance->Info.IsPhotoBooth != 0;
        case PricingGroup::toilets => return has(object, ParkObjectCapabilities::provides_relief);
        case PricingGroup::all => {
            return static_cast<std::uint32_t>(d.which_ui_type) <= 2 || d.which_ui_type == ObjectUiCategory::facilities;
        }
        case _ => {}
    }
    return false;
}

struct PricingScan {
    ThingId selected{};
    std::uint32_t matching{}, total{};
};

PricingScan scan(AdvisorWorld& c, PricingGroup group, bool cheap) {
    PricingScan result;
    const auto& rule = c.advisor.balance->Pricing[static_cast<std::size_t>(group)];
    linked_objects(c, [&](const ParkObjectRuntimeView& o) {
        if (!accepts(c, o, group))
            return;
        ++result.total;
        if (!o.history)
            return;
        const auto& h = o.history->dword_history_30;
        const auto unsigned_sample = [](std::uint32_t v) {
            return v;
        };
        const auto visits = fixed::truncate_integer(sum(h[2], 30, unsigned_sample) + sum(h[3], 30, unsigned_sample));
        if (!visits)
            return;
        const auto votes = sum(h[cheap ? 11 : 12], 30, unsigned_sample);
        if (votes > 0 && static_cast<double>(cheap ? rule.TooCheap : rule.TooExpensive) < votes * 100.0 / visits) {
            result.selected = o.base.header.id;
            ++result.matching;
        }
    });
    //These are decision-counter widths, not host pointer or record layouts.
    //The original local packed result exposes only these bits to callers.
    result.matching &= 255u;
    result.total &= 1023u;
    return result;
}

bool due(AdvisorWorld& c, PricingGroup group) {
    if (primary(c).saved.park_closed)
        return false;
    const auto& p = c.advisor.balance->Pricing[static_cast<std::size_t>(group)];
    //Draw precedes the calendar gate even when that gate will reject.
    const auto chance = static_cast<std::int32_t>(static_cast<std::uint32_t>(next_world_prng_value(primary(c))) % 100u);
    return chance <= p.PercentageChanceToHappen
        && std::bit_cast<std::int64_t>(
               scale_world_ticks_to_filetime_delta(primary(c).calendar, base(c).saved.game_tick))
                   / 25920000000000ll
               >= p.MonthsTilFirst;
}
} //namespace

std::optional<std::int32_t> evaluate_advisor_pricing_score(AdvisorWorld& c, std::int32_t action) {
    if (action < 350 || action > 369)
        return {};
    const bool single = action < 359;
    const auto group = static_cast<PricingGroup>(single ? (action - 350) / 2 : action >= 368 ? 5 : (action - 359) / 2);
    const bool cheap = single ? ((action - 350) % 2 != 0) : ((action >= 368 ? action - 368 : action - 359) % 2 != 0);
    if ((single || cheap) && !enabled(c))
        return 0;
    if (!due(c, group))
        return 0;
    const auto result = scan(c, group, cheap);
    if (single) {
        if (result.matching != 1)
            return 0;
        select(c, result.selected);
    } else if (result.matching <= 1
               || !(static_cast<double>(c.advisor.balance->Lots.Score)
                    <= static_cast<double>(result.matching) / result.total * 100.0))
        return 0;
    const auto& p = c.advisor.balance->Pricing[static_cast<std::size_t>(group)];
    //Action366 is the native photo-booth cheap-ratio/expensive-score mismatch.
    return cheap && action != 366 ? p.TooCheapScore : p.TooExpensiveScore;
}
} //namespace simcoaster
