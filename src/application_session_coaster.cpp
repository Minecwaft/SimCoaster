#include "simcoaster/entity_identity.hpp"
#include "application_session_internal.hpp"
#include "simcoaster/coaster_construction.hpp"
#include "simcoaster/file_platform.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_object_ui.hpp"
#include "simcoaster/world_save.hpp"
#include <format>
#include <algorithm>
#include <fstream>
#include <limits>

namespace simcoaster {
using namespace application_session_detail;

namespace {
struct DesignStream final : Stream {
    std::vector<std::byte> bytes;

    std::int32_t read(std::span<std::byte>) override {
        return 0;
    }

    std::int32_t write(std::span<const std::byte> input) override {
        if (input.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            return -1;
        }
        bytes.insert(bytes.end(), input.begin(), input.end());
        return static_cast<std::int32_t>(input.size());
    }
};
} //namespace

void ApplicationSession::State::save_coaster(const ApplicationSaveCoasterRequest& request) {
    validate_file(request, ApplicationFileKind::coaster);
    if (!park || !request.coaster || request.coaster >= park->context().entities.size()) {
        fail(ApplicationSessionFailure::state, {}, "Coaster save target is absent");
    }
    auto& c = park->context();
    const EntityIdentity target { request.coaster, request.coaster_identity, request.coaster_generation };
    auto* payload = target.live_payload<ParkObjectRuntimeView>(c.entities);
    if (!payload) {
        fail(ApplicationSessionFailure::state, {}, "Coaster save target was retired");
    }
    auto& object = *payload;
    if (object.creation_file_time != request.coaster_generation) {
        fail(ApplicationSessionFailure::state, {}, "Coaster save target ID was reused");
    }
    const auto* opening = world_operations::find_mesh_runtime_opening_state(c, object.mesh_instance_id);
    const auto* coasters = park->meshes().coasters();
    const auto* binding = coasters ? coasters->find(object.mesh_instance_id) : nullptr;
    if (!opening || (opening->placement_flags & 3u) != 1u || opening->geometry_peer_reference_count || !binding) {
        fail(ApplicationSessionFailure::save, {}, "Coaster no longer meets the native complete-design save contract");
    }
    const auto destination = root / "Data/my coasters" / std::filesystem::path(request.basename + u".cos");
    if (std::filesystem::exists(destination) && (!request.overwrite || !request.file_identity)) {
        fail(ApplicationSessionFailure::save, destination, "Existing coaster needs its confirmed file identity");
    }
    auto design = export_coaster_design(c, park->meshes(), binding->map_id);
    if (!design) {
        fail(ApplicationSessionFailure::save, destination, describe_mesh_error(design.error()));
    }
    if (request.naming) {
        for (std::size_t index = 0; index < request.naming->components.size(); ++index) {
            if (request.naming->components[index].size() > 14 || request.naming->components[index].find(char16_t {}) != std::u16string::npos) {
                fail(ApplicationSessionFailure::save, destination, "Coaster name exceeds the original editor capacity");
            }
            auto& name = index ? design->name_b : design->name_a;
            name.fill(0);
            std::ranges::transform(request.naming->components[index], name.begin(), [](char16_t value) {
                return static_cast<std::uint16_t>(value);
            });
        }
    }
    DesignStream output;
    const auto encoded = write_coaster_design(output, *design);
    if (!encoded) {
        fail(ApplicationSessionFailure::save, destination, std::format("Cannot serialize complete coaster design at record {}", encoded.error().record));
    }
    FileTransaction transaction(root);
    {
        const auto staged = transaction.stage(destination);
        std::ofstream file(staged, std::ios::binary | std::ios::trunc);
        if (!file.write(reinterpret_cast<const char*>(output.bytes.data()), static_cast<std::streamsize>(output.bytes.size())) || !file.flush()) {
            fail(ApplicationSessionFailure::save, staged, "Cannot write complete coaster design");
        }
    }
    transaction.commit();
    if (request.naming) {
        set_park_object_name_component(c, object, request.naming->components[0], true);
        set_park_object_name_component(c, object, request.naming->components[1], false);
        //5313C6..5313ED refreshes signs on immediate save. Deferred overwrite
        //531490 performs only the raw name setters and COS serialization.
        if (request.naming->refresh_sign) {
            (void)park->meshes().set_object_sign_caption(c, object, request.naming->components[0], request.naming->components[1]);
        }
    }
}

void ApplicationSession::State::delete_file(const ApplicationFileSelection& request, ApplicationFileKind kind) {
    validate_file(request, kind);
    if (!request.file_identity) {
        fail(ApplicationSessionFailure::stale_file, {}, "Deletion needs its selected file identity");
    }
    const auto selected_file = files.at(*request.file_identity).path;
    if (kind == ApplicationFileKind::coaster) {
        if (auto removal = file_platform::remove_file(selected_file); !removal) {
            fail(ApplicationSessionFailure::remove, selected_file, std::format("Cannot delete coaster file: {}", removal.error()));
        }
        files.erase(*request.file_identity);
        return;
    }
    const auto world = source_file(active, request.basename);
    //744EF0 short-circuits after TPWS failure. The following GMS deletion is
    //deliberately separate: a partial pair deletion must report its real error.
    if (auto removal = file_platform::remove_file(world); !removal) {
        fail(ApplicationSessionFailure::remove, world, std::format("Cannot delete park file: {}", removal.error()));
    }
    files.erase(*request.file_identity);
    if (auto removal = file_platform::remove_file(selected_file); !removal) {
        fail(ApplicationSessionFailure::remove, selected_file, std::format("Park deleted but metadata deletion failed: {}", removal.error()));
    }
}
} //namespace simcoaster
