#pragma once
#include "world_domains_fwd.hpp"
#include "rsse_runtime.hpp"

namespace simcoaster {
std::optional<RsseSourceTransform> resolve_rsse_action_source_transform(ScriptWorld&, const RsseScriptState&, std::int32_t source_node, std::uint32_t lookup_flags, bool with_direction = false);
MobileSpriteCategory get_person_sprite_generation_variant_code(ScriptWorld&, const PersonRuntimeView&);
void position_rsse_walking_person(ScriptWorld&, RsseScriptState&, RsseWalkRecord&, std::int32_t source_node, std::int32_t destination_node, RsseWalkLookupMode, std::int32_t progress_milli);
void update_rsse_walk_records(ScriptWorld&, RsseScriptState&);
void update_rsse_attached_people(ScriptWorld&);
} //namespace simcoaster
