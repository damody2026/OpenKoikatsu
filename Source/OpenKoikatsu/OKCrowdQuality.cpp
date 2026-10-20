#include "OKCrowdQuality.h"
#include "OKCrowdCompact.h"
#include "OKCrowdHairOutline.h"
#include "OKCrowdMaterialLOD.h"
#if WITH_EDITOR
#include "KKImportedCharacter.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "LODUtilities.h"
#include "AssetCompilingManager.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Internationalization/Regex.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/SavePackage.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Json.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Animation/AnimSequence.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "MaterialBakingStructures.h"
#include "IMaterialBakingModule.h"
#include "Modules/ModuleManager.h"
#include "SkeletalRenderPublic.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "SkeletalMeshAttributes.h"
#include "Animation/Skeleton.h"
#include "KKRustExtract.h"
#include "KKExactSkinning.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "ok_extract.h"

namespace {
TMap<int32,double> DescriptionAreas(const FMeshDescription& Desc){FSkeletalMeshConstAttributes A(Desc);const auto P=A.GetVertexPositions();TMap<int32,double> Areas;for(auto T:Desc.Triangles().GetElementIDs()){const auto V=Desc.GetTriangleVertices(T);Areas.FindOrAdd(Desc.GetTrianglePolygonGroup(T).GetValue())+=FVector3f::CrossProduct(P[V[1]]-P[V[0]],P[V[2]]-P[V[0]]).Size()*.5;}return Areas;}
void RestoreLostSurfaces(FMeshDescription& Desc,const FMeshDescription& Authored,const FMeshDescription& Encoded,const FReferenceSkeleton& Reference){
 const auto OriginalAreas=DescriptionAreas(Authored),LowAreas=DescriptionAreas(Desc);TSet<int32> Restore;for(const auto& A:OriginalAreas)if(A.Value<100.||(LowAreas.FindRef(A.Key)<A.Value*.65||LowAreas.FindRef(A.Key)>A.Value*1.5))Restore.Add(A.Key);if(Restore.IsEmpty())return;
 TArray<FPolygonID> Remove;for(auto P:Desc.Polygons().GetElementIDs())if(Restore.Contains(Desc.GetPolygonPolygonGroup(P).GetValue()))Remove.Add(P);for(auto P:Remove)Desc.DeletePolygon(P);
 Desc.VertexAttributes().RegisterAttribute<int32>(TEXT("GameLowAuthored"),1,0);auto AuthoredFlags=Desc.VertexAttributes().GetAttributesRef<int32>(TEXT("GameLowAuthored"));FSkeletalMeshAttributes To(Desc);FSkeletalMeshConstAttributes From(Encoded);TMap<FVertexID,FVertexID> Vertices;
 for(auto Triangle:Encoded.Triangles().GetElementIDs()){const auto Group=Encoded.GetTrianglePolygonGroup(Triangle);if(!Restore.Contains(Group.GetValue()))continue;if(!Desc.IsPolygonGroupValid(Group))Desc.CreatePolygonGroupWithID(Group);To.GetPolygonGroupMaterialSlotNames()[Group]=From.GetPolygonGroupMaterialSlotNames()[Group];TArray<FVertexInstanceID> Corners;
  for(auto Instance:Encoded.GetTriangleVertexInstances(Triangle)){const auto Original=Encoded.GetVertexInstanceVertex(Instance);FVertexID Vertex;if(auto* Found=Vertices.Find(Original))Vertex=*Found;else{Vertex=Desc.CreateVertex();AuthoredFlags[Vertex]=1;Vertices.Add(Original,Vertex);To.GetVertexPositions()[Vertex]=From.GetVertexPositions()[Original];TArray<UE::AnimationCore::FBoneWeight> Weights;for(const auto W:From.GetVertexSkinWeights().Get(Original))Weights.Add(W);To.GetVertexSkinWeights().SetRaw(Vertex,Weights);}auto New=Desc.CreateVertexInstance(Vertex);Corners.Add(New);To.GetVertexInstanceNormals()[New]=From.GetVertexInstanceNormals()[Instance];To.GetVertexInstanceTangents()[New]=From.GetVertexInstanceTangents()[Instance];To.GetVertexInstanceBinormalSigns()[New]=From.GetVertexInstanceBinormalSigns()[Instance];To.GetVertexInstanceColors()[New]=From.GetVertexInstanceColors()[Instance];for(int32 UV=0;UV<From.GetVertexInstanceUVs().GetNumChannels();++UV)To.GetVertexInstanceUVs().Set(New,UV,From.GetVertexInstanceUVs().Get(Instance,UV));}
  Desc.CreateTriangle(Group,Corners);
 }
 FElementIDRemappings Remappings;Desc.Compact(Remappings);
 UE_LOG(LogTemp,Display,TEXT("Game low retained authored geometry for %d unsafe simplified material groups"),Restore.Num());
}
double UnposedArea(const FSkeletalMeshLODModel& Model,int32 Material){TArray<FVector3f> Positions;Positions.SetNum(Model.NumVertices);for(const auto& S:Model.Sections)for(int32 V=0;V<S.SoftVertices.Num();++V)Positions[S.BaseVertexIndex+V]=S.SoftVertices[V].Position;double Area=0;for(const auto& S:Model.Sections)if(S.MaterialIndex==Material)for(uint32 T=0;T<S.NumTriangles;++T){const uint32 I=S.BaseIndex+T*3;Area+=FVector3f::CrossProduct(Positions[Model.IndexBuffer[I+1]]-Positions[Model.IndexBuffer[I]],Positions[Model.IndexBuffer[I+2]]-Positions[Model.IndexBuffer[I]]).Size()*.5;}return Area;}
bool RecoverBlenderSurfaces(FMeshDescription& Low,const FMeshDescription& Canonical,const TArray<int32>& Groups,uint32 Reserve){
 FSkeletalMeshConstAttributes CA(Canonical);const int32 Channels=CA.GetVertexInstanceUVs().GetNumChannels();TMap<int32,int32> Counts,Budgets;int32 Minimum=0,Total=0;for(auto T:Canonical.Triangles().GetElementIDs()){const int32 G=Canonical.GetTrianglePolygonGroup(T).GetValue();if(Groups.Contains(G))++Counts.FindOrAdd(G);}for(int32 G:Groups){Minimum+=FMath::Min(12,Counts[G]);Total+=Counts[G];}if(Minimum>int32(Reserve)){UE_LOG(LogTemp,Error,TEXT("Crowd protected surface minimum=%d reserve=%u groups=%d"),Minimum,Reserve,Groups.Num());return false;}for(int32 G:Groups)Budgets.Add(G,FMath::Min(12,Counts[G])+int32((int64(Reserve)-Minimum)*Counts[G]/FMath::Max(1,Total)));TArray<TSharedPtr<FJsonValue>> JsonGroups;
 for(int32 Group:Groups){const int32 Limit=Budgets[Group];auto Entry=MakeShared<FJsonObject>();Entry->SetNumberField(TEXT("id"),Group);Entry->SetNumberField(TEXT("budget"),Limit);Entry->SetBoolField(TEXT("binding_hints"),!FParse::Param(FCommandLine::Get(),TEXT("CrowdLegacyBindingTransfer")));TArray<TSharedPtr<FJsonValue>> Triangles;
  for(auto T:Canonical.Triangles().GetElementIDs())if(Canonical.GetTrianglePolygonGroup(T).GetValue()==Group){TArray<TSharedPtr<FJsonValue>> Corners;for(auto I:Canonical.GetTriangleVertexInstances(T)){const auto P=CA.GetVertexPositions()[Canonical.GetVertexInstanceVertex(I)];TArray<TSharedPtr<FJsonValue>> Values;for(float V:{P.X,P.Y,P.Z})Values.Add(MakeShared<FJsonValueNumber>(V));for(int32 UV=0;UV<Channels;++UV){const auto U=CA.GetVertexInstanceUVs().Get(I,UV);Values.Add(MakeShared<FJsonValueNumber>(U.X));Values.Add(MakeShared<FJsonValueNumber>(U.Y));}Corners.Add(MakeShared<FJsonValueArray>(Values));}Triangles.Add(MakeShared<FJsonValueArray>(Corners));}Entry->SetArrayField(TEXT("triangles"),Triangles);JsonGroups.Add(MakeShared<FJsonValueObject>(Entry));
 }
 auto Request=MakeShared<FJsonObject>();Request->SetArrayField(TEXT("groups"),JsonGroups);Request->SetNumberField(TEXT("reserve"),Reserve);FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);if(!FJsonSerializer::Serialize(Request,Writer))return false;
 const FString Base=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Crowd/Blender")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*FPaths::GetPath(Base),true);if(!FFileHelper::SaveStringToFile(Text,*(Base+TEXT(".input.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))return false;
 FString Executable=TEXT("C:/Program Files/Blender Foundation/Blender 5.2/blender.exe");FParse::Value(FCommandLine::Get(),TEXT("CrowdBlender="),Executable);const FString Script=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Tools/blender_crowd_surfaces.py"));if(!IFileManager::Get().FileExists(*Executable))return false;const FString Args=FString::Printf(TEXT("--background --factory-startup --python-exit-code 1 --python \"%s\" -- \"%s.input.json\" \"%s.output.json\""),*Script,*Base,*Base);int32 Exit=-1;FString Stdout,Stderr;FString Reuse,CachedInput;bool Reused=false;if(FParse::Value(FCommandLine::Get(),TEXT("CrowdBlenderReuse="),Reuse)&&FFileHelper::LoadFileToString(CachedInput,*(Reuse+TEXT(".input.json")))&&([&](){auto GeometryOnly=[](const FString& Input){const FRegexPattern Pattern(TEXT("\"(?:budget|reserve)\"\\s*:\\s*[0-9]+"));FRegexMatcher Matcher(Pattern,Input);FString Result;int32 Last=0;while(Matcher.FindNext()){Result+=Input.Mid(Last,Matcher.GetMatchBeginning()-Last);Result+=TEXT("\"allocation\":0");Last=Matcher.GetMatchEnding();}Result+=Input.Mid(Last);return Result;};return GeometryOnly(CachedInput)==GeometryOnly(Text);}())&&IFileManager::Get().Copy(*(Base+TEXT(".output.json")),*(Reuse+TEXT(".output.json")))==COPY_OK){Reused=true;Exit=0;Stdout=TEXT("Reused geometry after exact canonical geometry equality; allocation checked separately: ")+Reuse;}CachedInput.Empty();const bool Ran=Reused||FPlatformProcess::ExecProcess(*Executable,*Args,&Exit,&Stdout,&Stderr);FFileHelper::SaveStringToFile(Stdout+Stderr,*(Base+TEXT(".log")));if(!Ran||Exit!=0){UE_LOG(LogTemp,Error,TEXT("Crowd Blender failed exit=%d evidence=%s"),Exit,*Base);return false;}
 TSharedPtr<FJsonObject> Response;if(!FFileHelper::LoadFileToString(Text,*(Base+TEXT(".output.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Response)||!Response)return false;const TArray<TSharedPtr<FJsonValue>>* Results=nullptr;if(!Response->TryGetArrayField(TEXT("groups"),Results)||Results->Num()!=Groups.Num())return false;
 FSkeletalMeshAttributes LA(Low);LA.Register(true);Low.VertexAttributes().RegisterAttribute<int32>(TEXT("GameLowSourceVertex"),1,-1);auto SourceHints=Low.VertexAttributes().GetAttributesRef<int32>(TEXT("GameLowSourceVertex"));LA.GetVertexInstanceUVs().SetNumChannels(Channels);TSet<int32> Seen;uint32 TotalRecovered=0;
 for(const auto& Value:*Results){const auto Entry=Value->AsObject();if(!Entry)return false;const int32 Group=Entry->GetIntegerField(TEXT("id"));const int32 Limit=Budgets.FindRef(Group);const auto& Triangles=Entry->GetArrayField(TEXT("triangles"));if(!Groups.Contains(Group)||Seen.Contains(Group)||Triangles.IsEmpty()||Triangles.Num()>int32(Reserve))return false;TotalRecovered+=Triangles.Num();if(TotalRecovered>Reserve)return false;Seen.Add(Group);const FName Slot=CA.GetPolygonGroupMaterialSlotNames()[FPolygonGroupID(Group)];FPolygonGroupID PG;for(auto G:Low.PolygonGroups().GetElementIDs())if(LA.GetPolygonGroupMaterialSlotNames()[G]==Slot){PG=G;break;}if(PG.GetValue()<0)PG=Low.CreatePolygonGroup();TArray<FPolygonID> Remove;for(auto P:Low.Polygons().GetElementIDs())if(Low.GetPolygonPolygonGroup(P)==PG)Remove.Add(P);for(auto P:Remove)Low.DeletePolygon(P);LA.GetPolygonGroupMaterialSlotNames()[PG]=Slot;TArray<FVertexInstanceID> Original;for(auto T:Canonical.Triangles().GetElementIDs())if(Canonical.GetTrianglePolygonGroup(T).GetValue()==Group)for(auto I:Canonical.GetTriangleVertexInstances(T))Original.Add(I);
  const TArray<TSharedPtr<FJsonValue>>* BindingIndices=nullptr;Entry->TryGetArrayField(TEXT("binding_indices"),BindingIndices);if(BindingIndices&&BindingIndices->Num()!=Triangles.Num())return false;int32 TriangleIndex=0;for(const auto& Triangle:Triangles){const auto& Corners=Triangle->AsArray();if(Corners.Num()!=3)return false;TArray<FVertexInstanceID> Instances;int32 CornerIndex=0;for(const auto& Corner:Corners){const auto& Numbers=Corner->AsArray();if(Numbers.Num()!=3+Channels*2)return false;const FVector3f P(Numbers[0]->AsNumber(),Numbers[1]->AsNumber(),Numbers[2]->AsNumber());if(P.ContainsNaN())return false;FVertexInstanceID Nearest;float Distance=TNumericLimits<float>::Max();for(auto I:Original){const float D=(CA.GetVertexPositions()[Canonical.GetVertexInstanceVertex(I)]-P).SizeSquared();if(D<Distance){Distance=D;Nearest=I;}}if(BindingIndices){const auto& Indices=(*BindingIndices)[TriangleIndex]->AsArray();if(Indices.Num()!=3)return false;const int32 Hint=int32(Indices[CornerIndex]->AsNumber());if(!Original.IsValidIndex(Hint))return false;Nearest=Original[Hint];}if(Nearest.GetValue()<0)return false;const auto V=Low.CreateVertex();if(BindingIndices)SourceHints[V]=Canonical.GetVertexInstanceVertex(Nearest).GetValue();++CornerIndex;LA.GetVertexPositions()[V]=P;const auto I=Low.CreateVertexInstance(V);Instances.Add(I);LA.GetVertexInstanceNormals()[I]=CA.GetVertexInstanceNormals()[Nearest];LA.GetVertexInstanceTangents()[I]=CA.GetVertexInstanceTangents()[Nearest];LA.GetVertexInstanceBinormalSigns()[I]=CA.GetVertexInstanceBinormalSigns()[Nearest];LA.GetVertexInstanceColors()[I]=CA.GetVertexInstanceColors()[Nearest];for(int32 UV=0;UV<Channels;++UV)LA.GetVertexInstanceUVs().Set(I,UV,FVector2f(Numbers[3+UV*2]->AsNumber(),Numbers[4+UV*2]->AsNumber()));}Low.CreateTriangle(PG,Instances);++TriangleIndex;}
  UE_LOG(LogTemp,Display,TEXT("Crowd Blender recovered material=%d triangles=%d limit=%d evidence=%s"),Group,Triangles.Num(),Limit,*Base);
 }
 FElementIDRemappings Remappings;Low.Compact(Remappings);return true;
}
// Recover small missing features with a bounded derivative, never the entire
// original high section. Positions/bindings come from authored vertices.
bool RecoverSmallSurfaces(FMeshDescription& Low,const FMeshDescription& Canonical,const FMeshDescription& Encoded,uint32 Reserve,bool AllSurfaces=false){
 FString Keep;TArray<FString> KeepValues;TSet<int32> KeepGroups;if(FParse::Value(FCommandLine::Get(),TEXT("CrowdKeepMaterials="),Keep)){Keep.ParseIntoArray(KeepValues,TEXT(","));for(const auto& Value:KeepValues){int32 Group;if(!LexTryParseString(Group,*Value))return false;KeepGroups.Add(Group);}}uint32 KeptTriangles=0;for(auto T:Canonical.Triangles().GetElementIDs())if(KeepGroups.Contains(Canonical.GetTrianglePolygonGroup(T).GetValue()))++KeptTriangles;if(KeptTriangles>Reserve){UE_LOG(LogTemp,Error,TEXT("Crowd kept surfaces exceed reserve triangles=%u reserve=%u"),KeptTriangles,Reserve);return false;}Reserve-=KeptTriangles;const bool Blender=FParse::Param(FCommandLine::Get(),TEXT("BlenderSurfaces"));const auto Areas=DescriptionAreas(Canonical),Reduced=DescriptionAreas(Low);FSkeletalMeshConstAttributes Names(Canonical),LowNames(Low);TArray<int32> Groups;for(const auto& A:Areas){if(KeepGroups.Contains(A.Key))continue;double LowArea=0;const FName Slot=Names.GetPolygonGroupMaterialSlotNames()[FPolygonGroupID(A.Key)];for(auto G:Low.PolygonGroups().GetElementIDs())if(LowNames.GetPolygonGroupMaterialSlotNames()[G]==Slot)LowArea+=Reduced.FindRef(G.GetValue());if(A.Value>10&&(((A.Value<100||AllSurfaces)&&LowArea<A.Value*.3)||(Blender&&LowArea<A.Value*.1)))Groups.Add(A.Key);}if(Groups.IsEmpty())return true;if(Blender)return RecoverBlenderSurfaces(Low,Canonical,Groups,Reserve);
 const int32 Limit=FMath::Max(4,int32(Reserve)/Groups.Num());FSkeletalMeshConstAttributes CA(Canonical),EA(Encoded);FSkeletalMeshAttributes LA(Low);LA.Register(true);Low.VertexAttributes().RegisterAttribute<int32>(TEXT("GameLowAuthored"),1,0);auto Flags=Low.VertexAttributes().GetAttributesRef<int32>(TEXT("GameLowAuthored"));
 for(int32 Group:Groups){TArray<FTriangleID> Selected;TMap<FVertexID,FVertexID> Representative;bool Fits=false;
  for(float Cell=.02f;Cell<=8&&!Fits;Cell*=1.5f){Selected.Reset();Representative.Reset();TMap<FIntVector,FVertexID> Grid;TSet<FIntVector> Faces;
   for(auto T:Canonical.Triangles().GetElementIDs())if(Canonical.GetTrianglePolygonGroup(T).GetValue()==Group){FIntVector Key=FIntVector::ZeroValue;int32 Corner=0;for(auto V:Canonical.GetTriangleVertices(T)){const FVector3f P=CA.GetVertexPositions()[V];const FIntVector Bucket(FMath::FloorToInt(P.X/Cell),FMath::FloorToInt(P.Y/Cell),FMath::FloorToInt(P.Z/Cell));auto* R=Grid.Find(Bucket);const auto Anchor=R?*R:Grid.Add(Bucket,V);Representative.Add(V,Anchor);Key[Corner++]=Anchor.GetValue();}if(Key.X==Key.Y||Key.X==Key.Z||Key.Y==Key.Z)continue;int32 Ordered[3]={Key.X,Key.Y,Key.Z};if(Ordered[0]>Ordered[1])Swap(Ordered[0],Ordered[1]);if(Ordered[1]>Ordered[2])Swap(Ordered[1],Ordered[2]);if(Ordered[0]>Ordered[1])Swap(Ordered[0],Ordered[1]);const FIntVector Face(Ordered[0],Ordered[1],Ordered[2]);if(!Faces.Contains(Face)){Faces.Add(Face);Selected.Add(T);}}
   Fits=!Selected.IsEmpty()&&Selected.Num()<=Limit;
  }
  if(!Fits){UE_LOG(LogTemp,Error,TEXT("Crowd small surface recovery cannot fit material=%d limit=%d"),Group,Limit);return false;}
  const FName Slot=EA.GetPolygonGroupMaterialSlotNames()[FPolygonGroupID(Group)];FPolygonGroupID PG;for(auto G:Low.PolygonGroups().GetElementIDs())if(LA.GetPolygonGroupMaterialSlotNames()[G]==Slot){PG=G;break;}if(PG.GetValue()<0)PG=Low.CreatePolygonGroup();TArray<FPolygonID> Remove;for(auto P:Low.Polygons().GetElementIDs())if(Low.GetPolygonPolygonGroup(P)==PG)Remove.Add(P);for(auto P:Remove)Low.DeletePolygon(P);LA.GetPolygonGroupMaterialSlotNames()[PG]=Slot;LA.GetVertexInstanceUVs().SetNumChannels(EA.GetVertexInstanceUVs().GetNumChannels());TMap<FVertexID,FVertexID> Vertices;
  for(auto T:Selected){TArray<FVertexInstanceID> Corners;for(auto I:Encoded.GetTriangleVertexInstances(T)){const auto Anchor=Representative[Encoded.GetVertexInstanceVertex(I)];FVertexID V;if(auto* Found=Vertices.Find(Anchor))V=*Found;else{V=Low.CreateVertex();Vertices.Add(Anchor,V);Flags[V]=0;LA.GetVertexPositions()[V]=CA.GetVertexPositions()[Anchor];TArray<UE::AnimationCore::FBoneWeight> W;for(auto Weight:CA.GetVertexSkinWeights().Get(Anchor))W.Add(Weight);LA.GetVertexSkinWeights().SetRaw(V,W);}const auto N=Low.CreateVertexInstance(V);Corners.Add(N);LA.GetVertexInstanceNormals()[N]=CA.GetVertexInstanceNormals()[I];LA.GetVertexInstanceTangents()[N]=CA.GetVertexInstanceTangents()[I];LA.GetVertexInstanceBinormalSigns()[N]=EA.GetVertexInstanceBinormalSigns()[I];LA.GetVertexInstanceColors()[N]=EA.GetVertexInstanceColors()[I];for(int32 UV=0;UV<EA.GetVertexInstanceUVs().GetNumChannels();++UV)LA.GetVertexInstanceUVs().Set(N,UV,EA.GetVertexInstanceUVs().Get(I,UV));}Low.CreateTriangle(PG,Corners);}
  UE_LOG(LogTemp,Display,TEXT("Crowd small surface recovered material=%d triangles=%d limit=%d area=%f"),Group,Selected.Num(),Limit,Areas.FindRef(Group));
 }
 FElementIDRemappings Remappings;Low.Compact(Remappings);return true;
}
bool KeepAuthoredSurfaces(FMeshDescription& Low,const FMeshDescription& Encoded){
 FString Keep;if(!FParse::Value(FCommandLine::Get(),TEXT("CrowdKeepMaterials="),Keep))return true;TArray<FString> Values;Keep.ParseIntoArray(Values,TEXT(","));FSkeletalMeshConstAttributes From(Encoded);FSkeletalMeshAttributes To(Low);Low.VertexAttributes().RegisterAttribute<int32>(TEXT("GameLowAuthored"),1,0);auto Flags=Low.VertexAttributes().GetAttributesRef<int32>(TEXT("GameLowAuthored"));
 for(const auto& Value:Values){int32 Group;if(!LexTryParseString(Group,*Value))return false;if(!Encoded.IsPolygonGroupValid(FPolygonGroupID(Group)))continue;const FName Slot=From.GetPolygonGroupMaterialSlotNames()[FPolygonGroupID(Group)];FPolygonGroupID PG;for(auto G:Low.PolygonGroups().GetElementIDs())if(To.GetPolygonGroupMaterialSlotNames()[G]==Slot){PG=G;break;}if(PG.GetValue()<0)PG=Low.CreatePolygonGroup();To.GetPolygonGroupMaterialSlotNames()[PG]=Slot;TArray<FPolygonID> Remove;for(auto P:Low.Polygons().GetElementIDs())if(Low.GetPolygonPolygonGroup(P)==PG)Remove.Add(P);for(auto P:Remove)Low.DeletePolygon(P);TMap<FVertexID,FVertexID> Vertices;int32 Count=0;
  for(auto T:Encoded.Triangles().GetElementIDs())if(Encoded.GetTrianglePolygonGroup(T).GetValue()==Group){TArray<FVertexInstanceID> Corners;for(auto I:Encoded.GetTriangleVertexInstances(T)){const auto Original=Encoded.GetVertexInstanceVertex(I);FVertexID V;if(auto* Found=Vertices.Find(Original))V=*Found;else{V=Low.CreateVertex();Vertices.Add(Original,V);Flags[V]=1;To.GetVertexPositions()[V]=From.GetVertexPositions()[Original];TArray<UE::AnimationCore::FBoneWeight> Weights;for(const auto W:From.GetVertexSkinWeights().Get(Original))Weights.Add(W);To.GetVertexSkinWeights().SetRaw(V,Weights);}const auto N=Low.CreateVertexInstance(V);Corners.Add(N);To.GetVertexInstanceNormals()[N]=From.GetVertexInstanceNormals()[I];To.GetVertexInstanceTangents()[N]=From.GetVertexInstanceTangents()[I];To.GetVertexInstanceBinormalSigns()[N]=From.GetVertexInstanceBinormalSigns()[I];To.GetVertexInstanceColors()[N]=From.GetVertexInstanceColors()[I];for(int32 UV=0;UV<From.GetVertexInstanceUVs().GetNumChannels();++UV)To.GetVertexInstanceUVs().Set(N,UV,From.GetVertexInstanceUVs().Get(I,UV));}Low.CreateTriangle(PG,Corners);++Count;}
  UE_LOG(LogTemp,Display,TEXT("Crowd kept authored surface material=%d triangles=%d full_weights=1"),Group,Count);
 }FElementIDRemappings Remappings;Low.Compact(Remappings);return true;
}
// Offline only. Existing high-detail clips and runtime physics are untouched.
struct FNoClothDynamicsScope {
 FString Previous=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NO_CLOTH_DYNAMICS"));
 FNoClothDynamicsScope(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_NO_CLOTH_DYNAMICS"),TEXT("1"));}
 ~FNoClothDynamicsScope(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_NO_CLOTH_DYNAMICS"),*Previous);}
};
UAnimSequence* BakeRigidClothes(UKKImportedCharacter* NativeCard,const FKKImportedAnimation& Entry,const FString& Path){
 if(!NativeCard||NativeCard->Outfits.IsEmpty()||!Entry.Sequence)return nullptr;
 const auto& Outfit=NativeCard->Outfits[0];
 auto* Shared=LoadObject<UKKSharedAnimation>(nullptr,TEXT("/Game/Scenes/CrowdRoom/SharedWalk/Shared_0000.Shared_0000"));
 if(!Shared||Shared->Source!=Entry.Source||Shared->Clip!=Entry.Clip)return nullptr;
 FNoClothDynamicsScope Policy;FKKScopedNativeSkinSource NativeSource; struct FOfflineSession { OkRustBindings API{};OkSession* Handle=nullptr; FOfflineSession(){ok_register_bindings(OkUnrealBindings{},&API);} ~FOfflineSession(){if(Handle)API.session_free(Handle);} bool OpenAnimationSnapshot(const TArray<uint8>& Bytes){Handle=ok_session_open_animation_snapshot(Bytes.GetData(),Bytes.Num());return Handle!=nullptr;} bool InstallSharedClip(const TArray<uint8>& Bytes){return ok_session_install_shared_clip(Handle,Bytes.GetData(),Bytes.Num())==0;} bool ApplyAnimFrame(float Time,float Delta){return API.apply_anim_frame(Handle,Time,Delta)==0;} int32 SkinPalette(int32 Part,OkSkinPaletteView& View,TArray<float>& Values){const int32 Count=ok_part_skin_palette(Handle,Part,nullptr,nullptr,0);if(Count<=0||Count>65535)return -1;Values.SetNumUninitialized(Count*16);return ok_part_skin_palette(Handle,Part,&View,Values.GetData(),Count);} } Session;
 if(!Session.OpenAnimationSnapshot(Outfit.AnimationSnapshot)||!Session.InstallSharedClip(Shared->AuthoredCurves))return nullptr;
 auto* Sequence=Cast<UAnimSequence>(StaticDuplicateObject(Entry.Sequence,CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path)));if(!Sequence)return nullptr;Sequence->SetPreviewMesh(nullptr);
 const auto& Ref=Sequence->GetSkeleton()->GetReferenceSkeleton();const auto* Model=Entry.Sequence->GetDataModelInterface().GetInterface();const int32 Frames=Model->GetNumberOfFrames();const double Step=Model->GetFrameRate().AsInterval();
 struct FClothTrack {int32 Part,Slot,Axis;FName Bone;TArray<FVector> Positions,Scales;TArray<FQuat> Rotations;};TArray<FClothTrack> Tracks;
 for(const auto& Part:Outfit.Parts)if(Part.Role.Contains(TEXT("clothes"),ESearchCase::IgnoreCase))for(int32 Slot:Part.PaletteSlots)for(int32 Axis=0;Axis<3;++Axis){const FName Bone=OKPaletteBoneName(Part.NativePartIndex,Slot,Axis);if(Ref.FindBoneIndex(Bone)!=INDEX_NONE){FClothTrack Track;Track.Part=Part.NativePartIndex;Track.Slot=Slot;Track.Axis=Axis;Track.Bone=Bone;Tracks.Add(MoveTemp(Track));}}
 if(Tracks.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("Rigid clothes bake has no clothing palette tracks"));return nullptr;}
 for(int32 Frame=0;Frame<=Frames;++Frame){if(!Session.ApplyAnimFrame(Frame*Step,Frame==0?0.f:Step))return nullptr;TMap<int32,TArray<FMatrix>> Palettes;
  for(auto& Track:Tracks){if(!Palettes.Contains(Track.Part)){OkSkinPaletteView Raw{};TArray<float> Matrices;if(Session.SkinPalette(Track.Part,Raw,Matrices)<=0)return nullptr;TArray<FMatrix> Values;Values.SetNum(Matrices.Num()/16);for(int32 Slot=0;Slot<Values.Num();++Slot)if(!OKExactPaletteMatrix(Raw,Matrices,Slot,Values[Slot]))return nullptr;Palettes.Add(Track.Part,MoveTemp(Values));}
   // Signed per-vertex palette slots must be sampled through the exact helper.
   FMatrix Matrix;if(Track.Slot>=0&&Palettes[Track.Part].IsValidIndex(Track.Slot))Matrix=Palettes[Track.Part][Track.Slot];else{OkSkinPaletteView Raw{};TArray<float> Matrices;if(Session.SkinPalette(Track.Part,Raw,Matrices)<=0||!OKExactPaletteMatrix(Raw,Matrices,Track.Slot,Matrix))return nullptr;}
   const FTransform Pose=Track.Rotations.IsEmpty()?OKSkinPaletteColumn(Matrix,Track.Axis):OKSkinPaletteColumnContinuous(Matrix,Track.Axis,Track.Rotations.Last());if(Pose.ContainsNaN())return nullptr;Track.Positions.Add(Pose.GetTranslation());Track.Rotations.Add(Pose.GetRotation());Track.Scales.Add(Pose.GetScale3D());
  }
 }
 auto& Controller=Sequence->GetController();Controller.OpenBracket(NSLOCTEXT("KK","RigidClothesBake","Bake gameplay clothes without springs"),false);bool Good=true;
 for(const auto& Track:Tracks)Good=Controller.SetBoneTrackKeys(Track.Bone,Track.Positions,Track.Rotations,Track.Scales,false)&&Good;Controller.NotifyPopulated();Controller.CloseBracket(false);if(!Good)return nullptr;
 Sequence->PostEditChange();Sequence->CacheDerivedDataForCurrentPlatform();Sequence->WaitOnExistingCompression();
 double Maximum=0.;for(const auto& Track:Tracks)for(int32 Frame=0;Frame<=Frames;++Frame){FTransform Pose;FAnimExtractContext Context(Frame*Step);Context.bEnforceCompressedDataSampling=true;Sequence->GetBoneTransform(Pose,FSkeletonPoseBoneIndex(Ref.FindBoneIndex(Track.Bone)),Context,false);Maximum=FMath::Max(Maximum,FVector::Distance(Pose.GetTranslation(),Track.Positions[Frame]));if(Pose.ContainsNaN()||Maximum>.01||!Pose.GetScale3D().Equals(Track.Scales[Frame],.001)||Pose.GetRotation().AngularDistance(Track.Rotations[Frame])>.001){UE_LOG(LogTemp,Error,TEXT("Rigid clothes compressed key differs from native sample bone=%s frame=%d error=%f"),*Track.Bone.ToString(),Frame,Maximum);return nullptr;}}
 UE_LOG(LogTemp,Display,TEXT("Crowd rigid clothes compressed audit max_position_cm=%.9f"),Maximum);
 UE_LOG(LogTemp,Display,TEXT("Crowd rigid clothes baked tracks=%d frames=%d frequency=%.3f high_clip_preserved=1"),Tracks.Num(),Frames,Model->GetFrameRate().AsDecimal());return Sequence;
}
TArray<FMatrix44f> ReferenceSkinMatrices(USkeletalMesh* Mesh,UAnimSequence* Animation){
 TArray<FMatrix44f> Component,Matrices;if(!Animation)return Matrices;const auto& Ref=Mesh->GetRefSkeleton();const auto& Skeleton=Animation->GetSkeleton()->GetReferenceSkeleton();FAnimExtractContext Context(0.);Context.bEnforceCompressedDataSampling=true;
 for(int32 Bone=0;Bone<Ref.GetNum();++Bone){const int32 Index=Skeleton.FindBoneIndex(Ref.GetBoneName(Bone));if(Index==INDEX_NONE)return {};FTransform Local;Animation->GetBoneTransform(Local,FSkeletonPoseBoneIndex(Index),Context,false);FMatrix44f Transform(Local.ToMatrixWithScale());const int32 Parent=Ref.GetParentIndex(Bone);if(Parent!=INDEX_NONE)Transform=Transform*Component[Parent];Component.Add(Transform);Matrices.Add(Mesh->GetRefBasesInvMatrix()[Bone]*Transform);}return Matrices;
}
template<class T> FMatrix44f VertexBlend(const T& Weights,const TArray<FMatrix44f>& Matrices){
 FMatrix44f Blend(ForceInit);for(const auto W:Weights){const float Weight=float(W.GetRawWeight())/65535.f;const auto& M=Matrices[W.GetBoneIndex()];for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Blend.M[R][C]+=M.M[R][C]*Weight;}return Blend;
}
// Reduce geometry in posed space; never let the reducer interpolate affine columns.
#include "OKCrowdBindingCorrespondence.inl"
bool TransferAuthoredBindings(FMeshDescription& Low,const FMeshDescription& Canonical,const FMeshDescription& Encoded,const TArray<FMatrix44f>& Reference){
 FSkeletalMeshAttributes To(Low);FSkeletalMeshConstAttributes From(Canonical),Raw(Encoded);const auto OriginalPositions=From.GetVertexPositions();auto Positions=To.GetVertexPositions();auto Weights=To.GetVertexSkinWeights();auto Flags=Low.VertexAttributes().GetAttributesRef<int32>(TEXT("GameLowAuthored"));
 const float CellSize=2.f;auto Cell=[&](FVector3f P){return FIntVector(FMath::FloorToInt(P.X/CellSize),FMath::FloorToInt(P.Y/CellSize),FMath::FloorToInt(P.Z/CellSize));};
 TMap<int32,TMap<FIntVector,TArray<FVertexID>>> Grid;for(auto T:Canonical.Triangles().GetElementIDs())for(auto V:Canonical.GetTriangleVertices(T))Grid.FindOrAdd(Canonical.GetTrianglePolygonGroup(T).GetValue()).FindOrAdd(Cell(OriginalPositions[V])).AddUnique(V);
 TMap<int32,int32> GroupMap;for(auto G:Low.PolygonGroups().GetElementIDs()){const FName Slot=To.GetPolygonGroupMaterialSlotNames()[G];for(auto Original:Canonical.PolygonGroups().GetElementIDs())if(From.GetPolygonGroupMaterialSlotNames()[Original]==Slot){GroupMap.Add(G.GetValue(),Original.GetValue());break;}}
 TMap<FVertexID,int32> Materials;for(auto T:Low.Triangles().GetElementIDs()){const auto* Original=GroupMap.Find(Low.GetTrianglePolygonGroup(T).GetValue());if(!Original)return false;for(auto V:Low.GetTriangleVertices(T))Materials.FindOrAdd(V)=*Original;}
 FBindingUVSearch UVSearch(Encoded,Reference);float MaximumDistance=0;for(auto V:Low.Vertices().GetElementIDs()){if(Flags.IsValid()&&Flags[V])continue;const auto* Material=Materials.Find(V);if(!Material)continue;const auto* Cells=Grid.Find(*Material);if(!Cells)return false;const auto Center=Cell(Positions[V]);FVertexID Closest;float Distance=TNumericLimits<float>::Max();
  for(int32 Radius=1;Radius<=6;++Radius){for(int32 X=-Radius;X<=Radius;++X)for(int32 Y=-Radius;Y<=Radius;++Y)for(int32 Z=-Radius;Z<=Radius;++Z){if(Radius>1&&FMath::Max3(FMath::Abs(X),FMath::Abs(Y),FMath::Abs(Z))!=Radius)continue;if(const auto* Candidates=Cells->Find(Center+FIntVector(X,Y,Z)))for(auto Original:*Candidates){const float D=(OriginalPositions[Original]-Positions[V]).SizeSquared();if(D<Distance){Distance=D;Closest=Original;}}}if(Distance<FMath::Square((Radius-1)*CellSize))break;}
  const auto SourceHints=Low.VertexAttributes().GetAttributesRef<int32>(TEXT("GameLowSourceVertex"));const bool ExplicitHint=SourceHints.IsValid()&&SourceHints[V]>=0&&Encoded.IsVertexValid(FVertexID(SourceHints[V]));if(ExplicitHint){Closest=FVertexID(SourceHints[V]);Distance=(OriginalPositions[Closest]-Positions[V]).SizeSquared();}const auto Instances=Low.GetVertexVertexInstanceIDs(V);if(!ExplicitHint&&!Instances.IsEmpty()){FVertexID UVMatch;float UVError=0;const FName Slot=From.GetPolygonGroupMaterialSlotNames()[FPolygonGroupID(*Material)];if(UVSearch.Find(Slot,Positions[V],To.GetVertexInstanceUVs().Get(Instances[0],0),UVMatch,UVError)){Closest=UVMatch;Distance=(OriginalPositions[Closest]-Positions[V]).SizeSquared();}}
  if(Distance>100.f){UE_LOG(LogTemp,Error,TEXT("Game low binding transfer too far material=%d distance_cm=%f"),*Material,FMath::Sqrt(Distance));return false;}MaximumDistance=FMath::Max(MaximumDistance,FMath::Sqrt(Distance));TArray<UE::AnimationCore::FBoneWeight> Original;for(const auto W:From.GetVertexSkinWeights().Get(Closest))Original.Add(W);if(Original.Num()>12){FString Detail;for(auto W:Original)Detail+=FString::Printf(TEXT(" %d:%u"),W.GetBoneIndex(),W.GetRawWeight());UE_LOG(LogTemp,Warning,TEXT("Crowd binding has %d influences material=%d vertex=%d weights=%s"),Original.Num(),*Material,V.GetValue(),*Detail);}Weights.SetRaw(V,Original);const auto Blend=VertexBlend(Weights.Get(V),Reference);if(FMath::Abs(Blend.Determinant())<1e-10f)return false;const auto Inverse=Blend.Inverse();Positions[V]=Raw.GetVertexPositions()[Closest]+FVector3f(Inverse.TransformVector(Positions[V]-OriginalPositions[Closest]));
  for(auto Instance:Low.GetVertexVertexInstanceIDs(V)){To.GetVertexInstanceNormals()[Instance]=FVector3f(Inverse.TransformVector(To.GetVertexInstanceNormals()[Instance])).GetSafeNormal();To.GetVertexInstanceTangents()[Instance]=FVector3f(Inverse.TransformVector(To.GetVertexInstanceTangents()[Instance])).GetSafeNormal();}
 }
 UE_LOG(LogTemp,Display,TEXT("Game low authored binding transfer max_distance_cm=%f"),MaximumDistance);return true;
}
FBox3f SkinBounds(USkeletalMeshComponent* Skin,int32 LOD){
 FBox3f Box(ForceInit);const auto* Mesh=Skin->GetSkeletalMeshAsset();const auto* Model=Mesh?Mesh->GetImportedModel():nullptr;if(!Model||!Model->LODModels.IsValidIndex(LOD))return Box;
 const auto& Pose=Skin->GetComponentSpaceTransforms();const auto& Inverse=Mesh->GetRefBasesInvMatrix();TArray<FMatrix44f> Matrices;
 if(Pose.Num()!=Inverse.Num())return Box;for(int32 Bone=0;Bone<Pose.Num();++Bone)Matrices.Add(Inverse[Bone]*FMatrix44f(Pose[Bone].ToMatrixWithScale()));
 for(const auto& Section:Model->LODModels[LOD].Sections)for(const auto& V:Section.SoftVertices){FVector3f P=FVector3f::ZeroVector;uint32 Total=0;for(int32 Slot=0;Slot<MAX_TOTAL_INFLUENCES;++Slot)if(V.InfluenceWeights[Slot]){if(!Section.BoneMap.IsValidIndex(V.InfluenceBones[Slot]))return FBox3f(ForceInit);const int32 Bone=Section.BoneMap[V.InfluenceBones[Slot]];if(!Matrices.IsValidIndex(Bone))return FBox3f(ForceInit);P+=FVector3f(Matrices[Bone].TransformPosition(V.Position))*(float(V.InfluenceWeights[Slot])/65535.f);Total+=V.InfluenceWeights[Slot];}if(Total==0||P.ContainsNaN())return FBox3f(ForceInit);Box+=P;}
 return Box;
}
TMap<int32,double> SkinSurfaceAreas(USkeletalMeshComponent* Skin,int32 LOD,TArray<FVector3f>* OutPositions=nullptr){
 TMap<int32,double> Areas;const auto* Mesh=Skin->GetSkeletalMeshAsset();const auto& Model=Mesh->GetImportedModel()->LODModels[LOD];const auto& Pose=Skin->GetComponentSpaceTransforms();TArray<FMatrix44f> Matrices;for(int32 Bone=0;Bone<Pose.Num();++Bone)Matrices.Add(Mesh->GetRefBasesInvMatrix()[Bone]*FMatrix44f(Pose[Bone].ToMatrixWithScale()));TArray<FVector3f> Positions;Positions.SetNum(Model.NumVertices);
 for(const auto& Section:Model.Sections)for(int32 Vertex=0;Vertex<Section.SoftVertices.Num();++Vertex){const auto& V=Section.SoftVertices[Vertex];FVector3f P=FVector3f::ZeroVector;for(int32 Slot=0;Slot<MAX_TOTAL_INFLUENCES;++Slot)if(V.InfluenceWeights[Slot])P+=FVector3f(Matrices[Section.BoneMap[V.InfluenceBones[Slot]]].TransformPosition(V.Position))*(float(V.InfluenceWeights[Slot])/65535.f);Positions[Section.BaseVertexIndex+Vertex]=P;}
 for(const auto& Section:Model.Sections)for(uint32 Triangle=0;Triangle<Section.NumTriangles;++Triangle){const uint32 Index=Section.BaseIndex+Triangle*3;const auto A=Positions[Model.IndexBuffer[Index]],B=Positions[Model.IndexBuffer[Index+1]],C=Positions[Model.IndexBuffer[Index+2]];Areas.FindOrAdd(Section.MaterialIndex)+=FVector3f::CrossProduct(B-A,C-A).Size()*.5;}
 if(OutPositions)*OutPositions=MoveTemp(Positions);return Areas;
}
bool SmallSurfaceCoverage(const FSkeletalMeshLODModel& Source,const TArray<FVector3f>& SourcePositions,const FSkeletalMeshLODModel& Low,const TArray<FVector3f>& LowPositions,int32 Material,float Tolerance){
 struct FFace{FVector A,B,C;};auto Faces=[Material](const FSkeletalMeshLODModel& Model,const TArray<FVector3f>& Positions){TArray<FFace> Result;for(const auto& S:Model.Sections)if(S.MaterialIndex==Material)for(uint32 T=0;T<S.NumTriangles;++T){const uint32 I=S.BaseIndex+T*3;Result.Add({FVector(Positions[Model.IndexBuffer[I]]),FVector(Positions[Model.IndexBuffer[I+1]]),FVector(Positions[Model.IndexBuffer[I+2]])});}return Result;};
 const auto A=Faces(Source,SourcePositions),B=Faces(Low,LowPositions);if(A.IsEmpty()||B.IsEmpty())return false;
 auto Covered=[Tolerance](const TArray<FFace>& From,const TArray<FFace>& To){const double Limit=FMath::Square(double(Tolerance));for(const auto& F:From)for(const FVector& P:{F.A,F.B,F.C,(F.A+F.B+F.C)/3.}){bool Near=false;double Closest=TNumericLimits<double>::Max();for(const auto& T:To){const double Distance=FVector::DistSquared(P,FMath::ClosestPointOnTriangleToPoint(P,T.A,T.B,T.C));Closest=FMath::Min(Closest,Distance);if(Distance<=Limit){Near=true;break;}}if(!Near){UE_LOG(LogTemp,Warning,TEXT("Crowd small geometric coverage failed point=%s distance_cm=%f source_faces=%d target_faces=%d"),*P.ToString(),FMath::Sqrt(Closest),From.Num(),To.Num());return false;}}return true;};return Covered(A,B)&&Covered(B,A);
}
bool AuditReducedWalk(UKKImportedCharacter* Source,UKKImportedAnimationLibrary* Walk,UKKImportedCharacter* Reduced,int32* FailedPart=nullptr){
 auto LogAuditFailure=[FailedPart](const FString& Message){if(FailedPart){UE_LOG(LogTemp,Warning,TEXT("%s"),*Message);}else{UE_LOG(LogTemp,Error,TEXT("%s"),*Message);}};
 if(Walk->Animations.IsEmpty()||!Walk->Animations[0].Sequence)return false;
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* LowWalk=NewObject<UKKImportedAnimationLibrary>();LowWalk->Character=Reduced;LowWalk->Animations=Walk->Animations;
 auto Spawn=[&](UKKImportedCharacter* Card,UKKImportedAnimationLibrary* Library){auto* Actor=World->SpawnActorDeferred<AKKImportedCharacterActor>(AKKImportedCharacterActor::StaticClass(),FTransform::Identity);Actor->Character=Card;Actor->AnimationLibrary=Library;Actor->PlaySkeletalAnimation=true;Actor->FinishSpawning(FTransform::Identity);return Actor;};
 auto* Before=Spawn(Source,Walk);auto* After=Spawn(Reduced,LowWalk);TArray<USkeletalMeshComponent*> A,B;Before->GetComponents(A);After->GetComponents(B);bool Good=A.Num()==B.Num()&&A.Num()>0;
 const int32 LODCount=Good?B[0]->GetSkeletalMeshAsset()->GetLODNum():0;
 for(int32 LOD=0;LOD<LODCount&&Good;++LOD){for(auto* Skin:B)Skin->SetForcedLOD(LOD+1);
  for(int32 Sample=0;Sample<=6&&Good;++Sample){const float Time=Walk->Animations[0].Sequence->GetPlayLength()*Sample/6.f;Good=Before->SetAnimationPosition(Time)&&After->SetAnimationPosition(Time);
   for(int32 Part=0;Part<A.Num()&&Good;++Part){const FBox3f AB=SkinBounds(A[Part],0),BB=SkinBounds(B[Part],LOD);
    Good=AB.IsValid&&BB.IsValid;for(int32 Axis=0;Axis<3&&Good;++Axis)Good=BB.Min[Axis]>=AB.Min[Axis]-5-AB.GetSize()[Axis]*.15f&&BB.Max[Axis]<=AB.Max[Axis]+5+AB.GetSize()[Axis]*.15f&&BB.GetSize()[Axis]+1>=AB.GetSize()[Axis]*.65f;
    if(!Good&&FailedPart)*FailedPart=Part;if(!Good)LogAuditFailure(FString::Printf(TEXT("Crowd low deformation audit failed part=%d lod=%d sample=%d before=%s after=%s"),Part,LOD,Sample,*AB.ToString(),*BB.ToString()));
    if(Good){TArray<FVector3f> SourcePositions,LowPositions;const auto SourceAreas=SkinSurfaceAreas(A[Part],0,&SourcePositions),LowAreas=SkinSurfaceAreas(B[Part],LOD,&LowPositions);for(const auto& Area:SourceAreas)if(Area.Value>10.){const double LowArea=LowAreas.FindRef(Area.Key);if(LowArea<Area.Value*.3||LowArea>Area.Value*3.){if(LowArea>0&&SmallSurfaceCoverage(A[Part]->GetSkeletalMeshAsset()->GetImportedModel()->LODModels[0],SourcePositions,B[Part]->GetSkeletalMeshAsset()->GetImportedModel()->LODModels[LOD],LowPositions,Area.Key,.25f)){UE_LOG(LogTemp,Display,TEXT("Crowd small surface geometric coverage passed part=%d material=%d lod=%d sample=%d tolerance_cm=0.25 area=%f/%f"),Part,Area.Key,LOD,Sample,LowArea,Area.Value);continue;}LogAuditFailure(FString::Printf(TEXT("Crowd low surface coverage failed part=%d material=%d lod=%d sample=%d area=%f/%f source=%s reduced=%s"),Part,Area.Key,LOD,Sample,LowArea,Area.Value,*A[Part]->GetSkeletalMeshAsset()->GetPathName(),*B[Part]->GetSkeletalMeshAsset()->GetPathName()));Good=false;if(FailedPart)*FailedPart=Part;break;}}}
   }
  }
 }
 Before->Destroy();After->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);UE_LOG(LogTemp,Display,TEXT("Crowd low deformation audit good=%d parts=%d lods=%d samples=7"),Good?1:0,A.Num(),LODCount);return Good;
}
bool CaptureQuality(UKKImportedCharacter* Character,UKKImportedAnimationLibrary* Walk,const FString& Output){
 if(IFileManager::Get().FileExists(*Output))return false;
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Actor=World->SpawnActorDeferred<AKKImportedCharacterActor>(AKKImportedCharacterActor::StaticClass(),FTransform::Identity);Actor->Character=Character;Actor->AnimationLibrary=Walk;Actor->PlaySkeletalAnimation=true;Actor->ShareBakedPose=true;Actor->FinishSpawning(FTransform::Identity);
 bool Good=Actor->SetAnimationPosition(Walk->Animations[0].Sequence->GetPlayLength()*.25f);FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();World->UpdateWorldComponents(false,false);for(int32 Frame=0;Frame<3;++Frame){World->Tick(LEVELTICK_All,1.f/60);World->SendAllEndOfFrameUpdates();}FlushRenderingCommands();
 const FBox Box=Actor->GetComponentsBoundingBox(true);const FVector Center=Box.GetCenter();
 UE_LOG(LogTemp,Display,TEXT("Crowd quality capture bounds=%s center=%s"),*Box.GetSize().ToString(),*Center.ToString());
 // Inspect saved bake pixels independently from the screenshot/render path.
 for(const auto& Part:Character->Outfits[0].Parts)if(Part.SkinnedModel){const FString ArrayPath=FPaths::GetPath(Character->GetOutermost()->GetName())/TEXT("Atlas")/Part.SkinnedModel->GetName()/TEXT("Layers.Layers");if(auto* Array=LoadObject<UTexture2DArray>(nullptr,*ArrayPath)){TArray64<uint8> Data;if(Array->Source.GetMipData(Data,0)&&Array->Source.GetSizeX()==256&&Array->Source.GetSizeY()==256){for(int32 Layer=0;Layer<FMath::Min(8,Array->Source.GetNumSlices());++Layer){TArray64<uint8> PNG;const FColor* Pixels=reinterpret_cast<const FColor*>(Data.GetData()+int64(Layer)*256*256*4);FImageUtils::PNGCompressImageArray(256,256,MakeArrayView(Pixels,256*256),PNG);FFileHelper::SaveArrayToFile(PNG,*(Output+Part.SkinnedModel->GetName()+FString::Printf(TEXT("_Layer%d.png"),Layer)));}}}}
 auto* Capture=NewObject<USceneCaptureComponent2D>(Actor);Actor->AddInstanceComponent(Capture);Capture->RegisterComponent();Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->ProjectionType=ECameraProjectionMode::Orthographic;Capture->OrthoWidth=FMath::Max(220.,Box.GetSize().Z*1.1);Capture->CaptureSource=SCS_FinalColorLDR;Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;Capture->ShowOnlyActorComponents(Actor);Capture->ShowFlags.SetTemporalAA(false);Capture->ShowFlags.SetMotionBlur(false);
 const FVector Location=Center+FVector(300,-500,0);Capture->SetWorldLocationAndRotation(Location,(Center-Location).Rotation());
 Capture->PostProcessBlendWeight=1;Capture->PostProcessSettings.bOverride_AutoExposureMethod=true;Capture->PostProcessSettings.AutoExposureMethod=AEM_Manual;Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;
 auto* Target=NewObject<UTextureRenderTarget2D>(Capture);Target->ClearColor=FLinearColor(.18f,.18f,.18f,1);Target->InitCustomFormat(768,1024,PF_B8G8R8A8,false);Target->UpdateResourceImmediate(true);Capture->TextureTarget=Target;
 Capture->CaptureScene();FlushRenderingCommands();TArray<FColor> Pixels;Good=Good&&Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)&&Pixels.Num()==768*1024;
 if(Good){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(768,1024,Pixels,PNG);IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);Good=FFileHelper::SaveArrayToFile(PNG,*Output);}
 Actor->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return Good;
}
struct FQualityResources : FGCObject {
 FString Root; int32 Serial=0; TArray<TObjectPtr<UObject>> Assets; TMap<UObject*,UObject*> Copies;
 bool BakeMaterials=false;
 bool KeepSourceMaterials=false;
 bool Lightweight=false;
 bool SingleLow=false;
 uint32 TriangleBudget=28000;
 uint32 ReusableTriangleBudget=28000;
 uint32 TotalTriangleBudget=28000;
 uint32 SurfaceReserve=1700;
 bool ProtectAllSurfaces=false;
 TMap<USkeletalMesh*,TArray<FMatrix44f>> ReferenceOverrides;
 uint32 WholeTriangleLimit=30000;
 UAnimSequence* ReferenceAnimation=nullptr;
 explicit FQualityResources(const FString& InRoot):Root(InRoot){}
 void AddReferencedObjects(FReferenceCollector& Collector)override{Collector.AddReferencedObjects(Assets);}
 FString GetReferencerName()const override{return TEXT("CrowdQualityResources");}
 template<class T>T* Copy(T* Source,const TCHAR* Prefix){
  const FString Name=FString::Printf(TEXT("%s_%05d"),Prefix,Serial++),Path=Root/TEXT("Resources")/Name;
  if(FPackageName::DoesPackageExist(Path))return nullptr;
  T* Result=DuplicateObject<T>(Source,CreatePackage(*Path),*Name);Result->ClearFlags(RF_Transient);Result->SetFlags(RF_Public|RF_Standalone);Assets.Add(Result);Copies.Add(Source,Result);return Result;
 }
 UTexture* Texture(UTexture* Source){
  if(!Source)return nullptr;if(Assets.Contains(Source))return Source;if(auto** Found=Copies.Find(Source))return Cast<UTexture>(*Found);
  // Cube maps and depth captures retain their authored interpretation.
  auto* Texture2D=Cast<UTexture2D>(Source);if(!Texture2D)return Source;
  auto* Result=Copy(Texture2D,TEXT("T"));if(!Result)return nullptr;
  if(Lightweight&&FMath::Max(Result->Source.GetSizeX(),Result->Source.GetSizeY())>256){
   // Shrink source pixels too: MaxTextureSize alone leaves the large editor payload.
   TArray64<uint8> Pixels;
   if(Result->Source.GetFormat()!=TSF_BGRA8||!Result->Source.GetMipData(Pixels,0)){
    UE_LOG(LogTemp,Error,TEXT("Game low texture unsupported source format: %s"),*Source->GetPathName());return nullptr;
   }
   const int32 X=Result->Source.GetSizeX(),Y=Result->Source.GetSizeY();
   const float Scale=256.f/FMath::Max(X,Y);const int32 NX=FMath::Max(1,FMath::RoundToInt(X*Scale)),NY=FMath::Max(1,FMath::RoundToInt(Y*Scale));
   TArray<FColor> Small;Small.SetNumUninitialized(NX*NY);
   FImageUtils::ImageResize(X,Y,MakeArrayView(reinterpret_cast<const FColor*>(Pixels.GetData()),X*Y),NX,NY,MakeArrayView(Small),Result->SRGB,false);
   Result->PreEditChange(nullptr);Result->Source.Init(NX,NY,1,1,TSF_BGRA8,reinterpret_cast<const uint8*>(Small.GetData()));
  }
  Result->MaxTextureSize=256;Result->NeverStream=false;
  // The derivative permits new mips; original alpha/detail assets remain intact.
  Result->MipGenSettings=TMGS_SimpleAverage;Result->PostEditChange();return Result;
 }
 UMaterialInterface* Material(UMaterialInterface* Source){
  if(!Source)return nullptr;if(auto** Found=Copies.Find(Source))return Cast<UMaterialInterface>(*Found);
  if(auto* Instance=Cast<UMaterialInstance>(Source)){
   const FString Name=FString::Printf(TEXT("MI_%05d"),Serial++),Path=Root/TEXT("Resources")/Name;if(FPackageName::DoesPackageExist(Path))return nullptr;
   auto* Result=NewObject<UMaterialInstanceConstant>(CreatePackage(*Path),*Name,RF_Public|RF_Standalone);Assets.Add(Result);Copies.Add(Source,Result);
   auto* Parent=Material(Instance->Parent);if(!Parent)return nullptr;
   Result->SetParentEditorOnly(Parent);Result->CopyMaterialUniformParametersEditorOnly(Source,true);
   TArray<FMaterialParameterInfo> Infos;TArray<FGuid> Ids;Source->GetAllTextureParameterInfo(Infos,Ids);
   for(const auto& Info:Infos){UTexture* Original=nullptr;if(Source->GetTextureParameterValue(Info,Original)&&Original){auto* Reduced=Texture(Original);if(!Reduced)return nullptr;Result->SetTextureParameterValueEditorOnly(Info,Reduced);}}
   const auto Overrides=Result->TextureParameterValues;for(const auto& Override:Overrides)if(Override.ParameterValue){auto* Reduced=Texture(Override.ParameterValue);if(!Reduced)return nullptr;Result->SetTextureParameterValueEditorOnly(Override.ParameterInfo,Reduced);}
   Result->PostEditChange();return Result;
  }
  auto* Original=Cast<UMaterial>(Source);if(!Original)return nullptr;auto* Result=Copy(Original,TEXT("M"));if(!Result)return nullptr;
  TArray<UObject*> Owned;GetObjectsWithOuter(Result,Owned,EGetObjectsFlags::IncludeNestedObjects);Owned.Add(Result);
  TArray<UObject*> References;FReferenceFinder Finder(References,nullptr,false,true,false,false);for(auto* Object:Owned)Finder.FindReferences(Object);
  TMap<UObject*,UObject*> Replacements;for(auto* Reference:References)if(auto* OriginalTexture=Cast<UTexture>(Reference)){auto* Reduced=Texture(OriginalTexture);if(!Reduced)return nullptr;if(Reduced!=OriginalTexture)Replacements.Add(OriginalTexture,Reduced);}
  FArchiveReplaceObjectRef<UObject> Replace(Result,Replacements,EArchiveReplaceObjectFlags::IgnoreOuterRef|EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
  Result->SetUsageByFlag(MATUSAGE_SkeletalMesh,true);Result->PostEditChange();return Result;
 }
 USkeletalMesh* Mesh(USkeletalMesh* Source){
  if(auto** Found=Copies.Find(Source))return Cast<USkeletalMesh>(*Found);auto* Result=Copy(Source,TEXT("SK"));if(!Result)return nullptr;
  if(BakeMaterials){if(!OKBuildCrowdMaterialLOD(Result,Root,Assets))return nullptr;}
  else if(!KeepSourceMaterials)for(auto& Slot:Result->GetMaterials()){auto* Reduced=Material(Slot.MaterialInterface);if(!Reduced)return nullptr;Slot.MaterialInterface=Reduced;}
  TArray<FMatrix44f> Reference;FMeshDescription CanonicalDescription,EncodedDescription;USkeletalMesh* CachedRoot=nullptr;bool ReduceCachedRoot=false;FString RootCachePackage;
  uint32 KeepSmallParts=0;FParse::Value(FCommandLine::Get(),TEXT("CrowdKeepSmallParts="),KeepSmallParts);const bool KeepWholePart=SingleLow&&KeepSmallParts>0&&Source->GetMeshDescription(0)&&uint32(Source->GetMeshDescription(0)->Triangles().Num())<=KeepSmallParts;
  const auto LowMaterials=Result->GetMaterials();
  if(SingleLow){static bool Mounted=false;if(!Mounted){FPackageName::RegisterMountPoint(TEXT("/CrowdReductionCache/"),FPaths::ProjectSavedDir()/TEXT("Crowd/ReductionCache/"));Mounted=true;}const FString File=FPackageName::LongPackageNameToFilename(Source->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());auto PackageForBudget=[&](uint32 Budget){const FString Key=Source->GetPathName()+TEXT("|")+ReferenceAnimation->GetPathName()+TEXT("|")+IFileManager::Get().GetTimeStamp(*File).ToIso8601()+FString::Printf(TEXT("|%s|%u"),ReferenceOverrides.Contains(Source)?TEXT("CanonicalEvaluatedR1"):TEXT("CanonicalRootR1"),Budget);return TEXT("/CrowdReductionCache/")+FMD5::HashAnsiString(*Key)+TEXT("/Root");};RootCachePackage=PackageForBudget(TriangleBudget);FString LoadPackage=RootCachePackage;if(!FPackageName::DoesPackageExist(LoadPackage)&&ReusableTriangleBudget>TriangleBudget){LoadPackage=PackageForBudget(ReusableTriangleBudget);ReduceCachedRoot=FPackageName::DoesPackageExist(LoadPackage);}if(FPackageName::DoesPackageExist(LoadPackage)){CachedRoot=LoadObject<USkeletalMesh>(nullptr,*(LoadPackage+TEXT(".Root")));if(!CachedRoot||CachedRoot->GetLODNum()!=1||CachedRoot->GetRefSkeleton().GetNum()!=Source->GetRefSkeleton().GetNum())return nullptr;}}
  if(Lightweight){Reference=ReferenceOverrides.Contains(Source)?ReferenceOverrides[Source]:ReferenceSkinMatrices(Source,ReferenceAnimation);if(Reference.Num()!=Source->GetRefSkeleton().GetNum())return nullptr;auto* Desc=Result->GetMeshDescription(0);if(!Desc)return nullptr;if(ReferenceOverrides.Contains(Source)){FMeshDescription Imported;Result->GetImportedModel()->LODModels[0].GetRawPointIndices().Reset();Result->GetImportedModel()->LODModels[0].GetMeshDescription(Result,0,Imported);*Desc=MoveTemp(Imported);}EncodedDescription=*Desc;FSkeletalMeshAttributes Attr(*Desc);auto Positions=Attr.GetVertexPositions();auto Normals=Attr.GetVertexInstanceNormals();auto Tangents=Attr.GetVertexInstanceTangents();const auto Weights=Attr.GetVertexSkinWeights();
   for(auto Vertex:Desc->Vertices().GetElementIDs()){const auto Blend=VertexBlend(Weights.Get(Vertex),Reference);if(FMath::Abs(Blend.Determinant())<1e-10f){UE_LOG(LogTemp,Error,TEXT("Game low singular reference blend: %s"),*Source->GetPathName());return nullptr;}Positions[Vertex]=FVector3f(Blend.TransformPosition(Positions[Vertex]));for(auto Instance:Desc->GetVertexVertexInstanceIDs(Vertex)){Normals[Instance]=FVector3f(Blend.TransformVector(Normals[Instance])).GetSafeNormal();Tangents[Instance]=FVector3f(Blend.TransformVector(Tangents[Instance])).GetSafeNormal();}}
   CanonicalDescription=*Desc;double Area=0;for(auto Triangle:Desc->Triangles().GetElementIDs())if(Desc->GetTrianglePolygonGroup(Triangle).GetValue()==0){const auto V=Desc->GetTriangleVertices(Triangle);Area+=FVector3f::CrossProduct(Positions[V[1]]-Positions[V[0]],Positions[V[2]]-Positions[V[0]]).Size()*.5;}UE_LOG(LogTemp,Display,TEXT("Game low reference description area0=%f"),Area);
   if(!CachedRoot){for(auto Vertex:Desc->Vertices().GetElementIDs()){TArray<UE::AnimationCore::FBoneWeight> RootWeights;RootWeights.Add(UE::AnimationCore::FBoneWeight(static_cast<FBoneIndexType>(0),static_cast<uint16>(65535)));Attr.GetVertexSkinWeights().SetRaw(Vertex,RootWeights);}Result->CommitMeshDescription(0);Result->Build();FAssetCompilingManager::Get().FinishAllCompilation();UE_LOG(LogTemp,Display,TEXT("Game low reference build area0=%f"),UnposedArea(Result->GetImportedModel()->LODModels[0],0));}
  }
  if(CachedRoot){Assets.Remove(Result);Result=CachedRoot;Assets.Add(Result);Result->GetMaterials()=LowMaterials;UE_LOG(LogTemp,Display,TEXT("Crowd reused canonical root reduction %s"),*RootCachePackage);}
  // A separate gameplay mesh has no full-detail render LOD. The high mesh is
  // referenced softly by the scene, never through this lightweight asset.
  const int32 LODCount=SingleLow?1:3;
  while(Result->GetLODNum()<LODCount)Result->AddLODInfo();
  if(Lightweight){auto* Base=Result->GetLODInfo(0);Base->ReductionSettings.BaseLOD=0;Base->ReductionSettings.NumOfTrianglesPercentage=.35f;Base->ReductionSettings.MaxBonesPerVertex=4;Base->ReductionSettings.bRemapMorphTargets=true;}
  for(int32 LOD=1;LOD<LODCount;++LOD){auto* Info=Result->GetLODInfo(LOD);Info->ReductionSettings.BaseLOD=0;Info->ReductionSettings.NumOfTrianglesPercentage=LOD==1?.35f:.10f;Info->ReductionSettings.MaxBonesPerVertex=4;Info->ReductionSettings.bRemapMorphTargets=false;Info->ScreenSize.Default=LOD==1?.12f:.045f;}
  if(Lightweight)for(int32 LOD=1;LOD<LODCount;++LOD){auto* Info=Result->GetLODInfo(LOD);Info->ReductionSettings.NumOfTrianglesPercentage=LOD==1?.50f:.25f;Info->ReductionSettings.bRemapMorphTargets=true;}
  if(Lightweight)for(int32 LOD=0;LOD<LODCount;++LOD){auto& Settings=Result->GetLODInfo(LOD)->ReductionSettings;Settings.bMergeCoincidentVertBones=false;Settings.bEnforceBoneBoundaries=false;Settings.bLockEdges=false;Settings.bLockColorBounaries=false;Settings.bRemapMorphTargets=false;Settings.MaxBonesPerVertex=1;if(SingleLow){Settings.TerminationCriterion=SMTC_AbsNumOfTriangles;Settings.MaxNumOfTriangles=TriangleBudget;}}
  if((!CachedRoot||ReduceCachedRoot)&&!FLODUtilities::RegenerateLOD(Result,nullptr,LODCount,true,Lightweight)){UE_LOG(LogTemp,Error,TEXT("Crowd reducer failed mesh=%s budget=%u"),*Source->GetPathName(),TriangleBudget);return nullptr;}
  if(!SingleLow)Result->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();
  if(Lightweight)UE_LOG(LogTemp,Display,TEXT("Game low reduced reference area0=%f"),UnposedArea(Result->GetImportedModel()->LODModels[0],0));
  if(SingleLow&&(!CachedRoot||ReduceCachedRoot)){FMeshDescription Snapshot;Result->GetImportedModel()->LODModels[0].GetRawPointIndices().Reset();Result->GetImportedModel()->LODModels[0].GetMeshDescription(Result,0,Snapshot);auto* Cache=NewObject<USkeletalMesh>(CreatePackage(*RootCachePackage),TEXT("Root"),RF_Public|RF_Standalone);TStrongObjectPtr<USkeletalMesh> Hold(Cache);Cache->SetSkeleton(Source->GetSkeleton());Cache->SetRefSkeleton(Source->GetRefSkeleton());Cache->CalculateInvRefMatrices();Cache->GetMaterials()=Source->GetMaterials();Cache->SetHasVertexColors(Source->GetHasVertexColors());Cache->AddLODInfo();*Cache->GetLODInfo(0)=*Result->GetLODInfo(0);auto* Info=Cache->GetLODInfo(0);Info->bHasBeenSimplified=false;Info->ReductionSettings.TerminationCriterion=SMTC_NumOfTriangles;Info->ReductionSettings.NumOfTrianglesPercentage=1;Info->ReductionSettings.NumOfVertPercentage=1;Info->ReductionSettings.MaxNumOfTrianglesPercentage=MAX_uint32;Info->ReductionSettings.MaxNumOfVertsPercentage=MAX_uint32;Cache->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());Cache->CreateMeshDescription(0,MoveTemp(Snapshot));Cache->CommitMeshDescription(0);Cache->Build();FAssetCompilingManager::Get().FinishAllCompilation();const FString File=FPackageName::LongPackageNameToFilename(RootCachePackage,FPackageName::GetAssetPackageExtension());IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(Cache->GetOutermost(),Cache,*File,Args))return nullptr;UE_LOG(LogTemp,Display,TEXT("Crowd saved canonical root reduction %s"),*RootCachePackage);}
  if(Lightweight){
   // Generic reduction can prune individual columns of our affine palettes.
   // Rebuild independent low meshes with complete equally weighted triples.
   const FString Name=FString::Printf(TEXT("SKL_%05d"),Serial++),Path=Root/TEXT("Resources")/Name;
   auto* Final=NewObject<USkeletalMesh>(CreatePackage(*Path),*Name,RF_Public|RF_Standalone);Assets.Add(Final);Final->SetSkeleton(Source->GetSkeleton());Final->SetRefSkeleton(Source->GetRefSkeleton());Final->CalculateInvRefMatrices();Final->GetMaterials()=Result->GetMaterials();Final->SetHasVertexColors(Source->GetHasVertexColors());
   for(int32 LOD=0;LOD<LODCount;++LOD){FMeshDescription Desc;Result->GetImportedModel()->LODModels[LOD].GetRawPointIndices().Reset();Result->GetImportedModel()->LODModels[LOD].GetMeshDescription(Result,LOD,Desc);if(KeepWholePart){Desc=EncodedDescription;Desc.VertexAttributes().RegisterAttribute<int32>(TEXT("GameLowAuthored"),1,0);auto Flags=Desc.VertexAttributes().GetAttributesRef<int32>(TEXT("GameLowAuthored"));for(auto V:Desc.Vertices().GetElementIDs())Flags[V]=1;UE_LOG(LogTemp,Display,TEXT("Crowd retained small authored part mesh=%s triangles=%d"),*Source->GetPathName(),Desc.Triangles().Num());}else if(SingleLow){if(!RecoverSmallSurfaces(Desc,CanonicalDescription,EncodedDescription,FMath::Max<uint32>(24,SurfaceReserve*TriangleBudget/TotalTriangleBudget),ProtectAllSurfaces))return nullptr;}else RestoreLostSurfaces(Desc,CanonicalDescription,EncodedDescription,Final->GetRefSkeleton());FSkeletalMeshAttributes Attr(Desc);Attr.Register(true);const auto& Ref=Final->GetRefSkeleton();
    if(Attr.GetNumBones()==0)for(int32 Bone=0;Bone<Ref.GetNum();++Bone){auto Id=Attr.CreateBone();Attr.GetBoneNames()[Id]=Ref.GetBoneName(Bone);Attr.GetBoneParentIndices()[Id]=Ref.GetParentIndex(Bone);Attr.GetBonePoses()[Id]=Ref.GetRefBonePose()[Bone];}
    if(SingleLow&&!KeepAuthoredSurfaces(Desc,EncodedDescription))return nullptr;if(!TransferAuthoredBindings(Desc,CanonicalDescription,EncodedDescription,Reference))return nullptr;
    if(LOD>0){const auto* Previous=Final->GetMeshDescription(LOD-1);if(!Previous)return nullptr;if(Desc.Triangles().Num()>Previous->Triangles().Num()){UE_LOG(LogTemp,Display,TEXT("Game low LOD %d retained previous validated geometry because surface recovery grew triangles %d > %d"),LOD,Desc.Triangles().Num(),Previous->Triangles().Num());Desc=*Previous;}}
    Final->AddLODInfo();*Final->GetLODInfo(LOD)=*Result->GetLODInfo(LOD);auto* Info=Final->GetLODInfo(LOD);Info->LODMaterialMap.Reset();Info->bHasBeenSimplified=false;Info->ReductionSettings.NumOfTrianglesPercentage=1;Info->ReductionSettings.NumOfVertPercentage=1;Info->ReductionSettings.BaseLOD=LOD;Info->ReductionSettings.TerminationCriterion=SMTC_NumOfTriangles;Info->ReductionSettings.MaxNumOfTrianglesPercentage=MAX_uint32;Info->ReductionSettings.MaxNumOfVertsPercentage=MAX_uint32;Info->ReductionSettings.MaxBonesPerVertex=12;Info->BuildSettings.BoneInfluenceLimit=12;Info->BuildSettings.bUseHighPrecisionSkinWeights=true;Info->BuildSettings.bUseFullPrecisionUVs=true;Info->BuildSettings.bRemoveDegenerates=false;Info->BuildSettings.bRecomputeNormals=false;Info->BuildSettings.bRecomputeTangents=false;Info->BuildSettings.ThresholdPosition=0;Info->BuildSettings.ThresholdTangentNormal=0;Info->BuildSettings.ThresholdUV=0;
    Final->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());Final->CreateMeshDescription(LOD,MoveTemp(Desc));Final->CommitMeshDescription(LOD);
   }
   Final->Build();Final->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();Assets.Remove(Result);Copies[Source]=Final;Result=Final;
  }
  const auto* Data=Result->GetResourceForRendering();if(!Data||Data->LODRenderData.Num()!=LODCount)return nullptr;
  if(Lightweight)for(const auto& LOD:Data->LODRenderData)for(const auto& Section:LOD.RenderSections)for(uint32 Vertex=Section.BaseVertexIndex;Vertex<Section.BaseVertexIndex+Section.NumVertices;++Vertex){const auto W=LOD.SkinWeightVertexBuffer.GetVertexSkinWeights(Vertex);TMap<FString,TArray<uint16>> Groups;uint32 Total=0;for(int32 Slot=0;Slot<MAX_TOTAL_INFLUENCES;++Slot){Total+=W.InfluenceWeights[Slot];if(!W.InfluenceWeights[Slot])continue;if(!Section.BoneMap.IsValidIndex(W.InfluenceBones[Slot]))return nullptr;const FString Name=Result->GetRefSkeleton().GetBoneName(Section.BoneMap[W.InfluenceBones[Slot]]).ToString();if(Name.StartsWith(TEXT("__ok_palette_")))Groups.FindOrAdd(Name.LeftChop(2)).Add(W.InfluenceWeights[Slot]);}if(Total!=65535){UE_LOG(LogTemp,Error,TEXT("Game low skin weight total invalid mesh=%s vertex=%u total=%u"),*Source->GetPathName(),Vertex,Total);return nullptr;}for(const auto& Group:Groups)if(Group.Value.Num()!=3||Group.Value[0]!=Group.Value[1]||Group.Value[0]!=Group.Value[2]){FString Detail;for(int32 Slot=0;Slot<MAX_TOTAL_INFLUENCES;++Slot)if(W.InfluenceWeights[Slot])Detail+=FString::Printf(TEXT(" %s:%u"),*Result->GetRefSkeleton().GetBoneName(Section.BoneMap[W.InfluenceBones[Slot]]).ToString(),W.InfluenceWeights[Slot]);UE_LOG(LogTemp,Error,TEXT("Game low affine columns unbalanced vertex=%u material=%d group=%s count=%d weights=%s"),Vertex,Section.MaterialIndex,*Group.Key,Group.Value.Num(),*Detail);return nullptr;}}
  int64 Counts[3]={};for(int32 LOD=0;LOD<LODCount;++LOD)for(const auto& Section:Data->LODRenderData[LOD].RenderSections)Counts[LOD]+=Section.NumTriangles;
  if(SingleLow){if(Counts[0]<=0||Counts[0]>(KeepWholePart?uint32(EncodedDescription.Triangles().Num()):TriangleBudget+FMath::Max<uint32>(24,SurfaceReserve*TriangleBudget/TotalTriangleBudget))){UE_LOG(LogTemp,Error,TEXT("Crowd single low budget failed triangles=%lld budget=%u"),Counts[0],TriangleBudget);return nullptr;}UE_LOG(LogTemp,Display,TEXT("Crowd single low triangles=%lld sections=%d budget=%u lods=1"),Counts[0],Data->LODRenderData[0].RenderSections.Num(),TriangleBudget);return Result;}
  if(Counts[0]<=0||Counts[1]<=0||Counts[2]<=0||Counts[1]>Counts[0]||Counts[2]>Counts[1]){UE_LOG(LogTemp,Error,TEXT("Game low nonmonotone render triangle counts %lld/%lld/%lld"),Counts[0],Counts[1],Counts[2]);return nullptr;}
  UE_LOG(LogTemp,Display,TEXT("Crowd quality mesh triangles=%lld/%lld/%lld sections=%d/%d/%d"),Counts[0],Counts[1],Counts[2],Data->LODRenderData[0].RenderSections.Num(),Data->LODRenderData[1].RenderSections.Num(),Data->LODRenderData[2].RenderSections.Num());return Result;
 }
 bool Save(UObject* Asset){const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());if(IFileManager::Get().FileExists(*File))return false;IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;return UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,Args);}
};
#include "OKCrowdBodyCoverage.inl"
#include "OKCrowdBindingRepair.inl"
#include "OKCrowdRoleBudget.inl"
}
int32 OKRepairCrowdBindings(int32 Start,int32 End,const FString& Params){return RepairCrowdBindings(Start,End,Params);}
int32 OKBuildCrowdBodyCoverage(int32 Start,int32 End,const FString& Params){return BuildBodyCoverageAssets(Start,End,Params);}
USkeletalMesh* OKBuildCrowdNearHair(USkeletalMesh* Source,UAnimSequence* Walk,const FString& Root,uint32 TriangleLimit){
 if(!Source||!Walk||!Source->GetMeshDescription(0)||TriangleLimit<12||TriangleLimit>10000)return nullptr;
 FQualityResources Resources(Root);Resources.Lightweight=true;Resources.SingleLow=true;Resources.KeepSourceMaterials=true;Resources.ReferenceAnimation=Walk;
 Resources.SurfaceReserve=FMath::Min<uint32>(1024,TriangleLimit/4);Resources.TriangleBudget=FMath::Max<uint32>(12,TriangleLimit-Resources.SurfaceReserve);Resources.TotalTriangleBudget=Resources.TriangleBudget;Resources.ReusableTriangleBudget=Resources.TriangleBudget;
 USkeletalMesh* Mesh=Source;
 if(uint32(Source->GetMeshDescription(0)->Triangles().Num())>TriangleLimit){Mesh=Resources.Mesh(Source);if(!Mesh)return nullptr;for(auto Asset:Resources.Assets)if(!Resources.Save(Asset))return nullptr;}
 TSet<UMaterialInterface*> Materials;for(const auto& M:Mesh->GetMaterials())Materials.Add(M.MaterialInterface.Get());int32 Changed=0;return OKRepairCrowdHairOutline(Mesh,Walk,Materials,Root/TEXT("OutlineSafe"),Changed);
}
bool OKCrowdIsHeadAccessory(const FKKImportedPart& Part,UAnimSequence* Walk){
 if(!Part.SkinnedModel||!Part.Role.Contains(TEXT("accessory"))||!Walk)return false;
 const auto* Desc=Part.SkinnedModel->GetMeshDescription(0);if(!Desc||Desc->Triangles().Num()<200)return false;
 const auto Matrices=ReferenceSkinMatrices(Part.SkinnedModel,Walk);if(Matrices.Num()!=Part.SkinnedModel->GetRefSkeleton().GetNum())return false;
 FSkeletalMeshConstAttributes A(*Desc);int32 Above=0;FBox Box(ForceInit);
 for(auto V:Desc->Vertices().GetElementIDs()){const auto P=Part.Transform.TransformPosition(FVector(CoverageBlend(A.GetVertexSkinWeights().Get(V),Matrices).TransformPosition(A.GetVertexPositions()[V])));Box+=P;if(P.Z>=125)++Above;}
 return Desc->Vertices().Num()>0&&Above>=Desc->Vertices().Num()*.3&&Box.Max.Z>=135&&FVector2D(Box.GetCenter().X,Box.GetCenter().Y).Size()<60;
}
int32 OKOptimizeCrowdQuality(int32 Start,int32 End,const FString& Params){
 FString SourceCharacter,SourceWalk,DestinationRoot;
 const bool Single=FParse::Value(*Params,TEXT("SourceCharacter="),SourceCharacter);
 if(Single){if(!FParse::Value(*Params,TEXT("SourceWalk="),SourceWalk)||!FParse::Value(*Params,TEXT("DestinationRoot="),DestinationRoot)||!SourceCharacter.StartsWith(TEXT("/Game/"))||!SourceWalk.StartsWith(TEXT("/Game/"))||!FPackageName::IsValidLongPackageName(DestinationRoot))return 1;Start=0;End=1;}
 FString Variant=TEXT("Performance256");FParse::Value(*Params,TEXT("QualityVariant="),Variant);if(Variant.IsEmpty()||Variant.Len()>64)return 1;for(TCHAR C:Variant)if(!FChar::IsAlnum(C)&&C!=TEXT('_'))return 1;
 if(Variant==TEXT("GameLowRoleBudgetR1")||Variant==TEXT("GameLowRoleBudgetR3"))return Single?1:BuildRoleBudget(Start,End,Params);
 if(Variant==TEXT("GameLowRoleBudgetRigR2")||Variant==TEXT("GameLowRoleBudgetRigR4"))return Single?1:BuildCompactRoleBudget(Start,End,Params);
 if(FParse::Param(*Params,TEXT("RigidClothesOnly"))&&Variant==TEXT("GameLow256R1"))return 1; // Full-mesh diagnostics must never populate a gameplay low cache.
 if(FParse::Param(*Params,TEXT("CaptureQuality"))){FString Tag;FParse::Value(*Params,TEXT("CaptureTag="),Tag);if(Tag.IsEmpty())return 1;for(TCHAR C:Tag)if(!FChar::IsAlnum(C)&&C!=TEXT('_'))return 1;for(int32 Index=Start;Index<End;++Index){const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index)/Variant;TStrongObjectPtr<UKKImportedCharacter> Card(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("Character.Character"))));TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("Walk/Animations.Animations"))));if(!Card||!Walk||Walk->Animations.IsEmpty()||!CaptureQuality(Card.Get(),Walk.Get(),FPaths::ProjectSavedDir()/TEXT("Crowd")/(Tag+FString::Printf(TEXT("_C%03d.png"),Index))))return 1;CollectGarbage(RF_NoFlags);}return 0;}
 if(FParse::Param(*Params,TEXT("RepairSkeletalMaterials"))){FString Tag;if(!FParse::Value(*Params,TEXT("RepairTag="),Tag)||Tag.IsEmpty())return 1;for(TCHAR C:Tag)if(!FChar::IsAlnum(C)&&C!=TEXT('_'))return 1;
  for(int32 Index=Start;Index<End;++Index){const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d/GameLow256R1/Character.Character"),Index);TStrongObjectPtr<UKKImportedCharacter> Character(LoadObject<UKKImportedCharacter>(nullptr,*Base));if(!Character||Character->Schema!=1004)return 1;TSet<UMaterial*> Materials;int32 Fixed=0;
   for(const auto& Part:Character->Outfits[0].Parts)if(Part.SkinnedModel)for(const auto& Slot:Part.SkinnedModel->GetMaterials())if(Slot.MaterialInterface)Materials.Add(Slot.MaterialInterface->GetMaterial());
   for(auto* Material:Materials)if(Material&&!Material->GetUsageByFlag(MATUSAGE_SkeletalMesh)){const FString Package=Material->GetOutermost()->GetName();if(!Package.StartsWith(FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d/GameLow256R1/"),Index)))return 1;const FString File=FPackageName::LongPackageNameToFilename(Package,FPackageName::GetAssetPackageExtension()),Backup=FPaths::ProjectSavedDir()/TEXT("Crowd/MaterialRepairBackups")/Tag/Package.Mid(6)+FPackageName::GetAssetPackageExtension();if(IFileManager::Get().FileExists(*Backup))return 1;IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup),true);if(IFileManager::Get().Copy(*Backup,*File)!=COPY_OK)return 1;Material->SetUsageByFlag(MATUSAGE_SkeletalMesh,true);Material->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(Material->GetOutermost(),Material,*File,Args))return 1;++Fixed;}
   UE_LOG(LogTemp,Display,TEXT("Crowd low skeletal materials repaired index=%d materials=%d fixed=%d"),Index,Materials.Num(),Fixed);Character.Reset();CollectGarbage(RF_NoFlags);
  }return 0;
 }
 for(int32 Index=Start;Index<End;++Index){
  const FString Base=Single?DestinationRoot:FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index),Destination=Base/Variant;
  const FString SourceVariant=TEXT("PackedRig");
  {TStrongObjectPtr<UKKImportedCharacter> Source(LoadObject<UKKImportedCharacter>(nullptr,*(Single?SourceCharacter:Base/SourceVariant/TEXT("Character.Character"))));TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Single?SourceWalk:Base/SourceVariant/TEXT("Walk/Animations.Animations"))));
   if(!Source||!Walk||Walk->Character!=Source.Get()||Source->Outfits.IsEmpty())return 1;
   if(FPackageName::DoesPackageExist(Destination/TEXT("Character"))){auto* Existing=LoadObject<UKKImportedCharacter>(nullptr,*(Destination/TEXT("Character.Character")));if(!Existing||Existing->SourceHash!=Source->SourceHash||((Variant==TEXT("GameLow256R1")&&Existing->Schema!=1004)||(Variant==TEXT("GameLow30kR1")&&Existing->Schema!=1005)||(Variant==TEXT("GameLow10kR1")&&Existing->Schema!=1006))||!FPackageName::DoesPackageExist(Destination/TEXT("Walk/Animations")))return 1;}
   else {FQualityResources Resources(Destination);Resources.SingleLow=Variant==TEXT("GameLow30kR1")||Variant==TEXT("GameLow10kR1");if(Variant==TEXT("GameLow10kR1")){Resources.TotalTriangleBudget=9000;Resources.SurfaceReserve=700;Resources.WholeTriangleLimit=10000;}if(Resources.SingleLow){FParse::Value(*Params,TEXT("CrowdLowBaseBudget="),Resources.TotalTriangleBudget);FParse::Value(*Params,TEXT("CrowdLowSurfaceReserve="),Resources.SurfaceReserve);if(Resources.TotalTriangleBudget<1000||Resources.SurfaceReserve>Resources.WholeTriangleLimit||uint64(Resources.TotalTriangleBudget)+Resources.SurfaceReserve>=Resources.WholeTriangleLimit)return 1;}Resources.Lightweight=Resources.SingleLow||Variant==TEXT("GameLow256R1")||FParse::Param(*Params,TEXT("RigidClothesOnly"));Resources.ReferenceAnimation=Walk->Animations.IsEmpty()?nullptr:Walk->Animations[0].Sequence.Get();Resources.BakeMaterials=FParse::Param(*Params,TEXT("BakeMaterials"));if(Resources.Lightweight&&Resources.BakeMaterials)return 1;auto* Character=Resources.Copy(Source.Get(),TEXT("Unused"));if(!Character)return 1;
    // Publish at the standard variant path, after every dependency succeeds.
    Character->Rename(TEXT("Character"),CreatePackage(*(Destination/TEXT("Character"))));
    if(Resources.Lightweight){Character->Schema=Variant==TEXT("GameLow10kR1")?1006:(Resources.SingleLow?1005:1004);Character->Outfits.SetNum(1);Character->Outfits[0].AnimationSnapshot.Reset();}
    for(auto& Part:Character->Outfits[0].Parts)if(Part.SkinnedModel&&!FParse::Param(*Params,TEXT("RigidClothesOnly"))){if(Resources.SingleLow){int64 SourceTotal=0,PartTriangles=0;for(const auto& P:Source->Outfits[0].Parts)if(P.SkinnedModel)for(const auto& S:P.SkinnedModel->GetResourceForRendering()->LODRenderData[0].RenderSections)SourceTotal+=S.NumTriangles;for(const auto& S:Part.SkinnedModel->GetResourceForRendering()->LODRenderData[0].RenderSections)PartTriangles+=S.NumTriangles;Resources.TriangleBudget=FMath::Max<uint32>(12,uint32(double(Resources.TotalTriangleBudget)*PartTriangles/FMath::Max<int64>(1,SourceTotal)));Resources.ReusableTriangleBudget=FMath::Max<uint32>(12,uint32(28000.*PartTriangles/FMath::Max<int64>(1,SourceTotal)));}auto* Reduced=Resources.Mesh(Part.SkinnedModel);if(!Reduced)return 1;Part.SkinnedModel=Reduced;if(Resources.Lightweight)Part.Model=nullptr;}
    if(Resources.SingleLow){int64 Total=0;for(const auto& Part:Character->Outfits[0].Parts)if(Part.SkinnedModel)for(const auto& S:Part.SkinnedModel->GetResourceForRendering()->LODRenderData[0].RenderSections)Total+=S.NumTriangles;if(Total>=Resources.WholeTriangleLimit){UE_LOG(LogTemp,Error,TEXT("Crowd whole-character budget failed total=%lld"),Total);return 1;}UE_LOG(LogTemp,Display,TEXT("Crowd whole-character budget passed total=%lld limit=%u"),Total,Resources.WholeTriangleLimit-1);}if(Resources.Lightweight&&!FParse::Param(*Params,TEXT("RigidClothesOnly"))&&!AuditReducedWalk(Source.Get(),Walk.Get(),Character))return 1;
    auto* Library=NewObject<UKKImportedAnimationLibrary>(CreatePackage(*(Destination/TEXT("Walk/Animations"))),TEXT("Animations"),RF_Public|RF_Standalone);Resources.Assets.Add(Library);Library->Character=Character;Library->Animations=Walk->Animations; if(Resources.Lightweight){FString NativeRoot=Single?FPaths::GetPath(FPackageName::ObjectPathToPackageName(SourceCharacter)):Base;if(Single&&Source->Outfits[0].Parts.ContainsByPredicate([](const auto& P){return P.Role==TEXT("CrowdPacked");}))NativeRoot=FPaths::GetPath(NativeRoot);auto* NativeCard=LoadObject<UKKImportedCharacter>(nullptr,*(NativeRoot/TEXT("Character.Character")));if(!NativeCard||NativeCard->SourceHash!=Source->SourceHash)return 1;for(int32 Clip=0;Clip<Library->Animations.Num();++Clip){auto* Seq=BakeRigidClothes(NativeCard,Library->Animations[Clip],Destination/TEXT("Walk")/FString::Printf(TEXT("RigidClothes_%04d"),Clip));if(!Seq)return 1;Resources.Assets.Add(Seq);Library->Animations[Clip].Sequence=Seq;}}
    FAssetCompilingManager::Get().FinishAllCompilation();for(const auto& Asset:Resources.Assets)if(Asset!=Character&&!Resources.Save(Asset.Get()))return 1;if(!Resources.Save(Character))return 1;
   }
  }CollectGarbage(RF_NoFlags);UE_LOG(LogTemp,Display,TEXT("Crowd quality complete index=%d variant=%s texture_max=256"),Index,*Variant);
 }return 0;
}
#endif
