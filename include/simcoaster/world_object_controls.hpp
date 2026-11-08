#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
ObjectControlRecord* find_and_cache_world_object_control(ObjectControlTable&, std::uint16_t);
ObjectControlRecord* find_world_object_control(ObjectControlTable&, std::uint16_t);
int populate_missing_world_object_controls(AttractionWorld&, ObjectControlTable&);
int reset_world_object_controls(AttractionWorld&, ObjectControlTable&);
} //namespace simcoaster
