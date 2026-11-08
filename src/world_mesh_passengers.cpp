#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/resource_variant.hpp"
#include "simcoaster/simulation_sine.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;

[[noreturn]] void invalid(std::size_t index, std::string detail) {
    throw MeshError { MeshFailure::configuration, index, "Mesh passengers: " + std::move(detail), {} };
}

void require(bool condition, std::size_t index, std::string_view detail) {
    if (!condition) {
        invalid(index, std::string { detail });
    }
}

std::uint32_t head_program() {
    return resource_program_entry({ ResourceProgramFamily::attached_head, 0 }).index;
}

bool head_resource(const ResourceVariant& resource) {
    const auto category = resource.values[10].bits;
    return resource.entry == head_program() && resource.values[12].bits == 0 && (category == 3 || category == 4 || category == 5 || category == 7);
}

MeshMatrix multiply(const MeshMatrix& left, const MeshMatrix& right) {
    auto result = right;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            double value = row == 3 ? right[12 + column] : 0.;
            for (std::size_t k = 0; k < 3; ++k) {
                value += static_cast<double>(left[row * 4 + k]) * right[k * 4 + column];
            }
            result[row * 4 + column] = static_cast<float>(value);
        }
    }
    return result;
}

float sine(double radians) {
    //Native49B140 uses FISTP with the game's nearest-even lookup convention.
    return simulation_sine_at_rounded_index(static_cast<float>(radians * simulation_sine_samples_per_radian));
}

struct HeadProjection {
    std::uint32_t frame {};
    float roll_turns {};
};

HeadProjection project_head(const MeshMatrix& child, const MeshMatrix& camera_to_world) {
    //4DBDE0: relative = camera_to_world * inverse(child). In particular this
    //uses the camera's actual eye/roll, not the normal World yaw controls.
    const auto relative = multiply(camera_to_world, mesh_transform::inverse(child));
    const auto distance = std::sqrt(static_cast<double>(relative[12]) * relative[12] + static_cast<double>(relative[13]) * relative[13] + static_cast<double>(relative[14]) * relative[14]);
    Point view {};
    if (distance > 0) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            view[axis] = static_cast<float>(relative[12 + axis] / distance);
        }
    }
    Point candidate { 0, 1, 0 }, best = candidate;
    float best_score = 0;
    std::uint32_t vertical = 0, horizontal = 0;
    const auto turn_sine = sine(-.78539819), turn_cosine = sine(.78539819);
    const auto tilt_sine = sine(.52359879), tilt_cosine = sine(2.0943952);
    for (std::uint32_t tilt = 0; tilt < 7; ++tilt) {
        const float vertical_score = view[1] * candidate[1];
        for (std::uint32_t turn = 0; turn < 8; ++turn) {
            const auto score = static_cast<double>(view[2]) * candidate[2] + static_cast<double>(view[0]) * candidate[0] + vertical_score;
            if (score > best_score) {
                best_score = static_cast<float>(score);
                best = candidate;
                vertical = tilt;
                horizontal = turn;
            }
            const auto x = candidate[0];
            candidate[0] = static_cast<float>(static_cast<double>(turn_sine) * candidate[2] + static_cast<double>(turn_cosine) * x);
            candidate[2] = static_cast<float>(static_cast<double>(turn_cosine) * candidate[2] - static_cast<double>(x) * turn_sine);
        }
        const auto y = candidate[1];
        candidate[1] = static_cast<float>(static_cast<double>(tilt_cosine) * y - static_cast<double>(tilt_sine) * candidate[2]);
        candidate[2] = static_cast<float>(static_cast<double>(y) * tilt_sine + static_cast<double>(tilt_cosine) * candidate[2]);
    }
    if (vertical == 0 || vertical == 6) {
        horizontal = 0;
    }
    //At coincident camera/seat positions native leaves best_direction unset.
    //Keep the defined pole basis above rather than propagating stack garbage.
    Point up { 0, sine(static_cast<double>(vertical) * .52359879), -sine(static_cast<double>(vertical) * .52359879 + 1.5707964) };
    const auto yaw = static_cast<double>(horizontal) * -.78539819;
    const auto old_z = up[2];
    up[0] = sine(yaw) * old_z;
    up[2] = sine(yaw + 1.5707964) * old_z;
    const Point right { best[2] * up[1] - best[1] * up[2], best[0] * up[2] - best[2] * up[0], best[1] * up[0] - best[0] * up[1] };
    auto facing = mesh_identity_matrix;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        facing[axis] = right[axis];
        facing[4 + axis] = up[axis];
        facing[8 + axis] = best[axis];
    }
    //4DBFC0's destination is its SECOND argument, so this order is intentional.
    const auto residual = multiply(relative, mesh_transform::inverse(facing));
    const auto roll = std::atan2(-static_cast<double>(residual[4]), static_cast<double>(residual[5]));
    return { horizontal + 8 * vertical, static_cast<float>(roll * .15915494) };
}
} //namespace

std::expected<void, MeshError> WorldMeshRuntime::restore_passenger_resources(WorldRuntime& c, WorldMeshInstance& instance) {
    try {
        //Resource pool restoration precedes mesh restoration in the native wire
        //stream. Never adopt an old numeric handle before that pool is present.
        instance.passenger_resources.assign(instance.embedded.size(), {});
        for (std::size_t i = 0; i < instance.embedded.size(); ++i) {
            const auto& entry = instance.embedded[i];
            if (!(entry.flags & 2u) || !(instance.playback.model.resources.at(i).flags & 0x80u)) {
                continue;
            }
            const auto* resource = find_resource_variant(c, entry.resource_pool_handle);
            require(resource && resource->generation && resource->handle == entry.resource_pool_handle && head_resource(*resource), i, "saved head handle has no matching attached-head resource");
            for (const auto& owner : slots_) {
                if (owner && owner.get() != &instance) {
                    for (const auto& lease : owner->passenger_resources) {
                        require(lease.handle != entry.resource_pool_handle || lease.generation != resource->generation, i, "saved head resource has multiple mesh owners");
                    }
                }
            }
            for (std::size_t previous = 0; previous < i; ++previous) {
                require(instance.passenger_resources[previous].handle != entry.resource_pool_handle, i, "saved head resource has multiple seats");
            }
            instance.passenger_resources[i] = { entry.resource_pool_handle, resource->generation };
        }
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<void, MeshError> WorldMeshRuntime::prepare_passenger_sprites(WorldRuntime& c, const MeshMatrix& camera_to_world) {
    try {
        require(!restoring_, 0, "passenger draw during incomplete mesh restore");
        for (const auto value : camera_to_world) {
            require(std::isfinite(value), 0, "nonfinite render camera");
        }
        //The global sprite pool otherwise admits stale/offscreen mesh-head
        //commands independently of mesh visibility. Only a currently owned,
        //prepared seat can publish a frame. Do not run its program or change its
        //62ms deadline/render_enabled state here.
        for (auto& resource : c.resources.variants.slots) {
            if (resource && head_resource(*resource)) {
                resource->values[13].bits = std::numeric_limits<std::uint32_t>::max();
            }
        }
        std::set<std::uint32_t> owned;
        for (std::size_t slot = 1; slot < slots_.size(); ++slot) {
            auto* instance = slots_[slot].get();
            if (!instance || !(instance->embedded_flags & 4u)) {
                continue;
            }
            require(instance->passenger_resources.size() == instance->embedded.size(), slot, "head lease table disagrees with mesh resources");
            const auto& model = instance->playback.model;
            const auto poses = evaluate_mesh_pose(model);
            if (!poses) {
                return std::unexpected(poses.error());
            }
            for (std::size_t i = 0; i < instance->embedded.size(); ++i) {
                const auto& entry = instance->embedded[i];
                if (!(entry.flags & 2u) || !(model.resources.at(i).flags & 0x80u)) {
                    continue;
                }
                const auto& lease = instance->passenger_resources[i];
                auto* resource = find_resource_variant(c, entry.resource_pool_handle);
                require(resource && lease.handle == entry.resource_pool_handle && lease.generation == resource->generation && head_resource(*resource), i, "head resource lease expired or changed program");
                require(owned.insert(lease.handle).second, i, "head resource has multiple mesh owners");
                const auto& node = model.nodes.at(model.resources[i].node_index);
                if (!instance->playback.registered || (node.flags & 0x8000u)) {
                    continue;
                }
                auto attached = mesh_attachment_transform(model, *poses, i);
                if (!attached) {
                    return std::unexpected(attached.error());
                }
                //Native flips the sampled auxiliary child in place. Render/pick can
                //prepare the same frame twice on x64, so mutate only this copy.
                if (node.flags & 0x400u) {
                    for (const auto component : { 0u, 1u, 2u, 8u, 9u, 10u }) {
                        (*attached)[component] = -(*attached)[component];
                    }
                }
                const auto projected = project_head(*attached, camera_to_world);
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    resource->values[axis + 1].floating((*attached)[12 + axis]);
                }
                resource->values[15].bits = 0;
                resource->values[13].bits = projected.frame;
                resource->values[9].floating(projected.roll_turns);
                resource->values[8].floating(.6850000023841858f); //Native9AEAA8.
            }
        }
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
