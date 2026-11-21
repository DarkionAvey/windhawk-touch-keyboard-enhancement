// ==WindhawkMod==
// @id              touch-keyboard-enhancement
// @name            Touch Keyboard Enhancement
// @description     Compact touch keyboard with extra keys, no auto-dismiss, an edge-docking bubble and easier swipe typing
// @version         0.46
// @author          engrymoon
// @include         TextInputHost.exe
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -lgdi32 -luser32 -lruntimeobject -lshcore -lmsimg32 -loleaut32 -ladvapi32 -ldwmapi
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Touch Keyboard Enhancement for Swipe keyboard 

Symbols resolved from Microsoft public PDBs.

* **Bubble.** A floating bubble appears whenever the touch keyboard is hidden.
  Closing with X starts it at the close button. It glides to
  the nearest screen edge, can be flung around and always docks to an edge,
  fades after 500 ms idle, and reopens the keyboard on tap. It lives in
  explorer (popups inside TextInputHost never become visible).
  By default the bubble replaces the Touch keyboard tray icon: the icon is
  hidden while the bubble is on (if it was configured to show), and your own
  tray setting is restored when the option, the bubble or the mod is turned off.
* **No auto-dismiss.** With Disable auto-dismiss on, the compact keyboard can
  still open automatically but only closes with its X button. Shell and app
  hide requests (Start, physical typing, focus changes) are ignored.
* **Swipe.** Shape writing is forced on in the compact keyboard, and the
  swipe-start distance can be shortened so short words of nearby letters
  (we, as, the) swipe. Windows recognizes the word in the app, so classic
  IMM32 edit controls (e.g. Notepad++) still receive no swiped words.
* **Width.** The compact panel width can be adjusted (50-200%) independently
  of the Windows keyboard size. It takes effect on the next keyboard opening.
* **Extra keys.** Two rows of functional keys (Ctrl, Win, Alt, Tab, Menu,
  Esc, Del, clipboard, ★, Print Screen, Back/Forward, Home/End and arrows)
  are added inside the compact keyboard frame.
* **Shortcut keys.** Tap Ctrl or Alt before a shortcut key, or tap it again to
  cancel. Tap Win to open Start, or hold it for half a second and release to
  arm it for a shortcut such as Win+V. Tab also supports tap and hold.
  ★ runs the shortcut specified in Settings.
* **Wider Enter.** The optional Hide voice typing button setting gives the
  microphone key's space to Enter in the compact layout.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- hideVoiceTypingButton: false
  $name: Hide voice typing button
  $description: "Hides the microphone key on the compact touch keyboard and uses its space to widen Enter."
- noAutoHide: false
  $name: Disable auto-dismiss
  $description: Keeps the touch keyboard open when Windows requests automatic dismissal. The keyboard can still be closed with its X button.
- bubbleOnClose: true
  $name: Show floating bubble while keyboard is hidden
- hideTrayIconWithBubble: true
  $name: Hide the Touch keyboard tray icon while the bubble is on
  $description: The bubble replaces the tray icon. Your original tray icon setting is restored when this, the bubble or the mod is turned off.
- bubbleIdleOpacity: 35
  $name: Bubble opacity when idle (%)
- bubbleIdleDelay: 500
  $name: Fade the bubble after (ms)
- noSwipeStartDistance: false
  $name: Use custom swipe-start distance
  $description: Use the distance below. Turn off to use the Windows threshold (about 1.8 key widths horizontally).
- swipeStartDistancePercent: 50
  $name: Swipe-start distance (% of key width)
  $description: "0 bypasses the distance check. 50 is half a key width and 100 is one full key width. Very low values can turn taps into swipes."
- forceSwipe: true
  $name: Force swipe (shape writing) in every field
  $description: Enables swipe typing in the compact keyboard for every field the keyboard serves.
- horizontalScale: 100
  $name: Compact keyboard width (%)
  $description: "50–200. Adjusts key and frame width independently of height (use the Windows keyboard size setting for overall size). Reopen the keyboard to apply."
- specialKey: "Shift+Enter"
  $name: ★ shortcut
  $description: "Use + between a key and its modifiers. Examples: Shift+Enter, Alt+Tab. Ctrl+Alt+Delete is reserved by Windows and cannot be sent by this key."
- verbose: false
  $name: Verbose logging
  $description: Enables diagnostic messages and detailed function and branch traces for troubleshooting.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>
#include <windows.h>
#include <dwmapi.h>
#include <aclapi.h>
#include <objbase.h>
#include <winstring.h>
#include <shellscalingapi.h>
#include <windowsx.h>
#include <atomic>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <exception>
#include <array>
#include <cstring>
#include <limits>
#include <cwctype>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.ViewManagement.h>

std::atomic<bool> g_verboseLogging{false};

// Keep each diagnostic line identifiable across both host processes.
static void TraceLog(const char* function, unsigned line, PCWSTR event) {
    if (!g_verboseLogging) return;
    static thread_local bool logging;
    if (logging) return;
    const DWORD lastError = GetLastError();
    logging = true;
    Wh_Log(L"TKTRACE tick=%llu pid=%lu tid=%lu %hs:%u %s",
           static_cast<unsigned long long>(GetTickCount64()), GetCurrentProcessId(),
           GetCurrentThreadId(), function, line, event);
    logging = false;
    SetLastError(lastError);
}

struct TraceScope;
static thread_local TraceScope* t_traceScope;

struct TraceScope {
    const char* function;
    unsigned line;
    TraceScope* previous = t_traceScope;
    int exceptions = std::uncaught_exceptions();
    std::array<unsigned, 32> visited{};
    size_t count = 0;

    TraceScope(const char* function, unsigned line) : function(function), line(line) {
        if (!g_verboseLogging || (previous && previous->function == function)) return;
        t_traceScope = this;
        TraceLog(function, line, L"enter");
    }
    ~TraceScope() {
        if (t_traceScope != this) return;
        TraceLog(function, line, std::uncaught_exceptions() > exceptions ? L"unwind" : L"return");
        t_traceScope = previous;
    }
};

static void TraceBlock(const char* function, unsigned line) {
    if (!g_verboseLogging) return;
    if (auto scope = t_traceScope) {
        // Count a block once per call, including blocks inside hot loops.
        const auto end = scope->visited.begin() + scope->count;
        if (std::find(scope->visited.begin(), end, line) != end) return;
        if (scope->count < scope->visited.size()) scope->visited[scope->count++] = line;
    }
    TraceLog(function, line, L"block");
}

#define TRACE_SCOPE() TraceScope traceScope(__func__, __LINE__)
#define TRACE_BLOCK() TraceBlock(__func__, __LINE__)
#define TRACE_POINT(event) TraceLog(__func__, __LINE__, event)

struct Settings {
    bool hideVoiceTypingButton = false;
    bool noAutoHide = false;
    bool bubbleOnClose = false;
    bool hideTrayIcon = false;
    int bubbleIdleAlpha = 255;
    int bubbleIdleDelay = 500;
    bool customSwipeStartDistance = false;
    int swipeStartDistancePercent = 50;
    bool forceSwipe = false;
    int horizontalScale = 100;
    struct ShortcutChord {
        std::array<WORD, 5> keys{VK_SHIFT, VK_RETURN};
        unsigned count = 2;
    } specialKey;
};

Settings g_settings;
SRWLOCK g_settingsLock = SRWLOCK_INIT;
std::atomic<bool> g_unloading{false};
std::atomic<unsigned> g_inputSession{0};
std::mutex g_settingsWriter;
std::atomic<bool> g_textReady, g_inputAppReady, g_sizeReady;

static Settings ReadSettings() {
    TRACE_SCOPE();
    AcquireSRWLockShared(&g_settingsLock);
    const Settings settings = g_settings;
    ReleaseSRWLockShared(&g_settingsLock);
    return settings;
}

static void PublishSettings(const Settings& settings) {
    TRACE_SCOPE();
    AcquireSRWLockExclusive(&g_settingsLock);
    g_settings = settings;
    ReleaseSRWLockExclusive(&g_settingsLock);
}

static WORD ShortcutKey(std::wstring_view name) {
    struct NamedKey { PCWSTR name; WORD vk; };
    constexpr NamedKey names[] = {
        {L"Ctrl", VK_CONTROL}, {L"Control", VK_CONTROL}, {L"Alt", VK_MENU},
        {L"Shift", VK_SHIFT}, {L"Win", VK_LWIN}, {L"Windows", VK_LWIN},
        {L"Enter", VK_RETURN}, {L"Return", VK_RETURN}, {L"Tab", VK_TAB},
        {L"Delete", VK_DELETE}, {L"Del", VK_DELETE}, {L"Backspace", VK_BACK},
        {L"Esc", VK_ESCAPE}, {L"Escape", VK_ESCAPE}, {L"Space", VK_SPACE},
        {L"Left", VK_LEFT}, {L"Right", VK_RIGHT}, {L"Up", VK_UP}, {L"Down", VK_DOWN},
        {L"Home", VK_HOME}, {L"End", VK_END}, {L"Insert", VK_INSERT},
        {L"PageUp", VK_PRIOR}, {L"PageDown", VK_NEXT}, {L"Menu", VK_APPS},
    };
    for (const auto& key : names) {
        if (CompareStringOrdinal(name.data(), static_cast<int>(name.size()), key.name, -1, TRUE) == CSTR_EQUAL)
            return key.vk;
    }
    if (name.size() == 1) {
        const auto c = towupper(name.front());
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return static_cast<WORD>(c);
    }
    if (name.size() >= 2 && name.size() <= 3 && towupper(name.front()) == L'F') {
        unsigned n = 0;
        for (const auto c : name.substr(1)) {
            if (c < L'0' || c > L'9') return 0;
            n = 10 * n + c - L'0';
        }
        if (n >= 1 && n <= 24) return static_cast<WORD>(VK_F1 + n - 1);
    }
    return 0;
}

static bool IsModifierKey(WORD vk) {
    return vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN;
}

static Settings::ShortcutChord ParseShortcut(std::wstring_view text) {
    Settings::ShortcutChord chord{{}, 0};
    if (text.size() > 128) return chord;
    for (;;) {
        const auto plus = text.find(L'+');
        auto token = text.substr(0, plus);
        while (!token.empty() && iswspace(token.front())) token.remove_prefix(1);
        while (!token.empty() && iswspace(token.back())) token.remove_suffix(1);
        const WORD vk = token.empty() ? 0 : ShortcutKey(token);
        if (!vk || chord.count == chord.keys.size() || (plus != text.npos && !IsModifierKey(vk)) ||
            std::find(chord.keys.begin(), chord.keys.begin() + chord.count, vk) != chord.keys.begin() + chord.count)
            return {{}, 0};
        chord.keys[chord.count++] = vk;
        if (plus == text.npos) break;
        text.remove_prefix(plus + 1);
    }
    if (chord.keys[chord.count - 1] == VK_DELETE) {
        const auto end = chord.keys.begin() + chord.count;
        if (std::find(chord.keys.begin(), end, VK_CONTROL) != end &&
            std::find(chord.keys.begin(), end, VK_MENU) != end) return {{}, 0};
    }
    return chord;
}

static void SendShortcut(const Settings::ShortcutChord& chord) {
    const HWND target = GetForegroundWindow();
    if (!target) return;
    std::array<INPUT, 10> input{};
    UINT count = 0;
    for (unsigned i = 0; i < chord.count; ++i) {
        const WORD vk = chord.keys[i];
        if (IsModifierKey(vk) && (GetAsyncKeyState(vk) & 0x8000)) continue;
        const bool extended = vk == VK_LWIN || vk == VK_APPS || vk == VK_SNAPSHOT || vk == VK_INSERT || vk == VK_DELETE ||
                              (vk >= VK_PRIOR && vk <= VK_DOWN);
        input[count].type = INPUT_KEYBOARD;
        input[count].ki.wVk = vk;
        input[count].ki.dwFlags = extended ? KEYEVENTF_EXTENDEDKEY : 0;
        ++count;
    }
    if (!count) return;
    for (UINT i = 0; i < count; ++i) {
        input[count + i] = input[count - i - 1];
        input[count + i].ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    // Keep the Windows key and its partner in one batch so the shell
    // receives the complete hotkey without an interim standalone Win down.
    const bool winChord = std::find(chord.keys.begin(), chord.keys.begin() + chord.count,
                                    VK_LWIN) != chord.keys.begin() + chord.count;
    UINT sent = 0;
    if (!winChord && count > 1 && IsModifierKey(input[0].ki.wVk) &&
        !IsModifierKey(input[count - 1].ki.wVk)) {
        constexpr DWORD kShortcutModifierDelayMs = 50;
        const UINT modifierCount = count - 1;
        sent = SendInput(modifierCount, input.data(), sizeof(INPUT));
        if (sent == modifierCount) {
            Sleep(kShortcutModifierDelayMs);
            if (GetForegroundWindow() == target)
                sent += SendInput(count * 2 - sent, input.data() + sent, sizeof(INPUT));
        }
    } else {
        sent = SendInput(count * 2, input.data(), sizeof(INPUT));
    }
    if (sent != count * 2) {
        const UINT remaining = std::min(sent, count * 2 - sent);
        if (remaining) SendInput(remaining, input.data() + count * 2 - remaining, sizeof(INPUT));
        Wh_Log(L"Compact shortcut: Windows accepted %u of %u input events", sent, count * 2);
    }
}

bool g_isExplorer;

#define VLOG(...) do { if (g_verboseLogging) Wh_Log(__VA_ARGS__); } while (0)

static PCWSTR HStr(void* h) {
    TRACE_SCOPE();
    return h ? WindowsGetStringRawBuffer((HSTRING)h, nullptr) : L"(null)";
}

// ---------------------------------------------------------------------------
// State shared between TextInputHost and explorer
// ---------------------------------------------------------------------------
struct SharedState {
    LONG64 panelVisibilityDeadline;  // positive: visible; negative: hidden; zero: unknown
    LONG format;
    LONG bubbleVisibilityHint;
    alignas(8) LONG64 bubbleClosePoint;
    alignas(8) LONG64 compactModeDeadline;
};
SharedState* g_shared;
HANDLE g_sharedMap;
HANDLE g_bubbleEvt;  // auto-reset; requests an immediate visibility check
constexpr LONG kSharedFormat = 0x544B0008;
constexpr LONG kBubbleCloseHint = 1;
constexpr LONG kBubbleShowHint = 2;
constexpr LONG64 kNoBubbleClosePoint = INT64_MIN;

static bool OwnedByUser(HANDLE object, PSID user) {
    TRACE_SCOPE();
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    const DWORD result = GetSecurityInfo(object, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION,
                                         &owner, nullptr, nullptr, nullptr, &sd);
    const bool matches = result == ERROR_SUCCESS && owner && EqualSid(user, owner);
    if (sd) LocalFree(sd);
    return matches;
}

void InitShared() {
    TRACE_SCOPE();
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return;
    alignas(TOKEN_USER) BYTE tokenData[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    DWORD length = 0;
    const BOOL gotUser = GetTokenInformation(token, TokenUser, tokenData, sizeof(tokenData), &length);
    CloseHandle(token);
    if (!gotUser) return;
    const auto user = reinterpret_cast<const TOKEN_USER*>(tokenData)->User.Sid;

    EXPLICIT_ACCESS access{};
    // Object-specific low bits cover section map read/write and event modify.
    access.grfAccessPermissions = READ_CONTROL | SYNCHRONIZE | SECTION_MAP_READ |
                                  SECTION_MAP_WRITE | EVENT_MODIFY_STATE | SECTION_QUERY;
    access.grfAccessMode = SET_ACCESS;
    BuildTrusteeWithSid(&access.Trustee, user);
    PACL acl = nullptr;
    if (SetEntriesInAcl(1, &access, nullptr, &acl) != ERROR_SUCCESS) return;
    SECURITY_DESCRIPTOR sd;
    if (InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorOwner(&sd, user, FALSE) &&
        SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE)) {
        TRACE_BLOCK();
        SECURITY_ATTRIBUTES sa = {sizeof(sa), &sd, FALSE};
        constexpr auto mapName = L"Local\\WhTouchKeyboardEnhancement.State.v8";
        constexpr auto eventName = L"Local\\WhTouchKeyboardEnhancement.Bubble.v8";
        constexpr DWORD mapRights = FILE_MAP_READ | FILE_MAP_WRITE;
        g_sharedMap = OpenFileMapping(mapRights | READ_CONTROL, FALSE, mapName);
        if (!g_sharedMap && GetLastError() == ERROR_FILE_NOT_FOUND) {
            TRACE_BLOCK();
            g_sharedMap = CreateFileMapping(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0,
                                            sizeof(SharedState), mapName);
            if (!g_sharedMap && GetLastError() == ERROR_ACCESS_DENIED)
                g_sharedMap = OpenFileMapping(mapRights | READ_CONTROL, FALSE, mapName);
        }
        if (g_sharedMap && OwnedByUser(g_sharedMap, user)) {
            TRACE_BLOCK();
            g_shared = static_cast<SharedState*>(
                MapViewOfFile(g_sharedMap, mapRights, 0, 0, sizeof(SharedState)));
            if (g_shared) {
                TRACE_BLOCK();
                const LONG format = InterlockedCompareExchange(&g_shared->format, kSharedFormat, 0);
                if (format != 0 && format != kSharedFormat) {
                    TRACE_BLOCK();
                    UnmapViewOfFile(g_shared);
                    g_shared = nullptr;
                }
            }
        }
        if (g_shared) {
            TRACE_BLOCK();
            g_bubbleEvt = CreateEventEx(&sa, eventName, 0,
                                      SYNCHRONIZE | EVENT_MODIFY_STATE | READ_CONTROL);
            if (g_bubbleEvt && !OwnedByUser(g_bubbleEvt, user)) {
                TRACE_BLOCK();
                CloseHandle(g_bubbleEvt);
                g_bubbleEvt = nullptr;
            }
        }
    }
    LocalFree(acl);
    Wh_Log(L"shared state %s, bubble event %s", g_shared ? L"ok" : L"FAILED", g_bubbleEvt ? L"ok" : L"FAILED");
}

// ---------------------------------------------------------------------------
// Showing the keyboard: ITipInvocation on TabTip
// ---------------------------------------------------------------------------
static const CLSID CLSID_UIHostNoLaunch = {0x4CE576FA, 0x83DC, 0x4F88, {0x95, 0x1C, 0x9D, 0x07, 0x82, 0xB4, 0xE3, 0x76}};
static const IID IID_ITipInvocation = {0x37C994E7, 0x432B, 0x4834, {0xA2, 0xF7, 0xDC, 0xE1, 0xF1, 0x3B, 0x83, 0x4B}};
struct ITipInvocation : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Toggle(HWND wnd) = 0;
};

static bool ToggleKeyboard() {
    TRACE_SCOPE();
    ITipInvocation* tip = nullptr;
    TRACE_POINT(L"calling CoCreateInstance for ITipInvocation");
    HRESULT hr = CoCreateInstance(CLSID_UIHostNoLaunch, nullptr, CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER,
                                  IID_ITipInvocation, (void**)&tip);
    if (SUCCEEDED(hr) && tip) {
        TRACE_BLOCK();
        TRACE_POINT(L"calling ITipInvocation::Toggle");
        hr = tip->Toggle(GetDesktopWindow());
        TRACE_POINT(L"ITipInvocation::Toggle returned");
        tip->Release();
    }
    VLOG(L"ToggleKeyboard HRESULT=0x%08X", hr);
    return SUCCEEDED(hr) && tip != nullptr;
}

// ---------------------------------------------------------------------------
// Explorer worker thread and bubble window
// ---------------------------------------------------------------------------
HANDLE g_thread;
HANDLE g_threadStop;  // signalled on unload; the thread destroys its window and exits
std::mutex g_workerExitMutex;
bool g_workerExited;
bool g_workerOwnsSharedCleanup;
std::atomic<bool> g_requiresHostRestart;

// A stalled host callback may still be executing mod code when Windhawk unloads.
static bool KeepModMapped() {
    TRACE_SCOPE();
    HMODULE mod;
    if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&KeepModMapped), &mod)) return false;
    g_requiresHostRestart = true;
    return true;
}

static void CloseShared() {
    TRACE_SCOPE();
    if (g_shared) UnmapViewOfFile(g_shared);
    if (g_sharedMap) CloseHandle(g_sharedMap);
    if (g_bubbleEvt) CloseHandle(g_bubbleEvt);
    g_shared = nullptr;
    g_sharedMap = g_bubbleEvt = nullptr;
}

constexpr DWORD kKeyboardVisibilityPollMs = 250;

// TipbandDesiredVisibility: 0 never, 1 always, 2 when no keyboard is attached.
// The user's mode is persisted in mod storage while suppressed, so an explorer
// restart or crash in between still restores it.
static std::mutex g_trayIconMutex;
static DWORD g_savedTrayIconMode;
static bool g_savedTrayIconModeLoaded;

static void SaveTrayIconMode(DWORD mode) {
    g_savedTrayIconMode = mode;
    Wh_SetIntValue(L"savedTrayIconMode", static_cast<int>(mode));
}

static void SetTrayIconSuppressed(bool suppress) {
    constexpr auto keyPath = L"Software\\Microsoft\\TabletTip\\1.7";
    constexpr auto valueName = L"TipbandDesiredVisibility";
    {
        std::lock_guard lock(g_trayIconMutex);
        if (!g_savedTrayIconModeLoaded) {
            const int stored = Wh_GetIntValue(L"savedTrayIconMode", 0);
            g_savedTrayIconMode = stored == 1 || stored == 2 ? static_cast<DWORD>(stored) : 0;
            g_savedTrayIconModeLoaded = true;
        }
        if (suppress ? g_unloading.load() : g_savedTrayIconMode == 0) return;

        HKEY key;
        const auto open = RegOpenKeyEx(HKEY_CURRENT_USER, keyPath, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
        if (open != ERROR_SUCCESS) {
            if (open != ERROR_FILE_NOT_FOUND) Wh_Log(L"Touch keyboard tray setting: open failed (%ld)", open);
            return;
        }
        DWORD current = 0;
        DWORD bytes = sizeof(current);
        const auto read = RegGetValue(key, nullptr, valueName, RRF_RT_REG_DWORD, nullptr, &current, &bytes);
        // Leave missing values and unknown modes alone. A later explicit
        // Always or When no keyboard attached selection becomes the mode to restore.
        if (read != ERROR_SUCCESS || (suppress ? current != 1 && current != 2 : current != 0)) {
            if (read == ERROR_FILE_NOT_FOUND || (read == ERROR_SUCCESS && current != 0))
                SaveTrayIconMode(0);
            RegCloseKey(key);
            return;
        }
        const DWORD desired = suppress ? 0 : g_savedTrayIconMode;
        const auto write = RegSetValueEx(key, valueName, 0, REG_DWORD,
                                         reinterpret_cast<const BYTE*>(&desired), sizeof(desired));
        RegCloseKey(key);
        if (write != ERROR_SUCCESS) {
            Wh_Log(L"Touch keyboard tray setting: write failed (%ld)", write);
            return;
        }
        SaveTrayIconMode(suppress ? current : 0);
    }
    if (const HWND tray = FindWindow(L"Shell_TrayWnd", nullptr)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(tray, &pid);
        if (pid == GetCurrentProcessId()) {
            SendMessageTimeout(tray, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"TraySettings"),
                               SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, nullptr);
        }
    }
}
constexpr UINT_PTR kIdleTimer = 3;
HMONITOR g_frameMonitor;
DWORD g_monitorHz = 60;

static DWORD BubbleMonitorRefreshRate(HWND hwnd) {
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor != g_frameMonitor) {
        g_frameMonitor = monitor;
        g_monitorHz = 60;
        MONITORINFOEX info{};
        info.cbSize = sizeof(info);
        DEVMODE mode{};
        mode.dmSize = sizeof(mode);
        if (GetMonitorInfo(monitor, &info) &&
            EnumDisplaySettings(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
            mode.dmDisplayFrequency > 1) {
            g_monitorHz = mode.dmDisplayFrequency;
        }
    }
    return g_monitorHz;
}
constexpr LONG kMaxBubbleDiameter = 512;
constexpr float kBubbleHeadroom = 1.12f;  // window size / resting bubble size

// --- bubble (explorer) ---
// Messenger chat-head style: pops in where X was tapped, glides to a left or
// right edge on an under-damped spring, shrinks while pressed and springs
// back, follows the finger while dragged, flings on release with momentum,
// and fades after being idle.
struct Bubble {
    int d = 0;                 // diameter (px)
    float x = 0, y = 0;        // top-left (px)
    float vx = 0, vy = 0;      // px/s
    float tx = 0, ty = 0;      // spring target
    bool animating = false;
    LARGE_INTEGER lastTick{};
    // dragging
    bool down = false, dragged = false;
    UINT32 pointerId = 0;      // zero for a mouse drag
    POINT downPt{};
    float grabX = 0, grabY = 0;
    POINT lastPt{};
    // Recent input samples for a least-squares velocity fit (Android's
    // VelocityTracker approach), so a flick reads its real speed.
    struct Sample { double t; float x, y; };
    std::array<Sample, 20> samples{};
    size_t sampleCount = 0, sampleNext = 0;
    // scale spring (press feedback and pop-in)
    float scale = 1, scaleV = 0, scaleTarget = 1;
    float scaleK = 500, scaleZeta = 0.5f;
    // shrink-away exit after a tap opens the keyboard
    bool leaving = false;
    double leaveStart = 0;
    float leaveFromScale = 1;  // the exit continues from the current (e.g. pressed) scale
    // opacity
    int alpha = 255, alphaTarget = 255;
    ULONGLONG idleDeadline = 0;
    // rendering
    HDC memDC = nullptr;
    HBITMAP bmp = nullptr, oldBmp = nullptr;
} g_b;

static double Seconds(LARGE_INTEGER a, LARGE_INTEGER b) {
    TRACE_SCOPE();
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return double(b.QuadPart - a.QuadPart) / double(f.QuadPart);
}

// Bubble diameter in physical pixels on the monitor at pt.
static int BubbleDiameterAt(POINT pt) {
    TRACE_SCOPE();
    // TEMPLATE_PART_GripperCloseButton has Width=33 in the compact XAML dump.
    constexpr double closeButtonWidthDip = 33;
    // The window includes headroom for the scale spring's overshoot.
    const double diameterDip = 1.5 * closeButtonWidthDip * kBubbleHeadroom;
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    return std::clamp(static_cast<int>(std::lround(diameterDip * dpiX / 96.0)), 1,
                      static_cast<int>(kMaxBubbleDiameter));
}

// The touch keyboard's highlighted-key color (its accent Enter) as 0x00RRGGBB,
// available before the keyboard ever opens: the system accent palette shade
// the keyboard uses, AccentLight2 in dark app mode and AccentDark1 in light.
static DWORD BubbleAccentColor() {
    DWORD lightApps = 0, lightSize = sizeof(lightApps);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &lightApps, &lightSize);
    try {
        using winrt::Windows::UI::ViewManagement::UIColorType;
        const auto color = winrt::Windows::UI::ViewManagement::UISettings().GetColorValue(
            lightApps ? UIColorType::AccentDark1 : UIColorType::AccentLight2);
        return (DWORD{color.R} << 16) | (DWORD{color.G} << 8) | color.B;
    } catch (const winrt::hresult_error&) {
        // Fall back to the base accent below.
    }
    DWORD abgr = 0, size = sizeof(abgr);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor",
                     RRF_RT_REG_DWORD, nullptr, &abgr, &size) == ERROR_SUCCESS)
        return ((abgr & 0xFF) << 16) | (abgr & 0xFF00) | ((abgr >> 16) & 0xFF);
    DWORD argb = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&argb, &opaque))) return argb & 0xFFFFFF;
    return 0x0078D4;  // default Windows blue
}

// The surface is allocated once per diameter; painting reuses it, so the
// press and pop-in scale animations can repaint every frame cheaply.
static DWORD g_bubbleAccent;
static void* g_bubbleBits;
static float g_paintedScale = -1;

static bool EnsureBubbleSurface(int d) {
    if (g_b.memDC && g_b.d == d && g_bubbleBits) return true;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = d;
    bi.bmiHeader.biHeight = -d;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    const HBITMAP bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    const auto old = bitmap && bits ? SelectObject(dc, bitmap) : nullptr;
    if (!old || old == HGDI_ERROR) {
        DeleteDC(dc);
        if (bitmap) DeleteObject(bitmap);
        return false;
    }
    if (g_b.memDC) {
        SelectObject(g_b.memDC, g_b.oldBmp);
        DeleteDC(g_b.memDC);
        DeleteObject(g_b.bmp);
    }
    g_b.memDC = dc;
    g_b.bmp = bitmap;
    g_b.oldBmp = static_cast<HBITMAP>(old);
    g_b.d = d;
    g_bubbleBits = bits;
    g_paintedScale = -1;
    return true;
}

// Premultiplied ARGB circle with a keyboard glyph, drawn at `scale` about the
// centre. Signed distances are evaluated in unscaled space and multiplied by
// the scale, so edges stay one pixel soft at every size.
static void PaintBubble(float animatedScale) {
    if (!g_bubbleBits || std::fabs(animatedScale - g_paintedScale) < 0.002f) return;
    g_paintedScale = animatedScale;
    // The window has kBubbleHeadroom extra room so spring overshoot is not
    // clipped; at rest the bubble fills 1 / kBubbleHeadroom of it.
    const float scale = std::min(animatedScale, kBubbleHeadroom) / kBubbleHeadroom;
    const int d = g_b.d;
    BYTE* p = static_cast<BYTE*>(g_bubbleBits);
    if (scale < 0.02f) {
        memset(p, 0, static_cast<size_t>(d) * d * 4);
        return;
    }
    const DWORD accent = g_bubbleAccent;
    // BGR channel order to match the DIB.
    const std::array<float, 3> faceColor{static_cast<float>(accent & 0xFF),
        static_cast<float>((accent >> 8) & 0xFF), static_cast<float>((accent >> 16) & 0xFF)};
    // Black or white icon, whichever contrasts with the accent (Rec. 601 luma).
    const float luma = 0.299f * faceColor[2] + 0.587f * faceColor[1] + 0.114f * faceColor[0];
    const float glyphValue = luma >= 150 ? 0.0f : 255.0f;

    const float c = d / 2.0f, r = d / 2.0f - 0.5f;
    // Keyboard glyph: rounded outline, two rows of keys and a space bar.
    auto roundBox = [](float px, float py, float cx, float cy, float hw, float hh, float rad) {
        float qx = fabsf(px - cx) - (hw - rad), qy = fabsf(py - cy) - (hh - rad);
        float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
        return sqrtf(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - rad;
    };
    const float bw = d * 0.31f, bh = d * 0.20f;        // body half extents
    const float stroke = std::max(1.2f, d * 0.035f);   // outline thickness
    const float kh = d * 0.028f;                       // key half size
    const float colStep = (2 * bw - 2 * stroke) / 6.0f, rowStep = bh * 0.62f;
    const auto soft = [scale](float distance) { return std::clamp(0.5f - distance * scale, 0.0f, 1.0f); };
    auto glyph = [&](float px, float py) {
        const float body = roundBox(px, py, c, c, bw, bh, d * 0.05f);
        float cov = soft(fabsf(body + stroke / 2) - stroke / 2);
        for (int rr = 0; rr < 2; rr++)
            for (int k = 0; k < 5; k++) {
                const float kx = c - 2 * colStep + k * colStep, ky = c - rowStep + rr * rowStep * 0.95f;
                cov = std::max(cov, soft(roundBox(px, py, kx, ky, kh, kh, kh * 0.4f)));
            }
        const float sy = c + rowStep * 0.95f;
        return std::max(cov, soft(roundBox(px, py, c, sy, colStep * 1.6f, kh, kh * 0.6f)));
    };
    for (int yy = 0; yy < d; yy++)
        for (int xx = 0; xx < d; xx++, p += 4) {
            // Map the screen pixel back into unscaled bubble space.
            const float px = c + (xx + 0.5f - c) / scale, py = c + (yy + 0.5f - c) / scale;
            const float dist = sqrtf((px - c) * (px - c) + (py - c) * (py - c));
            const float cov = soft(dist - r);
            if (cov <= 0) {
                p[0] = p[1] = p[2] = p[3] = 0;
                continue;
            }
            const float g = glyph(px, py);
            for (int channel = 0; channel < 3; ++channel)  // premultiplied
                p[channel] = static_cast<BYTE>((faceColor[channel] * (1 - g) + glyphValue * g) * cov + 0.5f);
            p[3] = static_cast<BYTE>(cov * 255 + 0.5f);
        }
}

// Refreshes the accent (once per show) and paints at the current scale.
static bool RenderBubbleBitmap(int d) {
    TRACE_SCOPE();
    g_bubbleAccent = BubbleAccentColor();
    if (!EnsureBubbleSurface(d)) return false;
    g_paintedScale = -1;
    PaintBubble(g_b.scale);
    return true;
}

static bool BubbleApply(HWND h) {
    TRACE_SCOPE();
    POINT pos = {(LONG)lroundf(g_b.x), (LONG)lroundf(g_b.y)};
    SIZE size = {g_b.d, g_b.d};
    POINT src = {0, 0};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, (BYTE)g_b.alpha, AC_SRC_ALPHA};
    if (g_b.memDC && UpdateLayeredWindow(h, nullptr, &pos, &size, g_b.memDC, &src, 0, &bf, ULW_ALPHA))
        return true;
    Wh_Log(L"Bubble update failed (%lu)", GetLastError());
    return false;
}

static RECT WorkAreaAt(POINT pt) {
    TRACE_SCOPE();
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfo(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi)) return {};
    return mi.rcWork;
}

// Dock target: nearest left/right edge (after projecting the fling a little),
// vertically kept inside the work area.
static void BubbleArmIdle(HWND h);

static void BubbleDisarmIdle(HWND h) {
    TRACE_SCOPE();
    g_b.idleDeadline = 0;
    KillTimer(h, kIdleTimer);
}

static void BubbleDock(HWND h, float vx, float vy) {
    TRACE_SCOPE();
    float cx = g_b.x + g_b.d / 2.0f, cy = g_b.y + g_b.d / 2.0f;
    RECT wa = WorkAreaAt({(LONG)cx, (LONG)cy});
    if (wa.right <= wa.left || wa.bottom <= wa.top) {
        TRACE_BLOCK();
        g_b.animating = false;
        BubbleArmIdle(h);
        return;
    }
    // Margins are for the visible circle; the window adds transparent headroom.
    const float visible = g_b.d / kBubbleHeadroom, pad = (g_b.d - visible) / 2;
    float margin = std::max(4.0f, visible * 0.12f) - pad;
    // A deliberate horizontal flick commits to that side, as chat heads do;
    // a gentle release goes to the nearer edge after a short projection.
    // Vertical momentum carries on with friction (about a quarter second of
    // travel) before the spring settles it inside the work area.
    const float flingThreshold = visible * 6;  // px/s
    const bool flung = fabsf(vx) > flingThreshold;
    bool left = flung ? vx < 0 : cx + vx * 0.1f < (wa.left + wa.right) / 2.0f;
    float projY = g_b.y + vy * 0.25f;
    const float right = std::max((float)wa.left, (float)wa.right - g_b.d);
    const float horizontalMargin = std::min(margin, (right - wa.left) / 2);
    g_b.tx = left ? wa.left + horizontalMargin : right - horizontalMargin;
    const float bottom = std::max((float)wa.top, (float)wa.bottom - g_b.d);
    const float verticalMargin = std::min(margin, (bottom - wa.top) / 2);
    g_b.ty = std::clamp(projY, wa.top + verticalMargin, bottom - verticalMargin);
    g_b.vx = vx;
    g_b.vy = vy;
    g_b.animating = true;
    QueryPerformanceCounter(&g_b.lastTick);
    BubbleDisarmIdle(h);
}

// Scale springs, tuned after Messenger's chat heads: a quick, nearly
// critically damped press, a bouncy release, and a softer bouncy pop-in.
constexpr float kPressedScale = 0.88f;
static void BubbleScaleTo(float target, float stiffness, float dampingRatio) {
    g_b.scaleTarget = target;
    g_b.scaleK = stiffness;
    g_b.scaleZeta = dampingRatio;
}
static void BubblePressScale() { BubbleScaleTo(kPressedScale, 1400, 0.85f); }
static void BubbleReleaseScale() { BubbleScaleTo(1, 700, 0.38f); }
static void BubblePopIn() {
    g_b.scale = 0;
    g_b.scaleV = 0;
    BubbleScaleTo(1, 380, 0.42f);
}

static bool BubbleScaleAnimating() {
    return fabsf(g_b.scale - g_b.scaleTarget) > 0.001f || fabsf(g_b.scaleV) > 0.01f;
}

// The worker schedules frames while the bubble is moving, scaling or fading.
static bool BubbleNeedsFrames() {
    TRACE_SCOPE();
    return g_b.animating || g_b.dragged || g_b.alpha != g_b.alphaTarget || BubbleScaleAnimating() ||
           g_b.leaving;
}

// After a tap opens the keyboard the bubble shrinks away in place, then hides.
// Timing and curve match Android's FastOutSlowInInterpolator over 250 ms.
constexpr double kBubbleLeaveSeconds = 0.25;
// A tap-opened keyboard can report visible only after the exit ends; keep the
// bubble down until then instead of popping it back in. If the keyboard never
// shows, the bubble returns after this long.
constexpr ULONGLONG kBubbleRelaunchGraceMs = 1500;
static ULONGLONG g_bubbleHeldDownUntil;

// cubic-bezier(0.4, 0, 0.2, 1): solve x(s) = t by Newton's method, return y(s).
static float FastOutSlowIn(float t) {
    constexpr float x1 = 0.4f, y1 = 0.0f, x2 = 0.2f, y2 = 1.0f;
    const auto bezier = [](float s, float p1, float p2) {
        const float u = 1 - s;
        return 3 * u * u * s * p1 + 3 * u * s * s * p2 + s * s * s;
    };
    const auto slope = [](float s, float p1, float p2) {
        const float u = 1 - s;
        return 3 * u * u * p1 + 6 * u * s * (p2 - p1) + 3 * s * s * (1 - p2);
    };
    float s = t;
    for (int i = 0; i < 8; ++i) {
        const float dx = slope(s, x1, x2);
        if (fabsf(dx) < 1e-5f) break;
        s = std::clamp(s - (bezier(s, x1, x2) - t) / dx, 0.0f, 1.0f);
    }
    return bezier(s, y1, y2);
}

static double NowSeconds();
static void BubbleHide(HWND h);
static void BubbleLeave(HWND h) {
    g_b.leaving = true;
    g_b.animating = false;
    g_b.leaveStart = NowSeconds();
    g_b.leaveFromScale = std::max(g_b.scale, 0.0f);
    g_b.alpha = g_b.alphaTarget = 255;  // fully visible, as on the way in
    g_b.idleDeadline = 0;
    KillTimer(h, kIdleTimer);
}

static void BubbleTouch(HWND h) {
    TRACE_SCOPE();
    g_b.alphaTarget = 255;
    if (g_b.alpha != 255) {
        TRACE_BLOCK();
        g_b.alpha = 255;
        BubbleApply(h);
    }
    BubbleDisarmIdle(h);
}

static void BubbleArmIdle(HWND h) {
    TRACE_SCOPE();
    const auto settings = ReadSettings();
    g_b.idleDeadline = GetTickCount64() + settings.bubbleIdleDelay;
    if (!SetTimer(h, kIdleTimer, settings.bubbleIdleDelay, nullptr)) {
        TRACE_BLOCK();
        g_b.idleDeadline = 0;
        g_b.alphaTarget = settings.bubbleIdleAlpha;
        QueryPerformanceCounter(&g_b.lastTick);
    }
}

static void BubbleAnimStep(HWND h) {
    TRACE_SCOPE();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = (float)std::min(Seconds(g_b.lastTick, now), 0.05);
    g_b.lastTick = now;

    if (g_b.leaving) {
        // Shrink away in place on FastOutSlowIn, staying fully opaque.
        const float t = static_cast<float>(std::clamp((NowSeconds() - g_b.leaveStart) / kBubbleLeaveSeconds, 0.0, 1.0));
        g_b.scale = g_b.scaleTarget = g_b.leaveFromScale * (1 - FastOutSlowIn(t));
        g_b.scaleV = 0;
        PaintBubble(g_b.scale);
        if (t >= 1) {
            BubbleHide(h);
            return;
        }
    }
    if (g_b.dragged) {  // follow the finger, coalesced to one move per frame
        g_b.x = g_b.lastPt.x - g_b.grabX;
        g_b.y = g_b.lastPt.y - g_b.grabY;
    }
    if (g_b.animating) {
        TRACE_BLOCK();
        // Under-damped spring: settles in ~0.35 s with a small overshoot.
        const float k = 260.0f, damping = 2.0f * sqrtf(k) * 0.62f;
        for (int i = 0; i < 4; i++) {  // substeps for stability
            float h4 = dt / 4;
            g_b.vx += (k * (g_b.tx - g_b.x) - damping * g_b.vx) * h4;
            g_b.vy += (k * (g_b.ty - g_b.y) - damping * g_b.vy) * h4;
            g_b.x += g_b.vx * h4;
            g_b.y += g_b.vy * h4;
        }
        if (fabsf(g_b.tx - g_b.x) < 0.5f && fabsf(g_b.ty - g_b.y) < 0.5f && fabsf(g_b.vx) < 8 &&
            fabsf(g_b.vy) < 8) {
            TRACE_BLOCK();
            g_b.x = g_b.tx;
            g_b.y = g_b.ty;
            g_b.animating = false;
            BubbleArmIdle(h);
        }
    }
    if (BubbleScaleAnimating()) {
        const float damping = 2.0f * sqrtf(g_b.scaleK) * g_b.scaleZeta;
        for (int i = 0; i < 4; i++) {
            const float h4 = dt / 4;
            g_b.scaleV += (g_b.scaleK * (g_b.scaleTarget - g_b.scale) - damping * g_b.scaleV) * h4;
            g_b.scale += g_b.scaleV * h4;
        }
        g_b.scale = std::max(g_b.scale, 0.0f);
        if (fabsf(g_b.scale - g_b.scaleTarget) < 0.002f && fabsf(g_b.scaleV) < 0.05f) {
            g_b.scale = g_b.scaleTarget;
            g_b.scaleV = 0;
        }
        PaintBubble(g_b.scale);
    }
    if (g_b.alpha != g_b.alphaTarget) {
        TRACE_BLOCK();
        int step = std::max(1, (int)(dt * 900));  // ~250 ms full fade
        g_b.alpha = g_b.alpha > g_b.alphaTarget ? std::max(g_b.alphaTarget, g_b.alpha - step)
                                                : std::min(g_b.alphaTarget, g_b.alpha + step);
    }
    if (!BubbleApply(h)) {
        TRACE_BLOCK();
        g_b.animating = g_b.down = g_b.dragged = false;
        g_b.pointerId = 0;
        g_b.alphaTarget = g_b.alpha;
    }
}

// --- input: touch/pen via pointer messages, mouse via mouse messages ---
// Touch drags never arrive as mouse moves (Windows treats them as a pan), so
// pointer messages are used; the keyboard opens only on a tap, i.e. a release
// that never moved past the slop.
static double NowSeconds() {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return double(now.QuadPart) / double(frequency.QuadPart);
}

static void BubbleAddSample(POINT p) {
    g_b.samples[g_b.sampleNext] = {NowSeconds(), static_cast<float>(p.x), static_cast<float>(p.y)};
    g_b.sampleNext = (g_b.sampleNext + 1) % g_b.samples.size();
    g_b.sampleCount = std::min(g_b.sampleCount + 1, g_b.samples.size());
}

// Least-squares slope of position over the last 100 ms of samples. A finger
// that stopped moving more than 80 ms before release has no velocity.
static void BubbleReleaseVelocity(float& vx, float& vy) {
    vx = vy = 0;
    if (g_b.sampleCount < 2) return;
    const size_t size = g_b.samples.size();
    const auto& latest = g_b.samples[(g_b.sampleNext + size - 1) % size];
    if (NowSeconds() - latest.t > 0.08) return;
    double n = 0, st = 0, sx = 0, sy = 0, stt = 0, stx = 0, sty = 0;
    for (size_t i = 0; i < g_b.sampleCount; ++i) {
        const auto& s = g_b.samples[(g_b.sampleNext + size - 1 - i) % size];
        const double t = s.t - latest.t;  // <= 0
        if (t < -0.1) break;
        n += 1; st += t; sx += s.x; sy += s.y; stt += t * t; stx += t * s.x; sty += t * s.y;
    }
    const double denominator = n * stt - st * st;
    if (n < 2 || denominator <= 1e-9) return;
    vx = static_cast<float>((n * stx - st * sx) / denominator);
    vy = static_cast<float>((n * sty - st * sy) / denominator);
}

static void BubbleInputDown(HWND h, POINT p) {
    TRACE_SCOPE();
    BubbleTouch(h);
    BubblePressScale();
    g_b.sampleCount = g_b.sampleNext = 0;
    BubbleAddSample(p);
    g_b.down = true;
    g_b.dragged = false;
    g_b.animating = false;
    g_b.downPt = p;
    g_b.grabX = p.x - g_b.x;
    g_b.grabY = p.y - g_b.y;
    g_b.lastPt = p;
    QueryPerformanceCounter(&g_b.lastTick);
}

static void BubbleInputMove(POINT p) {
    TRACE_SCOPE();
    if (!g_b.down) return;
    BubbleAddSample(p);
    int slop = std::max(6, g_b.d / 8);
    if (!g_b.dragged && abs(p.x - g_b.downPt.x) <= slop && abs(p.y - g_b.downPt.y) <= slop) return;
    g_b.dragged = true;
    g_b.lastPt = p;
}

static void BubbleInputUp(HWND h, POINT p) {
    TRACE_SCOPE();
    if (!g_b.down) return;
    BubbleInputMove(p);
    bool wasDrag = g_b.dragged;
    g_b.down = false;
    g_b.dragged = false;
    g_b.pointerId = 0;
    BubbleReleaseScale();
    if (!wasDrag) {  // tap
        if (ToggleKeyboard()) {
            g_bubbleHeldDownUntil = GetTickCount64() + kBubbleRelaunchGraceMs;
            BubbleLeave(h);  // the bubble hides itself when the exit finishes
        } else {
            Wh_Log(L"Could not open the touch keyboard from the bubble");
            BubbleArmIdle(h);
        }
        return;
    }
    g_b.x = p.x - g_b.grabX;
    g_b.y = p.y - g_b.grabY;
    float vx, vy;
    BubbleReleaseVelocity(vx, vy);
    const float maxV = 8000;
    BubbleDock(h, std::clamp(vx, -maxV, maxV), std::clamp(vy, -maxV, maxV));
}

static void BubbleInputCancel(HWND h) {
    TRACE_SCOPE();
    if (!g_b.down) return;
    g_b.down = false;
    g_b.dragged = false;
    g_b.pointerId = 0;
    BubbleReleaseScale();
    BubbleDock(h, 0, 0);
}

constexpr LONG64 kPanelVisibilityLeaseMs = 1750;

static bool CompactModeActive() {
    return g_shared && InterlockedCompareExchange64(&g_shared->compactModeDeadline, 0, 0) >
                       static_cast<LONG64>(GetTickCount64());
}

static void PublishCompactMode(bool compact) {
    if (!g_shared || g_unloading) return;
    const auto deadline = static_cast<LONG64>(GetTickCount64() + kPanelVisibilityLeaseMs);
    InterlockedExchange64(&g_shared->compactModeDeadline, compact ? deadline : 0);
}

static void PublishPanelVisibility(bool visible) {
    if (!g_shared || g_unloading) return;
    const auto now = static_cast<LONG64>(GetTickCount64());
    const auto deadline = now + kPanelVisibilityLeaseMs;
    const auto previous = InterlockedExchange64(&g_shared->panelVisibilityDeadline, visible ? deadline : -deadline);
    if (g_bubbleEvt && (visible ? previous <= now : previous >= -now)) SetEvent(g_bubbleEvt);
}

static bool TouchKeyboardVisible() {
    if (g_shared) {
        const auto deadline = InterlockedCompareExchange64(&g_shared->panelVisibilityDeadline, 0, 0);
        const auto now = static_cast<LONG64>(GetTickCount64());
        if (deadline > now) return true;
        if (deadline < -now) return false;
    }
    bool visible = false;
    EnumWindows([](HWND window, LPARAM context) -> BOOL {
        if (!IsWindowVisible(window) || IsIconic(window)) return TRUE;
        WCHAR windowClass[64]{};
        if (!GetClassName(window, windowClass, ARRAYSIZE(windowClass))) return TRUE;
        const bool modern = wcscmp(windowClass, L"Windows.UI.Core.CoreWindow") == 0;
        if (!modern && wcscmp(windowClass, L"IPTip_Main_Window") != 0) return TRUE;

        RECT bounds;
        DWORD cloaked = 0;
        if (!GetWindowRect(window, &bounds) || IsRectEmpty(&bounds) ||
            !MonitorFromRect(&bounds, MONITOR_DEFAULTTONULL) ||
            (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked))
            return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (!pid) return TRUE;
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return TRUE;
        WCHAR path[MAX_PATH];
        DWORD size = ARRAYSIZE(path);
        const BOOL found = QueryFullProcessImageName(process, 0, path, &size);
        CloseHandle(process);
        if (!found) return TRUE;
        const auto slash = wcsrchr(path, L'\\');
        if (_wcsicmp(slash ? slash + 1 : path, modern ? L"TextInputHost.exe" : L"TabTip.exe") != 0)
            return TRUE;
        *reinterpret_cast<bool*>(context) = true;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&visible));
    return visible;
}

static void BubbleShow(HWND h, const POINT* closePoint, bool positioned) {
    TRACE_SCOPE();
    POINT pt{};
    if (closePoint) pt = *closePoint;
    else if (positioned) pt = {static_cast<LONG>(g_b.x + g_b.d / 2.0f), static_cast<LONG>(g_b.y + g_b.d / 2.0f)};
    else GetCursorPos(&pt);
    RECT wa = WorkAreaAt(pt);
    const auto limit = std::min({kMaxBubbleDiameter, wa.right - wa.left, wa.bottom - wa.top});
    if (limit <= 0) return;
    const int d = std::min(BubbleDiameterAt(pt), static_cast<int>(limit));
    g_b.leaving = false;  // a new appearance supersedes an exit in flight
    BubblePopIn();  // grow from nothing on a bouncy spring, like a new chat head
    if (!RenderBubbleBitmap(d)) {
        TRACE_BLOCK();
        Wh_Log(L"Could not allocate bubble bitmap (%d px)", d);
        return;
    }
    if (!closePoint && !positioned) pt = {wa.right - d / 2, wa.bottom - d - d / 2};
    g_b.x = std::clamp((float)pt.x - d / 2.0f, (float)wa.left, (float)wa.right - d);
    g_b.y = std::clamp((float)pt.y - d / 2.0f, (float)wa.top, (float)wa.bottom - d);
    g_b.alpha = g_b.alphaTarget = 255;
    if (!BubbleApply(h)) return;
    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    VLOG(L"bubble at %d,%d d=%d", pt.x, pt.y, d);
    BubbleDock(h, 0, 0);
}

static void BubbleHide(HWND h) {
    if (GetCapture() == h) ReleaseCapture();
    g_b.leaving = false;
    g_b.down = g_b.dragged = false;
    g_b.pointerId = 0;
    BubbleDisarmIdle(h);
    g_b.animating = false;
    g_b.alphaTarget = g_b.alpha;
    ShowWindow(h, SW_HIDE);
}

LRESULT CALLBACK WorkerProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    TRACE_SCOPE();
    switch (msg) {
        case WM_DISPLAYCHANGE:
            g_frameMonitor = nullptr;
            break;
        case WM_TIMER:
            if (wp == kIdleTimer && g_b.idleDeadline && GetTickCount64() >= g_b.idleDeadline) {
                TRACE_BLOCK();
                BubbleDisarmIdle(h);
                if (!g_b.down) {
                    TRACE_BLOCK();
                    g_b.alphaTarget = ReadSettings().bubbleIdleAlpha;
                    QueryPerformanceCounter(&g_b.lastTick);
                }
            }
            return 0;

        // touch / pen (handled, so Windows does not synthesize mouse input)
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP: {
            TRACE_BLOCK();
            const UINT32 id = GET_POINTERID_WPARAM(wp);
            POINTER_INPUT_TYPE type;
            if (!GetPointerType(id, &type)) {
                TRACE_BLOCK();
                if (g_b.pointerId && g_b.pointerId == id) {
                    TRACE_BLOCK();
                    BubbleInputCancel(h);
                    return 0;
                }
                break;
            }
            if (type != PT_TOUCH && type != PT_PEN) break;
            if (msg == WM_POINTERDOWN && (g_b.down || g_b.leaving)) return 0;
            if (msg != WM_POINTERDOWN && (!g_b.down || g_b.pointerId != id)) return 0;
            POINTER_INFO info;
            if (GetPointerInfo(id, &info) && (info.pointerFlags & POINTER_FLAG_CANCELED)) {
                TRACE_BLOCK();
                BubbleInputCancel(h);
                return 0;
            }
            POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (msg == WM_POINTERDOWN) {
                TRACE_BLOCK();
                BubbleInputDown(h, p);
                g_b.pointerId = id;
            }
            else if (msg == WM_POINTERUPDATE) BubbleInputMove(p);
            else BubbleInputUp(h, p);
            return 0;
        }
        case WM_POINTERCAPTURECHANGED:
            if (g_b.pointerId && g_b.pointerId == GET_POINTERID_WPARAM(wp)) BubbleInputCancel(h);
            return 0;
        case 0x02CC:  // WM_TABLET_QUERYSYSTEMGESTURESTATUS
            // no press-and-hold, flicks or tap feedback on the bubble
            return 0x00000001 | 0x00000008 | 0x00000010 | 0x00010000;

        // mouse
        case WM_LBUTTONDOWN: {
            TRACE_BLOCK();
            if (g_b.down || g_b.leaving) return 0;
            POINT p;
            if (!GetCursorPos(&p)) return 0;
            BubbleInputDown(h, p);
            SetCapture(h);
            return 0;
        }
        case WM_MOUSEMOVE: {
            TRACE_BLOCK();
            if (g_b.pointerId) return 0;
            if (!g_b.down) {  // hover: wake up, fade again later
                BubbleTouch(h);
                if (!g_b.animating) BubbleArmIdle(h);
                return 0;
            }
            POINT p;
            if (GetCursorPos(&p)) BubbleInputMove(p);
            return 0;
        }
        case WM_LBUTTONUP: {
            TRACE_BLOCK();
            if (g_b.pointerId) return 0;
            POINT p;
            // Handle the release before ReleaseCapture: that sends
            // WM_CAPTURECHANGED, which would otherwise cancel the tap.
            if (GetCursorPos(&p)) BubbleInputUp(h, p);
            else BubbleInputCancel(h);
            if (GetCapture() == h) ReleaseCapture();
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (!g_b.pointerId && (HWND)lp != h) BubbleInputCancel(h);
            return 0;
        case WM_CANCELMODE:
            BubbleInputCancel(h);
            if (GetCapture() == h) ReleaseCapture();
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

DWORD WINAPI WorkerThread(LPVOID) {
    TRACE_SCOPE();
    // An early-loaded Explorer may create the taskbar after the mod initializes.
    HWND taskbar;
    while (!(taskbar = FindWindow(L"Shell_TrayWnd", nullptr))) {
        if (WaitForSingleObject(g_threadStop, kKeyboardVisibilityPollMs) != WAIT_TIMEOUT) return 0;
    }
    DWORD taskbarPid = 0;
    GetWindowThreadProcessId(taskbar, &taskbarPid);
    if (taskbarPid != GetCurrentProcessId()) return 0;

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASS wc = {};
    wc.lpfnWndProc = WorkerProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_HAND);
    wc.lpszClassName = L"WhTouchKeyboardBubble";
    const ATOM registered = RegisterClass(&wc);

    HWND hwnd = nullptr;
    if (registered && SUCCEEDED(com))
        hwnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                              wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1,
                              nullptr, nullptr, wc.hInstance, nullptr);

    if (hwnd) {
        TRACE_BLOCK();
        // No press-and-hold / flicks / pen feedback on the bubble.
        SetProp(hwnd, L"MicrosoftTabletPenServiceProperty", (HANDLE)(ULONG_PTR)(0x00000001 | 0x00000008 | 0x00000010 | 0x00010000));
        // The bubble animates itself; no system show/hide transitions on top.
        BOOL noTransitions = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &noTransitions, sizeof(noTransitions));
        BOOL off = FALSE;
        for (auto fb : {FEEDBACK_TOUCH_CONTACTVISUALIZATION, FEEDBACK_TOUCH_TAP, FEEDBACK_TOUCH_PRESSANDHOLD,
                        FEEDBACK_GESTURE_PRESSANDTAP})
            SetWindowFeedbackSetting(hwnd, fb, 0, sizeof(off), &off);
    }

    // Use QPC deadlines and a waitable timer so input and shutdown can wake
    // the worker between frames. A coarse timeout covers timer failures.
    HANDLE frameTimer = CreateWaitableTimerEx(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                              TIMER_MODIFY_STATE | SYNCHRONIZE);
    HANDLE waits[3] = {g_threadStop, g_bubbleEvt};
    const DWORD nWaits = waits[1] ? 2 : 1;
    waits[nWaits] = frameTimer;
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    LONGLONG nextFrame = 0;
    ULONGLONG nextVisibilityCheck = 0;
    ULONGLONG closeOverrideUntil = 0;
    bool positioned = false;
    bool traySuppressed = false;
    ULONGLONG nextTrayCheck = 0;
    for (bool run = hwnd != nullptr; run;) {
        TRACE_BLOCK();
        const auto tick = GetTickCount64();
        if (tick >= nextVisibilityCheck) {
            nextVisibilityCheck = tick + kKeyboardVisibilityPollMs;
            const bool bubbleEnabled = ReadSettings().bubbleOnClose;
            // The bubble replaces the tray icon for as long as the bubble
            // feature is on (it appears whenever the keyboard is hidden).
            const bool suppressTray = bubbleEnabled && ReadSettings().hideTrayIcon;
            if (suppressTray != traySuppressed || tick >= nextTrayCheck) {
                SetTrayIconSuppressed(suppressTray);
                traySuppressed = suppressTray;
                nextTrayCheck = tick + 1000;
            }
            const bool show = bubbleEnabled &&
                              (tick < closeOverrideUntil || !TouchKeyboardVisible());
            if (!show) g_bubbleHeldDownUntil = 0;  // the keyboard is up; the hold did its job
            if (show && !IsWindowVisible(hwnd) && tick >= g_bubbleHeldDownUntil) {
                BubbleShow(hwnd, nullptr, positioned);
                positioned = IsWindowVisible(hwnd) || positioned;
            } else if (!show && IsWindowVisible(hwnd) && !g_b.leaving) {
                // The keyboard opened some other way: same shrink-away exit,
                // unless the bubble is being held.
                if (g_b.down) BubbleHide(hwnd);
                else BubbleLeave(hwnd);
            }
        }
        const bool frames = BubbleNeedsFrames();
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        DWORD count = nWaits;
        DWORD delay = INFINITE;
        if (!frames) {
            nextFrame = 0;
        } else if (nextFrame <= now.QuadPart) {
            delay = 0;
        } else {
            const auto remaining = nextFrame - now.QuadPart;
            LARGE_INTEGER due;
            due.QuadPart = -std::max<LONGLONG>(1, remaining * 10000000 / frequency.QuadPart);
            if (frameTimer && SetWaitableTimer(frameTimer, &due, 0, nullptr, nullptr, FALSE)) ++count;
            else delay = static_cast<DWORD>((remaining * 1000 + frequency.QuadPart - 1) / frequency.QuadPart);
        }
        const auto waitTick = GetTickCount64();
        const DWORD visibilityDelay = nextVisibilityCheck > waitTick
                                       ? static_cast<DWORD>(nextVisibilityCheck - waitTick) : 0;
        DWORD w = MsgWaitForMultipleObjectsEx(count, waits, std::min(delay, visibilityDelay),
                                             QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (w == WAIT_OBJECT_0) break;
        if (w == WAIT_FAILED) {
            TRACE_BLOCK();
            Wh_Log(L"Keyboard worker wait failed (%lu)", GetLastError());
            break;
        }
        if (nWaits == 2 && w == WAIT_OBJECT_0 + 1) {
            TRACE_BLOCK();
            const LONG hint = g_shared ? InterlockedExchange(&g_shared->bubbleVisibilityHint, 0) : 0;
            if (hint == kBubbleCloseHint && ReadSettings().bubbleOnClose) {
                g_bubbleHeldDownUntil = 0;  // closing with X always brings the bubble back
                // The keyboard can still report visible while its close animation runs.
                closeOverrideUntil = GetTickCount64() + kPanelVisibilityLeaseMs;
                const auto packed = static_cast<UINT64>(InterlockedCompareExchange64(&g_shared->bubbleClosePoint, 0, 0));
                const bool hasClosePoint = packed != static_cast<UINT64>(kNoBubbleClosePoint);
                if (hasClosePoint || !IsWindowVisible(hwnd)) {
                    const POINT closePoint{static_cast<LONG>(packed), static_cast<LONG>(packed >> 32)};
                    BubbleShow(hwnd, hasClosePoint ? &closePoint : nullptr, positioned);
                    positioned = IsWindowVisible(hwnd) || positioned;
                }
            } else if (hint == kBubbleShowHint) {
                closeOverrideUntil = 0;
            }
            nextVisibilityCheck = 0;
            continue;
        }
        MSG m;
        for (int count = 0; count < 32 && PeekMessage(&m, nullptr, 0, 0, PM_REMOVE); ++count) {
            TRACE_BLOCK();
            if (m.message == WM_QUIT) {
                TRACE_BLOCK();
                run = false;
                break;
            }
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
        QueryPerformanceCounter(&now);
        if (run && BubbleNeedsFrames() && now.QuadPart >= nextFrame) {
            TRACE_BLOCK();
            BubbleAnimStep(hwnd);
            const auto period = std::max<LONGLONG>(1, frequency.QuadPart / BubbleMonitorRefreshRate(hwnd));
            const auto last = nextFrame ? nextFrame : now.QuadPart;
            nextFrame = last + ((now.QuadPart - last) / period + 1) * period;
        }
    }
    if (frameTimer) CloseHandle(frameTimer);
    if (hwnd) DestroyWindow(hwnd);
    if (g_b.memDC) {
        TRACE_BLOCK();
        SelectObject(g_b.memDC, g_b.oldBmp);
        DeleteDC(g_b.memDC);
        DeleteObject(g_b.bmp);
        g_b.memDC = nullptr;
        g_bubbleBits = nullptr;
        g_paintedScale = -1;
    }
    if (registered) UnregisterClass(wc.lpszClassName, wc.hInstance);
    if (SUCCEEDED(com)) CoUninitialize();
    {
        TRACE_BLOCK();
        std::lock_guard lock(g_workerExitMutex);
        if (g_workerOwnsSharedCleanup) {
            TRACE_BLOCK();
            CloseShared();
            CloseHandle(g_threadStop);
        }
        g_workerExited = true;
    }
    return 0;
}

// An explicit close can show the bubble before the visibility report changes.
void NotifyKeyboardVisibilityChanged(bool fromClose, const POINT* closePoint = nullptr) {
    TRACE_SCOPE();
    if (!g_shared || !g_bubbleEvt) return;
    if (fromClose) {
        const auto packed = closePoint ? static_cast<LONG64>(
            (static_cast<UINT64>(static_cast<DWORD>(closePoint->y)) << 32) |
            static_cast<DWORD>(closePoint->x)) : kNoBubbleClosePoint;
        InterlockedExchange64(&g_shared->bubbleClosePoint, packed);
    }
    InterlockedExchange(&g_shared->bubbleVisibilityHint, fromClose ? kBubbleCloseHint : kBubbleShowHint);
    SetEvent(g_bubbleEvt);
}

// ---------------------------------------------------------------------------
// InputApp.dll (TextInputHost): X button
// ---------------------------------------------------------------------------
// The native close handler can queue deactivation after the click returns.
thread_local ULONGLONG t_keyboardCloseUntil;
thread_local unsigned t_keyboardCloseSession;

namespace CompactCtrlRow {
bool IsTouchKeyboardVisibleOnCurrentThread();
bool GetCloseButtonCenter(POINT& point);
winrt::Windows::Foundation::IInspectable CurrentKeyboardControl();
}

static bool KeyboardCloseRequested() {
    return t_keyboardCloseUntil > GetTickCount64() && t_keyboardCloseSession == g_inputSession.load();
}

// X button: notify Explorer that the keyboard is closing.
void UserClose() {
    TRACE_SCOPE();
    if (KeyboardCloseRequested()) return;
    t_keyboardCloseSession = ++g_inputSession;
    t_keyboardCloseUntil = GetTickCount64() + 1000;
    POINT point{};
    NotifyKeyboardVisibilityChanged(true, CompactCtrlRow::GetCloseButtonCenter(point) ? &point : nullptr);
}

using VoidThis_t = void(__cdecl*)(void* pThis);
VoidThis_t PageOnCloseButtonPressed_orig;
void __cdecl PageOnCloseButtonPressed_hook(void* pThis) {
    TRACE_SCOPE();
    if (g_inputAppReady && !g_unloading) UserClose();
    TRACE_POINT(L"calling native close handler");
    PageOnCloseButtonPressed_orig(pThis);
    TRACE_POINT(L"native close handler returned");
}

static bool ImageContains(HMODULE image, const void* address, size_t size, bool executable = false) {
    TRACE_SCOPE();
    MEMORY_BASIC_INFORMATION region{};
    if (!image || !address || !VirtualQuery(address, &region, sizeof(region)) ||
        region.AllocationBase != image || region.Type != MEM_IMAGE || region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    if (executable && !(region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
    const auto offset = reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(region.BaseAddress);
    return offset < region.RegionSize && size <= region.RegionSize - offset;
}

bool HookInputApp(HMODULE mod) {
    TRACE_SCOPE();
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{L"private: void __cdecl WindowsInternal::ComposableShell::Experiences::TextInputUndocked::InputApp::InputPanelPage::OnCloseButtonPressed(void)"},
         &PageOnCloseButtonPressed_orig, PageOnCloseButtonPressed_hook, true},
    };
    bool ok = WindhawkUtils::HookSymbols(mod, hooks, ARRAYSIZE(hooks));
    Wh_Log(L"InputApp hooks %s", ok ? L"ok" : L"FAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// TextInput.dll (TextInputHost)
// ---------------------------------------------------------------------------
using PrimaryViewTryHide_t = void(__cdecl*)(void* pThis, void* reason);
PrimaryViewTryHide_t PrimaryViewTryHide_orig;
void __cdecl PrimaryViewTryHide_hook(void* pThis, void* reason) {
    TRACE_SCOPE();
    if (!g_textReady || g_unloading) {
        TRACE_BLOCK();
        PrimaryViewTryHide_orig(pThis, reason);
        return;
    }
    const auto hideReason = HStr(reason);
    if (wcscmp(hideReason, L"Close Button Pressed") == 0) {
        TRACE_BLOCK();
        UserClose();
    } else if (ReadSettings().noAutoHide && CompactModeActive() && !KeyboardCloseRequested() &&
               CompactCtrlRow::IsTouchKeyboardVisibleOnCurrentThread()) {
        VLOG(L"PrimaryViewTryHide '%s' blocked by Disable auto-dismiss", hideReason);
        return;
    }
    PrimaryViewTryHide_orig(pThis, reason);
}

// --- Native key sound ---
// Native keys play through TextInputAudioManager::PlayAsyncInternal(clip,
// variant). Watching those calls yields the live audio manager and the clip
// names; the most frequent clip is the ordinary key click. Injected keys
// replay it through the same manager, honoring the "Play key sounds" setting.
namespace KeySound {
// PlayAsyncInternal(String^ clip, <second>). The mangled name leaves the
// second parameter ambiguous (String^ or IAsyncAction^), so it is never read:
// native calls pass it through, and replays pass nullptr, valid for either.
using Play_t = IUnknown*(__cdecl*)(void* manager, HSTRING clip, void* second);
using Enabled_t = bool(__cdecl*)(void* manager);
void* g_playTarget;
Play_t g_play;
Enabled_t g_isEnabled;
std::mutex g_lock;
IUnknown* g_manager;  // AddRef'd; the panel model's long-lived audio manager
struct Clip { std::wstring name; unsigned uses; };
std::vector<Clip> g_clips;
thread_local bool t_replaying;

void Observe(std::wstring_view decorated, void* address) {
    if (decorated.starts_with(L"?PlayAsyncInternal@TextInputAudioManager@Controls@TextInputUndocked@"))
        g_playTarget = address;
    else if (decorated.starts_with(L"?IsAudioFeedbackEnabledInternal@TextInputAudioManager@Controls@TextInputUndocked@"))
        g_isEnabled = reinterpret_cast<Enabled_t>(address);
}

IUnknown* __cdecl Play_hook(void* manager, HSTRING clip, void* second) {
    if (!t_replaying && manager && clip && !g_unloading) {
        UINT32 length = 0;
        const PCWSTR text = WindowsGetStringRawBuffer(clip, &length);
        const std::wstring_view name(text ? text : L"", text ? length : 0);
        std::lock_guard lock(g_lock);
        if (g_manager != manager) {
            static_cast<IUnknown*>(manager)->AddRef();
            if (g_manager) g_manager->Release();
            g_manager = static_cast<IUnknown*>(manager);
        }
        auto it = std::find_if(g_clips.begin(), g_clips.end(), [&](const Clip& c) { return c.name == name; });
        if (it != g_clips.end()) ++it->uses;
        else if (!name.empty() && g_clips.size() < 32) {
            g_clips.push_back({std::wstring(name), 1});
            VLOG(L"Key sound: native clip '%.*s'", static_cast<int>(name.size()), name.data());
        }
    }
    return g_play(manager, clip, second);
}

// Plays the ordinary key click, if one has been heard and key sounds are on.
void PlayClick() {
    IUnknown* manager = nullptr;
    std::wstring clip;
    {
        std::lock_guard lock(g_lock);
        if (!g_manager || g_clips.empty() || !g_play) return;
        clip = std::max_element(g_clips.begin(), g_clips.end(),
                                [](const Clip& a, const Clip& b) { return a.uses < b.uses; })->name;
        manager = g_manager;
        manager->AddRef();
    }
    if (!g_isEnabled || g_isEnabled(manager)) {
        HSTRING name = nullptr;
        if (SUCCEEDED(WindowsCreateString(clip.c_str(), static_cast<UINT32>(clip.size()), &name))) {
            t_replaying = true;
            if (IUnknown* action = g_play(manager, name, nullptr)) action->Release();
            t_replaying = false;
            WindowsDeleteString(name);
        }
    }
    manager->Release();
}

bool Install() {
    return g_playTarget && Wh_SetFunctionHook(g_playTarget, reinterpret_cast<void*>(Play_hook),
                                              reinterpret_cast<void**>(&g_play));
}

void Release() {
    std::lock_guard lock(g_lock);
    if (g_manager) g_manager->Release();
    g_manager = nullptr;
}
}  // namespace KeySound

// --- TryHide caller filter ---
// TryHide has no reason argument, so the caller identifies the request. The
// callers are resolved by symbol name and bounded by the next symbol start,
// so the filter follows code moves between builds instead of fixed RVAs.
namespace AutoHide {
struct Range { uintptr_t begin, end; PCWSTR label; };
std::vector<uintptr_t> g_symbolStarts;  // sorted after resolution
std::vector<Range> g_blocked;
HMODULE g_image;

// Callers that hide the keyboard without the user asking.
struct Caller { PCWSTR prefix; PCWSTR label; };
constexpr Caller kBlockedCallers[] = {
    // Shell/app InputPane show/hide requests (focus leaving a text field,
    // app-driven TryHide). User closes go through the X button hooks.
    {L"?OnChangeRequestedInternal@ShellViewPositionAndSizeManager@TextInputUndocked@", L"OnChangeRequestedInternal"},
};
std::vector<std::pair<uintptr_t, PCWSTR>> g_pending;
std::vector<void*> g_tryHideCandidates;

void Observe(std::wstring_view decorated, PCWSTR undecorated, void* address) {
    const auto at = reinterpret_cast<uintptr_t>(address);
    g_symbolStarts.push_back(at);
    for (const auto& caller : kBlockedCallers)
        if (decorated.starts_with(caller.prefix)) g_pending.push_back({at, caller.label});
    constexpr std::wstring_view tryHide =
        L"WindowsInternal::ComposableShell::Experiences::TextInputUndocked::ShellViewPositionAndSizeManager::"
        L"[WindowsInternal::ComposableShell::Experiences::TextInput::Common::IViewPositionAndSizeManager]::TryHide";
    if (undecorated && std::wstring_view(undecorated).find(tryHide) != std::wstring_view::npos &&
        std::wstring_view(undecorated).find(L"__abi_") == std::wstring_view::npos)
        g_tryHideCandidates.push_back(address);
}

// The interface TryHide symbol also names an adjustor thunk (sub rcx, n; jmp
// body). Internal callers call the body directly, so hook the body.
static void* TryHideBody(HMODULE image) {
    for (void* candidate : g_tryHideCandidates) {
        const auto code = static_cast<const BYTE*>(candidate);
        if (!ImageContains(image, code, 16, true)) continue;
        const bool thunk = code[0] == 0xE9 ||
            (code[0] == 0x48 && code[1] == 0x83 && code[2] == 0xE9 && code[4] == 0xE9) ||  // sub rcx, imm8; jmp
            (code[0] == 0x48 && code[1] == 0x81 && code[2] == 0xE9 && code[7] == 0xE9);    // sub rcx, imm32; jmp
        if (!thunk) return candidate;
    }
    return nullptr;
}

void Finish(HMODULE image) {
    std::sort(g_symbolStarts.begin(), g_symbolStarts.end());
    g_symbolStarts.erase(std::unique(g_symbolStarts.begin(), g_symbolStarts.end()), g_symbolStarts.end());
    for (const auto& [begin, label] : g_pending) {
        const auto next = std::upper_bound(g_symbolStarts.begin(), g_symbolStarts.end(), begin);
        // Public symbols are sparse; cap the range so a missing neighbour
        // cannot swallow unrelated code.
        const uintptr_t end = next == g_symbolStarts.end() ? begin + 0x2000 : std::min(*next, begin + 0x2000);
        g_blocked.push_back({begin, end, label});
        VLOG(L"Auto-dismiss: blocking TryHide from %s (+%llX..+%llX)", label,
             static_cast<unsigned long long>(begin - reinterpret_cast<uintptr_t>(image)),
             static_cast<unsigned long long>(end - reinterpret_cast<uintptr_t>(image)));
    }
    g_pending.clear();
    g_image = image;
    if (g_blocked.empty()) Wh_Log(L"Auto-dismiss: no TryHide caller resolved; only reasoned hide paths are filtered");
}

const Range* Match(const void* returnAddress) {
    const auto at = reinterpret_cast<uintptr_t>(returnAddress);
    for (const auto& range : g_blocked)
        if (at > range.begin && at <= range.end) return &range;
    return nullptr;
}
}  // namespace AutoHide

// The shell hides the keyboard with two calls in one burst: a reasoned
// Deactivate ("Shell requested hide", blocked below) immediately followed by
// an unreasoned TryHide through the COM interface. A TryHide shortly after a
// blocked deactivation belongs to that same request.
std::atomic<ULONGLONG> g_blockedDeactivateAt{0};
constexpr ULONGLONG kPairedHideWindowMs = 250;

VoidThis_t KeyboardTryHide_orig;
void __cdecl KeyboardTryHide_hook(void* manager) {
    const auto caller = __builtin_return_address(0);
    if (g_textReady && !g_unloading) {
        const auto range = AutoHide::Match(caller);
        const bool paired = GetTickCount64() - g_blockedDeactivateAt.load() < kPairedHideWindowMs;
        const bool block = (range || paired) && ReadSettings().noAutoHide && CompactModeActive() &&
                           !KeyboardCloseRequested();
        // Every caller is logged so unknown dismiss paths can be added by name.
        VLOG(L"TryHide from +%llX (%s)%s", static_cast<unsigned long long>(
                 reinterpret_cast<uintptr_t>(caller) - reinterpret_cast<uintptr_t>(AutoHide::g_image)),
             range ? range->label : paired ? L"paired with blocked deactivation" : L"unclassified",
             block ? L" blocked by Disable auto-dismiss" : L"");
        if (block) return;
    }
    KeyboardTryHide_orig(manager);
}

using KeyboardCloseClicked_t = void(__cdecl*)(void*, void*, void*);
KeyboardCloseClicked_t KeyboardCloseClicked_orig;
void __cdecl KeyboardCloseClicked_hook(void* model, void* sender, void* args) {
    if (g_textReady && !g_unloading && ReadSettings().noAutoHide &&
        CompactCtrlRow::IsTouchKeyboardVisibleOnCurrentThread()) UserClose();
    KeyboardCloseClicked_orig(model, sender, args);
}

using KeyboardDeactivate_t = void(__cdecl*)(void*, void*);
KeyboardDeactivate_t KeyboardDeactivate_orig;
void __cdecl KeyboardDeactivate_hook(void* keyboard, void* reason) {
    const auto text = HStr(reason);
    if (g_textReady && !g_unloading && ReadSettings().noAutoHide && CompactModeActive() &&
        !KeyboardCloseRequested() && wcscmp(text, L"Close Button Pressed") != 0 &&
        CompactCtrlRow::IsTouchKeyboardVisibleOnCurrentThread()) {
        VLOG(L"Touch keyboard deactivation '%s' blocked by Disable auto-dismiss", text);
        g_blockedDeactivateAt = GetTickCount64();
        return;
    }
    KeyboardDeactivate_orig(keyboard, reason);
}

using OnShowRequestedInternal_t = void(__cdecl*)(void* pThis, void* args);
OnShowRequestedInternal_t OnShowRequestedInternal_orig;
void __cdecl OnShowRequestedInternal_hook(void* pThis, void* args) {
    TRACE_SCOPE();
    if (!g_textReady || g_unloading) {
        TRACE_BLOCK();
        TRACE_POINT(L"calling native show request (pass-through)");
        OnShowRequestedInternal_orig(pThis, args);
        TRACE_POINT(L"native show request returned (pass-through)");
        return;
    }
    const auto session = ++g_inputSession;
    TRACE_POINT(L"calling native show request");
    OnShowRequestedInternal_orig(pThis, args);
    TRACE_POINT(L"native show request returned");
    if (g_inputSession == session) NotifyKeyboardVisibilityChanged(false);
}

// --- swipe ---
using BoolThis_t = bool(__cdecl*)(void* pThis);
BoolThis_t PointerIsShapewritingEnabled_orig;
bool __cdecl PointerIsShapewritingEnabled_hook(void* pThis) {
    TRACE_SCOPE();
    bool r = PointerIsShapewritingEnabled_orig(pThis);
    return g_textReady && CompactModeActive() && ReadSettings().forceSwipe ? true : r;
}

BoolThis_t SipIsShapewritingEnabled_orig;
bool __cdecl SipIsShapewritingEnabled_hook(void* pThis) {
    TRACE_SCOPE();
    bool r = SipIsShapewritingEnabled_orig(pThis);
    return g_textReady && CompactModeActive() && ReadSettings().forceSwipe ? true : r;
}

namespace FooterKeys {
void RememberAdapter(IUnknown* adapter);
}

using TextFrameworkFocusChanged_t = void(__cdecl*)(void*, void*, void*);
TextFrameworkFocusChanged_t TextFrameworkFocusChanged_orig;
void __cdecl TextFrameworkFocusChanged_hook(void* model, void* adapter, void* args) {
    TextFrameworkFocusChanged_orig(model, adapter, args);
    if (g_textReady && !g_unloading)
        FooterKeys::RememberAdapter(static_cast<IUnknown*>(adapter));
}

using OnShapewritingEnabled_t = void(__cdecl*)(void* pThis, void* adapter, bool enabled);
OnShapewritingEnabled_t ModelOnShapewritingEnabled_orig;
void __cdecl ModelOnShapewritingEnabled_hook(void* pThis, void* adapter, bool enabled) {
    TRACE_SCOPE();
    ModelOnShapewritingEnabled_orig(pThis, adapter, g_textReady && CompactModeActive() && ReadSettings().forceSwipe ? true : enabled);
    if (g_textReady && !g_unloading)
        FooterKeys::RememberAdapter(static_cast<IUnknown*>(adapter));
}

using SwipeStartDistance_t = bool(__cdecl*)(void*, const void*);
SwipeStartDistance_t SwipeStartDistance_orig;
std::atomic<float> g_swipeKeyWidth{};

// IsTraveledEnoughToStartShapewriting(this, Point const&) keeps the gesture
// origin and a per-axis start threshold in the handler:
//     |cur.x - origin.x| > threshold.x  ||  |cur.y - origin.y| > threshold.y
// Both field offsets are decoded from the function's own loads at install
// time, so a rebuilt layout is followed instead of trusted. The hook then
// substitutes the threshold for one call and restores it, which keeps the
// native pre-checks and comparison intact.
size_t g_swipeThresholdOffset;  // 0 = unknown on this build
const void* g_swipeFunction;  // resolved in the shared symbol pass

static size_t DecodeSwipeThresholdOffset(HMODULE image, const void* function) {
    const auto code = static_cast<const BYTE*>(function);
    constexpr size_t kWindow = 0xA0;
    if (!ImageContains(image, code, kWindow, true)) return 0;
    // The handler pointer arrives in RCX and is usually copied to a
    // callee-saved register (mov r64, rcx) in the prologue.
    int self = 1;  // rcx
    for (size_t i = 0; i + 3 <= 0x20; ++i) {
        if (code[i] == 0x48 && code[i + 1] == 0x8B && (code[i + 2] & 0xC7) == 0xC1) {
            self = (code[i + 2] >> 3) & 7;
            break;
        }
    }
    if (self == 4 || self == 5) return 0;  // rsp/rbp bases need SIB/other forms
    // Collect scalar float loads from [self + disp]: movss xmm, [reg+disp8/32].
    std::vector<uint32_t> loads;
    for (size_t i = 0; i + 5 <= kWindow && loads.size() < 8; ++i) {
        if (code[i] != 0xF3 || code[i + 1] != 0x0F || code[i + 2] != 0x10) continue;
        const BYTE modrm = code[i + 3];
        if ((modrm & 7) != self) continue;
        if ((modrm & 0xC0) == 0x40) loads.push_back(code[i + 4]);
        else if ((modrm & 0xC0) == 0x80 && i + 8 <= kWindow) {
            uint32_t disp;
            memcpy(&disp, code + i + 4, 4);
            loads.push_back(disp);
        }
    }
    // Expect origin.x, threshold.x, origin.y (+4), threshold.y (+4).
    if (loads.size() < 4) return 0;
    const uint32_t origin = loads[0], threshold = loads[1];
    if (loads[2] != origin + 4 || loads[3] != threshold + 4 || origin == threshold ||
        threshold % 4 || threshold > 0x1000) return 0;
    return threshold;
}

bool __cdecl SwipeStartDistance_hook(void* handler, const void* point) {
    const auto settings = ReadSettings();
    const float keyWidth = g_swipeKeyWidth.load(std::memory_order_relaxed);
    if (!g_textReady || g_unloading || !CompactModeActive() || !settings.customSwipeStartDistance ||
        !g_swipeThresholdOffset || !handler || !std::isfinite(keyWidth) || keyWidth <= 0)
        return SwipeStartDistance_orig(handler, point);

    const auto threshold = reinterpret_cast<float*>(static_cast<BYTE*>(handler) + g_swipeThresholdOffset);
    const float native[2] = {threshold[0], threshold[1]};
    // Only substitute values that look like a live threshold pair.
    if (!std::isfinite(native[0]) || !std::isfinite(native[1]) || native[0] < 0 || native[1] < 0 ||
        native[0] > 10000 || native[1] > 10000)
        return SwipeStartDistance_orig(handler, point);
    const float distance = keyWidth * (settings.swipeStartDistancePercent / 100.0f);
    thread_local float lastLogged = -1;
    if (g_verboseLogging && lastLogged != distance) {
        lastLogged = distance;
        Wh_Log(L"Swipe start: %d%% of %.2f DIP = %.2f DIP (native %.2f x %.2f)",
               settings.swipeStartDistancePercent, keyWidth, distance, native[0], native[1]);
    }
    threshold[0] = threshold[1] = distance;
    const bool result = SwipeStartDistance_orig(handler, point);
    threshold[0] = native[0];
    threshold[1] = native[1];
    return result;
}

// --- size ---
// SizePositionInfo layout (observed, no public type info): int kind, then 18
// floats: view w,h | panel w,h | rect x,y,w,h | rect x,y,w,h | toolbar
// metrics, then an int. The hook validates the compact geometry before
// touching anything, so a changed layout leaves the native size untouched.
constexpr int kSizeFloats = 18;
std::atomic<float> g_footerReserveDip{};
namespace CompactCtrlRow {
void NotifyModifierReleased();
}

namespace FooterKeys {
extern std::atomic<bool> g_enabled;
}
// --- frame size actually applied ---
// The rows need room the native frame only has once the shell applies the
// enlarged size. ShellViewPositionAndSizeManager::TrySetPositionAndSize is
// where the keyboard hands a SizePositionInfo (same layout as above) to the
// shell; the rows are revealed only when the applied view height includes
// the reserve requested in CalculateDesiredSize. Event-driven, no timers.
namespace FrameSize {
std::atomic<float> g_wantedView{0};   // view height requested with the reserve
std::atomic<float> g_appliedView{0};  // view height last handed to the shell
// Two native paths hand a size to the shell: TrySetPositionAndSize for
// resizes, and GetPlacement, which packages the size into the show placement.
using Apply_t = void(__cdecl*)(void* self, const void* info);
using Placement_t = void*(__cdecl*)(void* self, const void* info);
void* g_applyTarget;
void* g_placementTarget;
Apply_t g_apply;
Placement_t g_placement;

void Observe(std::wstring_view decorated, void* address) {
    // The primary vtable entry, not the WBI adjustor thunk.
    if (decorated == L"?TrySetPositionAndSize@?QIViewPositionAndSizeManager@Common@TextInput@Experiences@"
                     L"ComposableShell@WindowsInternal@@ShellViewPositionAndSizeManager@TextInputUndocked@456@"
                     L"UE$AAAXVSizePositionInfo@23456@@Z")
        g_applyTarget = address;
    else if (decorated.starts_with(L"?GetPlacement@ShellViewPositionAndSizeManager@TextInputUndocked@") &&
             decorated.find(L"AEAVSizePositionInfo@") != std::wstring_view::npos)
        g_placementTarget = address;
}

static void Record(const void* info, PCWSTR path) {
    if (!info || g_unloading) return;
    float view[2];
    std::memcpy(view, static_cast<const BYTE*>(info) + sizeof(int), sizeof(view));
    if (!std::isfinite(view[1]) || view[1] <= 0 || view[1] >= 100000) return;
    g_appliedView.store(view[1]);
    VLOG(L"Frame size %s: view %.1fx%.1f (rows want %.1f)", path, view[0], view[1], g_wantedView.load());
    CompactCtrlRow::NotifyModifierReleased();  // posts a row refresh
}

// SizePositionInfo is passed by value, so the callee gets a pointer to a copy.
void __cdecl Apply_hook(void* self, const void* info) {
    Record(info, L"applied");
    g_apply(self, info);
}

// Takes SizePositionInfo by reference; the placement it returns carries it.
void* __cdecl Placement_hook(void* self, const void* info) {
    Record(info, L"placed");
    return g_placement(self, info);
}

bool Install() {
    const bool apply = g_applyTarget && Wh_SetFunctionHook(g_applyTarget, reinterpret_cast<void*>(Apply_hook),
                                                           reinterpret_cast<void**>(&g_apply));
    const bool placement = g_placementTarget &&
        Wh_SetFunctionHook(g_placementTarget, reinterpret_cast<void*>(Placement_hook),
                           reinterpret_cast<void**>(&g_placement));
    return apply && placement;
}
}  // namespace FrameSize

// Whether the shell frame currently has room for the rows. Without the apply
// hook there is no signal, so the rows are shown as before.
static bool FrameHasRowRoom() {
    if (!FrameSize::g_apply) return true;
    const float wanted = FrameSize::g_wantedView.load();
    return wanted > 0 && FrameSize::g_appliedView.load() >= wanted - 1;
}

using CalcDesiredSize_t = void*(__cdecl*)(void* pThis, void* ret);
CalcDesiredSize_t WNCalcDesiredSize_orig;
void* __cdecl WNCalcDesiredSize_hook(void* pThis, void* ret) {
    TRACE_SCOPE();
    void* r = WNCalcDesiredSize_orig(pThis, ret);
    const auto settings = ReadSettings();
    if (!g_sizeReady || !ret || r != ret || (settings.horizontalScale == 100 && !FooterKeys::g_enabled)) return r;
    auto bytes = static_cast<BYTE*>(ret) + sizeof(int);
    std::array<float, kSizeFloats> values;
    std::memcpy(values.data(), bytes, sizeof(values));
    if (!std::all_of(values.begin(), values.end(), [](float value) {
            return std::isfinite(value) && std::abs(value) <= 100000;
        })) return r;
    const float viewHeight = values[1];
    const float panelWidth = values[2];
    const float panelHeight = values[3];
    // Compact has a taller outer view and two matching keyboard rectangles
    // below the transparent strip. Test the stock geometry before changing it.
    const bool compactGeometry = panelWidth > 0 && panelHeight > panelWidth &&
        std::abs(values[0] - panelWidth) <= 2 && std::abs(viewHeight - panelHeight) <= 2 &&
        std::abs(values[4]) <= 2 && std::abs(values[8]) <= 2 &&
        std::abs(values[6] - panelWidth) <= 2 && std::abs(values[10] - panelWidth) <= 2 &&
        values[5] > 0 && std::abs(values[5] - values[9]) <= 2 &&
        std::abs(values[5] + values[7] - viewHeight) <= 2 &&
        std::abs(values[9] + values[11] - viewHeight) <= 2;
    if (!compactGeometry) {
        g_footerReserveDip.store(0);
        return r;
    }
    float reserve = 0;
    if (FooterKeys::g_enabled) {
        // Room for the two injected rows below the native keys.
        reserve = 2.1f * std::clamp(panelWidth * 0.16f, 36.0f, 104.0f);
        for (const int y : {5, 9})
            if (values[y + 2] > panelHeight / 2) values[y + 2] += reserve;
        values[1] += reserve;
        values[3] += reserve;
    }
    if (settings.horizontalScale != 100) {
        const float widthFactor = settings.horizontalScale / 100.0f;
        // View width, panel width, and the X / width pairs of each rectangle.
        for (const int index : {0, 2, 4, 6, 8, 10}) values[index] *= widthFactor;
    }
    if (reserve > 0) FrameSize::g_wantedView.store(values[1]);
    if (g_footerReserveDip.exchange(reserve) != reserve)
        VLOG(L"Compact sizing: view %.1f -> %.1f DIP (%.1f DIP footer), width %d%%",
             viewHeight, values[1], reserve, settings.horizontalScale);
    std::memcpy(bytes, values.data(), sizeof(values));
    return r;
}

#define TIU L"WindowsInternal::ComposableShell::Experiences::TextInputUndocked::"
#define TIC L"WindowsInternal::ComposableShell::Experiences::TextInput::Common::"
#define OSI L"WindowsUdk::ApplicationModel::OnScreenInput::"

namespace FooterKeys {
using ProcessAction = void(__cdecl*)(void*, void*);
using HandleAction = bool(__cdecl*)(void*, unsigned int, void*, unsigned int);
using GetSubtype = unsigned int(__cdecl*)(void*);
using GetVirtualKey = unsigned int(__cdecl*)(unsigned int);
using GetValue = HSTRING(__cdecl*)(void*);
using InjectRawKey = void(__cdecl*)(void*, unsigned int, unsigned int, unsigned short);
using InjectKeyAbi = HRESULT(__cdecl*)(void*, void*);

ProcessAction g_processAction;
HandleAction g_handleAction;
GetSubtype g_getSubtype;
GetVirtualKey g_getVirtualKey;
GetValue g_getValue;
InjectRawKey g_injectRawKey;
InjectKeyAbi g_injectKeyAbi[2];
std::atomic<bool> g_enabled{false};
std::atomic<DWORD> g_arrowUiThread;
// Accessed only on the XAML thread that owns the compact row.
winrt::com_ptr<IUnknown> g_adapter;
HWND g_adapterForeground;
HWND g_adapterFocus;
unsigned g_adapterSession;
std::vector<void*> g_adapterRawMethods;
size_t g_adapterRawSlot;
HMODULE g_adapterImage;
void* const* g_keyboardControlVtable;
void* g_keyboardModelGetter;
size_t g_keyboardModelSlot;
std::vector<void*> g_modelAdapterGetters;

struct Shortcut {
    HWND target = nullptr;
    HWND focus = nullptr;
    unsigned session = 0;
    unsigned int pointerId = 0;
    unsigned short vk = 0;
    unsigned short modifier = 0;
};
struct Action {
    unsigned short vk = 0;
    unsigned short modifier = 0;
    bool release = false;
    bool injected = false;
};
thread_local Shortcut t_shortcut;
thread_local Action* t_action;
thread_local bool t_injectKeyActive;

static HWND FocusWithin(HWND foreground);

static bool ShortcutMatchesInput(HWND foreground) {
    TRACE_SCOPE();
    return t_shortcut.target && t_shortcut.target == foreground && t_shortcut.session == g_inputSession &&
           t_shortcut.focus == FocusWithin(foreground);
}

unsigned short ShortcutVirtualKey(void* key) {
    TRACE_SCOPE();
    const auto vk = g_getVirtualKey(g_getSubtype(key));
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') ||
        (vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_DELETE ||
        vk == VK_TAB || vk == VK_ESCAPE || vk == VK_BACK || vk == VK_RETURN ||
        vk == VK_HOME || vk == VK_END || vk == VK_SPACE) {
        TRACE_BLOCK();
        return static_cast<unsigned short>(vk);
    }

    HSTRING value = g_getValue(key);
    UINT32 length = 0;
    PCWSTR text = WindowsGetStringRawBuffer(value, &length);
    wchar_t letter = length == 1 ? text[0] : 0;
    WindowsDeleteString(value);
    if (letter >= L'a' && letter <= L'z') letter -= L'a' - L'A';
    if ((letter >= L'A' && letter <= L'Z') || (letter >= L'0' && letter <= L'9'))
        return static_cast<unsigned short>(letter);

    // Symbol layouts can expose punctuation as text without a native VK.
    // Only translate characters that need no extra modifiers in the target layout.
    if (letter) {
        const DWORD thread = GetWindowThreadProcessId(t_shortcut.focus, nullptr);
        if (thread) {
            const SHORT mapped = VkKeyScanExW(letter, GetKeyboardLayout(thread));
            if (mapped > 0 && mapped <= 0xFF) return static_cast<unsigned short>(mapped);
        }
    }
    return 0;
}

void __cdecl ProcessAction_hook(void* handler, void* args) {
    TRACE_SCOPE();
    if (!g_enabled || !CompactModeActive() || !t_shortcut.target || t_action) {
        TRACE_BLOCK();
        g_processAction(handler, args);
        return;
    }
    if (!ShortcutMatchesInput(GetForegroundWindow())) {
        TRACE_BLOCK();
        t_shortcut = {};
        g_processAction(handler, args);
        return;
    }
    Action action;
    {
        TRACE_BLOCK();
        struct ActionScope {
            explicit ActionScope(Action& action) { t_action = &action; }
            ~ActionScope() { t_action = nullptr; }
        } scope(action);
        g_processAction(handler, args);
    }
    if (action.release && !action.injected)
        Wh_Log(L"Footer keys: key release did not reach the native injector");
}

bool __cdecl HandleAction_hook(void* manager, unsigned int type, void* key,
                               unsigned int pointerId) {
    TRACE_SCOPE();
    if (t_action && !t_action->vk && t_shortcut.target && key && type <= 1) {
        TRACE_BLOCK();
        if (type == 0 && !t_shortcut.vk) {
            TRACE_BLOCK();
            t_shortcut.vk = ShortcutVirtualKey(key);
            if (t_shortcut.vk) t_shortcut.pointerId = pointerId;
        }
        if (t_shortcut.vk && t_shortcut.pointerId == pointerId) {
            TRACE_BLOCK();
            t_action->vk = t_shortcut.vk;
            t_action->modifier = t_shortcut.modifier;
            t_action->release = type == 1;
            if (t_action->release) {
                t_shortcut = {};
                CompactCtrlRow::NotifyModifierReleased();
            }
        }
    }
    return g_handleAction(manager, type, key, pointerId);
}

void ToggleModifier(unsigned short modifier) {
    TRACE_SCOPE();
    const HWND target = GetForegroundWindow();
    if (ShortcutMatchesInput(target) && t_shortcut.modifier == modifier) {
        TRACE_BLOCK();
        t_shortcut = {};
    } else {
        TRACE_BLOCK();
        const HWND focus = FocusWithin(target);
        t_shortcut = focus ? Shortcut{target, focus, g_inputSession.load()} : Shortcut{};
        if (focus) t_shortcut.modifier = modifier;
    }
}

bool IsNativeKeyboardAdapter(IUnknown* adapter) {
    TRACE_SCOPE();
    if (!adapter || g_adapterRawMethods.empty()) return false;
    const auto vtable = *reinterpret_cast<const BYTE* const*>(adapter);
    if (!ImageContains(g_adapterImage, vtable, (g_adapterRawSlot + 1) * sizeof(void*))) return false;
    void* entry;
    std::memcpy(&entry, vtable + g_adapterRawSlot * sizeof(entry), sizeof(entry));
    return std::find(g_adapterRawMethods.begin(), g_adapterRawMethods.end(), entry) != g_adapterRawMethods.end();
}

static HWND FocusWithin(HWND foreground) {
    TRACE_SCOPE();
    GUITHREADINFO info{sizeof(info)};
    DWORD pid = 0;
    const DWORD tid = foreground ? GetWindowThreadProcessId(foreground, &pid) : 0;
    return tid && pid != GetCurrentProcessId() && GetGUIThreadInfo(tid, &info) ? info.hwndFocus : nullptr;
}

void ClearAdapter() {
    TRACE_SCOPE();
    auto previous = std::move(g_adapter);
    g_adapterForeground = g_adapterFocus = nullptr;
}

bool AdapterMatchesInput(HWND foreground) {
    TRACE_SCOPE();
    return g_adapter && foreground == g_adapterForeground && FocusWithin(foreground) == g_adapterFocus &&
           g_inputSession == g_adapterSession;
}

void RememberAdapter(IUnknown* adapter) {
    TRACE_SCOPE();
    const DWORD thread = GetCurrentThreadId();
    if (!g_enabled || !CompactModeActive() || g_arrowUiThread != thread || !IsNativeKeyboardAdapter(adapter)) return;
    const auto foreground = GetForegroundWindow();
    const auto focus = FocusWithin(foreground);
    if (!focus) return;
    winrt::com_ptr<IUnknown> previous;
    if (g_adapter.get() != adapter) {
        TRACE_BLOCK();
        previous.copy_from(adapter);
        std::swap(g_adapter, previous);
    }
    if (g_adapterForeground != foreground || g_adapterFocus != focus || g_adapterSession != g_inputSession)
        VLOG(L"Compact keys: captured native adapter for foreground %p focus %p", foreground, focus);
    g_adapterForeground = foreground;
    g_adapterFocus = focus;
    g_adapterSession = g_inputSession;
}

void SendVirtualKey(void* adapter, unsigned short vk, unsigned short modifier) {
    TRACE_SCOPE();
    if (modifier == VK_LWIN) {
        // Windows shell hotkeys need to reach the system input stream.
        SendShortcut({{VK_LWIN, vk}, 2});
        return;
    }
    // Confirmed from native virtual-key callers of InjectRawKey.
    constexpr unsigned int kDown = 1;
    constexpr unsigned int kUp = 4;
    constexpr unsigned int kVirtualKey = 1;
    if (modifier) g_injectRawKey(adapter, kDown, kVirtualKey, modifier);
    g_injectRawKey(adapter, kDown, kVirtualKey, vk);
    g_injectRawKey(adapter, kUp, kVirtualKey, vk);
    if (modifier) g_injectRawKey(adapter, kUp, kVirtualKey, modifier);
}

static winrt::com_ptr<IUnknown> GetCurrentKeyboardAdapter() {
    using Getter = HRESULT(STDMETHODCALLTYPE*)(void*, IUnknown**);
    if (!g_keyboardModelSlot || g_modelAdapterGetters.empty()) return {};
    const auto control = CompactCtrlRow::CurrentKeyboardControl();
    if (!control) return {};

    uint32_t count = 0;
    winrt::guid* ids = nullptr;
    const auto abi = reinterpret_cast<winrt::impl::abi_t<winrt::Windows::Foundation::IInspectable>*>(winrt::get_abi(control));
    const HRESULT result = abi->GetIids(&count, &ids);
    struct FreeIids {
        winrt::guid* value;
        ~FreeIids() { CoTaskMemFree(value); }
    } freeIds{ids};
    if (FAILED(result) || !ids) return {};

    for (ULONG i = 0; i < count; ++i) {
        winrt::com_ptr<IUnknown> keyboardInterface;
        if (FAILED(abi->QueryInterface(ids[i], keyboardInterface.put_void()))) continue;
        const auto vtable = *reinterpret_cast<void* const* const*>(keyboardInterface.get());
        if (vtable != g_keyboardControlVtable || vtable[g_keyboardModelSlot] != g_keyboardModelGetter) continue;

        winrt::com_ptr<IUnknown> model;
        if (FAILED(reinterpret_cast<Getter>(g_keyboardModelGetter)(keyboardInterface.get(), model.put())) || !model)
            return {};
        // Find the native IInputPanelModel::TextFrameworkAdapter getter in the
        // model's vtable instead of assuming its slot.
        constexpr size_t kMaxSlots = 48;
        const auto modelVtable = *reinterpret_cast<void* const* const*>(model.get());
        if (!ImageContains(g_adapterImage, modelVtable, kMaxSlots * sizeof(void*))) return {};
        void* getter = nullptr;
        for (size_t slot = 6; slot < kMaxSlots && !getter; ++slot) {
            if (std::find(g_modelAdapterGetters.begin(), g_modelAdapterGetters.end(), modelVtable[slot]) !=
                g_modelAdapterGetters.end())
                getter = modelVtable[slot];
        }
        if (!getter) return {};

        winrt::com_ptr<IUnknown> adapter;
        if (SUCCEEDED(reinterpret_cast<Getter>(getter)(model.get(), adapter.put())) &&
            IsNativeKeyboardAdapter(adapter.get())) return adapter;
        return {};
    }
    return {};
}

void SendFooterChord(const Settings::ShortcutChord& chord) {
    const HWND foreground = GetForegroundWindow();
    if (!foreground || !chord.count || !CompactModeActive() || g_arrowUiThread != GetCurrentThreadId()) return;
    t_shortcut = {};

    const auto end = chord.keys.begin() + chord.count;
    const bool winChord = std::find(chord.keys.begin(), end, VK_LWIN) != end;
    const bool altChord = std::find(chord.keys.begin(), end, VK_MENU) != end;
    const WORD mainKey = chord.keys[chord.count - 1];
    const bool shellChord = winChord || (altChord &&
        (mainKey == VK_TAB || mainKey == VK_ESCAPE || mainKey == VK_LEFT || mainKey == VK_RIGHT));
    // Follow the primed-modifier path for focused text input. Shell and
    // navigation shortcuts still need the standard Windows input stream.
    winrt::com_ptr<IUnknown> adapter;
    if (!shellChord && chord.count > 1 && g_injectRawKey) {
        if (AdapterMatchesInput(foreground)) adapter = g_adapter;
        else {
            adapter = GetCurrentKeyboardAdapter();
            VLOG(L"Compact footer chord: on-demand adapter %s", adapter ? L"resolved" : L"unavailable");
        }
    }
    if (GetForegroundWindow() != foreground) return;
    if (adapter) {
        if (chord.count == 2) {
            SendVirtualKey(adapter.get(), chord.keys[1], chord.keys[0]);
        } else {
            constexpr unsigned int kDown = 1, kUp = 4, kVirtualKey = 1;
            for (auto key = chord.keys.begin(); key != end; ++key)
                g_injectRawKey(adapter.get(), kDown, kVirtualKey, *key);
            for (auto key = end; key != chord.keys.begin();)
                g_injectRawKey(adapter.get(), kUp, kVirtualKey, *--key);
        }
        VLOG(L"Compact footer chord: sent through native keyboard adapter");
    } else {
        SendShortcut(chord);
        VLOG(L"Compact footer chord: used standard Windows keyboard events for VK 0x%02X", static_cast<unsigned>(mainKey));
    }
}

void PressFooterKey(unsigned short vk) {
    TRACE_SCOPE();
    if (!g_enabled || !CompactModeActive() || g_arrowUiThread != GetCurrentThreadId()) return;
    const HWND target = GetForegroundWindow();
    if (!target) return;
    const WORD modifier = ShortcutMatchesInput(target) ? t_shortcut.modifier : 0;
    t_shortcut = {};
    if (modifier && modifier != vk) SendShortcut({{modifier, vk}, 2});
    else SendShortcut({{vk}, 1});
}

template <size_t entry>
HRESULT __cdecl InjectKey_hook(void* adapter, void* args) {
    TRACE_SCOPE();
    const auto original = g_injectKeyAbi[entry];
    // The adjustor thunk can call the other entry point. Inspect and cache the
    // interface pointer only at the outermost call.
    if (t_injectKeyActive || !g_enabled) return original(adapter, args);
    struct InjectionScope {
        InjectionScope() { t_injectKeyActive = true; }
        ~InjectionScope() { t_injectKeyActive = false; }
    } scope;
    const auto nativeAdapter = static_cast<IUnknown*>(adapter);
    RememberAdapter(nativeAdapter);
    const auto action = t_action;
    if (!action || !action->vk || !IsNativeKeyboardAdapter(nativeAdapter)) {
        TRACE_BLOCK();
        TRACE_POINT(L"calling concrete native key injector");
        return original(adapter, args);
    }
    if (action->release && !action->injected) {
        TRACE_BLOCK();
        action->injected = true;
        SendVirtualKey(adapter, action->vk, action->modifier);
        Wh_Log(L"Compact keys: sent shortcut through native InjectRawKey");
    }
    return S_OK;
}

// Decode the vtable slot that a thin virtual-call wrapper dispatches through:
// it loads the vtable from the interface pointer in RCX, then calls, jumps or
// loads through [rax + disp]. Accept both 8-bit and 32-bit displacements and
// skip a short prologue so compiler changes do not break the lookup.
static size_t FindVirtualDispatchSlot(HMODULE module, const void* function) {
    const auto code = static_cast<const BYTE*>(function);
    constexpr size_t kWindow = 48;
    if (!ImageContains(module, code, kWindow, true)) return 0;
    for (size_t i = 0; i + 3 <= kWindow; ++i) {
        // mov rax, [rcx]
        if (code[i] != 0x48 || code[i + 1] != 0x8B || code[i + 2] != 0x01) continue;
        for (size_t j = i + 3; j + 6 <= kWindow && j < i + 24; ++j) {
            uint32_t offset = 0;
            const BYTE op = code[j], modrm = code[j + 1];
            const bool rex = op == 0x48 && code[j + 1] == 0x8B;  // mov reg, [rax+disp]
            const BYTE m = rex ? code[j + 2] : modrm;
            const bool indirect = op == 0xFF && ((m & 0x38) == 0x10 || (m & 0x38) == 0x20);  // call/jmp
            if (!rex && !indirect) continue;
            const size_t at = j + (rex ? 3 : 2);
            if ((m & 0xC7) == 0x80) memcpy(&offset, code + at, 4);       // [rax+disp32]
            else if ((m & 0xC7) == 0x40) offset = code[at];               // [rax+disp8]
            else continue;
            if (offset >= 6 * sizeof(void*) && offset < 0x400 && offset % sizeof(void*) == 0)
                return offset / sizeof(void*);
        }
    }
    return 0;
}

void Install(HMODULE module) {
    TRACE_SCOPE();
    void* process = nullptr;
    void* handle = nullptr;
    void* inject[2]{};
    bool ambiguousInject = false;
    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(module, nullptr, &symbol);
    if (!search) return;
    struct SearchScope {
        HANDLE handle;
        ~SearchScope() { Wh_FindCloseSymbol(handle); }
    } scope{search};
    do {
        TRACE_BLOCK();
        if (!symbol.symbolDecorated || !symbol.address) continue;
        const std::wstring_view name = symbol.symbolDecorated;
        AutoHide::Observe(name, symbol.symbol, const_cast<void*>(symbol.address));
        KeySound::Observe(name, const_cast<void*>(symbol.address));
        FrameSize::Observe(name, const_cast<void*>(symbol.address));
        if (symbol.symbol && wcsstr(symbol.symbol, L"Controls::DefaultPointerHandler::IsTraveledEnoughToStartShapewriting"))
            g_swipeFunction = symbol.address;
        if (name.starts_with(L"?ProcessKeyAction@DefaultKeyActionHandlerImpl@Controls@"))
            process = symbol.address;
        else if (name.starts_with(L"?HandleKeyAction@ModifierStateManagerImpl@Controls@"))
            handle = symbol.address;
        // The projected ITextFrameworkAdapter::InjectKey wrapper is folded
        // with IToolBarList::Clear. Hook the adapter's concrete ABI instead.
        else if (name.starts_with(L"?__abi_WindowsInternal_ComposableShell_Experiences_TextInput_Common_ITextFrameworkAdapter____abi_InjectKey@?QITextFrameworkAdapter@") &&
                 name.find(L"@SystemInputProcessorTextFrameworkAdapter@") != std::wstring_view::npos &&
                 name.ends_with(L"PE$AAUIKeyboardKeyEventArgs@23456@@Z")) {
            TRACE_BLOCK();
            const size_t entry = name.find(L"@UE$AAAJ") != std::wstring_view::npos ? 0 : 1;
            if (inject[entry] && inject[entry] != symbol.address) ambiguousInject = true;
            inject[entry] = symbol.address;
        }
        else if (name == L"??_7InputPanelControl@Controls@TextInputUndocked@Experiences@ComposableShell@WindowsInternal@@6BIXamlKeyboardControl@TouchKeyboardUX@@@")
            g_keyboardControlVtable = static_cast<void* const*>(symbol.address);
        else if (name.starts_with(L"?__abi_TouchKeyboardUX_IXamlKeyboardControl____abi_get_Model@?QIXamlKeyboardControl@TouchKeyboardUX@@InputPanelControl@") &&
                 name.find(L"@@WBDI@") != name.npos && ImageContains(module, symbol.address, 1, true))
            g_keyboardModelGetter = symbol.address;
        else if (name.starts_with(L"?__abi_TouchKeyboardUX_IInputPanelModel____abi_get_TextFrameworkAdapter@?QIInputPanelModel@TouchKeyboardUX@@InputPanelModel@") &&
                 ImageContains(module, symbol.address, 1, true))
            g_modelAdapterGetters.push_back(symbol.address);
        else if (name.starts_with(L"?InjectRawKey@ITextFrameworkAdapter@Common@"))
            g_injectRawKey = reinterpret_cast<InjectRawKey>(symbol.address);
        else if (name.starts_with(L"?get@SubTypeAsEnum@IKeyboardKeyData@LayoutData@"))
            g_getSubtype = reinterpret_cast<GetSubtype>(symbol.address);
        else if (name.starts_with(L"?GetVirtualKeyFromKeySubType@Utility@@"))
            g_getVirtualKey = reinterpret_cast<GetVirtualKey>(symbol.address);
        else if (name.starts_with(L"?get@Value@IKeyboardKeyData@LayoutData@"))
            g_getValue = reinterpret_cast<GetValue>(symbol.address);
        else if (name.starts_with(L"?__abi_WindowsInternal_ComposableShell_Experiences_TextInput_Common_ITextFrameworkAdapter____abi_InjectRawKey@?QITextFrameworkAdapter@") &&
                 name.find(L"@SystemInputProcessorTextFrameworkAdapter@") != std::wstring_view::npos &&
                 ImageContains(module, symbol.address, 1, true))
            g_adapterRawMethods.push_back(symbol.address);
    } while (Wh_FindNextSymbol(search, &symbol));
    const void* code[] = {process, handle, inject[0], inject[1], reinterpret_cast<void*>(g_injectRawKey),
                          reinterpret_cast<void*>(g_getSubtype), reinterpret_cast<void*>(g_getVirtualKey),
                          reinterpret_cast<void*>(g_getValue)};
    if (ambiguousInject || inject[0] == inject[1] ||
        !std::all_of(std::begin(code), std::end(code), [module](const void* address) {
            TRACE_BLOCK();
            return ImageContains(module, address, 1, true);
        })) {
        TRACE_BLOCK();
        Wh_Log(L"Footer keys: required native key-action symbols unavailable");
        return;
    }
    // Locate the virtual dispatch slot in the native raw-key wrapper.
    g_adapterRawSlot = FindVirtualDispatchSlot(module, reinterpret_cast<const void*>(g_injectRawKey));
    if (!g_adapterRawSlot || g_adapterRawMethods.empty()) {
        Wh_Log(L"Footer keys: native raw-key dispatch not found; sticky Ctrl/Alt for native keys unavailable");
        return;
    }
    g_adapterImage = module;
    if (g_keyboardModelGetter && ImageContains(module, g_keyboardControlVtable, 48 * sizeof(void*))) {
        for (size_t slot = 6; slot < 48; ++slot) {
            if (g_keyboardControlVtable[slot] == g_keyboardModelGetter) {
                g_keyboardModelSlot = slot;
                break;
            }
        }
    }
    VLOG(L"Compact keys: on-demand keyboard adapter %s",
         g_keyboardModelSlot && !g_modelAdapterGetters.empty() ? L"available" : L"unavailable");
    const bool installed =
        Wh_SetFunctionHook(process, reinterpret_cast<void*>(ProcessAction_hook),
                           reinterpret_cast<void**>(&g_processAction)) &&
        Wh_SetFunctionHook(handle, reinterpret_cast<void*>(HandleAction_hook),
                           reinterpret_cast<void**>(&g_handleAction)) &&
        Wh_SetFunctionHook(inject[0], reinterpret_cast<void*>(InjectKey_hook<0>),
                           reinterpret_cast<void**>(&g_injectKeyAbi[0])) &&
        Wh_SetFunctionHook(inject[1], reinterpret_cast<void*>(InjectKey_hook<1>),
                           reinterpret_cast<void**>(&g_injectKeyAbi[1]));
    Wh_Log(L"Footer keys: native key-action hooks %s", installed ? L"installed" : L"failed");
}
}  // namespace FooterKeys

namespace CompactCtrlRow {
using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using Media::VisualTreeHelper;

constexpr wchar_t kWindowClass[] = L"WhCompactCtrlRow";
constexpr UINT kRemove = WM_APP + 1;
constexpr UINT kRefresh = WM_APP + 2;
constexpr UINT_PTR kWatchdog = 1;
constexpr UINT_PTR kArrowRepeat = 2;
WORD g_repeatingArrow;
bool g_arrowRepeatStarted;

void StopArrowRepeat(HWND hwnd) {
    KillTimer(hwnd, kArrowRepeat);
    g_repeatingArrow = 0;
}

constexpr UINT kWatchdogMs = 500;
std::atomic<HWND> g_uiWindow;
std::atomic<bool> g_stopping;

void NotifyModifierReleased() {
    if (const HWND hwnd = g_uiWindow.load(); hwnd && !g_stopping) PostMessage(hwnd, kRefresh, 0, 0);
}
std::mutex g_uiRegistrationMutex;
bool g_refreshing;
bool g_removing;
bool g_refreshQueued;
bool g_rowNeedsRemoval;
ULONGLONG g_rowUnstableSince;  // see HoldRowThroughGlitch
FrameworkElement g_layoutRoot{nullptr};
event_token g_layoutUpdated{};
Grid g_panel{nullptr};
FrameworkElement g_keyboard{nullptr};

struct CoreWindowHandle : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_WindowHandle(HWND* hwnd) = 0;
};

static HWND GetCoreWindowHandle(const Window& window) {
    const auto core = window.CoreWindow();
    constexpr GUID interopId{0x45D64A29, 0xA63E, 0x4CB6, {0xB4, 0x98, 0x57, 0x81, 0xD2, 0x98, 0xCB, 0x4F}};
    winrt::com_ptr<CoreWindowHandle> interop;
    if (!core || FAILED(reinterpret_cast<IUnknown*>(get_abi(core))->QueryInterface(interopId, interop.put_void())))
        return nullptr;
    HWND hwnd{};
    if (!interop || FAILED(interop->get_WindowHandle(&hwnd)) ||
        GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId()) return nullptr;
    return hwnd;
}

Canvas g_strip{nullptr};
Button g_button{nullptr};
Media::SolidColorBrush g_primedFace{nullptr};
Media::SolidColorBrush g_primedLabel{nullptr};
event_token g_click{};
// Native keys click on press; every injected key shares this handler.
Windows::Foundation::IInspectable g_soundPressed{nullptr};
struct FooterKey {
    const wchar_t* label;
    unsigned short vk;
    wchar_t glyph = 0;
    bool hasIcon = false;
    Button button{nullptr};
    event_token click{};
    Windows::Foundation::IInspectable pointerPressed{nullptr};
    Windows::Foundation::IInspectable pointerCanceled{nullptr};
};
constexpr WORD kSpecialShortcutKey = 0;
constexpr ULONGLONG kFooterHoldMs = 500;
WORD g_heldFooterKey;
ULONGLONG g_footerPressedAt;
FooterKey g_footerKeys[] = {
    // Positions come from kTopRow / kBottomRow in RefreshRow.
    {L"Win", VK_LWIN},
    {L"Tab", VK_TAB, L'\uE7FD'},
    {L"Alt", VK_MENU},
    {L"Menu", VK_APPS, L'\uE700'},
    {L"Esc", VK_ESCAPE},
    {L"Del", VK_DELETE},
    {L"Select all", 'A', L'\uE73A'},
    {L"Copy", 'C', L'\uE8C8'},
    {L"Paste", 'V', L'\uE77F'},
    {L"\u2605", kSpecialShortcutKey, L'\uE734'},
    {L"Print Screen", VK_SNAPSHOT, L'\uE722'},
    {L"\u00AB", VK_BROWSER_BACK},
    {L"\u00BB", VK_BROWSER_FORWARD},
    {L"Home", VK_HOME},
    {L"End", VK_END},
    {L"\u2191", VK_UP},
    {L"\u2190", VK_LEFT},
    {L"\u2193", VK_DOWN},
    {L"\u2192", VK_RIGHT},
};
RowDefinition g_footer{nullptr};
GridLength g_stockFooterHeight{};
double g_stockFooterMin;
double g_stockFooterMax;
double g_appliedFooterHeight;
double g_knownStockFooter;  // stock footer height from the first insert

FrameworkElement FindNamed(const DependencyObject& root, std::wstring_view name) {
    TRACE_SCOPE();
    if (auto element = root.try_as<FrameworkElement>(); element && element.Name() == name)
        return element;
    const int count = VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < count; ++i) {
        TRACE_BLOCK();
        if (auto element = FindNamed(VisualTreeHelper::GetChild(root, i), name)) return element;
    }
    return nullptr;
}

winrt::Windows::Foundation::IInspectable CurrentKeyboardControl() {
    try {
        const auto window = Window::Current();
        const auto content = window ? window.Content() : nullptr;
        if (const auto control = content ? FindNamed(content, L"InputPanelControl") : nullptr) {
            return control;
        }
    } catch (const hresult_error&) {
        return {};
    }
    return {};
}

static bool ReadBubbleBrush(const Media::Brush& brush, Windows::UI::Color& color) {
    if (const auto solid = brush.try_as<Media::SolidColorBrush>()) color = solid.Color();
    else if (const auto acrylic = brush.try_as<Media::AcrylicBrush>()) color = acrylic.FallbackColor();
    else return false;
    return color.A >= 128;
}

bool GetCloseButtonCenter(POINT& point) {
    try {
        const auto window = Window::Current();
        if (!window) return false;
        const auto content = window.Content();
        if (!content) return false;
        const auto button = FindNamed(content, L"TEMPLATE_PART_GripperCloseButton");
        if (!button || button.ActualWidth() <= 0 || button.ActualHeight() <= 0) return false;
        const auto center = button.TransformToVisual(nullptr).TransformPoint(
            {static_cast<float>(button.ActualWidth() / 2), static_cast<float>(button.ActualHeight() / 2)});
        const HWND hwnd = GetCoreWindowHandle(window);
        const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
        if (!dpi || !std::isfinite(center.X) || !std::isfinite(center.Y)) return false;
        point = {std::lround(center.X * dpi / 96.0), std::lround(center.Y * dpi / 96.0)};
        if (!ClientToScreen(hwnd, &point)) return false;
        VLOG(L"Keyboard close button center (%ld,%ld)", point.x, point.y);
        return true;
    } catch (const hresult_error&) {
        return false;
    }
}

bool IsTouchKeyboardVisibleOnCurrentThread() {
    try {
        const auto window = Window::Current();
        if (!window || !window.Visible()) return false;
        const auto content = window.Content();
        if (!content) return false;
        const auto keyboard = FindNamed(content, L"TEMPLATE_PART_KeyboardGrid");
        const auto keys = keyboard ? FindNamed(keyboard, L"TEMPLATE_PART_KeyGrid") : nullptr;
        if (!keys || keys.ActualWidth() <= 0 || keys.ActualHeight() <= 0) return false;
        for (DependencyObject element = keys; element; element = VisualTreeHelper::GetParent(element)) {
            const auto visual = element.try_as<UIElement>();
            if (visual && (visual.Visibility() != Visibility::Visible || visual.Opacity() <= 0)) return false;
        }
        return true;
    } catch (const hresult_error&) {
        return false;
    }
}

Border g_enterFace{nullptr};
TextBlock g_enterLabel{nullptr};
Windows::Foundation::IInspectable g_enterOriginalForeground{nullptr};
Windows::Foundation::IInspectable g_enterOriginalBackground{nullptr};
Media::SolidColorBrush g_enterAccent{nullptr};

void RestoreEnterFace() {
    if (!g_enterFace) return;
    const auto property = Border::BackgroundProperty();
    if (g_enterFace.ReadLocalValue(property) == g_enterAccent) {
        if (g_enterOriginalBackground == DependencyProperty::UnsetValue())
            g_enterFace.ClearValue(property);
        else
            g_enterFace.SetValue(property, g_enterOriginalBackground);
    }
    if (g_enterLabel) {
        const auto foreground = TextBlock::ForegroundProperty();
        if (g_enterLabel.ReadLocalValue(foreground) == g_primedLabel) {
            if (g_enterOriginalForeground == DependencyProperty::UnsetValue())
                g_enterLabel.ClearValue(foreground);
            else
                g_enterLabel.SetValue(foreground, g_enterOriginalForeground);
        }
    }
    g_enterLabel = nullptr;
    g_enterOriginalForeground = nullptr;
    g_enterFace = nullptr;
    g_enterOriginalBackground = nullptr;
    g_enterAccent = nullptr;
}

void ColorNativeEnter(const Grid& keys) {
    Border face{nullptr};
    TextBlock enterLabel{nullptr};
    for (const auto& child : keys.Children()) {
        const auto key = child.try_as<FrameworkElement>();
        if (!key || key.Visibility() != Visibility::Visible) continue;
        const auto label = FindNamed(key, L"TEMPLATE_PART_KeyLabel").try_as<TextBlock>();
        if (!label || label.Text() != L"\uE751") continue;
        face = FindNamed(key, L"TEMPLATE_PART_KeyBackgroundBorder").try_as<Border>();
        enterLabel = label;
        break;
    }
    if (g_enterFace == face && g_enterLabel == enterLabel) {
        if (face && face.ReadLocalValue(Border::BackgroundProperty()) != g_enterAccent) {
            g_enterOriginalBackground = face.ReadLocalValue(Border::BackgroundProperty());
            face.Background(g_enterAccent);
        }
        if (face && enterLabel && enterLabel.ReadLocalValue(TextBlock::ForegroundProperty()) != g_primedLabel) {
            g_enterOriginalForeground = enterLabel.ReadLocalValue(TextBlock::ForegroundProperty());
            enterLabel.Foreground(g_primedLabel);
        }
        return;
    }
    RestoreEnterFace();
    if (!face) return;
    if (!g_primedFace) return;
    g_enterAccent = g_primedFace;
    g_enterOriginalBackground = face.ReadLocalValue(Border::BackgroundProperty());
    g_enterFace = face;
    g_enterLabel = enterLabel;
    g_enterOriginalForeground = enterLabel.ReadLocalValue(TextBlock::ForegroundProperty());
    face.Background(g_enterAccent);
    enterLabel.Foreground(g_primedLabel);
}

// Hide voice typing: the native keyboard recycles key elements across
// layouts (letters, numbers, symbols) and caches their layout properties, so
// Margin/Width/Visibility edits leak into other views. Only render-time
// properties the keyboard never sets are touched here, each change is undone
// by identity, and the edit is re-validated against the live row on every
// refresh.
struct RenderEdit {
    UIElement element{nullptr};
    DependencyProperty property{nullptr};
    Windows::Foundation::IInspectable original{nullptr};
    Windows::Foundation::IInspectable applied{nullptr};
};
std::vector<RenderEdit> g_bottomRowEdits;
FrameworkElement g_hiddenMic{nullptr};
std::vector<FrameworkElement> g_editedRow;

static void ApplyRenderEdit(const UIElement& element, const DependencyProperty& property,
                            const Windows::Foundation::IInspectable& value) {
    g_bottomRowEdits.push_back({element, property, element.ReadLocalValue(property), value});
    element.SetValue(property, value);
}

void RestoreNativeBottomRow() {
    for (auto it = g_bottomRowEdits.rbegin(); it != g_bottomRowEdits.rend(); ++it) {
        if (it->element.ReadLocalValue(it->property) != it->applied) continue;
        if (it->original == DependencyProperty::UnsetValue()) it->element.ClearValue(it->property);
        else it->element.SetValue(it->property, it->original);
    }
    g_bottomRowEdits.clear();
    g_editedRow.clear();
    g_hiddenMic = nullptr;
}

static std::vector<FrameworkElement> NativeBottomRow(const Grid& keys) {
    std::vector<FrameworkElement> row;
    double bottomY = -1;
    for (const auto& item : keys.Children()) {
        const auto key = item.try_as<FrameworkElement>();
        if (!key || key.Visibility() != Visibility::Visible) continue;
        const double y = key.Margin().Top;
        if (y > bottomY + 0.5) {
            row.clear();
            bottomY = y;
        }
        if (std::abs(y - bottomY) <= 0.5) row.push_back(key);
    }
    std::sort(row.begin(), row.end(), [](const auto& a, const auto& b) { return a.Margin().Left < b.Margin().Left; });
    return row;
}

void WidenNativeEnter(const Grid& keys) {
    const auto row = NativeBottomRow(keys);
    const bool hasMic = row.size() >= 3 && FindNamed(row[1], L"TEMPLATE_PART_DictationKeyControl");
    const auto enterLabel = row.size() >= 3 ? FindNamed(row.back(), L"TEMPLATE_PART_KeyLabel").try_as<TextBlock>() : nullptr;
    const bool eligible = hasMic && enterLabel && enterLabel.Text() == L"";
    // Keep a valid edit; drop it the moment the row is no longer the one edited.
    if (g_hiddenMic) {
        const bool current = eligible && row == g_editedRow &&
            std::all_of(g_bottomRowEdits.begin(), g_bottomRowEdits.end(), [](const RenderEdit& edit) {
                return edit.element.ReadLocalValue(edit.property) == edit.applied;
            });
        if (current) return;
        RestoreNativeBottomRow();
    }
    if (!eligible) return;

    const auto mic = row[1];
    const double shift = row[2].Margin().Left - mic.Margin().Left;
    if (!std::isfinite(shift) || shift <= 0 || std::abs(shift - mic.ActualWidth()) > 2) return;
    const auto& enter = row.back();
    const auto enterFace = FindNamed(enter, L"TEMPLATE_PART_KeyBackgroundBorder");
    const double faceWidth = enterFace ? enterFace.ActualWidth() : 0;
    if (!enterFace || !std::isfinite(faceWidth) || faceWidth <= 0) return;

    ApplyRenderEdit(mic, UIElement::OpacityProperty(), box_value(0.0));
    ApplyRenderEdit(mic, UIElement::IsHitTestVisibleProperty(), box_value(false));
    // Slide the keys between the microphone and Enter one slot left.
    for (size_t i = 2; i + 1 < row.size(); ++i) {
        Media::TranslateTransform move;
        move.X(-shift);
        ApplyRenderEdit(row[i], UIElement::RenderTransformProperty(), move);
    }
    // Grow Enter's face leftward into the freed slot; keep its label centred.
    Media::ScaleTransform grow;
    grow.ScaleX((faceWidth + shift) / faceWidth);
    ApplyRenderEdit(enterFace, UIElement::RenderTransformOriginProperty(),
                    box_value(Windows::Foundation::Point{1, 0.5f}));
    ApplyRenderEdit(enterFace, UIElement::RenderTransformProperty(), grow);
    Media::TranslateTransform center;
    center.X(-shift / 2);
    ApplyRenderEdit(enterLabel, UIElement::RenderTransformProperty(), center);
    g_hiddenMic = mic;
    g_editedRow = row;
}

struct ModifierSlide {
    WORD modifier{};
    uint32_t pointerId{};
    ULONGLONG started{};
};
ModifierSlide g_modifierSlide;

Canvas g_modifierTrailCanvas{nullptr};
Shapes::Line g_modifierTrail{nullptr};

static void ClearModifierTrail() {
    if (g_panel && g_modifierTrailCanvas) {
        uint32_t index;
        if (g_panel.Children().IndexOf(g_modifierTrailCanvas, index)) g_panel.Children().RemoveAt(index);
    }
    g_modifierTrail = nullptr;
    g_modifierTrailCanvas = nullptr;
}

constexpr auto kModifierSlideEvents = 4;
const std::array<RoutedEvent, kModifierSlideEvents>& ModifierSlideEvents() {
    static const std::array<RoutedEvent, kModifierSlideEvents> events{
        UIElement::PointerPressedEvent(), UIElement::PointerReleasedEvent(), UIElement::PointerCanceledEvent(),
        UIElement::PointerMovedEvent()};
    return events;
}

using ModifierSlideHandlers = std::array<Windows::Foundation::IInspectable, kModifierSlideEvents>;
ModifierSlideHandlers g_ctrlSlideHandlers, g_winSlideHandlers, g_altSlideHandlers;

static WORD SlideDestination(const Input::PointerRoutedEventArgs& args) {
    if (g_strip && g_strip.IsHitTestVisible()) {
        for (const auto& key : g_footerKeys) {
            if (key.vk != VK_TAB || !key.button || key.button.Visibility() != Visibility::Visible) continue;
            const auto point = args.GetCurrentPoint(key.button).Position();
            if (point.X >= 0 && point.X < key.button.ActualWidth() &&
                point.Y >= 0 && point.Y < key.button.ActualHeight()) return VK_TAB;
            break;
        }
    }
    if (!g_keyboard) return 0;
    const auto keys = FindNamed(g_keyboard, L"TEMPLATE_PART_KeyGrid").try_as<Grid>();
    if (!keys) return 0;
    const auto point = args.GetCurrentPoint(keys).Position();
    for (const auto& child : keys.Children()) {
        const auto key = child.try_as<FrameworkElement>();
        if (!key || key.Visibility() != Visibility::Visible) continue;
        const auto face = FindNamed(key, L"TEMPLATE_PART_KeyBackgroundBorder");
        if (!face) continue;
        const auto bounds = face.TransformToVisual(keys).TransformBounds(
            {0, 0, static_cast<float>(face.ActualWidth()), static_cast<float>(face.ActualHeight())});
        if (point.X < bounds.X || point.X >= bounds.X + bounds.Width ||
            point.Y < bounds.Y || point.Y >= bounds.Y + bounds.Height) continue;
        const auto label = FindNamed(key, L"TEMPLATE_PART_KeyLabel").try_as<TextBlock>();
        if (!label) return 0;
        const auto text = label.Text();
        if (text.size() != 1) return 0;
        wchar_t ch = text[0];
        if (ch >= L'a' && ch <= L'z') ch -= L'a' - L'A';
        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')) return static_cast<WORD>(ch);
        return 0;
    }
    return 0;
}

static void AttachModifierSlide(const Button& button, WORD modifier, ModifierSlideHandlers& handlers) {
    button.IsHoldingEnabled(false);
    button.IsRightTapEnabled(false);
    handlers = {
        box_value(Input::PointerEventHandler([modifier](const auto& sender, const auto& args) {
            if (args.Pointer().PointerDeviceType() != Windows::Devices::Input::PointerDeviceType::Touch) return;
            const auto source = sender.template as<UIElement>();
            source.CapturePointer(args.Pointer());
            g_modifierSlide = {modifier, args.Pointer().PointerId(), GetTickCount64()};
            NotifyModifierReleased();
            ClearModifierTrail();
            if (!g_panel || !g_primedFace) return;
            Canvas canvas;
            canvas.IsHitTestVisible(false);
            Canvas::SetZIndex(canvas, 1001);
            Grid::SetRowSpan(canvas, std::max(1, static_cast<int>(g_panel.RowDefinitions().Size())));
            Grid::SetColumnSpan(canvas, std::max(1, static_cast<int>(g_panel.ColumnDefinitions().Size())));
            const auto start = args.GetCurrentPoint(g_panel).Position();
            Shapes::Line trail;
            trail.Stroke(g_primedFace);
            trail.StrokeThickness(3);
            trail.StrokeStartLineCap(Media::PenLineCap::Round);
            trail.StrokeEndLineCap(Media::PenLineCap::Round);
            trail.X1(start.X);
            trail.Y1(start.Y);
            trail.X2(start.X);
            trail.Y2(start.Y);
            canvas.Children().Append(trail);
            g_panel.Children().Append(canvas);
            g_modifierTrailCanvas = canvas;
            g_modifierTrail = trail;
        })),
        box_value(Input::PointerEventHandler([modifier](auto&&, const auto& args) {
            if (g_modifierSlide.modifier != modifier || g_modifierSlide.pointerId != args.Pointer().PointerId()) return;
            const auto gesture = std::exchange(g_modifierSlide, {});
            ClearModifierTrail();
            NotifyModifierReleased();
            if (!FooterKeys::g_enabled || !CompactModeActive() ||
                (modifier == VK_LWIN && GetTickCount64() - gesture.started < kFooterHoldMs)) return;
            try {
                if (const WORD destination = SlideDestination(args)) {
                    args.Handled(true);
                    FooterKeys::SendFooterChord({{modifier, destination}, 2});
                }
            } catch (const hresult_error&) {
                VLOG(L"Compact keys: could not resolve modifier slide destination");
            }
        })),
        box_value(Input::PointerEventHandler([modifier](auto&&, const auto& args) {
            if (g_modifierSlide.modifier == modifier && g_modifierSlide.pointerId == args.Pointer().PointerId()) {
                g_modifierSlide = {};
                ClearModifierTrail();
                NotifyModifierReleased();
            }
        })),
        box_value(Input::PointerEventHandler([modifier](auto&&, const auto& args) {
            if (!g_modifierTrail || g_modifierSlide.modifier != modifier ||
                g_modifierSlide.pointerId != args.Pointer().PointerId()) return;
            const auto point = args.GetCurrentPoint(g_modifierTrailCanvas).Position();
            g_modifierTrail.X2(point.X);
            g_modifierTrail.Y2(point.Y);
        })),
    };
    const auto& events = ModifierSlideEvents();
    for (size_t i = 0; i < events.size(); ++i) button.AddHandler(events[i], handlers[i], true);
}

static void DetachModifierSlide(const Button& button, ModifierSlideHandlers& handlers) {
    const auto& events = ModifierSlideEvents();
    for (size_t i = 0; i < events.size(); ++i) {
        if (handlers[i]) button.RemoveHandler(events[i], handlers[i]);
    }
    handlers = {};
}

void RemoveRow() {
    TRACE_SCOPE();
    StopArrowRepeat(g_uiWindow);
    RestoreEnterFace();
    RestoreNativeBottomRow();
    if (!g_strip) return;
    g_rowNeedsRemoval = true;
    FooterKeys::ClearAdapter();
    g_modifierSlide = {};
    ClearModifierTrail();
    if (g_button) {
        TRACE_BLOCK();
        DetachModifierSlide(g_button, g_ctrlSlideHandlers);
        if (g_soundPressed) g_button.RemoveHandler(UIElement::PointerPressedEvent(), g_soundPressed);
        g_button.Click(g_click);
        g_button = nullptr;
    }
    for (auto& key : g_footerKeys) {
        TRACE_BLOCK();
        if (key.button) {
            if (key.vk == VK_LWIN) DetachModifierSlide(key.button, g_winSlideHandlers);
            if (key.vk == VK_MENU) DetachModifierSlide(key.button, g_altSlideHandlers);
            key.button.Click(key.click);
            if (g_soundPressed) key.button.RemoveHandler(UIElement::PointerPressedEvent(), g_soundPressed);
            if (key.pointerPressed) {
                key.button.RemoveHandler(UIElement::PointerPressedEvent(), key.pointerPressed);
                key.pointerPressed = nullptr;
            }
            if (key.pointerCanceled) {
                if (key.vk >= VK_LEFT && key.vk <= VK_DOWN) {
                    key.button.RemoveHandler(UIElement::PointerReleasedEvent(), key.pointerCanceled);
                    key.button.RemoveHandler(UIElement::PointerCaptureLostEvent(), key.pointerCanceled);
                }
                key.button.RemoveHandler(UIElement::PointerCanceledEvent(), key.pointerCanceled);
                key.pointerCanceled = nullptr;
            }
        }
        key.button = nullptr;
    }
    g_heldFooterKey = 0;
    uint32_t index = 0;
    if (g_panel && g_panel.Children().IndexOf(g_strip, index)) {
        g_panel.Children().RemoveAt(index);
    }
    if (g_footer) {
        TRACE_BLOCK();
        const auto height = g_footer.Height();
        if (g_footer.MinHeight() == g_appliedFooterHeight) g_footer.MinHeight(g_stockFooterMin);
        if (height.GridUnitType == GridUnitType::Pixel && height.Value == g_appliedFooterHeight)
            g_footer.Height(g_stockFooterHeight);
        if (g_footer.MaxHeight() == g_appliedFooterHeight) g_footer.MaxHeight(g_stockFooterMax);
        g_footer = nullptr;
    }
    g_strip = nullptr;
    g_primedFace = nullptr;
    g_soundPressed = nullptr;
    g_rowUnstableSince = 0;
    g_primedLabel = nullptr;
    g_keyboard = nullptr;
    g_panel = nullptr;
    g_rowNeedsRemoval = false;
}

Button MakeButton(const hstring& label) {
    TRACE_SCOPE();
    Button button;
    button.Content(box_value(label));
    button.IsTabStop(false);
    button.AllowFocusOnInteraction(false);
    button.MinWidth(0);
    button.MinHeight(0);
    button.Padding({0, 0, 0, 0});
    if (!g_soundPressed)
        g_soundPressed = box_value(Input::PointerEventHandler([](auto&&, auto&&) { KeySound::PlayClick(); }));
    button.AddHandler(UIElement::PointerPressedEvent(), g_soundPressed, true);
    return button;
}

bool SetFooterKeyIcon(const FooterKey& key) {
    if (!key.glyph) return false;
    const HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    PCWSTR selectedFamily = nullptr;
    for (const auto family : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}) {
        const HFONT font = CreateFont(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                     DEFAULT_PITCH, family);
        if (!font) continue;
        const auto previous = SelectObject(dc, font);
        if (previous && previous != HGDI_ERROR) {
            WCHAR face[LF_FACESIZE];
            WORD glyph = 0xFFFF;
            if (GetTextFace(dc, ARRAYSIZE(face), face) && _wcsicmp(face, family) == 0 &&
                GetGlyphIndicesW(dc, &key.glyph, 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS) == 1 &&
                glyph != 0xFFFF && glyph != 0)
                selectedFamily = family;
            SelectObject(dc, previous);
        }
        DeleteObject(font);
        if (selectedFamily) break;
    }
    DeleteDC(dc);
    if (!selectedFamily) return false;
    key.button.FontFamily(Media::FontFamily(selectedFamily));
    key.button.Content(box_value(hstring(std::wstring_view(&key.glyph, 1))));
    Automation::AutomationProperties::SetName(key.button, key.vk == kSpecialShortcutKey ? L"Custom shortcut" : key.label);
    return true;
}

ControlTemplate StockKeyTemplate(Windows::UI::Color pressed, Windows::UI::Color label) {
    TRACE_SCOPE();
    // Use the key face directly, without the standard Button's outer rim.
    try {
        std::wstring markup = LR"xaml(
        <ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                         xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="Button">
          <Grid Background="Transparent" IsHoldingEnabled="False" IsRightTapEnabled="False">
            <Grid.Resources>
                <SolidColorBrush x:Key="KeyPressedFace" Color="PRESSED_FACE"/>
                <SolidColorBrush x:Key="KeyPressedLabel" Color="PRESSED_LABEL"/>
            </Grid.Resources>
            <VisualStateManager.VisualStateGroups>
                <VisualStateGroup x:Name="CommonStates">
                    <VisualState x:Name="Normal"/>
                    <VisualState x:Name="PointerOver"/>
                    <VisualState x:Name="Pressed">
                        <VisualState.Setters>
                            <Setter Target="KeyFace.Background" Value="{StaticResource KeyPressedFace}"/>
                            <Setter Target="KeyLabel.Foreground" Value="{StaticResource KeyPressedLabel}"/>
                        </VisualState.Setters>
                    </VisualState>
                    <VisualState x:Name="Disabled"/>
                </VisualStateGroup>
            </VisualStateManager.VisualStateGroups>
            <Border x:Name="KeyFace" Background="{TemplateBinding Background}"
                    IsHitTestVisible="False"
                    BorderBrush="{TemplateBinding BorderBrush}"
                    BorderThickness="{TemplateBinding BorderThickness}"
                    CornerRadius="{TemplateBinding CornerRadius}">
                <Grid>
                    <ContentPresenter x:Name="KeyLabel" Content="{TemplateBinding Content}"
                                      FontFamily="{TemplateBinding FontFamily}" FontSize="{TemplateBinding FontSize}"
                                      Foreground="{TemplateBinding Foreground}"
                                      HorizontalAlignment="Center" VerticalAlignment="Center"/>
                </Grid>
            </Border>
          </Grid>
        </ControlTemplate>)xaml";
        const auto replaceColor = [&](std::wstring_view token, Windows::UI::Color color) {
            wchar_t hex[10];
            swprintf_s(hex, ARRAYSIZE(hex), L"#%02X%02X%02X%02X",
                       static_cast<unsigned>(color.A), static_cast<unsigned>(color.R),
                       static_cast<unsigned>(color.G), static_cast<unsigned>(color.B));
            markup.replace(markup.find(token), token.size(), hex);
        };
        replaceColor(L"PRESSED_FACE", pressed);
        replaceColor(L"PRESSED_LABEL", label);
        return Markup::XamlReader::Load(hstring(markup)).as<ControlTemplate>();
    } catch (const hresult_error& error) {
        Wh_Log(L"Compact keys: could not load stock key template (0x%08X)", error.code().value);
        return nullptr;
    }
}

static bool ReadKeyboardThemeColor(const FrameworkElement& start, PCWSTR name, Windows::UI::Color& color) {
    const auto key = box_value(hstring(name));
    const auto read = [&](const ResourceDictionary& resources) {
        return resources && resources.HasKey(key) &&
               ReadBubbleBrush(resources.Lookup(key).try_as<Media::Brush>(), color);
    };
    for (DependencyObject node = start; node; node = VisualTreeHelper::GetParent(node)) {
        const auto element = node.try_as<FrameworkElement>();
        if (element && read(element.Resources())) return true;
    }
    const auto app = Application::Current();
    return app && read(app.Resources());
}

static bool ReadConfiguredKeyboardAccent(const FrameworkElement& panel, Windows::UI::Color& color) {
    const auto path = panel.ActualTheme() == ElementTheme::Light ?
        L"Software\\Microsoft\\TabletTip\\1.7\\SelectedThemeLight" :
        L"Software\\Microsoft\\TabletTip\\1.7\\SelectedThemeDark";
    wchar_t value[40]{};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, path, L"AccentColor", RRF_RT_REG_SZ,
                     nullptr, value, &size) != ERROR_SUCCESS) return false;
    unsigned red, green, blue;
    wchar_t trailing;
    if (swscanf_s(value, L"%u,%u,%u%c", &red, &green, &blue, &trailing, 1u) != 3 ||
        red > 255 || green > 255 || blue > 255) return false;
    color = {255, static_cast<BYTE>(red), static_cast<BYTE>(green), static_cast<BYTE>(blue)};
    return true;
}

static Windows::UI::Color ContrastLabel(Windows::UI::Color face) {
    const BYTE value = 299 * face.R + 587 * face.G + 114 * face.B >= 128000 ? 0 : 255;
    return {255, value, value, value};
}

static void UpdateInjectedKeyTheme(const FrameworkElement& panel, const FrameworkElement& keys) {
    static ULONGLONG lastCheck;
    const bool firstUpdate = !g_primedFace;
    const auto now = GetTickCount64();
    if (!firstUpdate && now - lastCheck < 250) return;
    lastCheck = now;
    Windows::UI::Color primed{}, pressed{}, primedLabel{}, pressedLabel{};
    const auto nativeKey = FindNamed(keys, L"TEMPLATE_PART_KeyBackgroundBorder");
    const auto source = nativeKey ? nativeKey : panel;
    if (!ReadKeyboardThemeColor(source, L"WideNarrowKeyToggledBackgroundBrush", primed) &&
        !ReadConfiguredKeyboardAccent(panel, primed))
        primed = Windows::UI::ViewManagement::UISettings().GetColorValue(
            Windows::UI::ViewManagement::UIColorType::AccentLight1);
    if (!ReadKeyboardThemeColor(source, L"WideNarrowKeyPressedBackgroundBrush", pressed)) pressed = primed;
    if (!ReadKeyboardThemeColor(source, L"WideNarrowKeyToggledLabelBrush", primedLabel))
        primedLabel = ContrastLabel(primed);
    if (!ReadKeyboardThemeColor(source, L"WideNarrowKeyPressedLabelBrush", pressedLabel))
        pressedLabel = ContrastLabel(pressed);

    if (!g_primedFace) g_primedFace = Media::SolidColorBrush(primed);
    else if (g_primedFace.Color() != primed) g_primedFace.Color(primed);
    if (!g_primedLabel) g_primedLabel = Media::SolidColorBrush(primedLabel);
    else if (g_primedLabel.Color() != primedLabel) g_primedLabel.Color(primedLabel);

    static Windows::UI::Color lastPressed{}, lastLabel{};
    if (firstUpdate || lastPressed != pressed || lastLabel != pressedLabel) {
        if (const auto keyTemplate = StockKeyTemplate(pressed, pressedLabel)) {
            g_button.Template(keyTemplate);
            for (const auto& key : g_footerKeys) key.button.Template(keyTemplate);
            lastPressed = pressed;
            lastLabel = pressedLabel;
        }
    }
}

// Pick the face whose resting style the injected keys copy. A hovered or
// pressed key swaps its background brush, so copying one live key spreads its
// hover state. Vote instead: the most common background is the letter style,
// the next most common is the function-key style (Shift, Backspace, &123...).
// A single hovered key forms its own minority and never wins. The accent
// Enter is skipped because this mod recolors it.
static Border FunctionKeyFace(const Grid& keys, const Border& fallback) {
    struct Style { UINT32 color; Border face; int votes; };
    std::vector<Style> styles;
    for (const auto& child : keys.Children()) {
        const auto key = child.try_as<FrameworkElement>();
        if (!key || key.Visibility() != Visibility::Visible) continue;
        const auto face = FindNamed(key, L"TEMPLATE_PART_KeyBackgroundBorder").try_as<Border>();
        if (!face || face == g_enterFace) continue;
        Windows::UI::Color color{};
        const auto brush = face.Background();
        if (const auto solid = brush.try_as<Media::SolidColorBrush>()) color = solid.Color();
        else if (const auto acrylic = brush.try_as<Media::AcrylicBrush>()) color = acrylic.FallbackColor();
        else continue;
        const UINT32 packed = (UINT32{color.A} << 24) | (color.R << 16) | (color.G << 8) | color.B;
        auto it = std::find_if(styles.begin(), styles.end(), [&](const Style& s) { return s.color == packed; });
        if (it == styles.end()) styles.push_back({packed, face, 1});
        else ++it->votes;
    }
    std::stable_sort(styles.begin(), styles.end(), [](const Style& a, const Style& b) { return a.votes > b.votes; });
    if (styles.size() >= 2 && styles[1].votes >= 2) return styles[1].face;
    return fallback;
}

void MatchKeyFace(const Button& button, const Border& face, bool primed) {
    TRACE_SCOPE();
    if (!face) return;
    if (primed && g_primedFace) {
        if (button.Background() != g_primedFace) button.Background(g_primedFace);
        if (button.Foreground() != g_primedLabel) button.Foreground(g_primedLabel);
    } else {
        if (button.Background() != face.Background()) button.Background(face.Background());
        if (g_primedLabel && button.Foreground() == g_primedLabel) button.ClearValue(Control::ForegroundProperty());
    }
    if (button.BorderBrush() != face.BorderBrush()) button.BorderBrush(face.BorderBrush());
    if (button.BorderThickness() != face.BorderThickness()) button.BorderThickness(face.BorderThickness());
    if (button.CornerRadius() != face.CornerRadius()) button.CornerRadius(face.CornerRadius());
}

struct NativeKeyRow {
    Windows::Foundation::Rect first{};
    double right = 0;
    double bottom = 0;
    double precedingBottom = std::numeric_limits<double>::lowest();
    Border face{nullptr};
    // The most common key face width. The bottom row starts with a wider
    // mode key in some layouts (abc, &123), so it cannot be the reference.
    double keyWidth = 0;
};

NativeKeyRow MeasureBottomRow(const Grid& keys, const UIElement& relativeTo) {
    TRACE_SCOPE();
    NativeKeyRow row;
    std::vector<long> widths;
    for (const auto& child : keys.Children()) {
        TRACE_BLOCK();
        const auto key = child.try_as<FrameworkElement>();
        if (!key || key.Visibility() != Visibility::Visible ||
            key.ActualWidth() <= 0 || key.ActualHeight() <= 0) continue;
        auto face = FindNamed(key, L"TEMPLATE_PART_KeyBackgroundBorder");
        if (!face || face.ActualWidth() <= 0 || face.ActualHeight() <= 0) face = key;
        const auto bounds = face.TransformToVisual(relativeTo).TransformBounds(
            {0, 0, static_cast<float>(face.ActualWidth()), static_cast<float>(face.ActualHeight())});
        const double keyBottom = bounds.Y + bounds.Height;
        const double keyRight = bounds.X + bounds.Width;
        if (bounds.Width > 0) widths.push_back(std::lround(bounds.Width * 2));
        if (!row.first.Width || keyBottom > row.bottom + 2) {
            TRACE_BLOCK();
            if (row.first.Width) row.precedingBottom = row.bottom;
            row.first = bounds;
            row.face = face.try_as<Border>();
            row.right = keyRight;
            row.bottom = keyBottom;
        } else if (keyBottom >= row.bottom - 2) {
            TRACE_BLOCK();
            if (bounds.X < row.first.X) {
                row.first = bounds;
                row.face = face.try_as<Border>();
            }
            row.right = std::max(row.right, keyRight);
            row.bottom = std::max(row.bottom, keyBottom);
        } else {
            TRACE_BLOCK();
            row.precedingBottom = std::max(row.precedingBottom, keyBottom);
        }
    }
    std::sort(widths.begin(), widths.end());
    size_t best = 0;
    for (size_t i = 0, j; i < widths.size(); i = j) {
        for (j = i; j < widths.size() && widths[j] - widths[i] <= 2; ++j) {}
        if (j - i > best) {
            best = j - i;
            row.keyWidth = widths[i] / 2.0;
        }
    }
    return row;
}

// Layout switches (&123 / abc) rebuild the native keys, and for a frame or two
// the tree fails the checks below. Once the row exists, a failure must persist
// for kRowGraceMs before the row is hidden or removed; until then it is left
// exactly as it is. A one-shot timer re-checks when the grace period ends.
constexpr ULONGLONG kRowGraceMs = 400;
constexpr UINT_PTR kGraceTimer = 3;

// Logs why the row is missing, once per change of reason.
static void RowState(PCWSTR format, ...) {
    static std::wstring last;
    wchar_t text[400];
    va_list args;
    va_start(args, format);
    vswprintf_s(text, format, args);
    va_end(args);
    if (last == text) return;
    last = text;
    Wh_Log(L"Compact row state: %s", text);
}

static bool HoldRowThroughGlitch(HWND hwnd) {
    if (!g_button || g_stopping) return false;
    const auto now = GetTickCount64();
    if (!g_rowUnstableSince) {
        g_rowUnstableSince = now;
        SetTimer(hwnd, kGraceTimer, static_cast<UINT>(kRowGraceMs + 50), nullptr);
    }
    return now - g_rowUnstableSince < kRowGraceMs;
}

void RefreshRow(HWND hwnd) {
    TRACE_SCOPE();
    const auto window = Window::Current();
    const auto content = window ? window.Content() : nullptr;
    const auto root = content ? content.try_as<FrameworkElement>() : nullptr;
    if (root != g_layoutRoot) {
        TRACE_BLOCK();
        if (g_layoutRoot) {
            TRACE_BLOCK();
            g_layoutRoot.LayoutUpdated(g_layoutUpdated);
            g_layoutRoot = nullptr;
        }
        if (root) {
            TRACE_BLOCK();
            g_layoutUpdated = root.LayoutUpdated([hwnd](auto&&, auto&&) {
                TraceScope traceScope("LayoutUpdated callback", __LINE__);
                if (!g_stopping && !g_refreshQueued) g_refreshQueued = PostMessage(hwnd, kRefresh, 0, 0);
            });
            g_layoutRoot = root;
        }
    }
    if (g_rowNeedsRemoval) RemoveRow();
    auto keyboard = content ? FindNamed(content, L"TEMPLATE_PART_KeyboardGrid") : nullptr;
    auto keys = keyboard ? FindNamed(keyboard, L"TEMPLATE_PART_KeyGrid").try_as<Grid>() : nullptr;
    auto panel = keyboard ? VisualTreeHelper::GetParent(keyboard).try_as<Grid>() : nullptr;
    // The compact tree contains 35 native keys and shares its template with
    // the other layouts. Allow a small range for compact language variants.
    const auto count = keys ? keys.Children().Size() : 0;
    bool visible = window && window.Visible() && count && keys.ActualWidth() > 0 && keys.ActualHeight() > 0;
    for (DependencyObject element = keys; visible && element; element = VisualTreeHelper::GetParent(element)) {
        if (auto ui = element.try_as<UIElement>())
            visible = ui.Visibility() == Visibility::Visible && ui.Opacity() > 0;
    }
    PublishPanelVisibility(visible);
    constexpr int footerIndex = 7;
    const bool compact = visible && panel && panel.Name() == L"TEMPLATE_PART_InputPanelGrid" &&
        count >= 33 && count <= 38 && Grid::GetRow(keyboard) == footerIndex - 1 &&
        Grid::GetRowSpan(keyboard) == 1 && panel.RowDefinitions().Size() == 9;
    // The rows live inside the keyboard, so a hidden or mid-rebuild keyboard
    // (empty or zero-size key grid) needs nothing done. Only a visible
    // non-compact layout means the mode changed and the rows must go.
    if (!visible && g_button) {
        PublishCompactMode(false);
        if (g_repeatingArrow) StopArrowRepeat(hwnd);
        return;
    }
    if (!compact)
        RowState(L"not compact (visible=%d panel=%s count=%u kbRow=%d span=%d rows=%u)", visible,
                 panel ? panel.Name().c_str() : L"-", count, keyboard ? Grid::GetRow(keyboard) : -1,
                 keyboard ? Grid::GetRowSpan(keyboard) : -1, panel ? panel.RowDefinitions().Size() : 0u);
    if (!compact && HoldRowThroughGlitch(hwnd)) return;
    PublishCompactMode(compact);
    if (!compact) {
        FooterKeys::t_shortcut = {};
        g_swipeKeyWidth.store(0, std::memory_order_relaxed);
        RemoveRow();
    }
    if (!compact) return;
    float swipeKeyWidth = 0;
    if (visible && ReadSettings().customSwipeStartDistance) {
        for (const auto& child : keys.Children()) {
            const auto key = child.try_as<FrameworkElement>();
            if (!key || key.Visibility() != Visibility::Visible) continue;
            const auto face = FindNamed(key, L"TEMPLATE_PART_KeyBackgroundBorder");
            const double width = face ? face.ActualWidth() : key.ActualWidth();
            if (!std::isfinite(width) || width <= 0) continue;
            swipeKeyWidth = static_cast<float>(width);
            break;
        }
    }
    g_swipeKeyWidth.store(swipeKeyWidth, std::memory_order_relaxed);
    if (!visible && g_repeatingArrow) StopArrowRepeat(hwnd);
    const double reserve = g_footerReserveDip.load();
    // Views inside the compact keyboard (emoji, clipboard) change its native
    // geometry, so the size hook stops reserving room for the rows. The
    // keyboard is still compact: park the row (hidden, footer at stock
    // height) instead of tearing it down; the normal path below restores it
    // as soon as the reserve returns.
    if (reserve <= 0 && g_button && g_footer && FooterKeys::g_enabled) {
        RowState(L"parked (no footer reserve in this view)");
        if (g_strip.IsHitTestVisible()) g_strip.IsHitTestVisible(false);
        if (g_strip.Opacity() != 0) g_strip.Opacity(0);
        if (g_footer.Height().Value != g_stockFooterHeight.Value) g_footer.Height(g_stockFooterHeight);
        if (g_footer.MinHeight() != g_stockFooterMin) g_footer.MinHeight(g_stockFooterMin);
        if (g_footer.MaxHeight() != g_stockFooterMax) g_footer.MaxHeight(g_stockFooterMax);
        g_appliedFooterHeight = g_stockFooterHeight.Value;
        return;
    }
    if (!FooterKeys::g_enabled || !root || !panel || reserve <= 0 ||
        !std::isfinite(keyboard.ActualWidth()) || keyboard.ActualWidth() <= 0 ||
        !std::isfinite(keyboard.ActualHeight()) || keyboard.ActualHeight() <= 16 ||
        keyboard.Visibility() != Visibility::Visible) {
        TRACE_BLOCK();
        RowState(L"not ready (enabled=%d reserve=%.1f kb=%.1fx%.1f vis=%d)", FooterKeys::g_enabled.load(), reserve,
                 keyboard.ActualWidth(), keyboard.ActualHeight(), keyboard.Visibility() == Visibility::Visible);
        if (!HoldRowThroughGlitch(hwnd)) RemoveRow();
        return;
    }
    // The compact keyboard occupies row 6. Its background and stroke
    // both span the fixed 8-DIP spacer immediately below it.
    const auto rows = panel.RowDefinitions();
    if (Grid::GetRow(keyboard) != footerIndex - 1 || Grid::GetRowSpan(keyboard) != 1 || rows.Size() != 9) {
        TRACE_BLOCK();
        RowState(L"unexpected rows (kbRow=%d span=%d rows=%u)", Grid::GetRow(keyboard), Grid::GetRowSpan(keyboard), rows.Size());
        if (!HoldRowThroughGlitch(hwnd)) RemoveRow();
        return;
    }
    const auto footer = rows.GetAt(footerIndex);
    if (g_button && (keyboard != g_keyboard || panel != g_panel || footer != g_footer)) {
        RowState(L"native elements replaced (keyboard=%d panel=%d footer=%d)", keyboard != g_keyboard,
                 panel != g_panel, footer != g_footer);
        RemoveRow();
    }
    if (!g_button) {
        TRACE_BLOCK();
        // A footer still at a height this mod applied (the row was removed
        // while expanded) is reset to the stock height seen at first insert.
        if (g_knownStockFooter > 0) {
            const auto current = footer.Height();
            if (current.GridUnitType == GridUnitType::Pixel && current.Value > 16 &&
                footer.MinHeight() == current.Value && footer.MaxHeight() == current.Value) {
                RowState(L"reset leftover footer %.1f -> %.1f", current.Value, g_knownStockFooter);
                footer.MinHeight(g_knownStockFooter);
                footer.Height({g_knownStockFooter, GridUnitType::Pixel});
                footer.MaxHeight(g_knownStockFooter);
            }
        }
        const auto stockHeight = footer.Height();
        const auto minimum = footer.MinHeight();
        const auto maximum = footer.MaxHeight();
        const auto background = FindNamed(panel, L"TEMPLATE_PART_InputPanelBackground");
        const auto stroke = FindNamed(panel, L"TEMPLATE_PART_InputPanelStrokeBorder");
        if (stockHeight.GridUnitType != GridUnitType::Pixel || stockHeight.Value <= 0 || stockHeight.Value > 16 ||
            minimum != stockHeight.Value || maximum != stockHeight.Value ||
            !background || Grid::GetRow(background) + Grid::GetRowSpan(background) != footerIndex + 1 ||
            !stroke || Grid::GetRow(stroke) + Grid::GetRowSpan(stroke) != footerIndex + 1) {
            RowState(L"footer not stock (height=%.1f unit=%d min=%.1f max=%.1f bg=%d stroke=%d)", stockHeight.Value,
                     static_cast<int>(stockHeight.GridUnitType), minimum, maximum,
                     background ? Grid::GetRow(background) + Grid::GetRowSpan(background) : -1,
                     stroke ? Grid::GetRow(stroke) + Grid::GetRowSpan(stroke) : -1);
            return;
        }
        auto button = MakeButton(L"Ctrl");
        AttachModifierSlide(button, VK_CONTROL, g_ctrlSlideHandlers);
        const auto click = button.Click([hwnd](auto&&, auto&&) {
            TraceScope traceScope("Ctrl Click callback", __LINE__);
            if (FooterKeys::g_enabled && CompactModeActive()) FooterKeys::ToggleModifier(VK_CONTROL);
            if (!g_stopping && !g_refreshQueued) g_refreshQueued = PostMessage(hwnd, kRefresh, 0, 0);
        });

        Canvas strip;
        strip.Opacity(0);
        strip.IsHitTestVisible(false);
        auto footerKeys = std::to_array(g_footerKeys);
        Grid::SetRow(strip, footerIndex);
        Grid::SetColumnSpan(strip, std::max(1, static_cast<int>(panel.ColumnDefinitions().Size())));
        strip.Children().Append(button);
        for (auto& key : footerKeys) {
            TRACE_BLOCK();
            const bool clipboardKey = key.vk == 'A' || key.vk == 'C' || key.vk == 'V';
            key.button = MakeButton(key.label);
            if (key.vk == VK_BROWSER_BACK || key.vk == VK_BROWSER_FORWARD)
                Automation::AutomationProperties::SetName(key.button, key.vk == VK_BROWSER_BACK ? L"Back" : L"Forward");
            if (clipboardKey) key.button.ClickMode(ClickMode::Press);
            key.hasIcon = SetFooterKeyIcon(key);
            if (key.vk == VK_LWIN) {
                // The native Win key uses U+E782 from TextInputHost's package.
                key.button.FontFamily(Media::FontFamily(
                    L"ms-resource:///Files/InputApp/Assets/Fonts/EmojiSIPIcons.ttf#Emoji SIP Fluent Icons,"
                    L"/InputApp/Assets/Fonts/EmojiSIPIcons.ttf#Emoji SIP Fluent Icons"));
                key.button.Content(box_value(hstring(L"\uE782")));
                Automation::AutomationProperties::SetName(key.button, L"Windows");
                key.hasIcon = true;
            }
            if (key.vk >= VK_LEFT && key.vk <= VK_DOWN) {
                key.button.ClickMode(ClickMode::Press);
                key.button.IsHoldingEnabled(false);
                key.pointerPressed = box_value(Input::PointerEventHandler([hwnd, vk = key.vk](auto&&, const auto& args) {
                    if (!args.GetCurrentPoint(nullptr).Properties().IsLeftButtonPressed()) return;
                    UINT delay = 1;
                    SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delay, 0);
                    g_repeatingArrow = vk;
                    g_arrowRepeatStarted = false;
                    SetTimer(hwnd, kArrowRepeat, (delay + 1) * 250, nullptr);
                }));
                key.pointerCanceled = box_value(Input::PointerEventHandler([hwnd, vk = key.vk](auto&&, auto&&) {
                    if (g_repeatingArrow == vk) StopArrowRepeat(hwnd);
                }));
                key.button.AddHandler(UIElement::PointerPressedEvent(), key.pointerPressed, true);
                key.button.AddHandler(UIElement::PointerReleasedEvent(), key.pointerCanceled, true);
                key.button.AddHandler(UIElement::PointerCaptureLostEvent(), key.pointerCanceled, true);
                key.button.AddHandler(UIElement::PointerCanceledEvent(), key.pointerCanceled, true);
            } else if (key.vk == VK_LWIN || key.vk == VK_TAB) {
                key.button.ClickMode(ClickMode::Release);
                key.button.IsHoldingEnabled(false);
                key.button.IsRightTapEnabled(false);
                key.pointerPressed = box_value(Input::PointerEventHandler([vk = key.vk](auto&&, auto&&) {
                    g_heldFooterKey = vk;
                    g_footerPressedAt = GetTickCount64();
                }));
                key.pointerCanceled = box_value(Input::PointerEventHandler([vk = key.vk](auto&&, auto&&) {
                    if (g_heldFooterKey == vk) g_heldFooterKey = 0;
                }));
                key.button.AddHandler(UIElement::PointerPressedEvent(), key.pointerPressed, true);
                key.button.AddHandler(UIElement::PointerCanceledEvent(), key.pointerCanceled, true);
            }
            if (key.vk == VK_LWIN) AttachModifierSlide(key.button, VK_LWIN, g_winSlideHandlers);
            if (key.vk == VK_MENU) AttachModifierSlide(key.button, VK_MENU, g_altSlideHandlers);
            key.click = key.button.Click([hwnd, vk = key.vk, clipboardKey](auto&&, auto&&) {
                TraceScope traceScope("Footer Key Click callback", __LINE__);
                if (!FooterKeys::g_enabled || !CompactModeActive()) return;
                const bool held = vk == std::exchange(g_heldFooterKey, 0) &&
                                  GetTickCount64() - g_footerPressedAt >= kFooterHoldMs;
                if (held && (vk == VK_LWIN || vk == VK_TAB)) {
                    FooterKeys::ToggleModifier(vk);
                } else if (vk == VK_LWIN) {
                    FooterKeys::t_shortcut = {};
                    SendShortcut({{VK_LWIN}, 1});
                } else if (vk == VK_BROWSER_BACK || vk == VK_BROWSER_FORWARD) {
                    FooterKeys::t_shortcut = {};
                    const WORD arrow = vk == VK_BROWSER_BACK ? VK_LEFT : VK_RIGHT;
                    SendShortcut({{VK_MENU, arrow}, 2});
                } else if (vk == VK_SNAPSHOT) {
                    FooterKeys::t_shortcut = {};
                    SendShortcut({{VK_LWIN, VK_SNAPSHOT}, 2});
                } else if (vk == kSpecialShortcutKey || clipboardKey) {
                    const auto chord = clipboardKey ? Settings::ShortcutChord{{VK_CONTROL, vk}, 2}
                                                    : ReadSettings().specialKey;
                    FooterKeys::t_shortcut = {};
                    if (chord.count) {
                        VLOG(L"Compact shortcut: sending footer chord for key 0x%02X", static_cast<unsigned>(vk));
                        FooterKeys::SendFooterChord(chord);
                    } else Wh_Log(L"Compact shortcut: the configured ★ shortcut is invalid or reserved by Windows");
                } else if (vk == VK_MENU) FooterKeys::ToggleModifier(VK_MENU);
                else FooterKeys::PressFooterKey(vk);
                if (!g_stopping && !g_refreshQueued) g_refreshQueued = PostMessage(hwnd, kRefresh, 0, 0);
            });
            strip.Children().Append(key.button);
        }
        g_keyboard = keyboard;
        g_panel = panel;
        g_strip = strip;
        g_button = button;
        g_click = click;
        g_footer = footer;
        g_stockFooterHeight = stockHeight;
        g_knownStockFooter = stockHeight.Value;
        g_stockFooterMin = minimum;
        g_stockFooterMax = maximum;
        g_appliedFooterHeight = stockHeight.Value;
        std::move(footerKeys.begin(), footerKeys.end(), std::begin(g_footerKeys));
        g_rowNeedsRemoval = true;
        panel.Children().Append(strip);
        g_rowNeedsRemoval = false;
        RowState(L"row added");
    }

    UpdateInjectedKeyTheme(panel, keys);
    ColorNativeEnter(keys);
    if (ReadSettings().hideVoiceTypingButton) WidenNativeEnter(keys);
    else if (g_hiddenMic) RestoreNativeBottomRow();
    const auto row = MeasureBottomRow(keys, g_strip);
    const double keyWidth = row.keyWidth > 0 ? row.keyWidth : row.first.Width;
    const double keyHeight = row.first.Height;
    if (keyWidth <= 0 || keyHeight <= 0) {
        TRACE_BLOCK();
        if (HoldRowThroughGlitch(hwnd)) return;
        if (g_strip.IsHitTestVisible()) g_strip.IsHitTestVisible(false);
        if (g_strip.Opacity() != 0) g_strip.Opacity(0);
        return;
    }
    const double nativeGap = row.first.Y - row.precedingBottom;
    const double gap = nativeGap > 0 && nativeGap < keyHeight / 2 ? nativeGap : keyHeight * 0.08;
    // Scale the screenshot-based top and bottom insets with the native key height.
    const double screenshotPixel = keyHeight / 81;
    const double top = std::max(0.0, row.bottom + gap + 2 * screenshotPixel);
    // The stock frame's bottom inset is about 18 px at the reference scale.
    const double bottomInset = 18 * screenshotPixel;
    const double lowerRowTop = top + keyHeight + gap;
    const double footerHeight = std::min(g_stockFooterHeight.Value + reserve, lowerRowTop + keyHeight + bottomInset);
    if (footer.MaxHeight() < footerHeight) footer.MaxHeight(footerHeight);
    if (footer.Height().Value != footerHeight) footer.Height({footerHeight, GridUnitType::Pixel});
    if (footer.MinHeight() != footerHeight) footer.MinHeight(footerHeight);
    if (footer.MaxHeight() != footerHeight) footer.MaxHeight(footerHeight);
    if (g_appliedFooterHeight != footerHeight) {
        TRACE_BLOCK();
        g_appliedFooterHeight = footerHeight;
        VLOG(L"Compact XAML: expanded framed footer to %.1f DIP", footerHeight);
    }

    // Both injected rows are one uniform 10-column grid spanning the native
    // bottom row, so every gap matches at any width scale and the arrow
    // columns line up. Edges snap to device pixels to keep gaps identical.
    constexpr int kColumns = 10;
    const double span = row.right - row.first.X;
    const double cellWidth = (span - (kColumns - 1) * gap) / kColumns;
    const bool fits = row.first.X >= 0 && cellWidth >= keyHeight * 0.4 &&
                      lowerRowTop + keyHeight <= g_strip.ActualHeight() + 0.5;
    if (!fits && HoldRowThroughGlitch(hwnd)) return;
    g_rowUnstableSince = 0;
    KillTimer(hwnd, kGraceTimer);
    const bool shown = fits && FrameHasRowRoom();
    if (fits && !shown)
        RowState(L"waiting for frame (applied view %.1f, rows want %.1f)", FrameSize::g_appliedView.load(),
                 FrameSize::g_wantedView.load());
    else if (shown)
        RowState(L"shown");
    if (g_strip.IsHitTestVisible() != shown) g_strip.IsHitTestVisible(shown);
    if (g_strip.Opacity() != (shown ? 1.0 : 0.0)) g_strip.Opacity(shown ? 1.0 : 0.0);
    if (!fits) return;

    double scale = 1;
    try {
        if (const auto xamlRoot = g_strip.XamlRoot()) scale = xamlRoot.RasterizationScale();
    } catch (const hresult_error&) {}
    if (!std::isfinite(scale) || scale <= 0) scale = 1;
    const auto snap = [scale](double value) { return std::round(value * scale) / scale; };
    auto place = [&](const Button& button, int column, double y, double font) {
        TRACE_BLOCK();
        const double left = snap(row.first.X + column * (cellWidth + gap));
        const double width = snap(row.first.X + column * (cellWidth + gap) + cellWidth) - left;
        if (Canvas::GetLeft(button) != left) Canvas::SetLeft(button, left);
        if (Canvas::GetTop(button) != y) Canvas::SetTop(button, y);
        if (button.Width() != width) button.Width(width);
        if (button.Height() != keyHeight) button.Height(keyHeight);
        if (button.FontSize() != font) button.FontSize(font);
        if (button.Visibility() != Visibility::Visible) button.Visibility(Visibility::Visible);
    };
    constexpr WORD kTopRow[kColumns] = {VK_CONTROL, VK_LWIN, VK_MENU, VK_TAB, VK_APPS,
                                        VK_ESCAPE, VK_DELETE, VK_HOME, VK_UP, VK_END};
    constexpr WORD kBottomRow[kColumns] = {'A', 'C', 'V', kSpecialShortcutKey, VK_SNAPSHOT,
                                           VK_BROWSER_BACK, VK_BROWSER_FORWARD, VK_LEFT, VK_DOWN, VK_RIGHT};
    const double iconFont = std::min(keyHeight * 0.36, cellWidth * 0.65);
    const double textFont = std::min(keyHeight * 0.30, cellWidth * 0.31);
    const double labelFont = std::min(keyHeight * 0.36, cellWidth * 0.45);  // Ctrl / Alt / Esc / Del
    place(g_button, 0, top, labelFont);

    const auto foreground = GetForegroundWindow();
    if (!FooterKeys::AdapterMatchesInput(foreground)) FooterKeys::ClearAdapter();
    const auto modifier = FooterKeys::ShortcutMatchesInput(foreground) ? FooterKeys::t_shortcut.modifier : 0;
    const auto referenceFace = FunctionKeyFace(keys, row.face);
    MatchKeyFace(g_button, referenceFace, modifier == VK_CONTROL || g_modifierSlide.modifier == VK_CONTROL);
    for (const auto& key : g_footerKeys) {
        TRACE_BLOCK();
        MatchKeyFace(key.button, referenceFace, (modifier && key.vk == modifier) ||
                     (g_modifierSlide.modifier && key.vk == g_modifierSlide.modifier));
        const bool wordLabel = key.vk == VK_HOME || key.vk == VK_END;
        const double font = key.hasIcon || key.vk == kSpecialShortcutKey ? iconFont :
                            wordLabel ? textFont : labelFont;
        for (int column = 0; column < kColumns; ++column) {
            if (kTopRow[column] == key.vk) place(key.button, column, top, font);
            else if (kBottomRow[column] == key.vk) place(key.button, column, lowerRowTop, font);
        }
    }
}

LRESULT CALLBACK UiWindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    TRACE_SCOPE();
    if (message == WM_TIMER && wp == kArrowRepeat) {
        if (!g_repeatingArrow || g_stopping || !FooterKeys::g_enabled || !CompactModeActive()) {
            StopArrowRepeat(hwnd);
            return 0;
        }
        if (!g_arrowRepeatStarted) {
            UINT speed = 31;
            SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &speed, 0);
            const UINT interval = 31000 / (77 + 27 * std::min(speed, 31u));
            if (!SetTimer(hwnd, kArrowRepeat, interval, nullptr)) StopArrowRepeat(hwnd);
            else g_arrowRepeatStarted = true;
        }
        if (g_repeatingArrow) FooterKeys::PressFooterKey(g_repeatingArrow);
        return 0;
    }
    if (message == kRefresh || (message == WM_TIMER && (wp == kWatchdog || wp == kGraceTimer))) {
        TRACE_BLOCK();
        if (message == WM_TIMER && wp != kWatchdog) KillTimer(hwnd, wp);
        if (message == kRefresh) g_refreshQueued = false;
        if (g_stopping || g_refreshing) return 0;
        g_refreshing = true;
        try {
            TRACE_BLOCK();
            RefreshRow(hwnd);
        } catch (const hresult_error& error) {
            TRACE_BLOCK();
            Wh_Log(L"Compact Ctrl row: XAML update failed (0x%08X)", error.code().value);
        } catch (const std::exception&) {
            TRACE_BLOCK();
            Wh_Log(L"Compact Ctrl row: update failed");
        }
        g_refreshing = false;
        if (g_stopping) PostMessage(hwnd, kRemove, 0, 0);
        return 0;
    }
    if (message == kRemove) {
        TRACE_BLOCK();
        if (!g_stopping || g_refreshing || g_removing) return 0;
        g_removing = true;
        KillTimer(hwnd, kWatchdog);
        try {
            TRACE_BLOCK();
            if (g_layoutRoot) {
                TRACE_BLOCK();
                g_layoutRoot.LayoutUpdated(g_layoutUpdated);
                g_layoutRoot = nullptr;
            }
            RemoveRow();
        } catch (const hresult_error& error) {
            TRACE_BLOCK();
            Wh_Log(L"Compact Ctrl row: XAML removal failed (0x%08X)", error.code().value);
            g_removing = false;
            return 0;
        } catch (const std::exception&) {
            TRACE_BLOCK();
            Wh_Log(L"Compact Ctrl row: removal failed");
            g_removing = false;
            return 0;
        }
        FooterKeys::ClearAdapter();
        if (!DestroyWindow(hwnd)) {
            TRACE_BLOCK();
            g_removing = false;
            return 0;
        }
        UnregisterClass(kWindowClass, GetModuleHandle(nullptr));
        g_uiWindow = nullptr;
        return 1;
    }
    return DefWindowProc(hwnd, message, wp, lp);
}

decltype(&DispatchMessageW) g_dispatchMessage;
LRESULT WINAPI DispatchMessage_hook(const MSG* message) {
    TRACE_SCOPE();
    const auto result = g_dispatchMessage(message);
    if (g_stopping || g_uiWindow ||
        !GetModuleHandle(L"Windows.UI.Xaml.dll")) return result;
    thread_local ULONGLONG lastProbe;
    const auto now = GetTickCount64();
    if (now - lastProbe < 250) return result;
    lastProbe = now;
    try {
        TRACE_BLOCK();
        const auto window = Window::Current();
        if (!window || !window.Content() ||
            !FindNamed(window.Content(), L"TEMPLATE_PART_KeyGrid")) return result;
        std::lock_guard lock(g_uiRegistrationMutex);
        if (g_stopping || g_uiWindow) return result;
        WNDCLASS wc{};
        wc.lpfnWndProc = UiWindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = kWindowClass;
        if (!RegisterClass(&wc)) return result;
        const auto hwnd = CreateWindowEx(0, kWindowClass, nullptr, 0, 0, 0, 0, 0,
                                         HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (hwnd && SetTimer(hwnd, kWatchdog, kWatchdogMs, nullptr)) {
            TRACE_BLOCK();
            FooterKeys::g_arrowUiThread = GetCurrentThreadId();
            g_uiWindow = hwnd;
            g_refreshQueued = PostMessage(hwnd, kRefresh, 0, 0);
        } else {
            TRACE_BLOCK();
            if (hwnd) DestroyWindow(hwnd);
            UnregisterClass(kWindowClass, wc.hInstance);
        }
    } catch (const hresult_error&) {
        TRACE_BLOCK();
        // This thread does not have an accessible XAML window.
    }
    return result;
}

void Stop() {
    TRACE_SCOPE();
    HWND hwnd;
    {
        TRACE_BLOCK();
        std::lock_guard lock(g_uiRegistrationMutex);
        g_stopping = true;
        FooterKeys::g_arrowUiThread = 0;
        hwnd = g_uiWindow;
    }
    if (!hwnd) return;
    DWORD_PTR removed = 0;
    if (SendMessageTimeout(hwnd, kRemove, 0, 0, SMTO_BLOCK | SMTO_ABORTIFHUNG, 2000, &removed) &&
        removed == 1) return;
    // Leave live window procedures and event handlers mapped until the host exits.
    if (KeepModMapped()) {
        TRACE_BLOCK();
        Wh_Log(L"Compact Ctrl row: deferring UI cleanup to its owning thread");
        PostMessage(hwnd, kRemove, 0, 0);
        return;
    }
    // The loader should always find the module that is currently executing.
    // If it cannot, wait for the owner rather than unloading live callbacks.
    while (g_uiWindow) {
        TRACE_BLOCK();
        Sleep(100);
        SendMessage(hwnd, kRemove, 0, 0);
    }
}
}  // namespace CompactCtrlRow

bool HookTextInput(HMODULE mod) {
    TRACE_SCOPE();
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{L"private: void __cdecl " TIU L"ShellViewPositionAndSizeManager::PrimaryViewTryHide(class Platform::String ^)"},
         &PrimaryViewTryHide_orig, PrimaryViewTryHide_hook, true},
        {{L"private: void __cdecl " TIU L"ShellViewPositionAndSizeManager::OnShowRequestedInternal(class " OSI L"OnScreenInputShowRequestedEventArgs ^)"},
         &OnShowRequestedInternal_orig, OnShowRequestedInternal_hook, true},
        {{L"private: bool __cdecl " TIU L"Controls::DefaultPointerHandler::IsShapewritingEnabled(void)"},
         &PointerIsShapewritingEnabled_orig, PointerIsShapewritingEnabled_hook, true},
        {{L"public: virtual bool __cdecl " TIU L"TextFramework::SystemInputProcessor::[" TIU L"TextFramework::ISystemInputProcessor]::IsShapewritingEnabled(void)"},
         &SipIsShapewritingEnabled_orig, SipIsShapewritingEnabled_hook, true},
        {{L"private: void __cdecl " TIU L"Controls::InputPanelModel::OnTextFrameworkFocusChanged(struct " TIC L"ITextFrameworkAdapter ^,struct " TIC L"ITextFrameworkFocusChangedEventArgs ^)"},
         &TextFrameworkFocusChanged_orig, TextFrameworkFocusChanged_hook, true},
        {{L"private: void __cdecl " TIU L"Controls::InputPanelModel::OnShapewritingEnabled(struct " TIC L"ITextFrameworkAdapter ^,bool)"},
         &ModelOnShapewritingEnabled_orig, ModelOnShapewritingEnabled_hook, true},
        {{L"public: virtual void __cdecl " TIU L"Controls::InputPanelModel::[TouchKeyboardUX::IInputPanelModel]::OnCloseButtonClicked(class Platform::Object ^,class Windows::UI::Xaml::RoutedEventArgs ^)"},
         &KeyboardCloseClicked_orig, KeyboardCloseClicked_hook, true},
        {{L"public: virtual void __cdecl " TIU L"Controls::InputPanelControl::[TouchKeyboardUX::IXamlKeyboardControl]::Deactivate(class Platform::String ^)"},
         &KeyboardDeactivate_orig, KeyboardDeactivate_hook, true},
        {{L"private: bool __cdecl " TIU L"Controls::DefaultPointerHandler::IsTraveledEnoughToStartShapewriting(class Windows::Foundation::Point const &)"},
         &SwipeStartDistance_orig, SwipeStartDistance_hook, true},
        {{L"public: virtual class " TIC L"SizePositionInfo __cdecl " TIU L"Controls::WideNarrowSizePositionManager::CalculateDesiredSize(void)"},
         &WNCalcDesiredSize_orig, WNCalcDesiredSize_hook, true},
    };
    bool ok = WindhawkUtils::HookSymbols(mod, hooks, ARRAYSIZE(hooks));
    Wh_Log(L"TextInput hooks %s", ok ? L"ok" : L"FAILED");
    if (!TextFrameworkFocusChanged_orig) Wh_Log(L"Compact keys: native focus adapter hook unavailable");
    if (ok) {
        FooterKeys::Install(mod);  // also feeds the symbol pass used below
        AutoHide::Finish(mod);
        Wh_Log(L"Key sound: native click %s", KeySound::Install() ? L"available" : L"unavailable");
        Wh_Log(L"Frame size: apply hook %s", FrameSize::Install() ? L"installed" : L"unavailable");
        void* tryHide = AutoHide::TryHideBody(mod);
        const bool hooked = tryHide &&
            Wh_SetFunctionHook(tryHide, reinterpret_cast<void*>(KeyboardTryHide_hook),
                               reinterpret_cast<void**>(&KeyboardTryHide_orig));
        Wh_Log(L"Auto-dismiss: TryHide caller filter %s", hooked ? L"installed" : L"unavailable");
    }
    // Decoded here, before Windhawk applies the hooks, while the bytes are original.
    g_swipeThresholdOffset = g_swipeFunction ? DecodeSwipeThresholdOffset(mod, g_swipeFunction) : 0;
    if (!g_swipeThresholdOffset)
        Wh_Log(L"Swipe start: threshold field not decoded; custom swipe-start distance unavailable");
    else
        VLOG(L"Swipe start: threshold at handler+0x%zX", g_swipeThresholdOffset);
    if (!Wh_SetFunctionHook(reinterpret_cast<void*>(DispatchMessageW),
                            reinterpret_cast<void*>(CompactCtrlRow::DispatchMessage_hook),
                            reinterpret_cast<void**>(&CompactCtrlRow::g_dispatchMessage))) {
        TRACE_BLOCK();
        Wh_Log(L"Keyboard visibility: could not hook the XAML message pump");
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Module loading
// ---------------------------------------------------------------------------
struct ModuleHooks {
    PCWSTR name;
    bool (*install)(HMODULE);
    HMODULE image = nullptr;
    bool configured = false;
};

ModuleHooks g_text{L"TextInput.dll", HookTextInput};
ModuleHooks g_inputApp{L"InputApp.dll", HookInputApp};
std::mutex g_moduleMutex;
std::atomic<bool> g_scanRequested;
std::atomic<bool> g_modulesSettled;
bool g_pendingApply;

static bool RegisterModule(ModuleHooks& hooks) {
    TRACE_SCOPE();
    if (hooks.image || !GetModuleHandleEx(0, hooks.name, &hooks.image)) return false;
    // Retain even a partially configured image until Windhawk removes its detours.
    try {
        TRACE_BLOCK();
        hooks.configured = hooks.install(hooks.image);
    } catch (const std::exception&) {
        TRACE_BLOCK();
        Wh_Log(L"Hook preparation failed for %s", hooks.name);
    }
    return true;
}

static void PublishHookReadiness() {
    TRACE_SCOPE();
    g_textReady = g_text.configured;
    g_inputAppReady = g_inputApp.configured;
    g_sizeReady = g_text.configured && WNCalcDesiredSize_orig;
    // The extra rows need only XAML and SendInput. The native key-action
    // hooks add sticky Ctrl/Alt for native keys when they resolve.
    FooterKeys::g_enabled = g_text.configured;
}

static void RegisterLoadedModules() {
    TRACE_SCOPE();
    if (!g_isExplorer) {
        TRACE_BLOCK();
        g_pendingApply |= RegisterModule(g_text);
        g_pendingApply |= RegisterModule(g_inputApp);
    }
    g_modulesSettled = g_isExplorer || (g_text.image && g_inputApp.image);
}

static void ScanLateModules() {
    TRACE_SCOPE();
    g_scanRequested = true;
    do {
        TRACE_BLOCK();
        // A symbol lookup can itself load a DLL. Coalesce reentrant notifications.
        std::unique_lock lock(g_moduleMutex, std::try_to_lock);
        if (!lock || g_unloading) return;
        if (!g_scanRequested.exchange(false)) return;
        RegisterLoadedModules();
        if (g_pendingApply) {
            TRACE_BLOCK();
            if (Wh_ApplyHookOperations()) {
                TRACE_BLOCK();
                g_pendingApply = false;
                PublishHookReadiness();
            } else {
                TRACE_BLOCK();
                Wh_Log(L"Could not activate pending touch keyboard hooks");
            }
        }
    } while (g_scanRequested);
}

using LoadLibraryExW_t = decltype(&LoadLibraryExW);
LoadLibraryExW_t LoadLibraryExW_orig;
HMODULE WINAPI LoadLibraryExW_hook(LPCWSTR name, HANDLE file, DWORD flags) {
    TRACE_SCOPE();
    HMODULE m = LoadLibraryExW_orig(name, file, flags);
    if (m && !g_modulesSettled && !g_unloading) ScanLateModules();
    return m;
}

void LoadSettings() {
    TRACE_SCOPE();
    std::lock_guard lock(g_settingsWriter);
    if (g_unloading) return;
    Settings settings;
    settings.hideVoiceTypingButton = Wh_GetIntSetting(L"hideVoiceTypingButton");
    settings.noAutoHide = Wh_GetIntSetting(L"noAutoHide");
    settings.bubbleOnClose = Wh_GetIntSetting(L"bubbleOnClose");
    settings.hideTrayIcon = Wh_GetIntSetting(L"hideTrayIconWithBubble");
    settings.bubbleIdleAlpha = std::clamp(Wh_GetIntSetting(L"bubbleIdleOpacity"), 5, 100) * 255 / 100;
    settings.bubbleIdleDelay = std::max(100, Wh_GetIntSetting(L"bubbleIdleDelay"));
    settings.customSwipeStartDistance = Wh_GetIntSetting(L"noSwipeStartDistance");
    settings.swipeStartDistancePercent = std::clamp(Wh_GetIntSetting(L"swipeStartDistancePercent"), 0, 200);
    settings.forceSwipe = Wh_GetIntSetting(L"forceSwipe");
    settings.horizontalScale = Wh_GetIntSetting(L"horizontalScale");
    if (settings.horizontalScale < 50 || settings.horizontalScale > 200) settings.horizontalScale = 100;
    if (PCWSTR shortcut = Wh_GetStringSetting(L"specialKey")) {
        if (*shortcut) settings.specialKey = ParseShortcut(shortcut);
        Wh_FreeStringSetting(shortcut);
    }
    g_verboseLogging = Wh_GetIntSetting(L"verbose") != 0;
    PublishSettings(settings);
}

BOOL Wh_ModInit() {
    TRACE_SCOPE();
    if (g_requiresHostRestart) {
        TRACE_BLOCK();
        Wh_Log(L"Restart the host process before re-enabling the mod after deferred shutdown");
        return FALSE;
    }
    WCHAR path[32768];
    const DWORD length = GetModuleFileName(nullptr, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) return FALSE;
    const auto separator = wcsrchr(path, L'\\');
    const auto filename = separator ? separator + 1 : path;
    g_isExplorer = _wcsicmp(filename, L"explorer.exe") == 0;
    Wh_Log(L"init v%s in %s pid=%u", WH_MOD_VERSION, filename, GetCurrentProcessId());
    LoadSettings();

    InitShared();
    RegisterLoadedModules();
    g_pendingApply = false;  // Windhawk will apply this initial batch.
    if (!g_modulesSettled) {
        TRACE_BLOCK();
        const HMODULE kernel = GetModuleHandle(L"kernelbase.dll");
        const auto loader = kernel ? GetProcAddress(kernel, "LoadLibraryExW") : nullptr;
        if (!loader || !Wh_SetFunctionHook(reinterpret_cast<void*>(loader),
                                           reinterpret_cast<void*>(LoadLibraryExW_hook),
                                           reinterpret_cast<void**>(&LoadLibraryExW_orig))) {
            TRACE_BLOCK();
            Wh_Log(L"Could not register late touch keyboard module discovery");
        }
    }

    // explorer: only the taskbar process hosts the bubble (folder windows may
    // run in a second explorer.exe).
    bool startThread = false;
    if (g_isExplorer) {
        TRACE_BLOCK();
        DWORD trayPid = 0;
        const HWND tray = FindWindow(L"Shell_TrayWnd", nullptr);
        if (tray) GetWindowThreadProcessId(tray, &trayPid);
        startThread = !tray || trayPid == GetCurrentProcessId();
    }
    if (startThread) {
        TRACE_BLOCK();
        g_threadStop = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (g_threadStop) g_thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        if (!g_thread) {
            TRACE_BLOCK();
            Wh_Log(L"Could not start keyboard worker (%lu)", GetLastError());
            if (g_threadStop) CloseHandle(g_threadStop);
            g_threadStop = nullptr;
        }
    }
    return TRUE;
}

void Wh_ModAfterInit() {
    TRACE_SCOPE();
    {
        TRACE_BLOCK();
        std::lock_guard lock(g_moduleMutex);
        if (!g_pendingApply) PublishHookReadiness();
    }
    if (!g_modulesSettled) ScanLateModules();
}

// Hooks are still installed here; make them all pass-through so nothing is
// blocked while Windhawk removes them.
void Wh_ModBeforeUninit() {
    TRACE_SCOPE();
    {
        TRACE_BLOCK();
        std::lock_guard lock(g_settingsWriter);
        g_unloading = true;
        PublishSettings(Settings{});
    }
    std::lock_guard lock(g_moduleMutex);
    FooterKeys::g_enabled = false;
}

void Wh_ModUninit() {
    TRACE_SCOPE();
    if (g_isExplorer) SetTrayIconSuppressed(false);
    CompactCtrlRow::Stop();
    KeySound::Release();
    if (!g_isExplorer && g_shared) {
        InterlockedExchange64(&g_shared->panelVisibilityDeadline, 0);
        InterlockedExchange64(&g_shared->compactModeDeadline, 0);
    }
    // Prefer joining the worker. If it is stuck inside the host, keep its code
    // mapped and let it release its resources after it returns.
    bool deferred = false;
    if (g_thread) {
        TRACE_BLOCK();
        SetEvent(g_threadStop);
        if (WaitForSingleObject(g_thread, 2000) != WAIT_OBJECT_0) {
            TRACE_BLOCK();
            {
                TRACE_BLOCK();
                std::lock_guard lock(g_workerExitMutex);
                if (!g_workerExited && KeepModMapped()) {
                    TRACE_BLOCK();
                    g_workerOwnsSharedCleanup = deferred = true;
                    Wh_Log(L"Keyboard worker: deferring cleanup until the worker exits");
                }
            }
            if (!deferred) WaitForSingleObject(g_thread, INFINITE);
        }
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (!deferred) {
        TRACE_BLOCK();
        if (g_threadStop) CloseHandle(g_threadStop);
        g_threadStop = nullptr;
        CloseShared();
    }
    if (!g_requiresHostRestart) {
        TRACE_BLOCK();
        for (auto hooks : {&g_inputApp, &g_text}) {
            TRACE_BLOCK();
            if (hooks->image) FreeLibrary(hooks->image);
        }
    }
}

void Wh_ModSettingsChanged() {
    TRACE_SCOPE();
    LoadSettings();
}
