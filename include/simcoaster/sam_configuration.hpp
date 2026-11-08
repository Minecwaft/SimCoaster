#pragma once
#include "application_configuration.hpp"
#include <functional>

namespace simcoaster {
struct ConfigSamDefaults {
    [[=sam::Field { "DefaultConfig", sam::Kind::nonnegative_integer }]] std::int32_t DefaultConfig {};
    [[=sam::Field { "CPUTolerance", sam::Kind::nonnegative_integer }]] std::int32_t CPUTolerance {};
};

struct ConfigSamProfile {
    [[=sam::Field { "MinCPU", sam::Kind::integer }]] std::int32_t MinCPU {};
    [[=sam::Field { "MaxCPU", sam::Kind::nonnegative_integer }]] std::int32_t MaxCPU {};
    [[=sam::Field { "MinVRAM", sam::Kind::nonnegative_integer }]] std::int32_t MinVRAM {};
    [[=sam::Field { "MaxVRAM", sam::Kind::nonnegative_integer }]] std::int32_t MaxVRAM {};
    [[=sam::Field { "MinMem", sam::Kind::nonnegative_integer }]] std::int32_t MinMem {};
    [[=sam::Field { "MaxMem", sam::Kind::nonnegative_integer }]] std::int32_t MaxMem {};
    [[=sam::Field { "DefaultSettings", sam::Kind::nonnegative_integer }]] std::int32_t DefaultSettings {};
    [[=sam::Field { "DefaultResolution", sam::Kind::nonnegative_integer }]] std::int32_t DefaultResolution {};
    [[=sam::Field { "MinZoom", sam::Kind::scalar }]] float MinZoom {};
    [[=sam::Field { "MaxZoom", sam::Kind::scalar }]] float MaxZoom {};
    [[=sam::Field { "DefaultGameSpeed", sam::Kind::integer }]] std::int32_t DefaultGameSpeed {};
    [[=sam::Field { "FPSClip", sam::Kind::scalar }]] float FPSClip {};
    [[=sam::Field { "PlayMusic", sam::Kind::integer }]] std::int32_t PlayMusic {};
    [[=sam::Field { "AllowSkirts", sam::Kind::integer }]] std::int32_t AllowSkirts {};
};

struct ConfigSamBalance {
    [[=sam::Group { "Defaults" }]] ConfigSamDefaults defaults;
    [[=sam::Rows { "Config" }]] std::array<ConfigSamProfile, 20> profiles {};
    [[=sam::Extent { "Config" }]] std::uint32_t extent {};
};

struct OnlineSamFields {
    [[=sam::Field { "MailPort", sam::Kind::integer }]] std::int32_t MailPort {};
    [[=sam::Field { "ChatPort", sam::Kind::integer }]] std::int32_t ChatPort {};
    [[=sam::Field { "LoginPort", sam::Kind::integer }]] std::int32_t LoginPort {};
    [[=sam::Field { "CityPort", sam::Kind::integer }]] std::int32_t CityPort {};
    [[=sam::Field { "NewsPort", sam::Kind::integer }]] std::int32_t NewsPort {};
    [[=sam::Field { "VotePort", sam::Kind::integer }]] std::int32_t VotePort {};
    [[=sam::Field { "MailServer", sam::Kind::string }]] std::optional<std::string> MailServer {};
    [[=sam::Field { "ChatServer", sam::Kind::string }]] std::optional<std::string> ChatServer {};
    [[=sam::Field { "LoginServer", sam::Kind::string }]] std::optional<std::string> LoginServer {};
    [[=sam::Field { "CityServer", sam::Kind::string }]] std::optional<std::string> CityServer {};
    [[=sam::Field { "NewsServer", sam::Kind::string }]] std::optional<std::string> NewsServer {};
    [[=sam::Field { "VoteServer", sam::Kind::string }]] std::optional<std::string> VoteServer {};
    [[=sam::Field { "ComplainEmail", sam::Kind::string }]] std::optional<std::string> ComplainEmail {};
};

struct OnlineSamBalance {
    [[=sam::Group { "OnlineInfo" }]] OnlineSamFields values;
};

struct ResolutionSamFields {
    [[=sam::Field { "RESOLUTION", 0, 8 }]] std::int32_t RESOLUTION { 0 };
};

struct ResolutionSamBalance {
    [[=sam::Group { "Res" }]] ResolutionSamFields values;
};

struct OnlineEndpoint {
    std::optional<std::string> server;
    std::uint16_t port {};
};

struct OnlineEndpoints {
    OnlineEndpoint mail, chat, login, city, news, vote;
    std::optional<std::string> complaint_email;
};

struct RootSamConfiguration {
    bool config_loaded {}, online_loaded {}, resolution_loaded {};
    ConfigSamBalance config;
    OnlineSamBalance online;
    ResolutionSamBalance resolution;
    std::optional<ApplicationHardwareProfile> custom;
    std::optional<OnlineEndpoints> endpoints;
};

using SamTextReader = std::function<std::optional<std::string>(const std::filesystem::path&)>;
//No network calls, hardware selection, display override or custom-profile activation.
RootSamConfiguration load_root_sam_configuration(const std::filesystem::path&, SamTextReader = {});
std::expected<void, sam::Error> load_online_sam_override(RootSamConfiguration&, std::string_view);
OnlineEndpoints online_endpoint_snapshot(const OnlineSamBalance&);
} //namespace simcoaster
