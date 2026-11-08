#include "world_person_use_internal.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_mesh_runtime.hpp"

namespace simcoaster {
namespace {
template <class Ring>
void finish_sample(Ring& ring) {
    ring.current_entry = fixed::add(ring.current_entry, 1);
    if (ring.current_entry >= ring.num_entries) {
        ring.current_entry = 0;
        ring.wrapped = 1;
    }
    if (ring.current_entry < 0)
        throw std::out_of_range("negative object history index");
    ring.data.at(static_cast<std::size_t>(ring.current_entry)) = ring.temporary;
    ring.temporary = 0;
}

std::uint32_t previous_cost(const ParkObjectRuntimeView& object) {
    if (!object.history)
        return 0;
    const auto& ring = object.history->dword_history_144[1];
    if (ring.num_entries <= 0 || (ring.current_entry < 0 && !ring.wrapped))
        return 0;
    const auto index = ring.current_entry < 0 ? ring.num_entries + ring.current_entry : ring.current_entry;
    return ring.data.at(static_cast<std::size_t>(index));
}

void weather_change(AttractionWorld& c, ParkObjectRuntimeView& object) {
    using namespace person_use;
    const auto& weather = record<WeatherRecord>(c, person_detail::primary_world(c).saved.weather);
    const auto& rules = get_park_object_balance(c, object).WeatherModifier;
    const auto closed = match (weather.type) -> std::int32_t {
        case WeatherCategory::heatwave => rules.ClosedDuringHeatWave;
        case WeatherCategory::rain     => rules.ClosedDuringRain;
        case WeatherCategory::storm    => rules.ClosedDuringStorm;
        case WeatherCategory::snow     => rules.ClosedDuringSnow;
        case _                         => 0;
    };
    if (object.closed_due_to_weather) {
        if (closed != 1) {
            (void)open_park_object(c, object);
            object.closed_due_to_weather = 0;
        }
    } else if (closed == 1) {
        //Weather closure neither drains queues nor broadcasts blocked-exit20.
        object.can_load = 0;
        object.person_being_loaded = 0;
        world_operations::set_script_slot(c, world_operations::find_script(c, object.script_id), 6, 1);
        world_operations::select_park_object_mesh_animation_variant(c,
                                                                    object.mesh_instance_id,
                                                                    ParkObjectMeshVariant::closed);
        object.closed_due_to_weather = 1;
    }
}
} //namespace

bool set_park_object_promoted_attraction(AttractionWorld& c, ParkObjectRuntimeView& object, ThingId promoted) {
    const auto* definition = get_park_object_definition(c, &object);
    object.promoted_attraction = promoted;
    const auto& source = promoted ? person_detail::park_object(c, promoted) : object;
    const auto component = [](const auto& value, bool first) {
        const auto end = std::ranges::find(value, char16_t{});
        std::u16string result{ value.begin(), end };
        if (first && result.ends_with(u"--"))
            result.pop_back();
        if (result.size() > 14)
            result.resize(14);
        return result;
    };
    const auto first = component(source.name_a, true), second = component(source.name_b, false);
    if (!definition || !object.mesh_instance_id)
        return false;
    if (!c.resources.meshes)
        throw std::logic_error("object sign needs mesh runtime");
    return c.resources.meshes->set_object_sign_caption(c, object, first, second);
}

void refresh_info_desk_signs_for_renamed_object(AttractionWorld& c, ThingId renamed) {
    std::size_t visited = 0;
    for (auto* row = c.entities.active_head; row; row = row->active_next) {
        if (++visited >= c.entities.size())
            throw std::length_error("cyclic info-desk sign list");
        auto* payload = row->payload;
        if (!payload || payload->kind != ThingKind::park_object)
            continue;
        auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(payload);
        const auto* d = get_park_object_definition(c, &object);
        if (d && d->balance && d->balance->UsageInfo.IsInfoDesk && object.promoted_attraction == renamed)
            (void)set_park_object_promoted_attraction(c, object, renamed);
    }
}

void handle_park_object_event(AttractionWorld& c, ParkObjectRuntimeView& object, const WorldEvent& event) {
    using namespace person_use;
    if (event.type() == 33) {
        const auto id = event.get<RetirementEvent>().retired_id;
        //Backlinks belong to this object even before it first opens. Retirement
        //owns the departing actor's state/resources; entering idle here can
        //recreate a hidden sprite or release room occupancy ahead of cleanup.
        if (object.assigned_staff_member == id) {
            object.assigned_staff_member = 0;
            object.time_marked_for_maintenance = 0;
        }
        //Disarm actor-owned pranks before the used-thing slot can be reused.
        if (object.booby_trap_owner == id)
            object.booby_trap_owner = 0;
        if (object.promoted_attraction == id
            && person_detail::primary_world(c).saved.state != WorldState::clearing
            && get_park_object_balance(c, object).UsageInfo.IsInfoDesk)
            (void)set_park_object_promoted_attraction(c, object, 0);
        return;
    }
    if (event.type() == 1)
        weather_change(c, object);
    if (!object.has_been_opened)
        return;
    match (event.type()) {
        case 11 => {
            if (object.history)
                for (const auto index : std::array{ 2, 3, 4, 0, 1, 5, 8, 6, 7, 9, 10, 11, 12 })
                    finish_sample(object.history->dword_history_30[index]);
        }
        case 12 => {
            const auto running =
                object.state == ParkObjectState::broken_down || object.state == ParkObjectState::condemned
                    ? 0u
                    : object.running_cost;
            //Charge the PREVIOUS completed expense sample, before publishing this
            //month's temporary expenses. This one-month delay is native behavior.
            const auto amount = previous_cost(object) + running;
            object.been_around_at_least_a_month = 1;
            if (object.history) {
                auto& h = object.history->dword_history_144;
                h[3].temporary = running;
                h[2].temporary = h[1].temporary + h[3].temporary;
                h[4].temporary = h[0].temporary - h[2].temporary;
                for (const auto index : std::array{ 0, 1, 3, 5, 2, 4 })
                    finish_sample(h[index]);
            }
            (void)charge_world_bank_account_expense(
                c,
                &record<BankRecord>(c, person_detail::base_world(c).saved.bank_account),
                fixed::word(amount));
            analyser(c).long_histories[13].temporary += amount;
        }
        case _ => ;
    }
}
} //namespace simcoaster
