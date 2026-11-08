#include "simcoaster/world_save.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_weather.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_special_rides.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_save_fields.hpp"
#include <bit>
#include <cstring>
#include <bitset>
#include <memory>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
static_assert(std::endian::native == std::endian::little);
constexpr std::uint32_t list_start = 1129595205;
constexpr std::uint32_t record_start = 1414349900;
constexpr std::uint32_t list_end = 1146048069;

bool transfer(Stream& stream, bool writing, void* value, std::size_t size) {
    if (writing)
        return stream.write({ static_cast<const std::byte*>(value), size }) == static_cast<std::int32_t>(size);
    return stream.read({ static_cast<std::byte*>(value), size }) == static_cast<std::int32_t>(size);
}
} //namespace

static_assert(save::record_width<CellBaseFields> == 29 && save::record_width<MapCell> == 27);
static_assert(save::record_width<TrackCell> == 2 && save::record_width<RegionEffectCell> == 10);
static_assert(save::value_width<CellRenderData> == 12);

int transfer_world_cell_base_fields(CellBase* cell, SaveArchive archive) {
    if (archive.schema > 1)
        return 0;
    return !archive.fields(cell->common);
}

int transfer_world_map_cell_record(MapCell* cell, SaveArchive archive) {
    //Native map/track wrappers deliberately discard the shared transfer result.
    (void)transfer_world_cell_base_fields(&cell->base, archive);
    if (archive.schema > 1)
        return 0;
    return !archive.fields(*cell);
}

int transfer_world_track_cell_record(TrackCell* cell, SaveArchive archive) {
    (void)transfer_world_cell_base_fields(&cell->base, archive);
    if (archive.schema > 1)
        return 0;
    return !archive.fields(*cell);
}

int transfer_world_map_cells(WorldRuntime& c,
                             World& storage,
                             PositionedStream& stream,
                             SaveServices& services,
                             bool writing,
                             std::uint32_t schema) {
    const SaveArchive archive{ stream, services, writing, schema };
    MapCell default_map{};
    default_map.base.record_kind = CellStorageKind::map;
    default_map.base.index = 16959;
    TrackCell default_track{};
    default_track.base.record_kind = CellStorageKind::track;
    (void)stream.position();
    if (schema > 1) {
        if (writing)
            (void)stream.position();
        return 0;
    }
    constexpr auto map_bit = std::to_underlying(WorldCellSaveMask::map),
                   track_bit = std::to_underlying(WorldCellSaveMask::track),
                   region_bit = std::to_underlying(WorldCellSaveMask::region);
    for (std::size_t i = 0; i < map_cell_count; ++i) {
        std::uint8_t mask = 0;
        if (writing) {
            default_map.status_flags = storage.map_cells[i].status_flags;
            default_map.base.index = static_cast<CellId>(i + 1);
            apply_world_map_cell_status_flags(c, &default_map, false);
            default_track.base.index = static_cast<CellId>(i + 1);
            reset_world_track_cell_defaults(c, &default_track);
            //Native map/track predicates are literal TRUE stubs. Their default
            //preparation still has real side effects and precedes mask output.
            mask = map_bit | track_bit;
            const auto& effects = storage.region_cells[i];
            if (effects.happiness || effects.illness || effects.hunger || effects.security || effects.attraction)
                mask |= region_bit;
        }
        if (!archive.field(mask, "save_status_byte"))
            return 1;
        if ((mask & map_bit) && transfer_world_map_cell_record(&storage.map_cells[i], archive))
            return 1;
        if ((mask & track_bit) && transfer_world_track_cell_record(&storage.track_cells[i], archive))
            return 1;
        if ((mask & region_bit) && archive.record(&storage.region_cells[i]))
            return 1;
    }
    if (writing)
        (void)stream.position();
    else
        for (auto& row : storage.grid_change_serials)
            for (auto& value : row)
                value = 0;
    return 0;
}

static_assert(save::record_width<StaffCandidate> == 31 && save::record_width<ArrivalState> == 18);

int transfer_world_population_state(World& world, SaveArchive archive) {
    if (archive.schema > 1)
        return 0;
    for (auto& candidate : world.staff_candidates)
        if (archive.record(&candidate))
            return 1;
    auto& pool = world.staff_pool;
    for (std::size_t i = 0; i < pool.linked_count.size(); ++i)
        if (!archive.field(pool.linked_count[i], "mPeopleInCat[i]")
            || !archive.field(pool.stop_producing[i], "mStopProducing[i]"))
            return 1;
    return !archive.field(pool.last_update_tick, "mTimeSig")
        || !archive.field(pool.opening_pool_generated, "mOpeningStaffPoolGenerated");
}

int transfer_world_fixed_state_blocks(World& world, SaveArchive archive) {
    return transfer_world_population_state(world, archive)
        || archive.record(&world.calendar)
        || archive.record(&world.arrival);
}

static_assert(save::value_width<ObjectControlRecord> == 36 && save::record_width<ObjectControlTable> == 6);

int transfer_world_object_controls(ObjectControlTable& table, SaveArchive archive) {
    if (archive.schema > 1)
        return 0;
    for (auto& row : table.rows)
        if (!archive.field(row, "mObjectControls[i]"))
            return 1;
    if (!archive.fields(table))
        return 1;
    if (!archive.writing) {
        table.cached_record = nullptr;
        table.cached_definition_id = 0;
    }
    return 0;
}

static_assert(save::record_width<WorldTail> == 66 && save::record_width<SpecialRideRegistryMember> == 10);

int transfer_special_ride_registry(WorldRuntime& c, SaveArchive archive) {
    if (!archive.field(c.attractions.special_ride_group_count, "mNumSpecialRides"))
        return 1;
    for (std::int32_t i = 0; i < std::bit_cast<std::int32_t>(c.attractions.special_ride_group_count); ++i) {
        auto& group = c.attractions.special_ride_groups.at(static_cast<std::size_t>(i));
        auto id = group.thing_id; //Native writer snapshots the ID before its header.
        if (!archive.field(id, "SpecialRideThingId"))
            return 1;
        if (!archive.writing)
            populate_special_ride_registry_entry(c, group, id);
        for (auto& member : group.members)
            if (!archive.fields(member))
                return 1;
    }
    return 0;
}

ParkThingHistoryState* initialize_history_ring_buffers(ParkThingHistoryState* history) {
    const auto initialize = [](auto& ring) {
        ring.temporary = 0;
        ring.current_entry = -1;
        ring.num_entries = static_cast<std::int32_t>(ring.data.size());
        ring.wrapped = 0;
    };
    for (auto& ring : history->dword_history_30)
        initialize(ring);
    for (auto& ring : history->dword_history_144)
        initialize(ring);
    return history;
}

void initialize_park_analyser_for_restore(ParkAnalyserState& analyser) {
    for (auto& cell : analyser.grid)
        for (std::size_t i = 0; i < cell.metrics.size(); ++i)
            cell.metrics[i] = { 0, std::bit_cast<std::uint32_t>(std::int32_t{ i < 2 ? -998653952 : -999 }), 0 };
    for (auto& metric : analyser.metrics)
        metric = { 0, std::bit_cast<std::uint32_t>(std::int32_t{ -999 }), 0 };
    const auto initialize = [](auto& ring) {
        ring.temporary = 0;
        ring.current_entry = -1;
        ring.num_entries = static_cast<std::int32_t>(ring.data.size());
        ring.wrapped = 0;
    };
    for (auto& ring : analyser.short_histories)
        initialize(ring);
    for (auto& ring : analyser.long_histories)
        initialize(ring);
    initialize(analyser.medium_history);
    initialize(analyser.tiny_history);
    for (auto& ring : analyser.byte_histories)
        initialize(ring);
    analyser.last_strike = analyser.last_puke = analyser.last_prank = analyser.park_last_opened =
        analyser.last_object_built = analyser.last_staff_hired = 0;
}

int transfer_park_analyser_grid_record(AnalyserGridRecord& cell, SaveArchive archive) {
    if (archive.schema > 1)
        return 0;
    if (!archive.fields(cell))
        return 1;
    for (auto& metric : cell.metrics)
        if (!archive.fields(metric))
            return 1;
    return 0;
}

int transfer_park_analyser_state(ParkAnalyserState& analyser, SaveArchive archive) {
    if (!transfer_map_object_base_state(&analyser.base, archive))
        return 1;
    if (archive.schema > 1)
        return 0;
    if (!archive.fields<0, 5>(analyser))
        return 1;
    for (auto& cell : analyser.grid)
        if (transfer_park_analyser_grid_record(cell, archive))
            return 1;
    for (auto& ring : analyser.short_histories)
        if (archive.history_ring(ring))
            return 1;
    for (auto& metric : analyser.metrics)
        if (!archive.fields(metric))
            return 1;
    //This permutation is part of the original wire format.
    constexpr std::array<unsigned, 21> order{
        0, 2, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
    };
    for (auto i : order)
        if (archive.history_ring(analyser.long_histories[i]))
            return 1;
    if (archive.history_ring(analyser.medium_history) || archive.history_ring(analyser.tiny_history))
        return 1;
    for (auto& ring : analyser.byte_histories)
        if (archive.history_ring(ring))
            return 1;
    return !archive.fields<5>(analyser);
}

int transfer_park_thing_history_state(ParkThingHistoryState& history, SaveArchive archive) {
    for (auto& ring : history.dword_history_30)
        if (archive.history_ring(ring))
            return 1;
    for (auto& ring : history.dword_history_144)
        if (archive.history_ring(ring))
            return 1;
    return 0;
}

static_assert(!save::supported_value<ParkObjectRuntimeView> && !save::supported_value<ParkThingDwordHistory30>);

int transfer_park_thing_state(WorldRuntime& c, ParkObjectRuntimeView& object, SaveArchive archive) {
    if (!transfer_map_object_base_state(&object.base, archive))
        return 1;
    if (archive.schema > 1)
        return 0;
    if (!archive.fields<0, 2>(object))
        return 1;
    std::array<std::uint32_t, 8> components{};
    std::int32_t status;
    auto creation = std::bit_cast<FileTime>(object.creation_file_time);
    if (archive.writing) {
        convert_filetime_to_system_time_fields(&creation,
                                               &status,
                                               &components[0],
                                               &components[1],
                                               &components[2],
                                               &components[3],
                                               &components[4],
                                               &components[5],
                                               &components[6],
                                               &components[7]);
        if (status)
            throw std::domain_error(
                "park-object creation time cannot be converted"); //Native emits indeterminate stack data on failure.
    }
    for (auto& component : components)
        if (!archive.field(component, "tv[t]"))
            return 1;
    if (!archive.writing) {
        convert_system_time_fields_to_filetime(&creation,
                                               &status,
                                               static_cast<std::uint16_t>(components[0]),
                                               static_cast<std::uint16_t>(components[1]),
                                               static_cast<std::uint16_t>(components[2]),
                                               static_cast<std::uint16_t>(components[4]),
                                               static_cast<std::uint16_t>(components[5]),
                                               static_cast<std::uint16_t>(components[6]),
                                               static_cast<std::uint16_t>(components[7]));
        object.creation_file_time =
            std::bit_cast<std::uint64_t>(creation); //Invalid components retain the prior timestamp.
    }
    if (!archive.fields<2, 4>(object))
        return 1;
    for (std::size_t i = 0; i < object.name_a.size(); ++i)
        if (!archive.field(object.name_a[i], "mNameA[i]") || !archive.field(object.name_b[i], "mNameB[i]"))
            return 1;
    if (!archive.fields<4, 31>(object))
        return 1;
    for (auto* value : { &object.pv, &object.working_life, &object.repair_state })
        if (!archive.meter(*value))
            return 1;
    if (!archive.fields<31>(object))
        return 1;
    auto has_history = static_cast<std::uint8_t>(object.history != nullptr);
    if (!archive.field(has_history, "HasHistory"))
        return 1;
    if (!has_history) {
        if (!archive.writing)
            object.history = nullptr;
        return 0;
    }
    if (!archive.writing) {
        auto* storage = static_cast<ParkThingHistoryState*>(
            c.ports.memory.allocate(Allocation::park_history, sizeof(ParkThingHistoryState)));
        if (!storage)
            throw std::bad_alloc();
        //Fresh native sample/padding bytes are unspecified. Define them as zero;
        //the separate initializer still preserves samples in supplied objects.
        object.history = initialize_history_ring_buffers(std::construct_at(storage));
    }
    return transfer_park_thing_history_state(*object.history, archive) != 0;
}

bool write_used_thing_list(WorldRuntime& c, Stream& stream, SaveServices& services, std::uint32_t version) {
    const SaveArchive archive{ stream, services, true };
    //Native dereferences active_head: initialized Worlds contain manager records.
    auto& head = c.entities.active_head->id_storage;
    if (!archive.field(head, "Used Thing Head"))
        return false;
    auto* entry = c.entities.active_head; //Snapshot before the raw list marker's callback.
    auto marker = list_start;
    (void)transfer(stream, true, &marker, sizeof marker); //Original raw write ignores BOOL failure.
    for (; entry; entry = entry->active_next) {
        auto next = entry->active_next ? entry->active_next->id_storage : 0;
        if (!archive.field(next, "Used Thing Next"))
            return false;
        auto kind = static_cast<std::uint32_t>(entry->payload->kind);
        if (!archive.field(kind, "thingmodel"))
            return false;
        marker = record_start;
        (void)transfer(stream, true, &marker, sizeof marker);
        const auto selected_kind = static_cast<ThingKind>(kind);
        if (const auto* descriptor = record_descriptor(selected_kind)) {
            marker = descriptor->stream_token;
            (void)transfer(stream, true, &marker, sizeof marker);
            if (services.transfer_payload(c, selected_kind, entry->payload, stream, true, 1, version))
                return false;
        }
    }
    marker = list_end;
    (void)transfer(stream, true, &marker, sizeof marker);
    return true;
}

static bool
restore_used_thing_list_after_reset(WorldRuntime& c, Stream& stream, SaveServices& services, std::uint32_t version) {
    const SaveArchive archive{ stream, services, false };
    std::uint32_t head;
    if (!archive.field(head, "Used Thing Head"))
        return false;
    if (head == 0 || head >= c.entities.size())
        return false;
    auto* entry = &c.entities[head];
    c.entities.active_head = entry;
    std::bitset<thing_slot_count> visited;
    ThingMetadata* previous = nullptr;
    std::uint32_t marker;
    if (!transfer(stream, false, &marker, sizeof marker) || marker != list_start)
        return false;
    do {
        visited.set(entry->id_storage);
        std::uint32_t next;
        if (!archive.field(next, "Used Thing Next"))
            return false;
        if (next >= c.entities.size() || (next != 0 && visited.test(next)))
            return false;
        entry->active_next = next ? &c.entities[next] : nullptr;
        entry->active_previous = previous;
        entry->free_next = nullptr;
        std::uint32_t kind_value;
        if (!archive.field(kind_value, "thingmodel"))
            return false;
        if (!transfer(stream, false, &marker, sizeof marker) || marker != record_start)
            return false;
        const auto kind = static_cast<ThingKind>(static_cast<std::uint8_t>(kind_value));
        //Native switch uses full DWORD, before narrowing the payload tag.
        const auto* descriptor = kind_value <= 255 ? record_descriptor(kind) : nullptr;
        if (descriptor) {
            if (!transfer(stream, false, &marker, sizeof marker) || marker != descriptor->stream_token)
                return false;
            auto* payload =
                static_cast<ThingHeader*>(c.entities.allocate_payload(c.ports.memory, descriptor->allocation_size));
            if (!payload)
                return false;
            const auto release = [&](ThingHeader* pointer) {
                if (is_staff_record_kind(kind))
                    release_staff_membership_storage(c, *reinterpret_cast<StaffRuntimeRecord*>(pointer));
                if (kind == ThingKind::tag_system)
                    release_tag_system_resources(c, reinterpret_cast<TagSystemRecord*>(pointer));
                if (kind == ThingKind::park_object) {
                    auto* object = reinterpret_cast<ParkObjectRuntimeView*>(pointer);
                    if (object->history)
                        c.ports.memory.release(Allocation::park_history, object->history);
                }
                c.entities.release_payload(c.ports.memory, pointer);
            };
            std::unique_ptr<ThingHeader, decltype(release)> owner(payload, release);
            descriptor->construct(payload);
            services.prepare_loaded_payload(c, payload, kind);
            payload->kind = kind;
            payload->id = static_cast<ThingId>(entry->id_storage);
            if (services.transfer_payload(c, kind, payload, stream, false, 1, version))
                return false;
            entry->allocation_kind = kind;
            entry->payload = owner.release();
        } else
            return false;
        previous = entry;
        entry = entry->active_next;
    } while (entry);
    if (!transfer(stream, false, &marker, sizeof marker) || marker != list_end)
        return false;
    rebuild_used_thing_free_list(c);
    return true;
}

bool restore_used_thing_list(WorldRuntime& c, Stream& stream, SaveServices& services, std::uint32_t version) {
    reset_used_thing_record_table(c); //Does not free payloads; caller tears down first.
    return restore_used_thing_list_after_reset(c, stream, services, version);
}

bool serialize_world_state(WorldRuntime& c, World& world, PositionedStream& stream, SaveServices& services) {
    const SaveArchive archive{ stream, services, true };
    (void)stream.position();
    services.prepare_world_save(c);
    std::uint32_t version = 3;
    if (!archive.field(version, "version") || !archive.fields(world.saved))
        return false;
    (void)stream.position();
    (void)stream.position();
    if (transfer_world_object_controls(world.object_controls, archive))
        return false;
    (void)stream.position();
    (void)stream.position();
    if (transfer_world_fixed_state_blocks(world, archive))
        return false;
    (void)stream.position();
    (void)stream.position();
    if (transfer_world_map_cells(c, world, stream, services, true, 1))
        return false;
    (void)stream.position();
    (void)stream.position();
    if (!write_used_thing_list(c, stream, services, version))
        return false;
    (void)stream.position();
    if (transfer_special_ride_registry(c, archive))
        return false;
    (void)stream.position();
    return true;
}

bool deserialize_world_state(WorldRuntime& c, World& world, PositionedStream& stream, SaveServices& services) {
    const SaveArchive archive{ stream, services, false };
    reset_used_thing_record_table(c);
    std::uint32_t version = 0;
    if (!archive.field(version, "version") || !archive.fields(world.saved))
        return false;
    if (transfer_world_object_controls(world.object_controls, archive)
        || transfer_world_fixed_state_blocks(world, archive)
        || transfer_world_map_cells(c, world, stream, services, false, 1)
        || !restore_used_thing_list_after_reset(c, stream, services, version)
        || transfer_special_ride_registry(c, archive))
        return false;
    //Native6873632/6873600 only store this flag, after complete success. The
    //receiver's live state is used even if callbacks rebind the global World.
    c.resources.sprite_resource_rendering_enabled = world.saved.state == WorldState::transition ? 0 : 1;
    return true;
}

bool transfer_map_object_base_state(MapObjectBase* record, SaveArchive archive) {
    if (archive.schema > 1)
        return true;
    if (!archive.fields(*record))
        return false;
    if (!archive.writing)
        record->header.retirement_pending = 0;
    return true;
}

void SaveServices::prepare_loaded_payload(WorldRuntime& c, ThingHeader* record, ThingKind kind) {
    if (!has_builtin_payload_codec(kind))
        throw std::logic_error("payload initializer has not been reconstructed");
    prepare_builtin_payload(c, record, kind);
}

int SaveServices::transfer_payload(WorldRuntime& c,
                                   ThingKind kind,
                                   ThingHeader* record,
                                   Stream& stream,
                                   bool writing,
                                   std::uint32_t schema,
                                   std::uint32_t world_version) {
    (void)world_version; //Implemented native codecs do not consume this argument.
    if (!has_builtin_payload_codec(kind))
        throw std::logic_error("payload codec has not been reconstructed");
    return transfer_builtin_payload_as(c, kind, record, SaveArchive{ stream, *this, writing, schema });
}

int transfer_builtin_payload(WorldRuntime& c, ThingHeader* record, SaveArchive archive) {
    return transfer_builtin_payload_as(c, record->kind, record, archive);
}
} //namespace simcoaster
