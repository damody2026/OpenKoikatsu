# Street Social private face patterns

Owned runtime data for card eyebrows, eyes, and mouth. This note does not claim that the module was compiled or that a scene was run. Root owns the source-table export, the scene API, and build validation.

## Contract

`FKKRuntimeFaceState` lives on the private mouth stream. Each pattern index and each opening defaults to `-1`.

- `Face == nullptr` passed to `OKUpdateRuntimeMouthStream` is the old call. Positions stay `Base + lerp(MouthClosed, MouthOpen, Opening) * Gain + BlinkClosed * Blink`. The crowd launcher leaves the face disabled, so it still takes this path.
- A pattern of `-1` preserves that card-authored channel. Eyes, brows, and mouth are independent. An explicit all-`-1` state does not replay the legacy mouth or blink.
- A selected mouth uses the audio `Opening` and the existing `Gain`. `MouthOpening >= 0` scales that audio opening; otherwise the scale is 1. `Gain == 0` contributes no mouth delta, including when a mouth pattern is selected.
- A selected eye uses `EyeOpening` when it is supplied. Blink closure moves that expression toward the selected closed endpoint: the open amount is `EyeOpening * (1 - Blink)`. `EyeOpening == -1` keeps the card opening and blinks from there toward the selected closed endpoint. `EyePattern == -1` ignores blink.
- A selected brow uses `BrowOpening`, or 1 when that opening was not supplied. Blink does not move brows.
- `OKValidateRuntimeFaceState` rejects a nonfinite opening, an opening outside `[-1, 1]`, a pattern below `-1`, and a pattern the stream does not contain. Failure returns before any render upload, so the color and depth position buffers stay as they were. The same rejection leaves the pure evaluator's output array unchanged.

The stream still stores the prepared bind-space positions. One upload writes the same positions into the color mesh and the depth mesh. GPU skinning is unchanged. No CPU animation pose and no shared character morph bank are required.

## Source

Native controls are `0` mouth, `1` eyes, and `2` eyebrow, read with `face_pattern_count`, `face_pair`, `face_baseline_weight`, and `ok_part_raw_morph` while the existing native session is open on the background snapshot task. The catalog and sparse deltas are ordinary owned bytes after that session closes.

For a selected pattern, every morph referenced by any pair in that control is included. The weight is the desired close/open amount minus that morph's baked baseline (`face_baseline_weight / 100`). A morph index of `-1` is skipped. If close and open are the same morph, the desired weight is 1. Raw deltas use the existing Unity-to-bind conversion: `(-X, Z, Y) * 100`, then the vertex's native skin matrix, then the inverse of the prepared reference skin. Rigid or singular skin still rejects a vertex that has a face or mouth delta.

Where the same native morph is referenced by more than one control, the later control replaces the earlier one. Order is eyebrow, then eyes, then mouth.

The old `MouthClosed`, `MouthOpen`, and `BlinkClosed` capture is unchanged. Legacy mouth still subtracts baselines for pattern 0's own pair only. Generic pattern 0 can therefore differ when other mouth morphs have a nonzero baseline. Legacy blink already subtracted every eye morph for pattern 0 closed.

Open amounts stay continuous, matching the private mouth path. They are not quantized to the integer percent used by the full FaceBlendShape controller.

## Memory

Per part and control:

- at most 256 patterns
- at most 512 unique morphs

Only nonzero deltas are stored, one sample per affected vertex (`control`, local morph, native morph, bind-space delta). Pattern pairs and baselines live in one catalog shared by that part's vertices. Vertex copies keep the catalog pointer, so existing reduction still carries the samples when it keeps an endpoint. There is no dense pattern-by-vertex matrix.

Those bytes are counted with `TArray::GetAllocatedSize` and refused with the existing capture error when they, plus the vertex and index slack, exceed `OKCaptureRuntimeMeshes`' budget. The character snapshot still defaults that budget to 256 MiB. Growing `FKKRuntimeMeshVertex` by the sparse array and the catalog pointer is included in the slack check because that check uses `sizeof`.

## Limits

- High face parts stay on the existing reduction rule: head, eye, brow, lash, and eyeline are not given a triangle cut. This change does not alter that policy.
- The scene still withholds automatic blink unless the legacy eyelid span is present, and it can force blink to zero when an expression disallows it. Pattern selection does not change that gate.
- A selected mouth follows speech gain. Silence returns that channel to the card basis instead of holding the expression closed or open. Eyes and brows stay on the selected expression.
- No scene UI, expression catalogue, or exporter file is edited here.
- Compile, automation execution, and in-game checks belong to root. They were not run for this note.

## 主代理整合與實測
原上文為Grok交付時狀態，現已完成獨立檢查與測試。capture由manifest enable_face_patterns控制，原百人預設false；-1 opening為唯一保留哨兵，(-1,0)非法。Sparse grow／讀取臨時陣列及Render packing的最壞複製payload在分配前納入256MiB預算。
DevelopmentStreetFace03、Editor18編譯通過；Python26項、UE3項通過。Runtime-FaceR2：26次來源表情套用、零缺漏，5NPC實際更新／復原並最後active=false；15話題+5開場+5道別、7拒絕、33.102s雙私人action、取消與存檔成功。六人全顯示8.269s；1920x1080離屏10000samples平均3.247ms、P955.417ms；不是正式視窗／百人驗收。公開StreetSocial.cmd已透過receipt使用修正版。
眉眼嘴仍由CPU組合稀疏形變並更新同版面的color/depth位置，骨架動畫繼續GPU，不能稱所有臉部工作都在GPU。source pattern blend旗標保留，但本輪直接套用endpoint，原逐漸混合尚未重製。腮紅／淚／高光／視線／頸IK未宣稱完成。
