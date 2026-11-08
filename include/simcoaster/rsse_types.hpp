#pragma once
#include "rsse_opcodes.hpp"
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <cstddef>
#include <filesystem>
#include <list>

namespace simcoaster {
struct RsseWalkRecord;
struct RsseBounceRecord;
struct RsseLimboRecord;
struct RsseSoundOcclusionRegion;
enum class RsseActionType : std::uint32_t {
    particle = 1,
    directed_particle = 2,
    local_ride_sound = 3,
    local_ambient_sound = 4,
    global_ride_sound = 5,
    kids_sound = 6,
    staff_sound = 7,
    global_ambient_sound = 8,
    ui_sound = 9,
    object_ride_sound = 10
};

struct RsseAction {
    RsseActionType type{};
    std::uint32_t handle{};
    std::int32_t source_node_id{}, source_clone_index{}, key{};
};

struct RsseMeshBounds {
    std::array<float, 3> min{}, max{};
};

struct RsseCloneDirection {
    std::array<float, 3> axis{};
    std::uint32_t flags{};
};

struct RsseSourceTransform {
    std::array<float, 3> position{};
    std::optional<std::array<float, 3>> direction;
};
enum class RsseWalkLookupMode : std::int32_t {
    node_to_node = 0,
    mesh_to_node = 1,
    node_to_mesh = 2
};
enum class MobileSpriteCategory : std::int32_t {
    none = 0,
    walking = 1,
    floating = 2,
    standing = 3,
    child = 5,
    parent_flag_set = 6,
    parent_flag_clear = 7,
    grandparent_flag_set = 8,
    grandparent_flag_clear = 9,
    rsse_pose13 = 13,
    bouncing = 14
};

struct RsseProgramHeader {
    std::uint32_t signature{}, opcode_checksum{};
    std::int32_t value_slot_count{}, operand_stack_capacity{}, instruction_step_budget{}, limbo_capacity{},
        bounce_capacity{}, walk_capacity{};
    std::array<std::uint32_t, 4> padding{};
    std::int32_t instruction_word_count{};
};
enum class RsseVariableMetadata {
    absent,
    complete,
    malformed
};

//A loaded script keeps its containing loose file or archive entry so child
//and sound scripts can be opened relative to that resource.
struct RsseScriptSource {
    std::filesystem::path path;
    std::string archive_entry;
};

struct RsseProgram {
    RsseProgramHeader header;
    std::vector<std::uint32_t> instructions;
    std::string string_table;
    std::vector<std::string> variable_names; //Optional compiler/debug footer, preserved verbatim.
    RsseVariableMetadata variable_metadata = RsseVariableMetadata::absent;
    std::optional<RsseScriptSource> source{};
};

//Runtime views use host pointers and spans. RsseProgramInstance owns the
//buffers for loaded states; supplied-storage clients can bind external spans.
struct RsseScriptState {
    RsseScriptState* next{};
    std::uint32_t id{}, sound_script_state_id{};
    std::span<std::int32_t> value_slots{};
    std::int16_t operating_speed{};
    RsseScriptState* previous{};
    std::uint32_t child_script_state_id{}, parent_script_state_id{};
    std::span<const std::uint32_t> instruction_words{};
    std::optional<RsseScriptSource> script_source{};
    //The original directory is serialized separately from the script words.
    std::string script_directory{};
    std::span<std::int32_t> operand_stack{};
    std::string_view string_table{};
    std::int32_t instruction_cursor{}, stack_high_free_index = -1, stack_low_count{}, current_value{};
    std::int32_t instruction_step_budget{}, remaining_instruction_steps{}, program_name_string_offset = -1;
    std::uint32_t wait_deadline_ms{}, animation_deadline_ms{}, last_loop_animation_key = 65535,
                                                               awaited_animation_id_plus_one{}, timer_deadline_ms{};
    std::int16_t animation_duration_scale_milli = 1000;
    std::uint8_t turbo_enabled{}, music_dip_requested{};
    std::int32_t reverb_preset_override = -1;
    std::uint32_t mesh_instance_handle{}, updates_suspended{};
    std::uint32_t auxiliary_runtime_object_handle{}, coaster_runtime_map_id{}, repair_particle_handle{},
        scream_request{}, inherited_context_word{};
    std::uint16_t owner_used_thing_id{};
    RsseSoundOcclusionRegion* sound_occlusion_region{};
    std::span<RsseWalkRecord> walk_records{};
    std::span<std::uint32_t> attached_person_ids{};
    std::list<RsseAction> actions{};
    std::span<RsseBounceRecord> bounce_records{};
    std::int16_t bounce_base_height{};
    std::uint32_t walk_float_start_ms{};
    std::int32_t walk_float_duration_ms{};
    std::uint16_t walk_float_horizontal_scale_milli{}, walk_float_vertical_scale_milli{};
    std::span<RsseLimboRecord> limbo_records{};
    std::int32_t limbo_count{}, bounce_first_node_id{};
    std::uint16_t bounce_count{};
    std::int16_t spark_source_node_id{}, spark_target_node_id{};
    std::uint32_t reserved_092{}, reserved_104{}, reserved_120{}, reserved_232{}, reserved_240{};
    std::uint16_t padding_174{}, padding_186{}, padding_194{};
    std::int16_t endpoint_animation_closed = -1;
    //A saved pointer is only a presence marker until its renderer resource is available.
    bool sound_occlusion_pending{};
};

struct RsseLimboRecord {
    std::uint32_t person_id{}, release_after_ms{};
};

struct RsseBounceRecord {
    std::uint32_t person_id{};
    std::int32_t attachment_node_id{};
    std::uint32_t release_after_ms{}, start_ms{};
};
enum class RsseWalkKind : std::int16_t {
    hide = 1,
    beam = 2,
    stand = 3,
    mesh_attach = 4,
    pose13 = 5,
    person_variant = 6
};
enum class RsseWalkPhase : std::int16_t {
    unused = 0,
    entering = 1,
    active = 2,
    exiting = 3,
    complete = 4
};
enum class RsseWalkFlags : std::uint16_t {
    none = 0,
    interpolate_height = 1
};

struct RsseWalkRecord {
    std::int16_t entry_start_node{}, entry_end_node{}, exit_start_node{}, exit_end_node{};
    std::uint32_t phase_start_ms{}, phase_end_ms{}, person_id{};
    std::int16_t direction{};
    RsseWalkKind kind{};
    RsseWalkPhase phase{};
    RsseWalkFlags flags{};
    std::uint32_t reserved{};
};

struct RsseProgramInstance {
    RsseProgram program;
    RsseScriptState state;
    std::vector<std::int32_t> values, stack;
    std::vector<RsseLimboRecord> limbo;
    std::vector<RsseBounceRecord> bounce;
    std::vector<RsseWalkRecord> walk;
    std::vector<std::uint32_t> attached_people;
};

struct RsseStateManager {
    std::int32_t active{};
    RsseScriptState* head{};
    std::uint32_t scheduler_tick{}, next_state_id = 1, state_count{};
    std::int32_t step_control{};
    std::uint32_t action_node_count{};
    std::vector<std::unique_ptr<RsseProgramInstance>> owned_states{};
};

struct MeshAnimationControlQuery {
    std::uint32_t flags{};
    std::optional<std::uint32_t> animation_id;
};
} //namespace simcoaster
