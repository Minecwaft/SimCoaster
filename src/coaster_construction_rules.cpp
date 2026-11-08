#include "simcoaster/coaster_construction.hpp"
#include <algorithm>
#include <cmath>

namespace simcoaster {
CoasterPlacementRules coaster_placement_rules(const DynamicTexturePylonBalance& balance, bool special) {
    CoasterPlacementRules rules;
    if (special) {
        rules.entrance_angles = { 2.0943952f, 3.0368729f, 2.0943952f, 3.0368729f };
        rules.ordinary_angles = { 0, 3.0368729f, 0, 3.0368729f };
        rules.maximum_distance = 9;
        rules.maximum_distance_squared = 81;
        rules.maximum_rotation_deviation = rules.maximum_loop_rotation_deviation = 1.5707964f;
        rules.maximum_loop_height_difference = 25;
    }
    const auto replace = [](float& target, float value) {
        if (value != 0)
            target = value;
    };
    replace(rules.minimum_height, balance.fMinHeight);
    replace(rules.maximum_height, balance.fMaxHeight);
    replace(rules.height_gradient, balance.fMaxHeightDiffPerGSquareDist);
    const auto radians = [](float degrees) {
        return static_cast<float>(static_cast<double>(degrees) * static_cast<double>(0.017453292f));
    };
    const std::array entrance{ balance.fEntranceMinXZAngleAtMinDist,
                               balance.fEntranceMaxXZAngleAtMinDist,
                               balance.fEntranceMinXZAngleAtMaxDist,
                               balance.fEntranceMaxXZAngleAtMaxDist };
    const std::array ordinary{ balance.fMinXZAngleAtMinDist,
                               balance.fMaxXZAngleAtMinDist,
                               balance.fMinXZAngleAtMaxDist,
                               balance.fMaxXZAngleAtMaxDist };
    for (std::size_t i = 0; i < 4; ++i) {
        if (entrance[i] != 0)
            rules.entrance_angles[i] = radians(entrance[i]);
        if (ordinary[i] != 0)
            rules.ordinary_angles[i] = radians(ordinary[i]);
    }
    if (balance.uiMinPlaceDist) {
        rules.minimum_distance = static_cast<float>(balance.uiMinPlaceDist);
        const auto value = static_cast<std::uint32_t>(balance.uiMinPlaceDist);
        rules.minimum_distance_squared = value * value;
    }
    if (balance.uiMaxPlaceDist) {
        rules.maximum_distance = static_cast<float>(balance.uiMaxPlaceDist);
        const auto value = static_cast<std::uint32_t>(balance.uiMaxPlaceDist);
        rules.maximum_distance_squared = value * value;
    }
    if (balance.uiMaxStackedPylons)
        rules.maximum_stack = static_cast<std::uint32_t>(balance.uiMaxStackedPylons);
    if (balance.fMaxRotnAngleDeviance != 0)
        rules.maximum_rotation_deviation = radians(balance.fMaxRotnAngleDeviance);
    if (balance.fMaxLoopRotnAngleDeviance != 0)
        rules.maximum_loop_rotation_deviation = radians(balance.fMaxLoopRotnAngleDeviance);
    if (balance.fMaxLoopHeightDifference != 0)
        rules.maximum_loop_height_difference = static_cast<float>(
            static_cast<double>(balance.fMaxLoopHeightDifference) * 10.0 * static_cast<double>(0.017453292f));
    rules.cost_per_unit = balance.fCostPerUnit;
    rules.cost_per_length = balance.fCostPer2DLength;
    rules.cost_per_height = balance.fCostPerHeight;
    return rules;
}

CoasterPlacementValidity validate_coaster_connection(const CoasterPlacementRules& rules,
                                                     const CoasterConnectionCandidate& c) {
    if (c.origin == c.candidate)
        return CoasterPlacementValidity::rejected;
    const auto dx = static_cast<std::uint32_t>(c.origin[0]) - static_cast<std::uint32_t>(c.candidate[0]);
    const auto dy = static_cast<std::uint32_t>(c.origin[1]) - static_cast<std::uint32_t>(c.candidate[1]);
    const auto squared = dx * dx + dy * dy;
    if ((c.origin_ordinal == 2 || !(c.origin_owner_flags & 2u))
        && (squared < rules.minimum_distance_squared || squared > rules.maximum_distance_squared))
        return squared < rules.minimum_distance_squared ? CoasterPlacementValidity::too_close
                                                        : CoasterPlacementValidity::rejected;
    if (c.preceding) {
        const auto outgoing_x = static_cast<double>(c.candidate[0]) - static_cast<float>(c.origin[0]);
        const auto outgoing_y =
            static_cast<float>(static_cast<double>(c.candidate[1]) - static_cast<float>(c.origin[1]));
        const auto incoming_x = static_cast<float>(static_cast<float>((*c.preceding)[0])
                                                   - static_cast<double>(static_cast<float>(c.origin[0])));
        const auto incoming_y = static_cast<float>(static_cast<float>((*c.preceding)[1])
                                                   - static_cast<double>(static_cast<float>(c.origin[1])));
        const auto outgoing_length =
            static_cast<float>(std::sqrt(outgoing_x * outgoing_x + static_cast<double>(outgoing_y) * outgoing_y));
        const auto incoming_length =
            std::sqrt(static_cast<double>(incoming_x) * incoming_x + static_cast<double>(incoming_y) * incoming_y);
        const auto incoming_unit_x = static_cast<float>(incoming_x / incoming_length);
        auto dot = static_cast<float>(incoming_y / incoming_length * (outgoing_y / static_cast<double>(outgoing_length))
                                      + incoming_unit_x * (outgoing_x / outgoing_length));
        //4740E0 treats an unordered comparison as the lower clamp. In
        //particular, a zero incoming vector becomes acos(-1), not an accepted
        //NaN angle. Express that policy without floating-point control state.
        if (!(dot >= -1.0f))
            dot = -1.0f;
        else if (dot > 1.0f)
            dot = 1.0f;
        const auto angle = std::abs(std::acos(static_cast<double>(dot)));
        auto blend = static_cast<float>((std::sqrt(static_cast<double>(squared)) - rules.minimum_distance)
                                        / (rules.maximum_distance - rules.minimum_distance));
        blend = std::clamp(blend, 0.0f, 1.0f);
        const auto near = static_cast<float>(1.0 - static_cast<double>(blend));
        const auto& limits = (c.origin_owner_flags & 2u) ? rules.entrance_angles : rules.ordinary_angles;
        const auto minimum = static_cast<double>(limits[0]) * near + static_cast<double>(limits[2]) * blend;
        const auto maximum =
            static_cast<float>(static_cast<double>(limits[1]) * near + static_cast<double>(limits[3]) * blend);
        if (angle < minimum || angle > maximum)
            return CoasterPlacementValidity::turn_angle;
    }
    return CoasterPlacementValidity::valid;
}

CoasterSupportSelection select_coaster_support_level(float requested,
                                                     const CoasterPlacementRules& rules,
                                                     const CoasterPylonRuntime& data,
                                                     std::span<const CoasterSupportLevel> levels,
                                                     bool moving_existing) {
    requested = std::min(requested, rules.maximum_height);
    const auto cumulative = [&](std::size_t last) {
        double result = 0;
        for (std::size_t i = 0; i <= last; ++i)
            result += (static_cast<double>(data.height_maximum) - data.height_minimum) * levels[i].loft
                    + data.height_minimum
                    + (i < last ? data.height_offset : 0);
        return result;
    };
    if (!moving_existing && !levels.empty() && (levels.front().flags & 0x40u)) {
        const auto height = cumulative(levels.size() - 1);
        if (height + rules.minimum_height + data.height_offset < rules.maximum_height)
            return { levels.size(),
                     static_cast<float>(std::max(static_cast<double>(requested),
                                                 height + data.height_offset + rules.minimum_height)) };
    }
    double previous = 0;
    std::optional<CoasterSupportSelection> last;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        const auto height = static_cast<float>(cumulative(i));
        const auto spacing = i ? 1.0 : 0.0;
        if ((spacing + 1) * data.height_offset + rules.minimum_height + rules.minimum_height
            <= height - previous + static_cast<double>(0.01f)) {
            if (height - data.height_offset - rules.minimum_height + static_cast<double>(0.01f) > requested)
                return { i, requested };
            last = CoasterSupportSelection{ i,
                                            static_cast<float>(
                                                previous + spacing * data.height_offset + rules.minimum_height) };
        }
        previous = height;
    }
    const auto offset = levels.empty() ? 0.0 : static_cast<double>(data.height_offset);
    if (offset + rules.minimum_height + previous >= rules.maximum_height) {
        if (last)
            return *last;
        return { levels.size(), static_cast<float>(previous + data.height_offset + rules.minimum_height) };
    }
    return { levels.size(),
             static_cast<float>(
                 std::min(static_cast<double>(rules.maximum_height),
                          std::max(static_cast<double>(requested), previous + offset + rules.minimum_height))) };
}
} //namespace simcoaster
