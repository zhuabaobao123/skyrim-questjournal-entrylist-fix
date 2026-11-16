# Quest Journal Overhaul - entryList fix

A drop-in replacement `QuestJournalOverhaul.dll` for
[Quest Journal Overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/141295) that fixes the
crash which happens after opening and closing the quest log a few times.

Download: [Nexus Mods 199346](https://www.nexusmods.com/skyrimspecialedition/mods/199346)

Nothing else changes. The original mod's SWFs and JSON still drive the interface, so skins
(including the Untarnished UI reskin) keep working, and no plugin or record is touched.

## The bug

`EventProcessor::ProcessEvent` kept the journal's quest list in a file-level `RE::GFxValue`:

```cpp
RE::GFxValue entryList;                          // file scope
...
titleList.GetMember("entryList", &entryList);    // tree owned by the JournalMenu movie
...
configArgs[0] = entryList;                       // handed to the QuestMenu movie
menu->uiMovie->Invoke("_root.QuestJournal_mc.onQuestsDataComplete", nullptr, configArgs.data(),
                      configArgs.size());
```

`entryList` is not data. It is a tree of ActionScript objects that belongs to the JournalMenu's
Scaleform runtime. On the journal hotkey path the mod closes JournalMenu immediately
(`QJO_EndPage`), so the owning movie - and every object in that tree - is destroyed before the
QuestMenu movie consumes the value. Scaleform then drops references into freed runtime memory:

```
SkyrimSE.exe+0EC825x   mov rax,[rdx+rax*8]     rdx = 0
GASValue::DropRefs+0x10B
GFxMovieRoot::InvokeFmt
QuestJournalOverhaul.dll+001D745               return address of the Invoke above
```

It takes a few open/close cycles for the timing to line up, which is why it looks random and why
the pause menu entry point (which leaves JournalMenu alive) usually survives.

## The fix

`src/QuestDataSnapshot.{h,cpp}` deep-copies the list into a plain C++ tree while the JournalMenu
movie is still alive:

* objects and arrays keep their shape, so `objectives` is still an array of objects,
* scalars keep their type, so the ActionScript side's `.toString()` behaves exactly as before,
* display objects are never descended into - only their text form is kept,
* nesting is capped at 16 levels and 20000 nodes, so a self-referencing object cannot run away.

When the QuestMenu opens, the tree is rebuilt inside *its* movie with `CreateArray` /
`CreateObject` / `SetMember` / `PushBack`. No reference to another movie survives, so it no longer
matters when JournalMenu is destroyed.

Two smaller changes came with it:

* `JournalMenuEx::InstallHooks()` runs on `kDataLoaded` instead of `SKSEPlugin_Load`. Other plugins
  (for example Stay At The System Page) write the same JournalMenu vtable slots and the last writer
  wins, so installing at load time left this mod's hooks dead.
* `QuestMenu` loads its movie with the engine's `BSScaleformManager::LoadMovie` instead of
  CommonLibSSE's `LoadMovieEx`. `LoadMovieEx` builds its own `GFxMovieDef::MemoryParams` and passes
  it to `GFxMovieDefImpl::CreateInstance`, which does not survive this runtime (access violation
  with `rcx = 0x2000`).

A missing-movie guard is included as well: if the winning `quest_journal.swf` could not be created
(for example because an asset it imports is not installed) the mod logs a clear warning instead of
silently letting the engine crash on a null `uiMovie`.

## Requirements

* Skyrim Special Edition 1.5.97 (built and tested there)
* [Quest Journal Overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/141295) 1.2 / 1.3
* SKSE64 2.0.20 and Address Library for SKSE Plugins

## Installation

Install with a mod manager and enable it **above** Quest Journal Overhaul in the left pane so this
DLL wins over the one the mod ships with. The folder contains only the DLL - the SWFs and JSON keep
coming from the original mod.

## Building

The build chain is the author's (CMake + vcpkg, `x64-windows-static-md`). `vcpkg-configuration.json`
pulls `commonlibsse-ng` from a GitLab registry; if GitLab is unreachable, build against a local
CommonLibSSE-NG prefix instead and point `CMAKE_PREFIX_PATH` at it. That is what
`CMakeUserPresets.json` is for - it is git-ignored so machine-specific paths stay out of the
repository.

```
cmake --preset release      # vcpkg manifest
cmake --build --preset release
```

## Credits

* jpsteel - Quest Journal Overhaul, the source this is built from
* License: GPL-3.0, same as upstream. This is a modified version; the changes are described above and
  the complete source is in this repository.
