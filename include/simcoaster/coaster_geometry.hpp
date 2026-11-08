#pragma once
#include "ride_train.hpp"
#include "mesh_model.hpp"

namespace simcoaster {
//Values are taken from the refreshed geometry object and its transformed
//attachment clones. A missing axis means the object has no mesh instance.
struct CoasterAttachmentInput {
    std::array<float, 3> brace_position{}, mesh_position{};
    std::optional<std::array<float, 3>> mesh_axis;
    std::int32_t source_mesh_resource_index{ -1 };
    float geometry_fraction{};
};

struct CoasterGeometryObjectInput {
    std::uint32_t geometry_index{}, geometry_flags{}, runtime_data_flags{};
    std::array<float, 3> inline_position{}, inline_outgoing_tangent{};
    float animation_heading{}, animation_wobble{}, attachment_axis_angle{};
    //When present, only this attachment updates the object component index.
    std::optional<std::size_t> component_attachment_index;
    std::vector<CoasterAttachmentInput> attachments;
    std::array<float, 3> inline_incoming_tangent{};
    float junction_angle{}, minimum_tangent_dot{ -0.99900001f };
    bool has_runtime_data{};
};

//Rebuild the design-node tangents before deriving pylon animation angles.
//Endpoint flags and the closing support's link to node one follow 0x46FDC0.
bool rebuild_coaster_design_splines(std::span<CoasterGeometryObjectInput>,
                                    float curviness,
                                    float straight_divisor,
                                    bool suspended);
//Resolve consecutive native track resource IDs, copy transformed clone axes
//and positions, and derive the brace/mesh offset and attachment bank angle.
//Requires the evaluated (possibly animated) pose, not an assumed default pose.
//Existing attachments are replaced only on success.
std::expected<void, MeshError> bind_coaster_model_attachments(CoasterGeometryObjectInput&,
                                                              const MeshModel&,
                                                              std::span<const MeshMatrix> pose,
                                                              std::uint32_t first_resource_id,
                                                              std::uint32_t attachment_count,
                                                              float heading_angle);

struct CoasterAttachmentRows {
    std::vector<RideTrackAttachmentRow> braces, mesh_rows;
    //First row and count per geometry object, in linked-object order.
    std::vector<std::pair<std::size_t, std::size_t>> object_rows;
};

//Reconstructs linked brace/mesh rows and their tangent/control points from
//already transformed model attachments. The component sample coordinates are
//populated by the separate geometry sampling pass.
bool build_coaster_attachment_rows(std::span<const CoasterGeometryObjectInput>,
                                   float track_curviness,
                                   CoasterAttachmentRows&);

struct CoasterTrackSamples {
    CoasterAttachmentRows rows;
    std::vector<RideMotionSample> samples;
    std::vector<std::uint32_t> geometry_component_indices;
    std::uint32_t reported_sample_count{}, maximum_component_index_span{};
    float last_sample_position{}, activation_lower{}, activation_upper{};

    RideTrackGeometry view(std::uint32_t frame_flags = 0, bool use_sample_bank = false) const {
        return { samples, rows.braces, rows.mesh_rows, frame_flags, use_sample_bank, reported_sample_count };
    }
};

//Rebuild the native fixed-distance component samples and assign coordinates to
//both attachment chains. Requires at least three linked geometry objects.
bool build_coaster_track_samples(std::span<const CoasterGeometryObjectInput>,
                                 CoasterAttachmentRows,
                                 float track_spacing,
                                 float entrance_size_interval,
                                 CoasterTrackSamples&);
//The activation interval uses the first attachment in the first and third
//geometry objects, blended toward their midpoint by the SAM interval setting.
bool
coaster_ride_activation_bounds(const CoasterAttachmentRows&, float entrance_size_interval, float& lower, float& upper);
} //namespace simcoaster
