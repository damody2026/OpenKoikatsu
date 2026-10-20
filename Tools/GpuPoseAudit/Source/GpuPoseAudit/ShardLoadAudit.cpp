#include "OKGpuAnimationManifest.h"
#include "Misc/FileHelper.h"
#include "Json.h"

bool RunShardLoadAudit(const FString& Path,const FString& Output){
 TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Path))return false;
 FString Text;FFileHelper::BufferToString(Text,Bytes.GetData(),Bytes.Num());TSharedPtr<FJsonObject> Index;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Index)||!Index||!Index->TryGetArrayField(TEXT("characters"),Rows)||Rows->Num()<2)return false;
 TArray<FString> Selected;for(const auto& Row:*Rows)Selected.Insert(Row->AsObject()->GetStringField(TEXT("gpu_character")),0);
 auto Identity=FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num());auto Loaded=OKLoadSelectedGpuAnimationShardsAsync(Path,Identity,Selected).Consume();
 bool Good=Loaded.Bundle&&Loaded.WorkerThread&&Loaded.FromIndex&&Loaded.LoadedShards==uint32(Selected.Num())&&!Loaded.MissingSelectedActors;
 if(Good)for(int32 I=0;I<Selected.Num();++I)Good&=Loaded.Bundle->Manifest.Actors[I].CharacterId==Selected[I];
 TArray<FString> Subset{Selected[0],TEXT("/Game/GpuAudit/Missing.Character")};auto Partial=OKLoadSelectedGpuAnimationShardsAsync(Path,Identity,Subset).Consume();
 Good&=Partial.Bundle&&Partial.LoadedShards==1&&Partial.MissingSelectedActors==1&&Partial.Bundle->Manifest.Actors[0].CharacterId==Subset[0];
 uint32 Rejected=0;for(int32 Case=0;Case<8;++Case){FString Input=Path;auto Expected=Identity;auto Pick=Selected;uint64 Budget=2ull*1024*1024*1024-1;
  if(Case==0)Expected.Hash[0]^=1;
  else if(Case==1)Budget=1;
  else if(Case==2){const FString Duplicate=Pick[0];Pick.Add(Duplicate);}
  else if(Case==3)Pick={TEXT("/Game/GpuAudit/Missing.Character")};
  else{
   TSharedPtr<FJsonObject> Bad;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Bad);auto& Values=Bad->GetArrayField(TEXT("characters"));
   if(Case==4)Values[0]->AsObject()->SetStringField(TEXT("cache"),TEXT("missing-shard.gpuanim"));
   if(Case==5)Values[0]->AsObject()->SetStringField(TEXT("identity"),TEXT("invalid"));
   if(Case==6)Values[0]->AsObject()->SetStringField(TEXT("gpu_character"),Values[1]->AsObject()->GetStringField(TEXT("gpu_character")));
   if(Case==7){const FString Id0=Values[0]->AsObject()->GetStringField(TEXT("gpu_character"));Values[0]->AsObject()->SetStringField(TEXT("gpu_character"),Values[1]->AsObject()->GetStringField(TEXT("gpu_character")));Values[1]->AsObject()->SetStringField(TEXT("gpu_character"),Id0);}
   FString Changed;FJsonSerializer::Serialize(Bad,TJsonWriterFactory<>::Create(&Changed));Input=Output+FString::Printf(TEXT(".reject-%d.json"),Case);if(!FFileHelper::SaveStringToFile(Changed,*Input,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))return false;
   TArray<uint8> ChangedBytes;if(!FFileHelper::LoadFileToArray(ChangedBytes,*Input))return false;Expected=FSHA1::HashBuffer(ChangedBytes.GetData(),ChangedBytes.Num());
  }
  auto Bad=OKLoadSelectedGpuAnimationShardsAsync(Input,Expected,Pick,Budget).Consume();if(!Bad.Bundle&&Bad.WorkerThread&&Bad.FromIndex&&!Bad.Error.IsEmpty())++Rejected;
 }
 Good&=Rejected==8;auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),Good);Report->SetBoolField(TEXT("worker_thread"),Loaded.WorkerThread);Report->SetBoolField(TEXT("selected_order_preserved"),Good);Report->SetNumberField(TEXT("loaded_shards"),Loaded.LoadedShards);Report->SetNumberField(TEXT("load_ms"),Loaded.LoadMs);Report->SetNumberField(TEXT("invalid_inputs_rejected"),Rejected);Report->SetBoolField(TEXT("partial_selection_passed"),Partial.Bundle&&Partial.LoadedShards==1&&Partial.MissingSelectedActors==1);Report->SetBoolField(TEXT("fps_evidence"),false);
 FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));return FFileHelper::SaveStringToFile(Json,*Output)&&Good;
}
