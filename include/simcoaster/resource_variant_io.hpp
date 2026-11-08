#pragma once
#include "resource_variant.hpp"

namespace simcoaster {
struct Stream;
//Original Scripts wire format: occupancy DWORDs and 280-byte records. Runtime
//pointers and cached render commands are rebuilt, never imported as pointers.
bool serialize_resource_variant_pool(WorldRuntime&, Stream&);
bool deserialize_resource_variant_pool(WorldRuntime&, Stream&);
bool write_resource_variant_save_section(WorldRuntime&, Stream&);
bool restore_resource_variant_save_section(WorldRuntime&, Stream&);
} //namespace simcoaster
