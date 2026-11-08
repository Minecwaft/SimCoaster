#include "simcoaster/sound_catalog.hpp"
#include "sound_io.hpp"
#include <format>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
using sound_detail::Reader;
constexpr std::array<std::uint32_t, 4> bank_tag { 3915459585u, 298987984u, 2684357044u, 66229193u };
constexpr std::array<std::uint32_t, 4> effect_tag { 3915459584u, 298987984u, 2952792500u, 66229193u };

std::string_view trimmed(std::string_view text) {
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = text.find_first_not_of(whitespace);
    if (first == text.npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}
} //namespace

std::string describe_sound_error(const SoundError& error) {
    const auto reason = match (error.reason) -> std::string_view {
        case SoundFailure::io            => "I/O failure";
        case SoundFailure::missing       => "missing resource";
        case SoundFailure::truncated     => "truncated record";
        case SoundFailure::signature     => "wrong format signature";
        case SoundFailure::count         => "array outside payload";
        case SoundFailure::string        => "invalid string extent";
        case SoundFailure::index         => "invalid resource or graph index";
        case SoundFailure::sample_header => "invalid sample header";
        case SoundFailure::sample_format => "unsupported sample format";
        case SoundFailure::mpeg          => "invalid MPEG audio";
        case SoundFailure::pcm           => "invalid PCM audio";
        case SoundFailure::selection     => "invalid sound selection";
        case SoundFailure::reference     => "invalid catalog reference";
        case SoundFailure::configuration => "invalid audio configuration";
    };
    return std::format("{}: {} at {}{}", error.resource, reason, error.offset, (error.sample_token ? std::format(" (sample {})", *error.sample_token) : ""));
}

std::expected<SoundBankMap, SoundError> parse_sound_bank_map(std::span<const std::byte> bytes) {
    try {
        Reader input { bytes };
        SoundBankMap result;
        const auto header = input.read<sound_wire::MapHeader>();
        if (header.tag != bank_tag) {
            input.fail(SoundFailure::signature, 0);
        }
        result.revision = header.revision;
        const auto array = input.read<sound_wire::ArrayHeader>();
        const auto records = input.array<sound_wire::BankRecord>(array.count);
        result.sources.reserve(records.size());
        for (const auto& record : records) {
            const auto extent = input.read<std::uint32_t>();
            auto name = input.string(extent);
            const auto separator = name.rfind('\\');
            auto basename = separator == name.npos ? name : name.substr(separator + 1);
            if (basename.empty()) {
                input.fail(SoundFailure::string, input.cursor - extent);
            }
            result.sources.push_back({ std::move(basename), std::move(name), record.flags, record.opaque_00A });
        }
        return result;
    } catch (const SoundError& error) {
        return std::unexpected(error);
    }
}

std::expected<SoundEffectMap, SoundError> parse_sound_effect_map(std::span<const std::byte> bytes) {
    try {
        Reader input { bytes };
        SoundEffectMap result;
        const auto header = input.read<sound_wire::MapHeader>();
        if (header.tag != effect_tag) {
            input.fail(SoundFailure::signature, 0);
        }
        result.revision = header.revision;
        const auto array = input.read<sound_wire::ArrayHeader>();
        const auto groups = input.array<sound_wire::Group>(array.count);
        result.groups.reserve(groups.size());
        for (const auto& group : groups) {
            SoundCatalogGroup output { group, {} };
            const auto descriptors = input.array<sound_wire::Descriptor>(group.count);
            output.descriptors.reserve(descriptors.size());
            for (const auto& descriptor : descriptors) {
                SoundDescriptor target { descriptor, {} };
                const auto selections = input.array<sound_wire::Selection>(descriptor.count);
                target.selections.reserve(selections.size());
                std::uint32_t previous_weight {};
                for (auto selection : selections) {
                    //Revision0 stores cumulative row weights; revision1 stores deltas.
                    //Sample weights remain cumulative in both formats.
                    const auto original_weight = selection.weight;
                    if (!header.revision) {
                        selection.weight -= previous_weight;
                    }
                    previous_weight = original_weight;
                    SoundSelection row { selection, input.array<sound_wire::Sample>(selection.sample_count), {} };
                    row.branches = input.array<sound_wire::Branch>(selection.branch_count);
                    for (const auto& branch : row.branches) {
                        if (!branch.selection || branch.selection > selections.size()) {
                            input.fail(SoundFailure::index, input.cursor - row.branches.size() * save::value_width<sound_wire::Branch>);
                        }
                    }
                    target.selections.push_back(std::move(row));
                }
                //Do not apply8A7E80's optimization based on branch_count==0: the
                //original tests a relocated nonnull pointer, even for an empty array.
                output.descriptors.push_back(std::move(target));
            }
            result.groups.push_back(std::move(output));
        }
        return result;
    } catch (const SoundError& error) {
        return std::unexpected(error);
    }
}

const SoundDescriptor* SoundCatalogGroup::find(std::uint32_t key) const {
    for (const auto& descriptor : descriptors) {
        if (descriptor.parameters.key == key) {
            return descriptor.selections.empty() ? nullptr : &descriptor;
        }
    }
    return nullptr;
}

std::expected<SoundCatalogReferences, SoundError> parse_sound_catalog_references(std::span<const std::byte> bytes) {
    try {
        Reader input { bytes };
        const auto extent = input.read<std::uint32_t>();
        const auto count = input.read<std::uint32_t>();
        const auto payload_begin = input.cursor;
        Reader payload { input.take(extent), 0, payload_begin };
        (void)payload.array<std::uint32_t>(count);
        SoundCatalogReferences result;
        result.paths.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto name_bytes = payload.read<std::uint8_t>();
            result.paths.push_back(name_bytes ? payload.string(name_bytes) : std::string {});
        }
        result.bytes_consumed = input.cursor;
        return result;
    } catch (const SoundError& error) {
        return std::unexpected(error);
    }
}

std::expected<SoundEventNames, SoundError> parse_sound_event_names(std::string_view text) {
    SoundEventNames result;
    std::size_t cursor {};
    bool header {};
    while (cursor < text.size()) {
        const auto begin = cursor;
        auto end = text.find('\n', cursor);
        if (end == text.npos) {
            end = text.size();
        }
        auto line = trimmed(text.substr(cursor, end - cursor));
        cursor = end == text.size() ? end : end + 1;
        if (const auto comment = line.find('#'); comment != line.npos) {
            line = trimmed(line.substr(0, comment));
        }
        if (line.empty()) {
            continue;
        }
        if (!header) {
            result.catalog_path = line;
            header = true;
            continue;
        }
        const auto equal = line.find('=');
        if (equal == line.npos) {
            return std::unexpected(SoundError { SoundFailure::string, begin, {} });
        }
        const auto name = trimmed(line.substr(0, equal)), number = trimmed(line.substr(equal + 1));
        std::uint32_t key {};
        const auto parsed = std::from_chars(number.data(), number.data() + number.size(), key);
        if (name.empty() || parsed.ec != std::errc {} || parsed.ptr != number.data() + number.size()) {
            return std::unexpected(SoundError { SoundFailure::string, begin, {} });
        }
        const auto [entry, inserted] = result.keys.emplace(name, key);
        if (!inserted && entry->second != key) {
            return std::unexpected(SoundError { SoundFailure::index, begin, {} });
        }
    }
    if (!header) {
        return std::unexpected(SoundError { SoundFailure::string, 0, {} });
    }
    return result;
}

std::expected<SoundSampleBank, SoundError> parse_sound_sample_bank(std::vector<std::byte> bytes) {
    try {
        Reader input { bytes };
        const auto count = input.read<std::uint32_t>();
        const auto offsets = input.array<std::uint32_t>(count);
        const auto directory_end = input.cursor;
        SoundSampleBank result;
        result.samples.reserve(count);
        for (const auto offset : offsets) {
            SoundSampleData sample {};
            sample.header_offset = offset;
            try {
                if (offset < directory_end || offset > bytes.size()) {
                    input.fail(SoundFailure::index, offset);
                }
                Reader record { std::span<const std::byte> { bytes }.subspan(offset), 0, offset };
                sample.header = record.read<sound_wire::SampleHeader>();
                if (sample.header.header_bytes != save::value_width<sound_wire::SampleHeader> || sample.header.stored_bytes < 0) {
                    input.fail(SoundFailure::sample_header, offset);
                }
                sample.payload_offset = static_cast<std::size_t>(offset) + sample.header.header_bytes;
                (void)record.take(static_cast<std::uint32_t>(sample.header.stored_bytes));
            } catch (const SoundError& error) {
                sample.validation_error = error;
            }
            //Native8BD510/8BD620 initialize only the directory; other entries do
            //not determine whether a requested sample is available. Cache checked
            //entry results in owned storage, and report errors at request time.
            result.samples.push_back(std::move(sample));
        }
        result.bytes = std::move(bytes);
        return result;
    } catch (const SoundError& error) {
        return std::unexpected(error);
    }
}

const SoundSampleData* SoundSampleBank::find(std::uint32_t token) const {
    const auto found = sample(token);
    return found ? *found : nullptr;
}

std::expected<const SoundSampleData*, SoundError> SoundSampleBank::sample(std::uint32_t token) const {
    if (!token || token > samples.size()) {
        return std::unexpected(SoundError { SoundFailure::index, token, {}, token });
    }
    const auto& found = samples[token - 1];
    if (found.validation_error) {
        auto error = *found.validation_error;
        error.sample_token = token;
        return std::unexpected(std::move(error));
    }
    return &found;
}

std::span<const std::byte> SoundSampleBank::payload(const SoundSampleData& sample) const {
    if (sample.header.stored_bytes < 0 || sample.payload_offset > bytes.size() || static_cast<std::uint32_t>(sample.header.stored_bytes) > bytes.size() - sample.payload_offset) {
        throw std::out_of_range("Sound sample payload");
    }
    return std::span<const std::byte> { bytes }.subspan(sample.payload_offset, static_cast<std::uint32_t>(sample.header.stored_bytes));
}
} //namespace simcoaster
