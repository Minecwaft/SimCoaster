#pragma once
#include "world_save.hpp"
#include "world_objects.hpp"

namespace simcoaster {
template <class T>
bool SaveArchive::field(T& value, std::string_view label) const {
    services.field_header(static_cast<std::uint32_t>(save::value_width<T>), label, writing);
    return save::transfer_value(value, writing ? save::Direction::write : save::Direction::read, [&](std::span<std::byte> bytes) {
        const auto count = writing ? stream.write(bytes) : stream.read(bytes);
        return count == static_cast<std::int32_t>(bytes.size());
    });
}

template <std::size_t First, std::size_t Last, class T>
bool SaveArchive::fields(T& value) const {
    return save::transfer_fields<First, Last>(value, [&](std::string_view label, auto& member) {
        return field(member, label);
    });
}

inline bool SaveArchive::meter(float& value, std::string_view label) const {
    //Compute the native byte-quantized temporary before the header boundary.
    //A short read changes the temporary only; the actual meter stays intact.
    auto wire = writing ? static_cast<float>(truncate_park_object_condition(value)) : 0.0f;
    if (!field(wire, label)) {
        return false;
    }
    if (!writing) {
        value = wire;
    }
    return true;
}

template <class Ring>
int SaveArchive::history_ring(Ring& ring) const {
    if (schema > 1) {
        return 0;
    }
    if (!fields(ring)) {
        return 1;
    }
    if (ring.num_entries > 0) {
        std::uint32_t index = 0;
        do {
            //Check the live count after every sample. Reject an actual invalid visit
            //rather than clamping or discarding earlier stream/state side effects.
            if (!field(ring.data.at(index), "mData[i]")) {
                return 1;
            }
            ++index;
            if (static_cast<std::int32_t>(index) >= ring.num_entries) {
                break;
            }
        } while (true);
    }
    return 0;
}
} //namespace simcoaster
