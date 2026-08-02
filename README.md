# FTL Speed Mod

**ftlspeed** for short. A speed control mod for **FTL: Faster Than Light 1.6.14** (32-bit). It
runs game time at a multiplier you choose, adjustable live with hotkeys while you play.

It does not patch the game, modify `ftl.dat`, touch save files, or read or write game memory. It
changes only what time the game thinks it is, by hooking the clock functions FTL imports:
`QueryPerformanceCounter`, `GetTickCount`, `timeGetTime` and `Sleep`. Everything that follows
the clock - combat, jumps, oxygen, fires, animations - scales together, so the game stays
internally consistent at any speed.

**Warning**: The game logic may not be able to keep up at high multipliers!

## Install

Two files in your FTL folder, next to `FTLGame.exe`:

```
dbghelp.dll
speed.toml
```

That is the whole mod - two files, nothing else. No injector, no launcher, no elevation, no
Slipstream, and `ftl.dat` is untouched. Start FTL however you normally do - Steam included.

## Uninstall

Delete `dbghelp.dll` from the game folder. The game goes back to loading the system one.

## Hotkeys

Defaults, all rebindable in `speed.toml`:

| key | action |
| --- | --- |
| hold **`** | temporary turbo - multiplies the current speed by `turbo_speed` |
| **]** | next preset (faster) |
| **[** | previous preset (slower) |
| **\\** | toggle between 1.0x and your last speed |
| **Ins** | show / hide the overlay |

Hotkeys only fire while FTL has focus.

`]` and `[` jump to the next preset **past** the current speed, so after typing 2.7 into the
overlay, `]` gives 3.0 and `[` gives 2.0.

Picking your own is a minefield, because FTL binds **every letter a-z**, `1-7`, F1-F8, Space,
Enter, `-`, `=`, `/` and Left Ctrl, and reserves **Shift** as its depower modifier (Shift+1
unpowers weapon 1). What is actually free: `` ` `` `[` `]` `\` `;` `'` `,` `.` `8` `9` `0`, Tab,
the numpad, the navigation cluster, and F9-F11. Avoid **F12** (Steam screenshot), **F10** and
**Alt** (both open the window menu), and Right Alt (AltGr on international layouts).

Rebind with a single character or a key name:

```toml
turbo_key  = "`"
faster_key = "]"
slower_key = "["
toggle_key = "\\"
```

Names accepted: `PgUp` `PgDn` `Home` `End` `Ins` `Del` `Tab` `Space` `Backspace` `CapsLock`
`Up` `Down` `Left` `Right` `LAlt` `RAlt` `LCtrl` `RCtrl` `LShift` `RShift` `F1`-`F12`
`Numpad+` `Numpad-` `Numpad*` `Numpad/`. Single characters resolve through your active keyboard
layout, so non-US keyboards get the right physical key.

## speed.toml

Read once at game start.

| key | meaning |
| --- | --- |
| `speed` | multiplier applied at launch (0.1 - 20.0) |
| `turbo_speed` | how much holding `turbo_key` multiplies the current speed |
| `show_in_title` | append `[2.00x]` to the window title |
| `presets` | list that `faster_key` / `slower_key` step through |
| `overlay` | overlay visible at launch |
| `overlay_scale` | `0` sizes it from your resolution, or set `1`-`4` yourself |
| `turbo_key` `faster_key` `slower_key` `toggle_key` `overlay_key` | see Hotkeys above |

## Overlay

**Ins** shows a small panel drawn inside FTL's own frame — not a separate window, so it survives
fullscreen and shows up in screenshots. It reads out the current speed, the boosted value while
turbo is held, and the active keybinds.

The `v` button drops down a row with `-` / `+` and a field for typing an exact speed: click the
field, type a number, **Enter** to apply or **Esc** to cancel. While that field is focused the
game does not see your keystrokes, so digits will not fire weapons.

Drag the panel by its header, resize with the grip in the bottom-right corner. Where you leave it
is written to **`lastwindowpos.toml`** beside `dbghelp.dll` and restored next launch — delete
that file to reset to the default position. Clicks that land on the panel are kept from the game,
so pressing a button never also orders a crew move behind it; clicks anywhere else pass straight
through and the game stays fully playable with the overlay open.

## VSync and the frame limiter

Speed is always correct, but **smoothness** depends on your video settings.

Game logic advances with the scaled clock, so at 4x each rendered frame covers 4x as much game
time. `Sleep` is scaled down to compensate, which lets FTL's own frame limiter render
proportionally more real frames and keeps motion smooth.

VSync is the exception: it blocks inside the graphics driver, not in `Sleep`, so nothing here can
see it. With VSync on at 60 Hz you get 60 real FPS no matter what, which at 4x is only 15 frames
per game-second. **Turn VSync off for multipliers above ~2x.**

## How it auto-loads

Windows searches the executable's own directory before `System32` when a program loads a DLL by
name. FTL imports `dbghelp.dll` and only ever calls it from its crash handler, so a `dbghelp.dll`
sitting next to `FTLGame.exe` gets loaded first and nothing in normal play notices.

Our `dbghelp.dll` is a *proxy*: it forwards all 268 of its exports to the real
`%SystemRoot%\SysWOW64\dbghelp.dll` - identical names, identical ordinals - so the game behaves
exactly as before. On load it also installs the clock hooks. Only `FTLGame.exe`'s import table is
patched, so `bass.dll`, `steam_api.dll` and the Steam overlay keep the real clock and audio does
not change pitch.

The overlay rides the same mechanism: `SwapBuffers` is hooked in that same import table, and the
panel is drawn with OpenGL into FTL's back buffer just before the frame is presented. GL entry
points are resolved at runtime, and if anything is missing the overlay disables itself and the
hook becomes a straight pass-through - the game never breaks because the overlay could not draw.

(This is the same mechanism Hyperspace uses, via `xinput1_4.dll`.)

## Build

Needs VS 2022 Build Tools (C++ x86 toolset) and Python 3 on PATH.

```
build.bat     compiles bin\dbghelp.dll and ftlspeed-dbg.exe
deploy.bat    copies dbghelp.dll + speed.toml into the game folder
```

`deploy.bat` will not overwrite an existing `speed.toml`, and refuses to run while FTL is open
(Windows locks a loaded DLL). Everything is x86 and `/MT`, so no MSVC runtime DLLs are needed
beside the MinGW-built game.

## ftlspeed-dbg.exe

A development tool. It is **not** part of the mod and `deploy.bat` never copies it into the game
folder - it stays here in `ftlspeed\`. A plain named-pipe client; it injects nothing and needs no
privileges. FTL must be running.

```
ftlspeed-dbg --speed 3.5     set the multiplier and exit
ftlspeed-dbg                 interactive prompt
```

At the prompt: a bare number sets the speed, `+` faster, `-` slower, `t` toggle, `r` reset,
`stats` shows hook counters, `q` quits.

## Compatibility

Tested against vanilla FTL 1.6.14 only. **Untested with Hyperspace / Multiverse** - those
require downgrading to 1.6.9 and load their own DLL into the same process.

## Note on Windows Defender

A local `dbghelp.dll` beside a game executable is a textbook DLL-sideloading pattern, and
Defender (or SmartScreen) may quarantine it on sight. If that happens, either exclude the game
folder, or regenerate the proxy against a different import - `winmm.dll` is the usual second
choice for FTL - by pointing `tools\gen_proxy.py` at `winmm.dll`, rebuilding, and copying
`bin\winmm.dll` to the game root instead.
