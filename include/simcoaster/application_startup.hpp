#pragma once
#include "application_movie.hpp"

namespace simcoaster {
enum class ApplicationStartupStage {
    clear,
    splash,
    legal,
    loading
};

//6F89B0/6F9A64 startup presentation. The application pumps it during actual
//resource loading; progress is resource-pump count, not invented elapsed work.
class ApplicationStartupPresentation {
    struct State;
    std::unique_ptr<State> state_;

public:
    ApplicationStartupPresentation(const std::filesystem::path& game_root, std::string language, std::uint32_t viewport_width, std::uint32_t viewport_height);
    ~ApplicationStartupPresentation();
    ApplicationStartupPresentation(const ApplicationStartupPresentation&) = delete;
    ApplicationStartupPresentation& operator=(const ApplicationStartupPresentation&) = delete;
    //A request made during the legal minimum hold is pending until advance
    //reaches3000ms. No busy wait or message-pump starvation crosses this API.
    void request(ApplicationStartupStage, std::uint32_t progress_duration = 500);
    void advance(std::chrono::milliseconds);
    void append(UiDrawList&) const;
    ApplicationStartupStage stage() const;
    bool transition_pending() const;
    TextureImagePtr image() const;
    std::uint64_t pump_count() const;
};

//Native6A1780/6A1930: American logo selection and day-of-month modulo8.
//Missing movie paths are nullopt, matching the native resolver's skip path.
std::array<std::optional<std::filesystem::path>, 2> application_startup_movie_paths(const std::filesystem::path& game_root, std::string_view language, unsigned day_of_month);
//Native6A1400 gates states4..8 on dialog graphics bit512. MovieVolume
//controls attenuation and is independent of this startup selection.
bool application_startup_movies_enabled(const std::filesystem::path& game_root);
} //namespace simcoaster
