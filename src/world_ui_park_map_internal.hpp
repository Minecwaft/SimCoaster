#pragma once
#include "simcoaster/world.hpp"
#include "simcoaster/world_ui_park_map.hpp"
#include <map>
#include <set>

namespace simcoaster {
struct WorldParkMapUiController::State {
    enum class CellClass {
        none,
        ride,
        shop,
        sideshow,
        facility,
        feature,
        litter,
        security,
        chill,
        track,
        hoarding,
        path,
        queue,
        outside
    };

    struct Cell {
        CellClass kind {};
        ThingId object {};
        auto operator<=>(const Cell&) const = default;
    };
    enum class Filter {
        mask,
        primary,
        secondary
    };

    struct Marker {
        std::size_t texture {};
        std::int32_t source_kind {};
        Filter filter {};
        std::uint32_t value {};
        float scale {};
        std::array<float, 2> position {};
        ThingId thing {};
        const ThingHeader* identity {};
        bool visible {};
        RgbaPixel color { 255, 255, 255, 255 };
    };

    WorldRuntime& c;
    UiRuntime& ui;
    std::filesystem::path game_root;
    WorldParkMapUiHost host;
    UiControl *root {}, *popup {};
    TextureImage background;
    std::shared_ptr<TextureImage> raster;
    std::array<TextureImagePtr, 21> textures;
    std::array<Cell, map_cell_count> cells {};
    std::array<std::int32_t, map_cell_count> heat {}, weights {};
    std::vector<Marker> markers;
    std::optional<std::size_t> hover;
    ThingId hover_cell {}, retiring {};
    std::set<ThingId> retired;
    std::map<ThingId, std::uint64_t> generations;
    std::array<float, 2> center {};
    float zoom = 1;
    std::uint32_t width {}, height {}, pixel_width = 1100, pixel_height = 760;
    UiPoint cursor { 1024, 768 };
    std::int32_t heat_choice {}, staff_choice {}, overlay_choice {};
    std::uint32_t primary_filter = 2, mask = 128;
    bool closing {}, pan_active {}, rmb_scroll {}, dirty = true, alternate {};
    std::set<std::uint32_t> keys;
    std::array<RgbaPixel, 4> heat_colors {}; //empty,minimum,middle,maximum
    std::array<RgbaPixel, 3> menu_colors {}; //base,highlight,depressed
    State(WorldRuntime&, UiRuntime&, const std::filesystem::path&, WorldParkMapUiHost);
    ~State();
    UiControl& required(std::uint32_t);
    std::u16string text(std::uint32_t) const;
    void caption(std::uint32_t, std::u16string_view, std::uint32_t = 6);
    void bind(UiControl&);
    void open();
    void close();
    void close_to_center(ThingId inspect = 0);
    void policy();
    void labels();
    void close_popup();
    void toggle_popup(std::uint32_t, bool);
    void pan(float, float);
    void pointer(UiPoint);
    void build_cache();
    void build_markers();
    bool passes(const ParkObjectRuntimeView&) const;
    bool admitted(const Marker&) const;
    void update_marker(Marker&);
    std::array<float, 2> project(std::array<float, 2>) const;
    std::array<float, 2> unproject(UiPoint) const;
    void render();
    void draw_cells(bool alert);
    void draw_heat();
    void draw_profiles();
    void draw_coverage();
    void pixel(std::int32_t, std::int32_t, RgbaPixel);
    void rectangle(std::int32_t, std::int32_t, std::int32_t, std::int32_t, RgbaPixel, bool = false);
    void line(float, float, float, float, RgbaPixel);
    void circle(float, float, float, RgbaPixel);
    std::intptr_t event(UiControl&, const UiEvent&);
    void advance();
    void append(UiDrawList&, std::uint32_t, std::uint32_t);
};
} //namespace simcoaster
