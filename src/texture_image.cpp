#include "simcoaster/texture_image.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace simcoaster {
namespace {
[[noreturn]] void fail(TextureFailure reason, std::size_t offset = 0) {
    throw TextureError { reason, offset, {} };
}

struct Reader {
    std::span<const std::byte> bytes;

    void range(std::size_t offset, std::size_t size) const {
        if (offset > bytes.size() || size > bytes.size() - offset) {
            fail(TextureFailure::bounds, offset);
        }
    }

    std::uint8_t byte(std::size_t offset) const {
        range(offset, 1);
        return std::to_integer<std::uint8_t>(bytes[offset]);
    }

    template <class T>
    T get(std::size_t offset) const {
        range(offset, save::value_width<T>);
        T value {};
        save::detail::decode<T>(std::span<const std::byte, save::value_width<T>> { bytes.data() + offset, save::value_width<T> }, value);
        return value;
    }
};

TextureImage image(std::uint32_t width, std::uint32_t height) {
    if (!width || !height || width > 16384 || height > 16384) {
        fail(TextureFailure::dimensions);
    }
    TextureImage result;
    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<std::size_t>(width) * height);
    return result;
}

void alpha(TextureImage& value) {
    bool transparent = false;
    for (const auto& pixel : value.pixels) {
        if (pixel[3] != 255) {
            transparent = true;
            if (pixel[3]) {
                value.alpha = TextureAlpha::translucent;
                return;
            }
        }
    }
    value.alpha = transparent ? TextureAlpha::cutout : TextureAlpha::opaque;
}

std::string normalized(std::string_view text) {
    std::string result { text };
    for (auto& c : result) {
        if (c == '\\') {
            c = '/';
        } else if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return result;
}

std::string texture_namespace(std::string name, MeshTextureSet set) {
    const bool small = set == MeshTextureSet::small;
    constexpr std::array<std::array<std::string_view, 2>, 3> directories { { { "textures/", "stexture/" }, { "gtexture/", "sgtextur/" }, { "pathtex/", "spathtex/" } } };
    for (const auto& pair : directories) {
        for (const auto prefix : pair) {
            if (name.starts_with(prefix)) {
                return std::string(pair[small ? 1u : 0u]) + name.substr(prefix.size());
            }
        }
    }
    return name;
}

std::string name_space(std::string_view text) {
    auto result = normalized(text);
    while (result.starts_with('/')) {
        result.erase(0, 1);
    }
    while (result.ends_with('/')) {
        result.pop_back();
    }
    return result;
}

std::string selected_filename(std::string_view text) {
    auto result = normalized(text);
    //4D6F20 mutates the first authored filename before directory selection.
    //Leading$ selects generated UVs, but the image remains file-backed.
    if (result.starts_with('$')) {
        result.erase(0, 1);
    } else if (const auto dollar = result.find('$'); dollar != std::string::npos) {
        result.replace(dollar, std::string::npos, ".tga");
    }
    return result;
}

std::string joined(std::string_view directory, std::string_view filename) {
    return directory.empty() ? std::string { filename } : std::string { directory } + "/" + std::string { filename };
}

std::string with_extension(std::string_view text, std::string_view extension) {
    auto result = std::filesystem::path { std::string { text } };
    result.replace_extension(extension);
    return normalized(result.native_encoded_string());
}

std::optional<std::string> entry_name(const MeshModelLibrary& library, std::string_view relative) {
    if (library.archive) {
        if (const auto found = library.archive->find_entry(relative)) {
            return library.archive->entries()[*found].path;
        }
    } else if (std::filesystem::is_regular_file(library.source / relative)) {
        return std::string { relative };
    }
    return std::nullopt;
}

TextureError resource_error(const MeshError& error, std::string resource) {
    return { error.reason == MeshFailure::missing ? TextureFailure::missing : TextureFailure::bounds, error.offset, std::move(resource) };
}

MeshTextureSources ordinary_sources(const MeshModelLibrary& primary, const std::filesystem::path& root, std::string_view theme, const std::filesystem::path& additional, MeshTextureSet set) {
    const auto directory = set == MeshTextureSet::small ? "stexture" : "textures";
    MeshTextureSources result { { &primary, {}, directory }, {}, true, true };
    if (!additional.empty()) {
        result.secondary = MeshTextureSource { nullptr, std::filesystem::absolute(additional), directory };
    } else if (!theme.empty()) {
        result.secondary = MeshTextureSource { nullptr, std::filesystem::absolute(root) / "Data/levels" / theme / (set == MeshTextureSet::small ? "ssharete" : "sharetex"), {} };
    }
    return result;
}
} //namespace

std::string describe_texture_error(const TextureError& error) {
    const auto message = match (error.reason) -> std::string_view {
        case TextureFailure::header      => "invalid texture header";
        case TextureFailure::format      => "unsupported texture format";
        case TextureFailure::bounds      => "truncated texture data";
        case TextureFailure::dimensions  => "invalid texture dimensions";
        case TextureFailure::palette     => "invalid texture palette";
        case TextureFailure::compression => "invalid compressed FSH pixels";
        case TextureFailure::missing     => "texture resource is missing";
    };
    return error.resource + ": " + std::string { message } + " at byte " + std::to_string(error.offset);
}

std::expected<TextureImage, TextureError> decode_tga_image(std::span<const std::byte> bytes, TextureRowOrigin origin) {
    try {
        Reader r { bytes };
        r.range(0, 18);
        const auto type = r.byte(2), depth = r.byte(16), descriptor = r.byte(17);
        const bool indexed = type == 1 || type == 9, gray = type == 3 || type == 11, rle = type >= 9;
        if (!(indexed || gray || type == 2 || type == 10) || r.byte(1) > 1 || (indexed && !r.byte(1))) {
            fail(TextureFailure::format);
        }
        if (!(depth == 8 || depth == 15 || depth == 16 || depth == 24 || depth == 32)) {
            fail(TextureFailure::format, 16);
        }
        auto result = image(r.get<std::uint16_t>(12), r.get<std::uint16_t>(14));
        std::size_t cursor = 18 + r.byte(0);
        r.range(cursor, 0);
        std::vector<RgbaPixel> palette;
        const auto palette_count = r.get<std::uint16_t>(5);
        const auto palette_start = r.get<std::uint16_t>(3);
        if (indexed) {
            if (r.byte(7) != 24 || static_cast<std::size_t>(palette_start) + palette_count > 256) {
                fail(TextureFailure::palette, 5);
            }
            palette.resize(static_cast<std::size_t>(palette_start) + palette_count);
            r.range(cursor, static_cast<std::size_t>(palette_count) * 3);
            for (std::size_t i = 0; i < palette_count; ++i) {
                palette[palette_start + i] = { r.byte(cursor + 2), r.byte(cursor + 1), r.byte(cursor), 255 };
                cursor += 3;
            }
        } else {
            const auto size = static_cast<std::size_t>(palette_count) * ((r.byte(7) + 7u) / 8u);
            r.range(cursor, size);
            cursor += size;
        }
        const auto pixel = [&]() -> RgbaPixel {
            const auto start = cursor;
            cursor += (depth + 7u) / 8u;
            r.range(start, cursor - start);
            if (indexed) {
                const auto index = r.byte(start);
                if (depth != 8 || index < palette_start || index >= palette.size()) {
                    fail(TextureFailure::palette, start);
                }
                return palette[index];
            }
            if (gray) {
                const auto value = r.byte(start);
                return { value, value, value, depth == 16 ? r.byte(start + 1) : std::uint8_t { 255 } };
            }
            if (depth == 24 || depth == 32) {
                return { r.byte(start + 2), r.byte(start + 1), r.byte(start), depth == 32 ? r.byte(start + 3) : std::uint8_t { 255 } };
            }
            if (depth != 15 && depth != 16) {
                fail(TextureFailure::format, start);
            }
            const auto value = r.get<std::uint16_t>(start);
            const auto channel = [](unsigned v) {
                return static_cast<std::uint8_t>((v * 255u + 15u) / 31u);
            };
            return { channel((value >> 10) & 31u), channel((value >> 5) & 31u), channel(value & 31u), static_cast<std::uint8_t>((depth == 16 && (descriptor & 15u) && !(value & 0x8000u)) ? 0 : 255) };
        };
        //6D93D9/6D952E select a vertical flip for TOP-origin TGA input.
        //MeshTextureLibrary requests that native bottom-origin convention.
        //Other image clients retain their explicit top-origin canvas convention.
        std::size_t written = 0;
        const auto put = [&](const RgbaPixel& value) {
            auto x = written % result.width, y = written / result.width;
            if (descriptor & 0x10u) {
                x = result.width - 1 - x;
            }
            if (((descriptor & 0x20u) != 0) != (origin == TextureRowOrigin::top)) {
                y = result.height - 1 - y;
            }
            result.pixels[x + y * result.width] = value;
            ++written;
        };
        while (written < result.pixels.size()) {
            const auto packet = rle ? r.byte(cursor++) : std::uint8_t {};
            const auto count = rle ? static_cast<std::size_t>((packet & 127u) + 1) : 1u;
            if (count > result.pixels.size() - written) {
                fail(TextureFailure::bounds, cursor);
            }
            if (rle && (packet & 0x80u)) {
                const auto value = pixel();
                for (std::size_t i = 0; i < count; ++i) {
                    put(value);
                }
            } else {
                for (std::size_t i = 0; i < count; ++i) {
                    put(pixel());
                }
            }
        }
        alpha(result);
        //0x6D96D0:32-bit input sets reader bit1; semi-alpha coverage at
        //least truncated(pixel_count*.05) sets bit2, including a zero threshold.
        const auto semitransparent = std::ranges::count_if(result.pixels, [](const auto& texel) {
            return texel[3] > 0 && texel[3] < 240;
        });
        const auto threshold = static_cast<std::size_t>(static_cast<double>(result.pixels.size()) * 0.050000001);
        result.source_mode = static_cast<std::size_t>(semitransparent) >= threshold ? TextureSourceMode::alpha_low : (depth == 32 ? TextureSourceMode::alpha_high : TextureSourceMode::opaque);
        return result;
    } catch (const TextureError& error) {
        return std::unexpected(error);
    }
}

std::expected<TextureImage, TextureError> decode_fsh_image(std::span<const std::byte> bytes, TextureRowOrigin origin) {
    try {
        Reader r { bytes };
        r.range(0, 24);
        if (r.get<std::array<char, 4>>(0) != std::array { 'S', 'H', 'P', 'I' } || r.get<std::uint32_t>(8) != 1) {
            fail(TextureFailure::header);
        }
        const auto record = static_cast<std::size_t>(r.get<std::uint32_t>(20));
        r.range(record, 16);
        auto result = image(r.get<std::uint16_t>(record + 4), r.get<std::uint16_t>(record + 6));
        const auto palette_record = record + (r.get<std::uint32_t>(record) >> 8);
        r.range(palette_record, 16);
        const auto palette_type = r.byte(palette_record);
        const auto count = r.get<std::uint16_t>(palette_record + 4);
        if ((palette_type != 36 && palette_type != 42) || !count || count > 256) {
            fail(TextureFailure::palette, palette_record);
        }
        const std::size_t stride = palette_type == 36 ? 3 : 4;
        std::array<RgbaPixel, 256> palette {};
        r.range(palette_record + 16, count * stride);
        result.source_mode = TextureSourceMode::opaque;
        for (std::size_t i = 0; i < count; ++i) {
            const auto p = palette_record + 16 + i * stride;
            palette[i] = palette_type == 36 ? RgbaPixel { r.byte(p), r.byte(p + 1), r.byte(p + 2), 255 } : RgbaPixel { r.byte(p + 2), r.byte(p + 1), r.byte(p), r.byte(p + 3) };
            if (palette[i][3] < 240) {
                result.source_mode = TextureSourceMode::alpha_low;
            }
        }
        std::vector<std::byte> unpacked;
        std::span<const std::byte> pixels;
        if (r.byte(record) & 0x80u) {
            auto decoded = decode_lz10fb_payload(bytes.subspan(record + 16), result.pixels.size() * 2 + 4096);
            if (!decoded) {
                fail(TextureFailure::compression, record + 16);
            }
            unpacked = std::move(decoded->bytes);
            pixels = unpacked;
        } else {
            r.range(record + 16, result.pixels.size());
            pixels = bytes.subspan(record + 16, result.pixels.size());
        }
        if (pixels.size() < result.pixels.size()) {
            fail(TextureFailure::bounds, record + 16);
        }
        //6D9264 marks FSH sources with reader bit20;6D9F59 maps that to
        //source40000, so both native upload paths reverse their rows (6C0B70).
        //Fold that upload into the mesh owner's requested row direction.
        for (std::size_t i = 0; i < result.pixels.size(); ++i) {
            const auto index = std::to_integer<std::uint8_t>(pixels[i]);
            if (index >= count) {
                fail(TextureFailure::palette, i);
            }
            const auto y = origin == TextureRowOrigin::bottom ? result.height - 1 - i / result.width : i / result.width;
            result.pixels[y * result.width + i % result.width] = palette[index];
        }
        alpha(result);
        return result;
    } catch (const TextureError& error) {
        return std::unexpected(error);
    }
}

MeshTextureLibrary::MeshTextureLibrary(const MeshModelLibrary& primary, const std::filesystem::path& root, std::string_view theme, std::filesystem::path additional, MeshTextureSet set, bool animated) : MeshTextureLibrary(ordinary_sources(primary, root, theme, additional, set), root, set, animated) {}

MeshTextureLibrary::MeshTextureLibrary(MeshTextureSources policy, const std::filesystem::path& root, MeshTextureSet set, bool animated) : game_root_(std::filesystem::absolute(root).lexically_normal()), texture_set_(set), animated_textures_(animated) {
    sources_.push_back(open_source(std::move(policy.primary)));
    if (policy.secondary) {
        secondary_ = sources_.size();
        sources_.push_back(open_source(std::move(*policy.secondary)));
    } else if (policy.fallback_to_library_root) {
        //4C1740 registers the model directory before its texture directory.
        //4D85F0 generated registrations instead omit that first directory.
        root_fallback_ = sources_.size();
        auto fallback = sources_.front();
        fallback.relative_namespace.clear();
        sources_.push_back(std::move(fallback));
    }
    if (policy.use_quickload) {
        load_quickload_index();
    }
}

MeshTextureLibrary::Source MeshTextureLibrary::open_source(MeshTextureSource description) const {
    Source result;
    result.relative_namespace = name_space(description.relative_namespace);
    if (description.library) {
        result.library = description.library;
        result.root = std::filesystem::absolute(description.library->source).lexically_normal();
    } else {
        result.root = description.root;
        if (result.root.is_relative()) {
            result.root = game_root_ / result.root;
        }
        result.root = result.root.lexically_normal();
    }
    if (normalized(result.root.extension().native_encoded_string()) == ".wad") {
        result.root.replace_extension();
    }
    auto archive = result.root;
    archive += ".wad";
    //A registered native directory may have both providers. A borrowed model
    //library is the active provider; fresh registrations try loose files first.
    //A cursor-only loose directory must not mask files in the mounted WAD.
    for (const auto& path : { result.root, archive }) {
        if ((!std::filesystem::is_directory(path) && !std::filesystem::is_regular_file(path)) || (result.library && normalized(std::filesystem::absolute(result.library->source).lexically_normal().native_encoded_string()) == normalized(path.native_encoded_string()))) {
            continue;
        }
        auto opened = open_mesh_model_library(path);
        if (!opened) {
            if (!result.error) {
                result.error = resource_error(opened.error(), path.native_encoded_string());
            }
            continue;
        }
        auto provider = std::make_shared<MeshModelLibrary>(std::move(*opened));
        if (!result.library) {
            result.owner = std::move(provider);
            result.library = result.owner.get();
        } else {
            result.alternatives.push_back(std::move(provider));
        }
    }
    if (!result.library && !result.error) {
        result.error = TextureError { TextureFailure::missing, 0, result.root.native_encoded_string() };
    }
    return result;
}

std::optional<std::pair<const MeshModelLibrary*, std::string>> MeshTextureLibrary::locate(Source& source, std::string_view relative) {
    const auto provider = [&](std::size_t selected) {
        return selected ? source.alternatives.at(selected - 1).get() : source.library;
    };
    const auto probe = [&](std::size_t selected) -> std::optional<std::pair<const MeshModelLibrary*, std::string>> {
        const auto* library = provider(selected);
        if (!library) {
            return std::nullopt;
        }
        if (auto entry = entry_name(*library, relative)) {
            //Native existence probes actually open the stream. A loose file that
            //cannot be opened (including a sharing lock) must not mask another
            //mounted provider of the same filename and extension.
            if (!library->archive) {
                std::ifstream probe_file(library->source / *entry, std::ios::binary);
                if (!probe_file) {
                    return std::nullopt;
                }
            }
            source.active_provider = selected;
            return std::pair { library, std::move(*entry) };
        }
        return std::nullopt;
    };
    const auto active = source.active_provider;
    if (auto entry = probe(active)) {
        return entry;
    }
    for (std::size_t selected = 0; selected <= source.alternatives.size(); ++selected) {
        if (selected != active) {
            if (auto entry = probe(selected)) {
                return entry;
            }
        }
    }
    return std::nullopt;
}

void MeshTextureLibrary::load_quickload_index() {
    auto& source = sources_.front();
    if (!source.library) {
        return;
    }
    const std::array paths { std::string { "qickload.txt" }, joined(source.relative_namespace, "qickload.txt") };
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto entry = locate(source, paths[index]);
        if (!entry) {
            continue;
        }
        const auto bytes = entry->first->read_resource(entry->second);
        if (!bytes) {
            quickload_error_ = resource_error(bytes.error(), source.root.native_encoded_string() + "/" + paths[index]);
            continue;
        }
        if (!*bytes) {
            continue;
        }
        quickload_ready_ = true;
        if (!index) {
            continue;
        }
        //4C1740/460840 read printable ASCII tokens, including punctuation and
        //comment markers, then4C15D0 compares case-insensitively without
        //suffixes.
        const auto& data = **bytes;
        for (std::size_t cursor = 0; cursor < data.size();) {
            while (cursor < data.size() && (std::to_integer<unsigned char>(data[cursor]) < 33 || std::to_integer<unsigned char>(data[cursor]) > 126)) {
                ++cursor;
            }
            const auto start = cursor;
            while (cursor < data.size() && std::to_integer<unsigned char>(data[cursor]) >= 33 && std::to_integer<unsigned char>(data[cursor]) <= 126) {
                ++cursor;
            }
            if (cursor > start) {
                quickload_names_.push_back(with_extension({ reinterpret_cast<const char*>(data.data() + start), cursor - start }, {}));
            }
        }
    }
}

std::expected<std::size_t, TextureError> MeshTextureLibrary::select_source(std::string_view raw) {
    const auto first = selected_filename(raw);
    if (const auto existing = slot_sources_.find(first); existing != slot_sources_.end()) {
        return existing->second;
    }
    if (quickload_error_) {
        return std::unexpected(*quickload_error_);
    }
    auto& primary = sources_.front();
    bool found = false;
    if (quickload_ready_) {
        const auto key = with_extension(first, {});
        found = std::ranges::find(quickload_names_, key) != quickload_names_.end();
    } else if (primary.library) {
        for (const auto extension : { ".fsh", ".tga" }) {
            if (locate(primary, joined(primary.relative_namespace, with_extension(first, extension)))) {
                found = true;
                break;
            }
        }
    }
    const auto selected = found ? std::size_t {} : secondary_.value_or(root_fallback_.value_or(0));
    slot_sources_.emplace(first, selected);
    return selected;
}

std::expected<TextureImagePtr, TextureError> MeshTextureLibrary::report(TextureError error) {
    if (std::ranges::none_of(diagnostics_, [&](const auto& previous) {
            return previous.reason == error.reason && previous.offset == error.offset && previous.resource == error.resource;
        })) {
        diagnostics_.push_back(error);
    }
    return std::unexpected(std::move(error));
}

std::expected<TextureImagePtr, TextureError> MeshTextureLibrary::load_from(std::size_t source_index, std::string_view raw) {
    const auto filename = selected_filename(raw);
    auto& source = sources_.at(source_index);
    const auto relative = joined(source.relative_namespace, filename);
    const auto resource = source.root.native_encoded_string() + "/" + relative;
    //6DA260 keys the selected directory AND filename, never filename alone.
    const auto key = normalized(resource);
    if (const auto cached = cache_.find(key); cached != cache_.end()) {
        return cached->second;
    }
    if (!source.library) {
        auto error = source.error.value_or(TextureError { TextureFailure::missing, 0, resource });
        error.resource = resource;
        cache_.emplace(key, std::unexpected(error));
        return report(std::move(error));
    }
    std::optional<TextureError> first_failure;
    for (const auto extension : { ".fsh", ".tga" }) {
        const auto candidate = joined(source.relative_namespace, with_extension(filename, extension));
        const auto entry = locate(source, candidate);
        if (!entry) {
            continue;
        }
        const auto bytes = entry->first->read_resource(entry->second);
        if (!bytes) {
            auto error = resource_error(bytes.error(), entry->first->source.native_encoded_string() + "/" + entry->second);
            if (!first_failure) {
                first_failure = error;
            }
            (void)report(std::move(error));
            continue;
        }
        if (!*bytes) {
            continue;
        }
        auto decoded = extension == std::string_view { ".fsh" } ? decode_fsh_image(**bytes, TextureRowOrigin::bottom) : decode_tga_image(**bytes, TextureRowOrigin::bottom);
        if (!decoded) {
            auto error = decoded.error();
            error.resource = entry->first->source.native_encoded_string() + "/" + entry->second;
            if (!first_failure) {
                first_failure = error;
            }
            (void)report(std::move(error));
            //6D9040 tries the original TGA if its FSH reader could not load.
            continue;
        }
        TextureImagePtr result = std::make_shared<TextureImage>(std::move(*decoded));
        cache_.emplace(key, result);
        return result;
    }
    auto failure = first_failure.value_or(TextureError { TextureFailure::missing, 0, resource });
    cache_.emplace(key, std::unexpected(failure));
    return report(std::move(failure));
}

std::expected<TextureImagePtr, TextureError> MeshTextureLibrary::load_qualified(std::string_view raw) {
    const auto name = selected_filename(raw);
    if (const auto found = qualified_sources_.find(name); found != qualified_sources_.end()) {
        return load_from(found->second.first, found->second.second);
    }
    auto path = std::filesystem::path { name };
    if (!path.is_absolute() && !name.starts_with("data/")) {
        //Relative catalog members are rooted at the owning archive, with their
        //explicit PathTex/GTexture namespace preserved after quality selection.
        const auto selected = texture_namespace(name, texture_set_);
        for (std::size_t index = 0; index < sources_.size(); ++index) {
            if (sources_[index].library == sources_.front().library && sources_[index].root == sources_.front().root && sources_[index].relative_namespace.empty()) {
                qualified_sources_.emplace(name, std::pair { index, selected });
                return load_from(index, selected);
            }
        }
        auto root = sources_.front();
        root.relative_namespace.clear();
        const auto index = sources_.size();
        sources_.push_back(std::move(root));
        qualified_sources_.emplace(name, std::pair { index, selected });
        return load_from(index, selected);
    }
    if (path.is_relative()) {
        path = game_root_ / path;
    }
    path = path.lexically_normal();
    //Fully qualified native paths (Sky, explicit MiscMesh and fixed generated
    //fallbacks) retain their exact directory spelling, independent of quality.
    //A WAD acts as a virtual directory at the longest matching path prefix.
    auto directory = path.parent_path();
    //Locate the mounted namespace before opening providers, rather than
    //treating an existing deeper loose directory as an exhaustive source.
    for (auto candidate = directory; !candidate.empty();) {
        auto archive = candidate;
        archive += ".wad";
        if (std::filesystem::is_regular_file(archive)) {
            directory = candidate;
            break;
        }
        const auto parent = candidate.parent_path();
        if (parent == candidate) {
            break;
        }
        candidate = parent;
    }
    if (std::filesystem::is_directory(directory) || std::filesystem::is_regular_file(std::filesystem::path { directory.native_encoded_string() + ".wad" })) {
        const auto relative = path.lexically_relative(directory).native_encoded_string();
        for (std::size_t index = 0; index < sources_.size(); ++index) {
            if (sources_[index].root.lexically_normal() == directory.lexically_normal() && sources_[index].relative_namespace.empty()) {
                qualified_sources_.emplace(name, std::pair { index, relative });
                return load_from(index, relative);
            }
        }
        const auto index = sources_.size();
        sources_.push_back(open_source({ nullptr, directory, {} }));
        qualified_sources_.emplace(name, std::pair { index, relative });
        return load_from(index, relative);
    }
    return report({ TextureFailure::missing, 0, std::string { raw } });
}

std::expected<TextureImagePtr, TextureError> MeshTextureLibrary::load(const MeshTextureSlot& slot) {
    if (slot.image_override) {
        return slot.image_override;
    }
    //6DA8D0's0x80 branch allocates a private render target directly and never
    //acquires a filename. CPU-generated signs supply the owned override above.
    if (slot.flags & 0x80u) {
        return TextureImagePtr {};
    }
    if (slot.filenames.empty() && slot.filename.empty()) {
        return TextureImagePtr {};
    }
    const auto& first = slot.filenames.empty() ? slot.filename : slot.filenames.front();
    const std::size_t variant = animated_textures_ ? slot.source_variant : 0u;
    if (!slot.filenames.empty() && variant >= slot.filenames.size()) {
        return report({ TextureFailure::bounds, variant, first });
    }
    const auto& current = slot.filenames.empty() ? slot.filename : slot.filenames[variant];
    //Runtime terrain catalogs and generated hoardings retain their explicit
    //native source directory in the logical name. They are not relative files
    //inside the ordinary model's texture folder.
    const auto qualified = selected_filename(first);
    if (qualified.starts_with("data/") || std::filesystem::path { qualified }.is_absolute() || qualified.starts_with("pathtex/") || qualified.starts_with("spathtex/")) {
        return load(current);
    }
    const auto source = select_source(first);
    if (!source) {
        return report(source.error());
    }
    return load_from(*source, current);
}

std::expected<TextureImagePtr, TextureError> MeshTextureLibrary::load(std::string_view raw) {
    if (raw.empty()) {
        return TextureImagePtr {};
    }
    const auto name = selected_filename(raw);
    if (name.find('/') != std::string::npos) {
        const auto selected = texture_namespace(name, texture_set_);
        const auto& prefix = sources_.front().relative_namespace;
        if (!prefix.empty() && selected.starts_with(prefix + "/")) {
            //Generated TrackMaster callers already qualify the selected GTexture
            //folder. Keep its explicit secondary policy without appending it twice.
            const auto local = selected.substr(prefix.size() + 1);
            const auto source = select_source(local);
            if (!source) {
                return report(source.error());
            }
            return load_from(*source, local);
        }
        return load_qualified(name);
    }
    const auto source = select_source(name);
    if (!source) {
        return report(source.error());
    }
    return load_from(*source, name);
}

std::expected<TextureImagePtr, TextureError> MeshTextureLibrary::missing_texture() {
    if (!missing_texture_) {
        auto fallback = load_qualified("data/generic/defaulttexture/notfound.tga");
        //6D8CA0 installs the entry even when creating its source returns NULL.
        //Preserve that nullable result and the actual failure in diagnostics.
        missing_texture_ = fallback ? std::move(fallback) : std::expected<TextureImagePtr, TextureError> { TextureImagePtr {} };
    }
    return *missing_texture_;
}

std::size_t MeshTextureLibrary::loaded_count() const {
    return static_cast<std::size_t>(std::ranges::count_if(cache_, [](const auto& entry) {
        return entry.second.has_value() && static_cast<bool>(*entry.second);
    }));
}
} //namespace simcoaster
