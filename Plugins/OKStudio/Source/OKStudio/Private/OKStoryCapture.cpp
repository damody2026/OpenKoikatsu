#include "OKStoryCapture.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
#include "TextureResource.h"

static bool OKStoryCaptureVector(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, FVector& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (!O || !O->TryGetArrayField(Field, A) || A->Num() < 3) return false;
	Out = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
	return true;
}

static bool OKStoryCaptureQuat(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, FQuat& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (!O || !O->TryGetArrayField(Field, A) || A->Num() < 4) return false;
	Out = FQuat((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber(), (*A)[3]->AsNumber()).GetNormalized();
	return true;
}

// The live portrait's picture: the colour capture, covered where the speaker's depth is (the rest transparent).
#if WITH_EDITOR
UMaterial* OKStoryBuildPortraitMaterial(UObject* Outer, FName Name, EObjectFlags Flags, UTexture* Color, UTexture* Depth)
{
	UMaterial* Mat = NewObject<UMaterial>(Outer, Name, Flags);
	Mat->MaterialDomain = MD_UI;
	Mat->BlendMode = BLEND_Translucent;
	auto Sampler = [&](const TCHAR* Name, UTexture* Default)
	{
		UMaterialExpressionTextureSampleParameter2D* S = NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat);
		S->Material = Mat;
		S->ParameterName = Name;
		S->SamplerType = MaterialExpressionUtils::GetSamplerTypeForTexture(Default);
		S->Texture = Default;
		Mat->GetExpressionCollection().AddExpression(S);
		return S;
	};
	auto Scalar = [&](const TCHAR* Name, float Value)
	{
		UMaterialExpressionScalarParameter* S = NewObject<UMaterialExpressionScalarParameter>(Mat);
		S->Material = Mat;
		S->ParameterName = Name;
		S->DefaultValue = Value;
		Mat->GetExpressionCollection().AddExpression(S);
		return S;
	};
	UMaterialExpressionTextureSampleParameter2D* ColorSample = Sampler(TEXT("Color"), Color);
	UMaterialExpressionTextureSampleParameter2D* DepthSample = Sampler(TEXT("Depth"), Depth);
	UMaterialExpressionScalarParameter* Alpha = Scalar(TEXT("Alpha"), 1.f);
	UMaterialExpressionScalarParameter* Gamma = Scalar(TEXT("Gamma"), 2.2f);
	UMaterialExpressionCustom* Custom = NewObject<UMaterialExpressionCustom>(Mat);
	Custom->Material = Mat;
	Custom->OutputType = CMOT_Float4;
	// The colour capture holds display-encoded bytes (FinalColorLDR into a linear target): back to linear for the UI.
	// Coverage: the HDR capture's alpha is the inverse opacity of what it drew (1 where nothing is).
	Custom->Code = TEXT("float Covered = saturate(1 - D.a);\nreturn float4(pow(saturate(C.rgb), G), Covered * A);");
	Custom->Inputs.Reset();
	auto Input = [&](const TCHAR* Name, UMaterialExpression* E)
	{
		FCustomInput In;
		In.InputName = Name;
		In.Input.Expression = E;
		// A texture sample's output 5 is its RGBA (output 0 would be RGB only).
		if (E->IsA<UMaterialExpressionTextureSampleParameter2D>()) In.Input.OutputIndex = 5;
		Custom->Inputs.Add(In);
	};
	Input(TEXT("C"), ColorSample);
	Input(TEXT("D"), DepthSample);
	Input(TEXT("A"), Alpha);
	Input(TEXT("G"), Gamma);
	Mat->GetExpressionCollection().AddExpression(Custom);
	auto Mask = [&](bool R, bool G, bool B, bool A)
	{
		UMaterialExpressionComponentMask* M = NewObject<UMaterialExpressionComponentMask>(Mat);
		M->Material = Mat;
		M->Input.Expression = Custom;
		M->R = R; M->G = G; M->B = B; M->A = A;
		Mat->GetExpressionCollection().AddExpression(M);
		return M;
	};
	if (UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData())
	{
		Ed->EmissiveColor.Expression = Mask(true, true, true, false);
		Ed->Opacity.Expression = Mask(false, false, false, true);
	}
	Mat->UpdateCachedExpressionData();
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	return Mat;
}
#endif

// The saved material (OKStorySavePortrait commandlet); in the editor without it, one built in memory.
static UMaterialInterface* OKStoryPortraitMaterial(UTexture* Color, UTexture* Depth)
{
	static TStrongObjectPtr<UMaterialInterface> Cached;
	if (Cached) return Cached.Get();
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, OK_STORY_PORTRAIT_MATERIAL, nullptr, LOAD_NoWarn | LOAD_Quiet);
#if WITH_EDITOR
	if (!Mat) Mat = OKStoryBuildPortraitMaterial(GetTransientPackage(), NAME_None, RF_Transient, Color, Depth);
#endif
	if (!Mat)
	{
		UE_LOG(LogTemp, Warning, TEXT("Story portrait material %s missing: no portraits (run the OKStorySavePortrait commandlet)"), OK_STORY_PORTRAIT_MATERIAL);
		return nullptr;
	}
	Cached.Reset(Mat);
	return Mat;
}

void FOKStoryPortraitCapture::Update(UObject* Outer, UWorld* World, const TSharedPtr<FJsonObject>& Portrait, TFunctionRef<void(USceneCaptureComponent2D*)> ShowOnly,
	const FPostProcessSettings* Post)
{
	if (!Outer || !World || !Portrait) return;
	const TArray<TSharedPtr<FJsonValue>>* Size = nullptr;
	int32 W = 768, H = 1152;
	if (Portrait->TryGetArrayField(TEXT("target"), Size) && Size->Num() == 2) { W = FMath::Clamp(int32((*Size)[0]->AsNumber()), 16, 4096); H = FMath::Clamp(int32((*Size)[1]->AsNumber()), 16, 4096); }
	auto MakeTarget = [&](TStrongObjectPtr<UTextureRenderTarget2D>& T, ETextureRenderTargetFormat Format, bool bEncoded, int32 TW, int32 TH)
	{
		if (T && T->SizeX == TW && T->SizeY == TH) return;
		if (!T)
		{
			T.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage()));
			T->RenderTargetFormat = Format;
			T->bForceLinearGamma = true;
			T->TargetGamma = bEncoded ? 2.2f : 0.f;
			T->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
		}
		T->InitAutoFormat(TW, TH);
		T->UpdateResourceImmediate(true);
	};
	for (int32 B = 0; B < 2; ++B)
	{
		MakeTarget(ColorTarget[B], RTF_RGBA8, true, W, H);
		// The coverage only shapes the edge: half the resolution, a quarter of the pixels.
		MakeTarget(CoverageTarget[B], RTF_RGBA16f, false, FMath::Max(16, W / 2), FMath::Max(16, H / 2));
	}
	const int32 Back = 1 - Front;
	auto MakeCapture = [&](TStrongObjectPtr<USceneCaptureComponent2D>& C, ESceneCaptureSource Source, UTextureRenderTarget2D* T)
	{
		if (!C || !C->IsRegistered())
		{
			C.Reset(NewObject<USceneCaptureComponent2D>(Outer));
			C->RegisterComponentWithWorld(World);
			C->bCaptureEveryFrame = false;
			C->bCaptureOnMovement = false;
			C->bAlwaysPersistRenderingState = true;
			C->ProjectionType = ECameraProjectionMode::Orthographic;
			C->CaptureSource = Source;
			C->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			// One character against nothing: the passes that gather the rest of the scene (global illumination,
			// reflections, ambient occlusion, volumetrics) cost a full view each and add nothing to a toon portrait.
			FEngineShowFlags& Show = C->ShowFlags;
			Show.SetAtmosphere(false); Show.SetFog(false); Show.SetVolumetricFog(false); Show.SetCloud(false);
			Show.SetTemporalAA(false); Show.SetMotionBlur(false);
			Show.SetGlobalIllumination(false); Show.SetLumenGlobalIllumination(false); Show.SetLumenReflections(false);
			Show.SetScreenSpaceReflections(false); Show.SetReflectionEnvironment(false);
			Show.SetAmbientOcclusion(false); Show.SetDistanceFieldAO(false); Show.SetContactShadows(false);
			Show.SetCapsuleShadows(false); Show.SetLightShafts(false); Show.SetParticles(false); Show.SetDecals(false);
			C->bUseRayTracingIfEnabled = false;
			if (Source == SCS_SceneColorHDR)
			{
				// The coverage capture only reads the scene alpha (what was drawn): no light, shadow or post at all.
				Show.SetLighting(false); Show.SetDynamicShadows(false); Show.SetSkyLighting(false);
				Show.SetPostProcessing(false); Show.SetBloom(false);
				// Unlit anyway: drawn as passes of the main renderer, not a second scene renderer every other frame.
				C->bRenderInMainRenderer = true;
			}
		}
		C->TextureTarget = T;
	};
	// Both captures draw into the back picture.
	MakeCapture(Color, SCS_FinalColorLDR, ColorTarget[Back].Get());
	MakeCapture(Coverage, SCS_SceneColorHDR, CoverageTarget[Back].Get());
	if (!Mid)
	{
		if (UMaterialInterface* M = OKStoryPortraitMaterial(ColorTarget[Front].Get(), CoverageTarget[Front].Get()))
			Mid.Reset(UMaterialInstanceDynamic::Create(M, GetTransientPackage()));
	}
	auto Show = [&]()
	{
		if (!Mid) return;
		Mid->SetTextureParameterValue(TEXT("Color"), ColorTarget[Front].Get());
		Mid->SetTextureParameterValue(TEXT("Depth"), CoverageTarget[Front].Get());
	};
	if (Mid)
	{
		// Nothing complete yet: nothing to see (a target's clear colour is fully transparent).
		Mid->SetScalarParameterValue(TEXT("Alpha"), bShown ? float(Portrait->GetNumberField(TEXT("alpha"))) : 0.f);
		Show();
	}
	// The portrait does not depend on the main view, so its two captures are spread over frames: the colour on the
	// frame ok-story asks for, its coverage (same framing) on the next. Each frame renders at most one extra view, and
	// between captures the view keeps drawing the last picture.
	auto Render = [&](USceneCaptureComponent2D* C, const FVector& P, const FQuat& R, float Width)
	{
		C->SetWorldLocationAndRotation(P, R);
		C->OrthoWidth = Width;
		ShowOnly(C);
		if (Post && C == Color.Get()) { C->PostProcessSettings = *Post; C->PostProcessBlendWeight = 1.f; }
		C->CaptureScene();
	};
	if (bCoveragePending)
	{
		bCoveragePending = false;
		Render(Coverage.Get(), PendingPos, PendingRot, PendingWidth);
		// The back picture is whole: the view shows it from now on (both captures are queued before the view's draw).
		Front = Back;
		bShown = true;
		Show();
		if (Mid) Mid->SetScalarParameterValue(TEXT("Alpha"), float(Portrait->GetNumberField(TEXT("alpha"))));
		return;
	}
	bool bCapture = false;
	Portrait->TryGetBoolField(TEXT("capture"), bCapture);
	FVector Pos;
	FQuat Rot;
	if (!bCapture || !OKStoryCaptureVector(Portrait, TEXT("pos"), Pos) || !OKStoryCaptureQuat(Portrait, TEXT("rot"), Rot)) return;
	const float Width = float(Portrait->GetNumberField(TEXT("ortho_width")));
	Render(Color.Get(), Pos, Rot, Width);
	bCoveragePending = true;
	PendingPos = Pos;
	PendingRot = Rot;
	PendingWidth = Width;
}

void FOKStoryPortraitCapture::Release()
{
	for (TStrongObjectPtr<USceneCaptureComponent2D>* C : { &Color, &Coverage })
	{
		if (C->IsValid()) (*C)->DestroyComponent();
		C->Reset();
	}
	for (int32 B = 0; B < 2; ++B)
	{
		ColorTarget[B].Reset();
		CoverageTarget[B].Reset();
	}
	Front = 0;
	bShown = false;
	bCoveragePending = false;
	Mid.Reset();
}

struct FOKStoryProbeReadbacks
{
	struct FFlight { uint64 Id = 0; TUniquePtr<FRHIGPUTextureReadback> Readback; };
	TArray<FFlight> Flights; // render thread only
	FCriticalSection Lock;
	TArray<TPair<uint64, double>> Answers;
};

namespace
{
	constexpr int32 OKStoryProbeSize = 8;
	// Depth renders a frame at most: a shot's dozen candidates are spread over a few frames (ok-story waits for them).
	constexpr int32 OKStoryProbesPerFrame = 3;

	// Render thread: the readbacks that have arrived become answers (the nearest depth of the 8x8 view).
	void OKStoryCollectProbes(FOKStoryProbeReadbacks& R)
	{
		for (int32 I = R.Flights.Num() - 1; I >= 0; --I)
		{
			FOKStoryProbeReadbacks::FFlight& F = R.Flights[I];
			if (!F.Readback->IsReady()) continue;
			double Clear = 1e9;
			int32 Pitch = 0, Rows = 0;
			if (const float* Data = static_cast<const float*>(F.Readback->Lock(Pitch, &Rows)))
			{
				for (int32 Y = 0; Y < FMath::Min(Rows > 0 ? Rows : OKStoryProbeSize, OKStoryProbeSize); ++Y)
					for (int32 X = 0; X < OKStoryProbeSize; ++X)
					{
						const float D = Data[Y * Pitch + X];
						if (D > 0.f && D < 1e6f) Clear = FMath::Min(Clear, double(D));
					}
				F.Readback->Unlock();
			}
			{
				FScopeLock Guard(&R.Lock);
				R.Answers.Add({ F.Id, Clear });
			}
			R.Flights.RemoveAtSwap(I);
		}
	}
}

void FOKStoryProbes::Run(UObject* Outer, UWorld* World, const TArray<TSharedPtr<FJsonValue>>& Probes, TFunctionRef<void(USceneCaptureComponent2D*)> Hide)
{
	if (!Outer || !World) return;
	for (const TSharedPtr<FJsonValue>& V : Probes)
	{
		const TSharedPtr<FJsonObject> P = V ? V->AsObject() : nullptr;
		FProbeRequest Q;
		if (!P || !OKStoryCaptureVector(P, TEXT("from"), Q.From) || !OKStoryCaptureVector(P, TEXT("to"), Q.To)) continue;
		Q.Id = uint64(P->GetNumberField(TEXT("id")));
		Queue.Add(Q);
	}
	if (!Readbacks) Readbacks = MakeShared<FOKStoryProbeReadbacks, ESPMode::ThreadSafe>();
	// Arrived readbacks turn into answers on the render thread, in order after the copies queued earlier.
	{
		TSharedPtr<FOKStoryProbeReadbacks, ESPMode::ThreadSafe> R = Readbacks;
		ENQUEUE_RENDER_COMMAND(OKStoryCollectProbes)([R](FRHICommandListImmediate&) { OKStoryCollectProbes(*R); });
	}
	if (Queue.IsEmpty()) return;
	if (!Target)
	{
		Target.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage()));
		Target->RenderTargetFormat = RTF_R32f;
		Target->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
		Target->InitAutoFormat(OKStoryProbeSize, OKStoryProbeSize);
		Target->UpdateResourceImmediate(true);
	}
	if (!Capture || !Capture->IsRegistered())
	{
		Capture.Reset(NewObject<USceneCaptureComponent2D>(Outer));
		Capture->RegisterComponentWithWorld(World);
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->CaptureSource = SCS_SceneDepth;
		Capture->FOVAngle = 4.f;
		Capture->TextureTarget = Target.Get();
		// Depth only: no light, shadow, reflection or post.
		FEngineShowFlags& Show = Capture->ShowFlags;
		Show.SetPostProcessing(false); Show.SetAtmosphere(false); Show.SetFog(false); Show.SetVolumetricFog(false);
		Show.SetLighting(false); Show.SetDynamicShadows(false); Show.SetGlobalIllumination(false);
		Show.SetLumenGlobalIllumination(false); Show.SetLumenReflections(false); Show.SetScreenSpaceReflections(false);
		Show.SetAmbientOcclusion(false); Show.SetDistanceFieldAO(false); Show.SetTranslucency(false); Show.SetParticles(false);
		Capture->bUseRayTracingIfEnabled = false;
	}
	Capture->HiddenActors.Reset();
	Capture->HiddenComponents.Reset();
	Hide(Capture.Get());
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	for (int32 N = 0; N < OKStoryProbesPerFrame && !Queue.IsEmpty(); ++N)
	{
		const FProbeRequest Q = Queue[0];
		Queue.RemoveAt(0);
		Capture->SetWorldLocationAndRotation(Q.From, (Q.To - Q.From).Rotation());
		Capture->CaptureScene();
		// Copied out right after its depth is drawn (commands run in order, so one target serves every probe).
		TSharedPtr<FOKStoryProbeReadbacks, ESPMode::ThreadSafe> R = Readbacks;
		const uint64 Id = Q.Id;
		ENQUEUE_RENDER_COMMAND(OKStoryProbeCopy)([R, Resource, Id](FRHICommandListImmediate& RHICmdList)
		{
			FOKStoryProbeReadbacks::FFlight F;
			F.Id = Id;
			F.Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("OKStoryProbe"));
			F.Readback->EnqueueCopy(RHICmdList, Resource->GetRenderTargetTexture());
			R->Flights.Add(MoveTemp(F));
		});
	}
}

TArray<TSharedPtr<FJsonValue>> FOKStoryProbes::TakeAnswers()
{
	TArray<TSharedPtr<FJsonValue>> Out;
	TArray<TPair<uint64, double>> Answers;
	if (Readbacks)
	{
		FScopeLock Guard(&Readbacks->Lock);
		Answers = MoveTemp(Readbacks->Answers);
		Readbacks->Answers.Reset();
	}
	for (const TPair<uint64, double>& A : Answers)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetNumberField(TEXT("id"), double(A.Key));
		P->SetNumberField(TEXT("clear"), A.Value);
		Out.Add(MakeShared<FJsonValueObject>(P));
	}
	return Out;
}

void FOKStoryProbes::Release()
{
	if (Capture) Capture->DestroyComponent();
	Capture.Reset();
	// In-flight copies keep their own readbacks (the shared state lives until the render thread lets go of it); the
	// target's resource is released after them, render commands running in order.
	Target.Reset();
	Queue.Reset();
	if (Readbacks)
	{
		// The readbacks still in flight are let go on the render thread, which owns them.
		TSharedPtr<FOKStoryProbeReadbacks, ESPMode::ThreadSafe> R = MoveTemp(Readbacks);
		ENQUEUE_RENDER_COMMAND(OKStoryReleaseProbes)([R](FRHICommandListImmediate&) { R->Flights.Reset(); });
	}
}
