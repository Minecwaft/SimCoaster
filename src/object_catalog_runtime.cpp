#include "simcoaster/object_catalog.hpp"
#include "simcoaster/resource_key.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_research.hpp"

namespace simcoaster {
std::uint32_t load_object_definition_preview_mesh(WorldRuntime& c, ObjectDefinitionAsset& asset) {
    const auto& info = asset.balance.Info;
    if (!info.Shape)
        throw std::invalid_argument("object preview mesh requires a Shape");
    auto directory = std::filesystem::path{ asset.source_path };
    const bool packed = ascii_lowercase(directory.extension().native_encoded_string()) == ".wad";
    if (packed)
        directory.replace_extension();
    auto directory_text = directory.native_encoded_string();
    if (packed && !directory_text.ends_with('\\'))
        directory_text += '\\';
    std::uint32_t options;
    if (info.DontApplyOffset)
        options = info.DoHeadProcessing ? 4525056u : 330752u;
    else {
        options = info.DoHeadProcessing ? 4924585u : 730281u;
        if (asset.balance.Prebuilt.IsPrebuiltCoaster)
            options &= ~1u;
    }
    const auto result = world_operations::load_and_register_mesh_definition_template(
        c,
        { directory_text,
          asset.resource_name,
          info.Shape.get(),
          info.Hoarding.get(),
          options,
          asset.runtime.id,
          static_cast<std::uint32_t>(asset.balance.UsageInfo.NumSimultAnims),
          nullptr,
          nullptr,
          info.DontApplyOffset ? static_cast<std::uint32_t>(info.EngineFootprintWidthOverride) : 0u,
          info.DontApplyOffset ? static_cast<std::uint32_t>(info.EngineFootprintHeightOverride) : 0u });
    asset.runtime.preview_mesh_template_id = result;
    asset.runtime.preview_mesh_template_loaded = true;
    asset.runtime.mesh_load_failed = result ? 0u : 1u;
    return result;
}

void load_object_definition_supplemental_meshes(WorldRuntime& c, ObjectDefinitionAsset& asset) {
    auto& runtime = asset.runtime;
    if (!runtime.preview_mesh_template_loaded)
        throw std::logic_error("object preview mesh is not loaded");
    if (runtime.mesh_load_failed)
        return;
    const auto count = asset.balance.SupplementalMeshesCount;
    if (count > asset.balance.SupplementalMeshes.size())
        throw std::out_of_range("supplemental mesh count");
    auto directory = std::filesystem::path{ asset.source_path };
    const bool packed = ascii_lowercase(directory.extension().native_encoded_string()) == ".wad";
    if (packed)
        directory.replace_extension();
    auto directory_text = directory.native_encoded_string();
    if (packed && !directory_text.ends_with('\\'))
        directory_text += '\\';
    runtime.additional_mesh_template_count = count;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& filename = asset.balance.SupplementalMeshes[i].FileName;
        if (!filename)
            throw std::invalid_argument("supplemental mesh filename is missing");
        runtime.additional_mesh_template_ids[i] = world_operations::load_and_register_mesh_definition_template(
            c,
            { directory_text,
              *filename,
              nullptr,
              nullptr,
              192u,
              static_cast<std::uint32_t>(runtime.id + i + 1),
              1u,
              nullptr,
              nullptr,
              0u,
              0u });
    }
}

std::optional<RsseScriptSource> resolve_saved_rsse_script_directory(const WorldRuntime& c,
                                                                    std::string_view saved_directory) {
    const auto normalize = [](std::string_view text) {
        auto value = ascii_lowercase(text);
        for (auto& ch : value)
            if (ch == '/')
                ch = '\\';
        while (!value.empty() && value.back() == '\\')
            value.pop_back();
        return value;
    };
    const auto saved = normalize(saved_directory);
    //New native-x64 scripts may carry an absolute source path. Retain that
    //path across save/load, including a virtual directory backed by a WAD.
    const auto direct = std::filesystem::path{ saved_directory };
    if (direct.is_absolute()) {
        if (std::filesystem::is_directory(direct))
            return RsseScriptSource{ direct / "_restored_.rse", {} };
        auto archive = direct;
        if (archive.filename().empty())
            archive = archive.parent_path();
        archive += ".wad";
        if (std::filesystem::is_regular_file(archive))
            return RsseScriptSource{ archive, "_restored_.rse" };
    }
    //Catalog paths retain the real loose directory or WAD, while saves record
    //the virtual directory with the archive extension removed.
    for (std::size_t i = 0; i < c.resources.object_catalog.count; ++i)
        if (const auto* definition = c.resources.object_catalog.entries[i]) {
            const auto* asset = definition->source_asset;
            if (!asset)
                continue;
            auto path = std::filesystem::path{ asset->source_path };
            const bool packed = ascii_lowercase(path.extension().native_encoded_string()) == ".wad";
            auto virtual_path = path;
            if (packed)
                virtual_path.replace_extension();
            const auto relative = virtual_path.lexically_relative(asset->game_root);
            if (normalize(relative.generic_native_encoded_string()) != saved)
                continue;
            if (packed)
                return RsseScriptSource{ path, "_restored_.rse" };
            return RsseScriptSource{ path / "_restored_.rse", {} };
        }
    //Shared scripts (for example vegetation) are outside an object directory.
    if (saved == "data" || saved.starts_with("data\\")) {
        const auto suffix = saved_directory.size() > 5 ? saved_directory.substr(5) : std::string_view{};
        const auto directory = std::filesystem::path{ c.source.data_directory } / std::filesystem::path{ suffix };
        if (std::filesystem::is_directory(directory))
            return RsseScriptSource{ directory / "_restored_.rse", {} };
        auto archive = directory;
        if (archive.filename().empty())
            archive = archive.parent_path();
        archive += ".wad";
        if (std::filesystem::is_regular_file(archive))
            return RsseScriptSource{ archive, "_restored_.rse" };
    }
    return std::nullopt;
}

std::expected<RsseScriptState*, ObjectScriptError>
create_installed_park_object_rsse_state(WorldRuntime& c, ParkObjectRuntimeView& object) {
    if (!c.scripts.states.active) {
        object.script_id = 0;
        return nullptr;
    }
    const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, object.definition_id);
    if (!definition || !definition->source_asset)
        return std::unexpected(ObjectScriptError{ {}, ObjectScriptFailure::missing_definition });
    const bool vegetation =
        (std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::requires_watering)) != 0;
    auto loaded = load_installed_object_script(*definition->source_asset, vegetation);
    if (!loaded)
        return std::unexpected(loaded.error());
    auto* state = create_rsse_program_state_for_owner(c, std::move(loaded->program), object.base.header.id);
    object.script_id = state ? state->id : 0;
    return state;
}
} //namespace simcoaster
