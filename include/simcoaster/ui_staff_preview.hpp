#pragma once
#include "ui_draw.hpp"
#include "world_types.hpp"
#include <memory>

namespace simcoaster {
struct WorldRuntime;

struct UiStaffPreviewPlacement {
    std::array<std::int32_t, 4> bounds{};
    std::array<float, 4> clip{ 0, 0, 2048, 1536 };
    //Pass the control attachment depth + 1, as in native 58F8D0.
    std::int16_t depth{};
    float adjustment = 8.f;
};

//One provider per preview control. WorldRuntime and its immutable sprite catalog
//must outlive the provider. All operations run on the owning UI/game thread.
//Preview script objects are independently owned, never retained in the World
//resource table between calls. No live entity or candidate pointer is owned.
class UiStaffPreview {
    struct State;
    std::unique_ptr<State> state_;

public:
    explicit UiStaffPreview(WorldRuntime&);
    ~UiStaffPreview();
    UiStaffPreview(const UiStaffPreview&) = delete;
    UiStaffPreview& operator=(const UiStaffPreview&) = delete;

    bool bind_candidate(std::size_t pool_slot);
    bool bind_staff(ThingId);
    void clear();
    //Deliver the retirement notification before an ID may be reused.
    void retire(ThingId);
    bool bound() const;
    //Reconcile identity and advance owned animation at its native deadline.
    //append calls this too; multiple calls in one tick do not advance twice.
    bool refresh();
    //false: no current source/frame or empty clip. Asset corruption throws.
    bool append(UiDrawList&, const UiStaffPreviewPlacement&);
};
} //namespace simcoaster
