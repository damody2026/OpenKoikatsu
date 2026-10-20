# StreetSocial final implementation audit

2026-10-07. Read-only source audit. No build, cook, Unreal launch, or test was run. Main HEAD `c1bc6b589b9e38b80a39a9661bd5e4e1ecd20f09`. Public receipt `Saved/StreetSocial/Build/receipt.json` records `passed: true`, `full_acceptance: false`, executable `StreetSubmit03.stage`, patch `StreetQueueReuse01.patch`. Those receipt flags are prior records, not results of this audit. Whole-scene, long-run, identity/reload, content-coverage, and visual acceptance remain undone.

Approved scope checked: one controllable male plus five player APNG females, runtime meshes, per-character face and attachments, save identity, cancel/retry, public paths after the CrowdAPNG move, and portable audio selection. Out of scope and not treated as gaps: full ADV confession, other KKS worlds, intimacy animation, invented gesture maps, blush/tears/highlights/gaze/neck IK, and a 100-person street.

## Implementation omission

**Edited card bytes orphan the old relationship. There is no retarget.**

`Tools/prepare_street_social_selection.py` lines 20–23 set `id` to `apng-` plus the SHA-256 of the whole file. The scene never hashes the card. `OKStreetStableId` (`Source/OpenKoikatsu/OKStreetSocialRules.h` lines 289–292) returns `characters[].id` whenever it is non-empty and does not consult `fingerprint`. `OKStreetRelationIndex` (lines 636–642) appends a new stranger when that string is unknown.

Trigger: change any byte of a selected APNG and start `StreetSocial.cmd`. The command always re-runs prepare (`Tools/run_street_social_development.ps1` lines 16–17). The new hash does not match the old save row. The next commit (`OKStreetSocialScene.cpp` `FinishLine` line 199 or `ApplyRelationship` line 234) creates a second relation. The old row stays in the save and nothing links the two ids.

What already works on this path, and is not the gap: rename or move with the same bytes keeps the hash, so the old row is found. Two cards with the same display name do not merge, because the compare is the id (`OKStreetSocialRules.h` lines 636–637). Identical bytes share one id. The save is schema 1 (`OKStreetSocialSchema`, line 34), written by `OKStreetAtomicSave` (lines 1157–1166) as temp, then `.bak`, then replace. `OKStreetWriteSave` (lines 1047–1089) stores that relation id and its stats. It does not store a separate world id or player id. This demo has one street and one player, so those missing fields have no second value and are not a second defect.

There is no reassociation function under `Source/OpenKoikatsu`. A hash change must stay a different person until an explicit retarget. The missing piece is that retarget.

## Checked, not omissions

**Player APNG input.** The default launch is `Saved/StreetSocial/heroines.json` (C000, C019, C044, C022, C068 under `Content/CharacterCards/CrowdAPNG`) plus `output/StreetSocialAPNG/Male01.png`. Those six files exist. `-MaleCard` and `-SelectionFile` accept any other paths. Prepare reads `sex` from the card (`prepare_street_social_selection.py` lines 13–26 and 45–46). The scene requires six manifest characters, slot 0 male, slots 1–5 female (`OKStreetSocialScene.cpp` lines 84–85; `OKStreetIsFemaleTarget` at `OKStreetSocialRules.h` lines 285–286). WASD moves slot 0 (`OKStreetSocialScene.cpp` lines 277–284), not the first female. The mesh is a transient skeletal mesh (`KKRuntimeRenderMesh.cpp` lines 229–231) built from the APNG snapshot. Startup requires the common shader library and material parents (`OKCrowdRuntimeScene.cpp` lines 713–720), not a cooked package per card. The runtime manifest accepts 1..100 card paths (line 698); the street cast of six is the approved demo, not a filename whitelist.

**Cache and runtime generation.** Each entry opens its own APNG (`OKCrowdRuntimeScene.cpp` line 562, `enable_face_patterns` from the manifest at line 703). Pose banks are built per entry (`Prepare`, lines 482–492) with `CharacterIdentity` from that snapshot. No street path loads a per-card cooked character.

**Expressions and attachments.** `QueueNode` resolves the line by that NPC’s personality and applies the result to `S.Target` (`OKStreetSocialScene.cpp` line 180) through that entry’s `MouthStream` (`OKCrowdRuntimeScene.cpp` lines 870–874). Pattern `-1` leaves the card channel. Blend timing is `OKRuntimeFaceTransitionSeconds` (0.15) in `OKRequestRuntimeFace` (`KKRuntimeRenderMesh.cpp` lines 172–176). A rigid part is weighted to its own follow bone (`KKRuntimeMesh.cpp` lines 95–102) and the pose bank samples that part’s bone (`KKRuntimePoseBank.cpp` lines 46–49). Talk does not substitute a shared prop. Male pitch comes from that card’s `voice_rate` (`OKCrowdRuntimeScene.cpp` line 723) and is applied when playback starts (line 629).

**Cancel and retry.** Esc, the in-line 取消 button, audio start failure, a non-completed clip, the 90-second timeout, and `EndPlay` call `EndTalk` (`OKStreetSocialScene.cpp` lines 207–210, 239–243, 288, 303–310, 362). `EndTalk` stops speech, resets the face, resumes both actors, and clears `Held`. `CancelSpeech` drops the decode future, stops audio, and zeroes the mouth (`OKCrowdRuntimeScene.cpp` lines 277–285). `OKResumeCrowdRuntimeBehavior` also clears the action slot (lines 769–772). Queued clip ids are discarded, so later segments do not start. Commit runs only after audio completes, on a copy, and `S.Store` updates only after `OKStreetAtomicSave` succeeds (`FinishLine` lines 193–203, `ApplyRelationship` lines 233–236). A failed save leaves the store unchanged, so the retry applies once. Reload only calls `OKStreetLoadSaveFile` (line 94) and does not replay a transaction. Scene confession and friendship use `FGuid::NewGuid()`, not `stable|event:confession`. After success, `RelationshipPending` is cleared. A later confession on a lover fails `confession_requires_friend` (`OKStreetSocialRules.h` line 1934). A refused confession stays friend and can be attempted again under a new id. The fixed-id helper `OKStreetCommitConfession` (lines 1959–1965) is not what the scene calls.

A later per-character runtime error is only appended (`OKStreetSocialScene.cpp` line 271). It does not call `EndTalk`. Esc and `EndPlay` still release the reservation. That is narrower than the plan’s “character removal / load failure cancels” sentence. Startup rejection happens before a reservation. It is not a second commit path.

**Public paths after the APNG move.** `StreetSocial.cmd`, `Tools/run_street_social_development.ps1`, `Saved/StreetSocial/heroines.json`, `Content/CharacterCards/CrowdAPNG/selection.json`, the hundred-person launcher, and the reader default use `Content/CharacterCards/CrowdAPNG`. No live street, crowd, or reader entry still opens `Content/Characters` or `output/CrowdAPNG`. The male default remains `output/StreetSocialAPNG/Male01.png`, which is present and was not part of the 100-card move. `output/CrowdAPNG` remains only on non-entry tools: `Tools/convert_crowd_apng.ps1`, `Tools/verify_crowd_apng.py`, `Tools/prepare_reader_library.ps1`, `Tools/publish_reader_mask_upgrade.ps1`, `Tools/run_gpu_skin_visual_audit.ps1`.

**Audio selection.** Shipped refs are relative `zh_ogg/` and `ja_ogg/`. `Content/IndependentResources/StreetSocialData/RuntimeV2/gaps.json` records `nonportable_voice_refs: 0` (a file field, not a test run here). Pack files live beside `audio_root` `Content/IndependentResources/StreetSocialData`, for example `zh_ogg/c00/com_ev_00_000.ogg` (present). `RuntimeV2/zh_ogg/...` is not present. The public command does not play the relative catalogue directly. Prepare (`prepare_street_social_selection.py` lines 57–86) resolves `audio_root`, keeps a whole sequence in Chinese only when every Chinese file exists, otherwise leaves the Japanese sequence, and writes absolute project paths. The game admits an ogg only under the declared `voice_root` (`OKAdmitCrowdRuntimeVoice`, `OKCrowdRuntimeScene.cpp` lines 844–852). External control evicts the oldest admission past 256 instead of preloading the catalogue. Nothing in that path opens `KoikatuSunshine` or `voice_extract`. This audit did not listen to any clip.

`OKStreetResolveAgainst` (`OKStreetSocialRules.h` lines 273–276) does not read `audio_root`. It joins a relative ref to the catalogue file’s directory. Pointing `dialogue_catalogue` at `RuntimeV2/catalogue.json` without prepare would look under `RuntimeV2/zh_ogg/` and admission would fail. The public command always prepares first, so that is not the shipping trigger. `OKStreetIsPortableAudioRef` is not called on the play path.

Lines are filtered by the card personality (`OKStreetSocialScene.cpp` line 139). Playback uses the same slot’s Chinese ref, else that slot’s Japanese `path` (`OKStreetSocialRules.h` line 780), unless the node has `voice_language` (lines 907–912). Subtitles stay on `text_zh` (`OKStreetLineLabel`, lines 307–310). Unknown street conditions are not treated as true (`OKStreetEvalAtom` lines 572–583).

## Not this audit

Do not read `receipt.json` `"passed": true` as final acceptance. `full_acceptance` is false. Stage G in `Docs/STREET_SOCIAL_SCENE_PLAN.md` (windowed play, CPU/GPU appearance, cold/warm load, long run, content coverage beyond the six-actor self-test) was not run. Identity rename, move, same-name, and edited-card behavior above is from the code, not from a reload test.

## Next actions

1. Add one explicit retarget: old `apng-<sha256>` to new `apng-<sha256>`, called out of band, never inferred from a hash change. Leave unmatched rows in place. Do not key it by filename, path, roster index, or display name.
2. Do not start the final scene acceptance until that retarget exists, or until root records that an edited card is intentionally a new person with no link.
3. Root runs the existing scripts below. This audit did not.

## Existing validation scripts

Not run here.

- `Tools/run_street_social_development.ps1` `-ValidateOnly` checks the receipt inputs on disk. `-SelfTest` launches the offscreen six-actor scene. `StreetSocial.cmd` forwards arguments to that script.
- `Tools/test_street_social_runtime.ps1` prepares a manifest and runs `-StreetSocialSelfTest`. It asserts the functional report, GPU ready, `cooked_character_loaded` false, expressions, cancel, and relationship flags. Optional switches: `-RequireFaceTransitions`, `-RequireCleanShadows`, `-RequireShadowProxy`.
- Automation filters already in tree: `OpenKoikatsu.StreetSocial.SourceExpressions` and `OpenKoikatsu.StreetSocial.RulesAndRealCatalogue` (`Source/OpenKoikatsu/Tests/OKStreetSocialRules.spec.cpp`); `OpenKoikatsu.RuntimeMesh.FacePatterns` and `OpenKoikatsu.RuntimeMesh.FaceTransitions` (`Plugins/KKRuntimePose/Source/KKRuntimePose/Private/KKRuntimeFace.spec.cpp`).
- Python, no Unreal: `Tools/tests/test_street_social_catalogue.py`, `Tools/tests/test_street_social_expressions.py`.
- `Tools/EnginePatches/VsmQueueReuse/check_vsm_queue_reuse.py` is a static shader check. It writes under `.ai-collab` and does not replace the engine shader. The public patch remains `Saved/StreetSocial/Build/StreetQueueReuse01.patch`.

## Root follow-up correction

The durable VSM checker is now Tools/EnginePatches/VsmQueueReuse/check_vsm_queue_reuse.py; its generated durable outputs stay in that directory, not .ai-collab. The original report's path description was stale. Approved plan 8.2 identity reassociation and active-actor error cancellation are being completed through the separate native retarget implementation. Source implementation alone is not acceptance; current evidence is maintained in STREET_SOCIAL_FINAL_FUNCTIONAL_VERIFICATION.md.
