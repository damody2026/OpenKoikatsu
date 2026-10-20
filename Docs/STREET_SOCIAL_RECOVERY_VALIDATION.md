# StreetSocial dialogue recovery validation

2026-10-08. The primary agent implemented, reviewed, built and tested this directly. No subagents, performance optimization, hardware-specific changes, commit or push in this round.

## Production behavior

Voiced relationship previews retain their pending effect after complete audio playback and require explicit confirmation before saving. The same confirmation gate serves UI and ApplyRelationship. Queued/incomplete/failed audio, an active line, missing target, and cancelled pending state cannot commit. Original text ADV previews keep explicit confirmation. This does not add or replace the original ADV story with unrelated audio.

All node/voice admission failures, playback failures and timeout use FailTalk. It cancels speech, resets the expression, resumes both private action streams, releases the NPC and clears pending relationship/branch effects. A separate retry notice survives ordinary nearby-candidate UI updates and clears on a new eligible interaction. Technical failure details remain in the log and error report.

## Verified evidence

- Development StreetRecovery01 succeeded in 19.91 seconds; Editor StreetRecoveryEditor01 succeeded in 42.93 seconds. Immutable stage was created from that Development build, without editing source after its snapshot.
- Python: 26 tests passed. Unreal Automation-StreetRecovery01: 13 succeeded, zero failed/not run. New RelationshipVoiceConfirmation exercises incomplete, active, cancelled, missing-target, completed voiced, and text-preview confirmation rules. Existing source catalogue, identity, candidate/activity, face, skin envelope and shadow regressions remain covered.
- Actual self-made street: one male plus five runtime APNG female actors. dialogue_recovery_checked=true; expected_voice_admission_failures=1. The probe admits a real greeting, rejects early relationship confirmation, changes only that node's in-memory voice reference to an invalid path, calls production QueueNode, restores the original reference, checks reservation/pending cleanup and unchanged store/transaction/save-file bytes, rejects post-abort confirmation, then successfully calls production BeginTalk again. The retried greeting is cancelled before playback/reward. Agent/rotation/player/camera state is restored. The deliberate admission rejection is labelled in the log; no production errors are suppressed.
- Normal actual playback then completed 15 topic lines, five greetings and five farewells. 11 positive favor commits, persisted roundtrip checked, zero rewards on active-actor interruption, errors=[]. This positive count is the outcome of source branch choices in this run, not a changed reward rule.
- Independent verifier matched all 15 topic actor APNG IDs, personalities, categories and voice-ID sequences to the prepared Chinese/Japanese catalogue. Saved evidence: Saved/StreetSocial/Runtime-StreetRecovery01/social.validation.json and voice.reward.validation.json.
- RequireFaceTransitions, RequireShadowProxy and RequireCleanShadows passed. Candidate and live Tick activity pause/resume/occupancy checks still pass. The test uses isolated relationship saves and validates preservation of the user save. Cards and common audio were not modified.
- Public StreetSocial.cmd now follows the verified StreetRecovery01.stage receipt. SHA256: a85604b07d3a5ef8353e98d770a3a1d8dd5dd155086e54fd4b0a8fcbf638d4ea. ValidateOnly passed, and the published executable hash was independently recomputed. Existing common StreetQueueReuse01 patch/catalogue and the hundred-person CMD are preserved.
- Incremental owned-source review: Saved/StreetSocial/RecoveryReview/implementation.diff, 259 lines. Five explicit backup paths were used; prior changes were preserved. HEAD remains c1bc6b589b9e38b80a39a9661bd5e4e1ecd20f09.

## Acceptance limits

The native rule test confirms the voiced relationship gate, and the actual scene verifies failure/cancel/retry plus ordinary voiced playback. This run does not claim an end-to-end playback of a real voiced friendship source event; the existing source friendship/confession cases are text ADV previews, whose confirm/save tests still pass. Full manual GUI, image and listening review, and long-run acceptance remain for the end as requested. full_acceptance=false. Existing functional harness telemetry is retained without a new performance conclusion.

2026-10-08 follow-up: the production playback/explicit-confirmation gap above is now verified using an isolated matched-audio fixture. See STREET_SOCIAL_VOICED_PREVIEW_VALIDATION.md. This follow-up does not claim to port the original friendship ADV story.
