# Optional weighted required-pose seed

This experiment reduces the initial required-bone seed for independent, single-LOD, single-node crowd leaders. It is disabled unless a validated manifest is explicitly supplied. It contains no card-specific or hardware-specific rules, does not change meshes, animation rate, materials or shadow policy, and does not introduce baked GPU animation data.

## Generate metadata

Run `Tools\lua\lua.exe Tools\run_crowd_editable_bone_audit.lua --tag <fresh-tag>` against prepared low/near assets. The separate editor commandlet reads editable mesh positive weights, maps every part into its own leader and closes parent chains. It never saves packages or requires retained CPU skin buffers in the game. For other asset layouts provide `--input-file` containing `cards` with `low` and `near` asset paths. The convenience selection wrapper targets the current R9/R6 layout.

The manifest includes source hashes, exact reference names/parents, mesh paths and sorted required indices. The 100-card manifest is approximately 8.6 MB of JSON metadata, not a pose bank. An audit alone is not proof that removing a bone is safe.

## Engine and game integration

`WeightedRequiredPoseSeed.patch` targets local UE 5.8 SkeletalMeshComponent.cpp. Inspect compatibility before applying on another Engine revision. It exports a cpp-only registration function without changing class layout. The project capability guard enables calls only in a matching non-Editor game build. Build and stage through the isolated Development host; do not replace authoring Editor Engine DLLs.

The seed is merged with the complete render ActiveBoneIndices. The original Engine follower, virtual-bone, physics, socket, shadow and parent requirements still run. A changed mesh or unsupported animation mode falls back to the original Engine seed. Unregister before introducing unsupported interactive animation, deformers or a different rig. No assumption that every future card lacks physics/sockets is made.

Launch with `--weighted-pose-seed Saved/Crowd/EditableBones-<tag>.json --verify-weighted-pose-seed` using `Tools\lua\lua.exe Tools\run_crowd_development.lua`. Validation rejects mismatched hashes, bone names/parents, missing mapping and unsupported components. Verification compares every retained required-bone component-space matrix byte for byte against the original seed at clip phases 0, 0.37 and 0.99. A failure unregisters the seed and fails the optional experiment.

For same-process A/B use `Tools\lua\lua.exe Tools\test_crowd_bone_packing.lua --modes 0,0,0 --weighted-pose-seed <manifest> --weighted-pose-seed-cases 0,1,0 --seconds 30`. Control `weighted_pose_seed=0` restores the original seed; `1` reapplies validated metadata. Compare actual ordered cards, camera, near-detail population and baseline drift. Counters are cumulative; `required_pose_bones` is the live total.

## Evidence and limits

100-card matrix checks passed with zero failures; near face/hair round-trip and current-shadow gates passed. Required count changed from 174,832 to 164,770 (5.76%). No stable whole-frame benefit has been proven: repeated-baseline drift was 12.95% and 9.06% in two trials. The candidate remains off by default. Development 120 FPS has not been achieved.

The actual animation codec must be measured, not inferred from installed plugins. The sample uses BitwiseCompressOnly; local constant-key decoding takes requested track pairs. A larger compressed track count is therefore not evidence that all tracks are decompressed every frame. The legacy raw-track view can be empty with a valid AnimationSequencerDataModel and compressed animation; do not rewrite clips based on that empty view.
