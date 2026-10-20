// Runtime primitive grouping retains sections and original shader resources.
// Native merge is only a layout builder: restore precise source vertex/weight
// payloads before registering a component, then audit every section.
namespace {
int32 CrowdPartSort(const FKKImportedPart& Part){
 const FString Layer=Part.NativeRenderer.ToLower();
 if(Layer.Contains(TEXT("hitomi")))return Layer.Contains(TEXT("02"))?102:101;
 if(Layer.Contains(TEXT("eyeline")))return Layer.Contains(TEXT("low"))?105:(Layer.EndsWith(TEXT("_kage"))?103:104);
 if(Layer.Contains(TEXT("mayuge")))return 106;if(Layer.Contains(TEXT("noseline")))return 107;return 0;
}
bool CrowdMergePartSafe(const FKKImportedPart& Part,USkeletalMesh* Leader){
 auto* Mesh=Part.SkinnedModel.Get();const auto* R=Mesh?Mesh->GetResourceForRendering():nullptr;
 if(!R||Mesh->GetSkeleton()!=Leader->GetSkeleton()||R->LODRenderData.Num()!=1||Part.MouthHideWhenClosed||!Part.MouthDefaultVisible||!Part.MouthPatterns.IsEmpty()||!Mesh->GetMorphTargets().IsEmpty()||!static_cast<const USkeletalMesh*>(Mesh)->GetMeshClothingAssets().IsEmpty())return false;
 const auto& L=R->LODRenderData[0];for(const auto& Section:L.RenderSections)if(Section.bRecomputeTangent)return false;const auto& V=L.StaticVertexBuffers;const auto* W=L.GetSkinWeightVertexBuffer();
 if(!L.GetNumVertices()||(W&&W->GetMaxBoneInfluences()>MAX_TOTAL_INFLUENCES))return false;
 if(!V.PositionVertexBuffer.GetVertexData()||!V.StaticMeshVertexBuffer.GetTangentData()||!V.StaticMeshVertexBuffer.GetTexCoordData()||!W||!W->GetDataVertexBuffer()->GetWeightData()||(W->GetVariableBonesPerVertex()&&!W->GetLookupVertexBuffer()->GetLookupData())||!L.MultiSizeIndexContainer.GetIndexBuffer())return false;
 if(V.ColorVertexBuffer.GetNumVertices()&&!V.ColorVertexBuffer.GetVertexData())return false;
 for(const auto& M:Mesh->GetMaterials())if(!M.MaterialInterface||IsTranslucentBlendMode(M.MaterialInterface->GetBlendMode()))return false;
 const auto& A=Mesh->GetRefSkeleton();const auto& B=Leader->GetRefSkeleton();
 for(int32 Bone=0;Bone<A.GetNum();++Bone){const int32 Other=B.FindBoneIndex(A.GetBoneName(Bone));if(Other==INDEX_NONE||!A.GetRefBonePose()[Bone].Equals(B.GetRefBonePose()[Other],0.))return false;
  const int32 AP=A.GetParentIndex(Bone),BP=B.GetParentIndex(Other);if((AP==INDEX_NONE)!=(BP==INDEX_NONE)||(AP!=INDEX_NONE&&A.GetBoneName(AP)!=B.GetBoneName(BP)))return false;
 }
 return true;
}
bool CrowdMergeFormatsEqual(const FKKImportedPart& A,const FKKImportedPart& B){
 const auto& X=A.SkinnedModel->GetResourceForRendering()->LODRenderData[0];const auto& Y=B.SkinnedModel->GetResourceForRendering()->LODRenderData[0];
 return CrowdPartSort(A)==CrowdPartSort(B)&&A.Transform.Equals(B.Transform,0.)&&X.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords()==Y.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords()&&X.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseHighPrecisionTangentBasis()==Y.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseHighPrecisionTangentBasis()&&X.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs()==Y.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs();
}
USkeletalMesh* CrowdMergeExact(AOKCrowdRenderActor* Actor,const TArray<FKKImportedPart>& Parts,FString& Error){
 TArray<USkeletalMesh*> Meshes;TArray<FSkelMeshMergeSectionMapping> Mappings;int32 Sections=0;int64 Triangles=0;
 for(const auto& P:Parts){auto* M=P.SkinnedModel.Get();Meshes.Add(M);FSkelMeshMergeSectionMapping Map;for(const auto& S:M->GetResourceForRendering()->LODRenderData[0].RenderSections){Map.SectionIDs.Add(Sections++);Triangles+=S.NumTriangles;}Mappings.Add(MoveTemp(Map));}
 auto* Mesh=NewObject<USkeletalMesh>(Actor);TStrongObjectPtr<USkeletalMesh> Hold(Mesh);Mesh->SetSkeleton(Meshes[0]->GetSkeleton());
 FSkeletalMeshMerge Merge(Mesh,Meshes,Mappings,0,EMeshBufferAccess::ForceCPUAndGPU);if(!Merge.DoMerge()){Error=TEXT("Native layout merge rejected");return nullptr;}
 Mesh->ReleaseResources();Mesh->ReleaseResourcesFence.Wait();
 auto& D=Mesh->GetResourceForRendering()->LODRenderData[0];auto& VB=D.StaticVertexBuffers.StaticMeshVertexBuffer;
 const auto& Format=Meshes[0]->GetResourceForRendering()->LODRenderData[0].StaticVertexBuffers.StaticMeshVertexBuffer;
 if(D.RenderSections.Num()!=Sections){Error=TEXT("Section count changed");return nullptr;}
 VB.SetUseHighPrecisionTangentBasis(Format.GetUseHighPrecisionTangentBasis());VB.SetUseFullPrecisionUVs(Format.GetUseFullPrecisionUVs());VB.Init(D.GetNumVertices(),Format.GetNumTexCoords(),true);
 TArray<FSkinWeightInfo> Weights;Weights.SetNumZeroed(D.GetNumVertices());bool Weight16=false,Index16=false;uint32 MaxInfluences=0;int32 SectionIndex=0;int64 Compared=0,ActualTriangles=0;
 for(auto* Source:Meshes){const auto& S=Source->GetResourceForRendering()->LODRenderData[0];const auto& SV=S.StaticVertexBuffers.StaticMeshVertexBuffer;const auto* SW=S.GetSkinWeightVertexBuffer();
  Weight16|=SW->GetBoneWeightByteSize()==2;Index16|=SW->GetBoneIndexByteSize()==2;MaxInfluences=FMath::Max(MaxInfluences,SW->GetMaxBoneInfluences());
  const uint32 TangentStride=SV.GetTangentSize()/SV.GetNumVertices(),UVStride=SV.GetTexCoordSize()/SV.GetNumVertices();
  if(!SV.GetNumVertices()||TangentStride!=uint32(VB.GetTangentSize()/VB.GetNumVertices())||UVStride!=uint32(VB.GetTexCoordSize()/VB.GetNumVertices())){Error=TEXT("Vertex precision changed");return nullptr;}
  for(const auto& SS:S.RenderSections){auto& DS=D.RenderSections[SectionIndex++];ActualTriangles+=DS.NumTriangles;
   if(DS.NumTriangles!=SS.NumTriangles||DS.NumVertices!=SS.NumVertices||Mesh->GetMaterials()[DS.MaterialIndex].MaterialInterface!=Source->GetMaterials()[SS.MaterialIndex].MaterialInterface){Error=TEXT("Section geometry/material changed");return nullptr;}
   DS.bCastShadow=SS.bCastShadow;DS.bRecomputeTangent=SS.bRecomputeTangent;DS.bDisabled=SS.bDisabled;
   for(uint32 V=0;V<SS.NumVertices;++V){const uint32 A=SS.BaseVertexIndex+V,B=DS.BaseVertexIndex+V;
    const FVector3f SP=S.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(A),DP=D.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(B);
    if(FMemory::Memcmp(&SP,&DP,sizeof(SP))!=0){Error=TEXT("Vertex position changed");return nullptr;}
    FMemory::Memcpy(static_cast<uint8*>(VB.GetTangentData())+uint64(B)*TangentStride,static_cast<const uint8*>(SV.GetTangentData())+uint64(A)*TangentStride,TangentStride);
    FMemory::Memcpy(static_cast<uint8*>(VB.GetTexCoordData())+uint64(B)*UVStride,static_cast<const uint8*>(SV.GetTexCoordData())+uint64(A)*UVStride,UVStride);
    const FColor SC=S.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices()>A?S.StaticVertexBuffers.ColorVertexBuffer.VertexColor(A):FColor::White;
    const FColor DC=D.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices()>B?D.StaticVertexBuffers.ColorVertexBuffer.VertexColor(B):FColor::White;
    if(SC!=DC){Error=TEXT("Vertex color changed");return nullptr;}
    auto W=SW->GetVertexSkinWeights(A);
    for(uint32 I=0;I<MAX_TOTAL_INFLUENCES;++I)if(W.InfluenceWeights[I]){
     if(!SS.BoneMap.IsValidIndex(W.InfluenceBones[I])){Error=TEXT("Source palette index invalid");return nullptr;}
     const FName Name=Source->GetRefSkeleton().GetBoneName(SS.BoneMap[W.InfluenceBones[I]]);const int32 NewBone=Mesh->GetRefSkeleton().FindBoneIndex(Name),Slot=DS.BoneMap.IndexOfByKey(NewBone);
     if(Slot==INDEX_NONE){Error=TEXT("Private palette bone missing");return nullptr;}W.InfluenceBones[I]=Slot;
     if(FMemory::Memcmp(&Source->GetRefBasesInvMatrix()[SS.BoneMap[SW->GetBoneIndex(A,I)]],&Mesh->GetRefBasesInvMatrix()[NewBone],sizeof(FMatrix44f))!=0){Error=TEXT("Private inverse bind changed");return nullptr;}
    }
    Weights[B]=W;++Compared;
   }
   for(uint32 I=0;I<SS.NumTriangles*3;++I)if(S.MultiSizeIndexContainer.GetIndexBuffer()->Get(SS.BaseIndex+I)-SS.BaseVertexIndex!=D.MultiSizeIndexContainer.GetIndexBuffer()->Get(DS.BaseIndex+I)-DS.BaseVertexIndex){Error=TEXT("Triangle connectivity changed");return nullptr;}
  }
 }
 if(ActualTriangles!=Triangles||Compared!=D.GetNumVertices()){Error=TEXT("Merge topology coverage incomplete");return nullptr;}
 auto& DW=D.SkinWeightVertexBuffer;DW.SetMaxBoneInfluences(MaxInfluences);DW.SetUse16BitBoneIndex(Index16);DW.SetUse16BitBoneWeight(Weight16);DW.SetNeedsCPUAccess(true);DW=Weights;
 for(uint32 V=0;V<D.GetNumVertices();++V){const auto Actual=DW.GetVertexSkinWeights(V);for(uint32 I=0;I<MAX_TOTAL_INFLUENCES;++I)if(Actual.InfluenceWeights[I]!=Weights[V].InfluenceWeights[I]||(Actual.InfluenceWeights[I]&&Actual.InfluenceBones[I]!=Weights[V].InfluenceBones[I])){Error=TEXT("Weight encoding changed");return nullptr;}}
 Mesh->InitResources();Actor->GroupedMeshes.Add(Mesh);Actor->GroupedAuditedVertices+=Compared;return Mesh;
}
FKKImportedOutfit CrowdGroupOutfit(AOKCrowdRenderActor* Actor,const FKKImportedOutfit& Original){
 FKKImportedOutfit Result=Original;Actor->GroupedSourceParts=Actor->GroupedResultParts=Original.Parts.Num();
 if(Original.Parts.IsEmpty()||!Original.Parts[0].SkinnedModel)return Result;
 const double Start=FPlatformTime::Seconds();TArray<TArray<int32>> Groups;USkeletalMesh* Leader=Original.Parts[0].SkinnedModel;
 // Retain the original complete pose leader and independently controlled parts.
 for(int32 P=1;P<Original.Parts.Num();++P)if(CrowdMergePartSafe(Original.Parts[P],Leader)){
  auto* Group=Groups.FindByPredicate([&](const auto& G){return CrowdMergeFormatsEqual(Original.Parts[G[0]],Original.Parts[P]);});if(Group)Group->Add(P);else Groups.Add({P});
 }
 TMap<int32,FKKImportedPart> Replacements;TSet<int32> Removed;
 for(const auto& Group:Groups)if(Group.Num()>1){TArray<FKKImportedPart> Parts;for(int32 P:Group)Parts.Add(Original.Parts[P]);FString Error;auto* Mesh=CrowdMergeExact(Actor,Parts,Error);
  if(!Mesh){++Actor->GroupedRejectedGroups;UE_LOG(LogTemp,Warning,TEXT("Crowd primitive merge retained originals: %s"),*Error);continue;}
  auto Part=Parts[0];Part.SkinnedModel=Mesh;Part.Name=TEXT("RuntimeGroupedPrimitives");for(const auto& P:Parts)if(P.Role==TEXT("head")||P.Role.StartsWith(TEXT("hair")))Part.Role=TEXT("head");
  Replacements.Add(Group[0],Part);for(int32 P:Group)if(P!=Group[0])Removed.Add(P);++Actor->GroupedAcceptedGroups;
 }
 Result.Parts.Reset();for(int32 P=0;P<Original.Parts.Num();++P)if(!Removed.Contains(P))Result.Parts.Add(Replacements.Contains(P)?Replacements[P]:Original.Parts[P]);Actor->GroupedResultParts=Result.Parts.Num();Actor->GroupedBuildSeconds=FPlatformTime::Seconds()-Start;return Result;
}
}
