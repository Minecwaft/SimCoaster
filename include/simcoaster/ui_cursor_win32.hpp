#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <meta>
#include <optional>
#include <string_view>

namespace simcoaster {
namespace ui_cursor_detail {
struct Asset {
    const char* filename;

    consteval Asset(std::string_view name) : filename(std::define_static_string(name)) {}
};
} //namespace ui_cursor_detail

//Exact presentation IDs and filenames loaded by native5093D0. Keep the
//recovered misspelling c_crosh in its resource metadata.
enum class UiCursorId : std::uint32_t {
    normal [[=ui_cursor_detail::Asset{ "c_norm.ani" }]] = 0,
    busy [[=ui_cursor_detail::Asset{ "c_busy.ani" }]] = 1,
    pick [[=ui_cursor_detail::Asset{ "c_pick.ani" }]] = 2,
    path [[=ui_cursor_detail::Asset{ "c_path.ani" }]] = 3,
    queue [[=ui_cursor_detail::Asset{ "c_queue.ani" }]] = 4,
    link [[=ui_cursor_detail::Asset{ "c_link.ani" }]] = 5,
    track [[=ui_cursor_detail::Asset{ "c_track.ani" }]] = 6,
    place [[=ui_cursor_detail::Asset{ "c_place.ani" }]] = 7,
    cannot_place [[=ui_cursor_detail::Asset{ "c_noplace.cur" }]] = 8,
    carry [[=ui_cursor_detail::Asset{ "c_carry.ani" }]] = 9,
    erase [[=ui_cursor_detail::Asset{ "c_erase.ani" }]] = 10,
    pylon [[=ui_cursor_detail::Asset{ "c_pylon.ani" }]] = 11,
    height [[=ui_cursor_detail::Asset{ "c_height.ani" }]] = 12,
    rotate [[=ui_cursor_detail::Asset{ "c_rotate.ani" }]] = 13,
    pivot [[=ui_cursor_detail::Asset{ "c_pivot.ani" }]] = 14,
    swap_down [[=ui_cursor_detail::Asset{ "c_swapdown.ani" }]] = 15,
    swap_up [[=ui_cursor_detail::Asset{ "c_swapup.ani" }]] = 16,
    move [[=ui_cursor_detail::Asset{ "c_move.ani" }]] = 17,
    end [[=ui_cursor_detail::Asset{ "c_end.ani" }]] = 18,
    camera [[=ui_cursor_detail::Asset{ "c_cam.ani" }]] = 19,
    cash [[=ui_cursor_detail::Asset{ "c_cash.ani" }]] = 20,
    crosshair [[=ui_cursor_detail::Asset{ "c_crosh.ani" }]] = 21,
    horizontal [[=ui_cursor_detail::Asset{ "c_horiz.ani" }]] = 22,
    vertical [[=ui_cursor_detail::Asset{ "c_vert.ani" }]] = 23,
    diagonal_1 [[=ui_cursor_detail::Asset{ "c_diag1.ani" }]] = 24,
    diagonal_2 [[=ui_cursor_detail::Asset{ "c_diag2.ani" }]] = 25
};

//UI-thread owner of the original cursor catalog. LoadCursorFromFileW returns
//shared handles: neither this provider nor its caller may DestroyCursor them.
//Windows retains ANI frame/rate handling; no game-frame animation timer is
//used.
class UiCursorWin32 {
    struct State;
    std::unique_ptr<State> state_;

public:
    explicit UiCursorWin32(const std::filesystem::path& game_root);
    ~UiCursorWin32();
    UiCursorWin32(const UiCursorWin32&) = delete;
    UiCursorWin32& operator=(const UiCursorWin32&) = delete;
    //Call while the pointer is in this viewer's client area or is captured.
    //Missing/invalid assets fail construction; unknown IDs and cross-thread
    //mutations throw. A null SetCursor return is a valid previous-cursor state.
    void set(UiCursorId);
    //Native8507F7 retains visibility across asset changes. Mouse look owns
    //this state until release; set() may update the desired asset meanwhile.
    void set_visible(bool);
    //Restore the shared stock arrow only if this provider's last selection is
    //still current (including its hidden presentation), leaving another
    //component's installed cursor alone. Releases the visibility override.
    void reset();
    std::optional<UiCursorId> current() const;
    const std::filesystem::path& source(UiCursorId) const;

    static constexpr std::size_t size() {
        return 26;
    }
};
} //namespace simcoaster
