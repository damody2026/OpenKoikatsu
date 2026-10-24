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
 // Her staging (rules song_choreo): a clip for her line before and after, and cues on the song's clock (seconds);
 // Cue is the one playing (-1: none yet), Staged whether any of her clips was started (then she resumes afterwards).
 // Want is the clip she should be showing, Next the one prepared meanwhile (one streamed action slot: a clip is
 // prepared while another plays, installed once the playing one is let go).
 FString IntroClip,OutroClip,Want,Next;TArray<double> CueAt;TArray<FString> CueClip,CuePart;int32 Cue=-1;bool Staged=false,NextAsked=false;
 // The camera's distance and how far below her head it looks, eased toward each part's framing.
 float CamDistance=-1,CamDrop=10;
 // The sheet as the song model sang it (Simplified): the caption in zh-CN.
 FString LyricsModel;
};
