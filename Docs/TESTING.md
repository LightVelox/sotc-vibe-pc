# Automated testing

## Scenario harness (`Tools/scenario.py`)

Tests whole-game situations without playing: it starts the port from a save state, calls the game's own
functions to warp, kill colossi and so on, and checks for crashes, freezes and stalls.

```bash
python Tools/scenario.py colossi build/scenario/run1 --jobs 2
python Tools/scenario.py colossi build/scenario/run2 --only 2,5,13 --save-states build/saved_states
python Tools/scenario.py script build/scenario/x 60:warp:5 3000:kill --end 7000
```

`colossi` runs one fight per colossus: colossus 1 from `build/saved_states/colossus1_prefight.state`, the others from
the same state followed by the Time Attack warp (`bossStatTimeAttackJumpSekiban(n - 1)`: marks the earlier colossi
dead, sets Wander's health and grip for that fight and makes the game-flow script jump to the arena with its intro
cutscene). After `--fight` fields (default 3000) the colossus is killed, and the run ends 300 fields after the game
marks it dead (save prompt), or `--after` fields at most. `--save-states DIR` writes `colossusNN_fight.state` just
before each kill, so later tests of one fight load in seconds (`--state` or `script --state`).

Each run uses a private copy of `sotc.exe` with a copy of the memory cards (`<out>/_exeN`), so the real card is never
written. Results: `<out>/report.md` and `report.json` (pass/fail and why, fields/s, drawn frames/s, audio underruns,
longest game-logic stall), `<out>/cNN/` (stdout, timeline, captures every `--every` fields) and `cNN_sheet.png`
contact sheets. With `--jobs 2` the speed numbers are lower than in a single run.

Checks per run: process crash or missing guest branch target, no field progress for 60 s (stall), the game's
scheduler frame counter (`0x1DC9EC`) not advancing for more than 300 fields while VBlanks continue (the game froze),
more than half of the fight captures black, no colossus object at kill time, and the colossus's dead flag not set
at the end.

`script` runs arbitrary steps (`<field>:<op>:<args>`, fields relative to the loaded state): `warp:N`, `kill`,
`call:<function name or hex address>[:a0=..][:f12=..]` (register values in hex/decimal, `$v0` = result of the
previous call), `poke:<hex addr>:<value>`, `pokef`, `pokeb`, `peek:<hex addr>[:bytes]`, `save:<path>`, `quit`.
Function names come from `Analysis/ADDRESS_MAP.generated.csv` (the game's own symbol names).

## Debug script in the port (`SOTC_DEBUG_SCRIPT`)

The harness drives `Port/src/sotc/debug_script.cpp`. `SOTC_DEBUG_SCRIPT` is a `;`-separated list of
`<field>:<op>:<args>` with numeric addresses (`+N` = relative to the first time the hook runs). Commands run at the
entry of `SOTC_DEBUG_HOOK` (default `0x1c35b0` `doit_letter_manager`, which runs every game frame; long-running
loops such as `st_gameflow` never re-enter after a state load and do not work as hooks). Calls are queued as guest invocations on the current thread (`EeScheduler::invokeCurrentSequence`),
with their own stack, and the hooked function then continues normally; results are logged as `[debug-script]`.

Useful game functions and data:

| What | Where |
|---|---|
| Time Attack warp to colossus n+1 | `bossStatTimeAttackJumpSekiban(n)` `0x135B4F8` |
| Current colossus object | `GAMEUTIL_get_boss_obj()` `0x13546F8` |
| Kill a colossus now (life 0, dead, signal script) | `0x1381408(obj)` (queued by `SCRBossDebugDeath(obj)` `0x13813C8` on the next motion change) |
| Colossus table | `*(0x157E318 + 0x10)`, 16 entries of 0xEC bytes; `+0x18` flag id, `+0xBC`/`+0xC0` Wander's max health/grip |
| Game flags / game work | bit array at `0x12E5910` (`_gameFlagChk`), words at `0x12E5960` (`_gameWorkChk`); a colossus is dead when its flag bit is clear |
| Scheduler frame counter | `0x1DC9EC`; VBlank counter `0x1DC9D8` |

These addresses are for SCES-53326 v1.00 with the modules at their normal load addresses (the runtime relocates
constants in code; `Port/generated` comments show the unrelocated values).
