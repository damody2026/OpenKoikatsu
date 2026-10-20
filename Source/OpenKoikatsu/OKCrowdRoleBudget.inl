// Included beside FQualityResources: separate group limits, never proportional
// reduction of the face or a whole-character target inherited from old recipes.
int32 BuildCompactRoleBudget(int32 Start,int32 End,const FString& Params){
 const bool Revision4=Params.Contains(TEXT("QualityVariant=GameLowRoleBudgetRigR4"));
 FString Output;if(!FParse::Value(*Params,TEXT("Output="),Output))return 1;
 TArray<TSharedPtr<FJsonValue>> Rows;
 for(int32 Index=Start;Index<End;++Index){
  const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index),Destination=Base/(Revision4?TEXT("GameLowRoleBudgetRigR4"):TEXT("GameLowRoleBudgetRigR2"));
  const FString SourceVariant=Revision4?TEXT("GameLowRoleBudgetR3"):TEXT("GameLowRoleBudgetR1");
  TStrongObjectPtr<UKKImportedCharacter> Source(LoadObject<UKKImportedCharacter>(nullptr,*(Base/(SourceVariant/TEXT("Character.Character")))));
  TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/(SourceVariant/TEXT("Walk/Animations.Animations")))));
  if(!Source||Source->Schema!=(Revision4?1017:1016)||Source->Outfits.Num()!=1||!Walk||Walk->Character!=Source.Get())return 1;
  FString RigIdentity;const FString RigRecipeFile=FPackageName::LongPackageNameToFilename(Destination/TEXT("Character"),TEXT(".recipe.txt"));
  if(Revision4){TArray<uint8> Bytes;FString SourceRecipe;if(!FFileHelper::LoadFileToArray(Bytes,*FPackageName::LongPackageNameToFilename(Base/(SourceVariant/TEXT("Character")),FPackageName::GetAssetPackageExtension()))||!FFileHelper::LoadFileToString(SourceRecipe,*FPackageName::LongPackageNameToFilename(Base/(SourceVariant/TEXT("Character")),TEXT(".recipe.txt"))))return 1;FSHAHash Hash;FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Hash.Hash);RigIdentity=TEXT("RigR4Revision1|")+Hash.ToString()+TEXT("|")+SourceRecipe;}
  FQualityResources Resources(Destination);UKKImportedCharacter* Character=nullptr;
  if(FPackageName::DoesPackageExist(Destination/TEXT("Character")))Character=LoadObject<UKKImportedCharacter>(nullptr,*(Destination/TEXT("Character.Character")));
  if(Revision4&&Character){FString Stored;if(!FFileHelper::LoadFileToString(Stored,*RigRecipeFile)||Stored!=RigIdentity){UE_LOG(LogTemp,Error,TEXT("Outdated compact role rig %s"),*Destination);return 1;}}
  if(!Character){
   Character=Resources.Copy(Source.Get(),TEXT("Unused"));if(!Character)return 1;
   Character->Rename(TEXT("Character"),CreatePackage(*(Destination/TEXT("Character"))));
   auto& Parts=Character->Outfits[0].Parts;const int32 Leader=Parts.IndexOfByPredicate([](const auto& P){return P.SkinnedModel!=nullptr;});if(Leader==INDEX_NONE)return 1;
   TSet<FName> Required;
   // Compact followers first. The leader must retain their complete private
   // hierarchies so the established per-character baked-pose mapping stays exact.
   for(int32 P=0;P<Parts.Num();++P)if(P!=Leader&&Parts[P].SkinnedModel){
    FString Error;auto* Mesh=OKCompactCrowdMesh(Parts[P].SkinnedModel,Destination/FString::Printf(TEXT("Rig/P%04d"),P),Error,{});
    if(!Mesh){UE_LOG(LogTemp,Error,TEXT("Role compact follower index=%d part=%d: %s"),Index,P,*Error);return 1;}
    Resources.Assets.Add(Mesh);Parts[P].SkinnedModel=Mesh;
    for(int32 B=0;B<Mesh->GetRefSkeleton().GetNum();++B)Required.Add(Mesh->GetRefSkeleton().GetBoneName(B));
   }
   FString Error;auto* Mesh=OKCompactCrowdMesh(Parts[Leader].SkinnedModel,Destination/TEXT("Rig/Leader"),Error,Required);
   if(!Mesh){UE_LOG(LogTemp,Error,TEXT("Role compact leader index=%d: %s"),Index,*Error);return 1;}
   Resources.Assets.Add(Mesh);Parts[Leader].SkinnedModel=Mesh;
   if(!AuditReducedWalk(Source.Get(),Walk.Get(),Character))return 1;
   auto* Library=NewObject<UKKImportedAnimationLibrary>(CreatePackage(*(Destination/TEXT("Walk/Animations"))),TEXT("Animations"),RF_Public|RF_Standalone);
   Library->Character=Character;Library->Animations=Walk->Animations;Resources.Assets.Add(Library);
   for(const auto& Asset:Resources.Assets)if(!Resources.Save(Asset.Get()))return 1;
   if(Revision4&&!FFileHelper::SaveStringToFile(RigIdentity,*RigRecipeFile))return 1;
  }
  if(Character->Schema!=(Revision4?1017:1016)||Character->SourceHash!=Source->SourceHash||!AuditReducedWalk(Source.Get(),Walk.Get(),Character))return 1;
  int64 Before=0,After=0,Triangles=0;for(int32 P=0;P<Source->Outfits[0].Parts.Num();++P){
   auto* A=Source->Outfits[0].Parts[P].SkinnedModel.Get();auto* B=Character->Outfits[0].Parts[P].SkinnedModel.Get();if(!A)continue;if(!B)return 1;
   int64 AT=0,BT=0;for(const auto& S:A->GetResourceForRendering()->LODRenderData[0].RenderSections)AT+=S.NumTriangles;for(const auto& S:B->GetResourceForRendering()->LODRenderData[0].RenderSections)BT+=S.NumTriangles;if(AT!=BT||A->GetMaterials()!=B->GetMaterials())return 1;
   Before+=A->GetRefSkeleton().GetNum();After+=B->GetRefSkeleton().GetNum();Triangles+=BT;
  }
  auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("index"),Index);Row->SetNumberField(TEXT("part_reference_bones_before"),Before);Row->SetNumberField(TEXT("part_reference_bones_after"),After);Row->SetNumberField(TEXT("triangles"),Triangles);Row->SetBoolField(TEXT("geometry_materials_preserved"),true);Rows.Add(MakeShared<FJsonValueObject>(Row));
  Resources.Assets.Reset();Resources.Copies.Reset();Source.Reset();Walk.Reset();CollectGarbage(RF_NoFlags);
 }
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),true);Report->SetStringField(TEXT("variant"),Revision4?TEXT("GameLowRoleBudgetRigR4"):TEXT("GameLowRoleBudgetRigR2"));Report->SetArrayField(TEXT("cards"),Rows);FString Json;return FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json))&&FFileHelper::SaveStringToFile(Json,*Output)?0:1;
}
int32 RoleBudgetGroup(const FKKImportedPart& Part){
 const FString Role=Part.Role.ToLower();if(Role==TEXT("head")||Role.Contains(TEXT("eye")))return 0;
 if(Role==TEXT("body"))return 1;if(Role.Contains(TEXT("clothes")))return 2;return 3;
}
int64 RoleTriangles(USkeletalMesh* Mesh){int64 Total=0;const auto* Data=Mesh?Mesh->GetResourceForRendering():nullptr;if(!Data||Data->LODRenderData.IsEmpty())return -1;for(const auto& S:Data->LODRenderData[0].RenderSections)Total+=S.NumTriangles;return Total;}
bool CaptureRoleReference(UKKImportedCharacter* Source,UKKImportedAnimationLibrary* Walk,FQualityResources& Resources){
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);auto* Actor=World->SpawnActorDeferred<AKKImportedCharacterActor>(AKKImportedCharacterActor::StaticClass(),FTransform::Identity);Actor->Character=Source;Actor->AnimationLibrary=Walk;Actor->PlaySkeletalAnimation=true;Actor->FinishSpawning(FTransform::Identity);bool Good=Actor->SetAnimationPosition(0);TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins){auto* Mesh=Skin->GetSkeletalMeshAsset();const auto& Pose=Skin->GetComponentSpaceTransforms();TArray<FMatrix44f> Matrices;const auto Direct=ReferenceSkinMatrices(Mesh,Resources.ReferenceAnimation);double Error=0;for(int32 B=0;B<Pose.Num();++B){Matrices.Add(Mesh->GetRefBasesInvMatrix()[B]*FMatrix44f(Pose[B].ToMatrixWithScale()));if(Direct.IsValidIndex(B))for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Error=FMath::Max(Error,double(FMath::Abs(Matrices[B].M[R][C]-Direct[B].M[R][C])));}Good&=Matrices.Num()==Mesh->GetRefSkeleton().GetNum();Resources.ReferenceOverrides.Add(Mesh,MoveTemp(Matrices));UE_LOG(LogTemp,Display,TEXT("Role evaluated reference mesh=%s matrix_difference=%g"),*Mesh->GetPathName(),Error);}Actor->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return Good&&!Skins.IsEmpty();
}
bool AuditRoleWithRetention(UKKImportedCharacter* Source,UKKImportedAnimationLibrary* Walk,UKKImportedCharacter* Character,FQualityResources& Resources){
 TSet<int32> Retained;for(int32 Attempt=0;Attempt<12;++Attempt){int32 Failed=INDEX_NONE;if(AuditReducedWalk(Source,Walk,Character,&Failed))return true;if(Failed==INDEX_NONE)return false;int32 Skin=0,PartIndex=INDEX_NONE;for(int32 P=0;P<Source->Outfits[0].Parts.Num();++P)if(Source->Outfits[0].Parts[P].SkinnedModel){if(Skin++==Failed){PartIndex=P;break;}}if(PartIndex==INDEX_NONE||Retained.Contains(PartIndex))return false;auto& Part=Character->Outfits[0].Parts[PartIndex];const auto& Original=Source->Outfits[0].Parts[PartIndex];const int32 G=RoleBudgetGroup(Part);int64 Used=0;for(const auto& P:Character->Outfits[0].Parts)if(P.SkinnedModel&&RoleBudgetGroup(P)==G)Used+=RoleTriangles(P.SkinnedModel);const int64 Final=Used-RoleTriangles(Part.SkinnedModel)+RoleTriangles(Original.SkinnedModel),Limit=G==0?MAX_int64:(G==3?30000:15000);if(Final>Limit){UE_LOG(LogTemp,Error,TEXT("Role retention would exceed group budget part=%d group=%d triangles=%lld limit=%lld"),PartIndex,G,Final,Limit);return false;}auto* Mesh=Resources.Copy(Original.SkinnedModel.Get(),TEXT("Preserved"));if(!Mesh)return false;for(auto& M:Mesh->GetMaterials()){M.MaterialInterface=Resources.Material(M.MaterialInterface);if(!M.MaterialInterface)return false;}Part.SkinnedModel=Mesh;Retained.Add(PartIndex);UE_LOG(LogTemp,Display,TEXT("Role retained authored geometry after audit part=%d name=%s group=%d group_triangles=%lld"),PartIndex,*Part.Name,G,Final);}return false;
}

// R3 is a separate immutable output namespace. Existing R1/RigR2 remain usable.
bool RoleHair(const FKKImportedPart& Part){
 const FString Name=Part.NativeRenderer.ToLower();
 return Part.Role.StartsWith(TEXT("hair"))||Name.Contains(TEXT("_kami"))||Name.Contains(TEXT("_hair_"));
}
bool RoleTightClothes(const FKKImportedPart& Part){
 const FString Name=Part.NativeRenderer.ToLower();
 return RoleBudgetGroup(Part)==2&&(Name.Contains(TEXT("tights"))||Name.Contains(TEXT("stocking"))||Name.Contains(TEXT("innerleg")));
}
FString RoleRecipeIdentity(UKKImportedCharacter* Source,UKKImportedAnimationLibrary* Walk){
 FString Key=TEXT("RoleR3Revision1|head-original|15000|15000|30000|reserve64|linear-cloth|hair-source|refill2|")+Source->SourceHash;
 TSet<FString> Files;
 auto Add=[&](UObject* Object){if(Object)Files.Add(FPackageName::LongPackageNameToFilename(Object->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()));};
 Add(Source);Add(Walk);for(const auto& Clip:Walk->Animations)Add(Clip.Sequence);
 for(const auto& Part:Source->Outfits[0].Parts){Key+=TEXT("|")+Part.Role+TEXT("|")+Part.NativeRenderer;Add(Part.SkinnedModel);}
 TArray<FString> Sorted=Files.Array();Sorted.Sort();for(const FString& File:Sorted){TArray<uint8> Data;if(!FFileHelper::LoadFileToArray(Data,*File))return {};FSHAHash Hash;FSHA1::HashBuffer(Data.GetData(),Data.Num(),Hash.Hash);Key+=TEXT("|")+File+TEXT(":")+Hash.ToString();}
 return FMD5::HashAnsiString(*Key);
}
TArray<uint32> AllocateRoleR3(UKKImportedCharacter* Source){
 const auto& Parts=Source->Outfits[0].Parts;TArray<uint32> Budgets;Budgets.SetNumZeroed(Parts.Num());
 for(int32 G=0;G<4;++G){TArray<int32> Pending;int64 Remaining=G==0?MAX_int64:(G==3?30000:15000);
  for(int32 P=0;P<Parts.Num();++P)if(Parts[P].SkinnedModel&&RoleBudgetGroup(Parts[P])==G)Pending.Add(P);
  if(G==0){for(int32 P:Pending)Budgets[P]=uint32(RoleTriangles(Parts[P].SkinnedModel));continue;}
  Remaining-=int64(Pending.Num())*24;const int64 SmallLimit=Remaining/4;int64 Kept=0;TArray<int32> Small=Pending;
  Small.Sort([&](int32 A,int32 B){return RoleTriangles(Parts[A].SkinnedModel)<RoleTriangles(Parts[B].SkinnedModel);});
  for(int32 P:Small){const int64 N=RoleTriangles(Parts[P].SkinnedModel);if(N<=2048&&Kept+N<=SmallLimit){Budgets[P]=uint32(N);Remaining-=N;Kept+=N;Pending.Remove(P);}}
  auto Weight=[&](int32 P){const double N=double(RoleTriangles(Parts[P].SkinnedModel));return G==2?N*(RoleTightClothes(Parts[P])?1.5:1.):FMath::Sqrt(N)*(RoleHair(Parts[P])?1.5:1.);};
  while(!Pending.IsEmpty()){
   double Sum=0;for(int32 P:Pending)Sum+=Weight(P);TArray<int32> Capped;
   for(int32 P:Pending)if(double(Remaining)*Weight(P)/Sum>=RoleTriangles(Parts[P].SkinnedModel))Capped.Add(P);
   if(!Capped.IsEmpty()){for(int32 P:Capped){Budgets[P]=uint32(RoleTriangles(Parts[P].SkinnedModel));Remaining-=Budgets[P];Pending.Remove(P);}continue;}
   for(int32 P:Pending)Budgets[P]=uint32(FMath::Max(12.,FMath::FloorToDouble(double(Remaining)*Weight(P)/Sum)));break;
  }
 }
 return Budgets;
}
bool RolePreservationValid(UKKImportedCharacter* Source,UKKImportedCharacter* Character,const TArray<uint32>& Budgets){
 if(Character->Outfits.Num()!=1||Character->Outfits[0].Parts.Num()!=Source->Outfits[0].Parts.Num())return false;
 for(int32 P=0;P<Budgets.Num();++P){const auto& A=Source->Outfits[0].Parts[P];const auto& B=Character->Outfits[0].Parts[P];if(!A.SkinnedModel)continue;const int64 N=RoleTriangles(A.SkinnedModel);if(!B.SkinnedModel||B.Role!=A.Role||B.NativeRenderer!=A.NativeRenderer)return false;if((RoleBudgetGroup(A)==0||N<=Budgets[P])&&RoleTriangles(B.SkinnedModel)!=N){UE_LOG(LogTemp,Error,TEXT("Role preservation failed part=%d original=%lld output=%lld"),P,N,RoleTriangles(B.SkinnedModel));return false;}}
 return true;
}

int32 BuildRoleBudget(int32 Start,int32 End,const FString& Params){
 const bool Revision3=Params.Contains(TEXT("QualityVariant=GameLowRoleBudgetR3"));
 FString Output;if(!FParse::Value(*Params,TEXT("Output="),Output)||IFileManager::Get().FileExists(*Output))return 1;TArray<TSharedPtr<FJsonValue>> Rows;const int64 Limits[4]={MAX_int64,15000,15000,30000};
 for(int32 Index=Start;Index<End;++Index){
  const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index),Destination=Base/(Revision3?TEXT("GameLowRoleBudgetR3"):TEXT("GameLowRoleBudgetR1"));
  TStrongObjectPtr<UKKImportedCharacter> Source(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("Character.Character"))));TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("WalkRigidBindingsR2/Animations.Animations"))));
  if(!Source||Source->Schema!=2||Source->Outfits.IsEmpty()||!Walk||Walk->Character!=Source.Get()||Walk->Animations.IsEmpty())return 1;
  TArray<TSharedPtr<FJsonValue>> PartRows;int64 Totals[4]={};int64 OriginalTotals[4]={};int32 PartCounts[4]={};
  for(const auto& P:Source->Outfits[0].Parts)if(P.SkinnedModel){const int32 G=RoleBudgetGroup(P);const int64 N=RoleTriangles(P.SkinnedModel);if(N<=0)return 1;OriginalTotals[G]+=N;++PartCounts[G];}
  TArray<uint32> PartBudgets;PartBudgets.SetNumZeroed(Source->Outfits[0].Parts.Num());for(int32 G=1;G<4;++G){TArray<int32> Pending;int64 Remaining=Limits[G]-int64(PartCounts[G])*48;for(int32 P=0;P<Source->Outfits[0].Parts.Num();++P)if(Source->Outfits[0].Parts[P].SkinnedModel&&RoleBudgetGroup(Source->Outfits[0].Parts[P])==G)Pending.Add(P);TArray<int32> Small=Pending;Small.Sort([&](int32 A,int32 B){const bool AH=Source->Outfits[0].Parts[A].Role.StartsWith(TEXT("hair")),BH=Source->Outfits[0].Parts[B].Role.StartsWith(TEXT("hair"));if(AH!=BH)return !AH;return RoleTriangles(Source->Outfits[0].Parts[A].SkinnedModel)<RoleTriangles(Source->Outfits[0].Parts[B].SkinnedModel);});const int64 SmallLimit=Remaining/2;int64 KeptSmall=0;for(int32 P:Small){const auto& Part=Source->Outfits[0].Parts[P];const int64 N=RoleTriangles(Part.SkinnedModel);if(N<=(Part.Role.StartsWith(TEXT("hair"))?1024:2048)&&KeptSmall+N<=SmallLimit){PartBudgets[P]=uint32(N);KeptSmall+=N;Remaining-=N;Pending.Remove(P);}}while(!Pending.IsEmpty()){double Weight=0;for(int32 P:Pending)Weight+=FMath::Sqrt(double(RoleTriangles(Source->Outfits[0].Parts[P].SkinnedModel)));bool Capped=false;for(int32 I=Pending.Num()-1;I>=0;--I){const int32 P=Pending[I];const int64 N=RoleTriangles(Source->Outfits[0].Parts[P].SkinnedModel);if(double(Remaining)*FMath::Sqrt(double(N))/Weight>=N){PartBudgets[P]=uint32(N);Remaining-=N;Pending.RemoveAt(I);Capped=true;}}if(Capped)continue;for(int32 P:Pending)PartBudgets[P]=FMath::Max<uint32>(12,uint32(double(Remaining)*FMath::Sqrt(double(RoleTriangles(Source->Outfits[0].Parts[P].SkinnedModel)))/Weight));break;}}
  if(Revision3)PartBudgets=AllocateRoleR3(Source.Get());
  const FString RecipeIdentity=Revision3?RoleRecipeIdentity(Source.Get(),Walk.Get()):FString();
  const FString RecipeFile=FPackageName::LongPackageNameToFilename(Destination/TEXT("Character"),TEXT(".recipe.txt"));
  if(Revision3&&RecipeIdentity.IsEmpty())return 1;
  TStrongObjectPtr<UKKImportedCharacter> Existing;if(FPackageName::DoesPackageExist(Destination/TEXT("Character"))){Existing.Reset(LoadObject<UKKImportedCharacter>(nullptr,*(Destination/TEXT("Character.Character"))));if(!Existing||Existing->Schema!=(Revision3?1017:1016)||Existing->SourceHash!=Source->SourceHash||!FPackageName::DoesPackageExist(Destination/TEXT("Walk/Animations")))return 1;}
  if(Revision3&&Existing){FString Stored;if(!FFileHelper::LoadFileToString(Stored,*RecipeFile)||Stored!=RecipeIdentity||!RolePreservationValid(Source.Get(),Existing.Get(),PartBudgets)){UE_LOG(LogTemp,Error,TEXT("Outdated role output %s: use a new recipe namespace; original assets retained"),*Destination);return 1;}}
  FQualityResources Resources(Revision3?Destination/(TEXT("Build_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)):Destination);Resources.Lightweight=true;Resources.SingleLow=true;Resources.SurfaceReserve=0;Resources.ProtectAllSurfaces=true;Resources.ReferenceAnimation=Walk->Animations[0].Sequence;
  if(!Existing&&!CaptureRoleReference(Source.Get(),Walk.Get(),Resources))return 1;UKKImportedCharacter* Character=Existing.Get();if(!Character){Character=Resources.Copy(Source.Get(),TEXT("Unused"));if(!Character)return 1;Character->Rename(TEXT("Character"),CreatePackage(*(Destination/TEXT("Character"))));Character->Schema=Revision3?1017:1016;Character->Outfits.SetNum(1);Character->Outfits[0].AnimationSnapshot.Reset();
   for(int32 PartIndex=0;PartIndex<Character->Outfits[0].Parts.Num();++PartIndex){auto& Part=Character->Outfits[0].Parts[PartIndex];Part.Model=nullptr;if(!Part.SkinnedModel)continue;const int32 G=RoleBudgetGroup(Part);auto* Original=Part.SkinnedModel.Get();const int64 N=RoleTriangles(Original);const int64 Available=G==0?N:Limits[G]-int64(PartCounts[G])*48;if(G!=0&&Available<PartCounts[G]*12)return 1;
    const uint32 Budget=G==0?uint32(N):PartBudgets[PartIndex];USkeletalMesh* Mesh=nullptr;
    if(G==0||N<=Budget){Mesh=Resources.Copy(Original,TEXT("SK"));if(!Mesh)return 1;for(auto& Slot:Mesh->GetMaterials()){Slot.MaterialInterface=Resources.Material(Slot.MaterialInterface);if(!Slot.MaterialInterface)return 1;}Mesh->GetLODInfo(0)->LODMaterialMap.Reset();Mesh->Build();FAssetCompilingManager::Get().FinishAllCompilation();}
    else{Resources.SurfaceReserve=FMath::Min<uint32>(Revision3?64:1024,Budget/4);Resources.TriangleBudget=Budget-Resources.SurfaceReserve;Resources.TotalTriangleBudget=Resources.TriangleBudget;Resources.ReusableTriangleBudget=Resources.TriangleBudget;Mesh=Resources.Mesh(Original);if(!Mesh)return 1;}
    if(G==3&&(Revision3?RoleHair(Part):Part.Role.StartsWith(TEXT("hair")))){TSet<UMaterialInterface*> Materials;for(const auto& M:Mesh->GetMaterials())Materials.Add(M.MaterialInterface.Get());int32 Changed=0;auto* Oriented=OKRepairCrowdHairOutline(Mesh,Resources.ReferenceAnimation,Materials,(Revision3?Resources.Root:Destination)/FString::Printf(TEXT("HairOriented/P%04d"),PartIndex),Changed);if(!Oriented)return 1;Mesh=Oriented;}
    Part.SkinnedModel=Mesh;
   }
  }

  if(Revision3&&!Existing){
   // Reclaim unused surface reserve. Each replacement has a hard local budget;
   // the sum of current geometry plus assigned increments never exceeds the cap.
   for(int32 Pass=0;Pass<2;++Pass)for(int32 G=1;G<4;++G){const int64 Limit=Limits[G];int64 Used=0;TArray<int32> Pending;
    for(int32 P=0;P<PartBudgets.Num();++P)if(Source->Outfits[0].Parts[P].SkinnedModel&&RoleBudgetGroup(Source->Outfits[0].Parts[P])==G){const int64 N=RoleTriangles(Character->Outfits[0].Parts[P].SkinnedModel);Used+=N;if(N<RoleTriangles(Source->Outfits[0].Parts[P].SkinnedModel))Pending.Add(P);}
    int64 Spare=Limit-Used-int64(Pending.Num())*24;if(Spare<256||Pending.IsEmpty())continue;
    double Sum=0;for(int32 P:Pending)Sum+=FMath::Sqrt(double(RoleTriangles(Source->Outfits[0].Parts[P].SkinnedModel)));
    for(int32 P:Pending){auto& Part=Character->Outfits[0].Parts[P];const auto& Original=Source->Outfits[0].Parts[P];const int64 Before=RoleTriangles(Part.SkinnedModel),N=RoleTriangles(Original.SkinnedModel);const uint32 Budget=uint32(FMath::Min(N,Before+int64(double(Spare)*FMath::Sqrt(double(N))/Sum)));if(Budget<Before+128)continue;
     USkeletalMesh* Mesh=nullptr;if(Budget>=N){Mesh=Resources.Copy(Original.SkinnedModel.Get(),TEXT("Refill"));if(!Mesh)return 1;for(auto& Slot:Mesh->GetMaterials())Slot.MaterialInterface=Resources.Material(Slot.MaterialInterface);}
     else{Resources.SurfaceReserve=FMath::Min<uint32>(64,Budget/4);Resources.TriangleBudget=Budget-Resources.SurfaceReserve;Resources.TotalTriangleBudget=Resources.TriangleBudget;Resources.ReusableTriangleBudget=Resources.TriangleBudget;Mesh=Resources.Mesh(Original.SkinnedModel);}
     if(!Mesh)return 1;if(RoleTriangles(Mesh)<=Before)continue;if(RoleTriangles(Mesh)>Budget)return 1;
     if(RoleHair(Part)){TSet<UMaterialInterface*> Materials;for(const auto& M:Mesh->GetMaterials())Materials.Add(M.MaterialInterface.Get());int32 Changed=0;Mesh=OKRepairCrowdHairOutline(Mesh,Resources.ReferenceAnimation,Materials,Resources.Root/FString::Printf(TEXT("HairRefill/P%d_%d"),Pass,P),Changed);if(!Mesh)return 1;}
     Part.SkinnedModel=Mesh;
    }
   }
  }
  if(Revision3&&!RolePreservationValid(Source.Get(),Character,PartBudgets))return 1;
  bool ExistingHairChanged=false;for(int32 P=0;Existing&&!Revision3&&P<Character->Outfits[0].Parts.Num();++P){auto& Part=Character->Outfits[0].Parts[P];if(!Part.SkinnedModel||!Part.Role.StartsWith(TEXT("hair")))continue;TSet<UMaterialInterface*> Materials;for(const auto& M:Part.SkinnedModel->GetMaterials())Materials.Add(M.MaterialInterface.Get());const FString Path=Destination/FString::Printf(TEXT("HairOriented/P%04d"),P);USkeletalMesh* Oriented=nullptr;if(FPackageName::DoesPackageExist(Path))Oriented=LoadObject<USkeletalMesh>(nullptr,*(Path+TEXT(".")+FPackageName::GetLongPackageAssetName(Path)));else{int32 Changed=0;Oriented=OKRepairCrowdHairOutline(Part.SkinnedModel,Resources.ReferenceAnimation,Materials,Path,Changed);}if(!Oriented)return 1;if(Oriented!=Part.SkinnedModel){Part.SkinnedModel=Oriented;ExistingHairChanged=true;}}if(ExistingHairChanged){if(!AuditReducedWalk(Source.Get(),Walk.Get(),Character))return 1;const FString File=FPackageName::LongPackageNameToFilename(Destination/TEXT("Character"),FPackageName::GetAssetPackageExtension());FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(Character->GetOutermost(),Character,*File,Args))return 1;}
  if(!Existing&&!AuditRoleWithRetention(Source.Get(),Walk.Get(),Character,Resources))return 1;
  for(int32 P=0;P<Character->Outfits[0].Parts.Num();++P){const auto& Part=Character->Outfits[0].Parts[P];if(!Part.SkinnedModel)continue;const int32 G=RoleBudgetGroup(Part);const int64 N=RoleTriangles(Part.SkinnedModel);if(N<=0)return 1;Totals[G]+=N;if(G==0&&N!=RoleTriangles(Source->Outfits[0].Parts[P].SkinnedModel))return 1;auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("name"),Part.Name);R->SetStringField(TEXT("role"),Part.Role);R->SetNumberField(TEXT("group"),G);R->SetNumberField(TEXT("triangles"),N);R->SetStringField(TEXT("mesh"),Part.SkinnedModel->GetPathName());PartRows.Add(MakeShared<FJsonValueObject>(R));}
  for(int32 G=1;G<4;++G)if(Totals[G]>Limits[G]){UE_LOG(LogTemp,Error,TEXT("Role triangle limit failed index=%d group=%d triangles=%lld limit=%lld"),Index,G,Totals[G],Limits[G]);return 1;}
  if(!Existing){auto* Library=NewObject<UKKImportedAnimationLibrary>(CreatePackage(*(Destination/TEXT("Walk/Animations"))),TEXT("Animations"),RF_Public|RF_Standalone);Resources.Assets.Add(Library);Library->Character=Character;Library->Animations=Walk->Animations;for(int32 Clip=0;Clip<Library->Animations.Num();++Clip){auto* Sequence=BakeRigidClothes(Source.Get(),Library->Animations[Clip],Destination/TEXT("Walk")/FString::Printf(TEXT("RigidClothes_%04d"),Clip));if(!Sequence)return 1;Resources.Assets.Add(Sequence);Library->Animations[Clip].Sequence=Sequence;}FAssetCompilingManager::Get().FinishAllCompilation();for(const auto& Asset:Resources.Assets)if(Asset!=Character&&!Resources.Save(Asset.Get()))return 1;if(!Resources.Save(Character))return 1;if(Revision3&&!FFileHelper::SaveStringToFile(RecipeIdentity,*RecipeFile))return 1;}
  auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("index"),Index);Row->SetStringField(TEXT("character"),Character->GetPathName());Row->SetStringField(TEXT("source_hash"),Source->SourceHash);Row->SetStringField(TEXT("recipe_identity"),RecipeIdentity);Row->SetBoolField(TEXT("preservation_validated"),!Revision3||RolePreservationValid(Source.Get(),Character,PartBudgets));Row->SetNumberField(TEXT("head_triangles"),Totals[0]);Row->SetNumberField(TEXT("body_triangles"),Totals[1]);Row->SetNumberField(TEXT("clothes_triangles"),Totals[2]);Row->SetNumberField(TEXT("hair_accessories_triangles"),Totals[3]);Row->SetBoolField(TEXT("head_geometry_preserved"),Totals[0]==OriginalTotals[0]);Row->SetArrayField(TEXT("parts"),PartRows);Rows.Add(MakeShared<FJsonValueObject>(Row));
  Existing.Reset();Source.Reset();Walk.Reset();Resources.Assets.Reset();Resources.Copies.Reset();CollectGarbage(RF_NoFlags);UE_LOG(LogTemp,Display,TEXT("Role budgets complete index=%d head=%lld body=%lld clothes=%lld hair_accessories=%lld"),Index,Totals[0],Totals[1],Totals[2],Totals[3]);
 }
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),true);Report->SetNumberField(TEXT("recipe"),Revision3?1017:1016);Report->SetNumberField(TEXT("generation_revision"),Revision3?1:8);Report->SetNumberField(TEXT("texture_max"),256);Report->SetBoolField(TEXT("source_assets_modified"),false);Report->SetArrayField(TEXT("cards"),Rows);FString Json;return FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json))&&FFileHelper::SaveStringToFile(Json,*Output)?0:1;
}
