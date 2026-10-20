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
	 * One pass over the scene colour, in Studio's order (opaque effects, then the image effects):
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
	 * Far: more than 900 m away (the background plane is at 1 km, the sky dome at 10 km).
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
	float2 Px = View.ViewSizeAndInvSize.zw;
	float Dx = OK_DEPTH(VUV + float2(Px.x, 0));
	float Dy = OK_DEPTH(VUV + float2(0, Px.y));
	float3 PosX = float3((Ndc.x + 2.0 * Px.x) * Dx / P00, Ndc.y * Dx / P11, Dx);
	float3 PosY = float3(Ndc.x * Dy / P00, (Ndc.y - 2.0 * Px.y) * Dy / P11, Dy);
	float3 N = normalize(cross(PosY - Pos, PosX - Pos));
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
		if (Dt > FarM)
		{
			float3 Ct = OK_COLOR(T);
			Sum += dot(max(Ct - SunThreshold.rgb, 0.0), float3(1, 1, 1)) * saturate(0.5 - length(Sun - T));
		}
	}
	C += saturate(Sum / 36.0 * SunColor.rgb * 2.2);
}
return C;
#undef OK_COLOR
#undef OK_DEPTH
)");

	UMaterial* BuildMaterial()
	{
#if WITH_EDITORONLY_DATA
		UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		Mat->MaterialDomain = MD_PostProcess;
		Mat->BlendableLocation = BL_SceneColorBeforeDOF;
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
		Scalar(TEXT("ShaftsOn")); Vector(TEXT("SunColor")); Vector(TEXT("SunThreshold"));
		// The scene textures the code samples (SceneTextureLookup needs them in the graph).
		for (ESceneTextureId Id : { PPI_PostProcessInput0, PPI_SceneDepth })
		{
			auto* T = NewObject<UMaterialExpressionSceneTexture>(Mat);
			T->SceneTextureId = Id;
			Add(T);
			Input(Id == PPI_SceneDepth ? TEXT("SceneDepthIn") : TEXT("SceneColorIn"), T);
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
	TStrongObjectPtr<UMaterial> Base;
	TStrongObjectPtr<UMaterialInstanceDynamic> Instance;
}

FString OKStudioScenePostMaterialState()
{
	if (!Base.IsValid()) return TEXT("not made");
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

void OKStudioApplyScenePostMaterial(const FOKScenePost& Post, FPostProcessSettings& Settings)
{
	if (Instance.IsValid()) Settings.WeightedBlendables.Array.RemoveAll([](const FWeightedBlendable& B) { return B.Object == Instance.Get(); });
	if (!Post.bFog && !Post.bAmbientOcclusion && !Post.bSunShafts) return;
	if (!Base.IsValid()) Base.Reset(BuildMaterial());
	if (!Base.IsValid()) return;
	// Until its shaders are compiled the engine would draw the default material over the whole view.
	{
		static bool bReported = false;
		const FMaterialResource* Resource = Base->GetMaterialResource(GMaxRHIShaderPlatform);
		if (Base->IsCompiling()) return;
#if WITH_EDITOR
		// FMaterialResource::GetCompileErrors is editor-only: a packaged game (StreetSocial) did not build.
		if (!bReported && Resource && Resource->GetCompileErrors().Num())
		{
			bReported = true;
			for (const FString& Message : Resource->GetCompileErrors()) UE_LOG(LogTemp, Warning, TEXT("OK studio: the scene post material did not compile: %s"), *Message);
		}
		if (Resource && Resource->GetCompileErrors().Num()) return;
#endif
		if (Base->IsCompilingOrHadCompileError(GMaxRHIShaderPlatform)) return;
		const FString State = OKStudioScenePostMaterialState();
		if (State == TEXT("shader map incomplete"))
		{
			// With the shader job cache (on-demand compilation) only the shaders asked for are compiled, and nothing asks
			// for the post-process pass shaders of a material made at run time: submit the rest of the map.
			static int32 Submits = 0;
			if (Submits < 4) { ++Submits; Base->CacheShaders(EMaterialShaderPrecompileMode::Default); }
		}
		if (State != TEXT("ready")) return;
	}
	if (!Instance.IsValid()) Instance.Reset(UMaterialInstanceDynamic::Create(Base.Get(), GetTransientPackage()));
	UMaterialInstanceDynamic* M = Instance.Get();
	M->SetScalarParameterValue(TEXT("FogOn"), Post.bFog ? 1.f : 0.f);
	M->SetVectorParameterValue(TEXT("FogColor"), Linear(Post.FogColor));
	M->SetScalarParameterValue(TEXT("AOOn"), Post.bAmbientOcclusion ? 1.f : 0.f);
	M->SetVectorParameterValue(TEXT("AOTint"), Linear(Post.AmbientOcclusionTint));
	M->SetScalarParameterValue(TEXT("AORadius"), FMath::Max(Post.AmbientOcclusionRadius, 0.001f));
	M->SetScalarParameterValue(TEXT("ShaftsOn"), Post.bSunShafts ? 1.f : 0.f);
	// SunShafts.sunColor and sunThreshold reach the shader as vectors (SetVector): not linearised.
	M->SetVectorParameterValue(TEXT("SunColor"), Post.SunShaftColor);
	M->SetVectorParameterValue(TEXT("SunThreshold"), Post.SunShaftThreshold);
	Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, M));
}
