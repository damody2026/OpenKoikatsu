#pragma once
#include "OKStreetSocialTopics.h"

// A shared failure row belongs to several branches. The caller must identify
// the selected branch rather than infer an unrelated question from that row.
inline FString OKStreetSourcePlayerPrompt(const FOKStreetCatalogue& Catalogue,int32 Node,const FString& BranchId=FString()){
 if(!Catalogue.Nodes.IsValidIndex(Node))return FString();
 const auto& Reply=Catalogue.Nodes[Node];
 if(!BranchId.IsEmpty())for(const auto& Branch:Catalogue.Branches)if(Branch.Id==BranchId){
  if(Branch.Personality!=Reply.PersonalityId||Branch.Stage!=Reply.StageIndex||!Catalogue.Nodes.IsValidIndex(Branch.IntroductionNode))return FString();
  const auto& Intro=Catalogue.Nodes[Branch.IntroductionNode];
  return Intro.VoiceFiles.IsEmpty()?OKStreetLineLabel(Intro.TextZh,Intro.TextJp,Intro.Id):FString();
 }
 if(!OKStreetIsGenericRole(Reply))return FString();
 const FOKStreetNode* Prompt=nullptr;
 for(const auto& Part:Catalogue.Nodes){
  if(Part.PersonalityId!=Reply.PersonalityId||Part.StageIndex!=Reply.StageIndex||Part.Group!=Reply.Group||Part.Command!=Reply.Command||Part.PartIndex>=Reply.PartIndex||!Part.VoiceFiles.IsEmpty()||!OKStreetIsGenericRole(Part))continue;
  bool Shared=false;for(int32 Container:Reply.Containers)if(Part.Containers.Contains(Container)){Shared=true;break;}
  if(Shared&&(!Prompt||Part.PartIndex>Prompt->PartIndex))Prompt=&Part;
 }
 return Prompt?OKStreetLineLabel(Prompt->TextZh,Prompt->TextJp,Prompt->Id):FString();
}

struct FOKStreetRelationshipDialogue {int32 Personality=INDEX_NONE;TArray<FOKStreetTopicPart> Accept,Refuse;};
inline bool OKStreetLoadRelationshipDialogue(const FString& Path,FOKStreetCatalogue& Catalogue,FOKStreetCatalogue& Other,TArray<FOKStreetRelationshipDialogue>& Out,FString& Error){
 FString Body;TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
 if(!FFileHelper::LoadFileToString(Body,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),Root)||!Root||!Root->TryGetArrayField(TEXT("profiles"),Rows)){Error=TEXT("Relationship dialogue missing");return false;}
 for(const auto& Value:*Rows){const auto J=Value->AsObject();FOKStreetRelationshipDialogue Item;const TArray<TSharedPtr<FJsonValue>>* Accept=nullptr;const TArray<TSharedPtr<FJsonValue>>* Refuse=nullptr;
  if(!J||!OKStreetJsonInt(J->TryGetField(TEXT("personality")),Item.Personality)||!J->TryGetArrayField(TEXT("accept"),Accept)||!J->TryGetArrayField(TEXT("refuse"),Refuse)||!OKStreetReadTopicParts(*Accept,Item.Personality,Item.Accept,Error)||!OKStreetReadTopicParts(*Refuse,Item.Personality,Item.Refuse,Error))return false;
  auto Add=[&](TArray<FOKStreetTopicPart>& Parts){for(auto& Part:Parts){if(Part.Line.VoiceFiles.IsEmpty())continue;Part.Line.Id=FString::Printf(TEXT("relationship-p%d-%d"),Item.Personality,Catalogue.Nodes.Num());Part.Line.Category=TEXT("relationship");Part.Node=Catalogue.Nodes.Add(Part.Line);if(!Other.Nodes.IsEmpty())Other.Nodes.Add(Part.Line);}};
  Add(Item.Accept);Add(Item.Refuse);Out.Add(MoveTemp(Item));
 }
 return !Out.IsEmpty();
}
