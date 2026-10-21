#include "KKPreviewActor.h"
#include "KKOkAnimation.h"
#include "KKMaterialDedupe.h"
#include "UObject/StrongObjectPtr.h"
#include "ImageUtils.h"
#include "KKMaterialExposure.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "OKPhysXSecondary.h"
#include "ShaderCompiler.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Tests/KKExternalBodyFixture.h"
#include <limits>
#include "KKObjLoader.h"
#include "KKRustExtract.h"
#include "KKTextureRuntime.h"
#include "Async/ParallelFor.h"
#include "Misc/SecureHash.h"
#include "KKMainItemPlus.h"
#include "KKForgeItem.h"
#include "KKForgeAlpha.h"
#include "KKForgeOpaque.h"
#include "KKNativeSkin.h"
#include "KKTripoPBR.h"
#include "Shaders/KoikanoHairGlossUv.inl"
#include "KKAnimationCatalog.h"
#include "OKMouthPatterns.h"
#include "KKFaceBlendShape.h"
#include "KKProceduralDeform.h"
#include "KKAffineProceduralMeshComponent.h"
#include "KKPrivateGpuBounds.h"
#include "KKGpuStream.h"
#include "KKSkeletalBuilder.h"
#include "KKExactSkinning.h"
#include "Misc/ScopeExit.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "KKCharAnimInstance.h"
#include "ok_extract.h"
#include "Components/SkeletalMeshComponent.h"
#include "KKRuntimeSkinBounds.h"
#include "KKRuntimeMaterial.h"
#include "KKRuntimeSharedMaterial.h"
#include "KKRuntimeDisplayProfile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "ReferenceSkeleton.h"
#include "Animation/AnimSequence.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
#include "ProceduralMeshComponent.h"
#include "Misc/Paths.h"
#include "String/LexFromString.h"
#include "Misc/FileHelper.h"
#include "Misc/Crc.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ok_raw_texture.h"
#include "Modules/ModuleManager.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "MaterialShared.h"

void AKKPreviewActor::ReleaseSceneCaches()
{
    check(IsInGameThread());
    OKClearRuntimePngCache();
    FOkSession::ReleaseSourceCaches();
}
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionPixelNormalWS.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionMultiply.h"

#include "Materials/MaterialExpressionMin.h"
#include "Materials/MaterialExpressionPixelNormalWS.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionVertexTangentWS.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTime.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarOkFacePruneInactiveMorphs(
    TEXT("ok.Face.PruneInactiveMorphs"),1,
    TEXT("Skip copying raw morph arrays with no change from the native baseline (0 = reference path)."));
static TAutoConsoleVariable<int32> CVarOkFaceFrameBatch(TEXT("ok.Face.FrameBatch"),1,TEXT("Publish speech, pose, gaze and blink morph display once per actor tick; 0 retains independent submissions."));
static TAutoConsoleVariable<int32> CVarOkExactLeader(TEXT("ok.Animation.LeaderPoseReuse"),1,TEXT("Share compatible immutable runtime poseable meshes for opted-in single pose publishers; 0 original path"));
static TAutoConsoleVariable<int32> CVarOkFacePreparedPalette(TEXT("ok.Face.PreparedPalette"),1,TEXT("Convert immutable native palette once for all face vertices; 0 retains per-influence conversion."));
static TAutoConsoleVariable<int32> CVarOkAnimationGeneratedStreams(
    TEXT("ok.Animation.GeneratedStreams"),1,TEXT("Fuse generated preview geometry with stream packing; 0 retains the intermediate-array oracle."));
static TAutoConsoleVariable<int32> CVarOkStableShadowTarget(
    TEXT("ok.Animation.StableShadowTarget"),1,TEXT("Keep the deferred scene-depth target bound while updating its contents before the main view; 0 retains ping-pong bindings."));
static TAutoConsoleVariable<int32> CVarOkSourceRangeVertices(TEXT("ok.Animation.SourceRangeVertices"),256,TEXT("Canonical vertices per source-indexed job, bounded 64..4096; legacy conversion retains 1024."));
static TAutoConsoleVariable<int32> CVarOkSourceIndexedStreams(TEXT("ok.Animation.SourceIndexedStreams"),0,TEXT("Convert canonical sources once and pack their disjoint section draw ranges in the same job."));
static TAutoConsoleVariable<int32> CVarOkNativeDisplayStreams(TEXT("ok.Animation.NativeDisplayStreams"),0,TEXT("Pack private display streams on native skin completion workers (blocking frame-local observer)."));
static TAutoConsoleVariable<int32> CVarOkPrivateGpuStreams(TEXT("ok.Animation.PrivateGpuStreams"),1,TEXT("Runtime high-detail private-rig GPU streams for opted-in interaction actors; 0 retains CPU rendering."));
static TAutoConsoleVariable<int32> CVarOkNativeDisplayParity(TEXT("ok.Animation.NativeDisplayParity"),0,TEXT("Verify native worker display against uncached canonical draw formula."));
static TAutoConsoleVariable<int32> CVarOkRestNormalCache(TEXT("ok.Animation.RestNormalCache"),1,TEXT("Reuse exact immutable rest-normal normalization built at runtime character load."));
static TAutoConsoleVariable<int32> CVarOkZeroOffsetRotation(TEXT("ok.Animation.ZeroOffsetRotation"),1,TEXT("Skip identity rotation of zero-offset base surfaces; retain source-position addition."));
static TAutoConsoleVariable<int32> CVarOkSourceIndexedParity(TEXT("ok.Animation.SourceIndexedParity"),0,TEXT("Compare completed source-indexed draw vertices with legacy canonical generation."));
static TAutoConsoleVariable<int32> CVarOkFusedSourceStreams(TEXT("ok.Animation.FusedSourceStreams"),0,TEXT("Combine independent native source conversion and generated stream packing."));
static TAutoConsoleVariable<int32> CVarOkFusedSourceParity(TEXT("ok.Animation.FusedSourceParity"),0,TEXT("Compare direct native draw values against canonical draws."));
static TAutoConsoleVariable<int32> CVarOkNormalMotionCache(
    TEXT("ok.Animation.NormalMotionCache"),1,TEXT("Compute outline normal rotation once per native source vertex in the existing conversion jobs; 0 retains per-draw evaluation."));
static TAutoConsoleVariable<int32> CVarOkNormalMotionParity(
    TEXT("ok.Animation.NormalMotionParity"),0,TEXT("Verify each reused outline quaternion against the original per-draw formula (diagnostic only)."));
static TAutoConsoleVariable<int32> CVarOkAnimationStreamMinParallel(
    TEXT("ok.Animation.StreamParallelMinVertices"),16384,
    TEXT("Minimum procedural draw vertices for parallel deformation stream fill. Smaller streams avoid task dispatch and waits."),ECVF_Default);
static TAutoConsoleVariable<int32> CVarOkAnimationSourceConversion(
    TEXT("ok.Animation.SourceConversion"),1,TEXT("Native source conversion: 0 serial oracle, 1 adaptive, 2 force parallel."));

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "UnrealClient.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"
#include "SkeletalRenderPublic.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/HUD.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerStart.h"

// The automated three-view capture must not inherit live orbit input.
static bool GOkShotCaptureActive = false;

// Opt-in wall-time breakdown for a single game-thread card load.
struct FOkCardLoadProfile
{
	bool bActive = false;
	double TextureSeconds = 0.0;
	double TextureDecodeSeconds = 0.0;
	double MaterialSeconds = 0.0;
	double MaterialFinalizeSeconds = 0.0;
	double SkeletonSeconds = 0.0;
	int32 TextureCount = 0;
	int32 MaterialCount = 0;
};
static FOkCardLoadProfile GOkCardLoadProfile;

struct FOkCardProfileScope
{
	double* Total;
	double Start;
	explicit FOkCardProfileScope(double* InTotal)
		: Total(InTotal), Start(InTotal ? FPlatformTime::Seconds() : 0.0) {}
	~FOkCardProfileScope() { if (Total) *Total += FPlatformTime::Seconds() - Start; }
};

static FVector OkToUe(float X, float Y, float Z)
{
	return FVector(-X * 100.0f, Z * 100.0f, Y * 100.0f);
}

// The Unity->UE basis change is a reflection, so direction vectors use the same
// swizzle as positions (the reflection is orthogonal: M^-T == M).
static FVector OkToUeDir(float X, float Y, float Z)
{
	return FVector(-X, Z, Y).GetSafeNormal();
}

static bool OKSessionMouthPairNames(const FOkSession& Session, int32 PartIndex,
	const FString& MeshName, int32 Pattern, FString& Close, FString& Open)
{
	// Mouth pairs are blend shapes. The game-table fallback matches mesh names
	// by substring, so a renderer without any blend shape whose name merely
	// contains "face"/"tang"/"tooth" (an accessory's "polySurface95") must not
	// become a mouth slot: its full-vertex mouth refresh would overwrite the
	// compacted animated sections and leave that hair frozen at its load pose.
	if (Session.MorphCount(PartIndex) <= 0)
	{
		return false;
	}
	int32 CloseIndex = 0;
	int32 OpenIndex = 0;
	if (Session.MouthPair(PartIndex, Pattern, CloseIndex, OpenIndex))
	{
		OkMorphView CloseMorph;
		OkMorphView OpenMorph;
		FMemory::Memzero(CloseMorph);
		FMemory::Memzero(OpenMorph);
		if (!Session.MorphAt(PartIndex, CloseIndex, CloseMorph)
			|| !Session.MorphAt(PartIndex, OpenIndex, OpenMorph)
			|| !CloseMorph.name || !OpenMorph.name)
		{
			return false;
		}
		Close = UTF8_TO_TCHAR(CloseMorph.name);
		Open = UTF8_TO_TCHAR(OpenMorph.name);
		return true;
	}
	return OKGameMouthPairNames(MeshName, Pattern, Close, Open);
}

static int32 OKSessionMouthPatternCount(const FOkSession& Session, int32 PartIndex,
	const FString& MeshName, const TMap<FString, FName>& AvailableMorphs)
{
	const int32 SourceCount = Session.MouthPatternCount(PartIndex);
	if (SourceCount <= 0 || SourceCount >= OKMouthPatternCount)
	{
		return SourceCount > 0 ? SourceCount : OKMouthPatternCount;
	}
	// Installed runtime plugins may append FBSTarget rows after deserializing
	// the head prefab. The reference table comes from the live game dump; expose
	// its suffix only when every target channel exists on this actual mesh.
	for (int32 Pattern = SourceCount; Pattern < OKMouthPatternCount; ++Pattern)
	{
		FString Close;
		FString Open;
		if (!OKGameMouthPairNames(MeshName, Pattern, Close, Open)
			|| !AvailableMorphs.Contains(Close.ToLower())
			|| !AvailableMorphs.Contains(Open.ToLower()))
		{
			return SourceCount;
		}
	}
	return OKMouthPatternCount;
}

// Port of StudioEditor KoikatsuFaceNormalProxyBuilder / FaceSphereNormalProxy.
// cf_O_face contains inner-mouth and eye-socket triangles whose vertex normals
// pollute the outer surface, so geometric/imported normals shade as mottled
// patches. Replacing the face normals with a radial "sphere" normal from an
// estimated centre makes the face read as one smooth anime surface.
// Works in UE space (X width, Y depth, Z up).
static void ApplyFaceSphereNormals(TArray<FVector>& Verts, TArray<FVector>& Normals)
{
	if (Verts.Num() == 0 || Normals.Num() != Verts.Num())
	{
		return;
	}
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NO_FACE_SPHERE")) == TEXT("1"))
	{
		return;
	}
	FVector Min(FLT_MAX, FLT_MAX, FLT_MAX);
	FVector Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (const FVector& V : Verts)
	{
		Min = Min.ComponentMin(V);
		Max = Max.ComponentMax(V);
	}
	FVector Center = (Min + Max) * 0.5f;
	const FVector Extents = (Max - Min) * 0.5f;

	FVector NormalSum = FVector::ZeroVector;
	int32 NormalCount = 0;
	for (const FVector& N : Normals)
	{
		if (!N.IsNearlyZero())
		{
			NormalSum += N.GetSafeNormal();
			++NormalCount;
		}
	}
	const float Coherence = NormalCount > 0 ? NormalSum.Size() / static_cast<float>(NormalCount) : 0.f;
	const float HorizontalRadius = FMath::Max(Extents.X, Extents.Z * 0.65f);
	if (Coherence > 0.2f)
	{
		const FVector Outward = NormalSum.GetSafeNormal();
		const float DepthOffset = FMath::Max(HorizontalRadius * 0.72f, Extents.Y * 0.25f);
		Center -= Outward * DepthOffset;
	}
	const FVector Up(0.f, 0.f, 1.f);
	constexpr float Blend = 0.85f;
	for (int32 i = 0; i < Normals.Num(); ++i)
	{
		FVector Delta = Verts[i] - Center;
		const float Height = FVector::DotProduct(Delta, Up);
		Delta -= Up * FMath::Min(Height, 0.f); // lower half treats the head as a cylinder
		const FVector SphereN = Delta.GetSafeNormal();
		if (!SphereN.IsNearlyZero())
		{
			const FVector Base = Normals[i].IsNearlyZero() ? SphereN : Normals[i].GetSafeNormal();
			Normals[i] = FMath::Lerp(Base, SphereN, Blend).GetSafeNormal();
		}
	}
}

static UTexture2D* TextureFromPngBuffer(const TArray<uint8>& Png, bool bClamp = false, const OkTextureSettings* Settings = nullptr, bool bReadonlyGPU = false)
{
	FOkCardProfileScope Profile(GOkCardLoadProfile.bActive ? &GOkCardLoadProfile.TextureSeconds : nullptr);
	if (Png.Num() < 16)
	{
		return nullptr;
	}
 bool bHit=false;double DecodeSeconds=0.;
 UTexture2D* Texture=bReadonlyGPU ? OKAcquireReadonlyRuntimeTextureFromPng(Png,Settings,bClamp,bHit,DecodeSeconds) : OKCreateRuntimeTextureFromPng(Png,Settings,bClamp,bHit,DecodeSeconds);
 if(GOkCardLoadProfile.bActive){GOkCardLoadProfile.TextureDecodeSeconds+=DecodeSeconds;if(Texture&&!bHit)++GOkCardLoadProfile.TextureCount;}
 return Texture;
}

static bool FillOverlayVertexColors(const uint8* Png, int32 Len, const float* Uv4, int32 N, const float Color[4], TArray<FColor>& Out)
{
	if (!Png || Len < 16 || !Uv4 || N <= 0)
	{
		return false;
	}
	FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TArray<uint8> Raw;
	int32 W = 0, H = 0;
	if (!OKDecodeSessionTexture(Png, Len, Raw, W, H))
	{
		return false;
	}
	if (W <= 1 || H <= 1 || Raw.Num() < W * H * 4)
	{
		return false;
	}
	auto Px = [&](int32 X, int32 Y) -> FLinearColor
	{
		const int32 O = (Y * W + X) * 4;
		return FLinearColor(Raw[O + 2] / 255.f, Raw[O + 1] / 255.f, Raw[O] / 255.f, Raw[O + 3] / 255.f);
	};
	Out.SetNum(N);
	int32 Hit = 0;
	for (int32 I = 0; I < N; ++I)
	{
		const float U = FMath::Clamp(Uv4[I * 2], 0.f, 1.f);
		const float V = FMath::Clamp(Uv4[I * 2 + 1], 0.f, 1.f);
		const float X = U * (W - 1);
		const float Y = (1.f - V) * (H - 1);
		const int32 X0 = FMath::Clamp(FMath::FloorToInt(X), 0, W - 1);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt(Y), 0, H - 1);
		const int32 X1 = FMath::Min(X0 + 1, W - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, H - 1);
		const float Fx = X - X0;
		const float Fy = Y - Y0;
		const FLinearColor A = FMath::Lerp(Px(X0, Y0), Px(X1, Y0), Fx);
		const FLinearColor B = FMath::Lerp(Px(X0, Y1), Px(X1, Y1), Fx);
		const FLinearColor S = FMath::Lerp(A, B, Fy);
		const float R = FMath::Clamp(S.R * Color[0], 0.f, 1.f);
		const float G = FMath::Clamp(S.G * Color[1], 0.f, 1.f);
		const float Bl = FMath::Clamp(S.B * Color[2], 0.f, 1.f);
		const float Al = FMath::Clamp(S.A * Color[3], 0.f, 1.f);
		Out[I] = FColor(FMath::RoundToInt(R * 255.f), FMath::RoundToInt(G * 255.f), FMath::RoundToInt(Bl * 255.f), FMath::RoundToInt(Al * 255.f));
		if (Al > 0.02f)
		{
			++Hit;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("OK overlay vc n=%d hit=%d color=(%.3f,%.3f,%.3f,%.3f)"), N, Hit, Color[0], Color[1], Color[2], Color[3]);
	return Hit > 0;
}

static UTexture2D* TextureFromPngBytes(const uint8* Data, int32 Len, bool bClamp = false, const OkTextureSettings* Settings = nullptr, bool bReadonlyGPU = false)
{
	if (!Data || Len < 16)
	{
		return nullptr;
	}
	TArray<uint8> Png;
	Png.Append(Data, Len);
	return TextureFromPngBuffer(Png, bClamp, Settings, bReadonlyGPU);
}

static UTexture2D* TextureFromPngFile(const FString& Path, bool bClamp = false)
{
	TArray<uint8> Png;
	if (!FFileHelper::LoadFileToArray(Png, *Path) || Png.Num() < 16)
	{
		return nullptr;
	}
	return TextureFromPngBuffer(Png, bClamp);
}

static UTexture2D* LinearTextureFromPngBytes(const uint8* Data,int32 Length)
{
	// Configure the final sampler before its first upload. Changing SRGB after
	// UpdateResource releases and rebuilds the same texture unnecessarily.
	const OkTextureSettings Linear{1,0,0,0,0};
	return TextureFromPngBytes(Data,Length,false,&Linear,true);
}

static UTexture2D* SolidColorTexture(FColor Color)
{
	UTexture2D* Tex = UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8);
	if (!Tex || !Tex->GetPlatformData() || Tex->GetPlatformData()->Mips.Num() == 0)
	{
		return nullptr;
	}
	void* Mip = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FColor* Pixels = static_cast<FColor*>(Mip);
	for (int32 i = 0; i < 16; ++i)
	{
		Pixels[i] = Color;
	}
	Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
	Tex->SRGB = true;
	Tex->Filter = TF_Bilinear;
	Tex->NeverStream = true;
	Tex->UpdateResource();
	return Tex;
}

struct FPngPixels
{
	int32 W = 0;
	int32 H = 0;
	TArray<FColor> Pixels;
};

static bool LoadPngPixels(const FString& Path, FPngPixels& Out)
{
	TArray<uint8> Png;
	if (!FFileHelper::LoadFileToArray(Png, *Path) || Png.Num() < 16)
	{
		return false;
	}
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Png.GetData(), Png.Num()))
	{
		return false;
	}
	TArray<uint8> Raw;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		return false;
	}
	Out.W = Wrapper->GetWidth();
	Out.H = Wrapper->GetHeight();
	if (Out.W <= 0 || Out.H <= 0)
	{
		return false;
	}
	Out.Pixels.SetNum(Out.W * Out.H);
	for (int32 i = 0; i < Out.Pixels.Num(); ++i)
	{
		const int32 o = i * 4;
		Out.Pixels[i] = FColor(Raw[o + 2], Raw[o + 1], Raw[o + 0], Raw[o + 3]);
	}
	return true;
}

static FColor SamplePng(const FPngPixels& Img, const FVector2D& UV)
{
	if (Img.W <= 0 || Img.H <= 0 || Img.Pixels.Num() == 0)
	{
		return FColor::White;
	}
	float U = FMath::Frac(UV.X);
	float V = FMath::Frac(UV.Y);
	if (U < 0.f)
	{
		U += 1.f;
	}
	if (V < 0.f)
	{
		V += 1.f;
	}
	const int32 X = FMath::Clamp(FMath::FloorToInt(U * (Img.W - 1)), 0, Img.W - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt(V * (Img.H - 1)), 0, Img.H - 1);
	return Img.Pixels[Y * Img.W + X];
}

static void PaintVerticesFromPng(const FPngPixels& Img, const TArray<FVector2D>& UVs, TArray<FColor>& Colors)
{
	if (Img.Pixels.Num() == 0)
	{
		return;
	}
	const int32 N = FMath::Max(UVs.Num(), Colors.Num());
	if (Colors.Num() < N)
	{
		Colors.SetNum(N);
	}
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D UV = UVs.IsValidIndex(i) ? UVs[i] : FVector2D::ZeroVector;
		FColor C = SamplePng(Img, UV);
		if (C.A < 8)
		{
			C = FColor(C.R, C.G, C.B, 255);
		}
		Colors[i] = C;
	}
}

// Card parts drawn as instances of the shared parents (KKRuntimeSharedMaterial: one cooked or DDC-cached parent per
// Koikatsu shader family under /Game/IndependentResources/RuntimeMaterials), so a new card compiles no shader. The
// editor's transient graph per part bakes the card's values into its HLSL and compiled anew for every card; it stays
// only for the parts no shared parent covers. Always in a packaged game; in the editor and -game unless
// OK_SHARED_PARENTS=0.
static bool OKSharedParents()
{
#if WITH_EDITOR
	static const bool bOn = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHARED_PARENTS")) != TEXT("0");
	return bOn;
#else
	return true;
#endif
}

static UMaterial* MakeSolidMaterial(FLinearColor Color)
{
#if WITH_EDITOR

	UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!Mat)
	{
		return nullptr;
	}
	Mat->MaterialDomain = MD_Surface;
	Mat->BlendMode = BLEND_Opaque;
	Mat->SetShadingModel(MSM_Unlit);
	Mat->TwoSided = true;
	Mat->SetUsageByFlag(MATUSAGE_StaticMesh, true);
	Mat->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
	UMaterialExpressionConstant3Vector* C = NewObject<UMaterialExpressionConstant3Vector>(Mat);
	C->Material = Mat;
	C->Constant = Color;
	Mat->GetExpressionCollection().AddExpression(C);
	if (UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData())
	{
		Ed->EmissiveColor.Expression = C;
	}
	Mat->UpdateCachedExpressionData();
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	Mat->ForceRecompileForRendering();
	OKEnsureSkeletalMaterialUsage(Mat);
	return Mat;

#else
return LoadObject<UMaterial>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
#endif
}

static void HideComputeSkel(UPoseableMeshComponent* Follow)
{
	if (!Follow)
	{
		return;
	}
	Follow->SetCastShadow(false);
	Follow->SetCastHiddenShadow(false);
	Follow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Follow->bHiddenInGame = true;
	Follow->SetHiddenInGame(true, true);
	Follow->SetVisibility(false, true);
	Follow->SetRenderInMainPass(false);
	Follow->SetRenderInDepthPass(false);
#if WITH_EDITOR
	static UMaterial* Hidden = nullptr;
	if (!Hidden)
	{
		Hidden = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		Hidden->MaterialDomain = MD_Surface;
		Hidden->BlendMode = BLEND_Translucent;
		Hidden->SetShadingModel(MSM_Unlit);
		Hidden->bDisableDepthTest = true;
		Hidden->TwoSided = true;
		// -game cannot auto-enable usage flags through SetMaterialUsage.
		// Declare the vertex factory before requesting or compiling shaders.
		OKEnsureSkeletalMaterialUsage(Hidden);
		Hidden->SetMaterialUsage(MATUSAGE_SkeletalMesh);
		UMaterialExpressionConstant* Z = NewObject<UMaterialExpressionConstant>(Hidden);
		Z->Material = Hidden;
		Z->R = 0.f;
		Hidden->GetExpressionCollection().AddExpression(Z);
		if (UMaterialEditorOnlyData* Ed = Hidden->GetEditorOnlyData())
		{
			Ed->EmissiveColor.Expression = Z;
			Ed->Opacity.Expression = Z;
		}
		Hidden->UpdateCachedExpressionData();
		Hidden->PreEditChange(nullptr);
		Hidden->PostEditChange();
		Hidden->ForceRecompileForRendering();
		OKEnsureSkeletalMaterialUsage(Hidden);
		Hidden->AddToRoot();
	}
	OKEnsureSkeletalMaterialUsage(Hidden);
	Follow->SetMaterial(0, Hidden);
#endif
}

struct FOKToonShadowParameters
{
	UTextureRenderTarget2D* Depth = nullptr;
	FVector Center;
	FVector LightDir;
	FVector Right;
	FVector Up;
	float Width = 260.f;
	float CameraDistance = 500.f;
	UMaterial* TripoPBR = nullptr;
	float AmbientDepth = 0.26f;
	// GPU _ambientshadowG. Alpha is GraphicData.shadowDepth in the character maker.
	float AmbientAlpha = 0.26f;
	FLinearColor AmbientRgb = FLinearColor(128.f/255.f, 128.f/255.f, 128.f/255.f, 1.f);
    bool bEnableShadows=true;
	float LineWidth = 0.5f;
	float LineDepth = 1.f;
	float LineTexOn = 1.f;
	float SkinLineWidth = 1.f;
	bool bKoikanoSkinOutline = false;
	float NormalDetailScale = 0.f;
	float FaceNormal = 0.f;
	float FaceShadow = 0.f;
	float KanoVerShift = 0.f;
	float ShadowExtendAnother = 0.f;
	FLinearColor MainTexST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	// Gag eyes (Koikano/sub_texture_animation): the sprite sheet's _TileAnimation (columns, rows, speed) and
	// (_SizeSpeed, _SizeWidth, _AngleSpeed) from the eye set (ChaControl.ChangeGagEyesMaterial).
	bool bGagAnimation = false;
	FLinearColor GagTileAnimation = FLinearColor(1.f, 1.f, 1.f, 0.f);
	FLinearColor GagMotion = FLinearColor(0.f, 0.f, 0.f, 0.f);
	FLinearColor AlphaMaskST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	FLinearColor NormalMapST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	FLinearColor NormalDetailST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	UTexture2D* NormalDetailTex = nullptr;
	FLinearColor NormalMaskST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	FLinearColor DetailMaskST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	FLinearColor LineMaskST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	FLinearColor HairGlossST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	bool bHairFrontProgram = false;
	// Shader Forge/main_item_studio_add: its additive light effects mix the item colours by the texture's channel masks.
	bool bStudioAddProgram = false;
	FLinearColor RampGST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	FLinearColor AnotherRampST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	UTexture2D* AnotherRampTex = nullptr;
	UTexture2D* EmissionTex = nullptr;
	FLinearColor EmissionColor = FLinearColor::Black;
	// Baked lightmap (RGBM, linear sampling) of a map part, on TEXCOORD1; Lightmaps keeps each index's texture for the
	// later parts of the same load (only the first part using a lightmap carries it).
	UTexture2D* LightmapTex = nullptr;
	TMap<int32, UTexture2D*> Lightmaps;
	UTexture2D* LightmapDirectionTex = nullptr;
	TMap<int32, UTexture2D*> LightmapDirections;
	// What the lightmap lights: Unity Standard's diffuse colour is albedo x oneMinusReflectivity (gamma space
	// 0.779 x (1 - metallic), or 1 - max(_SpecColor) for the specular setup); 1 for other shaders. With a
	// _MetallicGlossMap (keyword _METALLICGLOSSMAP) its red channel is the metallic, on the _MainTex UVs.
	float LightmapDiffuse = 1.f;
	bool bStandardLinear = false;
    bool bNativeReflection = false;
	UTexture2D* LightmapMetallicTex = nullptr;
	// Unity Standard's indirect specular from the baked reflection probe the part sits in (okmodel_view::
	// assign_reflection_probes): the probe's cubemap atlas (faces side by side, mips below; display values), its mips,
	// intensity, and the material's metallic (unless LightmapMetallicTex), smoothness (_Glossiness, or the metallic
	// map's alpha x _GlossMapScale) and whether it is Unity Standard at all. Reflections keeps each index's atlas.
	UTexture2D* ReflectionTex = nullptr;
	TMap<int32, UTexture2D*> Reflections;
	float ReflectionMips = 1.f;
	float ReflectionIntensity = 1.f;
	float StandardMetallic = 0.f;
	float StandardSmoothness = 0.5f;
	float StandardNormalScale = 1.f;
	bool bStandardSmoothnessFromMap = false;
	FLinearColor Texcoord2ST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	FLinearColor Texcoord3ST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	FLinearColor Texcoord4ST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	bool bFaceForgeOverlay=false;
	UTexture2D* FaceOvertex3 = nullptr;
	FLinearColor FaceOvercolor3 = FLinearColor::Black;
	FLinearColor OverColor2 = FLinearColor(0.f, 0.f, 0.f, 0.f);
	float NipSize = 0.5f;
	float NipSpecular = 0.5f;
	float NipToggle = 0.f;
	float Tex1Mask = 0.f;
	float SpecHeight = 0.95f;
	float SpecularPower = 0.f;
	float SpecularPowerNail = 0.f;
	FLinearColor SpecularColor = FLinearColor(1.f, 1.f, 1.f, 0.f);
	float NoTexSpecular = 0.f;
	float Metallic = 0.f;
	float Smoothness = 0.5f;
	bool bItemDraw = false;
	bool bHairSun = false;
	bool bHairOld = false;
	bool bHairReverseColors = false;
	bool bHairColor2 = true;
	bool bHairColor3 = true;
	bool bItemFourthColor = false;
	// Studio item alpha (ItemComponent rendAlpha parts): the translucent opacity is scaled by it.
	float ItemAlpha = 1.f;
	FLinearColor ItemColors[4] = {FLinearColor::White, FLinearColor::White, FLinearColor::White, FLinearColor::White};
	UTexture2D* ColorMaskTex = nullptr;
	FLinearColor ColorMaskST = FLinearColor(1.f, 1.f, 0.f, 0.f);
	// Parameter names this part's material overrides name (AKKPreviewActor::MaterialOverrideNames), and their
	// source material values (Unity-authored, colours still sRGB). See KKMaterialExposure.h.
	TSet<FString> Exposed;
	TMap<FString, FLinearColor> ExposedSource;
	int32 EyeDrawKind = 0;
	UTexture2D* EyeTextures[2] = {nullptr, nullptr};
	// Eye set pupil expression (_expression), sampled on TEXCOORD3 and drawn over the pupil by its alpha.
	UTexture2D* EyeExpressionTex = nullptr;
	FLinearColor EyeColors[2] = {FLinearColor::White, FLinearColor::White};
	FLinearColor EyeColor = FLinearColor::White;
	float EyeRotations[3] = {0.f, 0.5f, 0.5f};
	float EyeHighlight = 1.f;
	const FOKNativeItemDraw* NativeItem = nullptr;
	// A part outlined by its own graph: its runtime material description (OKDescribeRuntimeMaterialPart) and the
	// scene's shader globals, for the installed outline vertex program (OKMakeRuntimeOutlineVertexOffset).
	TSharedPtr<const FKKRuntimeMaterialPart> OutlinePart;
	TMap<FName, FLinearColor> OutlineGlobals;
	UMaterial* NativeSkin = nullptr;
    UMaterialInterface* CookedMaterial=nullptr;
    UMaterialInterface* CookedOutline=nullptr;
	// Studio's layer-10 Directional Chara: native _WorldSpaceLightPos0
	// (0, 0.6427876, 0.7660446) in Unity -> (0, 0.7660446, 0.6427876) in UE.
	// Keep this as scene state so another lighting setup can replace it.
	FVector MainLightToSurface = FVector(0.f, 0.7660446f, 0.6427876f);
	float HairRimStrength = 0.75f;
	float HairRimPower = 0.2f;
};

// Unity's _ST applies scale/offset in bottom-left UV space.  Extracted PNGs
// are top-left for UE, so V' = scaleV * V + 1 - scaleV - offsetV.
// Keep this in material nodes: a Custom expression feeding TextureSample
// produced black masks even for identity transforms in the preview.
static UMaterialExpression* MakeUnityMaskUV(UMaterial* Mat, const FLinearColor& ST)
{
#if WITH_EDITOR

	if (ST.Equals(FLinearColor(1.f, 1.f, 0.f, 0.f))) { return nullptr; }
	UMaterialExpressionTextureCoordinate* UV = NewObject<UMaterialExpressionTextureCoordinate>(Mat);
	UV->Material = Mat;
	UV->UTiling = ST.R;
	UV->VTiling = ST.G;
	Mat->GetExpressionCollection().AddExpression(UV);
	UMaterialExpressionConstant2Vector* Offset = NewObject<UMaterialExpressionConstant2Vector>(Mat);
	Offset->Material = Mat;
	Offset->R = ST.B;
	Offset->G = 1.f - ST.G - ST.A;
	Mat->GetExpressionCollection().AddExpression(Offset);
	UMaterialExpressionAdd* MappedUV = NewObject<UMaterialExpressionAdd>(Mat);
	MappedUV->Material = Mat;
	MappedUV->A.Expression = UV;
	MappedUV->B.Expression = Offset;
	Mat->GetExpressionCollection().AddExpression(MappedUV);
	return MappedUV;

#else
return nullptr; // Editor graph creation is not a runtime rendering path.
#endif
}

// The gag eye's moving texture coordinate, the pixel program of Koikano/sub_texture_animation (KKS chara/mm_base,
// cf_m_gageye_*): the coordinate pulses in size around the centre (sin(_SizeSpeed·t·10), _SizeWidth), turns
// (_AngleSpeed turns per second) and picks the sprite sheet frame (_TileAnimation columns × rows, frame
// _TileAnimation.z·t·9, rows from the top). `Base` is the UE coordinate after _MainTex_ST (null: TEXCOORD0); Unity's
// v runs the other way, so it is flipped in and out.
static UMaterialExpression* MakeGagEyeUV(UMaterial* Mat, UMaterialExpression* Base, const FLinearColor& Tile, const FLinearColor& Motion)
{
#if WITH_EDITOR
	auto Add = [Mat](UMaterialExpression* E) { E->Material=Mat; Mat->GetExpressionCollection().AddExpression(E); };
	if (!Base) { auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(Mat); UV->CoordinateIndex=0; Add(UV); Base=UV; }
	auto* Time=NewObject<UMaterialExpressionTime>(Mat); Add(Time);
	auto* TileConst=NewObject<UMaterialExpressionConstant4Vector>(Mat); TileConst->Constant=Tile; Add(TileConst);
	auto* MotionConst=NewObject<UMaterialExpressionConstant4Vector>(Mat); MotionConst->Constant=Motion; Add(MotionConst);
	auto* Code=NewObject<UMaterialExpressionCustom>(Mat);
	Code->OutputType=CMOT_Float2;
	Code->Code=TEXT(
		"float2 uv=float2(UV.x,1.0-UV.y);"
		"float s=sin(Motion.x*T*10.0);float k=2.0*(s+1.0)*Motion.y+1.0;"
		"float2 c=uv*k-0.5*k;float a=Motion.z*T*6.283185;float sn=sin(a),cs=cos(a);"
		"float2 r=float2(dot(c,float2(cs,sn)),dot(c,float2(-sn,cs)))+0.5;"
		"float2 n=max(floor(Tile.xy),1.0);float total=n.x*n.y;"
		"float f=Tile.z*T*9.0/total;f=f>=-f?frac(abs(f)):-frac(abs(f));"
		"float idx=round(total*f);if(idx<0.0)idx+=total;"
		"float g=idx/n.x;g=g>=-g?frac(abs(g)):-frac(abs(g));float col=round(n.x*g);"
		"float h=(idx-col)/n.x/n.y;h=h>=-h?frac(abs(h)):-frac(abs(h));float row=n.y-1.0-round(n.y*h);"
		"float2 o=r/n+float2(col,row)/n;"
		"return float2(o.x,1.0-o.y);");
	Code->Inputs.SetNum(4);
	Code->Inputs[0].InputName=TEXT("UV"); Code->Inputs[0].Input.Expression=Base;
	Code->Inputs[1].InputName=TEXT("T"); Code->Inputs[1].Input.Expression=Time;
	Code->Inputs[2].InputName=TEXT("Tile"); Code->Inputs[2].Input.Expression=TileConst;
	Code->Inputs[3].InputName=TEXT("Motion"); Code->Inputs[3].Input.Expression=MotionConst;
	Add(Code);
	return Code;
#else
	return nullptr; // Editor graph creation is not a runtime rendering path.
#endif
}

static UMaterialExpression* MakeRuntimeEyeUV(UMaterial* Mat, int32 Channel, const FLinearColor& ST = FLinearColor(1,1,0,0))
{
#if WITH_EDITOR

	auto Add = [Mat](UMaterialExpression* E) { E->Material=Mat; Mat->GetExpressionCollection().AddExpression(E); };
	auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(Mat); UV->CoordinateIndex=Channel; Add(UV);
	auto* One=NewObject<UMaterialExpressionConstant>(Mat); One->R=1.f; Add(One);
	auto* Homogeneous=NewObject<UMaterialExpressionAppendVector>(Mat); Homogeneous->A.Expression=UV; Homogeneous->B.Expression=One; Add(Homogeneous);
	UMaterialExpressionDotProduct* Dots[2];
	for(int32 Axis=0;Axis<2;++Axis){
		auto* Row=NewObject<UMaterialExpressionVectorParameter>(Mat);
		Row->ParameterName=*FString::Printf(TEXT("KKEyeUV%dRow%d"),Channel,Axis);
		Row->DefaultValue=Axis==0?FLinearColor(1,0,0,0):FLinearColor(0,1,0,0); Add(Row);
		Dots[Axis]=NewObject<UMaterialExpressionDotProduct>(Mat); Dots[Axis]->A.Expression=Homogeneous; Dots[Axis]->B.Expression=Row; Add(Dots[Axis]);
	}
	auto* Result=NewObject<UMaterialExpressionAppendVector>(Mat); Result->A.Expression=Dots[0]; Result->B.Expression=Dots[1]; Add(Result);
	auto* Scale=NewObject<UMaterialExpressionConstant2Vector>(Mat); Scale->R=ST.R; Scale->G=ST.G; Add(Scale);
	auto* Multiply=NewObject<UMaterialExpressionMultiply>(Mat); Multiply->A.Expression=Result; Multiply->B.Expression=Scale; Add(Multiply);
	auto* Offset=NewObject<UMaterialExpressionConstant2Vector>(Mat); Offset->R=ST.B; Offset->G=1.f-ST.G-ST.A; Add(Offset);
	auto* Mapped=NewObject<UMaterialExpressionAdd>(Mat); Mapped->A.Expression=Multiply; Mapped->B.Expression=Offset; Add(Mapped);
	return Mapped;

#else
return nullptr; // Editor graph creation is not a runtime rendering path.
#endif
}

static bool FitEyeUVRows(const TArray<FVector2D>& Base, const TArray<FVector2D>& Target, FLinearColor Rows[2])
{
	if(Base.Num()!=Target.Num()) return false;
	if(Base.IsEmpty()){Rows[0]=FLinearColor(1,0,0,0);Rows[1]=FLinearColor(0,1,0,0);return true;}
	int32 B=0,C=0; double Length=0,Area=0;
	for(int32 I=1;I<Base.Num();++I){const double D=(Base[I]-Base[0]).SizeSquared();if(D>Length){Length=D;B=I;}}
	const FVector2D U=Base[B]-Base[0];
	for(int32 I=1;I<Base.Num();++I){const auto V=Base[I]-Base[0];const double D=FMath::Abs(U.X*V.Y-U.Y*V.X);if(D>Area){Area=D;C=I;}}
	if(Area<1.e-12){
		if(Length>1.e-20){
			const auto Q=Target[B]-Target[0];
			for(int32 Axis=0;Axis<2;++Axis){const double A=Q[Axis]*U.X/Length,D=Q[Axis]*U.Y/Length;
				Rows[Axis]=FLinearColor(A,D,Target[0][Axis]-A*Base[0].X-D*Base[0].Y,0);}
		}else{
			Rows[0]=FLinearColor(0,0,Target[0].X,0); Rows[1]=FLinearColor(0,0,Target[0].Y,0);
		}
		for(int32 I=0;I<Base.Num();++I)for(int32 Axis=0;Axis<2;++Axis)
			if(FMath::Abs(Rows[Axis].R*Base[I].X+Rows[Axis].G*Base[I].Y+Rows[Axis].B-Target[I][Axis])>1.e-5) return false;
		return true;
	}
	const FVector2D V=Base[C]-Base[0],Q=Target[B]-Target[0],R=Target[C]-Target[0]; const double Det=U.X*V.Y-U.Y*V.X;
	for(int32 Axis=0;Axis<2;++Axis){const double A=(Q[Axis]*V.Y-R[Axis]*U.Y)/Det,D=(U.X*R[Axis]-V.X*Q[Axis])/Det;
		Rows[Axis]=FLinearColor(A,D,Target[0][Axis]-A*Base[0].X-D*Base[0].Y,0);}
	for(int32 I=0;I<Base.Num();++I)for(int32 Axis=0;Axis<2;++Axis)
		if(FMath::Abs(Rows[Axis].R*Base[I].X+Rows[Axis].G*Base[I].Y+Rows[Axis].B-Target[I][Axis])>1.e-5) return false;
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKEyeAffineUVTest,"OpenKoikatsu.Eye.GpuAffineUV",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKEyeAffineUVTest::RunTest(const FString& Parameters)
{
	FLinearColor Rows[2];
	TArray<FVector2D> Base{{0,0},{1,0},{0,1},{.3,.7}},Target{{.7,-.2},{.95,1.8},{-.8,.3},{-.275,.75}};
	TestTrue(TEXT("Independent rotation/shear/offset values"),FitEyeUVRows(Base,Target,Rows));
	TestTrue(TEXT("Main row"),Rows[0].Equals(FLinearColor(.25,-1.5,.7,0),1.e-6));
	TestTrue(TEXT("Second row"),Rows[1].Equals(FLinearColor(2,.5,-.2,0),1.e-6));
	Base={{0,0},{.5,0},{1,0}};Target={{2,3},{3,2.5},{4,2}};
	TestTrue(TEXT("One collapsed UV axis still supports gaze"),FitEyeUVRows(Base,Target,Rows));
	TestTrue(TEXT("Line row"),Rows[0].Equals(FLinearColor(2,0,2,0),1.e-6));
	Base={{0,0},{0,0},{0,0}};Target={{.2,.7},{.2,.7},{.2,.7}};
	TestTrue(TEXT("Constant coordinate remains supported"),FitEyeUVRows(Base,Target,Rows));
	Target[2].X=.9;TestFalse(TEXT("Lost-coordinate non-affine target is rejected"),FitEyeUVRows(Base,Target,Rows));
	Base={{0,0},{1,0},{0,1},{1,1}};Target={{0,0},{1,0},{0,1},{.9,1}};
	TestFalse(TEXT("Do not silently distort non-affine data"),FitEyeUVRows(Base,Target,Rows));
	Base.Reset();Target.Reset();TestTrue(TEXT("Optional absent highlight UVs"),FitEyeUVRows(Base,Target,Rows));
	Target.Add(FVector2D(0,0));TestFalse(TEXT("Mismatched source stream lengths"),FitEyeUVRows(Base,Target,Rows));
	return true;
}
#endif

static FString UnitySkinHueCode()
{
	// Koikano/main_skin 0072 PS:141-160. The first permutation sorts G/B;
	// the second must retain R even when R is not the maximum channel.
	// Share the conversion between diffuse and outline colour expressions.
	return TEXT(R"(
float3 c = saturate(Base.rgb);
float4 gb = c.g >= c.b
    ? float4(c.g, c.b, 0.0, -0.333333343)
    : float4(c.b, c.g, -1.0, 0.666666687);
float4 hsv = c.r >= gb.x
    ? float4(c.r, gb.y, gb.z, gb.x)
    : float4(gb.x, gb.y, gb.w, c.r);
float chroma = hsv.x - min(hsv.y, hsv.w);
float hue = abs((hsv.w - hsv.y) / max(chroma * 6.0, 1e-10) + hsv.z);
)");
}

static UTexture2D* OKAppearanceEmptyTexture(){
 static UTexture2D* Texture=nullptr;
 if(!Texture){Texture=UTexture2D::CreateTransient(1,1,PF_B8G8R8A8);if(Texture){
  void* Pixels=Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);FMemory::Memzero(Pixels,4);
  Texture->GetPlatformData()->Mips[0].BulkData.Unlock();Texture->SRGB=true;Texture->AddressX=Texture->AddressY=TA_Clamp;
  Texture->AddToRoot();Texture->UpdateResource();
 }}return Texture;
}

// ok-vertex-color-card parts (shader kind 20). COLOR0 carries the fully lit
// colour baked from the part's own material (ok.BakeCardAlbedo), sRGB
// encoded, with coverage in alpha. The camera light step of the toon graphs
// reapplies the card shadow colour on surfaces turned away from the view.
static UMaterial* MakeVertexColorMaterial(const FString& Blend, bool bTwoSided, FLinearColor ShadowTint)
{
#if WITH_EDITOR
	UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	UMaterialEditorOnlyData* Ed = Mat ? Mat->GetEditorOnlyData() : nullptr;
	if (!Ed)
	{
		return nullptr;
	}
	const bool bMasked = Blend.Equals(TEXT("masked"), ESearchCase::IgnoreCase);
	const bool bTranslucent = Blend.Equals(TEXT("translucent"), ESearchCase::IgnoreCase);
	Mat->bIsPreviewMaterial = true;
	Mat->MaterialDomain = MD_Surface;
	Mat->BlendMode = bTranslucent ? BLEND_Translucent : (bMasked ? BLEND_Masked : BLEND_Opaque);
	Mat->SetShadingModel(MSM_Unlit);
	Mat->OpacityMaskClipValue = 0.5f;
	Mat->TwoSided = bTwoSided;
	Mat->SetUsageByFlag(MATUSAGE_StaticMesh, true);
	Mat->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
	auto* Vc = NewObject<UMaterialExpressionVertexColor>(Mat);
	Vc->Material = Mat;
	Mat->GetExpressionCollection().AddExpression(Vc);
	auto* Normal = NewObject<UMaterialExpressionPixelNormalWS>(Mat);
	Normal->Material = Mat;
	Mat->GetExpressionCollection().AddExpression(Normal);
	auto* Tint = NewObject<UMaterialExpressionConstant3Vector>(Mat);
	Tint->Material = Mat;
	Tint->Constant = ShadowTint;
	Mat->GetExpressionCollection().AddExpression(Tint);
	auto* Shade = NewObject<UMaterialExpressionCustom>(Mat);
	Shade->Material = Mat;
	Shade->OutputType = CMOT_Float3;
	Shade->Code = TEXT("float3 c = saturate(C); float3 lin = lerp(pow((c + 0.055) / 1.055, 2.4), c / 12.92, step(c, 0.04045)); ")
		TEXT("float lambert = dot(normalize(N), -normalize(ResolvedView.ViewForward)); return lin * lerp(Tint, 1.0, smoothstep(0.0, 0.2, lambert));");
	for (const auto& Pair : { TPair<FName, UMaterialExpression*>(TEXT("C"), Vc), TPair<FName, UMaterialExpression*>(TEXT("N"), Normal), TPair<FName, UMaterialExpression*>(TEXT("Tint"), Tint) })
	{
		FCustomInput& Input = Shade->Inputs.AddDefaulted_GetRef();
		Input.InputName = Pair.Key;
		Input.Input.Expression = Pair.Value;
	}
	Mat->GetExpressionCollection().AddExpression(Shade);
	Ed->EmissiveColor.Expression = Shade;
	if (bMasked || bTranslucent)
	{
		FExpressionInput& Coverage = bMasked ? Ed->OpacityMask : Ed->Opacity;
		Coverage.Expression = Vc;
		Coverage.OutputIndex = 4;
	}
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	OKEnsureSkeletalMaterialUsage(Mat);
	return Mat;
#else
	return nullptr;
#endif
}

static UMaterialInterface* MakePartMaterial(UTexture2D* Tex, const FString& Blend, bool bUnlit, bool bTwoSided, bool bEye = false, UTexture2D* GlossTex = nullptr, UTexture2D* OverTex = nullptr, FLinearColor OverColor = FLinearColor::White, bool bNoZ = false, bool bPdo = false, bool bVcOverlay = false, UTexture2D* RampTex = nullptr, FLinearColor ShadowTint = FLinearColor(0.628f, 0.628f, 0.628f, 1.f), UTexture2D* DetailTex = nullptr, UTexture2D* LineTex = nullptr, UTexture2D* NormalTex = nullptr, UTexture2D* NormalMaskTex = nullptr, float ShadowExtend = 1.f, int32 ShaderKind = 0, const FOKToonShadowParameters* Shadow = nullptr, UTexture2D* AlphaMaskTex = nullptr, UTexture2D* OverTex2 = nullptr, bool bBodyOverlay = false)
{
	if (Shadow && Shadow->TripoPBR) return Shadow->TripoPBR;
	if (Shadow && Shadow->NativeSkin) return Shadow->NativeSkin;
    if(Shadow&&!Shadow->bEnableShadows)ShadowTint=FLinearColor::White;
#if WITH_EDITOR

	FOkCardProfileScope Profile(GOkCardLoadProfile.bActive ? &GOkCardLoadProfile.MaterialSeconds : nullptr);
	if (GOkCardLoadProfile.bActive) ++GOkCardLoadProfile.MaterialCount;
	if (ShaderKind == 9 && Shadow && Shadow->NativeItem) return OKMakeMainItemPlusMaterial(*Shadow->NativeItem,false);
	if (ShaderKind == 8 && Shadow && Shadow->NativeItem) return OKMakeForgeItemMaterial(*Shadow->NativeItem);
	if (ShaderKind == 2 && Shadow && Shadow->NativeItem && Shadow->NativeItem->Textures.Contains(TEXT("_MainTex"))) return OKMakeForgeOpaqueMaterial(*Shadow->NativeItem);
	if (ShaderKind == 11 && Shadow && Shadow->NativeItem) return OKMakeForgeAlphaMaterial(*Shadow->NativeItem);
	if (ShaderKind == 20) return MakeVertexColorMaterial(Blend, bTwoSided, ShadowTint);
	const bool bHairShader = ShaderKind == 1 || ShaderKind == 10;
	const bool bSkinShader = ShaderKind == 5 || ShaderKind == 7;
	// Preview is unlit/emissive for face and overlays so UE DefaultLit does
	// not self-shadow the inner-mouth triangles. Hair/body may still be lit.
	UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!Mat)
	{
		return nullptr;
	}
	const bool bMasked = Blend.Equals(TEXT("masked"), ESearchCase::IgnoreCase);
	// This new transient preview graph has no existing scene users. Unreal's
	// preview-material path compiles it without globally recreating unrelated
	// components on every part of a card.
	Mat->bIsPreviewMaterial = true;
	// Additive: Studio's light effects (Shader Forge/main_item_studio_add, Blend One One) add their colour to what is
	// behind them; black is no light.
	const bool bAdditive = Blend.Equals(TEXT("additive"), ESearchCase::IgnoreCase);
	const bool bTranslucent = bAdditive || Blend.Equals(TEXT("translucent"), ESearchCase::IgnoreCase);
	Mat->MaterialDomain = MD_Surface;
	Mat->BlendMode = bAdditive ? BLEND_Additive : bTranslucent ? BLEND_Translucent : (bMasked ? BLEND_Masked : BLEND_Opaque);
	// IBL_Shader_alpha evaluates Maker's SH and its direct BRDF inside the
	// fragment program. UE's studio light rig would light it a second time.
	Mat->SetShadingModel((bUnlit || ShaderKind == 6) ? MSM_Unlit : MSM_DefaultLit);
	if (bMasked)
	{
		Mat->OpacityMaskClipValue = 0.5f;
	}
	// Match the source ForwardBase pass: hair, items and Shader Forge skin
	// can render backfaces, while Koikano skin and eyes cull them.
	Mat->TwoSided = bTwoSided;
	// Hitomi, brows, upper eyeline, and nose line depth-test so front hair can
	// occlude them. Only the face-coplanar lower eyeline passes bNoZ.
	// `OK_EYE_NOZ=0` turns that remaining workaround off for diagnostics.
	const bool bEyeNoZ = bNoZ && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_EYE_NOZ")) != TEXT("0");
	if (bEyeNoZ)
	{
		Mat->BlendMode = BLEND_Translucent;
		Mat->bDisableDepthTest = true;
		Mat->TranslucencyPass = MTP_AfterDOF;
	}
	Mat->SetUsageByFlag(MATUSAGE_StaticMesh, true);
	Mat->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
	Mat->SetMaterialUsage(MATUSAGE_StaticMesh);
	Mat->SetMaterialUsage(MATUSAGE_SkeletalMesh);

	auto* Samp = NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat);
	Samp->ParameterName=TEXT("KKAppearanceMain");
	Samp->Material = Mat;
	Samp->SamplerType = Tex && !Tex->SRGB ? SAMPLERTYPE_LinearColor : SAMPLERTYPE_Color;
	Samp->ConstCoordinate = 0;
	Samp->Texture = Tex;
	// The Unity fragment samples t0 with _MainTex_ST (main_clothes_opaque
	// 0024.asm.txt:100-101), including card driven offsets on eye materials.
	Samp->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->MainTexST : FLinearColor(1.f, 1.f, 0.f, 0.f));
	if (Shadow && Shadow->EyeDrawKind) Samp->Coordinates.Expression=MakeRuntimeEyeUV(Mat,0,Shadow->MainTexST);
	if (Shadow && Shadow->bGagAnimation) Samp->Coordinates.Expression=MakeGagEyeUV(Mat,Samp->Coordinates.Expression,Shadow->GagTileAnimation,Shadow->GagMotion);

	if (bEye && Tex && (Tex->AddressX != TA_Clamp || Tex->AddressY != TA_Clamp))
	{
		Tex->AddressX = TA_Clamp;
		Tex->AddressY = TA_Clamp;
		// TextureFromPngBuffer created the resource with TA_Wrap; re-upload so
		// the clamp actually reaches the sampler before the material compiles.
		Tex->UpdateResource();
	}
	Mat->GetExpressionCollection().AddExpression(Samp);

	UMaterialExpression* MappedWorldNormal = nullptr;
	UMaterialExpression* EyeOpacity = nullptr;
	if (UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData())
	{
		if (NormalTex)
		{
			UMaterialExpressionTextureSample* Packed = NewObject<UMaterialExpressionTextureSample>(Mat);
			Packed->Material = Mat;
			Packed->SamplerType = SAMPLERTYPE_LinearColor;
			Packed->Texture = NormalTex;
			// t4 uses its own _NormalMap_ST, independent of the main texture.
			Packed->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->NormalMapST : FLinearColor(1.f, 1.f, 0.f, 0.f));
			Mat->GetExpressionCollection().AddExpression(Packed);
			UMaterialExpressionCustom* Unpack = NewObject<UMaterialExpressionCustom>(Mat);
			Unpack->Material = Mat;
			Unpack->OutputType = CMOT_Float3;
			// main_clothes_opaque/0024.asm.txt t4 is _NormalMap. Its optional
			// liquid perturbation samples t2 _Texture2 and t3 _liquidmask; t7
			// _DetailMask is never used for the finite-difference normal.
			const bool bSkinNormalDetail = ShaderKind == 5 && Shadow && Shadow->NormalDetailTex;
			// Both Shader Forge/main_hair ForwardBase 0000.asm.txt and
			// main_hair_front 0001.asm.txt multiply the sampled normal map's
			// red by alpha before remapping XY from [0,1] to [-1,1].
			// For materials with a normal map, alpha alone changes the hair normal.
			Unpack->Code = bSkinNormalDetail
				? TEXT("float2 b=float2(Packed.r*PackedAlpha,Packed.g)*2.0-1.0; float bz=sqrt(saturate(1.0-dot(b,b))); float2 d=float2(Detail.r*DetailAlpha,Detail.g)*2.0-1.0; float2 ds=d*Scale; float dz=sqrt(saturate(1.0-dot(ds,ds))); return normalize(float3(b+ds,bz*dz));")
				: (bHairShader || ShaderKind == 2 || ShaderKind == 3 || ShaderKind == 8 || bSkinShader)
					? TEXT("float2 xy = float2(PackedAlpha * Packed.r, Packed.g) * 2.0 - 1.0; return float3(xy, sqrt(saturate(1.0 - dot(xy, xy))));")
					: TEXT("float2 xy = float2(PackedAlpha, Packed.g) * 2.0 - 1.0; return float3(xy, sqrt(saturate(1.0 - dot(xy, xy))));");
			if (Shadow && Shadow->bStandardLinear)
				Unpack->Code = FString::Printf(TEXT("float2 xy=(float2(Packed.r*PackedAlpha,Packed.g)*2.0-1.0)*%.9g;return float3(xy,sqrt(saturate(1.0-dot(xy,xy))));"), Shadow->StandardNormalScale);
			FCustomInput& Input = Unpack->Inputs.AddDefaulted_GetRef();
			Input.InputName = TEXT("Packed");
			Input.Input.Expression = Packed;
			FCustomInput& AlphaInput = Unpack->Inputs.AddDefaulted_GetRef();
			AlphaInput.InputName = TEXT("PackedAlpha");
			AlphaInput.Input.Expression = Packed;
			AlphaInput.Input.OutputIndex = 4;
			if (bSkinNormalDetail)
			{
				UMaterialExpressionTextureSample* DetailNormal = NewObject<UMaterialExpressionTextureSample>(Mat);
				DetailNormal->Material = Mat;
				DetailNormal->SamplerType = SAMPLERTYPE_LinearColor;
				DetailNormal->Texture = Shadow->NormalDetailTex;
				DetailNormal->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow->NormalDetailST);
				Mat->GetExpressionCollection().AddExpression(DetailNormal);
				FCustomInput& DetailInput = Unpack->Inputs.AddDefaulted_GetRef();
				DetailInput.InputName = TEXT("Detail");
				DetailInput.Input.Expression = DetailNormal;
				FCustomInput& DetailAlphaInput = Unpack->Inputs.AddDefaulted_GetRef();
				DetailAlphaInput.InputName = TEXT("DetailAlpha");
				DetailAlphaInput.Input.Expression = DetailNormal;
				DetailAlphaInput.Input.OutputIndex = 4;
				UMaterialExpressionConstant* Scale = NewObject<UMaterialExpressionConstant>(Mat);
				Scale->Material = Mat;
				Scale->R = Shadow->NormalDetailScale;
				Mat->GetExpressionCollection().AddExpression(Scale);
				FCustomInput& ScaleInput = Unpack->Inputs.AddDefaulted_GetRef();
				ScaleInput.InputName = TEXT("Scale");
				ScaleInput.Input.Expression = Scale;
			}
			Mat->GetExpressionCollection().AddExpression(Unpack);
			Ed->Normal.Expression = Unpack;
			// The toon ramp is evaluated in Emissive on an unlit material.
			// PixelNormalWS can remain the vertex normal there even when the
			// material's Normal pin is connected. Feed the decoded tangent
			// normal through the mesh TBN explicitly, as the Unity fragment PS.
			UMaterialExpressionTransform* ToWorld = NewObject<UMaterialExpressionTransform>(Mat);
			ToWorld->Material = Mat;
			ToWorld->TransformSourceType = TRANSFORMSOURCE_Tangent;
			ToWorld->TransformType = TRANSFORM_World;
			UMaterialExpression* RampTangentNormal = Unpack;
			if (ShaderKind == 2 || ShaderKind == 3 || ShaderKind == 8)
			{
				// Forge/main_opaque PS 0012:125-128 and Koikano/
				// main_clothes_opaque PS 0024:285-288
				// forms XY from tangent/bitangent, but multiplies only the
				// geometric-normal Z term by SV_IsFrontFace ? 1 : -1.
				// UE flips its lit WorldNormal separately; our emissive ramp
				// uses this explicit TBN transform and needs the same sign.
				UMaterialExpressionTwoSidedSign* FaceSign = NewObject<UMaterialExpressionTwoSidedSign>(Mat);
				FaceSign->Material = Mat;
				Mat->GetExpressionCollection().AddExpression(FaceSign);
				UMaterialExpressionCustom* FlipGeoZ = NewObject<UMaterialExpressionCustom>(Mat);
				FlipGeoZ->Material = Mat;
				FlipGeoZ->OutputType = CMOT_Float3;
				FlipGeoZ->Code = TEXT("return float3(N.xy, N.z * Sign);");
				FCustomInput& NormalInput = FlipGeoZ->Inputs.AddDefaulted_GetRef();
				NormalInput.InputName = TEXT("N");
				NormalInput.Input.Expression = Unpack;
				FCustomInput& SignInput = FlipGeoZ->Inputs.AddDefaulted_GetRef();
				SignInput.InputName = TEXT("Sign");
				SignInput.Input.Expression = FaceSign;
				Mat->GetExpressionCollection().AddExpression(FlipGeoZ);
				RampTangentNormal = FlipGeoZ;
			}
			ToWorld->Input.Expression = RampTangentNormal;
			Mat->GetExpressionCollection().AddExpression(ToWorld);
			MappedWorldNormal = ToWorld;
		}
		if (bEye && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UV_DEBUG")) == TEXT("1"))
		{
			UMaterialExpressionTextureCoordinate* Uv = NewObject<UMaterialExpressionTextureCoordinate>(Mat);
			Uv->Material = Mat;
			Uv->CoordinateIndex = 0;
			Mat->GetExpressionCollection().AddExpression(Uv);
			Ed->EmissiveColor.Expression = Uv;
			Ed->EmissiveColor.OutputIndex = 0;
			Ed->BaseColor.Expression = Uv;
			Ed->BaseColor.OutputIndex = 0;
	Mat->UpdateCachedExpressionData();
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	Mat->ForceRecompileForRendering();
	Mat->BlendMode = BLEND_Translucent;
	Mat->bDisableDepthTest = true;
	Mat->TranslucencyPass = MTP_AfterDOF;
	return Mat;
}
		UMaterialExpression* ColorOut = Samp;
		if (Shadow && Shadow->bItemDraw)
		{
			// KKS Koikano/main_clothes_item PS 0024:102-116; EC Forge
			// main_item PS 0005:62-71 stops after the three masked colours.
			// ColorMask have independent ST; colours are Unity Color properties
			// in the linear player. Never bake this into a guessed mouth texture.
			UMaterialExpressionCustom* Mix = NewObject<UMaterialExpressionCustom>(Mat);
			Mix->Material = Mat;
			Mix->OutputType = CMOT_Float3;
			// Shader Forge/main_item_studio pattern slots (installed PS, SHA256 8e640300..., the cooked ForgeStudio
			// parent): only a part whose material overrides name a pattern property gets them, so items without a
			// pattern keep the plain colour mix. Slot k replaces colour k with lerp(_Colork_2, _Colork, t) where t
			// reads _PatternMaskk at the mesh UV moved by _Patternuvk (offset xy, scale zw), turned by
			// _patternrotatork*pi about the centre, clamped by _patternclampk, then _PatternMaskk_ST.
			bool bPattern = false;
			if (!Shadow->bHairSun) for (const FString& Name : Shadow->Exposed) bPattern |= OKIsItemPatternProperty(Name);
			auto Source = [Shadow](const FString& Name, const FLinearColor& Fallback) { const FLinearColor* V = Shadow->ExposedSource.Find(Name); return V ? *V : Fallback; };
			auto ToLinear = [](FLinearColor C) { for (float* V : {&C.R, &C.G, &C.B}) *V = *V <= 0.04045f ? *V / 12.92f : FMath::Pow((*V + 0.055f) / 1.055f, 2.4f); return C; };
			Mix->Code = TEXT("float3 K1=C1.rgb,K2=C2.rgb,K3=C3.rgb; ");
			if (bPattern)
			{
				TSet<FString> PatternNames;
				for (int32 Slot = 1; Slot <= 3; ++Slot) PatternNames.Append(OKItemPatternNames(Slot, false));
				auto* PatternUV = NewObject<UMaterialExpressionTextureCoordinate>(Mat);
				PatternUV->Material = Mat; Mat->GetExpressionCollection().AddExpression(PatternUV);
				FCustomInput& UVInput = Mix->Inputs.AddDefaulted_GetRef(); UVInput.InputName = TEXT("PUV"); UVInput.Input.Expression = PatternUV;
				for (int32 Slot = 1; Slot <= 3; ++Slot)
				{
					// _Patternuv, _patternrotator, _patternclamp, _PatternMask_ST, _Color_2, _PatternMask of this slot.
					const TArray<FString> N = OKItemPatternNames(Slot);
					const FString UV = OKExposedUniformInput(Mat, Mix, &PatternNames, N[0], 4, Source(N[0], FLinearColor(0.f, 0.f, 1.f, 1.f)));
					const FString Rotator = OKExposedUniformInput(Mat, Mix, &PatternNames, N[1], 1, Source(N[1], FLinearColor::Transparent));
					const FString Clamp = OKExposedUniformInput(Mat, Mix, &PatternNames, N[2], 1, Source(N[2], FLinearColor::Transparent));
					const FString ST = OKExposedUniformInput(Mat, Mix, &PatternNames, N[3], 4, Source(N[3], FLinearColor(1.f, 1.f, 0.f, 0.f)));
					const FString Second = OKExposedUniformInput(Mat, Mix, &PatternNames, N[4], 4, ToLinear(Source(N[4], FLinearColor(100.f / 255.f, 100.f / 255.f, 100.f / 255.f, 1.f))));
					auto* Mask = NewObject<UMaterialExpressionTextureObjectParameter>(Mat);
					Mask->Material = Mat; Mask->ParameterName = *N[5]; Mask->Texture = OKExposureWhiteTexture(); Mask->SamplerType = SAMPLERTYPE_LinearColor;
					Mat->GetExpressionCollection().AddExpression(Mask);
					FCustomInput& MaskInput = Mix->Inputs.AddDefaulted_GetRef(); MaskInput.InputName = *(TEXT("PM") + FString::FromInt(Slot)); MaskInput.Input.Expression = Mask;
					Mix->Code += FString::Printf(TEXT("{float4 puv=float4(%s);float2 p=float2(PUV.x,1.0-PUV.y)+puv.xy-0.5;float a=(%s).x*3.14159274;float s=sin(a),co=cos(a);")
						TEXT("p=float2(p.x*co+p.y*s,-p.x*s+p.y*co)+0.5;p=p*puv.zw+(puv.zw-1.0)*-0.5;p=p+(%s).x*(saturate(p)-p);float4 st=float4(%s);p=p*st.xy+st.zw;")
						TEXT("float4 m=PM%d.Sample(PM%dSampler,float2(p.x,1.0-p.y));float4 c2=float4(%s);float t=saturate(max(1.0-m.a,m.r)+(1.0-c2.a));K%d=lerp(c2.rgb,C%d.rgb,t);} "),
						*UV, *Rotator, *Clamp, *ST, Slot, Slot, *Second, Slot, Slot);
				}
			}
			Mix->Code += TEXT("float3 c = lerp(float3(1,1,1), K1, Mask.r); ");
            if (Shadow->bHairSun)
            {
                // Koikano hair_main_sun Forward PS 0096/0104/0112/0120:
                // runtime colour toggles; compile-time reverse swaps G/B.
                if (Shadow->bHairColor2) Mix->Code += Shadow->bHairReverseColors
                    ? TEXT("c=lerp(c,K2,Mask.b); ") : TEXT("c=lerp(c,K2,Mask.g); ");
                if (Shadow->bHairColor3) Mix->Code += Shadow->bHairReverseColors
                    ? TEXT("c=lerp(c,K3,Mask.g); ") : TEXT("c=lerp(c,K3,Mask.b); ");
            }
            else Mix->Code += TEXT("c=lerp(c,K2,Mask.g); c=lerp(c,K3,Mask.b); ");
			if (Shadow->bItemFourthColor)
				Mix->Code += TEXT("c = lerp(c, C4.rgb, 1.0-max(Mask.r,max(Mask.g,Mask.b))); ");
			Mix->Code += Shadow->bHairSun ? TEXT("return clamp(Main.rgb*c,0.01,1.0);") : TEXT("return Main.rgb*c;");
			FCustomInput& MainInput = Mix->Inputs.AddDefaulted_GetRef();
			MainInput.InputName = TEXT("Main"); MainInput.Input.Expression = Samp;
			UMaterialExpression* MaskInput = nullptr;
			if (Shadow->ColorMaskTex)
			{
				UMaterialExpressionTextureSample* Mask = NewObject<UMaterialExpressionTextureSample>(Mat);
				Mask->Material = Mat;
				Mask->Texture = Shadow->ColorMaskTex;
				Mask->SamplerType = Shadow->ColorMaskTex->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
				Mask->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow->ColorMaskST);
				Mat->GetExpressionCollection().AddExpression(Mask);
				MaskInput = Mask;
			}
			else
			{
				UMaterialExpressionConstant3Vector* Black = NewObject<UMaterialExpressionConstant3Vector>(Mat);
				Black->Material = Mat; Black->Constant = FLinearColor::Black;
				Mat->GetExpressionCollection().AddExpression(Black); MaskInput = Black;
			}
			FCustomInput& MaskSlot = Mix->Inputs.AddDefaulted_GetRef();
			MaskSlot.InputName = TEXT("Mask"); MaskSlot.Input.Expression = MaskInput;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				// An overridden (or patterned) colour is the parameter of its Unity name; the others stay literal.
				const FString ColorName = Index ? FString::Printf(TEXT("_Color%d"), Index + 1) : FString(TEXT("_Color"));
				UMaterialExpression* Color = nullptr;
				if (bPattern || Shadow->Exposed.Contains(ColorName))
				{
					auto* Parameter = NewObject<UMaterialExpressionVectorParameter>(Mat);
					Parameter->ParameterName = *ColorName; Parameter->DefaultValue = Shadow->ItemColors[Index]; Color = Parameter;
				}
				else
				{
					auto* Constant = NewObject<UMaterialExpressionConstant4Vector>(Mat);
					Constant->Constant = Shadow->ItemColors[Index]; Color = Constant;
				}
				Color->Material = Mat;
				Mat->GetExpressionCollection().AddExpression(Color);
				FCustomInput& Slot = Mix->Inputs.AddDefaulted_GetRef();
				Slot.InputName = *FString::Printf(TEXT("C%d"), Index + 1); Slot.Input.Expression = Color;
			}
			Mat->GetExpressionCollection().AddExpression(Mix);
			ColorOut = Mix;
		}
		if (Shadow && Shadow->EyeDrawKind)
		{
			// Koikano/main_eye PS 0008: highlights use R as intensity, max
			// between layers, independent alpha, and _isHighLight as a gate.
			auto* Mix = NewObject<UMaterialExpressionCustom>(Mat);
			Mix->OutputType = CMOT_Float4;
			Mix->Code = Shadow->EyeDrawKind == 1
                ? TEXT("float a=max(UpA*UpColor.a,DownA*DownColor.a)*Enabled; float3 h=max(UpR*UpColor.rgb*UpA,DownR*DownColor.rgb*DownA); return float4(saturate(lerp(Main*Color,h,a)),max(MainA,a));")
                : TEXT("float a=max(UpA*UpColor.a,DownA*DownColor.a)*Enabled; float3 h=max(UpColor.rgb*UpA,DownColor.rgb*DownA); return float4(lerp(Main,h,a),max(MainA,a));");
			auto Add = [&](const TCHAR* Name, UMaterialExpression* E, int32 Output = 0) {
				auto& I = Mix->Inputs.AddDefaulted_GetRef(); I.InputName = Name; I.Input.Expression = E; I.Input.OutputIndex = Output;
			};
			Add(TEXT("Main"), Samp); Add(TEXT("MainA"), Samp, 4);
			if (Shadow->EyeExpressionTex)
			{
				// main_eye: the expression replaces the pupil by its alpha (times _exppower 1), under the highlights.
				auto* Expression = NewObject<UMaterialExpressionTextureSample>(Mat);
				Expression->Material = Mat;
				Expression->Texture = Shadow->EyeExpressionTex;
				Expression->SamplerType = Shadow->EyeExpressionTex->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
				Expression->ConstCoordinate = 3;
				Mat->GetExpressionCollection().AddExpression(Expression);
				Add(TEXT("Expr"), Expression, 5);
				Mix->Code = TEXT("Main=lerp(Main,Expr.rgb,Expr.a); MainA=max(MainA,Expr.a); ") + Mix->Code;
			}
			auto* Color = NewObject<UMaterialExpressionConstant3Vector>(Mat);
			Color->Constant = Shadow->EyeColor; Mat->GetExpressionCollection().AddExpression(Color); Add(TEXT("Color"), Color);
			auto* Enabled = NewObject<UMaterialExpressionConstant>(Mat);
			Enabled->R = Shadow->EyeHighlight; Mat->GetExpressionCollection().AddExpression(Enabled); Add(TEXT("Enabled"), Enabled);
			for (int32 Layer = 0; Layer < 2; ++Layer) {
				auto* Col = NewObject<UMaterialExpressionConstant4Vector>(Mat);
				Col->Constant = Shadow->EyeColors[Layer]; Mat->GetExpressionCollection().AddExpression(Col);
				Add(Layer == 0 ? TEXT("UpColor") : TEXT("DownColor"), Col);
				{
					auto* T = NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat);
					T->ParameterName=Layer==0?TEXT("KKAppearanceEyeUp"):TEXT("KKAppearanceEyeDown");
					T->Texture = Shadow->EyeTextures[Layer]?Shadow->EyeTextures[Layer]:OKAppearanceEmptyTexture(); T->SamplerType = SAMPLERTYPE_Color;
					T->ConstCoordinate = Layer + 1;
					T->Coordinates.Expression=MakeRuntimeEyeUV(Mat,Layer+1);
					Mat->GetExpressionCollection().AddExpression(T);
					Add(Layer == 0 ? TEXT("UpR") : TEXT("DownR"), T, 1);
					Add(Layer == 0 ? TEXT("UpA") : TEXT("DownA"), T, 4);
				}
			}
			Mat->GetExpressionCollection().AddExpression(Mix);
			auto* RGB = NewObject<UMaterialExpressionComponentMask>(Mat); RGB->Input.Expression = Mix; RGB->R = RGB->G = RGB->B = true;
			Mat->GetExpressionCollection().AddExpression(RGB); ColorOut = RGB;
			auto* Alpha = NewObject<UMaterialExpressionComponentMask>(Mat); Alpha->Input.Expression = Mix; Alpha->A = true;
			Mat->GetExpressionCollection().AddExpression(Alpha); EyeOpacity = Alpha;
		}
		if (bVcOverlay)
		{
			UMaterialExpressionVertexColor* Vc = NewObject<UMaterialExpressionVertexColor>(Mat);
			Vc->Material = Mat;
			Mat->GetExpressionCollection().AddExpression(Vc);
			UMaterialExpressionLinearInterpolate* Lerp = NewObject<UMaterialExpressionLinearInterpolate>(Mat);
			Lerp->Material = Mat;
			Lerp->A.Expression = Samp;
			Lerp->B.Expression = Vc;
			Lerp->Alpha.Expression = Vc;
			Lerp->Alpha.OutputIndex = 4;
			Mat->GetExpressionCollection().AddExpression(Lerp);
			ColorOut = Lerp;
		}
		else if (bBodyOverlay && (OverTex || OverTex2 || (Shadow && Shadow->FaceOvertex3)))
		{
			// Koikano/main_skin PS 0072:120-149. VS 0071 supplies UV1/UV2
			// after their independent _texcoord2/3_ST and COLOR0.r/b gates.
			Mat->NumCustomizedUVs = Shadow && Shadow->FaceOvertex3 ? 4 : 3;
			UMaterialExpressionVertexColor* Vc = NewObject<UMaterialExpressionVertexColor>(Mat);
			Vc->Material = Mat;
			Mat->GetExpressionCollection().AddExpression(Vc);
			auto Constant = [Mat](float Value) -> UMaterialExpressionConstant* {
				UMaterialExpressionConstant* Node = NewObject<UMaterialExpressionConstant>(Mat);
				Node->Material = Mat;
				Node->R = Value;
				Mat->GetExpressionCollection().AddExpression(Node);
				return Node;
			};
			auto Vector = [Mat](const FLinearColor& Value) -> UMaterialExpressionConstant4Vector* {
				UMaterialExpressionConstant4Vector* Node = NewObject<UMaterialExpressionConstant4Vector>(Mat);
				Node->Material = Mat;
				Node->Constant = Value;
				Mat->GetExpressionCollection().AddExpression(Node);
				return Node;
			};
			auto Input = [](UMaterialExpressionCustom* Node, const TCHAR* Name, UMaterialExpression* Expr, int32 Output = 0) {
				FCustomInput& Slot = Node->Inputs.AddDefaulted_GetRef();
				Slot.InputName = Name;
				Slot.Input.Expression = Expr;
				Slot.Input.OutputIndex = Output;
			};
			auto OverlaySample = [Mat, Vc, Constant, Vector, Input, Shadow](UTexture2D* Texture, int32 UvChannel, bool bNip) -> UMaterialExpressionTextureSample* {
				if (!Texture) return nullptr;
				UMaterialExpressionTextureCoordinate* Uv = NewObject<UMaterialExpressionTextureCoordinate>(Mat);
				Uv->Material = Mat;
				Uv->CoordinateIndex = UvChannel;
				Mat->GetExpressionCollection().AddExpression(Uv);
				UMaterialExpressionCustom* Mapped = NewObject<UMaterialExpressionCustom>(Mat);
				Mapped->Material = Mat;
				Mapped->OutputType = CMOT_Float2;
				Mapped->Code = bNip ? TEXT(R"(
float2 u = float2(UV.x, 1.0-UV.y);
if(Forge==0) u=u*ST.xy+ST.zw;
float edge = saturate((length(u-0.5)-0.06)*16.666668);
float2 target = u*(2.0-1.4*Size) + (-0.5+0.7*Size);
float2 coord = lerp(u, target, edge);
coord = lerp(u*Vertex.r, coord*Vertex.r, Nip);
if(Forge!=0) coord=coord*ST.xy+ST.zw;
return float2(coord.x, 1.0-coord.y);
)") : TEXT("float2 u = float2(UV.x, 1.0-UV.y); if(Forge==0) u=u*ST.xy+ST.zw; u*=Vertex.b; if(Forge!=0) u=u*ST.xy+ST.zw; return float2(u.x, 1.0-u.y);");
				Input(Mapped, TEXT("UV"), Uv);
				Input(Mapped, TEXT("Vertex"), Vc);
				Input(Mapped, TEXT("ST"), Vector(Shadow ? (bNip ? Shadow->Texcoord2ST : Shadow->Texcoord3ST) : FLinearColor(1.f,1.f,0.f,0.f)));
				Input(Mapped, TEXT("Size"), Constant(Shadow ? Shadow->NipSize : 0.5f));
				Input(Mapped, TEXT("Nip"), Constant(Shadow ? Shadow->NipToggle : 0.f));
				Input(Mapped,TEXT("Forge"),Constant(Shadow && Shadow->bFaceForgeOverlay?1.f:0.f));
				Mat->GetExpressionCollection().AddExpression(Mapped);
				UMaterialExpressionTextureSample* Sample = NewObject<UMaterialExpressionTextureSample>(Mat);
				Sample->Material = Mat;
				Sample->SamplerType = Texture->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
				Sample->Texture = Texture;
				Sample->Coordinates.Expression = Mapped;
				Mat->GetExpressionCollection().AddExpression(Sample);
				return Sample;
			};
			UMaterialExpressionTextureSample* NipTex = OverlaySample(OverTex, 1, true);
			UMaterialExpressionTextureSample* UnderTex = OverlaySample(OverTex2, 2, false);
			UMaterialExpressionTextureSample* ThirdTex = nullptr;
            if (Shadow && Shadow->FaceOvertex3) {
                ThirdTex=NewObject<UMaterialExpressionTextureSample>(Mat);
                ThirdTex->Material=Mat;ThirdTex->Texture=Shadow->FaceOvertex3;
                ThirdTex->SamplerType=ThirdTex->Texture->SRGB?SAMPLERTYPE_Color:SAMPLERTYPE_LinearColor;
                UMaterialExpressionTextureCoordinate* Uv=NewObject<UMaterialExpressionTextureCoordinate>(Mat);
                Uv->Material=Mat;Uv->CoordinateIndex=3;Mat->GetExpressionCollection().AddExpression(Uv);
                UMaterialExpressionCustom* Mapped=NewObject<UMaterialExpressionCustom>(Mat);
                Mapped->Material=Mat;Mapped->OutputType=CMOT_Float2;
                Mapped->Code=TEXT("float2 u=float2(UV.x,1.0-UV.y)*ST.xy+ST.zw; return float2(u.x,1.0-u.y);");
                Input(Mapped,TEXT("UV"),Uv);Input(Mapped,TEXT("ST"),Vector(Shadow->Texcoord4ST));
                Mat->GetExpressionCollection().AddExpression(Mapped);
                ThirdTex->Coordinates.Expression=Mapped;Mat->GetExpressionCollection().AddExpression(ThirdTex);
            }
			UMaterialExpressionCustom* Combine = NewObject<UMaterialExpressionCustom>(Mat);
			Combine->Material = Mat;
			Combine->OutputType = CMOT_Float3;
			Combine->Code = TEXT(R"(
float3 tinted = NipRGB * NipColor.rgb;
float3 maskTint = NipRGB.r * (NipColor.rgb + 0.33 * NipRGB.g * NipSpecular);
float3 first = lerp(tinted, maskTint, saturate(Tex1Mask));
float3 c = lerp(Base.rgb, first, NipAlpha * NipColor.a);
c=lerp(c, UnderRGB * UnderColor.rgb, UnderAlpha * UnderColor.a);
return lerp(c, ThirdRGB * ThirdColor.rgb, ThirdAlpha * ThirdColor.a);
)");
			Input(Combine, TEXT("Base"), Samp);
			Input(Combine, TEXT("NipRGB"), NipTex ? static_cast<UMaterialExpression*>(NipTex) : Vector(FLinearColor::Black));
			Input(Combine, TEXT("NipAlpha"), NipTex ? static_cast<UMaterialExpression*>(NipTex) : Constant(0.f), NipTex ? 4 : 0);
			Input(Combine, TEXT("UnderRGB"), UnderTex ? static_cast<UMaterialExpression*>(UnderTex) : Vector(FLinearColor::Black));
			Input(Combine, TEXT("UnderAlpha"), UnderTex ? static_cast<UMaterialExpression*>(UnderTex) : Constant(0.f), UnderTex ? 4 : 0);
			// Unity Color properties reach the linear-space shader after sRGB
			// conversion. PNG sampling already decodes textures; the authored
			// overlay colours need the same conversion, retaining straight alpha.
			auto OverlayLinear = [](FLinearColor C) {
				auto Decode = [](float V) { return V <= .04045f ? V / 12.92f : FMath::Pow((V + .055f) / 1.055f, 2.4f); };
				return FLinearColor(Decode(C.R), Decode(C.G), Decode(C.B), C.A);
			};
			Input(Combine, TEXT("NipColor"), Vector(OverlayLinear(OverColor)));
			Input(Combine, TEXT("UnderColor"), Vector(OverlayLinear(Shadow ? Shadow->OverColor2 : FLinearColor::Black)));
			Input(Combine,TEXT("ThirdRGB"),ThirdTex?static_cast<UMaterialExpression*>(ThirdTex):Vector(FLinearColor::Black));
			Input(Combine,TEXT("ThirdAlpha"),ThirdTex?static_cast<UMaterialExpression*>(ThirdTex):Constant(0.f),ThirdTex?4:0);
			Input(Combine,TEXT("ThirdColor"),Vector(OverlayLinear(Shadow?Shadow->FaceOvercolor3:FLinearColor::Black)));
			Input(Combine, TEXT("NipSpecular"), Constant(Shadow ? Shadow->NipSpecular : 0.5f));
			Input(Combine, TEXT("Tex1Mask"), Constant(Shadow ? Shadow->Tex1Mask : 0.f));
			Mat->GetExpressionCollection().AddExpression(Combine);
			ColorOut = Combine;
		}
		else if (OverTex && OverColor.A > 0.001f)
		{
			Mat->NumCustomizedUVs = 2;
			OverTex->AddressX = TA_Clamp;
			OverTex->AddressY = TA_Clamp;
			OverTex->UpdateResource();
			UMaterialExpressionTextureCoordinate* Uv1 = NewObject<UMaterialExpressionTextureCoordinate>(Mat);
			Uv1->Material = Mat;
			Uv1->CoordinateIndex = 1;
			Mat->GetExpressionCollection().AddExpression(Uv1);
			UMaterialExpressionTextureSample* Over = NewObject<UMaterialExpressionTextureSample>(Mat);
			Over->Material = Mat;
			Over->SamplerType = SAMPLERTYPE_Color;
			Over->ConstCoordinate = 1;
			Over->Texture = OverTex;
			Over->Coordinates.Expression = Uv1;
			Mat->GetExpressionCollection().AddExpression(Over);
			UMaterialExpressionConstant3Vector* Col = NewObject<UMaterialExpressionConstant3Vector>(Mat);
			Col->Material = Mat;
			Col->Constant = FLinearColor(OverColor.R, OverColor.G, OverColor.B, 1.f);
			Mat->GetExpressionCollection().AddExpression(Col);
			UMaterialExpressionConstant* Ca = NewObject<UMaterialExpressionConstant>(Mat);
			Ca->Material = Mat;
			Ca->R = OverColor.A;
			Mat->GetExpressionCollection().AddExpression(Ca);
			UMaterialExpressionMultiply* Tint = NewObject<UMaterialExpressionMultiply>(Mat);
			Tint->Material = Mat;
			Tint->A.Expression = Over;
			Tint->B.Expression = Col;
			Mat->GetExpressionCollection().AddExpression(Tint);
			UMaterialExpressionMultiply* W = NewObject<UMaterialExpressionMultiply>(Mat);
			W->Material = Mat;
			W->A.Expression = Over;
			W->A.OutputIndex = 4;
			W->B.Expression = Ca;
			Mat->GetExpressionCollection().AddExpression(W);
			UMaterialExpressionLinearInterpolate* Lerp = NewObject<UMaterialExpressionLinearInterpolate>(Mat);
			Lerp->Material = Mat;
			Lerp->A.Expression = Samp;
			Lerp->B.Expression = Tint;
			Lerp->Alpha.Expression = W;
			Mat->GetExpressionCollection().AddExpression(Lerp);
			ColorOut = Lerp;
		}
		// Koikatsu samples global _RampG using the light/normal dot product.
		// Read the selected mt_ramp from system.xml so the preview uses its
		// actual Minimalist transition. Detail masks and normal maps still need
		// their per-shader paths below this shared ramp stage.
		// Shader Forge/main_hair_front is alpha-blended but its ForwardBase
		// still evaluates the same ramp and shadow terms as main_hair.
		if (RampTex && bUnlit && !bEye && !bEyeNoZ && (!bTranslucent || bHairShader))
		{
			UMaterialExpression* Normal = MappedWorldNormal;
			if (!Normal)
			{
				UMaterialExpressionPixelNormalWS* VertexNormal = NewObject<UMaterialExpressionPixelNormalWS>(Mat);
				VertexNormal->Material = Mat;
				Mat->GetExpressionCollection().AddExpression(VertexNormal);
				Normal = VertexNormal;
			}
			UMaterialExpression* RampNormal = Normal;
			if (bSkinShader && NormalMaskTex && Shadow && Shadow->FaceNormal != 0.f)
			{
				// KKLighting.cginc GetShadowAttenuation: mix the face normal
				// toward the view by _NormalMask.g * _FaceNormalG. Do not
				// renormalize the mixed vector; GetLambert uses it as-is.
				UMaterialExpressionTextureSample* FaceMask = NewObject<UMaterialExpressionTextureSample>(Mat);
				FaceMask->Material = Mat;
				FaceMask->SamplerType = SAMPLERTYPE_LinearColor;
				FaceMask->Texture = NormalMaskTex;
				FaceMask->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow->NormalMaskST);
				Mat->GetExpressionCollection().AddExpression(FaceMask);
				UMaterialExpressionCameraVectorWS* ViewDirection = NewObject<UMaterialExpressionCameraVectorWS>(Mat);
				ViewDirection->Material = Mat;
				Mat->GetExpressionCollection().AddExpression(ViewDirection);
				UMaterialExpressionConstant* FaceNormalAmount = NewObject<UMaterialExpressionConstant>(Mat);
				FaceNormalAmount->Material = Mat;
				FaceNormalAmount->R = Shadow->FaceNormal;
				Mat->GetExpressionCollection().AddExpression(FaceNormalAmount);
				UMaterialExpressionCustom* Adjusted = NewObject<UMaterialExpressionCustom>(Mat);
				Adjusted->Material = Mat;
				Adjusted->OutputType = CMOT_Float3;
				Adjusted->Code = TEXT("return N + (V - N) * M.g * FaceNormal;");
				for (const auto& Pair : { TPair<FName, UMaterialExpression*>(TEXT("N"), Normal), TPair<FName, UMaterialExpression*>(TEXT("V"), ViewDirection), TPair<FName, UMaterialExpression*>(TEXT("M"), FaceMask), TPair<FName, UMaterialExpression*>(TEXT("FaceNormal"), FaceNormalAmount) })
				{
					FCustomInput& Input = Adjusted->Inputs.AddDefaulted_GetRef();
					Input.InputName = Pair.Key;
					Input.Input.Expression = Pair.Value;
				}
				Mat->GetExpressionCollection().AddExpression(Adjusted);
				RampNormal = Adjusted;
			}
			UMaterialExpression* Light = nullptr;
			if (bHairShader || bSkinShader || ShaderKind == 8 || ShaderKind == 2 || ShaderKind == 3)
			{
				// Character maker parents its directional light to the camera.
				// To-light is the opposite of view forward. ViewForward is
				// constant over the draw, unlike CameraVectorWS.
				UMaterialExpressionCustom* CameraLight = NewObject<UMaterialExpressionCustom>(Mat);
				CameraLight->Material = Mat;
				CameraLight->OutputType = CMOT_Float3;
				CameraLight->Code = TEXT("return -normalize(ResolvedView.ViewForward);");
				Mat->GetExpressionCollection().AddExpression(CameraLight);
				Light = CameraLight;
			}
			else
			{
				UMaterialExpressionConstant3Vector* FixedLight = NewObject<UMaterialExpressionConstant3Vector>(Mat);
				FixedLight->Material = Mat;
				const FVector LightDir = FVector(-0.25f, 0.85f, 0.45f).GetSafeNormal();
				FixedLight->Constant = FLinearColor(LightDir.X, LightDir.Y, LightDir.Z, 1.f);
				Mat->GetExpressionCollection().AddExpression(FixedLight);
				Light = FixedLight;
			}
			UMaterialExpressionDotProduct* Lambert = NewObject<UMaterialExpressionDotProduct>(Mat);
			Lambert->Material = Mat;
			Lambert->A.Expression = RampNormal;
			Lambert->B.Expression = Light;
			Mat->GetExpressionCollection().AddExpression(Lambert);
			UMaterialExpression* RampLambert = Lambert;
			UMaterialExpressionCustom* ClothesRamp = nullptr;
			if (ShaderKind == 3 && DetailTex)
			{
				// Koikano/main_clothes_opaque ForwardBase PS (0024.asm.txt,
				// cb0[23].x/y and t7): the detail green channel shifts the
				// Lambert coordinate before sampling the global _RampG.
				UMaterialExpressionTextureSample* Detail = NewObject<UMaterialExpressionTextureSample>(Mat);
				Detail->Material = Mat;
				Detail->SamplerType = DetailTex->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
				Detail->Texture = DetailTex;
				Detail->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->DetailMaskST : FLinearColor(1.f, 1.f, 0.f, 0.f));
				Mat->GetExpressionCollection().AddExpression(Detail);
				UMaterialExpressionConstant* KanoShift = NewObject<UMaterialExpressionConstant>(Mat);
				KanoShift->Material = Mat;
				KanoShift->R = Shadow ? Shadow->KanoVerShift : 0.f;
				Mat->GetExpressionCollection().AddExpression(KanoShift);
				ClothesRamp = NewObject<UMaterialExpressionCustom>(Mat);
				ClothesRamp->Material = Mat;
				ClothesRamp->OutputType = CMOT_Float1;
				// 0024.asm.txt:289-305. Visibility is the fragment's shadow/
				// occlusion term before _RampG, not a multiplier after sampling it.
				// cb0[23].x (_KanoVerShift) supplies both the floor and the final
				// interpolation. cb0[23].y (_ShadowExtendAnother) is not read by
				// this ramp-coordinate sequence.
				ClothesRamp->Code = TEXT("float nd = saturate(NdotL); float dg = saturate(Detail.g); float gate = saturate((Visibility - 0.25) * 4.0); float floorLight = max(1.0 - KanoShift, (1.0 - dg) * (1.0 - dg)); float baseCoord = min(nd, gate); float shiftedCoord = min(gate, nd * floorLight) * (1.0 - 0.5 * dg); return lerp(baseCoord, shiftedCoord, saturate(KanoShift));");
				for (const auto& Pair : { TPair<FName, UMaterialExpression*>(TEXT("NdotL"), Lambert), TPair<FName, UMaterialExpression*>(TEXT("Detail"), Detail), TPair<FName, UMaterialExpression*>(TEXT("KanoShift"), KanoShift) })
				{
					FCustomInput& Input = ClothesRamp->Inputs.AddDefaulted_GetRef();
					Input.InputName = Pair.Key;
					Input.Input.Expression = Pair.Value;
				}
				FCustomInput& VisibilityInput = ClothesRamp->Inputs.AddDefaulted_GetRef();
				VisibilityInput.InputName = TEXT("Visibility");
				UMaterialExpressionConstant* NoOcclusion = NewObject<UMaterialExpressionConstant>(Mat);
				NoOcclusion->Material = Mat;
				NoOcclusion->R = 1.f;
				Mat->GetExpressionCollection().AddExpression(NoOcclusion);
				VisibilityInput.Input.Expression = NoOcclusion;
				Mat->GetExpressionCollection().AddExpression(ClothesRamp);
				RampLambert = ClothesRamp;
			}
			else if (bSkinShader)
			{
				// Koikano/main_skin clamps LdotN before _RampG; hair's
				// reconstructed ForwardBase deliberately samples the signed dot.
				UMaterialExpressionCustom* Nonnegative = NewObject<UMaterialExpressionCustom>(Mat);
				Nonnegative->Material = Mat;
				Nonnegative->OutputType = CMOT_Float1;
				Nonnegative->Code = TEXT("return max(0.0, NdotL);");
				FCustomInput& NormalInput = Nonnegative->Inputs.AddDefaulted_GetRef();
				NormalInput.InputName = TEXT("NdotL");
				NormalInput.Input.Expression = Lambert;
				Mat->GetExpressionCollection().AddExpression(Nonnegative);
				RampLambert = Nonnegative;
			}
			UMaterialExpression* RampUv = nullptr;
			if (bHairShader || ShaderKind == 8)
			{
				// Forge main_item uses cb0[20] and the same signed dot on
				// BOTH axes (PS 0005:172-174), not a fixed V=.5.
				// Forge main_hair ForwardBase: cb0[16] is _RampG_ST. The
				// same signed N dot light addresses both ramp axes (PS 0000
				// lines 138-140). PNG rows are top-left in UE, so flip V after ST.
				UMaterialExpressionConstant4Vector* RampST = NewObject<UMaterialExpressionConstant4Vector>(Mat);
				RampST->Material = Mat;
				RampST->Constant = Shadow ? Shadow->RampGST : FLinearColor(1.f, 1.f, 0.f, 0.f);
				Mat->GetExpressionCollection().AddExpression(RampST);
				UMaterialExpressionCustom* ForgeRampUv = NewObject<UMaterialExpressionCustom>(Mat);
				ForgeRampUv->Material = Mat;
				ForgeRampUv->OutputType = CMOT_Float2;
				ForgeRampUv->Code = TEXT("return float2(NdotL * ST.x + ST.z, 1.0 - NdotL * ST.y - ST.w);");
				FCustomInput& LightInput = ForgeRampUv->Inputs.AddDefaulted_GetRef();
				LightInput.InputName = TEXT("NdotL");
				LightInput.Input.Expression = RampLambert;
				FCustomInput& STInput = ForgeRampUv->Inputs.AddDefaulted_GetRef();
				STInput.InputName = TEXT("ST");
				STInput.Input.Expression = RampST;
				Mat->GetExpressionCollection().AddExpression(ForgeRampUv);
				RampUv = ForgeRampUv;
			}
			else
			{
				UMaterialExpressionConstant* Half = NewObject<UMaterialExpressionConstant>(Mat);
				Half->Material = Mat;
				Half->R = 0.5f;
				Mat->GetExpressionCollection().AddExpression(Half);
				UMaterialExpressionAppendVector* OtherRampUv = NewObject<UMaterialExpressionAppendVector>(Mat);
				OtherRampUv->Material = Mat;
				OtherRampUv->A.Expression = RampLambert;
				OtherRampUv->B.Expression = Half;
				Mat->GetExpressionCollection().AddExpression(OtherRampUv);
				RampUv = OtherRampUv;
			}
			UMaterialExpressionTextureSample* Ramp = NewObject<UMaterialExpressionTextureSample>(Mat);
			Ramp->Material = Mat;
			Ramp->SamplerType = SAMPLERTYPE_LinearColor;
			Ramp->Texture = RampTex;
			Ramp->Coordinates.Expression = RampUv;
			Mat->GetExpressionCollection().AddExpression(Ramp);
			// _ShadowColor is a per-material property, including ME overrides.
			// Convert the gamma-space material value for emissive output.
			UMaterialExpressionConstant3Vector* ShadowColor = NewObject<UMaterialExpressionConstant3Vector>(Mat);
			ShadowColor->Material = Mat;
			// Koikano/main_skin has no _ShadowColor material property.  The
			// fallback 0.628 belongs to other shader families; applying it to
			// skin turns the skirt's shadow map into a dark polygon on the legs.
			ShadowColor->Constant = bSkinShader ? FLinearColor::White : FLinearColor(
				FMath::Pow(FMath::Clamp(ShadowTint.R, 0.f, 1.f), 2.2f),
				FMath::Pow(FMath::Clamp(ShadowTint.G, 0.f, 1.f), 2.2f),
				FMath::Pow(FMath::Clamp(ShadowTint.B, 0.f, 1.f), 2.2f), 1.f);
			Mat->GetExpressionCollection().AddExpression(ShadowColor);
			UMaterialExpressionConstant* FullLight = NewObject<UMaterialExpressionConstant>(Mat);
			FullLight->Material = Mat;
			FullLight->R = 1.f;
			Mat->GetExpressionCollection().AddExpression(FullLight);
			UMaterialExpression* HairVisibility = FullLight;
			UMaterialExpressionLinearInterpolate* ShadowMix = NewObject<UMaterialExpressionLinearInterpolate>(Mat);
			ShadowMix->Material = Mat;
			ShadowMix->A.Expression = ShadowColor;
			ShadowMix->B.Expression = FullLight;
			ShadowMix->Alpha.Expression = Ramp;
			// TextureSample output 1 is the red scalar (output 0 is RGB).
			// Unity's _RampG shader also samples the ramp's red channel.
			ShadowMix->Alpha.OutputIndex = 1;
			UMaterialExpression* DetailForShadow = nullptr;
			UMaterialExpressionConstant* ExtendForShadow = nullptr;
			if (bHairShader || (ShaderKind == 2 && DetailTex))
			{
				// The hair and item shaders both read the DetailMask's green
				// channel, but combine it with _ShadowExtend differently.
				UMaterialExpression* Detail = nullptr;
				if (DetailTex)
				{
					UMaterialExpressionTextureSample* DetailSample = NewObject<UMaterialExpressionTextureSample>(Mat);
					DetailSample->Material = Mat;
					DetailSample->SamplerType = DetailTex->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
					DetailSample->Texture = DetailTex;
					// main_hair/0000.asm.txt:171-172 applies _DetailMask_ST before
					// reading t7. The same material property is already carried by the
					// session; sampling UV0 here skipped authored offsets on mod hair.
					DetailSample->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->DetailMaskST : FLinearColor(1.f, 1.f, 0.f, 0.f));
					Detail = DetailSample;
				}
				else if (bHairShader)
				{
					// Shader Forge/main_hair and main_hair_front serialize a black
					// default _DetailMask, even when the material has no texture bound.
					UMaterialExpressionConstant4Vector* BlackDetail = NewObject<UMaterialExpressionConstant4Vector>(Mat);
					BlackDetail->Material = Mat;
					BlackDetail->Constant = FLinearColor::Black;
					Detail = BlackDetail;
				}
				Mat->GetExpressionCollection().AddExpression(Detail);
				DetailForShadow = Detail;
				UMaterialExpressionConstant* Extend = NewObject<UMaterialExpressionConstant>(Mat);
				Extend->Material = Mat;
				Extend->R = FMath::Clamp(ShadowExtend, 0.f, 1.f);
				Mat->GetExpressionCollection().AddExpression(Extend);
				ExtendForShadow = Extend;
				UMaterialExpressionCustom* HairShadow = NewObject<UMaterialExpressionCustom>(Mat);
				HairShadow->Material = Mat;
				HairShadow->OutputType = CMOT_Float1;
				HairShadow->Code = bHairShader
					? TEXT("return max(max(Ramp.r, 1.0 - Detail.g), 1.0 - Extend);")
					: TEXT("float floorLight = 1.0 - 1.2 * Extend; float drawnShadow = min(1.0 - Line.b, 1.0 - Detail.g) * (1.0 - floorLight) + floorLight; return saturate(Ramp.r * drawnShadow);");
				UMaterialExpressionTextureSample* Line = nullptr;
				if (ShaderKind == 2 && LineTex)
				{
					Line = NewObject<UMaterialExpressionTextureSample>(Mat);
					Line->Material = Mat;
					Line->SamplerType = SAMPLERTYPE_LinearColor;
					Line->Texture = LineTex;
					Line->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->LineMaskST : FLinearColor(1.f, 1.f, 0.f, 0.f));
					Mat->GetExpressionCollection().AddExpression(Line);
				}
				for (const auto& Pair : { TPair<FName, UMaterialExpression*>(TEXT("Ramp"), Ramp), TPair<FName, UMaterialExpression*>(TEXT("Detail"), Detail), TPair<FName, UMaterialExpression*>(TEXT("Extend"), Extend) })
				{
					FCustomInput& Input = HairShadow->Inputs.AddDefaulted_GetRef();
					Input.InputName = Pair.Key;
					Input.Input.Expression = Pair.Value;
				}
				if (ShaderKind == 2)
				{
					FCustomInput& Input = HairShadow->Inputs.AddDefaulted_GetRef();
					Input.InputName = TEXT("Line");
					if (!Line)
					{
						UMaterialExpressionConstant3Vector* Black = NewObject<UMaterialExpressionConstant3Vector>(Mat);
						Black->Material = Mat;
						Black->Constant = FLinearColor::Black;
						Mat->GetExpressionCollection().AddExpression(Black);
						Input.Input.Expression = Black;
					}
					else
					{
						Input.Input.Expression = Line;
					}
				}
				Mat->GetExpressionCollection().AddExpression(HairShadow);
				ShadowMix->Alpha.Expression = HairShadow;
				ShadowMix->Alpha.OutputIndex = 0;
			}
			if (Shadow && Shadow->Depth && !bSkinShader)
			{
				UMaterialExpressionWorldPosition* WorldPos = NewObject<UMaterialExpressionWorldPosition>(Mat);
				WorldPos->Material = Mat;
				Mat->GetExpressionCollection().AddExpression(WorldPos);
				auto VectorConstant = [Mat](const FVector& V) -> UMaterialExpressionConstant3Vector* {
					UMaterialExpressionConstant3Vector* Node = NewObject<UMaterialExpressionConstant3Vector>(Mat);
					Node->Material = Mat;
					Node->Constant = FLinearColor(V.X, V.Y, V.Z, 1.f);
					Mat->GetExpressionCollection().AddExpression(Node);
					return Node;
				};
				UMaterialExpressionConstant3Vector* Center = VectorConstant(Shadow->Center);
				UMaterialExpressionConstant3Vector* Right = VectorConstant(Shadow->Right);
				UMaterialExpressionConstant3Vector* Up = VectorConstant(Shadow->Up);
				UMaterialExpressionConstant3Vector* ShadowLightNode = VectorConstant(Shadow->LightDir);
				UMaterialExpressionConstant* Width = NewObject<UMaterialExpressionConstant>(Mat);
				Width->Material = Mat;
				Width->R = Shadow->Width;
				Mat->GetExpressionCollection().AddExpression(Width);
				UMaterialExpressionCustom* ShadowUv = NewObject<UMaterialExpressionCustom>(Mat);
				ShadowUv->Material = Mat;
				ShadowUv->OutputType = CMOT_Float2;
				ShadowUv->Code = TEXT("float3 delta = Position - Center; return float2(0.5 + dot(delta, Right) / Width, 0.5 - dot(delta, Up) / Width);");
				for (const auto& Pair : { TPair<FName, UMaterialExpression*>(TEXT("Position"), WorldPos), TPair<FName, UMaterialExpression*>(TEXT("Center"), Center), TPair<FName, UMaterialExpression*>(TEXT("Right"), Right), TPair<FName, UMaterialExpression*>(TEXT("Up"), Up), TPair<FName, UMaterialExpression*>(TEXT("Width"), Width) })
				{
					FCustomInput& Input = ShadowUv->Inputs.AddDefaulted_GetRef();
					Input.InputName = Pair.Key;
					Input.Input.Expression = Pair.Value;
				}
				Mat->GetExpressionCollection().AddExpression(ShadowUv);
				UMaterialExpressionTextureSampleParameter2D* DepthSamples[4] = {};
				const TCHAR* Offsets[4] = {
					TEXT("return UV + float2(-0.00048828125, -0.00048828125);"),
					TEXT("return UV + float2( 0.00048828125, -0.00048828125);"),
					TEXT("return UV + float2(-0.00048828125,  0.00048828125);"),
					TEXT("return UV + float2( 0.00048828125,  0.00048828125);")};
				for (int32 Tap = 0; Tap < 4; ++Tap)
				{
					UMaterialExpressionCustom* OffsetUv = NewObject<UMaterialExpressionCustom>(Mat);
					OffsetUv->Material = Mat;
					OffsetUv->OutputType = CMOT_Float2;
					OffsetUv->Code = Offsets[Tap];
					FCustomInput& UvInput = OffsetUv->Inputs.AddDefaulted_GetRef();
					UvInput.InputName = TEXT("UV");
					UvInput.Input.Expression = ShadowUv;
					Mat->GetExpressionCollection().AddExpression(OffsetUv);
					DepthSamples[Tap] = NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat);
					DepthSamples[Tap]->Material = Mat;
					DepthSamples[Tap]->ParameterName = TEXT("KKShadowDepth");
					DepthSamples[Tap]->SamplerType = SAMPLERTYPE_LinearColor;
					DepthSamples[Tap]->Texture = Shadow->Depth;
					DepthSamples[Tap]->Coordinates.Expression = OffsetUv;
					Mat->GetExpressionCollection().AddExpression(DepthSamples[Tap]);
				}
				UMaterialExpressionConstant* Distance = NewObject<UMaterialExpressionConstant>(Mat);
				Distance->Material = Mat;
				Distance->R = Shadow->CameraDistance;
				Mat->GetExpressionCollection().AddExpression(Distance);
				UMaterialExpressionCustom* ShadowAttenuation = NewObject<UMaterialExpressionCustom>(Mat);
				ShadowAttenuation->Material = Mat;
				ShadowAttenuation->OutputType = CMOT_Float1;
				ShadowAttenuation->Code = TEXT("float receiver = Distance - dot(Position - Center, LightDir); if (!all(UV >= 0.0) || !all(UV <= 1.0)) return 1.0; float4 depths = float4(Depth0, Depth1, Depth2, Depth3); return dot(step(receiver, depths + 1.5), float4(0.25,0.25,0.25,0.25));");
				for (const auto& Pair : { TPair<FName, UMaterialExpression*>(TEXT("Position"), WorldPos), TPair<FName, UMaterialExpression*>(TEXT("Center"), Center), TPair<FName, UMaterialExpression*>(TEXT("LightDir"), ShadowLightNode), TPair<FName, UMaterialExpression*>(TEXT("Distance"), Distance), TPair<FName, UMaterialExpression*>(TEXT("UV"), ShadowUv) })
				{
					FCustomInput& Input = ShadowAttenuation->Inputs.AddDefaulted_GetRef();
					Input.InputName = Pair.Key;
					Input.Input.Expression = Pair.Value;
				}
				for (int32 Tap = 0; Tap < 4; ++Tap)
				{
					FCustomInput& DepthInput = ShadowAttenuation->Inputs.AddDefaulted_GetRef();
					DepthInput.InputName = FName(*FString::Printf(TEXT("Depth%d"), Tap));
					DepthInput.Input.Expression = DepthSamples[Tap];
					DepthInput.Input.OutputIndex = 1;
				}
				Mat->GetExpressionCollection().AddExpression(ShadowAttenuation);
			HairVisibility = ShadowAttenuation;
			if (ClothesRamp)
			{
				ClothesRamp->Inputs.Last().Input.Expression = ShadowAttenuation;
			}
				if (bHairShader && DetailForShadow && ExtendForShadow)
				{
					// Hair.shader applies the shadow map before its DetailMask.g
					// and _ShadowExtend light floors. Applying it afterwards turns
					// every overlapping hair card fully black.
					UMaterialExpressionCustom* HairMapShadow = NewObject<UMaterialExpressionCustom>(Mat);
					HairMapShadow->Material = Mat;
					HairMapShadow->OutputType = CMOT_Float1;
					HairMapShadow->Code = TEXT("return max(max(Ramp.r * Visibility, 1.0 - Detail.g), 1.0 - Extend);");
					for (const auto& Pair : { TPair<FName, UMaterialExpression*>(TEXT("Ramp"), Ramp), TPair<FName, UMaterialExpression*>(TEXT("Visibility"), ShadowAttenuation), TPair<FName, UMaterialExpression*>(TEXT("Detail"), DetailForShadow), TPair<FName, UMaterialExpression*>(TEXT("Extend"), ExtendForShadow) })
					{
						FCustomInput& Input = HairMapShadow->Inputs.AddDefaulted_GetRef();
						Input.InputName = Pair.Key;
						Input.Input.Expression = Pair.Value;
					}
					Mat->GetExpressionCollection().AddExpression(HairMapShadow);
					ShadowMix->Alpha.Expression = HairMapShadow;
				}
				else if (ShaderKind != 3)
				{
					UMaterialExpressionMultiply* ShadowedRamp = NewObject<UMaterialExpressionMultiply>(Mat);
					ShadowedRamp->Material = Mat;
					ShadowedRamp->A.Expression = ShadowMix->Alpha.Expression;
					ShadowedRamp->A.OutputIndex = ShadowMix->Alpha.OutputIndex;
					ShadowedRamp->B.Expression = ShadowAttenuation;
					Mat->GetExpressionCollection().AddExpression(ShadowedRamp);
					ShadowMix->Alpha.Expression = ShadowedRamp;
				}
				// Clothes still sample _RampG.r directly here. Only the other
				// families replace the ramp sample with a scalar expression.
				if (ShaderKind != 3)
				{
					ShadowMix->Alpha.OutputIndex = 0;
				}
			}
			Mat->GetExpressionCollection().AddExpression(ShadowMix);
			if (bSkinShader)
			{
				// Koikano/main_skin does not use the other families' _ShadowColor.
				// 0072.asm.txt:182-306 and the reconstructed ForwardBase frag show
				// a hue-derived shadow color and a ramp gated by LineMask.b and
				// DetailMask.g. Studio.SceneInfo defaults ambientShadow to RGB 128/255;
				// SystemButtonCtrl.SetAmbientShadow sends 1 - ambientShadowG as alpha.
				UMaterialExpressionCustom* Skin = NewObject<UMaterialExpressionCustom>(Mat);
				Skin->Material = Mat;
				Skin->OutputType = CMOT_Float3;
				Skin->Code = UnitySkinHueCode() + TEXT(R"(
float3 phases = 1.0 - frac(hue + float3(0.0, -0.333333343, 0.333333343)) * 2.0;
float3 hueTint = lerp(1.0, saturate(abs(phases) * 3.0 - 1.0),
    0.66 * chroma / max(hsv.x, 1e-10));
// 0072.asm.txt:316-404: the DetailMask's shifted red and alpha channels
// modulate a hue-coloured texture specular before diffuse shading.
float3 specPhases = abs(1.0 - frac(hue + float3(-0.08, -0.41, 0.25)) * 2.0) * 3.0 - 1.0;
float3 specHue = 0.97 * lerp(1.0, saturate(specPhases), 0.4) - 1.0;
float3 ambientRgb = float3(0.5019608, 0.5019608, 0.5019608);
float3 ambient = Depth > 0.5 ? 1.0 - (2.0 - 2.0 * Depth) * (1.0 - ambientRgb) : 2.0 * Depth * ambientRgb;
float3 dark = saturate(lerp(1.8 * hueTint * ambient,
    1.0 - (2.0 - 1.8 * hueTint) * (1.0 - ambient), step(0.5555556, hueTint)));
float floorLight = 1.0 - 1.2 * Extend;
// main_skin/0072.asm.txt:358-382 samples _LineMask.b into r17.x for
// shadow/ramp gating; _LineMask.r is a separate drawn-line blend below.
float gate = min(1.0 - Line.b * Scale, 1.0 - Detail.g);
float drawnLight = saturate(lerp(floorLight, 1.0, gate));
float light = saturate(Ramp.r * drawnLight);
float detailR = saturate(SpecDetail.r * 1.6666667);
float detailCube = detailR * detailR * detailR;
float specPower = max(DetailAlpha * Power, (1.0 - DetailAlpha) * NailPower);
float drawnSpec = max(0.0, detailR - detailCube) * specPower;
float specCoeff = min(min(min(dot(SpecColor.rgb, float3(0.299, 0.587, 0.114)), drawnSpec), light), DetailAlpha);
float3 texSpec = c * (1.0 + specCoeff * specHue) + specPower * detailCube * SpecColor.rgb;
float3 halfDir = normalize(normalize(View) + normalize(LightDir));
float ndh = saturate(dot(normalize(NormalDir), halfDir));
float directSpec = saturate(pow(ndh, 256.0 * Power) * Power * SpecColor.a);
float3 litBase = saturate(lerp(texSpec, c + directSpec * SpecColor.rgb, saturate(NoTexSpec)));
// main_skin ForwardBase lines 2613 and 2639-2646 multiply the diffuse
// result by a second lerp(dark, 1, drawnLight) before line-path blending.
// This factor remains active when _linetexon=0 on the body.
float3 diffusePath = litBase * lerp(dark, float3(1.0, 1.0, 1.0), light);
float3 finalDiffuse = diffusePath * lerp(dark, float3(1.0, 1.0, 1.0), drawnLight);
float lineMaskR = (1.0 - Line.r * Scale) * 0.5 + 0.5;
float linePower = pow((1.0 - LineWidth) * 0.8 + 0.2, Line.g);
float lineWeight = lerp(1.0, min(lineMaskR, linePower), saturate(LineTexOn));
// ChaShader.ChangeLineColor sets _LineColorG = (0.5,0.5,0.5,1-lineDepth).
// main_skin ForwardBase's final three MADs combine the lit result with the
// drawn-line endpoint. In particular lineDepth=1 makes that endpoint zero,
// not the once-shaded diffuse colour used by the previous approximation.
float lineAlpha = 1.0 - saturate(LineDepth);
float detailHalf = (1.0 - Detail.g) * 0.5 + 0.5;
float3 lineBase = 0.5 * c * dark;
float3 inverted = 1.0 - lineBase * detailHalf;
float3 lineOverlay = lineAlpha > 0.5
    ? 1.0 - (2.0 - 2.0 * lineAlpha) * inverted
    : lineBase * detailHalf * (2.0 * lineAlpha);
lineOverlay = saturate(lineOverlay);
return lineWeight * (finalDiffuse - lineOverlay) + lineOverlay;
)");
				auto AddSkinInput = [Mat, Skin](const TCHAR* Name, UMaterialExpression* Expr, int32 Output = 0) {
					FCustomInput& Input = Skin->Inputs.AddDefaulted_GetRef();
					Input.InputName = Name;
					Input.Input.Expression = Expr;
					Input.Input.OutputIndex = Output;
				};
				auto SampleSkinMask = [Mat, Shadow](UTexture2D* Texture, bool bDetailST) -> UMaterialExpression* {
					if (!Texture)
					{
						UMaterialExpressionConstant4Vector* Black = NewObject<UMaterialExpressionConstant4Vector>(Mat);
						Black->Material = Mat;
						// Unity's default black texture retains alpha 1, which the
						// skin specular path uses as its mask selector.
						Black->Constant = FLinearColor(0.f, 0.f, 0.f, 1.f);
						Mat->GetExpressionCollection().AddExpression(Black);
						return Black;
					}
					UMaterialExpressionTextureSample* Sample = NewObject<UMaterialExpressionTextureSample>(Mat);
					Sample->Material = Mat;
					Sample->SamplerType = Texture->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
					Sample->Texture = Texture;
					Sample->Coordinates.Expression = MakeUnityMaskUV(Mat,
						Shadow ? (bDetailST ? Shadow->DetailMaskST : Shadow->LineMaskST) : FLinearColor(1.f, 1.f, 0.f, 0.f));
					Mat->GetExpressionCollection().AddExpression(Sample);
					return Sample;
				};
				auto Scalar = [Mat](float Value) -> UMaterialExpressionConstant* {
					UMaterialExpressionConstant* C = NewObject<UMaterialExpressionConstant>(Mat);
					C->Material = Mat;
					C->R = Value;
					Mat->GetExpressionCollection().AddExpression(C);
					return C;
				};
				AddSkinInput(TEXT("Base"), ColorOut);
				UMaterialExpression* SkinDetail = SampleSkinMask(DetailTex, true);
				AddSkinInput(TEXT("Detail"), SkinDetail);
				AddSkinInput(TEXT("DetailAlpha"), DetailTex ? SkinDetail : Scalar(1.f), DetailTex ? 4 : 0);
				AddSkinInput(TEXT("Ramp"), ShadowMix->Alpha.Expression, ShadowMix->Alpha.OutputIndex);
				AddSkinInput(TEXT("Line"), SampleSkinMask(LineTex, false));
				AddSkinInput(TEXT("Extend"), Scalar(ShadowExtend));
				AddSkinInput(TEXT("Scale"), Scalar(Shadow ? Shadow->NormalDetailScale : 0.f));
				AddSkinInput(TEXT("Depth"), Scalar(Shadow ? Shadow->AmbientDepth : 0.26f));
				AddSkinInput(TEXT("LineWidth"), Scalar(Shadow ? Shadow->LineWidth : 0.5f));
				AddSkinInput(TEXT("LineDepth"), Scalar(Shadow ? Shadow->LineDepth : 1.f));
				AddSkinInput(TEXT("LineTexOn"), Scalar(Shadow ? Shadow->LineTexOn : 1.f));
				AddSkinInput(TEXT("Power"), Scalar(Shadow ? Shadow->SpecularPower : 0.f));
				AddSkinInput(TEXT("NailPower"), Scalar(Shadow ? Shadow->SpecularPowerNail : 0.f));
				AddSkinInput(TEXT("NoTexSpec"), Scalar(Shadow ? Shadow->NoTexSpecular : 0.f));
				UMaterialExpressionConstant4Vector* SkinSpecColor = NewObject<UMaterialExpressionConstant4Vector>(Mat);
				SkinSpecColor->Material = Mat;
				SkinSpecColor->Constant = Shadow ? Shadow->SpecularColor : FLinearColor(1.f, 1.f, 1.f, 0.f);
				Mat->GetExpressionCollection().AddExpression(SkinSpecColor);
				AddSkinInput(TEXT("SpecColor"), SkinSpecColor);
				UMaterialExpressionCameraVectorWS* SkinView = NewObject<UMaterialExpressionCameraVectorWS>(Mat);
				SkinView->Material = Mat;
				Mat->GetExpressionCollection().AddExpression(SkinView);
				AddSkinInput(TEXT("View"), SkinView);
				AddSkinInput(TEXT("NormalDir"), Normal);
				AddSkinInput(TEXT("LightDir"), Light);
				UMaterialExpression* SpecDetail = SkinDetail;
				if (DetailTex)
				{
				UMaterialExpressionTextureCoordinate* UV = NewObject<UMaterialExpressionTextureCoordinate>(Mat);
				UV->Material = Mat;
				Mat->GetExpressionCollection().AddExpression(UV);
				UMaterialExpressionVertexTangentWS* Tangent = NewObject<UMaterialExpressionVertexTangentWS>(Mat);
				Tangent->Material = Mat;
				Mat->GetExpressionCollection().AddExpression(Tangent);
				// 0071 VS supplies the authored, tangent.w-signed bitangent in
				// v3/v4/v5. Reconstructing cross(mappedNormal, tangent) here uses
				// the perturbed normal and drops mirrored-UV handedness.
				UMaterialExpressionConstant3Vector* TangentY = NewObject<UMaterialExpressionConstant3Vector>(Mat);
				TangentY->Material = Mat;
				TangentY->Constant = FLinearColor(0.f, 1.f, 0.f, 1.f);
				Mat->GetExpressionCollection().AddExpression(TangentY);
				UMaterialExpressionTransform* Bitangent = NewObject<UMaterialExpressionTransform>(Mat);
				Bitangent->Material = Mat;
				Bitangent->TransformSourceType = TRANSFORMSOURCE_Tangent;
				Bitangent->TransformType = TRANSFORM_World;
				Bitangent->Input.Expression = TangentY;
				Mat->GetExpressionCollection().AddExpression(Bitangent);
				UMaterialExpressionConstant4Vector* ST = NewObject<UMaterialExpressionConstant4Vector>(Mat);
				ST->Material = Mat;
				ST->Constant = Shadow ? Shadow->DetailMaskST : FLinearColor(1.f, 1.f, 0.f, 0.f);
				Mat->GetExpressionCollection().AddExpression(ST);
				UMaterialExpressionCustom* Offset = NewObject<UMaterialExpressionCustom>(Mat);
				Offset->Material = Mat;
				Offset->OutputType = CMOT_Float2;
				// 0072.asm.txt:345-351 also adds half the world light XY to
				// the view projection before the height offset. The readable
				// reconstruction omits this term, so follow the game DXBC.
				Offset->Code = TEXT("float3 t = normalize(Tangent); float3 b = normalize(Bitangent); float3 v = normalize(View); float2 shift = float2(dot(t,v),dot(b,v)) + 0.5*normalize(LightDir).xy; float2 unityUV = float2(UV.x, 1.0-UV.y) + (Height-1.0)*0.8*shift; return float2(unityUV.x*ST.x+ST.z, 1.0-unityUV.y*ST.y-ST.w);");
				auto OffsetInput = [Offset](const TCHAR* Name, UMaterialExpression* Expr) {
					FCustomInput& Input = Offset->Inputs.AddDefaulted_GetRef();
					Input.InputName = Name;
					Input.Input.Expression = Expr;
				};
				OffsetInput(TEXT("UV"), UV);
				OffsetInput(TEXT("Tangent"), Tangent);
				OffsetInput(TEXT("Bitangent"), Bitangent);
				OffsetInput(TEXT("View"), SkinView);
				OffsetInput(TEXT("LightDir"), Light);
				OffsetInput(TEXT("Height"), Scalar(Shadow ? Shadow->SpecHeight : .95f));
				OffsetInput(TEXT("ST"), ST);
				Mat->GetExpressionCollection().AddExpression(Offset);
				UMaterialExpressionTextureSample* Shifted = NewObject<UMaterialExpressionTextureSample>(Mat);
				Shifted->Material = Mat;
				Shifted->SamplerType = DetailTex->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
				Shifted->Texture = DetailTex;
				Shifted->Coordinates.Expression = Offset;
				Mat->GetExpressionCollection().AddExpression(Shifted);
				SpecDetail = Shifted;
				}
				AddSkinInput(TEXT("SpecDetail"), SpecDetail);
				Mat->GetExpressionCollection().AddExpression(Skin);
				ColorOut = Skin;
			}
			else if (bHairShader && DetailForShadow && ExtendForShadow)
			{
				// KKHairVertFrag.cginc:67-84 samples _HairGloss twice in UV1.
				// The second tap is 0.008 UV above the first, after applying ST.
				// Both serialized Shader Forge hair variants declare a black default
				// for _HairGloss. A null material binding still runs this pixel shader.
				UMaterialExpression* GlossSamples[2] = {};
					Mat->NumCustomizedUVs = FMath::Max(Mat->NumCustomizedUVs, 3);
					UMaterialExpressionTextureCoordinate* HairUv1 = NewObject<UMaterialExpressionTextureCoordinate>(Mat);
					HairUv1->Material = Mat;
					HairUv1->CoordinateIndex = 2;
					Mat->GetExpressionCollection().AddExpression(HairUv1);
					UMaterialExpressionCameraVectorWS* ViewDirection = NewObject<UMaterialExpressionCameraVectorWS>(Mat);
					ViewDirection->Material = Mat;
					Mat->GetExpressionCollection().AddExpression(ViewDirection);
					UMaterialExpressionConstant4Vector* GlossST = NewObject<UMaterialExpressionConstant4Vector>(Mat);
					GlossST->Material = Mat;
					GlossST->Constant = Shadow ? Shadow->HairGlossST : FLinearColor(1.f, 1.f, 0.f, 0.f);
					Mat->GetExpressionCollection().AddExpression(GlossST);
					UMaterialExpressionConstant* SpecHeightNode = NewObject<UMaterialExpressionConstant>(Mat);
					SpecHeightNode->Material = Mat;
					SpecHeightNode->R = Shadow ? Shadow->SpecHeight : .95f;
					Mat->GetExpressionCollection().AddExpression(SpecHeightNode);
					UMaterialExpressionConstant* FrontNode = NewObject<UMaterialExpressionConstant>(Mat);
					FrontNode->Material = Mat;
					FrontNode->R = Shadow && Shadow->bHairFrontProgram ? 1.f : 0.f;
					Mat->GetExpressionCollection().AddExpression(FrontNode);
					// A Custom node's direct Parameters.TangentToWorld access is
					// invisible to the material compiler's dependency tracking.
					// Unlit hair without a normal texture would omit the authored
					// tangent basis. An explicit transform requests that interpolator.
					UMaterialExpressionConstant3Vector* BitangentAxis = NewObject<UMaterialExpressionConstant3Vector>(Mat);
					BitangentAxis->Constant = FLinearColor(0, 1, 0);
					Mat->GetExpressionCollection().AddExpression(BitangentAxis);
					UMaterialExpressionTransform* HairBitangent = NewObject<UMaterialExpressionTransform>(Mat);
					HairBitangent->TransformSourceType = TRANSFORMSOURCE_Tangent;
					HairBitangent->TransformType = TRANSFORM_World;
					HairBitangent->Input.Expression = BitangentAxis;
					Mat->GetExpressionCollection().AddExpression(HairBitangent);
				for (int32 Tap = 0; Tap < 2; ++Tap)
				{
					UMaterialExpressionCustom* GlossUv = NewObject<UMaterialExpressionCustom>(Mat);
					GlossUv->Material = Mat;
					GlossUv->OutputType = CMOT_Float2;
					// main_hair VS 0004.asm.txt passes cross(N,T) * tangent.w
					// as o5. UE's signed TangentToWorld row 1 carries that basis.
					GlossUv->Code = Tap == 0
						? TEXT("float3 bitan = Bitangent; float u = UV.x + Lambert * lerp(0.005, 0.02, Front) + Front * 0.99; float unityV = 1.0 - UV.y + (SpecHeight - 1.0) * dot(normalize(View), bitan) + 0.008; return float2(u * ST.x + ST.z, 1.0 - unityV * ST.y - ST.w);")
						: TEXT("float3 bitan = Bitangent; float u = UV.x + Lambert * lerp(0.005, 0.02, Front) + Front * 0.99; float unityV = 1.0 - UV.y + (SpecHeight - 1.0) * dot(normalize(View), bitan); return float2(u * ST.x + ST.z, 1.0 - unityV * ST.y - ST.w);");
					if (Shadow && Shadow->bHairSun)
					{
						GlossUv->Code = GKoikanoHairGlossUv;
						UMaterialExpressionConstant4Vector* VertexST = NewObject<UMaterialExpressionConstant4Vector>(Mat);
						VertexST->Material = Mat;
						VertexST->Constant = Shadow->Texcoord2ST;
						Mat->GetExpressionCollection().AddExpression(VertexST);
						UMaterialExpressionConstant* OldHair = NewObject<UMaterialExpressionConstant>(Mat);
						OldHair->Material = Mat;
						OldHair->R = Shadow->bHairOld ? 1.f : 0.f;
						Mat->GetExpressionCollection().AddExpression(OldHair);
						UMaterialExpressionConstant* GlossTap = NewObject<UMaterialExpressionConstant>(Mat);
						GlossTap->Material = Mat;
						GlossTap->R = Tap == 0 ? 1.f : 0.f;
						Mat->GetExpressionCollection().AddExpression(GlossTap);
						for (const auto& Pair : { TPair<FName, UMaterialExpression*>(TEXT("VertexST"), VertexST), TPair<FName, UMaterialExpression*>(TEXT("OldHair"), OldHair), TPair<FName, UMaterialExpression*>(TEXT("Tap"), GlossTap) })
						{
							FCustomInput& Input = GlossUv->Inputs.AddDefaulted_GetRef();
							Input.InputName = Pair.Key;
							Input.Input.Expression = Pair.Value;
						}
					}
					for (const auto& Pair : { TPair<FName, UMaterialExpression*>(TEXT("UV"), HairUv1), TPair<FName, UMaterialExpression*>(TEXT("Lambert"), Lambert), TPair<FName, UMaterialExpression*>(TEXT("View"), ViewDirection), TPair<FName, UMaterialExpression*>(TEXT("ST"), GlossST), TPair<FName, UMaterialExpression*>(TEXT("SpecHeight"), SpecHeightNode), TPair<FName, UMaterialExpression*>(TEXT("Front"), FrontNode), TPair<FName, UMaterialExpression*>(TEXT("Bitangent"), HairBitangent) })
					{
						FCustomInput& Input = GlossUv->Inputs.AddDefaulted_GetRef();
						Input.InputName = Pair.Key;
						Input.Input.Expression = Pair.Value;
					}
					Mat->GetExpressionCollection().AddExpression(GlossUv);
					if (GlossTex)
					{
						UMaterialExpressionTextureSample* GlossSample = NewObject<UMaterialExpressionTextureSample>(Mat);
						GlossSample->Material = Mat;
						GlossSample->SamplerType = GlossTex->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
						GlossSample->Texture = GlossTex;
						GlossSample->Coordinates.Expression = GlossUv;
						GlossSamples[Tap] = GlossSample;
					}
					else
					{
						UMaterialExpressionConstant4Vector* BlackGloss = NewObject<UMaterialExpressionConstant4Vector>(Mat);
						BlackGloss->Material = Mat;
						BlackGloss->Constant = FLinearColor::Black;
						GlossSamples[Tap] = BlackGloss;
					}
					Mat->GetExpressionCollection().AddExpression(GlossSamples[Tap]);
				}
				// Both Shader Forge hair ForwardBase programs multiply their final
				// RGB by 1 on front faces and 0.66 on back faces (PS 0000/0001,
				// movc r4.xy from SV_IsFrontFace; mul by r4.y).
				UMaterialExpressionTwoSidedSign* HairFaceSign = NewObject<UMaterialExpressionTwoSidedSign>(Mat);
				HairFaceSign->Material = Mat;
				Mat->GetExpressionCollection().AddExpression(HairFaceSign);
				// Forge hair ForwardBase: cb1[4] is camera position, cb0[11]
				// _AnotherRamp_ST, and t6 (t7 in SHADOWS_SCREEN) _AnotherRamp.
				// Unlike _RampG, this ramp is addressed by normal dot view.
				UMaterialExpression* AnotherRamp = nullptr;
				if (Shadow && Shadow->AnotherRampTex)
				{
					UMaterialExpressionCameraVectorWS* RampView = NewObject<UMaterialExpressionCameraVectorWS>(Mat);
					RampView->Material = Mat;
					Mat->GetExpressionCollection().AddExpression(RampView);
					UMaterialExpressionConstant4Vector* AnotherST = NewObject<UMaterialExpressionConstant4Vector>(Mat);
					AnotherST->Material = Mat;
					AnotherST->Constant = Shadow->AnotherRampST;
					Mat->GetExpressionCollection().AddExpression(AnotherST);
					UMaterialExpressionCustom* AnotherUV = NewObject<UMaterialExpressionCustom>(Mat);
					AnotherUV->Material = Mat;
					AnotherUV->OutputType = CMOT_Float2;
					AnotherUV->Code = TEXT("float ndv = dot(normalize(N), normalize(V)); return float2(ndv * ST.x + ST.z, 1.0 - ndv * ST.y - ST.w);");
					for (const auto& Pair : { TPair<FName, UMaterialExpression*>(TEXT("N"), RampNormal), TPair<FName, UMaterialExpression*>(TEXT("V"), RampView), TPair<FName, UMaterialExpression*>(TEXT("ST"), AnotherST) })
					{
						FCustomInput& Input = AnotherUV->Inputs.AddDefaulted_GetRef();
						Input.InputName = Pair.Key;
						Input.Input.Expression = Pair.Value;
					}
					Mat->GetExpressionCollection().AddExpression(AnotherUV);
					UMaterialExpressionTextureSample* AnotherSample = NewObject<UMaterialExpressionTextureSample>(Mat);
					AnotherSample->Material = Mat;
					AnotherSample->SamplerType = Shadow->AnotherRampTex->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
					AnotherSample->Texture = Shadow->AnotherRampTex;
					AnotherSample->Coordinates.Expression = AnotherUV;
					Mat->GetExpressionCollection().AddExpression(AnotherSample);
					AnotherRamp = AnotherSample;
				}
				else
				{
					// Both serialized Forge hair variants default this texture to white.
					UMaterialExpressionConstant* WhiteRamp = NewObject<UMaterialExpressionConstant>(Mat);
					WhiteRamp->Material = Mat;
					WhiteRamp->R = 1.f;
					Mat->GetExpressionCollection().AddExpression(WhiteRamp);
					AnotherRamp = WhiteRamp;
				}
				// KKHairVertFrag.cginc:86-144. The two ramp red channels are
				// combined before the detail and shadow extension terms.
				UMaterialExpressionCustom* Hair = NewObject<UMaterialExpressionCustom>(Mat);
				Hair->Material = Mat;
				Hair->OutputType = CMOT_Float3;
				Hair->Code = TEXT(R"(
float3 ambientRgb = Ambient;
float depth = saturate(Depth);
float3 ambientIntensity = depth + ambientRgb - depth * ambientRgb;
float3 ambientLow = (depth + 1.0) * ambientRgb;
float3 ambient = depth > 0.0 ? ambientIntensity : ambientLow;
ambient = saturate(ambient);
float3 shadow = saturate(lerp(2.0 * ambient * ShadowColor,
    1.0 - (2.0 - 2.0 * ShadowColor) * (1.0 - ambient), step(0.5, ShadowColor)));
float glossDiff = (Gloss1.r - Gloss2.r) * 0.5;
float3 baseColor = Base.rgb;
float3 adjusted = baseColor * (1.0 + glossDiff * (shadow - 1.0));
float3 darkColor = adjusted * shadow;
float atten = saturate(min(Ramp.r, AnotherRamp.r) * Visibility);
float drawn = atten * (1.0 - Detail.g);
float3 diffuse = drawn * (adjusted - darkColor) + darkColor;
diffuse += Gloss2.r * (Ramp.r * 0.5 + 0.5);
	// main_hair ForwardBase cb0[10].x (_rimV), cb0[5].x
	// (_rimpower), the view/normal dot, and DetailMask.g/b.
// The assembly derives a hue tint from _ambientshadowG.rgb. A neutral
// colour reaches a zero-chroma division on D3D; use its limiting .33.
float3 hueTint = float3(0.33, 0.33, 0.33);
float chroma = max(ambientRgb.r, max(ambientRgb.g, ambientRgb.b))
    - min(ambientRgb.r, min(ambientRgb.g, ambientRgb.b));
if (chroma > 1e-7)
{
    float4 p = ambientRgb.g >= ambientRgb.b
        ? float4(ambientRgb.g, ambientRgb.b, -0.333333, 0.0)
        : float4(ambientRgb.b, ambientRgb.g, 0.666667, -1.0);
    float4 q = ambientRgb.r >= p.x
        ? float4(ambientRgb.r, p.y, p.w, p.x)
        : float4(p.x, p.y, p.z, ambientRgb.r);
    float h = abs(q.z + (q.w - q.y) / (6.0 * (q.x - min(q.y, q.w))));
    float3 phase = frac(abs(h) + float3(0.0, -0.333333, 0.333333));
    hueTint = 0.33 * (1.0 + saturate(abs(1.0 - 2.0 * phase) * 3.0 - 1.0));
}
float ndv = saturate(dot(normalize(N * FaceSign), normalize(View)));
float rimGate = saturate(5.0 * pow(1.0 - ndv, RimPower * 9.0 + 1.0) - 1.5);
float3 rimTint = min(hueTint * rimGate, 0.5);
diffuse = saturate(diffuse + RimStrength * (1.0 - Detail.g)
    * (1.0 - Detail.b) * rimTint);
float light = max(max(atten, 1.0 - Detail.g), 1.0 - Extend);
float faceScale = FaceSign < 0.0 ? 0.66 : 1.0;
// ForwardBase tail: max(_LightColor0.rgb * 0.6 + 0.4, _ambientshadowG.rgb).
float3 lightLift = max(float3(0.6, 0.6, 0.6) + 0.4, ambientRgb);
return diffuse * (light * (1.0 - shadow) + shadow) * faceScale * lightLift;
)");
				auto AddHairInput = [Hair](const TCHAR* Name, UMaterialExpression* Expr, int32 Output = 0) {
					FCustomInput& Input = Hair->Inputs.AddDefaulted_GetRef();
					Input.InputName = Name;
					Input.Input.Expression = Expr;
					Input.Input.OutputIndex = Output;
				};
				UMaterialExpressionConstant3Vector* AmbientNode = NewObject<UMaterialExpressionConstant3Vector>(Mat);
				AmbientNode->Material = Mat;
				AmbientNode->Constant = Shadow ? Shadow->AmbientRgb : FLinearColor(0.019f, 0.027f, 0.039f, 1.f);
				Mat->GetExpressionCollection().AddExpression(AmbientNode);
				UMaterialExpressionConstant* DepthNode = NewObject<UMaterialExpressionConstant>(Mat);
				DepthNode->Material = Mat;
				DepthNode->R = Shadow ? Shadow->AmbientAlpha : 0.f;
				Mat->GetExpressionCollection().AddExpression(DepthNode);
				AddHairInput(TEXT("Base"), ColorOut);
				AddHairInput(TEXT("Ramp"), Ramp);
				AddHairInput(TEXT("AnotherRamp"), AnotherRamp);
				AddHairInput(TEXT("Visibility"), HairVisibility);
				AddHairInput(TEXT("Detail"), DetailForShadow);
				AddHairInput(TEXT("Gloss1"), GlossSamples[0]);
				AddHairInput(TEXT("Gloss2"), GlossSamples[1]);
				AddHairInput(TEXT("ShadowColor"), ShadowColor);
				AddHairInput(TEXT("Extend"), ExtendForShadow);
				AddHairInput(TEXT("Ambient"), AmbientNode);
				AddHairInput(TEXT("Depth"), DepthNode);
				AddHairInput(TEXT("FaceSign"), HairFaceSign);
				UMaterialExpressionConstant* RimNode = NewObject<UMaterialExpressionConstant>(Mat);
				RimNode->Material = Mat;
				RimNode->R = Shadow ? Shadow->HairRimStrength : 0.75f;
				Mat->GetExpressionCollection().AddExpression(RimNode);
				AddHairInput(TEXT("RimStrength"), RimNode);
				UMaterialExpressionConstant* RimPowerNode = NewObject<UMaterialExpressionConstant>(Mat);
				RimPowerNode->Material = Mat;
				RimPowerNode->R = Shadow ? Shadow->HairRimPower : 0.2f;
				Mat->GetExpressionCollection().AddExpression(RimPowerNode);
				AddHairInput(TEXT("RimPower"), RimPowerNode);
				UMaterialExpressionCameraVectorWS* HairView = NewObject<UMaterialExpressionCameraVectorWS>(Mat);
				HairView->Material = Mat;
				Mat->GetExpressionCollection().AddExpression(HairView);
				AddHairInput(TEXT("View"), HairView);
				AddHairInput(TEXT("N"), RampNormal);
				Mat->GetExpressionCollection().AddExpression(Hair);
				ColorOut = Hair;
				// Read-only render diagnostic: isolate the actual sampled gloss,
				// without changing the source material or its runtime defaults.
				if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_HAIR_AUDIT")) == TEXT("gloss"))
					ColorOut = GlossSamples[1];
			}
			else
			{
				UMaterialExpressionMultiply* Lit = NewObject<UMaterialExpressionMultiply>(Mat);
				Lit->Material = Mat;
				Lit->A.Expression = ColorOut;
				Lit->B.Expression = ShadowMix;
				Mat->GetExpressionCollection().AddExpression(Lit);
				ColorOut = Lit;
			}
		}
		// Maker CustomScene's measured interpolated light probe is blue above,
		// neutral below, and identical in the four horizontal directions. Add
		// that scene irradiance to this IBL pass; the grey UE backdrop does not
		// contain the game's probe. Values come from runtime_maker_lighting.txt.
		if (ShaderKind == 6)
		{
			UMaterialExpressionPixelNormalWS* WorldNormal = NewObject<UMaterialExpressionPixelNormalWS>(Mat);
			WorldNormal->Material = Mat;
			Mat->GetExpressionCollection().AddExpression(WorldNormal);
			UMaterialExpressionCustom* ProbeAmbient = NewObject<UMaterialExpressionCustom>(Mat);
			ProbeAmbient->Material = Mat;
			ProbeAmbient->OutputType = CMOT_Float3;
			ProbeAmbient->Code = TEXT(R"(
float z = clamp(Normal.z, -1.0, 1.0);
float3 up = float3(0.033036530, 0.142879700, 0.473815700);
float3 down = float3(0.194466500, 0.157451100, 0.170697800);
float3 side = float3(0.075220380, 0.176605100, 0.320116400);
float3 irradiance = side + 0.5 * (up - down) * z
    + (0.5 * (up + down) - side) * z * z;
// IBL_Shader_alpha PS 0003 converts the evaluated SH with the same
// log * (1/2.4) / exp / 1.055 - .055 sequence before diffuse lighting.
float3 sh = max(1.055 * pow(max(irradiance, 0.0), 0.416667) - 0.055, 0.0);
return Base.rgb * sh;
)");
			FCustomInput& BaseInput = ProbeAmbient->Inputs.AddDefaulted_GetRef();
			BaseInput.InputName = TEXT("Base");
			BaseInput.Input.Expression = ColorOut;
			FCustomInput& NormalInput = ProbeAmbient->Inputs.AddDefaulted_GetRef();
			NormalInput.InputName = TEXT("Normal");
			NormalInput.Input.Expression = WorldNormal;
			Mat->GetExpressionCollection().AddExpression(ProbeAmbient);
			Ed->EmissiveColor.Expression = ProbeAmbient;
		}
		else if (bAdditive && Shadow && Shadow->bStudioAddProgram)
		{
			// Studio's light effects (Shader Forge/main_item_studio_add, installed pixel program in abdata/studio/mat/00):
			// the texture's channels are three layered masks, each tinting one item colour, and the brightest wins (in
			// display values): max(saturate(r - max(g,b)^8) _Color, saturate(g - b^8) _Color2, b _Color3) x _alpha.
			UMaterialExpressionCustom* Effect = NewObject<UMaterialExpressionCustom>(Mat);
			Effect->Material = Mat;
			Effect->OutputType = CMOT_Float3;
			Effect->Inputs.Empty();
			FCustomInput& TexIn = Effect->Inputs.AddDefaulted_GetRef();
			TexIn.InputName = TEXT("Tex");
			TexIn.Input.Expression = ColorOut;
			auto Gamma = [](const FLinearColor& C) { return FLinearColor(FMath::Pow(FMath::Max(C.R, 0.f), 1.f / 2.2f), FMath::Pow(FMath::Max(C.G, 0.f), 1.f / 2.2f), FMath::Pow(FMath::Max(C.B, 0.f), 1.f / 2.2f), 1.f); };
			const FLinearColor C1 = Gamma(Shadow ? Shadow->ItemColors[0] : FLinearColor::White);
			const FLinearColor C2 = Gamma(Shadow ? Shadow->ItemColors[1] : FLinearColor::White);
			const FLinearColor C3 = Gamma(Shadow ? Shadow->ItemColors[2] : FLinearColor::White);
			Effect->Code = FString::Printf(TEXT("float3 t = pow(max(Tex.rgb, 0.0), 1.0 / 2.2);\n")
				TEXT("float m = max(t.b, t.g); m *= m; m *= m;\n")
				TEXT("float3 c1 = saturate(t.r - m * m) * float3(%s, %s, %s);\n")
				TEXT("float b4 = t.b * t.b; b4 *= b4;\n")
				TEXT("float3 c2 = saturate(t.g - b4 * b4) * float3(%s, %s, %s);\n")
				TEXT("float3 c3 = t.b * float3(%s, %s, %s);\n")
				TEXT("return pow(max(max(c3, max(c1, c2)) * %s, 0.0), 2.2);"),
				*FString::SanitizeFloat(C1.R), *FString::SanitizeFloat(C1.G), *FString::SanitizeFloat(C1.B),
				*FString::SanitizeFloat(C2.R), *FString::SanitizeFloat(C2.G), *FString::SanitizeFloat(C2.B),
				*FString::SanitizeFloat(C3.R), *FString::SanitizeFloat(C3.G), *FString::SanitizeFloat(C3.B),
				*FString::SanitizeFloat(Shadow ? Shadow->ItemAlpha : 1.f));
			Mat->GetExpressionCollection().AddExpression(Effect);
			Ed->EmissiveColor.Expression = Effect;
		}
		else if (!(Shadow && Shadow->bStandardLinear))
		{
			// The generic preview carries an unlit albedo floor. Standard already
			// gets indirect diffuse from its baked lightmap and specular from its
			// probe; an extra albedo emission lights every wall twice.
			Ed->EmissiveColor.Expression = ColorOut;
		}
		Ed->BaseColor.Expression = ColorOut;
		if (ShaderKind == 6)
		{
			UMaterialExpressionConstant* Metallic = NewObject<UMaterialExpressionConstant>(Mat);
			Metallic->Material = Mat;
			Metallic->R = FMath::Clamp(Shadow ? Shadow->Metallic : 0.f, 0.f, 1.f);
			Mat->GetExpressionCollection().AddExpression(Metallic);
			Ed->Metallic.Expression = Metallic;
			UMaterialExpressionConstant* Roughness = NewObject<UMaterialExpressionConstant>(Mat);
			Roughness->Material = Mat;
			Roughness->R = 1.f - FMath::Clamp(Shadow ? Shadow->Smoothness : 0.5f, 0.f, 1.f);
			Mat->GetExpressionCollection().AddExpression(Roughness);
			Ed->Roughness.Expression = Roughness;
		}
		// Skin overlays are now composited in this same surface shader. The
		// former 1 cm separation for an extra overlay mesh exposes the reverse
		// outline shell through the surface at folds and thin fingers.
		if (bPdo && !bBodyOverlay)
		{
			UMaterialExpressionConstant* Pdo = NewObject<UMaterialExpressionConstant>(Mat);
			Pdo->Material = Mat;
			Pdo->R = 1.0f;
			Mat->GetExpressionCollection().AddExpression(Pdo);
			Ed->PixelDepthOffset.Expression = Pdo;
		}
		if (bMasked)
		{
			if (bSkinShader && AlphaMaskTex)
			{
				// Koikano/main_skin 0072:105-109 samples _AlphaMask at
				// UV0, then clips min(max(r,1-alpha_a),max(g,1-alpha_b))
				// against _Cutoff. The extractor whitens disabled channels.
				// Shader Forge/main_skin (the face, kind 7) clips the same
				// way: a card's face mask cuts the scalp under its hair.
				auto* Mask = NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat);
				Mask->ParameterName=TEXT("_AlphaMask");
				Mask->Material = Mat;
				Mask->SamplerType = SAMPLERTYPE_LinearColor;
				Mask->Texture = AlphaMaskTex;
				Mask->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->AlphaMaskST : FLinearColor(1.f, 1.f, 0.f, 0.f));
				Mat->GetExpressionCollection().AddExpression(Mask);
				UMaterialExpressionMin* Combined = NewObject<UMaterialExpressionMin>(Mat);
				Combined->Material = Mat;
				Combined->A.Expression = Mask;
				Combined->A.OutputIndex = 1;
				Combined->B.Expression = Mask;
				Combined->B.OutputIndex = 2;
				Mat->GetExpressionCollection().AddExpression(Combined);
				Ed->OpacityMask.Expression = Combined;
			}
			else if (bHairShader && AlphaMaskTex)
			{
				UMaterialExpressionTextureSample* Mask = NewObject<UMaterialExpressionTextureSample>(Mat);
				Mask->Material = Mat;
				Mask->SamplerType = SAMPLERTYPE_LinearColor;
				Mask->Texture = AlphaMaskTex;
				Mask->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->AlphaMaskST : FLinearColor(1.f, 1.f, 0.f, 0.f));
				Mat->GetExpressionCollection().AddExpression(Mask);
				UMaterialExpressionMultiply* Clip = NewObject<UMaterialExpressionMultiply>(Mat);
				Clip->Material = Mat;
				Clip->A.Expression = Samp;
				Clip->A.OutputIndex = 4;
				Clip->B.Expression = Mask;
				Clip->B.OutputIndex = 1;
				Mat->GetExpressionCollection().AddExpression(Clip);
				Ed->OpacityMask.Expression = Clip;
			}
			else
			{
				Ed->OpacityMask.Expression = Samp;
				Ed->OpacityMask.OutputIndex = 4;
			}
		}
		if (bTranslucent || bEyeNoZ)
		{
			Ed->Opacity.Expression = EyeOpacity ? EyeOpacity : Samp;
			Ed->Opacity.OutputIndex = EyeOpacity ? 0 : 4;
			if (Shadow && Shadow->ItemAlpha < 1.f && !EyeOpacity)
			{
				UMaterialExpressionConstant* Fade = NewObject<UMaterialExpressionConstant>(Mat);
				Fade->Material = Mat;
				Fade->R = Shadow->ItemAlpha;
				Mat->GetExpressionCollection().AddExpression(Fade);
				UMaterialExpressionMultiply* Faded = NewObject<UMaterialExpressionMultiply>(Mat);
				Faded->Material = Mat;
				Faded->A.Expression = Samp;
				Faded->A.OutputIndex = 4;
				Faded->B.Expression = Fade;
				Mat->GetExpressionCollection().AddExpression(Faded);
				Ed->Opacity.Expression = Faded;
				Ed->Opacity.OutputIndex = 0;
			}
		}
		// Hair `_HairGloss`: use its red channel as a specular mask with a low
		// roughness so the hair reads glossy like the game's matcap sheen.
		if (GlossTex && !bUnlit)
		{
			UMaterialExpressionTextureSample* G = NewObject<UMaterialExpressionTextureSample>(Mat);
			G->Material = Mat;
			G->SamplerType = GlossTex->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
			G->ConstCoordinate = 0;
			G->Texture = GlossTex;
			Mat->GetExpressionCollection().AddExpression(G);
			UMaterialExpressionConstant* K = NewObject<UMaterialExpressionConstant>(Mat);
			K->Material = Mat;
			K->R = 0.12f;
			Mat->GetExpressionCollection().AddExpression(K);
			Ed->Specular.Expression = G;
			Ed->Specular.OutputIndex = 1;
			Ed->Roughness.Expression = K;
		}
	}

	Mat->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
	Mat->SetUsageByFlag(MATUSAGE_StaticMesh, true);
	Mat->SetUsageByFlag(MATUSAGE_MorphTargets, true);
	if (Shadow && (Shadow->EmissionColor.R + Shadow->EmissionColor.G + Shadow->EmissionColor.B) > 0.f)
	{
		if (UMaterialEditorOnlyData* EmitEd = Mat->GetEditorOnlyData())
		{
			UMaterialExpressionConstant3Vector* EmitColor = NewObject<UMaterialExpressionConstant3Vector>(Mat);
			EmitColor->Material = Mat;
			EmitColor->Constant = Shadow->EmissionColor;
			Mat->GetExpressionCollection().AddExpression(EmitColor);
			UMaterialExpression* Emission = EmitColor;
			if (Shadow->EmissionTex)
			{
				UMaterialExpressionTextureSample* EmitMap = NewObject<UMaterialExpressionTextureSample>(Mat);
				EmitMap->Material = Mat;
				EmitMap->Texture = Shadow->EmissionTex;
				EmitMap->SamplerType = Shadow->EmissionTex->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
				Mat->GetExpressionCollection().AddExpression(EmitMap);
				UMaterialExpressionMultiply* EmitMul = NewObject<UMaterialExpressionMultiply>(Mat);
				EmitMul->Material = Mat;
				EmitMul->A.Expression = EmitColor;
				EmitMul->B.Expression = EmitMap;
				Mat->GetExpressionCollection().AddExpression(EmitMul);
				Emission = EmitMul;
			}
			if (EmitEd->EmissiveColor.Expression)
			{
				UMaterialExpressionAdd* EmitAdd = NewObject<UMaterialExpressionAdd>(Mat);
				EmitAdd->Material = Mat;
				EmitAdd->A.Expression = EmitEd->EmissiveColor.Expression;
				EmitAdd->A.OutputIndex = EmitEd->EmissiveColor.OutputIndex;
				EmitAdd->B.Expression = Emission;
				Mat->GetExpressionCollection().AddExpression(EmitAdd);
				EmitEd->EmissiveColor.Expression = EmitAdd;
				EmitEd->EmissiveColor.OutputIndex = 0;
			}
			else EmitEd->EmissiveColor.Expression = Emission;
		}
	}
	if (Shadow && (Shadow->LightmapTex || Shadow->ReflectionTex || Shadow->bStandardLinear))
	{
		// Installed Unity Standard uses linear indirect light: the colour lightmap is sRGB, its RGBM alpha decodes with
		// (5^2.2,2.2); indirect diffuse multiplies albedo and oneMinusReflectivity (0.96 for dielectric).
		// - baked reflection probe (UnityStandardBRDF BRDF1 indirect specular): surfaceReduction x probe(reflection,
		//   perceptualRoughness mip) x FresnelLerp(specColor, grazingTerm, n.v), specColor = lerp(dielectric 0.04, albedo,
		//   metallic). The probe is the cubemap atlas (okmodel_view::assign_reflection_probes): faces +X..-Z side by side,
		//   mip k at row size x (2 - 2^(1-k)); directions turn from Unreal (x, y, z) to Unity (-x, z, y).
		if (UMaterialEditorOnlyData* LmEd = Mat->GetEditorOnlyData(); LmEd && LmEd->BaseColor.Expression)
		{
			UMaterialExpressionCustom* Decode = NewObject<UMaterialExpressionCustom>(Mat);
			Decode->Material = Mat;
			Decode->OutputType = CMOT_Float3;
			Decode->Inputs.Empty();
			auto Input = [&](const TCHAR* Name, UMaterialExpression* E, int32 Output = 0)
			{
				E->Material = Mat;
				Mat->GetExpressionCollection().AddExpression(E);
				FCustomInput& In = Decode->Inputs.AddDefaulted_GetRef();
				In.InputName = Name;
				In.Input.Expression = E;
				In.Input.OutputIndex = Output;
			};
			FCustomInput AlbedoIn; AlbedoIn.InputName = TEXT("Albedo"); AlbedoIn.Input.Expression = LmEd->BaseColor.Expression; AlbedoIn.Input.OutputIndex = LmEd->BaseColor.OutputIndex; Decode->Inputs.Add(AlbedoIn);
			const bool bMetalMap = Shadow->LightmapMetallicTex != nullptr;
			UMaterialExpressionTextureSample* Metal = nullptr;
			if (bMetalMap)
			{
				Metal = NewObject<UMaterialExpressionTextureSample>(Mat);
				Metal->Texture = Shadow->LightmapMetallicTex;
				Metal->SamplerType = SAMPLERTYPE_LinearColor;
				Metal->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow->MainTexST);
				Input(TEXT("Metal"), Metal, 5);
			}
			const FString Metallic = bMetalMap ? FString(TEXT("Metal.r")) : FString::SanitizeFloat(Shadow->StandardMetallic);
			const bool bStandardLinear = Shadow->bStandardLinear;
			if (bStandardLinear)
			{
				// The map sun also needs the authored Standard metallic/smoothness surface.
				if (Metal)
				{
					LmEd->Metallic.Expression = Metal;
					LmEd->Metallic.OutputIndex = 1;
				}
				else
				{
					auto* MetallicValue = NewObject<UMaterialExpressionConstant>(Mat);
					MetallicValue->R = Shadow->StandardMetallic;
					Mat->GetExpressionCollection().AddExpression(MetallicValue);
					LmEd->Metallic.Expression = MetallicValue;
				}
				auto* Roughness = NewObject<UMaterialExpressionCustom>(Mat);
				Roughness->OutputType = CMOT_Float1;
				Roughness->Code = TEXT("return 1.0 - saturate(") + FString::SanitizeFloat(Shadow->StandardSmoothness)
					+ (Shadow->bStandardSmoothnessFromMap ? TEXT(" * Metal.a);") : TEXT(");"));
				if (Metal)
				{
					FCustomInput In; In.InputName = TEXT("Metal"); In.Input.Expression = Metal; In.Input.OutputIndex = 5;
					Roughness->Inputs.Add(In);
				}
				Mat->GetExpressionCollection().AddExpression(Roughness);
				LmEd->Roughness.Expression = Roughness;
			}
			FString Code = bStandardLinear ? TEXT("float3 a = max(Albedo, 0.0);\nfloat3 total = 0;\n")
				: TEXT("float3 a = pow(max(Albedo, 0.0), 1.0 / 2.2);\nfloat3 total = 0;\n");
			Code += FString::Printf(TEXT("float metallic = %s;\n"), *Metallic);
			if (Shadow->LightmapTex)
			{
				UMaterialExpressionTextureSample* LmSample = NewObject<UMaterialExpressionTextureSample>(Mat);
				LmSample->Texture = Shadow->LightmapTex;
				LmSample->SamplerType = SAMPLERTYPE_LinearColor;
				LmSample->ConstCoordinate = 1;
				Input(TEXT("Lm"), LmSample, 5); // RGBA output
				FString DirectionFactor;
				if (Shadow->LightmapDirectionTex)
				{
					UMaterialExpressionTextureSample* Direction = NewObject<UMaterialExpressionTextureSample>(Mat);
					Direction->Texture = Shadow->LightmapDirectionTex;
					Direction->SamplerType = SAMPLERTYPE_LinearColor;
					Direction->ConstCoordinate = 1;
					Input(TEXT("LmDirection"), Direction, 5);
					Input(TEXT("LmNormal"), NewObject<UMaterialExpressionPixelNormalWS>(Mat));
					// Installed Standard PS 112 (DIRLIGHTMAP_COMBINED): hemisphere
					// dot product and the baked directionality rebalancing divisor.
					Code += TEXT("float3 lmN = normalize(float3(-LmNormal.x, LmNormal.z, LmNormal.y));\nfloat lmDirection = (dot(lmN, LmDirection.rgb - 0.5) + 0.5) / max(LmDirection.a, 1e-4);\n");
					DirectionFactor = TEXT(" * lmDirection");
				}
				// Installed Standard uses linear light, an sRGB colour lightmap sampler,
				// and RGBM DecodeLightmap with unity_Lightmap_HDR=(5^2.2,2.2).
				if (bStandardLinear)
					Code += TEXT("float3 lmRgb = lerp(Lm.rgb / 12.92, pow((Lm.rgb + 0.055) / 1.055, 2.4), step(0.04045, Lm.rgb));\ntotal += pow(5.0 * Lm.a, 2.2) * lmRgb * a * ");
				else Code += TEXT("total += 5.0 * Lm.a * Lm.rgb * a * ");
				Code += FString::SanitizeFloat(Shadow->LightmapDiffuse) + (bMetalMap ? TEXT(" * (1.0 - Metal.r)") : TEXT("")) + DirectionFactor + TEXT(";\n");
			}
            if (bStandardLinear && !Shadow->LightmapTex)
            {
                FCustomInput Probe;Probe.InputName=TEXT("ProbeDiffuse");Probe.Input.Expression=OKMakeRuntimeProbeAmbient(Mat);Decode->Inputs.Add(Probe);
                Code += TEXT("total += ProbeDiffuse * a * ") + FString::SanitizeFloat(Shadow->LightmapDiffuse)
                    + (bMetalMap ? TEXT(" * (1.0 - Metal.r);\n") : TEXT(";\n"));
            }
			if (Shadow->ReflectionTex || Shadow->bNativeReflection)
			{
                if(Shadow->bNativeReflection)
                {
                    static TStrongObjectPtr<UTextureCube> BlackDefaultCube;
                    if(!BlackDefaultCube.IsValid())
                    {
                        auto* Black=NewObject<UTextureCube>();uint8 Pixels[24]={};Black->Source.Init(1,1,6,1,TSF_BGRA8,Pixels);Black->SRGB=false;Black->PostEditChange();BlackDefaultCube.Reset(Black);
                    }
                    UTextureCube* Cube=BlackDefaultCube.Get();
                    auto* Parameter=NewObject<UMaterialExpressionTextureObjectParameter>(Mat);Parameter->ParameterName=TEXT("unity_SpecCube0");Parameter->Texture=Cube;Parameter->SamplerType=SAMPLERTYPE_LinearColor;Input(TEXT("NativeCube"),Parameter);
                    auto* HDR=NewObject<UMaterialExpressionVectorParameter>(Mat);HDR->ParameterName=TEXT("unity_SpecCube0_HDR");HDR->DefaultValue=FLinearColor(1,1,0,0);Input(TEXT("NativeHDR"),HDR);
                }
                else
                {
                    auto* Atlas=NewObject<UMaterialExpressionTextureObject>(Mat);Atlas->Texture=Shadow->ReflectionTex;Atlas->SamplerType=SAMPLERTYPE_LinearColor;Input(TEXT("Atlas"),Atlas);
                }
				Input(TEXT("Position"), NewObject<UMaterialExpressionWorldPosition>(Mat));
				Input(TEXT("Camera"), NewObject<UMaterialExpressionCameraPositionWS>(Mat));
				Input(TEXT("Normal"), NewObject<UMaterialExpressionPixelNormalWS>(Mat));
				const float Size = Shadow->ReflectionTex ? float(Shadow->ReflectionTex->GetSizeX()) / 6.f : 128.f;
				Code += FString::Printf(TEXT("float smooth = %s;\nfloat Size = %s;\nfloat Mips = %s;\nfloat Intensity = %s;\n"),
					*(FString::SanitizeFloat(Shadow->StandardSmoothness) + (Shadow->bStandardSmoothnessFromMap ? TEXT(" * Metal.a") : TEXT(""))),
					*FString::SanitizeFloat(Size), *FString::SanitizeFloat(Shadow->ReflectionMips), *FString::SanitizeFloat(Shadow->ReflectionIntensity));
				FString ReflectionCode = TEXT(R"(float3 V = normalize(Camera - Position);
float3 N = normalize(Normal);
float3 Rw = reflect(-V, N);
float3 d = float3(-Rw.x, Rw.z, Rw.y);
float3 ad = abs(d);
float face; float2 st; float ma;
if (ad.x >= ad.y && ad.x >= ad.z) { ma = ad.x; face = d.x > 0 ? 0 : 1; st = d.x > 0 ? float2(-d.z, -d.y) : float2(d.z, -d.y); }
else if (ad.y >= ad.z) { ma = ad.y; face = d.y > 0 ? 2 : 3; st = d.y > 0 ? float2(d.x, d.z) : float2(d.x, -d.z); }
else { ma = ad.z; face = d.z > 0 ? 4 : 5; st = d.z > 0 ? float2(d.x, -d.y) : float2(-d.x, -d.y); }
st = st / max(ma, 1e-5) * 0.5 + 0.5;
float pr = 1.0 - smooth;
float lod = min(pr * (1.7 - 0.7 * pr) * 6.0, Mips - 1.0);
float3 env = 0;
for (int k = 0; k < 2; ++k)
{
	float l = min(floor(lod) + k, Mips - 1.0);
	float w = max(Size / exp2(l), 1.0);
	float row = Size * (2.0 - 2.0 / exp2(l));
	float2 p = clamp(st * w, 0.5, w - 0.5);
	float2 auv = float2((face * Size + p.x) / (6.0 * Size), (row + p.y) / (2.0 * Size));
	env += (k == 0 ? 1.0 - frac(lod) : frac(lod)) * Texture2DSampleLevel(Atlas, AtlasSampler, auv, 0).rgb;
}
float3 specColor = lerp(float3(0.04, 0.04, 0.04), a, metallic);
float omr = 0.96 * (1.0 - metallic);
float roughness = max(pr * pr, 0.002);
float surfaceReduction = 1.0 / (1.0 + roughness * roughness);
float grazing = saturate(smooth + (1.0 - omr));
float nv = saturate(abs(dot(N, V)));
total += surfaceReduction * env * Intensity * lerp(specColor, grazing.xxx, pow(1.0 - nv, 5.0));
)");
                if(Shadow->bNativeReflection)
                {
                    const int32 Begin=ReflectionCode.Find(TEXT("float3 ad = abs(d);"));
                    const int32 End=ReflectionCode.Find(TEXT("float3 specColor ="));
                    ReflectionCode=ReflectionCode.Left(Begin)+TEXT("float pr=1.0-smooth;float lod=pr*(1.7-.7*pr)*6.0;float3 env=TextureCubeSampleLevel(NativeCube,NativeCubeSampler,d,lod).rgb;Intensity=NativeHDR.r;\n")+ReflectionCode.Mid(End);
                }
                Code+=ReflectionCode;
			}
			Decode->Code = Code + (bStandardLinear ? TEXT("return max(total, 0.0);") : TEXT("return pow(max(total, 0.0), 2.2);"));
			Mat->GetExpressionCollection().AddExpression(Decode);
			if (LmEd->EmissiveColor.Expression)
			{
				UMaterialExpressionAdd* LmAdd = NewObject<UMaterialExpressionAdd>(Mat);
				LmAdd->Material = Mat;
				LmAdd->A.Expression = LmEd->EmissiveColor.Expression;
				LmAdd->A.OutputIndex = LmEd->EmissiveColor.OutputIndex;
				LmAdd->B.Expression = Decode;
				Mat->GetExpressionCollection().AddExpression(LmAdd);
				LmEd->EmissiveColor.Expression = LmAdd;
				LmEd->EmissiveColor.OutputIndex = 0;
			}
			else LmEd->EmissiveColor.Expression = Decode;
		}
	}
	// An equal graph finalized before (OK_MATERIAL_DEDUPE=1) is used as is.
	FString DedupeKey;
	if (UMaterialInterface* Same = OKFindEqualMaterialGraph(Mat, FString::Printf(TEXT("part|noz=%d"), bEyeNoZ ? 1 : 0), DedupeKey, GetTransientPackage())) return Same;
	Mat->UpdateCachedExpressionData();
	const double FinalizeStart = GOkCardLoadProfile.bActive ? FPlatformTime::Seconds() : 0.0;
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	OKEnsureSkeletalMaterialUsage(Mat);
	if (GOkCardLoadProfile.bActive) GOkCardLoadProfile.MaterialFinalizeSeconds += FPlatformTime::Seconds() - FinalizeStart;
	if (bEyeNoZ)
	{
		Mat->BlendMode = BLEND_Translucent;
		Mat->bDisableDepthTest = true;
		Mat->TranslucencyPass = MTP_AfterDOF;
	}
	OKRememberMaterial(DedupeKey, Mat);
	UE_LOG(LogTemp, Display, TEXT("OK part mat tex=%s blend=%s unlit=%d"),
		Tex ? *Tex->GetName() : TEXT("none"), *Blend, bUnlit ? 1 : 0);
	return Mat;

#else
return nullptr; // Editor graph creation is not a runtime rendering path.
#endif
}

// Unity's outline pass uses the same MainTex alpha clip and the material's
// _LineColor. Hair can have many overlapping strands; a shared black opaque
// shell paints their entire interiors black.
static UMaterialInterface* MakeOutlineMaterial(UTexture2D* Tex, const FLinearColor& LineColor, bool bHairShader, bool bSkinShader = false, float LineWidth = 0.f, UTexture2D* DetailTex = nullptr, const FOKToonShadowParameters* Shadow = nullptr, UTexture2D* AlphaMaskTex = nullptr, bool bKoikanoClothes = false)
{
#if WITH_EDITOR
	// A part on a shared parent outlines with that parent's _Outline (none where the family has no outline layout).
	if(Shadow&&Shadow->CookedMaterial&&OKSharedParents())return Shadow->CookedOutline;

	FOkCardProfileScope Profile(GOkCardLoadProfile.bActive ? &GOkCardLoadProfile.MaterialSeconds : nullptr);
	if (GOkCardLoadProfile.bActive) ++GOkCardLoadProfile.MaterialCount;
	if (Shadow && Shadow->NativeItem) return OKMakeMainItemPlusMaterial(*Shadow->NativeItem,true);
	UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!Mat)
	{
		return nullptr;
	}
	Mat->bIsPreviewMaterial = true;
	Mat->MaterialDomain = MD_Surface;
	Mat->BlendMode = Tex ? BLEND_Masked : BLEND_Opaque;
	if (bSkinShader || bKoikanoClothes) { Mat->OpacityMaskClipValue = 0.5f; }
	Mat->SetShadingModel(MSM_Unlit);
	Mat->TwoSided = false;
	Mat->SetUsageByFlag(MATUSAGE_StaticMesh, true);
	Mat->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
	UMaterialExpressionConstant3Vector* C = NewObject<UMaterialExpressionConstant3Vector>(Mat);
	C->Material = Mat;
	C->Constant = FLinearColor(
		FMath::Pow(FMath::Clamp(LineColor.R, 0.f, 1.f), 2.2f),
		FMath::Pow(FMath::Clamp(LineColor.G, 0.f, 1.f), 2.2f),
		FMath::Pow(FMath::Clamp(LineColor.B, 0.f, 1.f), 2.2f), 1.f);
	Mat->GetExpressionCollection().AddExpression(C);
	if (UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData())
	{
		if (bSkinShader || bHairShader || bKoikanoClothes)
		{
			// Source DXBC differs by shader family: Koikano skin uses (.1*d+.6)
			// *.003*_LineWidthS; Forge skin/hair use (.1*d+.3)*.005. DetailMask
			// is sampled at vertex LOD0 with its own ST, not nearest/8-bit coverage.
			UMaterialExpressionWorldPosition* Position = NewObject<UMaterialExpressionWorldPosition>(Mat);
			Position->Material = Mat;
			Mat->GetExpressionCollection().AddExpression(Position);
			UMaterialExpressionCameraPositionWS* Camera = NewObject<UMaterialExpressionCameraPositionWS>(Mat);
			Camera->Material = Mat;
			Mat->GetExpressionCollection().AddExpression(Camera);
			UMaterialExpressionVertexNormalWS* Normal = NewObject<UMaterialExpressionVertexNormalWS>(Mat);
			Normal->Material = Mat;
			Mat->GetExpressionCollection().AddExpression(Normal);
			UMaterialExpressionConstant* OneCoverage = NewObject<UMaterialExpressionConstant>(Mat);
			OneCoverage->Material = Mat;
			OneCoverage->R = 1.f;
			Mat->GetExpressionCollection().AddExpression(OneCoverage);
			UMaterialExpression* Coverage = OneCoverage;
			if (DetailTex)
			{
				UMaterialExpressionTextureSample* Detail = NewObject<UMaterialExpressionTextureSample>(Mat);
				Detail->Material = Mat;
				Detail->SamplerType = DetailTex->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
				Detail->Texture = DetailTex;
				Detail->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->DetailMaskST : FLinearColor(1.f, 1.f, 0.f, 0.f));
				Detail->MipValueMode = TMVM_MipLevel;
				UMaterialExpressionConstant* Mip = NewObject<UMaterialExpressionConstant>(Mat);
				Mip->Material = Mat;
				Mip->R = 0.f;
				Mat->GetExpressionCollection().AddExpression(Mip);
				Detail->MipValue.Expression = Mip;
				Mat->GetExpressionCollection().AddExpression(Detail);
				UMaterialExpressionSubtract* Gate = NewObject<UMaterialExpressionSubtract>(Mat);
				Gate->Material = Mat;
				Gate->A.Expression = OneCoverage;
				Gate->B.Expression = Detail;
				Gate->B.OutputIndex = 3; // DetailMask.b, a numeric linear mask.
				Mat->GetExpressionCollection().AddExpression(Gate);
				Coverage = Gate;
			}
			UMaterialExpressionConstant* Width = NewObject<UMaterialExpressionConstant>(Mat);
			Width->Material = Mat;
			// co_bot_01/main_clothes_opaque Outline VS 0000 shares Koikano's
			// distance, DetailMask.b, global width and _LineWidthS factors.
			const bool bKoikano = bKoikanoClothes || (bSkinShader && Shadow && Shadow->bKoikanoSkinOutline);
			Width->R = LineWidth * (bKoikano ? Shadow->SkinLineWidth : 1.f);
			Mat->GetExpressionCollection().AddExpression(Width);
			UMaterialExpressionCustom* Expand = NewObject<UMaterialExpressionCustom>(Mat);
			Expand->Material = Mat;
			Expand->OutputType = CMOT_Float3;
			// main_hair Outline VS blob 0 multiplies the extrusion by
			// _AlphaMask.r * _MainTex.a before the DetailMask.b gate. The
			// default AlphaMask is white for these materials.
			Expand->Code = bKoikano
				? TEXT("float d = distance(Position, Camera); return normalize(Normal) * ((d * 0.001 + 0.6) * Width * 0.3 * Coverage * Alpha);")
				: TEXT("float d = distance(Position, Camera); return normalize(Normal) * ((d * 0.001 + 0.3) * Width * 0.5 * Coverage * Alpha);");
			UE_LOG(LogTemp, Display, TEXT("OK outline source family=%s width_global=%.6f width_material=%.6f detail_lod0=%d"),
				bKoikanoClothes ? TEXT("KoikanoClothes") : (bKoikano ? TEXT("KoikanoSkin") : (bSkinShader ? TEXT("ForgeSkin") : TEXT("Hair"))),
				LineWidth, bKoikano ? Shadow->SkinLineWidth : 1.f, DetailTex ? 1 : 0);
			auto AddInput = [Expand](const TCHAR* Name, UMaterialExpression* Expr, int32 Output = 0) {
				FCustomInput& Input = Expand->Inputs.AddDefaulted_GetRef();
				Input.InputName = Name;
				Input.Input.Expression = Expr;
				Input.Input.OutputIndex = Output;
			};
			AddInput(TEXT("Position"), Position);
			AddInput(TEXT("Camera"), Camera);
			AddInput(TEXT("Normal"), Normal);
			AddInput(TEXT("Coverage"), Coverage);
			AddInput(TEXT("Width"), Width);
			UMaterialExpression* Alpha = nullptr;
			int32 AlphaOutput = 0;
			if (bHairShader && Tex)
			{
				UMaterialExpressionTextureSample* MainAlpha = NewObject<UMaterialExpressionTextureSample>(Mat);
				MainAlpha->Material = Mat;
				MainAlpha->SamplerType = SAMPLERTYPE_Color;
				MainAlpha->Texture = Tex;
				MainAlpha->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->MainTexST : FLinearColor(1.f, 1.f, 0.f, 0.f));
				Mat->GetExpressionCollection().AddExpression(MainAlpha);
				Alpha = MainAlpha;
				AlphaOutput = 4;
				if (AlphaMaskTex)
				{
					UMaterialExpressionTextureSample* Mask = NewObject<UMaterialExpressionTextureSample>(Mat);
					Mask->Material = Mat;
					Mask->SamplerType = SAMPLERTYPE_LinearColor;
					Mask->Texture = AlphaMaskTex;
					Mask->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->AlphaMaskST : FLinearColor(1.f, 1.f, 0.f, 0.f));
					Mat->GetExpressionCollection().AddExpression(Mask);
					UMaterialExpressionMultiply* Combined = NewObject<UMaterialExpressionMultiply>(Mat);
					Combined->Material = Mat;
					Combined->A.Expression = MainAlpha;
					Combined->A.OutputIndex = 4;
					Combined->B.Expression = Mask;
					Combined->B.OutputIndex = 1;
					Mat->GetExpressionCollection().AddExpression(Combined);
					Alpha = Combined;
					AlphaOutput = 0;
				}
			}
			else
			{
				UMaterialExpressionConstant* One = NewObject<UMaterialExpressionConstant>(Mat);
				One->Material = Mat;
				One->R = 1.f;
				Mat->GetExpressionCollection().AddExpression(One);
				Alpha = One;
			}
			AddInput(TEXT("Alpha"), Alpha, AlphaOutput);
			Mat->GetExpressionCollection().AddExpression(Expand);
			Ed->WorldPositionOffset.Expression = Expand;
			// The family's installed outline vertex program, when the part's description is at hand (the approximate
			// width above stays for families without one).
			if (Shadow && Shadow->OutlinePart)
			{
				FKKRuntimeMaterialScene Scene;
				Scene.Globals = Shadow->OutlineGlobals;
				FString NativeError;
				const auto PartTexture = [&](const FString& Property) -> UTexture*
				{
					if (Property == TEXT("_MainTex")) return Tex;
					if (Property == TEXT("_DetailMask")) return DetailTex;
					if (Property == TEXT("_AlphaMask")) return AlphaMaskTex;
					return nullptr;
				};
				UMaterialExpression* Native = OKMakeRuntimeOutlineVertexOffset(Mat, *Shadow->OutlinePart, Scene, PartTexture, NativeError);
				if (Native) Ed->WorldPositionOffset.Expression = Native;
				UE_LOG(LogTemp, Display, TEXT("OK outline width: %s part=%s"), Native ? TEXT("installed vertex program") : *(TEXT("approximate (") + NativeError + TEXT(")")), *Shadow->OutlinePart->Name);
			}
		}
		if (Tex)
		{
			UMaterialExpressionTextureSample* S = NewObject<UMaterialExpressionTextureSample>(Mat);
			S->Material = Mat;
			S->SamplerType = SAMPLERTYPE_Color;
			S->Texture = Tex;
			S->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->MainTexST : FLinearColor(1.f, 1.f, 0.f, 0.f));
			Mat->GetExpressionCollection().AddExpression(S);
			if (bSkinShader)
			{
				// main_skin Outline frag uses _LineColorG.a=1-lineDepth just
				// like ForwardBase. At the game's lineDepth=1 its colour is
				// exactly black; MainTex * _LineColor leaves a false grey band
				// under the chin. Keep the full two-sided overlay for other
				// graphics settings instead of special-casing the current card.
				UMaterialExpression* Detail = nullptr;
				if (DetailTex)
				{
					UMaterialExpressionTextureSample* DetailSample = NewObject<UMaterialExpressionTextureSample>(Mat);
					DetailSample->Material = Mat;
					DetailSample->SamplerType = DetailTex->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
					DetailSample->Texture = DetailTex;
					DetailSample->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->DetailMaskST : FLinearColor(1.f, 1.f, 0.f, 0.f));
					Mat->GetExpressionCollection().AddExpression(DetailSample);
					Detail = DetailSample;
				}
				else
				{
					UMaterialExpressionConstant4Vector* Black = NewObject<UMaterialExpressionConstant4Vector>(Mat);
					Black->Material = Mat;
					Black->Constant = FLinearColor(0.f, 0.f, 0.f, 1.f);
					Mat->GetExpressionCollection().AddExpression(Black);
					Detail = Black;
				}
				UMaterialExpressionConstant* Depth = NewObject<UMaterialExpressionConstant>(Mat);
				Depth->Material = Mat;
				Depth->R = Shadow ? Shadow->AmbientDepth : 0.26f;
				Mat->GetExpressionCollection().AddExpression(Depth);
				UMaterialExpressionConstant* LineDepth = NewObject<UMaterialExpressionConstant>(Mat);
				LineDepth->Material = Mat;
				LineDepth->R = Shadow ? Shadow->LineDepth : 1.f;
				Mat->GetExpressionCollection().AddExpression(LineDepth);
				UMaterialExpressionCustom* SkinOutline = NewObject<UMaterialExpressionCustom>(Mat);
				SkinOutline->Material = Mat;
				SkinOutline->OutputType = CMOT_Float3;
				SkinOutline->Code = UnitySkinHueCode() + TEXT(R"(
float3 phases = 1.0 - frac(hue + float3(0.0, -0.333333343, 0.333333343)) * 2.0;
float3 hueTint = lerp(1.0, saturate(abs(phases) * 3.0 - 1.0),
    0.66 * chroma / max(hsv.x, 1e-10));
float3 ambientRgb = float3(0.5019608, 0.5019608, 0.5019608);
float3 ambient = Depth > 0.5
    ? 1.0 - (2.0 - 2.0 * Depth) * (1.0 - ambientRgb)
    : 2.0 * Depth * ambientRgb;
float3 shade = saturate(lerp(1.8 * hueTint * ambient,
    1.0 - (2.0 - 1.8 * hueTint) * (1.0 - ambient), step(0.5555556, hueTint)));
float3 baseLine = c * shade * ((1.0 - Detail.g) * 0.5 + 0.5);
float a = 1.0 - saturate(LineDepth);
float3 lineColor = a > 0.5
    ? 1.0 - (2.0 - 2.0 * a) * (1.0 - baseLine)
    : 2.0 * a * baseLine;
return saturate(lineColor);
)");
				for (const auto& Pair : { TPair<FName, UMaterialExpression*>(TEXT("Base"), S), TPair<FName, UMaterialExpression*>(TEXT("Detail"), Detail), TPair<FName, UMaterialExpression*>(TEXT("Depth"), Depth), TPair<FName, UMaterialExpression*>(TEXT("LineDepth"), LineDepth) })
				{
					FCustomInput& Input = SkinOutline->Inputs.AddDefaulted_GetRef();
					Input.InputName = Pair.Key;
					Input.Input.Expression = Pair.Value;
				}
				Mat->GetExpressionCollection().AddExpression(SkinOutline);
				Ed->EmissiveColor.Expression = SkinOutline;
			}
			else if (bHairShader)
			{
				// Shader Forge/main_hair Outline applies _LineColor as an
				// overlay blend after sampling _ColorMask and mixing its three
				// material colors (Outline PS 0003.asm.txt). The ForwardBase and
				// Outline passes must use the same source mask, not a CPU-baked texel.
				UMaterialExpression* TintedMain = S;
				if (Shadow && Shadow->bItemDraw)
				{
					UMaterialExpression* MaskInput = nullptr;
					if (Shadow->ColorMaskTex)
					{
						UMaterialExpressionTextureSample* Mask = NewObject<UMaterialExpressionTextureSample>(Mat);
						Mask->Material = Mat;
						Mask->Texture = Shadow->ColorMaskTex;
						Mask->SamplerType = Shadow->ColorMaskTex->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
						Mask->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow->ColorMaskST);
						Mat->GetExpressionCollection().AddExpression(Mask);
						MaskInput = Mask;
					}
					else
					{
						UMaterialExpressionConstant3Vector* Black = NewObject<UMaterialExpressionConstant3Vector>(Mat);
						Black->Material = Mat;
						Black->Constant = FLinearColor::Black;
						Mat->GetExpressionCollection().AddExpression(Black);
						MaskInput = Black;
					}
					UMaterialExpressionCustom* Mix = NewObject<UMaterialExpressionCustom>(Mat);
					Mix->Material = Mat;
					Mix->OutputType = CMOT_Float3;
					Mix->Code = TEXT("float3 c=lerp(float3(1,1,1),C1.rgb,Mask.r); c=lerp(c,C2.rgb,Mask.g); c=lerp(c,C3.rgb,Mask.b); return Main.rgb*c;");
					auto AddMixInput = [Mix](const TCHAR* Name, UMaterialExpression* Expr) {
						FCustomInput& Input = Mix->Inputs.AddDefaulted_GetRef();
						Input.InputName = Name;
						Input.Input.Expression = Expr;
					};
					AddMixInput(TEXT("Main"), S);
					AddMixInput(TEXT("Mask"), MaskInput);
					for (int32 Index = 0; Index < 3; ++Index)
					{
						UMaterialExpressionConstant4Vector* Color = NewObject<UMaterialExpressionConstant4Vector>(Mat);
						Color->Material = Mat;
						Color->Constant = Shadow->ItemColors[Index];
						Mat->GetExpressionCollection().AddExpression(Color);
						AddMixInput(Index == 0 ? TEXT("C1") : (Index == 1 ? TEXT("C2") : TEXT("C3")), Color);
					}
					Mat->GetExpressionCollection().AddExpression(Mix);
					TintedMain = Mix;
				}
				UMaterialExpressionCustom* Overlay = NewObject<UMaterialExpressionCustom>(Mat);
				Overlay->Material = Mat;
				Overlay->OutputType = CMOT_Float3;
				// _LineColor is a [Gamma] shader property: C above holds its
				// linear value, as Unity's outline PS compares it to 0.5.
				Overlay->Code = TEXT("float3 lineRgb = saturate(Line.rgb); float3 low = 2.0 * Tex.rgb * lineRgb; float3 high = 1.0 - (2.0 - 2.0 * lineRgb) * (1.0 - Tex.rgb); return saturate(lerp(low, high, step(0.5, lineRgb)));");
				FCustomInput& TexInput = Overlay->Inputs.AddDefaulted_GetRef();
				TexInput.InputName = TEXT("Tex");
				TexInput.Input.Expression = TintedMain;
				FCustomInput& LineInput = Overlay->Inputs.AddDefaulted_GetRef();
				LineInput.InputName = TEXT("Line");
				LineInput.Input.Expression = C;
				Mat->GetExpressionCollection().AddExpression(Overlay);
				Ed->EmissiveColor.Expression = Overlay;
			}
			else
			{
				UMaterialExpressionMultiply* Mul = NewObject<UMaterialExpressionMultiply>(Mat);
				Mul->Material = Mat;
				Mul->A.Expression = S;
				Mul->B.Expression = C;
				Mat->GetExpressionCollection().AddExpression(Mul);
				Ed->EmissiveColor.Expression = Mul;
			}
			if (bSkinShader && AlphaMaskTex)
			{
				auto* Mask = NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat);
				Mask->ParameterName=TEXT("_AlphaMask");
				Mask->Material = Mat;
				Mask->SamplerType = SAMPLERTYPE_LinearColor;
				Mask->Texture = AlphaMaskTex;
				Mask->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->AlphaMaskST : FLinearColor(1.f, 1.f, 0.f, 0.f));
				Mat->GetExpressionCollection().AddExpression(Mask);
				UMaterialExpressionMin* Combined = NewObject<UMaterialExpressionMin>(Mat);
				Combined->Material = Mat;
				Combined->A.Expression = Mask;
				Combined->A.OutputIndex = 1;
				Combined->B.Expression = Mask;
				Combined->B.OutputIndex = 2;
				Mat->GetExpressionCollection().AddExpression(Combined);
				Ed->OpacityMask.Expression = Combined;
			}
			else if (bHairShader && AlphaMaskTex)
			{
				UMaterialExpressionTextureSample* Mask = NewObject<UMaterialExpressionTextureSample>(Mat);
				Mask->Material = Mat;
				Mask->SamplerType = SAMPLERTYPE_LinearColor;
				Mask->Texture = AlphaMaskTex;
				Mask->Coordinates.Expression = MakeUnityMaskUV(Mat, Shadow ? Shadow->AlphaMaskST : FLinearColor(1.f, 1.f, 0.f, 0.f));
				Mat->GetExpressionCollection().AddExpression(Mask);
				UMaterialExpressionMultiply* Clip = NewObject<UMaterialExpressionMultiply>(Mat);
				Clip->Material = Mat;
				Clip->A.Expression = S;
				Clip->A.OutputIndex = 4;
				Clip->B.Expression = Mask;
				Clip->B.OutputIndex = 1;
				Mat->GetExpressionCollection().AddExpression(Clip);
				Ed->OpacityMask.Expression = Clip;
			}
			else
			{
				Ed->OpacityMask.Expression = S;
				Ed->OpacityMask.OutputIndex = 4;
			}
		}
		else
		{
			Ed->EmissiveColor.Expression = C;
		}
	}
	FString DedupeKey;
	if (UMaterial* Same = OKFindEqualMaterial(Mat, TEXT("outline"), DedupeKey)) return Same;
	Mat->UpdateCachedExpressionData();
	const double FinalizeStart = GOkCardLoadProfile.bActive ? FPlatformTime::Seconds() : 0.0;
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	OKEnsureSkeletalMaterialUsage(Mat);
	if (GOkCardLoadProfile.bActive) GOkCardLoadProfile.MaterialFinalizeSeconds += FPlatformTime::Seconds() - FinalizeStart;
	OKRememberMaterial(DedupeKey, Mat);
	return Mat;

#else
return Shadow?Shadow->CookedOutline:nullptr;
#endif
}

static float OutlineWidth()
{
	const FString W = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_OUTLINE_WIDTH"));
	return W.IsEmpty() ? 0.15f : FCString::Atof(*W);
}

// Unity's built-in shaders (Standard, Legacy, Unlit, FX, Particles, ...) draw no outline pass: map walls and Studio
// props made with them have no inverted-hull shell. Seen from outside a room, that shell is a black wall.
static bool OKShaderHasNoOutlinePass(const FString& Shader)
{
	static const TCHAR* Families[] = { TEXT("Standard"), TEXT("Legacy Shaders/"), TEXT("Unlit/"), TEXT("Mobile/"), TEXT("Particles/"), TEXT("FX/"),
		TEXT("Skybox/"), TEXT("Sprites/"), TEXT("UI/"), TEXT("Nature/"), TEXT("Diffuse"), TEXT("Transparent/") };
	for (const TCHAR* F : Families) if (Shader.StartsWith(F, ESearchCase::IgnoreCase)) return true;
	return false;
}

static bool OutlineEnabled()
{
	// A ShotPreview batch can change the render variant between actors.
	// Caching the first actor's option silently invalidates later controls.
	return FPlatformMisc::GetEnvironmentVariable(TEXT("OK_OUTLINE")) != TEXT("0");
}

static void AddOutlineSection(
	UProceduralMeshComponent* Comp,
	const TArray<FVector>& Verts,
	const TArray<int32>& Tris,
	const TArray<FVector>& Normals,
	int32 SectionIndex,
	const TArray<FVector2D>* UVs = nullptr,
	bool bSkipFaceCavity = false,
	UTexture2D* MainTex = nullptr,
	bool bHairShader = false,
	bool bSkinShader = false,
	FLinearColor LineColor = FLinearColor::Black,
	float LineWidth = 0.f,
	UTexture2D* DetailTex = nullptr,
	const FOKToonShadowParameters* Shadow = nullptr,
	UTexture2D* AlphaMaskTex = nullptr,
	bool bKoikanoClothes = false)
{
	// A shared _Outline parent culls the front face itself, so its shell keeps the source winding; a mirrored component
	// (negative world determinant, e.g. a static accessory under a mirrored socket) flips which face is front.
	const bool bMirrored = Comp && Comp->GetComponentTransform().GetDeterminant() < 0.f;
	const bool bShared = !WITH_EDITOR || (Shadow && Shadow->CookedMaterial && OKSharedParents());
	const bool bSourceWinding = bShared && !bMirrored;
	if (Comp && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHARED_DUMP")).Len() > 0) UE_LOG(LogTemp, Display, TEXT("OK outline winding %s mirrored=%d source=%d"), *Comp->GetName(), bMirrored ? 1 : 0, bSourceWinding ? 1 : 0);
	if (!Comp || Verts.Num() == 0 || Tris.Num() < 3 || Normals.Num() != Verts.Num())
	{
		return;
	}
	const float Width = (bSkinShader || bHairShader || bKoikanoClothes || (Shadow && Shadow->NativeItem)) ? 0.f : OutlineWidth();
	// Skin/hair stay at source positions here. Their shader samples the numeric
	// DetailMask at LOD0, with bilinear filtering and its authored ST, in WPO.
	TArray<FVector> OV;
	OV.Reserve(Verts.Num());
	TArray<FColor> Col;
	Col.Reserve(Verts.Num());
	for (int32 i = 0; i < Verts.Num(); ++i)
	{
		FVector N = Normals[i].GetSafeNormal();
		if (N.IsNearlyZero())
		{
			N = FVector::UpVector;
		}
		OV.Add(Verts[i] + N * Width);
		Col.Add(FColor(0, 0, 0, 255));
	}
	TArray<int32> OT;
	OT.Reserve(Tris.Num());
	const bool bUv = bSkipFaceCavity && UVs && UVs->Num() == Verts.Num();
	for (int32 t = 0; t + 2 < Tris.Num(); t += 3)
	{
		const int32 A = Tris[t];
		const int32 B = Tris[t + 1];
		const int32 C = Tris[t + 2];
		if (bUv && UVs->IsValidIndex(A) && UVs->IsValidIndex(B) && UVs->IsValidIndex(C))
		{
			// dump inner-mouth UV v 0.015–0.065; chin starts ~0.10. Skip only
			// tris wholly inside the cavity so the mouth rim still outlines.
			if ((*UVs)[A].Y > 0.91f && (*UVs)[B].Y > 0.91f && (*UVs)[C].Y > 0.91f)
			{
				continue;
			}
		}
        OT.Add(A);
        // The editor's own outline graph draws the reversed winding; the shared _Outline parents (and every packaged
        // build) the source winding, else the shell's outer faces cover the body in black.
        if(bSourceWinding){OT.Add(B);OT.Add(C);}else{OT.Add(C);OT.Add(B);}
	}
	if (OT.Num() < 3)
	{
		return;
	}
	// The outline fragment pass samples the same UV0 as the surface to clip
	// MainTex alpha. Zero UVs turn the entire
	// extruded hair/clothing shell opaque at one texel, painting interior
	// strands and cut-out panels black.
	TArray<FVector2D> UV;
	if (UVs && UVs->Num() == Verts.Num())
	{
		UV = *UVs;
	}
	else
	{
		UV.SetNumZeroed(Verts.Num());
	}
	UMaterialInterface* OutlineMat = MakeOutlineMaterial(MainTex, LineColor, bHairShader, bSkinShader, LineWidth, DetailTex, Shadow, AlphaMaskTex, bKoikanoClothes);
	if (Comp && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHARED_DUMP")).Len() > 0) UE_LOG(LogTemp, Display, TEXT("OK outline material %s %s"), *Comp->GetName(), OutlineMat ? *OutlineMat->GetName() : TEXT("none"));
	// A part the shared parents give no outline material: no shell at all. Created without a material it
	// drew the engine's default material over the part (a hair accessory came out black).
	if (!OutlineMat && bShared)
	{
		return;
	}
	Comp->CreateMeshSection(
		SectionIndex, OV, OT, Normals, UV, Col, TArray<FProcMeshTangent>(), false);
	if (OutlineMat)
	{
		Comp->SetMaterial(SectionIndex, OutlineMat);
	}
}

static void ApplyPartMaterial(UMeshComponent* Comp, UTexture2D* Tex, const FString& Blend, bool bUnlit, bool bTwoSided, bool bEye = false, UTexture2D* GlossTex = nullptr, UTexture2D* OverTex = nullptr, FLinearColor OverColor = FLinearColor::White, bool bNoZ = false, bool bPdo = false, bool bVcOverlay = false, UTexture2D* RampTex = nullptr, FLinearColor ShadowTint = FLinearColor(0.628f, 0.628f, 0.628f, 1.f), UTexture2D* DetailTex = nullptr, UTexture2D* LineTex = nullptr, UTexture2D* NormalTex = nullptr, UTexture2D* NormalMaskTex = nullptr, float ShadowExtend = 1.f, int32 ShaderKind = 0, const FOKToonShadowParameters* Shadow = nullptr, TArray<TObjectPtr<UMaterialInstanceDynamic>>* ShadowMaterials = nullptr, UTexture2D* AlphaMaskTex = nullptr, UTexture2D* OverTex2 = nullptr, bool bBodyOverlay = false)
{
#if !WITH_EDITOR
    if(Comp&&Shadow&&Shadow->CookedMaterial){Comp->SetMaterial(0,Shadow->CookedMaterial);Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);Comp->SetCastShadow(true);}
    return;
#else
	// A part on a shared parent (OKPrepareCookedRole) needs no graph of its own.
	if(Comp&&Shadow&&Shadow->CookedMaterial&&OKSharedParents()){Comp->SetMaterial(0,Shadow->CookedMaterial);Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);Comp->SetCastShadow(true);return;}
	if (!Comp)
	{
		return;
	}
	UMaterialInterface* Mat = nullptr;
	if (Tex)
	{
		Mat = MakePartMaterial(Tex, Blend, bUnlit, bTwoSided, bEye, GlossTex, OverTex, OverColor, bNoZ, bPdo, bVcOverlay, RampTex, ShadowTint, DetailTex, LineTex, NormalTex, NormalMaskTex, ShadowExtend, ShaderKind, Shadow, AlphaMaskTex, OverTex2, bBodyOverlay);
	}
	if (!Mat)
	{
		Mat = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	if (!Mat)
	{
		return;
	}
	OKEnsureSkeletalMaterialUsage(Mat);
	if (bEye || (Shadow && ((Shadow->Depth && ShadowMaterials) || Shadow->EyeDrawKind)))
	{
		UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Mat, Comp);
		if (Dynamic)
		{
			if (Shadow && Shadow->Depth && ShadowMaterials) {
				Dynamic->SetTextureParameterValue(TEXT("KKShadowDepth"), Shadow->Depth);
				ShadowMaterials->Add(Dynamic);
			}
			Mat = Dynamic;
		}
	}
	Comp->SetMaterial(0, Mat);
	if(Shadow && Shadow->TripoPBR)Comp->SetLightingChannels(false,true,false);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCastShadow(true);
#endif
}

static TAutoConsoleVariable<float> CVarOkRenderTextureInterval(TEXT("ok.RenderTextureInterval"), 0.5f,
	TEXT("Seconds between captures of a Studio map's render-to-texture cameras (the live stage's screens); 0 captures once when built."));

// A part sampling a map camera's RenderTexture (Unity's Unlit/MosaicTexture screens, without the mosaic): unlit, the
// render target's colour. One graph; each screen is a MID with its target in "Screen".
static UMaterialInterface* OKRenderTextureScreenMaterial()
{
#if WITH_EDITOR
	static TStrongObjectPtr<UMaterial> Cached;
	if (Cached) return Cached.Get();
	UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	Mat->MaterialDomain = MD_Surface;
	Mat->BlendMode = BLEND_Opaque;
	Mat->SetShadingModel(MSM_Unlit);
	UMaterialExpressionTextureSampleParameter2D* Samp = NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat);
	Samp->Material = Mat;
	Samp->ParameterName = TEXT("Screen");
	Samp->SamplerType = SAMPLERTYPE_Color;
	Samp->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
	Mat->GetExpressionCollection().AddExpression(Samp);
	if (UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData()) Ed->EmissiveColor.Expression = Samp;
	Mat->UpdateCachedExpressionData();
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	Mat->ForceRecompileForRendering();
	OKEnsureSkeletalMaterialUsage(Mat);
	Cached.Reset(Mat);
	return Mat;
#else
	return nullptr; // Editor graph creation is not a runtime rendering path.
#endif
}

UTextureRenderTarget2D* AKKPreviewActor::RenderTextureFor(FOkSession& Source, int32 PartIndex)
{
	// okmodel_view::mark_render_texture: camera index, then the camera in prefab space (Unity axes, metres).
	FLinearColor Index, Pos, Fwd, Up, Size;
	if (!Source.MaterialValue(PartIndex, "_OKRenderTexture", Index)) return nullptr;
	const int32 Camera = FMath::RoundToInt(Index.R);
	if (const TObjectPtr<USceneCaptureComponent2D>* Found = RenderTextureCaptures.Find(Camera))
		return *Found ? (*Found)->TextureTarget.Get() : nullptr;
	if (!Source.MaterialValue(PartIndex, "_OKRTCamPos", Pos) || !Source.MaterialValue(PartIndex, "_OKRTCamFwd", Fwd)
		|| !Source.MaterialValue(PartIndex, "_OKRTCamUp", Up) || !Source.MaterialValue(PartIndex, "_OKRTCamSize", Size)) return nullptr;
	const int32 W = FMath::Clamp(FMath::RoundToInt(Size.R), 16, 2048), H = FMath::Clamp(FMath::RoundToInt(Size.G), 16, 2048);
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
	Target->RenderTargetFormat = RTF_RGBA8_SRGB;
	Target->ClearColor = FLinearColor::Black;
	Target->InitAutoFormat(W, H);
	Target->UpdateResourceImmediate(true);
	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(this, *FString::Printf(TEXT("RenderTextureCapture_%d"), Camera));
	Capture->SetupAttachment(RootComponent);
	Capture->SetRelativeLocation(OkToUe(Pos.R, Pos.G, Pos.B));
	// Unity's camera looks along +Z with +Y up.
	Capture->SetRelativeRotation(FRotationMatrix::MakeFromXZ(OkToUeDir(Fwd.R, Fwd.G, Fwd.B), OkToUeDir(Up.R, Up.G, Up.B)).Rotator());
	const float Aspect = float(W) / float(H);
	if (Size.B > 0.5f)
	{
		Capture->ProjectionType = ECameraProjectionMode::Orthographic;
		Capture->OrthoWidth = 2.f * Size.A * Aspect * 100.f;
	}
	else
	{
		// Unity's field of view is vertical, Unreal's horizontal.
		Capture->FOVAngle = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Pos.A) * 0.5f) * Aspect));
	}
	Capture->bOverride_CustomNearClippingPlane = true;
	Capture->CustomNearClippingPlane = FMath::Max(Fwd.A * 100.f, 0.1f);
	Capture->MaxViewDistanceOverride = Up.A * 100.f;
	Capture->CaptureSource = SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->TextureTarget = Target;
	Capture->RegisterComponent();
	Capture->CaptureSceneDeferred();
	RenderTextureCaptures.Add(Camera, Capture);
	UE_LOG(LogTemp, Display, TEXT("OK render texture camera=%d %dx%d fov=%.1f at=%s"), Camera, W, H, Pos.A, *Capture->GetComponentLocation().ToString());
	// Not every frame: again every ok.RenderTextureInterval seconds (the scene and its materials settle after the load).
	const float Interval = CVarOkRenderTextureInterval.GetValueOnGameThread();
	if (Interval > 0.f && !RenderTextureTicker.IsValid())
	{
		TWeakObjectPtr<AKKPreviewActor> Weak(this);
		RenderTextureTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
		{
			AKKPreviewActor* Self = Weak.Get();
			if (!Self) return false;
			for (const TPair<int32, TObjectPtr<USceneCaptureComponent2D>>& It : Self->RenderTextureCaptures)
				if (It.Value && It.Value->IsRegistered()) It.Value->CaptureSceneDeferred();
			// OK_RT_DUMP=1 (debugging): after a few captures, each target to Saved/OKStudio/render_texture_<camera>.png.
			static int32 Dumps = 0;
			if (++Dumps == 6 && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RT_DUMP")) == TEXT("1"))
				for (const TPair<int32, TObjectPtr<USceneCaptureComponent2D>>& It : Self->RenderTextureCaptures)
				{
					UTextureRenderTarget2D* Target = It.Value ? It.Value->TextureTarget.Get() : nullptr;
					FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
					TArray<FColor> Pixels;
					if (!Resource || !Resource->ReadPixels(Pixels)) continue;
					for (FColor& C : Pixels) C.A = 255;
					TArray64<uint8> Png;
					FImageUtils::PNGCompressImageArray(Target->SizeX, Target->SizeY, Pixels, Png);
					const FString Path = FPaths::ProjectSavedDir() / FString::Printf(TEXT("OKStudio/render_texture_%d.png"), It.Key);
					FFileHelper::SaveArrayToFile(Png, *Path);
					UE_LOG(LogTemp, Display, TEXT("OK render texture dump %s"), *Path);
					// And each screen showing it (the components hidden from its camera), seen from the camera's side.
					for (const TWeakObjectPtr<UPrimitiveComponent>& Hidden : It.Value->HiddenComponents)
					{
						UPrimitiveComponent* ScreenComp = Hidden.Get();
						if (!ScreenComp) continue;
						const FBox Box = ScreenComp->Bounds.GetBox();
						const FVector Dir = (It.Value->GetComponentLocation() - Box.GetCenter()).GetSafeNormal();
						UTextureRenderTarget2D* Shot = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
						Shot->RenderTargetFormat = RTF_RGBA8_SRGB;
						Shot->InitAutoFormat(1024, 512);
						Shot->UpdateResourceImmediate(true);
						USceneCaptureComponent2D* View = NewObject<USceneCaptureComponent2D>(Self);
						View->SetWorldLocationAndRotation(Box.GetCenter() + Dir * Box.GetExtent().GetMax() * 2.2f, (-Dir).Rotation());
						View->FOVAngle = 60.f;
						View->CaptureSource = SCS_FinalColorLDR;
						View->bCaptureEveryFrame = false;
						View->TextureTarget = Shot;
						View->RegisterComponent();
						View->CaptureScene();
						TArray<FColor> ShotPixels;
						if (Shot->GameThread_GetRenderTargetResource()->ReadPixels(ShotPixels))
						{
							for (FColor& C : ShotPixels) C.A = 255;
							TArray64<uint8> ShotPng;
							FImageUtils::PNGCompressImageArray(1024, 512, ShotPixels, ShotPng);
							FFileHelper::SaveArrayToFile(ShotPng, *(FPaths::GetBaseFilename(Path, false) + TEXT("_screen_") + ScreenComp->GetName() + TEXT(".png")));
						}
						View->DestroyComponent();
					}
				}
			// OK_RT_VIEW=x,y,z,dx,dy,dz,vfov (debugging, with OK_RT_DUMP): a sharp 1280x720 picture of the map from that
			// Unity-space pose (map metres, view direction), Saved/OKStudio/render_texture_view.png.
			TArray<FString> Pose;
			FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RT_VIEW")).ParseIntoArray(Pose, TEXT(","));
			if (Dumps == 6 && Pose.Num() == 7 && Self->GetRootComponent())
			{
				float P[7];
				for (int32 K = 0; K < 7; ++K) P[K] = FCString::Atof(*Pose[K]);
				UTextureRenderTarget2D* Shot = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
				Shot->RenderTargetFormat = RTF_RGBA8_SRGB;
				Shot->InitAutoFormat(1280, 720);
				Shot->UpdateResourceImmediate(true);
				USceneCaptureComponent2D* View = NewObject<USceneCaptureComponent2D>(Self);
				View->SetupAttachment(Self->GetRootComponent());
				View->SetRelativeLocation(OkToUe(P[0], P[1], P[2]));
				View->SetRelativeRotation(OkToUeDir(P[3], P[4], P[5]).Rotation());
				View->FOVAngle = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(P[6]) * 0.5f) * 16.f / 9.f));
				// OK_RT_VIEW_NOFX=1: without particle systems (Niagara), to tell them from geometry.
				if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RT_VIEW_NOFX")) == TEXT("1")) View->ShowFlags.SetParticles(false);
				View->CaptureSource = SCS_FinalColorLDR;
				View->bCaptureEveryFrame = false;
				View->TextureTarget = Shot;
				View->RegisterComponent();
				View->CaptureScene();
				TArray<FColor> ShotPixels;
				if (Shot->GameThread_GetRenderTargetResource()->ReadPixels(ShotPixels))
				{
					for (FColor& C : ShotPixels) C.A = 255;
					TArray64<uint8> ShotPng;
					FImageUtils::PNGCompressImageArray(1280, 720, ShotPixels, ShotPng);
					FFileHelper::SaveArrayToFile(ShotPng, *(FPaths::ProjectSavedDir() / TEXT("OKStudio/render_texture_view.png")));
				}
				View->DestroyComponent();
			}
			return true;
		}), Interval);
	}
	return Target;
}

void AKKPreviewActor::StopRenderTextureCaptures()
{
	if (RenderTextureTicker.IsValid())
	{
		FTSTicker::RemoveTicker(RenderTextureTicker);
		RenderTextureTicker.Reset();
	}
	for (const TPair<int32, TObjectPtr<USceneCaptureComponent2D>>& It : RenderTextureCaptures)
		if (It.Value) It.Value->DestroyComponent();
	RenderTextureCaptures.Reset();
}

// Unity's built-in additive particle shaders (Legacy Shaders/Particles/Additive: Studio's 2D lens flares): SrcAlpha One,
// colour 2 x vertex x _TintColor x texture in linear space. All installed quality levels enable SOFTPARTICLES_ON:
// alpha also fades by saturate(_InvFade * (sceneEyeDepth - particleEyeDepth)), with Unity depths in metres.
static UMaterialInterface* OKParticleAdditiveMaterial()
{
#if WITH_EDITOR
	static TStrongObjectPtr<UMaterial> Cached;
	if (Cached) return Cached.Get();
	UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	Mat->MaterialDomain = MD_Surface;
	Mat->BlendMode = BLEND_Additive;
	Mat->SetShadingModel(MSM_Unlit);
	// Unity renders transparent particles before camera image effects, including DOF.
	// UE defaults a new translucent material to AfterDOF, leaving sharp white flares.
	Mat->TranslucencyPass = MTP_BeforeDOF;
	Mat->TwoSided = true;
	UMaterialExpressionTextureSampleParameter2D* Samp = NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat);
	Samp->Material = Mat;
	Samp->ParameterName = TEXT("Tex");
	Samp->SamplerType = SAMPLERTYPE_Color;
	Samp->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black.Black")); // an sRGB default
	Mat->GetExpressionCollection().AddExpression(Samp);
	UMaterialExpressionVectorParameter* Tint = NewObject<UMaterialExpressionVectorParameter>(Mat);
	Tint->Material = Mat;
	Tint->ParameterName = TEXT("Tint");
	Tint->DefaultValue = FLinearColor::White;
	Mat->GetExpressionCollection().AddExpression(Tint);
	UMaterialExpressionVertexColor* Vertex = NewObject<UMaterialExpressionVertexColor>(Mat);
	Vertex->Material = Mat;
	Mat->GetExpressionCollection().AddExpression(Vertex);
	auto* Distance = NewObject<UMaterialExpressionScalarParameter>(Mat);
	Distance->Material = Mat;
	Distance->ParameterName = TEXT("FadeDistance");
	Distance->DefaultValue = 100.f;
	Mat->GetExpressionCollection().AddExpression(Distance);
	auto* Fade = NewObject<UMaterialExpressionDepthFade>(Mat);
	Fade->Material = Mat;
	Fade->OpacityDefault = 1.f;
	Fade->FadeDistance.Expression = Distance;
	Mat->GetExpressionCollection().AddExpression(Fade);
	UMaterialExpressionCustom* Program = NewObject<UMaterialExpressionCustom>(Mat);
	Program->Material = Mat;
	Program->OutputType = CMOT_Float3;
	auto Input = [&](const TCHAR* Name, UMaterialExpression* Expression, int32 Output)
	{
		FCustomInput In;
		In.InputName = Name;
		In.Input.Expression = Expression;
		In.Input.OutputIndex = Output;
		Program->Inputs.Add(In);
	};
	Input(TEXT("Tex"), Samp, 5);
	Input(TEXT("Tint"), Tint, 0);
	Input(TEXT("TintAlpha"), Tint, 4);
	Input(TEXT("Vertex"), Vertex, 0);
	Input(TEXT("VertexAlpha"), Vertex, 4);
	Input(TEXT("SoftFade"), Fade, 0);
	// Installed PS: rgb=2*vertex*tint*texture; alpha=saturate(2*v.a*tint.a*tex.a).
	// The sRGB texture sampler and the authored Color tint are already linear.
	Program->Code = TEXT("float3 rgb = 2.0 * Vertex * Tint * Tex.rgb;\nfloat alpha = saturate(2.0 * VertexAlpha * SoftFade * TintAlpha * Tex.a);\nreturn max(rgb * alpha, 0.0);");
	Mat->GetExpressionCollection().AddExpression(Program);
	if (UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData()) Ed->EmissiveColor.Expression = Program;
	Mat->UpdateCachedExpressionData();
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	Mat->ForceRecompileForRendering();
	OKEnsureSkeletalMaterialUsage(Mat);
	Cached.Reset(Mat);
	return Mat;
#else
	return nullptr; // Editor graph creation is not a runtime rendering path.
#endif
}

// Studio map stage effects (unity::stage_effect_kind, `_OKStageEffect`), from the live stage's Shader Forge DXBC: every
// kind tints by the hue of its scrolling `color` texture (rgb→hsv→hsv(h,1,1)), lerp(1, hue, _saturation)·_brightness.
// 1 volumeLight: ·saturate(_mask·_multiplier)·N·V, additive, front faces culled; 2 spotLight_beam: ·_mask·N·V (normal
// turned to the viewer), additive; 3 spotLaser / 4 spotLight_glow: ·_mask, additive; 5 electricLight (LED panels):
// ·the scrolling fast/slow masks squared (min 1) lerped to _MaskFull, opaque. Colour UVs are Unity uv2 (TEXCOORD2
// here), masks uv; Unity _Time is (t/20, t). One graph per kind; each part is a MID with its textures and values.
static UMaterialInterface* OKStageEffectMaterial(int32 Kind)
{
#if WITH_EDITOR
	static TStrongObjectPtr<UMaterial> Cached[6];
	if (Kind < 1 || Kind > 5) return nullptr;
	if (Cached[Kind]) return Cached[Kind].Get();
	UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	Mat->MaterialDomain = MD_Surface;
	Mat->BlendMode = Kind == 5 ? BLEND_Opaque : BLEND_Additive;
	Mat->SetShadingModel(MSM_Unlit);
	Mat->TwoSided = true;
	UMaterialExpressionCustom* Code = NewObject<UMaterialExpressionCustom>(Mat);
	Code->Material = Mat;
	Code->OutputType = CMOT_Float3;
	Mat->GetExpressionCollection().AddExpression(Code);
	auto Input = [&](const TCHAR* Name, UMaterialExpression* E, int32 Output = 0)
	{
		E->Material = Mat;
		Mat->GetExpressionCollection().AddExpression(E);
		FCustomInput& I = Code->Inputs.AddDefaulted_GetRef(); I.InputName = Name; I.Input.Expression = E; I.Input.OutputIndex = Output;
	};
	Code->Inputs.Reset();
	for (const TCHAR* Name : { TEXT("Color"), TEXT("Mask"), TEXT("Mask2"), TEXT("Mask3") })
	{
		auto* T = NewObject<UMaterialExpressionTextureObjectParameter>(Mat);
		T->ParameterName = Name; T->Texture = OKExposureWhiteTexture(); T->SamplerType = SAMPLERTYPE_LinearColor;
		Input(Name, T);
	}
	for (const TCHAR* Name : { TEXT("ColorST"), TEXT("MaskST"), TEXT("Mask2ST"), TEXT("Mask3ST"), TEXT("P"), TEXT("Q") })
	{
		auto* V = NewObject<UMaterialExpressionVectorParameter>(Mat);
		V->ParameterName = Name; V->DefaultValue = FLinearColor(1.f, 1.f, 0.f, 0.f);
		Input(Name, V, 5); // output 5 is RGBA (0 is RGB only)
	}
	auto* UV0 = NewObject<UMaterialExpressionTextureCoordinate>(Mat); UV0->CoordinateIndex = 0; Input(TEXT("UV0"), UV0);
	auto* UV1 = NewObject<UMaterialExpressionTextureCoordinate>(Mat); UV1->CoordinateIndex = 2; Input(TEXT("UV1"), UV1);
	Input(TEXT("T"), NewObject<UMaterialExpressionTime>(Mat));
	Input(TEXT("N"), NewObject<UMaterialExpressionVertexNormalWS>(Mat));
	Input(TEXT("V"), NewObject<UMaterialExpressionCameraVectorWS>(Mat));
	Input(TEXT("Side"), NewObject<UMaterialExpressionTwoSidedSign>(Mat));
	// P = (_saturation, _brightness, colour phase speed, _multiplier); Q = (uv offset, scroll speed, texture change,
	// full lighting). Texture coordinates are Unity's (v up): `S` applies a _ST and returns a sampling UV.
	FString Hlsl = TEXT("float2 u0=float2(UV0.x,1.0-UV0.y); float2 u1=float2(UV1.x,1.0-UV1.y); float tx=T/20.0, ty=T; ")
		TEXT("\n#define S(u,st) (float2(((u)*(st).xy+(st).zw).x,1.0-((u)*(st).xy+(st).zw).y))\n");
	Hlsl += Kind == 1 ? TEXT("float2 cu=u1; ") : Kind == 3 ? TEXT("float2 cu=u1+tx*P.z+Q.x; ") : TEXT("float2 cu=u1+tx*P.z; ");
	Hlsl += TEXT("float4 c=Color.Sample(ColorSampler,S(cu,ColorST)); ")
		TEXT("float4 k=float4(0,-1.0/3.0,2.0/3.0,-1.0); float4 p=lerp(float4(c.bg,k.wz),float4(c.gb,k.xy),step(c.b,c.g)); ")
		TEXT("float4 q=lerp(float4(p.xyw,c.r),float4(c.r,p.yzx),step(p.x,c.r)); float d=q.x-min(q.w,q.y); ")
		TEXT("float h=abs(q.z+(q.w-q.y)/(6.0*d+1e-10)); float3 hue=saturate(abs(1.0-2.0*frac(h+float3(0,-1.0/3.0,1.0/3.0)))*3.0-1.0); ")
		TEXT("float3 col=lerp(float3(1,1,1),hue,P.x)*P.y; float3 r; ");
	switch (Kind)
	{
	case 1: // Unity draws only the back faces (Cull Front); the normal unturned.
		Hlsl += TEXT("float3 n=normalize(N*Side); float ndv=dot(n,normalize(V)); float3 m=saturate(Mask.Sample(MaskSampler,S(u0,MaskST)).rgb*P.w); ")
			TEXT("r=Side<0 ? col*m*ndv : float3(0,0,0);"); break;
	case 2:
		Hlsl += TEXT("float ndv=dot(normalize(N),normalize(V)); r=col*Mask.Sample(MaskSampler,S(u0,MaskST)).rgb*ndv;"); break;
	case 5:
		Hlsl += TEXT("float s=ty*Q.y; float f=frac(s); float2 uf=u0+float2(floor(s)*Q.x,0); ")
			TEXT("float3 m=lerp(Mask.Sample(MaskSampler,S(uf,MaskST)).rgb,Mask2.Sample(Mask2Sampler,S(uf,Mask2ST)).rgb,Q.z); m=min((m*(1.0-f)+m)*(m*(1.0-f)+m),1.0); ")
			TEXT("m=lerp(m,Mask3.Sample(Mask3Sampler,S(u0,Mask3ST)).rgb,Q.w); r=col*m;"); break;
	default:
		Hlsl += TEXT("r=col*Mask.Sample(MaskSampler,S(u0,MaskST)).rgb;"); break;
	}
	// Unity works in gamma space: the textures are read raw and the result goes to linear for Unreal. Its target is LDR,
	// so a fragment adds at most 1 (_brightness 5 beams unclamped added 5^2.2 each and bloomed the stage white).
	Hlsl += TEXT("\n#undef S\nreturn pow(saturate(r),2.2);");
	Code->Code = Hlsl;
	if (UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData()) Ed->EmissiveColor.Expression = Code;
	Mat->UpdateCachedExpressionData();
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	Mat->ForceRecompileForRendering();
	OKEnsureSkeletalMaterialUsage(Mat);
	Cached[Kind].Reset(Mat);
	return Mat;
#else
	return nullptr; // Editor graph creation is not a runtime rendering path.
#endif
}

// The stage-effect MID of a session part (`_OKStageEffect`), or null: its textures (session `shader_textures`) and values.
static UMaterialInstanceDynamic* OKStageEffectInstance(FOkSession& Source, int32 Index, UObject* Outer, TArray<UTexture2D*>& OutTextures)
{
	FLinearColor KindValue;
	if (!Source.MaterialValue(Index, "_OKStageEffect", KindValue)) return nullptr;
	const int32 Kind = FMath::RoundToInt(KindValue.R);
	UMaterialInterface* Base = OKStageEffectMaterial(Kind);
	UMaterialInstanceDynamic* Dynamic = Base ? UMaterialInstanceDynamic::Create(Base, Outer) : nullptr;
	if (!Dynamic) return nullptr;
	auto Value = [&](const char* Name, float Default) { FLinearColor V; return Source.MaterialValue(Index, Name, V) ? V.R : Default; };
	auto ST = [&](const char* Name) { FLinearColor V(1.f, 1.f, 0.f, 0.f); Source.MaterialValue(Index, Name, V); return V; };
	auto Texture = [&](const char* Property, const TCHAR* Parameter)
	{
		const uint8* Bytes = nullptr;
		int32 Length = 0;
		if (!Source.ShaderTexture(Index, Property, Bytes, Length)) return;
		if (UTexture2D* T = TextureFromPngBytes(Bytes, Length))
		{
			T->SRGB = false;
			T->UpdateResource();
			T->AddToRoot();
			OutTextures.Add(T);
			Dynamic->SetTextureParameterValue(Parameter, T);
		}
	};
	if (Kind == 5)
	{
		Texture("_Emission", TEXT("Color")); Texture("_MaskFast", TEXT("Mask")); Texture("_MaskSlow", TEXT("Mask2")); Texture("_MaskFull", TEXT("Mask3"));
		Dynamic->SetVectorParameterValue(TEXT("ColorST"), ST("_Emission_ST"));
		Dynamic->SetVectorParameterValue(TEXT("MaskST"), ST("_MaskFast_ST"));
		Dynamic->SetVectorParameterValue(TEXT("Mask2ST"), ST("_MaskSlow_ST"));
		Dynamic->SetVectorParameterValue(TEXT("Mask3ST"), ST("_MaskFull_ST"));
		Dynamic->SetVectorParameterValue(TEXT("P"), FLinearColor(Value("_saturation", 1.f), Value("_brightness", 1.f), Value("_ColorPhaseSpeed", 0.f), 1.f));
		Dynamic->SetVectorParameterValue(TEXT("Q"), FLinearColor(Value("_UVOffset", 0.f), Value("_ScrollSpeed", 0.f), Value("_TextureChange", 0.f), Value("_FullLighting", 0.f)));
	}
	else
	{
		Texture("_color", TEXT("Color")); Texture("_mask", TEXT("Mask"));
		Dynamic->SetVectorParameterValue(TEXT("ColorST"), ST("_color_ST"));
		Dynamic->SetVectorParameterValue(TEXT("MaskST"), ST("_mask_ST"));
		Dynamic->SetVectorParameterValue(TEXT("P"), FLinearColor(Value("_saturation", 1.f), Value("_brightness", 1.f), Value("_colorphasespped", 0.f), Value("_multiplier", 1.f)));
		Dynamic->SetVectorParameterValue(TEXT("Q"), FLinearColor(Value("_uvoffset", 0.f), 0.f, 0.f, 0.f));
	}
	return Dynamic;
}

static UMaterial* MakeOvertexMaterial(UTexture2D* Tex, FLinearColor Color)
{
#if WITH_EDITOR

	UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!Mat || !Tex)
	{
		return nullptr;
	}
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	Tex->UpdateResource();
	Mat->MaterialDomain = MD_Surface;
	Mat->BlendMode = BLEND_Translucent;
	Mat->SetShadingModel(MSM_Unlit);
	Mat->TwoSided = false;
	Mat->SetUsageByFlag(MATUSAGE_StaticMesh, true);
	Mat->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
	Mat->SetMaterialUsage(MATUSAGE_StaticMesh);
	Mat->SetMaterialUsage(MATUSAGE_SkeletalMesh);
	UMaterialExpressionTextureSample* Samp = NewObject<UMaterialExpressionTextureSample>(Mat);
	Samp->Material = Mat;
	Samp->SamplerType = SAMPLERTYPE_Color;
	Samp->ConstCoordinate = 0;
	Samp->Texture = Tex;
	Mat->GetExpressionCollection().AddExpression(Samp);
	if (UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData())
	{
		UMaterialExpressionConstant3Vector* Col = NewObject<UMaterialExpressionConstant3Vector>(Mat);
		Col->Material = Mat;
		Col->Constant = FLinearColor::FromSRGBColor(FColor(
			(uint8)FMath::Clamp(FMath::RoundToInt(Color.R * 255.f), 0, 255),
			(uint8)FMath::Clamp(FMath::RoundToInt(Color.G * 255.f), 0, 255),
			(uint8)FMath::Clamp(FMath::RoundToInt(Color.B * 255.f), 0, 255),
			255));
		Mat->GetExpressionCollection().AddExpression(Col);
		UMaterialExpressionConstant* Ca = NewObject<UMaterialExpressionConstant>(Mat);
		Ca->Material = Mat;
		Ca->R = Color.A;
		Mat->GetExpressionCollection().AddExpression(Ca);
		UMaterialExpressionMultiply* Tint = NewObject<UMaterialExpressionMultiply>(Mat);
		Tint->Material = Mat;
		Tint->A.Expression = Samp;
		Tint->B.Expression = Col;
		Mat->GetExpressionCollection().AddExpression(Tint);
		UMaterialExpressionMultiply* Op = NewObject<UMaterialExpressionMultiply>(Mat);
		Op->Material = Mat;
		Op->A.Expression = Samp;
		Op->A.OutputIndex = 4;
		Op->B.Expression = Ca;
		Mat->GetExpressionCollection().AddExpression(Op);
		Ed->EmissiveColor.Expression = Tint;
		Ed->BaseColor.Expression = Tint;
		Ed->Opacity.Expression = Op;
		Ed->Opacity.OutputIndex = 0;
	}
	Mat->UpdateCachedExpressionData();
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	Mat->ForceRecompileForRendering();
	OKEnsureSkeletalMaterialUsage(Mat);
	return Mat;

#else
return nullptr; // Editor graph creation is not a runtime rendering path.
#endif
}

static void FillOvertexSection(UProceduralMeshComponent* Comp, const TArray<FVector>& Verts, const TArray<int32>& Tris, const TArray<FVector2D>& UV4)
{
	if (!Comp || UV4.Num() != Verts.Num() || Verts.Num() == 0)
	{
		return;
	}
	TArray<FVector> Ov = Verts;
	for (FVector& P : Ov)
	{
		P.Y += 0.08f;
	}
	TArray<FVector2D> OvUV;
	OvUV.Reserve(UV4.Num());
	for (const FVector2D& T : UV4)
	{
		OvUV.Add(FVector2D(T.X, 1.0f - T.Y));
	}
	TArray<FColor> White;
	White.Init(FColor::White, Verts.Num());
	TArray<FVector> EmptyN;
	TArray<FProcMeshTangent> Tangents;
	Comp->CreateMeshSection(1, Ov, Tris, EmptyN, OvUV, White, Tangents, false);
	Comp->SetCastShadow(false);
}

static void FillSection(UProceduralMeshComponent* Comp, const TArray<FVector>& Verts, const TArray<int32>& Tris, const TArray<FVector2D>& UV, const TArray<FColor>& Colors, const TArray<FVector>& InNormals = TArray<FVector>(), const TArray<FVector2D>& UV1 = TArray<FVector2D>(), const TArray<FVector2D>& UV2 = TArray<FVector2D>(), const TArray<FProcMeshTangent>& InTangents=TArray<FProcMeshTangent>(), const TArray<FVector2D>& UV3=TArray<FVector2D>())
{
	TArray<FVector> Normals;
	if (InNormals.Num() == Verts.Num() && Verts.Num() > 0)
	{
		for (int32 i = 0; i < InNormals.Num(); i++)
		{
			FVector N = InNormals[i].GetSafeNormal();
			Normals.Add(N.IsNearlyZero() ? FVector::UpVector : N);
		}
	}
	else
	{
		Normals.Init(FVector::ZeroVector, Verts.Num());
		for (int32 i = 0; i + 2 < Tris.Num(); i += 3)
		{
			const int32 A = Tris[i];
			const int32 B = Tris[i + 1];
			const int32 C = Tris[i + 2];
			if (!Verts.IsValidIndex(A) || !Verts.IsValidIndex(B) || !Verts.IsValidIndex(C))
			{
				continue;
			}
			const FVector N = FVector::CrossProduct(Verts[B] - Verts[A], Verts[C] - Verts[A]).GetSafeNormal();
			Normals[A] += N;
			Normals[B] += N;
			Normals[C] += N;
		}
		for (FVector& N : Normals)
		{
			N = N.GetSafeNormal();
			if (N.IsNearlyZero())
			{
				N = FVector::UpVector;
			}
		}
	}
	TArray<FProcMeshTangent> Tangents=InTangents;
	TArray<FVector2D> UseUV = UV;
	if (UseUV.Num() != Verts.Num())
	{
		UseUV.SetNumZeroed(Verts.Num());
	}
	TArray<FColor> UseColors = Colors;
	if (UseColors.Num() != Verts.Num())
	{
		UseColors.Init(FColor::White, Verts.Num());
	}
	if (Comp->GetNumSections() > 0 && Comp->GetProcMeshSection(0)->ProcVertexBuffer.Num()==Verts.Num())
	{
		UKKAffineProceduralMeshComponent::UpdateStreams(Comp,0, Verts, Normals, UseUV, UV1, UV2, UV3, UseColors, Tangents);
	}
	else if (UV2.Num() == Verts.Num() && Verts.Num() > 0)
	{
		TArray<FVector2D> OverlayUV = UV1;
		if (OverlayUV.Num() != Verts.Num()) OverlayUV.SetNumZeroed(Verts.Num());
		TArray<FVector2D> Empty;
		Comp->CreateMeshSection(0, Verts, Tris, Normals, UseUV, OverlayUV, UV2, UV3, UseColors, Tangents, false);
	}
	else if (UV1.Num() == Verts.Num() && Verts.Num() > 0)
	{
		TArray<FVector2D> Empty;
		Comp->CreateMeshSection(0, Verts, Tris, Normals, UseUV, UV1, Empty, Empty, UseColors, Tangents, false);
	}
	else
	{
		Comp->CreateMeshSection(0, Verts, Tris, Normals, UseUV, UseColors, Tangents, false);
	}
	Comp->SetCastShadow(false);
}

AKKPreviewActor::AKKPreviewActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	BodyMesh = CreateDefaultSubobject<UKKAffineProceduralMeshComponent>(TEXT("BodyMesh"));
    CastChecked<UKKAffineProceduralMeshComponent>(BodyMesh)->bUseAnimatedStreams = true;
	RootComponent = BodyMesh;
	HeadMesh = CreateDefaultSubobject<UKKAffineProceduralMeshComponent>(TEXT("HeadMesh"));
    CastChecked<UKKAffineProceduralMeshComponent>(HeadMesh)->bUseAnimatedStreams = true;
	HeadMesh->SetupAttachment(RootComponent);
	StudioLens = CreateDefaultSubobject<UCameraComponent>(TEXT("StudioLens"));
	StudioLens->SetupAttachment(RootComponent);
	StudioLens->bUsePawnControlRotation = false;
	bFindCameraComponentWhenViewTarget = true;
}

bool AKKPreviewActor::LoadConvertedPreview(FString& OutError)
{
	return LoadDirectory(FPaths::ProjectSavedDir() / TEXT("Converted/_vanilla"), OutError);
}

bool AKKPreviewActor::LoadDirectory(const FString& Dir, FString& OutError)
{
	DestroyAllParts();
	MissingMods.Reset();
	MappedSliders.Reset();
	UnmappedFace.Reset();
	UnmappedBody.Reset();
	ShapeFace.Reset();
	ShapeBody.Reset();

	const FString ManifestPath = Dir / TEXT("manifest.json");
	FString JsonText;
	TArray<FString> ObjFiles;
	TMap<FString, FString> TexByObj;
	TMap<FString, FString> BlendByObj;
	TMap<FString, bool> UnlitByObj;
	FLinearColor HairColor = FLinearColor::White;
	FLinearColor ClothesColor = FLinearColor::White;
	FLinearColor SkinColor = FLinearColor(0.78f, 0.56f, 0.46f, 1.0f);
	if (FFileHelper::LoadFileToString(JsonText, *ManifestPath))
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Missing = nullptr;
			if (Root->TryGetArrayField(TEXT("missing"), Missing))
			{
				for (const TSharedPtr<FJsonValue>& V : *Missing)
				{
					MissingMods.AddUnique(V->AsString());
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Mapped = nullptr;
			if (Root->TryGetArrayField(TEXT("mapped_sliders"), Mapped))
			{
				for (const TSharedPtr<FJsonValue>& V : *Mapped)
				{
					MappedSliders.Add(V->AsString());
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* UF = nullptr;
			if (Root->TryGetArrayField(TEXT("unmapped_face"), UF))
			{
				for (const TSharedPtr<FJsonValue>& V : *UF)
				{
					UnmappedFace.Add(static_cast<int32>(V->AsNumber()));
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* UB = nullptr;
			if (Root->TryGetArrayField(TEXT("unmapped_body"), UB))
			{
				for (const TSharedPtr<FJsonValue>& V : *UB)
				{
					UnmappedBody.Add(static_cast<int32>(V->AsNumber()));
				}
			}
			auto ReadColor = [](const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, FLinearColor& Out)
			{
				const TArray<TSharedPtr<FJsonValue>>* Col = nullptr;
				if (Obj->TryGetArrayField(Field, Col) && Col->Num() >= 3)
				{
					Out = FLinearColor(
						(*Col)[0]->AsNumber(),
						(*Col)[1]->AsNumber(),
						(*Col)[2]->AsNumber(),
						Col->Num() > 3 ? (*Col)[3]->AsNumber() : 1.0);
				}
			};
			ReadColor(Root, TEXT("hair_color"), HairColor);
			ReadColor(Root, TEXT("clothes_color"), ClothesColor);
			ReadColor(Root, TEXT("skin_color"), SkinColor);
			auto ReadFloats = [](const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, TArray<float>& Out)
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
				if (Obj->TryGetArrayField(Field, Arr))
				{
					for (const TSharedPtr<FJsonValue>& V : *Arr)
					{
						Out.Add(static_cast<float>(V->AsNumber()));
					}
				}
			};
			ReadFloats(Root, TEXT("shape_face"), ShapeFace);
			ReadFloats(Root, TEXT("shape_body"), ShapeBody);
			const TArray<TSharedPtr<FJsonValue>>* JParts = nullptr;
			if (Root->TryGetArrayField(TEXT("parts"), JParts))
			{
				for (const TSharedPtr<FJsonValue>& V : *JParts)
				{
					const TSharedPtr<FJsonObject> P = V->AsObject();
					if (!P.IsValid())
					{
						continue;
					}
					FString Rel;
					P->TryGetStringField(TEXT("path"), Rel);
					if (!Rel.IsEmpty())
					{
						ObjFiles.Add(Rel);
						FString Tex;
						if (P->TryGetStringField(TEXT("texture"), Tex) && !Tex.IsEmpty())
						{
							TexByObj.Add(Rel, Tex);
						}
						FString Blend;
						if (P->TryGetStringField(TEXT("blend"), Blend) && !Blend.IsEmpty())
						{
							BlendByObj.Add(Rel, Blend);
						}
						bool bUnlit = false;
						if (P->TryGetBoolField(TEXT("unlit"), bUnlit) && bUnlit)
						{
							UnlitByObj.Add(Rel, true);
						}
					}
				}
			}
		}
	}
	if (ObjFiles.Num() == 0)
	{
		IFileManager::Get().FindFiles(ObjFiles, *(Dir / TEXT("*.obj")), true, false);
	}
	if (ObjFiles.Num() == 0)
	{
		OutError = FString::Printf(TEXT("no obj in %s"), *Dir);
		return false;
	}

	int32 Extra = 0;
	for (const FString& Name : ObjFiles)
	{
		const FString Lower = Name.ToLower();
		const bool bBody = Lower.Equals(TEXT("body.obj")) || Lower.StartsWith(TEXT("body."));
		const bool bHead = Lower.StartsWith(TEXT("head"));
		const bool bHair = Lower.Contains(TEXT("hair"));
		const bool bAcc = Lower.Contains(TEXT("acc"));
		if (Lower.Contains(TEXT("hair_f_100")) || Lower.Contains(TEXT("nohair")))
		{
			continue;
		}
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NOHAIR")) == TEXT("1")
			&& (bHair || Lower.Contains(TEXT("kami"))))
		{
			continue;
		}
		if (bLoadBodyHeadOnly && !bBody && !bHead && !bHair && !bAcc)
		{
			continue;
		}
		FKKObjMesh Mesh;
		FString Err;
		if (!LoadObjFile(Dir / Name, Mesh, Err))
		{
			continue;
		}
		FKKLoadedPart Part;
		Part.Role = Lower;
		Part.RuntimeSourceGeometry.Reset();
		Part.BaseVertices = Mesh.Vertices;
		Part.Triangles = Mesh.Triangles;
		Part.UVs = Mesh.UVs;
		Part.Colors.Init(FColor::White, Mesh.Vertices.Num());
		if (const FString* TexName = TexByObj.Find(Name))
		{
			Part.TexturePath = Dir / *TexName;
		}
		if (const FString* B = BlendByObj.Find(Name))
		{
			Part.Blend = *B;
		}
		else if (Lower.Contains(TEXT("hair")) || Lower.Contains(TEXT("mayuge")) || Lower.Contains(TEXT("eyeline")))
		{
			Part.Blend = TEXT("masked");
		}
		else
		{
			Part.Blend = TEXT("opaque");
		}
		if (const bool* U = UnlitByObj.Find(Name))
		{
			Part.bUnlit = *U;
		}
		else if (Lower.StartsWith(TEXT("head")) || Lower.Contains(TEXT("hitomi")) || Lower.Contains(TEXT("tang")) || Lower.Contains(TEXT("tooth")))
		{
			Part.bUnlit = true;
		}
		UProceduralMeshComponent* Comp = nullptr;
		if (Lower.Equals(TEXT("head.obj")))
		{
			Comp = HeadMesh;
		}
		else if (Lower.Equals(TEXT("body.obj")))
		{
			Comp = BodyMesh;
			++Extra;
		}
		else
		{
			const FName CompName = *FString::Printf(TEXT("Part_%d"), Extra);
			Comp = NewObject<UKKAffineProceduralMeshComponent>(this, CompName);
            CastChecked<UKKAffineProceduralMeshComponent>(Comp)->bUseAnimatedStreams = true;
			Comp->SetupAttachment(RootComponent);
			Comp->RegisterComponent();
			++Extra;
		}
		UTexture2D* Tex = nullptr;
		if (!Part.TexturePath.IsEmpty())
		{
			FPngPixels Pixels;
			const bool bPix = LoadPngPixels(Part.TexturePath, Pixels);
			const bool bClamp = Part.Role.Contains(TEXT("clothes")) || Lower.Contains(TEXT("eye"));
			Tex = TextureFromPngFile(Part.TexturePath, bClamp);
			if (Tex)
			{
				Tex->AddToRoot();
				Part.Texture = Tex;
				LoadedTextures.Add(Tex);
			}
			Part.Colors.Init(FColor::White, Mesh.Vertices.Num());
			if (!Tex && bPix)
			{
				PaintVerticesFromPng(Pixels, Part.UVs, Part.Colors);
			}
		}
		FillSection(Comp, Mesh.Vertices, Mesh.Triangles, Mesh.UVs, Part.Colors);
		ApplyPartMaterial(Comp, Tex, Part.Blend, Part.bUnlit, Lower.Contains(TEXT("hair")));
		if (Comp)
		{
			Comp->SetCastShadow(false);
		}
		Part.Comp = Comp;
		Parts.Add(MoveTemp(Part));
	}
	if (Parts.Num() == 0)
	{
		OutError = TEXT("failed to load any obj");
		return false;
	}
	return true;
}

void AKKPreviewActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetActorTickEnabled(false);
    if(bPreserveSourceWorld&&StudioCam){StudioCam->Destroy();StudioCam=nullptr;}
	DestroyAllParts();
	Session.Close();
	Super::EndPlay(EndPlayReason);
}

void AKKPreviewActor::SetSharedVectorParameter(FName Name, const FLinearColor& Value)
{
    // Own-graph map materials expose the same probe parameters. Bind a MID per
    // renderer so changing maps/skies never mutates a shared material graph.
    if (!bUseSharedParents)
    {
        TInlineComponentArray<UMeshComponent*> Meshes(this);
        for (UMeshComponent* Mesh:Meshes) for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
        {
            UMaterialInterface* Parent=Mesh->GetMaterial(Slot);FLinearColor Existing;
            if(!Parent||!Parent->GetVectorParameterValue(FMaterialParameterInfo(Name),Existing))continue;
            auto* Dynamic=Cast<UMaterialInstanceDynamic>(Parent);
            if(!Dynamic)
            {
                Dynamic=UMaterialInstanceDynamic::Create(Parent,this);Mesh->SetMaterial(Slot,Dynamic);
                for(FKKLoadedPart& Part:Parts)if(Part.SurfaceMat==Parent && Part.Comp==Mesh)Part.SurfaceMat=Dynamic;
            }
            Dynamic->SetVectorParameterValue(Name,Value);
            Dynamic->SetScalarParameterValue(FName(Name.ToString()+TEXT("__A")),Value.A);
        }
    }

	for (const TArray<TObjectPtr<UMaterialInstanceDynamic>>* List : { &CookedRoleMaterials, &CookedRoleOutlines })
		for (UMaterialInstanceDynamic* M : *List)
		{
			FLinearColor Current;
			if (M && M->GetVectorParameterValue(FMaterialParameterInfo(Name), Current) && !Current.Equals(Value)) M->SetVectorParameterValue(Name, Value);
			// Shared shader graphs transport float4 alpha through a separate
			// scalar parameter; update it along with the RGB vector.
			float Alpha = 0.f;
			const FName AlphaName(*(Name.ToString() + TEXT("__A")));
			if (M && M->GetScalarParameterValue(FMaterialParameterInfo(AlphaName), Alpha) && Alpha != Value.A) M->SetScalarParameterValue(AlphaName, Value.A);
		}
}

void AKKPreviewActor::SetSharedTextureParameter(FName Name,UTexture* Value)
{
    if(!bUseSharedParents)
    {
        TInlineComponentArray<UMeshComponent*> Meshes(this);
        for(UMeshComponent* Mesh:Meshes)for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
        {
            UMaterialInterface* Parent=Mesh->GetMaterial(Slot);UTexture* Current=nullptr;
            if(!Parent||!Parent->GetTextureParameterValue(FMaterialParameterInfo(Name),Current))continue;
            auto* M=Cast<UMaterialInstanceDynamic>(Parent);
            if(!M){M=UMaterialInstanceDynamic::Create(Parent,this);Mesh->SetMaterial(Slot,M);for(FKKLoadedPart& Part:Parts)if(Part.SurfaceMat==Parent && Part.Comp==Mesh)Part.SurfaceMat=M;}
            UTexture* Target=Value;if(!Target && M->Parent)M->Parent->GetTextureParameterValue(FMaterialParameterInfo(Name),Target);
            if(Target && Target!=Current)M->SetTextureParameterValue(Name,Target);
        }
    }

    for(const TArray<TObjectPtr<UMaterialInstanceDynamic>>* List:{&CookedRoleMaterials,&CookedRoleOutlines})
        for(UMaterialInstanceDynamic* M:*List)
        {
            UTexture* Current=nullptr;if(!M||!M->GetTextureParameterValue(FMaterialParameterInfo(Name),Current))continue;
            UTexture* Target=Value;
            if(!Target&&M->Parent)M->Parent->GetTextureParameterValue(FMaterialParameterInfo(Name),Target);
            if(Target&&Target!=Current)M->SetTextureParameterValue(Name,Target);
        }
}

void AKKPreviewActor::BindSourceReflectionProbes(TFunctionRef<UTexture*(int32,FLinearColor&)> Resolve)
{
    TInlineComponentArray<UMeshComponent*> Meshes(this);
    for(UMeshComponent* Mesh:Meshes)
    {
        int32 Node=-1;
        for(const FName Tag:Mesh->ComponentTags){const FString T=Tag.ToString();const FString Prefix=TEXT("OKReflectionProbe=");if(T.StartsWith(Prefix))Node=FCString::Atoi(*T.Mid(Prefix.Len()));}
        FLinearColor HDR(1,1,0,0);UTexture* Texture=nullptr;bool Resolved=false;
        for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
        {
            UMaterialInterface* Parent=Mesh->GetMaterial(Slot);UTexture* Current=nullptr;
            if(!Parent||!Parent->GetTextureParameterValue(FMaterialParameterInfo(TEXT("unity_SpecCube0")),Current))continue;
            if(!Resolved){Texture=Resolve(Node,HDR);Resolved=true;}
            auto* M=Cast<UMaterialInstanceDynamic>(Parent);
            if(!M){M=UMaterialInstanceDynamic::Create(Parent,this);Mesh->SetMaterial(Slot,M);for(FKKLoadedPart& Part:Parts)if(Part.SurfaceMat==Parent&&Part.Comp==Mesh)Part.SurfaceMat=M;}
            UTexture* Target=Texture;if(!Target&&M->Parent)M->Parent->GetTextureParameterValue(FMaterialParameterInfo(TEXT("unity_SpecCube0")),Target);
            if(Target&&Current!=Target)M->SetTextureParameterValue(TEXT("unity_SpecCube0"),Target);
            M->SetVectorParameterValue(TEXT("unity_SpecCube0_HDR"),HDR);
        }
    }
}

void AKKPreviewActor::Destroyed()
{
	// Editor actors may be destroyed before BeginPlay. Keep cleanup idempotent
	// so both that path and normal EndPlay release only this actor's resources.
	DestroyAllParts();
	Session.Close();
	Super::Destroyed();
}

void AKKPreviewActor::DestroyAllParts()
{
	StopRenderTextureCaptures();
 CookedRoleMaterials.Reset();CookedRoleOutlines.Reset();
	bSessionLoadComplete = false; SessionLoadNextPart = 0; SessionLoadWhiteRamp = nullptr;
	SessionLoadState.Reset();
	SessionLoadSeenHead.Reset(); SessionLoadTranslucentDepth.Reset();
	bPlaybackMaterialsReady=false;
	bPlaybackRenderFencePending=false;
	StopSpeech();SpeechLines.Reset();SpeechPrevious=0.f;
	RigidNeutralBones.Reset();RigidNeutralAllBones.Reset();
	if (ToonShadowCapture)
	{
		ToonShadowCapture->DestroyComponent();
		ToonShadowCapture = nullptr;
	}
	ToonShadowMaterials.Reset();
    ToonShadowBoundIndex=INDEX_NONE;ToonShadowBoundMaterialCount=0;
	ToonShadowTargets.Reset();
	ToonShadowReadIndex = 0;
	for (FKKLoadedPart& Part : Parts)
	{
		if (Part.Comp && Part.Comp != BodyMesh && Part.Comp != HeadMesh)
		{
			Part.Comp->DestroyComponent();
		}
	}
	Parts.Reset();
	// BodyMesh/HeadMesh are persistent: just clear their sections so the next
	// outfit refills them instead of stacking new sections.
	if (BodyMesh)
	{
		BodyMesh->ClearAllMeshSections();
	}
	if (HeadMesh)
	{
		HeadMesh->ClearAllMeshSections();
		HeadMesh->SetRelativeTransform(FTransform::Identity);
	}
	for (UPoseableMeshComponent* F : SkelFollowers)
	{
		if (F)
		{
			F->DestroyComponent();
		}
	}
	for (const FOkMouthSlot& Slot : MouthSlots)
	{
		if (Slot.OutlineComp) Slot.OutlineComp->DestroyComponent();
		if (Slot.Comp)
		{
			Slot.Comp->DestroyComponent();
		}
	}
	SkelFollowers.Reset();
	MouthSlots.Reset();
    // Follower destruction can calculate bounds while detaching from its parent.
    // Keep the leader asset/bone map valid until every follower has unregistered.
	if (BodySkel)
	{
		BodySkel->SetSkinnedAssetAndUpdate(nullptr);
		HideComputeSkel(BodySkel);
	}

	PartMorphWeights.Reset();
	GenitalPrimitives.Reset();
	bGenitalVisible = false;
	VaginaMorphPart = INDEX_NONE;
	VaginaMorphName = NAME_None;
	bVaginaMorphResolved = false;
	bVaginaOpeningLogged = false;
	for (UProceduralMeshComponent* P : SkelPmcs)
	{
		if (P && P != BodyMesh && P != HeadMesh)
		{
			P->DestroyComponent();
		}
	}
	SkelPmcs.Reset();
	SharedSkel = nullptr;
	for (UTexture2D* T : LoadedTextures)
	{
		if (T)
		{
			T->RemoveFromRoot();
		}
	}
	LoadedTextures.Reset();
	// The host's ramp (RampOverride) stays the ramp of the next load; otherwise the load picks the display ramp.
	ToonRampTex = RampOverride;
}

static void OKDisableRealtimeSky(UWorld* World)
{
	if (!World)
	{
		return;
	}
	if (GEngine)
	{
		GEngine->Exec(World, TEXT("r.SkyLight.RealTimeReflectionCapture 0"));
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		if (USkyLightComponent* C = It->GetLightComponent())
		{
			C->SetMobility(EComponentMobility::Movable);
			C->SetRealTimeCapture(false);
			C->SetCaptureIsDirty();
		}
	}
}

static bool OKIsGenitalMesh(const FString& Lower)
{
	return Lower == TEXT("o_dankon") || Lower == TEXT("o_dan_f")
		|| Lower == TEXT("cm_o_dan00") || Lower == TEXT("cm_o_dan_f");
}

struct FOKSessionTextureJobs {
 struct FJob{TArray<uint8> Owned;const uint8* Bytes=nullptr;int32 Length=0;bool Mips=false;};
 TArray<FJob> Jobs;double GatherSeconds=0;
};

bool AKKPreviewActor::FirstLoadOption(const TCHAR* Name)
{
 // Unset: the measured defaults (B1, C3; A2, A3 on the native side). OK_FIRSTLOAD=none turns every option off; any list
 // replaces the defaults.
 static const TArray<FString> Options=[]{TArray<FString> Out;FString Value;if(!FPlatformMisc::GetEnvironmentVariable(TEXT("OK_FIRSTLOAD")).IsEmpty())Value=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_FIRSTLOAD"));else Value=TEXT("B1,C3");
  Value.ParseIntoArray(Out,TEXT(","));for(FString& O:Out)O.TrimStartAndEndInline();return Out;}();
 return Options.Contains(Name)||Options.Contains(TEXT("all"));
}

static void CollectTextures(FOkSession& Source,bool bPrepareSkeleton,bool bCopy,FOKSessionTextureJobs& Out)
{
 if(OKSharedParents()){
 const double MaterialStart=FPlatformTime::Seconds();
 FString MaterialError;if(!Source.PrepareRuntimeMaterials(MaterialError))UE_LOG(LogTemp,Error,TEXT("High model material preparation: %s"),*MaterialError);
 if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE"))==TEXT("1"))UE_LOG(LogTemp,Display,TEXT("OK runtime material capture ms=%.1f"),(FPlatformTime::Seconds()-MaterialStart)*1000.);
 }
 if(bPrepareSkeleton){
  TRACE_CPUPROFILER_EVENT_SCOPE(KK_PreviewNativeReferencePrepare);
  const double Start=FPlatformTime::Seconds();
  const int32 Bones=Source.SkelBoneCount();
  UE_LOG(LogTemp,Display,TEXT("OK preview native reference prepared bones=%d worker=%d ms=%.3f"),Bones,!IsInGameThread(),(FPlatformTime::Seconds()-Start)*1000.);
 }
 const double GatherStart=FPlatformTime::Seconds();
 // The session's own bytes (copied when the session may change before they are decoded). Repeats of one slot are
 // the same pointer; equal content at another address is merged here (equal length, then equal bytes: memcmp is
 // far cheaper than a hash, and only same-length candidates are compared), so it is not decoded twice in parallel.
 auto& Inputs=Out.Jobs;TMultiMap<int32,int32> ByLength;
 auto Add=[&](const uint8* Bytes,int32 Length,bool Mips){
  if(!Bytes||Length<16)return;
  TArray<int32> Same;ByLength.MultiFind(Length,Same);
  for(int32 I:Same)if(Inputs[I].Mips==Mips&&(Inputs[I].Bytes==Bytes||!FMemory::Memcmp(Inputs[I].Bytes,Bytes,Length)))return;
  FOKSessionTextureJobs::FJob Job;Job.Length=Length;Job.Mips=Mips;
  if(bCopy){Job.Owned.Append(Bytes,Length);Job.Bytes=Job.Owned.GetData();}else Job.Bytes=Bytes;
  ByLength.Add(Length,Inputs.Add(MoveTemp(Job)));
 };
 for(int32 Part=0;Part<Source.PartCount();++Part){
  OkMeshView View{};if(!Source.PartAt(Part,View))continue;
  OkTextureSettings Main{};Add(View.png,View.png_len,Source.TextureSettings(Part,"_MainTex",Main)&&Main.mip_policy==1);
  Add(View.ramp_png,View.ramp_png_len,true);
  Add(View.alpha_mask,View.alpha_mask_len,false);Add(View.line_mask,View.line_mask_len,false);
  Add(View.normal_map,View.normal_map_len,false);Add(View.normal_detail,View.normal_detail_len,false);Add(View.normal_mask,View.normal_mask_len,false);
  Add(View.overtex_png,View.overtex_png_len,false);Add(View.overtex2_png,View.overtex2_png_len,false);
  OkTextureSettings Detail{},Gloss{},Color{};
  Add(View.color_mask,View.color_mask_len,Source.TextureSettings(Part,"_ColorMask",Color)&&Color.mip_policy==1);
  Add(View.detail_mask,View.detail_mask_len,Source.TextureSettings(Part,"_DetailMask",Detail)&&Detail.mip_policy==1);
  Add(View.hair_gloss,View.hair_gloss_len,Source.TextureSettings(Part,"_HairGloss",Gloss)&&Gloss.mip_policy==1);
  for(int32 Eye=0;Eye<2;++Eye)Add(View.eye_textures[Eye],View.eye_texture_lens[Eye],false);
  auto AddSlot=[&](const char* Slot){
   const uint8* Bytes=nullptr;int32 Length=0;OkTextureSettings Settings{};
   if(Source.ShaderTexture(Part,Slot,Bytes,Length))Add(Bytes,Length,Source.TextureSettings(Part,Slot,Settings)&&Settings.mip_policy==1);
  };
  // Replay the renderer's actual binding list. Skin/eye/hair use the View
  // fields above, not every authored extra slot (which may be unused).
  if(View.shader_kind==2 || View.shader_kind==8 || View.shader_kind==9 || View.shader_kind==11){
   for(const FString& Slot:View.shader_kind==2?OKForgeOpaqueTextureNames():View.shader_kind==8?OKForgeItemTextureNames():View.shader_kind==11?OKForgeAlphaTextureNames():OKMainItemPlusTextureNames())AddSlot(TCHAR_TO_UTF8(*Slot));
  }else{
   if(View.shader_kind==1 || View.shader_kind==10)AddSlot("_AnotherRamp");
   for(const char* Slot:{"_overtex1","_overtex2","_overtex3"})AddSlot(Slot);
  }
 }
 // Largest first, each texture its own work item: a fixed lane split left the big maps on a few threads.
 Inputs.Sort([](const FOKSessionTextureJobs::FJob& A,const FOKSessionTextureJobs::FJob& B){return A.Length>B.Length;});
 Out.GatherSeconds=FPlatformTime::Seconds()-GatherStart;
}

TSharedRef<FOKSessionTextureJobs,ESPMode::ThreadSafe> AKKPreviewActor::CollectSessionTextures(FOkSession& Source,bool bPrepareSkeleton)
{
 auto Jobs=MakeShared<FOKSessionTextureJobs,ESPMode::ThreadSafe>();CollectTextures(Source,bPrepareSkeleton,true,*Jobs);return Jobs;
}

void AKKPreviewActor::PrecacheTextureJobs(const FOKSessionTextureJobs& Jobs)
{
#if !WITH_EDITOR
 // OK_FIRSTLOAD=C6: cooked parts bind the prepared material textures, so nothing reads these decodes.
 if(FirstLoadOption(TEXT("C6"))){
  if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE"))==TEXT("1"))UE_LOG(LogTemp,Display,TEXT("OK texture precache inputs=%d gather_ms=%.1f decode_ms=0.0 skipped=cooked"),Jobs.Jobs.Num(),Jobs.GatherSeconds*1000.);
  return;
 }
#endif
 const double DecodeStart=FPlatformTime::Seconds();
 ParallelFor(Jobs.Jobs.Num(),[&](int32 I){OKPrecacheRuntimePngView(Jobs.Jobs[I].Bytes,Jobs.Jobs[I].Length,Jobs.Jobs[I].Mips);},EParallelForFlags::Unbalanced);
 if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE"))==TEXT("1"))
  UE_LOG(LogTemp,Display,TEXT("OK texture precache inputs=%d gather_ms=%.1f decode_ms=%.1f"),Jobs.Jobs.Num(),Jobs.GatherSeconds*1000.,(FPlatformTime::Seconds()-DecodeStart)*1000.);
}

void AKKPreviewActor::PrecacheSessionTextures(FOkSession& Source, bool bPrepareSkeleton)
{
 FOKSessionTextureJobs Jobs;CollectTextures(Source,bPrepareSkeleton,false,Jobs);PrecacheTextureJobs(Jobs);
}

struct AKKPreviewActor::FSessionLoadState { FOKToonShadowParameters Shadow; FRotator ShadowRotation; int32 SetupStep=0; };

bool AKKPreviewActor::ApplyHeadTextureSession(const FOkSession& Source,FString& Error)
{
 check(IsInGameThread());TArray<int32> Changed;
 if(!Session.HeadTextureChanges(Source,Changed,false,Error))return false;
 struct FUpdate{FKKLoadedPart* Part;UMaterialInstanceDynamic* Material;UTexture2D* Main;UTexture2D* Up;UTexture2D* Down;};
 TArray<FUpdate> Updates;
 for(int32 Index:Changed){
  FKKLoadedPart* Part=Parts.FindByPredicate([Index](const auto& P){return P.SessionIndex==Index;});
  if(!Part){Error=TEXT("Appearance renderer is absent");return false;}
  auto* Material=Cast<UMaterialInstanceDynamic>(Part->SurfaceMat.Get());UTexture* Existing=nullptr;
  if(!Material||!Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("KKAppearanceMain")),Existing)){
   Error=TEXT("Appearance material requires full preparation");return false;
  }
  OkMeshView View{};if(!Source.PartAt(Index,View)||!View.png||View.png_len<=0){Error=TEXT("Appearance source texture is absent");return false;}
  OkTextureSettings Settings{};const bool Native=Source.TextureSettings(Index,"_MainTex",Settings);
  auto* Main=TextureFromPngBytes(View.png,View.png_len,true,Native?&Settings:nullptr,Native);
  auto* Up=View.eye_texture_lens[0]>0?TextureFromPngBytes(View.eye_textures[0],View.eye_texture_lens[0],true):OKAppearanceEmptyTexture();
  auto* Down=View.eye_texture_lens[1]>0?TextureFromPngBytes(View.eye_textures[1],View.eye_texture_lens[1],true):OKAppearanceEmptyTexture();
  if(!Main||!Up||!Down){Error=TEXT("Appearance texture preparation failed");return false;}
  Updates.Add({Part,Material,Main,Up,Down});
 }
 // Commit after every renderer and new texture has been validated. Native
 // gaze/morph/physics state stays live; no mesh or shader is recreated.
 if(!Session.HeadTextureChanges(Source,Changed,true,Error))return false;
 for(auto& U:Updates){
  U.Material->SetTextureParameterValue(TEXT("KKAppearanceMain"),U.Main);
  U.Material->SetTextureParameterValue(TEXT("KKAppearanceEyeUp"),U.Up);
  U.Material->SetTextureParameterValue(TEXT("KKAppearanceEyeDown"),U.Down);
  U.Part->Texture=U.Main;LoadedTextures.AddUnique(U.Main);LoadedTextures.AddUnique(U.Up);LoadedTextures.AddUnique(U.Down);
 }
 UE_LOG(LogTemp,Display,TEXT("Appearance texture commit parts=%d geometry_rebuilt=0 shader_rebuilt=0"),Changed.Num());
 return true;
}

bool AKKPreviewActor::ApplyHeadTextureCandidate(FOkAppearanceContext& Context,FOkAppearanceCandidate& Candidate,FString& Error)
{
 check(IsInGameThread());
 if(!Context.Commit(Candidate,Session,false,Error))return false;
 FString Json;TSharedPtr<FJsonObject> Info;
 if(!Candidate.Info(Json)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Info)||!Info){Error=TEXT("Invalid appearance candidate metadata");return false;}
 const TArray<TSharedPtr<FJsonValue>>* Changed=nullptr;
 if(!Info->TryGetArrayField(TEXT("parts"),Changed)){Error=TEXT("Missing appearance candidate parts");return false;}
 struct FUpdate{FKKLoadedPart* Part;UMaterialInstanceDynamic* Material;UTexture2D* Main;UTexture2D* Up;UTexture2D* Down;};
 TArray<FUpdate> Updates;
 for(const auto& Value:*Changed){
  auto Row=Value->AsObject();if(!Row){Error=TEXT("Invalid appearance candidate renderer");return false;}
  const int32 Index=Row->GetIntegerField(TEXT("index"));
  FKKLoadedPart* Part=Parts.FindByPredicate([Index](const auto& P){return P.SessionIndex==Index;});
  if(!Part){Error=TEXT("Appearance renderer is absent");return false;}
  auto* Material=Cast<UMaterialInstanceDynamic>(Part->SurfaceMat.Get());UTexture* Existing=nullptr;
  if(!Material||!Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("KKAppearanceMain")),Existing)){Error=TEXT("Appearance material requires full preparation");return false;}
  TArray<uint8> MainBytes,UpBytes,DownBytes;
  if(!Candidate.Texture(Index,0,MainBytes)||!Candidate.Texture(Index,1,UpBytes)||!Candidate.Texture(Index,2,DownBytes)||MainBytes.IsEmpty()){Error=TEXT("Appearance candidate pixels are absent");return false;}
  OkTextureSettings Settings{};bool Native=false;
  const TSharedPtr<FJsonObject>* Samplers=nullptr;
  const TArray<TSharedPtr<FJsonValue>>* MainSampler=nullptr;
  if(Row->TryGetObjectField(TEXT("runtime_textures"),Samplers)&&(*Samplers)->TryGetArrayField(TEXT("_MainTex"),MainSampler)&&MainSampler->Num()==5){
   Settings.filter=int32((*MainSampler)[0]->AsNumber());Settings.wrap_u=int32((*MainSampler)[1]->AsNumber());Settings.wrap_v=int32((*MainSampler)[2]->AsNumber());
   Settings.srgb=int32((*MainSampler)[3]->AsNumber());Settings.mip_policy=int32((*MainSampler)[4]->AsNumber());Native=true;
  }
  const double TextureBegin=FPlatformTime::Seconds();
  auto* Main=TextureFromPngBytes(MainBytes.GetData(),MainBytes.Num(),true,Native?&Settings:nullptr,Native);
  auto* Up=UpBytes.IsEmpty()?OKAppearanceEmptyTexture():TextureFromPngBytes(UpBytes.GetData(),UpBytes.Num(),true);
  auto* Down=DownBytes.IsEmpty()?OKAppearanceEmptyTexture():TextureFromPngBytes(DownBytes.GetData(),DownBytes.Num(),true);
  if(!Main||!Up||!Down){Error=TEXT("Appearance texture preparation failed");return false;}
  UE_LOG(LogTemp,Display,TEXT("Appearance candidate texture part=%d size=%dx%d bytes=%d prepare_ms=%.3f"),Index,Main->GetSizeX(),Main->GetSizeY(),MainBytes.Num(),(FPlatformTime::Seconds()-TextureBegin)*1000.);
  Updates.Add({Part,Material,Main,Up,Down});
 }
 if(!Context.Commit(Candidate,Session,true,Error))return false;
 for(auto& U:Updates){
  U.Material->SetTextureParameterValue(TEXT("KKAppearanceMain"),U.Main);U.Material->SetTextureParameterValue(TEXT("KKAppearanceEyeUp"),U.Up);U.Material->SetTextureParameterValue(TEXT("KKAppearanceEyeDown"),U.Down);
  U.Part->Texture=U.Main;LoadedTextures.AddUnique(U.Main);LoadedTextures.AddUnique(U.Up);LoadedTextures.AddUnique(U.Down);
 }
 UE_LOG(LogTemp,Display,TEXT("Appearance candidate committed parts=%d geometry_rebuilt=0 shader_rebuilt=0"),Updates.Num());return true;
}

bool AKKPreviewActor::LoadSession(FString& OutError)
{
 return LoadSessionBatch(OutError, MAX_int32, false);
}

bool AKKPreviewActor::BeginSessionLoad(FString& OutError)
{
 return LoadSessionBatch(OutError, 0, false);
}

bool AKKPreviewActor::ContinueSessionLoad(FString& OutError)
{
 if (bSessionLoadComplete) return true;
 return LoadSessionBatch(OutError, 1, true);
}

#include "KKPreviewRuntimeMaterials.inl"

struct AKKPreviewActor::FPartBuildOutput {
    TArray<FKKLoadedPart>& Parts;
    TArray<TObjectPtr<UTexture2D>>& Textures;
    TArray<TObjectPtr<UPoseableMeshComponent>>& Followers;
    TArray<FOkMouthSlot>& MouthSlots;
    TArray<TObjectPtr<UPrimitiveComponent>>& GenitalPrimitives;
    TArray<TObjectPtr<UMaterialInstanceDynamic>>& ShadowMaterials;
    TArray<TObjectPtr<UMaterialInstanceDynamic>>& CookedMaterials;
    TArray<TObjectPtr<UMaterialInstanceDynamic>>& CookedOutlines;
    TObjectPtr<UTexture2D>& Ramp;
    TSet<FString>& SeenHead;
    TMap<FString,int32>& TranslucentDepth;
    int32& Extra;
    int32& NextPart;
    UTexture2D* WhiteRamp;
    FOKToonShadowParameters& Shadow;
    // Stage mode builds an exact-slot candidate without touching live state:
    // components get unique names and stay unregistered, session indices are
    // offset to their published range, and socket attachment is deferred.
    bool bStage=false;
    int32 SessionOffset=0;
    TArray<TObjectPtr<UActorComponent>>* Staged=nullptr;
    TArray<TPair<TObjectPtr<USceneComponent>,FName>>* PendingSockets=nullptr;
};

bool AKKPreviewActor::BuildSessionPartRange(FOkSession& Source,FPartBuildOutput& Output,
    int32 Begin,int32 End,FString& OutError)
{
    if(!Source.IsOpen()||Begin<0||End<Begin||End>Source.PartCount()){
        OutError=TEXT("Invalid renderer preparation range");return false;
    }
    auto& ShadowParams=Output.Shadow;
    if(Output.bStage&&(!Output.Staged||!Output.PendingSockets)){OutError=TEXT("Invalid renderer staging output");return false;}
    // Live loads keep their historical names; staged candidates must never
    // reuse (and thereby replace) a live UObject with the same fixed name.
    auto ComponentName=[&](UClass* Class,const FString& Base){
        return Output.bStage?MakeUniqueObjectName(this,Class,FName(*(TEXT("Stage_")+Base))):FName(*Base);
    };
    auto Install=[&](UActorComponent* Component){
        if(Output.bStage)Output.Staged->Add(Component);else Component->RegisterComponent();
    };
	for (int32 i = Begin; i < End; ++i)
	{
		const int32 Published = i + Output.SessionOffset;
		Output.NextPart = i + 1;
		OkMeshView View;
		FMemory::Memzero(View);
		if (!Source.PartAt(i, View) || !View.positions || View.n_verts <= 0)
		{
			continue;
		}
		if (!View.render_visible)
		{
			UE_LOG(LogTemp, Display, TEXT("OK part source inactive name=%s"),
				View.name ? UTF8_TO_TCHAR(View.name) : TEXT(""));
			continue;
		}
		ShadowParams.TripoPBR = nullptr; // Clear per-part state before clothing.
		ShadowParams.AmbientDepth = View.shadow_depth;
		// Optional scene input for comparisons with a saved Studio scene.
		// This applies to every shader family; card/material colours stay sourced.
		bool bStudioCompare = false;
		float StudioShadowDepth;
		if (FParse::Value(FCommandLine::Get(),TEXT("OKStudioShadowDepth="),StudioShadowDepth) && FMath::IsFinite(StudioShadowDepth))
		{
			ShadowParams.AmbientDepth = 1.f-FMath::Clamp(StudioShadowDepth,0.f,1.f);
			bStudioCompare = true;
		}
        ShadowParams.bEnableShadows=bCharacterDisplayShadows;
		// Source shadow_depth is Studio's 1-GraphicData.shadowDepth. The character
		// maker shader alpha is the raw depth. Do not replace it with 1: Koikano
		// turns alpha 1 into a white indirect and washes the body.
		ShadowParams.AmbientAlpha = bStudioCompare ? ShadowParams.AmbientDepth : 1.f-FMath::Clamp(ShadowParams.AmbientDepth,0.f,1.f);
		ShadowParams.AmbientRgb = bStudioCompare
			? FLinearColor(128.f/255.f,128.f/255.f,128.f/255.f,1.f)
			: FLinearColor(0.019f,0.027f,0.039f,1.f);
		ShadowParams.NativeItem = nullptr;
		ShadowParams.NativeSkin = nullptr;
        // Shared-parent instances (OKPrepareCookedRole), in every build.
        ShadowParams.CookedMaterial=Output.CookedMaterials.IsValidIndex(i)?Output.CookedMaterials[i].Get():nullptr;
        ShadowParams.CookedOutline=Output.CookedOutlines.IsValidIndex(i)?Output.CookedOutlines[i].Get():nullptr;
        // A part outlined by its own graph (no shared parent: OK_SHARED_PARENTS=0, OK Studio maps) extrudes by its
        // family's installed outline vertex program, like the shared outline parents (ok.OutlineApprox keeps the
        // approximate width). Its description carries the resolved values the program reads.
        ShadowParams.OutlinePart.Reset();
        ShadowParams.OutlineGlobals=StudioShaderGlobals;
        if(!(ShadowParams.CookedMaterial&&OKSharedParents())&&OutlineEnabled()&&bItemOutline&&OKRuntimeOutlineApprox()<.5f)
        {
            auto Described=MakeShared<FKKRuntimeMaterialPart>();FString DescribeError;
            if(Source.DescribeRuntimeMaterialPart(i,*Described,DescribeError))ShadowParams.OutlinePart=Described;
        }
        // OK_FIRSTLOAD=C6 (cooked games): the cooked material already binds this part's prepared textures, and
        // ApplyPartMaterial uses nothing else, so no part texture is decoded again; Part.Texture/Overtex are the
        // cooked material's own.
        bool bCookedTextures=false;
#if !WITH_EDITOR
        bCookedTextures=ShadowParams.CookedMaterial&&FirstLoadOption(TEXT("C6"));
        auto CookedTexture=[&](const TCHAR* Name)->UTexture2D*{UTexture* T=nullptr;return ShadowParams.CookedMaterial&&ShadowParams.CookedMaterial->GetTextureParameterValue(FMaterialParameterInfo(Name),T)?Cast<UTexture2D>(T):nullptr;};
#endif
		ShadowParams.LineWidth = View.line_width;
		ShadowParams.LineDepth = View.line_depth;
		ShadowParams.LineTexOn = View.line_tex_on;
		ShadowParams.SkinLineWidth = View.skin_line_width;
		ShadowParams.bKoikanoSkinOutline = View.shader_kind == 5;
		ShadowParams.NormalDetailScale = View.normal_detail_scale;
		ShadowParams.FaceNormal = View.face_normal;
		ShadowParams.FaceShadow = View.face_shadow;
		ShadowParams.KanoVerShift = View.kano_ver_shift;
		ShadowParams.ShadowExtendAnother = View.shadow_extend_another;
		ShadowParams.MainTexST = FLinearColor(View.main_tex_st[0], View.main_tex_st[1], View.main_tex_st[2], View.main_tex_st[3]);
		// A gag eye whose eye set animates it carries the material values (eye_set::apply_gag_animation).
		{
			FLinearColor Tile, Speed, Width, Angle;
			ShadowParams.bGagAnimation = Source.MaterialValue(i, "_TileAnimation", Tile) && Source.MaterialValue(i, "_SizeSpeed", Speed)
				&& Source.MaterialValue(i, "_SizeWidth", Width) && Source.MaterialValue(i, "_AngleSpeed", Angle);
			ShadowParams.GagTileAnimation = ShadowParams.bGagAnimation ? Tile : FLinearColor(1.f, 1.f, 1.f, 0.f);
			ShadowParams.GagMotion = ShadowParams.bGagAnimation ? FLinearColor(Speed.R, Width.R, Angle.R, 0.f) : FLinearColor::Transparent;
			if (ShadowParams.bGagAnimation) UE_LOG(LogTemp, Display, TEXT("OK gag eye animation part=%d tile=(%.2f,%.2f,%.2f) size=(%.2f,%.2f) angle=%.2f"), i, Tile.R, Tile.G, Tile.B, Speed.R, Width.R, Angle.R);
		}
		ShadowParams.AlphaMaskST = FLinearColor(View.alpha_mask_st[0], View.alpha_mask_st[1], View.alpha_mask_st[2], View.alpha_mask_st[3]);
		ShadowParams.NormalMapST = FLinearColor(View.normal_map_st[0], View.normal_map_st[1], View.normal_map_st[2], View.normal_map_st[3]);
		ShadowParams.NormalDetailST = FLinearColor(View.normal_detail_st[0], View.normal_detail_st[1], View.normal_detail_st[2], View.normal_detail_st[3]);
		ShadowParams.NormalDetailTex = nullptr;
		ShadowParams.NormalMaskST = FLinearColor(View.normal_mask_st[0], View.normal_mask_st[1], View.normal_mask_st[2], View.normal_mask_st[3]);
		ShadowParams.DetailMaskST = FLinearColor(View.detail_mask_st[0], View.detail_mask_st[1], View.detail_mask_st[2], View.detail_mask_st[3]);
		ShadowParams.LineMaskST = FLinearColor(View.line_mask_st[0], View.line_mask_st[1], View.line_mask_st[2], View.line_mask_st[3]);
		ShadowParams.HairGlossST = FLinearColor(View.hair_gloss_st[0], View.hair_gloss_st[1], View.hair_gloss_st[2], View.hair_gloss_st[3]);
		// Render queue / alpha mode does not identify a shader program. Material
		// Editor can retain main_hair_front while changing its render state.
		ShadowParams.bHairFrontProgram = Source.MaterialShaderName(i).EndsWith(TEXT("_front"), ESearchCase::IgnoreCase);
		ShadowParams.bStudioAddProgram = Source.MaterialShaderName(i).Equals(TEXT("Shader Forge/main_item_studio_add"), ESearchCase::IgnoreCase);
		ShadowParams.AnotherRampTex = Output.WhiteRamp;
		ShadowParams.AnotherRampST = FLinearColor(1.f, 1.f, 0.f, 0.f);
		ShadowParams.RampGST = FLinearColor(1.f, 1.f, 0.f, 0.f);
		if (View.shader_kind == 1 || View.shader_kind == 10 || View.shader_kind == 8)
		{
			Source.MaterialValue(i, "_RampG_ST", ShadowParams.RampGST);
			Source.MaterialValue(i, "_AnotherRamp_ST", ShadowParams.AnotherRampST);
		}
		ShadowParams.Texcoord2ST = FLinearColor(View.texcoord2_st[0], View.texcoord2_st[1], View.texcoord2_st[2], View.texcoord2_st[3]);
		ShadowParams.Texcoord3ST = FLinearColor(View.texcoord3_st[0], View.texcoord3_st[1], View.texcoord3_st[2], View.texcoord3_st[3]);
		ShadowParams.OverColor2 = FLinearColor(View.overcolor2[0], View.overcolor2[1], View.overcolor2[2], View.overcolor2[3]);
		ShadowParams.NipSize = View.nip_size;
		ShadowParams.NipSpecular = View.nip_specular;
		ShadowParams.NipToggle = View.nip_toggle;
		ShadowParams.Tex1Mask = View.tex1_mask;
		ShadowParams.SpecHeight = View.spec_height;
		if (View.shader_kind == 1 || View.shader_kind == 10)
		{
			FLinearColor RimValue;
			ShadowParams.HairRimStrength = Source.MaterialValue(i, "_rimV", RimValue)
				? RimValue.R : 0.75f;
			ShadowParams.HairRimPower = Source.MaterialValue(i, "_rimpower", RimValue)
				? RimValue.R : 0.2f;
		}
		ShadowParams.SpecularPower = View.specular_power;
		ShadowParams.SpecularPowerNail = View.specular_power_nail;
		ShadowParams.SpecularColor = FLinearColor(View.specular_color[0], View.specular_color[1], View.specular_color[2], View.specular_color[3]);
		ShadowParams.NoTexSpecular = View.no_tex_specular;
		ShadowParams.Metallic = View.metallic;
		ShadowParams.Smoothness = View.smoothness;
        ShadowParams.bHairSun = View.shader_kind == 10;
        ShadowParams.bHairOld = ShadowParams.bHairSun && Source.MaterialKeyword(i,"_OLDHAIR_ON");
        ShadowParams.bHairReverseColors = ShadowParams.bHairSun && Source.MaterialKeyword(i,"_REVERSECOLOR01_ON");
        FLinearColor HairToggle;
        ShadowParams.bHairColor2 = !Source.MaterialValue(i,"_Color2onoff",HairToggle) || HairToggle.R!=0.f;
        ShadowParams.bHairColor3 = !Source.MaterialValue(i,"_Color3onoff",HairToggle) || HairToggle.R!=0.f;
		ShadowParams.bItemDraw = View.item_draw != 0;
		ShadowParams.EyeDrawKind = View.eye_draw;
		ShadowParams.EyeTextures[0] = ShadowParams.EyeTextures[1] = nullptr; ShadowParams.EyeExpressionTex = nullptr;
		ShadowParams.bItemFourthColor = View.item_draw == 4;
		ShadowParams.ItemAlpha = View.item_draw != 0 ? FMath::Clamp(ItemAlpha, 0.f, 1.f) : 1.f;
		ShadowParams.ColorMaskTex = nullptr;
		ShadowParams.ColorMaskST = FLinearColor(View.color_mask_st[0], View.color_mask_st[1], View.color_mask_st[2], View.color_mask_st[3]);
		for (int32 ColorIndex = 0; ColorIndex < 4; ++ColorIndex)
		{
			float C[4] = { View.item_colors[ColorIndex][0], View.item_colors[ColorIndex][1], View.item_colors[ColorIndex][2], View.item_colors[ColorIndex][3] };
			// Item colours: the item_draw colour mix, and the tint of Studio's additive light effects (texture x colour).
			const bool bAdditivePart = View.blend && FCStringAnsi::Stricmp(View.blend, "additive") == 0;
			if ((ShadowParams.bItemDraw || bAdditivePart) && ItemColorOverrides.IsValidIndex(ColorIndex) && ItemColorOverrides[ColorIndex].A >= 0.f) { const FLinearColor& O = ItemColorOverrides[ColorIndex]; C[0] = O.R; C[1] = O.G; C[2] = O.B; C[3] = O.A; }
			auto Linear = [](float V) { return V <= 0.04045f ? V / 12.92f : FMath::Pow((V + 0.055f) / 1.055f, 2.4f); };
			ShadowParams.ItemColors[ColorIndex] = FLinearColor(Linear(C[0]), Linear(C[1]), Linear(C[2]), C[3]);
		}
		if (!Output.Ramp && View.ramp_png && View.ramp_png_len > 16)
		{
			// Native Studio _RampG is a linear 256x4 Texture2D with nine mips,
			// Bilinear filtering and Clamp on both axes (HairRamp312Native186).
			// Its full texture agrees with the selected game mt_ramp after the
			// probe's sRGB PNG readback transfer.
			const OkTextureSettings RampSettings{1, 1, 1, 0, 1};
			Output.Ramp = TextureFromPngBytes(View.ramp_png, View.ramp_png_len, true, &RampSettings);
			if (Output.Ramp)
			{
				Output.Ramp->SRGB = false;
				Output.Ramp->AddressX = TA_Clamp;
				Output.Ramp->AddressY = TA_Clamp;
				Output.Ramp->AddToRoot();
				Output.Textures.Add(Output.Ramp);
			}
		}
		const FString Name = View.name ? UTF8_TO_TCHAR(View.name) : FString();
		const FString PartRole = View.role ? UTF8_TO_TCHAR(View.role) : FString();
		const FString Lower = Name.ToLower();
		if (Lower.Contains(TEXT("hair_f_100")) || Lower.Contains(TEXT("nohair")))
		{
			continue;
		}
		const bool bBody = PartRole.Equals(TEXT("body"));
		const bool bFaceDraw = Lower.Equals(TEXT("cf_o_face")) && (View.shader_kind==5 || View.shader_kind==7);
		const bool bHead = PartRole.Equals(TEXT("head")) || Lower.StartsWith(TEXT("head"));
		const bool bHair = PartRole.Contains(TEXT("hair"));
		const bool bAcc = PartRole.Contains(TEXT("acc"));
		if (bAcc && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NOACC")) == TEXT("1"))
		{
			continue;
		}
		// Head mods and a second SMR of the same name (vanilla + replacement)
		// both arrive as role=head. Drawing both doubles the face and eyes.
		if (bHead && !bHair && !Output.SeenHead.Contains(Lower))
		{
			Output.SeenHead.Add(Lower);
		}
		else if (bHead && !bHair)
		{
			continue;
		}
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NOHAIR")) == TEXT("1")
			&& (bHair || Lower.Contains(TEXT("hair")) || Lower.Contains(TEXT("kami")) || View.shader_kind == 1 || View.shader_kind == 2))
		{
			continue;
		}
		if (bLoadBodyHeadOnly && !bBody && !bHead && !bHair && !bAcc)
		{
			continue;
		}
		FKKLoadedPart Part;
		Part.bNativeFaceDraw=bFaceDraw;
		Part.Role = PartRole.IsEmpty() ? Lower : PartRole;
		// Material overrides known now become parameters of this part's editor graph (KKMaterialExposure.h).
		ShadowParams.Exposed.Reset(); ShadowParams.ExposedSource.Reset();
		if (!MaterialOverrides.IsEmpty())
		{
			FString MaterialName, SourceProperty;
			OKPartMaterialIdentity(Source, i, MaterialName, SourceProperty);
			ShadowParams.Exposed = MaterialOverrideNames(Published, Part.Role, MaterialName, SourceProperty);
			// Pattern overrides expose all three slots, which read the source values they do not override.
			bool bPattern = false;
			for (const FString& Exposed : ShadowParams.Exposed) bPattern |= OKIsItemPatternProperty(Exposed);
			TArray<FString> Names = ShadowParams.Exposed.Array();
			if (bPattern) for (int32 Slot = 1; Slot <= 3; ++Slot) for (const FString& Pattern : OKItemPatternNames(Slot, false)) Names.AddUnique(Pattern);
			for (const FString& Exposed : Names) { FLinearColor Value; if (Source.MaterialValue(i, TCHAR_TO_UTF8(*Exposed), Value)) ShadowParams.ExposedSource.Add(Exposed, Value); }
		}
		Part.SessionIndex = Published;
		Part.bRuntimeAccessory = View.skel_kind == 4;
		Part.BaseVertices.Reserve(View.n_verts);
		for (int32 v = 0; v < View.n_verts; ++v)
		{
			const float* P = View.positions + v * 3;
			Part.BaseVertices.Add(OkToUe(P[0], P[1], P[2]));
		}
		if (View.normals)
		{
			Part.BaseNormals.Reserve(View.n_verts);
			for (int32 v = 0; v < View.n_verts; ++v)
			{
				const float* Nv = View.normals + v * 3;
				Part.BaseNormals.Add(OkToUeDir(Nv[0], Nv[1], Nv[2]));
			}
		}
		Part.Triangles.Reserve(View.n_idx);
		if (View.indices && View.n_idx >= 3)
		{
			// Unity (left handed) triangle winding is the opposite of UE's, so
			// reverse each triangle to keep front faces front facing.
			for (int32 t = 0; t + 2 < View.n_idx; t += 3)
			{
				Part.Triangles.Add(static_cast<int32>(View.indices[t + 2]));
				Part.Triangles.Add(static_cast<int32>(View.indices[t + 1]));
				Part.Triangles.Add(static_cast<int32>(View.indices[t]));
			}
		}
		if (View.uvs)
		{
			Part.UVs.Reserve(View.n_verts);
			for (int32 v = 0; v < View.n_verts; ++v)
			{
				const float* Uv = (View.eye_draw && View.eye_sample_uvs ? View.eye_sample_uvs : View.uvs) + v * 2;
				Part.UVs.Add(FVector2D(Uv[0], 1.0f - Uv[1]));
			}
		}
		// Skin draw overlays preserve Unity UV1/UV2/UV3 at UE TEXCOORD1/2/3.
		const bool bHairGlossUv = View.shader_kind == 1 || View.shader_kind == 10;
		const float* OverlayUv1 = (bBody || bFaceDraw || View.eye_draw || bHairGlossUv) ? View.uv2 : View.uv4;
		if (!OverlayUv1 && bHairGlossUv) OverlayUv1 = View.uvs;
		if (OverlayUv1 && View.n_verts > 0)
		{
			Part.UV4.Reserve(View.n_verts);
			for (int32 v = 0; v < View.n_verts; ++v)
			{
				const float* Uv = OverlayUv1 + v * 2;
				Part.UV4.Add(FVector2D(Uv[0], 1.0f - Uv[1]));
			}
		}
		// A Studio map stage effect (`_OKStageEffect`) samples its colour at Unity uv2 (TEXCOORD2 here; uv when absent).
		FLinearColor StageEffectKind;
		const bool bStageEffect = !(bBody || bFaceDraw || View.eye_draw) && Source.MaterialValue(i, "_OKStageEffect", StageEffectKind);
		const float* OverlayUv2 = (bBody || bFaceDraw || View.eye_draw) ? View.uv3 : ((View.shader_kind == 1 || View.shader_kind == 10) ? View.uv2
			: bStageEffect ? (View.uv2 ? View.uv2 : View.uvs) : nullptr);
		// Unity's hair vertex binding aliases absent UV1 to UV0. Independent
		// native cloned-mesh controls match an explicit UV0 stream to <1/255,
		// while explicit zero loses the gloss band. Preserve that draw binding
		// without inventing a second set of authored mesh coordinates.
		if (!OverlayUv2 && (View.shader_kind == 1 || View.shader_kind == 10))
			Part.UV2 = Part.UVs;
		if (OverlayUv2 && View.n_verts > 0)
		{
			Part.UV2.Reserve(View.n_verts);
			for (int32 v = 0; v < View.n_verts; ++v)
			{
				const float* Uv = OverlayUv2 + v * 2;
				Part.UV2.Add(FVector2D(Uv[0], 1.0f - Uv[1]));
			}
		}
        if (bFaceDraw && View.uv4) for(int32 v=0;v<View.n_verts;++v)
            Part.UV3.Add(FVector2D(View.uv4[v*2],1.f-View.uv4[v*2+1]));
		Part.OverColor = FLinearColor(View.overcolor[0], View.overcolor[1], View.overcolor[2], View.overcolor[3]);
		Part.ShadowColor = FLinearColor(View.shadow_color[0], View.shadow_color[1], View.shadow_color[2], View.shadow_color[3]);
		if (View.item_draw != 0 && ItemShadowColorOverride.A >= 0.f) Part.ShadowColor = FLinearColor(ItemShadowColorOverride.R, ItemShadowColorOverride.G, ItemShadowColorOverride.B, 1.f);
		Part.LineColor = FLinearColor(View.line_color[0], View.line_color[1], View.line_color[2], View.line_color[3]);
		Part.ShadowExtend = View.shadow_extend;
		Part.NormalDetailScale = View.normal_detail_scale;
		Part.ShaderKind = View.shader_kind;
		Part.Colors.Init(FColor::White, Part.BaseVertices.Num());
		if ((bBody || bFaceDraw || View.shader_kind == 20) && View.vertex_colors)
		{
			for (int32 v = 0; v < View.n_verts; ++v)
			{
				const uint8* C = View.vertex_colors + v * 4;
				Part.Colors[v] = FColor(C[0], C[1], C[2], C[3]);
			}
		}
		const bool bVcOverlay = false;
		if (View.blend)
		{
			Part.Blend = UTF8_TO_TCHAR(View.blend);
		}
		Part.bUnlit = View.unlit != 0;
		Part.bFaceSphereNormals = Lower.Contains(TEXT("cf_o_face")) && !Part.bUnlit;
		TArray<FVector> OutlineNormals = Part.BaseNormals;
		if (Part.bFaceSphereNormals)
		{
			ApplyFaceSphereNormals(Part.BaseVertices, Part.BaseNormals);
		}
		if (Lower.Contains(TEXT("hitomi")))
		{
			// Extract already marks sclera/iris translucent (StudioEditor
			// ConfigureFaceRenderQueues). Forcing sclera opaque wrote depth and
			// let bangs cover the eyes. Keep translucent so they sort with
			// eyeline/mayuge in front of hair. `OK_EYE_MODE` is A/B only.
			const bool bIris = Lower.Contains(TEXT("02"));
			Part.EyeSort = bIris ? 2 : 1;
			Part.bUnlit = true;
			const FString EyeMode = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_EYE_MODE"));
			if (EyeMode == TEXT("translucent"))
			{
				Part.Blend = TEXT("translucent");
			}
			else if (EyeMode == TEXT("masked"))
			{
				Part.Blend = TEXT("masked");
			}
			else if (EyeMode == TEXT("opaque"))
			{
				Part.Blend = bIris ? TEXT("masked") : TEXT("opaque");
			}
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_EYE_TRANSLUCENT")) == TEXT("1"))
			{
				Part.Blend = TEXT("translucent");
			}
			const FString Fwd = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_EYE_FORWARD"));
			if (!Fwd.IsEmpty())
			{
				const float Cm = FCString::Atof(*Fwd);
				for (FVector& V : Part.BaseVertices)
				{
					V.Y += Cm; // Unity +Z (face forward) is UE +Y; units are cm
				}
			}
			if (bIris)
			{
				const FString IFwd = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_IRIS_FORWARD"));
				if (!IFwd.IsEmpty())
				{
					const float Cm = FCString::Atof(*IFwd);
					for (FVector& V : Part.BaseVertices)
					{
						V.Y += Cm;
					}
				}
			}
		}
		else if (Lower.Contains(TEXT("eyeline")) && Lower.Contains(TEXT("low")))
		{
			Part.EyeSort = 5;
		}
		else if (Lower.Contains(TEXT("eyeline")))
		{
			// Blitty orders the renderer's material passes by renderQueue:
			// eyeline shadow 2902 precedes the upper stroke 2903. Extraction
			// splits the shadow into a coincident *_kage component. Equal UE
			// priorities let its skin-coloured pixels cover the stroke depending
			// on the render-proxy tie break. Reserve a distinct layer for each.
			Part.EyeSort = Lower.EndsWith(TEXT("_kage")) ? 3 : 4;
		}
		else if (Lower.Contains(TEXT("mayuge")))
		{
			Part.EyeSort = 6;
		}
		else if (Lower.Contains(TEXT("noseline")))
		{
			Part.EyeSort = 7;
		}
		if (Part.EyeSort != 0)
			UE_LOG(LogTemp, Display, TEXT("OK face layer part=%s priority=%d"), *Name, 100 + Part.EyeSort);
		const FString ParentBone = View.parent_bone ? UTF8_TO_TCHAR(View.parent_bone) : FString();
		const bool bHairPart = Part.Role.Contains(TEXT("hair")) || Lower.Contains(TEXT("hair"))
			|| Lower.Contains(TEXT("kami"));
		const bool bStudioJunk = Lower.Contains(TEXT("highlight_"))
			|| Lower.Contains(TEXT("height bar"))
			|| Lower.Contains(TEXT("silhouette"))
			|| Lower == TEXT("background");
		if (bStudioJunk)
		{
			continue;
		}
		// Hair stays off this path until head_full bones are in the shared
		// skeleton (GPUSkin.md §4). skel_kind is 0 for hair today, so the
		// kind check already excludes it; do not socket-attach world-baked
		// hair verts.
		const bool bCanSkel = OKSkelMeshEnabled() && SharedSkel != nullptr
			&& View.rest_positions && View.weights && View.bone_idx
			&& (View.skel_kind == 1 || View.skel_kind == 2 || View.skel_kind == 3);
		const bool bSlotAcc = OKSkelMeshEnabled() && BodySkel != nullptr && !ParentBone.IsEmpty()
			&& !bCanSkel;
		const FString BakedFollowBone = View.rigid_follow_bone
			? UTF8_TO_TCHAR(View.rigid_follow_bone) : FString();
		// Runtime accessories already contain the animated socket transform in
		// their vertices. A rigid fallback would apply the head motion twice.
		if (!bCanSkel && !bSlotAcc && !Part.bRuntimeAccessory && View.skel_kind == 0)
		{
			Part.RigidFollowBone = BakedFollowBone.IsEmpty()
				? ((bHead || bHairPart) ? FName(TEXT("cf_s_head")) : NAME_None)
				: FName(*BakedFollowBone);
		}
		if (bHairPart && !bCanSkel)
		{
			UE_LOG(LogTemp, Display, TEXT("OK hair static %s kind=%d parent=%s"), *Name, View.skel_kind, *ParentBone);
		}
		if (bSlotAcc && View.rest_positions)
		{
			Part.RuntimeSourceGeometry.Reset();
			Part.BaseVertices.Reset();
			for (int32 v = 0; v < View.n_verts; ++v)
			{
				const float* P = View.rest_positions + v * 3;
				Part.BaseVertices.Add(OkToUe(P[0], P[1], P[2]));
			}
		}
		UProceduralMeshComponent* Comp = nullptr;
		if (bBody && Output.Extra == 0 && !Output.bStage)
		{
			Comp = BodyMesh;
			++Output.Extra;
		}
		else if (bHead && PartRole.Equals(TEXT("head")) && HeadMesh && !HeadMesh->GetNumSections() && !Output.bStage)
		{
			Comp = HeadMesh;
		}
		else
		{
			const FName CompName = ComponentName(UKKAffineProceduralMeshComponent::StaticClass(),FString::Printf(TEXT("Part_%d"), Output.Extra));
			Comp = NewObject<UKKAffineProceduralMeshComponent>(this, CompName);
            CastChecked<UKKAffineProceduralMeshComponent>(Comp)->bUseAnimatedStreams = true;
			Comp->SetupAttachment(RootComponent);
			Install(Comp);
			++Output.Extra;
		}
		UTexture2D* Tex = nullptr;
		// Bind input identity to each part/load so batch-order regressions can
		// be separated from render-state failures without changing the assets.
		UE_LOG(LogTemp, Display, TEXT("OK material input identity part=%d role=%s name=%s png_crc=%08x detail_crc=%08x normal_crc=%08x ramp_crc=%08x"),
			i, *Part.Role, *Name,
			View.png && View.png_len > 0 ? FCrc::MemCrc32(View.png, View.png_len) : 0,
			View.detail_mask && View.detail_mask_len > 0 ? FCrc::MemCrc32(View.detail_mask, View.detail_mask_len) : 0,
			View.normal_map && View.normal_map_len > 0 ? FCrc::MemCrc32(View.normal_map, View.normal_map_len) : 0,
			View.ramp_png && View.ramp_png_len > 0 ? FCrc::MemCrc32(View.ramp_png, View.ramp_png_len) : 0);
		const bool bEyeSolid = Part.EyeSort != 0 &&
			FPlatformMisc::GetEnvironmentVariable(TEXT("OK_EYE_SOLID")) == TEXT("1");
#if !WITH_EDITOR
		if (bCookedTextures)
		{
			Tex = CookedTexture(TEXT("_MainTex"));Part.Texture = Tex;
			// Only where the PNG path sets them: a non-null Overtex also adds the overlay mesh section, and an eye's
			// cooked _overtex1 is its eye layer, not an overlay.
			if (!bFaceDraw && View.overtex_png && View.overtex_png_len > 16) Part.Overtex = CookedTexture(TEXT("_overtex1"));
			if (bBody && View.overtex2_png && View.overtex2_png_len > 16) Part.Overtex2 = CookedTexture(TEXT("_overtex2"));
		}
		else
#endif
		if (View.png && View.png_len > 16 && !bEyeSolid)
		{
			const bool bClamp = Part.EyeSort != 0 || Part.Role.Contains(TEXT("clothes"))
				|| Lower.Contains(TEXT("overtex"));
			OkTextureSettings Settings{};
			const bool bRuntime = Source.TextureSettings(i, "_MainTex", Settings);
			Tex = TextureFromPngBytes(View.png, View.png_len, bClamp, bRuntime ? &Settings : nullptr, bRuntime && Part.EyeSort==0 && !Lower.Contains(TEXT("overtex")));
			if (Tex && bRuntime) UE_LOG(LogTemp, Display, TEXT("OK runtime texture part=%d role=%s name=%s mips=%d filter=%d wrap=(%d,%d) srgb=%d"),
				i,*Part.Role,*Name,Tex->GetPlatformData()->Mips.Num(),Settings.filter,Settings.wrap_u,Settings.wrap_v,Settings.srgb);
			if (Tex)
			{
				Tex->AddToRoot();
				Part.Texture = Tex;
				Output.Textures.Add(Tex);
			}
		}
		if (!bCookedTextures && (View.shader_kind == 1 || View.shader_kind == 10))
		{
			const uint8* Bytes = nullptr;
			int32 Length = 0;
			if (Source.ShaderTexture(i, "_AnotherRamp", Bytes, Length))
			{
				OkTextureSettings Settings{};
				const bool bSource = Source.TextureSettings(i, "_AnotherRamp", Settings);
				ShadowParams.AnotherRampTex = TextureFromPngBytes(Bytes, Length, true, bSource ? &Settings : nullptr, bSource);
				if (ShadowParams.AnotherRampTex)
				{
					if (!bSource)
					{
						ShadowParams.AnotherRampTex->SRGB = false;
						ShadowParams.AnotherRampTex->UpdateResource();
					}
					ShadowParams.AnotherRampTex->AddToRoot();
					Output.Textures.Add(ShadowParams.AnotherRampTex);
				}
			}
		}
		// Baked lightmap (okmodel_view::bake_lightmap): lightmap UVs in Unity uv4 (TEXCOORD1), index in _OKLightmapIndex.
		ShadowParams.LightmapTex = nullptr;
		ShadowParams.LightmapDirectionTex = nullptr;
		FLinearColor LightmapIndex(-1.f, 0.f, 0.f, 0.f);
		if (View.uv4 && !(bBody || bFaceDraw || View.eye_draw) && Source.MaterialValue(i, "_OKLightmapIndex", LightmapIndex))
		{
			const int32 Index = FMath::RoundToInt(LightmapIndex.R);
			const uint8* Bytes = nullptr;
			int32 Length = 0;
			if (!ShadowParams.Lightmaps.Contains(Index) && Source.ShaderTexture(i, "_OKLightmap", Bytes, Length))
				if (UTexture2D* Lightmap = LinearTextureFromPngBytes(Bytes, Length))
				{
					Lightmap->AddToRoot();
					Output.Textures.Add(Lightmap);
					ShadowParams.Lightmaps.Add(Index, Lightmap);
				}
			ShadowParams.LightmapTex = ShadowParams.Lightmaps.FindRef(Index);
			if (!ShadowParams.LightmapDirections.Contains(Index) && Source.ShaderTexture(i, "_OKLightmapDirection", Bytes, Length))
				if (UTexture2D* Direction = LinearTextureFromPngBytes(Bytes, Length))
				{
					Direction->AddToRoot();
					Output.Textures.Add(Direction);
					ShadowParams.LightmapDirections.Add(Index, Direction);
				}
			ShadowParams.LightmapDirectionTex = ShadowParams.LightmapDirections.FindRef(Index);
		}
		if (bSourceMapShading && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_LIGHTING_AUDIT")) == TEXT("1"))
		{
			UE_LOG(LogTemp, Display, TEXT("OK map lighting part=%d mesh=%s shader=%s uv4=%d lightmap=%g color=%d direction=%d cached=%d"),
				i, UTF8_TO_TCHAR(View.name), *Source.MaterialShaderName(i), View.uv4 != nullptr, LightmapIndex.R,
				ShadowParams.LightmapTex != nullptr, ShadowParams.LightmapDirectionTex != nullptr, ShadowParams.Lightmaps.Num());
		}
		// Unity Standard (linear space): indirect diffuse = lightmap x albedo x oneMinusReflectivity
		// (UnityStandardUtils OneMinusReflectivityFromMetallic: unity_ColorSpaceDielectricSpec.a x (1 - metallic)).
		ShadowParams.LightmapDiffuse = 1.f;
		ShadowParams.bStandardLinear = false;
        ShadowParams.bNativeReflection=false;
		ShadowParams.LightmapMetallicTex = nullptr;
		ShadowParams.ReflectionTex = nullptr;
		ShadowParams.StandardMetallic = 0.f;
		ShadowParams.StandardSmoothness = 0.5f;
		ShadowParams.StandardNormalScale = 1.f;
		ShadowParams.bStandardSmoothnessFromMap = false;
		FLinearColor ReflectionIndex = FLinearColor::Black;
		const bool bReflected = !(bBody || bFaceDraw || View.eye_draw) && Source.MaterialValue(i, "_OKReflectionIndex", ReflectionIndex);
        ShadowParams.bNativeReflection=bSourceMapShading;
		if (ShadowParams.LightmapTex || bReflected || (bSourceMapShading))
		{
			const FString Shader = Source.MaterialShaderName(i);
			if (Shader.StartsWith(TEXT("Standard"), ESearchCase::IgnoreCase))
			{
				ShadowParams.bStandardLinear = true;
				FLinearColor BumpScale;
				if (Source.MaterialValue(i, "_BumpScale", BumpScale)) ShadowParams.StandardNormalScale = BumpScale.R;
				constexpr float LinearDielectricOneMinus = 0.96f;
				FLinearColor Value;
				ShadowParams.StandardSmoothness = Source.MaterialValue(i, "_Glossiness", Value) ? FMath::Clamp(Value.R, 0.f, 1.f) : 0.5f;
				if (Shader.Contains(TEXT("Specular"), ESearchCase::IgnoreCase))
				{
					const auto Linear = [](float C) { return C <= .04045f ? C / 12.92f : FMath::Pow((C + .055f) / 1.055f, 2.4f); };
					if (!Source.MaterialValue(i, "_SpecColor", Value)) Value = FLinearColor(.2f, .2f, .2f, 1.f);
					ShadowParams.LightmapDiffuse = 1.f - FMath::Max3(Linear(Value.R), Linear(Value.G), Linear(Value.B));
				}
				else
				{
					ShadowParams.StandardMetallic = Source.MaterialValue(i, "_Metallic", Value) ? FMath::Clamp(Value.R, 0.f, 1.f) : 0.f;
					// Standard declares [Gamma] _Metallic. Native map32 meta00's
					// saved .25 reaches the pixel CB as .05087608844, not .25.
					const float MetallicGamma = ShadowParams.StandardMetallic;
					ShadowParams.StandardMetallic = MetallicGamma <= .04045f ? MetallicGamma / 12.92f : FMath::Pow((MetallicGamma + .055f) / 1.055f, 2.4f);
					ShadowParams.LightmapDiffuse = LinearDielectricOneMinus * (1.f - ShadowParams.StandardMetallic);
					const uint8* Bytes = nullptr;
					int32 Length = 0;
					if (Source.MaterialKeyword(i, "_METALLICGLOSSMAP") && Source.ShaderTexture(i, "_MetallicGlossMap", Bytes, Length))
						if ((ShadowParams.LightmapMetallicTex = LinearTextureFromPngBytes(Bytes, Length)))
						{
							ShadowParams.LightmapMetallicTex->AddToRoot();
							Output.Textures.Add(ShadowParams.LightmapMetallicTex);
							ShadowParams.LightmapDiffuse = LinearDielectricOneMinus;
							// UnityStandardInput MetallicGloss: smoothness is the map's alpha x _GlossMapScale (channel 0).
							FLinearColor Channel, Scale;
							const bool bAlbedoAlpha = Source.MaterialValue(i, "_SmoothnessTextureChannel", Channel) && Channel.R > .5f;
							ShadowParams.StandardSmoothness = Source.MaterialValue(i, "_GlossMapScale", Scale) ? Scale.R : 1.f;
							ShadowParams.bStandardSmoothnessFromMap = !bAlbedoAlpha;
						}
				}
				// Standard indirect specular is enabled after removing the generic albedo
				// emission floor. Set OK_STANDARD_REFLECTION=0 to isolate baked diffuse.
				static const bool bProbeReflections = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STANDARD_REFLECTION")) != TEXT("0");
				if (bReflected && bProbeReflections)
				{
					const int32 Index = FMath::RoundToInt(ReflectionIndex.R);
					const uint8* Bytes = nullptr;
					int32 Length = 0;
					if (!ShadowParams.Reflections.Contains(Index) && Source.ShaderTexture(i, "_OKReflection", Bytes, Length))
						if (UTexture2D* Atlas = LinearTextureFromPngBytes(Bytes, Length))
						{
							Atlas->AddToRoot();
							Output.Textures.Add(Atlas);
							ShadowParams.Reflections.Add(Index, Atlas);
						}
					ShadowParams.ReflectionTex = ShadowParams.Reflections.FindRef(Index);
					FLinearColor Mips, Intensity;
					ShadowParams.ReflectionMips = Source.MaterialValue(i, "_OKReflectionMips", Mips) ? FMath::Max(1.f, Mips.R) : 1.f;
					ShadowParams.ReflectionIntensity = Source.MaterialValue(i, "_OKReflectionIntensity", Intensity) ? Intensity.R : 1.f;
				}
			}
		}
		// Unity Standard-family emission (keyword _EMISSION): _EmissionColor (gamma, may exceed 1) times _EmissionMap.
		ShadowParams.EmissionTex = nullptr;
		ShadowParams.EmissionColor = FLinearColor::Black;
		if (!ShadowParams.bItemDraw && Source.MaterialKeyword(i, "_EMISSION"))
		{
			FLinearColor E;
			if (Source.MaterialValue(i, "_EmissionColor", E) && E.R + E.G + E.B > 0.f)
			{
				auto Linear = [](float V) { return V <= 0.f ? 0.f : FMath::Pow(V, 2.2f); };
				ShadowParams.EmissionColor = FLinearColor(Linear(E.R), Linear(E.G), Linear(E.B), 1.f);
				const uint8* Bytes = nullptr;
				int32 Length = 0;
				if (Source.ShaderTexture(i, "_EmissionMap", Bytes, Length))
				{
					ShadowParams.EmissionTex = TextureFromPngBytes(Bytes, Length);
					if (ShadowParams.EmissionTex) { ShadowParams.EmissionTex->AddToRoot(); Output.Textures.Add(ShadowParams.EmissionTex); }
				}
				UE_LOG(LogTemp, Verbose, TEXT("OK part emission part=%d color=(%.2f,%.2f,%.2f) map=%d"), i, E.R, E.G, E.B, ShadowParams.EmissionTex ? 1 : 0);
			}
		}

		if (ShadowParams.bItemDraw)
		{
			if (!bCookedTextures && View.color_mask && View.color_mask_len > 16)
			{
				OkTextureSettings MaskSettings{};
				const bool bSourceMask = Source.TextureSettings(i, "_ColorMask", MaskSettings);
				ShadowParams.ColorMaskTex = TextureFromPngBytes(View.color_mask, View.color_mask_len,
					false, bSourceMask ? &MaskSettings : nullptr, bSourceMask);
				if (ShadowParams.ColorMaskTex)
				{
					// A serialized Gamma Texture2D is sampled through sRGB decode in
					// Unity. Preserve that source flag; only generated masks lacking
					// texture metadata use the historical linear fallback.
					if (!bSourceMask) { ShadowParams.ColorMaskTex->SRGB = false; ShadowParams.ColorMaskTex->UpdateResource(); }
                    ShadowParams.ColorMaskTex->AddToRoot();
					Output.Textures.Add(ShadowParams.ColorMaskTex);
				}
			}
			UE_LOG(LogTemp, Display, TEXT("OK head item draw name=%s shader_kind=%d colors=%d color=(%.9g,%.9g,%.9g,%.9g) mask_bytes=%d mask_decoded=%d"),
				*Name, View.shader_kind, View.item_draw, View.item_colors[0][0], View.item_colors[0][1], View.item_colors[0][2], View.item_colors[0][3],
				View.color_mask_len, ShadowParams.ColorMaskTex ? 1 : 0);
		}
		if (View.eye_draw) {
			ShadowParams.EyeDrawKind = View.eye_draw; ShadowParams.EyeHighlight = View.eye_highlight;
			auto EyeLinear = [](float V) { return V <= 0.04045f ? V / 12.92f : FMath::Pow((V + 0.055f) / 1.055f, 2.4f); };
			ShadowParams.EyeColor = FLinearColor(EyeLinear(View.eye_color[0]), EyeLinear(View.eye_color[1]), EyeLinear(View.eye_color[2]), View.eye_color[3]);
			for (int32 EyeIndex = 0; EyeIndex < 3; ++EyeIndex) ShadowParams.EyeRotations[EyeIndex] = View.eye_rotations[EyeIndex];
			for (int32 EyeIndex = 0; EyeIndex < 2; ++EyeIndex) {
				ShadowParams.EyeColors[EyeIndex] = FLinearColor(EyeLinear(View.eye_colors[EyeIndex][0]), EyeLinear(View.eye_colors[EyeIndex][1]), EyeLinear(View.eye_colors[EyeIndex][2]), View.eye_colors[EyeIndex][3]);
				if (!bCookedTextures && View.eye_textures[EyeIndex] && View.eye_texture_lens[EyeIndex] > 16) {
					auto* T = TextureFromPngBytes(View.eye_textures[EyeIndex], View.eye_texture_lens[EyeIndex], true);
					if (T) { T->AddToRoot(); Output.Textures.Add(T); ShadowParams.EyeTextures[EyeIndex] = T; }
				}
			}
			// An eye set's pupil expression (hearts, stars: ChaControl.ChangeEyesPtn sets _expression, _exppower 1) on its
			// own varying (TEXCOORD3, free on eyes).
			{
				const uint8* ExpressionBytes = nullptr;
				int32 ExpressionLength = 0;
				TArray<FVector2f> ExpressionUVs;
				if (!bCookedTextures && Part.UV3.IsEmpty() && Source.ShaderTexture(i, "_expression", ExpressionBytes, ExpressionLength)
					&& Source.EyeExpressionUVs(i, ExpressionUVs) && ExpressionUVs.Num() == View.n_verts)
					if (UTexture2D* T = TextureFromPngBytes(ExpressionBytes, ExpressionLength, true))
					{
						T->AddToRoot(); Output.Textures.Add(T); ShadowParams.EyeExpressionTex = T;
						for (const FVector2f& Uv : ExpressionUVs) Part.UV3.Add(FVector2D(Uv.X, 1.f - Uv.Y));
					}
			}
			UE_LOG(LogTemp, Display, TEXT("OK eye draw name=%s uv1=%d uv2=%d up_bytes=%d down_bytes=%d rotations=(%.9g,%.9g,%.9g) enabled=%.9g"),
				*Name, Part.UV4.Num(), Part.UV2.Num(), View.eye_texture_lens[0], View.eye_texture_lens[1], View.eye_rotations[0], View.eye_rotations[1], View.eye_rotations[2], View.eye_highlight);
		}
		UTexture2D* GlossTex = nullptr;
		if (!bCookedTextures && View.hair_gloss && View.hair_gloss_len > 16)
		{
			OkTextureSettings GlossSettings{};
			const bool bNativeGloss = Source.TextureSettings(i, "_HairGloss", GlossSettings);
			GlossTex = TextureFromPngBytes(View.hair_gloss, View.hair_gloss_len,
				false, bNativeGloss ? &GlossSettings : nullptr, bNativeGloss);
			if (GlossTex)
			{
				if (bNativeGloss) UE_LOG(LogTemp, Display, TEXT("OK hair gloss part=%d name=%s mips=%d filter=%d wrap=(%d,%d) srgb=%d"),
					i, *Name, GlossTex->GetPlatformData()->Mips.Num(), GlossSettings.filter,
					GlossSettings.wrap_u, GlossSettings.wrap_v, GlossSettings.srgb);
				GlossTex->AddToRoot();
				Output.Textures.Add(GlossTex);
			}
		}
		UTexture2D* DetailTex = nullptr;
		UTexture2D* AlphaMaskTex = nullptr;
		if (!bCookedTextures && View.alpha_mask && View.alpha_mask_len > 16 &&
			(View.shader_kind == 1 || View.shader_kind == 10 || View.shader_kind == 5 || View.shader_kind == 7))
		{
			AlphaMaskTex = LinearTextureFromPngBytes(View.alpha_mask, View.alpha_mask_len);
			if (AlphaMaskTex) { AlphaMaskTex->AddToRoot(); Output.Textures.Add(AlphaMaskTex); }
		}
		if (bBody)
		{
			UE_LOG(LogTemp, Display, TEXT("OK body alpha mask bytes=%d decoded=%d blend=%s"),
				View.alpha_mask_len, AlphaMaskTex ? 1 : 0, *Part.Blend);
		}
		if (!bCookedTextures && View.detail_mask && View.detail_mask_len > 16)
		{
			OkTextureSettings DetailSettings{};
			const bool bNativeDetail = Source.TextureSettings(i, "_DetailMask", DetailSettings);
			DetailTex = bNativeDetail ? TextureFromPngBytes(View.detail_mask, View.detail_mask_len,false,&DetailSettings,true)
				: LinearTextureFromPngBytes(View.detail_mask,View.detail_mask_len);
			if (DetailTex) { DetailTex->AddToRoot(); Output.Textures.Add(DetailTex); }
		}
		UTexture2D* LineTex = nullptr;
		if (!bCookedTextures && View.line_mask && View.line_mask_len > 16)
		{
			LineTex = LinearTextureFromPngBytes(View.line_mask, View.line_mask_len);
			if (LineTex) { LineTex->AddToRoot(); Output.Textures.Add(LineTex); }
		}
		UTexture2D* NormalTex = nullptr;
		if (!bCookedTextures && View.normal_map && View.normal_map_len > 16)
		{
			NormalTex = LinearTextureFromPngBytes(View.normal_map, View.normal_map_len);
			if (NormalTex) { NormalTex->AddToRoot(); Output.Textures.Add(NormalTex); }
		}
		if (!bCookedTextures && ShadowParams.bStandardLinear && Source.MaterialKeyword(i, "_NORMALMAP"))
		{
			const uint8* Bytes = nullptr; int32 Length = 0;
			if (Source.ShaderTexture(i, "_BumpMap", Bytes, Length))
			{
				OkTextureSettings BumpSettings{1, 0, 0, 0, 0};
				Source.TextureSettings(i, "_BumpMap", BumpSettings);
				BumpSettings.srgb = 0;
				NormalTex = TextureFromPngBytes(Bytes, Length, false, &BumpSettings, true);
				if (NormalTex) { NormalTex->AddToRoot(); Output.Textures.Add(NormalTex); }
				ShadowParams.NormalMapST = FLinearColor(1.f, 1.f, 0.f, 0.f);
				Source.MaterialValue(i, "_BumpMap_ST", ShadowParams.NormalMapST);
			}
		}
		if (Source.MaterialShaderName(i) == TEXT("OpenKoikatsu/TripoPBR"))
		{
			auto PBRTexture=[&](const char* Slot){const uint8* Bytes=nullptr;int32 Length=0;
				if(!Source.ShaderTexture(i,Slot,Bytes,Length))return static_cast<UTexture2D*>(nullptr);
				auto* T=LinearTextureFromPngBytes(Bytes,Length);if(T){T->AddToRoot();Output.Textures.Add(T);}return T;};
			auto* Roughness=PBRTexture("_RoughnessMap");auto* Metallic=PBRTexture("_MetallicMap");
			ShadowParams.TripoPBR=OKMakeTripoPBRMaterial(Tex,NormalTex,Roughness,Metallic);
			if(ShadowParams.TripoPBR)OKEnsureTripoPBRPreviewLights(this);
			UE_LOG(LogTemp,Display,TEXT("OK Tripo PBR part=%s base=%d normal=%d roughness=%d metallic=%d ready=%d"),*Name,Tex?1:0,NormalTex?1:0,Roughness?1:0,Metallic?1:0,ShadowParams.TripoPBR?1:0);
		}
		if (!bCookedTextures && View.normal_detail && View.normal_detail_len > 16)
		{
			ShadowParams.NormalDetailTex = LinearTextureFromPngBytes(View.normal_detail, View.normal_detail_len);
			if (ShadowParams.NormalDetailTex)
			{
				ShadowParams.NormalDetailTex->SRGB = false;
				ShadowParams.NormalDetailTex->AddToRoot();
				Output.Textures.Add(ShadowParams.NormalDetailTex);
			}
		}
		UTexture2D* NormalMaskTex = nullptr;
		if (!bCookedTextures && View.normal_mask && View.normal_mask_len > 16)
		{
			NormalMaskTex = LinearTextureFromPngBytes(View.normal_mask, View.normal_mask_len);
			if (NormalMaskTex) { NormalMaskTex->AddToRoot(); Output.Textures.Add(NormalMaskTex); }
		}
        if (bFaceDraw) {
            for(int32 Slot=1;Slot<=3;++Slot) {
                const FString Property=FString::Printf(TEXT("_overtex%d"),Slot);
                const uint8* Data=nullptr;int32 Length=0;OkTextureSettings Settings{};
                UTexture2D* Texture=nullptr;
                if(!bCookedTextures && Source.ShaderTexture(i,TCHAR_TO_UTF8(*Property),Data,Length) && Length>16) {
                    const bool Native=Source.TextureSettings(i,TCHAR_TO_UTF8(*Property),Settings);
                    Texture=TextureFromPngBytes(Data,Length,true,Native?&Settings:nullptr,Native);
                    if(Texture){Texture->AddToRoot();Output.Textures.Add(Texture);}
                }
                FLinearColor Color=FLinearColor::Black;
                const FString ColorName=FString::Printf(TEXT("_overcolor%d"),Slot);
                Source.MaterialValue(i,TCHAR_TO_UTF8(*ColorName),Color);
                if(Slot==1){if(!bCookedTextures)Part.Overtex=Texture;Part.OverColor=Color;}
                else if(Slot==2){if(!bCookedTextures)Part.Overtex2=Texture;ShadowParams.OverColor2=Color;}
                else {ShadowParams.FaceOvertex3=Texture;ShadowParams.FaceOvercolor3=Color;}
                UE_LOG(LogTemp,Display,TEXT("OK face native overlay slot=%d bytes=%d alpha=%.6f"),Slot,Length,Color.A);
            }
            ShadowParams.bFaceForgeOverlay=View.shader_kind==7;
            if(ShadowParams.bFaceForgeOverlay) {
                Source.MaterialValue(i,"_overtex1_ST",ShadowParams.Texcoord2ST);
                Source.MaterialValue(i,"_overtex2_ST",ShadowParams.Texcoord3ST);
                Source.MaterialValue(i,"_overtex3_ST",ShadowParams.Texcoord4ST);
            } else Source.MaterialValue(i,"_texcoord4_ST",ShadowParams.Texcoord4ST);
        }
		if (!bCookedTextures && !bFaceDraw && View.overtex_png && View.overtex_png_len > 16)
		{
			OkTextureSettings OverSettings{};
			const bool bNativeOver = Source.TextureSettings(i, "_overtex1", OverSettings);
			UTexture2D* Over = TextureFromPngBytes(View.overtex_png, View.overtex_png_len, true,
				bNativeOver ? &OverSettings : nullptr,bNativeOver);
			if (Over)
			{
				if (bBody && !bNativeOver) { Over->AddressX = TA_Clamp; Over->AddressY = TA_Clamp; Over->UpdateResource(); }
				if (bBody) UE_LOG(LogTemp, Display, TEXT("OK body overlay slot=_overtex1 native=%d srgb=%d wrap=(%d,%d)"),
					bNativeOver, Over->SRGB, int32(Over->AddressX), int32(Over->AddressY));
				Over->AddToRoot();
				Part.Overtex = Over;
				Output.Textures.Add(Over);
			}
		}
		if (!bCookedTextures && bBody && View.overtex2_png && View.overtex2_png_len > 16)
		{
			OkTextureSettings OverSettings{};
			const bool bNativeOver = Source.TextureSettings(i, "_overtex2", OverSettings);
			UTexture2D* Over = TextureFromPngBytes(View.overtex2_png, View.overtex2_png_len, true,
				bNativeOver ? &OverSettings : nullptr,bNativeOver);
			if (Over)
			{
				if (!bNativeOver) { Over->AddressX = TA_Clamp; Over->AddressY = TA_Clamp; Over->UpdateResource(); }
				UE_LOG(LogTemp, Display, TEXT("OK body overlay slot=_overtex2 native=%d srgb=%d wrap=(%d,%d)"),
					bNativeOver, Over->SRGB, int32(Over->AddressX), int32(Over->AddressY));
				Over->AddToRoot();
				Part.Overtex2 = Over;
				Output.Textures.Add(Over);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("OK overtex name=%s uv4=%d over=%s color=(%.3f,%.3f,%.3f,%.3f)"),
			*Name, Part.UV4.Num(), Part.Overtex ? TEXT("yes") : TEXT("no"),
			Part.OverColor.R, Part.OverColor.G, Part.OverColor.B, Part.OverColor.A);
		const bool bFaceSkin = Lower.Equals(TEXT("cf_o_face"));
		UE_LOG(LogTemp, Display, TEXT("OK part png name=%s len=%d detail=%d line=%d normal=%d normalMask=%d faceNormal=%.3f shader=%d shadow=(%.3f,%.3f,%.3f) extend=%.3f"),
			*Name, View.png_len, View.detail_mask_len, View.line_mask_len, View.normal_map_len, View.normal_mask_len, View.face_normal, Part.ShaderKind, Part.ShadowColor.R, Part.ShadowColor.G,
			Part.ShadowColor.B, Part.ShadowExtend);
		// OK_PART_PNG_DUMP=<part name part>: that part's main texture as delivered (Saved/OKStudio/partpng/<name>_<index>.png).
		{
			static const FString PngDump = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_PART_PNG_DUMP"));
			if (!PngDump.IsEmpty() && Name.Contains(PngDump) && View.png && View.png_len > 0)
				FFileHelper::SaveArrayToFile(TArrayView<const uint8>(View.png, View.png_len), *(FPaths::ProjectSavedDir() / TEXT("OKStudio/partpng") / FString::Printf(TEXT("%s_%d.png"), *Name, int32(i))));
		}
		if (bFaceSkin && !Tex)
		{
			Tex = SolidColorTexture(FColor(220, 140, 94, 255));
			UE_LOG(LogTemp, Warning, TEXT("OK face png missing, solid skin fallback"));
		}
		// Share the installed skin pixel program, source color conversion and
		// scene binding with the cooked runtime. The previous hand reconstruction
		// omitted the ramp's chroma/ambient tail and shifted texture highlights.
#if WITH_EDITOR
		// A part on a shared parent (ShadowParams.CookedMaterial) draws with that instance; no skin graph of its own.
		if (View.shader_kind == 5 && !ShadowParams.CookedMaterial)
		{
			FKKRuntimeMaterialPart SkinPart;
			SkinPart.ShaderKind=5;SkinPart.SourceMetadataJson=Source.MaterialMetadataJson(i);
			// Optional input audit records the bytes actually delivered to the preview.
			const FString SkinAudit=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SKIN_INPUT_AUDIT"));
			if(!SkinAudit.IsEmpty())
			{
				IFileManager::Get().MakeDirectory(*SkinAudit,true);
				const FString Prefix=SkinAudit/FString::Printf(TEXT("part%d"),i);
				FFileHelper::SaveStringToFile(SkinPart.SourceMetadataJson,*(Prefix+TEXT(".json")));
				auto SaveInput=[&](const TCHAR* Suffix,const uint8* Bytes,int32 Length){if(Bytes&&Length>0)FFileHelper::SaveArrayToFile(TArrayView<const uint8>(Bytes,Length),*(Prefix+Suffix));};
				SaveInput(TEXT(".normal.png"),View.normal_map,View.normal_map_len);
				SaveInput(TEXT(".normal-detail.png"),View.normal_detail,View.normal_detail_len);
				SaveInput(TEXT(".line.png"),View.line_mask,View.line_mask_len);
			}
			SkinPart.Values.Add(TEXT("_ShadowDepth"),FVector4f(ShadowParams.AmbientDepth,0,0,0));
			SkinPart.Values.Add(TEXT("_LineDepth"),FVector4f(View.line_depth,0,0,0));
			SkinPart.Values.Add(TEXT("_LineWidth"),FVector4f(View.line_width,0,0,0));
			SkinPart.Values.Add(TEXT("_VerticalShift"),FVector4f(View.kano_ver_shift,0,0,0));
			auto SkinScalar=[&](const TCHAR* Key,float Value){SkinPart.Values.Add(Key,FVector4f(Value,0,0,0));};
			auto SkinColor=[&](const TCHAR* Key,const FLinearColor& Value){SkinPart.Values.Add(Key,FVector4f(Value.R,Value.G,Value.B,Value.A));};
			SkinColor(TEXT("_OverColor1"),Part.OverColor);SkinColor(TEXT("_OverColor2"),ShadowParams.OverColor2);
			SkinColor(TEXT("_SpecularColor"),FLinearColor(View.specular_color[0],View.specular_color[1],View.specular_color[2],View.specular_color[3]));
			SkinScalar(TEXT("_ShadowExtend"),View.shadow_extend);SkinScalar(TEXT("_NormalDetailScale"),View.normal_detail_scale);
			SkinScalar(TEXT("_FaceNormal"),View.face_normal);SkinScalar(TEXT("_LineTexOn"),View.line_tex_on);
			SkinScalar(TEXT("_SpecularHeight"),View.spec_height);SkinScalar(TEXT("_SpecularPower"),View.specular_power);
			SkinScalar(TEXT("_SpecularPowerNail"),View.specular_power_nail);SkinScalar(TEXT("_NoTexSpecular"),View.no_tex_specular);
			SkinColor(TEXT("_NormalDetail_ST"),ShadowParams.NormalDetailST);
			TMap<FString,TObjectPtr<UTexture2D>> SkinTextures;
			// Source.ShaderTexture contains additional named slots; the legacy
			// skin slots are delivered by OkMeshView. Bind both transports.
			auto SkinTexture=[&](const TCHAR* Property,UTexture2D* Texture){if(Texture)SkinTextures.Add(Property,Texture);};
			SkinTexture(TEXT("_MainTex"),Tex);SkinTexture(TEXT("_DetailMask"),DetailTex);
			SkinTexture(TEXT("_LineMask"),LineTex);SkinTexture(TEXT("_AlphaMask"),AlphaMaskTex);
			SkinTexture(TEXT("_NormalMap"),NormalTex);SkinTexture(TEXT("_NormalMapDetail"),ShadowParams.NormalDetailTex);
			SkinTexture(TEXT("_NormalMask"),NormalMaskTex);SkinTexture(TEXT("_overtex1"),Part.Overtex.Get());
			SkinTexture(TEXT("_overtex2"),Part.Overtex2.Get());SkinTexture(TEXT("_overtex3"),ShadowParams.FaceOvertex3);
			const auto* Layout=OKRuntimeSharedMaterialLayout(EKKRuntimeMaterialRecipe::KoikanoSkin);
			for(const auto& Slot:Layout->Textures)
			{
				if(Slot.Dim!=2 || Slot.Name==TEXT("_RampG"))continue;
				const uint8* Bytes=nullptr;int32 Length=0;
				if(!Source.ShaderTexture(i,TCHAR_TO_UTF8(*Slot.Name),Bytes,Length))continue;
				OkTextureSettings Settings{};
				const bool Native=Source.TextureSettings(i,TCHAR_TO_UTF8(*Slot.Name),Settings);
				if(!Native)Settings=OkTextureSettings{1,0,0,Slot.Name==TEXT("_MainTex")?1:0,0};
				if(auto* Texture=TextureFromPngBytes(Bytes,Length,false,&Settings,true))
				{Texture->AddToRoot();Output.Textures.Add(Texture);SkinTextures.Add(Slot.Name,Texture);}
			}
			if(Output.Ramp)SkinTextures.Add(TEXT("_RampG"),Output.Ramp);
			FKKRuntimeMaterialScene Scene;Scene.EnableShadows=bCharacterDisplayShadows;
			if(!bStudioCompare){OKApplyCharacterMakerLighting(Scene);Scene.StudioShadowGlobals=false;}
			FString Error;
			ShadowParams.NativeSkin=OKMakeNativeSkinMaterial(SkinPart,SkinTextures,Scene,Error,&ShadowParams.Exposed);
			UE_LOG(LogTemp,Display,TEXT("OK native preview skin name=%s textures=%d ready=%d error=%s"),*Name,SkinTextures.Num(),ShadowParams.NativeSkin?1:0,*Error);
		}
#endif
		FOKNativeItemDraw NativeItem;
		if (View.shader_kind == 9 || View.shader_kind == 8 || View.shader_kind == 11 || View.shader_kind == 2)
		{
			NativeItem.ShadowDepth=bCharacterDisplayShadows?ShadowParams.AmbientAlpha:1.f;NativeItem.LineDepth=View.line_depth;NativeItem.LineWidth=View.line_width;
			NativeItem.ForgeShadowDepth=bCharacterDisplayShadows?ShadowParams.Depth:nullptr;NativeItem.ForgeShadowCenter=ShadowParams.Center;
			NativeItem.ForgeShadowRight=ShadowParams.Right;NativeItem.ForgeShadowUp=ShadowParams.Up;
			NativeItem.ForgeShadowLight=ShadowParams.LightDir;NativeItem.ForgeShadowWidth=ShadowParams.Width;
			NativeItem.ForgeShadowDistance=ShadowParams.CameraDistance;
			const bool bForge=View.shader_kind == 8;
			const bool bAlpha=View.shader_kind == 11;
			const bool bOpaque=View.shader_kind == 2;
			for (const FString& Property:bOpaque?OKForgeOpaqueUniformNames():bAlpha?OKForgeAlphaUniformNames():(bForge?OKForgeItemUniformNames():OKMainItemPlusUniformNames()))
			{
				FLinearColor Value;
				if (Source.MaterialValue(i,TCHAR_TO_UTF8(*Property),Value)) NativeItem.Values.Add(Property,Value);
			}
			for (const FString& Property:bOpaque?OKForgeOpaqueTextureNames():bAlpha?OKForgeAlphaTextureNames():(bForge?OKForgeItemTextureNames():OKMainItemPlusTextureNames()))
			{
				const uint8* Bytes=nullptr;int32 Length=0;
				if (bCookedTextures || !Source.ShaderTexture(i,TCHAR_TO_UTF8(*Property),Bytes,Length)) continue;
				OkTextureSettings Settings{};const bool bRuntime=Source.TextureSettings(i,TCHAR_TO_UTF8(*Property),Settings);
				// Native item samplers are immutable. Set fallback metadata before
                // upload so both authored and fallback textures can be shared.
                if(!bRuntime){const bool Color=bOpaque?OKForgeOpaqueTextureIsColor(Property):bAlpha?OKForgeAlphaTextureIsColor(Property):(bForge?OKForgeItemTextureIsColor(Property):OKMainItemPlusTextureIsColor(Property));Settings=OkTextureSettings{1,0,0,Color?1:0,0};}
                UTexture2D* Texture=TextureFromPngBytes(Bytes,Length,false,&Settings,true);
				if (Texture)
				{

                    Texture->AddToRoot();Output.Textures.Add(Texture);NativeItem.Textures.Add(Property,Texture);
                    UE_LOG(LogTemp,Display,TEXT("OK native item texture part=%d property=%s provenance=%d srgb=%d mips=%d filter=%d wrap=(%d,%d)"),
                        i,*Property,bRuntime?1:0,Texture->SRGB?1:0,Texture->GetPlatformData()->Mips.Num(),int32(Texture->Filter),int32(Texture->AddressX),int32(Texture->AddressY));
				}
			}
			if (Output.Ramp) NativeItem.Textures.Add(TEXT("_RampG"),Output.Ramp);
            if(!bCharacterDisplayShadows)NativeItem.Values.Add(TEXT("_ShadowColor"),FLinearColor::White);
            {static const FString Dump=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHARED_DUMP"));if(!Dump.IsEmpty()&&Name.Contains(Dump)){for(const auto& V:NativeItem.Values)UE_LOG(LogTemp,Display,TEXT("OK item dump %s %s=%s"),*Name,*V.Key,*V.Value.ToString());for(const auto& T:NativeItem.Textures)UE_LOG(LogTemp,Display,TEXT("OK item dump %s %s=%s"),*Name,*T.Key,T.Value?*FString::Printf(TEXT("%dx%d srgb=%d"),T.Value->GetSizeX(),T.Value->GetSizeY(),int32(T.Value->SRGB)):TEXT("null"));}}
			NativeItem.Exposed=&ShadowParams.Exposed;ShadowParams.NativeItem=&NativeItem;
			NativeItem.OutlinePart=ShadowParams.OutlinePart;NativeItem.OutlineGlobals=ShadowParams.OutlineGlobals;
		}
		if (bFaceSkin)
		{
			Part.bUnlit = true;
		}
		// Neck and face share one skin surface. Lighting only the body makes the
		// chin polyline a hard colour step; the game's toon skin shader does not.
		if (bBody)
		{
			Part.bUnlit = true;
		}
		// Shader Forge/main_item, main_opaque, and Koikano/
		// main_clothes_opaque ForwardBase passes have Cull Off. Both opaque
		// clothes shaders use SV_IsFrontFace to adjust backface normals.
		// The studio primitive's shader reference may be unresolved, so use
		// its material family instead of widening every translucent item.
		// Shader Forge/main_skin ForwardBase uses Cull Off (kind 7); the
		// Koikano/main_skin ForwardBase uses Cull Back (kind 5).
		// KKS main_item_studio_alpha ForwardBase has Cull Back. Its ME copy
		// may share a hair renderer, but the copied shader keeps its own cull.
		const bool bTwoSided = Part.ShaderKind != 4 &&
			((Part.ShaderKind == 1 || Part.ShaderKind == 10) || Part.ShaderKind == 2 || Part.ShaderKind == 3 || Part.ShaderKind == 7 || Part.ShaderKind == 8 || Part.ShaderKind == 11 || Part.Role.Contains(TEXT("hair")) ||
			(Part.bFaceSphereNormals && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_FACE_TWOSIDED")) == TEXT("1")));
		// Let the extracted shader's render state decide whether the front hair
		// blends. A name containing "hair_f" is not evidence of that pass.
		const bool bFrontHair = (Part.ShaderKind == 1 || Part.ShaderKind == 10) &&
			Part.Blend.Equals(TEXT("translucent"), ESearchCase::IgnoreCase);
		// Brow and upper eyeline must depth-test against the front-hair cards.
		// The no-Z workaround let those face overlays paint over hair even though
		// Unity's queue 2474 brow renders before the queue 2475 front hair.
		// The lower eyeline is exactly coincident with the face in the dump.
		const bool bOverlayNoZ = Lower.Contains(TEXT("eyeline")) && Lower.Contains(TEXT("low"));
		FString Blend = Part.Blend;
		// A skin face with an _AlphaMask clips like the body (main_skin); every other head part stays opaque.
		const bool bMaskedSkinHead = bHead && AlphaMaskTex && (Part.ShaderKind == 5 || Part.ShaderKind == 7) && Part.Blend.Equals(TEXT("masked"), ESearchCase::IgnoreCase);
		if ((bBody && !Part.Blend.Equals(TEXT("masked"), ESearchCase::IgnoreCase)) ||
			(bHead && !bMaskedSkinHead && !Lower.Contains(TEXT("hitomi")) && !Lower.Contains(TEXT("eyeline")) && !Lower.Contains(TEXT("mayuge")) && !Lower.Contains(TEXT("noseline"))
				// Gag eyes (an eye set's drawn eyes) are a cut-out drawing over the face.
				&& !Lower.Contains(TEXT("gag_eye"))))
		{
			Blend = TEXT("opaque");
		}
		if (bFrontHair)
		{
			Blend = TEXT("translucent");
		}
		// A Studio item faded below 1 draws its fading (rendAlpha) parts blended.
		if (ShadowParams.ItemAlpha < 1.f && !Blend.Equals(TEXT("additive"), ESearchCase::IgnoreCase))
		{
			Blend = TEXT("translucent");
		}
		const bool bOutlinable =
			OutlineEnabled() && bItemOutline && !OKShaderHasNoOutlinePass(Source.MaterialShaderName(i)) &&
			((Part.ShaderKind != 2 && Part.ShaderKind != 3 && Part.ShaderKind != 9) || Source.OutlineEnabled(i)) &&
			Part.EyeSort == 0 &&
			// Forge MainAlpha has an independent One/Zero OUTLINE pass.
			// Its transparent FORWARD must not suppress this separate geometry;
			// the native outline vertex program still applies _outline width.
			(Part.ShaderKind == 11 || !Part.Blend.Equals(TEXT("translucent"), ESearchCase::IgnoreCase)) &&
			!Lower.Contains(TEXT("eyeline")) &&
			!Lower.Contains(TEXT("mayuge")) &&
			!Lower.Contains(TEXT("noseline")) &&
			!Lower.Contains(TEXT("namida")) &&
			!Lower.Contains(TEXT("gag_eye")) &&
			!Lower.Contains(TEXT("shadowcaster")) &&
			!Lower.Contains(TEXT("dankon")) &&
			!Lower.Contains(TEXT("mnpa")) &&
			!Lower.Contains(TEXT("mnpb")) &&
			!Lower.Contains(TEXT("gomu"));
		bool bGpu = false;
		if (bCanSkel)
		{
			const double SkeletonStart = GOkCardLoadProfile.bActive ? FPlatformTime::Seconds() : 0.0;
			USkeletalMesh* SkMesh = OKBuildSkeletalMesh(Source, View, this, SharedSkel, i);
			if (GOkCardLoadProfile.bActive) GOkCardLoadProfile.SkeletonSeconds += FPlatformTime::Seconds() - SkeletonStart;
			if (SkMesh)
			{
				const FName FollowName = ComponentName(UKKRuntimePoseableMeshComponent::StaticClass(),FString::Printf(TEXT("Skel_%d"), Published));
				UPoseableMeshComponent* Follow = NewObject<UKKRuntimePoseableMeshComponent>(this, FollowName);
				Follow->ComponentTags.Add(*FString::Printf(TEXT("KKSessionPart_%d"),Published));
				Follow->SetupAttachment(RootComponent);
				Install(Follow);
				Follow->SetSkinnedAssetAndUpdate(SkMesh);
				Follow->SkinCacheUsage.Reset();
				Follow->SkinCacheUsage.Add(ESkinCacheUsage::Disabled);
				Follow->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				Follow->SetComponentTickEnabled(true);
				Follow->AllocateTransformData();
				OKApplySkelPose(Follow, Source);
				Follow->SetCastShadow(false);
				Follow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Follow->SetHiddenInGame(false);
				Follow->SetVisibility(true, true);
				Follow->SetRenderInMainPass(true);
				Follow->SetRenderInDepthPass(true);
				ApplyPartMaterial(Follow, Tex, Blend, Part.bUnlit, bTwoSided, Part.EyeSort != 0, GlossTex, (bBody || bFaceDraw) ? Part.Overtex.Get() : nullptr, Part.OverColor, bOverlayNoZ, bBody, false, Output.Ramp, Part.ShadowColor, DetailTex, LineTex, NormalTex, NormalMaskTex, Part.ShadowExtend, Part.ShaderKind, &ShadowParams, &Output.ShadowMaterials, AlphaMaskTex, Part.Overtex2.Get(), bBody || bFaceDraw);
				Part.SurfaceMat = Follow->GetMaterial(0);
				if (Part.EyeSort != 0)
				{
					Follow->TranslucencySortPriority = 100 + Part.EyeSort;
				}
				else if (bFrontHair)
				{
					Follow->TranslucencySortPriority = 110;
				}
				if (Comp)
				{
					Comp->ClearAllMeshSections();
					Comp->SetVisibility(false);
				}
				Output.Followers.Add(Follow);
				bGpu = true;
				if (OKIsGenitalMesh(Lower)) Output.GenitalPrimitives.Add(Follow);
				const bool bSkinOutline = Part.ShaderKind == 5 || Part.ShaderKind == 7;
				// A part on a shared parent whose family has no _Outline gets no shell (it would draw the default material).
				const bool bNoSharedOutline = ShadowParams.CookedMaterial && OKSharedParents() && !ShadowParams.CookedOutline;
				if (!bNoSharedOutline && bOutlinable && (Part.ShaderKind == 3 || Part.ShaderKind == 9 ||
					(bSkinOutline && !bFaceSkin && Source.MorphCount(i) == 0)) && View.normals)
				{
					// Koikano/main_clothes_opaque has a separate outward Outline
					// pass. Its reverse winding and original skin weights keep fold
					// seams attached to the deforming garment.
					const double OutlineSkeletonStart = GOkCardLoadProfile.bActive ? FPlatformTime::Seconds() : 0.0;
					USkeletalMesh* OutlineMesh = OKBuildSkeletalMesh(Source, View, this, SharedSkel, i, 0.f, true, false, ShadowParams.CookedMaterial && OKSharedParents());
					if (GOkCardLoadProfile.bActive) GOkCardLoadProfile.SkeletonSeconds += FPlatformTime::Seconds() - OutlineSkeletonStart;
					if (OutlineMesh)
					{
						UPoseableMeshComponent* Outline = NewObject<UKKRuntimePoseableMeshComponent>(this, ComponentName(UKKRuntimePoseableMeshComponent::StaticClass(),FString::Printf(TEXT("SkelOutline_%d"), Published)));
						Outline->ComponentTags.Add(*FString::Printf(TEXT("KKSessionPart_%d"),Published));
						Outline->SetupAttachment(RootComponent);
						Install(Outline);
						Outline->SetSkinnedAssetAndUpdate(OutlineMesh);
						Outline->SkinCacheUsage.Reset();
						Outline->SkinCacheUsage.Add(ESkinCacheUsage::Disabled);
						Outline->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
						Outline->SetComponentTickEnabled(true);
						Outline->AllocateTransformData();
						OKApplySkelPose(Outline, Source);
						Outline->SetCastShadow(false);
						Outline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
						Outline->ComponentTags.Add(TEXT("KKPreviewOutline"));
						Outline->SetMaterial(0, MakeOutlineMaterial(Tex, Part.LineColor, false, bSkinOutline, View.line_width, DetailTex, &ShadowParams, AlphaMaskTex, !bSkinOutline));
						Output.Followers.Add(Outline);
						if (OKIsGenitalMesh(Lower)) Output.GenitalPrimitives.Add(Outline);
					}
				}
				const FString LowName = Name.ToLower();
				const bool bHideClosed = LowName == TEXT("cf_o_tooth") || LowName == TEXT("cf_o_canine") || LowName == TEXT("o_tang");
				FString ExpectedClose;
				FString ExpectedOpen;
				const bool bMouth = OKSessionMouthPairNames(Source, i, LowName, 0, ExpectedClose, ExpectedOpen) || bHideClosed
					|| Source.FacePatternCount(i, 1) > 0 || Source.FacePatternCount(i, 2) > 0;
				if (bMouth || Source.MorphCount(i) > 0)
				{
					USkeletalMeshComponent* MouthComp = NewObject<UKKRuntimeSkeletalMeshComponent>(this, ComponentName(UKKRuntimeSkeletalMeshComponent::StaticClass(),FString::Printf(TEXT("Mouth_%d"), Published)));
					MouthComp->SetupAttachment(RootComponent);
					MouthComp->SetTranslucentSortPriority(Follow->TranslucencySortPriority);
					Install(MouthComp);
					MouthComp->SetSkinnedAssetAndUpdate(SkMesh);
					MouthComp->SetAnimationMode(EAnimationMode::AnimationCustomMode);
					MouthComp->SetAnimInstanceClass(UKKCharAnimInstance::StaticClass());
					MouthComp->InitAnim(true);
					MouthComp->SetCastShadow(false);
					MouthComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					ApplyPartMaterial(MouthComp, Tex, Blend, Part.bUnlit, bTwoSided, Part.EyeSort != 0, GlossTex, (bBody || bFaceDraw) ? Part.Overtex.Get() : nullptr, Part.OverColor, bOverlayNoZ, bBody, false, Output.Ramp, Part.ShadowColor, DetailTex, LineTex, NormalTex, NormalMaskTex, Part.ShadowExtend, Part.ShaderKind, &ShadowParams, &Output.ShadowMaterials, AlphaMaskTex, Part.Overtex2.Get(), bBody || bFaceDraw);
					Follow->SetVisibility(false, true);
					Part.SurfaceMat = MouthComp->GetMaterial(0);
					Follow->SetHiddenInGame(true);
					if (OKIsGenitalMesh(Lower))
					{
						Output.GenitalPrimitives.Remove(Follow);
						Output.GenitalPrimitives.Add(MouthComp);
					}
					FOkMouthSlot Slot;
					Slot.Comp = MouthComp;
					Slot.SessionPartIndex = Published;
					Slot.bHideWhenClosed = bHideClosed;
					if (!bNoSharedOutline && bOutlinable && bSkinOutline && View.normals)
					{
						// The face shell needs its own morph buffer, with exactly the
						// surface's local pose and expression weights. Do not freeze it
						// in reference pose or suppress the source alpha/detail mask.
						if (USkeletalMesh* ShellMesh = OKBuildSkeletalMesh(Source, View, this, SharedSkel, i, 0.f, true, bFaceSkin, ShadowParams.CookedMaterial && OKSharedParents()))
						{
							auto* Shell = NewObject<UKKRuntimeSkeletalMeshComponent>(this, ComponentName(UKKRuntimeSkeletalMeshComponent::StaticClass(),FString::Printf(TEXT("SkinOutline_%d"), Published)));
							Shell->SetupAttachment(RootComponent);
							Install(Shell);
							Shell->SetSkinnedAssetAndUpdate(ShellMesh);
							Shell->SetAnimationMode(EAnimationMode::AnimationCustomMode);
							Shell->SetAnimInstanceClass(UKKCharAnimInstance::StaticClass());
							Shell->InitAnim(true);
							Shell->SetCastShadow(false);
							Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
							Shell->ComponentTags.Add(TEXT("KKPreviewOutline"));
							Shell->SetMaterial(0, MakeOutlineMaterial(Tex, Part.LineColor, false, true, View.line_width, DetailTex, &ShadowParams, AlphaMaskTex));
							Slot.OutlineComp = Shell;
						}
					}
					TMap<FString, FName> AvailableMorphs;
					int32 MissingPatternNames = 0;
					for (int32 Morph = 0; Morph < Source.MorphCount(i); ++Morph)
					{
						OkMorphView ViewMorph;
						FMemory::Memzero(ViewMorph);
						if (Source.MorphAt(i, Morph, ViewMorph) && ViewMorph.name)
						{
							const FString MorphName = UTF8_TO_TCHAR(ViewMorph.name);
							AvailableMorphs.Add(MorphName.ToLower(), FName(*MorphName));
						}
					}
					const int32 PatternCount = Source.MouthPatternCount(i) > 0 || bHideClosed
						? OKSessionMouthPatternCount(Source, i, LowName, AvailableMorphs) : 0;
					for (int32 Pattern = 0; Pattern < PatternCount; ++Pattern)
					{
						FName CloseName;
						FName OpenName;
						if (!OKSessionMouthPairNames(Source, i, LowName, Pattern, ExpectedClose, ExpectedOpen))
						{
							Slot.Patterns.Emplace(CloseName, OpenName);
							continue;
						}
						if (const FName* Found = AvailableMorphs.Find(ExpectedClose.ToLower())) CloseName = *Found;
						if (const FName* Found = AvailableMorphs.Find(ExpectedOpen.ToLower())) OpenName = *Found;
						if (CloseName.IsNone() || OpenName.IsNone()) ++MissingPatternNames;
						Slot.Patterns.Emplace(CloseName, OpenName);
					}
					if (MissingPatternNames > 0)
					{
						UE_LOG(LogTemp, Warning, TEXT("OK mouth %s missing %d/%d pattern names"), *Name, MissingPatternNames, PatternCount);
					}
					Output.MouthSlots.Add(Slot);
				}
				UE_LOG(LogTemp, Display, TEXT("OK skelmesh gpu part=%s"), *Name);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("OK skelmesh build failed for %s"), *Name);
			}
		}
		if (bGpu && View.eye_draw) {
			Part.GpuEyeUV[0]=Part.UVs; Part.GpuEyeUV[1]=Part.UV4; Part.GpuEyeUV[2]=Part.UV2;
			Part.GpuEyeMaterial=Cast<UMaterialInstanceDynamic>(Part.SurfaceMat.Get());
		}
		if (!bGpu)
		{
		TArray<FProcMeshTangent> SourceTangents;
		if(View.skel_kind==4 && View.rest_tangents) for(int32 Vertex=0;Vertex<View.n_verts;++Vertex) {
			const float* T=View.rest_tangents+Vertex*4;
			SourceTangents.Add(FProcMeshTangent(OkToUeDir(T[0],T[1],T[2]),T[3]<0.f));
		}
		FillSection(Comp, Part.BaseVertices, Part.Triangles, Part.UVs, Part.Colors, Part.BaseNormals, Part.UV4, Part.UV2,SourceTangents,Part.UV3);
		ApplyPartMaterial(Comp, Tex, Blend, Part.bUnlit, bTwoSided, Part.EyeSort != 0, GlossTex, (bBody || bFaceDraw) ? Part.Overtex.Get() : nullptr, Part.OverColor, bOverlayNoZ, false, false, Output.Ramp, Part.ShadowColor, DetailTex, LineTex, NormalTex, NormalMaskTex, Part.ShadowExtend, Part.ShaderKind, &ShadowParams, &Output.ShadowMaterials, AlphaMaskTex, Part.Overtex2.Get(), bBody || bFaceDraw);
		if (Comp)
		{
			Part.SurfaceMat = Comp->GetMaterial(0);
			// OK_HIDE_SHADER=<prefix>,... (debugging): parts whose source shader starts with one of them are hidden.
			static const TArray<FString> HiddenShaders = [] { TArray<FString> Out; FPlatformMisc::GetEnvironmentVariable(TEXT("OK_HIDE_SHADER")).ParseIntoArray(Out, TEXT(",")); return Out; }();
			if (HiddenShaders.Num())
			{
				const FString Shader = Source.MaterialShaderName(i);
				for (const FString& H : HiddenShaders) if (Shader.StartsWith(H)) Comp->SetVisibility(false);
			}
		}
		// A map screen sampling a map camera's RenderTexture: the camera's capture instead of its (missing) texture.
		if (Comp && !bBody && !bFaceDraw)
			if (UTextureRenderTarget2D* Screen = RenderTextureFor(Source, i))
				if (UMaterialInterface* ScreenMat = OKRenderTextureScreenMaterial())
					if (UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(ScreenMat, Comp))
					{
						Dynamic->SetTextureParameterValue(TEXT("Screen"), Screen);
						Comp->SetMaterial(0, Dynamic);
						Part.SurfaceMat = Dynamic;
						// Not seen by its own camera: the unlit screen filming itself feeds back to white (Unity's lit
						// Standard screen on the dark stage stays dim).
						for (const TPair<int32, TObjectPtr<USceneCaptureComponent2D>>& It : RenderTextureCaptures)
							if (It.Value && It.Value->TextureTarget == Screen) It.Value->HideComponent(Comp);
						UE_LOG(LogTemp, Display, TEXT("OK render texture screen part=%s bounds=%s"), *Name, *Comp->Bounds.GetBox().ToString());
					}
		// Unity's additive particle shaders (Studio's 2D lens flares): texture x 2 _TintColor, with source alpha, not the Studio light
		// effect's three-colour mix the additive part graph draws.
		if (Comp && Tex && Blend.Equals(TEXT("additive"), ESearchCase::IgnoreCase) && Source.MaterialShaderName(i).Contains(TEXT("Particles/Additive")))
			if (UMaterialInterface* ParticleMat = OKParticleAdditiveMaterial())
				if (UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(ParticleMat, Comp))
				{
					FLinearColor TintColor(0.5f, 0.5f, 0.5f, 0.5f);
					Source.MaterialValue(i, "_TintColor", TintColor);
					const auto Linear = [](float C) { return C <= .04045f ? C / 12.92f : FMath::Pow((C + .055f) / 1.055f, 2.4f); };
					TintColor = FLinearColor(Linear(TintColor.R), Linear(TintColor.G), Linear(TintColor.B), TintColor.A);
					FLinearColor InvFade(1.f, 0.f, 0.f, 0.f);
					Source.MaterialValue(i, "_InvFade", InvFade);
					Dynamic->SetTextureParameterValue(TEXT("Tex"), Tex);
					Dynamic->SetVectorParameterValue(TEXT("Tint"), TintColor);
					Dynamic->SetScalarParameterValue(TEXT("FadeDistance"), 100.f / FMath::Max(InvFade.R, 1e-8f));
					Comp->SetMaterial(0, Dynamic);
					Part.SurfaceMat = Dynamic;
					UE_LOG(LogTemp, Display, TEXT("OK native particle part=%s tint_linear=(%.4f,%.4f,%.4f,%.4f) inverse_fade=%.4f material=%s"), *Name, TintColor.R, TintColor.G, TintColor.B, TintColor.A, InvFade.R, *Dynamic->GetPathName());
				}
        if(Comp && bSourceMapShading)
        {
            FLinearColor ProbeNode(-1,0,0,0);Source.MaterialValue(i,"_OKReflectionNode",ProbeNode);
            Comp->ComponentTags.Add(FName(*FString::Printf(TEXT("OKReflectionProbe=%d"),FMath::RoundToInt(ProbeNode.R))));
        }
		FLinearColor RendererMode;
		if (Comp && Source.MaterialValue(i, "_OKRendererShadowMode", RendererMode))
		{
			const int32 Mode = FMath::Clamp(FMath::RoundToInt(RendererMode.R), 0, 3);
			if (Mode == 0) Comp->ComponentTags.Add(TEXT("OKShadowOff"));
			if (Mode == 3)
			{
				Comp->SetRenderInMainPass(false);
				Comp->SetRenderInDepthPass(false);
			}
			Comp->bCastShadowAsTwoSided = Mode == 2;
			Comp->SetCastShadow(Mode != 0);
			FLinearColor Layer = FLinearColor::Black, Culling = FLinearColor::Black, Enabled = FLinearColor::Black, CullingLo = FLinearColor::Black, CullingHi = FLinearColor::Black;
			if (Source.MaterialValue(i, "_OKRendererLayer", Layer) && Source.MaterialValue(i, "_OKSunCulling", Culling))
			{
				const int32 Bit = FMath::Clamp(FMath::RoundToInt(Layer.R), 0, 31);
				const bool bFullMask = Source.MaterialValue(i, "_OKSunCullingLo", CullingLo) && Source.MaterialValue(i, "_OKSunCullingHi", CullingHi);
				const uint32 Mask = bFullMask ? uint32(FMath::RoundToInt(CullingLo.R)) | (uint32(FMath::RoundToInt(CullingHi.R)) << 16)
					: uint32(FMath::RoundToInt(Culling.R));
				if ((bFullMask || Bit < 24) && (Mask & (1u << Bit)) == 0)
					Comp->ComponentTags.Add(TEXT("OKSunExcluded"));
			}
			if (Source.MaterialValue(i, "_OKRendererEnabled", Enabled) && Enabled.R == 0.f)
			{
				Comp->ComponentTags.Add(TEXT("OKRendererDisabled"));
				Comp->SetVisibility(false);
			}
		}
		// A Studio map part shown only at some times of day (okmodel_view::mark_sun_visible): its mask as a tag, for the
		// scene to show or hide it with the scene's time (SunLightInfo.Set).
		FLinearColor SunVisible;
		if (Comp && Source.MaterialValue(i, "_OKSunVisible", SunVisible))
			Comp->ComponentTags.Add(FName(*FString::Printf(TEXT("OKSunVisible%d"), FMath::RoundToInt(SunVisible.R))));
		// A Studio map stage effect (light cone, beam, laser, LED panel): OK's version of its Shader Forge program.
		if (Comp && bStageEffect)
		{
			TArray<UTexture2D*> StageTextures;
			if (UMaterialInstanceDynamic* Dynamic = OKStageEffectInstance(Source, i, Comp, StageTextures))
			{
				Comp->SetMaterial(0, Dynamic);
				Part.SurfaceMat = Dynamic;
				Output.Textures.Append(StageTextures);
			}
		}
		// The shared parents draw the overlay in their own program (_overtex); only the per-part graph needs the extra section.
		if (!ShadowParams.CookedMaterial && Part.Overtex && !bBody && !Part.bNativeFaceDraw && Part.UV4.Num() == Part.BaseVertices.Num())
		{
			FillOvertexSection(Comp, Part.BaseVertices, Part.Triangles, Part.UV4);
			if (UMaterial* Om = MakeOvertexMaterial(Part.Overtex, Part.OverColor))
			{
				Comp->SetMaterial(1, Om);
				Part.OverMat = Om;
			}
		}
		// main_skin has an Outline pass when _OUTLINE_ON is set. The head's
		// interior mouth triangles are not part of its visible silhouette.
		const bool bFace = bFaceSkin;
		// Studio's additive light effects have no Outline pass (their shell would paint the black of the texture).
		if (bOutlinable && Comp && !bStageEffect && !Blend.Equals(TEXT("additive"), ESearchCase::IgnoreCase))
		{
			AddOutlineSection(
				Comp, Part.BaseVertices, Part.Triangles, OutlineNormals,
				Comp->GetNumSections(), &Part.UVs, bFace,
				Tex,
				(Part.ShaderKind == 1 || Part.ShaderKind == 10), Part.ShaderKind == 5 || Part.ShaderKind == 7, Part.LineColor, View.line_width, DetailTex, &ShadowParams, AlphaMaskTex, Part.ShaderKind == 3);
		}
		if (Comp)
		{
			Comp->SetCastShadow(false);
		}
		if (Part.EyeSort != 0)
		{
			Comp->TranslucencySortPriority = 100 + Part.EyeSort;
		}
		else if (bFrontHair)
		{
			Comp->TranslucencySortPriority = 110;
		}
		else if (Blend.Equals(TEXT("translucent"), ESearchCase::IgnoreCase)
			&& View.positions && View.indices && View.uvs)
		{
			const uint32 PositionHash = FCrc::MemCrc32(View.positions, View.n_verts * 3 * sizeof(float));
			const uint32 IndexHash = FCrc::MemCrc32(View.indices, View.n_idx * sizeof(uint32));
			const uint32 UvHash = FCrc::MemCrc32(View.uvs, View.n_verts * 2 * sizeof(float));
			const FString StackKey = FString::Printf(TEXT("%s|%s|%s|%d|%d|%08x|%08x|%08x"),
				*PartRole, *Name, *ParentBone, View.n_verts, View.n_idx,
				PositionHash, IndexHash, UvHash);
			int32& Depth = Output.TranslucentDepth.FindOrAdd(StackKey);
			Comp->TranslucencySortPriority = FMath::Min(Depth, 99);
			++Depth;
		}
		if (bSlotAcc && Comp && BodySkel)
		{
			const int32 Bone = BodySkel->GetBoneIndex(FName(*ParentBone));
			UE_LOG(LogTemp, Display, TEXT("OK socket %s bone=%d"), *Name, Bone);
			if (Bone != INDEX_NONE)
			{
				if (Output.bStage) Output.PendingSockets->Emplace(Comp, FName(*ParentBone));
				else Comp->AttachToComponent(
					BodySkel,
					FAttachmentTransformRules::SnapToTargetNotIncludingScale,
					FName(*ParentBone));
				Part.bSlotAttached = true;
				Part.AttachSocket = FName(*ParentBone);
			}
		}
		}
		Part.Comp = bGpu ? nullptr : Comp;
		if (!bGpu && Comp)
		{
			FString ExpectedClose;
			FString ExpectedOpen;
			const FString LowName = Name.ToLower();
			if (OKSessionMouthPairNames(Source, i, LowName, 0, ExpectedClose, ExpectedOpen)
				|| Source.FacePatternCount(i, 1) > 0 || Source.FacePatternCount(i, 2) > 0 || Source.MorphCount(i) > 0)
			{
				FOkMouthSlot Slot;
				Slot.CpuComp = Comp;
				if (bBody) Part.bRuntimeAccessory = true; // Refresh CPU body base from the live pose before morphing.
				Slot.PartArrayIndex = Output.Parts.Num();
				Slot.SessionPartIndex = Published;
				Slot.bHideWhenClosed = LowName == TEXT("cf_o_tooth")
					|| LowName == TEXT("cf_o_canine") || LowName == TEXT("o_tang");
				TMap<FString, FName> AvailableMorphs;
				for (int32 Morph = 0; Morph < Source.MorphCount(i); ++Morph)
				{
					OkMorphView ViewMorph;
					FMemory::Memzero(ViewMorph);
					if (Source.MorphAt(i, Morph, ViewMorph) && ViewMorph.name)
					{
						const FString MorphName = UTF8_TO_TCHAR(ViewMorph.name);
						AvailableMorphs.Add(MorphName.ToLower(), FName(*MorphName));
					}
				}
				const int32 PatternCount = Source.MouthPatternCount(i) > 0
						? OKSessionMouthPatternCount(Source, i, LowName, AvailableMorphs) : 0;
				for (int32 Pattern = 0; Pattern < PatternCount; ++Pattern)
				{
					FName CloseName;
					FName OpenName;
					if (OKSessionMouthPairNames(Source, i, LowName, Pattern, ExpectedClose, ExpectedOpen))
					{
						if (const FName* Found = AvailableMorphs.Find(ExpectedClose.ToLower())) CloseName = *Found;
						if (const FName* Found = AvailableMorphs.Find(ExpectedOpen.ToLower())) OpenName = *Found;
					}
					Slot.Patterns.Emplace(CloseName, OpenName);
				}
				Output.MouthSlots.Add(MoveTemp(Slot));
			}
		}
		if (Comp && !bGpu)
		{
			Part.RuntimeSectionOffsets.SetNum(Comp->GetNumSections());
			Part.RuntimeSectionNormals.SetNum(Comp->GetNumSections());
			Part.RuntimeRestNormals = Part.BaseNormals;
            Part.RuntimeUnitRestNormals.SetNumUninitialized(Part.RuntimeRestNormals.Num());
            for(int32 I=0;I<Part.RuntimeRestNormals.Num();++I)Part.RuntimeUnitRestNormals[I]=Part.RuntimeRestNormals[I].GetSafeNormal();
			for (int32 SectionIndex = 0; SectionIndex < Comp->GetNumSections(); ++SectionIndex)
			{
				const FProcMeshSection* Section = Comp->GetProcMeshSection(SectionIndex);
				if (!Section || Section->ProcVertexBuffer.Num() != Part.BaseVertices.Num())
				{
					continue;
				}
				TArray<FVector>& Offsets = Part.RuntimeSectionOffsets[SectionIndex];
				TArray<FVector>& SectionNormals = Part.RuntimeSectionNormals[SectionIndex];
				Offsets.Reserve(Part.BaseVertices.Num());
				SectionNormals.Reserve(Part.BaseVertices.Num());
				for (int32 VertexIndex = 0; VertexIndex < Part.BaseVertices.Num(); ++VertexIndex)
				{
					Offsets.Add(Section->ProcVertexBuffer[VertexIndex].Position - Part.BaseVertices[VertexIndex]);
					SectionNormals.Add(Section->ProcVertexBuffer[VertexIndex].Normal);
				}
			}
		}
		// Morph displays retain canonical indices; other submeshes can compact
		// their draw streams without changing the imported source contract.
		if(!bGpu&&Comp&&!View.eye_draw&&Source.MorphCount(i)==0)
			OKCompactProceduralSections(Comp,Part.BaseVertices.Num(),Part.RuntimeDrawVertices);
        // Build the actual draw multiplicity at runtime. Material sections
        // often use disjoint source vertices; section count alone is not reuse.
        if(!bGpu&&Comp&&!Part.bFaceSphereNormals){
            Part.RuntimeNormalMotionUses.SetNumZeroed(Part.BaseVertices.Num());
            for(int32 SectionIndex=0;SectionIndex<Part.RuntimeSectionOffsets.Num();++SectionIndex){
                const auto* Section=Comp->GetProcMeshSection(SectionIndex);
                const auto* Draw=Part.RuntimeDrawVertices.IsValidIndex(SectionIndex)&&!Part.RuntimeDrawVertices[SectionIndex].IsEmpty()?&Part.RuntimeDrawVertices[SectionIndex]:nullptr;
                if(!Section||Part.RuntimeSectionOffsets[SectionIndex].Num()!=Part.BaseVertices.Num())continue;
                for(int32 DrawIndex=0;DrawIndex<Section->ProcVertexBuffer.Num();++DrawIndex){
                    const int32 SourceVertex=Draw?(*Draw)[DrawIndex]:DrawIndex;
                    if(!Part.RuntimeNormalMotionUses.IsValidIndex(SourceVertex))continue;
                    if(SectionIndex!=0||Part.RuntimeSectionOffsets[SectionIndex][SourceVertex]!=FVector::ZeroVector){
                        auto& Uses=Part.RuntimeNormalMotionUses[SourceVertex];Uses=FMath::Min<uint8>(2,Uses+1);
                        Part.bRuntimeNormalMotionReusable|=Uses==2;
                    }
                }
            }
            if(!Part.bRuntimeNormalMotionReusable)Part.RuntimeNormalMotionUses.Reset();
        }
		if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ANIM_PROFILE"))==TEXT("1")&&Comp){int32 DrawCount=0;for(int32 Section=0;Section<Comp->GetNumSections();++Section)if(const auto* Draw=Comp->GetProcMeshSection(Section))DrawCount+=Draw->ProcVertexBuffer.Num();UE_LOG(LogTemp,Display,TEXT("OK animation draw part=%d source_vertices=%d draw_vertices=%d morphs=%d"),i,View.n_verts,DrawCount,Source.MorphCount(i));}
		if (!bGpu && Comp && OKIsGenitalMesh(Lower)) Output.GenitalPrimitives.Add(Comp);
		Output.Parts.Add(MoveTemp(Part));
	}
    return true;
}

static int32 OKSessionPartTag(const UActorComponent* Component)
{
 static const FString Prefix=TEXT("KKSessionPart_");
 if(!Component)return INDEX_NONE;
 for(FName Tag:Component->ComponentTags){const FString Text=Tag.ToString();if(Text.StartsWith(Prefix))return FCString::Atoi(*Text.Mid(Prefix.Len()));}
 return INDEX_NONE;
}
static void OKRetagSessionPart(UActorComponent* Component,int32 Index)
{
 if(!Component)return;
 Component->ComponentTags.RemoveAll([](FName Tag){return Tag.ToString().StartsWith(TEXT("KKSessionPart_"));});
 Component->ComponentTags.Add(*FString::Printf(TEXT("KKSessionPart_%d"),Index));
}

bool AKKPreviewActor::ApplyAccessoryGeometryCandidate(FOkAppearanceContext& Context,FOkAppearanceGeometryCandidate& Candidate,FString& Error,TSharedPtr<FJsonObject> Timing)
{
 check(IsInGameThread());
 const double Begin=FPlatformTime::Seconds();
 if(!bSessionLoadComplete||!SessionLoadState||!Session.IsOpen()||!Session.IsPortable()){Error=TEXT("Preview is not ready for exact-slot geometry");return false;}
 // Native validation first: stale revision, rig, shape, map and provenance.
 if(!Context.CommitGeometry(Candidate,Session,false,Error))return false;
 FString Json;TSharedPtr<FJsonObject> Info;
 if(!Candidate.Info(Json)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Info)||!Info){Error=TEXT("Invalid geometry candidate metadata");return false;}
 const TArray<TSharedPtr<FJsonValue>>* RetainedJson=nullptr;const TArray<TSharedPtr<FJsonValue>>* RangeJson=nullptr;
 if(!Info->TryGetArrayField(TEXT("retained_indices"),RetainedJson)||!Info->TryGetArrayField(TEXT("added_range"),RangeJson)||RangeJson->Num()!=2){Error=TEXT("Geometry candidate has no renderer map");return false;}
 TArray<int32> Retained;Retained.Reserve(RetainedJson->Num());
 for(const auto& Value:*RetainedJson)Retained.Add(Value->IsNull()?INDEX_NONE:int32(Value->AsNumber()));
 const int32 Offset=int32((*RangeJson)[0]->AsNumber()),End=int32((*RangeJson)[1]->AsNumber());
 if(Retained.Num()!=Session.PartCount()||Offset<0||End<Offset){Error=TEXT("Geometry candidate map does not match the live renderers");return false;}
 TSet<int32> Removed;
 for(int32 Index=0;Index<Retained.Num();++Index)if(Retained[Index]==INDEX_NONE)Removed.Add(Index);
 for(const FKKLoadedPart& Part:Parts)if(!Retained.IsValidIndex(Part.SessionIndex)){Error=TEXT("Live renderer is outside the geometry candidate map");return false;}
 // Removed renderers must own only components this transaction can retire.
 for(const FKKLoadedPart& Part:Parts)if(Removed.Contains(Part.SessionIndex)&&(Part.Comp==BodyMesh||Part.Comp==HeadMesh)){Error=TEXT("Geometry candidate removes a persistent renderer");return false;}
 const double MapDone=FPlatformTime::Seconds();
 FOkSession Stage;
 if(!Context.StageGeometry(Candidate,Stage,Error))return false;
 if(Stage.PartCount()!=End-Offset){Error=TEXT("Geometry stage renderer count differs from candidate");return false;}
 const double StageDone=FPlatformTime::Seconds();
 TArray<FKKLoadedPart> NewParts;TArray<TObjectPtr<UTexture2D>> NewTextures;TArray<TObjectPtr<UPoseableMeshComponent>> NewFollowers;
 TArray<FOkMouthSlot> NewMouth;TArray<TObjectPtr<UPrimitiveComponent>> NewGenital;
 TArray<TObjectPtr<UMaterialInstanceDynamic>> NewShadowMaterials,NoCookedMaterials,NoCookedOutlines;
 TObjectPtr<UTexture2D> Ramp=ToonRampTex;TSet<FString> SeenHead=SessionLoadSeenHead;TMap<FString,int32> Depth=SessionLoadTranslucentDepth;
 int32 Extra=SessionLoadExtra,Next=0;FOKToonShadowParameters Shadow=SessionLoadState->Shadow;
 TArray<TObjectPtr<UActorComponent>> Staged;TArray<TPair<TObjectPtr<USceneComponent>,FName>> Sockets;
 FPartBuildOutput Output{NewParts,NewTextures,NewFollowers,NewMouth,NewGenital,NewShadowMaterials,NoCookedMaterials,NoCookedOutlines,Ramp,
  SeenHead,Depth,Extra,Next,SessionLoadWhiteRamp,Shadow};
 Output.bStage=true;Output.SessionOffset=Offset;Output.Staged=&Staged;Output.PendingSockets=&Sockets;
 // Only candidate-created objects are rolled back. Textures already owned by
 // this actor (shared cache hits) stay rooted for the live renderers.
 auto Rollback=[&]{
  for(UActorComponent* Component:Staged)if(Component)Component->DestroyComponent();
  for(UTexture2D* Texture:NewTextures)if(Texture&&!LoadedTextures.Contains(Texture)&&Texture!=ToonRampTex)Texture->RemoveFromRoot();
 };
 if(!BuildSessionPartRange(Stage,Output,0,Stage.PartCount(),Error)){Rollback();return false;}
 TMap<FName,FTransform> AdoptedNeutral;
 for(const FKKLoadedPart& Part:NewParts)if(!Part.RigidFollowBone.IsNone()&&!RigidNeutralBones.Contains(Part.RigidFollowBone)&&!CpuRigidNeutralBones.Contains(Part.RigidFollowBone)){
  // A neutral socket cannot be captured mid-animation without corrupting the
  // pose reference; adopt the same load-time capture recorded for all bones.
  if(const FTransform* Neutral=RigidNeutralAllBones.Find(Part.RigidFollowBone)){AdoptedNeutral.Add(Part.RigidFollowBone,*Neutral);continue;}
  Rollback();Error=TEXT("Geometry candidate needs an uncaptured rigid neutral socket");return false;
 }
 for(const auto& Socket:Sockets)if(!BodySkel||BodySkel->GetBoneIndex(Socket.Value)==INDEX_NONE){Rollback();Error=TEXT("Geometry candidate socket is unavailable");return false;}
 const double BuildDone=FPlatformTime::Seconds();
 // Native publication. Every foreseeable rejection retains the candidate and
 // leaves live state; the staged objects are then discarded.
 if(!Context.CommitGeometry(Candidate,Session,true,Error)){Rollback();return false;}
 const double NativeDone=FPlatformTime::Seconds();
 // UE publication: no fallible step below. Retire the replaced slot first.
 auto Retire=[&](UPrimitiveComponent* Component){
  if(!Component)return;GenitalPrimitives.Remove(Component);
  if(ToonShadowCapture)ToonShadowCapture->RemoveShowOnlyComponent(Component);
  Component->DestroyComponent();
 };
 for(int32 Index=SkelFollowers.Num()-1;Index>=0;--Index){
  UPoseableMeshComponent* Follower=SkelFollowers[Index];const int32 Part=OKSessionPartTag(Follower);
  if(Part==INDEX_NONE||!Retained.IsValidIndex(Part))continue;
  if(Removed.Contains(Part)){Retire(Follower);SkelFollowers.RemoveAt(Index);}
  else OKRetagSessionPart(Follower,Retained[Part]);
 }
 TArray<int32> ArrayMap;ArrayMap.Init(INDEX_NONE,Parts.Num());
 TArray<FKKLoadedPart> Kept;Kept.Reserve(Parts.Num()-Removed.Num()+NewParts.Num());
 for(int32 Index=0;Index<Parts.Num();++Index){
  FKKLoadedPart& Part=Parts[Index];
  if(Removed.Contains(Part.SessionIndex)){Retire(Part.Comp);continue;}
  Part.SessionIndex=Retained[Part.SessionIndex];ArrayMap[Index]=Kept.Num();Kept.Add(MoveTemp(Part));
 }
 for(int32 Index=MouthSlots.Num()-1;Index>=0;--Index){
  FOkMouthSlot& Slot=MouthSlots[Index];
  if(Retained.IsValidIndex(Slot.SessionPartIndex)&&Removed.Contains(Slot.SessionPartIndex)){
   // A CPU slot's component is the part component, already retired above.
   Retire(Slot.Comp);Retire(Slot.OutlineComp);MouthSlots.RemoveAt(Index);continue;
  }
  if(Retained.IsValidIndex(Slot.SessionPartIndex))Slot.SessionPartIndex=Retained[Slot.SessionPartIndex];
  if(Slot.PartArrayIndex!=INDEX_NONE)Slot.PartArrayIndex=ArrayMap.IsValidIndex(Slot.PartArrayIndex)?ArrayMap[Slot.PartArrayIndex]:INDEX_NONE;
 }
 TMap<int32,TMap<FName,float>> Weights;
 for(auto& Entry:PartMorphWeights)if(Retained.IsValidIndex(Entry.Key)&&Retained[Entry.Key]!=INDEX_NONE)Weights.Add(Retained[Entry.Key],MoveTemp(Entry.Value));
 PartMorphWeights=MoveTemp(Weights);
 if(VaginaMorphPart!=INDEX_NONE){
  VaginaMorphPart=Retained.IsValidIndex(VaginaMorphPart)?Retained[VaginaMorphPart]:INDEX_NONE;
  if(VaginaMorphPart==INDEX_NONE){bVaginaMorphResolved=false;VaginaMorphName=NAME_None;}
 }
 const int32 BaseArray=Kept.Num();
 for(FOkMouthSlot& Slot:NewMouth)if(Slot.PartArrayIndex!=INDEX_NONE)Slot.PartArrayIndex+=BaseArray;
 Kept.Append(MoveTemp(NewParts));Parts=MoveTemp(Kept);
 // Register only after native publication, then reapply state OnRegister resets.
 for(UActorComponent* Component:Staged)if(Component&&!Component->IsRegistered())Component->RegisterComponent();
 for(const auto& Socket:Sockets)if(Socket.Key)Socket.Key->AttachToComponent(BodySkel,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Socket.Value);
 for(UPoseableMeshComponent* Follower:NewFollowers)if(Follower&&Follower->GetSkinnedAsset()){Follower->AllocateTransformData();OKApplySkelPose(Follower,Session);}
 for(const FOkMouthSlot& Slot:NewMouth)for(USkeletalMeshComponent* Mesh:{Slot.Comp,Slot.OutlineComp})if(Mesh&&Mesh->GetSkinnedAsset())Mesh->InitAnim(true);
 SkelFollowers.Append(NewFollowers);MouthSlots.Append(MoveTemp(NewMouth));GenitalPrimitives.Append(NewGenital);
 for(UTexture2D* Texture:NewTextures)LoadedTextures.AddUnique(Texture);
 ToonShadowMaterials.Append(NewShadowMaterials);if(!ToonRampTex&&Ramp)ToonRampTex=Ramp;RigidNeutralBones.Append(AdoptedNeutral);
 SessionLoadSeenHead=MoveTemp(SeenHead);SessionLoadTranslucentDepth=MoveTemp(Depth);SessionLoadExtra=Extra;SessionLoadNextPart=Session.PartCount();
 if(ToonShadowCapture)for(UActorComponent* Component:Staged)if(auto* Primitive=Cast<UPrimitiveComponent>(Component))
  if(Primitive->IsVisible()&&Primitive->bRenderInMainPass&&!Primitive->ComponentHasTag(TEXT("KKPreviewOutline")))ToonShadowCapture->ShowOnlyComponent(Primitive);
 if(SuspendedDisplayTicks.Num()>0)SuspendDisplayTicks(true);
 SyncMouthPose();RefreshSkinnedDisplay();ApplyMouth();ApplyGenitalVisibility();
 if(!MaterialOverrides.IsEmpty())ApplyMaterialOverrides();
 const double Done=FPlatformTime::Seconds();
 if(Timing){
  Timing->SetNumberField(TEXT("ue_map_ms"),(MapDone-Begin)*1000.);Timing->SetNumberField(TEXT("ue_stage_clone_ms"),(StageDone-MapDone)*1000.);
  Timing->SetNumberField(TEXT("ue_stage_build_ms"),(BuildDone-StageDone)*1000.);Timing->SetNumberField(TEXT("native_commit_ms"),(NativeDone-BuildDone)*1000.);
  Timing->SetNumberField(TEXT("ue_publish_ms"),(Done-NativeDone)*1000.);Timing->SetNumberField(TEXT("removed_renderers"),Removed.Num());
  Timing->SetNumberField(TEXT("added_renderers"),End-Offset);Timing->SetNumberField(TEXT("staged_components"),Staged.Num());
 }
 UE_LOG(LogTemp,Display,TEXT("Appearance geometry committed removed=%d added=%d components=%d total_ms=%.3f full_rebuild=0"),Removed.Num(),End-Offset,Staged.Num(),(Done-Begin)*1000.);
 return true;
}

bool AKKPreviewActor::LoadSessionBatch(FString& OutError, int32 PartLimit, bool bContinue)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(KK_PreviewSessionBatch);
	const bool bProfile = PartLimit == MAX_int32 && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE")) == TEXT("1");
	if (bProfile) { GOkCardLoadProfile = FOkCardLoadProfile{}; GOkCardLoadProfile.bActive = true; }
	// Progressive loads (the gallery) profile from their first batch to completion.
	if (!bContinue && PartLimit != MAX_int32 && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE")) == TEXT("1")) { GOkCardLoadProfile = FOkCardLoadProfile{}; GOkCardLoadProfile.bActive = true; OKResetMaterialDedupeStats(); OKTextureCreateStats() = {}; }
	const double ProfileStart = FPlatformTime::Seconds();
	double ProfileAfterDestroy = ProfileStart;
	if (!bContinue) {
	TRACE_CPUPROFILER_EVENT_SCOPE(KK_PreviewSessionSetup);
	SessionLoadNextPart = 0; SessionLoadExtra = 0; bSessionLoadComplete = false;
	SessionLoadSeenHead.Reset(); SessionLoadTranslucentDepth.Reset();
	if(!bPreserveSourceWorld)OKDisableRealtimeSky(GetWorld());
	DestroyAllParts();
	ProfileAfterDestroy = FPlatformTime::Seconds();

	if (!Session.IsOpen())
	{
		GOkCardLoadProfile.bActive = false;
		OutError = TEXT("session not open");
		return false;
	}
	SessionLoadState = MakeShared<FSessionLoadState>();
    bCharacterDisplayShadows=CharacterDisplayShadowsOverride.IsSet()
        ? CharacterDisplayShadowsOverride.GetValue() : OKCharacterDisplayShadowsEnabled();
	// Unity SHADOWS_SCREEN multiplies the toon-ramp lookup by a scene shadow
	// attenuation sample. Two render targets avoid sampling the one currently
	// being written while the animated character captures its own depth.
	ToonShadowLightDir.Normalize();
	ToonShadowCenter = GetActorLocation() + FVector(0.f, 0.f, 90.f);
	SessionLoadState->ShadowRotation = (-ToonShadowLightDir).Rotation();
	ToonShadowRight = SessionLoadState->ShadowRotation.Quaternion().GetRightVector();
	ToonShadowUp = SessionLoadState->ShadowRotation.Quaternion().GetUpVector();
	SetActorTickEnabled(false);
	if (PartLimit != MAX_int32) return true;
	}
	if (!SessionLoadState || !Session.IsOpen()) { OutError = TEXT("session load was cancelled"); return false; }
	while (SessionLoadState->SetupStep < SessionLoadSetupStages) {
		TRACE_CPUPROFILER_EVENT_SCOPE(KK_PreviewSessionSetupStage);
		const double SetupStageStart=FPlatformTime::Seconds();
		switch (SessionLoadState->SetupStep) {
		case 0: case 1:
	if (!bCharacterDisplayShadows)
	{
		// Without display shadows the targets are never captured into: the materials only read their clear value (no
		// shadow). Every such preview shares one pair of tiny targets, instead of two 1024x1024 RGBA32f each (32 MB:
		// an OK Studio scene of a thousand items ran out of memory).
		static TStrongObjectPtr<UTextureRenderTarget2D> Shared[2];
		TStrongObjectPtr<UTextureRenderTarget2D>& Slot = Shared[ToonShadowTargets.Num() % 2];
		if (!Slot.IsValid())
		{
			UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
			Target->RenderTargetFormat = RTF_RGBA32f;
			Target->bForceLinearGamma = true;
			Target->SRGB = false;
			Target->ClearColor = FLinearColor(100000.f, 100000.f, 100000.f, 1.f);
			Target->AddressX = TA_Clamp;
			Target->AddressY = TA_Clamp;
			Target->InitAutoFormat(4, 4);
			Target->UpdateResourceImmediate(true);
			Slot.Reset(Target);
		}
		ToonShadowTargets.Add(Slot.Get());
	}
	else
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
		Target->RenderTargetFormat = RTF_RGBA32f;
		Target->bForceLinearGamma = true;
		Target->SRGB = false;
		Target->ClearColor = FLinearColor(100000.f, 100000.f, 100000.f, 1.f);
		Target->AddressX = TA_Clamp;
		Target->AddressY = TA_Clamp;
		Target->InitAutoFormat(1024, 1024);
		Target->UpdateResourceImmediate(true);
		ToonShadowTargets.Add(Target);
	}
		break;
		case 2: {
	ToonShadowCapture = NewObject<USceneCaptureComponent2D>(this, TEXT("ToonShadowCapture"));
	// Keep the capture in world space. SetupStudioView moves the actor to put
	// its feet on the floor after material constants are built; attaching the
	// capture would move its projection while the shader's Center stays fixed.
	ToonShadowCapture->SetWorldLocation(ToonShadowCenter + ToonShadowLightDir * 500.f);
	ToonShadowCapture->SetWorldRotation(SessionLoadState->ShadowRotation);
	ToonShadowCapture->ProjectionType = ECameraProjectionMode::Orthographic;
	ToonShadowCapture->OrthoWidth = ToonShadowWidth;
	ToonShadowCapture->CaptureSource = SCS_SceneDepth;
	ToonShadowCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	ToonShadowCapture->bCaptureEveryFrame = false;
	ToonShadowCapture->bCaptureOnMovement = false;
	ToonShadowCapture->TextureTarget = ToonShadowTargets[1];
	ToonShadowCapture->RegisterComponent();
		break; }
		case 3: {

	MappedSliders.Reset();
	MappedSliders.Add(TEXT("bone_morph ShapeBodyInfoFemale+ShapeHeadInfoFemale"));
	ShapeBody = Session.ShapeBody();
	ShapeFace = Session.ShapeFace();
	if (OKSkelMeshEnabled())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(KK_PreviewSkeletonSetup);
		const int32 Nb = Session.SkelBoneCount();
		UE_LOG(LogTemp, Display, TEXT("OK skelmesh bones=%d"), Nb);
		SharedSkel = OKBuildSharedSkeleton(Session, this);
		if (SharedSkel)
		{
			if (!BodySkel)
			{
				BodySkel = NewObject<UKKRuntimePoseableMeshComponent>(this, TEXT("BodySkel"));
				BodySkel->SetupAttachment(RootComponent);
				BodySkel->RegisterComponent();
			}
			if (USkeletalMesh* Dummy = OKBuildDummyLeaderMesh(SharedSkel, this))
			{
				OKAddSlotSockets(Dummy, Session);
				BodySkel->SetSkinnedAssetAndUpdate(Dummy);
				HideComputeSkel(BodySkel);
			}
		}
	}
		break; }
		case 4: {

	// The serialized Forge hair shaders always sample _AnotherRamp. Their
	// unbound texture property resolves to Unity's white default texture.
	SessionLoadWhiteRamp = SolidColorTexture(FColor::White);
	UTexture2D* WhiteHairAnotherRamp = SessionLoadWhiteRamp;
	if (WhiteHairAnotherRamp)
	{
		WhiteHairAnotherRamp->AddToRoot();
		LoadedTextures.Add(WhiteHairAnotherRamp);
	}
    // Named assignments keep scene setup independent of optional material fields.
    SessionLoadState->Shadow=FOKToonShadowParameters{};
    SessionLoadState->Shadow.Depth=ToonShadowTargets[0];
    SessionLoadState->Shadow.Center=ToonShadowCenter;
    SessionLoadState->Shadow.LightDir=ToonShadowLightDir;
    SessionLoadState->Shadow.Right=ToonShadowRight;
    SessionLoadState->Shadow.Up=ToonShadowUp;
    SessionLoadState->Shadow.Width=ToonShadowWidth;
    SessionLoadState->Shadow.CameraDistance=500.f;
	SetActorTickEnabled(false);
		break; }
		}
		if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE"))==TEXT("1"))
			UE_LOG(LogTemp,Display,TEXT("OK preview setup stage=%d ms=%.3f"),SessionLoadState->SetupStep,(FPlatformTime::Seconds()-SetupStageStart)*1000.);
		++SessionLoadState->SetupStep;
		if (PartLimit != MAX_int32) return true;
	}

	if (!SessionLoadState || !Session.IsOpen() || ToonShadowTargets.Num() != 2) { OutError = TEXT("session load was cancelled"); return false; }
	auto& ShadowParams = SessionLoadState->Shadow;
    if(!ToonRampTex){
        const FString RampPath=OKCharacterDisplayRampPath();
        if(!RampPath.IsEmpty()){
            TArray<uint8> Bytes;const OkTextureSettings Settings{1,1,1,0,1};
            if(!FFileHelper::LoadFileToArray(Bytes,*RampPath)||(ToonRampTex=TextureFromPngBytes(Bytes.GetData(),Bytes.Num(),true,&Settings))==nullptr){OutError=TEXT("Character display ramp could not be loaded: ")+RampPath;return false;}
            ToonRampTex->AddToRoot();LoadedTextures.Add(ToonRampTex);
            UE_LOG(LogTemp,Display,TEXT("OK character display ramp=%s shadows=%d"),*RampPath,bCharacterDisplayShadows?1:0);
        }
    }
	int32& Extra = SessionLoadExtra;
	const int32 N = Session.PartCount();
 if(OKSharedParents()&&bUseSharedParents&&!OKPrepareCookedRole(this,OutError)){
#if WITH_EDITOR
  // The editor still has every part's own graph: a card the shared parents cannot take loads as before.
  UE_LOG(LogTemp,Warning,TEXT("Shared material parents unavailable (%s): per-part material graphs"),*OutError);
  CookedRoleMaterials.Reset();CookedRoleOutlines.Reset();OutError.Reset();
#else
  return false;
#endif
 }
	UTexture2D* WhiteHairAnotherRamp = SessionLoadWhiteRamp;
	auto& SeenHead = SessionLoadSeenHead;
	auto& TranslucentStackDepth = SessionLoadTranslucentDepth;
	const double ProfileBeforeParts = FPlatformTime::Seconds();
	const int32 BatchEnd = SessionLoadNextPart + FMath::Min(PartLimit, N - SessionLoadNextPart);
    FPartBuildOutput Output{Parts,LoadedTextures,SkelFollowers,MouthSlots,GenitalPrimitives,
        ToonShadowMaterials,CookedRoleMaterials,CookedRoleOutlines,ToonRampTex,
        SeenHead,TranslucentStackDepth,Extra,SessionLoadNextPart,WhiteHairAnotherRamp,ShadowParams};
    const int32 BatchBegin=SessionLoadNextPart;const double BatchStart=FPlatformTime::Seconds();
    if(!BuildSessionPartRange(Session,Output,SessionLoadNextPart,BatchEnd,OutError))return false;
    if(PartLimit!=MAX_int32&&FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE"))==TEXT("1")){
        OkMeshView View;FMemory::Memzero(View);const bool bNamed=Session.PartAt(BatchBegin,View)&&View.name;
        UE_LOG(LogTemp,Display,TEXT("OK progressive part %d-%d/%d %s verts=%d ms=%.2f"),BatchBegin,BatchEnd,N,bNamed?UTF8_TO_TCHAR(View.name):TEXT(""),bNamed?View.n_verts:0,(FPlatformTime::Seconds()-BatchStart)*1000.);
    }
	if (SessionLoadNextPart < N) return true;
	const double ProfileAfterParts = FPlatformTime::Seconds();
	if (BodySkel && BodySkel->GetSkinnedAsset())
	{
		BodySkel->SetCastShadow(false);
		BodySkel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BodySkel->SetRenderInMainPass(false);
		BodySkel->SetRenderInDepthPass(false);
		BodySkel->SetHiddenInGame(false);
		BodySkel->SetVisibility(true);
		BodySkel->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		OKApplySkelPose(BodySkel, Session);
	}
	for (UPoseableMeshComponent* F : SkelFollowers)
	{
		if (F && F->GetSkinnedAsset())
		{
			OKApplySkelPose(F, Session);
		}
	}
	CaptureRigidFollowerNeutral();
	// Morph-capable display components replace the poseable followers. Loading
	// an unchanged card may skip ApplyShapes, so they must receive the saved
	// shape/pose here rather than waiting for the first animation or edit.
	SyncMouthPose();
	if (Parts.Num() == 0)
	{
		GOkCardLoadProfile.bActive = false;
		OutError = TEXT("session had no mesh parts");
		return false;
	}
	if (ToonShadowCapture)
	{
		TArray<UPrimitiveComponent*> Primitives;
		GetComponents<UPrimitiveComponent>(Primitives);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			// Unity's Outline pass has no ShadowCaster pass. The preview depth
			// capture uses an explicit component list, so CastShadow alone does
			// not exclude this geometry from its scene depth.
			if (Primitive && Primitive->IsVisible() && Primitive->bRenderInMainPass &&
				!Primitive->ComponentHasTag(TEXT("KKPreviewOutline")))
			{
				ToonShadowCapture->ShowOnlyComponent(Primitive);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("OK toon shadow casters=%d"), ToonShadowCapture->ShowOnlyComponents.Num());
	}
	SetActorTickEnabled(true);
	UE_LOG(LogTemp, Display, TEXT("OK mouth active slots=%d patterns=%d"),
		MouthSlots.Num(), GetMouthPatternCount());
	InitializeFaceControls();
	ApplyMouth();
	ApplyGenitalVisibility();
	const auto ReadonlyTextureStats = OKReadonlyRuntimeTextureCacheStats();
	UE_LOG(LogTemp, Display, TEXT("OK readonly GPU textures hits=%llu misses=%llu metadata_rejections=%llu source_bytes=%lld entries=%d"),
        ReadonlyTextureStats.Hits,ReadonlyTextureStats.Misses,ReadonlyTextureStats.MetadataRejections,
        ReadonlyTextureStats.SourceBytes,ReadonlyTextureStats.Entries);
	if (bProfile)
	{
		const double ProfileEnd = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Display, TEXT("OKCARD_PROFILE ue total_ms=%.1f destroy_ms=%.1f setup_ms=%.1f parts_ms=%.1f post_ms=%.1f texture_ms=%.1f decode_ms=%.1f textures=%d material_ms=%.1f finalize_ms=%.1f materials=%d skeleton_ms=%.1f parts=%d"),
			(ProfileEnd - ProfileStart) * 1000.0, (ProfileAfterDestroy - ProfileStart) * 1000.0,
			(ProfileBeforeParts - ProfileAfterDestroy) * 1000.0, (ProfileAfterParts - ProfileBeforeParts) * 1000.0,
			(ProfileEnd - ProfileAfterParts) * 1000.0, GOkCardLoadProfile.TextureSeconds * 1000.0,
			GOkCardLoadProfile.TextureDecodeSeconds * 1000.0, GOkCardLoadProfile.TextureCount,
			GOkCardLoadProfile.MaterialSeconds * 1000.0, GOkCardLoadProfile.MaterialFinalizeSeconds * 1000.0,
			GOkCardLoadProfile.MaterialCount, GOkCardLoadProfile.SkeletonSeconds * 1000.0, Parts.Num());
		GOkCardLoadProfile.bActive = false;
	}
	if (PartLimit != MAX_int32 && GOkCardLoadProfile.bActive && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE")) == TEXT("1"))
	{
		const FOKMaterialDedupeStats Dedupe = OKMaterialDedupeStats();
		const FOKTextureCreateStats TextureCreate = OKTextureCreateStats();
		UE_LOG(LogTemp, Display, TEXT("OKCARD_PROFILE textures lookup_ms=%.1f create_ms=%.1f upload_ms=%.1f count=%d"),
			TextureCreate.LookupSeconds * 1000.0, TextureCreate.CreateSeconds * 1000.0, TextureCreate.UploadSeconds * 1000.0, TextureCreate.Count);
		OKTextureCreateStats() = {};
		UE_LOG(LogTemp, Display, TEXT("OKCARD_PROFILE progressive texture_ms=%.1f decode_ms=%.1f textures=%d material_ms=%.1f finalize_ms=%.1f materials=%d skeleton_ms=%.1f parts=%d dedupe_hits=%d dedupe_misses=%d dedupe_ms=%.1f"),
			GOkCardLoadProfile.TextureSeconds * 1000.0, GOkCardLoadProfile.TextureDecodeSeconds * 1000.0, GOkCardLoadProfile.TextureCount,
			GOkCardLoadProfile.MaterialSeconds * 1000.0, GOkCardLoadProfile.MaterialFinalizeSeconds * 1000.0, GOkCardLoadProfile.MaterialCount,
			GOkCardLoadProfile.SkeletonSeconds * 1000.0, Parts.Num(), Dedupe.Hits, Dedupe.Misses, Dedupe.SignatureSeconds * 1000.0);
		OKResetMaterialDedupeStats();
		GOkCardLoadProfile.bActive = false;
	}
	bSessionLoadComplete = true;
	if (!MaterialOverrides.IsEmpty()) ApplyMaterialOverrides();
	return true;
}

void AKKPreviewActor::SyncMouthPose()
{
	UPoseableMeshComponent* Src = bShareIdenticalPose&&BodySkel&&BodySkel->BoneSpaceTransforms.Num()>0?BodySkel.Get():nullptr;
	for (UPoseableMeshComponent* F : SkelFollowers)
	{
		if (!Src && F && F->BoneSpaceTransforms.Num() > 0)
		{
			Src = F;
			break;
		}
	}
	if (!Src)
	{
		return;
	}
	for (const FOkMouthSlot& Slot : MouthSlots)
	{
		if (!Slot.Comp)
		{
			continue;
		}
		if (!Slot.Comp->LeaderPoseComponent.IsValid())
		if (UKKCharAnimInstance* Anim = Cast<UKKCharAnimInstance>(Slot.Comp->GetAnimInstance()))
		{
			Anim->SetLocalPose(Src->BoneSpaceTransforms, TArray<uint8>());
		}
		if (Slot.OutlineComp && !Slot.OutlineComp->LeaderPoseComponent.IsValid())
			if (UKKCharAnimInstance* Anim = Cast<UKKCharAnimInstance>(Slot.OutlineComp->GetAnimInstance()))
				Anim->SetLocalPose(Src->BoneSpaceTransforms, TArray<uint8>());
	}
}

void AKKPreviewActor::SuspendDisplayTicks(bool bSuspend){
    if(!bSuspend){
        for(const auto& Entry:SuspendedDisplayTicks)
            if(auto* Component=Entry.Key.Get())Component->SetComponentTickEnabled(Entry.Value);
        SuspendedDisplayTicks.Reset();return;
    }
    TInlineComponentArray<UMeshComponent*> Renderers(this);
    for(UMeshComponent* Component:Renderers)if(IsValid(Component)){
        if(!SuspendedDisplayTicks.Contains(Component))
            SuspendedDisplayTicks.Add(Component,Component->IsComponentTickEnabled());
        Component->SetComponentTickEnabled(false);
    }
}

void AKKPreviewActor::SetCoordinatedPlayback(bool bEnabled){
    bCoordinatedPlayback=bEnabled;
    if(!bEnabled){for(const auto& Entry:CoordinatedComponentTicks)if(auto* Component=Entry.Key.Get())Component->SetComponentTickEnabled(Entry.Value);CoordinatedComponentTicks.Reset();return;}
    auto Own=[&](UActorComponent* Component){if(!Component||CoordinatedComponentTicks.Contains(Component))return;
        CoordinatedComponentTicks.Add(Component,Component->IsComponentTickEnabled());Component->SetComponentTickEnabled(false);};
    // The paired actor publishes every native pose, morph and gaze once per
    // world frame. Disable only its owned renderers' duplicate automatic tick.
    Own(BodySkel);for(UPoseableMeshComponent* Follower:SkelFollowers)Own(Follower);
    for(const auto& Slot:MouthSlots)for(USkeletalMeshComponent* Mesh:{Slot.Comp,Slot.OutlineComp})
        if(Cast<UKKRuntimeSkeletalMeshComponent>(Mesh))Own(Mesh);
}

void AKKPreviewActor::RefreshSkinnedDisplay()
{
 TRACE_CPUPROFILER_EVENT_SCOPE(KK_PreviewSkinDisplay);
 const bool Profile=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ANIM_PROFILE"))==TEXT("1");
 const double Begin=FPlatformTime::Seconds();
 // Shared portable playback solves private-rig palettes without emitting
 // vertices. Procedural hair/accessories still need those current vertices.
 // Native skin_all honors OK_SKELMESH and skips GPU body/head mesh skinning;
 // CPU rendering also refreshes its body/head surfaces from the same pose.
 {TRACE_CPUPROFILER_EVENT_SCOPE(KK_NativeSkin);if(!bNativeSkinReady)SkinPrivateDisplay();bNativeSkinReady=false;}
 const double SkinDone=FPlatformTime::Seconds();
 // Procedural CPU display consumes skinned vertices and native rigid socket
 // matrices. Build a UE bone array only when a skeletal component uses it.
 TArray<FTransform> Pose;
 if(BodySkel || !SkelFollowers.IsEmpty()) OKBuildSkelPose(Session,Pose);
 FKKRuntimeAffinePoseScope SharedPoseScope(Pose);
 if(auto* Leader=Cast<UKKRuntimePoseableMeshComponent>(BodySkel))for(UPoseableMeshComponent* F:SkelFollowers)if(auto* Follower=Cast<UKKRuntimePoseableMeshComponent>(F))Follower->TryUseExactLeader(bShareIdenticalPose&&CVarOkExactLeader.GetValueOnGameThread()!=0?Leader:nullptr);
 if(auto* Leader=Cast<UKKRuntimePoseableMeshComponent>(BodySkel))for(const auto& Slot:MouthSlots)
  for(USkeletalMeshComponent* Mesh:{Slot.Comp,Slot.OutlineComp})if(auto* Follower=Cast<UKKRuntimeSkeletalMeshComponent>(Mesh))
   Follower->TryUseExactLeader(bShareIdenticalPose&&CVarOkExactLeader.GetValueOnGameThread()!=0?Leader:nullptr);
 const double PoseBuilt=FPlatformTime::Seconds();
	if (BodySkel && BodySkel->GetSkinnedAsset())
	{
		OKApplySkelPose(BodySkel, Pose);
	}
	for (UPoseableMeshComponent* F : SkelFollowers)
	{
		if (F && F->GetSkinnedAsset())
		{
			if(!F->LeaderPoseComponent.IsValid())OKApplySkelPose(F, Pose);
            else if(auto* Follower=Cast<UKKRuntimePoseableMeshComponent>(F))Follower->VerifyExactLeader(Pose);
		}
	}
	SyncMouthPose();

 const double BonesDone=FPlatformTime::Seconds();
 if(Profile)UE_LOG(LogTemp,Display,TEXT("OK animation skeleton skin_ms=%.3f build_ms=%.3f apply_ms=%.3f"),(SkinDone-Begin)*1000.,(PoseBuilt-SkinDone)*1000.,(BonesDone-PoseBuilt)*1000.);
	RefreshRuntimeAccessories();
 const double AccessoriesDone=FPlatformTime::Seconds();
	UpdateRigidFollowers();
 const double RigidDone=FPlatformTime::Seconds();
	if (!PartMorphWeights.IsEmpty()) ApplyMouth();
 if(Profile)UE_LOG(LogTemp,Display,TEXT("OK animation display bones_ms=%.3f rigid_ms=%.3f accessories_ms=%.3f morph_ms=%.3f"),(BonesDone-Begin)*1000.,(RigidDone-AccessoriesDone)*1000.,(AccessoriesDone-BonesDone)*1000.,(FPlatformTime::Seconds()-RigidDone)*1000.);
}

bool AKKPreviewActor::RefreshEditorGeometry()
{
 if(!Session.IsOpen()||!SharedSkel)return false;
 TMap<int32,USkeletalMesh*> Surfaces;
 // A part drawn on a shared parent outlines in the source winding (OKBuildSkeletalMesh bSourceWinding).
 auto SharedOutline=[&](int32 Index){return OKSharedParents()&&CookedRoleMaterials.IsValidIndex(Index)&&CookedRoleMaterials[Index]!=nullptr;};
 auto Surface=[&](int32 Index)->USkeletalMesh* {
  if(auto Existing=Surfaces.Find(Index))return *Existing;
  OkMeshView View{};if(!Session.PartAt(Index,View)||View.skel_kind!=1)return nullptr;
  USkeletalMesh* Mesh=OKBuildSkeletalMesh(Session,View,this,SharedSkel,Index);
  Surfaces.Add(Index,Mesh);return Mesh;
 };
 for(UPoseableMeshComponent* Follow:SkelFollowers){
  if(!Follow)continue;
  const FString Name=Follow->GetName();int32 Index=INDEX_NONE;
  const FString Prefix=TEXT("KKSessionPart_");
  for(FName Tag:Follow->ComponentTags){const FString Text=Tag.ToString();if(Text.StartsWith(Prefix)){Index=FCString::Atoi(*Text.Mid(Prefix.Len()));break;}}
  if(Index==INDEX_NONE)continue;
  OkMeshView View{};if(!Session.PartAt(Index,View)||View.skel_kind!=1)continue;
  USkeletalMesh* Mesh=Name.StartsWith(TEXT("SkelOutline_"))?OKBuildSkeletalMesh(Session,View,this,SharedSkel,Index,0.f,true,false,SharedOutline(Index)):Surface(Index);
  if(!Mesh)return false;
  Follow->SetSkinnedAssetAndUpdate(Mesh);Follow->AllocateTransformData();
 }
 for(auto& Slot:MouthSlots){
  OkMeshView View{};if(!Session.PartAt(Slot.SessionPartIndex,View)||View.skel_kind!=1)continue;
  USkeletalMesh* Mesh=Surface(Slot.SessionPartIndex);if(!Mesh)return false;
  if(Slot.Comp)Slot.Comp->SetSkinnedAssetAndUpdate(Mesh);
  if(Slot.OutlineComp){USkeletalMesh* Outline=OKBuildSkeletalMesh(Session,View,this,SharedSkel,Slot.SessionPartIndex,0.f,true,true,SharedOutline(Slot.SessionPartIndex));if(!Outline)return false;Slot.OutlineComp->SetSkinnedAssetAndUpdate(Outline);}
 }
 // Private clothing streams detect the native source revision and upload only
 // changed buffers. Their materials and renderer components remain installed.
 RefreshSkinnedDisplay();return true;
}

struct FKKPrivateGpuDisplay {
 FKKPrivateGpuSource Source;FKKPrivateGpuBounds Bounds;
 TArray<TArray<FOKGpuDeformDraw>> Draw;
 TArray<TSharedPtr<FKKGpuStreamReceipt,ESPMode::ThreadSafe>> Receipts;
 TArray<uint64> Generations;
 FKKPrivateGpuPose CurrentPose;uint64 PoseFrame=MAX_uint64;
 FKKPrivateGpuPose PreparedPose;uint64 PreparedPoseFrame=MAX_uint64;
 uint64 Epoch=0;bool HasGeneration=false,InstallQueued=false,Installed=false,QueryDirty=true,QuerySourceDirty=true,QueryNativeReady=false;
 TArray<bool> QuerySections;
 void MarkQueryDirty(){QueryDirty=QuerySourceDirty=true;QueryNativeReady=false;QuerySections.Init(true,Draw.Num());}
};
void AKKPreviewActor::SkinPrivateDisplay()
{
 TArray<int32,TInlineAllocator<64>> Excluded;TArray<uint64,TInlineAllocator<64>> Revisions;
 for(auto& Part:Parts)if(Part.PrivateGpuDisplay){Part.PrivateGpuDisplay->MarkQueryDirty();
  if(Part.PrivateGpuDisplay->Installed){Excluded.Add(Part.SessionIndex);Revisions.Add(Part.PrivateGpuDisplay->Source.Revision);}}
 if(Excluded.IsEmpty()||!Session.SkinDisplayExceptPrivate(Excluded,Revisions))Session.Skin();
}

bool AKKPreviewActor::UpdatePrivateGpuDisplay(FKKLoadedPart& Part,bool AllowPrepare,TArray<FKKGpuStreamSectionRequest>& Requests)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(KK_PrivateGpuDisplay);
 auto* Component=Cast<UKKAffineProceduralMeshComponent>(Part.Comp);
 const uint64 Epoch=Component?Component->GetGpuStreamProxyEpoch():0;
 auto Restore=[&](const FString& Reason,bool Reject){
  if(Part.PrivateGpuDisplay){EnsurePrivateGpuQueryGeometry(Part.Comp);Part.PrivateGpuDisplay.Reset();Part.RuntimeSourceGeometry.Reset();Part.bNativeDisplayPrepared=false;}
  if(Reject){Part.PrivateGpuRejectedEpoch=Epoch;Part.PrivateGpuRejectedRevision=Session.PrivateSkinSourceRevision(Part.SessionIndex);UE_LOG(LogTemp,Warning,TEXT("OK private GPU retained CPU actor=%s part=%d reason=%s"),*GetName(),Part.SessionIndex,*Reason);}
  return false;
 };
 if(!CVarOkPrivateGpuStreams.GetValueOnGameThread()||!bUsePrivateGpuPlayback||!Component||!Part.bRuntimeAccessory||Part.bSlotAttached||Part.bFaceSphereNormals||Part.SessionIndex==INDEX_NONE||Session.MorphCount(Part.SessionIndex)!=0)
  return Restore(FString(),false);
 if(Part.PrivateGpuDisplay&&Part.PrivateGpuDisplay->Epoch!=Epoch)Restore(FString(),false);
 if(!Epoch||Component->IsRenderStateDirty()||!Component->UsesAnimatedStreams()||Component->GetCollisionEnabled()!=ECollisionEnabled::NoCollision)return Restore(FString(),false);
 if(Part.PrivateGpuRejectedEpoch==Epoch&&Part.PrivateGpuRejectedRevision==Session.PrivateSkinSourceRevision(Part.SessionIndex))return false;
 FString Error;FKKPrivateGpuPose Pose;
 if(!Part.PrivateGpuDisplay){
  if(!AllowPrepare)return false;
  OkMeshView View{};
  if(!Session.PartAt(Part.SessionIndex,View)||View.skel_kind!=4||View.eye_draw)return false;
  // Before the first solved animation pose, private rigs may legitimately not
  // export a source yet. This is readiness, not a permanent unsupported route.
  if(!Session.PrivateSkinSourceRevision(Part.SessionIndex))return false;
  auto Next=MakeShared<FKKPrivateGpuDisplay>();Next->Epoch=Epoch;
  if(!OKCapturePrivateGpuSource(Session,Part.SessionIndex,Next->Source,Pose,Error))return Restore(Error,true);
  const int32 Count=Part.RuntimeSectionOffsets.Num();
  if(!Count||Count!=Component->GetNumSections())return Restore(TEXT("Section layout unsupported"),true);
  Next->Draw.SetNum(Count);TArray<FOKGpuDeformDraw> AllDraw;
  for(int32 S=0;S<Count;++S){
   if(!Part.RuntimeSectionNormals.IsValidIndex(S))return Restore(TEXT("Missing section normals"),true);
   const auto* CPU=Component->GetProcMeshSection(S);const TArray<int32> Empty;
   const auto& Indices=Part.RuntimeDrawVertices.IsValidIndex(S)?Part.RuntimeDrawVertices[S]:Empty;
   if(!CPU||!OKBuildPrivateGpuDraw(Next->Source,Indices,S,Part.RuntimeSectionOffsets[S],Part.RuntimeSectionNormals[S],Part.RuntimeRestNormals,Next->Draw[S],Error)||CPU->ProcVertexBuffer.Num()!=Next->Draw[S].Num())return Restore(Error.IsEmpty()?TEXT("Draw topology mismatch"):Error,true);
   AllDraw.Append(Next->Draw[S]);
  }
  if(!OKBuildPrivateGpuBounds(Next->Source,AllDraw,Next->Bounds,Error))return Restore(Error,true);
  Next->Generations.Init(0,Count);Next->Receipts.SetNum(Count);
  Part.PrivateGpuDisplay=MoveTemp(Next);
 }else{
  auto& State=*Part.PrivateGpuDisplay;
  bool Pending=false;
  for(int32 S=0;S<State.Receipts.Num();++S)if(State.Receipts[S]){
   const int32 Status=State.Receipts[S]->Status.Load();
   if(Status==FKKGpuStreamReceipt::Rejected)return Restore(State.Receipts[S]->Error,true);
   if(Status==FKKGpuStreamReceipt::Pending)Pending=true;
   else{State.Generations[S]=State.Receipts[S]->Generation;State.Receipts[S].Reset();}
  }
  State.MarkQueryDirty();
  if(Pending){if(Session.PrivateSkinSourceRevision(Part.SessionIndex)!=State.Source.Revision)return Restore(TEXT("Native source changed"),false);return true;} // Reserve queued publication.
  if(!State.HasGeneration)State.HasGeneration=true;
  else if(State.InstallQueued&&!State.Installed){State.Installed=true;UE_LOG(LogTemp,Display,TEXT("OK private GPU installed actor=%s part=%d source_vertices=%d sections=%d bounds_groups=%d"),*GetName(),Part.SessionIndex,State.Source.Vertices.Num(),State.Draw.Num(),State.Bounds.Groups.Num());}
  if(State.PreparedPoseFrame==GFrameCounter){Pose=MoveTemp(State.PreparedPose);State.PreparedPoseFrame=MAX_uint64;}
  else if(!OKReadPrivateGpuPose(Session,Part.SessionIndex,State.Source,Pose,Error))return Restore(Error,false);
 }
 auto& State=*Part.PrivateGpuDisplay;FBox Box(ForceInit);
 if(!OKEvaluatePrivateGpuBounds(State.Bounds,Pose,Box,Error))return Restore(Error,true);
 for(int32 S=0;S<State.Draw.Num();++S){
  const auto* CPU=Component->GetProcMeshSection(S);
  if(!CPU||CPU->ProcVertexBuffer.Num()!=State.Draw[S].Num())return Restore(TEXT("GPU topology changed"),true);
 }
 for(int32 S=0;S<State.Draw.Num();++S){
  FKKGpuStreamRequest Request;Request.ProxyEpoch=Epoch;Request.ExpectedGeneration=State.Generations[S];
  Request.Palette=Pose.Palette;Request.PostCorrection=Pose.PostCorrection;Request.HasPostCorrection=true;Request.NativeUnitySpace=true;
  Request.FallbackSlot=State.Source.FallbackSlot;Request.ConservativeLocalBounds=Box;
  if(!State.HasGeneration)Request.QueryGenerationOnly=true;
  else if(!State.InstallQueued){Request.Source=State.Source.Vertices;Request.Draw=State.Draw[S];}
  State.Receipts[S]=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>();
  Requests.Add({Component,S,MoveTemp(Request),State.Receipts[S]});
 }
 if(State.HasGeneration)State.InstallQueued=true;
 State.CurrentPose=MoveTemp(Pose);State.PoseFrame=GFrameCounter;State.MarkQueryDirty();return true;
}

bool AKKPreviewActor::QueryPrivateGpuSurfaceVertices(UProceduralMeshComponent* Component,int32 Section,TConstArrayView<int32> Vertices,TArray<FVector>& Out) const
{
 check(IsInGameThread());TRACE_CPUPROFILER_EVENT_SCOPE(KK_PrivateGpuSparseQuery);
 const auto* Part=Parts.FindByPredicate([&](const auto& P){return P.Comp==Component&&P.PrivateGpuDisplay.IsValid();});if(!Part)return false;
 const auto& State=*Part->PrivateGpuDisplay;if(!State.Installed||State.PoseFrame!=GFrameCounter||!State.Draw.IsValidIndex(Section))return false;
 const auto& Draw=State.Draw[Section];for(int32 Vertex:Vertices)if(!Draw.IsValidIndex(Vertex))return false;
 Out.SetNumUninitialized(Vertices.Num(),EAllowShrinking::No);const FMatrix Render=Component->GetRenderMatrix();
 const int32 Blocks=FMath::DivideAndRoundUp(Vertices.Num(),512);TArray<uint8,TInlineAllocator<32>> Valid;Valid.Init(1,Blocks);
 ParallelFor(Blocks,[&](int32 Block){for(int32 I=Block*512;I<FMath::Min(Vertices.Num(),(Block+1)*512);++I){FVector Position;if(!OKEvaluatePrivateGpuPosition(State.Source,State.CurrentPose,Draw[Vertices[I]],Position)){Valid[Block]=0;continue;}Out[I]=Render.TransformPosition(Position);}},Vertices.Num()<2048?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);
 return !Valid.Contains(0);
}

void AKKPreviewActor::PreparePrivateGpuQueryGeometry(TConstArrayView<UProceduralMeshComponent*> Components)
{
 check(IsInGameThread());TRACE_CPUPROFILER_EVENT_SCOPE(KK_PrivateGpuCpuQueryBatch);
 TArray<int32,TInlineAllocator<64>> Indices;TArray<FKKPrivateGpuDisplay*,TInlineAllocator<64>> States;
 for(auto& Part:Parts)if(Part.PrivateGpuDisplay&&Part.PrivateGpuDisplay->Installed&&Part.PrivateGpuDisplay->QuerySourceDirty&&!Part.PrivateGpuDisplay->QueryNativeReady&&Components.Contains(Part.Comp.Get())){Indices.Add(Part.SessionIndex);States.Add(Part.PrivateGpuDisplay.Get());}
 if(!Indices.IsEmpty()&&Session.QueryPrivateSkinGeometryBatch(Indices))for(auto* State:States)State->QueryNativeReady=true;
}
bool AKKPreviewActor::CapturePrivateGpuSurface(UProceduralMeshComponent* Component,int32 Section,FKKPrivateGpuSurfaceView& Out) const
{
 check(IsInGameThread());
 const auto* Part=Parts.FindByPredicate([&](const auto& P){return P.Comp==Component&&P.PrivateGpuDisplay.IsValid();});if(!Part)return false;
 const auto& State=*Part->PrivateGpuDisplay;
 if(!State.Installed||State.PoseFrame!=GFrameCounter||!State.Draw.IsValidIndex(Section))return false;
 Out.Source=&State.Source;Out.Pose=&State.CurrentPose;Out.Draw=State.Draw[Section];Out.Render=Component->GetRenderMatrix();return true;
}
void AKKPreviewActor::EnsurePrivateGpuQueryGeometry(UProceduralMeshComponent* Component,int32 Section)
{
 check(IsInGameThread());TRACE_CPUPROFILER_EVENT_SCOPE(KK_PrivateGpuCpuQuery);
 for(auto& Part:Parts){
  if(!Part.PrivateGpuDisplay||!Part.PrivateGpuDisplay->QueryDirty||(Component&&Part.Comp!=Component))continue;
  auto& State=*Part.PrivateGpuDisplay;
  if(State.QuerySections.Num()!=State.Draw.Num())State.QuerySections.Init(true,State.Draw.Num());
  if(Section!=INDEX_NONE&&(!State.QuerySections.IsValidIndex(Section)||!State.QuerySections[Section]))continue;
  if(State.QuerySourceDirty&&State.Installed&&!State.QueryNativeReady&&!Session.QueryPrivateSkinGeometry(Part.SessionIndex))continue;
  OkMeshView View{};if(!Session.PartAt(Part.SessionIndex,View)||!View.positions||!View.normals||View.n_verts!=Part.BaseVertices.Num())continue;
  if(State.QuerySourceDirty){for(int32 I=0;I<View.n_verts;++I){Part.BaseVertices[I]=OkToUe(View.positions[I*3],View.positions[I*3+1],View.positions[I*3+2]);Part.BaseNormals[I]=OkToUeDir(View.normals[I*3],View.normals[I*3+1],View.normals[I*3+2]);}State.QuerySourceDirty=false;}
  for(int32 S=0;S<State.Draw.Num();++S){if(!State.QuerySections[S]||(Section!=INDEX_NONE&&Section!=S))continue;auto* CPU=Part.Comp->GetProcMeshSection(S);if(!CPU||CPU->ProcVertexBuffer.Num()!=State.Draw[S].Num())continue;
   for(int32 D=0;D<CPU->ProcVertexBuffer.Num();++D){const auto& Draw=Part.PrivateGpuDisplay->Draw[S][D];const int32 I=Draw.Binding.X;FQuat Q=FQuat::Identity;
    const auto& Offset=Part.RuntimeSectionOffsets[S][I];if(S!=0||Offset!=FVector::ZeroVector){const FVector Rest=Part.RuntimeRestNormals[I].GetSafeNormal(),Current=Part.BaseNormals[I].GetSafeNormal();if(!Rest.IsNearlyZero()&&!Current.IsNearlyZero())Q=FQuat::FindBetweenNormals(Rest,Current);}
    auto& V=CPU->ProcVertexBuffer[D];V.Position=Part.BaseVertices[I]+Q.RotateVector(Offset);V.Normal=S==0?Part.BaseNormals[I]:Q.RotateVector(Part.RuntimeSectionNormals[S][I]);
    if(View.rest_tangents){const float* T=View.rest_tangents+I*4;V.Tangent=FProcMeshTangent(OkToUeDir(T[0],T[1],T[2]),T[3]<0.f);}
   }
   State.QuerySections[S]=false;
  }
  State.QueryDirty=State.QuerySections.Contains(true);
 }
}

void AKKPreviewActor::PublishPhysicsFrame() const
{
	const uint64 PhysicsGroup = Session.PhysicsGroup();
	if (!PhysicsGroup)
	{
		return;
	}
	const FTransform World = SkeletonWorldTransform();
	// Skeleton space (Unity metres, X Y Z) -> component space (UE cm, -X Z Y; see OKSkinPaletteToUnreal) -> world, in metres.
	// PhysX uses it to let hair/cloth lag when the character moves and to meet the partner's body in one frame.
	FMatrix44f Map = FMatrix44f::Identity;
	Map.M[0][0] = -100.f; Map.M[1][1] = 0.f; Map.M[1][2] = 100.f; Map.M[2][2] = 0.f; Map.M[2][1] = 100.f;
	FMatrix44f Frame = Map * FMatrix44f(World.ToMatrixWithScale());
	for (int32 Row = 0; Row < 4; ++Row) for (int32 Col = 0; Col < 3; ++Col) Frame.M[Row][Col] *= 0.01f;
	OKPhysXSecondary::SetGroupFrame(PhysicsGroup, Frame);
}

FTransform AKKPreviewActor::SkeletonWorldTransform() const
{
	// The skeleton is drawn by the part components; their world transform (not necessarily the actor's) places it.
	// Only a part that draws the skeleton counts: rigid items follow a bone (their transform moves with the animation)
	// and accessories hang on sockets. Taking one of them made the hips' motion look like the whole character moving,
	// so every chain got that motion as world inertia (bottoms flung off the tights, pantyhose holes showing).
	FTransform World = BodySkel ? BodySkel->GetComponentTransform() : GetActorTransform();
	for (const FKKLoadedPart& Part : Parts)
	{
		const USceneComponent* Drawn = Part.Comp.Get();
		if (Drawn && Part.RigidFollowBone.IsNone() && !Part.bRuntimeAccessory && !Part.bSlotAttached)
		{
			World = Drawn->GetComponentTransform();
			break;
		}
	}
	return World;
}

bool AKKPreviewActor::AppendJointFramePoints(TArray<FVector>& Points) const
{
	// Joint, and how far the body reaches around it (cm). The head joint sits at the neck: its sphere covers the face.
	static const TPair<const TCHAR*, float> Joints[] = {
		{TEXT("cf_j_head"), 24.f}, {TEXT("cf_j_neck"), 10.f}, {TEXT("cf_j_spine03"), 18.f}, {TEXT("cf_j_hips"), 18.f},
		{TEXT("cf_j_arm00_L"), 8.f}, {TEXT("cf_j_arm00_R"), 8.f}, {TEXT("cf_j_forearm01_L"), 7.f}, {TEXT("cf_j_forearm01_R"), 7.f},
		{TEXT("cf_j_hand_L"), 10.f}, {TEXT("cf_j_hand_R"), 10.f}, {TEXT("cf_j_thigh00_L"), 12.f}, {TEXT("cf_j_thigh00_R"), 12.f},
		{TEXT("cf_j_leg01_L"), 9.f}, {TEXT("cf_j_leg01_R"), 9.f}, {TEXT("cf_j_leg03_L"), 8.f}, {TEXT("cf_j_leg03_R"), 8.f},
		{TEXT("cf_j_toes_L"), 8.f}, {TEXT("cf_j_toes_R"), 8.f}};
	const FTransform World = SkeletonWorldTransform();
	int32 Found = 0;
	for (const TPair<const TCHAR*, float>& Joint : Joints)
	{
		float Matrix[16];
		if (!Session.BoneWorldMatrix(Joint.Key, Matrix)) continue;
		const FVector Center = World.TransformPosition(OKSkinPaletteToUnreal(Matrix).GetOrigin());
		const double R = Joint.Value * World.GetScale3D().GetAbsMax();
		for (int32 Corner = 0; Corner < 8; ++Corner)
			Points.Add(Center + FVector(Corner & 1 ? R : -R, Corner & 2 ? R : -R, Corner & 4 ? R : -R));
		++Found;
	}
	return Found >= 6;
}

bool AKKPreviewActor::ApplyPairedFrameWithDisplay(AKKPreviewActor& Female,AKKPreviewActor& Male,float Time,float Delta,
    const FString& State,const FString& FemaleMotion,const FString& MaleMotion,FString& Error)
{
    check(IsInGameThread());
    // Both roles are solved here (the partner may not tick on its own): publish both frames first.
    Female.PublishPhysicsFrame();Male.PublishPhysicsFrame();
    AKKPreviewActor* Roles[]={&Female,&Male};
    for(auto* RoleIndex:Roles){RoleIndex->bNativeSkinReady=false;for(auto& Part:RoleIndex->Parts)Part.bNativeDisplayPrepared=false;}
    auto Legacy=[&](){return FOkSession::ApplyPairedFrame(Female.Session,Male.Session,Time,Delta,State,FemaleMotion,MaleMotion,Error);};
    // Shared paired sampling still solves both rigs and physics. Avoid the CPU
    // observer's full private geometry pass when GPU owns any renderer; display
    // skins only the remaining parts and explicit queries skin on demand.
    if(CVarOkPrivateGpuStreams.GetValueOnGameThread()){
        TArray<int32,TInlineAllocator<64>> Indices[2];TArray<uint64,TInlineAllocator<64>> Revisions[2];
        for(int32 R=0;R<2;++R)for(const auto& P:Roles[R]->Parts)if(P.PrivateGpuDisplay&&P.PrivateGpuDisplay->Installed){Indices[R].Add(P.SessionIndex);Revisions[R].Add(P.PrivateGpuDisplay->Source.Revision);}
        if(!Indices[0].IsEmpty()||!Indices[1].IsEmpty())return FOkSession::ApplyPairedFrameGpu(Female.Session,Male.Session,Time,Delta,State,FemaleMotion,MaleMotion,Indices[0],Revisions[0],Indices[1],Revisions[1],Error);
    }
    if(!CVarOkNativeDisplayStreams.GetValueOnGameThread()||!CVarOkAnimationGeneratedStreams.GetValueOnGameThread()||!CVarOkAnimationSourceConversion.GetValueOnGameThread())return Legacy();
    TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairNativeDisplay);
    struct FSection {int32 Request,Index;const TArray<int32>* Draw;};
    struct FPart {FKKLoadedPart* Part;int32 Count;bool Cache;TArray<FSection,TInlineAllocator<4>> Sections;};
    TArray<FPart> Parts;TArray<int32> Maps[2];TArray<FKKGeneratedStreamRequest,TInlineAllocator<64>> Requests;
    const bool Cache=CVarOkNormalMotionCache.GetValueOnGameThread()!=0;
    const bool Parity=CVarOkNativeDisplayParity.GetValueOnGameThread()!=0||CVarOkNormalMotionParity.GetValueOnGameThread()!=0;
    for(int32 RoleIndex=0;RoleIndex<2;++RoleIndex){auto& Actor=*Roles[RoleIndex];Maps[RoleIndex].Init(INDEX_NONE,Actor.Session.PartCount());
        for(auto& Part:Actor.Parts){
            if(Part.PrivateGpuDisplay)continue;
            if(!Part.bRuntimeAccessory||!Part.Comp||Part.bSlotAttached||!Maps[RoleIndex].IsValidIndex(Part.SessionIndex)||Part.bFaceSphereNormals)continue;
            OkMeshView View{};if(!Actor.Session.PartAt(Part.SessionIndex,View)||View.skel_kind!=4||View.eye_draw||!View.normals||!View.positions||View.n_verts!=Part.BaseVertices.Num()||View.n_verts!=Part.BaseNormals.Num()||Actor.Session.MorphCount(Part.SessionIndex)!=0)continue;
            const bool CachePart=Cache&&Part.bRuntimeNormalMotionReusable&&Part.RuntimeNormalMotionUses.Num()==View.n_verts&&Part.RuntimeRestNormals.Num()==View.n_verts;
            FPart Prepared{&Part,View.n_verts,CachePart};bool Valid=!Part.RuntimeSectionOffsets.IsEmpty();
            for(int32 S=0;S<Part.RuntimeSectionOffsets.Num();++S){
                const auto* CPU=Part.Comp->GetProcMeshSection(S);const auto* Draw=Part.RuntimeDrawVertices.IsValidIndex(S)&&!Part.RuntimeDrawVertices[S].IsEmpty()?&Part.RuntimeDrawVertices[S]:nullptr;
                if(!CPU||Part.RuntimeSectionOffsets[S].Num()!=View.n_verts||!Part.RuntimeSectionNormals.IsValidIndex(S)||Part.RuntimeSectionNormals[S].Num()!=View.n_verts||CPU->ProcVertexBuffer.Num()!=(Draw?Draw->Num():View.n_verts)){Valid=false;break;}
                Prepared.Sections.Add({Requests.Num()+S,S,Draw});
            }
            if(!Valid)continue;
            if(View.rest_tangents)Part.RuntimeSourceTangents.SetNumUninitialized(View.n_verts);
            if(CachePart)Part.RuntimeSourceNormalMotion.SetNumUninitialized(View.n_verts);
            for(const auto& Section:Prepared.Sections)Requests.Add({Part.Comp,Section.Index});
            Maps[RoleIndex][Part.SessionIndex]=Parts.Num();Parts.Add(MoveTemp(Prepared));
        }
    }
    if(Parts.IsEmpty())return Legacy();
    struct FContext {TArray<FPart>* Parts;TArray<int32>* Maps;FKKGeneratedSectionEmitter* Emit;bool Parity,RestCache,ZeroRotation;};
    auto Callback=+[](void* Opaque,int32 RoleIndex,int32 Index,int32 Count,const float* Positions,const float* Normals,const float* Tangents){
        TRACE_CPUPROFILER_EVENT_SCOPE(KK_NativeDisplayPart);
        auto& Context=*static_cast<FContext*>(Opaque);
        if(RoleIndex<0||RoleIndex>1||!Context.Maps[RoleIndex].IsValidIndex(Index))return;
        const int32 PreparedIndex=Context.Maps[RoleIndex][Index];if(PreparedIndex==INDEX_NONE)return;
        auto& Prepared=(*Context.Parts)[PreparedIndex];if(Count!=Prepared.Count||!Positions||!Normals||(Tangents&&Prepared.Part->RuntimeSourceTangents.Num()!=Count))return;
        auto& Part=*Prepared.Part;Part.RuntimeSourceGeometry.Reset();Part.bRuntimeSourceNormalMotionPrepared=Prepared.Cache;
        for(int32 I=0;I<Count;++I){Part.BaseVertices[I]=OkToUe(Positions[I*3],Positions[I*3+1],Positions[I*3+2]);
            Part.BaseNormals[I]=OkToUeDir(Normals[I*3],Normals[I*3+1],Normals[I*3+2]);
            if(Tangents)Part.RuntimeSourceTangents[I]=FVector4f(Tangents[I*4],Tangents[I*4+1],Tangents[I*4+2],Tangents[I*4+3]);
            if(Prepared.Cache&&Part.RuntimeNormalMotionUses[I]>=2){FQuat Rotation=FQuat::Identity;
                const FVector Rest=Context.RestCache&&Part.RuntimeUnitRestNormals.IsValidIndex(I)?Part.RuntimeUnitRestNormals[I]:Part.RuntimeRestNormals[I].GetSafeNormal(),Current=Part.BaseNormals[I].GetSafeNormal();
                if(!Rest.IsNearlyZero()&&!Current.IsNearlyZero())Rotation=FQuat::FindBetweenNormals(Rest,Current);Part.RuntimeSourceNormalMotion[I]=Rotation;}}
        for(const auto& S:Prepared.Sections){
            auto Generate=[&](int32 DrawIndex,FProcMeshVertex& V){const int32 I=S.Draw?(*S.Draw)[DrawIndex]:DrawIndex;
                const auto& Offset=Part.RuntimeSectionOffsets[S.Index][I];const bool Needs=S.Index!=0||Offset!=FVector::ZeroVector;
                const bool Reuse=Needs&&Prepared.Cache&&Part.RuntimeNormalMotionUses[I]>=2;
                FQuat Rotation=Reuse?Part.RuntimeSourceNormalMotion[I]:FQuat::Identity;
                if(Needs&&(!Reuse||Context.Parity)&&Part.RuntimeRestNormals.IsValidIndex(I)){
                    FQuat Reference=FQuat::Identity;const FVector Rest=Context.Parity||!Context.RestCache||!Part.RuntimeUnitRestNormals.IsValidIndex(I)?Part.RuntimeRestNormals[I].GetSafeNormal():Part.RuntimeUnitRestNormals[I],Current=Part.BaseNormals[I].GetSafeNormal();
                    if(!Rest.IsNearlyZero()&&!Current.IsNearlyZero())Reference=FQuat::FindBetweenNormals(Rest,Current);
                    if(Reuse&&Context.Parity)checkf(Rotation.X==Reference.X&&Rotation.Y==Reference.Y&&Rotation.Z==Reference.Z&&Rotation.W==Reference.W,TEXT("Native display normal cache parity"));
                    if(!Reuse)Rotation=Reference;}
                V.Position=Part.BaseVertices[I]+(Context.ZeroRotation&&!Needs?FVector::ZeroVector:Rotation.RotateVector(Offset));
                V.Normal=S.Index==0?Part.BaseNormals[I]:Rotation.RotateVector(Part.RuntimeSectionNormals[S.Index][I]);
                if(Tangents)V.Tangent=FProcMeshTangent(OkToUeDir(Tangents[I*4],Tangents[I*4+1],Tangents[I*4+2]),Tangents[I*4+3]<0.f);
                if(Context.Parity){FQuat Reference=FQuat::Identity;
                    if(Needs&&Part.RuntimeRestNormals.IsValidIndex(I)){const FVector Rest=Part.RuntimeRestNormals[I].GetSafeNormal(),Current=Part.BaseNormals[I].GetSafeNormal();if(!Rest.IsNearlyZero()&&!Current.IsNearlyZero())Reference=FQuat::FindBetweenNormals(Rest,Current);}
                    checkf(V.Position==Part.BaseVertices[I]+Reference.RotateVector(Offset)&&V.Normal==(S.Index==0?Part.BaseNormals[I]:Reference.RotateVector(Part.RuntimeSectionNormals[S.Index][I])),TEXT("Native display draw parity"));}
            };
            (*Context.Emit)(S.Request,Generate);
        }
        Part.bNativeDisplayPrepared=true;
    };
    bool Applied=false,Called=false;
    const bool Accepted=UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsExternally(Requests,[&](FKKGeneratedSectionEmitter Emit){
        FContext Context{&Parts,Maps,&Emit,Parity,CVarOkRestNormalCache.GetValueOnGameThread()!=0,CVarOkZeroOffsetRotation.GetValueOnGameThread()!=0};Called=true;
        Applied=FOkSession::ApplyPairedFrame(Female.Session,Male.Session,Time,Delta,State,FemaleMotion,MaleMotion,Error,Callback,&Context);
    });
    if(!Accepted&&!Called)return Legacy();
    if(Applied){Female.bNativeSkinReady=true;Male.bNativeSkinReady=true;
        for(int32 RoleIndex=0;RoleIndex<2;++RoleIndex)for(int32 P:Maps[RoleIndex])if(P!=INDEX_NONE&&Parts[P].Part->bNativeDisplayPrepared)
            for(const auto& S:Parts[P].Sections)Roles[RoleIndex]->AnimationUploadedVertices+=S.Draw?S.Draw->Num():Parts[P].Count;
        static bool Reported=false;if(Parity&&!Reported){int32 Done=0,Vertices=0;for(const auto& P:Parts)if(P.Part->bNativeDisplayPrepared){++Done;Vertices+=P.Count;}
            if(Done){UE_LOG(LogTemp,Display,TEXT("OK native display parity parts=%d source_vertices=%d sections=%d"),Done,Vertices,Requests.Num());Reported=true;}}}
    return Applied;
}

bool AKKPreviewActor::AreInteractionStreamsReady() const {
    if(!bPlaybackMaterialsReady)return false;
    for(const auto& Part:Parts)if(Part.PrivateGpuDisplay&&!Part.PrivateGpuDisplay->Installed)return false;
    return true;
}

void AKKPreviewActor::PreparePrivateGpuDisplayPoses(){
 TRACE_CPUPROFILER_EVENT_SCOPE(KK_PrivateGpuPaletteBatch);
 if(!bUsePrivateGpuPlayback||!CVarOkPrivateGpuStreams.GetValueOnGameThread())return;
 TArray<int32> Indices;TArray<const FKKPrivateGpuSource*> Sources;TArray<FKKPrivateGpuDisplay*> States;
 for(auto& Part:Parts)if(Part.PrivateGpuDisplay&&Part.PrivateGpuDisplay->Installed){
  auto& State=*Part.PrivateGpuDisplay;State.PreparedPoseFrame=MAX_uint64;
  auto* Component=Cast<UKKAffineProceduralMeshComponent>(Part.Comp);
  if(!Component||Component->GetGpuStreamProxyEpoch()!=State.Epoch||Component->IsRenderStateDirty())continue;
  bool Ready=true;for(const auto& Receipt:State.Receipts)if(Receipt&&Receipt->Status.Load()!=FKKGpuStreamReceipt::Published){Ready=false;break;}
  if(Ready){Indices.Add(Part.SessionIndex);Sources.Add(&State.Source);States.Add(&State);}
 }
 if(Indices.IsEmpty())return;
 TArray<FKKPrivateGpuPose> Poses;FString Error;
 // A changing source rejects the entire read. Each display then takes its
 // existing revision-checked serial/fallback route without advancing physics.
 if(OKReadPrivateGpuPoses(Session,Indices,Sources,Poses,Error))for(int32 I=0;I<States.Num();++I){States[I]->PreparedPose=MoveTemp(Poses[I]);States[I]->PreparedPoseFrame=GFrameCounter;}
}
void AKKPreviewActor::RefreshRuntimeAccessories()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(KK_PreviewStreams);
    PreparePrivateGpuDisplayPoses();
    TArray<FKKGpuStreamSectionRequest> GpuRequests;
    ON_SCOPE_EXIT {if(!GpuRequests.IsEmpty())UKKAffineProceduralMeshComponent::SubmitGpuStreamsBatch(MoveTemp(GpuRequests));};
	const bool Profile=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ANIM_PROFILE"))==TEXT("1");
    // UpdateMeshSection retains omitted UV/color attributes and owns a copy
    // of mutable streams. Keep scratch capacity across parts AND frames.
    auto& Vertices=RuntimeMutableVertices;auto& Normals=RuntimeMutableNormals;
    auto& Tangents=RuntimeMutableTangents;
    struct FSectionWork {FKKLoadedPart* Part;OkMeshView View;int32 SectionIndex,Count;const FProcMeshSection* Section;const TArray<int32>* Draw;bool DirectNative;};
    struct FSectionJob {int32 WorkIndex,Begin,End;};
    TArray<FSectionWork,TInlineAllocator<32>> Work;
    TArray<FSectionJob,TInlineAllocator<64>> Jobs;
    struct FSourceWork {FKKLoadedPart* Part;OkMeshView View;bool Changed,CacheMotion,DirectNative;int32 FirstJob,EndJob,FirstWork=0,EndWork=0;};
    struct FSourceJob {int32 WorkIndex,Begin,End;bool Changed=false;};
    TArray<FSourceWork,TInlineAllocator<32>> Sources;
    TArray<FSourceJob,TInlineAllocator<128>> SourceJobs;
    TArray<int32,TInlineAllocator<128>> RegularSourceJobs,FusedSourceJobs;
    const bool Indexed=CVarOkSourceIndexedStreams.GetValueOnGameThread()!=0&&CVarOkAnimationGeneratedStreams.GetValueOnGameThread()!=0&&CVarOkAnimationSourceConversion.GetValueOnGameThread()!=0;
    const bool CheckIndexed=CVarOkSourceIndexedParity.GetValueOnGameThread()!=0;
    TArray<FSectionJob,TInlineAllocator<64>> RegularDrawJobs;
    const bool Fused=!Indexed&&CVarOkFusedSourceStreams.GetValueOnGameThread()!=0&&CVarOkAnimationGeneratedStreams.GetValueOnGameThread()!=0;
    const bool CheckFused=CVarOkFusedSourceParity.GetValueOnGameThread()!=0;
    int32 SourceVertices=0;
    int32 TotalDraw=0;
    const bool CacheNormalMotion=CVarOkNormalMotionCache.GetValueOnGameThread()!=0;
    const bool CheckNormalMotion=CVarOkNormalMotionParity.GetValueOnGameThread()!=0;
    const bool RestCache=CVarOkRestNormalCache.GetValueOnGameThread()!=0;
    const bool ZeroRotation=CVarOkZeroOffsetRotation.GetValueOnGameThread()!=0;
    auto ConvertSourceJob=[&](int32 JobIndex){
        auto& Job=SourceJobs[JobIndex];const auto& Source=Sources[Job.WorkIndex];
        auto& Part=*Source.Part;const auto& View=Source.View;bool GeometryChanged=Source.Changed;
		for (int32 VertexIndex = Job.Begin; VertexIndex < Job.End; ++VertexIndex)
		{
			const float* Position = View.positions + VertexIndex * 3;
			const FVector CurrentPosition=OkToUe(Position[0], Position[1], Position[2]);
			GeometryChanged=GeometryChanged||Part.BaseVertices[VertexIndex]!=CurrentPosition;
			Part.BaseVertices[VertexIndex] = CurrentPosition;
			if (View.normals && Part.BaseNormals.Num() == View.n_verts)
			{
				const float* Normal = View.normals + VertexIndex * 3;
				const FVector CurrentNormal=OkToUeDir(Normal[0], Normal[1], Normal[2]);
				GeometryChanged=GeometryChanged||Part.BaseNormals[VertexIndex]!=CurrentNormal;
				Part.BaseNormals[VertexIndex] = CurrentNormal;
			}
			if(View.rest_tangents){const float* T=View.rest_tangents+VertexIndex*4;const FVector4f CurrentTangent(T[0],T[1],T[2],T[3]);GeometryChanged=GeometryChanged||Part.RuntimeSourceTangents[VertexIndex]!=CurrentTangent;Part.RuntimeSourceTangents[VertexIndex]=CurrentTangent;}
            if(Source.CacheMotion&&Part.RuntimeNormalMotionUses[VertexIndex]>=2){
                FQuat Rotation=FQuat::Identity;
                const FVector RestNormal=RestCache&&Part.RuntimeUnitRestNormals.IsValidIndex(VertexIndex)?Part.RuntimeUnitRestNormals[VertexIndex]:Part.RuntimeRestNormals[VertexIndex].GetSafeNormal();
                const FVector CurrentNormal=Part.BaseNormals[VertexIndex].GetSafeNormal();
                if(!RestNormal.IsNearlyZero()&&!CurrentNormal.IsNearlyZero())Rotation=FQuat::FindBetweenNormals(RestNormal,CurrentNormal);
                Part.RuntimeSourceNormalMotion[VertexIndex]=Rotation;
            }
		}
        Job.Changed=GeometryChanged;
    };
	{
    TRACE_CPUPROFILER_EVENT_SCOPE(KK_StreamPrepareSources);
	bool GpuPrepared=false;
	for (FKKLoadedPart& Part : Parts)
	{
        const bool HadGpu=Part.PrivateGpuDisplay.IsValid();
        if(UpdatePrivateGpuDisplay(Part,!GpuPrepared,GpuRequests)){GpuPrepared|=!HadGpu;Part.bNativeDisplayPrepared=false;continue;}
        if(Part.bNativeDisplayPrepared){Part.bNativeDisplayPrepared=false;continue;}
		if ((!Part.bRuntimeAccessory && !SkelFollowers.IsEmpty()) || !Part.Comp || Part.bSlotAttached || Part.SessionIndex == INDEX_NONE)
		{
			continue;
		}
		OkMeshView View;
		FMemory::Memzero(View);
		if (!Session.PartAt(Part.SessionIndex, View) || !View.positions ||
			View.n_verts != Part.BaseVertices.Num())
		{
			continue;
		}
        // Rigid geometry stays in its authored local coordinates while its render
        // matrix follows the socket. Compare native streams before converting
        // those unchanged floats to doubles, irrespective of rigid mesh size.
        const bool DirectNative=(Indexed||Fused)&&View.skel_kind==4&&View.normals&&Part.BaseNormals.Num()==View.n_verts
            &&!Part.bFaceSphereNormals&&!View.eye_draw&&Session.MorphCount(Part.SessionIndex)==0
            &&(Indexed||!(CacheNormalMotion&&Part.bRuntimeNormalMotionReusable));
        const bool NativeSnapshot=(DirectNative||View.skel_kind==0||View.n_verts>=16384)&&!Part.bFaceSphereNormals&&!View.eye_draw&&Session.MorphCount(Part.SessionIndex)==0;
        if(!OKSyncSourceGeometry(Part.RuntimeSourceGeometry,View.n_verts,View.positions,View.normals,View.rest_tangents,NativeSnapshot)){AnimationSkippedVertices+=View.n_verts;continue;}
        bool GeometryChanged=NativeSnapshot;
		if(View.rest_tangents&&Part.RuntimeSourceTangents.Num()!=View.n_verts){Part.RuntimeSourceTangents.SetNumUninitialized(View.n_verts);GeometryChanged=true;}
        const bool CacheMotion=CacheNormalMotion&&Part.bRuntimeNormalMotionReusable&&!Part.bFaceSphereNormals
            &&Part.RuntimeNormalMotionUses.Num()==View.n_verts
            &&Part.RuntimeRestNormals.Num()==View.n_verts&&Part.BaseNormals.Num()==View.n_verts;
        Part.bRuntimeSourceNormalMotionPrepared=CacheMotion;
        if(CacheMotion&&Part.RuntimeSourceNormalMotion.Num()!=View.n_verts)Part.RuntimeSourceNormalMotion.SetNumUninitialized(View.n_verts);
        const int32 SourceIndex=Sources.Num(),FirstJob=SourceJobs.Num();
        const int32 SourceBatch=Indexed&&DirectNative?FMath::Clamp(CVarOkSourceRangeVertices.GetValueOnGameThread(),64,4096):1024;
        for(int32 Begin=0;Begin<View.n_verts;Begin+=SourceBatch)SourceJobs.Add({SourceIndex,Begin,FMath::Min(Begin+SourceBatch,View.n_verts),false});
        Sources.Add({&Part,View,GeometryChanged,CacheMotion,DirectNative,FirstJob,SourceJobs.Num()});SourceVertices+=View.n_verts;
        for(int32 Job=FirstJob;Job<SourceJobs.Num();++Job){if(DirectNative)FusedSourceJobs.Add(Job);else RegularSourceJobs.Add(Job);}
    }
    // Native views and part arrays are captured on the game thread. Workers
    // only convert disjoint source ranges; no UObject/FFI calls run here.
    const int32 SourceMode=CVarOkAnimationSourceConversion.GetValueOnGameThread();
    auto RunSourceJobs=[&](const auto& Indices){
        ParallelFor(Indices.Num(),[&](int32 Index){ConvertSourceJob(Indices[Index]);},
            SourceMode==0||(SourceMode!=2&&SourceVertices<16384)?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);
    };
    RunSourceJobs(RegularSourceJobs);
    if(CheckFused&&!Indexed)RunSourceJobs(FusedSourceJobs);
    for(auto& Source:Sources){
        auto& Part=*Source.Part;const auto& View=Source.View;bool GeometryChanged=Source.Changed;
        for(int32 Job=Source.FirstJob;Job<Source.EndJob;++Job)GeometryChanged|=SourceJobs[Job].Changed;
		if(!GeometryChanged){AnimationSkippedVertices+=View.n_verts;continue;}
		if (Part.bFaceSphereNormals) ApplyFaceSphereNormals(Part.BaseVertices,Part.BaseNormals);
        Source.FirstWork=Work.Num();
        for(int32 SectionIndex=0;SectionIndex<Part.RuntimeSectionOffsets.Num();++SectionIndex){
            const auto* Section=Part.Comp->GetProcMeshSection(SectionIndex);
            const TArray<int32>* Draw=Part.RuntimeDrawVertices.IsValidIndex(SectionIndex)&&!Part.RuntimeDrawVertices[SectionIndex].IsEmpty()?&Part.RuntimeDrawVertices[SectionIndex]:nullptr;
            const int32 Count=Draw?Draw->Num():View.n_verts;
            if(!Section||Part.RuntimeSectionOffsets[SectionIndex].Num()!=View.n_verts||Part.RuntimeSectionNormals[SectionIndex].Num()!=View.n_verts||Section->ProcVertexBuffer.Num()!=Count)continue;
            const int32 WorkIndex=Work.Num();
            Work.Add({&Part,View,SectionIndex,Count,Section,Draw,Source.DirectNative&&!Indexed});TotalDraw+=Count;
            for(int32 Begin=0;Begin<Count;Begin+=512){
                FSectionJob Job{WorkIndex,Begin,FMath::Min(Begin+512,Count)};Jobs.Add(Job);
                if(!Source.DirectNative)RegularDrawJobs.Add(Job);
            }
        }
        Source.EndWork=Work.Num();
    }
    } // source preparation
    auto FillDrawSource=[&](int32 WorkIndex,int32 DrawIndex,FProcMeshVertex& Vertex,bool DirectNative){
        const auto& W=Work[WorkIndex];auto& Part=*W.Part;const auto& View=W.View;
        const int32 SectionIndex=W.SectionIndex;

            const auto* Section=W.Section;const auto* Draw=W.Draw;
            const auto& Offsets=Part.RuntimeSectionOffsets[SectionIndex];const auto& RestSectionNormals=Part.RuntimeSectionNormals[SectionIndex];
			const int32 VertexIndex=Draw?(*Draw)[DrawIndex]:DrawIndex;
            const float* NativePosition=View.positions+VertexIndex*3;
            const float* NativeNormal=View.normals?View.normals+VertexIndex*3:nullptr;
            const FVector SourcePosition=DirectNative?OkToUe(NativePosition[0],NativePosition[1],NativePosition[2]):Part.BaseVertices[VertexIndex];
            const FVector SourceNormal=DirectNative?OkToUeDir(NativeNormal[0],NativeNormal[1],NativeNormal[2]):(Part.BaseNormals.IsValidIndex(VertexIndex)?Part.BaseNormals[VertexIndex]:FVector::ZeroVector);
            FQuat NormalMotion = FQuat::Identity;
			// The base surface has no displaced hull to rotate. Its authored
			// current normal can be copied directly without quaternion work.
			const bool NeedsRotation=SectionIndex!=0 || Offsets[VertexIndex]!=FVector::ZeroVector;
            const bool ReuseRotation=NeedsRotation&&Part.bRuntimeSourceNormalMotionPrepared&&Part.RuntimeNormalMotionUses[VertexIndex]>=2&&Part.RuntimeSourceNormalMotion.IsValidIndex(VertexIndex);
            if(ReuseRotation)NormalMotion=Part.RuntimeSourceNormalMotion[VertexIndex];
            if(NeedsRotation&&(!ReuseRotation||CheckNormalMotion)&&Part.RuntimeRestNormals.IsValidIndex(VertexIndex)&&Part.BaseNormals.IsValidIndex(VertexIndex)){
                FQuat Reference=FQuat::Identity;
                const FVector RestNormal=CheckNormalMotion||!RestCache||!Part.RuntimeUnitRestNormals.IsValidIndex(VertexIndex)?Part.RuntimeRestNormals[VertexIndex].GetSafeNormal():Part.RuntimeUnitRestNormals[VertexIndex];
                const FVector CurrentNormal=SourceNormal.GetSafeNormal();
                if(!RestNormal.IsNearlyZero()&&!CurrentNormal.IsNearlyZero())Reference=FQuat::FindBetweenNormals(RestNormal,CurrentNormal);
                if(ReuseRotation){checkf(NormalMotion.X==Reference.X&&NormalMotion.Y==Reference.Y&&NormalMotion.Z==Reference.Z&&NormalMotion.W==Reference.W,
                    TEXT("Reused outline normal rotation differs: part %d vertex %d"),Part.SessionIndex,VertexIndex);}
                else NormalMotion=Reference;
            }
			Vertex.Position=SourcePosition + (ZeroRotation&&!NeedsRotation?FVector::ZeroVector:NormalMotion.RotateVector(Offsets[VertexIndex]));
			Vertex.Normal=SectionIndex == 0 && Part.BaseNormals.IsValidIndex(VertexIndex) ? SourceNormal
				: NormalMotion.RotateVector(RestSectionNormals[VertexIndex]);
						if(View.rest_tangents) {
					const float* T=View.rest_tangents+VertexIndex*4;
                    Vertex.Tangent=FProcMeshTangent(OkToUeDir(T[0],T[1],T[2]),T[3]<0.f);
                }
    };
    auto FillDraw=[&](int32 WorkIndex,int32 DrawIndex,FProcMeshVertex& Vertex){
        FillDrawSource(WorkIndex,DrawIndex,Vertex,Work[WorkIndex].DirectNative);
        if(CheckFused&&Work[WorkIndex].DirectNative){
            FProcMeshVertex Reference=Vertex;FillDrawSource(WorkIndex,DrawIndex,Reference,false);
            checkf(Vertex.Position==Reference.Position&&Vertex.Normal==Reference.Normal&&Vertex.Tangent.TangentX==Reference.Tangent.TangentX&&Vertex.Tangent.bFlipTangentY==Reference.Tangent.bFlipTangentY,
                TEXT("Fused native stream differs from canonical source: section %d vertex %d"),WorkIndex,DrawIndex);
        }
    };
    if(CVarOkAnimationGeneratedStreams.GetValueOnGameThread()) {
        TArray<FKKGeneratedStreamRequest,TInlineAllocator<64>> Requests;
        for(const auto& W:Work)Requests.Add({W.Part->Comp,W.SectionIndex});
        const double Begin=Profile?FPlatformTime::Seconds():0.;
        bool Generated=false;
        if(Indexed){
            auto GenerateJob=[&](int32 Index,FKKGeneratedStreamRange Emit){
                if(Index<FusedSourceJobs.Num()){
                    const int32 JobIndex=FusedSourceJobs[Index];const auto& Job=SourceJobs[JobIndex];const auto& Source=Sources[Job.WorkIndex];
                    ConvertSourceJob(JobIndex);
                    for(int32 W=Source.FirstWork;W<Source.EndWork;++W){
                        const auto* Draw=Work[W].Draw;
                        auto Lower=[&](int32 Value){int32 First=0,End=Draw?Draw->Num():0;
                            while(First<End){const int32 Mid=First+(End-First)/2;if((*Draw)[Mid]<Value)First=Mid+1;else End=Mid;}return First;};
                        Emit(W,Draw?Lower(Job.Begin):Job.Begin,Draw?Lower(Job.End):Job.End);
                    }
                }else{const auto& Job=RegularDrawJobs[Index-FusedSourceJobs.Num()];Emit(Job.WorkIndex,Job.Begin,Job.End);}
            };
            Generated=UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsBySourceRanges(Requests,FusedSourceJobs.Num()+RegularDrawJobs.Num(),GenerateJob,FillDraw);
            if(Generated&&CheckIndexed){
                int32 Checked=0;
                for(int32 W=0;W<Work.Num();++W)for(int32 I=0;I<Work[W].Count;++I){
                    const auto& Actual=Work[W].Section->ProcVertexBuffer[I];FProcMeshVertex Reference=Actual;
                    FillDrawSource(W,I,Reference,false);
                    checkf(Actual.Position==Reference.Position&&Actual.Normal==Reference.Normal&&Actual.Tangent.TangentX==Reference.Tangent.TangentX&&Actual.Tangent.bFlipTangentY==Reference.Tangent.bFlipTangentY,
                        TEXT("Source indexed draw differs from canonical legacy: work %d vertex %d"),W,I);++Checked;
                }
                static bool Reported=false;if(!Reported&&Checked>0){UE_LOG(LogTemp,Display,TEXT("OK source indexed parity vertices=%d source_jobs=%d"),Checked,FusedSourceJobs.Num());Reported=true;}
            }
        }else Generated=UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsBatchWithSourceJobs(Requests,FillDraw,CheckFused?0:FusedSourceJobs.Num(),[&](int32 Index){ConvertSourceJob(FusedSourceJobs[Index]);});
        if(Generated) {

            if(!Indexed&&CheckFused&&!FusedSourceJobs.IsEmpty()){
                static bool ReportedParity=false;
                if(!ReportedParity){UE_LOG(LogTemp,Display,TEXT("OK fused source parity active source_jobs=%d sections=%d draw_vertices=%d"),FusedSourceJobs.Num(),Work.Num(),TotalDraw);ReportedParity=true;}
            }
            AnimationUploadedVertices+=TotalDraw;
            if(Profile)UE_LOG(LogTemp,Display,TEXT("OK animation global streams vertices=%d sections=%d generated_ms=%.3f"),TotalDraw,Work.Num(),(FPlatformTime::Seconds()-Begin)*1000.);
            return;
        }
    }
    if(Indexed||!CheckFused)for(int32 Job:FusedSourceJobs)ConvertSourceJob(Job);
    if(Vertices.Num()<Work.Num()){Vertices.SetNum(Work.Num());Normals.SetNum(Vertices.Num());Tangents.SetNum(Vertices.Num());}
    for(int32 I=0;I<Work.Num();++I){
        Vertices[I].SetNumUninitialized(Work[I].Count,EAllowShrinking::No);
        Normals[I].SetNumUninitialized(Work[I].Count,EAllowShrinking::No);
        Tangents[I].SetNumUninitialized(Work[I].Count,EAllowShrinking::No);
    }
    // One dispatch/wait for all changed meshes. Jobs own disjoint 512-vertex
    // ranges, so there is no per-vertex search through the complete work list.
    // Native views and CPU sections remain unchanged while workers read them.
    const double FillStart=Profile?FPlatformTime::Seconds():0.;
    {
    TRACE_CPUPROFILER_EVENT_SCOPE(KK_StreamFillDraw);
    ParallelFor(Jobs.Num(),[&](int32 JobIndex){
        const auto& Job=Jobs[JobIndex];const auto& W=Work[Job.WorkIndex];
        auto& Part=*W.Part;const auto& View=W.View;
        const int32 SectionIndex=W.SectionIndex;
        for(int32 DrawIndex=Job.Begin;DrawIndex<Job.End;++DrawIndex){
            FProcMeshVertex Vertex=W.Section->ProcVertexBuffer[DrawIndex];
            FillDraw(Job.WorkIndex,DrawIndex,Vertex);
            Vertices[Job.WorkIndex][DrawIndex]=Vertex.Position;
            Normals[Job.WorkIndex][DrawIndex]=Vertex.Normal;
            Tangents[Job.WorkIndex][DrawIndex]=Vertex.Tangent;
        }
        },TotalDraw<FMath::Max(1,CVarOkAnimationStreamMinParallel.GetValueOnGameThread())?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);
    } // draw fill
        const double UploadStart=Profile?FPlatformTime::Seconds():0.;
        {
        TRACE_CPUPROFILER_EVENT_SCOPE(KK_StreamSubmitDraw);
        TArray<FKKAnimatedStreamRequest,TInlineAllocator<64>> Requests;Requests.Reserve(Work.Num());
        for(int32 I=0;I<Work.Num();++I){const auto& W=Work[I];Requests.Add({W.Part->Comp,W.SectionIndex,&Vertices[I],&Normals[I],&Tangents[I]});AnimationUploadedVertices+=W.Count;}
        UKKAffineProceduralMeshComponent::UpdateAnimatedStreamsBatch(Requests);
        } // stream submission
        if(Profile)UE_LOG(LogTemp,Display,TEXT("OK animation global streams vertices=%d sections=%d jobs=%d fill_ms=%.3f upload_ms=%.3f"),TotalDraw,Work.Num(),Jobs.Num(),(UploadStart-FillStart)*1000.,(FPlatformTime::Seconds()-UploadStart)*1000.);
}

void AKKPreviewActor::CaptureRigidFollowerNeutral()
{
	RigidNeutralBones.Reset();
	CpuRigidNeutralBones.Reset();
	RigidNeutralAllBones.Reset();
	if (!BodySkel)
	{
        // CPU vertices are world-baked at load. Unity retains the renderer
        // under ChangeAccessoryParent's reference Transform during animation.
        for (const FKKLoadedPart& Part : Parts)
        {
            if (Part.RigidFollowBone.IsNone() || Part.bRuntimeAccessory || Part.bSlotAttached || CpuRigidNeutralBones.Contains(Part.RigidFollowBone)) continue;
            float Matrix[16];
            if (Session.BoneWorldMatrix(Part.RigidFollowBone.ToString(),Matrix))
                CpuRigidNeutralBones.Add(Part.RigidFollowBone,OKSkinPaletteToUnreal(Matrix));
            else UE_LOG(LogTemp,Warning,TEXT("OK CPU rigid socket unavailable %s"),*Part.RigidFollowBone.ToString());
        }
		return;
	}
	for (const FKKLoadedPart& Part : Parts)
	{
		if (Part.RigidFollowBone.IsNone() || RigidNeutralBones.Contains(Part.RigidFollowBone))
		{
			continue;
		}
		if (BodySkel->GetBoneIndex(Part.RigidFollowBone) != INDEX_NONE)
		{
			RigidNeutralBones.Add(Part.RigidFollowBone,
				BodySkel->GetSocketTransform(Part.RigidFollowBone, RTS_Component));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("OK rigid follow bone unavailable %s part=%s"),
				*Part.RigidFollowBone.ToString(), *Part.Role);
		}
	}
	if (BodySkel->GetSkinnedAsset())
	{
		const int32 Count = BodySkel->GetNumBones();
		RigidNeutralAllBones.Reserve(Count);
		for (int32 Bone = 0; Bone < Count; ++Bone)
		{
			const FName Name = BodySkel->GetBoneName(Bone);
			RigidNeutralAllBones.Add(Name, BodySkel->GetSocketTransform(Name, RTS_Component));
		}
	}
}

void AKKPreviewActor::UpdateRigidFollowers()
{
	if (!BodySkel)
	{
        TMap<FName,FTransform> Deltas;
        TMap<FName,FMatrix> Residuals;
        for (const auto& Entry : CpuRigidNeutralBones)
        {
            float Matrix[16];
            if (Session.BoneWorldMatrix(Entry.Key.ToString(),Matrix))
            {
                const FMatrix Exact=Entry.Value.Inverse()*OKSkinPaletteToUnreal(Matrix);
                const FTransform Transform(Exact);
                Deltas.Add(Entry.Key,Transform);
                const FMatrix Component=Transform.ToMatrixWithScale();
                Residuals.Add(Entry.Key,Exact.Equals(Component,1.e-6)?FMatrix::Identity:Exact*Component.Inverse());
            }
        }
        for (FKKLoadedPart& Part : Parts)
            if (Part.Comp && !Part.bRuntimeAccessory && !Part.bSlotAttached)
                if (const FTransform* Delta=Deltas.Find(Part.RigidFollowBone))
                {
                    Part.Comp->SetRelativeTransform(*Delta);
                    const FMatrix& Residual=Residuals.FindChecked(Part.RigidFollowBone);
                    if (auto* Affine=Cast<UKKAffineProceduralMeshComponent>(Part.Comp))
                        Affine->SetRenderResidual(Residual);
                }

		return;
	}
	TMap<FName, FTransform> Deltas;
	for (const TPair<FName, FTransform>& Entry : RigidNeutralBones)
	{
		const FTransform Current = BodySkel->GetSocketTransform(Entry.Key, RTS_Component);
		// Procedural vertices are in component space: neutral bone inverse,
		// followed by the current bone transform, preserves their pivot.
		Deltas.Add(Entry.Key, Entry.Value.Inverse() * Current);
	}
	for (FKKLoadedPart& Part : Parts)
	{
		if (Part.Comp)
		{
			if (const FTransform* Delta = Deltas.Find(Part.RigidFollowBone))
			{
				Part.Comp->SetRelativeTransform(*Delta);
			}
		}
	}
}

void AKKPreviewActor::StartAnimLoop()
{
	if(PoseMode==0) {SetPoseMode(1);return;}
	bAnimLoop=true;bGameAnimLooping=true;AnimPhase=0.f;SetActorTickEnabled(true);
	ApplyPoseFrame(0.f);
}

void AKKPreviewActor::MarkShapeDirty(const TArray<float>& Face, const TArray<float>& Body)
{
	PendingFace = Face;
	PendingBody = Body;
	ShapeRequestedAt=FPlatformTime::Seconds();
	bShapeDirty = true;
	if(!bCoordinatedPlayback)SetActorTickEnabled(true);
}

bool AKKPreviewActor::SetOkAnimation(TSharedPtr<FOkAnimPlayer> Player,bool bPublishFrame)
{
	if(!Session.IsOpen()||!Player||!Player->IsValid()||Player->Roles()<1) return false;
	if(bShapeDirty) SetPoseMode(0);
#if PLATFORM_WINDOWS
	::SetEnvironmentVariableW(TEXT("OK_UE_ANIM"), nullptr);
#endif
	WalkSeq=nullptr;OkPlayer=MoveTemp(Player);PoseMode=3;bAnimLoop=true;AnimPhase=0.f;
	AnimDuration=OkPlayer->Duration();bGameAnimLooping=true;
	SetActorTickEnabled(true);
	if(bPublishFrame)
	{
		if(!OkPlayer->ApplySessions({&Session},0.f,0.f,&OkReport)){UE_LOG(LogTemp,Error,TEXT("OK animation first frame failed: %s"),*OkReport);return false;}
		RefreshSkinnedDisplay();
	}
	return true;
}

void AKKPreviewActor::SetExternalPoseDriver(bool bExternal)
{
	if(!bExternal){if(PoseMode==4)SetPoseMode(0);return;}
	if(bShapeDirty) SetPoseMode(0);
#if PLATFORM_WINDOWS
	::SetEnvironmentVariableW(TEXT("OK_UE_ANIM"), nullptr);
#endif
	WalkSeq=nullptr;OkPlayer.Reset();PoseMode=4;bAnimLoop=false;AnimPhase=0.f;AnimDuration=0.f;
	SetActorTickEnabled(true);
}

void AKKPreviewActor::SetPoseMode(int32 Mode)
{
	// The default walk is the imported Studio controller state. Its source
	// Animator pose goes through shape/ABMX, Ver02 and clothing dynamics before
	// publishing the shared GPU palette. A display-only overlay skips physics.
	if (Mode != 0 && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_WALK_ANIM")).IsEmpty())
	{
		if (!SetGameAnimation(Session.IsPortable()?TEXT("studio/anime/00.unity3d|f_aruki_00_00_01"):TEXT("states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01")))
		{
			UE_LOG(LogTemp, Error, TEXT("OK source walk could not load"));
		}
		return;
	}
    if (Mode != 0)
    {
        const FString AssetPath=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_WALK_ANIM"));
        if(!SetUnrealAnimation(LoadObject<UAnimSequence>(nullptr,*AssetPath)))
            UE_LOG(LogTemp,Error,TEXT("OK external animation failed path=%s"),*AssetPath);
        return;
    }
    PoseMode=0;bAnimLoop=false;AnimationReadyClock.Stop();OkPlayer.Reset();
	AnimPhase = 0.f;
	if (PoseMode == 0)
	{
		AnimDuration = 0.f;
	}
	SetActorTickEnabled(true);
	if (Session.IsOpen())
	{
#if PLATFORM_WINDOWS
		::SetEnvironmentVariableW(TEXT("OK_UE_ANIM"), TEXT("1"));
#endif
		const TArray<float> Body = PendingBody.IsEmpty() ? Session.ShapeBody() : PendingBody;
		const TArray<float> Face = PendingFace.IsEmpty() ? Session.ShapeFace() : PendingFace;
		RigidNeutralBones.Reset();
		for (FKKLoadedPart& Part : Parts)
		{
			if (!Part.RigidFollowBone.IsNone() && Part.Comp)
			{
				Part.Comp->SetRelativeTransform(FTransform::Identity);
			}
		}
		Session.ApplyShape(Body, Face);
		bShapeDirty = false;
		RefreshSkinnedDisplay();
		if(BodySkel||CpuRigidNeutralBones.IsEmpty())CaptureRigidFollowerNeutral();
	}
}

bool AKKPreviewActor::PreparePlaybackMaterials(bool Wait)
{
    // Final shader resources remain owned by PendingPreviewMaterials while
    // the display uses shared previews. Do not mistake previews for readiness.
    if(!TickProgressiveMaterials())return false;
    static uint64 LastNonblockingSubmitFrame=MAX_uint64;
    if(bPlaybackMaterialsReady)return true;
    if(bPlaybackRenderFencePending){
        if(Wait)PlaybackRenderFence.Wait();
        else if(!PlaybackRenderFence.IsFenceComplete())return false;
        bPlaybackRenderFencePending=false;bPlaybackMaterialsReady=true;return true;
    }
    // Keep card resource loading responsive. Shader readiness belongs to the
    // animation loading boundary, before the playback clock is published.
#if WITH_EDITOR
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(KK_PreviewMaterialReady);
		const double MaterialReadyStart=FPlatformTime::Seconds();
		TSet<UMaterialInterface*> ReadyMaterials;
		TArray<FMaterial*> PendingResources;
		TInlineComponentArray<UPrimitiveComponent*> Displays;
		GetComponents(Displays);
		int32 InitiallyCompiling=0,Submitted=0;
		for(auto* Display:Displays)for(int32 Index=0;Index<Display->GetNumMaterials();++Index)
			if(auto* Material=Display->GetMaterial(Index))
				if(!ReadyMaterials.Contains(Material))
				{
					ReadyMaterials.Add(Material);
					InitiallyCompiling+=Material->IsCompiling()?1:0;
					// Prepare only the active world's rendering platform. Submit
					// all materials before waiting so compilation stays parallel.
					const auto Level=GetWorld()?GetWorld()->GetFeatureLevel():GMaxRHIFeatureLevel;
					if(auto* Resource=Material->GetMaterialResource(GetFeatureLevelShaderPlatform_Checked(Level)))
						if(!Resource->IsGameThreadShaderMapComplete())
						{
						if(!PendingResources.Contains(Resource)&&!Material->IsCompiling()) {
                                // Nonblocking polling must also bound job submission,
                                // which can itself stall the game thread for many materials.
                                if(!Wait&&(Submitted>=1||LastNonblockingSubmitFrame==GFrameCounter))return false;
                                Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::ForceLocal);++Submitted;if(!Wait)LastNonblockingSubmitFrame=GFrameCounter;
                            }
							PendingResources.AddUnique(Resource);
						}
				}
        // Shader compiler workers progress independently. Live APNG entry and
        // readiness polling must never wait for a character's new permutations.
        if(!Wait&&!PendingResources.IsEmpty())return false;
		// An empty list can wait for unrelated global shader jobs. Once all
        // display resources are complete there is nothing left to wait for.
        if(!PendingResources.IsEmpty())FMaterial::FinishCompilation(TEXT("KK preview material readiness"),PendingResources);
		UE_LOG(LogTemp,Display,TEXT("OK preview material readiness materials=%d initially_compiling=%d wait_ms=%.3f"),
			ReadyMaterials.Num(),InitiallyCompiling,(FPlatformTime::Seconds()-MaterialReadyStart)*1000.);
	}
#endif
    // Compiled shaders can leave component render states deferred until the
    // first playback frame. Complete the world's supported end-of-frame update
    // path and the queued render resources at this one-time loading boundary.
    // Do not call component deferred-update internals or advance the animation.
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(KK_PreviewRenderReady);
        const double RenderReadyStart=FPlatformTime::Seconds();
        if(UWorld* World=GetWorld()) World->SendAllEndOfFrameUpdates();
        // Poll through RHI submission as well as the render thread. Enqueuing
        // uploads alone cannot publish a prepared display as ready to switch.
        PlaybackRenderFence.BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);
        bPlaybackRenderFencePending=true;
        if(Wait)PlaybackRenderFence.Wait();
        UE_LOG(LogTemp,Display,TEXT("OK preview render readiness wait_ms=%.3f phase=%.6f"),
            (FPlatformTime::Seconds()-RenderReadyStart)*1000.,AnimPhase);
    }
    if(!Wait)return false;
    bPlaybackRenderFencePending=false;bPlaybackMaterialsReady=true;
    return true;
}

bool AKKPreviewActor::SetUnrealAnimation(UAnimSequence* Sequence,bool bLoop)
{
    if(!Session.IsOpen()||!BodySkel||!Sequence||!Sequence->GetSkeleton()) return false;
    float Duration=Sequence->GetPlayLength();
#if WITH_EDITOR
    if(TScriptInterface<IAnimationDataModel> Model=Sequence->GetDataModelInterface()) Duration=Model->GetPlayLength();
#endif
    if(!FMath::IsFinite(Duration)||Duration<=0.f) return false;
    // Validate before resetting the current animation or its particle history.
    TArray<OkBodyPose> Frame;
    if(!Session.BuildUnrealBodyFrame(BodySkel->GetSkinnedAsset()->GetRefSkeleton(),Sequence,0.f,Frame)) return false;
    SetPoseMode(0);WalkSeq=Sequence;PoseMode=1;AnimPhase=0.f;AnimDuration=Duration;
    bAnimLoop=bLoop;bGameAnimLooping=bLoop;SetActorTickEnabled(true);
    ApplyPoseFrame(0.f);
    PreparePlaybackMaterials();
    AnimationReadyClock.Ready(GFrameCounter);
    UE_LOG(LogTemp,Display,TEXT("OK external animation sequence=%s duration=%.6f looping=%d source_physics=1"),*Sequence->GetName(),Duration,bLoop);
    return true;
}

bool AKKPreviewActor::SetGameAnimation(const FString& ClipName, bool bPublishFrame)
{
	const double SwitchBegin=FPlatformTime::Seconds();
	if (!Session.IsOpen())
	{
		return false;
	}
	if(!Session.HasAnimationClip(ClipName)) return false;
	// Switching Animator states is not a shape/character reload. Rebuilding a
	// neutral pose here reinitializes every private DynamicBone (240 warmup
	// steps each). Keep the loaded shape and rigid neutral reference unless an
	// actual pending shape edit needs to be applied.
	if(bShapeDirty) SetPoseMode(0);
	WalkSeq=nullptr;
	const double SwitchNeutral=FPlatformTime::Seconds();
	if (!(Session.IsPortable()&&!ClipName.StartsWith(TEXT("states:"))&&!FOkSession::IsVideoClipKey(ClipName)?Session.SelectImportedSharedClip(ClipName):Session.SetAnimClip(ClipName)))
	{
		return false;
	}
#if PLATFORM_WINDOWS
	::SetEnvironmentVariableW(TEXT("OK_UE_ANIM"), nullptr);
#endif
	PoseMode = 2;
	bAnimLoop = true;
	AnimPhase = 0.f;
	AnimDuration = Session.AnimDuration(); // Valid one-frame clips can be shorter than 0.05s.
	bGameAnimLooping = Session.IsAnimLooping();
	SetActorTickEnabled(true);
	const double SwitchSelected=FPlatformTime::Seconds();
	if (bPublishFrame && Session.ApplyAnimFrame(0.f, 0.f))
	{
		RefreshSkinnedDisplay();
	}
	PreparePlaybackMaterials();
	AnimationReadyClock.Ready(GFrameCounter);
	UE_LOG(LogTemp, Display, TEXT("OK game animation clip=%s duration=%.3f looping=%d"), *ClipName, AnimDuration, bGameAnimLooping);
	if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_PAIR_PROFILE"))==TEXT("1"))UE_LOG(LogTemp,Display,TEXT("OK animation switch neutral_ms=%.6f select_ms=%.6f frame_ms=%.6f"),(SwitchNeutral-SwitchBegin)*1000.,(SwitchSelected-SwitchNeutral)*1000.,(FPlatformTime::Seconds()-SwitchSelected)*1000.);
	return true;
}

bool AKKPreviewActor::SetGameAnimationFloat(const FString& Name,float Value)
{
    if(PoseMode!=2||!FMath::IsFinite(Value)) return false;
    if(!Session.SetAnimationFloat(Name,Value)) return false;
    AnimDuration=Session.AnimDuration();
    AnimPhase=Session.AnimationElapsed();
    // Changing a blend parameter preserves normalized phase and does not
    // advance either face clocks or physics particle history.
    if(!Session.ApplyAnimFrame(AnimPhase,0.f)) return false;
    RefreshSkinnedDisplay();return true;
}

bool AKKPreviewActor::SetStudioAnimation(UKKAnimationCatalog* Catalog,FName Row,FString& Error,float Height,float Breast,float Motion)
{
    const double Start=FPlatformTime::Seconds();
    if(!Catalog||!Session.IsOpen()) {Error=TEXT("Studio catalog/session unavailable");return false;}
    FString Key;bool IsH,IsMotion;int32 Layer;
    if(!Catalog->StudioSettings(Row,IsH,Layer,IsMotion)||!Catalog->ResolveStudioAnimation(Row,
        Session.ClipNamesForSource(Catalog->StudioSource(Row)),Key,Error)) return false;
    FString YureSource,YureOwner;Catalog->StudioYureSource(Row,YureSource,YureOwner);
    if(!Session.SetStudioAnimation(Key,IsH,Height,Breast,IsMotion,Motion,YureSource,YureOwner,Catalog->StudioPVMask(Row))) {Error=TEXT("Studio source initialization failed");return false;}
    WalkSeq=nullptr;PoseMode=2;bAnimLoop=true;AnimPhase=0.f;AnimDuration=Session.AnimDuration();bGameAnimLooping=Session.IsAnimLooping();
    SetActorTickEnabled(true);
    const double Initialized=FPlatformTime::Seconds();
    const FString ProfileBefore=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ANIM_PROFILE"));
    // Detailed per-part logging is a separate diagnostic option; it must
    // not contaminate the ordinary menu latency measurement.
    const bool Profile=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MENU_DETAIL_PROFILE"))==TEXT("1");
    if(Profile)FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_PROFILE"),TEXT("1"));
    const bool Applied=Session.ApplyAnimFrame(0.f,0.f);
    if(Profile)FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_PROFILE"),*ProfileBefore);
    if(!Applied)return false;
    const double Framed=FPlatformTime::Seconds();
    RefreshSkinnedDisplay();
    PreparePlaybackMaterials();
    AnimationReadyClock.Ready(GFrameCounter);
    if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MENU_PROFILE"))==TEXT("1"))UE_LOG(LogTemp,Display,TEXT("OK Studio switch init_ms=%.3f frame_ms=%.3f render_ms=%.3f"),(Initialized-Start)*1000.,(Framed-Initialized)*1000.,(FPlatformTime::Seconds()-Framed)*1000.);
    UE_LOG(LogTemp,Display,TEXT("OK Studio animation row=%s key=%s H=%d layer=%d options=%.3f,%.3f motion=%.3f duration=%.6f yure=%d%d%d%d pv=%d%d%d%d"),
        *Row.ToString(),*Key,IsH,Layer,Height,Breast,Motion,AnimDuration,
        Session.StudioYureFlag(0),Session.StudioYureFlag(1),Session.StudioYureFlag(2),Session.StudioYureFlag(3),Session.StudioPVFlag(0),Session.StudioPVFlag(1),Session.StudioPVFlag(2),Session.StudioPVFlag(3));return true;
}

bool AKKPreviewActor::SetGameSceneMode(int32 Mode,bool bPublishFrame)
{
	if (!Session.IsOpen() || !Session.SetAnimSceneMode(Mode))
	{
		return false;
	}
	if (bPublishFrame && PoseMode == 2 && Session.ApplyAnimFrame(AnimPhase, 0.f))
	{
		RefreshSkinnedDisplay();
	}
	UE_LOG(LogTemp, Display, TEXT("OK game animation scene mode=%d"), Mode);
	return true;
}

void AKKPreviewActor::SetMouthOpen(float Open01)
{
	MouthOpen01 = FMath::Clamp(Open01, 0.f, 1.f);
	ApplyMouth();
}

void AKKPreviewActor::SetMouthPattern(int32 Pattern, bool bBlend)
{
	MouthPattern = FMath::Clamp(Pattern, 0, GetMouthPatternCount() - 1);
	FaceTransitions[0].Change(MouthPattern,bBlend);
	ApplyMouth();
}

int32 AKKPreviewActor::GetHandShapeCount() const
{
	return Session.HandShapeCount();
}

bool AKKPreviewActor::SetHandShape(int32 Side, bool Enabled, int32 A, int32 B, float Blend)
{
	if (!Session.SetHandShape(Side, Enabled, A, B, Blend)) return false;
	// Turning off an override that is not on changes no bone (a disabled hand follows the animation whatever its
	// stored keys): no new frame. A card's load set both hands that way and re-evaluated the pose (~0.15 s).
	const bool bWasOn = Side >= 0 && Side < 2 && bHandOverrideOn[Side];
	if (Side >= 0 && Side < 2) bHandOverrideOn[Side] = Enabled;
	if (!Enabled && !bWasOn) return true;
	if (HandDisplayBatchDepth > 0) { bHandDisplayPending = true; return true; }
	// Re-evaluate the same frame; preserve both animation clock and physics
	// history. ApplyAnimFrame restores the pre-hand pose before each override.
	if (PoseMode == 0 && !Session.StaticInteractionFrame(0.f)) return false;
	ApplyPoseFrame(0.f);
	return true;
}

void AKKPreviewActor::EndHandDisplayUpdate()
{
    check(HandDisplayBatchDepth > 0);
    if (--HandDisplayBatchDepth == 0 && bHandDisplayPending) {
        bHandDisplayPending = false;
        if (PoseMode == 0 && !Session.StaticInteractionFrame(0.f)) return;
        ApplyPoseFrame(0.f);
    }
}

FString AKKPreviewActor::GetMouthPatternLabel(int32 Pattern) const
{
	for (const FOkMouthSlot& Slot : MouthSlots)
	{
		if (Slot.Patterns.IsValidIndex(Pattern) && !Slot.Patterns[Pattern].Key.IsNone()
			&& !Slot.Patterns[Pattern].Value.IsNone())
		{
			return FString::Printf(TEXT("嘴型 %d：%s → %s"), Pattern,
				*Slot.Patterns[Pattern].Key.ToString(), *Slot.Patterns[Pattern].Value.ToString());
		}
	}
	return FString::Printf(TEXT("嘴型 %d"), Pattern);
}

int32 AKKPreviewActor::GetMouthPatternCount() const
{
	int32 Count = 0;
	for (const FOkMouthSlot& Slot : MouthSlots)
	{
		Count = FMath::Max(Count, Slot.Patterns.Num());
	}
	return Count > 0 ? Count : OKMouthPatternCount;
}

void AKKPreviewActor::InitializeFaceControls()
{
    FaceClock=0.f;
    FaceBlink=FOKFbsBlink();
    FaceBlink.Frequency=FMath::Clamp(FMath::TruncToInt(Session.FaceRuntimeSetting(0)),0,255);
    FaceBlink.BaseSpeed=Session.FaceRuntimeSetting(1);
    bBrowSyncBlink=Session.FaceRuntimeSetting(2)!=0.f;
    if(!bManualBlinkSetting) bAutomaticBlink=Session.FaceRuntimeSetting(3)!=0.f;
    FaceBlink.FixedFlags=bAutomaticBlink?0:1;
    BlinkEyeRate=1.f;
    BlinkBrowRate=(!bAutomaticBlink || bBrowSyncBlink)?1.f:0.f;
    for (int32 Control = 1; Control <= 2; ++Control)
    {
        if (!bManualFaceControl[Control - 1])
        {
            FacePattern[Control - 1] = 0;
            FaceOpen01[Control - 1] = Session.FaceDefaultRate(Control);
        }
    }
    for (FOkMouthSlot& Slot : MouthSlots)
    {
        auto NameAt = [&](int32 Index) -> FName {
            OkMorphView Morph = {};
            return Index >= 0 && Session.MorphAt(Slot.SessionPartIndex, Index, Morph) && Morph.name
                ? FName(UTF8_TO_TCHAR(Morph.name)) : NAME_None;
        };
        for (int32 Control = 1; Control <= 2; ++Control)
        {
            auto& Patterns = Control == 1 ? Slot.EyePatterns : Slot.BrowPatterns;
            for (int32 Pattern = 0; Pattern < Session.FacePatternCount(Slot.SessionPartIndex, Control); ++Pattern)
            {
                int32 Close = -1, Open = -1;
                if (Session.FacePair(Slot.SessionPartIndex, Control, Pattern, Close, Open))
                    Patterns.Emplace(NameAt(Close), NameAt(Open));
            }
        }
        Slot.MorphNames.Reset();
        for (int32 Morph = 0; Morph < Session.MorphCount(Slot.SessionPartIndex); ++Morph)
        {
            const FName Name=NameAt(Morph);Slot.MorphNames.Add(Name);
            Slot.BaselineWeights.Add(Name, Session.FaceBaselineWeight(Slot.SessionPartIndex, Morph));
        }
    }
    UE_LOG(LogTemp, Display, TEXT("OK face controls slots=%d eyes=%d brow=%d defaults=(%.6f,%.6f)"),
        MouthSlots.Num(), GetFacePatternCount(1), GetFacePatternCount(2), FaceOpen01[0], FaceOpen01[1]);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        FacePattern[Index] = FMath::Clamp(FacePattern[Index], 0, FMath::Max(0, GetFacePatternCount(Index + 1) - 1));
        FaceTransitions[Index+1].Reset(FacePattern[Index]);
    }
    MouthPattern=FMath::Clamp(MouthPattern,0,GetMouthPatternCount()-1);
    FaceTransitions[0].Reset(MouthPattern);
    SetEyeGaze(EyeGaze[0],EyeGaze[1],EyeGaze[2]);
}

int32 AKKPreviewActor::GetFacePatternCount(int32 Control) const
{
    if (Control < 1 || Control > 2) return 0;
    int32 Count = MAX_int32;
    for (const FOkMouthSlot& Slot : MouthSlots)
    {
        const auto& Patterns = Control == 1 ? Slot.EyePatterns : Slot.BrowPatterns;
        if (!Patterns.IsEmpty()) Count = FMath::Min(Count, Patterns.Num());
    }
    return Count == MAX_int32 ? 0 : Count;
}

FString AKKPreviewActor::GetFacePatternLabel(int32 Control, int32 Pattern) const
{
    if (Control < 1 || Control > 2) return FString();
    for (const FOkMouthSlot& Slot : MouthSlots)
    {
        const auto& Patterns = Control == 1 ? Slot.EyePatterns : Slot.BrowPatterns;
        if (Patterns.IsValidIndex(Pattern))
            return FString::Printf(TEXT("%s %d: %s → %s"), Control == 1 ? TEXT("眼睛") : TEXT("眉毛"),
                Pattern, *Patterns[Pattern].Key.ToString(), *Patterns[Pattern].Value.ToString());
    }
    return FString();
}

bool AKKPreviewActor::SetFaceControl(int32 Control, int32 Pattern, float Open01, bool bBlend)
{
    if (Control < 1 || Control > 2 || Pattern < 0 || Pattern >= GetFacePatternCount(Control)
        || !FMath::IsFinite(Open01)) return false;
    FacePattern[Control - 1] = Pattern;
    bManualFaceControl[Control - 1] = true;
    FaceOpen01[Control - 1] = FMath::Clamp(Open01, 0.f, 1.f);
    FaceTransitions[Control].Change(Pattern,bBlend);
    ApplyMouth();
    return true;
}

bool AKKPreviewActor::SetFacePattern(int32 Control,int32 Pattern,bool bBlend)
{
    if(Control<1 || Control>2 || Pattern<0 || Pattern>=GetFacePatternCount(Control)) return false;
    FacePattern[Control-1]=Pattern;
    FaceTransitions[Control].Change(Pattern,bBlend);
    ApplyMouth();
    return true;
}

void AKKPreviewActor::SetAutomaticBlink(bool bEnabled)
{
    bManualBlinkSetting=true;
    bAutomaticBlink=bEnabled;
    FaceBlink.FixedFlags=bEnabled?0:1;
    if(bEnabled) bManualFaceControl[0]=bManualFaceControl[1]=false;
    else BlinkEyeRate=BlinkBrowRate=1.f;
    ApplyMouth();
}

void AKKPreviewActor::ForceBlink()
{
    FaceBlink.ForceClose(FaceClock,[](float Min,float Max){return FMath::FRandRange(Min,Max);},
        [](int32 Min,int32 ExclusiveMax){return ExclusiveMax>Min?FMath::RandRange(Min,ExclusiveMax-1):Min;});
}

float AKKPreviewActor::EffectiveFaceRate(int32 Control) const
{
    if(Control==0) return bSpeechActive&&bSpeechDrivesMouth?FMath::Lerp(SpeechOpenMin,1.f,SpeechOpen):MouthOpen01;
    const int32 Index=Control-1;
    if(bManualFaceControl[Index]) return FaceOpen01[Index];
    const float Fixed=Session.FaceRuntimeSetting(6+Index);
    if(Fixed>=0.f) return Fixed;
    float Maximum=Session.FaceDefaultRate(Control);
    if(Control==1 && Session.FaceRuntimeSetting(11)!=0.f)
    {
        const float Correct=OKEyeCorrectOpenMax(Maximum,EyeGaze[0],EyeGaze[2],Session.FaceRuntimeSetting(8),Session.FaceRuntimeSetting(9),Session.FaceRuntimeSetting(10));
        if(Correct>=0.f) Maximum=Correct; // FBSBase negative sentinel.
    }
    return FMath::Lerp(Session.FaceRuntimeSetting(4+Index),Maximum,
        Control==1?BlinkEyeRate:BlinkBrowRate);
}

bool AKKPreviewActor::SetEyeGaze(float LeftHorizontal,float RightHorizontal,float Vertical)
{
    if(!FMath::IsFinite(LeftHorizontal)||!FMath::IsFinite(RightHorizontal)||!FMath::IsFinite(Vertical)) return false;
    if(!Session.SetEyeGaze(LeftHorizontal,RightHorizontal,Vertical)) return false;
    EyeGaze[0]=FMath::Clamp(LeftHorizontal,-1.f,1.f);
    EyeGaze[1]=FMath::Clamp(RightHorizontal,-1.f,1.f);
    EyeGaze[2]=FMath::Clamp(Vertical,-1.f,1.f);
    for(auto& Part:Parts)
    {
        if(Session.EyeMaterialSetting(Part.SessionIndex,-1)==0.f || (!Part.Comp && !Part.GpuEyeMaterial)) continue;
        OkMeshView View={};
        if(!Session.PartAt(Part.SessionIndex,View)||!View.uvs || View.n_verts!=Part.UVs.Num()) return false;
        for(int32 Vertex=0;Vertex<View.n_verts;++Vertex)
            Part.UVs[Vertex]=FVector2D((View.eye_draw ? View.eye_sample_uvs : View.uvs)[Vertex*2],1.f-(View.eye_draw ? View.eye_sample_uvs : View.uvs)[Vertex*2+1]);
        if(View.eye_draw && View.uv2 && View.uv3) {
            Part.UV4.SetNum(View.n_verts);Part.UV2.SetNum(View.n_verts);
            for(int32 Vertex=0;Vertex<View.n_verts;++Vertex) {
                Part.UV4[Vertex]=FVector2D(View.uv2[Vertex*2],1.f-View.uv2[Vertex*2+1]);
                Part.UV2[Vertex]=FVector2D(View.uv3[Vertex*2],1.f-View.uv3[Vertex*2+1]);
            }
        }
        // Keep source positions, normals, winding and independent UV1/2.
		if(Part.GpuEyeMaterial){
			const TArray<FVector2D>* Target[]={&Part.UVs,&Part.UV4,&Part.UV2}; FLinearColor Rows[3][2];
			for(int32 Channel=0;Channel<3;++Channel) if(!FitEyeUVRows(Part.GpuEyeUV[Channel],*Target[Channel],Rows[Channel])) {
				UE_LOG(LogTemp,Error,TEXT("GPU eye gaze is not affine part=%d channel=%d"),Part.SessionIndex,Channel); return false;
			}
			for(int32 Channel=0;Channel<3;++Channel)for(int32 Axis=0;Axis<2;++Axis)
				Part.GpuEyeMaterial->SetVectorParameterValue(*FString::Printf(TEXT("KKEyeUV%dRow%d"),Channel,Axis),Rows[Channel][Axis]);
		}
        if(!Part.Comp) continue;
        for(int32 Section=0;Section<Part.Comp->GetNumSections();++Section)
        {
            const auto* Mesh=Part.Comp->GetProcMeshSection(Section);
            if(!Mesh || Mesh->ProcVertexBuffer.Num()!=Part.UVs.Num()) return false;
            TArray<FVector> Positions,Normals;TArray<FColor> Colors;TArray<FProcMeshTangent> Tangents;
            for(const auto& Vertex:Mesh->ProcVertexBuffer) {
                Positions.Add(Vertex.Position);Normals.Add(Vertex.Normal);Colors.Add(Vertex.Color);Tangents.Add(Vertex.Tangent);
            }
            if(View.eye_draw) {
                const TArray<FVector2D> Empty;
                UKKAffineProceduralMeshComponent::UpdateStreams(Part.Comp,Section,Positions,Normals,Part.UVs,Part.UV4,Part.UV2,Empty,Colors,Tangents);
            } else UKKAffineProceduralMeshComponent::UpdateStreams(Part.Comp,Section,Positions,Normals,Part.UVs,Colors,Tangents);
        }
    }
    ApplyMouth();
    return true;
}

bool AKKPreviewActor::SetEyeLookTarget(FVector Target,int32 Pattern,bool Enabled)
{
    if(Target.ContainsNaN() || Pattern<0 || Pattern>=Session.EyeLookPatternCount()) return false;
    EyeLookWorldTarget=Target;EyeLookPattern=Pattern;bEyeLookTracking=Enabled;bEyeLookFollowCamera=false;
    return true;
}
void AKKPreviewActor::StopEyeLookTracking() {bEyeLookTracking=false;bEyeLookFollowCamera=false;}
bool AKKPreviewActor::FollowEyeLookCamera(int32 Pattern)
{
    if(!StudioCam || !SetEyeLookTarget(StudioCam->GetActorLocation(),Pattern,true)) return false;
    bEyeLookFollowCamera=true;return true;
}
void AKKPreviewActor::UseGalleryPhysics()
{
    if(bGalleryPhysics)return;
    bGalleryPhysics=true;
    AddPairPhysics();
}
void AKKPreviewActor::AddPairPhysics()
{
    if(PairPhysicsUsers++==0)Session.SetPhysXSecondary(true);
    UE_LOG(LogTemp,Display,TEXT("Pair physics %s: on (users=%d group=%llu)"),*GetName(),PairPhysicsUsers,Session.PhysicsGroup());
}
void AKKPreviewActor::RemovePairPhysics()
{
    if(PairPhysicsUsers>0&&--PairPhysicsUsers==0)Session.SetPhysXSecondary(false);
    UE_LOG(LogTemp,Display,TEXT("Pair physics %s: users=%d group=%llu"),*GetName(),PairPhysicsUsers,Session.PhysicsGroup());
}
bool AKKPreviewActor::FollowHeadLookCamera(bool Enabled)
{
    if(Enabled&&!StudioCam)return false;
    const FVector Local=Enabled?GetActorTransform().InverseTransformPosition(StudioCam->GetActorLocation()):FVector::ZeroVector;
    if(!Session.SetHeadLook(FVector(-Local.X/100.f,Local.Z/100.f,Local.Y/100.f),Enabled))return false;
    bHeadLookFollowCamera=Enabled;bHeadLookPoseDirty=true;return true;
}
void AKKPreviewActor::UpdateHeadLookTarget()
{
    if(!bHeadLookFollowCamera||!StudioCam)return;
    const FVector Local=GetActorTransform().InverseTransformPosition(StudioCam->GetActorLocation());
    Session.SetHeadLook(FVector(-Local.X/100.f,Local.Z/100.f,Local.Y/100.f),true);
}
bool AKKPreviewActor::AdvanceEyeLook(float DeltaSeconds)
{
    if(!bEyeLookTracking) return true;
    if(bEyeLookFollowCamera && StudioCam) EyeLookWorldTarget=StudioCam->GetActorLocation();
    const FVector Local=GetActorTransform().InverseTransformPosition(EyeLookWorldTarget);
    const FVector Unity(-Local.X/100.f,Local.Z/100.f,Local.Y/100.f);
    float Rates[3]={};
    if(!Session.StepEyeLook(Unity,EyeLookPattern,DeltaSeconds,true,Rates)) {
        UE_LOG(LogTemp,Warning,TEXT("EyeLook native rejected pattern=%d dt=%.9f target=(%.9f,%.9f,%.9f) rates=(%.9f,%.9f,%.9f)"),
            EyeLookPattern,DeltaSeconds,Unity.X,Unity.Y,Unity.Z,Rates[0],Rates[1],Rates[2]);return false;
    }
    const bool Applied=SetEyeGaze(Rates[0],Rates[1],Rates[2]);
    if(!Applied) UE_LOG(LogTemp,Warning,TEXT("EyeLook display rejected rates=(%.9f,%.9f,%.9f)"),Rates[0],Rates[1],Rates[2]);
    return Applied;
}
void AKKPreviewActor::AdvanceFaceAnimation(float DeltaSeconds)
{
    if(!FMath::IsFinite(DeltaSeconds) || DeltaSeconds<0.f) return;
    const int32 OldEye=OKFbsOpenPercent(EffectiveFaceRate(1)),OldBrow=OKFbsOpenPercent(EffectiveFaceRate(2));
    bool Changed=false;
    for(auto& Transition:FaceTransitions) Changed|=Transition.Advance(DeltaSeconds);
    FaceClock+=DeltaSeconds;
    FaceBlink.Calculate(FaceClock,[](float Min,float Max){return FMath::FRandRange(Min,Max);},
        [](int32 Min,int32 ExclusiveMax){return ExclusiveMax>Min?FMath::RandRange(Min,ExclusiveMax-1):Min;});
    if(FaceBlink.FixedFlags==0)
    {
        BlinkEyeRate=FaceBlink.OpenRate;
        if(bBrowSyncBlink) BlinkBrowRate=FaceBlink.OpenRate;
    }
    Changed|=OldEye!=OKFbsOpenPercent(EffectiveFaceRate(1)) || OldBrow!=OKFbsOpenPercent(EffectiveFaceRate(2));
    if(Changed) ApplyMouth();
}

TArray<FName> AKKPreviewActor::GetPartMorphNames(int32 PartIndex) const
{
    TArray<FName> Names;
    for(int32 I=0; I<Session.MorphCount(PartIndex); ++I) {
        OkMorphView M{};
        if(Session.MorphAt(PartIndex,I,M) && M.name) Names.Add(FName(UTF8_TO_TCHAR(M.name)));
    }
    return Names;
}
bool AKKPreviewActor::SetPartMorphWeight(int32 PartIndex,FName Name,float Percent)
{
    if(!FMath::IsFinite(Percent) || !GetPartMorphNames(PartIndex).Contains(Name)) return false;
    if(!MouthSlots.ContainsByPredicate([PartIndex](const FOkMouthSlot& S){return S.SessionPartIndex==PartIndex;})) return false;
    PartMorphWeights.FindOrAdd(PartIndex).Add(Name,FMath::Clamp(Percent,0.f,100.f));
    ApplyMouth();return true;
}

namespace
{
static const TCHAR* GVaginaKokanBones[] = {TEXT("cf_j_kokan"), TEXT("cf_J_Vagina_root"), TEXT("k_f_kokan_00")};
static const TCHAR* GVaginaTipBones[] = {TEXT("cm_J_dan_f_top"), TEXT("cm_J_dan109_00"), TEXT("cm_J_dan105_00"), TEXT("k_f_dan_end")};
struct FVaginaPullBone { const TCHAR* Name; float Weight; };
// BoneNames.KokanPullBones and the first 11 KokanPullWeights. The shipped
// config leaves enableKokanPush off, so this is only a radial stand-in.
static const FVaginaPullBone GVaginaPullBones[] = {
	{TEXT("cf_J_Vagina_L.011"), 0.1f}, {TEXT("cf_J_Vagina_R.011"), 0.25f},
	{TEXT("cf_J_Vagina_L.012"), 0.25f}, {TEXT("cf_J_Vagina_R.012"), 0.5f},
	{TEXT("cf_J_Vagina_L.013"), 0.5f}, {TEXT("cf_J_Vagina_R.013"), 0.5f},
	{TEXT("cf_J_Vagina_L.014"), 0.5f}, {TEXT("cf_J_Vagina_L.015"), 0.5f},
	{TEXT("cf_J_Vagina_R.014"), 0.5f}, {TEXT("cf_J_Vagina_R.015"), 0.5f},
	{TEXT("cf_J_Vagina_B.010"), 0.5f},
};
static constexpr float GVaginaOpenSpanCm = 1.f;
static constexpr float GVaginaPullCm = 1.5f;

static void EachVaginaPose(const AKKPreviewActor& Actor, TFunctionRef<void(UPoseableMeshComponent*)> Visit)
{
	if (Actor.BodySkel) Visit(Actor.BodySkel);
	for (UPoseableMeshComponent* Follower : Actor.SkelFollowers)
		if (Follower && Follower != Actor.BodySkel) Visit(Follower);
}

static int32 FindVaginaBone(const UPoseableMeshComponent* Comp, const TCHAR* const* Names, int32 Count)
{
	if (!Comp || !Comp->GetSkinnedAsset() || Comp->GetNumComponentSpaceTransforms() <= 0) return INDEX_NONE;
	const FReferenceSkeleton& Ref = Comp->GetSkinnedAsset()->GetRefSkeleton();
	for (int32 NameIndex = 0; NameIndex < Count; ++NameIndex)
	{
		const int32 Bone = Ref.FindBoneIndex(FName(Names[NameIndex]));
		if (Bone != INDEX_NONE && Comp->GetComponentSpaceTransforms().IsValidIndex(Bone)) return Bone;
	}
	return INDEX_NONE;
}

static bool VaginaBoneWorld(const AKKPreviewActor& Actor, const TCHAR* const* Names, int32 Count, FVector& Out)
{
	bool bFound = false;
	EachVaginaPose(Actor, [&](UPoseableMeshComponent* Comp)
	{
		if (bFound) return;
		const int32 Bone = FindVaginaBone(Comp, Names, Count);
		if (Bone == INDEX_NONE) return;
		const FTransform& Local = Comp->GetComponentSpaceTransforms()[Bone];
		if (Local.ContainsNaN()) return;
		const FVector World = Comp->GetComponentTransform().TransformPosition(Local.GetLocation());
		if (!FMath::IsFinite(World.X) || !FMath::IsFinite(World.Y) || !FMath::IsFinite(World.Z)) return;
		Out = World;
		bFound = true;
	});
	return bFound;
}

static bool PoseHasVaginaPullBone(const AKKPreviewActor& Actor)
{
	bool bFound = false;
	EachVaginaPose(Actor, [&](UPoseableMeshComponent* Comp)
	{
		if (bFound || !Comp || !Comp->GetSkinnedAsset()) return;
		const FReferenceSkeleton& Ref = Comp->GetSkinnedAsset()->GetRefSkeleton();
		for (const FVaginaPullBone& Pull : GVaginaPullBones)
			if (Ref.FindBoneIndex(FName(Pull.Name)) != INDEX_NONE) bFound = true;
	});
	return bFound;
}

static void RefreshVaginaPose(UPoseableMeshComponent* Comp)
{
	Comp->MarkRefreshTransformDirty();
	Comp->FillComponentSpaceTransforms();
	Comp->RefreshBoneTransforms();
	Comp->InvalidateCachedBounds();
	Comp->UpdateBounds();
	Comp->MarkRenderDynamicDataDirty();
	Comp->MarkRenderStateDirty();
	Comp->MarkRenderTransformDirty();
	Comp->ClearMotionVector();
}

static bool SpreadVaginaPullBones(UPoseableMeshComponent* Comp, float Open01)
{
	if (!Comp || !Comp->GetSkinnedAsset() || Open01 <= 0.f) return false;
	const FReferenceSkeleton& Ref = Comp->GetSkinnedAsset()->GetRefSkeleton();
	if (Comp->BoneSpaceTransforms.Num() != Ref.GetNum() || Comp->GetNumComponentSpaceTransforms() != Ref.GetNum()) return false;
	const int32 Kokan = FindVaginaBone(Comp, GVaginaKokanBones, UE_ARRAY_COUNT(GVaginaKokanBones));
	if (Kokan == INDEX_NONE) return false;
	const FVector KokanLocation = Comp->GetComponentSpaceTransforms()[Kokan].GetLocation();
	if (!FMath::IsFinite(KokanLocation.X) || !FMath::IsFinite(KokanLocation.Y) || !FMath::IsFinite(KokanLocation.Z)) return false;
	TMap<int32, FTransform> Desired;
	for (const FVaginaPullBone& Pull : GVaginaPullBones)
	{
		const int32 Bone = Ref.FindBoneIndex(FName(Pull.Name));
		if (!Comp->BoneSpaceTransforms.IsValidIndex(Bone) || !Comp->GetComponentSpaceTransforms().IsValidIndex(Bone)) continue;
		const FTransform& Current = Comp->GetComponentSpaceTransforms()[Bone];
		if (Current.ContainsNaN()) continue;
		const FVector Away = Current.GetLocation() - KokanLocation;
		const float Length = Away.Size();
		if (Length < 0.05f) continue;
		FTransform Moved = Current;
		Moved.AddToTranslation(Away / Length * (Open01 * Pull.Weight * GVaginaPullCm));
		Desired.Add(Bone, Moved);
	}
	if (Desired.IsEmpty()) return false;
	for (const TPair<int32, FTransform>& Item : Desired)
	{
		const int32 Parent = Ref.GetParentIndex(Item.Key);
		FTransform ParentTransform = FTransform::Identity;
		if (Parent != INDEX_NONE)
		{
			if (const FTransform* MovedParent = Desired.Find(Parent)) ParentTransform = *MovedParent;
			else if (Comp->GetComponentSpaceTransforms().IsValidIndex(Parent)) ParentTransform = Comp->GetComponentSpaceTransforms()[Parent];
		}
		FTransform Local = Item.Value.GetRelativeTransform(ParentTransform);
		Local.NormalizeRotation();
		if (Local.ContainsNaN()) continue;
		Comp->BoneSpaceTransforms[Item.Key] = Local;
	}
	RefreshVaginaPose(Comp);
	return true;
}
}

void AKKPreviewActor::ResolveVaginaMorph()
{
	VaginaMorphPart = INDEX_NONE;
	VaginaMorphName = NAME_None;
	if (!Session.IsOpen()) return;
	int32 FallbackPart = INDEX_NONE;
	FName FallbackName = NAME_None;
	for (int32 Part = 0; Part < Session.PartCount(); ++Part)
	{
		FName Found = NAME_None;
		for (const FName& Name : GetPartMorphNames(Part))
			if (Name == FName(TEXT("vagina.open"))) { Found = Name; break; }
		if (Found.IsNone()) continue;
		OkMeshView View;
		FMemory::Memzero(View);
		if (Session.PartAt(Part, View) && View.role && FCStringAnsi::Stricmp(View.role, "body") == 0)
		{
			VaginaMorphPart = Part;
			VaginaMorphName = Found;
			return;
		}
		if (FallbackPart == INDEX_NONE) { FallbackPart = Part; FallbackName = Found; }
	}
	VaginaMorphPart = FallbackPart;
	VaginaMorphName = FallbackName;
}

void AKKPreviewActor::DriveVaginaOpening(const AKKPreviewActor& Male, float& RangeMin, float& RangeMax, bool& bRange)
{
	if (!bVaginaMorphResolved)
	{
		if (!Session.IsOpen()) return;
		ResolveVaginaMorph();
		bVaginaMorphResolved = true;
	}
	if (!bVaginaOpeningLogged && VaginaMorphPart == INDEX_NONE)
	{
		if (PoseHasVaginaPullBone(*this))
		{
			UE_LOG(LogTemp, Display, TEXT("OK vagina opening uses radial pull-bone spread; not BetterPenetration collision"));
			bVaginaOpeningLogged = true;
		}
		else if (BodySkel || SkelFollowers.Num() > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("OK vagina opening left unchanged: no vagina.open morph and no cf_J_Vagina pull bones"));
			bVaginaOpeningLogged = true;
		}
	}
	FVector Kokan = FVector::ZeroVector;
	FVector Tip = FVector::ZeroVector;
	if (!VaginaBoneWorld(*this, GVaginaKokanBones, UE_ARRAY_COUNT(GVaginaKokanBones), Kokan)
		|| !VaginaBoneWorld(Male, GVaginaTipBones, UE_ARRAY_COUNT(GVaginaTipBones), Tip)) return;
	const float Dist = FVector::Distance(Tip, Kokan);
	if (!FMath::IsFinite(Dist)) return;
	if (!bRange || !FMath::IsFinite(RangeMin) || !FMath::IsFinite(RangeMax))
	{
		RangeMin = RangeMax = Dist;
		bRange = true;
	}
	else
	{
		RangeMin = FMath::Min(RangeMin, Dist);
		RangeMax = FMath::Max(RangeMax, Dist);
	}
	const float Span = RangeMax - RangeMin;
	const float Open01 = Span >= GVaginaOpenSpanCm ? FMath::Clamp(1.f - (Dist - RangeMin) / Span, 0.f, 1.f) : 0.f;
	if (VaginaMorphPart != INDEX_NONE)
	{
		const bool bApplied = SetPartMorphWeight(VaginaMorphPart, VaginaMorphName, Open01 * 100.f);
		if (!bVaginaOpeningLogged)
		{
			if (bApplied)
			{
				UE_LOG(LogTemp, Display, TEXT("OK vagina opening uses source morph %s on part %d"), *VaginaMorphName.ToString(), VaginaMorphPart);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("OK vagina.open morph part %d is not a controllable renderer"), VaginaMorphPart);
			}
			bVaginaOpeningLogged = true;
		}
		return;
	}
	if (Open01 <= 0.f) return;
	EachVaginaPose(*this, [Open01](UPoseableMeshComponent* Comp) { SpreadVaginaPullBones(Comp, Open01); });
}

void AKKPreviewActor::SetGenitalVisible(bool bVisible)
{
	bGenitalVisible = bVisible;
	ApplyGenitalVisibility();
}

void AKKPreviewActor::ApplyGenitalVisibility()
{
	for (UPrimitiveComponent* Prim : GenitalPrimitives)
	{
		if (!Prim) continue;
		Prim->SetVisibility(bGenitalVisible, true);
		Prim->SetHiddenInGame(!bGenitalVisible);
	}
}

void AKKPreviewActor::CloseVaginaOpening()
{
	if (!bVaginaMorphResolved) return;
	if (VaginaMorphPart != INDEX_NONE)
	{
		SetPartMorphWeight(VaginaMorphPart, VaginaMorphName, 0.f);
		return;
	}
	if (!Session.IsOpen()) return;
	if (BodySkel && BodySkel->GetSkinnedAsset()) OKApplySkelPose(BodySkel, Session);
	for (UPoseableMeshComponent* Follower : SkelFollowers)
		if (Follower && Follower->GetSkinnedAsset()) OKApplySkelPose(Follower, Session);
}

void AKKPreviewActor::EndFaceDisplayUpdate()
{
    check(FaceDisplayBatchDepth > 0);
    if (--FaceDisplayBatchDepth == 0 && bFaceDisplayPending) { bFaceDisplayPending = false; ApplyMouth(); }
}

void AKKPreviewActor::ApplyMouth()
{
    if (FaceDisplayBatchDepth > 0) { bFaceDisplayPending = true; return; }
    for (const FOkMouthSlot& Slot : MouthSlots)
    {
        TMap<FName, float> Desired;
        OKFbsWriteTransition(Desired, Slot.BrowPatterns, FaceTransitions[2], EffectiveFaceRate(2));
        OKFbsWriteTransition(Desired, Slot.EyePatterns, FaceTransitions[1], EffectiveFaceRate(1));
        OKFbsWriteTransition(Desired, Slot.Patterns, FaceTransitions[0], EffectiveFaceRate(0));
        if(const auto* Overrides=PartMorphWeights.Find(Slot.SessionPartIndex))
            for(const auto& Weight:*Overrides) Desired.Add(Weight.Key,Weight.Value);
        if (Slot.CpuComp && Parts.IsValidIndex(Slot.PartArrayIndex))
        {
            const FKKLoadedPart& Part = Parts[Slot.PartArrayIndex];
            TArray<FVector> Deformed = Part.BaseVertices;
            // Source morph offsets live before skinning. Transform their
            // weighted sum as vectors before adding to the posed surface.
            TArray<float> ReferenceDeltas;
            ReferenceDeltas.SetNumZeroed(Deformed.Num() * 3);
            OkSkinPaletteView RawSkin{};TArray<float> NativePalette;
            const bool HaveNativePalette=Session.SkinPalette(Slot.SessionPartIndex,RawSkin,NativePalette)>0&&RawSkin.n_verts==Deformed.Num();
            bool bHaveDeltas = false;
            for (int32 Morph = 0; Morph < Session.MorphCount(Slot.SessionPartIndex); ++Morph)
            {
                // Morph identity is immutable for this loaded slot. Check the
                // effective native-baseline delta before copying potentially
                // large raw index/delta arrays for every inactive expression.
                if(CVarOkFacePruneInactiveMorphs.GetValueOnGameThread()!=0&&Slot.MorphNames.IsValidIndex(Morph))
                {
                    const FName Name=Slot.MorphNames[Morph];const float* Wanted=Desired.Find(Name);
                    if(!Wanted||FMath::IsNearlyZero((*Wanted-Slot.BaselineWeights.FindRef(Name))/100.f))continue;
                }
                OkMorphView ViewMorph = {};
                FString RawName;TArray<uint32> RawIndices;TArray<float> RawDeltas;
                if(HaveNativePalette){
                    if(!Session.RawMorph(Slot.SessionPartIndex,Morph,RawName,RawIndices,RawDeltas))continue;
                    ViewMorph.indices=RawIndices.GetData();ViewMorph.deltas=RawDeltas.GetData();ViewMorph.n=RawIndices.Num();
                }else if (!Session.MorphAt(Slot.SessionPartIndex, Morph, ViewMorph) || !ViewMorph.name) continue;
                const FName Name(HaveNativePalette?RawName:FString(UTF8_TO_TCHAR(ViewMorph.name)));
                const float* Wanted = Desired.Find(Name);
                if (!Wanted) continue;
                const float Weight = (*Wanted - Slot.BaselineWeights.FindRef(Name)) / 100.f;
                if (FMath::IsNearlyZero(Weight)) continue;
                for (int32 Delta = 0; Delta < ViewMorph.n; ++Delta)
                {
                    const uint32 Vertex = ViewMorph.indices[Delta];
                    if (Deformed.IsValidIndex(static_cast<int32>(Vertex)))
                    {
                        const float* V = ViewMorph.deltas + Delta * 3;
                        for (int32 Axis = 0; Axis < 3; ++Axis) ReferenceDeltas[Vertex * 3 + Axis] += V[Axis] * Weight;
                        bHaveDeltas = true;
                        // Fallback for source meshes without a skin palette.
                        Deformed[Vertex] += OkToUe(V[0], V[1], V[2]) * Weight;
                    }
                }
            }
            if (bHaveDeltas)
            {
                if(HaveNativePalette){
                    // CPU views retain private prefab indices; the shared UE
                    // reference skeleton is not their deformation palette.
                    // Transform canonical raw deltas exactly once as vectors.
                    TArray<FMatrix> PreparedPalette;const bool Prepare=CVarOkFacePreparedPalette.GetValueOnGameThread()!=0&&NativePalette.Num()%16==0;
                    if(Prepare){PreparedPalette.SetNumUninitialized(NativePalette.Num()/16);for(int32 Bone=0;Bone<PreparedPalette.Num();++Bone)PreparedPalette[Bone]=OKSkinPaletteToUnreal(NativePalette.GetData()+Bone*16);}
                    for(int32 Vertex=0;Vertex<Deformed.Num();++Vertex){
                        FMatrix Matrix;if(!(Prepare?OKExactPreparedPaletteMatrix(RawSkin,PreparedPalette,-Vertex-1,Matrix):OKExactPaletteMatrix(RawSkin,NativePalette,-Vertex-1,Matrix)))continue;
                        const float* D=ReferenceDeltas.GetData()+Vertex*3;
                        Deformed[Vertex]=Part.BaseVertices[Vertex]+FVector(Matrix.TransformVector(OkToUe(D[0],D[1],D[2])));
                    }
                }
            }
            FillSection(Slot.CpuComp, Deformed, Part.Triangles, Part.UVs,
                Part.Colors, Part.BaseNormals, Part.UV4, Part.UV2,TArray<FProcMeshTangent>(),Part.UV3);
            const int32 Updated = OKUpdateSecondarySections(Slot.CpuComp, Deformed,
                Part.RuntimeSectionOffsets, Part.RuntimeSectionNormals);
            if (Updated == INDEX_NONE)
                UE_LOG(LogTemp, Error, TEXT("OK mouth secondary section topology mismatch part=%d"), Slot.SessionPartIndex);
            if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MOUTH_DISPLAY_AUDIT")) == TEXT("1"))
                UE_LOG(LogTemp, Display, TEXT("OK mouth display part=%d pattern=%d open_percent=%d secondary_sections=%d overlay=%d"),
                    Slot.SessionPartIndex, MouthPattern, OKFbsOpenPercent(EffectiveFaceRate(0)), Updated, Part.Overtex != nullptr);
        }
        if (Slot.Comp)
            for (const auto& Entry : Desired)
                Slot.Comp->SetMorphTarget(Entry.Key, (Entry.Value - Slot.BaselineWeights.FindRef(Entry.Key)) / 100.f);
        if (Slot.OutlineComp)
            for (const auto& Entry : Desired)
                Slot.OutlineComp->SetMorphTarget(Entry.Key, (Entry.Value - Slot.BaselineWeights.FindRef(Entry.Key)) / 100.f);
        // Leader sharing skips follower pose evaluation, so publish each
        // renderer's own morph overrides explicitly in this same frame.
        for(USkeletalMeshComponent* Mesh:{Slot.Comp,Slot.OutlineComp})if(Mesh&&(bCoordinatedPlayback||Mesh->LeaderPoseComponent.IsValid())){
            if(auto* Owned=Cast<UKKRuntimeSkeletalMeshComponent>(Mesh))Owned->PublishOwnedMorphTargets();
        }
        if (Slot.bHideWhenClosed)
        {
			const bool bShow = EffectiveFaceRate(0) > 0.001f;
            if (Slot.CpuComp) Slot.CpuComp->SetVisibility(bShow, true);
            if (Slot.Comp) { Slot.Comp->SetVisibility(bShow, true); Slot.Comp->SetHiddenInGame(!bShow); }
        }
    }
}

static FAutoConsoleCommandWithWorldAndArgs GOKPartMorph(
    TEXT("ok.SetPartMorph"),TEXT("Set source renderer morph: part-index morph-name weight-percent [actor-name]"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World) {
        if(!World || Args.Num()<3) return;
        int32 PartIndex;float Percent;
        if(!LexTryParseString(PartIndex,*Args[0]) || !LexTryParseString(Percent,*Args[2])) return;
        for(TActorIterator<AKKPreviewActor> It(World);It;++It) {
            if(Args.Num()>3 && It->GetName()!=Args[3]) continue;
            const bool Applied=It->SetPartMorphWeight(PartIndex,FName(*Args[1]),Percent);
            UE_LOG(LogTemp,Display,TEXT("ok.SetPartMorph actor=%s applied=%d"),*It->GetName(),Applied);return;
        }
    }));

void AKKPreviewActor::ApplyPoseFrame(float DeltaSeconds,bool bAdvanceAnimation)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(KK_PreviewPose);
    const bool ShapeUpdate=bShapeDirty;const double ShapeBegin=FPlatformTime::Seconds();
    ON_SCOPE_EXIT {if(ShapeUpdate){const double End=FPlatformTime::Seconds();const double Ms=(End-ShapeBegin)*1000.;ShapeUpdateTotalMs+=Ms;ShapeUpdateMaxMs=FMath::Max(ShapeUpdateMaxMs,Ms);ShapeLatencyMaxMs=FMath::Max(ShapeLatencyMaxMs,(End-ShapeRequestedAt)*1000.);++ShapeUpdateSamples;}};
	if (!Session.IsOpen())
	{
		return;
	}
	if (bShapeDirty)
	{
#if PLATFORM_WINDOWS
		::SetEnvironmentVariableW(TEXT("OK_UE_ANIM"), PoseMode == 2 ? nullptr : TEXT("1"));
#endif
		if(!Session.ApplyShapeLive(PendingBody, PendingFace,PoseMode!=0)){UE_LOG(LogTemp,Error,TEXT("OK live shape input rejected"));return;}
		bShapeDirty = false;
	}
    if (PoseMode == 1)
    {
        if(!BodySkel||!BodySkel->GetSkinnedAsset()||!WalkSeq||AnimDuration<=0.f) return;
        if(bAdvanceAnimation)AnimPhase=bGameAnimLooping?FMath::Fmod(AnimPhase+DeltaSeconds*AnimationPlaybackSpeed,AnimDuration):FMath::Min(AnimPhase+DeltaSeconds*AnimationPlaybackSpeed,AnimDuration);
        if(Session.ApplyUnrealFrame(BodySkel->GetSkinnedAsset()->GetRefSkeleton(),WalkSeq,AnimPhase,DeltaSeconds)) RefreshSkinnedDisplay();
        else UE_LOG(LogTemp,Error,TEXT("OK external Animator frame failed"));
    }
	else if (PoseMode == 2)
	{
		// Wrapping/clamping the shared elapsed clock at the base duration restarts
		// independent layers, and prevents an epoch after that duration ever firing.
		// Rust applies the source loop/clamp to each state's own normalized clock.
		if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.f) return;
		if(bAdvanceAnimation)AnimPhase = float(FMath::Min(double(AnimPhase) + double(DeltaSeconds)*double(AnimationPlaybackSpeed), double(MAX_flt)));
		const bool Applied=[&](){TRACE_CPUPROFILER_EVENT_SCOPE(KK_NativeAnimation);return Session.ApplyAnimFrame(AnimPhase,DeltaSeconds);}();
		if (Applied)
		{
			AnimDuration=Session.AnimDuration();
			RefreshSkinnedDisplay();
		}
	}
	else if (PoseMode == 3)
	{
		// Loop/clamp is the spec's own (per animation); the clock only advances.
		if (!OkPlayer || !OkPlayer->IsValid() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.f) return;
		if(bAdvanceAnimation)AnimPhase = float(FMath::Min(double(AnimPhase) + double(DeltaSeconds)*double(AnimationPlaybackSpeed), double(MAX_flt)));
		const bool Applied=[&](){TRACE_CPUPROFILER_EVENT_SCOPE(KK_OkAnimation);return OkPlayer->ApplySessions({&Session},AnimPhase,DeltaSeconds,&OkReport);}();
		if (Applied) { RefreshSkinnedDisplay(); OkPlayer->ApplyFaceCurves(0, AnimPhase, *this); }
		else UE_LOG(LogTemp,Error,TEXT("OK animation frame failed: %s"),*OkReport);
	}
	else if (PoseMode == 4)
	{
		// The external driver writes the pose and refreshes the display; a static frame here would overwrite it.
	}
	else if (bShapeDirty == false)
	{
        const bool ReactionWasPlaying=Session.IsBodyReactionPlaying();
		if(bHeadLookFollowCamera||bHeadLookPoseDirty||bBodyReactionPoseDirty||ReactionWasPlaying||bUserPoseDirty)Session.StaticInteractionFrame(DeltaSeconds);
		bUserPoseDirty=false;
        bBodyReactionPoseDirty=ReactionWasPlaying&&!Session.IsBodyReactionPlaying();
		bHeadLookPoseDirty=false;
		RefreshSkinnedDisplay();
	}
	bHeadLookPoseDirty=false;
}

bool AKKPreviewActor::SetGameAnimationTrigger(const FString& Name)
{
    if(PoseMode!=2||!Session.SetAnimationTrigger(Name)) return false;
    if(!Session.ApplyAnimFrame(AnimPhase,0.f)) return false;
    RefreshSkinnedDisplay();return true;
}
bool AKKPreviewActor::CrossFadeGameAnimation(const FString& Name,float Duration,float Offset)
{
    if(PoseMode!=2||!Session.CrossFadeAnimation(Name,Duration,Offset)) return false;
    AnimDuration=Session.AnimDuration();
    if(!Session.ApplyAnimFrame(AnimPhase,0.f)) return false;
    RefreshSkinnedDisplay();return true;
}

bool AKKPreviewActor::AuditAnimatorCrossFadeDisplay(const FString& Source,const FString& Destination)
{
    if(!BodySkel||SkelFollowers.IsEmpty())return false;
    const float FaceTime=FaceClock;bool Good=SetGameAnimation(Source)&&SetGameAnimationFloat(TEXT("MotionSpeed"),1.f)&&SetGameAnimationFloat(TEXT("Speed"),.37f);
    Tick(.61f);const float Held=AnimPhase,HeldFace=FaceClock,SourceLength=Session.AnimDuration();
    Good &= CrossFadeGameAnimation(Destination,.4f,.37f)&&AnimPhase==Held&&FaceClock==HeldFace&&FaceClock>=FaceTime&&IsGameAnimationTransitioning();
    Good &= !CrossFadeGameAnimation(Source,.4f,0.f)&&!SetGameAnimationFloat(TEXT("MotionSpeed"),1.6f);
    int32 Samples=0;double PaletteError=0.;
    for(float Delta:{.1f*SourceLength,.2f*SourceLength,.3f*SourceLength}) {
        const float Expected=AnimPhase+Delta;Tick(Delta);Good &= FMath::IsNearlyEqual(Expected,AnimPhase,1.e-6f);
        for(UPoseableMeshComponent* Follower:SkelFollowers) for(int32 I=0;I<BodySkel->BoneSpaceTransforms.Num();++I) {
            Good &= Follower&&Follower->BoneSpaceTransforms.IsValidIndex(I);
            if(Follower&&Follower->BoneSpaceTransforms.IsValidIndex(I)) {
                const auto& A=BodySkel->BoneSpaceTransforms[I];const auto& B=Follower->BoneSpaceTransforms[I];
                Good &= !A.ContainsNaN()&&!B.ContainsNaN()&&A.Equals(B,1.e-6);
                PaletteError=FMath::Max(PaletteError,FVector::Distance(A.GetTranslation(),B.GetTranslation()));
            }
        }
        Good &= IsGameAnimationTransitioning()==(Samples<2);++Samples;
    }
    Good &= !IsGameAnimationTransitioning();
    const float Elapsed=AnimPhase;Good &= SetGameAnimationFloat(TEXT("MotionSpeed"),1.6f)&&AnimPhase==Elapsed;
    UE_LOG(LogTemp,Display,TEXT("OK Animator CrossFade display audit passed=%d samples=%d actual_tick=1 elapsed_preserved=1 source_and_destination_playing=1 transition_complete=1 palette_error_cm=%.9f"),Good,Samples,PaletteError);
    return Good;
}

bool AKKPreviewActor::CrossFadeGameAnimationLayer(const FString& Name,int32 Layer,float Duration,float Offset)
{
	if(PoseMode!=2||!Session.CrossFadeAnimationLayer(Name,Layer,Duration,Offset))return false;
	AnimDuration=Session.AnimDuration();
	if(!Session.ApplyAnimFrame(AnimPhase,0.f))return false;
	RefreshSkinnedDisplay();return true;
}

bool AKKPreviewActor::AuditAnimatorLayerFadeDisplay(const FString& Source)
{
    if(!BodySkel||SkelFollowers.IsEmpty())return false;
    bool Good=SetGameAnimation(Source);
    const TArray<TPair<FString,float>> Controls={{TEXT("OK.LayerWeight.7"),.63f},{TEXT("OK.LayerWeight.8"),.37f},{TEXT("OK.LayerWeight.73"),.41f},{TEXT("OK.LayerStartTime.7"),0.f},{TEXT("OK.LayerStartPhase.7"),.23f},{TEXT("OK.LayerStartTime.8"),.17f},{TEXT("OK.LayerStartPhase.8"),.31f}};
    for(const auto& P:Controls)Good &= SetGameAnimationFloat(P.Key,P.Value);
    Tick(.61f);const float Held=AnimPhase,HeldFace=FaceClock,BaseLength=Session.AnimDuration();
    Good &= CrossFadeGameAnimationLayer(Source,7,.4f,.67f)&&AnimPhase==Held&&FaceClock==HeldFace&&IsGameAnimationTransitioning();
    Good &= !CrossFadeGameAnimationLayer(Source,7,.4f,.67f)&&!SetGameAnimationTrigger(TEXT("Restart"))&&!SetGameAnimationFloat(TEXT("height"),.5f);
    // Source layer length is a source parameter, not the base duration.
    const float LayerLength=Session.AnimationTransitionSourceLength();
    Good &= FMath::IsFinite(LayerLength)&&LayerLength>0.f;
    int32 Samples=0;double PaletteError=0.;
    for(float Delta:{.1f*LayerLength,.2f*LayerLength,.3f*LayerLength}) {
        const float Expected=AnimPhase+Delta;Tick(Delta);Good &= FMath::IsNearlyEqual(Expected,AnimPhase,1.e-6f)&&FMath::IsNearlyEqual(Session.AnimDuration(),BaseLength,1.e-4f);
        for(int32 I=0;I<Session.AnimationFloatCount();++I){FString Name;float FloatValue,Min,Max;Good &= Session.AnimationFloatAt(I,Name,FloatValue,Min,Max);for(const auto& P:Controls)if(Name==P.Key&&Name!=TEXT("OK.LayerStartTime.7")&&Name!=TEXT("OK.LayerStartPhase.7"))Good &= FloatValue==P.Value;}
        for(UPoseableMeshComponent* Follower:SkelFollowers)for(int32 I=0;I<BodySkel->BoneSpaceTransforms.Num();++I){Good &= Follower&&Follower->BoneSpaceTransforms.IsValidIndex(I);if(Follower&&Follower->BoneSpaceTransforms.IsValidIndex(I)){const auto& A=BodySkel->BoneSpaceTransforms[I];const auto& B=Follower->BoneSpaceTransforms[I];Good &= !A.ContainsNaN()&&!B.ContainsNaN()&&A.Equals(B,1.e-6);PaletteError=FMath::Max(PaletteError,FVector::Distance(A.GetTranslation(),B.GetTranslation()));}}
        Good &= IsGameAnimationTransitioning()==(Samples<2);++Samples;
    }
    Good &= !IsGameAnimationTransitioning();
    UE_LOG(LogTemp,Display,TEXT("OK Animator LayerFade display audit passed=%d samples=%d actual_tick=1 elapsed_preserved=1 other_clocks_weights_preserved=1 transition_complete=1 palette_error_cm=%.9f"),Good,Samples,PaletteError);
    return Good;
}

bool AKKPreviewActor::AuditAnimatorParallelFadeDisplay(const FString& Source)
{
    if(!BodySkel||SkelFollowers.IsEmpty())return false;
    bool Good=SetGameAnimation(Source);
    const TArray<TPair<FString,float>> Controls={{TEXT("height"),.37f},{TEXT("height1"),.61f},{TEXT("Breast1"),.37f},{TEXT("OK.LayerWeight.7"),.63f},{TEXT("OK.LayerWeight.8"),.37f},{TEXT("OK.LayerWeight.73"),.41f},{TEXT("OK.LayerStartTime.7"),0.f},{TEXT("OK.LayerStartPhase.7"),.23f},{TEXT("OK.LayerStartTime.8"),.17f},{TEXT("OK.LayerStartPhase.8"),.31f}};
    for(const auto& P:Controls)Good &= SetGameAnimationFloat(P.Key,P.Value);
    Tick(.61f);const float Held=AnimPhase,HeldFace=FaceClock,BaseLength=Session.AnimDuration();
    Good &= CrossFadeGameAnimationLayer(Source,7,.4f,.67f)&&AnimPhase==Held&&FaceClock==HeldFace&&IsGameAnimationLayerTransitioning(7);
    const float Length7=Session.AnimationLayerTransitionSourceLength(7);
    Good &= !CrossFadeGameAnimationLayer(Source,7,.4f,.67f)&&!SetGameAnimationTrigger(TEXT("Restart"))&&!SetGameAnimationFloat(TEXT("height"),.5f);
    Tick(.16f);const float HeldSecond=AnimPhase,HeldFaceSecond=FaceClock;
    Good &= CrossFadeGameAnimationLayer(Source,8,.35f,.43f)&&AnimPhase==HeldSecond&&FaceClock==HeldFaceSecond&&IsGameAnimationLayerTransitioning(7)&&IsGameAnimationLayerTransitioning(8);
    Good &= Session.AnimationLayerTransitionSourceLength(7)==Length7&&Session.AnimationLayerTransitionSourceLength(8)>0.f&&Session.AnimationTransitionSourceLength()==0.f;
    int32 Samples=0;double PaletteError=0.;
    for(float At:{1.04f,1.25f,1.92f}){
        Tick(At-AnimPhase);Good &= FMath::IsNearlyEqual(AnimPhase,At,1.e-6f)&&FMath::IsNearlyEqual(Session.AnimDuration(),BaseLength,1.e-4f);
        Good &= IsGameAnimationLayerTransitioning(7)==(Samples==0)&&IsGameAnimationLayerTransitioning(8)==(Samples<2)&&IsGameAnimationTransitioning()==(Samples<2);
        if(Samples==1)Good &= Session.AnimationTransitionSourceLength()==Session.AnimationLayerTransitionSourceLength(8)&&Session.AnimationLayerTransitionSourceLength(7)==0.f;
        for(int32 I=0;I<Session.AnimationFloatCount();++I){FString Name;float FloatValue,Min,Max;Good &= Session.AnimationFloatAt(I,Name,FloatValue,Min,Max);for(const auto& P:Controls)if(Name==P.Key){float Expected=P.Value;if(Name==TEXT("OK.LayerStartTime.7"))Expected=.61f;else if(Name==TEXT("OK.LayerStartPhase.7"))Expected=.67f;else if(Name==TEXT("OK.LayerStartTime.8"))Expected=.77f;else if(Name==TEXT("OK.LayerStartPhase.8"))Expected=.43f;Good &= FMath::IsNearlyEqual(FloatValue,Expected,1.e-6f);}}
        for(UPoseableMeshComponent* Follower:SkelFollowers)for(int32 I=0;I<BodySkel->BoneSpaceTransforms.Num();++I){Good &= Follower&&Follower->BoneSpaceTransforms.IsValidIndex(I);if(Follower&&Follower->BoneSpaceTransforms.IsValidIndex(I)){const auto& A=BodySkel->BoneSpaceTransforms[I];const auto& B=Follower->BoneSpaceTransforms[I];Good &= !A.ContainsNaN()&&!B.ContainsNaN()&&A.Equals(B,1.e-6);PaletteError=FMath::Max(PaletteError,FVector::Distance(A.GetTranslation(),B.GetTranslation()));}}++Samples;
    }
    Good &= !IsGameAnimationTransitioning();
    UE_LOG(LogTemp,Display,TEXT("OK Animator ParallelFade display audit passed=%d samples=%d actual_tick=1 elapsed_preserved=1 clocks_weights_preserved=1 independent_completion=1 palette_error_cm=%.9f"),Good,Samples,PaletteError);
    return Good;
}

bool AKKPreviewActor::AuditAnimatorElapsedClockDisplay()
{
	if (PoseMode != 2 || !Session.IsOpen() || SkelFollowers.IsEmpty() || AnimDuration <= 0.f) return false;
	TArray<TPair<FString,float>> Parameters;
	for (int32 I=0; I<Session.AnimationFloatCount(); ++I) {
		FString Name;float Value,Min,Max;
		if (!Session.AnimationFloatAt(I,Name,Value,Min,Max)) return false;
		Parameters.Emplace(Name,Value);
	}
	const float OldElapsed=AnimPhase,Duration=AnimDuration;
	const bool OldLoop=bGameAnimLooping;
	bool Good=SetGameAnimationFloat(TEXT("OK.LayerWeight.7"),0.f)
		&& SetGameAnimationFloat(TEXT("OK.LayerWeight.8"),1.f)
		&& SetGameAnimationFloat(TEXT("OK.LayerWeight.73"),0.f)
		&& SetGameAnimationFloat(TEXT("OK.LayerStartTime.8"),Duration+.11f)
		&& SetGameAnimationFloat(TEXT("OK.LayerStartPhase.8"),.37f);
	int32 Samples=0;
	for (bool Loop : {true,false}) {
		bGameAnimLooping=Loop;AnimPhase=Duration-.02f;
		for (float Delta : {.04f,.17f,2.f*Duration+.03f,.01f}) {
			const float Expected=AnimPhase+Delta;
			Tick(Delta); // Exercise the actor's production playback path.
			Good &= FMath::IsNearlyEqual(AnimPhase,Expected,1.e-6f);
			for (int32 I=0; I<Session.SkelBoneCount(); ++I) {
				OkBoneView Bone{};Good &= Session.SkelBoneAt(I,Bone);
				for (float V : Bone.pose_pos) Good &= FMath::IsFinite(V);
				for (float V : Bone.pose_rot) Good &= FMath::IsFinite(V);
				for (float V : Bone.pose_scl) Good &= FMath::IsFinite(V);
			}
			++Samples;
		}
		const float Held=AnimPhase;
		ApplyPoseFrame(-.01f);
		Good &= AnimPhase==Held;
	}
	for (const auto& Parameter : Parameters) Good &= Session.SetAnimationFloat(Parameter.Key,Parameter.Value);
	AnimPhase=OldElapsed;AnimDuration=Duration;bGameAnimLooping=OldLoop;
	Good &= Session.ApplyAnimFrame(AnimPhase,0.f);RefreshSkinnedDisplay();
	UE_LOG(LogTemp,Display,TEXT("OK Animator elapsed display audit passed=%d samples=%d base_cycles=3 delayed_epoch_after_base=1 loop_and_end_hold=1 actual_tick=1 restored_parameters=1"),Good,Samples);
	return Good;
}

bool AKKPreviewActor::AuditAnimatorTriggerDisplay()
{
	if(PoseMode!=2||SkelFollowers.IsEmpty()||!GetGameAnimationTriggers().Contains(TEXT("Restart"))) return false;
	TArray<TPair<FString,float>> Parameters;
	for(int32 I=0;I<Session.AnimationFloatCount();++I) {
		FString Name;float Value,Min,Max;if(!Session.AnimationFloatAt(I,Name,Value,Min,Max)) return false;
		Parameters.Emplace(Name,Value);
	}
	const float OldElapsed=AnimPhase,Duration=AnimDuration;bool Good=true;int32 Changed=0;
	for(int32 Event=0;Event<3;++Event) {
		const float Expected=AnimPhase+.61f;Tick(.61f);Good &= FMath::IsNearlyEqual(AnimPhase,Expected,1.e-6f);
		TArray<FQuat> Before;
		for(int32 I=0;I<Session.SkelBoneCount();++I) {OkBoneView B{};Good &= Session.SkelBoneAt(I,B);Before.Emplace(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]);}
		const float Held=AnimPhase,FaceTime=FaceClock;
		Good &= SetGameAnimationTrigger(TEXT("Restart"));Good &= AnimPhase==Held&&FaceClock==FaceTime&&AnimDuration==Duration;
		Good &= !SetGameAnimationTrigger(TEXT("not_a_trigger"))&&!SetGameAnimationTrigger(TEXT("height"));
		for(int32 I=0;I<Session.SkelBoneCount();++I) {OkBoneView B{};Good &= Session.SkelBoneAt(I,B);Changed+=!Before[I].Equals(FQuat(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]),1.e-4);}
		for(int32 I=0;I<Session.AnimationFloatCount();++I) {
			FString Name;float Value,Min,Max;Good &= Session.AnimationFloatAt(I,Name,Value,Min,Max);
			if(Name.StartsWith(TEXT("OK.LayerStartTime."))) Good &= Value==Held;
			else if(Name.StartsWith(TEXT("OK.LayerStartPhase."))) Good &= Value==0.f;
			else {const auto* Saved=Parameters.FindByPredicate([&](const auto& P){return P.Key==Name;});Good &= Saved&&Saved->Value==Value;}
		}
	}
	for(const auto& P:Parameters) Good &= Session.SetAnimationFloat(P.Key,P.Value);
	AnimPhase=OldElapsed;Good &= Session.ApplyAnimFrame(AnimPhase,0.f);RefreshSkinnedDisplay();Good &= Changed>0;
	UE_LOG(LogTemp,Display,TEXT("OK Animator trigger display audit passed=%d events=3 samples=3 actual_tick=1 clock_unchanged=1 weights_unchanged=1 changed_bones=%d restored_parameters=1"),Good,Changed);
	return Good;
}

bool AKKPreviewActor::AuditBodyDynamicsDisplay(const FString& TracePath, bool bExternal)
{
	if (!BodySkel || SkelFollowers.IsEmpty()) return false;
	if (bExternal) {
#if WITH_DEV_AUTOMATION_TESTS
#if WITH_EDITOR
		if(!SetUnrealAnimation(OKMakeExternalBodyFixture(this))) return false;
#else
        return false;
#endif
#else
		return false;
#endif
	} else SetPoseMode(1);
	if (PoseMode != (bExternal?1:2)) return false;
	TArray<int32> Joints;
	TArray<FTransform> Initial;
	for (const TCHAR* Side : {TEXT("L"),TEXT("R")})
		for (int32 Joint = 1; Joint <= 3; ++Joint)
		{
			const int32 Index = BodySkel->GetBoneIndex(FName(*FString::Printf(TEXT("cf_j_bust%02d_%s"),Joint,Side)));
			if (!BodySkel->BoneSpaceTransforms.IsValidIndex(Index)) return false;
			Joints.Add(Index);Initial.Add(BodySkel->BoneSpaceTransforms[Index]);
		}
	bool Good = true;int32 Samples = 0;double MaxMotionCm = 0.,MaxPaletteErrorCm = 0.;
	FString Trace = TEXT("frame\tphase\tbone\tx_cm\ty_cm\tz_cm\tqx\tqy\tqz\tqw\n");
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		ApplyPoseFrame(1.f/60.f);
		for (int32 Joint = 0; Joint < Joints.Num(); ++Joint)
		{
			const int32 Index = Joints[Joint];const FTransform& Pose = BodySkel->BoneSpaceTransforms[Index];
			Good &= !Pose.ContainsNaN();
			MaxMotionCm = FMath::Max(MaxMotionCm,FVector::Distance(Pose.GetTranslation(),Initial[Joint].GetTranslation()));
			for (UPoseableMeshComponent* Follower : SkelFollowers)
			{
				if (!Follower || !Follower->BoneSpaceTransforms.IsValidIndex(Index)) {Good = false;continue;}
				const FTransform& Displayed = Follower->BoneSpaceTransforms[Index];
				Good &= Displayed.Equals(Pose,1.e-6) && !Displayed.ContainsNaN();
				MaxPaletteErrorCm = FMath::Max(MaxPaletteErrorCm,FVector::Distance(Displayed.GetTranslation(),Pose.GetTranslation()));
				++Samples;
			}
			const FVector P = Pose.GetTranslation();const FQuat Q = Pose.GetRotation();
			Trace += FString::Printf(TEXT("%d\t%.9g\t%s\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\n"),
				Frame,AnimPhase,*BodySkel->GetBoneName(Index).ToString(),P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W);
		}
	}
	Good &= MaxMotionCm > 1.e-4;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(TracePath),true);
	Good &= FFileHelper::SaveStringToFile(Trace,*TracePath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogTemp,Display,TEXT("OK %s dynamics display audit passed=%d frames=180 joints=%d followers=%d samples=%d motion_cm=%.9f palette_error_cm=%.9f"),
		bExternal?TEXT("external"):TEXT("body"),Good,Joints.Num(),SkelFollowers.Num(),Samples,MaxMotionCm,MaxPaletteErrorCm);
	return Good;
}

bool AKKPreviewActor::AuditStudioPVDisplay()
{
    if(!BodySkel||SkelFollowers.IsEmpty()) return false;
    bool Good=true;int32 Samples=0;double MaxError=0.;
    for(int32 Slot=0;Slot<4;++Slot) for(bool Source:{false,true}) {
        const int32 Index=Session.StudioPVBone(Slot,Source);OkBoneView Bone{};
        if(!BodySkel->BoneSpaceTransforms.IsValidIndex(Index)||!Session.SkelBoneAt(Index,Bone)) return false;
        const FTransform& Pose=BodySkel->BoneSpaceTransforms[Index];
        const FVector Expected(-100.*Bone.pose_pos[0],100.*Bone.pose_pos[2],100.*Bone.pose_pos[1]);
        Good&=!Pose.ContainsNaN()&&Pose.GetTranslation().Equals(Expected,1.e-4);
        MaxError=FMath::Max(MaxError,FVector::Distance(Expected,Pose.GetTranslation()));
        for(UPoseableMeshComponent* Follower:SkelFollowers) {
            if(!Follower||!Follower->BoneSpaceTransforms.IsValidIndex(Index)) {Good=false;continue;}
            const FTransform& Display=Follower->BoneSpaceTransforms[Index];Good&=Display.Equals(Pose,1.e-6)&&!Display.ContainsNaN();
            MaxError=FMath::Max(MaxError,FVector::Distance(Pose.GetTranslation(),Display.GetTranslation()));++Samples;
        }
    }
    UE_LOG(LogTemp,Display,TEXT("OK Studio PV display audit passed=%d bindings=4 followers=%d samples=%d palette_error_cm=%.9f flags=%d%d%d%d"),
        Good,SkelFollowers.Num(),Samples,MaxError,Session.StudioPVFlag(0),Session.StudioPVFlag(1),Session.StudioPVFlag(2),Session.StudioPVFlag(3));
    return Good;
}

void AKKPreviewActor::ResetOrbit(const FVector& Focus, const FVector& CamLoc)
{
	OrbitFocus = Focus;
	const FVector Delta = CamLoc - Focus;
	OrbitDistance = Delta.Size();
	OrbitYaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.X, Delta.Y));
	OrbitPitch = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Delta.Z / FMath::Max(OrbitDistance, SMALL_NUMBER), -1.f, 1.f)));
}

static void OkUpdateStudioBackdrop(UWorld* World,FBox Bounds,const TArray<FVector>& Cameras);

void AKKPreviewActor::ApplyOrbitCamera()
{
	if (!StudioCam)
	{
		return;
	}
	const float YawRad = FMath::DegreesToRadians(OrbitYaw);
	const float PitchRad = FMath::DegreesToRadians(OrbitPitch);
	const float CP = FMath::Cos(PitchRad);
	const FVector Offset(
		FMath::Sin(YawRad) * CP * OrbitDistance,
		FMath::Cos(YawRad) * CP * OrbitDistance,
		FMath::Sin(PitchRad) * OrbitDistance);
	const FVector CamLoc = OrbitFocus + Offset;
	// Keep orientation continuous at distance zero and across the poles.
	StudioCam->SetActorLocationAndRotation(CamLoc, FRotator(-OrbitPitch, -90.f-OrbitYaw, 0.f));
	OkUpdateStudioBackdrop(GetWorld(),GetComponentsBoundingBox(true),{CamLoc});
	// MCP view captures turn actor tick off; keep the light with the camera.
	UpdateCameraKeyLight();
}

void AKKPreviewActor::UpdateCameraKeyLight()
{
	if (!bCameraKeyLight || bStudioItemShading || !StudioCam)
	{
		return;
	}
	// The maker's directional light is a child of the camera. Keep the old
	// fixed front light's 40 degree elevation (Unity (0,.643,.766)) relative
	// to the camera, so the default framing looks unchanged and orbiting keeps
	// the character's camera-facing side lit.
	const FVector Forward = StudioCam->GetActorForwardVector();
	const FVector Up = StudioCam->GetActorUpVector();
	const FVector Towards = (-Forward * 0.7660446f + Up * 0.6427876f).GetSafeNormal();
	if (Towards.IsNearlyZero())
	{
		return;
	}
	// Unreal (x, y, z) is Unity (-x, z, y).
	SetSharedVectorParameter(TEXT("_WorldSpaceLightPos0"), FLinearColor(-Towards.X, Towards.Z, Towards.Y, 0.f));
	if (ADirectionalLight* Key = StudioKeyLight.Get())
	{
		Key->SetActorRotation((-Towards).Rotation());
	}
}

void AKKPreviewActor::OrbitMouse(uint8 Buttons,float DeltaX,float DeltaY)
{
 if(GOkShotCaptureActive||!StudioCam||!FMath::IsFinite(DeltaX)||!FMath::IsFinite(DeltaY)||(DeltaX==0.f&&DeltaY==0.f))return;
 // InputManager mouse sensitivity .1, base rotation speed 5, move speed .05m.
 const float X=DeltaX*.1f,Y=-DeltaY*.1f,Move=5.f;
 if((Buttons&3)==3)OrbitFocus+=StudioCam->GetActorRightVector()*(X*Move)+StudioCam->GetActorForwardVector()*(Y*Move);
 else if(Buttons&1){OrbitYaw=FMath::Fmod(OrbitYaw-X*5.f,360.f);OrbitPitch=FMath::Fmod(OrbitPitch-Y*5.f,360.f);}
 else if(Buttons&2){OrbitFocus.Z+=Y*Move;OrbitDistance=FMath::Max(0.f,OrbitDistance+X*Move);}
 else if(Buttons&4)OrbitFocus+=StudioCam->GetActorRightVector()*(X*Move)+StudioCam->GetActorUpVector()*(Y*Move);
 else return;
 ApplyOrbitCamera();
}

void AKKPreviewActor::OrbitDrag(float DeltaX,float DeltaY){OrbitMouse(1,DeltaX,DeltaY);}

void AKKPreviewActor::OrbitZoom(float WheelNotches)
{
 if(GOkShotCaptureActive||!StudioCam||!FMath::IsFinite(WheelNotches)||WheelNotches==0.f)return;
 // Native wheel axis .1 * zoomSpeed 5m; distance=-CamDat.Dir.z.
 OrbitDistance=FMath::Max(0.f,OrbitDistance-WheelNotches*50.f);ApplyOrbitCamera();
}

void AKKPreviewActor::TickActor(float DeltaSeconds,ELevelTick TickType,FActorTickFunction& ThisTickFunction)
{
    const bool PreviousWorldTick=bWorldPoseTick;
    const float PreviousDelta=WorldPoseDelta;
    bWorldPoseTick=true;
    UWorld* PoseWorld=GetWorld();
    WorldPoseDelta=AnimationReadyClock.Delta(DeltaSeconds,GFrameCounter,PoseWorld&&PoseWorld->IsGameWorld()&&PoseWorld->HasBegunPlay());
    ON_SCOPE_EXIT { bWorldPoseTick=PreviousWorldTick;WorldPoseDelta=PreviousDelta; };
    Super::TickActor(DeltaSeconds,TickType,ThisTickFunction);
}

void AKKPreviewActor::Tick(float DeltaSeconds)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(KK_PreviewTick);
 const bool Profile=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ANIM_PROFILE"))==TEXT("1");const double Begin=FPlatformTime::Seconds();
	Super::Tick(DeltaSeconds);
	PublishPhysicsFrame();
    bool BatchFace=CVarOkFaceFrameBatch.GetValueOnGameThread()!=0;
    if(BatchFace)BeginFaceDisplayUpdate();
    ON_SCOPE_EXIT{if(BatchFace)EndFaceDisplayUpdate();};
	AdvanceSpeech();
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			PC->bAutoManageActiveCameraTarget = false;
			if (StudioCam && PC->GetViewTarget() != StudioCam)
			{
				PC->SetViewTarget(StudioCam);
			}
		}
	}
	// FrameCurrentPose and capture rigs move StudioCam without ApplyOrbitCamera.
	UpdateCameraKeyLight();
	if (!Session.IsOpen())
	{
		return;
	}
	UpdateHeadLookTarget();
	const bool bNeedPose = bShapeDirty || bHeadLookFollowCamera || bHeadLookPoseDirty || PoseMode == 1 || PoseMode == 2 || PoseMode == 3;
	if (bNeedPose)
	{
        ApplyPoseFrame(bWorldPoseTick?WorldPoseDelta:DeltaSeconds);
	}
	// FaceBlendShape.LateUpdate follows the character pose and uses frame time,
 const double PoseDone=FPlatformTime::Seconds();
	// independent of clip phase, looping, pausing and short animations.
	AdvanceEyeLook(DeltaSeconds);
	AdvanceFaceAnimation(DeltaSeconds);
    // No renderer consumes intermediate speech/pose/gaze/blink updates in
    // this tick. Publish their final authored state before shadow capture.
    if(BatchFace){EndFaceDisplayUpdate();BatchFace=false;}
 const double FaceDone=FPlatformTime::Seconds();
	if (bCharacterDisplayShadows && ToonShadowCapture && ToonShadowTargets.Num() == 2)
	{
        BindToonShadowTarget(ToonShadowReadIndex);
		ToonShadowCapture->TextureTarget = ToonShadowTargets[1 - ToonShadowReadIndex];
		ToonShadowCapture->CaptureScene();
		ToonShadowReadIndex = 1 - ToonShadowReadIndex;
	}
 const double TickMs=(FPlatformTime::Seconds()-Begin)*1000.;
 if(bNeedPose){AnimationTickTotalMs+=TickMs;++AnimationTickSamples;AnimationTickMaxMs=FMath::Max(AnimationTickMaxMs,TickMs);++AnimationTickHistogram[FMath::Clamp(int32(TickMs),0,255)];}
 if(Profile)UE_LOG(LogTemp,Display,TEXT("OK animation tick pose_ms=%.3f face_ms=%.3f shadow_ms=%.3f"),(PoseDone-Begin)*1000.,(FaceDone-PoseDone)*1000.,(FPlatformTime::Seconds()-FaceDone)*1000.);
}

void AKKPreviewActor::BindToonShadowTarget(int32 Index)
{
    if(ToonShadowBoundIndex==Index&&ToonShadowBoundMaterialCount==ToonShadowMaterials.Num())return;
    for(UMaterialInstanceDynamic* Mat:ToonShadowMaterials)
        if(Mat)Mat->SetTextureParameterValue(TEXT("KKShadowDepth"),ToonShadowTargets[Index]);
    ToonShadowBoundIndex=Index;ToonShadowBoundMaterialCount=ToonShadowMaterials.Num();
}

bool AKKPreviewActor::RefreshShotShadow(bool bDeferred)
{
    if(!bCharacterDisplayShadows){UE_LOG(LogTemp,Display,TEXT("OK character display shadow capture skipped (disabled)"));return true;}
	if (!ToonShadowCapture && ToonShadowTargets.IsEmpty()) return true;
	if (!ToonShadowCapture || ToonShadowTargets.Num() != 2) return false;
	const float HeldPhase = AnimPhase;
	const float HeldFaceClock = FaceClock;
    // Scene-depth capture writes its resolved target after rendering, before
    // the main view reads it. Its lighting does not consume KKShadowDepth.
    // Keep that resource bound to avoid invalidating every material uniform
    // buffer each frame. Immediate photos retain their original ping-pong.
    const int32 WriteIndex = bDeferred&&CVarOkStableShadowTarget.GetValueOnGameThread()
        ? ToonShadowReadIndex : 1 - ToonShadowReadIndex;
	ToonShadowCapture->TextureTarget = ToonShadowTargets[WriteIndex];
	// During playback, both captures run after the world's pending primitive
	// updates and before the main view. Immediate photo captures still flush.
	if (bDeferred) ToonShadowCapture->CaptureSceneDeferred();
	else ToonShadowCapture->CaptureScene();
    BindToonShadowTarget(WriteIndex);
	ToonShadowReadIndex = WriteIndex;
	const bool Held = AnimPhase == HeldPhase && FaceClock == HeldFaceClock;
	UE_LOG(LogTemp, Verbose, TEXT("ok.ShotPreview shadow readiness captured=1 pose_clock_held=%d materials=%d"), Held ? 1 : 0, ToonShadowMaterials.Num());
	return Held;
}

#if WITH_DEV_AUTOMATION_TESTS
bool AKKPreviewActor::VerifyDeferredShadowTarget(FString& Error)
{
    if(!GetWorld()||!GetWorld()->Scene||!ToonShadowCapture||ToonShadowTargets.Num()!=2){Error=TEXT("Shadow oracle has no capture");return false;}
    const float Phase=AnimPhase,Clock=FaceClock;
    const int32 Previous=CVarOkStableShadowTarget.GetValueOnGameThread();
    CVarOkStableShadowTarget.AsVariable()->Set(1,ECVF_SetByCode);
    ON_SCOPE_EXIT{CVarOkStableShadowTarget.AsVariable()->Set(Previous,ECVF_SetByCode);};
    if(!RefreshShotShadow(false)){Error=TEXT("Reference shadow changed clocks");return false;}
    const int32 ReferenceIndex=ToonShadowReadIndex;
    auto* Target=ToonShadowTargets[ReferenceIndex].Get();
    TArray<FLinearColor> Reference,Actual;
    if(!Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Reference)){Error=TEXT("Reference shadow read failed");return false;}
    if(!RefreshShotShadow(true)||ToonShadowReadIndex!=ReferenceIndex){Error=TEXT("Deferred shadow changed its bound target");return false;}
    GetWorld()->SendAllEndOfFrameUpdates();
    USceneCaptureComponent::UpdateDeferredCaptures(GetWorld()->Scene);
    if(!Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Actual)||Reference.Num()!=Actual.Num()){Error=TEXT("Deferred shadow read failed");return false;}
    int32 Covered=0;float Maximum=0.f;
    for(int32 I=0;I<Reference.Num();++I){
        const float A=Reference[I].R,B=Actual[I].R;
        if(!FMath::IsFinite(A)||!FMath::IsFinite(B)){Error=TEXT("Shadow depth is non-finite");return false;}
        Covered+=A>0.f&&A<100000.f;
        Maximum=FMath::Max(Maximum,FMath::Abs(A-B));
        if(FMath::Abs(A-B)>1.e-5f*FMath::Max(1.f,FMath::Abs(A))){Error=FString::Printf(TEXT("Deferred shadow differs at pixel %d: %.9g / %.9g"),I,A,B);return false;}
    }
    for(UMaterialInstanceDynamic* Material:ToonShadowMaterials)if(Material){
        UTexture* Bound=nullptr;
        if(!Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("KKShadowDepth")),Bound)||Bound!=Target){Error=TEXT("Shadow material binding differs");return false;}
    }
    if(!Covered||AnimPhase!=Phase||FaceClock!=Clock){Error=TEXT("Shadow oracle has no geometry or changed clocks");return false;}
    UE_LOG(LogTemp,Display,TEXT("OK stable shadow oracle pixels=%d covered=%d max_depth_error=%.9g clocks_held=1"),Actual.Num(),Covered,Maximum);
    return true;
}
#endif

void AKKPreviewActor::PlayAnimSequence(UAnimSequence* Seq, float Time)
{
	if(SetUnrealAnimation(Seq)&&FMath::IsFinite(Time)) {
		AnimPhase=FMath::Clamp(Time,0.f,AnimDuration);ApplyPoseFrame(0.f);
	}
}

void AKKPreviewActor::ApplyShapes(const TArray<float>& Face, const TArray<float>& Body)
{
	if (Session.IsOpen())
	{
		if (!Session.ApplyShape(Body, Face))
		{
			return;
		}
		// An explicit restore supersedes any slider request queued before it.
		PendingBody=Body;PendingFace=Face;bShapeDirty=false;
		RefreshSkinnedDisplay();
		if (PoseMode == 0 && (BodySkel||CpuRigidNeutralBones.IsEmpty()))
		{
			CaptureRigidFollowerNeutral();
		}
		// GPU followers are the display. Skin() rewrites every vertex on the
		// CPU; sliders must not take that path (GPUSkin.md §5).
		if (SkelFollowers.Num() > 0)
		{
			return;
		}
		Session.Skin();
		for (FKKLoadedPart& Part : Parts)
		{
			if (Part.SessionIndex == INDEX_NONE || !Part.Comp || Part.bSlotAttached || SkelPmcs.Contains(Part.Comp))
			{
				continue;
			}
			OkMeshView View;
			FMemory::Memzero(View);
			if (!Session.PartAt(Part.SessionIndex, View) || !View.positions)
			{
				continue;
			}
			Part.RuntimeSourceGeometry.Reset();
			const int32 N = FMath::Min(View.n_verts, Part.BaseVertices.Num());
			for (int32 v = 0; v < N; ++v)
			{
				const float* P = View.positions + v * 3;
				Part.BaseVertices[v] = OkToUe(P[0], P[1], P[2]);
			}
			if (View.normals && Part.BaseNormals.Num() == Part.BaseVertices.Num())
			{
				for (int32 v = 0; v < N; ++v)
				{
					const float* Nv = View.normals + v * 3;
					Part.BaseNormals[v] = OkToUeDir(Nv[0], Nv[1], Nv[2]);
				}
			}
			if (Part.bFaceSphereNormals)
			{
				ApplyFaceSphereNormals(Part.BaseVertices, Part.BaseNormals);
			}
			FillSection(Part.Comp, Part.BaseVertices, Part.Triangles, Part.UVs, Part.Colors, Part.BaseNormals, Part.UV4, Part.UV2,TArray<FProcMeshTangent>(),Part.UV3);
			if (Part.Overtex && !Part.bNativeFaceDraw && Part.UV4.Num() == Part.BaseVertices.Num())
			{
				FillOvertexSection(Part.Comp, Part.BaseVertices, Part.Triangles, Part.UV4);
				if (UMaterial* Om = MakeOvertexMaterial(Part.Overtex, Part.OverColor))
				{
					Part.Comp->SetMaterial(1, Om);
				}
			}
			if(!View.eye_draw&&Session.MorphCount(Part.SessionIndex)==0)
				OKCompactProceduralSections(Part.Comp,Part.BaseVertices.Num(),Part.RuntimeDrawVertices);
		}
		return;
	}
	// Bone morph is applied in ok-extract. Do not double-deform the OBJ.
	if (MappedSliders.Contains(TEXT("bone_morph ShapeBodyInfoFemale+ShapeHeadInfoFemale")))
	{
		return;
	}
	const float Height = Body.IsValidIndex(0) ? Body[0] : 0.5f;
	const float HeadB = Body.IsValidIndex(1) ? Body[1] : 0.5f;
	const float Bust = Body.IsValidIndex(4) ? Body[4] : 0.5f;
	const float Hip = Body.IsValidIndex(21) ? Body[21] : 0.5f;
	const float HeadF = Face.IsValidIndex(0) ? Face[0] : 0.5f;
	const float HeightScale = 0.7f + 0.6f * Height;
	const float HeadScale = 0.7f + 0.3f * HeadF + 0.3f * HeadB;
	const float BustScale = 0.75f + 0.5f * Bust;
	const float HipScale = 0.75f + 0.5f * Hip;

	for (FKKLoadedPart& Part : Parts)
	{
		if (!Part.Comp || Part.BaseVertices.Num() == 0)
		{
			continue;
		}
		FVector Min(FLT_MAX), Max(-FLT_MAX);
		for (const FVector& V : Part.BaseVertices)
		{
			Min = Min.ComponentMin(V);
			Max = Max.ComponentMax(V);
		}
		const float SpanZ = FMath::Max(1.0f, Max.Z - Min.Z);
		const FVector Center = (Min + Max) * 0.5f;
		TArray<FVector> Deformed;
		Deformed.Reserve(Part.BaseVertices.Num());
		const bool bHead = Part.Role.Contains(TEXT("head")) || Part.Role.Contains(TEXT("face"));
		const bool bBody = Part.Role.StartsWith(TEXT("body"));
		for (const FVector& V : Part.BaseVertices)
		{
			FVector D = V;
			if (bHead)
			{
				D = Center + (V - Center) * HeadScale;
			}
			else if (bBody)
			{
				const float RelZ = (V.Z - Min.Z) / SpanZ;
				D.Z = Min.Z + (V.Z - Min.Z) * HeightScale;
				if (RelZ > 0.55f)
				{
					D.X *= BustScale;
					D.Y *= BustScale;
				}
				else if (RelZ < 0.38f)
				{
					D.X *= HipScale;
					D.Y *= HipScale;
				}
			}
			Deformed.Add(D);
		}
		FillSection(Part.Comp, Deformed, Part.Triangles, Part.UVs, Part.Colors, TArray<FVector>(), Part.UV4, Part.UV2,TArray<FProcMeshTangent>(),Part.UV3);
	}
}

struct FOkCamRig
{
	FVector BodyCenter;
	FVector FaceCenter;
	FVector SideCenter;
	FTransform RootAnchor;
	FTransform HeadAnchor;
	bool bRootAnchor = false;
	bool bHeadAnchor = false;
	FVector BodyLoc;
	FRotator BodyRot;
	FVector BodySideLoc;
	FRotator BodySideRot;
	FVector BodyBackLoc;
	FRotator BodyBackRot;
	FVector FaceLoc;
	FRotator FaceRot;
	FVector SideLoc;
	FRotator SideRot;
};

static FOkCamRig GStudioRig;

// PIE starts on Template_Default's bright sky. Freezing eye adaptation there
// (r.EyeAdaptationQuality 0) locks a near-black exposure, so the studio and
// character render but the viewport stays empty.
static void ApplyStudioExposure(FPostProcessSettings& S)
{
	// Unity's character shader writes its color directly to the display.
	// UE's film curve otherwise darkens the near-white skin and pastel hair
	// after their game colors have already been composed.
	S.bOverride_ToneCurveAmount = true;
	S.ToneCurveAmount = 0.f;
	S.bOverride_ExpandGamut = true;
	S.ExpandGamut = 0.f;
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = AEM_Manual;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = 0.f;
	S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	S.AutoExposureApplyPhysicalCameraExposure = false;
	S.bOverride_AutoExposureMinBrightness = true;
	S.AutoExposureMinBrightness = 1.f;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMaxBrightness = 1.f;
	S.bOverride_MotionBlurAmount = true;
	S.MotionBlurAmount = 0.f;
	S.bOverride_MotionBlurMax = true;
	S.MotionBlurMax = 0.f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.f;
}

// Compute the body/face/side camera rig from the actor's current part bounds and
// shift the actor so the feet sit on Z=0.
static FOkCamRig OkComputeCameras(AKKPreviewActor* Actor)
{
    Actor->EnsurePrivateGpuQueryGeometry();
	// Use the actual vertices (in actor-local space) rather than
	// `Comp->Bounds`: ProceduralMeshComponent bounds can go stale after
	// `CreateMeshSection` (an outfit-0 accessory reported a 2.19m top while its
	// highest vertex was 1.49m), which threw the face camera far above the head.
	FBox Bound(ForceInit);
	for (const FKKLoadedPart& Part : Actor->Parts)
	{
		if (Part.BaseVertices.Num() == 0)
		{
			continue;
		}
		if (Part.Comp && !Part.Comp->IsVisible())
		{
			continue;
		}
		FVector Mn(FLT_MAX), Mx(-FLT_MAX);
		for (const FVector& V : Part.BaseVertices)
		{
			Mn = Mn.ComponentMin(V);
			Mx = Mx.ComponentMax(V);
		}
		if (Mx.Z < 40.f && (Part.Role.Contains(TEXT("hair")) || Part.Role.Contains(TEXT("acc"))))
		{
			continue;
		}
		const FTransform Rel = Part.Comp ? Part.Comp->GetRelativeTransform() : FTransform::Identity;
		for (const FVector& V : Part.BaseVertices)
		{
			Bound += Rel.TransformPosition(V);
		}
	}
	if (!Bound.IsValid)
	{
		Bound = FBox(FVector(-50, -50, 0), FVector(50, 50, 160));
	}
	const float FootZ = Bound.Min.Z;
	Actor->SetActorLocation(FVector(0.f, 0.f, -FootZ));
	Bound = Bound.ShiftBy(FVector(0.f, 0.f, -FootZ));
	const FVector BodyCenter = Bound.GetCenter();
	const float SizeZ = Bound.GetSize().Z;
	const float LookZ = Bound.Min.Z + SizeZ * 0.505f;
	// Fit the full T pose into a portrait or near-square view. The game's
	// reference captures use about 5% vertical margin; narrower viewports
	// need extra distance so the outstretched hands also remain in frame.
	FIntPoint ViewSize(900, 1080);
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		ViewSize = GEngine->GameViewport->Viewport->GetSizeXY();
	}
	const float Aspect = float(FMath::Max(ViewSize.X, 1)) / float(FMath::Max(ViewSize.Y, 1));
	const float PortraitWidthFit = FMath::Max(1.f, 0.83f / Aspect);
	const float BodyDist = FMath::Clamp(SizeZ * 2.63f * PortraitWidthFit, 390.f, 980.f);
	FOkCamRig R;
	R.BodyCenter = FVector(BodyCenter.X, BodyCenter.Y, LookZ);
	R.BodyLoc = FVector(BodyCenter.X, BodyCenter.Y + BodyDist, LookZ);
	R.BodyRot = (R.BodyCenter - R.BodyLoc).Rotation();
	const FVector FaceCenter = FVector(BodyCenter.X, BodyCenter.Y, Bound.Min.Z + SizeZ * 0.88f);
	FBox FaceBounds(EForceInit::ForceInit);
	for (const FKKLoadedPart& Part : Actor->Parts)
	{
		if (!Part.bFaceSphereNormals || Part.BaseVertices.IsEmpty())
		{
			continue;
		}
		const FTransform Rel = Part.Comp ? Part.Comp->GetRelativeTransform() : FTransform::Identity;
		for (const FVector& Vertex : Part.BaseVertices)
		{
			FaceBounds += Rel.TransformPosition(Vertex) - FVector(0.f, 0.f, FootZ);
		}
	}
	// Long hair can shift the full-body bound far behind the head in profile.
	// Look at the actual face surface for side portraits on every card.
	const FVector SideCenter = FaceBounds.IsValid ? FaceBounds.GetCenter() : FaceCenter;
	R.FaceCenter = FaceCenter;
	R.SideCenter = SideCenter;
	if (Actor->BodySkel)
	{
		R.bRootAnchor = Actor->BodySkel->GetBoneIndex(TEXT("cf_j_root")) != INDEX_NONE;
		R.bHeadAnchor = Actor->BodySkel->GetBoneIndex(TEXT("cf_s_head")) != INDEX_NONE;
		if (R.bRootAnchor) R.RootAnchor = Actor->BodySkel->GetSocketTransform(TEXT("cf_j_root"), RTS_World);
		if (R.bHeadAnchor) R.HeadAnchor = Actor->BodySkel->GetSocketTransform(TEXT("cf_s_head"), RTS_World);
	}
	const float FaceDist = FMath::Clamp(SizeZ * 0.28f, 90.0f, 130.0f);
	// The shot camera uses a 50 degree FOV and keeps its 16:9
	// reference aspect even in a portrait viewport.  The vertical half-angle
	// is therefore narrower than tan(25 degrees) / viewport aspect.  Accessories can be much
	// closer to either camera than the face, so height alone cannot tell whether
	// their upper vertices fit in a perspective image.  Fit the actual upper
	// geometry independently for the front and side views.  Reserve a quarter
	// of the vertical half-frustum so the top is comfortably inside the shot.
	const float TanVerticalHalfFov = FMath::Tan(FMath::DegreesToRadians(25.f)) / FMath::Max(Aspect, 16.f / 9.f);
	const FVector FrontAxis(0.f, 1.f, 0.f);
	const FVector SideAxis = FVector(-1.f, 0.08f, 0.f).GetSafeNormal();
	float FrontDist = FaceDist;
	float SideDist = FaceDist;
	for (const FKKLoadedPart& Part : Actor->Parts)
	{
		if (Part.BaseVertices.IsEmpty() || (Part.Comp && !Part.Comp->IsVisible()))
		{
			continue;
		}
		const FTransform Rel = Part.Comp ? Part.Comp->GetRelativeTransform() : FTransform::Identity;
		for (const FVector& Vertex : Part.BaseVertices)
		{
			const FVector P = Rel.TransformPosition(Vertex) - FVector(0.f, 0.f, FootZ);
			if (P.Z <= FaceCenter.Z)
			{
				continue;
			}
			const float TopSpan = (P.Z - FaceCenter.Z) / (TanVerticalHalfFov * 0.75f);
			FrontDist = FMath::Max(FrontDist, FVector::DotProduct(P - FaceCenter, FrontAxis) + TopSpan);
			const float SideTopSpan = (P.Z - SideCenter.Z) / (TanVerticalHalfFov * 0.75f);
			SideDist = FMath::Max(SideDist, FVector::DotProduct(P - SideCenter, SideAxis) + SideTopSpan);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("OK face framing base=%.1f front=%.1f side=%.1f top=%.1f"),
		FaceDist, FrontDist, SideDist, Bound.Max.Z);
	R.FaceLoc = FaceCenter + FrontAxis * FrontDist;
	R.FaceRot = (FaceCenter - R.FaceLoc).Rotation();
	R.SideLoc = SideCenter + SideAxis * SideDist;
	R.SideRot = (SideCenter - R.SideLoc).Rotation();
	return R;
}

// Enclose both the displayed model and all capture cameras. A fixed rear wall
// cannot cover portraits that follow a head rotated toward the other side.
static void OkUpdateStudioBackdrop(UWorld* World,FBox Bounds,const TArray<FVector>& Cameras)
{
    if(!World||Cameras.IsEmpty()) return;
    if(!Bounds.IsValid) Bounds=FBox(Cameras[0]-FVector(1.),Cameras[0]+FVector(1.));
    for(const FVector& Camera:Cameras) Bounds+=Camera;
    Bounds=Bounds.ExpandBy(FMath::Max(100.,Bounds.GetSize().GetMax()*.25));
    const FVector Center=Bounds.GetCenter(),Extent=Bounds.GetExtent();
    const TArray<int32> Triangles={0,1,2,0,2,3};
    TArray<FVector2D> UVs;UVs.Init(FVector2D::ZeroVector,4);
    TArray<FColor> Colors;Colors.Init(FColor::White,4);
    TArray<FProcMeshTangent> Tangents;
    for(TActorIterator<AActor> It(World);It;++It) {
        if(!It->ActorHasTag(TEXT("OKStudioBackdrop"))||It->IsHidden()) continue;
        auto* Shell=Cast<UProceduralMeshComponent>(It->GetRootComponent());
        if(!Shell||!Shell->ComponentHasTag(TEXT("OKStudioBackdropShell"))) continue;
        int32 Section=0;
        bool Changed=false;
        for(int32 Axis=0;Axis<3;++Axis) for(double Side:{-1.,1.}) {
            FVector C=Center,U=FVector::ZeroVector,V=FVector::ZeroVector,N=FVector::ZeroVector;
            C[Axis]+=Side*Extent[Axis];U[(Axis+1)%3]=Extent[(Axis+1)%3];V[(Axis+2)%3]=Extent[(Axis+2)%3];N[Axis]=-Side;
            const TArray<FVector> Vertices={C-U-V,C+U-V,C+U+V,C-U+V};
            TArray<FVector> Normals;Normals.Init(N,4);
            const FProcMeshSection* Existing=Shell->GetProcMeshSection(Section);
            bool Same=Existing&&Existing->ProcVertexBuffer.Num()==Vertices.Num();
            if(Same)for(int32 Vertex=0;Vertex<Vertices.Num();++Vertex)
                if(Existing->ProcVertexBuffer[Vertex].Position!=Vertices[Vertex]){Same=false;break;}
            if(!Same){
                if(Existing&&Existing->ProcVertexBuffer.Num()==Vertices.Num())
                    UKKAffineProceduralMeshComponent::UpdateStreams(Shell,Section,Vertices,Normals,UVs,Colors,Tangents);
                else Shell->CreateMeshSection(Section,Vertices,Triangles,Normals,UVs,Colors,Tangents,false);
                Changed=true;
            }
            ++Section;
        }
        if(Changed)UE_LOG(LogTemp,Display,TEXT("OK studio backdrop enclosure min=%s max=%s cameras=%d planes=6"),*Bounds.Min.ToString(),*Bounds.Max.ToString(),Cameras.Num());
    }
}

// Capture framing uses the displayed frame, including morphs and private
// accessory skinning. Reading CPU skinned vertices flushes rendering, so this
// runs once per frozen capture rather than in the interactive animation tick.
static void OkFitCaptureRig(AKKPreviewActor* Actor, FOkCamRig& Rig,AKKPreviewActor* Partner=nullptr,bool Realtime=false)
{
	if (!Actor || !Actor->StudioCam || !GEngine || !GEngine->GameViewport ||
		!GEngine->GameViewport->Viewport) { return; }
	TArray<FVector> Points;
	const auto AddBox=[&](const FBox& Box){if(!Box.IsValid)return;for(int32 Corner=0;Corner<8;++Corner)Points.Add(FVector(Corner&1?Box.Max.X:Box.Min.X,Corner&2?Box.Max.Y:Box.Min.Y,Corner&4?Box.Max.Z:Box.Min.Z));};
	for(AKKPreviewActor* DisplayActor : {Actor,Partner}) {
	if(!IsValid(DisplayActor))continue;
	// Realtime: the joints of the current pose. Mesh boxes are not posed under GPU skinning (procedural section boxes stay at
	// the reference pose, standing with arms out), so a kneeling pair was framed as two standing figures from metres away.
	if(Realtime&&DisplayActor->AppendJointFramePoints(Points))continue;
    if(!Realtime)DisplayActor->EnsurePrivateGpuQueryGeometry();
	TInlineComponentArray<UProceduralMeshComponent*> Procedural;
	DisplayActor->GetComponents(Procedural);
	for (UProceduralMeshComponent* Component : Procedural)
	{
		if (!Component->IsVisible()) { continue; }
		for (int32 SectionIndex = 0; SectionIndex < Component->GetNumSections(); ++SectionIndex)
		{
			const FProcMeshSection* Section = Component->GetProcMeshSection(SectionIndex);
			if (!Section || !Section->bSectionVisible) { continue; }
			if(Realtime){AddBox(Section->SectionLocalBox.TransformBy(Component->GetRenderMatrix()));continue;}
			for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
				Points.Add(Component->GetRenderMatrix().TransformPosition(Vertex.Position));
		}
	}
	TInlineComponentArray<USkinnedMeshComponent*> Skinned;
	DisplayActor->GetComponents(Skinned);
	for (USkinnedMeshComponent* Component : Skinned)
	{
		if (!Component->IsVisible() || !Component->GetSkinnedAsset()) { continue; }
		// Runtime skin bounds include current bone palettes and active morphs.
		// Their conservative envelopes avoid a render flush/GPU vertex readback.
		if(Realtime){AddBox(Component->CalcBounds(Component->GetComponentTransform()).GetBox());continue;}
		TArray<FFinalSkinVertex> Vertices;
		Component->GetCPUSkinnedVertices(Vertices, 0);
		for (const FFinalSkinVertex& Vertex : Vertices)
			Points.Add(Component->GetComponentTransform().TransformPosition(FVector(Vertex.Position)));
	}
	}
	if (Points.IsEmpty()) { return; }
	FViewport* Viewport = GEngine->GameViewport->Viewport;
	APlayerController* PC = Actor->GetWorld()->GetFirstPlayerController();
	ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
	if (!Player || !PC->PlayerCameraManager) { return; }
	// The player camera manager can override the camera component's FOV.
	// Read the same final view/projection used by rendering after updating its
	// current target; the component alone is not the effective lens.
	PC->PlayerCameraManager->UpdateCamera(0.f);
	const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
	FSceneViewProjectionData Projection;
	if (!Player->GetProjectionData(Viewport, Projection)) { return; }
	const double ScaleX = Projection.ProjectionMatrix.M[0][0];
	const double ScaleY = Projection.ProjectionMatrix.M[1][1];
	if (ScaleX <= 0.0 || ScaleY <= 0.0) { return; }
	UE_LOG(LogTemp, Display, TEXT("OK capture projection cache_fov=%.3f effective_fov=%.3f scale_x=%.6f scale_y=%.6f"),
		View.FOV, PC->PlayerCameraManager->GetFOVAngle(), ScaleX, ScaleY);
	FBox Bounds(ForceInit);
	for (const FVector& Point : Points) Bounds += Point;
	Rig.BodyCenter = Bounds.GetCenter();
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_FOUR_VIEWS")) == TEXT("1"))
	{
		const double BodyDistance = FVector::Distance(Rig.BodyLoc, Rig.BodyCenter);
		Rig.BodySideLoc = Rig.BodyCenter + FVector(-BodyDistance, 0., 0.);
		Rig.BodySideRot = (Rig.BodyCenter - Rig.BodySideLoc).Rotation();
		Rig.BodyBackLoc = Rig.BodyCenter + FVector(0., -BodyDistance, 0.);
		Rig.BodyBackRot = (Rig.BodyCenter - Rig.BodyBackLoc).Rotation();
	}
	auto Fit = [&](const TCHAR* Label, const FVector& Center, FVector& Location, FRotator& Rotation, bool UpperOnly)
	{
		const FVector TowardCamera = FRotationMatrix(Rotation).GetScaledAxis(EAxis::X) * -1.0;
		const FVector Right = FRotationMatrix(Rotation).GetScaledAxis(EAxis::Y);
		const FVector Up = FRotationMatrix(Rotation).GetScaledAxis(EAxis::Z);
		constexpr double Margin = 0.92;
		double Distance = UpperOnly ? FVector::Distance(Location, Center) : 1.0;
		int32 Used = 0;
		for (const FVector& Point : Points)
		{
			if (UpperOnly && FVector::DotProduct(Point - Center, Up) < 0.) { continue; }
			const FVector Relative = Point - Center;
			const double Depth = FVector::DotProduct(Relative, TowardCamera);
			Distance = FMath::Max(Distance, Depth + FMath::Abs(FVector::DotProduct(Relative, Right)) * ScaleX / Margin);
			Distance = FMath::Max(Distance, Depth + FMath::Abs(FVector::DotProduct(Relative, Up)) * ScaleY / Margin);
			Distance = FMath::Max(Distance, Depth + View.GetFinalPerspectiveNearClipPlane() + 1.0);
			++Used;
		}
		Location = Center + TowardCamera * Distance;
		// The viewing axis already points at Center; preserve its authored roll.
		double MaxX = 0.0, MaxY = 0.0;
		for (const FVector& Point : Points)
		{
			if (UpperOnly && FVector::DotProduct(Point - Center, Up) < 0.) { continue; }
			const FVector Relative = Point - Center;
			const double Depth = Distance - FVector::DotProduct(Relative, TowardCamera);
			MaxX = FMath::Max(MaxX, FMath::Abs(FVector::DotProduct(Relative, Right)) * ScaleX / Depth);
			MaxY = FMath::Max(MaxY, FMath::Abs(FVector::DotProduct(Relative, Up)) * ScaleY / Depth);
		}
		UE_LOG(LogTemp, Display, TEXT("OK capture fit view=%s points=%d distance=%.3f ndc_x=%.6f ndc_y=%.6f limit=0.92"),
			Label, Used, Distance, MaxX, MaxY);
	};
	Fit(TEXT("body"), Rig.BodyCenter, Rig.BodyLoc, Rig.BodyRot, false);
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_FOUR_VIEWS")) == TEXT("1"))
	{
		Fit(TEXT("body_side"), Rig.BodyCenter, Rig.BodySideLoc, Rig.BodySideRot, false);
		Fit(TEXT("body_back"), Rig.BodyCenter, Rig.BodyBackLoc, Rig.BodyBackRot, false);
	}
	Fit(TEXT("face"), Rig.FaceCenter, Rig.FaceLoc, Rig.FaceRot, true);
	Fit(TEXT("side"), Rig.SideCenter, Rig.SideLoc, Rig.SideRot, true);
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_MATERIAL_CLOSEUPS")) == TEXT("1"))
	{
		// Native-resolution detail capture, independent of production cameras.
		const double Height = Bounds.GetSize().Z;
		FVector Focus = Bounds.GetCenter();
		Focus.Z = Bounds.Min.Z + Height * .78;
		const double Distance = Height * .25 * ScaleY / .92;
		auto DetailView = [&](FVector& Location, const FRotator& Rotation) {
			Location = Focus - FRotationMatrix(Rotation).GetScaledAxis(EAxis::X) * Distance;
		};
		DetailView(Rig.BodyLoc, Rig.BodyRot);
		DetailView(Rig.BodySideLoc, Rig.BodySideRot);
		DetailView(Rig.BodyBackLoc, Rig.BodyBackRot);
		UE_LOG(LogTemp, Display, TEXT("OK material closeup framing height=%.3f distance=%.3f"), Height, Distance);
	}
	TArray<FVector> BackdropCameras{Rig.BodyLoc,Rig.FaceLoc,Rig.SideLoc};
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_FOUR_VIEWS")) == TEXT("1"))
	{
		BackdropCameras.Add(Rig.BodySideLoc);
		BackdropCameras.Add(Rig.BodyBackLoc);
	}
	OkUpdateStudioBackdrop(Actor->GetWorld(),Bounds,BackdropCameras);
}

static FOkCamRig OkPoseCameraRig(AKKPreviewActor* Actor, const FOkCamRig& Neutral)
{
	FOkCamRig Rig = Neutral;
	if (!Actor || !Actor->BodySkel) { return Rig; }
	// Body retains the selected world viewing axis. Portraits follow the actual
	// head orientation as well as its location, including lying/rotated poses.
	if (Neutral.bRootAnchor)
	{
		const FTransform Current = Actor->BodySkel->GetSocketTransform(TEXT("cf_j_root"), RTS_World);
		Rig.BodyCenter = Current.TransformPosition(Neutral.RootAnchor.InverseTransformPosition(Neutral.BodyCenter));
		Rig.BodyLoc += Rig.BodyCenter - Neutral.BodyCenter;
	}
	if (Neutral.bHeadAnchor)
	{
		const FTransform Current = Actor->BodySkel->GetSocketTransform(TEXT("cf_s_head"), RTS_World);
		Rig.FaceCenter = Current.TransformPosition(Neutral.HeadAnchor.InverseTransformPosition(Neutral.FaceCenter));
		Rig.SideCenter = Current.TransformPosition(Neutral.HeadAnchor.InverseTransformPosition(Neutral.SideCenter));
		const FQuat Delta = Current.GetRotation() * Neutral.HeadAnchor.GetRotation().Inverse();
		Rig.FaceLoc = Rig.FaceCenter + Delta.RotateVector(Neutral.FaceLoc - Neutral.FaceCenter);
		Rig.SideLoc = Rig.SideCenter + Delta.RotateVector(Neutral.SideLoc - Neutral.SideCenter);
		Rig.FaceRot = (Delta * Neutral.FaceRot.Quaternion()).Rotator();
		Rig.SideRot = (Delta * Neutral.SideRot.Quaternion()).Rotator();
	}
	UE_LOG(LogTemp, Display, TEXT("OK shot pose camera body_delta=%s face_delta=%s"),
		*(Rig.BodyCenter - Neutral.BodyCenter).ToString(), *(Rig.FaceCenter - Neutral.FaceCenter).ToString());
	OkFitCaptureRig(Actor, Rig);
	return Rig;
}

void AKKPreviewActor::FrameCurrentPose()
{
	if (!StudioCam) { return; }
	FOkCamRig Rig = GStudioRig;
	Rig.BodyRot = StudioCam->GetActorRotation();
	OkFitCaptureRig(this, Rig);
	ResetOrbit(Rig.BodyCenter, Rig.BodyLoc);
	StudioCam->SetActorLocationAndRotation(Rig.BodyLoc, Rig.BodyRot);
}

void AKKPreviewActor::FramePairedPose(AKKPreviewActor* Partner){
    if(!StudioCam||!IsValid(Partner))return;FOkCamRig Rig=GStudioRig;Rig.BodyRot=StudioCam->GetActorRotation();
    OkFitCaptureRig(this,Rig,Partner,true);ResetOrbit(Rig.BodyCenter,Rig.BodyLoc);StudioCam->SetActorLocationAndRotation(Rig.BodyLoc,Rig.BodyRot);
}

void AKKPreviewActor::SetupStudioView(bool PreserveWorld)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if(!PreserveWorld)OKDisableRealtimeSky(World);
	SetActorTickEnabled(true);
	// Do not r.SetRes to GetDesktopResolution(). That value is physical pixels
	// (3840x2160). This process is DPI-aware, so the window that already fills
	// the monitor is the work area in DIPs (3840/1.75 = 2194). Forcing 3840
	// makes the viewport ~1.75x the window; after maximize the picture stays in
	// the top-left at about a quarter of the client area.
	const FOkCamRig Rig = OkComputeCameras(this);
	GStudioRig = Rig;
	if (Parts.Num() > 0 && Parts[0].BaseVertices.Num() > 0)
	{
		FVector Mn(1e9), Mx(-1e9);
		for (const FVector& V : Parts[0].BaseVertices)
		{
			Mn = Mn.ComponentMin(V);
			Mx = Mx.ComponentMax(V);
		}
		UE_LOG(LogTemp, Display, TEXT("OK body verts n=%d min=%s max=%s"), Parts[0].BaseVertices.Num(), *Mn.ToString(), *Mx.ToString());
	}
	for (TActorIterator<AActor> It(World); !PreserveWorld && It; ++It)
	{
		AActor* A = *It;
		// Preview interaction tools survive scene cleanup across card/outfit reloads.
		if (!A || A == this || A->ActorHasTag(TEXT("OKPreviewInteraction")))
		{
			continue;
		}
		if (Cast<APlayerController>(A) || Cast<AHUD>(A) || Cast<APlayerCameraManager>(A)
			|| Cast<AGameModeBase>(A) || Cast<AGameStateBase>(A) || Cast<APlayerState>(A)
			|| Cast<APlayerStart>(A) || Cast<APawn>(A) || Cast<ACameraActor>(A))
		{
			continue;
		}
		A->SetActorHiddenInGame(true);
		A->SetActorEnableCollision(false);
	}
	if (StudioLens)
	{
		StudioLens->SetRelativeLocation(FVector(0.f, 320.f, 90.f));
		StudioLens->SetRelativeRotation(FRotator(-6.f, -90.f, 0.f));
		StudioLens->SetFieldOfView(35.f);
		StudioLens->SetConstraintAspectRatio(false);
		StudioLens->Activate();
	}
	if (StudioCam)
	{
		StudioCam->Destroy();
		StudioCam = nullptr;
	}
	StudioCam = World->SpawnActor<ACameraActor>(Rig.BodyLoc, Rig.BodyRot);
	if (StudioCam)
	{
		if (UCameraComponent* C = StudioCam->GetCameraComponent())
		{
			C->SetFieldOfView(50.f);
			C->SetConstraintAspectRatio(false);
			ApplyStudioExposure(C->PostProcessSettings);
			C->PostProcessBlendWeight = 1.f;
		}
	}
	if (!StudioCam)
	{
		const FVector CamW = GetActorLocation() + FVector(0.f, 320.f, 90.f);
		const FRotator CamR(-6.f, -90.f, 0.f);
		StudioCam = World->SpawnActor<ACameraActor>(CamW, CamR);
		if (StudioCam)
		{
			if (UCameraComponent* C = StudioCam->GetCameraComponent())
			{
				C->SetFieldOfView(35.f);
				C->SetConstraintAspectRatio(false);
			}
		}
	}
	if (StudioCam)
	{
		StudioCam->SetActorLocationAndRotation(Rig.BodyLoc, Rig.BodyRot);
		ResetOrbit(Rig.BodyCenter, Rig.BodyLoc);
		UE_LOG(LogTemp, Display, TEXT("OK cam world=%s rot=%s actor=%s"),
			*StudioCam->GetActorLocation().ToString(), *StudioCam->GetActorRotation().ToString(), *GetActorLocation().ToString());
	}
    if(PreserveWorld){
        // A mode owns its camera and color filter, never source-world volumes,
        // renderer CVars, show flags, lights, HUD or the camera manager's FOV.
        if(StudioCam){
            auto& Settings=StudioCam->GetCameraComponent()->PostProcessSettings;
            UTexture2D* Lut=TextureFromPngFile(FPaths::ProjectContentDir()/TEXT("KKPreview/KKSDefaultSaturationLUT16.png"),true);
            if(Lut){Lut->SRGB=false;Lut->LODGroup=TEXTUREGROUP_ColorLookupTable;Lut->UpdateResource();Settings.bOverride_ColorGradingLUT=true;Settings.ColorGradingLUT=Lut;Settings.bOverride_ColorGradingIntensity=true;Settings.ColorGradingIntensity=1.f;}
            if(auto* PC=World->GetFirstPlayerController())PC->SetViewTarget(StudioCam);
        }
        return;
    }
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->bAutoManageActiveCameraTarget = false;
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->SetActorHiddenInGame(true);
			Pawn->SetActorEnableCollision(false);
		}
		if (StudioCam)
		{
			PC->SetViewTarget(StudioCam);
		}
		PC->bShowMouseCursor = true;
		PC->bEnableClickEvents = true;
		if (AHUD* HUD = PC->GetHUD())
		{
			HUD->bShowHUD = true;
			HUD->SetActorHiddenInGame(false);
		}
		if (APlayerCameraManager* CamMgr = PC->PlayerCameraManager)
		{
			CamMgr->SetFOV(40.f);
		}
	}
	if (GEngine)
	{
		GEngine->Exec(World, TEXT("showhud 1"));
		GEngine->Exec(World, TEXT("r.DefaultFeature.AutoExposure 0"));
		GEngine->Exec(World, TEXT("r.EyeAdaptationQuality 0"));
		GEngine->Exec(World, TEXT("r.EyeAdaptation.PreExposureOverride 1"));
		GEngine->Exec(World, TEXT("r.ExposureOffset 0"));
		GEngine->Exec(World, TEXT("r.MotionBlurQuality 0"));
		GEngine->Exec(World, TEXT("r.DefaultFeature.MotionBlur 0"));
        // Native viewport resolution. Anti-aliasing follows ok.Viewport.AntiAliasing: SMAA by default (morphological,
        // no history, so fast hair does not smear like with TAA/TSR); 2 restores temporal AA.
        // Keep history at 100%: supersampling is not required for this fix.
		{
			static IConsoleVariable* Choice = IConsoleManager::Get().RegisterConsoleVariable(TEXT("ok.Viewport.AntiAliasing"), 5,
				TEXT("Anti-aliasing of the character viewport: 0 off, 1 FXAA, 2 TAA (temporal), 4 TSR (temporal), 5 SMAA (default). Applied when a preview takes the view."), ECVF_Default);
			GEngine->Exec(World, *FString::Printf(TEXT("r.AntiAliasingMethod %d"), FMath::Clamp(Choice ? Choice->GetInt() : 5, 0, 5)));
			GEngine->Exec(World, TEXT("r.SMAA.Quality 3"));
			// Supersampling (render above the window resolution, then downsample) keeps thin toon outlines and hair
			// strands continuous without any history: SMAA alone cannot rebuild a line thinner than a pixel.
			static IConsoleVariable* Scale = IConsoleManager::Get().RegisterConsoleVariable(TEXT("ok.Viewport.ScreenPercentage"), 200,
				TEXT("Render resolution of the character viewport in percent of the window (100 = native, 200 = 2x2 supersampling)."), ECVF_Default);
			GEngine->Exec(World, *FString::Printf(TEXT("r.ScreenPercentage %d"), FMath::Clamp(Scale ? Scale->GetInt() : 200, 50, 400)));
			GEngine->Exec(World, TEXT("r.Upscale.Quality 3"));
		}
        GEngine->Exec(World, TEXT("r.SecondaryScreenPercentage.GameViewport 100"));
        GEngine->Exec(World, TEXT("r.DynamicRes.OperationMode 0"));
        GEngine->Exec(World, TEXT("r.TemporalAA.Quality 2"));
        GEngine->Exec(World, TEXT("r.TemporalAA.HistoryScreenPercentage 100"));
        UE_LOG(LogTemp,Display,TEXT("OK character viewport quality: AA=%d primary=%d secondary=100 dynamicResolution=off history=100"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->GetInt(),IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))->GetInt());
		GEngine->Exec(World, TEXT("show Landscape 0"));
		GEngine->Exec(World, TEXT("show Foliage 0"));
		GEngine->Exec(World, TEXT("show SkyAtmosphere 0"));
		GEngine->Exec(World, TEXT("show VolumetricCloud 0"));
	}
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		ApplyStudioExposure(It->Settings);
		It->bUnbound = true;
		It->BlendWeight = 1.f;
	}
	if (APostProcessVolume* StudioPP = World->SpawnActor<APostProcessVolume>(Rig.BodyCenter, FRotator::ZeroRotator))
	{
		StudioPP->bUnbound = true;
		StudioPP->Priority = 100.f;
		StudioPP->BlendWeight = 1.f;
		ApplyStudioExposure(StudioPP->Settings);
		// UE's own color-grading LUT runs at the tonemapper's color stage. The
		// table is generated from KKS BetterColorPicker's inverse filter, so it
		// affects every card and the live viewport without per-material tuning.
		UTexture2D* Lut = TextureFromPngFile(FPaths::ProjectContentDir() / TEXT("KKPreview/KKSDefaultSaturationLUT16.png"), true);
		if (Lut && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NO_LUT")) != TEXT("1"))
		{
			Lut->SRGB = false;
			Lut->LODGroup = TEXTUREGROUP_ColorLookupTable;
			Lut->UpdateResource();
			StudioPP->Settings.bOverride_ColorGradingLUT = true;
			StudioPP->Settings.ColorGradingLUT = Lut;
			StudioPP->Settings.bOverride_ColorGradingIntensity = true;
			StudioPP->Settings.ColorGradingIntensity = 1.f;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("OK default KKS saturation LUT missing"));
		}
	}
	{
		auto AddDir = [World](const FVector& Dir, float Intensity, const FLinearColor& Color) -> ADirectionalLight*
		{
			ADirectionalLight* L = World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, Dir.Rotation());
			if (!L)
			{
				return nullptr;
			}
			if (UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(L->GetLightComponent()))
			{
				C->SetMobility(EComponentMobility::Movable);
				C->SetIntensity(Intensity);
				// Choose one main directional light instead of equal-priority fill lights.
				C->ForwardShadingPriority=FMath::RoundToInt(Intensity*10.f);
				C->SetLightColor(Color);
				C->SetCastShadows(false);
			}
			return L;
		};
		// The key light follows the camera (UpdateCameraKeyLight); fills stay fixed.
		StudioKeyLight = AddDir(FVector(0.25f, -0.85f, -0.45f), 2.5f, FLinearColor(1.0f, 0.97f, 0.93f));
		bCameraKeyLight = true;
		UpdateCameraKeyLight();
		AddDir(FVector(-0.75f, -0.35f, -0.10f), 1.5f, FLinearColor(0.90f, 0.93f, 1.0f));
		AddDir(FVector(0.0f, 0.90f, -0.30f), 1.2f, FLinearColor(0.95f, 0.95f, 1.0f));
	}
	AActor* Backdrop = World->SpawnActor<AActor>();
	if (Backdrop)
	{
		Backdrop->Tags.Add(TEXT("OKStudioBackdrop"));
		UProceduralMeshComponent* Shell = NewObject<UProceduralMeshComponent>(Backdrop);
		Shell->ComponentTags.Add(TEXT("OKStudioBackdropShell"));
		Backdrop->SetRootComponent(Shell);
		Shell->RegisterComponent();
		Shell->SetCastShadow(false);
		// The game's neutral-gray background is also behind the color filter.
		UMaterial* Gray=MakeSolidMaterial(FLinearColor(0.31f,0.347f,0.331f,1.f));
		for(int32 Section=0;Section<6;++Section) Shell->SetMaterial(Section,Gray);
		OkUpdateStudioBackdrop(World,GetComponentsBoundingBox(true),{Rig.BodyLoc,Rig.FaceLoc,Rig.SideLoc});
	}

}

// ---------------------------------------------------------------------------
// Multi-variant shot queue. `OK_RENDER_VARIANTS` is a `|`-separated list of
// variants; each variant is `tag:KEY=VALUE;KEY=VALUE` (tag optional). For every
// queued (variant, outfit) task we set the variant's env vars (so the in-process
// Rust session sees them on its next Open), load that outfit, capture the three
// shots, then chain the next task — all inside one UnrealEditor process so the
// expensive startup/backdrop setup happens once.
// ---------------------------------------------------------------------------
struct FShotTask
{
	int32 Variant = 0;
	int32 Outfit = 0;
	int32 Card = 0;
};

static TArray<FShotTask> GShotTasks;
static TArray<FString> GShotCards;
static TArray<TArray<TPair<FString, FString>>> GShotVariantEnv;
static TArray<FString> GShotVariantTag;
static TArray<FString> GShotAllKeys; // union of every variant's keys
static int32 GShotTaskIndex = 0;

static void ApplyShotVariantEnv(int32 Variant)
{
	// Unset every key any variant uses first: variants must be independent
	// (leaving a previous variant's OK_NOFBS=1 in place would poison the next).
	for (const FString& Key : GShotAllKeys)
	{
#if PLATFORM_WINDOWS
		::SetEnvironmentVariableW(*Key, nullptr);
#else
		unsetenv(TCHAR_TO_ANSI(*Key));
#endif
	}
	if (!GShotVariantEnv.IsValidIndex(Variant))
	{
		return;
	}
	for (const TPair<FString, FString>& KV : GShotVariantEnv[Variant])
	{
#if PLATFORM_WINDOWS
		::SetEnvironmentVariableW(*KV.Key, *KV.Value);
#else
		setenv(TCHAR_TO_ANSI(*KV.Key), TCHAR_TO_ANSI(*KV.Value), 1);
#endif
	}
}

static void BuildShotTasks(int32 NumOutfits)
{
	GShotTasks.Reset();
	GShotVariantEnv.Reset();
	GShotVariantTag.Reset();
	GShotAllKeys.Reset();
	const FString Spec = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RENDER_VARIANTS"));
	if (Spec.IsEmpty())
	{
		GShotVariantEnv.Add(TArray<TPair<FString, FString>>());
		GShotVariantTag.Add(TEXT(""));
	}
	else
	{
		TArray<FString> Variants;
		Spec.ParseIntoArray(Variants, TEXT("|"), true);
		for (const FString& V : Variants)
		{
			FString Tag;
			FString EnvList = V;
			int32 Colon = INDEX_NONE;
			if (V.FindChar(TEXT(':'), Colon))
			{
				Tag = V.Left(Colon);
				EnvList = V.Mid(Colon + 1);
			}
			TArray<TPair<FString, FString>> Pairs;
			TArray<FString> Items;
			EnvList.ParseIntoArray(Items, TEXT(";"), true);
			for (const FString& It : Items)
			{
				int32 Eq = INDEX_NONE;
				if (It.FindChar(TEXT('='), Eq))
				{
					Pairs.Add(TPair<FString, FString>(It.Left(Eq), It.Mid(Eq + 1)));
				}
			}
			const int32 Index = GShotVariantTag.Num();
			for (const TPair<FString, FString>& KV : Pairs)
			{
				GShotAllKeys.AddUnique(KV.Key);
			}
			GShotVariantEnv.Add(MoveTemp(Pairs));
			GShotVariantTag.Add(Tag.IsEmpty() ? FString::Printf(TEXT("_v%d"), Index) : TEXT("_") + Tag);
		}
	}
	const int32 NumCards = FMath::Max(GShotCards.Num(), 1);
	for (int32 C = 0; C < NumCards; ++C)
	{
		for (int32 V = 0; V < GShotVariantTag.Num(); ++V)
		{
			for (int32 O = 0; O < NumOutfits; ++O)
			{
				FShotTask T;
				T.Variant = V;
				T.Outfit = O;
				T.Card = C;
				GShotTasks.Add(T);
			}
		}
	}
	GShotTaskIndex = 0;
}

static void RequestCompletedShotReady(const FString& Path, AKKPreviewActor* Actor,
    const FVector& ExpectedLocation, const FRotator& ExpectedRotation,uint64 PreparedFrame)
{
    if(GFrameCounter-PreparedFrame<2&&IsValid(Actor)&&Actor->GetWorld()){
        TWeakObjectPtr<AKKPreviewActor> Weak(Actor);Actor->GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([Weak,Path,ExpectedLocation,ExpectedRotation,PreparedFrame](){RequestCompletedShotReady(Path,Weak.Get(),ExpectedLocation,ExpectedRotation,PreparedFrame);}));return;
    }

    APlayerController* PC = Actor && Actor->GetWorld() ? Actor->GetWorld()->GetFirstPlayerController() : nullptr;
    if (!PC || !PC->PlayerCameraManager || !Actor->StudioCam || PC->GetViewTarget()!=Actor->StudioCam)
    {
        UE_LOG(LogTemp, Error, TEXT("ok.ShotPreview camera target invalid path=%s"), *Path);
        FPlatformMisc::RequestExitWithStatus(false, 1);return;
    }
    const FMinimalViewInfo& View=PC->PlayerCameraManager->GetCameraCacheView();
    const double PositionError=FVector::Distance(View.Location,ExpectedLocation);
    const FRotator RotationDelta=(View.Rotation-ExpectedRotation).GetNormalized();
    const double RotationError=FMath::Abs(RotationDelta.Pitch)+FMath::Abs(RotationDelta.Yaw)+FMath::Abs(RotationDelta.Roll);
    if (PositionError>0.01 || RotationError>0.01)
    {
        UE_LOG(LogTemp,Error,TEXT("ok.ShotPreview camera cache stale position_error_cm=%.9f rotation_error_deg=%.9f path=%s"),PositionError,RotationError,*Path);
        FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    UE_LOG(LogTemp,Display,TEXT("ok.ShotPreview camera readiness frames=%llu position_error_cm=%.9f rotation_error_deg=%.9f fov=%.6f path=%s"),GFrameCounter-PreparedFrame,PositionError,RotationError,View.FOV,*Path);
    if (!Actor || Actor->IsActorTickEnabled())
    {
        UE_LOG(LogTemp, Error, TEXT("ok.ShotPreview frame is not frozen path=%s"), *Path);
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview frame readiness frozen=1 path=%s"), *Path);
	// View changes and per-frame resource work happen after task preparation.
	// Submit and complete that work at every capture boundary, not just once
	// when loading a character. This does not advance the frozen pose.
	if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
	FlushRenderingCommands();
	if (!Actor || !Actor->RefreshShotShadow())
	{
		UE_LOG(LogTemp, Error, TEXT("ok.ShotPreview shadow readiness failed path=%s"), *Path);
		FPlatformMisc::RequestExitWithStatus(false, 1);
		return;
	}
	ENQUEUE_RENDER_COMMAND(OKShotViewGpuReadiness)([](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.SubmitAndBlockUntilGPUIdle();
	});
	FlushRenderingCommands();
	UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview view GPU readiness completed=1 path=%s"), *Path);
	FScreenshotRequest::RequestScreenshot(Path, false, false);
}

static void RequestCompletedShot(const FString& Path, AKKPreviewActor* Actor)
{
    UWorld* World=Actor?Actor->GetWorld():nullptr;
    APlayerController* PC=World?World->GetFirstPlayerController():nullptr;
    if(!PC || !PC->PlayerCameraManager || !Actor->StudioCam)
    {UE_LOG(LogTemp,Error,TEXT("ok.ShotPreview camera unavailable path=%s"),*Path);FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const FVector Location=Actor->StudioCam->GetActorLocation();
    const FRotator Rotation=Actor->StudioCam->GetActorRotation();
    const FMinimalViewInfo Previous=PC->PlayerCameraManager->GetCameraCacheView();
    UE_LOG(LogTemp,Display,TEXT("ok.ShotPreview camera prepare previous_error_cm=%.9f actor=%s cache=%s path=%s"),FVector::Distance(Previous.Location,Location),*Location.ToString(),*Previous.Location.ToString(),*Path);
    PC->bAutoManageActiveCameraTarget=false;PC->SetViewTarget(Actor->StudioCam);
    PC->PlayerCameraManager->UpdateCamera(0.f);
    // A render/RHI fence does not build a new viewport view. Allow actual world
    // and camera updates before taking the screenshot; preserve the frozen pose.
    const TWeakObjectPtr<AKKPreviewActor> Weak(Actor);const uint64 PreparedFrame=GFrameCounter;
    World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([Weak,Path,Location,Rotation,PreparedFrame]()
    {
        if(AKKPreviewActor* A=Weak.Get()) A->GetWorld()->GetTimerManager().SetTimerForNextTick(
            FTimerDelegate::CreateLambda([Weak,Path,Location,Rotation,PreparedFrame](){RequestCompletedShotReady(Path,Weak.Get(),Location,Rotation,PreparedFrame);}));
    }));
}

static void RunShotTask(AKKPreviewActor* Actor, ACameraActor* CamActor, UWorld* World,
	const FString& GameRoot, const FString& CardPath);

// Load one task's outfit into the actor, set up the cameras and schedule the
// three screenshots, then chain to the next queued task (or quit).
static void RunShotTask(AKKPreviewActor* Actor, ACameraActor* CamActor, UWorld* World,
	const FString& GameRoot, const FString& CardPath)
{
	if (!Actor || !World || !GShotTasks.IsValidIndex(GShotTaskIndex))
	{
		GOkShotCaptureActive = false;
		FPlatformMisc::RequestExit(false);
		return;
	}
	const FShotTask Task = GShotTasks[GShotTaskIndex];
	ApplyShotVariantEnv(Task.Variant);
	const FString Tag = GShotVariantTag.IsValidIndex(Task.Variant) ? GShotVariantTag[Task.Variant] : FString();
	const FString TaskCard = GShotCards.IsValidIndex(Task.Card) ? GShotCards[Task.Card] : CardPath;

	FString Err;
	const double OpenStart = FPlatformTime::Seconds();
	const bool bOpened = Actor->Session.Open(GameRoot, TaskCard, Task.Outfit, Err);
	const double OpenEnd = FPlatformTime::Seconds();
	const bool bLoaded = bOpened && Actor->LoadSession(Err);
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE")) == TEXT("1"))
	{
		UE_LOG(LogTemp, Display, TEXT("OKCARD_PROFILE task=%d card=%s open_ms=%.1f ue_ms=%.1f success=%d"),
			GShotTaskIndex + 1, *FPaths::GetCleanFilename(TaskCard), (OpenEnd - OpenStart) * 1000.0,
			(FPlatformTime::Seconds() - OpenEnd) * 1000.0, bLoaded ? 1 : 0);
	}
	if (!bLoaded)
	{
		UE_LOG(LogTemp, Warning, TEXT("ok.ShotPreview card %s outfit %d failed: %s"), *TaskCard, Task.Outfit, *Err);
	}
	Actor->SetActorLocation(FVector::ZeroVector);
	Actor->SetupStudioView();
	FString ShotClip = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_GAME_CLIP"));
	const FString StudioRow = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_STUDIO_ROW"));
	if (!StudioRow.IsEmpty())
	{
		UKKAnimationCatalog* Catalog = UKKAnimationCatalog::LoadDefaultCatalog();
		FString Error;
		if (!Catalog || !Catalog->ResolveStudioAnimation(FName(*StudioRow),
			Actor->Session.ClipNamesForSource(Catalog->StudioSource(FName(*StudioRow))), ShotClip, Error))
		{
			UE_LOG(LogTemp, Error, TEXT("ok.ShotPreview imported Studio row failed: %s %s"), *StudioRow, *Error);
			GOkShotCaptureActive = false;
			FPlatformMisc::RequestExitWithStatus(false, 1);
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview imported Studio row=%s key=%s"), *StudioRow, *ShotClip);
	}
	const FString ShotScene = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_ANIM_SCENE"));
	if (!ShotScene.IsEmpty())
	{
		Actor->SetGameSceneMode(FCString::Atoi(*ShotScene));
	}
	FString StudioError;
	if (!ShotClip.IsEmpty() && !(StudioRow.IsEmpty()?Actor->SetGameAnimation(ShotClip):
		Actor->SetStudioAnimation(UKKAnimationCatalog::LoadDefaultCatalog(),FName(*StudioRow),StudioError)))
	{
		UE_LOG(LogTemp, Error, TEXT("ok.ShotPreview game clip missing: %s"), *ShotClip);
	}
    const FString FloatControl=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_ANIM_FLOAT"));
    TArray<FString> FloatControls;
    FloatControl.ParseIntoArray(FloatControls,TEXT(";"),true);
    for(const FString& Control:FloatControls) {
        FString Name,Value;
        if(!Control.Split(TEXT("="),&Name,&Value)||!Value.IsNumeric()||!Actor->SetGameAnimationFloat(Name,FCString::Atof(*Value))) {
            UE_LOG(LogTemp,Error,TEXT("ok.ShotPreview animation float rejected: %s"),*Control);
            GOkShotCaptureActive=false;FPlatformMisc::RequestExitWithStatus(false,1);return;
        }
        UE_LOG(LogTemp,Display,TEXT("OK shot animation float %s duration=%.9f"),*Control,Actor->Session.AnimDuration());
    }
	const FString ShotAnimTime = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_ANIM_TIME"));
	if (!ShotClip.IsEmpty() && !ShotAnimTime.IsEmpty())
	{
		const float TargetTime = FCString::Atof(*ShotAnimTime);
		if (!FMath::IsFinite(TargetTime) || TargetTime < 0.f || double(TargetTime)*60. > MAX_int32-1.)
		{ UE_LOG(LogTemp,Error,TEXT("invalid shot elapsed time"));FPlatformMisc::RequestExitWithStatus(false,1);return; }
		TArray<TPair<float,FString>> TriggerEvents;
		TArray<FString> EventStrings;
		FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_TRIGGER_EVENTS")).ParseIntoArray(EventStrings,TEXT(";"),true);
		for (const FString& Event : EventStrings) {
			FString Name,At;
			if (!Event.Split(TEXT("@"),&Name,&At,ESearchCase::CaseSensitive,ESearchDir::FromEnd) || !At.IsNumeric())
			{ UE_LOG(LogTemp,Error,TEXT("invalid shot trigger event"));FPlatformMisc::RequestExitWithStatus(false,1);return; }
			const float Epoch=FCString::Atof(*At);
			if (!FMath::IsFinite(Epoch)||Epoch<0.f||Epoch>TargetTime||(!TriggerEvents.IsEmpty()&&Epoch<TriggerEvents.Last().Key))
			{ UE_LOG(LogTemp,Error,TEXT("unordered shot trigger event"));FPlatformMisc::RequestExitWithStatus(false,1);return; }
			TriggerEvents.Emplace(Epoch,Name);
		}
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(TargetTime * 60.f));
		const int32 FadeLayer=FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_CROSSFADE_LAYER")));
		const FString FadeClip=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_CROSSFADE_CLIP"));
		const float FadeAt=FCString::Atof(*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_CROSSFADE_AT")));
		const float FadeDuration=FCString::Atof(*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_CROSSFADE_DURATION")));
		const float FadeOffset=FCString::Atof(*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_CROSSFADE_OFFSET")));
		struct FParallelFadeEvent{int32 Layer;float At,Duration,Offset;};
		TArray<FParallelFadeEvent> ParallelEvents;TArray<FString> ParallelStrings;
		FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_LAYER_CROSSFADE_EVENTS")).ParseIntoArray(ParallelStrings,TEXT(";"),true);
		for(const FString& Event:ParallelStrings){
			TArray<FString> Fields;Event.ParseIntoArray(Fields,TEXT("@"),false);
			if(Fields.Num()!=4||!Fields[0].IsNumeric()||!Fields[1].IsNumeric()||!Fields[2].IsNumeric()||!Fields[3].IsNumeric())
			{UE_LOG(LogTemp,Error,TEXT("invalid shot parallel fade fields"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
			const FParallelFadeEvent Parsed={FCString::Atoi(*Fields[0]),FCString::Atof(*Fields[1]),FCString::Atof(*Fields[2]),FCString::Atof(*Fields[3])};
			if(Parsed.Layer<=0||FString::FromInt(Parsed.Layer)!=Fields[0]||!FMath::IsFinite(Parsed.At)||!FMath::IsFinite(Parsed.Duration)||!FMath::IsFinite(Parsed.Offset)||Parsed.At<0.f||Parsed.At>TargetTime||Parsed.Duration<0.f||(!ParallelEvents.IsEmpty()&&Parsed.At<ParallelEvents.Last().At)||!FadeClip.IsEmpty()||!TriggerEvents.IsEmpty())
			{UE_LOG(LogTemp,Error,TEXT("invalid shot parallel fade schedule"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
			ParallelEvents.Add(Parsed);
		}
		if(!FadeClip.IsEmpty()&&(!TriggerEvents.IsEmpty()||!FMath::IsFinite(FadeAt)||FadeAt<0.f||FadeAt>TargetTime))
		{UE_LOG(LogTemp,Error,TEXT("invalid shot CrossFade schedule"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
		bool FadeFired=false;
		Actor->AnimPhase=0.f;int32 NextEvent=0,NextParallel=0;
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			const float FrameTime=TargetTime*Step/Steps;
			while(ParallelEvents.IsValidIndex(NextParallel)&&ParallelEvents[NextParallel].At<=FrameTime){
				const auto& Event=ParallelEvents[NextParallel++];Actor->ApplyPoseFrame(Event.At-Actor->AnimPhase);const float Held=Actor->AnimPhase;
				if(!Actor->CrossFadeGameAnimationLayer(ShotClip,Event.Layer,Event.Duration,Event.Offset)||Actor->AnimPhase!=Held)
				{UE_LOG(LogTemp,Error,TEXT("shot parallel fade failed"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
				UE_LOG(LogTemp,Display,TEXT("OK shot source ParallelFade layer=%d elapsed=%.9f normalized_duration=%.9f offset=%.9f elapsed_preserved=1"),Event.Layer,Held,Event.Duration,Event.Offset);
			}
			if(!FadeClip.IsEmpty()&&!FadeFired&&FadeAt<=FrameTime) {
				Actor->ApplyPoseFrame(FadeAt-Actor->AnimPhase);const float Held=Actor->AnimPhase;
				const bool Faded=FadeLayer>0?Actor->CrossFadeGameAnimationLayer(FadeClip,FadeLayer,FadeDuration,FadeOffset):Actor->CrossFadeGameAnimation(FadeClip,FadeDuration,FadeOffset);
				if(!Faded||Actor->AnimPhase!=Held)
				{UE_LOG(LogTemp,Error,TEXT("shot CrossFade failed"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
				FadeFired=true;UE_LOG(LogTemp,Display,TEXT("OK shot source CrossFade elapsed=%.9f normalized_duration=%.9f offset=%.9f elapsed_preserved=1 layer=%d"),Held,FadeDuration,FadeOffset,FadeLayer);
			}
			while (TriggerEvents.IsValidIndex(NextEvent) && TriggerEvents[NextEvent].Key<=FrameTime) {
				const auto& Event=TriggerEvents[NextEvent++];
				Actor->ApplyPoseFrame(Event.Key-Actor->AnimPhase);
				const float Held=Actor->AnimPhase;
				if (!Actor->SetGameAnimationTrigger(Event.Value) || Actor->AnimPhase!=Held)
				{ UE_LOG(LogTemp,Error,TEXT("shot trigger rejected or reset base elapsed"));FPlatformMisc::RequestExitWithStatus(false,1);return; }
				UE_LOG(LogTemp,Display,TEXT("OK shot source trigger name=%s elapsed=%.9f base_clock_unchanged=1"),*Event.Value,Held);
			}
			Actor->ApplyPoseFrame(FrameTime-Actor->AnimPhase);
		}
		Actor->RefreshSkinnedDisplay();
		Actor->SetActorTickEnabled(false);
		UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview fixed clip time %.3f seconds"), TargetTime);
	}
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_BODY_PHYSICS_AUDIT")) == TEXT("1"))
	{
		const bool bExternal=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_EXTERNAL_PHYSICS_AUDIT"))==TEXT("1");
		const FString TracePath = FPaths::ProjectSavedDir() / (bExternal?TEXT("Tests/ExternalBody288"):TEXT("Tests/BodyDynamics287")) /
			(FPaths::GetBaseFilename(TaskCard)+TEXT(".display.tsv"));
		if (!Actor->AuditBodyDynamicsDisplay(TracePath,bExternal))
		{
			UE_LOG(LogTemp,Error,TEXT("OK body dynamics display audit failed"));
			FPlatformMisc::RequestExitWithStatus(false,1);return;
		}
		Actor->SetActorTickEnabled(false);
	}
	const FString ShotMouth = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_MOUTH_PATTERN"));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FString Prefix = Side == 0 ? TEXT("OK_SHOT_HAND_LEFT") : TEXT("OK_SHOT_HAND_RIGHT");
		const FString A = FPlatformMisc::GetEnvironmentVariable(*Prefix);
		if (A.IsEmpty()) continue;
		const FString B = FPlatformMisc::GetEnvironmentVariable(*(Prefix + TEXT("_B")));
		const FString Blend = FPlatformMisc::GetEnvironmentVariable(*(Prefix + TEXT("_BLEND")));
		const int32 Pattern = FCString::Atoi(*A);
		if (!Actor->SetHandShape(Side, Pattern >= 0, FMath::Max(0, Pattern), B.IsEmpty() ? 0 : FCString::Atoi(*B), Blend.IsEmpty() ? 0.f : FCString::Atof(*Blend)))
		{ UE_LOG(LogTemp, Error, TEXT("ok.ShotPreview invalid hand selection side=%d"), Side); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
		UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview hand side=%d pattern=%d B=%s blend=%s count=%d"), Side, Pattern, *B, *Blend, Actor->GetHandShapeCount());
	}
	if (!ShotMouth.IsEmpty())
	{
		Actor->SetMouthPattern(FCString::Atoi(*ShotMouth),false);
	}
	const FString ShotMouthOpen = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_MOUTH_OPEN"));
	if (!ShotMouthOpen.IsEmpty())
	{
		Actor->SetMouthOpen(FCString::Atof(*ShotMouthOpen));
	}
    const bool TimeAudit=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_FACE_TIME_AUDIT"))==TEXT("1");
    if (TimeAudit || FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_FACE_AUDIT")) == TEXT("1"))
    {
        TArray<TArray<FVector>> Before;
        for (const auto& Slot : Actor->MouthSlots)
        {
            Before.AddDefaulted();
            if (Slot.CpuComp) for (int32 Section = 0; Section < Slot.CpuComp->GetNumSections(); ++Section)
                if (const auto* Mesh = Slot.CpuComp->GetProcMeshSection(Section))
                    for (const auto& Vertex : Mesh->ProcVertexBuffer) Before.Last().Add(Vertex.Position);
        }
        const int32 OldEye = Actor->FacePattern[0], OldBrow = Actor->FacePattern[1];
        const float OldEyeOpen = Actor->FaceOpen01[0], OldBrowOpen = Actor->FaceOpen01[1];
        const bool OldManualEye=Actor->bManualFaceControl[0],OldManualBrow=Actor->bManualFaceControl[1];
        const FOKFbsTransition OldTransitions[]={Actor->FaceTransitions[0],Actor->FaceTransitions[1],Actor->FaceTransitions[2]};
        const FOKFbsBlink OldBlink=Actor->FaceBlink;
        const float OldClock=Actor->FaceClock,OldBlinkEye=Actor->BlinkEyeRate,OldBlinkBrow=Actor->BlinkBrowRate,OldAnimTime=Actor->AnimPhase;
        const int32 NextEye=(OldEye+1)%Actor->GetFacePatternCount(1);
        if (!Actor->SetFaceControl(1, NextEye, 0.f, TimeAudit) || !Actor->SetFaceControl(2, (OldBrow+2)%Actor->GetFacePatternCount(2), .61337f, TimeAudit))
        { UE_LOG(LogTemp, Error, TEXT("OK face display audit lacks source expressions")); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
        bool TimeGood=true;
        if(TimeAudit)
        {
            Actor->AdvanceFaceAnimation(.05f);
            TimeGood=FMath::IsNearlyEqual(Actor->FaceTransitions[1].Alpha(),1.f/3.f,1e-6f);
        }
        int32 Changed = 0;
        for (int32 SlotIndex = 0; SlotIndex < Actor->MouthSlots.Num(); ++SlotIndex)
        {
            const auto& Slot = Actor->MouthSlots[SlotIndex]; int32 VertexIndex = 0;
            if (Slot.CpuComp) for (int32 Section = 0; Section < Slot.CpuComp->GetNumSections(); ++Section)
                if (const auto* Mesh = Slot.CpuComp->GetProcMeshSection(Section))
                    for (const auto& Vertex : Mesh->ProcVertexBuffer)
                        Changed += !Vertex.Position.Equals(Before[SlotIndex][VertexIndex++], 1e-5);
        }
        const bool Invalid = Actor->SetFaceControl(3, 0, .5f) || Actor->SetFaceControl(1, -1, .5f)
            || Actor->SetFaceControl(2, Actor->GetFacePatternCount(2), .5f);
        if(TimeAudit)
        {
            Actor->SetFaceControl(1,(NextEye+1)%Actor->GetFacePatternCount(1),.61337f,true);
            TimeGood&=Actor->FaceTransitions[1].Back==NextEye && Actor->FaceTransitions[1].Alpha()==0.f;
            Actor->AdvanceFaceAnimation(.2f);
            TimeGood&=Actor->FaceTransitions[1].Alpha()==1.f && Actor->AnimPhase==OldAnimTime;
        }
        Actor->FacePattern[0]=OldEye;Actor->FacePattern[1]=OldBrow;
        Actor->FaceOpen01[0]=OldEyeOpen;Actor->FaceOpen01[1]=OldBrowOpen;
        Actor->bManualFaceControl[0]=OldManualEye;Actor->bManualFaceControl[1]=OldManualBrow;
        for(int32 Index=0;Index<3;++Index) Actor->FaceTransitions[Index]=OldTransitions[Index];
        Actor->FaceBlink=OldBlink;Actor->FaceClock=OldClock;Actor->BlinkEyeRate=OldBlinkEye;Actor->BlinkBrowRate=OldBlinkBrow;
        Actor->ApplyMouth();
        double MaxError = 0.; int32 Vertices = 0;
        for (int32 SlotIndex = 0; SlotIndex < Actor->MouthSlots.Num(); ++SlotIndex)
        {
            const auto& Slot = Actor->MouthSlots[SlotIndex]; int32 VertexIndex = 0;
            if (Slot.CpuComp) for (int32 Section = 0; Section < Slot.CpuComp->GetNumSections(); ++Section)
                if (const auto* Mesh = Slot.CpuComp->GetProcMeshSection(Section))
                    for (const auto& Vertex : Mesh->ProcVertexBuffer)
                    { MaxError = FMath::Max(MaxError, (Vertex.Position - Before[SlotIndex][VertexIndex++]).Length()); ++Vertices; }
        }
        UE_LOG(LogTemp, Display, TEXT("OK face display audit eyes=%d brow=%d changed=%d vertices=%d restore_error_cm=%.9f invalid_accepted=%d"),
            Actor->GetFacePatternCount(1), Actor->GetFacePatternCount(2), Changed, Vertices, MaxError, Invalid);
        if(TimeAudit) UE_LOG(LogTemp,Display,TEXT("OK face time display audit passed=%d alpha=0.333333 interruption=previous_target completed=1 clock_unchanged=%d restore_error_cm=%.9f"),TimeGood,Actor->AnimPhase==OldAnimTime,MaxError);
        if (Invalid || Changed == 0 || MaxError > 1e-5 || !TimeGood)
        { UE_LOG(LogTemp, Error, TEXT("OK face display audit failed")); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
    }
    if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_GAZE_AUDIT"))==TEXT("1"))
    {
        TArray<FVector> Positions;TArray<FVector2D> Uvs,Uvs1,Uvs2;
        auto Snapshot=[&](bool Capture,double& PositionError,double& UvError,int32& Count) {
            for(const auto& Part:Actor->Parts) if(Part.Comp)
                for(int32 Section=0;Section<Part.Comp->GetNumSections();++Section)
                    if(const auto* Mesh=Part.Comp->GetProcMeshSection(Section))
                        for(const auto& Vertex:Mesh->ProcVertexBuffer) {
                            if(Capture) {Positions.Add(Vertex.Position);Uvs.Add(Vertex.UV0);Uvs1.Add(Vertex.UV1);Uvs2.Add(Vertex.UV2);}
                            else {
                                PositionError=FMath::Max(PositionError,(Vertex.Position-Positions[Count]).Length());
                                UvError=FMath::Max(UvError,(Vertex.UV0-Uvs[Count]).Size());
                                UvError=FMath::Max(UvError,(Vertex.UV1-Uvs1[Count]).Size());
                                UvError=FMath::Max(UvError,(Vertex.UV2-Uvs2[Count]).Size());
                            }
                            ++Count;
                        }
        };
        double PositionError=0.,UvError=0.;int32 Count=0;Snapshot(true,PositionError,UvError,Count);
        const float OldRates[]={Actor->EyeGaze[0],Actor->EyeGaze[1],Actor->EyeGaze[2]};
        const float Clock=Actor->AnimPhase,OldFixed=Actor->FaceOpen01[0];const bool OldManual=Actor->bManualFaceControl[0];
        bool Good=Actor->SetEyeGaze(.8f,-.6f,.7f);
        int32 IrisParts=0,ChangedUvs=0,ChangedUvs1=0,ChangedUvs2=0,Index=0;
        for(const auto& Part:Actor->Parts) if(Part.Comp) {
            if(Actor->Session.EyeMaterialSetting(Part.SessionIndex,-1)!=0.f) ++IrisParts;
            for(int32 Section=0;Section<Part.Comp->GetNumSections();++Section)
                if(const auto* Mesh=Part.Comp->GetProcMeshSection(Section)) for(const auto& Vertex:Mesh->ProcVertexBuffer) {
                    ChangedUvs1+=!Vertex.UV1.Equals(Uvs1[Index],1e-7);
                    ChangedUvs2+=!Vertex.UV2.Equals(Uvs2[Index],1e-7);
                    ChangedUvs+=!Vertex.UV0.Equals(Uvs[Index++],1e-7);
                    Good&=FMath::IsFinite(Vertex.UV0.X)&&FMath::IsFinite(Vertex.UV0.Y);
                }
        }
        Actor->bManualFaceControl[0]=true;Actor->FaceOpen01[0]=.61337f;
        Good&=Actor->EffectiveFaceRate(1)==.61337f;
        Actor->bManualFaceControl[0]=OldManual;Actor->FaceOpen01[0]=OldFixed;
        Good&=!Actor->SetEyeGaze(std::numeric_limits<float>::quiet_NaN(),0.f,0.f);
        Good&=Actor->SetEyeGaze(OldRates[0],OldRates[1],OldRates[2]);
        Count=0;Snapshot(false,PositionError,UvError,Count);
        Good&=IrisParts==2&&ChangedUvs>0&&Actor->AnimPhase==Clock&&PositionError<1e-5&&UvError<1e-7;
        UE_LOG(LogTemp,Display,TEXT("OK gaze display audit passed=%d iris_parts=%d changed_uvs=%d vertices=%d restore_position_cm=%.9f restore_uv=%.9f fixed_override=1 invalid_rejected=1 clock_unchanged=%d"),Good,IrisParts,ChangedUvs,Count,PositionError,UvError,Actor->AnimPhase==Clock);
        UE_LOG(LogTemp,Display,TEXT("OK eye highlight display audit passed=%d changed_uv1=%d changed_uv2=%d restore_all_uv=%.9f"),
            Good && ChangedUvs1>0 && ChangedUvs2>0,ChangedUvs1,ChangedUvs2,UvError);
        Good &= ChangedUvs1>0 && ChangedUvs2>0;
        if(!Good) {UE_LOG(LogTemp,Error,TEXT("OK gaze display audit failed"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    }
    const FString GazeShot=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_GAZE"));
    if(!GazeShot.IsEmpty()) {
        TArray<FString> Rates;GazeShot.ParseIntoArray(Rates,TEXT(","),false);
        if(Rates.Num()!=3||!Actor->SetEyeGaze(FCString::Atof(*Rates[0]),FCString::Atof(*Rates[1]),FCString::Atof(*Rates[2])))
        {UE_LOG(LogTemp,Error,TEXT("invalid gaze shot rates"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        UE_LOG(LogTemp,Display,TEXT("OK gaze shot rates=(%.6f,%.6f,%.6f) source_controller=%d"),Actor->EyeGaze[0],Actor->EyeGaze[1],Actor->EyeGaze[2],Actor->Session.FaceRuntimeSetting(11)!=0.f);
    }
    if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_EYE_LOOK"))==TEXT("1"))
    {
        const float Clock=Actor->FaceClock,Phase=Actor->AnimPhase;
        const FVector Target=Actor->GetActorTransform().TransformPosition(OkToUe(.45f,1.55f,2.f));
        const bool Selected=Actor->SetEyeLookTarget(Target,1);bool Good=Selected;int32 FailedStep=-1;
        for(int32 Step=0;Step<60;++Step) {const bool Advanced=Actor->AdvanceEyeLook(1.f/60.f);if(!Advanced) {FailedStep=Step;Good=false;break;}}
        const FVector Frozen(Actor->EyeGaze[0],Actor->EyeGaze[1],Actor->EyeGaze[2]);
        const bool Paused=Actor->AdvanceEyeLook(0.f);
        const bool Held=Frozen.Equals(FVector(Actor->EyeGaze[0],Actor->EyeGaze[1],Actor->EyeGaze[2]),0.f);Good&=Paused&&Held;
        Actor->StopEyeLookTracking();
        Good&=!Actor->bEyeLookTracking && Actor->FaceClock==Clock && Actor->AnimPhase==Phase;
        if(!Good) {UE_LOG(LogTemp,Error,TEXT("OK source world target gaze display failed selected=%d first_failed_step=%d paused=%d held=%d states=%d rates=(%.9f,%.9f,%.9f) clock_delta=%.9f phase_delta=%.9f"),
            Selected,FailedStep,Paused,Held,Actor->Session.EyeLookPatternCount(),Actor->EyeGaze[0],Actor->EyeGaze[1],Actor->EyeGaze[2],Actor->FaceClock-Clock,Actor->AnimPhase-Phase);FPlatformMisc::RequestExitWithStatus(false,1);return;}
        UE_LOG(LogTemp,Display,TEXT("OK eye look shot states=%d rates=(%.6f,%.6f,%.6f) paused_hold=1 clock_unchanged=1"),
            Actor->Session.EyeLookPatternCount(),Actor->EyeGaze[0],Actor->EyeGaze[1],Actor->EyeGaze[2]);
    }
    const FString BlinkShot=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_BLINK"));
    if(!BlinkShot.IsEmpty())
    {
        Actor->SetAutomaticBlink(true);
        Actor->SetFacePattern(1,0,false);Actor->SetFacePattern(2,0,false);
        Actor->ForceBlink();
        if(BlinkShot==TEXT("half")) Actor->AdvanceFaceAnimation(Actor->FaceBlink.CalcSpeed*.5f);
        else if(BlinkShot==TEXT("closed")) Actor->AdvanceFaceAnimation(Actor->FaceBlink.CalcSpeed+.001f);
        else if(BlinkShot==TEXT("open"))
        {
            Actor->FaceBlink.ForceOpen(Actor->FaceClock,[](float Min,float Max){return FMath::FRandRange(Min,Max);});
            Actor->AdvanceFaceAnimation(Actor->FaceBlink.CalcSpeed+.001f);
        }
        else { UE_LOG(LogTemp,Error,TEXT("invalid blink screenshot state"));FPlatformMisc::RequestExitWithStatus(false,1);return; }
        UE_LOG(LogTemp,Display,TEXT("OK blink shot state=%s open_rate=%.6f eyes=%.6f brow=%.6f sync=%d frequency=%d base_speed=%.6f"),
            *BlinkShot,Actor->FaceBlink.OpenRate,Actor->EffectiveFaceRate(1),Actor->EffectiveFaceRate(2),Actor->bBrowSyncBlink,Actor->FaceBlink.Frequency,Actor->FaceBlink.BaseSpeed);
    }
    for (int32 Control = 1; Control <= 2; ++Control)
    {
        const FString Prefix = Control == 1 ? TEXT("OK_SHOT_EYES") : TEXT("OK_SHOT_BROW");
        const FString Pattern = FPlatformMisc::GetEnvironmentVariable(*Prefix);
        const FString Open = FPlatformMisc::GetEnvironmentVariable(*(Prefix + TEXT("_OPEN")));
        if (Pattern.IsEmpty() && Open.IsEmpty()) continue;
        const int32 Ptn = Pattern.IsEmpty() ? 0 : FCString::Atoi(*Pattern);
        const float Rate = Open.IsEmpty() ? Actor->Session.FaceDefaultRate(Control) : FCString::Atof(*Open);
        if (!Actor->SetFaceControl(Control, Ptn, Rate))
        { UE_LOG(LogTemp, Error, TEXT("ok.ShotPreview invalid face control=%d"), Control); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
        UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview face control=%d pattern=%d open_percent=%d count=%d"), Control, Ptn, OKFbsOpenPercent(Rate), Actor->GetFacePatternCount(Control));
    }
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_RESET_POSE")) == TEXT("1"))
	{
		Actor->SetPoseMode(0);
		// Neutral-pose setup enables Tick. Hold this test frame so all three
		// cameras capture the same pose and the requested blink state.
		Actor->SetActorTickEnabled(false);
	}
    // Freeze after all requested pose/face controls, for every screenshot mode.
    // Pausing a clip alone does not stop FaceBlendShape.LateUpdate in Tick.
    Actor->SetActorTickEnabled(false);
    UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview frame frozen face_clock=%.9f anim_phase=%.9f"), Actor->FaceClock, Actor->AnimPhase);
	if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_PV_AUDIT"))==TEXT("1")&&!Actor->AuditStudioPVDisplay())
	{UE_LOG(LogTemp,Error,TEXT("OK Studio PV display audit failed"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_ATTACHMENT_AUDIT")) == TEXT("1"))
	{
		int32 AuditedParts = 0;
		for (const FKKLoadedPart& Part : Actor->Parts)
		{
			if (!Part.bRuntimeAccessory || !Part.Comp) { continue; }
			OkMeshView View;
			FMemory::Memzero(View);
			const FProcMeshSection* Section = Part.Comp->GetProcMeshSection(0);
			if (!Actor->Session.PartAt(Part.SessionIndex, View) || !View.positions || !Section ||
				Section->ProcVertexBuffer.Num() != View.n_verts)
			{
				UE_LOG(LogTemp, Error, TEXT("OK attachment display invalid runtime buffer: part index %d"), Part.SessionIndex);
				continue;
			}
			++AuditedParts;
			double MaxErrorCm = 0.0;
			for (int32 VertexIndex = 0; VertexIndex < View.n_verts; ++VertexIndex)
			{
				const float* P = View.positions + VertexIndex * 3;
				const FVector Expected = Actor->GetActorTransform().TransformPosition(OkToUe(P[0], P[1], P[2]));
				const FVector Displayed = Part.Comp->GetRenderMatrix().TransformPosition(
					Section->ProcVertexBuffer[VertexIndex].Position);
				MaxErrorCm = FMath::Max(MaxErrorCm, FVector::Distance(Expected, Displayed));
			}
			const FString Name = View.name ? UTF8_TO_TCHAR(View.name) : FString();
			UE_LOG(LogTemp, Display, TEXT("OK attachment display part=%s follow=%s max_vertex_error_cm=%.6f"),
				*Name, *Part.RigidFollowBone.ToString(), MaxErrorCm);
			if (Part.ShaderKind == 3)
			{
				FVector2D MinUv(FLT_MAX, FLT_MAX), MaxUv(-FLT_MAX, -FLT_MAX);
				int32 IndexErrors = Section->ProcIndexBuffer.Num() != Part.Triangles.Num();
				for (int32 k = 0; k < FMath::Min(Section->ProcIndexBuffer.Num(), Part.Triangles.Num()); ++k)
					IndexErrors += Section->ProcIndexBuffer[k] != static_cast<uint32>(Part.Triangles[k]);
				for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
				{
					MinUv.X = FMath::Min(MinUv.X, Vertex.UV0.X); MinUv.Y = FMath::Min(MinUv.Y, Vertex.UV0.Y);
					MaxUv.X = FMath::Max(MaxUv.X, Vertex.UV0.X); MaxUv.Y = FMath::Max(MaxUv.Y, Vertex.UV0.Y);
				}
				double MaxShellOffset = 0.0;
				for (int32 k = 1; k < Part.Comp->GetNumSections(); ++k)
				{
					const FProcMeshSection* Shell = Part.Comp->GetProcMeshSection(k);
					if (!Shell || Shell->ProcVertexBuffer.Num() != View.n_verts) continue;
					for (int32 v = 0; v < View.n_verts; ++v)
						MaxShellOffset = FMath::Max(MaxShellOffset, FVector::Distance(Shell->ProcVertexBuffer[v].Position, Section->ProcVertexBuffer[v].Position));
				}
				UE_LOG(LogTemp, Display, TEXT("OK clothes display part=%s sections=%d indices_mismatch=%d uv=(%.6f,%.6f)-(%.6f,%.6f) shell_offset_cm=%.6f"),
					*Name, Part.Comp->GetNumSections(), IndexErrors, MinUv.X, MinUv.Y, MaxUv.X, MaxUv.Y, MaxShellOffset);
			}
			if (MaxErrorCm > 0.01)
			{
				UE_LOG(LogTemp, Error, TEXT("OK attachment display transformed runtime vertices twice: %s"), *Name);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("OK attachment display audited runtime parts=%d"), AuditedParts);
	}
	ACameraActor* LiveCam = Actor->StudioCam;

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->bAutoManageActiveCameraTarget = false;
		if (LiveCam)
		{
			PC->SetViewTarget(LiveCam);
		}
	}
	const FOkCamRig Rig = OkPoseCameraRig(Actor, GStudioRig);
	const FString OutputDir = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_OUTPUT_DIR"));
	const FString ShotDir = FPaths::ConvertRelativePathToFull(OutputDir.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("Screenshots") : OutputDir);
	IFileManager::Get().MakeDirectory(*ShotDir, true);
	const FString CardTag = FPaths::GetBaseFilename(TaskCard);
	const FString S3 = ShotDir / FString::Printf(TEXT("%s_outfit%d%s_3d.png"), *CardTag, Task.Outfit, *Tag);
	const FString SBodySide = ShotDir / FString::Printf(TEXT("%s_outfit%d%s_3d_side.png"), *CardTag, Task.Outfit, *Tag);
	const FString SBodyBack = ShotDir / FString::Printf(TEXT("%s_outfit%d%s_3d_back.png"), *CardTag, Task.Outfit, *Tag);
	const FString SF = ShotDir / FString::Printf(TEXT("%s_outfit%d%s_face.png"), *CardTag, Task.Outfit, *Tag);
	const FString SS = ShotDir / FString::Printf(TEXT("%s_outfit%d%s_face_side.png"), *CardTag, Task.Outfit, *Tag);
	const bool bFourViews = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_FOUR_VIEWS")) == TEXT("1");
	// Runtime material compilation is asynchronous. A fixed camera timer does
	// not prove that the current actor's shaders have reached the render thread.
	const int32 PendingShaders = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
	const double ReadinessStart = FPlatformTime::Seconds();
	if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
	FlushRenderingCommands();
	// Render-thread completion alone does not wait for submitted RHI uploads.
	// Fixed-pose captures must start after GPU resource work has completed too.
	ENQUEUE_RENDER_COMMAND(OKShotGpuReadiness)([](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.SubmitAndBlockUntilGPUIdle();
	});
	FlushRenderingCommands();
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE")) == TEXT("1"))
	{
		UE_LOG(LogTemp, Display, TEXT("OKCARD_PROFILE readiness task=%d wait_ms=%.1f pending_before=%d"),
			GShotTaskIndex + 1, (FPlatformTime::Seconds() - ReadinessStart) * 1000.0, PendingShaders);
	}
	UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview GPU resource readiness completed=1"));
	UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview material readiness pending_before=%d pending_after=%d"),
		PendingShaders, GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0);
	UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview task %d/%d card=%s variant=%d outfit=%d parts=%d tag=%s"),
		GShotTaskIndex + 1, GShotTasks.Num(), *CardTag, Task.Variant, Task.Outfit, Actor->Parts.Num(), *Tag);

	TWeakObjectPtr<AKKPreviewActor> ActorWeak = Actor;
	FTimerManager& Timers = World->GetTimerManager();
	FTimerHandle HShot3d, HMoveFace, HShotFace, HMoveSide, HShotSide, HNext;
	Timers.SetTimer(HShot3d, FTimerDelegate::CreateLambda([S3, ActorWeak, Rig]()
	{
		if (AKKPreviewActor* A = ActorWeak.Get(); A && A->StudioCam)
		{
			A->StudioCam->SetActorLocationAndRotation(Rig.BodyLoc, Rig.BodyRot);
		}
		RequestCompletedShot(S3, ActorWeak.Get());
		UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview requested %s"), *S3);
	}), 1.5f, false);
	if (bFourViews)
	{
		FTimerHandle HMoveBack, HShotBack;
		Timers.SetTimer(HMoveSide, FTimerDelegate::CreateLambda([ActorWeak, Rig]()
		{
			if (AKKPreviewActor* A = ActorWeak.Get(); A && A->StudioCam)
				A->StudioCam->SetActorLocationAndRotation(Rig.BodySideLoc, Rig.BodySideRot);
		}), 2.8f, false);
		Timers.SetTimer(HShotSide, FTimerDelegate::CreateLambda([SBodySide, ActorWeak, Rig]()
		{
			if (AKKPreviewActor* A = ActorWeak.Get(); A && A->StudioCam)
				A->StudioCam->SetActorLocationAndRotation(Rig.BodySideLoc, Rig.BodySideRot);
			RequestCompletedShot(SBodySide, ActorWeak.Get());
			UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview requested %s"), *SBodySide);
		}), 4.0f, false);
		Timers.SetTimer(HMoveBack, FTimerDelegate::CreateLambda([ActorWeak, Rig]()
		{
			if (AKKPreviewActor* A = ActorWeak.Get(); A && A->StudioCam)
				A->StudioCam->SetActorLocationAndRotation(Rig.BodyBackLoc, Rig.BodyBackRot);
		}), 5.2f, false);
		Timers.SetTimer(HShotBack, FTimerDelegate::CreateLambda([SBodyBack, ActorWeak, Rig]()
		{
			if (AKKPreviewActor* A = ActorWeak.Get(); A && A->StudioCam)
				A->StudioCam->SetActorLocationAndRotation(Rig.BodyBackLoc, Rig.BodyBackRot);
			RequestCompletedShot(SBodyBack, ActorWeak.Get());
			UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview requested %s"), *SBodyBack);
		}), 6.4f, false);
		Timers.SetTimer(HMoveFace, FTimerDelegate::CreateLambda([ActorWeak, Rig]()
		{
			if (AKKPreviewActor* A = ActorWeak.Get(); A && A->StudioCam)
				A->StudioCam->SetActorLocationAndRotation(Rig.FaceLoc, Rig.FaceRot);
		}), 7.6f, false);
		Timers.SetTimer(HShotFace, FTimerDelegate::CreateLambda([SF, ActorWeak, Rig]()
		{
			if (AKKPreviewActor* A = ActorWeak.Get(); A && A->StudioCam)
				A->StudioCam->SetActorLocationAndRotation(Rig.FaceLoc, Rig.FaceRot);
			RequestCompletedShot(SF, ActorWeak.Get());
			UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview requested %s"), *SF);
		}), 8.8f, false);
	}
	else
	{
	Timers.SetTimer(HMoveFace, FTimerDelegate::CreateLambda([ActorWeak, Rig]()
	{
		if (AKKPreviewActor* A = ActorWeak.Get())
		{
			if (A->StudioCam)
			{
				A->StudioCam->SetActorLocationAndRotation(Rig.FaceLoc, Rig.FaceRot);
			}
		}
	}), 2.8f, false);
	Timers.SetTimer(HShotFace, FTimerDelegate::CreateLambda([SF, ActorWeak, Rig]()
	{
		if (AKKPreviewActor* A = ActorWeak.Get(); A && A->StudioCam)
		{
			A->StudioCam->SetActorLocationAndRotation(Rig.FaceLoc, Rig.FaceRot);
		}
		RequestCompletedShot(SF, ActorWeak.Get());
		UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview requested %s"), *SF);
	}), 4.0f, false);
	Timers.SetTimer(HMoveSide, FTimerDelegate::CreateLambda([ActorWeak, Rig]()
	{
		if (AKKPreviewActor* A = ActorWeak.Get())
		{
			if (A->StudioCam)
			{
				A->StudioCam->SetActorLocationAndRotation(Rig.SideLoc, Rig.SideRot);
			}
		}
	}), 5.2f, false);
	Timers.SetTimer(HShotSide, FTimerDelegate::CreateLambda([SS, ActorWeak, Rig]()
	{
		if (AKKPreviewActor* A = ActorWeak.Get(); A && A->StudioCam)
		{
			A->StudioCam->SetActorLocationAndRotation(Rig.SideLoc, Rig.SideRot);
		}
		RequestCompletedShot(SS, ActorWeak.Get());
		UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview requested %s"), *SS);
	}), 6.4f, false);
	}
	Timers.SetTimer(HNext, FTimerDelegate::CreateLambda([Actor, CamActor, World, GameRoot, CardPath]()
	{
		// Run after every requested capture: this exercises dynamics and face time,
		// and must not change the captured pose's simulation history.
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_LAYER_CLOCK_AUDIT")) == TEXT("1") && !Actor->AuditAnimatorElapsedClockDisplay())
		{
			UE_LOG(LogTemp,Error,TEXT("OK Animator elapsed display audit failed"));
			FPlatformMisc::RequestExitWithStatus(false,1);return;
		}
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_TRIGGER_AUDIT")) == TEXT("1") && !Actor->AuditAnimatorTriggerDisplay())
		{ UE_LOG(LogTemp,Error,TEXT("OK Animator trigger display audit failed"));FPlatformMisc::RequestExitWithStatus(false,1);return; }
		if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_CROSSFADE_AUDIT"))==TEXT("1")&&!Actor->AuditAnimatorCrossFadeDisplay(
			FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_GAME_CLIP")),FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_CROSSFADE_CLIP"))))
		{UE_LOG(LogTemp,Error,TEXT("OK Animator CrossFade display audit failed"));FPlatformMisc::RequestExitWithStatus(false,1);return;}

        if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_LAYER_FADE_AUDIT"))==TEXT("1")&&!Actor->AuditAnimatorLayerFadeDisplay(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_GAME_CLIP"))))
        {UE_LOG(LogTemp,Error,TEXT("OK Animator LayerFade display audit failed"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_PARALLEL_FADE_AUDIT"))==TEXT("1")&&!Actor->AuditAnimatorParallelFadeDisplay(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_GAME_CLIP"))))
        {UE_LOG(LogTemp,Error,TEXT("OK Animator ParallelFade display audit failed"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
		++GShotTaskIndex;
		RunShotTask(Actor, CamActor, World, GameRoot, CardPath);
	}), bFourViews ? 10.0f : 7.6f, false);
}

static void ShotPreviewNow()
{
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (!Ctx.World())
			{
				continue;
			}
			if (Ctx.WorldType == EWorldType::Game || Ctx.WorldType == EWorldType::PIE)
			{
				World = Ctx.World();
				break;
			}
			if (!World && Ctx.WorldType == EWorldType::Editor)
			{
				World = Ctx.World();
			}
		}
	}
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("ok.ShotPreview: no world"));
		return;
	}
	GOkShotCaptureActive = true;

	AKKPreviewActor* Actor = World->SpawnActor<AKKPreviewActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Actor)
	{
		UE_LOG(LogTemp, Error, TEXT("ok.ShotPreview: spawn failed"));
		return;
	}
	Actor->bLoadBodyHeadOnly = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_BODY_HEAD_ONLY")) == TEXT("1");
	FString Err;
	const FString GameRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_PORTABLE"))==TEXT("1") ? FString() : TEXT("C:/portable/KoikatuSunshine");
	GShotCards.Reset();
	const FString ShotCardsEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_CARDS"));
	const FString ShotDirEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_DIR"));
	if (!ShotCardsEnv.IsEmpty())
	{
		// Keep the original card paths when shooting a selected set. A copied
		// PNG changes the session cache key and can hide source-path regressions.
		TArray<FString> Paths;
		ShotCardsEnv.ParseIntoArray(Paths, TEXT(";"), true);
		for (FString& Path : Paths)
		{
			Path.TrimStartAndEndInline();
			const FString FullPath = FPaths::ConvertRelativePathToFull(Path);
			if (IFileManager::Get().FileExists(*FullPath))
			{
				GShotCards.Add(FullPath);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("ok.ShotPreview missing card %s"), *FullPath);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview explicit cards=%d"), GShotCards.Num());
	}
	else if (!ShotDirEnv.IsEmpty())
	{
		// Top-level cards only. FindFiles does not recurse, so subfolders stay out.
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(ShotDirEnv / TEXT("*.png")), true, false);
		Names.Sort();
		for (const FString& Name : Names)
		{
			GShotCards.Add(FPaths::ConvertRelativePathToFull(ShotDirEnv / Name));
		}
		UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview dir=%s top-level png=%d"), *ShotDirEnv, GShotCards.Num());
	}
	FString CardPath = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHOT_CARD"));
	if (GShotCards.Num() == 0)
	{
		if (CardPath.IsEmpty())
		{
			CardPath = TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/KK_091207.png");
		}
		GShotCards.Add(CardPath);
	}
	else
	{
		CardPath = GShotCards[0];
	}
	// SetupStudioView chooses the walking pose after the first Session.Open.
	// Establish that same extraction input now so the first open and queued
	// task use one pose and the SQL session key cannot reuse a neutral-pose
	// accessory bake for the walking preview.
	const FString InitialPose = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_POSE"));
	if (InitialPose.IsEmpty() || InitialPose.Equals(TEXT("Stand_D_00"), ESearchCase::IgnoreCase))
	{
#if PLATFORM_WINDOWS
		::SetEnvironmentVariableW(TEXT("OK_POSE"), TEXT("f_aruki_00_00"));
#endif
	}
	FOkSession::InitBindings();
	bool bOk = false;
	const double FirstOpenStart = FPlatformTime::Seconds();
	const bool bFirstOpened = Actor->Session.Open(GameRoot, CardPath, 0, Err);
	const double FirstOpenEnd = FPlatformTime::Seconds();
	if (bFirstOpened)
	{
		bOk = Actor->LoadSession(Err);
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE")) == TEXT("1"))
		{
			UE_LOG(LogTemp, Display, TEXT("OKCARD_PROFILE first_card=%s open_ms=%.1f ue_ms=%.1f success=%d"),
				*FPaths::GetCleanFilename(CardPath), (FirstOpenEnd - FirstOpenStart) * 1000.0,
				(FPlatformTime::Seconds() - FirstOpenEnd) * 1000.0, bOk ? 1 : 0);
		}
		UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview session parts=%d err=%s"), Actor->Parts.Num(), *Err);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ok.ShotPreview session_open failed: %s"), *Err);
	}
	if (!bOk && GShotCards.Num() <= 1)
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Converted/_user_check"));
		if (!Actor->LoadDirectory(Dir, Err))
		{
			UE_LOG(LogTemp, Error, TEXT("ok.ShotPreview load: %s"), *Err);
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview fell back to OBJ %s"), *Dir);
	}
	else if (!bOk)
	{
		UE_LOG(LogTemp, Warning, TEXT("ok.ShotPreview first card failed, continuing queue"));
	}
	Actor->SetupStudioView();
	if (APlayerController* PCLock = World->GetFirstPlayerController())
	{
		PCLock->bAutoManageActiveCameraTarget = false;
		if (Actor->StudioCam)
		{
			PCLock->SetViewTarget(Actor->StudioCam);
		}
	}
	if (GEngine)
	{
		GEngine->Exec(World, TEXT("r.EyeAdaptationQuality 0"));
		GEngine->Exec(World, TEXT("r.DefaultFeature.AutoExposure 0"));
		GEngine->Exec(World, TEXT("r.ExposureOffset 0"));
		GEngine->Exec(World, TEXT("showhud 0"));
		GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
		GEngine->Exec(World, TEXT("r.ScreenPercentage 100"));
		GEngine->Exec(World, TEXT("r.DefaultFeature.Bloom 0"));
		GEngine->Exec(World, TEXT("r.DefaultFeature.MotionBlur 0"));
		GEngine->Exec(World, TEXT("fov 40"));
	}
	if (UGameViewportClient* VP = World->GetGameViewport())
	{
		VP->RemoveAllViewportWidgets();
	}
	else if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveAllViewportWidgets();
	}

	const FVector BodyCamLoc = Actor->StudioLens ? Actor->StudioLens->GetComponentLocation() : FVector(0.f, 320.f, 90.f);
	const FRotator BodyCamRot = Actor->StudioLens ? Actor->StudioLens->GetComponentRotation() : FRotator(-6.f, -90.f, 0.f);
	const FVector FaceCamLoc = BodyCamLoc + FVector(0.f, -180.f, 40.f);
	const FRotator FaceCamRot = BodyCamRot;
	const FVector SideCamLoc = BodyCamLoc + FVector(-160.f, -80.f, 30.f);
	const FRotator SideCamRot = FRotator(-6.f, -20.f, 0.f);
	const FVector CamLoc = BodyCamLoc;
	const FVector Center = Actor->GetActorLocation() + FVector(0.f, 0.f, 80.f);

	ACameraActor* CamActor = Actor->StudioCam;

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->bAutoManageActiveCameraTarget = false;
		if (Actor->StudioCam)
		{
			PC->SetViewTarget(Actor->StudioCam);
		}
		PC->bShowMouseCursor = false;
		if (AHUD* HUD = PC->GetHUD())
		{
			HUD->bShowHUD = false;
			HUD->SetActorHiddenInGame(true);
		}
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		if (APlayerCameraManager* CamMgr = PC->PlayerCameraManager)
		{
			CamMgr->SetFOV(40.f);
		}
	}

	// Flat grey backdrop; the character is unlit so no lights are needed.
	{
		AActor* Backdrop = World->SpawnActor<AActor>();
		if (Backdrop)
		{
			UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(Backdrop);
			Backdrop->SetRootComponent(PMC);
			PMC->RegisterComponent();
			const float D = FMath::Abs(CamLoc.Y - Center.Y) + 200.f;
			const float Half = D * 0.6f + 300.f;
			const FVector BC = FVector(Center.X, Center.Y - 250.f, Center.Z);
			TArray<FVector> V;
			V.Add(BC + FVector(-Half, 0.f, -Half));
			V.Add(BC + FVector(Half, 0.f, -Half));
			V.Add(BC + FVector(Half, 0.f, Half));
			V.Add(BC + FVector(-Half, 0.f, Half));
			TArray<int32> T = { 0, 1, 2, 0, 2, 3 };
			TArray<FVector> N;
			N.Init(FVector(0.f, 1.f, 0.f), 4);
			TArray<FVector2D> UVs2;
			UVs2.Init(FVector2D::ZeroVector, 4);
			TArray<FColor> Cs;
			Cs.Init(FColor::White, 4);
			TArray<FProcMeshTangent> Tang;
			PMC->CreateMeshSection(0, V, T, N, UVs2, Cs, Tang, false);
			PMC->SetMaterial(0, MakeSolidMaterial(FLinearColor(0.28f, 0.28f, 0.28f, 1.0f)));

			// Grey floor to hide the world terrain.
			const float FZ = 2.f;
			const FVector FC = FVector(Center.X, Center.Y, FZ);
			TArray<FVector> FV;
			FV.Add(FC + FVector(-Half, -300.f, 0.f));
			FV.Add(FC + FVector(Half, -300.f, 0.f));
			FV.Add(FC + FVector(Half, 600.f, 0.f));
			FV.Add(FC + FVector(-Half, 600.f, 0.f));
			TArray<FVector> FN;
			FN.Init(FVector(0.f, 0.f, 1.f), 4);
			TArray<FVector2D> FUV;
			FUV.Init(FVector2D::ZeroVector, 4);
			TArray<FColor> FCs;
			FCs.Init(FColor::White, 4);
			UProceduralMeshComponent* Floor = NewObject<UProceduralMeshComponent>(Backdrop);
			Floor->SetupAttachment(PMC);
			Floor->RegisterComponent();
			Floor->CreateMeshSection(0, FV, T, FN, FUV, FCs, Tang, false);
			Floor->SetMaterial(0, MakeSolidMaterial(FLinearColor(0.32f, 0.32f, 0.32f, 1.0f)));

			// Far-side wall so the profile shot does not look into the void.
			const FVector SC = FVector(Center.X + 250.f, Center.Y, Center.Z);
			TArray<FVector> SV;
			SV.Add(SC + FVector(0.f, -Half, -Half));
			SV.Add(SC + FVector(0.f, Half, -Half));
			SV.Add(SC + FVector(0.f, Half, Half));
			SV.Add(SC + FVector(0.f, -Half, Half));
			TArray<FVector> SN;
			SN.Init(FVector(-1.f, 0.f, 0.f), 4);
			UProceduralMeshComponent* SideWall = NewObject<UProceduralMeshComponent>(Backdrop);
			SideWall->SetupAttachment(PMC);
			SideWall->RegisterComponent();
			SideWall->CreateMeshSection(0, SV, T, SN, UVs2, Cs, Tang, false);
			SideWall->SetMaterial(0, MakeSolidMaterial(FLinearColor(0.28f, 0.28f, 0.28f, 1.0f)));
		}
	}
	// Lit parts need lights: soft 3-point rig (key front-top, cool fill left,
	// back light) plus a low ambient skylight captured from the grey backdrop.
	{
		auto AddDir = [World](const FVector& Dir, float Intensity, const FLinearColor& Color, bool bShadow)
		{
			ADirectionalLight* L = World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, Dir.Rotation());
			if (!L)
			{
				return;
			}
			if (UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(L->GetLightComponent()))
			{
				C->SetMobility(EComponentMobility::Movable);
				C->SetIntensity(Intensity);
				C->SetLightColor(Color);
				C->SetCastShadows(bShadow);
			}
		};
		AddDir(FVector(0.25f, -0.85f, -0.45f), 2.5f, FLinearColor(1.0f, 0.97f, 0.93f), false);
		AddDir(FVector(-0.75f, -0.35f, -0.10f), 1.5f, FLinearColor(0.90f, 0.93f, 1.0f), false);
		AddDir(FVector(0.0f, 0.90f, -0.30f), 1.2f, FLinearColor(0.95f, 0.95f, 1.0f), false);
		if (ASkyLight* Sky = World->SpawnActor<ASkyLight>())
		{
			if (USkyLightComponent* SC = Sky->GetLightComponent())
			{
				SC->SetMobility(EComponentMobility::Movable);
				SC->SourceType = ESkyLightSourceType::SLS_CapturedScene;
				SC->bLowerHemisphereIsBlack = false;
				SC->SetIntensity(3.5f);
				SC->SetLightColor(FLinearColor(0.80f, 0.82f, 0.88f));
				SC->RecaptureSky();
			}
		}
	}
	if (APostProcessVolume* Ppv = World->SpawnActor<APostProcessVolume>(Center, FRotator::ZeroRotator))
	{
		Ppv->bUnbound = true;
		Ppv->Priority = 100.f;
		Ppv->BlendWeight = 1.f;
		Ppv->Settings.bOverride_AutoExposureMethod = true;
		Ppv->Settings.AutoExposureMethod = AEM_Manual;
		Ppv->Settings.bOverride_AutoExposureBias = true;
		Ppv->Settings.AutoExposureBias = 0.0f;
		Ppv->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		Ppv->Settings.AutoExposureApplyPhysicalCameraExposure = false;
	}

	// One process walks every queued card. A directory shoot is outfit 0 only
	// unless OK_OUTFITS_LIMIT asks for more; a single card still uses its
	// coordinate count.
	int32 NumOutfits = ShotDirEnv.IsEmpty() ? Actor->Session.CoordinateCount() : 1;
	if (NumOutfits <= 0)
	{
		NumOutfits = ShotDirEnv.IsEmpty() ? 4 : 1;
	}
	if (const FString Lim = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_OUTFITS_LIMIT")); !Lim.IsEmpty())
	{
		const int32 L = FCString::Atoi(*Lim);
		if (L > 0)
		{
			NumOutfits = FMath::Min(NumOutfits, L);
		}
	}
	BuildShotTasks(NumOutfits);
	UE_LOG(LogTemp, Display, TEXT("ok.ShotPreview rendering %d task(s) (%d card(s) x %d variant(s) x %d outfit(s))"),
		GShotTasks.Num(), GShotCards.Num(), GShotVariantTag.Num(), NumOutfits);
	RunShotTask(Actor, CamActor, World, GameRoot, CardPath);
}

static void LogVerifyMetric(AKKPreviewActor* Actor, const TCHAR* Tag)
{
	UPoseableMeshComponent* Comp = Actor && Actor->SkelFollowers.Num() ? Actor->SkelFollowers[0] : nullptr;
	FVector Hip = FVector::ZeroVector;
	FVector LegL = FVector::ZeroVector;
	FVector LegR = FVector::ZeroVector;
	float TopZ = 0.f;
	if (Comp)
	{
		Hip = Comp->GetBoneLocation(TEXT("cf_j_hips"), EBoneSpaces::ComponentSpace);
		LegL = Comp->GetBoneLocation(TEXT("cf_j_leg03_L"), EBoneSpaces::ComponentSpace);
		LegR = Comp->GetBoneLocation(TEXT("cf_j_leg03_R"), EBoneSpaces::ComponentSpace);
		const FVector HandL = Comp->GetBoneLocation(TEXT("cf_j_hand_L"), EBoneSpaces::ComponentSpace);
		const FVector HandR = Comp->GetBoneLocation(TEXT("cf_j_hand_R"), EBoneSpaces::ComponentSpace);
		UE_LOG(LogTemp, Display, TEXT("OK verify hands %s L=%s R=%s"), Tag, *HandL.ToString(), *HandR.ToString());
		TopZ = Comp->Bounds.GetBox().Max.Z;
	}
	UE_LOG(LogTemp, Display, TEXT("OK verify %s followers=%d mouth=%d hip=%s legL=%s legR=%s topZ=%.1f"),
		Tag,
		Actor ? Actor->SkelFollowers.Num() : 0,
		Actor ? Actor->MouthSlots.Num() : 0,
		*Hip.ToString(), *LegL.ToString(), *LegR.ToString(), TopZ);
}

static void VerifyDemoNow()
{
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.World() && (Ctx.WorldType == EWorldType::Game || Ctx.WorldType == EWorldType::PIE))
			{
				World = Ctx.World();
				break;
			}
		}
	}
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("ok.VerifyDemo: no world"));
		return;
	}
	AKKPreviewActor* Actor = World->SpawnActor<AKKPreviewActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Actor)
	{
		UE_LOG(LogTemp, Error, TEXT("ok.VerifyDemo: spawn failed"));
		return;
	}
	FString Err;
	const FString GameRoot = TEXT("C:/portable/KoikatuSunshine");
	const FString CardPath = TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/KK_091207.png");
	FOkSession::InitBindings();
	if (!Actor->Session.Open(GameRoot, CardPath, 0, Err) || !Actor->LoadSession(Err))
	{
		UE_LOG(LogTemp, Error, TEXT("ok.VerifyDemo load failed: %s"), *Err);
		FPlatformMisc::RequestExit(false);
		return;
	}
	Actor->SetupStudioView();

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->bAutoManageActiveCameraTarget = false;
		if (Actor->StudioCam)
		{
			PC->SetViewTarget(Actor->StudioCam);
		}
	}
	const FString ShotDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots"));
	IFileManager::Get().MakeDirectory(*ShotDir, true);
	TWeakObjectPtr<AKKPreviewActor> Weak = Actor;
	FTimerManager& Timers = World->GetTimerManager();
	FTimerHandle H1, H2, H3, H4, H5, H6, H7;
	Timers.SetTimer(H1, FTimerDelegate::CreateLambda([Weak, ShotDir]()
	{
		if (AKKPreviewActor* A = Weak.Get())
		{
			A->ApplyShapes(A->ShapeFace, A->ShapeBody);
			LogVerifyMetric(A, TEXT("tpose"));
		}
		FScreenshotRequest::RequestScreenshot(ShotDir / TEXT("verify_tpose.png"), false, false);
	}), 1.2f, false);
	Timers.SetTimer(H2, FTimerDelegate::CreateLambda([Weak]()
	{
		if (AKKPreviewActor* A = Weak.Get())
		{
			TArray<float> Body = A->ShapeBody;
			if (Body.Num() < 1)
			{
				Body.SetNum(44);
			}
			Body[0] = 0.f;
			A->ApplyShapes(A->ShapeFace, Body);
			LogVerifyMetric(A, TEXT("short"));
		}
	}), 2.4f, false);
	Timers.SetTimer(H3, FTimerDelegate::CreateLambda([ShotDir]()
	{
		FScreenshotRequest::RequestScreenshot(ShotDir / TEXT("verify_short.png"), false, false);
	}), 3.2f, false);
	Timers.SetTimer(H4, FTimerDelegate::CreateLambda([Weak]()
	{
		if (AKKPreviewActor* A = Weak.Get())
		{
			TArray<float> Body = A->ShapeBody;
			if (Body.Num() < 1)
			{
				Body.SetNum(44);
			}
			Body[0] = 1.f;
			A->ApplyShapes(A->ShapeFace, Body);
			LogVerifyMetric(A, TEXT("tall"));
		}
	}), 4.4f, false);
	Timers.SetTimer(H5, FTimerDelegate::CreateLambda([ShotDir]()
	{
		FScreenshotRequest::RequestScreenshot(ShotDir / TEXT("verify_tall.png"), false, false);
	}), 5.2f, false);
	Timers.SetTimer(H6, FTimerDelegate::CreateLambda([Weak]()
	{
		if (AKKPreviewActor* A = Weak.Get())
		{
			A->SetPoseMode(1);
			A->AnimPhase = 0.25f;
			A->ApplyPoseFrame(0.f);
			A->PoseMode = 0;
			A->bAnimLoop = false;
			if (A->StudioCam)
			{
				// Front view hides forward/back swing. Match the body camera distance
				// from a three-quarter angle so both arms and the stride stay in frame.
				const FVector Look = GStudioRig.BodyCenter;
				const float Dist = FMath::Max(720.f, (GStudioRig.BodyLoc - GStudioRig.BodyCenter).Size());
				const FVector Cam = Look + FVector(Dist * 0.72f, Dist * 0.55f, 40.f);
				A->StudioCam->SetActorLocationAndRotation(Cam, (Look - Cam).Rotation());
			}
			LogVerifyMetric(A, TEXT("walk"));
		}
	}), 6.4f, false);
	Timers.SetTimer(H7, FTimerDelegate::CreateLambda([ShotDir]()
	{
		FScreenshotRequest::RequestScreenshot(ShotDir / TEXT("verify_walk.png"), false, false);
		UE_LOG(LogTemp, Display, TEXT("ok.VerifyDemo shots requested"));
	}), 7.2f, false);
	FTimerHandle HQuit;
	Timers.SetTimer(HQuit, FTimerDelegate::CreateLambda([]()
	{
		FPlatformMisc::RequestExit(false);
	}), 9.5f, false);
}

static FAutoConsoleCommand VerifyDemoCmd(
	TEXT("ok.VerifyDemo"),
	TEXT("Load card, screenshot T-pose, short, tall, Unreal walk, then quit"),
	FConsoleCommandDelegate::CreateStatic(&VerifyDemoNow));

static FAutoConsoleCommand ShotPreviewCmd(
	TEXT("ok.ShotPreview"),
	TEXT("Load card via Rust session, capture user_check_3d.png, user_check_face.png, user_check_face_side.png, quit"),
	FConsoleCommandDelegate::CreateStatic(&ShotPreviewNow));

bool AKKPreviewActor::SetUserFk(const TArray<FString>& Bones, const TArray<FVector>& UnityEuler)
{
	if (!Session.IsOpen() || !Session.SetFk(Bones, UnityEuler)) return false;
	// Animated modes re-pose every frame; a static pose is recomputed on request.
	bUserPoseDirty = true;
	ApplyPoseFrame(0.f, false);
	return true;
}

bool AKKPreviewActor::SetStudioIk(const TArray<int32>& Effectors, const TArray<FVector>& UnityPositions, FString& OutError, const TArray<FQuat>& UnityRotations, const TArray<uint8>& UseRotations)
{
	if (!Session.IsOpen()) { OutError = TEXT("no session"); return false; }
	if (!bStudioIkReady && Effectors.Num())
	{
		// The solver is built from the shaped body, which the first pose frame makes: pose once and retry.
		if (!Session.PrepareStudioIk(OutError))
		{
			ApplyPoseFrame(0.f, false);
			if (!Session.PrepareStudioIk(OutError)) return false;
		}
		bStudioIkReady = true;
	}
	const bool bApplied = UnityRotations.IsEmpty() && UseRotations.IsEmpty() ? Session.SetStudioIk(Effectors, UnityPositions) : Session.SetStudioIkPose(Effectors, UnityPositions, UnityRotations, UseRotations);
    if (!bApplied) { OutError = TEXT("bad IK targets"); return false; }
	bUserPoseDirty = true;
	ApplyPoseFrame(0.f, false);
	return true;
}
