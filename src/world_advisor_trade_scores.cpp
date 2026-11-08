#include "simcoaster/world_advisor.hpp"
#include "simcoaster/advisor_balance.hpp"
#include "simcoaster/world_challenge.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/object_balance.hpp"
#include <algorithm>

namespace simcoaster {
namespace {
enum class TradeGroup {
    food,
    retail,
    restaurant,
    drink,
    sideshow
};
enum class TradeMetric {
    walkaway_ratio,
    satisfaction
};

struct Rule {
    std::int32_t threshold, per_point;
};

template <class Parameters>
Rule rule(const Parameters& parameters) {
    const auto& [threshold, per_point] = parameters;
    return { threshold, per_point };
}

bool accepts(const ResearchCatalogDefinition& definition, TradeGroup group) {
    if (group == TradeGroup::sideshow)
        return definition.which_ui_type == ObjectUiCategory::sideshows;
    if (!definition.balance)
        throw std::logic_error("advisor trade definition balance");
    const auto type = definition.balance->UsageInfo.ShopType;
    return match (group) -> bool {
        case TradeGroup::food       => type == 2;
        case TradeGroup::retail     => type == 1 || type == 5 || type == 6 || type == 8;
        case TradeGroup::restaurant => type == 3;
        case TradeGroup::drink      => type == 4;
        case _                      => false;
    };
}

struct Selection {
    ThingId id{};
    std::int32_t value{};
};

Selection select(AdvisorWorld& c, TradeGroup group, TradeMetric metric, bool maximum) {
    Selection best;
    for (auto* row = c.entities.active_head; row;) {
        const auto* header = row->payload;
        if (!header)
            throw std::logic_error("null advisor trade record");
        if (header->kind == ThingKind::park_object) {
            const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(header);
            const auto* definition = get_park_object_definition(c, &object);
            if (!definition)
                throw std::logic_error("missing advisor trade definition");
            if (accepts(*definition, group) && fixed::narrow(get_park_object_age_in_days(c, object)) > 30) {
                std::optional<std::int32_t> value;
                if (metric == TradeMetric::satisfaction) {
                    //The sideshow maximum additionally requires at least one use;
                    //the shop-type satisfaction selectors admit zero-use records.
                    if (group != TradeGroup::sideshow || sum_recent_park_object_uses(object))
                        value = calculate_park_object_customer_satisfaction(object);
                } else if (const auto uses = sum_recent_park_object_uses(object)) {
                    //Native multiplies and divides unsigned32 words, then compares the
                    //quotient as signed. Keep wrapping and first-encounter tie selection.
                    const auto numerator = 100u * static_cast<std::uint32_t>(sum_recent_park_object_walkaways(object));
                    value = fixed::word(numerator / static_cast<std::uint32_t>(uses));
                }
                if (value && (!best.id || (maximum ? *value > best.value : *value < best.value)))
                    best = { header->id, *value };
            }
        }
        row = c.entities.at(header->id).active_next;
    }
    return best;
}
} //namespace

std::optional<std::int32_t> evaluate_advisor_trade_score(AdvisorWorld& c, std::int32_t action) {
    if (action < 199 || action > 218)
        return {};
    const bool great = action >= 209;
    const auto& policy = *c.advisor.balance;
    if (!great && (!policy.GeneralAdvisor.UISelectionModeMessageFilter || c.construction.mode))
        return 0;
    const auto group = static_cast<TradeGroup>((action - 199) % 5);
    //Action208's poor-satisfaction label is misleading: the executable calls
    //the walkaway-ratio selector. Action218 really does use satisfaction.
    const bool satisfaction = (action >= 204 && action <= 208) || action >= 214;
    const auto metric = satisfaction && action != 208 ? TradeMetric::satisfaction : TradeMetric::walkaway_ratio;
    const auto selected = select(c, group, metric, great);
    if (!selected.id)
        return 0;
    const auto parameters = match (action) -> Rule {
        case 199 => rule(policy.FoodPoorValue);
        case 200 => rule(policy.ShopsPoorValue);
        case 201 => rule(policy.RestaurantPoorValue);
        case 202 => rule(policy.DrinksPoorValue);
        case 203 => rule(policy.SideshowsPoorValue);
        case 204 => rule(policy.FoodPoorSatisfaction);
        case 205 => rule(policy.ShopsPoorSatisfaction);
        case 206 => rule(policy.RestaurantPoorSatisfaction);
        case 207 => rule(policy.DrinksPoorSatisfaction);
        case 208 => rule(policy.SideshowsPoorSatisfaction);
        case 209 => rule(policy.FoodGreatValue);
        case 210 => rule(policy.ShopsGreatValue);
        case 211 => rule(policy.RestaurantGreatValue);
        case 212 => rule(policy.DrinksGreatValue);
        case 213 => rule(policy.SideshowsGreatValue);
        case 214 => rule(policy.FoodGreatSatisfaction);
        case 215 => rule(policy.ShopsGreatSatisfaction);
        case 216 => rule(policy.RestaurantGreatSatisfaction);
        case 217 => rule(policy.DrinksGreatSatisfaction);
        case _   => rule(policy.SideshowsGreatSatisfaction);
    };
    if (great ? selected.value <= parameters.threshold : selected.value >= parameters.threshold)
        return 0;
    c.advisor.behavior.callback_thing_id = selected.id;
    c.advisor.behavior.callback_secondary = static_cast<std::uint32_t>(selected.value);
    return fixed::word(static_cast<std::uint32_t>(parameters.per_point)
                       * static_cast<std::uint32_t>(fixed::subtract(selected.value, parameters.threshold)));
}
} //namespace simcoaster
