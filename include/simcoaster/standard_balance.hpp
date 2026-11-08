#pragma once
#include "sam_record.hpp"
#include "person_environment.hpp"
#include "hoarding_balance.hpp"
#include <filesystem>

namespace simcoaster {
struct WorldRuntime;
struct UiControl;

//One source ordering for every projection of the native merged receiver.
inline std::array<std::filesystem::path, 2> standard_balance_paths(const std::filesystem::path& root, std::string_view theme) {
    return { root / "Data/levels/Standard.sam", root / "Data/levels" / theme / "Standard.sam" };
}

template <class Reader, class Apply>
bool load_standard_balance_overlays(const std::filesystem::path& root, std::string_view theme, Reader&& read, Apply&& apply) {
    bool preserve = false;
    const auto paths = standard_balance_paths(root, theme);
    for (std::size_t i = 0; i < (theme.empty() ? 1u : 2u); ++i) {
        if (const auto text = read(paths[i])) {
            apply(*text, preserve, paths[i]);
            preserve = true;
        }
    }
    return preserve;
}

struct StandardUiParameters {
    [[=sam::Field { "MiniMenuOpenTime", sam::Kind::integer }]] std::int32_t duration = 300;
    [[=sam::Field { "ToolTipDelay", sam::Kind::integer }]] std::int32_t tooltip_delay {};
    [[=sam::Field { "ToolTipFadeInTime", sam::Kind::integer }]] std::int32_t tooltip_fade {};
    [[=sam::Field { "ToolTipXOfs", sam::Kind::integer }]] std::int32_t tooltip_x {};
    [[=sam::Field { "ToolTipYOfs", sam::Kind::integer }]] std::int32_t tooltip_y {};
    [[=sam::Field { "ToolTipMaxXMove", sam::Kind::integer }]] std::int32_t tooltip_max_x {};
    [[=sam::Field { "ToolTipMaxYMove", sam::Kind::integer }]] std::int32_t tooltip_max_y {};
    [[=sam::Field { "ToolTipOffsetWhere", sam::Kind::integer }]] std::int32_t tooltip_offset_where {};
    [[=sam::Field { "MouseButtonHeldTime", sam::Kind::integer }]] std::int32_t held_time {};
    [[=sam::Field { "MouseScrollSpeedX", sam::Kind::scalar }]] float scroll_x {};
    [[=sam::Field { "MouseScrollSpeedY", sam::Kind::scalar }]] float scroll_y {};
    [[=sam::Field { "MenuTextR", sam::Kind::integer }]] std::int32_t red = 192;
    [[=sam::Field { "MenuTextG", sam::Kind::integer }]] std::int32_t green = 192;
    [[=sam::Field { "MenuTextB", sam::Kind::integer }]] std::int32_t blue = 192;
    [[=sam::Field { "MenuTextHighlightedR", sam::Kind::integer }]] std::int32_t active_red = 255;
    [[=sam::Field { "MenuTextHighlightedG", sam::Kind::integer }]] std::int32_t active_green = 255;
    [[=sam::Field { "MenuTextHighlightedB", sam::Kind::integer }]] std::int32_t active_blue = 255;
    [[=sam::Field { "MenuTextDisabledR", sam::Kind::integer }]] std::int32_t disabled_red = 64;
    [[=sam::Field { "MenuTextDisabledG", sam::Kind::integer }]] std::int32_t disabled_green = 64;
    [[=sam::Field { "MenuTextDisabledB", sam::Kind::integer }]] std::int32_t disabled_blue = 64;
    [[=sam::Field { "MenuTextDepressedR", sam::Kind::integer }]] std::int32_t down_red {};
    [[=sam::Field { "MenuTextDepressedG", sam::Kind::integer }]] std::int32_t down_green {};
    [[=sam::Field { "MenuTextDepressedB", sam::Kind::integer }]] std::int32_t down_blue {};
    [[=sam::Field { "HeatMapEmptyR", sam::Kind::integer }]] std::int32_t empty_red {};
    [[=sam::Field { "HeatMapEmptyG", sam::Kind::integer }]] std::int32_t empty_green {};
    [[=sam::Field { "HeatMapEmptyB", sam::Kind::integer }]] std::int32_t empty_blue {};
    [[=sam::Field { "HeatMapEmptyA", sam::Kind::integer }]] std::int32_t empty_alpha {};
    [[=sam::Field { "HeatMapMinR", sam::Kind::integer }]] std::int32_t minimum_red {};
    [[=sam::Field { "HeatMapMinG", sam::Kind::integer }]] std::int32_t minimum_green {};
    [[=sam::Field { "HeatMapMinB", sam::Kind::integer }]] std::int32_t minimum_blue {};
    [[=sam::Field { "HeatMapMinA", sam::Kind::integer }]] std::int32_t minimum_alpha {};
    [[=sam::Field { "HeatMapMidR", sam::Kind::integer }]] std::int32_t middle_red {};
    [[=sam::Field { "HeatMapMidG", sam::Kind::integer }]] std::int32_t middle_green {};
    [[=sam::Field { "HeatMapMidB", sam::Kind::integer }]] std::int32_t middle_blue {};
    [[=sam::Field { "HeatMapMidA", sam::Kind::integer }]] std::int32_t middle_alpha {};
    [[=sam::Field { "HeatMapMaxR", sam::Kind::integer }]] std::int32_t maximum_red {};
    [[=sam::Field { "HeatMapMaxG", sam::Kind::integer }]] std::int32_t maximum_green {};
    [[=sam::Field { "HeatMapMaxB", sam::Kind::integer }]] std::int32_t maximum_blue {};
    [[=sam::Field { "HeatMapMaxA", sam::Kind::integer }]] std::int32_t maximum_alpha {};
};

struct StandardTooltipOffsets {
    [[=sam::Field { "XOffset", sam::Kind::integer }]] std::int32_t x {};
    [[=sam::Field { "YOffset", sam::Kind::integer }]] std::int32_t y {};
};

inline constexpr std::size_t tooltip_offset_count = 291;

struct StandardUiBalance {
    [[=sam::Group { "UserInterface" }]] StandardUiParameters user_interface;
    [[=sam::Rows { "ToolTips" }]] std::array<StandardTooltipOffsets, tooltip_offset_count> offsets {};
    [[=sam::Extent { "ToolTips" }]] std::uint32_t offset_extent {};
    bool loaded {};
};

const StandardUiBalance& ensure_world_standard_ui_balance(WorldRuntime&, const std::filesystem::path&);
void apply_standard_menu_text_colors(UiControl&, const StandardUiParameters&, bool white_mode = false, bool proceed_ready = false);
//Native source-presentation phase4284E0: after optional save restoration.
void load_world_source_sam_storage(WorldRuntime&, const std::filesystem::path&);

struct StandardSkyCount {
    [[=sam::Field { "n", sam::Kind::nonnegative_integer }]] std::int32_t n {};
};

struct StandardSkyObject {
    [[=sam::Field { "X", sam::Kind::scalar }]] float x {};
    [[=sam::Field { "Y", sam::Kind::nonnegative_scalar }]] float y {};
    [[=sam::Field { "Z", sam::Kind::scalar }]] float z {};
    [[=sam::Field { "Size", sam::Kind::nonnegative_scalar }]] float size {};
    [[=sam::Field { "FrontFlag", sam::Kind::nonnegative_integer }]] std::int32_t front_flag {};
    [[=sam::Field { "Filename", sam::Kind::string }]] std::optional<std::string> filename;
};

struct StandardWaterStart {
    [[=sam::Field { "X", sam::Kind::scalar }]] float x {};
    [[=sam::Field { "Y", sam::Kind::scalar }]] float y {};
    [[=sam::Field { "Z", sam::Kind::scalar }]] float z {};
};

struct StandardAutoRotate {
    [[=sam::Field { "ZoomLevel", sam::Kind::nonnegative_integer }]] std::int32_t zoom_level {};
};

struct StandardStaffType {
    [[=sam::Field { "PayMultiplier", sam::Kind::nonnegative_integer }]] std::int32_t pay_multiplier {};
};

struct StandardLegacyAdvisor {
    [[=sam::Field { "MinTimeAnyMessage", sam::Kind::nonnegative_integer }]] std::int32_t MinTimeAnyMessage {};
    [[=sam::Field { "MinTimeSameMessage", sam::Kind::nonnegative_integer }]] std::int32_t MinTimeSameMessage {};
};

struct StandardAdvisorCostume {
    [[=sam::Field { "AddOns", 0, 35 }]] std::int32_t AddOns { 0 };
    [[=sam::Field { "SubOffs", 0, 35 }]] std::int32_t SubOffs { 0 };
};

struct StandardGoldenTicketLocal {
    [[=sam::Field { "Visitors", sam::Kind::integer }]] std::int32_t Visitors {};
    [[=sam::Field { "PeopleInPark", sam::Kind::integer }]] std::int32_t PeopleInPark {};
    [[=sam::Field { "Happiness", sam::Kind::integer }]] std::int32_t Happiness {};
    [[=sam::Field { "AtLeastThisManyHappyPeople", sam::Kind::nonnegative_integer }]] std::int32_t AtLeastThisManyHappyPeople {};
    [[=sam::Field { "ProfitYear", sam::Kind::integer }]] std::int32_t ProfitYear {};
    [[=sam::Field { "RecentVisitors", sam::Kind::integer }]] std::int32_t RecentVisitors {};
    [[=sam::Field { "RecentVisitorMonths", sam::Kind::integer }]] std::int32_t RecentVisitorMonths {};
};

struct StandardGoldenTicketGlobal {
    [[=sam::Field { "CoasterHeight", sam::Kind::integer }]] std::int32_t CoasterHeight {};
    [[=sam::Field { "GokartExcitement", sam::Kind::integer }]] std::int32_t GokartExcitement {};
    [[=sam::Field { "WaterLength", sam::Kind::integer }]] std::int32_t WaterLength {};
    [[=sam::Field { "MinCellsOwned", sam::Kind::integer }]] std::int32_t MinCellsOwned {};
    [[=sam::Field { "MinCellsCovered", sam::Kind::integer }]] std::int32_t MinCellsCovered {};
};

struct StandardTipSuppression {
    [[=sam::Field { "Mail", sam::Kind::integer }]] std::int32_t mail {};
    [[=sam::Field { "CutScenes", sam::Kind::integer }]] std::int32_t cutscenes {};
    [[=sam::Field { "All", sam::Kind::integer }]] std::int32_t all {};
    [[=sam::Field { "TipOfTheDay", sam::Kind::integer }]] std::int32_t tips {};
};

struct StandardTipBalance {
    [[=sam::Group { "CutTheCrap" }]] StandardTipSuppression suppression;
};

struct StandardSupplementBalance {
    [[=sam::Group { "Advisor" }]] StandardLegacyAdvisor legacy_advisor;
    [[=sam::Rows { "ThemeAdvisorCostumes" }]] std::array<StandardAdvisorCostume, 4> advisor_costumes {};
    [[=sam::Extent { "ThemeAdvisorCostumes" }]] std::uint32_t advisor_costume_extent {};
    [[=sam::Group { "GoldenTicketLocal" }]] StandardGoldenTicketLocal golden_ticket_local;
    [[=sam::Group { "GoldenTicketGlobal" }]] StandardGoldenTicketGlobal golden_ticket_global;

    [[=sam::Group { "AutoRotate" }]] StandardAutoRotate auto_rotate;
    [[=sam::Group { "WaterStartPos" }]] StandardWaterStart water_start;
    [[=sam::Group { "NumSkyObjects" }]] StandardSkyCount sky_count;
    [[=sam::Rows { "SkyObjects" }]] std::array<StandardSkyObject, 4> sky_objects {};
    [[=sam::Extent { "SkyObjects" }]] std::uint32_t sky_extent {};
    [[=sam::Rows { "PerTypeStaffConsts" }]] std::array<StandardStaffType, 5> staff_types {};
    [[=sam::Extent { "PerTypeStaffConsts" }]] std::uint32_t staff_type_extent {};
};

struct WorldCellConstructionCosts {
    [[=sam::Field { "PathCell", sam::Kind::nonnegative_integer }]] std::int32_t path {};
    [[=sam::Field { "QueueCell", sam::Kind::nonnegative_integer }]] std::int32_t queue {};
    [[=sam::Field { "KartTrackCell", sam::Kind::nonnegative_integer }]] std::int32_t kart_track {};
    [[=sam::Field { "WaterTrackCell", sam::Kind::nonnegative_integer }]] std::int32_t water_track {};
    //The SAM parser validates every leaf of a recognized group. MapCell is
    //present in the native Costs schema even though4288F0 publishes only four.
    [[=sam::Field { "MapCell", sam::Kind::nonnegative_integer }]] std::int32_t map {};
};

struct BankStartup {
    [[=sam::Field { "InitialCash", sam::Kind::integer }]] std::int32_t cash {};
    [[=sam::Field { "InitialAdmissionFee", sam::Kind::nonnegative_integer }]] std::int32_t admission_fee {};
};

struct LoanStartup {
    [[=sam::Field { "LoanAmount", sam::Kind::nonnegative_integer }]] std::int32_t amount {};
    [[=sam::Field { "APRInPercent", 0, 100 }]] std::int32_t apr {};
    [[=sam::Field { "RepaymentPeriodInMonths", sam::Kind::nonnegative_integer }]] std::int32_t months {};
    [[=sam::Field { "Lendername", sam::Kind::nonnegative_integer }]] std::int32_t lender {};
};

struct EconomyStartup {
    [[=sam::Group { "BankAccountInfo" }]] BankStartup bank;
    [[=sam::Group { "Costs" }]] WorldCellConstructionCosts cell_costs;
    [[=sam::Rows { "LoanInfo" }]] std::array<LoanStartup, 10> loans {};
    [[=sam::Extent { "LoanInfo" }]] std::uint32_t loan_count {};
};

struct StandardEngineParameters {
    [[=sam::Field { "BackColour", sam::Kind::integer }]] std::uint32_t background {};
    [[=sam::Field { "FogColour", sam::Kind::integer }]] std::uint32_t fog {};
    [[=sam::Field { "AmbientLightLevel", sam::Kind::integer }]] std::uint32_t ambient {};
    [[=sam::Field { "DirectionalLightLevel", sam::Kind::integer }]] std::uint32_t directional {};
    [[=sam::Field { "ProceduralTexturingXOffset", sam::Kind::integer }]] std::int32_t texture_x {};
    [[=sam::Field { "ProceduralTexturingYOffset", sam::Kind::integer }]] std::int32_t texture_y {};
};

struct StandardLightNormalParameters {
    [[=sam::Field { "X", sam::Kind::scalar }]] float x {};
    [[=sam::Field { "Y", sam::Kind::scalar }]] float y {};
    [[=sam::Field { "Z", sam::Kind::scalar }]] float z { 1 };
};

struct ThemeRenderBalance {
    [[=sam::Group { "ThemeEngine" }]] StandardEngineParameters engine;
    [[=sam::Group { "LightNormal" }]] StandardLightNormalParameters normal;
    [[=sam::Group { "MapInfo" }]] PersonMapParameters map {};
};

struct StandardCameraEditExtents {
    [[=sam::Field { "MaxX", sam::Kind::nonnegative_integer }]] std::int32_t max_x {};
    [[=sam::Field { "MaxZ", sam::Kind::nonnegative_integer }]] std::int32_t max_z {};
    [[=sam::Field { "MinX", sam::Kind::nonnegative_integer }]] std::int32_t min_x {};
    [[=sam::Field { "MinZ", sam::Kind::nonnegative_integer }]] std::int32_t min_z {};
};

struct StandardCameraInputBalance {
    [[=sam::Group { "CoasterEditExtents" }]] StandardCameraEditExtents extents;
};

struct StandardLobbyCamera {
    [[=sam::Field { "CamX", sam::Kind::nonnegative_integer }]] std::int32_t x {};
    [[=sam::Field { "CamY", sam::Kind::nonnegative_integer }]] std::int32_t y {};
    [[=sam::Field { "CamZ", sam::Kind::nonnegative_integer }]] std::int32_t z {};
    [[=sam::Field { "CamZoom", sam::Kind::nonnegative_integer }]] std::int32_t zoom {};
    [[=sam::Field { "CamRot", sam::Kind::nonnegative_integer }]] std::int32_t rotation {};
};

struct StandardLobbyBalance {
    [[=sam::Rows { "AllThemes" }]] std::array<StandardLobbyCamera, 15> cameras {};
    [[=sam::Extent { "AllThemes" }]] std::uint32_t extent {};
};

struct StandardSeason {
    [[=sam::IntegerField { "Seasons", "PercentageChanceForHeatwave", -1, 101 }]] std::int32_t heatwave {};
    [[=sam::IntegerField { "Seasons", "PercentageChanceForRain", -1, 101 }]] std::int32_t rain {};
    [[=sam::IntegerField { "Seasons", "PercentageChanceForStorm", -1, 101 }]] std::int32_t storm {};
    [[=sam::IntegerField { "Seasons", "PercentageChanceForSnow", -1, 101 }]] std::int32_t snow {};
};

struct StandardSeasons {
    [[=sam::Rows { "Seasons" }]] std::array<StandardSeason, 4> rows {};
    [[=sam::Extent { "Seasons" }]] std::uint32_t extent {};
};

struct WorldReverbBalance {
    struct Sound {
        [[=sam::Field { "Reverb", 0, 16 }]] std::int32_t preset {};
    };

    [[=sam::Group { "Sound" }]] Sound sound;
};
} //namespace simcoaster
