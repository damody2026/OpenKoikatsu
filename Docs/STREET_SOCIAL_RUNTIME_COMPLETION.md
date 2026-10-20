# Street social runtime action streams

Date: 2026-10-07. Owner of this note: the runtime-stream agent. Root consolidates. This is not a visual or performance acceptance.

Static review only. `OKCrowdRuntimeScene.cpp`, `OKCrowdRuntimeScene.h`, and `OKCrowdRuntimeStream.inl` were not compiled, cooked, or run. A brace scan of `OKCrowdRuntimeScene.cpp` (strings and `//` comments ignored) ended at zero, with no negative depth. The stream include ended at zero braces. That is not a compile.

## Result

`FScene` no longer has one `StreamActor` / `StreamPose`. Two actors can each prepare and play one extra common clip at the same time. A third unpublished request waits. The shared GPU residency budget stays 4 GiB. Quality, shaders, and the F9 CPU/GPU switch are unchanged.

Street already asks for both performers. This agent did not edit `Source/OpenKoikatsu/OKStreetSocialScene.cpp`. On the first voiced clip it calls `OKSpeakCrowdRuntimeActionLine` for `S.Target` with `S.ActiveGesture`. When that target's mouth moves, it calls `OKPlayCrowdRuntimeAction` for character 0 with `talk_gesture_03.okanim`. It counts `FOKRuntimeActionStreamStatus::Playing` on both into `DualActionSeconds`. Those calls are sufficient for one male and one dialogue NPC. They do not by themselves prove that two GPU overlay slots were used: character 0 is admitted every declared action at load and can play a resident clip without a stream slot.

## Exact API

Declared in `Source/OpenKoikatsu/OKCrowdRuntimeScene.h`.

```cpp
inline constexpr int32 OKCrowdRuntimeActionSlots = 2;
inline constexpr int32 OKCrowdRuntimeActionQueue = 8;
inline constexpr double OKCrowdRuntimeActionPrepareSeconds = 60.0;

bool OKPlayCrowdRuntimeAction(AActor* Owner, int32 Character, const FString& ClipId, FString& Error);
bool OKPrewarmCrowdRuntimeAction(AActor* Owner, int32 Character, const FString& ClipId, FString& Error);
bool OKCancelCrowdRuntimeAction(AActor* Owner, int32 Character, FString& Error);
bool OKGetCrowdRuntimeActionStatus(AActor* Owner, int32 Character, FOKRuntimeActionStreamStatus& Status);

struct FOKRuntimeActionStreamStatus {
    bool Pending = false, Preparing = false, Ready = false, Playing = false, Prewarmed = false;
    int32 ActiveSlots = 0, QueuedRequests = 0;
    uint64 Session = 0;
    FString ClipId;
};
```

Kept, and now epoch-aware: `OKResumeCrowdRuntimeBehavior`, `OKSpeakCrowdRuntimeLine`, `OKSpeakCrowdRuntimeActionLine`, `OKStopCrowdRuntimeSpeech`.

`OKPrewarmCrowdRuntimeAction` uses the same admission as play but does not start the clip. A later play of that same clip reuses the published catalog. A clip already in that actor's catalog (`ClipSources.Find >= 2`) returns success without a slot.

`OKGetCrowdRuntimeActionStatus` returns false when the scene is stopped or the character index is invalid. Otherwise:

| Field | Meaning |
|---|---|
| `Preparing` | This character occupies a slot whose epoch is `PendingEpoch`, and the catalog is not installed yet. |
| `Pending` | `PendingAction` is set and the row above is false. A replacement queued behind this character's own installed clip is `Pending`, not `Preparing`. |
| `Ready` | `StreamInstalled` and clip source 2 is a valid index. |
| `Playing` | `ActionOverride >= 2`. This includes a resident preloaded clip. It does not mean an overlay slot is in use. |
| `Prewarmed` | `StreamHold`. The catalog is published and playback was not started. |
| `ActiveSlots` | Scene-wide occupied slots, including a slot that is mid-prepare. Not per character. |
| `QueuedRequests` | Scene-wide entries with `PendingAction != INDEX_NONE`, including requests already sitting in a slot. |
| `Session` | `PendingEpoch` if non-zero, otherwise `InstalledEpoch`. `0` means no queued or installed extra action. |
| `ClipId` | Pending source if any, otherwise the installed slot-2 source. |

`Session` changes on cancel and on a new request. A prepared result from an older session cannot publish or start audio.

## Where the behavior lives

| Piece | Path |
|---|---|
| Slot count, queue, 60 s, public calls | `Source/OpenKoikatsu/OKCrowdRuntimeScene.h` |
| Per-actor epoch, base catalog, queue, restore, report, speech | `Source/OpenKoikatsu/OKCrowdRuntimeScene.cpp` `FEntry`, `QueueRuntimeAction`, `Shutdown`, `Tick`, `WriteReport` |
| Prepare, GPU admit, activate, retire | `Source/OpenKoikatsu/OKCrowdRuntimeStream.inl` `FScene::ServiceStream` |
| Overlay copy | `OKCrowdRuntimeScene.cpp` `FRenderState::Provide` |
| Scratch release | `FRenderState::ReleaseStreamIndex` |
| Shared 4 GiB budget | `Plugins/KKCrowdGpuPacking/Source/KKCrowdGpuPacking/Private/KKCrowdGpuPacking.cpp` `OKGetCrowdGpuResidencyBudget` (`4ull*1024*1024*1024`) |
| Inactive-slot replace rule | `Plugins/KKCrowdGpuPacking/.../OKRuntimeGpuPlayback.cpp` `ReplaceInactiveClip` |
| Pose evaluate rule | `OKResidentGpuPose.cpp` `Evaluate`: playback count must equal the bank actor count; frame index must increase |

Upload acknowledgement is not activation. `AdmitRuntimeAction` may set install status to 1 only after the scratch lease is recorded. `ActivateAcknowledgedRuntimeAction` runs later, after the game thread has swapped that actor's clip catalog and CPU keys. `Provide` reads `Slot.Stream` only after that activate command. CPU mode (`UseCpu`, F9) returns before the overlay and samples the swapped `E.Clips` / `E.CpuKeys`.

## Limits that are intentional

- Two slots. One extra clip per actor. The same actor cannot occupy both slots (`ActionSlotBusy`).
- Queue of 8 counts every `PendingAction`, including a request already inside a slot. Playing a clip whose pending flag was cleared does not consume a queue slot. A ninth unpublished request is rejected and the current pose stays.
- Admission scans character index ascending, not request time. Occupied slots are not preempted. An in-progress one-shot, including its blend (`UsesClip(2)` or `ActionOverride`), finishes before that actor's slot can be replaced.
- Index 0 admits every declared action at load (`Prepare`: `SelectionIndex==0` takes `ActionCount`). Those plays do not take a stream slot. Other characters preload `npc_action_preload_count` (default 0) and stream through slot 2. Street's male gesture can therefore be `Playing` with zero overlay slots while the NPC uses one.
- Each streamed bank is still packed with `OKBuildRuntimeGpuActorBank(..., 128ull*1024*1024)`. That cap was not raised. Each slot then calls `Budget->Reserve(aggregateTasks*96)` and `InitializeManaged` on the shared 4 GiB budget. The second request is dropped if either charge fails. The pose already on screen is kept. The budget and the 128 MiB pack cap were not increased, and quality was not reduced.
- `ReplaceInactiveClip` refuses a clip that is in use or whose id duplicates another id in that actor's catalog. Restore failure retries next frame and does not swap keys. Activation failure is not retried: `StreamErrors` increments once, paired speech is cancelled, and `DiscardRejectedRuntimeAction` releases that GPU index.
- A looping extra clip that keeps `UsesClip(2)` true blocks a new stream for that actor. The four street talk gestures in `Content/IndependentResources/RuntimeAnimations/actions.manifest.json` are `"loop": false`.
- Retire does not set the aggregate `R->Reset`. The other performer's motion history stays. The retiring actor can show one frame of base-bank motion vectors.
- `Provide` still `Fail()`s the whole provider if any overlay evaluate, range, or overlap check fails (`Failures++`, GPU mode off). That matches the old single-slot failure path. F9 cannot return to GPU after that, because `GpuMode` requires `Failures==0`. Palettes, bone maps, private `E.Bank`, and the aggregate composition are not rewritten. `AnimationBounds` only expands.
- If a later slot's `Evaluate` fails after an earlier slot already recorded passes, those overlay copies are left for RDG to cull. The earlier slot's history may have advanced one undisplayed frame.
- Worker key views are still unchecked `*Keys`, same as the old path. Admission only starts after a stage-5 catalog with CPU keys.
- Scene exit can still overlap one previously queued activate with the following `Shutdown` release. Workers are not joined. `ClearActionSlot` drops the game-thread future; the worker owns plain data and cannot publish. `Shutdown` always enqueues `ReleaseStream` before the missing-world return. `RemoveApngProvider` calls `ReleaseStream` again. `ReleaseStreamIndex` zeroes `ScratchLease` after `Retire`, so a second release does not subtract twice.
- CrowdRoom keys 1–8, T, Y, Backspace, and F9 still drive the same actors as before (actions and Backspace: character 0 only). `external_control` skips NPC auto-look streaming. Street sets that flag. CrowdRoom does not, so auto-look may occupy both slots there; that is the old opt-in streaming, now with two seats instead of one.
- Report `action_session` prefers `InstalledEpoch`, then `PendingEpoch`. The status API prefers the opposite. Do not compare them as the same number.
- Report schema stays 1. Added scene fields: `runtime_action_slots`, `runtime_action_queue_limit`, `runtime_action_prepare_seconds`, `runtime_action_active_slots`, `runtime_action_queued_requests`, `runtime_action_stale_drops`. Added character fields: `stream_prepare_ms`, `action_session`, `action_prewarm_held`, `action_stream_installed`. `stream_gpu_dispatches` remains the sum of per-slot overlay evaluates.

## Cancellation

Each entry has `EpochClock`, `PendingEpoch` (0 = none), and `InstalledEpoch`. The slot copies `PendingEpoch` when the job starts.

- `Want` is `Slot.Epoch == PendingEpoch && PendingAction == Slot.Source`.
- `Live` is `StreamInstalled && Slot.Epoch == InstalledEpoch`.
- `!Want && !Live` is stale. Status 0 waits one cycle so the scratch lease exists, then the slot is released and not published. `StaleActionDrops` increments.

`OKSpeakCrowdRuntimeActionLine` links speech only when the paired action is that entry's pending source, or the installed slot-2 source of that same clip. It does not attach an unrelated installed stream. `OKSpeakCrowdRuntimeLine` clears `LinkedActionEpoch` and `SpeechPaired`, then the action-line call writes the link after that clear. `SpeechPaired` is set whenever the resolved action string is non-empty, including a resident catalog pair whose `LinkedActionEpoch` stays 0.

On tick, a speech job whose `LinkedActionEpoch` matches neither `PendingEpoch` nor `InstalledEpoch` is cancelled. Speech still waiting after 60 s is cancelled. On consume, the captured `SpeechEpoch` must still match or the audio is dropped and not played. The no-device branch is the `else` of the device check, inside the ready-job `if`.

`OKCancelCrowdRuntimeAction` clears `ActionOverride`, `PendingAction`, `PendingEpoch`, and `StreamHold`. It cancels speech when `SpeechPaired` or `LinkedActionEpoch != 0`. Speech-only lines are left. The in-use clip blends out on the next ticks; the overlay is released only after `UsesClip(2)` is false and the base catalog restores.

`OKStopCrowdRuntimeSpeech` drops a not-yet-installed paired action (`PendingAction` equal to that speech clip) and always cancels speech. It does not clear an already installed gesture. `OKResumeCrowdRuntimeBehavior` clears the pending epoch and hold, then always cancels speech.

`Shutdown` sets `Stopped`, drops slot futures, zeroes epochs, cancels speech, then enqueues `ReleaseStream`.

## What root should call

No new street call is required for the current male + one NPC pair. Optional, for a gesture that is not already in that actor's catalog:

```cpp
FString Error;
OKPrewarmCrowdRuntimeAction(Owner, Character, ClipId, Error);          // publish, do not play
OKPlayCrowdRuntimeAction(Owner, Character, ClipId, Error);             // play, or upgrade a hold
OKSpeakCrowdRuntimeActionLine(Owner, Character, VoiceId, ActionId, Error);
OKGetCrowdRuntimeActionStatus(Owner, Character, Status);               // completion / session
OKCancelCrowdRuntimeAction(Owner, Character, Error);                   // queued, held, or playing extra action
```

`Character` is the manifest card index. Street male is 0. The dialogue NPC is `S.Target`, not "the nearest CrowdRoom NPC".

To cancel a line that has already installed its gesture, call `OKCancelCrowdRuntimeAction` as well as `OKStopCrowdRuntimeSpeech`. Stop alone leaves the installed clip until it ends.

## Errors

No compiler, linker, cook, or runtime error was observed. Builds and Cook were out of scope. Nothing here is a passing street shot, a GPU capture, or a 120 FPS measurement.

Static review did not find a remaining single-slot `StreamActor` / `StreamPose` / scene-level `BaseClips` in the owned files. Per-entry `BaseClips` / `BaseKeys` / `BaseSource` are the restore copies. No further source edit was required after this review.

## Primary verified integration, CompletionR11

Supersedes the static-only status above for the integrated candidate. Development Completion10 compiled and passed two runtime functional runs; Editor completion16 and AutomationCompletion04 passed. Both male and NPC require private overlay streams in external-control mode; male no longer preloads every action. R11 measured34.008s of simultaneous action playback, male18stream installs, NPC4–5each, zero stream errors. All five NPCs produced real speech frames and mouth updates. Cancellation/restart and relationship save tests passed.

Primary fixed an old-stream hold deadlock: a pending speech action pins only a stream of the same source clip. Added explicit audio terminal states; Failed/Cancelled are not equivalent to Completed and cannot grant dialogue rewards. Evidence: `Saved/StreetSocial/Runtime-CompletionR11/{social.validation.json,runtime.json,game.log}`. Public launcher now points to this verified version. Full visual/performance acceptance remains separate; this is six-person functional evidence, not a100-person benchmark.
