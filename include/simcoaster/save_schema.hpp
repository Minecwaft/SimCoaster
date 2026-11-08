#pragma once
#include "schema_metadata.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <functional>
#include <meta>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace simcoaster::save {
//Persistent labels and order come from the recovered format. Reflection
//discovers annotated members; it never assigns persistent identities.
struct Field {
    const char* label;
    std::size_t order;

    consteval Field(std::string_view text, std::size_t position)
        : label(std::define_static_string(text)),
          order(position) {}
};

enum class Direction {
    read,
    write
};

//Labeled fragments can still drive a custom codec, but cannot be mistaken for
//its complete wire image when computed/interleaved/dynamic fields also exist.
struct ManualCodec {};

template <class T>
concept Scalar = !std::is_const_v<T>
              && !std::is_volatile_v<T>
              && !std::same_as<T, bool>
              && (std::is_integral_v<T> || std::is_enum_v<T> || std::same_as<T, float> || std::same_as<T, double>)
              && (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

template <class T>
struct ValueTraits;

template <class T>
inline constexpr bool supported_value = ValueTraits<T>::supported;
template <class T>
inline constexpr std::size_t value_width = ValueTraits<T>::width;

namespace detail {
struct Member {
    std::meta::info member;
    Field field;
};

template <class T>
consteval auto plan() {
    std::vector<Member> result;
    for (auto member : std::define_static_array(schema::members_of(^^T, std::meta::access_context::current()))) {
        const auto annotations = std::meta::annotations_of_with_type(member, ^^Field);
        if (annotations.empty())
            continue;
        if (annotations.size() != 1 || std::meta::is_bit_field(member))
            throw "save member needs one Field annotation and addressable storage";
        const auto field =
            schema::annotation<Field>(member, "save member needs one Field annotation and addressable storage");
        if (std::string_view{ field.label }.empty())
            throw "save field label must not be empty";
        if (!std::meta::extract<bool>(std::meta::substitute(^^supported_value, { std::meta::type_of(member) })))
            throw "save field needs a fixed-width scalar, nonempty array or " "explicitly annotated aggregate; runtime pointers are not wire " "values";
        result.push_back({ member, field });
    }
    std::ranges::sort(result, {}, [](const Member& member) {
        return member.field.order;
    });
    for (std::size_t i = 1; i < result.size(); ++i)
        if (result[i - 1].field.order == result[i].field.order)
            throw "save field order must be unique";
    return result;
}

template <class T>
inline constexpr auto members = std::define_static_array(plan<T>());

template <class T>
consteval bool annotated_aggregate() {
    if constexpr (std::is_class_v<T> && std::is_aggregate_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>)
        return std::meta::annotations_of_with_type(^^T, ^^ManualCodec).empty() && !members<T>.empty();
    else
        return false;
}

template <class T>
consteval std::size_t aggregate_width() {
    std::size_t total = 0;
    for (auto entry : members<T>)
        total +=
            std::meta::extract<std::size_t>(std::meta::substitute(^^value_width, { std::meta::type_of(entry.member) }));
    return total;
}
} //namespace detail

template <class T>
struct ValueTraits {
    static constexpr bool is_array = false;
    static constexpr bool supported = Scalar<T> || detail::annotated_aggregate<T>();
    static constexpr std::size_t width = [] consteval {
        if constexpr (Scalar<T>)
            return sizeof(T);
        else if constexpr (supported)
            return detail::aggregate_width<T>();
        else
            return std::size_t{ 0 };
    }();
};

template <class T, std::size_t N>
struct ValueTraits<std::array<T, N>> {
    static constexpr bool is_array = true;
    static constexpr bool supported = N != 0 && supported_value<T>;
    static constexpr std::size_t width = [] consteval {
        if constexpr (supported)
            return value_width<T> * N;
        else
            return std::size_t{ 0 };
    }();
};

namespace detail {
template <Scalar T>
constexpr void encode_scalar(T value, std::span<std::byte, sizeof(T)> output) {
    auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    if constexpr (std::endian::native == std::endian::big)
        std::ranges::reverse(bytes);
    std::ranges::copy(bytes, output.begin());
}

template <Scalar T>
constexpr void decode_scalar(std::span<const std::byte, sizeof(T)> input, T& value) {
    std::array<std::byte, sizeof(T)> bytes;
    std::ranges::copy(input, bytes.begin());
    if constexpr (std::endian::native == std::endian::big)
        std::ranges::reverse(bytes);
    value = std::bit_cast<T>(bytes);
}

template <class T>
    requires supported_value<T>
constexpr auto encode(const T& value) {
    std::array<std::byte, value_width<T>> bytes;
    if constexpr (Scalar<T>)
        encode_scalar(value, std::span<std::byte, sizeof(T)>{ bytes });
    else if constexpr (ValueTraits<T>::is_array) {
        using Element = typename T::value_type;
        for (std::size_t i = 0; i < value.size(); ++i) {
            const auto element = encode(value[i]);
            std::ranges::copy(element, bytes.begin() + i * value_width<Element>);
        }
    } else {
        std::size_t offset = 0;
        template for (constexpr auto entry : members<T>) {
            const auto member = encode(schema::member<entry.member>(value));
            std::ranges::copy(member, bytes.begin() + offset);
            offset += member.size();
        }
    }
    return bytes;
}

template <class T>
    requires supported_value<T>
constexpr void decode(std::span<const std::byte, value_width<T>> bytes, T& value) {
    if constexpr (Scalar<T>)
        decode_scalar(bytes, value);
    else if constexpr (ValueTraits<T>::is_array) {
        using Element = typename T::value_type;
        for (std::size_t i = 0; i < value.size(); ++i)
            decode<Element>(std::span<const std::byte, value_width<Element>>{ bytes.data() + i * value_width<Element>,
                                                                              value_width<Element> },
                            value[i]);
    } else {
        std::size_t offset = 0;
        template for (constexpr auto entry : members<T>) {
            auto& member = schema::member<entry.member>(value);
            using MemberType = std::remove_reference_t<decltype(member)>;
            constexpr auto width = value_width<MemberType>;
            decode<MemberType>(std::span<const std::byte, width>{ bytes.data() + offset, width }, member);
            offset += width;
        }
    }
}
} //namespace detail

template <class T>
inline constexpr std::size_t field_count = detail::members<T>.size();
template <class T>
inline constexpr std::size_t record_width = detail::aggregate_width<T>();

//Visitor contract: bool(std::string_view original_label, auto& typed_member).
//A nested aggregate remains one outer field/transfer. Its annotated members
//determine its bytes; their labels are used only when explicitly transferring
//that aggregate's fields. No runtime layout or padding enters the wire image.
template <std::size_t First = 0, std::size_t Last = static_cast<std::size_t>(-1), class T, class Visitor>
bool transfer_fields(T& value, Visitor&& visitor) {
    template for (constexpr auto entry : detail::members<T>) {
        constexpr auto field = entry.field;
        if constexpr (field.order >= First && field.order < Last)
            if (!std::invoke(visitor, std::string_view{ field.label }, schema::member<entry.member>(value)))
                return false;
    }
    return true;
}

//Byte transfer contract: bool(std::span<std::byte>). A write callback only
//reads the span. A read callback may replace a prefix before returning false;
//decode that prefix too, preserving the original format's partial-read effect.
template <class T, class Transfer>
    requires supported_value<T>
bool transfer_value(T& value, Direction direction, Transfer&& transfer) {
    auto bytes = detail::encode(value);
    return match (direction) -> bool {
        case Direction::write => std::invoke(transfer, std::span<std::byte>{ bytes });
        case Direction::read  => do {
            const bool complete = std::invoke(transfer, std::span<std::byte>{ bytes });
            detail::decode<T>(std::span<const std::byte, value_width<T>>{ bytes }, value);
            do_return complete;
        };
    };
}
} //namespace simcoaster::save
