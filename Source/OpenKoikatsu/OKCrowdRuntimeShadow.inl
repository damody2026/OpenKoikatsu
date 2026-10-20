// Shadow-only component. The displayed mesh and its materials are not rewritten.
// Workers already returned plain shadow geometry. Material objects, skeletal
// meshes, and provider registration stay on the game thread.
void NoteShadow(const FEntry& E){
 const FString Reasons=FString::Join(E.ShadowReasons,TEXT("; "));
 UE_LOG(LogTemp,Display,TEXT("APNG shadow submission: character=%s source_sections=%d proxy_sections=%d excluded=%d reasons=%s fallback=%s"),*E.Id,E.ShadowSourceSections,E.ShadowProxySections,E.ShadowExcludedSections,*Reasons,*E.ShadowFallback);
}
void ApplyShadowReveal(FEntry& E,UMaterialInstanceDynamic* Material){
 if(!E.RevealStarted||!Material)return;
 Material->SetScalarParameterValue(TEXT("__OK_RuntimeRevealStart"),E.RevealWorldStart);
 Material->SetScalarParameterValue(TEXT("__OK_RuntimeRevealDuration"),3);
}
UMaterialInstanceDynamic* MakeShadowProxy(FEntry& E,EOKRuntimeShadowSectionKind Kind,FString& Error){
 const EKKRuntimeMaterialRecipe Recipe=Kind==EOKRuntimeShadowSectionKind::SolidOneSided?EKKRuntimeMaterialRecipe::IBLDepth:Kind==EOKRuntimeShadowSectionKind::SolidTwoSided?EKKRuntimeMaterialRecipe::IBLOpaque:EKKRuntimeMaterialRecipe::None;
 if(Recipe==EKKRuntimeMaterialRecipe::None){Error=TEXT("Shadow section has no solid proxy");return nullptr;}
 const int32 Index=int32(Recipe)-1;if(!Parents.IsValidIndex(Index)||!Parents[Index].IsValid()){Error=TEXT("Solid shadow proxy parent is not loaded");return nullptr;}
 UMaterialInterface* Parent=Parents[Index].Get();const FString Wanted=OKRuntimeSharedMaterialParentName(Recipe);
 if(Parent->GetName()!=Wanted){Error=FString::Printf(TEXT("Solid shadow proxy parent is %s, expected %s"),*Parent->GetName(),*Wanted);return nullptr;}
 auto* Material=UMaterialInstanceDynamic::Create(Parent,GetTransientPackage());if(!Material){Error=TEXT("Solid shadow proxy instance failed");return nullptr;}
 float Family=-1.f;if(!Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("__OK_RuntimeMaterialFamily")),Family)||Family!=float(Recipe)){Error=TEXT("Solid shadow proxy family differs from its cooked parent");return nullptr;}
 if(Material->IsTwoSided()!=OKRuntimeSharedMaterialTwoSided(Recipe)||Material->GetBlendMode()!=OKRuntimeSharedMaterialBlendMode(Recipe)){Error=TEXT("Solid shadow proxy two-sidedness or blend differs from its cooked parent");return nullptr;}
 if(!Material->GetMaterialResource(GMaxRHIShaderPlatform)){Error=TEXT("Solid shadow proxy parent has no shader resource");return nullptr;}
 const FMaterialRelevance Relevance=Material->GetRelevance_Concurrent(GMaxRHIShaderPlatform);
 if(Relevance.bUsesWorldPositionOffset||Relevance.bUsesPixelDepthOffset||Relevance.bUsesDisplacement){Error=TEXT("Solid shadow proxy parent moves vertices");return nullptr;}
 ApplyShadowReveal(E,Material);E.ShadowProxyMaterials.Emplace(Material);return Material;
}
void RealizeShadowMesh(FEntry& E){
 if(E.ShadowResolved||!E.ShadowJob.IsValid()||!E.ShadowJob.IsReady()||!E.Mesh.IsValid())return;
 FOKRuntimeShadowPrepared Prepared=E.ShadowJob.Consume();E.ShadowResolved=true;
 E.ShadowSourceSections=Prepared.SourceCasterSections;E.ShadowExcludedSections=Prepared.ExcludedSections;E.ShadowReasons=MoveTemp(Prepared.Reasons);
 if(!Prepared.Error.IsEmpty()||Prepared.SectionKinds.Num()!=Prepared.Mesh.Sections.Num()||(Prepared.SourceCasterSections>0&&Prepared.Mesh.Sections.IsEmpty())){
  E.ShadowProxySections=0;E.ShadowFallback=Prepared.Error.IsEmpty()?TEXT("Shadow compaction dropped caster sections"):Prepared.Error;NoteShadow(E);return;
 }
 if(Prepared.Mesh.Sections.IsEmpty()){E.ShadowProxySections=0;NoteShadow(E);return;}
 if(Prepared.Mesh.Sections.Num()>=Prepared.SourceCasterSections){E.ShadowProxySections=0;E.ShadowFallback=TEXT("no compatible shadow section reduction");NoteShadow(E);return;}
 FString Error;TArray<UMaterialInterface*> Slots;TMap<int32,int32> OriginalSlots;UMaterialInstanceDynamic* SolidOne=nullptr;UMaterialInstanceDynamic* SolidTwo=nullptr;
 for(int32 SectionIndex=0;SectionIndex<Prepared.Mesh.Sections.Num();++SectionIndex){
  auto& Section=Prepared.Mesh.Sections[SectionIndex];UMaterialInterface* Material=nullptr;
  const int32 SourceMaterial=Section.Material;
  if(Prepared.SectionKinds[SectionIndex]==EOKRuntimeShadowSectionKind::OriginalMaterial){
   if(const int32* Slot=OriginalSlots.Find(SourceMaterial)){Section.Material=*Slot;continue;}
   if(!E.Materials.IsValidIndex(SourceMaterial)||!E.Materials[SourceMaterial].IsValid()){E.ShadowProxyMaterials.Reset();E.ShadowProxySections=0;E.ShadowFallback=TEXT("Shadow original material is missing");NoteShadow(E);return;}
   Material=E.Materials[SourceMaterial].Get();OriginalSlots.Add(SourceMaterial,Slots.Num());
  }else{
   UMaterialInstanceDynamic*& Proxy=Prepared.SectionKinds[SectionIndex]==EOKRuntimeShadowSectionKind::SolidOneSided?SolidOne:SolidTwo;
   if(!Proxy)Proxy=MakeShadowProxy(E,Prepared.SectionKinds[SectionIndex],Error);
   Material=Proxy;
  }
  if(!Material){E.ShadowProxyMaterials.Reset();E.ShadowProxySections=0;E.ShadowFallback=Error.IsEmpty()?TEXT("Shadow proxy material failed"):Error;NoteShadow(E);return;}
  Section.Material=Slots.Num();Slots.Add(Material);
 }
 Prepared.Mesh.Materials=Slots.Num();
 auto* Mesh=OKCreateRuntimeRenderMesh(Prepared.Mesh,Slots,Error);if(!Mesh){E.ShadowProxyMaterials.Reset();E.ShadowProxySections=0;E.ShadowFallback=Error.IsEmpty()?TEXT("Shadow mesh creation failed"):Error;NoteShadow(E);return;}
 const auto& DisplayRig=E.Mesh->GetRefSkeleton();const auto& ShadowRig=Mesh->GetRefSkeleton();
 const auto& DisplayBind=E.Mesh->GetRefBasesInvMatrix();const auto& ShadowBind=Mesh->GetRefBasesInvMatrix();
 if(DisplayRig.GetNum()!=ShadowRig.GetNum()||DisplayBind.Num()!=ShadowBind.Num()||(DisplayBind.Num()&&FMemory::Memcmp(DisplayBind.GetData(),ShadowBind.GetData(),DisplayBind.Num()*sizeof(FMatrix44f)))){E.ShadowProxyMaterials.Reset();E.ShadowProxySections=0;E.ShadowFallback=TEXT("Runtime shadow private rig bindings differ");NoteShadow(E);return;}
 for(int32 Bone=0;Bone<DisplayRig.GetNum();++Bone)if(DisplayRig.GetBoneName(Bone)!=ShadowRig.GetBoneName(Bone)||DisplayRig.GetParentIndex(Bone)!=ShadowRig.GetParentIndex(Bone)){E.ShadowProxyMaterials.Reset();E.ShadowProxySections=0;E.ShadowFallback=TEXT("Runtime shadow private rig order differs");NoteShadow(E);return;}
 E.ShadowMesh.Reset(Mesh);E.ShadowMouth=Prepared.Face;E.ShadowProxySections=Prepared.Mesh.Sections.Num();E.ShadowFaceDirty=E.ShadowMouth.IsValid();NoteShadow(E);
}
void AttachShadowComponent(FEntry& E,int32 ActorIndex){
 if(E.ShadowGpu.IsValid()||!E.ShadowMesh.IsValid()||!E.ShadowFallback.IsEmpty()||!E.Cpu.IsValid())return;
 // The aggregate bank pointer is published on the render thread. The game
 // thread reads the same immutable bundle from the composition it already owns.
 const bool Aggregate=Installed&&PendingComposition.IsValid();
 const bool Progressive=!Aggregate&&E.LoadingGpuInstalled&&E.LoadingRender.IsValid()&&E.Bank.IsValid();
 if(!Aggregate&&!Progressive)return;
 const int32 PaletteActor=Aggregate?ActorIndex:0;
 const FOKGpuAnimationBundle* Bank=Aggregate?&PendingComposition->Get():&E.Bank->Get();
 if(!Bank||!Bank->Manifest.Actors.IsValidIndex(PaletteActor)){E.ShadowFallback=TEXT("Shadow private rig is not in the pose bank");NoteShadow(E);return;}
 FString Error;FOKGpuSkinPaletteData Palette;
 if(!OKBuildGpuMeshPalette(Bank->Manifest.Actors[PaletteActor],E.ShadowMesh->GetRefSkeleton(),E.ShadowMesh->GetRefBasesInvMatrix(),Bank->Data.Pose.Tasks.Num(),Palette,Error)){E.ShadowFallback=Error.IsEmpty()?TEXT("Shadow private palette was rejected"):Error;NoteShadow(E);return;}
 auto* O=Owner.Get();if(!O)return;
 auto* Gpu=NewObject<UInstancedSkinnedMeshComponent>(O);O->AddInstanceComponent(Gpu);Gpu->SetSkinnedAssetAndUpdate(E.ShadowMesh.Get());
 auto State=Aggregate?TSharedPtr<FRenderState,ESPMode::ThreadSafe>(Render):E.LoadingRender;
 auto* Provider=NewObject<UOKIndependentTransformProvider>(Gpu);Provider->SetRuntimeProviderId(State->ProviderId);Provider->SetDiagnosticBounds(E.AnimationBounds);Gpu->SetTransformProvider(Provider);
 Gpu->SetAnimationMinScreenSize(-1);Gpu->SetCollisionEnabled(ECollisionEnabled::NoCollision);Gpu->SetRenderInMainPass(false);Gpu->SetRenderInDepthPass(false);Gpu->SetCastShadow(false);Gpu->SetVisibility(false);
 auto* Mesh=E.ShadowMesh.Get();const auto Cpu=E.Cpu->GetPrimitiveSceneId();
 ENQUEUE_RENDER_COMMAND(AdmitRuntimeShadowMesh)([State,Mesh,Palette=MoveTemp(Palette),Cpu](FRHICommandListImmediate&) mutable{
  if(State->MeshActors.Contains(Mesh))return;const int32 Slot=State->Sources.Num();
  State->Sources.Add(MoveTemp(Palette));State->MeshActors.Add(Mesh,Slot);State->CpuReferences.Add(Mesh,Cpu);
  if(!State->Initialized)return;
  if(State->Palettes.Num()!=Slot||State->BoneMaps.Num()!=Slot||State->StorageModes.Num()!=Slot){State->Fail(TEXT("Runtime shadow palette admission raced"));return;}
  State->Palettes.Add(nullptr);State->BoneMaps.AddDefaulted();State->StorageModes.AddDefaulted();
 });
 Gpu->AddInstance(FTransform::Identity,0);Gpu->RegisterComponent();Gpu->SetComponentTickEnabled(false);E.ShadowGpu=Gpu;
}
void ServiceShadowSubmission(){
 int32 Created=0;
 for(auto& Entry:Entries){auto& E=*Entry;if(E.ShadowResolved||!E.ShadowJob.IsValid()||!E.ShadowJob.IsReady())continue;if(Created)continue;RealizeShadowMesh(E);++Created;}
 for(int32 Actor=0;Actor<Entries.Num();++Actor)AttachShadowComponent(*Entries[Actor],Installed?Actor:0);
}
