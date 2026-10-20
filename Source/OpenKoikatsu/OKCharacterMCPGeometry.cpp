#include "OKCharacterMCPGeometry.h"
#include "OKPaintSkinning.h"
#include "KKPreviewActor.h"
#include "Components/PoseableMeshComponent.h"
#include "ProceduralMeshComponent.h"

namespace {
using FRows=TArray<TSharedPtr<FJsonValue>>;
FRows Vector(const FVector& V){return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};}
}
TSharedPtr<FJsonObject> OKCharacterMCPGeometry(AKKPreviewActor* Actor,FString& Error)
{
 if(!Actor){Error=TEXT("Character unavailable");return nullptr;}
 Actor->EnsurePrivateGpuQueryGeometry();
 FRows Meshes,Landmarks;FBox Bounds(ForceInit);int32 Count=0,Triangles=0;
 for(const auto& Part:Actor->Parts){
  if(Part.Role!=TEXT("body")&&Part.Role!=TEXT("head"))continue;
  auto Add=[&](const TArray<FVector>& Vertices,const TArray<uint32>& Indices){
   if(Vertices.IsEmpty()||Indices.IsEmpty())return;
   auto Mesh=MakeShared<FJsonObject>();FRows Positions,Faces;
   for(const auto& V:Vertices){if(V.ContainsNaN()){Error=TEXT("Nonfinite posed geometry");return;}Positions.Add(MakeShared<FJsonValueArray>(Vector(V)));Bounds+=V;}
   for(int32 I=0;I+2<Indices.Num();I+=3){if(Indices[I]>=uint32(Vertices.Num())||Indices[I+1]>=uint32(Vertices.Num())||Indices[I+2]>=uint32(Vertices.Num())){Error=TEXT("Invalid mesh triangle index");return;}Faces.Add(MakeShared<FJsonValueArray>(FRows{MakeShared<FJsonValueNumber>(Indices[I]),MakeShared<FJsonValueNumber>(Indices[I+1]),MakeShared<FJsonValueNumber>(Indices[I+2])}));}
   Mesh->SetStringField(TEXT("role"),Part.Role);Mesh->SetNumberField(TEXT("part"),Part.SessionIndex);Mesh->SetArrayField(TEXT("vertices"),Positions);Mesh->SetArrayField(TEXT("triangles"),Faces);Meshes.Add(MakeShared<FJsonValueObject>(Mesh));Count+=Vertices.Num();Triangles+=Indices.Num()/3;
  };
  bool Skinned=false;const FName Tag=*FString::Printf(TEXT("KKSessionPart_%d"),Part.SessionIndex);
  if(!Part.PrivateGpuDisplay)for(auto Skin:Actor->SkelFollowers)if(Skin&&Skin->GetName()==FString::Printf(TEXT("Skel_%d"),Part.SessionIndex)&&Skin->ComponentTags.Contains(Tag)){
   FOKPaintPose Pose;if(!Pose.Prepare(Skin)){Error=TEXT("Current skin geometry has no readable CPU data");return nullptr;}TArray<FVector> Vertices;for(uint32 I=0;I<Pose.LOD->GetNumVertices();++I)Vertices.Add(Pose.Position(I));
   TArray<uint32> Indices;const auto* Buffer=Pose.LOD->MultiSizeIndexContainer.GetIndexBuffer();for(const auto& Section:Pose.LOD->RenderSections)if(!Section.bDisabled)for(uint32 I=0;I<Section.NumTriangles*3;++I)Indices.Add(Buffer->Get(Section.BaseIndex+I));Add(Vertices,Indices);Skinned=true;
  }
  // Section zero is the physical surface. Additional procedural sections are
  // extruded outline/overlay passes, not additional anatomical geometry.
  // Native GPU display can hide its CPU observer component. Garment visibility
  // can also mask skin. Neither changes the underlying anatomical surface.
  if(!Skinned&&Part.Comp)if(const auto* Section=Part.Comp->GetProcMeshSection(0)){TArray<FVector> Vertices;for(const auto& V:Section->ProcVertexBuffer)Vertices.Add(Part.Comp->GetRenderMatrix().TransformPosition(V.Position));Add(Vertices,Section->ProcIndexBuffer);}
  if(!Error.IsEmpty())return nullptr;
 }
 if(!Bounds.IsValid||Meshes.IsEmpty()){Error=TEXT("No body/head anatomical geometry available");return nullptr;}
 if(Actor->BodySkel){auto* Skeleton=Actor->BodySkel.Get();for(int32 I=0;I<Skeleton->GetNumBones();++I){const auto Name=Skeleton->GetBoneName(I);const FVector Position=Skeleton->GetBoneLocation(Name,EBoneSpaces::WorldSpace);if(Position.ContainsNaN())continue;auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("bone"),Name.ToString());Item->SetArrayField(TEXT("position"),Vector(Position));Landmarks.Add(MakeShared<FJsonValueObject>(Item));}}
 auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("unit"),TEXT("centimeters"));R->SetStringField(TEXT("bounds_method"),TEXT("current_posed_triangle_vertices"));R->SetStringField(TEXT("coordinate_system"),TEXT("Unreal world XYZ, Z up; front looks along -Y, side along +X"));R->SetArrayField(TEXT("min"),Vector(Bounds.Min));R->SetArrayField(TEXT("max"),Vector(Bounds.Max));R->SetArrayField(TEXT("extent"),Vector(Bounds.GetSize()));R->SetNumberField(TEXT("vertex_count"),Count);R->SetNumberField(TEXT("triangle_count"),Triangles);R->SetArrayField(TEXT("meshes"),Meshes);R->SetArrayField(TEXT("landmarks"),Landmarks);return R;
}
