#include "application_session_internal.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

namespace simcoaster::application_session_detail {
bool equal_path(const std::filesystem::path& a, const std::filesystem::path& b) {
    const auto first = std::filesystem::absolute(a).lexically_normal().make_preferred().native();
    const auto second = std::filesystem::absolute(b).lexically_normal().make_preferred().native();
    return CompareStringOrdinal(first.data(), static_cast<int>(first.size()), second.data(), static_cast<int>(second.size()), TRUE) == CSTR_EQUAL;
}

std::array<std::uint32_t, 7> file_stamp(const std::filesystem::path& path) {
    const auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        fail(ApplicationSessionFailure::load, path, "Cannot inspect file identity: " + std::to_string(GetLastError()));
    }
    BY_HANDLE_FILE_INFORMATION info {};
    const auto success = GetFileInformationByHandle(handle, &info);
    const auto error = GetLastError();
    CloseHandle(handle);
    if (!success || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        fail(ApplicationSessionFailure::load, path, "Invalid save-file identity: " + std::to_string(error));
    }
    return { info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow, info.nFileSizeHigh, info.nFileSizeLow, info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime };
}

ApplicationFileDate file_date(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data {};
    FILETIME local {};
    SYSTEMTIME value {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) || !FileTimeToLocalFileTime(&data.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &value)) {
        fail(ApplicationSessionFailure::load, path, "Cannot read native local save date");
    }
    return { value.wYear, value.wMonth, value.wDay, value.wHour, value.wMinute };
}

FileTransaction::FileTransaction(const std::filesystem::path& root) {
    static std::atomic<std::uint64_t> counter {};
    const auto parent = std::filesystem::absolute(root / "save").lexically_normal();
    std::filesystem::create_directories(parent);
    directory_ = parent / (".application-stage-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++counter));
    if (directory_.parent_path() != parent || !std::filesystem::create_directory(directory_)) {
        fail(ApplicationSessionFailure::save, directory_, "Cannot create owned save staging directory");
    }
}

FileTransaction::~FileTransaction() {
    //This absolute directory was uniquely created by this transaction, directly
    //under save/. It never receives profile paths or user-supplied components.
    std::error_code ignored;
    if (!preserve_) {
        std::filesystem::remove_all(directory_, ignored);
    }
}

std::filesystem::path FileTransaction::stage(const std::filesystem::path& destination) {
    for (const auto& entry : entries_) {
        if (equal_path(entry.destination, destination)) {
            return entry.staged;
        }
    }
    const auto index = std::to_string(entries_.size());
    auto path = directory_ / (index + destination.extension().native_encoded_string());
    entries_.push_back({ destination, path, directory_ / (index + ".backup") });
    return path;
}

std::filesystem::path FileTransaction::read(const std::filesystem::path& path) const {
    for (const auto& entry : entries_) {
        if (equal_path(entry.destination, path)) {
            return entry.staged;
        }
    }
    return path;
}

void FileTransaction::copy(const std::filesystem::path& source, const std::filesystem::path& destination) {
    const auto original = read(source);
    if (equal_path(original, destination)) {
        return;
    }
    const auto staged = stage(destination);
    if (equal_path(original, staged)) {
        return;
    }
    std::filesystem::copy_file(original, staged, std::filesystem::copy_options::overwrite_existing);
}

void FileTransaction::commit() {
    for (auto& entry : entries_) {
        if (!std::filesystem::is_regular_file(entry.staged)) {
            fail(ApplicationSessionFailure::save, entry.staged, "Staged output is absent");
        }
        std::filesystem::create_directories(entry.destination.parent_path());
        entry.existed = std::filesystem::exists(entry.destination);
        if (entry.existed) {
            std::filesystem::copy_file(entry.destination, entry.backup);
        }
    }
    for (auto& entry : entries_) {
        if (MoveFileExW(entry.staged.c_str(), entry.destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            entry.published = true;
            continue;
        }
        auto reason = "Cannot publish staged save: " + std::to_string(GetLastError());
        for (auto& previous : entries_) {
            if (!previous.published) {
                continue;
            }
            if (previous.existed) {
                if (!CopyFileW(previous.backup.c_str(), previous.destination.c_str(), FALSE)) {
                    preserve_ = true;
                    reason += "; rollback failed for " + previous.destination.native_encoded_string();
                }
            } else if (!DeleteFileW(previous.destination.c_str())) {
                preserve_ = true;
                reason += "; rollback deletion failed for " + previous.destination.native_encoded_string();
            }
        }
        if (preserve_) {
            reason += "; recovery copies retained at " + directory_.native_encoded_string();
        }
        fail(ApplicationSessionFailure::save, entry.destination, std::move(reason));
    }
}
} //namespace simcoaster::application_session_detail
