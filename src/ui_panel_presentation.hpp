#pragma once
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/ui_panel_runtime.hpp"
#include <algorithm>
#include <optional>
#include <stdexcept>

namespace simcoaster {
struct WorldRuntime;
enum class UiPanelListStyle {
    optional_meter_columns,
    required_aligned_columns
};

struct UiPanelPresentationPolicy {
    std::string_view missing_control, text_error;
    bool text_error_row = true;
    std::int32_t caption_font = 7, caption_horizontal = 1;
    bool white_caption {};
    std::string_view list_error {};
    UiPanelListStyle list_style = UiPanelListStyle::optional_meter_columns;
};

std::u16string localize_ui_panel(const WorldRuntime&, std::optional<std::span<const std::byte>>, std::uint32_t, std::span<const BfstSignedIntegerArgument>, std::span<const BfstWideStringArgument>, const UiPanelPresentationPolicy&);

template <class Owner, auto Method>
struct UiPanelCallback {
    Owner& owner;

    std::intptr_t operator()(UiControl& sender, const UiEvent& event) const {
        return (owner.*Method)(sender, event);
    }
};

//The controller supplies its existing runtime, bindings and language owner.
//Explicit object parameters share the operations without another state owner,
//a forwarding interface, or a runtime strategy object.
struct UiPanelPresentation {
    template <auto Method, class Self>
    auto panel_callback(this Self& self) {
        return UiPanelCallback<Self, Method> { self };
    }

    template <class Self>
    UiControl& required(this const Self& self, UiControl* parent, std::uint32_t id) {
        return self.bindings.required(parent, id, Self::panel_presentation.missing_control);
    }

    template <class Callback>
    void bind(this auto& self, UiControl& control, Callback callback) {
        bind_ui_panel(self.ui, control, std::move(callback));
    }

    template <class Callback>
    UiControl& parse_control(this auto& self, UiControl* parent, std::span<const unsigned char> bytes, std::uint32_t id, Callback callback, std::string_view error) {
        auto result = self.ui.parse_template(parent, std::as_bytes(bytes));
        if (!result || !*result) {
            throw std::runtime_error(std::string(error));
        }
        //Search only the parser's returned tree: its last top-level creation.
        auto& control = self.required(*result, id);
        self.bind(control, std::move(callback));
        return control;
    }

    template <class Self>
    std::u16string localized_text(this const Self& self, std::span<const std::byte> table, std::uint32_t row, std::span<const BfstSignedIntegerArgument> integers = {}, std::span<const BfstWideStringArgument> strings = {}) {
        return localize_ui_panel(self.c, table, row, integers, strings, Self::panel_presentation);
    }

    template <class Self>
    std::u16string text(this const Self& self, std::uint32_t row, std::span<const BfstSignedIntegerArgument> integers = {}, std::span<const BfstWideStringArgument> strings = {}) {
        return localize_ui_panel(self.c, std::nullopt, row, integers, strings, Self::panel_presentation);
    }

    template <class Self>
    void caption(this Self& self, UiControl& control, std::u16string_view value, std::uint32_t font, std::int32_t horizontal = Self::panel_presentation.caption_horizontal, std::int32_t vertical = 1) {
        if constexpr (Self::panel_presentation.white_caption) {
            control.color = { 255, 255, 255, 255 };
        }
        control.font = font;
        control.horizontal_alignment = horizontal;
        control.vertical_alignment = vertical;
        self.ui.set_text(control, value);
    }

    template <class Self>
        requires(Self::panel_presentation.caption_font >= 0)
    void caption(this Self& self, UiControl& control, std::u16string_view value) {
        self.caption(control, value, Self::panel_presentation.caption_font);
    }

    template <bool FirstVisibility = true, class Self, class Question>
    void present_message(this Self& self, UiControl*& wrapper, UiControl*& root, UiMessageControls& controls, std::optional<std::u16string_view> heading, std::u16string& body, const Question& question, std::optional<UiMessageButtonKind> first = {}) {
        auto& title = *controls.title;
        if (heading) {
            self.caption(title, *heading, 5, 0, 1);
            title.primary_shadow_text = true;
        } else {
            title.primary_shadow_text = true;
            if (const auto split = body.find(u"\n\n"); split != body.npos) {
                self.caption(title, std::u16string_view { body }.substr(0, split), 5, 0, 1);
                body.erase(0, split + 2);
            }
        }
        self.caption(*root, body, 4, 1, 1);
        root->color = { 0, 32, 128, 255 };
        root->text_constraints = { true, true };
        auto& cancel = *controls.cancel;
        auto* early_okay = controls.okay;
        const auto first_kind = first ? *first : question ? UiMessageButtonKind::cancel : UiMessageButtonKind::okay;
        cancel.asset_hash = ui_model_asset_hash(first_kind == UiMessageButtonKind::okay ? "b_okay" : "b_cancel");
        cancel.help_index = first_kind == UiMessageButtonKind::okay ? 0u : 3u;
        if constexpr (FirstVisibility) {
            self.ui.set_visible(cancel, first_kind != UiMessageButtonKind::none);
            self.ui.set_visible(*controls.close, first_kind != UiMessageButtonKind::none);
        }
        //Application dialogs retain this projection across visibility callbacks;
        //panel dialogs resolve it after their first-button visibility changes.
        auto& okay = *(heading ? early_okay : controls.okay);
        okay.asset_hash = ui_model_asset_hash("b_okay");
        okay.help_index = 0;
        self.ui.set_visible(okay, static_cast<bool>(question));
        self.ui.place_modal_layer(*wrapper);
        self.ui.set_input_root(wrapper);
        self.ui.set_focus(root);
    }

    template <class Self>
        requires(!Self::panel_presentation.list_error.empty())
    void list(this Self& self, UiControl& control, std::span<const std::uint32_t> headings, std::span<const UiListCellFormat> formats, std::int32_t key = 0, std::uint32_t font = 7) {
        constexpr bool aligned = Self::panel_presentation.list_style == UiPanelListStyle::required_aligned_columns;
        if (formats.size() != headings.size() || formats.size() != control.columns.size()) {
            if constexpr (aligned) {
                throw std::logic_error(std::string(Self::panel_presentation.list_error));
            } else {
                throw std::runtime_error(std::string(Self::panel_presentation.list_error));
            }
        }
        control.font = font;
        control.row_height_font_padding = 6;
        control.list_highlight_asset_hash = ui_model_asset_hash("hilight");
        control.column_presentations.resize(formats.size());
        //Initialize every column before a caption callback or localization
        //failure can observe the list. Native setup uses these same phases.
        for (std::size_t i = 0; i < formats.size(); ++i) {
            control.column_presentations[i].format = formats[i];
            control.column_presentations[i].font = font;
        }
        for (std::size_t i = 0; i < formats.size(); ++i) {
            auto& column = control.column_presentations[i];
            if constexpr (aligned) {
                column.horizontal_alignment = i ? 2 : 0;
            } else if (formats[i] == UiListCellFormat::meter) {
                column.meter = { "ridestatbar.tga", { 0, 10 } };
            }
            const auto id = 16u + static_cast<std::uint32_t>(i);
            UiControl* heading;
            if constexpr (aligned) {
                heading = &self.required(&control, id);
            } else {
                heading = self.ui.find(&control, id);
            }
            if (!heading) {
                continue;
            }
            self.caption(*heading, self.text(headings[i]), 8, 1);
            //503090 keeps the primary bevel separate from the secondary text;
            //enabled palettes become white and the disabled palette survives.
            heading->asset_hash = ui_model_asset_hash("!cbut");
            if constexpr (!aligned) {
                heading->color = { 255, 255, 255, 255 };
            }
            heading->text_constraints = { true, true };
            for (std::size_t state = 0; state < 3; ++state) {
                heading->button_text_colors[state] = { 255, 255, 255, 255 };
            }
        }
        self.ui.set_list_sort_key(control, key, false);
    }

    template <bool RowStyle = false, class Self>
    void project_rows(this Self& self, UiControl& control, std::vector<UiListRow> rows, std::uint64_t update_mask = 0) {
        //Collect removals before mutation; removing a parent also removes its
        //children. Existing rows retain their order and selected identity.
        std::vector<std::intptr_t> removed;
        for (const auto& row : control.rows) {
            if (std::ranges::none_of(rows, [&](const auto& candidate) {
                    return candidate.identity == row.identity;
                })) {
                removed.push_back(row.identity);
            }
        }
        for (const auto id : removed) {
            (void)self.ui.remove_list_row(control, id, false);
        }
        for (auto& row : rows) {
            if (const auto index = self.ui.find_list_row(control, row.identity)) {
                if constexpr (RowStyle) {
                    update_mask = (std::uint64_t { 1 } << row.cells.size()) - 1u;
                }
                (void)self.ui.update_list_row(control, row.identity, row.cells, update_mask, false);
                if constexpr (RowStyle) {
                    control.rows[*index].color = row.color;
                    control.rows[*index].parent_identity = row.parent_identity;
                }
            } else {
                self.ui.insert_list_row(control, std::move(row), std::nullopt, false);
            }
        }
    }
};
} //namespace simcoaster
