#include "application_session_internal.hpp"
#include <algorithm>

namespace simcoaster {
using namespace application_session_detail;

std::vector<ApplicationFileEntry> ApplicationSession::State::list(ApplicationFileKind kind) {
    const auto directory = kind == ApplicationFileKind::park ? profile_directory() : root / "Data/my coasters";
    const auto slot = profiles->selected_slot();
    const auto generation = slot ? profiles->views().at(*slot).generation : 0;
    std::vector<ApplicationFileEntry> result;
    if (!std::filesystem::exists(directory)) {
        return result;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto extension = entry.path().extension().u16string();
        for (auto& unit : extension) {
            if (unit >= u'A' && unit <= u'Z') {
                unit = static_cast<char16_t>(unit + (u'a' - u'A'));
            }
        }
        if (extension != (kind == ApplicationFileKind::park ? u".gms" : u".cos")) {
            continue;
        }
        const auto name = entry.path().stem().u16string();
        auto folded = name;
        for (auto& unit : folded) {
            if (unit >= u'A' && unit <= u'Z') {
                unit = static_cast<char16_t>(unit + (u'a' - u'A'));
            }
        }
        if (kind == ApplicationFileKind::park && folded == u"autosave") {
            continue;
        }
        FileIdentity identity { entry.path(), kind, slot, generation, file_stamp(entry.path()) };
        auto existing = std::ranges::find_if(files, [&](const auto& item) {
            const auto& previous = item.second;
            return previous.kind == kind && previous.profile == slot && previous.profile_generation == generation && equal_path(previous.path, identity.path) && previous.stamp == identity.stamp;
        });
        const auto id = existing == files.end() ? ++next_file : existing->first;
        files.insert_or_assign(id, std::move(identity));
        result.push_back({ id, name, file_date(entry.path()) });
    }
    std::erase_if(files, [&](const auto& entry) {
        return entry.second.kind == kind && std::ranges::find(result, entry.first, &ApplicationFileEntry::identity) == result.end();
    });
    return result;
}

void ApplicationSession::State::validate_file(const ApplicationFileSelection& request, ApplicationFileKind kind) const {
    if (!valid_basename(request.basename)) {
        fail(ApplicationSessionFailure::state, {}, "Invalid original save basename");
    }
    if (!request.file_identity) {
        return;
    }
    const auto found = files.find(*request.file_identity);
    if (found == files.end() || found->second.kind != kind || found->second.path.stem().u16string() != request.basename || found->second.profile != profiles->selected_slot() || (found->second.profile && profiles->views().at(*found->second.profile).generation != found->second.profile_generation) || found->second.stamp != file_stamp(found->second.path)) {
        fail(ApplicationSessionFailure::stale_file, {}, "Save-file confirmation identity expired");
    }
}
} //namespace simcoaster
