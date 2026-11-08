#include "world_person_internal.hpp"

namespace simcoaster {
void unlink_and_release_world_mobile_entity_base_resources(VisitorWorld& c, MapObjectBase& base, MobileEntityState& mobile) {
    unlink_world_used_thing_map_and_event_lists(c, &base.header);
    if (mobile.animation.sprite_script) {
        release_world_sprite_animation_resource_handle(c, mobile.animation);
    }
    //Retirement releases this handle but deliberately leaves its stored value.
    if (mobile.thought_history.thought_script) {
        world_operations::release_resource_pool_handle(c, mobile.thought_history.thought_script);
    }
}

void cleanup_world_person_record(VisitorWorld& c, PersonRuntimeView& person) {
    if (person.balloon_script || person.remaining_balloon_life) {
        discard_person_balloon(c, person, false);
    }
    if (person.umbrella_script) {
        release_world_person_umbrella_resource(c, person);
    }
    unlink_and_release_world_mobile_entity_base_resources(c, person.base, person.mobile);
    --c.visitors.count;
}
} //namespace simcoaster
