#pragma once
#include "application_session.hpp"

namespace simcoaster {
enum class ApplicationWorldEntryPresentation {
    none,
    tip_of_the_day,
    load_chooser,
    campaign_flyby
};

struct ApplicationWorldEntryResult {
    ApplicationWorldEntryPresentation presentation {};
    ThingId flyby_target {};
};

//7468B0->64B940 plus State10's first-prebuilt repair. The Session calls this
//ONLY after successfully loading the original first Science prebuilt and
//BEFORE writing restart/capturing campaign state or publishing the new World.
//Ordinary park restoration must never erase its employees/cash/history.
void reset_application_prebuilt_world(ParkRuntime&);
//6A43A0 ordinary source activation, after application UI has been bound.
//None (graphics rebind), Intro and Mode3 do not repeat startup presentation.
//The caller consumes its native MapSourceSelection+50 chooser latch and opens
//chooser(source4,hide-close1) or Tip.show(false) as recommended. A rejected
//Tip, missing flyby or day gate does NOT imply an initial-mission event.
ApplicationWorldEntryResult apply_application_world_entry(ParkRuntime&, ApplicationWorldEntry, bool post_load_chooser_pending = false);
} //namespace simcoaster
