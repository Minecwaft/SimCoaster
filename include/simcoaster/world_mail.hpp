#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
struct Stream;
struct SaveArchive;

struct MailMessage {
    std::int32_t tag_id, dedupe_target_tag_id;
    std::array<char16_t, 1024> rendered_message;
    std::array<char16_t, 255> text_buffer_1, text_buffer_2, text_buffer_3;
    std::uint16_t auxiliary_field, expiry_delay_units, unknown_3590;
    std::uint32_t next_expiry_tick;
    std::uint8_t skip_counter_update, unknown_3597;
    std::uint16_t counter_slot;
    std::int32_t message_code; //Code1 also matches format_word during deduplication.
    std::uint16_t format_word, unknown_3606;
    std::int32_t format_dword;
    std::uint32_t creation_sequence;
    MailMessage* next;
    std::uint32_t packed_date;
    std::uint8_t marked_for_deletion;
    std::array<std::byte, 3> reserved_tail;
};

struct TagMailCounterState {
    std::int32_t total_count, notification_threshold;
    std::int32_t* per_slot_counts; //Four separately allocated counters.
};

struct PendingMailMessageFreeNode {
    MailMessage* message;
    PendingMailMessageFreeNode* next;
};

struct TagSystemRecord {
    MapObjectBase base;
    std::array<char16_t, 1024> formatted_message_scratch;
    std::uint32_t unknown_2068;
    MailMessage* mail_message_head;
    TagMailCounterState* mail_counter_state;
    std::uint32_t next_expiry_tick;
    std::uint8_t pending_free_list_cleanup, messages_need_unlink;
    std::array<std::byte, 2> unknown_2086;
    PendingMailMessageFreeNode* pending_free_list;
};

//Runtime records use native host pointers. Original file-layout checks belong
//to legacy_mail_format.hpp and do not constrain these objects' host layout.

TagMailCounterState* initialize_tag_mail_counter_state(WorldRuntime&, TagMailCounterState*);
TagSystemRecord* initialize_tag_system(WorldRuntime&, TagSystemRecord*);
void insert_tag_mail_message(WorldRuntime&, TagSystemRecord*, MailMessage*, bool append_to_tail);
//Compiled Advisor tags, challenge notifications, and company mail paths.
//Returns false for other event/classes, which retain their existing router.
bool handle_world_tag_system_event(MissionWorld&, TagSystemRecord&, const WorldEvent&);
int mark_tag_messages_by_primary_key_for_deletion(WorldRuntime&, TagSystemRecord*, std::int32_t primary_key);
int mark_matching_mail_for_deletion(WorldRuntime&, TagSystemRecord*, std::int32_t tag_id, std::int32_t format_dword);
int mark_tag_messages_by_format_word_for_deletion(WorldRuntime&, TagSystemRecord*, std::uint16_t format_word);
int mark_tag_messages_by_key_set_and_format_word_for_deletion(WorldRuntime&,
                                                              TagSystemRecord*,
                                                              std::span<const std::int32_t> keys,
                                                              std::uint16_t format_word);
void update_tag_system_lifecycle(WorldRuntime&, TagSystemRecord*);
//Release owned mail/counter storage without map/event unlink or UI callbacks.
//Suitable for an unpublished, partially restored, value-initialized payload.
void release_tag_system_resources(WorldRuntime&, TagSystemRecord*);
void destroy_tag_system(WorldRuntime&, TagSystemRecord*);
int transfer_tag_system_state(WorldRuntime&, TagSystemRecord*, SaveArchive archive);
} //namespace simcoaster
