#include "simcoaster/world_save_fields.hpp"
#include "simcoaster/world_staff.hpp"

namespace simcoaster {
static_assert(std::is_standard_layout_v<StaffRuntimeRecord> && std::is_trivially_destructible_v<StaffRuntimeRecord>);
static_assert(std::is_standard_layout_v<MechanicRuntimeRecord> && std::is_standard_layout_v<HandymanRuntimeRecord> && std::is_standard_layout_v<EntertainerRuntimeRecord> && std::is_standard_layout_v<GuardRuntimeRecord> && std::is_standard_layout_v<GardenerRuntimeRecord>);

void initialize_staff_for_restore(StaffRuntimeRecord& staff) {
    staff.room_occupancy_detaching = false;
    staff.mobile.thought_history = {};
    initialize_mobile_navigation_state(staff.mobile.navigation);
    staff.time_hired = 0;
    staff.happiness_score = 0;
    staff.motivation = 0;
    staff.energy = 0;
    staff.area.bounds = {};
    staff.area.header.bits &= ~std::uint32_t { 255 };
    staff.area.begin = staff.area.end = staff.area.capacity = nullptr;
    for (auto& ring : staff.activity_rings) {
        ring.temporary = 0;
        ring.current_entry = -1;
        ring.num_entries = 30;
        ring.wrapped = 0;
    }
    //The loader leaves samples, subtype fields and unrelated shared fields
    //untouched. It neither hires staff nor publishes gameplay creation events.
}

int transfer_world_staff_base_state(StaffWorld& c, StaffRuntimeRecord& staff, SaveArchive archive) {
    if (transfer_mobile_entity_state(c, staff.base, staff.mobile, archive)) {
        return 1;
    }
    if (archive.schema > 1) {
        return 0;
    }
    if (!archive.fields<0, 1>(staff) || !archive.meter(staff.happiness_score) || !archive.meter(staff.motivation) || !archive.fields<1, 2>(staff)) {
        return 1;
    }
    for (auto& unit : staff.name) {
        if (!archive.field(unit, "mName[i]")) {
            return 1;
        }
    }
    if (!archive.fields<2, 9>(staff) || !archive.meter(staff.energy) || !archive.field(staff.first_known_ability, "mKnownAbilities[j]") || !archive.field(staff.second_ability_or_count.bits, "mKnownAbilities[j]") || !archive.field(staff.second_ability_or_count.bits, "mNumberOfKnownAbilities") || !archive.fields<9>(staff)) {
        return 1;
    }
    for (auto& ring : staff.activity_rings) {
        if (archive.history_ring(ring)) {
            return 1;
        }
    }
    //The original parent deliberately discards this helper's status.
    (void)archive.fields(staff.area.bounds);
    return 0;
}

int transfer_world_staff_state(StaffWorld& c, ThingKind kind, ThingHeader* record, SaveArchive archive) {
    if (entity_supports<StaffRuntimeRecord>(kind)) {
        return transfer_builtin_payload_as(c, kind, record, archive);
    }
    //Retain the original invalid-kind contract: the shared prefix can transfer
    //(or fail) before subtype rejection, and newer schemas return after it.
    auto& staff = *reinterpret_cast<StaffRuntimeRecord*>(record);
    if (transfer_world_staff_base_state(c, staff, archive)) {
        return 1;
    }
    if (archive.schema > 1) {
        return 0;
    }
    throw std::invalid_argument("not a staff record kind");
}
} //namespace simcoaster
