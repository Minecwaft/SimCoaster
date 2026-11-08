#pragma once
#include "sam_record.hpp"
#include "ui_preferences.hpp"
#include <span>

namespace simcoaster {
//45B1D0/45AA10 process configuration, separate from each campaign's59-byte
//options. Legacy selector numbers are persisted settings, not x86 handles.
struct ApplicationConfiguration {
    std::uint32_t version = 1;
    std::uint32_t renderer_backend = 1, resolution = 1, hardware_profile = 1, video_device{};
    UiOptionVolume movie_volume{ 1, {}, 100 };
    std::int32_t sound_quality = 50;
};

struct ApplicationConfigurationError {
    std::size_t offset{};
    std::string reason;
};

std::expected<ApplicationConfiguration, ApplicationConfigurationError>
    decode_application_configuration(std::span<const std::byte>);
std::array<std::byte, 32> encode_application_configuration(const ApplicationConfiguration&);

//459D80's original low.sam/med.sam/high.sam payload. Keep file values typed;
//backend owners consume capabilities rather than reconstructing old driver
//bit fields or treating the quality slider as an invented filter preset.
struct ApplicationGraphicsProfile {
    [[=sam::Field{ "TEXTUREQUALITY", 0, 4 }]] std::int32_t texture_quality{};
    [[=sam::Field{ "TEXTUREFILTERING", 0, 4 }]] std::int32_t texture_filtering{};
    [[=sam::Field{ "PROCEDURALTEXTURING", 0, 4 }]] std::int32_t procedural_texturing{};
    [[=sam::Field{ "PROCEDURALTEXTURECACHE", 0, 2 }]] std::int32_t procedural_texture_cache{};
    [[=sam::Field{ "ANIMATINGTEXTURES", 0, 2 }]] std::int32_t animating_textures{};
    [[=sam::Field{ "SKYQUALITY", 0, 5 }]] std::int32_t sky_quality{};
    [[=sam::Field{ "SKYDETAIL", 0, 2 }]] std::int32_t sky_detail{};
    [[=sam::Field{ "SKYSHADOW", 0, 2 }]] std::int32_t sky_shadow{};
    [[=sam::Field{ "MESHSHADOW", 0, 2 }]] std::int32_t mesh_shadow{};
    [[=sam::Field{ "SPRITESHADOW", 0, 4 }]] std::int32_t sprite_shadow{};
    [[=sam::Field{ "TRIPLEBUFFER", 0, 2 }]] std::int32_t triple_buffer{};
    [[=sam::Field{ "FOG", 0, 2 }]] std::int32_t fog{};
    [[=sam::Field{ "MIPMAP", 0, 2 }]] std::int32_t mipmap{};
    [[=sam::Field{ "RENDER32", 0, 2 }]] std::int32_t render32{};
    [[=sam::Field{ "TEXTURE32", 0, 2 }]] std::int32_t texture32{};
    [[=sam::Field{ "BUMPMAPPING", 0, 2 }]] std::int32_t bumpmapping{};
    [[=sam::Field{ "FIRSTPERSONVIEWDISTANCE", 0, 4 }]] std::int32_t first_person_view_distance{};
    [[=sam::Field{ "LIGHTING", 0, 2 }]] std::int32_t lighting{};
    [[=sam::Field{ "FORCELOWALPHAREF", 0, 2 }]] std::int32_t force_low_alpha_ref{};
};

struct ApplicationGameGraphicsProfile {
    [[=sam::Field{ "COASTERSMOOTHNESS", 1, 9 }]] std::int32_t coaster_smoothness = 1;
    [[=sam::Field{ "COASTERTRACKDETAIL", 0, 2 }]] std::int32_t coaster_track_detail{};
    [[=sam::Field{ "PARTICLEDENSITY", 500, 2001 }]] std::int32_t particle_density = 500;
    [[=sam::Field{ "TOTALPARTICLES", 1000, 3201 }]] std::int32_t total_particles = 1000;
    [[=sam::Field{ "NUMKIDS", 0, 5 }]] std::int32_t num_kids{};
    [[=sam::Field{ "WEATHER", 0, 2 }]] std::int32_t weather{};
    [[=sam::Field{ "WATER", 0, 2 }]] std::int32_t water{};
    [[=sam::Field{ "LOBBYOBJECTS", 0, 101 }]] std::int32_t lobby_objects{};
};

struct ApplicationHardwareProfile {
    std::uint32_t profile_index = 1;
    [[=sam::Group{ "GraphicalOptions" }]] ApplicationGraphicsProfile graphics;
    [[=sam::Group{ "GameOptions" }]] ApplicationGameGraphicsProfile game;
};

std::expected<ApplicationHardwareProfile, ApplicationConfigurationError>
load_application_hardware_profile(const std::filesystem::path& game_root, std::uint32_t profile_index);
//Native Options resolution selectors (55E890), distinct from Resolution.sam's
//optional process-wide override table.
std::array<std::uint32_t, 2> application_resolution(std::uint32_t selector);
//45A040 -> 6516D0: enabled percentage conversion followed by ten-bit masking.
//There is no MovieVolume slider in the original Options template.
std::uint16_t application_movie_volume(const UiOptionVolume&);
} //namespace simcoaster
