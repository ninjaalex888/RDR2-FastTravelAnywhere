// Stand-in for ScriptHookRDR2.dll, used only by the automated test (tests/run_tests.sh).
// It exports the same MSVC-mangled names as the real Script Hook, simulates the
// natives and script globals Fast Travel Anywhere uses, and runs a model of the
// two game scripts it works with, written from the decompiled scripts of game
// build 1491.50:
//   * dynamic_craft_scenario: the hold-E kneel, its states and its "Resting"
//     prompt group, which comes up at the kneel animation's event, before
//     Arthur is all the way down, with bit 2 of Global_1955830.f_1 (cleared
//     when it takes its prompts down, but not for an interaction target or
//     the camera);
//   * short_update's "PLAYER CAMP FAST TRAVEL" module (func_80): the requests in
//     Global_43907, its state in Global_43908, the menu and all of its close
//     checks, the horse it takes along and the trip.
// And the game's CAMP text block, where the campfire's texts live: loaded on
// request after a few frames, unloaded by the camp scripts and the Craft menu
// when they end, gone after a trip (as players found in the game); a prompt
// whose label isn't loaded shows no text.
// For Set Up Camp: the kneel menu's own prompts in the scripts' shared prompt
// list (Global_1945188), its own Set Up Camp only where the game allows a camp,
// the script threads, launching player_camp, and player_camp itself (it gets
// Arthur up, then pitches camp or gives up when it finds no campsite).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef void (*ScriptMainFn)();

static ScriptMainFn g_scriptMain = nullptr;
static void*        g_hostFiber  = nullptr;

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

// ---------------------------------------------------------------------------
// Script globals
// ---------------------------------------------------------------------------
static const int kGlobalCount = 2000000;
static uint64_t  g_globals[kGlobalCount];

static int  GI(int i)             { int v; memcpy(&v, &g_globals[i], 4); return v; }
static void SI(int i, int v)      { memcpy(&g_globals[i], &v, 4); }
static void SF(int i, float v)    { memcpy(&g_globals[i], &v, 4); }

enum
{
    GL_PLAYER = 35, GL_FT_FLAGS = 43907, GL_FT_STATE = 43908, GL_CAMP = 1392040,
    GL_UIBUSY = 1898068, GL_UIBUSY4 = 1898072, GL_ACTIVE_HORSE = 1135, GL_HORSE_SAVES = 1136,
    GL_HORSE_PEDS = 1900383, GL_LODGING = 4644, GL_SESSION = 1572899, GL_UNLOCK_FLOW = 1351050,
    GL_FLOW_STATUS = 12106, GL_KNEEL_HUD = 1955831, GL_MENU_STOP_A = 1357517, GL_MENU_STOP_B = 1935690,
    GL_PROMPT_LIST = 1945188, GL_CAMP_IS_UP = 9459, GL_CAMP_STATE = 43885,
};

// ---------------------------------------------------------------------------
// The world
// ---------------------------------------------------------------------------
static const int      kPlayerPed  = 42;
static const uint32_t kHorseModel = 0x12345678;

struct Ent
{
    int      handle;
    int      exists, human, dead, injured, writhe;
    float    x, y, z;
    uint32_t model;
    int      rank, points;
    int      pcf388;                   // PCF_DisableFatallyWoundedBehaviour
};
static Ent g_ents[8];
static int g_entCount = 0;

static Ent* FindEnt(int h)
{
    if (h == 0)
        return nullptr;
    for (int i = 0; i < g_entCount; i++)
        if (g_ents[i].handle == h)
            return &g_ents[i];
    return nullptr;
}

enum KneelState { K_OFF = 0, K_DOWN, K_KNEEL, K_EXIT };

struct Sim
{
    int   gameMs;
    float px, py, pz;
    int   playerDead;
    int   kneel, kneelFrames;          // KneelState and frames left in it
    int   interactionTarget;
    int   cameraItem, simpleCrafting, journal, itemInteraction;
    int   wanted, mission, online, paused, inVehicle;
    int   playerMenu, canLaunch;
    int   suRunning, suIgnore;         // short_update running / not reacting to anything
    int   destinations;                // places the game would list
    int   refuseStart;                 // short_update func_512 refuses (the trip never starts)
    int   simFadesIn;                  // the game fades in by itself after the trip
    int   menuOpen, menuActive, menuResult, menuOpens, menuShows;
    float timeScale;
    int   travelFrames, trips, mountedOn, takenHorse, secondHorse;
    float destX, destY, destZ;
    int   music, musicStop;
    int   fade, fadeEnd;               // 0 in, 1 fading out, 2 out, 3 fading in
    int   modFadeOuts, modFadeIns;
    int   clearTasks, kneelExitCmds, lastHashCmd;
    int   pendingPress;
    uint32_t pressedNow;
    int   disabledX, disabledY, disabledJump, disabledAll, disabledOther;
    int   groupDrawn;
    float moveInput;                   // movement stick / W-S held
    int   moveFrames;                  // frames of movement the kneel script has read
    int   ctxFrontend, ctxFrontendLast;// the plugin set the FrontendMenu control context this frame / last frame
    int   contextSets;
    int   pauseEnabled;
    int   noScenarioTask;              // PED_HAS_USE_SCENARIO_TASK false even while kneeling
    int   stuckTravel;                 // the trip never finishes loading (state 5)
    int   modMusicStops;               // the plugin stopped the fast travel music
    int   downFrames;                  // how long getting down on one knee takes
    int   eventFrame;                  // when the kneel animation's event fires (frames into getting down)
    int   downElapsed;                 // frames since he started getting down
    int   dcState;                     // dynamic_craft_scenario's state (func_6): 0 start, 1 waiting, 2 prompts up, 3 down for a menu
    int   setCamp;                     // Set Up Camp pressed: 1 just now, 2 done (its prompts gone, the group still drawn)
    int   campfireHud;                 // a campfire script shows its own prompts (bits 2 and 3)
    int   hudBroken;                   // the kneel's HUD bit isn't where the plugin looks (another game build)
    int   notActive;                   // kneeling in the scenario's base, but IS_PED_ACTIVE_IN_SCENARIO(.., 0) false
    int   campLoaded, campLoadIn;      // the CAMP text block: loaded / frames until a request finishes
    int   campLoadFrames;              // how long loading it takes
    int   campNeverLoads;
    int   campRequests, campDeletes;   // asked for / unloaded through the natives (not by the game's own scripts here)
    int   scripts;                     // camp scripts running, a bit each (kCampScripts)
    int   french;                      // the game's language
    int   labelUnreadable;             // _GET_LABEL_TEXT_2 can't read CAMP_FT_PROMPT
    int   blankFrames, blankTotal;     // frames the kneel menu showed our prompt without any text
    // Set Up Camp
    int   campHere;                    // the game allows a camp here: the kneel menu has its own Set Up Camp
    int   listBroken;                  // the kneel's prompts aren't in the shared prompt list (another layout)
    int   kneelPrompts[3];             // the kneel menu's own prompts (Craft, Set Up Camp, Leave), 0 = none
    int   kitCamp, kitSimple, backupInv;
    int   interior, combat, recentCrime, minigame, animalInteraction;
    int   campScriptExists, campScriptNoLoad, campScriptLoadIn, campScriptLoaded;
    int   scriptRequests, noLongerNeeded, badScriptCalls, badInventory;
    int   campStarts, campArgc, campStack;
    uint64_t campArgs[10];
    int   campRunning, campFrames, campFails;   // the fake player_camp
    int   threadIter;                  // SCRIPT_THREAD_ITERATOR position
    int   disabledB, disabledA;
    int   gameCampStarts;              // camps the kneel menu's own Set Up Camp started
    int   campKneelExits;              // times player_camp got Arthur up from the kneel
    int   campStateAt;                 // the fake player_camp's frame count when it changed state
    int   evelyn;                      // RCM_EVELYN_MILLER4 running
    int   onVehicle, ragdoll, inBoat, ridingTrain, jacking;
    int   kitCampOff, kitSimpleOff;    // a script has the camp kit disabled
    int   disabledAskP2;               // the third argument of the last "is the kit disabled" question
    int   campScriptLoadFrames;        // how long REQUEST_SCRIPT("player_camp") takes
    int   dupFrames;                   // frames the drawn kneel menu had two Set Up Camps
    int   campPromptsMax;              // most Set Up Camp prompts of the plugin's at once
};
static Sim g_sim;

// The camp scripts that use the CAMP text, and whether they unload it when they end.
struct CampScript { const char* name; int unloads; };
static const CampScript kCampScripts[] =
{
    { "player_camp", 1 }, { "interactive_campfire", 1 }, { "property_use_core", 1 },
    { "campfire_always", 0 },   // only now and then (its func_7 check)
    { "sleeping_scenario", 0 },
};

// ---------------------------------------------------------------------------
// Prompts
// ---------------------------------------------------------------------------
struct Prompt
{
    int      id, valid, registering;
    uint32_t control;
    char     text[128];
    int      priority, transport, standard, release, attr18, group, tab, enabled, visible;
    int      justReleased, justPressed;
    int      game;                     // made by the game's own scripts (here the kneel), not by the plugin
};
static Prompt g_prompts[64];
static int    g_promptNext = 1000;
static int    g_promptsMade = 0, g_promptsDeleted = 0;
static Prompt g_lastPrompt;

static Prompt* FindPrompt(int id)
{
    for (int i = 0; i < 64; i++)
        if (g_prompts[i].id == id && id != 0)
            return &g_prompts[i];
    return nullptr;
}

static int LivePrompts()   // the plugin's
{
    int n = 0;
    for (int i = 0; i < 64; i++)
        if (g_prompts[i].id && g_prompts[i].valid && !g_prompts[i].game)
            n++;
    return n;
}

// The plugin's prompts: Fast Travel is a tap on release, its Set Up Camp a tap on press.
static bool IsCampPrompt(const Prompt& p) { return p.id && p.valid && !p.game && p.standard && !p.release; }

static int GamePrompts()
{
    int n = 0;
    for (int i = 0; i < 64; i++)
        if (g_prompts[i].id && g_prompts[i].valid && g_prompts[i].game)
            n++;
    return n;
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
static char g_varStrings[16][300];
static int  g_varNext = 0;
static char g_helps[64][300];
static int  g_helpCount = 0;
static char g_texts[64][300];
static int  g_textCount = 0;

static const char* MakeVarString(const char* s)
{
    char* b = g_varStrings[g_varNext++ % 16];
    snprintf(b, 300, "~LIT~%s", s ? s : "");
    return b;
}

// The labels of the CAMP text block the plugin uses, in English and French.
struct Label { const char* name; const char* en; const char* fr; };
static const Label kCampLabels[] =
{
    { "CAMP_FT_PROMPT", "Fast Travel", "Voyage rapide" },
    { "DEAD_HORSE_FT",  "You can't fast travel as your horse is dead.", "Votre cheval est mort." },
    { "FAR_HORSE_FT",   "Your horse is too far away to fast travel.", "Votre cheval est trop loin." },
    { "NO_HORSE_FT",    "You can't fast travel without your horse.", "Il vous faut votre cheval." },
    { "NO_REGION_FT",   "Discover more regions to use fast travel. Regions in lockdown are unavailable.", "Decouvrez d'autres regions." },
};

// Labels of the game's always-loaded text the kneel menu uses.
static const Label kGlobalLabels[] =
{
    { "SET_CAMP", "Set Up Camp", "Installer le camp" },
    { "CAMP_CRAFT", "Craft", "Fabriquer" },
    { "LEAVE", "Leave", "Partir" },
};

static bool IsCampBlock(const char* s) { return s && strcmp(s, "CAMP") == 0; }

// The label's text, if the label is loaded right now.
static const char* LabelText(const char* name)
{
    if (!name)
        return nullptr;
    for (const Label& l : kGlobalLabels)
        if (strcmp(l.name, name) == 0)
            return g_sim.french ? l.fr : l.en;
    if (!g_sim.campLoaded)
        return nullptr;
    for (const Label& l : kCampLabels)
        if (strcmp(l.name, name) == 0)
            return g_sim.french ? l.fr : l.en;
    return nullptr;
}

// What a prompt shows: its plain text, or its label's text (nothing if the label isn't loaded).
static const char* ShownText(const char* text)
{
    if (strncmp(text, "~LIT~", 5) == 0)
        return text + 5;
    const char* t = LabelText(text);
    return t ? t : "";
}

// ---------------------------------------------------------------------------
// Natives
// ---------------------------------------------------------------------------
static uint64_t g_hash = 0;
static uint64_t g_args[32];
static int      g_argc = 0;
static uint64_t g_result[4];
static int      g_unknownHashes = 0;
static uint64_t g_lastUnknown = 0;
static int      g_badArgCount = 0;
static uint64_t g_lastBadArg = 0;
static int      g_badVarString = 0;
static int      g_badHelp = 0;
static int      g_globalWritesOutside = 0;

#define EXPORT_AS(sym) __asm__(sym)

extern "C" {
__declspec(dllexport) void FS_scriptRegister(HMODULE, ScriptMainFn) EXPORT_AS("?scriptRegister@@YAXPEAUHINSTANCE__@@P6AXXZ@Z");
__declspec(dllexport) void FS_scriptRegisterAdditionalThread(HMODULE, ScriptMainFn) EXPORT_AS("?scriptRegisterAdditionalThread@@YAXPEAUHINSTANCE__@@P6AXXZ@Z");
__declspec(dllexport) void FS_scriptUnregister(HMODULE) EXPORT_AS("?scriptUnregister@@YAXPEAUHINSTANCE__@@@Z");
__declspec(dllexport) void FS_scriptUnregisterOld(ScriptMainFn) EXPORT_AS("?scriptUnregister@@YAXP6AXXZ@Z");
__declspec(dllexport) void FS_scriptWait(DWORD) EXPORT_AS("?scriptWait@@YAXK@Z");
__declspec(dllexport) void FS_nativeInit(uint64_t) EXPORT_AS("?nativeInit@@YAX_K@Z");
__declspec(dllexport) void FS_nativePush64(uint64_t) EXPORT_AS("?nativePush64@@YAX_K@Z");
__declspec(dllexport) uint64_t* FS_nativeCall() EXPORT_AS("?nativeCall@@YAPEA_KXZ");
__declspec(dllexport) uint64_t* FS_getGlobalPtr(int) EXPORT_AS("?getGlobalPtr@@YAPEA_KH@Z");
__declspec(dllexport) int FS_getGameVersion() EXPORT_AS("?getGameVersion@@YA?AW4eGameVersion@@XZ");
}

void FS_scriptRegister(HMODULE, ScriptMainFn f) { g_scriptMain = f; }
void FS_scriptRegisterAdditionalThread(HMODULE, ScriptMainFn) {}
void FS_scriptUnregister(HMODULE) {}
void FS_scriptUnregisterOld(ScriptMainFn) {}
void FS_scriptWait(DWORD) { SwitchToFiber(g_hostFiber); }
void FS_nativeInit(uint64_t hash) { g_hash = hash; g_argc = 0; }
void FS_nativePush64(uint64_t v) { if (g_argc < 32) g_args[g_argc++] = v; }
int  FS_getGameVersion() { return 99; }
uint64_t* FS_getGlobalPtr(int idx)
{
    if (idx < 0 || idx >= kGlobalCount)
        return nullptr;
    return &g_globals[idx];
}

static void Want(int n) { if (g_argc != n) { g_badArgCount++; g_lastBadArg = g_hash; } }
static float F(int i) { float f; memcpy(&f, &g_args[i], 4); return f; }
static int   I(int i) { return (int)(uint32_t)g_args[i]; }

static uint32_t H(const char* s) { return Joaat(s); }

// The kneel: the animation event has fired (he's on his way down), he's all the way down.
static bool KneelEventFired() { return g_sim.kneel == K_KNEEL || (g_sim.kneel == K_DOWN && g_sim.downElapsed >= g_sim.eventFrame); }
static bool InScenarioBase()  { return g_sim.kneel == K_KNEEL; }
static bool HasScenarioTask() { return g_sim.kneel != K_OFF && !g_sim.noScenarioTask; }
// IS_PED_ACTIVE_IN_SCENARIO(player, p1): p1 = 0 once all the way down; p1 = 1 also on the way down
// (dynamic_craft_scenario ends if it's false once its prompts are up).
static bool ActiveInScenario(int p1)
{
    if (p1 == 0)
        return g_sim.kneel == K_KNEEL && !g_sim.notActive;
    return KneelEventFired();
}

// short_update's func_502(1): what keeps its menu open.
static bool ShortUpdateScenarioOk()
{
    return !g_sim.playerDead && HasScenarioTask() && g_sim.kneel != K_EXIT && (InScenarioBase() || ActiveInScenario(0));
}

static void CampRequest()
{
    if (!g_sim.campLoaded && !g_sim.campLoadIn && !g_sim.campNeverLoads)
        g_sim.campLoadIn = g_sim.campLoadFrames > 0 ? g_sim.campLoadFrames : 1;
}

static void CampDelete()
{
    g_sim.campLoaded = 0;
    g_sim.campLoadIn = 0;
}

static void SimFadeOut(int ms)
{
    if (ms <= 0) { g_sim.fade = 2; return; }
    g_sim.fade = 1;
    g_sim.fadeEnd = g_sim.gameMs + ms;
}

static void SimFadeIn(int ms)
{
    if (ms <= 0) { g_sim.fade = 0; return; }
    g_sim.fade = 3;
    g_sim.fadeEnd = g_sim.gameMs + ms;
}

static bool HorseDeadOrWrithing(int h) // IS_ENTITY_DEAD || IS_PED_IN_WRITHE, a missing ped counts as dead
{
    Ent* e = FindEnt(h);
    if (!e || !e->exists)
        return true;
    return e->dead || e->writhe;
}

static float SimWhistleRange(int slot)
{
    // as the plugin's model of func_511, with the sim's attribute tables below
    int base = GL_HORSE_SAVES + 1 + slot * 436;
    int level = GI(base + 372);
    float pts; memcpy(&pts, &g_globals[base + 373], 4);
    static const int need[5] = { 0, 100, 300, 600, 1000 };
    float rmin = 50.0f + 25.0f * level, rmax = 75.0f + 25.0f * level;
    if (level >= 4) return rmin;
    float t = (pts - need[level]) / (float)(need[level + 1] - need[level]);
    return (1 - t) * rmin + t * rmax;
}

static bool HorseInRange(int h, int slot)
{
    Ent* e = FindEnt(h);
    if (!e || !e->exists || e->dead || e->writhe)
        return false;
    float dx = e->x - g_sim.px, dy = e->y - g_sim.py;
    return sqrtf(dx * dx + dy * dy) <= SimWhistleRange(slot);
}

// The game's script threads, as SCRIPT_THREAD_ITERATOR_* hands them out.
static const int kKneelThread = 7001, kCampThread = 7003;
static int ThreadList(int* ids, uint32_t* hashes)
{
    int n = 0;
    auto add = [&](int id, const char* name) { ids[n] = id; hashes[n] = Joaat(name); n++; };
    add(7100, "main");
    if (g_sim.kneel != K_OFF) add(kKneelThread, "dynamic_craft_scenario");
    add(7101, "long_update");
    if (g_sim.suRunning) add(7002, "short_update");
    if (g_sim.campRunning || (g_sim.scripts & 1)) add(kCampThread, "player_camp");
    for (int i = 1; i < (int)(sizeof kCampScripts / sizeof kCampScripts[0]); i++)
        if ((g_sim.scripts >> i) & 1) add(7010 + i, kCampScripts[i].name);
    if (g_sim.simpleCrafting) add(7020, "simple_crafting");
    if (g_sim.evelyn) add(7030, "rcm_evelyn_miller4");
    return n;
}

static bool StrIs(uint64_t arg, const char* want)
{
    const char* s = (const char*)(uintptr_t)arg;
    return s && strcmp(s, want) == 0;
}

// The kneel menu's own prompts (dynamic_craft_scenario func_19 -> func_53),
// in the game's shared prompt list (Global_1945188, slots of 18).
static void KneelUnlistPrompts();
static void KneelRemovePrompts()
{
    for (int i = 0; i < 3; i++)
    {
        if (Prompt* p = FindPrompt(g_sim.kneelPrompts[i]))
            memset(p, 0, sizeof(Prompt));
        g_sim.kneelPrompts[i] = 0;
    }
    KneelUnlistPrompts();
}

// Their slots in the shared list (func_53): f_1 bit 2 in use (and 128), f_3 the
// prompt, f_4 its control, f_16 the thread. The game's helper never uses slot 0.
static void KneelListPrompts()
{
    for (int i = 0; i < 3; i++)
    {
        Prompt* p = FindPrompt(g_sim.kneelPrompts[i]);
        if (!p)
            continue;
        int base = GL_PROMPT_LIST + 1 + (i + 1) * 18;
        SI(base + 0, 1);
        SI(base + 1, 2 | 128);
        SI(base + 3, p->id);
        SI(base + 4, (int)p->control);
        SI(base + 16, kKneelThread);
    }
}

static void KneelUnlistPrompts()
{
    for (int i = 0; i < 3; i++)
    {
        int base = GL_PROMPT_LIST + 1 + (i + 1) * 18;
        SI(base + 1, 0);
        SI(base + 4, 0);
        SI(base + 16, 0);
    }
}

static void KneelAddPrompts()
{
    KneelRemovePrompts();
    static const char* const labels[3] = { "CAMP_CRAFT", "SET_CAMP", "LEAVE" };
    const uint32_t controls[3] = { Joaat("INPUT_CONTEXT_X"), Joaat("INPUT_CONTEXT_Y"), Joaat("INPUT_CONTEXT_B") };
    for (int i = 0; i < 3; i++)
    {
        if (i == 1 && (!g_sim.campHere || !(g_sim.kitCamp || g_sim.kitSimple)))
            continue;                      // func_21 said no, or no camp kit: no Set Up Camp
        int slot = -1;
        for (int k = 0; k < 64; k++) if (!g_prompts[k].id) { slot = k; break; }
        if (slot < 0)
            return;
        Prompt& p = g_prompts[slot];
        memset(&p, 0, sizeof p);
        p.id = g_promptNext++;
        p.valid = p.enabled = p.visible = p.standard = 1;
        p.game = 1;
        p.control = controls[i];
        p.group = 342152817;
        p.priority = 1;
        snprintf(p.text, sizeof p.text, "%s", labels[i]);
        g_sim.kneelPrompts[i] = p.id;
    }
    if (!g_sim.listBroken)
        KneelListPrompts();
}

uint64_t* FS_nativeCall()
{
    memset(g_result, 0, sizeof g_result);
    switch (g_hash)
    {
    case 0x096275889B8E0EE0ULL: Want(0); g_result[0] = kPlayerPed; break;                 // PLAYER_PED_ID
    case 0x217E9DC48139933DULL: Want(0); g_result[0] = 0; break;                          // PLAYER_ID
    case 0x7D5B1F88E7504BBAULL: {                                                         // IS_ENTITY_DEAD
        Want(1);
        int e = I(0);
        if (e == kPlayerPed) g_result[0] = g_sim.playerDead;
        else { Ent* x = FindEnt(e); g_result[0] = (!x || !x->exists) ? 1 : x->dead; }
        break;
    }
    case 0xD42BD6EB2E0F1677ULL: {                                                         // DOES_ENTITY_EXIST
        Want(1);
        int e = I(0);
        Ent* x = FindEnt(e);
        g_result[0] = e == kPlayerPed || (x && x->exists);
        break;
    }
    case 0xA86D5F069399F44DULL: {                                                         // GET_ENTITY_COORDS
        Want(3);
        float v[3] = { 0, 0, 0 };
        int e = I(0);
        if (e == kPlayerPed) { v[0] = g_sim.px; v[1] = g_sim.py; v[2] = g_sim.pz; }
        else if (Ent* x = FindEnt(e)) { v[0] = x->x; v[1] = x->y; v[2] = x->z; }
        for (int i = 0; i < 3; i++) { uint64_t raw = 0; memcpy(&raw, &v[i], 4); g_result[i] = raw; }
        break;
    }
    case 0xDA76A9F39210D365ULL: { Want(1); Ent* x = FindEnt(I(0)); g_result[0] = x ? x->model : 0; break; } // GET_ENTITY_MODEL
    case 0xCF8176912DDA4EA5ULL: { Want(1); int e = I(0); g_result[0] = e == kPlayerPed || FindEnt(e) != nullptr; break; } // IS_ENTITY_A_PED
    case 0xB980061DA992779DULL: { Want(1); int e = I(0); Ent* x = FindEnt(e); g_result[0] = e == kPlayerPed ? 1 : (x ? x->human : 0); break; } // IS_PED_HUMAN
    case 0x84A2DD9AC37C35C1ULL: { Want(1); Ent* x = FindEnt(I(0)); g_result[0] = x ? x->injured : 0; break; } // IS_PED_INJURED
    case 0xDEB6D52126E7D640ULL: { Want(1); Ent* x = FindEnt(I(0)); g_result[0] = x ? x->writhe : 0; break; }  // IS_PED_IN_WRITHE
    case 0x392C8D8E07B70EFCULL: Want(1); g_result[0] = (uint32_t)g_args[0] == kHorseModel; break;           // IS_MODEL_VALID
    case 0x997ABD671D25CA0BULL: Want(2); g_result[0] = g_sim.inVehicle; break;                               // IS_PED_IN_ANY_VEHICLE
    case 0x6F972C1AB75A1ED0ULL: Want(1); g_result[0] = 0; break;                                            // IS_PED_IN_ANY_TRAIN
    case 0x460BC76A0E10655EULL: Want(1); g_result[0] = 0; break;                                            // IS_PED_ON_MOUNT
    case 0x8E34C953364A76DDULL: {                                                         // GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH
        Want(1);
        uint32_t h = (uint32_t)g_args[0];
        int n = 0;
        if (h == H("dynamic_craft_scenario")) n = g_sim.kneel != K_OFF;
        else if (h == H("short_update")) n = g_sim.suRunning;
        else if (h == H("CAMERA_ITEM")) n = g_sim.cameraItem;
        else if (h == H("SIMPLE_CRAFTING")) n = g_sim.simpleCrafting;
        else if (h == H("DOC_PLAYER_JOURNAL")) n = g_sim.journal;
        else if (h == H("RCM_EVELYN_MILLER4")) n = g_sim.evelyn;
        else
        {
            for (int i = 0; i < (int)(sizeof kCampScripts / sizeof kCampScripts[0]); i++)
                if (h == H(kCampScripts[i].name))
                    n = (g_sim.scripts >> i) & 1;
            if (h == H("player_camp") && g_sim.campRunning)
                n = 1;
        }
        g_result[0] = (uint32_t)n;
        break;
    }
    case 0xAA135F9482C82CC3ULL: Want(2); g_result[0] = I(0) == kPlayerPed && ActiveInScenario(I(1)); break;  // IS_PED_ACTIVE_IN_SCENARIO
    case 0x0C3CB2E600C8977DULL: Want(2); g_result[0] = I(0) == kPlayerPed && g_sim.kneel == K_EXIT; break; // IS_PED_EXITING_SCENARIO
    case 0xEC7E480FF8BD0BEDULL: Want(1); g_result[0] = g_sim.itemInteraction; break;                         // IS_PED_RUNNING_TASK_ITEM_INTERACTION
    case 0x295E3CCEC879CCD7ULL: Want(1); g_result[0] = I(0) == kPlayerPed && HasScenarioTask(); break;      // PED_HAS_USE_SCENARIO_TASK
    case 0x02EBBB3989B7E695ULL: Want(1); g_result[0] = I(0) == kPlayerPed && InScenarioBase(); break;        // _PED_IS_IN_SCENARIO_BASE
    case 0x3EE1F7A8C32F24E1ULL: {                                                         // GET_PLAYER_INTERACTION_TARGET_ENTITY
        Want(4);
        int64_t* out = (int64_t*)(uintptr_t)g_args[1];
        if (out) *out = g_sim.interactionTarget;
        g_result[0] = g_sim.interactionTarget != 0;
        break;
    }

    // ---- script threads and launching ----
    case 0x39382EB8DCD8684DULL: Want(0); g_sim.threadIter = 0; break;                        // SCRIPT_THREAD_ITERATOR_RESET
    case 0x3CE3FB167E837D7CULL: {                                                         // SCRIPT_THREAD_ITERATOR_GET_NEXT_THREAD_ID
        Want(0);
        int ids[32]; uint32_t hs[32];
        int n = ThreadList(ids, hs);
        g_result[0] = g_sim.threadIter < n ? (uint32_t)ids[g_sim.threadIter++] : 0;
        break;
    }
    case 0x724CB89D35B283D0ULL: {                                                         // _GET_HASH_OF_THREAD
        Want(1);
        int ids[32]; uint32_t hs[32];
        int n = ThreadList(ids, hs);
        for (int i = 0; i < n; i++)
            if (ids[i] == I(0))
                g_result[0] = hs[i];
        break;
    }
    case 0x552B171E3F69E5AEULL: Want(1); g_result[0] = StrIs(g_args[0], "player_camp") && g_sim.campScriptExists; break; // DOES_SCRIPT_EXIST
    case 0x46ED607DDD40D7FEULL:                                                           // REQUEST_SCRIPT
        Want(1);
        if (!StrIs(g_args[0], "player_camp")) { g_sim.badScriptCalls++; break; }
        g_sim.scriptRequests++;
        if (!g_sim.campScriptLoaded && !g_sim.campScriptLoadIn && !g_sim.campScriptNoLoad)
            g_sim.campScriptLoadIn = g_sim.campScriptLoadFrames > 0 ? g_sim.campScriptLoadFrames : 1;
        break;
    case 0xE97BD36574F8B0A6ULL: Want(1); g_result[0] = StrIs(g_args[0], "player_camp") && g_sim.campScriptLoaded; break; // HAS_SCRIPT_LOADED
    case 0x0086D3067E1CFD1CULL:                                                           // SET_SCRIPT_AS_NO_LONGER_NEEDED
        Want(1);
        if (StrIs(g_args[0], "player_camp")) { g_sim.noLongerNeeded++; g_sim.campScriptLoaded = 0; g_sim.campScriptLoadIn = 0; }
        else g_sim.badScriptCalls++;
        break;
    case 0xB8BA7F44DF1575E1ULL: {                                                         // START_NEW_SCRIPT_WITH_ARGS
        Want(4);
        if (!StrIs(g_args[0], "player_camp") || !g_sim.campScriptLoaded || I(2) != 10 || !g_args[1])
        {
            g_sim.badScriptCalls++;
            break;
        }
        uint64_t* a = (uint64_t*)(uintptr_t)g_args[1];
        for (int i = 0; i < 10; i++)
            g_sim.campArgs[i] = a[i];
        g_sim.campStarts++;
        g_sim.campArgc = I(2);
        g_sim.campStack = I(3);
        g_sim.campRunning = 1;
        g_sim.campFrames = 0;
        SI(GL_CAMP_STATE, 0);
        g_result[0] = (uint32_t)kCampThread;
        break;
    }

    // ---- the camp kit ----
    case 0x6D5D51B188333FD1ULL: Want(2); g_result[0] = (uint32_t)g_args[0] == H("KIT_CAMP") || (uint32_t)g_args[0] == H("KIT_CAMP_SIMPLE"); break; // _ITEMDATABASE_IS_KEY_VALID
    case 0x7C7E4AB748EA3B07ULL: Want(0); g_result[0] = g_sim.backupInv; break;               // _INVENTORY_IS_USING_BACKUP_INVENTORY
    case 0xE787F05DFC977BDEULL: {                                                         // _INVENTORY_GET_INVENTORY_ITEM_COUNT_WITH_ITEMID
        Want(3);
        uint32_t item = (uint32_t)g_args[1];
        if (I(0) != (g_sim.backupInv ? 5 : 1)) { g_sim.badInventory++; break; }
        g_result[0] = (uint32_t)(item == H("KIT_CAMP") ? g_sim.kitCamp : item == H("KIT_CAMP_SIMPLE") ? g_sim.kitSimple : 0);
        break;
    }

    case 0x3D10D7179D7034AFULL: {                                                         // _INVENTORY_IS_INVENTORY_ITEM_EQUIPPED (= is the item disabled)
        Want(3);
        uint32_t item = (uint32_t)g_args[1];
        if (I(0) != (g_sim.backupInv ? 5 : 1)) { g_sim.badInventory++; break; }
        g_sim.disabledAskP2 = I(2);
        g_result[0] = (uint32_t)(item == H("KIT_CAMP") ? g_sim.kitCampOff : item == H("KIT_CAMP_SIMPLE") ? g_sim.kitSimpleOff : 0);
        break;
    }

    // ---- the game's other camping rules ----
    case 0x67722AEB798E5FABULL: Want(2); g_result[0] = I(0) == kPlayerPed && g_sim.onVehicle; break;   // IS_PED_ON_VEHICLE
    case 0x47E4E977581C5B55ULL: Want(1); g_result[0] = I(0) == kPlayerPed && g_sim.ragdoll; break;     // IS_PED_RAGDOLL
    case 0x2E0E1C2B4F6CB339ULL: Want(1); g_result[0] = I(0) == kPlayerPed && g_sim.inBoat; break;      // IS_PED_IN_ANY_BOAT
    case 0x2FB0ACADA6A238DDULL: Want(1); g_result[0] = I(0) == 0 && g_sim.ridingTrain; break;          // IS_PLAYER_RIDING_TRAIN (player 0)
    case 0x4AE4FF911DFB61DAULL: Want(1); g_result[0] = I(0) == kPlayerPed && g_sim.jacking; break;     // IS_PED_JACKING
    case 0x4200F14D6F840A9AULL: Want(0); g_result[0] = g_sim.interior; break;                 // IS_INTERIOR_SCENE
    case 0x4859F1FC66A6278EULL: Want(2); g_result[0] = I(0) == kPlayerPed && g_sim.combat; break; // IS_PED_IN_COMBAT
    case 0x26934083D3F2579CULL: Want(1); g_result[0] = g_sim.recentCrime; break;              // LAW: recently committed a crime
    case 0xF4D8BCD052E7EA1BULL: Want(0); g_result[0] = g_sim.minigame; break;                 // IS_MINIGAME_IN_PROGRESS
    case 0x7FC84E85D98F063DULL: Want(1); g_result[0] = g_sim.animalInteraction; break;        // _IS_ANIMAL_INTERACTION_RUNNING

    // ---- prompts ----
    case 0x2787CC611D3FACC5ULL: { Want(1); Prompt* p = FindPrompt(I(0)); g_result[0] = p && p->justPressed; break; } // _UI_PROMPT_IS_JUST_PRESSED
    case 0x04F97DE45A519419ULL: {                                                         // _UI_PROMPT_REGISTER_BEGIN
        Want(0);
        int slot = -1;
        for (int i = 0; i < 64; i++) if (!g_prompts[i].id) { slot = i; break; }
        if (slot >= 0)
        {
            memset(&g_prompts[slot], 0, sizeof(Prompt));
            g_prompts[slot].id = g_promptNext++;
            g_prompts[slot].registering = 1;
            g_result[0] = (uint32_t)g_prompts[slot].id;
        }
        break;
    }
    case 0xB5352B7494A08258ULL: { Want(2); if (Prompt* p = FindPrompt(I(0))) p->control = (uint32_t)g_args[1]; break; } // SET_CONTROL_ACTION
    case 0x5DD02A8318420DD7ULL: { Want(2); if (Prompt* p = FindPrompt(I(0))) snprintf(p->text, sizeof p->text, "%s", (const char*)(uintptr_t)g_args[1]); break; } // SET_TEXT
    case 0xCA24F528D0D16289ULL: { Want(2); if (Prompt* p = FindPrompt(I(0))) p->priority = I(1); break; }      // SET_PRIORITY
    case 0x876E4A35C73A6655ULL: { Want(2); if (Prompt* p = FindPrompt(I(0))) p->transport = I(1); break; }     // SET_TRANSPORT_MODE
    case 0x560E76D5E2E1803FULL: { Want(3); if (Prompt* p = FindPrompt(I(0))) if (I(1) == 18) p->attr18 = I(2); break; } // SET_ATTRIBUTE
    case 0xCC6656799977741BULL: { Want(2); if (Prompt* p = FindPrompt(I(0))) { p->standard = 1; p->release = I(1); } break; } // SET_STANDARD_MODE
    case 0xF7AA2696A22AD8B9ULL: {                                                         // _UI_PROMPT_REGISTER_END
        Want(1);
        if (Prompt* p = FindPrompt(I(0))) { p->registering = 0; p->valid = 1; p->enabled = 1; p->visible = 1; g_promptsMade++; }
        break;
    }
    case 0x2F11D3A254169EA4ULL: { Want(3); if (Prompt* p = FindPrompt(I(0))) { p->group = I(1); p->tab = I(2); } break; } // SET_GROUP
    case 0x8A0FB4D03A630D21ULL: { Want(2); if (Prompt* p = FindPrompt(I(0))) p->enabled = I(1); break; }      // SET_ENABLED
    case 0x71215ACCFDE075EEULL: { Want(2); if (Prompt* p = FindPrompt(I(0))) p->visible = I(1); break; }      // SET_VISIBLE
    case 0x635CC82FA297A827ULL: { Want(1); Prompt* p = FindPrompt(I(0)); g_result[0] = p && p->justReleased; break; } // IS_JUST_RELEASED
    case 0x347469FBDD1589A9ULL: { Want(1); Prompt* p = FindPrompt(I(0)); g_result[0] = p && p->valid; break; } // IS_VALID
    case 0x00EDE88D4D13CF59ULL: {                                                         // _UI_PROMPT_DELETE
        Want(1);
        if (Prompt* p = FindPrompt(I(0))) { g_lastPrompt = *p; memset(p, 0, sizeof(Prompt)); g_promptsDeleted++; }
        break;
    }

    // ---- UI apps ----
    case 0x25B7A0206BDFAC76ULL: {                                                         // IS_UIAPP_ACTIVE_BY_HASH
        Want(1);
        uint32_t h = (uint32_t)g_args[0];
        g_result[0] = h == H("FAST_TRAVEL_MENU") ? g_sim.menuActive : h == H("PLAYER_MENU") ? g_sim.playerMenu : 0;
        break;
    }
    case 0xE555EC27D65EDE80ULL: Want(1); g_result[0] = (uint32_t)g_args[0] == H("FAST_TRAVEL_MENU") ? g_sim.canLaunch : 1; break; // CAN_LAUNCH_UIAPP_BY_HASH

    // ---- text ----
    case 0xFA925AC00EB830B9ULL:                                                           // VAR_STRING
        if (g_argc != 3 || I(0) != 10 || strcmp((const char*)(uintptr_t)g_args[1], "LITERAL_STRING") != 0)
            g_badVarString++;
        g_result[0] = (uint64_t)(uintptr_t)MakeVarString((const char*)(uintptr_t)g_args[2]);
        break;
    case 0x049D5C615BD38BADULL: {                                                         // _UI_FEED_POST_HELP_TEXT
        Want(3);
        int64_t* data = (int64_t*)(uintptr_t)g_args[0];
        int64_t* text = (int64_t*)(uintptr_t)g_args[1];
        if (!data || !text || data[0] <= 0 || !text[1])
            g_badHelp++;
        else
            snprintf(g_helps[g_helpCount++ % 64], 300, "%s", (const char*)(uintptr_t)text[1]);
        break;
    }
    case 0xD0976CC34002DB57ULL: Want(1); g_result[0] = IsCampBlock((const char*)(uintptr_t)g_args[0]) && g_sim.campLoaded; break; // TEXT_BLOCK_IS_LOADED
    case 0xF66090013DE648D5ULL:                                                           // TEXT_BLOCK_REQUEST
        Want(1);
        if (IsCampBlock((const char*)(uintptr_t)g_args[0])) { g_sim.campRequests++; CampRequest(); }
        break;
    case 0xAA03F130A637D923ULL:                                                           // _TEXT_BLOCK_DELETE
        Want(1);
        if (IsCampBlock((const char*)(uintptr_t)g_args[0])) { g_sim.campDeletes++; CampDelete(); }
        break;
    case 0x2C729F2B94CEA911ULL: Want(1); g_result[0] = IsCampBlock((const char*)(uintptr_t)g_args[0]); break; // _DOES_TEXT_BLOCK_EXIST
    case 0x73C258C68D6F55B6ULL: Want(1); g_result[0] = LabelText((const char*)(uintptr_t)g_args[0]) != nullptr; break; // DOES_TEXT_LABEL_EXIST
    case 0x3429670F9B9EF2D3ULL: {                                                         // _GET_LABEL_TEXT_2
        Want(1);
        const char* t = g_sim.labelUnreadable ? nullptr : LabelText((const char*)(uintptr_t)g_args[0]);
        g_result[0] = (uint64_t)(uintptr_t)(t ? t : "NULL");
        break;
    }
    case 0xA1253A3C870B6843ULL: Want(2); break;                                           // _BG_SET_TEXT_SCALE
    case 0x16FA5CE47F184F1EULL: Want(4); break;                                           // _BG_SET_TEXT_COLOR
    case 0x16794E044C9EFB58ULL:                                                           // _BG_DISPLAY_TEXT
        Want(3);
        snprintf(g_texts[g_textCount++ % 64], 300, "%s", (const char*)(uintptr_t)g_args[0]);
        break;

    // ---- game state ----
    case 0x4F67E8ECA7D3F667ULL: Want(0); g_result[0] = (uint32_t)g_sim.gameMs; break;    // GET_GAME_TIMER
    case 0xB15CD1CF58771DE1ULL: Want(0); g_result[0] = g_sim.mission; break;              // GET_MISSION_FLAG
    case 0xAD401C63158ACBAAULL: Want(1); g_result[0] = g_sim.wanted; break;               // IS_LAW_INCIDENT_ACTIVE
    case 0x535384D6067BA42EULL: Want(0); g_result[0] = g_sim.paused; break;               // IS_PAUSE_MENU_ACTIVE
    case 0x71D4BF5890659B0CULL: Want(0); g_result[0] = 0; break;                          // GET_IS_LOADING_SCREEN_ACTIVE
    case 0x9DE624D2FC4B603FULL: Want(0); g_result[0] = g_sim.online; break;               // NETWORK_IS_SESSION_STARTED
    case 0x10FAB35428CCC9D7ULL: Want(0); g_result[0] = 0; break;                          // NETWORK_IS_GAME_IN_PROGRESS

    // ---- controls ----
    case 0xFE99B66D079CF6BCULL: {                                                         // DISABLE_CONTROL_ACTION
        Want(3);
        uint32_t h = (uint32_t)g_args[1];
        if (h == H("INPUT_CONTEXT_X")) g_sim.disabledX = 1;
        else if (h == H("INPUT_CONTEXT_Y")) g_sim.disabledY = 1;
        else if (h == H("INPUT_CONTEXT_B")) g_sim.disabledB = 1;
        else if (h == H("INPUT_CONTEXT_A")) g_sim.disabledA = 1;
        else if (h == H("INPUT_JUMP")) g_sim.disabledJump = 1;
        else g_sim.disabledOther = 1;
        break;
    }
    case 0x5F4B6931816E599BULL: Want(1); g_sim.disabledAll = 1; g_sim.pauseEnabled = 0; break; // DISABLE_ALL_CONTROL_ACTIONS
    case 0x351220255D64C155ULL:                                                           // ENABLE_CONTROL_ACTION
        Want(3);
        if ((uint32_t)g_args[1] == H("INPUT_FRONTEND_PAUSE") || (uint32_t)g_args[1] == H("INPUT_FRONTEND_PAUSE_ALTERNATE"))
            g_sim.pauseEnabled++;
        break;
    case 0x11E65974A982637CULL: {                                                         // GET_DISABLED_CONTROL_NORMAL
        Want(2);
        uint32_t h = (uint32_t)g_args[1];
        float v = 0.0f;
        if ((h == H("INPUT_MOVE_UD")) && !g_sim.ctxFrontendLast)
            v = g_sim.moveInput;
        memcpy(&g_result[0], &v, 4);
        break;
    }
    case 0x2804658EB7D8A50BULL:                                                           // _SET_CONTROL_CONTEXT
        Want(2);
        g_sim.contextSets++;
        if ((uint32_t)g_args[1] == H("FrontendMenu")) g_sim.ctxFrontend = 1;
        break;
    case 0x305C8DCD79DA8B0FULL: Want(2); g_result[0] = g_sim.pressedNow != 0 && (uint32_t)g_args[1] == g_sim.pressedNow; break; // IS_DISABLED_CONTROL_JUST_RELEASED
    case 0xBC0884BC590951C7ULL: {                                                         // _IS_CONTROL_ACTION_VALID
        Want(2);
        uint32_t h = (uint32_t)g_args[0];
        g_result[0] = h == H("INPUT_CONTEXT_A") || h == H("INPUT_CONTEXT_X") || h == H("INPUT_CONTEXT_Y") ||
                      h == H("INPUT_CONTEXT_B") || h == H("INPUT_JUMP") || h == H("INPUT_CONTEXT_LT");
        break;
    }

    // ---- screen ----
    case 0x40C719A5E410B9E4ULL: Want(1); g_sim.modFadeOuts++; SimFadeOut(I(0)); break;   // DO_SCREEN_FADE_OUT
    case 0x6A053CF596F67DF7ULL: Want(1); g_sim.modFadeIns++; SimFadeIn(I(0)); break;     // DO_SCREEN_FADE_IN
    case 0xF5472C80DF2FF847ULL: Want(0); g_result[0] = g_sim.fade == 2; break;           // IS_SCREEN_FADED_OUT
    case 0x02F39BEFE7B88D00ULL: Want(0); g_result[0] = g_sim.fade == 1; break;           // IS_SCREEN_FADING_OUT
    case 0x0CECCC63FFA2EF24ULL: Want(0); g_result[0] = g_sim.fade == 3; break;           // IS_SCREEN_FADING_IN
    case 0x37F9A426FBCF4AF2ULL: Want(0); g_result[0] = g_sim.fade == 0; break;           // IS_SCREEN_FADED_IN

    // ---- the player ----
    case 0xD65FDC686A031C83ULL:                                                           // _GIVE_PED_HASH_COMMAND
        Want(3);
        g_sim.lastHashCmd = (int)(uint32_t)g_args[1];
        if ((uint32_t)g_args[1] == H("DYNAMIC_KNEEL_SLOW_EXIT") && I(0) == kPlayerPed)
        {
            g_sim.kneelExitCmds++;
            if (g_sim.kneel == K_KNEEL) { g_sim.kneel = K_EXIT; g_sim.kneelFrames = 40; }
        }
        break;
    case 0x1913FE4CBF41C463ULL: { Want(3); if (Ent* x = FindEnt(I(0))) if (I(1) == 388) x->pcf388 = I(2); break; } // SET_PED_CONFIG_FLAG
    case 0x706D57B0F50DA710ULL:                                                           // TRIGGER_MUSIC_EVENT
        Want(1);
        if (strcmp((const char*)(uintptr_t)g_args[0], "OpenWorldMusic_FastTravel_StopEvent") == 0)
            g_sim.modMusicStops++;
        g_result[0] = 1;
        break;
    case 0xAAA34F8A7CB32098ULL:                                                           // CLEAR_PED_TASKS_IMMEDIATELY
        Want(3);
        if (I(0) == kPlayerPed) { g_sim.clearTasks++; g_sim.kneel = K_OFF; }
        break;

    // ---- horse bonding ----
    case 0x7C059C55AD940CB4ULL: Want(2); g_result[0] = (uint32_t)g_args[0] == kHorseModel ? 4 : 0; break; // GET_DEFAULT_MAX_ATTRIBUTE_RANK
    case 0x94A7F191DB49A44DULL: {                                                         // GET_DEFAULT_ATTRIBUTE_POINTS_NEEDED_FOR_RANK
        Want(3);
        static const int need[5] = { 0, 100, 300, 600, 1000 };
        int r = I(2);
        g_result[0] = (uint32_t)(r >= 0 && r <= 4 ? need[r] : 0);
        break;
    }
    case 0xEB67D4E056C85A81ULL: { Want(1); float v = 50.0f + 25.0f * I(0); memcpy(&g_result[0], &v, 4); break; } // _GET_WHISTLE_RANGE_MIN_FOR_BONDING_LEVEL
    case 0x78D8C1D4EB80C588ULL: { Want(1); float v = 75.0f + 25.0f * I(0); memcpy(&g_result[0], &v, 4); break; } // _GET_WHISTLE_RANGE_MAX_FOR_BONDING_LEVEL
    case 0xA4C8E23E29040DE0ULL: { Want(2); Ent* x = FindEnt(I(0)); g_result[0] = (uint32_t)(x ? x->rank : 0); break; }   // GET_ATTRIBUTE_RANK
    case 0x219DA04BAA9CB065ULL: { Want(2); Ent* x = FindEnt(I(0)); g_result[0] = (uint32_t)(x ? x->points : 0); break; } // GET_ATTRIBUTE_POINTS

    default:
        g_unknownHashes++;
        g_lastUnknown = g_hash;
        break;
    }
    return g_result;
}

// ---------------------------------------------------------------------------
// One game frame: the game's scripts, before the plugin's script runs
// ---------------------------------------------------------------------------

static void MenuClose()
{
    g_sim.menuOpen = 0;
    g_sim.menuActive = 0;
    g_sim.timeScale = 1.0f;
}

// short_update's PLAYER CAMP FAST TRAVEL module (func_80), reduced to what the
// plugin interacts with.
static void ShortUpdateTick()
{
    if (!g_sim.suRunning || g_sim.suIgnore)
        return;
    int st = GI(GL_FT_STATE), fl = GI(GL_FT_FLAGS);
    int slot7 = GI(GL_ACTIVE_HORSE);
    int h7 = (slot7 >= 0 && slot7 < 7) ? GI(GL_HORSE_PEDS + 1 + slot7 * 45) : 0;
    int h1 = GI(GL_HORSE_PEDS + 1 + 45);
    switch (st)
    {
    case 0:
        if (fl & 4)
        {
            fl &= ~4;
            if (g_sim.destinations > 0)               // func_495: any place to list
            {
                g_sim.menuOpen = 1;                    // func_496 launches FAST_TRAVEL_MENU
                g_sim.menuActive = g_sim.canLaunch && g_sim.menuShows;
                g_sim.menuOpens++;
                g_sim.timeScale = 0.1f;
                st = 1;
            }
        }
        break;
    case 1:
        if (fl & 16) { fl &= ~16; MenuClose(); st = 0; break; }
        if (HorseDeadOrWrithing(h7) && HorseDeadOrWrithing(h1)) { MenuClose(); st = 0; break; }
        if (!(GI(GL_CAMP) & (1 << 23))) { MenuClose(); st = 0; break; }
        if (!ShortUpdateScenarioOk() || GI(GL_MENU_STOP_A) || GI(GL_MENU_STOP_B) || GI(GL_UIBUSY))   // func_502 to func_505
        {
            MenuClose();
            st = 0;
            break;
        }
        if (g_sim.menuResult == 2) { fl |= 8; MenuClose(); st = 2; }
        else if (g_sim.menuResult == 1) { MenuClose(); st = 0; }
        g_sim.menuResult = 0;
        break;
    case 2:
        if (fl & 1) { fl &= ~1; g_sim.music++; }
        if (fl & 32)
        {
            fl &= ~32;
            g_sim.takenHorse = 0; g_sim.secondHorse = 0;
            bool in7 = HorseInRange(h7, slot7), in1 = HorseInRange(h1, 1);
            if (!in7 && in1) g_sim.takenHorse = h1;
            else if (in7) { g_sim.takenHorse = h7; if (in1) g_sim.secondHorse = h1; }
            if (Ent* e = FindEnt(g_sim.takenHorse)) e->pcf388 = 1;
            if (Ent* e = FindEnt(g_sim.secondHorse)) e->pcf388 = 1;
            if (!g_sim.refuseStart)
            {
                st = 4;
                g_sim.travelFrames = 0;
            }
        }
        break;
    case 4:
        st = 5;
        break;
    case 5:
        if (g_sim.stuckTravel)
            break;
        if (++g_sim.travelFrames > 20)
        {
            if (Ent* e = FindEnt(g_sim.takenHorse)) e->pcf388 = 0;
            if (Ent* e = FindEnt(g_sim.secondHorse)) e->pcf388 = 0;
            g_sim.px = g_sim.destX; g_sim.py = g_sim.destY; g_sim.pz = g_sim.destZ;
            g_sim.mountedOn = 0;
            if (Ent* e = FindEnt(g_sim.takenHorse))
            {
                e->x = g_sim.destX; e->y = g_sim.destY; e->z = g_sim.destZ;
                g_sim.mountedOn = e->handle;
            }
            if (g_sim.simFadesIn && g_sim.fade == 2)
                SimFadeIn(1000);
            st = 6;
        }
        break;
    case 6:
        g_sim.musicStop++;
        fl = 0;
        st = 0;
        g_sim.trips++;
        CampDelete();   // after a trip, the CAMP text is gone (as players found in the game)
        break;
    }
    SI(GL_FT_STATE, st);
    SI(GL_FT_FLAGS, fl);
}

// player_camp, reduced to what the plugin sees (its state in Global_43885):
//   1  loads the CAMP text, gets Arthur up from the kneel (DYNAMIC_KNEEL_SLOW_EXIT
//      and CLEAR_PED_TASKS) and waits until he's out of it;
//   2  looks for the nearest campsite: found -> 3, none -> 14 ("Cannot create
//      camp in this location.") and it ends a moment later;
//   3  pitches the camp: up (9) after a while, with bit 23 of Global_40.f_9419,
//      and of Global_1392040 while you're by the fire.
// Its cleanup (func_4) sets Global_43885 back to 0 and unloads the CAMP text.
static void PlayerCampEnd()
{
    g_sim.campRunning = 0;
    SI(GL_CAMP_STATE, 0);
    SI(GL_CAMP_IS_UP, GI(GL_CAMP_IS_UP) & ~(1 << 23));
    SI(GL_CAMP, GI(GL_CAMP) & ~(1 << 23));
    CampDelete();
}

static void PlayerCampTick()
{
    if (!g_sim.campRunning)
        return;
    int f = ++g_sim.campFrames;
    switch (GI(GL_CAMP_STATE))
    {
    case 0:
        CampRequest();
        SI(GL_CAMP_STATE, 1);
        break;
    case 1:
        if (g_sim.kneel == K_DOWN || g_sim.kneel == K_KNEEL)
        {
            g_sim.kneel = K_EXIT;
            g_sim.kneelFrames = 10;
            g_sim.campKneelExits++;
        }
        else if (g_sim.kneel == K_OFF)
            SI(GL_CAMP_STATE, 2);
        break;
    case 2:
        SI(GL_CAMP_STATE, g_sim.campFails ? 14 : 3);
        g_sim.campStateAt = f;
        break;
    case 3:
        if (f - g_sim.campStateAt >= 20)
        {
            SI(GL_CAMP_STATE, 9);
            SI(GL_CAMP_IS_UP, GI(GL_CAMP_IS_UP) | (1 << 23));
            SI(GL_CAMP, GI(GL_CAMP) | (1 << 23));
        }
        break;
    case 14:
        if (f - g_sim.campStateAt >= 3)
            PlayerCampEnd();
        break;
    }
}

extern "C" __declspec(dllexport) void FakeGameTick()
{
    g_sim.gameMs += 33;

    // the screen fade
    if ((g_sim.fade == 1 || g_sim.fade == 3) && g_sim.gameMs >= g_sim.fadeEnd)
        g_sim.fade = g_sim.fade == 1 ? 2 : 0;

    // the kneel: getting down, kneeling, standing up
    if (g_sim.kneel == K_DOWN)
    {
        g_sim.downElapsed++;
        if (--g_sim.kneelFrames <= 0)
            g_sim.kneel = K_KNEEL;
    }
    else if (g_sim.kneel == K_EXIT && --g_sim.kneelFrames <= 0)
        g_sim.kneel = K_OFF;

    // the CAMP text block finishes loading
    if (g_sim.campLoadIn > 0 && --g_sim.campLoadIn == 0)
        g_sim.campLoaded = 1;

    // dynamic_craft_scenario (its func_6): the prompts go up at the kneel's
    // animation event and set bit 2 of Global_1955830.f_1 (clearing bit 3).
    // Going down for a menu clears it; an interaction target or the camera
    // takes the prompts down without clearing it; Set Up Camp takes them down
    // and clears it while the group is still drawn.
    int hud = GI(GL_KNEEL_HUD);
    bool menuUp = g_sim.simpleCrafting || g_sim.journal || g_sim.itemInteraction;
    if (g_sim.kneel == K_OFF)
    {
        if (g_sim.dcState != 0)
        {
            hud &= ~4;                                      // its cleanup (func_7)
            KneelRemovePrompts();
        }
        g_sim.dcState = 0;
        g_sim.setCamp = 0;
    }
    else
    {
        if (g_sim.dcState == 0)
            g_sim.dcState = 1;
        if (g_sim.dcState == 1 && KneelEventFired() && !g_sim.interactionTarget && !g_sim.cameraItem &&
            !g_sim.playerDead && !menuUp)
        {
            g_sim.dcState = 2;
            KneelAddPrompts();
            if (!g_sim.hudBroken)
                hud = (hud | 4) & ~8;
        }
        else if (g_sim.dcState == 2)
        {
            if (g_sim.interactionTarget || g_sim.cameraItem)
            {
                g_sim.dcState = 1;                          // bit 2 left as it is
                KneelRemovePrompts();
            }
            else if (menuUp)
            {
                g_sim.dcState = 3;
                hud &= ~4;
                KneelRemovePrompts();
            }
            else if (g_sim.setCamp == 1)
            {
                g_sim.setCamp = 2;
                hud &= ~4;
                KneelRemovePrompts();
            }
        }
        else if (g_sim.dcState == 3 && !menuUp)
            g_sim.dcState = 1;
    }
    if (g_sim.campfireHud)
        hud |= 4 | 8;                                       // a campfire's own prompts
    SI(GL_KNEEL_HUD, hud);
    g_sim.groupDrawn = g_sim.dcState == 2 && g_sim.kneel != K_EXIT && !g_sim.playerDead;

    // a prompt in the drawn kneel menu without any text to show
    for (int i = 0; i < 64; i++)
    {
        Prompt& p = g_prompts[i];
        if (p.id && p.valid && !p.game && p.visible && p.group == 342152817 && g_sim.groupDrawn && !ShownText(p.text)[0])
        {
            g_sim.blankFrames++;
            g_sim.blankTotal++;
        }
    }

    // never two Set Up Camps in the drawn kneel menu, never two of the plugin's
    {
        int ours = 0;
        for (int i = 0; i < 64; i++)
            if (IsCampPrompt(g_prompts[i]))
                ours++;
        if (ours > g_sim.campPromptsMax)
            g_sim.campPromptsMax = ours;
        if (g_sim.groupDrawn && ours > 0 && FindPrompt(g_sim.kneelPrompts[1]))
            g_sim.dupFrames++;
    }

    // input: a press queued by the test is released this frame
    g_sim.pressedNow = (uint32_t)g_sim.pendingPress;
    g_sim.pendingPress = 0;
    for (int i = 0; i < 64; i++)
    {
        Prompt& p = g_prompts[i];
        p.justReleased = 0;
        p.justPressed = 0;
        if (!p.id || !p.valid || !p.enabled || !p.visible || g_sim.pressedNow == 0 || p.control != g_sim.pressedNow)
            continue;
        bool shown = p.group == 0 || (p.group == 342152817 && g_sim.groupDrawn);
        if (shown && p.standard && p.release)
            p.justReleased = 1;
        if (shown && p.standard && !p.release)
            p.justPressed = 1;
    }
    // the kneel menu's own Set Up Camp pressed (func_25 -> func_27): it starts
    // player_camp and takes its prompts down (next frame here)
    if (g_sim.dcState == 2 && g_sim.setCamp == 0)
        if (Prompt* own = FindPrompt(g_sim.kneelPrompts[1]))
            if (own->justPressed)
            {
                g_sim.setCamp = 1;
                g_sim.gameCampStarts++;
                if (!g_sim.campRunning) { g_sim.campRunning = 1; g_sim.campFrames = 0; SI(GL_CAMP_STATE, 0); }
            }

    // per-frame control disables are cleared each frame, as in the game
    g_sim.disabledX = g_sim.disabledY = g_sim.disabledJump = g_sim.disabledAll = g_sim.disabledOther = 0;
    g_sim.disabledB = g_sim.disabledA = 0;

    // the control context lasts the frame it's set for
    g_sim.ctxFrontendLast = g_sim.ctxFrontend;
    g_sim.ctxFrontend = 0;

    // dynamic_craft_scenario: movement held 0.15 s while kneeling -> DYNAMIC_KNEEL_SLOW_EXIT.
    // It reads GET_DISABLED_CONTROL_NORMAL, which sees through disabled controls
    // but not through a control context without movement in it.
    if (g_sim.kneel == K_KNEEL && g_sim.groupDrawn && g_sim.moveInput > 0.1f && !g_sim.ctxFrontendLast)
    {
        if (++g_sim.moveFrames >= 5)
        {
            g_sim.kneel = K_EXIT;
            g_sim.kneelFrames = 20;
            g_sim.moveFrames = 0;
        }
    }
    else
        g_sim.moveFrames = 0;

    // REQUEST_SCRIPT("player_camp") finishes after a few frames
    if (g_sim.campScriptLoadIn > 0 && --g_sim.campScriptLoadIn == 0)
        g_sim.campScriptLoaded = 1;

    PlayerCampTick();
    ShortUpdateTick();
}

// ---------------------------------------------------------------------------
// Test control
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) ScriptMainFn FakeGetScriptMain() { return g_scriptMain; }
extern "C" __declspec(dllexport) void FakeSetHostFiber(void* f) { g_hostFiber = f; }

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
    S_EVELYN, S_ON_VEHICLE, S_RAGDOLL, S_IN_BOAT, S_RIDING_TRAIN, S_JACKING, S_KIT_CAMP_OFF, S_KIT_SIMPLE_OFF,
    S_CAMP_SCRIPT_LOAD_FRAMES,
};

extern "C" __declspec(dllexport) void FakeSet(int what, int v)
{
    switch (what)
    {
    case S_KNEEL:
        if (v) { g_sim.kneel = K_DOWN; g_sim.kneelFrames = g_sim.downFrames; g_sim.downElapsed = 0; g_sim.setCamp = 0; }
        else   { if (g_sim.kneel != K_OFF) { g_sim.kneel = K_EXIT; g_sim.kneelFrames = 10; } }
        break;
    case S_DOWN_FRAMES:   g_sim.downFrames = v; break;
    case S_CAMP_LOADED:   if (v) { g_sim.campLoaded = 1; g_sim.campLoadIn = 0; } else CampDelete(); break;
    case S_CAMP_LOAD_FRAMES: g_sim.campLoadFrames = v; break;
    case S_CAMP_NEVER:    g_sim.campNeverLoads = v; break;
    case S_CAMP_SCRIPT:
        // camp scripts start or end; those that unload the CAMP text when they end do so
        for (int i = 0; i < (int)(sizeof kCampScripts / sizeof kCampScripts[0]); i++)
            if (((g_sim.scripts >> i) & 1) && !((v >> i) & 1) && kCampScripts[i].unloads)
                CampDelete();
        g_sim.scripts = v;
        break;
    case S_FRENCH:        g_sim.french = v; break;
    case S_SET_CAMP:      if (v && g_sim.dcState == 2) g_sim.setCamp = 1; break;   // Set Up Camp (E) pressed
    case S_CAMPFIRE_HUD:
        g_sim.campfireHud = v;
        if (!v) SI(GL_KNEEL_HUD, GI(GL_KNEEL_HUD) & ~(4 | 8));   // the campfire takes its prompts down
        break;
    case S_HUD_BROKEN:    g_sim.hudBroken = v; if (v) SI(GL_KNEEL_HUD, 0); break;
    case S_NOT_ACTIVE:    g_sim.notActive = v; break;
    case S_LABEL_UNREADABLE: g_sim.labelUnreadable = v; break;
    case S_CAMP_HERE:     g_sim.campHere = v; break;          // the game allows a camp where he kneels
    case S_LIST_BROKEN:   // the kneel's prompts not in the list (another layout) / found there after all
        g_sim.listBroken = v;
        if (v) KneelUnlistPrompts(); else KneelListPrompts();
        break;
    case S_KIT_CAMP:      g_sim.kitCamp = v; break;
    case S_KIT_SIMPLE:    g_sim.kitSimple = v; break;
    case S_BACKUP_INV:    g_sim.backupInv = v; break;
    case S_INTERIOR:      g_sim.interior = v; break;
    case S_COMBAT:        g_sim.combat = v; break;
    case S_RECENT_CRIME:  g_sim.recentCrime = v; break;
    case S_MINIGAME:      g_sim.minigame = v; break;
    case S_ANIMAL_INTERACTION: g_sim.animalInteraction = v; break;
    case S_CAMP_SCRIPT_EXISTS: g_sim.campScriptExists = v; break;
    case S_CAMP_SCRIPT_NO_LOAD: g_sim.campScriptNoLoad = v; break;
    case S_CAMP_FAILS:    g_sim.campFails = v; break;
    case S_CAMP_DOWN:     if (g_sim.campRunning) PlayerCampEnd(); break;   // the camp is packed up
    case S_EVELYN:        g_sim.evelyn = v; break;
    case S_ON_VEHICLE:    g_sim.onVehicle = v; break;
    case S_RAGDOLL:       g_sim.ragdoll = v; break;
    case S_IN_BOAT:       g_sim.inBoat = v; break;
    case S_RIDING_TRAIN:  g_sim.ridingTrain = v; break;
    case S_JACKING:       g_sim.jacking = v; break;
    case S_KIT_CAMP_OFF:  g_sim.kitCampOff = v; break;
    case S_KIT_SIMPLE_OFF: g_sim.kitSimpleOff = v; break;
    case S_CAMP_SCRIPT_LOAD_FRAMES: g_sim.campScriptLoadFrames = v; break;
    case S_CRAFTING:
        // the kneel menu's Craft: simple_crafting loads the CAMP text when it
        // starts (its func_30) and unloads it when it ends
        g_sim.simpleCrafting = v;
        if (v) CampRequest(); else CampDelete();
        break;
    case S_INTERACTION:   g_sim.interactionTarget = v; break;
    case S_CAMERA:        g_sim.cameraItem = v; break;
    case S_WANTED:        g_sim.wanted = v; break;
    case S_MISSION:       g_sim.mission = v; break;
    case S_ONLINE:        g_sim.online = v; break;
    case S_PAUSED:        g_sim.paused = v; break;
    case S_PLAYERMENU:    g_sim.playerMenu = v; break;
    case S_CANLAUNCH:     g_sim.canLaunch = v; break;
    case S_SU_RUNNING:    g_sim.suRunning = v; break;
    case S_SU_IGNORE:     g_sim.suIgnore = v; break;
    case S_DESTINATIONS:  g_sim.destinations = v; break;
    case S_REFUSE:        g_sim.refuseStart = v; break;
    case S_SIMFADEIN:     g_sim.simFadesIn = v; break;
    case S_PLAYERDEAD:    g_sim.playerDead = v; break;
    case S_PRESS:         g_sim.pendingPress = v; break;
    case S_MENU:          g_sim.menuResult = v; break;   // 1 = back out, 2 = choose a place
    case S_PLAYERPOS_X:   g_sim.px = (float)v; break;
    case S_PLAYERPOS_Y:   g_sim.py = (float)v; break;
    case S_DEST_X:        g_sim.destX = (float)v; break;
    case S_DEST_Y:        g_sim.destY = (float)v; break;
    case S_INVEHICLE:     g_sim.inVehicle = v; break;
    case S_ITEMINTERACTION: g_sim.itemInteraction = v; break;
    case S_MENU_SHOWS:    g_sim.menuShows = v; break;
    case S_MOVE:          g_sim.moveInput = v ? 1.0f : 0.0f; break;
    case S_NO_SCENARIO_TASK: g_sim.noScenarioTask = v; break;
    case S_STUCK_TRAVEL:  g_sim.stuckTravel = v; break;
    case S_RESET_COUNTERS:
        g_sim.modFadeIns = g_sim.modFadeOuts = g_sim.clearTasks = g_sim.kneelExitCmds = 0;
        g_sim.music = g_sim.musicStop = g_sim.menuOpens = g_sim.modMusicStops = 0;
        g_sim.blankFrames = 0;
        g_sim.campStarts = g_sim.gameCampStarts = g_sim.scriptRequests = g_sim.noLongerNeeded = 0;
        g_sim.campKneelExits = 0;
        g_sim.campArgc = g_sim.campStack = 0;
        memset(g_sim.campArgs, 0, sizeof g_sim.campArgs);
        g_helpCount = 0;
        break;
    }
}

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
    Q_DUP_FRAMES, Q_CAMP_PROMPTS_MAX, Q_CAMP_STATE, Q_KNEEL_VISIBLE, Q_KNEEL_PROMPTS, Q_DISABLED_ASK_P2,
    Q_CAMP_ARG0 = 200,   // ...to Q_CAMP_ARG0 + 9
};

static Prompt* CurrentPrompt()   // Fast Travel
{
    for (int i = 0; i < 64; i++)
        if (g_prompts[i].id && g_prompts[i].valid && !g_prompts[i].game && !IsCampPrompt(g_prompts[i]))
            return &g_prompts[i];
    return nullptr;
}

static Prompt* CampPrompt()
{
    for (int i = 0; i < 64; i++)
        if (IsCampPrompt(g_prompts[i]))
            return &g_prompts[i];
    return nullptr;
}

static int CountPrompts(bool camp)
{
    int n = 0;
    for (int i = 0; i < 64; i++)
        if (g_prompts[i].id && g_prompts[i].valid && !g_prompts[i].game && IsCampPrompt(g_prompts[i]) == camp)
            n++;
    return n;
}

extern "C" __declspec(dllexport) int FakeGet(int what)
{
    Prompt* p = CurrentPrompt();
    switch (what)
    {
    case Q_LIVE_PROMPTS:    return LivePrompts();
    case Q_PROMPTS_MADE:    return g_promptsMade;
    case Q_PROMPTS_DELETED: return g_promptsDeleted;
    case Q_P_CONTROL:       return p ? (int)p->control : 0;
    case Q_P_PRIORITY:      return p ? p->priority : -1;
    case Q_P_TRANSPORT:     return p ? p->transport : -1;
    case Q_P_STANDARD:      return p ? p->standard : -1;
    case Q_P_RELEASE:       return p ? p->release : -1;
    case Q_P_ATTR18:        return p ? p->attr18 : -1;
    case Q_P_GROUP:         return p ? p->group : -1;
    case Q_P_TAB:           return p ? p->tab : -1;
    case Q_P_ENABLED:       return p ? p->enabled : -1;
    case Q_P_VISIBLE:       return p ? p->visible : -1;
    case Q_MENU_OPEN:       return g_sim.menuOpen;
    case Q_MENU_ACTIVE:     return g_sim.menuActive;
    case Q_MENU_OPENS:      return g_sim.menuOpens;
    case Q_KNEEL:           return g_sim.kneel;
    case Q_FADE:            return g_sim.fade;
    case Q_MOD_FADEOUTS:    return g_sim.modFadeOuts;
    case Q_MOD_FADEINS:     return g_sim.modFadeIns;
    case Q_MUSIC:           return g_sim.music;
    case Q_MUSIC_STOP:      return g_sim.musicStop;
    case Q_TRIPS:           return g_sim.trips;
    case Q_MOUNTED:         return g_sim.mountedOn;
    case Q_TAKEN_HORSE:     return g_sim.takenHorse;
    case Q_CLEAR_TASKS:     return g_sim.clearTasks;
    case Q_KNEEL_EXITS:     return g_sim.kneelExitCmds;
    case Q_DISABLED_X:      return g_sim.disabledX;
    case Q_DISABLED_Y:      return g_sim.disabledY;
    case Q_DISABLED_JUMP:   return g_sim.disabledJump;
    case Q_DISABLED_ALL:    return g_sim.disabledAll;
    case Q_DISABLED_OTHER:  return g_sim.disabledOther;
    case Q_HELP_COUNT:      return g_helpCount;
    case Q_UNKNOWN:         return g_unknownHashes;
    case Q_BAD_ARGS:        return g_badArgCount;
    case Q_BAD_VARSTRING:   return g_badVarString;
    case Q_BAD_HELP:        return g_badHelp;
    case Q_PLAYER_X:        return (int)g_sim.px;
    case Q_PLAYER_Y:        return (int)g_sim.py;
    case Q_TIMESCALE_X10:   return (int)lroundf(g_sim.timeScale * 10.0f);
    case Q_GAME_MS:         return g_sim.gameMs;
    case Q_LAST_UNKNOWN_LO: return (int)(uint32_t)g_lastUnknown;
    case Q_LAST_BADARG_LO:  return (int)(uint32_t)g_lastBadArg;
    case Q_CONTEXT_SETS:    return g_sim.contextSets;
    case Q_PAUSE_ENABLED:   return g_sim.pauseEnabled;
    case Q_MOD_MUSIC_STOPS: return g_sim.modMusicStops;
    case Q_PCF_100:         { Ent* e = FindEnt(100); return e ? e->pcf388 : -1; }
    case Q_PCF_101:         { Ent* e = FindEnt(101); return e ? e->pcf388 : -1; }
    case Q_GROUP_DRAWN:     return g_sim.groupDrawn;
    case Q_CAMP_LOADED:     return g_sim.campLoaded;
    case Q_CAMP_REQUESTS:   return g_sim.campRequests;
    case Q_CAMP_DELETES:    return g_sim.campDeletes;
    case Q_BLANK_FRAMES:    return g_sim.blankFrames;
    case Q_BLANK_TOTAL:     return g_sim.blankTotal;
    case Q_FT_PROMPTS:      return CountPrompts(false);
    case Q_CAMP_PROMPTS:    return CountPrompts(true);
    case Q_GAME_PROMPTS:    return GamePrompts();
    case Q_KNEEL_OWN_CAMP:  return FindPrompt(g_sim.kneelPrompts[1]) != nullptr;
    case Q_CAMP_STARTS:     return g_sim.campStarts;
    case Q_GAME_CAMP_STARTS: return g_sim.gameCampStarts;
    case Q_CAMP_ARGC:       return g_sim.campArgc;
    case Q_CAMP_STACK:      return g_sim.campStack;
    case Q_CAMP_RUNNING:    return g_sim.campRunning;
    case Q_SCRIPT_REQUESTS: return g_sim.scriptRequests;
    case Q_NO_LONGER_NEEDED: return g_sim.noLongerNeeded;
    case Q_BAD_SCRIPT_CALLS: return g_sim.badScriptCalls;
    case Q_BAD_INVENTORY:   return g_sim.badInventory;
    case Q_DISABLED_B:      return g_sim.disabledB;
    case Q_DISABLED_A:      return g_sim.disabledA;
    case Q_CAMP_KNEEL_EXITS: return g_sim.campKneelExits;
    case Q_DUP_FRAMES:      return g_sim.dupFrames;
    case Q_CAMP_PROMPTS_MAX: return g_sim.campPromptsMax;
    case Q_CAMP_STATE:      return GI(GL_CAMP_STATE);
    case Q_DISABLED_ASK_P2: return g_sim.disabledAskP2;
    case Q_KNEEL_VISIBLE:
    case Q_KNEEL_PROMPTS:
    {
        int n = 0;
        for (int i = 0; i < 3; i++)
            if (Prompt* k = FindPrompt(g_sim.kneelPrompts[i]))
                n += what == Q_KNEEL_PROMPTS || k->visible;
        return n;
    }
    }
    if (what >= Q_CAMP_ARG0 && what < Q_CAMP_ARG0 + 10)
        return (int)g_sim.campArgs[what - Q_CAMP_ARG0];
    if (what >= Q_CP_CONTROL && what <= Q_CP_VISIBLE)
    {
        Prompt* c = CampPrompt();
        if (!c)
            return -1;
        switch (what)
        {
        case Q_CP_CONTROL:   return (int)c->control;
        case Q_CP_PRIORITY:  return c->priority;
        case Q_CP_TRANSPORT: return c->transport;
        case Q_CP_STANDARD:  return c->standard;
        case Q_CP_RELEASE:   return c->release;
        case Q_CP_ATTR18:    return c->attr18;
        case Q_CP_GROUP:     return c->group;
        case Q_CP_TAB:       return c->tab;
        case Q_CP_ENABLED:   return c->enabled;
        case Q_CP_VISIBLE:   return c->visible;
        }
    }
    return -999;
}

extern "C" __declspec(dllexport) void FakeText(int what, char* out, int n)
{
    out[0] = 0;
    if (what == 1)        // the live prompt's text
    {
        if (Prompt* p = CurrentPrompt())
            snprintf(out, n, "%s", p->text);
    }
    else if (what == 2)   // the last help text
    {
        if (g_helpCount)
            snprintf(out, n, "%s", g_helps[(g_helpCount - 1) % 64]);
    }
    else if (what == 3)   // the debug texts drawn since the last call
    {
        int first = g_textCount > 64 ? g_textCount - 64 : 0;
        for (int i = first; i < g_textCount; i++)
        {
            strncat(out, g_texts[i % 64], n - strlen(out) - 2);
            strncat(out, "\n", n - strlen(out) - 1);
        }
        g_textCount = 0;
    }
    else if (what == 4)   // what the live prompt shows right now
    {
        if (Prompt* p = CurrentPrompt())
            snprintf(out, n, "%s", ShownText(p->text));
    }
    else if (what == 5)   // the Set Up Camp prompt's text as set
    {
        if (Prompt* p = CampPrompt())
            snprintf(out, n, "%s", p->text);
    }
    else if (what == 6)   // ...and what it shows
    {
        if (Prompt* p = CampPrompt())
            snprintf(out, n, "%s", ShownText(p->text));
    }
}

extern "C" __declspec(dllexport) void FakeSetGlobal(int idx, int v) { if (idx >= 0 && idx < kGlobalCount) SI(idx, v); }
extern "C" __declspec(dllexport) void FakeSetGlobalF(int idx, float v) { if (idx >= 0 && idx < kGlobalCount) SF(idx, v); }
extern "C" __declspec(dllexport) int  FakeGetGlobal(int idx) { return (idx >= 0 && idx < kGlobalCount) ? GI(idx) : 0; }

// A horse: its ped handle goes in Global_1900383[slot], its bonding in the save data.
extern "C" __declspec(dllexport) void FakeHorse(int handle, int slot, int exists, int dead, int x, int y, int level, int points)
{
    Ent* e = FindEnt(handle);
    if (!e)
    {
        e = &g_ents[g_entCount++];
        memset(e, 0, sizeof *e);
        e->handle = handle;
    }
    e->exists = exists; e->dead = dead; e->injured = 0; e->writhe = 0; e->human = 0;
    e->x = (float)x; e->y = (float)y; e->z = 0.0f;
    e->model = kHorseModel; e->rank = level; e->points = points;
    if (slot >= 0 && slot < 7)
    {
        SI(GL_HORSE_PEDS + 1 + slot * 45, exists ? handle : 0);
        int base = GL_HORSE_SAVES + 1 + slot * 436;
        SI(base + 9, (int)kHorseModel);
        SI(base + 372, level);
        SF(base + 373, (float)points);
    }
}

extern "C" __declspec(dllexport) void FakeInit()
{
    memset(&g_sim, 0, sizeof g_sim);
    g_sim.gameMs = 100000;
    g_sim.suRunning = 1;
    g_sim.canLaunch = 1;
    g_sim.menuShows = 1;
    g_sim.destinations = 6;
    g_sim.timeScale = 1.0f;
    g_sim.destX = 5000; g_sim.destY = 5000;
    g_sim.downFrames = 30;       // about a second to get down on one knee...
    g_sim.eventFrame = 8;        // ...and the kneel menu comes up a quarter of a second in
    g_sim.campLoadFrames = 3;
    g_sim.campHere = 1;          // the wilderness: the kneel menu has its own Set Up Camp
    g_sim.kitCamp = 1;
    g_sim.campScriptExists = 1;
    g_sim.campScriptLoadFrames = 3;
    SI(GL_PLAYER, kPlayerPed);
    // Another script's prompt on E in the shared prompt list (long_update's thread),
    // which isn't the kneel menu's
    {
        Prompt& p = g_prompts[63];
        memset(&p, 0, sizeof p);
        p.id = 999;
        p.valid = p.enabled = p.standard = 1;
        p.game = 1;
        p.control = Joaat("INPUT_CONTEXT_Y");
        snprintf(p.text, sizeof p.text, "%s", "PICKUP");
        int base = GL_PROMPT_LIST + 1 + 30 * 18;
        SI(base + 1, 2 | 128);
        SI(base + 3, p.id);
        SI(base + 4, (int)p.control);
        SI(base + 16, 7101);
    }
    SI(GL_SESSION, -1);
    SI(GL_LODGING, 3);
    SI(GL_ACTIVE_HORSE, 0);
    SI(GL_UNLOCK_FLOW, 0);
}
