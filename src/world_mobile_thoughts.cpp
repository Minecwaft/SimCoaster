#include "world_person_internal.hpp"

namespace simcoaster {
void world_operations::builtin::set_mobile_entity_thought_bubble(WorldRuntime& c, MobileEntityThoughtHistory* history, MobileThoughtCode thought, bool force) {
    if (!history) {
        throw std::invalid_argument("null thought history");
    }
    simcoaster::set_mobile_entity_thought_bubble(c, *history, thought, force);
}

void set_mobile_entity_thought_bubble(VisitorWorld& c, MobileEntityThoughtHistory& state, MobileThoughtCode thought, bool force) {
    if (thought == state.last_thought && state.thought_script) {
        return;
    }
    if (state.last_thought == MobileThoughtCode::naughty && state.thought_script && person_detail::primary_world(c).saved.game_tick <= state.time_bubble_shown + 120u) {
        return;
    }
    state.last_thought = thought;
    if (c.view.flags & 0x16) {
        return;
    }
    person_detail::initialize_mode(c);
    if (c.mode.game == GameMode::online && !force) {
        return;
    }
    if (state.thought_script) {
        world_operations::release_resource_pool_handle(c, state.thought_script);
        state.thought_script = 0;
    }
    constexpr std::array<std::uint32_t, 31> delay_multipliers { 0, 1, 1, 1, 1, 0, 0, 1, 3, 3, 3, 3, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 3, 2, 0, 0, 0, 0, 0, 0, 0 };
    const auto code = static_cast<std::uint32_t>(thought);
    //The original unknown-code diagnostic sink is empty.
    if (!code || code >= delay_multipliers.size()) {
        return;
    }
    const auto tick = person_detail::primary_world(c).saved.game_tick;
    if (tick >= state.time_bubble_shown + 20u * delay_multipliers[code]) {
        state.time_bubble_shown = tick;
        state.thought_script = world_operations::allocate_resource_variant(c, { ResourceProgramFamily::thought, code }, { 13, 0, 0, 0, 0, 0 });
    }
}

void release_expired_mobile_entity_thought_script(VisitorWorld& c, MobileEntityThoughtHistory& state) {
    const auto tick = person_detail::primary_world(c).saved.game_tick, time = state.time_bubble_shown;
    if (tick > time + 12u && (state.last_thought != MobileThoughtCode::naughty || tick > time + 120u)) {
        if (state.thought_script) {
            world_operations::release_resource_pool_handle(c, state.thought_script);
            state.thought_script = 0;
        }
    }
}
} //namespace simcoaster
