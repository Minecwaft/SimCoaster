#pragma once
#include "world_domains_fwd.hpp"
#include "world_state.hpp"
#include <meta>
#include <type_traits>
#include <utility>

namespace simcoaster {
namespace world_dependency_detail {
consteval std::vector<std::meta::info> fields(WorldDomain domain) {
    match (domain) {
        case WorldDomain::visitors => {
            return { ^^WorldStateOwners::ports, ^^WorldStateOwners::resources, ^^WorldStateOwners::entities, ^^WorldStateOwners::simulation, ^^WorldStateOwners::view, ^^WorldStateOwners::visitors, ^^WorldStateOwners::advisor, ^^WorldStateOwners::mode, ^^WorldStateOwners::construction, ^^WorldStateOwners::timing };
        }
        case WorldDomain::staff => {
            return { ^^WorldStateOwners::ports, ^^WorldStateOwners::resources, ^^WorldStateOwners::entities, ^^WorldStateOwners::simulation, ^^WorldStateOwners::messages, ^^WorldStateOwners::view, ^^WorldStateOwners::attractions, ^^WorldStateOwners::visitors, ^^WorldStateOwners::mode, ^^WorldStateOwners::construction, ^^WorldStateOwners::ui, ^^WorldStateOwners::staff, ^^WorldStateOwners::source };
        }
        case WorldDomain::attractions => {
            return { ^^WorldStateOwners::ports, ^^WorldStateOwners::resources, ^^WorldStateOwners::weather, ^^WorldStateOwners::entities, ^^WorldStateOwners::simulation, ^^WorldStateOwners::view, ^^WorldStateOwners::attractions, ^^WorldStateOwners::visitors, ^^WorldStateOwners::mode, ^^WorldStateOwners::construction, ^^WorldStateOwners::staff, ^^WorldStateOwners::random };
        }
        case WorldDomain::economy => {
            return { ^^WorldStateOwners::ports, ^^WorldStateOwners::resources, ^^WorldStateOwners::balance, ^^WorldStateOwners::entities, ^^WorldStateOwners::simulation, ^^WorldStateOwners::finance, ^^WorldStateOwners::messages, ^^WorldStateOwners::advisor, ^^WorldStateOwners::mode, ^^WorldStateOwners::ui };
        }
        case WorldDomain::research => {
            return { ^^WorldStateOwners::ports, ^^WorldStateOwners::resources, ^^WorldStateOwners::entities, ^^WorldStateOwners::simulation, ^^WorldStateOwners::staff, ^^WorldStateOwners::research, ^^WorldStateOwners::random };
        }
        case WorldDomain::missions => {
            return { ^^WorldStateOwners::ports, ^^WorldStateOwners::resources,    ^^WorldStateOwners::entities, ^^WorldStateOwners::simulation, ^^WorldStateOwners::missions, ^^WorldStateOwners::finance, ^^WorldStateOwners::messages, ^^WorldStateOwners::view,      ^^WorldStateOwners::advisor,
                     ^^WorldStateOwners::mode,  ^^WorldStateOwners::construction, ^^WorldStateOwners::timing,   ^^WorldStateOwners::animation,  ^^WorldStateOwners::ui,       ^^WorldStateOwners::staff,   ^^WorldStateOwners::source,   ^^WorldStateOwners::challenges };
        }
        case WorldDomain::scripts => {
            return { ^^WorldStateOwners::ports, ^^WorldStateOwners::resources, ^^WorldStateOwners::scripts, ^^WorldStateOwners::weather, ^^WorldStateOwners::balance, ^^WorldStateOwners::entities, ^^WorldStateOwners::simulation, ^^WorldStateOwners::visitors };
        }
        case WorldDomain::weather => {
            return { ^^WorldStateOwners::ports, ^^WorldStateOwners::resources, ^^WorldStateOwners::weather, ^^WorldStateOwners::balance, ^^WorldStateOwners::entities, ^^WorldStateOwners::simulation, ^^WorldStateOwners::view, ^^WorldStateOwners::source };
        }
        case WorldDomain::advisors => {
            return { ^^WorldStateOwners::ports, ^^WorldStateOwners::resources, ^^WorldStateOwners::entities, ^^WorldStateOwners::simulation, ^^WorldStateOwners::visitors, ^^WorldStateOwners::advisor, ^^WorldStateOwners::mode, ^^WorldStateOwners::construction, ^^WorldStateOwners::timing, ^^WorldStateOwners::ui, ^^WorldStateOwners::staff, ^^WorldStateOwners::source };
        }
        case WorldDomain::construction => {
            return { ^^WorldStateOwners::ports, ^^WorldStateOwners::resources, ^^WorldStateOwners::weather, ^^WorldStateOwners::balance, ^^WorldStateOwners::entities, ^^WorldStateOwners::simulation, ^^WorldStateOwners::view, ^^WorldStateOwners::attractions, ^^WorldStateOwners::visitors, ^^WorldStateOwners::mode, ^^WorldStateOwners::construction, ^^WorldStateOwners::staff, ^^WorldStateOwners::messages, ^^WorldStateOwners::ui };
        }
        case _ => {
        }
    } throw "unknown world dependency domain";
}
} //namespace world_dependency_detail

template <class T>
concept WorldDependency = requires {
    T::readonly;
    typename T::world_dependency_marker;
};
template <class Dependency>
Dependency& world_dependencies(WorldRuntime&);
template <class Dependency>
const Dependency& world_dependencies(const WorldRuntime&);

//State references are explicit and const-correct. The owner identity only
//coordinates cross-system calls and existing external callback boundaries.
template <WorldDomain Domain, bool Readonly>
struct WorldDependencies {
    using world_dependency_marker = void;
    static constexpr bool readonly = Readonly;
    using State = std::conditional_t<Readonly, const WorldStateOwners, WorldStateOwners>;
    using Owner = std::conditional_t<Readonly, const WorldRuntime, WorldRuntime>;

private:
    Owner& owner_;

public:
    consteval {
        std::meta::token_sequence declarations;
        std::meta::token_sequence initializers = ^^{:owner_(owner)};
        std::vector<std::meta::info> visited;
        for (auto member : world_dependency_detail::fields(Domain)) {
            if (std::ranges::find(visited, member) != visited.end()) {
                throw "duplicate world dependency";
            }
            visited.push_back(member);
            auto type = std::meta::type_of(member);
            if (Readonly) {
                type = std::meta::add_const(type);
            }
            const auto reference = std::meta::add_lvalue_reference(type);
            const auto name = std::meta::id(std::meta::identifier_of(member));
            declarations += ^^{ ${reference} ${name}; };
            initializers += ^^{ , ${name}(state.${name}) };
        }
        std::meta::queue_injection(^^{
            public:
            ${declarations} WorldDependencies(Owner& owner, State& state) ${initializers} {}

            World* referenced_world() const {
                return simulation.reference;
            }
        });
    }

    WorldRuntime& owner()
        requires(!Readonly) {
        return owner_;
    }

    const WorldRuntime& owner() const {
        return owner_;
    }

    operator WorldRuntime&()
        requires(!Readonly) {
        return owner_;
    }

    operator const WorldRuntime&() const {
        return owner_;
    }

    template <WorldDependency Other>
    operator Other&()
        requires(!Readonly) {
        return world_dependencies<Other>(owner_);
    }

    template <WorldDependency Other>
    operator const Other&() const
        requires(Other::readonly) {
        return world_dependencies<Other>(owner_);
    }
};

inline WorldRuntime& world_owner(WorldRuntime& owner) {
    return owner;
}

inline const WorldRuntime& world_owner(const WorldRuntime& owner) {
    return owner;
}

template <WorldDomain Domain, bool Readonly>
decltype(auto) world_owner(WorldDependencies<Domain, Readonly>& world) {
    return world.owner();
}

template <WorldDomain Domain, bool Readonly>
const WorldRuntime& world_owner(const WorldDependencies<Domain, Readonly>& world) {
    return world.owner();
}
} //namespace simcoaster
