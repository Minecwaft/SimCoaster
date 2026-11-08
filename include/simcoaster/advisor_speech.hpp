#pragma once
#include "world_domains_fwd.hpp"
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <string_view>

namespace simcoaster {
std::int32_t advisor_attachment_part_for_sample(std::int32_t);
bool set_advisor_attachment_part(AdvisorWorld&, std::int32_t);
std::uint32_t advisor_presentation_time_ms(AdvisorWorld&);
std::int32_t build_random_advisor_sequence(AdvisorWorld&, std::int32_t duration_ms);
std::int32_t load_advisor_canned_sequence(AdvisorWorld&, std::int32_t row, std::int32_t fallback_ms);
void initialize_advisor_speech_lookup(AdvisorWorld&);
void reset_advisor_speech(AdvisorWorld&);
void update_advisor_speech(AdvisorWorld&);
void update_advisor_mesh_transition(AdvisorWorld&);
void update_advisor_mesh_state(AdvisorWorld&);
void apply_advisor_mesh_command(AdvisorWorld&, std::int32_t);
void pause_advisor_speech_for_ui(AdvisorWorld&);
void resume_advisor_speech_after_ui(AdvisorWorld&);
void resize_advisor_ticket_particles(AdvisorWorld&, std::size_t count, std::uint32_t definition);
void update_advisor_ticket_particles(AdvisorWorld&);
void set_advisor_render_transforms(AdvisorWorld&, bool repositioned);
void load_advisor_mesh_resources(AdvisorWorld&, const std::filesystem::path& root, std::string_view theme);
void advance_advisor_speech_meshes(AdvisorWorld&);
} //namespace simcoaster
