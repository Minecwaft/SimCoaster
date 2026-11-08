#include "simcoaster/world_object_ui.hpp"
#include "simcoaster/layout_replay.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"
#include "world_object_access.hpp"
#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace simcoaster {
namespace {
using namespace object_access;

void set_name_component(WorldRuntime& c, ParkObjectRuntimeView& object, std::u16string_view name, bool first) {
    auto& destination = first ? object.name_a : object.name_b;
    std::ranges::fill(destination, char16_t{});
    std::ranges::copy(name, destination.begin());
    auto* primary = c.referenced_world();
    if (!primary)
        throw std::logic_error("Park-object rename primary World is not bound");
    const bool gate = object.base.header.id == primary->saved.park_gates;
    //Native5E7E70/5E7FA0 perform these side effects before duplicate checking.
    if (gate && c.mode.game != GameMode::online) {
        auto& manager = get_or_create_theme_global_info_manager(c);
        if (manager.selected_theme_info) {
            auto& stored = first ? manager.selected_theme_info->sign_name_a : manager.selected_theme_info->sign_name_b;
            std::ranges::fill(stored, char16_t{});
            std::ranges::copy(name, stored.begin());
            manager.selected_theme_info->name_changed = 1;
        }
    }
    if (!c.construction.action_recording_disabled && c.construction.action_record_streams_initialized) {
        if (!c.construction.action_record_stream)
            throw std::logic_error("Park-object rename recording stream is not bound");
        const auto map_id = gate ? std::uint16_t{ 60000 }
                                 : static_cast<std::uint16_t>(((object.base.y >> 8) << 7) + (object.base.x >> 8) + 1u);
        const LayoutAction action{ first ? LayoutActionCode::name_a : LayoutActionCode::name_b,
                                   LayoutMappedName{ map_id, std::u16string{ name } } };
        const auto written = write_layout_action(*c.construction.action_record_stream, action);
        if (!written)
            throw std::runtime_error(describe_layout_error(written.error()));
    }
}

std::array<char16_t, 30> collision_key(const ParkObjectRuntimeView& object) {
    std::array<char16_t, 30> result{};
    std::size_t count{};
    //Native511460/editor: scan exactly15 units from each bank, retain only
    //iswalnum units, concatenate without a delimiter and preserve case.
    for (const auto* bank : { &object.name_a, &object.name_b })
        for (std::size_t i = 0; i < 15; ++i)
            if (std::iswalnum(static_cast<std::wint_t>((*bank)[i])))
                result[count++] = (*bank)[i];
    return result;
}
} //namespace

ParkObjectUiStatusPresentation park_object_ui_status_presentation(ParkObjectUiStatus status) {
    //Executable33-DWORD table9B1C44, read through the native integer tool.
    constexpr std::array<std::uint32_t, 33> text_rows{ 0,   875, 979, 907, 905, 906, 904, 876, 877, 878, 882,
                                                       880, 881, 879, 883, 884, 888, 886, 887, 885, 889, 890,
                                                       894, 892, 893, 891, 895, 898, 899, 900, 896, 897, 908 };
    const auto value = static_cast<std::uint32_t>(status);
    if (value >= text_rows.size())
        throw std::out_of_range("Park-object UI status index");
    //9B1BC0's RGB bytes;502680 ignores stored byte3 and forces alpha255.
    const std::array<std::uint8_t, 4> color = value <= 6 ? std::array<std::uint8_t, 4>{ 0xFF, 0xFF, 0xFF, 0xFF }
                                            : value == 7 || value == 8 || status == ParkObjectUiStatus::dead
                                                ? std::array<std::uint8_t, 4>{ 0xFF, 0x32, 0x1E, 0xFF }
                                            : value <= 26 ? std::array<std::uint8_t, 4>{ 0x50, 0x64, 0xFF, 0xFF }
                                                          : std::array<std::uint8_t, 4>{ 0xFF, 0x96, 0x1E, 0xFF };
    return { text_rows[value], color };
}

std::array<std::u16string, 2> park_object_ui_name_components(const ParkObjectRuntimeView& object) {
    return { component(object.name_a), component(object.name_b) };
}

void
set_park_object_name_component(WorldRuntime& c, ParkObjectRuntimeView& object, std::u16string_view name, bool first) {
    name = name.substr(0, name.find(char16_t{}));
    if (name.size() > 32)
        throw std::length_error("Native park-object name component exceeds32 units");
    set_name_component(c, object, name, first);
}

ParkObjectUiNameCommitResult commit_park_object_ui_name(WorldRuntime& c,
                                                        ParkObjectRuntimeView& object,
                                                        std::u16string_view first,
                                                        std::u16string_view second) {
    if (&record<ParkObjectRuntimeView>(c, object.base.header.id, ThingKind::park_object) != &object)
        throw std::out_of_range("Park-object rename identity was replaced");
    first = first.substr(0, first.find(char16_t{}));
    second = second.substr(0, second.find(char16_t{}));
    if (first.size() > 14 || second.size() > 14)
        throw std::length_error("Original park-object name editor permits14 units per component");
    set_name_component(c, object, first, true);
    set_name_component(c, object, second, false);
    const auto* definition = get_park_object_definition(c, &object);
    if (!definition)
        throw std::out_of_range("Park-object rename definition is missing");
    const auto warning = match (definition->which_ui_type) -> std::optional<std::uint32_t> {
        case ObjectUiCategory::rides      => 719;
        case ObjectUiCategory::shops      => 720;
        case ObjectUiCategory::sideshows  => 721;
        case ObjectUiCategory::features   => 722;
        case ObjectUiCategory::facilities => 723;
        case _                            => std::nullopt;
    };
    const auto key = collision_key(object);
    std::size_t visited{};
    if (warning)
        for (auto* node = c.entities.active_head; node; node = node->active_next) {
            if (++visited > c.entities.size())
                throw std::logic_error("Cyclic active list during park-object rename");
            const auto* header = node->payload;
            if (!header || header->kind != ThingKind::park_object || header->id == object.base.header.id)
                continue;
            const auto& other = *reinterpret_cast<const ParkObjectRuntimeView*>(header);
            const auto* other_definition = get_park_object_definition(c, &other);
            if (other_definition
                && other_definition->which_ui_type == definition->which_ui_type
                && collision_key(other) == key)
                return { false, warning, std::u16string{ first } + u' ' + std::u16string{ second } };
        }
    if (object.mesh_instance_id) {
        if (!c.resources.meshes)
            throw std::logic_error("Park-object rename requires the World mesh owner");
        (void)c.resources.meshes->set_object_sign_caption(c, object, first, second);
    }
    refresh_info_desk_signs_for_renamed_object(c, object.base.header.id);
    return { true, std::nullopt, {} };
}
} //namespace simcoaster
