#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_save.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
int transfer_cdk_registration_record(CdkRegistrationRecord& registration, Stream& stream, bool writing) {
    //Native0x74B1F0 has no field headers: four scalar transfers in this order.
    //The explicit member list is the wire contract, independent of host layout.
    constexpr std::array members { ^^CdkRegistrationRecord::object_id, ^^CdkRegistrationRecord::theme_category, ^^CdkRegistrationRecord::exclude_from_coaster_source_picker, ^^CdkRegistrationRecord::source_picker_amount };
    template for (constexpr auto member : members) {
        static_assert(sizeof(registration.[:member:]) == 4);
        if (!save::transfer_value(registration.[:member:], writing ? save::Direction::write : save::Direction::read, [&](std::span<std::byte> bytes) {
                return (writing ? stream.write(bytes) : stream.read(bytes)) == 4;
            })) {
            return 1;
        }
    }
    return 0;
}

void clear_cdk_registration_exclusion_for_researched_object(ThemeGlobalInfoManager* manager, std::int32_t object_id) {
    //Native0x74D740 captures a signed DWORD count and compares full DWORD IDs.
    //Diagnostic strings do not change the registration or iteration cursor.
    const auto count = std::bit_cast<std::int32_t>(manager->cdk_registration_count);
    if (count <= 0) {
        return;
    }
    if (static_cast<std::size_t>(count) > manager->cdk_registrations.size()) {
        throw std::length_error("CDK registration count exceeds capacity");
    }
    for (std::int32_t i = 0; i < count; ++i) {
        auto& registration = manager->cdk_registrations[static_cast<std::size_t>(i)];
        if (registration.object_id == object_id) {
            registration.exclude_from_coaster_source_picker = 0;
            return;
        }
    }
}
} //namespace simcoaster
