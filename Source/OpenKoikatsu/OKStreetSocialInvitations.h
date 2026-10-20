#pragma once
#include "OKStreetSocialRules.h"

struct FOKStreetInvitation {
 int32 Personality=INDEX_NONE,Stage=INDEX_NONE,Command=INDEX_NONE;
 float Min=0,Max=0;
 FString PlayerZh;
 TArray<int32> Accept,Refuse;
};
// Street date is the reset boundary; KKS resets talkCommand at NPC population init. Whether an invitation may be asked
// and what it costs are rules (talk.rs invite_check).
// Only shared personality/stage data. APNG identity and private skeleton are never baked here.
inline bool OKStreetLoadInvitations(const FString& Path,FOKStreetCatalogue& Catalogue,FOKStreetCatalogue& Other,TArray<FOKStreetInvitation>& Out,FString& Error){
 FString Body;if(!FFileHelper::LoadFileToString(Body,*Path)){Error=TEXT("Invitation catalogue missing");return false;}
 auto Parsed=OKStreetParseCatalogue(Body,Path);TSharedPtr<FJsonObject> Root;
 if(!Parsed.Error.IsEmpty()||!Parsed.Warnings.IsEmpty()||Parsed.Nodes.IsEmpty()||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),Root)){Error=TEXT("Invitation catalogue invalid: ")+Parsed.Error+FString::Join(Parsed.Warnings,TEXT(";"));return false;}
 const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(!Root->TryGetArrayField(TEXT("invitations"),Rows)){Error=TEXT("Invitation rules missing");return false;}
 TSet<FString> Keys;TArray<FOKStreetInvitation> Pending;
 for(const auto& Value:*Rows){auto J=Value->AsObject();FOKStreetInvitation Item;double Min=0,Max=0;
  if(!J.IsValid()||!OKStreetJsonInt(J->TryGetField(TEXT("personality")),Item.Personality)||!OKStreetJsonInt(J->TryGetField(TEXT("stage")),Item.Stage)||!OKStreetJsonInt(J->TryGetField(TEXT("command")),Item.Command)||Item.Stage<0||Item.Stage>2||Item.Command<0||Item.Command>4||!J->TryGetNumberField(TEXT("chance_min"),Min)||!J->TryGetNumberField(TEXT("chance_max"),Max)||!FMath::IsFinite(Min)||!FMath::IsFinite(Max)||Min<0||Max>100||Min>Max){Error=TEXT("Invitation rule malformed");return false;}
  Item.Min=Min;Item.Max=Max;J->TryGetStringField(TEXT("player_zh"),Item.PlayerZh);
  FString Key=FString::Printf(TEXT("%d/%d/%d"),Item.Personality,Item.Stage,Item.Command);if(Keys.Contains(Key)){Error=TEXT("Duplicate invitation");return false;}Keys.Add(Key);
  auto Read=[&](const TCHAR* Name,TArray<int32>& Destination){const TArray<TSharedPtr<FJsonValue>>* Ids=nullptr;if(!J->TryGetArrayField(Name,Ids))return false;for(const auto& V:*Ids){FString Id;if(!V->TryGetString(Id)||!Parsed.ById.Contains(Id))return false;int32 Index=Parsed.ById[Id];const auto& N=Parsed.Nodes[Index];if(N.PersonalityId!=Item.Personality||N.StageIndex!=Item.Stage||N.Command!=Item.Command||N.TextZh.IsEmpty()||!N.VoiceSequenceComplete)return false;Destination.Add(Catalogue.Nodes.Num()+Index);}return true;};
  if(!Read(TEXT("accept"),Item.Accept)||!Read(TEXT("refuse"),Item.Refuse)||Item.Accept.IsEmpty()||(Item.Stage<2&&Item.Refuse.IsEmpty())){Error=TEXT("Invitation response incomplete");return false;}Pending.Add(Item);
 }
 if(!Other.Nodes.IsEmpty()&&Other.Nodes.Num()!=Catalogue.Nodes.Num()){Error=TEXT("Invitation language indices differ");return false;}
 for(const auto& Node:Parsed.Nodes){int32 Index=Catalogue.Nodes.Add(Node);Catalogue.ById.Add(Node.Id,Index);if(!Other.Nodes.IsEmpty()){Other.Nodes.Add(Node);Other.ById.Add(Node.Id,Index);}}
 Out=MoveTemp(Pending);return true;
}
