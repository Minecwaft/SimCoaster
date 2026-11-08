#include "world_object_ui_internal.hpp"
#include "world_ui_object_columns.hpp"

namespace simcoaster {
using namespace object_ui_detail;

namespace {
Filter category_filter(ObjectUiCategory category) {
    match (category) {
        case ObjectUiCategory::rides      => return Filter::rides;
        case ObjectUiCategory::shops      => return Filter::shops;
        case ObjectUiCategory::sideshows  => return Filter::sideshows;
        case ObjectUiCategory::features   => return Filter::features;
        case ObjectUiCategory::facilities => return Filter::facilities;
        case _                            => throw std::out_of_range("Park-object list category");
    }
}

std::int32_t stored_value_for_money(const ObjectDefinitionBalance& b, const ParkObjectRuntimeView& record) {
    const auto price = static_cast<float>(fixed::word(record.price_per_use));
    if (b.Info.WhichUIType == 1) {
        const auto type = b.UsageInfo.ShopType;
        const auto quality = static_cast<float>(fixed::word(record.quality_of_goods));
        if (type == 1 || type == 5 || type == 6 || type == 7 || type == 8) {
            return value_for_money(price, quality, 9999.f, 9999.f);
        }
        if (type >= 2 && type <= 4) {
            return value_for_money(price, quality, static_cast<float>(fixed::word(record.portion_size)), static_cast<float>(fixed::word(record.amount_of_special_ingredient)));
        }
    } else if (has(record, ParkObjectCapabilities::provides_relief)) {
        return value_for_money(price, 9999.f, 9999.f, 9999.f);
    }
    return 9999;
}

void sort_rows(ParkObjectListUiState& view) {
    const auto signed_key = view.sort_keys.at(static_cast<std::size_t>(view.category));
    if (!signed_key) {
        return;
    }
    const auto column = signed_key < 0 ? -std::int64_t { signed_key } - 1 : std::int64_t { signed_key } - 1;
    if (column > std::numeric_limits<std::int32_t>::max()) {
        throw std::out_of_range("Park-object list signed sort key");
    }
    const auto compare = [&](const auto& a, const auto& b) {
        //The name column precedes category validation in the original model.
        if (!column) {
            return signed_key < 0 ? a.name > b.name : a.name < b.name;
        }
        return with_object_list_columns<std::out_of_range>(
            view.category,
            [&](auto columns) {
                return columns.precedes(a, b, static_cast<std::size_t>(column), signed_key < 0, "Park-object list sort column");
            },
            "Park-object list sort column");
    };
    //Native855B60 keeps a related ride in its parent's child chain. Sort
    //siblings independently instead of interleaving children among roots.
    std::vector<ParkObjectListUiRow> roots, children;
    for (auto& entry : view.rows) {
        (entry.parent_object ? children : roots).push_back(std::move(entry));
    }
    std::ranges::stable_sort(roots, compare);
    std::ranges::stable_sort(children, compare);
    view.rows.clear();
    for (auto& entry : roots) {
        const auto id = entry.object;
        view.rows.push_back(std::move(entry));
        for (auto& child : children) {
            if (child.parent_object == id) {
                view.rows.push_back(std::move(child));
            }
        }
    }
}

std::int32_t graph_intervals(StatisticsGraphRange range) {
    match (range) {
        case StatisticsGraphRange::one_year     => return 12;
        case StatisticsGraphRange::three_years  => return 36;
        case StatisticsGraphRange::twelve_years => return 144;
        case _                                  => {
        }
    } throw std::out_of_range("Park-object statistics range");
}
} //namespace

ParkObjectListUiRow WorldParkObjectUiRuntime::State::row(ParkObjectRuntimeView& record) const {
    const bool related = view.list.category == ObjectUiCategory::rides && record.upgrade_parent;
    auto& stats = related ? object(record.upgrade_parent) : record;
    const auto& b = balance(record);
    ParkObjectListUiRow out;
    out.object = record.base.header.id;
    if (related) {
        out.parent_object = record.upgrade_parent;
    }
    out.definition = record.definition_id;
    out.name = park_object_ui_display_name(record);
    auto& status_record = record.upgrade_parent ? object(record.upgrade_parent) : record;
    out.status = classify_park_object_ui_status(c, status_record);
    out.color = park_object_ui_status_presentation(out.status).color;
    out.users = sum_recent_park_object_uses(stats);
    out.winners = stats.history ? fixed::truncate_integer(sum(stats.history->dword_history_30[4], 30)) : 0;
    if (view.list.category == ObjectUiCategory::rides || view.list.category == ObjectUiCategory::sideshows) {
        out.excitement_meter = meter(get_park_object_excitement(c, stats, false));
    }
    out.satisfaction_meter = meter(calculate_park_object_customer_satisfaction(stats));
    const auto repair = truncate_park_object_condition(stats.repair_state);
    out.repair_meter = view.list.category == ObjectUiCategory::facilities ? repair : meter(repair);
    out.profit = profit(stats);
    const auto value = stored_value_for_money(b, record);
    out.value_meter = value == 9999 ? 0 : meter(value);
    const auto* control = find_world_object_control(world().object_controls, record.definition_id);
    if (!control) {
        throw std::out_of_range("Park-object list control record");
    }
    out.owned_count = fixed::word(control->live_instance_count);
    out.gender = b.Info.GenderSpecific;
    const bool facility = b.Info.WhichUIType == 5;
    out.has_usage = !facility || has(record, ParkObjectCapabilities::provides_relief) || has(record, ParkObjectCapabilities::chills_you_out);
    out.has_finances = out.has_satisfaction = out.has_cleanliness = out.has_value = !facility || has(record, ParkObjectCapabilities::provides_relief);
    return out;
}

void WorldParkObjectUiRuntime::State::refresh_list_details() {
    auto& v = view.list;
    auto* record = live(v.selected);
    v.details_visible = record && v.category != ObjectUiCategory::features && (v.category != ObjectUiCategory::facilities || has(*record, ParkObjectCapabilities::provides_relief));
    if (!record) {
        v.selected = 0;
        return;
    }
    v.monthly_running_cost = running_cost(*record);
    //Native5E6890 list detail differs from the144-month graph summary getter.
    v.profit_to_date = record->history ? fixed::truncate_integer(sum(record->history->dword_history_30[0], 30) - sum(record->history->dword_history_30[1], 30)) : 0;
    const auto& weather = balance(*record).WeatherModifier;
    v.definition_modifiers = { weather.RainPercentage, weather.SnowPercentage, weather.HeatWavePercentage, weather.StormPercentage };
    v.upgrade_level = record->current_upgrade;
}

void WorldParkObjectUiRuntime::State::refresh_list(bool rebuild) {
    auto& v = view.list;
    if (!v.open) {
        return;
    }
    for (const auto category : std::array { ObjectUiCategory::rides, ObjectUiCategory::shops, ObjectUiCategory::sideshows, ObjectUiCategory::features, ObjectUiCategory::facilities }) {
        bool present = !objects(category_filter(category)).empty();
        if (category == ObjectUiCategory::rides) {
            present = present || !objects(Filter::upgrade_targets).empty();
        }
        v.available.at(static_cast<std::size_t>(category)) = present;
    }
    if (rebuild && !v.available.at(static_cast<std::size_t>(v.category))) {
        for (const auto category : std::array { ObjectUiCategory::rides, ObjectUiCategory::shops, ObjectUiCategory::sideshows, ObjectUiCategory::facilities, ObjectUiCategory::features }) {
            if (v.available.at(static_cast<std::size_t>(category))) {
                v.category = category;
                break;
            }
        }
    }
    if (rebuild) {
        v.rows.clear();
        for (auto* record : objects(category_filter(v.category))) {
            v.rows.push_back(row(*record));
        }
        if (v.category == ObjectUiCategory::rides) {
            for (auto* record : objects(Filter::upgrade_targets)) {
                const auto parent = std::ranges::find(v.rows, record->upgrade_parent, &ParkObjectListUiRow::object);
                if (record->upgrade_parent && parent != v.rows.end()) {
                    v.rows.insert(parent + 1, row(*record));
                }
            }
        }
        const auto first_inserted = v.rows.empty() ? ThingId {} : v.rows.front().object;
        sort_rows(v);
        if (!v.selected) {
            v.selected = first_inserted;
        }
    } else {
        //Native periodic row updates do not silently re-sort changing metrics.
        //Preserve text/membership/order, and remove missing identities safely.
        std::erase_if(v.rows, [&](const auto& entry) {
            return !live(entry.object);
        });
        if (v.category != ObjectUiCategory::features) {
            for (auto& entry : v.rows) {
                auto updated = row(object(entry.object));
                updated.name = std::move(entry.name);
                entry = std::move(updated);
            }
        }
    }
    if (!v.selected && !v.rows.empty()) {
        v.selected = v.rows.front().object;
    }
    if (std::ranges::find(v.rows, v.selected, &ParkObjectListUiRow::object) == v.rows.end()) {
        v.selected = 0;
    }
    refresh_list_details();
    ++v.revision;
}

void WorldParkObjectUiRuntime::open_list(ObjectUiCategory category, ThingId selected) {
    auto& s = *state_;
    (void)category_filter(category);
    s.view.list.open = true;
    s.view.list.category = category;
    s.view.list.selected = selected;
    s.refresh_list();
}

void WorldParkObjectUiRuntime::open_list() {
    open_list(state_->view.list.category);
}

void WorldParkObjectUiRuntime::select_list(ThingId id) {
    auto& s = *state_;
    auto& v = s.view.list;
    if (id && std::ranges::find(v.rows, id, &ParkObjectListUiRow::object) == v.rows.end()) {
        throw std::out_of_range("Park-object list selection");
    }
    v.selected = id;
    s.refresh_list_details();
}

void WorldParkObjectUiRuntime::sort_list(std::int32_t key) {
    auto& v = state_->view.list;
    v.sort_keys.at(static_cast<std::size_t>(v.category)) = key;
    sort_rows(v);
    ++v.revision;
}

void WorldParkObjectUiRuntime::refresh_list() {
    state_->refresh_list(false);
}

void WorldParkObjectUiRuntime::open_finance() {
    state_->view.finance.open = true;
    state_->view.finance.enabled.fill(false);
    //Native522FA0 retains the singleton policy, then522C20 resets inputs.
    set_finance_mode(state_->view.finance.mode);
}

void WorldParkObjectUiRuntime::set_finance_mode(ParkObjectFinanceMode mode) {
    if (mode != ParkObjectFinanceMode::amount && mode != ParkObjectFinanceMode::add && mode != ParkObjectFinanceMode::percent) {
        throw std::out_of_range("Park-object finance policy");
    }
    state_->view.finance.mode = mode;
    state_->view.finance.values.fill(0);
}

void WorldParkObjectUiRuntime::apply_finance() {
    auto& s = *state_;
    auto& v = s.view.finance;
    const auto adjust = [&](std::int32_t previous, std::int32_t change) {
        std::int32_t value;
        match (v.mode) {
            case ParkObjectFinanceMode::amount  => value = change;
            case ParkObjectFinanceMode::add     => value = fixed::add(previous, change);
            case ParkObjectFinanceMode::percent => {
                value = fixed::add(previous, fixed::word(static_cast<std::uint32_t>(previous) * static_cast<std::uint32_t>(change)) / 100);
            }
            case _ => throw std::out_of_range("Park-object finance policy");
        } return std::clamp(value, 0, 10000);
    };
    if (v.enabled[0]) {
        set_world_admission_fee(&s.bank(), adjust(s.bank().admission_fee, v.values[0]));
    }
    constexpr std::array filters { Filter::photo, Filter::rides, Filter::shops, Filter::sideshows, Filter::relief };
    for (std::size_t i = 0; i < filters.size(); ++i) {
        if (v.enabled[i + 1]) {
            for (auto* record : s.objects(filters[i])) {
                record->price_per_use = static_cast<std::uint32_t>(adjust(fixed::word(record->price_per_use), v.values[i + 1]));
            }
        }
    }
}

void WorldParkObjectUiRuntime::State::refresh_statistics() {
    auto& v = view.statistics;
    if (!v.open) {
        return;
    }
    auto& record = object(v.object);
    if (&record.base.header != statistics_identity) {
        throw std::out_of_range("Park-object statistics identity was replaced");
    }
    v.name = park_object_ui_display_name(record);
    v.running_cost = running_cost(record);
    v.takings = record.history ? latest(record.history->dword_history_144[0]) : 0;
    v.goods_cost = record.history ? latest(record.history->dword_history_144[1]) : 0;
    v.profit = profit(record);
    v.total_profit = total_profit(record);
    const auto category = definition(record).which_ui_type;
    v.show_goods_cost = category == ObjectUiCategory::shops;
    v.show_prizes_cost = category == ObjectUiCategory::sideshows;
    auto& graph = v.graph;
    const auto count = graph_intervals(graph.range);
    graph.lower = 0;
    graph.upper = 100;
    graph.divisions = 10;
    graph.series.clear();
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 4> bindings { { { 2, 0 }, { 1, 2 }, { 4, 4 }, { 8, 5 } } }; //524690 draw order.
    if (record.history) {
        for (const auto [mask, index] : bindings) {
            if (graph.selected_series_mask & mask) {
                const auto& history = record.history->dword_history_144[index];
                for (int offset = 0; offset < history.num_entries && offset <= count; ++offset) {
                    const auto value = sample(history, offset);
                    if (!value) {
                        break;
                    }
                    if (index == 4) {
                        graph.lower = std::min(graph.lower, fixed::word(*value));
                        graph.upper = std::max(graph.upper, fixed::word(*value));
                    } else {
                        graph.lower = fixed::word(std::min(static_cast<std::uint32_t>(graph.lower), *value));
                        graph.upper = fixed::word(std::max(static_cast<std::uint32_t>(graph.upper), *value));
                    }
                }
                auto& output = graph.series.emplace_back(index);
                const auto available = history.wrapped ? history.num_entries : fixed::add(history.current_entry, 1);
                const auto rendered = std::min(count, available);
                std::int32_t previous {};
                for (auto offset = rendered; offset >= 0; --offset) {
                    if (const auto value = sample(history, offset)) {
                        previous = fixed::word(*value);
                    }
                    output.samples.push_back(previous);
                }
            }
        }
    }
    ++graph.revision;
    ++v.revision;
}

void WorldParkObjectUiRuntime::open_statistics(ThingId id) {
    auto& s = *state_;
    auto& v = s.view.statistics;
    s.statistics_identity = &s.object(id).base.header;
    v.open = v.graph.open = true;
    v.object = id;
    s.refresh_statistics();
}

void WorldParkObjectUiRuntime::set_statistics_range(StatisticsGraphRange range) {
    (void)graph_intervals(range);
    state_->view.statistics.graph.range = range;
    state_->refresh_statistics();
}

void WorldParkObjectUiRuntime::toggle_statistics_series(std::uint32_t selection) {
    auto& mask = state_->view.statistics.graph.selected_series_mask;
    if (!selection) {
        mask = 0;
    } else if (selection == 1 || selection == 2 || selection == 4) {
        mask = (mask ^ selection) & ~8u;
    } else if (selection == 8) {
        mask = (mask ^ 8u) & ~7u;
    }
    state_->refresh_statistics();
}

void WorldParkObjectUiRuntime::refresh_statistics() {
    state_->refresh_statistics();
}

void WorldParkObjectUiRuntime::State::refresh_opinions() {
    auto& v = view.opinions;
    if (!v.open) {
        return;
    }
    auto& record = object(v.object);
    if (&record.base.header != opinions_identity) {
        throw std::out_of_range("Park-object opinions identity was replaced");
    }
    v.name = park_object_ui_display_name(record);
    FileTime now {};
    calculate_world_calendar_filetime(c, &world().calendar, &now);
    const auto difference = std::bit_cast<std::int64_t>(filetime_bits(now) - record.last_survey_reset_file_time);
    v.days = std::min(30, fixed::narrow(difference / 864000000000LL));
    v.days_caption_row = v.days == 1 ? 431u : v.days <= 1 ? 432u : 430u;
    const auto& b = balance(record);
    v.show_second_queue = b.Info.WhichUIType == 0;
    v.queue_caption_rows = { b.Info.WhichUIType == 0 ? 445u : b.Info.WhichUIType == 2 ? 443u : b.UsageInfo.SpecialIngredient ? 441u : 442u, 444u };
    v.satisfaction.fill(0);
    v.price.fill(0);
    v.queue.fill(0);
    v.users = v.walkaways = 0;
    if (record.history) {
        const auto count = [&](std::size_t series) {
            return fixed::truncate_integer(sum(record.history->dword_history_30.at(series), v.days));
        };
        v.users = count(2);
        v.walkaways = count(3);
        const auto percentage = [](std::int32_t value, std::int32_t total) {
            return quotient(fixed::word(static_cast<std::uint32_t>(value) * 100u), total);
        };
        if (v.days && v.users > 0) {
            v.satisfaction[0] = percentage(count(5), v.users);
            v.satisfaction[1] = percentage(count(6), v.users);
            v.satisfaction[3] = percentage(count(7), v.users);
            v.satisfaction[4] = percentage(count(8), v.users);
            v.satisfaction[2] = fixed::subtract(fixed::subtract(fixed::subtract(fixed::subtract(100, v.satisfaction[4]), v.satisfaction[3]), v.satisfaction[1]), v.satisfaction[0]);
        }
        const auto total = fixed::add(v.users, v.walkaways);
        if (v.days && total > 0) {
            const auto expensive = count(12);
            auto cheap = count(11);
            if (fixed::add(cheap, expensive) > total) {
                cheap = fixed::subtract(total, expensive);
            }
            v.price[0] = percentage(cheap, total);
            v.price[2] = percentage(expensive, total);
            v.price[1] = fixed::subtract(fixed::subtract(100, v.price[2]), v.price[0]);
            v.queue[0] = percentage(count(9), total);
            v.queue[1] = percentage(count(10), total);
        }
    }
    ++v.revision;
}

void WorldParkObjectUiRuntime::open_opinions(ThingId id) {
    auto& s = *state_;
    auto& record = s.object(id);
    const auto category = s.definition(record).which_ui_type;
    if (category != ObjectUiCategory::rides && category != ObjectUiCategory::shops && category != ObjectUiCategory::sideshows) {
        throw std::invalid_argument("Opinion summary requires a ride, shop or sideshow");
    }
    s.opinions_identity = &record.base.header;
    s.view.opinions.open = true;
    s.view.opinions.object = id;
    s.refresh_opinions();
}

void WorldParkObjectUiRuntime::reset_opinions() {
    auto& s = *state_;
    auto& record = s.object(s.view.opinions.object);
    FileTime now {};
    calculate_world_calendar_filetime(s.c, &s.world().calendar, &now);
    //Native15041 advances the survey window; it never clears history rings.
    record.last_survey_reset_file_time = filetime_bits(now);
    s.refresh_opinions();
}

void WorldParkObjectUiRuntime::refresh_opinions() {
    state_->refresh_opinions();
}
} //namespace simcoaster
