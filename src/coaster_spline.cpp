#include "simcoaster/coaster_geometry.hpp"
#include <algorithm>
#include <cmath>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;

Point difference(const Point& a, const Point& b) {
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

float magnitude(const Point& a) {
    return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

Point scaled(Point a, float scale) {
    for (auto& v : a) {
        v *= scale;
    }
    return a;
}

Point cross(const Point& a, const Point& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

float dot(const Point& a, const Point& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

bool normalize(Point& a) {
    const auto n = magnitude(a);
    if (!(n > 0) || !std::isfinite(n)) {
        return false;
    }
    a = scaled(a, 1 / n);
    return true;
}
} //namespace

bool rebuild_coaster_design_splines(std::span<CoasterGeometryObjectInput> nodes, float curviness, float straight, bool suspended) {
    if (!(straight > 0) || !std::isfinite(straight) || !std::isfinite(curviness)) {
        return false;
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        auto& node = nodes[i];
        const auto* previous = i && !(node.geometry_flags & 0x80000u) ? &nodes[i - 1] : nullptr;
        const auto* next = i + 1 < nodes.size() && !(node.geometry_flags & 0x40000u) ? &nodes[i + 1] : nullptr;
        if (node.geometry_flags & 0x100000u) {
            if (nodes.size() < 2) {
                return false;
            }
            next = &nodes[1];
        }
        node.geometry_flags &= 0xF1FFFFFFu;
        if (!previous && !next) {
            continue;
        }
        const bool special = (node.runtime_data_flags & 0x4000u) != 0;
        const bool adjacent_special = node.has_runtime_data && !special && ((i && (nodes[i - 1].runtime_data_flags & 0x4000u)) || (i + 1 < nodes.size() && (nodes[i + 1].runtime_data_flags & 0x4000u)));
        Point incoming {}, outgoing {};
        node.junction_angle = 0;
        if (!previous) {
            outgoing = scaled(difference(next->inline_position, node.inline_position), 1 / straight);
            incoming = scaled(outgoing, -1);
        } else if (!next) {
            incoming = scaled(difference(previous->inline_position, node.inline_position), 1 / straight);
            outgoing = scaled(incoming, -1);
            //0x46FEF3 compares the raw dot product, then pops an x87 register before
            //reading C0. Use the numeric comparison directly on x64.
            if (special && !(node.geometry_flags & 0x80u) && i >= 2 && dot(difference(previous->inline_position, nodes[i - 2].inline_position), difference(previous->inline_position, node.inline_position)) < node.minimum_tangent_dot) {
                incoming = scaled(incoming, -1);
                outgoing = scaled(outgoing, -1);
            }
        } else {
            const auto in_delta = difference(previous->inline_position, node.inline_position), out_delta = difference(next->inline_position, node.inline_position);
            const auto in_length = magnitude(in_delta), out_length = magnitude(out_delta);
            if (!(in_length > 0) || !(out_length > 0)) {
                return false;
            }
            const auto in = scaled(in_delta, 1 / in_length), out = scaled(out_delta, 1 / out_length);
            const auto product = dot(in, out);
            const auto in_horizontal = std::hypot(in[0], in[2]), out_horizontal = std::hypot(out[0], out[2]);
            if (in_horizontal > 0 && out_horizontal > 0) {
                const auto horizontal = std::clamp((in[0] * out[0] + in[2] * out[2]) / (in_horizontal * out_horizontal), -1.0f, 1.0f);
                const auto turn = out[0] * in[2] - out[2] * in[0];
                node.junction_angle = static_cast<float>((turn < 0 ? -1 : 1) * std::acos(horizontal));
            }
            const bool straight_branch = product < (node.has_runtime_data ? node.minimum_tangent_dot : -0.99900001f) || product > 0.99900001f;
            if (straight_branch && special) {
                auto axis = scaled(difference(in, out), 0.5f / straight);
                if (!(node.geometry_flags & 0x80u)) {
                    axis = scaled(axis, -1);
                }
                incoming = scaled(axis, in_length);
                outgoing = scaled(axis, -out_length);
            } else if (straight_branch) {
                incoming = scaled(in_delta, 1 / straight);
                outgoing = scaled(out_delta, 1 / straight);
            } else {
                Point bisector { out[0] + in[0], out[1] + in[1], out[2] + in[2] };
                if (special) {
                    incoming = scaled(bisector, in_length * curviness);
                    outgoing = scaled(bisector, -out_length * curviness);
                } else {
                    auto axis = cross(in_delta, out_delta);
                    if (suspended && !(node.geometry_flags & 0x40u) && axis[1] < 0) {
                        node.geometry_flags |= 0x10000000u;
                    } else {
                        node.geometry_flags &= ~0x10000000u;
                    }
                    if (!normalize(axis) || !normalize(bisector)) {
                        return false;
                    }
                    auto perpendicular = cross(bisector, axis);
                    incoming = scaled(perpendicular, in_length * curviness);
                    outgoing = scaled(perpendicular, -out_length * curviness);
                }
            }
        }
        if (previous && adjacent_special) {
            incoming[1] = 0;
            outgoing[1] = 0;
        }
        for (const auto value : incoming) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
        for (const auto value : outgoing) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
        node.inline_incoming_tangent = incoming;
        node.inline_outgoing_tangent = outgoing;
    }
    return true;
}
} //namespace simcoaster
