#include "simcoaster/sound_save.hpp"
#include "sound_io.hpp"
#include <set>

namespace simcoaster {
namespace {
using sound_detail::Reader;
using sound_detail::Writer;
constexpr std::uint32_t target_marker = 1234321;

void marker(Reader& input) {
    const auto at = input.cursor;
    if (input.read<std::uint32_t>() != target_marker)
        input.fail(SoundFailure::signature, at);
}

bool valid_kind(SoundTargetKind kind) {
    const auto value = static_cast<std::uint32_t>(kind);
    return value >= 1 && value <= 10;
}

bool child_kind(SoundTargetKind kind) {
    return kind == SoundTargetKind::child || kind == SoundTargetKind::conditional_child;
}
} //namespace

std::expected<SoundBackendSnapshotPrefix, SoundError>
parse_sound_backend_snapshot_prefix(std::span<const std::byte> bytes) {
    try {
        Reader input{ bytes };
        SoundBackendSnapshot result;
        result.listener = input.read<std::array<float, 3>>();
        const auto manager_start = input.cursor;
        const auto manager_extent = input.read<std::uint32_t>();
        if (manager_extent && (manager_extent < 8 || manager_extent > bytes.size() - manager_start))
            input.fail(SoundFailure::count, manager_start);
        if (manager_extent)
            input.bytes = bytes.first(manager_start + manager_extent);
        const auto count = input.read<std::uint32_t>();
        if (!manager_extent && count)
            input.fail(SoundFailure::count, manager_start);
        auto references = parse_sound_catalog_references(input.bytes.subspan(input.cursor));
        if (!references) {
            auto error = references.error();
            error.offset += input.cursor;
            return std::unexpected(std::move(error));
        }
        input.take(references->bytes_consumed);
        result.catalogs = std::move(*references);
        result.pool_blocks = input.read<std::uint32_t>();
        constexpr auto minimum_chain_bytes = 8 * save::value_width<std::uint32_t>
                                           + save::value_width<SoundSavedTargetFields>
                                           + save::value_width<std::uint8_t>;
        if (count > (input.bytes.size() - input.cursor) / minimum_chain_bytes)
            input.fail(SoundFailure::count, input.cursor);
        result.targets.reserve(count);
        std::set<std::uint32_t> ids;
        for (std::uint32_t i = 0; i < count; ++i) {
            SoundSavedTargetChain chain;
            chain.id = input.read<std::uint32_t>();
            if (!ids.insert(chain.id).second)
                input.fail(SoundFailure::index, input.cursor - 4);
            std::optional<SoundTargetKind> expected_kind;
            for (;;) {
                SoundSavedTarget target;
                target.kind = input.read<SoundTargetKind>();
                if (!valid_kind(target.kind) || (expected_kind && target.kind != *expected_kind))
                    input.fail(SoundFailure::index, input.cursor - 4);
                marker(input);
                target.fields = input.read<SoundSavedTargetFields>();
                marker(input);
                if (input.read<std::uint32_t>())
                    target.payload = input.read<SoundSavedPayload>();
                marker(input);
                target.has_xz_bounds = input.read<std::uint8_t>();
                target.target_id = input.read<std::uint32_t>();
                const auto child = input.read<SoundTargetKind>();
                chain.nodes.push_back(std::move(target));
                if (!static_cast<std::uint32_t>(child))
                    break;
                if (!child_kind(child))
                    input.fail(SoundFailure::index, input.cursor - 4);
                expected_kind = child;
            }
            result.targets.push_back(std::move(chain));
        }
        if (manager_extent && input.cursor != input.bytes.size())
            input.fail(SoundFailure::count, input.cursor);
        return SoundBackendSnapshotPrefix{ std::move(result), input.cursor };
    } catch (const SoundError& error) {
        return std::unexpected(error);
    }
}

std::expected<SoundBackendSnapshot, SoundError> parse_sound_backend_snapshot(std::span<const std::byte> bytes) {
    auto parsed = parse_sound_backend_snapshot_prefix(bytes);
    if (!parsed)
        return std::unexpected(parsed.error());
    if (parsed->bytes_consumed != bytes.size())
        return std::unexpected(SoundError{ SoundFailure::count, parsed->bytes_consumed, {} });
    return std::move(parsed->snapshot);
}

std::expected<std::vector<std::byte>, SoundError> encode_sound_backend_snapshot(const SoundBackendSnapshot& snapshot) {
    try {
        if (snapshot.catalogs.paths.size() > std::numeric_limits<std::uint32_t>::max()
            || snapshot.targets.size() > std::numeric_limits<std::uint32_t>::max())
            throw SoundError{ SoundFailure::count, 0, {} };
        Writer payload;
        const auto count = static_cast<std::uint32_t>(snapshot.catalogs.paths.size());
        for (std::uint32_t i = 0; i < count; ++i)
            payload.write(i);
        for (const auto& path : snapshot.catalogs.paths) {
            if (path.size() > 254 || path.find('\0') != path.npos)
                throw SoundError{ SoundFailure::string, payload.bytes.size(), path };
            const auto length = path.empty() ? 0u : static_cast<unsigned>(path.size() + 1);
            payload.write(static_cast<std::uint8_t>(length));
            if (length) {
                payload.append(std::as_bytes(std::span{ path.data(), path.size() }));
                payload.write(std::uint8_t{});
            }
        }
        Writer manager;
        manager.write(std::uint32_t{});
        manager.write(static_cast<std::uint32_t>(snapshot.targets.size()));
        manager.write(payload.size_word());
        manager.write(count);
        manager.append(payload.bytes);
        manager.write(snapshot.pool_blocks);
        std::set<std::uint32_t> ids;
        for (const auto& chain : snapshot.targets) {
            if (chain.nodes.empty() || !ids.insert(chain.id).second)
                throw SoundError{ SoundFailure::index, manager.bytes.size(), {} };
            manager.write(chain.id);
            for (std::size_t i = 0; i < chain.nodes.size(); ++i) {
                const auto& node = chain.nodes[i];
                if (!valid_kind(node.kind) || (i && !child_kind(node.kind)))
                    throw SoundError{ SoundFailure::index, manager.bytes.size(), {} };
                manager.write(node.kind);
                manager.write(target_marker);
                manager.write(node.fields);
                manager.write(target_marker);
                manager.write(node.payload ? 1u : 0u);
                if (node.payload)
                    manager.write(*node.payload);
                manager.write(target_marker);
                manager.write(node.has_xz_bounds);
                manager.write(node.target_id);
                manager.write(i + 1 < chain.nodes.size() ? chain.nodes[i + 1].kind : static_cast<SoundTargetKind>(0));
            }
        }
        const auto extent = save::detail::encode(manager.size_word());
        std::ranges::copy(extent, manager.bytes.begin());
        Writer result;
        result.write(snapshot.listener);
        result.append(manager.bytes);
        return std::move(result.bytes);
    } catch (const SoundError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
