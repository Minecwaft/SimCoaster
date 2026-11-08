#pragma once
#include "simcoaster/world_domains_fwd.hpp"
#include "company_email_internal.hpp"

namespace simcoaster::staff_mail_detail {
inline void initialize(StaffWorld& c, CompanyEmailMessage& email, StaffRuntimeRecord& staff, CompanyEmailMessageKind kind, std::uint32_t argument) {
    email.concrete_class = CompanyEmailClass::staff;
    email.reason = kind;
    email.staff_record = &staff;
    email.staff_argument = argument;
    email.context_mode = 1;
    email.default_resource = 151;
    email.append_signature = 1;
    email.tag_id = 151;
    email.selected_variant = find_tag_system_message_variant_by_id(c, 5);
    if (!email.selected_variant) {
        throw std::logic_error("staff sender variant5 is missing");
    }
}
} //namespace simcoaster::staff_mail_detail
