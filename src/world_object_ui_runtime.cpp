#include "simcoaster/require_bound.hpp"
#include "coaster_editor_internal.hpp"
#include "world_object_ui_internal.hpp"

namespace simcoaster {
using namespace object_ui_detail;

World& WorldParkObjectUiRuntime::State::world() const {
    if (!c.referenced_world()) {
        throw std::logic_error("Park-object UI primary World is unavailable");
    }
    return *c.referenced_world();
}

ParkObjectRuntimeView* WorldParkObjectUiRuntime::State::live(ThingId id) const {
    if (!id || id == retiring || id >= c.entities.size()) {
        return nullptr;
    }
    auto* record = c.entities[id].payload;
    return record && record->id == id && !record->retirement_pending && record->kind == ThingKind::park_object ? reinterpret_cast<ParkObjectRuntimeView*>(record) : nullptr;
}

ParkObjectRuntimeView& WorldParkObjectUiRuntime::State::object(ThingId id) const {
    return require_bound<std::out_of_range>(live(id), "Park-object UI target is no longer live");
}

ResearchCatalogDefinition& WorldParkObjectUiRuntime::State::definition(const ParkObjectRuntimeView& record) const {
    auto* result = get_park_object_definition(c, &record);
    if (!result || !result->balance) {
        throw std::out_of_range("Park-object UI definition is unavailable");
    }
    return *result;
}

const ObjectDefinitionBalance& WorldParkObjectUiRuntime::State::balance(const ParkObjectRuntimeView& record) const {
    return *definition(record).balance;
}

BankRecord& WorldParkObjectUiRuntime::State::bank() const {
    auto* record = c.entities.at(world().saved.bank_account).payload;
    if (!record || record->kind != ThingKind::bank_account) {
        throw std::logic_error("Park-object UI bank is unavailable");
    }
    return *reinterpret_cast<BankRecord*>(record);
}

std::u16string WorldParkObjectUiRuntime::State::text(std::span<const std::byte> table, std::uint32_t row, std::span<const BfstSignedIntegerArgument> integers) const {
    auto result = format_bfst_wide_string(table, c.messages.mail_language.conversion, row, 16383, {}, integers);
    if (!result) {
        throw std::runtime_error("Park-object UI localized row " + std::to_string(row) + ": " + result.error().reason);
    }
    return std::move(*result);
}

std::u16string WorldParkObjectUiRuntime::State::text(std::uint32_t row, std::span<const BfstSignedIntegerArgument> integers) const {
    return text(c.messages.mail_language.ui_text, row, integers);
}

bool WorldParkObjectUiRuntime::State::matches(const ParkObjectRuntimeView& record, Filter filter) const {
    const auto& d = definition(record);
    const auto& b = *d.balance;
    const bool base = static_cast<std::uint16_t>(d.add_on.upgrades_id) == 0;
    match (filter) {
        case Filter::rides => {
            return base && d.which_ui_type == ObjectUiCategory::rides && (d.is_special != 1 || is_special_ride_group_id_unregistered(c, record.base.header.id));
        }
        case Filter::special => {
            return base && d.which_ui_type == ObjectUiCategory::rides && d.is_special == 1 && !is_special_ride_group_id_unregistered(c, record.base.header.id);
        }
        case Filter::shops => {
            return base && d.which_ui_type == ObjectUiCategory::shops && !b.Info.IsPhotoBooth && !b.UsageInfo.IsInfoDesk;
        }
        case Filter::sideshows  => return base && d.which_ui_type == ObjectUiCategory::sideshows;
        case Filter::features   => return base && d.which_ui_type == ObjectUiCategory::features;
        case Filter::facilities => return base && d.which_ui_type == ObjectUiCategory::facilities;
        case Filter::relief     => {
            return base && d.which_ui_type == ObjectUiCategory::facilities && has(record, ParkObjectCapabilities::provides_relief);
        }
        case Filter::photo  => return base && d.which_ui_type == ObjectUiCategory::shops && b.Info.IsPhotoBooth;
        case Filter::info   => return d.which_ui_type == ObjectUiCategory::facilities && b.UsageInfo.IsInfoDesk;
        case Filter::litter => {
            return base && d.which_ui_type == ObjectUiCategory::facilities && has(record, ParkObjectCapabilities::holds_litter);
        }
        case Filter::security => {
            //Native4F9E10 deliberately omits the upgrade-target exclusion.
            return d.which_ui_type == ObjectUiCategory::facilities && has(record, ParkObjectCapabilities::provides_security);
        }
        case Filter::chill => {
            return base && d.which_ui_type == ObjectUiCategory::facilities && has(record, ParkObjectCapabilities::chills_you_out);
        }
        case Filter::addons          => return !base && d.add_on.upgrade_type == 2;
        case Filter::upgrade_targets => return !base;
        case _                       => {
        }
    } std::unreachable();
}

std::vector<ParkObjectRuntimeView*> WorldParkObjectUiRuntime::State::objects(Filter filter) const {
    std::vector<ParkObjectRuntimeView*> result;
    for (auto* entry = c.entities.active_head; entry; entry = entry->active_next) {
        if (auto* record = live(static_cast<ThingId>(entry->id_storage)); record && matches(*record, filter)) {
            result.push_back(record);
        }
    }
    return result;
}

ParkObjectUiRoute WorldParkObjectUiRuntime::State::route(ThingId id) const {
    const auto& record = object(id);
    const auto& d = definition(record);
    const auto& b = *d.balance;
    if (c.mode.game == GameMode::code_3) {
        return { ParkObjectUiPanel::mode3, id };
    }
    match (d.which_ui_type) {
        case ObjectUiCategory::rides => {
            if (b.Info.IsPhotoBooth) {
                return { ParkObjectUiPanel::shop, id };
            }
            if (d.is_special == 1 && !is_special_ride_group_id_unregistered(c, id)) {
                return { ParkObjectUiPanel::special_ride, id };
            }
            if (d.add_on.upgrade_type == 2) {
                return { ParkObjectUiPanel::secondary_ride, id };
            }
            if (record.upgrade_parent) {
                return { ParkObjectUiPanel::ride, object(record.upgrade_parent).base.header.id };
            }
            if (!static_cast<std::uint16_t>(d.add_on.upgrades_id)) {
                return { ParkObjectUiPanel::ride, id };
            }
            return {};
        }
        case ObjectUiCategory::shops => {
            return { b.Info.IsPhotoBooth ? ParkObjectUiPanel::photo_kiosk : ParkObjectUiPanel::shop, id };
        }
        case ObjectUiCategory::sideshows                                => return { ParkObjectUiPanel::sideshow, id };
        case ObjectUiCategory::features or ObjectUiCategory::facilities => {
            if (b.UsageInfo.IsInfoDesk) {
                return { ParkObjectUiPanel::info_desk, id };
            }
            if (has(record, ParkObjectCapabilities::provides_relief)) {
                return { ParkObjectUiPanel::bathroom, id };
            }
            if (has(record, ParkObjectCapabilities::chills_you_out)) {
                return { ParkObjectUiPanel::staff_room, id };
            }
            if (has(record, ParkObjectCapabilities::thinking_space)) {
                return { ParkObjectUiPanel::research, id };
            }
            if (has(record, ParkObjectCapabilities::training_room)) {
                return { ParkObjectUiPanel::training_room, id };
            }
            if (has(record, ParkObjectCapabilities::holds_litter)) {
                return { ParkObjectUiPanel::litter_bin, id };
            }
            return { ParkObjectUiPanel::feature, id };
        }
        case _ => return {};
    }
}

std::int32_t object_ui_detail::value_for_money(float price, float quality, float portion, float ingredient) {
    if (price <= 0) {
        return 100;
    }
    double value;
    //Native5E6B30's9999 sentinels have deliberately asymmetric branches.
    if (ingredient != 9999.f && portion != 9999.f && quality != 9999.f) {
        value = static_cast<double>(quality) / price * (portion * .25) * (ingredient * .25) * .07;
    } else if (portion != 9999.f) {
        value = static_cast<double>(quality) / price * (portion * .5) + static_cast<double>(quality) / price * (portion * .5);
    } else if (quality == 9999.f) {
        value = 100. - (price + price);
    } else {
        value = static_cast<double>(quality) / price * 25.;
    }
    return std::clamp(fixed::truncate_integer(value), 0, 100);
}

ParkObjectUiMetrics WorldParkObjectUiRuntime::State::metrics(ParkObjectRuntimeView& record, const ParkObjectUiStagedSettings& staged) const {
    const auto& b = balance(record);
    ParkObjectUiMetrics out;
    out.users = sum_recent_park_object_uses(record);
    out.walkaways = sum_recent_park_object_walkaways(record);
    out.winners = record.history ? fixed::truncate_integer(sum(record.history->dword_history_30[4], 30)) : 0;
    out.monthly_running_cost = running_cost(record);
    out.satisfaction_meter = meter(calculate_park_object_customer_satisfaction(record));
    out.repair_meter = meter(truncate_park_object_condition(record.repair_state));
    out.capacity = record.operating_capacity;
    out.gender = b.Info.GenderSpecific;
    const auto* control = find_world_object_control(world().object_controls, record.definition_id);
    if (!control) {
        throw std::out_of_range("Park-object UI control record is unavailable");
    }
    out.owned_count = control->live_instance_count;
    out.scrap_value = definition(record).research.upgrades[0].cost_of_upgrade * static_cast<std::uint32_t>(get_park_object_scrap_value_percent(c, &record)) / 100u;
    const auto kind = route(record.base.header.id).panel;
    if (kind == ParkObjectUiPanel::ride) {
        out.excitement_meter = meter(calculate_park_object_excitement_rating(c, record, staged.speed, staged.duration, staged.capacity));
        const auto decay = calculate_park_object_condition_decay(c, record, static_cast<float>(staged.speed), static_cast<float>(staged.capacity), static_cast<float>(staged.duration), true);
        out.condition_meter = meter(static_cast<std::uint8_t>(100u - static_cast<std::uint32_t>(fixed::truncate_integer(decay * 9.))));
        const auto working_life = b.Info.WorkingLife ? b.Info.WorkingLife : 100;
        out.life_meter = quotient(static_cast<std::int32_t>(truncate_park_object_condition(record.working_life)) << 10, working_life);
    } else if (kind == ParkObjectUiPanel::sideshow) {
        out.excitement_meter = meter(calculate_park_object_excitement_rating(c, record, staged.goods_cost, staged.price, staged.chance));
    } else if (kind == ParkObjectUiPanel::shop || kind == ParkObjectUiPanel::photo_kiosk) {
        const auto type = b.UsageInfo.ShopType;
        const bool fixed_portion = type == 1 || type == 5 || type == 6 || type == 7 || type == 8;
        const bool photo = kind == ParkObjectUiPanel::photo_kiosk;
        out.goods_cost = calculate_shop_goods_cost(c, record, static_cast<float>(staged.quality), photo ? 0.f : static_cast<float>(staged.ingredient), fixed_portion || photo ? -1.f : static_cast<float>(staged.portion));
        out.value_meter = meter(value_for_money(static_cast<float>(staged.price), static_cast<float>(staged.quality), fixed_portion || photo ? 9999.f : static_cast<float>(staged.portion), fixed_portion || photo || !b.UsageInfo.SpecialIngredient ? 9999.f : static_cast<float>(staged.ingredient)));
    } else if (kind == ParkObjectUiPanel::litter_bin) {
        auto* script = world_operations::find_script(c, record.script_id);
        const auto contents = world_operations::script_slot(c, script, 4);
        out.contents_meter = quotient(fixed::word(static_cast<std::uint32_t>(contents) << 10), c.attractions.park_object_creation_balance.LitterBin.Capacity);
    } else if (kind == ParkObjectUiPanel::mode3) {
        out.excitement_meter = meter(get_park_object_excitement(c, record, false));
    }
    return out;
}

void WorldParkObjectUiRuntime::State::load_settings(ParkObjectRuntimeView& record) {
    auto& v = view.settings;
    const auto& b = balance(record);
    v.definition_modifiers = { b.WeatherModifier.RainPercentage, b.WeatherModifier.SnowPercentage, b.WeatherModifier.HeatWavePercentage, b.WeatherModifier.StormPercentage };
    v.ingredient_caption = b.UsageInfo.SpecialIngredient >= 1 && b.UsageInfo.SpecialIngredient <= 4 ? text(c.messages.mail_language.ingredients, static_cast<std::uint32_t>(b.UsageInfo.SpecialIngredient - 1)) : std::u16string {};
    v.related_ride_name = record.upgrade_parent ? park_object_ui_display_name(object(record.upgrade_parent)) : std::u16string {};
    v.staged = { fixed::word(record.operating_speed), record.operating_capacity, record.operating_duration, fixed::word(record.price_per_use), static_cast<std::uint8_t>(record.quality_of_goods), static_cast<std::uint8_t>(record.amount_of_special_ingredient), fixed::word(record.portion_size), static_cast<std::uint8_t>(record.chance_of_winning), fixed::word(record.cost_of_goods), record.promoted_attraction };
    v.duration_unit = static_cast<ParkObjectDurationUnit>(b.Info.DurationUnit);
    v.capacity_caption_row = b.Bumper.WhichTrackType == 3 ? 970u : b.Bumper.WhichTrackType == 2 ? 969u : 936u;
    v.build_cost = b.Upgrades[0].CostOfUpgrade;
    v.build_cost_valid = false;
    if (v.panel == ParkObjectUiPanel::mode3) {
        const auto* opening = world_operations::find_mesh_runtime_opening_state(c, record.mesh_instance_id);
        v.build_cost_valid = opening && (opening->placement_flags & 1u) && !(opening->placement_flags & 2u);
        if (!c.resources.meshes) {
            throw std::logic_error("Mode3 cost requires the mesh runtime");
        }
        const auto* coasters = c.resources.meshes->coasters();
        const auto* binding = coasters ? coasters->find(record.mesh_instance_id) : nullptr;
        if (binding) {
            const auto ride_index = static_cast<std::size_t>(binding - coasters->bindings.data());
            auto cost = static_cast<std::uint32_t>(v.build_cost);
            //Native48E670 does not export a COS: incomplete/large layouts also
            //have a cost. Reuse the editor's pure evaluator without imposing the
            //exporter's40x40 serialization bounds.
            for (std::size_t i = 0; i < binding->geometry.size(); ++i) {
                const auto& node = binding->geometry_state.at(i);
                if (coasters->placements.at(node.placement).flags & 16u) {
                    continue;
                }
                cost += static_cast<std::uint32_t>(fixed::truncate_integer(coaster_editor_detail::piece_cost(*coasters, ride_index, i)));
            }
            v.build_cost = fixed::word(cost);
        }
    }
    v.speed = { b.UsageInfo.MinSpeed, b.UsageInfo.MaxSpeed };
    v.capacity = { b.UsageInfo.MinCapacity, b.UsageInfo.MaxCapacity, 1, b.UsageInfo.MinCapacity != b.UsageInfo.MaxCapacity };
    v.duration = { b.UsageInfo.MinDuration, b.UsageInfo.MaxDuration, 1, b.Info.DurationUnit != 0 };
    v.price = { 0, 10000 };
    v.quality = v.ingredient = v.portion = v.chance = { 0, 100 };
    v.goods_cost = { 0, 10000 };
    const bool fixed_portion = b.UsageInfo.ShopType == 1 || b.UsageInfo.ShopType == 5 || b.UsageInfo.ShopType == 6 || b.UsageInfo.ShopType == 7 || b.UsageInfo.ShopType == 8;
    v.portion.visible = !fixed_portion;
    v.ingredient.visible = !fixed_portion && b.UsageInfo.SpecialIngredient != 0;
    v.chance.visible = v.goods_cost.visible = !b.UsageInfo.NonWinnableSideshow;
    v.chance.enabled = v.goods_cost.enabled = !b.UsageInfo.NonWinnableSideshow;
    v.upgrade_level = record.current_upgrade;
    const auto& upgrade = b.Upgrades.at(record.current_upgrade);
    const auto speed_span = fixed::subtract(b.UsageInfo.MaxSpeed, b.UsageInfo.MinSpeed);
    const auto capacity_span = fixed::subtract(b.UsageInfo.MaxCapacity, b.UsageInfo.MinCapacity);
    v.speed_redline_meter = speed_span ? quotient(fixed::word(static_cast<std::uint32_t>(upgrade.RedLineSpeed) << 10), speed_span) : 0;
    v.capacity_redline_meter = capacity_span ? quotient(fixed::subtract(fixed::word(static_cast<std::uint32_t>(upgrade.RedLineCapacity) << 10), 1024), capacity_span) : 0;
    ++v.input_revision;
}

void WorldParkObjectUiRuntime::State::rebuild_navigation(ParkObjectRuntimeView& record) {
    navigation.clear();
    Filter filter;
    match (definition(record).which_ui_type) {
        case ObjectUiCategory::rides     => filter = Filter::rides;
        case ObjectUiCategory::shops     => filter = Filter::shops;
        case ObjectUiCategory::sideshows => filter = Filter::sideshows;
        case ObjectUiCategory::features  => filter = Filter::features;
        case _                           => {
            navigation.push_back(record.base.header.id);
            return;
        }
    } const auto rows = objects(filter);
    navigation.push_back(record.base.header.id);
    auto* current = &record;
    while (navigation.size() < rows.size()) {
        const auto found = std::ranges::find(rows, current);
        const auto start = found == rows.end() ? 0u : static_cast<std::size_t>(found - rows.begin());
        ParkObjectRuntimeView *next {}, *fallback {};
        for (std::size_t offset = 1; offset <= rows.size(); ++offset) {
            auto* candidate = rows[(start + offset) % rows.size()];
            if (std::ranges::find(navigation, candidate->base.header.id) != navigation.end()) {
                continue;
            }
            if (!fallback) {
                fallback = candidate;
            }
            if (candidate->definition_id == current->definition_id) {
                next = candidate;
                break;
            }
        }
        if (!next) {
            next = fallback;
        }
        if (!next) {
            break;
        }
        navigation.push_back(next->base.header.id);
        current = next;
    }
}

void WorldParkObjectUiRuntime::State::refresh_settings(bool load) {
    auto& v = view.settings;
    if (!v.open) {
        return;
    }
    auto& record = object(v.object);
    if (&record.base.header != settings_identity) {
        throw std::out_of_range("Park-object settings identity was replaced");
    }
    const auto status = classify_park_object_ui_status(c, record);
    if (load || (v.panel == ParkObjectUiPanel::ride && status != v.status)) {
        load_settings(record);
    }
    v.name = park_object_ui_display_name(record);
    v.definition = record.definition_id;
    v.closed = record.can_load == 0;
    const bool eligible = is_park_object_open_eligible(c, record);
    v.can_toggle_open = !v.closed || eligible;
    if (v.panel == ParkObjectUiPanel::bathroom || v.panel == ParkObjectUiPanel::info_desk) {
        v.can_toggle_open = eligible;
    } else if (v.panel == ParkObjectUiPanel::ride) {
        v.can_toggle_open = status != ParkObjectUiStatus::condemned && status != ParkObjectUiStatus::track_incomplete && status != ParkObjectUiStatus::track_invalid && has_clear_entry_exit_path(c, record) && has_usable_entry_approach(c, &record) && (!v.closed || (eligible && classify_park_object_ui_status(c, record, 1) == ParkObjectUiStatus::open)) && (v.closed || !is_ride_restricted_by_safety_review_challenge(c, record));
    }
    v.status = status;
    v.metrics = metrics(record, v.staged);
    v.promoted_attraction_name = v.staged.promoted_attraction ? park_object_ui_display_name(object(v.staged.promoted_attraction)) : text(284);
    Filter filter = Filter::features;
    match (v.panel) {
        case ParkObjectUiPanel::ride           => filter = Filter::rides;
        case ParkObjectUiPanel::shop           => filter = Filter::shops;
        case ParkObjectUiPanel::sideshow       => filter = Filter::sideshows;
        case ParkObjectUiPanel::bathroom       => filter = Filter::relief;
        case ParkObjectUiPanel::photo_kiosk    => filter = Filter::photo;
        case ParkObjectUiPanel::info_desk      => filter = Filter::info;
        case ParkObjectUiPanel::litter_bin     => filter = Filter::litter;
        case ParkObjectUiPanel::secondary_ride => filter = Filter::addons;
        case _                                 => {
            if (has(record, ParkObjectCapabilities::provides_security)) {
                filter = Filter::security;
            }
        }
    } v.can_navigate = objects(filter).size() > 1;
    ++v.revision;
}

void WorldParkObjectUiRuntime::State::clear_complaints(std::span<const std::int32_t> keys, std::uint16_t definition_id) {
    auto* header = c.entities.at(world().saved.tag_system).payload;
    if (!header || header->kind != ThingKind::tag_system) {
        throw std::logic_error("Park-object settings TagSystem is unavailable");
    }
    auto& tags = *reinterpret_cast<TagSystemRecord*>(header);
    bool marked {};
    //Native636ED0: format DWORD, with STORED zero wildcard; no format WORD.
    for (auto* message = tags.mail_message_head; message; message = message->next) {
        if (!message->marked_for_deletion && (!message->format_dword || message->format_dword == definition_id) && std::ranges::find(keys, message->tag_id) != keys.end()) {
            message->marked_for_deletion = 1;
            tags.messages_need_unlink = 1;
            marked = true;
        }
    }
    if (marked) {
        c.messages.tag_panel_refresh_requested = 1;
    }
}

void WorldParkObjectUiRuntime::State::apply_settings_to(ParkObjectRuntimeView& record, bool bulk) {
    const auto& v = view.settings;
    const auto& s = v.staged;
    const auto& b = balance(record);
    const auto clamp_byte = [](std::int32_t value, std::int32_t minimum, std::int32_t maximum) {
        const auto byte = static_cast<std::uint8_t>(value);
        return static_cast<std::uint8_t>(byte < minimum ? minimum : byte > maximum ? maximum : byte);
    };
    match (v.panel) {
        case ParkObjectUiPanel::ride => {
            auto* script = world_operations::find_script(c, record.script_id);
            set_rsse_script_state_operating_speed(c, script, std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(s.speed)));
            record.operating_speed = static_cast<std::uint32_t>(s.speed);
            auto capacity = static_cast<std::uint8_t>(s.capacity);
            if (fixed::add(b.UsageInfo.MinCapacity, b.UsageInfo.MaxCapacity) > 0) {
                capacity = clamp_byte(s.capacity, b.UsageInfo.MinCapacity, b.UsageInfo.MaxCapacity);
            }
            world_operations::set_script_slot(c, script, 2, capacity);
            record.operating_capacity = capacity;
            record.price_per_use = static_cast<std::uint32_t>(s.price);
            if (b.Info.DurationUnit) {
                const auto duration = clamp_byte(s.duration, b.UsageInfo.MinDuration, b.UsageInfo.MaxDuration);
                world_operations::set_script_slot(c, script, 3, duration);
                record.operating_duration = duration;
            }
            constexpr std::array<std::int32_t, 4> tags { 72, 73, 81, 82 };
            clear_complaints(tags, record.definition_id);
        }
        case ParkObjectUiPanel::shop => {
            record.amount_of_special_ingredient = static_cast<std::uint32_t>(s.ingredient);
            record.quality_of_goods = static_cast<std::uint8_t>(s.quality);
            record.price_per_use = static_cast<std::uint32_t>(s.price);
            record.portion_size = static_cast<std::uint32_t>(s.portion) > 100u ? 0u : static_cast<std::uint32_t>(s.portion);
            constexpr std::array<std::int32_t, 4> tags { 74, 75, 83, 84 };
            clear_complaints(tags, record.definition_id);
        }
        case ParkObjectUiPanel::sideshow => {
            record.price_per_use = static_cast<std::uint32_t>(s.price);
            if (!b.UsageInfo.NonWinnableSideshow) {
                record.chance_of_winning = static_cast<std::uint8_t>(s.chance);
                record.cost_of_goods = static_cast<std::uint32_t>(s.goods_cost);
            }
            //Native585BF0 bulk omits cleanup on a non-winnable game;585CD0 single
            //still performs it. Preserve that observable asymmetry.
            if (!bulk || !b.UsageInfo.NonWinnableSideshow) {
                constexpr std::array<std::int32_t, 4> tags { 76, 77, 85, 86 };
                clear_complaints(tags, record.definition_id);
            }
        }
        case ParkObjectUiPanel::photo_kiosk => {
            record.quality_of_goods = static_cast<std::uint8_t>(s.quality);
            record.price_per_use = static_cast<std::uint32_t>(s.price);
            constexpr std::array<std::int32_t, 4> tags { 78, 79, 87, 88 };
            clear_complaints(tags, record.definition_id);
        }
        case ParkObjectUiPanel::bathroom => {
            record.price_per_use = static_cast<std::uint32_t>(s.price);
            constexpr std::array<std::int32_t, 2> tags { 74, 80 };
            clear_complaints(tags, record.definition_id);
        }
        case ParkObjectUiPanel::info_desk                                                                                                 => (void)set_park_object_promoted_attraction(c, record, s.promoted_attraction);
        case ParkObjectUiPanel::feature or ParkObjectUiPanel::litter_bin or ParkObjectUiPanel::secondary_ride or ParkObjectUiPanel::mode3 => {
            //Verified common slot44 is a native no-op for these controllers.
        }
        case _ => throw std::logic_error("This park-object controller has no staged settings");
    }
}

WorldParkObjectUiRuntime::WorldParkObjectUiRuntime(WorldRuntime& c, ParkConstructionRuntime& editor) : state_(std::make_unique<State>(c, editor)) {}

WorldParkObjectUiRuntime::~WorldParkObjectUiRuntime() {
    close_all();
}

ParkObjectUiState& WorldParkObjectUiRuntime::state() {
    return state_->view;
}

const ParkObjectUiState& WorldParkObjectUiRuntime::state() const {
    return state_->view;
}

ParkObjectUiRoute WorldParkObjectUiRuntime::route(ThingId id) const {
    return state_->route(id);
}

void WorldParkObjectUiRuntime::set_retirement_observer(std::function<void(ThingId)> observer) {
    state_->retirement_observer = std::move(observer);
}

ParkObjectUiRoute WorldParkObjectUiRuntime::open_settings(ThingId id) {
    auto& s = *state_;
    const auto target = s.route(id);
    close_settings();
    if (target.panel == ParkObjectUiPanel::special_ride) {
        open_special(target.target);
        return target;
    }
    if (target.panel == ParkObjectUiPanel::none || target.panel == ParkObjectUiPanel::staff_room || target.panel == ParkObjectUiPanel::training_room || target.panel == ParkObjectUiPanel::research) {
        return target;
    }
    auto& record = s.object(target.target);
    s.view.settings.open = true;
    s.view.settings.panel = target.panel;
    s.view.settings.object = target.target;
    s.view.settings.apply_to_matching = false;
    s.settings_identity = &record.base.header;
    s.c.ui.panels.selected_record = s.settings_identity;
    s.rebuild_navigation(record);
    s.refresh_settings(true);
    return target;
}

void WorldParkObjectUiRuntime::refresh_settings() {
    state_->refresh_settings();
}

void WorldParkObjectUiRuntime::apply_settings() {
    auto& s = *state_;
    auto& v = s.view.settings;
    if (!v.open) {
        throw std::logic_error("Park-object settings are not open");
    }
    auto& selected = s.object(v.object);
    if (&selected.base.header != s.settings_identity) {
        throw std::out_of_range("Park-object apply identity was replaced");
    }
    if (!v.apply_to_matching || v.panel == ParkObjectUiPanel::ride) {
        s.apply_settings_to(selected, false);
        return;
    }
    Filter filter;
    match (v.panel) {
        case ParkObjectUiPanel::shop        => filter = Filter::shops;
        case ParkObjectUiPanel::sideshow    => filter = Filter::sideshows;
        case ParkObjectUiPanel::bathroom    => filter = Filter::relief;
        case ParkObjectUiPanel::photo_kiosk => filter = Filter::photo;
        case ParkObjectUiPanel::info_desk   => filter = Filter::info;
        case _                              => {
            s.apply_settings_to(selected, false);
            return;
        }
    } for (auto* record : s.objects(filter)) {
        if (v.panel == ParkObjectUiPanel::info_desk || record->definition_id == selected.definition_id) {
            s.apply_settings_to(*record, true);
        }
    }
}

void WorldParkObjectUiRuntime::navigate_settings(bool previous) {
    auto& s = *state_;
    auto& v = s.view.settings;
    if (!v.open) {
        return;
    }
    auto& current = s.object(v.object);
    v.apply_to_matching = false;
    apply_settings();
    Filter filter = Filter::features;
    bool use_navigation = false;
    match (v.panel) {
        case ParkObjectUiPanel::ride => {
            filter = Filter::rides;
            use_navigation = true;
        }
        case ParkObjectUiPanel::shop => {
            filter = Filter::shops;
            use_navigation = true;
        }
        case ParkObjectUiPanel::sideshow => {
            filter = Filter::sideshows;
            use_navigation = true;
        }
        case ParkObjectUiPanel::bathroom    => filter = Filter::relief;
        case ParkObjectUiPanel::photo_kiosk => filter = Filter::photo;
        case ParkObjectUiPanel::info_desk   => {
            filter = Filter::info;
            previous = true;
        }
        case ParkObjectUiPanel::litter_bin     => filter = Filter::litter;
        case ParkObjectUiPanel::secondary_ride => filter = Filter::addons;
        case _                                 => {
            if (has(current, ParkObjectCapabilities::provides_security)) {
                filter = Filter::security;
                previous = true;
            } else {
                use_navigation = true;
            }
        }
    } auto rows = s.objects(filter);
    if (use_navigation) {
        rows.clear();
        for (auto id : s.navigation) {
            if (auto* record = s.live(id)) {
                rows.push_back(record);
            }
        }
    }
    if (rows.empty()) {
        return;
    }
    auto found = std::ranges::find(rows, &current);
    const auto index = found == rows.end() ? 0u : static_cast<std::size_t>(found - rows.begin());
    const auto next = previous ? (index + rows.size() - 1) % rows.size() : (index + 1) % rows.size();
    v.object = rows[next]->base.header.id;
    s.settings_identity = &rows[next]->base.header;
    s.c.ui.panels.selected_record = s.settings_identity;
    s.refresh_settings(true);
}

void WorldParkObjectUiRuntime::close_settings() {
    auto& s = *state_;
    if (s.c.ui.panels.selected_record == s.settings_identity) {
        s.c.ui.panels.selected_record = nullptr;
    }
    s.view.settings.open = false;
    s.settings_identity = nullptr;
}

void WorldParkObjectUiRuntime::set_closed(ThingId id, bool closed) {
    auto& s = *state_;
    auto& record = s.object(id);
    if (closed) {
        close_park_object(s.c, record);
    } else {
        open_park_object(s.c, record);
    }
}

void WorldParkObjectUiRuntime::request_service(ThingId id, bool request) {
    auto& s = *state_;
    (void)handle_park_object_maintenance_request(s.c, s.object(id), request);
}

void WorldParkObjectUiRuntime::retire(const ThingHeader& record) {
    auto& s = *state_;
    const auto id = record.id;
    const auto previous_retiring = s.retiring;
    s.retiring = id;
    if (s.c.ui.panels.selected_record == &record) {
        s.c.ui.panels.selected_record = nullptr;
    }
    if (s.settings_identity == &record) {
        s.view.settings.open = false;
        s.settings_identity = nullptr;
    }
    const auto clear = [&](auto& view, const ThingHeader*& identity) {
        if (identity == &record) {
            view.open = false;
            identity = nullptr;
        }
    };
    clear(s.view.statistics, s.statistics_identity);
    clear(s.view.opinions, s.opinions_identity);
    clear(s.view.promotion, s.promotion_identity);
    clear(s.view.upgrade, s.upgrade_identity);
    clear(s.view.special, s.special_identity);
    std::erase_if(s.view.list.rows, [=](const auto& row) {
        return row.object == id || row.parent_object == id;
    });
    std::erase_if(s.view.promotion.rows, [=](const auto& row) {
        return row.object == id;
    });
    std::erase(s.navigation, id);
    if (std::ranges::find(s.view.list.rows, s.view.list.selected, &ParkObjectListUiRow::object) == s.view.list.rows.end()) {
        s.view.list.selected = 0;
        s.view.list.details_visible = false;
    }
    if (s.view.promotion.selected == id) {
        s.view.promotion.selected = 0;
    }
    if (s.view.settings.staged.promoted_attraction == id) {
        s.view.settings.staged.promoted_attraction = 0;
        s.view.settings.promoted_attraction_name = s.text(284);
    }
    for (auto& row : s.view.special.requirements) {
        if (row.staff == id) {
            row.staff = 0;
            row.name.clear();
            row.fulfilled = false;
            s.view.special.ready = s.view.special.can_activate = false;
        }
    }
    ++s.view.list.revision;
    //All identities are gone before the frontend retires its previews and
    //controls; reentrant queries exclude the still-live retiring payload.
    try {
        if (s.retirement_observer) {
            s.retirement_observer(id);
        }
    } catch (...) {
        s.retiring = previous_retiring;
        throw;
    }
    s.retiring = previous_retiring;
}

void WorldParkObjectUiRuntime::handle_event(const WorldEvent& event) {
    auto& s = *state_;
    if (event.type() == 33) {
        const auto id = event.get<RetirementEvent>().retired_id;
        if (id && id < s.c.entities.size()) {
            if (const auto* record = s.c.entities[id].payload) {
                retire(*record);
            }
        }
        return;
    }
    if (s.view.settings.open) {
        const auto target = event.type() == 7 ? event.get<ParkObjectBrokenDownEvent>().park_object_id : event.type() == 8 ? event.get<ParkObjectCondemnedEvent>().park_object_id : ThingId {};
        if (target && target == s.view.settings.object) {
            s.refresh_settings();
        }
    }
}

void WorldParkObjectUiRuntime::refresh() {
    auto& s = *state_;
    s.refresh_settings();
    s.refresh_list(false);
    s.refresh_statistics();
    s.refresh_opinions();
    s.refresh_promotion();
    s.refresh_upgrade();
    s.refresh_special();
}

void WorldParkObjectUiRuntime::close_all() {
    close_settings();
    auto& s = *state_;
    if (s.c.ui.panels.selected_record == s.statistics_identity || s.c.ui.panels.selected_record == s.opinions_identity || s.c.ui.panels.selected_record == s.promotion_identity || s.c.ui.panels.selected_record == s.upgrade_identity || s.c.ui.panels.selected_record == s.special_identity) {
        s.c.ui.panels.selected_record = nullptr;
    }
    s.view.list.open = s.view.finance.open = s.view.statistics.open = s.view.opinions.open = s.view.promotion.open = s.view.upgrade.open = s.view.special.open = false;
    s.view.statistics.graph.open = false;
    s.statistics_identity = s.opinions_identity = s.promotion_identity = s.upgrade_identity = s.special_identity = nullptr;
}
} //namespace simcoaster
