#include "OKStudioBloom.h"

#include "OKSceneDocument.h"
#include "Engine/Scene.h"
#include "HAL/IConsoleManager.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionUserSceneTexture.h"
#include "RHIGlobals.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
TAutoConsoleVariable<int32> CVarStudioSourceBloom(TEXT("ok.Studio.SourceBloom"), 1,
    TEXT("Studio's installed HDR BloomAndFlares passes (0: legacy UE Gaussian approximation)."));
constexpr int32 SourceBloomPassCount = 8;
TStrongObjectPtr<UMaterial> GSourceBloomParents[SourceBloomPassCount];
bool GSourceBloomSubmitted[SourceBloomPassCount] = {};
const TCHAR* GSourceBloomInputs[] = { nullptr, TEXT("OKBloomHalf"), TEXT("OKBloomQuarter"), TEXT("OKBloomBright"),
    TEXT("OKBloomV0"), TEXT("OKBloomH0"), TEXT("OKBloomV1"), TEXT("OKBloomFinal") };
const TCHAR* GSourceBloomOutputs[] = { TEXT("OKBloomHalf"), TEXT("OKBloomQuarter"), TEXT("OKBloomBright"), TEXT("OKBloomV0"),
    TEXT("OKBloomH0"), TEXT("OKBloomV1"), TEXT("OKBloomFinal"), nullptr };
const TCHAR* GSourceBloomAssetRoot = TEXT("/Game/OKStudio/Post/SourceBloom1");

UMaterial* BuildSourceBloomPass(int32 Pass, UObject* Outer = GetTransientPackage(), FName Name = NAME_None, EObjectFlags Flags = RF_Transient)
{
#if WITH_EDITORONLY_DATA
    UMaterial* Mat = NewObject<UMaterial>(Outer, Name, Flags);
    Mat->MaterialDomain = MD_PostProcess;
    Mat->BlendableLocation = BL_SceneColorBeforeDOF;
    Mat->BlendablePriority = 10 + Pass;
    Mat->bIsBlendable = false; // Separate graph inputs / outputs, never interpolate different passes together.
    // Carry pre-exposed HDR between passes. Bright-pass alone scales its threshold into that space.
    Mat->bDisablePreExposureScale = true;
    if (GSourceBloomOutputs[Pass])
    {
        Mat->UserSceneTexture = GSourceBloomOutputs[Pass];
        Mat->UserTextureDivisor = Pass == 0 ? FIntPoint(2, 2) : FIntPoint(4, 4);
    }
    auto Add = [Mat](UMaterialExpression* E) { E->Material = Mat; Mat->GetExpressionCollection().AddExpression(E); return E; };
    auto* Code = NewObject<UMaterialExpressionCustom>(Mat);
    Code->OutputType = CMOT_Float3;
    Code->Inputs.Reset();
    auto Input = [&](const TCHAR* Name, UMaterialExpression* E, int32 Output = 0)
    {
        FCustomInput& I = Code->Inputs.AddDefaulted_GetRef(); I.InputName = Name; I.Input.Expression = E; I.Input.OutputIndex = Output;
    };
    auto Scalar = [&](const TCHAR* Name)
    {
        auto* P = NewObject<UMaterialExpressionScalarParameter>(Mat); P->ParameterName = Name; Add(P); Input(Name, P);
    };
    FString Id = TEXT("PPI_PostProcessInput0");
    if (GSourceBloomInputs[Pass])
    {
        auto* Texture = NewObject<UMaterialExpressionUserSceneTexture>(Mat);
        Texture->UserSceneTexture = GSourceBloomInputs[Pass]; Texture->bFiltered = true; Texture->bClamped = true;
        Add(Texture); Input(TEXT("Source"), Texture); Input(TEXT("SourceSize"), Texture, 1);
        Id = FString(TEXT("PPIUser_")) + GSourceBloomInputs[Pass];
    }
    if (!GSourceBloomInputs[Pass] || Pass == 7)
    {
        auto* Scene = NewObject<UMaterialExpressionSceneTexture>(Mat);
        Scene->SceneTextureId = PPI_PostProcessInput0; Add(Scene); Input(TEXT("Scene"), Scene);
    }
    // SceneTextureFetch handles viewport offsets, filtering and clamping (including reduced-size texture tiles).
    Code->Code = TEXT("#define FETCH(O) SceneTextureFetch(") + Id + TEXT(", (O)).rgb\n");
    if (Pass < 2)
    {
        // Hidden/Blend pass 2: four samples at (+/-0.5,+/-0.5) input texels; first half, then quarter resolution.
        Code->Code += TEXT("return 0.25 * (FETCH(float2(-0.5,-0.5)) + FETCH(float2(0.5,0.5)) + FETCH(float2(-0.5,0.5)) + FETCH(float2(0.5,-0.5)));\n");
    }
    else if (Pass == 2)
    {
        Scalar(TEXT("Threshold"));
        // Main Camera has HDR on; the installed PS subtracts threshold.x per channel, with no 1/(1-t) multiplier.
        // Studio sets useSrcAlphaAsMask=0. Negative threshold is OK Studio's existing "all pixels" extension.
        Code->Code += TEXT("return max(Source.rgb - max(Threshold,0.0) * View.PreExposure, 0.0);\n");
    }
    else if (Pass < 7)
    {
        Scalar(TEXT("Spread"));
        const bool Vertical = Pass == 3 || Pass == 5;
        const float Iteration = Pass < 5 ? 1.f : 1.5f;
        Code->Code += FString::Printf(TEXT("float2 Step = float2(%s) * (Spread * %.1f * SourceSize.y / 512.0);\n"),
            Vertical ? TEXT("0,1") : TEXT("1,0"), Iteration);
        // Hidden/SeparableBlurPlus VS/PS: centre .225; +/-1 .15, +/-2 .11, +/-3 .075, +/-6.5 .0525.
        Code->Code += TEXT("return Source.rgb * 0.225 + (FETCH(Step) + FETCH(-Step)) * 0.15 + (FETCH(Step*2) + FETCH(-Step*2)) * 0.11 + (FETCH(Step*3) + FETCH(-Step*3)) * 0.075 + (FETCH(Step*6.5) + FETCH(-Step*6.5)) * 0.0525;\n");
    }
    else
    {
        Scalar(TEXT("Intensity"));
        // Hidden/Blend pass 1: source + intensity * bloom, without saturating HDR.
        Code->Code += TEXT("return Scene.rgb + Intensity * Source.rgb;\n");
    }
    Code->Code += TEXT("#undef FETCH\n");
    Add(Code);
    if (UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData()) Ed->EmissiveColor.Expression = Code;
    Mat->PreEditChange(nullptr); Mat->PostEditChange();
    return Mat;
#else
    return nullptr;
#endif
}

// Settings own the instances. An immutable parameter tuple lets different cameras retain their own values.
struct FSourceBloomInstances
{
    float Threshold, Spread, Intensity;
    TWeakObjectPtr<UMaterialInstanceDynamic> Passes[SourceBloomPassCount];
};
TArray<FSourceBloomInstances> GSourceBloomInstances;

#if WITH_EDITOR
void BakeBloom(const TArray<FString>& Args)
{
    bool Passed = true;
    for (int32 I = 0; I < SourceBloomPassCount; ++I)
    {
        const FString Name = FString::Printf(TEXT("M_SourceBloom%d"), I);
        const FString PackageName = FString(GSourceBloomAssetRoot) / Name;
        if (FPackageName::DoesPackageExist(PackageName))
        {
            UE_LOG(LogTemp, Error, TEXT("OK studio bake bloom: refusing to overwrite %s; use a new asset root version"), *PackageName);
            Passed = false; break;
        }
        UPackage* Package = CreatePackage(*PackageName);
        UMaterial* Material = BuildSourceBloomPass(I, Package, FName(*Name), RF_Public | RF_Standalone);
        FSavePackageArgs Save; Save.TopLevelFlags = RF_Public | RF_Standalone;
        const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        if (!Material || !UPackage::SavePackage(Package, Material, *File, Save)) { Passed = false; break; }
    }
    UE_LOG(LogTemp, Display, TEXT("OK studio bake bloom: RESULT %s"), Passed ? TEXT("PASS") : TEXT("FAIL"));
    if (Args.Contains(TEXT("exit"))) FPlatformMisc::RequestExit(false);
}
FAutoConsoleCommand BakeBloomCommand(TEXT("OKStudio.BakeBloom"), TEXT("OKStudio.BakeBloom [exit]: save the versioned shared source bloom parents for cooking; refuses existing assets."),
    FConsoleCommandWithArgsDelegate::CreateStatic(&BakeBloom));
#endif
}

FString OKStudioSourceBloomState()
{
    for (int32 I = 0; I < SourceBloomPassCount; ++I)
    {
        UMaterial* Mat = GSourceBloomParents[I].Get();
        if (!Mat) return TEXT("not made");
        const FMaterialResource* Resource = Mat->GetMaterialResource(GMaxRHIShaderPlatform);
#if WITH_EDITOR
        if (Resource && !Resource->GetCompileErrors().IsEmpty()) return FString::Join(Resource->GetCompileErrors(), TEXT(" | "));
#endif
        if (Mat->IsCompiling() || !Resource || !Resource->GetGameThreadShaderMap()) return TEXT("compiling");
        if (!Resource->IsGameThreadShaderMapComplete())
        {
            if (!GSourceBloomSubmitted[I]) { GSourceBloomSubmitted[I] = true; Mat->CacheShaders(EMaterialShaderPrecompileMode::Default); }
            return TEXT("compiling");
        }
    }
    return TEXT("ready");
}

bool OKStudioApplySourceBloom(const FOKScenePost& Post, FPostProcessSettings& Settings)
{
    Settings.WeightedBlendables.Array.RemoveAll([](const FWeightedBlendable& B)
    {
        if (const auto* BloomInstance = Cast<UMaterialInstanceDynamic>(B.Object))
            for (const auto& BloomParent : GSourceBloomParents) if (BloomParent && BloomInstance->Parent == BloomParent.Get()) return true;
        return false;
    });
    if (!Post.bBloom || CVarStudioSourceBloom.GetValueOnGameThread() == 0) return false;
    for (int32 I = 0; I < SourceBloomPassCount; ++I) if (!GSourceBloomParents[I])
    {
        const FString Name = FString::Printf(TEXT("M_SourceBloom%d"), I);
        UMaterial* Parent = LoadObject<UMaterial>(nullptr, *(FString(GSourceBloomAssetRoot) / Name + TEXT(".") + Name));
        GSourceBloomParents[I].Reset(Parent ? Parent : BuildSourceBloomPass(I));
    }
    if (OKStudioSourceBloomState() != TEXT("ready")) return false;
    const float Threshold = Post.BloomThreshold;
    const float Spread = 4.f * FMath::Clamp(Post.BloomSize, 0.1f, 8.f);
    const float Intensity = FMath::Max(Post.BloomIntensity, 0.f);
    GSourceBloomInstances.RemoveAll([](const FSourceBloomInstances& Entry) { return !Entry.Passes[0].IsValid(); });
    FSourceBloomInstances* Found = GSourceBloomInstances.FindByPredicate([&](const FSourceBloomInstances& Entry)
    {
        if (Entry.Threshold != Threshold || Entry.Spread != Spread || Entry.Intensity != Intensity) return false;
        for (const auto& BloomInstance : Entry.Passes) if (!BloomInstance.IsValid()) return false;
        return true;
    });
    if (!Found)
    {
        if (GSourceBloomInstances.Num() >= 32) GSourceBloomInstances.RemoveAt(0);
        FSourceBloomInstances& Entry = GSourceBloomInstances.AddDefaulted_GetRef();
        Entry.Threshold = Threshold; Entry.Spread = Spread; Entry.Intensity = Intensity;
        for (int32 I = 0; I < SourceBloomPassCount; ++I)
        {
            auto* BloomInstance = UMaterialInstanceDynamic::Create(GSourceBloomParents[I].Get(), GetTransientPackage());
            BloomInstance->SetScalarParameterValue(TEXT("Threshold"), Threshold);
            BloomInstance->SetScalarParameterValue(TEXT("Spread"), Spread);
            BloomInstance->SetScalarParameterValue(TEXT("Intensity"), Intensity);
            Entry.Passes[I] = BloomInstance;
        }
        Found = &Entry;
    }
    for (const auto& Pass : Found->Passes) Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, Pass.Get()));
    static bool Reported = false;
    if (!Reported)
    {
        Reported = true;
        UE_LOG(LogTemp, Display, TEXT("OK studio source bloom: HDR native chain ready (8 passes, 9 taps, before DOF)"));
    }
    return true;
}
