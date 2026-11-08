#pragma once
#include "ui_draw.hpp"
#include "world_types.hpp"
#include <memory>

namespace simcoaster {
struct WorldRuntime;

struct UiPersonPreviewPlacement {
    std::array<std::int32_t, 4> bounds {};
    std::array<float, 4> clip { 0, 0, 2048, 1536 };
    std::int16_t depth {};
    float adjustment = 12.f;
};

//Detached native502B60/502D30 body preview. Person is a distinct record
//hierarchy, and is never interpreted as a StaffRuntimeRecord.
class UiPersonPreview {
    struct State;
    std::unique_ptr<State> state_;

public:
    explicit UiPersonPreview(WorldRuntime&);
    ~UiPersonPreview();
    UiPersonPreview(const UiPersonPreview&) = delete;
    UiPersonPreview& operator=(const UiPersonPreview&) = delete;
    bool bind_person(ThingId);
    void clear();
    void retire(ThingId);
    bool bound() const;
    bool refresh();
    bool append(UiDrawList&, const UiPersonPreviewPlacement&);
};
} //namespace simcoaster
