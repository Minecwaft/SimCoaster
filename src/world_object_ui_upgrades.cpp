#include "world_object_ui_internal.hpp"

namespace simcoaster {
using namespace object_ui_detail;

namespace {
Filter promotion_filter(ObjectUiCategory category) {
    match (category) {
        case ObjectUiCategory::rides     => return Filter::rides;
        case ObjectUiCategory::shops     => return Filter::shops;
        case ObjectUiCategory::sideshows => return Filter::sideshows;
        case _                           => throw std::out_of_range("Info Desk attraction category");
    }
}

bool trained_mechanic(WorldRuntime& c, const World& world) {
    std::size_t visited {};
    for (auto id = world.saved.first_mechanic; id;) {
        if (++visited > c.entities.size()) {
            throw std::length_error("Cyclic mechanic roster");
        }
        const auto* header = c.entities.at(id).payload;
        if (!header || header->retirement_pending || header->kind != ThingKind::mechanic) {
            throw std::out_of_range("Invalid mechanic roster");
        }
        const auto& employee = *reinterpret_cast<const StaffRuntimeRecord*>(header);
        if (world_staff_first_known_ability_matches(employee, ThemeAbility::ride_upgrading)) {
            return true;
        }
        id = employee.next_staff;
    }
    return false;
}

bool any_staff(WorldRuntime& c, ThingKind kind, ThingId retiring, std::optional<ThemeAbility> ability = {}) {
    for (auto* entry = c.entities.active_head; entry; entry = entry->active_next) {
        const auto* header = entry->payload;
        if (!header || header->id == retiring || header->retirement_pending || header->kind != kind) {
            continue;
        }
        if (!ability || reinterpret_cast<const StaffRuntimeRecord*>(header)->first_known_ability == *ability) {
            return true;
        }
    }
    return false;
}
} //namespace

ParkObjectContextUiState WorldParkObjectUiRuntime::context(ThingId id) const {
    auto& s = *state_;
    auto& record = s.object(id);
    if (id == s.world().saved.park_gates) {
        //Native505DF0's families14/15 share their complete one-option table.
        //They do not evaluate ride purchase/service/upgrade predicates.
        ParkObjectContextUiState out;
        out.name = park_object_ui_display_name(record);
        out.menu_family = 14;
        return out;
    }
    const auto& d = s.definition(record);
    const auto& b = *d.balance;
    ParkObjectContextUiState out;
    out.route = s.route(id);
    out.name = park_object_ui_display_name(record);
    out.unregistered_special = is_special_ride_group_id_unregistered(s.c, id);
    if (d.which_ui_type == ObjectUiCategory::rides) {
        out.menu_family = d.is_special == 1 ? 18 : 4;
    } else if (d.which_ui_type == ObjectUiCategory::shops) {
        out.menu_family = b.Info.IsPhotoBooth ? 24 : 19;
    } else if (d.which_ui_type == ObjectUiCategory::sideshows) {
        out.menu_family = 20;
    } else if (b.UsageInfo.IsInfoDesk) {
        out.menu_family = 23;
    } else if (has(record, ParkObjectCapabilities::provides_relief)) {
        out.menu_family = 21;
    } else if (has(record, ParkObjectCapabilities::chills_you_out)) {
        out.menu_family = 25;
    } else if (has(record, ParkObjectCapabilities::thinking_space)) {
        out.menu_family = 27;
    } else if (has(record, ParkObjectCapabilities::training_room)) {
        out.menu_family = 26;
    } else if (has(record, ParkObjectCapabilities::provides_security)) {
        out.menu_family = 30;
    } else if (has(record, ParkObjectCapabilities::holds_litter)) {
        out.menu_family = 22;
    } else if (has(record, ParkObjectCapabilities::requires_watering)) {
        out.menu_family = 29;
    } else {
        out.menu_family = 28;
    }
    out.closed = record.can_load == 0;
    out.can_open = out.closed && s.c.mode.game != GameMode::code_3 && is_park_object_open_eligible(s.c, record);
    out.can_close = !out.closed && s.c.mode.game != GameMode::code_3;
    out.can_request_service = s.world().saved.first_mechanic && !record.requested_service && record.state != ParkObjectState::condemned;
    const bool litter = has(record, ParkObjectCapabilities::holds_litter);
    const bool plant = has(record, ParkObjectCapabilities::requires_watering);
    if (litter || plant) {
        auto* script = world_operations::find_script(s.c, record.script_id);
        const bool blocked = world_operations::script_slot(s.c, script, litter ? 5 : 12) != 0;
        const bool fire_staff = any_staff(s.c, ThingKind::handyman, s.retiring, ThemeAbility::firefighting);
        if (litter) {
            out.can_request_janitor = !record.requested_service && any_staff(s.c, ThingKind::handyman, s.retiring) && (!blocked || fire_staff);
        } else if (plant) {
            out.can_request_gardener = !record.requested_service && !blocked && !world_operations::script_slot(s.c, script, 4) && any_staff(s.c, ThingKind::gardener, s.retiring);
            //Native556380's Firefighting test scans the active registry; no
            //fatigue, work-state, strike, or extra dead-plant gate is present.
            out.can_request_janitor = !record.requested_service && blocked && fire_staff;
        }
        out.can_request_service = litter ? out.can_request_janitor : out.can_request_gardener;
    } else if (has(record, ParkObjectCapabilities::provides_relief)) {
        out.can_request_janitor = !record.requested_service && any_staff(s.c, ThingKind::handyman, s.retiring);
        out.can_request_service = out.can_request_janitor;
    }
    out.can_cancel_service = record.requested_service != 0;
    out.can_edit_track = b.Bumper.WhichTrackType > 0 && (d.is_special != 1 || is_special_ride_group_id_unregistered(s.c, id));
    out.can_ride = record.state != ParkObjectState::condemned && (!s.world().saved.park_closed || s.c.mode.game == GameMode::code_3);
    if (b.Bumper.WhichTrackType == 3) {
        const auto* opening = world_operations::find_mesh_runtime_opening_state(s.c, record.mesh_instance_id);
        out.can_ride = out.can_ride && opening && !opening->geometry_peer_reference_count;
    }
    out.can_delete = d.which_ui_type != ObjectUiCategory::rides || d.is_special != 1;
    const auto* control = find_world_object_control(s.world().object_controls, record.definition_id);
    if (!control) {
        throw std::out_of_range("Park-object context purchase control");
    }
    out.can_copy = fixed::word(control->purchase_cost) <= s.bank().balance;
    if (record.state != ParkObjectState::condemned) {
        if (record.state == ParkObjectState::upgrade_requested) {
            out.can_upgrade = true;
        }
        if (record.current_upgrade < 2 && static_cast<std::int32_t>(record.current_upgrade + 1u) <= fixed::word(control->research_option_index)) {
            out.can_upgrade = true;
        }
        for (const auto* candidate : s.c.resources.object_catalog.entries) {
            if (candidate && candidate->balance && !candidate->mesh_load_failed && (candidate->add_on.upgrades_id == record.definition_id || candidate->balance->AddOn.UpgradesAnyRide == 1) && (!record.photo_booth || !candidate->balance->Info.IsPhotoBooth)) {
                const auto* candidate_control = find_world_object_control(s.world().object_controls, candidate->id);
                if (!candidate_control) {
                    throw std::out_of_range("Park-object upgrade control");
                }
                if (candidate_control->available) {
                    out.can_upgrade = true;
                }
            }
        }
    }
    return out;
}

void WorldParkObjectUiRuntime::State::refresh_promotion(bool rebuild) {
    auto& v = view.promotion;
    if (!v.open) {
        return;
    }
    if (&object(v.info_desk).base.header != promotion_identity) {
        throw std::out_of_range("Info Desk picker target was replaced");
    }
    if (rebuild) {
        v.rows.clear();
        for (auto* record : objects(promotion_filter(v.category))) {
            if (!balance(*record).UsageInfo.IsInfoDesk) {
                const auto status = classify_park_object_ui_status(c, *record);
                v.rows.push_back({ record->base.header.id, park_object_ui_display_name(*record), status, park_object_ui_status_presentation(status).color });
            }
        }
    } else {
        std::erase_if(v.rows, [&](const auto& entry) {
            return !live(entry.object);
        });
        for (auto& entry : v.rows) {
            entry.status = classify_park_object_ui_status(c, object(entry.object));
            entry.color = park_object_ui_status_presentation(entry.status).color;
        }
    }
    if (rebuild && v.sort_key) {
        std::ranges::stable_sort(v.rows, [&](const auto& a, const auto& b) {
            return v.sort_key < 0 ? a.name > b.name : a.name < b.name;
        });
    }
    if (std::ranges::find(v.rows, v.selected, &ParkObjectPromotionUiRow::object) == v.rows.end()) {
        v.selected = 0;
    }
}

void WorldParkObjectUiRuntime::open_promotion(ThingId id, bool return_to_settings) {
    auto& s = *state_;
    auto& record = s.object(id);
    if (!s.balance(record).UsageInfo.IsInfoDesk) {
        throw std::invalid_argument("Promotion target is not an Info Desk");
    }
    auto& v = s.view.promotion;
    v.open = true;
    v.info_desk = id;
    v.return_to_settings = return_to_settings;
    v.selected = 0;
    s.promotion_identity = &record.base.header;
    s.refresh_promotion(true);
}

void WorldParkObjectUiRuntime::filter_promotion(ObjectUiCategory category) {
    (void)promotion_filter(category);
    state_->view.promotion.category = category;
    state_->refresh_promotion(true);
}

void WorldParkObjectUiRuntime::sort_promotion(std::int32_t key) {
    if (key != -1 && key != 0 && key != 1) {
        throw std::out_of_range("Info Desk picker sort key");
    }
    state_->view.promotion.sort_key = key;
    auto& rows = state_->view.promotion.rows;
    if (key) {
        std::ranges::stable_sort(rows, [=](const auto& a, const auto& b) {
            return key < 0 ? a.name > b.name : a.name < b.name;
        });
    }
}

void WorldParkObjectUiRuntime::choose_promotion(ThingId id) {
    auto& s = *state_;
    auto& v = s.view.promotion;
    if (!v.open) {
        throw std::logic_error("Info Desk picker is not open");
    }
    auto& desk = s.object(v.info_desk);
    if (&desk.base.header != s.promotion_identity) {
        throw std::out_of_range("Info Desk picker target was replaced");
    }
    if (id && std::ranges::find(v.rows, id, &ParkObjectPromotionUiRow::object) == v.rows.end()) {
        throw std::out_of_range("Info Desk attraction selection is unavailable");
    }
    if (id) {
        (void)s.object(id);
    }
    if (v.return_to_settings) {
        if (id) {
            (void)open_settings(v.info_desk);
        }
        s.view.settings.staged.promoted_attraction = id;
        s.refresh_settings();
    } else {
        (void)set_park_object_promoted_attraction(s.c, desk, id);
    }
    v.open = false;
    s.promotion_identity = nullptr;
}

void WorldParkObjectUiRuntime::State::refresh_upgrade(bool rebuild) {
    auto& v = view.upgrade;
    if (!v.open) {
        return;
    }
    auto& record = object(v.object);
    if (&record.base.header != upgrade_identity) {
        throw std::out_of_range("Ride-upgrade target was replaced");
    }
    const auto funds = bank().balance;
    const auto& b = balance(record);
    if (!rebuild) {
        for (auto& entry : v.rows) {
            match (entry.kind) {
                case ParkObjectUpgradeKind::attachment => {
                    const auto* control = find_world_object_control(world().object_controls, static_cast<std::uint16_t>(entry.key));
                    if (!control) {
                        throw std::out_of_range("Ride-upgrade attachment control");
                    }
                    entry.affordable = funds >= fixed::word(control->purchase_cost);
                }
                case ParkObjectUpgradeKind::level => {
                    entry.affordable = funds > b.Upgrades.at(static_cast<std::size_t>(-entry.key)).CostOfUpgrade;
                }
                case ParkObjectUpgradeKind::cancel      => entry.affordable = true;
                case ParkObjectUpgradeKind::information => entry.affordable = false;
                case _                                  => {
                }
            }
        }
        return;
    }
    v.rows.clear();
    if (c.mode.game == GameMode::code_2) {
        if (record.current_upgrade == 2) {
            v.rows.push_back({ -7890, ParkObjectUpgradeKind::information, text(125), 0, false });
        }
        return;
    }
    for (const auto* candidate : c.resources.object_catalog.entries) {
        if (candidate && candidate->balance && !candidate->mesh_load_failed && (candidate->add_on.upgrades_id == record.definition_id || candidate->balance->AddOn.UpgradesAnyRide == 1) && (!record.photo_booth || !candidate->balance->Info.IsPhotoBooth)) {
            const auto* control = find_world_object_control(world().object_controls, candidate->id);
            if (!control) {
                throw std::out_of_range("Ride attachment purchase control");
            }
            if (control->available) {
                const auto cost = fixed::word(control->purchase_cost);
                v.rows.push_back({ candidate->id, ParkObjectUpgradeKind::attachment, get_catalog_object_display_name(c, candidate->id), cost, funds >= cost });
            }
        }
    }
    if (record.state == ParkObjectState::upgrade_requested) {
        const std::array arguments { BfstSignedIntegerArgument { 3, record.current_upgrade + 2 } };
        v.rows.push_back({ -12999, ParkObjectUpgradeKind::cancel, text(127, arguments), 0, true });
    } else if (record.current_upgrade < 2) {
        const auto* control = find_world_object_control(world().object_controls, record.definition_id);
        if (!control) {
            throw std::out_of_range("Ride upgrade research control");
        }
        const auto next = static_cast<std::int32_t>(record.current_upgrade + 1u);
        if (next <= fixed::word(control->research_option_index)) {
            const std::array arguments { BfstSignedIntegerArgument { 3, next + 1 } };
            const auto cost = b.Upgrades.at(static_cast<std::size_t>(next)).CostOfUpgrade;
            v.rows.push_back({ -next, ParkObjectUpgradeKind::level, text(126, arguments), cost, funds > cost });
        }
    }
    if (v.rows.empty() && record.current_upgrade == 2) {
        v.rows.push_back({ -7890, ParkObjectUpgradeKind::information, text(125), 0, false });
    }
    if (v.sort_key) {
        std::ranges::stable_sort(v.rows, [&](const auto& a, const auto& other) {
            const auto key = v.sort_key < 0 ? -std::int64_t { v.sort_key } : std::int64_t { v.sort_key };
            if (key == 1) {
                return v.sort_key < 0 ? a.name > other.name : a.name < other.name;
            }
            if (key == 2) {
                return v.sort_key < 0 ? a.cost > other.cost : a.cost < other.cost;
            }
            throw std::out_of_range("Ride upgrade sort key");
        });
    }
}

void WorldParkObjectUiRuntime::open_upgrade(ThingId id) {
    auto& s = *state_;
    auto& v = s.view.upgrade;
    auto& record = s.object(id);
    v.object = id;
    v.trained_mechanic = trained_mechanic(s.c, s.world());
    v.message_row = v.trained_mechanic ? std::optional<std::uint32_t> {} : std::optional<std::uint32_t> { 11 };
    v.open = v.trained_mechanic;
    v.selected = 0;
    s.upgrade_identity = &record.base.header;
    s.refresh_upgrade(true);
}

void WorldParkObjectUiRuntime::refresh_upgrade() {
    state_->refresh_upgrade();
}

bool WorldParkObjectUiRuntime::apply_upgrade(std::int32_t key) {
    auto& s = *state_;
    auto& v = s.view.upgrade;
    if (!v.open) {
        return false;
    }
    s.refresh_upgrade();
    const auto row = std::ranges::find(v.rows, key, &ParkObjectUpgradeUiRow::key);
    if (row == v.rows.end() || !row->affordable || row->kind == ParkObjectUpgradeKind::information) {
        return false;
    }
    auto& record = s.object(v.object);
    if (row->kind == ParkObjectUpgradeKind::cancel) {
        if (record.state == ParkObjectState::upgrade_requested) {
            world_operations::configure_mesh_instance_deformation_animation(s.c, record.mesh_instance_id);
            open_park_object(s.c, record);
        }
    } else if (row->kind == ParkObjectUpgradeKind::level) {
        if (!has(record, ParkObjectCapabilities::is_choosable)) {
            return false;
        }
        transition_park_object_state(s.c, record, ParkObjectState::upgrade_requested);
        world_operations::select_park_object_mesh_animation_variant(s.c, record.mesh_instance_id, ParkObjectMeshVariant::upgrade);
    } else {
        cancel_world_staff_interaction(s.c);
        s.c.view.secondary_cursor_record_snapshot = &record.base.header;
        s.c.construction.park_object = &record;
        s.construction.begin_placement(s.c, static_cast<std::uint16_t>(key), 0);
        s.construction.refresh_origin(s.c, true);
    }
    v.open = false;
    return true;
}

void WorldParkObjectUiRuntime::State::refresh_special() {
    auto& v = view.special;
    if (!v.open) {
        return;
    }
    auto& record = object(v.object);
    if (&record.base.header != special_identity) {
        throw std::out_of_range("Special-ride UI target was replaced");
    }
    v.name = park_object_ui_display_name(record);
    auto& theme = get_or_create_theme_global_info_manager(c);
    v.ticket_cost = balance(record).UsageInfo.GoldenTicketCost;
    v.ticket_balance = theme.golden_ticket_balance;
    v.ready = is_special_ride_construction_ready(c, v.object);
    v.can_activate = v.ready;
    v.abilities_activated = true;
    v.requirements.clear();
    SpecialRideRegistryGroup* group {};
    for (std::uint32_t i = 0; i < c.attractions.special_ride_group_count; ++i) {
        if (c.attractions.special_ride_groups.at(i).thing_id == v.object) {
            group = &c.attractions.special_ride_groups[i];
            break;
        }
    }
    if (!group) {
        throw std::out_of_range("Special-ride registry group is unavailable");
    }
    for (std::size_t i = 0; i < group->members.size(); ++i) {
        auto& requirement = group->members[i];
        if (requirement.type == SpecialRideRequirementType::end) {
            break;
        }
        StaffRuntimeRecord* employee {};
        if (requirement.assigned_staff && requirement.assigned_staff != retiring) {
            const auto* header = c.entities.at(requirement.assigned_staff).payload;
            if (header && !header->retirement_pending && is_staff_record_kind(header->kind)) {
                auto* candidate = c.entities.at_payload<StaffRuntimeRecord>(requirement.assigned_staff);
                if (candidate->first_known_ability == requirement.ability) {
                    employee = candidate;
                }
            }
        }
        if (!employee) {
            requirement.assigned_staff = 0;
        }
        //Native55E1F0 only auto-fills Janitor/Engineer/Gardener, not Guard.
        if (!employee && (requirement.type == SpecialRideRequirementType::handyman || requirement.type == SpecialRideRequirementType::mechanic || requirement.type == SpecialRideRequirementType::gardener)) {
            auto next = requirement.type == SpecialRideRequirementType::handyman ? world().saved.first_handyman : requirement.type == SpecialRideRequirementType::mechanic ? world().saved.first_mechanic : world().saved.first_gardener;
            std::size_t visited {};
            while (next) {
                if (++visited > c.entities.size()) {
                    throw std::length_error("Cyclic special-ride staff roster");
                }
                const auto* header = c.entities.at(next).payload;
                if (!header || !is_staff_record_kind(header->kind)) {
                    throw std::out_of_range("Invalid special-ride staff roster");
                }
                auto* candidate = c.entities.at_payload<StaffRuntimeRecord>(next);
                if (next != retiring && !header->retirement_pending && candidate->first_known_ability == requirement.ability && std::ranges::none_of(group->members, [=](const auto& other) {
                        return other.assigned_staff == next;
                    })) {
                    employee = candidate;
                    requirement.assigned_staff = next;
                    break;
                }
                next = candidate->next_staff;
            }
        }
        const auto ability = static_cast<std::size_t>(requirement.ability);
        v.abilities_activated = v.abilities_activated && theme.theme_ability_activation_flags.at(ability) != 0;
        ParkObjectSpecialRequirementUiRow output;
        output.slot = i;
        output.type = requirement.type;
        output.ability = requirement.ability;
        output.staff = requirement.assigned_staff;
        output.fulfilled = employee != nullptr;
        output.ability_activated = theme.theme_ability_activation_flags.at(ability) != 0;
        const auto role = static_cast<std::uint32_t>(requirement.type);
        output.role = text(c.messages.mail_language.staff_types, role * 2u);
        output.ability_name = text(c.messages.mail_language.abilities, static_cast<std::uint32_t>(ability));
        if (employee) {
            output.name = { employee->name.begin(), std::ranges::find(employee->name, char16_t {}) };
        }
        v.requirements.push_back(std::move(output));
    }
    ++v.revision;
}

void WorldParkObjectUiRuntime::open_special(ThingId id) {
    auto& s = *state_;
    auto& record = s.object(id);
    if (s.balance(record).Info.IsSpecial != 1 || is_special_ride_group_id_unregistered(s.c, id)) {
        throw std::invalid_argument("Special-ride dialog target is not registered");
    }
    s.view.special.open = true;
    s.view.special.object = id;
    s.special_identity = &record.base.header;
    s.c.ui.panels.selected_record = s.special_identity;
    //Native5863E0 validates once before its first ordinary refresh.
    s.refresh_special();
    s.refresh_special();
}

void WorldParkObjectUiRuntime::refresh_special() {
    state_->refresh_special();
}

bool WorldParkObjectUiRuntime::activate_special() {
    auto& s = *state_;
    if (!s.view.special.open) {
        return false;
    }
    auto& record = s.object(s.view.special.object);
    if (&record.base.header != s.special_identity) {
        throw std::out_of_range("Special-ride activation target was replaced");
    }
    const auto result = activate_special_ride_for_construction(s.c, s.construction, record.base.header.id);
    if (!result) {
        return false;
    }
    s.view.special.open = false;
    return true;
}
} //namespace simcoaster
