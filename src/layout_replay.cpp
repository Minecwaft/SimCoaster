#include "simcoaster/save_stream.hpp"
#include "simcoaster/layout_replay.hpp"
#include "simcoaster/rsse_runtime.hpp"
#include "simcoaster/save_schema.hpp"
#include <limits>
#include <utility>

namespace simcoaster {
namespace {
std::expected<BankRecord*, LayoutError> replay_bank(WorldRuntime& c, std::size_t offset) {
    if (!c.simulation.current) {
        return std::unexpected(LayoutError { LayoutFailure::missing_world, offset, {}, "live World is not bound" });
    }
    const auto id = c.simulation.current->saved.bank_account;
    auto* header = id ? c.entities.at(id).payload : nullptr;
    if (!header || header->kind != ThingKind::bank_account) {
        return std::unexpected(LayoutError { LayoutFailure::missing_bank, offset, {}, "World bank account is missing" });
    }
    return reinterpret_cast<BankRecord*>(header);
}
} //namespace

LayoutReplayRuntime::LayoutReplayRuntime() {
    stream_.reset(true);
}

std::expected<void, LayoutError> LayoutReplayRuntime::bind(WorldRuntime& c) {
    if ((bound_ && bound_ != &c) || (c.construction.action_record_stream && c.construction.action_record_stream != &stream_)) {
        return std::unexpected(LayoutError { LayoutFailure::binding, stream_.cursor(), {}, "another recording owner is already bound" });
    }
    if (!bound_) {
        if (c.construction.action_recording_disabled) {
            return std::unexpected(LayoutError { LayoutFailure::binding, stream_.cursor(), {}, "cannot adopt externally suppressed action state" });
        }
        bound_ = &c;
        c.construction.action_record_stream = &stream_;
        c.construction.action_record_streams_initialized = 1;
    }
    return {};
}

void LayoutReplayRuntime::detach(WorldRuntime& c) noexcept {
    if (bound_ != &c) {
        return;
    }
    //The production owner cancels before teardown. Detach itself never visits
    //World payloads, because WorldRuntime/World destruction may already have begun.
    if (c.construction.action_record_stream == &stream_) {
        c.construction.action_record_stream = nullptr;
        c.construction.action_record_streams_initialized = 0;
        c.construction.action_recording_disabled = 0;
    }
    bound_ = nullptr;
    backend_ = nullptr;
}

std::expected<void, LayoutError> LayoutReplayRuntime::restore(WorldRuntime& c, std::uint32_t published, std::span<const std::byte> bytes) {
    if (state_ == LayoutReplayState::replaying) {
        return std::unexpected(LayoutError { LayoutFailure::binding, stream_.cursor(), {}, "cancel replay before restoring a different recording" });
    }
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(LayoutError { LayoutFailure::invalid_contract, 0, {}, "recording does not fit native DWORD save size" });
    }
    if (auto result = bind(c); !result) {
        return result;
    }
    stream_.restore(bytes);
    loaded_published_ = published;
    countdown_ = 0;
    dispatched_ = 0;
    error_.reset();
    state_ = LayoutReplayState::idle;
    c.construction.action_recording_disabled = 0;
    return {};
}

std::expected<void, LayoutError> LayoutReplayRuntime::fail(WorldRuntime& c, LayoutError error) {
    c.construction.action_recording_disabled = 1;
    if (auto* backend = std::exchange(backend_, nullptr)) {
        try {
            backend->cancel_replay(c);
        } catch (const std::exception& cleanup) {
            error.detail += "; replay cleanup: ";
            error.detail += cleanup.what();
        } catch (...) {
            error.detail += "; replay cleanup failed";
        }
    }
    state_ = LayoutReplayState::failed;
    error_ = std::move(error);
    c.construction.action_recording_disabled = 0;
    //A decoded-contract/backend failure is not a successful layout replay.
    //Release the economic suppression owned by begin without asserting that
    //the park was published or applying the successful completion side effects.
    if (auto bank = replay_bank(c, stream_.cursor()); bank) {
        (*bank)->withdrawals_enabled = 1;
    }
    stream_.append_position();
    return std::unexpected(*error_);
}

std::expected<void, LayoutError> LayoutReplayRuntime::finish(WorldRuntime& c, LayoutReplayBackend& backend) {
    //Original41DB29/41DC29 ordering: recording resumes before scripts, then
    //eight immediate script passes, train request reset, withdrawals enabled.
    stream_.append_position();
    state_ = LayoutReplayState::idle;
    c.construction.action_recording_disabled = 0;
    loaded_published_ = 1;
    auto bank = replay_bank(c, stream_.cursor());
    if (!bank) {
        return fail(c, bank.error());
    }
    try {
        for (unsigned pass = 0; pass < 8; ++pass) {
            update_rsse_scripts(c);
        }
        backend.reset_train_rsse(c);
        (*bank)->withdrawals_enabled = 1;
        backend_ = nullptr;
        return {};
    } catch (const std::exception& error) {
        return fail(c, { LayoutFailure::runtime_dependency, stream_.cursor(), {}, error.what() });
    } catch (...) {
        return fail(c, { LayoutFailure::runtime_dependency, stream_.cursor(), {}, "unclassified replay completion failure" });
    }
}

std::expected<bool, LayoutError> LayoutReplayRuntime::step(WorldRuntime& c, LayoutReplayBackend& backend) {
    const auto start = stream_.cursor();
    auto action = read_layout_action(stream_, start);
    if (!action) {
        return std::unexpected(action.error());
    }
    if (!*action) {
        return false;
    }
    //This exactly preserves legacy ignored-name and reserved-code behavior,
    //without asking a production backend to invent effects for those records.
    const auto extension = decode_coaster_insert_action(**action);
    if (!extension) {
        auto error = extension.error();
        error.offset = start;
        return std::unexpected(std::move(error));
    }
    if (layout_action_has_native_effect((**action).code) || extension->has_value()) {
        try {
            backend.dispatch(c, **action);
        } catch (const LayoutError& error) {
            auto located = error;
            located.offset = start;
            located.action = (**action).code;
            return std::unexpected(std::move(located));
        } catch (const std::exception& error) {
            return std::unexpected(LayoutError { LayoutFailure::runtime_dependency, start, (**action).code, error.what() });
        } catch (...) {
            return std::unexpected(LayoutError { LayoutFailure::runtime_dependency, start, (**action).code, "backend raised an unclassified native runtime failure" });
        }
    }
    ++dispatched_;
    return true;
}

std::expected<void, LayoutError> LayoutReplayRuntime::begin(WorldRuntime& c, LayoutReplayBackend& backend, LayoutReplayTiming timing) {
    if (auto result = bind(c); !result) {
        return result;
    }
    if (state_ == LayoutReplayState::replaying) {
        if (backend_ != &backend) {
            return std::unexpected(LayoutError { LayoutFailure::binding, stream_.cursor(), {}, "active replay belongs to a different backend" });
        }
        return {};
    }
    if (timing != LayoutReplayTiming::immediate && timing != LayoutReplayTiming::short_delay && timing != LayoutReplayTiming::recorded_delay) {
        return std::unexpected(LayoutError { LayoutFailure::invalid_contract, stream_.cursor(), {}, "invalid native replay timing mode" });
    }
    auto bank = replay_bank(c, stream_.cursor());
    if (!bank) {
        return std::unexpected(bank.error());
    }
    state_ = LayoutReplayState::replaying;
    timing_ = timing;
    //41D800 does not initialize its countdown; its sole native caller uses
    //immediate mode. This public owner also supports repeated timed sessions.
    //Start each new session ready, while retaining the exact per-tick EOF
    //decrement/wrap rule and its observable final countdown in progress().
    countdown_ = 0;
    dispatched_ = 0;
    error_.reset();
    backend_ = &backend;
    c.construction.action_recording_disabled = 1;
    (*bank)->withdrawals_enabled = 0;
    stream_.rewind();
    return tick(c, backend);
}

std::expected<void, LayoutError> LayoutReplayRuntime::tick(WorldRuntime& c, LayoutReplayBackend& backend) {
    if (bound_ != &c) {
        return std::unexpected(LayoutError { LayoutFailure::binding, stream_.cursor(), {}, "recording owner is not bound to this Context" });
    }
    if (state_ != LayoutReplayState::replaying) {
        if (c.construction.action_recording_disabled) {
            return std::unexpected(LayoutError { LayoutFailure::binding, stream_.cursor(), {}, "replay flag was set outside its runtime owner" });
        }
        return {};
    }
    if (backend_ != &backend) {
        return fail(c, { LayoutFailure::binding, stream_.cursor(), {}, "replay backend differs from its begin owner" });
    }
    if (!c.construction.action_recording_disabled) {
        return fail(c, { LayoutFailure::binding, stream_.cursor(), {}, "replay suppression was changed externally" });
    }
    try {
        backend.clear_interaction_status(c);
    } catch (const std::exception& error) {
        return fail(c, { LayoutFailure::runtime_dependency, stream_.cursor(), {}, error.what() });
    }
    bool processed = false;
    if (timing_ == LayoutReplayTiming::immediate || countdown_ == 0) {
        processed = true;
        while (true) {
            auto more = step(c, backend);
            if (!more) {
                return fail(c, more.error());
            }
            if (!*more) {
                if (auto result = finish(c, backend); !result) {
                    return result;
                }
                break;
            }
            if (timing_ != LayoutReplayTiming::immediate && countdown_ != 0) {
                break;
            }
        }
    }
    if (timing_ != LayoutReplayTiming::immediate) {
        //Deliberate native DWORD wrap, including after successful EOF. Current
        //active action formats have no delay-setting opcode; the two secondary
        //memory streams are unused by writers/dispatcher in this executable.
        --countdown_;
        if (timing_ == LayoutReplayTiming::short_delay && processed && countdown_ > 2) {
            countdown_ = 2;
        }
    }
    return {};
}

std::expected<void, LayoutError> LayoutReplayRuntime::cancel(WorldRuntime& c) {
    if (auto result = bind(c); !result) {
        return result;
    }
    if (state_ == LayoutReplayState::replaying) {
        auto bank = replay_bank(c, stream_.cursor());
        if (!bank) {
            return std::unexpected(bank.error());
        }
        if (auto* backend = std::exchange(backend_, nullptr)) {
            try {
                backend->cancel_replay(c);
            } catch (const std::exception& error) {
                return fail(c, { LayoutFailure::runtime_dependency, stream_.cursor(), {}, error.what() });
            } catch (...) {
                return fail(c, { LayoutFailure::runtime_dependency, stream_.cursor(), {}, "replay cancellation failed" });
            }
        }
        (*bank)->withdrawals_enabled = 1;
        c.construction.action_recording_disabled = 0;
    }
    state_ = LayoutReplayState::idle;
    countdown_ = 0;
    error_.reset();
    stream_.append_position();
    return {};
}

std::expected<void, LayoutError> LayoutReplayRuntime::reset(WorldRuntime& c) {
    if (auto result = cancel(c); !result) {
        return result;
    }
    stream_.reset(!c.construction.action_recording_disabled && c.construction.action_record_streams_initialized);
    dispatched_ = 0;
    //41CBB0 resets stream contents, not the loaded-published flag.
    return {};
}

std::expected<void, LayoutError> LayoutReplayRuntime::record(WorldRuntime& c, const LayoutAction& action) {
    if (auto result = bind(c); !result) {
        return result;
    }
    if (c.construction.action_recording_disabled || !c.construction.action_record_streams_initialized) {
        return {};
    }
    //Assemble a whole typed command before touching the live stream. Existing
    //low-level writers retain native multi-write behavior through the same
    //owner.
    LayoutActionStream command;
    auto result = write_layout_action(command, action, stream_.cursor());
    if (!result) {
        return result;
    }
    if (stream_.write(command.bytes()) != static_cast<std::int32_t>(command.bytes().size())) {
        return std::unexpected(LayoutError { LayoutFailure::transfer, stream_.cursor(), action.code, "recording append failed" });
    }
    return {};
}

std::expected<void, LayoutError> LayoutReplayRuntime::write_save_prefix(Stream& out) {
    if (&out == &stream_) {
        return std::unexpected(LayoutError { LayoutFailure::binding, stream_.cursor(), {}, "recording cannot serialize its save prefix into itself" });
    }
    const auto bytes = stream_.bytes();
    stream_.append_position();
    if (!save::write(out, loaded_published_) || !save::write(out, static_cast<std::uint32_t>(bytes.size()))) {
        return std::unexpected(LayoutError { LayoutFailure::transfer, 0, {}, "cannot write recording save prefix" });
    }
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return std::unexpected(LayoutError { LayoutFailure::transfer, 0, {}, "recording exceeds one native stream transfer" });
    }
    if (out.write(bytes) != static_cast<std::int32_t>(bytes.size())) {
        return std::unexpected(LayoutError { LayoutFailure::transfer, 0, {}, "cannot write recording save payload" });
    }
    return {};
}

LayoutReplayProgress LayoutReplayRuntime::progress() const {
    return { state_, stream_.cursor(), stream_.bytes().size(), dispatched_, countdown_, loaded_published_, error_ };
}
} //namespace simcoaster
