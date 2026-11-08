#pragma once
#include "application_postcards.hpp"
#include "ui_draw.hpp"
#include "world_ui_panel.hpp"

namespace simcoaster {
struct WorldRuntime;

struct PostcardOnlineStatus {
    bool session_active {}, services_active {}, permission = true;
    std::string from = "nobody@nowhere.net";
};

struct ApplicationPostcardUiHost {
    WorldUiPanelHost panels;
    std::string language = "American";
    std::function<std::array<std::uint32_t, 2>()> viewport;
    std::function<std::expected<TextureImage, PostcardError>()> capture_scene;
    //Own temporary UI/cursor hiding; false releases only this transition.
    std::function<void(bool pending)> composition_transition;
    std::function<void()> restore_after_capture;
    std::function<PostcardOnlineStatus()> online_status;
    std::function<void()> go_online;
    //Explicit send request borrowed until completion/cancellation. A transport
    //crossing threads must copy these records. It owns connection/progress and
    //calls complete_send; no failed/unavailable service reports delivery.
    std::function<void(std::uint64_t, std::span<const PostcardRecord>)> send;
    std::function<void(std::uint64_t)> cancel_send;
    std::function<void(bool)> pause;
    std::function<void(const PostcardError&)> report_error;
    std::function<void(std::uint32_t error_message_row)> status_message;
};

class ApplicationPostcardUiController {
    struct State;
    std::unique_ptr<State> state_;

public:
    ApplicationPostcardUiController(WorldRuntime&, UiRuntime&, UiControl* parent, const std::filesystem::path& game_root, ApplicationPostcards&, ApplicationPostcardUiHost);
    ~ApplicationPostcardUiController();
    void open_composer();
    void open_outbox();
    void open_address_book();
    void close();
    bool is_open() const;
    UiControl* root() const;
    void advance();
    //Native50AA30 boundary, before primary queued control events. The third
    //poll after entry captures the old camera view, then opens the composer.
    void poll_composition();
    //Call before this control's ordinary attachments, after its ancestors.
    //This preserves wrapper dimming and the original surface-attachment order.
    void append(UiDrawList&, const UiControl&, UiRect clip, std::uint32_t pixel_width, std::uint32_t pixel_height);
    void complete_send(std::uint64_t serial, bool success, std::int32_t detail);
};
} //namespace simcoaster
