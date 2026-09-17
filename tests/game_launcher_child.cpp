#include "hydra/xinput_adapter_session.hpp"

#if defined(_WIN32)
#include <windows.h>

#include <cwchar>
#include <string>

namespace {

bool parseUnsigned(const wchar_t* text, unsigned long long& value) {
    if (text == nullptr || *text == L'\0') return false;
    wchar_t* end = nullptr;
    value = std::wcstoull(text, &end, 10);
    return end != text && end != nullptr && *end == L'\0';
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    SetErrorMode(SEM_FAILCRITICALERRORS |
                 SEM_NOGPFAULTERRORBOX |
                 SEM_NOOPENFILEERRORBOX);

    if (argc != 5) return 2;

    const auto config = hydra::controller::adapter::loadSessionConfigFromEnvironment();
    if (!config) return 3;
    if (config->pipeEndpoint != argv[1]) return 4;

    unsigned long long expectedSeat = 0;
    unsigned long long expectedSourceGeneration = 0;
    if (!parseUnsigned(argv[2], expectedSeat) ||
        !parseUnsigned(argv[3], expectedSourceGeneration)) {
        return 5;
    }
    if (config->seatId != expectedSeat) return 6;
    if (config->sourceGeneration != expectedSourceGeneration) return 7;
    if (config->activationGeneration == 0) return 8;

    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[4]);
    if (ready == nullptr) return 9;
    const BOOL signaled = SetEvent(ready);
    CloseHandle(ready);
    if (signaled == FALSE) return 10;

    // Bound orphan lifetime if the parent test crashes before cleanup.
    Sleep(30000);
    return 0;
}
#else
int main() {
    return 2;
}
#endif
