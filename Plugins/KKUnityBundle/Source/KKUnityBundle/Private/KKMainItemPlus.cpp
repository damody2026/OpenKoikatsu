#include "KKMainItemPlus.h"
#include "KKSkeletalBuilder.h"
#include "KKMaterialDedupe.h"
#include "KKMaterialExposure.h"
#include "KKRuntimeSharedMaterial.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionVertexTangentWS.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

struct FOKItemUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKItemTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKItemProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* DefaultTexture;};
#include "Shaders/MainItemPlusBindings.inl"
#include "Shaders/MainItemPlusProgram.inl"

static const FOKItemProperty* Property(const FString& Name)
{
    for (const auto& P:GItemProperties) if(Name==P.Name) return &P;
    return nullptr;
}
static FLinearColor Value(const FOKNativeItemDraw& Draw,const FString& Name)
{
    if(const FLinearColor* V=Draw.Values.Find(Name)) return *V;
    if(const FOKItemProperty* P=Property(Name)) return P->Default;
    // KKPItemDiffuse.cginc initializes this non-Properties uniform to 1e-10.
    // Zero makes RGBtoHCV divide 0/0 on achromatic albedo (e.g. null MainTex).
    if(Name==TEXT("Epsilon")) return FLinearColor(1e-10f,0,0,0);
    if(Name.EndsWith(TEXT("_ST"))) return FLinearColor(1,1,0,0);
    return FLinearColor(0,0,0,0);
}
static FString Bits(float Value)
{
    uint32 Word;FMemory::Memcpy(&Word,&Value,4);
    return FString::Printf(TEXT("asfloat(0x%08xu)"),Word);
}
static float Linear(float V)
{
    return V<=0.04045f ? V/12.92f : FMath::Pow((V+0.055f)/1.055f,2.4f);
}
TArray<FString> OKMainItemPlusUniformNames()
{
    TArray<FString> Names;
    for(const auto& U:GForwardUniforms) Names.AddUnique(U.Name);
    for(const auto& U:GOutlineUniforms) Names.AddUnique(U.Name);
    Names.AddUnique(TEXT("_CullOption"));Names.AddUnique(TEXT("_LineWidthS"));
    return Names;
}
TArray<FString> OKMainItemPlusTextureNames()
{
    TArray<FString> Names;
    for(const auto& T:GForwardTextures) if(T.Dim==2 && FString(T.Name)!=TEXT("_RampG")) Names.AddUnique(T.Name);
    return Names;
}
bool OKMainItemPlusTextureIsColor(const FString& Name)
{
    return Name==TEXT("_MainTex") || Name==TEXT("_EmissionMask") || Name==TEXT("_ReflectionMapCap");
}
static UTexture* DefaultTexture(const FString& Name,bool bCube=false)
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

UMaterial* OKMakeMainItemPlusMaterial(const FOKNativeItemDraw& Draw,bool bOutline)
{
#if WITH_EDITOR

    UMaterial* Mat=NewObject<UMaterial>(GetTransientPackage(),NAME_None,RF_Transient);
    // Fresh transient preview materials have no existing users to recreate.
    Mat->bIsPreviewMaterial=true;
    Mat->MaterialDomain=MD_Surface;Mat->BlendMode=BLEND_Masked;Mat->SetShadingModel(MSM_Unlit);
    const float Cull=Value(Draw,TEXT("_CullOption")).R;
    Mat->TwoSided=!bOutline && Cull!=2.f;Mat->OpacityMaskClipValue=0.5f;
    Mat->SetUsageByFlag(MATUSAGE_SkeletalMesh,true);Mat->SetUsageByFlag(MATUSAGE_StaticMesh,true);Mat->SetUsageByFlag(MATUSAGE_MorphTargets,true);
    UMaterialExpressionCustom* PS=NewObject<UMaterialExpressionCustom>(Mat);
    PS->Material=Mat;PS->OutputType=CMOT_Float4;PS->Description=TEXT("Installed MainItemPlus D3D11 program");
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
    FString Code=TEXT("const float UVFlip=1.0;\nfloat4 v1=float4(UV.x,1.0-UV.y,0,0);\nfloat4 v2=float4(-Position.x,Position.z,Position.y,100.0)*0.01;\nfloat4 v3=float4(-Normal.x,Normal.z,Normal.y,0);\nfloat3 t=float3(-Tangent.x,Tangent.z,Tangent.y);\nfloat3 b=float3(-Bitangent.x,Bitangent.z,Bitangent.y);\nfloat4 v4=float4(t,sign(dot(cross(v3.xyz,t),b)));\nfloat4 v5=float4(normalize(cross(t,v3.xyz)),0);\nfloat4 v6=float4(asfloat(FaceSign>0?0xffffffffu:0u),0,0,0);\n");
    if(!bOutline && Cull==1.f) Code+=TEXT("if(FaceSign>0) clip(-1.0);\n");
    TArray<int32> Sizes;
    if(bOutline) Sizes.Append(GOutlineCBSize,UE_ARRAY_COUNT(GOutlineCBSize));else Sizes.Append(GForwardCBSize,UE_ARRAY_COUNT(GForwardCBSize));
    for(int32 I=0;I<Sizes.Num();++I)
    {
        Code+=FString::Printf(TEXT("float4 cb%d[%d];\n"),I,Sizes[I]);
        for(int32 J=0;J<Sizes[I];++J) Code+=FString::Printf(TEXT("cb%d[%d]=0.0;\n"),I,J);
    }
    auto Bind=[&](const FOKItemUniform& U)
    {
        FLinearColor V=Value(Draw,U.Name);
        if(const FOKItemProperty* P=Property(U.Name);P && P->Gamma) {V.R=Linear(V.R);V.G=Linear(V.G);V.B=Linear(V.B);}
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
            FString Expr=Bits((&V.R)[C]);
            if(U.Type==1 || U.Type==2) Expr=FString::Printf(TEXT("asfloat(%du)"),U.Type==2?int32((&V.R)[C]!=0):int32((&V.R)[C]));
            if(!ExposedInput.IsEmpty()) Expr=OKExposedComponent(ExposedInput,C,U.Type);
            if(Name==TEXT("_Time")) Expr=C==0?TEXT("FrameTime*0.05"):C==1?TEXT("FrameTime"):C==2?TEXT("FrameTime*2.0"):TEXT("FrameTime*3.0");
            if(Name==TEXT("_WorldSpaceCameraPos")) Expr=C==0?TEXT("-CameraPosition.x*0.01"):C==1?TEXT("CameraPosition.z*0.01"):TEXT("CameraPosition.y*0.01");
            if(Name==TEXT("_WorldSpaceLightPos0")) Expr=C==0?TEXT("ResolvedView.ViewForward.x"):C==1?TEXT("-ResolvedView.ViewForward.z"):C==2?TEXT("-ResolvedView.ViewForward.y"):TEXT("0.0");
            Code+=FString::Printf(TEXT("cb%d[%d].%c=%s;\n"),U.CB,(U.Offset/4+C)/4,TEXT("xyzw")[(U.Offset/4+C)%4],*Expr);
        }
    };
    if(bOutline) for(const auto& U:GOutlineUniforms) Bind(U);else for(const auto& U:GForwardUniforms) Bind(U);
    FString Program=bOutline?GMainItemPlusOutline:GMainItemPlusForward;
    auto BindTexture=[&](const FOKItemTexture& T)
    {
        UTexture* Texture=nullptr;
        if(const auto* Found=Draw.Textures.Find(T.Name)) Texture=Found->Get();
        if(!Texture) {const auto* P=Property(T.Name);Texture=DefaultTexture(P?P->DefaultTexture:TEXT("white"),T.Dim==4);}
        UMaterialExpressionTextureBase* Object=OKNewTextureObject(Mat,Draw.Exposed,T.Name);
        Object->Texture=Texture;Object->SamplerType=Texture && Texture->SRGB?SAMPLERTYPE_Color:SAMPLERTYPE_LinearColor;
        Add(*FString::Printf(TEXT("Texture%d"),T.Index),Object);
        if(T.Sampler>=0) Program.ReplaceInline(*FString::Printf(TEXT("Sampler%d,"),T.Sampler),*FString::Printf(TEXT("Texture%dSampler,"),T.Index));
    };
    if(bOutline) for(const auto& T:GOutlineTextures) BindTexture(T);else for(const auto& T:GForwardTextures) BindTexture(T);
    PS->Code=Code+Program;Mat->GetExpressionCollection().AddExpression(PS);
    if(FParse::Param(FCommandLine::Get(),TEXT("OKNativeItemAudit")))
    {
        const FString Directory=FPaths::ProjectSavedDir()/TEXT("Tests/MainItemPlus310/UEProgram");
        IFileManager::Get().MakeDirectory(*Directory,true);
        FFileHelper::SaveStringToFile(PS->Code,*(Directory/(bOutline?TEXT("Outline.hlsl"):TEXT("Forward.hlsl"))));
    }
    if(auto* Ed=Mat->GetEditorOnlyData())
    {
        Ed->EmissiveColor.Expression=PS;Ed->EmissiveColor.Mask=1;Ed->EmissiveColor.MaskR=Ed->EmissiveColor.MaskG=Ed->EmissiveColor.MaskB=1;
        Ed->OpacityMask.Expression=PS;Ed->OpacityMask.Mask=1;Ed->OpacityMask.MaskA=1;
        // The installed outline vertex program of the part's family (as the shared outline parents extrude).
        UMaterialExpression* Native=nullptr;
        if(bOutline&&Draw.OutlinePart)
        {
            FKKRuntimeMaterialScene Scene;Scene.Globals=Draw.OutlineGlobals;FString Error;
            Native=OKMakeRuntimeOutlineVertexOffset(Mat,*Draw.OutlinePart,Scene,[&](const FString& Property)->UTexture*{const auto* Found=Draw.Textures.Find(Property);return Found?Found->Get():nullptr;},Error);
            UE_LOG(LogTemp,Display,TEXT("OK outline width: %s part=%s"),Native?TEXT("installed vertex program"):*(TEXT("approximate (")+Error+TEXT(")")),*Draw.OutlinePart->Name);
        }
        if(Native)Ed->WorldPositionOffset.Expression=Native;
        else if(bOutline)
        {
            UMaterialExpressionCustom* Offset=NewObject<UMaterialExpressionCustom>(Mat);Offset->Material=Mat;Offset->OutputType=CMOT_Float3;
            Offset->Code=FString::Printf(TEXT("float2 uv=float2(UV.x,1.0-UV.y);float4 st=float4(%s,%s,%s,%s);uv=uv*st.xy+st.zw;float d=Texture.SampleLevel(TextureSampler,float2(uv.x,1.0-uv.y),0).b;return Normal*(length(CameraPosition-Position)*0.001+0.3)*%s*0.5*(1.0-d)*%s;"),*Bits(Value(Draw,TEXT("_DetailMask_ST")).R),*Bits(Value(Draw,TEXT("_DetailMask_ST")).G),*Bits(Value(Draw,TEXT("_DetailMask_ST")).B),*Bits(Value(Draw,TEXT("_DetailMask_ST")).A),*Bits(Draw.LineWidth),*Bits(Value(Draw,TEXT("_LineWidthS")).R));
            for(const TCHAR* Name:{TEXT("UV"),TEXT("Position"),TEXT("CameraPosition"),TEXT("Normal")})
                for(const auto& I:PS->Inputs) if(I.InputName==Name) {auto& O=Offset->Inputs.AddDefaulted_GetRef();O=I;}
            auto* Object=NewObject<UMaterialExpressionTextureObject>(Mat);Object->Material=Mat;
            const auto* Found=Draw.Textures.Find(TEXT("_DetailMask"));Object->Texture=Found?Found->Get():DefaultTexture(TEXT("black"));Object->SamplerType=Object->Texture&&Object->Texture->SRGB?SAMPLERTYPE_Color:SAMPLERTYPE_LinearColor;Mat->GetExpressionCollection().AddExpression(Object);
            auto& TI=Offset->Inputs.AddDefaulted_GetRef();TI.InputName=TEXT("Texture");TI.Input.Expression=Object;
            Mat->GetExpressionCollection().AddExpression(Offset);Ed->WorldPositionOffset.Expression=Offset;
        }
    }
    // An equal graph finalized before (OK_MATERIAL_DEDUPE=1) is used as is.
    FString DedupeKey;if(UMaterial* Same=OKFindEqualMaterial(Mat,TEXT("KKMainItemPlus"),DedupeKey))return Same;
    Mat->UpdateCachedExpressionData();Mat->PreEditChange(nullptr);OKEnsureSkeletalMaterialUsage(Mat);Mat->PostEditChange();OKRememberMaterial(DedupeKey,Mat);
    UE_LOG(LogTemp,Display,TEXT("OK native MainItemPlus pass=%s uniforms=%d textures=%d cull=%.0f"),bOutline?TEXT("Outline"):TEXT("Forward"),Draw.Values.Num(),Draw.Textures.Num(),Cull);
    if(!bOutline && Value(Draw,TEXT("_UseMatCapReflection")).R<1.f && Value(Draw,TEXT("_ReflectiveBlend")).R>0.f)
        UE_LOG(LogTemp,Warning,TEXT("MainItemPlus Unity scene reflection cubemap unavailable; shader math retained, preview cubemap is black"));
    return Mat;

#else
return nullptr; // Runtime characters bind the cooked shared parent family.
#endif
}
