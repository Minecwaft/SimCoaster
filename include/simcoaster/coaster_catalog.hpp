#pragma once
#include "world_domains_fwd.hpp"
#include "coaster_design.hpp"
#include "park_construction.hpp"
#include "sam_record.hpp"
#include <filesystem>
#include <map>

namespace simcoaster {
//Native 9A2840 schema. The nine fields are native SAM names, not an ABI
//overlay. Zero is the descriptor reset value; X=512 terminates consumption.
struct PredefinedCoasterPylon {
    [[=sam::Field{ "X", sam::Kind::integer }]] std::int32_t X{};
    [[=sam::Field{ "Y", sam::Kind::integer }]] std::int32_t Y{};
    [[=sam::Field{ "IsSpecial", 0, 2 }]] std::int32_t IsSpecial{};
    [[=sam::Field{ "IsFirstSpecial", 0, 2 }]] std::int32_t IsFirstSpecial{};
    [[=sam::Field{ "Level", sam::Kind::integer }]] std::int32_t Level{};
    [[=sam::Field{ "Loft", sam::Kind::scalar }]] float Loft{};
    [[=sam::Field{ "Rotate", sam::Kind::scalar }]] float Rotate{};
    [[=sam::Field{ "Wobble", sam::Kind::scalar }]] float Wobble{};
    [[=sam::Field{ "Pitch", sam::Kind::scalar }]] float Pitch{};
};

struct PredefinedCoasterBalance {
    [[=sam::Rows{ "CoasterPylons" }]] std::array<PredefinedCoasterPylon, 64> CoasterPylons{};
    [[=sam::Extent{ "CoasterPylons" }]] std::uint32_t parsed_extent{};
    std::size_t sentinel_count() const;
};
enum class CoasterCatalogFailure {
    missing_definition,
    invalid_source,
    missing_predefined_data,
    malformed_predefined_data
};

struct CoasterCatalogError {
    CoasterCatalogFailure reason{};
    std::string source;
    std::optional<sam::Error> schema;
};

std::expected<PredefinedCoasterBalance, CoasterCatalogError> parse_predefined_coaster_balance(std::string_view);
std::expected<PredefinedCoasterBalance, CoasterCatalogError>
load_predefined_coaster_balance(const ResearchCatalogDefinition&);
bool is_coaster_catalog_definition(const ResearchCatalogDefinition&);
//B1E1D4 is the raw selector. The SET_RIDE5 secondary B17A48 is separate and
//suppresses this getter for track attachments; it is never assigned 3 here.
std::uint32_t construction_track_type(const AttractionQuery&);
void set_construction_track_type(AttractionWorld&, std::uint32_t, bool record_action = true);

//World-owned catalog selection and portable-design values. Once committed,
//all parent/RSSE/mesh/train/cell ownership belongs to the production World.
class CoasterCatalogRuntime {
    ParkConstructionRuntime& construction_;

    struct Selection {
        std::uint16_t catalog{}, base{};
        std::optional<CoasterDesign> design;
        std::optional<PredefinedCoasterBalance> predefined;
        std::optional<std::int32_t> build_quote_override;
        DefinitionGrid preview_grid{};
    };

    std::optional<Selection> selection_;
    bool internal_selection_{};
    std::map<std::uint16_t, std::expected<PredefinedCoasterBalance, CoasterCatalogError>> predefined_cache_;
    Selection& resolve(AttractionWorld&, std::uint16_t);
    std::int32_t cost(AttractionWorld&, const Selection&) const;

public:
    explicit CoasterCatalogRuntime(ParkConstructionRuntime& construction) : construction_(construction) {}

    bool handles(AttractionWorld&, std::uint16_t) const;
    std::expected<void, CoasterCatalogError> available(AttractionWorld&, std::uint16_t);
    void begin(AttractionWorld&, std::uint16_t, std::int32_t rotation = 0);
    void select_design(AttractionWorld&,
                       CoasterDesign,
                       std::int32_t rotation = 0,
                       std::optional<std::int32_t> build_quote = std::nullopt);
    void select_design_file(AttractionWorld&, const std::filesystem::path&, std::int32_t rotation = 0);
    void clear(AttractionWorld&);
    void observe_set_ride_action(AttractionWorld&);
    void reset_after_restore();
    bool preview(AttractionWorld&, std::uint16_t, WorldCellCoordinates, std::int32_t rotation);
    std::optional<ParkPlacementResult>
    place(AttractionWorld&, std::uint16_t, WorldCellCoordinates, std::int32_t rotation);
};

//Default 490BF0 and predefined 491DE0 use real World cell commands and the
//already-created parent's three station controls, followed by owned geometry.
void build_default_coaster_catalog_layout(AttractionWorld&, ParkConstructionRuntime&, ThingId);
void build_predefined_coaster_catalog_layout(AttractionWorld&,
                                             ParkConstructionRuntime&,
                                             ThingId,
                                             const PredefinedCoasterBalance&);
void build_imported_coaster_catalog_layout(AttractionWorld&, ParkConstructionRuntime&, ThingId, const CoasterDesign&);
//Ordinary COS/predefined creation defers support completion until its own
//RSSE sound state exists. Resolve by this object, never by a stale UI context.
} //namespace simcoaster
