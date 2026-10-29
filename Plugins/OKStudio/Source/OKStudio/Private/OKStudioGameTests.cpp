// OK Studio self tests and comparison runners (console commands in OKStudioGame.cpp).
#include "SOKStudioPanel.h"
#include "OKStudioGameInternal.h"

using OKStudioGameInternal::BoneWorld;

void UOKStudioGame::RunKStudioCompare(const TArray<FString>& Scenes, int32 WaitFrames, bool bExit)
{
	// Each KStudio scene, imported and pictured from its Studio camera after some real frames, beside the picture the
	// original Studio saved in the scene card: Saved/OKStudio/compare/<scene>.png (left original, right OK Studio).
	struct FState { int32 Index = -1; int32 Frame = 0; int32 Done = 0; bool bLoaded = false; bool bPreviousPause = false; };
	TSharedRef<FState> State = MakeShared<FState>();
    State->bPreviousPause = SceneActor && SceneActor->bPauseAnimations;
	TWeakObjectPtr<UOKStudioGame> Self(this);
	// Acceptance runs own their images: another run must not overwrite their evidence.
	FString Out;
	if (!FParse::Value(FCommandLine::Get(), TEXT("OKStudioCompareOutput="), Out) || Out.IsEmpty())
		Out = FPaths::ProjectSavedDir() / TEXT("OKStudio/compare");
	Out = FPaths::ConvertRelativePathToFull(Out);
	IFileManager::Get().MakeDirectory(*Out, true);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, Scenes, Out, WaitFrames, bExit](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		if (State->bLoaded && ++State->Frame < WaitFrames) return true;
		// Runtime materials still compiling draw nothing yet: wait for them (at most ten times the frames asked for).
		if (State->bLoaded && Self->MaterialsPending() && State->Frame < WaitFrames * 10) return true;
		if (State->bLoaded)
		{
			const FString& Path = Scenes[State->Index];
			// -OKStudioCompareWidth=<px>: each half's width (16:9), e.g. 960 for a 1920x540 pair; the card's own picture is 320x180.
			int32 W = 640;
			FParse::Value(FCommandLine::Get(), TEXT("OKStudioCompareWidth="), W);
			W = FMath::Clamp(W, 160, 3840) / 16 * 16;
			const int32 H = W * 9 / 16;
			TArray<FColor> Pixels;
			FString Error;
			Self->UpdateViewer();
			const UCameraComponent* Cam = Self->Viewer->GetCameraComponent();
			// Opt-in, read-only capture of the source view and the actual GPU reference stream.
			const FString GeometryAudit = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_GEOMETRY_AUDIT"));
			if (!GeometryAudit.IsEmpty())
			{
				FString Filter = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_GEOMETRY_MESH"));
				if (Filter.IsEmpty()) Filter = TEXT("cf_O_face");
				TArray<TSharedPtr<FJsonValue>> Rows;
				for (const FOKSceneObject& Object : Self->Document->Objects)
				{
					const AKKPreviewActor* Actor = Self->SceneActor->PreviewOf(Object.Id);
					if (!Actor || Object.Kind != EOKSceneObjectKind::Character) continue;
					for (const FKKLoadedPart& Part : Actor->Parts)
					{
						OkMeshView View{};
						if (!Actor->Session.PartAt(Part.SessionIndex, View) || !FString(UTF8_TO_TCHAR(View.name)).Contains(Filter)) continue;
						auto Row = MakeShared<FJsonObject>();
						Row->SetNumberField(TEXT("object"), Object.Id);
						Row->SetNumberField(TEXT("part"), Part.SessionIndex);
						Row->SetNumberField(TEXT("skel_kind"), View.skel_kind);
						Row->SetNumberField(TEXT("eyes_mode"), int32(Object.Character.EyesLook));
						Row->SetNumberField(TEXT("fixed_eyes_count"), Object.Character.FixedEyes.Num());
						Row->SetStringField(TEXT("mesh"), UTF8_TO_TCHAR(View.name));
						Row->SetBoolField(TEXT("procedural_sphere_normals"), Part.bFaceSphereNormals);
						auto FloatArray = [&](const TCHAR* Key, const float* Data, int32 Count)
						{
							if (!Data) return;
							TArray<TSharedPtr<FJsonValue>> Values;
							Values.Reserve(Count);
							for (int32 I = 0; I < Count; ++I) Values.Add(MakeShared<FJsonValueNumber>(Data[I]));
							Row->SetArrayField(Key, Values);
						};
						FloatArray(TEXT("source_positions"), View.positions, View.n_verts * 3);
						FloatArray(TEXT("source_normals"), View.normals, View.n_verts * 3);
						FloatArray(TEXT("source_reference_normals"), View.reference_normals, View.n_verts * 3);
						FloatArray(TEXT("source_rest_positions"), View.rest_positions, View.n_verts * 3);
						FloatArray(TEXT("source_uv0"), View.uvs, View.n_verts * 2);
						FloatArray(TEXT("source_uv1"), View.uv2, View.n_verts * 2);
						FloatArray(TEXT("eye_gaze"), Actor->EyeGaze, 3);
						auto MorphWeights = MakeShared<FJsonObject>();
						for (const auto& Slot : Actor->MouthSlots)
							if (Slot.SessionPartIndex == Part.SessionIndex && Slot.Comp)
								for (const FName Name : Slot.MorphNames)
									MorphWeights->SetNumberField(Name.ToString(), Slot.BaselineWeights.FindRef(Name) + Slot.Comp->GetMorphTarget(Name) * 100.f);
						Row->SetObjectField(TEXT("effective_morph_percent"), MorphWeights);
						TInlineComponentArray<USkinnedMeshComponent*> Components;
						Actor->GetComponents(Components);
						for (const USkinnedMeshComponent* Component : Components)
						{
							const bool IsFaceSurface = Actor->MouthSlots.ContainsByPredicate([&](const auto& Slot) { return Slot.SessionPartIndex == Part.SessionIndex && Slot.Comp == Component; });
							if ((!IsFaceSurface && !Component->ComponentHasTag(FName(*FString::Printf(TEXT("KKSessionPart_%d"), Part.SessionIndex)))) || !Component->IsVisible() || Component->bHiddenInGame) continue;
							const USkinnedAsset* Asset = Component->GetSkinnedAsset();
							const FSkeletalMeshRenderData* Data = Asset ? Asset->GetResourceForRendering() : nullptr;
							if (!Data || Data->LODRenderData.IsEmpty()) continue;
							const auto& LOD = Data->LODRenderData[0];
							TArray<float> Normals, Positions, UVs;
							for (uint32 I = 0; I < LOD.GetNumVertices(); ++I)
							{
								const FVector3f N = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(I);
								const FVector3f P = LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(I);
								const FVector2f UV = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I, 0);
								// Return the reference stream to Unity coordinates; these are not posed normals.
								Normals.Append({-N.X, N.Z, N.Y});
								Positions.Append({-P.X * .01f, P.Z * .01f, P.Y * .01f});
								UVs.Append({UV.X, UV.Y});
							}
							Row->SetStringField(TEXT("component"), Component->GetName());
							FloatArray(TEXT("gpu_reference_normals_unity"), Normals.GetData(), Normals.Num());
							FloatArray(TEXT("gpu_reference_positions_unity"), Positions.GetData(), Positions.Num());
							FloatArray(TEXT("gpu_uv0"), UVs.GetData(), UVs.Num());
							TArray<FMatrix44f> RefToLocal;
							Component->GetCurrentRefToLocalMatrices(RefToLocal, 0);
							TArray<float> WorldNormals;
							WorldNormals.SetNumZeroed(LOD.GetNumVertices() * 3);
							FMatrix LocalToWorld = Component->GetComponentTransform().ToMatrixWithScale();
							// GpuSkinVertexFactory::CalcTangentToWorld removes primitive scale.
							LocalToWorld.RemoveScaling();
							for (const auto& Section : LOD.RenderSections)
								for (uint32 Vertex = Section.BaseVertexIndex; Vertex < Section.BaseVertexIndex + Section.NumVertices; ++Vertex)
								{
									const FVector3f N = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(Vertex);
									FVector4f Sum(0, 0, 0, 0);
									for (uint32 Influence = 0; Influence < LOD.SkinWeightVertexBuffer.GetMaxBoneInfluences(); ++Influence)
									{
										const uint32 Bone = LOD.SkinWeightVertexBuffer.GetBoneIndex(Vertex, Influence);
										const uint16 Weight = LOD.SkinWeightVertexBuffer.GetBoneWeight(Vertex, Influence);
										if (!Weight || !Section.BoneMap.IsValidIndex(Bone) || !RefToLocal.IsValidIndex(Section.BoneMap[Bone])) continue;
										Sum += RefToLocal[Section.BoneMap[Bone]].TransformFVector4(FVector4f(N.X, N.Y, N.Z, 0)) * (float(Weight) / 65535.f);
									}
									const FVector World = FVector(LocalToWorld.TransformVector(FVector(Sum.X, Sum.Y, Sum.Z))).GetSafeNormal();
									WorldNormals[Vertex * 3] = -World.X;
									WorldNormals[Vertex * 3 + 1] = World.Z;
									WorldNormals[Vertex * 3 + 2] = World.Y;
								}
							FloatArray(TEXT("cpu_reference_to_local_world_normals_unity"), WorldNormals.GetData(), WorldNormals.Num());
						}
						Rows.Add(MakeShared<FJsonValueObject>(Row));
					}
				}
				FString Json;
				FJsonSerializer::Serialize(Rows, TJsonWriterFactory<>::Create(&Json));
				IFileManager::Get().MakeDirectory(*GeometryAudit, true);
				FFileHelper::SaveStringToFile(Json, *(GeometryAudit / (FPaths::GetBaseFilename(Path) + TEXT(".geometry.json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			}
			const FString MaterialAudit = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_MATERIAL_AUDIT"));
			if (!MaterialAudit.IsEmpty())
			{
				TArray<TSharedPtr<FJsonValue>> Rows;
				for (const FOKSceneObject& Object : Self->Document->Objects)
				{
					const AKKPreviewActor* Actor = Self->SceneActor->PreviewOf(Object.Id);
					if (!Actor || Object.Kind != EOKSceneObjectKind::Character) continue;
					for (const FKKLoadedPart& Part : Actor->Parts)
					{
						OkMeshView View{};
						if (!Part.SurfaceMat || !Actor->Session.PartAt(Part.SessionIndex, View) || (View.shader_kind != 1 && View.shader_kind != 10 && View.shader_kind != 5 && View.shader_kind != 7)) continue;
						auto Row = MakeShared<FJsonObject>();
						Row->SetNumberField(TEXT("object"), Object.Id);
						Row->SetStringField(TEXT("mesh"), UTF8_TO_TCHAR(View.name));
						Row->SetStringField(TEXT("material"), Part.SurfaceMat->GetPathName());
						if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_MATERIAL_AUDIT_ALL")) == TEXT("1"))
						{
							auto Scalars = MakeShared<FJsonObject>();
							auto Vectors = MakeShared<FJsonObject>();
							auto Textures = MakeShared<FJsonObject>();
							TArray<FMaterialParameterInfo> Infos;
							TArray<FGuid> Ids;
							Part.SurfaceMat->GetAllScalarParameterInfo(Infos, Ids);
							for (const auto& Info : Infos) { float V; if (Part.SurfaceMat->GetScalarParameterValue(Info, V)) Scalars->SetNumberField(Info.Name.ToString(), V); }
							Part.SurfaceMat->GetAllVectorParameterInfo(Infos, Ids);
							for (const auto& Info : Infos)
							{
								FLinearColor V;
								if (!Part.SurfaceMat->GetVectorParameterValue(Info, V)) continue;
								TArray<TSharedPtr<FJsonValue>> Components;
								for (int32 C = 0; C < 4; ++C) Components.Add(MakeShared<FJsonValueNumber>((&V.R)[C]));
								Vectors->SetArrayField(Info.Name.ToString(), Components);
							}
							Part.SurfaceMat->GetAllTextureParameterInfo(Infos, Ids);
							for (const auto& Info : Infos)
							{
								UTexture* T = nullptr;
								if (!Part.SurfaceMat->GetTextureParameterValue(Info, T) || !T) continue;
								auto Texture = MakeShared<FJsonObject>();
								Texture->SetStringField(TEXT("path"), T->GetPathName());
								Texture->SetBoolField(TEXT("srgb"), T->SRGB);
								Texture->SetNumberField(TEXT("width"), T->GetSurfaceWidth());
								Texture->SetNumberField(TEXT("height"), T->GetSurfaceHeight());
								Textures->SetObjectField(Info.Name.ToString(), Texture);
							}
							Row->SetObjectField(TEXT("all_scalars"), Scalars);
							Row->SetObjectField(TEXT("all_vectors"), Vectors);
							Row->SetObjectField(TEXT("all_textures"), Textures);
						}
						for (const TCHAR* Key : { TEXT("_LightColor0"), TEXT("_WorldSpaceLightPos0"), TEXT("_ambientshadowG"), TEXT("unity_SHAr"), TEXT("unity_SHAg"), TEXT("unity_SHAb"), TEXT("unity_SHBr"), TEXT("unity_SHBg"), TEXT("unity_SHBb"), TEXT("unity_SHC"), TEXT("_Color"), TEXT("_ShadowColor"), TEXT("_GlossColor"), TEXT("_HairGloss_ST"), TEXT("_texcoord2_ST"), TEXT("_overcolor1"), TEXT("_overcolor2") })
						{
							FLinearColor Value;
							if (!Part.SurfaceMat->GetVectorParameterValue(FMaterialParameterInfo(Key), Value)) continue;
							TArray<TSharedPtr<FJsonValue>> Components;
							for (int32 C = 0; C < 4; ++C) Components.Add(MakeShared<FJsonValueNumber>((&Value.R)[C]));
							Row->SetArrayField(Key, Components);
							float Alpha = 0.f;
							const FString AlphaKey = FString(Key) + TEXT("__A");
							if (Part.SurfaceMat->GetScalarParameterValue(FMaterialParameterInfo(*AlphaKey), Alpha)) Row->SetNumberField(AlphaKey, Alpha);
						}
						for (const TCHAR* Key : { TEXT("unity_SpecCube0"), TEXT("unity_SpecCube1"), TEXT("_RampG") })
						{
							UTexture* Texture = nullptr;
							if (Part.SurfaceMat->GetTextureParameterValue(FMaterialParameterInfo(Key), Texture) && Texture)
							{
								Row->SetStringField(Key, Texture->GetPathName());
								Row->SetNumberField(FString(Key)+TEXT("_width"), Texture->GetSurfaceWidth());
								Row->SetStringField(FString(Key)+TEXT("_class"), Texture->GetClass()->GetName());
								Row->SetBoolField(FString(Key)+TEXT("_srgb"), Texture->SRGB);
								if (FString(Key)==TEXT("_RampG")) Row->SetBoolField(TEXT("ramp_matches_scene_override"),Texture==Actor->RampOverride);
							}
						}
						Rows.Add(MakeShared<FJsonValueObject>(Row));
					}
				}
				FString Json;
				FJsonSerializer::Serialize(Rows, TJsonWriterFactory<>::Create(&Json));
				IFileManager::Get().MakeDirectory(*MaterialAudit, true);
				FFileHelper::SaveStringToFile(Json, *(MaterialAudit / (FPaths::GetBaseFilename(Path) + TEXT(".materials.json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			}
			if (!FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_POSE_DUMP")).IsEmpty())
				UE_LOG(LogTemp, Display, TEXT("OK studio camera: view=%s horizontal_fov=%.9f aspect=%.9f active=%d"), *Cam->GetComponentTransform().ToString(), Cam->FieldOfView, Cam->AspectRatio, Self->Document->ActiveCamera);
			FImage Original;
			if (FOKSceneCard::Capture(Self->SceneActor, Self->Viewer->GetActorTransform(), Cam->FieldOfView, &Cam->PostProcessSettings, W, H, false, Pixels, Error)
				&& (FImageUtils::LoadImage(*Path, Original) || (Path.EndsWith(TEXT(".okscene")) && (Original.Init(W, H, ERawImageFormat::BGRA8, EGammaSpace::sRGB), FMemory::Memzero(Original.RawData.GetData(), Original.RawData.Num()), true))))
			{
				FImage Left;
				Original.ResizeTo(Left, W, H, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
				TArray<FColor> Side;
				Side.SetNumUninitialized(W * 2 * H);
				const TArrayView64<FColor> L = Left.AsBGRA8();
				for (int32 Y = 0; Y < H; ++Y)
					for (int32 X = 0; X < W; ++X) { Side[Y * W * 2 + X] = L[Y * W + X]; Side[Y * W * 2 + W + X] = Pixels[Y * W + X]; }
				for (FColor& C : Side) C.A = 255;
				TArray64<uint8> Png;
				FImageUtils::PNGCompressImageArray(W * 2, H, TArrayView64<const FColor>(Side.GetData(), Side.Num()), Png);
				const FString Output = Out / (FPaths::GetBaseFilename(Path) + TEXT(".png"));
				if (FFileHelper::SaveArrayToFile(Png, *Output)) ++State->Done;
				else Error = TEXT("cannot write ") + Output;
				// OK_STUDIO_COMPARE_DUMP=<text>: the materials (blend mode, parent) of the mesh components whose name contains it.
				const FString Dump = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_COMPARE_DUMP"));
				if (!Dump.IsEmpty())
					for (const FOKSceneObject& O : Self->Document->Objects)
						if (AKKPreviewActor* Preview = Self->SceneActor->PreviewOf(O.Id))
						{
							TInlineComponentArray<UMeshComponent*> Meshes(Preview);
							for (UMeshComponent* Mesh : Meshes)
								for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
									if (UMaterialInterface* M = Mesh->GetMaterial(Slot); M && (O.Name.Contains(Dump) || Mesh->GetName().Contains(Dump)))
									{
										FString Values;
										for (const TCHAR* Name : { TEXT("_Color"), TEXT("_Color2"), TEXT("_Color3"), TEXT("_Color4"), TEXT("_ShadowColor") })
										{
											FLinearColor V;
											if (M->GetVectorParameterValue(FMaterialParameterInfo(Name), V)) Values += FString::Printf(TEXT(" %s=(%.3f,%.3f,%.3f,%.3f)"), Name, V.R, V.G, V.B, V.A);
										}
										for (const TCHAR* Name : { TEXT("_MainTex"), TEXT("_ColorMask") })
										{
											UTexture* T = nullptr;
											if (!M->GetTextureParameterValue(FMaterialParameterInfo(Name), T)) continue;
											const UTexture2D* T2 = Cast<UTexture2D>(T);
											Values += FString::Printf(TEXT(" %s=%s(%dx%d address %d,%d srgb %d)"), Name, T ? *T->GetName() : TEXT("none"), T2 ? T2->GetSizeX() : 0, T2 ? T2->GetSizeY() : 0,
												T2 ? int32(T2->AddressX) : -1, T2 ? int32(T2->AddressY) : -1, T ? int32(T->SRGB) : -1);
										}
										UE_LOG(LogTemp, Display, TEXT("OK studio compare dump: %s %s slot %d %s blend=%d base=%s visible=%d%s"), *O.Name, *Mesh->GetName(), Slot, *M->GetName(),
											int32(M->GetBlendMode()), *M->GetMaterial()->GetName(), Mesh->IsVisible() ? 1 : 0, *Values);
									}
						}
                if (!FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_POSE_DUMP")).IsEmpty())
                    for (const FOKSceneObject& O : Self->Document->Objects)
                        if (AKKPreviewActor* P = Self->SceneActor->PreviewOf(O.Id))
                        {
                            UE_LOG(LogTemp, Display, TEXT("OK studio pose: %s world=%s anime=%d/%d/%d time=%.6f"), *O.Name, *P->GetActorTransform().ToString(), O.Character.StudioAnimeGroup, O.Character.StudioAnimeCategory, O.Character.StudioAnimeNo, O.Character.Time);
                            for (const FOKSceneIkTarget& T : O.Character.Ik)
                            {
                                FVector Actual;
                                if (P->Session.IkEffectorPosition(T.Effector, Actual))
                                    UE_LOG(LogTemp, Display, TEXT("OK studio pose: %s effector=%d target=%s actual=%s"), *O.Name, T.Effector, *AKKPreviewActor::ActorLocalToUnity(T.Position).ToString(), *Actual.ToString());
                            }
                            for (const TCHAR* B : { TEXT("cf_j_hips"), TEXT("cf_j_spine03"), TEXT("cf_j_head") })
                            {
                                float M[16];
                                if (P->Session.BoneWorldMatrix(B, M)) UE_LOG(LogTemp, Display, TEXT("OK studio pose: %s bone=%s xyz=(%.6f,%.6f,%.6f)"), *O.Name, B, M[3], M[7], M[11]);
                            }
                        }
				// OK_STUDIO_COMPARE_WIDE=<factor>: the same view pulled back along its direction (what lies outside the frame).
				const float Wide = FCString::Atof(*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_COMPARE_WIDE")));
				if (Wide > 1.f)
				{
					FTransform Back = Self->Viewer->GetActorTransform();
					const FVector Focus = Back.GetLocation() + Back.GetRotation().GetForwardVector() * 200.f;
					Back.SetLocation(Focus - Back.GetRotation().GetForwardVector() * 200.f * Wide);
					TArray<FColor> WidePixels;
					if (FOKSceneCard::Capture(Self->SceneActor, Back, Cam->FieldOfView, &Cam->PostProcessSettings, W, H, false, WidePixels, Error))
					{
						for (FColor& C : WidePixels) C.A = 255;
						TArray64<uint8> WidePng;
						FImageUtils::PNGCompressImageArray(W, H, TArrayView64<const FColor>(WidePixels.GetData(), WidePixels.Num()), WidePng);
						FFileHelper::SaveArrayToFile(WidePng, *(Out / (FPaths::GetBaseFilename(Path) + TEXT("_wide.png"))));
					}
				}
			}
			UE_LOG(LogTemp, Display, TEXT("OK studio compare: %s %s %s (memory %.1f GB)"), *FPaths::GetCleanFilename(Path), Error.IsEmpty() ? TEXT("pictured") : TEXT("failed"), *Error, FPlatformMemory::GetStats().UsedPhysical / double(1 << 30));
		}
		if (++State->Index >= Scenes.Num())
		{
			UE_LOG(LogTemp, Display, TEXT("OK studio compare: RESULT %d / %d pictured into %s"), State->Done, Scenes.Num(), *Out);
            if (Self->SceneActor) Self->SceneActor->bPauseAnimations = State->bPreviousPause;
			if (bExit)
			{
				Self->ReleaseSceneResources();
				UE_LOG(LogTemp, Display, TEXT("OK studio compare: final scene released (memory %.1f GB)"), FPlatformMemory::GetStats().UsedPhysical / double(1 << 30));
				FPlatformMisc::RequestExit(false);
			}
			return false;
		}
		FString LoadError;
		// Remove only this run's named output: a failed import must never leave
		// an older picture looking like a successful comparison.
		IFileManager::Get().Delete(*(Out / (FPaths::GetBaseFilename(Scenes[State->Index]) + TEXT(".png"))), false, true);
		State->bLoaded = Self->Load(Scenes[State->Index], LoadError);
		if (!State->bLoaded)
		{
			UE_LOG(LogTemp, Warning, TEXT("OK studio compare: %s not opened: %s"), *Scenes[State->Index], *LoadError);
			return true;
		}
		// Diagnostic ablations change only this comparison's in-memory document.
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_COMPARE_BLOOM")) == TEXT("0")) Self->Document->Post.bBloom = false;
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_COMPARE_DEPTH")) == TEXT("0")) Self->Document->Post.bDepthOfField = false;
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_COMPARE_FILTER")) == TEXT("0")) Self->Document->Post.ColorFilter = -1;
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_COMPARE_AO")) == TEXT("0")) Self->Document->Post.bAmbientOcclusion = false;
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_COMPARE_VIGNETTE")) == TEXT("0")) Self->Document->Post.bVignette = false;
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_COMPARE_FOG")) == TEXT("0")) Self->Document->Post.bFog = false;
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_COMPARE_SUNSHAFTS")) == TEXT("0")) Self->Document->Post.bSunShafts = false;
		// OK_STUDIO_COMPARE_HIDE=<group>/<category>/<no>[,...]: those catalogue items hidden (what they cover); `*` matches
		// any part ("1/6/*"), and "#<id>" hides one object.
		TArray<FString> Hide;
		FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_COMPARE_HIDE")).ParseIntoArray(Hide, TEXT(","));
		for (FOKSceneObject& O : Self->Document->Objects)
		{
			const FString Key = FString::Printf(TEXT("%d/%d/%d"), O.Item.Group, O.Item.Category, O.Item.No);
			for (const FString& H : Hide)
				if (H == FString::Printf(TEXT("#%d"), O.Id) || (O.Kind == EOKSceneObjectKind::Item && Key.MatchesWildcard(H))) O.bVisible = false;
		}
		Self->SceneActor->bPauseAnimations = true;
        Self->Document->ItemAnimationSeconds = 0.f;
        Self->SyncNow();
		Self->UpdateViewer();
		State->Frame = 0;
		return true;
	}));
}

void UOKStudioGame::RunStressItems(int32 Count, const FString& Item, bool bExit)
{
	TArray<FString> Id;
	Item.ParseIntoArray(Id, TEXT("/"));
	if (Id.Num() != 3) { UE_LOG(LogTemp, Error, TEXT("OK studio stress: item must be group/category/no")); if (bExit) FPlatformMisc::RequestExit(false); return; }
	struct FState { int32 Added = 0; int32 Wait = 0; };
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<UOKStudioGame> Self(this);
	const int32 G = FCString::Atoi(*Id[0]), C = FCString::Atoi(*Id[1]), N = FCString::Atoi(*Id[2]);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, Count, G, C, N, bExit](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		if (++State->Wait < 10) return true;
		State->Wait = 0;
		// Live objects by class (what each new copy keeps), and the process' memory.
		auto Live = [](UClass* Class) { int32 K = 0; for (TObjectIterator<UObject> It; It; ++It) if (It->IsA(Class)) ++K; return K; };
		const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
		UE_LOG(LogTemp, Display, TEXT("OK studio stress: items=%d used=%.2f GB textures=%d materials=%d mids=%d meshes=%d actors=%d"),
			State->Added, Mem.UsedPhysical / double(1 << 30), Live(UTexture2D::StaticClass()), Live(UMaterial::StaticClass()),
			Live(UMaterialInstanceDynamic::StaticClass()), Live(UMeshComponent::StaticClass()), Live(AActor::StaticClass()));
		if (State->Added >= Count) { UE_LOG(LogTemp, Display, TEXT("OK studio stress: RESULT done")); if (bExit) FPlatformMisc::RequestExit(false); return false; }
		for (int32 K = 0; K < 50 && State->Added < Count; ++K, ++State->Added)
		{
			const int32 Obj = Self->Document->AddObject(EOKSceneObjectKind::Item, TEXT("stress"), -1);
			FOKSceneObject* O = Self->Document->Find(Obj);
			O->Item.Group = G; O->Item.Category = C; O->Item.No = N;
			O->Transform = FTransform(FVector((State->Added % 25) * 30.0, (State->Added / 25) * 30.0, 0.0));
		}
		Self->Document->NotifyChanged();
		Self->SyncNow();
		return true;
	}));
}

bool UOKStudioGame::MaterialsPending() const
{
	if (SceneActor && SceneActor->HasPendingBuilds()) return true;
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) return true;
	if (!SceneActor || !Document) return false;
	for (const FOKSceneObject& O : Document->Objects)
		if (const AKKPreviewActor* P = SceneActor->PreviewOf(O.Id); P && !P->PendingPreviewMaterials.IsEmpty()) return true;
	return false;
}

void UOKStudioGame::RunEyeSetTest(bool bExit)
{
	// One character pictured at the face with eyes patterns 0 (default), 23 (swirl gag eyes), 25 (hearts in the
	// pupils) and 34 (right eye squeezed shut, blend shape pattern 23): the gag eye shows only for 23, the pupils hide for it, both
	// special patterns change the face picture, and the face takes each set's blend shape pattern.
	Select(-1);
	const int32 Id = Add(EOKSceneObjectKind::Character);
	// C002 first (the card the test was written with); when a card cannot be opened here (a local card set without all
	// of its resources), the next local card is tried: the test is about eye sets, not one card.
	TArray<FString> Cards = { TEXT("Content/CharacterCards/CrowdAPNG/C002.png") };
	for (const FString& C : AvailableCards()) if (C.Contains(TEXT("CrowdAPNG/")) && !Cards.Contains(C) && Cards.Num() < 12) Cards.Add(C);
	Document->Find(Id)->Character.Card = Cards[0];
	Document->Find(Id)->Character.EyesPattern = 0;
	struct FState { TArray<FString> Cards; int32 Card = 0; int32 Frame = 0; int32 Step = 0; TArray<FColor> Plain; TArray<FColor> Swirl; bool bSwirlTaken = false; TArray<FString> Lines; int32 Failures = 0; TOptional<FTransform> View; };
	TSharedRef<FState> State = MakeShared<FState>();
	State->Cards = Cards;
	TWeakObjectPtr<UOKStudioGame> Self(this);
	Document->NotifyChanged();
	SyncNow();
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, Id, bExit](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		const int32 Frame = ++State->Frame;
		if (Frame < 60 || (Self->MaterialsPending() && Frame < 900)) return true;
		if (State->Step == 0 && !State->View && Self->SceneActor->Errors.Contains(Id) && State->Card + 1 < State->Cards.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("OK studio eye set test: %s not shown (%s); trying %s"), *State->Cards[State->Card], *Self->SceneActor->Errors[Id], *State->Cards[State->Card + 1]);
			Self->Document->Find(Id)->Character.Card = State->Cards[++State->Card];
			Self->Document->NotifyChanged();
			Self->SyncNow();
			State->Frame = 0;
			return true;
		}
		static const int32 Patterns[] = { 0, 23, 25, 34 };
		auto Check = [&State](bool bOk, const FString& What) { State->Lines.Add(FString::Printf(TEXT("%s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); if (!bOk) ++State->Failures; };
		AKKPreviewActor* Preview = Self->SceneActor->PreviewOf(Id);
		// The face from the front: the eye in front of the face's centre gives the facing.
		FBox Face(ForceInit), Eye(ForceInit);
		int32 GagShown = 0, PupilShown = 0;
		if (Preview)
		{
			// Parts by their session name (components are named Part_n / Skel_n): shown ones are built. A part with
			// morphs (face, eyes, gag eyes) draws through its mouth slot's skeletal component; its own one is hidden.
			TMap<int32, UPrimitiveComponent*> Drawn;
			for (const AKKPreviewActor::FOkMouthSlot& Slot : Preview->MouthSlots) if (Slot.Comp) Drawn.Add(Slot.SessionPartIndex, Slot.Comp);
			for (const FKKLoadedPart& Part : Preview->Parts)
			{
				OkMeshView View;
				FMemory::Memzero(View);
				if (Part.SessionIndex == INDEX_NONE || !Preview->Session.PartAt(Part.SessionIndex, View) || !View.name || !View.render_visible) continue;
				const FString N = FString(UTF8_TO_TCHAR(View.name)).ToLower();
				UPrimitiveComponent* Comp = Drawn.FindRef(Part.SessionIndex);
				if (!Comp) Comp = Part.Comp;
				const FBox Bounds = Comp ? Comp->Bounds.GetBox() : FBox(ForceInit);
				if (N == TEXT("cf_o_face")) Face += Bounds;
				// Both eye whites: their middle is in front of the face's centre.
				if (N.StartsWith(TEXT("cf_ohitomi_")) && !N.Contains(TEXT("02"))) Eye += Bounds;
				if (N.Contains(TEXT("gag_eye")))
				{
					++GagShown;
					UMaterialInterface* M = Comp ? Comp->GetMaterial(0) : nullptr;
					UE_LOG(LogTemp, Display, TEXT("OK studio eye set test: gag part %s material %s (base %s)"), *N, M ? *M->GetName() : TEXT("none"),
						M && M->GetBaseMaterial() ? *M->GetBaseMaterial()->GetName() : TEXT("none"));
				}
				if (N.StartsWith(TEXT("cf_ohitomi_")) && N.Contains(TEXT("02"))) ++PupilShown;
			}
			UE_LOG(LogTemp, Display, TEXT("OK studio eye set test: eyes %d parts=%d eye patterns=%d face=%d"), Patterns[State->Step], Preview->Parts.Num(), Preview->GetFacePatternCount(1), Preview->FacePattern[0]);
		}
		TArray<FColor> Pixels;
		// The view is found once, with the default eyes (an eye set may hide the eye whites).
		if (!State->View && Face.IsValid && Eye.IsValid)
		{
			const FVector Front = (Eye.GetCenter() - Face.GetCenter()).GetSafeNormal2D();
			const FVector Target = Eye.GetCenter();
			State->View = FTransform(FRotationMatrix::MakeFromX(-Front).ToQuat(), Target + Front * 45.f);
		}
		if (State->View)
		{
			FString Error;
			FOKSceneCard::Capture(Self->SceneActor, *State->View, 40.f, &Self->Viewer->GetCameraComponent()->PostProcessSettings, 320, 180, false, Pixels, Error);
			TArray64<uint8> Png;
			if (Pixels.Num() == 320 * 180)
			{
				FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
				FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("OKStudio/eyeset_%d.png"), Patterns[State->Step])));
			}
		}
		auto Differs = [&State, &Pixels]() { int32 N = 0; for (int32 K = 0; K < Pixels.Num() && K < State->Plain.Num(); ++K) { const FColor& A = State->Plain[K]; const FColor& B = Pixels[K]; N += FMath::Abs(A.R - B.R) + FMath::Abs(A.G - B.G) + FMath::Abs(A.B - B.B) > 60 ? 1 : 0; } return N; };
		const int32 Ptn = Patterns[State->Step];
		const int32 Blend = FOKStudioCatalog::Get().EyeSet(Ptn).BlendPattern;
		if (Ptn == 0) { State->Plain = Pixels; Check(Pixels.Num() > 0 && GagShown == 0 && PupilShown > 0, FString::Printf(TEXT("eyes 0: face pictured, gag eyes %d, pupils %d"), GagShown, PupilShown)); }
		else if (Ptn == 23 && !State->bSwirlTaken)
		{
			// The swirl turns (its gag material's _AngleSpeed -0.5): pictured again a second later. Taken once even when
			// the picture failed (no view), so the test ends with a failure instead of retrying forever.
			State->bSwirlTaken = true;
			State->Swirl = Pixels;
			const int32 D = Differs();
			Check(GagShown == 1 && PupilShown == 0 && D > 50, FString::Printf(TEXT("eyes 23 (swirl): gag eyes %d, pupils %d, %d pixels changed"), GagShown, PupilShown, D));
			State->Frame = 50;
			return true;
		}
		else if (Ptn == 23)
		{
			int32 N = 0;
			for (int32 K = 0; K < Pixels.Num() && K < State->Swirl.Num(); ++K)
				N += FMath::Abs(State->Swirl[K].R - Pixels[K].R) + FMath::Abs(State->Swirl[K].G - Pixels[K].G) + FMath::Abs(State->Swirl[K].B - Pixels[K].B) > 30 ? 1 : 0;
			// Half a turn changes the swirls; hair sway alone changes about 150 pixels of this picture.
			Check(N > 500, FString::Printf(TEXT("eyes 23 (swirl): turns, %d pixels changed in a second"), N));
			// eyeset_23.png is the second picture; the first one beside it.
			TArray64<uint8> First;
			if (State->Swirl.Num() == 320 * 180)
			{
				FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(State->Swirl.GetData(), State->Swirl.Num()), First);
				FFileHelper::SaveArrayToFile(First, *(FPaths::ProjectSavedDir() / TEXT("OKStudio/eyeset_23_first.png")));
			}
		}
		else if (Ptn == 25) { const int32 D = Differs(); Check(GagShown == 0 && PupilShown > 0 && D > 10, FString::Printf(TEXT("eyes 25 (hearts in the pupils): gag eyes %d, %d pixels changed"), GagShown, D)); }
		else Check(Blend == 23 && Preview && Preview->FacePattern[0] == 23, FString::Printf(TEXT("eyes 34 (right eye squeezed shut): blend shape pattern %d, face %d"), Blend, Preview ? Preview->FacePattern[0] : -1));
		if (State->Step + 1 < UE_ARRAY_COUNT(Patterns))
		{
			++State->Step;
			State->Frame = 0;
			Self->Document->Find(Id)->Character.EyesPattern = Patterns[State->Step];
			Self->Document->NotifyChanged();
			Self->SyncNow();
			return true;
		}
		for (const FString& L : State->Lines) UE_LOG(LogTemp, Display, TEXT("OK studio eye set test: %s"), *L);
		UE_LOG(LogTemp, Display, TEXT("OK studio eye set test: RESULT %s"), State->Failures ? TEXT("FAIL") : TEXT("PASS"));
		if (bExit) FPlatformMisc::RequestExit(false);
		return false;
	}), 0.1f);
}

void UOKStudioGame::RunSkyTest(bool bExit)
{
	// No map, no background: each sky pattern pictured looking up and towards the horizon, then with the sky off.
	// Every pattern draws (the patterns differ), the day sky is blue above, the night sky dark, and with the sky off
	// the view changes (the dome is gone). Last a map with its own sky (MapSkyInfo, map 50) with the scene's sky off:
	// the dome shows the map's sky.
	Select(-1);
	Document->Map = -1;
	Document->Post.Background.Reset();
	Document->Post.BackgroundColor = FLinearColor(0.f, 0.f, 0.f, -1.f);
	struct FState { int32 Frame = 0; int32 Step = 0; TArray<TArray<FColor>> Ups; TArray<FString> Lines; int32 Failures = 0; };
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<UOKStudioGame> Self(this);
	auto Show = [Self](int32 Step)
	{
		const TArray<FOKStudioSkyPattern>& Patterns = OKStudioSkyPatterns();
		FOKScenePost& P = Self->Document->Post;
		P.bSky = Patterns.IsValidIndex(Step);
		if (P.bSky) P.SkyPattern = Patterns[Step].No;
		Self->Document->NotifyChanged();
		Self->SyncNow();
		// The dome takes the pattern now; its material compiles and its render state updates before the pictures.
		Self->SceneActor->PlaceBackground(FTransform(FVector(0.f, 0.f, 160.f)), 70.f);
	};
	Show(0);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, Show, bExit](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		const TArray<FOKStudioSkyPattern>& Patterns = OKStudioSkyPatterns();
		auto Check = [&State](bool bOk, const FString& What) { State->Lines.Add(FString::Printf(TEXT("%s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); if (!bOk) ++State->Failures; };
		if (State->Step == 0 && State->Frame == 0) Check(Patterns.Num() >= 4, FString::Printf(TEXT("%d sky patterns in the library"), Patterns.Num()));
		if (++State->Frame < 30 || (Self->MaterialsPending() && State->Frame < 1200)) return true;
		if (State->Step == Patterns.Num() + 1)
		{
			int32 Want = -1;
			const bool bHas = OKStudioMapSky(50, Self->Document->Post.SunType, Want);
			Self->SceneActor->PlaceBackground(FTransform(FVector(0.f, 0.f, 160.f)), 70.f);
			const int32 Shown = Self->SceneActor->ShownSkyPattern();
			Check(bHas && Want >= 1000 && Shown == Want, FString::Printf(TEXT("map 50 (scene sky off): the dome shows the map's own sky (pattern %d, shown %d, map %s)"),
				Want, Shown, Self->SceneActor->MapError.IsEmpty() ? TEXT("shown") : *Self->SceneActor->MapError));
			Self->Document->Map = -1;
			Self->Document->NotifyChanged();
			Self->SyncNow();
			for (const FString& L : State->Lines) UE_LOG(LogTemp, Display, TEXT("OK studio sky test: %s"), *L);
			UE_LOG(LogTemp, Display, TEXT("OK studio sky test: RESULT %s"), State->Failures ? TEXT("FAIL") : TEXT("PASS"));
			if (bExit) FPlatformMisc::RequestExit(false);
			return false;
		}
		const bool bOff = !Patterns.IsValidIndex(State->Step);
		const FString Label = bOff ? FString(TEXT("off")) : FString::Printf(TEXT("%d %s"), Patterns[State->Step].No, *Patterns[State->Step].Name);
		const FVector Eye(0.f, 0.f, 160.f);
		TArray<FColor> Up, Horizon;
		FString Error;
		for (TPair<TArray<FColor>*, float> V : { TPair<TArray<FColor>*, float>(&Up, 60.f), TPair<TArray<FColor>*, float>(&Horizon, 8.f) })
		{
			// The dome follows the view it is placed for.
			const FTransform View(FRotator(V.Value, 30.f, 0.f), Eye);
			Self->SceneActor->PlaceBackground(View, 70.f);
			FOKSceneCard::Capture(Self->SceneActor, View, 70.f, &Self->Viewer->GetCameraComponent()->PostProcessSettings, 320, 180, false, *V.Key, Error);
			TArray64<uint8> Png;
			if (V.Key->Num() == 320 * 180)
			{
				FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(V.Key->GetData(), V.Key->Num()), Png);
				FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("OKStudio/sky_%s_%s.png"),
					bOff ? TEXT("off") : *FString::FromInt(Patterns[State->Step].No), V.Value > 30.f ? TEXT("up") : TEXT("horizon"))));
			}
		}
		auto Mean = [](const TArray<FColor>& Px) { FLinearColor M(0, 0, 0, 0); for (const FColor& C : Px) M += FLinearColor(C.R, C.G, C.B, 0); return Px.Num() ? M / float(Px.Num()) : M; };
		const FLinearColor U = Mean(Up), H = Mean(Horizon);
		const FString Values = FString::Printf(TEXT("up mean (%.0f,%.0f,%.0f), horizon mean (%.0f,%.0f,%.0f)"), U.R, U.G, U.B, H.R, H.G, H.B);
		if (bOff)
		{
			Check(Up.Num() == 320 * 180 && State->Ups.Num() > 0 && Up != State->Ups.Last(), TEXT("sky off: the view changes; ") + Values);
			Self->Document->Map = 50;
			Self->Document->NotifyChanged();
			Self->SyncNow();
			++State->Step;
			State->Frame = 0;
		}
		else
		{
			Check(Up.Num() == 320 * 180 && Horizon.Num() == 320 * 180, FString::Printf(TEXT("sky %s: pictured; %s"), *Label, *Values));
			if (Patterns[State->Step].No == 1) Check(U.B > U.R + 30.f, FString::Printf(TEXT("sky %s: blue above"), *Label));
			if (Patterns[State->Step].No == 3 && State->Ups.Num() > 1)
			{
				// Night: darker above than the day, and its moon (MOON_CUSTOM_TEXTURE) bright where _MoonPosition points.
				const FLinearColor Day = Mean(State->Ups[1]);
				Check(U.R + U.G + U.B < Day.R + Day.G + Day.B - 20.f, FString::Printf(TEXT("sky %s: darker above than the day"), *Label));
				FVector Moon;
				TArray<FColor> At;
				if (OKStudioSkyBodyDirection(3, TEXT("_MoonPosition"), Moon))
				{
					const FTransform View(Moon.Rotation(), Eye);
					Self->SceneActor->PlaceBackground(View, 20.f);
					FOKSceneCard::Capture(Self->SceneActor, View, 20.f, &Self->Viewer->GetCameraComponent()->PostProcessSettings, 320, 180, false, At, Error);
				}
				const FLinearColor M = Mean(At);
				const FColor C = At.Num() == 320 * 180 ? At[90 * 320 + 160] : FColor::Black;
				if (At.Num() == 320 * 180)
				{
					TArray64<uint8> Png;
					FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(At.GetData(), At.Num()), Png);
					FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / TEXT("OKStudio/sky_3_moon.png")));
				}
				Check(C.R + C.G + C.B > M.R + M.G + M.B + 60.f, FString::Printf(TEXT("sky %s: the moon at _MoonPosition (centre %d,%d,%d, mean %.0f,%.0f,%.0f)"), *Label, C.R, C.G, C.B, M.R, M.G, M.B));
			}
			for (int32 K = 0; K < State->Ups.Num(); ++K)
			{
				const FLinearColor O = Mean(State->Ups[K]);
				Check(FMath::Abs(O.R - U.R) + FMath::Abs(O.G - U.G) + FMath::Abs(O.B - U.B) > 6.f, FString::Printf(TEXT("sky %s differs from pattern %d"), *Label, Patterns[K].No));
			}
			State->Ups.Add(Up);
			++State->Step;
			State->Frame = 0;
			Show(State->Step);
		}
		return true;
	}), 0.1f);
}

void UOKStudioGame::RunPostTest(bool bExit)
{
	// One character, no map, the day sky. Each effect pictured off and on from its own view: fog (red) on the character
	// 40 m away (Unity Exp2 0.01: about 15 % fog there), ambient occlusion (black tint, 0.3 m) on her up close, and sun
	// shafts (white) looking at the sky past her head.
	Select(-1);
	Document->Map = -1;
	Document->Post.Background.Reset();
	Document->Post.bSky = true;
	Document->Post.SkyPattern = 1;
	const int32 Id = Add(EOKSceneObjectKind::Character);
	Document->Find(Id)->Character.Card = TEXT("Content/CharacterCards/CrowdAPNG/C002.png");
	Document->NotifyChanged();
	SyncNow();
	struct FState { int32 Frame = 0; bool bWarm = false; int32 WarmShots = 0; TArray<FString> Lines; int32 Failures = 0; };
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<UOKStudioGame> Self(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, bExit](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		if (++State->Frame < 60 || (Self->MaterialsPending() && State->Frame < 1200)) return true;
		// The post material is made the first time an effect is on; its shaders compile before the pictures.
		if (!State->bWarm)
		{
			State->bWarm = true;
			State->Frame = 30;
			FPostProcessSettings Warm;
			Self->Document->Post.bFog = true;
			Self->Document->Post.ApplyTo(Warm);
			Self->Document->Post.bFog = false;
			return true;
		}
		const FString PostState = OKStudioScenePostMaterialState();
		if (PostState != TEXT("ready") && State->Frame < 1800)
		{
			FPostProcessSettings Warm;
			Self->Document->Post.bFog = true;
			Self->Document->Post.ApplyTo(Warm);
			Self->Document->Post.bFog = false;
			if (State->Frame % 100 == 0) UE_LOG(LogTemp, Display, TEXT("OK studio post test: post material %s"), *PostState);
			return true;
		}
		// A few throwaway pictures with every effect on: the material instance gets its render state before the checks.
		if (State->WarmShots < 10)
		{
			if (State->WarmShots++ == 0) UE_LOG(LogTemp, Display, TEXT("OK studio post test: post material %s after %d frames"), *PostState, State->Frame);
			FOKScenePost& P = Self->Document->Post;
			P.bFog = P.bAmbientOcclusion = P.bSunShafts = true;
			FPostProcessSettings Settings = Self->Viewer->GetCameraComponent()->PostProcessSettings;
			P.ApplyTo(Settings);
			P.bFog = P.bAmbientOcclusion = P.bSunShafts = false;
			TArray<FColor> Pixels;
			FString Error;
			FOKSceneCard::Capture(Self->SceneActor, FTransform(FVector(-300.f, 0.f, 100.f)), 60.f, &Settings, 64, 36, false, Pixels, Error);
			return true;
		}
		auto Check = [&State](bool bOk, const FString& What) { State->Lines.Add(FString::Printf(TEXT("%s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); if (!bOk) ++State->Failures; };
		FOKScenePost& Post = Self->Document->Post;
		auto Picture = [&](const FTransform& View, float Fov, const TCHAR* Name, TArray<FColor>* OutPixels = nullptr)
		{
			Self->SceneActor->PlaceBackground(View, Fov);
			FPostProcessSettings Settings = Self->Viewer->GetCameraComponent()->PostProcessSettings;
			Post.ApplyTo(Settings);
			TArray<FColor> Pixels;
			FString Error;
			FOKSceneCard::Capture(Self->SceneActor, View, Fov, &Settings, 320, 180, false, Pixels, Error);
			TArray64<uint8> Png;
			if (Pixels.Num() == 320 * 180)
			{
				FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
				FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("OKStudio/post_%s.png"), Name)));
			}
			if (OutPixels) *OutPixels = Pixels;
			FLinearColor M(0, 0, 0, 0);
			for (const FColor& C : Pixels) M += FLinearColor(C.R, C.G, C.B, 0);
			return Pixels.Num() ? M / float(Pixels.Num()) : M;
		};
		// The pictures need a frame for a sky or setting change to reach the renderer: the off and on pictures are taken
		// with the same view right after one another; the effect is in the post-process material (no render state).
		const FTransform Far(FRotator(0.f, 0.f, 0.f), FVector(-4000.f, 0.f, 100.f));
		Post.bFog = false;
		TArray<FColor> FogOffPixels, FogOnPixels;
		const FLinearColor FogOff = Picture(Far, 4.f, TEXT("fog_off"), &FogOffPixels);
		Post.bFog = true;
		Post.FogColor = FLinearColor(1.f, 0.f, 0.f, 1.f);
		const FLinearColor FogOn = Picture(Far, 4.f, TEXT("fog_on"), &FogOnPixels);
		Post.bFog = false;
        // The sky is intentionally exempt from distance fog. Whole-image means
        // dilute the actual geometry response when the fixture covers little of the view.
        int32 FogGeometryPixels = 0;
        if (FogOffPixels.Num() == 320 * 180 && FogOnPixels.Num() == FogOffPixels.Num())
            for (int32 Pixel = 0; Pixel < FogOffPixels.Num(); ++Pixel)
                if (int32(FogOnPixels[Pixel].R) - FogOffPixels[Pixel].R >= 5
                    && int32(FogOnPixels[Pixel].G) - FogOffPixels[Pixel].G <= -5) ++FogGeometryPixels;
        Check(FogGeometryPixels >= 100, FString::Printf(TEXT("fog: at least 100 geometry pixels gain red and lose green by 5; actual=%d, means R %.0f -> %.0f G %.0f -> %.0f"),
            FogGeometryPixels, FogOff.R, FogOn.R, FogOff.G, FogOn.G));
		const FTransform Near(FRotator(-10.f, 0.f, 0.f), FVector(-120.f, 0.f, 140.f));
		Post.bAmbientOcclusion = false;
		const FLinearColor AoOff = Picture(Near, 40.f, TEXT("ao_off"));
		Post.bAmbientOcclusion = true;
		Post.AmbientOcclusionTint = FLinearColor::Black;
		Post.AmbientOcclusionRadius = 0.3f;
		const FLinearColor AoOn = Picture(Near, 40.f, TEXT("ao_on"));
        Post.AmbientOcclusionTint = FLinearColor(.53125f, .5280878f, .5280878f, 1.f);
        FPostProcessSettings TintSettings;
        Post.ApplyTo(TintSettings);
        bool bTintBound = false;
        for (const FWeightedBlendable& Blendable : TintSettings.WeightedBlendables.Array)
            if (auto* Material = Cast<UMaterialInstanceDynamic>(Blendable.Object))
            {
                FLinearColor Tint;
                if (Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("AOTint")), Tint))
                    bTintBound = Tint.Equals(Post.AmbientOcclusionTint, 1e-6f);
            }
        Check(bTintBound, TEXT("AO global tint preserves native raw shader RGB without a second gamma decode"));
        const FLinearColor AoTinted = Picture(Near, 40.f, TEXT("ao_tinted"));
        Check(AoTinted.R + AoTinted.G + AoTinted.B > AoOn.R + AoOn.G + AoOn.B + 1.f
            && AoTinted.R + AoTinted.G + AoTinted.B < AoOff.R + AoOff.G + AoOff.B - 1.f,
            TEXT("native mid-gray AO tint retains contact shading between black AO and disabled AO"));
        Post.AmbientOcclusionTint = FLinearColor::White;
        const FLinearColor AoWhite = Picture(Near, 40.f, TEXT("ao_white"));
        Check(AoWhite.Equals(AoOff, 1.f), TEXT("white AO tint is the neutral multiplicative limit"));
		Post.bAmbientOcclusion = false;
		Check(AoOn.R + AoOn.G + AoOn.B < AoOff.R + AoOff.G + AoOff.B - 3.f, FString::Printf(TEXT("ambient occlusion: darker (%.0f -> %.0f)"), AoOff.R + AoOff.G + AoOff.B, AoOn.R + AoOn.G + AoOn.B));
		const FTransform Up(FRotator(35.f, 0.f, 0.f), FVector(-150.f, 0.f, 120.f));
		Post.bSunShafts = false;
		const FLinearColor ShaftOff = Picture(Up, 60.f, TEXT("shafts_off"));
		Post.bSunShafts = true;
		Post.SunShaftColor = FLinearColor::White;
		Post.SunShaftThreshold = FLinearColor(0.2f, 0.2f, 0.2f, 1.f);
		const FLinearColor ShaftOn = Picture(Up, 60.f, TEXT("shafts_on"));
		Post.bSunShafts = false;
		Check(ShaftOn.R + ShaftOn.G + ShaftOn.B > ShaftOff.R + ShaftOff.G + ShaftOff.B + 3.f, FString::Printf(TEXT("sun shafts: brighter (%.0f -> %.0f)"), ShaftOff.R + ShaftOff.G + ShaftOff.B, ShaftOn.R + ShaftOn.G + ShaftOn.B));
		for (const FString& L : State->Lines) UE_LOG(LogTemp, Display, TEXT("OK studio post test: %s"), *L);
		UE_LOG(LogTemp, Display, TEXT("OK studio post test: RESULT %s"), State->Failures ? TEXT("FAIL") : TEXT("PASS"));
		if (bExit) FPlatformMisc::RequestExit(false);
		return false;
	}), 0.1f);
}

void UOKStudioGame::RunPatternTest(bool bExit)
{
	// An item with a pattern slot, pictured plain and then with pattern 1 (stripes) in red: the patterned picture must
	// differ where the item is, and every pattern parameter must have reached its material (no unapplied overrides).
	const FOKStudioCatalogItem* Patterned = FOKStudioCatalog::Get().Items.FindByPredicate([](const FOKStudioCatalogItem& I) { return I.Patterns[0] && I.Colors[0] && I.Group == 0; });
	if (!Patterned) Patterned = FOKStudioCatalog::Get().Items.FindByPredicate([](const FOKStudioCatalogItem& I) { return I.Patterns[0]; });
	if (!Patterned) { UE_LOG(LogTemp, Display, TEXT("OK studio pattern test: RESULT FAIL no patterned catalogue item")); if (bExit) FPlatformMisc::RequestExit(false); return; }
	Select(-1);
	const int32 Id = Add(EOKSceneObjectKind::Item);
	FOKSceneItem& Item = Document->Find(Id)->Item;
	Item.Group = Patterned->Group; Item.Category = Patterned->Category; Item.No = Patterned->No;
	Item.Colors = { FLinearColor(1.f, 1.f, 1.f, 1.f) };
	Document->Find(Id)->Transform = FTransform(FRotator(0.f, 0.f, 0.f), FVector(0.f, 400.f, 100.f), FVector(3.f));
	Focus = FVector(0.f, 400.f, 110.f); Yaw = 90.f; Pitch = -15.f; Distance = 260.f;
	struct FState { int32 Frame = 0; int32 Step = 0; int32 ColoredChanged = 0; int32 ColoredRed = 0; int32 Changed = 0; int32 Reddish = 0; TArray<FColor> Plain; };
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<UOKStudioGame> Self(this);
	const FString Name = Patterned->Name;
	Document->NotifyChanged();
	SyncNow();
	UpdateViewer();
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, Id, Name, bExit](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		// Three pictures, each once the rebuilt item's runtime materials have compiled: plain (white), main colour red,
		// then white with pattern 1 in red.
		const int32 Frame = ++State->Frame;
		const bool bCompiling = Self->MaterialsPending();
		if (Frame < 60 || (bCompiling && Frame < 900)) return true;
		TArray<FColor> Pixels;
		FString Error;
		Self->UpdateViewer();
		const UCameraComponent* Cam = Self->Viewer->GetCameraComponent();
		FOKSceneCard::Capture(Self->SceneActor, Self->Viewer->GetActorTransform(), Cam->FieldOfView, &Cam->PostProcessSettings, 320, 180, false, Pixels, Error);
		static const TCHAR* Shots[] = { TEXT("plain"), TEXT("colored"), TEXT("striped"), TEXT("edited") };
		TArray64<uint8> Png;
		if (Pixels.Num() == 320 * 180)
		{
			FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("OKStudio/pattern_%s.png"), Shots[State->Step])));
		}
		// Pixels that differ from the plain picture, and how many of them are red.
		auto Red = [&State](const TArray<FColor>& Shot, int32& Changed)
		{
			int32 Reddish = 0;
			Changed = 0;
			for (int32 K = 0; K < Shot.Num() && K < State->Plain.Num(); ++K)
			{
				const FColor& A = State->Plain[K];
				const FColor& B = Shot[K];
				if (FMath::Abs(A.R - B.R) + FMath::Abs(A.G - B.G) + FMath::Abs(A.B - B.B) > 60) { ++Changed; Reddish += B.R > B.G + 40 && B.R > B.B + 40 ? 1 : 0; }
			}
			return Reddish;
		};
		FOKSceneItem& Shown = Self->Document->Find(Id)->Item;
		if (State->Step == 0)
		{
			State->Plain = Pixels;
			Shown.Colors = { FLinearColor(1.f, 0.f, 0.f, 1.f) };
		}
		else if (State->Step == 1)
		{
			State->ColoredRed = Red(Pixels, State->ColoredChanged);
			FOKSceneItemPattern P;
			P.Pattern = 1;
			P.Color = FLinearColor(1.f, 0.f, 0.f, 1.f);
			P.Uv = FVector4(0., 0., 4., 4.);
			Shown.Colors = { FLinearColor(1.f, 1.f, 1.f, 1.f) };
			Shown.Patterns = { P };
		}
		else if (State->Step == 2)
		{
			State->Reddish = Red(Pixels, State->Changed);
			// A MaterialEditor edit (any material of the item) makes the main colour red again; the pattern stays, so the
			// unapplied check below covers both.
			FOKSceneMaterialEdit Edit;
			Edit.Property = TEXT("Color");
			Edit.Kind = EOKSceneMaterialEditKind::Color;
			Edit.Value = FLinearColor(1.f, 0.f, 0.f, 1.f);
			Shown.MaterialEdits = { Edit };
		}
		if (State->Step < 3)
		{
			++State->Step;
			State->Frame = 0;
			Self->Document->NotifyChanged();
			Self->SyncNow();
			return true;
		}
		int32 EditChanged = 0;
		const int32 EditRed = Red(Pixels, EditChanged);
		const int32 Changed = State->Changed, Reddish = State->Reddish;
		TArray<FString> Unapplied;
		if (AKKPreviewActor* Preview = Self->SceneActor->PreviewOf(Id))
		{
			Preview->ApplyMaterialOverrides(&Unapplied);
			// What the item's materials do take, for a failure report.
			if (!Unapplied.IsEmpty())
			{
				TInlineComponentArray<UMeshComponent*> Meshes(Preview);
				for (UMeshComponent* Mesh : Meshes)
					for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
						if (UMaterialInterface* M = Mesh->GetMaterial(Slot))
						{
							TArray<FMaterialParameterInfo> Infos, More;
							TArray<FGuid> Ids;
							M->GetAllTextureParameterInfo(Infos, Ids);
							M->GetAllVectorParameterInfo(More, Ids); Infos.Append(More);
							M->GetAllScalarParameterInfo(More, Ids); Infos.Append(More);
							TArray<FString> Names;
							for (const FMaterialParameterInfo& I : Infos) Names.Add(I.Name.ToString());
							UE_LOG(LogTemp, Display, TEXT("OK studio pattern test: %s slot %d material %s (%s) parameters [%s]"), *Mesh->GetName(), Slot, *M->GetName(), *M->GetMaterial()->GetName(), *FString::Join(Names, TEXT(" ")));
						}
			}
		}
		const bool bOk = State->ColoredRed > 200 && Changed > 200 && Reddish > 100 && EditRed > 200 && Unapplied.IsEmpty();
		UE_LOG(LogTemp, Display, TEXT("OK studio pattern test: RESULT %s item %s: main colour red %d of %d changed; pattern %d pixels changed, %d red; material edit red %d of %d changed; unapplied [%s]"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Name, State->ColoredRed, State->ColoredChanged, Changed, Reddish, EditRed, EditChanged, *FString::Join(Unapplied, TEXT(", ")));
		Self->Document->RemoveObject(Id);
		Self->Document->NotifyChanged();
		if (bExit) FPlatformMisc::RequestExit(false);
		return false;
	}));
}

void UOKStudioGame::RunEffectColorTest(bool bExit)
{
	// The effect colour reaches the particles (User.Color, linked by OKStudio.MakeEffects rewire). The particles are
	// simulated and drawn over real frames (they reach the renderer at the end of a frame), red then blue, and the
	// bright pixels of a picture away from everything else must follow.
	// A looping template keeps its earlier particles a while, so each colour gets a fresh effect object.
	Focus = FVector(0.f, 400.f, 150.f); Yaw = 90.f; Pitch = 0.f; Distance = 250.f;
	struct FState { int32 Frame = 0; int32 Fx = -1; double R[2] = { 0, 0 }; double B[2] = { 0, 0 }; int32 Lit[2] = { 0, 0 }; };
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<UOKStudioGame> Self(this);
	auto Paint = [Self, State](const FLinearColor& Color)
	{
		if (!Self.IsValid()) return;
		if (State->Fx >= 0) Self->Document->RemoveObject(State->Fx);
		Self->Select(-1);
		State->Fx = Self->Add(EOKSceneObjectKind::Effect);
		FOKSceneObject* O = Self->Document->Find(State->Fx);
		O->Effect.Effect = TEXT("fireball");
		O->Effect.Color = Color;
		O->Transform.SetLocation(FVector(0.f, 400.f, 150.f));
		Self->Document->NotifyChanged();
		Self->SyncNow();
		Self->UpdateViewer();
	};
	Paint(FLinearColor(1.f, 0.f, 0.f, 1.f));
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, bExit, Paint](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		const int32 Frame = ++State->Frame;
		if (Frame != 90 && Frame != 180) return true;
		const int32 Shot = Frame == 90 ? 0 : 1;
		TArray<FColor> Pixels;
		FString Error;
		Self->UpdateViewer();
		if (FOKSceneCard::Capture(Self->SceneActor, Self->Viewer->GetActorTransform(), 40.f, &Self->Viewer->GetCameraComponent()->PostProcessSettings, 320, 180, false, Pixels, Error))
		{
			for (const FColor& P : Pixels) if (FMath::Max3(P.R, P.G, P.B) > 60) { State->R[Shot] += P.R; State->B[Shot] += P.B; ++State->Lit[Shot]; }
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("OKStudio/effect_color_%s.png"), Shot ? TEXT("blue") : TEXT("red"))));
		}
		if (Shot == 0) { Paint(FLinearColor(0.f, 0.f, 1.f, 1.f)); return true; }
		const bool bOk = State->Lit[0] > 20 && State->Lit[1] > 20 && State->R[0] > State->B[0] * 1.5 && State->B[1] > State->R[1] * 1.5;
		UE_LOG(LogTemp, Display, TEXT("OK studio effect colour test: RESULT %s red %d lit (R %.0f / B %.0f), blue %d lit (R %.0f / B %.0f)"), bOk ? TEXT("PASS") : TEXT("FAIL"),
			State->Lit[0], State->R[0], State->B[0], State->Lit[1], State->R[1], State->B[1]);
		Self->Document->RemoveObject(State->Fx);
		Self->Document->NotifyChanged();
		if (bExit) FPlatformMisc::RequestExit(false);
		return false;
	}));
}

void UOKStudioGame::RunSelfTest(bool bExit)
{
	TArray<FString> Lines;
	int32 Failures = 0;
	auto Step = [](const TCHAR* What) { UE_LOG(LogTemp, Display, TEXT("OK studio game selftest step: %s"), What); GLog->Flush(); };
	auto Check = [&](bool bOk, const FString& What)
	{
		Lines.Add(FString::Printf(TEXT("%s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What));
		if (!bOk) ++Failures;
	};
	Check(Panel.IsValid(), TEXT("studio panel in the game viewport"));
	Check(Document->Objects.Num() == 1 && Document->Objects[0].Kind == EOKSceneObjectKind::Light, TEXT("a new scene starts with a key light"));
	Check(AvailableCards().Num() >= 100, FString::Printf(TEXT("card picker lists %d cards"), AvailableCards().Num()));
	Step(TEXT("add folder and character"));
	const int32 Folder = Add(EOKSceneObjectKind::Folder);
	const int32 A = Add(EOKSceneObjectKind::Character);
	Document->Find(A)->Character.Card = TEXT("Content/CharacterCards/CrowdAPNG/C002.png");
	Document->Find(A)->Character.ClothesStates[1] = 1;
	Document->Find(A)->Character.EyesPattern = 2;
	Select(Folder);
	const int32 Item = Add(EOKSceneObjectKind::Item);
	Document->Find(Item)->Item.Model = TEXT("447f3c180000000045cc17684f4b4331");
	Document->Find(Item)->Transform.SetLocation(FVector(0.f, 60.f, 120.f));
	const int32 Cam = Add(EOKSceneObjectKind::Camera);
	Step(TEXT("sync"));
	SyncNow();
	Step(TEXT("synced"));
	const AKKPreviewActor* PA = SceneActor->PreviewOf(A);
	const AKKPreviewActor* PItem = SceneActor->PreviewOf(Item);
	Check(Document->Find(A)->Parent == Folder && Document->Find(Item)->Parent == Folder, TEXT("objects added into the selected folder"));
	Check(PA && PA->Session.PartCount() > 0, FString::Printf(TEXT("character (OK card C002): %d parts"), PA ? PA->Session.PartCount() : 0));
	Check(PItem && PItem->Session.PartCount() > 0, FString::Printf(TEXT("item (library model): %d parts"), PItem ? PItem->Session.PartCount() : 0));
	Check(SceneActor->Errors.Num() == 0, FString::Printf(TEXT("no object errors (%d)"), SceneActor->Errors.Num()));
	// Character state (R8): tears level 2 shows the medium tears, every accessory slot switched off hides them all.
	Step(TEXT("character state"));
	{
		struct FShownParts { int32 Tears = 0, Accessories = 0, Body = 0, Clothes = 0; };
		auto Count = [&]
		{
			FShownParts S;
			const AKKPreviewActor* P = SceneActor->PreviewOf(A);
			for (int32 Part = 0; P && Part < P->Session.PartCount(); ++Part)
			{
				OkMeshView V;
				if (!P->Session.PartAt(Part, V) || !V.render_visible) continue;
				const FString Name = UTF8_TO_TCHAR(V.name ? V.name : ""), Role = UTF8_TO_TCHAR(V.role ? V.role : "");
				S.Tears += Name.Contains(TEXT("cf_O_namida_M")) ? 1 : 0;
				S.Accessories += Role.StartsWith(TEXT("acc")) ? 1 : 0;
				S.Clothes += Role.StartsWith(TEXT("clothes")) ? 1 : 0;
				S.Body += Role == TEXT("body") || Role == TEXT("head") ? 1 : 0;
			}
			return S;
		};
		const FShownParts Before = Count();
		Document->Find(A)->Character.TearsLevel = 2;
		Document->Find(A)->Character.AccessoriesShown.Init(0, 21);
		Document->Find(A)->Character.InvisibleBody = 1;
		Document->NotifyChanged();
		SyncNow();
		const FShownParts After = Count();
		Check(Before.Tears == 0 && After.Tears == 0, FString::Printf(TEXT("tears level 2 is under the invisible head (%d → %d)"), Before.Tears, After.Tears));
		Check(Before.Accessories > 0 && After.Accessories == 0, FString::Printf(TEXT("accessories switched off (%d → %d shown)"), Before.Accessories, After.Accessories));
		Check(Before.Body > 0 && After.Body == 0 && After.Clothes == Before.Clothes, FString::Printf(TEXT("invisible body hides body and head, keeps clothes (%d → %d body, %d → %d clothes)"), Before.Body, After.Body, Before.Clothes, After.Clothes));
		Document->Find(A)->Character.InvisibleBody = -1;
		Document->NotifyChanged();
		SyncNow();
		const FShownParts Visible = Count();
		// The tears object is a head part of its own.
		Check(Visible.Tears == 1 && Visible.Body == Before.Body + 1,FString::Printf(TEXT("tears level 2 shows the medium tears; the body is back (%d tears, %d body)"), Visible.Tears, Visible.Body));
		Document->Find(A)->Character.TearsLevel = -1;
		Document->Find(A)->Character.AccessoriesShown.Reset();
		Document->NotifyChanged();
		SyncNow();
	}
	// Undo history: each committed change is one step; undo restores the document, redo brings it back, and a new change
	// drops the redo steps.
	Step(TEXT("undo"));
	{
		CommitHistory();
		const int32 Before = UndoSteps();
		Select(-1);
		const int32 UndoFolder = Add(EOKSceneObjectKind::Folder);
		CommitHistory();
		Document->Find(UndoFolder)->Name = TEXT("undo test");
		Document->NotifyChanged();
		CommitHistory();
		const bool bTwoSteps = UndoSteps() == Before + 2;
		Undo();
		const bool bRenameUndone = Document->Find(UndoFolder) && Document->Find(UndoFolder)->Name != TEXT("undo test");
		const int32 Kept = LastHistoryKept, Total = Document->Objects.Num();
		Undo();
		const bool bAddUndone = !Document->Find(UndoFolder);
		Redo();
		const bool bRedone = Document->Find(UndoFolder) != nullptr && CanRedo();
		Document->RemoveObject(UndoFolder);
		CommitHistory();
		Check(bTwoSteps && bRenameUndone && bAddUndone && bRedone && !CanRedo(),
			FString::Printf(TEXT("undo / redo step through the document (steps +%d, rename undone %d, add undone %d, redone %d)"), UndoSteps() - Before, bRenameUndone, bAddUndone, bRedone));
		Check(Total > 1 && Kept == Total - 1, FString::Printf(TEXT("undoing a rename rebuilds only that object (%d of %d kept)"), Kept, Total));
	}
	// Item child point (R4): a child of a catalogue item with 子の接続先 sits on that node, not on the item origin.
	Step(TEXT("child point"));
	{
		const FOKStudioCatalogItem* Joint = FOKStudioCatalog::Get().Items.FindByPredicate([](const FOKStudioCatalogItem& I) { return I.Prefab == TEXT("p_koi_stu_60kansetu00_02"); });
		const FTransform Point = Joint ? FOKStudioCatalog::Get().ItemChildPoint(Joint->Group, Joint->Category, Joint->No) : FTransform::Identity;
		Select(-1);
		const int32 Base = Add(EOKSceneObjectKind::Item);
		FOKSceneItem& BI = Document->Find(Base)->Item;
		if (Joint) { BI.Model = Joint->Model; BI.Prefab = Joint->Prefab; BI.Group = Joint->Group; BI.Category = Joint->Category; BI.No = Joint->No; }
		Document->Find(Base)->Transform = FTransform(FRotator(0.f, 90.f, 0.f), FVector(100.f, 0.f, 0.f));
		const int32 Kid = Document->AddObject(EOKSceneObjectKind::Folder, TEXT("child"), Base);
		Document->Find(Kid)->Transform = FTransform::Identity;
		const FVector Expected = (Point * Document->Find(Base)->Transform).GetLocation();
		Check(Joint && !Point.GetLocation().IsNearlyZero() && Document->Find(Kid)->Parent == Base && Document->WorldTransform(Kid).GetLocation().Equals(Expected, 0.01f),
			FString::Printf(TEXT("child sits on the item's child point (%s, %s)"), *Point.GetLocation().ToString(), *Document->WorldTransform(Kid).GetLocation().ToString()));
		const FVector Before = Document->WorldTransform(Kid).GetLocation();
		Document->Reparent(Kid, -1);
		Check(Document->WorldTransform(Kid).GetLocation().Equals(Before, 0.01f), TEXT("unparenting from the item keeps the child where it was"));
		Document->RemoveObject(Kid);
		Document->RemoveObject(Base);
		Select(-1);
	}
	Step(TEXT("viewer"));
	// Free camera, then the camera object.
	Orbit(30.f, -5.f);
	UpdateViewer();
	const FVector Free = Viewer->GetActorLocation();
	Document->Find(Cam)->Transform.SetLocation(FVector(250.f, 250.f, 160.f));
	Document->ActiveCamera = Cam;
	SyncNow();
	UpdateViewer();
	Check(Viewer->GetActorLocation().Equals(SceneActor->ActiveCamera()->GetComponentLocation(), 0.1f) && !Viewer->GetActorLocation().Equals(Free, 1.f), TEXT("viewer follows the active camera object"));
	APlayerController* PC = SceneActor->GetWorld()->GetFirstPlayerController();
	Check(PC && PC->GetViewTarget() == Viewer, TEXT("player views through the studio viewer"));
	// Guide object on the character, seen from the free camera.
	Step(TEXT("guide"));
	Document->ActiveCamera = -1;
	Focus = FVector(0.f, 0.f, 90.f); Yaw = -90.f; Pitch = -8.f; Distance = 380.f;
	UpdateViewer();
	if (PC && PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0.f);
	Select(A);
	GuideMode = 0;
	{
		TArray<FGuideHandle> Handles;
		FVector2D Center;
		double CmPerPixel = 0.;
		const bool bHandles = GuideHandles(Handles, Center, CmPerPixel);
		// The X / Y / Z handles (the plane squares follow them).
		const int32 Planes = Handles.FilterByPredicate([](const FGuideHandle& H) { return H.Axis >= 4 && H.Axis <= 6; }).Num();
		Handles.RemoveAll([](const FGuideHandle& H) { return H.Axis > 2; });
		Check(bHandles && Handles.Num() == 3 && Planes >= 2, FString::Printf(TEXT("move guide: %d axis handles, %d plane handles, %.3f cm per pixel"), Handles.Num(), Planes, CmPerPixel));
		if (bHandles && Handles.Num() == 3)
		{
			const FVector2D End = Handles[0].Points.Last();
			Check(HitGuide(End) == 0, TEXT("clicking the X handle picks X"));
			const FVector Before = Document->WorldTransform(A).GetLocation();
			ApplyGuideDrag(0, (End - Handles[0].Points[0]).GetSafeNormal() * 50.);
			const FVector After = Document->WorldTransform(A).GetLocation();
			Check(After.X - Before.X > 1. && FMath::IsNearlyEqual(After.Y, Before.Y, 0.01) && FMath::IsNearlyEqual(After.Z, Before.Z, 0.01), FString::Printf(TEXT("dragging X moves %.1f cm along X only"), After.X - Before.X));
			GuideMode = 1;
			const double Yaw0 = Document->WorldTransform(A).Rotator().Yaw;
			ApplyGuideDrag(2, FVector2D(20., 0.));
			const double Turned = FRotator::NormalizeAxis(Document->WorldTransform(A).Rotator().Yaw - Yaw0);
			Check(FMath::IsNearlyEqual(Turned, 10., 0.1), FString::Printf(TEXT("rotating about Z turns %.2f degrees"), Turned));
			GuideMode = 2;
			ApplyGuideDrag(3, FVector2D(50., 0.));
			Check(FMath::IsNearlyEqual(Document->Find(A)->Transform.GetScale3D().X, 1.5, 0.01), TEXT("uniform scale handle scales 1.5x"));
			Document->Find(A)->Transform.SetScale3D(FVector::OneVector);
			GuideMode = 0;
			SyncNow();
			Check(SceneActor->PreviewOf(A) && SceneActor->PreviewOf(A)->GetActorLocation().Equals(Document->WorldTransform(A).GetLocation() + SceneActor->GetActorLocation(), 0.1f), TEXT("the character follows the guide"));
		}
	}
	Step(TEXT("fk"));
	FVector FkSet(10.f, 25.f, -50.f);
	{
		const FOKStudioCatalog& Catalog = FOKStudioCatalog::Get();
		Check(Catalog.FkBones.Num() >= 300 && Catalog.FindFkBone(TEXT("cf_j_arm00_L")), FString::Printf(TEXT("studio FK bone list: %d bones"), Catalog.FkBones.Num()));
		const AKKPreviewActor* P = SceneActor->PreviewOf(A);
		const AKKPreviewActor* const P0 = P;
		FVector Arm0(0), Head0(0), Arm1(0), Head1(0), Arm2(0);
		const bool bRead = P && P->Session.BoneEuler(TEXT("cf_j_arm00_L"), Arm0) && P->Session.BoneEuler(TEXT("cf_j_head"), Head0);
		SOKStudioPanel::SetFk(*Document->Find(A), TEXT("cf_j_arm00_L"), FkSet);
		Document->NotifyChanged();
		SyncNow();
		P = SceneActor->PreviewOf(A);
		const bool bPosed = P && P->Session.BoneEuler(TEXT("cf_j_arm00_L"), Arm1) && P->Session.BoneEuler(TEXT("cf_j_head"), Head1);
		Check(bRead && bPosed && P == P0 && Arm1.Equals(FkSet, 0.05f) && Head1.Equals(Head0, 0.05f),
			FString::Printf(TEXT("FK arm00_L %s -> %s (head unchanged %s), no rebuild"), *Arm0.ToString(), *Arm1.ToString(), *Head1.ToString()));
		Document->Find(A)->Character.Fk.Reset();
		Document->NotifyChanged();
		SyncNow();
		const bool bCleared = P && P->Session.BoneEuler(TEXT("cf_j_arm00_L"), Arm2) && Arm2.Equals(Arm0, 0.05f);
		Check(bCleared, FString::Printf(TEXT("clearing FK restores the pose %s"), *Arm2.ToString()));
		// Accessory point: an object hung on a_n_hand_L sits at the posed hand and follows it when the arm turns.
		{
			Check(Catalog.AccessoryPoints.Num() >= 50 && Catalog.FindAccessoryPoint(43) && Catalog.FindAccessoryPoint(43)->Bone == TEXT("a_n_hand_L"),
				FString::Printf(TEXT("studio accessory points: %d"), Catalog.AccessoryPoints.Num()));
			const int32 Held = Document->AddObject(EOKSceneObjectKind::Light, TEXT("held light"), A);
			Document->Find(Held)->AttachBone = TEXT("a_n_hand_L");
			Document->NotifyChanged();
			SyncNow();
			auto HandWorld = [&](FVector& Out)
			{
				float M[16];
				if (!P || !P->Session.BoneWorldMatrix(TEXT("a_n_hand_L"), M)) return false;
				Out = (FTransform(OKSkinPaletteToUnreal(M)) * P->GetActorTransform()).GetLocation();
				return true;
			};
			const USceneComponent* HeldComponent = SceneActor->ComponentOf(Held);
			FVector HeldHand0(0), HeldHand1(0);
			const bool bAt = HeldComponent && HandWorld(HeldHand0) && HeldComponent->GetComponentLocation().Equals(HeldHand0, 0.1);
			SOKStudioPanel::SetFk(*Document->Find(A), TEXT("cf_j_arm00_L"), FkSet);
			Document->NotifyChanged();
			SyncNow();
			SceneActor->PlaceAttached();
			const bool bFollows = HeldComponent && HandWorld(HeldHand1) && FVector::Dist(HeldHand0, HeldHand1) > 1. && HeldComponent->GetComponentLocation().Equals(HeldHand1, 0.1);
			Check(bAt && bFollows, FString::Printf(TEXT("an object on accessory point a_n_hand_L sits at the hand (%s) and follows it %.1f cm"),
				HeldComponent ? *HeldComponent->GetComponentLocation().ToString() : TEXT("none"), FVector::Dist(HeldHand0, HeldHand1)));
			Document->Find(A)->Character.Fk.Reset();
			Document->RemoveObject(Held);
			Document->NotifyChanged();
			SyncNow();
		}
		// Hand shape: the game's finger pattern replaces the animated fingers, without a rebuild.
		FVector Finger0(0), Finger1(0), Finger2(0);
		const bool bFinger0 = P && P->Session.BoneEuler(TEXT("cf_j_index02_L"), Finger0);
		Document->Find(A)->Character.HandLeft = 2;
		Document->NotifyChanged();
		SyncNow();
		const bool bFinger1 = P == SceneActor->PreviewOf(A) && P->Session.BoneEuler(TEXT("cf_j_index02_L"), Finger1) && !Finger1.Equals(Finger0, 0.5f);
		Check(bFinger0 && bFinger1, FString::Printf(TEXT("left hand shape 2 bends the index finger %s -> %s (%d shapes)"), *Finger0.ToString(), *Finger1.ToString(), P ? P->GetHandShapeCount() : 0));
		Document->Find(A)->Character.HandLeft = -1;
		Document->NotifyChanged();
		SyncNow();
		const bool bFinger2 = P->Session.BoneEuler(TEXT("cf_j_index02_L"), Finger2) && Finger2.Equals(Finger0, 0.05f);
		Check(bFinger2, FString::Printf(TEXT("hand shape -1 gives the fingers back %s"), *Finger2.ToString()));
		// KK Studio pose file: save the FK, clear it, load it back.
		{
			SOKStudioPanel::SetFk(*Document->Find(A), TEXT("cf_j_arm00_L"), FkSet);
			SOKStudioPanel::SetFk(*Document->Find(A), TEXT("cf_j_neck"), FVector(5.f, -15.f, 0.f));
			Document->NotifyChanged();
			SyncNow();
			Select(A);
			const TArray<FOKSceneFkBone> Saved = Document->Find(A)->Character.Fk;
			const FString PosePath = FOKStudioPose::Directory() / TEXT("_selftest_pose.dat");
			const bool bPoseSaved = Panel && Panel->SavePose(PosePath, TEXT("selftest"));
			TArray<uint8> Head;
			FFileHelper::LoadFileToArray(Head, *PosePath);
			const FUTF8ToTCHAR Code(Head.Num() > 1 ? (const ANSICHAR*)Head.GetData() + 1 : "", Head.Num() > 1 ? FMath::Min<int32>(Head[0], Head.Num() - 1) : 0);
			const bool bSigned = FString(Code.Length(), Code.Get()) == TEXT("【pose】");
			Document->Find(A)->Character.Fk.Reset();
			Document->NotifyChanged();
			SyncNow();
			const bool bPoseLoaded = bPoseSaved && Panel->LoadPose(PosePath);
			SyncNow();
			FVector Neck(0);
			const bool bNeck = P->Session.BoneEuler(TEXT("cf_j_neck"), Neck) && Neck.Equals(FVector(5.f, -15.f, 0.f), 0.05f);
			Check(bPoseSaved && bSigned && bPoseLoaded && Document->Find(A)->Character.Fk == Saved && bNeck,
				FString::Printf(TEXT("KK pose file round trip (%d FK bones, neck %s)"), Saved.Num(), *Neck.ToString()));
			IFileManager::Get().Delete(*PosePath);
			Document->Find(A)->Character.Fk.Reset();
		}
		// Bone guide: rotate rings on the bone, a drag turns it about its local Y and becomes its FK rotation.
		{
			Select(A);
			GuideBone = TEXT("cf_j_arm00_L");
			Document->ActiveCamera = -1;
			UpdateViewer();
			if (PC && PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0.f);
			TArray<FGuideHandle> BoneHandles;
			FVector2D BoneCenter;
			double BoneCm = 0.;
			const bool bBoneGuide = GuideHandles(BoneHandles, BoneCenter, BoneCm);
			// Beside the rings, the character keeps its own small move axes (10..12).
			const int32 RootAxes = BoneHandles.FilterByPredicate([](const FGuideHandle& H) { return H.Axis >= 10; }).Num();
			BoneHandles.RemoveAll([](const FGuideHandle& H) { return H.Axis > 2; });
			const bool bRings = bBoneGuide && BoneHandles.Num() == 3 && BoneHandles[0].bClosed && RootAxes == 3;
			FVector Start(0), Posed(0);
			P->Session.BoneEuler(TEXT("cf_j_arm00_L"), Start);
			const float In[3] = { float(Start.X), float(Start.Y), float(Start.Z) };
			float Want[3] = {};
			ok_unity_euler_rotate(In, 1, 10.f, Want);
			const bool bDragged = ApplyGuideDrag(1, FVector2D(20., 0.));
			SyncNow();
			const FOKSceneFkBone* Turned = Document->Find(A)->Character.Fk.FindByPredicate([](const FOKSceneFkBone& F) { return F.Bone == TEXT("cf_j_arm00_L"); });
			const bool bPosedOk = P->Session.BoneEuler(TEXT("cf_j_arm00_L"), Posed) && Posed.Equals(FVector(Want[0], Want[1], Want[2]), 0.05f);
			Check(bRings && bDragged && Turned && Turned->Euler.Equals(FVector(Want[0], Want[1], Want[2]), 0.01f) && bPosedOk,
				FString::Printf(TEXT("bone guide turns arm00_L about its local Y: %s -> %s"), *Start.ToString(), *Posed.ToString()));
			GuideBone.Reset();
			Document->Find(A)->Character.Fk.Reset();
			Document->NotifyChanged();
			SyncNow();
		}
		// Studio IK: a left-hand target starts on the hand; moving it carries the hand there; off gives the pose back.
		{
			Select(A);
			FTransform Hand0, Hand1, Hand2;
			const bool bHand0 = BoneWorld(P, TEXT("cf_j_hand_L"), Hand0);
			const bool bOn = Panel && Panel->SetIkEnabled(*Document->Find(A), 5, true);
			SyncNow();
			FVector Target0(0), Target1(0);
			const bool bAt = bOn && IkTargetWorld(5, Target0) && FVector::Dist(Target0, Hand0.GetLocation()) < 0.5;
			Check(bHand0 && bAt, FString::Printf(TEXT("an IK target starts on the hand (%.2f cm off)"), FVector::Dist(Target0, Hand0.GetLocation())));
			if (FOKSceneIkTarget* T = Document->Find(A)->Character.Ik.FindByPredicate([](const FOKSceneIkTarget& X) { return X.Effector == 5; })) T->Position += FVector(-12.f, -8.f, 10.f); // toward the body: the T-posed arm cannot reach further out
			Document->NotifyChanged();
			SyncNow();
			const bool bMoved = IkTargetWorld(5, Target1) && BoneWorld(P, TEXT("cf_j_hand_L"), Hand1) && FVector::Dist(Hand1.GetLocation(), Target1) < 1.0 && FVector::Dist(Hand1.GetLocation(), Hand0.GetLocation()) > 10.0;
			Check(bMoved, FString::Printf(TEXT("the hand follows its IK target (%.2f cm off, moved %.1f cm)"), FVector::Dist(Hand1.GetLocation(), Target1), FVector::Dist(Hand1.GetLocation(), Hand0.GetLocation())));
			GuideIk = 5;
			TArray<FGuideHandle> IkHandles;
			FVector2D IkCenter;
			double IkCm = 0.;
			const bool bIkHandles = GuideHandles(IkHandles, IkCenter, IkCm);
			IkHandles.RemoveAll([](const FGuideHandle& H) { return H.Axis > 2; });
			const bool bIkGuide = bIkHandles && IkHandles.Num() == 3 && !IkHandles[0].bClosed;
			const FVector Before2 = Document->Find(A)->Character.Ik[0].Position;
			const bool bDragged = bIkGuide && ApplyGuideDrag(0, (IkHandles[0].Points.Last() - IkHandles[0].Points[0]).GetSafeNormal() * 40.);
			Check(bIkGuide && bDragged && !Document->Find(A)->Character.Ik[0].Position.Equals(Before2, 0.1), TEXT("the IK guide moves the target"));
			GuideIk = -1;
			Panel->SetIkEnabled(*Document->Find(A), 5, false);
			SyncNow();
			const bool bBack = BoneWorld(P, TEXT("cf_j_hand_L"), Hand2) && FVector::Dist(Hand2.GetLocation(), Hand0.GetLocation()) < 0.5 && Document->Find(A)->Character.Ik.IsEmpty();
			Check(bBack, FString::Printf(TEXT("turning IK off gives the pose back (%.2f cm)"), FVector::Dist(Hand2.GetLocation(), Hand0.GetLocation())));
		}
		// Kept for the save / load round trip below.
		SOKStudioPanel::SetFk(*Document->Find(A), TEXT("cf_j_arm00_L"), FkSet);
		Document->NotifyChanged();
		SyncNow();
	}
	const FString Path = FPaths::ProjectSavedDir() / TEXT("OKStudio/game_selftest.okscene");
	FString Error;
	const FString Before = Document->ToJson();
	Step(TEXT("save"));
	const bool bSaved = Save(Path, Error);
	Document->Objects.Reset();
	SyncNow();
	Check(!SceneActor->PreviewOf(A), TEXT("clearing the scene removes its characters"));
	Step(TEXT("load"));
	const bool bLoaded = bSaved && Load(Path, Error);
	SyncNow();
	Check(bLoaded && Document->ToJson() == Before && SceneActor->PreviewOf(A) && SceneActor->PreviewOf(A)->Session.PartCount() > 0, TEXT("save, clear and load restores the scene: ") + (bLoaded ? Path : Error));
	Step(TEXT("scene card"));
	{
		// OK scene card: an APNG of four 1280x720 views, 3 s each; the scene and its character cards inside.
		const FString CardPath = FPaths::ProjectSavedDir() / TEXT("OKStudio/selftest_card.png");
		FString CardError;
		const bool bWritten = Save(CardPath, CardError);
		TArray<uint8> CardBytes;
		TArray<FKKPngFrame> Frames;
		uint32 Plays = 1;
		FString ApngError;
		const bool bApng = bWritten && FFileHelper::LoadFileToArray(CardBytes, *CardPath) && FKKAnimatedPng::Read(CardBytes, Frames, Plays, ApngError);
		FImage First;
		const bool bSize = bApng && FImageUtils::DecompressImage(CardBytes.GetData(), CardBytes.Num(), First) && First.SizeX == FOKSceneCard::Width && First.SizeY == FOKSceneCard::Height;
		const bool bTiming = Frames.Num() == 4 && !Frames.ContainsByPredicate([](const FKKPngFrame& F) { return !FMath::IsNearlyEqual(F.Seconds, 3.0, 0.01); });
		Check(bWritten && bApng && bSize && bTiming && Plays == 0 && FOKSceneCard::Kind(CardPath) == FOKSceneCard::EKind::OkScene,
			FString::Printf(TEXT("scene card: %d frames, first %dx%d, loop %u, %d bytes %s%s"), Frames.Num(), First.SizeX, First.SizeY, Plays, CardBytes.Num(), *CardError, *ApngError));
		// The four views differ (preview, front, side, top).
		bool bDistinct = Frames.Num() == 4;
		for (int32 I = 1; bDistinct && I < 4; ++I) bDistinct = Frames[I].Png != Frames[I - 1].Png;
		Check(bDistinct, TEXT("the scene card's four views differ"));
		// Opened again: the same scene (character cards from inside the card), characters rebuilt.
		auto WithoutCards = [](UOKSceneDocument* D) { UOKSceneDocument* C = DuplicateObject(D, GetTransientPackage()); for (FOKSceneObject& O : C->Objects) O.Character.Card.Reset(); return C->ToJson(); };
		const FString Expected = WithoutCards(Document);
		const TWeakObjectPtr<AKKPreviewActor> PreviousPreview = SceneActor->PreviewOf(A);
		const bool bHadPreviousPreview = PreviousPreview.IsValid();
		const bool bOpened = Load(CardPath, CardError);
		Check(bHadPreviousPreview && bOpened && !PreviousPreview.IsValid(), TEXT("opening a replacement releases the old preview before rebuilding"));
		SyncNow();
		const FOKSceneObject* Reopened = Document->Find(A);
		Check(bOpened && WithoutCards(Document) == Expected && Reopened && FPaths::FileExists(Reopened->Character.Card) && SceneActor->PreviewOf(A) && SceneActor->PreviewOf(A)->Session.PartCount() > 0,
			TEXT("opening the scene card restores the scene and its characters: ") + (bOpened ? (Reopened ? Reopened->Character.Card : FString()) : CardError));
		// Official KStudio scenes the tests use are in the OK base library (studio/samples): no game needed.
		const FString KStudio = AOKSceneActor::LibraryDirectory() / TEXT("studio/samples/koikatu_cs0000059.png");
		if (FPaths::FileExists(KStudio)) Check(FOKSceneCard::Kind(KStudio) == FOKSceneCard::EKind::KStudio, TEXT("an original KStudio scene card is recognised"));
		// Back to the scene's own card files before the extracted copies go.
		FString RestoreError;
		Document->FromJson(Before, RestoreError);
		Document->NotifyChanged();
		SyncNow();
		IFileManager::Get().Delete(*CardPath);
		IFileManager::Get().DeleteDirectory(*(FOKSceneCard::CardDirectoryFor(CardPath)), false, true);
		File.Reset();
	}
	{
		FVector ArmLoaded(0);
		const AKKPreviewActor* PL = SceneActor->PreviewOf(A);
		const bool bLoadedFk = PL && PL->Session.BoneEuler(TEXT("cf_j_arm00_L"), ArmLoaded) && ArmLoaded.Equals(FkSet, 0.05f);
		Check(bLoadedFk, FString::Printf(TEXT("a loaded scene poses the character with its FK %s"), *ArmLoaded.ToString()));
	}
	Step(TEXT("studio catalogue"));
	{
		const FOKStudioCatalog& Catalog = FOKStudioCatalog::Get();
		Check(Catalog.bLoaded && Catalog.Items.Num() >= 2000 && Catalog.Maps.Num() >= 70, FString::Printf(TEXT("studio catalogue: %d items, %d maps %s"), Catalog.Items.Num(), Catalog.Maps.Num(), *Catalog.Error));
		{
			// Installed classroom desk root: Unity z=-0.306 m. AddObjectItem's
			// guide overwrites that root with the scene TRS, rather than adding it.
			AOKSceneActor* RootTest = SceneActor->GetWorld()->SpawnActor<AOKSceneActor>();
			RootTest->Document = NewObject<UOKSceneDocument>(RootTest);
			const int32 Id = RootTest->Document->AddObject(EOKSceneObjectKind::Item, TEXT("root placement test"), -1);
			FOKSceneObject* Desk = RootTest->Document->Find(Id);
			Desk->Item.Group = 2; Desk->Item.Category = 12; Desk->Item.No = 681;
			Desk->Transform = FTransform(FRotator(0, 35, 0), FVector(110, 220, 330));
			RootTest->Sync();
			const AKKPreviewActor* Preview = RootTest->PreviewOf(Id);
			Check(Preview && Preview->GetActorTransform().TransformPosition(FVector(0, -30.6, 0)).Equals(Desk->Transform.GetLocation(), 0.05),
				TEXT("Studio item placement replaces the authored prefab root"));
			RootTest->Destroy();
		}
		const FOKStudioCatalogItem* Balloon = Catalog.Items.FindByPredicate([](const FOKStudioCatalogItem& I) { return I.Prefab == TEXT("p_acs_waterbaloon_stu"); });
		Select(-1);
		const int32 Picked = Add(EOKSceneObjectKind::Item);
		const bool bPicked = Balloon && Panel && Panel->PickItem(Balloon->Group, Balloon->Category, Balloon->No);
		SyncNow();
		const AKKPreviewActor* PB = SceneActor->PreviewOf(Picked);
		Check(bPicked && PB && PB->Session.PartCount() > 0, FString::Printf(TEXT("catalogue item %s from the browser: %d parts %s"), Balloon ? *Balloon->Name : TEXT("?"), PB ? PB->Session.PartCount() : 0, SceneActor->Errors.Contains(Picked) ? *SceneActor->Errors[Picked] : TEXT("")));
		if (FOKSceneObject* PO = Document->Find(Picked)) PO->Item.Colors = { FLinearColor(1.f, 0.f, 0.f, 1.f) };
		Document->NotifyChanged();
		SyncNow();
		const AKKPreviewActor* PRed = SceneActor->PreviewOf(Picked);
		Check(PRed && PRed != PB && PRed->ItemColorOverrides.Num() == 1 && PRed->ItemColorOverrides[0] == FLinearColor(1.f, 0.f, 0.f, 1.f) && PRed->Session.PartCount() > 0, TEXT("changing an item colour rebuilds it with the colour"));
		Document->RemoveObject(Picked);
		Document->Map = 0;
		SyncNow();
		Check(SceneActor->MapPartCount() > 0, FString::Printf(TEXT("scene map 0: %d parts %s"), SceneActor->MapPartCount(), *SceneActor->MapError));
		// OK_STUDIO_MAP_SHOT=<folder>: pictures of maps 0 and 1 for a look at where their rigid meshes landed.
		const FString MapShots = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_MAP_SHOT"));
		if (!MapShots.IsEmpty())
		{
			const FVector ShotFocus = Focus; const float ShotYaw = Yaw, ShotPitch = Pitch, ShotDistance = Distance;
			for (int32 MapNo : { 0, 1 })
			{
				Document->Map = MapNo;
				SyncNow();
				Focus = FVector(0.f, 0.f, 120.f); Yaw = 35.f; Pitch = -25.f; Distance = MapNo == 0 ? 650.f : 1800.f;
				UpdateViewer();
				FString ShotError;
				const FString Shot = MapShots / FString::Printf(TEXT("map_%d.png"), MapNo);
				Check(CaptureToPng(Shot, 1280, 720, ShotError), FString::Printf(TEXT("map %d picture %s (%d parts) %s"), MapNo, *Shot, SceneActor->MapPartCount(), *ShotError));
			}
			Document->Map = 0;
			Focus = ShotFocus; Yaw = ShotYaw; Pitch = ShotPitch; Distance = ShotDistance;
			UpdateViewer();
			SyncNow();
		}
		const FString WithMap = Document->ToJson();
		FString MapError;
		Check(Document->FromJson(WithMap, MapError) && Document->Map == 0, TEXT("the scene map is saved with the scene"));
		Document->Map = -1;
		SyncNow();
		Check(!SceneActor->MapPreview(), TEXT("clearing the map removes it"));
	}
	Step(TEXT("timeline"));
	{
		const int32 Moving = Add(EOKSceneObjectKind::Light);
		Document->Find(Moving)->Transform.SetLocation(FVector(0.f, 0.f, 100.f));
		Document->Find(Moving)->Light.Intensity = 2.f;
		Document->SetKey(Moving, 0.f);
		Document->Find(Moving)->Transform.SetLocation(FVector(200.f, 0.f, 100.f));
		Document->Find(Moving)->Light.Intensity = 10.f;
		Document->SetKey(Moving, 2.f);
		SetTimelineTime(1.f);
		const FVector Mid = Document->Find(Moving)->Transform.GetLocation();
		const float MidIntensity = Document->Find(Moving)->Light.Intensity;
		Check(Mid.Equals(FVector(100.f, 0.f, 100.f), 0.01f) && FMath::IsNearlyEqual(MidIntensity, 6.f, 0.01f), FString::Printf(TEXT("timeline interpolates between keys: %s, intensity %.2f"), *Mid.ToString(), MidIntensity));
		const FString Json = Document->ToJson();
		UOKSceneDocument* Copy = NewObject<UOKSceneDocument>();
		FString TimelineError;
		const bool bRound = Copy->FromJson(Json, TimelineError) && Copy->TrackOf(Moving) && Copy->TrackOf(Moving)->Keys.Num() == 2;
		Check(bRound, TEXT("timeline tracks are saved with the scene"));
		bTimelinePlaying = true;
		SetTimelineTime(0.f);
		Tick(0.5f);
		bTimelinePlaying = false;
		Check(FMath::IsNearlyEqual(TimelineTime, 0.5f, 0.01f) && Document->Find(Moving)->Transform.GetLocation().Equals(FVector(50.f, 0.f, 100.f), 0.01f), TEXT("playback advances the timeline"));
		// Curves: ease in-out from the first key is at 2 * 0.25^2 of the way a quarter in (x 25 of 200 at 0.5 s); a step holds the key.
		Document->KeyAt(Moving, 0.f)->Ease = EOKSceneEase::EaseInOut;
		SetTimelineTime(0.f);
		Document->SetKey(Moving, 0.f);
		const bool bKept = Document->KeyAt(Moving, 0.f)->Ease == EOKSceneEase::EaseInOut;
		SetTimelineTime(0.5f);
		const float Eased = float(Document->Find(Moving)->Transform.GetLocation().X);
		Document->KeyAt(Moving, 0.f)->Ease = EOKSceneEase::Step;
		SetTimelineTime(1.9f);
		const float Held = float(Document->Find(Moving)->Transform.GetLocation().X);
		Check(bKept && FMath::IsNearlyEqual(Eased, 25.f, 0.05f) && FMath::IsNearlyEqual(Held, Document->TrackOf(Moving)->Keys[0].Transform.GetLocation().X, 0.01f),
			FString::Printf(TEXT("timeline curves: ease in-out x %.2f at 0.5 s, step holds x %.2f; re-keying keeps the curve %d"), Eased, Held, bKept ? 1 : 0));
		Document->KeyAt(Moving, 0.f)->Ease = EOKSceneEase::Linear;
		// A smooth path (camera path) passes through a middle key without a corner: off the straight line between keys.
		{
			FOKSceneObject* M = Document->Find(Moving);
			const FTransform SavedAt2 = Document->TrackOf(Moving)->Keys.Last().Transform;
			M->Transform.SetLocation(FVector(100.f, 100.f, 100.f));
			Document->SetKey(Moving, 1.f);
			Document->TrackOf(Moving)->bSmoothPath = true;
			SetTimelineTime(1.f);
			const FVector OnKey = Document->Find(Moving)->Transform.GetLocation();
			SetTimelineTime(0.5f);
			const FVector Curve = Document->Find(Moving)->Transform.GetLocation();
			Document->TrackOf(Moving)->bSmoothPath = false;
			SetTimelineTime(0.5f);
			const FVector Straight = Document->Find(Moving)->Transform.GetLocation();
			Check(OnKey.Equals(FVector(100.f, 100.f, 100.f), 0.01f) && Straight.Equals(FVector(50.f, 50.f, 100.f), 0.01f) && Curve.Y > Straight.Y + 1.f,
				FString::Printf(TEXT("smooth path through the key %s: half way %s (straight %s)"), *OnKey.ToString(), *Curve.ToString(), *Straight.ToString()));
			Document->RemoveKey(Moving, 1.f);
			Document->TrackOf(Moving)->Keys.Last().Transform = SavedAt2;
		}
		Check(Document->RemoveKey(Moving, 2.f) && Document->TrackOf(Moving)->Keys.Num() == 1, TEXT("removing a key"));
		// A character's FK is part of its keys: the bone turns between the keyed rotations.
		{
			FOKSceneObject* Ch = Document->Find(A);
			const FOKSceneCharacter SavedCharacter = Ch->Character;
			const FTransform SavedTransform = Ch->Transform;
			Ch->Character.Fk = { { TEXT("cf_j_arm00_L"), FVector(10.f, 0.f, 0.f) } };
			Document->SetKey(A, 0.f);
			Document->Find(A)->Character.MouthOpen = 0.f;
			Document->Find(A)->Character.EyesPattern = 1;
			Document->SetKey(A, 0.f);
			Document->Find(A)->Character.Fk = { { TEXT("cf_j_arm00_L"), FVector(50.f, 0.f, 0.f) } };
			Document->Find(A)->Character.MouthOpen = 1.f;
			Document->Find(A)->Character.EyesPattern = 4;
			Document->SetKey(A, 2.f);
			SetTimelineTime(1.f);
			SyncNow();
			const FOKSceneFkBone* MidBone = Document->Find(A)->Character.Fk.FindByPredicate([](const FOKSceneFkBone& B) { return B.Bone == TEXT("cf_j_arm00_L"); });
			FVector Posed(0);
			const bool bPosed = SceneActor->PreviewOf(A) && SceneActor->PreviewOf(A)->Session.BoneEuler(TEXT("cf_j_arm00_L"), Posed);
			Check(MidBone && MidBone->Euler.Equals(FVector(30.f, 0.f, 0.f), 0.05f) && bPosed && Posed.Equals(FVector(30.f, 0.f, 0.f), 0.05f),
				FString::Printf(TEXT("timeline FK keys turn the bone halfway: %s, posed %s"), MidBone ? *MidBone->Euler.ToString() : TEXT("?"), *Posed.ToString()));
			const FOKSceneCharacter& MidC = Document->Find(A)->Character;
			Check(FMath::IsNearlyEqual(MidC.MouthOpen, 0.5f, 0.01f) && MidC.EyesPattern == 1, FString::Printf(TEXT("timeline face keys: mouth %.2f blends, eyes pattern %d holds until the next key"), MidC.MouthOpen, MidC.EyesPattern));
			SetTimelineTime(2.f);
			Check(Document->Find(A)->Character.EyesPattern == 4, TEXT("the eyes pattern switches at its key"));
			// The timeline as an OK animation: played back without the FK keys, the arm is halfway at 1 s.
			{
				const float SavedDuration = Document->Duration;
				Document->Duration = 2.f;
				FString AnimError;
				const FString AnimFile = ExportOkAnim(A, 30.f, AnimError);
				Document->Duration = SavedDuration;
				UOKAnimSequence* Recorded = AnimFile.IsEmpty() ? nullptr : UOKAnimationLibrary::LoadOkAnimFile(AnimFile, nullptr, NAME_None, AnimError);
				const TSharedPtr<FOkAnimPlayer> Player = Recorded ? Recorded->CreatePlayer(AnimError) : nullptr;
				AKKPreviewActor* Actor = SceneActor->PreviewOf(A);
				FVector Played(0);
				bool bPlayed = false;
				if (Player && Actor)
				{
					Actor->SetUserFk({}, {});
					bPlayed = Actor->SetOkAnimation(Player, false);
					Actor->PoseMode = 3;
					Actor->AnimPhase = 1.f;
					Actor->ApplyPoseFrame(0.f, false);
					bPlayed = bPlayed && Actor->Session.BoneEuler(TEXT("cf_j_arm00_L"), Played);
				}
				Check(Recorded && bPlayed && Played.Equals(FVector(30.f, 0.f, 0.f), 0.5f) && Recorded->GetPlayLength() > 1.9f,
					FString::Printf(TEXT("timeline to .okanim: %s, %.2f s, arm at 1 s %s %s"), *FPaths::GetCleanFilename(AnimFile), Recorded ? Recorded->GetPlayLength() : 0.f, *Played.ToString(), *AnimError));
				if (!AnimFile.IsEmpty()) IFileManager::Get().Delete(*AnimFile);
				// The test played the file on the character's preview: back to the document's character.
				SceneActor->Rebuild(A);
			}
			Document->Tracks.RemoveAll([&](const FOKSceneTrack& T2) { return T2.ObjectId == A; });
			Document->Find(A)->Character = SavedCharacter;
			Document->Find(A)->Transform = SavedTransform;
			Document->NotifyChanged();
			SyncNow();
		}
		Document->RemoveObject(Moving);
		Document->Tracks.Reset();
		Document->Duration = 10.f;
		Select(-1);
		SyncNow();
	}
	Step(TEXT("effect"));
	{
		const FOKStudioEffect* Fireflies = FOKStudioCatalog::Get().FindEffect(TEXT("fireflies"));
		const int32 Fx = Add(EOKSceneObjectKind::Effect);
		Document->Find(Fx)->Effect.Effect = TEXT("fireflies");
		Document->Find(Fx)->Effect.Speed = 0.5f;
		Document->Find(Fx)->Transform.SetLocation(FVector(0.f, 50.f, 100.f));
		Document->NotifyChanged();
		SyncNow();
		const UNiagaraComponent* Nc = Cast<UNiagaraComponent>(SceneActor->ComponentOf(Fx));
		Check(Fireflies && Nc && Nc->GetAsset() && Nc->GetAsset()->GetPathName() == Fireflies->Asset && Nc->IsActive() && FMath::IsNearlyEqual(Nc->GetCustomTimeDilation(), 0.5f),
			FString::Printf(TEXT("Niagara effect fireflies: component %d, asset %s, active %d %s"), Nc ? 1 : 0, Nc && Nc->GetAsset() ? *Nc->GetAsset()->GetPathName() : TEXT("-"), Nc && Nc->IsActive() ? 1 : 0,
				SceneActor->Errors.Contains(Fx) ? *SceneActor->Errors[Fx] : TEXT("")));
		const FString Json = Document->ToJson();
		UOKSceneDocument* FxCopy = NewObject<UOKSceneDocument>();
		FString FxError;
		Check(FxCopy->FromJson(Json, FxError) && FxCopy->Find(Fx) && FxCopy->Find(Fx)->Effect == Document->Find(Fx)->Effect, TEXT("effects are saved with the scene"));
		const FOKStudioEffect* Imported = FOKStudioCatalog::Get().EffectForKks(6, 148, 2529);
		Check(Imported && Imported->Id == TEXT("fireflies") && FOKStudioCatalog::Get().Effects.Num() >= 8, FString::Printf(TEXT("the KKS firefly item becomes the OK fireflies effect; %d effects"), FOKStudioCatalog::Get().Effects.Num()));
		Document->RemoveObject(Fx);
		Document->NotifyChanged();
		SyncNow();
	}
	Step(TEXT("item fk"));
	{
		const int32 FkItem = Add(EOKSceneObjectKind::Item);
		FOKSceneItem& FI = Document->Find(FkItem)->Item;
		FI.Group = 15; FI.Category = 74; FI.No = 978;
		Document->NotifyChanged();
		SyncNow();
		FString Model, Prefab, ModelError;
		const TArray<FString> Bones = AOKSceneActor::ItemModel(Document->Find(FkItem)->Item, Model, Prefab, ModelError) ? FOkSession::OkModelFkBones(AOKSceneActor::LibraryDirectory(), Model, Prefab) : TArray<FString>();
		// The item's vertices (session parts, model space): their centre moves when a bone turns part of the mesh.
		auto Centre = [&](int32& OutVerts)
		{
			FVector Sum(0);
			OutVerts = 0;
			const AKKPreviewActor* P = SceneActor->PreviewOf(FkItem);
			for (int32 Part = 0; P && Part < P->Session.PartCount(); ++Part)
			{
				OkMeshView V;
				if (!P->Session.PartAt(Part, V) || !V.positions) continue;
				for (int32 I = 0; I < V.n_verts; ++I) Sum += FVector(V.positions[I * 3], V.positions[I * 3 + 1], V.positions[I * 3 + 2]);
				OutVerts += V.n_verts;
			}
			return OutVerts ? Sum / OutVerts * 100.0 : FVector::ZeroVector;
		};
		int32 N0 = 0, N1 = 0, N2 = 0;
		const FVector C0 = Centre(N0);
		Check(Bones.Contains(TEXT("neo_J_fk_kansetu_00")) && N0 > 0, FString::Printf(TEXT("FK item 15/74/978: %d FK bones, %d vertices shown"), Bones.Num(), N0));
		Document->Find(FkItem)->Item.Fk = { { TEXT("neo_J_fk_kansetu_00"), FVector(0.f, 0.f, 60.f) } };
		Document->NotifyChanged();
		SyncNow();
		const FVector C1 = Centre(N1);
		Document->Find(FkItem)->Item.Fk.Reset();
		Document->NotifyChanged();
		SyncNow();
		const FVector C2 = Centre(N2);
		Check(N1 == N0 && N2 == N0 && FVector::Dist(C0, C1) > 0.1 && FVector::Dist(C0, C2) < 0.001, FString::Printf(TEXT("item FK turns the item (vertex centre moves %.2f cm) and clearing restores it (%.4f cm)"), FVector::Dist(C0, C1), FVector::Dist(C0, C2)));
		Check(!SceneActor->Errors.Contains(FkItem), TEXT("the FK item has no errors"));
		Document->RemoveObject(FkItem);
		Document->NotifyChanged();
		SyncNow();
	}
	Step(TEXT("look modes"));
	{
		AKKPreviewActor* PL = SceneActor->PreviewOf(A);
		FVector Head0(0), Head1(0), Head2(0);
		const bool bH0 = PL && PL->Session.BoneEuler(TEXT("cf_j_neck"), Head0);
		FOKSceneCharacter& LC = Document->Find(A)->Character;
		LC.NeckLook = EOKSceneLook::Point;
		LC.LookPoint = FVector(150.f, 60.f, 140.f);
		for (int32 I = 0; I < 30; ++I) SceneActor->UpdateLooks(1.f / 30.f);
		const bool bH1 = PL && PL->Session.BoneEuler(TEXT("cf_j_neck"), Head1) && !Head1.Equals(Head0, 2.f);
		Check(bH0 && bH1, FString::Printf(TEXT("the neck turns to the look point: %s -> %s"), *Head0.ToString(), *Head1.ToString()));
		Document->Find(A)->Character.NeckLook = EOKSceneLook::Animation;
		for (int32 I = 0; I < 30; ++I) SceneActor->UpdateLooks(1.f / 30.f);
		const bool bH2 = PL && PL->Session.BoneEuler(TEXT("cf_j_neck"), Head2) && Head2.Equals(Head0, 0.5f);
		Check(bH2, FString::Printf(TEXT("back to the animation's neck: %s"), *Head2.ToString()));
	}
    Step(TEXT("saved fixed neck"));
    {
        AKKPreviewActor* Preview = SceneActor->PreviewOf(A);
        FVector SavedNeckBefore;
        const bool bBefore = Preview && Preview->Session.BoneEuler(TEXT("cf_j_head"), SavedNeckBefore);
        auto NativeQuat = [](const FVector& Euler)
        {
            const FQuat U = FOKStudioImport::UnityToUe(FVector::ZeroVector, Euler, FVector::OneVector).GetRotation();
            return FQuat(-U.X, U.Z, U.Y, U.W);
        };
        FOKSceneCharacter& Character = Document->Find(A)->Character;
        Character.FixedNeck = { NativeQuat(FVector(9,-12,3)), NativeQuat(FVector(18,-27,7)) };
        Character.NeckLook = EOKSceneLook::Fixed;
        Document->NotifyChanged();
        SyncNow();
        FVector SavedNeckFixed;
        const bool bFixed = Preview && Preview->Session.BoneEuler(TEXT("cf_j_head"), SavedNeckFixed);
        Check(bFixed && FMath::Abs(NativeQuat(SavedNeckFixed) | Character.FixedNeck[1]) > .99999, TEXT("saved FIX mode applies the source head local"));
        Character.NeckLook = EOKSceneLook::Animation;
        Document->NotifyChanged();
        SyncNow();
        FVector SavedNeckRestored;
        const bool bRestored = Preview && Preview->Session.BoneEuler(TEXT("cf_j_head"), SavedNeckRestored);
        Check(bBefore && bRestored && FMath::Abs(NativeQuat(SavedNeckRestored) | NativeQuat(SavedNeckBefore)) > .99999, TEXT("animation mode clears runtime FIX and restores the animated head"));
        Character.NeckLook = EOKSceneLook::Fixed;
        Document->NotifyChanged();
        SyncNow();
        const bool bAgain = Preview && Preview->Session.BoneEuler(TEXT("cf_j_head"), SavedNeckFixed);
        Check(bAgain && FMath::Abs(NativeQuat(SavedNeckFixed) | Character.FixedNeck[1]) > .99999, TEXT("saved FIX can be selected again without reimport"));
        Character.FixedNeck.Reset();
        Character.NeckLook = EOKSceneLook::Animation;
        Document->NotifyChanged();
        SyncNow();
    }
	Step(TEXT("post and frames"));
	{
		Document->Post.bBloom = true; Document->Post.BloomIntensity = 2.5f;
		Document->Post.bVignette = true; Document->Post.SourceVignetteIntensity = 0.3f;
		Document->Post.bDepthOfField = true; Document->Post.FocalDistance = 250.f;
		Document->Post.ShaderType = 0;
		Tick(0.f);
		const FPostProcessSettings& S = Viewer->GetCameraComponent()->PostProcessSettings;
		const bool bSourceBloom = OKStudioSourceBloomState() == TEXT("ready") && S.BloomIntensity == 0.f && S.WeightedBlendables.Array.Num() >= 8;
		// Studio's vignette: the source grading pass with k = 1 / (1 - 0.3) - 1 and Unreal's own vignette off, or
		// Unreal's fallback vignette while that pass is not ready.
		float SourceVignette = -1.f;
		for (const FWeightedBlendable& B : S.WeightedBlendables.Array)
			if (const auto* M = Cast<UMaterialInstanceDynamic>(B.Object)) { float V = 0.f; if (M->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Vignette")), V)) SourceVignette = V; }
		const bool bVignetteReaches = (FMath::IsNearlyEqual(SourceVignette, 1.f / 0.7f - 1.f, 1e-4f) && S.VignetteIntensity == 0.f)
			|| (SourceVignette < 0.f && FMath::IsNearlyEqual(S.VignetteIntensity, Document->Post.VignetteIntensity));
		Check(S.bOverride_BloomIntensity && (bSourceBloom || S.BloomIntensity >= 2.5f) && bVignetteReaches && S.bOverride_DepthOfFieldFocalDistance && FMath::IsNearlyEqual(S.DepthOfFieldFocalDistance, 250.f),
			FString::Printf(TEXT("scene post-processing reaches the view (bloom, vignette %.4f, depth of field)"), SourceVignette));
		UOKSceneDocument* PostCopy = NewObject<UOKSceneDocument>();
		FString PostError;
		Check(PostCopy->FromJson(Document->ToJson(), PostError) && PostCopy->Post.bBloom && FMath::IsNearlyEqual(PostCopy->Post.SourceVignetteIntensity, 0.3f), TEXT("post-processing is saved with the scene"));
        Check(PostCopy->Post.ShaderType == 0 && PostCopy->Post.ShaderGlobals().FindRef(TEXT("_KanoVerShift")).R == 0.f,
            TEXT("saved Studio mode zero overrides a card's Koikano mode"));
        PostCopy->Post.ShaderType = 2;
        Check(PostCopy->Post.ShaderGlobals().FindRef(TEXT("_KanoVerShift")).R == 1.f,
            TEXT("nonzero Studio shader modes follow ChaShader.ChangeShaderType"));
        Check(!FOKScenePost().ShaderGlobals().Contains(TEXT("_KanoVerShift")),
            TEXT("legacy scenes without a shader mode preserve the card default"));
		Document->Post = FOKScenePost();
		Tick(0.f);
		const float SavedDuration = Document->Duration;
		Document->Duration = 0.1f;
		const FString Dir = ExportFrames(30.f, 320, 180);
		Check(IsExporting() && ExportFrameCount() == 3, FString::Printf(TEXT("frame export plans %d frames into %s"), ExportFrameCount(), *Dir));
		for (int32 Guard = 0; IsExporting() && Guard < 20; ++Guard) TickExport();
		TArray<FString> Frames;
		IFileManager::Get().FindFiles(Frames, *(Dir / TEXT("frame_*.png")), true, false);
		Check(!IsExporting() && Frames.Num() == 3, FString::Printf(TEXT("frame export writes %d of 3 PNG frames"), Frames.Num()));
		// The same frames as an mp4 (ffmpeg is a local tool; without it the check is skipped).
		if (FPaths::FileExists(FfmpegPath()))
		{
			const FString Movie = Dir + TEXT(".mp4");
			FString Encoder, MovieError;
			const bool bMovie = EncodeMp4(Dir, 30.f, Movie, Encoder, MovieError);
			const int64 Bytes = IFileManager::Get().FileSize(*Movie);
			Check(bMovie && Bytes > 1000, FString::Printf(TEXT("mp4 export: %lld bytes with %s %s"), Bytes, *Encoder, *MovieError));
			IFileManager::Get().Delete(*Movie);
		}
		else Lines.Add(TEXT("SKIP  mp4 export (no Tools/ffmpeg/ffmpeg.exe)"));
		IFileManager::Get().DeleteDirectory(*Dir, false, true);
		Document->Duration = SavedDuration;
	}
	Step(TEXT("screenshot"));
	{
		const FString Shot = Screenshot(640, 360);
		TArray<uint8> ShotBytes;
		const bool bShot = !Shot.IsEmpty() && FFileHelper::LoadFileToArray(ShotBytes, *Shot) && ShotBytes.Num() > 1000;
		FImage Decoded;
		const bool bSize = bShot && FImageUtils::DecompressImage(ShotBytes.GetData(), ShotBytes.Num(), Decoded) && Decoded.SizeX == 640 && Decoded.SizeY == 360;
		Check(bShot && bSize, FString::Printf(TEXT("screenshot 640x360 written (%d bytes) %s"), ShotBytes.Num(), *Shot));
		if (bShot) IFileManager::Get().Delete(*Shot);
		// Transparent background: the characters opaque, the rest clear.
		const FString Clear = Screenshot(640, 360, true);
		TArray<uint8> ClearBytes;
		FImage ClearImage;
		int64 Opaque = 0, Transparent = 0;
		if (!Clear.IsEmpty() && FFileHelper::LoadFileToArray(ClearBytes, *Clear) && FImageUtils::DecompressImage(ClearBytes.GetData(), ClearBytes.Num(), ClearImage))
		{
			ClearImage.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
			for (const FColor& C : ClearImage.AsBGRA8()) { if (C.A == 255) ++Opaque; else if (C.A == 0) ++Transparent; }
		}
		const int64 All = int64(640) * 360;
		Check(Opaque > All / 200 && Transparent > All / 2 && Opaque + Transparent == All, FString::Printf(TEXT("transparent screenshot: %lld opaque, %lld clear of %lld pixels"), Opaque, Transparent, All));
		if (!Clear.IsEmpty()) IFileManager::Get().Delete(*Clear);
	}
	Step(TEXT("studio animation"));
	{
		const FOKStudioCatalog& Catalog = FOKStudioCatalog::Get();
		const int32 Walk = Catalog.Anims.IndexOfByPredicate([](const FOKStudioAnim& X) { return X.Kind == 0 && !X.File.IsEmpty() && X.Duration > 1.f && X.bLooping; });
		Select(A);
		// FK (kept from the save / load check above) would hold its bones over the animation.
		Document->Find(A)->Character.Fk.Reset();
		const bool bPicked = Walk != INDEX_NONE && Panel && Panel->PickAnim(Walk);
		SyncNow();
		AKKPreviewActor* PA2 = SceneActor->PreviewOf(A);
		FVector E0(0), E1(0);
		const bool bE0 = PA2 && PA2->Session.BoneEuler(TEXT("cf_j_arm00_L"), E0);
		SceneActor->AdvanceAnimations(Walk != INDEX_NONE ? Catalog.Anims[Walk].Duration * 0.37f : 0.5f);
		const bool bMoved = PA2 && PA2->PoseMode == 3 && PA2->Session.BoneEuler(TEXT("cf_j_arm00_L"), E1) && !E1.Equals(E0, 0.5f);
		Check(bPicked && bE0 && bMoved && !SceneActor->Errors.Contains(A), FString::Printf(TEXT("library Studio animation %s plays (%d animations, arm %s -> %s) %s"),
			Walk != INDEX_NONE ? *Catalog.Anims[Walk].Name : TEXT("?"), Catalog.Anims.FilterByPredicate([](const FOKStudioAnim& X) { return !X.File.IsEmpty(); }).Num(), *E0.ToString(), *E1.ToString(), SceneActor->Errors.Contains(A) ? *SceneActor->Errors[A] : TEXT("")));
        if (PA2)
        {
            const bool ClockPreviousPause = SceneActor->bPauseAnimations;
            const float ClockPreviousSpeed = Document->Find(A)->Character.Speed;
            const float ClockHeldPhase = PA2->AnimPhase;
            const double ClockHeldItems = Document->ItemAnimationSeconds;
            SceneActor->bPauseAnimations = true;
            for (int32 FrameIndex = 0; FrameIndex < 12; ++FrameIndex) SceneActor->Tick(1.f / 60.f);
            Check(PA2->AnimPhase == ClockHeldPhase && Document->ItemAnimationSeconds == ClockHeldItems,
                TEXT("paused Studio character frames preserve clip and item clocks"));
            Document->Find(A)->Character.Speed = 0.f;
            SceneActor->bPauseAnimations = false;
            SceneActor->Tick(1.f / 60.f);
            Check(PA2->AnimPhase == ClockHeldPhase && Document->ItemAnimationSeconds > ClockHeldItems,
                TEXT("zero character speed holds its clip while the scene frame continues"));
            // A native FK setter only queues the source change. A static actor
            // without an animation player must still receive the finishing frame.
            PA2->SetPoseMode(0);
            const FVector ClockStaticFk(12.f, 18.f, -35.f);
            const bool ClockFkQueued = PA2->Session.SetFk({ TEXT("cf_j_arm00_L") }, { ClockStaticFk });
            PA2->bUserPoseDirty = false;
            SceneActor->AdvanceCharacterFrames(1.f / 60.f, false);
            FVector ClockStaticActual;
            Check(ClockFkQueued && PA2->Session.BoneEuler(TEXT("cf_j_arm00_L"), ClockStaticActual)
                && ClockStaticActual.Equals(ClockStaticFk, .01f),
                TEXT("held static Studio character receives the native pose and physics finishing pass"));
            PA2->Session.SetFk({}, {});
            Document->Find(A)->Character.Speed = ClockPreviousSpeed;
            SceneActor->bPauseAnimations = ClockPreviousPause;
        }
		Document->Find(A)->Character.StudioAnimeNo = -1;
		Document->NotifyChanged();
		SyncNow();
	}
	Step(TEXT("kstudio import"));
	{
		const FString Samples = AOKSceneActor::LibraryDirectory() / TEXT("studio/samples");
		const FString ClearPng = Samples / TEXT("koikatu_cs0005698.png");
		if (FPaths::FileExists(ClearPng))
		{
			UOKSceneDocument* Clear = NewObject<UOKSceneDocument>(this);
			Clear->Post.BackgroundColor = FLinearColor::Red;
			FOKStudioImport::FReport ClearReport; FString ClearError;
			Check(FOKStudioImport::Import(ClearPng, Clear, FPaths::ProjectSavedDir() / TEXT("OKStudio/clear_color_selftest_cards"), ClearReport, ClearError)
				&& Clear->Map < 0 && !Clear->Post.bSky && Clear->Post.Background.IsEmpty()
				&& Clear->Post.BackgroundColor.Equals(FLinearColor::Black),
				TEXT("unmapped source scene takes the plain clear colour its picture shows (5698: black)"));
			const FString BluePng = Samples / TEXT("koikatu_cs0002534.png");
			UOKSceneDocument* Blue = NewObject<UOKSceneDocument>(this);
			Check(FOKStudioImport::Import(BluePng, Blue, FPaths::ProjectSavedDir() / TEXT("OKStudio/clear_color_selftest_cards"), ClearReport, ClearError)
				&& Blue->Post.BackgroundColor.B > 0.5f && Blue->Post.BackgroundColor.B > Blue->Post.BackgroundColor.R + 0.2f,
				TEXT("unmapped source scene takes its author's clear colour from its picture (2534: blue)"));
			UOKSceneDocument* SavedClear = NewObject<UOKSceneDocument>(this);
			Check(SavedClear->FromJson(Clear->ToJson(), ClearError) && SavedClear->Post.BackgroundColor.Equals(Clear->Post.BackgroundColor),
				TEXT("source clear colour survives scene save and reopen"));
		}
		// Import into an existing document must equal a fresh import, including
		// neutral source values that do not enter the positive-effect branches.
		const FString NeutralPng = Samples / TEXT("koikatu_cs0003800.png");
		if (FPaths::FileExists(NeutralPng))
		{
			UOKSceneDocument* Fresh = NewObject<UOKSceneDocument>(this);
			UOKSceneDocument* Reused = NewObject<UOKSceneDocument>(this);
			Reused->Post.bDepthOfField = true;
			Reused->Post.FocalDistance = 10.f;
			Reused->Post.BackgroundColor = FLinearColor::Red;
			Reused->Post.Exposure = -3.f;
			Reused->Duration = 99.f;
			Reused->Tracks.AddDefaulted();
			Reused->MapTransform.SetLocation(FVector(100.f));
			FOKStudioImport::FReport FreshReport, ReusedReport;
			FString ImportError;
			const FString CardDir = FPaths::ProjectSavedDir() / TEXT("OKStudio/import_reset_selftest_cards");
			const bool bFresh = FOKStudioImport::Import(NeutralPng, Fresh, CardDir, FreshReport, ImportError);
			const bool bReused = FOKStudioImport::Import(NeutralPng, Reused, CardDir, ReusedReport, ImportError);
			Check(bFresh && bReused && Fresh->ToJson() == Reused->ToJson() && !Reused->Post.bDepthOfField,
				TEXT("scene import clears previous DOF, background, exposure and timeline state"));
			const FOKSceneObject* SourceLight = Fresh->Objects.FindByPredicate([](const FOKSceneObject& O) {
				return O.Kind == EOKSceneObjectKind::Light && O.Light.bCharactersOnly;
			});
			if (bFresh && SourceLight)
			{
				const FVector F = Fresh->WorldTransform(SourceLight->Id).GetRotation().GetForwardVector();
				const FVector Towards(F.X, -F.Z, -F.Y);
				const FTransform LightBefore = Fresh->WorldTransform(SourceLight->Id);
				Fresh->Find(Fresh->ActiveCamera)->Transform.SetRotation(FQuat(FVector::UpVector, 1.f));
				Check(SourceLight->Parent == -1 && Towards.Equals(FVector(-0.3033395, 0.190809235, -0.9335829), 0.00001)
					&& Fresh->WorldTransform(SourceLight->Id).Equals(LightBefore),
					TEXT("native Studio light direction includes authored child rotation and is independent of camera"));
			}
			else Check(false, TEXT("native Studio character light fixture exists"));
            const FLinearColor Sun=OKStudioLinearLightColor(FLinearColor(.8948067f,.9558824f,.892625451f,1),1.1f);
            Check(Sun.Equals(FLinearColor(.964627564f,1.11674428f,.959291399f,1.1f),.000001f),TEXT("map light gamma and HDR intensity match native Standard GPU constants"));
			FLinearColor Red, Green, Blue;
			Check(OKStudioMapAmbient(20, Red, Green, Blue)
				&& Red.Equals(FLinearColor(0.007252015f, -0.00568231149f, -0.0125606805f, 0.169953629f), 0.000001f)
				&& Green.Equals(FLinearColor(0.0114032608f, 0.04134383f, -0.0197532624f, 0.211334154f), 0.000001f),
				TEXT("map ambient packed coefficients match the native renderer GPU buffer"));
		}
		const FString Png = Samples / TEXT("koikatu_cs0000059.png");
		if (FPaths::FileExists(Png))
		{
			UOKSceneDocument* Saved = DuplicateObject(Document.Get(), this);
			FOKStudioImport::FReport Report;
			FString ImportError;
			const FString CardDir = FPaths::ProjectSavedDir() / TEXT("OKStudio/import_selftest_cards");
			const bool bImported = FOKStudioImport::Import(Png, Document, CardDir, Report, ImportError);
			SyncNow();
			// Lighting is submitted by the scene frame after building the renderers.
			if (SceneActor) SceneActor->Tick(0.f);
			int32 Built = 0, Chars = 0, Items = 0, ItemsBuilt = 0;
			int32 AuthoredHairMaterials = 0;
			bool bStudioLighting = true;
			float HairShadowError = 0.f;
			float LightAlphaError = 0.f;
			bool bItemsUseSharedNeutralDepth = true;
			TArray<FLinearColor> ExpectedHigher;
			FLinearColor AmbientR, AmbientG, AmbientB;
			bool bMapAmbient = OKStudioMapAmbient(Document->Map, AmbientR, AmbientG, AmbientB, &ExpectedHigher);
			int32 AmbientPattern = -1;
			if (!OKStudioMapSky(Document->Map, Document->Post.SunType, AmbientPattern) && Document->Post.bSky) AmbientPattern=Document->Post.SkyPattern;
			if (AmbientPattern >= 0 && OKStudioSkyAmbient(AmbientPattern, AmbientR, AmbientG, AmbientB))
			{
				ExpectedHigher.Init(FLinearColor(0,0,0,0),4); bMapAmbient=true;
			}
			int32 HigherSamples = 0;
			float HigherError = 0.f;
			FString HigherWitness;
			for (const FOKSceneObject& O : Document->Objects)
			{
				const AKKPreviewActor* PO = SceneActor->PreviewOf(O.Id);
				if (O.Kind == EOKSceneObjectKind::Character) { ++Chars; if (PO && PO->Session.PartCount() > 0) ++Built; }
				if (O.Kind == EOKSceneObjectKind::Character && PO)
				{
					bStudioLighting &= PO->bCharacterDisplayShadows && PO->CharacterDisplayShadowsOverride.IsSet();
					for (const FKKLoadedPart& Part : PO->Parts)
					{
						OkMeshView V{};
						if (!Part.SurfaceMat || !PO->Session.PartAt(Part.SessionIndex, V) || (V.shader_kind != 1 && V.shader_kind != 10)) continue;
						FLinearColor Actual;
						if (!Part.SurfaceMat->GetVectorParameterValue(FMaterialParameterInfo(TEXT("_ShadowColor")), Actual)) continue;
						++AuthoredHairMaterials;
						if (bMapAmbient)
						{
							const FName Names[] = { TEXT("unity_SHBr"), TEXT("unity_SHBg"), TEXT("unity_SHBb"), TEXT("unity_SHC") };
							for (int32 I=0; I<4; ++I)
							{
								FLinearColor Value;
								if (!Part.SurfaceMat->GetVectorParameterValue(FMaterialParameterInfo(Names[I]), Value)) continue;
								++HigherSamples;
								float Alpha = 0.f;
								const bool bAlpha = Part.SurfaceMat->GetScalarParameterValue(FMaterialParameterInfo(FName(Names[I].ToString()+TEXT("__A"))), Alpha);
								// SHC.w is not read by the native vertex program.
								float SampleError = I==3 ? 0.f : bAlpha ? FMath::Abs(Alpha-ExpectedHigher[I].A) : 1.f;
								for (int32 C=0; C<3; ++C) SampleError = FMath::Max(SampleError, FMath::Abs((&Value.R)[C]-(&ExpectedHigher[I].R)[C]));
								if (SampleError > HigherError) { HigherError=SampleError; HigherWitness=FString::Printf(TEXT("map=%d %s actual=%s expected=%s alpha=%d/%.6f"),Document->Map,*Names[I].ToString(),*Value.ToString(),*ExpectedHigher[I].ToString(),bAlpha,Alpha); }
							}
						}
						FLinearColor LightColor;
						float LightAlpha = 0.f;
						if (Part.SurfaceMat->GetVectorParameterValue(FMaterialParameterInfo(TEXT("_LightColor0")), LightColor)
							&& Part.SurfaceMat->GetScalarParameterValue(FMaterialParameterInfo(TEXT("_LightColor0__A")), LightAlpha))
							LightAlphaError = FMath::Max(LightAlphaError, FMath::Abs(LightColor.A - LightAlpha));
						for (int32 C = 0; C < 3; ++C)
						{
							const float S = V.shadow_color[C];
							const float Expected = S <= 0.04045f ? S / 12.92f : FMath::Pow((S + 0.055f) / 1.055f, 2.4f);
							HairShadowError = FMath::Max(HairShadowError, FMath::Abs((&Actual.R)[C] - Expected));
						}
					}
				}
				if (O.Kind == EOKSceneObjectKind::Item)
				{
					++Items;
					if (PO && PO->Session.PartCount() > 0)
					{
						++ItemsBuilt;
						bItemsUseSharedNeutralDepth &= !PO->bCharacterDisplayShadows && PO->bStudioItemShading;
						for (const auto& Target : PO->ToonShadowTargets)
							bItemsUseSharedNeutralDepth &= Target && Target->SizeX == 4 && Target->SizeY == 4;
					}
				}
			}
			Check(bStudioLighting && AuthoredHairMaterials > 0 && HairShadowError < 0.0001f,
				FString::Printf(TEXT("Studio retains native hair shadow colours independently of Gallery: %d materials, error %.7f"), AuthoredHairMaterials, HairShadowError));
			Check(AuthoredHairMaterials > 0 && LightAlphaError < 0.0001f, TEXT("shared float4 lighting updates keep shader alpha synchronized"));
			Check(ItemsBuilt > 0 && bItemsUseSharedNeutralDepth, TEXT("Studio items retain authored shading without per-item character depth targets"));
			Check(bMapAmbient && HigherSamples > 0 && HigherError < 0.000001f,
				FString::Printf(TEXT("Studio vertex ambient matches selected map or sky lighting: %d vectors, error %.9f %s"), HigherSamples, HigherError,*HigherWitness));
			Check(bImported && Chars == Report.Characters && Report.CharacterCards == Report.Characters && Built == Chars && Chars > 0,
				FString::Printf(TEXT("KStudio import: %d characters, %d OK cards, %d shown %s"), Chars, Report.CharacterCards, Built, *ImportError));
			Check(bImported && Items == Report.CatalogItems && ItemsBuilt == Items, FString::Printf(TEXT("KStudio import: %d of %d items from the catalogue, %d shown; map %d"), Report.CatalogItems, Report.Items, ItemsBuilt, Report.Map));
			for (const FString& N : Report.Notes) UE_LOG(LogTemp, Display, TEXT("OK studio import note: %s"), *N);
            // Unlightmapped Standard renderers must receive native probe diffuse,
            // with per-renderer instances retaining their immutable graph defaults.
            Document->Map=20;Document->Post.bSky=false;Document->Post.SunType=0;Document->NotifyChanged();SyncNow();
            SceneActor->PlaceBackground(FTransform::Identity,45.f);SceneActor->Tick(0.f);
            TInlineComponentArray<UDirectionalLightComponent*> MapLights(SceneActor);
            bool NativeSun=false;
            for(const auto* L:MapLights)if(L->GetName()==TEXT("MapSun"))
                NativeSun=(L->GetColoredLightBrightness()/UE_PI).Equals(FLinearColor(.964627564f,1.11674428f,.959291399f,1.11674428f),.006f);
            Check(NativeSun,TEXT("map sun retains native HDR irradiance through UE FColor quantization"));
            int32 ProbeMaterials=0,NativeCubeMaterials=0;float ProbeError=0.f;bool ProbeInstances=true,NativeCubesBound=true;

            FLinearColor ProbeA[3];TArray<FLinearColor> ProbeB;
            const bool ProbeAvailable=OKStudioMapAmbient(20,ProbeA[0],ProbeA[1],ProbeA[2],&ProbeB);
            if(auto* MapActor=SceneActor->MapPreview())
            {
                TInlineComponentArray<UMeshComponent*> Meshes(MapActor);
                const FName Keys[]={TEXT("unity_SHAr"),TEXT("unity_SHAg"),TEXT("unity_SHAb"),TEXT("unity_SHBr"),TEXT("unity_SHBg"),TEXT("unity_SHBb"),TEXT("unity_SHC")};
                for(UMeshComponent* Mesh:Meshes)for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
                {
                    auto* M=Mesh->GetMaterial(Slot);FLinearColor Value;UTexture* BoundCube=nullptr;
                    if(M&&M->GetTextureParameterValue(FMaterialParameterInfo(TEXT("unity_SpecCube0")),BoundCube))
                    {
                        FString Root;int32 Node=-1;
                        for(const FName Tag:MapActor->Tags){const FString T=Tag.ToString(),Prefix=TEXT("OKStudioMapRoot=");if(T.StartsWith(Prefix))Root=T.Mid(Prefix.Len());}
                        for(const FName Tag:Mesh->ComponentTags){const FString T=Tag.ToString(),Prefix=TEXT("OKReflectionProbe=");if(T.StartsWith(Prefix))Node=FCString::Atoi(*T.Mid(Prefix.Len()));}
                        FLinearColor HDR;UTextureCube* ExpectedCube=OKStudioMapReflection(20,HDR,Root,Node);
                        ++NativeCubeMaterials;NativeCubesBound&=BoundCube==ExpectedCube && ExpectedCube && ExpectedCube->GetSizeX()>1;
                    }
                    if(!M||!M->GetVectorParameterValue(FMaterialParameterInfo(Keys[0]),Value))continue;
                    ++ProbeMaterials;auto* MID=Cast<UMaterialInstanceDynamic>(M);ProbeInstances&=MID!=nullptr;
                    for(int32 K=0;K<7;++K)
                    {
                        const FLinearColor Expected=K<3?ProbeA[K]:ProbeB[K-3];
                        if(!M->GetVectorParameterValue(FMaterialParameterInfo(Keys[K]),Value)){ProbeError=1;continue;}
                        for(int32 C=0;C<3;++C)ProbeError=FMath::Max(ProbeError,FMath::Abs((&Value.R)[C]-(&Expected.R)[C]));
                        float Alpha=0;const FName AlphaKey(Keys[K].ToString()+TEXT("__A"));
                        if(K!=6)ProbeError=FMath::Max(ProbeError,M->GetScalarParameterValue(FMaterialParameterInfo(AlphaKey),Alpha)?FMath::Abs(Alpha-Expected.A):1.f);
                        if(MID&&MID->Parent){FLinearColor Default;ProbeInstances&=MID->Parent->GetVectorParameterValue(FMaterialParameterInfo(Keys[K]),Default)&&Default.R==0&&Default.G==0&&Default.B==0;}
                    }
                }
            }
            Check(NativeCubeMaterials>0&&NativeCubesBound,FString::Printf(TEXT("Standard map native HDR cube bound on %d renderer materials"),NativeCubeMaterials));
            Check(ProbeAvailable&&ProbeMaterials>0&&ProbeInstances&&ProbeError<.000001f,FString::Printf(TEXT("unlightmapped Standard map probe: %d renderer MIDs, immutable parents, error %.9f"),ProbeMaterials,ProbeError));
            Document->Map=-1;Document->NotifyChanged();SyncNow();SceneActor->Tick(0.f);
            int32 EmptyProbeMaterials=0,EmptyAbsentHigher=0,EmptyNonzero=0;bool EmptyProbeZero=true;
            for(const auto& Object:Document->Objects)if(auto* Actor=SceneActor->PreviewOf(Object.Id))
            {
                TInlineComponentArray<UMeshComponent*> Meshes(Actor);
                for(auto* Mesh:Meshes)for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
                {
                    auto* M=Mesh->GetMaterial(Slot);FLinearColor Value;
                    if(!M||!M->GetVectorParameterValue(FMaterialParameterInfo(TEXT("unity_SHAr")),Value))continue;
                    ++EmptyProbeMaterials;
                    const FName Keys[]={TEXT("unity_SHAr"),TEXT("unity_SHAg"),TEXT("unity_SHAb"),TEXT("unity_SHBr"),TEXT("unity_SHBg"),TEXT("unity_SHBb"),TEXT("unity_SHC")};
                    for(int32 K=0;K<UE_ARRAY_COUNT(Keys);++K)
                    {
                        const bool Found=M->GetVectorParameterValue(FMaterialParameterInfo(Keys[K]),Value);
                        if(!Found&&K>=3){++EmptyAbsentHigher;continue;} // Eyes/legacy programs do not consume vertex L2.
                        const float ExpectedAlpha=K==6?1.f:0.f;
                        float Alpha=ExpectedAlpha;
                        if(Found)M->GetScalarParameterValue(FMaterialParameterInfo(FName(Keys[K].ToString()+TEXT("__A"))),Alpha);
                        const bool Zero=Found&&Value.R==0&&Value.G==0&&Value.B==0&&Value.A==ExpectedAlpha&&Alpha==ExpectedAlpha;
                        if(!Zero&&EmptyNonzero++<8)UE_LOG(LogTemp,Warning,TEXT("OK empty probe nonzero actor=%d material=%s key=%s found=%d value=%s"),Object.Id,*M->GetPathName(),*Keys[K].ToString(),Found,*Value.ToString());
                        EmptyProbeZero&=Zero;
                    }
                }
            }
            Check(EmptyProbeMaterials>0&&EmptyProbeZero,FString::Printf(TEXT("empty native environment zeroes SH on %d character and item materials (absent unused L2=%d, nonzero=%d)"),EmptyProbeMaterials,EmptyAbsentHigher,EmptyNonzero));
			// A scene posed with Studio IK: its characters get the 9 effector targets and their hands reach them.
			const FString IkPng = Samples / TEXT("koikatu_cs0000010.png");
			FOKStudioImport::FReport IkReport;
			if (FPaths::FileExists(IkPng) && FOKStudioImport::Import(IkPng, Document, CardDir, IkReport, ImportError))
			{
				SyncNow();
				int32 Posed = 0, Reached = 0;
				float Worst = 0.f;
				for (const FOKSceneObject& O : Document->Objects)
				{
					if (O.Kind != EOKSceneObjectKind::Character || O.Character.Ik.Num() != 13) continue;
					++Posed;
					const AKKPreviewActor* PO = SceneActor->PreviewOf(O.Id);
					const FOKSceneIkTarget* Hand = O.Character.Ik.FindByPredicate([](const FOKSceneIkTarget& T) { return T.Effector == 5; });
					FVector At;
					if (PO && Hand && PO->Session.IkEffectorPosition(5, At))
					{
						const float Miss = float(FVector::Dist(At, AKKPreviewActor::ActorLocalToUnity(Hand->Position))) * 100.f;
						Worst = FMath::Max(Worst, Miss);
						if (Miss < 3.f) ++Reached;
					}
					else
					{
						const FString* ShowError = SceneActor->Errors.Find(O.Id);
						UE_LOG(LogTemp, Display, TEXT("OK studio IK import: %s not measured (preview %d, %s)"), *O.Name, PO ? 1 : 0, ShowError ? **ShowError : TEXT("no error"));
					}
				}
				Check(Posed > 0 && Reached == Posed, FString::Printf(TEXT("KStudio IK import: %d characters with 13 IK targets (9 effectors, 4 bend goals), %d left hands on target (worst %.2f cm)"), Posed, Reached, Worst));
			}
			Document->Objects = Saved->Objects; Document->NextId = Saved->NextId; Document->ActiveCamera = Saved->ActiveCamera; Document->Map = Saved->Map;
			Document->NotifyChanged();
			SyncNow();
		}
		else Lines.Add(TEXT("SKIP  KStudio import (no installed game scene)"));
	}
	Step(TEXT("scene list"));
	{
		// The save button writes an OK scene card; reopened, its characters use the card's embedded copies.
		const FString Named = SceneDirectory() / TEXT("_selftest_scene.png");
		const FString Current = Document->ToJson();
		auto WithoutCards = [](UOKSceneDocument* D) { UOKSceneDocument* C = DuplicateObject(D, GetTransientPackage()); for (FOKSceneObject& O : C->Objects) O.Character.Card.Reset(); return C->ToJson(); };
		const FString Expected = WithoutCards(Document);
		const bool bNamed = Panel && Panel->SaveNamed(TEXT("_selftest_scene"));
		Check(bNamed && SceneFiles().Contains(Named) && Panel->ListedFiles() == SceneFiles().Num(), FString::Printf(TEXT("scene list shows the saved scene (%d files)"), SceneFiles().Num()));
		Document->Objects.Reset();
		SyncNow();
		Check(bNamed && Panel->LoadFile(Named) && WithoutCards(Document) == Expected, TEXT("picking it from the list loads it"));
		FString RestoreError;
		Document->FromJson(Current, RestoreError);
		Document->NotifyChanged();
		SyncNow();
		IFileManager::Get().Delete(*Named);
		IFileManager::Get().DeleteDirectory(*(SceneDirectory() / TEXT("_selftest_scene_cards")), false, true);
	}
	Step(TEXT("stop"));
	{
		bool bStopped = false;
		OnStopped.AddLambda([&bStopped] { bStopped = true; });
		APlayerController* StopPC = SceneActor->GetWorld()->GetFirstPlayerController();
		const AActor* OldViewer = Viewer;
		RequestStop();
		Tick(0.f);
		Check(bStopped && !SceneActor && Get() == nullptr && StopPC && StopPC->GetViewTarget() != OldViewer, TEXT("leaving the studio tears it down, restores the view and tells the caller"));
	}
	Lines.Add(Failures ? FString::Printf(TEXT("RESULT FAIL (%d)"), Failures) : FString(TEXT("RESULT PASS")));
	for (const FString& L : Lines) UE_LOG(LogTemp, Display, TEXT("OK studio game selftest: %s"), *L);
	FFileHelper::SaveStringArrayToFile(Lines, *(FPaths::ProjectSavedDir() / TEXT("OKStudio/game_selftest.txt")));
	if (bExit) FPlatformMisc::RequestExit(false);
}
