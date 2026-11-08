#include "object_name_catalog.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/world_object_ui.hpp"
#include "simcoaster/world_research.hpp"
#include "world_object_access.hpp"
#include <algorithm>

namespace simcoaster {
void world_operations::builtin::copy_catalog_object_sign_name(WorldRuntime& c, std::uint16_t id, bool second, std::span<char16_t> destination) {
    if (destination.empty()) {
        return;
    }
    //Native lookup stops at the terminal0 before comparing it. The mapping is
    //authoritative even if its corresponding catalog definition is not loaded.
    const auto mapping = std::ranges::find(object_name_catalog::rows, id, &object_name_catalog::Row::definition_id);
    if (id && mapping != object_name_catalog::rows.end()) {
        auto name = format_bfst_wide_string(c.messages.mail_language.object_names, c.messages.mail_language.conversion, second ? mapping->sign_b : mapping->sign_a, destination.size());
        if (!name) {
            throw std::runtime_error("Object sign name: " + name.error().reason);
        }
        std::ranges::copy(*name, destination.begin());
        destination[name->size()] = 0;
        return; //The formatter leaves the unused tail unchanged.
    }
    auto* definition = find_object_definition_by_id(&c.resources.object_catalog, id);
    if (!definition || !definition->source_asset || !definition->source_asset->configured_text) {
        throw std::logic_error("unmapped object sign has no configured-language text");
    }
    const auto& text = definition->source_asset->configured_text->fields[second ? 2u : 1u];
    //The fallback is wcsncpy: pads to capacity, but does not force a terminator
    //when the configured string reaches/exceeds that capacity.
    const auto count = std::min(destination.size(), text.size());
    std::copy_n(text.begin(), count, destination.begin());
    std::fill(destination.begin() + static_cast<std::ptrdiff_t>(count), destination.end(), char16_t {});
}

std::u16string get_catalog_object_display_name(WorldRuntime& c, std::uint16_t id) {
    const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, id);
    if (!definition) {
        throw std::logic_error("catalog display-name definition");
    }
    if (definition->source_asset && definition->source_asset->configured_text) {
        const auto& name = definition->source_asset->configured_text->fields[0];
        if (name != u"MISSING STRING") {
            return name.substr(0, 64);
        }
    }
    const auto row = std::ranges::find(object_name_catalog::rows, id, &object_name_catalog::Row::definition_id);
    if (row == object_name_catalog::rows.end() || !id) {
        return {};
    }
    const auto text = [&](std::uint16_t entry) {
        auto value = format_bfst_wide_string(c.messages.mail_language.object_names, c.messages.mail_language.conversion, entry, 64);
        if (!value) {
            throw std::runtime_error("Catalog display name: " + value.error().reason);
        }
        return *value;
    };
    auto first = text(row->sign_a);
    const auto second = text(row->sign_b);
    if (!first.empty() && first.back() == u'-') {
        first.pop_back();
    } else if (!second.empty()) {
        first += u' ';
    }
    first += second;
    return first.substr(0, 64);
}

using object_access::component;

std::u16string park_object_ui_display_name(const ParkObjectRuntimeView& object) {
    auto first = component(object.name_a);
    const auto second = component(object.name_b);
    if (first.empty()) {
        return second.substr(0, 32);
    }
    if (first.back() == u'-') {
        first.pop_back();
    } else if (!second.empty()) {
        first += u' ';
    }
    first += second;
    return first.substr(0, 32);
}
} //namespace simcoaster
