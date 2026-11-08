#include "simcoaster/world_person.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_save.hpp"
#include <array>
#include <bit>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace simcoaster {
namespace {
bool vehicle_name_equal(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        auto lower = [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
        };
        if (lower(left[i]) != lower(right[i])) {
            return false;
        }
    }
    return true;
}

ParkObjectRuntimeView& arrival_vehicle(VisitorWorld& c, ThingId id) {
    auto* object = c.entities.at(id).payload;
    if (!object) {
        throw std::logic_error("arrival vehicle record is missing");
    }
    return *reinterpret_cast<ParkObjectRuntimeView*>(object);
}

void start_vehicle_script(VisitorWorld& c, ParkObjectRuntimeView& object, int slot) {
    auto* script = world_operations::find_script(c, object.script_id);
    world_operations::set_script_slot(c, script, slot, 1);
}
} //namespace

ThingId update_world_arrival_vehicle(VisitorWorld& c, World& world, std::int32_t variant) {
    if (!(c.mode.tick_mode_initialized & 1u)) {
        c.mode.tick_mode_initialized |= 1u;
        initialize_game_mode_selector(c);
    }
    if (c.mode.game == GameMode::code_3) {
        return 0;
    }
    if (world.saved.current_arrival_vehicle) {
        start_vehicle_script(c, arrival_vehicle(c, world.saved.current_arrival_vehicle), 0);
        return world.saved.current_arrival_vehicle;
    }
    std::array<ThingId*, 3> slots { &world.saved.arrival_vehicle_size_1, &world.saved.arrival_vehicle_size_2, &world.saved.arrival_vehicle_size_3 };
    constexpr std::array<std::string_view, 3> names { "bus", "heli", "mono" };
    std::uint32_t tried = 0;
    (void)first_object_catalog_entry(&c.resources.object_catalog);
    for (;;) {
        for (auto* slot : slots) {
            if (*slot) {
                (void)retire_world_used_thing(c, &arrival_vehicle(c, *slot).base.header);
                *slot = 0;
            }
        }
        std::uint32_t selected;
        if (!tried) {
            if (variant == 0) {
                selected = static_cast<std::uint32_t>(next_world_prng_value(world)) % 3u;
            } else if (variant >= 1 && variant <= 3) {
                selected = static_cast<std::uint32_t>(variant - 1);
            } else {
                throw std::out_of_range("arrival vehicle variant");
            }
        } else if (!(tried & 4u)) {
            selected = 2;
        } else if (!(tried & 2u)) {
            selected = 1;
        } else if (!(tried & 1u)) {
            selected = 0;
        } else {
            return world.saved.current_arrival_vehicle;
        }
        tried |= 1u << selected;
        auto& slot = *slots[selected];
        if (slot) {
            start_vehicle_script(c, arrival_vehicle(c, slot), 0);
        } else {
            auto* definition = first_object_catalog_entry(&c.resources.object_catalog);
            while (definition && !vehicle_name_equal(definition->resource_catalog_name, names[selected])) {
                definition = next_object_catalog_entry(&c.resources.object_catalog);
            }
            if (!definition) {
                continue;
            }
            const auto* descriptor = record_descriptor(ThingKind::park_object);
            if (!descriptor) {
                throw std::logic_error("missing park object allocation descriptor");
            }
            auto* storage = static_cast<ParkObjectRuntimeView*>(c.entities.allocate_payload(c.ports.memory, descriptor->allocation_size));
            if (!storage) {
                throw std::bad_alloc {};
            }
            auto* object = std::construct_at(storage);
            try {
                object = world_operations::initialize_park_object_instance(c, object, 1, definition->id, 0, 0, 0);
            } catch (...) {
                c.entities.release_payload(c.ports.memory, storage);
                throw;
            }
            if (!object) {
                c.entities.release_payload(c.ports.memory, storage);
                throw std::logic_error("park object initializer returned null");
            }
            slot = object->base.header.id;
            start_vehicle_script(c, *object, 1);
        }
        world.saved.current_arrival_vehicle = slot;
        return slot;
    }
}

void world_operations::builtin::update_arrival_vehicle(WorldRuntime& c, World& world, std::int32_t variant) {
    (void)update_world_arrival_vehicle(c, world, variant);
}
} //namespace simcoaster
