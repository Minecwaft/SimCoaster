#pragma once
#include "thing_store.hpp"
#include <meta>

namespace simcoaster {
inline std::uint64_t entity_identity_generation(const ThingHeader&);

namespace entity_identity_detail {
template <class Payload>
struct Projection;

template <>
struct Projection<ThingHeader> {
    static constexpr bool accepts(ThingKind) {
        return true;
    }
};

template <>
struct Projection<ParkObjectRuntimeView> {
    static constexpr auto generation = ^^ParkObjectRuntimeView::creation_file_time;

    static constexpr bool accepts(ThingKind kind) {
        return kind == ThingKind::park_object;
    }
};

template <>
struct Projection<PersonRuntimeView> {
    static constexpr auto generation = ^^PersonRuntimeView::arrival_date;

    static constexpr bool accepts(ThingKind kind) {
        return kind == ThingKind::person;
    }
};

template <>
struct Projection<StaffRuntimeRecord> {
    static constexpr auto generation = ^^StaffRuntimeRecord::time_hired;

    static constexpr bool accepts(ThingKind kind) {
        return is_staff_record_kind(kind);
    }
};

template <class Payload>
std::uint64_t generation(const Payload& payload) {
    if constexpr (std::same_as<std::remove_cv_t<Payload>, ThingHeader>) {
        return entity_identity_generation(payload);
    } else {
        constexpr auto member = Projection<std::remove_cv_t<Payload>>::generation;
        return payload.[:member:];
    }
}
} //namespace entity_identity_detail

template <class Payload, bool CheckRecordId = false, class Store>
auto find_live_entity(Store& store, std::int32_t id, const ThingHeader* expected = nullptr) {
    using Result = std::conditional_t<std::is_const_v<Store>, std::add_const_t<Payload>, Payload>;
    auto* header = id > 0 && static_cast<std::size_t>(id) < store.size() ? store[static_cast<std::size_t>(id)].payload : nullptr;
    if (!header || (expected && header != expected) || (CheckRecordId && header->id != id) || header->retirement_pending || !entity_identity_detail::Projection<std::remove_cv_t<Payload>>::accepts(header->kind)) {
        return static_cast<Result*>(nullptr);
    }
    if constexpr (std::same_as<std::remove_cv_t<Payload>, ThingHeader>) {
        return static_cast<Result*>(header);
    } else {
        return entity_assume<Result>(header, header->kind);
    }
}

inline std::uint64_t entity_identity_generation(const ThingHeader& header) {
    match (header.kind) {
        case ThingKind::park_object => return entity_identity_detail::generation(*entity_assume<ParkObjectRuntimeView>(&header, header.kind));
        case ThingKind::person      => return entity_identity_detail::generation(*entity_assume<PersonRuntimeView>(&header, header.kind));
        case _                      => {
            return is_staff_record_kind(header.kind) ? entity_identity_detail::generation(*entity_assume<StaffRuntimeRecord>(&header, header.kind)) : 0;
        }
    }
}

//A slot ID, address and domain generation jointly identify a borrowed entity.
//This value never owns the payload or extends its deferred-removal lifetime.
struct EntityIdentity {
    std::int32_t id {};
    const ThingHeader* identity {};
    std::uint64_t identity_generation {};

    static EntityIdentity capture(const ThingHeader* header) {
        return header ? EntityIdentity { header->id, header, entity_identity_generation(*header) } : EntityIdentity {};
    }

    //Some workflows report retirement and generation mismatch separately.
    template <class Payload, bool CheckRecordId = false, class Store>
    auto live_payload(Store& store) const {
        return identity ? find_live_entity<Payload, CheckRecordId>(store, id, identity) : nullptr;
    }

    template <class Payload, bool CheckRecordId = false, class Store>
    auto get(Store& store) const {
        auto* payload = live_payload<Payload, CheckRecordId>(store);
        return payload && entity_identity_detail::generation(*payload) == identity_generation ? payload : nullptr;
    }
};
} //namespace simcoaster
