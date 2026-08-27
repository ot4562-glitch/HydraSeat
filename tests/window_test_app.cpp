#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

#ifdef _WIN32

std::atomic<int> liveWindows{0};
std::atomic<bool> fightPosition{false};
std::atomic<bool> fightGuard{false};
constexpr UINT_PTR kSafetyTimer = 1;
constexpr UINT kFightPositionMessage = WM_APP + 42u;

LRESULT CALLBACK fixtureWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SETTEXT: {
            const LRESULT result = DefWindowProcW(window, message, wParam, lParam);
            NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, window, OBJID_WINDOW, CHILDID_SELF);
            return result;
        }
        case WM_WINDOWPOSCHANGED: {
            const LRESULT result = DefWindowProcW(window, message, wParam, lParam);
            if (fightPosition.load(std::memory_order_acquire) &&
                !fightGuard.load(std::memory_order_acquire)) {
                wchar_t title[64]{};
                const int copied = GetWindowTextW(window, title, 64);
                if (copied > 0 && std::wstring_view(title, static_cast<std::size_t>(copied)) ==
                                      L"Hydra Game") {
                    (void)PostMessageW(window, kFightPositionMessage, 0, 0);
                }
            }
            return result;
        }
        case kFightPositionMessage:
            fightGuard.store(true, std::memory_order_release);
            (void)SetWindowPos(window, nullptr, 160, 120, 640, 400,
                               SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
            fightGuard.store(false, std::memory_order_release);
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (liveWindows.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                PostQuitMessage(0);
            }
            return 0;
        case WM_TIMER:
            if (wParam == kSafetyTimer) {
                DestroyWindow(window);
                return 0;
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

std::filesystem::path selfPath() {
    std::wstring buffer(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                             static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

std::wstring quote(std::wstring_view argument) {
    if (argument.empty()) return L"\"\"";
    if (argument.find_first_of(L" \t\"") == std::wstring_view::npos) {
        return std::wstring(argument);
    }
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') {
            ++slashes;
        } else if (ch == L'\"') {
            result.append(slashes * 2u + 1u, L'\\');
            result.push_back(L'\"');
            slashes = 0;
        } else {
            result.append(slashes, L'\\');
            slashes = 0;
            result.push_back(ch);
        }
    }
    result.append(slashes * 2u, L'\\');
    result.push_back(L'\"');
    return result;
}

bool spawnGameChild() {
    const auto executable = selfPath();
    std::wstring command = quote(executable.wstring()) + L" --mode game";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executable.c_str(), mutableCommand.data(),
                                        nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                        nullptr, executable.parent_path().c_str(),
                                        &startup, &process);
    if (created == FALSE) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

HWND createWindow(const wchar_t* className, const wchar_t* title,
                  DWORD style, DWORD exStyle, int x, int y, int width, int height,
                  HWND owner = nullptr) {
    HWND window = CreateWindowExW(exStyle, className, title, style,
                                  x, y, width, height, owner, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    if (window == nullptr) return nullptr;
    liveWindows.fetch_add(1, std::memory_order_relaxed);
    (void)SetTimer(window, kSafetyTimer, 15000u, nullptr);
    ShowWindow(window, SW_SHOWNA);
    UpdateWindow(window);
    return window;
}

#endif

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    std::string mode = "launcher";
    bool spawnChild = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--mode" && index + 1 < argc) {
            mode = argv[++index];
        } else if (argument == "--spawn-child") {
            spawnChild = true;
        } else if (argument == "--fight-position") {
            fightPosition.store(true, std::memory_order_release);
        } else {
            return 2;
        }
    }

    const wchar_t* className = L"HydraSeatP4WindowFixture";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = fixtureWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (RegisterClassW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 3;
    }

    HWND first = nullptr;
    HWND second = nullptr;
    if (mode == "launcher") {
        first = createWindow(className, L"Hydra Launcher", WS_OVERLAPPEDWINDOW,
                             0, 40, 40, 500, 320);
        second = createWindow(className, L"Hydra Launcher Extra", WS_OVERLAPPEDWINDOW,
                              0, 100, 100, 360, 240);
        if (spawnChild && !spawnGameChild()) return 4;
    } else if (mode == "game") {
        first = createWindow(className, L"Hydra Game", WS_OVERLAPPEDWINDOW,
                             0, 160, 120, 640, 400);
        second = createWindow(className, L"Hydra Game Popup", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                              WS_EX_TOOLWINDOW, 220, 180, 280, 160, first);
    } else if (mode == "unowned") {
        first = createWindow(className, L"Hydra Unowned", WS_OVERLAPPEDWINDOW,
                             0, 250, 200, 420, 260);
    } else {
        return 5;
    }

    if (first == nullptr || ((mode == "launcher" || mode == "game") && second == nullptr)) {
        return 6;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
#else
    (void)argc;
    (void)argv;
    return 0;
#endif
}
