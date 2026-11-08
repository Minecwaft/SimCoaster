#pragma once
#include "simcoaster/texture_image.hpp"
#include <filesystem>

namespace simcoaster {
TextureImage load_startup_png(const std::filesystem::path&);
} //namespace simcoaster
