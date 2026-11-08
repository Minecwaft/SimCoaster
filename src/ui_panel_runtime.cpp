#include "simcoaster/ui_panel_runtime.hpp"
#include "simcoaster/schema_metadata.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world_runtime.hpp"
#include "ui_panel_presentation.hpp"
#include <algorithm>
#include <meta>
#include <ranges>
#include <stdexcept>

namespace simcoaster {
UiControl* UiPanelBindings::find(UiControl* root, std::uint32_t id) const {
    const auto revision = ui_.tree_revision();
    if (revision != revision_) {
        controls_.clear();
        revision_ = revision;
    }
    const auto key = std::pair { static_cast<const UiControl*>(root), id };
    if (const auto found = controls_.find(key); found != controls_.end()) {
        return found->second;
    }
    auto* control = ui_.find_recursive(root, id);
    //A missing optional control can be created later in this same frame.
    if (control) {
        controls_.emplace(key, control);
    }
    return control;
}

UiControl& UiPanelBindings::required(UiControl* root, std::uint32_t id, std::string_view missing) const {
    if (auto* control = find(root, id)) {
        return *control;
    }
    throw std::runtime_error(std::string(missing) + std::to_string(id));
}

namespace {
constexpr unsigned char message_template[] {
#embed "ui_templates/message.bin"
};

constexpr auto message_fields = std::define_static_array(schema::members_of(^^UiMessageControls, std::meta::access_context::current()));
constexpr auto message_ids = [] consteval {
    std::array<std::uint32_t, message_fields.size()> result {};
    std::size_t index {};
    for (auto field : message_fields) {
        if (std::meta::type_of(field) != ^^UiControl*) {
            throw "incompatible UI control binding";
        }
        auto annotations = std::meta::annotations_of(field);
        if (annotations.size() != 1) {
            throw "required UI binding needs exactly one ID";
        }
        const auto id = schema::annotation<UiRequiredControl>(field, "required UI binding needs exactly one ID").id;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (result[previous] == id) {
                throw "duplicate required UI binding";
            }
        }
        result[index++] = id;
    }
    return result;
}();
} //namespace

UiMessageControls bind_ui_message_controls(UiPanelBindings& bindings, UiControl* root, std::string_view missing) {
    UiMessageControls controls;
    template for (constexpr auto index : std::views::iota(std::size_t { 0 }, message_fields.size())) {
        constexpr auto member = message_fields[index];
        controls.[:member:] = &bindings.required(root, message_ids[index], missing);
    }
    return controls;
}

std::span<const unsigned char> ui_message_template() {
    return message_template;
}

UiControl& create_ui_message_wrapper(UiRuntime& ui) {
    auto& wrapper = ui.create(nullptr, UiControlKind::window, 1, 1, { 0, 0, 2047, 1535 });
    wrapper.secondary_asset_hash = ui_model_asset_hash("lolight");
    return wrapper;
}

void split_ui_message_title(std::u16string& heading, std::u16string& body) {
    if (heading.empty()) {
        if (const auto split = body.find(u"\n\n"); split != body.npos) {
            heading = body.substr(0, split);
            body.erase(0, split + 2);
        }
    }
}

void cancel_ui_advisor_action(WorldRuntime& world, std::int32_t action, bool clear_panel_flags) {
    const std::array owners { world.simulation.current, world.referenced_world() };
    for (std::size_t i = 0; i < owners.size(); ++i) {
        if (!owners[i] || (i && owners[i] == owners[0])) {
            continue;
        }
        const auto id = owners[i]->saved.advisor;
        if (id && id < world.entities.size() && world.entities[id].payload && world.entities[id].payload->kind == ThingKind::advisor) {
            auto& advisor = *world.entities.payload<AdvisorRecord>(id);
            if (clear_panel_flags) {
                advisor.from_ui_panel = advisor.panel_up = 0;
            }
            world_operations::cancel_advisor_action_by_id(world, &advisor, action);
        }
    }
}

void ui_panel_detail::bind(UiRuntime& ui, UiControl& control, Binder binder, bool route_root) {
    binder.install(binder.callback, ui, control, route_root);
    for (auto* child : control.children) {
        bind(ui, *child, binder, false);
    }
}

std::u16string localize_ui_panel(const WorldRuntime& world, std::optional<std::span<const std::byte>> table, std::uint32_t row, std::span<const BfstSignedIntegerArgument> integers, std::span<const BfstWideStringArgument> strings, const UiPanelPresentationPolicy& policy) {
    const auto& language = world.messages.mail_language;
    auto value = format_bfst_wide_string(table ? *table : std::span<const std::byte> { language.ui_text }, language.conversion, row, 16383, strings, integers);
    if (!value) {
        auto error = std::string(policy.text_error);
        if (policy.text_error_row) {
            error += std::to_string(row) + ": ";
        }
        throw std::runtime_error(error + value.error().reason);
    }
    return std::move(*value);
}

UiControl& ui_panel_detail::parse(UiRuntime& ui, UiControl* parent, std::span<const unsigned char> bytes, std::uint32_t root_id, Binder binder, std::vector<UiControl*>* owners, UiPanelTemplatePolicy policy) {
    const auto previous = parent->children;
    const auto parsed = ui.parse_template(parent, std::as_bytes(bytes));
    if (!parsed) {
        auto message = std::string(policy.parse_error);
        if (policy.include_word) {
            message += std::to_string(parsed.error().word) + ": ";
        }
        throw std::runtime_error(message + parsed.error().reason);
    }
    UiControl* selected {};
    for (auto* child : parent->children) {
        if (std::ranges::find(previous, child) != previous.end()) {
            continue;
        }
        bind(ui, *child, binder, true);
        if (owners) {
            owners->push_back(child);
        }
        auto* found = policy.lookup == UiPanelRootLookup::recursive ? ui.find_recursive(child, root_id) : child->id == root_id ? child : nullptr;
        if (found) {
            selected = found;
        }
    }
    if (!selected) {
        throw std::runtime_error(std::string(policy.missing_root) + (policy.include_root_id ? std::to_string(root_id) : ""));
    }
    return *selected;
}
} //namespace simcoaster
