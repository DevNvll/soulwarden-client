# The ranged and magic actions of the player

Stage P3.6 (Linear AST-179). The client plays the basic attacks and the abilities of the bow, the fire staff, and the life
staff from the action lists and the animation databases of the data. It sends the shot items of list 2 at the steps of the data,
flies its own picture of each projectile, shows the ground target of an aimed ability, predicts the mana, and shows the heal
numbers. The sources are the research files `~/Games/soulwarden-client/research/p36-backend.md` (the backend contracts) and
`p36-data.md` (the game data and the client), the Soulwarden protocol notes (4.5, 4.27, 4.28, 4.31, 4.46), and the backend sources
(`world/projectiles.cpp`, `world/abilities.cpp`, `server/rep/client_input.cpp`; read only).

The backend has no attack graph for these weapons. It takes a basic shot from the shot item and the attack bits of its press: the
item binds to the newest press of bit 555 at or before its step, comes 30 steps or less after the release, and a heavy shot needs
14 steps of hold (a bow) or 4 (a magic weapon) (backend `Projectiles::launch`). The server flies each player projectile itself
and sends no record of it: a hit gives the damage number 2071 to the shooter; a miss or a wall gives nothing.

## The parts

| Part | Job |
| --- | --- |
| `converter/swc_convert/ranged.py` | The "ranged" entry of the weapon entries of `actions.json` (`tools/convert-player.sh actions`): the actions, the fragments with their timed items, the abilities, and the named conditions; the shared part: the shot rules with their flight, the spells, the mana costs. |
| `Combat/SwcRanged` | Plain C++ (no engine type): the data structs, the evaluator of the conditions of the data, the player of the actions, and the mana prediction (`ranged::Mana`). |
| `Combat/SwcAttackGraph` (`Driver::step_ranged`) | The player of the weapon in the hand at each input step of the session: the inputs of the step, the facts, the shot items, the mana payments, the own cooldowns; the snapshot for the game thread. |
| `Character/SwcRangedView` | The pictures: the aim point of the camera, the own projectile of each shot, the ground target disc, the heal numbers. |
| `Character/SwcPlayerActions` | The fragment of the ranged scope on the FullBody channel (upper-body clips: the legs keep the movement), the aim of the data (the camera and the walk), the turn to the camera, Left Ctrl. |
| `Net/Codec/SwcRepClient` | The shot item of a `ShotRequest` (the row, the charge, the ammunition, the origin rule) to the aim point; the mana fields 2 and 3, message 4289, the heal bit of the owner vitals, message 4298 ([NETWORK.md](NETWORK.md)). |

## The data

`tools/convert-player.sh actions` (`ranged.py`) writes for the tag sets `Bow,LongBow`, `MagicStaff,Fire`, and `MagicStaff,Heal`:

- the actions of their action lists (`playeractions_archery` and `playeractions_ability_bow`; `playeractions_attack` and
  `playeractions_ability_firemagic` or `_lifemagic`) that the crawl reaches from the start actions (Bow_ReadyWeapon,
  Bow_Shoot_HipFire; Attack_Start_Charge_Upperbody), from the starts of the abilities, and each action of the own lists of the
  weapon: the fragment, the activation and the deactivation conditions, and the activities with the condition of their
  IfThenElse branches (SetMannequinTag, ClearActiveAbilityMoveInput, CastSpell, PayManaCost, SetCooldownTimer, ForceWalk,
  CastSpellTargeting, CastSpellRaycast, ChannelSpell, CameraRelativeFacing, ...);
- the fragments: each variant of the SubADB chain (its tags beyond the weapon tags, its record for the clips, its length and loop
  flag, its timed items: the transitions, CAGE-RangedAttack, CAGE-CastSpell, CAGE-Named, CAGE-PayManaCost, CAGE-Damage);
- the abilities: the id and its CRC-32 (the id of the bar), the start actions (with the self-target key), the cooldown, the cost;
- the named conditions: one name space for all action lists (the engine loads them into one controller).

The counts: the bow 32 actions and 25 fragments, the fire staff 22 and 19, the life staff 26 and 30; 77 shot rules, 51 spells, 43
mana costs. Mannequin compares the fragment names with no case (`Attack_Start_Charge_Upperbody` of the action list is
`Attack_Start_Charge_UpperBody` of the databases). A named condition is a condition node with a name.

## The rules of the player

- The installed actions run their activities; one of them owns the fragment scope: the last action that started a fragment. An
  action that loses the scope stays installed to its deactivation condition (Bow_ReadyWeapon to the mark Exit_BowAimShoot of the
  lower fragment: its tags select the variants of the next actions); an action with no deactivation condition ends with its
  fragment or when it loses the scope.
- Each step: the inputs go into the history (BackTicks); the deactivation of the installed actions; the items and the transitions
  of the fragment at its clock ((step - start + 1) / 30 s, the clock of the attack graph); the end of a fragment; the activities at
  the edges of their conditions; the activation of the actions that are not installed, in the order of the data (one new fragment
  action in a step; the enter activities of a new action run at its start). The activities come before the activations: the raycast
  of a confirm is then a fact of the cast action in the same step. With the other order, the cooldown state of the server for the
  ability (the backend starts it at the confirm) came before the next step in a live run and blocked the cast (IfCooldownTimer).
- A transition: an instant window at the step; a buffered window at its end (the rule of the attack graph). NewFragment changes the
  fragment of the action; NewAction starts an action (an ability start, or the next action of the same ability).
- The items: a CAGE-RangedAttack gives a shot item (its attack, its row, its charge, the ammunition of the weapon); a CastSpell of
  a projectile spell gives a shot item with the RangedAttackName of the spell (row 0, charge 0, no ammunition); a spell with a
  ManaCost pays it at the cast; a CAGE-PayManaCost pays its cost.
- The variant of a fragment: the variant with the most tags in the tags of the installed actions (the tags of the enter of a new
  action come first).
- The conditions: each leaf type of the data has a rule of the client (the inputs, the tags, the marks, the cooldowns, the mana, the
  learned nodes, the statuses, the stun and the silence, the encumbered movement, the fragment of the action of the condition, the
  raycast of the ground target). The conditions of the engine: Ability_Startup is its mark, UseUpperBodyAttacks is true, the
  setting javelin.enable-ammoless-weapons is on. No water, no mount, no group, no friend lock: false. `tools/test-ranged.sh`
  fails on a leaf with no rule.
- Rules of this project: an action whose fragment is done and whose deactivation does not hold ends after 2 s (`kStuckTime`); an
  installed action that lost the scope ends after 10 s (`kLostTime`).
- The deactivations of one step read the state at the start of the step, then the actions end (AST-254, 2026-10-22). Before, the
  ends came one at a time: the end of Bow_Lower_FromShoot_Charged at its mark Exit_BowAimShoot stopped the fragment before
  Bow_ReadyWeapon and Bow_AimChargeShoot_SubState read the same mark; they stayed installed and set Aim and AimDownSights again, so
  the aim camera and the aim pose stayed after the release (the right button released in a charged shot; the play report "the
  pose stays when I release the mouse button"). `tools/test-ranged.sh` has the cases (the right button, the left button with no
  aim, both, the right released in the charged shot and pressed again in the lower fragment).

## The input

| Bit | Input | When |
| --- | --- | --- |
| 555, 203, 408 | attack_primary, _virtual, attack_shoot | the press of the left button |
| 315 (bow), 191 (staffs) | aim_shoot_hold, attack_primary_hold | each step while the left button is down (the live bow sends no 191; the backend takes the charge from the press to the release) |
| 567 | attack_primary_release | the release |
| 126 and the other bits of the right button | aim_draw, block, rotate_character_start, ... | stage P3.G: the bits of the bindings of the right button as the retail client sends them for each weapon (the press, each step while it is down, the release; [ACTIONS.md](ACTIONS.md) "The block") |
| 430 | self_target | each step while Left Ctrl is down (also the walk of the default profile) |
| the keys of the bar | ability1 to 3 | stage P3.5 ([ACTIONS.md](ACTIONS.md)); the press gives the ability input of the data (IfActiveAbilityMoveName) |

## The shot item

- The attack, the row, the charge, and the ammunition of the data (CRC-32 values): the bow `basic`/`bowattack_hipshot`,
  `basic`/`bowattack1`, `basic_heavy`/`bowattack2`/100, the ammunition DefaultArrow (the fallback of the empty slot); the fire staff
  `basic`, `charged`; the life staff `primary`, `heavy` (row 0, charge 0, no ammunition).
- The origin: 1.0 m along the look and 1.46 m above the feet (the live bow item, protocol notes 4.27). The direction: to the aim
  point of the camera (the camera ray to its first hit on the channel Pawn, or to 100 m). No receiver: the server flies the shot.

The steps of the data (`tools/test-ranged.sh`) and the live values of the notes:

| Shot | Step of the client | Live (protocol notes) |
| --- | --- | --- |
| Hip shot (a click) | 0.4 s of Bow_Shoot_HipFire: 10 steps after the release | 10 steps after the release |
| Aimed light shot | the step of the release | the step of the release |
| Charged shot | the release after the mark SetChargedAim (0.5 s of the draw); 14 steps or more | 16 steps gave a charged shot |
| Fire staff, light | 0.1 s of Magic_Staff_Cast_Primary_C1: 3 steps after the press of a click | 5 or 6 steps after the press |
| Fire staff, heavy | 0.6667 s of the charge + 0.1 s | (the harness gave none) |
| Beacon | 0.1667 s: 4 steps after the key | 4 steps |
| Explosive Arrow | 13 steps after the key | 13 steps |
| Rapid Shot | 9, 25, and 52 steps after the key | 9, 25, 52 |

## The own projectile

`SwcRangedView` flies a picture of each shot with the flight of its shot rule (projectile-data.json): the speed and the
acceleration of the slice after the acceleration delay of the attack (`p += (v + a dt / 2) dt`, `v += a dt`), to the range of the
attack or the life of the slice. It ends at the first hit of a sphere of its radius on the channel Pawn (the world and the bodies
of the creatures); an arrow stays there 8 s (the stuck life of basic_arrow). The bow: 100 m/s, gravity after 5 m, 100 m; the fire
ball and the life staff shots: 100 m/s, no gravity, 0.5 s. The look is the basic shapes of the engine.

## The ground target and the confirm

The data shows the ground target while CastSpellTargeting is on (the aims of Sacred Ground, Pillar of Fire, Meteor Shower, Rain of
Arrows). `SwcRangedView` puts a disc of the radius of the spell at the ground of the camera ray in its cast distance (beyond it: the
ground below the point at the cast distance). The confirm of the data (the left button or the key again) runs CastSpellRaycast: the
result is the disc on a ground (ActionConditionSpellSpawnLocationState RaycastPassed starts the cast action). The server gets only
the bits of the confirm (555 or 430): it puts Sacred Ground at the caster (no ground point is known on the wire).

## The mana

`ranged::Mana`: the regeneration of the data (ManaBaseMax 100, 2 a second, the delay 0 s, 2 s after a payment to 0; times the rate
factor of message 4289), the payments of the actions (the costs of ability-data.json). Message 4289 sets the prediction at once (the
retail client copies the four values of the server, backend `mana_messages.h`); the mana state of the server corrects it after 2 s
of a difference of more than 5 with no payment. The HUD shows the prediction (`USwcBackendSubsystem::GetLocalPlayer`). A press
with too little mana does not start (IfManaCost of the data).

## The HUD

`USwcBackendSubsystem::GetLocalPlayer` gives the HUD the predictions of the client: the stamina (stage P2.5), the mana, and the
selected weapon. The backend switches the weapon at the same input step as the client, but its paperdoll state keeps the first
weapon slot with an item as the active one (backend `paperdoll_state.cpp`: no draw and no sheathe yet). Thus the active map of the
HUD is the slot of the weapon switch of the attack driver (with its off hand), and the sheathe map has the weapon in the hand out of
its sheath while the character holds it drawn. Before this rule the HUD kept the bar of slot 24 selected after a switch to slot 25
(the life staff abilities were in the cooldown of the server, but the bow bar showed as selected).

## The heal numbers

The owner vitals state with bit 1 of `healthChangeFlags` gives a green "+N" (N = the new health less the health of the client;
UIStyle COLOR_NAMEPLATE_HEALING, RGB 48 229 171) 1.6 m above the feet for 2 s. Message 4298 (a heal of a friend by the owner) goes
to the log (the client shows no other player yet). From stage WF7 the UI shows the heal numbers in the original damage number
canvas and calls `USwcRangedView::SetUiHealNumbers(true)`: the simple numbers go off.

## The tests

`tools/test-ranged.sh` builds and runs three programs:

- `tests/ranged/ranged_tests.cpp`: the player with the programs of `actions.json`: the bow (the hip shot, the aim, the light and the
  charged shots), the staffs (the light and the heavy casts, the next charge of a long hold), the abilities (the Beacon, its
  cooldown and its mana gates, Sacred Ground with its ground target and its confirm, the self cast of Divine Embrace, Explosive
  Arrow, Rapid Shot, Penetrating Shot, Pillar of Fire), the cast of Sacred Ground before the cooldown state of the server, the mana,
  and a rule for each leaf of the conditions (67 checks).
- `tests/net/ranged_state_tests.cpp`: the mana state, message 4289, the owner vitals with the heal numbers (two Beacon pulses of
  55 after a silent gain), and message 4298 of the encoders of the backend into the read path of the client (12 checks).
- `tests/ranged/parity_tests.cpp`: the parity of the presses: the attack driver of the client makes the input entries of the hip,
  light, and charged shots of the bow and the light and heavy casts of the two staffs; the world of the backend (its core library,
  built once outside the Soulwarden repository in `SWC_WORK/build/backend-core`) takes them through its input reader and its shot
  rules with the projectile tables of the backend copy: each shot is accepted and hits a wolf 20 m away; a control item (a charged
  shot after 5 steps of hold) is refused (27 checks).

## The exit check (development runs, 2026-10-18)

`Mysticwarden` (the bow in slot 24; the fire staff or the life staff in slot 25), three runs of `tools/creature-check.sh` with the
scripts `tests/drive/p36-bow.txt`, `p36-wall.txt`, and `p36-life.txt` (their commands are in the scripts). Evidence in
`~/Games/soulwarden-client/evidence/P3.6/`: `p36-bow.mp4`, `p36-wall.mp4`, `p36-life.mp4`, a sheet of each one
(`p36-bow-sheet.png`, ...), and the logs.

| Run | The client | The server (backend log) |
| --- | --- | --- |
| Bow, open ground, a wolf 26 m east | the hip shot 10 steps after the release; the aim (AimCamera); the light shot at the release; the charged shot after 1 s; 6 damage numbers | 3 shots accepted, 3 hits: 218 (a critical headshot), 363, 545 |
| Fire staff, the same wolf | the light cast, the heavy cast (0.6667 s of the charge) | 2 shots accepted, 2 hits: 150, 210 |
| Bow, 20 m south of the tavern of Oxboro, a wolf 35 m north behind it | the own arrows stop at the tavern after 9.4 and 9.5 m; no damage number | 2 shots accepted; each projectile ends after 9.5 m at 8915.39 4248.00: "a wall or the ground stopped it" |
| Life staff, a wolf 26 m east | the light and the heavy casts; Sacred Ground: the aim with its ground disc, the confirm (a click), the cast at 0.333 s; 13 heal numbers (+35 each second, +265); Divine Embrace on the caster (Left Ctrl); the Beacon 4 steps after its key | 2 shots accepted; Sacred Ground aimed at the key and cast at the confirm, its area at the caster, 35.4 each second; the Beacon (cost 16) |

- The HUD selects the bar of the weapon in the hand after a switch (the HUD rule above).
- The first two places of the wall check failed: a place in a log fort (the camera in the structure) and a place in a corruption
  ground (the client fell 6 m below the position of the server; the server refused the shots: "the origin is not near the
  character"). The tavern in the street of the town has the same wall in the client and in the server.

## Differences and open points

- The backend has no fire staff ability, no channel, and no Splash of Light, Orb of Protection, Light's Embrace, Evade Shot, Poison
  Shot, or Rain of Arrows: the client plays them from the data, the server refuses the press (no cooldown state, no effect).
- No wire field for a ground point or a friend target is known: the server puts Sacred Ground at the caster and heals the nearest
  friend with Divine Embrace. The friend lock of the life staff (ForceCameraLock) is not in the client.
- A staff button that stays down after a heavy cast starts the next charge (the data): its shot item binds to the same press, and
  the backend refuses it ("the press has a projectile"). A live check of the retail input of a long hold is open.
- The channel ring and the cast bar of the HUD (`AbilityChannel` of Vitals2024), the reticle data (`Hud.LocalPlayer.ReticleToShow`),
  and the original canvas of the damage and heal numbers are not fed.
- The accuracy spread of the bow, the use of extraSpeed and timeForMaxSpeed of the arrow slice, and the native rules of
  CastSpellRaycast (CastHeight, the obstruction test) are not known.
- A dodge does not end a ranged fragment yet (the data has CanDodgeCancel windows).
- The paperdoll state of the backend does not follow a weapon switch (the HUD rule above covers it on the client).
- The self cast of Divine Embrace: the backend casts it at the key with bit 430 but heals nobody ("no target that the data
  permits", no payment).
- The side-by-side check with the retail client (stage P3.G, set 6 `player-bow`): the shots and the arrows match (18.3 / 17.9 m in
  188 / 190 ms, a hit; the shot times within 0.1 s); the aim walk 1.59 / 1.55 m/s. The aim stance of the bow (the tags Aim and
  ChargedAim of the ranged player: the stance variants Idle and Strafe `Bow+Aim`, `Bow+ChargedAim` of `player_bow.adb`) came with
  stage P3.G. The opinion of the user is open.
