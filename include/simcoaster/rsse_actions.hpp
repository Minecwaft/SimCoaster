#pragma once
#include "world_domains_fwd.hpp"
#include "rsse_people.hpp"

namespace simcoaster {
std::uint32_t get_object_definition_particle_template_index(const ResearchCatalogDefinition&, std::uint32_t selector);
std::uint32_t get_object_ride_sound_catalog_slot(const ResearchCatalogDefinition&);
std::uint32_t
dispatch_resource_manager_operation(ScriptWorld&, std::uint32_t target, std::int32_t operation, std::int32_t value);
std::uint32_t dispatch_rsse_action_operation(ScriptWorld&,
                                             RsseScriptState&,
                                             RsseActionType,
                                             std::int32_t node,
                                             std::int32_t template_or_sound_key,
                                             std::int32_t scale_milli);
std::array<float, 3> get_rsse_mesh_effect_origin(ScriptWorld&, const RsseScriptState&);
void set_rsse_repair_effect(ScriptWorld&, RsseScriptState&, bool enabled);
void spawn_rsse_upgrade_effect(ScriptWorld&, RsseScriptState&);
std::uint32_t start_rsse_scream(ScriptWorld&,
                                std::uint32_t previous,
                                std::int32_t group,
                                std::int32_t level,
                                std::int32_t speed,
                                const std::array<std::int32_t, 3>& position);
std::uint32_t submit_rsse_passenger_voice(ScriptWorld&,
                                          std::int32_t group,
                                          std::int32_t level,
                                          std::int32_t speed,
                                          const std::array<std::int32_t, 3>& position);
std::uint32_t submit_rsse_scream(ScriptWorld&, std::int32_t group, const std::array<std::int32_t, 3>& position);
std::uint32_t set_rsse_scream_level(ScriptWorld&, std::uint32_t request, std::int32_t level, std::int32_t speed);
bool step_rsse_action_instruction(ScriptWorld&, RsseScriptState&, RsseOpcode);
} //namespace simcoaster
