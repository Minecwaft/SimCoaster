#pragma once
#include "save_schema.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace simcoaster {
struct Stream;
struct DynamicTextureMeshBalance;

//Fixed-size wire records use explicit field order; x64 object padding is not saved.
struct CoasterRecordHeader {
    [[=save::Field { "packed_flags", 0 }]] std::uint32_t packed_flags {};
    [[=save::Field { "detail_state_flags", 1 }]] std::uint32_t detail_state_flags {};
    [[=save::Field { "object_index", 2 }]] std::uint16_t object_index {};
    [[=save::Field { "mesh_instance_id", 3 }]] std::uint16_t mesh_instance_id {};
    [[=save::Field { "map_id", 4 }]] std::uint16_t map_id {};
    [[=save::Field { "tile_x", 5 }]] std::int16_t tile_x {};
    [[=save::Field { "tile_y", 6 }]] std::int16_t tile_y {};
    [[=save::Field { "animation_speed", 7 }]] float animation_speed {};
    [[=save::Field { "geometry_count", 8 }]] std::uint16_t geometry_count {};
    [[=save::Field { "train_count", 9 }]] std::uint16_t train_count {};
    [[=save::Field { "peep_capacity_limit", 10 }]] std::uint16_t peep_capacity_limit {};
    [[=save::Field { "geometry_peer_reference_count", 11 }]] std::uint32_t geometry_peer_reference_count {};
};

struct CoasterGeometryHeader {
    [[=save::Field { "packed_flags", 0 }]] std::uint32_t packed_flags {};
    [[=save::Field { "mesh_slot", 1 }]] std::uint32_t mesh_slot {};
    [[=save::Field { "tile_x", 2 }]] std::int16_t tile_x {};
    [[=save::Field { "tile_y", 3 }]] std::int16_t tile_y {};
    [[=save::Field { "link_slot", 4 }]] std::uint8_t link_slot {};
    [[=save::Field { "loft", 5 }]] float loft {};
    [[=save::Field { "angle_a", 6 }]] float angle_a {};
    [[=save::Field { "angle_b", 7 }]] float angle_b {};
    [[=save::Field { "rotation", 8 }]] float rotation {};
    [[=save::Field { "wobble", 9 }]] float wobble {};
    [[=save::Field { "peer_count", 10 }]] std::uint8_t peer_count {};
};

struct CoasterTrainHeader {
    [[=save::Field { "flags", 0 }]] std::uint32_t flags {};
    [[=save::Field { "car_count", 1 }]] std::uint8_t car_count {};
    [[=save::Field { "distance", 2 }]] float distance {};
    [[=save::Field { "speed", 3 }]] float speed {};
    [[=save::Field { "transition_state", 4 }]] std::uint32_t transition_state {};
    [[=save::Field { "positioned_request_a", 5 }]] std::uint32_t positioned_request_a {};
    [[=save::Field { "positioned_request_b", 6 }]] std::uint32_t positioned_request_b {};
    [[=save::Field { "passenger_count", 7 }]] std::uint16_t passenger_count {};
};

struct CoasterCarHeader {
    [[=save::Field { "flags", 0 }]] std::uint32_t flags {};
    [[=save::Field { "person_count_a", 1 }]] std::uint8_t person_count_a {};
    [[=save::Field { "person_count_b", 2 }]] std::uint8_t person_count_b {};
    [[=save::Field { "passenger_capacity", 3 }]] std::uint8_t passenger_capacity {};
    [[=save::Field { "mesh_slot", 4 }]] std::uint32_t mesh_slot {};
};

static_assert(save::record_width<CoasterRecordHeader> == 32);
static_assert(save::record_width<CoasterGeometryHeader> == 34);
static_assert(save::record_width<CoasterTrainHeader> == 27);
static_assert(save::record_width<CoasterCarHeader> == 11);

struct CoasterGeometrySave {
    CoasterGeometryHeader header {};
    std::vector<std::uint16_t> peers;
};

struct CoasterCarSave {
    CoasterCarHeader header {};
    std::vector<std::uint32_t> persons_a, persons_b;
};

struct CoasterTrainSave {
    CoasterTrainHeader header {};
    std::vector<CoasterCarSave> cars;
};

struct CoasterRecordSave {
    CoasterRecordHeader header {};
    std::vector<std::uint16_t> root_peers;
    std::vector<CoasterGeometrySave> geometry;
    std::vector<CoasterTrainSave> trains;
};

struct CoasterSaveSection {
    std::uint32_t registered_record_count {}, available_id_holes {}, next_map_id {};
    std::vector<CoasterRecordSave> records;
};

struct CoasterTrainLimits {
    std::uint32_t trains {}, cars_per_train {};
};

//Native construction uses these defaults only after successfully building car types.
std::optional<CoasterTrainLimits> coaster_train_limits(const DynamicTextureMeshBalance&);
using CoasterLimitsResolver = std::function<std::optional<CoasterTrainLimits>(std::uint16_t mesh_instance_id)>;
//These functions transfer the saved snapshot, not the unported geometry/renderer
//reconstruction. The resolver must identify the already restored parent mesh.
bool read_coaster_save_section(Stream&, CoasterSaveSection&, const CoasterLimitsResolver&);
bool write_coaster_save_section(Stream&, const CoasterSaveSection&, const CoasterLimitsResolver&);
} //namespace simcoaster
