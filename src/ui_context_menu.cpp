#include "ui_context_menu.hpp"
#include "simcoaster/standard_balance.hpp"
#include "simcoaster/simulation_sine.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <cmath>
#include <stdexcept>

namespace simcoaster {
void UiContextMenu::load(WorldRuntime& world, const std::filesystem::path& game_root) {
    if (policy_loaded) {
        return;
    }
    const auto& settings = ensure_world_standard_ui_balance(world, game_root).user_interface;
    duration = settings.duration;
    const auto rgba = [](std::int32_t r, std::int32_t g, std::int32_t b) {
        return RgbaPixel { static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), 255 };
    };
    colors = { rgba(settings.red, settings.green, settings.blue), rgba(settings.active_red, settings.active_green, settings.active_blue), rgba(settings.down_red, settings.down_green, settings.down_blue), rgba(settings.disabled_red, settings.disabled_green, settings.disabled_blue) };
    policy_loaded = true;
}

std::pair<UiRect, bool> UiContextMenu::bounds(std::chrono::milliseconds now, std::int32_t width, std::int32_t height, bool animate) const {
    double fraction = 1;
    const auto elapsed = (now - started).count();
    if (animate && duration > 0 && elapsed >= 0 && elapsed < duration) {
        const auto duration_float = static_cast<float>(duration);
        const auto phase = static_cast<float>(static_cast<double>(elapsed) / duration_float * 1.5707964 * simulation_sine_samples_per_radian);
        const auto index = static_cast<std::int32_t>(std::nearbyint(phase));
        fraction = std::trunc(static_cast<double>(simulation_sine_at_index(index)) * duration_float) / duration_float;
        if (fraction < 0 || fraction > 1) {
            fraction = 1;
        }
    }
    const auto w = std::min(width, fixed::truncate_integer(100 + width * fraction));
    const auto h = std::min(height, fixed::truncate_integer(100 + height * fraction));
    return { { origin.x > 1024 ? origin.x - w : origin.x, origin.y > 768 ? origin.y - h : origin.y, origin.x > 1024 ? origin.x : origin.x + w, origin.y > 768 ? origin.y : origin.y + h }, w == width && h == height };
}

std::int32_t UiContextMenu::measure(UiFontLibrary& fonts, std::uint32_t px, std::uint32_t py, std::u16string_view text, std::uint32_t ordinal, bool visitor) {
    const auto font = fonts.slot(ordinal, px);
    if (!font) {
        throw std::runtime_error(visitor ? "Original Visitor context font unavailable" : "Original context font is unavailable");
    }
    UiTextStyle style;
    style.scale = { 2048.f / static_cast<float>(px), 1536.f / static_cast<float>(py) };
    if (!visitor) {
        style.wrap = false;
    }
    const auto extent = fonts.measure(**font, text, { 0, 0, 65000, 65000 }, style);
    if (!extent) {
        throw std::runtime_error(visitor ? "Original Visitor context text layout failed" : "Original context caption cannot be measured");
    }
    return fixed::truncate_integer(extent->bounds[2] - extent->bounds[0] + 12.f * style.scale[0]);
}

std::int32_t UiContextMenu::padding(std::uint32_t px) {
    return fixed::truncate_integer(12.f * 2048.f / static_cast<float>(px));
}
} //namespace simcoaster
