#pragma once
#include "texture_image.hpp"
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace simcoaster {
struct PostcardError {
    std::filesystem::path path;
    std::string reason;
};

struct PostcardDate {
    std::int32_t day {}, month {}, year {};
};

struct PostcardRecord {
    std::uint32_t id {};
    PostcardDate date;
    std::string from, subject;
    std::vector<std::string> recipients;
    std::filesystem::path image_path;
};
enum class PostcardRowAction {
    none,
    send,
    erase
};

struct PostcardOutboxIndex {
    std::vector<std::uint32_t> records, pending_delete, send_queue;
};

bool has_basic_postcard_address_syntax(std::u16string_view);
//A nonempty delimiter-only input is accepted by the original UI and produces
//an empty recipient list. The actual sender separately rejects that list.
std::expected<std::vector<std::u16string>, PostcardError> parse_postcard_recipients(std::u16string_view);
std::string postcard_to_ansi(std::u16string_view, std::optional<std::size_t> byte_limit = {});
std::u16string postcard_from_ansi(std::string_view);
std::expected<TextureImage, PostcardError> load_postcard_jpeg(const std::filesystem::path&);
std::expected<void, PostcardError> save_postcard_jpeg(const std::filesystem::path&, const TextureImage&);
std::expected<PostcardOutboxIndex, PostcardError> decode_postcard_outbox_index(std::string_view);
std::string encode_postcard_outbox_index(const PostcardOutboxIndex&);
std::expected<PostcardRecord, PostcardError> decode_postcard_record(std::string_view);
std::string encode_postcard_record(const PostcardRecord&);

//Profile-scoped genuine native addrbook.dat and outbox/{outbox.dat,ID.dat,
//ID.jpg}. The session selects this owner at the same boundary as its profile.
//No transport operation is implicit in reading or storing an outbox record.
class ApplicationPostcards {
    struct State;
    std::unique_ptr<State> state_;

public:
    explicit ApplicationPostcards(const std::filesystem::path& game_root);
    ~ApplicationPostcards();
    std::expected<void, PostcardError> select_profile(const std::filesystem::path&);
    std::expected<void, PostcardError> save_profile();
    void clear_profile();
    std::uint64_t profile_revision() const;
    const std::filesystem::path& profile_directory() const;
    const std::vector<std::string>& addresses() const;
    std::size_t insert_address(std::string_view);
    void erase_address(std::size_t);
    const PostcardOutboxIndex& index() const;
    std::expected<PostcardRecord, PostcardError> read(std::uint32_t) const;
    std::expected<std::vector<PostcardRecord>, PostcardError> records() const;
    std::expected<std::uint32_t, PostcardError> store(PostcardRecord, const TextureImage&);
    std::expected<void, PostcardError> apply(std::span<const std::pair<std::uint32_t, PostcardRowAction>>);
    std::expected<void, PostcardError> process_deletions();
    //Only the real transport's confirmed successful completion calls sent().
    std::expected<void, PostcardError> sent(std::uint32_t);
    std::expected<void, PostcardError> update_album(const TextureImage& hires);
};
} //namespace simcoaster
