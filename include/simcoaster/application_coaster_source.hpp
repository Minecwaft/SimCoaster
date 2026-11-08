#pragma once
#include "coaster_design.hpp"
#include "world_ui_panel.hpp"

namespace simcoaster {
struct WorldRuntime;
class ParkConstructionRuntime;

//Owned values copied before a source transition. Plain registrations use the
//catalog Shape; a COS selection retains its actual complete grid, crop origins
//and scanner-derived names in design. No generated editor grid is substituted.
struct ApplicationCoasterSourceSelection {
    std::int32_t source_index = -1;
    std::uint16_t definition_id{};
    bool is_plain = true;
    std::string native_design_path;
    std::optional<CoasterDesign> design;
    //Native map-source recreation flag==1 saves the ordinary profile before
    //selecting special profile mode4; the designer must not overwrite its GMS.
    bool detach_profile{};
    std::uint8_t remember_current_source{};
    std::int32_t previous_source_index = -1;
};

//Call after catalog binding and BEFORE fresh Mode3 source initialization.
//Validation has no mutations, cancellation, stream writes or World creation.
void validate_coaster_source_selection(const WorldRuntime&, const ApplicationCoasterSourceSelection&);
void install_coaster_source_selection(WorldRuntime&, const ApplicationCoasterSourceSelection&);
//Native5305B0 after World initialization: select the real definition/COS in
//the construction owner. The source's genuine CEM boundary is initialized by
//ParkRuntime; this function never invents land, cells or supports.
void begin_coaster_source_world_interaction(WorldRuntime&,
                                            ParkConstructionRuntime&,
                                            const ApplicationCoasterSourceSelection&);

struct ApplicationCoasterSourceHost {
    WorldUiPanelHost panels;
    std::function<std::int32_t()> selected_source;
    //Queued transition; never destroy this UI owner from its own callback.
    std::function<void(ApplicationCoasterSourceSelection)> request_source;
    std::function<bool(const ApplicationCoasterSourceSelection&)> begin_same_source;
    std::function<void(bool)> pause;
    std::function<bool()> gameplay_running;
    std::function<void()> resume_ui_transition;
    std::function<std::uint8_t()> map_source_recreation_flag;
    //Cancel from the Intro/Profile route retains native special profile mode4
    //semantics. Its persistent save/configuration owner implements this action.
    std::function<void()> cancel_profile_route;
};

class ApplicationCoasterSourceController {
    struct State;
    std::unique_ptr<State> state_;

public:
    ApplicationCoasterSourceController(WorldRuntime&,
                                       UiRuntime&,
                                       UiControl* primary,
                                       const std::filesystem::path& game_root,
                                       ApplicationCoasterSourceHost,
                                       std::string language = "American");
    ~ApplicationCoasterSourceController();
    ApplicationCoasterSourceController(const ApplicationCoasterSourceController&) = delete;
    ApplicationCoasterSourceController& operator=(const ApplicationCoasterSourceController&) = delete;
    void open(std::uint8_t allow_same_source_placement = 0, std::uint8_t remember_current_source = 0);
    void refresh();
    void restore_return_policy(std::uint8_t remember_current_source, std::int32_t previous_source_index);
    void close();
    bool is_open() const;
    UiControl* root() const;
    const std::vector<std::string>& diagnostics() const;
};
} //namespace simcoaster
