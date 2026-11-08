#pragma once
#include "mesh_model.hpp"
#include <variant>

namespace simcoaster {
enum class MeshAnimationEntryFlag : std::uint32_t {
    position = 1,
    rotation = 8,
    scale = 0x80,
    parent_path = 0x200,
    orient_to_path = 0x400,
    deformation = 0x1000,
    uncompressed_vertices = 0x4000,
    uv = 0x10000,
    visibility = 0x20000,
};

struct MeshQuaternionKey {
    MeshFrameKey key;
    std::array<float, 4> rotation {};
};

struct MeshVectorKey {
    MeshFrameKey key;
    std::array<float, 3> value {};
};

struct MeshFloatCurve {
    std::uint32_t start_frame {};
    std::vector<float> samples;
};

struct MeshCompressedChannel {
    std::vector<std::uint16_t> frames, vertex_indices;
    std::size_t elements_per_key {};
    //Signed XYZ10 values are expanded once during resource loading.
    std::vector<std::array<float, 3>> samples;
};

struct MeshCompressedDeformation {
    std::uint16_t flags {};
    std::array<float, 3> scale {}, bias {};
    //Channel 0 carries bounds. Mask 2 selects a static vertex list in channel 1.
    std::vector<MeshCompressedChannel> channels;
};

struct MeshVertexFrameSamples {
    std::uint32_t samples_per_frame {};
    std::vector<std::uint32_t> vertex_indices;
    std::vector<std::array<float, 3>> samples;
};

struct MeshUVChannel {
    std::vector<std::uint16_t> frames;
    std::vector<std::array<float, 2>> samples;
};

struct MeshTextureVariantTrack {
    std::uint16_t slot {};
    std::vector<MeshFrameKey> keys;
};

struct MeshAnimationEntry {
    std::uint32_t flags {}, authored_start_frame {}, authored_end_frame {};
    std::uint16_t target_node {};
    std::optional<MeshVectorCurve> position;
    std::vector<MeshQuaternionKey> rotations;
    std::vector<MeshVectorKey> scales;
    std::optional<MeshFloatCurve> path_percentage;
    std::variant<std::monostate, MeshCompressedDeformation, MeshVertexFrameSamples> deformation;
    std::vector<MeshUVChannel> uv_channels;
    std::vector<std::int16_t> visibility_events;
    std::vector<std::array<std::uint8_t, 8>> quaternion_fraction_curves;
};

struct MeshAnimation {
    std::uint32_t flags {}, start_frame {}, end_frame {};
    std::vector<MeshAnimationEntry> entries;
    std::vector<MeshTextureVariantTrack> texture_variants;
    std::vector<std::uint16_t> hidden_nodes;

    float frame_span() const {
        return static_cast<float>(end_frame - start_frame);
    }
};

std::expected<MeshAnimation, MeshError> parse_mesh_animation(std::span<const std::byte>, const MeshModel* target_model = nullptr);
std::expected<void, MeshError> validate_mesh_animation_binding(const MeshModel&, const MeshAnimation&);

struct MeshAnimationOptions {
    std::optional<bool> additive;
    bool linear_quaternions {}, suppress_uv {}, suppress_texture_variants {};
};

struct MeshAnimationState {
    std::vector<std::vector<std::size_t>> deformation_keys;
    std::vector<bool> deformation_initialized;
    std::optional<float> previous_frame;
};

//The caller owns a mutable clone of the model. State belongs to one bound
//clip; clear it when replacing/resetting that clone. Rewinds reset compressed
//cursors, corresponding to the native loop/transition reset. No global scratch.
std::expected<void, MeshError> apply_mesh_animation_frame(MeshModel&, const MeshAnimation&, float frame, MeshAnimationState&, MeshAnimationOptions = {});

struct MeshAnimationLayer {
    const MeshAnimation* animation {};
    float frame {};
    MeshAnimationOptions options;
    bool sample { true }; //Active additive layers can reset channels without sampling.
};

//Resets the union of additive channels from the source template before applying
//the ordered layers, matching the per-control reset/application passes.
std::expected<void, MeshError> evaluate_mesh_animation_layers(const MeshModel& source, MeshModel& runtime, std::span<const MeshAnimationLayer>, std::span<MeshAnimationState>);

struct LoadedMeshAnimation {
    std::string filename;
    MeshAnimation animation;
};

struct MeshAnimationLibrary {
    //Native table order: C,D,I,L,S,M,E,U,W,B,R,O; numbered variants are zero based.
    std::array<std::vector<LoadedMeshAnimation>, 12> groups;
    const MeshAnimation* find(std::size_t category, std::size_t sub_animation = 0) const;
};

std::expected<MeshAnimationLibrary, MeshError> load_mesh_animation_library(const MeshModelLibrary&, std::string_view model_filename, const MeshModel* target_model = nullptr);

struct CoasterAnimationChannelRange {
    float minimum {}, maximum { 1 }, scale { 1 }, bias {};
};

//Maps the five geometry controls to explicit frame values. Includes the native
//loft clamp, loop-turn wrap, mirrored heading and pitch/bank half-range biases.
std::array<float, 5> coaster_animation_frames(std::array<float, 5>& controls, const std::array<CoasterAnimationChannelRange, 5>& ranges, const std::array<float, 5>& frame_spans, std::uint32_t geometry_flags);
//Native five-channel binding: L0, R0, I0, B0, R1. Missing groups remain idle.
std::expected<void, MeshError> animate_coaster_pylon(const MeshModel& source, MeshModel& runtime, const MeshAnimationLibrary&, std::array<float, 5>& controls, const std::array<CoasterAnimationChannelRange, 5>&, std::uint32_t geometry_flags, std::array<MeshAnimationState, 5>& states);
} //namespace simcoaster
