#include "simcoaster/layout_world_runtime.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <cwctype>

namespace simcoaster {
namespace {
bool space(char16_t unit) {
    return std::iswspace(static_cast<std::wint_t>(unit)) != 0;
}

char16_t lower(char16_t unit) {
    return static_cast<char16_t>(std::towlower(static_cast<std::wint_t>(unit)));
}

bool prefix(std::u16string_view text, std::size_t at, std::u16string_view pattern) {
    if (at > text.size() || pattern.size() > text.size() - at) {
        return false;
    }
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (lower(text[at + i]) != lower(pattern[i])) {
            return false;
        }
    }
    return true;
}

std::size_t find(std::u16string_view text, std::size_t from, std::u16string_view pattern) {
    for (auto at = from; at < text.size(); ++at) {
        if (prefix(text, at, pattern)) {
            return at;
        }
    }
    return std::u16string_view::npos;
}

std::u16string component(const std::array<char16_t, 33>& name) {
    return { name.begin(), std::ranges::find(name, char16_t {}) };
}

void store(std::array<char16_t, 33>& out, std::u16string_view value) {
    value = value.substr(0, value.find(char16_t {}));
    std::ranges::fill(out, char16_t {});
    std::copy_n(value.begin(), std::min(value.size(), out.size()), out.begin());
}

void refresh_sign(WorldRuntime& c, ParkObjectRuntimeView& object, std::u16string first, std::u16string second) {
    if (const auto end = first.find(char16_t {}); end != std::u16string::npos) {
        first.resize(end);
    }
    if (const auto end = second.find(char16_t {}); end != std::u16string::npos) {
        second.resize(end);
    }
    if (!get_park_object_definition(c, &object) || !object.mesh_instance_id) {
        return;
    }
    if (!c.resources.meshes) {
        throw std::logic_error("layout rename requires the owned World mesh runtime");
    }
    (void)c.resources.meshes->set_object_sign_caption(c, object, first, second);
}
} //namespace

bool filter_layout_name(ThemeGlobalInfoManager& manager, std::u16string& text, bool censor) {
    bool found = false;
    const auto logical_end = text.find(char16_t {});
    //Native wide-string searches stop at NUL even when the stored string has a
    //longer explicit length. Keep the untouched suffix for wire round trips.
    const auto end = logical_end == std::u16string::npos ? text.size() : logical_end;
    for (const auto& stored : manager.swear_word_list) {
        auto pattern = std::u16string_view { stored };
        enum class Mode {
            token,
            word_start,
            substring
        };
        auto mode = Mode::token;
        if (!pattern.empty() && pattern.front() == u' ') {
            mode = Mode::word_start;
            pattern.remove_prefix(1);
        } else if (!pattern.empty() && pattern.front() == u'*') {
            mode = Mode::substring;
            pattern.remove_prefix(1);
        }
        if (pattern.empty()) {
            throw LayoutError { LayoutFailure::invalid_contract, 0, {}, "empty language-filter pattern would not advance the native search" };
        }
        std::size_t search = 0;
        while (search < end) {
            const auto match_at = find(std::u16string_view { text }.substr(0, end), search, pattern);
            if (match_at == std::u16string_view::npos) {
                break;
            }
            auto start = match_at;
            if (mode == Mode::token) {
                while (start != search && !space(text[start])) {
                    --start;
                }
                if (space(text[start])) {
                    ++start;
                }
            }
            const bool boundary = mode == Mode::substring || start == search || (start && space(text[start - 1]));
            bool allowed = false;
            if (boundary) {
                for (const auto& word : manager.allowed_word_list) {
                    if (prefix(std::u16string_view { text }.substr(0, end), start, word)) {
                        allowed = true;
                        break;
                    }
                }
            }
            auto next = match_at + pattern.size();
            if (boundary && !allowed) {
                found = true;
                if (mode != Mode::substring) {
                    while (next < end && !space(text[next])) {
                        ++next;
                    }
                }
                if (censor) {
                    std::fill(text.begin() + static_cast<std::ptrdiff_t>(start), text.begin() + static_cast<std::ptrdiff_t>(next), u'*');
                }
            }
            search = next;
        }
    }
    return found;
}

void apply_layout_object_name(WorldRuntime& c, LayoutActionCode code, std::uint16_t key, std::u16string_view input) {
    if (code != LayoutActionCode::name_a && code != LayoutActionCode::name_b) {
        throw LayoutError { LayoutFailure::invalid_contract, 0, code, "not a mapped Type3 name action" };
    }
    if (!c.simulation.current) {
        throw std::logic_error("layout rename World is missing");
    }
    ThingId id {};
    if (key == 60000) {
        id = c.simulation.current->saved.park_gates;
    } else if (key && key <= map_cell_count) {
        id = find_first_park_object_id_in_map_cell(c, &c.simulation.current->map_cells.at(key - 1u));
    } else {
        throw LayoutError { LayoutFailure::invalid_contract, 0, code, "name key is neither gate sentinel nor map cell" };
    }
    auto* header = id ? c.entities.at(id).payload : nullptr;
    if (!header || header->kind != ThingKind::park_object) {
        throw LayoutError { LayoutFailure::invalid_contract, 0, code, "mapped name target has no Type3 record" };
    }
    auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(header);
    std::u16string text { input };
    auto& manager = get_or_create_theme_global_info_manager(c);
    filter_layout_name(manager, text, true);
    auto& destination = code == LayoutActionCode::name_a ? object.name_a : object.name_b;
    store(destination, text);
    if (c.mode.game != GameMode::online && object.base.header.id == c.simulation.current->saved.park_gates) {
        if (manager.selected_theme_info) {
            store(code == LayoutActionCode::name_a ? manager.selected_theme_info->sign_name_a : manager.selected_theme_info->sign_name_b, text);
            manager.selected_theme_info->name_changed = 1;
        }
    }
    //The newly supplied component is passed to sign rendering directly. Only
    //the other component uses its33-unit stored/formatted representation.
    auto first = code == LayoutActionCode::name_a ? text : component(object.name_a);
    if (code == LayoutActionCode::name_b && first.ends_with(u"--")) {
        first.pop_back();
    }
    auto second = code == LayoutActionCode::name_b ? text : component(object.name_b);
    refresh_sign(c, object, std::move(first), std::move(second));
    refresh_info_desk_signs_for_renamed_object(c, object.base.header.id);
}
} //namespace simcoaster
