#include "simcoaster/world_startup.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace simcoaster {
void initialize_world_map_and_track_storage(World& world, const WorldStartupBalance& balance) {
    world.map_cells.fill({});
    world.track_cells.fill({});
    for (auto& row : world.grid_change_serials)
        std::fill(std::begin(row), std::end(row), 0u);
    for (std::size_t x = 0; x < 128; ++x)
        for (std::size_t y = 0; y < 128; ++y) {
            const auto index = x + y * 128;
            const auto id = static_cast<CellId>(index + 1);
            world.map_cells[index].base.index = id;
            world.map_cells[index].base.record_kind = CellStorageKind::map;
            world.track_cells[index].base.index = id;
            world.track_cells[index].base.record_kind = CellStorageKind::track;
        }
    const auto word = [](std::int32_t value) {
        return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
    };
    for (std::size_t i = 0; i < balance.region_effects.size(); ++i) {
        const auto& input = balance.region_effects[i];
        auto& output = world.region_profiles[i];
        output.radius = static_cast<std::uint8_t>(input.radius);
        output.effects = { word(input.happiness),
                           word(input.illness),
                           word(input.hunger),
                           word(input.security),
                           word(input.attraction) };
    }
}

void bind_restored_world_managers(WorldRuntime& c, World& world) {
    //These five role IDs are absent from the World wire record. Resolve them
    //from actual restored payloads instead of retaining fresh-world numeric IDs.
    constexpr std::array roles{ std::pair{ ThingKind::advisor, &WorldTail::advisor },
                                std::pair{ ThingKind::challenge_manager, &WorldTail::challenge_manager },
                                std::pair{ ThingKind::mission_controller, &WorldTail::mission_controller },
                                std::pair{ ThingKind::animation_manager, &WorldTail::animation_manager },
                                std::pair{ ThingKind::helper_23, &WorldTail::helper_23 } };
    for (const auto& [kind, member] : roles) {
        ThingId found = 0;
        for (auto* row = c.entities.active_head; row; row = row->active_next)
            if (row->payload && row->payload->kind == kind) {
                if (found)
                    throw std::runtime_error("Ambiguous restored World manager role");
                found = row->payload->id;
            }
        world.saved.*member = found;
    }
    c.attractions.park_object_record_count = 0;
    c.visitors.count = 0;
    for (auto* row = c.entities.active_head; row; row = row->active_next)
        if (row->payload) {
            if (row->payload->kind == ThingKind::park_object)
                ++c.attractions.park_object_record_count;
            else if (row->payload->kind == ThingKind::person)
                ++c.visitors.count;
        }
}

bool load_world_base_map(WorldRuntime& c, World& world, std::span<const std::byte> bytes) {
    const auto get = [&](std::size_t at) {
        std::uint32_t value{};
        save::detail::decode(std::span<const std::byte, 4>{ bytes.data() + at, 4 }, value);
        return value;
    };
    if (bytes.size() < 44 || get(0) != 1295142996u)
        return false;
    std::size_t cursor = 36;
    while (cursor <= bytes.size() - 8) {
        const auto tag = get(cursor), length = get(cursor + 4);
        cursor += 8;
        if (length > bytes.size() - cursor)
            return false;
        if (tag == 542130509u) {
            if (length < 28)
                return false;
            const auto width = get(cursor), height = get(cursor + 4);
            if (!width
                || !height
                || width > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
                || height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
                || static_cast<std::uint64_t>(width) * height > length - 28)
                return false;
            for (std::uint32_t row = 0; row < height; ++row)
                for (std::uint32_t column = 0; column < width; ++column) {
                    const auto x = static_cast<std::int64_t>(row) - c.visitors.environment.MapInfo.HeightfieldXStart,
                               y = static_cast<std::int64_t>(column) - c.visitors.environment.MapInfo.HeightfieldYStart;
                    if (x >= 0 && y >= 0 && x < 128 && y < 128)
                        world.map_cells[static_cast<std::size_t>(x + y * 128)].status_flags =
                            std::to_integer<std::uint8_t>(
                                bytes[cursor + 28 + column + static_cast<std::size_t>(row) * width]);
                }
        }
        if (bytes[cursor - 8] == std::byte{ 'E' }
            && bytes[cursor - 7] == std::byte{ 'N' }
            && bytes[cursor - 6] == std::byte{ 'D' }
            && bytes[cursor - 5] == std::byte{ ' ' }
            && !length)
            return true;
        cursor += length;
    }
    return false;
}
} //namespace simcoaster
