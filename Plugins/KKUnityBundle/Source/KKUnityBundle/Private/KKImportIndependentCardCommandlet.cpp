#include "KKImportIndependentCardCommandlet.h"
#include "KKImportDestination.h"
#include "KKCardSource.h"
#include "KKImportedCharacter.h"
#include "KKPreviewActor.h"
#include "KKSkeletalBuilder.h"
#include "KKExactSkinning.h"
#include "ReferenceSkeleton.h"
#include "ok_extract.h"
#include "Misc/Parse.h"
#if WITH_EDITOR
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Animation/Skeleton.h"
#include "Animation/MorphTarget.h"
#include "Components/StaticMeshComponent.h"
#include "StaticMeshResources.h"
#include "Misc/ScopedSlowTask.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "MaterialShared.h"
#include "BoneWeights.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/GCObject.h"
#include "UObject/GarbageCollection.h"
#include "HAL/PlatformMemory.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "StaticMeshAttributes.h"
#include "SkeletalMeshAttributes.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "RenderingThread.h"
#include "Math/Float16Color.h"
#include "Misc/AutomationTest.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Json.h"
namespace {
uint32 RenderableSkinTriangles(const FMeshDescription& Desc){
 FSkeletalMeshConstAttributes Attributes(Desc);const auto Positions=Attributes.GetVertexPositions();const auto Weights=Attributes.GetVertexSkinWeights();
 const auto Morphs=Attributes.GetMorphTargetNames();
 auto Coincident=[&](FVertexID A,FVertexID B,FVertexInstanceID IA,FVertexInstanceID IB){
  if(Positions[A]!=Positions[B])return false;
  const auto X=Weights.Get(A),Y=Weights.Get(B);if(X.Num()!=Y.Num())return false;
  for(int32 I=0;I<X.Num();++I)if(X.Get(I)!=Y.Get(I))return false;
  for(const auto& Name:Morphs){const auto Deltas=Attributes.GetVertexMorphPositionDelta(Name);if(Deltas[A]!=Deltas[B])return false;}
  if(Attributes.GetVertexInstanceNormals()[IA]!=Attributes.GetVertexInstanceNormals()[IB]||Attributes.GetVertexInstanceTangents()[IA]!=Attributes.GetVertexInstanceTangents()[IB]||
     Attributes.GetVertexInstanceBinormalSigns()[IA]!=Attributes.GetVertexInstanceBinormalSigns()[IB]||Attributes.GetVertexInstanceColors()[IA]!=Attributes.GetVertexInstanceColors()[IB])return false;
  const auto UV=Attributes.GetVertexInstanceUVs();for(int32 U=0;U<UV.GetNumChannels();++U)if(UV.Get(IA,U)!=UV.Get(IB,U))return false;
  return true;
 };
 uint32 Count=0;for(auto Triangle:Desc.Triangles().GetElementIDs()){
  const auto Corners=Desc.GetTriangleVertexInstances(Triangle);
  const auto A=Desc.GetVertexInstanceVertex(Corners[0]),B=Desc.GetVertexInstanceVertex(Corners[1]),C=Desc.GetVertexInstanceVertex(Corners[2]);
  // Identical positions, weights and every morph stay coincident in all poses.
  // UE removes these index-only degenerates even with removal disabled.
  if(!Coincident(A,B,Corners[0],Corners[1])&&!Coincident(A,C,Corners[0],Corners[2])&&!Coincident(B,C,Corners[1],Corners[2]))++Count;
 }return Count;
}
struct FImportedUVs {
 FVector2f Values[4];
 bool operator==(const FImportedUVs& Other)const{for(int32 I=0;I<4;++I)if(Values[I]!=Other.Values[I])return false;return true;}
 friend uint32 GetTypeHash(const FImportedUVs& Value){uint32 Hash=0;for(const auto& UV:Value.Values){Hash=HashCombineFast(Hash,::GetTypeHash(UV.X==0.f?0.f:UV.X));Hash=HashCombineFast(Hash,::GetTypeHash(UV.Y==0.f?0.f:UV.Y));}return Hash;}
};
bool CheckUVStreams(UStaticMesh* Mesh,FString& Error){
 const auto* Desc=Mesh->GetMeshDescription(0);const auto* Data=Mesh->GetRenderData();if(!Desc||!Data||Data->LODResources.IsEmpty())return false;
 FStaticMeshConstAttributes A(*Desc);const auto UVs=A.GetVertexInstanceUVs();const auto& Render=Data->LODResources[0].VertexBuffers.StaticMeshVertexBuffer;
 if(UVs.GetNumChannels()!=4||Render.GetNumTexCoords()!=4){Error=TEXT("UV channel count changed: ")+Mesh->GetPathName();return false;}
 TSet<FImportedUVs> Authored;for(auto Id:Desc->VertexInstances().GetElementIDs()){FImportedUVs Key;for(int32 I=0;I<4;++I)Key.Values[I]=UVs.Get(Id,I);Authored.Add(Key);}
 for(uint32 V=0;V<Render.GetNumVertices();++V){FImportedUVs Key;for(int32 I=0;I<4;++I)Key.Values[I]=Render.GetVertexUV(V,I);if(!Authored.Contains(Key)){Error=TEXT("Rendered UV streams differ from source: ")+Mesh->GetPathName();return false;}}
 return true;
}
struct FImportedSkinCorner {
 FVector3f Position;FImportedUVs UVs{};FColor Color;
 bool operator==(const FImportedSkinCorner& Other)const{return Position==Other.Position&&UVs==Other.UVs&&Color==Other.Color;}
 friend uint32 GetTypeHash(const FImportedSkinCorner& V){uint32 H=GetTypeHash(V.UVs);H=HashCombineFast(H,::GetTypeHash(V.Color.ToPackedARGB()));for(int32 I=0;I<3;++I)H=HashCombineFast(H,::GetTypeHash(V.Position[I]==0.f?0.f:V.Position[I]));return H;}
};
bool CheckSkinColors(USkeletalMesh* Mesh,FString& Error){
 if(!Mesh->GetHasVertexColors())return true;
 const auto* Desc=Mesh->GetMeshDescription(0);const auto* Data=Mesh->GetResourceForRendering();
 if(!Desc||!Data||Data->LODRenderData.IsEmpty()){Error=TEXT("Missing colored skin geometry: ")+Mesh->GetPathName();return false;}
 const auto& Render=Data->LODRenderData[0];FSkeletalMeshConstAttributes A(*Desc);const auto UVs=A.GetVertexInstanceUVs();const auto Colors=A.GetVertexInstanceColors();
 if(UVs.GetNumChannels()>4||UVs.GetNumChannels()!=int32(Render.GetNumTexCoords())||Render.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices()!=Render.GetNumVertices()){Error=TEXT("Missing skin color buffer or UV channels: ")+Mesh->GetPathName();return false;}
 // Match complete authored corners; skin builders may reorder/weld render vertices.
 TSet<FImportedSkinCorner> Expected;for(auto Id:Desc->VertexInstances().GetElementIDs()){FImportedSkinCorner Key;Key.Position=A.GetVertexPositions()[Desc->GetVertexInstanceVertex(Id)];const auto C=Colors[Id];Key.Color=FLinearColor(C.X,C.Y,C.Z,C.W).ToFColor(false);for(int32 U=0;U<UVs.GetNumChannels();++U)Key.UVs.Values[U]=UVs.Get(Id,U);Expected.Add(Key);}
 for(uint32 I=0;I<Render.GetNumVertices();++I){FImportedSkinCorner Key;Key.Position=Render.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(I);Key.Color=Render.StaticVertexBuffers.ColorVertexBuffer.VertexColor(I);for(uint32 U=0;U<Render.GetNumTexCoords();++U)Key.UVs.Values[U]=Render.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I,U);if(!Expected.Contains(Key)){FString Match=TEXT("none");float Nearest=MAX_flt;for(const auto& Candidate:Expected){const float Distance=(Candidate.Position-Key.Position).SizeSquared();if(Distance>=Nearest)continue;Nearest=Distance;Match=FString::Printf(TEXT("color=%s uv0=%s uv1=%s uv2=%s"),*Candidate.Color.ToString(),*Candidate.UVs.Values[0].ToString(),*Candidate.UVs.Values[1].ToString(),*Candidate.UVs.Values[2].ToString());Match+=FString::Printf(TEXT(" deltaSq=%.12g"),Nearest);}Error=FString::Printf(TEXT("Rebuilt skin vertex color changed: %s render=%u color=%s pos=%s uv0=%s uv1=%s uv2=%s authored=%s"),*Mesh->GetPathName(),I,*Key.Color.ToString(),*Key.Position.ToString(),*Key.UVs.Values[0].ToString(),*Key.UVs.Values[1].ToString(),*Key.UVs.Values[2].ToString(),*Match);return false;}}
 return true;
}
bool CheckSkinMorphs(USkeletalMesh* Mesh,FString& Error,int64& Targets,int64& Samples,int64& Tiny){
 const auto* Desc=Mesh->GetMeshDescription(0);FSkeletalMeshConstAttributes A(*Desc);const auto Names=A.GetMorphTargetNames();if(Names.IsEmpty())return true;
 const auto& Render=Mesh->GetResourceForRendering()->LODRenderData[0];const auto& Map=Mesh->GetImportedModel()->LODModels[0].GetRawPointIndices();
 if(Map.Num()!=int32(Render.GetNumVertices())){Error=TEXT("Missing source morph vertex map: ")+Mesh->GetPathName();return false;}
 for(uint32 I=0;I<Render.GetNumVertices();++I)if(!Desc->IsVertexValid(FVertexID(Map[I]))||Render.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(I)!=A.GetVertexPositions()[FVertexID(Map[I])]){Error=TEXT("Source morph vertex mapping changed: ")+Mesh->GetPathName();return false;}
 for(const FName Name:Names){auto* Target=Mesh->FindMorphTarget(Name);if(!Target){Error=TEXT("Source morph target lost: ")+Name.ToString();return false;}++Targets;TMap<uint32,FVector3f> Actual;
  if(Target->GetMorphLODModels().Num()>0)for(const auto& D:Target->GetMorphLODModels()[0].Vertices){if(D.SourceIdx>=Render.GetNumVertices()||Actual.Contains(D.SourceIdx)){Error=TEXT("Invalid rebuilt morph vertex: ")+Name.ToString();return false;}Actual.Add(D.SourceIdx,D.PositionDelta);}
  const auto Deltas=A.GetVertexMorphPositionDelta(Name);for(uint32 I=0;I<Render.GetNumVertices();++I){const FVertexID Id(Map[I]);const FVector3f Delta=Deltas[Id];const FVector3f Base=A.GetVertexPositions()[Id];
   // Match UE's MeshDescription -> absolute morph point -> relative delta arithmetic.
   const FVector3f Expected=Delta.IsNearlyZero()?FVector3f::ZeroVector:(Base+Delta)-Base;if(Delta!=FVector3f::ZeroVector&&Delta.IsNearlyZero())++Tiny;
   const auto* Value=Actual.Find(I);const FVector3f Found=Value?*Value:FVector3f::ZeroVector;if(Found!=Expected){Error=FString::Printf(TEXT("Source morph delta lost: %s target=%s vertex=%u expected=%s actual=%s"),*Mesh->GetPathName(),*Name.ToString(),I,*Expected.ToString(),*Found.ToString());return false;}if(Expected!=FVector3f::ZeroVector)++Samples;
  }
 }
 return true;
}
bool CheckReferences(UObject* Asset,FString& Error){
 TArray<UObject*> Owned;GetObjectsWithOuter(Asset,Owned,EGetObjectsFlags::IncludeNestedObjects);Owned.Add(Asset);
 for(auto* Object:Owned){TArray<UObject*> Refs;FReferenceFinder Finder(Refs,nullptr,false,true,false,true);Finder.FindReferences(Object);
  for(auto* Ref:Refs)if(Ref&&!Ref->IsA<UClass>()&&(Ref->HasAnyFlags(RF_Transient)||Ref->GetOutermost()==GetTransientPackage()||Ref->GetOutermost()->HasAnyFlags(RF_Transient)||Ref->GetOutermost()->GetName().StartsWith(TEXT("/Temp/")))){
   Error=TEXT("Unsaved dependency: ")+Object->GetPathName()+TEXT(" -> ")+Ref->GetPathName();return false;}}
 return true;
}
class FCardAssetImport : public FGCObject {
public:
 FString Root,Error;int32 Serial=0;TArray<UObject*> Assets;TMap<UObject*,UObject*> Copies;TMap<FString,UTexture2D*> TextureCache;
 void AddReferencedObjects(FReferenceCollector& Collector) override {Collector.AddReferencedObjects(Assets);}
 FString GetReferencerName() const override {return TEXT("FCardAssetImport");}
 explicit FCardAssetImport(const FString& In):Root(In){}
 template<class T>T* Asset(const FString& Prefix){const FString Name=FString::Printf(TEXT("%s_%05d"),*Prefix,Serial++);const FString Path=Root/TEXT("Resources")/Name;
  auto* Value=NewObject<T>(CreatePackage(*Path),*Name,RF_Public|RF_Standalone);Assets.Add(Value);return Value;}
 UTexture2D* Texture(UTexture2D* Src){
  if(!Src)return nullptr;if(auto* Found=Copies.Find(Src))return Cast<UTexture2D>(*Found);
  if(Src->IsAsset()&&!Src->HasAnyFlags(RF_Transient)&&Src->GetOutermost()!=GetTransientPackage())return Src;
  auto* Data=Src->GetPlatformData();if(!Data||Data->Mips.IsEmpty()){Error=TEXT("Texture has no readable mip");return nullptr;}
  auto& Mip=Data->Mips[0];const EPixelFormat Pixel=Data->PixelFormat;
  const ETextureSourceFormat Format=Pixel==PF_B8G8R8A8?TSF_BGRA8:Pixel==PF_G8?TSF_G8:Pixel==PF_FloatRGBA?TSF_RGBA16F:TSF_Invalid;
  if(Format==TSF_Invalid){Error=TEXT("Unsupported source texture pixel format: ")+Src->GetName();return nullptr;}
  // Preserve the native/runtime mip chain: UE-generated mips can change alpha clipping.
  TArray64<uint8> Pixels;int32 Width=Mip.SizeX,Height=Mip.SizeY;
  for(int32 I=0;I<Data->Mips.Num();++I){auto& Level=Data->Mips[I];
   const int64 Expected=int64(Width)*Height*(Format==TSF_BGRA8?4:Format==TSF_G8?1:8);
   if(Width<=0||Height<=0||Level.SizeX!=Width||Level.SizeY!=Height||Level.SizeZ!=1||Level.BulkData.GetBulkDataSize()!=Expected||(I>0&&Data->Mips[I-1].SizeX==1&&Data->Mips[I-1].SizeY==1)){Error=TEXT("Invalid source texture dimensions or mip size: ")+Src->GetName();return nullptr;}
   const uint8* Raw=static_cast<const uint8*>(Level.BulkData.LockReadOnly());
   if(!Raw){Level.BulkData.Unlock();Error=TEXT("Texture mip unavailable: ")+Src->GetName();return nullptr;}
   Pixels.Append(Raw,Expected);Level.BulkData.Unlock();Width=FMath::Max(1,Width/2);Height=FMath::Max(1,Height/2);
  }
  uint8 Digest[20];FSHA1::HashBuffer(Pixels.GetData(),Pixels.Num(),Digest);
  const FString Key=BytesToHex(Digest,20)+FString::Printf(TEXT("_%d_%d_%d_%d_%d_%d_%d_%d_%d"),Mip.SizeX,Mip.SizeY,Data->Mips.Num(),int32(Pixel),Src->SRGB,int32(Src->Filter),int32(Src->AddressX),int32(Src->AddressY),int32(Src->CompressionSettings));
  if(auto** Existing=TextureCache.Find(Key)){Copies.Add(Src,*Existing);return *Existing;}
  auto* Dst=Asset<UTexture2D>(TEXT("T"));Dst->Source.Init(Mip.SizeX,Mip.SizeY,1,Data->Mips.Num(),Format,Pixels.GetData());
  Dst->SRGB=Src->SRGB;Dst->Filter=Src->Filter;Dst->AddressX=Src->AddressX;Dst->AddressY=Src->AddressY;Dst->CompressionSettings=Src->CompressionSettings;
  Dst->MipGenSettings=Data->Mips.Num()==1?TMGS_NoMipmaps:TMGS_LeaveExistingMips;
  Dst->NeverStream=false;Dst->PostEditChange();Copies.Add(Src,Dst);TextureCache.Add(Key,Dst);return Dst;
 }
 UObject* PersistReference(UObject* Src){
  if(!Src)return nullptr;if(auto* Found=Copies.Find(Src))return *Found;
  if(auto* Tex=Cast<UTexture2D>(Src))return Texture(Tex);
  if(auto* Cube=Cast<UTextureCube>(Src)){
   if(Cube->IsAsset()&&!Cube->HasAnyFlags(RF_Transient))return Cube;
   auto* Data=Cube->GetPlatformData();
   if(!Data||Data->Mips.Num()!=1||Data->PixelFormat!=PF_B8G8R8A8){Error=TEXT("Unsupported transient environment cube");return nullptr;}
   auto& Mip=Data->Mips[0];const int64 Expected=int64(Mip.SizeX)*Mip.SizeY*6*4;
   if(Expected<=0||Mip.BulkData.GetBulkDataSize()!=Expected){Error=TEXT("Invalid transient cube faces");return nullptr;}
   const uint8* Pixels=static_cast<const uint8*>(Mip.BulkData.LockReadOnly());
   if(!Pixels){Mip.BulkData.Unlock();Error=TEXT("Missing transient cube pixels");return nullptr;}
   auto* Dst=Asset<UTextureCube>(TEXT("Cube"));Dst->Source.Init(Mip.SizeX,Mip.SizeY,6,1,TSF_BGRA8,Pixels);Mip.BulkData.Unlock();
   Dst->SRGB=Cube->SRGB;Dst->Filter=Cube->Filter;Dst->CompressionSettings=Cube->CompressionSettings;Dst->MipGenSettings=TMGS_NoMipmaps;Dst->PostEditChange();Copies.Add(Src,Dst);return Dst;
  }
  if(Cast<UTextureRenderTarget2D>(Src)){
   // A capture is scene dependent, not authored card data. Far depth means
   // unobstructed lighting until the destination scene supplies its own capture.
   auto* Dst=Asset<UTexture2D>(TEXT("T_UnoccludedDepth"));const FFloat16Color Pixel(FLinearColor(65504.f,65504.f,65504.f,1.f));
   Dst->Source.Init(1,1,1,1,TSF_RGBA16F,reinterpret_cast<const uint8*>(&Pixel));Dst->SRGB=false;Dst->CompressionSettings=TC_HDR;Dst->PostEditChange();Copies.Add(Src,Dst);return Dst;
  }
  if(auto* Mat=Cast<UMaterialInterface>(Src))return Material(Mat);
  return Src;
 }
 UMaterialInterface* Material(UMaterialInterface* Src){
  if(!Src)return nullptr;if(auto* Found=Copies.Find(Src))return Cast<UMaterialInterface>(*Found);
  if(Src->IsAsset()&&!Src->HasAnyFlags(RF_Transient)&&Src->GetOutermost()!=GetTransientPackage())return Src;
  if(auto* Instance=Cast<UMaterialInstance>(Src)){
   auto* Dst=Asset<UMaterialInstanceConstant>(TEXT("MI"));Copies.Add(Src,Dst);
   auto* Parent=Material(Instance->Parent);if(!Parent)return nullptr;
   Dst->SetParentEditorOnly(Parent);Dst->CopyMaterialUniformParametersEditorOnly(Src,true);
   TArray<FMaterialParameterInfo> Infos;TArray<FGuid> Ids;Src->GetAllTextureParameterInfo(Infos,Ids);
   for(const auto& Info:Infos){UTexture* Tex=nullptr;if(Src->GetTextureParameterValue(Info,Tex)){
    auto* Saved=Cast<UTexture>(PersistReference(Tex));Dst->SetTextureParameterValueEditorOnly(Info,Saved);}}
   // Runtime instances may carry overrides absent from the current expression
   // dictionary. Persist those too instead of silently dropping their references.
   const auto Overrides=Dst->TextureParameterValues;
   for(const auto& Parameter:Overrides)Dst->SetTextureParameterValueEditorOnly(Parameter.ParameterInfo,Cast<UTexture>(PersistReference(Parameter.ParameterValue)));
   Dst->PostEditChange();return Dst;
  }
  auto* Original=Cast<UMaterial>(Src);if(!Original){Error=TEXT("Unsupported material type");return nullptr;}
  const FString Name=FString::Printf(TEXT("M_%05d"),Serial++);auto* Package=CreatePackage(*(Root/TEXT("Resources")/Name));
  auto* Dst=DuplicateObject<UMaterial>(Original,Package,*Name);Dst->ClearFlags(RF_Transient);Dst->SetFlags(RF_Public|RF_Standalone);Assets.Add(Dst);Copies.Add(Src,Dst);
  TArray<UObject*> Owned;GetObjectsWithOuter(Dst,Owned,EGetObjectsFlags::IncludeNestedObjects);for(auto* Child:Owned)Child->ClearFlags(RF_Transient);Owned.Add(Dst);
  TArray<UObject*> Refs;FReferenceFinder Finder(Refs,nullptr,false,true,false,false);for(auto* Object:Owned)Finder.FindReferences(Object);
  TMap<UObject*,UObject*> Replace;
  for(auto* Ref:Refs)if(Cast<UTexture>(Ref)){auto* Saved=PersistReference(Ref);if(Saved!=Ref)Replace.Add(Ref,Saved);}
  FArchiveReplaceObjectRef<UObject> Archive(Dst,Replace,EArchiveReplaceObjectFlags::IgnoreOuterRef|EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
  Dst->PostEditChange();return Dst;
 }
 UStaticMesh* Static(UProceduralMeshComponent* Component){
  FMeshDescription Desc;FStaticMeshAttributes A(Desc);A.Register();A.GetVertexInstanceUVs().SetNumChannels(4);
  auto* Mesh=Asset<UStaticMesh>(TEXT("SM"));
  for(int32 Section=0;Section<Component->GetNumSections();++Section){const auto* S=Component->GetProcMeshSection(Section);if(!S||!S->bSectionVisible||S->ProcIndexBuffer.IsEmpty())continue;
   if(S->ProcIndexBuffer.Num()%3!=0){Error=TEXT("Incomplete source triangle");return nullptr;}
   auto* Mat=Material(Component->GetMaterial(Section));if(!Mat||!Error.IsEmpty())return nullptr;
   const FPolygonGroupID Group=Desc.CreatePolygonGroup();A.GetPolygonGroupMaterialSlotNames()[Group]=FName(*FString::Printf(TEXT("Slot%d"),Mesh->GetStaticMaterials().Num()));Mesh->GetStaticMaterials().Add(FStaticMaterial(Mat,A.GetPolygonGroupMaterialSlotNames()[Group]));
   TArray<FVertexID> Verts;for(const auto& V:S->ProcVertexBuffer){auto Id=Desc.CreateVertex();A.GetVertexPositions()[Id]=FVector3f(V.Position);Verts.Add(Id);}
   for(int32 I=0;I<S->ProcIndexBuffer.Num();I+=3){TArray<FVertexInstanceID> Corners;
    for(int32 C=0;C<3;++C){const int32 Index=S->ProcIndexBuffer[I+C];if(!Verts.IsValidIndex(Index)){Error=TEXT("Invalid triangle index");return nullptr;}
     const auto& V=S->ProcVertexBuffer[Index];auto Id=Desc.CreateVertexInstance(Verts[Index]);Corners.Add(Id);
     A.GetVertexInstanceNormals()[Id]=FVector3f(V.Normal);A.GetVertexInstanceTangents()[Id]=FVector3f(V.Tangent.TangentX);A.GetVertexInstanceBinormalSigns()[Id]=V.Tangent.bFlipTangentY?-1.f:1.f;
     const FLinearColor Color(V.Color);A.GetVertexInstanceColors()[Id]=FVector4f(Color.R,Color.G,Color.B,Color.A);
     const FVector2D UV[]={V.UV0,V.UV1,V.UV2,V.UV3};for(int32 U=0;U<4;++U)A.GetVertexInstanceUVs().Set(Id,U,FVector2f(UV[U]));}
    Desc.CreateTriangle(Group,Corners);
   }
  }
  if(Desc.Triangles().Num()==0){Error=TEXT("No visible source triangles");return nullptr;}
  Mesh->AddSourceModel();Mesh->GetSourceModel(0).BuildSettings.bRecomputeNormals=false;Mesh->GetSourceModel(0).BuildSettings.bRecomputeTangents=false;
  Mesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs=false;Mesh->GetSourceModel(0).BuildSettings.bRemoveDegenerates=false;
  Mesh->GetSourceModel(0).BuildSettings.bUseFullPrecisionUVs=true;Mesh->CreateMeshDescription(0,MoveTemp(Desc));Mesh->CommitMeshDescription(0);Mesh->Build(false);Mesh->PostEditChange();return Mesh;
 }
 USkeletalMesh* Skeletal(FOkSession& Session,int32 Part,USkeleton* Skeleton,UMaterialInterface* Mat){
  OkMeshView View{};if(!Session.PartAt(Part,View)||!View.rest_positions||!View.weights||!View.bone_idx||View.skel_kind<1||View.skel_kind>3)return nullptr;
  auto* Source=OKBuildSkeletalMesh(Session,View,GetTransientPackage(),Skeleton,Part);if(!Source){Error=TEXT("Failed to construct source skin");return nullptr;}
  FlushRenderingCommands();const auto& Render=Source->GetResourceForRendering()->LODRenderData[0];
  auto* Mesh=Asset<USkeletalMesh>(TEXT("SK"));Mesh->SetSkeleton(Skeleton);Mesh->SetRefSkeleton(Source->GetRefSkeleton());Mesh->CalculateInvRefMatrices();Mesh->AddLODInfo();Mesh->GetMaterials().Add(FSkeletalMaterial(Mat));
  FSkeletalMeshLODModel LOD;LOD.NumTexCoords=Render.GetNumTexCoords();LOD.NumVertices=Render.GetNumVertices();
  Render.MultiSizeIndexContainer.GetIndexBuffer(LOD.IndexBuffer);LOD.MeshToImportVertexMap.SetNum(LOD.NumVertices);for(uint32 I=0;I<LOD.NumVertices;++I)LOD.MeshToImportVertexMap[I]=I;LOD.MaxImportVertex=LOD.NumVertices-1;
  for(const auto& R:Render.RenderSections){FSkelMeshSection S;S.MaterialIndex=0;S.BaseIndex=R.BaseIndex;S.NumTriangles=R.NumTriangles;S.BaseVertexIndex=R.BaseVertexIndex;S.NumVertices=R.NumVertices;S.BoneMap=R.BoneMap;S.MaxBoneInfluences=Render.SkinWeightVertexBuffer.GetMaxBoneInfluences();
   for(uint32 I=R.BaseVertexIndex;I<R.BaseVertexIndex+R.NumVertices;++I){FSoftSkinVertex V; FMemory::Memzero(&V,sizeof(V));
    V.Position=Render.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(I);V.TangentX=Render.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(I);V.TangentY=Render.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(I);const FVector3f Normal=Render.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(I);V.TangentZ=FVector4f(Normal,(FVector3f::CrossProduct(V.TangentX,V.TangentY)|Normal)<0.f?-1.f:1.f);
    V.Color=Render.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices()?Render.StaticVertexBuffers.ColorVertexBuffer.VertexColor(I):FColor::White;
    for(uint32 U=0;U<LOD.NumTexCoords;++U)V.UVs[U]=Render.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I,U);
    const FSkinWeightInfo Weights=Render.SkinWeightVertexBuffer.GetVertexSkinWeights(I);
    for(int32 B=0;B<MAX_TOTAL_INFLUENCES;++B){V.InfluenceBones[B]=Weights.InfluenceBones[B];V.InfluenceWeights[B]=Weights.InfluenceWeights[B];}S.SoftVertices.Add(V);
   }LOD.Sections.Add(MoveTemp(S));
  }
  Mesh->SetHasVertexColors(Source->GetHasVertexColors());
  FMeshDescription Desc;LOD.GetMeshDescription(Mesh,0,Desc);
  if(Source->GetHasVertexColors()!=Mesh->GetHasVertexColors()){Error=TEXT("Source skin vertex-color flag lost: ")+Source->GetName();return nullptr;}
  FSkeletalMeshAttributes A(Desc);A.Register(true);const auto& Ref=Mesh->GetRefSkeleton();
  if(Source->GetHasVertexColors()){const auto Colors=A.GetVertexInstanceColors();for(auto Id:Desc.VertexInstances().GetElementIDs()){
   const auto Index=Desc.GetVertexInstanceVertex(Id).GetValue();const auto Expected=FVector4f(Render.StaticVertexBuffers.ColorVertexBuffer.VertexColor(Index).ReinterpretAsLinear());
   if(!Colors[Id].Equals(Expected,1.e-6f)){Error=TEXT("Source skin vertex color lost during conversion: ")+Source->GetName();return nullptr;}}}
  if(A.GetNumBones()==0)for(int32 I=0;I<Ref.GetNum();++I){auto Id=A.CreateBone();A.GetBoneNames()[Id]=Ref.GetBoneName(I);A.GetBoneParentIndices()[Id]=Ref.GetParentIndex(I);A.GetBonePoses()[Id]=Ref.GetRefBonePose()[I];}
  for(const auto& Morph:Source->GetMorphTargets())if(Morph->GetMorphLODModels().Num()>0){A.RegisterMorphTargetAttribute(Morph->GetFName(),false);auto Deltas=A.GetVertexMorphPositionDelta(Morph->GetFName());for(const auto& D:Morph->GetMorphLODModels()[0].Vertices)if(Desc.IsVertexValid(FVertexID(D.SourceIdx)))Deltas[FVertexID(D.SourceIdx)]=D.PositionDelta;}
  Mesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());
  Mesh->GetLODInfo(0)->BuildSettings.bRecomputeNormals=false;Mesh->GetLODInfo(0)->BuildSettings.bRecomputeTangents=false;Mesh->GetLODInfo(0)->BuildSettings.bUseFullPrecisionUVs=true;
  Mesh->GetLODInfo(0)->BuildSettings.bRemoveDegenerates=false;
  Mesh->GetLODInfo(0)->BuildSettings.ThresholdPosition=0.f;Mesh->GetLODInfo(0)->BuildSettings.ThresholdTangentNormal=0.f;Mesh->GetLODInfo(0)->BuildSettings.ThresholdUV=0.f;
  Mesh->GetLODInfo(0)->ReductionSettings.NumOfTrianglesPercentage=1.f;Mesh->GetLODInfo(0)->ReductionSettings.NumOfVertPercentage=1.f;
  Mesh->GetLODInfo(0)->BuildSettings.MorphThresholdPosition=0.f;
  Mesh->CreateMeshDescription(0,MoveTemp(Desc));Mesh->CommitMeshDescription(0);Mesh->Build();Mesh->PostEditChange();return Mesh;
 }
 USkeletalMesh* ExactSkeletal(FOkSession& Session,const FKKLoadedPart& Part,FKKImportedPart& Saved,USkeleton* Skeleton){
  OkSkinPaletteView Raw{};TArray<float> Matrices;const int32 Slots=Saved.RigidFollow.IsEmpty()?Session.SkinPalette(Part.SessionIndex,Raw,Matrices):1;
  if(Slots<=0){Error=TEXT("Missing exact source palette: ")+Saved.Name;return nullptr;}
  const auto* Main=Part.Comp->GetProcMeshSection(0);if(!Main||Main->ProcVertexBuffer.IsEmpty()){Error=TEXT("Missing source section");return nullptr;}
  const int32 NV=Main->ProcVertexBuffer.Num();if(Saved.RigidFollow.IsEmpty()&&Raw.n_verts!=NV){Error=TEXT("Source palette/section vertices differ");return nullptr;}
  auto* Mesh=Asset<USkeletalMesh>(TEXT("SK"));Mesh->SetSkeleton(Skeleton);Mesh->SetRefSkeleton(Skeleton->GetReferenceSkeleton());Mesh->CalculateInvRefMatrices();Mesh->AddLODInfo();Mesh->SetHasVertexColors(true);
  FMeshDescription Desc;FSkeletalMeshAttributes A(Desc);A.Register(true);A.GetVertexInstanceUVs().SetNumChannels(4);
  const auto& Ref=Mesh->GetRefSkeleton();for(int32 B=0;B<Ref.GetNum();++B){auto Id=A.CreateBone();A.GetBoneNames()[Id]=Ref.GetBoneName(B);A.GetBoneParentIndices()[Id]=Ref.GetParentIndex(B);A.GetBonePoses()[Id]=Ref.GetRefBonePose()[B];}
  TArray<FMatrix> Blends;Blends.SetNum(NV);TArray<TArray<UE::AnimationCore::FBoneWeight>> Weights;Weights.SetNum(NV);
  for(int32 V=0;V<NV;++V){TMap<int32,double> Combined;if(Saved.RigidFollow.IsEmpty()){if(OKHasSignedSkinVertex(Raw,V))Combined.Add(-V-1,1.);else for(int32 K=0;K<4;++K)if(Raw.weights[V*4+K]>=1e-6)Combined.FindOrAdd(Raw.bone_idx[V*4+K])+=Raw.weights[V*4+K];}else Combined.Add(0,1.);
   TArray<int32> Keys;Combined.GetKeys(Keys);Keys.Sort();double Sum=0.;for(int32 K:Keys)Sum+=Combined[K];if(Sum<=0.){Error=TEXT("Empty vertex weights");return nullptr;}
   FMatrix Blend(ForceInit);TArray<int32> Quantized;int32 Acc=0,Max=0;
   for(int32 K=0;K<Keys.Num();++K){const double W=Combined[Keys[K]]/Sum;FMatrix M=FMatrix::Identity;if(Saved.RigidFollow.IsEmpty()&&!OKExactPaletteMatrix(Raw,Matrices,Keys[K],M)){Error=TEXT("Invalid signed source palette");return nullptr;}
    for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Blend.M[R][C]+=M.M[R][C]*W;Quantized.Add(FMath::RoundToInt(W*21845.));Acc+=Quantized.Last();if(W>Combined[Keys[Max]]/Sum)Max=K;}
   Quantized[Max]+=21845-Acc;Blends[V]=Blend;if(FMath::Abs(Blend.Determinant())<1e-12){Error=TEXT("Singular reference skin matrix: ")+Saved.Name;return nullptr;}
   for(int32 K=0;K<Keys.Num();++K)if(Quantized[K]>0)for(int32 Axis=0;Axis<3;++Axis){const int32 Bone=Ref.FindBoneIndex(OKPaletteBoneName(Part.SessionIndex,Keys[K],Axis));if(Bone==INDEX_NONE){Error=TEXT("Missing palette bone");return nullptr;}Weights[V].Add(UE::AnimationCore::FBoneWeight(static_cast<FBoneIndexType>(Bone),static_cast<uint16>(Quantized[K])));}
   Weights[V].Sort([](const auto& L,const auto& R){return L.GetRawWeight()>R.GetRawWeight();});
  }
  TArray<int32> SectionStarts;
  for(int32 Section=0;Section<Part.Comp->GetNumSections();++Section){const auto* S=Part.Comp->GetProcMeshSection(Section);if(!S||!S->bSectionVisible||S->ProcIndexBuffer.IsEmpty())continue;
   if(S->ProcVertexBuffer.Num()!=NV||S->ProcIndexBuffer.Num()%3){Error=TEXT("Source section does not share renderer vertex layout: ")+Saved.Name;return nullptr;}
   auto* Mat=Material(Part.Comp->GetMaterial(Section));if(!Mat){Error=TEXT("Missing section material");return nullptr;}OKEnsureSkeletalMaterialUsage(Mat);
   const FName SlotName(*FString::Printf(TEXT("Slot%d"),Mesh->GetMaterials().Num()));const auto Group=Desc.CreatePolygonGroup();A.GetPolygonGroupMaterialSlotNames()[Group]=SlotName;Mesh->GetMaterials().Add(FSkeletalMaterial(Mat,true,false,SlotName));SectionStarts.Add(Desc.Vertices().Num());
   TArray<FVertexID> Vertices;for(int32 V=0;V<NV;++V){auto Id=Desc.CreateVertex();A.GetVertexPositions()[Id]=FVector3f(FVector(Blends[V].Inverse().TransformPosition(S->ProcVertexBuffer[V].Position)));A.GetVertexSkinWeights().SetRaw(Id,Weights[V]);Vertices.Add(Id);}
   for(int32 I=0;I<S->ProcIndexBuffer.Num();I+=3){TArray<FVertexInstanceID> Corners;for(int32 C=0;C<3;++C){const int32 V=S->ProcIndexBuffer[I+C];if(!Vertices.IsValidIndex(V)){Error=TEXT("Invalid source triangle");return nullptr;}const auto& P=S->ProcVertexBuffer[V];auto Id=Desc.CreateVertexInstance(Vertices[V]);Corners.Add(Id);
     const FMatrix Inv=Blends[V].Inverse();A.GetVertexInstanceNormals()[Id]=FVector3f(FVector(Inv.TransformVector(P.Normal)).GetSafeNormal());A.GetVertexInstanceTangents()[Id]=FVector3f(FVector(Inv.TransformVector(P.Tangent.TangentX)).GetSafeNormal());A.GetVertexInstanceBinormalSigns()[Id]=P.Tangent.bFlipTangentY?-1.f:1.f;
     A.GetVertexInstanceColors()[Id]=FVector4f(P.Color.ReinterpretAsLinear());const FVector2D UV[]={P.UV0,P.UV1,P.UV2,P.UV3};for(int32 U=0;U<4;++U)A.GetVertexInstanceUVs().Set(Id,U,FVector2f(UV[U]));}Desc.CreateTriangle(Group,Corners);}
  }
  // Preserve authored mesh-local morphs; the old CPU morph API returns posed body deltas.
  for(int32 M=0;M<Session.MorphCount(Part.SessionIndex);++M){FString Name;TArray<uint32> Indices;TArray<float> Deltas;if(!Session.RawMorph(Part.SessionIndex,M,Name,Indices,Deltas)){Error=TEXT("Cannot read raw morph");return nullptr;}if(Indices.IsEmpty())continue;
   const FName MorphName(*Name);A.RegisterMorphTargetAttribute(MorphName,false);auto Values=A.GetVertexMorphPositionDelta(MorphName);
   for(int32 I=0;I<Indices.Num();++I){if(Indices[I]>=uint32(NV)){Error=TEXT("Morph vertex outside source renderer");return nullptr;}const float* D=Deltas.GetData()+I*3;const FVector3f Delta(-D[0]*100.,D[2]*100.,D[1]*100.);for(int32 Start:SectionStarts)Values[FVertexID(Start+Indices[I])]=Delta;}
  }
  Mesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());auto* LOD=Mesh->GetLODInfo(0);LOD->BuildSettings.bRecomputeNormals=false;LOD->BuildSettings.bRecomputeTangents=false;LOD->BuildSettings.bUseFullPrecisionUVs=true;LOD->BuildSettings.bRemoveDegenerates=false;LOD->BuildSettings.BoneInfluenceLimit=12;LOD->BuildSettings.bUseHighPrecisionSkinWeights=true;LOD->BuildSettings.MorphThresholdPosition=0.f;
  LOD->BuildSettings.ThresholdPosition=0.f;LOD->BuildSettings.ThresholdTangentNormal=0.f;LOD->BuildSettings.ThresholdUV=0.f;
  LOD->BuildSettings.ThresholdPosition=0.f;LOD->BuildSettings.ThresholdTangentNormal=0.f;LOD->BuildSettings.ThresholdUV=0.f;LOD->ReductionSettings.NumOfTrianglesPercentage=1.f;LOD->ReductionSettings.NumOfVertPercentage=1.f;LOD->ReductionSettings.MaxBonesPerVertex=12;
  Mesh->CreateMeshDescription(0,MoveTemp(Desc));Mesh->CommitMeshDescription(0);Mesh->Build();Mesh->PostEditChange();
  const auto& Render=Mesh->GetResourceForRendering()->LODRenderData[0];TMap<int32,int32> BoneSlots;
  for(int32 Slot:Saved.PaletteSlots)for(int32 Axis=0;Axis<3;++Axis)BoneSlots.Add(Ref.FindBoneIndex(OKPaletteBoneName(Part.SessionIndex,Slot,Axis)),Slot);
  for(const auto& Section:Render.RenderSections)for(uint32 V=Section.BaseVertexIndex;V<Section.BaseVertexIndex+Section.NumVertices;++V){const auto W=Render.SkinWeightVertexBuffer.GetVertexSkinWeights(V);TMap<int32,TArray<int32>> Triples;uint32 Total=0;
   for(int32 K=0;K<MAX_TOTAL_INFLUENCES;++K){Total+=W.InfluenceWeights[K];if(!W.InfluenceWeights[K])continue;if(!Section.BoneMap.IsValidIndex(W.InfluenceBones[K])){Error=TEXT("Invalid rebuilt section bone map");return nullptr;}
    const auto* Slot=BoneSlots.Find(Section.BoneMap[W.InfluenceBones[K]]);if(!Slot){Error=TEXT("Rebuilt palette weight references a native hierarchy bone");return nullptr;}Triples.FindOrAdd(*Slot).Add(W.InfluenceWeights[K]);}
   if(Total!=65535){Error=TEXT("Rebuilt skin weights not 16bit normalized");return nullptr;}for(const auto& Triple:Triples)if(Triple.Value.Num()!=3||Triple.Value[0]!=Triple.Value[1]||Triple.Value[0]!=Triple.Value[2]){Error=FString::Printf(TEXT("Rebuilt skin pruned or unbalanced a palette column renderer=%s vertex=%u slot=%d weights=%s max_influences=%d; verify UnlimitedBoneInfluences and 16BitBoneIndex project settings"),*Saved.NativeRenderer,V,Triple.Key,*FString::JoinBy(Triple.Value,TEXT(","),[](int32 W){return FString::FromInt(W);}),Render.SkinWeightVertexBuffer.GetMaxBoneInfluences());return nullptr;}}
  UE_LOG(LogTemp,Display,TEXT("Exact skin saved source=%s sections=%d vertices=%d influences=%d bones=%d"),*Saved.NativeRenderer,Mesh->GetMaterials().Num(),Render.GetNumVertices(),Render.SkinWeightVertexBuffer.GetMaxBoneInfluences(),Ref.GetNum());return Mesh;
 }
 bool Save(){
  FAssetCompilingManager::Get().FinishAllCompilation();
  for(auto* Asset:Assets)if(auto* Mesh=Cast<UStaticMesh>(Asset))if(!CheckUVStreams(Mesh,Error))return false;
  for(auto* Asset:Assets)if(auto* Mesh=Cast<USkeletalMesh>(Asset))if(!CheckSkinColors(Mesh,Error))return false;
  int64 MorphTargets=0,MorphSamples=0,TinyMorphs=0;for(auto* Asset:Assets)if(auto* Mesh=Cast<USkeletalMesh>(Asset))if(!CheckSkinMorphs(Mesh,Error,MorphTargets,MorphSamples,TinyMorphs))return false;
  UE_LOG(LogTemp,Display,TEXT("Independent morph verified targets=%lld samples=%lld ue_nearly_zero_samples=%lld"),MorphTargets,MorphSamples,TinyMorphs);
  for(auto* Asset:Assets)if(auto* Mesh=Cast<USkeletalMesh>(Asset)){uint32 RenderTriangles=0;for(const auto& Section:Mesh->GetResourceForRendering()->LODRenderData[0].RenderSections)RenderTriangles+=Section.NumTriangles;
   const auto& Desc=*Mesh->GetMeshDescription(0);const uint32 Expected=RenderableSkinTriangles(Desc);
   if(RenderTriangles!=Expected){Error=FString::Printf(TEXT("Source skin triangles reduced: %s source=%d renderable=%u render=%u"),*Mesh->GetPathName(),Desc.Triangles().Num(),Expected,RenderTriangles);return false;}}
  for(auto* Asset:Assets)if(!CheckReferences(Asset,Error))return false;
  for(auto* Asset:Assets){const FString Filename=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());if(IFileManager::Get().FileExists(*Filename)){Error=TEXT("Output appeared during import; refusing overwrite: ")+Filename;return false;}}
  // Publish the manifest last so a failed resource save has no success manifest.
  Assets.StableSort([](const UObject& A,const UObject& B){return !A.IsA<UKKImportedCharacter>()&&B.IsA<UKKImportedCharacter>();});
  for(auto* Asset:Assets){Asset->MarkPackageDirty();FAssetRegistryModule::AssetCreated(Asset);
   FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
   const FString Filename=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
   if(!UPackage::SavePackage(Asset->GetOutermost(),Asset,*Filename,Args)){Error=TEXT("Failed saving: ")+Filename;return false;}}
  return true;
 }
};
}
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKImportedDegenerateTest,"OpenKoikatsu.ImportedAssets.PermanentDegenerates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKImportedDegenerateTest::RunTest(const FString&){
 FMeshDescription Desc;FSkeletalMeshAttributes A(Desc);A.Register(true);A.GetVertexInstanceUVs().SetNumChannels(4);
 TArray<FVertexID> V;TArray<FVertexInstanceID> I;
 const TArray<UE::AnimationCore::FBoneWeight> W={UE::AnimationCore::FBoneWeight(uint16(0),uint16(65535))};
 for(int32 K=0;K<3;++K){V.Add(Desc.CreateVertex());A.GetVertexPositions()[V[K]]=K==2?FVector3f(0,1,0):FVector3f::ZeroVector;A.GetVertexSkinWeights().SetRaw(V[K],W);I.Add(Desc.CreateVertexInstance(V[K]));}
 Desc.CreateTriangle(Desc.CreatePolygonGroup(),I);
 TestEqual(TEXT("Only permanent exact coincident geometry may be removed"),RenderableSkinTriangles(Desc),0u);
 A.RegisterMorphTargetAttribute(TEXT("SeparatingMorph"),false);auto Morph=A.GetVertexMorphPositionDelta(TEXT("SeparatingMorph"));Morph[V[1]]=FVector3f(1,0,0);
 TestEqual(TEXT("A morph can expand a bind-pose zero triangle"),RenderableSkinTriangles(Desc),1u);Morph[V[1]]=FVector3f::ZeroVector;
 const TArray<UE::AnimationCore::FBoneWeight> Other={UE::AnimationCore::FBoneWeight(uint16(1),uint16(65535))};A.GetVertexSkinWeights().SetRaw(V[1],Other);
 TestEqual(TEXT("Different animation bones can separate coincident vertices"),RenderableSkinTriangles(Desc),1u);A.GetVertexSkinWeights().SetRaw(V[1],W);
 A.GetVertexInstanceUVs().Set(I[1],0,FVector2f(1,0));TestEqual(TEXT("Preserve corners that UE cannot merge"),RenderableSkinTriangles(Desc),1u);
 return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKImportedMipTest,"OpenKoikatsu.ImportedAssets.NativeMips",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKImportedMipTest::RunTest(const FString& Parameters){
 auto Make=[](uint8 Low,bool Chain){auto* Tex=UTexture2D::CreateTransient(4,4,PF_B8G8R8A8);auto* Data=Tex->GetPlatformData();
  const int32 Count=Chain?3:1;for(int32 I=0;I<Count;++I){auto* Mip=I==0?&Data->Mips[0]:new FTexture2DMipMap();if(I)Data->Mips.Add(Mip);
   Mip->SizeX=Mip->SizeY=4>>I;Mip->SizeZ=1;Mip->BulkData.Lock(LOCK_READ_WRITE);void* Bytes=Mip->BulkData.Realloc(Mip->SizeX*Mip->SizeY*4);FMemory::Memset(Bytes,I?Low:200,Mip->SizeX*Mip->SizeY*4);Mip->BulkData.Unlock();}
  return Tex;};
 // Identical top levels, distinct native low levels: these must not be deduplicated.
 FCardAssetImport Import(TEXT("/Temp/ImportedMipAutomation"));auto* A=Import.Texture(Make(127,true));auto* B=Import.Texture(Make(128,true));auto* Same=Import.Texture(Make(127,true));auto* Single=Import.Texture(Make(127,false));
 if(!TestNotNull(TEXT("Imported native mip chain"),A)||!TestNotNull(TEXT("Distinct chain"),B)||!TestNotNull(TEXT("Single level"),Single))return false;
 TestTrue(TEXT("Different native mips remain distinct"),A!=B);TestTrue(TEXT("Identical full chains deduplicate"),A==Same);
 TestEqual(TEXT("All native levels retained"),A->Source.GetNumMips(),3);TestTrue(TEXT("UE must preserve existing levels"),A->MipGenSettings==TMGS_LeaveExistingMips);
 TestEqual(TEXT("Single-level source stays single"),Single->Source.GetNumMips(),1);TestTrue(TEXT("No synthetic mips"),Single->MipGenSettings==TMGS_NoMipmaps);
 for(int32 I=0;I<3;++I){TArray64<uint8> Bytes;if(TestTrue(TEXT("Readable source mip"),A->Source.GetMipData(Bytes,I))){TestEqual(TEXT("Mip byte count"),Bytes.Num(),int64((4>>I)*(4>>I)*4));for(uint8 Byte:Bytes)if(Byte!=(I?127:200)){AddError(TEXT("Native mip bytes changed"));break;}}}
 auto* Invalid=Make(127,true);Invalid->GetPlatformData()->Mips[1].SizeX=3;FCardAssetImport Bad(TEXT("/Temp/InvalidMipAutomation"));TestNull(TEXT("Malformed chain rejected"),Bad.Texture(Invalid));TestTrue(TEXT("Malformed chain has diagnostic"),!Bad.Error.IsEmpty());
 FAssetCompilingManager::Get().FinishAllCompilation();UE_LOG(LogTemp,Display,TEXT("Imported native mip audit chain=3 distinct_low_levels=preserved single_level=preserved malformed=rejected"));return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKImportedCubeTest,"OpenKoikatsu.ImportedAssets.TransientCube",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKImportedCubeTest::RunTest(const FString&){
 auto* Cube=UTextureCube::CreateTransient(1,1,PF_B8G8R8A8);Cube->SRGB=false;
 auto& Bulk=Cube->GetPlatformData()->Mips[0].BulkData;auto* Pixels=static_cast<uint8*>(Bulk.Lock(LOCK_READ_WRITE));
 for(int32 I=0;I<24;++I)Pixels[I]=I;Bulk.Unlock();
 FCardAssetImport Import(TEXT("/Temp/ImportedCubeAutomation"));auto* Saved=Cast<UTextureCube>(Import.PersistReference(Cube));
 if(!TestNotNull(TEXT("Transient cube persisted"),Saved))return false;
 TestFalse(TEXT("Saved cube is not transient"),Saved->HasAnyFlags(RF_Transient));TestEqual(TEXT("All faces retained"),Saved->Source.GetNumSlices(),6);
 TestFalse(TEXT("Linear cube retains color space"),Saved->SRGB);
 TArray64<uint8> Bytes;if(!TestTrue(TEXT("Readable saved faces"),Saved->Source.GetMipData(Bytes,0)))return false;
 TestEqual(TEXT("Six complete BGRA faces"),Bytes.Num(),int64(24));for(int32 I=0;I<Bytes.Num();++I)if(Bytes[I]!=I){AddError(TEXT("Cube pixel bytes changed"));break;}
 TestTrue(TEXT("Repeated material references share cube"),Import.PersistReference(Cube)==Saved);
 FAssetCompilingManager::Get().FinishAllCompilation();return !HasAnyErrors();
}
#endif
#endif
UKKImportIndependentCardCommandlet::UKKImportIndependentCardCommandlet(){IsClient=false;IsEditor=true;IsServer=false;LogToConsole=true;}
static bool ImportablePart(const AKKPreviewActor* Actor,const FKKLoadedPart& Part){
 if(!Part.Comp)return false;
 const auto* Mouth=Actor->MouthSlots.FindByPredicate([&](const auto& Slot){return Slot.SessionPartIndex==Part.SessionIndex;});
 return Part.Comp->IsVisible()||(Mouth&&(!Mouth->Patterns.IsEmpty()||Mouth->bHideWhenClosed));
}
static void ImportMouthMetadata(const AKKPreviewActor* Actor,const FKKLoadedPart& Part,FKKImportedPart& Out){
 Out.MouthDefaultVisible=Part.Comp->IsVisible();
 const auto* Mouth=Actor->MouthSlots.FindByPredicate([&](const auto& Slot){return Slot.SessionPartIndex==Part.SessionIndex;});if(!Mouth)return;
 Out.MouthHideWhenClosed=Mouth->bHideWhenClosed;
 for(const auto& Pair:Mouth->Patterns){FKKImportedMouthPair P;P.Close=Pair.Key;P.Open=Pair.Value;Out.MouthPatterns.Add(P);
  if(!P.Close.IsNone())Out.MouthBaseline.Add(P.Close,Mouth->BaselineWeights.FindRef(P.Close));
  if(!P.Open.IsNone())Out.MouthBaseline.Add(P.Open,Mouth->BaselineWeights.FindRef(P.Open));}
}
int32 UKKImportIndependentCardCommandlet::Main(const FString& Params){
#if WITH_EDITOR
 FString StorageRoot;
 if(FParse::Value(*Params,TEXT("OptimizeStorage="),StorageRoot)){
  if(!StorageRoot.StartsWith(TEXT("/Game/"))||StorageRoot.Contains(TEXT("..")))return 1;
  FString Report; if(!FParse::Value(*Params,TEXT("StorageReport="),Report))return 1;
  const bool Apply=FParse::Param(*Params,TEXT("StorageApply"));
  FString PackageList;TSet<FString> SelectedPackages;
  if(FParse::Value(*Params,TEXT("StoragePackageList="),PackageList)){
   TArray<FString> Lines;if(!FFileHelper::LoadFileToStringArray(Lines,*PackageList)||Lines.IsEmpty())return 1;
   for(const FString& Line:Lines)SelectedPackages.Add(Line);
  }
  auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
  Registry.SearchAllAssets(true);TArray<FAssetData> Assets;
  Registry.GetAssetsByClass(UKKImportedCharacter::StaticClass()->GetClassPathName(),Assets);
  TArray<TSharedPtr<FJsonValue>> Rows;int64 Before=0,After=0;
  for(const auto& Asset:Assets){const FString Package=Asset.PackageName.ToString();if(!Package.StartsWith(StorageRoot+TEXT("/")))continue;
   if(!SelectedPackages.IsEmpty()&&!SelectedPackages.Contains(Package))continue;
   {TStrongObjectPtr<UKKImportedCharacter> Character(Cast<UKKImportedCharacter>(Asset.GetAsset()));if(!Character)return 1;
    int64 OldBytes=0,NewBytes=0;bool Changed=false;
    for(auto& Outfit:Character->Outfits){if(Outfit.AnimationSnapshot.IsEmpty())continue;
     FOkSession Original,Compact;TArray<uint8> Bytes;
     if(!Original.OpenAnimationSnapshot(Outfit.AnimationSnapshot))return 1;
     if(Outfit.AnimationSnapshot.Num()>=8&&FMemory::Memcmp(Outfit.AnimationSnapshot.GetData(),"OKAN0001",8)==0)Bytes=Outfit.AnimationSnapshot;
     else if(!Original.ExportAnimationSnapshot(Bytes))return 1;
     if(!Compact.OpenAnimationSnapshot(Bytes))return 1;
     // Verify the complete native binding contracts and affine palettes before
     // replacing the snapshot. Existing images remain in UE texture assets.
     if(Original.PartCount()!=Compact.PartCount())return 1;
     if(!Original.ApplyAnimFrame(0.f,0.f)||!Compact.ApplyAnimFrame(0.f,0.f))return 1;
     for(int32 P=0;P<Original.PartCount();++P){
      if(Original.SkinSourceHashV2(P)!=Compact.SkinSourceHashV2(P))return 1;
      OkSkinPaletteView A{},B{};TArray<float> X,Y;
      const int32 N=Original.SkinPalette(P,A,X),M=Compact.SkinPalette(P,B,Y);
      if(N!=M||X!=Y)return 1;
     }
     OldBytes+=Outfit.AnimationSnapshot.Num();NewBytes+=FMath::Min(Outfit.AnimationSnapshot.Num(),Bytes.Num());
     if(Bytes.Num()<Outfit.AnimationSnapshot.Num()){Changed=true;if(Apply)Outfit.AnimationSnapshot=MoveTemp(Bytes);}
    }
    if(Changed&&Apply){const FString File=FPackageName::LongPackageNameToFilename(Package,FPackageName::GetAssetPackageExtension());
     const FString Backup=FPaths::GetPath(Report)/TEXT("Backups")/Package.Mid(6)+TEXT(".uasset");
     if(IFileManager::Get().FileExists(*Backup))return 1;IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup),true);
     if(IFileManager::Get().Copy(*Backup,*File)!=COPY_OK)return 1;
     FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;
     if(!UPackage::SavePackage(Character->GetOutermost(),Character.Get(),*File,Save))return 1;
    }
    Before+=OldBytes;After+=FMath::Min(OldBytes,NewBytes);auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("package"),Package);Row->SetNumberField(TEXT("before_snapshot_bytes"),OldBytes);Row->SetNumberField(TEXT("after_snapshot_bytes"),FMath::Min(OldBytes,NewBytes));Row->SetBoolField(TEXT("applied"),Changed&&Apply);Rows.Add(MakeShared<FJsonValueObject>(Row));
    UE_LOG(LogTemp,Display,TEXT("Import storage package=%s snapshot=%lld -> %lld applied=%d native_bindings_and_palettes=unchanged"),*Package,OldBytes,FMath::Min(OldBytes,NewBytes),Changed&&Apply);
   }CollectGarbage(RF_NoFlags);
  }
  auto Summary=MakeShared<FJsonObject>();Summary->SetArrayField(TEXT("characters"),Rows);Summary->SetNumberField(TEXT("before_snapshot_bytes"),Before);Summary->SetNumberField(TEXT("after_snapshot_bytes"),After);Summary->SetBoolField(TEXT("apply"),Apply);FString Text;FJsonSerializer::Serialize(Summary,TJsonWriterFactory<>::Create(&Text));
  return !Rows.IsEmpty()&&(SelectedPackages.IsEmpty()||Rows.Num()==SelectedPackages.Num())&&FFileHelper::SaveStringToFile(Text,*Report)?0:1;
 }
 FString Verify;if(FParse::Value(*Params,TEXT("Verify="),Verify)){
  auto* Card=LoadObject<UKKImportedCharacter>(nullptr,*Verify);if(!Card||Card->Outfits.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("Saved character unavailable"));return 1;}
  FAssetCompilingManager::Get().FinishAllCompilation();
  for(const auto& O:Card->Outfits)for(const auto& P:O.Parts){FString UVError;if(!P.Model||!CheckUVStreams(P.Model,UVError)){UE_LOG(LogTemp,Error,TEXT("%s"),*UVError);return 1;}}
  int32 Models=0,Skins=0,ColoredSkins=0;int64 Triangles=0,SkinTriangles=0,VerifiedMorphTargets=0,VerifiedMorphSamples=0,VerifiedTinyMorphs=0;TSet<UObject*> Dependencies;Dependencies.Add(Card);
  for(const auto& O:Card->Outfits){if(!O.Skeleton||O.Parts.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("Invalid outfit %d"),O.Index);return 1;}Dependencies.Add(O.Skeleton);
   for(const auto& P:O.Parts){if(!P.Model||!P.Model->GetMeshDescription(0)||!P.Model->GetRenderData()||P.Transform.ContainsNaN()){UE_LOG(LogTemp,Error,TEXT("Invalid saved model: %s desc=%d render=%d finite=%d"),*P.Name,P.Model&&P.Model->GetMeshDescription(0)!=nullptr,P.Model&&P.Model->GetRenderData()!=nullptr,!P.Transform.ContainsNaN());return 1;}
    const auto* Desc=P.Model->GetMeshDescription(0);if(Desc->Triangles().Num()==0){UE_LOG(LogTemp,Error,TEXT("Empty saved triangles %s"),*P.Name);return 1;}Triangles+=Desc->Triangles().Num();++Models;Dependencies.Add(P.Model);
    for(const auto& Mat:P.Model->GetStaticMaterials()){if(!Mat.MaterialInterface){UE_LOG(LogTemp,Error,TEXT("Missing saved material %s"),*P.Name);return 1;}Dependencies.Add(Mat.MaterialInterface);}
    if(P.SkinnedModel){if(!P.SkinnedModel->GetMeshDescription(0)||P.SkinnedModel->GetRefSkeleton().GetNum()==0||P.SkinnedModel->GetSkeleton()!=O.Skeleton||!P.SkinnedModel->GetResourceForRendering()||P.SkinnedModel->GetResourceForRendering()->LODRenderData.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("Invalid saved skin: %s desc=%d bones=%d skeleton=%d render=%d"),*P.SkinnedModel->GetPathName(),P.SkinnedModel->GetMeshDescription(0)!=nullptr,P.SkinnedModel->GetRefSkeleton().GetNum(),P.SkinnedModel->GetSkeleton()==O.Skeleton,P.SkinnedModel->GetResourceForRendering()?P.SkinnedModel->GetResourceForRendering()->LODRenderData.Num():-1);return 1;}++Skins;Dependencies.Add(P.SkinnedModel);}
   }
  }
  FString Error;int32 Textures=0,Materials=0,NativeChains=0,SourceMips=0;TArray<UObject*> Queue=Dependencies.Array();
  for(int32 I=0;I<Queue.Num();++I){auto* Asset=Queue[I];if(!CheckReferences(Asset,Error)){UE_LOG(LogTemp,Error,TEXT("%s"),*Error);return 1;}
   if(auto* Tex=Cast<UTexture2D>(Asset)){if(!Tex->Source.IsValid()){UE_LOG(LogTemp,Error,TEXT("Invalid saved texture source: %s"),*Tex->GetPathName());return 1;}++Textures;SourceMips+=Tex->Source.GetNumMips();if(Tex->Source.GetNumMips()>1)++NativeChains;
    if(FParse::Param(*Params,TEXT("VerifyNativeMips"))&&(Tex->Source.GetSizeX()>1||Tex->Source.GetSizeY()>1)&&Tex->GetOutermost()->GetName().StartsWith(FPackageName::GetLongPackagePath(Card->GetOutermost()->GetName())/TEXT("Resources/T_"))){
     const auto Policy=Tex->Source.GetNumMips()==1?TMGS_NoMipmaps:TMGS_LeaveExistingMips;
     if(Tex->MipGenSettings!=Policy){UE_LOG(LogTemp,Error,TEXT("Saved texture regenerates source mips; reimport required: %s"),*Tex->GetPathName());return 1;}}}
   if(Cast<UMaterialInterface>(Asset))++Materials;
   TArray<UObject*> Owned;GetObjectsWithOuter(Asset,Owned,EGetObjectsFlags::IncludeNestedObjects);Owned.Add(Asset);
   TArray<UObject*> Refs;FReferenceFinder Finder(Refs,nullptr,false,true,false,true);for(auto* Object:Owned)Finder.FindReferences(Object);
   for(auto* Ref:Refs)if(Ref&&(Cast<UTexture>(Ref)||Cast<UMaterialInterface>(Ref))&&!Dependencies.Contains(Ref)){Dependencies.Add(Ref);Queue.Add(Ref);}
  }
  if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
  for(auto* Asset:Queue)if(auto* Mat=Cast<UMaterialInterface>(Asset)){const auto* Resource=Mat->GetMaterialResource(GMaxRHIShaderPlatform);if(Resource&&!Resource->GetCompileErrors().IsEmpty()){for(const auto& Message:Resource->GetCompileErrors())UE_LOG(LogTemp,Error,TEXT("Material %s: %s"),*Mat->GetPathName(),*Message);return 1;}}
  for(const auto& O:Card->Outfits)for(const auto& P:O.Parts)if(P.SkinnedModel){const auto* Desc=P.SkinnedModel->GetMeshDescription(0);FSkeletalMeshConstAttributes A(*Desc);const auto Weights=A.GetVertexSkinWeights();
   if(FParse::Param(*Params,TEXT("VerifyMorphs")))if(!CheckSkinMorphs(P.SkinnedModel,Error,VerifiedMorphTargets,VerifiedMorphSamples,VerifiedTinyMorphs)){UE_LOG(LogTemp,Error,TEXT("%s"),*Error);return 1;}
   uint32 RenderTriangles=0;for(const auto& Section:P.SkinnedModel->GetResourceForRendering()->LODRenderData[0].RenderSections)RenderTriangles+=Section.NumTriangles;
   if(RenderTriangles!=RenderableSkinTriangles(*Desc)){UE_LOG(LogTemp,Error,TEXT("Source skin triangles reduced: %s source=%d renderable=%u render=%u"),*P.SkinnedModel->GetPathName(),Desc->Triangles().Num(),RenderableSkinTriangles(*Desc),RenderTriangles);return 1;}SkinTriangles+=RenderTriangles;
   if(!CheckSkinColors(P.SkinnedModel,Error)){UE_LOG(LogTemp,Error,TEXT("%s"),*Error);return 1;}if(P.SkinnedModel->GetHasVertexColors())++ColoredSkins;
   for(auto V:Desc->Vertices().GetElementIDs()){if(A.GetVertexPositions()[V].ContainsNaN()||Weights.Get(V).Num()==0){UE_LOG(LogTemp,Error,TEXT("Invalid skin position/weight %s"),*P.Name);return 1;}
    for(const auto& Weight:Weights.Get(V))if(Weight.GetBoneIndex()>=P.SkinnedModel->GetRefSkeleton().GetNum()){UE_LOG(LogTemp,Error,TEXT("Invalid skin bone index %s"),*P.Name);return 1;}}}
  const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
  auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
  auto* Actor=World->SpawnActor<AKKImportedCharacterActor>();Actor->Character=Card;
  for(const auto& O:Card->Outfits){Actor->Outfit=O.Index;Actor->OnConstruction(FTransform::Identity);Actor->OnConstruction(FTransform::Identity);
   TArray<UStaticMeshComponent*> Components;Actor->GetComponents(Components);if(Components.Num()!=O.Parts.Num()){UE_LOG(LogTemp,Error,TEXT("Construction mismatch outfit=%d expected=%d actual=%d"),O.Index,O.Parts.Num(),Components.Num());World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return 1;}}
  World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
  if(FParse::Param(*Params,TEXT("VerifyMorphs")))UE_LOG(LogTemp,Display,TEXT("Independent morph verified targets=%lld samples=%lld ue_nearly_zero_samples=%lld"),VerifiedMorphTargets,VerifiedMorphSamples,VerifiedTinyMorphs);
  UE_LOG(LogTemp,Display,TEXT("Independent assets verified outfits=%d models=%d skins=%d materials=%d textures=%d triangles=%lld construction=passed no_card_or_game=1 uv_streams=preserved native_mip_chains=%d source_mips=%d skin_vertex_colors=preserved colored_skins=%d skin_triangles=%lld"),Card->Outfits.Num(),Models,Skins,Materials,Textures,Triangles,NativeChains,SourceMips,ColoredSkins,SkinTriangles);return 0;
 }
 FString Card,Destination,GameRoot;int32 Outfit=-1;
 FKKScopedNativeSkinSource CallerEnvironment(false);
 FParse::Value(*Params,TEXT("GameRoot="),GameRoot);FParse::Value(*Params,TEXT("Card="),Card);FParse::Value(*Params,TEXT("Destination="),Destination);FParse::Value(*Params,TEXT("Outfit="),Outfit);
 if(Card.IsEmpty()||!Destination.StartsWith(TEXT("/Game/"))||!FPackageName::IsValidLongPackageName(Destination)||Outfit < -1){UE_LOG(LogTemp,Error,TEXT("Use -Card=<independent PNG> -Destination=/Game/NewFolder [-Outfit=N]"));return 1;}
 FString Error;if(!OKValidateImportDestination(Destination,Error)){UE_LOG(LogTemp,Error,TEXT("%s"),*Error);return 1;}
 if(GameRoot.IsEmpty()&&!OKResolveCardSource(Card,GameRoot,Error)){UE_LOG(LogTemp,Error,TEXT("Card source failed: %s"),*Error);return 1;}
 FOkSession Session;if(!Session.Open(GameRoot,Card,FMath::Max(0,Outfit),Error)||(GameRoot.IsEmpty()&&!Session.IsPortable())){UE_LOG(LogTemp,Error,TEXT("Independent card required: %s"),*Error);return 1;}
 const int32 Count=Session.CoordinateCount();if(Outfit>=Count)return 1;const bool Exact=!Session.IsPortable()&&!FParse::Param(*Params,TEXT("LegacySkin"));Session.Close();
 FCardAssetImport Import(Destination);const FString Name=TEXT("Character");auto* Manifest=NewObject<UKKImportedCharacter>(CreatePackage(*(Destination/Name)),*Name,RF_Public|RF_Standalone);Import.Assets.Add(Manifest);
 Manifest->Schema=Exact?2:1;TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Card))return 1;uint8 Hash[20];FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Hash);Manifest->SourceHash=BytesToHex(Hash,20);
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 const FString Previous=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_CPU_SKIN"));FPlatformMisc::SetEnvironmentVar(TEXT("OK_CPU_SKIN"),TEXT("1"));
 bool Good=true;for(int32 I=Outfit<0?0:Outfit;I<(Outfit<0?Count:Outfit+1);++I){auto* Actor=World->SpawnActor<AKKPreviewActor>();
  if(!Actor->Session.Open(GameRoot,Card,I,Error)){Import.Error=Error;Good=false;break;}
  // Rigid geometry is placed when the card opens, before any animation. Its
  // inverse bind must use that same placement, not the first animated pose.
  TMap<int32,TArray<float>> RigidPlacement;
  if(Exact)for(int32 PartIndex=0;PartIndex<Actor->Session.PartCount();++PartIndex){OkMeshView View{};if(!Actor->Session.PartAt(PartIndex,View)){Good=false;break;}if(View.rigid_follow_bone&&View.rigid_follow_bone[0]){TArray<float> Matrix;Matrix.SetNum(16);if(!Actor->Session.BoneWorldMatrix(UTF8_TO_TCHAR(View.rigid_follow_bone),Matrix.GetData())){Good=false;break;}RigidPlacement.Add(PartIndex,MoveTemp(Matrix));}}
  if(!Good||!Actor->Session.ApplyAnimFrame(0.f,0.f)||!Actor->Session.Skin()||!Actor->LoadSession(Error)){Import.Error=Error;Good=false;break;}
  Actor->SetActorTickEnabled(false);FKKImportedOutfit O;O.Index=I;
  if(Exact&&!Actor->Session.ExportAnimationSnapshot(O.AnimationSnapshot)){Import.Error=TEXT("Cannot export independent animation rig snapshot");Good=false;break;}
  auto* SourceSkeleton=OKBuildSharedSkeleton(Actor->Session,Actor);if(!SourceSkeleton){Import.Error=TEXT("Missing skeleton");Good=false;break;}
  O.Skeleton=Import.Asset<USkeleton>(TEXT("Skeleton"));
  if(Exact){FReferenceSkeleton Ref;{FReferenceSkeletonModifier Modifier(Ref,O.Skeleton);Modifier.Add(FMeshBoneInfo(TEXT("__ok_scene"),TEXT("__ok_scene"),INDEX_NONE),FTransform::Identity);
   const auto& Native=SourceSkeleton->GetReferenceSkeleton();for(int32 B=0;B<Native.GetNum();++B){const FName NativeName=Native.GetBoneName(B);if(NativeName.ToString().StartsWith(TEXT("__ok_"))){Import.Error=TEXT("Reserved export bone namespace");Good=false;break;}
    Modifier.Add(FMeshBoneInfo(NativeName,NativeName.ToString(),Native.GetParentIndex(B)<0?0:Native.GetParentIndex(B)+1),Native.GetRefBonePose()[B]);}
   for(const auto& Part:Actor->Parts){if(!Good||!ImportablePart(Actor,Part))continue;OkMeshView View{};if(!Actor->Session.PartAt(Part.SessionIndex,View)){Good=false;Import.Error=TEXT("Missing source part");break;}
    FKKImportedPart P;P.Name=Part.Comp->GetName();P.Role=Part.Role;P.Transform=Part.Comp->GetRelativeTransform();P.NativePartIndex=Part.SessionIndex;P.NativeRenderer=View.name?UTF8_TO_TCHAR(View.name):TEXT("");P.SkinSourceHash=Actor->Session.SkinSourceHash(Part.SessionIndex);
    P.SkinSourceHashV2=Actor->Session.SkinSourceHashV2(Part.SessionIndex);ImportMouthMetadata(Actor,Part,P);
    if(!P.Transform.Equals(FTransform::Identity,.0001)||P.SkinSourceHash.IsEmpty()||P.SkinSourceHashV2.IsEmpty()){Good=false;Import.Error=TEXT("Unresolved source transform/hash: ")+P.Name;break;}
    OkSkinPaletteView Raw{};TArray<float> Matrices;const int32 CountSlots=Actor->Session.SkinPalette(Part.SessionIndex,Raw,Matrices);
    if(CountSlots>0){TSet<int32> Used;for(int32 V=0;V<Raw.n_verts;++V){if(OKHasSignedSkinVertex(Raw,V))Used.Add(-V-1);else for(int32 K=0;K<4;++K)if(Raw.weights[V*4+K]>=1e-6)Used.Add(Raw.bone_idx[V*4+K]);}P.PaletteSlots=Used.Array();P.PaletteSlots.Sort();}
    else{P.RigidFollow=View.rigid_follow_bone?UTF8_TO_TCHAR(View.rigid_follow_bone):TEXT("");
     const auto* Placement=RigidPlacement.Find(Part.SessionIndex);if(P.RigidFollow.IsEmpty()||!Placement){Good=false;Import.Error=TEXT("Renderer has neither exact skin nor authored rigid socket: ")+P.NativeRenderer;break;}P.RigidReference=*Placement;P.PaletteSlots.Add(0);}
    if(Ref.GetRawBoneNum()+P.PaletteSlots.Num()*3>65535){Good=false;Import.Error=TEXT("Character rig exceeds 16bit bone index range");break;}
    for(int32 Slot:P.PaletteSlots)for(int32 Axis=0;Axis<3;++Axis){const FName PaletteName=OKPaletteBoneName(Part.SessionIndex,Slot,Axis);Modifier.Add(FMeshBoneInfo(PaletteName,PaletteName.ToString(),0),FTransform::Identity);}O.Parts.Add(MoveTemp(P));
   }
   } // The modifier rebuilds the full reference skeleton on destruction, before mesh merging.
   if(Good){auto* Dummy=NewObject<USkeletalMesh>(Actor);Dummy->SetRefSkeleton(Ref);O.Skeleton->MergeAllBonesToBoneTree(Dummy);
    for(auto& P:O.Parts){const auto* Source=Actor->Parts.FindByPredicate([&](const auto& Part){return Part.Comp&&Part.Comp->GetName()==P.Name;});P.Model=Import.Static(Source->Comp);P.SkinnedModel=Import.ExactSkeletal(Actor->Session,*Source,P,O.Skeleton);if(!P.Model||!P.SkinnedModel||!Import.Error.IsEmpty()){Good=false;break;}}
   }
  }else O.Skeleton->MergeAllBonesToBoneTree(OKBuildDummyLeaderMesh(SourceSkeleton,Actor));
  if(!Exact)
  for(const auto& Part:Actor->Parts){if(!ImportablePart(Actor,Part))continue;
   FKKImportedPart P;P.Name=Part.Comp->GetName();P.Role=Part.Role;P.Transform=Part.Comp->GetRelativeTransform();ImportMouthMetadata(Actor,Part,P);P.Model=Import.Static(Part.Comp);
   if(!P.Model){Good=false;break;}P.SkinnedModel=Import.Skeletal(Actor->Session,Part.SessionIndex,O.Skeleton,P.Model->GetMaterial(0));
   if(!Import.Error.IsEmpty()){Good=false;break;}O.Parts.Add(P);
  }UE_LOG(LogTemp,Display,TEXT("Independent import outfit=%d parts=%d assets=%d"),I,O.Parts.Num(),Import.Assets.Num());Manifest->Outfits.Add(MoveTemp(O));
  // Destroy marks an actor for GC; it does not immediately release its Rust
  // session, preview textures or materials. Retain exported assets explicitly
  // while releasing each outfit's source before loading the next one.
  Actor->Session.Close();Actor->Destroy();Import.Copies.Reset();
  FAssetCompilingManager::Get().FinishAllCompilation();CollectGarbage(RF_NoFlags);
  UE_LOG(LogTemp,Display,TEXT("Independent import source released outfit=%d memory_mb=%.1f"),I,double(FPlatformMemory::GetStats().UsedPhysical)/(1024.*1024.));
  if(!Good)break;
 }
 FPlatformMisc::SetEnvironmentVar(TEXT("OK_CPU_SKIN"),*Previous);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 if(Good)Good=Import.Save();if(!Good){UE_LOG(LogTemp,Error,TEXT("Independent import failed: %s"),*Import.Error);return 1;}
 UE_LOG(LogTemp,Display,TEXT("Independent import saved %s.Character outfits=%d assets=%d game_root=%s"),*(Destination/TEXT("Character")),Manifest->Outfits.Num(),Import.Assets.Num(),GameRoot.IsEmpty()?TEXT("empty"):TEXT("provided"));return 0;
#else
 return 1;
#endif
}
