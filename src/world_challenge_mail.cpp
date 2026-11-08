#include "simcoaster/world_mail.hpp"
#include "company_email_internal.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_analyser.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/object_catalog.hpp"
#include "tag_message_catalog.hpp"
#include "simcoaster/bfst_output.hpp"
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace simcoaster {
namespace {
struct ChallengeMailMetadata {
    std::array<std::int32_t, 5> text, action;
};

//Original tag27_message_metadata_rows, reordered only into selector order.
constexpr std::array<ChallengeMailMetadata, 7> metadata { {
    { { { 0, 0, 0, 0, 0 } }, { { 645, 645, 645, 645, 645 } } },
    { { { 0, 0, 0, 0, 0 } }, { { 645, 645, 645, 645, 645 } } },
    { { { 53, 54, 55, 56, 57 } }, { { 569, 645, 571, 572, 570 } } },
    { { { 58, 59, 60, 61, 62 } }, { { 604, 645, 606, 607, 605 } } },
    { { { 63, 64, 65, 66, 67 } }, { { 581, 645, 583, 584, 582 } } },
    { { { 68, 69, 70, 71, 72 } }, { { 613, 645, 615, 616, 614 } } },
    { { { 73, 74, 75, 76, 77 } }, { { 597, 645, 599, 600, 598 } } },
} };

std::u16string text(MissionWorld& c, std::span<const std::byte> resource, std::int32_t row, std::size_t capacity, std::span<const BfstWideStringArgument> arguments = {}, std::span<const BfstSignedIntegerArgument> integers = {}) {
    const auto formatted = format_bfst_wide_string(resource, c.messages.mail_language.conversion, static_cast<std::uint32_t>(row), capacity, arguments, integers);
    if (!formatted) {
        throw std::runtime_error("Mail BFST: " + formatted.error().reason + " at byte " + std::to_string(formatted.error().offset));
    }
    return *formatted;
}

std::u16string tag_text(MissionWorld& c, std::int32_t row, std::size_t capacity = 255, std::span<const BfstWideStringArgument> arguments = {}, std::span<const BfstSignedIntegerArgument> integers = {}) {
    return text(c, c.messages.mail_language.tag_system, row, capacity, arguments, integers);
}

template <std::size_t N>
void copy(std::array<char16_t, N>& target, std::u16string_view value) {
    if (value.size() >= N) {
        throw std::length_error("native mail text exceeds destination");
    }
    std::ranges::copy(value, target.begin());
    target[value.size()] = 0;
}

std::u16string current_name(MissionWorld& c) {
    const auto* owner = c.finance.shares_finance.ownership_context.get();
    if (!owner) {
        throw std::logic_error("mail ownership context");
    }
    return owner->current_profile_name;
}

void queue(MissionWorld& c, std::int32_t action) {
    world_operations::queue_scored_advisor_action(c, action);
}

void set_date(MissionWorld& c, MailMessage& message) {
    if (!c.simulation.current) {
        throw std::logic_error("mail World is not bound");
    }
    FileTime time;
    std::uint32_t year {}, month {}, day {};
    calculate_world_calendar_filetime(c, &c.simulation.current->calendar, &time, &year, &month, &day);
    message.packed_date = day + (((month - 1) + (year << 8)) << 8);
}

std::u16string park_object_name(const ParkObjectRuntimeView& object) {
    const auto component = [](const auto& name) {
        const auto end = std::ranges::find(name, char16_t {});
        if (end == name.end()) {
            throw std::length_error("unterminated park-object name");
        }
        return std::u16string { name.begin(), end };
    };
    auto first = component(object.name_a);
    const auto second = component(object.name_b);
    if (first.empty()) {
        return second.substr(0, 32);
    }
    if (first.back() == u'-') {
        first.pop_back();
    } else if (!second.empty()) {
        first += u' ';
    }
    first += second;
    return first.substr(0, 32);
}

bool advisor_tag_notice(MissionWorld& c, TagSystemRecord& system, MailMessage& message, const TagMailEvent& event) {
    //Native ignores BFST's return status here, retaining its finalized prefix.
    //In particular literal tags109..112 reference a row needing an argument
    //which these records never provide; their actual body is the prefix Re: .
    const auto tag_prefix = [&](int row, std::size_t capacity = 255, std::span<const BfstWideStringArgument> args = {}) {
        return format_bfst_wide_output(c.messages.mail_language.tag_system, c.messages.mail_language.conversion, static_cast<std::uint32_t>(row), capacity, args).text;
    };
    const auto* row = tag_catalog::find(event.tag_id);
    if (!row) {
        return false;
    }
    message.tag_id = event.tag_id;
    message.dedupe_target_tag_id = row->target;
    message.format_word = event.thing_id;
    message.format_dword = std::bit_cast<std::int32_t>(event.secondary);
    message.expiry_delay_units = row->expiry;
    message.message_code = row->dedupe_mode;
    message.counter_slot = row->counter_slot;
    copy(message.text_buffer_1, tag_prefix(row->sender));
    message.auxiliary_field = 0;
    for (const auto& variant : c.messages.tag_system_message_variants) {
        if (variant.category_resource_id == row->sender || variant.secondary_resource_id == row->sender) {
            message.auxiliary_field = variant.variant_code;
            break;
        }
    }
    copy(message.text_buffer_2, current_name(c));
    copy(message.text_buffer_3, tag_prefix(row->subject));
    std::u16string argument, body;
    match (row->format) {
        case tag_catalog::Format::literal          => body = tag_prefix(row->body, 1024);
        case tag_catalog::Format::park_object_name => {
            if (!event.thing_id) {
                return false;
            }
            const auto* h = c.entities.at(event.thing_id).payload;
            if (!h || h->kind != ThingKind::park_object) {
                throw std::logic_error("tag message requires a live park object");
            }
            argument = park_object_name(*reinterpret_cast<const ParkObjectRuntimeView*>(h));
        }
        case tag_catalog::Format::catalog_object_name => {
            if (!static_cast<std::uint16_t>(event.secondary)) {
                return false;
            }
            argument = get_catalog_object_display_name(c, static_cast<std::uint16_t>(event.secondary));
        }
        case _ => return false;
    } if (row->format != tag_catalog::Format::literal) {
        const std::array strings { BfstWideStringArgument { 0, argument } };
        body = tag_prefix(row->body, 1024, strings);
    }
    copy(system.formatted_message_scratch, body);
    if (body.empty()) {
        return false;
    }
    copy(message.rendered_message, body);
    return true;
}

void challenge_notice(MissionWorld& c, MailMessage& message, const ChallengeNotificationEvent& event) {
    if (!event.record) {
        throw std::invalid_argument("null challenge notification");
    }
    const auto& challenge = *event.record;
    const auto selector = static_cast<std::size_t>(event.code);
    if (selector >= 5) {
        throw std::out_of_range("challenge notification selector");
    }
    const auto& row = metadata.at(static_cast<std::size_t>(challenge.sender));
    copy(message.text_buffer_3, tag_text(c, 42 + static_cast<std::int32_t>(selector) * 2));
    auto body = tag_text(c, row.text[selector], 1024);
    message.tag_id = 145 + static_cast<std::int32_t>(selector);
    if (selector == 0) {
        message.message_code = 11;
    } else if (selector >= 2) {
        message.dedupe_target_tag_id = 146;
    }
    if (selector != 1) {
        queue(c, row.action[selector]);
    }
    message.format_word = 0;
    message.format_dword = std::to_underlying(challenge.script_index);
    body += text(c, c.messages.mail_language.challenge_titles, std::to_underlying(challenge.script_index), 255);
    copy(message.rendered_message, body);
    const auto* variant = find_tag_system_message_variant_by_id(c, challenge.sender);
    if (!variant) {
        throw std::logic_error("missing challenge sender variant");
    }
    copy(message.text_buffer_1, tag_text(c, variant->category_resource_id));
    message.auxiliary_field = variant->variant_code;
    copy(message.text_buffer_2, current_name(c));
}

void company_notice(MissionWorld& c, MailMessage& message, CompanyEmailMessage& email) {
    if (email.concrete_class == CompanyEmailClass::spam || email.concrete_class == CompanyEmailClass::annual_report) {
        format_calendar_company_notice(c, message, email);
        return;
    }
    //635D80/5CCFF0: bind the real CTag before invoking any virtual text
    //formatter. The category caption can be the SECONDARY signature row:
    //initial mission92 uses174, victory93 uses176. Do not replace that caption
    //with the selected variant's category row173/175.
    const auto* tagged = (email.concrete_class == CompanyEmailClass::tag_system || email.concrete_class == CompanyEmailClass::debug_promotion) ? tag_catalog::find(email.tag_id) : nullptr;
    if (email.concrete_class == CompanyEmailClass::tag_system && !tagged) {
        throw std::logic_error("tag-backed company email has no CTag row");
    }
    if (tagged) {
        const auto variant = std::ranges::find_if(c.messages.tag_system_message_variants, [&](const auto& value) {
            return value.category_resource_id == tagged->sender || value.secondary_resource_id == tagged->sender;
        });
        if (variant != c.messages.tag_system_message_variants.end()) {
            email.selected_variant = &*variant;
        }
    }
    if (!email.selected_variant) {
        throw std::logic_error("missing company sender variant");
    }
    const auto& sender = *email.selected_variant;
    std::u16string body, heading;
    if (tagged) {
        if (email.concrete_class == CompanyEmailClass::debug_promotion) {
            const auto rank = text(c, c.messages.mail_language.promotions, next_crt_random(c) % 6 + 1, 255);
            const std::array argument { BfstWideStringArgument { 4, rank } };
            heading = tag_text(c, 169, 255, argument);
        } else {
            heading = tag_text(c, tagged->subject);
        }
        body = tag_text(c, tagged->body, 1024);
        message.tag_id = email.tag_id;
    } else if (email.concrete_class == CompanyEmailClass::objective_complete) {
        //5CEBB0/5CEBF0: direct OBJECTIVE_COMPLETE row, no MRAV argument.
        heading = tag_text(c, 168);
        body = text(c, c.messages.mail_language.objective_complete, email.objective_text_row_index, 1024);
        message.tag_id = 138;
    } else if (email.concrete_class == CompanyEmailClass::game_over) {
        heading = tag_text(c, 165);
        if (email.reason == CompanyEmailMessageKind::failed_challenges) {
            body = tag_text(c, 167, 1024);
        } else if (email.reason == CompanyEmailMessageKind::bankruptcy) {
            body = tag_text(c, 166, 1024);
        }
        message.tag_id = 138;
    } else if (email.concrete_class == CompanyEmailClass::staff) {
        if (!email.staff_record) {
            throw std::logic_error("missing staff mail record");
        }
        const auto& name = email.staff_record->name;
        const auto end = std::ranges::find(name, char16_t {});
        const std::u16string_view staff_name { name.data(), static_cast<std::size_t>(end - name.begin()) };
        if (email.reason == CompanyEmailMessageKind::staff_wage_demand || email.reason == CompanyEmailMessageKind::staff_wage_variant) {
            heading = tag_text(c, 251); //Native5CF050, both wage-message variants.
            const std::array arguments { BfstWideStringArgument { 8, staff_name } };
            const std::array amount { BfstSignedIntegerArgument { 9, std::bit_cast<std::int32_t>(email.staff_argument) } };
            body = tag_text(c, email.reason == CompanyEmailMessageKind::staff_wage_demand ? 252 : 253, 1024, arguments, amount);
            message.tag_id = 144;
        } else {
            heading = tag_text(c, email.reason == CompanyEmailMessageKind::staff_training_complete ? 78 : 80);
            const auto table = parse_bfst_literals(c.messages.mail_language.abilities, c.messages.mail_language.conversion);
            if (!table) {
                throw std::runtime_error("Abilities BFST: " + table.error().reason);
            }
            const std::array arguments { BfstWideStringArgument { 1, staff_name }, BfstWideStringArgument { 2, table->strings.at(email.staff_argument) } };
            body = tag_text(c, email.reason == CompanyEmailMessageKind::staff_training_complete ? 79 : 81, 1024, arguments);
            message.tag_id = email.reason == CompanyEmailMessageKind::staff_training_complete ? 142 : 143;
        }
    } else {
        const auto rank = text(c, c.messages.mail_language.promotions, email.promotion_rank, 255);
        const std::array argument { BfstWideStringArgument { 4, rank } };
        heading = tag_text(c, 169, 255, argument);
        body = tag_text(c, email.share_prize ? 171 : 170, 1024, argument);
        message.tag_id = 141;
    }
    copy(message.text_buffer_3, heading);
    copy(message.text_buffer_1, tag_text(c, tagged ? tagged->sender : sender.category_resource_id));
    copy(message.text_buffer_2, email.context_mode == 1 ? current_name(c) : tag_text(c, email.context_mode == 2 ? 187 : email.context_mode == 3 ? 188 : 189));
    if (email.append_signature) {
        body += u'\n';
        body += tag_text(c, sender.category_resource_id);
        body += u'\n';
        body += tag_text(c, sender.secondary_resource_id);
    }
    copy(message.rendered_message, body);
    message.dedupe_target_tag_id = 151;
    message.expiry_delay_units = 0;
    message.message_code = 0;
    message.format_word = 0;
    message.format_dword = 0;
    message.auxiliary_field = sender.variant_code;
    message.counter_slot = static_cast<std::uint16_t>(email.counter_slot);
    if (tagged) {
        message.dedupe_target_tag_id = tagged->target;
        message.expiry_delay_units = tagged->expiry;
        message.message_code = tagged->dedupe_mode;
    }
    if (email.concrete_class == CompanyEmailClass::debug_promotion) {
        message.packed_date = 28u + ((11u + (2088u << 8)) << 8); //5CF8B0: 28 December2088.
    }
    if (email.concrete_class == CompanyEmailClass::objective_complete) {
        //5CEC30: native click actions are independent of the objective text.
        match (email.objective_text_row_index) {
            case 0 => {
                message.message_code = 2;
                message.format_dword = 1703;
            }
            case 5 => {
                message.message_code = 2;
                message.format_dword = 3701;
            }
            case 8 => {
                message.message_code = 2;
                message.format_dword = 1702;
            }
            case 11 => {
                message.message_code = 2;
                message.format_dword = 2703;
            }
            case 12 => {
                message.message_code = 2;
                message.format_dword = 2702;
            }
            case 15 => {
                message.message_code = 2;
                message.format_dword = 3703;
            }
            case 2 or 10 => {
                message.message_code = 18;
                message.format_dword = 2;
            }
            case 3 or 7 => {
                message.message_code = 18;
                message.format_dword = 1;
            }
            case 4 or 14 => {
                message.message_code = 18;
                message.format_dword = 0;
            }
            case 6 or 9 or 13 or 16 => message.message_code = 11;
            case _                  => message.message_code = 12;
        }
    }
    if (email.concrete_class == CompanyEmailClass::staff) {
        message.message_code = 5;
        message.format_word = email.staff_record->base.header.id;
        if (email.reason == CompanyEmailMessageKind::staff_wage_demand || email.reason == CompanyEmailMessageKind::staff_wage_variant) {
            message.message_code = 0x0E;
            message.counter_slot = 2;
            message.format_dword = std::bit_cast<std::int32_t>(email.staff_argument);
        }
    }
    const auto kind = std::to_underlying(email.reason);
    if (kind >= 6 && kind <= 8) {
        //The native UI copies this modal notice before the owned mail is inserted.
        c.messages.modal_mail_notices.push_back({ heading, body, std::u16string { message.text_buffer_1.data() }, std::u16string { message.text_buffer_2.data() }, kind });
        message.skip_counter_update = 1;
        if (kind == 6) {
            queue(c, 417);
        } else if (kind == 7) {
            message.tag_id = 95;
            queue(c, 419);
        } else {
            message.tag_id = 94;
            queue(c, 418);
        }
    }
}
} //namespace

bool handle_world_tag_system_event(MissionWorld& c, TagSystemRecord& system, const WorldEvent& event) {
    if (event.type() != 24 && event.type() != 27 && event.type() != 25) {
        return false;
    }
    CompanyEmailMessage* email = nullptr;
    if (event.type() == 25) {
        email = event.get<HeapPayloadEvent>().payload;
        if (!email) {
            throw std::invalid_argument("null company email payload");
        }
        //Native rejects an unknown non151 tag before formatting/insertion. Every
        //compiled CompanyEmailClass has its actual formatter below, including
        //the previously omitted initial-mission/victory and objective classes.
        if (email->tag_id != 151 && !tag_catalog::find(email->tag_id)) {
            return true;
        }
    }
    auto* storage = c.ports.memory.allocate(Allocation::mail_message, sizeof(MailMessage));
    if (!storage) {
        throw std::bad_alloc();
    }
    auto* message = std::construct_at(static_cast<MailMessage*>(storage));
    bool retain = true;
    try {
        set_date(c, *message);
        if (event.type() == 24) {
            retain = advisor_tag_notice(c, system, *message, event.get<TagMailEvent>());
        } else if (email) {
            company_notice(c, *message, *email);
        } else {
            challenge_notice(c, *message, event.get<ChallengeNotificationEvent>());
        }
        if (retain) {
            insert_tag_mail_message(c, &system, message, false);
        }
    } catch (...) {
        bool linked = false;
        for (auto* item = system.mail_message_head; item; item = item->next) {
            if (item == message) {
                linked = true;
                break;
            }
        }
        if (!linked) {
            std::destroy_at(message);
            c.ports.memory.release(Allocation::mail_message, message);
        }
        throw;
    }
    if (!retain) {
        std::destroy_at(message);
        c.ports.memory.release(Allocation::mail_message, message);
    }
    return true;
}
} //namespace simcoaster
