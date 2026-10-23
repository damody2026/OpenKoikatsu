// Heroine song event state (OKStreetSocialSong.inl; Docs/HEROINE_SINGING_IMPLEMENTATION_PLAN.md). One member of the scene
// state, so the event's fields stay here instead of being spread over the scene.
#pragma once

#include "CoreMinimal.h"

// The game event, separate from the ok-song job: a ready song is not a song being sung.
enum class EOKSongPhase : uint8 { Idle, Preparing, Ready, Offered, Intro, Singing, Outro, Failed };

struct FOKSongState {
 EOKSongPhase Phase=EOKSongPhase::Idle;
 // ok-song service (OK_SONG_ENDPOINT, set by the launcher); empty: no songs in this run.
 FString Endpoint;
 // Automatic songs are off until the player turns them on (saved in songs.json); the control channel can still test.
 bool Enabled=false,Loaded=false,Override=false,RequestPending=false,PollPending=false,SayPending=false,SpeechStarted=false;
 // The locked singer and this event's identity; callbacks check Epoch (bumped by every reset, slot load and new event).
 int32 Girl=INDEX_NONE;uint32 Epoch=0;
 FString SaveId,EventId,RequestId,JobId,HeroineId,VoiceTag,ArtifactKey,JobStage,Template,Lyrics,SongPath,SongVoiceId,Notice,LastReason;
 TArray<float> Mouth;float Hop=.01f;
 // Times: SceneTime() seconds; minutes of play for the rules' cooldowns.
 double NextCheck=0,PhaseAt=0,LaterUntil=0,LastSongAtMinutes=-1,DeclinedAtMinutes=-1;
 int32 SungDay=-1,SungToday=0;
 // Her singing shot (SongCameraShot): when it began (-1: not on) and where the camera eased to.
 double CameraSince=-1;FVector CameraAt=FVector::ZeroVector;
};
