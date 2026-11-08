#include "world_staff_internal.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mail.hpp"

namespace simcoaster {
using namespace staff_detail;

namespace {
bool blocked(StaffWorld& c, ParkObjectRuntimeView& o) {
    if (has(o, ParkObjectCapabilities::holds_litter)) {
        return world_operations::script_slot(c, script(c, o), 5) != 0;
    }
    if (has(o, ParkObjectCapabilities::requires_watering)) {
        return world_operations::script_slot(c, script(c, o), 12) != 0;
    }
    return false;
}
} //namespace

namespace staff_detail {
void reset_position(StaffWorld& c, MapObjectBase& base, MobileEntityState& mobile, std::uint16_t x, std::uint16_t y) {
    reset_mobile_entity_position(c, base, mobile, x, y);
}

void face_cell_direction(StaffWorld& c, StaffRuntimeRecord& s) {
    s.mobile.sprite_angle = match (get_world_cell_direction(&c.simulation.map_cell_records[cell(s.base) - 1].base)) -> std::uint32_t {
        case 4  => 1500;
        case 16 => 1000;
        case 64 => 500;
        case _  => 2000;
    };
}

ThingId select_staff_service_target(StaffWorld& c, StaffRuntimeRecord& s, ParkObjectCapabilities category) {
    ThingId selected = 0;
    auto best = std::numeric_limits<std::uint32_t>::max();
    const auto& role = s.base.header.kind == ThingKind::gardener ? c.staff.recruitment.balance.Gardener : c.staff.recruitment.balance.Handyman;
    for (auto id = world(c).saved.first_park_object; id;) {
        auto& o = object(c, id);
        const auto next = o.next_park_object;
        //Relief/plant scans filter capabilities before reading the claim. The
        //handyman bin/fire scan reads every claim before its category tests.
        if (category != ParkObjectCapabilities::holds_litter && !has(o, category)) {
            id = next;
            continue;
        }
        bool allowed = !claim_owner(c, o) || claim_owner(c, o) == s.base.header.id;
        if (allowed) {
            if (category == ParkObjectCapabilities::holds_litter) {
                const auto firefighting = s.first_known_ability == ThemeAbility::firefighting;
                allowed = (has(o, ParkObjectCapabilities::holds_litter) && (!blocked(c, o) || firefighting)) || (has(o, ParkObjectCapabilities::requires_watering) && blocked(c, o) && firefighting);
            } else {
                allowed = has(o, category);
            }
        }
        if (allowed && is_park_object_in_staff_work_area(c, s, o)) {
            const auto squared = distance(s.base, o.base);
            const auto radius = interpolate(c, s, role.MinDetectionRange, role.MaxDetectionRange);
            //Requested jobs compete by distance. The automatic branch deliberately
            //has no comparison with best: later in-range jobs may replace earlier ones.
            bool eligible = o.requested_service && squared < best;
            if (category == ParkObjectCapabilities::requires_watering && eligible) {
                eligible = !blocked(c, o);
            }
            if (!eligible) {
                eligible = park_object_requires_staff_service(c, o) && squared < static_cast<std::uint32_t>(radius) * static_cast<std::uint32_t>(radius) && (category != ParkObjectCapabilities::requires_watering || !blocked(c, o));
            }
            if (eligible) {
                bool reached;
                if (category == ParkObjectCapabilities::provides_relief) {
                    reached = route(c, s, o.entry_cell);
                } else if (category == ParkObjectCapabilities::holds_litter && has(o, ParkObjectCapabilities::holds_litter)) {
                    const auto p = compute_park_object_service_target_coordinates(c, o);
                    reached = route(c, s, p[0], p[1]);
                } else {
                    const auto approach = find_nearest_path_cell_around_object(c, o, cell(s.base));
                    reached = approach && route(c, s, approach);
                }
                if (reached) {
                    selected = id;
                    best = squared;
                }
            }
        }
        id = next;
    }
    if (selected) {
        claim(c, object(c, selected), s);
    }
    return selected;
}
} //namespace staff_detail

CellId find_nearest_available_litter_cell(StaffWorld& c, ParkAnalyserState& analyser, ThingId collector, CellId origin, std::int32_t radius) {
    const auto index = static_cast<std::uint32_t>(origin) - 1u;
    const auto x = static_cast<int>(index & 127u), y = static_cast<int>(index >> 7);
    const auto min_x = std::max<std::int64_t>(0, static_cast<std::int64_t>(x) - radius), max_x = std::min<std::int64_t>(127, static_cast<std::int64_t>(x) + radius);
    const auto min_y = std::max<std::int64_t>(0, static_cast<std::int64_t>(y) - radius), max_y = std::min<std::int64_t>(127, static_cast<std::int64_t>(y) + radius);
    std::size_t count = 0;
    for (auto cx = min_x; cx <= max_x; ++cx) {
        for (auto cy = min_y; cy <= max_y; ++cy) {
            const auto id = static_cast<CellId>(cx + cy * 128 + 1);
            const auto& target = c.simulation.map_cell_records[id - 1];
            if (target.litter_selector && (!target.litter_collector || target.litter_collector == collector)) {
                analyser.litter_candidate_cells.at(count++) = id;
            }
        }
    }
    CellId selected = 0;
    int best = 9999;
    for (std::size_t i = 0; i < count; ++i) {
        const auto id = analyser.litter_candidate_cells[i];
        const auto n = static_cast<std::uint32_t>(id) - 1u;
        const auto distance = std::abs(static_cast<int>(n & 127u) - x) + std::abs(static_cast<int>(n >> 7) - y);
        if (distance < best) {
            selected = id;
            best = distance;
        }
    }
    return selected;
}

void complete_park_object_service_task(StaffWorld& c, ParkObjectRuntimeView& o) {
    const auto clear_tags = [&](std::array<std::int32_t, 4> keys) {
        (void)mark_tag_messages_by_key_set_and_format_word_for_deletion(c, &record<TagSystemRecord>(c, world(c).saved.tag_system), keys, o.base.header.id);
    };
    if (has(o, ParkObjectCapabilities::provides_relief)) {
        world_operations::set_script_slot(c, script(c, o), 8, 0);
        if (park_object_requires_staff_service(c, o)) {
            remove_world_re_cell_effect_profile(c.simulation.region_effects, RegionEffectProfileId::dirty_toilet, cell(o.base));
            add_world_re_cell_effect_profile(c.simulation.region_effects, RegionEffectProfileId::clean_toilet, cell(o.base));
        }
        clear_tags({ 10, 11, 12, 13 });
    }
    if (has(o, ParkObjectCapabilities::holds_litter)) {
        world_operations::set_script_slot(c, script(c, o), 1, 1);
        if (park_object_requires_staff_service(c, o)) {
            remove_world_re_cell_effect_profile(c.simulation.region_effects, RegionEffectProfileId::dirty_bin, cell(o.base));
            add_world_re_cell_effect_profile(c.simulation.region_effects, RegionEffectProfileId::clean_bin, cell(o.base));
        }
    }
    if (has(o, ParkObjectCapabilities::requires_watering)) {
        world_operations::set_script_slot(c, script(c, o), 1, 1);
        clear_tags({ 44, 45, 46, 47 });
    }
    o.repair_state = 100;
    o.assigned_staff_member = 0;
    o.requested_service = 0;
}

void release_world_litter_record(StaffWorld& c, MapCell& target) {
    if (target.litter_selector == std::to_underlying(LitterSelector::vomit)) {
        remove_world_re_cell_effect_profile(c.simulation.region_effects, RegionEffectProfileId::vomit_puddle, target.base.index);
    } else if (target.litter_selector == std::to_underlying(LitterSelector::stink_bomb)) {
        remove_world_re_cell_effect_profile(c.simulation.region_effects, RegionEffectProfileId::stink_bomb, target.base.index);
        if (target.litter_auxiliary_request) {
            c.ports.effects.set_particle_effect_lifetime(c, target.litter_auxiliary_request, -2);
        }
    }
    world_operations::release_resource_pool_handle(c, target.litter_request);
    target.litter_request = 0;
    target.litter_collector = 0;
    target.litter_selector = 0;
}
} //namespace simcoaster
