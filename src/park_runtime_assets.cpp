#include "park_runtime_codec.hpp"
#include "park_runtime_state.hpp"

namespace simcoaster {
using namespace park_detail;

void ParkRuntime::State::initialize_assets(const std::filesystem::path& root, const std::string& theme) {
    auto& startup = configuration.source_startup.world;
    const auto loaded_sources = load_standard_balance_overlays(
        root,
        theme,
        [&](const auto& path) {
            return bindings.read_balance_asset(*simulation.world, path.native_encoded_string());
        },
        [&](const std::string& source, bool preserve, const auto& path) {
            const auto* text = &source;
            const auto apply = [&](auto& balance, auto loader) {
                const auto loaded = loader(balance, *text, preserve);
                if (!loaded)
                    throw std::runtime_error(sam::describe_error(path.native_encoded_string(), loaded.error()));
            };
            apply(simulation.world->visitors.environment, load_person_environment_balance);
            apply(simulation.world->visitors.needs_balance, load_person_balance);
            apply(simulation.world->visitors.creation_balance, load_person_creation_balance);
            apply(simulation.world->visitors.arrivals, load_arrival_balance);
            apply(simulation.world->visitors.flow_balance, load_guest_flow_balance);
            apply(simulation.world->attractions.park_object_creation_balance, load_park_object_creation_balance);
            apply(simulation.world->research.balance, load_world_research_balance);
            const auto staff =
                sam::parse_record<StaffRecruitmentBalance, true>(simulation.world->staff.recruitment.balance,
                                                                 *text,
                                                                 preserve);
            if (!staff)
                throw std::runtime_error(sam::describe_error(path.native_encoded_string(), staff.error()));
            const auto challenges =
                sam::parse_record<ChallengeBalance, true>(simulation.world->challenges.balance, *text, preserve);
            if (!challenges)
                throw std::runtime_error("Invalid challenge schedule balance at byte "
                                         + std::to_string(challenges.error().offset));
            const auto weather_loaded = load_weather_balance_checked(*simulation.world, *text, preserve);
            if (!weather_loaded)
                throw std::runtime_error(sam::describe_error(path.native_encoded_string(), weather_loaded.error()));
            presentation.audio->configure_world_balance(*text, preserve);
            presentation.camera_input_runtime.configure_balance(*text, preserve);
            const auto parsed = load_world_source_startup_balance(configuration.source_startup, *text, preserve);
            if (!parsed)
                throw std::runtime_error(sam::describe_error(path.native_encoded_string(), parsed.error()));
            apply(simulation.world->balance.standard_supplement, [](auto& balance, std::string_view input, bool keep) {
                return sam::parse_record<StandardSupplementBalance, true>(balance, input, keep);
            });
        });
    ensure(loaded_sources, "Startup Standard.sam is missing");
    (void)ensure_world_standard_ui_balance(*simulation.world, root);
    const auto& vegetation = simulation.world->attractions.park_object_creation_balance.Vegetation;
    simulation.world->weather.vegetation_weather = { vegetation.HeatwaveWither,
                                                     vegetation.RainWither,
                                                     vegetation.StormWither,
                                                     vegetation.SnowWither };
    for (std::size_t i = 0; i < simulation.world->staff.firing_line_for_rank_limits.size(); ++i)
        simulation.world->staff.firing_line_for_rank_limits[i] =
            simulation.world->challenges.balance.rank_failures[i].limit;
    //SAM globals are configuration, independent of the World restore path.
    //In particular64B940 resets the first prebuilt game's bank from these;
    //setting them only in the fresh-manager constructor made new games $0
    //and left restored parks without their loan offers.
    simulation.world->finance.initial_cash = configuration.source_startup.initial_cash;
    simulation.world->finance.initial_admission_fee = configuration.source_startup.initial_admission_fee;
    simulation.world->finance.bank_loan_offers = configuration.source_startup.loan_offers;
    //These are source configuration globals in4288F0, not saved bank or
    //fresh-World state. Restored and prebuilt parks need the same prices.
    simulation.world->construction.path_cell_base_cost = configuration.source_startup.cell_costs.path;
    simulation.world->construction.queue_cell_base_cost = configuration.source_startup.cell_costs.queue;
    simulation.world->construction.kart_track_cell_base_cost = configuration.source_startup.cell_costs.kart_track;
    simulation.world->construction.water_track_cell_base_cost = configuration.source_startup.cell_costs.water_track;
    simulation.world->finance.spam_balance = startup.spam;
    simulation.world->balance.award_balance = startup.awards;
    simulation.world->finance.annual_report_policy = { startup.shares.AnnRptSimilarPercentage,
                                                       startup.shares.AnnRptBigChangePercentage };
    const auto& staff = simulation.world->staff.recruitment.balance;
    simulation.world->staff.work_balance = {
        staff.all.MotivationFactor,
        staff.all.MaxExperience,
        { { { staff.Handyman.MinWorkDuration, staff.Handyman.MaxWorkDuration },
            { staff.Mechanic.MinWorkDuration, staff.Mechanic.MaxWorkDuration },
            { staff.Entertainer.MinWorkDuration, staff.Entertainer.MaxWorkDuration },
            { staff.Guard.MinWorkDuration, staff.Guard.MaxWorkDuration },
            { staff.Researcher.MinResearchAbility, staff.Researcher.MaxResearchAbility },
            { staff.Gardener.MinWorkDuration, staff.Gardener.MaxWorkDuration } } }
    };
    simulation.world->research.max_researchers = staff.pool.MaxResearchersInPark;
    initialize_weather_particles(*simulation.world,
                                 *simulation.weather,
                                 static_cast<std::uint32_t>(rendering.meshes.clocks.simulation.count()));
    initialize_weather_sky(*simulation.world, *simulation.weather);
    if (configuration.hardware_profile.graphics.sky_detail == 0
        || configuration.hardware_profile.graphics.sky_detail == 1)
        simulation.weather->sky_detail = configuration.hardware_profile.graphics.sky_detail != 0;
    initialize_weather_sky_objects(*simulation.world, *simulation.weather);
    world_operations::load_weather_effect_textures(*simulation.world);
    simulation.world->finance.company_shares_issued = startup.shares.CompanySharesIssued;
    const auto challenge_definitions =
        bindings.read_balance_asset(*simulation.world, (root / "Data/Challenges.sam").native_encoded_string());
    ensure(challenge_definitions.has_value(), "Challenges.sam is missing");
    const auto parsed_challenges =
        sam::parse_record<ChallengeDefinitions, true>(simulation.world->challenges.definitions, *challenge_definitions);
    if (!parsed_challenges)
        throw std::runtime_error("Invalid Challenges.sam at byte " + std::to_string(parsed_challenges.error().offset));
    const auto language = root / "Data/Language/American";
    const auto mapping_bytes = bindings.read_asset((language / "MBToUni.dat").native_encoded_string());
    ensure(mapping_bytes.has_value(), "Staff Unicode conversion table is missing");
    const auto mapping = parse_mb_to_unicode_map(*mapping_bytes);
    if (!mapping)
        throw std::runtime_error(mapping.error().reason);
    simulation.world->messages.mail_language.conversion = *mapping;
    for (auto [name, target] :
         { std::pair{ "TAG_SYSTEM.str", &simulation.world->messages.mail_language.tag_system },
           std::pair{ "CHALLENGE_TITLES.str", &simulation.world->messages.mail_language.challenge_titles },
           std::pair{ "CHALLENGE_GENERIC.str", &simulation.world->messages.mail_language.challenge_generic },
           std::pair{ "CHALLENGE_SPECIFIC.str", &simulation.world->messages.mail_language.challenge_specific },
           std::pair{ "OBJECTIVE_COMPLETE.str", &simulation.world->messages.mail_language.objective_complete },
           std::pair{ "PROMOTIONLEVELS.str", &simulation.world->messages.mail_language.promotions },
           std::pair{ "ABILITIES.str", &simulation.world->messages.mail_language.abilities },
           std::pair{ "STAFF_TYPES.str", &simulation.world->messages.mail_language.staff_types },
           std::pair{ "INGREDIENT.str", &simulation.world->messages.mail_language.ingredients },
           std::pair{ "STAFFSTATES.str", &simulation.world->messages.mail_language.ui_staff_states },
           std::pair{ "THOUGHTS.str", &simulation.world->messages.mail_language.ui_thoughts },
           std::pair{ "KIDSTATES.str", &simulation.world->messages.mail_language.ui_kid_states },
           std::pair{ "OBJECT_DESC.str", &simulation.world->messages.mail_language.object_descriptions },
           std::pair{ "UIHELPTEXT.str", &simulation.world->messages.mail_language.ui_help },
           std::pair{ "UITEXT.str", &simulation.world->messages.mail_language.ui_text },
           std::pair{ "OBJECT_NAMES.str", &simulation.world->messages.mail_language.object_names },
           std::pair{ "MONTHS.str", &simulation.world->messages.mail_language.months },
           std::pair{ "LOANNAMES.str", &simulation.world->messages.mail_language.lender_names } }) {
        auto bytes = bindings.read_asset((language / name).native_encoded_string());
        ensure(bytes.has_value(), "Mail language resource is missing");
        *target = std::move(*bytes);
    }
    constexpr std::array names{ "HANDYMAN_NAMES.str", "MECHANIC_NAMES.str",   "ENTERTAINER_NAMES.str",
                                "GUARD_NAMES.str",    "RESEARCHER_NAMES.str", "GARDENER_NAMES.str" };
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto bytes = bindings.read_asset((language / names[i]).native_encoded_string());
        ensure(bytes.has_value(), "Staff name table is missing");
        auto table = parse_bfst_literals(*bytes, *mapping);
        if (!table)
            throw std::runtime_error(std::string{ names[i] } + ": " + table.error().reason);
        ensure(table->strings.size() >= 35, "Staff name table has too few entries");
        simulation.world->staff.recruitment.names[i] = std::move(table->strings);
    }
    initialize_world_map_and_track_storage(*simulation.world->simulation.current, startup);
    const auto base = bindings.read_asset((root / "Data/levels" / theme / "terrain/Base.map").native_encoded_string());
    ensure(base && load_world_base_map(*simulation.world, *simulation.world->simulation.current, *base),
           "Base.map initialization failed");
    auto objects = load_installed_object_catalog(root, theme);
    if (!objects)
        throw std::runtime_error(describe_catalog_error(objects.error()));
    content.catalog = std::move(*objects);
    simulation.world->resources.object_catalog = content.catalog.runtime_view();
    //Native saved resource slots contain category+relative variant, never an
    //absolute ESP index. Keep one canonical maximum catalog for this World:
    //changing quality only changes future selection limits, not live handles
    //or registered resource indices beneath running resource/RSSE scripts.
    SpriteCatalogSettings full_catalog;
    full_catalog.num_kids = 3;
    full_catalog.use_f_family = false; //Native6A3768 initializes68E240(0,0): ordinary T sources.
    auto loaded_sprites = load_installed_sprite_catalog(root, theme, full_catalog);
    if (!loaded_sprites)
        throw std::runtime_error(describe_sprite_catalog_error(loaded_sprites.error()));
    content.sprites = std::move(*loaded_sprites);
    initialize_resource_variant_runtime(*simulation.world, content.sprites);
    apply_hardware_profile(configuration.hardware_profile);
    bindings.reload_sprite_family = [this, archive_path = root / "Data/esprites.wad"](bool f_family) {
        if (content.sprite_f_family == f_family)
            return;
        if (!content.sprite_texture_archive) {
            auto archive = load_dwfb_archive(archive_path);
            if (!archive)
                throw std::runtime_error("Cannot open sprite texture archive: "
                                         + std::string{ asset_failure_name(archive.error().reason) });
            content.sprite_texture_archive = std::move(*archive);
        }
        struct Replacement {
            std::size_t index;
            std::string path;
            std::vector<std::byte> pixels;
        };
        std::vector<Replacement> pending;
        SpriteCatalogSettings settings;
        settings.use_f_family = f_family;
        for (std::size_t i = 0; i < content.sprites.resources.size(); ++i) {
            const auto& resource = content.sprites.resources[i];
            if (!resource.descriptor.texture_suffix_reload_flag)
                continue;
            auto path = sprite_pixel_sidecar_path(resource.descriptor_path, resource.descriptor, settings);
            const auto entry = content.sprite_texture_archive->find_entry(path);
            if (!entry)
                throw std::runtime_error("Missing camera-mode sprite texture: " + path);
            auto pixels = content.sprite_texture_archive->read_entry(*entry);
            if (!pixels)
                throw std::runtime_error("Invalid camera-mode sprite texture: " + path);
            pending.push_back({ i, std::move(path), std::move(*pixels) });
        }
        //Preserve every registered resource index and live script handle.
        //Only the native reload-flagged sources change; stage before commit.
        for (auto& replacement : pending) {
            auto& resource = content.sprites.resources[replacement.index];
            resource.pixel_path = std::move(replacement.path);
            resource.pixel_payload = std::move(replacement.pixels);
        }
        content.sprite_f_family = f_family;
        rendering.sprite_scene = WorldSpriteScene{};
        rendering.particle_scene = WorldParticleScene{};
        //SpritePixelCatalog frame spans borrow the replaced pixel buffers.
        //The catalog object address is unchanged across F/T reloads.
        rendering.mobile_picker = WorldMobilePicker{};
    };
    for (auto& asset : content.catalog.definitions) {
        presentation.audio->load_object_catalog(*asset);
        const auto id = load_object_definition_preview_mesh(*simulation.world, *asset);
        if (!id)
            throw std::runtime_error(rendering.meshes.last_error ? describe_mesh_error(*rendering.meshes.last_error)
                                                                 : "Object template load failed");
        load_object_definition_supplemental_meshes(*simulation.world, *asset);
        if (!asset->balance.Info.DontApplyOffset && asset->balance.Bumper.WhichTrackType == 3) {
            auto dynamic = load_dynamic_texture_mesh_balance(*asset);
            if (!dynamic)
                throw std::runtime_error(describe_catalog_error(dynamic.error()));
            if (*dynamic) {
                WorldCoasterDefinition definition;
                definition.balance = std::move(**dynamic);
                auto directory = std::filesystem::path{ asset->source_path };
                if (canonical_asset_key(directory.extension().native_encoded_string()) == ".wad")
                    directory.replace_extension();
                const auto path = directory.native_encoded_string();
                std::optional<MeshError> mesh_error;
                auto trains = build_ride_train_configuration(
                    definition.balance,
                    [&](std::uint32_t key, std::string_view name) -> std::optional<RideCarMeshMetadata> {
                        const auto loaded = rendering.meshes.load_template(
                            *simulation.world,
                            { path, name, nullptr, nullptr, 0xC0u, key, 1, nullptr, nullptr, 0, 0 });
                        if (!loaded) {
                            mesh_error = loaded.error();
                            return std::nullopt;
                        }
                        return ride_car_mesh_metadata(rendering.meshes.definition(*loaded)->assets->model, *loaded);
                    });
                if (!trains)
                    throw std::runtime_error(mesh_error ? describe_mesh_error(*mesh_error)
                                                        : "Invalid coaster train configuration: " + path);
                definition.trains = std::move(*trains);
                for (std::size_t i = 0; i < definition.balance.pylons.size(); ++i) {
                    const auto& pylon = definition.balance.pylons[i];
                    if (pylon.pcMeshFilename) {
                        DefinitionGrid shape{};
                        shape.width = pylon.uiPylonWidthCells;
                        shape.height = pylon.uiPylonLengthCells;
                        const auto loaded =
                            rendering.meshes.load_template(*simulation.world,
                                                           { path,
                                                             *pylon.pcMeshFilename,
                                                             &shape,
                                                             nullptr,
                                                             0x1013405u,
                                                             static_cast<std::uint32_t>(pylon.uiPylonTypeUniqueID),
                                                             5,
                                                             nullptr,
                                                             nullptr,
                                                             0,
                                                             0 });
                        if (!loaded)
                            throw std::runtime_error(describe_mesh_error(loaded.error()));
                        definition.pylon_templates[i] = *loaded;
                    }
                }
                const auto pylons = prepare_coaster_pylons(*simulation.world, rendering.meshes, definition);
                if (!pylons)
                    throw std::runtime_error(path + ": " + describe_mesh_error(pylons.error()));
                rendering.meshes.configure_coaster_template(id, std::move(definition));
            }
        }
    }
    load_queue_mesh_definition_templates(*simulation.world);
    load_gokart_mesh_definition_templates(*simulation.world);
    load_water_ride_mesh_definition_templates(*simulation.world);
    load_hoarding_mesh_definition_templates(*simulation.world);
    //672EB0 stores nullable results from all four compiled cell-template
    //tables. 4C2E20 returns zero when a source archive is absent; aliases
    //inherit that zero. Intro deliberately ships no hoarding.wad. A saved
    //mesh or live cell that actually requires an absent template is checked
    //at its point of use, rather than rejecting unused table rows here.
    rendering.meshes.last_error.reset();
    const auto library_bytes = bindings.read_asset((root / "Data/Particle/Tp2.plb").native_encoded_string());
    ensure(library_bytes.has_value(), "Native particle library is missing");
    const auto library =
        initialize_world_profile_particles(*simulation.particles, *library_bytes, configuration.hardware_profile);
    if (!library)
        throw std::runtime_error(
            "Data/Particle/Tp2.plb at byte " + std::to_string(library.error().offset) + ": " + library.error().reason);
    configuration.particle_library = *library;
    //HUD clones have independent ownership, as in the original. They must
    //never become World mesh slots or be serialized with park geometry.
    load_advisor_mesh_resources(*simulation.world, root, theme);
    //Sprite selection and research use ThemeInfo ordinals, rather than the
    //resource-source list's indices. Bind the real global.sam-owned record
    //when the restored park's requested theme has been loaded.
    const auto selected = std::ranges::find_if(theme_global_source_names, [&](std::string_view source) {
        return canonical_asset_key(std::string{ source }) == canonical_asset_key(theme);
    });
    if (selected == theme_global_source_names.end())
        throw std::invalid_argument("Unknown park theme: " + theme);
    auto& manager = get_or_create_theme_global_info_manager(*simulation.world);
    manager.selected_theme_info = get_or_create_theme_global_info_for_ordinal(
        *simulation.world,
        manager,
        static_cast<std::uint32_t>(selected - theme_global_source_names.begin()));
    ensure(manager.selected_theme_info != nullptr, "Selected park ThemeInfo could not be loaded");
}
} //namespace simcoaster
