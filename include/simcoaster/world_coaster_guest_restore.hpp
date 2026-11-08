#pragma once
#include "world_domains_fwd.hpp"

namespace simcoaster {
struct WorldCoasterBinding;

//Game.exe 0x478090: recover ride-controlled visitors absent from all train
//cars. Repeated host calls also preserve existing queue membership without
//duplicates.
void queue_ride_peep_ids_absent_from_train_cars(AttractionWorld&, WorldCoasterBinding&);

//Rebuild compatibility repair: the native Coaster wire format omits both
//passenger queues. Call once after restoring World, RSSE and coaster owner
//bindings. Reapply the native lifecycle8 drain for already-closing rides so
//recovered exit waiters can leave. These post-load invocations are host
//repairs; recovery by the original loader has not been established.
void reconstruct_coaster_guest_queues(AttractionWorld&);
} //namespace simcoaster
