#pragma once
#include "sound_catalog.hpp"
#include <functional>

namespace simcoaster {
using SoundAssetReader = std::function<std::expected<std::optional<std::vector<std::byte>>, SoundError>(std::string_view)>;

struct SoundBankResource {
    std::string root, name;
    std::uint8_t flags {};
    std::optional<SoundSampleBank> bank;
    std::map<std::uint32_t, std::shared_ptr<const SoundPcmSample>> decoded;
    std::string loaded_path {};
};

struct SoundCatalogAsset {
    std::string path;
    SoundEffectMap effects;
    std::vector<std::shared_ptr<SoundBankResource>> resources; //Native one-based BANK references.
    std::optional<SoundEventNames> event_names;
    const SoundDescriptor* find(std::uint32_t key) const;
};

class SoundCatalogLibrary {
    struct Entry {
        std::shared_ptr<const SoundCatalogAsset> asset;
        std::uint32_t references {};
    };

    SoundAssetReader read_;
    std::string global_root_;
    std::vector<Entry> catalogs_ { 1 }; //Reserved native fallback slot0.
    std::map<std::string, std::weak_ptr<SoundBankResource>, std::less<>> resources_;
    bool prefer_hd_ = true;
    bool dropping_ {};

public:
    SoundCatalogLibrary(SoundAssetReader, std::string global_root);
    std::expected<std::uint32_t, SoundError> load(std::string root, std::string category);
    //Active targets retain their catalog and resources independently
    //of this registration. Releasing a slot cannot invalidate a live voice.
    void release(std::uint32_t slot);
    //Call after retiring target owners as well as before loading new catalogs.
    //Registration survives the caller's release while a target retains it.
    void collect_released();
    std::shared_ptr<const SoundCatalogAsset> catalog(std::uint32_t slot) const;
    std::uint32_t find(std::string_view path) const;
    std::vector<std::string> reference_paths() const;
    std::vector<std::uint32_t> remap(const SoundCatalogReferences&) const;
    std::expected<std::shared_ptr<const SoundPcmSample>, SoundError> sample(const SoundCatalogAsset&, const sound_wire::Sample&);
    std::expected<const SoundSampleData*, SoundError> sample_data(const SoundCatalogAsset&, const sound_wire::Sample&);

    void prefer_hd(bool value) {
        prefer_hd_ = value;
    }

    void set_sample_dropping(bool);

private:
    std::expected<SoundSampleBank*, SoundError> open_bank(SoundBankResource&);
};

//Shared native1664525/1013904223 LCG stream (C64560). Row selection adds
//weights; sample selection compares already-cumulative thresholds. These are
//intentionally different algorithms. Indices stay local to their descriptor.
struct SoundSelectionCursor {
    std::optional<std::size_t> selection, sample;
    std::uint8_t previous_selection = 255, previous_sample = 255;
    std::array<std::uint8_t, 4> keys {}, values {};
    bool payload_present {};
};

struct SoundPlaylistCursor {
    const SoundDescriptor* descriptor {};
    std::uint32_t random {};
    std::size_t next {};
    bool alternating_step {};
};

//Kind10 owns a shared row playlist in its payload. Native reset permutes
//whole42-byte rows; branches continue to address their physical row slots.
std::optional<std::size_t> select_sound_playlist_record(const SoundDescriptor&, SoundPlaylistCursor&, bool repeat);
std::uint32_t next_sound_selection_random(std::uint32_t& state);
std::expected<bool, SoundError> select_sound_record(const SoundDescriptor&, SoundSelectionCursor&, std::uint32_t& random, bool avoid_repeat);
std::expected<bool, SoundError> select_sound_sample(const SoundDescriptor&, SoundSelectionCursor&, std::uint32_t& random, bool avoid_repeat);
std::expected<bool, SoundError> select_sound_conditional_record(const SoundDescriptor&, SoundSelectionCursor&, std::uint32_t& random, std::uint32_t selector);
bool advance_sound_sequential_record(const SoundDescriptor&, SoundSelectionCursor&, bool repeat);
void refresh_sound_selection_keys(const SoundDescriptor&, SoundSelectionCursor&);
std::optional<std::uint32_t> query_sound_selection_value(const SoundDescriptor&, const SoundSelectionCursor&, std::uint32_t mask);
std::uint32_t choose_sound_level(const SoundDescriptor&, SoundSelectionCursor&, std::uint32_t& random);
std::int32_t choose_sound_frequency_parameter(const SoundDescriptor&, SoundSelectionCursor&, std::uint32_t& random);
std::uint32_t choose_sound_interval(const SoundDescriptor&, SoundSelectionCursor&, std::uint32_t& random);
} //namespace simcoaster
