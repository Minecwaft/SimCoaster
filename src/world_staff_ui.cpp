#include "simcoaster/world_staff_ui.hpp"
#include "world_ui_staff_columns.hpp"
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <bit>
#include <map>
#include <set>
#include <stdexcept>
#include <variant>

namespace simcoaster {
bool matches_staff_hire_snapshot(const StaffCandidate& a, const StaffCandidate& b) {
    return a.type == b.type && a.name_index == b.name_index && a.salary == b.salary && a.agency_index == b.agency_index && a.resource_variant == b.resource_variant && a.creation_tick == b.creation_tick && a.timeout == b.timeout && a.experience == b.experience;
}

namespace {
std::size_t role_index(StaffType role) {
    const auto value = std::to_underlying(role);
    if (value < 0 || value >= 6) {
        throw std::out_of_range("Staff UI role");
    }
    return static_cast<std::size_t>(value);
}

bool concrete_role(StaffType role) {
    return std::to_underlying(role) >= 0 && std::to_underlying(role) < 6;
}

std::u16string staff_name(const StaffRuntimeRecord& staff) {
    return { staff.name.begin(), std::ranges::find(staff.name, char16_t {}) };
}

std::int32_t percent_meter(float value) {
    //51FD50/58D450 preserve the lowBYTE of truncation before scaling.
    return (static_cast<std::uint8_t>(fixed::truncate_integer(value)) << 10) / 100;
}

using SortValue = std::variant<std::int32_t, std::u16string_view>;

template <class Columns, class Row>
void sort_rows(std::vector<Row>& rows, std::int32_t key) {
    if (!key) {
        return;
    }
    const auto absolute = key < 0 ? -static_cast<std::int64_t>(key) : key;
    if (absolute < 1 || static_cast<std::uint64_t>(absolute) > Columns::size) {
        throw std::out_of_range("Staff UI signed sort key");
    }
    const auto column = static_cast<std::size_t>(absolute - 1);
    std::ranges::stable_sort(rows, [&](const Row& a, const Row& b) {
        return Columns::precedes(a, b, column, key < 0, "Staff UI signed sort key");
    });
}

bool custom_patrol(const StaffRuntimeRecord& staff) {
    const auto& b = staff.area.bounds;
    const auto first = (static_cast<std::uint32_t>(b.start_y) << 7) + static_cast<std::uint32_t>(b.start_x) + 1;
    const auto last = (static_cast<std::uint32_t>(b.end_y) << 7) + static_cast<std::uint32_t>(b.end_x) + 1;
    return static_cast<CellId>(first) != 1 || static_cast<CellId>(last) != map_cell_count;
}

StaffActivityStatus displayed_activity(const StaffRuntimeRecord& staff) {
    const auto activity = classify_staff_activity(staff);
    return activity == StaffActivityStatus::looking_for_work && !custom_patrol(staff) ? StaffActivityStatus::idle : activity;
}

void training_marker(StaffRuntimeRecord& staff, bool mark) {
    //62B960 substitutes the state word without executing transition effects.
    if (mark) {
        if (staff.state == StaffState::training) {
            staff.state = StaffState::training_paused;
        } else if (staff.state == StaffState::walking_to_training_room) {
            staff.state = StaffState::walking_to_training_room_paused;
        }
    } else {
        if (staff.state == StaffState::training_paused) {
            staff.state = StaffState::training;
        } else if (staff.state == StaffState::walking_to_training_room_paused) {
            staff.state = StaffState::walking_to_training_room;
        }
    }
}

bool training_travel(const StaffRuntimeRecord& staff) {
    return staff.state == StaffState::walking_to_training_room || ((staff.state == StaffState::frozen || staff.state == StaffState::frozen_editing_patrol_area) && staff.saved_state == StaffState::walking_to_training_room);
}
} //namespace

struct WorldStaffUiRuntime::State {
    WorldRuntime& c;
    std::filesystem::path root;
    StaffUiState view;
    std::function<void(ThingId)> retirement_observer;
    std::optional<StaffCandidate> selected_candidate;
    std::set<ThingId> training_marked;
    std::map<std::string, std::vector<std::byte>, std::less<>> language;

    explicit State(WorldRuntime& context, const std::filesystem::path& path) : c(context), root(path) {
        if (c.ui.panels.staff_retiring) {
            throw std::logic_error("Staff UI retirement owner is already bound");
        }
        c.ui.panels.staff_retiring = [this](WorldRuntime&, const ThingHeader& record) {
            retire(record);
        };
    }

    ~State() {
        c.ui.panels.staff_retiring = {};
    }

    World& world() const {
        if (!c.referenced_world()) {
            throw std::logic_error("Staff UI primary World is not bound");
        }
        return *c.referenced_world();
    }

    StaffRuntimeRecord* live_staff(ThingId id) const {
        if (!id || id >= c.entities.size()) {
            return nullptr;
        }
        auto* payload = c.entities[id].payload;
        return payload && !payload->retirement_pending && is_staff_record_kind(payload->kind) ? reinterpret_cast<StaffRuntimeRecord*>(payload) : nullptr;
    }

    StaffRuntimeRecord& staff(ThingId id) const {
        auto* record = live_staff(id);
        if (!record) {
            throw std::out_of_range("Staff UI identity is not a live employee");
        }
        return *record;
    }

    ParkObjectRuntimeView& room(ThingId id, ParkObjectCapabilities capability) const {
        if (!id || id >= c.entities.size()) {
            throw std::out_of_range("Staff UI room identity");
        }
        auto* payload = c.entities[id].payload;
        if (!payload || payload->retirement_pending || payload->kind != ThingKind::park_object) {
            throw std::out_of_range("Staff UI room is not a live park object");
        }
        auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(payload);
        if (!(std::to_underlying(object.capabilities) & std::to_underlying(capability))) {
            throw std::invalid_argument("Staff UI room has the wrong capability");
        }
        return object;
    }

    template <class Function>
    void each_staff(Function function) const {
        for (auto* node = c.entities.active_head; node; node = node->active_next) {
            if (auto* record = live_staff(static_cast<ThingId>(node->id_storage))) {
                function(*record);
            }
        }
    }

    const std::vector<std::byte>& table(std::string name) {
        if (const auto found = language.find(name); found != language.end()) {
            return found->second;
        }
        const auto path = (root / "Data/Language/American" / name).native_encoded_string();
        const auto content = c.ports.assets.read_balance_asset(c, path);
        if (!content) {
            throw std::runtime_error("Missing Staff UI language resource: " + path);
        }
        const auto bytes = std::as_bytes(std::span { content->data(), content->size() });
        return language.emplace(std::move(name), std::vector<std::byte> { bytes.begin(), bytes.end() }).first->second;
    }

    std::u16string text(std::span<const std::byte> source, std::uint32_t row, std::span<const BfstSignedIntegerArgument> integers = {}, std::span<const BfstWideStringArgument> strings = {}) const {
        auto result = format_bfst_wide_string(source, c.messages.mail_language.conversion, row, 16383, strings, integers);
        if (!result) {
            throw std::runtime_error("Staff UI localized row " + std::to_string(row) + ": " + result.error().reason);
        }
        return std::move(*result);
    }

    std::u16string agency(std::uint32_t index) const {
        //Original9D2CD8 indexes these four localized agency names.
        constexpr std::array<std::uint32_t, 4> rows { 110, 111, 112, 113 };
        return text(c.messages.mail_language.ui_text, rows.at(index));
    }

    StaffUiRow row(const StaffRuntimeRecord& record) {
        const auto maximum = c.staff.recruitment.balance.all.MaxExperience;
        if (!maximum) {
            throw std::domain_error("Staff UI maximum experience is zero");
        }
        const auto role = world_staff_type_for_used_thing(record.base.header);
        StaffUiRow result;
        result.staff = record.base.header.id;
        result.role = role;
        result.name = staff_name(record);
        result.role_name = text(c.messages.mail_language.staff_types, static_cast<std::uint32_t>(role) * 2);
        result.ability = text(c.messages.mail_language.abilities, static_cast<std::uint32_t>(record.first_known_ability));
        result.activity = text(table("STAFFSTATES.str"), static_cast<std::uint32_t>(displayed_activity(record)));
        result.experience_meter = static_cast<std::int32_t>((record.experience << 10) / static_cast<std::uint32_t>(maximum));
        result.wage = fixed::word(record.salary);
        result.happiness_meter = percent_meter(record.happiness_score);
        result.energy_meter = percent_meter(record.energy);
        result.ability_id = record.first_known_ability;
        return result;
    }

    template <class Record>
    Record& record(ThingId id, ThingKind kind) const {
        if (!id || id >= c.entities.size() || !c.entities[id].payload || c.entities[id].payload->kind != kind) {
            throw std::logic_error("Staff UI World owner has the wrong record kind");
        }
        return *c.entities.payload<Record>(id);
    }

    std::vector<ThingId> role_ids(StaffType role, ThingId excluded = 0) const {
        const auto& saved = world().saved;
        const std::array heads { saved.first_handyman, saved.first_mechanic, saved.first_entertainer, saved.first_guard, saved.first_researcher, saved.first_gardener };
        std::vector<ThingId> ids;
        std::set<ThingId> visited;
        for (auto id = heads.at(role_index(role)); id;) {
            if (!visited.insert(id).second) {
                throw std::logic_error("Staff UI found a cyclic role chain");
            }
            const auto& employee = staff(id);
            if (world_staff_type_for_used_thing(employee.base.header) != role) {
                throw std::logic_error("Staff UI role chain contains a different role");
            }
            if (id != excluded) {
                ids.push_back(id);
            }
            id = employee.next_staff;
        }
        return ids;
    }

    std::array<bool, 6> available_roles() const {
        std::array<bool, 6> available {};
        each_staff([&](const StaffRuntimeRecord& record) {
            available[role_index(world_staff_type_for_used_thing(record.base.header))] = true;
        });
        return available;
    }

    void refresh_hire();
    void refresh_list();
    void refresh_details();
    void refresh_locator();
    void refresh_activity();
    void refresh_selector();
    void refresh_staff_room(bool rebuild = false);
    void retire(const ThingHeader&);
};

void WorldStaffUiRuntime::State::refresh_hire() {
    auto& model = view.hire;
    if (!model.open) {
        return;
    }
    const auto index = role_index(model.filter);
    constexpr std::array<std::uint32_t, 6> headings { 212, 214, 215, 216, 217, 213 };
    model.heading_row = headings[index];
    model.available.fill(true);
    model.message_row = 0;
    const bool was_empty = model.rows.empty();
    model.rows.clear();
    const auto& control = record<StaffHqRecord>(world().saved.staff_hq, ThingKind::staff_hq);
    if (control.strikes[index].strike_active) {
        model.message_row = 224;
    }
    if (!model.message_row && model.filter == StaffType::researcher) {
        bool facility = false;
        for (auto id = world().saved.first_park_object; id;) {
            const auto& object = record<ParkObjectRuntimeView>(id, ThingKind::park_object);
            if ((std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::thinking_space)) != 0) {
                facility = true;
                break;
            }
            id = object.next_park_object;
        }
        if (!facility) {
            model.message_row = 222;
        }
    }
    if (!model.message_row) {
        const auto maximum = c.staff.recruitment.balance.all.MaxExperience;
        if (!maximum) {
            throw std::domain_error("Staff candidate maximum experience is zero");
        }
        for (std::size_t slot = 0; slot < world().staff_candidates.size(); ++slot) {
            const auto& candidate = world().staff_candidates[slot];
            if (!candidate.valid || candidate.type != model.filter) {
                continue;
            }
            //630220 truncates the percentage FIRST; 540940 scales that integer.
            const auto percent = static_cast<std::uint32_t>(fixed::truncate_integer(static_cast<double>(candidate.experience) / maximum * 100.0));
            model.rows.push_back({ slot, candidate.type, c.staff.recruitment.names.at(index).at(candidate.name_index), agency(candidate.agency_index), static_cast<std::int32_t>((percent << 10) / 100u), fixed::word(candidate.salary), candidate.resource_variant });
        }
        sort_rows<StaffHireColumns>(model.rows, model.sort_key);
        if (model.rows.empty()) {
            model.message_row = 225;
        }
    }
    const bool retained = model.selected_slot >= 0 && selected_candidate && std::ranges::any_of(model.rows, [&](const auto& row) {
                              return row.slot == static_cast<std::size_t>(model.selected_slot) && matches_staff_hire_snapshot(world().staff_candidates[row.slot], *selected_candidate);
                          });
    if (!retained) {
        model.selected_slot = -1;
        selected_candidate.reset();
        c.staff.recruitment.selected_candidate = -1;
        //Native540940 inserts with select_if_first_row=1, then sorts the
        //completed list before the queued selection notification is handled.
        //The first populated visible row is therefore selected automatically.
        if (was_empty && !model.rows.empty()) {
            const auto slot = model.rows.front().slot;
            model.selected_slot = static_cast<std::int32_t>(slot);
            selected_candidate = world().staff_candidates[slot];
            c.staff.recruitment.selected_candidate = model.selected_slot;
        }
    }
    ++model.revision;
}

void WorldStaffUiRuntime::State::refresh_list() {
    auto& model = view.list;
    if (!model.open) {
        return;
    }
    model.available = available_roles();
    constexpr std::array priority { StaffType::handyman, StaffType::mechanic, StaffType::gardener, StaffType::entertainer, StaffType::guard, StaffType::researcher };
    if (!concrete_role(model.filter) || !model.available[role_index(model.filter)]) {
        const auto found = std::ranges::find_if(priority, [&](auto role) {
            return model.available[role_index(role)];
        });
        model.filter = found == priority.end() ? StaffType::unsupported_index : *found;
    }
    model.rows.clear();
    if (concrete_role(model.filter)) {
        for (const auto id : role_ids(model.filter)) {
            model.rows.push_back(row(staff(id)));
        }
    }
    sort_rows<StaffListColumns>(model.rows, model.sort_key);
    if (std::ranges::none_of(model.rows, [&](const auto& item) {
            return item.staff == model.selected;
        })) {
        model.selected = 0;
    }
    model.status = model.selected ? row(staff(model.selected)).activity : std::u16string {};
    std::uint32_t total = 0, role_total = 0, count = 0, role_count = 0;
    each_staff([&](const StaffRuntimeRecord& employee) {
        const auto happiness = static_cast<std::uint8_t>(fixed::truncate_integer(employee.happiness_score));
        total += happiness;
        ++count;
        if (world_staff_type_for_used_thing(employee.base.header) == model.filter) {
            role_total += happiness;
            ++role_count;
        }
    });
    //5B3530 uses an integer role mean; 5AFDC0 retains a fractional all-role
    //mean.
    model.role_happiness_meter = role_count ? static_cast<std::int32_t>(((role_total / role_count) << 10) / 100u) : 0;
    model.all_happiness_meter = count ? fixed::truncate_integer(static_cast<double>(total) / count * 1024.0 * 0.0099999998) : 0;
    ++model.revision;
}

void WorldStaffUiRuntime::State::refresh_details() {
    auto& model = view.details;
    if (!model.open) {
        return;
    }
    const auto* employee = live_staff(model.staff);
    if (!employee) {
        model.open = false;
        model.staff = 0;
        return;
    }
    model.row = row(*employee);
    model.agency = agency(employee->agency);
    model.activity = classify_staff_activity(*employee);
    //5020A0's non-active frozen/pickup cases deliberately use part9.
    match (model.activity) {
        case StaffActivityStatus::idle or StaffActivityStatus::looking_for_work => model.activity_icon = 0;
        case StaffActivityStatus::working                                       => model.activity_icon = 1;
        case StaffActivityStatus::resting                                       => model.activity_icon = 2;
        case StaffActivityStatus::on_strike                                     => model.activity_icon = 3;
        case _                                                                  => model.activity_icon = 9;
    } model.status = model.row.activity;
    model.motivation_meter = percent_meter(employee->motivation);
    model.sprite_category = employee->mobile.sprite_category;
    model.sprite_variant = employee->mobile.sprite_id;
    FileTime now {};
    calculate_world_calendar_filetime(c, &world().calendar, &now);
    const auto current = std::uint64_t { now.low } | (std::uint64_t { now.high } << 32);
    const auto days = std::bit_cast<std::int64_t>(current - employee->time_hired) / 864000000000LL;
    const std::array tenure { BfstSignedIntegerArgument { 19, fixed::narrow(days) } };
    model.tenure = text(c.messages.mail_language.ui_text, 952, tenure);
    model.thought_code = employee->mobile.thought_history.thought_script ? employee->mobile.thought_history.last_thought : MobileThoughtCode::none;
    model.thought = text(table("THOUGHTS.str"), static_cast<std::uint32_t>(model.thought_code));
    const auto thought = std::to_underlying(model.thought_code);
    model.thought_icon = thought ? thought - 1 : 999;
    constexpr std::array<std::int32_t, 9> help { 183, 185, 203, 205, 207, 206, 204, 208, 198 };
    model.thought_help = thought >= 22 && thought <= 30 ? help[static_cast<std::size_t>(thought - 22)] : -1;
    std::size_t count = 0;
    each_staff([&](const StaffRuntimeRecord&) {
        ++count;
    });
    model.can_navigate = count > 1;
    ++model.revision;
}

void WorldStaffUiRuntime::State::refresh_locator() {
    auto& model = view.locator;
    if (!model.open) {
        return;
    }
    model.available = available_roles();
    const auto* employee = live_staff(model.target);
    if (!employee) {
        model.target = 0;
        model.caption.clear();
        c.view.flags &= ~4096u;
        return;
    }
    const auto name = staff_name(*employee);
    const std::array argument { BfstWideStringArgument { 0, name } };
    model.caption = text(c.messages.mail_language.ui_text, 47, {}, argument);
    const auto pose = interpolate_mobile_entity_render_position(employee->mobile, 1.0f, true);
    model.position = pose.position;
    model.area = employee->area.bounds;
    ++model.revision;
}

void WorldStaffUiRuntime::State::refresh_activity() {
    auto& model = view.activity;
    if (!model.open) {
        return;
    }
    const auto* employee = live_staff(model.staff);
    if (!employee) {
        model.open = false;
        model.staff = 0;
        return;
    }
    std::array<float, 6> totals {};
    for (std::size_t i = 0; i < totals.size(); ++i) {
        double sum = 0;
        sum_staff_activity_ring_samples(employee->activity_rings[i], sum, 30);
        totals[i] = static_cast<float>(sum);
        const auto whole = static_cast<std::uint32_t>(fixed::truncate_integer(totals[i]));
        model.percentages[i + 1] = fixed::truncate_integer(static_cast<double>(whole) * 3.3333333);
    }
    double idle = 30.0;
    for (const auto value : totals) {
        idle -= value;
    }
    const auto whole = static_cast<std::uint32_t>(fixed::truncate_integer(idle));
    model.percentages[0] = fixed::truncate_integer(static_cast<double>(whole) * 3.3333333);
    ++model.revision;
}

void WorldStaffUiRuntime::State::refresh_selector() {
    auto& model = view.selector;
    if (!model.open) {
        return;
    }
    model.available = {};
    for (std::size_t i = 0; i < model.available.size(); ++i) {
        if (!model.excluded_roles[i]) {
            for (const auto id : role_ids(static_cast<StaffType>(i))) {
                if (!std::ranges::contains(model.excluded, id)) {
                    model.available[i] = true;
                }
            }
        }
    }
    if (!concrete_role(model.filter) || !model.available[role_index(model.filter)]) {
        const auto found = std::ranges::find(model.available, true);
        model.filter = found == model.available.end() ? StaffType::unsupported_index : static_cast<StaffType>(found - model.available.begin());
    }
    model.rows.clear();
    if (concrete_role(model.filter)) {
        for (const auto id : role_ids(model.filter)) {
            if (std::ranges::contains(model.excluded, id)) {
                continue;
            }
            auto value = row(staff(id));
            //Selector58E9C0 uses signed IDIV; ordinary staff list51FD50 uses
            //unsigned DIV.
            value.experience_meter = fixed::word(staff(id).experience << 10) / c.staff.recruitment.balance.all.MaxExperience;
            model.rows.push_back(std::move(value));
        }
    }
    sort_rows<StaffSelectorColumns>(model.rows, model.sort_key);
    if (std::ranges::none_of(model.rows, [&](const auto& item) {
            return item.staff == model.selected;
        })) {
        model.selected = 0;
    }
}

void WorldStaffUiRuntime::State::refresh_staff_room(bool rebuild) {
    if (!view.staff_room) {
        return;
    }
    (void)room(view.staff_room, ParkObjectCapabilities::chills_you_out);
    auto& model = c.ui.panels.staff_room;
    if (!rebuild) {
        //58D3B0/58DBC0 update only energy (column mask4) at the periodic callback.
        //Membership is maintained by World UI notifications58DA50/58DB30;
        //rebuilding and sorting here would move Energy-sorted rows mid-click.
        bool changed = false;
        for (auto& row : model.rows) {
            const auto* employee = live_staff(row.staff);
            if (!employee || &employee->base.header != row.record) {
                throw std::logic_error("Staff Room roster retained an expired employee");
            }
            const auto energy = percent_meter(employee->energy);
            changed |= row.energy_meter != energy;
            row.energy_meter = energy;
        }
        if (changed) {
            ++model.revision;
        }
        return;
    }
    model.rows.clear();
    each_staff([&](const StaffRuntimeRecord& employee) {
        if (employee.state == StaffState::resting && employee.rest_area == view.staff_room) {
            model.rows.push_back({ &employee.base.header, employee.base.header.id, staff_name(employee), text(c.messages.mail_language.staff_types, static_cast<std::uint32_t>(world_staff_type_for_used_thing(employee.base.header)) * 2), percent_meter(employee.energy) });
        }
    });
    const auto field = [&](const StaffRoomRosterRow& value) -> SortValue {
        if (model.sort_column == 0) {
            return std::u16string_view { value.name };
        }
        if (model.sort_column == 1) {
            return std::u16string_view { value.role };
        }
        if (model.sort_column == 2) {
            return value.energy_meter;
        }
        throw std::out_of_range("Staff Room sort column");
    };
    if (model.sort_column >= 0) {
        std::ranges::stable_sort(model.rows, [&](const auto& a, const auto& b) {
            return model.sort_direction == UiListSortDirection::ascending ? field(a) < field(b) : field(a) > field(b);
        });
    }
    ++model.revision;
}

void WorldStaffUiRuntime::State::retire(const ThingHeader& retiring) {
    const auto id = retiring.id;
    if (c.ui.panels.selected_record == &retiring) {
        c.ui.panels.selected_record = nullptr;
    }
    if (view.details.staff == id) {
        view.details.open = false;
        view.details.staff = 0;
    }
    if (view.activity.staff == id) {
        view.activity.open = false;
        view.activity.staff = 0;
    }
    if (view.wage.staff == id) {
        view.wage = {};
    }
    if (view.list.selected == id) {
        view.list.selected = 0;
        view.list.status.clear();
    }
    std::erase_if(view.list.rows, [=](const auto& row) {
        return row.staff == id;
    });
    ++view.list.revision;
    if (view.selector.selected == id) {
        view.selector.selected = 0;
    }
    std::erase(view.selector.excluded, id);
    std::erase_if(view.selector.rows, [=](const auto& row) {
        return row.staff == id;
    });
    if (view.locator.target == id) {
        view.locator.target = 0;
        view.locator.caption.clear();
        c.view.flags &= ~4096u;
        ++view.locator.revision;
    }
    remove_staff_room_roster_member(c, retiring);
    if (c.staff.interaction.training_picker.staff == id) {
        c.staff.interaction.training_picker = {};
    }
    for (auto& row : c.staff.interaction.training_pending) {
        if (row.staff == id) {
            row = {};
            if (c.ui.panels.training_room.active_row_count > 0) {
                --c.ui.panels.training_room.active_row_count;
            }
        }
    }
    for (auto& row : view.training.baseline) {
        if (row.staff == id) {
            row = {};
        }
    }
    training_marked.erase(id);
    if (view.staff_room == id) {
        view.staff_room = 0;
        c.ui.panels.staff_room.open = false;
        c.ui.panels.staff_room.rows.clear();
    }
    if (view.training.room == id) {
        for (const auto staff_id : training_marked) {
            if (auto* employee = live_staff(staff_id)) {
                training_marker(*employee, false);
            }
        }
        training_marked.clear();
        view.training = {};
        c.ui.panels.training_room = {};
        c.staff.interaction.training_pending = {};
    } else if (c.ui.panels.training_room.open) {
        refresh_training_room_rows_and_availability(c);
    }
    if (view.training.ability_picker_slot && !c.staff.interaction.training_pending.at(*view.training.ability_picker_slot).staff) {
        view.training.ability_picker_slot.reset();
    }
    if (retirement_observer) {
        retirement_observer(id);
    }
}

WorldStaffUiRuntime::WorldStaffUiRuntime(WorldRuntime& c, const std::filesystem::path& root) : state_(std::make_unique<State>(c, root)) {}

WorldStaffUiRuntime::~WorldStaffUiRuntime() {
    close_all();
    state_->c.staff.interaction.training_picker = {};
}

StaffUiState& WorldStaffUiRuntime::state() {
    return state_->view;
}

const StaffUiState& WorldStaffUiRuntime::state() const {
    return state_->view;
}

void WorldStaffUiRuntime::set_retirement_observer(std::function<void(ThingId)> observer) {
    state_->retirement_observer = std::move(observer);
}

void WorldStaffUiRuntime::refresh() {
    auto& s = *state_;
    s.refresh_hire();
    s.refresh_list();
    s.refresh_details();
    s.refresh_locator();
    s.refresh_activity();
    s.refresh_selector();
    s.refresh_staff_room();
    if (s.view.training.open) {
        refresh_training_room_rows_and_availability(s.c);
    }
}

void WorldStaffUiRuntime::close_all() {
    auto& s = *state_;
    close_hire();
    cancel_training_room();
    close_staff_room();
    close_locator();
    if (s.c.ui.panels.selected_record && s.c.ui.panels.selected_record->id == s.view.details.staff) {
        s.c.ui.panels.selected_record = nullptr;
    }
    s.view.list.open = s.view.details.open = s.view.finance.open = s.view.activity.open = s.view.selector.open = false;
    s.view.list.rows.clear();
    s.view.selector.rows.clear();
}

void WorldStaffUiRuntime::open_hire(StaffType role) {
    auto& s = *state_;
    if (s.c.mode.game == GameMode::code_3) {
        return; //Explicit53F840 Coaster Edit Mode gate.
    }
    if (role == StaffType::unsupported_index) {
        role = s.view.hire.filter;
    }
    if (role == StaffType::unsupported_index) {
        role = StaffType::mechanic;
    }
    (void)role_index(role);
    //Native257 rebuilds even when explicitly selecting the same category.
    //Clear the old category's rows before the first-row selection contract.
    s.view.hire.rows.clear();
    s.view.hire.selected_slot = -1;
    s.selected_candidate.reset();
    s.c.staff.recruitment.selected_candidate = -1;
    s.view.hire.filter = role;
    s.view.hire.open = true;
    s.refresh_hire();
}

void WorldStaffUiRuntime::open_list(StaffType role, ThingId selected) {
    auto& s = *state_;
    //The public sentinel represents the native opener's category -1.
    if (role != StaffType::unsupported_index) {
        (void)role_index(role);
        s.view.list.filter = role;
    }
    s.view.list.open = true;
    s.view.list.selected = selected;
    s.refresh_list();
}

void WorldStaffUiRuntime::set_name(ThingId id, std::u16string_view name) {
    auto& staff = state_->staff(id);
    //62B5C0 copies32 code units and unconditionally terminates the33rd.
    const auto length = std::min<std::size_t>(32, name.size());
    std::copy_n(name.begin(), length, staff.name.begin());
    std::fill(staff.name.begin() + static_cast<std::ptrdiff_t>(length), staff.name.end(), char16_t {});
    refresh();
}

void WorldStaffUiRuntime::set_salary(ThingId id, std::int32_t requested) {
    auto& s = *state_;
    auto& staff = s.staff(id);
    //62D160 has a minimum clamp, with NO maximum clamp. Recalculation is
    //conditional: unchanged salary must not award another happiness update.
    const auto value = std::max(requested, s.c.staff.recruitment.balance.all.MinWage);
    if (fixed::word(staff.salary) != value) {
        staff.salary = static_cast<std::uint32_t>(value);
        update_world_staff_wage_state(s.c, staff);
    }
}

void WorldStaffUiRuntime::apply_details_salary() {
    auto& details = state_->view.details;
    if (!details.open) {
        throw std::logic_error("Staff Details is closed");
    }
    set_salary(details.staff, details.staged_wage);
    state_->refresh_details();
}

void WorldStaffUiRuntime::begin_pickup(ThingId id) {
    start_staff_pickup_interaction(state_->c, state_->staff(id));
}

void WorldStaffUiRuntime::begin_patrol(ThingId id) {
    begin_staff_area_selection(state_->c, state_->staff(id));
}

void WorldStaffUiRuntime::clear_patrol(ThingId id) {
    reset_world_object_staff_area_bounds_to_full_map(state_->staff(id));
}

void WorldStaffUiRuntime::open_finance() {
    state_->view.finance.open = true;
}

void WorldStaffUiRuntime::set_finance_mode(StaffFinanceAdjustment mode) {
    if (mode < StaffFinanceAdjustment::amount || mode > StaffFinanceAdjustment::percent) {
        throw std::invalid_argument("Staff Finance adjustment mode");
    }
    state_->view.finance.mode = mode;
    state_->view.finance.values.fill(mode == StaffFinanceAdjustment::amount ? state_->c.staff.recruitment.balance.all.MinWage : 0);
}

void WorldStaffUiRuntime::apply_finance() {
    auto& s = *state_;
    const auto finance = s.view.finance;
    if (!finance.open) {
        throw std::logic_error("Staff Finance is closed");
    }
    std::vector<ThingId> ids;
    s.each_staff([&](const StaffRuntimeRecord& staff) {
        ids.push_back(staff.base.header.id);
    });
    for (const auto id : ids) {
        const auto& staff = s.staff(id);
        const auto index = role_index(world_staff_type_for_used_thing(staff.base.header));
        if (!finance.enabled[index]) {
            continue;
        }
        const auto current = fixed::word(staff.salary), requested = finance.values[index];
        const auto value = finance.mode == StaffFinanceAdjustment::amount ? requested : finance.mode == StaffFinanceAdjustment::add ? fixed::add(current, requested) : fixed::add(current, fixed::word(static_cast<std::uint32_t>(current) * static_cast<std::uint32_t>(requested)) / 100);
        set_salary(id, std::clamp(value, 0, 10000));
    }
    refresh();
}

void WorldStaffUiRuntime::sort_hire(std::int32_t key) {
    auto& model = state_->view.hire;
    sort_rows<StaffHireColumns>(model.rows, key);
    model.sort_key = key;
    ++model.revision;
}

void WorldStaffUiRuntime::sort_list(std::int32_t key) {
    auto& model = state_->view.list;
    sort_rows<StaffListColumns>(model.rows, key);
    model.sort_key = key;
    ++model.revision;
}

void WorldStaffUiRuntime::select_hire(std::size_t slot) {
    auto& s = *state_;
    if (!s.view.hire.open || std::ranges::none_of(s.view.hire.rows, [=](const auto& row) {
            return row.slot == slot;
        })) {
        throw std::out_of_range("Staff hire slot is not displayed");
    }
    const auto& candidate = s.world().staff_candidates.at(slot);
    if (!candidate.valid) {
        throw std::out_of_range("Staff candidate is no longer available");
    }
    s.view.hire.selected_slot = static_cast<std::int32_t>(slot);
    s.selected_candidate = candidate;
    s.c.staff.recruitment.selected_candidate = static_cast<std::int32_t>(slot);
    ++s.view.hire.revision;
}

void WorldStaffUiRuntime::close_hire() {
    state_->view.hire.open = false;
    state_->view.hire.selected_slot = -1;
    state_->selected_candidate.reset();
    state_->c.staff.recruitment.selected_candidate = -1;
}

bool WorldStaffUiRuntime::hire_selected() {
    auto& s = *state_;
    if (!s.view.hire.open || s.view.hire.selected_slot < 0) {
        return false;
    }
    const auto slot = static_cast<std::size_t>(s.view.hire.selected_slot);
    if (!s.selected_candidate || !s.world().staff_candidates.at(slot).valid || !matches_staff_hire_snapshot(s.world().staff_candidates.at(slot), *s.selected_candidate)) {
        return false;
    }
    const bool hired = hire_staff_from_prefix_slot(s.c, s.world(), slot);
    if (hired) {
        close_hire();
    }
    return hired;
}

void WorldStaffUiRuntime::select_list(ThingId id) {
    auto& s = *state_;
    if (id && std::ranges::none_of(s.view.list.rows, [=](const auto& row) {
            return row.staff == id;
        })) {
        throw std::out_of_range("Staff list selection is not displayed");
    }
    s.view.list.selected = id;
    s.refresh_list();
}

void WorldStaffUiRuntime::open_details(ThingId id) {
    auto& s = *state_;
    const auto& staff = s.staff(id);
    s.view.details.open = true;
    s.view.details.staff = id;
    s.view.details.staged_wage = fixed::word(staff.salary);
    s.c.ui.panels.selected_record = &staff.base.header;
    s.refresh_details();
}

void WorldStaffUiRuntime::open_activity(ThingId id) {
    (void)state_->staff(id);
    state_->view.activity.open = true;
    state_->view.activity.staff = id;
    state_->refresh_activity();
}

void WorldStaffUiRuntime::navigate_details(bool previous) {
    auto& s = *state_;
    if (!s.view.details.open) {
        throw std::logic_error("Staff Details is closed");
    }
    std::vector<ThingId> ring;
    for (std::int32_t role = 0; role < 6; ++role) {
        const auto ids = s.role_ids(static_cast<StaffType>(role));
        ring.insert(ring.end(), ids.begin(), ids.end());
    }
    const auto found = std::ranges::find(ring, s.view.details.staff);
    if (found == ring.end()) {
        throw std::logic_error("Staff Details selected employee is not in the role chains");
    }
    const auto index = static_cast<std::size_t>(found - ring.begin());
    open_details(ring[(index + ring.size() + (previous ? ring.size() - 1 : 1)) % ring.size()]);
}

void WorldStaffUiRuntime::fire(ThingId id) {
    auto& s = *state_;
    auto& employee = s.staff(id);
    const auto role = world_staff_type_for_used_thing(employee.base.header);
    if (classify_staff_activity(employee) == StaffActivityStatus::on_strike) {
        std::size_t strikers = 0;
        for (const auto other : s.role_ids(role)) {
            if (classify_staff_activity(s.staff(other)) == StaffActivityStatus::on_strike) {
                ++strikers;
            }
        }
        if (strikers == 1) {
            auto& strike = s.record<StaffHqRecord>(s.world().saved.staff_hq, ThingKind::staff_hq).strikes[role_index(role)];
            strike.stage = StaffStrikeStage::none;
            strike.strike_active = 0;
        }
    }
    initialize_world_staff_interaction(s.c);
    const auto kind = world_staff_interaction_kind(s.c);
    if (kind == WorldInteractionHandlerKind::held_staff || kind == WorldInteractionHandlerKind::staff_area) {
        cancel_world_staff_interaction(s.c);
    }
    (void)charge_world_bank_account_expense(s.c, &s.record<BankRecord>(s.world().saved.bank_account, ThingKind::bank_account), fixed::word(employee.salary));
    s.record<ParkAnalyserState>(s.world().saved.park_analyser, ThingKind::park_analyser).long_histories[4].temporary += employee.salary;
    (void)mark_tag_messages_by_format_word_for_deletion(s.c, &s.record<TagSystemRecord>(s.world().saved.tag_system, ThingKind::tag_system), id);
    (void)create_runtime_template_at_mobile_entity_resource_position(s.c, employee.mobile, 19, 0, 0);
    (void)retire_world_used_thing(s.c, &employee.base.header);
    refresh();
}

void WorldStaffUiRuntime::open_locator(ThingId id) {
    auto& s = *state_;
    if (id) {
        (void)s.staff(id);
    }
    s.view.locator.open = true;
    s.view.locator.target = id;
    s.view.locator.filter = id ? world_staff_type_for_used_thing(s.staff(id).base.header) : StaffType::unsupported_index;
    if (id) {
        s.c.view.flags |= 4096u;
    }
    s.refresh_locator();
}

void WorldStaffUiRuntime::cycle_locator(StaffType role, bool previous) {
    auto& s = *state_;
    auto& model = s.view.locator;
    if (!model.open) {
        throw std::logic_error("Staff Locator is closed");
    }
    const auto ids = s.role_ids(role);
    if (role != model.filter) {
        model.target = 0;
    }
    model.filter = role;
    const auto current = std::ranges::find(ids, model.target);
    if (ids.empty()) {
        model.target = 0;
    } else if (current == ids.end()) {
        model.target = previous ? ids.back() : ids.front();
    } else {
        const auto index = static_cast<std::size_t>(current - ids.begin());
        model.target = ids[(index + (previous ? ids.size() - 1 : 1)) % ids.size()];
    }
    if (model.target) {
        s.c.view.flags |= 4096u;
    } else {
        s.c.view.flags &= ~4096u;
    }
    s.refresh_locator();
}

void WorldStaffUiRuntime::close_locator() {
    state_->view.locator.open = false;
    state_->view.locator.target = 0;
    state_->view.locator.caption.clear();
    state_->c.view.flags &= ~4096u;
}

void WorldStaffUiRuntime::open_staff_room(ThingId id) {
    auto& s = *state_;
    auto& room = s.room(id, ParkObjectCapabilities::chills_you_out);
    s.view.staff_room = id;
    s.c.ui.panels.staff_room.open = true;
    s.c.ui.panels.selected_record = &room.base.header;
    s.refresh_staff_room(true);
}

void WorldStaffUiRuntime::close_staff_room() {
    auto& s = *state_;
    if (s.c.ui.panels.selected_record && s.c.ui.panels.selected_record->id == s.view.staff_room) {
        s.c.ui.panels.selected_record = nullptr;
    }
    s.view.staff_room = 0;
    s.c.ui.panels.staff_room.open = false;
    s.c.ui.panels.staff_room.rows.clear();
}

void WorldStaffUiRuntime::open_selector(std::span<const ThingId> excluded, std::span<const StaffType> excluded_roles) {
    auto& s = *state_;
    auto& model = s.view.selector;
    model.open = true;
    model.selected = 0;
    model.excluded.assign(excluded.begin(), excluded.end());
    model.excluded_roles = {};
    for (const auto role : excluded_roles) {
        model.excluded_roles[role_index(role)] = true;
    }
    s.refresh_selector();
}

void WorldStaffUiRuntime::filter_selector(StaffType role) {
    (void)role_index(role);
    state_->view.selector.filter = role;
    state_->view.selector.selected = 0;
    state_->refresh_selector();
}

void WorldStaffUiRuntime::sort_selector(std::int32_t key) {
    auto& model = state_->view.selector;
    sort_rows<StaffSelectorColumns>(model.rows, key);
    model.sort_key = key;
}

void WorldStaffUiRuntime::open_training_room(ThingId id) {
    auto& s = *state_;
    (void)s.room(id, ParkObjectCapabilities::training_room);
    cancel_training_room();
    s.view.training.open = true;
    s.view.training.room = id;
    auto& display = s.c.ui.panels.training_room;
    display = {};
    display.open = true;
    s.c.staff.interaction.training_pending = {};
    s.each_staff([&](const StaffRuntimeRecord& employee) {
        if (employee.state != StaffState::training && !training_travel(employee)) {
            return;
        }
        if (display.active_row_count >= 3) {
            throw std::logic_error("More than three employees have native training assignments");
        }
        s.c.staff.interaction.training_pending[static_cast<std::size_t>(display.active_row_count++)] = { employee.base.header.id, training_travel(employee) ? employee.going_to_learn : employee.currently_learning };
    });
    s.view.training.baseline = s.c.staff.interaction.training_pending;
    refresh_training_room_rows_and_availability(s.c);
}

std::vector<StaffTrainingAbilityRow> WorldStaffUiRuntime::training_abilities(ThingId id) const {
    auto& s = *state_;
    const auto& employee = s.staff(id);
    std::vector<StaffTrainingAbilityRow> rows;
    for (std::int32_t value = 0; value < 15; ++value) {
        const auto ability = static_cast<ThemeAbility>(value);
        if (can_world_staff_learn_theme_ability(s.c, employee, ability)) {
            rows.push_back({ ability, s.text(s.c.messages.mail_language.abilities, static_cast<std::uint32_t>(value)) });
        }
    }
    std::ranges::stable_sort(rows, {}, &StaffTrainingAbilityRow::name);
    return rows;
}

void WorldStaffUiRuntime::begin_training_ability_picker(std::size_t slot) {
    auto& s = *state_;
    if (!s.view.training.open) {
        throw std::logic_error("Training Room is closed");
    }
    close_training_ability_picker();
    auto& employee = s.staff(s.c.staff.interaction.training_pending.at(slot).staff);
    s.view.training.ability_picker_slot = slot;
    training_marker(employee, true);
    s.training_marked.insert(employee.base.header.id);
}

void WorldStaffUiRuntime::close_training_ability_picker(bool restore) {
    auto& s = *state_;
    auto& model = s.view.training;
    if (model.ability_picker_slot) {
        const auto id = s.c.staff.interaction.training_pending.at(*model.ability_picker_slot).staff;
        if (restore) {
            if (auto* employee = s.live_staff(id)) {
                training_marker(*employee, false);
            }
            s.training_marked.erase(id);
        }
        model.ability_picker_slot.reset();
    }
}

bool WorldStaffUiRuntime::stage_training_staff(ThingId id) {
    auto& s = *state_;
    const auto& employee = s.staff(id);
    if (!s.view.training.open) {
        throw std::logic_error("Training Room is closed");
    }
    const auto role = world_staff_type_for_used_thing(employee.base.header);
    if (role == StaffType::entertainer || role == StaffType::researcher) {
        return false;
    }
    auto& rows = s.c.staff.interaction.training_pending;
    if (std::ranges::any_of(rows, [=](const auto& row) {
            return row.staff == id;
        })) {
        return false;
    }
    const auto empty = std::ranges::find_if(rows, [](const auto& row) {
        return !row.staff;
    });
    if (empty == rows.end()) {
        return false;
    }
    *empty = { id, ThemeAbility::none };
    ++s.c.ui.panels.training_room.active_row_count;
    refresh_training_room_rows_and_availability(s.c);
    return true;
}

StaffTrainingSelectionResult WorldStaffUiRuntime::stage_training_ability(std::size_t slot, ThemeAbility ability) {
    auto& s = *state_;
    if (!s.view.training.open) {
        throw std::logic_error("Training Room is closed");
    }
    auto& pending = s.c.staff.interaction.training_pending.at(slot);
    const auto& employee = s.staff(pending.staff);
    const auto choices = training_abilities(pending.staff);
    if (std::ranges::none_of(choices, [=](const auto& row) {
            return row.ability == ability;
        })) {
        throw std::out_of_range("Ability is not in staged Training Room picker");
    }
    s.view.training.message_row = 0;
    if (ability == employee.first_known_ability) {
        s.view.training.message_row = 714;
        close_training_ability_picker(true);
        return StaffTrainingSelectionResult::already_known;
    }
    const bool changed = pending.ability != ability;
    pending.ability = ability;
    close_training_ability_picker(!changed);
    refresh_training_room_rows_and_availability(s.c);
    return StaffTrainingSelectionResult::started;
}

void WorldStaffUiRuntime::remove_training_staff(std::size_t slot) {
    auto& s = *state_;
    if (!s.view.training.open) {
        throw std::logic_error("Training Room is closed");
    }
    auto& pending = s.c.staff.interaction.training_pending.at(slot);
    if (!pending.staff) {
        return;
    }
    auto& employee = s.staff(pending.staff);
    training_marker(employee, true);
    s.training_marked.insert(pending.staff);
    if (s.view.training.ability_picker_slot == slot) {
        s.view.training.ability_picker_slot.reset();
    }
    pending = {};
    --s.c.ui.panels.training_room.active_row_count;
    refresh_training_room_rows_and_availability(s.c);
}

void WorldStaffUiRuntime::commit_training_room() {
    auto& s = *state_;
    if (!s.view.training.open) {
        throw std::logic_error("Training Room is closed");
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const auto saved = s.view.training.baseline[i], pending = s.c.staff.interaction.training_pending[i];
        if (saved.staff && saved.staff != pending.staff) {
            if (auto* employee = s.live_staff(saved.staff)) {
                training_marker(*employee, false);
                cancel_staff_training_request(s.c, *employee);
            }
        }
        if (!pending.staff) {
            continue;
        }
        auto& employee = s.staff(pending.staff);
        if (employee.currently_learning == pending.ability) {
            continue;
        }
        if (classify_staff_activity(employee) == StaffActivityStatus::training) {
            if (pending.ability == ThemeAbility::none) {
                cancel_staff_training_request(s.c, employee);
            } else if (employee.experience >= 10000u) {
                employee.currently_learning = pending.ability;
                employee.ability_progress = 0;
            }
        } else {
            employee.going_to_learn = pending.ability;
            enter_world_staff_state(s.c, employee, StaffState::walking_to_training_room);
        }
    }
    cancel_training_room();
}

void WorldStaffUiRuntime::cancel_training_room() {
    auto& s = *state_;
    if (!s.view.training.open) {
        return;
    }
    for (const auto& row : s.c.staff.interaction.training_pending) {
        if (auto* employee = s.live_staff(row.staff)) {
            training_marker(*employee, false);
        }
    }
    for (const auto& row : s.view.training.baseline) {
        if (auto* employee = s.live_staff(row.staff)) {
            training_marker(*employee, false);
        }
    }
    for (const auto id : s.training_marked) {
        if (auto* employee = s.live_staff(id)) {
            training_marker(*employee, false);
        }
    }
    s.training_marked.clear();
    s.view.training = {};
    s.c.ui.panels.training_room = {};
    s.c.staff.interaction.training_pending = {};
    s.view.selector.open = false;
    s.view.selector.rows.clear();
}

void WorldStaffUiRuntime::begin_wage(ThingId id, std::int32_t demand) {
    (void)state_->staff(id);
    state_->view.wage = { true, false, id, demand, demand, StaffWageOutcome::rejected, 0 };
}

void WorldStaffUiRuntime::set_wage_offer(std::int32_t offered) {
    auto& wage = state_->view.wage;
    if (!wage.open || wage.showing_result) {
        throw std::logic_error("Staff wage demand is not open");
    }
    wage.offered_raise = offered;
}

StaffWageOutcome WorldStaffUiRuntime::show_wage_result() {
    auto& s = *state_;
    auto& wage = s.view.wage;
    if (!wage.open) {
        throw std::logic_error("Staff wage negotiation is not open");
    }
    const auto salary = fixed::word(s.staff(wage.staff).salary);
    const auto offered = fixed::add(salary, wage.offered_raise), demanded = fixed::add(salary, wage.demanded_raise);
    wage.showing_result = true;
    if (offered >= demanded) {
        wage.outcome = StaffWageOutcome::demand_met;
        wage.response_row = 255;
    } else if (static_cast<double>(offered) / demanded * 100.0 >= 100 - s.c.staff.recruitment.balance.all.PercentageForBadWageDecision) {
        wage.outcome = StaffWageOutcome::partial_offer_accepted;
        wage.response_row = 256;
    } else {
        wage.outcome = StaffWageOutcome::rejected;
        wage.response_row = 257;
    }
    return wage.outcome;
}

void WorldStaffUiRuntime::resolve_wage_result(StaffRejectedWageChoice choice) {
    auto& s = *state_;
    const auto wage = s.view.wage;
    if (!wage.open || !wage.showing_result) {
        return;
    }
    if (wage.outcome != StaffWageOutcome::rejected || choice == StaffRejectedWageChoice::accept_full_demand) {
        const auto increment = wage.outcome == StaffWageOutcome::rejected ? wage.demanded_raise : wage.offered_raise;
        const std::array<std::int32_t, 1> keys { 144 };
        (void)mark_tag_messages_by_key_set_and_format_word_for_deletion(s.c, &s.record<TagSystemRecord>(s.world().saved.tag_system, ThingKind::tag_system), keys, wage.staff);
        set_salary(wage.staff, fixed::add(fixed::word(s.staff(wage.staff).salary), increment));
    } else if (choice == StaffRejectedWageChoice::dismiss) {
        fire(wage.staff);
    }
    s.view.wage = {};
    refresh();
}

StaffTrainingBudgetUiState WorldStaffUiRuntime::training_budget() const {
    auto& s = *state_;
    StaffTrainingBudgetUiState result;
    const auto& training = s.record<StaffHqRecord>(s.world().saved.staff_hq, ThingKind::staff_hq).training;
    result.have_ever_trained = training.have_ever_trained != 0;
    for (std::size_t i = 0; i < 6; ++i) {
        result.budgets[i] = fixed::word(training.budget[i]);
        result.total = fixed::add(result.total, result.budgets[i]);
        std::uint32_t sum = 0, count = 0;
        s.each_staff([&](const StaffRuntimeRecord& employee) {
            if (world_staff_type_for_used_thing(employee.base.header) == static_cast<StaffType>(i)) {
                sum += employee.experience;
                ++count;
            }
        });
        if (count) {
            const auto denominator = fixed::word(count * static_cast<std::uint32_t>(s.c.staff.recruitment.balance.all.MaxExperience));
            if (!denominator) {
                throw std::domain_error("Staff budget average denominator is zero");
            }
            const auto average = fixed::word(sum * 100u) / denominator;
            result.experience_meters[i] = fixed::word(static_cast<std::uint32_t>(average) << 10) / 100;
        }
    }
    const auto& analyser = s.record<ParkAnalyserState>(s.world().saved.park_analyser, ThingKind::park_analyser);
    const auto latest = [](const auto& history) -> std::int32_t {
        if (history.num_entries <= 0) {
            return 0;
        }
        auto index = history.current_entry;
        if (index < 0) {
            if (!history.wrapped) {
                return 0;
            }
            index += history.num_entries;
        }
        if (index < 0 || static_cast<std::size_t>(index) >= history.data.size()) {
            throw std::out_of_range("Staff budget financial history index");
        }
        return fixed::word(history.data[static_cast<std::size_t>(index)]);
    };
    result.history_6 = latest(analyser.long_histories[6]);
    result.history_4 = latest(analyser.long_histories[4]);
    result.history_3_minus_4 = fixed::subtract(latest(analyser.long_histories[3]), result.history_4);
    result.remainder = fixed::subtract(fixed::subtract(result.history_6, result.history_3_minus_4), result.total);
    return result;
}

void WorldStaffUiRuntime::set_training_budget(StaffType role, std::int32_t value) {
    auto& s = *state_;
    auto& training = s.record<StaffHqRecord>(s.world().saved.staff_hq, ThingKind::staff_hq).training;
    const auto budget = std::max(value, 0);
    training.budget.at(role_index(role)) = static_cast<std::uint32_t>(budget);
    if (budget) {
        training.have_ever_trained = 1;
    }
}

StaffContextUiState WorldStaffUiRuntime::context(ThingId id) const {
    auto& s = *state_;
    const auto& employee = s.staff(id);
    StaffContextUiState result;
    result.role = world_staff_type_for_used_thing(employee.base.header);
    result.name = staff_name(employee);
    const auto activity = classify_staff_activity(employee);
    result.on_strike = activity == StaffActivityStatus::on_strike;
    const bool patrol = result.role != StaffType::researcher && !result.on_strike;
    const bool custom = custom_patrol(employee);
    result.can_set_patrol = patrol && !custom;
    result.can_edit_patrol = result.can_clear_patrol = patrol && custom;
    bool training_room = false;
    for (auto room_id = s.world().saved.first_park_object; room_id;) {
        const auto& room = s.record<ParkObjectRuntimeView>(room_id, ThingKind::park_object);
        if ((std::to_underlying(room.capabilities) & std::to_underlying(ParkObjectCapabilities::training_room)) != 0) {
            training_room = true;
            break;
        }
        room_id = room.next_park_object;
    }
    result.can_train = !result.on_strike && training_room;
    result.can_pickup = is_world_staff_interaction_eligible(s.c, employee);
    result.can_hire = count_world_staff_candidates(s.world(), result.role) > 0;
    return result;
}
} //namespace simcoaster
