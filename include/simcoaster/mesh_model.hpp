#pragma once
#include "mesh_error.hpp"
#include "mesh_types.hpp"
#include "resource_archive.hpp"
#include "ride_train.hpp"
#include "terrain_grid.hpp"
#include <memory>

namespace simcoaster {
struct TextureImage;
enum class MeshResourceFlag : std::uint32_t {
    clone_transform = 0x30,
    head = 0x80,
    track_attachment = 0x400000,
};

struct MeshTriangle {
    std::uint16_t normal_index_and_flags{};
    std::array<std::uint16_t, 3> corners{};
};

struct MeshTriangleGroup {
    std::optional<std::size_t> texture_slot; //Null is a valid untextured group.
    std::uint16_t first_triangle{}, triangle_count{}, minimum_corner{}, maximum_corner{};
    std::uint8_t flags{};
};

struct MeshDrawable {
    MeshBounds bounds;
    std::vector<std::array<float, 3>> positions;
    //Triangle corners index render vertices; this table maps them to positions.
    std::vector<std::uint16_t> position_indices;
    std::vector<std::array<float, 2>> texture_coordinates;
    //Corner normals precede the face normals addressed by triangle word0.
    std::vector<std::array<float, 3>> normals;
    std::vector<MeshTriangle> triangles;
    std::vector<MeshTriangleGroup> groups;
    std::vector<std::uint32_t> packed_vertex_colors{}; //Node01000000 overlays the normal source with
    //ARGB.
    //Bounded Vec3 view of that overlaid source, used only by the native mesh
    //intensity writer. It is separate from the renderer's actual normals.
    std::vector<std::array<float, 3>> packed_color_normal_alias{};
    //Native terrain darkening's packed-ARGB destination in unlit mode.
    //This replaces source vertex color instead of multiplying it. Retaining
    //independent storage lets lighting modes change without aliasing normals.
    std::vector<std::uint32_t> world_shadow_colors{};
};

struct MeshNode {
    std::uint32_t flags{};
    std::optional<std::size_t> parent, next_sibling, first_child;
    MeshMatrix local_transform{};
    std::uint16_t embedded_index{}, vector_curve_index{};
    std::string name;
    std::optional<MeshDrawable> drawable;
    std::optional<std::uint8_t> reflection_texture{}; //Embedded $0/$1 selects the shared sky/table
    //source.
};

struct MeshAttachmentAnchor {
    std::uint16_t triangle{};
    float first_blend{}, second_blend{}, normal_offset{};
    std::array<float, 3> orientation_radians{};
};

struct MeshResourceRecord {
    std::uint32_t flags{}, id{}, selector_filter{};
    std::size_t node_index{};
    //Native metadata+12 owns a16-byte triangle/weight record. The orientation
    //correction is stored before that record, reached by its back-offset WORD.
    std::optional<MeshAttachmentAnchor> attachment{};
};

struct MeshTextureSlot {
    std::uint16_t flags{}, source_variant{};
    std::uint32_t metadata_flags{};
    std::string filename;
    std::vector<std::string> filenames{};                 //Native fixed20-byte variant entries.
    std::shared_ptr<const TextureImage> image_override{}; //Immutable per-clone generated sign texture.
};

struct MeshFrameKey {
    [[=save::Field{ "frame", 0 }]] std::uint16_t frame{};
    [[=save::Field{ "metadata", 1 }]] std::uint16_t metadata{};
};

struct MeshVectorCurve {
    std::uint32_t flags{};
    std::vector<std::array<float, 3>> points;
    std::vector<MeshFrameKey> keys;
};

struct MeshModel {
    std::string name;
    std::uint32_t flags{}, animation_format_version{};
    float surface_height_offset{};
    MeshBounds bounds;
    std::optional<std::size_t> root;
    std::size_t drawable_node_count{};
    std::vector<MeshNode> nodes;
    std::vector<MeshResourceRecord> resources;
    std::vector<MeshTextureSlot> textures;
    std::vector<MeshVectorCurve> vector_curves;
    std::optional<TerrainGridData> terrain{};
    std::optional<std::size_t> find_resource(std::uint32_t flags, std::uint32_t id) const;
    std::vector<std::uint32_t> resource_indices(std::uint32_t flags) const;
};

std::string describe_mesh_error(const MeshError&);
//Bullfrog's proprietary MD2 format, not Quake's format. Decode file offsets
//into native indices/owners; no on-disk pointer is installed in runtime
//memory. Only the base-model layout used by the game (221) is accepted here.
//Animation sidecars are a distinct format and explicitly rejected.
std::expected<MeshModel, MeshError> parse_mesh_model(std::span<const std::byte>);
std::expected<MeshModel, MeshError> load_mesh_model(const std::filesystem::path&);
//Native auxiliary-clone resource setup and recursive eligibility. Returns
//initial per-resource entry flags, with native indices.
std::expected<std::vector<std::uint32_t>, MeshError> initialize_mesh_resource_nodes(MeshModel&);

//Full local overrides let animation/placement supply transforms without
//changing the shared model. An empty span selects the file's default pose.
std::expected<std::vector<MeshMatrix>, MeshError> evaluate_mesh_pose(const MeshModel&,
                                                                     std::span<const MeshMatrix> local_overrides = {});
//Native B-cross-A face winding and degenerate fallback (0,-1,0). Clears
//drawable dirty bits and the model normal-dirty bit after recomputation.
std::expected<void, MeshError> recompute_mesh_face_normals(MeshModel&);

//Native4CA270, without optional terrain-base coverage. A selected node keeps
//sign preparation from normalizing unrelated animated material coordinates.
struct MeshUvBaseCoverage {
    std::span<const std::uint8_t> mask; //X-major, with the supplied row stride.
    std::span<const std::array<float, 3>> positions;
    std::size_t stride{ 40 };
};

void normalize_mesh_uvs(MeshModel&,
                        std::optional<std::size_t> selected_node = {},
                        const MeshUvBaseCoverage* coverage = nullptr);
//Returns the unnormalized clone matrix, as consumed by coaster geometry and
//passengers. Deformed-parent anchors use the model's current vertices and
//authored triangle weights; missing anchor data is an explicit error.
std::expected<MeshMatrix, MeshError>
mesh_attachment_transform(const MeshModel&, std::span<const MeshMatrix> pose, std::size_t resource_index);
RideCarMeshMetadata ride_car_mesh_metadata(const MeshModel&, std::uint32_t template_id);

struct MeshModelTemplate {
    std::uint32_t unique_id{};
    std::string filename;
    MeshModel model;
};

//Handles are one-based and local to this library. A mesh backend using a
//resulting train configuration must bind to the same library/handle namespace.
struct MeshModelLibrary {
    std::filesystem::path source;
    std::optional<DwfbArchive> archive;
    std::vector<MeshModelTemplate> templates;
    std::expected<std::optional<std::vector<std::byte>>, MeshError> read_resource(std::string_view filename) const;
    std::expected<std::uint32_t, MeshError> load(std::uint32_t unique_id, std::string_view filename);
    const MeshModel* model(std::uint32_t template_id) const;
};

std::expected<MeshModelLibrary, MeshError> open_mesh_model_library(const std::filesystem::path&);
std::expected<RideTrainConfiguration, MeshError>
build_model_backed_ride_train_configuration(const DynamicTextureMeshBalance&, MeshModelLibrary&);
} //namespace simcoaster
