#pragma once
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/ui_control.hpp"
#include "simcoaster/ui_font.hpp"
#include <algorithm>
#include <filesystem>

namespace simcoaster {
struct WorldRuntime;

struct UiContextOption {
    std::uint32_t row;
    bool visible, enabled;
    std::uint32_t key{};
    std::int32_t value{};
};

//Owns only menu presentation. Controllers retain target identities, actions,
//input ownership and the order in which they restore their retained panels.
struct UiContextMenu {
    UiPoint origin{};
    std::chrono::milliseconds started{};
    std::int32_t duration = 300;
    std::array<RgbaPixel, 4> colors{
        { { 192, 192, 192, 255 }, { 255, 255, 255, 255 }, { 0, 0, 0, 255 }, { 64, 64, 64, 255 } }
    };
    std::array<std::uint32_t, 15> rows{};
    bool policy_loaded{}, layout_ready{};

    void load(WorldRuntime&, const std::filesystem::path&);
    std::pair<UiRect, bool> bounds(std::chrono::milliseconds, std::int32_t, std::int32_t, bool) const;
    static std::int32_t measure(UiFontLibrary&, std::uint32_t, std::uint32_t, std::u16string_view, std::uint32_t, bool);

    template <class Controller>
    void start(Controller& owner, UiPoint point) {
        owner.ui.set_depth(*owner.context, 32);
        origin = point;
        started = owner.ui.clock();
        layout_ready = false;
        for (std::uint32_t id = 1001; id <= 1016; ++id)
            owner.ui.set_visible(owner.required(owner.context, id), false);
    }

    template <bool Visitor = false, class Controller, class Options>
    void present(Controller& owner, UiFontLibrary& fonts, std::u16string_view title, const Options& options) {
        const auto px = std::max(owner.pixel_width, 1u), py = std::max(owner.pixel_height, 1u);
        const auto extent = [&](std::u16string_view text, std::uint32_t font) {
            return measure(fonts, px, py, text, font, Visitor);
        };

        struct Visible {
            std::uint32_t row;
            bool enabled;
            std::u16string caption;
        };

        auto visible = [] {
            if constexpr (Visitor)
                return std::array<Visible, 4>{};
            else
                return std::vector<Visible>{};
        }();
        if constexpr (Visitor)
            static_assert(std::tuple_size_v<Options> == 4);
        [[maybe_unused]] std::size_t next{};
        auto width = extent(title, 5);
        for (const auto option : options) {
            if constexpr (!Visitor)
                if (!option.visible)
                    continue;
            Visible entry;
            if constexpr (Visitor) {
                entry.row = option.caption;
                entry.caption = owner.text(option.caption);
            } else {
                entry.row = option.row;
                const std::array args{ BfstSignedIntegerArgument{ option.key, option.value } };
                entry.caption = option.key ? owner.text(option.row, args) : owner.text(option.row);
            }
            entry.enabled = option.enabled;
            width = std::max(width, extent(entry.caption, 6));
            if constexpr (Visitor)
                visible[next++] = std::move(entry);
            else
                visible.push_back(std::move(entry));
        }
        width += px >= 1280 ? 20 : px >= 1024 ? 12 : px >= 800 ? 8 : 0;
        width = std::min(width, 1512);
        const auto height = std::min(130 + static_cast<int>(visible.size()) * 50, 886);
        const auto [animated, ready] = bounds(owner.ui.clock(), width, height, owner.animate_menus);
        owner.ui.set_bounds(*owner.context, animated);
        layout_ready = ready;
        if constexpr (!Visitor)
            rows.fill(0);
        auto& heading = owner.required(owner.context, 1016);
        owner.caption(heading, title, 5, 0, 1);
        if constexpr (!Visitor)
            heading.text_constraints = {};
        heading.color = { 235, 255, 255, 255 };
        owner.ui.set_bounds(heading, { animated.left + 20, animated.top + 19, animated.right - 20, animated.top + 98 });
        owner.ui.set_visible(heading, ready);
        constexpr std::array<std::int32_t, 15> tops{ 130, 181, 231, 280, 330, 380, 431, 479,
                                                     529, 581, 630, 679, 730, 780, 831 };
        constexpr std::array<std::int32_t, 15> heights{ 45, 45, 45, 45, 45, 45, 44, 45, 45, 44, 45, 45, 45, 45, 45 };
        const auto count = Visitor ? visible.size() : rows.size();
        for (std::size_t i = 0; i < count; ++i) {
            auto& control = owner.required(owner.context, static_cast<std::uint32_t>(1001 + i));
            if constexpr (!Visitor) {
                const bool shown = ready && i < visible.size();
                owner.ui.set_visible(control, shown);
                if (!shown)
                    continue;
            }
            const auto& entry = visible[i];
            rows[i] = entry.row;
            owner.caption(control, entry.caption, 6, 0, 1);
            control.button_text_colors = colors;
            control.color = colors[entry.enabled ? 0 : 3];
            if constexpr (!Visitor)
                control.text_constraints = {};
            const auto text_width = std::max(1, extent(entry.caption, 6) - padding(px));
            owner.ui.set_bounds(control,
                                { animated.left + 19,
                                  animated.top + tops[i],
                                  animated.left + 19 + text_width,
                                  animated.top + tops[i] + heights[i] });
            owner.ui.set_enabled(control, entry.enabled);
            //Visitors update hidden controls during opening; the other families
            //leave their caption/enabled state alone until layout completes.
            if constexpr (Visitor)
                owner.ui.set_visible(control, ready);
        }
    }

private:
    static std::int32_t padding(std::uint32_t);
};
} //namespace simcoaster
