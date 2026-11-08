#include "simcoaster/world_mail.hpp"
#include "simcoaster/legacy_mail_format.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_save.hpp"
#include <algorithm>
#include <bit>
#include <memory>
#include <new>
#include <optional>

namespace simcoaster {
namespace {
std::int32_t add_wrapped(std::int32_t value, std::uint32_t amount) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value) + amount);
}

TagMailCounterState* allocate_counter(WorldRuntime& c) {
    auto* storage = static_cast<TagMailCounterState*>(
        c.ports.memory.allocate(Allocation::mail_counter, sizeof(TagMailCounterState)));
    if (!storage)
        throw std::bad_alloc();
    auto* counter = std::construct_at(storage);
    try {
        return initialize_tag_mail_counter_state(c, counter);
    } catch (...) {
        //Both construction and restoration release the owner when allocating its
        //four counters fails.
        c.ports.memory.release(Allocation::mail_counter, counter);
        throw;
    }
}

void increment_counter(WorldRuntime& c, TagMailCounterState* counter, int slot, bool append) {
    counter->total_count = add_wrapped(counter->total_count, 1);
    counter->per_slot_counts[slot] = add_wrapped(counter->per_slot_counts[slot], 1);
    if (counter->total_count == counter->notification_threshold) {
        if (!append)
            world_operations::queue_scored_advisor_action(c, counter->notification_threshold == 3 ? 408 : 349);
        counter->notification_threshold =
            add_wrapped(counter->notification_threshold, std::bit_cast<std::uint32_t>(counter->notification_threshold));
    }
    if (c.ui.mail_counter_root) {
        auto* control = c.ports.ui.find_ui_control(c, c.ui.mail_counter_root, 0x46u);
        if (control) {
            int selected = 3;
            while (!counter->per_slot_counts[selected] && selected > 0)
                --selected;
            c.ports.ui.send_ui_count_value(c, control, counter->total_count);
            auto* button = c.ports.ui.find_ui_control(c, control, 0x47u);
            c.ports.ui.set_ui_visibility(c, button, 1);
            const auto animation = selected == 2 ? MailCountAnimation::slot_2
                                 : selected == 3 ? MailCountAnimation::slot_3
                                                 : MailCountAnimation::other;
            button = c.ports.ui.find_ui_control(c, control, 0x47u); //Original repeats the lookup.
            c.ports.ui.start_mail_count_animation(c, button, animation, 10);
            if (!append)
                c.ports.audio.submit_mail_notification_template(c, c.resources.ui_catalog_slot, 0x1Eu);
        }
    }
}

void decrement_counter(WorldRuntime& c, TagMailCounterState* counter, int slot) {
    counter->total_count = add_wrapped(counter->total_count, 4294967295u);
    counter->per_slot_counts[slot] = add_wrapped(counter->per_slot_counts[slot], 4294967295u);
    if (c.ui.mail_counter_root) {
        auto* control = c.ports.ui.find_ui_control(c, c.ui.mail_counter_root, 0x46u);
        if (control)
            c.ports.ui.send_ui_count_value(c, control, counter->total_count);
    }
}

void drain_pending(WorldRuntime& c, TagSystemRecord* system) {
    while (system->pending_free_list) {
        c.ports.memory.release(Allocation::mail_message, system->pending_free_list->message);
        auto* node = system->pending_free_list;
        system->pending_free_list = node->next;
        c.ports.memory.release(Allocation::pending_mail_free, node);
    }
}

PendingMailMessageFreeNode*
queue_free(WorldRuntime& c, TagSystemRecord* system, PendingMailMessageFreeNode* tail, MailMessage* message) {
    auto* node = static_cast<PendingMailMessageFreeNode*>(
        c.ports.memory.allocate(Allocation::pending_mail_free, sizeof(PendingMailMessageFreeNode)));
    if (!node)
        throw std::bad_alloc();
    std::construct_at(node);
    if (tail)
        tail->next = node;
    else
        system->pending_free_list = node;
    node->next = nullptr;
    node->message = message;
    return node;
}

template <class T>
bool field(SaveArchive archive, T& value, std::string_view label) {
    static_assert(std::same_as<T, std::int32_t> || std::same_as<T, legacy::MailMessage32>);
    archive.services.field_header(sizeof(T), label, archive.writing);
    if (archive.writing)
        return archive.stream.write({ reinterpret_cast<const std::byte*>(&value), sizeof(T) }) == sizeof(T);
    return archive.stream.read({ reinterpret_cast<std::byte*>(&value), sizeof(T) }) == sizeof(T);
}

legacy::MailMessage32 mail_image(const MailMessage& message) {
    //Obsolete/reserved wire bytes have a defined zero value. They do not read
    //unspecified runtime fields or leak host storage into the original format.
    return { .tag_id = message.tag_id,
             .dedupe_target_tag_id = message.dedupe_target_tag_id,
             .rendered_message = message.rendered_message,
             .text_buffer_1 = message.text_buffer_1,
             .text_buffer_2 = message.text_buffer_2,
             .text_buffer_3 = message.text_buffer_3,
             .auxiliary_field = message.auxiliary_field,
             .expiry_delay_units = message.expiry_delay_units,
             .unknown_3590 = 0,
             .next_expiry_tick = message.next_expiry_tick,
             .skip_counter_update = message.skip_counter_update,
             .unknown_3597 = 0,
             .counter_slot = message.counter_slot,
             .message_code = message.message_code,
             .format_word = message.format_word,
             .unknown_3606 = 0,
             .format_dword = message.format_dword,
             .creation_sequence = message.creation_sequence,
             .obsolete_next_link = 0,
             .packed_date = message.packed_date,
             .marked_for_deletion = message.marked_for_deletion,
             .reserved_tail = {} };
}

std::optional<std::array<std::size_t, 4>> terminated_text_lengths(const legacy::MailMessage32& image) {
    const std::array<std::span<const char16_t>, 4> texts{ image.rendered_message,
                                                          image.text_buffer_1,
                                                          image.text_buffer_2,
                                                          image.text_buffer_3 };
    std::array<std::size_t, 4> lengths;
    for (std::size_t i = 0; i < texts.size(); ++i) {
        const auto terminator = std::ranges::find(texts[i], char16_t{});
        if (terminator == texts[i].end())
            return std::nullopt;
        lengths[i] = static_cast<std::size_t>(terminator - texts[i].begin());
    }
    return lengths;
}
} //namespace

TagMailCounterState* initialize_tag_mail_counter_state(WorldRuntime& c, TagMailCounterState* counter) {
    counter->total_count = 0;
    counter->notification_threshold = 3;
    counter->per_slot_counts =
        static_cast<std::int32_t*>(c.ports.memory.allocate(Allocation::mail_counter_slots, 4 * sizeof(std::int32_t)));
    if (!counter->per_slot_counts)
        throw std::bad_alloc();
    for (int i = 0; i < 4; ++i)
        counter->per_slot_counts[i] = 0;
    return counter;
}

TagSystemRecord* initialize_tag_system(WorldRuntime& c, TagSystemRecord* system) {
    initialize_world_map_object_base(c, &system->base, 1, 12);
    for (int category : { 24, 25, 27 })
        register_world_used_thing_for_event(c,
                                            get_world_event_subscription_registry(c),
                                            &system->base.header,
                                            category);
    system->mail_message_head = nullptr;
    system->mail_counter_state = allocate_counter(c);
    system->next_expiry_tick = 0;
    system->pending_free_list_cleanup = 0;
    system->messages_need_unlink = 0;
    system->pending_free_list = nullptr;
    return system;
}

void insert_tag_mail_message(WorldRuntime& c, TagSystemRecord* system, MailMessage* message, bool append) {
    message->marked_for_deletion = 0;
    if (message->expiry_delay_units) {
        const auto expiry = c.simulation.current->saved.game_tick + 4u * message->expiry_delay_units;
        message->next_expiry_tick = expiry;
        if (!system->next_expiry_tick || expiry < system->next_expiry_tick)
            system->next_expiry_tick = expiry;
    }
    message->creation_sequence = c.messages.tag_mail_next_creation_sequence++;
    message->next = nullptr;
    if (auto* head = system->mail_message_head) {
        MailMessage* replaced = nullptr;
        MailMessage* last = nullptr;
        for (auto* existing = head; existing; existing = existing->next) {
            if (!append
                && existing->tag_id == message->dedupe_target_tag_id
                && (message->message_code != 1 || message->format_word == existing->format_word)
                && message->format_dword == existing->format_dword
                && !existing->marked_for_deletion) {
                replaced = existing;
                break;
            }
            last = existing;
        }
        if (append)
            last->next = message;
        else {
            message->next = head;
            system->mail_message_head = message;
            if (replaced) {
                replaced->marked_for_deletion = 1;
                system->messages_need_unlink = 1;
            }
            c.messages.tag_panel_refresh_requested = 1;
        }
    } else {
        system->mail_message_head = message;
        c.messages.tag_panel_refresh_requested = 1;
    }
    if (!message->skip_counter_update)
        increment_counter(c, system->mail_counter_state, std::bit_cast<std::int16_t>(message->counter_slot), append);
}

int mark_tag_messages_by_key_set_and_format_word_for_deletion(WorldRuntime& c,
                                                              TagSystemRecord* system,
                                                              std::span<const std::int32_t> keys,
                                                              std::uint16_t format_word) {
    int count = 0;
    for (auto* message = system->mail_message_head; message; message = message->next) {
        if (message->format_word != format_word
            || message->marked_for_deletion
            || std::ranges::find(keys, message->tag_id) == keys.end())
            continue;
        message->marked_for_deletion = 1;
        system->messages_need_unlink = 1;
        ++count;
    }
    if (count)
        c.messages.tag_panel_refresh_requested = 1;
    return count;
}

int mark_tag_messages_by_format_word_for_deletion(WorldRuntime& c, TagSystemRecord* system, std::uint16_t format_word) {
    int count = 0;
    for (auto* message = system->mail_message_head; message; message = message->next) {
        if (message->format_word != format_word || message->marked_for_deletion)
            continue;
        message->marked_for_deletion = 1;
        system->messages_need_unlink = 1;
        ++count;
    }
    if (count)
        c.messages.tag_panel_refresh_requested = 1;
    return count;
}

void update_tag_system_lifecycle(WorldRuntime& c, TagSystemRecord* system) {
    if (system->pending_free_list_cleanup) {
        system->pending_free_list_cleanup = 0;
        drain_pending(c, system);
        system->pending_free_list = nullptr;
    }
    if (system->messages_need_unlink) {
        auto* current = system->mail_message_head;
        MailMessage* previous = nullptr;
        PendingMailMessageFreeNode* tail = nullptr;
        system->pending_free_list = nullptr;
        system->messages_need_unlink = 0;
        for (; current; current = current->next) {
            if (current->marked_for_deletion) {
                if (!current->skip_counter_update) {
                    current->skip_counter_update = 1;
                    if (system->mail_counter_state)
                        decrement_counter(c,
                                          system->mail_counter_state,
                                          std::bit_cast<std::int16_t>(current->counter_slot));
                }
                if (!previous || current == system->mail_message_head)
                    system->mail_message_head = current->next;
                else
                    previous->next = current->next;
                tail = queue_free(c, system, tail, current);
                system->pending_free_list_cleanup = 1;
                if (current->marked_for_deletion)
                    continue; //Recheck after counter/UI callbacks.
            }
            previous = current;
        }
    }
    if (system->next_expiry_tick && c.simulation.current->saved.game_tick >= system->next_expiry_tick) {
        const auto tick = c.simulation.current->saved.game_tick;
        system->next_expiry_tick = 0;
        for (auto* message = system->mail_message_head; message; message = message->next) {
            if (message->next_expiry_tick && !message->marked_for_deletion) {
                if (message->next_expiry_tick > tick) {
                    if (!system->next_expiry_tick || message->next_expiry_tick < system->next_expiry_tick)
                        system->next_expiry_tick = message->next_expiry_tick;
                } else {
                    message->marked_for_deletion = 1;
                    system->messages_need_unlink = 1;
                }
            }
        }
        c.messages.tag_panel_refresh_requested = 1;
    }
}

void release_tag_system_resources(WorldRuntime& c, TagSystemRecord* system) {
    //Pending messages have already been detached from the active mail chain.
    //This resource-only path also works before a restored payload is published
    //in the metadata table; it deliberately makes no map/event/UI calls.
    drain_pending(c, system);
    while (auto* message = system->mail_message_head) {
        system->mail_message_head = message->next;
        c.ports.memory.release(Allocation::mail_message, message);
    }
    if (auto* counter = system->mail_counter_state) {
        if (counter->per_slot_counts)
            c.ports.memory.release(Allocation::mail_counter_slots, counter->per_slot_counts);
        c.ports.memory.release(Allocation::mail_counter, counter);
        system->mail_counter_state = nullptr;
    }
    system->mail_message_head = nullptr;
    system->pending_free_list = nullptr;
    system->next_expiry_tick = 0;
    system->pending_free_list_cleanup = 0;
    system->messages_need_unlink = 0;
}

int mark_tag_messages_by_primary_key_for_deletion(WorldRuntime& c, TagSystemRecord* system, std::int32_t primary_key) {
    int marked = 0;
    for (auto* message = system->mail_message_head; message; message = message->next)
        if (message->tag_id == primary_key && !message->marked_for_deletion) {
            message->marked_for_deletion = 1;
            system->messages_need_unlink = 1;
            ++marked;
        }
    if (marked)
        c.messages.tag_panel_refresh_requested = 1;
    return marked;
}

int mark_matching_mail_for_deletion(WorldRuntime& c,
                                    TagSystemRecord* system,
                                    std::int32_t tag_id,
                                    std::int32_t format_dword) {
    int marked = 0;
    for (auto* message = system->mail_message_head; message; message = message->next)
        if (message->tag_id == tag_id && message->format_dword == format_dword && !message->marked_for_deletion) {
            message->marked_for_deletion = 1;
            system->messages_need_unlink = 1;
            ++marked;
        }
    if (marked)
        c.messages.tag_panel_refresh_requested = 1;
    return marked;
}

void destroy_tag_system(WorldRuntime& c, TagSystemRecord* system) {
    if (system->pending_free_list_cleanup)
        drain_pending(c, system);
    system->pending_free_list = nullptr;
    for (auto* m = system->mail_message_head; m; m = m->next)
        m->marked_for_deletion = 1;
    MailMessage* previous = nullptr;
    PendingMailMessageFreeNode* tail = nullptr;
    for (auto* m = system->mail_message_head; m; m = m->next) {
        if (!previous || m == system->mail_message_head)
            system->mail_message_head = system->mail_message_head->next;
        else
            previous->next = m->next;
        tail = queue_free(c, system, tail, m);
        previous = m;
    }
    drain_pending(c, system);
    system->mail_message_head = nullptr;
    system->pending_free_list = nullptr;
    system->next_expiry_tick = 0;
    system->pending_free_list_cleanup = 0;
    system->messages_need_unlink = 0;
    if (auto* counter = system->mail_counter_state) {
        c.ports.memory.release(Allocation::mail_counter_slots, counter->per_slot_counts);
        c.ports.memory.release(Allocation::mail_counter, counter);
        system->mail_counter_state = nullptr;
    }
    unlink_world_used_thing_map_and_event_lists(c, &system->base.header);
}

int transfer_tag_system_state(WorldRuntime& c, TagSystemRecord* system, SaveArchive archive) {
    if (!transfer_map_object_base_state(&system->base, archive))
        return 1;
    if (archive.schema > 1)
        return 0;
    std::int32_t count = 0;
    if (archive.writing) {
        for (auto* m = system->mail_message_head; m; m = m->next)
            if (!m->marked_for_deletion)
                count = add_wrapped(count, 1);
        if (!field(archive, count, "numtags"))
            return 1;
        for (auto* m = system->mail_message_head; m; m = m->next)
            if (!m->marked_for_deletion) {
                auto image = mail_image(*m);
                if (!field(archive, image, "MailMessage"))
                    return 1;
            }
    } else {
        system->mail_message_head = nullptr;
        system->mail_counter_state = allocate_counter(c);
        system->next_expiry_tick = 0;
        system->pending_free_list_cleanup = 0;
        system->messages_need_unlink = 0;
        system->pending_free_list = nullptr;
        if (!field(archive, count, "numtags"))
            return 1;
        if (count < 0)
            return 1;
        for (std::int32_t i = 0; i < count; ++i) {
            legacy::MailMessage32 image{};
            if (!field(archive, image, "MailMessage"))
                return 1;
            const auto lengths = terminated_text_lengths(image);
            if (!lengths)
                return 1;
            if (image.counter_slot >= 4)
                return 1;
            auto* storage =
                static_cast<MailMessage*>(c.ports.memory.allocate(Allocation::mail_message, sizeof(MailMessage)));
            if (!storage)
                throw std::bad_alloc();
            auto* m = std::construct_at(storage);
            m->next = nullptr;
            m->tag_id = image.tag_id;
            m->dedupe_target_tag_id = image.dedupe_target_tag_id;
            m->expiry_delay_units = image.expiry_delay_units;
            m->skip_counter_update = image.skip_counter_update;
            m->counter_slot = image.counter_slot;
            m->message_code = image.message_code;
            m->format_word = image.format_word;
            m->format_dword = image.format_dword;
            std::copy_n(image.rendered_message.begin(), (*lengths)[0] + 1, m->rendered_message.begin());
            std::copy_n(image.text_buffer_1.begin(), (*lengths)[1] + 1, m->text_buffer_1.begin());
            std::copy_n(image.text_buffer_2.begin(), (*lengths)[2] + 1, m->text_buffer_2.begin());
            std::copy_n(image.text_buffer_3.begin(), (*lengths)[3] + 1, m->text_buffer_3.begin());
            m->packed_date = image.packed_date;
            m->auxiliary_field = image.auxiliary_field;
            m->marked_for_deletion = image.marked_for_deletion;
            insert_tag_mail_message(c, system, m, true);
        }
        c.messages.tag_panel_refresh_requested = 1;
    }
    return 0;
}
} //namespace simcoaster
