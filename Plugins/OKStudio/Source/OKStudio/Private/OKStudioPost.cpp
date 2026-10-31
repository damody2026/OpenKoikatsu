#include "OKStudioPost.h"

#include "OKSceneDocument.h"
#include "Engine/Scene.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "UObject/StrongObjectPtr.h"
#include "RHIGlobals.h"
#include "MaterialShared.h"

namespace
{
	/**
	 * One program over the scene colour, run as two passes in Studio's order (FPass: the opaque effects before bloom,
	 * the sun shafts after bloom and the grade):
	 * - Fog: Unity's own fog of the objects (RenderSettings of the Studio scene, level2: fog mode Exp2, density 0.01):
	 *   factor exp2(-(1.2011 density z)^2) of the eye depth z in metres, the colour (linearised fogColor) towards it.
	 *   The GlobalFog image effect on Studio's camera has distance fog off and a height density of 0.001: it adds nothing.
	 * - Ambient occlusion: Amplify Occlusion (radius in metres, intensity 1, power 1), here as a 12-tap screen-space
	 *   occlusion around the pixel's view-space position; the colour times lerp(Tint, 1, visibility).
	 * - Sun shafts: Standard Assets SunShafts with Studio's settings (level2: no sun transform, so from the view centre;
	 *   blur radius 3, intensity 2.2, max radius 0.5, two iterations of two 6-tap radial blurs, additive): only far
	 *   pixels (sky, background) feed it, dot(max(colour - threshold, 0), 1) times saturate(0.5 - distance to the sun).
	 *   The four radial blurs move a tap to the sun by 1 - prod(1 - j_k b_k) (b_k: 3 x 0.0013, 6/768, 12/768,
	 *   18/768); the two largest are sampled fully (36 taps), the two smallest by their mean.
	 * Far: more than 900 m away (the background plane is at 1 km, the sky dome at 10 km), or a surface Unity's depth
	 *   texture leaves out (render queue past 2500, such as Studio's basic shapes at 3000: SunShafts sees the far plane
	 *   there and their colour feeds the shafts). OKSceneActor draws those into custom depth with stencil 255.
	 */
	const TCHAR* GStudioPostCode = TEXT(R"(
float2 VUV = GetViewportUV(Parameters);
// The pixel itself from the scene texture nodes (the engine maps their coordinates); other taps by buffer UV, offset
// from the pixel by the viewport-to-buffer scale.
float3 C = SceneColorIn.rgb;
float DepthM = SceneDepthIn.r * 0.01;
float2 Base = GetDefaultSceneTextureUV(Parameters, 14);
float2 Scale = ViewportUVToSceneTextureUV(float2(1, 1), 14) - ViewportUVToSceneTextureUV(float2(0, 0), 14);
#define OK_COLOR(V) SceneTextureLookup(Base + ((V) - VUV) * Scale, 14, false).rgb
#define OK_DEPTH(V) (SceneTextureLookup(Base + ((V) - VUV) * Scale, 1, false).r * 0.01)
#define OK_CUSTOM_DEPTH(V) (SceneTextureLookup(Base + ((V) - VUV) * Scale, 13, false).r * 0.01)
#define OK_STENCIL(V) SceneTextureLookup(Base + ((V) - VUV) * Scale, 25, false).r
const float FarM = 900.0;
bool bFar = DepthM > FarM;
if (FogOn > 0.5 && !bFar)
{
	float F = 1.2011 * 0.01 * DepthM;
	C = lerp(FogColor.rgb, C, saturate(exp2(-F * F)));
}
if (AOOn > 0.5 && !bFar)
{
	float P00 = View.ViewToClip[0][0], P11 = View.ViewToClip[1][1];
	float2 Ndc = float2(VUV.x * 2.0 - 1.0, 1.0 - VUV.y * 2.0);
	float3 Pos = float3(Ndc.x * DepthM / P00, Ndc.y * DepthM / P11, DepthM);
	// The normal from depth 8 pixels to each side (central differences): next to the pixel, depth is flat per triangle
	// and every facet of a body took its own shade (blocks on the legs). Where one side jumps (a silhouette), only the
	// side whose depth is closer.
	float2 Px = View.ViewSizeAndInvSize.zw * 8.0;
	float Dr = OK_DEPTH(VUV + float2(Px.x, 0)), Dl = OK_DEPTH(VUV - float2(Px.x, 0));
	float Dd = OK_DEPTH(VUV + float2(0, Px.y)), Du = OK_DEPTH(VUV - float2(0, Px.y));
	float3 PosR = float3((Ndc.x + 2.0 * Px.x) * Dr / P00, Ndc.y * Dr / P11, Dr);
	float3 PosL = float3((Ndc.x - 2.0 * Px.x) * Dl / P00, Ndc.y * Dl / P11, Dl);
	float3 PosD = float3(Ndc.x * Dd / P00, (Ndc.y - 2.0 * Px.y) * Dd / P11, Dd);
	float3 PosU = float3(Ndc.x * Du / P00, (Ndc.y + 2.0 * Px.y) * Du / P11, Du);
	const float Jump = 0.02 * DepthM + 0.01;
	float3 Ex = max(abs(Dr - DepthM), abs(Dl - DepthM)) < Jump ? (PosR - PosL) : (abs(Dr - DepthM) < abs(Dl - DepthM) ? PosR - Pos : Pos - PosL);
	float3 Ey = max(abs(Dd - DepthM), abs(Du - DepthM)) < Jump ? (PosD - PosU) : (abs(Dd - DepthM) < abs(Du - DepthM) ? PosD - Pos : Pos - PosU);
	float3 N = normalize(cross(Ey, Ex));
	if (N.z > 0) N = -N;
	float2 RadiusUV = float2(AORadius * P00 * 0.5 / DepthM, AORadius * P11 * 0.5 / DepthM);
	float Occ = 0.0;
	[unroll] for (int k = 0; k < 12; ++k)
	{
		float A = (k + 0.5) * 2.39996;
		float R = sqrt((k + 0.5) / 12.0);
		float2 S = VUV + float2(cos(A), sin(A)) * R * RadiusUV * float2(1, -1);
		float Ds = OK_DEPTH(S);
		float2 Ns = float2(S.x * 2.0 - 1.0, 1.0 - S.y * 2.0);
		float3 V = float3(Ns.x * Ds / P00, Ns.y * Ds / P11, Ds) - Pos;
		float Vv = dot(V, V);
		Occ += max(0.0, dot(V, N) - 0.002 * DepthM) / (Vv + 0.0001) * saturate(1.0 - Vv / (AORadius * AORadius * 4.0));
	}
	float Visibility = saturate(1.0 - Occ / 12.0 * AORadius);
	C *= lerp(AOTint.rgb, float3(1, 1, 1), Visibility);
}
if (ShaftsOn > 0.5)
{
	const float2 Sun = float2(0.5, 0.5);
	const float B1 = 3.0 * 0.0013020834, B2 = 3.0 * 6.0 / 768.0, B3 = 3.0 * 12.0 / 768.0, B4 = 3.0 * 18.0 / 768.0;
	float Small = (1.0 - 2.5 * B1) * (1.0 - 2.5 * B2);
	float Sum = 0.0;
	[loop] for (int j3 = 0; j3 < 6; ++j3)
	[loop] for (int j4 = 0; j4 < 6; ++j4)
	{
		float Keep = Small * (1.0 - j3 * B3) * (1.0 - j4 * B4);
		float2 T = Sun - (Sun - VUV) * Keep;
		float Dt = OK_DEPTH(T);
		// The visible surface is one the depth texture leaves out: the marked parts' custom depth is where it is.
		bool bAbsent = OK_STENCIL(T) > 254.5 && abs(OK_CUSTOM_DEPTH(T) - Dt) < 0.002 * Dt + 0.01;
		if (Dt > FarM || bAbsent || ShaftsAllFar > 0.5)
		{
			float3 Ct = OK_COLOR(T);
			// The shaft source lives in an 8-bit render texture (RenderTexture.GetTemporary's ARGB32): each tap is at
			// most 1. Summed unclamped, a bright HDR sky saturated the whole disc into a white blob without rays.
			Sum += saturate(dot(max(Ct - SunThreshold.rgb, 0.0), float3(1, 1, 1)) * saturate(0.5 - length(Sun - T)));
		}
	}
	C += saturate(Sum / 36.0 * SunColor.rgb * 2.2);
}
return C;
#undef OK_COLOR
#undef OK_DEPTH
#undef OK_CUSTOM_DEPTH
#undef OK_STENCIL
)");

	/** The pass at `Location` (priority `Priority` there). */
	UMaterial* BuildMaterial(EBlendableLocation Location, int32 Priority)
	{
#if WITH_EDITORONLY_DATA
		UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		Mat->MaterialDomain = MD_PostProcess;
		Mat->BlendableLocation = Location;
		Mat->BlendablePriority = Priority;
		auto Add = [Mat](UMaterialExpression* E) { E->Material = Mat; Mat->GetExpressionCollection().AddExpression(E); return E; };
		auto* Code = NewObject<UMaterialExpressionCustom>(Mat);
		Code->OutputType = CMOT_Float3;
		Code->Code = GStudioPostCode;
		Code->Inputs.Reset();
		auto Input = [&](const TCHAR* Name, UMaterialExpression* E) { FCustomInput& I = Code->Inputs.AddDefaulted_GetRef(); I.InputName = Name; I.Input.Expression = E; };
		auto Scalar = [&](const TCHAR* Name) { auto* P = NewObject<UMaterialExpressionScalarParameter>(Mat); P->ParameterName = Name; Add(P); Input(Name, P); };
		auto Vector = [&](const TCHAR* Name) { auto* P = NewObject<UMaterialExpressionVectorParameter>(Mat); P->ParameterName = Name; Add(P); Input(Name, P); };
		Scalar(TEXT("FogOn")); Vector(TEXT("FogColor"));
		Scalar(TEXT("AOOn")); Vector(TEXT("AOTint")); Scalar(TEXT("AORadius"));
		Scalar(TEXT("ShaftsOn")); Scalar(TEXT("ShaftsAllFar")); Vector(TEXT("SunColor")); Vector(TEXT("SunThreshold"));
		// The scene textures the code samples (SceneTextureLookup needs them in the graph).
		for (ESceneTextureId Id : { PPI_PostProcessInput0, PPI_SceneDepth, PPI_CustomDepth, PPI_CustomStencil })
		{
			auto* T = NewObject<UMaterialExpressionSceneTexture>(Mat);
			T->SceneTextureId = Id;
			Add(T);
			Input(Id == PPI_SceneDepth ? TEXT("SceneDepthIn") : Id == PPI_PostProcessInput0 ? TEXT("SceneColorIn")
				: Id == PPI_CustomDepth ? TEXT("CustomDepthIn") : TEXT("CustomStencilIn"), T);
		}
		Add(Code);
		if (UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData()) Ed->EmissiveColor.Expression = Code;
		Mat->PreEditChange(nullptr);
		Mat->PostEditChange();
		return Mat;
#else
		return nullptr;
#endif
	}

	/** Unity's GammaToLinearSpace: Studio is in linear space, its colours reach the shaders linearised. */
	FLinearColor Linear(const FLinearColor& C)
	{
		auto L = [](float V) { return V <= 0.04045f ? V / 12.92f : FMath::Pow((V + 0.055f) / 1.055f, 2.4f); };
		return FLinearColor(L(C.R), L(C.G), L(C.B), C.A);
	}
}

namespace
{
	/**
	 * Two passes of the same program, in Studio's order: the opaque effects (fog, ambient occlusion) before bloom, and
	 * SunShafts after bloom and the colour grade (Studio's camera runs BloomAndFlares, AmplifyColor, then SunShafts).
	 * Added before bloom, the shafts were bloomed into a white blob over the view centre (83283571_p1's stage window).
	 */
	struct FPass
	{
		TStrongObjectPtr<UMaterial> Base;
		TStrongObjectPtr<UMaterialInstanceDynamic> Instance;
	};
	FPass Passes[2];
	const EBlendableLocation PassLocation[2] = { BL_SceneColorBeforeDOF, BL_SceneColorAfterDOF };
	// After the grade (OKStudioGrade.cpp: after depth of field, priority 100).
	const int32 PassPriority[2] = { 0, 110 };

	FString MaterialState(const UMaterial* Base)
	{
		if (!Base) return TEXT("not made");
		if (Base->IsCompiling()) return TEXT("compiling");
		const FMaterialResource* Resource = Base->GetMaterialResource(GMaxRHIShaderPlatform);
		if (!Resource) return TEXT("no resource");
#if WITH_EDITOR
		// Compile errors and compile progress are editor-only on FMaterialResource (a packaged game did not build).
		if (Resource->GetCompileErrors().Num()) return FString::Join(Resource->GetCompileErrors(), TEXT(" | "));
#endif
		if (!Resource->GetGameThreadShaderMap()) return TEXT("no shader map");
#if WITH_EDITOR
		if (!Resource->IsCompilationFinished()) return TEXT("shaders compiling");
#endif
		if (!Resource->IsGameThreadShaderMapComplete()) return TEXT("shader map incomplete");
		return TEXT("ready");
	}

	/** The pass's instance once its shaders are ready (null until then: the engine would draw the default material). */
	UMaterialInstanceDynamic* ReadyInstance(int32 Index)
	{
		FPass& P = Passes[Index];
		if (!P.Base.IsValid()) P.Base.Reset(BuildMaterial(PassLocation[Index], PassPriority[Index]));
		UMaterial* Base = P.Base.Get();
		if (!Base || Base->IsCompiling()) return nullptr;
#if WITH_EDITOR
		static bool bReported = false;
		const FMaterialResource* Resource = Base->GetMaterialResource(GMaxRHIShaderPlatform);
		// FMaterialResource::GetCompileErrors is editor-only: a packaged game (StreetSocial) did not build.
		if (!bReported && Resource && Resource->GetCompileErrors().Num())
		{
			bReported = true;
			for (const FString& Message : Resource->GetCompileErrors()) UE_LOG(LogTemp, Warning, TEXT("OK studio: the scene post material did not compile: %s"), *Message);
		}
		if (Resource && Resource->GetCompileErrors().Num()) return nullptr;
#endif
		if (Base->IsCompilingOrHadCompileError(GMaxRHIShaderPlatform)) return nullptr;
		const FString State = MaterialState(Base);
		// Each change of a pass's state, once (why a scene shows without its shafts while they compile).
		static FString Reported[2];
		static int32 Submits[2] = {};
		if (State != Reported[Index])
		{
			UE_LOG(LogTemp, Display, TEXT("OK studio post pass %d: %s (%d submits)"), Index, *State, Submits[Index]);
			Reported[Index] = State;
		}
		if (State == TEXT("shader map incomplete"))
		{
			// With the shader job cache (on-demand compilation) only the shaders asked for are compiled, and nothing asks
			// for the post-process pass shaders of a material made at run time: submit the rest of the map, again every
			// two seconds while it stays incomplete. Four submits in the first frames once ran out while the compiler was
			// busy (a new Niagara system), and that process never drew its shafts.
			static double LastSubmit[2] = {};
			const double Now = FPlatformTime::Seconds();
			if (Submits[Index] == 0 || Now - LastSubmit[Index] > 2.0)
			{
				++Submits[Index];
				LastSubmit[Index] = Now;
				Base->CacheShaders(EMaterialShaderPrecompileMode::Default);
			}
		}
		if (State != TEXT("ready")) return nullptr;
		if (!P.Instance.IsValid()) P.Instance.Reset(UMaterialInstanceDynamic::Create(Base, GetTransientPackage()));
		return P.Instance.Get();
	}
}

FString OKStudioScenePostMaterialState(int32 Pass)
{
	return MaterialState(Passes[FMath::Clamp(Pass, 0, 1)].Base.Get());
}

void OKStudioApplyScenePostMaterial(const FOKScenePost& Post, FPostProcessSettings& Settings)
{
	for (const FPass& P : Passes)
		if (P.Instance.IsValid()) Settings.WeightedBlendables.Array.RemoveAll([&P](const FWeightedBlendable& B) { return B.Object == P.Instance.Get(); });
	// OK_STUDIO_POST_AO=0: without the occlusion pass (diagnostic).
	static const bool bAoOff = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_POST_AO")) == TEXT("0");
	const bool bOpaque = Post.bFog || (Post.bAmbientOcclusion && !bAoOff);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (Index == 0 ? !bOpaque : !Post.bSunShafts) continue;
		UMaterialInstanceDynamic* M = ReadyInstance(Index);
		if (!M) continue;
		M->SetScalarParameterValue(TEXT("FogOn"), Index == 0 && Post.bFog ? 1.f : 0.f);
		M->SetVectorParameterValue(TEXT("FogColor"), Linear(Post.FogColor));
		M->SetScalarParameterValue(TEXT("AOOn"), Index == 0 && Post.bAmbientOcclusion && !bAoOff ? 1.f : 0.f);
		// AmplifyOcclusionBase writes _AO_Levels with Shader.SetGlobalColor, which
		// forwards directly to SetGlobalVectorImpl. Its RGB is already shader input.
		M->SetVectorParameterValue(TEXT("AOTint"), Post.AmbientOcclusionTint);
		M->SetScalarParameterValue(TEXT("AORadius"), FMath::Max(Post.AmbientOcclusionRadius, 0.001f));
		M->SetScalarParameterValue(TEXT("ShaftsOn"), Index == 1 ? 1.f : 0.f);
		// OK_STUDIO_SHAFTS_DEPTH=0: every pixel feeds the shafts, near or far (diagnostic).
		static const bool bShaftsAllFar = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_SHAFTS_DEPTH")) == TEXT("0");
		M->SetScalarParameterValue(TEXT("ShaftsAllFar"), bShaftsAllFar ? 1.f : 0.f);
		// SunShafts.sunColor and sunThreshold reach the shader as vectors (SetVector): not linearised.
		M->SetVectorParameterValue(TEXT("SunColor"), Post.SunShaftColor);
		M->SetVectorParameterValue(TEXT("SunThreshold"), Post.SunShaftThreshold);
		Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, M));
	}
}
