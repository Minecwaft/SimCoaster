#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace simcoaster::legacy {
//Original-format record only. Host pointers and host object padding never
//enter this layout; the serialized list link is obsolete input on restoration.
struct MailMessage32 {
    std::int32_t tag_id, dedupe_target_tag_id;
    std::array<char16_t, 1024> rendered_message;
    std::array<char16_t, 255> text_buffer_1, text_buffer_2, text_buffer_3;
    std::uint16_t auxiliary_field, expiry_delay_units, unknown_3590;
    std::uint32_t next_expiry_tick;
    std::uint8_t skip_counter_update, unknown_3597;
    std::uint16_t counter_slot;
    std::int32_t message_code;
    std::uint16_t format_word, unknown_3606;
    std::int32_t format_dword;
    std::uint32_t creation_sequence;
    std::uint32_t obsolete_next_link;
    std::uint32_t packed_date;
    std::uint8_t marked_for_deletion;
    std::array<std::byte, 3> reserved_tail;
};

static_assert(std::endian::native == std::endian::little);
static_assert(std::is_standard_layout_v<MailMessage32> && std::is_trivially_copyable_v<MailMessage32>);
static_assert(sizeof(MailMessage32) == 3628 && alignof(MailMessage32) == 4);
static_assert(offsetof(MailMessage32, rendered_message) == 8);
static_assert(offsetof(MailMessage32, text_buffer_1) == 2056 && offsetof(MailMessage32, text_buffer_2) == 2566);
static_assert(offsetof(MailMessage32, text_buffer_3) == 3076 && offsetof(MailMessage32, expiry_delay_units) == 3588);
static_assert(offsetof(MailMessage32, next_expiry_tick) == 3592 && offsetof(MailMessage32, message_code) == 3600);
static_assert(offsetof(MailMessage32, format_dword) == 3608 && offsetof(MailMessage32, creation_sequence) == 3612);
static_assert(offsetof(MailMessage32, obsolete_next_link) == 3616 && offsetof(MailMessage32, packed_date) == 3620);
static_assert(offsetof(MailMessage32, marked_for_deletion) == 3624);
} //namespace simcoaster::legacy
