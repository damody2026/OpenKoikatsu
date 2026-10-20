#include "KKRuntimeRenderMesh.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

namespace {
TSharedPtr<FKKRuntimeMouthStream,ESPMode::ThreadSafe> MakeFaceStream(FKKRuntimeMouthVertex Vertex){
 auto Stream=MakeShared<FKKRuntimeMouthStream,ESPMode::ThreadSafe>();
 if(Vertex.FaceCatalog)for(int32 Control=0;Control<3;++Control)Stream->PatternCount[Control]=Vertex.FaceCatalog->Control[Control].Close.Num();
 Stream->Vertices.Add(MoveTemp(Vertex));Stream->Ranges.Add({0,0,1});return Stream;
}
FKKRuntimeMouthVertex MouthVertex(){
 auto Catalog=MakeShared<FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>();auto& Mouth=Catalog->Control[0];
 Mouth.Close.Append({int16(0),int16(2)});Mouth.Open.Append({int16(1),int16(3)});Mouth.Baseline.Append({0.25f,0.f,0.5f,0.1f});
 FKKRuntimeMouthVertex Vertex;Vertex.Base=FVector3f(4.f,5.f,6.f);Vertex.FaceCatalog=Catalog;
 Vertex.Face.Add({0,0,10,FVector3f(1.f,0.f,0.f)});Vertex.Face.Add({0,1,11,FVector3f(0.f,1.f,0.f)});
 Vertex.Face.Add({0,2,12,FVector3f(0.f,0.f,1.f)});Vertex.Face.Add({0,3,13,FVector3f(1.f,1.f,0.f)});return Vertex;
}
FKKRuntimeMouthVertex TransitionVertex(){
 auto Catalog=MakeShared<FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>();
 auto& Mouth=Catalog->Control[0];Mouth.Close.Append({int16(0),int16(2)});Mouth.Open.Append({int16(1),int16(3)});Mouth.Baseline.SetNumZeroed(4);
 auto& Eye=Catalog->Control[1];Eye.Close.Add(0);Eye.Open.Add(1);Eye.Baseline.SetNumZeroed(2);
 auto& Brow=Catalog->Control[2];Brow.Close.Add(0);Brow.Open.Add(1);Brow.Baseline.SetNumZeroed(2);
 FKKRuntimeMouthVertex Vertex;Vertex.Base=FVector3f(1.f,1.f,1.f);Vertex.Closed=FVector3f::ZeroVector;Vertex.Open=FVector3f(8.f,0.f,0.f);Vertex.Blink=FVector3f(0.f,0.f,7.f);Vertex.FaceCatalog=Catalog;
 Vertex.Face.Add({0,0,10,FVector3f(2.f,0.f,0.f)});Vertex.Face.Add({0,1,11,FVector3f(0.f,2.f,0.f)});
 Vertex.Face.Add({0,2,12,FVector3f(0.f,0.f,2.f)});Vertex.Face.Add({0,3,13,FVector3f(4.f,0.f,0.f)});
 Vertex.Face.Add({1,0,20,FVector3f(0.f,3.f,0.f)});Vertex.Face.Add({1,1,21,FVector3f(0.f,0.f,5.f)});
 Vertex.Face.Add({2,0,30,FVector3f(6.f,0.f,0.f)});Vertex.Face.Add({2,1,31,FVector3f(0.f,6.f,0.f)});return Vertex;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKRuntimeFacePatternTest,"OpenKoikatsu.RuntimeMesh.FacePatterns",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKRuntimeFacePatternTest::RunTest(const FString&){
 auto Unchanged=[&](const TCHAR* Name,const TSharedPtr<FKKRuntimeMouthStream,ESPMode::ThreadSafe>& Stream,const FKKRuntimeFaceState& State,float Opening,float Gain,float Blink){
  TArray<FVector3f> Out;Out.Add(FVector3f(9.f,8.f,7.f));const FVector3f Sentinel=Out[0];FString Unused;
  const bool Valid=OKValidateRuntimeFaceState(Stream,State,Unused);const bool Evaluated=OKEvaluateRuntimeFacePositions(*Stream,Opening,Gain,Blink,&State,Out,Unused);
  return TestFalse(Name,Valid||Evaluated)&&TestTrue(Name,Out.Num()==1&&Out[0]==Sentinel);
 };
 FKKRuntimeMouthVertex Legacy;Legacy.Base=FVector3f(10.f,0.f,0.f);Legacy.Closed=FVector3f(1.f,0.f,0.f);Legacy.Open=FVector3f(0.f,1.f,0.f);Legacy.Blink=FVector3f(0.f,0.f,1.f);
 const FVector3f LegacyBase=Legacy.Base,LegacyOpen=Legacy.Open,LegacyBlink=Legacy.Blink;
 auto Extra=MakeShared<FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>();Extra->Control[0].Close.Add(0);Extra->Control[0].Open.Add(0);Extra->Control[0].Baseline.Add(0.f);
 Legacy.FaceCatalog=Extra;Legacy.Face.Add({0,0,1,FVector3f(5.f,0.f,0.f)});
 auto LegacyStream=MakeFaceStream(Legacy);FString Error;TArray<FVector3f> Absent,Historical;
 const FKKRuntimeFaceState Preserve;
 if(!TestTrue(TEXT("absent face validates"),OKValidateRuntimeFaceState(LegacyStream,Preserve,Error))||!TestTrue(TEXT("absent evaluation"),OKEvaluateRuntimeFacePositions(*LegacyStream,1.f,1.f,1.f,&Preserve,Absent,Error)))return false;
 if(!TestTrue(TEXT("absent default preserves card basis"),Absent.Num()==1&&Absent[0].Equals(LegacyBase,1e-6f)))return false;
 if(!TestTrue(TEXT("historical evaluation"),OKEvaluateRuntimeFacePositions(*LegacyStream,1.f,1.f,1.f,nullptr,Historical,Error)))return false;
 const FVector3f HistoricalWant=LegacyBase+LegacyOpen+LegacyBlink;
 if(!TestTrue(TEXT("nullptr keeps legacy mouth and blink"),Historical.Num()==1&&Historical[0].Equals(HistoricalWant,1e-6f)&&!Historical[0].Equals(Absent[0],1e-4f)))return false;

 auto MouthStream=MakeFaceStream(MouthVertex());FKKRuntimeFaceState ClosedMouth;ClosedMouth.MouthPattern=1;
 TArray<FVector3f> Closed,Open,Scaled,Silent;
 if(!TestTrue(TEXT("selected mouth"),OKEvaluateRuntimeFacePositions(*MouthStream,0.f,1.f,0.f,&ClosedMouth,Closed,Error)))return false;
 const FVector3f ClosedWant=MouthVertex().Base+FVector3f(1.f,0.f,0.f)*-.25f+FVector3f(0.f,0.f,1.f)*.5f+FVector3f(1.f,1.f,0.f)*-.1f;
 const FVector3f PairOnly=MouthVertex().Base+FVector3f(0.f,0.f,1.f)*.5f+FVector3f(1.f,1.f,0.f)*-.1f;
 if(!TestTrue(TEXT("mouth baseline includes unselected control morphs"),Closed.Num()==1&&Closed[0].Equals(ClosedWant,1e-5f)&&!Closed[0].Equals(PairOnly,1e-4f)))return false;
 if(!TestTrue(TEXT("open mouth"),OKEvaluateRuntimeFacePositions(*MouthStream,1.f,1.f,0.f,&ClosedMouth,Open,Error)))return false;
 const FVector3f OpenWant=MouthVertex().Base+FVector3f(1.f,0.f,0.f)*-.25f+FVector3f(0.f,0.f,1.f)*-.5f+FVector3f(1.f,1.f,0.f)*.9f;
 if(!TestTrue(TEXT("open mouth uses every baseline"),Open.Num()==1&&Open[0].Equals(OpenWant,1e-5f)))return false;
 FKKRuntimeFaceState Half=ClosedMouth;Half.MouthOpening=.5f;
 if(!TestTrue(TEXT("mouth open max scales audio"),OKEvaluateRuntimeFacePositions(*MouthStream,1.f,1.f,0.f,&Half,Scaled,Error)))return false;
 const FVector3f ScaledWant=MouthVertex().Base+FVector3f(1.f,0.f,0.f)*-.25f+FVector3f(1.f,1.f,0.f)*.4f;
 if(!TestTrue(TEXT("explicit mouth scale"),Scaled.Num()==1&&Scaled[0].Equals(ScaledWant,1e-5f)))return false;
 if(!TestTrue(TEXT("silent gain keeps card mouth"),OKEvaluateRuntimeFacePositions(*MouthStream,1.f,0.f,0.f,&ClosedMouth,Silent,Error))||!TestTrue(TEXT("gain zero"),Silent.Num()==1&&Silent[0].Equals(MouthVertex().Base,1e-6f)))return false;

 auto Eyes=MakeShared<FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>();auto& Eye=Eyes->Control[1];
 Eye.Close.Add(0);Eye.Open.Add(1);Eye.Baseline.Append({0.f,0.f});
 FKKRuntimeMouthVertex EyeVertex;EyeVertex.Base=FVector3f(1.f,1.f,1.f);EyeVertex.FaceCatalog=Eyes;
 EyeVertex.Face.Add({1,0,20,FVector3f(0.f,2.f,0.f)});EyeVertex.Face.Add({1,1,21,FVector3f(0.f,0.f,3.f)});
 auto EyeStream=MakeFaceStream(EyeVertex);FKKRuntimeFaceState EyeState;EyeState.EyePattern=0;EyeState.EyeOpening=1.f;
 TArray<FVector3f> EyeOpen,EyeClosed,EyeQuarter,EyeCard,EyeIgnored;
 if(!TestTrue(TEXT("eye open"),OKEvaluateRuntimeFacePositions(*EyeStream,0.f,0.f,0.f,&EyeState,EyeOpen,Error)))return false;
 if(!TestTrue(TEXT("blink zero is selected open"),EyeOpen.Num()==1&&EyeOpen[0].Equals(EyeVertex.Base+FVector3f(0.f,0.f,3.f),1e-5f)))return false;
 if(!TestTrue(TEXT("eye closed"),OKEvaluateRuntimeFacePositions(*EyeStream,0.f,0.f,1.f,&EyeState,EyeClosed,Error)))return false;
 if(!TestTrue(TEXT("blink one is selected closed"),EyeClosed.Num()==1&&EyeClosed[0].Equals(EyeVertex.Base+FVector3f(0.f,2.f,0.f),1e-5f)))return false;
 if(!TestTrue(TEXT("eye quarter"),OKEvaluateRuntimeFacePositions(*EyeStream,0.f,0.f,.25f,&EyeState,EyeQuarter,Error)))return false;
 if(!TestTrue(TEXT("blink closes the selected expression"),EyeQuarter.Num()==1&&EyeQuarter[0].Equals(EyeVertex.Base+FVector3f(0.f,.5f,2.25f),1e-5f)))return false;
 FKKRuntimeFaceState EyePreserve=EyeState;EyePreserve.EyeOpening=-1.f;
 if(!TestTrue(TEXT("card eye opening"),OKEvaluateRuntimeFacePositions(*EyeStream,0.f,0.f,1.f,&EyePreserve,EyeCard,Error)))return false;
 if(!TestTrue(TEXT("unspecified eye opening blinks from the card"),EyeCard.Num()==1&&EyeCard[0].Equals(EyeVertex.Base+FVector3f(0.f,2.f,0.f),1e-5f)))return false;
 FKKRuntimeFaceState NoEye;if(!TestTrue(TEXT("no eye pattern"),OKEvaluateRuntimeFacePositions(*EyeStream,0.f,0.f,1.f,&NoEye,EyeIgnored,Error)))return false;
 if(!TestTrue(TEXT("missing eye pattern ignores blink"),EyeIgnored.Num()==1&&EyeIgnored[0].Equals(EyeVertex.Base,1e-6f)))return false;

 auto Brows=MakeShared<FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>();auto& Brow=Brows->Control[2];
 Brow.Close.Add(0);Brow.Open.Add(1);Brow.Baseline.Append({0.f,0.f});
 FKKRuntimeMouthVertex BrowVertex;BrowVertex.Base=FVector3f(2.f,0.f,0.f);BrowVertex.FaceCatalog=Brows;
 BrowVertex.Face.Add({2,0,30,FVector3f(4.f,0.f,0.f)});BrowVertex.Face.Add({2,1,31,FVector3f(0.f,5.f,0.f)});
 auto BrowStream=MakeFaceStream(BrowVertex);FKKRuntimeFaceState BrowState;BrowState.BrowPattern=0;
 TArray<FVector3f> BrowDefault,BrowShut,BrowBlink;
 if(!TestTrue(TEXT("brow default"),OKEvaluateRuntimeFacePositions(*BrowStream,0.f,0.f,1.f,&BrowState,BrowDefault,Error)))return false;
 if(!TestTrue(TEXT("brow uses open rate 1"),BrowDefault.Num()==1&&BrowDefault[0].Equals(BrowVertex.Base+FVector3f(0.f,5.f,0.f),1e-5f)))return false;
 BrowState.BrowOpening=0.f;if(!TestTrue(TEXT("brow closed"),OKEvaluateRuntimeFacePositions(*BrowStream,0.f,0.f,0.f,&BrowState,BrowShut,Error)))return false;
 if(!TestTrue(TEXT("brow opening zero is closed"),BrowShut.Num()==1&&BrowShut[0].Equals(BrowVertex.Base+FVector3f(4.f,0.f,0.f),1e-5f)))return false;
 BrowState.BrowOpening=-1.f;if(!TestTrue(TEXT("brow blink"),OKEvaluateRuntimeFacePositions(*BrowStream,0.f,0.f,1.f,&BrowState,BrowBlink,Error)))return false;
 if(!TestTrue(TEXT("blink does not move brows"),BrowBlink.Num()==1&&BrowBlink[0].Equals(BrowDefault[0],1e-6f)))return false;

 auto Shared=MakeShared<FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>();
 Shared->Control[2].Close.Add(0);Shared->Control[2].Open.Add(-1);Shared->Control[2].Baseline.Add(0.f);
 Shared->Control[0].Close.Add(-1);Shared->Control[0].Open.Add(0);Shared->Control[0].Baseline.Add(0.f);
 FKKRuntimeMouthVertex SharedVertex;SharedVertex.FaceCatalog=Shared;SharedVertex.Base=FVector3f::ZeroVector;
 SharedVertex.Face.Add({2,0,42,FVector3f(3.f,0.f,0.f)});SharedVertex.Face.Add({0,0,42,FVector3f(3.f,0.f,0.f)});
 auto SharedStream=MakeFaceStream(SharedVertex);FKKRuntimeFaceState Overwrite;Overwrite.BrowPattern=0;Overwrite.BrowOpening=0.f;Overwrite.MouthPattern=0;
 TArray<FVector3f> Overwritten;
 if(!TestTrue(TEXT("shared channel"),OKEvaluateRuntimeFacePositions(*SharedStream,0.f,1.f,0.f,&Overwrite,Overwritten,Error)))return false;
 if(!TestTrue(TEXT("mouth overwrites brow on a shared morph"),Overwritten.Num()==1&&Overwritten[0].Equals(FVector3f::ZeroVector,1e-6f)))return false;

 FKKRuntimeRenderMeshData Prepared;FKKRuntimeMeshVertex First,Second,Idle;
 auto Wide=MakeShared<FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>();Wide->Control[0].Close.Append({int16(0),int16(1)});Wide->Control[0].Open.Append({int16(-1),int16(-1)});Wide->Control[0].Baseline.Append({0.f,0.f});
 auto Narrow=MakeShared<FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>();Narrow->Control[0].Close.Add(0);Narrow->Control[0].Open.Add(-1);Narrow->Control[0].Baseline.Add(0.f);
 First.FaceCatalog=Wide;First.Face.Add({0,0,1,FVector3f(1.f,0.f,0.f)});Second.FaceCatalog=Narrow;Second.Face.Add({0,0,2,FVector3f(0.f,1.f,0.f)});
 Prepared.Vertices.Add(First);Prepared.Vertices.Add(Idle);Prepared.Vertices.Add(Second);
 auto PreparedStream=OKPrepareRuntimeMouthStream(Prepared);
 if(!TestNotNull(TEXT("face vertices form a stream"),PreparedStream.Get())||!TestEqual(TEXT("minimum pattern count"),PreparedStream->PatternCount[0],1)||!TestEqual(TEXT("gap kept inside the face span"),PreparedStream->Vertices.Num(),3))return false;
 FKKRuntimeFaceState WidePattern;WidePattern.MouthPattern=1;FString Rejected;
 if(!TestFalse(TEXT("pattern unknown to another part"),OKValidateRuntimeFaceState(PreparedStream,WidePattern,Rejected))||!TestFalse(TEXT("rejection explains why"),Rejected.IsEmpty()))return false;

 const float Infinite=std::numeric_limits<float>::infinity();FKKRuntimeFaceState Bad=Preserve;
 Bad.MouthOpening=std::numeric_limits<float>::quiet_NaN();if(!Unchanged(TEXT("nonfinite mouth opening"),LegacyStream,Bad,0.f,0.f,0.f))return false;
 Bad=Preserve;Bad.EyeOpening=Infinite;if(!Unchanged(TEXT("infinite eye opening"),EyeStream,Bad,0.f,0.f,0.f))return false;
 Bad=Preserve;Bad.MouthOpening=1.5f;if(!Unchanged(TEXT("mouth opening above one"),MouthStream,Bad,0.f,0.f,0.f))return false;
 Bad=Preserve;Bad.MouthOpening=-.2f;if(!Unchanged(TEXT("mouth opening below preserved"),MouthStream,Bad,0.f,0.f,0.f))return false;
 Bad=Preserve;Bad.MouthPattern=-2;if(!Unchanged(TEXT("pattern below preserved"),MouthStream,Bad,0.f,0.f,0.f))return false;
 Bad=Preserve;Bad.MouthPattern=2;if(!Unchanged(TEXT("mouth pattern out of range"),MouthStream,Bad,0.f,0.f,0.f))return false;
 Bad=Preserve;Bad.EyePattern=0;if(!Unchanged(TEXT("eye pattern absent from stream"),MouthStream,Bad,0.f,0.f,0.f))return false;
 Bad=Preserve;Bad.EyePattern=10000;if(!Unchanged(TEXT("street unknown eye pattern"),EyeStream,Bad,0.f,0.f,0.f))return false;
 TArray<FVector3f> Seed;Seed.Add(FVector3f(3.f,3.f,3.f));const FVector3f SeedValue=Seed[0];
 if(!TestFalse(TEXT("nonfinite audio rejected"),OKEvaluateRuntimeFacePositions(*LegacyStream,std::numeric_limits<float>::quiet_NaN(),1.f,0.f,nullptr,Seed,Error))||!TestTrue(TEXT("audio rejection keeps output"),Seed.Num()==1&&Seed[0]==SeedValue))return false;
 Seed[0]=SeedValue;if(!TestFalse(TEXT("blink out of range"),OKEvaluateRuntimeFacePositions(*LegacyStream,0.f,0.f,1.1f,nullptr,Seed,Error))||!TestTrue(TEXT("blink rejection keeps output"),Seed[0]==SeedValue))return false;
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKRuntimeFaceTransitionTest,"OpenKoikatsu.RuntimeMesh.FaceTransitions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKRuntimeFaceTransitionTest::RunTest(const FString&){
 const FKKRuntimeMouthVertex Vertex=TransitionVertex();auto Stream=MakeFaceStream(Vertex);FString Error;
 if(!TestTrue(TEXT("transition duration is the source 0.15s"),OKRuntimeFaceTransitionSeconds==.15f))return false;
 const FKKRuntimeFaceState Defaults;if(!TestFalse(TEXT("default blend flags stay immediate"),Defaults.MouthBlend||Defaults.EyeBlend||Defaults.BrowBlend))return false;
 auto Face=[&](int32 Mouth,bool MouthEase,float MouthOpen,int32 Eye,bool EyeEase,float EyeOpen,int32 Brow,bool BrowEase,float BrowOpen){FKKRuntimeFaceState State;State.MouthPattern=Mouth;State.MouthBlend=MouthEase;State.MouthOpening=MouthOpen;State.EyePattern=Eye;State.EyeBlend=EyeEase;State.EyeOpening=EyeOpen;State.BrowPattern=Brow;State.BrowBlend=BrowEase;State.BrowOpening=BrowOpen;return State;};
 const FKKRuntimeFaceState Uniform=Face(1,true,-1.f,0,true,1.f,0,true,-1.f),Immediate=Face(1,false,-1.f,-1,false,-1.f,-1,false,-1.f),Mixed=Face(1,false,-1.f,0,true,1.f,-1,false,-1.f),CardEye=Face(-1,false,-1.f,0,true,-1.f,-1,false,-1.f),HalfMouth=Face(1,true,.5f,-1,false,-1.f,-1,false,-1.f);
 auto At=[&](const FKKRuntimeFaceTransition& Transition,float Audio,float Gain,float Blink,FVector3f& Out){TArray<FVector3f> Positions;if(!OKEvaluateRuntimeFaceTransition(*Stream,Audio,Gain,Blink,Transition,Positions,Error)||Positions.Num()!=1)return false;Out=Positions[0];return true;};
 auto Direct=[&](const FKKRuntimeFaceState& State,float Audio,float Gain,float Blink,FVector3f& Out){TArray<FVector3f> Positions;if(!OKEvaluateRuntimeFacePositions(*Stream,Audio,Gain,Blink,&State,Positions,Error)||Positions.Num()!=1)return false;Out=Positions[0];return true;};
 auto Legacy=[&](float Audio,float Gain,float Blink,FVector3f& Out){TArray<FVector3f> Positions;if(!OKEvaluateRuntimeFacePositions(*Stream,Audio,Gain,Blink,nullptr,Positions,Error)||Positions.Num()!=1)return false;Out=Positions[0];return true;};
 auto Expect=[&](const FKKRuntimeFaceState& State,float Audio,float Gain,float Blink,float U,FVector3f& Out){
  auto Delta=[&](int32 Which,FVector3f& Value){FKKRuntimeFaceState Only;if(Which==0){Only.MouthPattern=State.MouthPattern;Only.MouthOpening=State.MouthOpening;}else if(Which==1){Only.EyePattern=State.EyePattern;Only.EyeOpening=State.EyeOpening;}else{Only.BrowPattern=State.BrowPattern;Only.BrowOpening=State.BrowOpening;}TArray<FVector3f> Positions;if(!OKEvaluateRuntimeFacePositions(*Stream,Audio,Gain,Blink,&Only,Positions,Error)||Positions.Num()!=1)return false;Value=Positions[0]-Vertex.Base;return true;};
  FVector3f Mouth,Eyes,Brow;if(!Delta(0,Mouth)||!Delta(1,Eyes)||!Delta(2,Brow))return false;const FVector3f Piece[]={Mouth,Eyes,Brow};const FVector3f Historical[]={FMath::Lerp(Vertex.Closed,Vertex.Open,Audio)*Gain,Vertex.Blink*Blink,FVector3f::ZeroVector};const bool Ease[]={State.MouthBlend,State.EyeBlend,State.BrowBlend};FVector3f Sum=Vertex.Base;for(int32 I=0;I<3;++I)Sum+=Ease[I]?FMath::Lerp(Historical[I],Piece[I],U):Piece[I];Out=Sum;return true;};
 auto TimingU=[&](const FKKRuntimeFaceTransition& Transition){for(const auto& Channel:Transition.Channel)if(Channel.Timing)return FMath::Clamp(Channel.Elapsed/OKRuntimeFaceTransitionSeconds,0.f,1.f);return 1.f;};
 auto Match=[&](const TCHAR* Name,const FKKRuntimeFaceTransition& Transition,const FKKRuntimeFaceState& State,float Audio,float Gain,float Blink){FVector3f Got,Want;if(!At(Transition,Audio,Gain,Blink,Got)||!Expect(State,Audio,Gain,Blink,TimingU(Transition),Want))return false;return TestTrue(Name,Got.Equals(Want,1e-4f));};
 auto Ask=[&](FOKRuntimeFacePlayback& Playback,const FKKRuntimeFaceState& State){return OKRequestRuntimeFace(*Stream,Playback,State,true,Error);};
 FOKRuntimeFacePlayback Idle;FOKRuntimeFaceUploadPlan Plan;
 if(!TestTrue(TEXT("idle schedule"),OKPlanRuntimeFaceUpload(Idle,.016f,5,4,false,false,false,Plan,Error))||!TestFalse(TEXT("disabled bucket change is not a face upload"),Plan.Upload))return false;
 if(!TestTrue(TEXT("legacy mouth still schedules"),OKPlanRuntimeFaceUpload(Idle,.016f,6,5,true,false,false,Plan,Error))||!TestTrue(TEXT("bucketed legacy mouth uploads"),Plan.Upload))return false;
 if(!TestTrue(TEXT("legacy mouth same bucket"),OKPlanRuntimeFaceUpload(Idle,.016f,6,6,true,false,false,Plan,Error))||!TestFalse(TEXT("legacy mouth waits for the next 60Hz bucket"),Plan.Upload))return false;

 FOKRuntimeFacePlayback Held;if(!TestTrue(TEXT("immediate request"),Ask(Held,Immediate))||!TestEqual(TEXT("snap does not start"),Held.Started,0ull)||!TestFalse(TEXT("snap is not timing"),Held.Transition.Active())||!TestTrue(TEXT("immediate matches authored"),Match(TEXT("immediate endpoint"),Held.Transition,Immediate,1.f,1.f,0.f)))return false;
 const uint64 HeldRequests=Held.Requests;if(!TestTrue(TEXT("same immediate target"),Ask(Held,Immediate))||!TestEqual(TEXT("same target does not request again"),Held.Requests,HeldRequests)||!TestEqual(TEXT("same target does not start"),Held.Started,0ull))return false;

 FOKRuntimeFacePlayback Blend;if(!TestTrue(TEXT("blend request"),Ask(Blend,Uniform))||!TestEqual(TEXT("one start per easing channel"),Blend.Started,3ull)||!TestTrue(TEXT("blend clock running"),Blend.Transition.Active()))return false;
 FVector3f AtStart,Historical;if(!At(Blend.Transition,.25f,1.f,.5f,AtStart)||!Legacy(.25f,1.f,.5f,Historical)||!TestTrue(TEXT("blend starts on the nullptr face"),AtStart.Equals(Historical,1e-4f)))return false;
 const FVector3f HistoricalWant=Vertex.Base+Vertex.Open*.25f+Vertex.Blink*.5f;if(!TestTrue(TEXT("nullptr face is the legacy mouth and blink"),Historical.Equals(HistoricalWant,1e-5f)))return false;
 int32 Finished=0;if(!TestTrue(TEXT("half step"),OKAdvanceRuntimeFaceTransition(Blend.Transition,.075f,Finished,Error))||!TestEqual(TEXT("half step finishes nothing"),Finished,0)||!TestTrue(TEXT("half elapsed"),FMath::IsNearlyEqual(Blend.Transition.Channel[0].Elapsed,.075f,1e-6f)))return false;
 if(!Match(TEXT("mid audio low blink low"),Blend.Transition,Uniform,.2f,1.f,.3f)||!Match(TEXT("mid audio high"),Blend.Transition,Uniform,.8f,1.f,.3f)||!Match(TEXT("mid blink open"),Blend.Transition,Uniform,.2f,1.f,0.f)||!Match(TEXT("mid blink shut"),Blend.Transition,Uniform,.2f,1.f,1.f)||!Match(TEXT("mid quiet gain"),Blend.Transition,Uniform,.2f,0.f,.3f)||!Match(TEXT("mid partial gain"),Blend.Transition,Uniform,.2f,.4f,.3f))return false;
 FVector3f Low,High,OpenLid,ShutLid;if(!At(Blend.Transition,.2f,1.f,.3f,Low)||!At(Blend.Transition,.8f,1.f,.3f,High)||!At(Blend.Transition,.2f,1.f,0.f,OpenLid)||!At(Blend.Transition,.2f,1.f,1.f,ShutLid))return false;
 if(!TestFalse(TEXT("audio stays live during the blend"),Low.Equals(High,1e-3f))||!TestFalse(TEXT("blink stays live during the blend"),OpenLid.Equals(ShutLid,1e-3f)))return false;
 const float Kept=Blend.Transition.Channel[1].Elapsed;const uint64 Started=Blend.Started,Requests=Blend.Requests;
 if(!TestTrue(TEXT("same blend target"),Ask(Blend,Uniform))||!TestEqual(TEXT("idempotent request"),Blend.Requests,Requests)||!TestEqual(TEXT("idempotent start"),Blend.Started,Started)||!TestTrue(TEXT("idempotent clock"),FMath::IsNearlyEqual(Blend.Transition.Channel[0].Elapsed,Kept,1e-6f)&&FMath::IsNearlyEqual(Blend.Transition.Channel[2].Elapsed,Kept,1e-6f)))return false;
 bool Changed=true;int32 StartedChannels=7;FKKRuntimeFaceTransition Applied;if(!TestTrue(TEXT("direct same target"),OKApplyRuntimeFaceTransition(*Stream,Blend.Transition,Uniform,false,Applied,Changed,StartedChannels,Error))||!TestFalse(TEXT("unchanged apply"),Changed)||!TestEqual(TEXT("unchanged apply starts nothing"),StartedChannels,0)||!TestTrue(TEXT("apply keeps the clock"),FMath::IsNearlyEqual(Applied.Channel[0].Elapsed,Blend.Transition.Channel[0].Elapsed,1e-6f)))return false;
 if(!TestTrue(TEXT("finish"),OKAdvanceRuntimeFaceTransition(Blend.Transition,.2f,Finished,Error))||!TestEqual(TEXT("three channels complete"),Finished,3)||!TestFalse(TEXT("settled clock"),Blend.Transition.Active()))return false;
 FVector3f Settled,Authored;if(!At(Blend.Transition,.2f,.4f,.3f,Settled)||!Direct(Uniform,.2f,.4f,.3f,Authored)||!TestTrue(TEXT("settled endpoint matches authored"),Settled.Equals(Authored,1e-4f))||!Legacy(.2f,.4f,.3f,Historical)||!TestFalse(TEXT("authored endpoint is not the nullptr face"),Settled.Equals(Historical,1e-3f)))return false;
 if(!TestTrue(TEXT("reset after settle"),OKResetRuntimeFace(Blend,Error))||!TestFalse(TEXT("reset clears the face"),Blend.Enabled||Blend.Transition.Active())||!TestEqual(TEXT("settle is not a cancel"),Blend.Cancelled,0ull)||!TestEqual(TEXT("settle still restores"),Blend.Restores,1ull)||!TestEqual(TEXT("reset pattern"),Blend.Face.MouthPattern,-1))return false;

 FOKRuntimeFacePlayback Retarget;if(!TestTrue(TEXT("retarget source"),Ask(Retarget,Uniform))||!TestTrue(TEXT("retarget half"),OKAdvanceRuntimeFaceTransition(Retarget.Transition,.075f,Finished,Error)))return false;
 FVector3f Before,After,OldEnd,NewEnd;const FKKRuntimeFaceState NextMouth=Face(0,true,-1.f,0,true,1.f,0,true,-1.f);
 if(!At(Retarget.Transition,.4f,1.f,.25f,Before)||!TestTrue(TEXT("retarget request"),Ask(Retarget,NextMouth)))return false;
 if(!TestEqual(TEXT("retarget starts only the new channel"),Retarget.Started,4ull)||!TestTrue(TEXT("retarget restarts that channel"),Retarget.Transition.Channel[0].Elapsed==0.f&&Retarget.Transition.Channel[0].Timing)||!TestTrue(TEXT("other channels keep their clock"),FMath::IsNearlyEqual(Retarget.Transition.Channel[1].Elapsed,.075f,1e-6f)&&FMath::IsNearlyEqual(Retarget.Transition.Channel[2].Elapsed,.075f,1e-6f)))return false;
 if(!At(Retarget.Transition,.4f,1.f,.25f,After)||!Direct(Uniform,.4f,1.f,.25f,OldEnd)||!Direct(NextMouth,.4f,1.f,.25f,NewEnd))return false;
 FVector3f Restarted;if(!Legacy(.4f,1.f,.25f,Restarted))return false;
 if(!TestTrue(TEXT("retarget continues the current contribution"),Before.Equals(After,1e-4f))||!TestFalse(TEXT("retarget does not snap to the previous target"),After.Equals(OldEnd,1e-3f))||!TestFalse(TEXT("retarget does not snap to the new target"),After.Equals(NewEnd,1e-3f))||!TestFalse(TEXT("retarget does not restart from the nullptr face"),After.Equals(Restarted,1e-3f)))return false;
 if(!TestEqual(TEXT("from weights stay one catalog"),Retarget.Transition.Channel[0].From.Num(),1)||!TestEqual(TEXT("to weights stay one catalog"),Retarget.Transition.Channel[0].To.Num(),1))return false;
 const FKKRuntimeFaceState Back=Uniform;FVector3f Continued;if(!At(Retarget.Transition,.4f,1.f,.25f,Continued)||!TestTrue(TEXT("second retarget"),Ask(Retarget,Back))||!At(Retarget.Transition,.4f,1.f,.25f,After))return false;
 if(!TestTrue(TEXT("second retarget keeps the current contribution"),Continued.Equals(After,1e-4f))||!TestEqual(TEXT("history does not grow"),Retarget.Transition.Channel[0].From.Num(),1)||!TestEqual(TEXT("second retarget starts one channel"),Retarget.Started,5ull))return false;
 FVector3f Moved;if(!At(Retarget.Transition,.9f,1.f,.25f,Moved)||!TestFalse(TEXT("retargeted mouth still follows audio"),Moved.Equals(After,1e-3f)))return false;

 FOKRuntimeFacePlayback MixedPlay;if(!TestTrue(TEXT("mixed request"),Ask(MixedPlay,Mixed))||!TestEqual(TEXT("mixed starts the eased channel only"),MixedPlay.Started,1ull)||!TestFalse(TEXT("false channel is immediate"),MixedPlay.Transition.Channel[0].Timing)||!TestTrue(TEXT("true channel is timing"),MixedPlay.Transition.Channel[1].Timing)||!Match(TEXT("mixed start"),MixedPlay.Transition,Mixed,.25f,1.f,.5f))return false;
 if(!TestTrue(TEXT("mixed half"),OKAdvanceRuntimeFaceTransition(MixedPlay.Transition,.075f,Finished,Error))||!Match(TEXT("mixed midpoint"),MixedPlay.Transition,Mixed,.25f,1.f,.5f))return false;
 FVector3f MixedMid,FullLegacy,FullAuthored;if(!At(MixedPlay.Transition,.25f,1.f,.5f,MixedMid)||!Legacy(.25f,1.f,.5f,FullLegacy)||!Direct(Mixed,.25f,1.f,.5f,FullAuthored))return false;
 if(!TestFalse(TEXT("immediate channel is not included in the ease"),MixedMid.Equals(FMath::Lerp(FullLegacy,FullAuthored,.5f),1e-3f)))return false;
 if(!TestTrue(TEXT("blink flag change is not a new blend"),OKRequestRuntimeFace(*Stream,MixedPlay,Mixed,false,Error))||!TestEqual(TEXT("blink change counts a request"),MixedPlay.Requests,2ull)||!TestEqual(TEXT("blink change starts nothing"),MixedPlay.Started,1ull)||!TestTrue(TEXT("blink change keeps the eye clock"),FMath::IsNearlyEqual(MixedPlay.Transition.Channel[1].Elapsed,.075f,1e-6f)))return false;

 FOKRuntimeFacePlayback Card;if(!TestTrue(TEXT("card eye request"),Ask(Card,CardEye))||!TestEqual(TEXT("card eye starts"),Card.Started,1ull)||!TestTrue(TEXT("card eye half"),OKAdvanceRuntimeFaceTransition(Card.Transition,.075f,Finished,Error))||!Match(TEXT("card eye blink low"),Card.Transition,CardEye,0.f,0.f,.25f)||!Match(TEXT("card eye blink shut"),Card.Transition,CardEye,0.f,0.f,1.f))return false;
 FVector3f CardLow,CardShut;if(!At(Card.Transition,0.f,0.f,.25f,CardLow)||!At(Card.Transition,0.f,0.f,1.f,CardShut)||!TestFalse(TEXT("card eye opening still blinks"),CardLow.Equals(CardShut,1e-3f)))return false;
 FOKRuntimeFacePlayback Scaled;if(!TestTrue(TEXT("scaled mouth request"),Ask(Scaled,HalfMouth))||!Match(TEXT("scaled mouth start drops omitted blink"),Scaled.Transition,HalfMouth,.2f,1.f,.4f))return false;
 FVector3f ScaledStart,ScaledLegacy;if(!At(Scaled.Transition,.2f,1.f,.4f,ScaledStart)||!Legacy(.2f,1.f,.4f,ScaledLegacy)||!TestFalse(TEXT("omitted eye does not keep the historical blink"),ScaledStart.Equals(ScaledLegacy,1e-3f)))return false;
 if(!TestTrue(TEXT("scaled mouth half"),OKAdvanceRuntimeFaceTransition(Scaled.Transition,.075f,Finished,Error))||!Match(TEXT("scaled mouth quiet"),Scaled.Transition,HalfMouth,.2f,1.f,0.f)||!Match(TEXT("scaled mouth open"),Scaled.Transition,HalfMouth,1.f,1.f,0.f))return false;
 FVector3f ScaledQuiet,ScaledOpen;if(!At(Scaled.Transition,.2f,1.f,0.f,ScaledQuiet)||!At(Scaled.Transition,1.f,1.f,0.f,ScaledOpen)||!TestFalse(TEXT("explicit mouth opening still follows audio"),ScaledQuiet.Equals(ScaledOpen,1e-3f)))return false;

 FOKRuntimeFacePlayback Live;if(!TestTrue(TEXT("cancel source"),Ask(Live,Uniform))||!TestTrue(TEXT("cancel half"),OKAdvanceRuntimeFaceTransition(Live.Transition,.075f,Finished,Error))||!TestTrue(TEXT("cancel"),OKResetRuntimeFace(Live,Error)))return false;
 if(!TestFalse(TEXT("cancel disables"),Live.Enabled||Live.Transition.Active())||!TestEqual(TEXT("cancel counted"),Live.Cancelled,1ull)||!TestEqual(TEXT("cancel restores"),Live.Restores,1ull)||!TestTrue(TEXT("cancel dirties one legacy upload"),Live.Dirty)||!TestEqual(TEXT("cancel clears the stored pattern"),Live.Transition.Channel[0].Pattern,-1))return false;
 FVector3f Cleared;if(!At(Live.Transition,.3f,1.f,.4f,Cleared)||!Legacy(.3f,1.f,.4f,Historical)||!TestTrue(TEXT("cleared transition is the card basis"),Cleared.Equals(Vertex.Base,1e-5f))||!TestFalse(TEXT("legacy route remains the nullptr face"),Cleared.Equals(Historical,1e-3f)))return false;
 FOKRuntimeFaceUploadPlan Restore;if(!TestTrue(TEXT("restore plan"),OKPlanRuntimeFaceUpload(Live,0.f,1,0,false,false,false,Restore,Error))||!TestTrue(TEXT("restore uploads once"),Restore.Upload))return false;
 OKNoteRuntimeFaceUpload(Live,Restore);
 if(!TestEqual(TEXT("restore update"),Live.Updates,1ull)||!TestFalse(TEXT("restore consumed"),Live.Dirty))return false;
 if(!TestTrue(TEXT("second reset"),OKResetRuntimeFace(Live,Error))||!TestEqual(TEXT("disabled reset is idle"),Live.Restores,1ull)||!TestEqual(TEXT("disabled reset is not another cancel"),Live.Cancelled,1ull))return false;
 FOKRuntimeFacePlayback Snap;if(!TestTrue(TEXT("snap reset source"),Ask(Snap,Immediate))||!TestTrue(TEXT("snap reset"),OKResetRuntimeFace(Snap,Error))||!TestEqual(TEXT("immediate reset is not a cancel"),Snap.Cancelled,0ull)||!TestEqual(TEXT("immediate reset restores"),Snap.Restores,1ull))return false;

 FOKRuntimeFacePlayback Reject=Retarget;Reject.Started=9;Reject.Requests=4;Reject.Transition.Channel[0].Elapsed=.04f;const FOKRuntimeFacePlayback Rejected=Reject;
 FKKRuntimeFaceState BadEye=Uniform;BadEye.EyePattern=10000;FKKRuntimeFaceState BadOpen=Uniform;BadOpen.MouthOpening=std::numeric_limits<float>::quiet_NaN();
 if(!TestFalse(TEXT("unknown pattern rejected"),Ask(Reject,BadEye))||!TestFalse(TEXT("nonfinite opening rejected"),Ask(Reject,BadOpen)))return false;
 if(!TestEqual(TEXT("rejection keeps requests"),Reject.Requests,Rejected.Requests)||!TestEqual(TEXT("rejection keeps starts"),Reject.Started,Rejected.Started)||!TestTrue(TEXT("rejection keeps the clock"),Reject.Transition.Channel[0].Elapsed==Rejected.Transition.Channel[0].Elapsed&&Reject.Enabled==Rejected.Enabled&&Reject.Transition.Channel[0].Pattern==Rejected.Transition.Channel[0].Pattern))return false;
 FKKRuntimeFaceTransition Sentinel;Sentinel.Channel[0].Pattern=4;Sentinel.Channel[0].Elapsed=.4f;Sentinel.EndpointPending=true;Changed=true;StartedChannels=7;const FKKRuntimeFaceTransition SentinelCopy=Sentinel;
 if(!TestFalse(TEXT("apply rejection"),OKApplyRuntimeFaceTransition(*Stream,Sentinel,BadEye,false,Sentinel,Changed,StartedChannels,Error))||!TestTrue(TEXT("apply rejection keeps outputs"),Sentinel.Channel[0].Pattern==SentinelCopy.Channel[0].Pattern&&Sentinel.Channel[0].Elapsed==SentinelCopy.Channel[0].Elapsed&&Sentinel.EndpointPending&&Changed&&StartedChannels==7))return false;
 auto MorphStream=[&](int32 Baseline,int16 Close){auto Catalog=MakeShared<FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>();Catalog->Control[0].Close.Add(Close);Catalog->Control[0].Open.Add(-1);Catalog->Control[0].Baseline.SetNumZeroed(Baseline);FKKRuntimeMouthVertex Morph;Morph.Base=FVector3f(1.f,0.f,0.f);Morph.FaceCatalog=Catalog;return MakeFaceStream(Morph);};
 auto Bounded=MorphStream(OKRuntimeFaceMorphLimit,0);FOKRuntimeFacePlayback Bound;FKKRuntimeFaceState One;One.MouthPattern=0;One.MouthBlend=false;
 if(!TestTrue(TEXT("morph limit accepted"),Bounded.IsValid()&&OKRequestRuntimeFace(*Bounded,Bound,One,true,Error))||!TestEqual(TEXT("bounded snap requests"),Bound.Requests,1ull))return false;
 auto Overflow=MorphStream(OKRuntimeFaceMorphLimit+1,0);FOKRuntimeFacePlayback Over;if(!TestFalse(TEXT("morph above 512 rejected"),Overflow.IsValid()&&OKRequestRuntimeFace(*Overflow,Over,One,true,Error))||!TestEqual(TEXT("overflow writes nothing"),Over.Requests,0ull)||!TestFalse(TEXT("overflow stays disabled"),Over.Enabled)||!TestFalse(TEXT("overflow explains why"),Error.IsEmpty()))return false;
 auto Broken=MorphStream(1,5);FOKRuntimeFacePlayback BrokenPlay;if(!TestFalse(TEXT("morph index rejected"),Broken.IsValid()&&OKRequestRuntimeFace(*Broken,BrokenPlay,One,true,Error))||!TestEqual(TEXT("bad morph writes nothing"),BrokenPlay.Requests,0ull)||!TestFalse(TEXT("bad morph stays disabled"),BrokenPlay.Enabled))return false;
 int32 FinishedSentinel=9;FKKRuntimeFaceTransition Clock=Retarget.Transition;if(!TestFalse(TEXT("negative dt rejected"),OKAdvanceRuntimeFaceTransition(Clock,-.1f,FinishedSentinel,Error))||!TestEqual(TEXT("negative dt keeps the counter"),FinishedSentinel,9)||!TestTrue(TEXT("negative dt keeps elapsed"),Clock.Channel[1].Elapsed==Retarget.Transition.Channel[1].Elapsed))return false;
 TArray<FVector3f> Seed;Seed.Add(FVector3f(3.f,3.f,3.f));const FVector3f SeedValue=Seed[0];
 if(!TestFalse(TEXT("transition audio rejected"),OKEvaluateRuntimeFaceTransition(*Stream,std::numeric_limits<float>::quiet_NaN(),1.f,0.f,Retarget.Transition,Seed,Error))||!TestTrue(TEXT("transition rejection keeps output"),Seed.Num()==1&&Seed[0]==SeedValue))return false;

 FOKRuntimeFacePlayback Scheduled;if(!TestTrue(TEXT("schedule request"),Ask(Scheduled,HalfMouth)))return false;
 FOKRuntimeFaceUploadPlan Step;if(!TestTrue(TEXT("dirty start"),OKPlanRuntimeFaceUpload(Scheduled,0.f,0,0,false,false,false,Step,Error))||!TestTrue(TEXT("dirty start uploads"),Step.Upload)||!TestFalse(TEXT("zero elapsed is not intermediate"),Step.Intermediate)||!TestEqual(TEXT("start completes nothing"),Step.Completed,0))return false;
 OKNoteRuntimeFaceUpload(Scheduled,Step);if(!TestEqual(TEXT("start counts an update"),Scheduled.Updates,1ull)||!TestEqual(TEXT("start is not intermediate"),Scheduled.Intermediate,0ull))return false;
 if(!TestTrue(TEXT("same bucket advances quietly"),OKPlanRuntimeFaceUpload(Scheduled,.05f,0,0,false,false,false,Step,Error))||!TestFalse(TEXT("same bucket does not upload"),Step.Upload)||!TestTrue(TEXT("quiet step still moves the clock"),FMath::IsNearlyEqual(Scheduled.Transition.Channel[0].Elapsed,.05f,1e-6f)))return false;
 if(!TestTrue(TEXT("next bucket"),OKPlanRuntimeFaceUpload(Scheduled,0.f,1,0,false,false,false,Step,Error))||!TestTrue(TEXT("60Hz uploads without mouth motion"),Step.Upload&&Step.Intermediate)||!TestEqual(TEXT("60Hz completes nothing yet"),Scheduled.Completed,0ull))return false;
 OKNoteRuntimeFaceUpload(Scheduled,Step);if(!TestEqual(TEXT("intermediate counted on upload"),Scheduled.Intermediate,1ull)||!TestEqual(TEXT("intermediate is not another update"),Scheduled.Updates,1ull))return false;
 if(!TestTrue(TEXT("gain forces the same bucket"),OKPlanRuntimeFaceUpload(Scheduled,0.f,1,1,false,false,true,Step,Error))||!TestTrue(TEXT("gain upload"),Step.Upload&&Step.Intermediate))return false;
 OKNoteRuntimeFaceUpload(Scheduled,Step);if(!TestEqual(TEXT("second intermediate"),Scheduled.Intermediate,2ull))return false;
 if(!TestTrue(TEXT("endpoint inside the bucket"),OKPlanRuntimeFaceUpload(Scheduled,.2f,1,1,false,false,false,Step,Error))||!TestTrue(TEXT("endpoint uploads"),Step.Upload)||!TestFalse(TEXT("endpoint is not intermediate"),Step.Intermediate)||!TestEqual(TEXT("endpoint completes the channel"),Step.Completed,1)||!TestEqual(TEXT("completed counted once"),Scheduled.Completed,1ull)||!TestFalse(TEXT("endpoint settled"),Scheduled.Transition.Active())||!TestTrue(TEXT("endpoint pending"),Scheduled.Transition.EndpointPending))return false;
 if(!TestTrue(TEXT("endpoint held until noted"),OKPlanRuntimeFaceUpload(Scheduled,.05f,2,1,false,false,false,Step,Error))||!TestTrue(TEXT("held endpoint still wants one upload"),Step.Upload)||!TestEqual(TEXT("held endpoint does not complete again"),Scheduled.Completed,1ull)||!TestEqual(TEXT("held plan adds no channels"),Step.Completed,0))return false;
 OKNoteRuntimeFaceUpload(Scheduled,Step);if(!TestFalse(TEXT("note consumes the endpoint"),Scheduled.Transition.EndpointPending)||!TestEqual(TEXT("endpoint note is not intermediate"),Scheduled.Intermediate,2ull))return false;
 if(!TestTrue(TEXT("settled bucket"),OKPlanRuntimeFaceUpload(Scheduled,.05f,4,3,false,false,false,Step,Error))||!TestFalse(TEXT("settled bucket change does not upload"),Step.Upload))return false;
 if(!TestTrue(TEXT("settled speech"),OKPlanRuntimeFaceUpload(Scheduled,.05f,5,4,true,false,false,Step,Error))||!TestTrue(TEXT("settled speech uploads on the bucket"),Step.Upload)||!TestFalse(TEXT("settled speech is not a transition"),Step.Intermediate))return false;
 OKNoteRuntimeFaceUpload(Scheduled,Step);if(!TestEqual(TEXT("speech upload is not another expression update"),Scheduled.Updates,1ull))return false;
 if(!TestTrue(TEXT("intra-bucket speech"),OKPlanRuntimeFaceUpload(Scheduled,.05f,5,5,true,false,false,Step,Error))||!TestFalse(TEXT("settled speech stays on the 60Hz bucket"),Step.Upload))return false;
 const float Frozen=Scheduled.Transition.Channel[0].Elapsed;const uint64 FrozenCompleted=Scheduled.Completed;FOKRuntimeFaceUploadPlan Untouched;Untouched.Upload=true;Untouched.Intermediate=true;Untouched.Completed=4;
 if(!TestFalse(TEXT("bad dt rejected"),OKPlanRuntimeFaceUpload(Scheduled,std::numeric_limits<float>::quiet_NaN(),6,5,true,true,true,Untouched,Error))||!TestTrue(TEXT("bad dt keeps the plan"),Untouched.Upload&&Untouched.Intermediate&&Untouched.Completed==4)||!TestTrue(TEXT("bad dt keeps playback"),Scheduled.Transition.Channel[0].Elapsed==Frozen&&Scheduled.Completed==FrozenCompleted))return false;
 FOKRuntimeFacePlayback Hitch;if(!TestTrue(TEXT("hitch request"),Ask(Hitch,Uniform))||!TestTrue(TEXT("hitch plan"),OKPlanRuntimeFaceUpload(Hitch,2.f,1,0,false,false,false,Step,Error))||!TestEqual(TEXT("hitch completes every easing channel"),Hitch.Completed,3ull)||!TestFalse(TEXT("hitch settles"),Hitch.Transition.Active())||!TestTrue(TEXT("hitch snaps to the endpoint"),FMath::IsNearlyEqual(Hitch.Transition.Channel[0].Elapsed,OKRuntimeFaceTransitionSeconds,1e-6f)))return false;
 return true;
}
#endif
