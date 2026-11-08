#include "simcoaster/standard_balance.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/ui_control.hpp"
#include <format>
#include <stdexcept>

namespace simcoaster {
void load_world_source_sam_storage(WorldRuntime& c, const std::filesystem::path& root) {
    const auto directory = root / "Data/levels" / c.source.selected_resource_source_name;
    const auto load = [&](auto& value, std::string_view name) {
        const auto path = directory / name;
        const auto text = c.ports.assets.read_balance_asset(c, path.native_encoded_string());
        if (!text) {
            throw std::runtime_error(std::format("Missing SAM: {}", path.native_encoded_string()));
        }
        if (!text->empty()) {
            const auto parsed = sam::parse_record(value, *text);
            if (!parsed) {
                throw std::runtime_error(sam::describe_error(path.native_encoded_string(), parsed.error()));
            }
        }
    };
    //Three independently reset-loaded receivers; stop at the first failure.
    load(c.balance.hoarding_sam.main, "Hoardings.sam");
    load(c.balance.hoarding_sam.cemetery, "cem_Hoardings.sam");
    load(c.balance.hoarding_sam.end, "EndHoardings.sam");
    //4288F0 uses the previous/current columns, in this exact direction.
    for (std::size_t i = 0; i < 4; ++i) {
        c.advisor.speech.advisor_mesh_node_ids_to_clear[i] = c.balance.standard_supplement.advisor_costumes[i].AddOns;
        c.advisor.speech.advisor_mesh_node_ids_to_set[i] = c.balance.standard_supplement.advisor_costumes[i].SubOffs;
    }
}

void apply_standard_menu_text_colors(UiControl& control, const StandardUiParameters& p, bool white_mode, bool proceed_ready) {
    const auto rgba = [](std::int32_t r, std::int32_t g, std::int32_t b) {
        return std::array<std::uint8_t, 4> { static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), 255 };
    };
    const std::array<std::uint8_t, 4> white { 255, 255, 255, 255 };
    const auto normal = rgba(p.red, p.green, p.blue);
    control.button_text_colors = { proceed_ready ? white : normal, normal, rgba(p.active_red, p.active_green, p.active_blue), rgba(p.disabled_red, p.disabled_green, p.disabled_blue) };
    if (white_mode) {
        control.color = white;
        control.button_text_colors.fill(white);
    }
}

const StandardUiBalance& ensure_world_standard_ui_balance(WorldRuntime& c, const std::filesystem::path& root) {
    if (c.balance.standard_ui_balance.loaded) {
        return c.balance.standard_ui_balance;
    }
    StandardUiBalance balance;
    const auto loaded = load_standard_balance_overlays(
        root,
        c.source.selected_resource_source_name,
        [&](const auto& path) {
            return c.ports.assets.read_balance_asset(c, path.native_encoded_string());
        },
        [&](std::string_view text, bool preserve, const auto& path) {
            const auto result = sam::parse_record<StandardUiBalance, true>(balance, text, preserve);
            if (!result) {
                throw std::runtime_error(std::format("Invalid UI balance in {} at byte {}", path.native_encoded_string(), result.error().offset));
            }
        });
    if (!loaded) {
        throw std::runtime_error("Original UserInterface/ToolTips balance is missing");
    }
    balance.loaded = true;
    c.balance.standard_ui_balance = std::move(balance);
    return c.balance.standard_ui_balance;
}
} //namespace simcoaster
