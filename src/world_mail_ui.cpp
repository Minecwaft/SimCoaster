#include "simcoaster/world_mail_ui.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_staff.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
TagSystemRecord* tag_system(WorldRuntime& c) {
    const auto* world = c.referenced_world();
    if (!world || !world->saved.tag_system) {
        return nullptr;
    }
    auto* record = c.entities.at(world->saved.tag_system).payload;
    if (!record || record->kind != ThingKind::tag_system) {
        throw std::runtime_error("Message Center has an invalid TagSystem owner");
    }
    return reinterpret_cast<TagSystemRecord*>(record);
}

template <std::size_t N>
std::u16string text(const std::array<char16_t, N>& source) {
    return { source.begin(), std::ranges::find(source, char16_t {}) };
}

bool has_staff(WorldRuntime& c, ThingKind kind) {
    for (auto* link = c.entities.active_head; link; link = link->active_next) {
        if (link->payload && link->payload->kind == kind && !link->payload->retirement_pending) {
            return true;
        }
    }
    return false;
}

ThingHeader* thing(WorldRuntime& c, ThingId id) {
    auto* header = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
    return header && header->id == id && !header->retirement_pending ? header : nullptr;
}
} //namespace

MailMessage* find_ui_mail(WorldRuntime& c, WorldMailIdentity key) {
    auto* system = tag_system(c);
    if (!system) {
        return nullptr;
    }
    for (auto* message = system->mail_message_head; message; message = message->next) {
        if (!message->marked_for_deletion && message->tag_id == key.tag && message->creation_sequence == key.sequence) {
            return message;
        }
    }
    return nullptr;
}

std::vector<WorldMailUiRow> world_mail_ui_rows(WorldRuntime& c) {
    std::vector<WorldMailUiRow> result;
    const auto* system = tag_system(c);
    if (!system) {
        return result;
    }
    for (auto* message = system->mail_message_head; message; message = message->next) {
        if (message->marked_for_deletion) {
            continue;
        }
        const auto date = message->packed_date;
        const std::array arguments { BfstSignedIntegerArgument { 41, static_cast<std::int32_t>(date & 255u) }, BfstSignedIntegerArgument { 42, static_cast<std::int32_t>((date >> 8) & 255u) + 1 }, BfstSignedIntegerArgument { 43, std::bit_cast<std::int32_t>(date) >> 16 } };
        auto formatted = format_bfst_wide_string(c.messages.mail_language.ui_text, c.messages.mail_language.conversion, 956, 63, {}, arguments);
        if (!formatted) {
            throw std::runtime_error("Message Center calendar date: " + formatted.error().reason);
        }
        result.push_back({ { message->tag_id, message->creation_sequence }, text(message->text_buffer_3), std::move(*formatted), date, message->skip_counter_update != 0, message->counter_slot });
    }
    return result;
}

bool consume_ui_mail(WorldRuntime& c, WorldMailIdentity key) {
    auto* message = find_ui_mail(c, key);
    if (!message || message->skip_counter_update) {
        return false;
    }
    auto* system = tag_system(c);
    auto* counter = system->mail_counter_state;
    const auto slot = std::bit_cast<std::int16_t>(message->counter_slot);
    if (!counter || !counter->per_slot_counts || slot < 0 || slot >= 4) {
        throw std::runtime_error("Invalid Message Center unread counter bucket");
    }
    //Native553480/636820 decrements even bucket0, exactly once per message.
    message->skip_counter_update = 1;
    counter->total_count = fixed::subtract(counter->total_count, 1);
    counter->per_slot_counts[slot] = fixed::subtract(counter->per_slot_counts[slot], 1);
    if (c.ui.mail_counter_root) {
        if (auto* control = c.ports.ui.find_ui_control(c, c.ui.mail_counter_root, 70)) {
            c.ports.ui.send_ui_count_value(c, control, counter->total_count);
        }
    }
    return true;
}

bool delete_ui_mail(WorldRuntime& c, WorldMailIdentity key) {
    auto* message = find_ui_mail(c, key);
    if (!message) {
        return false;
    }
    //636A40 matches the pair rather than pointer identity. Unlink/free and
    //unread-count consumption remain the TagSystem lifecycle's next stages.
    message->marked_for_deletion = 1;
    tag_system(c)->messages_need_unlink = 1;
    c.messages.tag_panel_refresh_requested = 1;
    return true;
}

std::optional<WorldMailUiAction> resolve_world_mail_ui_action(WorldRuntime& c, const MailMessage& message) {
    auto selector = message.message_code;
    if (selector == 19) {
        selector = has_staff(c, ThingKind::mechanic) ? 1 : 16;
    } else if (selector == 20) {
        selector = has_staff(c, ThingKind::guard) ? 4 : 23;
    }
    WorldMailUiAction action;
    action.target = message.format_word;
    action.argument = message.format_dword;
    using K = WorldMailUiActionKind;
    match (selector) {
        case 1 => action.kind = K::focus_thing;
        case 2 => {
            action.kind = K::focus_thing;
            action.target = 0;
            for (auto* link = c.entities.active_head; link; link = link->active_next) {
                auto* record = link->payload;
                if (record && !record->retirement_pending && record->kind == ThingKind::park_object && reinterpret_cast<ParkObjectRuntimeView*>(record)->definition_id == static_cast<std::uint16_t>(message.format_dword)) {
                    action.target = record->id;
                    break;
                }
            }
            match (message.format_dword) {
                case 1702 => {
                    action.x_offset = 60;
                    action.y_offset = 15;
                }
                case 1703 => {
                    action.x_offset = 255;
                    action.y_offset = 50;
                }
                case 2702 => action.x_offset = 30;
                case 2703 => {
                    action.x_offset = 55;
                    action.y_offset = 25;
                }
                case 3701 => {
                    action.x_offset = 50;
                    action.y_offset = 10;
                }
                case 3703 => {
                    action.x_offset = 20;
                    action.y_offset = 35;
                }
                case _ => {
                }
            }
        }
        case 3 => {
            action.kind = K::focus_cell;
            action.argument = static_cast<std::uint16_t>(message.format_dword);
            return action;
        }
        case 4                      => action.kind = K::visitor_locator;
        case 5                      => action.kind = K::staff_locator;
        case 6 or 7 or 8 or 9 or 10 => {
            action.kind = K::build;
            action.category = selector == 6 ? ObjectUiCategory::rides : selector == 7 ? ObjectUiCategory::shops : selector == 8 ? ObjectUiCategory::sideshows : selector == 9 ? ObjectUiCategory::features : ObjectUiCategory::facilities;
            action.argument = static_cast<std::uint16_t>(message.format_dword);
            return action;
        }
        case 11 => {
            action.kind = K::challenges;
            return action;
        }
        case 12 => {
            action.kind = K::objectives;
            return action;
        }
        case 13 => {
            action.kind = K::research;
            return action;
        }
        case 14 => action.kind = K::wage_demand;
        case 15 => {
            action.kind = K::loans;
            return action;
        }
        case 16 or 17 or 21 or 22 or 23 or 24 => {
            action.kind = K::hire;
            action.staff_type = selector == 16 ? StaffType::mechanic : selector == 17 ? StaffType::gardener : selector == 21 ? StaffType::handyman : selector == 22 ? StaffType::entertainer : selector == 23 ? StaffType::guard : StaffType::researcher;
            return action;
        }
        case 18 => {
            action.kind = K::overview;
            return action;
        }
        case 25 => {
            action.kind = K::park_gates;
            return action;
        }
        case 27 => action.kind = K::maintenance;
        case 28 => {
            action.kind = K::park_statistics;
            return action;
        }
        case _ => {
            return {}; //Native26 and unknown selectors deliberately do nothing.
        }
    } action.identity = thing(c, action.target);
    if (!action.identity) {
        return {};
    }
    if ((action.kind == K::visitor_locator && action.identity->kind != ThingKind::person) || (action.kind == K::staff_locator && !is_staff_record_kind(action.identity->kind)) || (action.kind == K::wage_demand && !is_staff_record_kind(action.identity->kind)) || (action.kind == K::maintenance && action.identity->kind != ThingKind::park_object)) {
        return {};
    }
    return action;
}
} //namespace simcoaster
