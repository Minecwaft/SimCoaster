#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/simulation_sine.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/coaster_construction.hpp"
#include "simcoaster/park_construction_refresh.hpp"
#include "simcoaster/rsse_state.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
std::optional<MeshMatrix> attachment_pose(WorldMeshRuntime& meshes, std::uint32_t slot, std::int32_t index) {
    const auto* instance = meshes.instance(slot);
    if (!instance || index < 0 || static_cast<std::size_t>(index) >= instance->embedded.size())
        return std::nullopt;
    if (!(instance->embedded[static_cast<std::size_t>(index)].flags & 1u))
        return std::nullopt;
    const auto pose = evaluate_mesh_pose(instance->playback.model);
    if (!pose)
        throw pose.error();
    const auto matrix = mesh_attachment_transform(instance->playback.model, *pose, static_cast<std::size_t>(index));
    if (!matrix)
        throw matrix.error();
    return *matrix;
}

[[noreturn]] void fail(MeshFailure reason, std::size_t index = 0) {
    throw MeshError{ reason, index, {}, {} };
}

void require(bool value, std::size_t index = 0) {
    if (!value)
        fail(MeshFailure::configuration, index);
}

WorldMeshRuntime& runtime(WorldRuntime& c) {
    return require_bound(c.resources.meshes, "world mesh runtime is not bound");
}

WorldMeshInstance& live(WorldMeshRuntime& meshes, std::uint32_t slot) {
    return require_bound<std::out_of_range>(meshes.instance(slot), "world mesh slot");
}

void initialize_embedded(WorldMeshInstance& instance) {
    auto& model = instance.playback.model;
    instance.embedded.resize(model.resources.size());
    instance.passenger_resources.resize(model.resources.size());
    const auto flags = initialize_mesh_resource_nodes(model);
    if (!flags)
        throw flags.error();
    for (std::size_t i = 0; i < model.resources.size(); ++i) {
        const auto& resource = model.resources[i];
        require(resource.node_index < model.nodes.size(), i);
        auto& node = model.nodes[resource.node_index];
        auto& entry = instance.embedded[i];
        entry.flags = (*flags)[i];
        if (entry.flags & 1u) {
            entry.resource_pool_handle = std::numeric_limits<std::uint32_t>::max();
            instance.embedded_flags |= 1u;
        }
        if (resource.flags & 2u)
            instance.embedded_flags |= 8u;
        if (node.flags & 0x20000000u)
            instance.embedded_flags |= 1u;
    }
}

float terrain_vertex(const TerrainGridView& grid, std::int64_t x, std::int64_t y) {
    require(x >= 0 && y >= 0 && x <= grid.width_cells && y <= grid.height_cells);
    const auto index =
        static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * (static_cast<std::size_t>(grid.width_cells) + 1);
    require(index < grid.height_samples.size(), index);
    return grid.height_samples[index];
}

void place(WorldRuntime& c,
           WorldMeshInstance& instance,
           const CellMeshRegistration& args,
           const WorldMeshTemplate& source,
           bool rotate = true) {
    if (!(args.creation_flags & 0x26u))
        return;
    require(c.construction.active_terrain_model_data && c.construction.active_terrain_model_data->terrain_grid);
    const auto& terrain = *c.construction.active_terrain_model_data->terrain_grid;
    const auto& placement = args.placement;
    auto x = static_cast<std::int64_t>(placement.tile_x), y = static_cast<std::int64_t>(placement.tile_y);
    //0x4C9E00 adjusts the terrain vertex independently of footprint bounds.
    if (placement.orientation_degrees == 90 || placement.orientation_degrees == 180)
        ++y;
    if (placement.orientation_degrees == 180 || placement.orientation_degrees == 270)
        ++x;
    auto minimum_x = x, minimum_y = y;
    if (placement.orientation_degrees == 180)
        minimum_x -= source.footprint_width;
    if (placement.orientation_degrees == 270)
        minimum_x -= source.footprint_height;
    if (placement.orientation_degrees == 90)
        minimum_y -= source.footprint_width;
    if (placement.orientation_degrees == 180)
        minimum_y -= source.footprint_height;
    float height;
    //The modern land renderer instantiates the native17300 template family;
    //the original group5 updater only released these meshes. Sample its actual
    //construction vertex, then run the real terrain fitter below. Reusing the
    //unused factory's footprint average would hit4C9B60's180-degree mixed-X/Y
    //formula outside the installed96x85 Arabian grid. This policy also applies
    //during restoration and refresh, without relaxing terrain bounds checks.
    const bool boundary_template =
        source.definition_key >= 17300u && source.definition_key < 17300u + initial_hoarding_mesh_templates.size();
    if (placement.geometry && (args.creation_flags & 8u) && !boundary_template) {
        const auto sampled = mesh_placement_reference_height(terrain,
                                                             { minimum_x, minimum_y },
                                                             placement.geometry,
                                                             source.footprint_width,
                                                             source.footprint_height,
                                                             args.creation_flags,
                                                             placement.orientation_degrees);
        if (!sampled)
            throw sampled.error();
        height = *sampled;
    } else
        height = terrain_vertex(terrain, x, y);
    instance.placement_reference_height = height;
    auto& model = instance.playback.model;
    instance.metadata.placement_x = placement.tile_x;
    instance.metadata.placement_y = placement.tile_y;
    if (args.creation_flags & 2u) {
        const bool straight = placement.orientation_degrees == 0 || placement.orientation_degrees == 180;
        const auto width = straight ? source.footprint_width : source.footprint_height;
        const auto height_cells = straight ? source.footprint_height : source.footprint_width;
        //Native packs these coordinates into WORD/BYTE fields. The x64 runtime
        //keeps semantic bounds without narrowing the placement coordinates.
        instance.cell_bounds =
            WorldMeshCellBounds{ { minimum_x, minimum_y }, { minimum_x + width - 1, minimum_y + height_cells - 1 } };
        model.flags |= 0x400u;
    }
    if (args.creation_flags & 0x20u) {
        require(model.root.has_value());
        auto& transform = model.nodes[*model.root].local_transform;
        const auto radians =
            static_cast<float>(static_cast<double>(placement.orientation_degrees) * static_cast<double>(0.017453292f));
        const auto index = [](float value) {
            return static_cast<std::int32_t>(static_cast<std::int64_t>(value)
                                             % static_cast<std::int64_t>(simulation_sine_sample_count));
        };
        const auto sine = simulation_sine_at_index(index(radians * simulation_sine_samples_per_radian));
        const auto cosine =
            simulation_sine_at_index(index((radians + 1.5707964f) * simulation_sine_samples_per_radian));
        if (rotate)
            for (std::size_t row = 0; row < 3; ++row) {
                const double a = transform[row * 4], b = transform[row * 4 + 2];
                transform[row * 4] = static_cast<float>(a * cosine + b * sine);
                transform[row * 4 + 2] = static_cast<float>(b * cosine - a * sine);
            }
        transform[12] = static_cast<float>(static_cast<double>(x) * terrain.cell_scale_x + terrain.origin_x);
        transform[13] = height;
        transform[14] = static_cast<float>(static_cast<double>(y) * terrain.cell_scale_y + terrain.origin_y);
        auto angle = placement.orientation_degrees % 360;
        if (angle < 0)
            angle += 360;
        instance.metadata.orientation_degrees = angle;
    }
    instance.metadata.footprint_width = source.footprint_width;
    instance.metadata.footprint_height = source.footprint_height;
    if ((args.creation_flags & 2u) && model.root) {
        const auto matrix = model.nodes[*model.root].local_transform;
        const auto transform = [&](const std::array<float, 3>& point) {
            std::array<float, 3> result{};
            for (std::size_t axis = 0; axis < 3; ++axis)
                result[axis] = static_cast<float>(static_cast<double>(matrix[axis + 8]) * point[2]
                                                  + static_cast<double>(matrix[axis + 4]) * point[1]
                                                  + static_cast<double>(point[0]) * matrix[axis]
                                                  + matrix[axis + 12]);
            return result;
        };
        //copy_and_transform_mesh_model_bounds transforms just the two endpoints, without reordering
        //them into a normalized AABB after rotation.
        model.bounds = { transform(model.bounds.minimum), transform(model.bounds.maximum) };
    }
}
} //namespace

const WorldMeshTemplate* WorldMeshRuntime::definition(std::uint32_t id) const {
    return id && id <= templates_.size() ? &templates_[id - 1] : nullptr;
}

void WorldMeshRuntime::configure_coaster_template(std::uint32_t id, WorldCoasterDefinition value) {
    if (!id || id > templates_.size())
        throw std::out_of_range("Coaster template");
    auto& target = templates_[id - 1];
    target.coaster = std::make_shared<WorldCoasterDefinition>(std::move(value));
    target.runtime_flags |= 0x40000u;
}

WorldMeshInstance* WorldMeshRuntime::instance(std::uint32_t id) {
    return id && id < slots_.size() ? slots_[id].get() : nullptr;
}

const WorldMeshInstance* WorldMeshRuntime::instance(std::uint32_t id) const {
    return id && id < slots_.size() ? slots_[id].get() : nullptr;
}

WorldMeshCreationCheckpoint WorldMeshRuntime::capture_creation_checkpoint(WorldRuntime&) const {
    if (restoring_)
        throw std::logic_error("cannot checkpoint mesh restore staging");
    if (slots_.empty()
        || slots_[0]
        || allocator_.next_slot_cursor >= slots_.size()
        || static_cast<std::uint64_t>(allocator_.live_count) + allocator_.hole_count + 1 != slots_.size())
        throw std::logic_error("invalid mesh allocator before creation");
    WorldMeshCreationCheckpoint checkpoint;
    checkpoint.owner_ = this;
    checkpoint.allocator_ = allocator_;
    checkpoint.slot_limit_ = slots_.size();
    checkpoint.event_count_ = events_.size();
    checkpoint.retiring_count_ = retiring_slots_.size();
    checkpoint.selected_slot_ = selected_slot;
    checkpoint.original_live_.reserve(allocator_.live_count);
    for (std::size_t slot = 1; slot < slots_.size(); ++slot)
        if (slots_[slot])
            checkpoint.original_live_.push_back({ static_cast<std::uint32_t>(slot), slots_[slot].get() });
    if (checkpoint.original_live_.size() != allocator_.live_count
        || (allocator_.next_slot_cursor + 1 < slots_.size() && slots_[allocator_.next_slot_cursor + 1]))
        throw std::logic_error("mesh creation checkpoint disagrees with live slots");
    return checkpoint;
}

void WorldMeshRuntime::rollback_failed_creation(WorldRuntime&, const WorldMeshCreationCheckpoint& checkpoint) {
    if (checkpoint.owner_ != this
        || restoring_
        || !checkpoint.slot_limit_
        || retiring_slots_.size() != checkpoint.retiring_count_
        || events_.size() < checkpoint.event_count_)
        throw std::logic_error("invalid failed mesh creation scope");
    //Exact identities also catch an unreclaimed child allocated into an old
    //hole, or accidental destruction/replacement of a pre-existing owner.
    std::size_t original = 0;
    for (std::size_t slot = 1; slot < slots_.size(); ++slot)
        if (slots_[slot]) {
            if (slot >= checkpoint.slot_limit_ || original >= checkpoint.original_live_.size())
                throw std::logic_error("failed mesh creation still owns a new live slot");
            const auto& expected = checkpoint.original_live_[original++];
            if (expected.slot != slot || expected.instance != slots_[slot].get())
                throw std::logic_error("failed mesh creation changed an original mesh owner");
        }
    if (original != checkpoint.original_live_.size())
        throw std::logic_error("failed mesh creation lost an original mesh owner");
    //erase() may have trimmed holes that were present before the scope. Resize
    //to the captured limit in either direction, then restore coherent counters.
    slots_.resize(checkpoint.slot_limit_);
    allocator_ = checkpoint.allocator_;
    events_.resize(checkpoint.event_count_);
    selected_slot = checkpoint.selected_slot_;
}

std::expected<std::uint32_t, MeshError> WorldMeshRuntime::register_template(WorldMeshTemplate value) {
    if (!value.assets
        || templates_.size() >= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
        || value.control_count > std::numeric_limits<std::uint16_t>::max()
        || value.footprint_width < 0
        || value.footprint_height < 0)
        return std::unexpected(MeshError{ MeshFailure::configuration, templates_.size(), {}, {} });
    value.control_count = std::max(value.control_count, 1u);
    templates_.push_back(std::move(value));
    return static_cast<std::uint32_t>(templates_.size());
}

std::expected<void, MeshError> WorldMeshRuntime::prepare_hoarding(WorldRuntime& c,
                                                                  std::uint32_t id,
                                                                  const DefinitionGrid* shape,
                                                                  const DefinitionGrid& layout,
                                                                  std::string_view theme,
                                                                  bool allowed) {
    const auto prepared = prepare_terrain(c, id, shape, allowed);
    if (!prepared)
        return prepared;
    auto& source = templates_[id - 1];
    auto built = build_mesh_hoarding(source.assets->model, *source.terrain_base, layout, theme);
    if (!built)
        return std::unexpected(built.error());
    if (*built) {
        source.hoarding = std::make_shared<MeshHoardingTemplate>(std::move(**built));
        source.runtime_flags |= 2u;
    } else {
        source.hoarding.reset();
        source.runtime_flags &= ~2u;
    }
    return {};
}

std::expected<void, MeshError>
WorldMeshRuntime::prepare_terrain(WorldRuntime& c, std::uint32_t id, const DefinitionGrid* shape, bool allowed) {
    if (!definition(id)
        || !c.construction.active_terrain_model_data
        || !c.construction.active_terrain_model_data->terrain_grid)
        return std::unexpected(MeshError{ MeshFailure::configuration, id, {}, {} });
    auto& source = templates_[id - 1];
    const auto& terrain = *c.construction.active_terrain_model_data->terrain_grid;
    auto primary = select_mesh_procedural_base(source.assets->model,
                                               shape,
                                               terrain,
                                               source.footprint_width,
                                               source.footprint_height,
                                               allowed);
    if (!primary)
        return std::unexpected(primary.error());
    std::shared_ptr<const MeshProceduralBase> alternate;
    if (source.alternate) {
        auto selected = select_mesh_procedural_base(source.alternate->model,
                                                    shape,
                                                    terrain,
                                                    source.footprint_width,
                                                    source.footprint_height,
                                                    allowed);
        if (!selected)
            return std::unexpected(selected.error());
        alternate = std::make_shared<MeshProceduralBase>(std::move(*selected));
    }
    auto assets = std::make_shared<MeshPlaybackAssets>(*source.assets);
    auto tiles = build_mesh_base_tile_plan(assets->model,
                                           *primary,
                                           shape,
                                           terrain,
                                           static_cast<std::uint32_t>(source.footprint_width),
                                           static_cast<std::uint32_t>(source.footprint_height));
    if (!tiles)
        return std::unexpected(tiles.error());
    source.terrain_texture_plan =
        tiles->cells.empty() ? nullptr : std::make_shared<const MeshBaseTilePlan>(std::move(*tiles));
    source.assets = std::move(assets);
    source.terrain_base = std::make_shared<MeshProceduralBase>(std::move(*primary));
    source.alternate_terrain_base = std::move(alternate);
    return {};
}

void WorldMeshRuntime::deliver(WorldRuntime& c, std::uint32_t slot, const MeshPlaybackResult& result) {
    if (result.events) {
        events_.push_back({ slot, result.events });
        if (result.events & std::to_underlying(MeshPlaybackEvent::renderer_invalidated))
            invalidate_mesh_renderer_cache(c);
    }
}

void WorldMeshRuntime::deliver_hoarding(WorldRuntime& c, std::uint32_t slot, const MeshHoardingResult& result) {
    std::uint32_t events = 0;
    if (result.registered)
        events |= std::to_underlying(WorldMeshHoardingEvent::registered);
    if (result.unregistered)
        events |= std::to_underlying(WorldMeshHoardingEvent::unregistered);
    if (result.pose_changed)
        events |= std::to_underlying(WorldMeshHoardingEvent::pose_changed);
    deliver(c, slot, { {}, events });
}

std::expected<std::uint32_t, MeshError> WorldMeshRuntime::create(WorldRuntime& c, const CellMeshRegistration& args) {
    std::optional<WorldMeshCreationCheckpoint> creation;
    std::uint32_t pending_slot = 0;
    bool new_coaster_owner = false;
    std::string_view creation_stage = "template/playback";
    const auto rollback = [&] {
        if (!creation)
            return;
        //initialize_fresh_coaster_runtime reclaims its cars, pylon meshes and map
        //reservation on failure. Remove the remaining parent before pool rollback.
        if (pending_slot && instance(pending_slot))
            erase(c, pending_slot);
        if (new_coaster_owner)
            coasters_.reset();
        rollback_failed_creation(c, *creation);
    };
    try {
        last_error.reset();
        const auto* source = definition(static_cast<std::uint32_t>(args.definition_index));
        require(source != nullptr);
        //Native creation0x800 asks clone_mesh_asset_resource_with_options for
        //private resource slots (0x231F instead of 0x211F). MeshPlayback already
        //owns those model/resource vectors; it needs no separate legacy clone.
        //Creation0x200 on a coaster template additionally builds track runtime.
        const bool coaster_runtime = (args.creation_flags & 0x200u) && (source->runtime_flags & 0x40000u);
        const bool fresh_coaster = coaster_runtime && !restoring_;
        if (coaster_runtime) {
            require(source->coaster != nullptr, args.definition_index);
            //Native476BE0 mode1 defers trains/geometry to the Coasters section.
            //Fresh mode0 creates the real runtime; other unstaged modes are invalid.
            require(restoring_ ? args.callback_context == 1 : args.callback_context == 0, args.callback_context);
            if (fresh_coaster)
                creation.emplace(capture_creation_checkpoint(c));
        }
        if (args.creation_flags & 8u)
            require(c.construction.active_terrain_model_data && c.construction.active_terrain_model_data->terrain_grid);
        const bool alternate = (args.creation_flags & 0x400u) && source->alternate;
        auto flags = source->runtime_flags & ~1u;
        if (args.creation_flags & 1u)
            flags |= 4u;
        if (args.creation_flags & 2u)
            flags |= 0x4000u;
        if (args.creation_flags & 0x80u)
            flags |= 0x8000u;
        //Native p-prefixed alternates are loaded without the primary's generated
        //hoarding descriptor. Presence must follow the selected owner.
        if (alternate)
            flags &= ~2u;
        auto playback = create_mesh_playback(alternate ? source->alternate : source->assets,
                                             alternate ? 1 : source->control_count,
                                             flags,
                                             (args.creation_flags & 0x100u) != 0);
        if (!playback)
            throw playback.error();
        auto created = std::make_unique<WorldMeshInstance>();
        created->template_id = static_cast<std::uint32_t>(args.definition_index);
        created->playback = std::move(*playback);
        //4AAFB0 ->49A220: factory clone options211F/231F select a MASK,
        //not a resource/node ordinal. Matching metadata hides its mapped node.
        //Keep the shared asset unchanged; each instance owns these flags.
        for (const auto& resource : created->playback.model.resources)
            if (args.embedded_node_resource_selector & resource.selector_filter)
                created->playback.model.nodes.at(resource.node_index).flags |= 0x80000010u;
        created->terrain_base = alternate ? source->alternate_terrain_base : source->terrain_base;
        if (args.creation_flags & 8u)
            require(created->terrain_base != nullptr);
        if (created->terrain_base)
            apply_mesh_procedural_base_flags(created->playback.model, *created->terrain_base);
        if (args.placement.geometry)
            created->placement_geometry = std::make_shared<DefinitionGrid>(*args.placement.geometry);
        if (source->hoarding && !alternate) {
            created->hoarding = create_mesh_hoarding(source->hoarding);
            created->playback.runtime_flags |= 2u;
        }
        auto& metadata = created->metadata;
        metadata.definition_key = source->definition_key;
        metadata.creation_flags = args.creation_flags;
        metadata.owner_context = args.owner_context_or_rsse_state;
        metadata.footprint_width = source->footprint_width;
        metadata.footprint_height = source->footprint_height;
        initialize_embedded(*created);
        MeshPlaybackResult initial;
        if (created->playback.registered)
            initial.events |= std::to_underlying(MeshPlaybackEvent::registered);
        if (created->playback.assets->animations.find(0)) {
            auto start = request_mesh_playback(created->playback, 0, { { 0, 0 }, 0, 1 }, clocks);
            if (!start)
                throw start.error();
            initial.events |= start->events;
            auto hold = request_mesh_playback(created->playback, 0, { { 13, 0 }, 0, 1 }, clocks);
            if (!hold)
                throw hold.error();
            initial.events |= hold->events;
        }
        creation_stage = "placement";
        try {
            place(c, *created, args, *source);
        } catch (MeshError error) {
            if (error.resource.empty())
                error.resource =
                    "Mesh placement "
                    + source->resource_name
                    + " dimensions "
                    + std::to_string(source->footprint_width)
                    + "x"
                    + std::to_string(source->footprint_height)
                    + " nodes "
                    + std::to_string(created->playback.model.nodes.size())
                    + " root "
                    + (created->playback.model.root ? std::to_string(*created->playback.model.root) : "absent")
                    + " angle "
                    + std::to_string(args.placement.orientation_degrees);
            throw error;
        }
        if (args.creation_flags & 8u) {
            creation_stage = "terrain fitting";
            const auto fitted = fit_mesh_to_terrain(created->playback.assets->model,
                                                    created->playback.model,
                                                    *created->terrain_base,
                                                    *c.construction.active_terrain_model_data->terrain_grid,
                                                    created->placement_reference_height);
            if (!fitted)
                throw fitted.error();
        }
        if ((args.creation_flags & 4u)
            && created->hoarding
            && c.construction.active_terrain_model_data
            && c.construction.active_terrain_model_data->terrain_grid) {
            creation_stage = "construction hoarding";
            const auto bounds = created->cell_bounds.value_or(WorldMeshCellBounds{});
            const auto root_height =
                created->playback.model.root
                    ? created->playback.model.nodes[*created->playback.model.root].local_transform[13]
                    : 0;
            const auto initialized =
                initialize_mesh_hoarding_terrain(*created->hoarding,
                                                 *c.construction.active_terrain_model_data->terrain_grid,
                                                 bounds.minimum,
                                                 bounds.maximum,
                                                 created->metadata.orientation_degrees,
                                                 root_height);
            if (!initialized)
                throw initialized.error();
        }
        for (auto& node : created->playback.model.nodes)
            if (node.drawable)
                node.flags |= 0x10000u;
        created->playback.model.flags |= 0x40000u;
        creation_stage = "face normals";
        const auto normals = recompute_mesh_face_normals(created->playback.model);
        if (!normals)
            throw normals.error();
        if (args.creation_flags & 8u)
            prepare_terrain_textures(*created, *source);
        const bool automatic = args.slot_index == -1;
        require(automatic ? !restoring_ : (restoring_ && args.slot_index > 0));
        const auto slot = automatic ? allocator_.next_slot_cursor + 1 : static_cast<std::uint32_t>(args.slot_index);
        require(slot && slot <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()), slot);
        if (!automatic)
            require(slot < slots_.size(), slot);
        if (slot >= slots_.size())
            slots_.resize(static_cast<std::size_t>(slot) + 1);
        require(!slots_[slot], slot);
        if (next_instance_generation_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("mesh instance generation exhausted");
        created->generation = ++next_instance_generation_;
        metadata.slot = slot;
        slots_[slot] = std::move(created);
        pending_slot = slot;
        if (automatic) {
            ++allocator_.live_count;
            if (allocator_.hole_count)
                --allocator_.hole_count;
            allocator_.next_slot_cursor = slot;
            while (allocator_.next_slot_cursor + 1 < slots_.size() && slots_[allocator_.next_slot_cursor + 1])
                ++allocator_.next_slot_cursor;
        }
        if (fresh_coaster) {
            //Publish the parent's queued renderer event before creating children;
            //a failed factory scope removes the entire unobserved event tail.
            deliver(c, slot, initial);
            if (!coasters_) {
                coasters_ = std::make_unique<WorldCoasterRuntime>();
                new_coaster_owner = true;
            }
            const auto initialized =
                initialize_fresh_coaster_runtime(c, *this, *coasters_, slot, args.associated_park_object);
            if (!initialized)
                throw initialized.error();
            return slot;
        }
        deliver(c, slot, initial);
        return slot;
    } catch (MeshError error) {
        rollback();
        if (error.resource.empty())
            error.resource = "Mesh creation: "
                           + std::string{ creation_stage }
                           + " (template "
                           + std::to_string(args.definition_index)
                           + ", tile "
                           + std::to_string(args.placement.tile_x)
                           + ","
                           + std::to_string(args.placement.tile_y)
                           + ", angle "
                           + std::to_string(args.placement.orientation_degrees)
                           + ")";
        last_error = error;
        return std::unexpected(std::move(error));
    } catch (...) {
        rollback();
        throw;
    }
}

std::expected<void, MeshError> WorldMeshRuntime::refresh_terrain(WorldRuntime& c, std::uint32_t slot) {
    try {
        last_error.reset();
        auto* found = instance(slot);
        require(found != nullptr, slot);
        auto& target = *found;
        const auto* source = definition(target.template_id);
        require(source
                && c.construction.active_terrain_model_data
                && c.construction.active_terrain_model_data->terrain_grid);
        const auto& terrain = *c.construction.active_terrain_model_data->terrain_grid;
        //Refresh uses the current pose and zero rotation delta. Reapplying the
        //original cardinal rotation here would rotate the mesh on every refresh.
        const auto flags = target.metadata.creation_flags;
        CellMeshRegistration args{ static_cast<std::int32_t>(target.template_id),
                                   flags,
                                   { target.metadata.orientation_degrees,
                                     target.metadata.placement_x,
                                     target.metadata.placement_y,
                                     target.placement_geometry.get() },
                                   0,
                                   target.metadata.owner_context,
                                   nullptr,
                                   0,
                                   0,
                                   -1,
                                   0 };
        auto temporary = target;
        temporary.playback.model.bounds = temporary.playback.assets->model.bounds;
        place(c, temporary, args, *source, false);
        if (flags & 8u) {
            require(temporary.terrain_base != nullptr);
            const auto fitted = fit_mesh_to_terrain(temporary.playback.assets->model,
                                                    temporary.playback.model,
                                                    *temporary.terrain_base,
                                                    terrain,
                                                    temporary.placement_reference_height);
            if (!fitted)
                throw fitted.error();
        }
        if ((flags & 4u) && temporary.hoarding) {
            const auto bounds = temporary.cell_bounds.value_or(WorldMeshCellBounds{});
            const auto root_height =
                temporary.playback.model.root
                    ? temporary.playback.model.nodes[*temporary.playback.model.root].local_transform[13]
                    : 0;
            auto result = initialize_mesh_hoarding_terrain(*temporary.hoarding,
                                                           terrain,
                                                           bounds.minimum,
                                                           bounds.maximum,
                                                           temporary.metadata.orientation_degrees,
                                                           root_height);
            if (!result)
                throw result.error();
            result = apply_mesh_hoarding_phase(*temporary.hoarding, temporary.metadata.animation_phase);
            if (!result)
                throw result.error();
            if (temporary.hoarding->registered && temporary.hoarding->model.root && temporary.playback.model.root)
                for (std::size_t axis = 0; axis < 3; ++axis)
                    temporary.hoarding->model.nodes[*temporary.hoarding->model.root].local_transform[12 + axis] =
                        temporary.playback.model.nodes[*temporary.playback.model.root].local_transform[12 + axis];
        }
        const auto normals = recompute_mesh_face_normals(temporary.playback.model);
        if (!normals)
            throw normals.error();
        if (flags & 8u)
            prepare_terrain_textures(temporary, *source);
        //Commit geometry only. Save/restore staging spans keep their existing
        //owners instead of inheriting pointers from the temporary's copied arrays.
        target.playback.model = std::move(temporary.playback.model);
        target.hoarding = std::move(temporary.hoarding);
        target.terrain_texture_cells = std::move(temporary.terrain_texture_cells);
        target.terrain_texture_originals = std::move(temporary.terrain_texture_originals);
        target.terrain_texture_error = std::move(temporary.terrain_texture_error);
        target.cell_bounds = temporary.cell_bounds;
        target.placement_reference_height = temporary.placement_reference_height;
        invalidate_mesh_renderer_cache(c);
        return {};
    } catch (const MeshError& error) {
        last_error = error;
        return std::unexpected(error);
    }
}

bool WorldMeshRuntime::erase(WorldRuntime& c, std::uint32_t slot) {
    auto* value = instance(slot);
    if (!value)
        return false;
    if (restoring_)
        throw std::logic_error("mesh pool is restoring");
    //Removing coaster placement cells can retire the owning park object, whose
    //script cleanup requests this same mesh again. Keep its allocation alive
    //until this outer teardown finishes, and make that nested request harmless.
    if (std::ranges::contains(retiring_slots_, slot))
        return false;
    retiring_slots_.push_back(slot);

    struct RetiringScope {
        std::vector<std::uint32_t>& slots;

        ~RetiringScope() {
            slots.pop_back();
        }
    } retiring{ retiring_slots_ };

    c.ports.camera.retire_world_view_inspection_mesh(c, slot, value->generation);
    if (coasters_ && coasters_->find(slot))
        erase_coaster(c, slot);
    //Native car teardown releases active flag128 head attachments before the
    //clone arrays disappear. Keep that lifetime with the host mesh owner so a
    //caller cannot leave ResourceVariant allocations behind when erasing a slot.
    for (std::size_t i = 0; i < value->embedded.size(); ++i)
        if ((value->embedded[i].flags & 2u) && (value->playback.model.resources.at(i).flags & 0x80u))
            release_embedded_person(c, slot, i);
    if (value->playback.registered)
        deliver(c, slot, { {}, std::to_underlying(MeshPlaybackEvent::unregistered) });
    if (value->hoarding && value->hoarding->registered)
        deliver_hoarding(c, slot, { false, true, false });
    slots_[slot].reset();
    --allocator_.live_count;
    while (slots_.size() > 1 && !slots_.back())
        slots_.pop_back();
    allocator_.hole_count = static_cast<std::uint32_t>(slots_.size() - 1) - allocator_.live_count;
    allocator_.next_slot_cursor = std::min(allocator_.next_slot_cursor, slot - 1);
    allocator_.next_slot_cursor = std::min(allocator_.next_slot_cursor, static_cast<std::uint32_t>(slots_.size() - 1));
    if (selected_slot == slot)
        selected_slot = 0;
    return true;
}

std::uint32_t WorldMeshRuntime::mark_for_refresh(WorldRuntime& c, const CellMeshRefresh& args) {
    const auto* source = definition(static_cast<std::uint32_t>(args.definition_index));
    if (!source)
        return 0;
    const auto match = [&](WorldMeshInstance& candidate, bool explicit_id) {
        if (!explicit_id) {
            if ((candidate.playback.runtime_flags & 0x10000u)
                || candidate.template_id != static_cast<std::uint32_t>(args.definition_index)
                || candidate.metadata.creation_flags != args.creation_flags)
                return false;
            if (args.placement_present) {
                if (candidate.metadata.placement_x != args.placement.tile_x
                    || candidate.metadata.placement_y != args.placement.tile_y
                    || candidate.metadata.orientation_degrees != args.placement.orientation_degrees)
                    return false;
                if (args.placement.geometry) {
                    const auto* object =
                        find_object_definition_by_id(&c.resources.object_catalog,
                                                     static_cast<std::uint16_t>(source->definition_key));
                    if (object) {
                        if (!object->balance || object->balance->Info.Shape.get() != args.placement.geometry)
                            return false;
                    } else {
                        const auto width = args.resource_context
                                             ? reinterpret_cast<std::uintptr_t>(args.resource_context)
                                             : static_cast<std::uintptr_t>(args.placement.geometry->width);
                        const auto height = args.object_identity
                                              ? reinterpret_cast<std::uintptr_t>(args.object_identity)
                                              : static_cast<std::uintptr_t>(args.placement.geometry->height);
                        if (width != static_cast<std::uintptr_t>(candidate.metadata.footprint_width)
                            || height != static_cast<std::uintptr_t>(candidate.metadata.footprint_height))
                            return false;
                    }
                }
            }
        }
        if (candidate.playback.runtime_flags & 0x40000u) {
            auto* binding = coasters_ ? coasters_->find(candidate.metadata.slot) : nullptr;
            if (!binding)
                throw std::logic_error("Coaster mesh has no restored runtime association");
            binding->park_object = args.owner_park_object;
            //Native refresh passes allow_registration=0: only remove the remembered
            //pulsing-color registration. It does not rebuild track geometry/metrics.
            if (binding->pulsing_geometry_slot) {
                auto& geometry = live(*this, *binding->pulsing_geometry_slot);
                geometry.playback.runtime_flags &= ~0x4000000u;
                binding->pulsing_geometry_slot.reset();
            }
        }
        candidate.playback.runtime_flags |= 0x10000u;
        return true;
    };
    if (args.mesh_instance_id) {
        auto* value = instance(args.mesh_instance_id);
        return value && match(*value, true) ? args.mesh_instance_id : 0;
    }
    for (std::uint32_t slot = 1; slot < slots_.size(); ++slot)
        if (slots_[slot] && match(*slots_[slot], false))
            return slot;
    return 0;
}

void WorldMeshRuntime::bind_rsse(WorldRuntime& c, std::uint32_t slot, std::uint32_t script) {
    if (!slot)
        return;
    auto& target = live(*this, slot);
    if (target.playback.runtime_flags & 0x40000u) {
        auto* binding = coasters_ ? coasters_->find(slot) : nullptr;
        if (!binding)
            throw std::logic_error("Coaster mesh has no restored runtime association");
        binding->script_id = script;
        //0x47A430 visits only the active train count. Sound-script slots 5..8
        //are distinct from saved positioned_request_a/b and are not serialized.
        for (std::size_t i = 0; i < binding->train_state.active_train_count; ++i) {
            auto& train = binding->train_state.trains.at(i);
            std::array fields{ &train.rsse_value_slot_5,
                               &train.rsse_value_slot_6,
                               &train.rsse_value_slot_7,
                               &train.rsse_value_slot_8 };
            for (std::size_t j = 0; j < fields.size(); ++j)
                if (!*fields[j])
                    *fields[j] = static_cast<std::uint32_t>(
                        get_rsse_sound_script_slot(c, script, static_cast<std::int32_t>(j + 5)));
        }
    }
    target.metadata.owner_context = script;
}

void WorldMeshRuntime::set_registered(WorldRuntime& c, std::uint32_t slot, bool registered) {
    auto& target = live(*this, slot);
    if (target.playback.registered == registered)
        return;
    target.playback.registered = registered;
    deliver(c,
            slot,
            { {}, std::to_underlying(registered ? MeshPlaybackEvent::registered : MeshPlaybackEvent::unregistered) });
}

void WorldMeshRuntime::force_animation_active(std::uint32_t slot) {
    auto& target = live(*this, slot);
    target.playback.active_control_count = 1;
    target.playback.model.flags |= 0x80000u;
}

std::expected<MeshPlaybackResult, MeshError>
WorldMeshRuntime::request(WorldRuntime& c, std::uint32_t slot, std::size_t control, const MeshPlaybackRequest& value) {
    auto* target = instance(slot);
    if (!target)
        return std::unexpected(MeshError{ MeshFailure::index, slot, {}, {} });
    auto result = request_mesh_playback(target->playback, control, value, clocks);
    if (result)
        deliver(c, slot, *result);
    return result;
}

std::expected<void, MeshError> WorldMeshRuntime::activate(WorldRuntime& c, std::uint32_t slot) {
    auto* target = instance(slot);
    if (!target)
        return std::unexpected(MeshError{ MeshFailure::index, slot, {}, {} });
    auto& playback = target->playback;
    const auto activated = reactivate_mesh_playback(playback);
    if (!activated)
        return std::unexpected(activated.error());
    if (playback.active_control_count && !(playback.runtime_flags & 0x3000u)) {
        auto result = advance_mesh_playback(playback, clocks, true);
        if (!result)
            return std::unexpected(result.error());
        deliver(c, slot, *result);
    }
    return advance_hoarding(c, slot, *target);
}

std::expected<void, MeshError> WorldMeshRuntime::mark_visible(WorldRuntime& c, std::uint32_t slot) {
    const auto result = activate(c, slot);
    if (result)
        instance(slot)->playback.runtime_flags |= 0x200000u;
    return result;
}

std::expected<void, MeshError>
WorldMeshRuntime::select_hoarding_variant(WorldRuntime& c, std::uint32_t slot, std::int32_t variant) {
    auto* value = instance(slot);
    if (!value)
        return std::unexpected(MeshError{ MeshFailure::index, slot, {}, {} });
    if (!value->hoarding)
        return {};
    const auto result = select_mesh_hoarding_variant(*value->hoarding,
                                                     value->playback.runtime_flags,
                                                     value->playback.model,
                                                     value->metadata.orientation_degrees,
                                                     variant);
    if (!result)
        return std::unexpected(result.error());
    deliver_hoarding(c, slot, *result);
    return {};
}

void WorldMeshRuntime::reverse_hoarding(std::uint32_t slot) {
    auto& value = live(*this, slot);
    if (value.hoarding)
        reverse_mesh_hoarding(*value.hoarding, value.playback.runtime_flags);
}

std::expected<void, MeshError>
WorldMeshRuntime::advance_hoarding(WorldRuntime& c, std::uint32_t slot, WorldMeshInstance& value) {
    if (!(value.playback.runtime_flags & 0x20u))
        return {};
    if (!value.hoarding)
        return std::unexpected(MeshError{ MeshFailure::configuration, slot, {}, {} });
    const auto result = advance_mesh_hoarding(*value.hoarding,
                                              value.playback.runtime_flags,
                                              value.metadata.animation_phase,
                                              simulation_delta.count());
    if (!result)
        return std::unexpected(result.error());
    deliver_hoarding(c, slot, *result);
    return {};
}

std::expected<void, MeshError> WorldMeshRuntime::update(WorldRuntime& c, WorldMeshUpdate mode) {
    if (mode == WorldMeshUpdate::selected) {
        if (c.view.flags & 4u) {
            if (!selected_slot || !instance(selected_slot))
                return std::unexpected(MeshError{ MeshFailure::index, selected_slot, {}, {} });
            auto& value = *instance(selected_slot);
            auto result = advance_mesh_playback(value.playback, clocks, true);
            if (!result)
                return std::unexpected(result.error());
            deliver(c, selected_slot, *result);
            const auto advanced = advance_hoarding(c, selected_slot, value);
            if (!advanced)
                return advanced;
        }
        return {};
    }
    for (std::uint32_t slot = 1; slot < slots_.size(); ++slot)
        if (auto* value = instance(slot)) {
            auto& playback = value->playback;
            if (playback.runtime_flags & 0x200000u) {
                playback.runtime_flags &= ~0x200000u;
                continue;
            }
            if (playback.active_control_count && !(playback.runtime_flags & 0x3000u)) {
                auto result = advance_mesh_playback(playback, clocks, false, 8);
                if (!result)
                    return std::unexpected(result.error());
                deliver(c, slot, *result);
            }
            const auto advanced = advance_hoarding(c, slot, *value);
            if (!advanced)
                return advanced;
            playback.runtime_flags |= 0x800000u;
        }
    return {};
}

void WorldMeshRuntime::set_deferred_rates(std::uint32_t slot, float rate) {
    if (!std::isfinite(rate) || rate < 0)
        throw std::domain_error("mesh animation rate");
    for (auto& control : live(*this, slot).playback.controls)
        control.deferred.rate = rate;
}

void WorldMeshRuntime::set_control_rate(std::uint32_t slot, std::size_t index, float rate) {
    if (!std::isfinite(rate) || rate < 0)
        throw std::domain_error("mesh animation rate");
    live(*this, slot).playback.controls.at(index).rate = rate;
}

std::vector<WorldMeshEvent> WorldMeshRuntime::take_events() {
    auto result = std::move(events_);
    events_ = {};
    return result;
}

std::uint32_t WorldMeshRuntime::high_resolution_time_ms() {
    return static_cast<std::uint32_t>(clocks.unscaled.count());
}

void WorldMeshRuntime::restore_clock_snapshots(std::uint32_t simulation_ms, std::uint32_t unscaled_ms) {
    clocks = { MeshMilliseconds{ simulation_ms }, MeshMilliseconds{ unscaled_ms } };
}

void WorldMeshRuntime::begin_mesh_restore(WorldRuntime&, const MeshSlotAllocatorState& value) {
    last_error.reset();
    const auto size = static_cast<std::uint64_t>(value.live_count) + value.hole_count;
    if (size > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        throw std::length_error("mesh restore pool");
    coasters_.reset();
    slots_.clear();
    slots_.resize(static_cast<std::size_t>(size) + 1);
    allocator_ = value;
    events_.clear();
    selected_slot = 0;
    restoring_ = true;
}

void WorldMeshRuntime::update_mesh_animation_clock_sources(
    WorldRuntime&) { /* The host supplies both 64-bit clock sources in clocks. */ }

std::int32_t WorldMeshRuntime::find_mesh_template_by_key(WorldRuntime&, std::uint32_t key) {
    for (std::size_t i = templates_.size(); i > 0; --i)
        if (templates_[i - 1].definition_key == key)
            return static_cast<std::int32_t>(i);
    return 0;
}

const DefinitionGrid* WorldMeshRuntime::fallback_mesh_geometry(WorldRuntime&, std::int32_t width, std::int32_t height) {
    if (width <= 0 || height <= 0 || width > 40 || height > 40)
        return nullptr;
    if (!fallback_storage_)
        fallback_storage_ = std::make_unique<DefinitionGrid>();
    auto& result = *fallback_storage_;
    result.width = width;
    result.height = height;
    return &result;
}

MeshRestoreInstance&
WorldMeshRuntime::mesh_restore_instance(WorldRuntime&, std::uint32_t slot, const SavedMeshInstance& saved) {
    auto& target = live(*this, slot);
    target.metadata = saved;
    auto& playback = target.playback;
    target.wire_node_flags.clear();
    for (const auto& node : playback.model.nodes)
        target.wire_node_flags.push_back(node.flags);
    target.wire_controls = capture_mesh_playback_controls(playback);
    target.restore_view = {
        playback.runtime_flags,       playback.transient_state, playback.model.flags,     playback.active_control_count,
        saved.runtime_state,          saved.animation_phase,    !target.embedded.empty(), target.embedded_flags,
        target.active_embedded_count, target.embedded,          target.wire_node_flags,   target.wire_controls
    };
    return target.restore_view;
}

void WorldMeshRuntime::rebuild_mesh_deformation_after_restore(WorldRuntime& c, std::uint32_t slot) {
    auto& target = live(*this, slot);
    if (!target.hoarding) {
        if (target.restore_view.runtime_flags & 0x20u)
            throw std::logic_error("saved mesh requires a hoarding template");
        return;
    }
    const auto restored = restore_mesh_hoarding(*target.hoarding,
                                                target.restore_view.runtime_flags,
                                                target.restore_view.animation_phase,
                                                target.playback.model,
                                                target.metadata.orientation_degrees);
    if (!restored)
        throw std::runtime_error(describe_mesh_error(restored.error()));
    deliver_hoarding(c, slot, *restored);
}

std::optional<MeshAnimationTiming> WorldMeshRuntime::mesh_animation_timing(WorldRuntime&,
                                                                           std::uint32_t slot,
                                                                           std::uint32_t category,
                                                                           std::uint32_t variant) {
    const auto* animation = live(*this, slot).playback.assets->animations.find(category, variant);
    if (!animation)
        return std::nullopt;
    return MeshAnimationTiming{ animation->start_frame, animation->end_frame };
}

void WorldMeshRuntime::invalidate_mesh_renderer_cache(WorldRuntime& c) {
    c.resources.renderer_cache.flags |= 0x80000000u;
}

bool WorldMeshRuntime::finish_mesh_restore_instance(WorldRuntime& c, std::uint32_t slot, MeshRestoreInstance& view) {
    auto& target = live(*this, slot);
    auto& playback = target.playback;
    playback.runtime_flags = view.runtime_flags;
    playback.transient_state = view.transient_state;
    playback.model.flags = view.model_flags;
    target.embedded_flags = view.embedded_flags;
    target.active_embedded_count = view.embedded_extra;
    for (std::size_t i = 0; i < view.node_flags.size(); ++i)
        playback.model.nodes[i].flags = view.node_flags[i];
    const auto reject = [&](MeshError error) {
        if (error.resource.empty()) {
            const auto* source = definition(target.template_id);
            if (source)
                error.resource = source->source_directory + "/" + source->resource_name;
            error.resource += " [restore slot="
                            + std::to_string(slot)
                            + ", key="
                            + std::to_string(target.metadata.definition_key)
                            + ", nodes="
                            + std::to_string(playback.model.nodes.size());
            for (std::size_t i = 0; i < view.animation_controls.size(); ++i) {
                const auto& control = view.animation_controls[i];
                error.resource += "; control"
                                + std::to_string(i)
                                + "="
                                + std::to_string(control.animation_id)
                                + "/"
                                + std::to_string(control.sub_animation_id)
                                + ", deferred="
                                + std::to_string(control.deferred_animation_id)
                                + "/"
                                + std::to_string(control.deferred_sub_animation_id);
                if (control.animation_id < playback.assets->animations.groups.size())
                    error.resource += ", group_size="
                                    + std::to_string(playback.assets->animations.groups[control.animation_id].size());
            }
            error.resource += "]";
        }
        last_error = std::move(error);
        return false;
    };
    //Native4C5770 retains categories with an absent resource group; only the
    //derived timing lookup is skipped. The playback adapter validates actual
    //nonempty-group variants and safely preserves this original save state.
    const auto restored = restore_mesh_playback_controls(playback, view.animation_controls, clocks);
    if (!restored)
        return reject(restored.error());
    deliver(c, slot, *restored);
    const auto posed = evaluate_mesh_playback_pose(playback, true);
    if (!posed)
        return reject(posed.error());
    deliver(c, slot, *posed);
    const auto passengers = restore_passenger_resources(c, target);
    if (!passengers)
        return reject(passengers.error());
    return true;
}

bool WorldMeshRuntime::finish_mesh_restore(WorldRuntime&) {
    std::uint32_t live_count = 0, first_free = static_cast<std::uint32_t>(slots_.size() - 1);
    for (std::size_t i = 1; i < slots_.size(); ++i)
        if (slots_[i])
            ++live_count;
        else
            first_free = std::min(first_free, static_cast<std::uint32_t>(i - 1));
    if (live_count != allocator_.live_count
        || allocator_.next_slot_cursor != first_free
        || (slots_.size() > 1 && !slots_.back())) {
        last_error = MeshError{ MeshFailure::configuration, allocator_.next_slot_cursor, {}, {} };
        return false;
    }
    restoring_ = false;
    return true;
}

MeshSlotAllocatorState WorldMeshRuntime::mesh_slot_allocator_state(WorldRuntime&) const {
    return allocator_;
}

std::optional<MeshSaveInstance> WorldMeshRuntime::mesh_save_instance(WorldRuntime&, std::uint32_t slot) const {
    const auto* target = instance(slot);
    if (!target)
        return std::nullopt;
    target->wire_node_flags.clear();
    for (const auto& node : target->playback.model.nodes)
        target->wire_node_flags.push_back(node.flags);
    target->wire_controls = capture_mesh_playback_controls(target->playback);
    return MeshSaveInstance{ target->metadata,
                             target->playback.runtime_flags,
                             target->metadata.runtime_state,
                             target->metadata.animation_phase,
                             !target->embedded.empty(),
                             target->embedded_flags,
                             target->active_embedded_count,
                             target->embedded,
                             target->wire_node_flags,
                             target->wire_controls };
}

std::uint32_t WorldMeshRuntime::bind_embedded_person(WorldRuntime& c,
                                                     std::uint32_t slot,
                                                     std::size_t index,
                                                     std::uint32_t person_id) {
    auto& owner = live(*this, slot);
    require(index < owner.embedded.size(), index);
    auto& entry = owner.embedded[index];
    require(!(entry.flags & 2u), index);
    owner.passenger_resources.resize(owner.embedded.size());
    const auto id = static_cast<ThingId>(person_id);
    auto* header = c.entities.at(id).payload;
    require(id && header && header->kind == ThingKind::person, id);
    const auto& person = *reinterpret_cast<const PersonRuntimeView*>(header);
    std::uint32_t category{}, variant{};
    const auto generation = static_cast<std::int32_t>(person.generation);
    require(generation >= 0 && generation <= 2, id);
    category = static_cast<std::uint32_t>(generation) + 3u;
    if (get_resource_category_variant_count(c, 7)) {
        variant = person.mobile.sprite_id;
        if (person.mobile.sprite_category == 6)
            variant |= 0x80000000u;
        if (variant & 0x80000000u) {
            category = 7;
            variant &= 0x7FFFFFFFu;
        }
    } else {
        require(c.simulation.current && c.simulation.reference, id);
        c.simulation.current->saved.random_seed = id;
        const auto count = get_resource_category_variant_count(c, category);
        require(count > 0, category);
        variant = (static_cast<std::uint32_t>(next_world_prng_value(*c.simulation.reference)) >> 2)
                % static_cast<std::uint32_t>(count);
    }
    const auto handle = allocate_resource_variant(c,
                                                  resource_program_entry({ ResourceProgramFamily::attached_head, 0 }),
                                                  { category, variant, 0, 0, 0, 0 });
    require(handle != 0, index);
    const auto* resource = find_resource_variant(c, handle);
    require(resource && resource->generation, index);
    owner.passenger_resources[index] = { handle, resource->generation };
    entry.flags |= 2u;
    entry.resource_pool_handle = handle;
    owner.embedded_flags |= 4u;
    ++owner.active_embedded_count;
    return handle;
}

void world_operations::builtin::bind_world_person_sprite_to_mesh_model_object(WorldRuntime& c,
                                                                              std::uint32_t slot,
                                                                              std::int32_t index,
                                                                              std::uint32_t person) {
    if (index < 0)
        throw std::out_of_range("mesh passenger resource index");
    (void)runtime(c).bind_embedded_person(c, slot, static_cast<std::size_t>(index), person);
}

void WorldMeshRuntime::release_embedded_person(WorldRuntime& c, std::uint32_t slot, std::size_t index) {
    auto& owner = live(*this, slot);
    require(index < owner.embedded.size(), index);
    auto& entry = owner.embedded[index];
    //This is a release of an owned attachment, not an idempotent hide request.
    require((entry.flags & 2u) && owner.active_embedded_count, index);
    require(index < owner.passenger_resources.size(), index);
    const auto& lease = owner.passenger_resources[index];
    const auto* resource = find_resource_variant(c, entry.resource_pool_handle);
    require(resource
                && lease.handle == entry.resource_pool_handle
                && lease.generation == resource->generation
                && resource_variant_matches(c,
                                            lease.handle,
                                            resource_program_entry({ ResourceProgramFamily::attached_head, 0 })),
            index);
    --owner.active_embedded_count;
    if (!owner.active_embedded_count)
        owner.embedded_flags &= ~4u;
    entry.flags &= ~2u;
    world_operations::release_resource_pool_handle(c, entry.resource_pool_handle);
    entry.resource_pool_handle = std::numeric_limits<std::uint32_t>::max();
    owner.passenger_resources[index] = {};
}

void
world_operations::builtin::release_mesh_model_object_instance(WorldRuntime& c, std::uint32_t slot, std::int32_t index) {
    if (index < 0)
        throw std::out_of_range("mesh passenger resource index");
    runtime(c).release_embedded_person(c, slot, static_cast<std::size_t>(index));
}

std::optional<std::array<float, 3>>
world_operations::builtin::get_mesh_clone_attachment_position(WorldRuntime& c, std::uint32_t slot, std::int32_t index) {
    if (const auto matrix = attachment_pose(runtime(c), slot, index))
        return std::array{ (*matrix)[12], (*matrix)[13], (*matrix)[14] };
    return std::nullopt;
}

std::optional<RsseCloneDirection>
world_operations::builtin::get_mesh_clone_direction(WorldRuntime& c, std::uint32_t slot, std::int32_t index) {
    if (const auto matrix = attachment_pose(runtime(c), slot, index))
        return RsseCloneDirection{ { (*matrix)[8], (*matrix)[9], (*matrix)[10] },
                                   live(runtime(c), slot).embedded[static_cast<std::size_t>(index)].flags };
    return std::nullopt;
}

std::array<float, 3>
world_operations::builtin::get_mesh_clone_attachment_forward(WorldRuntime& c, std::uint32_t slot, std::int32_t index) {
    auto direction = world_operations::get_mesh_clone_direction(c, slot, index);
    if (!direction)
        return { 0, 0, 1 };
    auto axis = direction->axis;
    if (direction->flags & 16u)
        for (auto& value : axis)
            value = -value;
    const auto length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (!(length > 0))
        throw std::domain_error("zero mesh attachment direction");
    for (auto& value : axis)
        value /= length;
    return axis;
}

void world_operations::builtin::destroy_mesh(WorldRuntime& c, std::uint32_t slot) {
    if (slot)
        runtime(c).erase(c, slot);
}

std::uint32_t ClockPort::get_simulation_clock_time_ms(WorldRuntime& c) {
    return static_cast<std::uint32_t>(runtime(c).clocks.simulation.count());
}

void
world_operations::builtin::clear_deferred_mesh_animation(WorldRuntime& c, std::uint32_t slot, std::uint32_t index) {
    if (slot)
        clear_deferred_mesh_playback(live(runtime(c), slot).playback, index);
}

MeshAnimationControlQuery
world_operations::builtin::get_mesh_animation_control(WorldRuntime& c, std::uint32_t slot, std::uint32_t index) {
    if (!slot)
        return {};
    const auto& control = live(runtime(c), slot).playback.controls.at(index);
    return { control.flags, control.clip.category };
}

void world_operations::builtin::set_mesh_animation_divisor(WorldRuntime& c, std::uint32_t slot, float rate) {
    if (slot)
        runtime(c).set_deferred_rates(slot, rate);
}

void world_operations::builtin::select_park_object_mesh_animation_variant(WorldRuntime& c,
                                                                          std::uint32_t slot,
                                                                          ParkObjectMeshVariant variant) {
    if (!slot)
        return;
    const auto result = runtime(c).select_hoarding_variant(c, slot, static_cast<std::int32_t>(variant));
    if (!result)
        throw std::runtime_error(describe_mesh_error(result.error()));
}

void world_operations::builtin::configure_mesh_instance_deformation_animation(WorldRuntime& c, std::uint32_t slot) {
    if (slot)
        runtime(c).reverse_hoarding(slot);
}

void world_operations::builtin::set_park_object_mesh_normal_intensity(WorldRuntime& c,
                                                                      std::uint32_t slot,
                                                                      float intensity,
                                                                      std::uint8_t selector,
                                                                      std::int32_t origin_x,
                                                                      std::int32_t origin_y) {
    if (!slot)
        return;
    if (!c.referenced_world()
        || !c.weather.map_heightfield_width
        || !c.weather.map_heightfield_height
        || c.weather.map_heightfield_width > 128
        || c.weather.map_heightfield_height > 128)
        throw std::logic_error("invalid mesh shading map dimensions");
    auto& model = live(runtime(c), slot).playback.model;
    const auto pose = evaluate_mesh_pose(model);
    if (!pose)
        throw pose.error();
    for (std::size_t i = 0; i < model.drawable_node_count; ++i)
        if (auto& geometry = model.nodes.at(i).drawable) {
            auto& drawable = *geometry;
            auto& normals =
                drawable.packed_vertex_colors.empty() ? drawable.normals : drawable.packed_color_normal_alias;
            if (normals.size() < drawable.position_indices.size())
                throw std::logic_error("mesh shading requires the original corner normal source");
            //Color animation can change the shared prefix between intensity updates.
            for (std::size_t word = 0; word < drawable.packed_vertex_colors.size(); ++word)
                normals.at(word / 3)[word % 3] = std::bit_cast<float>(drawable.packed_vertex_colors[word]);
            const auto& matrix = pose->at(i);
            for (std::size_t corner = 0; corner < drawable.position_indices.size(); ++corner) {
                const auto& p = drawable.positions.at(drawable.position_indices[corner]);
                const auto transformed = [&](std::size_t axis) {
                    return static_cast<float>(static_cast<double>(p[0]) * matrix[axis]
                                              + static_cast<double>(p[1]) * matrix[4 + axis]
                                              + static_cast<double>(p[2]) * matrix[8 + axis]
                                              + matrix[12 + axis]);
                };
                const auto mapped = [&](float position, std::int32_t origin, std::uint32_t dimension) {
                    const auto coordinate =
                        fixed::subtract(origin, fixed::truncate_integer(static_cast<double>(position) * -0.1));
                    return static_cast<std::uint32_t>(
                        std::clamp(coordinate, 0, static_cast<std::int32_t>(dimension) - 1));
                };
                const auto x = mapped(transformed(0), origin_x, c.weather.map_heightfield_width),
                           y = mapped(transformed(2), origin_y, c.weather.map_heightfield_height);
                const auto flags = c.referenced_world()->map_cells.at(y * 128u + x).base.common.flags;
                auto& normal = normals[corner];
                const auto target = selector == 1 || ((flags & 0x40u) && !(flags & 0x80u)) ? intensity : 1.0f;
                //Exported rides (including Bellows Bouncer's cushion) contain zero
                //normals. Use the same finite fallback as construction shading rather
                //than reproducing the native divide-by-zero on the first closed tick.
                normal = construction_normal_intensity(normal, target);
            }
            for (std::size_t word = 0; word < drawable.packed_vertex_colors.size(); ++word)
                drawable.packed_vertex_colors[word] = std::bit_cast<std::uint32_t>(normals.at(word / 3)[word % 3]);
        }
}

std::uint32_t world_operations::builtin::create_mesh_instance_and_register_slot(WorldRuntime& c,
                                                                                const CellMeshRegistration& args) {
    auto result = runtime(c).create(c, args);
    return result ? *result : 0;
}

std::uint32_t world_operations::builtin::mark_or_find_matching_mesh_instance_for_refresh(WorldRuntime& c,
                                                                                         const CellMeshRefresh& args) {
    return runtime(c).mark_for_refresh(c, args);
}

void
world_operations::builtin::bind_mesh_instance_to_rsse_state(WorldRuntime& c, std::uint32_t slot, std::uint32_t script) {
    runtime(c).bind_rsse(c, slot, script);
}

std::int32_t world_operations::builtin::request_mesh_animation_control_transition(WorldRuntime& c,
                                                                                  std::uint32_t slot,
                                                                                  MeshAnimationRequest category,
                                                                                  std::uint32_t variant,
                                                                                  MeshAnimationTransitionFlags flags,
                                                                                  float rate,
                                                                                  std::uint32_t index) {
    if (!slot)
        return 0;
    auto result = runtime(c).request(
        c,
        slot,
        index,
        { { static_cast<std::uint32_t>(category), variant }, static_cast<std::uint32_t>(flags), rate });
    if (!result)
        throw std::runtime_error(describe_mesh_error(result.error()));
    const auto delay = result->nominal_delay.count();
    if (delay > std::numeric_limits<std::int32_t>::max() || delay < std::numeric_limits<std::int32_t>::min())
        throw std::overflow_error("mesh nominal duration");
    return static_cast<std::int32_t>(delay);
}
} //namespace simcoaster
