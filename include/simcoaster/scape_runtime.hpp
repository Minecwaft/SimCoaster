#pragma once
#include "campaign_profile.hpp"
#include "sound_runtime.hpp"
#include <filesystem>

namespace simcoaster {
enum class ScapeOmpRecordKind : std::uint32_t {
    ambient = 1,
    particle = 2,
    ambient_area = 3
};

//6A6110 reads a zero-filled60-byte prefix of each declared-width record.
//Unknown kinds and trailing bytes are retained as data and perform no action.
struct ScapeOmpRecord {
    [[=save::Field { "kind", 0 }]] ScapeOmpRecordKind kind {};
    [[=save::Field { "option_or_template", 1 }]] std::uint32_t option_or_template {};
    [[=save::Field { "event", 2 }]] std::uint32_t event_id {};
    [[=save::Field { "position", 3 }]] std::array<std::int32_t, 3> position {};
    [[=save::Field { "radius_parameter", 4 }]] std::int32_t radius_parameter {};
    [[=save::Field { "area_width", 5 }]] std::int32_t area_width {};
    [[=save::Field { "area_depth", 6 }]] std::int32_t area_depth {};
    [[=save::Field { "unknown_tail", 7 }]] std::array<std::uint32_t, 6> unknown_tail {};
};

static_assert(save::value_width<ScapeOmpRecord> == 60);

struct ScapeOmpFile {
    std::uint32_t record_width {};
    std::vector<ScapeOmpRecord> records;
};

struct ScapeOmpError {
    std::size_t offset {};
    std::string reason;
};

std::expected<ScapeOmpFile, ScapeOmpError> parse_scape_omp(std::span<const std::byte>);
std::expected<ScapeOmpFile, ScapeOmpError> load_scape_omp(const std::filesystem::path&);

//One live source World. Its SoundRuntime must outlive this owner. Particles
//belong to the World particle pool, while every nonzero created sound target
//is recycled here before its source backend is destroyed. Importing campaign
//handles never installs an old encoded ID into this backend.
class ScapeOmpRuntime {
    WorldRuntime& context_;
    SoundRuntime& audio_;
    std::uint32_t source_ {};
    bool loaded_ {};
    std::vector<std::uint32_t> sounds_, particles_;
    //Native live-list traversal order, opposite CampaignAuxiliaryState wire
    //order. Only local type1 records participate in the ATM mute/restore list.
    std::vector<CampaignAmbientRecord> retained_;
    std::vector<std::string> diagnostics_;
    void upsert(std::uint32_t event, std::uint32_t handle);

public:
    ScapeOmpRuntime(WorldRuntime&, SoundRuntime&, std::uint32_t resource_source_index);
    ~ScapeOmpRuntime();
    ScapeOmpRuntime(const ScapeOmpRuntime&) = delete;
    ScapeOmpRuntime& operator=(const ScapeOmpRuntime&) = delete;
    void load(const ScapeOmpFile&);
    void load(const std::filesystem::path&);
    //Full saves already restored their particles and sound graph. Adopt only
    //this source's validated live local ambience handles, without loading OMP.
    void adopt_saved_ambient(std::span<const CampaignAmbientRecord> native_wire_order);
    void set_retained_ambient_level(std::uint32_t percent);

    std::span<const CampaignAmbientRecord> retained_ambient() const {
        return retained_;
    }

    //Preserve imported native wire order and unrelated source records. Rebind
    //this source by (event,source), clear rebuilt legacy links, and append any
    //newly introduced local entries in native write order. Unresolved entries
    //receive handle0, never a potentially reused imported handle.
    std::vector<CampaignAmbientRecord> remap_imported_ambient(std::span<const CampaignAmbientRecord>) const;

    std::span<const std::uint32_t> particle_handles() const {
        return particles_;
    }

    const std::vector<std::string>& diagnostics() const {
        return diagnostics_;
    }
};
} //namespace simcoaster
