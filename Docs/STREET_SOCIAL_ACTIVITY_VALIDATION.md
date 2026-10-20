# StreetSocial activity functionality validation

2026-10-08. Implemented and verified directly by the primary agent, with no Grok or other subagent delegation in this round. User priority is functionality first; no performance optimization or performance acceptance was performed.

## Implementation

Arrival now records DwellPoint separately from the advanced route Cursor. Looking/resting state follows the arrived stop until expiry. Held pauses its timer and preserves the remaining activity; release restores the original state, and expiry clears occupancy.

Activity occupancy derives from runtime agents and the shared scene route graph. An existing visitor or a person standing on the point reserves it. Approaching competitors use deterministic slot priority within the entry region. Waiting retains its route destination and clears stuck accumulation, rather than skipping an occupied activity point. Departed visitors release it. No card names, fingerprints, personal Cook or special-case avatars are involved. Seat points retain the existing resting/idle animation; this change does not introduce an authored seated clip or eye/neck tracking.

## Independent validation

- Saved incremental review diff: Saved/StreetSocial/ActivityReview/implementation.diff (242 lines before the final documentation update). Only declared source/test paths changed for this feature; prior work preserved.
- Development StreetActivity01 compiled. The live probe was subsequently strengthened to call production BeginTalk/EndTalk, so a fresh StreetActivity02 build (20.05 seconds) and immutable stage were used for all final evidence, rather than accepting the earlier snapshot. Editor StreetActivityEditor02 compiled (42.63 seconds).
- Python 26 tests passed. Unreal Automation-StreetActivity02 passed 12 tests, including ActivityStops across Look/Seat/Rest points, current-stop state after route advancement, reservation pause/resume, expiry release, occupied waiting without stuck skipping, simultaneous arrivals and a player occupying a stop.
- Actual runtime: one male and five APNG female actors in the independent scene. activity_state_checked=true and candidate_selection_checked=true. The activity probe is driven by successive production Tick updates, using scene route point17 and actual scene agent records. It calls production BeginTalk and EndTalk while a visitor is looking, verifies timer preservation and unchanged relationship/transaction state, resumes the stop, checks expiry, occupied waiting, then release and arrival by the waiting actor. Agent records, positions, rotations and camera/player state are restored before the ordinary dialogue suite. Probe teleports are self-test only.
- All 15 topic lines, five greetings and five farewells completed. Eight actual positive favor commits; favor_save_verified=true; active actor interruption committed zero rewards; errors=[]. All 15 topic APNG/personality/category/voice-ID sequences independently matched the prepared Chinese/Japanese catalogue. Positive counts are outcomes of this particular source-branch run, not a new success rate or changed reward rule.
- RequireFaceTransitions, RequireShadowProxy and RequireCleanShadows passed. Runtime report: Saved/StreetSocial/Runtime-StreetActivity02/social.validation.json; voice proof: voice.reward.validation.json. Tests use independent relationship saves; the user-save preservation checks passed.
- Public StreetSocial.cmd receipt now resolves to Saved/StreetSocial/Build/StreetActivity02.stage/Windows/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe. SHA256: bcf29041601fec15b33d9a20fec09aeb34be2731a34b46845085beecea953611. Launch ValidateOnly passed and the receipt hash was independently checked. Common patch/catalogue and the original hundred-person CMD remain as before. HEAD unchanged: c1bc6b589b9e38b80a39a9661bd5e4e1ecd20f09. No commit or push.

Current activity functionality is accepted. Full manual GUI, visual/audio review and long-run acceptance remain for the end, as requested; full_acceptance=false. Routine telemetry produced by the existing functional harness is preserved in raw evidence, but no performance conclusion is drawn.
