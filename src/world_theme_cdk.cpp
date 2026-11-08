#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_save.hpp"
#include "object_name_catalog.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
//Actual CRT initializer0x74AC20 fills this table; its BSS image is zero.
constexpr std::array default_registrations{
    CdkRegistrationRecord{ 1190, 1, 1, 7500 },  CdkRegistrationRecord{ 4150, 1, 0, 7500 },
    CdkRegistrationRecord{ 1185, 1, 0, 9000 },  CdkRegistrationRecord{ 4500, 1, 1, 12000 },
    CdkRegistrationRecord{ 4208, 1, 1, 11000 }, CdkRegistrationRecord{ 4185, 2, 1, 12000 },
    CdkRegistrationRecord{ 2190, 2, 0, 12000 }, CdkRegistrationRecord{ 2180, 2, 1, 14500 },
    CdkRegistrationRecord{ 2123, 2, 1, 12000 }, CdkRegistrationRecord{ 4160, 2, 1, 12000 },
    CdkRegistrationRecord{ 4201, 2, 0, 12000 }, CdkRegistrationRecord{ 4110, 0, 1, 15000 },
    CdkRegistrationRecord{ 4170, 0, 1, 15000 }, CdkRegistrationRecord{ 3185, 0, 0, 13000 },
    CdkRegistrationRecord{ 3190, 0, 1, 15000 }, CdkRegistrationRecord{ 4300, 0, 1, 15000 }
};

CdkRegistrationRecord decode_registration(std::span<const std::byte, 16> bytes) {
    CdkRegistrationRecord record;
    constexpr std::array members{ ^^CdkRegistrationRecord::object_id,
                                  ^^CdkRegistrationRecord::theme_category,
                                  ^^CdkRegistrationRecord::exclude_from_coaster_source_picker,
                                  ^^CdkRegistrationRecord::source_picker_amount };
    std::size_t offset = 0;
    template for (constexpr auto member : members) {
        static_assert(sizeof(record.[:member:]) == 4);
        (void)save::transfer_value(record.[:member:], save::Direction::read, [&](std::span<std::byte> field) {
            std::ranges::copy(bytes.subspan(offset, 4), field.begin());
            offset += 4;
            return true;
        });
    }
    return record;
}

bool valid_registration(const CdkRegistrationRecord& record) {
    const bool valid_id =
        (record.object_id >= 5000 && record.object_id <= 8999) || record.object_id == 4105 || record.object_id == 4112;
    return valid_id
        && record.theme_category >= 0
        && record.theme_category <= 2
        && record.exclude_from_coaster_source_picker <= 1
        && record.source_picker_amount > 0;
}

std::int32_t checked_count(const ThemeGlobalInfoManager& manager) {
    const auto count = std::bit_cast<std::int32_t>(manager.cdk_registration_count);
    if (count < 0)
        throw std::length_error("negative CDK registration count");
    return count;
}
} //namespace

bool has_fallback_object_name_mapping(std::int32_t object_id) {
    //Membership also accepts the native terminal0 row; sign lookup does not.
    return std::ranges::any_of(object_name_catalog::rows, [&](const auto& row) {
        return row.definition_id == object_id;
    });
}

void rebuild_theme_cdk_registrations(MissionWorld& c, ThemeGlobalInfoManager* manager) {
    if (!manager)
        throw std::invalid_argument("null theme manager");
    manager->cdk_registration_count = static_cast<std::uint32_t>(default_registrations.size());
    manager->cdk_registration_cursor = 0;
    std::ranges::copy(default_registrations, manager->cdk_registrations.begin());
    //Preserve enumeration order: the first accepted duplicate ID wins.
    const auto files = c.ports.assets.find_asset_files(c, "data/cdk registration/*.cdk");
    for (const auto& filename : files) {
        if (checked_count(*manager) >= static_cast<std::int32_t>(manager->cdk_registrations.size()))
            break;
        auto stream = c.ports.assets.open_asset_stream(c, std::string{ "data/cdk registration/" } + filename);
        if (!stream)
            continue;
        std::array<std::byte, 16> bytes;
        if (stream->read(bytes) != 16)
            continue; //Exactly one transfer, not the four-field save codec.
        const auto candidate = decode_registration(bytes);
        if (!valid_registration(candidate))
            continue;
        const auto count = checked_count(*manager);
        if (static_cast<std::size_t>(count) > manager->cdk_registrations.size())
            throw std::length_error("CDK registration count exceeds capacity");
        bool duplicate = false;
        for (std::int32_t i = 0; i < count; ++i)
            if (manager->cdk_registrations[static_cast<std::size_t>(i)].object_id == candidate.object_id) {
                duplicate = true;
                break;
            }
        if (duplicate || has_fallback_object_name_mapping(candidate.object_id))
            continue;
        //The native append reloads count after the fallback-table check.
        const auto append = static_cast<std::size_t>(checked_count(*manager));
        if (append >= manager->cdk_registrations.size())
            throw std::length_error("CDK registration capacity exhausted");
        manager->cdk_registrations[append] = candidate;
        ++manager->cdk_registration_count;
        //Stream destruction follows validation/append, including rejected records.
    }
}
} //namespace simcoaster
