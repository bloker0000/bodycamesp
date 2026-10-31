# bodycam esp

visuals overlay for bodycam (steam app 2406770, ue 5.5.4)

internal dll, draws with the engine's own ucanvas from a hook on `AHUD::PostRender`

- boxes, skeleton, head, health, names, weapons, distance, tracers, look direction, offscreen arrows
- chams through walls, or only the parts that are actually in view
- zombie mode items, radar, target counter
- crosshair that sits where the bullet lands
- lens, vignette, grain and motion blur removal, brightness

every overlay stays lined up with the bodycam lens, and inside scopes too

## build

`build.bat`, needs visual studio with the c++ tools. built with cl 19.51, anything that does c++20 should work

gives you `build\bcesp.dll` and `build\inject.exe`

## run

`build\inject.exe` with the game running

settings save to `bcesp.ini` next to the dll. the log is there too, `bcesp_live_<tick>.log`, with a perf line every 10s

## keys

insert menu, delete visuals, home chams, page down radar, end unload. all rebindable in the menu

ctrl+f12 writes calibration captures next to the dll

## layout

- `src/ue` sig scans, fname decode, object array, reflection lookups, processevent
- `src/sdk` every class, function and offset the features use, resolved by name, plus typed call wrappers
- `src/tex` procedural textures and a tiny png encoder
- `src/draw` canvas renderer
- `src/config` settings and the ini
- `src/input` key snapshots, wndproc subclass, raw input cursor
- `src/menu` immediate mode gui
- `src/esp` targets: view, lens, scope mapping, classification, health, team, bones, bounds
- `src/visuals` everything drawn per target plus world items, radar, crosshair, counter
- `src/chams` scene capture chams, visible parts only, lens calibration
- `src/view` lens removal and the post process volume
- `src/overlay` keeps the menu above the game's umg
- `src/app` frame loop, hotkeys, fault guard, perf log
- `src/main` init thread, postrender hook, unload
- `injector/inject.cpp` unloads a loaded copy, then injects a fresh one
