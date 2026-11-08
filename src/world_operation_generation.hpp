#pragma once
#include "simcoaster/world.hpp"

namespace simcoaster::world_operation_detail {
//Production emits unconditional direct calls. Tests instantiate the same
//definitions with explicit failure/ordering probes, without production checks.
consteval std::meta::token_sequence definitions(bool probes) {
    std::meta::token_sequence result;
    for (auto function : functions) {
        const auto declaration = std::meta::set_name(std::meta::declaration_of(function), std::meta::identifier_of(function));
        const auto arguments = std::meta::argument_list_for(declaration, { .forward = true });
        std::meta::token_sequence probe;
        if (probes) {
            const auto name = std::meta::id(std::meta::identifier_of(function));
            if (lifecycle_hook(function)) {
                probe = ^^{
                    if (p0.ports.operations.${name})
                        return p0.ports.operations.${name}(p0.ports.operations.target, ${arguments});
                };
            } else {
                probe = ^^{
                    if (const auto* probes = p0.ports.operations.probes; probes && probes->${name})
                        return probes->${name}(p0.ports.operations.target, ${arguments});
                };
            }
        }
        result += ^^{
            ${declaration} {
                ${probe} return ${function}(${arguments});
            }
        };
    }
    return result;
}
} //namespace simcoaster::world_operation_detail
