#include "simcoaster/calendar_runtime.hpp"
#include "company_email_internal.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_research.hpp"
#include <algorithm>
#include <memory>
#include <utility>

namespace simcoaster {
namespace {
struct SpamTemplate {
    std::int32_t tag, body, sender;
};

//Populated native CTagTable rows at9D3480, not inferred from tag ordering.
constexpr std::array spam_templates{
    SpamTemplate{ 115, 85, 191 },  SpamTemplate{ 116, 86, 191 },  SpamTemplate{ 117, 87, 191 },
    SpamTemplate{ 118, 88, 191 },  SpamTemplate{ 119, 89, 191 },  SpamTemplate{ 120, 90, 191 },
    SpamTemplate{ 121, 91, 191 },  SpamTemplate{ 122, 92, 191 },  SpamTemplate{ 123, 93, 184 },
    SpamTemplate{ 125, 95, 178 },  SpamTemplate{ 126, 96, 184 },  SpamTemplate{ 127, 97, 186 },
    SpamTemplate{ 128, 98, 180 },  SpamTemplate{ 130, 100, 184 }, SpamTemplate{ 131, 101, 184 },
    SpamTemplate{ 132, 102, 184 }, SpamTemplate{ 133, 103, 184 }
};

TagMessageVariant* variant(EconomyWorld& c, std::int32_t id) {
    auto* found = find_tag_system_message_variant_by_id(c, id);
    if (!found)
        throw std::logic_error("calendar mail sender variant");
    return found;
}

std::u16string text(EconomyWorld& c,
                    std::int32_t row,
                    std::size_t capacity = 255,
                    std::span<const BfstWideStringArgument> strings = {},
                    std::span<const BfstSignedIntegerArgument> integers = {}) {
    auto value = format_bfst_wide_string(c.messages.mail_language.tag_system,
                                         c.messages.mail_language.conversion,
                                         static_cast<std::uint32_t>(row),
                                         capacity,
                                         strings,
                                         integers);
    if (!value)
        throw std::runtime_error("calendar mail BFST: " + value.error().reason);
    return std::move(*value);
}

template <std::size_t N>
void copy(std::array<char16_t, N>& out, std::u16string_view value) {
    if (value.size() >= N)
        throw std::length_error("calendar mail destination");
    std::ranges::copy(value, out.begin());
    out[value.size()] = 0;
}

void post(EconomyWorld& c, CompanyEmailMessage email) {
    auto* storage = static_cast<CompanyEmailMessage*>(
        c.ports.memory.allocate(Allocation::event_payload, sizeof(CompanyEmailMessage)));
    if (!storage)
        throw std::bad_alloc();
    auto* payload = std::construct_at(storage, std::move(email));
    dispatch_heap_payload_event(c, payload);
}

std::u16string wrap_subject(EconomyWorld& c, std::u16string_view subject, std::int32_t row) {
    const std::array strings{ BfstWideStringArgument{ 7, subject } };
    return text(c, row, 255, strings);
}

std::u16string spam_subject(EconomyWorld& c, SpamCategory category) {
    auto& s = c.finance.spam_scheduler;
    match (category) {
        case SpamCategory::joke => {
            const auto subject = text(c, 82 + next_crt_random(c) % 3);
            s.previous_joke_subject = wrap_subject(c, subject, 195 + (next_crt_random(c) % 2 != 0));
            return s.previous_joke_subject;
        }
        case SpamCategory::joke_response => return wrap_subject(c, s.previous_joke_subject, 194);
        case SpamCategory::instruction => {
            const std::array integers{ BfstSignedIntegerArgument{ 3, next_crt_random(c) % 254 + 1 } };
            return text(c, 94, 255, {}, integers);
        }
        case SpamCategory::controversy => {
            const auto subject = text(c, 99);
            const bool prior = std::exchange(s.special_text_flag, true);
            return prior ? wrap_subject(c, subject, 194) : subject;

        //Native categories4/5 return success without touching the initialized
        //buffer.
        }
        case SpamCategory::urban_legend or SpamCategory::brain_teaser => return {};
        case _ => return text(c, 193);
    }
}

std::u16string spam_sender(EconomyWorld& c, CompanyEmailMessage& email, std::int32_t row) {
    if (row == 191) {
        int attempts = 0;
        for (;;) {
            email.selected_variant = variant(c, next_crt_random(c) % 9);
            if (attempts++ > 100) {
                email.selected_variant = variant(c, 0);
                break;
            }
            if (email.selected_variant->available)
                break;
        }
        row = email.selected_variant->category_resource_id;
    }
    auto result = text(c, row);
    if (email.spam_category == SpamCategory::joke)
        c.finance.spam_scheduler.previous_joke_sender = result;
    return result;
}

std::u16string spam_recipient(EconomyWorld& c, SpamCategory category) {
    match (category) {
        case SpamCategory::joke or SpamCategory::instruction or SpamCategory::urban_legend or SpamCategory::brain_teaser => {
            return text(c, 187);
        }
        case SpamCategory::joke_response => {
            const auto staff = text(c, 187);
            const std::array arguments{ BfstWideStringArgument{ 5, c.finance.spam_scheduler.previous_joke_sender },
                                        BfstWideStringArgument{ 6, staff } };
            return text(c, 190, 255, arguments);
        }
        case _ => return text(c, 188);
    }
}

std::u16string number(EconomyWorld& c, std::int32_t row, std::uint32_t key, std::int32_t value) {
    //The annual-report caller constructs MRAV with the magnitude in its
    //value field. Its separate negative flag is ignored by the native
    //integer formatter; the selected resource supplies loss/decrease wording.
    const std::array integers{ BfstSignedIntegerArgument{ key, fixed::absolute(value) } };
    return text(c, row, 1024, {}, integers);
}

std::u16string report_body(EconomyWorld& c, const CompanyEmailMessage& email) {
    const auto* r = email.annual_report;
    if (!r)
        throw std::logic_error("annual mail report record");
    auto body = number(c, email.report_body, 10, email.report_trend);
    if (email.report_body != 263)
        body += u"\n\n";
    const auto change = [&](std::int32_t value) {
        return r->report_year <= 2000 ? std::u16string{ u"\n" }
                                      : number(c,
                                               value > 0   ? 271
                                               : value < 0 ? 272
                                                           : 273,
                                               11,
                                               value);
    };
    const auto& d = r->year_over_year;
    body += number(c, 274, 12, fixed::truncate_integer(r->history_sum_a));
    body += change(fixed::truncate_integer(d.income_change));
    body += number(c, r->history_difference < 0 ? 276 : 275, 13, r->history_difference);
    body += change(d.profit_change);
    body += number(c, 277, 14, fixed::truncate_integer(r->history_sum_b));
    body += change(fixed::truncate_integer(d.park_value_change));
    body += number(c, 278, 15, r->history_sum);
    body += change(d.visitor_change);
    body += number(c, 279, 16, r->history_average);
    body += change(d.rating_difference);
    body += text(c, email.report_summary, 1024);
    return body;
}
} //namespace

void dispatch_spam_email(EconomyWorld& c, std::int32_t resource, SpamCategory category) {
    CompanyEmailMessage email{};
    email.concrete_class = CompanyEmailClass::spam;
    email.reason = CompanyEmailMessageKind::spam;
    email.context_mode = 1;
    email.default_resource = 151;
    email.tag_id = resource;
    email.spam_resource = resource;
    email.spam_category = category;
    email.selected_variant = variant(c, 0);
    email.append_signature = category == SpamCategory::joke_response
                          || category == SpamCategory::instruction
                          || category == SpamCategory::controversy;
    post(c, std::move(email));
}

void
dispatch_share_report_email(EconomyWorld& c, const ShareFinanceSavedRecord& report, int body, int summary, int trend) {
    CompanyEmailMessage email{};
    email.concrete_class = CompanyEmailClass::annual_report;
    email.reason = CompanyEmailMessageKind::annual_report;
    email.context_mode = 3;
    email.default_resource = 151;
    //5CE090 keeps the constructor's no-CTag sentinel. Only the output virtual
    //5CEA50 assigns mailbox marker139; using it here fails635D80's tag lookup.
    email.tag_id = 151;
    email.counter_slot = 3;
    email.append_signature = 1;
    email.selected_variant = variant(c, 2);
    email.annual_report = &report;
    email.report_body = body;
    email.report_summary = summary;
    email.report_trend = trend;
    post(c, std::move(email));
}

void format_calendar_company_notice(EconomyWorld& c, MailMessage& message, CompanyEmailMessage& email) {
    std::u16string subject, sender, recipient, body;
    if (email.concrete_class == CompanyEmailClass::spam) {
        const auto found = std::ranges::find(spam_templates, email.spam_resource, &SpamTemplate::tag);
        if (found == spam_templates.end())
            throw std::out_of_range("spam tag-table entry");
        //Binding precedes all formatter virtuals and can change the selected
        //sender.
        for (auto& row : c.messages.tag_system_message_variants)
            if (row.category_resource_id == found->sender || row.secondary_resource_id == found->sender) {
                email.selected_variant = &row;
                break;
            }
        subject = spam_subject(c, email.spam_category);
        sender = spam_sender(c, email, found->sender);
        recipient = spam_recipient(c, email.spam_category);
        body = text(c, found->body, 1024);
    } else {
        subject = text(c, 262);
        sender = text(c, 177);
        recipient = text(c, 188);
        body = report_body(c, email);
    }
    if (email.append_signature) {
        if (!email.selected_variant)
            throw std::logic_error("calendar mail signature variant");
        body += u'\n';
        body += text(c, email.selected_variant->category_resource_id);
        body += u'\n';
        body += text(c, email.selected_variant->secondary_resource_id);
    }
    copy(message.text_buffer_3, subject);
    copy(message.text_buffer_1, sender);
    copy(message.text_buffer_2, recipient);
    copy(message.rendered_message, body);
    message.tag_id = email.concrete_class == CompanyEmailClass::annual_report ? 139 : email.tag_id;
    message.dedupe_target_tag_id = 151;
    message.expiry_delay_units = 0;
    message.message_code = 0;
    message.format_word = 0;
    message.format_dword = 0;
    message.counter_slot = static_cast<std::uint16_t>(email.counter_slot);
    message.auxiliary_field = email.selected_variant->variant_code;
}
} //namespace simcoaster
