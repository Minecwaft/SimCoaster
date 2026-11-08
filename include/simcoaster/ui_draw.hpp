#pragma once
#include "mesh_material.hpp"
#include <optional>

namespace simcoaster {
//4E6520 derives preprojection Z from the supplied selector without clamping.
//Text/surface updater511F90 separately clamps its signed depth to at least1.
//Native projected mode2 (6E16C0) subsequently multiplies Z by0.1.
inline float ui_attachment_depth(std::int32_t selector) {
    return 1.f - static_cast<float>(selector) / 40.f;
}

//UI drawing uses the original 2048 by1536 logical canvas; its Win32/D3D
//consumer maps that canvas to the current client area. No packed x86 ABI.
struct UiDrawVertex {
    std::array<float, 3> position{};
    std::array<float, 2> uv{};
    RgbaPixel color{ 255, 255, 255, 255 };
    std::array<float, 2> reflection_uv{ .5f, .5f };
};
//The Advisor retains model depth within its layer; interactive controls
//composite above it using their own depth. They cannot slice its geometry.
enum class UiDrawLayer {
    advisor,
    controls
};

struct UiDrawCommand {
    TextureImagePtr texture;
    std::uint32_t first_index{}, index_count{};
    //Logical left,top,right,bottom clip edges (right/bottom exclusive).
    std::array<float, 4> clip{ 0, 0, 2048, 1536 };
    MeshMaterial material;
    bool native_model_filter{};
    bool depth_buffered{};
    TextureImagePtr secondary_texture{};
    UiDrawLayer layer = UiDrawLayer::controls;
};

struct UiDrawList {
    float canvas_width = 2048, canvas_height = 1536;
    std::vector<UiDrawVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<UiDrawCommand> commands;
    //Default preprojection Z of the control currently emitting attachments.
    //Application movies and standalone diagnostic drawing have no such owner.
    std::optional<float> native_depth;

    void clear() {
        vertices.clear();
        indices.clear();
        commands.clear();
    }
};

//Legacy custom attachment producers emit flat quads without an explicit
//native layer. Give only those commands the owning control's surface depth;
//models, fonts and 3D previews supply their own projected Z and opt out by
//marking depth_buffered. Nested controls finish their own scope first.
class UiNativeDepthScope {
    UiDrawList& draw_;
    std::size_t first_command_;
    std::optional<float> previous_depth_;
    float depth_;

public:
    UiNativeDepthScope(UiDrawList& draw, float depth)
        : draw_(draw),
          first_command_(draw.commands.size()),
          previous_depth_(draw.native_depth),
          depth_(depth) {
        draw_.native_depth = depth;
    }

    UiNativeDepthScope(const UiNativeDepthScope&) = delete;
    UiNativeDepthScope& operator=(const UiNativeDepthScope&) = delete;

    ~UiNativeDepthScope() {
        for (auto i = first_command_; i < draw_.commands.size(); ++i) {
            auto& command = draw_.commands[i];
            if (command.depth_buffered)
                continue;
            for (std::size_t n = command.first_index;
                 n < static_cast<std::size_t>(command.first_index) + command.index_count;
                 ++n)
                draw_.vertices[draw_.indices[n]].position[2] = depth_ * .1f;
            //These legacy flat commands used the modern depthless compositor.
            //Native attachment ownership supplies shared depth testing/writing;
            //genuine per-material overrides are already marked depth_buffered.
            command.material.flags &= ~(0x8000u | 0x800u);
            command.depth_buffered = true;
        }
        draw_.native_depth = previous_depth_;
    }
};
} //namespace simcoaster
