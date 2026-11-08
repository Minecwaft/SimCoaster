#pragma once
#include <array>
#include <cstdint>

namespace simcoaster::tag_catalog {
enum class Format : std::int32_t {
    literal = 0,
    park_object_name = 2,
    catalog_object_name = 3
};

//Original populated CTagTable at10302592:130 records, each40 bytes.
//Retain fields read by the handler. Derive a checked direct index at compile
//time because native physical rows are not ordered by their tag IDs.
struct Row {
    std::int32_t tag, target, body, dedupe_mode;
    std::uint16_t expiry;
    Format format;
    std::int32_t sender, subject;
    std::uint16_t counter_slot;
};

inline constexpr std::array<Row, 130> rows { {
    { 0, 0, 0, 0, 0, Format::literal, 192, 197, 0 },
    { 1, 1, 1, 0, 0, Format::literal, 192, 198, 0 },
    { 2, 2, 2, 0, 0, Format::literal, 192, 199, 0 },
    { 3, 3, 3, 0, 0, Format::literal, 192, 200, 0 },
    { 4, 4, 4, 0, 0, Format::literal, 192, 201, 0 },
    { 5, 5, 5, 0, 0, Format::literal, 192, 202, 0 },
    { 6, 6, 18, 19, 0, Format::park_object_name, 192, 215, 2 },
    { 7, 7, 18, 19, 0, Format::park_object_name, 192, 215, 2 },
    { 8, 6, 19, 19, 0, Format::park_object_name, 192, 216, 2 },
    { 9, 6, 20, 19, 0, Format::park_object_name, 192, 216, 2 },
    { 10, 10, 21, 1, 0, Format::literal, 192, 217, 0 },
    { 11, 11, 22, 1, 0, Format::literal, 192, 217, 0 },
    { 12, 12, 23, 1, 0, Format::literal, 192, 217, 0 },
    { 13, 13, 24, 1, 0, Format::literal, 192, 217, 0 },
    { 14, 14, 26, 6, 0, Format::catalog_object_name, 192, 219, 0 },
    { 15, 15, 25, 6, 0, Format::catalog_object_name, 192, 218, 0 },
    { 16, 16, 27, 7, 0, Format::catalog_object_name, 192, 220, 0 },
    { 17, 17, 28, 8, 0, Format::catalog_object_name, 192, 221, 0 },
    { 18, 18, 29, 9, 0, Format::catalog_object_name, 192, 222, 0 },
    { 19, 19, 113, 10, 0, Format::catalog_object_name, 192, 237, 0 },
    { 20, 20, 123, 13, 0, Format::literal, 192, 245, 0 },
    { 21, 21, 33, 15, 180, Format::literal, 192, 226, 0 },
    { 22, 22, 34, 15, 0, Format::literal, 192, 227, 0 },
    { 23, 23, 35, 0, 0, Format::literal, 192, 228, 0 },
    { 24, 24, 36, 0, 0, Format::literal, 192, 228, 0 },
    { 25, 25, 37, 0, 0, Format::literal, 192, 228, 0 },
    { 26, 26, 106, 15, 0, Format::literal, 192, 227, 0 },
    { 27, 27, 30, 1, 180, Format::park_object_name, 192, 223, 2 },
    { 28, 28, 38, 1, 180, Format::park_object_name, 192, 229, 0 },
    { 29, 151, 39, 0, 0, Format::literal, 192, 230, 0 },
    { 30, 151, 41, 0, 0, Format::literal, 192, 232, 0 },
    { 31, 151, 40, 0, 0, Format::literal, 192, 231, 0 },
    { 32, 32, 6, 0, 0, Format::literal, 192, 203, 0 },
    { 33, 33, 7, 0, 0, Format::literal, 192, 204, 0 },
    { 34, 34, 8, 0, 0, Format::literal, 192, 205, 0 },
    { 35, 35, 9, 0, 0, Format::literal, 192, 206, 0 },
    { 36, 36, 10, 0, 0, Format::literal, 192, 207, 0 },
    { 37, 37, 11, 0, 0, Format::literal, 192, 208, 0 },
    { 38, 38, 12, 0, 0, Format::literal, 192, 209, 0 },
    { 39, 39, 13, 0, 0, Format::literal, 192, 210, 0 },
    { 40, 40, 14, 0, 0, Format::literal, 192, 211, 0 },
    { 41, 41, 15, 0, 0, Format::literal, 192, 212, 0 },
    { 42, 42, 16, 0, 0, Format::literal, 192, 213, 0 },
    { 43, 43, 17, 0, 0, Format::literal, 192, 214, 0 },
    { 44, 44, 109, 17, 0, Format::park_object_name, 192, 236, 2 },
    { 45, 45, 110, 1, 0, Format::park_object_name, 192, 236, 2 },
    { 46, 46, 111, 17, 0, Format::literal, 192, 236, 2 },
    { 47, 47, 112, 1, 0, Format::literal, 192, 236, 2 },
    { 48, 48, 122, 1, 0, Format::park_object_name, 192, 244, 2 },
    { 49, 49, 31, 20, 120, Format::literal, 192, 224, 2 },
    { 50, 50, 32, 20, 120, Format::literal, 192, 225, 2 },
    { 51, 51, 114, 20, 120, Format::literal, 192, 238, 2 },
    { 52, 52, 115, 20, 120, Format::literal, 192, 239, 2 },
    { 53, 53, 116, 20, 120, Format::literal, 192, 239, 2 },
    { 54, 54, 117, 20, 120, Format::literal, 192, 240, 2 },
    { 55, 55, 118, 20, 120, Format::literal, 192, 240, 2 },
    { 56, 56, 119, 20, 120, Format::literal, 192, 241, 2 },
    { 57, 57, 120, 20, 120, Format::literal, 192, 242, 2 },
    { 58, 58, 121, 20, 120, Format::literal, 192, 243, 2 },
    { 59, 151, 124, 0, 0, Format::literal, 192, 246, 0 },
    { 60, 151, 125, 0, 0, Format::literal, 192, 247, 0 },
    { 61, 151, 126, 0, 0, Format::literal, 192, 247, 0 },
    { 62, 151, 127, 0, 0, Format::literal, 192, 247, 0 },
    { 63, 151, 128, 0, 0, Format::literal, 192, 247, 0 },
    { 64, 151, 129, 0, 0, Format::literal, 192, 247, 0 },
    { 65, 151, 130, 0, 0, Format::literal, 192, 247, 0 },
    { 66, 151, 131, 0, 0, Format::literal, 192, 247, 0 },
    { 67, 151, 132, 0, 0, Format::literal, 192, 247, 0 },
    { 68, 151, 133, 0, 0, Format::literal, 192, 247, 0 },
    { 69, 151, 154, 0, 0, Format::literal, 192, 247, 0 },
    { 70, 151, 155, 0, 0, Format::literal, 192, 247, 0 },
    { 71, 151, 156, 0, 0, Format::literal, 192, 247, 0 },
    { 72, 72, 134, 1, 0, Format::park_object_name, 192, 248, 0 },
    { 73, 73, 135, 1, 0, Format::park_object_name, 192, 249, 0 },
    { 74, 74, 136, 1, 0, Format::park_object_name, 192, 248, 0 },
    { 75, 75, 137, 1, 0, Format::park_object_name, 192, 249, 0 },
    { 76, 76, 138, 1, 0, Format::park_object_name, 192, 248, 0 },
    { 77, 77, 139, 1, 0, Format::park_object_name, 192, 249, 0 },
    { 78, 78, 140, 1, 0, Format::park_object_name, 192, 248, 0 },
    { 79, 79, 141, 1, 0, Format::park_object_name, 192, 249, 0 },
    { 80, 80, 142, 1, 0, Format::park_object_name, 192, 248, 0 },
    { 81, 81, 143, 0, 0, Format::literal, 192, 248, 0 },
    { 82, 82, 144, 0, 0, Format::literal, 192, 249, 0 },
    { 83, 83, 145, 0, 0, Format::literal, 192, 248, 0 },
    { 84, 84, 146, 0, 0, Format::literal, 192, 249, 0 },
    { 85, 85, 147, 0, 0, Format::literal, 192, 248, 0 },
    { 86, 86, 148, 0, 0, Format::literal, 192, 249, 0 },
    { 87, 87, 149, 0, 0, Format::literal, 192, 248, 0 },
    { 88, 88, 150, 0, 0, Format::literal, 192, 249, 0 },
    { 89, 89, 151, 0, 0, Format::literal, 192, 248, 0 },
    { 90, 90, 152, 0, 180, Format::literal, 192, 248, 0 },
    { 91, 91, 153, 0, 180, Format::literal, 192, 249, 0 },
    { 92, 151, 162, 0, 0, Format::literal, 174, 161, 0 },
    { 93, 151, 164, 0, 0, Format::literal, 176, 163, 0 },
    { 94, 151, 166, 0, 0, Format::literal, 192, 193, 0 },
    { 95, 151, 167, 0, 0, Format::literal, 192, 193, 0 },
    { 96, 96, 157, 0, 0, Format::literal, 192, 250, 0 },
    { 97, 97, 158, 0, 0, Format::literal, 192, 250, 0 },
    { 98, 98, 159, 0, 0, Format::literal, 192, 250, 0 },
    { 99, 99, 160, 0, 0, Format::literal, 192, 250, 0 },
    { 100, 100, 19, 1, 0, Format::park_object_name, 192, 216, 2 },
    { 101, 101, 20, 1, 0, Format::park_object_name, 192, 216, 2 },
    { 102, 102, 257, 1, 0, Format::park_object_name, 192, 256, 0 },
    { 103, 103, 259, 1, 0, Format::park_object_name, 192, 258, 0 },
    { 104, 104, 261, 1, 0, Format::park_object_name, 192, 260, 0 },
    { 105, 105, 104, 1, 0, Format::park_object_name, 192, 233, 2 },
    { 106, 106, 105, 1, 0, Format::park_object_name, 192, 234, 2 },
    { 107, 107, 107, 1, 0, Format::park_object_name, 192, 235, 2 },
    { 108, 108, 108, 1, 0, Format::park_object_name, 192, 235, 2 },
    { 109, 109, 20, 0, 0, Format::literal, 192, 216, 0 },
    { 110, 110, 20, 0, 0, Format::literal, 192, 216, 0 },
    { 111, 111, 20, 0, 0, Format::literal, 192, 216, 0 },
    { 112, 112, 20, 0, 0, Format::literal, 192, 216, 0 },
    { 115, 151, 85, 0, 0, Format::literal, 191, 193, 0 },
    { 116, 151, 86, 0, 0, Format::literal, 191, 193, 0 },
    { 117, 151, 87, 0, 0, Format::literal, 191, 193, 0 },
    { 118, 151, 88, 0, 0, Format::literal, 191, 193, 0 },
    { 119, 151, 89, 0, 0, Format::literal, 191, 193, 0 },
    { 120, 151, 90, 0, 0, Format::literal, 191, 193, 0 },
    { 121, 151, 91, 0, 0, Format::literal, 191, 193, 0 },
    { 122, 151, 92, 0, 0, Format::literal, 191, 193, 0 },
    { 123, 151, 93, 0, 0, Format::literal, 184, 193, 0 },
    { 125, 151, 95, 0, 0, Format::literal, 178, 193, 0 },
    { 126, 151, 96, 0, 0, Format::literal, 184, 193, 0 },
    { 127, 151, 97, 0, 0, Format::literal, 186, 193, 0 },
    { 128, 151, 98, 0, 0, Format::literal, 180, 193, 0 },
    { 130, 151, 100, 0, 0, Format::literal, 184, 193, 0 },
    { 131, 151, 101, 0, 0, Format::literal, 184, 193, 0 },
    { 132, 151, 102, 0, 0, Format::literal, 184, 193, 0 },
    { 133, 151, 103, 0, 0, Format::literal, 184, 193, 0 },
} };
inline constexpr auto by_id = [] consteval {
    std::array<const Row*, 150> result {};
    for (const auto& row : rows) {
        if (row.tag < 0 || static_cast<std::size_t>(row.tag) >= result.size() || row.counter_slot >= 4 || result[static_cast<std::size_t>(row.tag)]) {
            throw "Invalid recovered tag catalog";
        }
        result[static_cast<std::size_t>(row.tag)] = &row;
    }
    return result;
}();

constexpr const Row* find(std::int32_t tag) {
    return tag >= 0 && static_cast<std::size_t>(tag) < by_id.size() ? by_id[static_cast<std::size_t>(tag)] : nullptr;
}
} //namespace simcoaster::tag_catalog
