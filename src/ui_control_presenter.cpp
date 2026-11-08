#include "simcoaster/ui_control_presenter.hpp"
#include "simcoaster/simulation_sine.hpp"
#include "simcoaster/text_format.hpp"
#include <format>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
UiRect intersect(UiRect first, UiRect second) {
    return { std::max(first.left, second.left), std::max(first.top, second.top), std::min(first.right, second.right), std::min(first.bottom, second.bottom) };
}

std::array<float, 4> bounds(UiRect rect) {
    return { static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(rect.right), static_cast<float>(rect.bottom) };
}

std::uint32_t index(std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("UI presentation index domain exceeded");
    }
    return static_cast<std::uint32_t>(value);
}

float depth(std::int32_t value) {
    return ui_attachment_depth(value);
}

UiRect inset(UiRect rectangle, UiPoint amount) {
    rectangle.left += amount.x;
    rectangle.right -= amount.x;
    rectangle.top += amount.y;
    rectangle.bottom -= amount.y;
    return rectangle;
}

void append_meter(UiDrawList& draw, UiAssetLibrary& assets, const UiMeterPresentation& presentation, UiRect rectangle, UiRect clip, std::int32_t value, std::int32_t z, std::int32_t rotation, UiPoint pivot) {
    if (presentation.texture.empty()) {
        throw std::invalid_argument("UI meter has no original texture binding");
    }
    rectangle = inset(rectangle, presentation.inset);
    clip = intersect(clip, { 0, 0, static_cast<std::int32_t>(draw.canvas_width), static_cast<std::int32_t>(draw.canvas_height) });
    if (rectangle.width() <= 0 || rectangle.height() <= 0 || clip.width() <= 0 || clip.height() <= 0) {
        return;
    }
    const auto texture = assets.texture(presentation.texture);
    if (!texture) {
        throw std::runtime_error(std::format("UI meter: {}", describe_texture_error(texture.error())));
    }
    const auto fraction = static_cast<float>(std::clamp(value, 0, 1024)) / 1024.f;
    const auto left = static_cast<float>(rectangle.left);
    const auto right = static_cast<float>(rectangle.right) + .5f;
    const auto top = static_cast<float>(rectangle.top) - .5f;
    const auto bottom = static_cast<float>(rectangle.bottom);
    const auto cut = left + (right - left) * fraction;
    const auto base = index(draw.vertices.size());
    (void)index(draw.vertices.size() + 8);
    //4E82A0/4E8990: native modelX/Y0..100, two quads sharing the moving
    //boundary. Shared decoding includes the native upload row direction, so
    //retain the authored filledV(.9,.6) and emptyV(.4,.1).
    std::array<UiDrawVertex, 8> vertices { UiDrawVertex { { left, top, depth(z) * .1f }, { 0, .9f } },       UiDrawVertex { { cut, top, depth(z) * .1f }, { fraction, .9f } },    UiDrawVertex { { cut, bottom, depth(z) * .1f }, { fraction, .6f } }, UiDrawVertex { { left, bottom, depth(z) * .1f }, { 0, .6f } },
                                           UiDrawVertex { { cut, top, depth(z) * .1f }, { fraction, .4f } }, UiDrawVertex { { cut, bottom, depth(z) * .1f }, { fraction, .1f } }, UiDrawVertex { { right, top, depth(z) * .1f }, { 1, .4f } },         UiDrawVertex { { right, bottom, depth(z) * .1f }, { 1, .1f } } };
    if (rotation) {
        const auto angle = static_cast<double>(rotation) * 3.1415927 * .00048828125;
        const auto sine_index = static_cast<std::int32_t>(static_cast<float>(angle * simulation_sine_samples_per_radian));
        const auto cosine_index = static_cast<std::int32_t>(static_cast<float>((static_cast<float>(angle) + 1.5707964) * simulation_sine_samples_per_radian));
        const auto sine = simulation_sine_at_index(sine_index);
        const auto cosine = simulation_sine_at_index(cosine_index);
        const auto ratio = (right - left) / (bottom - top);
        for (auto& vertex : vertices) {
            const auto x = vertex.position[0] - static_cast<float>(pivot.x);
            const auto y = vertex.position[1] - static_cast<float>(pivot.y);
            vertex.position[0] = static_cast<float>(pivot.x) + x * cosine - y * ratio * sine;
            vertex.position[1] = static_cast<float>(pivot.y) + x / ratio * sine + y * cosine;
        }
    }
    draw.vertices.insert(draw.vertices.end(), vertices.begin(), vertices.end());
    UiDrawCommand command;
    command.texture = *texture;
    command.clip = bounds(clip);
    command.first_index = index(draw.indices.size());
    command.index_count = 12;
    MeshTextureSlot slot;
    command.material = derive_mesh_material(0, 0, &slot, texture->get());
    //Native source flag0x100000 selects the top mip only.
    command.material.flags |= 0x40000u;
    command.native_model_filter = true;
    command.depth_buffered = true;
    draw.indices.insert(draw.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3, base + 4, base + 6, base + 7, base + 4, base + 7, base + 5 });
    draw.commands.push_back(std::move(command));
}
} //namespace

UiControlPresenter::UiControlPresenter(UiAssetLibrary& assets, UiFontLibrary& fonts, UiCurrencyFormatter currency) : assets_(assets), fonts_(fonts), currency_(std::move(currency)) {}

void UiControlPresenter::append(UiDrawList& draw, UiControl& control, UiRect clip, std::uint32_t pixel_width, std::uint32_t pixel_height) {
    if (!control.runtime || !control.runtime->owns(&control) || !control.visible()) {
        return;
    }
    if (!pixel_width || !pixel_height) {
        throw std::invalid_argument("UI presentation requires a nonzero viewport");
    }
    if (control.meter_presentation) {
        append_meter(draw, assets_, *control.meter_presentation, control.attachment_rect, clip, control.value, control.secondary_placement.depth, control.secondary_placement.rotation, control.secondary_placement.pivot);
    }
    if (control.kind != UiControlKind::list_control && control.kind != UiControlKind::list_box) {
        return;
    }
    const auto font = fonts_.slot(control.font, pixel_width);
    if (!font) {
        throw std::runtime_error(std::format("UI list font: {}: {}", font.error().resource, font.error().reason));
    }
    if (control.row_height_font_padding) {
        //520B90 and the other native row factories use the actual bitmap
        //font's line advance, physical height and their own logical padding.
        const auto height = 1536LL * (**font).line_advance / pixel_height + static_cast<std::int64_t>(*control.row_height_font_padding);
        if (height <= 0 || height > std::numeric_limits<std::int32_t>::max()) {
            throw std::invalid_argument("UI list row height is outside its domain");
        }
        control.row_height = static_cast<std::int32_t>(height);
    }
    control.runtime->refresh_list(control, false);
    const auto view = intersect(clip, control.viewport);
    if (view.width() <= 0 || view.height() <= 0 || control.row_height <= 0) {
        return;
    }
    const auto first = static_cast<std::size_t>(control.scroll_offset);
    const auto last = std::min(control.rows.size(), first + static_cast<std::size_t>(control.visible_rows));
    if (control.list_highlight_asset_hash && control.selected_id >= 0 && static_cast<std::size_t>(control.selected_id) >= first && static_cast<std::size_t>(control.selected_id) < last) {
        const auto top = control.viewport.top + (control.selected_id - control.scroll_offset) * control.row_height;
        UiModelPlacement placement;
        placement.bounds = bounds({ control.viewport.left, top, control.viewport.right, top + control.row_height });
        placement.clip = bounds(view);
        placement.pivot = { static_cast<float>(control.viewport.left + control.viewport.width() / 2), static_cast<float>(top + control.row_height / 2) };
        placement.depth = static_cast<std::int16_t>(std::clamp(control.render_depth + 2, 1, 256));
        placement.rotation = static_cast<std::int16_t>(control.secondary_placement.rotation);
        if (!assets_.append(draw, control.list_highlight_asset_hash, placement)) {
            throw std::runtime_error(std::format("UI list original highlight asset is missing: {}", control.list_highlight_asset_hash));
        }
    }
    for (auto row = first; row < last; ++row) {
        const auto top = control.viewport.top + static_cast<std::int32_t>(row - first) * control.row_height;
        for (std::size_t column = 0; column < control.rows[row].cells.size(); ++column) {
            const auto& cell = control.rows[row].cells[column];
            if (!cell.visible) {
                continue;
            }
            UiListColumnPresentation fallback;
            fallback.format = cell.numeric() ? UiListCellFormat::signed_integer : UiListCellFormat::text;
            const auto& presentation = column < control.column_presentations.size() ? control.column_presentations[column] : fallback;
            auto left = control.viewport.left, right = control.viewport.right;
            if (column < control.columns.size()) {
                left = control.columns[column].left;
                right = control.columns[column].right;
            } else if (column) {
                throw std::invalid_argument("UI list cell has no column bounds");
            }
            const UiRect rectangle { left, top, right, top + control.row_height };
            const auto cell_clip = intersect(view, rectangle);
            if (cell_clip.width() <= 0 || cell_clip.height() <= 0) {
                continue;
            }
            if (presentation.format == UiListCellFormat::meter) {
                if (!cell.numeric()) {
                    throw std::invalid_argument("UI meter column requires a numeric cell");
                }
                append_meter(draw, assets_, presentation.meter, rectangle, cell_clip, cell.number(), control.render_depth + 3, 0, { left + (right - left) / 2, top + control.row_height / 2 });
                continue;
            }
            if (presentation.format == UiListCellFormat::model_state) {
                if (!cell.numeric() || !presentation.model_asset_hash) {
                    throw std::invalid_argument("UI model column requires a numeric state and original asset");
                }
                UiModelPlacement placement;
                auto model_rectangle = rectangle;
                model_rectangle.left += presentation.model_inset.x;
                model_rectangle.top += presentation.model_inset.y;
                model_rectangle.right -= presentation.model_inset.x;
                model_rectangle.bottom -= presentation.model_inset.y;
                placement.bounds = bounds(model_rectangle);
                placement.clip = bounds(cell_clip);
                placement.depth = static_cast<std::int16_t>(std::clamp(control.render_depth + 3, 1, 256));
                placement.part_value = static_cast<std::uint16_t>(cell.number());
                const auto color = cell.color.value_or(presentation.color.value_or(std::array<std::uint8_t, 4> { 255, 255, 255, 255 }));
                placement.color = { color[0], color[1], color[2], color[3] };
                //A registered model's missing part and the shipped staffstat binding
                //are native null attachments. Other absent models remain errors.
                if (!assets_.append(draw, presentation.model_asset_hash, placement) && !assets_.find(presentation.model_asset_hash) && !assets_.is_original_null_binding(presentation.model_asset_hash)) {
                    throw std::runtime_error(std::format("UI list original model asset is missing: {}", presentation.model_asset_hash));
                }
                continue;
            }
            std::u16string formatted;
            std::u16string_view text;
            (do {
                match (presentation.format) {
                    case UiListCellFormat::text => {
                        if (cell.numeric()) {
                            throw std::invalid_argument("UI text column requires a text cell");
                        }
                        text = cell.text();
                    }
                    case UiListCellFormat::signed_integer => {
                        if (!cell.numeric()) {
                            throw std::invalid_argument("UI numeric column requires a numeric cell");
                        }
                        formatted = number_text(cell.number());
                        text = formatted;
                    }
                    case UiListCellFormat::signed_currency => {
                        if (!cell.numeric()) {
                            throw std::invalid_argument("UI currency column requires a numeric cell");
                        }
                        if (!currency_) {
                            throw std::logic_error("UI signed-currency formatter is not bound");
                        }
                        if (presentation.zero_blank && cell.number() == 0) {
                            do_return;
                        }
                        formatted = currency_(cell.number());
                        text = formatted;
                    }
                    case UiListCellFormat::meter or UiListCellFormat::model_state => {
                    }
                    case _ => {
                    }
                }
            });
            if (cell.display_text) {
                text = *cell.display_text;
            }
            if (text.empty()) {
                continue;
            }
            auto column_font = font;
            if (presentation.font && *presentation.font != control.font) {
                column_font = fonts_.slot(*presentation.font, pixel_width);
            }
            if (!column_font) {
                throw std::runtime_error(std::format("UI list cell font: {}: {}", column_font.error().resource, column_font.error().reason));
            }
            UiTextStyle style;
            style.color = cell.color.value_or(presentation.color.value_or(control.rows[row].color.value_or(control.color)));
            style.horizontal = static_cast<UiTextHorizontal>(std::clamp(presentation.horizontal_alignment, 0, 2));
            style.vertical = static_cast<UiTextVertical>(std::clamp(presentation.vertical_alignment, 0, 2));
            style.wrap = presentation.text_constraints.width;
            style.scale = { 2048.f / static_cast<float>(pixel_width), 1536.f / static_cast<float>(pixel_height) };
            style.depth = depth(std::max(control.render_depth + 3, 1));
            auto area = rectangle;
            if (column == 0 && presentation.format == UiListCellFormat::text) {
                //854E0A applies20 logical units per parent link to the first TEXT
                //column only. Other columns retain their exact descriptor bounds.
                auto parent = control.runtime->parent_list_row(control, row);
                std::size_t tree_depth {};
                while (parent) {
                    if (++tree_depth > control.rows.size()) {
                        throw std::logic_error("UI list parent cycle during presentation");
                    }
                    parent = control.runtime->parent_list_row(control, *parent);
                }
                area.left = static_cast<std::int32_t>(std::min<std::int64_t>(area.right, static_cast<std::int64_t>(area.left) + static_cast<std::int64_t>(tree_depth) * 20));
            }
            //Native512680 truncates each logical offset through physical pixels.
            const auto dx = static_cast<std::int32_t>(static_cast<float>(static_cast<std::int64_t>(pixel_width) * presentation.text_offset.x / 2048) * style.scale[0]);
            const auto dy = static_cast<std::int32_t>(static_cast<float>(static_cast<std::int64_t>(pixel_height) * presentation.text_offset.y / 1536) * style.scale[1]);
            if (style.horizontal == UiTextHorizontal::right) {
                area.right -= dx;
                area.bottom -= dy;
            } else {
                area.left += dx;
                area.top += dy;
            }
            const auto rendered = fonts_.append(draw, **column_font, text, bounds(area), style, bounds(cell_clip));
            if (!rendered) {
                throw std::runtime_error(std::format("UI list cell text: {}: {}", rendered.error().resource, rendered.error().reason));
            }
        }
    }
}
} //namespace simcoaster
