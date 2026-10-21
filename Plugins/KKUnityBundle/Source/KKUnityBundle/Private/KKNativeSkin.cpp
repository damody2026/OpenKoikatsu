#include "KKNativeSkin.h"
#include "KKSkeletalBuilder.h"
#include "KKMaterialDedupe.h"
#include "KKMaterialExposure.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/VolumeTexture.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionVertexTangentWS.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionTransform.h"

UMaterial* OKMakeNativeSkinMaterial(const FKKRuntimeMaterialPart& Part,
    const TMap<FString,TObjectPtr<UTexture2D>>& Textures,const FKKRuntimeMaterialScene& Scene,FString& Error,const TSet<FString>* Exposed)
{
#if WITH_EDITOR
    const auto Recipe=OKRuntimeSharedMaterialRecipe(Part);
    if(Recipe!=EKKRuntimeMaterialRecipe::KoikanoSkin){Error=TEXT("Unsupported native preview skin recipe");return nullptr;}
    FKKRuntimeMaterialBinding Binding;
    if(!OKPrepareRuntimeSharedMaterialBinding(Part,Scene,Binding,Error))return nullptr;
    const auto& Layout=*OKRuntimeSharedMaterialLayout(Recipe);
    auto* Mat=NewObject<UMaterial>(GetTransientPackage(),NAME_None,RF_Transient);
    Mat->bIsPreviewMaterial=true;Mat->MaterialDomain=MD_Surface;Mat->BlendMode=BLEND_Masked;
    Mat->SetShadingModel(MSM_Unlit);Mat->TwoSided=false;Mat->NumCustomizedUVs=4;
    for(auto Usage:{MATUSAGE_SkeletalMesh,MATUSAGE_StaticMesh,MATUSAGE_MorphTargets})Mat->SetUsageByFlag(Usage,true);
    auto* PS=NewObject<UMaterialExpressionCustom>(Mat);PS->OutputType=CMOT_Float4;
    PS->Description=TEXT("Installed skin program / shared runtime schema");
    auto Register=[&](UMaterialExpression* E){E->Material=Mat;Mat->GetExpressionCollection().AddExpression(E);};
    auto Add=[&](const FString& N,UMaterialExpression* E){Register(E);auto& I=PS->Inputs.AddDefaulted_GetRef();I.InputName=*N;I.Input.Expression=E;};
    const TCHAR* UVNames[]={TEXT("UV"),TEXT("SecondaryUV"),TEXT("ThirdUV"),TEXT("FourthUV")};
    for(int32 I=0;I<4;++I){auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(Mat);UV->CoordinateIndex=I;Add(UVNames[I],UV);}
    Add(TEXT("Position"),NewObject<UMaterialExpressionWorldPosition>(Mat));
    Add(TEXT("CameraPosition"),NewObject<UMaterialExpressionCameraPositionWS>(Mat));
    Add(TEXT("Normal"),NewObject<UMaterialExpressionVertexNormalWS>(Mat));
    Add(TEXT("Tangent"),NewObject<UMaterialExpressionVertexTangentWS>(Mat));
    Add(TEXT("FaceSign"),NewObject<UMaterialExpressionTwoSidedSign>(Mat));
    auto* Color=NewObject<UMaterialExpressionVertexColor>(Mat);Register(Color);
    auto* RGBA=NewObject<UMaterialExpressionAppendVector>(Mat);RGBA->A.Expression=Color;RGBA->B.Expression=Color;RGBA->B.OutputIndex=4;Add(TEXT("VertexColor"),RGBA);
    auto* Y=NewObject<UMaterialExpressionConstant3Vector>(Mat);Y->Constant=FLinearColor(0,1,0,1);Register(Y);
    auto* Bitangent=NewObject<UMaterialExpressionTransform>(Mat);Bitangent->TransformSourceType=TRANSFORMSOURCE_Tangent;Bitangent->TransformType=TRANSFORM_World;Bitangent->Input.Expression=Y;Add(TEXT("Bitangent"),Bitangent);
    FString Code=TEXT("const float UVFlip=1;float4 v1=float4(UV.x,1-UV.y,0,0),v2=0,v3=0,v4=0,v5=0,v6=0;float3 t=float3(-Tangent.x,Tangent.z,Tangent.y),b=float3(-Bitangent.x,Bitangent.z,Bitangent.y);\n");
    auto Bits=[](float Value){uint32 Word;FMemory::Memcpy(&Word,&Value,4);return FString::Printf(TEXT("asfloat(0x%08xu)"),Word);};
    for(int32 I=0;I<Layout.Sizes.Num();++I){Code+=FString::Printf(TEXT("float4 cb%d[%d];\n"),I,Layout.Sizes[I]);for(int32 J=0;J<Layout.Sizes[I];++J)Code+=FString::Printf(TEXT("cb%d[%d]=0;\n"),I,J);}
    for(const auto& U:Layout.Uniforms){
        if(U.Rows){
            if(U.Name!=TEXT("unity_ProbeVolumeWorldToObject")||U.Rows!=4){Error=TEXT("Unexpected skin matrix");return nullptr;}
            for(int32 Row=0;Row<4;++Row)Code+=FString::Printf(TEXT("cb%d[%d]=float4(%d,%d,%d,%d);\n"),U.CB,U.Offset/16+Row,Row==0,Row==1,Row==2,Row==3);
            continue;
        }
        FLinearColor V=U.Default;
        if(const auto* Vector=Binding.Vectors.Find(*U.Name))V=*Vector;
        if(const auto* Scalar=Binding.Scalars.Find(*U.Name))V.R=*Scalar;
        const FString ExposedInput=OKExposedUniformInput(Mat,PS,Exposed,U.Name,U.Dim,V);
        for(int32 C=0;C<U.Dim;++C){
            FString Value=Bits((&V.R)[C]);
            if(U.Type==1||U.Type==2)Value=FString::Printf(TEXT("asfloat(%du)"),U.Type==2?int32((&V.R)[C]!=0):int32((&V.R)[C]));
            if(!ExposedInput.IsEmpty())Value=OKExposedComponent(ExposedInput,C,U.Type);
            if(U.Name==TEXT("_WorldSpaceCameraPos"))Value=C==0?TEXT("-CameraPosition.x*.01"):C==1?TEXT("CameraPosition.z*.01"):TEXT("CameraPosition.y*.01");
            // Character maker parents the body light to the camera. Unity to-light
            // is the opposite of view forward, in the same axis swap as position.
            if(U.Name==TEXT("_WorldSpaceLightPos0"))Value=C==0?TEXT("ResolvedView.ViewForward.x"):C==1?TEXT("-ResolvedView.ViewForward.z"):C==2?TEXT("-ResolvedView.ViewForward.y"):TEXT("0");
            Code+=FString::Printf(TEXT("cb%d[%d].%c=%s;\n"),U.CB,(U.Offset/4+C)/4,TEXT("xyzw")[(U.Offset/4+C)%4],*Value);
        }
    }
    auto& AmbientInput=PS->Inputs.AddDefaulted_GetRef();AmbientInput.InputName=TEXT("VertexAmbient");
    AmbientInput.Input.Expression=OKMakeRuntimeVertexAmbient(Mat,Scene);
    Code+=OKRuntimeSharedMaterialVaryings(Recipe);
    FString Program=OKRuntimeSharedMaterialProgram(Recipe);
    static TMap<FString,UTexture*> Defaults;
    for(const auto& Slot:Layout.Textures){
        UTexture* Texture=nullptr;if(const auto* Found=Textures.Find(Slot.Name))Texture=Found->Get();
        if(!Texture){
            const FString Key=Slot.Dim==4?TEXT("cube"):Slot.Dim==3?TEXT("volume"):Slot.Default;
            if(auto** Hit=Defaults.Find(Key))Texture=*Hit;
            else {
                if(Slot.Dim==4){auto* T=NewObject<UTextureCube>();uint8 Zero[24]={};T->Source.Init(1,1,6,1,TSF_BGRA8,Zero);Texture=T;}
                else if(Slot.Dim==3){auto* T=NewObject<UVolumeTexture>();uint8 Zero[4]={};T->Source.Init(1,1,1,1,TSF_BGRA8,Zero);Texture=T;}
                else {auto* T=NewObject<UTexture2D>();FColor Pixel=Key==TEXT("white")?FColor::White:Key==TEXT("bump")?FColor(255,128,255,128):Key==TEXT("gray")?FColor(128,128,128,128):FColor(0,0,0,0);T->Source.Init(1,1,1,1,TSF_BGRA8,reinterpret_cast<uint8*>(&Pixel));Texture=T;}
                Texture->SRGB=false;Texture->CompressionSettings=TC_VectorDisplacementmap;Texture->MipGenSettings=TMGS_NoMipmaps;Texture->Filter=TF_Bilinear;Texture->PostEditChange();Texture->AddToRoot();Defaults.Add(Key,Texture);
            }
        }
        UMaterialExpressionTextureBase* Object=nullptr;
        if(Slot.Name==TEXT("_AlphaMask")){auto* Parameter=NewObject<UMaterialExpressionTextureObjectParameter>(Mat);Parameter->ParameterName=TEXT("_AlphaMask");Object=Parameter;}
        else Object=OKNewTextureObject(Mat,Exposed,Slot.Name);
        Object->Texture=Texture;Object->SamplerType=Texture->SRGB?SAMPLERTYPE_Color:SAMPLERTYPE_LinearColor;Add(FString::Printf(TEXT("Texture%d"),Slot.Index),Object);
        if(Slot.Sampler>=0)Program.ReplaceInline(*FString::Printf(TEXT("Sampler%d,"),Slot.Sampler),*FString::Printf(TEXT("Texture%dSampler,"),Slot.Index));
    }
    PS->Code=Code+Program;Register(PS);
    auto* Ed=Mat->GetEditorOnlyData();Ed->EmissiveColor.Expression=PS;Ed->EmissiveColor.Mask=1;Ed->EmissiveColor.MaskR=Ed->EmissiveColor.MaskG=Ed->EmissiveColor.MaskB=1;
    auto* Opaque=NewObject<UMaterialExpressionConstant>(Mat);Opaque->R=1;Register(Opaque);Ed->OpacityMask.Expression=Opaque;
    // An equal graph finalized before (OK_MATERIAL_DEDUPE=1) is used as is.
    FString DedupeKey;if(UMaterial* Same=OKFindEqualMaterial(Mat,TEXT("KKNativeSkin"),DedupeKey)){Error.Reset();return Same;}
    Mat->UpdateCachedExpressionData();Mat->PreEditChange(nullptr);OKEnsureSkeletalMaterialUsage(Mat);Mat->PostEditChange();OKRememberMaterial(DedupeKey,Mat);
    Error.Reset();return Mat;
#else
    Error=TEXT("Runtime uses cooked common skin parent");return nullptr;
#endif
}
