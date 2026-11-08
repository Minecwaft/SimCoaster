#include "simcoaster/object_catalog.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
ResearchCatalogDefinition* first_object_catalog_entry(ObjectCatalogManager* catalog) {
    catalog->iterator_index = 0;
    return catalog->entries[0];
}

ResearchCatalogDefinition* next_object_catalog_entry(ObjectCatalogManager* catalog) {
    if (std::bit_cast<std::int32_t>(catalog->iterator_index) >= std::bit_cast<std::int32_t>(catalog->count - 1))
        return nullptr;
    return catalog->entries[++catalog->iterator_index];
}

ResearchCatalogDefinition* find_object_definition_by_id(ObjectCatalogManager* catalog, std::uint16_t id) {
    if (id == catalog->cached_definition_id)
        return catalog->cached_entry;
    const auto count = std::bit_cast<std::int32_t>(catalog->count);
    if (count <= 0)
        return nullptr;
    if (static_cast<std::size_t>(count) > catalog->entries.size())
        throw std::out_of_range("object catalog count");
    for (std::int32_t i = 0; i < count; ++i) {
        auto* entry = catalog->entries[static_cast<std::size_t>(i)];
        if (entry->id == id) {
            catalog->cached_definition_id = id;
            catalog->cached_entry = entry;
            return entry;
        }
    }
    return nullptr;
}
} //namespace simcoaster
