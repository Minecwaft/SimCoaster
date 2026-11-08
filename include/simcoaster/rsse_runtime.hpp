#pragma once
#include "world_domains_fwd.hpp"
#include "rsse_vm.hpp"

namespace simcoaster {
void initialize_rsse_script_state_manager(ScriptWorld&);
void update_rsse_scripts(ScriptWorld&);
//Returns the next list record, read after resource callbacks. The manager
//owns loaded states; external views retain ownership of their supplied storage.
RsseScriptState* destroy_rsse_script_state_record(ScriptWorld&, RsseScriptState*, RsseScriptCleanupFlags);
RsseScriptState* destroy_rsse_script_state_by_id(ScriptWorld&, std::uint32_t, RsseScriptCleanupFlags);
void shutdown_rsse_script_state_manager(ScriptWorld&);
} //namespace simcoaster
