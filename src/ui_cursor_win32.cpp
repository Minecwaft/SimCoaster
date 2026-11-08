#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include "simcoaster/ui_cursor_win32.hpp"
#include <format>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <windows.h>

namespace simcoaster {
namespace {
struct CursorSpec {
    UiCursorId id {};
    const char* filename {};
};

consteval auto cursor_specs() {
    std::array<CursorSpec, UiCursorWin32::size()> result {};
    std::size_t index {};
    for (const auto enumerator : std::meta::enumerators_of(^^UiCursorId)) {
        const auto assets = std::meta::annotations_of_with_type(enumerator, ^^ui_cursor_detail::Asset);
        if (assets.size() != 1) {
            throw "Each native UI cursor needs exactly one asset";
        }
        const auto id = std::meta::extract<UiCursorId>(enumerator);
        if (static_cast<std::size_t>(id) != index || index >= result.size()) {
            throw "Native UI cursor IDs must be contiguous and keep their original " "order";
        }
        result[index++] = { id, std::meta::extract<ui_cursor_detail::Asset>(std::meta::constant_of(assets.front())).filename };
    }
    if (index != result.size()) {
        throw "Incomplete native UI cursor catalog";
    }
    return result;
}

inline constexpr auto specs = cursor_specs();

std::string lower(std::string value) {
    for (auto& c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return value;
}

std::filesystem::path child(const std::filesystem::path& parent, std::string_view name, bool directory) {
    const auto expected = parent / name;
    const auto matches_type = [&](const std::filesystem::path& path) {
        return directory ? std::filesystem::is_directory(path) : std::filesystem::is_regular_file(path);
    };
    if (matches_type(expected)) {
        return expected;
    }
    //The target is Win32, but a checkout can reside in a directory explicitly
    //marked case-sensitive. Preserve the game's ASCII-insensitive asset lookup.
    std::optional<std::filesystem::path> found;
    if (std::filesystem::is_directory(parent)) {
        for (const auto& entry : std::filesystem::directory_iterator(parent)) {
            if (lower(entry.path().filename().native_encoded_string()) == name && matches_type(entry.path())) {
                if (found) {
                    throw std::runtime_error(std::format("Ambiguous UI cursor path: {}", expected.native_encoded_string()));
                }
                found = entry.path();
            }
        }
    }
    if (!found) {
        throw std::runtime_error(std::format("Missing UI cursor path: {}", expected.native_encoded_string()));
    }
    return *found;
}

std::size_t cursor_index(UiCursorId id) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= specs.size()) {
        throw std::out_of_range(std::format("Unknown native UI cursor ID {}", static_cast<std::uint32_t>(id)));
    }
    return index;
}

[[noreturn]] void win32_failure(const std::filesystem::path& path, DWORD error) {
    throw std::runtime_error(std::format("Cannot load UI cursor {} (Win32 error {})", path.native_encoded_string(), error));
}
} //namespace

struct UiCursorWin32::State {
    DWORD thread = GetCurrentThreadId();
    std::array<HCURSOR, UiCursorWin32::size()> handles {};
    std::array<std::filesystem::path, UiCursorWin32::size()> sources;
    HCURSOR stock_arrow {};
    HCURSOR selected {};
    bool visible = true, installed {};

    explicit State(const std::filesystem::path& root) {
        const auto directory = child(child(child(root, "data", true), "ui", true), "cursors", true);
        stock_arrow = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        if (!stock_arrow) {
            win32_failure("shared stock arrow", GetLastError());
        }
        for (const auto& spec : specs) {
            const auto index = cursor_index(spec.id);
            sources[index] = child(directory, spec.filename, false);
            handles[index] = LoadCursorFromFileW(sources[index].c_str());
            if (!handles[index]) {
                win32_failure(sources[index], GetLastError());
            }
        }
    }

    void require_thread() const {
        if (GetCurrentThreadId() != thread) {
            throw std::logic_error("UI cursor mutation requires its owning Win32 thread");
        }
    }

    void apply() {
        const auto cursor = visible ? (selected ? selected : stock_arrow) : nullptr;
        if (GetCursor() != cursor) {
            SetCursor(cursor);
        }
        installed = true;
    }

    void reset() {
        if (installed && GetCursor() == (visible ? selected : nullptr)) {
            SetCursor(stock_arrow);
        }
        selected = nullptr;
        visible = true;
        installed = false;
    }

    ~State() {
        //Shared file cursors must not be destroyed. If a host tears down the
        //provider elsewhere, its old UI thread still holds a valid shared handle;
        //do not alter an unrelated thread's cursor while attempting restoration.
        if (GetCurrentThreadId() == thread) {
            reset();
        }
    }
};

UiCursorWin32::UiCursorWin32(const std::filesystem::path& root) : state_(std::make_unique<State>(root)) {}

UiCursorWin32::~UiCursorWin32() = default;

void UiCursorWin32::set(UiCursorId id) {
    state_->require_thread();
    state_->selected = state_->handles[cursor_index(id)];
    state_->apply();
}

void UiCursorWin32::set_visible(bool visible) {
    state_->require_thread();
    state_->visible = visible;
    if (state_->installed || !visible) {
        state_->apply();
    }
}

void UiCursorWin32::reset() {
    state_->require_thread();
    state_->reset();
}

std::optional<UiCursorId> UiCursorWin32::current() const {
    state_->require_thread();
    const auto found = std::ranges::find(state_->handles, GetCursor());
    return found == state_->handles.end() ? std::nullopt : std::optional { specs[static_cast<std::size_t>(found - state_->handles.begin())].id };
}

const std::filesystem::path& UiCursorWin32::source(UiCursorId id) const {
    return state_->sources[cursor_index(id)];
}
} //namespace simcoaster
