#pragma once
#include "mesh_scene.hpp"
#include "application_configuration.hpp"
#include "frame_profile.hpp"

namespace simcoaster {
struct UiDrawList;

//D3D11 owns native resources behind this boundary; model/save types contain
//no COM pointers. A null window renders to an offscreen target for validation.
class MeshRenderer {
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    MeshRenderer(std::uint32_t width, std::uint32_t height, void* window = nullptr, bool force_software = false, std::optional<std::uint32_t> adapter = {}, std::optional<ApplicationGraphicsProfile> profile = {});
    ~MeshRenderer();
    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;
    void resize(std::uint32_t width, std::uint32_t height);
    void draw(const MeshScene&, const SceneCamera&);
    void draw_ui(const UiDrawList&);
    void draw_effects(const EffectScene&);
    void present();
    //Opt-in timestamp queries never wait in a measured frame. Draining once
    //after a diagnostic run submits pending work and waits outside its timings.
    void finish_profile_frame();
    std::vector<GpuFrameProfile> take_profile_samples(bool drain = false);
    TextureImage capture();
    bool software_adapter() const;
    std::string adapter_name() const;
    static std::vector<std::u16string> hardware_adapters();
};

void write_texture_png(const TextureImage&, const std::filesystem::path&);
int run_mesh_viewer(const std::filesystem::path& source, std::string_view model_name, const std::filesystem::path& game_root, const std::optional<std::filesystem::path>& capture = {});
int run_park_viewer(const std::filesystem::path& save, std::string theme, const std::filesystem::path& game_root, const std::optional<std::filesystem::path>& capture = {});
int run_application(const std::filesystem::path& game_root, const std::optional<std::filesystem::path>& initial_park = {}, std::string initial_theme = {});
} //namespace simcoaster
