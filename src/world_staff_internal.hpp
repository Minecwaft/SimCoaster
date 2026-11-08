#pragma once
#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_domains_fwd.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster::staff_detail {
inline World& world(StaffWorld& c) {
    return require_bound(c.simulation.current, "staff current World");
}

inline World& primary(StaffWorld& c) {
    return require_bound(c.referenced_world(), "staff primary World");
}

template <class T>
T& record(StaffWorld& c, ThingId id) {
    auto* value = c.entities.at(id).payload;
    if (!value)
        throw std::logic_error("staff record is missing");
    return *reinterpret_cast<T*>(value);
}

inline ParkObjectRuntimeView& object(StaffWorld& c, ThingId id) {
    return record<ParkObjectRuntimeView>(c, id);
}

inline const AllStaffParameters& balance(const StaffQuery& c) {
    return c.staff.recruitment.balance.all;
}

inline bool has(const ParkObjectRuntimeView& o, ParkObjectCapabilities flag) {
    return (std::to_underlying(o.capabilities) & std::to_underlying(flag)) != 0;
}

inline CellId cell(const MapObjectBase& base) {
    return static_cast<CellId>(((base.y >> 8) << 7) + (base.x >> 8) + 1u);
}

inline std::array<std::uint16_t, 2> center(CellId id) {
    const auto v = static_cast<std::uint32_t>(id) - 1u;
    return { static_cast<std::uint16_t>(((v & 127u) << 8) | 128u), static_cast<std::uint16_t>((v >> 7 << 8) | 128u) };
}

inline bool route(StaffWorld& c, StaffRuntimeRecord& s, std::uint16_t x, std::uint16_t y) {
    return set_mobile_entity_destination_and_path(c, s.base, s.mobile, x, y);
}

inline bool route(StaffWorld& c, StaffRuntimeRecord& s, CellId id) {
    const auto p = center(id);
    return route(c, s, p[0], p[1]);
}

inline NavigationResult navigate(StaffWorld& c, StaffRuntimeRecord& s) {
    return update_mobile_entity_navigation(c, s.base, s.mobile);
}

inline std::uint32_t distance(const MapObjectBase& a, const MapObjectBase& b) {
    const auto dx = static_cast<int>(a.x >> 8) - static_cast<int>(b.x >> 8),
               dy = static_cast<int>(a.y >> 8) - static_cast<int>(b.y >> 8);
    return static_cast<std::uint32_t>(dx * dx + dy * dy);
}

inline float percentage(double value) {
    return value <= 100.0 ? (value >= 0.0 ? static_cast<float>(value) : 0.0f) : 100.0f;
}

inline std::int64_t integer(double value) {
    const auto v = std::trunc(value);
    if (!std::isfinite(v) || v < -std::ldexp(1.0, 63) || v >= std::ldexp(1.0, 63))
        throw std::domain_error("nonrepresentable staff integer");
    return static_cast<std::int64_t>(v);
}

inline std::uint8_t byte(float value) {
    return static_cast<std::uint8_t>(integer(value));
}

inline std::uint32_t random(StaffWorld& c) {
    return static_cast<std::uint32_t>(next_world_prng_value(world(c)));
}

inline std::uint32_t bounded_random(StaffWorld& c, std::uint32_t size) {
    if (!size)
        throw std::domain_error("zero staff random range");
    return random(c) % size;
}

inline void thought(VisitorWorld& c, StaffRuntimeRecord& s, MobileThoughtCode code) {
    world_operations::set_mobile_entity_thought_bubble(c, &s.mobile.thought_history, code, false);
}

inline void clear_thought(StaffWorld& c, StaffRuntimeRecord& s) {
    release_and_clear_mobile_entity_thought_script_handle(c, s.mobile.thought_history);
}

inline void idle_animation(StaffRuntimeRecord& s) {
    set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::normal);
    set_sprite_animation_next(s.mobile.animation, 3);
}

inline void animation(ScriptWorld& c, StaffRuntimeRecord& s, std::uint32_t id) {
    auto& a = s.mobile.animation;
    if (a.sprite_script)
        set_sprite_animation_next(a, id);
    else
        set_world_sprite_animation_resource(c,
                                            a,
                                            id,
                                            static_cast<std::int32_t>(s.mobile.sprite_category),
                                            static_cast<std::int32_t>(s.mobile.sprite_id));
}

inline void notify(StaffWorld& c, WorldEvent event) {
    dispatch_world_event_to_subscribers(c,
                                        get_world_event_subscription_registry(c),
                                        &event,
                                        deliver_world_record_event,
                                        nullptr);
}

inline auto* script(AdvisorWorld& c, ParkObjectRuntimeView& o) {
    return world_operations::find_script(c, o.script_id);
}

inline ParkObjectRuntimeView* live_room(StaffWorld& c, ThingId id, ParkObjectCapabilities capability) {
    if (!id || id >= c.entities.size())
        return nullptr;
    auto* value = c.entities[id].payload;
    if (!value || value->kind != ThingKind::park_object || value->retirement_pending)
        return nullptr;
    auto& room = *reinterpret_cast<ParkObjectRuntimeView*>(value);
    return has(room, capability) ? &room : nullptr;
}

//Release only actual occupancy, while the room's RSSE state still exists.
//Does not allocate a sprite or select another task for a retiring employee.
void detach_staff_room_occupancy(StaffWorld&, StaffRuntimeRecord&, ThingId removed_training_room = 0);
void restore_cancelled_training_availability(StaffWorld&, StaffRuntimeRecord&);

inline ThingId first_training_room(StaffWorld& c) {
    for (auto id = primary(c).saved.first_park_object; id;) {
        auto& o = object(c, id);
        if (has(o, ParkObjectCapabilities::training_room))
            return id;
        id = o.next_park_object;
    }
    return 0;
}

inline StaffHqRecord& hq(StaffWorld& c) {
    return record<StaffHqRecord>(c, world(c).saved.staff_hq);
}

inline bool striking(StaffWorld& c, const StaffRuntimeRecord& s) {
    return hq(c).strikes.at(static_cast<std::size_t>(world_staff_type_for_used_thing(s.base.header))).strike_active
        != 0;
}

void post_training_mail(StaffWorld&, StaffRuntimeRecord&, bool completed, ThemeAbility);
void post_staff_mail(StaffWorld&, StaffRuntimeRecord&, CompanyEmailMessageKind, std::uint32_t);
void queue_action(StaffWorld&, std::int32_t, bool use_primary = true);
void start_task_after_idle_delay(StaffWorld&, StaffRuntimeRecord&);
void restore_saved_state(StaffWorld&, StaffRuntimeRecord&);
void arrive_repair(StaffWorld&, MechanicRuntimeRecord&);
void repair_tick(StaffWorld&, MechanicRuntimeRecord&);
ThingId claim_owner(StaffWorld&, ParkObjectRuntimeView&);
ThingId select_staff_service_target(StaffWorld&, StaffRuntimeRecord&, ParkObjectCapabilities);
void reset_position(StaffWorld&, MapObjectBase&, MobileEntityState&, std::uint16_t, std::uint16_t);

inline std::int32_t
interpolate(const StaffQuery& c, const StaffRuntimeRecord& s, std::int32_t minimum, std::int32_t maximum) {
    const auto limit = balance(c).MaxExperience, value = fixed::word(s.experience);
    if (value > limit)
        return maximum;
    if (value < 0)
        return minimum;
    if (!limit)
        throw std::domain_error("zero maximum staff experience");
    const auto fraction = fixed::word(s.experience << 14) / limit;
    const auto sum = static_cast<std::uint32_t>(maximum) * static_cast<std::uint32_t>(fraction)
                   + static_cast<std::uint32_t>(minimum) * (16384u - static_cast<std::uint32_t>(fraction));
    return fixed::word(sum) / 16384;
}

inline bool task_expired(StaffWorld& c, const StaffRuntimeRecord& s, std::uint32_t started) {
    return world(c).saved.game_tick > static_cast<std::uint32_t>(calculate_staff_work_value(c, &s)) + started;
}

void face_cell_direction(StaffWorld&, StaffRuntimeRecord&);
void release_escorted_person(StaffWorld&, PersonRuntimeView&, CellId);

inline ThingId& head(World& w, StaffType role) {
    return match (role) -> ThingId& {
        case StaffType::handyman    => w.saved.first_handyman;
        case StaffType::mechanic    => w.saved.first_mechanic;
        case StaffType::entertainer => w.saved.first_entertainer;
        case StaffType::guard       => w.saved.first_guard;
        case StaffType::researcher  => w.saved.first_researcher;
        case StaffType::gardener    => w.saved.first_gardener;
        case _                      => throw std::out_of_range("staff roster role");
    };
}

inline void claim(StaffWorld& c, ParkObjectRuntimeView& o, const StaffRuntimeRecord& s) {
    o.assigned_staff_member = s.base.header.id;
    o.time_marked_for_maintenance = world(c).saved.game_tick;
}

inline void release_claim(StaffWorld& c, ParkObjectRuntimeView& o) {
    if (o.assigned_staff_member)
        enter_world_staff_state(c, record<StaffRuntimeRecord>(c, o.assigned_staff_member), StaffState::idle);
    o.assigned_staff_member = 0;
    o.time_marked_for_maintenance = 0;
}

inline std::uint16_t speed(const StaffQuery& c, const StaffRuntimeRecord& s, std::uint16_t minimum) {
    return static_cast<std::uint16_t>(interpolate(c, s, minimum, fixed::add(minimum, balance(c).MaxSpeedDifference)));
}
} //namespace simcoaster::staff_detail
