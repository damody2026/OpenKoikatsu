#include "KKSharedAnimation.h"
#include "KKImportedCharacter.h"
#include "KKImportedPoseComponent.h"
#include "KKRustExtract.h"
#include "KKExactSkinning.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "SkeletalRenderPublic.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSharedMouthTest,"OpenKoikatsu.Import.SharedOriginalAndMouth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSharedMouthTest::RunTest(const FString& Parameters){
 FKKScopedNativeSkinSource NativeSource;
 AddInfo(TEXT("Stage376OracleR9 native_skin=1 diagnostics=1"));
 const FString CharacterPath=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_SHARED_CHARACTER"));
 const FString AnimationPath=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_SHARED_ANIMATION"));
 const FString Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_SHARED_ROOT"));
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_SHARED_CARD"));
 TStrongObjectPtr<UKKImportedCharacter> Character(LoadObject<UKKImportedCharacter>(nullptr,*CharacterPath));
 TStrongObjectPtr<UKKSharedAnimation> Animation(LoadObject<UKKSharedAnimation>(nullptr,*AnimationPath));
 if(!TestNotNull(TEXT("Saved character fixture"),Character.Get())||!TestNotNull(TEXT("Saved shared curve fixture"),Animation.Get()))return false;
 if(!TestTrue(TEXT("Independent rig persisted"),!Character->Outfits.IsEmpty()&&!Character->Outfits[0].AnimationSnapshot.IsEmpty()))return false;
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* A=World->SpawnActor<AKKImportedCharacterActor>();auto* B=World->SpawnActor<AKKImportedCharacterActor>();
 A->Character=Character.Get();B->Character=Character.Get();A->Outfit=B->Outfit=Character->Outfits[0].Index;
 bool Good=TestTrue(TEXT("Actor A uses shared resource"),A->PlaySharedAnimation(Animation.Get()));
 Good=TestTrue(TEXT("Actor B uses same shared resource"),B->PlaySharedAnimation(Animation.Get()))&&Good;
 AddInfo(TEXT("Driver A: ")+A->AnimationError+TEXT("; Driver B: ")+B->AnimationError);
 const auto& O=Character->Outfits[0];FOkSession Native;
 FString NativeError;Good=TestTrue(TEXT("Native source opens"),Native.Open(Root,Card,O.Index,NativeError))&&Good;AddInfo(NativeError);
 Good=Native.SetAnimClip(Animation->Source+TEXT("|")+Animation->Clip)&&Good;
 Native.ApplyAnimFrame(0.f,0.f);Native.Skin();
 AddInfo(FString::Printf(TEXT("Oracle root=%s card=%s parts=%d"),*Root,*Card,Native.PartCount()));
 // Oracle generation can add unrelated hidden parts. Resolve by immutable
 // source identity, never assume the live source retains snapshot indices.
 // Runtime playback still validates its own saved indices against the snapshot.
 TMap<int32,int32> OracleParts;
 for(const auto& P:O.Parts){int32 Match=INDEX_NONE,Count=0;
  for(int32 I=0;I<Native.PartCount();++I){OkMeshView V{};if(!Native.PartAt(I,V)||!V.name||!V.role)continue;
   if(P.NativeRenderer==UTF8_TO_TCHAR(V.name)&&P.Role==UTF8_TO_TCHAR(V.role)&&P.SkinSourceHashV2==Native.SkinSourceHashV2(I)){Match=I;++Count;}
  }
  if(Count!=1){AddError(FString::Printf(TEXT("Oracle source identity missing or ambiguous renderer=%s candidates=%d"),*P.NativeRenderer,Count));Good=false;}
  else {OracleParts.Add(P.NativePartIndex,Match);if(Match!=P.NativePartIndex)AddInfo(FString::Printf(TEXT("Oracle source identity index shifted renderer=%s saved=%d live=%d"),*P.NativeRenderer,P.NativePartIndex,Match));}
 }
 int32 Aliases=0,MouthParts=0;int64 Vertices=0;double Maximum=0.;
 if(Good){Good=TestTrue(TEXT("Independent mouth available"),A->SetMouthControl(0,.37f))&&Good;
  Good=TestTrue(TEXT("Independent second mouth"),B->SetMouthControl(0,.83f))&&Good;
  Good=TestFalse(TEXT("Nonfinite mouth rejected"),A->SetMouthControl(0,std::numeric_limits<float>::quiet_NaN()))&&Good;
 }
 float OracleTime=-1.f;
 for(float Time:{0.f,.5f,1.f,.25f,Animation->Duration+.13f})if(Good){
  Good=TestTrue(TEXT("Seek while mouth active"),A->SetAnimationPosition(Time));if(!Good)break;
  if(OracleTime<0.f||Time<OracleTime){if(!Native.Open(Root,Card,O.Index,NativeError)||!Native.SetAnimClip(Animation->Source+TEXT("|")+Animation->Clip)){AddError(NativeError);Good=false;break;}Native.ApplyAnimFrame(0.f,0.f);OracleTime=0.f;}
  auto Sample=[&](float T){return Animation->Loop?FMath::Fmod(T,Animation->Duration):FMath::Min(T,Animation->Duration);};
  while(Time-OracleTime>1.f/60.f+1e-6f){Native.ApplyAnimFrame(Sample(OracleTime+1.f/60.f),1.f/60.f);OracleTime+=1.f/60.f;}if(Time>OracleTime){Native.ApplyAnimFrame(Sample(Time),Time-OracleTime);OracleTime=Time;}Native.Skin();
  TArray<UKKImportedPoseComponent*> Skins;A->GetComponents(Skins);Good=TestEqual(TEXT("All imported renderers animated"),Skins.Num(),O.Parts.Num())&&Good;
  for(const auto& P:O.Parts){auto** Found=Skins.FindByPredicate([&](const auto* C){return C->GetSkeletalMeshAsset()==P.SkinnedModel;});if(!Found){AddError(TEXT("Oracle target component missing: ")+P.NativeRenderer);Good=false;break;}
   auto* C=*Found;OkSkinPaletteView Raw{};TArray<float> Matrices;FMatrix Rigid=FMatrix::Identity;
   if(P.RigidFollow.IsEmpty()){if(Native.SkinPalette(OracleParts.FindChecked(P.NativePartIndex),Raw,Matrices)<=0){AddError(TEXT("Oracle palette missing: ")+P.NativeRenderer);Good=false;break;}}
   else {float M[16];if(!Native.BoneWorldMatrix(P.RigidFollow,M)){AddError(TEXT("Oracle rigid bone missing: ")+P.RigidFollow);Good=false;break;}Rigid=OKSkinPaletteToUnreal(P.RigidReference.GetData()).Inverse()*OKSkinPaletteToUnreal(M);}
   for(int32 Slot:P.PaletteSlots){FMatrix Actual=FMatrix::Identity;for(int32 R=0;R<4;++R)for(int32 Col=0;Col<4;++Col)Actual.M[R][Col]=0.;
    for(int32 Axis=0;Axis<3;++Axis){const int32 Index=C->GetBoneIndex(OKPaletteBoneName(P.NativePartIndex,Slot,Axis));if(Index<0){Good=false;break;}const FMatrix M=C->GetComponentSpaceTransforms()[Index].ToMatrixWithScale();for(int32 R=0;R<4;++R)for(int32 Col=0;Col<4;++Col)Actual.M[R][Col]+=M.M[R][Col]/3.;++Aliases;}
    FMatrix Expected=Rigid;if(P.RigidFollow.IsEmpty()&&!OKExactPaletteMatrix(Raw,Matrices,Slot,Expected)){AddError(TEXT("Oracle signed palette missing"));Good=false;break;}
    double Difference=0.;for(int32 R=0;R<4;++R)for(int32 Col=0;Col<4;++Col)Difference=FMath::Max(Difference,FMath::Abs(Actual.M[R][Col]-Expected.M[R][Col]));
    if(Difference>Maximum){Maximum=Difference;UE_LOG(LogTemp,Display,TEXT("Shared palette max time=%.6f renderer=%s slot=%d error=%.9f actual=%s expected=%s"),Time,*P.NativeRenderer,Slot,Maximum,*Actual.ToString(),*Expected.ToString());}
   }
   if(!P.MouthPatterns.IsEmpty()){++MouthParts;TMap<FName,float> Desired;
    for(const auto& Pair:P.MouthPatterns){if(!Pair.Close.IsNone())Desired.Add(Pair.Close,0.f);if(!Pair.Open.IsNone())Desired.Add(Pair.Open,0.f);}
    const auto& Pair=P.MouthPatterns[0];if(!Pair.Close.IsNone())Desired.FindOrAdd(Pair.Close)+=63.f;if(!Pair.Open.IsNone())Desired.FindOrAdd(Pair.Open)+=37.f;
    for(const auto& V:Desired)Good=TestTrue(TEXT("Native baseline-relative FBS survives seek"),FMath::IsNearlyEqual(C->GetMorphTarget(V.Key),(V.Value-P.MouthBaseline.FindRef(V.Key))/100.f,1e-6f))&&Good;
   }
   TArray<FFinalSkinVertex> Positions;C->GetCPUSkinnedVertices(Positions,0);Vertices+=Positions.Num();
   for(const auto& V:Positions)if(V.Position.ContainsNaN()){Good=false;break;}
  }
 }
 Good=TestTrue(TEXT("Original native affine palette matches"),Aliases>0&&Maximum<.01)&&Good;
 Good=TestTrue(TEXT("Mouth-controlled geometry present"),MouthParts>0&&Vertices>0)&&Good;
 Good=TestEqual(TEXT("Other actor clock unaffected"),B->SharedAnimationTime,0.f)&&Good;
 Good=TestEqual(TEXT("Other actor mouth unaffected"),B->MouthOpen,.83f)&&Good;
 double MouthDisplacement=0.;TArray<UKKImportedPoseComponent*> MouthSkins;A->GetComponents(MouthSkins);
 for(auto* C:MouthSkins){const auto* P=O.Parts.FindByPredicate([&](const auto& Part){return Part.SkinnedModel==C->GetSkeletalMeshAsset();});if(!P||P->MouthPatterns.IsEmpty())continue;
  if(P->MouthHideWhenClosed){A->SetMouthControl(0,0.f);Good=TestFalse(TEXT("Closed oral renderer hidden"),C->IsVisible())&&Good;
   A->SetMouthControl(0,1.f);Good=TestTrue(TEXT("Open oral renderer visible"),C->IsVisible())&&Good;continue;}

  const TArray<FTransform> Bones=C->GetComponentSpaceTransforms();TArray<FFinalSkinVertex> Closed,Opened;
  A->SetMouthControl(0,0.f);C->RefreshBoneTransforms();C->GetCPUSkinnedVertices(Closed,0);
  A->SetMouthControl(0,1.f);C->RefreshBoneTransforms();C->GetCPUSkinnedVertices(Opened,0);
  Good=TestEqual(TEXT("Morph keeps vertex count"),Opened.Num(),Closed.Num())&&Good;
  for(int32 I=0;I<FMath::Min(Closed.Num(),Opened.Num());++I)MouthDisplacement=FMath::Max(MouthDisplacement,double((Closed[I].Position-Opened[I].Position).Size()));
  const auto& After=C->GetComponentSpaceTransforms();for(int32 I=0;I<Bones.Num();++I)Good=TestTrue(TEXT("Mouth does not alter skeletal pose"),After.IsValidIndex(I)&&Bones[I].Equals(After[I],1e-9))&&Good;
 }
 Good=TestTrue(TEXT("Mouth really deforms rendered geometry"),MouthDisplacement>.001)&&Good;
 UE_LOG(LogTemp,Display,TEXT("Shared mouth geometry displacement_cm=%.9f skeleton_unchanged=%d"),MouthDisplacement,Good?1:0);
 A->SharedAnimationPlaying=false;const float Paused=A->SharedAnimationTime;A->Tick(.2f);Good=TestEqual(TEXT("Pause retains animation time"),A->SharedAnimationTime,Paused)&&Good;
 A->ResetMouthControl();Good=TestFalse(TEXT("Reset independent mouth"),A->MouthControlEnabled)&&Good;
 UE_LOG(LogTemp,Display,TEXT("Shared original mouth audit valid=%d actors=2 aliases=%d mouth_parts=%d vertices=%lld matrix_max=%.9f seek=1 pause=1 independent_clock=1"),Good?1:0,Aliases,MouthParts,Vertices,Maximum);
 A->Destroy();B->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return Good&&!HasAnyErrors();
}
#endif
