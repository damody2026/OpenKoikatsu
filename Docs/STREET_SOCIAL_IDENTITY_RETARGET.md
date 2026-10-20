# StreetSocial identity retarget

Explicit reassociation for an edited APNG. A changed file hash is a different person until the caller names both production ids. Nothing in this path infers identity from a file name, path, roster index, display name, or appearance.

This note describes the source that was added. Unreal was not compiled or launched while writing it. `full_acceptance` stays false.

## Identity

A production id is `apng-` plus 64 lowercase hex digits, the same form as `hashlib.sha256().hexdigest()`. Uppercase hex, a short hash, a `session-` id, a fingerprint, and a card path are rejected.

`new_id` must be one of the currently selected female manifest ids. `old_id` must not equal any currently selected actor, male or female. Rows whose ids are not part of the request stay in the save. There is no automatic reassociation.

## Store API

`OKStreetRetargetRelations` in `Source/OpenKoikatsu/OKStreetSocialRules.h` is copy-on-write. The caller's store changes only if that caller assigns the result. Every request is validated before any row moves. One bad request publishes nothing.

A move copies the relation and changes only `Id`. Favor, lewdness, stage, talks remaining, day, talked, flags, committed, topics, and cooldowns stay. An existing destination row is `destination_collision`: it is not overwritten or merged. A missing old row is `unknown_old_id`. The same `request_id` with the same pair is an idempotent replay: no second ledger row and no second move. The same `request_id` with a different pair is `request_id_mismatch`. `request_id` is 1–64 characters of `A-Za-z0-9_-` and must not start with `session-`. A batch is at most five requests. Chains and duplicate endpoints inside one batch are refused.

## Save

Schema stays 1. `identity_retargets` is omitted when empty, so an old save still loads and a later save of an untouched store does not grow a ledger. When present, each entry is exactly `old_id`, `new_id`, `request_id`, and `time`. A malformed ledger rejects the whole save. A later save keeps the ledger. The scene writes one atomic save before it replaces the live store, and only when at least one row moved. A failed save leaves the previous store and does not start the runtime. A pure replay does not rewrite the file.

## Launch

Default `Tools/run_street_social_development.lua` is unchanged and does not retarget.

`-RetargetFile` is the caller's authorization. The file is either a JSON array of `{request_id, old_id, new_id}` or an object whose only field is `identity_retargets`. The file must be 1–65536 bytes and contain 1–5 requests. After prepare, the script copies those requests into the new manifest. It checks `new_id` against the prepared female ids and rejects an `old_id` that is still selected. It does not read or write `Saved/StreetSocial/relationships.json`. The game is the authority: it loads the save, applies the manifest requests, and atomic-saves only if a move is new. `-ValidateOnly` checks the request file and does not launch. `-RetargetFile` cannot be combined with `-SelfTest`.

## Isolated scene check

`Tools/test_street_social_runtime.ps1 -Tag <fresh> -RetargetSelfTest` builds `fixture.relationships.json` under `Saved/StreetSocial/Runtime-<Tag>/`, points the manifest `save_path` at that fixture, and passes `-StreetSocialRetargetSelfTest` instead of the 15-line self-test. The fixture's old id is a synthetic `apng-` hash that is not on the roster. Its new id is the first prepared female. The game refuses that mode if the save path is `Saved/StreetSocial/relationships.json`.

The scene applies the move, atomic-saves the fixture, applies the same request again in process, then starts the six APNGs. It leaves when all six are visible. It does not play dialogue. Report counters for a fresh fixture are `identity_retarget_applied` 1, `identity_retarget_replayed` 1, `identity_retarget_rejected` 0, `active_actor_interrupts` 0, and `active_actor_interrupt_commits` 0. Rules loaded before the save may add step-up; the relation fields above are what the fixture must still contain. Repeating the same persisted request does not move the row again.

## Active-actor interrupt

While a talk is active, slot 0 and the talk target are sampled. `OKGetCrowdRuntimeCharacterStatus` sets `Ready` only at stage 5 and `Visible` only after the three-second reveal. `Visible` is not frustum culling. An empty error with `Ready` or `Visible` still false is loading or fade and does not end the talk. A query miss before the actor has been ready does not end it either.

The talk ends immediately when that male or target has a non-empty runtime error, when a previously ready actor's query fails or `Ready` drops, or when a previously visible actor's `Visible` drops. `EndTalk` stops both actors' speech, clears both action slots, drops queued clip ids, releases the target, and does not commit a reward. Other NPC errors are still appended and do not end the current talk.

The existing dialogue self-test arms one synthetic fault on the first spoken line of NPC 1. The next tick uses that same helper. The following self-test frame checks that the target is clear, speech is cancelled, the save is unchanged, and `active_actor_interrupt_commits` is 0, then restarts that NPC. The synthetic fault is not copied into `errors`. The default runtime script now also requires `active_actor_interrupts >= 1` and `active_actor_interrupt_commits == 0`.

## Root validation

Unreal was not run here. After root compiles:

```text
OpenKoikatsu.StreetSocial.IdentityRetarget
```

The existing `OpenKoikatsu.StreetSocial.RulesAndRealCatalogue` and `OpenKoikatsu.StreetSocial.SourceExpressions` filters are unchanged.

Runtime, only after that compile, with a fresh tag:

```text
powershell -NoProfile -File Tools/test_street_social_runtime.ps1 -Tag <fresh> -RetargetSelfTest
powershell -NoProfile -File Tools/test_street_social_runtime.ps1 -Tag <another-fresh>
```

Request-file check, no launch:

```text
Tools\lua\lua.exe Tools/run_street_social_development.lua -ValidateOnly -RetargetFile <requests.json>
```

A real retarget, only when the caller means to rewrite the live save through the game:

```text
Tools\lua\lua.exe Tools/run_street_social_development.lua -RetargetFile <requests.json>
```


## Root verified implementation

The earlier "not compiled or launched" statements describe the worker handoff, not the final result. Root subsequently compiled Development/Editor, passed nine native automation tests, the six-APNG migration and complete dialogue runtime tests, and three persisted replay/rejection launches. Evidence and timings: Docs/STREET_SOCIAL_FINAL_FUNCTIONAL_VERIFICATION.md. The native migration API and interruption path are accepted; full_acceptance remains false.

Reusable persisted replay check after a successful isolated RetargetSelfTest:

```text
Tools\lua\lua.exe Tools/test_street_social_retarget_persistence.lua --tag <fresh> --evidence Saved/StreetSocial/Runtime-<migration-tag> --executable <verified-exe> --patch <common-patch>
```

This tool creates its own fixtures under Saved/StreetSocial, never rewrites the original migration evidence or the user relationship save, and verifies disk replay and rejected collisions through native game startup.
