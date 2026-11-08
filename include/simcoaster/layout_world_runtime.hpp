#pragma once
#include "layout_replay.hpp"
#include "park_construction.hpp"

namespace simcoaster {
//Full native coaster replay is an input/editor workflow, not a sequence of
//independent geometry setters. MOVE60 selects/detaches a node; its first two
//WORDs are unused.55/56 select ordinary/special placement for later clicks.
//An editor owner must supply this contract before those streams can execute.
struct LayoutCoasterActionBackend {
    virtual ~LayoutCoasterActionBackend() = default;
    virtual void dispatch(AttractionWorld&, ParkConstructionRuntime&, const LayoutAction&) = 0;
    virtual void cancel(AttractionWorld&, ParkConstructionRuntime&) = 0;
};

//Production World adapter. Every effectful opcode is routed explicitly.
//Unsupported editor ownership is a reported dependency; it is never treated
//as a successful action or approximated by invented node coordinates.
class LayoutWorldBackend final : public LayoutReplayBackend {
    ParkConstructionRuntime& construction_;
    LayoutCoasterActionBackend* coaster_ {};

public:
    explicit LayoutWorldBackend(ParkConstructionRuntime& construction, LayoutCoasterActionBackend* coaster = nullptr) : construction_(construction), coaster_(coaster) {}

    void bind_coaster_editor(LayoutCoasterActionBackend* editor) {
        coaster_ = editor;
    }

    void clear_interaction_status(WorldRuntime&) override;
    void dispatch(WorldRuntime&, const LayoutAction&) override;
    void reset_train_rsse(WorldRuntime&) override;
    void cancel_replay(WorldRuntime&) override;
};

//Native replay rename path. Censorship preserves
//UTF16 length. The name storage is33 code units, including native unterminated
//full-length inputs; unbounded wchar_t string scans are not reproduced.
bool filter_layout_name(ThemeGlobalInfoManager&, std::u16string&, bool censor = true);
void apply_layout_object_name(WorldRuntime&, LayoutActionCode, std::uint16_t key, std::u16string_view);
} //namespace simcoaster
