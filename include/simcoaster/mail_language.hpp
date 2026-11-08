#pragma once
#include "bfst_strings.hpp"

namespace simcoaster {
struct MailLanguageResources {
    MbToUnicodeMap conversion;
    std::vector<std::byte> tag_system, challenge_titles, promotions, abilities, staff_types, ui_text, object_names,
        months, lender_names, ingredients, ui_staff_states, object_descriptions, ui_help, ui_thoughts, ui_kid_states,
        challenge_generic, challenge_specific, objective_complete;
};

struct MailDialogNotice {
    std::u16string heading, body, sender, recipient;
    std::int32_t message_kind;
};
} //namespace simcoaster
