# Selected GPU shard loading in formal CrowdRoom (2026-10-05)

The Development scene now accepts `-CrowdIndependentGpuShardIndex` and
`-CrowdIndependentGpuShardIndexIdentity`. It derives the selected runtime
character IDs after the actual quality variant is selected, then asynchronously
loads only matching shards. Missing selected cards remain native. A malformed
selected shard rejects the batch and preserves native rendering.

`OKLoadSelectedGpuAnimationShardsAsync` runs file IO, linked cache/manifest
validation and composition on a worker. No Unreal objects are loaded by this
path and gameplay does not wait on its future. Selected order is preserved;
every loaded manifest must contain exactly its requested private character.
Pose, track, inverse-bind and mesh palette indices are rebased by the existing
validated composition implementation.

Bounds: index <=4 MiB and <=100 rows; per-cache <=512 MiB; manifest uses the
shared format limit; aggregate encoded files <=2 GiB-1 by default. All selected
file sizes are checked before cache allocations. File-size changes during
loading are rejected and each cache decoder receives its prechecked file
budget. Composition also enforces its own payload budget. Source shard data
and composed output coexist temporarily, so this is not a 2 GiB peak-process
memory guarantee. GPU initialization/upload pacing remains future work.

The scene report exposes `independent_gpu_cache_load`: source kind, worker
execution, loaded shard and bundle actor counts, missing selected actors and
IO/validation/composition milliseconds. These are not scene-startup or GPU
upload times, nor FPS evidence.

## Launcher

The existing launcher keeps its prior ten-card default. To test a prepared
index explicitly:

```powershell
CrowdRoomDevelopmentGPU.cmd -GpuShardIndex Saved/Crowd/GpuShardArbitraryR30/selection-index.json
```

F8 still switches CPU/native bridge mode; F9 reports actual GPU execution.
Cache and index arguments are mutually exclusive. The wrapper hashes the index
for the worker's expected identity. ValidateOnly was checked without opening
another user game. No quality, animation frequency or hardware affinity was
changed by this integration.

## Validation and known limits

- R36 standalone Development build succeeded; R37 adds structured load stats.
- SelectedShardLoadR36 audit passes selected order, missing-selection fallback,
  and eight rejection cases: stale index identity, aggregate budget, duplicate
  selection, no selected shard, missing cache, malformed identity, duplicate
  index character and swapped manifest ownership. Two-shard load/composition
  was 66.791 ms in that cached diagnostic, not a cold hundred-card entry test.
- R32 formal scene loaded 2 shards on a worker in 72.703 ms, with 98 missing
  selected actors remaining native. R36 repeated load in 88.492 ms.
- R36 verified 4,258,176 current renderer matrices, zero mismatches, max error
  0.000175476074. GPU-only probe compared 14,292 previous matrices with zero
  mismatches, max error 0.0000443458557.
- R32/R34/R36 strict native history checks remain rejected: native previous
  comparison count is zero for this fixture. Sustained sampling and matching
  the native `UpdateMode OR DirtyBoneTransforms::Previous` upload condition did
  not produce those samples. This is explicitly not complete motion-history
  or visual acceptance; the strict gate was not relaxed.

This index contains two cards outside the old first-ten fixture, not all 100.
It demonstrates runtime selection/composition of independent private shards.
The other 98 retain their complete native path; CPU animation remains for all
actors. Full GPU ownership, all selected card exports, multi-walk export,
original material shader permutations, independent production clocks/bounds,
native CPU removal and clean 120 FPS/cold <60-second entry acceptance remain.
Functional scene timing with audit/readback or a frame cap is not throughput
acceptance. Errors and rejected runs are kept in CROWD_GPU_POSE_ERRORS.md.

R37 formal loader/toggle test completed OFF/ON/OFF successfully, with matrix
readback disabled (not full GPU history acceptance). All three structured
reports show worker execution, 2 loaded shards / bundle actors, 98 missing
selected actors, and 100 loaded/animated native-backed characters. GPU executed
components are 0 / 3 / 0 across the phases. IO/validation/composition was
76.378 ms in this run. These assertions and launcher ValidateOnly passed;
no throughput acceptance is claimed. Receipt:
Saved/Crowd/GpuAnimationControl-ShardSceneR37.json.
