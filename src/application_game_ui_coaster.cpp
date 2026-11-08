#include "simcoaster/entity_identity.hpp"
#include "application_game_ui_internal.hpp"
#include "simcoaster/mesh_runtime_queries.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world_object_ui.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_special_rides.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char coaster_words[]{
#embed "ui_templates/app_coaster_save.bin"
};
} //namespace

ParkObjectRuntimeView* ApplicationGameUiController::State::first_ride() const {
    //4FA090 filter128 delegates to the same base-ride predicate as the roster.
    std::size_t visited{};
    for (auto* node = c.entities.active_head; node; node = node->active_next) {
        if (++visited > c.entities.size())
            throw std::logic_error("Cyclic live object list");
        auto* header = node->payload;
        if (!header || header->kind != ThingKind::park_object || header->retirement_pending)
            continue;
        auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(header);
        const auto* definition = get_park_object_definition(c, &object);
        if (definition
            && definition->which_ui_type == ObjectUiCategory::rides
            && !static_cast<std::uint16_t>(definition->add_on.upgrades_id)
            && (definition->is_special != 1 || is_special_ride_group_id_unregistered(c, header->id)))
            return &object;
    }
    return nullptr;
}

bool ApplicationGameUiController::State::coaster_alive() const {
    return EntityIdentity { coaster, coaster_identity, coaster_generation }.get<ParkObjectRuntimeView>(c.entities) != nullptr;
}

void ApplicationGameUiController::State::open_coaster_save() {
    begin(Page::coaster_name, false, false);
    auto* object = first_ride();
    if (!object) {
        page = Page::none;
        message(text(56), text(57), {}, {}, UiMessageButtonKind::okay);
        return;
    }
    const auto* opening = world_operations::find_mesh_runtime_opening_state(c, object->mesh_instance_id);
    if (!opening || (opening->placement_flags & 3u) != 1u) {
        page = Page::none;
        message(text(58), text(59), {}, {}, UiMessageButtonKind::okay);
        return;
    }
    if (opening->geometry_peer_reference_count) {
        page = Page::none;
        message(text(60), text(61), {}, {}, UiMessageButtonKind::okay);
        return;
    }
    coaster = object->base.header.id;
    coaster_identity = &object->base.header;
    coaster_generation = object->creation_file_time;
    dialog(coaster_words, 900000);
    auto& heading = required(root, 900001);
    caption(heading, text(51), 5);
    heading.primary_shadow_text = true;
    auto& notice = required(root, 900004);
    caption(notice, text(7), 6, 1, 1);
    notice.text_constraints = { true, true };
    const auto names = park_object_ui_name_components(*object);
    for (std::uint32_t i = 0; i < 2; ++i) {
        auto& editor = required(root, 900002 + i);
        caption(editor, names[i], 7);
        editor.text_limit = 14;
        editor.edit_caret_asset_hash = ui_model_asset_hash("hilight");
        if (!i) {
            ui.prepare_edit_selection(editor);
        }
    }
    c.ports.ui.disable_command_families_and_set_disabled_flag(c);
    commands_disabled = true;
    ui.set_focus(&required(root, 900002));
}

void ApplicationGameUiController::State::save_coaster(bool overwrite) {
    if (!coaster_alive() || page != Page::coaster_name)
        return;
    ApplicationSaveCoasterRequest request;
    request.naming.emplace();
    request.coaster = coaster;
    request.coaster_identity = coaster_identity;
    request.coaster_generation = coaster_generation;
    request.naming->components = { required(root, 900002).text, required(root, 900003).text };
    auto& name = request.basename;
    name = request.naming->components[0];
    //531260 reads one WORD before an empty native prefix. Modern owned text
    //makes that undefined case deterministic: an empty prefix uses B alone.
    if (!name.empty() && name.back() != u' ')
        name += u' ';
    name += request.naming->components[1];
    auto files = host.list_files(ApplicationFileKind::coaster);
    if (!files) {
        message(std::move(files.error().title), std::move(files.error().body), {}, {}, UiMessageButtonKind::okay);
        return;
    }
    const auto existing = std::ranges::find(*files, name, &ApplicationFileEntry::name);
    if (existing != files->end())
        request.file_identity = existing->identity;
    request.overwrite = overwrite;
    request.naming->refresh_sign = !overwrite;
    const bool conflict = existing != files->end();
    //5314E0 closes the name editor before validation/overwrite presentation.
    close();
    coaster = request.coaster;
    coaster_identity = request.coaster_identity;
    coaster_generation = request.coaster_generation;
    if (conflict && !overwrite) {
        message(text(8), text(9), {}, [this, request = std::move(request)]() mutable {
            if (!coaster_alive())
                return;
            request.overwrite = true;
            request.naming->refresh_sign = false;
            enqueue({ .payload = std::move(request) }, true);
        });
    } else
        enqueue({ .payload = std::move(request) }, true);
}

std::intptr_t ApplicationGameUiController::State::coaster_event(UiControl& sender, const UiEvent& e) {
    if (e.type == 2050) {
        if (e.id == 900002)
            ui.set_focus(&required(root, 900003));
        else
            ui.post(root, 256, 4294967295u);
        return 0;
    }
    if (e.type == 2051) {
        ui.set_focus(&required(root, e.id == 900002 ? 900003 : 900002));
        return 0;
    }
    if (e.type == 2052 || (e.type == 256 && e.id == 4294967294u)) {
        close();
        return 0;
    }
    if (e.type == 256 && e.id == 4294967295u) {
        save_coaster();
        return 0;
    }
    return ui.secondary(sender, e);
}
} //namespace simcoaster
