#include "simcoaster/application_postcards.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <ctime>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace simcoaster {
namespace {
std::vector<std::string> lines(std::string_view source) {
    std::vector<std::string> result;
    for (std::size_t start = 0; start < source.size();) {
        const auto end = source.find('\n', start);
        auto value = source.substr(start, end == source.npos ? source.size() - start : end - start);
        if (!value.empty() && value.back() == '\r')
            value.remove_suffix(1);
        if (const auto zero = value.find('\0'); zero != value.npos)
            value = value.substr(0, zero);
        result.emplace_back(value);
        if (end == source.npos)
            break;
        start = end + 1;
    }
    return result;
}

std::int32_t integer(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1);
    std::int64_t number{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
    return result.ec == std::errc{} ? fixed::narrow(number) : 0;
}

std::expected<std::string, PostcardError> read_text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::unexpected(PostcardError{ path, "Unable to open original postcard data" });
    std::string contents{ std::istreambuf_iterator<char>(file), {} };
    if (file.bad())
        return std::unexpected(PostcardError{ path, "Unable to read original postcard data" });
    return contents;
}

std::expected<void, PostcardError> write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || !file.write(text.data(), static_cast<std::streamsize>(text.size())))
        return std::unexpected(PostcardError{ path, "Unable to write postcard data" });
    file.close();
    if (!file)
        return std::unexpected(PostcardError{ path, "Unable to close postcard data" });
    return {};
}

std::string record_name(std::uint32_t id, std::string_view extension) {
    return std::format("{:08x}.{}", id, extension);
}

void unique_ids(std::vector<std::uint32_t>& ids) {
    std::ranges::sort(ids);
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

std::expected<void, PostcardError> writable(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::exists(path, error))
        std::filesystem::permissions(path,
                                     std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add,
                                     error);
    if (error)
        return std::unexpected(PostcardError{ path, error.message() });
    return {};
}
} //namespace

bool has_basic_postcard_address_syntax(std::u16string_view address) {
    const auto at = address.find(u'@');
    if (at == address.npos || !at)
        return false;
    const auto dot = address.find(u'.', at);
    return dot != address.npos && dot > at + 1;
}

std::expected<std::vector<std::u16string>, PostcardError> parse_postcard_recipients(std::u16string_view source) {
    if (source.empty())
        return std::unexpected(PostcardError{ {}, "Empty postcard recipients" });
    std::vector<std::u16string> result;
    const auto delimiter = [](char16_t unit) {
        return unit == u';' || unit == u',' || unit == u' ' || unit == u'\n';
    };
    for (std::size_t cursor = 0; cursor < source.size();) {
        while (cursor < source.size() && delimiter(source[cursor]))
            ++cursor;
        const auto begin = cursor;
        while (cursor < source.size() && !delimiter(source[cursor]))
            ++cursor;
        if (begin == cursor)
            continue;
        auto token = source.substr(begin, cursor - begin);
        if (!has_basic_postcard_address_syntax(token))
            return std::unexpected(PostcardError{ {}, "Invalid original postcard recipient syntax" });
        result.emplace_back(token);
    }
    return result;
}

std::expected<PostcardOutboxIndex, PostcardError> decode_postcard_outbox_index(std::string_view source) {
    PostcardOutboxIndex result;
    std::array groups{ &result.records, &result.pending_delete, &result.send_queue };
    std::size_t group = 0;
    for (const auto& line : lines(source)) {
        if (line.empty()) {
            if (++group >= groups.size())
                break;
            continue;
        }
        groups[group]->push_back(std::bit_cast<std::uint32_t>(integer(line)));
    }
    return result;
}

std::string encode_postcard_outbox_index(const PostcardOutboxIndex& index) {
    std::string result;
    for (const auto* group : { &index.records, &index.pending_delete, &index.send_queue }) {
        if (group != &index.records)
            result += '\n';
        for (const auto id : *group)
            result += std::to_string(std::bit_cast<std::int32_t>(id)) + "\n";
    }
    return result;
}

std::expected<PostcardRecord, PostcardError> decode_postcard_record(std::string_view source) {
    const auto data = lines(source);
    if (data.size() < 5)
        return std::unexpected(PostcardError{ {}, "Truncated original numbered postcard record" });
    PostcardRecord result;
    result.date = { integer(data[0]), integer(data[1]), integer(data[2]) };
    result.from = data[3];
    result.subject = data[4];
    result.recipients.assign(data.begin() + 5, data.end());
    return result;
}

std::string encode_postcard_record(const PostcardRecord& record) {
    auto result = std::to_string(record.date.day)
                + "\n"
                + std::to_string(record.date.month)
                + "\n"
                + std::to_string(record.date.year)
                + "\n"
                + record.from
                + "\n"
                + record.subject
                + "\n";
    for (const auto& recipient : record.recipients)
        result += recipient + "\n";
    return result;
}

struct ApplicationPostcards::State {
    std::filesystem::path game_root, profile, outbox;
    std::vector<std::string> addresses;
    PostcardOutboxIndex index;
    std::uint64_t revision{};

    explicit State(std::filesystem::path path) : game_root(std::move(path)) {}
};

ApplicationPostcards::ApplicationPostcards(const std::filesystem::path& path) : state_(std::make_unique<State>(path)) {}

ApplicationPostcards::~ApplicationPostcards() = default;

std::expected<void, PostcardError> ApplicationPostcards::select_profile(const std::filesystem::path& path) {
    if (path.empty())
        return std::unexpected(PostcardError{ path, "Postcard profile path is empty" });
    State selected(state_->game_root);
    selected.profile = path;
    selected.outbox = path / "outbox";
    std::error_code error;
    std::filesystem::create_directories(selected.outbox, error);
    if (error)
        return std::unexpected(PostcardError{ selected.outbox, error.message() });
    const auto book_path = path / "addrbook.dat";
    if (std::filesystem::exists(book_path, error)) {
        auto book = read_text(book_path);
        if (!book)
            return std::unexpected(book.error());
        selected.addresses = lines(*book); //Native load does not sort or deduplicate.
    }
    if (error)
        return std::unexpected(PostcardError{ book_path, error.message() });
    const auto index_path = selected.outbox / "outbox.dat";
    if (std::filesystem::exists(index_path, error)) {
        auto bytes = read_text(index_path);
        if (!bytes)
            return std::unexpected(bytes.error());
        auto index = decode_postcard_outbox_index(*bytes);
        if (!index)
            return std::unexpected(index.error());
        selected.index = std::move(*index);
    }
    if (error)
        return std::unexpected(PostcardError{ index_path, error.message() });
    selected.revision = state_->revision + 1;
    *state_ = std::move(selected);
    return {};
}

void ApplicationPostcards::clear_profile() {
    const auto revision = state_->revision + 1;
    *state_ = State(state_->game_root);
    state_->revision = revision;
}

std::uint64_t ApplicationPostcards::profile_revision() const {
    return state_->revision;
}

const std::filesystem::path& ApplicationPostcards::profile_directory() const {
    return state_->profile;
}

const std::vector<std::string>& ApplicationPostcards::addresses() const {
    return state_->addresses;
}

const PostcardOutboxIndex& ApplicationPostcards::index() const {
    return state_->index;
}

std::size_t ApplicationPostcards::insert_address(std::string_view value) {
    auto& addresses = state_->addresses;
    addresses.emplace_back(value);
    std::ranges::sort(addresses);
    addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());
    return static_cast<std::size_t>(std::ranges::find(addresses, value) - addresses.begin());
}

void ApplicationPostcards::erase_address(std::size_t index) {
    if (index >= state_->addresses.size())
        throw std::out_of_range("Postcard address identity is stale");
    state_->addresses.erase(state_->addresses.begin() + static_cast<std::ptrdiff_t>(index));
}

std::expected<void, PostcardError> ApplicationPostcards::save_profile() {
    if (state_->profile.empty())
        return std::unexpected(PostcardError{ {}, "No active postcard profile" });
    std::string addresses;
    for (const auto& value : state_->addresses)
        addresses += value + "\n";
    auto book = write_text(state_->profile / "addrbook.dat", addresses);
    if (!book)
        return book;
    return write_text(state_->outbox / "outbox.dat", encode_postcard_outbox_index(state_->index));
}

std::expected<PostcardRecord, PostcardError> ApplicationPostcards::read(std::uint32_t id) const {
    if (std::ranges::find(state_->index.records, id) == state_->index.records.end())
        return std::unexpected(PostcardError{ state_->outbox, "Postcard record identity is absent" });
    const auto path = state_->outbox / record_name(id, "dat");
    auto bytes = read_text(path);
    if (!bytes)
        return std::unexpected(bytes.error());
    auto record = decode_postcard_record(*bytes);
    if (!record) {
        record.error().path = path;
        return record;
    }
    record->id = id;
    record->image_path = state_->outbox / record_name(id, "jpg");
    return record;
}

std::expected<std::vector<PostcardRecord>, PostcardError> ApplicationPostcards::records() const {
    std::vector<PostcardRecord> result;
    for (const auto id : state_->index.records) {
        auto record = read(id);
        if (!record)
            return std::unexpected(record.error());
        result.push_back(std::move(*record));
    }
    return result;
}

std::expected<std::uint32_t, PostcardError> ApplicationPostcards::store(PostcardRecord record,
                                                                        const TextureImage& image) {
    if (state_->profile.empty())
        return std::unexpected(PostcardError{ {}, "No active postcard profile" });
    //781080 uses the LAST list node plus1, and0 for an empty outbox.
    record.id = state_->index.records.empty() ? 0u : state_->index.records.back() + 1u;
    if (std::ranges::find(state_->index.records, record.id) != state_->index.records.end())
        return std::unexpected(PostcardError{ state_->outbox, "Original postcard ID range exhausted" });
    const auto data = state_->outbox / record_name(record.id, "dat");
    const auto jpeg = state_->outbox / record_name(record.id, "jpg");
    auto wrote_image = save_postcard_jpeg(jpeg, image);
    if (!wrote_image)
        return std::unexpected(wrote_image.error());
    auto wrote = write_text(data, encode_postcard_record(record));
    if (!wrote)
        return std::unexpected(wrote.error());
    state_->index.records.push_back(record.id);
    state_->index.send_queue.push_back(record.id);
    unique_ids(state_->index.send_queue);
    auto index = write_text(state_->outbox / "outbox.dat", encode_postcard_outbox_index(state_->index));
    if (!index)
        return std::unexpected(index.error());
    return record.id;
}

std::expected<void, PostcardError>
ApplicationPostcards::apply(std::span<const std::pair<std::uint32_t, PostcardRowAction>> actions) {
    for (const auto [id, action] : actions) {
        if (std::ranges::find(state_->index.records, id) == state_->index.records.end())
            return std::unexpected(PostcardError{ state_->outbox, "Outbox selection refers to a retired record" });
        std::erase(state_->index.send_queue, id);
        std::erase(state_->index.pending_delete, id);
        if (action == PostcardRowAction::send)
            state_->index.send_queue.push_back(id);
        else if (action == PostcardRowAction::erase)
            state_->index.pending_delete.push_back(id);
    }
    unique_ids(state_->index.send_queue);
    unique_ids(state_->index.pending_delete);
    return write_text(state_->outbox / "outbox.dat", encode_postcard_outbox_index(state_->index));
}

std::expected<void, PostcardError> ApplicationPostcards::process_deletions() {
    for (const auto id : state_->index.pending_delete) {
        for (const auto extension : { "dat", "jpg" }) {
            const auto path = state_->outbox / record_name(id, extension);
            auto permission = writable(path);
            if (!permission)
                return permission;
            std::error_code error;
            std::filesystem::remove(path, error);
            if (error)
                return std::unexpected(PostcardError{ path, error.message() });
        }
        std::erase(state_->index.send_queue, id);
        std::erase(state_->index.records, id);
    }
    state_->index.pending_delete.clear();
    return write_text(state_->outbox / "outbox.dat", encode_postcard_outbox_index(state_->index));
}

std::expected<void, PostcardError> ApplicationPostcards::sent(std::uint32_t id) {
    if (std::ranges::find(state_->index.records, id) == state_->index.records.end())
        return std::unexpected(PostcardError{ state_->outbox, "Completed postcard no longer belongs to this profile" });
    std::erase(state_->index.send_queue, id);
    state_->index.pending_delete.push_back(id);
    unique_ids(state_->index.pending_delete);
    return process_deletions();
}

std::expected<void, PostcardError> ApplicationPostcards::update_album(const TextureImage& image) {
    const auto album = state_->game_root / "My Postcards";
    std::error_code error;
    std::filesystem::create_directories(album, error);
    if (error)
        return std::unexpected(PostcardError{ album, error.message() });
    const auto filename = [&](std::int32_t n, std::string_view extension) {
        return album / std::format("postcard{:02}.{}", n, extension);
    };
    std::int32_t next = 1;
    for (std::int32_t n = 1; n <= 10; ++n) {
        const auto from = filename(n, "jpg");
        const auto exists = std::filesystem::exists(from, error);
        if (error)
            return std::unexpected(PostcardError{ from, error.message() });
        if (!exists)
            continue;
        if (n != next) {
            auto permission = writable(from);
            if (!permission)
                return permission;
            std::filesystem::rename(from, filename(next, "jpg"), error);
            if (error)
                return std::unexpected(PostcardError{ from, error.message() });
        }
        ++next;
    }
    for (auto n = next; n <= 10; ++n) {
        std::filesystem::copy_file(state_->game_root / "Data/postcard/blankpostcard.jpg",
                                   filename(n, "jpg"),
                                   std::filesystem::copy_options::skip_existing,
                                   error);
        if (error)
            return std::unexpected(PostcardError{ filename(n, "jpg"), error.message() });
    }
    auto permission = writable(filename(10, "jpg"));
    if (!permission)
        return permission;
    std::filesystem::remove(filename(10, "jpg"), error);
    if (error)
        return std::unexpected(PostcardError{ filename(10, "jpg"), error.message() });
    for (std::int32_t n = 9; n >= 1; --n) {
        permission = writable(filename(n, "jpg"));
        if (!permission)
            return permission;
        std::filesystem::rename(filename(n, "jpg"), filename(n + 1, "jpg"), error);
        if (error)
            return std::unexpected(PostcardError{ filename(n, "jpg"), error.message() });
    }
    auto saved = save_postcard_jpeg(filename(1, "jpg"), image);
    if (!saved)
        return saved;
    for (std::int32_t n = 1; n <= 10; ++n) {
        std::filesystem::copy_file(state_->game_root / "Data/postcard" / filename(n, "html").filename(),
                                   filename(n, "html"),
                                   std::filesystem::copy_options::skip_existing,
                                   error);
        if (error)
            return std::unexpected(PostcardError{ filename(n, "html"), error.message() });
    }
    return {};
}
} //namespace simcoaster
