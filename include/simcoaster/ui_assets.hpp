#pragma once
#include "ui_draw.hpp"

namespace simcoaster {
enum class UiModelAssetKind {
    model,
    frame,
    slider
};

struct UiModelAssetInfo {
    std::string name;
    std::uint32_t hash {};
    UiModelAssetKind kind {};
    std::size_t parts {};
};

struct UiModelPlacement {
    //Native attachment rectangle uses logical edge differences, not a pixel
    //count. The model layout adds the recovered half-coordinate correction.
    std::array<float, 4> bounds {};
    std::array<float, 4> clip { 0, 0, 2048, 1536 };
    std::array<float, 2> pivot {};
    std::uint16_t part_base {}, part_value {};
    std::int16_t depth {}, rotation {};
    RgbaPixel color { 255, 255, 255, 255 };
};

//Native registration hashes signed name bytes with multiplier47 and no case
//folding. Identity is the loaded root node name, not the model filename.
std::uint32_t ui_model_asset_hash(std::string_view);

class UiAssetLibrary {
    struct State;
    std::unique_ptr<State> state_;

public:
    explicit UiAssetLibrary(const std::filesystem::path& game_root, std::string language = "American", MeshTextureSet texture_set = MeshTextureSet::full, bool animated_textures = true);
    ~UiAssetLibrary();
    UiAssetLibrary(const UiAssetLibrary&) = delete;
    UiAssetLibrary& operator=(const UiAssetLibrary&) = delete;
    void animated_textures(bool);
    std::span<const UiModelAssetInfo> assets() const;
    const UiModelAssetInfo* find(std::uint32_t hash) const;
    //The shipped Staff templates reference an unregistered staffstat model.
    //Native lookup returns null for that binding; actual registered art wins.
    bool is_original_null_binding(std::uint32_t hash) const;
    //Shares the original UI texture namespace with procedural attachments.
    std::expected<TextureImagePtr, TextureError> texture(std::string_view);
    //No control pointer crosses this boundary. Parts/layout are per-instance;
    //immutable models/textures remain shared across all control attachments.
    bool append(UiDrawList&, std::uint32_t hash, const UiModelPlacement&);
    const std::vector<std::string>& diagnostics() const;
};
} //namespace simcoaster
