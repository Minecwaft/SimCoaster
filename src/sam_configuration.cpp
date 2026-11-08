#include "simcoaster/sam_configuration.hpp"
#include "simcoaster/world_sound_runtime.hpp"
#include <fstream>
#include <stdexcept>

namespace simcoaster {
namespace {
std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        if (error) {
            throw std::runtime_error(path.native_encoded_string() + ": " + error.message());
        }
        return {};
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot read SAM: " + path.native_encoded_string());
    }
    std::string result { std::istreambuf_iterator<char> { input }, {} };
    if (input.bad()) {
        throw std::runtime_error("Incomplete SAM read: " + path.native_encoded_string());
    }
    return result;
}

template <class T>
bool load(T& record, const std::filesystem::path& path, const SamTextReader& read) {
    const auto text = read(path);
    if (!text) {
        return false;
    }
    //Native file/stream wrappers return before parser/reset for zero bytes.
    if (!text->empty()) {
        const auto parsed = sam::parse_record(record, *text);
        if (!parsed) {
            throw std::runtime_error(sam::describe_error(path.native_encoded_string(), parsed.error()));
        }
    }
    return true;
}
} //namespace

OnlineEndpoints online_endpoint_snapshot(const OnlineSamBalance& balance) {
    const auto& p = balance.values;
    const auto endpoint = [](const auto& server, std::int32_t port) {
        return OnlineEndpoint { server, static_cast<std::uint16_t>(port) };
    };
    return { endpoint(p.MailServer, p.MailPort), endpoint(p.ChatServer, p.ChatPort), endpoint(p.LoginServer, p.LoginPort), endpoint(p.CityServer, p.CityPort), endpoint(p.NewsServer, p.NewsPort), endpoint(p.VoteServer, p.VotePort), p.ComplainEmail };
}

std::expected<void, sam::Error> load_online_sam_override(RootSamConfiguration& state, std::string_view text) {
    const auto parsed = sam::parse_record(state.online, text, true);
    if (!parsed) {
        return parsed; //Keep partial receiver writes; do not publish them.
    }
    state.endpoints = online_endpoint_snapshot(state.online);
    return {};
}

RootSamConfiguration load_root_sam_configuration(const std::filesystem::path& root, SamTextReader read) {
    if (!read) {
        read = read_text;
    }
    RootSamConfiguration state;
    //Storage-only files are independent; none of their values select a device,
    //profile, display size, zoom, game speed, or audio-output mode.
    state.config_loaded = load(state.config, root / "Data/Config.sam", read);
    ApplicationHardwareProfile custom;
    if (load(custom, root / "Data/custom.sam", read)) {
        state.custom = std::move(custom);
    }
    WorldSoundBalance sound;
    if (load(sound, root / "Data/sound.sam", read)) {
        state.online_loaded = load(state.online, root / "Data/Online.sam", read);
        if (state.online_loaded) {
            state.endpoints = online_endpoint_snapshot(state.online);
            state.resolution_loaded = load(state.resolution, root / "Data/Resolution.sam", read);
        }
    }
    return state;
}
} //namespace simcoaster
