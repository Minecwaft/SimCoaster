#include "world_staff_internal.hpp"
#include "simcoaster/resource_variant.hpp"

namespace simcoaster {
using namespace staff_detail;

namespace staff_detail {
void release_escorted_person(StaffWorld& c, PersonRuntimeView& person, CellId origin) {
    const auto p = center(origin);
    auto& mobile = person.mobile;
    reset_position(c, person.base, mobile, p[0], p[1]);
    set_world_sprite_animation_resource(c, mobile.animation, 1, static_cast<std::int32_t>(mobile.sprite_category), static_cast<std::int32_t>(mobile.sprite_id));
    c.ports.resources.submit_mobile_entity_positioned_template_request(c, mobile, c.resources.kids_catalog_slot, 128);
    mobile.navigation.navigation_mode = 1;
    if (!try_set_mobile_entity_path_to_park_exit(c, person)) {
        (void)retire_world_used_thing(c, &person.base.header);
    } else {
        enter_world_person_state(c, person, PersonState::walk_to_exit);
    }
}
} //namespace staff_detail

void set_world_staff_state_by_type(StaffWorld& c, StaffRuntimeRecord& s, StaffState requested) {
    match (s.base.header.kind) {
        case ThingKind::mechanic   => set_world_mechanic_state(c, reinterpret_cast<MechanicRuntimeRecord&>(s), requested);
        case ThingKind::researcher => {
            enter_world_staff_state(c, s, requested);
            s.state = requested;
        }
        case ThingKind::handyman => {
            match (requested) {
                case StaffState::walking_to_litter or StaffState::walking_to_clean_toilet or StaffState::walking_to_clean_bin => {
                    set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::boost_25);
                    set_sprite_animation_next(s.mobile.animation, 15);
                    s.state = requested;
                }
                case StaffState::cleaning_up or StaffState::cleaning_toilet or StaffState::cleaning_bin => {
                    reinterpret_cast<HandymanRuntimeRecord&>(s).time_started_cleaning = world(c).saved.game_tick;
                    set_sprite_animation_next(s.mobile.animation, 25);
                    s.state = requested;
                }
                case _ => enter_world_staff_state(c, s, requested);
            }
        }
        case ThingKind::gardener => {
            match (requested) {
                case StaffState::walking_to_water_plant => {
                    set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::boost_25);
                    set_sprite_animation_next(s.mobile.animation, 15);
                    s.state = requested;
                }
                case StaffState::watering_plant => {
                    reinterpret_cast<GardenerRuntimeRecord&>(s).time_started_watering = world(c).saved.game_tick;
                    set_sprite_animation_next(s.mobile.animation, 25);
                    s.state = requested;
                }
                case _ => enter_world_staff_state(c, s, requested);
            }
        }
        case ThingKind::guard => {
            auto& guard = reinterpret_cast<GuardRuntimeRecord&>(s);
            match (requested) {
                case StaffState::idle => {
                    guard.perpetrator = 0;
                    enter_world_staff_state(c, s, requested);
                    s.state = requested;
                }
                case StaffState::picked_up => {
                    if (s.state == StaffState::walking_to_entrance && guard.perpetrator && c.entities.at(guard.perpetrator).payload) {
                        release_escorted_person(c, record<PersonRuntimeView>(c, guard.perpetrator), cell(s.base));
                        s.state = requested;
                        guard.perpetrator = 0;
                    } else {
                        s.state = requested;
                    }
                }
                case StaffState::prosecuting => {
                    set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::boost_25);
                    guard.prosecution_timestamp = 5u * static_cast<std::uint32_t>(calculate_staff_work_value(c, &s));
                    s.state = requested;
                }
                case StaffState::first_aiding => {
                    set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::boost_25);
                    guard.first_aid_timestamp = 5u * static_cast<std::uint32_t>(calculate_staff_work_value(c, &s));
                    s.state = requested;
                }
                case StaffState::walking_to_entrance or StaffState::returning_to_patrol => {
                    set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::normal);
                    s.state = requested;
                }
                case _ => {
                    set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::normal);
                    enter_world_staff_state(c, s, requested);
                }
            }
        }
        case ThingKind::entertainer => {
            const auto* catalog = c.resources.variants.catalog;
            if (!catalog) {
                throw std::logic_error("entertainer sprite metadata is missing");
            }
            const auto first = catalog->variants.at(s.mobile.sprite_category).first_loaded_resource_index;
            const auto count = catalog->resources.at(first + s.mobile.sprite_id).descriptor.available_component_kind_count();
            const auto theme = selected_research_theme_ordinal(c), choice = bounded_random(c, count);
            if (requested == StaffState::entertaining) {
                const auto variant = choice ? (theme == 2 && s.mobile.sprite_id == 1 ? 21u : 22u) : (theme == 0 && s.mobile.sprite_id == 0 ? 20u : 19u);
                set_sprite_animation_next(s.mobile.animation, variant);
                s.state = requested;
                reinterpret_cast<EntertainerRuntimeRecord&>(s).time_started_entertaining = world(c).saved.game_tick;
            } else {
                enter_world_staff_state(c, s, requested);
            }
        }
        case _ => ;
    }
}
} //namespace simcoaster
