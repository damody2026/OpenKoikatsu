// Opt-in ownership for fixed baked clips. Native mesh/material rendering stays
// intact; independent GPU matrices replace pose evaluation, not facial state.
namespace {
TAutoConsoleVariable<int32> CVarGpuEligibilityPass(TEXT("ok.CrowdGpuEligibilityPass"),1,TEXT("0 legacy two-pass eligibility;1 equivalent single traversal and empty-clothing shortcut. Changes live without resetting clocks."),ECVF_Default);
void RestoreGpuClock(FOKCrowdIndependentGpu::FClock& Clock){
 UOKCrowdRenderSkin* Leader=nullptr;
 for(auto& Saved:Clock.Skins)if(auto* Skin=Saved.Key.Get()){
  Skin->GpuAnimationClockOwned=false;Skin->GpuAnimationBoundsRadius=0;
  if(auto* Instance=Skin->GetSingleNodeInstance())if(Instance->GetCurrentAsset()==Clock.Clip.Get())Instance->SetPosition(Clock.Time,false);
  Skin->SetComponentTickEnabled(Saved.Value);Skin->UpdateBounds();Skin->MarkRenderTransformDirty();
  if(!Skin->LeaderPoseComponent.IsValid())Leader=Skin;
 }
 if(Leader){Leader->bRequiredBonesUpToDate=false;Leader->RecalcRequiredBones(0);Leader->RefreshBoneTransforms(nullptr);}
 for(auto& Saved:Clock.Skins)if(auto* Skin=Saved.Key.Get())Skin->MarkRenderDynamicDataDirty();
}
void RestoreAllGpuClocks(FOKCrowdIndependentGpu& S){for(auto& Pair:S.Clocks)RestoreGpuClock(Pair.Value);S.Clocks.Reset();}

// Immutable reach cache follows the actual mesh and active GPU weight resources.
// Cook can discard CPU arrays; acquire missing arrays together once per actor,
// never during ordinary playback. Unsupported layouts retain the coarse bound.
struct FGpuReachCache {
 TWeakObjectPtr<USkeletalMesh> Mesh;const FSkinWeightVertexBuffer* Weights=nullptr;
 const FRHIBuffer* PositionResource=nullptr;const FRHIBuffer* WeightResource=nullptr;const FRHIBuffer* LookupResource=nullptr;
 TArray<double> Reach;double WeightScale=1.;bool Valid=false;
 TArray<uint8> Positions,WeightData,LookupData; // retained only for opt-in visual audit
};
TArray<TSharedPtr<FGpuReachCache>> GpuReachCache;
TSharedPtr<FGpuReachCache> FindGpuReach(USkeletalMeshComponent* Skin){
 auto* Mesh=Skin->GetSkeletalMeshAsset();const auto* Render=Mesh?Mesh->GetResourceForRendering():nullptr;
 if(!Render||Render->LODRenderData.IsEmpty())return nullptr;
 const auto& P=Render->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;const auto* W=Skin->GetSkinWeightBuffer(0);
 if(!W)return nullptr;
 for(const auto& C:GpuReachCache)if(C->Mesh.Get()==Mesh&&C->Weights==W&&C->PositionResource==P.VertexBufferRHI.GetReference()&&C->WeightResource==W->GetDataVertexBuffer()->VertexBufferRHI.GetReference()&&C->LookupResource==W->GetLookupVertexBuffer()->VertexBufferRHI.GetReference())return C;
 return nullptr;
}
uint32 ReadGpuWeightValue(const uint8* P,uint32 Bytes){uint16 V=0;if(Bytes==1)return *P;FMemory::Memcpy(&V,P,2);return V;}
bool BuildGpuReach(USkeletalMeshComponent* Skin,FGpuReachCache& C,const uint8* PositionData,const uint8* WeightData,const uint8* LookupData){
 auto* Mesh=Skin->GetSkeletalMeshAsset();const auto& LOD=Mesh->GetResourceForRendering()->LODRenderData[0];
 const auto& P=LOD.StaticVertexBuffers.PositionVertexBuffer;const auto* W=Skin->GetSkinWeightBuffer(0);
 const auto& Inv=Mesh->GetRefBasesInvMatrix();C.Reach.Init(-1.,Inv.Num());C.WeightScale=1.;TBitArray<> Seen(false,P.GetNumVertices());
 const uint32 Stride=W->GetConstantInfluencesVertexStride(),IndexBytes=W->GetBoneIndexByteSize(),WeightBytes=W->GetBoneWeightByteSize();
 for(const auto& Section:LOD.RenderSections){
  if(uint64(Section.BaseVertexIndex)+Section.NumVertices>P.GetNumVertices())return false;
  for(uint32 V=Section.BaseVertexIndex;V<Section.BaseVertexIndex+Section.NumVertices;++V){
   if(Seen[V])return false;Seen[V]=true;FVector3f Position;FMemory::Memcpy(&Position,PositionData+uint64(V)*P.GetStride(),sizeof(Position));if(Position.ContainsNaN())return false;if(P.GetVertexData()&&!Position.Equals(P.VertexPosition(V),0.f))return false;
   uint64 Total=0;
   uint32 Offset=V*Stride,Count=W->GetMaxBoneInfluences();if(W->GetVariableBonesPerVertex()){if(!LookupData)return false;uint32 Entry;FMemory::Memcpy(&Entry,LookupData+uint64(V)*4,4);Offset=Entry>>8;Count=Entry&255;}
   if(!Count||Count>W->GetMaxBoneInfluences()||uint64(Offset)+uint64(Count)*(IndexBytes+WeightBytes)>W->GetDataVertexBuffer()->GetVertexDataSize())return false;
   for(uint32 I=0;I<Count;++I){const uint8* Vertex=WeightData+Offset;
    uint32 Weight=ReadGpuWeightValue(Vertex+Count*IndexBytes+I*WeightBytes,WeightBytes);if(WeightBytes==1)Weight*=257;if(!Weight)continue;Total+=Weight;
    const uint32 Map=ReadGpuWeightValue(Vertex+I*IndexBytes,IndexBytes);
    if(W->GetDataVertexBuffer()->GetWeightData()&&(!W->GetVariableBonesPerVertex()||W->GetLookupVertexBuffer()->GetLookupData()))if(Map!=W->GetBoneIndex(V,I)||Weight!=W->GetBoneWeight(V,I))return false;
    if(!Section.BoneMap.IsValidIndex(Map))return false;const uint32 Bone=Section.BoneMap[Map];if(!Inv.IsValidIndex(Bone))return false;
    const double Reach=FVector3f(Inv[Bone].TransformPosition(Position)).Size();if(!FMath::IsFinite(Reach))return false;C.Reach[Bone]=FMath::Max(C.Reach[Bone],Reach);
   }
   if(!Total)return false;C.WeightScale=FMath::Max(C.WeightScale,double(Total)/65535.);
  }
 }
 return Seen.CountSetBits()==int32(P.GetNumVertices());
}
void PrepareGpuReach(const TArray<TWeakObjectPtr<USkeletalMeshComponent>>& Skins){
 TRACE_CPUPROFILER_EVENT_SCOPE(CrowdGpuInfluenceBoundsPrepare);
 GpuReachCache.RemoveAll([](const auto& C){return !C->Mesh.IsValid();});
 struct FRead {TSharedPtr<FGpuReachCache> Cache;TWeakObjectPtr<USkeletalMeshComponent> Skin;FBufferRHIRef Position,Weight,Lookup;uint32 PositionBytes=0,WeightBytes=0,LookupBytes=0;TUniquePtr<FRHIGPUBufferReadback> P,W,L;};
 auto Jobs=MakeShared<TArray<TSharedPtr<FRead>>,ESPMode::ThreadSafe>();
 const bool Audit=FParse::Param(FCommandLine::Get(),TEXT("CrowdGpuClockVisualCompare"));const bool ForceReadback=Audit&&FParse::Param(FCommandLine::Get(),TEXT("CrowdGpuBoundsReadbackAudit"));const double Start=FPlatformTime::Seconds();
 for(const auto& Weak:Skins)if(auto* Skin=Weak.Get()){
  if(FindGpuReach(Skin))continue;auto* Mesh=Skin->GetSkeletalMeshAsset();const auto* Render=Mesh?Mesh->GetResourceForRendering():nullptr;if(!Render||Render->LODRenderData.IsEmpty())continue;
  const auto& P=Render->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;const auto* W=Skin->GetSkinWeightBuffer(0);
  if(!W)continue;if(P.GetStride()!=sizeof(FVector3f)||W->GetNumVertices()!=P.GetNumVertices()||!P.GetNumVertices()){UE_LOG(LogTemp,Display,TEXT("GPU reach unsupported mesh=%s variable=%d stride=%u vertices=%u weights=%u"),*Mesh->GetPathName(),W->GetVariableBonesPerVertex(),P.GetStride(),P.GetNumVertices(),W->GetNumVertices());continue;}
  const uint64 PB=uint64(P.GetNumVertices())*P.GetStride(),WB=W->GetDataVertexBuffer()->GetVertexDataSize();
  if(PB>MAX_int32||WB>MAX_int32||!P.VertexBufferRHI||!W->GetDataVertexBuffer()->VertexBufferRHI||PB>P.VertexBufferRHI->GetSize()||WB>W->GetDataVertexBuffer()->VertexBufferRHI->GetSize()){UE_LOG(LogTemp,Display,TEXT("GPU reach missing resource mesh=%s position_bytes=%llu weight_bytes=%llu pos_resource=%d weight_resource=%d"),*Mesh->GetPathName(),PB,WB,bool(P.VertexBufferRHI),bool(W->GetDataVertexBuffer()->VertexBufferRHI));continue;}
  auto C=MakeShared<FGpuReachCache>();C->Mesh=Mesh;C->Weights=W;C->PositionResource=P.VertexBufferRHI.GetReference();C->WeightResource=W->GetDataVertexBuffer()->VertexBufferRHI.GetReference();C->LookupResource=W->GetLookupVertexBuffer()->VertexBufferRHI.GetReference();GpuReachCache.Add(C);
  if(!ForceReadback&&P.GetVertexData()&&W->GetDataVertexBuffer()->GetWeightData()&&(!W->GetVariableBonesPerVertex()||W->GetLookupVertexBuffer()->GetLookupData())){
   C->Valid=BuildGpuReach(Skin,*C,static_cast<const uint8*>(P.GetVertexData()),W->GetDataVertexBuffer()->GetWeightData(),W->GetLookupVertexBuffer()->GetLookupData());continue;
  }
  auto J=MakeShared<FRead>();J->Cache=C;J->Skin=Skin;J->Position=P.VertexBufferRHI;J->Weight=W->GetDataVertexBuffer()->VertexBufferRHI;J->PositionBytes=uint32(PB);J->WeightBytes=uint32(WB);if(W->GetVariableBonesPerVertex()){J->Lookup=W->GetLookupVertexBuffer()->VertexBufferRHI;J->LookupBytes=W->GetLookupVertexBuffer()->GetVertexDataSize();if(!J->Lookup||J->LookupBytes!=W->GetNumVertices()*4||J->LookupBytes>J->Lookup->GetSize())continue;}Jobs->Add(J);
 }
 if(Jobs->IsEmpty())return;
 ENQUEUE_RENDER_COMMAND(CrowdReadInfluenceGeometry)([Jobs](FRHICommandListImmediate& Cmd){
  for(const auto& J:*Jobs){J->P=MakeUnique<FRHIGPUBufferReadback>(TEXT("Crowd.Bounds.Positions"));J->W=MakeUnique<FRHIGPUBufferReadback>(TEXT("Crowd.Bounds.Weights"));
   Cmd.Transition(FRHITransitionInfo(J->Position,ERHIAccess::Unknown,ERHIAccess::CopySrc));Cmd.Transition(FRHITransitionInfo(J->Weight,ERHIAccess::Unknown,ERHIAccess::CopySrc));
   J->P->EnqueueCopy(Cmd,J->Position,J->PositionBytes);J->W->EnqueueCopy(Cmd,J->Weight,J->WeightBytes);
   if(J->Lookup){J->L=MakeUnique<FRHIGPUBufferReadback>(TEXT("Crowd.Bounds.Lookup"));Cmd.Transition(FRHITransitionInfo(J->Lookup,ERHIAccess::Unknown,ERHIAccess::CopySrc));J->L->EnqueueCopy(Cmd,J->Lookup,J->LookupBytes);Cmd.Transition(FRHITransitionInfo(J->Lookup,ERHIAccess::CopySrc,ERHIAccess::SRVMask));}
   Cmd.Transition(FRHITransitionInfo(J->Position,ERHIAccess::CopySrc,ERHIAccess::VertexOrIndexBuffer|ERHIAccess::SRVMask));Cmd.Transition(FRHITransitionInfo(J->Weight,ERHIAccess::CopySrc,ERHIAccess::VertexOrIndexBuffer|ERHIAccess::SRVMask));
  }
 });FlushRenderingCommands();
 // Poll fences in a bounded initialization path; no GPU-wide idle or frame-loop readback.
 bool Ready=false;while(FPlatformTime::Seconds()-Start<10.){
  ENQUEUE_RENDER_COMMAND(CrowdPollInfluenceGeometry)([Jobs,&Ready](FRHICommandListImmediate&){Ready=true;for(const auto& J:*Jobs)Ready&=J->P->IsReady()&&J->W->IsReady()&&(!J->L||J->L->IsReady());});FlushRenderingCommands();
  if(Ready)break;FPlatformProcess::Sleep(.001f);
 }
 if(Ready){ENQUEUE_RENDER_COMMAND(CrowdCopyInfluenceGeometry)([Jobs](FRHICommandListImmediate&){for(const auto& J:*Jobs){
  const void* P=J->P->Lock(J->PositionBytes);const void* W=J->W->Lock(J->WeightBytes);
  if(P&&W){J->Cache->Positions.SetNumUninitialized(J->PositionBytes);J->Cache->WeightData.SetNumUninitialized(J->WeightBytes);FMemory::Memcpy(J->Cache->Positions.GetData(),P,J->PositionBytes);FMemory::Memcpy(J->Cache->WeightData.GetData(),W,J->WeightBytes);}
  J->P->Unlock();J->W->Unlock();if(J->L){const void* L=J->L->Lock(J->LookupBytes);if(L){J->Cache->LookupData.SetNumUninitialized(J->LookupBytes);FMemory::Memcpy(J->Cache->LookupData.GetData(),L,J->LookupBytes);}J->L->Unlock();}
 }});FlushRenderingCommands();}
 for(const auto& J:*Jobs){if(auto* Skin=J->Skin.Get())if(!J->Cache->Positions.IsEmpty()&&!J->Cache->WeightData.IsEmpty())J->Cache->Valid=BuildGpuReach(Skin,*J->Cache,J->Cache->Positions.GetData(),J->Cache->WeightData.GetData(),J->Cache->LookupData.GetData());if(!Audit){J->Cache->Positions.Empty();J->Cache->WeightData.Empty();J->Cache->LookupData.Empty();}}
 ENQUEUE_RENDER_COMMAND(CrowdReleaseInfluenceReadback)([Jobs](FRHICommandListImmediate&){for(const auto& J:*Jobs){J->P.Reset();J->W.Reset();J->L.Reset();}});FlushRenderingCommands();
 UE_LOG(LogTemp,Display,TEXT("GPU influence readback parts=%d ready=%d seconds=%.6f"),Jobs->Num(),Ready,FPlatformTime::Seconds()-Start);
}
bool GpuInfluenceReach(USkeletalMeshComponent* Skin,TArray<double>& Reach,double& WeightScale){
 const auto C=FindGpuReach(Skin);if(!C||!C->Valid)return false;Reach=C->Reach;WeightScale=C->WeightScale;return true;
}

// Conservative radius for ANY interpolation of the active clip. Translation
// length and scale operator norms are bounded over all keys and propagated down
// the private hierarchy; inverse-bind corners bound the native skin inputs.
bool GpuClockBounds(const FOKGpuAnimationBundle& Bundle,const FOKGpuActorRecord& Actor,int32 Clip,
 const TArray<TWeakObjectPtr<USkeletalMeshComponent>>& Skins,TArray<float>& PartBounds,float& Out){
 if(!Actor.Clips.IsValidIndex(Clip)||!Actor.PoseCount)return false;PrepareGpuReach(Skins);
 TMap<FName,int32> PoseIndices;PoseIndices.Reserve(Actor.PoseBones.Num());
 for(int32 Bone=0;Bone<Actor.PoseBones.Num();++Bone)PoseIndices.Add(FName(*Actor.PoseBones[Bone].Name),Bone);
 const auto& Data=Bundle.Data.Pose;TArray<double> Radii,Scales;Radii.SetNum(Actor.PoseCount);Scales.SetNum(Actor.PoseCount);
 const uint32 Base=Actor.Clips[Clip].TrackOffset;
 for(uint32 Bone=0;Bone<Actor.PoseCount;++Bone){
  if(!Data.Tracks.IsValidIndex(Base+Bone))return false;const auto& Track=Data.Tracks[Base+Bone];double T=0,S=0;
  for(uint32 Key=0;Key<Track.Y;++Key){const uint64 I=uint64(Track.X+Key)*3;if(I+2>=uint64(Data.Keys.Num()))return false;
   const auto& P=Data.Keys[I];const auto& Scale=Data.Keys[I+2];T=FMath::Max(T,FVector3f(P.X,P.Y,P.Z).Size());S=FMath::Max(S,double(FMath::Max3(FMath::Abs(Scale.X),FMath::Abs(Scale.Y),FMath::Abs(Scale.Z))));
  }
  if(Track.Z==MAX_uint32){Radii[Bone]=T;Scales[Bone]=S;}
  else{if(Track.Z>=Bone)return false;Radii[Bone]=Radii[Track.Z]+Scales[Track.Z]*T;Scales[Bone]=Scales[Track.Z]*S;}
  if(!FMath::IsFinite(Radii[Bone])||!FMath::IsFinite(Scales[Bone]))return false;
 }
 double Radius=0;PartBounds.Reset();PartBounds.Reserve(Skins.Num());uint32 TightParts=0;
 for(const auto& Weak:Skins){auto* Skin=Weak.Get();auto* Mesh=Skin?Skin->GetSkeletalMeshAsset():nullptr;if(!Mesh)return false;
  double PartRadius=10;
  const FBox Box=Mesh->GetImportedBounds().GetBox();const auto& Ref=Mesh->GetRefSkeleton();const auto& Inverse=Mesh->GetRefBasesInvMatrix();
  // Unweighted hierarchy ancestors cannot move a rendered vertex directly.
  // Their motion remains in Radii/Scales, but their inverse-bind boxes must not
  // inflate the character bound. Section palettes cover every LOD0 influence.
  const auto* Render=Mesh->GetResourceForRendering();if(!Render||Render->LODRenderData.IsEmpty())return false;
  TSet<int32> Weighted;for(const auto& Section:Render->LODRenderData[0].RenderSections)for(uint16 Bone:Section.BoneMap)Weighted.Add(Bone);
  if(Weighted.IsEmpty())return false;
  TArray<double> InfluenceReach;double WeightScale=1.;const bool Tight=GpuInfluenceReach(Skin,InfluenceReach,WeightScale);TightParts+=Tight?1:0;
  for(int32 B:Weighted){if(B<0||B>=Ref.GetNum())return false;const int32* PoseIndex=PoseIndices.Find(Ref.GetBoneName(B));if(!PoseIndex||!Inverse.IsValidIndex(B))return false;const int32 Pose=*PoseIndex;
   if(Tight&&InfluenceReach[B]<0)continue;
   double Reach=Tight?InfluenceReach[B]:0.;if(!Tight)for(int32 Corner=0;Corner<8;++Corner){const FVector3f P(Corner&1?Box.Max.X:Box.Min.X,Corner&2?Box.Max.Y:Box.Min.Y,Corner&4?Box.Max.Z:Box.Min.Z);Reach=FMath::Max(Reach,double(FVector3f(Inverse[B].TransformPosition(P)).Size()));}
   const double Local=WeightScale*(Radii[Pose]+Scales[Pose]*Reach);const FTransform Relative=Skin->GetRelativeTransform();PartRadius=FMath::Max(PartRadius,Relative.GetTranslation().Size()+Relative.GetScale3D().GetAbsMax()*Local);
  }
  // Bound this part's entire clip, not every part by the actor's largest hair
  // or garment radius. Include a small numerical/WPO margin; CalcBounds also
  // unions the original native bound and existing facial visibility guard.
  if(!FMath::IsFinite(PartRadius)||PartRadius>100000)return false;
  PartBounds.Add(float(PartRadius*1.01+2));Radius=FMath::Max(Radius,PartRadius*1.01+2);
 }
 if(!FMath::IsFinite(Radius)||Radius>100000)return false;Out=float(Radius);
 UE_LOG(LogTemp,Display,TEXT("GPU influence bounds actor=%s tight_parts=%u total_parts=%d radius_cm=%.3f"),*Actor.CharacterId,TightParts,Skins.Num(),Out);return true;
}

bool ClockSkinSupported(UOKCrowdRenderSkin* Skin,USkeletalMesh* Mesh,bool Fast=true){
 if(!Skin||!Skin->IsRegistered()||!Mesh||Skin->IsSimulatingPhysics()||Skin->GetPredictedLODLevel()!=0||Skin->GetMeshDeformerInstanceForLOD(0))return false;
 if(Skin->MorphTargetWeights.ContainsByPredicate([](float W){return W!=0.f;})||Skin->GetExternalMorphSetsArray().ContainsByPredicate([](const auto& Set){return !Set.IsEmpty();}))return false;
 for(const auto& Curve:Skin->GetMorphTargetCurves())if(Curve.Value!=0.f)return false;
 // With no clothing assets, GetClothingAssetsInUse cannot resolve any section
 // GUID. Read the live asset list every call; adding assets restores the full
 // native query, so this shortcut requires neither a TTL nor stale cache.
 if(Fast&&static_cast<const USkeletalMesh*>(Mesh)->GetMeshClothingAssets().IsEmpty())return true;
 TArray<UClothingAssetBase*> Cloth;Mesh->GetClothingAssetsInUse(Cloth);return Cloth.IsEmpty();
}
void TickOwnedGpuClocks(FOKCrowdIndependentGpu& S,TConstArrayView<TObjectPtr<AKKImportedCharacterActor>> Actors,TConstArrayView<TObjectPtr<USkeletalMeshComponent>> Leaders){
 TRACE_CPUPROFILER_EVENT_SCOPE(CrowdGpuOwnedClockTick);
 if(!FParse::Param(FCommandLine::Get(),TEXT("CrowdIndependentGpuOwnClock")))return;
 // Readback uses the CPU as oracle and must never measure this ownership mode.
 if(S.Render->Verify||S.ProbeMode||S.VisualCompare||FParse::Param(FCommandLine::Get(),TEXT("CrowdGpuClockVisualCompare")))return;
 const uint64 Failures=S.Render->Fallbacks.Load();if(Failures!=S.ClockRenderFallbacks){RestoreAllGpuClocks(S);++S.ClockFallbacks;S.ClockRenderFallbacks=Failures;return;}
 if(!S.Render->Ready.Load()){RestoreAllGpuClocks(S);return;}
 const bool FastEligibility=CVarGpuEligibilityPass.GetValueOnGameThread()!=0;
 const double Now=S.World.IsValid()?S.World->GetTimeSeconds():0;TSet<TWeakObjectPtr<AKKImportedCharacterActor>> Live;
 for(int32 A=0;A<Actors.Num();++A){auto* Actor=Actors[A].Get();auto* Leader=Leaders.IsValidIndex(A)?Cast<UOKCrowdRenderSkin>(Leaders[A]):nullptr;if(!Actor||!Leader)continue;
  Live.Add(Actor);auto* Lookup=S.ActorLookups.Find(Actor);auto* Instance=Leader->GetSingleNodeInstance();auto* Clip=Instance?Cast<UAnimSequence>(Instance->GetCurrentAsset()):nullptr;
  bool Good=false;auto* Existing=S.Clocks.Find(Actor);bool SameParts=true;
  {
  TRACE_CPUPROFILER_EVENT_SCOPE(CrowdGpuClockEligibility);
  Good=Lookup&&Lookup->Index!=INDEX_NONE&&Lookup->ClipIndex!=INDEX_NONE&&Clip&&Instance->IsPlaying()&&FMath::IsFinite(Clip->GetPlayLength())&&Clip->GetPlayLength()>0&&FMath::IsFinite(Instance->GetPlayRate())&&!Clip->HasRootMotion()&&!Actor->MouthOpenCurve;
  if(Good){auto Data=Clip->GetCompressedData();Good=Data.Get().IndexedCurveNames.IsEmpty();}
  if(Good)for(auto* Component:Actor->GetComponents())if(auto* Primitive=Cast<UPrimitiveComponent>(Component))if(Primitive->IsRegistered()&&(Primitive->IsVisible()||Primitive->bCastHiddenShadow)&&Cast<UMeshComponent>(Primitive)&&!Cast<UOKCrowdRenderSkin>(Primitive))Good=false;
  if(FastEligibility){
  // Validate live state and adoption identity in one ordered traversal. Keep
  // physics/morph/deformer/visibility checks live rather than memoizing them.
  SameParts=!Existing||(Lookup&&Existing->Skins.Num()==Lookup->Skins.Num()&&Existing->MeshAssets.Num()==Existing->Skins.Num()&&Existing->WeightBuffers.Num()==Existing->Skins.Num()&&Existing->RelativeTransforms.Num()==Existing->Skins.Num());
  if(Good){TRACE_CPUPROFILER_EVENT_SCOPE(CrowdGpuClockSkinEligibility);
   for(int32 Part=0;Good&&Part<Lookup->Skins.Num();++Part){
    auto* Skin=Cast<UOKCrowdRenderSkin>(Lookup->Skins[Part].Get());auto* Mesh=Skin?Skin->GetSkeletalMeshAsset():nullptr;
    const auto* Registered=Skin?S.RegisteredMeshes.Find(Skin->GetPrimitiveSceneId()):nullptr;
    Good=Registered&&Registered->Mesh.Get()==Mesh&&Registered->Actor==uint32(Lookup->Index)&&ClockSkinSupported(Skin,Mesh);
    if(Existing&&SameParts)SameParts=Good&&Existing->Skins[Part].Key.Get()==Skin&&Existing->MeshAssets[Part].Get()==Mesh&&Existing->WeightBuffers[Part]==Skin->GetSkinWeightBuffer(0)&&Existing->RelativeTransforms[Part].Equals(Skin->GetRelativeTransform(),0.);
   }
  }
  }else{
   // Deliberately preserve the pre-change path for same-process A/B/A evidence.
   if(Good)for(const auto& Weak:Lookup->Skins){auto* Skin=Cast<UOKCrowdRenderSkin>(Weak.Get());const auto* Registered=Skin?S.RegisteredMeshes.Find(Skin->GetPrimitiveSceneId()):nullptr;Good&=ClockSkinSupported(Skin,Skin?Skin->GetSkeletalMeshAsset():nullptr,false)&&Registered&&Registered->Mesh.Get()==Skin->GetSkeletalMeshAsset()&&Registered->Actor==uint32(Lookup->Index);}
   if(Existing&&Lookup){SameParts=Existing->Skins.Num()==Lookup->Skins.Num()&&Existing->MeshAssets.Num()==Existing->Skins.Num()&&Existing->WeightBuffers.Num()==Existing->Skins.Num()&&Existing->RelativeTransforms.Num()==Existing->Skins.Num();
    for(int32 Part=0;SameParts&&Part<Existing->Skins.Num();++Part){auto* Saved=Existing->Skins[Part].Key.Get();SameParts=Saved&&Saved==Lookup->Skins[Part].Get()&&Existing->MeshAssets[Part].Get()==Saved->GetSkeletalMeshAsset()&&Existing->WeightBuffers[Part]==Saved->GetSkinWeightBuffer(0)&&Existing->RelativeTransforms[Part].Equals(Saved->GetRelativeTransform(),0.);}
   }
  }
  }
  if(Existing&&(!Good||!SameParts||Existing->Clip.Get()!=Clip)){RestoreGpuClock(*Existing);S.Clocks.Remove(Actor);++S.ClockFallbacks;Existing=nullptr;}
  if(!Good)continue;
  if(!Existing){float Radius=0;TArray<float> PartBounds;const auto& Record=S.Bundle->Manifest.Actors[Lookup->Index];if(!GpuClockBounds(*S.Bundle,Record,Lookup->ClipIndex,Lookup->Skins,PartBounds,Radius))continue;
   FOKCrowdIndependentGpu::FClock Clock;Clock.Clip=Clip;Clock.StartWorld=Now;Clock.StartTime=Leader->FinalizedPoseClip.Get()==Clip?Leader->FinalizedPoseTime:Instance->GetCurrentTime();Clock.Time=Clock.StartTime;Clock.Rate=Instance->GetPlayRate();Clock.CpuEvaluations=Leader->PoseEvaluations;
   for(int32 Part=0;Part<Lookup->Skins.Num();++Part){auto* Skin=CastChecked<UOKCrowdRenderSkin>(Lookup->Skins[Part].Get());Clock.Skins.Add({Skin,Skin->IsComponentTickEnabled()});Clock.MeshAssets.Add(Skin->GetSkeletalMeshAsset());Clock.WeightBuffers.Add(Skin->GetSkinWeightBuffer(0));Clock.RelativeTransforms.Add(Skin->GetRelativeTransform());Skin->GpuAnimationClockOwned=true;Skin->GpuAnimationBoundsRadius=PartBounds[Part];Skin->SetComponentTickEnabled(false);Skin->UpdateBounds();Skin->MarkRenderTransformDirty();}
   S.Clocks.Add(Actor,MoveTemp(Clock));Existing=S.Clocks.Find(Actor);UE_LOG(LogTemp,Display,TEXT("GPU owns independent clock character=%s clip=%s parts=%d bounds_radius_cm=%.3f"),*Actor->Character->GetPathName(),*Clip->GetPathName(),Lookup->Skins.Num(),Radius);
  }
  TRACE_CPUPROFILER_EVENT_SCOPE(CrowdGpuClockAdvance);
  auto& Clock=*Existing;
  // A gameplay speed change or explicit seek is independent per actor. Rebase
  // its clock rather than silently retaining the adoption-time play rate.
  const float Rate=Instance->GetPlayRate(),Position=Instance->GetCurrentTime();
  if(Rate!=Clock.Rate||FMath::Abs(Position-Clock.Time)>1e-5f){Clock.StartTime=Position;Clock.StartWorld=Now;Clock.Rate=Rate;}
  const double Elapsed=FMath::FloorToDouble(FMath::Max(0.,Now-Clock.StartWorld)*60.)/60.;const float RawTime=Clock.StartTime+float(Elapsed)*Clock.Rate;
  float Time=Instance->IsLooping()?FMath::Fmod(RawTime,Clip->GetPlayLength()):FMath::Clamp(RawTime,0.f,Clip->GetPlayLength());if(Time<0)Time+=Clip->GetPlayLength();Clock.Time=Time;
  if(!Instance->IsLooping()&&((Clock.Rate>0&&RawTime>=Clip->GetPlayLength())||(Clock.Rate<0&&RawTime<=0)))Instance->SetPlaying(false);
  auto& Playback=S.Playback[Lookup->Index];S.PreviousPlayback[Lookup->Index]=Playback;Playback.ClipA=Playback.ClipB=Lookup->ClipIndex;Playback.Times=FVector4f(Time,Time,0,0);
  Instance->SetPosition(Time,false); // scalar clock mirror; does not evaluate bones
  for(const auto& Saved:Clock.Skins)if(auto* Skin=Saved.Key.Get())Skin->ForceMotionVector();++S.ClockUpdates;
 }
 for(auto It=S.Clocks.CreateIterator();It;++It)if(!Live.Contains(It.Key())){RestoreGpuClock(It.Value());It.RemoveCurrent();}
}
}

bool OKCaptureCrowdGpuClockComparison(const TSharedPtr<FOKCrowdIndependentGpu>& S,const FString& Prefix,FString& Error){
 if(!S||!S->Bundle||!S->Render->Ready.Load()||S->ActorLookups.Num()!=1||S->ProbeMode||S->Render->Verify){Error=TEXT("One complete registered native GPU character required");return false;}
 auto It=S->ActorLookups.CreateIterator();auto* Actor=It.Key().Get();auto& Lookup=It.Value();auto* World=S->World.Get();
 auto* PC=World?World->GetFirstPlayerController():nullptr;auto* Camera=PC?Cast<ACameraActor>(PC->GetViewTarget()):nullptr;
 auto* Clip=Lookup.Clip.Get();UOKCrowdRenderSkin* Leader=nullptr;
 for(const auto& Weak:Lookup.Skins)if(auto* Skin=Cast<UOKCrowdRenderSkin>(Weak.Get())){if(!ClockSkinSupported(Skin,Skin->GetSkeletalMeshAsset())||!S->RegisteredMeshes.Contains(Skin->GetPrimitiveSceneId())){Error=TEXT("Character has unsupported or unregistered native part");return false;}if(!Skin->LeaderPoseComponent.IsValid())Leader=Skin;}
 if(!Actor||!Camera||!Leader||!Clip||Lookup.Index==INDEX_NONE||Lookup.ClipIndex==INDEX_NONE){Error=TEXT("Capture actor/clip/camera unavailable");return false;}
 TArray<float> Bounds;float MaximumBound=0;if(!GpuClockBounds(*S->Bundle,S->Bundle->Manifest.Actors[Lookup.Index],Lookup.ClipIndex,Lookup.Skins,Bounds,MaximumBound)){Error=TEXT("GPU clock capture bounds unavailable");return false;}
 const TCHAR* Views[]={TEXT("front"),TEXT("side"),TEXT("back")};constexpr int32 Size=1024;
 for(int32 Phase=0;Phase<7;++Phase)for(const TCHAR* View:Views)for(const TCHAR* Mode:{TEXT("cpu"),TEXT("gpu")})if(IFileManager::Get().FileExists(*(Prefix+FString::Printf(TEXT(".p%d."),Phase)+View+TEXT(".")+Mode+TEXT(".png")))){Error=TEXT("Fresh comparison prefix required");return false;}
 float Height=100,Distance=300;FParse::Value(FCommandLine::Get(),TEXT("CrowdVisualFocusHeight="),Height);FParse::Value(FCommandLine::Get(),TEXT("CrowdVisualDistance="),Distance);
 if(!FMath::IsFinite(Height)||!FMath::IsFinite(Distance)||Distance<30||Distance>1000){Error=TEXT("Invalid capture view");return false;}
 RestoreAllGpuClocks(*S);auto* Instance=Leader->GetSingleNodeInstance();const float SavedTime=Instance->GetCurrentTime(),SavedRate=Instance->GetPlayRate();const bool Enabled=S->Render->Enabled;
 TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>(World));Target->RenderTargetFormat=RTF_RGBA8;Target->InitAutoFormat(Size,Size);Target->UpdateResourceImmediate(true);
 TStrongObjectPtr<USceneCaptureComponent2D> Capture(NewObject<USceneCaptureComponent2D>(World));Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->bAlwaysPersistRenderingState=false;Capture->CaptureSource=SCS_FinalColorLDR;Capture->TextureTarget=Target.Get();Capture->FOVAngle=50;Capture->PostProcessSettings=Camera->GetCameraComponent()->PostProcessSettings;Capture->PostProcessBlendWeight=1;Capture->ShowFlags.SetTemporalAA(false);Capture->ShowFlags.SetMotionBlur(false);Capture->RegisterComponentWithWorld(World);
 bool Good=true;TArray<TSharedPtr<FJsonValue>> Phases;auto R=S->Render;const uint64 InitialPasses=R->Passes.Load();uint64 MinimumGpuParts=MAX_uint64,BoundsAuditVertices=0;double MaxBoundsRatio=0;
 for(int32 Phase=0;Good&&Phase<7;++Phase){for(const auto& Weak:Lookup.Skins)if(auto* Skin=Cast<UOKCrowdRenderSkin>(Weak.Get()))Skin->GpuAnimationClockOwned=false;const float Time=Clip->GetPlayLength()*float(Phase)/7.f;Instance->SetPlayRate(0);Instance->SetPosition(Time,false);Leader->bRequiredBonesUpToDate=false;Leader->RecalcRequiredBones(0);Leader->RefreshBoneTransforms(nullptr);
  for(const auto& Weak:Lookup.Skins)if(auto* Skin=Cast<UOKCrowdRenderSkin>(Weak.Get()))Skin->ForceMotionVector();FlushRenderingCommands();
  // Native CPU skinning oracle without switching MeshObject to CPU skinning
  // (which would recreate proxies and invalidate the GPU comparison).
  for(int32 Part=0;Good&&Part<Lookup.Skins.Num();++Part)if(auto* Skin=Lookup.Skins[Part].Get()){
   TArray<double> Reach;double WeightScale=1.;if(!GpuInfluenceReach(Skin,Reach,WeightScale))continue;
   const auto& LOD=Skin->GetSkeletalMeshAsset()->GetResourceForRendering()->LODRenderData[0];
   TArray<FMatrix44f> Matrices;Skin->GetCurrentRefToLocalMatrices(Matrices,0);TArray<FVector3f> Positions;
   const auto* W=Skin->GetSkinWeightBuffer(0);const auto C=FindGpuReach(Skin);
   if(LOD.StaticVertexBuffers.PositionVertexBuffer.GetVertexData()&&W->GetDataVertexBuffer()->GetWeightData())USkinnedMeshComponent::ComputeSkinnedPositions(Skin,Positions,Matrices,LOD,*W);
   else if(C&&!C->Positions.IsEmpty()&&!C->WeightData.IsEmpty()){
    Positions.Init(FVector3f::ZeroVector,W->GetNumVertices());
    for(const auto& Section:LOD.RenderSections)for(uint32 V=Section.BaseVertexIndex;V<Section.BaseVertexIndex+Section.NumVertices;++V){
     FVector3f P;FMemory::Memcpy(&P,C->Positions.GetData()+uint64(V)*sizeof(FVector3f),sizeof(P));uint32 Offset=V*W->GetConstantInfluencesVertexStride(),Count=W->GetMaxBoneInfluences();if(W->GetVariableBonesPerVertex()){uint32 Entry;FMemory::Memcpy(&Entry,C->LookupData.GetData()+uint64(V)*4,4);Offset=Entry>>8;Count=Entry&255;}const uint8* Vertex=C->WeightData.GetData()+Offset;
     for(uint32 I=0;I<Count;++I){const uint32 Raw=ReadGpuWeightValue(Vertex+Count*W->GetBoneIndexByteSize()+I*W->GetBoneWeightByteSize(),W->GetBoneWeightByteSize());if(!Raw)continue;
      const uint32 Map=ReadGpuWeightValue(Vertex+I*W->GetBoneIndexByteSize(),W->GetBoneIndexByteSize());const uint32 Bone=Section.BoneMap[Map];
      if(!Matrices.IsValidIndex(Bone)){Good=false;Error=TEXT("Readback oracle bone missing");break;}
      Positions[V]+=FVector3f(Matrices[Bone].TransformPosition(P))*(float(Raw)/(W->GetBoneWeightByteSize()==1?255.f:65535.f));
     }
    }
   }
   if(Positions.Num()!=int32(LOD.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices())){Good=false;Error=TEXT("Bounds native skin oracle vertex count differs");break;}
   for(const auto& Position:Positions){const FVector P=Actor->GetActorTransform().InverseTransformPosition(Skin->GetComponentTransform().TransformPosition(FVector(Position)));const double Ratio=P.Size()/Bounds[Part];
    if(Position.ContainsNaN()||!FMath::IsFinite(Ratio)||Ratio>1.){Good=false;Error=TEXT("Animated native vertex exceeds conservative GPU bound");break;}
    ++BoundsAuditVertices;MaxBoundsRatio=FMath::Max(MaxBoundsRatio,Ratio);
   }
  }
  if(!Good)break;
  auto Playback=S->Playback;Playback[Lookup.Index].ClipA=Playback[Lookup.Index].ClipB=Lookup.ClipIndex;Playback[Lookup.Index].Times=FVector4f(Time,Time,0,0);
  Phases.Add(MakeShared<FJsonValueNumber>(Time));const FVector Focus=Actor->GetActorLocation()+FVector(0,0,Height);
  for(int32 View=0;Good&&View<3;++View){const FVector Offset=Actor->GetActorRotation().RotateVector(FRotator(0,View*90,0).RotateVector(FVector(0,Distance,0)));Capture->SetWorldLocationAndRotation(Focus+Offset,(-Offset).Rotation());
   for(int32 Mode=0;Good&&Mode<2;++Mode){const bool Gpu=Mode==1;
    // Isolate matrix/shading equivalence from different culling metadata. The
    // unmodified CPU-bound comparison is preserved separately as R172 evidence.
    for(int32 Part=0;Part<Lookup.Skins.Num();++Part)if(auto* Skin=Cast<UOKCrowdRenderSkin>(Lookup.Skins[Part].Get())){Skin->GpuAnimationClockOwned=Gpu;Skin->GpuAnimationBoundsRadius=Bounds[Part];Skin->UpdateBounds();Skin->MarkRenderTransformDirty();}
    ENQUEUE_RENDER_COMMAND(ClockComparisonMode)([R,Gpu,Playback](FRHICommandListImmediate&){R->Enabled=Gpu;R->Reset=true;R->Playback=Playback;R->PreviousPlayback=Playback;});
    for(const auto& Weak:Lookup.Skins)if(auto* Skin=Cast<UOKCrowdRenderSkin>(Weak.Get()))Skin->ForceMotionVector();FlushRenderingCommands();
    for(int32 Warm=0;Warm<3;++Warm){Capture->CaptureScene();FlushRenderingCommands();}
    if(Gpu){MinimumGpuParts=FMath::Min(MinimumGpuParts,R->Components.Load());Good=MinimumGpuParts>0&&R->Passes.Load()>InitialPasses;if(!Good)break;}
    TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);Good=Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)&&Pixels.Num()==Size*Size;
    if(Good)Good=FImageUtils::SaveImageByExtension(*(Prefix+FString::Printf(TEXT(".p%d."),Phase)+Views[View]+(Gpu?TEXT(".gpu.png"):TEXT(".cpu.png"))),FImageView(Pixels.GetData(),Size,Size));
   }
  }
 }
 Capture->DestroyComponent();for(const auto& Weak:Lookup.Skins)if(auto* Skin=Cast<UOKCrowdRenderSkin>(Weak.Get())){Skin->GpuAnimationClockOwned=false;Skin->GpuAnimationBoundsRadius=0;Skin->UpdateBounds();Skin->MarkRenderTransformDirty();}Instance->SetPosition(SavedTime,false);Instance->SetPlayRate(SavedRate);Leader->RefreshBoneTransforms(nullptr);
 ENQUEUE_RENDER_COMMAND(RestoreClockComparisonMode)([R,Enabled](FRHICommandListImmediate&){R->Enabled=Enabled;R->Reset=true;});FlushRenderingCommands();
 if(!Good){if(Error.IsEmpty())Error=TEXT("Whole-character capture failed");return false;}
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("captured"),true);Report->SetBoolField(TEXT("group_primitives"),FParse::Param(FCommandLine::Get(),TEXT("CrowdGroupPrimitives")));Report->SetBoolField(TEXT("full_gpu_character"),true);Report->SetBoolField(TEXT("gpu_clock_bounds_active"),true);Report->SetBoolField(TEXT("identical_conservative_bounds"),true);Report->SetBoolField(TEXT("identical_pose_and_camera"),true);Report->SetBoolField(TEXT("temporal_aa"),false);Report->SetNumberField(TEXT("native_parts"),Lookup.Skins.Num());Report->SetNumberField(TEXT("minimum_executed_gpu_parts"),MinimumGpuParts);Report->SetNumberField(TEXT("executed_gpu_passes"),R->Passes.Load()-InitialPasses);Report->SetStringField(TEXT("character"),Actor->Character->GetPathName());Report->SetStringField(TEXT("clip"),Clip->GetPathName());Report->SetArrayField(TEXT("sample_times"),Phases);FString Json;
 uint32 ReadbackParts=0;for(const auto& Weak:Lookup.Skins)if(auto* Skin=Weak.Get()){const auto C=FindGpuReach(Skin);if(C&&!C->Positions.IsEmpty())++ReadbackParts;}Report->SetNumberField(TEXT("bounds_gpu_readback_parts"),ReadbackParts);Report->SetNumberField(TEXT("bounds_audit_native_vertices"),BoundsAuditVertices);Report->SetNumberField(TEXT("bounds_audit_max_radius_ratio"),MaxBoundsRatio);Report->SetBoolField(TEXT("bounds_audit_sampled_containment_passed"),BoundsAuditVertices>0);
 return FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json))&&FFileHelper::SaveStringToFile(Json,*(Prefix+TEXT(".capture.json")));
}
