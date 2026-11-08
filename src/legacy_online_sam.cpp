#include "simcoaster/legacy_online_sam.hpp"
#include "simcoaster/sam_schema.hpp"
#include <algorithm>
#include <charconv>

namespace simcoaster {
namespace {
//Asset vocabulary only: none of these are added to the native schema.
constexpr std::array retired_keys { "EntertainerConstsPerGrade.ActivationDistance",  "EntertainerConstsPerGrade.WorkDuration", "FixedItemInfo.TicketBoothAPosX",       "FixedItemInfo.TicketBoothAPosY",           "FixedItemInfo.TicketBoothBPosX", "FixedItemInfo.TicketBoothBPosY",      "GuardConstsPerGrade.WorkDuration", "HandymanConstsPerGrade.DetectionRange", "HandymanConstsPerGrade.WorkDuration", "MechanicConstsPerGrade.WorkDuration", "PerGradeStaffConsts.BaseWage",
                                    "PerGradeStaffConsts.HappinessRecuperationRate", "PerGradeStaffConsts.IdleDuration",       "PerGradeStaffConsts.RecuperationRate", "ResearcherConstsPerGrade.ResearchAbility", "Seasons.AvgWeatherQuality",      "Seasons.ChanceForExceptionalWeather", "Seasons.NormalTolerance" };

bool numeric_token(std::string_view text, std::size_t& cursor) {
    if (cursor < text.size() && (text[cursor] == '-' || text[cursor] == '+')) {
        ++cursor;
    }
    std::size_t digits {};
    while (cursor < text.size() && sam::detail::digit(text[cursor])) {
        ++cursor;
        ++digits;
    }
    if (cursor < text.size() && text[cursor] == '.') {
        ++cursor;
        while (cursor < text.size() && sam::detail::digit(text[cursor])) {
            ++cursor;
            ++digits;
        }
    }
    if (!digits) {
        return false;
    }
    if (cursor < text.size() && (text[cursor] == 'e' || text[cursor] == 'E')) {
        ++cursor;
        if (cursor < text.size() && (text[cursor] == '-' || text[cursor] == '+')) {
            ++cursor;
        }
        const auto start = cursor;
        while (cursor < text.size() && sam::detail::digit(text[cursor])) {
            ++cursor;
        }
        if (start == cursor) {
            return false;
        }
    }
    return cursor == text.size() || sam::detail::space(text[cursor]);
}

std::optional<std::string_view> canonical_index(std::optional<std::string_view> index) {
    if (index && !index->empty()) {
        const auto first = index->find_first_not_of('0');
        *index = first == index->npos ? index->substr(index->size() - 1) : index->substr(first);
    }
    return index;
}
} //namespace

const LegacySamAssignment* LegacyOnlineSamDocument::effective(std::string_view group, std::optional<std::string_view> index, std::string_view field) const {
    for (auto it = assignments.rbegin(); it != assignments.rend(); ++it) {
        if (it->group == group && it->field == field && canonical_index(it->index ? std::optional<std::string_view> { *it->index } : std::nullopt) == canonical_index(index)) {
            return &*it;
        }
    }
    return nullptr;
}

std::vector<LegacySamAssignment> LegacyOnlineSamDocument::unmapped() const {
    std::vector<LegacySamAssignment> result;
    for (const auto& value : assignments) {
        if (value.retired) {
            result.push_back(value);
        }
    }
    return result;
}

std::expected<void, sam::Error> parse_legacy_online_sam(LegacyOnlineSamDocument& out, std::string_view text, std::string_view source, bool preserve) {
    if (!preserve) {
        out.assignments.clear();
    }
    text = text.substr(0, text.find('\0'));
    const auto contracts = sam_schema_catalog();
    std::size_t line {};
    while (line < text.size()) {
        auto cursor = line;
        while (cursor < text.size() && sam::detail::space(text[cursor])) {
            ++cursor;
        }
        if (cursor == text.size()) {
            break;
        }
        const auto statement = cursor;
        if (text[cursor] == '#') {
            const auto next = text.find('\n', cursor);
            line = next == text.npos ? text.size() : next + 1;
            continue;
        }
        std::string_view group, field;
        std::optional<std::string> index;
        auto fail = [&](sam::Failure reason) {
            sam::Error error { reason, cursor, std::string { group }, std::string { field } };
            if (index) {
                std::uint32_t value {};
                const auto parsed = std::from_chars(index->data(), index->data() + index->size(), value);
                if (parsed.ec == std::errc {}) {
                    error.index = value;
                }
            }
            return std::unexpected(std::move(error));
        };
        const auto start = cursor;
        while (cursor < text.size() && sam::detail::alnum(text[cursor])) {
            ++cursor;
        }
        if (start == cursor || cursor == text.size()) {
            return fail(sam::Failure::path);
        }
        group = text.substr(start, cursor - start);
        if (text[cursor] == '[') {
            const auto first = ++cursor;
            while (cursor < text.size() && sam::detail::digit(text[cursor])) {
                ++cursor;
            }
            if (first == cursor || cursor == text.size() || text[cursor] != ']') {
                return fail(sam::Failure::path);
            }
            index = std::string { text.substr(first, cursor - first) };
            ++cursor;
        }
        if (cursor == text.size() || text[cursor++] != '.') {
            return fail(sam::Failure::path);
        }
        std::array<std::string_view, 16> names {};
        std::size_t count {};
        for (;;) {
            if (count == names.size() || cursor == text.size() || !sam::detail::alpha(text[cursor])) {
                return fail(sam::Failure::path);
            }
            const auto first = cursor++;
            while (cursor < text.size() && sam::detail::alnum(text[cursor])) {
                ++cursor;
            }
            names[count++] = text.substr(first, cursor - first);
            if (cursor == text.size()) {
                return fail(sam::Failure::path);
            }
            const auto separator = text[cursor++];
            if (separator == '.') {
                if (count < names.size()) {
                    continue;
                }
                if (cursor < text.size() && text[cursor] == '.') {
                    return fail(sam::Failure::path);
                }
                break;
            }
            if (!sam::detail::space(separator)) {
                return fail(sam::Failure::path);
            }
            break;
        }
        const auto source_line = static_cast<std::size_t>(std::ranges::count(text.substr(0, statement), '\n')) + 1;
        const auto previous = text.rfind('\n', statement);
        const auto column = statement - (previous == text.npos ? 0 : previous + 1) + 1;
        for (std::size_t i = 0; i < count; ++i) {
            field = names[i];
            const auto key = std::string { group } + "." + std::string { field };
            const auto retired = std::ranges::find(retired_keys, key) != retired_keys.end();
            const SamFieldContract* contract = nullptr;
            if (!retired) {
                for (const auto& candidate : contracts) {
                    if (std::string_view { candidate.schema } == "world_balance_descriptors" && group == candidate.field.group && field == candidate.field.name) {
                        contract = &candidate.field;
                        break;
                    }
                }
                if (!contract) {
                    return fail(sam::Failure::unknown_field);
                }
                if (contract->indexed != index.has_value()) {
                    return fail(sam::Failure::path);
                }
                if (index) {
                    std::uint32_t value {};
                    const auto converted = std::from_chars(index->data(), index->data() + index->size(), value);
                    if (converted.ec != std::errc {} || value >= contract->capacity) {
                        return fail(sam::Failure::index);
                    }
                }
            }
            while (cursor < text.size() && sam::detail::space(text[cursor])) {
                ++cursor;
            }
            const auto value_start = cursor;
            if (retired) {
                if (!numeric_token(text, cursor)) {
                    return fail(sam::Failure::value);
                }
            } else if (contract->kind <= 6) {
                std::int64_t checked {};
                if (contract->unsigned_storage) {
                    std::uint32_t value {};
                    if (!sam::detail::integer(text, cursor, value)) {
                        return fail(sam::Failure::value);
                    }
                    checked = value;
                } else {
                    std::int32_t value {};
                    if (!sam::detail::integer(text, cursor, value)) {
                        return fail(sam::Failure::value);
                    }
                    checked = value;
                }
                if ((contract->kind == 5 && checked < 0) || (contract->kind == 6 && (checked < contract->minimum || checked >= contract->maximum))) {
                    return fail(sam::Failure::range);
                }
            } else if (contract->kind <= 9) {
                const auto value = sam::parse_scalar(text, cursor);
                if (!value) {
                    return fail(sam::Failure::value);
                }
                if ((contract->kind == 8 && *value < 0) || (contract->kind == 9 && (*value < contract->lower || *value >= contract->upper))) {
                    return fail(sam::Failure::range);
                }
            } else if (contract->kind == 10) {
                if (cursor == text.size() || text[cursor++] != '"') {
                    return fail(sam::Failure::value);
                }
                while (cursor < text.size() && text[cursor] != '"' && text[cursor] != '\n') {
                    ++cursor;
                }
                if (cursor == text.size() || text[cursor++] != '"') {
                    return fail(sam::Failure::value);
                }
            } else {
                return fail(sam::Failure::map_fields);
            }
            out.assignments.push_back({ std::string { group }, std::string { field }, index, std::string { text.substr(value_start, cursor - value_start) }, std::string { source }, source_line, column, statement, value_start, retired });
        }
        const auto next = text.find('\n', statement);
        line = next == text.npos ? text.size() : next + 1;
    }
    return {};
}
} //namespace simcoaster
