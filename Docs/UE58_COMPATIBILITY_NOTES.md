# MetaHuman Baseline UE 5.8 Compatibility Notes

**Date:** 2026-07-10  
**Original (UE 5.6, untouched):** `D:\UE Projects\MetaHuman_Baseline_Test`  
**Clone (UE 5.8):** `D:\UE Projects\MetaHuman_Baseline_UE58_Test`

## Summary

| Item | Result |
|---|---|
| Original left on 5.6? | **Yes** (`EngineAssociation: 5.6`) |
| Clone retargeted to 5.8? | **Yes** |
| C++ editor build | **Succeeded** |
| Safe to open original in 5.8? | **No** |

## Clone-only changes

1. `UnrealPerformer.uproject` → `EngineAssociation: 5.8`
2. `UnrealPerformer.Target.cs` / `UnrealPerformerEditor.Target.cs` → `BuildSettingsVersion.V7` + `Unreal5_8`
3. ACE plugin patches (compile blockers on 5.8):
   - `ACECoreModule.cpp` — `FindOrAddConfigSection`
   - `OmniverseLiveLinkListener.cpp` — `auto&` JSON Values iteration

## Open the clone

```powershell
& "D:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "D:\UE Projects\MetaHuman_Baseline_UE58_Test\UnrealPerformer.uproject"
```

Accept Convert Project with **Open a copy** (safest) or, because this folder is already a clone, in-place on **this path only** is OK.

## Original baseline (keep on 5.6)

```powershell
& "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe" "D:\UE Projects\MetaHuman_Baseline_Test\UnrealPerformer.uproject"
```
