#include "coaster_editor_internal.hpp"
#include "simcoaster/world_save.hpp"

namespace simcoaster {
namespace {
using namespace coaster_editor_detail;

std::optional<CoasterParameterRange>
parameter_range(const WorldCoasterRuntime& world, std::size_t r, std::size_t index, CoasterParameter parameter) {
    const auto& binding = world.bindings.at(r);
    require(index < binding.geometry.size(), index);
    const auto& local = binding.geometry_state[index];
    const auto& owner = world.placements.at(local.placement);
    if ((owner.flags & 2u) || !owner.definition)
        return std::nullopt;
    const auto& input = binding.geometry[index];
    const auto& data = pylon(owner);
    const auto limits = rules(owner);
    match (parameter) {
        case CoasterParameter::loft => {
            if (!(data.flags & 0x20u))
                return std::nullopt;
            const auto span = static_cast<double>(data.height_maximum) - data.height_minimum;
            require(span != 0, index);
            //485E76 uses normalized absolute minimum;485ECB normalizes the delta
            //between the stack's current total and its SAM maximum.
            auto total = 0.0;
            for (std::size_t n = 0; n < owner.links.size(); ++n) {
                const auto ref = owner.links[n];
                require(ref.has_value(), n);
                total +=
                    span * world.bindings[ref->ride].geometry_state.at(ref->node).controls[0] + data.height_minimum;
                if (n + 1 < owner.links.size())
                    total += data.height_offset;
            }
            return CoasterParameterRange{
                local.controls[0],
                static_cast<float>(std::max(0.0, (limits.minimum_height - data.height_minimum) / span)),
                static_cast<float>((limits.maximum_height - total) / span + local.controls[0]),
                0.0049999999f
            };
        }
        case CoasterParameter::heading => {
            if (!(data.flags & 0x40u))
                return std::nullopt;
            return CoasterParameterRange{ local.angles[3],
                                          local.angles[0] - limits.maximum_rotation_deviation,
                                          local.angles[0] + limits.maximum_rotation_deviation,
                                          0.02f };
        }
        case CoasterParameter::bank => {
            if ((input.geometry_flags & 0x40u) || !(data.flags & 0x100u))
                return std::nullopt;
            return CoasterParameterRange{ local.angles[1], -3.1414928f, 3.1414928f, 0.02f };
        }
        case CoasterParameter::incline => {
            //The original byte TEST at4865C2 checks80h; the signed enum condition
            //emitted by the old decompiler does not mean the DWORD sign bit.
            if ((input.geometry_flags & 0x40u) || !(data.flags & 0x80u))
                return std::nullopt;
            return CoasterParameterRange{ local.angles[2], -3.1414928f, 3.1414928f, 0.02f };
        }
        case CoasterParameter::loop_rotation => {
            if (!(input.geometry_flags & 0x40u) || !(data.flags & 0x200u))
                return std::nullopt;
            const bool first = (input.geometry_flags & 0x80u) != 0;
            auto value = local.controls[4];
            if (first && value > 0.5f)
                value -= 1;
            return CoasterParameterRange{ value,
                                          first ? -0.34999999f : 0.15000001f,
                                          first ? 0.34999999f : 0.85000002f,
                                          0.0099999998f };
        }
        case _ => {}
    }
    require(false);
    return {};
}

template <class... Payload>
void record(AttractionWorld& c, std::byte event, const Payload&... payload) {
    if (c.construction.action_recording_disabled || !c.construction.action_record_streams_initialized)
        return;
    require(c.construction.action_record_stream != nullptr);
    const std::array prefix{ event };
    (void)c.construction.action_record_stream->write(prefix);
    ((void)c.construction.action_record_stream->write(save::detail::encode(payload)), ...);
}
} //namespace

std::expected<std::optional<CoasterParameterRange>, MeshError>
coaster_node_parameter_range(const WorldMeshRuntime& meshes,
                             std::uint32_t map,
                             std::size_t index,
                             CoasterParameter parameter) {
    try {
        const auto* world = meshes.coasters();
        require(world != nullptr);
        const auto* binding = world->find_map(map);
        require(binding != nullptr, map);
        return parameter_range(*world, static_cast<std::size_t>(binding - world->bindings.data()), index, parameter);
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<void, MeshError> edit_coaster_node_parameter(AttractionWorld& c,
                                                           WorldMeshRuntime& meshes,
                                                           std::uint32_t map,
                                                           std::size_t index,
                                                           CoasterParameter parameter,
                                                           float requested,
                                                           bool skip_camera_fit) {
    try {
        require(std::isfinite(requested), index);
        EditTransaction edit{ c, meshes };
        const auto r = ride_index(edit.world, map);
        auto& binding = edit.world.bindings[r];
        const auto range = parameter_range(edit.world, r, index, parameter);
        require(range.has_value(), index);
        const auto camera_point =
            parameter == CoasterParameter::loft && !skip_camera_fit && !c.construction.action_recording_disabled
                ? std::optional{ profile_position(meshes, edit.world, binding.geometry_state[index]) }
                : std::nullopt;
        if (!c.construction.action_recording_disabled && c.construction.action_record_streams_initialized)
            require(c.construction.action_record_stream != nullptr);
        auto* bank = parameter == CoasterParameter::loft ? editing_bank(c) : nullptr;
        const auto original_cost =
            parameter == CoasterParameter::loft ? truncated_cost(piece_cost(edit.world, r, index, false, false)) : 0;
        //488396..4883C8 tests the lower bound first, then the upper bound. This
        //also preserves its behavior for a restored stack with inverted limits.
        const auto value = requested < range->minimum ? range->minimum
                         : requested > range->maximum ? range->maximum
                                                      : requested;
        auto& local = binding.geometry_state[index];
        auto& flags = binding.geometry[index].geometry_flags;
        match (parameter) {
            case CoasterParameter::loft => {
                local.controls[0] = value;
                const auto& owner = edit.world.placements[local.placement];
                const auto& data = pylon(owner);
                //This command explicitly selects its node; the raw loading setter's
                //implicit current-node contract is not restricted to the graph tail.
                if (!(data.flags & 0x4000u))
                    binding.default_attachment_height = static_cast<float>(
                        (static_cast<double>(data.height_maximum) - data.height_minimum) * std::clamp(value, 0.0f, 1.0f)
                        + data.height_minimum);
            }
            case CoasterParameter::heading => {
                local.angles[3] = value;
                flags |= 1u;
            }
            case CoasterParameter::bank => {
                local.angles[1] = value;
                flags |= 4u;
            }
            case CoasterParameter::incline => {
                local.angles[2] = value;
                flags |= 2u;
            }
            case CoasterParameter::loop_rotation => local.controls[4] = value;
            case _ => {}
        }
        binding.train_state.runtime_flags &= ~0x2000u;
        mark_owner(edit.world, edit.world.placements[local.placement]);
        finish(edit, r);
        const auto new_cost =
            bank ? rounded_cost(static_cast<float>(piece_cost(edit.world, r, index, false, false))) : 0;
        edit.commit();
        const auto ordinal = static_cast<std::uint16_t>(index);
        match (parameter) {
            case CoasterParameter::loft => {
                record(c, std::byte{ 0x39 }, local.controls[0], ordinal);
                if (flags & 0x40u) {
                    const auto adjacent = (flags & 0x80u) ? index + 1 : index ? index - 1 : binding.geometry.size();
                    if (adjacent < binding.geometry.size())
                        record(c,
                               std::byte{ 0x39 },
                               binding.geometry_state[adjacent].controls[0],
                               static_cast<std::uint16_t>(adjacent));
                }
                settle_cost(c, bank, original_cost, new_cost);
            }
            case CoasterParameter::heading => record(c, std::byte{ 0x3A }, local.angles[3], ordinal);
            case CoasterParameter::bank or CoasterParameter::incline => {
                record(c, std::byte{ 0x3B }, local.angles[1], local.angles[2], ordinal);
            }
            case CoasterParameter::loop_rotation => record(c, std::byte{ 0x3B }, local.controls[4], 0.0f, ordinal);
            case _ => {}
        }
        if (camera_point)
            fit_world_view_camera_to_coaster_attachment(c, *camera_point);
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
