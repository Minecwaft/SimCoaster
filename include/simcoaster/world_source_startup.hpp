#pragma once
#include "standard_balance.hpp"
#include "world_startup.hpp"

namespace simcoaster {
class ParkConstructionRuntime;

//4288F0 publishes these four Costs fields to the cell command system after
//the shared Standard.sam and selected source's overrides have been loaded.
struct WorldSourceStartupBalance {
    WorldStartupBalance world;
    WorldCellConstructionCosts cell_costs;
    std::int32_t initial_cash {}, initial_admission_fee {};
    std::array<BankLoanOffer, 10> loan_offers {};
};

std::expected<void, sam::Error> load_world_source_startup_balance(WorldSourceStartupBalance&, std::string_view, bool preserve);
//Fresh source-world setup,648070. Assets, the selected mission/profile and
//catalog templates must be bound; the World must have no linked records.
//Base.map status bytes are applied afterward, as6A377B ->6A378A does.
void initialize_world_runtime_state(WorldRuntime&, World&, const WorldSourceStartupBalance&);
//Post-Base.map672EB0 setup and source3's65DA60 cell-command pass.
void initialize_world_source_interaction(WorldRuntime&, World&, ParkConstructionRuntime&);
//Default source population,647BF0; never used on a restored park.
void populate_world_from_object_definition_catalog(WorldRuntime&, World&, ParkConstructionRuntime&);
} //namespace simcoaster
