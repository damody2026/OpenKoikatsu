// Offline repair: preserve the reduced surface, materials, UVs and topology,
// but disambiguate nearby source limbs using their authored UV correspondence.
int32 BindingStretchCount(USkeletalMesh* Mesh,UAnimSequence* Walk,const FMeshDescription& Desc,double& MaxRatio){
 FSkeletalMeshConstAttributes A(Desc);TMap<FVertexID,FVector> First;TSet<FTriangleID> Bad;MaxRatio=1;
 for(int32 S=0;S<13;++S){const auto M=CoverageMatrices(Mesh,Walk,Walk->GetPlayLength()*S/12.);if(M.IsEmpty())return -1;TMap<FVertexID,FVector> P;
  for(auto V:Desc.Vertices().GetElementIDs())P.Add(V,FVector(CoverageBlend(A.GetVertexSkinWeights().Get(V),M).TransformPosition(A.GetVertexPositions()[V])));
  if(S==0)First=P;
  for(auto T:Desc.Triangles().GetElementIDs()){const auto V=Desc.GetTriangleVertices(T);for(int32 E=0;E<3;++E){const auto X=V[E],Y=V[(E+1)%3];const double Base=FVector::Distance(First[X],First[Y]),Now=FVector::Distance(P[X],P[Y]);if(Base>.1){MaxRatio=FMath::Max(MaxRatio,Now/Base);if(Base<8&&Now>15&&Now>Base*3)Bad.Add(T);}}}
 }return Bad.Num();
}
TSharedPtr<FJsonObject> BindingStretchDetails(USkeletalMesh* Mesh,UAnimSequence* Walk){
 const auto* Desc=Mesh->GetMeshDescription(0);if(!Desc)return nullptr;FSkeletalMeshConstAttributes A(*Desc);
 TMap<FVertexID,FVector> First;TMap<FTriangleID,TSharedPtr<FJsonObject>> Bad;TMap<FString,int32> Slots;
 for(int32 S=0;S<13;++S){const auto M=CoverageMatrices(Mesh,Walk,Walk->GetPlayLength()*S/12.);if(M.IsEmpty())return nullptr;TMap<FVertexID,FVector> P;
  for(auto V:Desc->Vertices().GetElementIDs())P.Add(V,FVector(CoverageBlend(A.GetVertexSkinWeights().Get(V),M).TransformPosition(A.GetVertexPositions()[V])));if(S==0)First=P;
  for(auto T:Desc->Triangles().GetElementIDs()){const auto Vertices=Desc->GetTriangleVertices(T);bool Suspect=false;double Ratio=0;
   for(int32 E=0;E<3;++E){const double Base=FVector::Distance(First[Vertices[E]],First[Vertices[(E+1)%3]]),Now=FVector::Distance(P[Vertices[E]],P[Vertices[(E+1)%3]]);if(Base>.1&&Base<8&&Now>15&&Now>Base*3){Suspect=true;Ratio=FMath::Max(Ratio,Now/Base);}}
   if(!Suspect)continue;const FString Slot=A.GetPolygonGroupMaterialSlotNames()[Desc->GetTrianglePolygonGroup(T)].ToString();auto& Row=Bad.FindOrAdd(T);
   if(Row&&Row->GetNumberField(TEXT("ratio"))>=Ratio)continue;
   if(!Row)++Slots.FindOrAdd(Slot);Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("triangle"),T.GetValue());Row->SetStringField(TEXT("slot"),Slot);Row->SetNumberField(TEXT("phase"),S/12.);Row->SetNumberField(TEXT("ratio"),Ratio);
   TArray<TSharedPtr<FJsonValue>> Points;for(auto V:Vertices){auto Point=MakeShared<FJsonObject>();Point->SetNumberField(TEXT("vertex"),V.GetValue());Point->SetStringField(TEXT("reference_cm"),First[V].ToString());Point->SetStringField(TEXT("posed_cm"),P[V].ToString());TArray<TSharedPtr<FJsonValue>> Bones;
    for(const auto W:A.GetVertexSkinWeights().Get(V)){auto Bone=MakeShared<FJsonObject>();Bone->SetStringField(TEXT("name"),Mesh->GetRefSkeleton().GetBoneName(W.GetBoneIndex()).ToString());Bone->SetNumberField(TEXT("weight"),W.GetWeight());Bones.Add(MakeShared<FJsonValueObject>(Bone));}Point->SetArrayField(TEXT("bones"),Bones);Points.Add(MakeShared<FJsonValueObject>(Point));}Row->SetArrayField(TEXT("vertices"),Points);
  }
 }
 auto Result=MakeShared<FJsonObject>();auto Groups=MakeShared<FJsonObject>();for(const auto& Slot:Slots)Groups->SetNumberField(Slot.Key,Slot.Value);Result->SetObjectField(TEXT("slots"),Groups);TArray<TSharedPtr<FJsonValue>> Triangles;for(const auto& Row:Bad)Triangles.Add(MakeShared<FJsonValueObject>(Row.Value));Result->SetArrayField(TEXT("triangles"),Triangles);return Result;
}
void RestoreBindingSurface(FMeshDescription& Low,const FMeshDescription& Source,FName Slot){
 FSkeletalMeshAttributes To(Low);FSkeletalMeshConstAttributes From(Source);FPolygonGroupID Group;
 for(auto G:Low.PolygonGroups().GetElementIDs())if(To.GetPolygonGroupMaterialSlotNames()[G]==Slot){Group=G;break;}
 check(Group.GetValue()>=0);TArray<FPolygonID> Remove;for(auto P:Low.Polygons().GetElementIDs())if(Low.GetPolygonPolygonGroup(P)==Group)Remove.Add(P);for(auto P:Remove)Low.DeletePolygon(P);
 TMap<FVertexID,FVertexID> Vertices;for(auto T:Source.Triangles().GetElementIDs())if(From.GetPolygonGroupMaterialSlotNames()[Source.GetTrianglePolygonGroup(T)]==Slot){TArray<FVertexInstanceID> Corners;
  for(auto I:Source.GetTriangleVertexInstances(T)){const auto Original=Source.GetVertexInstanceVertex(I);FVertexID V;if(const auto* Found=Vertices.Find(Original))V=*Found;else{V=Low.CreateVertex();Vertices.Add(Original,V);To.GetVertexPositions()[V]=From.GetVertexPositions()[Original];TArray<UE::AnimationCore::FBoneWeight> Weights;for(const auto W:From.GetVertexSkinWeights().Get(Original))Weights.Add(W);To.GetVertexSkinWeights().SetRaw(V,Weights);}
   auto N=Low.CreateVertexInstance(V);Corners.Add(N);To.GetVertexInstanceNormals()[N]=From.GetVertexInstanceNormals()[I];To.GetVertexInstanceTangents()[N]=From.GetVertexInstanceTangents()[I];To.GetVertexInstanceBinormalSigns()[N]=From.GetVertexInstanceBinormalSigns()[I];To.GetVertexInstanceColors()[N]=From.GetVertexInstanceColors()[I];for(int32 UV=0;UV<From.GetVertexInstanceUVs().GetNumChannels();++UV)To.GetVertexInstanceUVs().Set(N,UV,From.GetVertexInstanceUVs().Get(I,UV));}
  Low.CreateTriangle(Group,Corners);
 }
 FElementIDRemappings Remap;Low.Compact(Remap);
}
int32 RestoreResidualBindingAssets(int32 Start,int32 End,const FString& Params){
 FString Output,Evidence,Text;if(!FParse::Value(*Params,TEXT("Output="),Output)||IFileManager::Get().FileExists(*Output)||!FParse::Value(*Params,TEXT("ResidualEvidence="),Evidence)||!FFileHelper::LoadFileToString(Text,*Evidence))return 1;
 TSharedPtr<FJsonObject> Audit;const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Audit)||!Audit||!Audit->TryGetArrayField(TEXT("cards"),Entries))return 1;TMap<int32,TSharedPtr<FJsonObject>> EvidenceCards;for(const auto& Entry:*Entries){auto Row=Entry->AsObject();if(!Row)return 1;EvidenceCards.Add(int32(Row->GetNumberField(TEXT("index"))),Row);}TArray<TSharedPtr<FJsonValue>> Rows;
 for(int32 Index=Start;Index<End;++Index){
  {const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index),Root=Base/TEXT("GameLowSurfaceSafeR8");
   TStrongObjectPtr<UKKImportedCharacter> Low(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("GameLowBindingSafeR7/Character.Character"))));TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("GameLowBindingSafeR7/Walk/Animations.Animations"))));if(!Low||Low->Outfits.Num()!=1||!Walk||Walk->Animations.Num()!=1||Walk->Character!=Low.Get()||FPackageName::DoesPackageExist(Root/TEXT("Character")))return 1;
   FAssetCompilingManager::Get().FinishAllCompilation();int64 Total=0;for(const auto& P:Low->Outfits[0].Parts)if(P.SkinnedModel)for(const auto& S:P.SkinnedModel->GetResourceForRendering()->LODRenderData[0].RenderSections)Total+=S.NumTriangles;
   auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("index"),Index);Row->SetStringField(TEXT("source_hash"),Low->SourceHash);Row->SetNumberField(TEXT("triangles_before"),Total);TArray<TSharedPtr<FJsonValue>> Restored;USkeletalMesh* Replacement=nullptr;TStrongObjectPtr<USkeletalMesh> SkinHold;
   if(const auto* EvidenceRow=EvidenceCards.Find(Index)){
    if((*EvidenceRow)->GetStringField(TEXT("source_hash"))!=Low->SourceHash)return 1;const auto BadSlots=(*EvidenceRow)->GetObjectField(TEXT("published_low"))->GetObjectField(TEXT("slots")),OriginalBad=(*EvidenceRow)->GetObjectField(TEXT("original_high"))->GetObjectField(TEXT("slots")),RigidBad=(*EvidenceRow)->GetObjectField(TEXT("high_with_low_animation"))->GetObjectField(TEXT("slots"));
    TStrongObjectPtr<UKKImportedCharacter> High(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("PackedRig/Character.Character"))));if(!High||High->SourceHash!=Low->SourceHash)return 1;
    const auto* H=High->Outfits[0].Parts.FindByPredicate([](const auto& P){return P.Name==TEXT("PackedVisibleParts");});const auto* L=Low->Outfits[0].Parts.FindByPredicate([](const auto& P){return P.Name==TEXT("PackedVisibleParts");});if(!H||!L||!H->SkinnedModel||!L->SkinnedModel)return 1;
    const auto& HR=H->SkinnedModel->GetRefSkeleton();const auto& LR=L->SkinnedModel->GetRefSkeleton();if(HR.GetNum()!=LR.GetNum())return 1;for(int32 B=0;B<HR.GetNum();++B)if(HR.GetBoneName(B)!=LR.GetBoneName(B)||!HR.GetRefBonePose()[B].Equals(LR.GetRefBonePose()[B],1e-6))return 1;
    const auto* Source=H->SkinnedModel->GetMeshDescription(0);if(!Source||!L->SkinnedModel->GetMeshDescription(0))return 1;FMeshDescription Desc=*L->SkinnedModel->GetMeshDescription(0);FSkeletalMeshConstAttributes HA(*Source);int32 Before=0,After=0;double Ratio=0;Before=BindingStretchCount(L->SkinnedModel,Walk->Animations[0].Sequence,Desc,Ratio);
    for(const auto& Bad:BadSlots->Values){if(OriginalBad->HasField(Bad.Key)||RigidBad->HasField(Bad.Key))continue;const FName Slot(*Bad.Key);FSkeletalMeshConstAttributes LA(Desc);int32 OriginalCount=0,LowCount=0;for(auto T:Source->Triangles().GetElementIDs())if(HA.GetPolygonGroupMaterialSlotNames()[Source->GetTrianglePolygonGroup(T)]==Slot)++OriginalCount;for(auto T:Desc.Triangles().GetElementIDs())if(LA.GetPolygonGroupMaterialSlotNames()[Desc.GetTrianglePolygonGroup(T)]==Slot)++LowCount;
     if(OriginalCount<=0||LowCount<=0||Total-LowCount+OriginalCount>=30000)continue;FMeshDescription Candidate=Desc;RestoreBindingSurface(Candidate,*Source,Slot);const int32 Count=BindingStretchCount(L->SkinnedModel,Walk->Animations[0].Sequence,Candidate,Ratio);const int32 Current=BindingStretchCount(L->SkinnedModel,Walk->Animations[0].Sequence,Desc,Ratio);if(Count<0||Count>=Current)continue;Desc=MoveTemp(Candidate);Total+=OriginalCount-LowCount;Restored.Add(MakeShared<FJsonValueString>(Slot.ToString()));
    }
    After=BindingStretchCount(L->SkinnedModel,Walk->Animations[0].Sequence,Desc,Ratio);Row->SetNumberField(TEXT("suspects_before"),Before);Row->SetNumberField(TEXT("suspects_after"),After);if(!Restored.IsEmpty()){Replacement=DuplicateObject<USkeletalMesh>(L->SkinnedModel,CreatePackage(*(Root/TEXT("Body"))),TEXT("Body"));SkinHold.Reset(Replacement);Replacement->CreateMeshDescription(0,MoveTemp(Desc));Replacement->CommitMeshDescription(0);Replacement->Build();Replacement->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();if(!SaveCoverageAsset(Replacement))return 1;}
   }
   if(Total<=0||Total>=30000)return 1;auto* Card=DuplicateObject<UKKImportedCharacter>(Low.Get(),CreatePackage(*(Root/TEXT("Character"))),TEXT("Character"));TStrongObjectPtr<UKKImportedCharacter> CardHold(Card);if(Replacement)for(auto& P:Card->Outfits[0].Parts)if(P.Name==TEXT("PackedVisibleParts"))P.SkinnedModel=Replacement;
   auto* Library=DuplicateObject<UKKImportedAnimationLibrary>(Walk.Get(),CreatePackage(*(Root/TEXT("Walk/Animations"))),TEXT("Animations"));TStrongObjectPtr<UKKImportedAnimationLibrary> WalkHold(Library);Library->Character=Card;if(!SaveCoverageAsset(Card)||!SaveCoverageAsset(Library))return 1;Row->SetNumberField(TEXT("triangles_after"),Total);Row->SetArrayField(TEXT("restored_slots"),Restored);Rows.Add(MakeShared<FJsonValueObject>(Row));UE_LOG(LogTemp,Display,TEXT("Crowd residual surface index=%d restored=%d total=%lld"),Index,Restored.Num(),Total);
  }CollectGarbage(RF_NoFlags);
 }
 auto Result=MakeShared<FJsonObject>();Result->SetArrayField(TEXT("cards"),Rows);FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));return FFileHelper::SaveStringToFile(Text,*Output)?0:1;
}
int32 TryBlenderResidualBindings(int32 Start,int32 End,const FString& Params){
 FString Output,Evidence,Text;if(!FParse::Value(*Params,TEXT("Output="),Output)||IFileManager::Get().FileExists(*Output)||!FParse::Value(*Params,TEXT("ResidualEvidence="),Evidence)||!FFileHelper::LoadFileToString(Text,*Evidence))return 1;
 TSharedPtr<FJsonObject> Audit;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Audit)||!Audit)return 1;TArray<TSharedPtr<FJsonValue>> Rows;
 for(int32 Index=Start;Index<End;++Index){const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index),Root=Base/TEXT("GameLowSurfaceSafeR8");
  const auto* Entry=Audit->GetArrayField(TEXT("cards")).FindByPredicate([Index](const auto& V){return V->AsObject()->GetIntegerField(TEXT("index"))==Index;});if(!Entry)return 1;const auto E=(*Entry)->AsObject();
  TStrongObjectPtr<UKKImportedCharacter> Low(LoadObject<UKKImportedCharacter>(nullptr,*(Root/TEXT("Character.Character")))),High(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("PackedRig/Character.Character"))));TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Root/TEXT("Walk/Animations.Animations")))),HighWalk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("PackedRig/Walk/Animations.Animations"))));if(!Low||!High||!Walk||!HighWalk||Low->SourceHash!=E->GetStringField(TEXT("source_hash"))||High->SourceHash!=Low->SourceHash)return 1;
  FAssetCompilingManager::Get().FinishAllCompilation();auto* L=Low->Outfits[0].Parts.FindByPredicate([](const auto& P){return P.Name==TEXT("PackedVisibleParts");});const auto* H=High->Outfits[0].Parts.FindByPredicate([](const auto& P){return P.Name==TEXT("PackedVisibleParts");});if(!L||!H||!L->SkinnedModel||!H->SkinnedModel)return 1;
  const auto Ref=ReferenceSkinMatrices(H->SkinnedModel,HighWalk->Animations[0].Sequence);const auto& Encoded=*H->SkinnedModel->GetMeshDescription(0);FMeshDescription Canonical=Encoded;FSkeletalMeshAttributes CA(Canonical);for(auto V:Canonical.Vertices().GetElementIDs()){const auto Blend=VertexBlend(CA.GetVertexSkinWeights().Get(V),Ref);CA.GetVertexPositions()[V]=FVector3f(Blend.TransformPosition(CA.GetVertexPositions()[V]));for(auto I:Canonical.GetVertexVertexInstanceIDs(V)){CA.GetVertexInstanceNormals()[I]=FVector3f(Blend.TransformVector(CA.GetVertexInstanceNormals()[I])).GetSafeNormal();CA.GetVertexInstanceTangents()[I]=FVector3f(Blend.TransformVector(CA.GetVertexInstanceTangents()[I])).GetSafeNormal();}}
  FMeshDescription Desc=*L->SkinnedModel->GetMeshDescription(0);Desc.VertexAttributes().RegisterAttribute<int32>(TEXT("GameLowAuthored"),1,0);auto Flags=Desc.VertexAttributes().GetAttributesRef<int32>(TEXT("GameLowAuthored"));for(auto V:Desc.Vertices().GetElementIDs())Flags[V]=1;
  const auto Bad=E->GetObjectField(TEXT("published_low"))->GetObjectField(TEXT("slots")),SourceBad=E->GetObjectField(TEXT("original_high"))->GetObjectField(TEXT("slots")),RigidBad=E->GetObjectField(TEXT("high_with_low_animation"))->GetObjectField(TEXT("slots"));TArray<int32> Groups;TSet<FName> Slots;
  for(const auto& Slot:Bad->Values)if(!RigidBad->HasField(Slot.Key)){const FName Name(*Slot.Key);Slots.Add(Name);for(auto G:Canonical.PolygonGroups().GetElementIDs())if(CA.GetPolygonGroupMaterialSlotNames()[G]==Name)Groups.Add(G.GetValue());}if(Groups.IsEmpty())return 1;
  int64 Other=0;for(const auto& Part:Low->Outfits[0].Parts)if(Part.SkinnedModel&&Part.Name!=TEXT("PackedVisibleParts"))for(const auto& Section:Part.SkinnedModel->GetResourceForRendering()->LODRenderData[0].RenderSections)Other+=Section.NumTriangles;FSkeletalMeshConstAttributes DA(Desc);for(auto T:Desc.Triangles().GetElementIDs())if(!Slots.Contains(DA.GetPolygonGroupMaterialSlotNames()[Desc.GetTrianglePolygonGroup(T)]))++Other;if(Other>=29900)return 1;
  double Ratio=0;const int32 Before=BindingStretchCount(L->SkinnedModel,Walk->Animations[0].Sequence,Desc,Ratio);if(!RecoverBlenderSurfaces(Desc,Canonical,Groups,uint32(29900-Other))||!TransferAuthoredBindings(Desc,Canonical,Encoded,Ref))return 1;const int32 After=BindingStretchCount(L->SkinnedModel,Walk->Animations[0].Sequence,Desc,Ratio);const int64 Total=Other+([&](){int64 Count=0;FSkeletalMeshConstAttributes A(Desc);for(auto T:Desc.Triangles().GetElementIDs())if(Slots.Contains(A.GetPolygonGroupMaterialSlotNames()[Desc.GetTrianglePolygonGroup(T)]))++Count;return Count;}());
  auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("index"),Index);Row->SetNumberField(TEXT("suspects_before"),Before);Row->SetNumberField(TEXT("suspects_after"),After);Row->SetNumberField(TEXT("triangles"),Total);Row->SetBoolField(TEXT("accepted"),After==0&&Total<30000);Row->SetBoolField(TEXT("applied"),false);
  if(After==0&&Total<30000&&FParse::Param(*Params,TEXT("ApplyBlenderResidual"))){auto* Mesh=DuplicateObject<USkeletalMesh>(L->SkinnedModel,CreatePackage(*(Root/TEXT("BodyTopologySafeR1"))),TEXT("BodyTopologySafeR1"));TStrongObjectPtr<USkeletalMesh> Hold(Mesh);Mesh->CreateMeshDescription(0,MoveTemp(Desc));Mesh->CommitMeshDescription(0);Mesh->Build();Mesh->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();if(!SaveCoverageAsset(Mesh))return 1;L->SkinnedModel=Mesh;FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(Low->GetOutermost(),Low.Get(),*FPackageName::LongPackageNameToFilename(Root/TEXT("Character"),FPackageName::GetAssetPackageExtension()),Args))return 1;Row->SetBoolField(TEXT("applied"),true);}
  Rows.Add(MakeShared<FJsonValueObject>(Row));
 }auto Result=MakeShared<FJsonObject>();Result->SetArrayField(TEXT("cards"),Rows);FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));return FFileHelper::SaveStringToFile(Text,*Output)?0:1;
}
int32 RepairCrowdBindings(int32 Start,int32 End,const FString& Params){
 if(FParse::Param(*Params,TEXT("TryBlenderResidual")))return TryBlenderResidualBindings(Start,End,Params);
 if(FParse::Param(*Params,TEXT("RestoreResidualSurfaces")))return RestoreResidualBindingAssets(Start,End,Params);
 FString Output;if(!FParse::Value(*Params,TEXT("Output="),Output)||IFileManager::Get().FileExists(*Output))return 1;
 const bool AuditOnly=FParse::Param(*Params,TEXT("AuditOnly"));TArray<TSharedPtr<FJsonValue>> Rows;
 for(int32 Index=Start;Index<End;++Index){
  {const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index),Root=Base/TEXT("GameLowBindingSafeR7");
   TStrongObjectPtr<UKKImportedCharacter> Source(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("PackedRig/Character.Character"))));
   TStrongObjectPtr<UKKImportedCharacter> Low(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("GameLowBodySafeR6/Character.Character"))));
   TStrongObjectPtr<UKKImportedAnimationLibrary> HighWalk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("PackedRig/Walk/Animations.Animations"))));
   TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("GameLowBodySafeR6/Walk/Animations.Animations"))));
   if(!Source||!Low||!HighWalk||!Walk||Source->SourceHash!=Low->SourceHash)return 1;FAssetCompilingManager::Get().FinishAllCompilation();
   const auto* Authored=Source->Outfits[0].Parts.FindByPredicate([](const auto& P){return P.Name==TEXT("PackedVisibleParts");});
   const auto* Reduced=Low->Outfits[0].Parts.FindByPredicate([](const auto& P){return P.Name==TEXT("PackedVisibleParts");});
   if(!Authored||!Reduced||!Authored->SkinnedModel||!Reduced->SkinnedModel)return 1;
   auto* HighMesh=Authored->SkinnedModel.Get();auto* LowMesh=Reduced->SkinnedModel.Get();if(!HighMesh->GetMeshDescription(0)||!LowMesh->GetMeshDescription(0))return 1;
   const auto& ARef=HighMesh->GetRefSkeleton();const auto& BRef=LowMesh->GetRefSkeleton();if(ARef.GetNum()!=BRef.GetNum())return 1;for(int32 B=0;B<ARef.GetNum();++B)if(ARef.GetBoneName(B)!=BRef.GetBoneName(B)||!ARef.GetRefBonePose()[B].Equals(BRef.GetRefBonePose()[B],1e-6))return 1;
   const auto Ref=ReferenceSkinMatrices(HighMesh,HighWalk->Animations[0].Sequence);if(Ref.Num()!=ARef.GetNum())return 1;
   FBindingUVSearch Search(*HighMesh->GetMeshDescription(0),Ref);FSkeletalMeshConstAttributes Original(*HighMesh->GetMeshDescription(0));
   FMeshDescription Desc=*LowMesh->GetMeshDescription(0);FSkeletalMeshAttributes A(Desc);auto Pos=A.GetVertexPositions();auto Weights=A.GetVertexSkinWeights();
   double BeforeRatio=0,AfterRatio=0;const int32 Before=BindingStretchCount(LowMesh,Walk->Animations[0].Sequence,Desc,BeforeRatio);if(Before<0)return 1;
   TMap<FVertexID,FName> Slots;for(auto T:Desc.Triangles().GetElementIDs())for(auto V:Desc.GetTriangleVertices(T))Slots.FindOrAdd(V)=A.GetPolygonGroupMaterialSlotNames()[Desc.GetTrianglePolygonGroup(T)];
   int32 Changed=0,Unmatched=0;double MaximumReferenceError=0;
   for(auto V:Desc.Vertices().GetElementIDs()){
    const auto Instances=Desc.GetVertexVertexInstanceIDs(V);if(Instances.IsEmpty())continue;const auto OldBlend=VertexBlend(Weights.Get(V),Ref);const FVector3f P(OldBlend.TransformPosition(Pos[V]));
    FVertexID Match;float UVError=0;if(!Search.Find(Slots.FindRef(V),P,A.GetVertexInstanceUVs().Get(Instances[0],0),Match,UVError)){++Unmatched;continue;}
    TArray<UE::AnimationCore::FBoneWeight> NewWeights;bool Same=true;int32 K=0;for(const auto W:Original.GetVertexSkinWeights().Get(Match)){NewWeights.Add(W);if(K>=Weights.Get(V).Num()||Weights.Get(V)[K].GetBoneIndex()!=W.GetBoneIndex()||Weights.Get(V)[K].GetRawWeight()!=W.GetRawWeight())Same=false;++K;}Same&=K==Weights.Get(V).Num();if(Same)continue;
    const auto NewBlend=VertexBlend(Original.GetVertexSkinWeights().Get(Match),Ref);if(FMath::Abs(NewBlend.Determinant())<1e-10)return 1;const auto Inverse=NewBlend.Inverse();Pos[V]=FVector3f(Inverse.TransformPosition(P));Weights.SetRaw(V,NewWeights);
    MaximumReferenceError=FMath::Max(MaximumReferenceError,double((FVector3f(NewBlend.TransformPosition(Pos[V]))-P).Size()));
    for(auto I:Instances){A.GetVertexInstanceNormals()[I]=FVector3f(Inverse.TransformVector(OldBlend.TransformVector(A.GetVertexInstanceNormals()[I]))).GetSafeNormal();A.GetVertexInstanceTangents()[I]=FVector3f(Inverse.TransformVector(OldBlend.TransformVector(A.GetVertexInstanceTangents()[I]))).GetSafeNormal();}++Changed;
   }
   const int32 After=BindingStretchCount(LowMesh,Walk->Animations[0].Sequence,Desc,AfterRatio);if(After<0||MaximumReferenceError>.01)return 1;
   auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("index"),Index);Row->SetStringField(TEXT("source_hash"),Low->SourceHash);Row->SetNumberField(TEXT("changed_vertices"),Changed);Row->SetNumberField(TEXT("unmatched_vertices"),Unmatched);Row->SetNumberField(TEXT("suspect_triangles_before"),Before);Row->SetNumberField(TEXT("suspect_triangles_after"),After);Row->SetNumberField(TEXT("maximum_edge_ratio_before"),BeforeRatio);Row->SetNumberField(TEXT("maximum_edge_ratio_after"),AfterRatio);Row->SetNumberField(TEXT("reference_error_cm"),MaximumReferenceError);Row->SetNumberField(TEXT("triangles"),Desc.Triangles().Num());Rows.Add(MakeShared<FJsonValueObject>(Row));
   if(FParse::Param(*Params,TEXT("ResidualDetails"))){
    if(!AuditOnly)return 1;TStrongObjectPtr<UKKImportedCharacter> Current(LoadObject<UKKImportedCharacter>(nullptr,*(Root/TEXT("Character.Character"))));if(!Current||Current->Outfits.Num()!=1)return 1;
    const auto* Part=Current->Outfits[0].Parts.FindByPredicate([](const auto& P){return P.Name==TEXT("PackedVisibleParts");});if(!Part||!Part->SkinnedModel)return 1;
    const auto Detail=BindingStretchDetails(Part->SkinnedModel,Walk->Animations[0].Sequence),HighDetail=BindingStretchDetails(HighMesh,HighWalk->Animations[0].Sequence),HighRigid=BindingStretchDetails(HighMesh,Walk->Animations[0].Sequence),LowOriginal=BindingStretchDetails(Part->SkinnedModel,HighWalk->Animations[0].Sequence);
    if(!Detail||!HighDetail||!HighRigid||!LowOriginal)return 1;Row->SetObjectField(TEXT("published_low"),Detail);Row->SetObjectField(TEXT("original_high"),HighDetail);Row->SetObjectField(TEXT("high_with_low_animation"),HighRigid);Row->SetObjectField(TEXT("low_with_original_animation"),LowOriginal);
    TStrongObjectPtr<UKKImportedCharacter> Native(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("Character.Character"))));if(!Native||Native->Outfits.IsEmpty())return 1;auto Roles=MakeShared<FJsonObject>();for(const auto& NativePart:Native->Outfits[0].Parts)Roles->SetStringField(FString::FromInt(NativePart.NativePartIndex),NativePart.Role+TEXT(":")+NativePart.Name);Row->SetObjectField(TEXT("native_parts"),Roles);
   }
   UE_LOG(LogTemp,Display,TEXT("Crowd binding repair index=%d changed=%d unmatched=%d stretched=%d/%d ratio=%g/%g reference_error=%g"),Index,Changed,Unmatched,Before,After,BeforeRatio,AfterRatio,MaximumReferenceError);
   if(!AuditOnly){if(After>Before)return 1;if(FPackageName::DoesPackageExist(Root/TEXT("Character"))){auto* Existing=LoadObject<UKKImportedCharacter>(nullptr,*(Root/TEXT("Character.Character")));if(!Existing||Existing->SourceHash!=Low->SourceHash)return 1;continue;}
    auto* Card=DuplicateObject<UKKImportedCharacter>(Low.Get(),CreatePackage(*(Root/TEXT("Character"))),TEXT("Character"));TStrongObjectPtr<UKKImportedCharacter> Hold(Card);
    auto* Mesh=DuplicateObject<USkeletalMesh>(LowMesh,CreatePackage(*(Root/TEXT("Body"))),TEXT("Body"));TStrongObjectPtr<USkeletalMesh> SkinHold(Mesh);Mesh->CreateMeshDescription(0,MoveTemp(Desc));Mesh->CommitMeshDescription(0);Mesh->Build();Mesh->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();
    for(auto& Part:Card->Outfits[0].Parts)if(Part.Name==TEXT("PackedVisibleParts"))Part.SkinnedModel=Mesh;
    auto* Library=DuplicateObject<UKKImportedAnimationLibrary>(Walk.Get(),CreatePackage(*(Root/TEXT("Walk/Animations"))),TEXT("Animations"));TStrongObjectPtr<UKKImportedAnimationLibrary> WalkHold(Library);Library->Character=Card;
    if(!SaveCoverageAsset(Mesh)||!SaveCoverageAsset(Library)||!SaveCoverageAsset(Card))return 1;
   }
  }CollectGarbage(RF_NoFlags);
 }
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("audit_only"),AuditOnly);Report->SetArrayField(TEXT("cards"),Rows);FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));return FFileHelper::SaveStringToFile(Text,*Output)?0:1;
}
