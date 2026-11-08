#pragma once
#include "simcoaster/application_debug_selector.hpp"
#include <array>

namespace simcoaster::debug_detail {
using C = ApplicationDebugCommand;
enum class Close {
    none,
    before,
    after
};

struct Slot {
    std::u16string_view caption = u"Add button text here";
    C command = C::none;
    Close close {};
    bool forward = true;
    std::intptr_t result {};
};

//Native initializers5323D0..5388C0 and callbacks532A40..538F30.
//Caption identity is independent of the action: RICHARD and BEN demonstrate
//why looking at text alone cannot supply this dispatch table.
inline constexpr auto pages = [] {
    std::array<std::array<Slot, 12>, 12> p {};
    const auto set = [&](unsigned page, unsigned slot, std::u16string_view text, C command = C::none, Close close = Close::none, bool forward = false, std::intptr_t result = 0) {
        p[page][slot - 1] = { text, command, close, forward, result };
    };
    set(0, 1, u"Skip Mission", C::skip_mission, Close::after);
    set(0, 2, u"Get Promoted", C::promote, Close::after);
    for (auto page : { 1u, 3u, 4u }) {
        set(page, 1, u"Add button text here");
    }
    set(2, 1, u"Raise Light", C::raise_light, Close::none, true);
    set(2, 2, u"Lower Light", C::lower_light);
    set(2, 3, u"Park Award", C::award, Close::none, true);
    set(2, 4, u"Add button text here", C::advisor95, Close::none, true);
    set(5, 1, u"Offer another challenge", C::offer_challenge, Close::after);
    set(5, 2, u"Open all drinks shops", C::open_drinks, Close::after);
    set(5, 3, u"Close all drinks shops", C::close_drinks, Close::after);
    set(5, 4, u"Send Game Over msg", C::game_over_mail, Close::before);
    set(5, 5, u"Reload Challenges for this theme", C::reload_challenges, Close::before);
    set(5, 6, u"Make new Research Panel", C::open_research, Close::before);
    set(5, 7, u"Reconstruct obstacle slot system", C::rebuild_staff_slots, Close::before);
    for (unsigned i = 7; i < 12; ++i) {
        p[5][i].caption = u"";
    }
    set(6, 1, u"Spawn Message Room");
    set(6, 2, u"Spawn Research Assign", C::assign_research, Close::before);
    set(6, 3, u"Spawn of Satan", C::open_research, Close::before);
    set(6, 4, u"Spawn Research Lab", C::open_research, Close::before);
    p[6][4].caption = u"Frog Spawn";
    set(6, 8, u"Add a looping sound...", C::sound, Close::none, true);
    set(6, 9, u"Music level up", C::music_up, Close::none, true);
    set(6, 10, u"Music level down", C::music_down, Close::none, true);
    set(6, 11, u"Dave Felton Comedy Genius", C::promotion_mail);
    set(6, 12, u"Close button window...", C::button_window, Close::before);
    set(7, 1, u"Debug Test Bed", C::inspection, Close::before);
    set(7, 7, u"Spawn Guard", C::spawn_guard, Close::none, false, 1);
    set(7, 8, u"Spawn Researcher", C::spawn_researcher, Close::none, false, 1);
    p[8][0] = p[0][0];
    p[8][1] = p[0][1];
    p[8][0].caption = p[8][1].caption = u"Add button text here";
    set(9, 1, u"OutBox", C::outbox, Close::before);
    set(9, 2, u"Dialup Connection", C::dialup, Close::before);
    set(9, 3, u"Patrol Area Edit", C::none, Close::before);
    set(9, 8, u"Test lighting", C::refresh_lighting, Close::before);
    set(9, 9, u"Can always open Special Rides", C::toggle_special);
    set(9, 10, u"Always win Challenges", C::toggle_challenges);
    set(9, 11, u"Add button text here");
    set(9, 12, u"Tip Of The Day", C::tip, Close::before);
    set(10, 1, u"Access all areas...", C::unlock_areas, Close::none, true);
    set(10, 2, u"Staff removed from training e-mail...", C::training_mail, Close::none, true);
    set(10, 3, u"Wage demand e-mail...", C::wage_mail, Close::none, true);
    set(10, 4, u"Win the game...", C::win_game, Close::none, true);
    set(10, 5, u"Award 10 Shares", C::shares10, Close::none, true);
    set(10, 6, u"Shut the advisor up...", C::silence_advisor, Close::none, true);
    set(10, 7, u"Zoom to ATM desk thing", C::focus_atm, Close::none, true);
    set(10, 8, u"Increase share value by 5%", C::share_price5, Close::none, true);
    set(10, 9, u"Trigger Next Annual Report", C::annual_report, Close::none, true);
    set(10, 10, u"Game intro screen...", C::intro_mail, Close::none, true);
    set(10, 11, u"Next Objective Complete E-mail", C::objective_mail, Close::none, true);
    set(10, 12, u"Toggle e-mail test mode", C::spam_test, Close::none, true);
    set(11, 1, u"Win Golden Ticket", C::ticket);
    set(11, 2, u"Bring Up Current Obstacle Hoardings", C::obstacle_message, Close::after);
    set(11, 3, u"Open Current Obstacle", C::open_obstacle, Close::after);
    set(11, 4, u"Set Staff to Obstacle", C::send_entertainer, Close::after);
    set(11, 5, u"Jump to Water World", C::source_water, Close::after);
    set(11, 6, u"Jump to Arabian Nights", C::source_arabian, Close::after);
    set(11, 7, u"Jump to New Science", C::source_science, Close::after);
    set(11, 8, u"Send Researched Obstacle Message", C::obstacle_message);
    set(11, 9, u"Buy Stuff Panel", C::build, Close::before);
    set(11, 10, u"Info Panel", C::statistics, Close::before);
    set(11, 11, u"Finance Panel", C::finance, Close::before);
    p[11][11].caption = u"Now't 'Ere";
    return p;
}();
} //namespace simcoaster::debug_detail
