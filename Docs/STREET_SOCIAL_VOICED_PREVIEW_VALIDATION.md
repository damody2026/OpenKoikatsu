# StreetSocial voiced preview pipeline verification

2026-10-08. Primary-agent implementation and validation, no subagents. Functionality first; no performance optimization or hardware tuning.

## Implementation

Added a self-test-only asynchronous scene probe for the production voiced relationship pipeline. It plays the actual selected NPC's matched greeting audio through QueueNode/production Tick/FinishLine, routed as a relationship preview only inside the probe. It does not alter the common catalogue or claim the greeting is an original voiced friendship event. Production relationship rules and reward values are unchanged.

Two playbacks exercise: early confirmation rejection; complete audio with no automatic save; cancellation after completion with no effect; another complete playback followed by explicit ApplyRelationship; duplicate confirmation rejection; persisted roundtrip. The fixture uses an isolated relationship file and a +3 test effect, restores the original Store/save path/transactions/agents/rotations/player/camera, and does not count fixture effects as regular voiced topics. Clearing only the fixture's relation list keeps common rules while avoiding dependence on any pre-existing favor cap. No APNG or source audio file is changed.

## Evidence

- Development StreetVoicedPreview01 compiled successfully (31.28 seconds), then an immutable stage was generated. Editor StreetVoicedPreviewEditor01 compiled (42.71 seconds). No source changes after the Development snapshot; later PowerShell verifier edits do not change the binary.
- Python 26 tests passed. Unreal Automation-StreetVoicedPreview01: 13 succeeded, zero failed/not-run.
- Actual independent six-APNG street passed: voiced_relationship_preview_checked=true and voiced_relationship_probe_confirmations=1. The independent PowerShell check also reads voiced-confirmation.relationships.json, requires one relation with favor3 and exactly one transaction, and matches its ID to the real prepared APNG participant. This is fixture evidence, not an extra gameplay reward.
- Earlier admission failure/retry, active actor cancellation, camera candidate selection and live activity pause/resume/occupancy checks still passed. One deliberately invalid voice was rejected; errors=[] and active_actor_interrupt_commits=0.
- Normal suite: 15 topic lines, five greetings and five farewells completed; 10 real positive favor commits; persisted favor roundtrip agrees. The count follows this run's original source branch choices; no reward rule or success rate was changed.
- Independent verifier matched all15 topic APNG IDs, personalities, categories and voice-ID sequences to the prepared Chinese/Japanese catalogue. RequireFaceTransitions, RequireShadowProxy and RequireCleanShadows passed. User-save preservation checks passed.
- Actual report: Saved/StreetSocial/Runtime-StreetVoicedPreview01/social.validation.json. Independent audio proof: voice.reward.validation.json. The isolated voiced confirmation file remains beside the report for review.
- Public StreetSocial.cmd receipt now resolves to StreetVoicedPreview01.stage. ValidateOnly passed. Independently checked SHA256: 3b8bd22a8d978bd6fccb4f3f1e279c7e79bd19c5b9f819c427d156088213c11c. Existing common StreetQueueReuse01 patch/catalogue and hundred-person CMD preserved.
- Incremental review: Saved/StreetSocial/VoicedPreviewReview/implementation.diff, 136 lines across three declared source/test paths. Prior work preserved; HEAD c1bc6b589b9e38b80a39a9661bd5e4e1ecd20f09 unchanged. No commit or push.

Current voiced-preview functionality is verified, closing Recovery01's production playback/confirmation test gap. Original ADV friendship/confession stories are still the explicitly scoped text-preview presentation; this test does not port those stories. Full manual GUI/image/listening/content/long-run acceptance remains for the end, full_acceptance=false. Routine harness telemetry is not a new performance claim.
