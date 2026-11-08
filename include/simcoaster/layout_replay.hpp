#pragma once
#include "world_save.hpp"
#include <algorithm>
#include <array>
#include <expected>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace simcoaster {
//Original action-recorder dictionary, recovered from41D8C0 and the complete
//constructor instruction range41C2A0..41C4B1. These values are persisted IDs.
enum class LayoutActionCode : std::uint8_t {
    set_mode = 0,
    set_ride = 5,
    set_rotation = 10,
    mouse_down = 15,
    mouse_up = 20,
    set_mode_nr = 25,
    set_track = 30,
    set_queue = 35,
    modified_mouse_down = 40,
    modified_mouse_up = 45,
    set_selected_object = 50,
    coaster_normal = 55,
    coaster_special = 56,
    coaster_loft = 57,
    coaster_heading = 58,
    coaster_wobble = 59,
    coaster_move = 60,
    coaster_stack_up = 61,
    coaster_stack_down = 62,
    coaster_delete_range = 63,
    coaster_generate_legacy = 64,
    coaster_backtrack = 65,
    coaster_edit_legacy = 66,
    coaster_generate = 67,
    coaster_edit = 68,
    coaster_speed = 69,
    legacy_name_a = 70,
    legacy_name_b = 75,
    name_a = 80,
    name_b = 85,
    marker = 231
};
enum class LayoutActionFormat : std::uint8_t {
    coordinates = 0,
    word = 1,
    modified_coordinates = 2,
    ride = 3,
    float_word = 4,
    two_floats_word = 5,
    word_pair = 6,
    word_triple = 7,
    empty = 8,
    legacy_name = 9,
    mapped_name = 10,
    marker = 11
};

struct LayoutCoordinates {
    std::uint8_t x {}, y {};
};

struct LayoutWord {
    std::uint16_t value {};
};

struct LayoutModifiedCoordinates {
    std::uint8_t x {}, y {};
    std::uint16_t modifiers {};
};

struct LayoutRide {
    std::uint16_t definition {};
    std::uint8_t secondary {};
};

struct LayoutFloatWord {
    float value {};
    std::uint16_t ordinal {};
};

struct LayoutTwoFloatsWord {
    float first {}, second {};
    std::uint16_t ordinal {};
};

struct LayoutWordPair {
    std::uint16_t first {}, second {};
};

struct LayoutWordTriple {
    std::uint16_t first {}, second {}, third {};
};

struct LayoutLegacyName {
    std::u16string text;
};

struct LayoutMappedName {
    std::uint16_t key {};
    std::u16string text;
};

struct LayoutMarker {
    std::array<std::byte, 128> bytes {};
};

using LayoutActionPayload = std::variant<LayoutCoordinates, LayoutWord, LayoutModifiedCoordinates, LayoutRide, LayoutFloatWord, LayoutTwoFloatsWord, LayoutWordPair, LayoutWordTriple, std::monostate, LayoutLegacyName, LayoutMappedName, LayoutMarker>;

struct LayoutAction {
    LayoutActionCode code {};
    LayoutActionPayload payload;
};

struct CoasterInsertReplayCommand {
    std::uint16_t map {}, ordinal {};
    std::uint8_t x {}, y {};
};
enum class LayoutFailure {
    transfer,
    invalid_code,
    payload_mismatch,
    name_too_long,
    binding,
    missing_world,
    missing_bank,
    invalid_contract,
    runtime_dependency
};

struct LayoutError {
    LayoutFailure reason {};
    std::size_t offset {};
    std::optional<LayoutActionCode> action;
    std::string detail;
};

std::string describe_layout_error(const LayoutError&);
LayoutAction make_coaster_insert_action(CoasterInsertReplayCommand);
std::expected<std::optional<CoasterInsertReplayCommand>, LayoutError> decode_coaster_insert_action(const LayoutAction&);
//Unassigned IDs0..231 retain the constructor's zero-initialized XY format.
//They consume two bytes and are diagnosed/ignored by the original dispatcher.
//IDs232..255 indexed beyond the original table; the host rejects that UB.
std::optional<LayoutActionFormat> layout_action_format(std::uint8_t);
bool layout_action_has_native_effect(LayoutActionCode);
std::expected<std::optional<LayoutAction>, LayoutError> read_layout_action(Stream&, std::size_t offset = 0);
std::expected<void, LayoutError> write_layout_action(Stream&, const LayoutAction&, std::size_t offset = 0);

//Real owner behind WorldRuntime::action_record_stream, including legacy writers
//that emit the opcode and payload through separate Stream::write calls.
class LayoutActionStream final : public PositionedStream {
    std::vector<std::byte> bytes_;
    std::size_t cursor_ {};

public:
    std::int64_t position() const override;
    std::int32_t read(std::span<std::byte>) override;
    std::int32_t write(std::span<const std::byte>) override;

    std::span<const std::byte> bytes() const {
        return bytes_;
    }

    std::size_t cursor() const {
        return cursor_;
    }

    void rewind() {
        cursor_ = 0;
    }

    void append_position() {
        cursor_ = bytes_.size();
    }

    //Roll back an appended complete packet without allocating during unwinding.
    void truncate(std::size_t size) noexcept {
        if (size < bytes_.size()) {
            bytes_.resize(size);
        }
        cursor_ = std::min(cursor_, bytes_.size());
    }

    void restore(std::span<const std::byte>);
    void reset(bool seed_marker);
};
enum class LayoutReplayTiming : std::uint32_t {
    immediate = 0,
    short_delay = 1,
    recorded_delay = 2
};
enum class LayoutReplayState {
    idle,
    replaying,
    failed
};

struct LayoutReplayProgress {
    LayoutReplayState state {};
    std::size_t offset {}, size {}, actions_dispatched {};
    std::uint32_t ticks_until_next_action {}, loaded_published_park {};
    std::optional<LayoutError> error;
};

//Dispatch is mandatory and operates on the live World. No fake object graph
//or success-by-default implementation is supplied. The complete native
//decoder knows every payload before this boundary can mutate game state.
struct LayoutReplayBackend {
    virtual ~LayoutReplayBackend() = default;
    virtual void clear_interaction_status(WorldRuntime&) = 0;
    virtual void dispatch(WorldRuntime&, const LayoutAction&) = 0;
    virtual void reset_train_rsse(WorldRuntime&) = 0;
    virtual void cancel_replay(WorldRuntime&) = 0;
};

class LayoutReplayRuntime {
    LayoutActionStream stream_;
    LayoutReplayState state_ {};
    LayoutReplayTiming timing_ {};
    std::uint32_t countdown_ {}, loaded_published_ {};
    std::size_t dispatched_ {};
    std::optional<LayoutError> error_;
    WorldRuntime* bound_ {};
    LayoutReplayBackend* backend_ {};
    std::expected<void, LayoutError> finish(WorldRuntime&, LayoutReplayBackend&);
    std::expected<bool, LayoutError> step(WorldRuntime&, LayoutReplayBackend&);
    std::expected<void, LayoutError> fail(WorldRuntime&, LayoutError);

public:
    LayoutReplayRuntime();
    LayoutReplayRuntime(const LayoutReplayRuntime&) = delete;
    LayoutReplayRuntime& operator=(const LayoutReplayRuntime&) = delete;
    //Binding never steals a different recorder; detach before WorldRuntime dies.
    std::expected<void, LayoutError> bind(WorldRuntime&);
    void detach(WorldRuntime&) noexcept;
    std::expected<void, LayoutError> restore(WorldRuntime&, std::uint32_t published, std::span<const std::byte>);
    //A fresh host session starts ready (countdown0), including after a prior
    //timed EOF wrap. Calling begin again while active is idempotent only for
    //the same backend; the native tick/EOF arithmetic is unchanged.
    std::expected<void, LayoutError> begin(WorldRuntime&, LayoutReplayBackend&, LayoutReplayTiming = LayoutReplayTiming::immediate);
    std::expected<void, LayoutError> tick(WorldRuntime&, LayoutReplayBackend&);
    //Native has no dedicated cancel method. Host cancellation explicitly
    //releases replay suppression/withdrawal ownership without claiming replay
    //completion or running its eight script updates.
    std::expected<void, LayoutError> cancel(WorldRuntime&);
    std::expected<void, LayoutError> reset(WorldRuntime&);
    std::expected<void, LayoutError> record(WorldRuntime&, const LayoutAction&);
    //Native serialization leaves the recording cursor at END, even during
    //replay. The next active tick consequently completes that replay.
    std::expected<void, LayoutError> write_save_prefix(Stream&);

    LayoutActionStream& stream() {
        return stream_;
    }

    const LayoutActionStream& stream() const {
        return stream_;
    }

    LayoutReplayProgress progress() const;
};
} //namespace simcoaster
