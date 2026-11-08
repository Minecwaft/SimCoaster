#include "simcoaster/entity_identity.hpp"
#include "simcoaster/ui_staff_preview.hpp"
#include "simcoaster/ui_person_preview.hpp"
#include "simcoaster/world_visitor_ui.hpp"
#include "simcoaster/sprite_geometry.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_staff.hpp"
#include <format>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>
#include <variant>

namespace simcoaster {
namespace {
//Native UIHireStaffPanel uses off_9B11F0, inside the existing immutable
//resource program image beginning at 9AFB08. Its own script sets flags51000,
//frame-entry1, direction5 and cycles frame offsets0..7, yielding each frame.
constexpr ResourceScriptEntry hire_program { false, (10162672u - 10156808u) / 4u };

std::uint32_t staff_category(StaffType role) {
    return match (role) -> std::uint32_t {
        case StaffType::handyman    => 9;
        case StaffType::mechanic    => 10;
        case StaffType::entertainer => 8;
        case StaffType::guard       => 11;
        case StaffType::researcher  => 12;
        case StaffType::gardener    => 18;
        case _                      => throw std::invalid_argument("Staff preview needs a concrete role");
    };
}

//The native APIs use a handle table, whereas UI ownership must not leave a
//preview in the World sprite pass or retain a handle that can be reused.
//Lease one trailing slot only for a synchronous native operation. The object
//returns to its independent unique_ptr even when script/asset decoding throws.
class PreviewLease {
    WorldRuntime& c_;
    std::unique_ptr<ResourceVariant>& owner_;
    ResourceVariant* saved_active_;
    std::size_t index_;

public:
    PreviewLease(WorldRuntime& c, std::unique_ptr<ResourceVariant>& owner, std::string_view family) : c_(c), owner_(owner), saved_active_(c.resources.variants.active), index_(c.resources.variants.slots.size()) {
        if (!owner || !c.resources.variants.initialized) {
            throw std::logic_error(std::format("{} preview resource lease is unavailable", family));
        }
        if (index_ >= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            throw std::length_error(std::format("{} preview resource handle capacity", family));
        }
        c.resources.variants.slots.push_back(std::move(owner));
        c.resources.variants.slots[index_]->handle = handle();
    }

    ~PreviewLease() {
        auto& runtime = c_.resources.variants;
        owner_ = std::move(runtime.slots[index_]);
        if (owner_) {
            owner_->handle = 0;
        }
        if (runtime.slots.size() == index_ + 1) {
            runtime.slots.pop_back();
        }
        runtime.active = saved_active_;
    }

    PreviewLease(const PreviewLease&) = delete;

    std::uint32_t handle() const {
        return static_cast<std::uint32_t>(index_);
    }
};

bool due(std::uint32_t now, std::uint32_t deadline) {
    //Same signed-clock comparison and one update per native scheduler deadline.
    return std::bit_cast<std::int32_t>(now) > std::bit_cast<std::int32_t>(deadline);
}

struct CandidateBinding {
    std::size_t slot;
    StaffCandidate identity;
};

struct StaffBinding {
    ThingId id;
    //Identity comparison only; never dereference this saved address.
    const ThingHeader* identity;
    std::uint64_t time_hired;
};

struct PersonBinding {
    ThingId id;
    const ThingHeader* identity;
    std::uint32_t arrival_date, arrival_index;
};

struct SpritePreviewPlacement {
    std::array<std::int32_t, 4> bounds;
    std::array<float, 4> clip;
    std::int16_t depth;
    float adjustment;
};

//Both native mobile preview paths use the same detached resource and renderer.
//Only source identity, candidate placement and initial fallback policy differ.
struct SpritePreviewState {
    WorldRuntime& c;
    const std::thread::id thread = std::this_thread::get_id();
    const World* world {};
    const LoadedSpriteCatalog* catalog {};
    std::unique_ptr<ResourceVariant> preview, fallback;
    std::uint32_t category {}, variant {};
    SpritePixelCatalog pixels;
    ResourceSpritePass pass;
    std::map<std::pair<std::size_t, std::size_t>, TextureImagePtr> textures;
    std::string pixel_path;
    std::span<const std::byte> pixel_payload;
    const std::string_view family;
    const float adjustment;
    const bool initial_fallback;

    SpritePreviewState(WorldRuntime& context, std::string_view name, float scale, bool initial) : c(context), family(name), adjustment(scale), initial_fallback(initial) {}

    void require_thread() const {
        if (thread != std::this_thread::get_id()) {
            throw std::logic_error(std::format("{} previews require their owning UI thread", family));
        }
    }

    void clear() {
        world = nullptr;
        preview.reset();
        fallback.reset();
        pass.commands.clear();
        pass.special.clear();
        for (auto& region : pass.regions) {
            region.clear();
        }
    }

    bool ready() {
        if (!c.resources.variants.initialized || !c.resources.variants.catalog || !world || world != c.referenced_world()) {
            return false;
        }
        if (catalog != c.resources.variants.catalog) {
            catalog = c.resources.variants.catalog;
            pixels.bind(*catalog);
            textures.clear();
            preview.reset();
            fallback.reset();
        }
        return true;
    }

    void validate_variant() {
        if (category >= catalog->variants.size() || catalog->variants[category].loaded_resource_count < 0 || variant >= static_cast<std::uint32_t>(catalog->variants[category].loaded_resource_count)) {
            throw std::runtime_error(std::format("{} preview selects an absent sprite variant", family));
        }
    }

    std::unique_ptr<ResourceVariant> take_resource(std::uint32_t handle) {
        auto* resource = find_resource_variant(c, handle);
        if (!resource) {
            throw std::runtime_error(std::format("Unable to create original {} preview resource", family));
        }
        auto owned = std::move(c.resources.variants.slots[handle]);
        if (c.resources.variants.active == resource) {
            c.resources.variants.active = nullptr;
        }
        owned->handle = 0;
        return owned;
    }

    std::uint32_t narrow_index(std::size_t index) {
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error(std::format("{} preview draw index capacity", family));
        }
        return static_cast<std::uint32_t>(index);
    }

    void synchronize_pixels() {
        const auto& values = preview->values;
        const auto& asset = catalog->resources.at(static_cast<std::size_t>(catalog->variants.at(values[10].bits).first_loaded_resource_index) + values[11].bits + static_cast<std::uint32_t>(values[12].integer() / 16));
        //Native F/T reload preserves the catalog address, but replaces the
        //backing buffers borrowed by decoded frame metadata.
        if (pixel_path != asset.pixel_path || pixel_payload.data() != asset.pixel_payload.data() || pixel_payload.size() != asset.pixel_payload.size()) {
            pixels.bind(*catalog);
            textures.clear();
            pixel_path = asset.pixel_path;
            pixel_payload = asset.pixel_payload;
        }
    }

    void advance(std::unique_ptr<ResourceVariant>& owned, std::uint32_t now, bool initialize = false) {
        if (!owned || owned->state == ResourceRunState::completed || (!initialize && !due(now, owned->deadline))) {
            return;
        }
        PreviewLease lease { c, owned, family };
        //Metadata/program operations use the runtime's current snapshot. Restore
        //it rather than letting a UI update change the World scheduler's clock.
        const auto snapshot = c.resources.variants.time_snapshot;
        c.resources.variants.time_snapshot = now;
        try {
            auto& resource = *find_resource_variant(c, lease.handle());
            (void)run_resource_variant_script(c, resource);
            resource.deadline = now + resource.delay;
        } catch (...) {
            c.resources.variants.time_snapshot = snapshot;
            throw;
        }
        c.resources.variants.time_snapshot = snapshot;
    }

    void copy_from(std::uint32_t handle) {
        PreviewLease lease { c, preview, family };
        copy_resource_variant_state(c, lease.handle(), handle);
    }

    bool refresh_live(const MobileEntityState& mobile, std::uint32_t now) {
        //Scheduling precedes the copy: the source program can write slot16,
        //then 4E4CF0 restores the preview flags without resetting completion.
        advance(preview, now);
        const auto handle = mobile.animation.sprite_script;
        const auto* live = find_resource_variant(c, handle);
        if (live && (live->values[10].bits != category || live->values[11].bits != variant)) {
            live = nullptr;
        }
        if (live) {
            fallback.reset();
            if (!preview) {
                preview = take_resource(clone_resource_variant_for_preview(c, handle, 0, 0, 0, adjustment));
            } else {
                copy_from(handle);
            }
        } else {
            //Visitors retain 502B60's initial empty-source behavior. The approved
            //staff repair primes a new owned category3 fallback, even while paused.
            if (!preview && !initial_fallback) {
                return false;
            }
            const bool create_fallback = !fallback || fallback->values[10].bits != category || fallback->values[11].bits != variant;
            if (create_fallback) {
                fallback = take_resource(allocate_resource_variant(c, resource_program_entry({ ResourceProgramFamily::sprite_animation, 3 }), { category, variant, 0, 0, 0, 0 }));
            }
            advance(fallback, now, !preview && create_fallback);
            PreviewLease lease { c, fallback, family };
            if (!preview) {
                preview = take_resource(clone_resource_variant_for_preview(c, lease.handle(), 0, 0, 0, adjustment));
            } else {
                copy_from(lease.handle());
            }
        }
        //690400 renders a copied visible frame even after the clone reached END.
        return preview && preview->render_enabled;
    }

    TextureImagePtr texture(SpriteFrameReference frame) {
        const auto key = std::pair { frame.resource, frame.frame };
        if (const auto found = textures.find(key); found != textures.end()) {
            return found->second;
        }
        const auto image = decode_sprite_image(pixels.payload(frame.resource), frame.frame);
        if (!image) {
            throw std::runtime_error(std::format("Original {} sprite decode failed at byte {}", family, image.error().offset));
        }
        auto output = std::make_shared<TextureImage>();
        output->width = image->width;
        output->height = image->height;
        bool cutout = false, partial = false;
        output->pixels.reserve(image->pixels.size());
        for (const auto packed : image->pixels) {
            const auto rgba = mesh_argb_color(packed);
            cutout |= rgba[3] == 0;
            partial |= rgba[3] != 0 && rgba[3] != 255;
            output->pixels.push_back(rgba);
        }
        output->alpha = partial ? TextureAlpha::translucent : cutout ? TextureAlpha::cutout : TextureAlpha::opaque;
        output->source_mode = partial ? TextureSourceMode::alpha_low : cutout ? TextureSourceMode::alpha_high : TextureSourceMode::opaque;
        return textures.emplace(key, std::move(output)).first->second;
    }

    std::array<float, 4> clipped(const UiDrawList& output, const SpritePreviewPlacement& placement) const {
        if (!std::isfinite(placement.adjustment)
            || placement.adjustment <= 0
            || placement.adjustment > static_cast<float>(std::numeric_limits<std::int32_t>::max())
            || !std::ranges::all_of(placement.clip,
                                    [](float value) {
                                        return std::isfinite(value);
                                    })
            || placement.bounds[2] < placement.bounds[0]
            || placement.bounds[3] < placement.bounds[1]) {
            throw std::invalid_argument(std::format("Invalid {} UI preview placement", family));
        }
        auto clip = placement.clip;
        clip[0] = std::max({ clip[0], static_cast<float>(placement.bounds[0]), 0.f });
        clip[1] = std::max({ clip[1], static_cast<float>(placement.bounds[1]), 0.f });
        clip[2] = std::min({ clip[2], static_cast<float>(placement.bounds[2]), output.canvas_width });
        clip[3] = std::min({ clip[3], static_cast<float>(placement.bounds[3]), output.canvas_height });
        return clip;
    }

    bool draw(UiDrawList& output, const SpritePreviewPlacement& placement, const std::array<float, 4>& clip, bool candidate) {
        synchronize_pixels();

        //502B60 and hire-candidate preview use the original 2048x1536 canvas.
        const auto& b = placement.bounds;
        const auto center = (static_cast<std::int64_t>(b[2]) - b[0]) / 2 + b[0];
        auto baseline = static_cast<std::int64_t>(b[3]) - (static_cast<std::int64_t>(b[3]) - b[1]) / 4;
        if (!candidate) {
            baseline += static_cast<std::int64_t>(placement.adjustment * -.25f);
        }
        preview->values[1].floating(static_cast<float>(center - 1016) * .0009765625f);
        preview->values[2].floating(static_cast<float>(768 - baseline) * .0013020834f);
        preview->values[3].floating(1.f - static_cast<float>(placement.depth) / 40.f);
        preview->values[8].floating(placement.adjustment);

        {
            PreviewLease lease { c, preview, family };
            //queue_resource_sprite's ordinary height sampler is a World-space
            //adapter. UI explicit coordinates already carry their own normalized Y.
            //This high-only bypass bit is removed by the queue's24-bit flag mask;
            //the resulting command retains the native preview flags unchanged.
            auto* resource = find_resource_variant(c, lease.handle());
            const auto flags = resource->values[16].bits;
            resource->values[16].bits |= 0x1000000u;
            const ResourceSpritePassSettings settings { 0, 0, 32, 32 };
            begin_resource_sprite_pass(c, pass, settings);
            bool queued;
            try {
                queued = queue_resource_sprite(c, pixels, pass, settings, lease.handle());
            } catch (...) {
                resource->values[16].bits = flags;
                throw;
            }
            resource->values[16].bits = flags;
            if (!queued) {
                return false;
            }
        }
        SpriteGeometrySettings geometry_settings;
        //Preserve square sprite reference dimensions on the fixed4:3 UI canvas.
        geometry_settings.coordinate_scale = 1536.f / 2048.f;
        geometry_settings.direct3d_state = true;
        const auto geometry = build_resource_sprite_geometry(pass, pixels, geometry_settings, [&](SpriteFrameReference frame) -> std::optional<SpriteTextureState> {
            const auto image = texture(frame);
            return SpriteTextureState { static_cast<std::uint32_t>(*image->source_mode), 0, 0, 0, 1, 1 };
        });
        if (geometry.quads.empty()) {
            return false;
        }
        (void)narrow_index(output.vertices.size() + geometry.quads.size() * 4);
        (void)narrow_index(output.indices.size() + geometry.quads.size() * 6);
        for (const auto& quad : geometry.quads) {
            const auto base = narrow_index(output.vertices.size());
            const auto color = mesh_argb_color(quad.color);
            const std::array<std::array<float, 2>, 4> uv { { { quad.uv[0], quad.uv[1] }, { quad.uv[2], quad.uv[1] }, { quad.uv[2], quad.uv[3] }, { quad.uv[0], quad.uv[3] } } };
            for (std::size_t i = 0; i < quad.vertices.size(); ++i) {
                const auto& v = quad.vertices[i];
                //The shared502B60 preview path supplies selector depth before the
                //explicit-sprite-.00125 bias. Mode2 projection6E16C0 finally scales
                //the complete source Z by0.1, matching ordinary UI attachments.
                const std::array<float, 3> position { (v[0] + 1.f) * 1024.f, (v[1] + 1.f) * 768.f, v[2] * .1f };
                if (!std::ranges::all_of(position, [](float value) {
                        return std::isfinite(value);
                    })) {
                    throw std::runtime_error(std::format("Nonfinite original {} preview geometry", family));
                }
                output.vertices.push_back({ position, uv[i], color });
            }
            UiDrawCommand draw;
            draw.texture = texture(quad.frame);
            draw.first_index = narrow_index(output.indices.size());
            draw.index_count = 6;
            draw.clip = clip;
            draw.material.flags = quad.render_state;
            draw.depth_buffered = true;
            //Sprites decode directly to top-left images. Unlike imported MD2 model
            //UVs, their frame UVs already match the UI compositor row origin.
            if (quad.packet_flags & 2u) {
                for (const auto i : { 0u, 2u, 1u, 0u, 3u, 2u }) {
                    output.indices.push_back(base + i);
                }
            } else {
                for (const auto i : { 0u, 1u, 2u, 0u, 2u, 3u }) {
                    output.indices.push_back(base + i);
                }
            }
            output.commands.push_back(std::move(draw));
        }
        return true;
    }

    template <class Self, class Placement>
    bool append(this Self& self, UiDrawList& output, const Placement& source_placement) {
        self.require_thread();
        const SpritePreviewPlacement placement { source_placement.bounds, source_placement.clip, source_placement.depth, source_placement.adjustment };
        const auto clip = self.clipped(output, placement);
        if (!self.refresh() || clip[0] >= clip[2] || clip[1] >= clip[3]) {
            return false;
        }
        return self.draw(output, placement, clip, self.candidate_selected());
    }
};
} //namespace

struct UiStaffPreview::State : SpritePreviewState {
    std::variant<std::monostate, CandidateBinding, StaffBinding> source;

    explicit State(WorldRuntime& context) : SpritePreviewState(context, "Staff", 8.f, true) {}

    void clear() {
        source = std::monostate {};
        SpritePreviewState::clear();
    }

    bool candidate_selected() const {
        return std::holds_alternative<CandidateBinding>(source);
    }

    StaffRuntimeRecord* staff(ThingId id) const {
        return find_live_entity<StaffRuntimeRecord, true>(c.entities, id);
    }

    const StaffCandidate* candidate(std::size_t slot) const {
        const auto* owner = c.referenced_world();
        if (!owner || slot >= owner->staff_candidates.size()) {
            return nullptr;
        }
        const auto& value = owner->staff_candidates[slot];
        return value.valid && !value.on_pointer ? &value : nullptr;
    }

    bool same_candidate(const StaffCandidate& a, const StaffCandidate& b) const {
        return a.creation_tick == b.creation_tick && a.type == b.type && a.resource_variant == b.resource_variant && a.name_index == b.name_index && a.agency_index == b.agency_index && a.timeout == b.timeout;
    }

    bool reconcile() {
        if (!ready()) {
            clear();
            return false;
        }
        if (const auto* bound = std::get_if<CandidateBinding>(&source)) {
            const auto* current = candidate(bound->slot);
            if (!current || !same_candidate(*current, bound->identity)) {
                clear();
                return false;
            }
            category = staff_category(current->type);
            variant = current->resource_variant;
        } else if (const auto* employee_binding = std::get_if<StaffBinding>(&source)) {
            const auto* current = EntityIdentity { employee_binding->id, employee_binding->identity, employee_binding->time_hired }.get<StaffRuntimeRecord, true>(c.entities);
            if (!current) {
                clear();
                return false;
            }
            category = current->mobile.sprite_category;
            variant = current->mobile.sprite_id;
        } else {
            return false;
        }
        validate_variant();
        return true;
    }

    bool refresh() {
        if (!reconcile()) {
            return false;
        }
        const auto now = c.ports.clock.get_simulation_clock_time_ms(c);
        if (!candidate_selected()) {
            return refresh_live(staff(std::get<StaffBinding>(source).id)->mobile, now);
        }
        if (!preview) {
            preview = take_resource(allocate_resource_variant(c, hire_program, { category, variant, 1, 0, 0, 0 }));
            preview->values[8].floating(8.f);
        }
        advance(preview, now);
        return preview && preview->render_enabled;
    }
};

struct UiPersonPreview::State : SpritePreviewState {
    std::optional<PersonBinding> source;

    explicit State(WorldRuntime& context) : SpritePreviewState(context, "Person", 12.f, false) {}

    void clear() {
        source.reset();
        SpritePreviewState::clear();
    }

    bool candidate_selected() const {
        return false;
    }

    bool reconcile() {
        if (!ready()) {
            clear();
            return false;
        }
        if (!source) {
            return false;
        }
        const auto* current = EntityIdentity { source->id, source->identity, source->arrival_date }.get<PersonRuntimeView, true>(c.entities);
        if (!current || current->arrival_index != source->arrival_index) {
            clear();
            return false;
        }
        category = current->mobile.sprite_category;
        variant = current->mobile.sprite_id;
        validate_variant();
        return true;
    }

    bool refresh() {
        if (!reconcile()) {
            return false;
        }
        const auto now = c.ports.clock.get_simulation_clock_time_ms(c);
        return refresh_live(find_ui_visitor(c, source->id)->mobile, now);
    }
};

UiStaffPreview::UiStaffPreview(WorldRuntime& c) : state_(std::make_unique<State>(c)) {}

UiStaffPreview::~UiStaffPreview() = default;

bool UiStaffPreview::bind_candidate(std::size_t slot) {
    auto& s = *state_;
    s.require_thread();
    const auto* candidate = s.candidate(slot);
    if (!candidate) {
        s.clear();
        return false;
    }
    if (s.world == s.c.referenced_world()) {
        if (const auto* bound = std::get_if<CandidateBinding>(&s.source); bound && bound->slot == slot && s.same_candidate(bound->identity, *candidate)) {
            return true;
        }
    }
    s.clear();
    s.world = s.c.referenced_world();
    s.source = CandidateBinding { slot, *candidate };
    return true;
}

bool UiStaffPreview::bind_staff(ThingId id) {
    auto& s = *state_;
    s.require_thread();
    const auto* staff = s.staff(id);
    if (!staff) {
        s.clear();
        return false;
    }
    if (s.world == s.c.referenced_world()) {
        if (const auto* bound = std::get_if<StaffBinding>(&s.source); bound && bound->id == id && bound->identity == &staff->base.header && bound->time_hired == staff->time_hired) {
            return true;
        }
    }
    s.clear();
    s.world = s.c.referenced_world();
    s.source = StaffBinding { id, &staff->base.header, staff->time_hired };
    return true;
}

void UiStaffPreview::clear() {
    state_->require_thread();
    state_->clear();
}

void UiStaffPreview::retire(ThingId id) {
    state_->require_thread();
    if (const auto* bound = std::get_if<StaffBinding>(&state_->source); bound && bound->id == id) {
        state_->clear();
    }
}

bool UiStaffPreview::bound() const {
    state_->require_thread();
    return !std::holds_alternative<std::monostate>(state_->source);
}

bool UiStaffPreview::refresh() {
    state_->require_thread();
    return state_->refresh();
}

bool UiStaffPreview::append(UiDrawList& output, const UiStaffPreviewPlacement& placement) {
    return state_->append(output, placement);
}

UiPersonPreview::UiPersonPreview(WorldRuntime& c) : state_(std::make_unique<State>(c)) {}

UiPersonPreview::~UiPersonPreview() = default;

bool UiPersonPreview::bind_person(ThingId id) {
    auto& s = *state_;
    s.require_thread();
    const auto* person = find_ui_visitor(s.c, id);
    if (!person) {
        s.clear();
        return false;
    }
    if (s.world == s.c.referenced_world() && s.source && s.source->id == id && s.source->identity == &person->base.header && s.source->arrival_date == person->arrival_date && s.source->arrival_index == person->arrival_index) {
        return true;
    }
    s.clear();
    s.world = s.c.referenced_world();
    s.source = PersonBinding { id, &person->base.header, person->arrival_date, person->arrival_index };
    return true;
}

void UiPersonPreview::clear() {
    state_->require_thread();
    state_->clear();
}

void UiPersonPreview::retire(ThingId id) {
    state_->require_thread();
    if (state_->source && state_->source->id == id) {
        state_->clear();
    }
}

bool UiPersonPreview::bound() const {
    state_->require_thread();
    return state_->source.has_value();
}

bool UiPersonPreview::refresh() {
    state_->require_thread();
    return state_->refresh();
}

bool UiPersonPreview::append(UiDrawList& output, const UiPersonPreviewPlacement& placement) {
    return state_->append(output, placement);
}
} //namespace simcoaster
