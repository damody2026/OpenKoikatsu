# Runtime shadow submission

2026-10-07. Iteration 1. This does not claim the virtual shadow map marking warning is gone. Root still has to compile and run the scene.

## What changed

Each admitted character can gain one extra GPU-skinned component that only casts shadows. The component uses that character's existing private pose provider. The mesh the player sees, and every material on it, is unchanged.

The shadow mesh is built from the same skinned triangles that already cast. Triangles are copied. Nothing is welded, dropped, or clamped to fit a palette. If the copy cannot be finished inside the existing 256-bone section limit and 256 MB preparation bound, the shadow mesh is discarded and the original component keeps casting.

Preparation of the index, weight, and vertex arrays runs on a worker. Material reads, the skeletal mesh, the proxy material instances, and provider registration stay on the game thread. No per-card asset is cooked. No queue-capacity or shadow-quality console variable was added.

## What may share a proxy

A section joins a solid proxy only when its shadow is the whole skinned triangle:

- the recipe is masked and its installed program has no coverage `clip`
- the live blend matches that recipe
- two-sidedness matches the recipe
- the live material does not use world position offset, pixel depth offset, or displacement
- a translucent material is not being asked to cast as masked

Those solid sections are not drawn with `M_ForgeOpaque`. That parent clips on texture alpha and its family id is 1, so it would either cut holes or delete the surface. The proxy is an already cooked parent, and the instance keeps that parent's default family:

- two-sided solid sections use `M_IBLOpaque` (family 47)
- one-sided solid sections use `M_IBLDepth` (family 26)

No current body, clothes, or hair recipe is solid and one-sided. `M_IBLDepth` is reserved for that case. `IBLOpaque` and `OilPainting` are the solid two-sided recipes. As corrected on 2026-10-20, glasses (`KoikanoGlasses0`–`11`) use source alpha blending and are excluded from solid shadow proxies. Everything else that still casts keeps its original material, UVs, and bone weights. Same-material original sections may be packed together even when they are not adjacent, still under the 256-bone limit.

Translucent recipes do not cast a virtual shadow map today, because the cooked parents do not set cast-dynamic-shadow-as-masked. Those sections are left out of the shadow mesh. They stay on the displayed mesh.

This is the compatible reduction. It will not merge the body, hair, and clothes into one draw. Doing that would change the silhouette.

## Scene rules that stay

- Loading still has a 60 second diagnostic deadline.
- Each character keeps its own private clip and the shared 4 GiB pose cache.
- Near-camera shadows stay at 1600 cm. Outside that radius the shadow component does not cast. The displayed GPU component stops casting only after the shadow component exists, so the same triangles are not submitted twice. If shadow preparation fails, the displayed component keeps the old cast.
- CPU/GPU switching still follows F9 / `UseCpu`. The CPU poseable is still not the installed caster.
- The 3 second reveal writes the same start and duration onto a proxy instance, including one created after reveal has already started.
- Face and mouth positions are uploaded with a stream built from the shadow mesh's own vertex order. The displayed stream is not reused.
- Streamed actions still widen the diagnostic bounds. The shadow component receives that box with the color and depth components.
- The corrected joint skin envelope is untouched. There is no radius clamp and no constant-pose fallback.

## What is logged

For each character, `APNG shadow submission` and the scene report row record:

- `shadow_source_sections`: caster sections before this compaction
- `shadow_proxy_sections`: sections on the shadow mesh
- `shadow_excluded_sections`: sections that do not cast and were omitted
- `shadow_reasons`: why sections stayed on their original material, or why they were omitted
- `shadow_fallback`: empty when the shadow mesh is in use; otherwise the displayed caster was kept
- `shadow_proxy_active`: the extra component is registered

`shadow_gpu_component` remains the displayed GPU component path, as before.

## Tests

Pure automation, no scene and no cooked card:

- `OpenKoikatsu.Runtime.ShadowSubmit.Coverage`
- `OpenKoikatsu.Runtime.ShadowSubmit.Mesh`

Coverage walks recipes 1–61 and checks the clip, parent name, two-sided, blend, displacement, and masked-cast cases. Mesh checks merged triangles, UVs, remapped bones, a face stream in shadow vertex order, a palette split at the supplied limit, and a hard failure when one source section cannot fit that limit.

## Root validation

Not run in this change. No editor, cook, or packaged build was launched. Root's pass is still: UE automation for StreetSocial, `RuntimeMesh.Face`, `Runtime.SkinEnvelope`, and `OpenKoikatsu.Runtime.ShadowSubmit`; Python26; Development staged StreetSocialSelfTest including all five voices, action, expressions, cancel, and save; then real virtual shadow map warnings and frame averages. Publish the CMD only after those functional checks. Do not treat a clean log line as proof until that run exists.


## Root review and verification
Root added no-gain fallback: proxy is created only if strictly fewer caster sections result. The five-card scene has154 potential caster sections ->144 (10 fewer, 6.49%); actual per-frame draws depend on camera admission. C04433->30, C06841->35, C02235->34 use proxies. Male10, C00012 and C01923 retain the original caster with reason no compatible shadow section reduction. Display materials and geometry unchanged; no special card branches.

Root repaired settled-face endpoint validation: non-casting facial parts are absent from proxy, so only missing channels are projected to -1 on the shadow stream. Present channels remain strictly validated; live morph transitions keep their original private catalogue weights. Added FaceChannels regression. Root also repaired the host include-copy list; coating-test NaN/Inf macros were replaced with equivalent numeric_limits values to avoid Development compile C4756. Coating shader parser changes came from concurrent workspace work; fresh common Cook07 succeeded, no character-specific Cook.

Development StreetSubmit03, Editor-streetsubmit26, UE Automation-Submit02 eight tests, Python26 and actual runtime SubmitR3/R4 all passed after repairs. SubmitR1 failed missing shared global coating shader; Cook06 failed old grouped parameter declarations; SubmitR2 failed absent face channel. All retained in STREET_SOCIAL_ERRORS.md. Public receipt updated only after SubmitR4 and ValidateOnly passed. Original hundred-person CMD unchanged.

SubmitR4 six-character native1080p RenderOffscreen10000samples: mean5.034ms, P9510.281ms; GT2.376ms, RT4.919ms, RHI1.511ms, GPU1.930ms; all visible8.464s. Stages overlap. This is functional scene evidence, not a controlled speedup comparison or stable120FPS acceptance. VSM marking overflow still logged; compatible merging alone did not remove it. Masked-silhouette/material fidelity must remain intact in future work. Full visual/content/window acceptance stays separate.

Grok job run-muxxuf2s-o30l9z, thread067f35bf-a3c4-4137-85d7-fdc99694dbe3, grok-4.7-build-fast, completed21m6s; reported420297 input,9248000 cached input,113704 output, USD4.25372228. Root independently reviewed current source, applied concrete repairs and ran builds/tests. Shared workspace HEAD advanced externally during interrupted turn; history preserved, no root commit/push/reset.
