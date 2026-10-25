# Fast Travel Anywhere (RDR2)

A [Script Hook RDR2](http://www.dev-c.com/rdr2/scripthookrdr2/) plugin for **Red Dead Redemption 2 on PC (Story Mode)**.

Hold **E** to kneel anywhere, and the kneel menu in the bottom-right corner (Craft, Set Up Camp, Leave) gets a **Fast Travel** button on **Space** (A on a controller), the same button a campfire has. It opens the game's own fast travel menu: the same list of places, the same trip with your horse, the same rules as at a campfire.

## Download

Download [`release/FastTravelAnywhere-1.0.1.zip`](release/FastTravelAnywhere-1.0.1.zip) (on that page, use the download button). It contains `FastTravelAnywhere.asi`, `FastTravelAnywhere.ini`, a README and the full source.

## Install

1. Install the latest [Script Hook RDR2](http://www.dev-c.com/rdr2/scripthookrdr2/). Its `ScriptHookRDR2.dll` and `dinput8.dll` go in your game folder (the one with `RDR2.exe`). On Steam: *Library → right-click Red Dead Redemption 2 → Manage → Browse local files*.
2. Copy `FastTravelAnywhere.asi` and `FastTravelAnywhere.ini` into the same folder.
3. Load Story Mode. A yellow message appears in the top-left corner for a few seconds:
   `Fast Travel Anywhere 1.0.1 loaded - hold E to kneel, then Space: Fast Travel`

To uninstall, delete `FastTravelAnywhere.asi`, `FastTravelAnywhere.ini` and `FastTravelAnywhere.log`.

## Using it

1. Hold **E** with nothing around you to kneel. The kneel menu appears in the bottom-right corner, now with **Fast Travel** on Space.
2. Press **Space**. The game's fast travel menu opens, the same one as at a campfire. You can press it as soon as it appears: if Arthur is still getting down, the menu opens the moment he's down.
3. Pick a place. Arthur stands up, the screen fades out, and you arrive at the place on your horse. Back out of the menu to stay.

In the menu, use the arrow keys, the mouse or the D-pad. W/S or the left stick might make Arthur get up from the kneel, which closes the menu (see [Troubleshooting](#troubleshooting)).

## The rules

Fast Travel follows the game's own rules for fast travel at a campfire:

| Rule | Message when you press the greyed-out button |
| --- | --- |
| Fast travel has to be unlocked, as for your camp (Arthur: the lodging upgrade in the camp ledger) | "You haven't unlocked fast travel yet. ..." |
| Your horse has to be alive and within whistling range: it travels with you | the game's own *"You can't fast travel as your horse is dead."*, *"Your horse is too far away to fast travel."* or *"You can't fast travel without your horse."* |
| Not while you're wanted | "You can't fast travel while you're wanted." |
| Not during a mission | "You can't fast travel during a mission." |
| You have to have discovered somewhere to go | the game's own *"Discover more regions to use fast travel. Regions in lockdown are unavailable."* |

Which places are listed (and which are off-limits, such as New Austin for Arthur) is decided by the game itself, as at a campfire. When the button can't be used it's shown greyed out, and pressing it says why. The unlock, horse, wanted and mission rules can each be relaxed in the `.ini`.

## Settings (`FastTravelAnywhere.ini`)

Changes take effect the next time you kneel, no restart needed.

| Setting | Default | What it does |
| --- | --- | --- |
| `Enabled` | `1` | Fast Travel in the kneel menu on/off. |
| `RequireUnlock` | `1` | Fast travel has to be unlocked, as for your camp. |
| `RequireHorseNearby` | `1` | Your horse has to be within whistling range. `0` = also when it's too far away; you arrive on foot. A living horse is always needed: the game's menu closes without one. |
| `AllowWhenWanted` | `0` | `1` = allow fast travel while wanted. |
| `AllowOnMissions` | `0` | `1` = allow fast travel during a mission. |
| `ShowWhenUnavailable` | `1` | Show the button greyed out when it can't be used (pressing it says why). `0` = hide it, as a campfire does. |
| `StandUp` | `1` | Arthur stands up as the screen fades out. |
| `PromptControl` | `INPUT_CONTEXT_A` | The button, by the game's control name (`INPUT_CONTEXT_A` is Space / A). The kneel menu already uses R, E and F. |
| `PromptText` | *(empty)* | Empty = the game's own "Fast Travel" text, in your game's language. |
| `ShowLoadMessage` | `1` | The "loaded" message when Story Mode starts. |
| `DebugLog` | `0` | More detail in `FastTravelAnywhere.log`. |
| `LockMovementInMenu` | `0` | Experimental. If the menu closes when you scroll it with W/S or the left stick, `1` switches the controls to menu-only while it's open. |

## How it works

Everything here comes from the game's decompiled scripts (build 1491.50).

- **The kneel menu** belongs to the game's `dynamic_craft_scenario` script. Its prompts sit in prompt group `342152817` (`REST_UI_GROUP`, titled "Resting"), which that script draws every frame while you kneel. It shows them as soon as the kneel animation's event fires, before Arthur is all the way down, and notes that in bit 2 of `Global_1955830.f_1`; it clears the bit when it takes them down again (for the Craft menu, the journal, Set Up Camp). The plugin adds its prompt to the same group at the same moment, with the campfire's own text label `CAMP_FT_PROMPT`, and takes it away with them. (If that bit never shows during a kneel, as could happen on another game build, it falls back to 1.0.0's way: the prompt once Arthur is all the way down.)
- **That label lives in the game's `CAMP` text block**, as do the fast travel menu's own heading ("FAST TRAVEL", "Locations") and the horse messages. Only the camp scripts load it (`player_camp`, `interactive_campfire`, `property_use_core`, `campfire_always`, each unloading it when done, and `long_update` while a campfire you left is still burning), and the kneel menu's own Craft menu (`simple_crafting`), which unloads it again when it closes. A prompt whose label isn't loaded shows no text, although its button still works: that was the Fast Travel that went missing in 1.0.0, after a trip for example. So the plugin asks for `CAMP` when a kneel starts, uses the label only while it's loaded, and otherwise shows the same words as plain text (the game's own, read from the label earlier, or English until then). A press waits for `CAMP` (a second at most) before opening the menu, so the menu has its heading. A few seconds after the kneel the plugin unloads `CAMP` again, if it loaded it and no camp script, Craft menu or camp message is using it.
- **A campfire's Fast Travel button doesn't travel by itself.** It sets bit 4 of `Global_43907`. The always-running `short_update` script (its "PLAYER CAMP FAST TRAVEL" module) picks that up, builds the list of places, opens the `FAST_TRAVEL_MENU` app and keeps its progress in `Global_43908` (0 idle, 1 menu open, 2 place chosen, 4–5 travelling, 6 done).
- **While the menu is open**, `short_update` closes it unless bit 23 of `Global_1392040` is set (the campfire scripts set it while you're at the fire), you're all the way down in a scenario (kneeling at the fire, or here the kneel: its `func_502`), and one of your horses is alive. So a press while Arthur is still getting down waits until he's down (up to 3 seconds).
- **When you pick a place**, `short_update` sets bit 8 and waits for the camp to pack up: the camp plays its kick-the-fire animation, the screen fades out, and it sets bit 32. Then `short_update` takes your horse along if it's within whistling range, loads the place and puts you on your horse.

So the plugin does what a campfire does: on the button it sets bit 23 and bit 4; when you pick a place it starts the fast travel music (bit 1), has Arthur stand up, fades the screen out, clears his tasks and sets bit 32; the game does the trip. It puts bit 23 back the way it was afterwards, and it checks the same things a campfire checks before it offers fast travel (`player_camp` `func_105` and `func_306`), with the game's own messages where the game has them (`property_use_core` `func_198`).

Safety:

- Before the plugin first writes anything, it checks the globals look like build 1491.50: `Global_35` has to be the player, and the fast travel globals have to hold the small values `short_update` keeps there. If not, Fast Travel stays greyed out and the log says why.
- If the game doesn't pick up the request within 3 seconds, the plugin takes it back, puts bit 23 back and turns itself off until the game restarts.
- It only offers the button while the kneel counts as a scenario task (`PED_HAS_USE_SCENARIO_TASK`), which the game's menu needs to stay open, and not while one of the flags is set that make `short_update` close its menu (`Global_1357517`, `Global_1935689.f_1`).
- If the menu stays open on paper but isn't showing, it asks the game to close it (bit 16, as a camp does when it shuts down).
- If the game never starts the trip (for example a horse that can't be mounted), it resets the game's fast travel after 12 seconds the way `short_update` finishes a trip (the horses' trip flag cleared, the fast travel music stopped, flags and state back to 0) and fades the screen back in. If the player dies after picking a place, it does the same, so the game isn't left waiting for a camp.
- If something else starts the trip during the fade (a camp of yours elsewhere being packed away also sets bit 32), it lets the game go ahead.
- If the screen is still black 3 seconds after the trip, it fades it in. If a trip is still loading after 2 minutes, it gives the screen back and leaves the trip alone.
- During the fade, all controls except pause are held, as the camp scripts do around their fades.
- Idle in Red Dead Online.

The plugin binds to `ScriptHookRDR2.dll` at runtime by its exported names (including `getGlobalPtr`), so no SDK files are needed to build it. Native hashes come from [alloc8or's RDR3 native DB](https://github.com/alloc8or/rdr3-nativedb-data).

## Troubleshooting

- **No message when Story Mode loads:** check `ScriptHookRDR2.log` in the game folder (update Script Hook if it says your game version isn't supported), then `FastTravelAnywhere.log`.
- **No Fast Travel in the kneel menu:** `FastTravelAnywhere.log` gets a `Kneel menu up.` line each time you kneel, with the game's fast travel state, the unlock check and your horse, then a `Fast Travel added to the kneel menu (...)` line that says which text it shows. No such lines mean the plugin didn't see the kneel menu.
- **Fast Travel is greyed out:** press it to see why; the log has a `Fast Travel: ...` line with the reason too.
- **"doesn't recognise this version of the game":** the globals don't match build 1491.50 (the log says which check failed). The plugin writes nothing in that case.
- **The menu closes when you scroll it:** with W/S or the left stick, Arthur may get up from the kneel, and the game closes the menu when he does. Use the arrow keys, the mouse or the D-pad, or set `LockMovementInMenu=1`. The log has a `The fast travel menu closed without a choice ...` line with everything the game checks; please include it if you report this.
- **The menu flashes open and closes right away:** same log line; it shows which of the game's checks closed it.

## Building from source

Cross-compile from Linux or macOS with zig:

```sh
pip install ziglang
./build.sh                  # -> build/FastTravelAnywhere.asi, build/FastTravelAnywhere.ini, dist/FastTravelAnywhere-<version>.zip
python3 package/package.py  # re-package an existing build without recompiling
```

Or on Windows, from an *x64 Native Tools Command Prompt for VS*:

```bat
build_msvc.bat
```

## Tests

`tests/run_tests.sh` builds a fake `ScriptHookRDR2.dll` and a test host, then runs the real `.asi` under Wine. The fake exports the same names as Script Hook, simulates the natives and script globals the plugin uses, and runs a model of the two game scripts it works with, written from their decompiled code: the kneel (`dynamic_craft_scenario`: its states and its HUD bit, its menu coming up while Arthur is still getting down, Set Up Camp, and its stand-up when you move) and `short_update`'s fast travel module (the requests, the menu, all of its checks, the horse it takes along and the trip). It also models the game's `CAMP` text block: loaded a few frames after a request, unloaded by the camp scripts and the Craft menu when they end, gone after a trip (as players found in the game), and a prompt whose label isn't loaded shows no text. The 185 checks cover:

- The prompt: only while the kneel menu is up, in its group, with the game's text label, button, mode and priority; gone with an interaction target, the camera out, or standing up; Jump held off while kneeling.
- Its timing: up in the same frame as the kneel menu's own buttons, before Arthur is all the way down, and gone with them (Set Up Camp, a campfire's own prompts); 1.0.0's timing when the kneel menu's bit never shows; a press before he's down waits and the menu opens (and stays open) once he is, in the scenario's base or active in it; dropped if he gets back up, presses Set Up Camp, or isn't down 3 seconds later; a press while the `CAMP` text is coming back in waits for it.
- Its text: the `CAMP` text asked for when a kneel starts and let go of afterwards (but not while any of the five camp scripts, the Craft menu or a camp message is using it, never when the game had loaded it, and not left behind when it's still loading); the label only while it's loaded, the same words as plain text otherwise; still there after a trip, after the Craft menu, with the text pulled out from under it, or with a text that never loads (English messages then); a French game, also with the label's words unreadable; and over the whole run, the button never shown without text.
- A whole trip: the camp bit and the request, the menu opening with time slowed, Craft and Set Up Camp blocked under it, picking a place, the music, the stand-up and fade, controls held except pause, the trip only after the screen is black, arriving on the horse, the screen back afterwards (and not faded twice when the game fades in itself), the game's state left idle.
- Backing out, standing up with the menu open, a camp bit the game had set already (left alone), nowhere to go (the game's message).
- The horse: dead, too far, none, the whistling range by bonding level and progress, a saved horse model the game wouldn't accept (too far, as the game counts it), the second horse slot, `RequireHorseNearby=0` (arriving on foot, still needing a living horse), the game's trip flag on the horse cleared afterwards.
- Wanted, missions, not unlocked (and the other way of unlocking), a kneel that isn't a scenario task, game menus, the journal or satchel open (and opened over the menu), the app unable to launch, another fast travel under way, `ShowWhenUnavailable=0`.
- Settings reloaded on the next kneel, `Enabled=0` and back, `PromptText`, `PromptControl` (and an unknown one).
- Movement under the menu (the kneel ends, the menu closes, the log and a tip say why) and `LockMovementInMenu=1`.
- Trouble: a menu that never shows, the player dying right after pressing, with the menu open or during the fade, a trip that never starts (reset like the game's own finish), the trip started early by a camp elsewhere, a trip still loading after 2 minutes, a leftover close request, Red Dead Online, globals that don't match the game build (nothing written), and a game that never answers (request withdrawn, turned off).
- Every native called with the right number of arguments, and no unknown natives.

v1.0.1 passes all 185 checks. Run against 1.0.0, 47 of them fail: the checks for the two problems below, for the other changes in 1.0.1, and the version number.

## Changes

- **1.0.1**: Fast Travel always shows its name. In 1.0.0 it could be missing from the kneel menu (after a trip, for example) while Space still worked, because its text lives in the game's `CAMP` text block, which only the camp scripts load; the plugin now loads it itself. Fast Travel also comes up together with the kneel menu's own buttons instead of up to a second later (a press before Arthur is all the way down opens the menu once he is), and goes away with them (after Set Up Camp, for example). Greyed out while the journal or satchel is open, which would close the game's menu.
- **1.0.0**: First release.

## Credits

- Script Hook RDR2: Alexander Blade
- RDR3 native database: alloc8or
- Decompiled game scripts and native enums: Halen84
- Controls, input contexts and other game data: femga (rdr3_discoveries)
