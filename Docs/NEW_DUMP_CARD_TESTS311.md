# Stage311: four additional cards and source reference audit

## Scope and references

These four cards were loaded through the same outfit0/session renderer path as the existing regression set. No card-name transform, color, or shader override was added.

| Card | Game mesh JSON | Game material JSON | Reference used |
| --- | ---: | ---: | --- |
| `KK_380982.png` | 56 | 56 | `mesh_dump/KK_380982.png/SnowShot_2026-09-29_22-19-12.jpg` |
| `弹丸论破 - 索尼娅 内瓦曼2.png` | 82 | 82 | `mesh_dump/<card>/SnowShot_2026-09-29_22-21-50.jpg` |
| `KK_381623.png` | 69 | 69 | `mesh_dump/KK_381623.png/SnowShot_2026-09-29_22-22-48.jpg` |
| `弹丸论破 - 边谷山佩子.png` | 0 | 0 | Card PNG preview only; **no game T-pose photo** |

The dump roots in the table are under `C:/portable/KoikatuSunshine/mesh_dump/`. Card inputs are under `C:/portable/KoikatuSunshine/UserData/chara/female/`. `Saved/Logs/NewCards311Initial173Audit.json` contains the exact absolute reference paths and SHA-256 values. `Tools/GamePoseProbe/report_dump_cards.py` now records `reference_type`, dump counts, and a fallback to card preview when no game photo or previous Unreal capture exists. A preview is explicitly labeled and is never counted as game photo parity.

## Baseline loading and pose test

`Tools/GamePoseProbe/run_dump_cards.ps1` captured four cards in T-pose and Studio walk at 0.4 seconds, with one full-body, one face, and one side-face frame per pose. `Saved/DebugShots/NewCards311Initial173/` contains 24 PNGs. Both owned Unreal processes exited 0. `Saved/Logs/NewCards311Initial173Audit.json` records 4/4 PV display audits for each pose, 92 attachment checks per pose with maximum vertex error 0 cm, and palette errors at most 2.262e-6 cm. The 15 historical `Saved/Screenshots/` captures remained hash-identical. [The contact sheet](../Saved/Tests/NewCards311Initial173/ContactSheet.png) combines the source image, T-pose, walk, and face for each card.

Visual comparisons: the first three game photos and their Unreal T-poses show the same principal outfit and accessory layout. This is a load/pose check, not pixel-perfect shader parity. The `KK_381623` blonde hair and some skin/clothes shadows remain visibly different; the game photo and preview also use different face expressions. `visual_parity_passed` is therefore `false`.

The fourth card's PNG preview shows a purple rabbit-ear outfit, while Unreal outfit0 shows a maid outfit. A fresh native Studio probe loaded the card using `Studio.AddObjectFemale.Add(card)` and captured 69 renderer rows in `Saved/Tests/NewCards311PekoNative173/`. The active native meshes `o_top_maid_01_a` (4922 vertices), `o_bot_maid_01_a` (1944), `o_shoes_maid_01` (1656), and `o_acs_head_maid_02` (828) match the outfit0 mesh names in the Unreal log. Thus the card preview cannot be used to assert that Unreal outfit0 chose the wrong clothing coordinate. The native fixture does not provide a game T-pose photo for this card.

## Rejected shared-light experiment

The native Studio snapshot has an enabled `Directional Chara` light on layer 10 with Unity forward `(0,-0.6427876,-0.7660446)` (`Saved/Tests/NewCards311PekoNative173/Card0.json.lights.tsv`). A test build temporarily replaced camera-relative light input in the shared skin/hair/clothes ramp branch with this fixed Studio direction. Build `Saved/Logs/NewCards311StudioLightBuild174.log` succeeded and `Saved/DebugShots/NewCards311StudioLight174/` preserves 24 experimental captures; `Saved/Logs/NewCards311StudioLight174Audit.json` confirms they loaded, **not** that they match the game. Visual A/B on `KK_381623` showed new hard-edged orange arm shading and blue/white patches on the shirt and trousers absent from the game photo, while the hair mismatch remained. The shared change was rejected and reverted; `Saved/Logs/NewCards311RevertBuild175.log` succeeded. A new `KK_381623` T-pose smoke (`Saved/DebugShots/NewCards311RevertSmoke175/`) exited 0, and its three frames differ from the pre-experiment baseline by at most 0.145 mean RGB levels per channel across each full image. This experiment shows that scene-light direction alone cannot stand in for each shader family's normal, visibility, ramp, and tone calculations. Future work must compare the original per-family DXBC path and all shader inputs before changing the production branch.

All errors and the false light assumption are recorded in `Docs/ERROR_LOG.md` E-169. The deferred chin outline was not changed.
