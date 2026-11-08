#include "simcoaster/rotation_tables.hpp"
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/particle_runtime.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
using Vector = std::array<std::int32_t, 3>;
constexpr auto failure = std::numeric_limits<std::uint32_t>::max();
//The tail union has16 opaque bytes before its six runtime bounds DWORDs.
constexpr std::size_t bounds_offset = 16;

std::int32_t quotient(std::int32_t a, std::int32_t b) {
    return numeric::quotient(a, b, "particle integer division");
}

std::int32_t random(ParticleRuntime& runtime, std::int32_t range, bool nonnegative = false) {
    if (!range) {
        return 0;
    }
    auto& state = runtime.state.random_state;
    state = std::bit_cast<std::int32_t>(214013u * static_cast<std::uint32_t>(state) + 2531011u);
    return (nonnegative ? ((state >> 16) & 0x7FFF) : (state >> 16)) % range;
}

std::int32_t sine(std::int32_t phase) {
    return 4 * particle_phase_sines()[static_cast<std::uint32_t>(phase / 8) & 0x1FFu];
}

std::int32_t cosine(std::int32_t phase) {
    return 4 * particle_phase_sines()[(static_cast<std::uint32_t>(phase / 8) + 128u) & 0x1FFu];
}

template <class Pool>
std::int16_t prepend(Pool& pool, std::int16_t& free, std::int16_t& head) {
    if (free < 0) {
        return -1;
    }
    const auto index = free;
    auto& item = pool.at(static_cast<std::size_t>(index));
    free = item.links.next;
    if (free >= 0) {
        pool.at(static_cast<std::size_t>(free)).links.previous = -1;
    }
    item.links.next = head;
    if (head >= 0) {
        pool.at(static_cast<std::size_t>(head)).links.previous = index;
    }
    head = index;
    return index;
}

bool spawn(ParticleRuntime& runtime, std::int16_t owner) {
    auto& state = runtime.state;
    auto& effect = state.effects.at(static_cast<std::size_t>(owner));
    const auto index = prepend(runtime.particles, state.free_particle_head, effect.particle_head);
    if (index < 0) {
        return false;
    }
    auto& particle = runtime.particles.at(static_cast<std::size_t>(index));
    ++state.live_particle_count;
    ++effect.particle_count;
    particle.state = 2;
    particle.effect_index = owner;
    particle.size = effect.initial_particle_size;
    particle.group_tag = effect.group_id;
    const auto lifetime = numeric::add(effect.initial_particle_lifetime, random(runtime, effect.initial_particle_lifetime >> 2));
    particle.remaining_lifetime = lifetime;
    particle.initial_lifetime = lifetime ? lifetime : 1;
    particle.sprite_frame_index = 0;
    particle.rotation = effect.random_rotation_enabled ? static_cast<std::uint16_t>(random(runtime, 32768, true)) : 0;
    particle.argb = effect.color_palette[effect.color_flags ? static_cast<std::size_t>(random(runtime, 16, true)) : 15];
    Vector offset {};
    if (effect.position_spread != Vector {}) {
        if (effect.box_position_spread) {
            offset[0] = random(runtime, effect.position_spread[0]);
            offset[1] = random(runtime, effect.position_spread[1], effect.nonnegative_y_spread != 0);
            offset[2] = random(runtime, effect.position_spread[2]);
        } else {
            const auto phase = random(runtime, 4096, true);
            const auto x = effect.fixed_spread_radius ? effect.position_spread[0] : random(runtime, effect.position_spread[0], true);
            offset[0] = numeric::product(x, cosine(phase)) >> 10;
            offset[1] = random(runtime, effect.position_spread[1], effect.nonnegative_y_spread != 0);
            const auto z = effect.fixed_spread_radius ? effect.position_spread[2] : random(runtime, effect.position_spread[2], true);
            offset[2] = numeric::negate(numeric::product(z, sine(phase)) >> 10);
        }
        if (effect.spread_yaw) {
            const auto x = offset[0], z = offset[2], s = sine(effect.spread_yaw), co = cosine(effect.spread_yaw);
            offset[0] = numeric::add(numeric::product(x, co), numeric::negate(numeric::product(z, s))) >> 10;
            offset[2] = numeric::add(numeric::product(x, s), numeric::product(z, co)) >> 10;
        }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        particle.position[i] = numeric::add(effect.position[i], offset[i]);
    }
    if (effect.phase_offset_radius) {
        particle.position[0] = numeric::add(particle.position[0], numeric::product(effect.phase_offset_radius, sine(effect.phase)) >> 10);
        particle.position[2] = numeric::add(particle.position[2], numeric::product(effect.phase_offset_radius, cosine(effect.phase)) >> 10);
    }
    if (effect.random_velocity_range) {
        const auto phase = random(runtime, 4096, true), radius = random(runtime, effect.random_velocity_range, true);
        particle.velocity[0] = numeric::product(radius, cosine(phase)) >> 10;
        particle.velocity[1] = random(runtime, effect.random_velocity_range);
        particle.velocity[2] = numeric::product(radius, sine(phase)) >> 10;
    } else {
        for (std::size_t i = 0; i < 3; ++i) {
            particle.velocity[i] = numeric::add(effect.initial_particle_velocity[i], random(runtime, effect.particle_velocity_jitter));
        }
    }
    if (effect.inherit_emitter_velocity) {
        for (std::size_t i = 0; i < 3; ++i) {
            particle.velocity[i] = numeric::add(particle.velocity[i], effect.motion[i]);
        }
    }
    return true;
}

void scale(ParticleEffect& effect, std::int32_t parameter) {
    for (auto& rate : effect.emission_rates) {
        if (rate) {
            const auto value = std::min(numeric::product(rate, parameter) >> 10, 127);
            rate = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value ? value : 1));
        }
    }
    if (effect.maximum_particles > 0) {
        effect.maximum_particles = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(numeric::product(effect.maximum_particles, parameter) >> 10));
        if (!effect.maximum_particles) {
            effect.maximum_particles = 1;
        }
    }
    if (effect.initial_particle_count > 0) {
        effect.initial_particle_count = numeric::product(effect.initial_particle_count, parameter) >> 10;
        if (!effect.initial_particle_count) {
            effect.initial_particle_count = 1;
        }
    }
}

std::uint32_t create_attractor(ParticleRuntime& runtime, std::uint32_t definition, const Vector& position) {
    auto& state = runtime.state;
    if (!runtime.initialized || definition >= runtime.attractor_templates.size()) {
        return failure;
    }
    const auto& source = runtime.attractor_templates[definition];
    if (state.creation_filter_enabled && source.suppress_when_filter_enabled) {
        return 0;
    }
    const auto index = prepend(state.attractors, state.free_attractor_head, state.active_attractor_head);
    if (index < 0) {
        return failure;
    }
    auto& item = state.attractors[static_cast<std::size_t>(index)];
    const auto links = item.links;
    item = source;
    item.links = links;
    item.runtime_state = 1;
    for (std::size_t i = 0; i < 3; ++i) {
        item.position[i] = position[i] >> 4;
    }
    item.generation = state.next_attractor_generation++;
    item.attached_to_parent = 0;
    return static_cast<std::uint32_t>(index) | (static_cast<std::uint32_t>(item.generation) << 16);
}

std::uint32_t create(ParticleRuntime& runtime, std::uint32_t definition, const Vector& position, std::int32_t lifetime, const Vector* direction) {
    auto& state = runtime.state;
    if (!runtime.initialized || definition >= runtime.templates.size()) {
        return failure;
    }
    const auto& source = runtime.templates[definition];
    if (state.creation_filter_enabled && source.suppress_when_filter_enabled) {
        return 0;
    }
    const auto index = prepend(state.effects, state.free_effect_head, state.active_effect_head);
    if (index < 0) {
        return failure;
    }
    auto& effect = state.effects[static_cast<std::size_t>(index)];
    const auto links = effect.links;
    effect = source;
    effect.links = links;
    if (!effect.skip_emission_scaling) {
        scale(effect, state.parameter_scale);
    }
    effect.initial_lifetime = effect.remaining_lifetime;
    for (std::size_t i = 0; i < 3; ++i) {
        effect.position[i] = position[i] >> 4;
    }
    std::array<std::int32_t, 6> bounds {};
    if (!effect.screen_space) {
        std::fill_n(bounds.begin(), 3, std::numeric_limits<std::int32_t>::max());
        std::fill_n(bounds.begin() + 3, 3, std::numeric_limits<std::int32_t>::min());
    }
    const auto encoded = save::detail::encode(bounds);
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        effect.tail[bounds_offset + i] = static_cast<std::uint8_t>(encoded[i]);
    }
    effect.particle_count = 0;
    effect.particle_head = -1;
    effect.template_index = static_cast<std::uint16_t>(definition);
    effect.runtime_state = 1;
    if (direction) {
        for (std::size_t i = 0; i < 3; ++i) {
            effect.initial_particle_velocity[i] = numeric::product((*direction)[i], effect.direction_magnitude) / 1024;
        }
    } else {
        for (auto& component : effect.motion) {
            component = numeric::add(component, random(runtime, effect.motion_jitter_range));
        }
    }
    effect.generation = state.next_effect_generation++;
    effect.attached_to_parent = 0;
    if (lifetime) {
        effect.remaining_lifetime = lifetime;
        effect.initial_lifetime = lifetime;
        effect.hold_lifetime = 0;
    }
    effect.velocity_jitter_enabled = effect.velocity_jitter_range != Vector {};
    if (!effect.emission_mode) {
        if (effect.initial_particle_count < 0) {
            throw std::invalid_argument("negative initial particle count");
        }
        for (std::int32_t i = 0; i < effect.initial_particle_count; ++i) {
            if (!spawn(runtime, index)) {
                break;
            }
        }
    } else if (effect.emission_mode == 1) {
        effect.ring_radius = 0;
    }
    const auto child_definition = std::bit_cast<std::int32_t>(effect.linked_effect);
    if (child_definition >= 0) {
        auto child_position = position;
        const auto child = static_cast<std::uint32_t>(child_definition);
        if (effect.relative_link) {
            const auto& motion = effect.linked_is_attractor ? runtime.attractor_templates.at(child).motion : runtime.templates.at(child).motion;
            for (std::size_t i = 0; i < 3; ++i) {
                child_position[i] = numeric::add(child_position[i], numeric::product(motion[i], 16));
            }
        }
        effect.linked_effect = effect.linked_is_attractor ? create_attractor(runtime, child, child_position) : (child == definition ? failure : create(runtime, child, child_position, 0, nullptr));
        if (std::bit_cast<std::int32_t>(effect.linked_effect) >= 0 && effect.relative_link) {
            const auto slot = static_cast<std::uint16_t>(effect.linked_effect);
            //Repair the original creator's unmasked attractor-handle array access.
            if (effect.linked_is_attractor) {
                state.attractors.at(slot).attached_to_parent = 1;
            } else {
                state.effects.at(slot).attached_to_parent = 1;
            }
        }
    }
    return static_cast<std::uint32_t>(index) | (static_cast<std::uint32_t>(effect.generation) << 16);
}

template <class Pool>
void release(Pool& pool, std::int16_t index, std::int16_t& head, std::int16_t& free) {
    auto& item = pool.at(static_cast<std::size_t>(index));
    const auto previous = item.links.previous, next = item.links.next;
    if (head == index) {
        head = next;
    }
    if (previous >= 0) {
        pool.at(static_cast<std::size_t>(previous)).links.next = next;
    }
    if (next >= 0) {
        pool.at(static_cast<std::size_t>(next)).links.previous = previous;
    }
    item.links = { free, -1 };
    free = index;
}

template <class Pool, class Body>
void walk(Pool& pool, std::int16_t head, Body body) {
    std::size_t count = 0;
    for (auto index = head; index >= 0;) {
        if (count++ >= pool.size()) {
            throw std::out_of_range("cyclic particle pool chain");
        }
        auto& item = pool.at(static_cast<std::size_t>(index));
        const auto next = item.links.next;
        body(index, item);
        index = next;
    }
}

using Bounds = std::array<std::int32_t, 6>;

Bounds read_bounds(const ParticleEffect& effect) {
    std::array<std::byte, 24> bytes;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::byte>(effect.tail[bounds_offset + i]);
    }
    Bounds result;
    save::detail::decode(std::span<const std::byte, 24> { bytes }, result);
    return result;
}

void write_bounds(ParticleEffect& effect, const Bounds& bounds) {
    const auto bytes = save::detail::encode(bounds);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        effect.tail[bounds_offset + i] = static_cast<std::uint8_t>(bytes[i]);
    }
}

void reset_bounds(ParticleEffect& effect) {
    Bounds bounds;
    std::fill_n(bounds.begin(), 3, std::numeric_limits<std::int32_t>::max());
    std::fill_n(bounds.begin() + 3, 3, std::numeric_limits<std::int32_t>::min());
    write_bounds(effect, bounds);
}

Vector external_position(const Vector& position) {
    Vector result;
    for (std::size_t i = 0; i < 3; ++i) {
        result[i] = numeric::product(position[i], 16);
    }
    return result;
}

ParticleAttractor* find_attractor(ParticleRuntime& runtime, std::uint32_t handle) {
    const auto index = static_cast<std::uint16_t>(handle);
    if (!handle || !runtime.initialized || index >= runtime.state.attractors.size()) {
        return nullptr;
    }
    auto& attractor = runtime.state.attractors[index];
    return attractor.generation == (handle >> 16) ? &attractor : nullptr;
}

template <class Emitter>
void move(Emitter& emitter, ParticleHeightQuery ground, std::int32_t wall_divisor) {
    for (auto& velocity : emitter.motion) {
        velocity = numeric::add(velocity, numeric::negate(numeric::product(emitter.drag_1024, velocity) / 1024));
    }
    emitter.motion[1] = numeric::add(emitter.motion[1], numeric::negate(emitter.gravity));
    for (std::size_t i = 0; i < 3; ++i) {
        emitter.position[i] = numeric::add(emitter.position[i], emitter.motion[i]);
    }
    if (!emitter.collide_with_ground) {
        return;
    }
    const auto height = ground(numeric::product(emitter.position[0], 16), numeric::product(emitter.position[2], 16)) >> 4;
    if (emitter.position[1] >= height) {
        return;
    }
    if (numeric::add(emitter.position[1], numeric::negate(height)) >= emitter.motion[1]) {
        emitter.position[1] = height;
        emitter.motion[1] /= -2;
        emitter.motion[0] /= 2;
        emitter.motion[2] /= 2;
    } else {
        emitter.position[0] = numeric::add(emitter.position[0], numeric::negate(emitter.motion[0]));
        emitter.position[1] = numeric::add(emitter.position[1], numeric::negate(emitter.motion[1]));
        emitter.motion[0] /= -wall_divisor;
        emitter.motion[1] /= wall_divisor;
        emitter.motion[2] /= -wall_divisor;
    }
}

std::int32_t emission_rate(const ParticleEffect& effect) {
    const auto quartile = effect.initial_lifetime ? quotient(numeric::product(4, effect.remaining_lifetime), effect.initial_lifetime) : -1;
    return effect.emission_rates[quartile >= 0 && quartile <= 2 ? static_cast<std::size_t>(3 - quartile) : 0];
}

void emit_ring(ParticleRuntime& runtime, std::int16_t owner, ParticleEffect& effect) {
    const auto radius = numeric::add(effect.ring_radius, effect.particle_velocity_jitter);
    auto& state = runtime.state;
    const auto attempts = effect.runtime_state > 0 ? numeric::product(emission_rate(effect), numeric::add(radius >> 7, 32)) : 0;
    for (std::int32_t i = 0; i < attempts; ++i) {
        const auto index = prepend(runtime.particles, state.free_particle_head, effect.particle_head);
        if (index < 0) {
            break;
        }
        auto& particle = runtime.particles.at(static_cast<std::size_t>(index));
        ++state.live_particle_count;
        ++effect.particle_count;
        particle.effect_index = owner;
        particle.state = 2;
        particle.size = effect.initial_particle_size;
        particle.group_tag = effect.group_id;
        particle.argb = effect.color_palette[15];
        const auto lifetime = numeric::add(effect.initial_particle_lifetime, random(runtime, effect.initial_particle_lifetime >> 1));
        particle.remaining_lifetime = lifetime;
        particle.initial_lifetime = lifetime ? lifetime : 1;
        const auto phase = random(runtime, 4096, true), co = cosine(phase), s = sine(phase);
        particle.position = { numeric::add(effect.position[0], numeric::product(radius, co) >> 10), effect.position[1], numeric::add(effect.position[2], numeric::negate(numeric::product(radius, s) >> 10)) };
        particle.velocity = { numeric::product(effect.particle_velocity_jitter, co) >> 10, numeric::add(effect.initial_particle_velocity[1], 20), numeric::negate(numeric::product(effect.particle_velocity_jitter, s) >> 10) };
        //Native ring emission preserves reused sprite-frame and rotation values.
    }
    effect.ring_radius = radius;
}

void update_attractors(ParticleRuntime& runtime, ParticleHeightQuery ground) {
    auto& state = runtime.state;
    walk(state.attractors, state.active_attractor_head, [&](std::int16_t index, ParticleAttractor& attractor) {
        if (!attractor.hold_lifetime) {
            attractor.remaining_lifetime = numeric::add(attractor.remaining_lifetime, -1);
        }
        if (!attractor.radius_parameter) {
            attractor.radius_parameter = 1;
        }
        if (!attractor.attached_to_parent && (attractor.motion != Vector {} || attractor.gravity)) {
            move(attractor, ground, 4);
        }
        if (attractor.remaining_lifetime < 0) {
            const auto position = external_position(attractor.position);
            if (attractor.expiry_spawn_is_attractor) {
                (void)create_attractor(runtime, static_cast<std::uint32_t>(attractor.expiry_spawn_id), position);
            } else {
                (void)create(runtime, static_cast<std::uint32_t>(attractor.expiry_spawn_id), position, 0, nullptr);
            }
            attractor.expiry_spawn_is_attractor = 255;
            attractor.runtime_state = 0;
            release(state.attractors, index, state.active_attractor_head, state.free_attractor_head);
            return;
        }
        Vector center;
        for (std::size_t i = 0; i < 3; ++i) {
            center[i] = numeric::add(attractor.position[i], random(runtime, attractor.jitter_range[i]));
        }
        walk(state.effects, state.active_effect_head, [&](std::int16_t, ParticleEffect& effect) {
            if (effect.updates_suspended || (attractor.target_group != -1 && attractor.target_group != effect.group_id)) {
                return;
            }
            walk(runtime.particles, effect.particle_head, [&](std::int16_t, ParticleInstance& particle) {
                if (attractor.target_group < 0 ? state.effects.at(static_cast<std::size_t>(particle.effect_index)).ignore_global_attractors != 0 : particle.group_tag != attractor.target_group) {
                    return;
                }
                Vector delta;
                std::int32_t square = 0;
                for (std::size_t i = 0; i < 3; ++i) {
                    delta[i] = numeric::add(center[i], numeric::negate(particle.position[i])) / 4;
                    square = numeric::add(square, numeric::product(delta[i], delta[i]));
                }
                const auto distance = numeric::add(square >> 8, 10);
                for (std::size_t i = 0; i < 3; ++i) {
                    particle.velocity[i] = numeric::add(particle.velocity[i], quotient(numeric::product(attractor.strength, delta[i]), distance));
                }
                if (attractor.mode == 2 && distance < (numeric::product(attractor.radius_parameter, attractor.radius_parameter) >> 8)) {
                    particle.remaining_lifetime = 0;
                }
            });
        });
    });
}

void update_particles(ParticleRuntime& runtime) {
    auto& state = runtime.state;
    walk(state.effects, state.active_effect_head, [&](std::int16_t, ParticleEffect& list_owner) {
        if (list_owner.updates_suspended) {
            return;
        }
        walk(runtime.particles, list_owner.particle_head, [&](std::int16_t index, ParticleInstance& particle) {
            auto& effect = state.effects.at(static_cast<std::size_t>(particle.effect_index));
            particle.remaining_lifetime = numeric::add(particle.remaining_lifetime, -1);
            if (particle.remaining_lifetime < 0) {
                --state.live_particle_count;
                --effect.particle_count;
                if (effect.particle_expiry_template >= 0) {
                    (void)create(runtime, static_cast<std::uint32_t>(effect.particle_expiry_template), external_position(particle.position), 0, nullptr);
                }
                particle.state = 0;
                release(runtime.particles, index, effect.particle_head, state.free_particle_head);
                return;
            }
            if (effect.remaining_lifetime < 0 && effect.expire_particles_with_effect) {
                particle.remaining_lifetime = -1;
            }
            if (!effect.color_flags) {
                const auto palette = effect.initial_particle_lifetime > 0 ? quotient(numeric::product(15, particle.remaining_lifetime), particle.initial_lifetime) : 0;
                particle.argb = effect.color_palette.at(static_cast<std::size_t>(palette));
            } else if (effect.color_flags & 1u) {
                particle.argb = effect.color_palette[static_cast<std::size_t>(random(runtime, 16, true))];
            }
            auto frame = static_cast<std::int32_t>(effect.sprite_frame_count);
            if (frame) {
                if (particle.remaining_lifetime >= 0) {
                    frame = numeric::add(frame, numeric::negate(quotient(numeric::product(particle.remaining_lifetime, frame - 1), particle.initial_lifetime)));
                }
                particle.sprite_frame_index = static_cast<std::uint16_t>(frame - 1);
                if (particle.sprite_frame_index >= effect.sprite_frame_count) {
                    particle.sprite_frame_index = 0;
                }
            } else {
                particle.sprite_frame_index = 0;
            }
            particle.size = effect.initial_particle_lifetime <= 0 ? effect.initial_particle_size : numeric::low_short(numeric::add(effect.final_particle_size, quotient(numeric::product(particle.remaining_lifetime, static_cast<std::int32_t>(effect.initial_particle_size) - effect.final_particle_size), particle.initial_lifetime)));
            auto rotation = static_cast<std::int32_t>(effect.rotation_step_min);
            if (rotation != effect.rotation_step_max) {
                rotation = numeric::add(rotation, random(runtime, static_cast<std::int32_t>(effect.rotation_step_max) - rotation, true));
            }
            particle.rotation = static_cast<std::uint16_t>(particle.rotation + rotation);
            if (effect.velocity_jitter_enabled) {
                for (std::size_t i = 0; i < 3; ++i) {
                    particle.velocity[i] = numeric::add(particle.velocity[i], random(runtime, effect.velocity_jitter_range[i]));
                }
            }
            if (effect.particle_drag_1024) {
                for (auto& velocity : particle.velocity) {
                    velocity = numeric::add(velocity, numeric::negate(numeric::product(effect.particle_drag_1024, velocity) / 1024));
                }
            }
            particle.velocity[1] = numeric::add(particle.velocity[1], numeric::negate(effect.particle_gravity));
            if (particle.state == 2) {
                particle.state = 1;
            } else {
                for (std::size_t i = 0; i < 3; ++i) {
                    particle.position[i] = numeric::add(particle.position[i], particle.velocity[i]);
                }
            }
            if (!effect.screen_space) {
                auto bounds = read_bounds(effect);
                const auto radius = particle.size / 1024;
                for (std::size_t i = 0; i < 3; ++i) {
                    bounds[i] = std::min(bounds[i], numeric::add(particle.position[i], numeric::negate(radius)));
                    bounds[i + 3] = std::max(bounds[i + 3], numeric::add(particle.position[i], radius));
                }
                write_bounds(effect, bounds);
            }
        });
    });
}
} //namespace

void reset_particle_runtime_pools(ParticleRuntime& runtime) {
    runtime.control_particle_handles.clear();
    if (!runtime.initialized) {
        return;
    }
    auto& state = runtime.state;
    state.live_particle_count = 0;
    state.update_call_count = 0;
    state.random_state = 1;
    state.unknown_reset_one_020 = 1;
    state.next_effect_generation = 1;
    state.next_attractor_generation = 1;
    const auto reset = [](auto& pool) {
        for (std::size_t i = 0; i < pool.size(); ++i) {
            pool[i] = {};
            pool[i].links = { i + 1 < pool.size() ? static_cast<std::int16_t>(i + 1) : static_cast<std::int16_t>(-1), static_cast<std::int16_t>(static_cast<std::int32_t>(i) - 1) };
        }
    };
    reset(state.effects);
    reset(state.attractors);
    reset(runtime.particles);
    state.active_effect_head = -1;
    state.active_attractor_head = -1;
    state.free_effect_head = 0;
    state.free_attractor_head = 0;
    state.free_particle_head = 0;
}

void initialize_particle_runtime(ParticleRuntime& runtime, std::int32_t capacity, std::int32_t parameter_scale) {
    if (runtime.initialized) {
        return;
    }
    runtime.state = {};
    runtime.state.particle_capacity = std::clamp(capacity, 100, 32767);
    runtime.state.parameter_scale = parameter_scale ? parameter_scale : 1;
    runtime.particles.resize(static_cast<std::size_t>(runtime.state.particle_capacity));
    runtime.initialized = true;
    reset_particle_runtime_pools(runtime);
}

ParticleEffect* find_particle_effect(ParticleRuntime& runtime, std::uint32_t handle) {
    const auto index = static_cast<std::uint16_t>(handle);
    if (!handle || !runtime.initialized || index >= runtime.state.effects.size()) {
        return nullptr;
    }
    auto& effect = runtime.state.effects[index];
    return effect.generation == (handle >> 16) ? &effect : nullptr;
}

std::uint32_t create_particle_effect(ParticleRuntime& runtime, std::uint32_t definition, const Vector& position, std::int32_t lifetime) {
    return create(runtime, definition, position, lifetime, nullptr);
}

std::uint32_t create_particle_effect_with_direction(ParticleRuntime& runtime, std::uint32_t definition, const Vector& position, const Vector& direction) {
    return create(runtime, definition, position, 0, &direction);
}

std::int32_t default_particle_surface_height(std::int32_t, std::int32_t) {
    return 0;
}

void update_particle_system(ParticleRuntime& runtime, ParticleHeightQuery ground) {
    if (!runtime.initialized) {
        return;
    }
    auto& state = runtime.state;
    state.update_call_count = numeric::add(state.update_call_count, 1);
    if (state.update_call_count <= 1) {
        return;
    }
    walk(state.effects, state.active_effect_head, [&](std::int16_t index, ParticleEffect& effect) {
        if (effect.updates_suspended) {
            return;
        }
        if (!effect.hold_lifetime) {
            effect.remaining_lifetime = numeric::add(effect.remaining_lifetime, -1);
        }
        if (!effect.screen_space) {
            reset_bounds(effect);
        }
        if (effect.remaining_lifetime < 0) {
            if (effect.expiry_spawn_id >= 0 && effect.remaining_lifetime == -1) {
                const auto position = external_position(effect.position);
                if (effect.expiry_spawn_is_attractor) {
                    (void)create_attractor(runtime, static_cast<std::uint32_t>(effect.expiry_spawn_id), position);
                } else {
                    (void)create(runtime, static_cast<std::uint32_t>(effect.expiry_spawn_id), position, 0, nullptr);
                }
                effect.expiry_spawn_id = -1;
            }
            if (effect.expire_linked_child && !(effect.linked_effect & 0x80000000u)) {
                if (effect.linked_is_attractor) {
                    if (auto* child = find_attractor(runtime, effect.linked_effect)) {
                        child->remaining_lifetime = -1;
                    }
                } else if (auto* child = find_particle_effect(runtime, effect.linked_effect)) {
                    child->remaining_lifetime = -1;
                }
            }
            if (!effect.particle_count) {
                effect.runtime_state = 0;
                effect.generation = 0;
                release(state.effects, index, state.active_effect_head, state.free_effect_head);
            }
            return;
        }
        if (effect.relative_link && !(effect.linked_effect & 0x80000000u)) {
            if (effect.linked_is_attractor) {
                if (auto* child = find_attractor(runtime, effect.linked_effect)) {
                    child->position = effect.position;
                }
            } else if (auto* child = find_particle_effect(runtime, effect.linked_effect)) {
                child->position = effect.position;
            }
        }
        if (effect.phase_increment) {
            effect.phase = numeric::add(effect.phase, effect.phase_increment) & 0xFFF;
        }
        if (!effect.attached_to_parent && (effect.motion != Vector {} || effect.gravity)) {
            move(effect, ground, 2);
        }
        if (effect.emission_mode == 1) {
            emit_ring(runtime, index, effect);
        } else if (!effect.emission_mode && effect.maximum_particles > effect.particle_count) {
            const auto rate = emission_rate(effect);
            if (rate < 0) {
                effect.emission_delay_counter = numeric::low_short(static_cast<std::int32_t>(effect.emission_delay_counter) - 1);
                if (effect.emission_delay_counter <= 0) {
                    (void)spawn(runtime, index);
                    effect.emission_delay_counter = numeric::low_short(-rate);
                }
            } else {
                for (std::int32_t i = 0; i < rate; ++i) {
                    if (!spawn(runtime, index)) {
                        break;
                    }
                }
            }
        }
    });
    update_attractors(runtime, ground);
    update_particles(runtime);
}

int resize_control_particle_group(ParticleRuntime& runtime, int count, std::uint32_t definition) {
    if (count < 0 || count > 5) {
        throw std::out_of_range("control particle group capacity");
    }
    auto& handles = runtime.control_particle_handles;
    const auto desired = static_cast<std::size_t>(count);
    if (desired > handles.size()) {
        const auto growth = static_cast<int>(desired - handles.size());
        while (handles.size() < desired) {
            handles.push_back(create_particle_effect(runtime, definition, {}));
        }
        return growth; //Native counts requested slots, even filtered/failed ones.
    }
    int removed = 0;
    for (std::size_t i = desired; i < handles.size(); ++i) {
        if (handles[i]) {
            if (auto* effect = find_particle_effect(runtime, handles[i])) {
                effect->remaining_lifetime = -2;
            }
            --removed;
        }
    }
    handles.resize(desired);
    return removed;
}

int clear_control_particle_group(ParticleRuntime& runtime) {
    for (auto handle : runtime.control_particle_handles) {
        if (handle) {
            if (auto* effect = find_particle_effect(runtime, handle)) {
                effect->render_hidden = 1;
            }
        }
    }
    return resize_control_particle_group(runtime, 0, 0);
}
} //namespace simcoaster
