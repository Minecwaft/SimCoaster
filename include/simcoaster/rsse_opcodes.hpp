#pragma once
#include <array>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace simcoaster {
struct RsseOpcodeDescriptor {
    std::string_view mnemonic;
    std::uint8_t operand_count;
};

//Recovered directly from Game.exe's106 original mnemonic/count descriptors.
//The same ordered metadata defines the wire enum and diagnostic names.
inline constexpr std::array<RsseOpcodeDescriptor, 106> rsse_opcode_descriptors { {
    { "NOP", 0 },
    { "CRIT_LOCK", 0 },
    { "CRIT_UNLOCK", 0 },
    { "COPY", 2 },
    { "SETLV", 1 },
    { "SUB", 3 },
    { "ENDSLICE", 0 },
    { "GETTIME", 1 },
    { "ADDOBJ", 4 },
    { "ADDOBJ_EXT", 5 },
    { "KILLOBJ", 1 },
    { "FADEOBJ", 1 },
    { "SETOBJPARAM", 3 },
    { "EVENT", 3 },
    { "EVENT_EXT", 4 },
    { "FLUSHANIM", 0 },
    { "TRIGANIM", 3 },
    { "WAITANIM", 2 },
    { "LOOPANIM", 2 },
    { "TRIGWAITANIM", 3 },
    { "GETANIM", 1 },
    { "TRIGANIMSPEED", 4 },
    { "FLUSHANIM_CH", 1 },
    { "TRIGANIM_CH", 4 },
    { "WAITANIM_CH", 3 },
    { "LOOPANIM_CH", 3 },
    { "TRIGWAITANIM_CH", 4 },
    { "GETANIM_CH", 2 },
    { "RAND", 2 },
    { "JSR", 1 },
    { "RETURN", 0 },
    { "BRANCH", 1 },
    { "BRANCH_Z", 1 },
    { "BRANCH_NZ", 1 },
    { "BRANCH_NV", 1 },
    { "BRANCH_PV", 1 },
    { "DBGMSG", 1 },
    { "NAME", 1 },
    { "TEST", 1 },
    { "CMP", 2 },
    { "PUSH", 1 },
    { "POP", 1 },
    { "HUSH", 1 },
    { "HOP", 1 },
    { "WAIT", 1 },
    { "WAITABS", 1 },
    { "WAIT4ANIM", 0 },
    { "ADD", 2 },
    { "MULT", 3 },
    { "DIV", 3 },
    { "MOD", 3 },
    { "TURBO", 1 },
    { "END", 0 },
    { "TOUR", 2 },
    { "BUMP", 2 },
    { "COAST", 2 },
    { "ADDHEAD", 1 },
    { "DELHEAD", 1 },
    { "LIMBO", 2 },
    { "UNLIMBO", 1 },
    { "FORCEUNLIMBO", 1 },
    { "INLIMBO", 1 },
    { "LIMBOSPACE", 1 },
    { "SPAWNCHILD", 1 },
    { "SPAWNSOUND", 1 },
    { "REMOVECHILD", 0 },
    { "SETVARINCHILD", 2 },
    { "GETVARINCHILD", 2 },
    { "SETVARINPARENT", 2 },
    { "GETVARINPARENT", 2 },
    { "BOUNCESETNODE", 1 },
    { "BOUNCESETBASE", 1 },
    { "BOUNCE", 2 },
    { "UNBOUNCE", 1 },
    { "FORCEUNBOUNCE", 1 },
    { "BOUNCING", 1 },
    { "WALKON", 7 },
    { "WALKOFF", 1 },
    { "WALKGET", 1 },
    { "WALKST_FLOAT", 3 },
    { "WALKFLOATSTAT", 1 },
    { "WALKFLOATSTOP", 0 },
    { "ENABLELIGHT", 1 },
    { "DISABLELIGHT", 1 },
    { "SETLIGHT", 2 },
    { "COLOURLIGHT", 4 },
    { "STARTSCREAM", 2 },
    { "STOPSCREAM", 0 },
    { "SINGLESCREAM", 2 },
    { "SCREAMLEVEL", 1 },
    { "FINDSCRIPTRAND", 2 },
    { "GETREMOTEVAR", 3 },
    { "SETREMOTEVAR", 3 },
    { "REPAIREFFECT", 1 },
    { "GETCUSTPTCLCODE", 2 },
    { "SETTIMER", 1 },
    { "GETTIMER", 1 },
    { "YEAR", 1 },
    { "MONTH", 1 },
    { "DAY", 1 },
    { "HOUR", 1 },
    { "MIN", 1 },
    { "SEC", 1 },
    { "SETREVERB", 1 },
    { "DIPMUSIC", 1 },
    { "SPARK", 4 },
} };
enum class RsseOpcode : std::uint32_t;
inline constexpr std::uint32_t rsse_opcode_checksum = [] consteval {
    std::uint32_t value = 0;
    for (const auto& descriptor : rsse_opcode_descriptors) {
        for (unsigned char c : descriptor.mnemonic) {
            value += c;
        }
        value += static_cast<std::uint32_t>('0') + descriptor.operand_count;
    }
    return value;
}();
static_assert(rsse_opcode_checksum == 69457);
consteval {
    std::vector<std::meta::info> specifications;
    for (std::size_t i = 0; i < rsse_opcode_descriptors.size(); ++i) {
        specifications.push_back(std::meta::enumerator_spec({ .name = std::string { rsse_opcode_descriptors[i].mnemonic }, .value = std::meta::reflect_constant(static_cast<std::uint32_t>(i)) }));
    }
    std::meta::define_enum(^^RsseOpcode, specifications);
}
enum class RsseWordTag : std::uint32_t {
    opcode = 2147483648u,
    value_slot = 1073741824u,
    address = 536870912u,
    string = 268435456u
};
inline constexpr std::uint32_t rsse_tag_mask = 4278190080u;

constexpr std::uint32_t rsse_word(RsseWordTag tag, std::uint32_t value) {
    return static_cast<std::uint32_t>(tag) | value;
}

constexpr std::uint32_t rsse_word(RsseOpcode opcode) {
    return rsse_word(RsseWordTag::opcode, static_cast<std::uint32_t>(opcode));
}
} //namespace simcoaster
