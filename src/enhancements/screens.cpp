#include "enhancements/game_resources.h"
#include "screens.h"
#include "focus.h"
#include "game_ui.h"
#include "text_entry.h"
#include "settings.h"
#include "controls.h"
#include "ui/menu_link.h"
#include "dialogue.h"
#include "inventory.h"

#include <array>
#include <algorithm>
#include <vector>

namespace enhancements {
namespace {

enum class Pda { none, region, city, notes, addresses, inbox, message };

bool toolbar(const RECT& item) {
    return item.top >= 386 && item.bottom <= 412;
}

bool click_at(HWND window, const std::vector<RECT>& items, POINT point) {
    for (const auto& item : items) {
        if (PtInRect(&item, point)) {
            return point_controller(window, item, true);
        }
    }
    return false;
}

}

bool navigate_emotions(HWND window, int direction, bool focus) {
    if ((!direction && !focus) || current_dialogue() || inventory_focused(window)) {
        return false;
    }
    const auto items = game::emotion_targets();
    if (items.empty()) {
        return false;
    }
    POINT cursor{};
    if (GetCursorPos(&cursor) && ScreenToClient(window, &cursor)) {
        const auto selected = focus ? 0 : hotspot_target(items, cursor, direction);
        if (selected >= 0) {
            point_controller(window, items[selected]);
        }
    }
    return true;
}

bool navigate_screen(HWND window, int horizontal, int vertical, bool activate, bool cancel,
                     bool change_group, bool keyboard) {
    std::vector<RECT> items;
    unsigned edit_resource = 0;
    std::vector<RECT> edit_targets;
    const auto input = game::input_vtable();
    Pda pda = Pda::none;
    bool workstation = false;
    bool workstation_message = false;
    items = game::modal_buttons();
    const bool modal = !items.empty();
    if (!items.empty()) {
        if (activate && items.size() == 1) {
            point_controller(window, items.front(), true);
            return true;
        }
        horizontal = horizontal ? horizontal : vertical;
        vertical = 0;
        if (cancel || change_group) {
            if (items.size() == 1) {
                point_controller(window, items.front(), true);
            }
            return true;
        }
    } else if (input == game::edition().main_menu) {
        items = main_menu_targets(game::saving_available());
        items.push_back(settings_link);
        vertical = vertical ? vertical : horizontal;
        horizontal = 0;
    } else if (!input) {
        auto script = game::script_controls();
        const auto has = [&](unsigned resource) {
            return std::find(script.resources.begin(), script.resources.end(), resource) !=
                   script.resources.end();
        };
        if (!script.acknowledgement_buttons.empty()) {
            if (activate || cancel || change_group || horizontal || vertical) {
                point_controller(window, script.acknowledgement_buttons.front(),
                                 activate || cancel || change_group);
            }
            return true;
        }
        if (script.script_dialog && !script.dialog_buttons.empty()) {
            if (keyboard && !script.dialog_fields.empty()) {
                return false;
            }
            if (cancel || change_group ||
                (activate && script.dialog_buttons.size() == 1 && script.dialog_fields.empty())) {
                point_controller(window, script.dialog_buttons.front(), true);
                return true;
            }
            items = script.dialog_buttons;
            items.insert(items.end(), script.dialog_fields.begin(), script.dialog_fields.end());
            POINT cursor{};
            if (GetCursorPos(&cursor) && ScreenToClient(window, &cursor)) {
                if (activate && !keyboard && !script.resources.empty()) {
                    for (const auto& field : script.dialog_fields) {
                        if (PtInRect(&field, cursor)) {
                            open_text_entry(window, field, script.resources.back());
                            return true;
                        }
                    }
                }
                const auto selected = directional_target(items, cursor, horizontal, vertical);
                if (selected >= 0) {
                    point_controller(window, items[selected], activate);
                } else if (activate) {
                    click_at(window, script.dialog_buttons, cursor);
                }
            }
            return true;
        }
        const bool options = has(resource::options);
        const bool save = has(resource::save);
        const bool load = has(resource::load);
        const bool help = has(resource::help);
        const bool phone = has(resource::phone);
        pda = has(resource::pda_region)      ? Pda::region
              : has(resource::pda_city)      ? Pda::city
              : has(resource::pda_notes)     ? Pda::notes
              : has(resource::pda_addresses) ? Pda::addresses
              : has(resource::pda_inbox)     ? Pda::inbox
              : has(resource::pda_message)   ? Pda::message
                                             : Pda::none;
        // The workstation root remains registered after its visible page closes.
        const bool workstation_page =
            has(resource::workstation_root) || has(resource::workstation_welcome) ||
            has(resource::workstation_login) || has(resource::workstation_landing) ||
            has(resource::workstation_search) || has(resource::workstation_inbox) ||
            has(resource::workstation_message) || has(resource::workstation_photo) ||
            has(resource::workstation_apb) || has(resource::workstation_media);
        workstation = workstation_page && !script.buttons.empty() && !options && !save && !load &&
                      !help && !phone && pda == Pda::none;
        workstation_message = workstation && has(resource::workstation_message);
        const bool login = workstation && has(resource::workstation_login);
        const bool generic =
            !options && pda == Pda::none && !phone && !save && !load && !help && !workstation;
        if ((generic && script.buttons.empty()) || (generic && script.text_input && keyboard) ||
            ((save || login ||
              (workstation &&
               (has(resource::workstation_search) || has(resource::workstation_media)))) &&
             keyboard)) {
            return false;
        }
        items = std::move(script.buttons);
        if (generic) {
            const auto edge = [](const RECT& rect) {
                return (rect.left == 0 && rect.right <= 16) ||
                       (rect.top >= 467 && rect.bottom == 480) ||
                       (rect.top == 0 && rect.bottom <= 13) ||
                       (rect.left >= 624 && rect.right == 640);
            };
            if (cancel || change_group) {
                const auto back = std::find_if(items.begin(), items.end(), edge);
                if (back != items.end()) {
                    point_controller(window, *back, true);
                }
                return true;
            }
            std::erase_if(items, edge);
            if (!keyboard && script.text_input && !script.resources.empty()) {
                edit_resource = script.resources.back();
                for (const auto& item : items) {
                    if (std::any_of(script.fields.begin(), script.fields.end(),
                                    [&](const auto& field) {
                                        return field.bounds.left >= item.left &&
                                               field.bounds.top >= item.top &&
                                               field.bounds.right <= item.right &&
                                               field.bounds.bottom <= item.bottom &&
                                               field.bounds.right - field.bounds.left > 10;
                                    })) {
                        edit_targets.push_back(item);
                    }
                }
            }
        }
        if (pda != Pda::none) {
            // The active tab is artwork without a callback, but remains a focus anchor.
            std::erase_if(items, toolbar);
            items.insert(items.end(), pda_toolbar.begin(), pda_toolbar.end());
        }
        if (!keyboard && login) {
            edit_targets = {{253, 219, 455, 247}, {252, 280, 455, 310}};
            edit_resource = resource::workstation_login;
        } else if (!keyboard && workstation && has(resource::workstation_search)) {
            edit_targets = {{330, 249, 610, 269}};
            edit_resource = resource::workstation_search;
            items.push_back(edit_targets.front());
        } else if (!keyboard && workstation && has(resource::workstation_media)) {
            edit_targets = {workstation_media_field};
            edit_resource = resource::workstation_media;
            items.push_back(edit_targets.front());
        }
        if (!keyboard && save) {
            edit_targets = {{198, 161, 443, 192}, {197, 218, 443, 251}, {197, 276, 442, 309}};
            edit_resource = resource::save;
        }
        if (help) {
            items = std::move(script.hover_buttons);
            items.push_back({420, 440, 618, 466});
        }
        if (cancel) {
            if (workstation && !workstation_message && !login &&
                !has(resource::workstation_welcome)) {
                POINT cursor{};
                if (GetCursorPos(&cursor) && ScreenToClient(window, &cursor) && cursor.x > 115) {
                    point_controller(window, RECT{24, 98, 113, 121});
                    return true;
                }
            }
            const POINT back = options || help       ? POINT{511, 460}
                               : phone               ? POINT{390, 27}
                               : workstation_message ? POINT{94, 349}
                               : workstation && (login || has(resource::workstation_welcome))
                                   ? POINT{8, 240}
                               : workstation         ? POINT{69, 249}
                               : pda == Pda::city    ? POINT{395, 373}
                               : pda == Pda::message ? POINT{314, 373}
                               : pda != Pda::none    ? POINT{384, 399}
                                                     : POINT{250, 391};
            click_at(window, items, back);
            return true;
        }
        if (workstation) {
            std::erase_if(items, [](const RECT& item) {
                return item.left < 16 || item.top < 13 || item.right > 622 || item.bottom > 467;
            });
        }
        if (pda == Pda::inbox || (workstation && has(resource::workstation_inbox))) {
            const LONG left = pda == Pda::inbox ? 226 : 183;
            const LONG right = pda == Pda::inbox ? 421 : 598;
            const auto populated = [&](const RECT& item) {
                return std::any_of(script.text.begin(), script.text.end(), [&](const RECT& text) {
                    return text.left >= item.left && text.top >= item.top &&
                           text.right <= item.right && text.bottom <= item.bottom;
                });
            };
            const bool have_text = std::any_of(items.begin(), items.end(), [&](const RECT& item) {
                return item.left == left && item.right == right && populated(item);
            });
            if (have_text) {
                std::erase_if(items, [&](const RECT& item) {
                    return item.left == left && item.right == right && !populated(item);
                });
            }
        }
        std::sort(items.begin(), items.end(), [](const RECT& a, const RECT& b) {
            const auto ay = a.top + a.bottom, by = b.top + b.bottom;
            return ay == by ? a.left + a.right < b.left + b.right : ay < by;
        });
        items.erase(std::unique(items.begin(), items.end(),
                                [](const RECT& a, const RECT& b) {
                                    return a.left + a.right == b.left + b.right &&
                                           a.top + a.bottom == b.top + b.bottom;
                                }),
                    items.end());
    } else {
        return false;
    }
    POINT cursor{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(window, &cursor)) {
        return true;
    }
    if (workstation && change_group) {
        const auto target = std::find_if(items.begin(), items.end(), [&](const RECT& item) {
            return (cursor.x <= 115) != (item.left < 115);
        });
        if (target != items.end()) {
            point_controller(window, *target);
        }
        return true;
    }
    if (activate && settings_link_visible() && PtInRect(&settings_link, cursor)) {
        request_settings(window);
        return true;
    }
    if (workstation_message && cursor.x > 150 && vertical) {
        click_at(window, items, POINT{607, vertical < 0 ? 107 : 443});
        return true;
    }
    const bool in_toolbar = cursor.y >= 386 && cursor.y < 412;
    const LONG active_tab = pda == Pda::notes                          ? 281
                            : pda == Pda::addresses                    ? 247
                            : pda == Pda::inbox || pda == Pda::message ? 350
                                                                       : 315;
    if (pda != Pda::none && activate && in_toolbar &&
        std::any_of(pda_toolbar.begin(), pda_toolbar.end(), [&](const RECT& item) {
            return PtInRect(&item, cursor) && PtInRect(&item, POINT{active_tab, 399});
        })) {
        return true;
    }
    if (pda != Pda::none && change_group) {
        if (in_toolbar && (pda == Pda::notes || pda == Pda::message)) {
            point_controller(window, RECT{250, 180, 350, 220});
            return true;
        }
        const auto target = std::find_if(items.begin(), items.end(), [&](const RECT& item) {
            return in_toolbar ? !toolbar(item) : PtInRect(&item, POINT{active_tab, 399}) != FALSE;
        });
        if (target != items.end()) {
            point_controller(window, *target);
        }
        return true;
    }
    if (pda != Pda::none) {
        if (!in_toolbar) {
            if ((pda == Pda::notes || pda == Pda::message) && vertical) {
                const POINT point = pda == Pda::notes ? POINT{422, vertical < 0 ? 95 : 339}
                                                      : POINT{428, vertical < 0 ? 62 : 351};
                click_at(window, items, point);
                return true;
            }
            if (pda == Pda::notes && horizontal) {
                click_at(window, items, POINT{horizontal < 0 ? 355 : 390, 373});
                return true;
            }
            if (pda == Pda::addresses && (horizontal || vertical)) {
                const auto direction = vertical ? vertical : horizontal;
                click_at(window, items, POINT{direction < 0 ? 352 : 383, 373});
                return true;
            }
        }
        std::erase_if(items, [&](const RECT& item) { return toolbar(item) != in_toolbar; });
        if (in_toolbar) {
            horizontal = horizontal ? horizontal : vertical;
            vertical = 0;
        }
    }
    if (items.empty()) {
        return true;
    }
    auto selected = directional_target(items, cursor, horizontal, vertical);
    if (activate && !edit_targets.empty()) {
        const auto edited =
            std::find_if(edit_targets.begin(), edit_targets.end(),
                         [&](const RECT& field) { return PtInRect(&field, cursor); });
        if (edited != edit_targets.end()) {
            open_text_entry(window, *edited, edit_resource);
            return true;
        }
    }
    if (selected >= 0) {
        point_controller(window, items[selected], activate);
    } else if (activate) {
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (PtInRect(&items[index], cursor)) {
                selected = static_cast<int>(index);
                break;
            }
        }
        if (selected < 0 && modal) {
            point_controller(window, items.front());
        } else if (selected < 0 && settings().analog_cursor) {
            point_controller(window, RECT{cursor.x, cursor.y, cursor.x + 1, cursor.y + 1}, true);
        } else {
            point_controller(window, items[selected < 0 ? 0 : selected], true);
        }
    }
    return true;
}

}
