#pragma once
#include <cstdint>
#include <string_view>

namespace native_game {
namespace inventory_selection {
enum Id : std::int32_t {
    none = 0,
    badge = 1,
    binoculars = 2,
    camera = 3,
    case_files = 4,
    cattle_prod = 5,
    crowbar = 6,
    evidence_kit = 7,
    flashlight = 8,
    gun = 9,
    handcuffs = 10,
    jose_chung = 11,
    lockpick = 12,
    night_vision = 13,
    pda = 14,
    phone = 15,
    photo_of_mulder_and_scully = 16,
    photo_of_nsa_car = 17,
    photo_of_nsa_guy = 18,
    photo_of_rauch = 19,
    photo_of_smolnikoff = 20,
    screwdriver = 21,
    shovel = 22,
    stiletto = 23,
    travel_request = 24,
    wire_cutters = 25,
    fax_lead_sphere = 26,
    paper_folder = 27,
    powerbook = 28,
    laptop = 29,
    videotape = 30,
    fax_log_translation = 31,
};
}

// Script selection values are separate from inventory graphic resource IDs.
constexpr std::wstring_view inventory_selection_name(std::int32_t value) {
    switch (value) {
        case inventory_selection::none:
            return L"None";
        case inventory_selection::badge:
            return L"Badge";
        case inventory_selection::binoculars:
            return L"Binoculars";
        case inventory_selection::camera:
            return L"Camera";
        case inventory_selection::case_files:
            return L"Case files";
        case inventory_selection::cattle_prod:
            return L"Cattle prod";
        case inventory_selection::crowbar:
            return L"Crowbar";
        case inventory_selection::evidence_kit:
            return L"Evidence kit";
        case inventory_selection::flashlight:
            return L"Flashlight";
        case inventory_selection::gun:
            return L"Gun";
        case inventory_selection::handcuffs:
            return L"Handcuffs";
        case inventory_selection::jose_chung:
            return L"Jose Chung";
        case inventory_selection::lockpick:
            return L"Lockpick";
        case inventory_selection::night_vision:
            return L"Night vision";
        case inventory_selection::pda:
            return L"PDA";
        case inventory_selection::phone:
            return L"Phone";
        case inventory_selection::photo_of_mulder_and_scully:
            return L"Photo of Mulder and Scully";
        case inventory_selection::photo_of_nsa_car:
            return L"Photo of NSA car";
        case inventory_selection::photo_of_nsa_guy:
            return L"Photo of NSA guy";
        case inventory_selection::photo_of_rauch:
            return L"Photo of Rauch";
        case inventory_selection::photo_of_smolnikoff:
            return L"Photo of Smolnikoff";
        case inventory_selection::screwdriver:
            return L"Screwdriver";
        case inventory_selection::shovel:
            return L"Shovel";
        case inventory_selection::stiletto:
            return L"Stiletto";
        case inventory_selection::travel_request:
            return L"Travel request";
        case inventory_selection::wire_cutters:
            return L"Wire cutters";
        case inventory_selection::fax_lead_sphere:
            return L"Fax - lead sphere";
        case inventory_selection::paper_folder:
            return L"Paper folder";
        case inventory_selection::powerbook:
            return L"Scully's Powerbook";
        case inventory_selection::laptop:
            return L"Laptop";
        case inventory_selection::videotape:
            return L"Videotape";
        case inventory_selection::fax_log_translation:
            return L"Fax log translation";
        default:
            return {};
    }
}
}
