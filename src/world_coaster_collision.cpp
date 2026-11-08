#include "simcoaster/coaster_collision.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_objects.hpp"
#include <stdexcept>

namespace simcoaster {
void world_operations::builtin::clear_park_object_coaster_geometry_peer_links(WorldRuntime& c, ParkObjectRuntimeView& object) {
    if (!c.resources.meshes) {
        throw std::logic_error("Coaster mesh runtime is not bound");
    }
    auto* world = c.resources.meshes->coasters();
    auto* binding = world ? world->find(object.mesh_instance_id) : nullptr;
    if (!binding) {
        throw std::out_of_range("Coaster geometry owner is missing");
    }
    //0x5E4790 resolves THIS object's mesh after closing the global active editor
    //context, then 0x48E780 clears all peer links and their pending rebuild
    //bits.
    clear_coaster_geometry_peers(*binding);
}
} //namespace simcoaster
