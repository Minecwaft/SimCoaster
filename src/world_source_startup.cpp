#include "simcoaster/world_object_placement.hpp"
#include "simcoaster/world_source_startup.hpp"
#include "park_construction_internal.hpp"
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/park_construction.hpp"
#include "simcoaster/rsse_runtime.hpp"
#include "simcoaster/standard_balance.hpp"
#include "simcoaster/world_animation.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_challenge.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_source_hoarding.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_weather.hpp"
#include <algorithm>
#include <bit>
#include <ctime>
#include <memory>
#include <stdexcept>

namespace simcoaster {
namespace {
template <class T, class Initialize>
T* create_payload(WorldRuntime& c, Initialize initialize) {
    auto* storage = static_cast<T*>(c.entities.allocate_payload(c.ports.memory, sizeof(T)));
    if (!storage)
        throw std::bad_alloc{};
    std::construct_at(storage);
    try {
        if (initialize(storage) != storage)
            throw std::runtime_error("Fresh World constructor lost its allocation");
        return storage;
    } catch (...) {
        const auto id = storage->base.header.id;
        //A constructor may already have published its record. The World owns
        //that allocation from publication onward, including exception cleanup.
        if (!id || id >= c.entities.size() || c.entities[id].payload != &storage->base.header) {
            std::destroy_at(storage);
            c.entities.release_payload(c.ports.memory, storage);
        }
        throw;
    }
}

ParkAnalyserState* create_analyser(WorldRuntime& c, ParkAnalyserState* analyser) {
    //5AC390/5AF750. The restore initializer shares the exact metric/history
    //constructors; fresh setup additionally seeds timestamps and grid bounds.
    initialize_world_map_object_base(c, &analyser->base, 1, 13);
    initialize_park_analyser_for_restore(*analyser);
    analyser->map_squares_per_cell = 4;
    analyser->grid_size_x = analyser->grid_size_y = 32;
    analyser->map_size_x = analyser->map_size_y = 128;
    FileTime time{};
    calculate_world_calendar_filetime(c, &c.referenced_world()->calendar, &time);
    const auto timestamp = std::uint64_t{ time.low } | (std::uint64_t{ time.high } << 32);
    analyser->last_strike = analyser->last_puke = analyser->last_prank = analyser->park_last_opened =
        analyser->last_object_built = analyser->last_staff_hired = timestamp;
    for (std::size_t i = 0; i < analyser->grid.size(); ++i) {
        auto& cell = analyser->grid[i];
        cell.start_x = static_cast<std::int32_t>(i % 32) * 4;
        cell.start_y = static_cast<std::int32_t>(i / 32) * 4;
        cell.size_x = cell.size_y = 4;
    }
    for (const auto category : { 12, 17, 19, 7 })
        register_world_used_thing_for_event(c,
                                            get_world_event_subscription_registry(c),
                                            &analyser->base.header,
                                            category);
    return analyser;
}

void apply_share_defaults(WorldRuntime& c, const ShareStartupParameters& balance) {
    //620930 fills only unset profile values. A new park must not erase the
    //already loaded campaign portfolio or company name.
    auto& state = c.finance.shares_finance;
    if (!state.ownership_context)
        state.ownership_context = std::make_unique<SharesOwnershipContext>();
    if (!state.share_price_context)
        state.share_price_context = std::make_unique<SharePriceContext>();
    auto& ownership = *state.ownership_context;
    if (ownership.shares_owned == -1 && balance.InitialSharesOwned > 0)
        ownership.shares_owned = balance.InitialSharesOwned;
    if (ownership.shares_owned > 0 && balance.CompanySharesIssued > 0)
        ownership.ownership_percentage =
            static_cast<std::int32_t>(std::int64_t{ 100 } * ownership.shares_owned / balance.CompanySharesIssued);
    auto& price = *state.share_price_context;
    if (price.company_title.empty()) {
        auto title =
            format_bfst_wide_string(c.messages.mail_language.tag_system, c.messages.mail_language.conversion, 172, 30);
        if (!title)
            throw std::runtime_error("Initial company title: " + title.error().reason);
        price.company_title = std::move(*title);
    }
    if (price.current_share_price == -1 && balance.InitialSharePrice > 0)
        price.current_share_price = balance.InitialSharePrice;
}

bool same_key(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) {
        const auto lower = [](unsigned char v) {
            return v >= 'A' && v <= 'Z' ? static_cast<unsigned char>(v + ('a' - 'A')) : v;
        };
        return lower(x) == lower(y);
    });
}

ParkObjectRuntimeView* create_catalog_object(WorldRuntime& c, CellId origin, std::uint16_t definition) {
    return create_payload<ParkObjectRuntimeView>(c, [&](auto* storage) {
        return world_operations::initialize_park_object_instance(c, storage, origin, definition, 0, 0, 0);
    });
}
} //namespace

std::expected<void, sam::Error>
load_world_source_startup_balance(WorldSourceStartupBalance& output, std::string_view text, bool preserve) {
    EconomyStartup economy;
    economy.bank = { output.initial_cash, output.initial_admission_fee };
    economy.cell_costs = output.cell_costs;
    for (std::size_t i = 0; i < economy.loans.size(); ++i) {
        const auto& loan = output.loan_offers[i];
        economy.loans[i] = { loan.amount,
                             loan.apr_in_percent,
                             loan.repayment_period_in_months,
                             loan.lender_name_index };
    }
    auto world = output.world;
    auto parsed = sam::parse_record<WorldStartupBalance, true>(world, text, preserve);
    if (!parsed)
        return std::unexpected(parsed.error());
    parsed = sam::parse_record<EconomyStartup, true>(economy, text, preserve);
    if (!parsed)
        return std::unexpected(parsed.error());
    output.world = std::move(world);
    output.initial_cash = economy.bank.cash;
    output.initial_admission_fee = economy.bank.admission_fee;
    output.cell_costs = economy.cell_costs;
    for (std::size_t i = 0; i < economy.loans.size(); ++i) {
        const auto& loan = economy.loans[i];
        output.loan_offers[i] = { loan.amount, loan.apr, loan.months, loan.lender };
    }
    return {};
}

void initialize_world_runtime_state(WorldRuntime& c, World& world, const WorldSourceStartupBalance& balance) {
    if (&world != c.simulation.current || &world != c.referenced_world() || c.entities.active_head)
        throw std::logic_error("Fresh source setup requires an empty primary World");
    //6A3150 enables the RSSE manager before creating the source World. The
    //restore reader initializes it separately; fresh object constructors must
    //also load and bind their real programs, including Gates/ATM and 4798.
    initialize_rsse_script_state_manager(c);
    world.saved.state = WorldState::code_1;
    world.saved.number_of_visitors_to_date = world.saved.game_tick = 0;
    world.saved.random_seed = static_cast<std::uint32_t>(std::time(nullptr));
    reset_world_object_controls(c, world.object_controls);
    initialize_world_map_and_track_storage(world, balance.world);
    initialize_world_opening_staff_pool(c, world);
    synchronize_world_calendar_month_year(c, &world.calendar);
    c.finance.initial_cash = balance.initial_cash;
    c.finance.initial_admission_fee = balance.initial_admission_fee;
    c.finance.bank_loan_offers = balance.loan_offers;
    c.finance.company_shares_issued = balance.world.shares.CompanySharesIssued;
    //The context's steering list is the native weighted registry in typed form.
    c.visitors.steering_behaviors.assign(default_steering_behaviors.begin(), default_steering_behaviors.end());

    const auto add = [&]<class T, class Dependencies>(ThingId& role, T* (*initialize)(Dependencies&, T*)) {
        role = create_payload<T>(c, [&](T* storage) {
                   return initialize(c, storage);
               })->base.header.id;
    };
    //Exact648070 construction/publication order. Later constructors can query
    //earlier roles, and the final Tag19 event observes the complete set.
    add(world.saved.staff_hq, initialize_world_staff_hq_record);
    add(world.saved.mechanic_hq, initialize_world_mechanic_hq_state);
    add(world.saved.advisor, initialize_world_advisor_state);
    add(world.saved.tag_system, initialize_tag_system);
    add(world.saved.park_analyser, create_analyser);
    add(world.saved.research_lab, initialize_world_research_lab_state);
    add(world.saved.helper_23, initialize_world_used_thing_type23_record);
    add(world.saved.weather, initialize_world_weather_state);
    add(world.saved.bank_account, initialize_world_bank_account_state);
    add(world.saved.ui_receiver, initialize_world_ui_message_receiver);
    add(world.saved.challenge_manager, initialize_challenge_manager);
    add(world.saved.animation_manager, initialize_world_animation_state);
    add(world.saved.mission_controller, initialize_world_cutscene_state);
    initialize_world_research_lab_runtime_state(c, c.entities.at_payload<ResearchLabRecord>(world.saved.research_lab));
    apply_share_defaults(c, balance.world.shares);
    world.saved.park_gates = world.saved.lobby = world.saved.traffic_lights = 0;
    for (const auto* definition : c.resources.object_catalog.entries) {
        if (!definition)
            continue;
        const auto key = definition->resource_catalog_name;
        if (same_key(key, "gates"))
            world.saved.park_gates = create_catalog_object(c, 1, definition->id)->base.header.id;
        else if (same_key(key, "atm"))
            world.saved.lobby = create_catalog_object(c, 1, definition->id)->base.header.id;
        else if (same_key(key, "lights") || same_key(key, "atmdesk"))
            (void)create_catalog_object(c, 1, definition->id);
    }
    WorldEvent startup{ Tag19Event{ 0 } };
    dispatch_world_event_to_subscribers(c,
                                        get_world_event_subscription_registry(c),
                                        &startup,
                                        deliver_world_record_event,
                                        nullptr);
    world.saved.state = WorldState::code_0;
}

void initialize_world_source_interaction(WorldRuntime& c, World& world, ParkConstructionRuntime& editor) {
    if (&world != c.simulation.current || &world != c.referenced_world())
        throw std::logic_error("Source grid setup requires the primary World");
    editor.reset_after_restore();
    c.construction.mode = 0;
    c.construction.previous_object_definition_id = 0;
    c.construction.track_type = 0;
    c.construction.selected_track_type = 0;
    c.construction.origin_cell = 0;
    c.construction.park_object = nullptr;
    c.view.secondary_cursor_record_snapshot = nullptr;
    (void)reset_world_cells_in_grid(c, 128, 128);
    initialize_source_hoarding_interaction(c, editor);
    if (c.ports.assets.selected_resource_source(c) == 3) {
        const auto& map = c.visitors.environment.MapInfo;
        if (map.HeightfieldWidth < 0
            || map.HeightfieldWidth > 128
            || map.HeightfieldHeight < 0
            || map.HeightfieldHeight > 128)
            throw std::out_of_range("Intro active map dimensions exceed World");
        //Native65DA60 intentionally starts at0,0; it does not add MapInfo's
        //heightfield start offsets. Raw selector8 is preserved as a cell command.
        for (int y = 0; y < map.HeightfieldHeight; ++y)
            for (int x = 0; x < map.HeightfieldWidth; ++x) {
                auto& cell = world.map_cells.at(static_cast<std::size_t>(y) * 128 + x);
                (void)editor.command(c, cell.base, 0);
                refresh_world_cell_render_state(c, &cell.base);
                (void)editor.command(c, cell.base, 8);
                refresh_world_cell_render_state(c, &cell.base);
            }
    }
}

void populate_world_from_object_definition_catalog(WorldRuntime& c, World& world, ParkConstructionRuntime& editor) {
    using namespace construction_detail;
    if (&world != c.simulation.current || &world != c.referenced_world())
        throw std::logic_error("Source population requires the primary World");

    const auto surround_special = [&](const ParkObjectRuntimeView& owner, const ResearchCatalogDefinition& d) {
        //647930(false): only the footprint's own linked cells can seed the
        //one-cell exclusion border. Unrelated objects and existing paths survive.
        const auto offset = rotate_park_object_local_cell_offset({ d.cached_map_offset[0], d.cached_map_offset[1] },
                                                                 std::bit_cast<int>(owner.angle));
        const auto origin = coordinate(owner.top_left);
        const auto& shape = *d.balance->Info.Shape;
        for (int row = 0; row < shape.height; ++row)
            for (int column = 0; column < shape.width; ++column) {
                const WorldCellCoordinates p{ origin.x + offset.x + column, origin.y + offset.y + row };
                if (!bounds(p))
                    throw std::out_of_range("Source special footprint outside map");
                const auto parent = map(c, cell_id(p)).base.common.parent_id;
                if (!parent || find_first_park_object_id_in_map_cell(c, &map(c, parent)) != owner.base.header.id)
                    continue;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const WorldCellCoordinates adjacent{ p.x + dx, p.y + dy };
                        if (!bounds(adjacent))
                            continue;
                        auto& neighbor = map(c, cell_id(adjacent));
                        if (find_first_park_object_id_in_map_cell(c, &neighbor))
                            continue;
                        if (neighbor.base.common.type == 0 || neighbor.base.common.type == 5)
                            (void)try_change_world_cell_type(c, &neighbor.base, 5);
                    }
            }
    };

    //647BF0 iterates the loaded catalog, not a hardcoded list of attractions.
    //Allocation failures abort this staged source rather than leave a partial
    //obstacle raster with no owner, as the original unchecked path could do.
    for (const auto* d : c.resources.object_catalog.entries) {
        if (!d || !d->balance)
            continue;
        const auto& balance = *d->balance;
        const auto& info = balance.Info;
        if (info.IsSpecial == 1 && c.mode.game != GameMode::code_3) {
            editor.set_mode(c, 4, false);
            apply_set_ride_action(c, &d->id, 0);
            editor.state.rotation_degrees = 0;
            editor.state.placement_from_origin = false;
            const auto placed = editor.process_grid(c,
                                                    { info.EngineMapOffsetOverrideX, info.EngineMapOffsetOverrideY },
                                                    0,
                                                    0,
                                                    true,
                                                    false,
                                                    info.Shape.get());
            if (!placed || !placed->created_object)
                throw std::runtime_error("Source special placement did not create an owner");
            if (balance.DefaultCoaster.AddDefaultPylons == 1)
                build_default_coaster_catalog_layout(c, editor, placed->created_object);
            auto* owner = object(c, placed->created_object);
            if (!owner)
                throw std::runtime_error("Source special owner disappeared");
            surround_special(*owner, *d);
        } else if (info.IsCameraPath == 1 || info.IsOutsideFeature == 1) {
            (void)create_catalog_object(c, 1, d->id);
        } else if (info.IsObstacle == 1) {
            const auto& offset = balance.Obstacle;
            const WorldCellCoordinates origin{ offset.ShapeOffsetX + info.MapOffsetX,
                                               offset.ShapeOffsetY + info.MapOffsetY };
            if (!bounds(origin) || !info.Shape)
                throw std::out_of_range("Source obstacle has invalid origin or Shape");
            const auto parent = cell_id(origin);
            (void)create_catalog_object(c, parent, d->id);
            const auto& shape = *info.Shape;
            for (int row = 0; row < shape.height; ++row)
                for (int column = 0; column < shape.width; ++column) {
                    const auto code = shape.cells.at(static_cast<std::size_t>(row) * 40 + column).code;
                    if (code == 33)
                        continue;
                    const WorldCellCoordinates p{ offset.ShapeOffsetX + column, offset.ShapeOffsetY + row };
                    if (!bounds(p))
                        throw std::out_of_range("Source obstacle footprint outside map");
                    auto& cell = map(c, cell_id(p));
                    (void)editor.command(c, cell.base, 0);
                    refresh_world_cell_render_state(c, &cell.base);
                    (void)editor.command(c, cell.base, code);
                    refresh_world_cell_render_state(c, &cell.base);
                    if (code) {
                        cell.is_obstacle = 1;
                        set_world_cell_parent_id_unchecked(&cell.base, parent);
                    }
                }
        }
    }
    c.ports.meshes.rebuild_terrain_cell_shading(c);
    world_operations::refresh_active_park_object_meshes(c);
    editor.set_mode(c, 0, false);
    c.construction.park_object = nullptr;
    c.view.secondary_cursor_record_snapshot = nullptr;
}
} //namespace simcoaster
