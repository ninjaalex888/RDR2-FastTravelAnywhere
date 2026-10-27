FAST TRAVEL ANYWHERE  v1.2.1
Red Dead Redemption 2 (PC) - Story Mode
=====================================================================

Hold E to kneel anywhere, and the kneel menu in the bottom-right corner
(Craft, Set Up Camp, Leave) gets a Fast Travel button on Space (A on a
controller), the same button a campfire has. Where the game won't kneel at
all (in Saint Denis, say), keep holding E for a second and the mod has
Arthur kneel.

* With a waypoint on your map, it takes you straight there: the screen
  fades out, and a few seconds later you're at your waypoint (on the road
  nearest it) in the saddle, with the clock moved on by the time the ride
  would have taken.
* Without one, it opens the game's own fast travel menu: the same list of
  places, the same trip with your horse, the same rules as at a campfire.

And Set Up Camp (E) is there wherever you kneel, towns included. Where the
game leaves it out of the kneel menu, the mod puts it in, and the game makes
camp at the nearest of its own campsites: from a town, just outside it.


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
     Fast Travel Anywhere 1.2.1 loaded - hold E to kneel: Space to
     Fast Travel, E to Set Up Camp anywhere

To uninstall, delete FastTravelAnywhere.asi, FastTravelAnywhere.ini and
FastTravelAnywhere.log.


KNEELING
--------
Hold E with nothing around you, and Arthur kneels: the kneel menu appears
in the bottom-right corner. In some places the game doesn't kneel at all,
Saint Denis among them. There, keep holding E: after a second the mod has
Arthur kneel, the way he does to craft from the satchel, and the kneel menu
comes up as anywhere else. It leaves E alone when it's doing something
else: mounting, looting, picking something up, grabbing someone,
surrendering while wanted, or in a shop. KneelAnywhere=0 turns this off.


USING IT: TO YOUR WAYPOINT
--------------------------
1. Set a waypoint on the map, as you would to ride somewhere.
2. Hold E with nothing around you to kneel. The kneel menu appears in the
   bottom-right corner, now with Fast Travel on Space.
3. Press Space. Arthur stands up, the screen fades out, and a few seconds
   later (while the place loads) you're there, in the saddle, facing the
   waypoint.

Where you arrive:
* A waypoint near a road: on the road nearest it, where the GPS line on
  your map comes closest to it.
* Off the roads: at the waypoint itself, on the ground there.
* Out in a lake or a river: on the nearest shore, or else on the road
  nearest the waypoint.
* Somewhere with no ground to stand on (the game finds none as the place
  loads): back where you started, with a message saying so. Try a waypoint
  on or near a road.

Your horse comes along when it's within whistling range (and your second
horse, if it's near you too); otherwise you go on foot. The clock moves on
by the time the ride would have taken: the length of the GPS route at a
steady gallop (a game minute for every 18 m), or at a walk on foot (8 m),
so a ride across the map takes a few game hours. AdvanceClock=0 keeps the
time of day.

The fast travel rules apply (see THE RULES): fast travel unlocked, your
horse within whistling range, not wanted, not on a mission. Space opens the
menu instead when:
* the waypoint is closer than 60 m, or you're already at the road nearest
  it after a trip there;
* you're Arthur and the waypoint is somewhere the game keeps him out: New
  Austin, where the game's sniper shoots him, or Blackwater, which the
  game's fast travel leaves out for him (a message says so);
* WaypointTravel=0.


USING IT: TO A PLACE IN THE MENU
--------------------------------
With no waypoint set:
1. Kneel as above.
2. Press Space. The game's fast travel menu opens, the same one as at a
   campfire. You can press it as soon as it appears: if Arthur is still
   getting down, the menu opens the moment he's down.
3. Pick a place. Arthur stands up, the screen fades out, and you arrive at
   the place on your horse. Back out of the menu to stay where you are.

In the menu, use the arrow keys, the mouse or the D-pad. W/S or the left
stick might make Arthur get up from the kneel, which closes the menu (see
TROUBLESHOOTING).


SET UP CAMP ANYWHERE
--------------------
Out in the wilderness the kneel menu has its own Set Up Camp, and the mod
leaves it alone. In a town, near train tracks or next to someone with a
mission for you, the game leaves it out; there the mod adds it, on the same
button (E), with the game's own text. Press it and the game does what its
own button does: Arthur gets up, pitches camp, the screen fades, and you're
at your camp, set up at the nearest of the game's campsites (from a town,
just outside it). A full camp when your horse is within whistling range, a
small one when it isn't, as the game decides.


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
Arthur), is decided by the game itself, as at a campfire. A trip to your
waypoint keeps to the same: as Arthur, a waypoint in New Austin or
Blackwater opens the menu instead.

When the button can't be used it's shown greyed out, and pressing it says
why, with the game's own message where it has one ("Your horse is too far
away to fast travel." and so on). The unlock, horse, wanted and mission
rules can each be relaxed in FastTravelAnywhere.ini.

Set Up Camp keeps all of the game's camping rules except the ones about the
place: you need a camp kit (one the game hasn't disabled for the moment), and
it isn't added while you're wanted or have just committed a crime, in combat,
on a mission (unless the mission lets you camp), indoors, in a gang hideout,
at the gang's camp, in Guarma, in an area under lockdown, on a vehicle, boat
or train, during a random encounter, an ambush or a minigame, while busy with
your horse, or with a camp already up. If the game finds no
campsite to use, it says so itself ("Cannot create camp in this location.").


SETTINGS (FastTravelAnywhere.ini)
---------------------------------
Changes take effect within a couple of seconds, no restart needed.

Enabled=1              Fast Travel in the kneel menu on/off.
KneelAnywhere=1        Where the game won't kneel (Saint Denis, say),
                       holding E for a second has Arthur kneel anyway.
                       0 = only where the game kneels by itself.
WaypointTravel=1       With a waypoint on your map, Fast Travel takes you
                       straight there (to the road nearest it, with your
                       horse). 0 = always the game's fast travel menu.
AdvanceClock=1         On a trip to your waypoint, the clock moves on by
                       the time the ride would have taken. 0 = you arrive
                       at the same time of day.
SetUpCampAnywhere=1    Set Up Camp in the kneel menu wherever you kneel.
                       0 = only where the game offers it itself.
RequireUnlock=1        Fast travel has to be unlocked, as for your camp.
RequireHorseNearby=1   Your horse has to be within whistling range.
                       0 = also when it's too far away: you arrive on foot.
                       The game's menu always needs a living horse (it
                       closes without one); a trip to your waypoint
                       doesn't.
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
* Holding E doesn't kneel: after a second of holding it, the log has a
  "Kneel: ..." line: "the plugin has him kneel" (then "the kneel menu came
  up"), or why it left E to the game ("a game script has the kneel button
  turned off here", "busy with something else", "wanted", "on a mission"
  and so on). No such line means the mod didn't see E held.

* Space opens the menu although a waypoint is set: the log's
  "Fast Travel: available (...)" line says why: no waypoint set, your
  waypoint is right here (closer than 60 m), you're at the road nearest
  your waypoint (after a trip there), your waypoint is in New Austin or in
  Blackwater (as Arthur), or the menu because WaypointTravel=0.

* A trip to your waypoint went wrong: the log follows each step
  ("Fast Travel pressed: travelling to your waypoint ...", "screen black;
  the horse comes along", "the ground at the waypoint is at ..." off the
  roads, "the waypoint is in deep water ..." in a lake, "arrived at ...;
  on the horse", "the clock moved on ..." and "done"). Please include those
  lines if you report a problem.

* No message when Story Mode loads: check ScriptHookRDR2.log in the game
  folder (update Script Hook if it says your game version isn't
  supported), then FastTravelAnywhere.log.

* No Fast Travel in the kneel menu: FastTravelAnywhere.log gets a
  "Kneel menu up." line each time you kneel, with the game's fast travel
  state, the unlock check and your horse, then a "Fast Travel added to the
  kneel menu (...)" line that says which text it shows. No such lines mean
  the mod didn't see the kneel menu.

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

* No Set Up Camp in a town: the log has a "Set Up Camp: ..." line each
  time you kneel that says why ("not added: wanted", "not added: no camp
  kit" and so on), or "the kneel menu has its own here" where the game
  offers it itself.

* Set Up Camp pressed, but no camp: the log follows the game's camp script
  ("Set Up Camp pressed: ...", "The game's camp script is running.", then
  how it went). "it found no campsite to use here" means the game had no
  campsite for that part of the map; it says "Cannot create camp in this
  location." itself.


HOW IT WORKS
------------
A campfire's Fast Travel button doesn't travel by itself: it hands the
request to one of the game's own scripts, which opens the fast travel menu
and makes the trip, while the campfire plays its part (it lets the menu
stay open while you're at the fire, then kicks out the fire, fades the
screen and packs up when you pick a place). This mod plays the campfire's
part from the kneel menu: it hands over the same request, and when you pick
a place, Arthur stands up and the screen fades. The game does the rest.

To your waypoint: the game has no trip to a waypoint of its own, so the mod
makes one the way the game's own trips are made: it fades the screen out,
holds you and your horses at the place while it loads (unable to take
damage meanwhile), checks the ground there, puts your horse down on it
with you in the saddle, moves the clock on and fades the screen back in.
The place is the point of the GPS route nearest the waypoint; off the
roads, the waypoint itself, with its ground found as the place loads; out
of deep water, the nearest shore. If the player dies on the way, the
game's death screen takes over; a save loaded on the way takes the trip
with it.

Set Up Camp: the kneel menu adds its own Set Up Camp only where the game
allows a camp. The mod looks at the kneel menu's buttons in the game's list
of prompts, and only when they're there without a Set Up Camp does it add
one, so there are never two. A press starts the game's camp script with the
same settings the kneel menu's own button gives it; the game picks the
campsite and does the rest.

Before it changes anything, the mod checks that the game's internals match
the version it was made for. If the game doesn't respond, the mod takes the
request back and turns itself off until the game restarts. Idle in Red Dead
Online.


CHANGES
-------
1.2.1  Kneel anywhere. Where holding E doesn't kneel (in Saint Denis, say),
       keep holding it for a second and the mod has Arthur kneel, as he
       does to craft from the satchel, so the kneel menu with Fast Travel
       and Set Up Camp is there too. New setting KneelAnywhere (on by
       default).
1.2.0  Fast travel to your waypoint. With a waypoint on your map, Fast
       Travel in the kneel menu takes you straight there, to the road
       nearest it, on your horse, with the clock moved on by the ride's
       length. Without a waypoint it opens the game's fast travel menu as
       before. New settings WaypointTravel and AdvanceClock (both on by
       default).
1.1.0  Set Up Camp anywhere. Where the game leaves Set Up Camp out of the
       kneel menu (in a town, say), the mod adds it on E, and the game makes
       camp at the nearest of its campsites, outside town. Its other camping
       rules still apply. New setting SetUpCampAnywhere (on by default).
       Settings changes now apply within a couple of seconds, without
       kneeling again.
1.0.1  Fast Travel always shows its name now. It could be missing from the
       kneel menu, after a trip for example, while Space still worked: its
       text lives in a part of the game's text that only the camp scripts
       load, and the mod now loads it itself. Fast Travel also comes up
       together with the kneel menu's own buttons instead of up to a
       second later, and goes away with them (after Set Up Camp, for
       example). Greyed out while the journal or satchel is open.
1.0.0  First release.


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
