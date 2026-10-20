# Game pose probe

Read-only diagnostics for the animation loader. The DLL is **not installed** in
the game after the test. Build with `Toolsuaua.exe ToolsGamePoseProbebuild.lua`.
It targets the installed CharaStudio assemblies and BepInEx.

To reproduce, temporarily copy `OpenKoikatsu.GamePoseProbe.dll` to
`C:/portable/KoikatuSunshine/BepInEx/plugins/`, set the environment below, and launch
a new CharaStudio process with its own `-logFile`. Close only this test process and
move the DLL back to this tool directory afterward. Do not run the probe against
a Studio session containing user work: it creates a character and requests exit.
It never saves the character card or Studio scene.

| Variable | Meaning |
|---|---|
| `OK_PROBE_CARD` | Absolute original card path; default `KK_091207.png` |
| `OK_GAME_POSE_OUTPUT` | Absolute JSON output path; set explicitly |
| `OK_PROBE_EXPRESSION=0` | Disable the Studio ExpressionBone component before playback |
| `OK_PROBE_FACTORY=plain` | Use Character.CreateFemale + ChaControl.Load without Studio initialization |
| `OK_PROBE_BODY_PHYSICS=0` | Disable Ver02 body components and record the Animator/reference baseline |
| `OK_PROBE_BODY_PHYSICS=fixed` | Freeze that baseline, reset Ver02 setup, then invoke the game's numerical step 240 times at 60 Hz |

The probe selects `action/animator/00.unity3d`, controller `c33`, state
`Base Layer.Main.Idle`, normalized time `0.2625`, MotionSpeed 1, Animator speed 0,
arm correction off and hand correction on. This matches the Rust state selector
at elapsed 0.5 seconds (state speed 0.7, clip duration 4/3 seconds).

Outputs: JSON with BakeMesh world vertices, original vertices/weights/bindposes
and renderer bone paths; `.bones.tsv` with local transforms and actual ExpressionBone
flags; `.png` from a separate diagnostic camera. The JSON serializer omitted the
custom nested Bone/Flag arrays, so use the TSV for those fields. Missing JSON fields
do not prove a component is absent. Camera/shading is diagnostic, not a shader match.

The `plain` factory confirms ExpressionBone is absent, but also misses some installed
plugins' Studio initialization hooks: this test loaded vanilla clothing/accessories
and different ABMX scales. Do not use it as a complete mod-card visual reference.
The Studio disable control retains the installed mod character and compares the
effect of that component; it can retain helper transforms changed during initial load.

From the project root:

```powershell
Tools/lua/lua.exe Tools/GamePoseProbe/compare.lua --game Saved/Logs/StudioPoseC33NoExpression.json --rust Saved/Logs/K091ControllerElbowScene.json --output Saved/Logs/K091ControllerSceneComparison.json
Tools/lua/lua.exe Tools/GamePoseProbe/compare.lua --game Saved/Logs/StudioPoseC33Render.json --rust Saved/Logs/K091ControllerElbowSceneVariants.json --variant studio_full --output Saved/Logs/K091ControllerStudioComparisonFinal.json
```

Rust diagnostic:

```powershell
cd Tools/ok-extract
cargo test --release --lib k091_controller_elbow_skin_diagnostic -- --ignored --nocapture
```

Only compare equal vertex counts and renderer topology. This probe is not a full
Animator graph player, source of asset metadata, or game modification shipped with UE.

## Ver02 controlled comparison

Additional outputs: `.physics.tsv` records the live four body components and all
particle fields; `.physics-baseline.tsv` is the pose before the controlled steps;
`.physics-setup.tsv` records parameters **after** ResetPosition/ResetParticlesPosition.
The fixed test restores every body transform to the captured baseline each frame.
Repeated Animator.Update(0) alone is not a frozen transform control.

Set `OK_PROBE_BODY_DT` (default 1/60) and `OK_PROBE_BODY_MOVE` (default 0) to
exercise high frame rates and constant prefab-root X movement per frame.
`skip_patch_owner` records live Harmony patches; `component_state` includes the
existing UpdateTime remainder. ResetPosition does **not** zero this timer.
The validated moving 120 Hz fixture is `BodyVer02HalfRateStateGame.json`;
`BodyVer02HalfRateGame.json` lacked the initial timer state and is diagnostic only.

`BodyVer02SetupGame.json` is the validated fixture. `BodyVer02FixedGame.json` used
repeated Animator.Update; `BodyVer02FrozenGame.json` lacked the post-reset setup
capture. Keep those as diagnostic records. The Rust controlled test supplies the
captured local setup/soft parameters to isolate numerical simulation; it does not
prove the entire factory/ABMX/Animator pipeline matches the game pointwise.

```powershell
cd C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib ver02_game_frozen_body_chains -- --ignored --nocapture
cargo test --release --lib runtime_body_ver02_mod_cards_and_cache -- --ignored --nocapture
$env:OK_TEST_BODY_FIXTURE='../../Saved/Logs/BodyVer02HalfRateStateGame.json'
cargo test --release --lib ver02_game_frozen_body_chains -- --ignored --nocapture
```

## Body collider contact control (2026-09-28)

`OK_PROBE_COLLIDERS=1` enables the installed KKS_Colliders controller's three
flags for this in-memory character and calls ApplyColliders/LateUpdate;
`OK_PROBE_COLLIDER_CONTACT=1` moves both forearm helper transforms near the
breasts before the frozen baseline. Neither saves card, scene or game config.
`WritePhysics` now records every live Ver02 collider, allowing source constants,
binding and particle radii to be checked independently of numerical results.

Validated fixture: `Saved/Logs/BodyVer02CollidersGame.json` and companion TSVs.
Run `OK_TEST_BODY_FIXTURE=../../Saved/Logs/BodyVer02CollidersGame.json`
with ignored Rust test `ver02_game_frozen_body_chains`. It verifies seven generated
colliders per breast, real contact (>0.1 mm displacement from no collisions), and
the unchanged strict bone comparison tolerance. This isolates the solver and
uses captured setup values; it is not a full animation/character equivalence test.

## Ordinary hair / skirt collider control (2026-09-28)

Set `OK_PROBE_ORDINARY_PHYSICS=fixed`, `OK_PROBE_BODY_PHYSICS=0`,
`OK_PROBE_EXPRESSION=0` and `OK_PROBE_COLLIDERS=1`. The probe disables existing
ordinary components in memory, creates two three-bone components, captures the
whole transform baseline, and invokes the installed game's ordinary solver for
240 frozen 60 Hz steps. `ct_clothesBot` has the source skirt root name and X
FreezeAxis; `ct_hairB` exercises the floor without skirt/arm additions.

The control obtains native collider names from the actual `p_cf_body_bone`
prefab, keeping this comparison independent of additional Uncensor organs.
The source has 24 native colliders; hair uses 25 with floor and skirt uses 31
with floor and six legs. Arbitrary mod-added body colliders are a separate
loader requirement and are not established by this isolated solver fixture.

Validated fixture: `Saved/Logs/OrdinaryCollidersGame265v4.json`, with
`.ordinary-baseline.tsv`, `.ordinary-setup.tsv`, `.ordinary-result.tsv`.
The Rust test parses native colliders from the bundle, checks dimensions,
source hierarchy traversal order, exact world matrices and FreezeAxis, then
compares six bones at the unchanged 2e-6 m / 2e-5 quaternion-component gates.
Contact is measured in **world position**, since parent rotations may leave
local child positions unchanged. Removing all colliders moves a bone by
73.34827 mm in this control. `OrdinaryGameContact267.log` passes.

```powershell
cd C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib ordinary_game_floor_skirt_control -- --ignored --nocapture
cargo test --release --lib ordinary_runtime_cards_and_cache -- --ignored --nocapture --test-threads=1
```

Earlier artifacts remain diagnostic: v1 had uninitialized AddComponent lists/
curves; v2 included four mod organ colliders absent from the native source;
v3/v4 initial contact assertions incorrectly compared local child positions.
Those failed logs are not validation results. This probe never saves a card,
scene or configuration. Move its DLL out of BepInEx/plugins after each run.

## Mixed clothing slots and ModBoneImplantor control (2026-09-28)

Set `OK_PROBE_CLOTHING_WEIGHTS=1` for a seven-vertex synthetic SMR under
the neutral character `objTop`. It retains four original bone slots and bind
poses, including two slots referring to the same `borrowed` transform with
different binds, one private transform outside `cf_j_root`, and one inside.
The installed game's `AssignedWeightsAndSetBounds` runs before `BakeMesh`.
After destruction completes, the probe captures world vertices with the body
bone at x=10 and x=20, plus the actual surviving/null bone references.

With `OK_PROBE_IMPLANT=1`, it adds the installed plugin's real
`ModBoneImplantor.BoneImplantProcess`, selecting the otherwise destroyed bone
as `trfSrc` and the cloned body bone as `trfDst`. The actual Harmony prefix
rescues that transform, retains its local x=3, and sets the other unimplanted
private slot to null. Without the marker, vanilla retains that private slot.
No per-card decisions or altered mesh weights are used in the Rust comparison.

Validated fixtures: `Saved/Logs/MixedClothesGame268.json.clothing.tsv` and
`Saved/Logs/MixedClothesImplantGame270.json.clothing.tsv`. Both JSON errors
are empty; their companion PNGs exist. Two samples × seven vertices × two
branches match at 0 m maximum difference, with the unchanged 2e-6 m gate.
These controls establish loader/slot semantics in a neutral outer frame;
they do not establish arbitrary Studio global transforms, every mod asset's
serialized metadata, or a full Animator graph.

```powershell
cd C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib clothing_game_ -- --ignored --nocapture
```

Pure tests additionally check same-name implant priority and cache restoration,
invalid-marker null slots, component/collider world movement after body updates,
and collision response (55.641 mm against a no-collider control). The latter
is a Rust contact test, not a game solver equivalence comparison.

## Shared clothing prefab coordinator and native quaternion control (2026-09-28)

Set `OK_PROBE_CLOTHING_WEIGHTS=1`, `OK_PROBE_IMPLANT=1`, and
`OK_PROBE_IMPLANT_PHYSICS=1`. The real installed implantation hook first
reparents two private nodes beneath the body transform. Two real ordinary
DynamicBone components, rooted at the shared body and implanted private bone,
then run 120 steps at 60 Hz. All InitTransforms calls run before any
UpdateDynamicBones calls in each step, matching Unity Update/LateUpdate order.
The physics control frame is detached to world identity; no card, scene or
configuration is saved. `.clothing-physics.tsv` contains three local bone TRS
samples and seven actual BakeMesh vertices. `.quaternion.tsv` records seven
actual Unity Quaternion.FromToRotation angle controls.

The Rust coordinator test uses
`Saved/Logs/ClothesCoordinatorGame271.json.clothing-physics.tsv`; v2 and v3
have the same numerical physics samples. The native quaternion test uses
`Saved/Logs/ClothesCoordinatorGame271v3.json.quaternion.tsv`. All companion
JSON errors are empty and PNGs exist. Final validation is
`Saved/Logs/ClothesCoordinatorNativeQuaternion271.log`: four manual tests pass.
Three bone local positions differ by at most 0.00000003 m, quaternion
components by 0.00000004, and seven vertices by 0.00000019 m. Original
2e-6 m / 2e-5 quaternion gates remain unchanged. The earlier two loader
controls still compare 28 vertices at 0 m maximum error.

The native controls return identity at angles 1e-6, 1e-5, 0.0001, 0.0005,
and 0.001 rad; 0.0015 and 0.00485 rad produce nonzero rotations. The matching
near-parallel test is dot > 0.999999. The older 0.9999 cutoff and removing
the cutoff entirely both disagree with these samples. World-to-local position
conversion also uses inverse linear basis times (world-parentWorld), avoiding
translation cancellation without dropping shear.

```powershell
cd C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib clothing_game_ -- --ignored --nocapture
cargo test --release --lib clothing_prefab_coordinator -- --nocapture
```

Preserve failed diagnostic logs. `ClothesCoordinatorGameComparison271v2.log`
and `ClothesCoordinatorSolverDiagnostic271.log` failed the strict numerical
gate. `ClothesCoordinatorInversePosition271.log` reduced position error but
still failed; only the native quaternion correction completed that comparison.
Moving the control frame in v2 did not change physics samples, so outer-frame
placement was not the cause. The LNK1104 in GameComparison271.log occurred
because another Windows test executable still held its output file; wait for
that process before compiling the same binary again.

These are constructed in-memory prefabs exercising the actual installed game
loader and solver. They do not prove arbitrary zipmod serialized metadata,
all branching child orders, complete character/Animator equivalence or
UncensorSelector.TransferBones support. The five-card Unreal regression
contains no mixed clothing renderer. Every owned game process was stopped
after checking its executable path; all three run DLLs are back in this tool
directory, and no probe DLL remains in BepInEx/plugins.

## Branching hierarchy, notRolls and exclusions control (2026-09-28)

Set `OK_PROBE_HIERARCHY=1`, `OK_PROBE_BODY_PHYSICS=0`,
`OK_PROBE_EXPRESSION=0`, and explicitly set `OK_GAME_POSE_OUTPUT`. This
independent world-identity control creates twelve transforms with five root
children. Their `SetSiblingIndex` order differs from object creation order.
Three cases run the actual installed ordinary DynamicBone: plain, two notRoll
siblings, and those same notRolls with two exclusions. Each captures baseline
local TRS/sibling indices, actual private `m_Particles` order and parent indices,
then final local TRS after 120 steps at 60 Hz. Virtual ends use source
`m_EndOffset=(0,0.05,0)`; the force is `(0.007,-0.001,0.003)`.

Validated fixture: `Saved/Logs/BranchingGame272.json.branching.tsv`; JSON error
is empty and the companion PNG exists. Native particle counts are 18, 14 and 9.
Ordinary siblings are traversed first; only the **last** non-excluded notRoll
sibling has its eligible children expanded afterward. The first notRoll
sibling's subtree is omitted, matching this game's decompiled AppendParticles.
Do not expand every notRoll subtree just because that behavior seems preferable.

`SourceSiblingTargeted272Final.log` passes three tests: this actual-game control,
two-prefab implantation/cache order, and original `oo_base.unity3d` parsing.
The game test compares all 36 final transforms and exact particle parent/order:
maximum local position error 0.00000007 m, quaternion component error
0.00000107, unchanged 2e-6 m / 2e-5 gates. The parser verifies 5,531 original
child references and 814 parents whose m_Children order differs from PathID sort.
All original sibling metadata survives a bincode roundtrip. The pure implantation
test keeps existing body children before top and bottom prefab implants and
preserves marker append order when private IDs are reassigned.

```powershell
cd C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib source_sibling -- --include-ignored --nocapture
```

The source field is now carried through every parsed/merged/private skeleton;
synthetic old fixtures without sibling metadata use deterministic ID fallback.
The coordinator resolves body identities and appends new children in prefab load
order. Cache layout 87 / pipeline 118 / extraction 272 invalidate older rigs.
The original game fixtures and their stricter numerical gates are retained.

The source parser test is an original bundle test; the branching physics test is
a constructed real-game control, not proof that every original zipmod's external
references or unrelated Harmony override has been resolved. No card, scene or
configuration is saved. Owned CharaStudio PID 69976 was checked and stopped,
and its DLL moved to `OpenKoikatsu.GamePoseProbe.branching-run.dll` here.


## External MonoScript deserialization control (2026-09-28)

Run `python Tools/GamePoseProbe/make_external_script_fixture.py` from the repo
root. UnityPy reads original oo_base/ao_hair_00 and writes copies only under
Saved/Tests/ExternalScripts273. Genuine MonoScripts move to separate CAB files;
the source retains a wrong-class MonoScript at the same PathID. All m_Script,
AssetBundle preload references and local script identifiers point to the actual
external file. This distinguishes correct FileID resolution from local-ID lookup.
The locator fixture supplies a loose body dependency and a zipmod hair dependency.

Set OK_PROBE_EXTERNAL_SCRIPTS to that absolute output directory, explicitly set
OK_GAME_POSE_OUTPUT, OK_PROBE_BODY_PHYSICS=0 and OK_PROBE_EXPRESSION=0. Clear other
clothing/hierarchy/ordinary control flags. Build/copy the temporary probe as above
and launch an owned CharaStudio process. The game loads dependency then source,
compares every prefab's MonoBehaviour type/hierarchy and public physics fields
against the original loaded bundle, and writes .external-scripts.tsv.

Validated: ExternalScriptsGame273.json, empty error and companion PNG. Summary:
body 13 prefabs / 4 Ver02 / 28 colliders (female body subtree has 24); hair 29
prefabs / 5 ordinary. This proves real game deserialization of these rewritten
original-resource copies. It does not prove arbitrary Harmony hooks or cross-file
Transform graphs. PID 24148 was checked against CharaStudio.exe and stopped;
its DLL is back here as OpenKoikatsu.GamePoseProbe.external-scripts-run.dll.

```powershell
cd C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib external_script -- --include-ignored --nocapture
```

Six tests pass in ExternalScriptTargeted273Final.log. They cover all 304 genuine
MonoBehaviour classes, complete physics semantic config, same-ID decoys,
invalid/null PPtrs, EOF handling, changed older dependencies, duplicate CAB
rejection and actual source-package precedence. Removing package context must
invalidate script/physics maps and refuse three ambiguous candidates. Named files
inside one bundle also resolve without game_root. Directory reads are 136 bytes
of 41,568,676 and 114 of 18,580; the warm persistent index reparses zero sources.
A deflate ZIP with tail metadata still consumes that entry to reach the directory.

Keep ExternalScriptTargeted273.log, which failed for HashMap byte ordering and
an incorrect front-directory test assumption. Config comparison now uses all
semantic fields while retaining array order. UnityPy's save tuple documentation
reverses the actual save_fs argument order: this control explicitly uses (66,2)
for front-directory dependency bundles. No original game resources are rewritten.
See HANDOFF2 section 130 and ERROR_LOG E-128 for remaining graph/import limits.

## Head FBS and outline source controls (stages280/281)

`verify_head_fbs.lua --sources-only` compares the Rust-selected head prefab's
Mouth/Eyes/Eyebrow PtnSets against an independent UnityPy read (2456 rows over
the five installed cards). The complete verifier also checks native March
world geometry, final UE test stdout and all fifteen screenshots. This validates
the pattern0 loader baseline, not blink/gaze or full controller transitions.

The materials probe (`OK_PROBE_MATERIALS=1`) writes all current head renderer
materials before and eight seconds after Studio.AddObjectFemale.Add. It now
records `_LineWidthS` floats as well as colours and texture identities. March
stage281 reports cf_O_face scalar1 in both snapshots, empty JSON error. Its
owned PID15160 was checked against CharaStudio.exe then stopped after the
fixture completed; the DLL is stored here as `OpenKoikatsu.GamePoseProbe.skin-outline-run.dll` and is not
installed in the game. No original card/scene/configuration is saved.

`verify_skin_outline.lua` checks actual KKS/EC compiled vertex bindings and
DXBC constants, native material scalar, cache/unit/build/test logs and five-card
final capture gates. Koikano uses .6/.003 plus material `_LineWidthS`; Forge
skin uses .3/.005. Both sample numeric DetailMask at vertex LOD0 with its own ST.
Rendering uses direct GPU samples instead of nearest/quantized vertex coverage.
See HANDOFF2§138 and E-137; full fragment lighting/object-scale parity remains
separate work. These display audits do not compare native pixels.

### Eyes / Eyebrow controller capture (stage282)

With `OK_PROBE_FACE_CONTROLS=1`, the existing original-card Studio probe freezes
Animator/ChaControl/Expression updates, resets Brow -> Eyes -> Mouth to source
pattern0/default rates for each case, then applies every Eyes/Brow pattern with
FixedRate0/.61337/1. It saves `.face-controls.tsv` with target/channel identities,
all Close/Open indices and actual renderer blendshape weights (including reset
zeros and inactive tears). This is a weight control, not a native pixel or
full world-vertex fixture.

Fresh March141 cases and mod Alice135 cases are in `Saved/Tests/FaceControls282`.
UE `OpenKoikatsu.FaceBlendShape.NativeFaceControls` queries the actual card
Session through C ABI and checks1096 pairs and145524 native channel weights.
Initial failure exposed dropped tear targets; FBS references now survive
visibility filtering, with default tearsLv0 still hidden. The probe DLL was
moved back here; the game plugin directory has no installed probe.

`Tools/lua/lua.exe Tools/GamePoseProbe/verify_face_controls.lua` checks those fixtures,
five selected-head sources, six UE tests, fresh March geometry and actual UI/
five-card display captures. `OK_SHOT_EYES`, `OK_SHOT_EYES_OPEN`, `OK_SHOT_BROW`,
`OK_SHOT_BROW_OPEN` select fixed controls; `OK_SHOT_FACE_AUDIT=1` verifies CPU
primary/secondary section restoration and invalid inputs. Use absolute card
paths in semicolon-separated `OK_SHOT_CARDS`, and `OK_OUTFITS_LIMIT=1`.
`OK_VERIFY_FACE_CONTROLS=1` with `ok.VerifyAnimationCatalog` tests real gallery
callbacks. See HANDOFF2§139 / E-138 for retained failures and scope limits.

`run_face_controls.lua --mode Tests`, `--mode Shots` and `--mode Gallery` reproduce
the final pipeline132 gates with absolute original-card paths and the actual
UE parameters. Synthetic eyeline second-material passes inherit the source FBS
key, channels, baseline and transformed deltas; the UE gate checks58 such pairs
across March/Alice. Hidden head visibility uses renderer GameObject references,
with a missing-metadata asset-name fallback; a renamed-mod control checks this.
Final logs: build282Finalv5, automation282Finalv4Console, shots282Finalv4,
gallery282Finalv2 and FaceControls282Final132Verified. Middle130/131 screenshots
remain under DebugShots/old and are not the current accepted capture.

## Native MainItemPlus controls and mask RGB sampling (stage315)

`run_renderer_native.lua --renderer-controls --mesh-names o_acs_widebrimmedhat_0 o_tang`
captures requested visible renderers with source materials, removal, primary
colour regions, white shadow, flat normal, null mask and ambient-alpha controls.
The camera uses the manual palette world vertices; these are isolated renderer
focused controls, not the original scene camera or final pixel parity. Material clones,
camera, renderer and global state are restored after each renderer.

`--studio-shadow-depth .74` on either the native runner or `run_dump_cards.lua`
sets the same explicit diagnostic scene depth (effective ambient alpha .26).
Omit it for the existing scene default. The native runner changes only its
owned process; it does not save the game's config, card or scene. Native
`_ambientshadowG` RGB is 128/255. The old reference JPG has no saved scene
globals, so a blue result with this diagnostic value is not photo parity.

Renderer controls also write `._ColorMask.uv-samples.*` and
`._DetailMask.uv-samples.*` for each source mesh UV with the material's ST.
The native Unlit/Texture GPU probe checks mip0 RGB only: its output alpha is
always one and cannot verify packed normal alpha. Clear sentinels reject an
unrendered fixture. DXT compression and GPU quantization explain small errors.

With `OK_DUMP_MATERIAL_INPUTS=1`, CLI exports include each mesh's
`.material-inputs.json`, exact `.shader-_Property.png` inputs, the existing
`.color-mask.png`/`.detail-mask.png`/`.hair-gloss.png` names, and `toon-ramp.png`.
Null shader defaults stay absent. This opt-in bypasses a normal export's stamp
so diagnostics cannot silently disappear on an otherwise warm export.
`OK_DUMP_HAIR_INPUTS=1` retains its previous hair-only scope.

```powershell
Tools/lua/lua.exe Tools/GamePoseProbe/verify_item_uv_samples.lua Saved/Tests/MainItem315NativeScene074/Card0.json Saved/Tests/MainItem315FinalInputsHat Saved/Logs/MainItem315HatUvAudit.json
Tools/lua/lua.exe Tools/GamePoseProbe/verify_item_uv_samples.lua Saved/Tests/MainItem315NativeScene074/Card1.json Saved/Tests/MainItem315SamplingInputsCounter Saved/Logs/MainItem315CounterUvAudit.json
```

The verifier matches MainItemPlus material identity and accessory slot, records
source hashes and colour space, compares bilinear RGB sampling with `1-V`, and
reports the no-conversion negative control. It does not verify normal maps,
compressed mip chains or complete native/UE shader output.

## Installed MaterialEditor texture API controls (stage316)

`run_renderer_native.lua --material-texture-controls --mesh-names o_acs_widebrimmedhat_0 o_tang`
uses the installed `KK_Plugins.TextureContainer` and
`MaterialEditorAPI.MaterialAPI.SetTexture`, through reflection, on temporary
renderer material instances. The original materials are restored and owned
instances are destroyed in `finally`; original cards and scene files are not
saved. The runner removes the probe from the game in its existing cleanup path.

Each declared property gets an asymmetric 2x2 PNG and null, Repeat, Clamp and
Mirror previous bindings. Null must become Repeat; a nonnull binding's wrap
must survive even when it came from an earlier runtime texture. The controls
record the real RT, sRGB view, Bilinear filter, two mip levels and GPU RGB at
all source mesh UVs. `renderer.materials` is necessary because the actual API
can instantiate a material held in `sharedMaterials`.

```powershell
C:/portable/OpenKoikatsu/Tools/lua/lua.exe C:/portable/OpenKoikatsu/Tools/GamePoseProbe/verify_material_texture_controls.lua C:/portable/OpenKoikatsu/Saved/Tests/MainItem316NativeTextureAPIR2 C:/portable/OpenKoikatsu/Saved/Logs/MainItem316NativeFactoryAudit.json
```

The R2 fixture has 64 cases and 67,168 RGB samples over hat and tongue
materials. The fixed RGB thresholds are max .025 / mean .002; incorrect linear
sampling must fail the negative control. Normal conversion, packed alpha and
the complete mip chain are excluded. MainItemPlus does not declare `_AlphaMask`:
Rust now checks the same generated ParsedForm texture-property schema as the
UE adapter before accepting an ME texture override, matching SetTexture's
`HasProperty` guard. A generic draw test includes an absent-property rejection,
three wrap modes and accessory/clothing/hair/body lookup semantics; its body
case does not verify the separate production body/face loader.

The later `MainItem316NativeTextureAPIFinal` fixture adds eight UV coordinates
outside 0–1 per property, with negative and multiple-period values. Its 32 cases,
59,552 RGB samples and one actual SetTexture rejection of `_AlphaMask` pass
`Saved/Logs/MainItem316NativeFactoryStressAudit.json`. Restoration covers all
renderers below the target because the installed API visits child renderers.

`verify_face_layer_order.lua NATIVE_CARD_JSON OUTPUT_JSON UE_LOG ...` compares
the native upper-eyeline renderer's shadow/stroke material queues with the
actual UE component priorities. Each split shadow pass must draw before its
stroke. The tool accepts independent card-order capture logs, fails on missing
controls, and does not declare final-pixel parity. Stage316's first 17-card
batch failed local visual review despite its passing image inventory; keep its
fixtures as a negative control and use the corrected R2 batch for publication.

## Native accessory move hierarchy controls (Stage317)

- ChaControl.UpdateAccessoryMoveFromInfo replaces N_move and N_move2 local TRS separately. For SMRs, apply these changes to the private Transform tree before DynamicBone and LBS; a post-skin matrix cannot represent branch-specific moves. Rigid MeshRenderers keep their renderer-ancestor placement path.
- Generic nested/sibling branches and mixed weights: `cargo test --manifest-path C:/portable/OpenKoikatsu/Tools/ok-extract/Cargo.toml accessory_moves_preserve_independent_skin_branches`.
- Native independent fixture: `run_renderer_native.lua --card-names "弹丸论破 - 江之岛盾子2.png" --tag NEW_TAG --mesh-names o_tie_01 o_necktie_yure_long junkochoker --capture-face --capture-isolations`. MeshNames matches sharedMesh.name, not GameObject.name (`o_mesh` is the GO for o_tie_01).
- Installed fixture test `native_accessory_move_hierarchy -- --ignored --nocapture` uses preserved `Saved/Tests/Neck317NativeR2`. It gates fixed vertices, raw geometry/binds, and all-vertex native-palette LBS; records settled DynamicBone differences separately. See `Saved/Logs/Neck317NativeHierarchyAudit.json`. A numerical attachment check is only session-to-viewer consistency and does not prove native placement.
- Split aliases: a partial SMR can use the original prefab N_move2 as live adjustment zero. If authored move matrices coincide, resolve candidates through actual SMR bone ancestry before canonical node names. `accessory_split_move_alias_uses_actual_skin_ancestor` guards this case; native control `native_partial_hair_move_control -- --ignored --nocapture` compares 190／failed191／192 against `Neck317YuukaNative`, and gates all659 static vertices. Keep the failed191 images/report as negative controls; final Stage317 is SESSION192／CLI303.

### Accessory socket ancestry and direct brow UV controls (Stages318/319)

`run_renderer_native.lua --accessory-physics --mesh-names ...` captures the actual selected accessory owners, local Transform ancestry, DynamicBone source settings, colliders, original live particle/private histories, computed SetupParticles parameters, and fixed 0/1/60/240-step results at 60 Hz with installed/no_colliders controls. All original transforms, colliders, and private histories are restored before the renderer snapshot. Output: `.accessory-physics.tsv`. This does not prove arbitrary free-running animation or collider-selection parity.

`-TextureCoverage -MeshNames @('cf_O_mayuge') -CaptureFace` adds mip0 `Unlit/Transparent` GPU RGB coverage over black at real mesh UVs plus a dense 64×64 UV grid. Output: `._MainTex.coverage.uv-samples.{tsv,json,rgba32f}`. The output alpha is blended framebuffer alpha, not texture alpha. `verify_brow_uv_coverage.lua NATIVE_DIR CACHED_INPUT_DIR REPORT.json` first validates native readback PNG direction against GPU coverage, then compares exported alpha at UE's actual `1-v` coordinate. Fixed gates: coverage max .002, alpha max2e-5, authored UV max1e-6.

Stage198's `verify_brow_orientation.lua` only compared PNG rows and did not distinguish direct Texture2D versus MaterialEditor replacement RenderTexture. Do not use its direct-row preference to decide a production flip. Stage319 controls include two bundle sources (Junko/Yuuka), one Sideloader PNG override (Android18), and one MaterialEditor replacement (Ayaka). Only decoded bundle rows reverse for upload; both PNG override paths retain their existing direction. No per-card or brush-shape decision is used.


Stage320 `run_renderer_native.lua --face-create` captures face create inputs at
`RebuildTextureAndSetMaterial`, with distinct call numbers (later partial updates
must not overwrite earlier full inputs), plus the actual face before renderer
controls and KSOX FaceOver/FaceUnder runtime overlays. The pure cloned create
shader omits plugin overlays by design; compare its postfix output and runtime
KSOX layers before attributing missing strokes to paint or eyelid alpha.
Android18 uses legacy KSOX version1 named FaceOver data; the parser now follows
installed `KoiSkinOverlayController.ReadLegacyData`, while version2 still uses
Lookup. Captures are diagnostics, never production asset inputs.

### Stage320/321 eye-rim and independent eyeline material slots

- `-FaceCreate` captures each prefix/create/postfix call independently, plus native KSOX `GetExtendedData` version and `GetOverlayTextures` before camera/isolation controls. `EyeRim320OverlayNativeFinalR3` is the final version1 fixture; earlier partial call captures are diagnostics, not complete create controls.
- `verify_legacy_face_overlay.lua` checks saved FaceOver vs native runtime RGBA, opaque eye-rim pixels, and a baseline that must fail. This isolates a missing legacy layer, not full face color/pose parity.
- `native_eyeline_slot_inputs` reads actual renderer `m_Materials[1]` and the shared source resolver for six cards. `verify_eyeline_shadow.lua` checks slot1 alpha using native float alpha-squared, per-texel RGBA8 conversion bounds, authored/dense UV, and exact UE alpha transport. `verify_eyeline_slots.lua` retains the stricter eighteen-case RGB/alpha report, including known failures; do not replace it with the narrower shadow PASS.
- Final integration uses `compare_eyeline_slot_session_caches`: read-only cached buffers, all17 UV/index/geometry/normals/other textures fixed, changed head PNGs re-resolved from selected sources. `cached_brow_sampling_inputs` supports `OK_BROW_CARD_NAMES`, `OK_BROW_PIPELINE`, `OK_BROW_INPUT_DIR`; the coverage verifier accepts an optional per-row native fixture prefix.

- `audit_capture_regression.lua INVENTORY CAPTURE PRIOR LOG OUTPUT` checks numeric UE/PV/attachment/image gates and records all68 image differences. Visual acceptance belongs in a separate report after reviewing contact sheets and full-size pairs; a numeric PASS alone cannot authorize publishing a visibly wrong batch.
- `verify_legacy_face_overlay.lua --cached-name final321_cached_face.png` validates a later cached face without overwriting the Stage320 baseline fixture. Stage321 final cached brows/shadow/legacy FaceOver controls are separately indexed in `Saved/Logs/Brow321SourceEvidence.json`.
- Brow322CounterNative is an additional counterexample diagnostic only (production remains SESSION197/CLI308). Use `shaderWorldVertices` for native four-weight LBS; plain `worldVertices` has a different basis. The initial missing-brow interpretation was withdrawn after full-size/native review. Forward CullBack/LEqual/Offset0 were read from installed shader resources; do not add no-Z patches based on a thumbnail interpretation.

### Stage322 hair source and expanded capture gates

- `Hair322NativeInputsAudit.json` validates actual cached Koikano hair material colours and null bindings against installed Studio controls. `verify_native_hair_inputs.lua` also checks selected ColorMask/DetailMask GPU samples at mip0: decode the probe's sRGB PNG readback for linear sources, bound encoded texel conversion before bilinear filtering, then propagate the D3D11 coordinate precision interval. This does not establish final lighting, alpha or complete mip parity.
- `inventory_mesh_dumps.lua` rejects dump names without a corresponding source card. Preserve legacy control cards when dump directories are replaced. `audit_four_views.lua --historical-audit PREVIOUS_PASS_JSON` accepts a missing dump only with matching dump identity and source card SHA; the default remains strict.
- `audit_capture_regression.lua --allow-new-cards` records a missing prior image as unavailable, never as a zero difference. Material compilation warnings now fail the numeric gate. `--require-material-readiness` also requires one shader completion record per task, all with zero remaining jobs. Each image SHA binds the numeric report to the captured files.
- ShotPreview waits for runtime shader compilation and render-thread commands before starting camera timers. This is a completion gate, not a shader/colour override. It reports pending jobs before and after the wait; do not infer an earlier visual failure's cause solely from a later zero-job record.
- `publish_capture_batch.lua AUDIT REGRESSION VISUAL BASELINE OFFICIAL ARCHIVE OUTPUT` checks all reports, numeric image hashes and the visual audit hash, verifies the current baseline, copies the old batch into a fresh `OFFICIAL/old/...` archive, verifies it before publishing, then checks every official image SHA. Diagnostic/failed batches must not be published.

### Stage323 Koikano hair gloss coordinates

- `Tools\lua\lua.exe Tools/shader_oracle310/hair_gloss_uv.lua --extract-bundle BUNDLE` extracts installed front/back PS and corresponding VS bytecode, recording source SHA and keyword sets. Disassemble the extracted blobs with FXC, then run the script without arguments to prepare four coordinate controls. `--baseline` prepares the previous shared Forge expression as an expected failing counterexample.
- `compute_oracle.cpp` runs each prepared HLSL on D3D11 WARP and compares the native VS/PS coordinate instruction slice with the exact production string from `KoikanoHairGlossUv.inl`: 512 cases per variant, both taps, out-of-range UV, signed light dot, negative/non-unit vertex and gloss ST. New cases must exit0; each baseline must exit1 because of a UV mismatch, not because compilation failed. The reverse-color variants must retain the same native coordinate instructions.
- This checks coordinate-stage math and source provenance only. It does not execute the original entire pixel shader, prove texture/mip sampling parity, or establish complete Koikano lighting parity. Production selects this path by shader family and `_OLDHAIR_ON`; no card/mesh identity controls it. Forge materials retain their existing coordinate formula.

- ShotPreview also submits prior RHI work and waits for GPU idle before camera timers. `run_dump_cards.lua` requires one material and GPU completion record per card. Numeric review additionally supports `--require-gpu-readiness`; an old successful capture lacking that record must fail this stronger gate. This prerequisite does not prove the cause of an earlier first-view colour mismatch.

- Frozen-pose shots disable actor Tick, so per-shot shadow capture must run independently of Tick. `RefreshShotShadow` captures the current pose into the other target, binds that completed target, and verifies unchanged animation/face clocks; each screenshot then waits for GPU completion. The runner requires one shadow and view completion record per output path. Use `--require-view-gpu-readiness --require-shadow-readiness` for independent numeric review. These gates verify completion and held clocks; existing shadow filtering and lighting still require visual/native review.

- Runtime footwear is exclusive: `ChaControl.DrawClothes` shows slot7 only for `shoesType=0` and slot8 only for `shoesType=1`, also requiring the selected clothing state to be worn. Studio card import uses `noLoadStatus=true` and the `ChaFileStatus` default is outdoor (1); stored card Status is not a footwear request. Use `run_dump_cards.lua -ShoesType 0` for an explicit indoor control. `OK_SHOES_TYPE` participates in the session/extract cache key (SESSION200 / CLI311). Never select a shoe by mesh identity, colour or duplicate-geometry detection.
- `verify_native_footwear_selection.lua NATIVE_RENDERERS DUMP_DIR UE_LOG OUTPUT --card NAME` validates native hierarchy activity and the actual task's source shoe count. It must reject the old two-shoe input block. `renderer.enabled` alone omits inactive parent GameObjects. Coincident native indoor/outdoor geometry is reported as supporting evidence, not used as a production selection rule.
- After all34 captures, run the ignored `compare_footwear_visibility_session_caches` installed test with two threads. It requires the new parts to be an exact ordered subsequence of p199, with only shoe parts removed. Canonicalize material/private-skeleton maps and the two bone-membership sets before comparison; their serialization iteration order is randomized. Keep all ordered arrays and numeric values exact, including floating-point bits. An uncanonicalized bincode mismatch is not proof of changed source values. Retain failed comparison logs separately.

### Stage324 MaterialEditor texture coordinates

- `MaterialTexturePropertyList` has independent nullable `Scale` and `Offset` values. A missing `TexID` or `TextureDictionary` does not disable those edits. Parse each pair into the existing material ST component transport; preserve omitted axes and ignore the `Original` restoration fields. Body bypasses generic `push_meshes`, so replay its ST overrides explicitly before emitting the part. SESSION202 / CLI313 invalidate the earlier incomplete p201 control; bundle102 and the FFI/SessionBlob layout remain unchanged.
- Run the ignored `compare_material_editor_texture_transform_caches` test only after the requested captures finish. It compares p200/p202 texture buffers, ordered geometry, UV channels and palettes, restricts material changes to ST components, and checks the exported ST against material values. `OK_TEXTURE_ST_COMPARE_NAMES` accepts a JSON filename list for a control; `OK_TEXTURE_ST_COMPARE_OUTPUT` sets its report path. Default scope is all34 inventory cards.
- `verify_material_texture_transforms.lua CACHE_REPORT NATIVE_TSV OUTPUT --card NAME` independently checks changed cached ST against native renderer material scale/offset. It verifies inputs, not complete shader/RGB parity. Existing SkinPlus fallback and Siesta's dark side-view neck remain open; do not claim coordinate transport repairs the entire lighting model.
- Native comparisons identify both renderer mesh and material/property; conflicting duplicate native states are rejected instead of silently retaining the last TSV row.
- `Saved/Tests/Skin324InventoryShader.py` maps the extracted `xukmi/SkinPlus` Outline/Forward/ShadowCaster programs, keywords, constant buffers and samplers into `Saved/Logs/Skin324NativeShaderInventory.json`. All 14 profile/index/digest mappings are checked. This is source research only; the production SkinPlus shader remains unported.
- `verify_dump_texture_transforms.lua INVENTORY COMPARISON OUTPUT --identities SOURCE_IDENTITIES` validates changed supported ST against current user material dumps. Match the explicit Unity `mesh` asset, not `gameObject`. Reused accessory assets require source `field`/`slot`; additional renderer draws require the original mesh identity recorded by the source emitter. `trace_texture_transform_source_identities` is an opt-in installed Rust test: source-only ordered extraction must match every cached role/mesh/full material before its original card property/slot is used. Its manifest hook and original-mesh metadata are `cfg(test)` only, with no production ABI/cache change. Do not select native rows using the desired ST values or guessed suffix removal.
- Preserve historical reports: Stage323 cached Megumin comparison selected an indoor control because its SQL exclusion expected a trailing delimiter after the last key field. Stage324 requires exact default-mode key endings and complete keys identical except pipeline. This supersedes the earlier formal-outdoor cache parity claim; native footwear/capture evidence retain their own scopes.
