#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include "simcoaster/park_authoring_win32.hpp"
#include "simcoaster/park_runtime.hpp"
#include "simcoaster/ui_cursor_win32.hpp"
#include "simcoaster/win32_input_capture.hpp"
#include "simcoaster/world_cursor_pick.hpp"
#include "simcoaster/world_staff_view.hpp"
#include <iterator>
#include <format>
#include <algorithm>
#include <array>
#include <shobjidl.h>
#include <stdexcept>

namespace simcoaster {
namespace {
WorldInputModifierMask native_modifiers() {
    //Native503970 encodes each held key as its two-bit group. In particular,
    //exact12 means Control alone; Shift, Alt and combinations remain distinct.
    std::uint32_t flags {};
    if (GetAsyncKeyState(VK_SHIFT) < 0) {
        flags |= 0x03u;
    }
    if (GetAsyncKeyState(VK_CONTROL) < 0) {
        flags |= 0x0Cu;
    }
    if (GetAsyncKeyState(VK_MENU) < 0) {
        flags |= 0x30u;
    }
    return static_cast<WorldInputModifierMask>(flags);
}

enum Command : UINT {
    save_as = 101,
    import_design,
    inspect,
    build_path,
    build_queue,
    build_track,
    demolish,
    erase_path,
    erase_track,
    activate,
    open_ride,
    close_ride,
    cancel_tool,
    rotate_tool,
    help,
    open_park,
    close_park,
    request_level,
    cancel_level,
    select_node,
    add_node,
    add_special_node,
    add_pylon_node,
    move_node,
    finish_coaster,
    first_catalog = 4000
};

std::wstring wide(std::u16string_view value) {
    return { value.begin(), value.end() };
}

std::wstring availability_label(ParkCatalogAvailability value) {
    match (value) {
        case ParkCatalogAvailability::available              => return {};
        case ParkCatalogAvailability::research_locked        => return L" [research locked]";
        case ParkCatalogAvailability::single_instance        => return L" [already built]";
        case ParkCatalogAvailability::requires_selection     => return L" [select a ride]";
        case ParkCatalogAvailability::incompatible_selection => return L" [different ride]";
        case ParkCatalogAvailability::coaster_editor         => return L" [prebuilt.sam unavailable]";
        case _                                               => {
        }
    } throw std::logic_error("unknown catalog availability");
}

std::wstring tool_label(ParkAuthoringTool tool) {
    match (tool) {
        case ParkAuthoringTool::inspect            => return L"Select";
        case ParkAuthoringTool::object             => return L"Place object";
        case ParkAuthoringTool::path               => return L"Build path";
        case ParkAuthoringTool::queue              => return L"Build queue";
        case ParkAuthoringTool::track              => return L"Build track";
        case ParkAuthoringTool::demolish           => return L"Demolish object";
        case ParkAuthoringTool::erase_path         => return L"Remove path";
        case ParkAuthoringTool::erase_track        => return L"Remove track";
        case ParkAuthoringTool::hoarding_chain     => return L"Build boundary";
        case ParkAuthoringTool::land_boundary      => return L"Extend park boundary";
        case ParkAuthoringTool::demolish_rectangle => return L"Delete area";
        case _                                     => {
        }
    } throw std::logic_error("unknown authoring tool");
}

HMENU submenu(HMENU parent, const wchar_t* label) {
    const auto menu = CreatePopupMenu();
    if (!menu || !AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(menu), label)) {
        throw std::runtime_error("Cannot create park authoring menu");
    }
    return menu;
}

void item(HMENU menu, UINT command, const wchar_t* label, bool enabled = true) {
    if (!AppendMenuW(menu, MF_STRING | (enabled ? 0u : MF_GRAYED), command, label)) {
        throw std::runtime_error("Cannot append park authoring menu item");
    }
}

struct DialogOwner {
    IFileSaveDialog* dialog {};

    ~DialogOwner() {
        if (dialog) {
            dialog->Release();
        }
    }
};
} //namespace

struct ParkAuthoringFrontend::State {
    ParkAuthoringController& controller;
    WorldRuntime& context;
    SceneCamera& camera;
    TerrainGridView terrain;
    std::filesystem::path original;
    HWND window {};
    HMENU menu {}, build {}, upgrades {}, selected_menu {};
    std::vector<std::uint16_t> command_definitions;
    std::uint32_t width {}, height {};
    std::uint32_t saved_tick {};
    int x {}, y {}, down_x {}, down_y {};
    bool pointer_down {}, dragged {}, dirty {}, camera_gesture {}, allow_close {};
    bool staff_active {}, com_initialized {};
    bool replay_disabled {};
    bool diagnostic_ui {};
    UiCursorWin32* cursors {};
    std::uint64_t interaction_revision {};

    void synchronize_pointer_tracking() {
        const auto revision = controller.interaction_revision();
        if (revision != interaction_revision) {
            pointer_down = dragged = camera_gesture = false;
            interaction_revision = revision;
        }
    }

    std::function<void()> release_pointer_callback;

    explicit State(ParkRuntime& park, SceneCamera& view, TerrainGridView grid, const std::filesystem::path& save, HWND handle, bool diagnostics, std::function<void()> release_pointer, UiCursorWin32* assets)
        : controller(park.authoring()),
          context(park.context()),
          camera(view),
          terrain(grid),
          original(save),
          window(handle),
          saved_tick(context.simulation.current->saved.game_tick),
          diagnostic_ui(diagnostics),
          cursors(assets),
          interaction_revision(controller.interaction_revision()),
          release_pointer_callback(std::move(release_pointer)) {
        if (!diagnostic_ui) {
            return;
        }
        const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(apartment)) {
            throw std::runtime_error("Cannot initialize park dialog COM apartment");
        }
        com_initialized = true;
        try {
            menu = CreateMenu();
            if (!menu) {
                throw std::runtime_error("Cannot create park menu");
            }
            const auto file = submenu(menu, L"&File");
            item(file, save_as, L"Save park &as...\tCtrl+Shift+S");
            item(file, import_design, L"Place coaster design (.COS)...");
            item(file, open_park, L"Open park to guests");
            item(file, close_park, L"Close park to guests");
            build = submenu(menu, L"&Build");
            const auto paths = submenu(menu, L"&Paths");
            item(paths, build_path, L"Build &path\tP");
            item(paths, build_queue, L"Edit selected ride &queue\tQ");
            item(paths, build_track, L"Edit selected ride &track\tT");
            item(paths, erase_path, L"Remove path");
            item(paths, erase_track, L"Remove selected ride track");
            const auto object = submenu(menu, L"&Selected");
            selected_menu = object;
            item(object, inspect, L"&Select object\tV");
            item(object, activate, L"&Activate special ride");
            item(object, open_ride, L"&Open");
            item(object, close_ride, L"&Close");
            upgrades = submenu(object, L"&Add-ons and upgrades");
            item(object, request_level, L"Request level upgrade");
            item(object, cancel_level, L"Cancel level upgrade request");
            const auto coaster = submenu(object, L"Coaster &nodes");
            item(coaster, select_node, L"Select node under cursor\tN");
            item(coaster, add_node, L"Add ordinary node\tG");
            item(coaster, add_special_node, L"Add special node\tH");
            item(coaster, add_pylon_node, L"Add node with pylon selection");
            item(coaster, move_node, L"Move selected node\tM");
            item(coaster, finish_coaster, L"Finish coaster edit\tEnter");
            item(object, demolish, L"&Demolish\tDelete");
            item(object, rotate_tool, L"&Rotate placement\tR");
            item(object, cancel_tool, L"Finish &tool\tEscape");
            const auto assistance = submenu(menu, L"&Help");
            item(assistance, help, L"Park authoring controls");
            if (!SetMenu(window, menu)) {
                throw std::runtime_error("Cannot attach park menu");
            }
            rebuild_catalog();
            DrawMenuBar(window);
        } catch (...) {
            if (menu) {
                if (GetMenu(window) == menu) {
                    SetMenu(window, nullptr);
                }
                DestroyMenu(menu);
                menu = nullptr;
            }
            CoUninitialize();
            com_initialized = false;
            throw;
        }
    }

    ~State() {
        if (diagnostic_ui && IsWindow(window)) {
            SetMenu(window, nullptr);
        }
        if (menu) {
            DestroyMenu(menu);
        }
        if (com_initialized) {
            CoUninitialize();
        }
    }

    void release_capture() {
        if (release_pointer_callback) {
            release_pointer_callback();
        } else if (GetCapture() == window) {
            ReleaseCapture();
        }
    }

    bool has_changes() const {
        return dirty || context.simulation.current->saved.game_tick != saved_tick;
    }

    void rebuild_catalog() {
        while (GetMenuItemCount(build) > 0) {
            DeleteMenu(build, 0, MF_BYPOSITION);
        }
        while (GetMenuItemCount(upgrades) > 0) {
            DeleteMenu(upgrades, 0, MF_BYPOSITION);
        }
        command_definitions.clear();
        constexpr std::array labels { L"Rides", L"Shops", L"Sideshows", L"Scenery", L"Other", L"Facilities", L"Other" };
        std::array<HMENU, 7> categories {};
        for (const auto& row : controller.catalog()) {
            auto target = upgrades;
            if (!row.attachment) {
                const auto category = static_cast<std::size_t>(row.category);
                if (category >= categories.size()) {
                    throw std::out_of_range("catalog UI category");
                }
                if (!categories[category]) {
                    categories[category] = submenu(build, labels[category]);
                }
                target = categories[category];
            }
            const auto command = first_catalog + static_cast<UINT>(command_definitions.size());
            command_definitions.push_back(row.definition);
            const auto label = std::format(L"{}  ${}{}", wide(row.name), row.cost, availability_label(row.availability));
            item(target, command, label.c_str(), row.availability == ParkCatalogAvailability::available);
        }
        if (GetMenuItemCount(upgrades) == 0) {
            item(upgrades, 0, L"No add-ons in this park", false);
        }
        const auto level = controller.level_upgrade();
        std::wstring label = std::format(L"Request level {} upgrade  ${} (charged on completion)", level.level + 1u, level.cost);
        match (level.availability) {
            case ParkLevelUpgradeAvailability::no_selection       => label = L"Select an object for level upgrades";
            case ParkLevelUpgradeAvailability::not_upgradeable    => label = L"This object has no level upgrades";
            case ParkLevelUpgradeAvailability::maximum_level      => label = L"Fully upgraded";
            case ParkLevelUpgradeAvailability::research_locked    => label += L" [research locked]";
            case ParkLevelUpgradeAvailability::needs_mechanic     => label += L" [train a mechanic in ride upgrading]";
            case ParkLevelUpgradeAvailability::insufficient_funds => label += L" [insufficient funds]";
            case ParkLevelUpgradeAvailability::pending            => label = L"Level upgrade awaiting a mechanic";
            case ParkLevelUpgradeAvailability::available          => {
            }
            case _ => {
            }
        } const auto request_flags = MF_BYCOMMAND | MF_STRING | (level.availability == ParkLevelUpgradeAvailability::available ? MF_ENABLED : MF_GRAYED);
        ModifyMenuW(selected_menu, request_level, request_flags, request_level, label.c_str());
        EnableMenuItem(selected_menu, cancel_level, MF_BYCOMMAND | (level.availability == ParkLevelUpgradeAvailability::pending ? MF_ENABLED : MF_GRAYED));
    }

    std::optional<WorldCellCoordinates> cursor() const {
        if (!width || !height) {
            return std::nullopt;
        }
        const auto hit = pick_world_interaction_cursor(context, camera, terrain, { x, y }, width, height);
        if (!hit || !hit->cell) {
            return std::nullopt;
        }
        return WorldCellCoordinates { static_cast<int>((hit->cell - 1u) % 128u), static_cast<int>((hit->cell - 1u) / 128u) };
    }

    void import_coaster() {
        if (controller.replaying()) {
            return;
        }
        IFileOpenDialog* raw {};
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(&raw)))) {
            throw std::runtime_error("Cannot open coaster design dialog");
        }

        struct Owner {
            IFileOpenDialog* p;

            ~Owner() {
                p->Release();
            }
        } owner { raw };

        const COMDLG_FILTERSPEC filters[] { { L"SimCoaster coaster design", L"*.cos" }, { L"All files", L"*.*" } };
        (void)raw->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
        (void)raw->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
        const auto shown = raw->Show(window);
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            return;
        }
        if (FAILED(shown)) {
            throw std::runtime_error("Coaster design dialog failed");
        }
        IShellItem* item {};
        if (FAILED(raw->GetResult(&item))) {
            throw std::runtime_error("No coaster design selected");
        }

        struct Item {
            IShellItem* p;

            ~Item() {
                p->Release();
            }
        } result { item };

        PWSTR path {};
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            throw std::runtime_error("Coaster design has no filesystem path");
        }

        struct Path {
            PWSTR p;

            ~Path() {
                CoTaskMemFree(p);
            }
        } selected { path };

        (void)controller.choose_design(std::filesystem::path { path });
    }

    bool save() {
        DialogOwner owner;
        const auto created = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileSaveDialog, reinterpret_cast<void**>(&owner.dialog));
        if (FAILED(created)) {
            throw std::runtime_error("Cannot open Save Park dialog");
        }
        const auto pattern = L"*" + original.extension().wstring();
        const COMDLG_FILTERSPEC filters[] { { L"SimCoaster saved park", pattern.c_str() }, { L"All files", L"*.*" } };
        if (FAILED(owner.dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters))) {
            throw std::runtime_error("Cannot configure Save Park dialog");
        }
        const auto filename = original.stem().wstring() + L"-rebuilt" + original.extension().wstring();
        (void)owner.dialog->SetFileName(filename.c_str());
        if (!original.extension().empty()) {
            const auto extension = original.extension().wstring().substr(1);
            (void)owner.dialog->SetDefaultExtension(extension.c_str());
        }
        DWORD options {};
        if (SUCCEEDED(owner.dialog->GetOptions(&options))) {
            (void)owner.dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST);
        }
        const auto shown = owner.dialog->Show(window);
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            return false;
        }
        if (FAILED(shown)) {
            throw std::runtime_error("Save Park dialog failed");
        }
        IShellItem* result {};
        if (FAILED(owner.dialog->GetResult(&result))) {
            throw std::runtime_error("No save file selected");
        }

        struct ResultOwner {
            IShellItem* item;

            ~ResultOwner() {
                item->Release();
            }
        } result_owner { result };

        PWSTR path {};
        if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            throw std::runtime_error("Save file is not a filesystem path");
        }

        struct PathOwner {
            PWSTR path;

            ~PathOwner() {
                CoTaskMemFree(path);
            }
        } path_owner { path };

        controller.save(std::filesystem::path { path }, native_modifiers());
        dirty = false;
        saved_tick = context.simulation.current->saved.game_tick;
        return true;
    }

    void command(UINT value) {
        if (value >= first_catalog) {
            const auto index = static_cast<std::size_t>(value - first_catalog);
            if (index < command_definitions.size()) {
                (void)controller.choose_object(command_definitions[index]);
            }
            if (!controller.replaying()) {
                controller.hover(cursor(), native_modifiers());
            }
            if (controller.tool() != ParkAuthoringTool::inspect) {
                staff_active = false;
            }
            return;
        }
        match (value) {
            case import_design => import_coaster();
            case save_as       => (void)save();
            case inspect       => (void)controller.choose_tool(ParkAuthoringTool::inspect);
            case build_path    => (void)controller.choose_tool(ParkAuthoringTool::path);
            case build_queue   => (void)controller.choose_tool(ParkAuthoringTool::queue);
            case build_track   => dirty = controller.choose_tool(ParkAuthoringTool::track) || dirty;
            case demolish      => (void)controller.choose_tool(ParkAuthoringTool::demolish);
            case erase_path    => (void)controller.choose_tool(ParkAuthoringTool::erase_path);
            case erase_track   => dirty = controller.choose_tool(ParkAuthoringTool::erase_track) || dirty;
            case activate      => dirty = controller.activate_selected() || dirty;
            case open_ride     => dirty = controller.open_selected() || dirty;
            case close_ride    => dirty = controller.close_selected() || dirty;
            case open_park     => dirty = controller.set_park_open(true) || dirty;
            case close_park    => dirty = controller.set_park_open(false) || dirty;
            case request_level => dirty = controller.request_level_upgrade() || dirty;
            case cancel_level  => dirty = controller.cancel_level_upgrade() || dirty;
            case cancel_tool   => controller.cancel_interaction(native_modifiers());
            case rotate_tool   => controller.rotate();
            case select_node   => {
                if (const auto position = cursor()) {
                    (void)controller.select_coaster_node(*position);
                }
            }
            case add_node         => (void)controller.stage_coaster_node();
            case add_special_node => (void)controller.stage_coaster_node(true);
            case add_pylon_node   => (void)controller.stage_coaster_node(false, true);
            case move_node        => (void)controller.move_coaster_node();
            case finish_coaster   => dirty = controller.finalize_coaster() || dirty;
            case help             => {
                MessageBoxW(
                    window,
                    L"Build: choose installed objects from the Build menu. R " L"rotates. Click to place.\n" L"Paths, queues and tracks: click a start and then " L"successive end points.\n" L"Select: click an object, then use Selected for opening, " L"activation or add-ons.\n" L"Escape or right click finishes the current tool. Control " L"keeps a placement tool active.\n" L"Hold Alt while dragging to orbit or pan during " L"construction. The mouse wheel zooms.\n" L"Right click while no tool is active opens the existing " L"staff controls.\n" L"Space pauses the park. Ctrl+Shift+S saves a rebuilt copy.",
                    L"SimCoaster park authoring",
                    MB_OK | MB_ICONINFORMATION);
            }
            case _ => {
            }
        } if (!controller.replaying()) {
            controller.hover(cursor(), native_modifiers());
        }
        if (controller.tool() != ParkAuthoringTool::inspect) {
            staff_active = false;
        }
    }
};

ParkAuthoringFrontend::ParkAuthoringFrontend(ParkRuntime& park, SceneCamera& camera, TerrainGridView terrain, const std::filesystem::path& save, HWND window, bool diagnostic_ui, std::function<void()> release_pointer, UiCursorWin32* cursors) : state_(std::make_unique<State>(park, camera, terrain, save, window, diagnostic_ui, std::move(release_pointer), cursors)) {}

ParkAuthoringFrontend::~ParkAuthoringFrontend() = default;

void ParkAuthoringFrontend::begin_world_map_press(HWND window, std::array<std::int32_t, 2> pixel, WorldCellCoordinates cell, WorldInputModifierMask modifiers) {
    auto& s = *state_;
    s.synchronize_pointer_tracking();
    s.controller.hover(cell, modifiers);
    if (s.controller.tool() == ParkAuthoringTool::inspect) {
        return;
    }
    s.staff_active = false;
    s.x = s.down_x = pixel[0];
    s.y = s.down_y = pixel[1];
    s.dragged = s.camera_gesture = false;
    s.pointer_down = true;
    s.controller.mouse_down(cell, modifiers);
    s.dirty = true;
    acquire_window_pointer_capture(window);
}

void ParkAuthoringFrontend::cancel_pointer_input() {
    auto& s = *state_;
    s.synchronize_pointer_tracking();
    const bool construction_press = s.pointer_down && !s.camera_gesture;
    s.pointer_down = s.camera_gesture = false;
    if (construction_press) {
        s.controller.cancel_pointer_input(native_modifiers());
    }
}

void ParkAuthoringFrontend::synchronize_from_world() {
    auto& s = *state_;
    s.controller.synchronize_from_world();
    s.pointer_down = s.dragged = s.camera_gesture = false;
    s.interaction_revision = s.controller.interaction_revision();
}

bool ParkAuthoringFrontend::pointer_active() const {
    state_->synchronize_pointer_tracking();
    return state_->pointer_down || state_->camera_gesture;
}

void ParkAuthoringFrontend::note_external_change() {
    state_->dirty = true;
}

bool ParkAuthoringFrontend::delete_object(ThingId id) {
    const bool changed = state_->controller.delete_object(id);
    state_->dirty |= changed;
    return changed;
}

bool ParkAuthoringFrontend::buy_object(ThingId id) {
    return state_->controller.buy_object(id);
}

bool ParkAuthoringFrontend::select_tool(ParkAuthoringTool tool) {
    return state_->controller.choose_tool(tool);
}

bool ParkAuthoringFrontend::message(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CAPTURECHANGED && retained_window_pointer_capture(window, lp)) {
        return false;
    }
    auto& s = *state_;
    s.synchronize_pointer_tracking();
    try {
        if (!s.diagnostic_ui && (message == WM_INITMENU || message == WM_COMMAND || message == WM_CLOSE || message == WM_KEYDOWN || message == WM_KEYUP)) {
            return false;
        }
        if (message == WM_INITMENU) {
            s.rebuild_catalog();
            return false;
        }
        if (message == WM_COMMAND && HIWORD(wp) == 0) {
            s.command(LOWORD(wp));
            return true;
        }
        if (message == WM_CLOSE && !s.allow_close) {
            if (s.has_changes()) {
                const auto choice = MessageBoxW(window, L"Save park changes before closing?", L"SimCoaster park", MB_YESNOCANCEL | MB_ICONQUESTION);
                if (choice == IDCANCEL || (choice == IDYES && !s.save())) {
                    return true;
                }
            }
            s.controller.cancel(native_modifiers());
            s.allow_close = true;
            return false;
        }
        if (s.controller.replaying()) {
            if (message == WM_KEYDOWN && wp == VK_ESCAPE) {
                s.controller.cancel(native_modifiers());
                return true;
            }
            if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_MOUSEMOVE) {
                return true;
            }
            if (message == WM_KEYDOWN && wp != VK_SPACE && wp != VK_F12) {
                return true;
            }
        }
        if (message == WM_KEYDOWN && !(lp & (std::intptr_t { 1 } << 30))) {
            UINT command = 0;
            match (wp) {
                case 'S' => {
                    if ((GetKeyState(VK_CONTROL) < 0) && (GetKeyState(VK_SHIFT) < 0)) {
                        command = save_as;
                    }
                }
                case 'V'       => command = inspect;
                case 'P'       => command = build_path;
                case 'Q'       => command = build_queue;
                case 'T'       => command = build_track;
                case 'R'       => command = rotate_tool;
                case 'N'       => command = select_node;
                case 'G'       => command = add_node;
                case 'H'       => command = add_special_node;
                case 'M'       => command = move_node;
                case VK_RETURN => {
                    if (!s.staff_active) {
                        command = finish_coaster;
                    }
                }
                case VK_DELETE => command = demolish;
                case VK_ESCAPE => {
                    if (s.controller.tool() != ParkAuthoringTool::inspect) {
                        command = cancel_tool;
                    } else if (!s.staff_active) {
                        SendMessageW(window, WM_CLOSE, 0, 0);
                        return true;
                    }
                }
                case 'B' => {
                    s.rebuild_catalog();
                    POINT point { s.x, s.y };
                    ClientToScreen(window, &point);
                    const auto selected = TrackPopupMenu(s.build, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
                    if (selected) {
                        s.command(static_cast<UINT>(selected));
                    }
                    return true;
                }
                case _ => {
                }
            } if (command) {
                s.command(command);
                return true;
            }
        }
        if (message == WM_MOUSEMOVE) {
            s.x = static_cast<short>(LOWORD(lp));
            s.y = static_cast<short>(HIWORD(lp));
            if (s.pointer_down) {
                s.dragged |= std::abs(s.x - s.down_x) > 3 || std::abs(s.y - s.down_y) > 3;
            }
            if (s.controller.tool() != ParkAuthoringTool::inspect && !s.camera_gesture) {
                s.controller.hover(s.cursor(), native_modifiers());
            }
            return s.controller.tool() != ParkAuthoringTool::inspect && !s.camera_gesture;
        }
        if (s.staff_active && (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)) {
            return false;
        }
        if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN) {
            s.x = static_cast<short>(LOWORD(lp));
            s.y = static_cast<short>(HIWORD(lp));
            s.down_x = s.x;
            s.down_y = s.y;
            s.dragged = false;
            s.pointer_down = message == WM_LBUTTONDOWN;
            s.camera_gesture = GetKeyState(VK_MENU) < 0;
            if (s.controller.tool() != ParkAuthoringTool::inspect && !s.camera_gesture) {
                if (message == WM_LBUTTONDOWN) {
                    if (const auto cursor = s.cursor()) {
                        s.controller.mouse_down(*cursor, native_modifiers());
                        s.dirty = true;
                    }
                }
                acquire_window_pointer_capture(window);
                return true;
            }
        }
        if (message == WM_LBUTTONUP || message == WM_RBUTTONUP) {
            s.x = static_cast<short>(LOWORD(lp));
            s.y = static_cast<short>(HIWORD(lp));
            const auto active = s.controller.tool() != ParkAuthoringTool::inspect;
            if (!s.camera_gesture && message == WM_RBUTTONUP && active) {
                s.controller.cancel_interaction(native_modifiers());
                s.pointer_down = s.camera_gesture = false;
                s.release_capture();
                return true;
            }
            if (!s.camera_gesture && message == WM_LBUTTONUP && s.pointer_down && (!s.dragged || active)) {
                if (const auto cursor = s.cursor()) {
                    const bool changed = s.controller.click(*cursor, native_modifiers());
                    s.dirty |= changed && active;
                } else {
                    s.controller.cancel_pointer_input(native_modifiers());
                }
            }
            s.pointer_down = false;
            if (active && !s.camera_gesture) {
                s.release_capture();
                return true;
            }
            s.camera_gesture = false;
        }
        if (message == WM_CAPTURECHANGED) {
            cancel_pointer_input();
        }
    } catch (const std::exception& error) {
        const std::string narrow = error.what();
        const std::wstring description { narrow.begin(), narrow.end() };
        MessageBoxW(window, description.c_str(), L"Park action could not complete", MB_OK | MB_ICONERROR);
        return true;
    }
    return false;
}

void ParkAuthoringFrontend::frame(HWND window, std::uint32_t width, std::uint32_t height, bool staff_editing, bool paused, bool path_hover_inspection) {
    auto& s = *state_;
    s.synchronize_pointer_tracking();
    s.width = width;
    s.height = height;
    s.staff_active = staff_editing;
    const bool replaying = s.controller.replaying();
    if (s.diagnostic_ui && replaying != s.replay_disabled) {
        for (UINT position = 1; position <= 3; ++position) {
            EnableMenuItem(s.menu, position, MF_BYPOSITION | (replaying ? MF_GRAYED : MF_ENABLED));
        }
        s.replay_disabled = replaying;
        DrawMenuBar(window);
    }
    if (replaying) {
        if (s.diagnostic_ui) {
            SetWindowTextW(window, L"SimCoaster park | Replaying layout | Escape: stop | Space: pause");
        }
        if (s.cursors) {
            s.cursors->set(UiCursorId::busy);
        } else if (s.diagnostic_ui) {
            SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        }
        return;
    }
    if (staff_editing || path_hover_inspection) {
        return;
    }
    //A retired selected object is invalidated through the engine owner before
    //it can be used by a later menu command or a recycled ThingId.
    (void)s.controller.selected();
    s.controller.hover(s.cursor(), native_modifiers());
    if (!s.diagnostic_ui) {
        return;
    }
    const auto& feedback = s.controller.feedback();
    auto title = L"SimCoaster park | " + tool_label(s.controller.tool());
    if (const auto name = s.controller.selected_name(); !name.empty()) {
        title += L" | " + wide(name);
    }
    if (s.controller.tool() != ParkAuthoringTool::inspect) {
        const auto tool = s.controller.tool();
        if (tool == ParkAuthoringTool::object || tool == ParkAuthoringTool::path || tool == ParkAuthoringTool::queue || tool == ParkAuthoringTool::track) {
            std::format_to(std::back_inserter(title), L" | Cost ${}", feedback.cost);
        }
        std::format_to(std::back_inserter(title), L" | Cash ${}", feedback.balance);
        if (feedback.insufficient_funds) {
            title += L" | Insufficient funds";
        } else if (feedback.cursor && !feedback.accepted) {
            title += feedback.rejection.empty() ? L" | Cannot build here" : L" | " + wide(feedback.rejection);
        }
        if (s.controller.tool() == ParkAuthoringTool::object) {
            std::format_to(std::back_inserter(title), L" | R: rotate {} degrees", feedback.rotation);
        }
        title += L" | Escape: finish | Alt+drag: camera";
        if (s.cursors) {
            s.cursors->set(feedback.accepted ? UiCursorId::crosshair : UiCursorId::cannot_place);
        } else {
            SetCursor(LoadCursorW(nullptr, feedback.accepted ? IDC_CROSS : IDC_NO));
        }
    } else {
        title += L" | B: Build | right click: staff | Ctrl+Shift+S: save";
    }
    if (paused) {
        title += L" | Paused";
    }
    if (s.has_changes()) {
        title += L" *";
    }
    if (!feedback.message.empty()) {
        title += L" | " + wide(feedback.message);
    }
    if (s.diagnostic_ui) {
        SetWindowTextW(window, title.c_str());
    }
}
} //namespace simcoaster
