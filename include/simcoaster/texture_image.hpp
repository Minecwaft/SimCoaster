#pragma once
#include "mesh_model.hpp"
#include <memory>
#include <unordered_map>

namespace simcoaster {
using RgbaPixel = std::array<std::uint8_t, 4>;
enum class TextureAlpha {
    opaque,
    cutout,
    translucent
};
enum class TextureRowOrigin {
    top,
    bottom
};
//TexturePixelSource+1C source-mode bits, before renderer-state derivation.
enum class TextureSourceMode : std::uint32_t {
    opaque = 0,
    alpha_high = 0x100,
    alpha_low = 0x200,
    additive = 0x300
};

struct TextureImage {
    std::uint32_t width {}, height {};
    //Explicit RGBA rows; row zero is sampled at V=0 by the producer's UVs.
    //Mesh assets use native bottom-origin rows. Ordinary image decoding, glyph
    //atlases and framebuffer captures use top-origin rows with their own UVs.
    std::vector<RgbaPixel> pixels;
    TextureAlpha alpha {};
    std::optional<TextureSourceMode> source_mode {}; //Native palette/header policy can differ from visible
    //pixels.
};
enum class TextureFailure {
    header,
    format,
    bounds,
    dimensions,
    palette,
    compression,
    missing
};

struct TextureError {
    TextureFailure reason;
    std::size_t offset {};
    std::string resource;
};

std::string describe_texture_error(const TextureError&);
std::expected<TextureImage, TextureError> decode_tga_image(std::span<const std::byte>, TextureRowOrigin = TextureRowOrigin::top);
std::expected<TextureImage, TextureError> decode_fsh_image(std::span<const std::byte>, TextureRowOrigin = TextureRowOrigin::top);
using TextureImagePtr = std::shared_ptr<const TextureImage>;
//6A13B0/4CBEC0 select original image namespaces, not a synthetic mip bias.
enum class MeshTextureSet {
    full,
    small
};

constexpr MeshTextureSet mesh_texture_set_for_quality(std::int32_t quality) {
    return static_cast<std::uint32_t>(quality) < 2u ? MeshTextureSet::small : MeshTextureSet::full;
}

//An existing library is borrowed; otherwise root opens an owned loose/WAD
//provider. The namespace is exact: callers select textures/stexture, and
//native fixed directories such as MiscMesh/textures stay fixed.
struct MeshTextureSource {
    const MeshModelLibrary* library {};
    std::filesystem::path root;
    std::string relative_namespace;
};

struct MeshTextureSources {
    MeshTextureSource primary;
    std::optional<MeshTextureSource> secondary;
    bool use_quickload = true;
    //4D8E50 uses the first registered directory if there is no secondary.
    //Ordinary/UI model registration puts the model root there; generated
    //runtime registration puts the primary texture directory there instead.
    bool fallback_to_library_root = false;
};

class MeshTextureLibrary {
    struct Source {
        const MeshModelLibrary* library {};
        std::shared_ptr<MeshModelLibrary> owner;
        std::vector<std::shared_ptr<MeshModelLibrary>> alternatives;
        std::size_t active_provider {};
        std::filesystem::path root;
        std::string relative_namespace;
        std::optional<TextureError> error;
    };

    std::filesystem::path game_root_;
    MeshTextureSet texture_set_;
    bool animated_textures_;
    std::vector<Source> sources_;
    std::optional<std::size_t> secondary_, root_fallback_;
    bool quickload_ready_ {};
    std::vector<std::string> quickload_names_;
    std::optional<TextureError> quickload_error_;
    std::unordered_map<std::string, std::size_t> slot_sources_;
    //Fully qualified virtual paths need provider discovery only on first use.
    std::unordered_map<std::string, std::pair<std::size_t, std::string>> qualified_sources_;
    std::unordered_map<std::string, std::expected<TextureImagePtr, TextureError>> cache_;
    std::vector<TextureError> diagnostics_;
    std::optional<std::expected<TextureImagePtr, TextureError>> missing_texture_;
    Source open_source(MeshTextureSource) const;
    std::optional<std::pair<const MeshModelLibrary*, std::string>> locate(Source&, std::string_view);
    void load_quickload_index();
    std::expected<std::size_t, TextureError> select_source(std::string_view);
    std::expected<TextureImagePtr, TextureError> load_from(std::size_t, std::string_view);
    std::expected<TextureImagePtr, TextureError> load_qualified(std::string_view);
    std::expected<TextureImagePtr, TextureError> report(TextureError);

public:
    //Compatibility constructor: ordinary model root + selected texture folder,
    //then either the explicit secondary texture folder or theme SharedTex.
    //An empty theme/secondary retains the model-root fallback used by UI models.
    MeshTextureLibrary(const MeshModelLibrary&, const std::filesystem::path& game_root, std::string_view theme, std::filesystem::path additional_library_root = {}, MeshTextureSet texture_set = MeshTextureSet::full, bool animated_textures = true);
    MeshTextureLibrary(MeshTextureSources, const std::filesystem::path& game_root, MeshTextureSet texture_set = MeshTextureSet::full, bool animated_textures = true);

    MeshTextureSet texture_set() const {
        return texture_set_;
    }

    void set_animated_textures(bool enabled) {
        animated_textures_ = enabled;
    }

    bool animated_textures() const {
        return animated_textures_;
    }

    //Native feature0x40 chooses the slot's current source or the first source;
    //it does not stop the independent animation controller or UV movement.
    const std::string& filename(const MeshTextureSlot& slot) const {
        return slot.filenames.empty() ? slot.filename : slot.filenames.at(animated_textures_ ? slot.source_variant : 0);
    }

    //4D8E50 selects the directory from the FIRST filename once. 6DAAF0 loads
    //every animation variant from it. Generated UV ($) slots still load files;
    //a genuine unbound/private target without an image returns success/null.
    std::expected<TextureImagePtr, TextureError> load(const MeshTextureSlot&);
    //Qualified names address only their exact namespace/provider; bare names
    //retain the same first-filename directory-selection contract.
    std::expected<TextureImagePtr, TextureError> load(std::string_view);
    //6D8CA0 installs this actual original image as the global missing fallback.
    //If even this original image cannot load, native installs a null fallback.
    //Return success/null in that case and retain its failure in diagnostics.
    std::expected<TextureImagePtr, TextureError> missing_texture();

    const std::vector<TextureError>& diagnostics() const {
        return diagnostics_;
    }

    std::size_t loaded_count() const;
};
} //namespace simcoaster
