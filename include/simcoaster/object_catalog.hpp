#pragma once
#include "dynamic_texture_balance.hpp"
#include "mesh_model.hpp"
#include "object_balance.hpp"
#include "resource_archive.hpp"
#include "ride_description_balance.hpp"
#include "rsse_program.hpp"
#include <variant>

namespace simcoaster {
struct WorldRuntime;
ResearchCatalogDefinition* first_object_catalog_entry(ObjectCatalogManager*);
ResearchCatalogDefinition* next_object_catalog_entry(ObjectCatalogManager*);
ResearchCatalogDefinition* find_object_definition_by_id(ObjectCatalogManager*, std::uint16_t);
enum class CatalogMode {
    standard,
    online,
    easy
};
enum class CatalogFailure {
    io,
    missing_source,
    capacity,
    missing_shape,
    category_archive
};

struct ObjectCatalogError {
    std::string path;
    std::variant<CatalogFailure, AssetError, sam::Error, MeshError> cause;
};

std::string describe_catalog_error(const ObjectCatalogError&);

struct ConfiguredObjectText {
    std::array<std::u16string, 3> fields{ u"MISSING STRING", u"MISSING STRING", u"MISSING STRING" };
};

//Native configured-language NAME/SIGNA/SIGNB key/value stream, UTF-16LE.
ConfiguredObjectText parse_configured_object_definition_text(std::span<const std::byte>);
//Native inline64-character catalog display name, populated from configured
//NAME or the localized two-component fallback during catalog loading.
std::u16string get_catalog_object_display_name(WorldRuntime&, std::uint16_t definition_id);

struct ObjectDefinitionAsset {
    std::string category, resource_name, source_path;
    std::filesystem::path game_root;
    ObjectDefinitionBalance balance;
    std::optional<ConfiguredObjectText> configured_text;
    ResearchCatalogDefinition runtime{};
};

//Reads pylon/car configuration and the track cross-section/material schema
//from an installed Coaster.sam.
//A missing file is distinct from a malformed archive or balance field.
std::expected<std::optional<DynamicTextureMeshBalance>, ObjectCatalogError>
load_dynamic_texture_mesh_balance(const ObjectDefinitionAsset&);
//The executable scans columns first and only searches for an exit after it
//finds an entrance. The catalog owns the shape used to populate this cache.
void finalize_object_definition_markers(ResearchCatalogDefinition&, const DefinitionGrid&);
//Load the primary render template through the mesh-resource backend. This is
//one part of catalog resource acquisition; audio and supplemental assets are
//still separate operations.
std::uint32_t load_object_definition_preview_mesh(WorldRuntime&, ObjectDefinitionAsset&);
void load_object_definition_supplemental_meshes(WorldRuntime&, ObjectDefinitionAsset&);
std::optional<RsseScriptSource> resolve_saved_rsse_script_directory(const WorldRuntime&, std::string_view);
enum class ObjectScriptFailure {
    missing,
    io,
    missing_definition
};

struct ObjectScriptError {
    std::string path;
    std::variant<ObjectScriptFailure, AssetError, RsseProgramError> cause;
};

struct LoadedObjectScript {
    RsseProgram program;
    std::string path;
    bool used_fallback;
};

//RequiresWatering selects the shared vegetation script; otherwise use the
//object source. Missing scripts fall back to Data/TestScript/test.rse.
std::expected<LoadedObjectScript, ObjectScriptError> load_installed_object_script(const ObjectDefinitionAsset&,
                                                                                  bool requires_watering);
std::expected<RsseProgram, ObjectScriptError> load_relative_rsse_script(const RsseScriptSource&,
                                                                        std::string_view relative_path);
//Creates the RSSE state and records its ID. Mesh binding and optional initial
//effects remain backend operations performed by the park-object constructor.
std::expected<RsseScriptState*, ObjectScriptError> create_installed_park_object_rsse_state(WorldRuntime&,
                                                                                           ParkObjectRuntimeView&);

struct LoadedObjectCatalog {
    std::unique_ptr<RideDescriptionBalance> shared_ride_description;
    std::vector<std::unique_ptr<ObjectDefinitionAsset>> definitions;
    //Payload addresses remain stable when this owner moves. The returned view
    //borrows these assets; keep the owner alive while a WorldRuntime uses the
    //view.
    ObjectCatalogManager runtime_view() const;
};

//Installed per-theme category directories, with loose object directories and
//WAD object bundles. Resource mesh/audio loading remains a separate
//dependency.
std::expected<LoadedObjectCatalog, ObjectCatalogError>
load_installed_object_catalog(const std::filesystem::path& game_root,
                              std::string_view source,
                              CatalogMode = CatalogMode::standard);
//Carry the process-wide Bumper.sam receiver across catalog reloads. The
//previous value is copied before any new object overlays are parsed.
std::expected<LoadedObjectCatalog, ObjectCatalogError>
load_installed_object_catalog(const std::filesystem::path& game_root,
                              std::string_view source,
                              CatalogMode,
                              const RideDescriptionBalance* previous_description,
                              std::string_view configured_language = "American");
} //namespace simcoaster
