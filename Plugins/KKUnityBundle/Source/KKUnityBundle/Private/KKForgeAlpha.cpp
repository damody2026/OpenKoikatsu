#include "KKForgeAlpha.h"
#include "KKRuntimeSharedMaterial.h"
#include "KKToonShadowBias.h"
#include "KKSkeletalBuilder.h"
#include "KKMaterialDedupe.h"
#include "KKMaterialExposure.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionVertexTangentWS.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

struct FOKForgeAlphaUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKForgeAlphaTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKForgeAlphaProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* ForgeAlphaDefaultTexture;};
#include "Shaders/ForgeAlphaBindings.inl"
#include "Shaders/ForgeAlphaProgram.inl"

static const FOKForgeAlphaProperty* ForgeAlphaProperty(const FString& Name)
{
    for (const auto& P:GForgeAlphaProperties) if(Name==P.Name) return &P;
    return nullptr;
}
static FLinearColor ForgeAlphaValue(const FOKNativeItemDraw& Draw,const FString& Name)
{
    if(const FLinearColor* V=Draw.Values.Find(Name)) return *V;
    if(const FOKForgeAlphaProperty* P=ForgeAlphaProperty(Name)) return P->Default;
    // KKPItemDiffuse.cginc initializes this non-Properties uniform to 1e-10.
    // Zero makes RGBtoHCV divide 0/0 on achromatic albedo (e.g. null MainTex).
    if(Name==TEXT("Epsilon")) return FLinearColor(1e-10f,0,0,0);
    if(Name.EndsWith(TEXT("_ST"))) return FLinearColor(1,1,0,0);
    return FLinearColor(0,0,0,0);
}
static FString ForgeAlphaBits(float ForgeAlphaValue)
{
    uint32 Word;FMemory::Memcpy(&Word,&ForgeAlphaValue,4);
    return FString::Printf(TEXT("asfloat(0x%08xu)"),Word);
}
static float ForgeAlphaLinear(float V)
{
    return V<=0.04045f ? V/12.92f : FMath::Pow((V+0.055f)/1.055f,2.4f);
}
TArray<FString> OKForgeAlphaUniformNames()
{
    TArray<FString> Names;
    for(const auto& U:GForgeAlphaForwardUniforms) Names.AddUnique(U.Name);
    for(const auto& U:GForgeAlphaShadowUniforms) Names.AddUnique(U.Name);
    Names.AddUnique(TEXT("_CullOption"));Names.AddUnique(TEXT("_LineWidthS"));
    Names.AddUnique(TEXT("_OKNativeAlphaOpaque"));
    return Names;
}
TArray<FString> OKForgeAlphaTextureNames()
{
    TArray<FString> Names;
    for(const auto& T:GForgeAlphaForwardTextures) if(T.Dim==2 && FString(T.Name)!=TEXT("_RampG")) Names.AddUnique(T.Name);
    return Names;
}
bool OKForgeAlphaTextureIsColor(const FString& Name)
{
    return Name==TEXT("_MainTex") || Name==TEXT("_Texture2");
}
static UTexture* ForgeAlphaDefaultTexture(const FString& Name,bool bCube=false)
{
    static TMap<FString,UTexture*> Cache;
    const FString Key=bCube?TEXT("cube"):Name;
    if(UTexture** Hit=Cache.Find(Key)) return *Hit;
    UTexture* Texture=nullptr;
    if(bCube)
    {
        UTextureCube* Cube=UTextureCube::CreateTransient(1,1,PF_B8G8R8A8);
        if(Cube)
        {
            auto& Bulk=Cube->GetPlatformData()->Mips[0].BulkData;
            void* Data=Bulk.Lock(LOCK_READ_WRITE);FMemory::Memzero(Data,Bulk.GetBulkDataSize());Bulk.Unlock();
            Cube->SRGB=false;Cube->UpdateResource();Texture=Cube;
        }
    }
    else
    {
        UTexture2D* Tex=UTexture2D::CreateTransient(1,1,PF_B8G8R8A8);
        if(Tex)
        {
            // Unity packed bump decodes alpha*red=.5 and green=.5.
            // KKS Unity 2019 ShaderLab "black" includes alpha zero. Alpha one
            // would make a missing emission mask replace all diffuse with black.
            FColor Color=Name==TEXT("white")?FColor::White:Name==TEXT("bump")?FColor(255,128,255,128):
                Name==TEXT("gray")?FColor(128,128,128,128):Name==TEXT("red")?FColor(255,0,0,0):FColor(0,0,0,0);
            void* Data=Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
            FMemory::Memcpy(Data,&Color,4);Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
            Tex->SRGB=false;Tex->AddressX=Tex->AddressY=TA_Wrap;Tex->UpdateResource();Texture=Tex;
        }
    }
    if(Texture) {Texture->AddToRoot();Cache.Add(Key,Texture);}
    return Texture;
}

UMaterial* OKMakeForgeAlphaMaterial(const FOKNativeItemDraw& Draw)
{
#if WITH_EDITOR

    const bool bShadow=Draw.ForgeShadowDepth!=nullptr;
    const auto* AlphaOpaque=Draw.Values.Find(TEXT("_OKNativeAlphaOpaque"));
    const bool bOpaque=AlphaOpaque && AlphaOpaque->R==1.f;
    UMaterial* Mat=NewObject<UMaterial>(GetTransientPackage(),NAME_None,RF_Transient);
    // Fresh transient preview materials have no existing users to recreate.
    Mat->bIsPreviewMaterial=true;
    Mat->MaterialDomain=MD_Surface;Mat->BlendMode=bOpaque?BLEND_Masked:BLEND_Translucent;Mat->SetShadingModel(MSM_Unlit);
    Mat->TwoSided=true;Mat->OpacityMaskClipValue=0.5f;
    Mat->SetUsageByFlag(MATUSAGE_SkeletalMesh,true);Mat->SetUsageByFlag(MATUSAGE_StaticMesh,true);Mat->SetUsageByFlag(MATUSAGE_MorphTargets,true);
    UMaterialExpressionCustom* PS=NewObject<UMaterialExpressionCustom>(Mat);
    PS->Material=Mat;PS->OutputType=CMOT_Float4;PS->Description=TEXT("Installed Forge MainAlpha D3D11 program");
    auto Add=[Mat,PS](const TCHAR* Name,UMaterialExpression* Expression)
    {
        Expression->Material=Mat;Mat->GetExpressionCollection().AddExpression(Expression);
        auto& I=PS->Inputs.AddDefaulted_GetRef();I.InputName=Name;I.Input.Expression=Expression;
    };
    Add(TEXT("UV"),NewObject<UMaterialExpressionTextureCoordinate>(Mat));
    Add(TEXT("Position"),NewObject<UMaterialExpressionWorldPosition>(Mat));
    Add(TEXT("CameraPosition"),NewObject<UMaterialExpressionCameraPositionWS>(Mat));
    Add(TEXT("Normal"),NewObject<UMaterialExpressionVertexNormalWS>(Mat));
    Add(TEXT("Tangent"),NewObject<UMaterialExpressionVertexTangentWS>(Mat));
    Add(TEXT("FaceSign"),NewObject<UMaterialExpressionTwoSidedSign>(Mat));
    Add(TEXT("FrameTime"),NewObject<UMaterialExpressionTime>(Mat));
    UMaterialExpressionConstant3Vector* Y=NewObject<UMaterialExpressionConstant3Vector>(Mat);
    Y->Material=Mat;Y->Constant=FLinearColor(0,1,0,1);Mat->GetExpressionCollection().AddExpression(Y);
    UMaterialExpressionTransform* Bitangent=NewObject<UMaterialExpressionTransform>(Mat);
    Bitangent->TransformSourceType=TRANSFORMSOURCE_Tangent;Bitangent->TransformType=TRANSFORM_World;Bitangent->Input.Expression=Y;
    Add(TEXT("Bitangent"),Bitangent);
    FString Code=TEXT("const float UVFlip=1.0;\nfloat4 v1=float4(UV.x,1.0-UV.y,0,0);\nfloat4 v2=float4(-Position.x,Position.z,Position.y,100.0)*0.01;\nfloat4 v3=float4(-Normal.x,Normal.z,Normal.y,0);\nfloat3 t=float3(-Tangent.x,Tangent.z,Tangent.y);\nfloat3 b=float3(-Bitangent.x,Bitangent.z,Bitangent.y);\nfloat4 v4=float4(t,sign(dot(cross(v3.xyz,t),b)));\nfloat4 v5=float4(normalize(b),0);\n");
    Code+=bShadow?TEXT("float4 v6=float4(0,0,0,1);float4 v7=float4(asfloat(FaceSign>0?0xffffffffu:0u),0,0,0);\n"):TEXT("float4 v6=float4(asfloat(FaceSign>0?0xffffffffu:0u),0,0,0);\n");
    TArray<int32> Sizes;
    if(bShadow) Sizes.Append(GForgeAlphaShadowCBSize,UE_ARRAY_COUNT(GForgeAlphaShadowCBSize));else Sizes.Append(GForgeAlphaForwardCBSize,UE_ARRAY_COUNT(GForgeAlphaForwardCBSize));
    for(int32 I=0;I<Sizes.Num();++I)
    {
        Code+=FString::Printf(TEXT("float4 cb%d[%d];\n"),I,Sizes[I]);
        for(int32 J=0;J<Sizes[I];++J) Code+=FString::Printf(TEXT("cb%d[%d]=0.0;\n"),I,J);
    }
    auto Bind=[&](const FOKForgeAlphaUniform& U)
    {
        FLinearColor V=ForgeAlphaValue(Draw,U.Name);
        if(const FOKForgeAlphaProperty* P=ForgeAlphaProperty(U.Name);P && P->Gamma) {V.R=ForgeAlphaLinear(V.R);V.G=ForgeAlphaLinear(V.G);V.B=ForgeAlphaLinear(V.B);}
        const FString Name=U.Name;
        if(Name==TEXT("_LightColor0")) V=FLinearColor::White;
        // Camera-parented maker light. The constant on the draw is not used.
        if(Name==TEXT("_ambientshadowG")) {V=Draw.AmbientShadowColorUnity;V.A=Draw.ShadowDepth;}
        if(Name==TEXT("_LineColorG")) V=FLinearColor(.5f,.5f,.5f,1.f-Draw.LineDepth);
        if(Name==TEXT("unity_WorldTransformParams")) V=FLinearColor(0,0,0,1);
        if(Name==TEXT("unity_SpecCube0_HDR")) V=FLinearColor(1,1,0,1);
        if(U.Rows)
        {
            // Unity camera view rows in the reflected Unity world basis.
            Code+=TEXT("float3 vr=float3(-ResolvedView.ViewRight.x,ResolvedView.ViewRight.z,ResolvedView.ViewRight.y);\nfloat3 vu=float3(-ResolvedView.ViewUp.x,ResolvedView.ViewUp.z,ResolvedView.ViewUp.y);\nfloat3 vf=float3(ResolvedView.ViewForward.x,-ResolvedView.ViewForward.z,-ResolvedView.ViewForward.y);\n");
            for(int32 Row=0;Row<4;++Row) Code+=FString::Printf(TEXT("cb%d[%d]=%s;\n"),U.CB,U.Offset/16+Row,Row<3?*FString::Printf(TEXT("float4(vr[%d],vu[%d],vf[%d],0)"),Row,Row,Row):TEXT("float4(0,0,0,1)"));
            return;
        }
        const FString ExposedInput=OKExposedUniformInput(Mat,PS,Draw.Exposed,Name,U.Dim,V);
        for(int32 C=0;C<U.Dim;++C)
        {
            FString Expr=ForgeAlphaBits((&V.R)[C]);
            if(U.Type==1 || U.Type==2) Expr=FString::Printf(TEXT("asfloat(%du)"),U.Type==2?int32((&V.R)[C]!=0):int32((&V.R)[C]));
            if(!ExposedInput.IsEmpty()) Expr=OKExposedComponent(ExposedInput,C,U.Type);
            if(Name==TEXT("_Time")) Expr=C==0?TEXT("FrameTime*0.05"):C==1?TEXT("FrameTime"):C==2?TEXT("FrameTime*2.0"):TEXT("FrameTime*3.0");
            if(Name==TEXT("_WorldSpaceCameraPos")) Expr=C==0?TEXT("-CameraPosition.x*0.01"):C==1?TEXT("CameraPosition.z*0.01"):TEXT("CameraPosition.y*0.01");
            if(Name==TEXT("_WorldSpaceLightPos0")) Expr=C==0?TEXT("ResolvedView.ViewForward.x"):C==1?TEXT("-ResolvedView.ViewForward.z"):C==2?TEXT("-ResolvedView.ViewForward.y"):TEXT("0.0");
            Code+=FString::Printf(TEXT("cb%d[%d].%c=%s;\n"),U.CB,(U.Offset/4+C)/4,TEXT("xyzw")[(U.Offset/4+C)%4],*Expr);
        }
    };
    if(bShadow) for(const auto& U:GForgeAlphaShadowUniforms) Bind(U);else for(const auto& U:GForgeAlphaForwardUniforms) Bind(U);
    FString Program=bShadow?GForgeAlphaShadowProgram:GForgeAlphaForwardProgram;
    if(bShadow)
    {
        UMaterialExpressionTextureObjectParameter* Depth=NewObject<UMaterialExpressionTextureObjectParameter>(Mat);
        Depth->Texture=Draw.ForgeShadowDepth;Depth->ParameterName=TEXT("KKShadowDepth");Depth->SamplerType=SAMPLERTYPE_LinearColor;
        Add(TEXT("ShadowDepth"),Depth);
        auto Vector=[&](const TCHAR* Name,const FVector& V) {auto* E=NewObject<UMaterialExpressionConstant3Vector>(Mat);E->Constant=FLinearColor(V.X,V.Y,V.Z,1);Add(Name,E);};
        Vector(TEXT("ShadowCenter"),Draw.ForgeShadowCenter);Vector(TEXT("ShadowRight"),Draw.ForgeShadowRight);
        Vector(TEXT("ShadowUp"),Draw.ForgeShadowUp);Vector(TEXT("ShadowLight"),Draw.ForgeShadowLight);
        Code+=FString::Printf(TEXT("float3 sd=Position-ShadowCenter;float2 su=float2(.5+dot(sd,ShadowRight)/%s,.5-dot(sd,ShadowUp)/%s);float receiver=%s-dot(sd,ShadowLight);float visibility=1.0;if(all(su>=0)&&all(su<=1)){float4 depths=float4(ShadowDepth.SampleLevel(ShadowDepthSampler,su+float2(-1,-1)/2048.0,0).r,ShadowDepth.SampleLevel(ShadowDepthSampler,su+float2(1,-1)/2048.0,0).r,ShadowDepth.SampleLevel(ShadowDepthSampler,su+float2(-1,1)/2048.0,0).r,ShadowDepth.SampleLevel(ShadowDepthSampler,su+float2(1,1)/2048.0,0).r);visibility=dot(step(receiver,depths+%s),float4(.25,.25,.25,.25));}\n"),*ForgeAlphaBits(Draw.ForgeShadowWidth),*ForgeAlphaBits(Draw.ForgeShadowWidth),*ForgeAlphaBits(Draw.ForgeShadowDistance),*ForgeAlphaBits(OKToonShadowBias()));
        const FString Sample=TEXT("Texture6.Sample(Sampler0,float2((r5.xy).x,lerp((r5.xy).y,1.0-(r5.xy).y,UVFlip)))");
        check(Program.Contains(Sample));Program.ReplaceInline(*Sample,TEXT("float4(visibility,visibility,visibility,visibility)"));
    }
    auto BindTexture=[&](const FOKForgeAlphaTexture& T)
    {
        if(FString(T.Name)==TEXT("_ShadowMapTexture")) return;
        UTexture* Texture=nullptr;
        if(const auto* Found=Draw.Textures.Find(T.Name)) Texture=Found->Get();
        if(!Texture) {const auto* P=ForgeAlphaProperty(T.Name);Texture=ForgeAlphaDefaultTexture(P?P->ForgeAlphaDefaultTexture:TEXT("white"),T.Dim==4);}
        UMaterialExpressionTextureBase* Object=nullptr;
        if(FString(T.Name)==TEXT("_MainTex")){
            auto* Parameter=NewObject<UMaterialExpressionTextureObjectParameter>(Mat);
            Parameter->ParameterName=TEXT("KKAppearanceMain");Object=Parameter;
        }else Object=OKNewTextureObject(Mat,Draw.Exposed,T.Name);
        Object->Texture=Texture;Object->SamplerType=Texture && Texture->SRGB?SAMPLERTYPE_Color:SAMPLERTYPE_LinearColor;
        Add(*FString::Printf(TEXT("Texture%d"),T.Index),Object);
        if(T.Sampler>=0) Program.ReplaceInline(*FString::Printf(TEXT("Sampler%d,"),T.Sampler),*FString::Printf(TEXT("Texture%dSampler,"),T.Index));
    };
    if(bShadow) for(const auto& T:GForgeAlphaShadowTextures) BindTexture(T);else for(const auto& T:GForgeAlphaForwardTextures) BindTexture(T);
    PS->Code=Code+Program;Mat->GetExpressionCollection().AddExpression(PS);
    if(FParse::Param(FCommandLine::Get(),TEXT("OKNativeItemAudit")))
    {
        const FString Directory=FPaths::ProjectSavedDir()/TEXT("Tests/ForgeAlpha333/UEProgram");
        IFileManager::Get().MakeDirectory(*Directory,true);
        FFileHelper::SaveStringToFile(PS->Code,*(Directory/(bShadow?TEXT("Shadow.hlsl"):TEXT("Forward.hlsl"))));
    }
    if(auto* Ed=Mat->GetEditorOnlyData())
    {
        Ed->EmissiveColor.Expression=PS;Ed->EmissiveColor.Mask=1;Ed->EmissiveColor.MaskR=Ed->EmissiveColor.MaskG=Ed->EmissiveColor.MaskB=1;
        if(bOpaque) {
            auto* Opaque=NewObject<UMaterialExpressionConstant>(Mat);Opaque->Material=Mat;Opaque->R=1.f;
            Mat->GetExpressionCollection().AddExpression(Opaque);Ed->OpacityMask.Expression=Opaque;
        } else {Ed->Opacity.Expression=PS;Ed->Opacity.Mask=1;Ed->Opacity.MaskA=1;}
    }
    // An equal graph finalized before (OK_MATERIAL_DEDUPE=1) is used as is.
    OKAddUnityTranslucentDepthTest(Mat);FString DedupeKey;if(UMaterial* Same=OKFindEqualMaterial(Mat,TEXT("KKForgeAlpha"),DedupeKey))return Same;
    Mat->UpdateCachedExpressionData();Mat->PreEditChange(nullptr);OKEnsureSkeletalMaterialUsage(Mat);Mat->PostEditChange();OKRememberMaterial(DedupeKey,Mat);
    UE_LOG(LogTemp,Display,TEXT("OK native Forge MainAlpha shadow=%d uniforms=%d textures=%d"),bShadow,Draw.Values.Num(),Draw.Textures.Num());
    return Mat;

#else
return nullptr; // Runtime characters bind the cooked shared parent family.
#endif
}
