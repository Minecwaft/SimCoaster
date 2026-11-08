#pragma once
#include <windows.h>

namespace simcoaster {
//Capture belongs to the HWND shared by UI, construction and World gestures.
//SetCapture on an already owning HWND sends a synchronous self notification
//on Windows; repeated acquisition must not interrupt those existing owners.
inline void acquire_window_pointer_capture(HWND window) {
    if (GetCapture() != window) {
        SetCapture(window);
    }
}

//External callers can still repeat SetCapture directly. Ignore only a
//notification naming this HWND while it really retains capture; a release
//or transition to any other owner must run ordinary interruption cleanup.
inline bool retained_window_pointer_capture(HWND window, LPARAM next_owner) {
    return reinterpret_cast<HWND>(next_owner) == window && GetCapture() == window;
}
} //namespace simcoaster
