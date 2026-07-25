# unrealperformer (Captain Godfrey)

**Production master:** UE **5.8** — `D:\UE Projects\MetaHuman_Baseline_UE58_Test`

See **[Docs/UE58_Baseline.md](Docs/UE58_Baseline.md)** and **[Docs/EngineeringManual.md](Docs/EngineeringManual.md)**.

## Known-good checkpoints

| Tag / note | Meaning |
|------------|---------|
| `restore/ue58-audio-ok-2026-07-10` | UE 5.8 clone with PIE audio mute fix (pre–Phase 1) |
| Phase 1 hardening | Diagnostics, validation tool, config, docs — behaviour unchanged |

## Open in Unreal

1. Epic Games Launcher → UE **5.8**
2. Open `UnrealPerformer.uproject`
3. Level: **Godfrey_World**
4. Optional: **Tools → Validate Godfrey Project**

## Not in this repo

- **`Content/MetaHumans/`** — stock Quixel Bridge assets (see `Docs/MetaHumanAssets.txt`)
- ACE third-party binaries (see `Docs/ACEPlugin.txt`)
- `Saved/`, `Intermediate/`, `Binaries/`

## Backups (do not treat as master)

- `MetaHuman_Baseline_Test` — UE 5.6 known-good
- `Test_Live_Audio` — legacy reference
