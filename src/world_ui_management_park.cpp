#include "simcoaster/layout_replay.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_object_ui.hpp"
#include "simcoaster/world_park_admission.hpp"
#include "simcoaster/world_theme.hpp"
#include "world_ui_management_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char gate_words[] {
#embed "ui_templates/app_gates.bin"
};
constexpr unsigned char gate_name_words[] {
#embed "ui_templates/app_gate_name.bin"
};
constexpr unsigned char award_words[] {
#embed "ui_templates/app_awards.bin"
};
constexpr unsigned char statistics_words[] {
#embed "ui_templates/app_statistics.bin"
};

void commit_gate_name(WorldRuntime& c, ParkObjectRuntimeView& gate, std::u16string_view first, std::u16string_view second) {
    if (first.size() > 14 || second.size() > 14) {
        throw std::length_error("Park gate editor exceeds original14-unit component");
    }
    const auto set = [&](auto& destination, std::u16string_view value) {
        std::ranges::fill(destination, char16_t {});
        std::ranges::copy(value, destination.begin());
    };
    //53B270 deliberately has no ordinary object-name duplicate rejection.
    //Both component setters record the gate sentinel and update theme names;
    //the editor then publishes the selected theme names even in online mode.
    auto& manager = get_or_create_theme_global_info_manager(c);
    const auto store_theme = [&](std::u16string_view value, bool component_a) {
        if (manager.selected_theme_info) {
            set(component_a ? manager.selected_theme_info->sign_name_a : manager.selected_theme_info->sign_name_b, value);
            manager.selected_theme_info->name_changed = 1;
        }
    };
    for (const bool component_a : { true, false }) {
        const auto value = component_a ? first : second;
        set(component_a ? gate.name_a : gate.name_b, value);
        if (c.mode.game != GameMode::online) {
            store_theme(value, component_a);
        }
        if (!c.construction.action_recording_disabled && c.construction.action_record_streams_initialized) {
            if (!c.construction.action_record_stream) {
                throw std::logic_error("Gate rename recording stream absent");
            }
            auto result = write_layout_action(*c.construction.action_record_stream, LayoutAction { component_a ? LayoutActionCode::name_a : LayoutActionCode::name_b, LayoutMappedName { 60000, std::u16string { value } } });
            if (!result) {
                throw std::runtime_error(describe_layout_error(result.error()));
            }
        }
    }
    if (gate.mesh_instance_id) {
        if (!c.resources.meshes) {
            throw std::logic_error("Gate rename mesh owner absent");
        }
        c.resources.meshes->set_object_sign_caption(c, gate, first, second);
    }
    store_theme(first, true);
    store_theme(second, false);
    //Native53B44A's two encoded constants decode to these exact inputs.
    //It enables the real saved cheat latch and emits the boolean tag21 event.
    if (first == u"Kevin the" && second == u"Cosmic Cow") {
        c.debug.cheat_commands.cheat_commands_enabled = 1;
        const std::array payload { std::byte { 1 } };
        WorldEvent event { ExternalWorldEvent { 21, payload } };
        dispatch_world_event_to_subscribers(c, get_world_event_subscription_registry(c), &event, deliver_world_record_event, nullptr);
    }
}
} //namespace

void WorldManagementUiController::State::open_gates() {
    if (page == Page::gates && root) {
        return;
    }
    begin(Page::gates);
    gate_target = world().saved.park_gates;
    root = &parse(gate_words, 42500);
    title(42504, park_object_ui_display_name(gates()));
    caption(required(root, 42503), text(428), 6);
    required(root, 42503).text_constraints = { true, true };
    staged_fee = bank().admission_fee;
    edit(required(root, 42502), 0, 10000, staged_fee, u"$ ");
    auto& toggle = required(root, 42506);
    ui.set_value(toggle, static_cast<std::int32_t>((toggle.visual_flags & ~1u) | (world().saved.park_closed ? 1u : 0u)));
    toggle.help_index = world().saved.park_closed ? 224 : 225;
    auto& name = required(root, 42504);
    ui.set_primary_callback(name, [this](UiControl& sender, const UiEvent& e) {
        if (e.type == 65537) {
            sender.color = { 0xEF, 0xEA, 0x18, 255 };
        } else if (e.type == 65538) {
            sender.color = { 255, 255, 255, 255 };
        }
        if (e.type == 65542 && !e.id) {
            open_gate_name();
            return std::intptr_t {};
        }
        return ui.secondary(sender, e);
    });
    stage_advisor(488);
    activate();
}

void WorldManagementUiController::State::open_gate_name() {
    if (name_editor || page != Page::gates) {
        return;
    }
    const auto parsed = ui.parse_template(root, std::as_bytes(std::span { gate_name_words }));
    if (!parsed) {
        throw std::runtime_error(parsed.error().reason);
    }
    name_editor = &required(root, 7000000);
    const auto bounds = name_editor->rect;
    ui.translate(*name_editor, root->rect.left + root->rect.width() / 2 - bounds.width() / 2 - bounds.left, root->rect.top + root->rect.height() / 2 - bounds.height() / 2 - bounds.top);
    ui.set_depth(*name_editor, root->render_depth + 8);
    bind(*name_editor, [this](UiControl& sender, const UiEvent& e) -> std::intptr_t {
        if (e.type == 20 && &sender == name_editor) {
            name_editor = nullptr;
            c.ports.ui.restore_command_families_state(c, false);
            if (!closing) {
                if (host.panels.synchronize_focus) {
                    host.panels.synchronize_focus();
                } else {
                    ui.set_focus(root);
                }
            }
            return ui.secondary(sender, e);
        }
        if (e.type == 256 && (e.id == 4294967295u || e.id == 4294967294u)) {
            if (e.id == 4294967295u) {
                const auto first = required(name_editor, 7000002).text, second = required(name_editor, 7000003).text;
                commit_gate_name(c, gates(), first, second);
                title(42504, park_object_ui_display_name(gates()));
            }
            auto* old = name_editor;
            ui.destroy(old);
            return 0;
        }
        if (e.type == 2050) {
            if (e.id == 7000002) {
                ui.set_focus(&required(name_editor, 7000003));
            } else {
                ui.post(name_editor, 256, 4294967295u);
            }
            return 0;
        }
        if (e.type == 2051) {
            ui.set_focus(&required(name_editor, e.id == 7000002 ? 7000003 : 7000002));
            return 0;
        }
        if (e.type == 2052 || e.type == 5) {
            auto* old = name_editor;
            ui.destroy(old);
            return 0;
        }
        return ui.secondary(sender, e);
    });
    auto& heading = required(name_editor, 7000001);
    caption(heading, text(716), 5, 0, 1);
    heading.secondary_shadow_text = true;
    const auto names = park_object_ui_name_components(gates());
    for (std::uint32_t i = 0; i < 2; ++i) {
        auto& entry = required(name_editor, 7000002 + i);
        entry.font = 7;
        entry.text_limit = 14;
        entry.edit_caret_asset_hash = ui_model_asset_hash("hilight");
        ui.set_text(entry, names[i]);
        ui.prepare_edit_selection(entry);
    }
    c.ports.ui.disable_command_families_and_set_disabled_flag(c);
    ui.set_focus(&required(name_editor, 7000002));
}

void WorldManagementUiController::State::open_awards(std::optional<std::int32_t> type) {
    if (type && (*type < 0 || *type >= 8)) {
        throw std::out_of_range("Park award type");
    }
    if (page != Page::awards || !root) {
        begin(Page::awards);
        root = &parse(award_words, 14000);
        for (std::uint32_t i = 0; i < 8; ++i) {
            auto& caption_control = required(root, 14011 + i);
            caption(caption_control, text(453 + i), 6);
            caption_control.text_constraints = { true, true };
        }
        const auto name = park_object_ui_display_name(gates());
        const std::array args { BfstWideStringArgument { 20, name } };
        title(14019, text(452, {}, args));
        //562AA1..562ABA positions the animated overlay four depth units above
        //the awards group, independently of its template placement.
        ui.set_depth(required(root, 14020), required(root, 14002).render_depth + 4);
        c.ui.panels.park_awards.open = true;
        stage_advisor(437);
        activate();
    }
    if (type) {
        c.ui.panels.park_awards.highlighted_type = *type;
        c.ui.panels.park_awards.highlight_visible = true;
        ++c.ui.panels.park_awards.revision;
    }
    award_revision = 0;
    refresh_awards();
}

void WorldManagementUiController::State::refresh_awards() {
    if (page != Page::awards || !root) {
        return;
    }
    auto& theme = get_or_create_theme_global_info_manager(c);
    auto* info = get_or_create_theme_global_info_for_source(c, theme, c.source.selected_resource_source_name);
    if (!info) {
        throw std::logic_error("Awards source metadata absent");
    }
    for (std::uint32_t i = 0; i < 8; ++i) {
        auto& row = required(root, 14003 + i);
        ui.set_value(row, static_cast<std::int32_t>(i));
        ui.set_visible(row, info->award_winner_flags[i] != 0);
    }
    auto& model = c.ui.panels.park_awards;
    auto& overlay = required(root, 14020);
    if (award_revision != model.revision || highlighted_award != model.highlighted_type) {
        award_revision = model.revision;
        highlighted_award = model.highlighted_type;
        const auto type = highlighted_award.value_or(-1);
        if (type < 0 || type >= 8) {
            ui.set_visible(overlay, false);
            return;
        }
        const auto bounds = required(root, 14003 + static_cast<std::uint32_t>(type)).rect;
        ui.translate(overlay, bounds.left + bounds.width() / 2 - overlay.rect.width() / 2 - overlay.rect.left, bounds.top + bounds.height() / 2 - overlay.rect.height() / 2 - overlay.rect.top);
        award_phase = award_step = award_next_ms = 0;
        award_started_ms = static_cast<std::uint32_t>(ui.clock().count());
        ui.set_value(overlay, 0);
        ui.set_visible(overlay, true);
    }
    if (!overlay.visible()) {
        return;
    }
    const auto now = static_cast<std::uint32_t>(ui.clock().count());
    if (now > award_next_ms) {
        award_next_ms = now + 60;
        //561E60 cycles0,1,2,1,0 and waits for the0 phase before expiry.
        constexpr std::array<std::int32_t, 4> parts { 0, 1, 2, 1 };
        award_phase = (award_phase + 1u) % parts.size();
        ui.set_value(overlay, parts[award_phase]);
        if (award_phase == 0 && now - award_started_ms > 3000) {
            ui.set_visible(overlay, false);
            model.highlight_visible = false;
        }
    }
}

void WorldManagementUiController::State::open_statistics() {
    if (page == Page::statistics && root) {
        return;
    }
    begin(Page::statistics);
    root = &parse(statistics_words, 42000);
    title(42017, text(422));
    graph_control = &required(root, 42008);
    graph_styles = { { 17, { 255, 0, 255, 255 } }, { 14, { 255, 255, 0, 255 } }, { 20, { 255, 0, 0, 255 } } };
    c.ui.panels.park_statistics.open = true;
    for (const auto [id, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 8> { { { 42010, 874 }, { 42011, 872 }, { 42012, 873 }, { 42013, 424 }, { 42015, 425 }, { 42016, 423 }, { 42003, 423 }, { 42005, 426 } } }) {
        auto& control = required(root, id);
        caption(control, text(row), 6, id >= 42010 && id <= 42016 ? 1 : 0);
        if (id >= 42010 && id <= 42016) {
            control.color = { 0, 0, 0, 255 };
        }
    }
    const auto range = c.ui.panels.park_statistics.range;
    ui.set_selected(required(root, 42009), range == StatisticsGraphRange::one_year ? 42011 : range == StatisticsGraphRange::three_years ? 42012 : 42010);
    ui.set_periodic(*root, 0, UiPeriodicEvent { 16, 524419, 0, std::chrono::milliseconds { 1000 } });
    c.ports.ui.refresh_park_statistics_month(c);
    refresh_statistics();
    stage_advisor(161);
    activate();
}

void WorldManagementUiController::State::refresh_statistics() {
    if (page != Page::statistics || !root) {
        return;
    }
    const auto mask = c.ui.panels.park_statistics.selected_series_mask;
    for (const auto [id, bit, part] : std::array<std::array<std::uint32_t, 3>, 3> { { { 42016, 1, 1 }, { 42013, 2, 2 }, { 42015, 4, 3 } } }) {
        auto& control = required(root, id);
        const bool selected = (mask & bit) != 0;
        ui.set_value(control, static_cast<std::int32_t>((control.visual_flags & ~1u) | (selected ? 1u : 0u)));
        ui.set_attachment_part(required(&control, 42014), static_cast<std::uint16_t>(selected ? part : 0));
    }
    scalar(42004, fixed::word(count_eligible_person_records(c, 4)));
    scalar(42006, fixed::word(analyser().lifetime_visitors));
}

std::intptr_t WorldManagementUiController::State::park_event(UiControl& sender, const UiEvent& e) {
    if (page == Page::gates) {
        if (e.type == 2048) {
            staged_fee = static_cast<std::int32_t>(e.value);
            return 0;
        }
        if (e.type == 256) {
            if (e.id == 42505) {
                set_world_admission_fee(&bank(), staged_fee);
                close();
                return 0;
            }
            if (e.id == 42501 || e.id == 4294967294u) {
                close();
                return 0;
            }
            if (e.id == 42507) {
                help_advisor(488);
                return 0;
            }
            if (e.id == 42506) {
                handle_world_park_open_close(c, world(), e.value != 1);
                required(root, 42506).help_index = e.value == 1 ? 224 : 225;
                return 0;
            }
        }
    } else if (page == Page::awards) {
        if (e.type == 30) {
            refresh_awards();
        }
        if (e.type == 256) {
            if (e.id == 14001 || e.id == 4294967294u) {
                close();
                return 0;
            }
            if (e.id == 14021) {
                help_advisor(437);
                return 0;
            }
        }
    } else if (page == Page::statistics) {
        if (e.type == 16 && e.id == 524419) {
            refresh_statistics();
            return 0;
        }
        auto& graph = c.ui.panels.park_statistics;
        if (e.type == 256) {
            if (e.id == 42001 || e.id == 4294967294u) {
                close();
                return 0;
            }
            if (e.id == 42018) {
                help_advisor(161);
                return 0;
            }
            const auto bit = e.id == 42016 ? 1u : e.id == 42013 ? 2u : e.id == 42015 ? 4u : 0u;
            if (bit) {
                graph.selected_series_mask = graph.selected_series_mask == bit ? 0 : bit;
                c.ports.ui.refresh_park_statistics_month(c);
                refresh_statistics();
                return 0;
            }
        }
        if (e.type == 257 && (e.id == 42010 || e.id == 42011 || e.id == 42012)) {
            graph.range = e.id == 42011 ? StatisticsGraphRange::one_year : e.id == 42012 ? StatisticsGraphRange::three_years : StatisticsGraphRange::twelve_years;
            if (e.value == 1) {
                c.ports.ui.refresh_park_statistics_month(c);
            }
            return 0;
        }
    }
    return ui.secondary(sender, e);
}
} //namespace simcoaster
