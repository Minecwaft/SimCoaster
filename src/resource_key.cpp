#include "simcoaster/resource_key.hpp"

namespace simcoaster {
std::string ascii_lowercase(std::string_view text) {
    std::string result{ text };
    for (char& c : result)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return result;
}

std::string canonical_asset_key(std::string text) {
    for (auto& c : text) {
        if (c == '\\')
            c = '/';
        else if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    }
    return text;
}

bool resource_name_matches(std::string_view pattern, std::string_view text) {
    std::size_t p = 0, t = 0, star = std::string_view::npos, retry = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            retry = t;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            t = ++retry;
        } else
            return false;
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}
} //namespace simcoaster
