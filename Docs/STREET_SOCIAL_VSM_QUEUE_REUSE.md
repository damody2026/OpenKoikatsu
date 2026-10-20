> 初版五個barrier候選被root拒絕，以下Grok初版紀錄保留作歷史。當前候選以文末root修正為準。

# VSM marking queue reuse candidate

Source-only candidate. The engine shader was not modified, and this pass did not cook, launch Unreal, or compile the shader. `Tools/apply_vsm_queue_reuse.lua` replaces the engine file only when root runs it. A failed compile or a shadow mismatch should restore the backed-up original and keep the public known-good command.

The authored replacement is `.ai-collab/VsmMarkingQueueReuse.kernel.hlsl`. `Tools/EnginePatches/VsmQueueReuse/check_vsm_queue_reuse.lua` (run with `Tools/lua/lua.exe`; the `.ai-collab` Python copy is stale) splices that function into a byte copy of the engine shader and writes:

- `.ai-collab/VsmMarkingQueueReuse.usf`
- `.ai-collab/VsmMarkingQueueReuse.patch` (unified diff, no preamble)
- `.ai-collab/VsmMarkingQueueReuse.meta.json`

## Hashes

| | |
|---|---|
| Engine file | `D:/UE5.8/Engine/Shaders/Private/VirtualShadowMaps/VirtualShadowMapBuildPerPageDrawCommands.usf` |
| `original_sha256` | `71eb11cb6277cd8d298a9808601b517922bd2ae65492fc3b408bec86c4429c11` |
| Engine size | 20043 bytes, still equal to that digest after the candidate was written |
| `candidate_sha256` | `456c7b9f0f248d3f3efa625264e1c7d6f4e89d893fa3bc55796b0217d6917793` |
| Candidate size | 23015 bytes, CRLF |

The metadata keeps the older `OriginalSHA256` / `CandidateSHA256` aliases. The apply script reads the lowercase `original_sha256` and `candidate_sha256` fields and refuses a candidate whose bytes do not match.

Only `CullPerPageDrawCommandsCs` changes, plus three groupshared counters inserted after `SharedMarkingJobs`:

```hlsl
groupshared uint GroupFirstPrimaryView;
groupshared uint GroupPrimaryViewCount;
groupshared uint GroupMipCount;
```

`MAX_SINGLE_THREAD_MARKING_AREA` stays `8`. `MARKING_JOB_QUEUE_SIZE` stays `NUM_THREADS_PER_GROUP * 2` (64 threads, 128 slots). `PackJob`, `UnpackJob`, `MarkPageDirty`, and the other two compute kernels are not edited. The checker compares the extracted text of `AllocateCommandInstanceOutputSpaceCs` and `OutputCommandInstanceListsCs` with the engine file.

## Scheduling

The original kernel lets every valid lane walk all primary views and mips, and append large marking jobs to one 128-slot groupshared queue. A full directional group is 64 lanes times 17 clipmap views. That is 1088 large jobs before the single drain, so the overflow path is structural. Overflow still marks; it marks serially on the lane that failed to claim a slot, and the original sticky flag then forces later work on that same lane through the serial path as well.

The candidate drains and reuses that same queue once per primary view and mip. Each lane enqueues at most one large job in a round, so a round occupies at most `NUM_THREADS_PER_GROUP` slots. The capacity is unchanged. Invalid, frustum-culled, page-mask-culled, and HZB-culled lanes do not enqueue, but they execute the same round barriers and the same cooperative drain.

View range and mip count are published by lane 0 through groupshared, then consumed only after `GroupMemoryBarrierWithGroupSync`:

- `VSMCullingBatchInfo.FirstPrimaryView` and `NumPrimaryViews` are properties of the dispatch group. In batch mode they come from `VSMCullingBatchInfos[BatchInds[DispatchGroupId]]`. Unbatched, they are kernel parameters. `DispatchGroupId` comes from `SV_GroupID`.
- `TargetNumMipLevels` is a field of the primary `FNaniteView`, including a local-light view. Lane 0 loads `GetNaniteView(FirstPrimaryViewId + ViewOrdinal)` and publishes it. On the shadow path `GetNaniteView` unpacks `InViews[ViewIndex]` (or `InViews[0]` if `NANITE_MULTI_VIEW` is off). It does not write page or dirty state. The uniform-buffer branch is marked unused by shadow passes and would report zero mips to every lane, which is the original behavior.

Where wave vote is compiled, the loop bounds are `WaveReadLaneFirst` of those groupshared values. That does not change the value: lane 0 wrote it, the barrier happened, and every lane reads the same locations. It gives DXC a compiler-uniform trip count per wave. Both waves of a 64-thread group still see the same count. The non-wave permutation reads the groupshared values directly. The barriers are not inside either branch.

## Barrier sites

Every lane that passes the opening batch check participates. That check returns before any barrier and is uniform because the whole group shares `SV_GroupID`.

There is no `continue` in the replacement. HZB failure sets `bHzbCulled` instead of continuing, so a culled lane still reaches both round barriers. `PixelEstRadius *= 0.5` stays before the overlap test and before the HZB test, matching the original order, so a culled mip still halves the radius for the next mip.

The five barriers in `CullPerPageDrawCommandsCs`, counted from the `[numthreads` line of the spliced function:

| Function line | Control-flow stack | Role |
|---|---|---|
| 15 | none | Original zero of `NumSharedMarkingJobs` |
| 45 | none | Publish `GroupFirstPrimaryView` and `GroupPrimaryViewCount` |
| 110 | `for (ViewOrdinal < PrimaryViewCount)` | Publish `GroupMipCount` for this primary view |
| 281 | view `for` and `for (MipLevel < MipCount)` | Zero the queue for this round |
| 303 | same two loops | Drain after enqueue |

The lane-0 stores sit in their own `if (GroupThreadIndex == 0)` blocks. The barriers are outside those blocks. The enqueue `if (bDoLargeJob)` is between the two round barriers and does not contain a barrier. Item setup, frustum visibility, overlap, and HZB state are per-lane flags used only to decide work; they do not guard a barrier.

`InstanceCullingLoadBalancer_Setup` is still called unconditionally, on the same arguments as the original, before the view loop. Its own barriers stay in that helper. Its early returns are the original ones and occur before those barriers: one compares the group batch index with `NumBatches`, and the other compares the group's `NumItems` with `NUM_THREADS_PER_GROUP`. The helper's per-lane item test does not wrap its barriers. This candidate does not move those barriers.

## Why one job per lane cannot overflow

`InterlockedAdd(NumSharedMarkingJobs, 1U, LargeJobIndex)` occurs once per mip iteration, and only when that lane's rect is larger than 8 pages and `MarkPageDirtyFlags != 0`. The queue is reset to 0 after a group barrier and before that add. At most one lane produces one index. Sixty-four participating lanes therefore produce indexes `0 .. jobs-1` with `jobs <= 64`. `MARKING_JOB_QUEUE_SIZE` is 128, so `LargeJobIndex >= MARKING_JOB_QUEUE_SIZE` is not reachable. The serial overflow store and the `min(MARKING_JOB_QUEUE_SIZE, NumMarkingJobs)` drain clamp remain in the shader anyway.

This holds for any `NUM_THREADS_PER_GROUP`, not only 64: one job per lane is strictly below a queue of twice the group size. It does not hold if a later edit enqueues twice in one round.

The original sticky `bMarkingJobQueueOverflow` also redirected later small and zero-flag rects on that lane into the serial marker. The candidate keeps the sticky flag only for `VSM_STAT_OVERFLOW_FLAG_MARKING_JOB_QUEUE`. The serial decision for the current rect is `bIsSmallJob || bThisJobOverflow`, where `bThisJobOverflow` is that job's own failed index. While the index is unreachable, both versions leave the flag false and take the same non-overflow path. If a future edit made overflow reachable, they would diverge: the original sticky path would serial-mark later zero-flag rects and might then skip `WriteCmd` when no physical page is valid, while this candidate would keep `NumMappedPages = 1` for those later rects.

## What is intentionally the same

Per valid item, before any view loop: payload, dynamic instance-id offset and `checkSlow`, `UnpackDrawCommandDesc`, `GetInstanceSceneData`, `GetPrimitiveData`, and one `LoadDynamicMeshBounds` on the local-bounds copies. The shader-print OBB is in that same block.

Per primary view, only on a valid lane, and with no barrier inside it: `CalculateInstanceDynamicData`, `FBoxCull` init, distance, WPO, `VirtualShadowMapIsWPOAllowed`, `ShouldCacheInstanceAsStatic`, cache-dependent state, screen size, global clip, `ShouldMaterialInvalidateShadowCache`, and `GetInstanceViewData(...).bIsDeforming`. `Cull.Frustum()` still runs only when `Cull.bIsVisible`. `VSM_STAT_NON_NANITE_INSTANCES_TOTAL` and `VSM_STAT_NON_NANITE_INSTANCES_FRUSTUM_CULLED` still add `NaniteView.TargetNumMipLevels` once per primary view.

Per mip, only when the item is valid and the primary view was visible: screen rect, `IsDetailGeometry`, radius halving, uncached rect, page rect, `OverlapsAnyValidPage` with the same page-flag mask, detail flag, and receiver mask. Stats page area is counted on overlap and before HZB. HZB still uses `IsVisibleMaskedHZB` and increments `VSM_STAT_NON_NANITE_INSTANCES_HZB_CULLED`. Page-mask misses increment `VSM_STAT_NON_NANITE_INSTANCES_PAGE_MASK_CULLED`. Dirty flags are still `VirtualShadowMapGetMarkPageDirtyFlags(..., false /*bInvalidateLODDelta*/)`.

Small rects, and a large rect that actually overflowed, serial-mark with the original inclusive `X`/`Y` loops and count real `MarkPageDirty` hits. A large rect that was queued, and a rect whose dirty flags are zero, does not serial-mark. Both set `NumMappedPages = 1` ("Must assume we have mapped pages"). `WriteCmd` is called only when `NumMappedPages > 0`, with `(MipViewId, InstanceId, IndirectArgIndex, ViewCullingFlags, bViewCacheAsStatic)`. `MipViewId` is still `MipLevel * TotalPrimaryViews + PrimaryViewId`. Per lane, calls stay view-major and mip-minor.

`ThreadTotalForAllViews` still increments even if `WriteCmd` returns false. After the view loop, a valid lane still adds the drawn-instance stat, the two overflow flags, and one `InterlockedAdd` of that total into `DrawIndirectArgsBufferOut[IndirectArgIndex * INDIRECT_ARGS_NUM_WORDS + 1]`. The page-area diagnostic loop, including its stated data race, is unchanged. Invalid lanes do not update those per-item aggregates.

`MarkPageDirty` reads `VirtualShadowMapGetPageFlag` and, when the mask matches, calls `VirtualShadowMapMarkPageDirty`. That function writes `OutDirtyPageFlags[...] = 1` for the set flag bits and does not write the page-flag texture. `OverlapsAnyValidPage` gathers page flags, not `OutDirtyPageFlags`. Dirty stores of 1 are idempotent. Draining a round before the next mip's overlap test therefore does not change overlap, HZB, cache, WPO, or invalidation decisions. The set of dirty-flag stores is the same set the original kernel produces; only their issue order inside the group changes.

Cooperative drain body is the original stride loop, including the comment that invalidation does not consult `VSM_FLAG_DETAIL_GEOMETRY`. Each queued job is stored once and drained once, then the counter is cleared for the next round. Stale slots past `NumMarkingJobs` are not read.

## Residual non-equivalence

`WriteCmd` allocates through `WaveInterlockedAddScalar_` on `VisibleInstanceCountBufferOut[0]`. Cross-lane success order was already atomic. Aligning calls to round barriers can change which lane observes `VisibleInstanceOutputOffset >= VisibleInstancesBufferNum` if that buffer fills inside the group. It does not change the per-lane argument sequence, the per-item attempt count, or the indirect-argument aggregate. The visible-instance overflow flag remains the OR of that lane's failed calls.

## Checks actually run

`python .ai-collab\check_vsm_queue_reuse.py` from the workspace root, exit 0:

```
original_sha256 71eb11cb6277cd8d298a9808601b517922bd2ae65492fc3b408bec86c4429c11
candidate_sha256 456c7b9f0f248d3f3efa625264e1c7d6f4e89d893fa3bc55796b0217d6917793
interface_files 17 missing []
barriers 5
  barrier line 15: []
  barrier line 45: []
  barrier line 110: ['for (uint ViewOrdinal = 0U; ViewOrdinal < PrimaryViewCount; ++ViewOrdinal) {']
  barrier line 281: ['for (uint ViewOrdinal = 0U; ViewOrdinal < PrimaryViewCount; ++ViewOrdinal) {', 'for (uint MipLevel = 0U; MipLevel < MipCount; ++MipLevel) {']
  barrier line 303: ['for (uint ViewOrdinal = 0U; ViewOrdinal < PrimaryViewCount; ++ViewOrdinal) {', 'for (uint MipLevel = 0U; MipLevel < MipCount; ++MipLevel) {']
schedule directional-64-large: barriers/lane=53 decisions=1088 max_jobs=64 overflows=0 original_queue_peak=1088
schedule point-light-mixed: barriers/lane=104 decisions=1536 max_jobs=25 overflows=0 original_queue_peak=1200
schedule all-invalid: barriers/lane=53 decisions=0 max_jobs=0 overflows=0 original_queue_peak=0
schedule unflagged-large-rect: barriers/lane=19 decisions=240 max_jobs=0 overflows=0 original_queue_peak=0
diff_touch_lines 390
PASS
```

A second process recomputed both digests without using the checker. Engine `71eb11cb6277cd8d298a9808601b517922bd2ae65492fc3b408bec86c4429c11` (20043 bytes). Candidate `456c7b9f0f248d3f3efa625264e1c7d6f4e89d893fa3bc55796b0217d6917793` (23015 bytes, CRLF). Both match the lowercase metadata fields. The patch starts with `--- D:/UE5.8/.../VirtualShadowMapBuildPerPageDrawCommands.usf`. The spliced cull body contains 5 barriers, 1 queue `InterlockedAdd`, 0 `continue`, and 1 `return`.

Schedule figures are an abstract protocol, not a shader execution. `directional-64-large` is 64 lanes, 17 views, 1 mip, every lane large: the original queue would grow to 1088, and each candidate round stops at 64. `point-light-mixed` is 6 views by 8 mips; lanes 40–63 are invalid, and lower lanes are HZB, small, frustum-culled, or large, with at most 25 large jobs in a round against an original peak of 1200. `all-invalid` still takes 53 barriers and writes nothing. `unflagged-large-rect` is one view, 8 mips, 30 zero-flag rects: 240 mark/write decisions, no queue traffic. `decisions` counts lanes that reach the mark block. A small job can still get `NumMappedPages == 0` and skip `WriteCmd`. Barrier counts are `2 + views * (1 + mips * 2)`.

The interface check looks for call and type names in 17 engine files: the original usf plus `Common.ush`, `ViewData.ush`, the VSM page/projection/stats/cache headers, `VirtualShadowMapDefinitions.h`, Nanite culling/HZB/decode, instance-culling and its load balancer, `WaveOpUtil.ush`, `DynamicMeshBounds.ush`, `ShaderPrint.ush`, and `SceneData.ush`. `missing []` means every listed name occurs somewhere in that text. It does not check types, overloads, register bindings, or permutation defines.

## What those checks do not prove

- The barrier walker is brace-based. It does not run DXC's uniformity analysis. It can miss a barrier hidden in a macro or in a helper. It does treat `#if` as text, not as nested flow. A raw count of `GroupMemoryBarrierWithGroupSync` in the cull function matched the five parsed sites.
- The schedule model does not evaluate frustum, HZB, page masks, or `MarkPageDirty`.
- Name presence is not a compile. No DXC or Unreal shader compile was run. Do not treat this document as compile success.
- The model assumes the published view count and mip count are group-uniform, which is the engine data layout above, not a measurement from a capture.

## Compile risks left for root

1. Non-wave permutation: the loop bounds are ordinary groupshared loads. DXC does not treat groupshared as implicitly uniform and may reject the barriers as non-uniform control flow. The wave permutation uses `WaveReadLaneFirst` under `COMPILER_SUPPORTS_WAVE_VOTE`, which is the existing `WaveOpUtil.ush` switch. That macro is present because this shader already includes that header for `WriteCmd`.
2. `(FDrawCommandDesc)0`, `(FInstanceSceneData)0`, and `(FPrimitiveSceneData)0` are definite-assignment initializers for lanes that never read them. The original already zero-initializes `FContextBatchInfo` and `FFrustumCullData`. `FPrimitiveSceneData` contains `float4 CustomPrimitiveData[NUM_CUSTOM_PRIMITIVE_DATA]`. DXC usually accepts a scalar cast of these numeric structs; that was not compiled here. A rejection is a compile failure, not a reason to change the engine by hand in this workspace.
3. Hoisted item and per-view state can increase register pressure or spill. Occupancy was not measured.
4. `const uint` declared inside the view and mip loops is legal SM6 HLSL and was not compiled here.
5. If `GetNaniteView` were ever given a side effect, the extra lane-0 publish load would matter. The current unpack path does not have one.

No trace was removed. No engine C++, runtime, game, plugin, or Tools source was edited. No per-card resource or hardware configuration was added.

## Root validation, not run here

Apply only after review. The script writes `Saved/EnginePatches/VsmQueueReuse/VirtualShadowMapBuildPerPageDrawCommands.original.usf` when the engine digest is still the original, then replaces the engine file through a sibling `.ok-queue-reuse.tmp`. Restore copies that backup back only if its digest is `original_sha256`.

```
Tools/lua/lua.exe Tools/apply_vsm_queue_reuse.lua --engine D:/UE5.8 --candidate .ai-collab/VsmMarkingQueueReuse.usf --metadata .ai-collab/VsmMarkingQueueReuse.meta.json --backup Saved/EnginePatches/VsmQueueReuse/VirtualShadowMapBuildPerPageDrawCommands.original.usf --mode apply
```

`Tools/test_street_social.ps1` does not exist. The cook and runtime scripts that do exist are `Tools/cook_street_social.ps1` and `Tools/test_street_social_runtime.ps1`. Cook requires a fresh tag and will refuse an existing `.cook`, `.log`, or `.patch` suffix for that tag. Apply the engine shader before that cook so the new global shader cache is the one packaged. Point the runtime test at that new patch directory; its default patch is the older `Saved/StreetSocial/Build/StreetCook03.patch`.

```
Tools/cook_street_social.ps1 -Tag <fresh>
Tools/test_street_social_runtime.ps1 -Tag <fresh> -Patch Saved/StreetSocial/Build/<fresh>.patch -RequireFaceTransitions -RequireCleanShadows -RequireShadowProxy
```

`-RequireCleanShadows` fails the run if `game.log` still contains a `[VSM]` queue-overflow line. Off/on equivalence and mean/P95 remain root measurements. On a compile, sync, or visual failure, restore with `--mode restore` and do not replace the known-good command.

## Root同步修正與錯誤紀錄

Grok初版缺少drain結束的group sync：快lane0可能在慢lane讀NumSharedMarkingJobs或SharedMarkingJobs之前開始下一round並清零／改寫。只有enqueue後barrier不足以保護consumer結束，abstract model原先也未模擬這個race，因此初版PASS不能作正確性證據。

root在每輪WriteCmd之後、uniform mip loop末尾加GroupMemoryBarrierWithGroupSync。所有lane必須結束這轮queue讀取及命令工作，才可進入下一輪reset。群組barrier共6處，每mip三處，directional模型70 barriers/lane、point-light152、all-invalid70、zero-flags27；max jobs64／25／0／0、overflow0。root重新執行check_vsm_queue_reuse.py exit0。新候選SHA256為7db463d6a89199f67b3a0674beeac7ecdd09207d5337fe481221c73bc09530f1。

root已hash-guard備份原shader到Saved/EnginePatches/VsmQueueReuse/VirtualShadowMapBuildPerPageDrawCommands.original.usf，套用修正候選並開始StreetQueueReuse01共用Cook；未修改容量、材質／幾何／品質或硬體設定。編譯與runtime尚待結果。

Grok完成job run-muy35zaj-wkg667，thread8f882c24-3a98-4347-8d8e-20c1e39ea81f，grok-4.7-build-fast，9m2s，input386318／cached2489216／output42394，USD1.65433664。root只接受作輸入，修正以上缺陷後獨立驗證。

共用Cook StreetQueueReuse01成功，16個shader jobs全部完成，包含SM6全域shader cache並封裝。檢查時首次誤查OpenKoikatsu/Content下的global cache（不存在），隨即以rg --files定位實際Engine/GlobalShaderCache-PCD3D_SM6.bin；未影響Cook及測試。

## 首輪實際Development結果

同一Submit03執行檔、1920×1080、同選單及selftest：

| 版本 | 平均ms | P95ms | GTms | RTms | RHIms | GPUms | overflow |
|---|---:|---:|---:|---:|---:|---:|---:|
| Cook07 / Baseline01 |3.712|6.523|1.639|3.690|1.150|1.823|1|
| QueueReuse01 / Patched01 |5.082|10.978|2.513|4.971|1.561|1.962|0|

兩輪對話15、招呼5、告別5、表情／轉換／取消／關係分支、六人private APNG GPU及shadow proxy斷言均通過。不同pipeline階段不能相加。首輪曾有其它Unreal程序，無法據此宣稱受控效能提升；root因觀察到退化而追加Baseline02/Patched02，正式receipt尚未更新。

原始截圖street.selftest.png目視前景人物與腳影接近，背景NPC位置受時間變化。補充像素分析：foreground平均channel絕對差2.439/255、腳影1.088/255、靜態建築0.487/255。這不是逐像素全景等價證明，region與結果在Build/StreetQueueReuse01.visual.json。

## Root本階段驗收完成

追加順序前後測Baseline02／Patched02均通過功能檢查。原版平均3.442ms／P955.699ms、GT1.484／RT3.422／RHI1.066／GPU1.746ms、overflow1；新版本平均3.147ms／P955.353ms、GT1.269／RT3.128／RHI1.013／GPU1.760ms、overflow0。兩次patched均clean，對話／語音嘴形／關係事件／表情轉換及private shadow proxy通過。數據有系統負載波動，不能把CPU平均差異全歸因於GPUshader，更不是百人120FPS證明。

正式StreetSocial.cmd使用的receipt已由backup+atomic publisher更新至StreetQueueReuse01.patch，同一Submit03執行檔；ValidateOnly通過。正式百人CMD仍使用其原先已驗證shader包。完整交互視窗、百人及local-light runtime驗收保留到最後；本次是六人正式Development新功能驗證。

可維護候選／kernel／patch／metadata／checker已放到Tools/EnginePatches/VsmQueueReuse，不再只依赖.ai-collab。checker支援--original-shader選擇原檔備份並拒絕未知引擎revision；重新產生候選digest仍與已安裝shader相同。root拒絕初版同步缺陷後完成修正與驗證。HEAD未變、沒有commit/push。逐輪比較資料在Saved/StreetSocial/Build/StreetQueueReuse01.comparison.json。
