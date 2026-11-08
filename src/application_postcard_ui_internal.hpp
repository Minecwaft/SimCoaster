#pragma once
#include "simcoaster/ui_modal_scope.hpp"
#include "ui_panel_presentation.hpp"
#include "application_postcard_image.hpp"
#include "simcoaster/application_postcard_ui.hpp"
#include "simcoaster/ui_font.hpp"
#include "simcoaster/world.hpp"
#include <map>

namespace simcoaster {
struct ApplicationPostcardUiController::State : UiPanelPresentation {
    static constexpr UiPanelPresentationPolicy panel_presentation { .missing_control = "Original postcard template missing control ", .text_error = "Postcard localized text: ", .text_error_row = false, .white_caption = true };
    enum class Page {
        none,
        composer,
        outbox
    };
    WorldRuntime& c;
    UiModalScope message_scope { c };
    UiFocusBookmark modal_focus;
    UiMessageControls message_controls;
    UiRuntime& ui;
    mutable UiPanelBindings bindings { ui };
    UiControl *parent {}, *root {}, *wrapper {}, *book {}, *entry {};
    UiControl *message_wrapper {}, *message_root {};
    std::uint64_t profile_revision {}, next_send = 1, send_serial {}, send_profile_revision {};
    std::filesystem::path game_root;
    ApplicationPostcards& store;
    ApplicationPostcardUiHost host;
    UiFontLibrary fonts;
    std::unique_ptr<PostcardImageComposer> preview, hires;
    TextureImagePtr preview_image, outbox_image, pending_image;
    std::optional<PostcardRecord> pending_postcard;
    std::filesystem::path temporary_directory;
    std::vector<std::filesystem::path> temporary_images;
    bool sending_from_outbox {};
    //Present through capture/restoration/UI construction, including ready0.
    std::optional<std::int32_t> composition_polls;
    bool composition_hidden {};
    Page page {};
    bool closing {}, paused {}, commands_disabled {};
    bool book_shifted {}, subject_edited {}, body_edited {}, recipients_edited {}, dragging {};
    UiPoint drag_previous {}, drag_offset {};
    std::int32_t preview_deadline {}, entry_index = -1;
    std::vector<PostcardRecord> outbox_records, pending_send;
    std::map<std::uint32_t, PostcardRowAction> actions;
    std::function<void()> message_cancel, message_accept;
    std::uint32_t pixel_width = 1100, pixel_height = 760;
    bool outbox_scroll_dirty {};
    std::optional<std::array<std::uint32_t, 2>> outbox_resolution;
    State(WorldRuntime&, UiRuntime&, UiControl*, const std::filesystem::path&, ApplicationPostcards&, ApplicationPostcardUiHost);
    ~State();
    UiControl& control(std::uint32_t) const;

    template <class Callback>
    UiControl& parse(UiControl* owner, std::span<const unsigned char> bytes, std::uint32_t id, Callback callback) {
        return parse_control(owner, bytes, id, std::move(callback), "Cannot parse original postcard template");
    }

    void title(UiControl&, std::uint32_t, std::uint32_t);
    void edit(UiControl&, std::size_t, std::uint32_t, std::int32_t = 0);
    void list(UiControl&, std::span<const std::uint32_t>);
    void pause();
    void release_pause();
    void begin(Page);
    void close();
    void close_panel();
    void close_message();
    void message(std::u16string, std::u16string, std::function<void()> cancel = {}, std::function<void()> accepted = {});
    void resource_message(std::uint32_t, std::function<void()> accepted = {}, std::function<void()> cancel = {});
    void error(const PostcardError&);
    void open_composer();
    void begin_composer();
    void poll_composition();
    void release_composition_transition();
    void refresh_preview(bool = true);
    void submit();
    void store_offline();
    void start_send();
    void complete_send(std::uint64_t, bool, std::int32_t);
    void open_book();
    void close_book();
    void refresh_book(std::optional<std::size_t> = {});
    void add_recipient();
    void remove_recipient();
    void apply_recipients();
    void open_entry(std::int32_t);
    void close_entry();
    void commit_entry();
    void open_outbox();
    void refresh_outbox();
    void outbox_preview();
    void outbox_scroll();
    void apply_outbox();
    std::intptr_t composer_event(UiControl&, const UiEvent&);
    std::intptr_t book_event(UiControl&, const UiEvent&);
    std::intptr_t entry_event(UiControl&, const UiEvent&);
    std::intptr_t outbox_event(UiControl&, const UiEvent&);
    void advance();
    void append(UiDrawList&, const UiControl&, UiRect, std::uint32_t, std::uint32_t);
};

bool postcard_escape(const UiEvent&);
} //namespace simcoaster
