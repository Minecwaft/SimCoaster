#include "simcoaster/ui_graph.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
using Bounds = std::array<float, 4>;

Bounds bounds(UiRect rectangle) {
    return { static_cast<float>(rectangle.left), static_cast<float>(rectangle.top), static_cast<float>(rectangle.right), static_cast<float>(rectangle.bottom) };
}

struct Label {
    std::u16string text;
    std::int32_t left {}, top {}, right {}, bottom {};
    RgbaPixel color {};
    bool right_aligned {};
};

struct GraphKey {
    std::int32_t width {}, height {}, lower {}, upper {}, divisions {}, horizontal {};
    std::vector<std::pair<std::uint32_t, std::vector<std::int32_t>>> series;
    std::vector<UiGraphSeriesStyle> styles;
    bool operator==(const GraphKey&) const = default;
};

std::u16string integer(std::int32_t value) {
    const auto text = std::to_string(value);
    return { text.begin(), text.end() };
}

constexpr RgbaPixel axis { 0, 200, 0, 255 }, grid { 0, 100, 0, 255 };
} //namespace

struct UiGraphPresenter::State {
    UiFontLibrary& fonts;
    std::optional<GraphKey> key;
    TextureImagePtr texture;
    std::vector<Label> labels;

    explicit State(UiFontLibrary& library) : fonts(library) {}

    void rebuild(GraphKey next) {
        if (next.width <= 24 || next.height <= 24 || next.width > 16384 || next.height > 16384 || next.divisions <= 0 || next.horizontal <= 0) {
            throw std::invalid_argument("Statistics graph has invalid surface or divisions");
        }
        TextureImage image;
        image.width = static_cast<std::uint32_t>(next.width);
        image.height = static_cast<std::uint32_t>(next.height);
        image.alpha = TextureAlpha::opaque;
        image.pixels.assign(static_cast<std::size_t>(image.width) * image.height, RgbaPixel { 0, 0, 0, 255 });
        labels.clear();
        const auto pixel = [&](std::int32_t x, std::int32_t y, RgbaPixel color) {
            if (x >= 0 && y >= 0 && x < next.width && y < next.height) {
                image.pixels[static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)] = color;
            }
        };
        //Render at physical resolution, so axes and endpoint markers retain
        //native one-pixel strokes rather than scaling with the logical canvas.
        const auto line = [&](std::int32_t x, std::int32_t y, std::int32_t end_x, std::int32_t end_y, RgbaPixel color) {
            const auto dx = std::abs(end_x - x), dy = -std::abs(end_y - y);
            const auto sx = x < end_x ? 1 : -1, sy = y < end_y ? 1 : -1;
            auto error = dx + dy;
            for (;;) {
                pixel(x, y, color);
                if (x == end_x && y == end_y) {
                    break;
                }
                const auto twice = error * 2;
                if (twice >= dy) {
                    error += dy;
                    x += sx;
                }
                if (twice <= dx) {
                    error += dx;
                    y += sy;
                }
            }
        };
        const std::int32_t left = 4, top = 4, right = next.width - 20, bottom = next.height - 20;
        const auto width = right - left, height = bottom - top;
        const auto skip = next.horizontal == 12 ? 1 : next.horizontal == 36 ? 3 : next.horizontal == 144 ? 12 : 0;
        if (!skip) {
            throw std::invalid_argument("Statistics graph requires12,36 or144 months");
        }
        const auto horizontal_step = static_cast<float>(width) / static_cast<float>(next.horizontal);
        const auto vertical_step = static_cast<float>(height) / static_cast<float>(next.divisions);
        auto x = static_cast<float>(left) + horizontal_step;
        for (std::int32_t interval = 1; interval <= next.horizontal; ++interval) {
            if (interval % skip == 0) {
                const auto at = static_cast<std::int32_t>(x);
                line(at, top, at, bottom, grid);
                labels.push_back({ integer(interval), std::max(at, 2), std::max(bottom + 4, 2), next.width, next.height, axis, false });
            }
            x += horizontal_step;
        }
        auto y = static_cast<float>(bottom) - vertical_step + 1.f;
        for (std::int32_t i = 0; i < next.divisions; ++i) {
            const auto at = static_cast<std::int32_t>(y);
            line(left, at, right, at, grid);
            y -= vertical_step;
        }
        const auto span = static_cast<std::int64_t>(next.upper) - next.lower;
        auto zero = bottom + 1;
        if (next.lower < 0 && span > 0) {
            zero = static_cast<std::int32_t>(bottom + static_cast<std::int64_t>(next.lower) * height / span);
        }
        //Extremely negative ranges can put the zero axis outside this surface;
        //native line dispatch clips it without moving it onto the plot border.
        if (zero >= 0 && zero < next.height) {
            line(left - 1, zero, right + 1, zero, axis);
        }
        line(left - 1, top - 1, left - 1, bottom + 1, axis);
        for (const auto& [history_index, samples] : next.series) {
            if (samples.empty()) {
                continue;
            }
            const auto style = std::ranges::find(next.styles, history_index, &UiGraphSeriesStyle::history_index);
            if (style == next.styles.end()) {
                throw std::invalid_argument("Statistics graph series lacks its native style");
            }
            const auto sample_y = [&](std::int32_t value) {
                const auto result = span ? bottom - static_cast<std::int64_t>(height) * (static_cast<std::int64_t>(value) - next.lower) / span : bottom;
                return static_cast<std::int32_t>(std::clamp<std::int64_t>(result, top, bottom));
            };
            auto current_x = static_cast<float>(left);
            auto end_x = left, end_y = sample_y(samples.front());
            for (std::size_t sample = 1; sample < samples.size(); ++sample) {
                const auto start_x = static_cast<std::int32_t>(std::clamp(current_x, static_cast<float>(left), static_cast<float>(right)));
                const auto start_y = sample_y(samples[sample - 1]);
                end_x = static_cast<std::int32_t>(std::clamp(current_x + horizontal_step, static_cast<float>(left), static_cast<float>(right)));
                end_y = sample_y(samples[sample]);
                line(start_x, start_y, end_x, end_y, style->color);
                if (next.horizontal < 144) {
                    line(end_x - 1, end_y - 1, end_x + 1, end_y - 1, style->color);
                    line(end_x + 1, end_y - 1, end_x + 1, end_y + 1, style->color);
                    line(end_x + 1, end_y + 1, end_x - 1, end_y + 1, style->color);
                    line(end_x - 1, end_y + 1, end_x - 1, end_y - 1, style->color);
                }
                current_x += horizontal_step;
            }
            labels.push_back({ integer(samples.back()), std::max(left, 2), std::max(end_y - 6, 2), end_x, bottom, style->color, true });
        }
        texture = std::make_shared<const TextureImage>(std::move(image));
        key = std::move(next);
    }
};

UiGraphPresenter::UiGraphPresenter(UiFontLibrary& fonts) : state_(std::make_unique<State>(fonts)) {}

UiGraphPresenter::~UiGraphPresenter() = default;

void UiGraphPresenter::append(UiDrawList& draw, const UiControl& control, const StatisticsGraphUiState& graph, std::span<const UiGraphSeriesStyle> styles, UiRect clip, std::uint32_t pixel_width, std::uint32_t pixel_height) {
    if (!pixel_width || !pixel_height || control.rect.width() <= 0 || control.rect.height() <= 0) {
        return;
    }
    GraphKey key;
    key.width = static_cast<std::int32_t>(static_cast<std::int64_t>(control.rect.width()) * pixel_width / 2048);
    key.height = static_cast<std::int32_t>(static_cast<std::int64_t>(control.rect.height()) * pixel_height / 1536);
    //A minimized/tiny viewport has no drawable native plot.
    if (key.width <= 24 || key.height <= 24) {
        return;
    }
    key.lower = graph.lower;
    key.upper = graph.upper;
    key.divisions = graph.divisions;
    key.horizontal = graph.range == StatisticsGraphRange::one_year ? 12 : graph.range == StatisticsGraphRange::three_years ? 36 : 144;
    key.styles.assign(styles.begin(), styles.end());
    for (const auto& series : graph.series) {
        key.series.emplace_back(series.history_index, series.samples);
    }
    if (!state_->key || *state_->key != key) {
        state_->rebuild(std::move(key));
    }
    auto rectangle = control.rect;
    clip = { std::max(clip.left, rectangle.left), std::max(clip.top, rectangle.top), std::min(clip.right, rectangle.right), std::min(clip.bottom, rectangle.bottom) };
    if (clip.width() <= 0 || clip.height() <= 0) {
        return;
    }
    if (draw.vertices.size() > std::numeric_limits<std::uint32_t>::max() - 4 || draw.indices.size() > std::numeric_limits<std::uint32_t>::max() - 6) {
        throw std::length_error("Statistics graph exceeds UI draw index domain");
    }
    const auto base = static_cast<std::uint32_t>(draw.vertices.size());
    const auto b = bounds(rectangle);
    const auto z = ui_attachment_depth(std::max(control.render_depth + 1, 1));
    draw.vertices.insert(draw.vertices.end(), { { { b[0], b[1], z * .1f }, { 0, 0 } }, { { b[2], b[1], z * .1f }, { 1, 0 } }, { { b[2], b[3], z * .1f }, { 1, 1 } }, { { b[0], b[3], z * .1f }, { 0, 1 } } });
    UiDrawCommand command;
    command.texture = state_->texture;
    command.clip = bounds(clip);
    command.first_index = static_cast<std::uint32_t>(draw.indices.size());
    command.index_count = 6;
    command.native_model_filter = true;
    //The native graph is another CPU-backed tiled UI surface (6FCB00).
    command.material.flags = 0x07060556u;
    command.depth_buffered = true;
    draw.indices.insert(draw.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    draw.commands.push_back(std::move(command));
    auto font = state_->fonts.slot(10, pixel_width);
    if (!font) {
        throw std::runtime_error("Statistics graph bitmap font: " + font.error().reason);
    }
    const auto sx = static_cast<float>(rectangle.width()) / static_cast<float>(state_->texture->width);
    const auto sy = static_cast<float>(rectangle.height()) / static_cast<float>(state_->texture->height);
    const auto local_bounds = [&](const Label& label) -> Bounds {
        return { static_cast<float>(rectangle.left) + static_cast<float>(label.left) * sx, static_cast<float>(rectangle.top) + static_cast<float>(label.top) * sy, static_cast<float>(rectangle.left) + static_cast<float>(label.right) * sx, static_cast<float>(rectangle.top) + static_cast<float>(label.bottom) * sy };
    };
    for (const auto& label : state_->labels) {
        UiTextStyle style;
        style.color = label.color;
        style.wrap = false;
        style.depth = z;
        style.scale = { 2048.f / static_cast<float>(pixel_width), 1536.f / static_cast<float>(pixel_height) };
        style.horizontal = label.right_aligned ? UiTextHorizontal::right : UiTextHorizontal::left;
        auto area = local_bounds(label);
        if (label.right_aligned) {
            const auto measured = state_->fonts.measure(**font, label.text, area, style);
            if (!measured) {
                throw std::runtime_error("Statistics endpoint label metrics: " + measured.error().reason);
            }
            area[2] = std::max(area[2], area[0] + measured->bounds[2] - measured->bounds[0]);
        }
        const auto rendered = state_->fonts.append(draw, **font, label.text, area, style, bounds(clip));
        if (!rendered) {
            throw std::runtime_error("Statistics graph label: " + rendered.error().reason);
        }
    }
}
} //namespace simcoaster
