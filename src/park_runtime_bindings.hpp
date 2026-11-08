#pragma once
#include "simcoaster/mesh_model.hpp"
#include "simcoaster/mesh_surface.hpp"
#include "simcoaster/world_ports.hpp"
#include "simcoaster/world_save.hpp"
#include "simcoaster/world_staff_view.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <meta>
#include <unordered_map>

namespace simcoaster {
class WorldSoundRuntime;
class WorldUiRuntime;
class WorldMeshScene;
class ParkConstructionRuntime;
class LayoutReplayRuntime;
class LayoutWorldBackend;
struct WorldMeshInstance;
struct ParticleEffect;
struct ParticleRuntime;

namespace park_runtime_binding_detail {
constexpr std::array ports { ^^AllocationPort, ^^EntityEnginePort, ^^WorldUiHostPort, ^^ConstructionHostPort, ^^AssetIoPort, ^^ParticlePort, ^^ResourceHostPort, ^^AudioHostPort, ^^MeshHostPort, ^^DiagnosticsPort, ^^CameraPort, ^^SaveServices };
} //namespace park_runtime_binding_detail

//Both inheritance and overrides come from complete port declarations.
//Reflecting this host's own bases during its member injection is premature.
consteval {
    std::meta::token_sequence bases;
    for (auto port : park_runtime_binding_detail::ports) {
        if (!bases.empty())
            bases += ^^{ , };
        bases += ^^{
            public
            ${port}
        };
    }
    std::meta::queue_injection(^^{
        class ParkRuntimeBindings final : ${bases} { std::filesystem::path root_;
        std::unordered_map<void*, Allocation> allocations_;
        std::unordered_map<std::string, MeshModelLibrary> libraries_;
        std::optional<MeshSurfaceQuery> surface_;
        std::uint32_t surface_slot_ {};
        AdvisorParticleMotion control_particle_motion_;
        std::optional<std::size_t> inspection_resource_;
        std::uint32_t inspection_slot_ {};
        std::uint64_t inspection_generation_ {}, inspection_binding_revision_ {};
        std::array<float, 3> inspection_saved_focus_ {};
        static void resource_visibility(WorldMeshInstance& instance, std::uint32_t hidden, std::uint32_t visible);
        void camera_notification(WorldRuntime& c);
        void set_camera_controls_visible(bool mode);
        ParticleEffect* effect(std::uint32_t handle);
        MeshModelLibrary* library(const std::filesystem::path& directory);

        public:
        TrackRideManager* track_rides {};
        std::function<void()> resource_pump;
        ParticleRuntime* particles {};
        WorldMeshScene* scene {};
        std::string last_field;
        std::size_t field_transfers {};
        std::vector<StaffInteractionOverlay>* runtime_overlays {};
        ParkConstructionRuntime* construction {};
        LayoutReplayRuntime* layout_replay {};
        LayoutWorldBackend* layout_backend {};
        WorldSoundRuntime* audio {};
        WorldUiRuntime* ui {};
        bool command_families_disabled {};
        std::function<void(bool)> reload_sprite_family;
        WorldUiRuntime& ui_runtime();
        void update_control_particles(WorldRuntime& c);
        void select_camera_ui_roots(WorldRuntime& c, bool secondary);
        void special_camera_terrain(WorldRuntime& c, bool special);
        void finish_camera_mode_transition(WorldRuntime& c);
        void abandon_inspection_for_shutdown(WorldRuntime& c) noexcept;
        void restore_special_camera_modes(WorldRuntime& c, bool clear_previous_poi);
        ParkConstructionRuntime& construction_runtime();
        explicit ParkRuntimeBindings(std::filesystem::path root);

        ~ParkRuntimeBindings() override {
            for (const auto& [pointer, kind] : allocations_) {
                (void)kind;
                ::operator delete(pointer);
            }
        }

        std::optional<std::vector<std::byte>> read_asset(std::string_view name);
        //The ports own the signatures. This host implements their operations;
        //only these shared domain defaults remain inherited.
        consteval {
            using namespace std::meta;
            auto inherited = overloads_of(^^ResourceHostPort::submit_mobile_entity_positioned_template_request);
            inherited.insert(inherited.end(), { ^^EntityEnginePort::online_presence_mobile_state, ^^SaveServices::request_mobile_entity_path, ^^SaveServices::prepare_loaded_payload, ^^SaveServices::transfer_payload });
            std::vector<info> generated;
            std::size_t retained {};
            for (auto base : park_runtime_binding_detail::ports) {
                for (auto function : members_of(base, access_context::current())) {
                    if (!is_function(function) || !is_virtual(function) || is_destructor(function)) {
                        continue;
                    }
                    if (std::ranges::find(inherited, function) != inherited.end()) {
                        ++retained;
                        continue;
                    }
                    const auto previous = std::ranges::find_if(generated, [&](info other) {
                        return identifier_of(other) == identifier_of(function);
                    });
                    if (previous != generated.end()) {
                        if (type_of(*previous) != type_of(function)) {
                            throw "runtime adapter operation has incompatible base signatures";
                        }
                        continue;
                    }
                    generated.push_back(function);
                    //A cloned class-body definition preserves implicit inline.
                    //These implementations live out of line; override checking
                    //enforces their complete signatures and exception contracts.
                    if (!attributes_of(function).empty()) {
                        throw "runtime adapter attributes require an explicit signature policy";
                    }
                    const auto result = return_type_of(function);
                    const auto name = id(identifier_of(function));
                    token_sequence parameters;
                    for (auto parameter : parameters_of(function)) {
                        if (has_default_argument(parameter)) {
                            throw "runtime adapter defaults require an explicit signature policy";
                        }
                        if (!parameters.empty()) {
                            parameters += ^^{ , };
                        }
                        const auto type = type_of(parameter);
                        parameters += ^^{ ${type} };
                    }
                    queue_injection(^^{ ${result} ${name}(${parameters}) override; });
                }
            }
            if (retained != inherited.size()) {
                throw "runtime adapter default is absent from its ports";
            }
        }
        }
        ;
    });
}
} //namespace simcoaster
