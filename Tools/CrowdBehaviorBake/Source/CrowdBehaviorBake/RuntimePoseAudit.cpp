#include "KKRuntimePoseBank.h"
#include "KKRustExtract.h"
#include "KKExactSkinning.h"
#include "KKImportedCharacter.h"
#include "KKSharedAnimation.h"
#include "Animation/Skeleton.h"
#include "Misc/FileHelper.h"
#include "Json.h"
#include "Serialization/BufferArchive.h"

bool AuditRuntimePoseOnGpu(const FKKRuntimePoseRequest&,const FKKRuntimePoseResult&,double&,FString&);

// Editor-host fixture adapter only. The production sampling function receives
// plain copied metadata and does not load any of these UObjects.
bool RunRuntimePoseAudit(const FString& Input,const FString& Output){
 FString Text,Error;TSharedPtr<FJsonObject> Args;if(!FFileHelper::LoadFileToString(Text,*Input)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Args)||!Args)return false;
 auto* Character=LoadObject<UKKImportedCharacter>(nullptr,*Args->GetStringField(TEXT("character")));
 auto* Animation=LoadObject<UKKSharedAnimation>(nullptr,*Args->GetStringField(TEXT("curves")));
 if(!Character||Character->Outfits.IsEmpty()||!Character->Outfits[0].Skeleton||!Animation)return false;
 FKKRuntimePoseRequest R;const auto& Outfit=Character->Outfits[0];
 const FString Apng=Args->GetStringField(TEXT("apng"));TArray<uint8> Card;
 if(!FFileHelper::LoadFileToArray(Card,*Apng))return false;R.CharacterIdentity=FSHA1::HashBuffer(Card.GetData(),Card.Num()).ToString();Card.Reset();
 R.ClipIdentity=Animation->Source+TEXT("/")+Animation->Clip;R.Duration=Animation->Duration;R.AuthoredCurves=Animation->AuthoredCurves;
 const double OpenStart=FPlatformTime::Seconds();
 {FKKScopedNativeSkinSource NativeSource;FOkSession Session;if(!Session.Open(FString(),Apng,0,Error)||!Session.IsPortable()||!Session.ExportAnimationSnapshot(R.Snapshot)){UE_LOG(LogTemp,Error,TEXT("Runtime APNG audit: %s"),*Error);return false;}}
 const double OpenMs=(FPlatformTime::Seconds()-OpenStart)*1000;
 const auto& Ref=Outfit.Skeleton->GetReferenceSkeleton();for(int32 B=0;B<Ref.GetNum();++B)R.Bones.Add({Ref.GetBoneName(B),Ref.GetParentIndex(B),Ref.GetRefBonePose()[B]});
 TSet<int32> Seen;for(const auto& Part:Outfit.Parts)if(Part.NativePartIndex>=0&&!Part.PaletteSlots.IsEmpty()&&!Seen.Contains(Part.NativePartIndex)){
  Seen.Add(Part.NativePartIndex);FKKRuntimePalettePart P;P.NativePart=Part.NativePartIndex;P.Slots=Part.PaletteSlots;P.RigidFollow=Part.RigidFollow;
  if(!P.RigidFollow.IsEmpty()){if(Part.RigidReference.Num()!=16)return false;P.RigidBindWorld=OKSkinPaletteToUnreal(Part.RigidReference.GetData());}R.Parts.Add(MoveTemp(P));
 }
 FBufferArchive Recipe;uint32 Magic=0x52504b4b;int32 Count=R.Bones.Num();Recipe<<Magic<<Count;
 for(auto B:R.Bones){FString Name=B.Name.ToString();Recipe<<Name<<B.Parent<<B.Reference;}
 Count=R.Parts.Num();Recipe<<Count;for(auto P:R.Parts)Recipe<<P.NativePart<<P.Slots<<P.RigidFollow<<P.RigidBindWorld;
 Recipe<<R.AuthoredCurves<<R.Duration<<R.ClipIdentity;if(!FFileHelper::SaveArrayToFile(Recipe,*(Output+TEXT(".recipe"))))return false;
 auto Tiny=R;Tiny.MaxKeyBytes=1;auto Rejected=OKPrepareRuntimePoseBankAsync(MoveTemp(Tiny)).Consume();
 auto Result=OKPrepareRuntimePoseBankAsync(R).Consume();
 auto Repeat=OKPrepareRuntimePoseBankAsync(R).Consume();
 const bool Deterministic=Result.Passed()&&Repeat.Passed()&&Result.Keys==Repeat.Keys&&Result.Tracks==Repeat.Tracks;
 bool Finite=true;for(const auto& K:Result.Keys)for(int32 C=0;C<4;++C)Finite&=FMath::IsFinite(K[C]);
 bool RequestedGpu=false;Args->TryGetBoolField(TEXT("gpu"),RequestedGpu);double GpuError=0;const bool GpuPassed=!RequestedGpu||AuditRuntimePoseOnGpu(R,Result,GpuError,Error);
 const bool Passed=GpuPassed&&Result.Passed()&&Result.WorkerThread&&!Rejected.Passed()&&Deterministic&&Finite;
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),Passed);Report->SetBoolField(TEXT("runtime_worker"),Result.WorkerThread);Report->SetBoolField(TEXT("deterministic"),Deterministic);Report->SetBoolField(TEXT("finite"),Finite);Report->SetBoolField(TEXT("preallocation_budget_rejected"),!Rejected.Passed());
 Report->SetStringField(TEXT("error"),Result.Error);Report->SetNumberField(TEXT("apng_open_snapshot_ms"),OpenMs);Report->SetNumberField(TEXT("pose_prepare_ms"),Result.PrepareMs);Report->SetNumberField(TEXT("repeat_prepare_ms"),Repeat.PrepareMs);Report->SetNumberField(TEXT("key_bytes"),uint64(Result.Keys.Num())*16);Report->SetNumberField(TEXT("tracks"),Result.Tracks.Num());Report->SetNumberField(TEXT("constant_tracks"),Result.ConstantTracks);Report->SetNumberField(TEXT("samples"),Result.Samples);
 Report->SetBoolField(TEXT("external_kks_root_used"),false);Report->SetBoolField(TEXT("saved_sequence_created"),false);Report->SetBoolField(TEXT("metadata_fixture_used"),true);Report->SetBoolField(TEXT("gpu_readback_verified"),RequestedGpu&&GpuPassed);Report->SetNumberField(TEXT("gpu_max_matrix_error"),GpuError);Report->SetStringField(TEXT("gpu_error"),Error);
 FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));return FFileHelper::SaveStringToFile(Text,*Output)&&Passed;
}
