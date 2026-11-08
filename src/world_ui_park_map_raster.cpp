#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_staff.hpp"
#include "world_ui_park_map_internal.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
RgbaPixel packed(std::int32_t color) {
    return mesh_argb_color(std::bit_cast<std::uint32_t>(color));
}

RgbaPixel ramp(std::int32_t percentage, RgbaPixel start, RgbaPixel middle, RgbaPixel end) {
    percentage = std::clamp(percentage, 0, 100);
    const auto lower = percentage < 50 ? start : middle, upper = percentage < 50 ? middle : end;
    const auto weight = ((percentage < 50 ? percentage : percentage - 50) << 16) / 50;
    RgbaPixel output;
    for (std::size_t i = 0; i < 4; ++i)
        output[i] =
            static_cast<std::uint8_t>(lower[i] + ((weight * (static_cast<std::int32_t>(upper[i]) - lower[i])) >> 16));
    return output;
}

bool bad_object(WorldRuntime& c, const ParkObjectRuntimeView& object, std::int32_t category) {
    if (category == 0)
        return object.state == ParkObjectState::condemned
            || object.state == ParkObjectState::broken_down
            || static_cast<std::uint8_t>(fixed::truncate_integer(object.repair_state)) < 20;
    const auto flags = std::to_underlying(object.capabilities);
    return object.state == ParkObjectState::condemned
        || (category == 1 && (flags & 0x41u) && park_object_requires_staff_service(c, object))
        || (category == 2 && (flags & 1u) && park_object_requires_staff_service(c, object));
}
} //namespace

void WorldParkMapUiController::State::pixel(std::int32_t x, std::int32_t y, RgbaPixel color) {
    if (x < 0
        || y < 0
        || x >= static_cast<std::int32_t>(raster->width)
        || y >= static_cast<std::int32_t>(raster->height)
        || !color[3])
        return;
    //Raster surfaces use bottom-origin map coordinates. TextureImage owns the
    //normalized top-left image used by the modern UI compositor.
    auto& destination = raster->pixels[(raster->height - 1u - static_cast<std::uint32_t>(y)) * raster->width
                                       + static_cast<std::uint32_t>(x)];
    const auto alpha = static_cast<std::uint32_t>(color[3]);
    for (std::size_t i = 0; i < 3; ++i)
        destination[i] = static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(color[i]) * alpha + static_cast<std::uint32_t>(destination[i]) * (255u - alpha))
            / 255u);
    destination[3] = 255;
}

void WorldParkMapUiController::State::rectangle(std::int32_t l,
                                                std::int32_t t,
                                                std::int32_t r,
                                                std::int32_t b,
                                                RgbaPixel color,
                                                bool outline) {
    if (outline) {
        for (auto x = l; x < r; ++x) {
            pixel(x, t, color);
            pixel(x, b - 1, color);
        }
        for (auto y = t + 1; y < b - 1; ++y) {
            pixel(l, y, color);
            pixel(r - 1, y, color);
        }
    } else
        for (auto y = t; y < b; ++y)
            for (auto x = l; x < r; ++x)
                pixel(x, y, color);
}

void WorldParkMapUiController::State::line(float ax, float ay, float bx, float by, RgbaPixel color) {
    //7BD7F0: Bresenham's signed decision term drives three coverage samples
    //normal to the major axis. The last endpoint is deliberately excluded.
    auto x = fixed::truncate_integer(ax), y = fixed::truncate_integer(ay);
    auto last_x = fixed::truncate_integer(bx), last_y = fixed::truncate_integer(by);
    const auto steep = std::abs(last_y - y) > std::abs(last_x - x);
    if ((steep && y > last_y) || (!steep && x > last_x)) {
        std::swap(x, last_x);
        std::swap(y, last_y);
    }
    const auto dx = last_x - x, dy = last_y - y;
    const auto major = steep ? dy : dx, minor = steep ? std::abs(dx) : std::abs(dy);
    if (!major)
        return;
    const auto direction = (steep ? dx : dy) < 0 ? -1 : 1;
    const auto reciprocal = 1. / (2. * std::hypot(static_cast<double>(dx), static_cast<double>(dy)));
    const auto neighbor_distance = 2. * major * reciprocal;
    auto decision = 2 * minor - major, previous_error = 0;
    const auto sample = [&](std::int32_t sx, std::int32_t sy, double distance) {
        distance = std::abs(distance);
        if (distance > 1
            || sx < 0
            || sy < 0
            || sx >= static_cast<std::int32_t>(raster->width)
            || sy >= static_cast<std::int32_t>(raster->height))
            return;
        auto& destination = raster->pixels[(raster->height - 1u - static_cast<std::uint32_t>(sy)) * raster->width
                                           + static_cast<std::uint32_t>(sx)];
        const auto coverage = static_cast<std::uint32_t>((1. - distance) * 255.);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto mixed = coverage * color[channel] + (255u - coverage) * destination[channel];
            destination[channel] = static_cast<std::uint8_t>(((mixed >> 8) + mixed + 1u) >> 8);
        }
    };
    for (auto step = 0; step < major; ++step) {
        const auto distance = previous_error * reciprocal;
        sample(x, y, distance);
        sample(x + (steep ? direction : 0), y + (steep ? 0 : direction), neighbor_distance - distance);
        sample(x - (steep ? direction : 0), y - (steep ? 0 : direction), neighbor_distance + distance);
        if (decision >= 0) {
            previous_error = decision - major;
            decision += 2 * (minor - major);
            if (steep)
                x += direction;
            else
                y += direction;
        } else {
            previous_error = decision + major;
            decision += 2 * minor;
        }
        if (steep)
            ++y;
        else
            ++x;
    }
}

void WorldParkMapUiController::State::circle(float cx, float cy, float size, RgbaPixel color) {
    const auto x = fixed::truncate_integer(cx), y = fixed::truncate_integer(cy), radius = fixed::truncate_integer(size);
    const auto eight = [&](std::int32_t a, std::int32_t b, RgbaPixel rgba) {
        for (const auto point : std::array<std::array<std::int32_t, 2>, 8>{ { { x + a, y + b },
                                                                              { x - a, y + b },
                                                                              { x + a, y - b },
                                                                              { x - a, y - b },
                                                                              { x + b, y + a },
                                                                              { x - b, y + a },
                                                                              { x + b, y - a },
                                                                              { x - b, y - a } } })
            pixel(point[0], point[1], rgba);
    };
    //Native7BEC70:16 successively attenuated alpha records, plus paired
    //outer/inward eightfold samples at rounded fractional coverage levels.
    std::array<RgbaPixel, 16> fades;
    fades[0] = color;
    for (std::size_t i = 1; i < fades.size(); ++i) {
        color[3] = static_cast<std::uint8_t>(static_cast<double>(color[3]) * .9375);
        fades[i] = color;
    }
    auto edge = radius, sample = 0, previous = 0;
    eight(radius, 0, fades[0]);
    if (radius <= 0)
        return;
    do {
        ++sample;
        const auto intersection =
            std::sqrt(static_cast<double>(radius) * radius - static_cast<double>(sample) * sample);
        const auto coverage =
            static_cast<std::int32_t>(std::floor((std::ceil(intersection) - intersection) * 15. + .5));
        if (coverage <= previous)
            --edge;
        previous = coverage;
        eight(edge, sample, fades[static_cast<std::size_t>(coverage)]);
        eight(edge - 1, sample, fades[static_cast<std::size_t>(15 - coverage)]);
    } while (sample < edge);
}

void WorldParkMapUiController::State::draw_cells(bool alert) {
    const auto cw = fixed::truncate_integer(480. / width), ch = fixed::truncate_integer(425. / height);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto& cell = cells[y * 128 + x];
            const auto left = 15 + static_cast<std::int32_t>(x) * cw, top = 42 + static_cast<std::int32_t>(y) * ch;
            ParkObjectRuntimeView* object{};
            if (cell.object) {
                const auto* header = c.entities.at(cell.object).payload;
                if (!header
                    || header->kind != ThingKind::park_object
                    || header->retirement_pending
                    || retired.contains(cell.object))
                    continue;
                object = c.entities.payload<ParkObjectRuntimeView>(cell.object);
                if (!passes(*object))
                    continue;
            }
            bool draw = false, borders = false;
            RgbaPixel color{};
            const auto default_color = packed(-8794624);
            match (cell.kind) {
                case CellClass::ride => {
                    draw = alert == bad_object(c, *object, 0);
                    borders = true;
                    color = default_color;
                }
                case CellClass::litter or CellClass::security or CellClass::feature => {
                    draw = alert == bad_object(c, *object, 1);
                    borders = true;
                    color = default_color;
                }
                case CellClass::chill => {
                    draw = alert == (object->state == ParkObjectState::condemned);
                    borders = true;
                    color = primary_filter ? packed(-16776961) : default_color;
                }
                case CellClass::facility => {
                    draw = alert == bad_object(c, *object, 2);
                    borders = true;
                    color = ((std::to_underlying(object->capabilities) & (1024u | 2u)) && primary_filter)
                              ? packed(-16776961)
                              : default_color;
                }
                case CellClass::shop or CellClass::sideshow => {
                    draw = !alert;
                    borders = !alert;
                    color = default_color;
                }
                case CellClass::track => {
                    draw = !alert;
                    borders = !alert;
                    color = { 0x8E, 0x8E, 0x8E, 255 };
                }
                case CellClass::hoarding => {
                    draw = !alert;
                    color = packed(-16771328);
                }
                case CellClass::path => {
                    draw = !alert;
                    color = packed(-868224);
                }
                case CellClass::queue => {
                    draw = !alert;
                    color = packed(-1069215);
                }
                case CellClass::outside => {
                    draw = !alert;
                    color = packed(-2147477760);
                }
                case _ => {}
            }
            if (!draw)
                continue;
            if (alert)
                color = alternate ? RgbaPixel{ 0, 0, 0, 255 } : RgbaPixel{ 255, 0, 0, 255 };
            rectangle(left, top, left + cw, top + ch, color);
            if (!borders)
                continue;
            const auto same = [&](std::int32_t nx, std::int32_t ny) {
                return nx >= 0
                    && ny >= 0
                    && nx < 128
                    && ny < 128
                    && cells[static_cast<std::size_t>(ny) * 128 + static_cast<std::size_t>(nx)] == cell;
            };
            const RgbaPixel black{ 0, 0, 0, 255 };
            if (!same(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y) - 1))
                rectangle(left, top, left + cw, top + 1, black);
            if (!same(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y) + 1))
                rectangle(left, top + cw - 1, left + cw, top + cw, black);
            if (!same(static_cast<std::int32_t>(x) - 1, static_cast<std::int32_t>(y)))
                rectangle(left, top, left + 1, top + cw, black);
            if (!same(static_cast<std::int32_t>(x) + 1, static_cast<std::int32_t>(y)))
                rectangle(left + cw - 1, top, left + cw, top + cw, black);
        }
    if (!alert)
        return;
    for (std::size_t i = 0; i < markers.size(); ++i) {
        const auto& marker = markers[i];
        if (marker.source_kind != 2
            || !marker.visible
            || !marker.identity
            || !is_staff_record_kind(marker.identity->kind))
            continue;
        const auto& bounds = reinterpret_cast<const StaffRuntimeRecord*>(marker.identity)->area.bounds;
        if (bounds.start_x && bounds.start_y && bounds.end_x < 127 && bounds.end_y < 127)
            rectangle(15 + cw * bounds.start_x,
                      42 + ch * bounds.start_y,
                      15 + cw * (bounds.end_x + 1),
                      42 + ch * (bounds.end_y + 1),
                      hover == i ? RgbaPixel{ 0x19, 0x19, 0xFA, 0xC8 } : RgbaPixel{ 0x19, 0x19, 0x96, 0xC8 },
                      true);
    }
}

void WorldParkMapUiController::State::draw_heat() {
    heat.fill(0);
    weights.fill(0);
    const auto& p = c.visitors.needs_balance.PeepInfo;
    for (const auto& marker : markers) {
        if (marker.source_kind || !marker.identity)
            continue;
        const auto& person = *reinterpret_cast<const PersonRuntimeView*>(marker.identity);
        const auto x = person.base.x >> 8, y = person.base.y >> 8;
        const auto index = static_cast<std::size_t>(x) + static_cast<std::size_t>(width) * y;
        if (index >= heat.size())
            throw std::runtime_error("Park Map visitor heat cell exceeds native array");
        const auto happiness = static_cast<std::uint8_t>(fixed::truncate_integer(person.happiness));
        std::int32_t value{}, threshold{}, denominator{};
        match (heat_choice) {
            case 0 => {
                value = happiness;
                threshold = p.ThoughtBubbleHappy;
            }
            case 1 => {
                value = happiness;
                threshold = p.ThoughtBubbleUnhappy;
            }
            case 2 => {
                value = static_cast<std::uint8_t>(fixed::truncate_integer(person.needs.sickness));
                threshold = p.ThoughtBubbleSick;
            }
            case 3 => {
                value = static_cast<std::uint8_t>(fixed::truncate_integer(person.needs.hunger));
                threshold = p.ThoughtBubbleHungry;
            }
            case 4 => {
                value = static_cast<std::uint8_t>(fixed::truncate_integer(person.needs.thirst));
                threshold = p.ThoughtBubbleThirsty;
            }
            case _ => {
                //Native Bored and None both accumulate zero.
            }
        }
        if (heat_choice < 5 && (heat_choice == 1 ? value <= threshold : value >= threshold)) {
            denominator = heat_choice == 1 ? threshold : 100 - threshold;
            if (!denominator)
                throw std::runtime_error("Park Map thought threshold has a zero native denominator");
            heat[index] =
                fixed::add(heat[index], 100 * (heat_choice == 1 ? threshold - value : value - threshold) / denominator);
        }
        ++weights[index];
    }
    for (std::size_t i = 0; i < heat.size(); ++i) {
        if (weights[i] > 0)
            heat[i] /= weights[i];
        heat[i] /= 36;
        if (heat[i] > 0)
            heat[i] += 2;
        heat[i] = -heat[i];
        weights[i] = 0;
    }
    //7C4FE0 preserves negative source cells while propagating two diamond
    //rings. Other source cells are never overwritten by adjacent contributions.
    for (std::int32_t y = 7; y < static_cast<std::int32_t>(height) - 7; ++y)
        for (std::int32_t x = 7; x < static_cast<std::int32_t>(width) - 7; ++x) {
            const auto origin = heat[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
            if (origin >= 0)
                continue;
            for (std::int32_t radius = 1; radius < 3; ++radius) {
                const auto contribution = std::max(0, -origin - radius);
                for (std::int32_t t = 0; t < radius; ++t)
                    for (const auto point :
                         std::array<std::array<std::int32_t, 2>, 4>{ { { x - t, y + t - radius },
                                                                       { x + t - radius, y + t },
                                                                       { x + t, y + radius - t },
                                                                       { x + radius - t, y - t } } }) {
                        const auto index =
                            static_cast<std::size_t>(point[1]) * width + static_cast<std::size_t>(point[0]);
                        if (heat[index] < 0)
                            continue;
                        heat[index] += contribution;
                        weights[index] += contribution > 0 ? 1 : 0;
                    }
            }
        }
    for (std::size_t i = 0; i < heat.size(); ++i) {
        if (heat[i] < 0)
            heat[i] = -heat[i];
        if (weights[i] > 1)
            heat[i] /= weights[i];
    }
    const auto cw = fixed::truncate_integer(480. / width), ch = fixed::truncate_integer(425. / height);
    for (std::uint32_t y = 0; y + 1 < height; ++y)
        for (std::uint32_t x = 0; x + 1 < width; ++x) {
            const auto index = static_cast<std::size_t>(y) * width + x;
            const std::array values{ heat[index] * 14,
                                     heat[index + 1] * 14,
                                     heat[index + width] * 14,
                                     heat[index + width + 1] * 14 };
            if (std::ranges::all_of(values, [](auto value) {
                    return value <= 0;
                }))
                continue;
            std::array<RgbaPixel, 4> corners;
            for (std::size_t i = 0; i < 4; ++i)
                corners[i] =
                    values[i] ? ramp(values[i], heat_colors[1], heat_colors[2], heat_colors[3]) : heat_colors[0];
            for (auto py = 0; py < ch; ++py)
                for (auto px = 0; px < cw; ++px) {
                    const auto fx = static_cast<float>(px) / static_cast<float>(cw),
                               fy = static_cast<float>(py) / static_cast<float>(ch);
                    RgbaPixel color;
                    for (std::size_t i = 0; i < 4; ++i)
                        color[i] = static_cast<std::uint8_t>(corners[0][i] * (1.f - fx) * (1.f - fy)
                                                             + corners[1][i] * fx * (1.f - fy)
                                                             + corners[2][i] * (1.f - fx) * fy
                                                             + corners[3][i] * fx * fy);
                    pixel(15 + static_cast<std::int32_t>(x) * cw + px,
                          42 + static_cast<std::int32_t>(y) * ch + py,
                          color);
                }
        }
}

void WorldParkMapUiController::State::draw_profiles() {
    if (primary_filter != 0 && primary_filter != 4 && primary_filter != 2)
        return;
    if (!c.resources.meshes || !c.resources.meshes->coasters())
        return;
    const auto scale = static_cast<float>(480. / (static_cast<double>(width) * 10.));
    for (const auto& binding : c.resources.meshes->coasters()->bindings) {
        if (!binding.park_object || !binding.track)
            continue;
        const auto id = binding.park_object->base.header.id;
        if (retired.contains(id)
            || id >= c.entities.size()
            || c.entities[id].payload != &binding.park_object->base.header)
            continue;
        const auto& object = *binding.park_object;
        const auto color = ramp(static_cast<std::uint8_t>(fixed::truncate_integer(object.repair_state)),
                                { 0xF8, 0, 0, 255 },
                                { 0xF8, 0xC8, 0, 255 },
                                { 0x30, 0xFC, 0, 255 });
        const auto& rows = binding.track->track.rows.braces;
        std::optional<std::array<float, 3>> previous;
        for (const auto& row : rows) {
            if (!row.next || *row.next >= rows.size())
                continue;
            const auto& next = rows[*row.next];
            for (float t = 0.f; t <= 1.; t = static_cast<float>(static_cast<double>(t) + .1)) {
                const auto inverse = 1. - t;
                std::array<float, 3> position;
                for (std::size_t i = 0; i < 3; ++i)
                    position[i] = static_cast<float>(inverse * inverse * inverse * row.position[i]
                                                     + 3. * inverse * inverse * t * row.outgoing_control[i]
                                                     + 3. * inverse * t * t * next.incoming_control[i]
                                                     + t * t * t * next.position[i]);
                if (previous)
                    line(static_cast<float>(fixed::truncate_integer((*previous)[0] * scale) + 15),
                         static_cast<float>(fixed::truncate_integer((*previous)[2] * scale) + 42),
                         static_cast<float>(fixed::truncate_integer(position[0] * scale) + 15),
                         static_cast<float>(fixed::truncate_integer(position[2] * scale) + 42),
                         color);
                previous = position;
            }
        }
    }
}

void WorldParkMapUiController::State::draw_coverage() {
    const auto cw = fixed::truncate_integer(480. / width), ch = fixed::truncate_integer(425. / height);
    const auto* world = c.referenced_world();
    if (primary_filter == 3)
        for (auto* link = c.entities.active_head; link; link = link->active_next) {
            const auto* header = link->payload;
            if (!header
                || header->kind != ThingKind::park_object
                || header->retirement_pending
                || retired.contains(header->id))
                continue;
            const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(header);
            const auto* definition = get_park_object_definition(c, &object);
            if (definition
                && definition->which_ui_type == ObjectUiCategory::features
                && (std::to_underlying(object.capabilities) & 16u))
                circle(static_cast<float>(fixed::truncate_integer(((object.base.x >> 8) + .5) * cw) + 15),
                       static_cast<float>(fixed::truncate_integer(((object.base.y >> 8) + .5) * ch) + 42),
                       static_cast<float>(cw * world->region_profiles[4].radius + 1),
                       { 255, 255, 255, 255 });
        }
    const auto& balance = c.staff.recruitment.balance;
    for (const auto& marker : markers) {
        if (marker.source_kind != 2 || !marker.visible || !marker.identity)
            continue;
        const auto& staff = *reinterpret_cast<const StaffRuntimeRecord*>(marker.identity);
        std::int32_t minimum{}, maximum{};
        if (staff.base.header.kind == ThingKind::guard) {
            minimum = balance.Guard.MinDetectionRange;
            maximum = balance.Guard.MaxDetectionRange;
        } else if (staff.base.header.kind == ThingKind::entertainer) {
            minimum = balance.Entertainer.MinActiveRange;
            maximum = balance.Entertainer.MaxActiveRange;
        } else
            continue;
        const auto extent = balance.all.MaxExperience;
        if (extent <= 0)
            throw std::runtime_error("Park Map maximum Staff experience is not positive");
        const auto experience = std::clamp(fixed::word(staff.experience), 0, extent);
        const auto radius =
            minimum + fixed::narrow(static_cast<std::int64_t>(experience) * (maximum - minimum) / extent);
        if (cw * radius == -1)
            continue;
        circle(static_cast<float>(fixed::truncate_integer(marker.position[0] * static_cast<float>(cw)) + 15),
               static_cast<float>(fixed::truncate_integer(marker.position[1] * static_cast<float>(ch)) + 42),
               static_cast<float>(cw * radius),
               { 255, 255, 255, 255 });
    }
}

void WorldParkMapUiController::State::render() {
    raster = std::make_shared<TextureImage>(background);
    raster->alpha = TextureAlpha::opaque;
    raster->source_mode = TextureSourceMode::opaque;
    for (auto& marker : markers)
        update_marker(marker);
    draw_cells(false);
    draw_profiles();
    draw_heat();
    alternate = !alternate;
    draw_cells(true);
    draw_coverage();
    dirty = false;
}
} //namespace simcoaster
