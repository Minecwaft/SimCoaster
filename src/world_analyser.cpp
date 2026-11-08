#include "simcoaster/world_analyser.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_objects.hpp"
#include <stdexcept>

namespace simcoaster {
int world_operations::builtin::cached_park_value(WorldRuntime& c, ParkAnalyserFinanceView* analyser, std::uint32_t age) {
    if (!analyser || !c.simulation.current) {
        throw std::logic_error("weighted park-value receiver");
    }
    auto& cache = analyser->metrics[4];
    if (cache.cache_valid && age >= (c.simulation.current->saved.game_tick >> 2) - (cache.last_calculation_tick >> 2)) {
        return fixed::word(cache.cached_value_bits);
    }
    std::uint32_t total = 0;
    for (std::size_t x = 128; x; --x) {
        for (std::size_t y = 128; y; --y) {
            const auto& cell = c.simulation.current->map_cells[(y - 1) * 128 + x - 1];
            if (cell.base.common.flags & 0x40u) {
                continue;
            }
            const auto type = cell.base.common.type;
            total += type == 2;
            total += type == 1 ? 3u : 0u;
            total += type == 3 || type == 9 ? 5u : 0u;
            total += type == 9;
            total += type == 10;
            total += type == 0 ? 2u : 0u;
            total += world_track_reference_matches_type_set(c, cell.base.index) ? 50u : 0u;
        }
    }
    for (auto id = c.simulation.current->saved.first_park_object; id;) {
        auto* header = c.entities.at(id).payload;
        if (!header) {
            throw std::logic_error("weighted park-value object");
        }
        const auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(header);
        if (object.state != ParkObjectState::condemned) {
            std::uint32_t cost = 0;
            for (std::size_t upgrade = 0; upgrade <= object.current_upgrade; ++upgrade) {
                const auto* definition = get_park_object_definition(c, &object);
                if (!definition) {
                    throw std::logic_error("weighted park-value definition");
                }
                cost += definition->research.upgrades.at(upgrade).cost_of_upgrade;
            }
            const auto repair = static_cast<std::uint8_t>(fixed::truncate_integer(object.repair_state));
            total += static_cast<std::uint32_t>(fixed::word(cost * repair) / 100);
        }
        id = object.next_park_object;
    }
    cache = { c.simulation.current->saved.game_tick, total, 1 };
    return fixed::word(total);
}

void append_park_analyser_byte_history(ParkAnalyserState& analyser, std::size_t series, std::uint8_t sample) {
    auto& history = analyser.byte_histories.at(series);
    history.current_entry = fixed::add(history.current_entry, 1);
    if (history.current_entry >= history.num_entries) {
        history.current_entry = 0;
        history.wrapped = 1;
    }
    if (history.current_entry < 0) {
        throw std::out_of_range("negative analyser byte-history index");
    }
    history.data.at(static_cast<std::size_t>(history.current_entry)) = sample;
}

void record_visitor_admission_in_park_analyser(EconomyWorld& c, ParkAnalyserState& analyser, std::int32_t fee) {
    const auto previous = std::bit_cast<std::int32_t>(analyser.most_paid_for_ticket);
    ++analyser.lifetime_visitors;
    ++analyser.visitors_this_month;
    if (fee > previous) {
        analyser.most_paid_for_ticket = static_cast<std::uint32_t>(fee);
    }
    WorldEvent event { ChallengeProgressEvent { ChallengeType::visitor_admissions, 1, 0, 0 } };
    dispatch_world_event_to_subscribers(c, get_world_event_subscription_registry(c), &event, deliver_world_record_event, nullptr);
}

void world_operations::builtin::record_visitor_admission(WorldRuntime& c, ParkAnalyserFinanceView* analyser, std::int32_t fee) {
    if (!analyser) {
        throw std::invalid_argument("null visitor-admission analyser");
    }
    record_visitor_admission_in_park_analyser(c, *analyser, fee);
}

AnalyserGridRecord& get_park_analyser_grid_record(ParkAnalyserState& analyser, CellId cell) {
    const auto divisor = std::bit_cast<std::int32_t>(analyser.map_squares_per_cell);
    if (!divisor) {
        throw std::domain_error("analyser cell scale is zero");
    }
    const auto index = static_cast<std::uint32_t>(cell) - 1u;
    const auto x = static_cast<std::int32_t>(index & 127u) / divisor, y = static_cast<std::int32_t>(index >> 7) / divisor;
    const auto row = static_cast<std::uint32_t>(y) * analyser.grid_size_x + static_cast<std::uint32_t>(x);
    return analyser.grid.at(row);
}

namespace {
template <AnalyserCellMetric Metric>
std::int32_t count_cells(EconomyWorld& c, AnalyserGridRecord& grid, std::uint32_t age) {
    if (!c.simulation.current) {
        throw std::logic_error("base World is required for analyser timing");
    }
    auto& cache = grid.metrics[static_cast<std::size_t>(Metric)];
    if (cache.cache_valid && age >= (c.simulation.current->saved.game_tick >> 2) - (cache.last_calculation_tick >> 2)) {
        return std::bit_cast<std::int32_t>(cache.cached_value_bits);
    }
    std::uint32_t count = 0;
    const auto end_x = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(grid.start_x) + static_cast<std::uint32_t>(grid.size_x));
    const auto end_y = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(grid.start_y) + static_cast<std::uint32_t>(grid.size_y));
    for (auto x = grid.start_x; x < end_x; ++x) {
        for (auto y = grid.start_y; y < end_y; ++y) {
            const auto id = static_cast<CellId>((static_cast<std::uint32_t>(y) << 7) + static_cast<std::uint32_t>(x) + 1);
            const auto& cell = *get_world_map_cell_record_by_id(c, id);
            if constexpr (Metric == AnalyserCellMetric::litter_cells) {
                count += cell.litter_selector != 0;
            } else {
                const auto type = cell.base.common.type;
                if constexpr (Metric == AnalyserCellMetric::developed_cells) {
                    count += type == 1 || type == 3 || type == 9 || type == 10 || type == 4;
                } else {
                    count += type == 1 || type == 3 || type == 9;
                }
            }
        }
    }
    cache.cached_value_bits = count;
    cache.cache_valid = 1;
    cache.last_calculation_tick = c.simulation.current->saved.game_tick;
    return std::bit_cast<std::int32_t>(cache.cached_value_bits);
}
} //namespace

std::int32_t count_cached_analyser_litter_cells(EconomyWorld& c, AnalyserGridRecord& grid, std::uint32_t age) {
    return count_cells<AnalyserCellMetric::litter_cells>(c, grid, age);
}

std::int32_t count_cached_analyser_walkable_cells(EconomyWorld& c, AnalyserGridRecord& grid, std::uint32_t age) {
    return count_cells<AnalyserCellMetric::walkable_cells>(c, grid, age);
}

std::int32_t count_cached_analyser_developed_cells(EconomyWorld& c, AnalyserGridRecord& grid, std::uint32_t age) {
    return count_cells<AnalyserCellMetric::developed_cells>(c, grid, age);
}

std::int32_t count_park_developed_cells(EconomyWorld& c, ParkAnalyserState& analyser, std::uint32_t age) {
    std::uint32_t count = 0;
    for (std::size_t i = analyser.grid.size(); i; --i) {
        count += static_cast<std::uint32_t>(count_cached_analyser_developed_cells(c, analyser.grid[i - 1], age));
    }
    return fixed::word(count);
}

namespace {
std::array<CellId, 2> patrol_endpoints(const StaffRuntimeRecord& s) {
    const auto& b = s.area.bounds;
    return { static_cast<CellId>(static_cast<std::uint32_t>(b.start_y) * 128u + static_cast<std::uint32_t>(b.start_x) + 1u), static_cast<CellId>(static_cast<std::uint32_t>(b.end_y) * 128u + static_cast<std::uint32_t>(b.end_x) + 1u) };
}
} //namespace

void mark_world_staff_patrol_coverage(EconomyWorld& c, ThingKind role) {
    if (!c.simulation.current) {
        throw std::logic_error("staff coverage requires base World");
    }
    for (auto& cell : c.simulation.current->map_cells) {
        cell.staff_patrol_coverage = 0;
    }
    for (auto* node = c.entities.active_head; node; node = node->active_next) {
        if (node->payload->kind != role || !is_staff_record_kind(role)) {
            continue;
        }
        const auto& staff = *reinterpret_cast<const StaffRuntimeRecord*>(node->payload);
        const auto endpoints = patrol_endpoints(staff);
        if (endpoints[0] == 1 && endpoints[1] == map_cell_count) {
            continue;
        }
        const auto first = static_cast<std::uint32_t>(endpoints[0]) - 1u, last = static_cast<std::uint32_t>(endpoints[1]) - 1u;
        //Packed WORD corners, inclusive traversal, no corner normalization.
        for (auto x = first & 127u; x <= (last & 127u); ++x) {
            for (auto y = first >> 7; y <= (last >> 7); ++y) {
                c.simulation.current->map_cells.at(y * 128u + x).staff_patrol_coverage = 1;
            }
        }
    }
}

std::int32_t calculate_world_staff_patrol_coverage(EconomyWorld& c, ThingKind role, StaffCoverageCellFilter filter) {
    mark_world_staff_patrol_coverage(c, role);
    std::int32_t total = 0, covered = 0;
    for (const auto& cell : c.simulation.current->map_cells) {
        const auto type = cell.base.common.type;
        const bool path = type == 1 || type == 3 || type == 9;
        bool accept = false;
        match (filter) {
            case StaffCoverageCellFilter::land      => accept = type != 2 && type != 7 && type != 30;
            case StaffCoverageCellFilter::developed => {
                accept = path || type == 10 || type == 4 || world_track_reference_matches_type_set(c, cell.base.index) || type == 21;
            }
            case StaffCoverageCellFilter::object_entries   => accept = type == 9;
            case StaffCoverageCellFilter::paths_and_queues => accept = path;
            case _                                         => {
            }
        } if (accept) {
            ++total;
            covered += cell.staff_patrol_coverage > 0;
        }
    }
    return total ? 100 * covered / total : 0;
}
} //namespace simcoaster
