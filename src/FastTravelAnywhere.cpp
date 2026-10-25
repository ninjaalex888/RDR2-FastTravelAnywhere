/*
    Fast Travel Anywhere  -  Red Dead Redemption 2 (Story Mode)
    A Script Hook RDR2 plugin (.asi)

    Hold E to kneel anywhere and the game shows its "Resting" menu in the
    bottom-right corner: Craft (R), Set Up Camp (E) and Leave (F). This plugin
    adds Fast Travel to that menu on Space (A on a controller), the same button
    a campfire uses, and it runs the game's own fast travel: the same menu of
    places, and the same trip with your horse.

    How the game does it (build 1491.50, from the decompiled game scripts)
    ----------------------------------------------------------------------
      * The kneel menu belongs to the dynamic_craft_scenario script. Its prompts
        sit in prompt group 342152817 ("REST_UI_GROUP", titled "Resting"),
        which that script draws every frame while you kneel.
      * A campfire's Fast Travel prompt (player_camp, interactive_campfire,
        sleeping_scenario) doesn't travel by itself. It sets bit 4 of
        Global_43907. The always-running short_update script (its "PLAYER CAMP
        FAST TRAVEL" module) sees the bit, builds the list of places, opens the
        FAST_TRAVEL_MENU app and keeps its progress in Global_43908:
          0 idle, 1 menu open, 2 place chosen, 4 and 5 travelling, 6 done.
      * While the menu is open, short_update closes it unless bit 23 of
        Global_1392040 is set. The campfire scripts set that bit while you're
        at the fire.
      * When you pick a place, short_update sets bit 8 and waits. The camp
        plays its kick-the-fire animation, the screen fades out, the camp packs
        up and sets bit 32. Then short_update takes your horse along (if it's
        within whistling range), loads the place and puts you on your horse.

    What this plugin does
    ---------------------
      * While the kneel menu is up, it adds a Fast Travel prompt to the same
        group, with the campfire's own text ("CAMP_FT_PROMPT"), so it reads
        "Fast Travel" in your game's language. That text lives in the game's
        CAMP text block, which the plugin loads when a kneel starts.
      * Pressing it sets bit 23 and bit 4, as a campfire does (once Arthur is
        all the way down: the menu doesn't stay open before that).
      * When you pick a place, it plays the camp's part: Arthur stands up, the
        screen fades out, then it sets bit 32 and the game does the rest.
      * It's greyed out when a campfire wouldn't offer fast travel either (not
        unlocked yet, wanted, on a mission, horse dead or too far away).
        Pressing it then shows the game's own message for the reason.

    Build: build.sh (zig, cross-compiles from Linux or macOS) or build_msvc.bat
    (Visual Studio). No Script Hook SDK files are needed: the plugin binds to
    ScriptHookRDR2.dll at runtime.
*/

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#endif

#define FTA_NAME    "Fast Travel Anywhere"
#define FTA_VERSION "1.0.1"

// ===========================================================================
// Paths & logging
// ===========================================================================

static char g_iniPath[MAX_PATH + 8];
static char g_logPath[MAX_PATH + 8];

static void BuildPaths(HMODULE mod)
{
    char path[MAX_PATH] = { 0 };
    DWORD n = GetModuleFileNameA(mod, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        strcpy(path, "FastTravelAnywhere.asi");

    char* dot   = strrchr(path, '.');
    char* slash = strrchr(path, '\\');
    if (dot && (!slash || dot > slash))
        *dot = 0;

    snprintf(g_iniPath, sizeof g_iniPath, "%s.ini", path);
    snprintf(g_logPath, sizeof g_logPath, "%s.log", path);
}

static void Log(const char* fmt, ...)
{
    if (!g_logPath[0])
        return;
    FILE* f = fopen(g_logPath, "a");
    if (!f)
        return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", (int)st.wHour, (int)st.wMinute,
            (int)st.wSecond, (int)st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void ResetLog()
{
    FILE* f = fopen(g_logPath, "w");
    if (f)
        fclose(f);
}

// The game's string hash (Jenkins one-at-a-time on the lower-cased string),
// used for script names, control names and UI app names.
static uint32_t Joaat(const char* s)
{
    uint32_t h = 0;
    for (; *s; s++)
    {
        uint8_t c = (uint8_t)*s;
        if (c >= 'A' && c <= 'Z')
            c = (uint8_t)(c - 'A' + 'a');
        h += c;
        h += h << 10;
        h ^= h >> 6;
    }
    h += h << 3;
    h ^= h >> 11;
    h += h << 15;
    return h;
}

// ===========================================================================
// Settings (FastTravelAnywhere.ini, next to the .asi)
// ===========================================================================

struct Settings
{
    bool     enabled;
    bool     requireUnlock;
    bool     requireHorseNearby;
    bool     allowWhenWanted;
    bool     allowOnMissions;
    bool     showWhenUnavailable;
    bool     standUp;
    bool     showLoadMessage;
    bool     debugLog;
    bool     lockMovementInMenu;
    uint32_t promptControl;
    char     promptControlName[64];
    char     promptText[96];
};

static Settings g_cfg;

static void DefaultSettings(Settings& s)
{
    memset(&s, 0, sizeof s);
    s.enabled             = true;
    s.requireUnlock       = true;
    s.requireHorseNearby  = true;
    s.allowWhenWanted     = false;
    s.allowOnMissions     = false;
    s.showWhenUnavailable = true;
    s.standUp             = true;
    s.showLoadMessage     = true;
    s.debugLog            = false;
    s.lockMovementInMenu  = false;
    strcpy(s.promptControlName, "INPUT_CONTEXT_A");
    s.promptControl       = Joaat("INPUT_CONTEXT_A");
    s.promptText[0]       = 0;
}

// Written when the file is missing (CRLF, for Notepad).
static const char kDefaultIni[] =
    "; Fast Travel Anywhere settings.\r\n"
    "; Changes take effect the next time you kneel (hold E), no restart needed.\r\n"
    "[FastTravelAnywhere]\r\n"
    "\r\n"
    "; 1 = add Fast Travel to the kneel menu, 0 = off\r\n"
    "Enabled=1\r\n"
    "\r\n"
    "; The game's own rules for fast travel at a campfire (1 = follow them):\r\n"
    "; fast travel has to be unlocked, as for your camp (Arthur: the lodging upgrade in the camp ledger).\r\n"
    "RequireUnlock=1\r\n"
    "; your horse has to be alive and within whistling range: it travels with you.\r\n"
    "; 0 = you can also go when your horse is too far away, and you arrive on foot.\r\n"
    "RequireHorseNearby=1\r\n"
    "; 1 = allow fast travel while you're wanted (a campfire doesn't)\r\n"
    "AllowWhenWanted=0\r\n"
    "; 1 = allow fast travel during a mission (a campfire doesn't)\r\n"
    "AllowOnMissions=0\r\n"
    "\r\n"
    "; 1 = show Fast Travel greyed out when you can't use it; pressing it says why.\r\n"
    "; 0 = hide it instead, as a campfire does.\r\n"
    "ShowWhenUnavailable=1\r\n"
    "; 1 = Arthur stands up as the screen fades out\r\n"
    "StandUp=1\r\n"
    "\r\n"
    "; The button, by the game's control name. INPUT_CONTEXT_A is Space (A on a controller),\r\n"
    "; the campfire's Fast Travel button. The kneel menu already uses R, E and F.\r\n"
    "PromptControl=INPUT_CONTEXT_A\r\n"
    "; Empty = the game's own \"Fast Travel\" text, in your game's language.\r\n"
    "PromptText=\r\n"
    "\r\n"
    "; 1 = show a short \"loaded\" message when Story Mode starts\r\n"
    "ShowLoadMessage=1\r\n"
    "; 1 = write more detail to FastTravelAnywhere.log\r\n"
    "DebugLog=0\r\n"
    "\r\n"
    "; Experimental. If the fast travel menu closes when you scroll it with W/S or the left stick\r\n"
    "; (Arthur gets up from the kneel), 1 switches the controls to menu-only while it's open.\r\n"
    "LockMovementInMenu=0\r\n";

static void Trim(char* s)
{
    char* p = s;
    while (*p == ' ' || *p == '\t')
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
}

static bool ParseBool(const char* v, bool def)
{
    if (!_stricmp(v, "1") || !_stricmp(v, "true") || !_stricmp(v, "yes") || !_stricmp(v, "on"))
        return true;
    if (!_stricmp(v, "0") || !_stricmp(v, "false") || !_stricmp(v, "no") || !_stricmp(v, "off"))
        return false;
    return def;
}

// "INPUT_CONTEXT_A", "0x5181713D" or a decimal hash.
static uint32_t ParseControl(const char* v)
{
    if (!v[0])
        return 0;
    if ((v[0] == '0') && (v[1] == 'x' || v[1] == 'X'))
        return (uint32_t)strtoul(v + 2, nullptr, 16);
    bool digits = true;
    for (const char* p = v; *p; p++)
        if (*p < '0' || *p > '9')
            digits = false;
    if (digits)
        return (uint32_t)strtoul(v, nullptr, 10);
    return Joaat(v);
}

static FILETIME g_iniStamp = { 0, 0 };

static bool IniChanged()
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &a))
        return g_iniStamp.dwLowDateTime != 0 || g_iniStamp.dwHighDateTime != 0;
    return CompareFileTime(&a.ftLastWriteTime, &g_iniStamp) != 0;
}

static void LoadSettings()
{
    DefaultSettings(g_cfg);

    FILE* f = fopen(g_iniPath, "rb");
    if (!f)
    {
        FILE* w = fopen(g_iniPath, "wb");
        if (w)
        {
            fwrite(kDefaultIni, 1, sizeof kDefaultIni - 1, w);
            fclose(w);
            Log("Settings: %s was missing, wrote the defaults.", g_iniPath);
        }
        else
        {
            Log("Settings: no %s and it couldn't be created; using the defaults.", g_iniPath);
        }
    }
    else
    {
        char line[512];
        while (fgets(line, sizeof line, f))
        {
            Trim(line);
            if (!line[0] || line[0] == ';' || line[0] == '#' || line[0] == '[')
                continue;
            char* eq = strchr(line, '=');
            if (!eq)
                continue;
            *eq = 0;
            char* key = line;
            char* val = eq + 1;
            Trim(key);
            Trim(val);

            if      (!_stricmp(key, "Enabled"))             g_cfg.enabled = ParseBool(val, g_cfg.enabled);
            else if (!_stricmp(key, "RequireUnlock"))       g_cfg.requireUnlock = ParseBool(val, g_cfg.requireUnlock);
            else if (!_stricmp(key, "RequireHorseNearby"))  g_cfg.requireHorseNearby = ParseBool(val, g_cfg.requireHorseNearby);
            else if (!_stricmp(key, "AllowWhenWanted"))     g_cfg.allowWhenWanted = ParseBool(val, g_cfg.allowWhenWanted);
            else if (!_stricmp(key, "AllowOnMissions"))     g_cfg.allowOnMissions = ParseBool(val, g_cfg.allowOnMissions);
            else if (!_stricmp(key, "ShowWhenUnavailable")) g_cfg.showWhenUnavailable = ParseBool(val, g_cfg.showWhenUnavailable);
            else if (!_stricmp(key, "StandUp"))             g_cfg.standUp = ParseBool(val, g_cfg.standUp);
            else if (!_stricmp(key, "ShowLoadMessage"))     g_cfg.showLoadMessage = ParseBool(val, g_cfg.showLoadMessage);
            else if (!_stricmp(key, "DebugLog"))            g_cfg.debugLog = ParseBool(val, g_cfg.debugLog);
            else if (!_stricmp(key, "LockMovementInMenu"))  g_cfg.lockMovementInMenu = ParseBool(val, g_cfg.lockMovementInMenu);
            else if (!_stricmp(key, "PromptText"))
            {
                strncpy(g_cfg.promptText, val, sizeof g_cfg.promptText - 1);
                g_cfg.promptText[sizeof g_cfg.promptText - 1] = 0;
            }
            else if (!_stricmp(key, "PromptControl"))
            {
                uint32_t c = ParseControl(val);
                if (c)
                {
                    g_cfg.promptControl = c;
                    strncpy(g_cfg.promptControlName, val, sizeof g_cfg.promptControlName - 1);
                    g_cfg.promptControlName[sizeof g_cfg.promptControlName - 1] = 0;
                }
            }
        }
        fclose(f);
    }

    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &a))
        g_iniStamp = a.ftLastWriteTime;

    Log("Settings: Enabled=%d RequireUnlock=%d RequireHorseNearby=%d AllowWhenWanted=%d "
        "AllowOnMissions=%d ShowWhenUnavailable=%d StandUp=%d PromptControl=%s (0x%08X) "
        "PromptText=\"%s\" DebugLog=%d LockMovementInMenu=%d",
        g_cfg.enabled, g_cfg.requireUnlock, g_cfg.requireHorseNearby, g_cfg.allowWhenWanted,
        g_cfg.allowOnMissions, g_cfg.showWhenUnavailable, g_cfg.standUp, g_cfg.promptControlName,
        g_cfg.promptControl, g_cfg.promptText, g_cfg.debugLog, g_cfg.lockMovementInMenu);
}

// ===========================================================================
// Script Hook RDR2 binding (resolved at runtime, no .lib needed)
// ===========================================================================

typedef void      (*ScriptMainFn)();
typedef void      (*PFN_scriptRegister)(HMODULE, ScriptMainFn);
typedef void      (*PFN_scriptUnregister)(HMODULE);
typedef void      (*PFN_scriptWait)(DWORD);
typedef void      (*PFN_nativeInit)(uint64_t);
typedef void      (*PFN_nativePush64)(uint64_t);
typedef uint64_t* (*PFN_nativeCall)();
typedef uint64_t* (*PFN_getGlobalPtr)(int);
typedef int       (*PFN_getGameVersion)();

static PFN_scriptRegister   SH_scriptRegister   = nullptr;
static PFN_scriptUnregister SH_scriptUnregister = nullptr;
static PFN_scriptWait       SH_scriptWait       = nullptr;
static PFN_nativeInit       SH_nativeInit       = nullptr;
static PFN_nativePush64     SH_nativePush64     = nullptr;
static PFN_nativeCall       SH_nativeCall       = nullptr;
static PFN_getGlobalPtr     SH_getGlobalPtr     = nullptr;
static PFN_getGameVersion   SH_getGameVersion   = nullptr;
static bool                 g_registered        = false;

// Script Hook exports C++ (MSVC-mangled) names. Try the exact name first, then
// fall back to scanning the export table by function-name prefix, so a small
// signature difference in a future Script Hook build still binds.
static FARPROC FindExport(HMODULE mod, const char* exact, const char* prefix,
                          const char* mustContain)
{
    FARPROC p = GetProcAddress(mod, exact);
    if (p)
        return p;

    BYTE* base = (BYTE*)mod;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return nullptr;
    IMAGE_DATA_DIRECTORY dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress || !dir.Size)
        return nullptr;

    IMAGE_EXPORT_DIRECTORY* exp = (IMAGE_EXPORT_DIRECTORY*)(base + dir.VirtualAddress);
    DWORD* names = (DWORD*)(base + exp->AddressOfNames);
    WORD*  ords  = (WORD*)(base + exp->AddressOfNameOrdinals);
    DWORD* funcs = (DWORD*)(base + exp->AddressOfFunctions);
    size_t plen  = strlen(prefix);

    for (DWORD i = 0; i < exp->NumberOfNames; i++)
    {
        const char* name = (const char*)(base + names[i]);
        if (strncmp(name, prefix, plen) != 0)
            continue;
        if (mustContain && !strstr(name, mustContain))
            continue;
        Log("Bound %s via export scan (%s)", prefix, name);
        return (FARPROC)(base + funcs[ords[i]]);
    }
    return nullptr;
}

static bool BindScriptHook()
{
    HMODULE sh = GetModuleHandleA("ScriptHookRDR2.dll");
    if (!sh)
        sh = LoadLibraryA("ScriptHookRDR2.dll");
    if (!sh)
    {
        Log("ERROR: ScriptHookRDR2.dll was not found next to RDR2.exe. "
            "Install Script Hook RDR2 (dev-c.com) first.");
        return false;
    }

    SH_scriptRegister = (PFN_scriptRegister)FindExport(
        sh, "?scriptRegister@@YAXPEAUHINSTANCE__@@P6AXXZ@Z", "?scriptRegister@@", nullptr);
    SH_scriptUnregister = (PFN_scriptUnregister)FindExport(
        sh, "?scriptUnregister@@YAXPEAUHINSTANCE__@@@Z", "?scriptUnregister@@", "HINSTANCE");
    SH_scriptWait = (PFN_scriptWait)FindExport(
        sh, "?scriptWait@@YAXK@Z", "?scriptWait@@", nullptr);
    SH_nativeInit = (PFN_nativeInit)FindExport(
        sh, "?nativeInit@@YAX_K@Z", "?nativeInit@@", nullptr);
    SH_nativePush64 = (PFN_nativePush64)FindExport(
        sh, "?nativePush64@@YAX_K@Z", "?nativePush64@@", nullptr);
    SH_nativeCall = (PFN_nativeCall)FindExport(
        sh, "?nativeCall@@YAPEA_KXZ", "?nativeCall@@", nullptr);
    SH_getGlobalPtr = (PFN_getGlobalPtr)FindExport(
        sh, "?getGlobalPtr@@YAPEA_KH@Z", "?getGlobalPtr@@", nullptr);
    SH_getGameVersion = (PFN_getGameVersion)FindExport(
        sh, "?getGameVersion@@YA?AW4eGameVersion@@XZ", "?getGameVersion@@", nullptr);

    if (!SH_scriptRegister || !SH_scriptWait || !SH_nativeInit ||
        !SH_nativePush64 || !SH_nativeCall)
    {
        Log("ERROR: could not find required Script Hook functions "
            "(register=%p wait=%p init=%p push=%p call=%p). "
            "Your Script Hook RDR2 may be too old or too new for this build.",
            (void*)SH_scriptRegister, (void*)SH_scriptWait, (void*)SH_nativeInit,
            (void*)SH_nativePush64, (void*)SH_nativeCall);
        return false;
    }
    if (!SH_getGlobalPtr)
        Log("ERROR: Script Hook has no getGlobalPtr. Fast travel can't be started without it.");
    return true;
}

// ===========================================================================
// Native calls
// ===========================================================================

template <typename T>
static inline void PushArg(T v)
{
    static_assert(sizeof(T) <= sizeof(uint64_t), "native argument too large");
    uint64_t raw = 0;
    memcpy(&raw, &v, sizeof(T)); // floats occupy the low 4 bytes, like the SDK
    SH_nativePush64(raw);
}

static inline void PushArgs() {}

template <typename T, typename... Rest>
static inline void PushArgs(T first, Rest... rest)
{
    PushArg(first);
    PushArgs(rest...);
}

template <typename R, typename... A>
static inline R Invoke(uint64_t hash, A... args)
{
    SH_nativeInit(hash);
    PushArgs(args...);
    uint64_t* res = SH_nativeCall();
    R out;
    memcpy(&out, res, sizeof(R));
    return out;
}

template <typename... A>
static inline void InvokeVoid(uint64_t hash, A... args)
{
    SH_nativeInit(hash);
    PushArgs(args...);
    SH_nativeCall();
}

// Hashes from alloc8or's RDR3 native DB (github.com/alloc8or/rdr3-nativedb-data).
namespace N
{
    // A Vector3 the way the Script Hook SDK lays it out: three floats, each in an 8-byte slot.
    struct Vec3 { float x; uint32_t padX; float y; uint32_t padY; float z; uint32_t padZ; };

    static inline int  PlayerPedId()             { return Invoke<int>(0x096275889B8E0EE0ULL); }                    // PLAYER::PLAYER_PED_ID
    static inline int  PlayerId()                { return Invoke<int>(0x217E9DC48139933DULL); }                    // PLAYER::PLAYER_ID
    static inline bool IsEntityDead(int e)       { return Invoke<int>(0x7D5B1F88E7504BBAULL, e) != 0; }           // ENTITY::IS_ENTITY_DEAD
    static inline bool DoesEntityExist(int e)    { return Invoke<int>(0xD42BD6EB2E0F1677ULL, e) != 0; }           // ENTITY::DOES_ENTITY_EXIST
    static inline Vec3 EntityCoords(int e)       { return Invoke<Vec3>(0xA86D5F069399F44DULL, e, 1, 0); }         // ENTITY::GET_ENTITY_COORDS(entity, alive, realCoords)
    static inline bool IsEntityAPed(int e)       { return Invoke<int>(0xCF8176912DDA4EA5ULL, e) != 0; }           // ENTITY::IS_ENTITY_A_PED
    static inline bool IsPedInWrithe(int p)      { return Invoke<int>(0xDEB6D52126E7D640ULL, p) != 0; }           // TASK::IS_PED_IN_WRITHE
    static inline bool IsPedHuman(int p)         { return Invoke<int>(0xB980061DA992779DULL, p) != 0; }           // PED::IS_PED_HUMAN
    static inline bool IsModelValid(uint32_t m)  { return Invoke<int>(0x392C8D8E07B70EFCULL, m) != 0; }           // STREAMING::IS_MODEL_VALID
    static inline bool IsPedInAnyVehicle(int p)  { return Invoke<int>(0x997ABD671D25CA0BULL, p, 0) != 0; }        // PED::IS_PED_IN_ANY_VEHICLE
    static inline bool IsPedInAnyTrain(int p)    { return Invoke<int>(0x6F972C1AB75A1ED0ULL, p) != 0; }           // PED::IS_PED_IN_ANY_TRAIN
    static inline bool IsPedOnMount(int p)       { return Invoke<int>(0x460BC76A0E10655EULL, p) != 0; }           // PED::IS_PED_ON_MOUNT

    static inline int  ScriptThreads(uint32_t h) { return Invoke<int>(0x8E34C953364A76DDULL, h); }                // SCRIPT::GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH

    // The kneel. dynamic_craft_scenario shows its prompts once the kneel's
    // animation event fires or IS_PED_ACTIVE_IN_SCENARIO(player, 0), while the
    // player has no interaction target.
    static inline bool IsPedActiveInScenario(int p) { return Invoke<int>(0xAA135F9482C82CC3ULL, p, 0) != 0; }     // TASK::IS_PED_ACTIVE_IN_SCENARIO
    static inline bool IsPedExitingScenario(int p)  { return Invoke<int>(0x0C3CB2E600C8977DULL, p, 1) != 0; }     // TASK::IS_PED_EXITING_SCENARIO
    static inline bool IsPedRunningItemInteraction(int p) { return Invoke<int>(0xEC7E480FF8BD0BEDULL, p) != 0; } // TASK::IS_PED_RUNNING_TASK_ITEM_INTERACTION
    static inline bool PedHasUseScenarioTask(int p) { return Invoke<int>(0x295E3CCEC879CCD7ULL, p) != 0; }        // TASK::PED_HAS_USE_SCENARIO_TASK
    static inline bool PedIsInScenarioBase(int p)   { return Invoke<int>(0x02EBBB3989B7E695ULL, p) != 0; }        // TASK::_PED_IS_IN_SCENARIO_BASE
    static inline bool PlayerHasInteractionTarget(int player)
    {
        int64_t target = 0;  // Entity* out-parameter: give it a full 8-byte slot
        return Invoke<int>(0x3EE1F7A8C32F24E1ULL, player, &target, 0, 0) != 0;                                    // PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY
    }

    // Prompts
    static inline int  PromptRegisterBegin()                  { return Invoke<int>(0x04F97DE45A519419ULL); }      // HUD::_UI_PROMPT_REGISTER_BEGIN
    static inline void PromptSetControlAction(int p, uint32_t a) { InvokeVoid(0xB5352B7494A08258ULL, p, a); }     // HUD::_UI_PROMPT_SET_CONTROL_ACTION
    static inline void PromptSetText(int p, const char* t)    { InvokeVoid(0x5DD02A8318420DD7ULL, p, t); }        // HUD::_UI_PROMPT_SET_TEXT
    static inline void PromptSetPriority(int p, int v)        { InvokeVoid(0xCA24F528D0D16289ULL, p, v); }        // HUD::_UI_PROMPT_SET_PRIORITY
    static inline void PromptSetTransportMode(int p, int v)   { InvokeVoid(0x876E4A35C73A6655ULL, p, v); }        // HUD::_UI_PROMPT_SET_TRANSPORT_MODE
    static inline void PromptSetAttribute(int p, int a, int on) { InvokeVoid(0x560E76D5E2E1803FULL, p, a, on); }  // HUD::_UI_PROMPT_SET_ATTRIBUTE
    static inline void PromptSetStandardMode(int p, int release) { InvokeVoid(0xCC6656799977741BULL, p, release); } // HUD::_UI_PROMPT_SET_STANDARD_MODE
    static inline void PromptRegisterEnd(int p)               { InvokeVoid(0xF7AA2696A22AD8B9ULL, p); }           // HUD::_UI_PROMPT_REGISTER_END
    static inline void PromptSetGroup(int p, int g, int tab)  { InvokeVoid(0x2F11D3A254169EA4ULL, p, g, tab); }   // HUD::_UI_PROMPT_SET_GROUP
    static inline void PromptSetEnabled(int p, int on)        { InvokeVoid(0x8A0FB4D03A630D21ULL, p, on); }       // HUD::_UI_PROMPT_SET_ENABLED
    static inline void PromptSetVisible(int p, int on)        { InvokeVoid(0x71215ACCFDE075EEULL, p, on); }       // HUD::_UI_PROMPT_SET_VISIBLE
    static inline bool PromptIsJustReleased(int p)            { return Invoke<int>(0x635CC82FA297A827ULL, p) != 0; } // HUD::_UI_PROMPT_IS_JUST_RELEASED
    static inline bool PromptIsValid(int p)                   { return Invoke<int>(0x347469FBDD1589A9ULL, p) != 0; } // HUD::_UI_PROMPT_IS_VALID
    static inline void PromptDelete(int p)                    { InvokeVoid(0x00EDE88D4D13CF59ULL, p); }           // HUD::_UI_PROMPT_DELETE

    // UI apps
    static inline bool IsUiAppActive(uint32_t h)   { return Invoke<int>(0x25B7A0206BDFAC76ULL, h) != 0; }        // UIAPPS::IS_UIAPP_ACTIVE_BY_HASH
    static inline bool CanLaunchUiApp(uint32_t h)  { return Invoke<int>(0xE555EC27D65EDE80ULL, h) != 0; }        // UIAPPS::CAN_LAUNCH_UIAPP_BY_HASH

    // Text
    static inline const char* VarStringLiteral(const char* text)
    { return Invoke<const char*>(0xFA925AC00EB830B9ULL, 10, "LITERAL_STRING", text); }                           // MISC::VAR_STRING
    static inline int  PostHelpText(int64_t* data, int64_t* text, int immediately)
    { return Invoke<int>(0x049D5C615BD38BADULL, data, text, immediately); }                                       // UIFEED::_UI_FEED_POST_HELP_TEXT
    static inline bool TextBlockIsLoaded(const char* b)       { return Invoke<int>(0xD0976CC34002DB57ULL, b) != 0; } // HUD::TEXT_BLOCK_IS_LOADED
    static inline void TextBlockRequest(const char* b)        { InvokeVoid(0xF66090013DE648D5ULL, b); }           // HUD::TEXT_BLOCK_REQUEST
    static inline void TextBlockDelete(const char* b)         { InvokeVoid(0xAA03F130A637D923ULL, b); }           // HUD::_TEXT_BLOCK_DELETE
    static inline bool DoesTextBlockExist(const char* b)      { return Invoke<int>(0x2C729F2B94CEA911ULL, b) != 0; } // HUD::_DOES_TEXT_BLOCK_EXIST
    static inline bool DoesTextLabelExist(const char* l)      { return Invoke<int>(0x73C258C68D6F55B6ULL, l) != 0; } // HUD::DOES_TEXT_LABEL_EXIST
    static inline const char* LabelText(const char* l)        { return Invoke<const char*>(0x3429670F9B9EF2D3ULL, l); } // HUD::_GET_LABEL_TEXT_2
    static inline void BgSetTextScale(float sx, float sy)     { InvokeVoid(0xA1253A3C870B6843ULL, sx, sy); }      // UIDEBUG::_BG_SET_TEXT_SCALE
    static inline void BgSetTextColor(int r, int g, int b, int a) { InvokeVoid(0x16FA5CE47F184F1EULL, r, g, b, a); } // UIDEBUG::_BG_SET_TEXT_COLOR
    static inline void BgDisplayText(const char* vs, float x, float y) { InvokeVoid(0x16794E044C9EFB58ULL, vs, x, y); } // UIDEBUG::_BG_DISPLAY_TEXT

    // Game state
    static inline int  GameTimer()                 { return Invoke<int>(0x4F67E8ECA7D3F667ULL); }                // MISC::GET_GAME_TIMER
    static inline bool MissionFlag()               { return Invoke<int>(0xB15CD1CF58771DE1ULL) != 0; }           // MISC::GET_MISSION_FLAG
    static inline bool IsLawIncidentActive(int pl) { return Invoke<int>(0xAD401C63158ACBAAULL, pl) != 0; }       // LAW::IS_LAW_INCIDENT_ACTIVE
    static inline bool IsPauseMenuActive()         { return Invoke<int>(0x535384D6067BA42EULL) != 0; }           // HUD::IS_PAUSE_MENU_ACTIVE
    static inline bool IsLoadingScreenActive()     { return Invoke<int>(0x71D4BF5890659B0CULL) != 0; }           // DLC::GET_IS_LOADING_SCREEN_ACTIVE
    static inline bool NetworkIsSessionStarted()   { return Invoke<int>(0x9DE624D2FC4B603FULL) != 0; }           // NETWORK::NETWORK_IS_SESSION_STARTED
    static inline bool NetworkIsGameInProgress()   { return Invoke<int>(0x10FAB35428CCC9D7ULL) != 0; }           // NETWORK::NETWORK_IS_GAME_IN_PROGRESS

    // Controls (pad 0, as the game's scripts use)
    static inline void DisableControlAction(uint32_t a)        { InvokeVoid(0xFE99B66D079CF6BCULL, 0, a, 0); }  // PAD::DISABLE_CONTROL_ACTION
    static inline void DisableAllControlActions()              { InvokeVoid(0x5F4B6931816E599BULL, 0); }        // PAD::DISABLE_ALL_CONTROL_ACTIONS
    static inline void EnableControlAction(uint32_t a)         { InvokeVoid(0x351220255D64C155ULL, 0, a, 1); }  // PAD::ENABLE_CONTROL_ACTION
    static inline float DisabledControlNormal(uint32_t a)      { return Invoke<float>(0x11E65974A982637CULL, 0, a); } // PAD::GET_DISABLED_CONTROL_NORMAL
    static inline void SetControlContext(int layer, uint32_t ctx) { InvokeVoid(0x2804658EB7D8A50BULL, layer, ctx); } // PAD::_SET_CONTROL_CONTEXT (every frame)
    static inline bool IsDisabledControlJustReleased(uint32_t a) { return Invoke<int>(0x305C8DCD79DA8B0FULL, 0, a) != 0; } // PAD::IS_DISABLED_CONTROL_JUST_RELEASED
    static inline bool IsControlActionValid(uint32_t a)        { return Invoke<int>(0xBC0884BC590951C7ULL, a, 0) != 0; } // PAD::_IS_CONTROL_ACTION_VALID(action, control)

    // Screen
    static inline void FadeOut(int ms)  { InvokeVoid(0x40C719A5E410B9E4ULL, ms); }                               // CAMERA::DO_SCREEN_FADE_OUT
    static inline void FadeIn(int ms)   { InvokeVoid(0x6A053CF596F67DF7ULL, ms); }                               // CAMERA::DO_SCREEN_FADE_IN
    static inline bool IsFadedOut()     { return Invoke<int>(0xF5472C80DF2FF847ULL) != 0; }                      // CAMERA::IS_SCREEN_FADED_OUT
    static inline bool IsFadingOut()    { return Invoke<int>(0x02F39BEFE7B88D00ULL) != 0; }                      // CAMERA::IS_SCREEN_FADING_OUT
    static inline bool IsFadingIn()     { return Invoke<int>(0x0CECCC63FFA2EF24ULL) != 0; }                      // CAMERA::IS_SCREEN_FADING_IN
    static inline bool IsFadedIn()      { return Invoke<int>(0x37F9A426FBCF4AF2ULL) != 0; }                      // CAMERA::IS_SCREEN_FADED_IN

    // The player
    static inline void GivePedHashCommand(int p, uint32_t cmd, float t) { InvokeVoid(0xD65FDC686A031C83ULL, p, cmd, t); } // PED::_GIVE_PED_HASH_COMMAND
    static inline void ClearPedTasksImmediately(int p)        { InvokeVoid(0xAAA34F8A7CB32098ULL, p, 0, 1); }     // TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped, p1, resetCrouch)
    static inline void SetPedConfigFlag(int p, int flag, int on) { InvokeVoid(0x1913FE4CBF41C463ULL, p, flag, on); } // PED::SET_PED_CONFIG_FLAG
    static inline void TriggerMusicEvent(const char* e)       { Invoke<int>(0x706D57B0F50DA710ULL, e); }          // AUDIO::TRIGGER_MUSIC_EVENT

    // Horse bonding and whistle range (short_update's func_511)
    static inline int   DefaultMaxAttributeRank(uint32_t m, int attr)      { return Invoke<int>(0x7C059C55AD940CB4ULL, m, attr); }       // ATTRIBUTE::GET_DEFAULT_MAX_ATTRIBUTE_RANK
    static inline int   PointsNeededForRank(uint32_t m, int attr, int r)   { return Invoke<int>(0x94A7F191DB49A44DULL, m, attr, r); }   // ATTRIBUTE::GET_DEFAULT_ATTRIBUTE_POINTS_NEEDED_FOR_RANK
    static inline float WhistleRangeMin(int level)                         { return Invoke<float>(0xEB67D4E056C85A81ULL, level); }      // TASK::_GET_WHISTLE_RANGE_MIN_FOR_BONDING_LEVEL
    static inline float WhistleRangeMax(int level)                         { return Invoke<float>(0x78D8C1D4EB80C588ULL, level); }      // TASK::_GET_WHISTLE_RANGE_MAX_FOR_BONDING_LEVEL
}

static void DrawTextLine(const char* text, float x, float y, float scale, int r, int g, int b)
{
    // Drop shadow first, then the text, for readability on bright scenes.
    N::BgSetTextScale(scale, scale);
    N::BgSetTextColor(0, 0, 0, 200);
    N::BgDisplayText(N::VarStringLiteral(text), x + 0.0012f, y + 0.0018f);
    N::BgSetTextScale(scale, scale);
    N::BgSetTextColor(r, g, b, 255);
    N::BgDisplayText(N::VarStringLiteral(text), x, y);
}

// ===========================================================================
// The game's script globals (game build 1491.50)
// ===========================================================================
//
// Indices come from the decompiled scripts of build 1491.50. A struct field is
// base + offset; an array element is base + 1 + index * size (slot 0 holds the
// array's length). Values are 32-bit ints in 8-byte slots.

namespace G
{
    static const int kPlayerPed   = 35;       // Global_35: the player's ped (every script caches it here)
    static const int kFtFlags     = 43907;    // Global_43907: fast travel requests between the camp scripts and short_update
    static const int kFtState     = 43908;    // Global_43908: short_update's fast travel state
    static const int kCampFlags   = 1392040;  // Global_1392040: camp flags; bit 23 = at a campfire, camp actions allowed
    static const int kUiBusy      = 1898068;  // Global_1898068 and .f_4: a game menu is up (the camp checks both)
    static const int kUiBusy4     = 1898072;
    static const int kActiveHorse = 1135;     // Global_40.f_1095: slot of the active horse (0-6, -1 = none)
    static const int kHorseSaves  = 1136;     // Global_40.f_1095.f_1[slot /*436*/]: saved horse data
    static const int kHorsePeds   = 1900383;  // Global_1900383[slot /*45*/]: the horse peds
    static const int kLodging     = 4644;     // Global_40.f_4283.f_321: lodging upgrade level (3+ = fast travel map)
    static const int kSession     = 1572899;  // Global_1572887.f_12: -1 in Story Mode
    static const int kUnlockFlow  = 1351050;  // Global_1347702[68 /*49*/].f_15: the flow behind the other unlock (func_339(68))
    static const int kFlowStatus  = 12106;    // Global_12106[flow /*7*/].f_1: flow status, 5 or 6 = done
    static const int kKneelHud    = 1955831;  // Global_1955830.f_1: bit 2 = the kneel's prompts are up (bits 2+3: a campfire's)
    static const int kMenuStopA   = 1357517;  // Global_1357517: short_update closes the menu while set (func_503)
    static const int kMenuStopB   = 1935690;  // Global_1935689.f_1: likewise (func_504)

    // Bits of kFtFlags (short_update func_80)
    static const int FT_MUSIC  = 1;   // play the fast travel music
    static const int FT_OPEN   = 4;   // open the fast travel menu
    static const int FT_CHOSEN = 8;   // set by short_update: a place was chosen
    static const int FT_CLOSE  = 16;  // close the menu
    static const int FT_READY  = 32;  // the camp has packed up: go
    // (64 is short_update's own note that it faded the screen during the camp's
    // kick-the-fire animation; this plugin fades the screen itself.)

    static const int CAMP_AT_FIRE = 1 << 23;

    static inline uint64_t* Ptr(int idx)
    {
        return SH_getGlobalPtr ? SH_getGlobalPtr(idx) : nullptr;
    }

    static inline bool Get(int idx, int32_t* out)
    {
        uint64_t* p = Ptr(idx);
        if (!p)
            return false;
        memcpy(out, p, 4);
        return true;
    }

    static inline int Int(int idx, int fallback)
    {
        int32_t v;
        return Get(idx, &v) ? v : fallback;
    }

    static inline float Float(int idx, float fallback)
    {
        uint64_t* p = Ptr(idx);
        if (!p)
            return fallback;
        float f;
        memcpy(&f, p, 4);
        return f;
    }

    static inline bool Set(int idx, int32_t v)
    {
        uint64_t* p = Ptr(idx);
        if (!p)
            return false;
        memcpy(p, &v, 4);
        return true;
    }
}

// ===========================================================================
// Hashes
// ===========================================================================

static const int kRestGroup = 342152817; // the kneel menu's prompt group ("REST_UI_GROUP")

static uint32_t H_DYNAMIC_CRAFT, H_SHORT_UPDATE, H_CAMERA_ITEM, H_SIMPLE_CRAFTING, H_JOURNAL;
static uint32_t H_PLAYER_CAMP, H_INTERACTIVE_CAMPFIRE, H_PROPERTY_USE, H_CAMPFIRE_ALWAYS, H_SLEEPING;
static uint32_t H_FAST_TRAVEL_MENU, H_PLAYER_MENU;
static uint32_t H_INPUT_CONTEXT_X, H_INPUT_CONTEXT_Y, H_INPUT_JUMP;
static uint32_t H_INPUT_MOVE_LR, H_INPUT_MOVE_UD, H_FRONTEND_PAUSE, H_FRONTEND_PAUSE_ALT, H_CTX_FRONTEND_MENU;
static uint32_t H_KNEEL_EXIT;

static void InitHashes()
{
    H_DYNAMIC_CRAFT    = Joaat("dynamic_craft_scenario");
    H_SHORT_UPDATE     = Joaat("short_update");
    H_CAMERA_ITEM      = Joaat("CAMERA_ITEM");
    H_SIMPLE_CRAFTING  = Joaat("SIMPLE_CRAFTING");
    H_JOURNAL          = Joaat("DOC_PLAYER_JOURNAL");
    H_PLAYER_CAMP      = Joaat("player_camp");
    H_INTERACTIVE_CAMPFIRE = Joaat("interactive_campfire");
    H_PROPERTY_USE     = Joaat("property_use_core");
    H_CAMPFIRE_ALWAYS  = Joaat("campfire_always");
    H_SLEEPING         = Joaat("sleeping_scenario");
    H_FAST_TRAVEL_MENU = Joaat("FAST_TRAVEL_MENU");
    H_PLAYER_MENU      = Joaat("PLAYER_MENU");
    H_INPUT_CONTEXT_X  = Joaat("INPUT_CONTEXT_X");
    H_INPUT_CONTEXT_Y  = Joaat("INPUT_CONTEXT_Y");
    H_INPUT_JUMP       = Joaat("INPUT_JUMP");
    H_INPUT_MOVE_LR    = Joaat("INPUT_MOVE_LR");
    H_INPUT_MOVE_UD    = Joaat("INPUT_MOVE_UD");
    H_FRONTEND_PAUSE   = Joaat("INPUT_FRONTEND_PAUSE");
    H_FRONTEND_PAUSE_ALT = Joaat("INPUT_FRONTEND_PAUSE_ALTERNATE");
    H_CTX_FRONTEND_MENU  = Joaat("FrontendMenu");
    H_KNEEL_EXIT       = Joaat("DYNAMIC_KNEEL_SLOW_EXIT");
}

// ===========================================================================
// The game's texts: the CAMP text block
// ===========================================================================
//
// The campfire's texts (the Fast Travel prompt CAMP_FT_PROMPT, the horse and
// region messages, the fast travel menu's heading) live in the game's CAMP
// text block. Only the camp scripts load it (player_camp, interactive_campfire,
// property_use_core, campfire_always, each unloading it when it's done; and
// long_update, again and again while a campfire you left is still burning),
// and the kneel menu's own Craft menu (simple_crafting) loads it when it opens
// and unloads it when it closes. A prompt whose label isn't loaded has no
// text, and the game doesn't draw it, although its button still works. So the
// plugin
//   * asks for CAMP as soon as a kneel starts, and lets it go again once the
//     kneel is over, if the plugin loaded it and nothing else is using it (as
//     simple_crafting does);
//   * uses the label only while it's loaded, and otherwise the same words as
//     plain text: the game's own, read from the label while it was loaded, or
//     English until then.

static const char kCampText[]  = "CAMP";
static const char kFtLabel[]   = "CAMP_FT_PROMPT";   // "Fast Travel"
static const char kFtEnglish[] = "Fast Travel";

static bool g_campTextOurs      = false;  // the plugin loaded it
static int  g_campTextAskedAt   = -1;     // when the plugin last asked for it
static bool g_campTextSeen      = false;  // seen loaded since then
static int  g_campTextHoldUntil = 0;      // a help text with a CAMP label is on screen until then
static char g_ftText[64]        = "";     // the game's own words for CAMP_FT_PROMPT, in its language

static bool CampTextLoaded()
{
    return N::TextBlockIsLoaded(kCampText);
}

// A label from the CAMP text can be shown right now.
static bool CampLabelReady(const char* label)
{
    return CampTextLoaded() && N::DoesTextLabelExist(label);
}

static void RequestCampText(int now)
{
    if (CampTextLoaded())
    {
        g_campTextSeen = true;
        return;
    }
    if (!N::DoesTextBlockExist(kCampText))
        return;
    // Asked again at once if it was loaded and has gone since (the Craft menu
    // unloads it); while a request is still loading, every 1.5 s at most.
    if (g_campTextAskedAt < 0 || g_campTextSeen || now - g_campTextAskedAt > 1500)
    {
        N::TextBlockRequest(kCampText);
        if (g_campTextAskedAt < 0)
            Log("Loading the game's CAMP text (the \"Fast Travel\" label).");
        g_campTextAskedAt = now;
        g_campTextSeen = false;
        g_campTextOurs = true;
    }
}

static void ReleaseCampText(int now)
{
    if (!g_campTextOurs || now - g_campTextHoldUntil < 0)
        return;
    // Something else is using it now. The camp scripts and the Craft menu
    // unload it themselves when they're done.
    if (N::ScriptThreads(H_PLAYER_CAMP) > 0 || N::ScriptThreads(H_INTERACTIVE_CAMPFIRE) > 0 ||
        N::ScriptThreads(H_PROPERTY_USE) > 0 || N::ScriptThreads(H_CAMPFIRE_ALWAYS) > 0 ||
        N::ScriptThreads(H_SIMPLE_CRAFTING) > 0 || N::ScriptThreads(H_SLEEPING) > 0)
        return;
    if (CampTextLoaded())
    {
        N::TextBlockDelete(kCampText);
        if (g_cfg.debugLog)
            Log("Unloaded the CAMP text again.");
    }
    else if (!g_campTextSeen && g_campTextAskedAt >= 0 && now - g_campTextAskedAt < 10000)
        return;   // the request is still loading: unload it once it's in, rather than leave it behind
    g_campTextOurs = false;
    g_campTextAskedAt = -1;
    g_campTextSeen = false;
}

// Keep the game's own words for "Fast Travel", in the game's language
// (call while CampLabelReady(kFtLabel)). False if they couldn't be read.
static bool ReadFtText()
{
    const char* s = N::LabelText(kFtLabel);
    if (!s || !s[0] || strlen(s) >= sizeof g_ftText || strcmp(s, "NULL") == 0 || strcmp(s, kFtLabel) == 0)
        return false;
    if (strcmp(s, g_ftText) != 0)
    {
        snprintf(g_ftText, sizeof g_ftText, "%s", s);
        Log("The game's text for Fast Travel: \"%s\".", g_ftText);
    }
    return true;
}

// ===========================================================================
// On-screen messages
// ===========================================================================

static char      g_noteText[256];
static ULONGLONG g_noteUntil = 0;
static int       g_noteR = 255, g_noteG = 230, g_noteB = 160;

static void Notify(int ms, bool warning, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_noteText, sizeof g_noteText, fmt, ap);
    va_end(ap);
    g_noteUntil = GetTickCount64() + (ULONGLONG)ms;
    g_noteR = 255;
    g_noteG = warning ? 140 : 230;
    g_noteB = warning ? 110 : 160;
}

static void DrawNotification()
{
    if (GetTickCount64() < g_noteUntil)
        DrawTextLine(g_noteText, 0.015f, 0.015f, 0.38f, g_noteR, g_noteG, g_noteB);
}

// The game's help box (top left), as the camp scripts post it (their func_58):
// a duration struct and a text struct whose second field is the text, either a
// text label from the game (shown in the game's language) or a VAR_STRING.
// label is one of the CAMP text's, used only while it's loaded; otherwise the
// same words go in as plain English text.
static void HelpText(const char* label, const char* literal)
{
    int64_t data[4] = { 8000, 0, 0, 0 };
    int64_t text[2] = { 0, 0 };
    bool useLabel = label && CampLabelReady(label);
    const char* s = useLabel ? label : N::VarStringLiteral(literal);
    text[1] = (int64_t)(intptr_t)s;
    N::PostHelpText(data, text, 1);
    if (useLabel)
        g_campTextHoldUntil = N::GameTimer() + 8500;   // keep the text loaded while it's on screen
    Log("Help text: %s%s", useLabel ? label : literal, (label && !useLabel) ? " (label not loaded)" : "");
}

// ===========================================================================
// Why fast travel isn't available right now
// ===========================================================================

enum Reason
{
    R_OK = 0,
    R_BROKEN,      // the game's fast travel didn't respond earlier: off for this session
    R_UNSUPPORTED, // the script globals don't look like game build 1491.50
    R_NOT_READY,   // short_update isn't running
    R_NO_SCENARIO, // the kneel isn't a scenario task short_update accepts (its func_502)
    R_BUSY,        // a fast travel is already under way, or a game menu is up
    R_LOCKED,      // fast travel isn't unlocked yet
    R_MISSION,
    R_WANTED,
    R_HORSE_DEAD,
    R_HORSE_FAR,
    R_NO_HORSE,
    R_VEHICLE,
};

static const char* ReasonName(int r)
{
    switch (r)
    {
    case R_OK:         return "available";
    case R_BROKEN:     return "turned off: the game's fast travel didn't respond earlier";
    case R_UNSUPPORTED: return "off: the game's script globals don't match game build 1491.50";
    case R_NOT_READY:  return "the game's fast travel script isn't running";
    case R_NO_SCENARIO: return "the kneel isn't a scenario task the game's fast travel menu accepts (PED_HAS_USE_SCENARIO_TASK is false)";
    case R_BUSY:       return "busy (a fast travel or a game menu is under way)";
    case R_LOCKED:     return "fast travel isn't unlocked yet";
    case R_MISSION:    return "on a mission";
    case R_WANTED:     return "wanted";
    case R_HORSE_DEAD: return "horse is dead";
    case R_HORSE_FAR:  return "horse is too far away";
    case R_NO_HORSE:   return "no horse";
    case R_VEHICLE:    return "in a vehicle or on a train";
    }
    return "?";
}

static char g_brokenWhy[200];

static void ShowReason(int r)
{
    switch (r)
    {
    case R_HORSE_DEAD: HelpText("DEAD_HORSE_FT", "You can't fast travel as your horse is dead."); break;
    case R_HORSE_FAR:  HelpText("FAR_HORSE_FT", "Your horse is too far away to fast travel."); break;
    case R_NO_HORSE:   HelpText("NO_HORSE_FT", "You can't fast travel without your horse."); break;
    case R_LOCKED:
        HelpText(nullptr, "You haven't unlocked fast travel yet. It opens up the same way as at your camp "
                          "(for Arthur: the lodging upgrade in the camp ledger).");
        break;
    case R_MISSION:    HelpText(nullptr, "You can't fast travel during a mission."); break;
    case R_WANTED:     HelpText(nullptr, "You can't fast travel while you're wanted."); break;
    case R_BROKEN:     HelpText(nullptr, g_brokenWhy[0] ? g_brokenWhy :
                                "Fast Travel Anywhere is off for now. See FastTravelAnywhere.log."); break;
    case R_UNSUPPORTED:
        HelpText(nullptr, "Fast Travel Anywhere doesn't recognise this version of the game, so it stays off. "
                          "See FastTravelAnywhere.log.");
        break;
    case R_NOT_READY:
    case R_NO_SCENARIO:
    case R_BUSY:
    case R_VEHICLE:
    default:           HelpText(nullptr, "You can't fast travel right now."); break;
    }
}

// ===========================================================================
// Game checks
// ===========================================================================

static bool g_globalsOk      = false;

// Before the plugin ever writes a global, check that this game build lays them
// out as build 1491.50 does: Global_35 must be the player, and the fast travel
// globals must hold the small values short_update keeps there.
static bool CheckGlobals(int ped, char* why, size_t whySize)
{
    if (!SH_getGlobalPtr)
    {
        snprintf(why, whySize, "Script Hook has no getGlobalPtr");
        return false;
    }
    int32_t player = 0, st = 0, fl = 0, camp = 0;
    if (!G::Get(G::kPlayerPed, &player) || !G::Get(G::kFtState, &st) ||
        !G::Get(G::kFtFlags, &fl) || !G::Get(G::kCampFlags, &camp))
    {
        snprintf(why, whySize, "Script Hook returned no pointer for a script global");
        return false;
    }
    if (player != ped)
    {
        snprintf(why, whySize, "Global_35 is %d, not the player (%d): a different game build?", player, ped);
        return false;
    }
    if (st < 0 || st > 6 || fl < 0 || fl > 127)
    {
        snprintf(why, whySize, "unexpected values (fast travel state %d, flags %d, camp flags 0x%X): "
                 "a different game build?", st, fl, (unsigned)camp);
        return false;
    }
    return true;
}

// Fast travel unlocked, as the camp decides it (player_camp func_105):
// the lodging upgrade at level 3 or more, or (outside it) flow 68 done.
// Returns 1 yes, 0 no, -1 can't tell (treated as yes).
static int FastTravelUnlocked(int* lodgingOut, int* flowStatusOut)
{
    int lodging = G::Int(G::kLodging, -999);
    *lodgingOut = lodging;
    *flowStatusOut = -1;
    if (lodging >= 3 && lodging <= 20)
        return 1;
    if (G::Int(G::kSession, 0) == -1)
    {
        int flow = G::Int(G::kUnlockFlow, 0);
        if (flow > 0 && flow < 771)
        {
            int status = G::Int(G::kFlowStatus + 1 + flow * 7 + 1, -1);
            *flowStatusOut = status;
            if (status == 5 || status == 6)
                return 1;
        }
    }
    if (lodging < 0 || lodging > 20)
        return -1;
    return 0;
}

// The horses short_update can take along: the active one (slot from
// Global_40.f_1095) and slot 1. Global_1900383[slot] holds the ped.
struct HorseInfo
{
    int   slot;     // -1 = none
    int   ped;      // 0 = none
    bool  exists, dead;
    float dist, range;
};

static float Lerp(float a, float b, float t) { return (1.0f - t) * a + t * b; }

// The horse's whistle range, exactly as short_update's func_511 works it out
// from the save data for the slot: the bonding level's min..max range, by
// progress toward the next level (7 = the bonding attribute). -1 when the
// saved model isn't valid: the game then counts the horse as too far away and
// travels without it.
static float WhistleRange(int slot)
{
    int base = G::kHorseSaves + 1 + slot * 436;
    uint32_t model = (uint32_t)G::Int(base + 9, 0);
    int      level = G::Int(base + 372, -1);
    float    pts   = G::Float(base + 373, 0.0f);

    if (!N::IsModelValid(model))
        return -1.0f;
    int   maxRank = N::DefaultMaxAttributeRank(model, 7);
    float rmin    = N::WhistleRangeMin(level);
    float rmax    = N::WhistleRangeMax(level);
    if (level >= maxRank)
        return rmin;
    float need0 = (float)N::PointsNeededForRank(model, 7, level);
    float need1 = (float)N::PointsNeededForRank(model, 7, level + 1);
    if (need1 == need0)
        return rmin;
    return Lerp(rmin, rmax, (pts - need0) / (need1 - need0));
}

static HorseInfo ReadHorse(int slot, const N::Vec3& me)
{
    HorseInfo h;
    memset(&h, 0, sizeof h);
    h.slot = slot;
    h.range = -1.0f;
    if (slot < 0 || slot > 6)
        return h;
    h.ped = G::Int(G::kHorsePeds + 1 + slot * 45, 0);
    // short_update only takes an animal along (func_1595: not IS_PED_HUMAN).
    if (!h.ped || !N::DoesEntityExist(h.ped) || !N::IsEntityAPed(h.ped) || N::IsPedHuman(h.ped))
    {
        h.ped = 0;
        return h;
    }
    h.exists = true;
    h.dead   = N::IsEntityDead(h.ped) || N::IsPedInWrithe(h.ped);   // what short_update checks
    N::Vec3 p = N::EntityCoords(h.ped);
    float dx = p.x - me.x, dy = p.y - me.y;   // short_update measures flat (func_510)
    h.dist  = sqrtf(dx * dx + dy * dy);
    h.range = WhistleRange(slot);
    return h;
}

static int HorseCheck(int ped, HorseInfo* activeOut, HorseInfo* slot1Out)
{
    N::Vec3 me = N::EntityCoords(ped);
    HorseInfo a = ReadHorse(G::Int(G::kActiveHorse, -1), me);
    HorseInfo b = ReadHorse(1, me);
    if (activeOut) *activeOut = a;
    if (slot1Out)  *slot1Out = b;

    bool aliveA = a.exists && !a.dead, aliveB = b.exists && !b.dead;
    bool nearA  = aliveA && a.range >= 0.0f && a.dist <= a.range;
    bool nearB  = aliveB && b.range >= 0.0f && b.dist <= b.range;

    if (g_cfg.requireHorseNearby ? (nearA || nearB) : (aliveA || aliveB))
        return R_OK;
    // The messages, in the order the game checks them (property_use_core func_198).
    if (a.exists)
        return a.dead ? R_HORSE_DEAD : R_HORSE_FAR;
    if (b.exists)
        return b.dead ? R_HORSE_DEAD : R_HORSE_FAR;
    return R_NO_HORSE;
}

// Arthur is all the way down in the kneel: what short_update needs to keep its
// menu open (func_502(1): in the scenario's base, or active in the scenario).
static bool FullyKneeling(int ped)
{
    return N::IsPedActiveInScenario(ped) || N::PedIsInScenarioBase(ped);
}

// Everything a campfire checks before it offers Fast Travel (player_camp
// func_105 and func_306), in the order the reasons are reported.
static int Availability(int ped, int player)
{
    if (g_brokenWhy[0])
        return R_BROKEN;
    if (!g_globalsOk)
        return R_UNSUPPORTED;
    if (N::ScriptThreads(H_SHORT_UPDATE) <= 0)
        return R_NOT_READY;
    int st = G::Int(G::kFtState, -1), fl = G::Int(G::kFtFlags, -1);
    if (st != 0 || fl < 0 || (fl & (G::FT_OPEN | G::FT_CHOSEN | G::FT_READY)) != 0)
        return R_BUSY;
    if (g_cfg.requireUnlock)
    {
        int lodging, flow;
        if (FastTravelUnlocked(&lodging, &flow) == 0)
            return R_LOCKED;
    }
    if (!g_cfg.allowOnMissions && N::MissionFlag())
        return R_MISSION;
    if (!g_cfg.allowWhenWanted && N::IsLawIncidentActive(player))
        return R_WANTED;
    if (N::IsPedInAnyVehicle(ped) || N::IsPedInAnyTrain(ped) || N::IsPedOnMount(ped))
        return R_VEHICLE;
    // short_update would close the menu straight away (func_502). The kneel is a
    // use-scenario task from its first frame (dynamic_craft_scenario ends as
    // soon as the player isn't using a scenario); being all the way down, the
    // other half of func_502, is what a press waits for.
    if (!N::PedHasUseScenarioTask(ped))
        return R_NO_SCENARIO;
    int horse = HorseCheck(ped, nullptr, nullptr);
    if (horse != R_OK)
        return horse;
    // A game menu is up, or short_update would close the menu (func_503, func_504).
    if (N::IsUiAppActive(H_PLAYER_MENU) || G::Int(G::kUiBusy, 0) != 0 || G::Int(G::kUiBusy4, 0) != 0 ||
        G::Int(G::kMenuStopA, 0) != 0 || G::Int(G::kMenuStopB, 0) != 0 || !N::CanLaunchUiApp(H_FAST_TRAVEL_MENU))
        return R_BUSY;
    return R_OK;
}

// The kneel menu is up. dynamic_craft_scenario shows its "Resting" prompts as
// soon as the kneel animation's event fires, before Arthur is all the way down
// (its func_6, state 1 -> 2), and marks that with bit 2 of Global_1955830.f_1
// (the campfire scripts set bits 2 and 3 for their own prompts; the kneel
// clears 3). It clears bit 2 again when it takes its prompts down for the
// Craft menu, the journal, Set Up Camp and the like, though not for an
// interaction target or the camera (checked below). Should the bit never show
// in a kneel (a game build with the globals elsewhere), fully kneeling counts,
// as in 1.0.0.
static bool g_kneelHudSeen = false;   // bit 2 seen in this kneel

static bool KneelMenuUp(int ped, int player)
{
    if (N::ScriptThreads(H_DYNAMIC_CRAFT) <= 0)
        return false;
    if (N::IsEntityDead(ped))
        return false;
    int hud = G::Int(G::kKneelHud, 0);
    if (hud & 4)
        g_kneelHudSeen = true;
    bool kneelBit  = (hud & 4) != 0 && (hud & 8) == 0;
    bool promptsUp = kneelBit || (!g_kneelHudSeen && N::IsPedActiveInScenario(ped));
    if (!promptsUp || N::IsPedExitingScenario(ped))
        return false;
    if (N::PlayerHasInteractionTarget(player))
        return false;
    if (N::ScriptThreads(H_CAMERA_ITEM) > 0 || N::ScriptThreads(H_SIMPLE_CRAFTING) > 0 ||
        N::ScriptThreads(H_JOURNAL) > 0)
        return false;
    if (N::IsPedRunningItemInteraction(ped))
        return false;
    return true;
}

// ===========================================================================
// The Fast Travel prompt
// ===========================================================================

// What the prompt's text is.
enum PromptTextKind
{
    PT_CUSTOM,   // PromptText from the .ini
    PT_LABEL,    // the game's label CAMP_FT_PROMPT (needs the CAMP text loaded)
    PT_LITERAL,  // the game's own words for it, read from the label earlier, as plain text
    PT_ENGLISH,  // "Fast Travel" in English, until the game's own words can be read
};

static int      g_prompt        = 0;
static int      g_promptEnabled = -1;
static int      g_promptKind    = PT_LABEL;
static uint32_t g_control       = 0;   // the control the prompt uses (PromptControl, checked)

static void DeletePrompt()
{
    if (g_prompt)
    {
        if (N::PromptIsValid(g_prompt))
            N::PromptDelete(g_prompt);
        g_prompt = 0;
    }
    g_promptEnabled = -1;
}

// Built the way the game builds a campfire's Fast Travel prompt (player_camp
// func_158 -> func_53, mode 2: a tap that fires on release), with the priority
// the kneel menu gives its own prompts (1), then put in the kneel menu's group
// (dynamic_craft_scenario func_20). Its text: PromptText if set, else the
// campfire's own label while the CAMP text is loaded, else the same words as
// plain text.
static void CreatePrompt()
{
    uint32_t control = g_cfg.promptControl;
    uint32_t space   = Joaat("INPUT_CONTEXT_A");
    if (control != space && !N::IsControlActionValid(control))
    {
        Log("PromptControl %s (0x%08X) isn't a control the game knows; using INPUT_CONTEXT_A.",
            g_cfg.promptControlName, control);
        control = space;
    }
    g_control = control;

    const char* text;
    const char* how;
    if (g_cfg.promptText[0])
    {
        g_promptKind = PT_CUSTOM;
        text = N::VarStringLiteral(g_cfg.promptText);
        how  = "PromptText";
    }
    else if (CampLabelReady(kFtLabel))
    {
        ReadFtText();
        g_promptKind = PT_LABEL;
        text = kFtLabel;
        how  = "the game's label";
    }
    else if (g_ftText[0])
    {
        g_promptKind = PT_LITERAL;
        text = N::VarStringLiteral(g_ftText);
        how  = "the game's text, its label isn't loaded";
    }
    else
    {
        g_promptKind = PT_ENGLISH;
        text = N::VarStringLiteral(kFtEnglish);
        how  = "English text, the game's label isn't loaded yet";
    }

    int p = N::PromptRegisterBegin();
    N::PromptSetControlAction(p, control);
    N::PromptSetText(p, text);
    N::PromptSetPriority(p, 1);
    N::PromptSetTransportMode(p, 0);
    N::PromptSetAttribute(p, 18, 1);
    N::PromptSetStandardMode(p, 1);
    N::PromptRegisterEnd(p);
    N::PromptSetGroup(p, kRestGroup, 0);
    N::PromptSetVisible(p, 1);
    N::PromptSetEnabled(p, 1);
    g_prompt = p;
    g_promptEnabled = 1;
    Log("Fast Travel added to the kneel menu (%s).", how);
}

// Keep the prompt's text showing (once per frame while it's up). Should the
// CAMP text be unloaded under it (any script can), the same prompt comes back
// with its words as plain text. And English stands in only until the game's
// own words can be read.
static void CheckPromptText()
{
    if (!g_prompt || !N::PromptIsValid(g_prompt))
        return;
    if (g_promptKind == PT_LABEL && !CampLabelReady(kFtLabel))
    {
        Log("The CAMP text was unloaded: Fast Travel now shows its text without the label.");
        DeletePrompt();
    }
    else if (g_promptKind == PT_ENGLISH && CampLabelReady(kFtLabel))
    {
        if (ReadFtText() && strcmp(g_ftText, kFtEnglish) == 0)
            g_promptKind = PT_LITERAL;    // the same words: nothing to change
        else
            DeletePrompt();               // back right away with the game's label
    }
}

// ===========================================================================
// Fast travel, step by step
// ===========================================================================

enum Phase
{
    PH_IDLE,       // kneel menu watch, prompt
    PH_REQUESTED,  // asked short_update to open the menu (bit 4)
    PH_MENU,       // the game's fast travel menu is open
    PH_FADING,     // a place was chosen: standing up, fading out
    PH_TRAVEL,     // told the game to go (bit 32), it's travelling
    PH_ARRIVED,    // trip over: make sure the screen comes back
};

static const char* PhaseName(int p)
{
    switch (p)
    {
    case PH_IDLE:      return "idle";
    case PH_REQUESTED: return "menu requested";
    case PH_MENU:      return "menu open";
    case PH_FADING:    return "fading out";
    case PH_TRAVEL:    return "travelling";
    case PH_ARRIVED:   return "arrived";
    }
    return "?";
}

static int  g_phase          = PH_IDLE;
static int  g_phaseStart     = 0;
static bool g_weSetCampBit   = false;
static int  g_menuSeenAt     = 0;     // last time the menu app was seen open
static int  g_menuOpenedAt   = 0;
static float g_menuMaxMove   = 0.0f;  // strongest movement input seen while the menu was open
static bool g_menuCloseAsked = false; // we asked the game to close it (bit 16)
static bool g_moveTipShown   = false;
static int  g_lastState      = -1;    // short_update's state, last logged
static bool g_weFaded        = false;
static bool g_musicAsked     = false; // we set bit 1 (the game starts the fast travel music)
static bool g_longTripLogged = false;

// While the screen is fading or the game is about to travel: no input, except
// pause (as the camp scripts do around their own fades).
static void HoldControls()
{
    N::DisableAllControlActions();
    N::EnableControlAction(H_FRONTEND_PAUSE);
    N::EnableControlAction(H_FRONTEND_PAUSE_ALT);
}

// The menu closed without a choice: log every condition short_update checks
// while it's open (func_80 state 1), so a log shows which one closed it.
static void LogMenuClosed(int now, int ped)
{
    bool kneeling = N::IsPedActiveInScenario(ped);
    Log("The fast travel menu closed without a choice after %.1f s. Kneeling %d (scenario task %d, exiting %d, "
        "in base %d), camp bit 23 %d, close asked %d, movement input seen %.2f, game flags 1357517=%d "
        "1935690=%d 1898068=%d.",
        (now - g_menuOpenedAt) / 1000.0, kneeling, N::PedHasUseScenarioTask(ped), N::IsPedExitingScenario(ped),
        N::PedIsInScenarioBase(ped), (G::Int(G::kCampFlags, 0) & G::CAMP_AT_FIRE) ? 1 : 0, g_menuCloseAsked,
        g_menuMaxMove, G::Int(G::kMenuStopA, -1), G::Int(G::kMenuStopB, -1), G::Int(G::kUiBusy, -1));
    if (!kneeling && !g_menuCloseAsked && g_menuMaxMove > 0.1f && !g_cfg.lockMovementInMenu)
    {
        Log("  Arthur got up from the kneel while the menu was open, after movement input. "
            "Use the arrow keys, mouse or D-pad in the menu, or try LockMovementInMenu=1.");
        if (!g_moveTipShown)
        {
            g_moveTipShown = true;
            HelpText(nullptr, "The fast travel menu closed because Arthur got up. In the menu, use the arrow keys, "
                              "the mouse or the D-pad: W/S and the left stick make him stand.");
        }
    }
}

static void SetPhase(int phase, int now)
{
    if (phase != g_phase)
        Log("Fast travel: %s -> %s (state %d, flags %d)", PhaseName(g_phase), PhaseName(phase),
            G::Int(G::kFtState, -1), G::Int(G::kFtFlags, -1));
    g_phase = phase;
    g_phaseStart = now;
}

static void RestoreCampBit()
{
    if (!g_weSetCampBit)
        return;
    g_weSetCampBit = false;
    int32_t camp;
    if (G::Get(G::kCampFlags, &camp) && (camp & G::CAMP_AT_FIRE))
        G::Set(G::kCampFlags, camp & ~G::CAMP_AT_FIRE);
}

static void SetFlag(int bit)
{
    int32_t fl;
    if (G::Get(G::kFtFlags, &fl))
        G::Set(G::kFtFlags, fl | bit);
}

static void ClearFlag(int bit)
{
    int32_t fl;
    if (G::Get(G::kFtFlags, &fl))
        G::Set(G::kFtFlags, fl & ~bit);
}

// Put the game's fast travel back to idle when it can't finish on its own, doing
// what short_update's state 6 and its func_81 would have done: the horses lose
// the "don't play dead" flag it gave them on bit 32 (PCF 388), the fast travel
// music stops, and the flags and state go back to 0.
static void ResetGameFastTravel()
{
    int slots[2] = { G::Int(G::kActiveHorse, -1), 1 };
    for (int i = 0; i < 2; i++)
    {
        if (slots[i] < 0 || slots[i] > 6)
            continue;
        int horse = G::Int(G::kHorsePeds + 1 + slots[i] * 45, 0);
        if (horse && N::DoesEntityExist(horse) && !N::IsEntityDead(horse))
            N::SetPedConfigFlag(horse, 388, 0);  // PCF_DisableFatallyWoundedBehaviour
    }
    if (g_musicAsked && !(G::Int(G::kFtFlags, 0) & G::FT_MUSIC))   // short_update started the music
        N::TriggerMusicEvent("OpenWorldMusic_FastTravel_StopEvent");
    g_musicAsked = false;
    G::Set(G::kFtFlags, 0);
    G::Set(G::kFtState, 0);
}

static void TurnOff(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char why[200];
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    Log("ERROR: %s Fast Travel Anywhere is off until the game restarts.", why);
    snprintf(g_brokenWhy, sizeof g_brokenWhy,
             "Fast Travel Anywhere is off: the game's fast travel didn't respond. See FastTravelAnywhere.log.");
    Notify(9000, true, "%s: the game's fast travel didn't respond - turned off. See FastTravelAnywhere.log", FTA_NAME);
}

// Press: do what a campfire does (player_camp: SET bit 23 while at the fire,
// then bit 4 when Fast Travel is pressed).
static void StartRequest(int now)
{
    int32_t st, fl, camp;
    if (!G::Get(G::kFtState, &st) || !G::Get(G::kFtFlags, &fl) || !G::Get(G::kCampFlags, &camp) || st != 0)
        return;
    g_weSetCampBit = (camp & G::CAMP_AT_FIRE) == 0;
    if (g_weSetCampBit)
        G::Set(G::kCampFlags, camp | G::CAMP_AT_FIRE);
    G::Set(G::kFtFlags, (fl & ~G::FT_CLOSE) | G::FT_OPEN);   // a leftover close (16) would shut the new menu
    DeletePrompt();
    Log("Fast Travel pressed: asking the game to open its fast travel menu "
        "(flags %d -> %d, camp bit 23 %s).", fl, (fl & ~G::FT_CLOSE) | G::FT_OPEN,
        g_weSetCampBit ? "set by us" : "already set by the game");
    g_lastState = 0;
    g_weFaded = false;
    g_musicAsked = false;
    g_longTripLogged = false;
    SetPhase(PH_REQUESTED, now);
}

static void Abort(int now, const char* why)
{
    Log("Fast travel: stopped (%s).", why);
    RestoreCampBit();
    if (g_weFaded && !N::IsFadedIn() && !N::IsFadingIn())
        N::FadeIn(800);
    g_weFaded = false;
    SetPhase(PH_IDLE, now);
}

static void FlowTick(int now, int ped)
{
    int st  = G::Int(G::kFtState, -1);
    int fl  = G::Int(G::kFtFlags, 0);
    int age = now - g_phaseStart;

    if (st != g_lastState)
    {
        if (g_cfg.debugLog || g_phase >= PH_FADING)
            Log("Game fast travel state %d -> %d (flags %d)", g_lastState, st, fl);
        g_lastState = st;
    }

    switch (g_phase)
    {
    case PH_REQUESTED:
        if (N::IsEntityDead(ped))
        {
            ClearFlag(G::FT_OPEN);
            Abort(now, "the player died");
            break;
        }
        if (st == 1 || st >= 2)
        {
            g_menuSeenAt = now;
            g_menuOpenedAt = now;
            g_menuMaxMove = 0.0f;
            g_menuCloseAsked = false;
            Log("The game opened its fast travel menu.");
            SetPhase(PH_MENU, now);
            break;
        }
        if (!(fl & G::FT_OPEN))
        {
            // short_update took the request but found no place to go
            // (short_update func_495 found nothing): the game's own message.
            RestoreCampBit();
            Log("The game found no place to fast travel to.");
            HelpText("NO_REGION_FT", "Discover more regions to use fast travel. Regions in lockdown are unavailable.");
            SetPhase(PH_IDLE, now);
            break;
        }
        if (age > 3000)
        {
            ClearFlag(G::FT_OPEN);
            RestoreCampBit();
            TurnOff("The game didn't pick up the fast travel request within 3 s (short_update threads %d, "
                    "state %d, flags %d).", N::ScriptThreads(H_SHORT_UPDATE), st, fl);
            SetPhase(PH_IDLE, now);
        }
        break;

    case PH_MENU:
        if (st == 1)
        {
            // Under the menu, the kneel menu's own Craft (R) and Set Up Camp (E)
            // must not fire (a campfire disables its prompts while the menu is up).
            N::DisableControlAction(H_INPUT_CONTEXT_X);
            N::DisableControlAction(H_INPUT_CONTEXT_Y);
            if (g_cfg.lockMovementInMenu)
            {
                // Menu-only controls: the kneel script reads movement even when
                // it's disabled, but not outside the active control context.
                N::SetControlContext(4, H_CTX_FRONTEND_MENU);
            }
            {
                float lr = N::DisabledControlNormal(H_INPUT_MOVE_LR);
                float ud = N::DisabledControlNormal(H_INPUT_MOVE_UD);
                float m = lr * lr + ud * ud;
                if (m > g_menuMaxMove)
                    g_menuMaxMove = m;
            }
            if (N::IsUiAppActive(H_FAST_TRAVEL_MENU))
                g_menuSeenAt = now;
            else if (now - g_menuSeenAt > 2500)
            {
                // Open on paper, but the app isn't showing: let the game close it
                // the way a campfire does when it shuts down (bit 16).
                Log("The fast travel menu isn't showing; asking the game to close it.");
                SetFlag(G::FT_CLOSE);
                g_menuCloseAsked = true;
                g_menuSeenAt = now;
            }
            break;
        }
        if (st == 0 || st < 0)
        {
            LogMenuClosed(now, ped);   // before the camp bit goes back: the log shows it as the game saw it
            RestoreCampBit();
            SetPhase(PH_IDLE, now);
            break;
        }
        RestoreCampBit();
        // st 2: a place was chosen. Now the camp's part (player_camp state 13):
        // the camp starts the fast travel music (bit 1) with its animation,
        // and the screen fades out.
        Log("A place was chosen. Fading out.");
        SetFlag(G::FT_MUSIC);
        g_musicAsked = true;
        if (g_cfg.standUp)
            N::GivePedHashCommand(ped, H_KNEEL_EXIT, 2.0f);  // the kneel menu's own Leave
        if (!N::IsFadedOut() && !N::IsFadingOut())
        {
            N::FadeOut(1000);
            g_weFaded = true;
        }
        SetPhase(PH_FADING, now);
        break;

    case PH_FADING:
        HoldControls();
        if (st == 0)
        {
            Abort(now, "the game reset its fast travel");
            break;
        }
        if (N::IsEntityDead(ped))
        {
            // short_update would wait for the camp forever: reset it as its own
            // cleanup does (func_81). The death screen takes over the fade.
            ResetGameFastTravel();
            g_weFaded = false;
            Abort(now, "the player died before leaving");
            break;
        }
        if (st >= 4)
        {
            // Something else set bit 32 already (player_camp's cleanup sets it
            // whenever the state is 2, e.g. a camp set up elsewhere being packed
            // away): the game is on its way. Leave the player and the bit alone.
            Log("The game started the trip by itself before the fade ended.");
            SetPhase(PH_TRAVEL, now);
            break;
        }
        if (N::IsFadedOut() || age > 4000)
        {
            // The camp clears the player's tasks before its exit scene, then
            // sets bit 32 once it has packed up.
            N::ClearPedTasksImmediately(ped);
            SetFlag(G::FT_READY);
            Log("Screen faded out: told the game to travel (bit 32).");
            SetPhase(PH_TRAVEL, now);
        }
        break;

    case PH_TRAVEL:
        if (st == 2)
        {
            HoldControls();
            if (age > 12000)
            {
                // The game never started the trip (short_update func_512 refused,
                // e.g. a horse that can't be mounted).
                Log("ERROR: the game didn't start the trip within 12 s (flags %d). Resetting it.", fl);
                ResetGameFastTravel();
                if (N::IsFadedOut() || N::IsFadingOut())
                    N::FadeIn(800);
                g_weFaded = false;
                HelpText(nullptr, "Fast travel didn't start. Try again.");
                SetPhase(PH_IDLE, now);
            }
            break;
        }
        if (st == 0)
        {
            Log("The game finished the trip.");
            g_musicAsked = false;
            SetPhase(PH_ARRIVED, now);
            break;
        }
        if (age > 120000 && !g_longTripLogged)
        {
            // Still loading the place after 2 minutes: give the player the
            // screen back, but leave the game's trip alone.
            g_longTripLogged = true;
            Log("The trip is still going after 2 minutes (state %d).%s", st,
                N::IsFadedOut() ? " Fading the screen back in." : "");
            if (N::IsFadedOut())
                N::FadeIn(1000);
        }
        break;

    case PH_ARRIVED:
        if (N::IsFadedIn() || N::IsFadingIn())
        {
            g_weFaded = false;
            SetPhase(PH_IDLE, now);
            break;
        }
        if (age > 3000)
        {
            if (N::IsFadedOut())
            {
                N::FadeIn(1000);
                Log("The screen was still black after the trip: faded it back in.");
            }
            g_weFaded = false;
            SetPhase(PH_IDLE, now);
        }
        break;
    }
}

// ===========================================================================
// The kneel menu watch
// ===========================================================================

static bool g_kneelUp     = false;
static int  g_lastReason  = -1;
static int  g_lastHelpAt  = -100000;
static int  g_lastHelpWhy = -1;
static int  g_iniCheckAt  = 0;
static int  g_kneelSeenAt = -100000;  // last time a kneel was going on
static int  g_pressAt     = -1;       // Fast Travel pressed, the menu not opened yet: when

// The fast travel menu can open: Arthur is all the way down (short_update
// closes its menu until then, func_502), and the CAMP text is in, since the
// menu's own heading comes from it (CAMP_FT_HEADER, CAMP_FT_SUB). The text is
// waited for 1 s at most.
static bool ReadyForMenu(int ped, int now)
{
    return FullyKneeling(ped) && (CampTextLoaded() || now - g_pressAt > 1000);
}

static void LogKneelDetails(int ped)
{
    int lodging, flow;
    int unlocked = FastTravelUnlocked(&lodging, &flow);
    HorseInfo a, b;
    int horse = HorseCheck(ped, &a, &b);
    int hud = G::Int(G::kKneelHud, 0);
    Log("Kneel menu up. Game fast travel state %d, flags %d, camp bit 23 %d, kneel prompts bit %d, all the way "
        "down %d, CAMP text %s; unlocked %d (lodging %d, flow status %d); horse: %s",
        G::Int(G::kFtState, -1), G::Int(G::kFtFlags, -1),
        (G::Int(G::kCampFlags, 0) & G::CAMP_AT_FIRE) ? 1 : 0, (hud >> 2) & 1, FullyKneeling(ped) ? 1 : 0,
        CampTextLoaded() ? "loaded" : "not loaded", unlocked, lodging, flow, ReasonName(horse));
    Log("  active horse: slot %d ped %d %s, %.1f m, whistle range %.1f m; slot 1: ped %d %s, %.1f m, range %.1f m",
        a.slot, a.ped, !a.exists ? "none" : a.dead ? "dead" : "alive", a.dist, a.range,
        b.ped, !b.exists ? "none" : b.dead ? "dead" : "alive", b.dist, b.range);
}

static void DropPress(const char* why)
{
    if (g_pressAt < 0)
        return;
    g_pressAt = -1;
    Log("Fast Travel: dropped the press, the menu didn't open (%s).", why);
}

static void IdleTick(int now, int ped, int player)
{
    // A kneel has started (dynamic_craft_scenario runs from its first moment):
    // load the CAMP text now, so the label is ready by the time the kneel menu
    // shows its prompts. Let it go again a while after the kneel is over.
    bool kneeling = g_cfg.enabled && N::ScriptThreads(H_DYNAMIC_CRAFT) > 0 && !N::IsEntityDead(ped);
    if (kneeling)
    {
        g_kneelSeenAt = now;
        RequestCampText(now);
    }
    else
    {
        g_kneelHudSeen = false;   // the next kneel starts afresh
        if (now - g_kneelSeenAt > 3000)
            ReleaseCampText(now);
    }

    bool up = kneeling && KneelMenuUp(ped, player);
    if (up != g_kneelUp)
    {
        g_kneelUp = up;
        g_lastReason = -1;
        if (up)
        {
            if (IniChanged())
            {
                LoadSettings();
                up = g_cfg.enabled && KneelMenuUp(ped, player);
                g_kneelUp = up;
            }
            if (up)
            {
                if (!g_globalsOk)
                {
                    char why[200];
                    g_globalsOk = CheckGlobals(ped, why, sizeof why);
                    if (g_globalsOk)
                        Log("Script globals look right (game build 1491.50 layout).");
                    else
                        Log("ERROR: the game's script globals don't look like build 1491.50 (%s). "
                            "Fast travel stays off.", why);
                }
                LogKneelDetails(ped);
            }
        }
        else
        {
            if (g_cfg.debugLog)
                Log("Kneel menu down.");
        }
    }
    else if (!up && !g_cfg.enabled && now - g_iniCheckAt > 2000)
    {
        // Turned off, the kneel never counts as up: look at the .ini now and
        // then, so Enabled=1 works without restarting the game.
        g_iniCheckAt = now;
        if (IniChanged())
            LoadSettings();
    }

    if (!up)
    {
        DropPress("the kneel menu closed");
        DeletePrompt();
        return;
    }

    int r = Availability(ped, player);
    if (r != g_lastReason)
    {
        Log("Fast Travel: %s.", ReasonName(r));
        g_lastReason = r;
    }

    // The kneel menu shows its prompts before Arthur is all the way down, but
    // short_update closes its menu straight away until he is (func_502). So a
    // press from before then goes in once he's down (see ReadyForMenu).
    if (g_pressAt >= 0)
    {
        if (r != R_OK)
        {
            DropPress(ReasonName(r));
            g_lastHelpAt = now;
            g_lastHelpWhy = r;
            ShowReason(r);
        }
        else if (ReadyForMenu(ped, now))
        {
            Log("Ready %.1f s after the press: opening the fast travel menu.", (now - g_pressAt) / 1000.0);
            g_pressAt = -1;
            StartRequest(now);
            return;
        }
        else if (now - g_pressAt > 3000)
            DropPress("Arthur wasn't all the way down 3 s later");
    }

    if (r != R_OK && !g_cfg.showWhenUnavailable)
    {
        DeletePrompt();
        return;
    }

    // The press, read before the prompt might be replaced below.
    bool pressed = g_prompt && N::PromptIsValid(g_prompt) && N::PromptIsJustReleased(g_prompt);

    CheckPromptText();
    if (!g_prompt || !N::PromptIsValid(g_prompt))
    {
        g_prompt = 0;
        CreatePrompt();
    }
    int want = r == R_OK ? 1 : 0;
    if (want != g_promptEnabled)
    {
        N::PromptSetEnabled(g_prompt, want);
        g_promptEnabled = want;
    }

    // Space is also Jump: nothing else should happen on it while kneeling.
    N::DisableControlAction(H_INPUT_JUMP);

    if (r == R_OK)
    {
        if (pressed && g_pressAt < 0)
        {
            g_pressAt = now;
            if (ReadyForMenu(ped, now))
            {
                g_pressAt = -1;
                StartRequest(now);
            }
            else if (!FullyKneeling(ped))
                Log("Fast Travel pressed while Arthur is still getting down: the menu opens once he's down.");
            else
                Log("Fast Travel pressed: the menu opens once the CAMP text is in (the menu's heading).");
        }
    }
    else if (N::IsDisabledControlJustReleased(g_control) && (r != g_lastHelpWhy || now - g_lastHelpAt > 1500))
    {
        // One message per press, at most every 1.5 s for the same reason.
        g_lastHelpAt = now;
        g_lastHelpWhy = r;
        ShowReason(r);
    }
}

// ===========================================================================
// Main loop
// ===========================================================================

static void ScriptMain()
{
    Log("Script started.");
    InitHashes();
    LoadSettings();
    if (SH_getGameVersion)
        Log("Script Hook reports game version id %d", SH_getGameVersion());

    bool shownLoaded = false;
    bool wasOnline   = false;

    for (;;)
    {
        bool online = N::NetworkIsSessionStarted() || N::NetworkIsGameInProgress();
        if (online != wasOnline)
        {
            Log(online ? "Online session detected - mod idle." : "Back in Story Mode.");
            wasOnline = online;
            if (online)
            {
                DeletePrompt();
                RestoreCampBit();
                g_phase = PH_IDLE;
                g_kneelUp = false;
                g_pressAt = -1;
                g_campTextOurs = false;   // Red Dead Online has its own camps: leave the text to it
                g_campTextAskedAt = -1;
                g_campTextSeen = false;
            }
        }

        int ped = online ? 0 : N::PlayerPedId();
        if (!online && ped != 0 && !N::IsLoadingScreenActive())
        {
            int now = N::GameTimer();
            int player = N::PlayerId();

            if (!shownLoaded && N::IsFadedIn())
            {
                shownLoaded = true;
                if (!SH_getGlobalPtr)
                    Notify(9000, true, "%s %s: your Script Hook RDR2 has no getGlobalPtr - see FastTravelAnywhere.log",
                           FTA_NAME, FTA_VERSION);
                else if (g_cfg.showLoadMessage)
                    Notify(6000, false, g_cfg.enabled
                           ? "%s %s loaded - hold E to kneel, then Space: Fast Travel"
                           : "%s %s loaded - OFF (Enabled=0 in FastTravelAnywhere.ini)",
                           FTA_NAME, FTA_VERSION);
            }

            if (g_phase != PH_IDLE)
                FlowTick(now, ped);
            else if (!N::IsPauseMenuActive())
                IdleTick(now, ped, player);
        }

        DrawNotification();
        SH_scriptWait(0);
    }
}

// ===========================================================================
// DLL entry
// ===========================================================================

// The only export. Handy for identifying the build, and its presence also
// stops MinGW-style linkers from auto-exporting every internal function.
extern "C" __declspec(dllexport) const char* FastTravelAnywhereVersion()
{
    return FTA_VERSION;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        BuildPaths(hModule);
        ResetLog();
        Log("%s %s loading (%s)", FTA_NAME, FTA_VERSION, g_logPath);
        if (BindScriptHook())
        {
            SH_scriptRegister(hModule, ScriptMain);
            g_registered = true;
            Log("Registered with Script Hook RDR2. Waiting for Story Mode to start.");
        }
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        // reserved != NULL means the process is exiting: leave everything as is.
        if (reserved == nullptr && g_registered && SH_scriptUnregister)
            SH_scriptUnregister(hModule);
    }
    return TRUE;
}
