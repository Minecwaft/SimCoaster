#pragma once
#include "sam_error.hpp"
#include <span>
#include <vector>

namespace simcoaster {
struct LegacySamAssignment {
    std::string group, field;
    std::optional<std::string> index; //Lexical index; no invented retired capacity.
    std::string value;
    std::string source;
    std::size_t line {}, column {}, offset {}, value_offset {};
    bool retired {};
};

struct LegacyOnlineSamDocument {
    std::vector<LegacySamAssignment> assignments;
    const LegacySamAssignment* effective(std::string_view group, std::optional<std::string_view> index, std::string_view field) const;
    std::vector<LegacySamAssignment> unmapped() const;
};

//Separate diagnostic/configuration importer; never applies values to World.
//Earlier assignments survive failure. preserve=false clears the document.
std::expected<void, sam::Error> parse_legacy_online_sam(LegacyOnlineSamDocument&, std::string_view text, std::string_view source, bool preserve = false);
} //namespace simcoaster
