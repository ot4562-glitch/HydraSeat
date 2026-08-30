#ifdef _WIN32
#include "hydra/gui_win32.hpp"
#endif

#include <iostream>

#ifdef _WIN32
namespace {

void enableBestEffortDpiAwareness() noexcept {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) return;

    using SetContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const auto setContext = reinterpret_cast<SetContext>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setContext != nullptr &&
        setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        return;
    }

    using SetLegacy = BOOL(WINAPI*)();
    const auto setLegacy = reinterpret_cast<SetLegacy>(
        GetProcAddress(user32, "SetProcessDPIAware"));
    if (setLegacy != nullptr) (void)setLegacy();
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)pCmdLine;

    enableBestEffortDpiAwareness();
    hydra::gui::Win32App guiApp;
    if (guiApp.initialize(hInstance, nCmdShow)) {
        return guiApp.run();
    }
    return 0;
}
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    return wWinMain(GetModuleHandle(NULL), NULL, GetCommandLineW(), SW_SHOW);
#else
    (void)argc;
    (void)argv;
    std::cout << "HydraSeat GUI requires Windows." << std::endl;
    return 0;
#endif
}
