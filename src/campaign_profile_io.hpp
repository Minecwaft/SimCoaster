#pragma once
#include "simcoaster/campaign_profile.hpp"

namespace simcoaster::campaign_detail {
inline CampaignProfileError io_error(const std::filesystem::path& path, std::string reason) {
    return { path, 0, std::move(reason) };
}
} //namespace simcoaster::campaign_detail
