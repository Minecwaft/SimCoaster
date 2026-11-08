#include "application_session_internal.hpp"
#include <algorithm>

namespace simcoaster::application_session_detail {
[[noreturn]] void fail(ApplicationSessionFailure failure, const std::filesystem::path& path, std::string detail) {
    throw ApplicationSessionError{ failure, path, std::move(detail) };
}

ApplicationSessionError profile_error(const CampaignProfileError& error) {
    return { ApplicationSessionFailure::profile, error.path, error.reason + " at " + std::to_string(error.offset) };
}

bool valid_basename(std::u16string_view name) {
    return !name.empty()
        && name.size() <= 32
        && name != u"."
        && name != u".."
        && name.find_first_of(u"\\/*?:|<>\"\0", 0, 10) == name.npos
        && name.back() != u'.'
        && name.back() != u' ';
}

bool extension_is(const std::filesystem::path& path, std::u16string_view suffix) {
    const auto extension = path.extension().u16string();
    const auto fold = [](char16_t unit) {
        return unit >= u'A' && unit <= u'Z' ? static_cast<char16_t>(unit + (u'a' - u'A')) : unit;
    };
    return std::ranges::equal(extension, suffix, [&](char16_t a, char16_t b) {
        return fold(a) == fold(b);
    });
}
} //namespace simcoaster::application_session_detail
