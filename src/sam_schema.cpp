#include "simcoaster/sam_schema.hpp"
#include "simcoaster/advisor_balance.hpp"
#include "simcoaster/challenge_balance.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/dynamic_texture_balance.hpp"
#include "simcoaster/guest_flow_balance.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/park_object_balance.hpp"
#include "simcoaster/person_balance.hpp"
#include "simcoaster/ride_description_balance.hpp"
#include "simcoaster/sam_configuration.hpp"
#include "simcoaster/staff_balance.hpp"
#include "simcoaster/standard_balance.hpp"
#include "simcoaster/world_sound_runtime.hpp"
#include "simcoaster/world_startup.hpp"
#include "world_research_balance.hpp"
#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace simcoaster {
namespace {
template <class T, bool Integer = false>
void append(std::vector<NamedSamContract>& out, const char* schema) {
    static constexpr auto fields = std::define_static_array(compiled_sam_contracts<T, Integer>());
    for (const auto& field : fields) {
        out.push_back({ schema, field });
    }
}

bool same(const SamFieldContract& a, const SamFieldContract& b) {
    return a.kind == b.kind && a.minimum == b.minimum && a.maximum == b.maximum && a.lower == b.lower && a.upper == b.upper && a.capacity == b.capacity && a.indexed == b.indexed && a.subtype == b.subtype && a.unsigned_storage == b.unsigned_storage;
}
} //namespace

std::span<const NamedSamContract> sam_schema_catalog() {
    static const auto data = [] {
        std::vector<NamedSamContract> fields;
        constexpr auto world = "world_balance_descriptors";
        append<PersonBalance>(fields, world);
        append<PersonCreationBalance>(fields, world);
        append<PersonEnvironmentBalance>(fields, world);
        append<ArrivalBalanceFile>(fields, world);
        append<StaffRecruitmentBalance>(fields, world);
        append<GuestFlowBalance>(fields, world);
        append<ParkObjectCreationBalance>(fields, world);
        append<ChallengeBalance>(fields, world);
        append<WorldStartupBalance>(fields, world);
        append<EconomyStartup>(fields, world);
        append<research_balance_detail::Balance>(fields, world);
        append<ThemeRenderBalance>(fields, world);
        append<StandardSupplementBalance>(fields, world);
        append<StandardUiBalance>(fields, world);
        append<StandardLobbyBalance>(fields, world);
        append<WorldReverbBalance>(fields, world);
        append<StandardCameraInputBalance>(fields, world);
        append<StandardTipBalance>(fields, world);
        append<WeatherParameters, true>(fields, world);
        append<WeatherEffectsParameters, true>(fields, world);
        append<StandardSeasons, true>(fields, world);
        append<AdvisorBalance>(fields, "advisor_balance_descriptors");
        append<ChallengeDefinitions>(fields, "challenge_sam_descriptors");
        append<ConfigSamBalance>(fields, "config_sam_descriptors");
        append<ThemeGlobalBalance, true>(fields, "theme_global_balance_sam_descriptors");
        append<HoardingBalance>(fields, "hoarding_clicks_balance_descriptors");
        append<MissionBalance, true>(fields, "mission_sam_descriptors");
        append<OnlineSamBalance>(fields, "online_settings_descriptors");
        append<ApplicationHardwareProfile>(fields, "graphics_game_profile_sam_descriptors");
        append<PredefinedCoasterBalance>(fields, "prebuilt_pylon_balance_descriptors");
        append<ResolutionSamBalance>(fields, "resolution_settings_descriptors");
        append<ObjectDefinitionBalance>(fields, "object_definition_balance_descriptors");
        append<WorldSoundBalance>(fields, "sound_settings_descriptors");
        append<RideDescriptionBalance>(fields, "ride_description_balance_descriptors");
        constexpr auto dynamic = "generic_dynamic_texture_descriptors";
        append<DynamicTextureMeshBalance>(fields, dynamic);
        for (const auto& field : coaster_mesh_sam_contracts()) {
            fields.push_back({ dynamic, field });
        }
        for (const auto& field : coaster_collision_sam_contracts()) {
            fields.push_back({ dynamic, field });
        }
        const auto key = [](const auto& f) {
            return std::tuple { std::string_view { f.schema }, std::string_view { f.field.group }, std::string_view { f.field.name } };
        };
        std::ranges::sort(fields, [&](const auto& a, const auto& b) {
            return key(a) < key(b);
        });
        auto output = fields.begin();
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            if (output != fields.begin() && key(*(output - 1)) == key(*it)) {
                if (!same((output - 1)->field, it->field)) {
                    throw std::logic_error("Conflicting compiled SAM projections: " + std::string { it->schema } + " " + it->field.group + "." + it->field.name);
                }
            } else {
                *output++ = *it;
            }
        }
        fields.erase(output, fields.end());
        return fields;
    }();
    return data;
}
} //namespace simcoaster
