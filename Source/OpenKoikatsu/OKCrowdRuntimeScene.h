#pragma once
#include "CoreMinimal.h"
class AActor;
// Selection manifest contains APNG paths and common animation curves only.
// No imported character or per-card cooked resource is consulted.
bool OKStartCrowdRuntimeScene(AActor* Owner,const FString& Manifest,FString& Error);
// Keep the admitted world and exact playback state resident during a hosted scene.
bool OKSuspendCrowdRuntimeScene(AActor* Owner,bool Suspended);

// Game-thread commands. IDs are admitted common curve filenames; each actor
// retains its private prepared poses. One-shot actions return to behavior.
// On-demand gestures share the existing 4GiB GPU residency budget. Two actors
// can prepare and play at once. A third waits. At most eight unpublished
// requests are queued. Preparation that is still unpublished after 60 seconds
// is dropped and cannot activate later.
// Streamed action slots for the whole scene: two for what he does with a heroine (both of them), two for the heroines
// around going about their day (eating, reading) meanwhile. Each slot's GPU scratch is taken from the budget when used.
inline constexpr int32 OKCrowdRuntimeActionSlots=4;
inline constexpr int32 OKCrowdRuntimeActionQueue=8;
inline constexpr double OKCrowdRuntimeActionPrepareSeconds=60.0;
bool OKPlayCrowdRuntimeAction(AActor* Owner,int32 Character,const FString& ClipId,FString& Error);
// Same admission as play, but do not start the clip. A later play of that clip
// reuses the published catalog. Already-admitted clips are a no-op success.
bool OKPrewarmCrowdRuntimeAction(AActor* Owner,int32 Character,const FString& ClipId,FString& Error);
// Configure a common contact recipe before this clip is first admitted. Immutable per actor/clip.
bool OKConfigureCrowdRuntimeActionContact(AActor* Owner,int32 Character,const FString& ClipId,const FString& Recipe,FString& Error);
// Drop this character's queued, preparing, prewarmed, or playing extra action.
// A paired speech for that action is cancelled. Unrelated speech is left alone.
// An in-use clip blends out, then its private overlay is released.
bool OKCancelCrowdRuntimeAction(AActor* Owner,int32 Character,FString& Error);
bool OKResumeCrowdRuntimeBehavior(AActor* Owner,int32 Character,FString& Error);
// Let go of the extra action she shows (back to her behavior) but keep a queued or prepared one and every speech: the
// next clip installs once the slot is free (a song sung over a sequence of clips).
bool OKReleaseCrowdRuntimeAction(AActor* Owner,int32 Character,FString& Error);
// Session is 0 when this character has no queued or installed extra action.
// Cancel and a new request both change it. A prepared result from an older
// session cannot publish or start audio on the replacement.
struct FOKRuntimeActionStreamStatus {
 bool Pending=false,Preparing=false,Ready=false,Playing=false,Prewarmed=false;
 int32 ActiveSlots=0,QueuedRequests=0;
 uint64 Session=0;
 // Current admitted action clock, including the incoming side of a blend.
 float PlaybackSeconds=0,DurationSeconds=0;
 bool Transitioning=false;
 FString ClipId;
};
bool OKGetCrowdRuntimeActionStatus(AActor* Owner,int32 Character,FOKRuntimeActionStreamStatus& Status);

// Asynchronous common OGG playback; mouth rate follows the audio render clock.
bool OKSpeakCrowdRuntimeLine(AActor* Owner,int32 Character,const FString& VoiceId,FString& Error);
// A song: an admitted OGG like a line, but the mouth follows MouthEnvelope (her vocal alone, one value per HopSeconds,
// read at the audio render clock) instead of the mix, and it plays at pitch 1. Stop / active / state as a line.
bool OKSpeakCrowdRuntimeSong(AActor* Owner,int32 Character,const FString& VoiceId,const TArray<float>& MouthEnvelope,float HopSeconds,FString& Error);
// Plays PCM that is still arriving (a streamed TTS response); same speech state, mouth and stop as a line.
bool OKSpeakCrowdRuntimeStream(AActor* Owner,int32 Character,TSharedPtr<struct FKKRuntimeSpeechStream,ESPMode::ThreadSafe> Stream,FString& Error);
// Prepare audio in the background, then start the admitted action and voice together.
bool OKSpeakCrowdRuntimeActionLine(AActor* Owner,int32 Character,const FString& VoiceId,const FString& ActionId,FString& Error);
// Stop active audio and discard pending decode; mouth restores on the next tick.
bool OKStopCrowdRuntimeSpeech(AActor* Owner,int32 Character,FString& Error);
// The heroine he is on the phone with (INDEX_NONE: nobody): her lines play unattenuated, not from where she stands.
void OKSetCrowdRuntimeSpeechRemote(AActor* Owner,int32 Character);
// 0 restores authored eyes; 1 native closed eyelids; -1 resumes automatic blink.
bool OKSetCrowdRuntimeBlink(AActor* Owner,int32 Character,float Closure,FString& Error);
float OKCrowdRuntimeSpeechMouth(AActor* Owner,int32 Character);

// Opt-in external control for independently authored gameplay scenes.
struct FOKRuntimeCharacterStatus {
 FString Path,Error,ActiveClipId;
 bool AnimationTransitioning=false;
 FTransform Transform=FTransform::Identity;
 bool Ready=false,Visible=false,ActionsReady=false;
};
bool OKGetCrowdRuntimeCharacterStatus(AActor* Owner,int32 Character,FOKRuntimeCharacterStatus& Status);
// Sparse gameplay query, sampled from this actor's private current animation.
// Does not read back GPU buffers or enable full CPU pose evaluation.
bool OKGetCrowdRuntimeBonePosition(AActor* Owner,int32 Character,FName Bone,FVector& World);
bool OKGetCrowdRuntimeBoneTransform(AActor* Owner,int32 Character,FName Bone,FTransform& World);
bool OKSetCrowdRuntimeCharacterMotion(AActor* Owner,int32 Character,const FTransform& Transform,float Speed,FString& Error);
// Admit only files below manifest voice_root; voice IDs can then use existing playback APIs.
bool OKAdmitCrowdRuntimeVoice(AActor* Owner,const FString& Path,FString& VoiceId,FString& Error);
bool OKCrowdRuntimeSpeechActive(AActor* Owner,int32 Character);
enum class EOKRuntimeSpeechState:uint8 {Idle,Preparing,Playing,Completed,Cancelled,Failed};
bool OKGetCrowdRuntimeSpeechState(AActor* Owner,int32 Character,EOKRuntimeSpeechState& State,FString& Error);
// The components drawing one character (a portrait capture shows only these).
void OKGetCrowdRuntimeCharacterComponents(AActor* Owner,int32 Character,TArray<class UPrimitiveComponent*>& Out);
// Seconds of her current speech played so far, and its length (Total < 0: a stream still arriving). False: none plays.
bool OKGetCrowdRuntimeSpeechPlayback(AActor* Owner,int32 Character,double& Played,double& Total);
// Refresh diagnostic evidence after external gameplay, without ending the scene.
bool OKCaptureCrowdRuntimeSceneReport(AActor* Owner);

// Private card-authored expression channels; no shared-character morph data.
struct FKKRuntimeFaceState;
bool OKSetCrowdRuntimeFaceExpression(AActor* Owner,int32 Character,const FKKRuntimeFaceState& Face,bool AllowBlink,FString& Error);
bool OKResetCrowdRuntimeFaceExpression(AActor* Owner,int32 Character,FString& Error);
// What the face patterns cannot show, on her materials: cheek blush and sweat 0..1 (-1 leaves the card's), the iris
// offset (-1..1, x to her left on screen, y down) and the eye highlight (1 on, 0 off, -1 the card's).
// Tears: 0 none, 1..3 the source's small / medium / large (tearsLv), -1 leaves them as they are.
// FadeScale (dev, > 0): stretches how long the look takes to ease in, to watch a change frame by frame.
struct FOKRuntimeFaceLook {float Blush=-1.f,Sweat=-1.f;FVector2D Gaze=FVector2D::ZeroVector;int32 Highlight=-1,Tears=-1;float FadeScale=-1.f;};
bool OKSetCrowdRuntimeFaceLook(AActor* Owner,int32 Character,const FOKRuntimeFaceLook& Look,FString& Error);
// The look drawn now (Tears: the level most shown, its visibility in TearShown) and whether it is still easing.
struct FOKRuntimeFaceLookNow {float Blush=0.f,Sweat=0.f,Highlight=1.f,TearShown=0.f;FVector2D Gaze=FVector2D::ZeroVector;int32 Tears=0;bool Fading=false;};
bool OKGetCrowdRuntimeFaceLook(AActor* Owner,int32 Character,FOKRuntimeFaceLookNow& Now);
// Her head turns to a world point (her neck and head over whatever clip plays, within a natural range), easing in and
// out; Enabled false lets her head follow the clip again.
bool OKSetCrowdRuntimeHeadLook(AActor* Owner,int32 Character,bool Enabled,const FVector& World);
// Her outline shells drawn or not (a capture that must not see them, e.g. the galgame portrait's coverage); the change
// reaches the renderer in order with the captures queued between two calls.
void OKSetCrowdRuntimeOutlinesShown(AActor* Owner,int32 Character,bool Shown);
// Development: one material parameter on every material of a face-look kind (1 face skin, 2 skin, 3 iris, 4 tear, 5 brow);
// answers how many materials took it.
int32 OKSetCrowdRuntimeLookParameter(AActor* Owner,int32 Character,int32 Kind,const FString& Name,const FLinearColor& Value,bool Vector);

struct FOKRuntimeFaceStatus {bool Active=false;uint64 Requests=0,Updates=0,Restores=0,Started=0,Intermediate=0,Completed=0,Cancelled=0;};
bool OKGetCrowdRuntimeFaceStatus(AActor* Owner,int32 Character,FOKRuntimeFaceStatus& Status);

// Permanent base poses; streamed actions retain priority.
bool OKSetCrowdRuntimeLocomotionState(AActor* Owner,int32 Character,bool Conversation,bool Running,FString& Error);
// Standing still: the reading idle (book) when true, the plain standing idle when false (default true).
bool OKSetCrowdRuntimeIdleReading(AActor* Owner,int32 Character,bool Reading);
// Standing still in one of her own standing poses (manifest idle_variants; INDEX_NONE: the idle above). Answers how
// many she has (0 before she is loaded or when the manifest names none).
int32 OKSetCrowdRuntimeIdleVariant(AActor* Owner,int32 Character,int32 Variant);

struct FPostProcessSettings;
// Apply the admitted CardReader display LUT to gameplay and diagnostic cameras.
bool OKApplyCrowdRuntimeDisplayProfile(AActor* Owner,FPostProcessSettings& Settings);

// Match CardReader camera-attached character light; hair retains its authored light.
void OKUpdateCrowdRuntimeDisplayLighting(AActor* Owner,const FVector& LightDirection);
void OKSetCrowdRuntimeRamp(AActor* Owner,const FString& Key);
void OKSetCrowdRuntimeStudioLight(AActor* Owner,const FVector& Light);
