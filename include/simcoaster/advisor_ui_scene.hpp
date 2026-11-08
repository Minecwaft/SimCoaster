#pragma once
#include "ui_draw.hpp"
#include <filesystem>

namespace simcoaster {
struct WorldRuntime;

//Presentation-only consumer of the two independently owned Advisor meshes.
//Their animation clocks and registration remain with AdvisorPresentationState.
class AdvisorUiScene {
    struct State;
    std::unique_ptr<State> state_;

public:
    AdvisorUiScene(WorldRuntime&, const std::filesystem::path& game_root);
    ~AdvisorUiScene();
    AdvisorUiScene(const AdvisorUiScene&) = delete;
    AdvisorUiScene& operator=(const AdvisorUiScene&) = delete;
    bool registered() const;
    //The native screen pass retains the current ambient baseline, replaces
    //World directional lights with the two Advisor lights. The compositor
    //isolates its depth from controls while retaining mesh self-occlusion.
    void append(UiDrawList&, const MeshLighting& baseline);
    const std::vector<std::string>& diagnostics() const;
};
} //namespace simcoaster
