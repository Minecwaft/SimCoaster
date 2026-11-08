#pragma once
#include <string>
#include <string_view>

namespace simcoaster {
std::string ascii_lowercase(std::string_view);
std::string canonical_asset_key(std::string);
bool resource_name_matches(std::string_view pattern, std::string_view text);
} //namespace simcoaster
