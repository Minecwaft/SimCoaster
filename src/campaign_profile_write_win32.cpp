#include "campaign_profile_io.hpp"
#include <algorithm>
#include <limits>
#include <system_error>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

namespace simcoaster {
using campaign_detail::io_error;

std::expected<void, CampaignProfileError> write_campaign_profile(const std::filesystem::path& path,
                                                                 const CampaignProfile& profile) {
    auto bytes = encode_campaign_profile(profile);
    if (!bytes) {
        bytes.error().path = path;
        return std::unexpected(bytes.error());
    }
    //Stage a complete image beside the destination, then publish with Windows'
    //replace operation. A short write cannot destroy an existing named profile.
    auto temporary = path;
    temporary += L".pending-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return std::unexpected(io_error(path,
                                        "Cannot create staged campaign profile: "
                                            + std::system_category().message(static_cast<int>(GetLastError()))));
    bool written = true;
    std::size_t offset{};
    while (offset < bytes->size()) {
        const auto count =
            static_cast<DWORD>(std::min<std::size_t>(bytes->size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD completed{};
        if (!WriteFile(file, bytes->data() + offset, count, &completed, nullptr) || completed != count) {
            written = false;
            break;
        }
        offset += completed;
    }
    written = written && FlushFileBuffers(file);
    const auto write_error = GetLastError();
    CloseHandle(file);
    if (!written) {
        DeleteFileW(temporary.c_str());
        return std::unexpected(io_error(path,
                                        "Cannot write staged campaign profile: "
                                            + std::system_category().message(static_cast<int>(write_error))));
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        DeleteFileW(temporary.c_str());
        return std::unexpected(
            io_error(path,
                     "Cannot publish campaign profile: " + std::system_category().message(static_cast<int>(error))));
    }
    return {};
}
} //namespace simcoaster
