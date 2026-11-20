// ==WindhawkMod==
// @id              touch-keyboard-probe
// @name            Touch Keyboard Probe (dismissal tracer)
// @description     Logs real calls inside the touch keyboard stack, for diagnosing touch-keyboard-enhancement
// @version         0.11
// @author          engrymoon
// @include         TextInputHost.exe
// @architecture    x86-64
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Touch Keyboard Probe

Traces symbolized hide, dismissal, activation, focus, and visibility calls
in the native input stack. Every hook forwards to the original function.

Disable touch-keyboard-enhancement while testing the native baseline.
Capture separate logs for opening Start, pressing the keyboard's X, and
opening or closing the emoji and clipboard panels.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- maxPerSecond: 20
  $name: Max logged calls per function per second
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <atomic>
#include <cwctype>
#include <string>
#include <vector>
#include <unordered_set>

static constexpr PCWSTR g_modules[] = {
    L"TextInput.dll", L"InputApp.dll", L"Windows.UI.Core.TextInput.dll",
    L"TextInputFramework.dll", L"tiptsf.dll",
};
int g_maxPerSecond;
// --- per-function state ----------------------------------------------------
struct FuncInfo {
    std::wstring name;
    std::atomic<ULONGLONG> windowStart{0};
    std::atomic<int> count{0};
    std::atomic<int> suppressed{0};
};

// Thunks and original-pointer slots live in VirtualAlloc memory that is never
// freed, so a thread that is still inside a thunk during unload never jumps
// into freed memory.
constexpr size_t kThunkSize = 160;
constexpr size_t kMaxFuncs = 6000;
BYTE* g_thunks;
void** g_slots;
FuncInfo* g_funcs;
std::atomic<size_t> g_funcCount{0};
std::unordered_set<void*> g_hookedAddrs;
DWORD g_tlsGuard = TLS_OUT_OF_INDEXES;
std::atomic<bool> g_active{false};

static std::wstring Shorten(PCWSTR s) {
    std::wstring r = s;
    static const std::pair<const wchar_t*, const wchar_t*> reps[] = {
        {L"WindowsInternal::ComposableShell::Experiences::TextInputUndocked::", L"TIU::"},
        {L"WindowsInternal::ComposableShell::Experiences::TextInput::Common::", L"C::"},
        {L"WindowsUdk::ApplicationModel::OnScreenInput::", L"OSI::"},
        {L"Windows::UI::Internal::Text::Core::", L"WUITC::"},
        {L"class Platform::String ^", L"String^"},
        {L"public: ", L""}, {L"private: ", L""}, {L"protected: ", L""},
        {L"virtual ", L""}, {L"static ", L""}, {L"__cdecl ", L""}, {L" __ptr64", L""},
    };
    for (auto& [from, to] : reps) {
        size_t pos;
        while ((pos = r.find(from)) != std::wstring::npos) r.replace(pos, wcslen(from), to);
    }
    if (r.size() > 300) r.resize(300);
    return r;
}

extern "C" void __cdecl ProbeLog(size_t id, void* retAddr) {
    if (!g_active || id >= g_funcCount) return;
    if (TlsGetValue(g_tlsGuard)) return;  // no recursion through Wh_Log
    TlsSetValue(g_tlsGuard, (void*)1);
    DWORD err = GetLastError();

    FuncInfo& f = g_funcs[id];
    ULONGLONG now = GetTickCount64();
    ULONGLONG ws = f.windowStart;
    if (now - ws >= 1000) {
        f.windowStart = now;
        int sup = f.suppressed.exchange(0);
        f.count = 0;
        if (sup) Wh_Log(L"#%zu (%d more calls suppressed)", id, sup);
    }
    if (f.count++ < g_maxPerSecond) {
        HMODULE m = nullptr;
        WCHAR mod[MAX_PATH] = L"?";
        if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCWSTR)retAddr, &m))
            GetModuleFileName(m, mod, MAX_PATH);
        PCWSTR base = wcsrchr(mod, L'\\');
        Wh_Log(L"#%zu T%u %s <- %s+%llX", id, GetCurrentThreadId(), f.name.c_str(), base ? base + 1 : mod,
               (unsigned long long)((char*)retAddr - (char*)m));
    } else {
        f.suppressed++;
    }

    SetLastError(err);
    TlsSetValue(g_tlsGuard, nullptr);
}

// Generic x64 thunk: saves the 4 integer and 4 float argument registers,
// calls ProbeLog(id, returnAddress), restores them and tail-jumps to the
// original function, so any signature passes through unchanged.
static void BuildThunk(BYTE* p, size_t id, void** slot) {
    auto emit = [&](std::initializer_list<BYTE> b) { for (BYTE x : b) *p++ = x; };
    auto imm64 = [&](UINT64 v) { memcpy(p, &v, 8); p += 8; };
    emit({0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00});  // sub rsp,0x88
    emit({0x48, 0x89, 0x4C, 0x24, 0x20});              // mov [rsp+20h],rcx
    emit({0x48, 0x89, 0x54, 0x24, 0x28});              // mov [rsp+28h],rdx
    emit({0x4C, 0x89, 0x44, 0x24, 0x30});              // mov [rsp+30h],r8
    emit({0x4C, 0x89, 0x4C, 0x24, 0x38});              // mov [rsp+38h],r9
    emit({0x66, 0x0F, 0xD6, 0x44, 0x24, 0x40});        // movq [rsp+40h],xmm0
    emit({0x66, 0x0F, 0xD6, 0x4C, 0x24, 0x48});        // movq [rsp+48h],xmm1
    emit({0x66, 0x0F, 0xD6, 0x54, 0x24, 0x50});        // movq [rsp+50h],xmm2
    emit({0x66, 0x0F, 0xD6, 0x5C, 0x24, 0x58});        // movq [rsp+58h],xmm3
    emit({0x48, 0xB9}); imm64(id);                     // mov rcx,id
    emit({0x48, 0x8B, 0x94, 0x24, 0x88, 0x00, 0x00, 0x00});  // mov rdx,[rsp+88h] (return address)
    emit({0x48, 0xB8}); imm64((UINT64)&ProbeLog);      // mov rax,ProbeLog
    emit({0xFF, 0xD0});                                // call rax
    emit({0xF3, 0x0F, 0x7E, 0x44, 0x24, 0x40});        // movq xmm0,[rsp+40h]
    emit({0xF3, 0x0F, 0x7E, 0x4C, 0x24, 0x48});        // movq xmm1,[rsp+48h]
    emit({0xF3, 0x0F, 0x7E, 0x54, 0x24, 0x50});        // movq xmm2,[rsp+50h]
    emit({0xF3, 0x0F, 0x7E, 0x5C, 0x24, 0x58});        // movq xmm3,[rsp+58h]
    emit({0x48, 0x8B, 0x4C, 0x24, 0x20});              // mov rcx,[rsp+20h]
    emit({0x48, 0x8B, 0x54, 0x24, 0x28});              // mov rdx,[rsp+28h]
    emit({0x4C, 0x8B, 0x44, 0x24, 0x30});              // mov r8,[rsp+30h]
    emit({0x4C, 0x8B, 0x4C, 0x24, 0x38});              // mov r9,[rsp+38h]
    emit({0x48, 0x81, 0xC4, 0x88, 0x00, 0x00, 0x00});  // add rsp,0x88
    emit({0x48, 0xB8}); imm64((UINT64)slot);           // mov rax,slot
    emit({0x48, 0x8B, 0x00});                          // mov rax,[rax]
    emit({0xFF, 0xE0});                                // jmp rax
}

static bool Matches(PCWSTR sym) {
    const auto args = wcschr(sym, L'(');
    if (!args) return false;
    std::wstring name(sym, args);
    for (auto& character : name) character = towlower(character);
    for (PCWSTR exclusion : {L"telemetry", L"_tlg", L"wil::", L"std::", L"operator", L"dependencyproperty"})
        if (name.find(exclusion) != name.npos) return false;
    // Match the method itself. Types such as ActivationChangedEventArgs
    // otherwise pull in unrelated COM bookkeeping for every instance.
    const auto separator = name.rfind(L"::");
    const auto method = name.substr(separator == name.npos ? 0 : separator + 2);
    if (method.starts_with(L"get_") || method.starts_with(L"put_") ||
        method.starts_with(L"add_") || method.starts_with(L"remove_") ||
        method.find(L"____abi_get_") != method.npos ||
        method.find(L"____abi_put_") != method.npos) return false;
    for (PCWSTR keyword : {L"hide", L"dismiss", L"deactivat", L"activation", L"activate",
                           L"focus", L"foreground", L"visibility", L"close", L"lostcapture"}) {
        if (method.find(keyword) != method.npos) return true;
    }
    return false;
}
// Folded one-liner stubs (`ret`, `xor eax,eax; ret`, `mov al,1; ret`, ...) are
// shared by unrelated callers across the process (one was hammered from
// xaudio2's audio thread and crashed TextInputHost), so they are never hooked.
static bool IsTrivialStub(const BYTE* p) {
    if (p[0] == 0xC3 || p[0] == 0xC2) return true;                                 // ret
    if ((p[0] == 0x33 || p[0] == 0x32) && p[1] == 0xC0 && p[2] == 0xC3) return true;  // xor eax/al; ret
    if (p[0] == 0xB0 && p[2] == 0xC3) return true;                                 // mov al,imm8; ret
    if (p[0] == 0xB8 && p[5] == 0xC3) return true;                                 // mov eax,imm32; ret
    return false;
}

static int ProbeModule(HMODULE mod, PCWSTR modName) {
    int added = 0, failed = 0;
    WH_FIND_SYMBOL fs;
    HANDLE h = Wh_FindFirstSymbol(mod, nullptr, &fs);
    if (!h) {
        Wh_Log(L"[%s] no symbols", modName);
        return 0;
    }
    do {
        if (!fs.symbol || !fs.address) continue;
        if (!Matches(fs.symbol)) continue;
        if (g_hookedAddrs.count(fs.address)) continue;  // identical-code-folded aliases
        if (IsTrivialStub((const BYTE*)fs.address)) continue;
        size_t id = g_funcCount;
        if (id >= kMaxFuncs) break;
        BYTE* thunk = g_thunks + id * kThunkSize;
        BuildThunk(thunk, id, &g_slots[id]);
        g_funcs[id].name = std::wstring(modName) + L"!" + Shorten(fs.symbol);
        if (Wh_SetFunctionHook(fs.address, thunk, &g_slots[id])) {
            g_hookedAddrs.insert(fs.address);
            Wh_Log(L"hook #%zu %s", id, g_funcs[id].name.c_str());
            g_funcCount++;
            added++;
        } else {
            failed++;
        }
    } while (Wh_FindNextSymbol(h, &fs));
    Wh_FindCloseSymbol(h);
    Wh_Log(L"[%s] hooked %d functions (%d failed)", modName, added, failed);
    return added;
}

std::vector<HMODULE> g_probedModules;
SRWLOCK g_probeLock = SRWLOCK_INIT;

static bool ProbeLoadedModules() {
    bool any = false;
    // The symbol engine itself loads DLLs; don't re-enter (or deadlock) from that.
    if (!TryAcquireSRWLockExclusive(&g_probeLock)) return false;
    for (PCWSTR name : g_modules) {
        HMODULE m = GetModuleHandle(name);
        if (!m) continue;
        bool seen = false;
        for (HMODULE p : g_probedModules) seen |= p == m;
        if (seen) continue;
        g_probedModules.push_back(m);
        any |= ProbeModule(m, name) > 0;
    }
    ReleaseSRWLockExclusive(&g_probeLock);
    return any;
}

using LoadLibraryExW_t = decltype(&LoadLibraryExW);
LoadLibraryExW_t LoadLibraryExW_orig;
HMODULE WINAPI LoadLibraryExW_hook(LPCWSTR name, HANDLE file, DWORD flags) {
    HMODULE m = LoadLibraryExW_orig(name, file, flags);
    if (m && g_active && ProbeLoadedModules()) Wh_ApplyHookOperations();
    return m;
}


BOOL Wh_ModInit() {
    g_maxPerSecond = Wh_GetIntSetting(L"maxPerSecond");
    if (g_maxPerSecond <= 0) g_maxPerSecond = 20;

    WCHAR exe[MAX_PATH];
    GetModuleFileName(nullptr, exe, MAX_PATH);
    Wh_Log(L"probe init build %S %S in %s pid=%u", __DATE__, __TIME__, wcsrchr(exe, L'\\') + 1,
           GetCurrentProcessId());

    g_tlsGuard = TlsAlloc();
    g_thunks = (BYTE*)VirtualAlloc(nullptr, kThunkSize * kMaxFuncs, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    g_slots = (void**)VirtualAlloc(nullptr, sizeof(void*) * kMaxFuncs, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_funcs = new FuncInfo[kMaxFuncs];
    if (!g_thunks || !g_slots || g_tlsGuard == TLS_OUT_OF_INDEXES) return FALSE;

    g_active = true;
    ProbeLoadedModules();
    Wh_SetFunctionHook((void*)GetProcAddress(GetModuleHandle(L"kernelbase.dll"), "LoadLibraryExW"),
                       (void*)LoadLibraryExW_hook, (void**)&LoadLibraryExW_orig);
    return TRUE;
}

void Wh_ModBeforeUninit() {
    g_active = false;
}

void Wh_ModUninit() {
    // Thunks/slots are intentionally leaked (see above). FuncInfo names are
    // only read while g_active, which is already false.
    Wh_Log(L"probe uninit build %S %S pid=%u, %zu functions", __DATE__, __TIME__, GetCurrentProcessId(),
           g_funcCount.load());
}
