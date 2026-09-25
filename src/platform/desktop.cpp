#include "desktop.h"

#include <algorithm>
#include <stdexcept>
#include <windows.h>

Desktop desktop_layout() {
    Desktop desktop;

    for (DWORD index = 0;; ++index) {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(nullptr, index, &device, 0)) {
            break;
        }
        if (!(device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) {
            continue;
        }

        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsExW(device.DeviceName, ENUM_CURRENT_SETTINGS, &mode, 0)) {
            throw std::runtime_error("Cannot read monitor settings.");
        }
        desktop.push_back({device.DeviceName, mode.dmPosition.x, mode.dmPosition.y,
                           mode.dmPelsWidth, mode.dmPelsHeight, mode.dmBitsPerPel,
                           mode.dmDisplayFrequency, mode.dmDisplayOrientation});
    }

    if (desktop.empty()) {
        throw std::runtime_error("No active monitors found.");
    }
    std::sort(desktop.begin(), desktop.end(),
              [](const Display& left, const Display& right) { return left.name < right.name; });
    return desktop;
}

void write_desktop(std::ostream& output, const Desktop& desktop) {
    for (const auto& display : desktop) {
        for (wchar_t character : display.name) {
            output << static_cast<char>(character <= 127 ? character : '?');
        }
        output << ' ' << display.width << 'x' << display.height << " at " << display.x << ','
               << display.y << " bits=" << display.bits << " hz=" << display.frequency
               << " orientation=" << display.orientation << '\n';
    }
}
