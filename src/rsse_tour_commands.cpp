#include "rsse_tour_internal.hpp"
#include "simcoaster/rsse_vm.hpp"

namespace simcoaster {
void step_rsse_tour_instruction(ScriptWorld& c, RsseScriptState& script) {
    using namespace tour_detail;
    const auto read = [&] {
        return read_rsse_instruction_word(script);
    };
    const auto resolve = [&] {
        return resolve_rsse_operand(script, read());
    };
    const auto tagged = [](std::uint32_t v) {
        return (v & rsse_tag_mask) == std::to_underlying(RsseWordTag::value_slot);
    };
    const auto handle = script.auxiliary_runtime_object_handle;
    // clang-format off
  match(static_cast<RsseTourCommand>(read())){
    case RsseTourCommand::create=>{
      const auto short_approach=resolve();const auto index=c.ports.meshes.find_embedded_node_resource_record(c,script.mesh_instance_handle,0x800u,99);
      if(index!=-1){const auto p=world_operations::get_mesh_clone_attachment_position(c,script.mesh_instance_handle,index).value_or(std::array<float,3>{});const auto* instance=c.resources.meshes?c.resources.meshes->instance(script.mesh_instance_handle):nullptr;if(!instance)throw std::logic_error("TOUR reference mesh is not bound");
        script.auxiliary_runtime_object_handle=create_rsse_tour(c,script.owner_used_thing_id,script.id,mul(300,fixed::truncate_integer(p[0])),mul(300,fixed::truncate_integer(p[1])),mul(300,fixed::truncate_integer(p[2])),mul(instance->metadata.orientation_degrees,4096)/360,short_approach);
      }
    }
    case RsseTourCommand::destroy=>world_operations::destroy_rsse_tour(c,handle);
    case RsseTourCommand::board=>{const auto source=read();if(tagged(source))script.current_value=board_rsse_tour_passenger(c,handle,static_cast<std::uint32_t>(script.value_slots.at(source^std::to_underlying(RsseWordTag::value_slot))));}
    case RsseTourCommand::unload=>{const auto destination=read();if(tagged(destination)){script.current_value=std::bit_cast<std::int32_t>(unload_rsse_tour_passenger(c,handle));script.value_slots.at(destination^std::to_underlying(RsseWordTag::value_slot))=script.current_value;}}
    case RsseTourCommand::set_count=>set_rsse_tour_element_count(c,handle,static_cast<std::uint32_t>(resolve()));
    case RsseTourCommand::set_duration=>world_operations::set_rsse_tour_duration(c,handle,mul(1000,resolve()));
    case RsseTourCommand::set_effect=>set_rsse_tour_effect(c,handle,static_cast<Effect>(resolve()));
    case RsseTourCommand::docked_ready=>{(void)resolve();const auto* r=find(c,handle);script.current_value=r?r->docked_element_ready:0;}
    case RsseTourCommand::start=>{(void)resolve();script.current_value=start_rsse_tour(c,handle);}
    case RsseTourCommand::fixed_approach=>{const auto enabled=resolve();if(auto* r=find(c,handle))r->fixed_approach_heading_enabled=enabled;}
    case RsseTourCommand::enabled=>set_rsse_tour_enabled(c,handle,resolve()!=0);
    case RsseTourCommand::damaged_docked=>{(void)resolve();const auto* r=find(c,handle);script.current_value=r && r->entries[0].particle_transition_pending && r->entries[0].element_state==State::docked;}
    case RsseTourCommand::busy=>{const auto destination=read();if(tagged(destination)){script.current_value=rsse_tour_busy(c,handle);script.value_slots.at(destination^std::to_underlying(RsseWordTag::value_slot))=script.current_value;}}
    case RsseTourCommand::separation=>{const auto value=resolve();if(auto* r=find(c,handle))r->separation_scale=value;}
    case RsseTourCommand::attitude=>{const auto value=resolve();if(auto* r=find(c,handle))r->attitude_scale=value;}
    case _=>{}
  }
    // clang-format on
}
} //namespace simcoaster
