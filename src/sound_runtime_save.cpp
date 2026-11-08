#include "sound_runtime_internal.hpp"
#include <cmath>
#include <limits>
#include <set>

namespace simcoaster {
using sound_runtime_detail::Flag;
using sound_runtime_detail::Target;

SoundSavedTarget SoundRuntime::State::save_node(const Target& target) const {
    SoundSavedTarget result;
    result.kind = target.kind;
    result.target_id = target.id;
    result.fields.position = target.position;
    result.fields.parameter = target.parameter;
    result.fields.catalog = target.catalog_slot;
    if (target.catalog && !target.catalog->effects.groups.empty()) {
        const auto& descriptors = target.catalog->effects.groups.front().descriptors;
        for (std::size_t i = 0; i < descriptors.size(); ++i) {
            if (&descriptors[i] == target.descriptor) {
                result.fields.descriptor = static_cast<std::uint32_t>(i);
                break;
            }
        }
    }
    result.fields.selection = static_cast<std::uint32_t>(target.cursor.selection.value_or(0));
    result.fields.sample = static_cast<std::uint32_t>(target.cursor.sample.value_or(0));
    if (target.descriptor && target.cursor.selection && *target.cursor.selection < target.descriptor->selections.size()) {
        const auto count = target.descriptor->selections[*target.cursor.selection].parameters.sample_count;
        //Native8BA620 (8BA7C6..8BA7E9) bounds the sample index when writing,
        //independently of the live cursor. Conditional emitter children can
        //change their parent's row without changing its unused sample cursor;
        //persisting that old row-local index verbatim produces an invalid save.
        //Preserve the native empty-row sentinel as well as its upper clamp.
        result.fields.sample = count ? std::min(result.fields.sample, static_cast<std::uint32_t>(count - 1)) : std::numeric_limits<std::uint32_t>::max();
    }
    result.has_xz_bounds = target.has(Flag::has_bounds) ? 1 : 0;
    if (target.cursor.payload_present) {
        result.payload = target.payload;
        result.payload->keys = target.cursor.keys;
        result.payload->values = target.cursor.values;
    }
    return result;
}

std::expected<SoundBackendSnapshot, SoundError> SoundRuntime::snapshot() const {
    const auto& s = *state_;
    SoundBackendSnapshot result;
    result.listener = s.mixer.listener().position;
    result.pool_blocks = static_cast<std::uint32_t>(s.slots.size() / 100);
    result.catalogs.paths = s.library.reference_paths();
    //Native writer walks tail->previous, retaining original creation order.
    for (auto it = s.active.rbegin(); it != s.active.rend(); ++it) {
        const auto& t = *s.slots[*it].target;
        if ((t.has(Flag::recycle) && t.has(Flag::fading)) || !t.catalog || !t.descriptor) {
            continue;
        }
        if (!t.cursor.selection || *t.cursor.selection >= t.descriptor->selections.size()) {
            continue;
        }
        SoundSavedTargetChain chain;
        chain.id = t.id;
        chain.nodes.push_back(s.save_node(t));
        for (const auto& child : t.children) {
            if (!child->cursor.selection || *child->cursor.selection >= child->descriptor->selections.size()) {
                return std::unexpected(SoundError { SoundFailure::selection, child->cursor.selection.value_or(0), t.catalog->path });
            }
            chain.nodes.push_back(s.save_node(*child));
        }
        result.targets.push_back(std::move(chain));
    }
    return result;
}

std::expected<std::unique_ptr<Target>, SoundError> SoundRuntime::State::restore_node(const SoundSavedTarget& saved, const std::vector<std::uint32_t>& remap, Target* parent) {
    auto target = std::make_unique<Target>();
    target->kind = saved.kind;
    target->position = saved.fields.position;
    target->parameter = saved.fields.parameter;
    target->parent = parent;
    target->id = saved.target_id;
    target->playlist.random = now;
    const auto fail = [&](SoundFailure why, std::size_t offset, std::string_view field) {
        const auto location = (target->catalog ? target->catalog->path : std::string {}) + " [restore " + std::string { field } + "; target=" + std::to_string(saved.target_id) + "; kind=" + std::to_string(std::to_underlying(saved.kind)) + "; catalog=" + std::to_string(saved.fields.catalog) + "; descriptor=" + std::to_string(saved.fields.descriptor) + "; selection=" + std::to_string(saved.fields.selection) + "; sample=" + std::to_string(saved.fields.sample) + "]";
        return std::unexpected(SoundError { why, offset, location });
    };
    if (!std::ranges::all_of(saved.fields.position,
                             [](float value) {
                                 return std::isfinite(value);
                             })
        || !std::isfinite(saved.fields.parameter)) {
        return fail(SoundFailure::configuration, 0, "finite position/parameter");
    }
    if (saved.fields.catalog >= remap.size()) {
        return fail(SoundFailure::reference, saved.fields.catalog, "catalog remap");
    }
    target->catalog_slot = remap[saved.fields.catalog];
    target->catalog = library.catalog(target->catalog_slot);
    //Native missing-catalog restoration consumes the whole target chain and
    //marks the pooled record for retirement. Do not bind a different category.
    if (!target->catalog) {
        target->set(Flag::recycle);
        return target;
    }
    if (target->catalog->effects.groups.empty()) {
        return fail(SoundFailure::index, 0, "empty descriptor group");
    }
    const auto& group = target->catalog->effects.groups.front();
    if (saved.fields.descriptor >= group.descriptors.size()) {
        return fail(SoundFailure::index, saved.fields.descriptor, "descriptor count=" + std::to_string(group.descriptors.size()));
    }
    target->descriptor = &group.descriptors[saved.fields.descriptor];
    if (saved.fields.selection >= target->descriptor->selections.size()) {
        return fail(SoundFailure::index, saved.fields.selection, "selection count=" + std::to_string(target->descriptor->selections.size()));
    }
    const auto& row = target->descriptor->selections[saved.fields.selection];
    target->cursor.selection = saved.fields.selection;
    if (row.parameters.sample_count) {
        if (saved.fields.sample >= row.parameters.sample_count || saved.fields.sample >= row.samples.size()) {
            return fail(SoundFailure::index, saved.fields.sample, "sample active=" + std::to_string(row.parameters.sample_count) + " stored=" + std::to_string(row.samples.size()));
        }
        target->cursor.sample = saved.fields.sample;
    } else if (!target->emitter()) {
        return fail(SoundFailure::selection, saved.fields.selection, "sampleless non-emitter");
    }
    target->channel_mask = group.parameters.channel_mask;
    configure(*target);
    if (saved.payload) {
        target->cursor.payload_present = true;
        target->payload = *saved.payload;
        target->cursor.keys = saved.payload->keys;
        target->cursor.values = saved.payload->values;
    }
    if (saved.has_xz_bounds) {
        if (!saved.payload || saved.payload->min_x > saved.payload->max_x || saved.payload->min_z > saved.payload->max_z) {
            return fail(SoundFailure::selection, saved.fields.selection, "XZ bounds");
        }
        target->set(Flag::has_bounds);
    }
    //The format deliberately has no driver voice handle or schedule. Rebuild
    //those against the current unscaled clock while retaining the saved graph
    //indices; no old sample pointer can alias a newly selected different row.
    target->scheduled_tick = now + (target->sample() ? target->sample()->clock_offset_ms : 0) + 250u;
    refresh_sound_selection_keys(*target->descriptor, target->cursor);
    return target;
}

std::expected<void, SoundError> SoundRuntime::restore(const SoundBackendSnapshot& snapshot) {
    auto& s = *state_;
    if (snapshot.pool_blocks > 655u) {
        return std::unexpected(SoundError { SoundFailure::count, snapshot.pool_blocks, {} });
    }
    const auto count = std::max(s.slots.size(), static_cast<std::size_t>(snapshot.pool_blocks) * 100);
    std::vector<sound_runtime_detail::Slot> next_slots(count);
    std::vector<std::uint16_t> next_active;
    auto next_generation = s.generation;
    const auto remap = s.library.remap(snapshot.catalogs);
    std::set<std::uint16_t> occupied;
    for (std::size_t i = 0; i < count; ++i) {
        next_slots[i].id = static_cast<std::uint32_t>(i);
    }
    for (const auto& chain : snapshot.targets) {
        const auto slot = static_cast<std::uint16_t>(chain.id);
        if (!chain.id || slot >= count || chain.nodes.empty() || !occupied.insert(slot).second) {
            return std::unexpected(SoundError { SoundFailure::index, chain.id, {} });
        }
        next_generation = std::max(next_generation, chain.id >> 16);
        const auto root_kind = chain.nodes.front().kind;
        if (static_cast<std::uint32_t>(root_kind) < 1 || static_cast<std::uint32_t>(root_kind) > 10 || root_kind == SoundTargetKind::child || root_kind == SoundTargetKind::conditional_child || root_kind == SoundTargetKind::kind8) {
            return std::unexpected(SoundError { SoundFailure::index, chain.id, {} });
        }
        auto root = s.restore_node(chain.nodes.front(), remap, nullptr);
        if (!root) {
            return std::unexpected(root.error());
        }
        (*root)->id = chain.id;
        if (chain.nodes.size() > 1 && !(*root)->emitter()) {
            return std::unexpected(SoundError { SoundFailure::selection, chain.id, {} });
        }
        for (std::size_t i = 1; i < chain.nodes.size(); ++i) {
            const auto expected_child = root_kind == SoundTargetKind::emitter ? SoundTargetKind::child : SoundTargetKind::conditional_child;
            if (chain.nodes[i].kind != expected_child || chain.nodes[i].fields.catalog != chain.nodes[0].fields.catalog || chain.nodes[i].fields.descriptor != chain.nodes[0].fields.descriptor) {
                return std::unexpected(SoundError {
                    SoundFailure::index,
                    i,
                    "restore child chain target=" + std::to_string(chain.id) + "; parent kind=" + std::to_string(std::to_underlying(root_kind)) + "; child kind=" + std::to_string(std::to_underlying(chain.nodes[i].kind)) + "; parent catalog/descriptor=" + std::to_string(chain.nodes[0].fields.catalog) + "/" + std::to_string(chain.nodes[0].fields.descriptor) + "; child catalog/descriptor=" + std::to_string(chain.nodes[i].fields.catalog) + "/" + std::to_string(chain.nodes[i].fields.descriptor) });
            }
            auto child = s.restore_node(chain.nodes[i], remap, root->get());
            if (!child) {
                return std::unexpected(child.error());
            }
            //Native8A4820 restores through base bind: now+sample_offset+250,
            //not fresh-childABC60's random interval. The old expiry is not saved.
            (*root)->children.push_back(std::move(*child));
        }
        next_slots[slot].id = chain.id;
        next_slots[slot].target = std::move(*root);
        next_active.insert(next_active.begin(), slot);
    }
    std::vector<std::uint16_t> next_free;
    next_free.reserve(count - occupied.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (!next_slots[i].target) {
            next_free.push_back(static_cast<std::uint16_t>(i));
        }
    }
    s.mixer.stop_all();
    s.voices.clear();
    if (s.output) {
        s.output->clear();
    }
    s.slots = std::move(next_slots);
    s.active = std::move(next_active);
    s.free_slots = std::move(next_free);
    //Native89DB40 imports IDs without advancing the process-lived counter.
    //A fresh owner can then immediately reissue an imported handle. Reserve
    //the imported generation range only after the whole graph validates.
    //The native wire still has a16-bit tag and its eventual wrap limitation.
    s.generation = next_generation;
    s.queued.clear();
    s.force_update = true;
    s.library.collect_released();
    return {};
}
} //namespace simcoaster
