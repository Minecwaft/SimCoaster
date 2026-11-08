#include "world_person_internal.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_analyser.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/object_balance.hpp"
#include <algorithm>

namespace simcoaster {
bool is_object_price_too_high_for_person(VisitorWorld& c, PersonRuntimeView& person, ParkObjectRuntimeView& object) {
    const auto price = object.price_per_use;
    if (!price)
        return false;
    const auto* definition = get_park_object_definition(c, &object);
    const auto& balance = *definition->balance;
    std::int32_t expected;
    if (definition->which_ui_type == ObjectUiCategory::sideshows) {
        const auto cost = get_park_object_definition(c, &object)->which_ui_type == ObjectUiCategory::sideshows
                            ? object.cost_of_goods
                            : 0u;
        expected = fixed::word(cost * static_cast<std::uint8_t>(object.chance_of_winning)) / 100;
    } else
        expected = balance.Info.ExpectedPrice;
    const auto effect = [](std::int32_t coefficient, std::int32_t amount) {
        return fixed::word(static_cast<std::uint32_t>(coefficient) * static_cast<std::uint32_t>(amount)) / 100;
    };
    const auto happiness = truncate_park_object_condition(person.happiness);
    auto usefulness =
        fixed::add(effect(balance.UsageInfo.ThirstEffect, truncate_park_object_condition(person.needs.thirst)),
                   effect(balance.UsageInfo.HungerEffect, truncate_park_object_condition(person.needs.hunger)));
    usefulness = fixed::add(usefulness, effect(balance.UsageInfo.HappinessEffect, 100 - happiness));
    usefulness =
        fixed::subtract(usefulness,
                        effect(balance.UsageInfo.VomitEffect, truncate_park_object_condition(person.needs.sickness)));
    usefulness = fixed::add(usefulness, 100);
    auto threshold = static_cast<std::uint32_t>(expected) * static_cast<std::uint32_t>(usefulness) / 100u;
    const auto* control =
        find_and_cache_world_object_control(person_detail::base_world(c).object_controls, object.definition_id);
    if (!control)
        throw std::logic_error("guest price check has no object control");
    threshold = (happiness + 100u) * (threshold * (control->copied_rip_off_ok + 100u) / 100u) / 100u;
    if (get_park_object_excitement(c, object, false)) {
        const auto difference = std::min(std::abs(int(static_cast<std::uint8_t>(person.preferred_excitement))
                                                  - int(get_park_object_excitement(c, object, false))),
                                         100);
        threshold = threshold * static_cast<std::uint32_t>(50 * (100 - difference) / 100 + 100) / 100u;
    }
    const auto category = get_park_object_definition(c, &object)->which_ui_type;
    const auto sample = static_cast<std::uint8_t>(std::min(2u * (5u * threshold + 25u) - 10u * price, 100u));
    const auto record = [&](std::size_t series) {
        auto& analyser = *reinterpret_cast<ParkAnalyserState*>(
            &person_detail::map_object(c, person_detail::primary_world(c).saved.park_analyser));
        append_park_analyser_byte_history(analyser, series, sample);
    };
    if (category == ObjectUiCategory::sideshows)
        record(10);
    else if (category == ObjectUiCategory::shops) {
        if (balance.UsageInfo.SpecialIngredient >= 1 && balance.UsageInfo.SpecialIngredient <= 4)
            record(static_cast<std::size_t>(balance.UsageInfo.SpecialIngredient));
        match (static_cast<PersonAppearanceEffect>(balance.UsageInfo.AppearanceEffect)) {
            case PersonAppearanceEffect::balloon => record(6);
            case PersonAppearanceEffect::costume => record(7);
            case PersonAppearanceEffect::umbrella => record(8);
            case _ => {}
        }
    }
    auto* history = object.history;
    if (price > threshold || static_cast<std::uint32_t>(person.cash) < price) {
        if (history) {
            ++history->dword_history_30[12].temporary;
            if (balance.Info.WhichUIType == std::to_underlying(ObjectUiCategory::sideshows))
                ++history->dword_history_30[9].temporary;
        }
        return true;
    }
    if (threshold * static_cast<std::uint32_t>(c.visitors.needs_balance.PeepInfo.PcOfGoodPriceIsCheap) / 100u > price
        && history)
        ++history->dword_history_30[11].temporary;
    return false;
}
} //namespace simcoaster
