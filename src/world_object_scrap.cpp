#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_calendar.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
const CatalogUpgradeResearchRow& upgrade(AttractionWorld& c, const ParkObjectRuntimeView* object, bool first = false) {
    auto* definition = get_park_object_definition(c, object);
    if (!definition) {
        throw std::invalid_argument("null scrap object definition");
    }
    return definition->research.upgrades.at(first ? 0 : object->current_upgrade);
}
} //namespace

std::int32_t sum_recent_park_thing_history_values(const AnalyserDwordHistory144& history, double& sum, std::int32_t maximum) {
    const auto available = history.wrapped ? history.num_entries : numeric::word(numeric::bits(history.current_entry) + 1u);
    const auto count = std::min(maximum, available);
    sum = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        if (i >= history.num_entries) {
            continue;
        }
        std::int32_t index;
        if (i <= history.current_entry) {
            index = numeric::word(numeric::bits(history.current_entry) - numeric::bits(i));
        } else if (history.wrapped) {
            index = numeric::word(numeric::bits(history.num_entries) + numeric::bits(history.current_entry) - numeric::bits(i));
        } else {
            continue;
        }
        if (index < 0 || static_cast<std::size_t>(index) >= history.data.size()) {
            throw std::out_of_range("scrap history index");
        }
        sum += history.data[static_cast<std::size_t>(index)];
    }
    return count;
}

std::int32_t get_park_object_scrap_value_percent(AttractionWorld& c, ParkObjectRuntimeView* object) {
    if (!object) {
        throw std::invalid_argument("null scrap park object");
    }
    if ((static_cast<std::uint16_t>(object->capabilities) >> 8) & 16u) {
        auto* script = world_operations::find_script(c, object->script_id);
        const auto condition = world_operations::script_slot(c, script, 0);
        const auto remaining = 3.0 - static_cast<double>(condition);
        //The native constant is a single-precision33.333332 value. Ordinary
        //floating arithmetic expresses this formula without x87 state or ABI.
        const auto value = static_cast<std::int64_t>(remaining * static_cast<double>(33.333332f));
        return numeric::word(static_cast<std::uint32_t>(value) + static_cast<std::uint32_t>(remaining != 0));
    }
    if (object->state == ParkObjectState::condemned) {
        return 0;
    }
    if (!c.simulation.current) {
        throw std::invalid_argument("null scrap calendar World");
    }
    FileTime now;
    calculate_world_calendar_filetime(c, &c.simulation.current->calendar, &now);
    const auto now_bits = static_cast<std::uint64_t>(now.low) | (static_cast<std::uint64_t>(now.high) << 32);
    const auto age = std::bit_cast<std::int64_t>(now_bits - object->creation_file_time);
    std::int32_t used = 0;
    if (object->history) {
        //At most144 unsignedDWORDs are summed, exactly representable in double
        //and int64. Only the native lowDWORD determines whether history is used.
        double sum;
        (void)sum_recent_park_thing_history_values(object->history->dword_history_144[5], sum, 144);
        const auto total = static_cast<std::int64_t>(sum);
        used = numeric::word(static_cast<std::uint32_t>(total));
    }
    const auto years = static_cast<std::uint32_t>(age / 315360000000000LL);
    if (years) {
        const auto& row = upgrade(c, object);
        if (years == 1) {
            return numeric::word(row.scrap_value_year2);
        }
        if (years == 2) {
            return numeric::word(row.scrap_value_year3);
        }
        return numeric::word(row.scrap_value_year4);
    }
    if (static_cast<std::uint32_t>(age / 25920000000000LL) || used) {
        return numeric::word(upgrade(c, object).scrap_value_year1);
    }
    if (numeric::word(upgrade(c, object, true).scrap_value_year1) > 0) {
        return 100;
    }
    return numeric::word(upgrade(c, object).scrap_value_year1);
}
} //namespace simcoaster
