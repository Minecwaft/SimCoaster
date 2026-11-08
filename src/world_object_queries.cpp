#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_research.hpp"
#include <stdexcept>
#include <cmath>

namespace simcoaster {
namespace {
const ParkObjectRuntimeView& require_object(const ParkObjectRuntimeView* object) {
    return require_bound<std::invalid_argument>(object, "null park object");
}

ResearchCatalogDefinition& require_definition(AttractionWorld& c, const ParkObjectRuntimeView* object) {
    return require_bound<std::out_of_range>(get_park_object_definition(c, object), "park object definition");
}
} //namespace

//Native0x5DF6D0: mObjectDefinitionID is a WORD, not a pointer cached in
//the object. A missing definition is returned unchanged to the caller.
ResearchCatalogDefinition* get_park_object_definition(AttractionWorld& c, const ParkObjectRuntimeView* object) {
    return find_object_definition_by_id(&c.resources.object_catalog, require_object(object).definition_id);
}

//Native0x5E81D0 tests IsSpecial ==1; other nonzero values are ordinary.
bool park_object_definition_is_marked_special(AttractionWorld& c, const ParkObjectRuntimeView* object) {
    return require_definition(c, object).is_special == 1;
}

//Native0x5E8210 returns the raw mBeenActivated byte, without normalization.
std::uint8_t get_park_object_activation_flag(const ParkObjectRuntimeView* object) {
    return require_object(object).activated;
}

std::uint8_t truncate_park_object_condition(float value) {
    const auto truncated = std::trunc(static_cast<double>(value));
    const auto limit = std::ldexp(1.0, 63);
    if (!std::isfinite(truncated) || truncated < -limit || truncated >= limit) {
        return 0;
    }
    return static_cast<std::uint8_t>(static_cast<std::int64_t>(truncated));
}

bool park_object_requires_staff_service(AttractionWorld& c, const ParkObjectRuntimeView& object) {
    const auto flags = static_cast<std::uint16_t>(object.capabilities); //Priority uses the entry mask.
    if (flags & std::to_underlying(ParkObjectCapabilities::provides_relief)) {
        return truncate_park_object_condition(object.repair_state) < 25;
    }
    const auto slot = [&](int index) {
        auto* script = world_operations::find_script(c, object.script_id);
        return world_operations::script_slot(c, script, index);
    };
    if (flags & std::to_underlying(ParkObjectCapabilities::holds_litter)) {
        const auto dirty = slot(11), contents = slot(4), onfire = slot(5);
        return contents > dirty || onfire != 0;
    }
    if (flags & std::to_underlying(ParkObjectCapabilities::requires_watering)) {
        const auto dead = slot(4), condition = slot(0), onfire = slot(12);
        return condition > 0 && (!dead || onfire);
    }
    return false;
}

ThingId find_first_park_object_record_id_by_definition_id(AttractionWorld& c, std::uint16_t definition_id) {
    //Native0x647850 follows the process-global active list. Neither World alias,
    //the park-only list nor retirement_pending participates in this query.
    ThingId id = c.entities.active_head ? static_cast<ThingId>(c.entities.active_head->id_storage) : 0;
    std::size_t traversed = 0;
    while (id) {
        if (traversed++ >= c.entities.size()) {
            throw std::length_error("cyclic active thing list");
        }
        auto* payload = c.entities.at(id).payload;
        if (!payload) {
            throw std::invalid_argument("null active thing payload");
        }
        if (payload->kind == ThingKind::park_object) {
            auto* object = reinterpret_cast<const ParkObjectRuntimeView*>(payload);
            //Compare the resolved definition's ID. Even an existing cache entry
            //inconsistent with the object's lookup key is observed by the original.
            if (require_definition(c, object).id == definition_id) {
                return id;
            }
        }
        //The link belongs to metadata indexed by the payload's own header ID,
        //not necessarily the cursor ID used to fetch that payload.
        const auto* next = c.entities.at(payload->id).active_next;
        id = next ? static_cast<ThingId>(next->id_storage) : 0;
    }
    return 0;
}
} //namespace simcoaster
