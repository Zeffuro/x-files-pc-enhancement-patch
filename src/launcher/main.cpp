#include "application.h"

#include <windows.h>
#include <exception>

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    try {
        return resume_game(application_directory());
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "The X-Files", MB_OK | MB_ICONERROR);
        return 1;
    }
}
