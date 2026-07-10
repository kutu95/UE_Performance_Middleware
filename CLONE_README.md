# MetaHuman Baseline UE 5.8 Compatibility Clone

**Created:** 2026-07-10  
**Source (untouched UE 5.6):** `D:\UE Projects\MetaHuman_Baseline_Test\UnrealPerformer.uproject`  
**This clone:** `D:\UE Projects\MetaHuman_Baseline_UE58_Test`

## Purpose

Safe upgrade test only. The original `MetaHuman_Baseline_Test` must remain on UE **5.6**.

## Clone-only changes already applied

1. `"EngineAssociation": "5.8"` in `UnrealPerformer.uproject`
2. `Target.cs`: `BuildSettingsVersion.V7` + `EngineIncludeOrderVersion.Unreal5_8`
3. NVIDIA ACE UE 5.8 compile fixes (same as Godfrey clone):
   - `ACECoreModule.cpp` → `FindOrAddConfigSection`
   - `OmniverseLiveLinkListener.cpp` → JSON `Values` key type via `auto&`

## Open this clone in UE 5.8

```powershell
& "D:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "D:\UE Projects\MetaHuman_Baseline_UE58_Test\UnrealPerformer.uproject"
```

When the Convert Project dialog appears:

- Prefer **Open a copy** (safest), or
- **Skip conversion and open in-place** is acceptable **only** on this clone folder (never on `MetaHuman_Baseline_Test`).

## Production / baseline path (do not convert)

```powershell
& "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe" "D:\UE Projects\MetaHuman_Baseline_Test\UnrealPerformer.uproject"
```
