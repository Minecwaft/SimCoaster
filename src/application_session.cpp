#include "application_session_internal.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace simcoaster {
using namespace application_session_detail;

ApplicationSession::State::State(const std::filesystem::path& directory,
                                 std::span<const std::string_view> registered,
                                 UiPreferences options,
                                 ApplicationSessionHost callbacks)
    : root(std::filesystem::absolute(directory).lexically_normal()),
      defaults(options),
      host(std::move(callbacks)) {
    for (const auto source : registered)
        sources.emplace_back(source);
    if (sources.size() < 4
        || sources[0] != "arabian"
        || sources[1] != "science"
        || sources[2] != "water"
        || sources[3] != "wIntro")
        throw std::invalid_argument("Application source ordinals do not match native registration");
    if (!host.release_world_views
        || !host.bind_world
        || !host.complete_request
        || !host.profile_services
        || !host.save_configuration
        || !host.external_request)
        throw std::invalid_argument("Application session lifecycle hooks are required");
    profiles = std::make_unique<ApplicationProfiles>(root, registered, defaults, [this] {
        if (!park)
            throw std::logic_error("Creating a player requires the original Intro service host");
        return create_fresh_campaign_profile(park->context(), defaults);
    });
}

CampaignProfile ApplicationSession::State::capture() const {
    if (!park)
        fail(ApplicationSessionFailure::state, {}, "No World to capture");
    auto result = park->capture_campaign(campaign ? campaign->auxiliary : CampaignAuxiliaryState{});
    result.selected_source = selected;
    if (const auto* manager = park->context().missions.theme_global_info_manager) {
        result.theme->save_file_basename = manager->save_file_basename;
        result.theme->use_gmi_suffix = manager->use_gmi_suffix;
    }
    return result;
}

void ApplicationSession::State::saved_metadata(const CampaignProfile& saved) {
    if (park && park->context().missions.theme_global_info_manager) {
        auto& manager = *park->context().missions.theme_global_info_manager;
        manager.save_file_basename = saved.theme->save_file_basename;
        manager.use_gmi_suffix = saved.theme->use_gmi_suffix;
    }
}

std::filesystem::path ApplicationSession::State::profile_directory() const {
    const auto slot = profiles->selected_slot();
    if (!slot)
        fail(ApplicationSessionFailure::profile, {}, "No ordinary player profile is selected");
    return profiles->profile_directory(*slot);
}

std::filesystem::path
ApplicationSession::State::source_file(std::int32_t source, std::u16string_view basename, bool gmi) const {
    if (source < 0 || static_cast<std::size_t>(source) >= sources.size() || !valid_basename(basename))
        fail(ApplicationSessionFailure::source, {}, "Invalid native source/save identity");
    return profile_directory()
         / sources[static_cast<std::size_t>(source)]
         / std::filesystem::path(std::u16string(basename) + (gmi ? u".TPWI" : u".TPWS"));
}

void ApplicationSession::State::complete(std::uint64_t serial, std::optional<ApplicationSessionError> error) {
    host.complete_request(serial, std::move(error));
}

void ApplicationSession::State::replace(std::unique_ptr<ParkRuntime> next,
                                        CampaignProfile next_campaign,
                                        std::int32_t next_source,
                                        ApplicationSessionPage next_page,
                                        std::uint64_t serial,
                                        ApplicationWorldEntry entry,
                                        bool post_load_chooser,
                                        std::function<void()> publish,
                                        std::optional<ApplicationCoasterSourceSelection> next_coaster,
                                        std::optional<ProfileBinding> next_profile) {
    const auto old_slot = profiles->selected_slot();
    const auto old_generation = old_slot ? profiles->views().at(*old_slot).generation : 0;
    const auto old_profile_path = old_slot ? profiles->profile_directory(*old_slot) : std::filesystem::path{};
    const auto old_selected = selected, old_active = active;
    const auto old_page = page;
    const auto old_entry = world_entry;
    const auto old_chooser = post_load_chooser_pending;
    const auto old_epoch = epoch, old_next_file = next_file;
    complete(serial);
    transition_completed = true;
    //Close the pending dialog while it still belongs to the outgoing World.
    //Its World remains owned until new views AND staged persistence succeed.
    host.release_world_views(park.get());
    auto previous = std::move(park);
    auto previous_campaign = std::move(campaign);
    auto previous_coaster = std::move(coaster_selection);
    auto previous_files = std::move(files);
    park = std::move(next);
    campaign = std::move(next_campaign);
    coaster_selection = std::move(next_coaster);
    selected = active = next_source;
    page = next_page;
    world_entry = entry;
    post_load_chooser_pending = post_load_chooser;
    files.clear();
    ++epoch;
    bool services_changed = false;
    try {
        host.bind_world(park.get(), page);
        if (next_profile) {
            services_changed = true;
            const auto directory =
                next_profile->slot ? profiles->profile_directory(*next_profile->slot) : std::filesystem::path{};
            if (const auto bound = host.profile_services(directory, true); !bound)
                throw bound.error();
            if (next_profile->slot) {
                if (const auto committed = profiles->commit_activation(*next_profile->slot); !committed)
                    throw profile_error(committed.error());
            } else
                profiles->clear_selection();
        }
        if (publish)
            publish();
    } catch (...) {
        auto failure = []() -> ApplicationSessionError {
            try {
                throw;
            } catch (const ApplicationSessionError& error) {
                return error;
            } catch (const std::exception& error) {
                return { ApplicationSessionFailure::load, {}, error.what() };
            } catch (...) {
                return { ApplicationSessionFailure::load, {}, "Unknown application binding failure" };
            }
        }();
        binding_rollback = true;
        //Partial controllers must die before their candidate World. They may
        //still own pause, audio and resource callbacks into that exact World.
        host.release_world_views(park.get());
        auto rejected = std::move(park);
        park = std::move(previous);
        campaign = std::move(previous_campaign);
        coaster_selection = std::move(previous_coaster);
        selected = old_selected;
        active = old_active;
        page = old_page;
        world_entry = old_entry;
        post_load_chooser_pending = old_chooser;
        files = std::move(previous_files);
        epoch = old_epoch;
        next_file = old_next_file;
        std::erase_if(requests, [old_epoch](const auto& request) {
            return request.epoch != old_epoch;
        });
        if (const auto restored = profiles->restore_selection(old_slot, old_generation); !restored) {
            failure.detail += "; profile recovery: " + restored.error().reason;
            diagnostics.push_back(profile_error(restored.error()));
        }
        if (services_changed) {
            if (const auto restored = host.profile_services(old_profile_path, true); !restored) {
                failure.detail += "; mailbox recovery: " + restored.error().detail;
                diagnostics.push_back(restored.error());
            }
        }
        rejected.reset();
        //The saved state already contains the ongoing animation/camera/programs.
        //Rebinding must not run State10, replay Intro or consume another tip.
        try {
            host.bind_world(park.get(), page);
        } catch (const std::exception& error) {
            failure.detail += "; outgoing view recovery: " + std::string(error.what());
        } catch (...) {
            failure.detail += "; outgoing view recovery failed";
        }
        binding_rollback = false;
        throw failure;
    }
    previous.reset();
}

bool ApplicationSession::State::can_autosave() const {
    return park
        && profiles->selected_slot()
        && active != 3
        && park->context().mode.game != GameMode::code_3
        && park->context().mode.game != GameMode::online
        && park->context().simulation.current
        && park->context().simulation.current->saved.state != WorldState::transition;
}

void ApplicationSession::State::persist_profile() {
    if (!profiles->selected_slot() || !park)
        return;
    if (const auto result = host.profile_services(profile_directory(), false); !result)
        throw result.error();
    if (park->context().mode.game != GameMode::code_3) {
        auto saved = capture();
        if (const auto result = profiles->save_selected(saved); !result)
            throw profile_error(result.error());
        saved_metadata(saved);
        campaign = std::move(saved);
    }
    if (const auto result = host.save_configuration(); !result)
        throw result.error();
}

void ApplicationSession::State::quit(std::uint64_t serial) {
    if (can_autosave()) {
        FileTransaction transaction(root);
        auto saved = capture();
        stage_save(transaction, saved, u"autosave");
        transaction.commit();
        saved_metadata(saved);
        campaign = std::move(saved);
    }
    persist_profile();
    complete(serial);
    host.release_world_views(park.get());
    park.reset();
    campaign.reset();
    coaster_selection.reset();
    profiles->clear_selection();
    files.clear();
    active = -1;
    page = ApplicationSessionPage::quit;
    ++epoch;
    host.bind_world(nullptr, page);
}

ApplicationSession::ApplicationSession(const std::filesystem::path& root,
                                       std::span<const std::string_view> sources,
                                       UiPreferences defaults,
                                       ApplicationSessionHost host)
    : state_(std::make_unique<State>(root, sources, defaults, std::move(host))) {}

ApplicationSession::~ApplicationSession() {
    if (state_->park) {
        state_->host.release_world_views(state_->park.get());
        state_->park.reset();
    }
}

ParkRuntime* ApplicationSession::park() {
    return state_->park.get();
}

const ParkRuntime* ApplicationSession::park() const {
    return state_->park.get();
}

ApplicationProfiles& ApplicationSession::profiles() {
    return *state_->profiles;
}

const CampaignProfile* ApplicationSession::campaign() const {
    return state_->campaign ? &*state_->campaign : nullptr;
}

CampaignTipHistory& ApplicationSession::tip_history() {
    if (!state_->campaign)
        throw std::logic_error("Tip history requires an active campaign");
    return state_->campaign->auxiliary.tips;
}

bool ApplicationSession::take_post_load_chooser_pending() {
    return std::exchange(state_->post_load_chooser_pending, false);
}

ApplicationWorldEntry ApplicationSession::world_entry() const {
    return state_->world_entry;
}

bool ApplicationSession::binding_rollback() const {
    return state_->binding_rollback;
}

const ApplicationCoasterSourceSelection* ApplicationSession::current_coaster_source() const {
    return state_->coaster_selection ? &*state_->coaster_selection : nullptr;
}

ApplicationSessionPage ApplicationSession::page() const {
    return state_->page;
}

std::int32_t ApplicationSession::selected_source() const {
    return state_->selected;
}

std::int32_t ApplicationSession::active_source() const {
    return state_->active;
}

void ApplicationSession::select_source(std::int32_t source) {
    if (source < 0 || static_cast<std::size_t>(source) >= state_->sources.size())
        throw std::out_of_range("Native application resource source");
    state_->selected = source;
    if (state_->campaign)
        state_->campaign->selected_source = source;
}

std::expected<void, ApplicationSessionError> ApplicationSession::boot_intro() {
    auto& s = *state_;
    try {
        if (s.park)
            fail(ApplicationSessionFailure::state, {}, "Application Intro is already initialized");
        auto next = std::make_unique<ParkRuntime>(
            s.root,
            ParkRuntimeOpen{ "wIntro",
                             {},
                             nullptr,
                             false,
                             nullptr,
                             s.host.resource_pump,
                             s.host.hardware_profile ? std::optional{ s.host.hardware_profile() } : std::nullopt });
        next->apply_preferences(s.defaults);
        auto initial = next->capture_campaign({});
        initial.selected_source = 3;
        s.park = std::move(next); //Profile enumeration's native reset factory needs its services.
        if (const auto result = s.profiles->refresh(); !result) {
            s.park.reset();
            throw profile_error(result.error());
        }
        s.campaign = std::move(initial);
        s.active = s.selected = 3;
        s.page = ApplicationSessionPage::intro;
        ++s.epoch;
        s.host.bind_world(s.park.get(), s.page);
        return {};
    } catch (const ApplicationSessionError& error) {
        return std::unexpected(error);
    } catch (const std::exception& error) {
        return std::unexpected(ApplicationSessionError{ ApplicationSessionFailure::load, s.root, error.what() });
    }
}

std::expected<void, ApplicationSessionError> ApplicationSession::activate_profile(std::uint32_t slot) {
    auto& s = *state_;
    try {
        if (!s.park)
            fail(ApplicationSessionFailure::state, {}, "Profile activation requires Intro World services");
        s.persist_profile();
        auto staged = s.profiles->load_for_activation(slot);
        if (!staged)
            throw profile_error(staged.error());
        auto previous = s.capture();
        const auto old_slot = s.profiles->selected_slot();
        try {
            s.park->apply_campaign_for_lobby(*staged);
            if (const auto services = s.host.profile_services(s.profiles->profile_directory(slot), true); !services)
                throw services.error();
            if (const auto committed = s.profiles->commit_activation(slot); !committed)
                throw profile_error(committed.error());
        } catch (...) {
            s.park->apply_campaign_for_lobby(previous);
            if (const auto restored = s.host.profile_services(old_slot ? s.profiles->profile_directory(*old_slot)
                                                                       : std::filesystem::path{},
                                                              true);
                !restored)
                s.diagnostics.push_back(restored.error());
            throw;
        }
        s.selected = staged->selected_source;
        s.campaign = std::move(*staged);
        s.saved_metadata(*s.campaign);
        s.files.clear();
        return {};
    } catch (const ApplicationSessionError& error) {
        return std::unexpected(error);
    } catch (const std::exception& error) {
        return std::unexpected(ApplicationSessionError{ ApplicationSessionFailure::profile, {}, error.what() });
    }
}

std::expected<void, ApplicationSessionError>
ApplicationSession::create_profile(std::uint32_t slot, std::u16string_view name, bool easy) {
    try {
        const auto result = state_->profiles->create(slot, name, easy);
        if (!result)
            return std::unexpected(profile_error(result.error()));
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(ApplicationSessionError{ ApplicationSessionFailure::profile, {}, error.what() });
    }
}

void ApplicationSession::State::discard_intro_profile_selection() {
    if (!park || park->context().mode.source_recreation != 1)
        return;
    //547750/530AC0 ->776C40(mode4): persist the ordinary player before
    //discarding its campaign. Keep the Intro World and its ATM/RSSE owners.
    persist_profile();
    auto fresh = create_fresh_campaign_profile(park->context(), park->preferences());
    auto previous = capture();
    park->apply_campaign_for_lobby(fresh);
    if (const auto cleared = host.profile_services({}, true); !cleared) {
        park->apply_campaign_for_lobby(previous);
        throw cleared.error();
    }
    profiles->clear_selection();
    selected = active;
    fresh.selected_source = selected;
    campaign = std::move(fresh);
    coaster_selection.reset();
    files.clear();
}

std::expected<void, ApplicationSessionError> ApplicationSession::cancel_coaster_source_profile_route() {
    try {
        state_->discard_intro_profile_selection();
        return {};
    } catch (const ApplicationSessionError& error) {
        return std::unexpected(error);
    } catch (const std::exception& error) {
        return std::unexpected(ApplicationSessionError{ ApplicationSessionFailure::profile, {}, error.what() });
    }
}

std::expected<void, ApplicationSessionError> ApplicationSession::erase_profile(std::uint32_t slot,
                                                                               std::uint64_t generation) {
    try {
        const auto result = state_->profiles->erase(slot, generation);
        state_->files.clear();
        if (!result)
            return std::unexpected(profile_error(result.error()));
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(ApplicationSessionError{ ApplicationSessionFailure::profile, {}, error.what() });
    }
}

void ApplicationSession::request_source(std::int32_t source, bool from_profile, bool designer) {
    state_->requests.push_back({ state_->epoch, State::SourceTransition{ source, from_profile, designer, {}, 0 } });
}

void ApplicationSession::request(ApplicationGameRequest request) {
    State::GameTransition item{ std::move(request), {} };
    if (const auto* coaster = std::get_if<ApplicationSaveCoasterRequest>(&item.request.payload);
        coaster && state_->park) {
        auto& c = state_->park->context();
        const auto id = coaster->coaster;
        const auto* record = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
        if (record
            && record == coaster->coaster_identity
            && record->kind == ThingKind::park_object
            && !record->retirement_pending) {
            const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(record);
            if (const auto* mesh = state_->park->meshes().instance(object.mesh_instance_id))
                item.coaster_mesh = std::pair{ object.mesh_instance_id, mesh->generation };
        }
    }
    state_->requests.push_back({ state_->epoch, std::move(item) });
}

void ApplicationSession::request_coaster_source(ApplicationCoasterSourceSelection selection) {
    const auto source = selection.source_index;
    state_->requests.push_back(
        { state_->epoch, State::SourceTransition{ source, false, true, std::move(selection), 0 } });
}

std::expected<bool, ApplicationSessionError>
ApplicationSession::begin_same_source(const ApplicationCoasterSourceSelection& selection) {
    auto& s = *state_;
    if (!s.park || s.active != selection.source_index || s.park->context().mode.game != GameMode::code_3)
        return false;
    try {
        auto retained = selection;
        //Native5305B0 leaves the previous tool untouched until real World input
        //reaches4D6160. ParkRuntime owns the validated pending value copy.
        s.park->stage_coaster_source(retained);
        s.coaster_selection = std::move(retained);
        return true;
    } catch (const std::exception& error) {
        return std::unexpected(ApplicationSessionError{ ApplicationSessionFailure::source, {}, error.what() });
    }
}

void ApplicationSession::request_saved_park(std::filesystem::path path, std::string theme) {
    state_->requests.push_back({ state_->epoch, State::SavedParkTransition{ std::move(path), std::move(theme) } });
}

void ApplicationSession::request_quit() {
    state_->requests.push_back({ 0, State::QuitTransition{} });
}

void ApplicationSession::process_requests() {
    auto& s = *state_;
    if (s.processing)
        throw std::logic_error("Application transition dispatch is not reentrant");
    s.processing = true;

    struct Finish {
        bool& value;

        ~Finish() {
            value = false;
        }
    } finish{ s.processing };

    auto pending = std::exchange(s.requests, {});
    for (const auto& item : pending) {
        s.transition_completed = false;
        const auto* game = std::get_if<State::GameTransition>(&item.payload);
        const auto serial = game ? game->request.serial : 0;
        try {
            if (!std::holds_alternative<State::QuitTransition>(item.payload) && item.epoch != s.epoch)
                fail(ApplicationSessionFailure::state, {}, "Queued request belongs to a retired World");
            if (game && game->coaster_mesh) {
                const auto* mesh = s.park ? s.park->meshes().instance(game->coaster_mesh->first) : nullptr;
                if (!mesh || mesh->generation != game->coaster_mesh->second)
                    fail(ApplicationSessionFailure::state,
                         {},
                         "Queued coaster save belongs to a retired mesh lifetime");
            }
            match (item.payload) {
                case { const State::SourceTransition& source }   => s.source(source);
                case { const State::GameTransition& request }    => s.game(request.request);
                case { const State::SavedParkTransition& saved } => s.load_path(saved);
                case { State::QuitTransition }                   => s.quit(0);
            }
        } catch (const ApplicationSessionError& error) {
            s.diagnostics.push_back(error);
            s.complete(s.transition_completed ? 0 : serial, error);
        } catch (const std::exception& error) {
            ApplicationSessionError reported{ ApplicationSessionFailure::state, {}, error.what() };
            s.diagnostics.push_back(reported);
            s.complete(s.transition_completed ? 0 : serial, std::move(reported));
        }
    }
}

bool ApplicationSession::has_pending_requests() const {
    return !state_->requests.empty();
}

bool ApplicationSession::has_pending_world_transition() const {
    return std::ranges::any_of(state_->requests, [](const State::Pending& item) {
        return match (item.payload) -> bool {
            case { State::SourceTransition } or { State::SavedParkTransition } => true;
            case {
                const State::GameTransition& game
            } => std::holds_alternative<ApplicationLoadParkRequest>(game.request.payload)
                     || std::holds_alternative<ApplicationChangePlayerRequest>(game.request.payload)
                     || std::holds_alternative<ApplicationLeaveCoasterDesignerRequest>(game.request.payload);
            case { State::QuitTransition } => false;
        };
    });
}

std::expected<std::vector<ApplicationFileEntry>, ApplicationSessionError>
ApplicationSession::list_files(ApplicationFileKind kind) {
    try {
        return state_->list(kind);
    } catch (const ApplicationSessionError& error) {
        return std::unexpected(error);
    } catch (const std::exception& error) {
        return std::unexpected(ApplicationSessionError{ ApplicationSessionFailure::load, {}, error.what() });
    }
}

std::expected<void, ApplicationSessionError> ApplicationSession::save_current_profile() {
    try {
        state_->persist_profile();
        return {};
    } catch (const ApplicationSessionError& error) {
        return std::unexpected(error);
    } catch (const std::exception& error) {
        return std::unexpected(ApplicationSessionError{ ApplicationSessionFailure::save, {}, error.what() });
    }
}

const std::vector<ApplicationSessionError>& ApplicationSession::diagnostics() const {
    return state_->diagnostics;
}
} //namespace simcoaster
