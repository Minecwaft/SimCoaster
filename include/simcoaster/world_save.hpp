#pragma once

#include "world.hpp"
#include "stream.hpp"
#include "entity_catalog.hpp"
#include <span>
#include <string_view>

namespace simcoaster {
enum class WorldCellSaveMask : std::uint8_t {
    none = 0,
    map = 1,
    track = 2,
    region = 4
};

struct SaveServices {
    virtual ~SaveServices() = default;
    //Required input-system boundary: replace the active interaction with its
    //default handler, including lazy lifetime initialization and online-mode
    //substitution, while suppressing the default cursor refresh. No fallback.
    virtual void prepare_world_save(WorldRuntime&) = 0;
    virtual void field_header(std::uint32_t size, std::string_view label, bool writing) = 0;
    //The mobile codec requests a new path after successful read AND write, even
    //when schema>1 skips its own fields. The default runs the rebuilt router.
    virtual bool
    request_mobile_entity_path(WorldRuntime&, MapObjectBase&, MobileEntityState&, std::uint16_t x, std::uint16_t y);
    virtual void prepare_loaded_payload(WorldRuntime&, ThingHeader*, ThingKind);
    //The native dispatcher selects the codec before calling stream callbacks;
    //the payload's live kind can change independently during those callbacks.
    virtual int transfer_payload(WorldRuntime&,
                                 ThingKind selected_kind,
                                 ThingHeader*,
                                 Stream&,
                                 bool writing,
                                 std::uint32_t schema,
                                 std::uint32_t world_version);
};

//One transfer owns its stream/header boundary and immutable codec policy.
//Child codecs copy this small view; field callbacks still observe each native
//transfer in order, including partial reads and intentionally ignored failures.
struct SaveArchive {
    Stream& stream;
    SaveServices& services;
    const bool writing;
    const std::uint32_t schema = 1;

    template <class T>
    bool field(T&, std::string_view label) const;
    template <std::size_t First = 0, std::size_t Last = static_cast<std::size_t>(-1), class T>
    bool fields(T&) const;
    bool meter(float&, std::string_view label = "pv") const;

    template <class T>
    int record(T& value) const {
        return schema > 1 ? 0 : !fields(value);
    }

    template <class T>
    int record(T* value) const {
        return schema > 1 ? 0 : !fields(*value);
    }
    template <class Ring>
    int history_ring(Ring&) const;
};

//Complete World section, with the native success convention (true succeeds).
//The enclosing park-file container, compression and other sections are separate.
//Loading resets metadata at entry without freeing the previous payloads; caller
//teardown must precede it. Missing entity codecs throw rather than emit an
//incomplete successful save. Input-system preparation remains a required leaf.
bool serialize_world_state(WorldRuntime&, World&, PositionedStream&, SaveServices&);
bool deserialize_world_state(WorldRuntime&, World&, PositionedStream&, SaveServices&);

//Exactly the native used-thing list fragment: fixed World fields/cells precede
//it, SpecialRide registry follows. This is not a complete .TPWS container.
//Unreconstructed payload codecs remain required integration dependencies.
//Equivalence is tested with complete valid records. This reader rejects short
//raw marker transfers; the original sometimes compares partially written stack
//storage after a short read. That indeterminate malformed-input behavior is
//outside this source milestone's equivalence claim.
bool write_used_thing_list(WorldRuntime&, Stream&, SaveServices&, std::uint32_t version);
bool restore_used_thing_list(WorldRuntime&, Stream&, SaveServices&, std::uint32_t version);
bool transfer_map_object_base_state(MapObjectBase*, SaveArchive archive);
bool has_builtin_payload_codec(ThingKind);
void prepare_builtin_payload(WorldRuntime&, ThingHeader*, ThingKind);
int transfer_builtin_payload(WorldRuntime&, ThingHeader*, SaveArchive archive);
int transfer_builtin_payload_as(WorldRuntime&, ThingKind, ThingHeader*, SaveArchive archive);

int transfer_world_cell_base_fields(CellBase*, SaveArchive archive);
int transfer_world_map_cell_record(MapCell*, SaveArchive archive);
int transfer_world_track_cell_record(TrackCell*, SaveArchive archive);

int
transfer_world_map_cells(WorldRuntime&, World&, PositionedStream&, SaveServices&, bool writing, std::uint32_t schema);

int transfer_world_population_state(World&, SaveArchive archive);

int transfer_world_fixed_state_blocks(World&, SaveArchive archive);
int transfer_world_object_controls(ObjectControlTable&, SaveArchive archive);
int transfer_special_ride_registry(WorldRuntime&, SaveArchive archive);
ParkThingHistoryState* initialize_history_ring_buffers(ParkThingHistoryState*);

void initialize_park_analyser_for_restore(ParkAnalyserState&);
int transfer_park_analyser_grid_record(AnalyserGridRecord&, SaveArchive archive);
int transfer_park_analyser_state(ParkAnalyserState&, SaveArchive archive);
int transfer_park_thing_history_state(ParkThingHistoryState&, SaveArchive archive);
int transfer_park_thing_state(WorldRuntime&, ParkObjectRuntimeView&, SaveArchive archive);
} //namespace simcoaster
