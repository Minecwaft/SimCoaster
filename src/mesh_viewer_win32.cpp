#include "application_host_win32.hpp"
#include "simcoaster/text_format.hpp"

namespace simcoaster {
using namespace win32_host;

int run_mesh_viewer(const std::filesystem::path& source, std::string_view name, const std::filesystem::path& root, const std::optional<std::filesystem::path>& output) {
    auto library = open_mesh_model_library(source);
    if (!library) {
        throw std::runtime_error(describe_mesh_error(library.error()));
    }
    auto assets = read_mesh_playback_assets(*library, name);
    if (!assets) {
        throw std::runtime_error(describe_mesh_error(assets.error()));
    }
    const auto metadata = initialize_mesh_resource_nodes(assets->model);
    if (!metadata) {
        throw std::runtime_error(describe_mesh_error(metadata.error()));
    }
    for (const auto& record : assets->model.resources) {
        if (record.flags & 0x80u) {
            assets->model.nodes[record.node_index].flags |= 0x80000010u;
        }
    }
    auto created = create_mesh_playback(std::make_shared<MeshPlaybackAssets>(std::move(*assets)), 1, 4, true);
    if (!created) {
        throw std::runtime_error(describe_mesh_error(created.error()));
    }
    auto& playback = *created;
    const unsigned initial = playback.assets->animations.find(5) ? 5u : (playback.assets->animations.find(2) ? 2u : 0u);
    request(playback, initial, {});
    const auto posed = evaluate_mesh_playback_pose(playback, true);
    if (!posed) {
        throw std::runtime_error(describe_mesh_error(posed.error()));
    }
    const auto theme = theme_from_path(source);
    const auto lighting = load_installed_mesh_lighting(root, theme);
    MeshTextureLibrary textures { *library, root, theme };
    std::optional<TerrainTextureAtlas> terrain_textures;
    if (playback.model.terrain) {
        auto loaded = load_terrain_texture_atlas(*library, theme == "wintro" ? 16u : 32u);
        if (!loaded) {
            throw std::runtime_error(describe_texture_error(loaded.error()));
        }
        terrain_textures = std::move(*loaded);
        initialize_procedural_terrain_cell_flags(*playback.model.terrain);
        terrain_textures->update(playback.model, textures);
    }
    const auto procedural = terrain_textures ? &*terrain_textures : nullptr;
    MeshScene scene;
    scene.clear();
    append_mesh_scene(scene, playback.model, textures, lighting, procedural);
    if (scene.indices.empty()) {
        throw std::runtime_error("Model has no visible geometry");
    }
    WindowState state;
    state.camera.fit(playback.model.terrain ? terrain_grid_bounds(playback.model.terrain->view()) : scene.bounds);
    for (const auto& missing : scene.missing_textures) {
        print_text(stderr, "Unresolved texture (flat material): {}\n", missing);
    }
    if (output) {
        MeshRenderer renderer { 1100, 760 };
        renderer.draw(scene, state.camera);
        if (!output->parent_path().empty()) {
            std::filesystem::create_directories(output->parent_path());
        }
        write_texture_png(renderer.capture(), *output);
        print_text(stdout, "Rendered {} triangles, {} materials and {} texture sources using {}{}\nSaved {}\n", scene.indices.size() / 3, scene.draws.size(), textures.loaded_count(), renderer.adapter_name(), (renderer.software_adapter() ? " (software)" : " (hardware)"), output->native_encoded_string());
        return 0;
    }
    Window window { state };
    MeshRenderer renderer { state.width, state.height, window.handle };
    ShowWindow(window.handle, SW_SHOW);
    print_text(stdout, "Renderer: {}\n", renderer.adapter_name());
    auto previous = std::chrono::steady_clock::now(), begin = previous;
    std::chrono::steady_clock::duration simulation {};
    auto width = state.width, height = state.height;
    bool running = true;
    while (running) {
        MSG message {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) {
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto delta = now - previous;
        previous = now;
        if (!state.paused) {
            simulation += delta;
        }
        if (!state.width || !state.height) {
            Sleep(20);
            continue;
        }
        if (width != state.width || height != state.height) {
            width = state.width;
            height = state.height;
            renderer.resize(width, height);
        }
        const MeshPlaybackClocks clocks { std::chrono::duration_cast<MeshMilliseconds>(simulation), std::chrono::duration_cast<MeshMilliseconds>(now - begin) };
        if (state.clip) {
            request(playback, *state.clip, clocks);
            state.clip.reset();
        }
        if (GetForegroundWindow() == window.handle) {
            const auto speed = static_cast<float>(std::chrono::duration<double>(delta).count()) * state.camera.distance * 0.4f;
            float x = 0, z = 0;
            if (GetAsyncKeyState('W') < 0) {
                z -= speed;
            }
            if (GetAsyncKeyState('S') < 0) {
                z += speed;
            }
            if (GetAsyncKeyState('A') < 0) {
                x -= speed;
            }
            if (GetAsyncKeyState('D') < 0) {
                x += speed;
            }
            state.camera.target[0] += x * std::cos(state.camera.yaw) + z * std::sin(state.camera.yaw);
            state.camera.target[2] += -x * std::sin(state.camera.yaw) + z * std::cos(state.camera.yaw);
        }
        state.camera.far_plane = std::max(state.camera.far_plane, state.camera.distance * 2);
        const auto advanced = advance_mesh_playback(playback, clocks);
        if (!advanced) {
            throw std::runtime_error(describe_mesh_error(advanced.error()));
        }
        if (terrain_textures) {
            terrain_textures->update(playback.model, textures);
        }
        scene.clear();
        append_mesh_scene(scene, playback.model, textures, lighting, procedural);
        renderer.draw(scene, state.camera);
        if (state.capture) {
            write_texture_png(renderer.capture(), root / "SimCoaster-frame.png");
            state.capture = false;
        }
        renderer.present();
    }
    return 0;
}

int run_park_viewer(const std::filesystem::path& save, std::string theme, const std::filesystem::path& root, const std::optional<std::filesystem::path>& output) {
    ParkRuntime park { root, save, std::move(theme) };
    MeshScene scene;
    EffectScene screen_particles;
    park.build_scene(scene);
    WindowState state;
    state.ui = &park.ui();
    state.park = &park;
    state.ui_context = &park.context();
    state.camera.fit(terrain_grid_bounds(park.world_scene().terrain()->terrain->view()));
    const auto& limits = park.preferences();
    state.preferences = &limits;
    park.context().view.camera.isometric_mode = limits.isometric_on != 0;
    const auto& saved_camera = park.context().view.camera;
    const auto saved_zoom = park.context().view.mobile.camera_zoom;
    if (std::isfinite(saved_zoom) && saved_zoom > 0 && std::ranges::all_of(saved_camera.focus_position, [](float v) {
            return std::isfinite(v);
        })) {
        state.camera.target = saved_camera.focus_position;
        state.camera.distance = saved_zoom;
        state.camera.yaw = saved_camera.heading_radians;
        state.camera.pitch = saved_camera.pitch_radians;
    }
    park.set_render_camera(state.camera);
    state.camera = park.render_camera();
    UiDrawList ui_draw;
    park.build_scene(scene, state.camera, static_cast<float>(state.width) / static_cast<float>(state.height));
    const auto& stats = park.statistics();
    print_text(stdout, "Restored {} World records, {} real mesh instances, {} RSSE states, {} subscriptions, {} coasters, {} train slots and {} car slots from {} section bytes\n", stats.things, stats.mesh_instances, stats.scripts, stats.event_subscriptions, stats.coaster_records, stats.train_slots, stats.car_slots, stats.restored_bytes);
    for (const auto& name : scene.missing_textures) {
        print_text(stderr, "Unresolved texture (flat material): {}\n", name);
    }
    if (output) {
        MeshRenderer renderer { state.width, state.height };
        renderer.draw(scene, state.camera);
        park.ui().build_draw(ui_draw, state.width, state.height);
        renderer.draw_ui(ui_draw);
        park.build_screen_particles(screen_particles, static_cast<float>(state.width) / static_cast<float>(state.height));
        renderer.draw_effects(screen_particles);
        if (!output->parent_path().empty()) {
            std::filesystem::create_directories(output->parent_path());
        }
        write_texture_png(renderer.capture(), *output);
        print_text(stdout, "Rendered {} triangles in {} world draws and {} UI draws across {} controls using {}\nSaved {}\n", scene.indices.size() / 3, scene.draws.size(), ui_draw.commands.size(), park.ui().controls().size(), renderer.adapter_name(), output->native_encoded_string());
        return 0;
    }
    UiCursorWin32 cursors { root };
    state.cursor_assets = &cursors;
    Window window { state, L"SimCoaster saved park | Space: pause | drag: orbit | " L"right drag: pan | wheel: zoom | F12: capture" };
    MeshRenderer renderer { state.width, state.height, window.handle };
    ShowWindow(window.handle, SW_SHOW);
    auto width = state.width, height = state.height;
    bool running = true;
    auto previous = std::chrono::steady_clock::now();
    StaffWorldInput staff { park, state, park.world_scene().terrain()->terrain->view() };
    state.drain_world_input = [&staff](HWND handle) {
        staff.update(handle);
    };
    ParkAuthoringFrontend authoring { park,
                                      state.camera,
                                      park.world_scene().terrain()->terrain->view(),
                                      save,
                                      window.handle,
                                      true,
                                      [&state, target = window.handle] {
                                          release_host_pointer_capture(state, target);
                                      },
                                      &cursors };
    state.authoring = &authoring;

    struct DetachAuthoring {
        WindowState& state;

        ~DetachAuthoring() {
            state.authoring = nullptr;
            state.drain_world_input = {};
        }
    } detach_authoring { state };

    while (running) {
        state.ui_commands_enabled = park.ui_commands_enabled();
        MSG message {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
            }
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        if (state.ui_error) {
            std::rethrow_exception(state.ui_error);
        }
        if (!running) {
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - previous);
        previous += elapsed;
        if (!state.width || !state.height) {
            Sleep(20);
            continue;
        }
        process_ui_actions(park, state, window.handle, limits);
        advance_staff_camera(park, state, window.handle, elapsed);
        park.set_render_camera(state.camera);
        state.camera = park.render_camera();
        if (state.ui_camera_changed) {
            park.commit_ui_camera();
            state.ui_camera_changed = false;
        }
        if (park.ui_commands_enabled()) {
            staff.update(window.handle);
        }
        //Camera picker DOWN can enter a real view here. Apply its notification
        //before publishing the host camera again, which would overwrite it.
        process_ui_actions(park, state, window.handle, limits);
        const bool geometry = coaster_geometry_active(state);
        if (geometry && park.ui_commands_enabled() && !state.orbit && !state.pan && (state.coaster_geometry_capture || !ui_owns_pointer(state)) && client_owns_pointer(window.handle)) {
            //Kind8 runs its pick/drag update each frame, including when a stationary
            //pointer's ray changes because the keyboard moves the camera.
            park.set_render_camera(state.camera);
            state.camera = park.render_camera();
            (void)park.coaster_geometry_pointer(CoasterGeometryPointerPhase::hover, { state.mouse_x, state.mouse_y }, state.width, state.height);
        }
        authoring.frame(window.handle, state.width, state.height, !park.ui_commands_enabled() || state.staff_controls || state.staff_editing || geometry || ui_owns_pointer(state), state.paused);
        park.set_render_camera(state.camera);
        state.camera = park.render_camera();
        if (state.ui_camera_changed) {
            park.commit_ui_camera();
            state.ui_camera_changed = false;
        }
        park.advance_frame(elapsed, state.paused);
        state.camera = park.render_camera();
        //Widget callbacks release their logical capture while draining the UI
        //queue. Release Win32 capture afterward, including drag releases that
        //were still queued when WM_*BUTTONUP returned.
        release_host_pointer_capture(state, window.handle);
        process_ui_actions(park, state, window.handle, limits);
        follow_staff_locator(park, state);
        state.camera = park.render_camera();
        apply_camera_input_feedback(park, state, window.handle);
        if (client_owns_pointer(window.handle)) {
            install_ui_cursor(state);
        }
        park.build_scene(scene, state.camera, static_cast<float>(state.width) / static_cast<float>(state.height));
        append_locator_outline(park, scene);
        if (width != state.width || height != state.height) {
            width = state.width;
            height = state.height;
            renderer.resize(width, height);
        }
        renderer.draw(scene, state.camera);
        park.ui().build_draw(ui_draw, state.width, state.height);
        renderer.draw_ui(ui_draw);
        park.build_screen_particles(screen_particles, static_cast<float>(state.width) / static_cast<float>(state.height));
        renderer.draw_effects(screen_particles);
        if (state.capture) {
            write_texture_png(renderer.capture(), root / "SimCoaster-park.png");
            state.capture = false;
        }
        renderer.present();
    }
    return 0;
}
} //namespace simcoaster
