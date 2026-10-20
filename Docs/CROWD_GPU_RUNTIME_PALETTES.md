# Independent GPU runtime mesh palettes

## Scope and implementation

Manifest v2 records per-actor pose bone name, parent index and canonical float32 local-reference-matrix SHA1. Each actor retains its independent clip/track/pose ranges. Selection/composition preserve identities while rebasing ranges. Version 1 remains readable; its missing bone identities prevent runtime palette construction while its prepared low palettes keep working. SHA1 here is an asset consistency check, not a security authorization.

OKBuildGpuMeshPalette maps every mesh bone by name into that actor's private pose, validates parent name and local-reference identity, and uses the mesh's own inverse binds. Missing/ambiguous names, hierarchy/reference mismatch, incomplete identities or nonfinite inverse binds reject atomically without changing the output. Compatible newly loaded near face/hair meshes do not require a per-card mesh-path whitelist. No shared rig or pose assumption, mesh simplification, material substitution, section merge or hardware tuning.

Manifest bytes are bounded to 64 MiB plus header for full-crowd identity tables. Encoder, decoder and async reader share this limit. Existing cache GPU data and shaders are unchanged. The v2 real cache GPU file is byte-identical to its prior fixture (SHA1 F111D0D572ED73F7198C734069AD913F80B6D895); the manifest supplies additional identity data.

Runtime adoption is opt-in via -CrowdIndependentGpuRuntimePalettes, or -RuntimePalettes on Tools/test_crowd_gpu_animation_control.ps1, and obeys the existing formal-scene CPU/GPU toggle. Mesh changes invalidate lookup/palette state; actor/component removal drops bindings. Unsupported meshes retain native CPU rendering. Existing original material, section visibility, native morph data and shadow policy remain in use. CPU pose evaluation is still retained for verification/fallback.

## Evidence

- Standalone Development R19 build succeeds, 22.03 seconds.
- RuntimeBoneIdentitySyntheticR17: 100 independent synthetic actors, 20 clips each; GPU oracle passes after selection/reordering/composition. 28800 compared elements, max error .00000143051147. Four malformed binding cases reject without changing output. Synthetic results are not scene/FPS evidence.
- RuntimeBoneIdentityRealR18: ten actual selected cards; resident pose, private palette, cache roundtrip and joint-loader checks pass. 16 invalid manifest cases and 11 invalid cache cases reject. Palette max error .000126361847; vertex error .000147656378 cm. Cache-only read ~292 ms for 118799304 bytes in this run; not a cold 100-card loading claim.
- RuntimeBoneIdentityR19 formal CrowdRoom, third person, 100 distinct animated cards: GPU OFF/GPU ON/GPU OFF (native CPU pose retained throughout) and near/far round-trip pass. 567099 matrices including 6969 previous matrices, zero mismatches, max .00019073486328125 < .001. 55 cumulative runtime palette builds, zero rejects for this fixture. GPU ON ends with 28 executed components including 11 runtime-created bindings; peak verified population 37. Builds are cumulative operations, not 55 distinct characters or meshes.

## Limits and next ownership step

This verifies bone/palette correctness for the current ten-card fixture, including runtime near meshes. It does not establish full-scene visual parity, all 100 actors' GPU ownership or 120 FPS. Native CPU evaluation remains; no fresh throughput claim is made for metadata/binding coverage. Last throughput R15 was excluded due 17.346% CPU-control drift and foreign game/editor activity.

Before disabling a CPU leader, require coverage of all visible/shadow followers, supported animation/morph/physics features, independently driven clocks, conservative animated bounds, and a safe handoff on clip/outfit/detail/fallback changes. Validate actual GPU-owned rendering against a separate diagnostic CPU reference so removing CPU work cannot make the comparison self-referential. Profile without the diagnostic reference/readback after ownership succeeds.

## Interactive comparison launcher

CrowdRoomDevelopmentGPU.cmd launches `Tools\lua\lua.exe Tools\run_crowd_gpu_development.lua`. It snapshots the current standalone Development binary by SHA256 and shares the existing cooked Engine/Content through junctions; it does not replace the CPU stage or overwrite a running executable. Original optimized launcher quality, third-person/WASD camera, TAA, 60 Hz animation and 160 FPS cap are retained. It adds the v2 ten-card cache, trusted identity, shader patch, runtime palettes and stat unit display. F8 toggles the GPU request in the same process for comparison; F9 reports actual GPU coverage. Native CPU pose remains. This launcher is not evidence of complete 100-card offload or a measured speedup.

-ValidateOnly verified required inputs, executable/patch hashes, parameter forwarding and the final launch arguments, without opening a game. PowerShell syntax checks pass. Each actual launch gets a fresh Saved/Crowd/DevelopmentOptimized*.launch.json/log/report path.
