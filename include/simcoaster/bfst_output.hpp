#pragma once
#include "bfst_strings.hpp"
#include <optional>

namespace simcoaster {
//The native wide sink is finalized even when token interpretation fails.
//Some consumers deliberately keep the emitted prefix and ignore that status.
struct BfstWideOutput {
    std::u16string text;
    std::optional<BfstError> error;
};

BfstWideOutput format_bfst_wide_output(std::span<const std::byte>, const MbToUnicodeMap&, std::uint32_t row, std::size_t capacity, std::span<const BfstWideStringArgument> arguments = {}, std::span<const BfstSignedIntegerArgument> integers = {});
} //namespace simcoaster
