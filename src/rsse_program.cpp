#include "simcoaster/rsse_program.hpp"
#include "simcoaster/save_schema.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <ranges>

namespace simcoaster {
namespace {
class Reader {
    std::span<const std::byte> bytes_;

public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    std::size_t remaining() const {
        return bytes_.size();
    }

    std::optional<std::uint32_t> word() {
        if (bytes_.size() < 4) {
            return std::nullopt;
        }
        std::uint32_t value {};
        save::detail::decode<std::uint32_t>(std::span<const std::byte, 4> { bytes_.data(), 4 }, value);
        bytes_ = bytes_.subspan(4);
        return value;
    }

    std::optional<std::string> string(std::size_t count) {
        if (count > bytes_.size()) {
            return std::nullopt;
        }
        std::string result { reinterpret_cast<const char*>(bytes_.data()), count };
        bytes_ = bytes_.subspan(count);
        return result;
    }
};
} //namespace

std::expected<RsseProgram, RsseProgramError> parse_rsse_program(std::span<const std::byte> bytes) {
    if (bytes.size() < 52) {
        return std::unexpected(RsseProgramError::short_header);
    }
    Reader reader { bytes };
    std::array<std::uint32_t, 13> header {};
    for (auto& word : header) {
        word = *reader.word();
    }
    //The runtime checks only the signature. Preserve checksum and Pad words,
    //including noncanonical values accepted by the original loader.
    if (header[0] != 1163088722u) {
        return std::unexpected(RsseProgramError::bad_signature);
    }
    RsseProgram result;
    auto& h = result.header;
    h.signature = header[0];
    h.opcode_checksum = header[1];
    h.value_slot_count = std::bit_cast<std::int32_t>(header[2]);
    h.operand_stack_capacity = std::bit_cast<std::int32_t>(header[3]);
    h.instruction_step_budget = std::bit_cast<std::int32_t>(header[4]);
    h.limbo_capacity = std::bit_cast<std::int32_t>(header[5]);
    h.bounce_capacity = std::bit_cast<std::int32_t>(header[6]);
    h.walk_capacity = std::bit_cast<std::int32_t>(header[7]);
    std::ranges::copy(header | std::views::drop(8) | std::views::take(4), h.padding.begin());
    h.instruction_word_count = std::bit_cast<std::int32_t>(header[12]);
    if (h.value_slot_count < 0 || h.operand_stack_capacity < 0 || h.limbo_capacity < 0 || h.bounce_capacity < 0 || h.walk_capacity < 0 || h.instruction_word_count < 0) {
        return std::unexpected(RsseProgramError::negative_count);
    }
    if (static_cast<std::size_t>(h.instruction_word_count) > reader.remaining() / 4) {
        return std::unexpected(RsseProgramError::short_payload);
    }
    result.instructions.reserve(static_cast<std::size_t>(h.instruction_word_count));
    for (int i = 0; i < h.instruction_word_count; ++i) {
        result.instructions.push_back(*reader.word());
    }
    const auto string_count = reader.word();
    if (!string_count) {
        return std::unexpected(RsseProgramError::short_payload);
    }
    auto strings = reader.string(*string_count);
    if (!strings) {
        return std::unexpected(RsseProgramError::short_payload);
    }
    result.string_table = std::move(*strings);
    //This optional footer is ignored by the game runtime. It is retained for
    //variable-name inspection and never required to execute a runtime-only file.
    if (reader.remaining()) {
        result.variable_metadata = RsseVariableMetadata::complete;
        for (int i = 0; i < h.value_slot_count; ++i) {
            const auto count = reader.word();
            if (!count) {
                result.variable_metadata = RsseVariableMetadata::malformed;
                break;
            }
            auto name = reader.string(*count);
            if (!name || (!name->empty() && name->back() != '\0')) {
                result.variable_metadata = RsseVariableMetadata::malformed;
                break;
            }
            if (!name->empty()) {
                name->pop_back();
            }
            result.variable_names.push_back(std::move(*name));
        }
    }
    return result;
}
} //namespace simcoaster
