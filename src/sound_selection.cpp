#include "simcoaster/sound_library.hpp"
#include <bit>

namespace simcoaster {
namespace {
const SoundSelection* row(const SoundDescriptor& descriptor, const SoundSelectionCursor& cursor) {
    return cursor.selection && *cursor.selection < descriptor.selections.size() ? &descriptor.selections[*cursor.selection] : nullptr;
}

std::unexpected<SoundError> invalid(std::size_t index) {
    return std::unexpected(SoundError { SoundFailure::selection, index, {} });
}

//The native repeat path narrows the increment through signed char before
//unsigned modulo and then narrows its result once more for addressing. Keep
//the integer result; reject an out-of-range signed index instead of making an
//out-of-bounds pointer. Shipped row/sample counts are inspected separately.
std::optional<std::size_t> repeat_index(std::size_t index, std::size_t count) {
    const auto increment = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(index + 1));
    const auto remainder = static_cast<std::uint32_t>(static_cast<std::int32_t>(increment)) % static_cast<std::uint32_t>(count);
    const auto narrowed = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(remainder));
    if (narrowed < 0 || static_cast<std::size_t>(narrowed) >= count) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(narrowed);
}

std::uint32_t random_offset(std::uint32_t& state, std::uint32_t span) {
    return span ? next_sound_selection_random(state) % span : 0;
}
} //namespace

std::uint32_t next_sound_selection_random(std::uint32_t& state) {
    state = 1664525u * state + 1013904223u;
    return std::rotr(state, 13);
}

void refresh_sound_selection_keys(const SoundDescriptor& descriptor, SoundSelectionCursor& cursor) {
    const auto* selection = row(descriptor, cursor);
    if (!selection) {
        return;
    }
    if (selection->parameters.first_key || selection->parameters.second_key) {
        cursor.payload_present = true;
    }
    cursor.keys[1] = static_cast<std::uint8_t>(selection->parameters.first_key);
    cursor.keys[2] = static_cast<std::uint8_t>(selection->parameters.second_key);
    cursor.keys[3] = 0;
}

std::expected<bool, SoundError> select_sound_record(const SoundDescriptor& descriptor, SoundSelectionCursor& cursor, std::uint32_t& random, bool avoid_repeat) {
    if (descriptor.selections.empty()) {
        cursor.selection.reset();
        return false;
    }
    if (descriptor.selections.size() == 1) {
        cursor.selection = 0;
        return true;
    }
    const auto draw = next_sound_selection_random(random) >> 16;
    std::uint32_t total {};
    for (std::size_t i = 0; i < descriptor.selections.size(); ++i) {
        total += descriptor.selections[i].parameters.weight;
        if (total < draw) {
            continue;
        }
        auto index = i;
        if (avoid_repeat && descriptor.selections.size() > 2 && static_cast<std::uint8_t>(i) == cursor.previous_selection) {
            const auto next = repeat_index(i, descriptor.selections.size());
            if (!next) {
                return invalid(i);
            }
            index = *next;
        }
        cursor.selection = index;
        cursor.previous_selection = static_cast<std::uint8_t>(index);
        refresh_sound_selection_keys(descriptor, cursor);
        return true;
    }
    cursor.selection = 0;
    return true; //Native weighted-row fallback.
}

std::expected<bool, SoundError> select_sound_sample(const SoundDescriptor& descriptor, SoundSelectionCursor& cursor, std::uint32_t& random, bool avoid_repeat) {
    const auto* selection = row(descriptor, cursor);
    cursor.sample.reset();
    if (!selection) {
        return false;
    }
    const auto count = std::min<std::size_t>(selection->parameters.sample_count, selection->samples.size());
    if (count == 1) {
        cursor.sample = 0;
        return true;
    }
    if (!count) {
        return false;
    }
    const auto draw = next_sound_selection_random(random) >> 16;
    for (std::size_t i = 0; i < count; ++i) {
        if (selection->samples[i].cumulative_weight < draw) {
            continue;
        }
        auto index = i;
        if (avoid_repeat && count > 2 && static_cast<std::uint8_t>(i) == cursor.previous_sample) {
            const auto next = repeat_index(i, count);
            if (!next) {
                return invalid(i);
            }
            index = *next;
        }
        cursor.sample = index;
        cursor.previous_sample = static_cast<std::uint8_t>(index);
        return true;
    }
    return false; //Exhausted sample thresholds do not select the first sample.
}

std::expected<bool, SoundError> select_sound_conditional_record(const SoundDescriptor& descriptor, SoundSelectionCursor& cursor, std::uint32_t& random, std::uint32_t selector) {
    if (!cursor.selection) {
        if (descriptor.selections.empty()) {
            return false;
        }
        cursor.selection = 0;
        if (descriptor.selections[0].parameters.sample_count) {
            return true;
        }
    }
    const auto* selection = row(descriptor, cursor);
    if (!selection) {
        return invalid(cursor.selection.value_or(0));
    }
    if (selection->branches.empty()) {
        cursor.selection.reset();
        return false;
    }
    std::uint32_t weight {};
    std::size_t matching {};
    for (const auto& branch : selection->branches) {
        if (!branch.selection || branch.selection > descriptor.selections.size()) {
            return invalid(branch.selection);
        }
        if (branch.minimum <= selector && branch.maximum >= selector) {
            ++matching;
            weight += descriptor.selections[branch.selection - 1].parameters.weight;
        }
    }
    if (!matching) {
        cursor.values[0] = 127;
        return false;
    }
    if (!weight) {
        return invalid(*cursor.selection); //Native division-by-zero hazard.
    }
    const auto draw = next_sound_selection_random(random) % weight;
    std::uint32_t cumulative {};
    for (const auto& branch : selection->branches) {
        if (branch.minimum <= selector && branch.maximum >= selector) {
            cumulative += descriptor.selections[branch.selection - 1].parameters.weight;
            if (cumulative >= draw) {
                cursor.selection = branch.selection - 1;
                return true;
            }
        }
    }
    return true;
}

bool advance_sound_sequential_record(const SoundDescriptor& descriptor, SoundSelectionCursor& cursor, bool repeat) {
    if (descriptor.selections.empty()) {
        cursor.selection.reset();
        return false;
    }
    if (!cursor.selection) {
        cursor.selection = 0;
        return true;
    }
    ++*cursor.selection;
    if (*cursor.selection >= descriptor.selections.size()) {
        if (!repeat) {
            return false;
        }
        cursor.selection = 0;
    }
    return true;
}

std::optional<std::uint32_t> query_sound_selection_value(const SoundDescriptor& descriptor, const SoundSelectionCursor& cursor, std::uint32_t mask) {
    const auto* selection = row(descriptor, cursor);
    if (!selection || !cursor.payload_present) {
        return std::nullopt;
    }
    if (selection->parameters.first_mask & static_cast<std::uint16_t>(mask)) {
        return cursor.values[1];
    }
    if (selection->parameters.second_mask & static_cast<std::uint16_t>(mask)) {
        return cursor.values[2];
    }
    return std::nullopt;
}

std::uint32_t choose_sound_level(const SoundDescriptor& descriptor, SoundSelectionCursor& cursor, std::uint32_t& random) {
    const auto* selection = row(descriptor, cursor);
    if (!selection) {
        return 100;
    }
    const auto lo = std::min(selection->parameters.minimum_level, selection->parameters.maximum_level);
    const auto hi = std::max(selection->parameters.minimum_level, selection->parameters.maximum_level);
    selection->parameters.minimum_level = lo;
    selection->parameters.maximum_level = hi;
    const auto span = static_cast<std::uint32_t>(hi - lo);
    if (const auto value = query_sound_selection_value(descriptor, cursor, 1)) {
        return lo + span * *value / 100u;
    }
    return lo + random_offset(random, span);
}

std::int32_t choose_sound_frequency_parameter(const SoundDescriptor& descriptor, SoundSelectionCursor& cursor, std::uint32_t& random) {
    const auto* selection = row(descriptor, cursor);
    if (!selection) {
        return 0;
    }
    const auto lo = std::min(selection->parameters.minimum_frequency, selection->parameters.maximum_frequency);
    const auto hi = std::max(selection->parameters.minimum_frequency, selection->parameters.maximum_frequency);
    selection->parameters.minimum_frequency = lo;
    selection->parameters.maximum_frequency = hi;
    const auto span = static_cast<std::uint32_t>(hi - lo);
    const auto offset = query_sound_selection_value(descriptor, cursor, 2);
    const auto value = offset ? span * *offset / 100u : random_offset(random, span);
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(lo)) + value);
}

std::uint32_t choose_sound_interval(const SoundDescriptor& descriptor, SoundSelectionCursor& cursor, std::uint32_t& random) {
    const auto* selection = row(descriptor, cursor);
    if (!selection) {
        return 0;
    }
    const auto minimum = selection->parameters.minimum_interval, maximum = selection->parameters.maximum_interval;
    if (!minimum && !maximum) {
        return 0;
    }
    //This keyed path deliberately keeps the endpoint order and signed DWORD
    //division; only the unkeyed path sorts endpoints before drawing.
    if (const auto keyed = query_sound_selection_value(descriptor, cursor, 4)) {
        const auto span = static_cast<std::uint32_t>(static_cast<std::int32_t>(maximum) - minimum);
        const auto product = std::bit_cast<std::int32_t>(*keyed * span);
        return static_cast<std::uint32_t>(product / 100) + minimum;
    }
    const auto lo = std::min(minimum, maximum), hi = std::max(minimum, maximum);
    selection->parameters.minimum_interval = lo;
    selection->parameters.maximum_interval = hi;
    return lo + random_offset(random, static_cast<std::uint32_t>(hi - lo));
}

std::optional<std::size_t> select_sound_playlist_record(const SoundDescriptor& descriptor, SoundPlaylistCursor& cursor, bool repeat) {
    const auto count = descriptor.selections.size();
    if (!count) {
        return std::nullopt;
    }
    const auto reset = [&] {
        cursor.next = 0;
        const auto selected = static_cast<std::size_t>(next_sound_selection_random(cursor.random)) % count;
        auto left = selected, right = selected;
        //Native starts left at selected-1. selected0 immediately skips the loop.
        while (left && right < count - 1) {
            --left;
            std::swap(descriptor.selections[left], descriptor.selections[right]);
            cursor.alternating_step = !cursor.alternating_step;
            right += cursor.alternating_step ? 2u : 1u;
        }
    };
    if (cursor.descriptor != &descriptor) {
        cursor.descriptor = &descriptor;
        reset();
    }
    if (cursor.next >= count) {
        if (!repeat) {
            return std::nullopt;
        }
        reset();
    }
    return cursor.next++;
}
} //namespace simcoaster
