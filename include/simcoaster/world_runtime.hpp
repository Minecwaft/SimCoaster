#pragma once
#include "world_dependencies.hpp"

namespace simcoaster {
//World owns domain state; stable cached views pass only each subsystem's
//declared references. Views allocate nothing and share the same owner
//identity.
struct WorldRuntime : WorldStateOwners {
    template <class Backend>
    explicit WorldRuntime(Backend& backend, EntityOwnership ownership = EntityOwnership::allocator)
        : WorldStateOwners(backend, ownership) {}

    explicit WorldRuntime(WorldPorts bindings, EntityOwnership ownership = EntityOwnership::allocator)
        : WorldStateOwners(bindings, ownership) {}

    WorldRuntime(const WorldRuntime&) = delete;
    WorldRuntime& operator=(const WorldRuntime&) = delete;
    WorldRuntime(WorldRuntime&&) = delete;
    WorldRuntime& operator=(WorldRuntime&&) = delete;
    VisitorWorld visitors_dependencies{ *this, *this };
    VisitorQuery visitors_query{ *this, *this };
    StaffWorld staff_dependencies{ *this, *this };
    StaffQuery staff_query{ *this, *this };
    AttractionWorld attractions_dependencies{ *this, *this };
    AttractionQuery attractions_query{ *this, *this };
    EconomyWorld economy_dependencies{ *this, *this };
    EconomyQuery economy_query{ *this, *this };
    ResearchWorld research_dependencies{ *this, *this };
    ResearchQuery research_query{ *this, *this };
    MissionWorld missions_dependencies{ *this, *this };
    MissionQuery missions_query{ *this, *this };
    ScriptWorld scripts_dependencies{ *this, *this };
    ScriptQuery scripts_query{ *this, *this };
    WeatherWorld weather_dependencies{ *this, *this };
    WeatherQuery weather_query{ *this, *this };
    AdvisorWorld advisors_dependencies{ *this, *this };
    AdvisorQuery advisors_query{ *this, *this };
    ConstructionWorld construction_dependencies{ *this, *this };
    ConstructionQuery construction_query{ *this, *this };

    template <WorldDependency Dependency>
    operator Dependency&() {
        return world_dependencies<Dependency>(*this);
    }

    template <WorldDependency Dependency>
    operator const Dependency&() const
        requires(Dependency::readonly) {
        return world_dependencies<Dependency>(*this);
    }
};

namespace world_dependency_detail {
template <class Dependency>
consteval std::meta::info member() {
    for (auto field : std::meta::nonstatic_data_members_of(^^WorldRuntime, std::meta::access_context::current()))
        if (std::meta::dealias(std::meta::type_of(field)) == ^^Dependency)
            return field;
    throw "world dependency has no cached owner view";
}
} //namespace world_dependency_detail

template <class Dependency>
Dependency& world_dependencies(WorldRuntime& owner) {
    return owner.[:world_dependency_detail::member<std::remove_cv_t<Dependency>>():];
}

template <class Dependency>
const Dependency& world_dependencies(const WorldRuntime& owner) {
    static_assert(Dependency::readonly, "a const World cannot provide mutable state");
    return owner.[:world_dependency_detail::member<std::remove_cv_t<Dependency>>():];
}
} //namespace simcoaster
