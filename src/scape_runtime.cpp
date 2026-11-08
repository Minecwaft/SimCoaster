#include "simcoaster/numeric_word.hpp"
#include "simcoaster/scape_runtime.hpp"
#include <format>
#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
template <class T>
T read(std::span<const std::byte> bytes, std::size_t offset) {
    T result {};
    save::detail::decode<T>(std::span<const std::byte, save::value_width<T>> { bytes.data() + offset, save::value_width<T> }, result);
    return result;
}

std::array<float, 3> sound_position(const ScapeOmpRecord& record) {
    //652A90/652860 accept signed integer positions, so division truncates
    //toward zero BEFORE the request converts the result to float.
    return { static_cast<float>(record.position[0] / 1024), static_cast<float>(record.position[1] / 1024), static_cast<float>(record.position[2] / 1024) };
}

SoundTargetXZBounds area(const ScapeOmpRecord& record) {
    const auto x = record.position[0] / 1024;
    const auto z = record.position[2] / 1024;
    return { x, z, numeric::add(x, record.area_width / 1024), numeric::add(z, record.area_depth / 1024) };
}
} //namespace

std::expected<ScapeOmpFile, ScapeOmpError> parse_scape_omp(std::span<const std::byte> bytes) {
    if (bytes.size() < 12) {
        return std::unexpected(ScapeOmpError { bytes.size(), "Truncated OBJ_ header" });
    }
    constexpr std::array signature { std::byte { 'O' }, std::byte { 'B' }, std::byte { 'J' }, std::byte { '_' } };
    if (!std::ranges::equal(bytes.first(4), signature)) {
        return std::unexpected(ScapeOmpError { 0, "Expected OBJ_ signature" });
    }
    const auto count = read<std::int32_t>(bytes, 4);
    const auto width = read<std::uint32_t>(bytes, 8);
    if (count < 0 || count > 1048576) {
        return std::unexpected(ScapeOmpError { 4, "Invalid Scape record count" });
    }
    if (width && static_cast<std::uint32_t>(count) > (bytes.size() - 12) / width) {
        return std::unexpected(ScapeOmpError { 12, "Truncated Scape record table" });
    }
    ScapeOmpFile result { width, {} };
    result.records.reserve(static_cast<std::size_t>(count));
    std::size_t offset = 12;
    for (std::int32_t index = 0; index < count; ++index) {
        std::array<std::byte, save::value_width<ScapeOmpRecord>> prefix {};
        std::ranges::copy(bytes.subspan(offset, std::min<std::size_t>(width, prefix.size())), prefix.begin());
        ScapeOmpRecord record;
        save::detail::decode<ScapeOmpRecord>(prefix, record);
        result.records.push_back(record);
        offset += width;
    }
    //Installed Intro Scape.omp has0 records and additional trailing bytes;
    //native6A6110 closes after the declared records without interpreting them.
    return result;
}

std::expected<ScapeOmpFile, ScapeOmpError> load_scape_omp(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return std::unexpected(ScapeOmpError { 0, "Cannot open " + path.native_encoded_string() });
    }
    const auto length = input.tellg();
    if (length < 0 || length > 67108864) {
        return std::unexpected(ScapeOmpError { 0, "Invalid Scape file size" });
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), length)) {
        return std::unexpected(ScapeOmpError { 0, "Cannot read " + path.native_encoded_string() });
    }
    return parse_scape_omp(bytes);
}

ScapeOmpRuntime::ScapeOmpRuntime(WorldRuntime& context, SoundRuntime& audio, std::uint32_t source) : context_(context), audio_(audio), source_(source) {
    if (source > 3) {
        throw std::invalid_argument("Invalid Scape resource source index");
    }
}

ScapeOmpRuntime::~ScapeOmpRuntime() {
    for (const auto handle : sounds_) {
        audio_.recycle(handle);
    }
}

void ScapeOmpRuntime::upsert(std::uint32_t event, std::uint32_t handle) {
    const auto found = std::ranges::find_if(retained_, [&](const auto& entry) {
        return entry.event_id == event && entry.resource_source_index == source_;
    });
    if (found != retained_.end()) {
        found->sound_handle = handle;
    } else {
        retained_.insert(retained_.begin(), { event, handle, source_, 0 });
    }
}

void ScapeOmpRuntime::load(const std::filesystem::path& path) {
    const auto file = load_scape_omp(path);
    if (!file) {
        throw std::runtime_error(std::format("Original Scape byte {}: {}", file.error().offset, file.error().reason));
    }
    load(*file);
}

void ScapeOmpRuntime::load(const ScapeOmpFile& file) {
    if (loaded_) {
        throw std::logic_error("Scape runtime records are initialized once per source World");
    }
    //Reject malformed area ordering before any audio/particle mutation. The
    //native clamp consumer requires min<=max; every shipped type3 satisfies it.
    for (const auto& record : file.records) {
        if (record.kind == ScapeOmpRecordKind::ambient_area) {
            const auto bounds = area(record);
            if (bounds.min_x > bounds.max_x || bounds.min_z > bounds.max_z) {
                throw std::invalid_argument("Invalid Scape ambient-area dimensions");
            }
        }
    }
    sounds_.reserve(file.records.size());
    particles_.reserve(file.records.size());
    retained_.reserve(file.records.size());
    loaded_ = true;
    for (std::size_t index = 0; index < file.records.size(); ++index) {
        const auto& record = file.records[index];
        if (record.kind == ScapeOmpRecordKind::particle) {
            const auto handle = context_.ports.effects.create_particle_effect(context_, record.option_or_template, record.position, 0);
            particles_.push_back(handle);
            if (!handle) {
                diagnostics_.push_back(std::format("Scape particle record {} did not create a live effect", index));
            }
            continue;
        }
        if (record.kind != ScapeOmpRecordKind::ambient && record.kind != ScapeOmpRecordKind::ambient_area) {
            continue;
        }
        const auto catalog = record.option_or_template ? context_.resources.local_ambient_catalog_slot : context_.resources.ambient_catalog_slot;
        //Native multiplication wraps as a DWORD before signed division.
        const auto parameter = record.kind == ScapeOmpRecordKind::ambient ? static_cast<float>(numeric::add(record.radius_parameter, record.radius_parameter) / 1024) : 0.f;
        const auto handle = audio_.submit(0, catalog, record.event_id, sound_position(record), parameter);
        if (handle) {
            sounds_.push_back(handle);
        } else {
            diagnostics_.push_back(std::format("Scape sound record {} returned no retained target handle (catalog {}, event {})", index, catalog, record.event_id));
        }
        if (record.kind == ScapeOmpRecordKind::ambient) {
            if (record.option_or_template) {
                upsert(record.event_id, handle);
            }
        } else if (handle && !audio_.set_xz_bounds(handle, area(record))) {
            throw std::runtime_error("Scape ambient-area target became unavailable");
        }
    }
}

void ScapeOmpRuntime::set_retained_ambient_level(std::uint32_t percent) {
    for (const auto& entry : retained_) {
        if (entry.resource_source_index == source_ && entry.sound_handle) {
            audio_.set_level(entry.sound_handle, percent);
        }
    }
}

void ScapeOmpRuntime::adopt_saved_ambient(std::span<const CampaignAmbientRecord> imported) {
    if (loaded_) {
        throw std::logic_error("Scape ambience is initialized once per source World");
    }
    const auto snapshot = audio_.snapshot();
    if (!snapshot) {
        throw std::runtime_error(describe_sound_error(snapshot.error()));
    }
    const auto local = audio_.catalogs().catalog(context_.resources.local_ambient_catalog_slot);
    retained_.reserve(imported.size());
    sounds_.reserve(imported.size());
    for (auto it = imported.rbegin(); it != imported.rend(); ++it) {
        if (it->resource_source_index != source_) {
            continue;
        }
        auto entry = *it;
        entry.legacy_link_word = 0;
        bool valid = false;
        if (entry.sound_handle && local && !local->effects.groups.empty()) {
            const auto found = std::ranges::find(snapshot->targets, entry.sound_handle, &SoundSavedTargetChain::id);
            if (found != snapshot->targets.end() && !found->nodes.empty()) {
                const auto& fields = found->nodes.front().fields;
                const auto& descriptors = local->effects.groups.front().descriptors;
                valid = fields.catalog == context_.resources.local_ambient_catalog_slot && fields.descriptor < descriptors.size() && &descriptors[fields.descriptor] == local->find(entry.event_id);
            }
        }
        if (!valid && entry.sound_handle) {
            diagnostics_.push_back(std::format("Saved local ambience event {} has no matching live source/catalog target", entry.event_id));
            entry.sound_handle = 0;
        }
        if (entry.sound_handle && std::ranges::find(sounds_, entry.sound_handle) == sounds_.end()) {
            sounds_.push_back(entry.sound_handle);
        }
        retained_.push_back(entry);
    }
    loaded_ = true;
}

std::vector<CampaignAmbientRecord> ScapeOmpRuntime::remap_imported_ambient(std::span<const CampaignAmbientRecord> imported) const {
    std::vector<CampaignAmbientRecord> result(imported.begin(), imported.end());
    for (auto& entry : result) {
        if (entry.resource_source_index != source_) {
            continue;
        }
        const auto found = std::ranges::find_if(retained_, [&](const auto& live) {
            return live.event_id == entry.event_id;
        });
        entry.sound_handle = found == retained_.end() ? 0 : found->sound_handle;
        entry.legacy_link_word = 0;
    }
    //Native new entries prepend to the live list and therefore append on wire.
    for (auto it = retained_.rbegin(); it != retained_.rend(); ++it) {
        if (std::ranges::none_of(result, [&](const auto& entry) {
                return entry.event_id == it->event_id && entry.resource_source_index == it->resource_source_index;
            })) {
            result.push_back(*it);
        }
    }
    return result;
}
} //namespace simcoaster
