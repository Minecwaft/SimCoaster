#pragma once
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <typeinfo>

namespace simcoaster {
enum class ThingKind : std::uint8_t;
struct ThingHeader;
struct MapObjectBase;
struct StaffRuntimeRecord;
struct WorldRuntime;
struct WorldEvent;

struct RecordDescriptor {
    ThingKind kind;
    std::size_t allocation_size;
    std::uint32_t stream_token, original_allocation_size;
    ThingHeader* (*construct)(void* storage);
    const std::type_info* payload_type;
    bool staff_projection;
};

const RecordDescriptor* record_descriptor(ThingKind);
void cleanup_entity_payload(WorldRuntime&, ThingHeader*);

//The header is the first subobject of every registered payload. Type identity
//comes from the same catalog as construction; aliases may share a payload
//type.
template <class T>
bool entity_supports(ThingKind kind) {
    const auto* descriptor = record_descriptor(kind);
    using Payload = std::remove_cv_t<T>;
    return descriptor
        && (std::same_as<Payload, MapObjectBase>
            || (std::same_as<Payload, StaffRuntimeRecord> && descriptor->staff_projection)
            || *descriptor->payload_type == typeid(Payload));
}

template <class T, class Header>
    requires std::same_as<std::remove_const_t<Header>, ThingHeader>
auto entity_cast(Header* header, ThingKind kind) {
    using Payload = std::conditional_t<std::is_const_v<Header>, std::add_const_t<T>, T>;
    return header && entity_supports<T>(kind) ? reinterpret_cast<Payload*>(header) : nullptr;
}

//Use when the algorithm already establishes the kind (dispatch/typed lists).
//Debug checks use catalog identity; release access is a direct projection.
template <class T, class Header>
    requires std::same_as<std::remove_const_t<Header>, ThingHeader>
auto entity_assume(Header* header, [[maybe_unused]] ThingKind allocation_kind) {
    using Payload = std::conditional_t<std::is_const_v<Header>, std::add_const_t<T>, T>;
    assert(!header || entity_supports<T>(allocation_kind));
    return reinterpret_cast<Payload*>(header);
}
} //namespace simcoaster
