#include "browser.h"
#include "assets.h"
#include "source.h"
#include <shlobj.h>
#include "standalone_preview.h"
#include "settings.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <memory>
#include <stdexcept>

const Settings& settings() {
    static const Settings defaults;
    return defaults;
}

void trace_value(const char*, std::uint32_t) {}

namespace {
constexpr wchar_t host_class[] = L"XFilesStandaloneDatabase";
constexpr unsigned open_id = 4100, folder_id = 4103;
constexpr UINT open_source_message = WM_APP + 1;
using namespace devtools::standalone;

struct Host {
    HWND window = nullptr, open = nullptr, folder = nullptr, path = nullptr, browser = nullptr;
    HMODULE module = nullptr;
    HFONT font = nullptr;
    std::filesystem::path root;
    std::filesystem::path pending_source;
    std::unique_ptr<PreviewWindow> preview;
};

HWND child(HWND parent, HMODULE module, HFONT font, const wchar_t* type, const wchar_t* caption,
           DWORD style, unsigned id = 0) {
    const auto window =
        CreateWindowExW(0, type, caption, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, parent,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), module, nullptr);
    if (!window) {
        throw std::runtime_error("Cannot create browser window controls");
    }
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    return window;
}

void failure(HWND owner, const std::exception& error) {
    MessageBoxA(owner, error.what(), "The X-Files dev tools", MB_OK | MB_ICONERROR);
}

void open_preview(Host& host, const std::filesystem::path& requested) {
    const auto path = devtools::database_asset_path(requested);
    if (!path || !devtools::database_asset_previewable(*path) ||
        !devtools::database_asset_file(host.root, *path)) {
        throw std::runtime_error("The asset is missing or its format cannot be previewed");
    }
    auto next = std::make_unique<PreviewWindow>();
    next->module = host.module;
    next->font = host.font;
    next->player = std::make_unique<devtools::Preview>(host.root, *path);
    if (host.preview && host.preview->window) {
        DestroyWindow(host.preview->window);
    }
    host.preview = std::move(next);
    const auto window =
        CreateWindowExW(WS_EX_CONTROLPARENT, preview_class, path->generic_wstring().c_str(),
                        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 760,
                        600, host.window, nullptr, host.module, host.preview.get());
    if (!window) {
        throw std::runtime_error("Cannot create asset preview window");
    }
    ShowWindow(window, SW_SHOW);
    SetFocus(host.preview->play);
}

void layout(Host& state) {
    RECT bounds{};
    GetClientRect(state.window, &bounds);
    MoveWindow(state.open, 12, 10, 130, 28, TRUE);
    MoveWindow(state.folder, 150, 10, 130, 28, TRUE);
    MoveWindow(state.path, 292, 14, std::max(1L, bounds.right - 304), 24, TRUE);
    MoveWindow(state.browser, 12, 50, std::max(1L, bounds.right - 24),
               std::max(1L, bounds.bottom - 62), TRUE);
}

void open_database(Host& state, std::filesystem::path requested) {
    const auto source = devtools::browser_source(requested);
    const auto next = devtools::create_database_browser(
        state.window, state.module, state.font, source.root,
        [&state](const auto& path) {
            const auto file = devtools::database_asset_file(state.root, path);
            if (!file) {
                throw std::runtime_error("The asset is missing or its path is unsafe");
            }
            const auto source = devtools::browser_source(*file);
            if (!source.database.empty()) {
                state.pending_source = *file;
                PostMessageW(state.window, open_source_message, 0, 0);
            } else {
                open_preview(state, path);
            }
        },
        {}, source.database, source.database.empty(), source.asset);
    if (state.preview && state.preview->window) {
        DestroyWindow(state.preview->window);
    }
    if (state.browser) {
        DestroyWindow(state.browser);
    }
    state.root = source.root;
    state.browser = next;
    SetWindowTextW(state.path, requested.c_str());
    layout(state);
    ShowWindow(next, SW_SHOW);
    SetFocus(GetDlgItem(next, 3101));
    if (!source.asset.empty() && IsWindowEnabled(GetDlgItem(next, 3115))) {
        SendMessageW(next, WM_COMMAND, 3115, 0);
    }
}

void choose_database(Host& state) {
    std::wstring path(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = state.window;
    dialog.lpstrFilter = L"Game assets and "
                         L"databases\0*.hdb;*.gam;*.x;*.pff;*.xtx;*.xt;*.hot;*.nmv;*.xmv;*.mov;"
                         L"*.amv;*.dmv;*.mus;*.ttr;*.ttf;XFiles?.dll\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrTitle = L"Open The X-Files database or asset";
    dialog.lpstrInitialDir = state.root.empty() ? nullptr : state.root.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) {
        path.resize(wcslen(path.c_str()));
        open_database(state, path);
    }
}

void choose_folder(Host& state) {
    IFileDialog* dialog = nullptr;
    const auto created = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&dialog));
    if (FAILED(created)) {
        throw std::runtime_error("Cannot open the folder picker");
    }

    struct DialogOwner {
        IFileDialog* value;

        ~DialogOwner() {
            value->Release();
        }
    } owner{dialog};

    DWORD options = 0;
    if (FAILED(dialog->GetOptions(&options)) ||
        FAILED(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM)) ||
        FAILED(dialog->SetTitle(L"Browse an asset folder"))) {
        throw std::runtime_error("Cannot configure the folder picker");
    }
    if (!state.root.empty()) {
        IShellItem* initial = nullptr;
        if (SUCCEEDED(
                SHCreateItemFromParsingName(state.root.c_str(), nullptr, IID_PPV_ARGS(&initial)))) {
            const auto selected = dialog->SetFolder(initial);
            initial->Release();
            if (FAILED(selected)) {
                throw std::runtime_error("Cannot set the current asset folder");
            }
        }
    }
    const auto shown = dialog->Show(state.window);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return;
    }
    IShellItem* item = nullptr;
    if (FAILED(shown) || FAILED(dialog->GetResult(&item))) {
        throw std::runtime_error("Cannot select the asset folder");
    }
    PWSTR path = nullptr;
    const auto selected = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    item->Release();
    if (FAILED(selected)) {
        throw std::runtime_error("Cannot locate the selected folder");
    }
    const std::filesystem::path requested(path);
    CoTaskMemFree(path);
    open_database(state, requested);
}

LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM value, LPARAM data) {
    auto* state = reinterpret_cast<Host*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(data)->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) {
        return DefWindowProcW(window, message, value, data);
    }
    try {
        if (message == WM_CREATE) {
            state->open = child(window, state->module, state->font, L"BUTTON", L"Open file...",
                                BS_PUSHBUTTON | WS_TABSTOP, open_id);
            state->folder = child(window, state->module, state->font, L"BUTTON", L"Open folder...",
                                  BS_PUSHBUTTON | WS_TABSTOP, folder_id);
            state->path = child(window, state->module, state->font, L"STATIC",
                                L"Open a folder, HDB, PFF or asset file", SS_LEFT);
            SetTimer(window, 1, 500, nullptr);
            return 0;
        }
        if (message == WM_SIZE) {
            layout(*state);
            return 0;
        }
        if (message == WM_GETMINMAXINFO) {
            reinterpret_cast<MINMAXINFO*>(data)->ptMinTrackSize = {1000, 810};
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(value) == open_id) {
            choose_database(*state);
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(value) == folder_id) {
            choose_folder(*state);
            return 0;
        }
        if (message == open_source_message && !state->pending_source.empty()) {
            auto requested = std::move(state->pending_source);
            state->pending_source.clear();
            open_database(*state, requested);
            return 0;
        }
        if (message == WM_TIMER) {
            devtools::update_database_browser(state->browser);
            return 0;
        }
        if (message == WM_DESTROY) {
            KillTimer(window, 1);
            if (state->preview && state->preview->window) {
                DestroyWindow(state->preview->window);
            }
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception& error) {
        if (message == WM_TIMER) {
            KillTimer(window, 1);
        }
        failure(window, error);
        return message == WM_CREATE ? -1 : 0;
    }
    return DefWindowProcW(window, message, value, data);
}
}

int WINAPI wWinMain(_In_ HINSTANCE module, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int show) {
    try {
        const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(initialized)) {
            throw std::runtime_error("Cannot initialize the asset browser");
        }

        struct ComOwner {
            ~ComOwner() {
                CoUninitialize();
            }
        } com;

        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
        if (!InitCommonControlsEx(&controls)) {
            throw std::runtime_error("Cannot initialize browser controls");
        }
        for (const auto& entry :
             {std::pair{host_class, host_proc}, std::pair{preview_class, preview_proc}}) {
            WNDCLASSEXW type{};
            type.cbSize = sizeof(type);
            type.lpfnWndProc = entry.second;
            type.hInstance = module;
            type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            type.hIcon = LoadIconW(module, MAKEINTRESOURCEW(101));
            type.hIconSm = static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                                         GetSystemMetrics(SM_CXSMICON),
                                                         GetSystemMetrics(SM_CYSMICON), LR_SHARED));
            type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
            type.lpszClassName = entry.first;
            if (!RegisterClassExW(&type)) {
                throw std::runtime_error("Cannot register browser window");
            }
        }
        Host host;
        host.module = module;
        host.font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        std::wstring executable(32768, L'\0');
        const auto length =
            GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length && length < executable.size()) {
            executable.resize(length);
            host.root = std::filesystem::path(executable).parent_path();
        }
        const auto window =
            CreateWindowExW(WS_EX_CONTROLPARENT, host_class, L"The X-Files dev tools",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                            1200, 850, nullptr, nullptr, module, &host);
        if (!window) {
            throw std::runtime_error("Cannot create database browser window");
        }
        int count = 0;
        auto* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (arguments && count > 1) {
            const std::filesystem::path requested(arguments[1]);
            LocalFree(arguments);
            arguments = nullptr;
            try {
                open_database(host, requested);
            } catch (const std::exception& error) {
                failure(window, error);
            }
        }
        LocalFree(arguments);
        ShowWindow(window, show);
        if (!host.browser) {
            SetFocus(host.open);
        }
        MSG message{};
        for (;;) {
            const auto result = GetMessageW(&message, nullptr, 0, 0);
            if (result <= 0) {
                return result < 0 ? 1 : static_cast<int>(message.wParam);
            }
            const auto root = GetAncestor(message.hwnd, GA_ROOT);
            const auto owner = root ? root : window;
            if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
                SendMessageW(owner, WM_CLOSE, 0, 0);
            } else if (!IsDialogMessageW(owner, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    } catch (const std::exception& error) {
        failure(nullptr, error);
        return 1;
    }
}
