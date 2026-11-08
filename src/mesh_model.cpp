#include "simcoaster/mesh_model.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/simulation_sine.hpp"
#include "mesh_binary.hpp"
#include <iterator>
#include <format>
#include <fstream>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace simcoaster {
namespace {
using mesh_binary::Reader;
using mesh_binary::ModelHeader;
using mesh_binary::NodeHeader;

MeshBounds bounds(const std::array<float, 6>& values) {
    return { { values[0], values[1], values[2] }, { values[3], values[4], values[5] } };
}

std::string lower(std::string_view value) {
    std::string result { value };
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        } else if (c == '\\') {
            c = '/';
        }
    }
    return result;
}

std::expected<std::vector<std::byte>, MeshError> read_file(const std::filesystem::path& path) {
    const auto error = [&] {
        return std::unexpected(MeshError { MeshFailure::io, 0, path.native_encoded_string(), {} });
    };
    std::ifstream stream { path, std::ios::binary | std::ios::ate };
    if (!stream) {
        return error();
    }
    const auto end = static_cast<std::streamoff>(stream.tellg());
    if (end < 0 || !std::in_range<std::size_t>(end) || !std::in_range<std::streamsize>(end)) {
        return error();
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return error();
    }
    return bytes;
}

//0x4DBFC0: row-vector affine local * parent; preserve the parent's last column.
MeshMatrix compose(const MeshMatrix& local, const MeshMatrix& parent) {
    auto result = parent;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            result[row * 4 + column] = local[row * 4] * parent[column] + local[row * 4 + 2] * parent[8 + column] + local[row * 4 + 1] * parent[4 + column];
            if (row == 3) {
                result[12 + column] += parent[12 + column];
            }
        }
    }
    return result;
}
} //namespace

std::string describe_mesh_error(const MeshError& error) {
    const auto message = match (error.reason) -> std::string_view {
        case MeshFailure::io                  => "mesh file I/O failed";
        case MeshFailure::archive             => "mesh archive read failed";
        case MeshFailure::missing             => "mesh resource is missing";
        case MeshFailure::short_header        => "incomplete mesh header";
        case MeshFailure::signature           => "invalid Bullfrog MD2 signature";
        case MeshFailure::version             => "unsupported Bullfrog MD2 version";
        case MeshFailure::animation_resource  => "MD2 animation sidecar is not a base model";
        case MeshFailure::bounds              => "mesh table lies outside its resource";
        case MeshFailure::unterminated_string => "mesh string has no terminator";
        case MeshFailure::node_link           => "invalid mesh node link";
        case MeshFailure::node_cycle          => "mesh node hierarchy contains a cycle";
        case MeshFailure::metadata            => "invalid mesh node metadata mapping";
        case MeshFailure::index               => "mesh geometry index is out of range";
        case MeshFailure::configuration       => "invalid mesh runtime configuration";
        case MeshFailure::pose_size           => "mesh pose has the wrong node count";
        case MeshFailure::attachment          => "mesh resource has no clone attachment";
        case MeshFailure::deformed_attachment => "mesh attachment requires live deformed vertices";
        case MeshFailure::animation_version   => "unsupported MD2 animation version";
        case MeshFailure::animation_frame     => "animation frame is outside its track data";
        case MeshFailure::animation_track     => "invalid mesh animation track";
        case MeshFailure::base_resource       => "MD2 base model is not an animation sidecar";
        case MeshFailure::animation_binding   => "animation data is incompatible with the target model";
        case MeshFailure::height_map          => "invalid mesh collision height map";
    };
    auto result = error.resource.empty() ? std::string {} : error.resource + ": ";
    result += message;
    std::format_to(std::back_inserter(result), " at byte/index {}", error.offset);
    if (error.archive_error) {
        result += " (" + std::string { asset_failure_name(error.archive_error->reason) } + ")";
    }
    return result;
}

std::expected<MeshModel, MeshError> parse_mesh_model(std::span<const std::byte> bytes) {
    try {
        Reader r { bytes };
        if (bytes.size() < save::record_width<ModelHeader>) {
            r.fail(MeshFailure::short_header, bytes.size());
        }
        const auto h = r.get<ModelHeader>(0);
        if (h.magic != 483482950u) {
            r.fail(MeshFailure::signature, 0);
        }
        if (h.version != 221) {
            r.fail(MeshFailure::version, 4);
        }
        if (h.auxiliary) {
            r.fail(MeshFailure::animation_resource, 152);
        }
        if (h.drawable_count > h.node_count) {
            r.fail(MeshFailure::metadata, 68);
        }
        //Exporters use 65535 as the unused base when there are no resource rows.
        if (h.metadata_count && (h.first_metadata_node > h.node_count || h.metadata_count > h.node_count - h.first_metadata_node)) {
            r.fail(MeshFailure::metadata, 70);
        }
        r.range(h.drawables, h.drawable_count, 160);
        r.range(h.transforms, h.node_count - h.drawable_count, 88);
        r.range(h.metadata, h.metadata_count, 20);
        r.range(h.slots, h.slot_count, 8);
        if (h.flags & 1) {
            r.range(h.slot_metadata, h.slot_count, 16);
        }
        r.range(h.positions, h.position_block_count, 48);
        r.range(h.normals, h.normal_count, 12);
        r.range(h.uvs, h.uv_block_count, 32);
        r.range(h.groups, h.group_count, 16);
        r.range(h.triangles, h.triangle_count, 8);
        const auto table_slice = [&](std::uint32_t base, std::size_t total, std::size_t stride, std::uint32_t offset, std::size_t count) {
            if (!count) {
                return;
            }
            if (offset < base || (offset - base) % stride || (offset - base) / stride > total || count > total - (offset - base) / stride) {
                r.fail(MeshFailure::bounds, offset);
            }
        };
        MeshModel model;
        model.name.assign(h.name.begin(), std::ranges::find(h.name, '\0'));
        model.flags = h.flags;
        model.animation_format_version = h.animation_version;
        model.surface_height_offset = h.surface_height;
        model.bounds = bounds(h.bounds);
        model.drawable_node_count = h.drawable_count;
        r.range(h.curves, h.curve_count, 16);
        for (std::size_t i = 0; i < h.curve_count; ++i) {
            model.vector_curves.push_back(mesh_binary::vector_curve(r, h.curves + i * 16));
        }
        std::unordered_map<std::uint32_t, std::size_t> node_offsets;
        for (std::size_t i = 0; i < h.node_count; ++i) {
            const auto offset = i < h.drawable_count ? h.drawables + i * 160 : h.transforms + (i - h.drawable_count) * 88;
            if (!node_offsets.emplace(static_cast<std::uint32_t>(offset), i).second) {
                r.fail(MeshFailure::node_link, offset);
            }
        }
        const auto link = [&](std::uint32_t offset) -> std::optional<std::size_t> {
            if (!offset) {
                return std::nullopt;
            }
            const auto found = node_offsets.find(offset);
            if (found == node_offsets.end()) {
                r.fail(MeshFailure::node_link, offset);
            }
            return found->second;
        };
        model.root = link(h.root);
        if (h.node_count && !model.root) {
            r.fail(MeshFailure::node_link, 120);
        }
        for (std::size_t i = 0; i < h.slot_count; ++i) {
            const auto offset = h.slots + i * 8;
            MeshTextureSlot slot;
            slot.flags = r.get<std::uint16_t>(offset);
            slot.source_variant = r.get<std::uint16_t>(offset + 2);
            if (h.flags & 1) {
                const auto metadata = h.slot_metadata + i * 16;
                slot.metadata_flags = r.get<std::uint32_t>(metadata);
                slot.filename = r.text(r.get<std::uint32_t>(metadata + 12));
                const auto count = r.get<std::uint16_t>(metadata + 10);
                const auto names = r.get<std::uint32_t>(metadata + 12);
                r.range(names, count, 20);
                for (std::size_t name = 0; name < count; ++name) {
                    const auto at = static_cast<std::size_t>(names) + name * 20;
                    const auto text = r.text(at);
                    if (text.size() >= 20) {
                        r.fail(MeshFailure::unterminated_string, at);
                    }
                    slot.filenames.push_back(text);
                }
                //The base loader accepts an inline name. '$' names are renderer
                //selectors; preserve them until texture acquisition interprets them.
            }
            model.textures.push_back(std::move(slot));
        }
        if (h.terrain) {
            const auto terrain = r.get<mesh_binary::TerrainHeader>(h.terrain);
            auto& grid = model.terrain.emplace();
            const auto width = static_cast<std::size_t>(terrain.dimensions[0]), height = static_cast<std::size_t>(terrain.dimensions[1]);
            if (!width || !height || width > terrain.cell_count || height > terrain.cell_count || width * height != terrain.cell_count || (width + 1) * (height + 1) != terrain.sample_count) {
                r.fail(MeshFailure::metadata, h.terrain);
            }
            for (const auto value : terrain.origin) {
                if (!std::isfinite(value)) {
                    r.fail(MeshFailure::metadata, h.terrain);
                }
            }
            for (const auto value : terrain.scale) {
                if (!(value > 0) || !std::isfinite(value)) {
                    r.fail(MeshFailure::metadata, h.terrain);
                }
            }
            for (const auto value : terrain.height_range) {
                if (!std::isfinite(value)) {
                    r.fail(MeshFailure::metadata, h.terrain);
                }
            }
            grid.unknown_header_word = terrain.unknown;
            grid.origin_x = terrain.origin[0];
            grid.origin_y = terrain.origin[1];
            grid.cell_scale_x = terrain.scale[0];
            grid.cell_scale_y = terrain.scale[1];
            grid.width_cells = terrain.dimensions[0];
            grid.height_cells = terrain.dimensions[1];
            grid.height_range_min = terrain.height_range[0];
            grid.height_range_max = terrain.height_range[1];
            grid.height_samples = r.array<float>(terrain.samples, terrain.sample_count);
            for (const auto value : grid.height_samples) {
                if (!std::isfinite(value)) {
                    r.fail(MeshFailure::metadata, terrain.samples);
                }
            }
            r.range(terrain.cells, terrain.cell_count, 4);
            grid.cell_metadata.reserve(terrain.cell_count);
            for (std::size_t i = 0; i < terrain.cell_count; ++i) {
                const auto at = terrain.cells + i * 4;
                TerrainCellMetadataRecord cell { static_cast<TerrainCellFlags>(r.get<std::uint16_t>(at)), r.get<std::uint16_t>(at + 2) };
                if (!(static_cast<std::uint16_t>(cell.flags) & 1u) && cell.resource_slot_index >= model.textures.size()) {
                    r.fail(MeshFailure::index, at + 2);
                }
                grid.cell_metadata.push_back(cell);
            }
        }
        for (std::size_t i = 0; i < h.node_count; ++i) {
            const auto offset = i < h.drawable_count ? h.drawables + i * 160 : h.transforms + (i - h.drawable_count) * 88;
            const auto raw = r.get<NodeHeader>(offset);
            MeshNode node;
            node.flags = raw.flags;
            node.parent = link(raw.parent);
            node.next_sibling = link(raw.next);
            node.first_child = link(raw.child);
            node.local_transform = raw.transform;
            node.embedded_index = raw.index;
            node.vector_curve_index = raw.curve;
            node.name = r.text(raw.name);
            if (raw.index != i) {
                r.fail(MeshFailure::metadata, offset + 80);
            }
            if (i < h.drawable_count) {
                auto& d = node.drawable.emplace();
                d.bounds = bounds(r.get<std::array<float, 6>>(offset + 120));
                const auto positions = r.get<std::uint16_t>(offset + 88), groups = r.get<std::uint16_t>(offset + 90);
                const auto triangles = r.get<std::uint16_t>(offset + 92), corners = r.get<std::uint16_t>(offset + 94);
                const auto vertices = r.get<std::uint32_t>(offset + 96), uvs = r.get<std::uint32_t>(offset + 104);
                const auto normals = r.get<std::uint32_t>(offset + 100);
                const auto group_data = r.get<std::uint32_t>(offset + 108), triangle_data = r.get<std::uint32_t>(offset + 112), indices = r.get<std::uint32_t>(offset + 148);
                r.range(vertices, (static_cast<std::size_t>(positions) + 3) / 4, 48);
                r.range(indices, corners, 2);
                table_slice(h.positions, h.position_block_count, 48, vertices, (static_cast<std::size_t>(positions) + 3) / 4);
                if (uvs) {
                    r.range(uvs, (static_cast<std::size_t>(corners) + 3) / 4, 32);
                }
                if (uvs) {
                    table_slice(h.uvs, h.uv_block_count, 32, uvs, (static_cast<std::size_t>(corners) + 3) / 4);
                }
                r.range(group_data, groups, 16);
                r.range(triangle_data, triangles, 8);
                table_slice(h.groups, h.group_count, 16, group_data, groups);
                table_slice(h.triangles, h.triangle_count, 8, triangle_data, triangles);
                for (std::size_t p = 0; p < positions; ++p) {
                    const auto block = vertices + (p / 4) * 48 + (p % 4) * 4;
                    d.positions.push_back({ r.get<float>(block), r.get<float>(block + 16), r.get<float>(block + 32) });
                }
                for (std::size_t p = 0; p < corners; ++p) {
                    const auto index = r.get<std::uint16_t>(indices + p * 2);
                    if (index >= positions) {
                        r.fail(MeshFailure::index, indices + p * 2);
                    }
                    d.position_indices.push_back(index);
                    if (uvs) {
                        const auto block = uvs + (p / 4) * 32 + (p % 4) * 4;
                        d.texture_coordinates.push_back({ r.get<float>(block), r.get<float>(block + 16) });
                    }
                }
                std::size_t normal_count = corners;
                for (std::size_t t = 0; t < triangles; ++t) {
                    const auto at = triangle_data + t * 8;
                    MeshTriangle triangle { r.get<std::uint16_t>(at), r.get<std::array<std::uint16_t, 3>>(at + 2) };
                    for (auto index : triangle.corners) {
                        if (index >= corners) {
                            r.fail(MeshFailure::index, at);
                        }
                    }
                    normal_count = std::max(normal_count, static_cast<std::size_t>(triangle.normal_index_and_flags & 0x7FFFu) + 1);
                    d.triangles.push_back(triangle);
                }
                if (normals && (node.flags & 0x1000000u)) {
                    r.range(normals, corners, 4);
                    for (std::size_t corner = 0; corner < corners; ++corner) {
                        d.packed_vertex_colors.push_back(r.get<std::uint32_t>(normals + corner * 4));
                    }
                    //The native mesh-intensity writer aliases this same storage as Vec3
                    //normals even for ARGB nodes. Retain that view when it is in bounds;
                    //the writer diagnoses an invalid source if the allocation is shorter.
                    if (normals <= r.bytes.size() && corners <= (r.bytes.size() - normals) / 12) {
                        for (std::size_t corner = 0; corner < corners; ++corner) {
                            d.packed_color_normal_alias.push_back(r.get<std::array<float, 3>>(normals + corner * 12));
                        }
                    }
                } else if (normals) {
                    r.range(normals, normal_count, 12);
                    table_slice(h.normals, h.normal_count, 12, normals, normal_count);
                    for (std::size_t n = 0; n < normal_count; ++n) {
                        d.normals.push_back(r.get<std::array<float, 3>>(normals + n * 12));
                    }
                }
                for (std::size_t g = 0; g < groups; ++g) {
                    const auto at = group_data + g * 16;
                    const auto slot_offset = r.get<std::uint32_t>(at);
                    std::optional<std::size_t> slot;
                    if (slot_offset) {
                        if (slot_offset < h.slots || (slot_offset - h.slots) % 8 || (slot_offset - h.slots) / 8 >= h.slot_count) {
                            r.fail(MeshFailure::index, at);
                        }
                        slot = (slot_offset - h.slots) / 8;
                    }
                    MeshTriangleGroup group { slot, r.get<std::uint16_t>(at + 4), r.get<std::uint16_t>(at + 6), r.get<std::uint16_t>(at + 8), r.get<std::uint16_t>(at + 10), r.get<std::uint8_t>(at + 12) };
                    if (group.first_triangle > triangles || group.triangle_count > triangles - group.first_triangle) {
                        r.fail(MeshFailure::index, at + 4);
                    }
                    d.groups.push_back(group);
                }
            }
            model.nodes.push_back(std::move(node));
        }
        //Loader dollar markers inspect the FIRST filename. An embedded selector
        //applies to every group in its node; later slots overwrite earlier ones.
        for (std::size_t slot_index = 0; slot_index < model.textures.size(); ++slot_index) {
            auto& slot = model.textures[slot_index];
            const auto dollar = slot.filename.find('$');
            if (dollar == std::string::npos) {
                continue;
            }
            if (dollar == 0) {
                slot.flags |= 0x2000u;
                continue;
            }
            if (dollar + 1 >= slot.filename.size() || slot.filename[dollar + 1] < '0' || slot.filename[dollar + 1] > '1') {
                r.fail(MeshFailure::configuration, h.slots + slot_index * 8);
            }
            for (auto& node : model.nodes) {
                if (node.drawable && std::ranges::any_of(node.drawable->groups, [&](const auto& group) {
                        return group.texture_slot == slot_index;
                    })) {
                    node.reflection_texture = static_cast<std::uint8_t>(slot.filename[dollar + 1] - '0');
                    node.flags |= 0x2000000u;
                }
            }
        }
        //Validate child/sibling topology independently of the parent chains used
        //for pose evaluation. Never follow corrupt on-disk links recursively.
        std::vector<std::uint8_t> state(model.nodes.size());
        std::vector<std::pair<std::size_t, bool>> pending;
        for (std::size_t i = 0; i < model.nodes.size(); ++i) {
            pending.emplace_back(i, false);
            while (!pending.empty()) {
                const auto [index, done] = pending.back();
                pending.pop_back();
                if (done) {
                    state[index] = 2;
                    continue;
                }
                if (state[index] == 1) {
                    r.fail(MeshFailure::node_cycle, index);
                }
                if (state[index] == 2) {
                    continue;
                }
                state[index] = 1;
                pending.emplace_back(index, true);
                const auto& node = model.nodes[index];
                if (node.first_child) {
                    if (model.nodes[*node.first_child].parent != index) {
                        r.fail(MeshFailure::node_link, index);
                    }
                    pending.emplace_back(*node.first_child, false);
                }
                if (node.next_sibling) {
                    if (model.nodes[*node.next_sibling].parent != node.parent) {
                        r.fail(MeshFailure::node_link, index);
                    }
                    pending.emplace_back(*node.next_sibling, false);
                }
            }
        }
        if (const auto pose = evaluate_mesh_pose(model); !pose) {
            return std::unexpected(pose.error());
        }
        for (std::size_t i = 0; i < h.metadata_count; ++i) {
            const auto row = h.metadata + i * 20;
            MeshResourceRecord resource { r.get<std::uint32_t>(row), r.get<std::uint32_t>(row + 4), r.get<std::uint32_t>(row + 8), h.first_metadata_node + i };
            const auto anchor = r.get<std::uint32_t>(row + 12);
            if ((resource.flags & 0x40040u) && anchor) {
                r.range(anchor, 1, 16);
                auto& data = resource.attachment.emplace();
                data.triangle = r.get<std::uint16_t>(anchor);
                data.first_blend = r.get<float>(anchor + 4);
                data.second_blend = r.get<float>(anchor + 8);
                data.normal_offset = r.get<float>(anchor + 12);
                if (!std::isfinite(data.first_blend) || !std::isfinite(data.second_blend) || !std::isfinite(data.normal_offset)) {
                    r.fail(MeshFailure::metadata, anchor);
                }
                if (resource.flags & 0x40000u) {
                    const auto back = r.get<std::uint16_t>(anchor + 2);
                    if (back > anchor) {
                        r.fail(MeshFailure::bounds, anchor + 2);
                    }
                    data.orientation_radians = r.get<std::array<float, 3>>(anchor - back);
                    for (const auto angle : data.orientation_radians) {
                        if (!std::isfinite(angle)) {
                            r.fail(MeshFailure::metadata, anchor - back);
                        }
                    }
                }
            }
            model.resources.push_back(std::move(resource));
        }
        return model;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<MeshModel, MeshError> load_mesh_model(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto result = parse_mesh_model(*bytes);
    if (!result) {
        result.error().resource = path.native_encoded_string();
    }
    return result;
}

std::optional<std::size_t> MeshModel::find_resource(std::uint32_t requested_flags, std::uint32_t id) const {
    const auto mask = (requested_flags & 0x3DA1F83u) ? requested_flags : 0x3DA1F82u;
    for (std::size_t i = 0; i < resources.size(); ++i) {
        if (resources[i].id == id && (resources[i].flags & mask)) {
            return i;
        }
    }
    return std::nullopt;
}

std::vector<std::uint32_t> MeshModel::resource_indices(std::uint32_t mask) const {
    std::vector<std::uint32_t> result;
    for (std::size_t i = 0; i < resources.size(); ++i) {
        if (resources[i].flags & mask) {
            result.push_back(static_cast<std::uint32_t>(i));
        }
    }
    return result;
}

std::expected<std::vector<MeshMatrix>, MeshError> evaluate_mesh_pose(const MeshModel& model, std::span<const MeshMatrix> overrides) {
    if (!overrides.empty() && overrides.size() != model.nodes.size()) {
        return std::unexpected(MeshError { MeshFailure::pose_size, overrides.size(), {}, {} });
    }
    std::vector<MeshMatrix> result(model.nodes.size());
    std::vector<std::uint8_t> state(model.nodes.size());
    std::vector<std::size_t> chain;
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        std::optional<std::size_t> next = i;
        while (next) {
            const auto index = *next;
            if (index >= model.nodes.size()) {
                return std::unexpected(MeshError { MeshFailure::node_link, index, {}, {} });
            }
            if (state[index] == 1) {
                return std::unexpected(MeshError { MeshFailure::node_cycle, index, {}, {} });
            }
            if (state[index] == 2) {
                break;
            }
            state[index] = 1;
            chain.push_back(index);
            next = model.nodes[index].parent;
        }
        while (!chain.empty()) {
            const auto index = chain.back();
            chain.pop_back();
            const auto& node = model.nodes[index];
            const auto& parent = node.parent ? result[*node.parent] : mesh_identity_matrix;
            result[index] = compose(overrides.empty() ? node.local_transform : overrides[index], parent);
            state[index] = 2;
        }
    }
    return result;
}

std::expected<std::vector<std::uint32_t>, MeshError> initialize_mesh_resource_nodes(MeshModel& model) {
    const auto failure = [](std::size_t index) {
        return std::unexpected(MeshError { MeshFailure::metadata, index, {}, {} });
    };
    std::vector<std::uint32_t> entries(model.resources.size());
    std::vector<std::optional<std::size_t>> descriptors(model.nodes.size());
    for (std::size_t index = 0; index < model.resources.size(); ++index) {
        const auto& resource = model.resources[index];
        if (resource.node_index >= model.nodes.size() || descriptors[resource.node_index]) {
            return failure(index);
        }
        descriptors[resource.node_index] = index;
        auto& node = model.nodes[resource.node_index];
        if (resource.flags & 0x30u) {
            node.flags |= 0x20000000u;
            entries[index] |= 1u;
            if (node.flags & 0x400u) {
                entries[index] |= 16u;
            }
            if (resource.flags & 0x40040u) {
                if (!node.parent || *node.parent >= model.nodes.size()) {
                    return failure(index);
                }
                model.nodes[*node.parent].flags |= 0x10000000u;
            }
        }
        if (resource.flags & 8u) {
            node.flags |= 0x40u;
        }
        if (resource.flags & 0x10000u) {
            node.flags |= 0x20000u;
        }
    }
    if (model.resources.empty()) {
        return entries;
    }
    std::vector<std::pair<std::size_t, bool>> pending;
    std::vector<std::uint8_t> state(model.nodes.size());
    std::vector<bool> eligible(model.nodes.size());
    if (model.root) {
        pending.emplace_back(*model.root, false);
    }
    while (!pending.empty()) {
        const auto [index, leaving] = pending.back();
        pending.pop_back();
        if (index >= model.nodes.size()) {
            return failure(index);
        }
        auto& node = model.nodes[index];
        if (!leaving) {
            if (state[index]) {
                return failure(index);
            }
            state[index] = 1;
            pending.emplace_back(index, true);
            std::size_t count = 0;
            for (auto child = node.first_child; child; child = model.nodes[*child].next_sibling) {
                if (*child >= model.nodes.size() || ++count > model.nodes.size()) {
                    return failure(index);
                }
                pending.emplace_back(*child, false);
            }
        } else {
            bool accepted = !descriptors[index] || !(model.resources[*descriptors[index]].flags & 0x30u);
            if (descriptors[index] && !node.first_child) {
                entries[*descriptors[index]] |= 0x20u;
            }
            for (auto child = node.first_child; child; child = model.nodes[*child].next_sibling) {
                accepted = accepted && eligible[*child];
            }
            eligible[index] = accepted;
            state[index] = 2;
            if (accepted) {
                node.flags |= 0x40000000u;
            }
        }
    }
    return entries;
}

std::expected<MeshMatrix, MeshError> mesh_attachment_transform(const MeshModel& model, std::span<const MeshMatrix> pose, std::size_t index) {
    const auto failure = [&](MeshFailure reason) {
        return std::unexpected(MeshError { reason, index, {}, {} });
    };
    if (pose.size() != model.nodes.size()) {
        return failure(MeshFailure::pose_size);
    }
    if (index >= model.resources.size()) {
        return failure(MeshFailure::index);
    }
    const auto& record = model.resources[index];
    if (record.node_index >= model.nodes.size()) {
        return failure(MeshFailure::metadata);
    }
    if (!(record.flags & 0x30)) {
        return failure(MeshFailure::attachment);
    }
    const auto& node = model.nodes[record.node_index];
    if (node.parent && *node.parent >= model.nodes.size()) {
        return failure(MeshFailure::metadata);
    }
    try {
        //499B30 composes auxiliary resources through their real hierarchy. A
        //deformed parent uses its live triangle vertices and face normal, not
        //the authored seat translation. Preserve supplied pose overrides too.
        std::vector<std::size_t> chain;
        for (std::optional<std::size_t> at = record.node_index; at; at = model.nodes[*at].parent) {
            if (*at >= model.nodes.size()) {
                return failure(MeshFailure::metadata);
            }
            if (chain.size() >= model.nodes.size()) {
                return failure(MeshFailure::node_cycle);
            }
            chain.push_back(*at);
        }
        auto parent = mesh_identity_matrix;
        for (auto current = chain.rbegin(); current != chain.rend(); ++current) {
            const auto& current_node = model.nodes[*current];
            auto matrix = pose[*current];
            if (current_node.parent && parent != pose[*current_node.parent]) {
                matrix = compose(compose(matrix, mesh_transform::inverse(pose[*current_node.parent])), parent);
            }
            const auto metadata = std::ranges::find(model.resources, *current, &MeshResourceRecord::node_index);
            if (metadata != model.resources.end() && (metadata->flags & 0x30u) && (metadata->flags & 0x40040u) && current_node.parent && (model.nodes[*current_node.parent].flags & 0x200000u)) {
                if (!metadata->attachment) {
                    return failure(MeshFailure::deformed_attachment);
                }
                const auto& parent_node = model.nodes[*current_node.parent];
                if (!parent_node.drawable) {
                    return failure(MeshFailure::metadata);
                }
                const auto& drawable = *parent_node.drawable;
                const auto& anchor = *metadata->attachment;
                if (anchor.triangle >= drawable.triangles.size()) {
                    return failure(MeshFailure::index);
                }
                const auto& triangle = drawable.triangles[anchor.triangle];
                std::array<std::array<float, 3>, 3> local, world;
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    if (triangle.corners[corner] >= drawable.position_indices.size()) {
                        return failure(MeshFailure::index);
                    }
                    const auto position = drawable.position_indices[triangle.corners[corner]];
                    if (position >= drawable.positions.size()) {
                        return failure(MeshFailure::index);
                    }
                    local[corner] = drawable.positions[position];
                    world[corner] = mesh_transform::point(parent, local[corner]);
                }
                const auto cross = [](const auto& a, const auto& b) {
                    return std::array<float, 3> { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
                };
                const auto normal_index = triangle.normal_index_and_flags & 0x7FFFu;
                if (normal_index >= drawable.normals.size()) {
                    return failure(MeshFailure::index);
                }
                auto normal = drawable.normals[normal_index];
                if ((parent_node.flags & 0x10010000u) == 0x10010000u) {
                    std::array<float, 3> a, b;
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        a[axis] = local[0][axis] - local[1][axis];
                        b[axis] = local[2][axis] - local[1][axis];
                    }
                    normal = cross(b, a);
                    const auto length = std::sqrt(static_cast<double>(normal[0]) * normal[0] + static_cast<double>(normal[1]) * normal[1] + static_cast<double>(normal[2]) * normal[2]);
                    if (!(length > 0)) {
                        normal = { 0, -1, 0 };
                    } else {
                        for (auto& value : normal) {
                            value = static_cast<float>(value / length);
                        }
                    }
                }
                std::array<float, 3> up, blended;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    up[axis] = normal[0] * parent[axis] + normal[2] * parent[8 + axis] + normal[1] * parent[4 + axis];
                    const auto first = static_cast<float>((1.0 - anchor.first_blend) * world[0][axis] + static_cast<double>(anchor.first_blend) * world[1][axis]);
                    blended[axis] = static_cast<float>(static_cast<double>(anchor.second_blend) * world[2][axis] + (1.0 - anchor.second_blend) * first);
                    matrix[12 + axis] = static_cast<float>(static_cast<double>(anchor.normal_offset) * up[axis] + blended[axis]);
                }
                if (metadata->flags & 0x40000u) {
                    std::array<float, 3> direction;
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        direction[axis] = blended[axis] - world[0][axis];
                    }
                    const auto length = std::sqrt(static_cast<double>(direction[0]) * direction[0] + static_cast<double>(direction[1]) * direction[1] + static_cast<double>(direction[2]) * direction[2]);
                    //Native499350's degenerate path permutes the reference normal.
                    if (!(length > 0)) {
                        direction = { up[2], up[0], up[1] };
                    } else {
                        for (auto& value : direction) {
                            value = static_cast<float>(value / length);
                        }
                    }
                    const auto x = cross(up, direction);
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        matrix[axis] = x[axis];
                        matrix[4 + axis] = up[axis];
                        matrix[8 + axis] = direction[axis];
                    }
                    const auto sine = [](double angle) {
                        return simulation_sine_at_rounded_index(static_cast<float>(angle * simulation_sine_samples_per_radian));
                    };
                    const auto sx = sine(anchor.orientation_radians[0]), cx = sine(static_cast<double>(anchor.orientation_radians[0]) + 1.5707964);
                    const auto sy = sine(anchor.orientation_radians[1]), cy = sine(static_cast<double>(anchor.orientation_radians[1]) + 1.5707964);
                    const auto sz = sine(anchor.orientation_radians[2]), cz = sine(static_cast<double>(anchor.orientation_radians[2]) + 1.5707964);
                    const MeshMatrix rz { cz, sz, 0, 0, -sz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
                    const MeshMatrix ry { cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1 };
                    const MeshMatrix rx { 1, 0, 0, 0, 0, cx, sx, 0, 0, -sx, cx, 0, 0, 0, 0, 1 };
                    const auto euler = compose(compose(rz, ry), rx);
                    const auto original = matrix;
                    for (std::size_t row = 0; row < 3; ++row) {
                        for (std::size_t column = 0; column < 3; ++column) {
                            matrix[row * 4 + column] = euler[row * 4] * original[column] + euler[row * 4 + 1] * original[4 + column] + euler[row * 4 + 2] * original[8 + column];
                        }
                    }
                }
            }
            parent = matrix;
        }
        return parent;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

RideCarMeshMetadata ride_car_mesh_metadata(const MeshModel& model, std::uint32_t template_id) {
    RideCarMeshMetadata result { template_id, model.bounds.minimum[2], model.bounds.maximum[2], model.root ? model.nodes[*model.root].local_transform[14] : 0.0f, model.resource_indices(std::to_underlying(MeshResourceFlag::head)) };
    std::size_t largest = 0;
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        if (model.nodes[i].drawable && model.nodes[i].drawable->positions.size() > largest) {
            largest = model.nodes[i].drawable->positions.size();
            result.primary_node = i;
        }
    }
    return result;
}

std::expected<MeshModelLibrary, MeshError> open_mesh_model_library(const std::filesystem::path& source) {
    MeshModelLibrary library;
    library.source = source;
    if (lower(source.extension().native_encoded_string()) == ".wad") {
        auto archive = load_dwfb_archive(source);
        if (!archive) {
            return std::unexpected(MeshError { MeshFailure::archive, archive.error().offset, source.native_encoded_string(), archive.error() });
        }
        library.archive = std::move(*archive);
    } else if (!std::filesystem::is_directory(source)) {
        return std::unexpected(MeshError { MeshFailure::missing, 0, source.native_encoded_string(), {} });
    }
    return library;
}

std::expected<std::uint32_t, MeshError> MeshModelLibrary::load(std::uint32_t unique_id, std::string_view filename) {
    std::string name { filename };
    if (std::filesystem::path { name }.extension().empty()) {
        name += ".md2";
    }
    const auto key = lower(name);
    for (std::size_t i = 0; i < templates.size(); ++i) {
        if (templates[i].unique_id == unique_id && lower(templates[i].filename) == key) {
            return static_cast<std::uint32_t>(i + 1);
        }
    }
    auto bytes = read_resource(name);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    if (!*bytes) {
        return std::unexpected(MeshError { MeshFailure::missing, 0, name, {} });
    }
    auto loaded = parse_mesh_model(**bytes);
    if (!loaded) {
        auto error = loaded.error();
        if (error.resource.empty()) {
            error.resource = source.native_encoded_string() + "/" + name;
        }
        return std::unexpected(std::move(error));
    }
    if (templates.size() >= std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(MeshError { MeshFailure::index, templates.size(), name, {} });
    }
    templates.push_back({ unique_id, std::move(name), std::move(*loaded) });
    return static_cast<std::uint32_t>(templates.size());
}

std::expected<std::optional<std::vector<std::byte>>, MeshError> MeshModelLibrary::read_resource(std::string_view filename) const {
    if (archive) {
        const auto index = archive->find_entry(filename);
        if (!index) {
            return std::optional<std::vector<std::byte>> {};
        }
        auto bytes = archive->read_entry(*index);
        if (!bytes) {
            return std::unexpected(MeshError { MeshFailure::archive, bytes.error().offset, std::string { filename }, bytes.error() });
        }
        return std::optional { std::move(*bytes) };
    }
    const auto path = source / filename;
    if (!std::filesystem::is_regular_file(path)) {
        return std::optional<std::vector<std::byte>> {};
    }
    auto bytes = read_file(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return std::optional { std::move(*bytes) };
}

const MeshModel* MeshModelLibrary::model(std::uint32_t id) const {
    return id && id <= templates.size() ? &templates[id - 1].model : nullptr;
}

std::expected<RideTrainConfiguration, MeshError> build_model_backed_ride_train_configuration(const DynamicTextureMeshBalance& balance, MeshModelLibrary& library) {
    std::optional<MeshError> error;
    auto config = build_ride_train_configuration(balance, [&](std::uint32_t key, std::string_view name) -> std::optional<RideCarMeshMetadata> {
        const auto id = library.load(key, name);
        if (!id) {
            error = id.error();
            return std::nullopt;
        }
        return ride_car_mesh_metadata(*library.model(*id), *id);
    });
    if (error) {
        return std::unexpected(std::move(*error));
    }
    if (!config) {
        return std::unexpected(MeshError { MeshFailure::configuration, 0, library.source.native_encoded_string(), {} });
    }
    return std::move(*config);
}
} //namespace simcoaster
