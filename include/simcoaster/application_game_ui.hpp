#pragma once
#include "world_ui_panel.hpp"
#include <expected>

namespace simcoaster {
struct WorldRuntime;
struct WorldEvent;
enum class ApplicationFileKind {
    park,
    coaster
};

struct ApplicationFileDate {
    std::int32_t year {}, month {}, day {}, hour {}, minute {};
};

struct ApplicationFileEntry {
    //Stable within the returned listing, independently of displayed sort order.
    std::uint64_t identity {};
    std::u16string name;
    ApplicationFileDate local_modified;
};

struct ApplicationUiError {
    std::u16string title, body;
};

struct ApplicationMenuStatus {
    //gameplay_running is the current gameplay-pause transition latch, including
    //other UI pause owners; it is not inferred from WorldState alone.
    bool online_session_active {}, online_services_active {}, gameplay_running {};
    std::uint32_t pending_postcards {};
};

struct ApplicationFileSelection {
    std::u16string basename {};
    std::optional<std::uint64_t> file_identity {};
};

struct ApplicationLoadParkRequest : ApplicationFileSelection {
    bool from_profile {};
};

struct ApplicationFileWrite : ApplicationFileSelection {
    bool overwrite {};
};

struct ApplicationSaveParkRequest : ApplicationFileWrite {};

struct ApplicationCoasterNameEdit {
    std::array<std::u16string, 2> components {};
    bool refresh_sign {};
};

struct ApplicationSaveCoasterRequest : ApplicationFileWrite {
    ThingId coaster {};
    const ThingHeader* coaster_identity {};
    std::uint64_t coaster_generation {};
    std::optional<ApplicationCoasterNameEdit> naming;
};

struct ApplicationDeleteParkRequest : ApplicationFileSelection {};

struct ApplicationDeleteCoasterRequest : ApplicationFileSelection {};

struct ApplicationChangePlayerRequest {};

struct ApplicationGoOnlineRequest {};

struct ApplicationGoOfflineRequest {};

struct ApplicationChooseCoasterSourceRequest {};

struct ApplicationLeaveCoasterDesignerRequest {};

struct ApplicationQuitRequest {};

struct ApplicationCancelLoadRequest {
    bool from_profile {};
};

using ApplicationGamePayload = std::variant<ApplicationLoadParkRequest, ApplicationSaveParkRequest, ApplicationSaveCoasterRequest, ApplicationDeleteParkRequest, ApplicationDeleteCoasterRequest, ApplicationChangePlayerRequest, ApplicationGoOnlineRequest, ApplicationGoOfflineRequest, ApplicationChooseCoasterSourceRequest, ApplicationLeaveCoasterDesignerRequest, ApplicationQuitRequest, ApplicationCancelLoadRequest>;

struct ApplicationGameRequest {
    std::uint64_t serial {};
    ApplicationGamePayload payload;
};

struct ApplicationGameUiHost {
    WorldUiPanelHost panels;
    std::string language;
    std::function<ApplicationMenuStatus()> status;
    std::function<std::array<std::uint32_t, 2>()> viewport;
    //Park listings are selected-profile *.GMS basenames excluding autosave.GMS;
    //coaster listings use the native prebuilt/COS enumeration. No path search
    //or current-directory fallback is performed by the dialog owner.
    std::function<std::expected<std::vector<ApplicationFileEntry>, ApplicationUiError>(ApplicationFileKind)> list_files;
    std::function<void(bool)> pause;
    //50DC90 closes auxiliary context menus and, outside CDK mode, the coaster
    //editor before constructing the transient root. It can retain a main page.
    std::function<void(bool coaster_designer)> prepare_menu;
    std::function<void()> open_options;
    std::function<void(std::uint32_t)> play_ui_sound;
};

//Original transient command menu, full-screen Load/Save wrappers, coaster
//naming/save dialog and their overwrite/delete/postcard/quit confirmations.
//Session operations are copied requests, consumed after UI event dispatch.
//Complete a successful load before replacing its World/controller owner.
class ApplicationGameUiController {
    struct State;
    std::unique_ptr<State> state_;

public:
    ApplicationGameUiController(WorldRuntime&, UiRuntime&, UiControl* primary, const std::filesystem::path& game_root, ApplicationGameUiHost);
    ~ApplicationGameUiController();
    ApplicationGameUiController(const ApplicationGameUiController&) = delete;
    ApplicationGameUiController& operator=(const ApplicationGameUiController&) = delete;
    void open_menu(bool retain_current_panel = false);
    void request_quit_confirmation();
    void open_load(bool from_profile = false);
    void open_save(bool coaster_file_mode = false);
    void open_coaster_save();
    void close();
    bool is_open() const;
    bool menu_visible() const;
    UiControl* root() const;
    std::vector<ApplicationGameRequest> take_requests();
    void complete(std::uint64_t request_serial, std::optional<ApplicationUiError> = {});
    void report_error(ApplicationUiError);
    void show_message(std::u16string body, bool question, std::function<void()> accepted = {});
    void refresh_files();
    void handle_event(const WorldEvent&);
};
} //namespace simcoaster
