#include "simcoaster/file_platform.hpp"
#include <system_error>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

namespace simcoaster::file_platform {
DirectoryIdentity directory_identity(const std::filesystem::path& path) {
    const auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::filesystem::filesystem_error("Cannot inspect profile directory identity", path, std::error_code(static_cast<int>(GetLastError()), std::system_category()));
    }
    BY_HANDLE_FILE_INFORMATION info {};
    const auto success = GetFileInformationByHandle(handle, &info);
    const auto failure = success ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!success) {
        throw std::filesystem::filesystem_error("Cannot read profile directory identity", path, std::error_code(static_cast<int>(failure), std::system_category()));
    }
    return { info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow };
}

bool ordinal_equal(std::wstring_view a, std::wstring_view b) {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::expected<void, std::uint32_t> remove_file(const std::filesystem::path& path) {
    if (DeleteFileW(path.c_str())) {
        return {};
    }
    return std::unexpected(GetLastError());
}
} //namespace simcoaster::file_platform
