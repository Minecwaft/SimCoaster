#pragma once
#include "resource_archive.hpp"
#include "world_types.hpp"
#include <variant>

namespace simcoaster {
struct SpriteFrameIndexEntry {
    [[=save::Field{ "base_frame_index", 0 }]] std::uint16_t base_frame_index{};
    [[=save::Field{ "frame_stride", 1 }]] std::uint8_t frame_stride{};
    [[=save::Field{ "variant_frame_count", 2 }]] std::uint8_t variant_frame_count{};
};

struct SpriteComponentMetadata {
    [[=save::Field{ "one_based_selector_00", 0 }]] std::uint8_t one_based_selector_00{};
    [[=save::Field{ "one_based_selector_01", 1 }]] std::uint8_t one_based_selector_01{};
    [[=save::Field{ "value_02", 2 }]] std::uint8_t value_02{};
    [[=save::Field{ "value_03", 3 }]] std::uint8_t value_03{};
};

struct SpriteDescriptor {
    [[=save::Field{ "texture_suffix_reload_flag", 0 }]] std::uint8_t texture_suffix_reload_flag{};
    [[=save::Field{ "sprite_gender_flag", 1 }]] std::uint8_t sprite_gender_flag{};
    [[=save::Field{ "frame_index_entries", 2 }]] std::array<SpriteFrameIndexEntry, 16> frame_index_entries{};
    std::array<SpriteComponentMetadata, 4> component_metadata{};
    std::uint16_t nonzero_frame_stride_entry_count() const;
    std::uint16_t available_component_kind_count() const;
};
enum class SpriteMetadataFailure {
    incomplete_descriptor,
    incomplete_components,
    resource_capacity
};

struct SpriteMetadataError {
    SpriteMetadataFailure reason;
    std::size_t offset;
};

std::expected<SpriteDescriptor, SpriteMetadataError> parse_sprite_descriptor(std::span<const std::byte>);

struct SpriteCatalogSettings {
    std::int32_t num_kids = 2;
    std::uint32_t profile_variant_selection_mask{}, profile_selection_mask{};
    bool use_c_suffix = true, use_f_family = true;
};

std::int32_t kid_sprite_variant_limit(const SpriteCatalogSettings&);
std::int32_t staff_sprite_variant_limit(const SpriteCatalogSettings&);
std::string sprite_pixel_sidecar_path(std::string_view, const SpriteDescriptor&, const SpriteCatalogSettings&);
inline constexpr std::array<std::string_view, 20> sprite_category_names{
    "kids",         "parents",      "grandparents", "kidsheads", "parentsheads", "grandparentsheads", "costumes",
    "costumeheads", "entertainers", "handymen",     "mechanics", "guards",       "researchers",       "thoughts",
    "balloons",     "litter",       "specialfx",    "particles", "gardeners",    "umbrellas"
};

struct SpriteResourceAsset {
    std::string descriptor_path, pixel_path;
    SpriteDescriptor descriptor;
    //Owned original pixel payload, ready for the renderer's frame decoder.
    //Category acceptance depends on opening this file, not decoding its pixels.
    std::vector<std::byte> pixel_payload;
};

struct LoadedSpriteCatalog {
    std::array<ResourceCategoryVariantRange, 20> variants{};
    std::vector<SpriteResourceAsset> resources;
    std::vector<std::string> missing_sidecars;
};

struct SpriteCatalogError {
    std::string path;
    std::variant<AssetError, SpriteMetadataError> cause;
};

std::string describe_sprite_catalog_error(const SpriteCatalogError&);
//Full category pass over the Sprites namespace supplied by this archive.
//The installed game supplies this namespace through Data/esprites.wad.
std::expected<LoadedSpriteCatalog, SpriteCatalogError>
load_sprite_catalog(const DwfbArchive&, std::string_view source, const SpriteCatalogSettings& = {});
std::expected<LoadedSpriteCatalog, SpriteCatalogError>
load_installed_sprite_catalog(const std::filesystem::path& game_root,
                              std::string_view source,
                              const SpriteCatalogSettings& = {});
} //namespace simcoaster
