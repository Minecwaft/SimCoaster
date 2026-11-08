#include "simcoaster/sound_library.hpp"
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
std::string rooted(std::string root, std::string_view leaf) {
    if (!root.empty() && root.back() != '/' && root.back() != '\\') {
        root += '/';
    }
    root += leaf;
    return root;
}

SoundError located(SoundError error, std::string_view path) {
    error.resource = path;
    return error;
}

void drop_samples(const SoundCatalogAsset& asset, bool enabled) {
    for (const auto& group : asset.effects.groups) {
        for (const auto& descriptor : group.descriptors) {
            for (const auto& row : descriptor.selections) {
                auto& parameters = row.parameters;
                if (!enabled) {
                    parameters.sample_count = parameters.unfiltered_count;
                    parameters.unfiltered_count = 0;
                    continue;
                }
                parameters.unfiltered_count = parameters.sample_count;
                std::size_t active = parameters.sample_count, index {};
                while (index < active) {
                    const auto reference = row.samples[index].bank_reference;
                    if (reference && reference <= asset.resources.size() && (asset.resources[reference - 1]->flags & static_cast<std::uint8_t>(SoundBankFlags::droppable))) {
                        --active;
                        std::swap(row.samples[index], row.samples[active]);
                    } else {
                        ++index;
                    }
                }
                parameters.sample_count = static_cast<std::uint16_t>(active);
            }
        }
    }
}
} //namespace

SoundCatalogLibrary::SoundCatalogLibrary(SoundAssetReader reader, std::string global_root) : read_(std::move(reader)), global_root_(std::move(global_root)) {
    if (!read_) {
        throw std::invalid_argument("Sound asset reader is required");
    }
}

const SoundDescriptor* SoundCatalogAsset::find(std::uint32_t key) const {
    //Positioned requests address the first group, exactly as catalog_entry.data.
    return effects.groups.empty() ? nullptr : effects.groups.front().find(key);
}

std::expected<std::uint32_t, SoundError> SoundCatalogLibrary::load(std::string root, std::string category) {
    collect_released();
    const auto path = rooted(root, category);
    if (const auto existing = find(path)) {
        auto& references = catalogs_[existing].references;
        if (references == std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected(SoundError { SoundFailure::count, 0, path });
        }
        ++references;
        return existing;
    }
    const auto read_map = [&](std::string_view suffix) -> std::expected<std::vector<std::byte>, SoundError> {
        const auto filename = path + std::string { suffix };
        auto bytes = read_(filename);
        if (!bytes) {
            return std::unexpected(located(bytes.error(), filename));
        }
        if (!*bytes) {
            return std::unexpected(SoundError { SoundFailure::missing, 0, filename });
        }
        return std::move(**bytes);
    };
    auto bank_bytes = read_map("BANK.map");
    if (!bank_bytes) {
        return std::unexpected(bank_bytes.error());
    }
    auto banks = parse_sound_bank_map(*bank_bytes);
    if (!banks) {
        return std::unexpected(located(banks.error(), path + "BANK.map"));
    }
    auto effect_bytes = read_map("SFX.map");
    if (!effect_bytes) {
        return std::unexpected(effect_bytes.error());
    }
    auto effects = parse_sound_effect_map(*effect_bytes);
    if (!effects) {
        return std::unexpected(located(effects.error(), path + "SFX.map"));
    }
    for (const auto& group : effects->groups) {
        for (const auto& descriptor : group.descriptors) {
            for (const auto& selection : descriptor.selections) {
                for (const auto& sample : selection.samples) {
                    if (sample.bank_reference > banks->sources.size()) {
                        return std::unexpected(SoundError { SoundFailure::index, 0, path + "SFX.map" });
                    }
                }
            }
        }
    }
    auto result = std::make_shared<SoundCatalogAsset>();
    result->path = path;
    result->effects = std::move(*effects);
    //Stage ownership before publishing the slot, so a malformed sidecar never
    //leaves a partially registered catalog/resource list as the original did.
    std::vector<std::pair<std::string, std::shared_ptr<SoundBankResource>>> pending;
    for (const auto& source : banks->sources) {
        const auto& bank_root = (source.flags & static_cast<std::uint8_t>(SoundBankFlags::global_root)) ? global_root_ : root;
        const auto key = rooted(bank_root, source.name);
        std::shared_ptr<SoundBankResource> resource;
        if (const auto found = resources_.find(key); found != resources_.end()) {
            resource = found->second.lock();
        }
        if (!resource) {
            for (const auto& entry : pending) {
                if (entry.first == key) {
                    resource = entry.second;
                    break;
                }
            }
            if (!resource) {
                resource = std::make_shared<SoundBankResource>();
                resource->root = bank_root;
                resource->name = source.name;
                resource->flags = source.flags;
                pending.emplace_back(key, resource);
            }
        }
        result->resources.push_back(std::move(resource));
    }
    auto names = read_(path + ".ply");
    if (!names) {
        return std::unexpected(located(names.error(), path + ".ply"));
    }
    if (*names) {
        const auto& bytes = **names;
        auto parsed = parse_sound_event_names({ reinterpret_cast<const char*>(bytes.data()), bytes.size() });
        if (!parsed) {
            return std::unexpected(located(parsed.error(), path + ".ply"));
        }
        result->event_names = std::move(*parsed);
    }
    std::size_t slot = 1;
    while (slot < catalogs_.size() && catalogs_[slot].asset) {
        ++slot;
    }
    if (slot > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(SoundError { SoundFailure::count, 0, path });
    }
    if (slot == catalogs_.size()) {
        catalogs_.emplace_back();
    }
    if (dropping_) {
        drop_samples(*result, true);
    }
    for (auto& [key, resource] : pending) {
        resources_[key] = resource;
    }
    catalogs_[slot] = { std::move(result), 1 };
    return static_cast<std::uint32_t>(slot);
}

void SoundCatalogLibrary::set_sample_dropping(bool enabled) {
    if (enabled == dropping_) {
        return;
    }
    for (const auto& entry : catalogs_) {
        if (entry.asset) {
            drop_samples(*entry.asset, enabled);
        }
    }
    dropping_ = enabled;
}

void SoundCatalogLibrary::release(std::uint32_t slot) {
    if (!slot || slot >= catalogs_.size() || !catalogs_[slot].asset) {
        return;
    }
    if (catalogs_[slot].references) {
        --catalogs_[slot].references;
    }
    collect_released();
}

void SoundCatalogLibrary::collect_released() {
    for (auto& entry : catalogs_) {
        if (entry.asset && !entry.references && entry.asset.use_count() == 1) {
            entry = {};
        }
    }
    for (auto it = resources_.begin(); it != resources_.end();) {
        if (it->second.expired()) {
            it = resources_.erase(it);
        } else {
            ++it;
        }
    }
}

std::shared_ptr<const SoundCatalogAsset> SoundCatalogLibrary::catalog(std::uint32_t slot) const {
    return slot < catalogs_.size() ? catalogs_[slot].asset : nullptr;
}

std::uint32_t SoundCatalogLibrary::find(std::string_view path) const {
    for (std::size_t i = 1; i < catalogs_.size(); ++i) {
        if (catalogs_[i].asset && catalogs_[i].asset->path == path) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return 0;
}

std::vector<std::string> SoundCatalogLibrary::reference_paths() const {
    std::vector<std::string> result;
    result.reserve(catalogs_.size());
    for (const auto& entry : catalogs_) {
        result.push_back(entry.asset ? entry.asset->path : std::string {});
    }
    return result;
}

std::vector<std::uint32_t> SoundCatalogLibrary::remap(const SoundCatalogReferences& references) const {
    std::vector<std::uint32_t> result;
    result.reserve(references.paths.size());
    for (const auto& path : references.paths) {
        result.push_back(path.empty() ? 0 : find(path));
    }
    return result;
}

std::expected<SoundSampleBank*, SoundError> SoundCatalogLibrary::open_bank(SoundBankResource& resource) {
    if (resource.bank) {
        return &*resource.bank;
    }
    //The native hardware SoundFont source falls back to the same SDT when that
    //backend cannot create a source. Native x64 uses the software SDT resource.
    //Preserve HD/HW preference and the exact alternate-suffix search order.
    const std::array<std::string_view, 2> suffixes = prefer_hd_ ? std::array<std::string_view, 2> { "HD.sdt", "HW.sdt" } : std::array<std::string_view, 2> { "HW.sdt", "HD.sdt" };
    for (const auto suffix : suffixes) {
        const auto path = rooted(resource.root, resource.name + std::string { suffix });
        auto bytes = read_(path);
        if (!bytes) {
            return std::unexpected(located(bytes.error(), path));
        }
        if (!*bytes) {
            continue;
        }
        auto bank = parse_sound_sample_bank(std::move(**bytes));
        if (!bank) {
            return std::unexpected(located(bank.error(), path));
        }
        resource.bank = std::move(*bank);
        resource.loaded_path = path;
        return &*resource.bank;
    }
    return std::unexpected(SoundError { SoundFailure::missing, 0, rooted(resource.root, resource.name + std::string { suffixes.front() }) });
}

std::expected<const SoundSampleData*, SoundError> SoundCatalogLibrary::sample_data(const SoundCatalogAsset& catalog_asset, const sound_wire::Sample& sample_record) {
    if (!sample_record.bank_reference || sample_record.bank_reference > catalog_asset.resources.size()) {
        return std::unexpected(SoundError { SoundFailure::index, sample_record.bank_reference, catalog_asset.path, sample_record.token });
    }
    auto& resource = *catalog_asset.resources[sample_record.bank_reference - 1];
    auto bank = open_bank(resource);
    if (!bank) {
        auto error = bank.error();
        error.sample_token = sample_record.token;
        return std::unexpected(std::move(error));
    }
    const auto record = (*bank)->sample(sample_record.token);
    if (!record) {
        return std::unexpected(located(record.error(), resource.loaded_path));
    }
    return *record;
}

std::expected<std::shared_ptr<const SoundPcmSample>, SoundError> SoundCatalogLibrary::sample(const SoundCatalogAsset& catalog_asset, const sound_wire::Sample& record) {
    auto data = sample_data(catalog_asset, record);
    if (!data) {
        return std::unexpected(data.error());
    }
    auto& resource = *catalog_asset.resources[record.bank_reference - 1];
    if (const auto found = resource.decoded.find(record.token); found != resource.decoded.end()) {
        return found->second;
    }
    auto decoded = decode_sound_sample(*resource.bank, record.token);
    if (!decoded) {
        return std::unexpected(located(decoded.error(), resource.loaded_path));
    }
    auto owned = std::make_shared<const SoundPcmSample>(std::move(*decoded));
    resource.decoded.emplace(record.token, owned);
    return owned;
}
} //namespace simcoaster
