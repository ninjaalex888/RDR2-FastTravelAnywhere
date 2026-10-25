FAST TRAVEL ANYWHERE  v1.0.0
Red Dead Redemption 2 (PC) - Story Mode
=====================================================================

Hold E to kneel anywhere, and the kneel menu in the bottom-right corner
(Craft, Set Up Camp, Leave) gets a Fast Travel button on Space (A on a
controller), the same button a campfire has. It opens the game's own fast
travel menu: the same list of places, the same trip with your horse, the
same rules as at a campfire.


REQUIREMENTS
------------
* RDR2 on PC (Steam, Rockstar Games Launcher or Epic), current version
  (game build 1491.50).
* Script Hook RDR2 by Alexander Blade:
  http://www.dev-c.com/rdr2/scripthookrdr2/
  Get the latest version. Its zip contains ScriptHookRDR2.dll and
  dinput8.dll; both go in the game folder.


INSTALL
-------
1. Open your game folder (the one with RDR2.exe). On Steam:
   Library > right-click Red Dead Redemption 2 > Manage > Browse local files.
2. Copy FastTravelAnywhere.asi and FastTravelAnywhere.ini into it.
3. Start the game and load Story Mode. A yellow message appears in the
   top-left corner for a few seconds:
       Fast Travel Anywhere 1.0.0 loaded - hold E to kneel, then Space: Fast Travel

To uninstall, delete FastTravelAnywhere.asi, FastTravelAnywhere.ini and
FastTravelAnywhere.log.


USING IT
--------
1. Hold E with nothing around you to kneel. The kneel menu appears in the
   bottom-right corner, now with Fast Travel on Space.
2. Press Space. The game's fast travel menu opens, the same one as at a
   campfire.
3. Pick a place. Arthur stands up, the screen fades out, and you arrive at
   the place on your horse. Back out of the menu to stay where you are.

In the menu, use the arrow keys, the mouse or the D-pad. W/S or the left
stick might make Arthur get up from the kneel, which closes the menu (see
TROUBLESHOOTING).


THE RULES
---------
Fast Travel follows the game's own rules for fast travel at a campfire:

* Fast travel has to be unlocked, as for your camp (for Arthur: the
  lodging upgrade in the camp ledger).
* Your horse has to be alive and within whistling range: it travels with
  you.
* Not while you're wanted, and not during a mission.
* You have to have discovered somewhere to go.

Which places are listed, and which are off-limits (such as New Austin for
Arthur), is decided by the game itself, as at a campfire.

When the button can't be used it's shown greyed out, and pressing it says
why, with the game's own message where it has one ("Your horse is too far
away to fast travel." and so on). The unlock, horse, wanted and mission
rules can each be relaxed in FastTravelAnywhere.ini.


SETTINGS (FastTravelAnywhere.ini)
---------------------------------
Changes take effect the next time you kneel, no restart needed.

Enabled=1              Fast Travel in the kneel menu on/off.
RequireUnlock=1        Fast travel has to be unlocked, as for your camp.
RequireHorseNearby=1   Your horse has to be within whistling range.
                       0 = also when it's too far away: you arrive on foot.
                       A living horse is always needed (the game's menu
                       closes without one).
AllowWhenWanted=0      1 = allow fast travel while wanted.
AllowOnMissions=0      1 = allow fast travel during a mission.
ShowWhenUnavailable=1  Show the button greyed out when it can't be used;
                       pressing it says why. 0 = hide it, as a campfire does.
StandUp=1              Arthur stands up as the screen fades out.
PromptControl=INPUT_CONTEXT_A
                       The button, by the game's control name.
                       INPUT_CONTEXT_A is Space (A on a controller). The
                       kneel menu already uses R, E and F.
PromptText=            Empty = the game's own "Fast Travel" text, in your
                       game's language.
ShowLoadMessage=1      The "loaded" message when Story Mode starts.
DebugLog=0             More detail in FastTravelAnywhere.log.
LockMovementInMenu=0   Experimental. If the menu closes when you scroll it
                       with W/S or the left stick, 1 switches the controls
                       to menu-only while it's open.


TROUBLESHOOTING
---------------
* No message when Story Mode loads: check ScriptHookRDR2.log in the game
  folder (update Script Hook if it says your game version isn't
  supported), then FastTravelAnywhere.log.

* No Fast Travel in the kneel menu: FastTravelAnywhere.log gets a
  "Kneel menu up." line each time you kneel, with the game's fast travel
  state, the unlock check and your horse. No such line means the mod
  didn't see the kneel menu.

* Fast Travel is greyed out: press it to see why. The log has a
  "Fast Travel: ..." line with the reason too.

* "doesn't recognise this version of the game": the mod checks the game's
  internals before it changes anything, and they don't match the version
  it was made for. It stays off and changes nothing.

* The menu closes when you scroll it: with W/S or the left stick, Arthur
  may get up from the kneel, and the game closes the menu when he does.
  Use the arrow keys, the mouse or the D-pad, or set LockMovementInMenu=1.
  The log has a "The fast travel menu closed without a choice ..." line
  with everything the game checks; please include it if you report this.


HOW IT WORKS
------------
A campfire's Fast Travel button doesn't travel by itself: it hands the
request to one of the game's own scripts, which opens the fast travel menu
and makes the trip, while the campfire plays its part (it lets the menu
stay open while you're at the fire, then kicks out the fire, fades the
screen and packs up when you pick a place). This mod plays the campfire's
part from the kneel menu: it hands over the same request, and when you pick
a place, Arthur stands up and the screen fades. The game does the rest.

Before it changes anything, the mod checks that the game's internals match
the version it was made for. If the game doesn't respond, the mod takes the
request back and turns itself off until the game restarts. Idle in Red Dead
Online.


RED DEAD ONLINE
---------------
Story Mode only. Script Hook RDR2 doesn't run in Red Dead Online, and this
mod also stays idle if it detects an online session. Rockstar still bans
for modded files in Online, so remove or disable your mods (including
Script Hook's dinput8.dll) before playing Online.


CREDITS
-------
Script Hook RDR2: Alexander Blade
RDR3 native database: alloc8or
Decompiled game scripts and native enums: Halen84
Controls, input contexts and other game data: femga (rdr3_discoveries)
