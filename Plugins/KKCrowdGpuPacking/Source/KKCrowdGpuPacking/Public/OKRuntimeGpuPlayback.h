#pragma once
#include "OKResidentGpuPose.h"

struct FOKRuntimeGpuPlaybackClip {FString Id;float Duration=0;bool Loop=true;};
// One private actor's logical playback. No UObject, CPU pose evaluation, or
// sequence asset. Indices refer only to that actor's admitted GPU clip bank.
class KKCROWDGPUPACKING_API FOKRuntimeGpuPlayback {
 TArray<FOKRuntimeGpuPlaybackClip> Clips;
 uint32 A=0,B=0;double TimeA=0,TimeB=0,BlendTime=0,BlendDuration=0;
 float Rate=1;bool Paused=false;
 int32 Queued=INDEX_NONE;double QueuedDuration=0,QueuedTime=0;
 void Begin(uint32 Clip,double Duration,double Time);
 double AdvanceTime(uint32 Clip,double Time,double Delta)const;
public:
 // Invalid operations preserve the prior state.
 bool Initialize(TConstArrayView<FOKRuntimeGpuPlaybackClip> Catalog,uint32 Clip,double Time,FString& Error);
 bool RequestClip(uint32 Clip,double BlendSeconds,double StartTime,FString& Error);
 // Replace only an inactive, unqueued slot. Preserve both active clocks.
 bool ReplaceInactiveClip(uint32 Clip,const FOKRuntimeGpuPlaybackClip& Replacement,FString& Error);
 bool UsesClip(uint32 Clip)const{return A==Clip||B==Clip||Queued==int32(Clip);}
 bool SetRate(float Value,FString& Error);
 void SetPaused(bool Value){Paused=Value;}
 bool Advance(double DeltaSeconds,FString& Error);
 FOKGpuPosePlayback Parameters()const;
 bool IsTransitioning()const{return BlendDuration>0;}
 bool IsReady()const{return !Clips.IsEmpty();}
};
