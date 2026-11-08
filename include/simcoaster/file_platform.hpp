#pragma once
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string_view>

namespace simcoaster::file_platform {
struct DirectoryIdentity {
    std::uint32_t volume{}, index_high{}, index_low{};
    bool operator==(const DirectoryIdentity&) const = default;
};

DirectoryIdentity directory_identity(const std::filesystem::path&);
bool ordinal_equal(std::wstring_view, std::wstring_view);
std::expected<void, std::uint32_t> remove_file(const std::filesystem::path&);
} //namespace simcoaster::file_platform
