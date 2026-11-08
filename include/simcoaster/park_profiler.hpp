#pragma once
#include <filesystem>
#include <span>
#include <string_view>

namespace simcoaster {
//Offscreen diagnostic only. Never saves a park or handles application requests.
int run_park_profiler(std::span<const std::string_view>, const std::filesystem::path& default_root = {});
} //namespace simcoaster
