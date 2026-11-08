#include "world_operation_generation.hpp"

namespace simcoaster::world_operations {
consteval {
    std::meta::queue_injection(world_operation_detail::definitions(false));
}
} //namespace simcoaster::world_operations
