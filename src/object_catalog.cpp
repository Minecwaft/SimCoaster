#include "simcoaster/object_catalog.hpp"
#include "simcoaster/coaster_mesh.hpp"
#include "simcoaster/resource_key.hpp"
#include <format>
#include <fstream>

namespace simcoaster {
namespace {
std::string path_text(const std::filesystem::path& path) {
    return path.generic_native_encoded_string();
}

std::optional<std::filesystem::path> child(const std::filesystem::path& directory, std::string_view name) {
    if (!std::filesystem::is_directory(directory)) {
        return std::nullopt;
    }
    const auto key = ascii_lowercase(name);
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (ascii_lowercase(entry.path().filename().native_encoded_string()) == key) {
            return entry.path();
        }
    }
    return std::nullopt;
}

std::expected<std::vector<std::byte>, ObjectCatalogError> read_file(const std::filesystem::path& path) {
    std::ifstream stream { path, std::ios::binary | std::ios::ate };
    const auto io = [&] {
        return std::unexpected(ObjectCatalogError { path_text(path), CatalogFailure::io });
    };
    if (!stream) {
        return io();
    }
    const auto size = static_cast<std::streamoff>(stream.tellg());
    if (size < 0 || !std::in_range<std::size_t>(size) || !std::in_range<std::streamsize>(size)) {
        return io();
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return io();
    }
    return bytes;
}

struct ObjectSource {
    std::filesystem::path path;
    std::optional<DwfbArchive> archive;

    std::string label(std::string_view name) const {
        return path_text(path) + (archive ? "*/" : "/") + std::string { name };
    }

    std::expected<std::optional<std::vector<std::byte>>, ObjectCatalogError> read(std::string_view name) const {
        if (archive) {
            const auto index = archive->find_entry(name);
            if (!index) {
                return std::optional<std::vector<std::byte>> {};
            }
            auto bytes = archive->read_entry(*index);
            if (!bytes) {
                return std::unexpected(ObjectCatalogError { label(name), bytes.error() });
            }
            return std::optional { std::move(*bytes) };
        }
        const auto file = child(path, name);
        if (!file) {
            return std::optional<std::vector<std::byte>> {};
        }
        auto bytes = read_file(*file);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        return std::optional { std::move(*bytes) };
    }
};

std::expected<bool, ObjectCatalogError> apply(ObjectDefinitionBalance& record, const ObjectSource& source, std::string_view name, bool preserve) {
    const auto bytes = source.read(name);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    if (!*bytes) {
        return false;
    }
    const auto& data = **bytes;
    if (data.empty()) {
        return true; //Native stream wrapper returns before parser/reset.
    }
    const auto result = sam::parse_record(record, std::string_view { reinterpret_cast<const char*>(data.data()), data.size() }, preserve);
    if (!result) {
        return std::unexpected(ObjectCatalogError { source.label(name), result.error() });
    }
    return true;
}

void make_runtime_definition(ObjectDefinitionAsset& asset) {
    const auto& balance = asset.balance;
    auto& target = asset.runtime;
    target.balance = &balance;
    target.resource_catalog_name = asset.resource_name;
    target.source_asset = &asset;
    //finalize_object_definition truncates Info.Id into the runtime WORD key.
    target.id = static_cast<std::uint16_t>(balance.Info.Id);
    target.destroy_particle_effect = static_cast<std::uint32_t>(balance.Info.DestroyParticleEffect);
    target.repair_particle_effect = static_cast<std::uint32_t>(balance.Info.RepairParticleEffect);
    target.is_photo_booth = balance.Info.IsPhotoBooth;
    target.which_track_type = balance.Bumper.WhichTrackType;
    target.usage_fatigue_effect = static_cast<std::uint32_t>(balance.UsageInfo.FatigueEffect);
    target.max_capacity = static_cast<std::uint32_t>(balance.UsageInfo.MaxCapacity);
    target.max_speed = static_cast<std::uint32_t>(balance.UsageInfo.MaxSpeed);
    target.usage_rip_off_ok = static_cast<std::uint32_t>(balance.UsageInfo.RipOffOK);
    auto& research = target.research;
    research.linked_with = static_cast<std::uint32_t>(balance.Research.LinkedWith);
    research.group = balance.Research.Group;
    research.category = static_cast<ResearchCategory>(balance.Research.Category);
    research.type = balance.Research.Type;
    research.staff_count = balance.RStaffCount;
    for (std::size_t i = 0; i < balance.RStaff.size(); ++i) {
        research.staff[i] = { static_cast<ResearchStaffRequirement>(balance.RStaff[i].SType), static_cast<ThemeAbility>(balance.RStaff[i].SA) };
    }
    research.upgrade_count = balance.UpgradesCount;
    for (std::size_t i = 0; i < balance.Upgrades.size(); ++i) {
        const auto& row = balance.Upgrades[i];
        auto& upgrade = research.upgrades[i];
        upgrade.cost_of_research = row.CostOfResearch;
        upgrade.cash_per_brain_point = row.CashPerBrainPoint;
        upgrade.estimated_price_variance = static_cast<std::uint32_t>(row.EstimatedPriceVariance);
        upgrade.scrap_value_year1 = static_cast<std::uint32_t>(row.ScrapValueYear1);
        upgrade.scrap_value_year2 = static_cast<std::uint32_t>(row.ScrapValueYear2);
        upgrade.scrap_value_year3 = static_cast<std::uint32_t>(row.ScrapValueYear3);
        upgrade.scrap_value_year4 = static_cast<std::uint32_t>(row.ScrapValueYear4);
        upgrade.initial_capacity = static_cast<std::uint32_t>(row.InitCapacity);
        upgrade.redline_capacity = static_cast<std::uint32_t>(row.RedLineCapacity);
        upgrade.initial_duration = static_cast<std::uint32_t>(row.InitDuration);
        upgrade.redline_duration = static_cast<std::uint32_t>(row.RedLineDuration);
        upgrade.initial_speed = static_cast<std::uint32_t>(row.InitSpeed);
        upgrade.redline_speed = static_cast<std::uint32_t>(row.RedLineSpeed);
        upgrade.wear_rate = static_cast<std::uint32_t>(row.WearRate);
        upgrade.queue_wait_time_constant = row.QueueWaitTimeConstant;
        upgrade.cost_of_upgrade = static_cast<std::uint32_t>(row.CostOfUpgrade);
    }
    for (std::size_t i = 0; i < balance.SkillSet.size(); ++i) {
        research.skill_set_counts[i] = static_cast<std::uint32_t>(balance.SkillSet[i].Num);
    }
    research.skill_set_count = balance.SkillSetCount;
    research.breakdown_imminent = balance.RideBreakDown.Imminent;
    research.breakdown_warn_player = balance.RideBreakDown.WarnPlayer;
    target.which_ui_type = static_cast<ObjectUiCategory>(balance.Info.WhichUIType);
    target.add_on = { static_cast<std::uint32_t>(balance.AddOn.UpgradesId), balance.AddOn.UpgradeType, balance.AddOn.UpgradesAnyRide };
    target.is_special = balance.Info.IsSpecial;
    target.opened_automatically = balance.Obstacle.OpenedAutomaticly;
    target.gate_to = balance.Info.GateTo;
    target.after_shape = balance.Obstacle.AfterShape.get();
    target.shape_offset_x = static_cast<std::uint32_t>(balance.Obstacle.ShapeOffsetX);
    target.shape_offset_y = static_cast<std::uint32_t>(balance.Obstacle.ShapeOffsetY);
    target.delete_after_use = balance.Obstacle.DeleteAfterUse;
    target.replace_with_id = static_cast<std::uint32_t>(balance.Obstacle.ReplaceWithID);
    target.cached_entry_direction = 1;
    target.requires_teleport = balance.UsageInfo.RequiresTeleport;
    target.appearance_effect = static_cast<PersonAppearanceEffect>(balance.UsageInfo.AppearanceEffect);
    target.attraction_value = balance.Info.AttractionValue;
    target.new_attraction_decay_time = balance.Info.NewAttractionDecayTime;
    for (std::size_t i = 0; i < 3; ++i) {
        target.attraction_new_bonus[i] = balance.Attraction[i].NewBonus;
    }
    target.cached_map_offset = { balance.Info.MapOffsetX, balance.Info.MapOffsetY };
    finalize_object_definition_markers(target, *balance.Info.Shape);
}
} //namespace

void finalize_object_definition_markers(ResearchCatalogDefinition& target, const DefinitionGrid& shape) {
    target.cached_entry_position = { 0, 0 };
    target.cached_exit_position = { 0, 0 };
    target.cached_entry_direction = 1;
    target.cached_exit_direction = 16;
    for (std::int32_t x = 0; x < shape.width; ++x) {
        for (std::int32_t y = 0; y < shape.height; ++y) {
            const auto& cell = shape.cells.at(static_cast<std::size_t>(y * 40 + x));
            if (cell.code != 9) {
                continue;
            }
            target.cached_entry_position = { x, y };
            target.cached_exit_position = { x, y };
            target.cached_entry_direction = cell.attribute;
            target.cached_exit_direction = cell.attribute;
            for (std::int32_t exit_x = 0; exit_x < shape.width; ++exit_x) {
                for (std::int32_t exit_y = 0; exit_y < shape.height; ++exit_y) {
                    const auto& exit = shape.cells.at(static_cast<std::size_t>(exit_y * 40 + exit_x));
                    if (exit.code == 10) {
                        target.cached_exit_position = { exit_x, exit_y };
                        target.cached_exit_direction = exit.attribute;
                        return;
                    }
                }
            }
            return;
        }
    }
}

std::expected<std::optional<DynamicTextureMeshBalance>, ObjectCatalogError> load_dynamic_texture_mesh_balance(const ObjectDefinitionAsset& asset) {
    ObjectSource source { asset.source_path, {} };
    if (ascii_lowercase(source.path.extension().native_encoded_string()) == ".wad" && std::filesystem::is_regular_file(source.path)) {
        auto archive = load_dwfb_archive(source.path);
        if (!archive) {
            return std::unexpected(ObjectCatalogError { path_text(source.path), archive.error() });
        }
        source.archive = std::move(*archive);
    }
    auto bytes = source.read("Coaster.sam");
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    if (!*bytes) {
        return std::optional<DynamicTextureMeshBalance> {};
    }
    DynamicTextureMeshBalance balance {};
    const auto& data = **bytes;
    const auto parsed = sam::parse_record<DynamicTextureMeshBalance, true>(balance, std::string_view { reinterpret_cast<const char*>(data.data()), data.size() });
    if (!parsed) {
        return std::unexpected(ObjectCatalogError { source.label("Coaster.sam"), parsed.error() });
    }
    auto mesh = parse_coaster_mesh_definition({ reinterpret_cast<const char*>(data.data()), data.size() });
    if (!mesh) {
        return std::unexpected(ObjectCatalogError { source.label("Coaster.sam"), mesh.error() });
    }
    balance.mesh_definition = std::make_shared<CoasterMeshDefinition>(std::move(*mesh));
    return std::optional { std::move(balance) };
}

std::expected<LoadedObjectScript, ObjectScriptError> load_installed_object_script(const ObjectDefinitionAsset& asset, bool requires_watering) {
    auto source_error = [](const ObjectCatalogError& error) -> ObjectScriptError {
        if (const auto* cause = std::get_if<AssetError>(&error.cause)) {
            return { error.path, *cause };
        }
        return { error.path, ObjectScriptFailure::io };
    };
    std::optional<std::vector<std::byte>> bytes;
    std::string selected_path;
    RsseScriptSource origin;
    if (requires_watering) {
        const auto path = asset.game_root / "Data" / "levels" / "vegetation.rse";
        selected_path = path_text(path);
        origin.path = path;
        if (std::filesystem::is_regular_file(path)) {
            auto loaded = read_file(path);
            if (!loaded) {
                return std::unexpected(source_error(loaded.error()));
            }
            bytes = std::move(*loaded);
        }
    } else {
        ObjectSource source { asset.source_path, {} };
        if (ascii_lowercase(source.path.extension().native_encoded_string()) == ".wad" && std::filesystem::is_regular_file(source.path)) {
            auto archive = load_dwfb_archive(source.path);
            if (!archive) {
                return std::unexpected(ObjectScriptError { path_text(source.path), archive.error() });
            }
            source.archive = std::move(*archive);
        }
        const auto name = asset.resource_name + ".rse";
        selected_path = source.label(name);
        if (source.archive) {
            origin = { source.path, name };
        } else {
            origin = { source.path / name, {} };
        }
        auto loaded = source.read(name);
        if (!loaded) {
            return std::unexpected(source_error(loaded.error()));
        }
        bytes = std::move(*loaded);
    }
    const bool fallback = !bytes;
    if (fallback) {
        const auto data = asset.game_root / "Data";
        const auto directory = data / "TestScript";
        const auto loose = child(directory, "test.rse");
        if (loose) {
            selected_path = path_text(*loose);
            origin = { *loose, {} };
            auto loaded = read_file(*loose);
            if (!loaded) {
                return std::unexpected(source_error(loaded.error()));
            }
            bytes = std::move(*loaded);
        } else if (const auto archive_path = child(data, "TestScript.wad")) {
            auto archive = load_dwfb_archive(*archive_path);
            if (!archive) {
                return std::unexpected(ObjectScriptError { path_text(*archive_path), archive.error() });
            }
            ObjectSource source { *archive_path, std::move(*archive) };
            selected_path = source.label("test.rse");
            origin = { *archive_path, "test.rse" };
            auto loaded = source.read("test.rse");
            if (!loaded) {
                return std::unexpected(source_error(loaded.error()));
            }
            bytes = std::move(*loaded);
        } else {
            selected_path = path_text(directory / "test.rse");
        }
        if (!bytes) {
            return std::unexpected(ObjectScriptError { selected_path, ObjectScriptFailure::missing });
        }
    }
    auto program = parse_rsse_program(*bytes);
    if (!program) {
        return std::unexpected(ObjectScriptError { selected_path, program.error() });
    }
    program->source = std::move(origin);
    return LoadedObjectScript { std::move(*program), std::move(selected_path), fallback };
}

std::expected<RsseProgram, ObjectScriptError> load_relative_rsse_script(const RsseScriptSource& source, std::string_view relative_path) {
    std::vector<std::byte> bytes;
    RsseScriptSource next;
    std::string label;
    if (!source.archive_entry.empty()) {
        auto archive = load_dwfb_archive(source.path);
        if (!archive) {
            return std::unexpected(ObjectScriptError { path_text(source.path), archive.error() });
        }
        const auto slash = source.archive_entry.find_last_of("\\/");
        const auto name = (slash == std::string::npos ? std::string {} : source.archive_entry.substr(0, slash + 1)) + std::string { relative_path };
        label = path_text(source.path) + "*/" + name;
        const auto index = archive->find_entry(name);
        if (!index) {
            return std::unexpected(ObjectScriptError { label, ObjectScriptFailure::missing });
        }
        auto loaded = archive->read_entry(*index);
        if (!loaded) {
            return std::unexpected(ObjectScriptError { label, loaded.error() });
        }
        bytes = std::move(*loaded);
        next = { source.path, name };
    } else {
        const auto path = source.path.parent_path() / std::filesystem::path { relative_path };
        label = path_text(path);
        if (!std::filesystem::is_regular_file(path)) {
            return std::unexpected(ObjectScriptError { label, ObjectScriptFailure::missing });
        }
        auto loaded = read_file(path);
        if (!loaded) {
            return std::unexpected(ObjectScriptError { label, ObjectScriptFailure::io });
        }
        bytes = std::move(*loaded);
        next = { path, {} };
    }
    auto program = parse_rsse_program(bytes);
    if (!program) {
        return std::unexpected(ObjectScriptError { label, program.error() });
    }
    program->source = std::move(next);
    return std::move(*program);
}

std::string describe_catalog_error(const ObjectCatalogError& error) {
    const auto detail = match (error.cause) -> std::string {
        case { const MeshError& cause }  => describe_mesh_error(cause);
        case { const AssetError& cause } => std::format("{} at byte {}", asset_failure_name(cause.reason), cause.offset);
        case { const sam::Error& cause } => std::format("SAM parse failure {} at byte {}", std::to_underlying(cause.reason), cause.offset);
        case { const CatalogFailure& cause } =>
            match (cause) -> std::string {
                case CatalogFailure::io               => "file I/O failed";
                case CatalogFailure::missing_source   => "theme source directory is missing";
                case CatalogFailure::capacity         => "object catalog exceeds330 entries";
                case CatalogFailure::missing_shape    => "object definition has no Info.Shape";
                case CatalogFailure::category_archive => "category-level archive provider remains unported";
            };
    };
    return std::format("{}: {}", error.path, detail);
}

ObjectCatalogManager LoadedObjectCatalog::runtime_view() const {
    ObjectCatalogManager result {};
    for (const auto& asset : definitions) {
        result.entries.at(result.count++) = &asset->runtime;
    }
    if (shared_ride_description) {
        result.shared_ride_description = &*shared_ride_description;
    }
    return result;
}

std::expected<LoadedObjectCatalog, ObjectCatalogError> load_installed_object_catalog(const std::filesystem::path& game_root, std::string_view source, CatalogMode mode, const RideDescriptionBalance* previous_description, std::string_view configured_language) {
    LoadedObjectCatalog result;
    if (previous_description) {
        result.shared_ride_description = std::make_unique<RideDescriptionBalance>(*previous_description);
    }
    const auto levels = game_root / "Data" / "levels";
    const auto source_path = child(levels, source);
    if (!source_path || !std::filesystem::is_directory(*source_path)) {
        return std::unexpected(ObjectCatalogError { path_text(levels / std::string { source }), CatalogFailure::missing_source });
    }
    //reload_object_definition_catalog scans these roots in this exact order.
    for (std::string_view category : { "Rides", "Features", "Upgrades", "Shops", "Sideshow", "Facilities", "Obstacles" }) {
        if (const auto packed = child(*source_path, std::string { category } + ".wad")) {
            return std::unexpected(ObjectCatalogError { path_text(*packed), CatalogFailure::category_archive });
        }
        const auto directory = child(*source_path, category);
        if (!directory || !std::filesystem::is_directory(*directory)) {
            continue;
        }
        const ObjectSource defaults { *directory, {} };
        std::vector<std::string> names;
        for (const auto& entry : std::filesystem::directory_iterator(*directory)) {
            auto resource_name = entry.path().filename().native_encoded_string();
            if (resource_name.starts_with('.')) {
                continue;
            }
            const bool is_archive = entry.is_regular_file() && ascii_lowercase(entry.path().extension().native_encoded_string()) == ".wad";
            if (!entry.is_directory() && !is_archive) {
                continue;
            }
            ObjectSource object { entry.path(), {} };
            if (is_archive) {
                resource_name = entry.path().stem().native_encoded_string();
                auto archive = load_dwfb_archive(entry.path());
                if (!archive) {
                    return std::unexpected(ObjectCatalogError { path_text(entry.path()), archive.error() });
                }
                object.archive = std::move(*archive);
            }
            const auto primary_name = resource_name + ".sam";
            const auto primary = object.read(primary_name);
            if (!primary) {
                return std::unexpected(primary.error());
            }
            if (!*primary) {
                continue;
            }
            const auto key = ascii_lowercase(resource_name);
            if (std::ranges::find(names, key) != names.end()) {
                continue;
            }
            names.push_back(key);
            if (result.definitions.size() == 330) {
                return std::unexpected(ObjectCatalogError { path_text(entry.path()), CatalogFailure::capacity });
            }
            auto asset = std::make_unique<ObjectDefinitionAsset>();
            asset->category = category;
            asset->resource_name = resource_name;
            asset->source_path = path_text(entry.path());
            asset->game_root = game_root;
            sam::reset_record(asset->balance);
            //Base is non-preserving; online category, primary and mode-specific
            //object files preserve every unmentioned value. Easy has no base
            //overlay.
            const auto base = apply(asset->balance, defaults, std::string { category } + ".sam", false);
            if (!base) {
                return std::unexpected(base.error());
            }
            if (*base && mode == CatalogMode::online) {
                if (const auto parsed = apply(asset->balance, defaults, "Online_" + std::string { category } + ".sam", true); !parsed) {
                    return std::unexpected(parsed.error());
                }
            }
            const auto& bytes = **primary;
            const auto parsed = sam::parse_record(asset->balance, std::string_view { reinterpret_cast<const char*>(bytes.data()), bytes.size() }, true);
            if (!parsed) {
                return std::unexpected(ObjectCatalogError { object.label(primary_name), parsed.error() });
            }
            if (mode != CatalogMode::standard) {
                if (const auto overlay = apply(asset->balance, object, (mode == CatalogMode::online ? "Online_" : "Easy_") + primary_name, true); !overlay) {
                    return std::unexpected(overlay.error());
                }
            }
            if (!asset->balance.Info.Shape) {
                return std::unexpected(ObjectCatalogError { asset->source_path, CatalogFailure::missing_shape });
            }
            if (!asset->balance.Info.DontApplyOffset && asset->balance.Bumper.BumperType == -11) {
                const auto bumper = object.read("Bumper.sam");
                if (!bumper) {
                    return std::unexpected(bumper.error());
                }
                if (*bumper && !(**bumper).empty()) {
                    if (!result.shared_ride_description) {
                        result.shared_ride_description = std::make_unique<RideDescriptionBalance>();
                    }
                    const auto& data = **bumper;
                    const auto parsed_bumper = sam::parse_record(*result.shared_ride_description, std::string_view { reinterpret_cast<const char*>(data.data()), data.size() }, true);
                    if (!parsed_bumper) {
                        return std::unexpected(ObjectCatalogError { object.label("Bumper.sam"), parsed_bumper.error() });
                    }
                }
            }
            const auto text = object.read(std::string { configured_language } + ".txt");
            if (!text) {
                return std::unexpected(text.error());
            }
            if (*text) {
                asset->configured_text = parse_configured_object_definition_text(**text);
            }
            make_runtime_definition(*asset);
            result.definitions.push_back(std::move(asset));
        }
    }
    return result;
}

std::expected<LoadedObjectCatalog, ObjectCatalogError> load_installed_object_catalog(const std::filesystem::path& game_root, std::string_view source, CatalogMode mode) {
    return load_installed_object_catalog(game_root, source, mode, nullptr);
}
} //namespace simcoaster
