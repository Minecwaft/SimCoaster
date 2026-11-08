#pragma once
#include <concepts>
#include <cstdio>
#include <format>
#include <print>
#include <string>
#include <system_error>

namespace simcoaster {
//Native resource names and diagnostics can contain Windows-encoded bytes.
//Do not reinterpret them as UTF-8 when stdout/stderr refers to a console.
//Like the replaced console streams, failed writes do not change CLI exit codes.
template <class... Args>
bool print_text(std::FILE* output, std::format_string<Args...> pattern, Args&&... args) {
    try {
        std::vprint_nonunicode(output, pattern.get(), std::make_format_args(args...));
        return true;
    } catch (const std::system_error&) {
        return false;
    }
}

//Only ASCII numeric output is widened here. BFST strings keep their original
//conversion table and UTF-16 representation. Promotion keeps bool/char numeric.
template <class T>
    requires(std::integral<T> || std::floating_point<T>)
std::u16string number_text(T value, std::format_string<decltype(+value)> pattern = "{}") {
    const auto text = std::format(pattern, +value);
    return { text.begin(), text.end() };
}
} //namespace simcoaster
