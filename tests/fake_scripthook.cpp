// Stand-in for ScriptHookRDR2.dll, used only by the automated test (tests/run_tests.sh).
// It exports the same MSVC-mangled names as the real Script Hook, simulates the
// natives and script globals Fast Travel Anywhere uses, and runs a model of the
// two game scripts it works with, written from the decompiled scripts of game
// build 1491.50:
//   * dynamic_craft_scenario: the hold-E kneel and its "Resting" prompt group;
//   * short_update's "PLAYER CAMP FAST TRAVEL" module (func_80): the requests in
//     Global_43907, its state in Global_43908, the menu, the horse it takes
//     along and the trip.
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
    GL_FLOW_STATUS = 12106, GL_KNEEL_HUD = 1955831,
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
};
static Sim g_sim;

// ---------------------------------------------------------------------------
// Prompts
// ---------------------------------------------------------------------------
struct Prompt
{
    int      id, valid, registering;
    uint32_t control;
    char     text[128];
    int      priority, transport, standard, release, attr18, group, tab, enabled, visible;
    int      justReleased;
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

static int LivePrompts()
{
    int n = 0;
    for (int i = 0; i < 64; i++)
        if (g_prompts[i].id && g_prompts[i].valid)
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

static bool PlayerKneelActive() { return g_sim.kneel == K_KNEEL; }

// The kneel's "Resting" group is drawn this frame (dynamic_craft_scenario state 2).
static bool KneelGroupDrawn()
{
    return g_sim.kneel == K_KNEEL && !g_sim.interactionTarget && !g_sim.cameraItem &&
           !g_sim.simpleCrafting && !g_sim.journal && !g_sim.itemInteraction && !g_sim.playerDead;
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
        g_result[0] = (uint32_t)n;
        break;
    }
    case 0xAA135F9482C82CC3ULL: Want(2); g_result[0] = I(0) == kPlayerPed && PlayerKneelActive(); break;   // IS_PED_ACTIVE_IN_SCENARIO
    case 0x0C3CB2E600C8977DULL: Want(2); g_result[0] = I(0) == kPlayerPed && g_sim.kneel == K_EXIT; break; // IS_PED_EXITING_SCENARIO
    case 0xEC7E480FF8BD0BEDULL: Want(1); g_result[0] = g_sim.itemInteraction; break;                         // IS_PED_RUNNING_TASK_ITEM_INTERACTION
    case 0x295E3CCEC879CCD7ULL: Want(1); g_result[0] = I(0) == kPlayerPed && g_sim.kneel != K_OFF && !g_sim.noScenarioTask; break; // PED_HAS_USE_SCENARIO_TASK
    case 0x02EBBB3989B7E695ULL: Want(1); g_result[0] = I(0) == kPlayerPed && g_sim.kneel == K_KNEEL; break;  // _PED_IS_IN_SCENARIO_BASE
    case 0x3EE1F7A8C32F24E1ULL: {                                                         // GET_PLAYER_INTERACTION_TARGET_ENTITY
        Want(4);
        int64_t* out = (int64_t*)(uintptr_t)g_args[1];
        if (out) *out = g_sim.interactionTarget;
        g_result[0] = g_sim.interactionTarget != 0;
        break;
    }

    // ---- prompts ----
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
        if (!PlayerKneelActive() || g_sim.playerDead) { MenuClose(); st = 0; break; }  // func_502
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
        break;
    }
    SI(GL_FT_STATE, st);
    SI(GL_FT_FLAGS, fl);
}

extern "C" __declspec(dllexport) void FakeGameTick()
{
    g_sim.gameMs += 33;

    // the screen fade
    if ((g_sim.fade == 1 || g_sim.fade == 3) && g_sim.gameMs >= g_sim.fadeEnd)
        g_sim.fade = g_sim.fade == 1 ? 2 : 0;

    // the kneel: getting down, kneeling, standing up
    if (g_sim.kneel == K_DOWN && --g_sim.kneelFrames <= 0)
        g_sim.kneel = K_KNEEL;
    else if (g_sim.kneel == K_EXIT && --g_sim.kneelFrames <= 0)
        g_sim.kneel = K_OFF;

    // dynamic_craft_scenario draws its group (and notes it in Global_1955830.f_1 bit 2)
    g_sim.groupDrawn = KneelGroupDrawn();
    int hud = GI(GL_KNEEL_HUD);
    SI(GL_KNEEL_HUD, g_sim.groupDrawn ? (hud | 4) : (hud & ~4));

    // input: a press queued by the test is released this frame
    g_sim.pressedNow = (uint32_t)g_sim.pendingPress;
    g_sim.pendingPress = 0;
    for (int i = 0; i < 64; i++)
    {
        Prompt& p = g_prompts[i];
        p.justReleased = 0;
        if (!p.id || !p.valid || !p.enabled || !p.visible || g_sim.pressedNow == 0 || p.control != g_sim.pressedNow)
            continue;
        bool shown = p.group == 0 || (p.group == 342152817 && g_sim.groupDrawn);
        if (shown && p.standard && p.release)
            p.justReleased = 1;
    }

    // per-frame control disables are cleared each frame, as in the game
    g_sim.disabledX = g_sim.disabledY = g_sim.disabledJump = g_sim.disabledAll = g_sim.disabledOther = 0;

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
};

extern "C" __declspec(dllexport) void FakeSet(int what, int v)
{
    switch (what)
    {
    case S_KNEEL:
        if (v) { g_sim.kneel = K_DOWN; g_sim.kneelFrames = 10; }
        else   { if (g_sim.kneel != K_OFF) { g_sim.kneel = K_EXIT; g_sim.kneelFrames = 10; } }
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
};

static Prompt* CurrentPrompt()
{
    for (int i = 0; i < 64; i++)
        if (g_prompts[i].id && g_prompts[i].valid)
            return &g_prompts[i];
    return nullptr;
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
    SI(GL_PLAYER, kPlayerPed);
    SI(GL_SESSION, -1);
    SI(GL_LODGING, 3);
    SI(GL_ACTIVE_HORSE, 0);
    SI(GL_UNLOCK_FLOW, 0);
}
