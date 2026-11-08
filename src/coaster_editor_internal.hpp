#pragma once
#include "simcoaster/world_domains_fwd.hpp"
#include "simcoaster/coaster_construction.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <source_location>
#include <string_view>

namespace simcoaster::coaster_editor_detail {
inline void require(bool value, std::size_t index = 0, std::source_location where = std::source_location::current()) {
    if (!value) {
        const std::string_view source { where.file_name() };
        const auto separator = source.find_last_of("/\\");
        const auto filename = separator == std::string_view::npos ? source : source.substr(separator + 1);
        throw MeshError { MeshFailure::configuration, index, "Coaster editor " + std::string { filename } + ":" + std::to_string(where.line()), {} };
    }
}

inline WorldCoasterRuntime& runtime(WorldMeshRuntime& meshes) {
    auto* value = meshes.coasters();
    require(value != nullptr);
    return *value;
}

inline std::size_t ride_index(WorldCoasterRuntime& world, std::uint32_t map) {
    const auto found = std::ranges::find(world.bindings, map, &WorldCoasterBinding::map_id);
    require(found != world.bindings.end(), map);
    return static_cast<std::size_t>(found - world.bindings.begin());
}

inline CoasterGeometryObjectInput& node(WorldCoasterRuntime& world, CoasterNodeReference ref) {
    return world.bindings.at(ref.ride).geometry.at(ref.node);
}

inline CoasterNodeState& state(WorldCoasterRuntime& world, CoasterNodeReference ref) {
    return world.bindings.at(ref.ride).geometry_state.at(ref.node);
}

inline const CoasterPylonRuntime& pylon(const CoasterPlacement& owner) {
    require(owner.definition != nullptr);
    return owner.definition->pylons.at(owner.pylon);
}

inline void set_explicit_owner_position(AttractionWorld& c, CoasterPlacement& owner, const std::array<float, 3>& point) {
    const auto* terrain = c.construction.active_terrain_model_data ? c.construction.active_terrain_model_data->terrain_grid : nullptr;
    require(terrain && terrain->width_cells && terrain->height_cells);
    //Allocator47174C copies the explicit point and derives/clamps the tile;
    //the tile arguments supplied by475090/476E10 are ignored on this branch.
    owner.position = point;
    owner.tile_x = std::clamp(static_cast<std::int32_t>(static_cast<double>(point[0]) * static_cast<double>(0.1f)), 0, static_cast<std::int32_t>(terrain->width_cells) - 1);
    owner.tile_y = std::clamp(static_cast<std::int32_t>(static_cast<double>(point[2]) * static_cast<double>(0.1f)), 0, static_cast<std::int32_t>(terrain->height_cells) - 1);
}

inline std::array<float, 3> profile_position(WorldMeshRuntime& meshes, const WorldCoasterRuntime& world, const CoasterNodeState& local) {
    const auto& data = pylon(world.placements.at(local.placement));
    require(data.profile_resource.has_value());
    const auto* live = meshes.instance(local.mesh_slot);
    require(live != nullptr, local.mesh_slot);
    const auto poses = evaluate_mesh_pose(live->playback.model);
    if (!poses) {
        throw poses.error();
    }
    const auto transform = mesh_attachment_transform(live->playback.model, *poses, *data.profile_resource);
    if (!transform) {
        throw transform.error();
    }
    return { (*transform)[12], (*transform)[13], (*transform)[14] };
}

inline CoasterPlacementRules rules(const CoasterPlacement& owner) {
    require(owner.definition != nullptr);
    return coaster_placement_rules(owner.definition->balance.pylons.at(owner.pylon), owner.pylon != 0);
}

inline std::array<std::int32_t, 2> tile(const CoasterPlacement& owner) {
    return { owner.tile_x, owner.tile_y };
}

inline void validate_top_level_terrain(const AttractionQuery& c, std::int32_t x, std::int32_t y) {
    const auto* terrain = c.construction.active_terrain_model_data ? c.construction.active_terrain_model_data->terrain_grid : nullptr;
    require(terrain && x >= 0 && y >= 0 && static_cast<std::uint32_t>(x) < terrain->width_cells && static_cast<std::uint32_t>(y) < terrain->height_cells);
    const auto cell = static_cast<std::size_t>(y) * terrain->width_cells + static_cast<std::uint32_t>(x);
    require(cell < c.construction.original_terrain_cells.size(), cell);
    //473E62 reads the original terrain snapshot only on its top-level path.
    //COS loading and explicit recursive neighbor checks bypass this flag test.
    require(!(std::to_underlying(c.construction.original_terrain_cells[cell].flags) & 1u), cell);
}

inline std::size_t level(const CoasterPlacement& owner, CoasterNodeReference ref) {
    for (std::size_t i = 0; i < owner.links.size(); ++i) {
        if (owner.links[i] && owner.links[i]->ride == ref.ride && owner.links[i]->node == ref.node) {
            return i;
        }
    }
    require(false, ref.node);
    return 0;
}

inline double cumulative(WorldCoasterRuntime& world, const CoasterPlacement& owner, std::size_t end) {
    const auto& data = pylon(owner);
    double height = 0;
    for (std::size_t i = 0; i <= end; ++i) {
        if (const auto ref = owner.links.at(i)) {
            height += (static_cast<double>(data.height_maximum) - data.height_minimum) * state(world, *ref).controls[0] + data.height_minimum + (i < end ? data.height_offset : 0);
        }
    }
    return height;
}

inline void set_height(WorldCoasterRuntime& world, CoasterPlacement& owner, std::size_t index, double requested, bool bypass = false) {
    const auto& data = pylon(owner);
    if (index) {
        requested -= cumulative(world, owner, index - 1) + data.height_offset;
    }
    if (!bypass) {
        const auto limits = rules(owner);
        requested = std::clamp(requested, static_cast<double>(limits.minimum_height), static_cast<double>(limits.maximum_height));
    }
    require(data.height_maximum != data.height_minimum);
    state(world, *owner.links.at(index)).controls[0] = static_cast<float>(std::clamp((requested - data.height_minimum) / (data.height_maximum - data.height_minimum), 0.0, 1.0));
}

inline void mark(WorldCoasterBinding& binding, std::size_t index) {
    binding.geometry.at(index).geometry_flags |= 0x200000u;
    if (index) {
        binding.geometry[index - 1].geometry_flags |= 0x200000u;
    }
    binding.geometry_dirty = true;
    binding.train_state.runtime_flags |= 0x1001u;
}

inline void mark_owner(WorldCoasterRuntime& world, const CoasterPlacement& owner) {
    //471A50 refreshes every linked mesh on this support, marking each node and
    //its predecessor. Raising a lower link also moves the upper links' bounds.
    for (const auto& ref : owner.links) {
        if (ref) {
            mark(world.bindings.at(ref->ride), ref->node);
        }
    }
}

inline const CoasterPlacement* previous_placement_owner(const WorldCoasterRuntime& world, std::size_t ride, std::size_t placement) {
    for (std::size_t i = placement; i > 0; --i) {
        if (std::ranges::any_of(world.placements[i - 1].links, [&](const auto& ref) {
                return ref && ref->ride == ride;
            })) {
            return &world.placements[i - 1];
        }
    }
    return nullptr;
}

inline double piece_cost(const WorldCoasterRuntime& world, std::size_t r, std::size_t index, bool include_unit = true, bool use_owner_center = true, std::optional<std::array<float, 2>> previous_override = {}) {
    const auto& binding = world.bindings.at(r);
    const auto& local = binding.geometry_state.at(index);
    const auto& owner = world.placements.at(local.placement);
    if (!owner.definition) {
        return 0;
    }
    const auto& data = pylon(owner);
    const auto original = rules(owner);
    auto coefficients = original;
    const auto* previous_owner = previous_placement_owner(world, r, local.placement);
    auto selected_flags = data.flags;
    if (data.flags & 0xC000u) {
        if (!previous_owner || !previous_owner->definition || !(pylon(*previous_owner).flags & 0xC000u)) {
            coefficients = coaster_placement_rules(binding.definition->balance.pylons[0], false);
            selected_flags = binding.definition->pylons[0].flags;
        }
        if ((selected_flags & 0x4000u) && (!index || !(binding.geometry[index - 1].geometry_flags & 0x80u))) {
            coefficients = coaster_placement_rules(binding.definition->balance.pylons[0], false);
        }
    }
    const auto& previous = binding.geometry.at(index ? index - 1 : 0).inline_position;
    const auto& position = binding.geometry[index].inline_position;
    //The native helper has independent endpoint and predecessor overrides.
    //Placement/export supply the tile center, ordinary edit quotes supply no
    //endpoint override, and moving a closed piece supplies its new center as
    //the following piece's predecessor override.
    const auto x = use_owner_center ? owner.position[0] + 5 : position[0], z = use_owner_center ? owner.position[2] + 5 : position[2];
    const auto dx = static_cast<double>(x) - (previous_override ? (*previous_override)[0] : previous[0]);
    const auto dz = static_cast<double>(z) - (previous_override ? (*previous_override)[1] : previous[2]);
    const auto height = (static_cast<double>(data.height_maximum) - data.height_minimum) * local.controls[0] + data.height_minimum;
    return std::sqrt(dx * dx + dz * dz) * static_cast<double>(0.1f) * coefficients.cost_per_length + (include_unit ? original.cost_per_unit : 0) + height * static_cast<double>(0.1f) * coefficients.cost_per_height;
}

inline std::int32_t truncated_cost(double value) {
    require(std::isfinite(value) && value >= static_cast<double>(std::numeric_limits<std::int64_t>::min()) && value < static_cast<double>(std::numeric_limits<std::int64_t>::max()));
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(value)));
}

inline std::int32_t sum_costs(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

inline std::int32_t rounded_cost(float value) {
    //480CB0 truncates the floating argument, then adds one for any nonzero
    //fractional part. Preserve its negative-input behavior as well as positives.
    double integral {};
    const auto fraction = std::modf(static_cast<double>(value), &integral);
    return sum_costs(truncated_cost(integral), fraction != 0 ? 1 : 0);
}

inline BankRecord* editing_bank(AttractionWorld& c) {
    if (c.construction.action_recording_disabled) {
        return nullptr;
    }
    require(c.simulation.current != nullptr);
    auto* payload = c.entities.at(c.simulation.current->saved.bank_account).payload;
    require(payload && payload->kind == ThingKind::bank_account);
    return reinterpret_cast<BankRecord*>(payload);
}

inline void settle_cost(AttractionWorld& c, BankRecord* bank, std::int32_t refund, std::int32_t expense) {
    if (!bank) {
        return;
    }
    (void)apply_bank_balance_and_profit_delta(c, bank, refund);
    (void)charge_world_bank_account_expense(c, bank, expense);
}

//Editor commands may modify another coaster's shared support stack. Keep one
//transaction over the whole graph and its affected pylon poses. Child meshes
//are destroyed only after the new graph has passed the full geometry builder.
struct EditTransaction {
    AttractionWorld& context;
    WorldMeshRuntime& meshes;
    WorldCoasterRuntime& world;
    WorldMeshCreationCheckpoint creation;
    std::vector<WorldCoasterBinding> bindings;
    std::vector<CoasterPlacement> placements;

    struct PylonSnapshot {
        std::uint32_t slot {};
        MeshPlaybackInstance playback;
        SavedMeshInstance metadata;
        std::optional<WorldMeshCellBounds> bounds;
        float reference_height {};
        std::optional<MeshHoardingRuntime> hoarding;
    };

    std::vector<PylonSnapshot> poses;
    std::vector<std::uint32_t> created, retired;
    EditTransaction* parent {};
    bool defer_lifecycle {};

    struct LifecycleChange {
        std::uint32_t map;
        RideDetailState request;
        std::uint32_t detail;
        float multiplier;
        bool active;
    };

    struct AdvisorCancellation {
        ThingId advisor;
        std::int32_t action;
    };

    std::vector<std::variant<LifecycleChange, AdvisorCancellation>> effects;
    bool committed {};

    EditTransaction(AttractionWorld& c, WorldMeshRuntime& m, bool defer = false) : context(c), meshes(m), world(runtime(m)), creation(m.capture_creation_checkpoint(c)), bindings(world.bindings), placements(world.placements), parent(world.active_edit_transaction), defer_lifecycle(defer) {
        const auto capture = [&](std::uint32_t slot) {
            if (!slot || std::ranges::any_of(poses, [&](const auto& p) {
                    return p.slot == slot;
                })) {
                return;
            }
            if (const auto* mesh = meshes.instance(slot)) {
                poses.push_back({ slot, mesh->playback, mesh->metadata, mesh->cell_bounds, mesh->placement_reference_height, mesh->hoarding });
            }
        };
        for (const auto& ride : world.bindings) {
            for (const auto& local : ride.geometry_state) {
                capture(local.mesh_slot);
            }
            if (defer) {
                for (const auto& train : ride.train_state.trains) {
                    for (const auto& car : train.cars) {
                        capture(car.mesh_slot);
                    }
                }
            }
        }
        world.active_edit_transaction = this;
    }

    EditTransaction& root() {
        auto* p = this;
        while (p->parent) {
            p = p->parent;
        }
        return *p;
    }

    void reserve_mesh_creation() {
        created.reserve(created.size() + 1);
    }

    ~EditTransaction() {
        world.active_edit_transaction = parent;
        if (committed) {
            return;
        }
        world.bindings = std::move(bindings);
        world.placements = std::move(placements);
        for (auto& pose : poses) {
            if (auto* mesh = meshes.instance(pose.slot)) {
                mesh->playback = std::move(pose.playback);
                mesh->metadata = pose.metadata;
                mesh->cell_bounds = pose.bounds;
                mesh->placement_reference_height = pose.reference_height;
                mesh->hoarding = std::move(pose.hoarding);
            }
        }
        for (const auto slot : created) {
            if (meshes.instance(slot)) {
                meshes.erase(context, slot);
            }
        }
        meshes.rollback_failed_creation(context, creation);
    }

    void commit() {
        if (parent) {
            //A nested geometry helper cannot retire a mesh still needed by the
            //outer insertion's rollback. Reserve both journals before publishing.
            parent->created.reserve(parent->created.size() + created.size());
            parent->retired.reserve(parent->retired.size() + retired.size());
            parent->created.insert(parent->created.end(), created.begin(), created.end());
            parent->retired.insert(parent->retired.end(), retired.begin(), retired.end());
            committed = true;
            return;
        }
        defer_lifecycle = false;
        for (const auto& effect : effects) {
            match (effect) {
                case { const LifecycleChange& change } => {
                    auto* ride = world.find_map(change.map);
                    require(ride != nullptr, change.map);
                    auto& state = ride->train_state;
                    state.detail_state_flags = change.detail;
                    state.animation_speed_multiplier = change.multiplier;
                    state.runtime_flags = (state.runtime_flags & ~0x800u) | (change.active ? 0x800u : 0u);
                    transition_coaster_detail_state(context, meshes, *ride, change.request);
                }
                case { const AdvisorCancellation& change } => {
                    auto* advisor = context.entities.at(change.advisor).payload;
                    require(advisor && advisor->kind == ThingKind::advisor);
                    world_operations::cancel_advisor_action_by_id(context, reinterpret_cast<AdvisorRecord*>(advisor), change.action);
                }
            }
        }
        for (const auto slot : retired) {
            if (meshes.instance(slot)) {
                meshes.erase(context, slot);
            }
        }
        committed = true;
    }
};

std::size_t place_node_raw(EditTransaction&, std::size_t, const CoasterNodePlacement&, bool validate);
enum class PlacementValidation {
    interactive,
    native_reconstruction
};
//48E270 reconstructs a midpoint through474B60 after its own candidate scan;
//it does not invoke473D60's interactive recursive angle validation. Both
//paths retain one atomic geometry transaction and the same native debit.
std::expected<std::size_t, MeshError> place_node_transaction(AttractionWorld&, WorldMeshRuntime&, std::uint32_t map, const CoasterNodePlacement&, PlacementValidation);
void detach_node_from_stack(WorldCoasterRuntime&, std::size_t ride, std::size_t index);
void erase_node_raw(EditTransaction&, std::size_t ride, std::size_t index);
void finish(EditTransaction&, std::size_t ride);
void move_node_raw(EditTransaction&, std::size_t ride, std::size_t index, std::int32_t x, std::int32_t y, bool validate);
} //namespace simcoaster::coaster_editor_detail
