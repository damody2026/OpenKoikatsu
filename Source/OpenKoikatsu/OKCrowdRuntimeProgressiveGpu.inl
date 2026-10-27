// FScene methods: private GPU admission while the aggregate bank is preparing.
// All asynchronous commands own their render state and immutable bank. No entry
// or scene pointer is borrowed across render/worker lifetime.
void RetireProgressiveGpu(FEntry& E){
 if(!E.LoadingRender)return;
 auto R=E.LoadingRender;E.LoadingRender.Reset();E.LoadingGpuInstalled=false;E.PrivateCharged=false;
 auto* O=Owner.Get();if(!O||!O->GetWorld()->Scene)return;
 auto* Scene=static_cast<::FScene*>(O->GetWorld()->Scene->GetRenderScene());
 ENQUEUE_RENDER_COMMAND(RetirePrivateLoadingProvider)([R,Scene](FRHICommandListImmediate&){
  if(R->ProviderRegistered)if(auto* P=Scene->GetExtensionPtr<FSkinningTransformProvider>())P->UnregisterProvider(R->ProviderId);
  R->Enabled=false;R->ProviderRegistered=false;R->Scene=nullptr;R->Pose.Release();R->Palettes.Reset();
 });
}
// The aggregate bank reserves its whole size at once while the private leases are still charged (100 bank girls: 2.4 GB
// of each against 4 GiB). Hand private banks back, farthest from the player first, until it fits beside the rest; one
// whose bank went back stays hidden until the aggregate installs, like one still waiting for budget.
static uint64 ResidentBankBytes(const FOKGpuAnimationBundle& Bank){const auto& B=Bank.Data.Pose;return uint64(B.Keys.Num())*16+uint64(B.Tracks.Num())*16+uint64(B.Actors.Num())*32+uint64(B.Tasks.Num())*112+uint64(B.InverseBindRows.Num())*16;}
bool MakeRoomForAggregate(){
 const uint64 Need=ResidentBankBytes(PendingComposition->Get()),Limit=Render->Budget->GetLimit();
 if(Need>Limit){VisualError=FString::Printf(TEXT("Aggregate GPU bank (%llu MB) exceeds the crowd GPU budget (%llu MB)"),Need>>20,Limit>>20);return false;}
 uint64 Charged=0;TArray<int32> Held;
 for(int32 A=0;A<Entries.Num();++A){const auto& E=*Entries[A];if(E.PrivateCharged&&E.Bank.IsValid()){Charged+=ResidentBankBytes(E.Bank->Get());Held.Add(A);}}
 Held.Sort([&](int32 X,int32 Y){return FVector::DistSquared(Entries[X]->Transform.GetTranslation(),PlayerPosition)>FVector::DistSquared(Entries[Y]->Transform.GetTranslation(),PlayerPosition);});
 int32 Returned=0;
 for(int32 A:Held){if(Charged+Need<=Limit)break;auto& E=*Entries[A];Charged-=ResidentBankBytes(E.Bank->Get());++Returned;
  if(auto* G=E.Gpu.Get())G->DestroyComponent();if(auto* G=E.DepthGpu.Get())G->DestroyComponent();if(auto* G=E.ShadowGpu.Get())G->DestroyComponent();if(auto* G=E.FrontGpu.Get())G->SetVisibility(false);
  E.Gpu.Reset();E.DepthGpu.Reset();E.ShadowGpu.Reset();RetireProgressiveGpu(E);}
 UE_LOG(LogTemp,Display,TEXT("APNG aggregate bank %llu MB: %d private banks hold %llu MB of %llu MB; %d handed back"),Need>>20,Held.Num()-Returned,Charged>>20,Limit>>20,Returned);
 return true;
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
 // GPU rig: her bank keeps only its layout (a pose from it is not hers in motion), so nobody shows before the rig does.
 if(Installed||!ProgressiveLoading||LoadingCpuPreview||!RequestedGpu||GpuRig)return false;
 int32 Best=INDEX_NONE;double Distance=TNumericLimits<double>::Max();bool Pending=false;
 // One install a frame: each registers two instanced skinned components (several at once stalled loading 660 ms).
 bool InstalledOne=false;
 for(int32 A=AggregateGpu?1:0;A<Entries.Num();++A){auto& E=*Entries[A];if(E.Stage!=5||E.LoadingGpuInstalled)continue;
  Pending=true;if(!E.LoadingRender){E.LoadingRender=MakeShared<FRenderState,ESPMode::ThreadSafe>();E.LoadingRender->Budget=Render->Budget;}
  const int32 Status=E.LoadingRender->UploadStatus.Load();
  if(Status<0){VisualError=TEXT("Private NPC GPU admission failed");Stopped=true;return true;}
  // The GPU budget is full right now (others still uploading or retiring): try her again in a while.
  if(Status==3){E.LoadingRender->UploadStatus.Store(0);E.PrivateCharged=false;E.BudgetRetryAt=SceneTime+2;if(!E.BudgetWaitLogged){E.BudgetWaitLogged=true;UE_LOG(LogTemp,Warning,TEXT("APNG private GPU bank %d waits for budget"),A);}continue;}
  if(SceneTime<E.BudgetRetryAt){Pending=true;continue;}
  if(Status==2){if(!InstalledOne){InstalledOne=true;QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_InstallProgressive);InstallProgressiveGpu(E);}continue;}
  if(Status==1)continue;
  const double D=FVector::DistSquared(SpawnPosition(A),PlayerPosition);if(D<Distance){Distance=D;Best=A;}
 }
 // One bounded private upload globally per tick, nearest ready actor first.
 if(Best!=INDEX_NONE){auto& E=*Entries[Best];E.LoadingRender->UploadStatus.Store(1);E.PrivateCharged=true;
  ENQUEUE_RENDER_COMMAND(UploadPrivateLoadingBank)([R=E.LoadingRender,Bank=E.Bank](FRHICommandListImmediate& Cmd){
   FRDGBuilder G(Cmd);FString Error;bool Done=false;R->Budget->Collect();
   const bool Passed=R->Pose.UploadManagedChunk(G,Bank.ToSharedRef(),R->Budget,4ull*1024*1024,Done,Error);G.Execute();++R->UploadChunks;
   if(!Passed&&Error.Contains(TEXT("capacity unavailable"))){R->UploadStatus.Store(3);return;}
   if(!Passed){R->Fail(Error);R->UploadStatus.Store(-1);}else R->UploadStatus.Store(Done?2:0);
  });
 }
 return Pending;
}
