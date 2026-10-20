#include "../OKGalleryWidget.h"
#include "../OKShapeSlider.h"
#include "KKPreviewActor.h"
#include "ok_extract.h"
#include "Components/ComboBoxString.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "ShaderCompiler.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKClothingSpeedAudit {
struct FRun:TSharedFromThis<FRun>{
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;FString Card,Output,Clip;
 int32 Stage=0,Index=0;double Start=FPlatformTime::Seconds(),Loaded=0.,Ready=0.,MaxMs=0.;uint64 Worn=0,Half=0;uint32 WornMask=0,HalfMask=0,OffMask=0;
 TArray<TSharedPtr<FJsonValue>> Cases,Switches;TArray<float> Body,Face;TMap<FString,FQuat> Fingers;FVector Shoulder;
 TMap<FString,FQuat> Capture(AKKPreviewActor* A){TMap<FString,FQuat> R;for(int32 I=0;I<A->Session.SkelBoneCount();++I){OkBoneView V{};if(!A->Session.SkelBoneAt(I,V)||V.layer!=1||!V.name)continue;const FString N=UTF8_TO_TCHAR(V.name);for(const TCHAR* Stem:{TEXT("index"),TEXT("little"),TEXT("middle"),TEXT("ring"),TEXT("thumb")})for(int32 Joint=1;Joint<=3;++Joint)for(const TCHAR* Side:{TEXT("L"),TEXT("R")})if(N==FString::Printf(TEXT("cf_j_%s%02d_%s"),Stem,Joint,Side))R.Add(N,FQuat(V.pose_rot[0],V.pose_rot[1],V.pose_rot[2],V.pose_rot[3]));}return R;}
 FVector Scale(AKKPreviewActor* A){for(int32 I=0;I<A->Session.SkelBoneCount();++I){OkBoneView V{};if(A->Session.SkelBoneAt(I,V)&&V.layer==1&&V.name&&FString(UTF8_TO_TCHAR(V.name))==TEXT("cf_s_shoulder02_L"))return FVector(V.pose_scl[0],V.pose_scl[1],V.pose_scl[2]);}return FVector::ZeroVector;}
 uint64 Triangles(AKKPreviewActor* A){if(A->bClothingHidden)return 0;uint64 N=0;for(const auto& P:A->Parts)if(P.Role.StartsWith(TEXT("clothes"))){OkMeshView V{};if(A->Session.PartAt(P.SessionIndex,V)&&V.render_visible)N+=P.Triangles.Num()/3;}return N;}
 uint32 Mask(AKKPreviewActor* A){
  if(A->bClothingHidden){
   if(A->ClothingMaskMaterials.IsEmpty())return 0;
   UTexture* Value=nullptr;if(!A->ClothingMaskMaterials[0]->GetTextureParameterValue(FMaterialParameterInfo(TEXT("_AlphaMask")),Value))return 0;
   auto* Texture=Cast<UTexture2D>(Value);if(!Texture||Texture->GetSizeX()!=1||Texture->GetSizeY()!=1)return 0;
   auto& Data=Texture->GetPlatformData()->Mips[0].BulkData;const auto* Pixel=static_cast<const FColor*>(Data.LockReadOnly());
   const uint32 Result=Pixel&&*Pixel==FColor::White?FCrc::MemCrc32(Pixel,sizeof(FColor)):0;Data.Unlock();return Result;
  }
  for(const auto& P:A->Parts)if(P.Role==TEXT("body")){OkMeshView V{};if(A->Session.PartAt(P.SessionIndex,V)&&V.alpha_mask&&V.alpha_mask_len>0)return FCrc::MemCrc32(V.alpha_mask,V.alpha_mask_len);}return 0;
 }
 void Add(const TCHAR* Name){auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("case"),Name);R->SetBoolField(TEXT("valid"),true);Cases.Add(MakeShared<FJsonValueObject>(R));}
 bool Stop(const FString& Error){auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("valid"),Error.IsEmpty());R->SetStringField(TEXT("error"),Error);R->SetArrayField(TEXT("cases"),Cases);R->SetArrayField(TEXT("switches"),Switches);R->SetNumberField(TEXT("max_switch_ms"),MaxMs);R->SetNumberField(TEXT("worn_triangles"),double(Worn));R->SetNumberField(TEXT("half_triangles"),double(Half));R->SetNumberField(TEXT("worn_mask_crc"),WornMask);R->SetNumberField(TEXT("half_mask_crc"),HalfMask);R->SetNumberField(TEXT("off_mask_crc"),OffMask);FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*Output);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);return false;}
 bool Tick(float){auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Start>360)return Stop(TEXT("Clothing speed audit timeout"));auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return true;}
  if(Stage==0){G->bIndependentCards=true;G->Entries.Reset();FKKCardListEntry E;E.Path=Card;E.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(E);G->SelectIndex(0);G->OnLoad3D();Stage=1;return true;}
  if(G->bLoadInFlight||G->bAnimationPreparing||G->PendingAnimationKind>=0)return true;auto* A=G->PreviewActor.Get();if(!A)return Stop(TEXT("Preview missing"));
  if(Stage==1){if(Loaded==0)Loaded=FPlatformTime::Seconds();if(G->ClothingPreviews.Num()<2||G->bClothingWarmInFlight||G->ClothingWarmReady||!G->ClothingWarmQueue.IsEmpty())return true;for(const auto& Entry:G->ClothingPreviews)if(!Entry.Value->bPlaybackMaterialsReady)return true;Add(TEXT("geometric_states_prepared_full_off_uses_visibility"));Worn=Triangles(A);WornMask=Mask(A);
   if(G->Selected.Body.ShapeValueBody.IsEmpty())return Stop(TEXT("Body shape missing"));G->Selected.Body.ShapeValueBody[0]+=.02f;A->ApplyShapes(G->Selected.Face.ShapeValueFace,G->Selected.Body.ShapeValueBody);Body=G->Selected.Body.ShapeValueBody;Face=G->Selected.Face.ShapeValueFace;
   UOKShapeSlider* Bone=nullptr;for(UOKShapeSlider* S:G->BoneSliders)if(S->ModBone==TEXT("cf_s_shoulder02_L")&&S->ModAxis==-1){Bone=S;break;}if(!Bone)return Stop(TEXT("Bone control missing"));Bone->ModSide->SetSelectedIndex(1);G->OnBoneChanged(Bone,1.2f);G->ApplyPendingEditorMods();Shoulder=Scale(A);
   G->HandCombos[0]->SetSelectedIndex(2);G->HandCombos[1]->SetSelectedIndex(A->GetHandShapeCount()-1);G->OnLeftHandBlend(.3f);G->HandCombos[2]->SetSelectedIndex(A->GetHandShapeCount());G->OnRightHandBlend(.8f);
   G->GameClipCombo->SetSelectedOption(G->DemoAnimationOptions[1]);Stage=2;return true;
  }
  if(Stage==2){if(A->PoseMode!=2)return true;A->SetActorTickEnabled(false);A->AnimPhase=.4f;A->ApplyPoseFrame(0.f);Fingers=Capture(A);Clip=G->SelectedGameClip;Stage=3;return true;}
  if(Stage==3){if(Index>=32){Add(TEXT("thirty_two_common_state_switches_use_ready_displays"));Add(TEXT("body_and_bone_modifiers_preserved"));Add(TEXT("independent_hands_animation_clock_and_pause_preserved"));Add(TEXT("body_alpha_mask_changes_with_clothing"));Ready=FPlatformTime::Seconds();Stage=4;return true;}
   const int32 Action=Index%4==0?101:Index%4==1?102:Index%4==2?101:100;uint8 Expected[9];FString Error;if(!FOkSession::ClothingTransition(FString(),Card,G->OutfitIndex,G->ClothesStates,Action,Expected,Error))return Stop(Error);
   G->OnClothesState(Action);A=G->PreviewActor.Get();if(G->bLoadInFlight||!G->bLastClothingSwitchCached)return Stop(TEXT("Prepared state started a character reload"));
   if(A->IsHidden()||A->IsActorTickEnabled()||A->PoseMode!=2||G->SelectedGameClip!=Clip||!FMath::IsNearlyEqual(A->AnimPhase,.4f,.000001f))return Stop(TEXT("Switch changed visibility, animation clock or pause"));
   if(A->Session.ShapeBody()!=Body||A->Session.ShapeFace()!=Face||!Scale(A).Equals(Shoulder,.0001))return Stop(TEXT("Switch lost edited body or bone modifier"));auto Actual=Capture(A);if(Actual.Num()!=30||Fingers.Num()!=30)return Stop(TEXT("Finger coverage incomplete"));for(const auto& F:Fingers){const FQuat* Q=Actual.Find(F.Key);if(!Q||!Q->Equals(F.Value,.00002))return Stop(TEXT("Switch lost independent hand pose"));}
   for(int32 I=0;I<9;++I)if(G->LoadedClothesStates[I]!=Expected[I])return Stop(TEXT("Switch did not use native garment rules"));const uint64 Count=Triangles(A);const uint32 Alpha=Mask(A);
   if(Action==101){Half=Count;HalfMask=Alpha;if(Half==Worn||Half==0)return Stop(TEXT("Half clothing geometry did not change"));}
   if(Action==102){OffMask=Alpha;if(Count!=0)return Stop(TEXT("Full removal still draws clothing"));}
   if(Action==100&&Count!=Worn)return Stop(TEXT("Worn clothing did not restore"));
   if(G->ClothingPreviews.Num()>3)return Stop(TEXT("Inactive preview cache exceeded bound"));MaxMs=FMath::Max(MaxMs,G->LastClothingSwitchMs);auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("action"),Action);R->SetNumberField(TEXT("ms"),G->LastClothingSwitchMs);R->SetNumberField(TEXT("visible_clothing_triangles"),double(Count));Switches.Add(MakeShared<FJsonValueObject>(R));++Index;return true;
  }
  if(Stage==4){if(MaxMs>200)return Stop(TEXT("Common clothing switch exceeded 200ms"));if(WornMask==0||HalfMask==0||OffMask==0||WornMask==OffMask)return Stop(TEXT("Body alpha masks missing or unchanged"));if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()){Ready=FPlatformTime::Seconds();return true;}if(FPlatformTime::Seconds()-Ready<2)return true;FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output,TEXT("png")),true,false);Stage=5;return true;}
  if(Stage==5){G->OnClothesState(0);Stage=6;return true;}
  if(Stage==6){if(G->ClothingPreviews.Num()>3)return Stop(TEXT("Individual state exceeded cache bound"));Add(TEXT("individual_garment_fallback_keeps_bounded_cache"));G->ResetClothingPreviews();if(!G->ClothingPreviews.IsEmpty()||G->ClothingWarmReady||!G->ClothingWarmQueue.IsEmpty())return Stop(TEXT("Cache reset retained inactive displays"));Add(TEXT("resource_invalidation_releases_prepared_states"));return Stop(FString());}return true;
 }
};
}
static FAutoConsoleCommandWithWorld ClothingSpeedAudit(TEXT("ok.AuditEditorClothingSpeed"),TEXT("Verify prepared clothing display speed, masks and editor state"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));if(!W||Card.IsEmpty()||Output.IsEmpty())return;auto R=MakeShared<OKClothingSpeedAudit::FRun>();R->World=W;R->Card=Card;R->Output=Output;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R](float D){return R->Tick(D);}));}));
#endif
