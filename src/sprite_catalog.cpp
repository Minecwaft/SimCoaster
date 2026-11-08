#include "simcoaster/sprite_catalog.hpp"
#include <format>

namespace simcoaster {
namespace {
std::string lower(std::string_view text) {
    std::string result { text };
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return result;
}

std::string_view leaf(std::string_view path) {
    const auto separator = path.find_last_of("\\/");
    return separator == path.npos ? path : path.substr(separator + 1);
}

std::string_view parent(std::string_view path) {
    const auto separator = path.find_last_of("\\/");
    return separator == path.npos ? std::string_view {} : path.substr(0, separator);
}

template <class T>
T read(std::span<const std::byte> bytes, std::size_t offset) {
    T value {};
    save::detail::decode<T>(std::span<const std::byte, save::value_width<T>> { bytes.data() + offset, save::value_width<T> }, value);
    return value;
}
} //namespace

static_assert(save::record_width<SpriteDescriptor> == 66);
static_assert(save::record_width<SpriteFrameIndexEntry> == 4 && save::record_width<SpriteComponentMetadata> == 4);

std::uint16_t SpriteDescriptor::nonzero_frame_stride_entry_count() const {
    return static_cast<std::uint16_t>(std::ranges::count_if(frame_index_entries, [](const auto& entry) {
        return entry.frame_stride != 0;
    }));
}

std::uint16_t SpriteDescriptor::available_component_kind_count() const {
    return static_cast<std::uint16_t>(std::ranges::count_if(component_metadata, [](const auto& entry) {
        return entry.one_based_selector_00 != 0;
    }));
}

std::expected<SpriteDescriptor, SpriteMetadataError> parse_sprite_descriptor(std::span<const std::byte> bytes) {
    //Game.exe6875744 discards 8+4+256 bytes before its first useful field.
    constexpr std::size_t prefix = 268, core_end = prefix + save::record_width<SpriteDescriptor>;
    if (bytes.size() < core_end) {
        return std::unexpected(SpriteMetadataError { SpriteMetadataFailure::incomplete_descriptor, bytes.size() });
    }
    auto result = read<SpriteDescriptor>(bytes, prefix);
    if (bytes.size() == core_end) {
        return result; //Older descriptors omit all component metadata.
    }
    constexpr auto component_bytes = save::value_width<decltype(result.component_metadata)>;
    if (bytes.size() - core_end < component_bytes) {
        return std::unexpected(SpriteMetadataError { SpriteMetadataFailure::incomplete_components, bytes.size() });
    }
    result.component_metadata = read<decltype(result.component_metadata)>(bytes, core_end);
    return result;
}

std::int32_t kid_sprite_variant_limit(const SpriteCatalogSettings& settings) {
    if (settings.profile_variant_selection_mask == 0x4000) {
        return match (settings.profile_selection_mask) -> std::int32_t {
            case 8  => 8;
            case 16 => 6;
            case 32 => 4;
            case _  => 0;
        };
    }
    return match (settings.num_kids) -> std::int32_t {
        case 0 => 2;
        case 1 => 4;
        case 2 => 6;
        case _ => 8;
    };
}

std::int32_t staff_sprite_variant_limit(const SpriteCatalogSettings& settings) {
    if (settings.profile_variant_selection_mask == 0x4000) {
        return match (settings.profile_selection_mask) -> std::int32_t {
            case 8 or 16 => 2;
            case 32      => 1;
            case _       => 0;
        };
    }
    return settings.num_kids == 0 ? 1 : 2;
}

std::string sprite_pixel_sidecar_path(std::string_view descriptor_path, const SpriteDescriptor& descriptor, const SpriteCatalogSettings& settings) {
    std::string result { descriptor_path.substr(0, descriptor_path.size() - 4) };
    const bool f_family = settings.use_f_family && descriptor.texture_suffix_reload_flag;
    result += settings.use_c_suffix ? (f_family ? ".fpc" : ".tpc") : (f_family ? ".fps" : ".tps");
    return result;
}

std::string describe_sprite_catalog_error(const SpriteCatalogError& error) {
    const auto detail = match (error.cause) -> std::string {
        case { const AssetError& cause } => std::format("{} at byte {}", asset_failure_name(cause.reason), cause.offset);
        case {
            const SpriteMetadataError& cause
        } => do {
            const auto text = match (cause.reason) -> std::string_view {
                case SpriteMetadataFailure::incomplete_descriptor => "incomplete ESP descriptor";
                case SpriteMetadataFailure::incomplete_components => "incomplete ESP component metadata";
                case SpriteMetadataFailure::resource_capacity     => "sprite catalog exceeds512 resources";
            };
            do_return std::format("{} at byte {}", text, cause.offset);
        };
    };
    return std::format("{}: {}", error.path, detail);
}

std::expected<LoadedSpriteCatalog, SpriteCatalogError> load_sprite_catalog(const DwfbArchive& archive, std::string_view source, const SpriteCatalogSettings& settings) {
    LoadedSpriteCatalog result;
    for (std::size_t category = 0; category < sprite_category_names.size(); ++category) {
        const auto name = sprite_category_names[category];
        auto& range = result.variants[category];
        range = { name, static_cast<std::int32_t>(result.resources.size()), 0 };
        std::vector<std::size_t> candidates;
        //Native namespace searches append generic candidates before source ones.
        //Equal leaf names retain that enumeration order; neither overrides another.
        for (const auto& directory : { std::string { "generic\\" } + std::string { name }, lower(source) + "\\" + std::string { name } }) {
            for (std::size_t i = 0; i < archive.entries().size(); ++i) {
                const auto& path = archive.entries()[i].path;
                if (candidates.size() < 128 && parent(path) == directory && path.ends_with(".esp")) {
                    candidates.push_back(i);
                }
            }
        }
        std::stable_sort(candidates.begin(), candidates.end(), [&](auto left, auto right) {
            return leaf(archive.entries()[left].path) < leaf(archive.entries()[right].path);
        });
        std::vector<bool> accepted(candidates.size());
        bool finished = false;
        const bool kids = name == "kids" || name == "kidsheads";
        const bool staff = name == "handymen" || name == "mechanics" || name == "guards" || name == "researchers";
        const auto accept = [&](std::size_t position) -> std::expected<void, SpriteCatalogError> {
            if (accepted[position]) {
                return {};
            }
            const auto index = candidates[position];
            const auto& path = archive.entries()[index].path;
            auto bytes = archive.read_entry(index);
            if (!bytes) {
                return std::unexpected(SpriteCatalogError { path, bytes.error() });
            }
            auto descriptor = parse_sprite_descriptor(*bytes);
            if (!descriptor) {
                return std::unexpected(SpriteCatalogError { path, descriptor.error() });
            }
            auto pixel_path = sprite_pixel_sidecar_path(path, *descriptor, settings);
            const auto pixel_index = archive.find_entry(pixel_path);
            if (!pixel_index) {
                result.missing_sidecars.push_back(std::move(pixel_path));
                return {};
            }
            auto pixels = archive.read_entry(*pixel_index);
            if (!pixels) {
                return std::unexpected(SpriteCatalogError { pixel_path, pixels.error() });
            }
            if (result.resources.size() == 512) {
                return std::unexpected(SpriteCatalogError { path, SpriteMetadataError { SpriteMetadataFailure::resource_capacity, 0 } });
            }
            result.resources.push_back({ path, std::move(pixel_path), *descriptor, std::move(*pixels) });
            accepted[position] = true;
            ++range.loaded_resource_count;
            finished = result.resources.size() == 512 || (kids && range.loaded_resource_count >= kid_sprite_variant_limit(settings)) || (staff && range.loaded_resource_count >= staff_sprite_variant_limit(settings));
            return {};
        };
        if (kids) {
            for (std::string_view avatar : { "spr_bi.esp", "spr_ki.esp", "spr_ta.esp", "spr_su.esp" }) {
                for (std::size_t i = 0; i < candidates.size() && !finished; ++i) {
                    if (leaf(archive.entries()[candidates[i]].path) == avatar) {
                        if (auto added = accept(i); !added) {
                            return std::unexpected(added.error());
                        }
                    }
                }
                if (finished) {
                    break;
                }
            }
        }
        for (std::size_t i = 0; i < candidates.size() && !finished; ++i) {
            if (auto added = accept(i); !added) {
                return std::unexpected(added.error());
            }
        }
    }
    return result;
}

std::expected<LoadedSpriteCatalog, SpriteCatalogError> load_installed_sprite_catalog(const std::filesystem::path& root, std::string_view source, const SpriteCatalogSettings& settings) {
    const auto path = root / "Data" / "esprites.wad";
    auto archive = load_dwfb_archive(path);
    if (!archive) {
        return std::unexpected(SpriteCatalogError { path.generic_native_encoded_string(), archive.error() });
    }
    return load_sprite_catalog(*archive, source, settings);
}
} //namespace simcoaster
