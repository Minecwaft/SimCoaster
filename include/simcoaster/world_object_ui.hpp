#pragma once
#include "world_types.hpp"

namespace simcoaster {
struct WorldRuntime;
enum class ParkObjectUiStatus : std::int32_t {
    open = 0,
    closed = 1,
    park_closed = 2,
    closed_heatwave = 3,
    closed_rain = 4,
    closed_snowstorm = 5,
    closed_storm = 6,
    broken_down = 7,
    condemned = 8,
    upgrade = 9,
    upgrade_no_engineers = 0xA,
    upgrade_waiting_for_engineer = 0xB,
    upgrade_engineers_on_strike = 0xC,
    upgrade_engineer_assigned = 0xD,
    upgrading = 0xE,
    cleaning = 0xF,
    cleaning_no_janitors = 0x10,
    cleaning_waiting_for_janitor = 0x11,
    cleaning_janitors_on_strike = 0x12,
    cleaning_janitor_assigned = 0x13,
    being_cleaned = 0x14,
    repair = 0x15,
    repair_no_engineers = 0x16,
    repair_waiting_for_engineer = 0x17,
    repair_engineers_on_strike = 0x18,
    repair_engineer_assigned = 0x19,
    being_repaired = 0x1A,
    entry_not_connected = 0x1B,
    closed_entry_not_connected = 0x1C,
    exit_not_connected = 0x1D,
    track_incomplete = 0x1E,
    track_invalid = 0x1F,
    dead = 0x20
};

struct ParkObjectUiStatusPresentation {
    std::uint32_t text_row{};
    std::array<std::uint8_t, 4> color{};
};

//5022A0: all modes share the ordered fault/service predicates. Final fallback
//is CLOSED for0, OPEN for1/negative, or park/admission state for values>1.
//This query may expire an old staff claim and send its owner to IDLE.
ParkObjectUiStatus classify_park_object_ui_status(WorldRuntime&, ParkObjectRuntimeView&, std::int32_t mode = 2);
ParkObjectUiStatusPresentation park_object_ui_status_presentation(ParkObjectUiStatus);
//Native two-component instance name, including trailing-hyphen composition
//and the32-character display limit. It does not substitute a catalog title.
std::u16string park_object_ui_display_name(const ParkObjectRuntimeView&);
std::array<std::u16string, 2> park_object_ui_name_components(const ParkObjectRuntimeView&);
//Raw native5E7E70/5E7FA0 mutation, including gate metadata and ACT80/85.
//Duplicate-dialog policy and sign refresh are separate caller contracts.
void set_park_object_name_component(WorldRuntime&, ParkObjectRuntimeView&, std::u16string_view, bool first);

struct ParkObjectUiNameCommitResult {
    bool accepted{};
    std::optional<std::uint32_t> warning_row;
    //Duplicate warning key29 uses literal inputA+' '+inputB, independently of
    //the hyphen-aware composed display name.
    std::u16string warning_name;
};

ParkObjectUiNameCommitResult commit_park_object_ui_name(WorldRuntime&,
                                                        ParkObjectRuntimeView&,
                                                        std::u16string_view first,
                                                        std::u16string_view second);
} //namespace simcoaster
