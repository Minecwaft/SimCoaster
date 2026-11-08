#include "park_runtime_state.hpp"
#include "park_runtime_codec.hpp"

namespace simcoaster {
using namespace park_detail;

ParkRuntime::State::State(const std::filesystem::path& root, const std::string& theme, bool coaster_designer, std::function<void()> pump, std::optional<ApplicationHardwareProfile> requested_profile) : bindings(root), rendering(root, theme), simulation(bindings) {
    bindings.resource_pump = std::move(pump);
    if (requested_profile) {
        configuration.hardware_profile = std::move(*requested_profile);
    } else {
        auto profile = load_application_hardware_profile(root, 1);
        if (!profile) {
            throw std::runtime_error(profile.error().reason);
        }
        configuration.hardware_profile = std::move(*profile);
    }
    simulation.world->source.data_directory = (root / "Data").native_encoded_string();
    simulation.world->source.selected_resource_source_name = theme;
    if (coaster_designer) {
        simulation.world->mode.configuration = 0x20000000u;
        initialize_game_mode_selector(*simulation.world);
    }
    presentation.audio = std::make_unique<WorldSoundRuntime>(
        *simulation.world,
        [this](std::string_view path) -> std::expected<std::optional<std::vector<std::byte>>, SoundError> {
            try {
                return bindings.read_asset(path);
            } catch (const std::exception& error) {
                return std::unexpected(SoundError { SoundFailure::io, 0, std::string { path } + ": " + error.what() });
            }
        },
        root / "Data",
        theme);
    bindings.audio = presentation.audio.get();
    simulation.world->resources.meshes = &rendering.meshes;
    rendering.meshes.configure_sign_fonts(root / "Data/fonts.wad");
    simulation.world->weather.weather_runtime = simulation.weather.get();
    simulation.world->resources.track_rides = &simulation.rides;
    bindings.scene = &rendering.scene;
    bindings.track_rides = &simulation.rides;
    bindings.particles = simulation.particles.get();
    simulation.world->random.mesh_passenger_random_state = static_cast<std::uint32_t>(next_crt_random(*simulation.world)); //Native terrain preparation seeds the
    //independent boarding stream from rand().
    simulation.world->timing.scale = 1.;
    bindings.runtime_overlays = &frame.runtime_overlays;
    bindings.construction = &tools.construction;
    bindings.layout_replay = &tools.layout_replay;
    bindings.layout_backend = &tools.layout_backend;
    simulation.world->construction.editor = &tools.coaster_editor;
    simulation.world->construction.coaster_catalog = &tools.coaster_catalog;
    const auto recording = tools.layout_replay.bind(*simulation.world);
    if (!recording) {
        throw std::runtime_error(describe_layout_error(recording.error()));
    }
    simulation.rides.random_state = static_cast<std::uint32_t>(next_crt_random(*simulation.world)); //Independent TrackRide effect stream,
    //initialize_track_ride_simulation.
    rendering.scene.configure_texture_set(mesh_texture_set_for_quality(configuration.hardware_profile.graphics.texture_quality));
    rendering.scene.load_terrain();
    rendering.meshes.configure_terrain_textures(rendering.scene);
    rendering.terrain = rendering.scene.terrain()->terrain->view();
    simulation.world->construction.active_terrain_model_data = &rendering.active;
    simulation.world->construction.original_terrain_cells = rendering.scene.original_terrain_cells();
    simulation.world->weather.map_heightfield_width = rendering.terrain.width_cells;
    simulation.world->weather.map_heightfield_height = rendering.terrain.height_cells;
    ensure(create_world_structure(*simulation.world) != 0, "World allocation failed");
    auto render_settings = load_installed_theme_render_settings(root, theme);
    rendering.lighting = std::move(render_settings.lighting);
    rendering.atmosphere_colors = render_settings.atmosphere;
    rendering.scene.configure_procedural_bounds(render_settings.procedural_bounds);
    simulation.weather->diffuse_mask = rendering.lighting.diffuse_mask;
}

ParkRuntime::State::~State() {
    try {
        //Special view restores roots, terrain and F/T resources while their
        //UI owners are still alive. Late mesh cleanup has a UI-free fallback.
        bindings.restore_world_view_from_special_modes(*simulation.world);
    } catch (...) {
        //Destruction must still release the World after a failed resource load.
    }
    bindings.abandon_inspection_for_shutdown(*simulation.world);
    presentation.scape.reset();
    presentation.ui.reset();
    bindings.ui = nullptr;
    presentation.authoring.reset();
    (void)tools.layout_replay.cancel(*simulation.world);
    try {
        tools.coaster_editor.cancel(*simulation.world, tools.construction);
        tools.coaster_editor.activate(*simulation.world, 0, false);
    } catch (...) {
        //World teardown still owns every model and record after a failed edit.
    }
    tools.layout_replay.detach(*simulation.world);
    tools.coaster_catalog.clear(*simulation.world);
    simulation.world->construction.coaster_catalog = nullptr;
    simulation.world->construction.editor = nullptr;
    release_world_staff_interaction(*simulation.world);
    if (simulation.world->simulation.event_registry) {
        destroy_world_event_subscription_registry(*simulation.world, simulation.world->simulation.event_registry);
        bindings.release(Allocation::event_registry, simulation.world->simulation.event_registry);
        simulation.world->simulation.event_registry = nullptr;
    }
    if (simulation.world->simulation.reference) {
        std::destroy_at(simulation.world->simulation.reference);
        bindings.release(Allocation::world, simulation.world->simulation.reference);
        simulation.world->simulation.current = simulation.world->simulation.reference = nullptr;
    }
}
} //namespace simcoaster
