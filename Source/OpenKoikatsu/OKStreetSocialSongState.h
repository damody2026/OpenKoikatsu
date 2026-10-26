// Heroine song event state (OKStreetSocialSong.inl; Docs/HEROINE_SINGING_IMPLEMENTATION_PLAN.md). One member of the scene
// state, so the event's fields stay here instead of being spread over the scene.
#pragma once

#include "CoreMinimal.h"

// The game event, separate from the ok-song job: a ready song is not a song being sung.
enum class EOKSongPhase : uint8 { Idle, Preparing, Ready, Offered, Intro, Singing, Outro, Failed };

// A heroine's milestone songs (rules song_milestone): the occasions she has sung, lines of talk since her last song, and
// her stage when the song event last looked (SongTrackStages: becoming his lover is seen as it happens; one who was
// his lover already when songs first saw her has her confession behind her).
struct FOKSongProgress {
 TArray<FString> Done;int32 Lines=0;FString Stage;
};

// A song she sang, kept to hear again: the mix, her mouth envelope and the sheet (songs.json "memories").
struct FOKSongMemory {
 FString HeroineId,Occasion,Template,Lyrics,LyricsModel,SongPath,MouthPath;float Hop=.01f;int32 Day=0;
 // Each breath as ok-asr heard it (SongListen): the captions follow what she sang. Empty: not transcribed (yet).
 TArray<FString> Heard;
};

struct FOKSongState {
 EOKSongPhase Phase=EOKSongPhase::Idle;
 // ok-song service (OK_SONG_ENDPOINT, set by the launcher); empty: no songs in this run.
 FString Endpoint;
 // Songs are on unless the player turns them off (saved in songs.json); the control channel can still test.
 bool Enabled=true,Loaded=false,Override=false,RequestPending=false,PollPending=false,SayPending=false,SpeechStarted=false;
 // This event's milestone (song_milestone: confession, favor100, favor150, lines; empty for a test song), whether it
 // is due (a song made ahead waits until it is), and Replay: she sings one of her earlier songs again (no milestone).
 FString Occasion;bool Due=false,Replay=false;
 bool StartWhenSeated=false; // accepted while they were still sitting down together: sung once they sit (R3-220)
 // He asked for one of her songs again while she was still talking (his words, or the button mid-line): sung once she
 // is free, if that is within a minute (AskedAt: SceneTime()).
 int32 AskedMemory=INDEX_NONE;double AskedAt=0;
 // The song she has just sung (a memory index) and to whom: "hear it again" by the dialogue box while he is still with her.
 int32 JustSung=INDEX_NONE,JustSungGirl=INDEX_NONE;
 // This song's breaths as ok-asr heard them (SongListen; song_choreo "heard").
 TArray<FString> Heard;
 TMap<FString,FOKSongProgress> Progress;TArray<FOKSongMemory> Memories;
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
 // The caption while she sings: each line from when it starts (rules song_choreo lines), Line the one shown.
 TArray<double> LineAt;TArray<FString> LineText;int32 Line=-1;
};
