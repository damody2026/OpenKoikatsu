# Relationship automatic commit

## Plan
1. Remove the manual relationship confirmation UI.
2. Commit and atomically save at the natural end of the source dialogue, keeping the last subtitle.
3. Verify acceptance, refusal, cancellation, duplicate prevention and persisted reload.

## Error record
- The old flow finished the source confession dialogue but left its effect pending until an extra confirmation button was pressed. This made a visibly accepted confession remain a friendship.
- Completion guards must remain: queued, playing, cancelled and failed audio must never commit. All source dialogue parts must finish before automatic persistence.
- Runtime tests previously asserted manual confirmation; update them to require automatic persistence before any explicit apply call.

## Implementation
- Both multi-part source ADV and single voiced relationship completions call the shared guarded atomic commit. Text-only friendship uses the same path.
- Preserve both speakers’ final subtitle and refresh the live relationship controls immediately.
- No user confirmation button remains. Repeated completion/apply is inert after success.

## Validation
- Development game build: succeeded; isolated executable SHA256 `eca9f3255161321a616061560f060067367b73df243fae520042cf936a205de4`.
- 70 StreetSocial tests and 11 shader code generation tests passed.
- Runtime-StreetRelationship25Context: 20 original dialogue question/reply pairs, 5 automatic accepted confessions, 5 automatic refusals; cancellation, early commit rejection, exactly-once budget and disk reload verified for every girl. Independent original asset and 22 source voice hashes passed.
- Runtime-StreetRelationship25Presentation: 5 standing actors, 6 running actors, 15 exclusive live relationship controls passed, FXAA at 150% internal rendering.
- Runtime-StreetRelationship25Touch: 20 authored body reaction cases passed.
- StreetRelationship25C019/C000/C044: 9 source-geometry GPU/CPU views, all passed with max pixel error zero.
- Runtime-StreetRelationship25FullRetry: full integrated acceptance passed, no errors; 15 original street dialogue lines, 15 frozen CardReader high-model roundtrips, all common activities, single-voice automatic commit/cancel/reload and exactly-once transaction verified.
- PowerShell ValidateExisting post-check: passed on the same completed runtime after correcting its stale fixture filename.
- Independent publisher: 848 source voice hashes, 1230 dialogue rows, 270 touch rows, original confession scripts/22 voices, phone resources and 15 high-model contacts all passed.
- Published receipt: Saved/StreetSocial/Build/receipt.json now selects StreetRelationship25.stage, with runtime_relationship_auto_commit=true. Prior receipt preserved by the atomic publisher.
- Final launcher integrity check: Tools/run_street_social_development.ps1 -ValidateOnly passed and resolved the published StreetRelationship25 executable.

## Tooling error
- Windows rg does not expand a path such as Tools/verify_street_social*.py. Use rg --files to discover paths, or search the Tools directory with -g instead.
- A guessed verifier filename did not exist. Discover filenames with rg --files before reading them; do not infer tool filenames from their purpose.

## Runtime environment error
- First full regression run failed with High interaction preparation timeout before entering the first high-model scene. Its owned host log stopped at AutoSDK ValidatePlatforms; Build.bat was waiting for the shared TMP lock held by a separate root editor build. This was not a confession failure. Retain the failed evidence under Runtime-StreetRelationship25Full.
- Retry uses an isolated TMP/TEMP directory scoped only to its test supervisor and owned child processes. It does not terminate the other build, change engine scripts, modify the frozen high host, or alter the accepted launcher.

## Validation tooling correction
- The retry game completed all runtime assertions with passed=true and no errors. The PowerShell post-check still referenced the old voiced-confirmation.relationships.json filename after the C++ fixture had been renamed to voiced-auto-commit.relationships.json. Fixed that stale reference.
- Added ValidateExisting to rerun the unchanged post-checks on the same completed evidence, requiring exit=0 and matching executable path, manifest, patch and test mode from the original log. This avoids replaying already successful long-running game tests for a validator-only filename correction.

## Planning investigation follow-up
- During the shared interaction mode investigation, an rg path argument containing a wildcard failed on Windows. Use a real directory with -g file filters, or discover paths with rg --files. No application source or launch configuration was changed during this planning review.

