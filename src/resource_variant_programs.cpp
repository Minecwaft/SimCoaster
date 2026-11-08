#include "simcoaster/resource_variant.hpp"
#include <stdexcept>

namespace simcoaster {
namespace {
//Original executable's immutable resource scripts, recovered from the IDB.
//Translate threaded handler/branch addresses at compile time. The resulting
//x64 program has typed operations and word indices, never executable addresses.
constexpr unsigned char original_words[] = {
#embed "../assets/resource_variant_words.bin"
};
static_assert(sizeof(original_words) == 5968);

consteval auto translate_program() {
    std::array<ResourceScriptWord, sizeof(original_words) / 4> result {};
    for (std::size_t i = 0; i < result.size(); ++i) {
        std::uint32_t word {};
        for (unsigned byte = 0; byte < 4; ++byte) {
            word |= std::uint32_t { original_words[i * 4 + byte] } << (byte * 8);
        }
        auto opcode = ResourceOpcode::operand;
        match (word) {
            case 5132256 => opcode = ResourceOpcode::jump;
            case 5132320 => opcode = ResourceOpcode::compare;
            case 5133200 => opcode = ResourceOpcode::end_if;
            case 5133312 => opcode = ResourceOpcode::call;
            case 5133520 => opcode = ResourceOpcode::repeat;
            case 5133568 => opcode = ResourceOpcode::until;
            case 5134768 => opcode = ResourceOpcode::end;
            case 5134800 => opcode = ResourceOpcode::yield;
            case 5134880 => opcode = ResourceOpcode::set;
            case 5135040 => opcode = ResourceOpcode::add;
            case 5135232 => opcode = ResourceOpcode::subtract;
            case 5136560 => opcode = ResourceOpcode::copy;
            case 5136880 => opcode = ResourceOpcode::frame_entry;
            case 5136928 => opcode = ResourceOpcode::frame_slot;
            case 5137072 => opcode = ResourceOpcode::frame;
            case 5137216 => opcode = ResourceOpcode::frame_always;
            case 5137296 => opcode = ResourceOpcode::hide;
            case 5137552 => opcode = ResourceOpcode::advance_slot9;
            case _       => {
            }
        } if (opcode != ResourceOpcode::operand) {
            word = 0;
        } else if (word >= 10156808 && word < 10162776) {
            if ((word - 10156808) % 4) {
                throw "unaligned resource-script target";
            }
            word = (word - 10156808) / 4;
        }
        result[i] = { opcode, word };
    }
    return result;
}

constexpr auto program = translate_program();
constexpr std::array<ResourceScriptEntry, 38> animations { {
    { true, 0 },    { false, 40 },  { false, 63 },  { false, 86 },  { false, 95 }, { false, 134 }, { false, 208 }, { false, 171 }, { false, 282 }, { false, 245 }, { false, 356 }, { false, 371 }, { false, 401 }, { false, 386 }, { false, 319 }, { false, 1 },   { true, 0 },    { true, 0 },     { true, 0 },
    { false, 631 }, { false, 650 }, { false, 665 }, { false, 684 }, { true, 0 },   { true, 0 },    { false, 714 }, { false, 737 }, { false, 769 }, { false, 760 }, { false, 699 }, { false, 796 }, { false, 819 }, { false, 842 }, { false, 865 }, { false, 888 }, { false, 925 }, { false, 1283 }, { false, 1303 },
} };
constexpr std::array<std::uint32_t, 4> metadata_entries { 1396, 1434, 1446, 1458 };
constexpr std::array<std::uint32_t, 12> litter_entries { 940, 949, 958, 967, 976, 985, 994, 1003, 1012, 1021, 1030, 1039 };
constexpr std::array<std::uint32_t, 30> thought_entries { 1096, 1108, 1120, 1126, 1144, 1102, 1132, 1078, 1048, 1054, 1066, 1072, 1060, 1084, 1090, 1114, 1138, 1150, 1156, 1162, 1168, 1174, 1180, 1186, 1192, 1198, 1204, 1210, 1216, 1222 };
} //namespace

std::span<const ResourceScriptWord> builtin_resource_variant_programs() {
    return program;
}

std::uint32_t resource_metadata_program_entry(std::uint32_t index) {
    return metadata_entries.at(index);
}

ResourceScriptEntry resource_program_entry(ResourceProgram requested) {
    return match (requested.family) -> ResourceScriptEntry {
        case ResourceProgramFamily::sprite_animation     => animations.at(requested.index);
        case ResourceProgramFamily::thought              => ResourceScriptEntry { false, thought_entries.at(requested.index - 1u) };
        case ResourceProgramFamily::litter               => ResourceScriptEntry { false, litter_entries.at(requested.index - 1u) };
        case ResourceProgramFamily::balloon_idle         => ResourceScriptEntry { false, 1283 };
        case ResourceProgramFamily::balloon_discard      => ResourceScriptEntry { false, 1298 };
        case ResourceProgramFamily::umbrella             => ResourceScriptEntry { false, 1303 };
        case ResourceProgramFamily::attached_head        => ResourceScriptEntry { false, 1342 }; //Original resource script at 0x9B1000.
        case ResourceProgramFamily::upgrade_balloon      => ResourceScriptEntry { false, 1228 }; //Original resource script at 0x9B0E38.
        case ResourceProgramFamily::staff_cursor_preview => ResourceScriptEntry { false, 699 };  //Original staff_cursor_preview_script at 0x9B05F4.
        case _                                           => throw std::invalid_argument("unknown resource-program family");
    };
}
} //namespace simcoaster
