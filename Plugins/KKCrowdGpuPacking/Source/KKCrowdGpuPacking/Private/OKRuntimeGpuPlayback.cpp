#include "OKRuntimeGpuPlayback.h"

double FOKRuntimeGpuPlayback::AdvanceTime(uint32 Clip,double Time,double Delta)const{
 const auto& C=Clips[Clip];const double T=Time+Delta*Rate;
 if(!C.Loop)return FMath::Clamp(T,0.,double(C.Duration));
 double Wrapped=FMath::Fmod(T,double(C.Duration));if(Wrapped<0)Wrapped+=C.Duration;return Wrapped;
}
bool FOKRuntimeGpuPlayback::Initialize(TConstArrayView<FOKRuntimeGpuPlaybackClip> Catalog,uint32 Clip,double Time,FString& Error){
 if(Catalog.IsEmpty()||Catalog.Num()>100||Clip>=uint32(Catalog.Num())||!FMath::IsFinite(Time)){Error=TEXT("Invalid private runtime playback catalog");return false;}
 TSet<FString> Ids;for(const auto& C:Catalog){if(C.Id.IsEmpty()||Ids.Contains(C.Id)||!FMath::IsFinite(C.Duration)||C.Duration<=0){Error=TEXT("Invalid or duplicate runtime playback clip");return false;}Ids.Add(C.Id);}
 Clips.Reset();Clips.Append(Catalog.GetData(),Catalog.Num());A=B=Clip;Rate=1;TimeA=TimeB=AdvanceTime(Clip,Time,0);BlendTime=BlendDuration=0;Queued=INDEX_NONE;Paused=false;Error.Reset();return true;
}
void FOKRuntimeGpuPlayback::Begin(uint32 Clip,double Duration,double Time){
 if(Duration==0){A=B=Clip;TimeA=TimeB=AdvanceTime(Clip,Time,0);BlendTime=BlendDuration=0;return;}
 B=Clip;TimeB=AdvanceTime(Clip,Time,0);BlendTime=0;BlendDuration=Duration;
}
bool FOKRuntimeGpuPlayback::RequestClip(uint32 Clip,double Duration,double Time,FString& Error){
 if(!IsReady()||Clip>=uint32(Clips.Num())||!FMath::IsFinite(Duration)||Duration<0||Duration>60||!FMath::IsFinite(Time)){Error=TEXT("Invalid private runtime clip transition");return false;}
 // A two-input GPU blend cannot represent an interrupted blend of three
 // poses. Queue the latest request until the current transition completes.
 if(IsTransitioning()){Queued=Clip;QueuedDuration=Duration;QueuedTime=Time;}
 else Begin(Clip,Duration,Time);
 Error.Reset();return true;
}
bool FOKRuntimeGpuPlayback::SetRate(float Value,FString& Error){
 if(!FMath::IsFinite(Value)||FMath::Abs(Value)>16){Error=TEXT("Invalid private runtime playback rate");return false;}Rate=Value;Error.Reset();return true;
}
bool FOKRuntimeGpuPlayback::ReplaceInactiveClip(uint32 Clip,const FOKRuntimeGpuPlaybackClip& Replacement,FString& Error){
 if(Clip>=uint32(Clips.Num())||UsesClip(Clip)||Replacement.Id.IsEmpty()||!FMath::IsFinite(Replacement.Duration)||Replacement.Duration<=0){Error=TEXT("Cannot replace an active/queued or invalid runtime clip slot");return false;}
 for(int32 I=0;I<Clips.Num();++I)if(uint32(I)!=Clip&&Clips[I].Id==Replacement.Id){Error=TEXT("Runtime replacement duplicates a clip ID");return false;}
 Clips[Clip]=Replacement;Error.Reset();return true;
}
bool FOKRuntimeGpuPlayback::Advance(double Delta,FString& Error){
 if(!IsReady()||!FMath::IsFinite(Delta)||Delta<0||Delta>86400){Error=TEXT("Invalid private runtime clock step");return false;}
 if(!Paused){while(Delta>0){const double Step=IsTransitioning()?FMath::Min(Delta,BlendDuration-BlendTime):Delta;
   TimeA=AdvanceTime(A,TimeA,Step);TimeB=AdvanceTime(B,TimeB,Step);Delta-=Step;
   if(IsTransitioning()){BlendTime+=Step;if(BlendTime>=BlendDuration){A=B;TimeA=TimeB;BlendTime=BlendDuration=0;if(Queued!=INDEX_NONE){const uint32 Next=Queued;Queued=INDEX_NONE;Begin(Next,QueuedDuration,QueuedTime);}}}
  }}Error.Reset();return true;
}
FOKGpuPosePlayback FOKRuntimeGpuPlayback::Parameters()const{
 FOKGpuPosePlayback P;P.ClipA=A;P.ClipB=B;P.Times=FVector4f(TimeA,TimeB,IsTransitioning()?BlendTime/BlendDuration:0.,0);return P;
}
