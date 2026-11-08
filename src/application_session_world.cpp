#include "application_session_internal.hpp"
#include "simcoaster/application_world_entry.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>

namespace simcoaster {
using namespace application_session_detail;

namespace {
void set_retained_save(ParkRuntime&, const CampaignProfile&);
} //namespace

std::expected<void, ApplicationSessionError>
ApplicationSession::recreate_graphics(const ApplicationHardwareProfile& profile) {
    auto& s = *state_;
    try {
        if (!s.park || s.active < 0)
            fail(ApplicationSessionFailure::state, {}, "No World for graphics recreation");
        //459D80 requests recreation level2 for a texture-source quality change.
        //Stage the real complete save without publishing any user save name.
        FileTransaction files(s.root);
        const auto image = files.stage(s.root / "save/.graphics-recreation.TPWS");
        s.park->save(image);
        auto saved = s.capture();
        const bool designer = s.park->context().mode.game == GameMode::code_3;
        auto next = std::make_unique<ParkRuntime>(s.root,
                                                  ParkRuntimeOpen{ s.sources.at(static_cast<std::size_t>(s.active)),
                                                                   image,
                                                                   &saved,
                                                                   designer,
                                                                   nullptr,
                                                                   s.host.resource_pump,
                                                                   profile });
        if (designer && s.coaster_selection)
            install_coaster_source_selection(next->context(), *s.coaster_selection);
        auto captured = next->capture_campaign(saved.auxiliary);
        captured.theme->save_file_basename = saved.theme->save_file_basename;
        captured.theme->use_gmi_suffix = saved.theme->use_gmi_suffix;
        set_retained_save(*next, captured);
        s.replace(std::move(next),
                  std::move(captured),
                  s.active,
                  s.page,
                  0,
                  ApplicationWorldEntry::none,
                  false,
                  {},
                  s.coaster_selection);
        return {};
    } catch (const ApplicationSessionError& error) {
        return std::unexpected(error);
    } catch (const std::exception& error) {
        return std::unexpected(ApplicationSessionError{ ApplicationSessionFailure::load, {}, error.what() });
    }
}

namespace {
void
write_profile(FileTransaction& transaction, const std::filesystem::path& destination, const CampaignProfile& campaign) {
    const auto result = write_campaign_profile(transaction.stage(destination), campaign);
    if (!result)
        throw profile_error(result.error());
}

void set_retained_save(ParkRuntime& park, const CampaignProfile& campaign) {
    auto& manager = get_or_create_theme_global_info_manager(park.context());
    manager.save_file_basename = campaign.theme->save_file_basename;
    manager.use_gmi_suffix = campaign.theme->use_gmi_suffix;
}
} //namespace

void ApplicationSession::State::stage_save(FileTransaction& transaction,
                                           CampaignProfile& saved,
                                           std::u16string_view basename,
                                           bool gmi) {
    if (!park || !valid_basename(basename))
        fail(ApplicationSessionFailure::save, {}, "Invalid named park save");
    const auto previous =
        saved.theme->save_file_basename.empty() ? std::u16string{ u"autosave" } : saved.theme->save_file_basename;
    const bool previous_gmi = saved.theme->use_gmi_suffix != 0;
    const auto directory = profile_directory();
    saved.selected_source = active;
    saved.theme->save_file_basename = basename;
    saved.theme->use_gmi_suffix = gmi ? 1 : 0;
    //745720 copies the three gameplay source namespaces, excluding current
    //and Intro. Missing unvisited parks are normal: both original CopyFile
    //attempts fail without preventing the current park/metadata write.
    if (!gmi)
        for (std::int32_t source_index = 0; source_index < 3; ++source_index) {
            if (source_index == active)
                continue;
            const auto destination = source_file(source_index, basename);
            const auto original = source_file(source_index, previous, previous_gmi);
            if (equal_path(original, destination))
                continue;
            const auto restart = source_file(source_index, u"restart", true);
            if (!std::filesystem::exists(transaction.read(original))
                && !std::filesystem::exists(transaction.read(restart)))
                continue;
            try {
                transaction.copy(original, destination);
            } catch (const std::filesystem::filesystem_error&) {
                //Native745AB8..745BB3 retries precisely this sibling restart image.
                transaction.copy(restart, destination);
            }
        }
    park->save(transaction.stage(source_file(active, basename, gmi)));
    write_profile(transaction,
                  directory / std::filesystem::path(std::u16string(basename) + (gmi ? u".GMI" : u".GMS")),
                  saved);
}

void ApplicationSession::State::source(const SourceTransition& request) {
    if (request.source < 0 || static_cast<std::size_t>(request.source) >= sources.size() || !park)
        fail(ApplicationSessionFailure::source, {}, "Invalid source transition");
    FileTransaction transaction(root);
    auto next_campaign = capture();
    auto retained_selection =
        request.designer ? request.coaster_source : std::optional<ApplicationCoasterSourceSelection>{};
    //Profile activation/lobby departure skips exactly the old lobby autosave;
    //an ordinary live-park source transition first preserves the departing park.
    if (!request.from_profile && can_autosave())
        stage_save(transaction, next_campaign, u"autosave");
    const bool detach_profile = request.coaster_source && request.coaster_source->detach_profile;
    const bool entering_profiles = request.source == 3 && !request.designer;
    if ((entering_profiles || detach_profile) && profiles->selected_slot()) {
        if (const auto result = host.profile_services(profile_directory(), false); !result)
            throw result.error();
        if (const auto result = host.save_configuration(); !result)
            throw result.error();
        if (!can_autosave() && park->context().mode.game != GameMode::code_3) {
            next_campaign.theme->save_file_basename = u"autosave";
            next_campaign.theme->use_gmi_suffix = 0;
            write_profile(transaction, profile_directory() / "autosave.GMS", next_campaign);
        }
    }
    if (detach_profile || entering_profiles)
        //776C40(mode4) discards the ordinary manager/shares and does not reload
        //its GMS. The source picker supplies owned geometry separately.
        next_campaign = create_fresh_campaign_profile(park->context(), park->preferences());
    const bool first_start = next_campaign.theme->manager_startup_action_pending != 0;
    next_campaign.selected_source = request.source;
    //6A3CAC consumes the latch on EVERY source entry, including Intro/CDK.
    next_campaign.theme->manager_startup_action_pending = 0;
    const auto basename = next_campaign.theme->save_file_basename.empty() ? std::u16string{ u"autosave" }
                                                                          : next_campaign.theme->save_file_basename;
    std::optional<std::filesystem::path> saved;
    if (!request.designer && first_start && request.source == 1)
        saved = root / "Data/PreBuilt/prebuilt.TPWS";
    else if (!request.designer && profiles->selected_slot() && request.source != 3) {
        const auto path = source_file(request.source, basename, next_campaign.theme->use_gmi_suffix != 0);
        const auto input = transaction.read(path);
        if (std::filesystem::exists(input))
            saved = input;
        //746660 is the direct source-entry load. An absent park falls through to
        //genuine base-map/catalog/Scape population, without scanning other names.
    }
    const auto create_world = [&] {
        return std::make_unique<ParkRuntime>(
            root,
            ParkRuntimeOpen{ sources[static_cast<std::size_t>(request.source)],
                             saved,
                             &next_campaign,
                             request.designer,
                             request.coaster_source ? &*request.coaster_source : nullptr,
                             host.resource_pump,
                             host.hardware_profile ? std::optional{ host.hardware_profile() } : std::nullopt });
    };
    bool prebuilt_loaded = !request.designer && first_start && request.source == 1;
    std::unique_ptr<ParkRuntime> next;
    if (prebuilt_loaded) {
        try {
            next = create_world();
        } catch (const std::exception& error) {
            //6A3150 suppresses default population only when7468B0 succeeds. A
            //failed isolated prebuilt candidate must not leave a half-restored
            //World.
            diagnostics.push_back(
                { ApplicationSessionFailure::load,
                  *saved,
                  "Prebuilt park rejected; trying native fresh population: " + std::string(error.what()) });
            prebuilt_loaded = false;
            saved.reset();
            next = create_world(); //A genuine fresh-source failure still propagates.
        }
    } else
        next = create_world();
    if (prebuilt_loaded)
        reset_application_prebuilt_world(*next);
    set_retained_save(*next, next_campaign);
    next->apply_preferences(next_campaign.options);
    if (!request.designer && request.source != 3 && profiles->selected_slot()) {
        const auto current = source_file(request.source, basename, next_campaign.theme->use_gmi_suffix != 0);
        if (!std::filesystem::exists(transaction.read(current)))
            //6A2AD0/6A4F7B write restart.TPWI, not a guessed autosave, when the
            //exact retained source file cannot be opened after a new source starts.
            next->save(transaction.stage(source_file(request.source, u"restart", true)));
    }
    auto captured = next->capture_campaign(next_campaign.auxiliary);
    captured.selected_source = request.source;
    captured.theme->save_file_basename = basename;
    captured.theme->use_gmi_suffix = next_campaign.theme->use_gmi_suffix;
    replace(
        std::move(next),
        std::move(captured),
        request.source,
        entering_profiles  ? ApplicationSessionPage::profiles
        : request.designer ? ApplicationSessionPage::coaster_designer
                           : ApplicationSessionPage::park,
        request.serial,
        entering_profiles || request.designer ? ApplicationWorldEntry::none
        : prebuilt_loaded                     ? ApplicationWorldEntry::first_prebuilt
        : saved                               ? ApplicationWorldEntry::restored
                                              : ApplicationWorldEntry::fresh,
        false,
        [&transaction] {
            transaction.commit();
        },
        std::move(retained_selection),
        entering_profiles || detach_profile ? std::optional<ProfileBinding>{ ProfileBinding{} } : std::nullopt);
    if (const auto refreshed = profiles->refresh(); !refreshed)
        diagnostics.push_back(profile_error(refreshed.error()));
}

void ApplicationSession::State::load_named(const ApplicationLoadParkRequest& request, std::uint64_t serial) {
    validate_file(request, ApplicationFileKind::park);
    //7451E0 forms its park path before loading metadata. 74D110 preserves an
    //already selected gameplay source; a GMS's saved source is not permission
    //to load a different theme instead of this selected source's named copy.
    const auto source_index = selected;
    if (!request.file_identity)
        fail(ApplicationSessionFailure::stale_file, {}, "Named load has no selected file identity");
    const auto metadata = profile_directory() / std::filesystem::path(request.basename + u".GMS");
    auto imported = load_campaign_profile(metadata, defaults);
    if (!imported)
        throw profile_error(imported.error());
    imported->shares.ownership_context->current_profile_name = profiles->views().at(*profiles->selected_slot()).name;
    if (source_index < 0 || static_cast<std::size_t>(source_index) >= sources.size())
        fail(ApplicationSessionFailure::source, metadata, "Saved campaign source is unregistered");
    imported->theme->save_file_basename = request.basename;
    imported->theme->use_gmi_suffix = 0;
    imported->theme->manager_startup_action_pending = 0;
    const auto target = source_file(source_index, request.basename);
    std::unique_ptr<ParkRuntime> next;
    FileTransaction transaction(root);
    try {
        next = std::make_unique<ParkRuntime>(
            root,
            ParkRuntimeOpen{ sources[static_cast<std::size_t>(source_index)],
                             target,
                             &*imported,
                             false,
                             nullptr,
                             host.resource_pump,
                             host.hardware_profile ? std::optional{ host.hardware_profile() } : std::nullopt });
    } catch (const std::exception& primary) {
        const auto restart = source_file(source_index, u"restart", true);
        try {
            transaction.copy(restart, target);
            next = std::make_unique<ParkRuntime>(
                root,
                ParkRuntimeOpen{ sources[static_cast<std::size_t>(source_index)],
                                 transaction.read(target),
                                 &*imported,
                                 false,
                                 nullptr,
                                 host.resource_pump,
                                 host.hardware_profile ? std::optional{ host.hardware_profile() } : std::nullopt });
        } catch (const std::exception& fallback) {
            fail(ApplicationSessionFailure::load,
                 target,
                 std::string{ primary.what() } + "; native restart fallback: " + fallback.what());
        }
    }
    set_retained_save(*next, *imported);
    next->apply_preferences(imported->options);
    auto captured = next->capture_campaign(imported->auxiliary);
    captured.selected_source = source_index;
    captured.theme->save_file_basename = request.basename;
    captured.theme->use_gmi_suffix = 0;
    replace(std::move(next),
            std::move(captured),
            source_index,
            source_index == 3 ? ApplicationSessionPage::intro : ApplicationSessionPage::park,
            serial,
            ApplicationWorldEntry::restored,
            true,
            [&transaction] {
                transaction.commit();
            });
}

void ApplicationSession::State::load_path(const SavedParkTransition& request) {
    const auto input = request.path.is_absolute() ? request.path : root / request.path;
    const auto source = std::ranges::find(sources, request.theme);
    if (source == sources.end())
        fail(ApplicationSessionFailure::source, input, "Explicit startup source is unregistered");
    const auto preferences = load_ui_preferences(root, input);
    if (!preferences)
        fail(ApplicationSessionFailure::profile, preferences.error().path, preferences.error().reason);
    std::optional<CampaignProfile> imported;
    std::optional<std::uint32_t> associated_slot;
    if (!preferences->profile_bytes.empty()) {
        const auto views = profiles->views();
        for (std::uint32_t slot = 0; slot < 4; ++slot)
            if (views[slot].present
                && equal_path(profiles->profile_directory(slot), preferences->profile_path.parent_path())) {
                associated_slot = slot;
                break;
            }
        auto decoded = associated_slot ? profiles->load_for_activation(*associated_slot, preferences->profile_path)
                                       : decode_campaign_profile(preferences->profile_bytes, defaults);
        if (!decoded)
            throw profile_error(decoded.error());
        imported = std::move(*decoded);
        imported->theme->save_file_basename = input.stem().u16string();
        imported->theme->use_gmi_suffix = extension_is(input, u".TPWI") ? 1 : 0;
        imported->theme->manager_startup_action_pending = 0;
    }
    auto next = std::make_unique<ParkRuntime>(
        root,
        ParkRuntimeOpen{ request.theme,
                         input,
                         imported ? &*imported : nullptr,
                         false,
                         nullptr,
                         host.resource_pump,
                         host.hardware_profile ? std::optional{ host.hardware_profile() } : std::nullopt });
    auto captured = next->capture_campaign(imported ? imported->auxiliary : CampaignAuxiliaryState{});
    const auto index = static_cast<std::int32_t>(source - sources.begin());
    captured.selected_source = index;
    captured.theme->save_file_basename = input.stem().u16string();
    captured.theme->use_gmi_suffix = extension_is(input, u".TPWI") ? 1 : 0;
    set_retained_save(*next, captured);
    replace(std::move(next),
            std::move(captured),
            index,
            index == 3 ? ApplicationSessionPage::intro : ApplicationSessionPage::park,
            0,
            ApplicationWorldEntry::restored,
            false,
            {},
            {},
            ProfileBinding{ associated_slot });
}

void ApplicationSession::State::save_named(const ApplicationSaveParkRequest& request, std::uint64_t serial) {
    validate_file(request, ApplicationFileKind::park);
    const auto destination = profile_directory() / std::filesystem::path(request.basename + u".GMS");
    if (std::filesystem::exists(destination) && (!request.overwrite || !request.file_identity))
        fail(ApplicationSessionFailure::save, destination, "Existing named save needs its confirmed file identity");
    auto saved = capture();
    FileTransaction transaction(root);
    stage_save(transaction, saved, request.basename);
    transaction.commit();
    saved_metadata(saved);
    campaign = std::move(saved);
    if (const auto refreshed = profiles->refresh(); !refreshed)
        diagnostics.push_back(profile_error(refreshed.error()));
    complete(serial);
}

void ApplicationSession::State::game(const ApplicationGameRequest& request) {
    match (request.payload) {
        case { const ApplicationLoadParkRequest& payload } => {
            load_named(payload, request.serial);
            return;
        }
        case { const ApplicationSaveParkRequest& payload } => {
            save_named(payload, request.serial);
            return;
        }
        case { const ApplicationSaveCoasterRequest& payload }   => save_coaster(payload);
        case { const ApplicationDeleteParkRequest& payload }    => delete_file(payload, ApplicationFileKind::park);
        case { const ApplicationDeleteCoasterRequest& payload } => delete_file(payload, ApplicationFileKind::coaster);
        case { ApplicationChangePlayerRequest }                 => {
            SourceTransition pending;
            pending.source = 3;
            pending.serial = request.serial;
            source(pending);
            return;
        }
        case { ApplicationLeaveCoasterDesignerRequest } => {
            SourceTransition pending;
            pending.source = coaster_selection && coaster_selection->remember_current_source
                               ? coaster_selection->previous_source_index
                               : 3;
            pending.from_profile = pending.source != 3;
            pending.serial = request.serial;
            source(pending);
            return;
        }
        case { ApplicationQuitRequest } => {
            quit(request.serial);
            return;
        }
        case { ApplicationCancelLoadRequest } => {
            discard_intro_profile_selection();
            if (const auto external = host.external_request(request); !external)
                throw external.error();
        }
        case { ApplicationGoOnlineRequest }
            or { ApplicationGoOfflineRequest }
            or { ApplicationChooseCoasterSourceRequest } => {
            if (const auto external = host.external_request(request); !external)
                throw external.error();
        }
    } complete(request.serial);
}
} //namespace simcoaster
