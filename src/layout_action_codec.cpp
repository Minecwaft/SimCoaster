#include "simcoaster/save_stream.hpp"
#include "simcoaster/layout_replay.hpp"
#include "simcoaster/save_schema.hpp"
#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace simcoaster {
namespace {
constexpr auto formats = [] {
    std::array<LayoutActionFormat, 232> result{};
    const auto assign = [&](LayoutActionFormat format, std::initializer_list<std::size_t> codes) {
        for (auto code : codes)
            result[code] = format;
    };
    assign(LayoutActionFormat::word, { 0, 10, 25, 30, 35, 50, 61, 62, 66, 67, 68 });
    assign(LayoutActionFormat::modified_coordinates, { 40, 45 });
    assign(LayoutActionFormat::ride, { 5 });
    assign(LayoutActionFormat::float_word, { 57, 58 });
    assign(LayoutActionFormat::two_floats_word, { 59 });
    assign(LayoutActionFormat::word_pair, { 63, 69 });
    assign(LayoutActionFormat::word_triple, { 60 });
    assign(LayoutActionFormat::empty, { 55, 56, 64, 65 });
    assign(LayoutActionFormat::legacy_name, { 70, 75 });
    assign(LayoutActionFormat::mapped_name, { 80, 85 });
    assign(LayoutActionFormat::marker, { 231 });
    return result;
}();

template <class T>
bool read_payload(Stream& stream, T& value) {
    if constexpr (std::same_as<T, LayoutLegacyName> || std::same_as<T, LayoutMappedName>) {
        if constexpr (std::same_as<T, LayoutMappedName>)
            if (!save::read(stream, value.key))
                return false;
        std::uint8_t count{};
        if (!save::read(stream, count))
            return false;
        value.text.resize(count);
        //wchar_t is deliberately absent: the recording contains UTF16 units on
        //every host, without a terminator. The native decoder's64-unit scratch
        //array is replaced with bounded ownership for the full BYTE count.
        std::vector<std::byte> bytes(static_cast<std::size_t>(count) * 2);
        if (stream.read(bytes) != static_cast<std::int32_t>(bytes.size()))
            return false;
        for (std::size_t i = 0; i < count; ++i)
            save::detail::decode<char16_t>(std::span<const std::byte, 2>{ bytes.data() + i * 2, 2 }, value.text[i]);
        return true;
    } else {
        // clang-format off
    template for(constexpr auto member:std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T,std::meta::access_context::current()))) {
      if(!save::read(stream,value.[:member:]))return false;
    }
        // clang-format on
        return true;
    }
}

template <class T>
bool write_payload(Stream& stream, const T& value) {
    if constexpr (std::same_as<T, LayoutLegacyName> || std::same_as<T, LayoutMappedName>) {
        if constexpr (std::same_as<T, LayoutMappedName>)
            if (!save::write(stream, value.key))
                return false;
        if (!save::write(stream, static_cast<std::uint8_t>(value.text.size())))
            return false;
        std::vector<std::byte> bytes;
        bytes.reserve(value.text.size() * 2);
        for (const auto unit : value.text) {
            const auto encoded = save::detail::encode(unit);
            bytes.insert(bytes.end(), encoded.begin(), encoded.end());
        }
        return stream.write(bytes) == static_cast<std::int32_t>(bytes.size());
    } else {
        // clang-format off
    template for(constexpr auto member:std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T,std::meta::access_context::current()))) {
      if(!save::write(stream,value.[:member:]))return false;
    }
        // clang-format on
        return true;
    }
}
} //namespace

namespace {
struct ExtensionHeader {
    std::array<std::byte, 8> signature;
    std::uint16_t version{}, kind{}, length{}, reserved{};
};

constexpr std::array extension_signature{ std::byte{ 'S' }, std::byte{ 'C' }, std::byte{ '6' }, std::byte{ '4' },
                                          std::byte{ 'E' }, std::byte{ 'X' }, std::byte{ 'T' }, std::byte{ 0 } };
} //namespace

LayoutAction make_coaster_insert_action(CoasterInsertReplayCommand command) {
    LayoutActionStream encoded;
    write_payload(encoded, ExtensionHeader{ extension_signature, 1, 1, 6, 0 });
    write_payload(encoded, command);
    LayoutMarker marker;
    std::ranges::copy(encoded.bytes(), marker.bytes.begin());
    return { LayoutActionCode::marker, marker };
}

std::expected<std::optional<CoasterInsertReplayCommand>, LayoutError>
decode_coaster_insert_action(const LayoutAction& action) {
    if (action.code != LayoutActionCode::marker)
        return std::optional<CoasterInsertReplayCommand>{};
    const auto* marker = std::get_if<LayoutMarker>(&action.payload);
    if (!marker)
        return std::unexpected(
            LayoutError{ LayoutFailure::payload_mismatch, 0, action.code, "marker payload missing" });
    if (!std::equal(extension_signature.begin(), extension_signature.end(), marker->bytes.begin()))
        return std::optional<CoasterInsertReplayCommand>{};
    const auto bad = [&](std::string text) {
        return std::unexpected(LayoutError{ LayoutFailure::invalid_contract, 0, action.code, std::move(text) });
    };
    LayoutActionStream encoded;
    encoded.restore(marker->bytes);
    encoded.rewind();
    ExtensionHeader header;
    CoasterInsertReplayCommand command;
    if (!read_payload(encoded, header)
        || header.version != 1
        || header.kind != 1
        || header.length != 6
        || header.reserved)
        return bad("unsupported or malformed SC64EXT header");
    if (!read_payload(encoded, command) || !command.map || command.x >= 128 || command.y >= 128)
        return bad("invalid SC64EXT insertion operands");
    if (std::ranges::any_of(std::span{ marker->bytes }.subspan(encoded.cursor()), [](auto b) {
            return b != std::byte{ 0 };
        }))
        return bad("nonzero SC64EXT padding");
    return std::optional{ command };
}

std::optional<LayoutActionFormat> layout_action_format(std::uint8_t code) {
    return code < formats.size() ? std::optional{ formats[code] } : std::nullopt;
}

bool layout_action_has_native_effect(LayoutActionCode code) {
    match (code) {
        case LayoutActionCode::legacy_name_a or LayoutActionCode::legacy_name_b or LayoutActionCode::marker => return false;
        case LayoutActionCode::set_mode or LayoutActionCode::set_ride or LayoutActionCode::set_rotation or LayoutActionCode::mouse_down or LayoutActionCode::mouse_up or LayoutActionCode::set_mode_nr or LayoutActionCode::set_track or LayoutActionCode::set_queue or LayoutActionCode::modified_mouse_down or LayoutActionCode::modified_mouse_up or LayoutActionCode::set_selected_object or LayoutActionCode::coaster_normal or LayoutActionCode::coaster_special or LayoutActionCode::coaster_loft or LayoutActionCode::coaster_heading or LayoutActionCode::coaster_wobble or LayoutActionCode::coaster_move or LayoutActionCode::coaster_stack_up or LayoutActionCode::coaster_stack_down or LayoutActionCode::coaster_delete_range or LayoutActionCode::coaster_generate_legacy or LayoutActionCode::coaster_backtrack or LayoutActionCode::coaster_edit_legacy or LayoutActionCode::coaster_generate or LayoutActionCode::coaster_edit or LayoutActionCode::coaster_speed or LayoutActionCode::name_a or LayoutActionCode::name_b => {
            return true;
        }
        case _ => return false;
    }
}

std::expected<std::optional<LayoutAction>, LayoutError> read_layout_action(Stream& stream, std::size_t offset) {
    std::uint8_t code{};
    std::array<std::byte, 1> first{};
    const auto count = stream.read(first);
    if (!count)
        return std::optional<LayoutAction>{};
    if (count != 1)
        return std::unexpected(LayoutError{ LayoutFailure::transfer, offset, {}, "cannot read action byte" });
    code = std::to_integer<std::uint8_t>(first[0]);
    const auto format = layout_action_format(code);
    if (!format)
        return std::unexpected(LayoutError{ LayoutFailure::invalid_code,
                                            offset,
                                            static_cast<LayoutActionCode>(code),
                                            "byte indexes outside native232-entry format table" });
    LayoutAction action;
    action.code = static_cast<LayoutActionCode>(code);
    static_assert(std::variant_size_v<LayoutActionPayload> == 12);
    template for (constexpr auto index : std::views::iota(std::size_t{ 0 }, std::variant_size_v<LayoutActionPayload>)) {
        if (static_cast<std::size_t>(*format) == index) {
            using Payload = std::variant_alternative_t<index, LayoutActionPayload>;
            Payload payload{};
            if (!read_payload(stream, payload))
                return std::unexpected(
                    LayoutError{ LayoutFailure::transfer, offset, action.code, "truncated action payload" });
            action.payload = std::move(payload);
            return std::optional{ std::move(action) };
        }
    }
    std::unreachable();
}

// clang-format off
std::expected<void,LayoutError> write_layout_action(Stream& stream,const LayoutAction& action,std::size_t offset) {
  const auto format=layout_action_format(std::to_underlying(action.code));
  if(!format)return std::unexpected(LayoutError{LayoutFailure::invalid_code,offset,action.code,"unknown action code"});
  if(static_cast<std::size_t>(*format)!=action.payload.index())
    return std::unexpected(LayoutError{LayoutFailure::payload_mismatch,offset,action.code,"payload alternative does not match native wire format"});
  const auto name_size=match(action.payload)->std::size_t {
    case {const LayoutLegacyName& value}=>value.text.size();
    case {const LayoutMappedName& value}=>value.text.size();
    case _=>0;
  };
  if(name_size>std::numeric_limits<std::uint8_t>::max())
    return std::unexpected(LayoutError{LayoutFailure::name_too_long,offset,action.code,"UTF16 name exceeds BYTE length"});
  if(!save::write(stream,std::to_underlying(action.code)))
    return std::unexpected(LayoutError{LayoutFailure::transfer,offset,action.code,"cannot write action byte"});
  template for(constexpr auto index:std::views::iota(std::size_t{0},std::variant_size_v<LayoutActionPayload>)) {
    if(action.payload.index()==index) {
      if(!write_payload(stream,std::get<index>(action.payload)))
        return std::unexpected(LayoutError{LayoutFailure::transfer,offset,action.code,"cannot write action payload"});
      return {};
    }
  }
  std::unreachable();
}
// clang-format on

std::string describe_layout_error(const LayoutError& error) {
    auto message = std::string{ "Layout action at byte " } + std::to_string(error.offset);
    if (error.action)
        message += " (event " + std::to_string(std::to_underlying(*error.action)) + ")";
    if (!error.detail.empty())
        message += ": " + error.detail;
    return message;
}

std::int64_t LayoutActionStream::position() const {
    return static_cast<std::int64_t>(cursor_);
}

std::int32_t LayoutActionStream::read(std::span<std::byte> out) {
    const auto count = std::min(
        { out.size(), bytes_.size() - cursor_, static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) });
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(cursor_), count, out.begin());
    cursor_ += count;
    return static_cast<std::int32_t>(count);
}

std::int32_t LayoutActionStream::write(std::span<const std::byte> in) {
    if (in.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
        || in.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - cursor_)
        return -1;
    bytes_.resize(std::max(bytes_.size(), cursor_ + in.size()));
    std::copy(in.begin(), in.end(), bytes_.begin() + static_cast<std::ptrdiff_t>(cursor_));
    cursor_ += in.size();
    return static_cast<std::int32_t>(in.size());
}

void LayoutActionStream::restore(std::span<const std::byte> bytes) {
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("layout recording exceeds DWORD size");
    std::vector<std::byte> copy{ bytes.begin(), bytes.end() };
    bytes_ = std::move(copy);
    append_position();
}

void LayoutActionStream::reset(bool marker) {
    bytes_.clear();
    cursor_ = 0;
    if (marker) {
        bytes_.resize(129);
        bytes_[0] = static_cast<std::byte>(std::to_underlying(LayoutActionCode::marker));
        append_position();
    }
}
} //namespace simcoaster
