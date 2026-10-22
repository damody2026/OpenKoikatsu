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
static UMaterialInterface* OKStoryPortraitMaterial(UTexture* Color, UTexture* Depth)
{
#if WITH_EDITOR
	static TStrongObjectPtr<UMaterial> Cached;
	if (Cached) return Cached.Get();
	UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
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
	Cached.Reset(Mat);
	return Mat;
#else
	return nullptr; // Editor graph creation is not a runtime rendering path: the view shows the colour capture alone.
#endif
}

void FOKStoryPortraitCapture::Update(UObject* Outer, UWorld* World, const TSharedPtr<FJsonObject>& Portrait, TFunctionRef<void(USceneCaptureComponent2D*)> ShowOnly,
	const FPostProcessSettings* Post)
{
	if (!Outer || !World || !Portrait) return;
	const TArray<TSharedPtr<FJsonValue>>* Size = nullptr;
	int32 W = 768, H = 1152;
	if (Portrait->TryGetArrayField(TEXT("target"), Size) && Size->Num() == 2) { W = FMath::Clamp(int32((*Size)[0]->AsNumber()), 16, 4096); H = FMath::Clamp(int32((*Size)[1]->AsNumber()), 16, 4096); }
	auto MakeTarget = [&](TStrongObjectPtr<UTextureRenderTarget2D>& T, ETextureRenderTargetFormat Format, bool bEncoded)
	{
		if (T && T->SizeX == W && T->SizeY == H) return;
		if (!T)
		{
			T.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage()));
			T->RenderTargetFormat = Format;
			T->bForceLinearGamma = true;
			T->TargetGamma = bEncoded ? 2.2f : 0.f;
			T->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
		}
		T->InitAutoFormat(W, H);
		T->UpdateResourceImmediate(true);
	};
	MakeTarget(ColorTarget, RTF_RGBA8, true);
	MakeTarget(CoverageTarget, RTF_RGBA16f, false);
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
			C->ShowFlags.SetAtmosphere(false);
			C->ShowFlags.SetFog(false);
			C->ShowFlags.SetTemporalAA(false);
			C->ShowFlags.SetMotionBlur(false);
		}
		C->TextureTarget = T;
	};
	MakeCapture(Color, SCS_FinalColorLDR, ColorTarget.Get());
	MakeCapture(Coverage, SCS_SceneColorHDR, CoverageTarget.Get());
	if (!Mid)
	{
		if (UMaterialInterface* M = OKStoryPortraitMaterial(ColorTarget.Get(), CoverageTarget.Get()))
			Mid.Reset(UMaterialInstanceDynamic::Create(M, GetTransientPackage()));
	}
	if (Mid)
	{
		Mid->SetTextureParameterValue(TEXT("Color"), ColorTarget.Get());
		Mid->SetTextureParameterValue(TEXT("Depth"), CoverageTarget.Get());
		Mid->SetScalarParameterValue(TEXT("Alpha"), float(Portrait->GetNumberField(TEXT("alpha"))));
	}
	bool bCapture = false;
	Portrait->TryGetBoolField(TEXT("capture"), bCapture);
	FVector Pos;
	FQuat Rot;
	if (!bCapture || !OKStoryCaptureVector(Portrait, TEXT("pos"), Pos) || !OKStoryCaptureQuat(Portrait, TEXT("rot"), Rot)) return;
	const float Width = float(Portrait->GetNumberField(TEXT("ortho_width")));
	for (USceneCaptureComponent2D* C : { Color.Get(), Coverage.Get() })
	{
		C->SetWorldLocationAndRotation(Pos, Rot);
		C->OrthoWidth = Width;
		ShowOnly(C);
		if (Post) { C->PostProcessSettings = *Post; C->PostProcessBlendWeight = 1.f; }
		C->CaptureScene();
	}
}

void FOKStoryPortraitCapture::Release()
{
	for (TStrongObjectPtr<USceneCaptureComponent2D>* C : { &Color, &Coverage })
	{
		if (C->IsValid()) (*C)->DestroyComponent();
		C->Reset();
	}
	ColorTarget.Reset();
	CoverageTarget.Reset();
	Mid.Reset();
}

void FOKStoryProbes::Run(UObject* Outer, UWorld* World, const TArray<TSharedPtr<FJsonValue>>& Probes, TFunctionRef<void(USceneCaptureComponent2D*)> Hide)
{
	if (!Outer || !World || Probes.IsEmpty()) return;
	constexpr int32 Size = 8;
	if (!Target)
	{
		Target.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage()));
		Target->RenderTargetFormat = RTF_R32f;
		Target->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
		Target->InitAutoFormat(Size, Size);
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
		Capture->ShowFlags.SetPostProcessing(false);
		Capture->ShowFlags.SetAtmosphere(false);
		Capture->ShowFlags.SetFog(false);
	}
	Capture->HiddenActors.Reset();
	Capture->HiddenComponents.Reset();
	Hide(Capture.Get());
	for (const TSharedPtr<FJsonValue>& V : Probes)
	{
		const TSharedPtr<FJsonObject> P = V->AsObject();
		FVector From, To;
		if (!P || !OKStoryCaptureVector(P, TEXT("from"), From) || !OKStoryCaptureVector(P, TEXT("to"), To)) continue;
		const uint64 Id = uint64(P->GetNumberField(TEXT("id")));
		Capture->SetWorldLocationAndRotation(From, (To - From).Rotation());
		Capture->CaptureScene();
		TArray<FLinearColor> Depth;
		double Clear = 1e9;
		if (Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Depth))
			for (const FLinearColor& D : Depth)
				if (D.R > 0.f && D.R < 1e6f) Clear = FMath::Min(Clear, double(D.R));
		Answers.Add({ Id, Clear });
	}
}

TArray<TSharedPtr<FJsonValue>> FOKStoryProbes::TakeAnswers()
{
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const TPair<uint64, double>& A : Answers)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetNumberField(TEXT("id"), double(A.Key));
		P->SetNumberField(TEXT("clear"), A.Value);
		Out.Add(MakeShared<FJsonValueObject>(P));
	}
	Answers.Reset();
	return Out;
}

void FOKStoryProbes::Release()
{
	if (Capture) Capture->DestroyComponent();
	Capture.Reset();
	Target.Reset();
	Answers.Reset();
}
