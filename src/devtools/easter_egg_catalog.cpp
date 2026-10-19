#include "easter_eggs.h"

namespace devtools {
namespace {
constexpr EasterEgg entries[]{
    {L"PDA: level select", L"warp speed x",
     L"Opens the original location selector. Choosing a location also changes story state."},
    {L"PDA: hidden level-select button", L"Von",
     L"Enables a hidden button near (439, 15) on the Puget Sound map. Right-click it to select a "
     L"location."},
    {L"PDA: inventory editor", L"big booty",
     L"Opens the original item grid. Click an item to add or remove it."},
    {L"PDA: FBI equipment", L"superagent",
     L"Grants the ten FBI tools, including the camera, gun, evidence kit and phone."},
    {L"PDA: idea icons", L"good idea oLord", L"Opens the original idea-icon selector."},
    {L"PDA: set Paranoia high", L"paranoia me up",
     L"Sets iUberParanoia, variable 859 (0x35B), to 150. Default: 100. "
     L"The initial high threshold is 105, variable 4679.",
     859},
    {L"PDA: set Paranoia low", L"paranoia me down",
     L"Sets iUberParanoia, variable 859 (0x35B), to 50. It does not subtract from the current "
     L"value.",
     859},
    {L"PDA: set Loss high", L"loss me up",
     L"Sets iUberLoss, variable 860 (0x35C), to 150. Default: 100. "
     L"The initial high threshold is 105, variable 4678.",
     860},
    {L"PDA: set Loss low", L"loss me down",
     L"Sets iUberLoss, variable 860 (0x35C), to 50. It does not subtract from the current value.",
     860},
    {L"PDA: set X high", L"x me up",
     L"Sets iUberXTrack, variable 858 (0x35A), to 150. Default: 100. "
     L"The initial high threshold is 103, variable 4680.",
     858},
    {L"PDA: set X low", L"x me down",
     L"Sets iUberXTrack, variable 858 (0x35A), to 50. It does not subtract from the current value.",
     858},
    {L"PDA: set Asta relationship high", L"asta me up",
     L"Sets iAstaScore, variable 1411 (0x583), to 150. Default: 100. "
     L"Asta's initial low threshold is 98 (variable 4683), high threshold 104 (variable 4682). "
     L"At the hauling-yard kiss, action 41115 tests >= high and selects asset list 41116. "
     L"Action 41119 tests <= low and selects list 41120. Use Variable paths to follow conditions "
     L"and writes. No universal minimum or maximum is established.",
     1411},
    {L"PDA: set Asta relationship low", L"asta me down",
     L"Sets iAstaScore, variable 1411 (0x583), to 50. It does not subtract from the current value. "
     L"Authored branches compare it with low and high threshold variables. Use Variable paths to "
     L"follow them.",
     1411},
    {L"PDA: warehouse lock shortcut", L"action movie",
     L"Enables an alternate response when an item is used on the warehouse lock while Skinner is "
     L"present."},
    {L"PDA: infinity / red map button", L"the nature of infinity is paradox",
     L"Enables the red HyperBole Studios button on the Seattle map. The original button only "
     L"allows entry in story node 6. It does not check the system date. It leads to panoramas "
     L"of the original developer office. "
     L"Use the separate HyperBole Studios shortcut to visit from another chapter."},
    {L"HyperBole Studios: visit the developer office", L"",
     L"Visit the original developer office during exploration, from any story chapter, "
     L"without changing story variables or the system date. Close the PDA and other device "
     L"screens first. Save before visiting so you can load that save to return.",
     0, EasterEggShortcut::studio},
    {L"PDA: close map", L"Corey Hart", L"Closes the map."},
    {L"PDA: Asta reaction test", L"Mary Astadourian",
     L"Plays available stock reactions for this character."},
    {L"PDA: Scully reaction test", L"Dana Scully",
     L"Plays available stock reactions for this character."},
    {L"PDA: Skinner reaction test", L"Walter Skinner",
     L"Plays available stock reactions for this character."},
    {L"PDA: Shanks reaction test", L"Armistead Shanks",
     L"Plays available stock reactions for this character."},
    {L"PDA: Cook reaction test", L"Mark Cook",
     L"Plays available stock reactions for this character."},
    {L"PDA: Joan reaction test", L"Joan Truit",
     L"Plays available stock reactions for this character."},
    {L"PDA: Amis reaction test", L"John Amis",
     L"Plays available stock reactions for this character."},
    {L"PDA: Arley reaction test", L"Arley", L"Plays available stock reactions for this character."},
    {L"PDA: Smolnikoff reaction test", L"Smolnikoff",
     L"Plays available stock reactions for this character."},
    {L"PDA: Stearns reaction test", L"Stearns",
     L"Plays available stock reactions for this character."},
    {L"PDA: hidden debug controls", L"",
     L"In the address book, right-click (318, 430), then open the Puget Sound map. "
     L"Right-click (417, 15) for ideas, (427, 15) for inventory or (439, 15) for level select. "
     L"Coordinates refer to the original 640 by 480 image."},
    {L"FBI login: dwight", L"xyzzy",
     L"Replace the login name with dwight and use this password for a dedication."},
    {L"FBI login: nickolai", L"jason",
     L"Replace the login name with nickolai and use this password for a dedication."},
    {L"FBI login: angelina", L"jason",
     L"Replace the login name with angelina and use this password for a dedication."},
    {L"FBI login: twila", L"jason",
     L"Replace the login name with twila and use this password for a dedication."},
    {L"FBI login: normal password", L"shiloh",
     L"Use this password with the prefilled FBI login name."},
    {L"FBI login: skip password", L"",
     L"Right-click the letter o in Welcome on the FBI login screen."},
    {L"Scully's laptop", L"faith",
     L"The password still fails to open the laptop, but is remembered for dialogue with Skinner. "
     L"Repeated attempts can lock the fields for the rest of the game session."},
    {L"Mel's photographs", L"",
     L"At Rauch's house, use the gun on the woman in brown during Willmore's call from Scully, "
     L"about five seconds into the scene. The brief target can display a photograph of Melanie "
     L"McClaire."},
    {L"Willmore's notes", L"",
     L"Using the gun on Willmore in a movie ends the game with one of three notes. "
     L"Shiloh's data research indicates a random choice."},
    {L"ING: developer records", L"",
     L"Search the Criminal database for names from the credits or aliases such as Mel, Frosty, "
     L"Bunny slayer, Boba Fett, pez boy, Sound God, towing queen, Red Goddess, asset wrangler or "
     L"Wanker. "
     L"Shiloh reports 42 joke records. Sending a record by e-mail can get a joke reply."},
    {L"Media search: developer biographies", L"",
     L"Search for Melanie McClaire (Mel or Bob), Jason VandenBerghe (Kayla), Derek Rhys Creech "
     L"(Frosty), "
     L"Pete Isensee (pez boy), Joseph Kim (asset wrangler) or Alex Smits (Bunny slayer)."},
    {L"Phone: personal call", L"206-555-0179",
     L"Call Barbara from the government phone. After her message, watch for an accounting e-mail "
     L"about the charge."},
    {L"Phone: Lone Gunmen", L"1-202-555-0149",
     L"Call once from the Comity Inn chapter onward. Shiloh's research corrects the "
     L"often-published 0147 ending."},
    {L"PDA: studio address", L"",
     L"Watch the address book immediately after opening it. A default HyperBole Studios card "
     L"briefly precedes the contact."},
    {L"Warehouse: Jose Chung's book", L"",
     L"With Skinner at the warehouse, try the book on the door lock. "
     L"Shiloh reports an alternate authored response, not yet checked there in the original game."},
    {L"Original debug keys", L"",
     L"Shiloh reports Shift+Alt+F for frame rate, Shift+Alt+H for targets, Shift+Alt+I for hidden "
     L"scores "
     L"and Shift+Alt+J for instant text. Some original behavior is unverified. "
     L"Alt+0 through Alt+9 sets volume. Alt+A toggles Artificial Intuition, Alt+M Subliminal "
     L"Messages, "
     L"Alt+R Rollover Text, Alt+T Transitions and Alt+V High Quality Video. Alt+P pauses, "
     L"and Alt+C/S/X controls captions, saving and exit. Patch shortcuts can take precedence."},
    {L"Subliminal messages", L"",
     L"The original Subliminal Messages option can briefly flash words between movies. "
     L"The highest hidden score determines the word set. Shiloh reports confirming this in the "
     L"original game."},
    {L"Unused media", L"",
     L"Shiloh reports unreferenced clips, dialogue, sounds and cursor animations. "
     L"Use Database & assets and the Library to explore your installed files."},
};
}

std::span<const EasterEgg> easter_eggs() {
    return entries;
}
}
