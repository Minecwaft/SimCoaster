#pragma once
#include "rsse_types.hpp"
#include "world_domains_fwd.hpp"
#include "world_ids.hpp"
#include <expected>

namespace simcoaster {
enum class RsseProgramError {
    short_header,
    bad_signature,
    negative_count,
    short_payload
};
std::expected<RsseProgram, RsseProgramError> parse_rsse_program(std::span<const std::byte>);
RsseScriptState* create_rsse_program_state(ScriptWorld&, RsseProgram);
RsseScriptState* create_rsse_program_state_for_owner(ScriptWorld&, RsseProgram, ThingId owner);
} //namespace simcoaster
