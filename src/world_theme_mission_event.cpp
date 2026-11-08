#include "simcoaster/company_email.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_theme.hpp"
#include "company_email_internal.hpp"
#include <algorithm>
#include <memory>

namespace simcoaster {
namespace {
void initialize_common_email(CompanyEmailMessage* email, CompanyEmailMessageKind kind, std::int32_t context_mode) {
    email->reason = kind;
    email->context_mode = context_mode;
    email->counter_slot = 0;
    email->default_resource = 151;
    email->append_signature = 1;
    email->tag_id = 151;
}
} //namespace

CompanyEmailMessage* construct_tag_system_company_email(MissionWorld& c, CompanyEmailMessage* email, std::int32_t tag_id, std::int32_t message_kind, std::int32_t context_mode) {
    initialize_common_email(email, static_cast<CompanyEmailMessageKind>(message_kind), context_mode);
    //The native search selects ID0. It never reserves or modifies that row.
    email->selected_variant = find_tag_system_message_variant_by_id(c, 0);
    email->concrete_class = CompanyEmailClass::tag_system;
    email->tag_id = tag_id;
    email->tag_entry = nullptr;
    return email;
}

GameOverCompanyEmail* construct_game_over_company_email(MissionWorld& c, GameOverCompanyEmail* email, GameOverEmailReason reason) {
    initialize_common_email(email, reason, 1);
    email->selected_variant = find_tag_system_message_variant_by_id(c, 1);
    email->concrete_class = CompanyEmailClass::game_over;
    email->counter_slot = 2;
    //The native constructor does not store the tag-backed extension field.
    return email;
}

bool dispatch_initial_mission_event_once(MissionWorld& c) {
    auto& manager = get_or_create_theme_global_info_manager(c);
    //Native CMP/JZ tests exactly1, rather than treating the byte as bool.
    if (manager.mission_world_event_latch == 1 || manager.mission_number != 0) {
        return false;
    }
    auto* raw = static_cast<CompanyEmailMessage*>(c.ports.memory.allocate(Allocation::event_payload, sizeof(CompanyEmailMessage)));
    CompanyEmailMessage* email = nullptr;
    if (raw) {
        try {
            std::construct_at(raw);
            email = construct_tag_system_company_email(c, raw, 92, 5, 1);
        } catch (...) {
            c.ports.memory.release(Allocation::event_payload, raw);
            throw;
        }
    }
    //The wrapper owns its live pointer, including a receiver's replacement.
    dispatch_heap_payload_event(c, email);
    //Delivery and raw release can rebind the process-global manager. Re-fetch
    //only after both finish; reentrant delivery sees the old latch until then.
    get_or_create_theme_global_info_manager(c).mission_world_event_latch = 1;
    return true;
}
} //namespace simcoaster
