// FScene methods: private GPU admission while the aggregate bank is preparing.
// All asynchronous commands own their render state and immutable bank. No entry
// or scene pointer is borrowed across render/worker lifetime.
void RetireProgressiveGpu(FEntry& E){
 if(!E.LoadingRender)return;
 auto R=E.LoadingRender;E.LoadingRender.Reset();E.LoadingGpuInstalled=false;
 auto* O=Owner.Get();if(!O||!O->GetWorld()->Scene)return;
 auto* Scene=static_cast<::FScene*>(O->GetWorld()->Scene->GetRenderScene());
 ENQUEUE_RENDER_COMMAND(RetirePrivateLoadingProvider)([R,Scene](FRHICommandListImmediate&){
  if(R->ProviderRegistered)if(auto* P=Scene->GetExtensionPtr<FSkinningTransformProvider>())P->UnregisterProvider(R->ProviderId);
  R->Enabled=false;R->ProviderRegistered=false;R->Scene=nullptr;R->Pose.Release();R->Palettes.Reset();
 });
}
bool InstallProgressiveGpu(FEntry& E){
 auto* O=Owner.Get();auto* Scene=static_cast<::FScene*>(O->GetWorld()->Scene->GetRenderScene());
 auto R=E.LoadingRender;const auto& Bank=E.Bank->Get();FString Error;
 TArray<FOKGpuSkinPaletteData> Palettes;TMap<const USkinnedAsset*,int32> Actors;
 TMap<const USkinnedAsset*,FPrimitiveComponentId> References;TArray<FPrimitiveComponentId> DepthIds;
 for(int32 Pass=0;Pass<2;++Pass){
  auto* Mesh=Pass?E.DepthMesh.Get():E.Mesh.Get();if(!Mesh)continue;
  FOKGpuSkinPaletteData Palette;
  {QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_InstallPalette);if(!OKBuildGpuMeshPalette(Bank.Manifest.Actors[0],Mesh->GetRefSkeleton(),Mesh->GetRefBasesInvMatrix(),Bank.Data.Pose.Tasks.Num(),Palette,Error)){VisualError=Error;Stopped=true;return false;}}
  Actors.Add(Mesh,Palettes.Num());Palettes.Add(MoveTemp(Palette));References.Add(Mesh,E.Cpu->GetPrimitiveSceneId());
  auto* G=NewObject<UInstancedSkinnedMeshComponent>(O);O->AddInstanceComponent(G);{QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_InstallSetAsset);G->SetSkinnedAssetAndUpdate(Mesh);}
  auto* Provider=NewObject<UOKIndependentTransformProvider>(G);Provider->SetRuntimeProviderId(R->ProviderId);Provider->SetDiagnosticBounds(E.AnimationBounds);G->SetTransformProvider(Provider);
  G->SetAnimationMinScreenSize(-1);G->SetCollisionEnabled(ECollisionEnabled::NoCollision);G->SetCastShadow(false);G->SetVisibility(false);
  if(Pass){G->SetRenderInMainPass(false);G->SetRenderInDepthPass(false);E.DepthGpu=G;if(DepthPrepassEnabled)DepthIds.Add(G->GetPrimitiveSceneId());}else E.Gpu=G;
  G->AddInstance(FTransform::Identity,0);{QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_InstallRegister);G->RegisterComponent();}G->SetComponentTickEnabled(false);
  // Registration assigns the actual scene primitive identity.
  if(Pass&&DepthPrepassEnabled)DepthIds.Last()=G->GetPrimitiveSceneId();
 }
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_InstallFrontHair);AttachFrontHair(E,R->ProviderId);}
 TArray<const FMaterialRenderProxy*> SourceMaterials;
 for(const auto& M:E.Materials)if(M->GetMaterial()->GetName()==TEXT("M_IBLAlpha"))SourceMaterials.Add(M->GetRenderProxy());
 ENQUEUE_RENDER_COMMAND(RegisterPrivateLoadingProvider)([R,Scene,Verified=E.Bank,Global=Render,Actors=MoveTemp(Actors),References=MoveTemp(References),Palettes=MoveTemp(Palettes),DepthIds=MoveTemp(DepthIds),SourceMaterials=MoveTemp(SourceMaterials)](FRHICommandListImmediate&)mutable{
  R->Bank=&Verified->Get();R->Validated=Verified;R->Scene=Scene;R->MeshActors=MoveTemp(Actors);R->CpuReferences=MoveTemp(References);R->Sources=MoveTemp(Palettes);R->Initialized=true;
  R->Palettes.SetNum(R->Sources.Num());R->BoneMaps.SetNum(R->Sources.Num());R->StorageModes.SetNum(R->Sources.Num());
  Global->LateDepthProxies.Append(DepthIds);for(auto* M:SourceMaterials)Global->SourceIblMaterials.Add(M);
  if(auto* P=Scene->GetExtensionPtr<FSkinningTransformProvider>()){P->RegisterProvider(R->ProviderId,FSkinningTransformProvider::FOnProvideTransforms::CreateLambda([R](auto& C){R->Provide(C);}),false);R->ProviderRegistered=true;}else R->Fail(TEXT("Private loading transform provider unavailable"));
 });
 if(!LateDepthExtension.IsValid()){LateDepthExtension=FSceneViewExtensions::NewExtension<FLateSourceDepth>(Render,O->GetWorld()->Scene);ENQUEUE_RENDER_COMMAND(RegisterPrivateLoadingComposite)([Extension=LateDepthExtension](FRHICommandListImmediate&){Extension->Register();});}
 BeginReveal(E);E.LoadingGpuInstalled=true;return true;
}
bool ServiceProgressiveGpu(){
 if(Installed||!ProgressiveLoading||LoadingCpuPreview||!RequestedGpu)return false;
 int32 Best=INDEX_NONE;double Distance=TNumericLimits<double>::Max();bool Pending=false;
 // One install a frame: each registers two instanced skinned components (several at once stalled loading 660 ms).
 bool InstalledOne=false;
 for(int32 A=AggregateGpu?1:0;A<Entries.Num();++A){auto& E=*Entries[A];if(E.Stage!=5||E.LoadingGpuInstalled)continue;
  Pending=true;if(!E.LoadingRender){E.LoadingRender=MakeShared<FRenderState,ESPMode::ThreadSafe>();E.LoadingRender->Budget=Render->Budget;}
  const int32 Status=E.LoadingRender->UploadStatus.Load();
  if(Status<0){VisualError=TEXT("Private NPC GPU admission failed");Stopped=true;return true;}
  // The GPU budget is full right now (others still uploading or retiring): try her again in a while.
  if(Status==3){E.LoadingRender->UploadStatus.Store(0);E.BudgetRetryAt=SceneTime+2;if(!E.BudgetWaitLogged){E.BudgetWaitLogged=true;UE_LOG(LogTemp,Warning,TEXT("APNG private GPU bank %d waits for budget"),A);}continue;}
  if(SceneTime<E.BudgetRetryAt){Pending=true;continue;}
  if(Status==2){if(!InstalledOne){InstalledOne=true;QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_InstallProgressive);InstallProgressiveGpu(E);}continue;}
  if(Status==1)continue;
  const double D=FVector::DistSquared(SpawnPosition(A),PlayerPosition);if(D<Distance){Distance=D;Best=A;}
 }
 // One bounded private upload globally per tick, nearest ready actor first.
 if(Best!=INDEX_NONE){auto& E=*Entries[Best];E.LoadingRender->UploadStatus.Store(1);
  ENQUEUE_RENDER_COMMAND(UploadPrivateLoadingBank)([R=E.LoadingRender,Bank=E.Bank](FRHICommandListImmediate& Cmd){
   FRDGBuilder G(Cmd);FString Error;bool Done=false;R->Budget->Collect();
   const bool Passed=R->Pose.UploadManagedChunk(G,Bank.ToSharedRef(),R->Budget,4ull*1024*1024,Done,Error);G.Execute();++R->UploadChunks;
   if(!Passed&&Error.Contains(TEXT("capacity unavailable"))){R->UploadStatus.Store(3);return;}
   if(!Passed){R->Fail(Error);R->UploadStatus.Store(-1);}else R->UploadStatus.Store(Done?2:0);
  });
 }
 return Pending;
}
