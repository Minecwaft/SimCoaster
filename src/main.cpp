#include "simcoaster/park_profiler.hpp"
#include "simcoaster/save_container.hpp"
#include "simcoaster/resource_archive.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/sprite_catalog.hpp"
#include "simcoaster/mesh_model.hpp"
#include "simcoaster/mesh_renderer.hpp"
#include "simcoaster/park_runtime.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

#if defined(SIMCOASTER_GUI_EXECUTABLE)
#    define WIN32_LEAN_AND_MEAN
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <cstdio>
#endif

namespace {
#if defined(SIMCOASTER_GUI_EXECUTABLE)
void attach_command_line_console() {
    struct Output {
        FILE* stream;
        DWORD identifier;
        HANDLE inherited;
        bool redirected;
    };

    const auto output = [](FILE* stream, DWORD identifier) {
        const auto handle = GetStdHandle(identifier);
        const auto type = GetFileType(handle);
        DWORD mode {};
        const bool redirected = type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK || (type == FILE_TYPE_CHAR && !GetConsoleMode(handle, &mode));
        return Output { stream, identifier, handle, redirected };
    };
    const std::array outputs { output(stdout, STD_OUTPUT_HANDLE), output(stderr, STD_ERROR_HANDLE) };
    //Use an existing terminal for CLI commands. Explorer launches never create
    //a console, and inherited output pipes/files keep their original handles.
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }
    for (const auto& target : outputs) {
        if (target.redirected) {
            (void)SetStdHandle(target.identifier, target.inherited);
        } else {
            (void)std::freopen("CONOUT$", "w", target.stream);
        }
    }
}
#endif

std::optional<std::string> local_version_stamp(const std::filesystem::path& root) {
    std::ifstream stream { root / "Data" / "Ver.txt", std::ios::binary };
    if (!stream) {
        return std::nullopt;
    }
    std::string line;
    char c {};
    while (line.size() < 127 && stream.get(c)) {
        line.push_back(c);
        if (c == '\n') {
            break;
        }
    }
    return line;
}
} //namespace

int main(int argc, char** argv) {
#if defined(SIMCOASTER_GUI_EXECUTABLE)
    if (argc > 1) {
        attach_command_line_console();
    }
#endif
    using namespace simcoaster;
    if (argc >= 2 && std::string_view { argv[1] } == "profile-park") {
        try {
            std::vector<std::string_view> arguments;
            for (int i = 2; i < argc; ++i) {
                arguments.emplace_back(argv[i]);
            }
            return run_park_profiler(arguments);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }

    if (argc == 1 || (argc >= 2 && std::string_view { argv[1] } == "play")) {
        if (argc > 3) {
            std::cerr << "Usage: simcoaster_runtime play [game-root]\n";
            return 2;
        }
        try {
            return run_application(argc == 3 ? std::filesystem::path { argv[2] } : std::filesystem::path {});
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc >= 2 && std::string_view { argv[1] } == "play-park") {
        if (argc < 4 || argc > 5) {
            std::cerr << "Usage: simcoaster_runtime play-park <theme> <park.TPWS> [game-root]\n";
            return 2;
        }
        try {
            return run_application(argc == 5 ? std::filesystem::path { argv[4] } : std::filesystem::path {}, std::filesystem::path { argv[3] }, argv[2]);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc >= 2 && (std::string_view { argv[1] } == "view-park" || std::string_view { argv[1] } == "render-park")) {
        const bool capture = std::string_view { argv[1] } == "render-park";
        const int required = capture ? 5 : 4;
        if (argc < required || argc > required + 1) {
            std::cerr << "Usage: simcoaster_runtime view-park <theme> <park.TPWS> [game-root]\n       simcoaster_runtime render-park <theme> <park.TPWS> <output.png> [game-root]\n";
            return 2;
        }
        try {
            return run_park_viewer(argv[3], argv[2], argc == required + 1 ? std::filesystem::path { argv[required] } : std::filesystem::current_path(), capture ? std::optional { std::filesystem::path { argv[4] } } : std::nullopt);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc >= 2 && (std::string_view { argv[1] } == "view-terrain" || std::string_view { argv[1] } == "render-terrain")) {
        const bool capture = std::string_view { argv[1] } == "render-terrain";
        const int required = capture ? 4 : 3;
        if (argc < required || argc > required + 1) {
            std::cerr << "Usage: simcoaster_runtime view-terrain <theme> [game-root]\n       simcoaster_runtime render-terrain <theme> <output.png> [game-root]\n";
            return 2;
        }
        const auto root = argc == required + 1 ? std::filesystem::path { argv[required] } : std::filesystem::current_path();
        try {
            return run_mesh_viewer(root / "Data/levels" / argv[2] / "terrain.wad", "base", root, capture ? std::optional { std::filesystem::path { argv[3] } } : std::nullopt);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc >= 2 && (std::string_view { argv[1] } == "view-model" || std::string_view { argv[1] } == "render-model")) {
        const bool capture = std::string_view { argv[1] } == "render-model";
        const int required = capture ? 5 : 4;
        if (argc < required || argc > required + 1) {
            std::cerr << "Usage: simcoaster_runtime view-model <archive.wad|directory> <model> [game-root]\n       simcoaster_runtime render-model <archive.wad|directory> <model> <output.png> [game-root]\n";
            return 2;
        }
        try {
            return run_mesh_viewer(argv[2], argv[3], argc == required + 1 ? std::filesystem::path { argv[required] } : std::filesystem::current_path(), capture ? std::optional { std::filesystem::path { argv[4] } } : std::nullopt);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    const bool rewrite = argc >= 2 && std::string_view { argv[1] } == "rewrite-save";
    const bool archive = argc >= 2 && std::string_view { argv[1] } == "inspect-archive";
    const bool catalog = argc >= 2 && std::string_view { argv[1] } == "inspect-catalog";
    const bool sprites = argc >= 2 && std::string_view { argv[1] } == "inspect-sprites";
    const bool model = argc >= 2 && std::string_view { argv[1] } == "inspect-model";
    if (argc < 3 || ((catalog || sprites) ? argc > 5 : (model ? argc > 4 : (archive ? argc != 3 : (rewrite ? (argc < 4 || argc > 5) : (argc > 4 || std::string_view { argv[1] } != "inspect-save")))))) {
        std::cerr
            << "Usage: simcoaster_runtime inspect-save <park.TPWS|park.TPWI> [game-directory]\n"
            << "       simcoaster_runtime play [game-root]\n"
            << "       simcoaster_runtime play-park <theme> <park.TPWS> [game-root]\n"
            << "       simcoaster_runtime profile-park <theme> <park.TPWS> [frames] [game-root]\n"
            << "       simcoaster_runtime rewrite-save <input> <output> [game-directory]\n"
            << "       simcoaster_runtime inspect-archive <archive.wad>\n"
            << "       simcoaster_runtime inspect-model <model.md2|archive.wad> [resource.md2]\n"
            << "       simcoaster_runtime inspect-catalog <theme> [game-directory] [standard|online|easy]\n"
            << "       simcoaster_runtime inspect-sprites <theme> [game-directory] [low|medium|high|max]\n"
            << "       simcoaster_runtime view-model <archive.wad|directory> <model> [game-root]\n"
            << "       simcoaster_runtime render-model <archive.wad|directory> <model> <output.png> [game-root]\n"
            << "       simcoaster_runtime view-terrain <theme> [game-root]\n"
            << "       simcoaster_runtime render-terrain <theme> <output.png> [game-root]\n"
            << "       simcoaster_runtime view-park <theme> <park.TPWS> [game-root]\n"
            << "       simcoaster_runtime render-park <theme> <park.TPWS> <output.png> [game-root]\n";
        return 2;
    }
    try {
        if (model) {
            auto loaded = do {
                if (argc == 3) {
                    do_return load_mesh_model(argv[2]);
                }
                auto library = open_mesh_model_library(argv[2]);
                if (!library) {
                    do_return std::expected<MeshModel, MeshError> { std::unexpected(library.error()) };
                }
                const auto id = library->load(1, argv[3]);
                if (!id) {
                    do_return std::expected<MeshModel, MeshError> { std::unexpected(id.error()) };
                }
                do_return std::expected<MeshModel, MeshError> { std::move(library->templates[*id - 1].model) };
            };
            if (!loaded) {
                std::cerr << describe_mesh_error(loaded.error()) << '\n';
                return 1;
            }
            std::size_t vertices = 0, triangles = 0;
            for (const auto& node : loaded->nodes) {
                if (node.drawable) {
                    vertices += node.drawable->positions.size();
                    triangles += node.drawable->triangles.size();
                }
            }
            std::cout
                << "Loaded model "
                << loaded->name
                << "\nNodes: "
                << loaded->nodes.size()
                << "\nDrawable nodes: "
                << loaded->drawable_node_count
                << "\nPositions: "
                << vertices
                << "\nTriangles: "
                << triangles
                << "\nTextures: "
                << loaded->textures.size()
                << "\nResource records: "
                << loaded->resources.size()
                << "\nHead attachments: "
                << loaded->resource_indices(std::to_underlying(MeshResourceFlag::head)).size()
                << "\nTrack attachments: "
                << loaded->resource_indices(std::to_underlying(MeshResourceFlag::track_attachment)).size()
                << '\n';
            return 0;
        }
        if (sprites) {
            SpriteCatalogSettings settings;
            if (argc == 5) {
                const std::string_view profile { argv[4] };
                if (profile == "low") {
                    settings.num_kids = 0;
                } else if (profile == "medium") {
                    settings.num_kids = 1;
                } else if (profile == "high") {
                    settings.num_kids = 2;
                } else if (profile == "max") {
                    settings.num_kids = 3;
                } else {
                    std::cerr << "Unknown sprite profile: " << profile << '\n';
                    return 2;
                }
            }
            const auto loaded = load_installed_sprite_catalog(argc >= 4 ? std::filesystem::path { argv[3] } : std::filesystem::current_path(), argv[2], settings);
            if (!loaded) {
                std::cerr << describe_sprite_catalog_error(loaded.error()) << '\n';
                return 1;
            }
            std::cout << "Loaded sprite assets for " << argv[2] << "\nResources: " << loaded->resources.size() << '\n';
            for (const auto& range : loaded->variants) {
                std::cout << range.category_name << ": " << range.loaded_resource_count << " starting at " << range.first_loaded_resource_index << '\n';
            }
            std::cout << "Missing pixel sidecars: " << loaded->missing_sidecars.size() << '\n';
            return 0;
        }
        if (catalog) {
            CatalogMode mode = CatalogMode::standard;
            if (argc == 5) {
                const std::string_view text { argv[4] };
                if (text == "online") {
                    mode = CatalogMode::online;
                } else if (text == "easy") {
                    mode = CatalogMode::easy;
                } else if (text != "standard") {
                    std::cerr << "Unknown catalog mode: " << text << '\n';
                    return 2;
                }
            }
            const auto loaded = load_installed_object_catalog(argc >= 4 ? std::filesystem::path { argv[3] } : std::filesystem::current_path(), argv[2], mode);
            if (!loaded) {
                std::cerr << describe_catalog_error(loaded.error()) << '\n';
                return 1;
            }
            std::size_t special = 0;
            for (const auto& definition : loaded->definitions) {
                special += definition->balance.Info.IsSpecial == 1;
            }
            std::cout << "Loaded object catalog for " << argv[2] << "\nDefinitions: " << loaded->definitions.size() << "\nSpecial rides: " << special << '\n';
            return 0;
        }
        if (archive) {
            const auto file = load_dwfb_archive(argv[2]);
            if (!file) {
                std::cerr << asset_failure_name(file.error().reason) << " at file byte " << file.error().offset << '\n';
                return 1;
            }
            std::size_t decoded_bytes = 0, definitions = 0, scripts = 0;
            for (std::size_t i = 0; i < file->entries().size(); ++i) {
                const auto data = file->read_entry(i);
                if (!data) {
                    std::cerr << file->entries()[i].path << ": " << asset_failure_name(data.error().reason) << " at file byte " << data.error().offset << '\n';
                    return 1;
                }
                decoded_bytes += data->size();
                definitions += file->entries()[i].path.ends_with(".sam");
                scripts += file->entries()[i].path.ends_with(".rse");
            }
            std::cout << "Loaded " << argv[2] << "\nArchive version: " << file->header.version << "\nEntries: " << file->entries().size() << "\nDecoded entry bytes: " << decoded_bytes << "\nSAM files: " << definitions << "\nRSSE files: " << scripts << '\n';
            return 0;
        }
        const int root_argument = rewrite ? 4 : 3;
        const auto stamp = local_version_stamp(argc > root_argument ? std::filesystem::path { argv[root_argument] } : std::filesystem::current_path());
        SaveCompatibility compatibility;
        if (stamp) {
            compatibility.local_version_stamp = *stamp;
        }
        auto save = load_park_save_file(argv[2], compatibility);
        if (!save) {
            std::cerr << save_container_failure_name(save.error().reason) << " at file byte " << save.error().offset << '\n';
            return 1;
        }
        if (rewrite) {
            if (std::filesystem::exists(argv[3]) && std::filesystem::equivalent(argv[2], argv[3])) {
                std::cerr << "Input and output paths must differ\n";
                return 2;
            }
            const auto result = write_park_save_file(argv[3], save->header, save->sections);
            if (!result) {
                std::cerr << save_container_failure_name(result.error().reason) << '\n';
                return 1;
            }
            std::cout << "Rewrote compressed save container: " << argv[3] << '\n';
            return 0;
        }
        auto prefix = read_park_save_body_prefix(save->sections);
        if (!prefix) {
            std::cerr << save_container_failure_name(prefix.error().reason) << " at section byte " << prefix.error().offset << '\n';
            return 1;
        }
        std::cout << "Loaded " << argv[2] << "\nSave version: " << save->header.version << "\nDecoded section bytes: " << save->sections.size() << "\nAction-recording bytes: " << prefix->action_recording.size() << '\n';
        if (prefix->world_and_later_sections.size() >= 4) {
            std::uint32_t version {};
            save::detail::decode<std::uint32_t>(std::span<const std::byte, 4> { prefix->world_and_later_sections.data(), 4 }, version);
            std::cout << "World section version: " << version << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
