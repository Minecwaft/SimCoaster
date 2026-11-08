#pragma once
#include <stdexcept>

namespace simcoaster {
template <class Error = std::logic_error, class Pointer>
decltype(auto) require_bound(Pointer&& value, const char* message) {
    if (!value) {
        throw Error(message);
    }
    return *value;
}
} //namespace simcoaster
