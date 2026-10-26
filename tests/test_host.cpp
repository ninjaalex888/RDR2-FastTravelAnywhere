// End-to-end test host (run under Wine or Windows): loads the real
// FastTravelAnywhere.asi next to the fake ScriptHookRDR2.dll, drives its script
// frame by frame as the game would, and plays the parts of the player and of the
// game's scripts (see fake_scripthook.cpp).
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef void (*ScriptMainFn)();
typedef ScriptMainFn (*PFN_FakeGetScriptMain)();
typedef void (*PFN_FakeSetHostFiber)(void*);
typedef void (*PFN_FakeGameTick)();
typedef void (*PFN_FakeSet)(int, int);
typedef int  (*PFN_FakeGet)(int);
typedef void (*PFN_FakeText)(int, char*, int);
typedef void (*PFN_FakeSetGlobal)(int, int);
typedef int  (*PFN_FakeGetGlobal)(int);
typedef void (*PFN_FakeHorse)(int, int, int, int, int, int, int, int);
typedef void (*PFN_FakeInit)();

static PFN_FakeGameTick  FakeGameTick;
static PFN_FakeSet       FakeSet;
static PFN_FakeGet       FakeGet;
static PFN_FakeText      FakeText;
static PFN_FakeSetGlobal FakeSetGlobal;
static PFN_FakeGetGlobal FakeGetGlobal;
static PFN_FakeHorse     FakeHorse;

static void*        g_host;
static void*        g_script;
static ScriptMainFn g_main;
static int          g_fail = 0, g_pass = 0;
static int          g_textPulls = 0;   // times a test unloads the CAMP text from under a showing prompt

// Same values as in fake_scripthook.cpp
enum SetWhat
{
    S_KNEEL = 1, S_INTERACTION, S_CAMERA, S_WANTED, S_MISSION, S_ONLINE, S_PAUSED, S_PLAYERMENU, S_CANLAUNCH,
    S_SU_RUNNING, S_SU_IGNORE, S_DESTINATIONS, S_REFUSE, S_SIMFADEIN, S_PLAYERDEAD, S_PRESS, S_MENU,
    S_PLAYERPOS_X, S_PLAYERPOS_Y, S_DEST_X, S_DEST_Y, S_RESET_COUNTERS, S_INVEHICLE, S_ITEMINTERACTION,
    S_MENU_SHOWS, S_MOVE, S_NO_SCENARIO_TASK, S_STUCK_TRAVEL,
    S_DOWN_FRAMES, S_CAMP_LOADED, S_CAMP_LOAD_FRAMES, S_CAMP_NEVER, S_CAMP_SCRIPT, S_FRENCH, S_CRAFTING,
    S_SET_CAMP, S_CAMPFIRE_HUD, S_HUD_BROKEN, S_NOT_ACTIVE, S_LABEL_UNREADABLE,
    S_CAMP_HERE, S_LIST_BROKEN, S_KIT_CAMP, S_KIT_SIMPLE, S_BACKUP_INV, S_INTERIOR, S_COMBAT, S_RECENT_CRIME,
    S_MINIGAME, S_ANIMAL_INTERACTION, S_CAMP_SCRIPT_EXISTS, S_CAMP_SCRIPT_NO_LOAD, S_CAMP_FAILS, S_CAMP_DOWN,
    S_EVELYN,
};
// The camp scripts the fake knows, in the bit order of S_CAMP_SCRIPT, and whether they unload the CAMP text when they end.
static const char* const kCampScripts[] = { "player_camp", "interactive_campfire", "property_use_core", "campfire_always", "sleeping_scenario" };
static const int kCampScriptUnloads[]   = { 1, 1, 1, 0, 0 };
enum GetWhat
{
    Q_LIVE_PROMPTS = 1, Q_PROMPTS_MADE, Q_PROMPTS_DELETED, Q_P_CONTROL, Q_P_PRIORITY, Q_P_TRANSPORT,
    Q_P_STANDARD, Q_P_RELEASE, Q_P_ATTR18, Q_P_GROUP, Q_P_TAB, Q_P_ENABLED, Q_P_VISIBLE,
    Q_MENU_OPEN, Q_MENU_ACTIVE, Q_MENU_OPENS, Q_KNEEL, Q_FADE, Q_MOD_FADEOUTS, Q_MOD_FADEINS, Q_MUSIC,
    Q_MUSIC_STOP, Q_TRIPS, Q_MOUNTED, Q_TAKEN_HORSE, Q_CLEAR_TASKS, Q_KNEEL_EXITS, Q_DISABLED_X,
    Q_DISABLED_Y, Q_DISABLED_JUMP, Q_DISABLED_ALL, Q_HELP_COUNT, Q_UNKNOWN, Q_BAD_ARGS, Q_BAD_VARSTRING,
    Q_BAD_HELP, Q_PLAYER_X, Q_PLAYER_Y, Q_TIMESCALE_X10, Q_GAME_MS, Q_LAST_UNKNOWN_LO, Q_LAST_BADARG_LO,
    Q_DISABLED_OTHER, Q_CONTEXT_SETS, Q_PAUSE_ENABLED, Q_MOD_MUSIC_STOPS, Q_PCF_100, Q_PCF_101,
    Q_GROUP_DRAWN, Q_CAMP_LOADED, Q_CAMP_REQUESTS, Q_CAMP_DELETES, Q_BLANK_FRAMES, Q_BLANK_TOTAL,
    Q_FT_PROMPTS, Q_CAMP_PROMPTS, Q_CP_CONTROL, Q_CP_PRIORITY, Q_CP_TRANSPORT, Q_CP_STANDARD, Q_CP_RELEASE,
    Q_CP_ATTR18, Q_CP_GROUP, Q_CP_TAB, Q_CP_ENABLED, Q_CP_VISIBLE, Q_GAME_PROMPTS, Q_KNEEL_OWN_CAMP,
    Q_CAMP_STARTS, Q_GAME_CAMP_STARTS, Q_CAMP_ARGC, Q_CAMP_STACK, Q_CAMP_RUNNING, Q_SCRIPT_REQUESTS,
    Q_NO_LONGER_NEEDED, Q_BAD_SCRIPT_CALLS, Q_BAD_INVENTORY, Q_DISABLED_B, Q_DISABLED_A, Q_CAMP_KNEEL_EXITS,
    Q_DUP_FRAMES, Q_CAMP_PROMPTS_MAX, Q_CAMP_STATE, Q_KNEEL_VISIBLE, Q_KNEEL_PROMPTS,
    Q_CAMP_ARG0 = 200,   // ...to Q_CAMP_ARG0 + 9
};
enum { K_OFF = 0, K_DOWN, K_KNEEL, K_EXIT };

static const int GL_PLAYER = 35, GL_FT_FLAGS = 43907, GL_FT_STATE = 43908, GL_CAMP = 1392040,
                 GL_UIBUSY = 1898068, GL_LODGING = 4644, GL_UNLOCK_FLOW = 1351050, GL_FLOW_STATUS = 12106,
                 GL_ACTIVE_HORSE = 1135, GL_MENU_STOP_A = 1357517, GL_MENU_STOP_B = 1935690;
static const int CAMP_BIT = 1 << 23;
static const int kRestGroup = 342152817;
// What the game's camping rules look at (see CampAvailability in the plugin)
static const int GL_CAMP_FLAGS2 = 1392041, GL_CAMP_IS_UP = 9459, GL_ZONE = 1894899, GL_ZONE_AREA = 1894901,
                 GL_AREA_FLAGS = 23118, GL_DISTRICT = 1897993, GL_STORY_CAMP = 1359044, GL_LAW = 1935630,
                 GL_ON_MISSION = 1898328, GL_MISSION_CAMP = 1914318, GL_ENCOUNTER = 1326827, GL_AMBUSH = 1393449,
                 GL_HORSE_AWAY = 4190, GL_LED_HORSE = 1900776, GL_CAMP_STATE = 43885, GL_CAMP_FLOW120 = 1353598,
                 GL_CAMP_FLOW15 = 1836123, GL_ITEM_USE_OFF = 1935697;

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

static void WINAPI ScriptFiber(void*) { g_main(); for (;;) SwitchToFiber(g_host); }

static void Frame() { FakeGameTick(); SwitchToFiber(g_script); }
static void Frames(int n) { for (int i = 0; i < n; i++) Frame(); }
template <class F> static bool RunUntil(F cond, int maxFrames)
{
    for (int i = 0; i < maxFrames; i++)
    {
        Frame();
        if (cond())
            return true;
    }
    return false;
}

static void Check(const char* name, bool ok, const char* detail = "")
{
    printf("  [%s] %s %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (ok) g_pass++; else g_fail++;
}

static void Section(const char* name) { printf("\n%s\n", name); }

static int G(int idx) { return FakeGetGlobal(idx); }
static bool CampBit() { return (G(GL_CAMP) & CAMP_BIT) != 0; }

// The live prompt's text as set (a label, or "~LIT~" + plain text), and what it shows right now.
static bool RawIs(const char* want)
{
    char t[300];
    FakeText(1, t, sizeof t);
    return strcmp(t, want) == 0;
}
static bool ShowsText(const char* want)
{
    char t[300];
    FakeText(4, t, sizeof t);
    return strcmp(t, want) == 0;
}

static void LastHelp(char* out, int n) { FakeText(2, out, n); }
static bool HelpIs(const char* want)
{
    char h[300];
    LastHelp(h, sizeof h);
    return strcmp(h, want) == 0;
}
static bool HelpHas(const char* part)
{
    char h[300];
    LastHelp(h, sizeof h);
    return strstr(h, part) != nullptr;
}

static int LogCount(const char* part)
{
    FILE* f = fopen("FastTravelAnywhere.log", "rb");
    if (!f) return 0;
    static char buf[1 << 21];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    int count = 0;
    for (const char* p = strstr(buf, part); p; p = strstr(p + 1, part))
        count++;
    return count;
}
static bool LogHas(const char* part) { return LogCount(part) > 0; }

static int g_iniBump = 0;
static void WriteIni(const char* body)
{
    FILE* f = fopen("FastTravelAnywhere.ini", "wb");
    fprintf(f, "[FastTravelAnywhere]\r\n%s", body);
    fclose(f);
    // a distinct modification time each write, whatever the file system's resolution
    HANDLE h = CreateFileA("FastTravelAnywhere.ini", FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE)
    {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
        u.QuadPart += (ULONGLONG)(++g_iniBump) * 20000000ULL;
        ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
        SetFileTime(h, nullptr, nullptr, &ft);
        CloseHandle(h);
    }
}

// The player holds E: down on one knee, the "Resting" menu appears.
static bool Kneel()
{
    FakeSet(S_KNEEL, 1);
    return RunUntil([] { return FakeGet(Q_KNEEL) == K_KNEEL; }, 60) && (Frames(3), true);
}

static void StandUp()
{
    FakeSet(S_KNEEL, 0);
    RunUntil([] { return FakeGet(Q_KNEEL) == K_OFF; }, 60);
    Frames(3);
}

static void Press(uint32_t control) { FakeSet(S_PRESS, (int)control); Frame(); }

static const uint32_t SPACE = 0x5181713D; // INPUT_CONTEXT_A

static void ResetWorld()
{
    FakeSet(S_RESET_COUNTERS, 0);
    FakeSet(S_MENU, 0);              // no menu choice left over from a test whose menu never opened
    FakeSet(S_CAMP_DOWN, 1);         // no camp left up by a test
    FakeSetGlobal(GL_FT_FLAGS, 0);
    FakeSetGlobal(GL_FT_STATE, 0);
    FakeSetGlobal(GL_CAMP, 0);
    FakeSet(S_PLAYERPOS_X, 0);
    FakeSet(S_PLAYERPOS_Y, 0);
    FakeHorse(100, 0, 1, 0, 10, 0, 2, 150);   // the active horse (slot 0), 10 m away, bonding 2
    FakeHorse(101, 1, 0, 0, 0, 0, 1, 0);      // nothing in slot 1
    FakeSetGlobal(GL_ACTIVE_HORSE, 0);
    // Set Up Camp: out in the wilderness (the kneel menu has its own), a camp
    // kit in the satchel, none of the game's camping rules in force
    FakeSet(S_CAMP_HERE, 1);
    FakeSet(S_LIST_BROKEN, 0);
    FakeSet(S_KIT_CAMP, 1);
    FakeSet(S_KIT_SIMPLE, 0);
    FakeSet(S_BACKUP_INV, 0);
    FakeSet(S_INTERIOR, 0);
    FakeSet(S_COMBAT, 0);
    FakeSet(S_RECENT_CRIME, 0);
    FakeSet(S_MINIGAME, 0);
    FakeSet(S_ANIMAL_INTERACTION, 0);
    FakeSet(S_CAMP_SCRIPT_EXISTS, 1);
    FakeSet(S_CAMP_SCRIPT_NO_LOAD, 0);
    FakeSet(S_CAMP_FAILS, 0);
    FakeSet(S_EVELYN, 0);
    static const int zero[] = { GL_CAMP_FLAGS2, GL_CAMP_IS_UP, GL_ZONE, GL_ZONE_AREA, GL_DISTRICT, GL_STORY_CAMP,
                                GL_ON_MISSION, GL_MISSION_CAMP, GL_ENCOUNTER, GL_AMBUSH, GL_HORSE_AWAY, GL_LED_HORSE,
                                GL_CAMP_FLOW120, GL_CAMP_FLOW15, GL_ITEM_USE_OFF };
    for (int g : zero)
        FakeSetGlobal(g, 0);
    for (int f = 13; f <= 21; f++)
        FakeSetGlobal(GL_LAW + f, 0);
}

// The plugin's Set Up Camp prompt: its text as set, and what it shows.
static bool CampRawIs(const char* want)
{
    char t[300];
    FakeText(5, t, sizeof t);
    return strcmp(t, want) == 0;
}
static bool CampShows(const char* want)
{
    char t[300];
    FakeText(6, t, sizeof t);
    return strcmp(t, want) == 0;
}
static bool CampArgsAre(const int* want)
{
    for (int i = 0; i < 10; i++)
        if (FakeGet(Q_CAMP_ARG0 + i) != want[i])
            return false;
    return true;
}
static void CampArgsText(char* out, int n)
{
    int len = snprintf(out, n, "(args");
    for (int i = 0; i < 10 && len < n; i++)
        len += snprintf(out + len, n - len, " %d", FakeGet(Q_CAMP_ARG0 + i));
    if (len < n)
        snprintf(out + len, n - len, ")");
}

// Kneels in a town (no Set Up Camp of the kneel menu's own) with one of the
// game's camping rules in force: the mod adds none either, and the log says
// why; once the rule is lifted (still kneeling), there it is.
typedef void (*Fn)();
static void CampRule(const char* name, Fn on, Fn off, const char* why)
{
    ResetWorld();
    FakeSet(S_CAMP_HERE, 0);
    on();
    int logged = LogCount(why);
    Kneel();
    bool none = FakeGet(Q_CAMP_PROMPTS) == 0;
    bool says = LogCount(why) > logged;
    off();
    Frames(2);
    bool back = FakeGet(Q_CAMP_PROMPTS) == 1;
    StandUp();
    char n[220], d[80];
    snprintf(n, sizeof n, "%s: no Set Up Camp, the log says why; there once that's over", name);
    snprintf(d, sizeof d, "(none %d, logged %d, back %d)", none, says, back);
    Check(n, none && says && back, d);
}

// The press goes through to player_camp; waits until it has Arthur up.
static bool CampRuns()
{
    return RunUntil([] { return FakeGet(Q_CAMP_STARTS) >= 1; }, 30) &&
           RunUntil([] { return FakeGet(Q_KNEEL) == K_OFF; }, 60);
}

int main()
{
    printf("Fast Travel Anywhere - end-to-end test (fake Script Hook)\n");
    DeleteFileA("FastTravelAnywhere.ini");
    DeleteFileA("FastTravelAnywhere.log");

    HMODULE fake = LoadLibraryA("ScriptHookRDR2.dll");
    if (!fake) { printf("could not load fake ScriptHookRDR2.dll\n"); return 2; }
    auto FakeGetScriptMain = (PFN_FakeGetScriptMain)GetProcAddress(fake, "FakeGetScriptMain");
    auto FakeSetHostFiber  = (PFN_FakeSetHostFiber)GetProcAddress(fake, "FakeSetHostFiber");
    auto FakeInit          = (PFN_FakeInit)GetProcAddress(fake, "FakeInit");
    FakeGameTick  = (PFN_FakeGameTick)GetProcAddress(fake, "FakeGameTick");
    FakeSet       = (PFN_FakeSet)GetProcAddress(fake, "FakeSet");
    FakeGet       = (PFN_FakeGet)GetProcAddress(fake, "FakeGet");
    FakeText      = (PFN_FakeText)GetProcAddress(fake, "FakeText");
    FakeSetGlobal = (PFN_FakeSetGlobal)GetProcAddress(fake, "FakeSetGlobal");
    FakeGetGlobal = (PFN_FakeGetGlobal)GetProcAddress(fake, "FakeGetGlobal");
    FakeHorse     = (PFN_FakeHorse)GetProcAddress(fake, "FakeHorse");
    if (!FakeGetScriptMain || !FakeSetHostFiber || !FakeInit || !FakeGameTick || !FakeSet || !FakeGet ||
        !FakeText || !FakeSetGlobal || !FakeGetGlobal || !FakeHorse)
    {
        printf("fake Script Hook is missing test exports\n");
        return 2;
    }
    FakeInit();
    ResetWorld();

    HMODULE asi = LoadLibraryA("FastTravelAnywhere.asi");
    Section("Loading");
    Check("plugin loads", asi != nullptr);
    if (!asi) return 1;
    g_main = FakeGetScriptMain();
    Check("registers its script with Script Hook", g_main != nullptr);
    if (!g_main) return 1;

    g_host = ConvertThreadToFiber(nullptr);
    FakeSetHostFiber(g_host);
    g_script = CreateFiber(0, ScriptFiber, nullptr);

    // A first kneel while Global_35 isn't the player: the plugin must not trust
    // the globals, and must not write any.
    FakeSetGlobal(GL_PLAYER, 7);
    Frames(5);
    {
        char t[4096];
        FakeText(3, t, sizeof t);
        Check("shows the loaded message", strstr(t, "Fast Travel Anywhere 1.1.0 loaded") != nullptr &&
                                            strstr(t, "Set Up Camp anywhere") != nullptr);
        FILE* f = fopen("FastTravelAnywhere.ini", "rb");
        char ini[4096] = { 0 };
        if (f) { fread(ini, 1, sizeof ini - 1, f); fclose(f); }
        Check("writes the default settings file", strstr(ini, "PromptControl=INPUT_CONTEXT_A") != nullptr &&
                                                   strstr(ini, "RequireHorseNearby=1") != nullptr &&
                                                   strstr(ini, "SetUpCampAnywhere=1") != nullptr);
    }

    // The first kneels, in a French game whose CAMP text loads slowly: the
    // plugin hasn't read the game's words for "Fast Travel" yet.
    Section("The game's language");
    {
        FakeSet(S_FRENCH, 1);
        FakeSet(S_CAMP_LOAD_FRAMES, 20);
        FakeSet(S_LABEL_UNREADABLE, 1);   // first, the label's words can't be read out
        int made = FakeGet(Q_PROMPTS_MADE);
        FakeSet(S_KNEEL, 1);
        RunUntil([] { return FakeGet(Q_LIVE_PROMPTS) == 1; }, 60);
        Check("the CAMP text is asked for as the kneel starts", FakeGet(Q_CAMP_REQUESTS) >= 1);
        Check("kneel menu up before the text has loaded: \"Fast Travel\" as plain text",
              FakeGet(Q_CAMP_LOADED) == 0 && RawIs("~LIT~Fast Travel") && ShowsText("Fast Travel"));
        RunUntil([] { return FakeGet(Q_CAMP_LOADED) == 1; }, 60);
        Check("once it has loaded: the game's label, in the game's language (even with its words unreadable)",
              RawIs("CAMP_FT_PROMPT") && ShowsText("Voyage rapide") && FakeGet(Q_LIVE_PROMPTS) == 1);
        Frames(30);
        Check("...switched once, and never without text", FakeGet(Q_PROMPTS_MADE) == made + 2 && FakeGet(Q_BLANK_FRAMES) == 0);
        StandUp();
        Frames(120);                      // the CAMP text is let go
        FakeSet(S_LABEL_UNREADABLE, 0);
        FakeSet(S_KNEEL, 1);
        RunUntil([] { return FakeGet(Q_LIVE_PROMPTS) == 1; }, 60);
        Check("next kneel, text not loaded yet: English as plain text again", RawIs("~LIT~Fast Travel"));
        RunUntil([] { return FakeGet(Q_CAMP_LOADED) == 1; }, 60);
        Check("...then the label, its words read this time", RawIs("CAMP_FT_PROMPT") && ShowsText("Voyage rapide") &&
                                                             LogHas("The game's text for Fast Travel: \"Voyage rapide\""));
        StandUp();
        FakeSet(S_FRENCH, 0);
        FakeSet(S_CAMP_LOAD_FRAMES, 3);
    }

    Section("Game build check");
    Kneel();
    Check("globals that don't match: prompt shown greyed out", FakeGet(Q_LIVE_PROMPTS) == 1 && FakeGet(Q_P_ENABLED) == 0);
    Press(SPACE);
    Frames(2);
    Check("...and pressing it writes nothing", G(GL_FT_FLAGS) == 0 && !CampBit() && FakeGet(Q_MENU_OPENS) == 0);
    Check("...and says the game version isn't recognised", HelpHas("doesn't recognise"));
    Check("...and the log says why", LogHas("don't look like build 1491.50"));
    StandUp();
    FakeSetGlobal(GL_PLAYER, 42);

    Section("The kneel menu");
    Frames(30);
    Check("no prompt while standing", FakeGet(Q_LIVE_PROMPTS) == 0);
    Kneel();
    Check("kneeling adds one prompt", FakeGet(Q_LIVE_PROMPTS) == 1);
    {
        char t[300];
        FakeText(1, t, sizeof t);
        Check("text: the campfire's own label CAMP_FT_PROMPT", strcmp(t, "CAMP_FT_PROMPT") == 0, t);
    }
    Check("button: INPUT_CONTEXT_A (Space / A)", (uint32_t)FakeGet(Q_P_CONTROL) == SPACE);
    Check("in the kneel menu's group 342152817, tab 0", FakeGet(Q_P_GROUP) == kRestGroup && FakeGet(Q_P_TAB) == 0);
    Check("built like the game's prompts (priority 1 as in the kneel menu, any transport, attribute 18, tap on release)",
          FakeGet(Q_P_PRIORITY) == 1 && FakeGet(Q_P_TRANSPORT) == 0 && FakeGet(Q_P_ATTR18) == 1 &&
          FakeGet(Q_P_STANDARD) == 1 && FakeGet(Q_P_RELEASE) == 1);
    Check("enabled and visible", FakeGet(Q_P_ENABLED) == 1 && FakeGet(Q_P_VISIBLE) == 1);
    Check("Jump (also Space) is disabled while kneeling", FakeGet(Q_DISABLED_JUMP) == 1);
    Check("log notes the kneel with the game's state", LogHas("Kneel menu up.") && LogHas("Script globals look right"));

    FakeSet(S_INTERACTION, 77);
    Frames(3);
    Check("gone while the player has an interaction target", FakeGet(Q_LIVE_PROMPTS) == 0);
    FakeSet(S_INTERACTION, 0);
    Frames(3);
    Check("back afterwards", FakeGet(Q_LIVE_PROMPTS) == 1);
    FakeSet(S_CAMERA, 1);
    Frames(3);
    Check("gone while the camera is out", FakeGet(Q_LIVE_PROMPTS) == 0);
    FakeSet(S_CAMERA, 0);
    Frames(3);
    FakeSet(S_PAUSED, 1);
    Press(SPACE);
    Frames(3);
    Check("pause menu: nothing happens", G(GL_FT_FLAGS) == 0 && FakeGet(Q_MENU_OPENS) == 0);
    FakeSet(S_PAUSED, 0);
    StandUp();
    Check("standing up removes it", FakeGet(Q_LIVE_PROMPTS) == 0 && FakeGet(Q_PROMPTS_DELETED) >= 1);

    Section("Along with the kneel menu's own buttons");
    ResetWorld();
    FakeSet(S_KNEEL, 1);
    {
        bool drawn = RunUntil([] { return FakeGet(Q_GROUP_DRAWN) == 1; }, 60);
        Check("the kneel menu shows its buttons before Arthur is all the way down", drawn && FakeGet(Q_KNEEL) == K_DOWN);
        Check("Fast Travel comes up with them, in the same frame, with its text",
              FakeGet(Q_LIVE_PROMPTS) == 1 && ShowsText("Fast Travel") && FakeGet(Q_P_ENABLED) == 1);
    }
    Press(SPACE);
    Check("pressed before he's all the way down: nothing yet (the game's menu would close at once)",
          (G(GL_FT_FLAGS) & 4) == 0 && !CampBit() && FakeGet(Q_MENU_OPENS) == 0 && FakeGet(Q_KNEEL) == K_DOWN);
    Check("...the log says it waits for him", LogHas("still getting down"));
    {
        bool opened = RunUntil([] { return FakeGet(Q_MENU_OPEN) == 1; }, 60);
        Check("the menu opens as soon as he's down", opened && FakeGet(Q_KNEEL) == K_KNEEL);
    }
    Frames(15);
    Check("...and stays open", FakeGet(Q_MENU_OPEN) == 1 && G(GL_FT_STATE) == 1 && FakeGet(Q_MENU_OPENS) == 1);
    FakeSet(S_MENU, 1);
    Frames(3);
    StandUp();

    ResetWorld();
    FakeSet(S_KNEEL, 1);
    RunUntil([] { return FakeGet(Q_LIVE_PROMPTS) == 1; }, 60);
    Press(SPACE);
    FakeSet(S_KNEEL, 0);   // he gets straight back up
    Frames(40);
    Check("pressed while getting down, then straight back up: no menu",
          FakeGet(Q_MENU_OPENS) == 0 && G(GL_FT_FLAGS) == 0 && !CampBit());
    Check("...the log says the press was dropped", LogHas("dropped the press"));
    StandUp();

    ResetWorld();
    FakeSet(S_DOWN_FRAMES, 200);   // a kneel that takes ages to get down
    FakeSet(S_KNEEL, 1);
    RunUntil([] { return FakeGet(Q_LIVE_PROMPTS) == 1; }, 60);
    Press(SPACE);
    Frames(100);
    Check("still not down 3 s after the press: the press is dropped", LogHas("3 s later") && FakeGet(Q_MENU_OPENS) == 0);
    RunUntil([] { return FakeGet(Q_KNEEL) == K_KNEEL; }, 250);
    Frames(5);
    Check("...and nothing opens by itself once he's down", FakeGet(Q_MENU_OPENS) == 0 && G(GL_FT_FLAGS) == 0);
    StandUp();
    FakeSet(S_DOWN_FRAMES, 30);

    ResetWorld();
    Kneel();
    FakeSet(S_SET_CAMP, 1);   // Set Up Camp (E): the kneel menu takes its buttons down, its group still up
    Frames(2);
    Check("Set Up Camp pressed: Fast Travel goes with the kneel menu's own buttons",
          FakeGet(Q_LIVE_PROMPTS) == 0 && FakeGet(Q_GROUP_DRAWN) == 1);
    Press(SPACE);
    Frames(2);
    Check("...and Space does nothing then", FakeGet(Q_MENU_OPENS) == 0 && G(GL_FT_FLAGS) == 0 && !CampBit());
    StandUp();

    ResetWorld();
    {
        int dropped = LogCount("dropped the press");
        FakeSet(S_KNEEL, 1);
        RunUntil([] { return FakeGet(Q_LIVE_PROMPTS) == 1; }, 60);
        Press(SPACE);             // pressed while getting down...
        FakeSet(S_SET_CAMP, 1);   // ...then Set Up Camp
        Frames(40);
        Check("a press held for him to get down, then Set Up Camp: dropped, no menu",
              FakeGet(Q_MENU_OPENS) == 0 && G(GL_FT_FLAGS) == 0 && LogCount("dropped the press") == dropped + 1);
        StandUp();
    }

    ResetWorld();
    Kneel();
    FakeSet(S_CAMPFIRE_HUD, 1);   // a campfire's own prompts come up (its bits 2 and 3)
    Frames(2);
    Check("a campfire's own prompts up: Fast Travel leaves them to it", FakeGet(Q_LIVE_PROMPTS) == 0);
    FakeSet(S_CAMPFIRE_HUD, 0);
    StandUp();

    ResetWorld();
    FakeSet(S_HUD_BROKEN, 1);     // a game build where the kneel's bit isn't where the plugin looks
    FakeSet(S_KNEEL, 1);
    {
        bool early = RunUntil([] { return FakeGet(Q_LIVE_PROMPTS) == 1; }, 20);
        Check("kneel menu bit never seen: no Fast Travel while he's getting down", !early && FakeGet(Q_KNEEL) == K_DOWN);
    }
    RunUntil([] { return FakeGet(Q_KNEEL) == K_KNEEL; }, 60);
    Frames(1);
    Check("...but once he's all the way down, as in 1.0.0", FakeGet(Q_LIVE_PROMPTS) == 1 && FakeGet(Q_P_ENABLED) == 1);
    Press(SPACE);
    Frames(3);
    Check("...and it works", FakeGet(Q_MENU_OPEN) == 1);
    FakeSet(S_MENU, 1);
    Frames(3);
    StandUp();
    FakeSet(S_HUD_BROKEN, 0);

    ResetWorld();
    FakeSet(S_NOT_ACTIVE, 1);     // all the way down, in the scenario's base, but not "active" in it
    Kneel();
    Press(SPACE);
    Frames(15);
    Check("down in the scenario's base only: the menu opens and stays open (func_502 takes either)",
          FakeGet(Q_MENU_OPEN) == 1 && G(GL_FT_STATE) == 1);
    FakeSet(S_MENU, 1);
    Frames(3);
    StandUp();
    FakeSet(S_NOT_ACTIVE, 0);

    ResetWorld();
    Kneel();
    FakeSet(S_CAMP_LOADED, 0);        // the CAMP text gone just now...
    g_textPulls++;
    FakeSet(S_CAMP_LOAD_FRAMES, 10);  // ...and slow to come back
    Frames(1);
    Press(SPACE);
    Check("pressed while the CAMP text is still coming in: the menu waits for it (its heading is in it)",
          FakeGet(Q_MENU_OPENS) == 0 && (G(GL_FT_FLAGS) & 4) == 0 && LogHas("once the CAMP text is in") &&
          FakeGet(Q_BLANK_FRAMES) <= 1);
    {
        bool opened = RunUntil([] { return FakeGet(Q_MENU_OPEN) == 1; }, 40);
        Check("...and opens once it's in", opened && FakeGet(Q_CAMP_LOADED) == 1);
    }
    FakeSet(S_MENU, 1);
    Frames(3);
    StandUp();
    FakeSet(S_CAMP_LOAD_FRAMES, 3);

    Section("Fast travel, all the way");
    ResetWorld();
    Kneel();
    Press(SPACE);
    Check("pressing it sets the camp bit and asks for the menu", CampBit());
    Frames(1);
    Check("the game opens its fast travel menu", FakeGet(Q_MENU_OPEN) == 1 && G(GL_FT_STATE) == 1 && FakeGet(Q_MENU_OPENS) == 1);
    Check("time slows, as at a campfire", FakeGet(Q_TIMESCALE_X10) == 1);
    Check("our prompt is gone while the menu is up", FakeGet(Q_LIVE_PROMPTS) == 0);
    Frames(20);
    Check("menu stays open (camp bit kept)", FakeGet(Q_MENU_OPEN) == 1 && CampBit());
    Check("Craft (R) and Set Up Camp (E) are blocked under the menu", FakeGet(Q_DISABLED_X) == 1 && FakeGet(Q_DISABLED_Y) == 1);
    FakeSet(S_MENU, 2); // choose a place
    Frame();
    Frames(1);
    Check("choosing: the menu closes, time back to normal", FakeGet(Q_MENU_OPEN) == 0 && FakeGet(Q_TIMESCALE_X10) == 10);
    Check("camp bit restored", !CampBit());
    Check("fade-out started and Arthur stands up", FakeGet(Q_MOD_FADEOUTS) == 1 && FakeGet(Q_KNEEL_EXITS) == 1);
    Frames(2);
    Check("fast travel music started", FakeGet(Q_MUSIC) == 1);
    Check("controls held during the fade, except pause", FakeGet(Q_DISABLED_ALL) == 1 && FakeGet(Q_PAUSE_ENABLED) == 2);
    Check("not leaving before the screen is black", G(GL_FT_STATE) == 2 && FakeGet(Q_TRIPS) == 0);
    bool left = RunUntil([] { return FakeGet(Q_TRIPS) == 1; }, 200);
    Check("after the fade, the game makes the trip", left);
    Check("tasks cleared before leaving", FakeGet(Q_CLEAR_TASKS) == 1);
    Check("player is at the place he chose", FakeGet(Q_PLAYER_X) == 5000 && FakeGet(Q_PLAYER_Y) == 5000);
    Check("on his horse", FakeGet(Q_MOUNTED) == 100);
    Check("the game's horse flag set for the trip is cleared again", FakeGet(Q_PCF_100) == 0);
    Check("music stopped", FakeGet(Q_MUSIC_STOP) == 1);
    Check("screen still black right after (the game didn't fade in here)", FakeGet(Q_FADE) == 2 && FakeGet(Q_MOD_FADEINS) == 0);
    bool faded = RunUntil([] { return FakeGet(Q_FADE) == 0; }, 200);
    Check("the plugin fades the screen back in", faded && FakeGet(Q_MOD_FADEINS) == 1);
    Check("game state back to idle", G(GL_FT_STATE) == 0 && G(GL_FT_FLAGS) == 0 && !CampBit());
    Check("the CAMP text is gone after the trip (as players found in the game)", FakeGet(Q_CAMP_LOADED) == 0);
    Kneel();
    Check("kneeling again offers it again", FakeGet(Q_LIVE_PROMPTS) == 1 && FakeGet(Q_P_ENABLED) == 1);
    Check("...with its text showing (1.0.0 showed none here)", ShowsText("Fast Travel") && FakeGet(Q_BLANK_FRAMES) == 0);
    StandUp();

    Section("When the game fades in by itself");
    ResetWorld();
    FakeSet(S_SIMFADEIN, 1);
    FakeSet(S_DEST_X, 7000);
    Kneel();
    Press(SPACE);
    Frames(3);
    FakeSet(S_MENU, 2);
    RunUntil([] { return FakeGet(Q_TRIPS) == 2; }, 300);
    RunUntil([] { return FakeGet(Q_FADE) == 0; }, 200);
    Frames(150);
    Check("trip made", FakeGet(Q_TRIPS) == 2 && FakeGet(Q_PLAYER_X) == 7000);
    Check("the plugin doesn't fade in on top of the game", FakeGet(Q_MOD_FADEINS) == 0 && FakeGet(Q_FADE) == 0);
    FakeSet(S_SIMFADEIN, 0);
    FakeSet(S_DEST_X, 5000);

    Section("Backing out of the menu");
    ResetWorld();
    Kneel();
    Press(SPACE);
    Frames(3);
    Check("menu open", FakeGet(Q_MENU_OPEN) == 1);
    FakeSet(S_MENU, 1);
    Frames(3);
    Check("back out: menu closed, camp bit restored, no fade",
          FakeGet(Q_MENU_OPEN) == 0 && !CampBit() && FakeGet(Q_MOD_FADEOUTS) == 0 && G(GL_FT_STATE) == 0);
    Check("prompt back in the kneel menu", FakeGet(Q_LIVE_PROMPTS) == 1);
    Press(SPACE);
    Frames(3);
    Check("and it works a second time", FakeGet(Q_MENU_OPEN) == 1 && FakeGet(Q_MENU_OPENS) == 2);
    StandUp();
    Frames(3);
    Check("standing up with the menu open: the game closes it, camp bit restored",
          FakeGet(Q_MENU_OPEN) == 0 && !CampBit() && G(GL_FT_STATE) == 0);

    Section("The camp bit was already set by the game");
    ResetWorld();
    FakeSetGlobal(GL_CAMP, CAMP_BIT | 4);
    Kneel();
    Press(SPACE);
    Frames(3);
    FakeSet(S_MENU, 1);
    Frames(3);
    Check("left set afterwards", (G(GL_CAMP) & (CAMP_BIT | 4)) == (CAMP_BIT | 4));
    StandUp();
    FakeSetGlobal(GL_CAMP, 0);

    Section("Nowhere to go");
    ResetWorld();
    FakeSet(S_DESTINATIONS, 0);
    Kneel();
    Press(SPACE);
    Frames(3);
    Check("the game's message NO_REGION_FT", HelpIs("NO_REGION_FT"));
    Check("no menu, camp bit restored", FakeGet(Q_MENU_OPENS) == 0 && !CampBit() && G(GL_FT_FLAGS) == 0);
    Check("prompt back", FakeGet(Q_LIVE_PROMPTS) == 1);
    StandUp();
    FakeSet(S_DESTINATIONS, 6);

    Section("Your horse");
    ResetWorld();
    FakeHorse(100, 0, 1, 1, 10, 0, 2, 150);   // dead
    Kneel();
    Check("dead horse: greyed out", FakeGet(Q_LIVE_PROMPTS) == 1 && FakeGet(Q_P_ENABLED) == 0);
    Press(SPACE);
    Frames(2);
    Check("pressing it: the game's DEAD_HORSE_FT, nothing written", HelpIs("DEAD_HORSE_FT") && G(GL_FT_FLAGS) == 0 && !CampBit());
    StandUp();

    ResetWorld();
    FakeHorse(100, 0, 1, 0, 1000, 0, 2, 150);  // alive, 1 km away
    Kneel();
    Check("horse too far: greyed out", FakeGet(Q_P_ENABLED) == 0);
    Press(SPACE);
    Frames(2);
    Check("pressing it: FAR_HORSE_FT", HelpIs("FAR_HORSE_FT"));
    StandUp();

    ResetWorld();
    FakeHorse(100, 0, 1, 0, 130, 0, 4, 1000);  // bonding 4: range 150 m
    Kneel();
    Check("bonding 4 horse at 130 m: within whistling range", FakeGet(Q_P_ENABLED) == 1);
    StandUp();
    FakeHorse(100, 0, 1, 0, 110, 0, 1, 100);   // bonding 1, no progress: range 75 m
    Kneel();
    Check("bonding 1 horse at 110 m: too far", FakeGet(Q_P_ENABLED) == 0);
    StandUp();
    FakeHorse(100, 0, 1, 0, 90, 0, 1, 300);    // bonding 1, about to level up: range 100 m
    Kneel();
    Check("bonding 1 horse at 90 m, nearly level 2: within range", FakeGet(Q_P_ENABLED) == 1);
    StandUp();

    ResetWorld();
    FakeSetGlobal(1136 + 1 + 0 * 436 + 9, 0);  // the saved model for slot 0 isn't valid
    Kneel();
    Check("saved horse model not valid: too far, as the game counts it", FakeGet(Q_P_ENABLED) == 0);
    Press(SPACE);
    Frames(2);
    Check("...FAR_HORSE_FT", HelpIs("FAR_HORSE_FT"));
    StandUp();

    ResetWorld();
    FakeHorse(100, 0, 0, 0, 0, 0, 1, 0);       // no horse in either slot
    Kneel();
    Press(SPACE);
    Frames(2);
    Check("no horse: NO_HORSE_FT", HelpIs("NO_HORSE_FT") && FakeGet(Q_P_ENABLED) == 0);
    StandUp();

    ResetWorld();
    FakeHorse(100, 0, 1, 0, 1000, 0, 2, 150);  // active horse far away...
    FakeHorse(101, 1, 1, 0, 20, 0, 2, 150);    // ...but the slot-1 horse is close
    Kneel();
    Check("slot-1 horse nearby is enough (as at a campfire)", FakeGet(Q_P_ENABLED) == 1);
    Press(SPACE);
    Frames(3);
    FakeSet(S_MENU, 2);
    RunUntil([] { return FakeGet(Q_TRIPS) == 3; }, 300);
    Check("...and the game takes that horse along", FakeGet(Q_TAKEN_HORSE) == 101 && FakeGet(Q_MOUNTED) == 101);
    RunUntil([] { return FakeGet(Q_FADE) == 0; }, 200);
    StandUp();

    ResetWorld();
    WriteIni("RequireHorseNearby=0\r\n");
    FakeHorse(100, 0, 1, 0, 1000, 0, 2, 150);  // far away
    Kneel();
    Check("RequireHorseNearby=0: allowed with the horse far away", FakeGet(Q_P_ENABLED) == 1);
    Press(SPACE);
    Frames(3);
    FakeSet(S_MENU, 2);
    RunUntil([] { return FakeGet(Q_TRIPS) == 4; }, 300);
    Check("...and he arrives on foot", FakeGet(Q_TRIPS) == 4 && FakeGet(Q_MOUNTED) == 0);
    RunUntil([] { return FakeGet(Q_FADE) == 0; }, 200);
    StandUp();
    FakeHorse(100, 0, 1, 1, 10, 0, 2, 150);    // dead
    Kneel();
    Check("RequireHorseNearby=0 still needs a living horse", FakeGet(Q_P_ENABLED) == 0);
    StandUp();
    WriteIni("");

    Section("The game's other rules");
    ResetWorld();
    FakeSet(S_WANTED, 1);
    Kneel();
    Press(SPACE);
    Frames(2);
    Check("wanted: greyed out, says why", FakeGet(Q_P_ENABLED) == 0 && HelpHas("wanted") && G(GL_FT_FLAGS) == 0);
    StandUp();
    WriteIni("AllowWhenWanted=1\r\n");
    Kneel();
    Check("AllowWhenWanted=1: allowed", FakeGet(Q_P_ENABLED) == 1);
    StandUp();
    WriteIni("");
    FakeSet(S_WANTED, 0);

    FakeSet(S_MISSION, 1);
    Kneel();
    Press(SPACE);
    Frames(2);
    Check("on a mission: greyed out, says why", FakeGet(Q_P_ENABLED) == 0 && HelpHas("mission"));
    StandUp();
    FakeSet(S_MISSION, 0);

    FakeSetGlobal(GL_LODGING, 2);
    Kneel();
    Press(SPACE);
    Frames(2);
    Check("not unlocked (lodging 2): greyed out, says why", FakeGet(Q_P_ENABLED) == 0 && HelpHas("unlocked"));
    StandUp();
    FakeSetGlobal(GL_UNLOCK_FLOW, 5);
    FakeSetGlobal(GL_FLOW_STATUS + 1 + 5 * 7 + 1, 5);
    Kneel();
    Check("unlocked the other way (flow 68 done)", FakeGet(Q_P_ENABLED) == 1);
    StandUp();
    FakeSetGlobal(GL_FLOW_STATUS + 1 + 5 * 7 + 1, 2);
    WriteIni("RequireUnlock=0\r\n");
    Kneel();
    Check("RequireUnlock=0: allowed before unlocking", FakeGet(Q_P_ENABLED) == 1);
    StandUp();
    WriteIni("");
    FakeSetGlobal(GL_LODGING, 3);

    FakeSet(S_NO_SCENARIO_TASK, 1);
    FakeSet(S_KNEEL, 1);
    RunUntil([] { return FakeGet(Q_LIVE_PROMPTS) == 1; }, 60);
    Check("kneel without a scenario task (the game would close its menu): greyed out from the start",
          FakeGet(Q_P_ENABLED) == 0 && FakeGet(Q_KNEEL) == K_DOWN);
    RunUntil([] { return FakeGet(Q_KNEEL) == K_KNEEL; }, 60);
    Frames(2);
    Check("...and once he's down", FakeGet(Q_P_ENABLED) == 0);
    Check("...and the log says so", LogHas("PED_HAS_USE_SCENARIO_TASK is false"));
    StandUp();
    WriteIni("ShowWhenUnavailable=0\r\n");
    FakeSet(S_KNEEL, 1);
    {
        bool shown = RunUntil([] { return FakeGet(Q_LIVE_PROMPTS) != 0; }, 45);
        Check("...with ShowWhenUnavailable=0, never shown at all", !shown);
    }
    StandUp();
    WriteIni("");
    FakeSet(S_NO_SCENARIO_TASK, 0);

    FakeSetGlobal(GL_MENU_STOP_A, 1);   // the journal is open
    Kneel();
    Check("journal open (the game would close its menu): greyed out", FakeGet(Q_P_ENABLED) == 0);
    StandUp();
    FakeSetGlobal(GL_MENU_STOP_A, 0);
    FakeSetGlobal(GL_MENU_STOP_B, 1);   // the satchel is open
    Kneel();
    Check("satchel open (likewise): greyed out", FakeGet(Q_P_ENABLED) == 0);
    StandUp();
    FakeSetGlobal(GL_MENU_STOP_B, 0);
    Kneel();
    Press(SPACE);
    Frames(3);
    FakeSetGlobal(GL_MENU_STOP_A, 1);   // the journal opened over the fast travel menu
    Frames(2);
    Check("...and with the menu open: the game closes it, the plugin goes back to idle, camp bit restored",
          FakeGet(Q_MENU_OPEN) == 0 && G(GL_FT_STATE) == 0 && !CampBit() && LogHas("1357517=1"));
    FakeSetGlobal(GL_MENU_STOP_A, 0);
    StandUp();

    FakeSet(S_PLAYERMENU, 1);
    Kneel();
    Check("a game menu up: greyed out", FakeGet(Q_P_ENABLED) == 0);
    StandUp();
    FakeSet(S_PLAYERMENU, 0);
    FakeSetGlobal(GL_UIBUSY, 1);
    Kneel();
    Check("game UI busy flag: greyed out", FakeGet(Q_P_ENABLED) == 0);
    StandUp();
    FakeSetGlobal(GL_UIBUSY, 0);
    FakeSet(S_CANLAUNCH, 0);
    Kneel();
    Check("menu can't launch right now: greyed out", FakeGet(Q_P_ENABLED) == 0);
    StandUp();
    FakeSet(S_CANLAUNCH, 1);
    FakeSetGlobal(GL_FT_STATE, 2);
    Kneel();
    Check("another fast travel under way: greyed out", FakeGet(Q_P_ENABLED) == 0);
    StandUp();
    FakeSetGlobal(GL_FT_STATE, 0);

    WriteIni("ShowWhenUnavailable=0\r\n");
    FakeSet(S_WANTED, 1);
    Kneel();
    Check("ShowWhenUnavailable=0: hidden when it can't be used", FakeGet(Q_LIVE_PROMPTS) == 0);
    FakeSet(S_WANTED, 0);
    Frames(3);
    Check("...and shown when it can", FakeGet(Q_LIVE_PROMPTS) == 1);
    StandUp();
    WriteIni("");

    Section("The Fast Travel text (the game's CAMP text)");
    ResetWorld();
    {
        int dels = FakeGet(Q_CAMP_DELETES);
        Kneel();
        Check("loaded while kneeling", FakeGet(Q_CAMP_LOADED) == 1);
        StandUp();
        Frames(60);
        Check("...kept for a moment after the kneel", FakeGet(Q_CAMP_LOADED) == 1);
        Frames(90);
        Check("...then let go of, as the Craft menu does", FakeGet(Q_CAMP_LOADED) == 0 && FakeGet(Q_CAMP_DELETES) == dels + 1);
    }

    ResetWorld();
    FakeSet(S_CAMP_LOAD_FRAMES, 20);   // slower to load than the kneel menu is to come up
    {
        int made = FakeGet(Q_PROMPTS_MADE);
        FakeSet(S_KNEEL, 1);
        RunUntil([] { return FakeGet(Q_LIVE_PROMPTS) == 1; }, 60);
        Check("not loaded yet when the kneel menu comes up: the game's words as plain text",
              FakeGet(Q_CAMP_LOADED) == 0 && RawIs("~LIT~Fast Travel") && ShowsText("Fast Travel"));
        RunUntil([] { return FakeGet(Q_CAMP_LOADED) == 1; }, 60);
        Frames(5);
        Check("...and left as it is once the text has loaded", FakeGet(Q_PROMPTS_MADE) == made + 1 && FakeGet(Q_LIVE_PROMPTS) == 1);
        StandUp();
    }
    FakeSet(S_CAMP_LOAD_FRAMES, 3);

    ResetWorld();
    Kneel();
    FakeSet(S_CRAFTING, 1);   // Craft (R): the crafting menu opens
    Frames(5);
    Check("Craft menu open: Fast Travel out of its way", FakeGet(Q_LIVE_PROMPTS) == 0);
    FakeSet(S_CRAFTING, 0);   // ...and closes, unloading the CAMP text as it does
    Frames(2);
    Check("back from the Craft menu, which unloaded the CAMP text: Fast Travel back, with its text",
          FakeGet(Q_LIVE_PROMPTS) == 1 && FakeGet(Q_CAMP_LOADED) == 0 && ShowsText("Fast Travel") && FakeGet(Q_BLANK_FRAMES) == 0);
    Check("...and the text is loaded again", RunUntil([] { return FakeGet(Q_CAMP_LOADED) == 1; }, 90));
    Press(SPACE);
    Frames(2);
    Check("...and it works", FakeGet(Q_MENU_OPEN) == 1);
    FakeSet(S_MENU, 1);
    Frames(3);
    StandUp();

    ResetWorld();
    Kneel();
    Check("while the text is loaded: the game's label", RawIs("CAMP_FT_PROMPT") && ShowsText("Fast Travel"));
    FakeSet(S_CAMP_LOADED, 0);   // something unloads the CAMP text under the kneel menu
    g_textPulls++;
    Frames(2);
    Check("text unloaded under it: Fast Travel switches to the same words as plain text",
          FakeGet(Q_LIVE_PROMPTS) == 1 && RawIs("~LIT~Fast Travel") && ShowsText("Fast Travel"));
    Check("...blank for one frame at most", FakeGet(Q_BLANK_FRAMES) <= 1);
    StandUp();
    Frames(120);

    ResetWorld();
    FakeSet(S_CAMP_LOADED, 1);   // loaded by the game already (as with a camp of yours set up elsewhere)
    {
        int req = FakeGet(Q_CAMP_REQUESTS), del = FakeGet(Q_CAMP_DELETES);
        Kneel();
        StandUp();
        Frames(150);
        Check("text the game had loaded: not asked for, and not unloaded afterwards",
              FakeGet(Q_CAMP_REQUESTS) == req && FakeGet(Q_CAMP_DELETES) == del && FakeGet(Q_CAMP_LOADED) == 1);
    }
    FakeSet(S_CAMP_LOADED, 0);

    for (int i = 0; i < 5; i++)
    {
        ResetWorld();
        Kneel();                             // the plugin loads the CAMP text
        int dels = FakeGet(Q_CAMP_DELETES);
        FakeSet(S_CAMP_SCRIPT, 1 << i);      // a camp script starts using it
        StandUp();
        Frames(150);
        bool kept = FakeGet(Q_CAMP_LOADED) == 1 && FakeGet(Q_CAMP_DELETES) == dels;
        FakeSet(S_CAMP_SCRIPT, 0);           // ...and ends (unloading the text itself, if it does)
        Frames(2);
        bool after = FakeGet(Q_CAMP_LOADED) == 0 && FakeGet(Q_CAMP_DELETES) == dels + (kCampScriptUnloads[i] ? 0 : 1);
        char name[160];
        snprintf(name, sizeof name, "%s running: the text is left loaded for it, then unloaded once, %s",
                 kCampScripts[i], kCampScriptUnloads[i] ? "by the script itself" : "by the plugin");
        Check(name, kept && after);
    }

    ResetWorld();
    FakeSet(S_CAMP_LOAD_FRAMES, 150);    // the text takes 5 s to come in
    {
        int dels = FakeGet(Q_CAMP_DELETES);
        FakeSet(S_KNEEL, 1);
        Frames(5);
        StandUp();                       // up again long before it has loaded
        Frames(120);                     // past the plugin's 3 s
        bool waited = FakeGet(Q_CAMP_LOADED) == 0 && FakeGet(Q_CAMP_DELETES) == dels;
        bool unloaded = RunUntil([dels] { return FakeGet(Q_CAMP_DELETES) == dels + 1; }, 150);
        Check("a load still under way when the plugin lets go: unloaded once it's in, not left behind",
              waited && unloaded && FakeGet(Q_CAMP_LOADED) == 0);
    }
    FakeSet(S_CAMP_LOAD_FRAMES, 3);

    ResetWorld();
    FakeHorse(100, 0, 1, 1, 10, 0, 2, 150);   // dead
    Kneel();
    Press(SPACE);
    Frames(2);
    Check("the game's message (from the CAMP text)", HelpIs("DEAD_HORSE_FT"));
    StandUp();
    Frames(120);
    Check("...the text stays loaded while that message is on screen", FakeGet(Q_CAMP_LOADED) == 1);
    Frames(150);
    Check("...and is let go of afterwards", FakeGet(Q_CAMP_LOADED) == 0);

    ResetWorld();
    FakeSet(S_CAMP_NEVER, 1);
    FakeHorse(100, 0, 1, 1, 10, 0, 2, 150);   // dead
    Kneel();
    Check("a CAMP text that never loads: Fast Travel still shows, as plain text",
          FakeGet(Q_LIVE_PROMPTS) == 1 && RawIs("~LIT~Fast Travel") && ShowsText("Fast Travel"));
    Press(SPACE);
    Frames(2);
    Check("...and the game's messages come in English", HelpIs("~LIT~You can't fast travel as your horse is dead."));
    StandUp();
    FakeSet(S_CAMP_NEVER, 0);
    Frames(120);

    ResetWorld();
    FakeSet(S_FRENCH, 1);
    Kneel();
    Check("a French game: the label, in French", RawIs("CAMP_FT_PROMPT") && ShowsText("Voyage rapide"));
    StandUp();
    Frames(120);
    FakeSet(S_CAMP_LOAD_FRAMES, 20);
    Kneel();
    Check("...and before the text has loaded, the same French words as plain text",
          RawIs("~LIT~Voyage rapide") && ShowsText("Voyage rapide"));
    StandUp();
    FakeSet(S_FRENCH, 0);
    FakeSet(S_CAMP_LOAD_FRAMES, 3);
    Kneel();
    Check("back in English", RawIs("CAMP_FT_PROMPT") && ShowsText("Fast Travel"));
    StandUp();

    Section("Settings");
    WriteIni("Enabled=0\r\n");
    Kneel();
    Frames(5);
    Check("Enabled=0: no prompt", FakeGet(Q_LIVE_PROMPTS) == 0);
    WriteIni("Enabled=1\r\n");
    Frames(70);
    Check("Enabled=1 again: back without restarting", FakeGet(Q_LIVE_PROMPTS) == 1);
    StandUp();
    WriteIni("PromptText=Ride Elsewhere\r\nPromptControl=INPUT_CONTEXT_LT\r\n");
    Kneel();
    {
        char t[300];
        FakeText(1, t, sizeof t);
        Check("PromptText: shown as typed", strcmp(t, "~LIT~Ride Elsewhere") == 0, t);
    }
    Check("PromptControl: another button", (uint32_t)FakeGet(Q_P_CONTROL) == Joaat("INPUT_CONTEXT_LT"));
    StandUp();
    WriteIni("PromptControl=INPUT_NOT_A_THING\r\n");
    Kneel();
    Check("an unknown PromptControl falls back to Space", (uint32_t)FakeGet(Q_P_CONTROL) == SPACE);
    StandUp();
    WriteIni("StandUp=0\r\n");
    ResetWorld();
    Kneel();
    Press(SPACE);
    Frames(3);
    FakeSet(S_MENU, 2);
    Frames(3);
    Check("StandUp=0: no stand-up animation, still fades", FakeGet(Q_KNEEL_EXITS) == 0 && FakeGet(Q_MOD_FADEOUTS) == 1);
    RunUntil([] { return FakeGet(Q_FADE) == 0 && G(GL_FT_STATE) == 0; }, 400);
    WriteIni("");
    StandUp();

    Section("Moving under the menu");
    ResetWorld();
    Kneel();
    Press(SPACE);
    Frames(3);
    FakeSet(S_MOVE, 1);   // W or the left stick held in the menu
    Frames(12);
    Check("movement under the menu: Arthur gets up, the game closes the menu", G(GL_FT_STATE) == 0 && FakeGet(Q_KNEEL) != K_KNEEL);
    Check("...the log says why", LogHas("got up from the kneel while the menu was open"));
    Check("...and a tip says what to use instead", HelpHas("arrow keys"));
    Check("the plugin didn't touch the control context", FakeGet(Q_CONTEXT_SETS) == 0);
    FakeSet(S_MOVE, 0);
    StandUp();
    WriteIni("LockMovementInMenu=1\r\n");
    ResetWorld();
    Kneel();
    Press(SPACE);
    Frames(3);
    FakeSet(S_MOVE, 1);
    Frames(30);
    Check("LockMovementInMenu=1: the menu stays open while scrolling", G(GL_FT_STATE) == 1 && FakeGet(Q_KNEEL) == K_KNEEL);
    Check("...by setting the menu control context each frame", FakeGet(Q_CONTEXT_SETS) >= 30);
    FakeSet(S_MOVE, 0);
    FakeSet(S_MENU, 2);
    RunUntil([] { return G(GL_FT_STATE) == 0 && FakeGet(Q_FADE) == 0; }, 400);
    int sets = FakeGet(Q_CONTEXT_SETS);
    Frames(10);
    Check("...and only while the menu is open", FakeGet(Q_CONTEXT_SETS) == sets);
    StandUp();
    WriteIni("");

    Section("Trouble along the way");
    ResetWorld();
    FakeSet(S_MENU_SHOWS, 0);
    Kneel();
    Press(SPACE);
    Frames(3);
    Check("menu open on paper but not showing", G(GL_FT_STATE) == 1 && FakeGet(Q_MENU_ACTIVE) == 0);
    RunUntil([] { return G(GL_FT_STATE) == 0; }, 150);
    Check("after 2.5 s the plugin asks the game to close it (bit 16)", G(GL_FT_STATE) == 0 && !CampBit());
    Check("...and logs it", LogHas("isn't showing"));
    FakeSet(S_MENU_SHOWS, 1);
    StandUp();

    ResetWorld();
    Kneel();
    Press(SPACE);
    Frames(3);
    FakeSet(S_PLAYERDEAD, 1);
    Frames(3);
    Check("player dies with the menu open: menu closed, camp bit restored", G(GL_FT_STATE) == 0 && !CampBit());
    FakeSet(S_PLAYERDEAD, 0);
    StandUp();

    ResetWorld();
    Kneel();
    Press(SPACE);
    Frames(3);
    FakeSet(S_MENU, 2);
    Frames(4);
    FakeSet(S_PLAYERDEAD, 1);
    Frames(3);
    Check("player dies during the fade: the game's fast travel is reset", G(GL_FT_STATE) == 0 && G(GL_FT_FLAGS) == 0);
    FakeSet(S_PLAYERDEAD, 0);
    StandUp();

    ResetWorld();
    FakeSet(S_REFUSE, 1);
    Kneel();
    Press(SPACE);
    Frames(3);
    FakeSet(S_MENU, 2);
    RunUntil([] { return FakeGet(Q_FADE) == 2; }, 100);
    Frames(5);
    Check("trip refused: waiting", G(GL_FT_STATE) == 2);
    bool reset = RunUntil([] { return G(GL_FT_STATE) == 0; }, 500);
    Check("after 12 s: reset, screen back, says so", reset && G(GL_FT_FLAGS) == 0 && HelpHas("didn't start"));
    Check("...the horse's trip flag cleared and the music stopped, as the game's own finish does",
          FakeGet(Q_PCF_100) == 0 && FakeGet(Q_MOD_MUSIC_STOPS) == 1);
    RunUntil([] { return FakeGet(Q_FADE) == 0; }, 100);
    Check("screen visible again", FakeGet(Q_FADE) == 0);
    FakeSet(S_REFUSE, 0);
    StandUp();

    ResetWorld();
    Kneel();
    Press(SPACE);
    Frames(3);
    FakeSet(S_MENU, 2);
    Frames(3);
    FakeSetGlobal(GL_FT_FLAGS, G(GL_FT_FLAGS) | 32);   // a camp elsewhere packs up mid-fade (player_camp's cleanup)
    Frames(3);
    Check("the game starts the trip before the fade ends: the plugin doesn't step in", FakeGet(Q_CLEAR_TASKS) == 0);
    RunUntil([] { return G(GL_FT_STATE) == 0 && FakeGet(Q_FADE) == 0; }, 400);
    Check("...and the trip finishes, screen back", FakeGet(Q_FADE) == 0 && G(GL_FT_FLAGS) == 0 && FakeGet(Q_CLEAR_TASKS) == 0);
    StandUp();

    ResetWorld();
    FakeSet(S_STUCK_TRAVEL, 1);
    Kneel();
    Press(SPACE);
    Frames(3);
    FakeSet(S_MENU, 2);
    RunUntil([] { return G(GL_FT_STATE) == 5; }, 200);
    Frames(30 * 60);   // a minute
    Check("a trip still loading after a minute: screen still black, left alone", FakeGet(Q_FADE) == 2 && G(GL_FT_STATE) == 5);
    RunUntil([] { return FakeGet(Q_FADE) != 2; }, 30 * 70);
    Check("after 2 minutes: the screen comes back, the game's trip untouched", FakeGet(Q_FADE) != 2 && G(GL_FT_STATE) == 5);
    Check("...and the log says so", LogHas("still going after 2 minutes"));
    FakeSet(S_STUCK_TRAVEL, 0);
    RunUntil([] { return G(GL_FT_STATE) == 0; }, 200);
    Frames(120);
    Check("when it finishes, all back to idle", G(GL_FT_STATE) == 0 && G(GL_FT_FLAGS) == 0);
    StandUp();

    ResetWorld();
    FakeSet(S_SU_IGNORE, 1);
    Kneel();
    Press(SPACE);
    FakeSet(S_PLAYERDEAD, 1);
    Frames(2);
    Check("player dies right after pressing: request taken back, camp bit restored", G(GL_FT_FLAGS) == 0 && !CampBit());
    FakeSet(S_PLAYERDEAD, 0);
    FakeSet(S_SU_IGNORE, 0);
    StandUp();

    ResetWorld();
    FakeSetGlobal(GL_FT_FLAGS, 16);           // a leftover close request
    Kneel();
    Press(SPACE);
    Frames(5);
    Check("a leftover close request doesn't shut the new menu", G(GL_FT_STATE) == 1 && FakeGet(Q_MENU_OPEN) == 1);
    FakeSet(S_MENU, 1);
    Frames(3);
    StandUp();

    ResetWorld();
    FakeSet(S_ONLINE, 1);
    Frames(3);
    FakeSet(S_KNEEL, 1);
    Frames(30);
    Check("online: nothing", FakeGet(Q_LIVE_PROMPTS) == 0);
    FakeSet(S_ONLINE, 0);
    StandUp();

    Section("Set Up Camp anywhere");
    {
        const uint32_t E = Joaat("INPUT_CONTEXT_Y");

        // Out in the wilderness the kneel menu has its own Set Up Camp: left to it.
        ResetWorld();
        Kneel();
        Check("where the game allows a camp: the kneel menu's own Set Up Camp, the mod adds none",
              FakeGet(Q_KNEEL_OWN_CAMP) == 1 && FakeGet(Q_CAMP_PROMPTS) == 0 && FakeGet(Q_FT_PROMPTS) == 1);
        Check("...the log says so", LogHas("Set Up Camp: the kneel menu has its own here."));
        Press(E);
        Frames(3);
        Check("...and E there is the game's own button: the mod starts nothing",
              FakeGet(Q_GAME_CAMP_STARTS) == 1 && FakeGet(Q_CAMP_STARTS) == 0 && FakeGet(Q_SCRIPT_REQUESTS) == 0);
        StandUp();

        // In a town the kneel menu leaves it out: the mod adds it.
        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        FakeSet(S_KNEEL, 1);
        {
            bool drawn = RunUntil([] { return FakeGet(Q_GROUP_DRAWN) == 1; }, 60);
            Check("in a town (no Set Up Camp in the kneel menu): the mod adds one, with the kneel menu's own buttons",
                  drawn && FakeGet(Q_KNEEL_OWN_CAMP) == 0 && FakeGet(Q_CAMP_PROMPTS) == 1 && FakeGet(Q_FT_PROMPTS) == 1 &&
                  FakeGet(Q_KNEEL) == K_DOWN);
        }
        RunUntil([] { return FakeGet(Q_KNEEL) == K_KNEEL; }, 60);
        Frames(2);
        Check("...on E (INPUT_CONTEXT_Y), in the kneel menu's group, tab 0",
              (uint32_t)FakeGet(Q_CP_CONTROL) == E && FakeGet(Q_CP_GROUP) == kRestGroup && FakeGet(Q_CP_TAB) == 0);
        Check("...built as the game builds its own (priority 1, any transport, attribute 18, a tap that fires on press)",
              FakeGet(Q_CP_PRIORITY) == 1 && FakeGet(Q_CP_TRANSPORT) == 0 && FakeGet(Q_CP_ATTR18) == 1 &&
              FakeGet(Q_CP_STANDARD) == 1 && FakeGet(Q_CP_RELEASE) == 0);
        Check("...with the game's own text (SET_CAMP: \"Set Up Camp\")", CampRawIs("SET_CAMP") && CampShows("Set Up Camp"));
        Check("...enabled and visible", FakeGet(Q_CP_ENABLED) == 1 && FakeGet(Q_CP_VISIBLE) == 1);
        Check("...the log says so", LogHas("Set Up Camp: added"));
        Press(E);
        Check("pressing it: the game's camp script asked for, both of the mod's buttons out of the way",
              FakeGet(Q_SCRIPT_REQUESTS) == 1 && FakeGet(Q_CAMP_PROMPTS) == 0 && FakeGet(Q_FT_PROMPTS) == 0 &&
              LogHas("Set Up Camp pressed: starting the game's camp (a full camp"));
        Frames(1);
        Check("...the kneel menu's other buttons held meanwhile (Craft, Leave, Fast Travel, Jump)",
              FakeGet(Q_DISABLED_X) == 1 && FakeGet(Q_DISABLED_Y) == 1 && FakeGet(Q_DISABLED_B) == 1 &&
              FakeGet(Q_DISABLED_A) == 1 && FakeGet(Q_DISABLED_JUMP) == 1);
        Check("...and hidden, as the game's own Set Up Camp takes them away (but not deleted: they're the game's)",
              FakeGet(Q_KNEEL_PROMPTS) == 2 && FakeGet(Q_KNEEL_VISIBLE) == 0);
        bool started = RunUntil([] { return FakeGet(Q_CAMP_STARTS) >= 1; }, 30);
        Check("...then player_camp started as the kneel menu's own button starts it: 10 arguments, stack 6096",
              started && FakeGet(Q_CAMP_ARGC) == 10 && FakeGet(Q_CAMP_STACK) == 6096);
        {
            static const int full[10] = { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 };
            char d[120];
            CampArgsText(d, sizeof d);
            Check("...arguments: flags 0 (horse near: a full camp), 0, 0, 0, 0, 1, no camp spot", CampArgsAre(full), d);
        }
        Check("...the script let go of once it's running", FakeGet(Q_NO_LONGER_NEEDED) == 1 && FakeGet(Q_BAD_SCRIPT_CALLS) == 0);
        {
            bool up = RunUntil([] { return FakeGet(Q_KNEEL) == K_OFF; }, 60);
            Check("player_camp gets Arthur up, as from the game's own button", up && FakeGet(Q_CAMP_KNEEL_EXITS) == 1);
        }
        Frames(5);
        Check("...one camp, nothing left in the kneel menu, the controls given back",
              FakeGet(Q_CAMP_STARTS) == 1 && FakeGet(Q_LIVE_PROMPTS) == 0 && FakeGet(Q_DISABLED_X) == 0 &&
              FakeGet(Q_DISABLED_JUMP) == 0);
        Check("...the log follows it", LogHas("The game's camp script is running.") &&
                                       LogHas("Arthur is up, the game is pitching the camp"));
        {
            bool pitched = RunUntil([] { return (G(GL_CAMP_IS_UP) & CAMP_BIT) != 0; }, 60);
            Check("the game pitches the camp (at its nearest campsite)", pitched && FakeGet(Q_CAMP_RUNNING) == 1);
        }
        Kneel();
        Check("kneeling with the camp up: no Set Up Camp added, Fast Travel as usual",
              FakeGet(Q_CAMP_PROMPTS) == 0 && FakeGet(Q_FT_PROMPTS) == 1 && LogHas("not added: a camp is already up"));
        StandUp();
        FakeSet(S_CAMP_DOWN, 1);   // packed up
        Frames(2);
        Kneel();
        Check("packed up: Set Up Camp back in the town", FakeGet(Q_CAMP_PROMPTS) == 1);
        StandUp();

        // Pressed as soon as it shows, before Arthur is all the way down (the game's own works then too).
        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        FakeSet(S_KNEEL, 1);
        RunUntil([] { return FakeGet(Q_CAMP_PROMPTS) == 1; }, 60);
        Press(E);
        Check("pressed while he's still getting down: player_camp starts at once (it gets him up itself)",
              FakeGet(Q_KNEEL) == K_DOWN && CampRuns() && FakeGet(Q_CAMP_STARTS) == 1);
        Frames(30);

        // Fast Travel pressed first (waiting for him to get down), then Set Up Camp.
        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        FakeSet(S_KNEEL, 1);
        RunUntil([] { return FakeGet(Q_CAMP_PROMPTS) == 1; }, 60);
        Press(SPACE);
        Press(E);
        Check("Fast Travel pressed while getting down, then Set Up Camp: the camp, and the Fast Travel press dropped",
              CampRuns() && LogHas("Set Up Camp was pressed") && FakeGet(Q_MENU_OPENS) == 0 && G(GL_FT_FLAGS) == 0 && !CampBit());
        Frames(30);

        // A small camp when the horse isn't near, as the game's button picks.
        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        FakeHorse(100, 0, 1, 0, 1000, 0, 2, 150);   // 1 km away
        Kneel();
        Check("horse out of whistling range: Set Up Camp still offered (Fast Travel greyed out)",
              FakeGet(Q_CAMP_PROMPTS) == 1 && FakeGet(Q_CP_ENABLED) == 1 && FakeGet(Q_P_ENABLED) == 0);
        Press(E);
        {
            static const int small[10] = { 128 | 32, 0, 0, 0, 0, 1, 0, 0, 0, 0 };
            char d[120];
            bool ran = CampRuns();
            CampArgsText(d, sizeof d);
            Check("...a small camp (flags 128 + 32), as the game's button picks", ran && CampArgsAre(small), d);
        }
        Frames(30);
        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        FakeSetGlobal(GL_HORSE_AWAY, 1);              // the horse is near, but "away" as the game marks it
        Kneel();
        Press(E);
        Check("horse near but marked away: a small camp", CampRuns() && FakeGet(Q_CAMP_ARG0) == 160);
        Frames(30);
        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        FakeHorse(100, 0, 1, 0, 1000, 0, 2, 150);   // the active horse far away...
        FakeHorse(102, -1, 1, 0, 3, 0, 1, 0);       // ...but leading another horse, 3 m off
        FakeSetGlobal(GL_LED_HORSE, 102);
        Kneel();
        Press(E);
        Check("leading a horse within 5 m: a full camp", CampRuns() && FakeGet(Q_CAMP_ARG0) == 0);
        Frames(30);

        // The camp kit.
        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        FakeSet(S_KIT_CAMP, 0);
        Kneel();
        Check("no camp kit: no Set Up Camp", FakeGet(Q_CAMP_PROMPTS) == 0 && LogHas("not added: no camp kit"));
        FakeSet(S_KIT_SIMPLE, 1);
        Frames(2);
        Check("...the simple camp kit will do", FakeGet(Q_CAMP_PROMPTS) == 1);
        FakeSet(S_BACKUP_INV, 1);
        Frames(2);
        Check("...looked for in the backup inventory while the game uses it", FakeGet(Q_CAMP_PROMPTS) == 1 &&
                                                                          FakeGet(Q_BAD_INVENTORY) == 0);
        StandUp();

        // The game's camping rules other than the place.
        CampRule("wanted", [] { FakeSetGlobal(GL_LAW + 20, 1); }, [] { FakeSetGlobal(GL_LAW + 20, 0); },
                 "not added: wanted");
        CampRule("the law closing in", [] { FakeSetGlobal(GL_LAW + 18, 1); FakeSetGlobal(GL_LAW + 19, 2); },
                 [] { FakeSetGlobal(GL_LAW + 18, 0); FakeSetGlobal(GL_LAW + 19, 0); }, "not added: wanted");
        CampRule("a crime just committed", [] { FakeSet(S_RECENT_CRIME, 1); }, [] { FakeSet(S_RECENT_CRIME, 0); },
                 "not added: a crime was committed recently");
        CampRule("in combat", [] { FakeSet(S_COMBAT, 1); }, [] { FakeSet(S_COMBAT, 0); }, "not added: in combat");
        CampRule("on a mission", [] { FakeSetGlobal(GL_ON_MISSION, 1); }, [] { FakeSetGlobal(GL_ON_MISSION, 0); },
                 "not added: on a mission");
        CampRule("on a mission that lets you camp, then off it",
                 [] { FakeSetGlobal(GL_ON_MISSION, 1); FakeSetGlobal(GL_MISSION_CAMP, 2); },
                 [] { FakeSetGlobal(GL_MISSION_CAMP, 3); }, "not added: on a mission");
        CampRule("in a gang hideout", [] { FakeSetGlobal(GL_ZONE, 4); }, [] { FakeSetGlobal(GL_ZONE, 0); },
                 "not added: in a gang hideout");
        CampRule("at the gang's camp", [] { FakeSetGlobal(GL_STORY_CAMP, 1 << 9); }, [] { FakeSetGlobal(GL_STORY_CAMP, 0); },
                 "not added: at the gang's camp");
        CampRule("indoors", [] { FakeSet(S_INTERIOR, 1); }, [] { FakeSet(S_INTERIOR, 0); }, "not added: indoors");
        CampRule("in Guarma", [] { FakeSetGlobal(GL_DISTRICT, 8); }, [] { FakeSetGlobal(GL_DISTRICT, 3); },
                 "not added: in Guarma");
        CampRule("an area under lockdown",
                 [] { FakeSetGlobal(GL_ZONE_AREA, 5); FakeSetGlobal(GL_AREA_FLAGS + 1 + 5 * 11, 1 << 25); },
                 [] { FakeSetGlobal(GL_AREA_FLAGS + 1 + 5 * 11, 0); }, "not added: the area is under lockdown");
        CampRule("a minigame", [] { FakeSet(S_MINIGAME, 1); }, [] { FakeSet(S_MINIGAME, 0); },
                 "not added: something is going on nearby");
        CampRule("a random encounter", [] { FakeSetGlobal(GL_ENCOUNTER, 4711); }, [] { FakeSetGlobal(GL_ENCOUNTER, 0); },
                 "not added: something is going on nearby");
        CampRule("an ambush", [] { FakeSetGlobal(GL_AMBUSH, 16); }, [] { FakeSetGlobal(GL_AMBUSH, 0); },
                 "not added: something is going on nearby");
        CampRule("with an animal", [] { FakeSet(S_ANIMAL_INTERACTION, 1); }, [] { FakeSet(S_ANIMAL_INTERACTION, 0); },
                 "not added: something is going on nearby");
        CampRule("a camp already up", [] { FakeSetGlobal(GL_CAMP_IS_UP, CAMP_BIT); }, [] { FakeSetGlobal(GL_CAMP_IS_UP, 0); },
                 "not added: a camp is already up");
        CampRule("by a campfire", [] { FakeSetGlobal(GL_CAMP, CAMP_BIT); }, [] { FakeSetGlobal(GL_CAMP, 0); },
                 "not added: a camp is already up");
        CampRule("player_camp running", [] { FakeSet(S_CAMP_SCRIPT, 1); },
                 [] { FakeSet(S_CAMP_SCRIPT, 0); g_textPulls++; },   // it unloads the CAMP text as it ends
                 "not added: a camp is already up");
        CampRule("camping blocked by a script", [] { FakeSetGlobal(GL_CAMP_FLAGS2, 1); },
                 [] { FakeSetGlobal(GL_CAMP_FLAGS2, 1 | (1 << 9)); }, "not added: the game has camping blocked");
        CampRule("a mission whose camp has its own spot",
                 [] { FakeSetGlobal(GL_CAMP_FLOW120, 7); FakeSetGlobal(GL_FLOW_STATUS + 1 + 7 * 7 + 1, 3); },
                 [] { FakeSetGlobal(GL_FLOW_STATUS + 1 + 7 * 7 + 1, 5); }, "not added: a mission with its own camp spot");
        CampRule("...the other such mission",
                 [] { FakeSetGlobal(GL_CAMP_FLOW15, 9); FakeSetGlobal(GL_FLOW_STATUS + 1 + 9 * 7 + 1, 4); },
                 [] { FakeSetGlobal(GL_FLOW_STATUS + 1 + 9 * 7 + 1, 0); }, "not added: a mission with its own camp spot");
        FakeSetGlobal(GL_FLOW_STATUS + 1 + 7 * 7 + 1, 0);
        CampRule("a stranger mission that bans camping near it", [] { FakeSet(S_EVELYN, 1); }, [] { FakeSet(S_EVELYN, 0); },
                 "not added: on a mission");
        CampRule("laying bait (no kit used meanwhile)", [] { FakeSetGlobal(GL_ITEM_USE_OFF, 1); },
                 [] { FakeSetGlobal(GL_ITEM_USE_OFF, 0); }, "not added: something is going on nearby");

        // Never a second Set Up Camp.
        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        FakeSet(S_LIST_BROKEN, 1);   // the kneel menu's buttons not where the mod looks for them
        Kneel();
        Check("the kneel menu's own buttons not found: no Set Up Camp risked", FakeGet(Q_CAMP_PROMPTS) == 0 &&
              LogHas("weren't found in the game's prompt list"));
        FakeSet(S_LIST_BROKEN, 0);   // ...found a moment later
        Frames(2);
        Check("...found a moment later: added then (they're looked at every frame)", FakeGet(Q_CAMP_PROMPTS) == 1);
        StandUp();

        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        Kneel();
        FakeSet(S_INTERACTION, 77);
        Frames(3);
        Check("someone to talk to: Set Up Camp goes with the kneel menu's buttons", FakeGet(Q_CAMP_PROMPTS) == 0);
        FakeSet(S_CAMP_HERE, 1);     // ...and when the kneel menu comes back, it has its own
        FakeSet(S_INTERACTION, 0);
        Frames(3);
        Check("...back with its own Set Up Camp this time: the mod's stays out",
              FakeGet(Q_KNEEL_OWN_CAMP) == 1 && FakeGet(Q_CAMP_PROMPTS) == 0 && FakeGet(Q_FT_PROMPTS) == 1);
        StandUp();

        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        FakeSet(S_HUD_BROKEN, 1);    // the kneel menu's bit never seen (another game build)
        Kneel();
        Check("kneel menu bit never seen: Set Up Camp once he's all the way down, as Fast Travel",
              FakeGet(Q_CAMP_PROMPTS) == 1 && FakeGet(Q_FT_PROMPTS) == 1);
        StandUp();
        FakeSet(S_HUD_BROKEN, 0);

        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        Kneel();
        FakeSet(S_SET_CAMP, 1);      // the kneel menu takes its buttons down (as for its own Set Up Camp)
        Frames(2);
        Check("the kneel menu takes its buttons down: Set Up Camp goes too", FakeGet(Q_CAMP_PROMPTS) == 0 &&
                                                                             FakeGet(Q_LIVE_PROMPTS) == 0);
        StandUp();

        // When player_camp has trouble.
        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        FakeSet(S_CAMP_FAILS, 1);    // no campsite the game can use here
        Kneel();
        Press(E);
        Check("no campsite the game can use: player_camp says so itself (\"Cannot create camp in this location.\")",
              CampRuns() && RunUntil([] { return FakeGet(Q_CAMP_RUNNING) == 0; }, 60));
        Frames(2);
        Check("...and the log notes it", LogHas("it found no campsite to use here"));
        FakeSet(S_CAMP_FAILS, 0);

        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        FakeSet(S_CAMP_SCRIPT_NO_LOAD, 1);
        Kneel();
        Press(E);
        Frames(30);
        Check("player_camp still loading: nothing started yet, the buttons held",
              FakeGet(Q_CAMP_STARTS) == 0 && FakeGet(Q_DISABLED_X) == 1 && FakeGet(Q_KNEEL) == K_KNEEL);
        Frames(150);
        Check("...not loaded after 5 s: says so, lets go of it, the kneel menu as it was",
              FakeGet(Q_CAMP_STARTS) == 0 && HelpHas("couldn't be set up") && FakeGet(Q_NO_LONGER_NEEDED) == 1 &&
              FakeGet(Q_CAMP_PROMPTS) == 1 && FakeGet(Q_FT_PROMPTS) == 1 && LogHas("didn't load within 5 s") &&
              FakeGet(Q_KNEEL_PROMPTS) == 2 && FakeGet(Q_KNEEL_VISIBLE) == 2);
        StandUp();

        ResetWorld();
        FakeSet(S_CAMP_HERE, 0);
        FakeSet(S_CAMP_SCRIPT_EXISTS, 0);
        Kneel();
        Press(E);
        Frames(3);
        Check("no player_camp script at all: says so, starts nothing", HelpHas("couldn't be set up") &&
              FakeGet(Q_SCRIPT_REQUESTS) == 0 && LogHas("the game has no player_camp script"));
        StandUp();

        // Settings.
        ResetWorld();
        WriteIni("SetUpCampAnywhere=0\r\n");
        FakeSet(S_CAMP_HERE, 0);
        Kneel();
        Check("SetUpCampAnywhere=0: no Set Up Camp, Fast Travel as before",
              FakeGet(Q_CAMP_PROMPTS) == 0 && FakeGet(Q_FT_PROMPTS) == 1 && LogHas("off (SetUpCampAnywhere=0)"));
        Press(E);
        Frames(3);
        Check("...and E does nothing", FakeGet(Q_SCRIPT_REQUESTS) == 0 && FakeGet(Q_CAMP_STARTS) == 0);
        WriteIni("");
        Frames(70);
        Check("...turned back on while kneeling: there within a couple of seconds", FakeGet(Q_CAMP_PROMPTS) == 1);
        StandUp();

        ResetWorld();
        WriteIni("Enabled=0\r\n");
        FakeSet(S_CAMP_HERE, 0);
        {
            int req = FakeGet(Q_CAMP_REQUESTS);
            Kneel();
            Check("Enabled=0 (Fast Travel off): Set Up Camp alone, and the CAMP text left alone",
                  FakeGet(Q_FT_PROMPTS) == 0 && FakeGet(Q_CAMP_PROMPTS) == 1 && FakeGet(Q_CAMP_REQUESTS) == req);
            Press(E);
            Check("...and it works", CampRuns() && FakeGet(Q_CAMP_STARTS) == 1);
        }
        Frames(30);
        WriteIni("");
        ResetWorld();

        Check("never two Set Up Camps in the kneel menu, nor two of the mod's",
              FakeGet(Q_DUP_FRAMES) == 0 && FakeGet(Q_CAMP_PROMPTS_MAX) <= 1);
    }

    // Last: the game ignores the request (as with globals at other addresses).
    Section("The game doesn't respond");
    ResetWorld();
    FakeSet(S_SU_IGNORE, 1);
    Kneel();
    Press(SPACE);
    Frames(10);
    Check("request pending", (G(GL_FT_FLAGS) & 4) != 0 && CampBit());
    RunUntil([] { return (G(GL_FT_FLAGS) & 4) == 0; }, 200);
    Frames(2);
    Check("after 3 s: request withdrawn, camp bit restored", (G(GL_FT_FLAGS) & 4) == 0 && !CampBit());
    Check("turned off for the session, greyed out", FakeGet(Q_P_ENABLED) == 0);
    Check("log says so", LogHas("didn't pick up the fast travel request"));
    Press(SPACE);
    Frames(2);
    Check("pressing it says it's off", HelpHas("off"));
    FakeSet(S_SU_IGNORE, 0);
    StandUp();
    ResetWorld();
    FakeSet(S_CAMP_HERE, 0);
    Kneel();
    Press(Joaat("INPUT_CONTEXT_Y"));
    Check("...Set Up Camp still works", CampRuns() && FakeGet(Q_CAMP_STARTS) == 1);
    Frames(30);
    ResetWorld();

    Section("Native calls");
    {
        char d[80];
        snprintf(d, sizeof d, "(last unknown 0x%08X)", (unsigned)FakeGet(Q_LAST_UNKNOWN_LO));
        Check("only natives the fake knows", FakeGet(Q_UNKNOWN) == 0, d);
        snprintf(d, sizeof d, "(last 0x%08X)", (unsigned)FakeGet(Q_LAST_BADARG_LO));
        Check("every native got its argument count", FakeGet(Q_BAD_ARGS) == 0, d);
        Check("VAR_STRING always as (10, \"LITERAL_STRING\", text)", FakeGet(Q_BAD_VARSTRING) == 0);
        Check("help texts well formed", FakeGet(Q_BAD_HELP) == 0);
        Check("player_camp only ever asked for, started and let go of by name, with its 10 arguments",
              FakeGet(Q_BAD_SCRIPT_CALLS) == 0);
        Check("the camp kit looked for in the right inventory", FakeGet(Q_BAD_INVENTORY) == 0);
        snprintf(d, sizeof d, "(%d frames, text pulled from under it %d times)", FakeGet(Q_BLANK_TOTAL), g_textPulls);
        Check("over the whole run, Fast Travel never sat in the kneel menu without text "
              "(but for a frame each time the tests pulled the text from under it)", FakeGet(Q_BLANK_TOTAL) <= g_textPulls, d);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
