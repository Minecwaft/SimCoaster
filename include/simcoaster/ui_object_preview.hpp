#pragma once
#include "ui_control.hpp"
#include "ui_draw.hpp"
#include "world_types.hpp"
#include <filesystem>

namespace simcoaster {
struct WorldRuntime;

//Original4CC0E0/4CC620 building preview. It owns detached model/animation
//state, keeps World mesh slots untouched and validates its object identity.
class UiObjectPreview {
    struct State;
    std::unique_ptr<State> state_;

public:
    UiObjectPreview(WorldRuntime&, const std::filesystem::path& game_root);
    ~UiObjectPreview();
    UiObjectPreview(const UiObjectPreview&) = delete;
    UiObjectPreview& operator=(const UiObjectPreview&) = delete;
    bool bind(ThingId, std::uint32_t animation_id = 5);
    //Build catalogue previews use a definition without constructing a World
    //object. The catalog/template owners, rather than a ThingId, are validated.
    bool bind_definition(std::uint16_t, std::uint32_t animation_id = 5);
    void clear();
    //Invoke before a retired World ID becomes reusable, as with Staff previews.
    void retire(ThingId);
    bool reconcile();
    void advance();
    const std::vector<std::string>& diagnostics() const;
    void append(UiDrawList&, UiControl&, UiRect clip, std::uint32_t pixel_width, std::uint32_t pixel_height, std::int32_t depth_offset = 1);
};
} //namespace simcoaster
