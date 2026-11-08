#pragma once

namespace simcoaster {
struct WorldRuntime;
enum class WorldDomain {
    visitors,
    staff,
    attractions,
    economy,
    research,
    missions,
    scripts,
    weather,
    advisors,
    construction
};
template <WorldDomain, bool>
struct WorldDependencies;
using VisitorWorld = WorldDependencies<WorldDomain::visitors, false>;
using VisitorQuery = WorldDependencies<WorldDomain::visitors, true>;
using StaffWorld = WorldDependencies<WorldDomain::staff, false>;
using StaffQuery = WorldDependencies<WorldDomain::staff, true>;
using AttractionWorld = WorldDependencies<WorldDomain::attractions, false>;
using AttractionQuery = WorldDependencies<WorldDomain::attractions, true>;
using EconomyWorld = WorldDependencies<WorldDomain::economy, false>;
using EconomyQuery = WorldDependencies<WorldDomain::economy, true>;
using ResearchWorld = WorldDependencies<WorldDomain::research, false>;
using ResearchQuery = WorldDependencies<WorldDomain::research, true>;
using MissionWorld = WorldDependencies<WorldDomain::missions, false>;
using MissionQuery = WorldDependencies<WorldDomain::missions, true>;
using ScriptWorld = WorldDependencies<WorldDomain::scripts, false>;
using ScriptQuery = WorldDependencies<WorldDomain::scripts, true>;
using WeatherWorld = WorldDependencies<WorldDomain::weather, false>;
using WeatherQuery = WorldDependencies<WorldDomain::weather, true>;
using AdvisorWorld = WorldDependencies<WorldDomain::advisors, false>;
using AdvisorQuery = WorldDependencies<WorldDomain::advisors, true>;
using ConstructionWorld = WorldDependencies<WorldDomain::construction, false>;
using ConstructionQuery = WorldDependencies<WorldDomain::construction, true>;
} //namespace simcoaster
