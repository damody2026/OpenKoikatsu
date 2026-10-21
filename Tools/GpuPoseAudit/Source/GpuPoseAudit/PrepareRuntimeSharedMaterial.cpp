#if OK_RUNTIME_SHARED_MATERIAL_TOOL
#include "KKRuntimeSharedMaterial.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionVertexTangentWS.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionTime.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/VolumeTexture.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Json.h"
#include "ShaderCompiler.h"
#include "MaterialShared.h"
#include "OKSourceMaterialMesh.h"
#include "Materials/MaterialRenderProxy.h"
#include "VertexFactory.h"
#if OK_HG_SHADING_TOOL
#include "OKHGShading.h"
#endif
#include "RenderingThread.h"

bool PrepareRuntimeSharedMaterial(const FString& AssetRoot,const FString& Root,const FString& Output,bool HGShading){
#if !OK_HG_SHADING_TOOL
 if(HGShading){UE_LOG(LogTemp,Error,TEXT("-HGShading needs the OKHGShading plugin in the material host"));return false;}
#else
 FString HGRelight;if(HGShading){FString HGError;if(!OKHGRelightSource(HGRelight,HGError)){UE_LOG(LogTemp,Error,TEXT("%s"),*HGError);return false;}}
#endif
 if(!Root.StartsWith(TEXT("/Game/IndependentResources/RuntimeMaterials/"))||!FPackageName::IsValidLongPackageName(Root))return false;
 FPackageName::RegisterMountPoint(TEXT("/Game/"),FPaths::ConvertRelativePathToFull(AssetRoot)/TEXT(""));if(IFileManager::Get().DirectoryExists(*FPackageName::LongPackageNameToFilename(Root)))return false;
 TArray<TStrongObjectPtr<UObject>> Keep;TArray<UObject*> Assets;TMap<FString,UTexture*> Defaults;
 auto Default=[&](const FKKRuntimeShaderTexture& Slot)->UTexture*{
  const FString Key=Slot.Dim==4?TEXT("cube"):Slot.Dim==3?TEXT("volume"):Slot.Default;if(auto** Hit=Defaults.Find(Key))return *Hit;
  const FString Name=TEXT("T_")+Key;auto* Package=CreatePackage(*(Root/Name));UTexture* Texture=nullptr;
  if(Slot.Dim==4){auto* T=NewObject<UTextureCube>(Package,*Name,RF_Public|RF_Standalone);uint8 Zero[24]={};T->Source.Init(1,1,6,1,TSF_BGRA8,Zero);Texture=T;}
  else if(Slot.Dim==3){auto* T=NewObject<UVolumeTexture>(Package,*Name,RF_Public|RF_Standalone);uint8 Zero[4]={};T->Source.Init(1,1,1,1,TSF_BGRA8,Zero);Texture=T;}
  else{auto* T=NewObject<UTexture2D>(Package,*Name,RF_Public|RF_Standalone);FColor Color=Key==TEXT("white")?FColor::White:Key==TEXT("bump")?FColor(255,128,255,128):Key==TEXT("gray")?FColor(128,128,128,128):Key==TEXT("red")?FColor(255,0,0,0):FColor(0,0,0,0);T->Source.Init(1,1,1,1,TSF_BGRA8,reinterpret_cast<uint8*>(&Color));Texture=T;}
  Texture->SRGB=false;Texture->CompressionSettings=TC_VectorDisplacementmap;Texture->MipGenSettings=TMGS_NoMipmaps;Texture->Filter=TF_Bilinear;Texture->PostEditChange();Keep.Emplace(Texture);Assets.Add(Texture);Defaults.Add(Key,Texture);return Texture;
 };
 for(auto Recipe:OKRuntimeSharedMaterialRecipes())for(bool Outline:{false,true}){
 if(Outline&&!OKRuntimeOutlineLayout(Recipe))continue;
 const bool Translucent=!Outline&&OKRuntimeSharedMaterialTranslucent(Recipe);
 const FString Name=OKRuntimeSharedMaterialParentName(Recipe)+(Outline?TEXT("_Outline"):TEXT(""));auto* Mat=NewObject<UMaterial>(CreatePackage(*(Root/Name)),*Name,RF_Public|RF_Standalone);Keep.Emplace(Mat);Assets.Add(Mat);
 Mat->MaterialDomain=MD_Surface;Mat->BlendMode=Outline?BLEND_Masked:OKRuntimeSharedMaterialBlendMode(Recipe);Mat->SetShadingModel(MSM_Unlit);Mat->TwoSided=Outline||OKRuntimeSharedMaterialTwoSided(Recipe);Mat->OpacityMaskClipValue=.5f;
 // The native vertex recipes consume independent UV0..UV3. Declare their
 // vertex pass-through explicitly; pixel custom inputs alone are insufficient
 // to initialize all FMaterialVertexParameters texture coordinates.
 Mat->NumCustomizedUVs=4;
 // Resolve coplanar main/depth vertex-program roundoff without moving visible geometry.
 for(auto Usage:{MATUSAGE_SkeletalMesh,MATUSAGE_StaticMesh,MATUSAGE_MorphTargets,MATUSAGE_InstancedSkinnedMesh})Mat->SetUsageByFlag(Usage,true);
 auto* PS=NewObject<UMaterialExpressionCustom>(Mat);PS->OutputType=CMOT_Float4;PS->Description=TEXT("Shared runtime ")+Name+TEXT(" schema1")+(HGShading?TEXT(" hg"):TEXT(""));
 auto Register=[&](UMaterialExpression* E){E->Material=Mat;Mat->GetExpressionCollection().AddExpression(E);};
 auto Add=[&](const FString& N,UMaterialExpression* E){Register(E);auto& I=PS->Inputs.AddDefaulted_GetRef();I.InputName=*N;I.Input.Expression=E;};
 Add(TEXT("UV"),NewObject<UMaterialExpressionTextureCoordinate>(Mat));if(int32(Recipe)>=13||Recipe==EKKRuntimeMaterialRecipe::ForgeHair||Recipe==EKKRuntimeMaterialRecipe::ForgeHairFront||Recipe==EKKRuntimeMaterialRecipe::ForgeEye||Recipe==EKKRuntimeMaterialRecipe::KoikanoEye){auto* UV1=NewObject<UMaterialExpressionTextureCoordinate>(Mat);UV1->CoordinateIndex=1;Add(TEXT("SecondaryUV"),UV1);}Add(TEXT("Position"),NewObject<UMaterialExpressionWorldPosition>(Mat));Add(TEXT("CameraPosition"),NewObject<UMaterialExpressionCameraPositionWS>(Mat));Add(TEXT("Normal"),NewObject<UMaterialExpressionVertexNormalWS>(Mat));Add(TEXT("Tangent"),NewObject<UMaterialExpressionVertexTangentWS>(Mat));Add(TEXT("FaceSign"),NewObject<UMaterialExpressionTwoSidedSign>(Mat));Add(TEXT("FrameTime"),NewObject<UMaterialExpressionTime>(Mat));
 if(int32(Recipe)>=13){auto* UV2=NewObject<UMaterialExpressionTextureCoordinate>(Mat);UV2->CoordinateIndex=2;Add(TEXT("ThirdUV"),UV2);auto* UV3=NewObject<UMaterialExpressionTextureCoordinate>(Mat);UV3->CoordinateIndex=3;Add(TEXT("FourthUV"),UV3);auto* Color=NewObject<UMaterialExpressionVertexColor>(Mat);Register(Color);auto* RGBA=NewObject<UMaterialExpressionAppendVector>(Mat);RGBA->A.Expression=Color;RGBA->B.Expression=Color;RGBA->B.OutputIndex=4;Add(TEXT("VertexColor"),RGBA);}
 auto* Y=NewObject<UMaterialExpressionConstant3Vector>(Mat);Y->Constant=FLinearColor(0,1,0,1);Register(Y);auto* Bitangent=NewObject<UMaterialExpressionTransform>(Mat);Bitangent->TransformSourceType=TRANSFORMSOURCE_Tangent;Bitangent->TransformType=TRANSFORM_World;Bitangent->Input.Expression=Y;Add(TEXT("Bitangent"),Bitangent);
 auto Scalar=[&](const FString& N,float V){auto* E=NewObject<UMaterialExpressionScalarParameter>(Mat);E->ParameterName=*N;E->DefaultValue=V;return E;};
 Add(TEXT("Schema"),Scalar(TEXT("__OK_RuntimeMaterialSchema"),1));Add(TEXT("Family"),Scalar(TEXT("__OK_RuntimeMaterialFamily"),(float(Recipe)+(Outline?128:0))));
 Add(TEXT("RevealStart"),Scalar(TEXT("__OK_RuntimeRevealStart"),-1));
 Add(TEXT("RevealDuration"),Scalar(TEXT("__OK_RuntimeRevealDuration"),3));
 Add(TEXT("RevealOverride"),Scalar(TEXT("__OK_RuntimeRevealOverride"),-1));
 // Opt-in source-input inspection. Normal rendering always uses mode zero.
 const bool ForgeHairDebug=!Outline&&(Recipe==EKKRuntimeMaterialRecipe::ForgeHair||Recipe==EKKRuntimeMaterialRecipe::ForgeHairFront);
 if(ForgeHairDebug)Add(TEXT("DisplayDebug"),Scalar(TEXT("__OK_RuntimeDisplayDebug"),0));
 FString Code=TEXT("clip(Schema-.5);clip(.25-abs(Family-" )+FString::SanitizeFloat((float(Recipe)+(Outline?128:0)))+TEXT("));const float UVFlip=1.0;\nfloat4 v1=float4(UV.x,1.0-UV.y,0,0);\nfloat4 v2=float4(-Position.x,Position.z,Position.y,100.0)*.01;\nfloat4 v3=float4(-Normal.x,Normal.z,Normal.y,0);\nfloat3 t=float3(-Tangent.x,Tangent.z,Tangent.y);float3 b=float3(-Bitangent.x,Bitangent.z,Bitangent.y);\nfloat4 v4=float4(t,sign(dot(cross(v3.xyz,t),b)));float4 v5=float4(normalize(b),0);float4 v6=float4(asfloat(FaceSign>0?0xffffffffu:0u),0,0,0);\n");
 if(Recipe==EKKRuntimeMaterialRecipe::ForgeHair||Recipe==EKKRuntimeMaterialRecipe::ForgeHairFront||Recipe==EKKRuntimeMaterialRecipe::ForgeEye||Recipe==EKKRuntimeMaterialRecipe::KoikanoEye)Code+=TEXT("v1.zw=float2(SecondaryUV.x,1.0-SecondaryUV.y);\n");
 if(Recipe==EKKRuntimeMaterialRecipe::ForgeEye||Recipe==EKKRuntimeMaterialRecipe::KoikanoEye){auto* UV2=NewObject<UMaterialExpressionTextureCoordinate>(Mat);UV2->CoordinateIndex=2;Add(TEXT("ThirdUV"),UV2);Code+=TEXT("v2.xy=float2(ThirdUV.x,1.0-ThirdUV.y);v3=float4(-Position.x,Position.z,Position.y,100.0)*.01;v4=float4(normalize(float3(-Normal.x,Normal.z,Normal.y)),0);v5=float4(normalize(t),0);v6=float4(normalize(b),0);\n");}
 if(Recipe==EKKRuntimeMaterialRecipe::ForgeEye){auto* Expression=NewObject<UMaterialExpressionTextureCoordinate>(Mat);Expression->CoordinateIndex=3;Add(TEXT("ExpressionUV"),Expression);Code+=TEXT("float4 v7=float4(ExpressionUV.x,1.0-ExpressionUV.y,0,0);\n");}
 if(Recipe==EKKRuntimeMaterialRecipe::KoikanoEye){auto* Expression=NewObject<UMaterialExpressionTextureCoordinate>(Mat);Expression->CoordinateIndex=3;Add(TEXT("ExpressionUV"),Expression);Code+=TEXT("float4 v7=float4(ExpressionUV.x,1.0-ExpressionUV.y,0,0);v1.zw=float2(ThirdUV.x,1.0-ThirdUV.y);v2.xy=float2(SecondaryUV.x,1.0-SecondaryUV.y);v3=float4(normalize(float3(-Normal.x,Normal.z,Normal.y)),0);v4=float4(-Position.x,Position.z,Position.y,100.0)*.01;v5=0;\n");}
 if(Recipe==EKKRuntimeMaterialRecipe::KoikanoEyeWhite)Code+=TEXT("float3 wp=float3(-Position.x,Position.z,Position.y)*.01;float3 wn=normalize(float3(-Normal.x,Normal.z,Normal.y));v2=float4(t.x,b.x,wn.x,wp.x);v3=float4(t.y,b.y,wn.y,wp.y);v4=float4(t.z,b.z,wn.z,wp.z);v5=0;\n");
 const auto& L=*(Outline?OKRuntimeOutlineLayout(Recipe):OKRuntimeSharedMaterialLayout(Recipe));for(int32 I=0;I<L.Sizes.Num();++I){Code+=FString::Printf(TEXT("float4 cb%d[%d];\n"),I,L.Sizes[I]);for(int32 J=0;J<L.Sizes[I];++J)Code+=FString::Printf(TEXT("cb%d[%d]=0.0;\n"),I,J);}
 int32 Parameter=0;for(const auto& U:L.Uniforms){
  if(U.Rows&&U.Name==TEXT("unity_ProbeVolumeWorldToObject")){if(U.Rows!=4)return false;for(int32 Row=0;Row<4;++Row){const FString MatrixRowName=U.Name+FString::Printf(TEXT("_Row%d"),Row);FLinearColor Value(0,0,0,0);(&Value.R)[Row]=1;auto* V=NewObject<UMaterialExpressionVectorParameter>(Mat);V->ParameterName=*MatrixRowName;V->DefaultValue=Value;Register(V);auto* A=Scalar(MatrixRowName+TEXT("__A"),Value.A);Register(A);auto* Append=NewObject<UMaterialExpressionAppendVector>(Mat);Append->A.Expression=V;Append->B.Expression=A;const FString Input=FString::Printf(TEXT("Matrix%d_%d"),U.CB,Row);Add(Input,Append);Code+=FString::Printf(TEXT("cb%d[%d]=%s;\n"),U.CB,U.Offset/16+Row,*Input);}continue;}
  if(U.Rows&&U.Name==TEXT("unity_ObjectToWorld")){if(U.Rows!=4||U.CB<0||U.CB>=L.Sizes.Num())return false;Code+=TEXT("float3 ox=DFMultiplyVector(float3(-1,0,0),GetLocalToWorldDF(Parameters));float3 oy=DFMultiplyVector(float3(0,0,1),GetLocalToWorldDF(Parameters));float3 oz=DFMultiplyVector(float3(0,1,0),GetLocalToWorldDF(Parameters));float3 ot=DFDemote(DFMultiply(float3(0,0,0),GetLocalToWorldDF(Parameters)))*.01;\n");const TCHAR* Axis[]={TEXT("ox"),TEXT("oy"),TEXT("oz"),TEXT("ot")};for(int32 Row=0;Row<FMath::Min(U.Rows,L.Sizes[U.CB]-U.Offset/16);++Row)Code+=FString::Printf(TEXT("cb%d[%d]=float4(-%s.x,%s.z,%s.y,%d);\n"),U.CB,U.Offset/16+Row,Axis[Row],Axis[Row],Axis[Row],Row==3?1:0);continue;}
  if(U.Rows&&U.Name==TEXT("unity_WorldToObject")){if(U.Rows!=4||U.CB<0||U.CB>=L.Sizes.Num())return false;Code+=TEXT("float3 wx=DFMultiplyVector(float3(-1,0,0),GetWorldToLocalDF(Parameters));float3 wy=DFMultiplyVector(float3(0,0,1),GetWorldToLocalDF(Parameters));float3 wz=DFMultiplyVector(float3(0,1,0),GetWorldToLocalDF(Parameters));\n");const TCHAR* Axis[]={TEXT("wx"),TEXT("wy"),TEXT("wz")};for(int32 Row=0;Row<FMath::Min(U.Rows,L.Sizes[U.CB]-U.Offset/16);++Row)Code+=FString::Printf(TEXT("cb%d[%d]=%s;\n"),U.CB,U.Offset/16+Row,Row<3?*FString::Printf(TEXT("float4(-%s.x,%s.z,%s.y,0)"),Axis[Row],Axis[Row],Axis[Row]):TEXT("float4(0,0,0,1)"));continue;}
  if(U.Rows){if(U.Name!=TEXT("unity_MatrixV")||U.CB<0||U.CB>=L.Sizes.Num())return false;Code+=TEXT("float3 vr=float3(-ResolvedView.ViewRight.x,ResolvedView.ViewRight.z,ResolvedView.ViewRight.y);float3 vu=float3(-ResolvedView.ViewUp.x,ResolvedView.ViewUp.z,ResolvedView.ViewUp.y);float3 vf=float3(ResolvedView.ViewForward.x,-ResolvedView.ViewForward.z,-ResolvedView.ViewForward.y);\n");const int32 Rows=FMath::Min(U.Rows,L.Sizes[U.CB]-U.Offset/16);for(int32 Row=0;Row<Rows;++Row)Code+=FString::Printf(TEXT("cb%d[%d]=%s;\n"),U.CB,U.Offset/16+Row,Row<3?*FString::Printf(TEXT("float4(vr[%d],vu[%d],vf[%d],0)"),Row,Row,Row):TEXT("float4(0,0,0,1)"));continue;}
  const bool Automatic=U.Name==TEXT("_Time")||U.Name==TEXT("_WorldSpaceCameraPos");const FString Input=FString::Printf(TEXT("P%d"),Parameter++);
  if(!Automatic){if(U.Dim==1)Add(Input,Scalar(U.Name,U.Default.R));else{auto* V=NewObject<UMaterialExpressionVectorParameter>(Mat);V->ParameterName=*U.Name;V->DefaultValue=U.Default;if(U.Dim==4){Register(V);auto* A=Scalar(U.Name+TEXT("__A"),U.Default.A);Register(A);auto* Append=NewObject<UMaterialExpressionAppendVector>(Mat);Append->A.Expression=V;Append->B.Expression=A;Add(Input,Append);}else Add(Input,V);}}
  for(int32 C=0;C<U.Dim;++C){FString Value=U.Dim==1?Input:Input+TEXT(".")+FString::Chr(TEXT("xyzw")[C]);if(U.Type==1)Value=TEXT("asfloat(uint(int(")+Value+TEXT(")))");if(U.Type==2)Value=TEXT("asfloat(uint(")+Value+TEXT("!=0))");
   if(U.Name==TEXT("_Time"))Value=C==0?TEXT("FrameTime*.05"):C==1?TEXT("FrameTime"):C==2?TEXT("FrameTime*2"):TEXT("FrameTime*3");if(U.Name==TEXT("_WorldSpaceCameraPos"))Value=C==0?TEXT("-CameraPosition.x*.01"):C==1?TEXT("CameraPosition.z*.01"):TEXT("CameraPosition.y*.01");
   Code+=FString::Printf(TEXT("cb%d[%d].%c=%s;\n"),U.CB,(U.Offset/4+C)/4,TEXT("xyzw")[(U.Offset/4+C)%4],*Value);
  }
 }
 Code+=TEXT("float RevealOpacity=RevealOverride>=0?saturate(RevealOverride):(RevealStart<0?1:saturate((FrameTime-RevealStart)/max(RevealDuration,.001)));clip(RevealOpacity-0.000001);\n");
 if(!Translucent)Code+=TEXT("if(RevealOpacity<1){float2 rp=floor(Parameters.SvPosition.xy)+fmod(floor(FrameTime*60),8)*float2(32.665,11.815);float rn=frac(52.9829189*frac(dot(rp,float2(.06711056,.00583715))));clip(RevealOpacity-rn);}\n");
 // HG style: the family program's surface, relit (OKHGShading). Outlines and
 // drawn-not-lit families (eyes, brows, glasses, effects) stay Koikatsu.
#if OK_HG_SHADING_TOOL
 const EOKHGSurface HGSurface=HGShading&&!Outline?OKHGSurfaceOf(Recipe):EOKHGSurface::None;
 if(HGSurface!=EOKHGSurface::None){
  for(const auto& P:OKHGParameters()){UMaterialExpression* E=nullptr;if(P.Vector){auto* V=NewObject<UMaterialExpressionVectorParameter>(Mat);V->ParameterName=P.Name;V->DefaultValue=P.Default;E=V;}else E=Scalar(P.Name,P.Default.R);Add(P.Input,E);}
  FString Light=TEXT("float3(0,.6427876,.7660446)");
  for(const auto& U:L.Uniforms)if(U.Name==TEXT("_WorldSpaceLightPos0")&&!U.Rows&&U.Dim>=3){Light=TEXT("float3(");for(int32 C=0;C<3;++C)Light+=FString::Printf(TEXT("%scb%d[%d].%c"),C?TEXT(","):TEXT(""),U.CB,(U.Offset/4+C)/4,TEXT("xyzw")[(U.Offset/4+C)%4]);Light+=TEXT(")");break;}
  Code+=FString::Printf(TEXT("const float3 HGLightUnity=%s;const float HGSkin=%d;const float HGHair=%d;const float HGPremultiplied=%d;\n"),*Light,HGSurface==EOKHGSurface::Skin?1:0,HGSurface==EOKHGSurface::Hair?1:0,Mat->BlendMode==BLEND_AlphaComposite?1:0);
 }
#endif
 if(!Outline){
  if(OKRuntimeSharedMaterialUsesVertexAmbient(Recipe)){
   auto& AmbientInput=PS->Inputs.AddDefaulted_GetRef();AmbientInput.InputName=TEXT("VertexAmbient");
   AmbientInput.Input.Expression=OKMakeRuntimeVertexAmbient(Mat,FKKRuntimeMaterialScene());
  }
  Code+=OKRuntimeSharedMaterialVaryings(Recipe);
 }
 else {Code+=TEXT("clip(-FaceSign);\n");if(Recipe==EKKRuntimeMaterialRecipe::ForgeSkin)Code+=TEXT("v1.zw=float2(SecondaryUV.x,1-SecondaryUV.y);v2=float4(ThirdUV.x,1-ThirdUV.y,FourthUV.x,1-FourthUV.y);v4=VertexColor;\n");if(Recipe==EKKRuntimeMaterialRecipe::KoikanoSkin)Code+=TEXT("v1.zw=float2(SecondaryUV.x,1-SecondaryUV.y);v2=float4(ThirdUV.x,1-ThirdUV.y,FourthUV.x,1-FourthUV.y);v5=VertexColor;\n");}
 if(Outline&&Recipe==EKKRuntimeMaterialRecipe::KoikanoSkin){const TCHAR* Names[]={TEXT("_texcoord_ST"),TEXT("_texcoord2_ST"),TEXT("_texcoord3_ST"),TEXT("_texcoord4_ST")};const TCHAR* Dest[]={TEXT("v1.xy"),TEXT("v1.zw"),TEXT("v2.xy"),TEXT("v2.zw")};for(int32 I=0;I<4;++I)for(const auto& U:L.Uniforms)if(U.Name==Names[I])Code+=FString::Printf(TEXT("%s=%s*cb%d[%d].xy+cb%d[%d].zw;\n"),Dest[I],Dest[I],U.CB,U.Offset/16,U.CB,U.Offset/16);}
 if(Outline&&int32(Recipe)>=17&&int32(Recipe)<=25){for(int32 I=0;I<2;++I){if(I==1&&(Recipe==EKKRuntimeMaterialRecipe::KoikanoClothesItem||Recipe==EKKRuntimeMaterialRecipe::KoikanoClothesItemAlpha||Recipe==EKKRuntimeMaterialRecipe::KoikanoClothesItemTranslucent||Recipe==EKKRuntimeMaterialRecipe::KoikanoClothesItemAlphaTranslucent))continue;for(const auto& U:L.Uniforms)if(U.Name==(I?TEXT("_texcoord2_ST"):TEXT("_texcoord_ST")))Code+=FString::Printf(TEXT("v1.%s=float2(%s.x,1-%s.y)*cb%d[%d].xy+cb%d[%d].zw;\n"),I?TEXT("zw"):TEXT("xy"),I?TEXT("SecondaryUV"):TEXT("UV"),I?TEXT("SecondaryUV"):TEXT("UV"),U.CB,U.Offset/16,U.CB,U.Offset/16);}}
 FString Program=Outline?OKRuntimeOutlineProgram(Recipe):OKRuntimeSharedMaterialProgram(Recipe);for(const auto& T:L.Textures){auto* E=NewObject<UMaterialExpressionTextureObjectParameter>(Mat);E->ParameterName=*T.Name;E->Texture=Default(T);E->SamplerType=SAMPLERTYPE_LinearColor;Add(FString::Printf(TEXT("Texture%d"),T.Index),E);if(T.Sampler>=0)Program.ReplaceInline(*FString::Printf(TEXT("Sampler%d,"),T.Sampler),*FString::Printf(TEXT("Texture%dSampler,"),T.Index));}
 // Evaluate native discard in depth/shadow passes as well as main color.
 if(ForgeHairDebug)Program.ReplaceInline(TEXT("return o0;"),TEXT("if(DisplayDebug>2.5)return float4(frac(v1.zw),0,1);if(DisplayDebug>1.5)return float4(r2.xxx,1);if(DisplayDebug>.5)return float4(frac(v1.xy),0,1);return o0;"));
#if OK_HG_SHADING_TOOL
 if(HGSurface!=EOKHGSurface::None){if(!Program.Contains(TEXT("return o0;"))){UE_LOG(LogTemp,Error,TEXT("HG relight: %s has no return o0"),*Name);return false;}Program.ReplaceInline(TEXT("return o0;"),*(TEXT("\n")+HGRelight+TEXT("\nreturn o0;")));}
#endif
 // Opaque/masked source output alpha is not an additional .5 coverage cutoff.
 if(!Translucent)Program.ReplaceInline(TEXT("return o0;"),TEXT("return float4(o0.rgb,1.0);"));
 else if(Mat->BlendMode==BLEND_AlphaComposite)Program.ReplaceInline(TEXT("return o0;"),TEXT("return o0*RevealOpacity;"));
 else Program.ReplaceInline(TEXT("return o0;"),TEXT("return float4(o0.rgb,o0.a*RevealOpacity);"));
 PS->Code=Code+Program;Register(PS);auto* Ed=Mat->GetEditorOnlyData();Ed->EmissiveColor.Expression=PS;Ed->EmissiveColor.Mask=1;Ed->EmissiveColor.MaskR=Ed->EmissiveColor.MaskG=Ed->EmissiveColor.MaskB=1;Ed->OpacityMask.Expression=PS;Ed->OpacityMask.Mask=1;Ed->OpacityMask.MaskA=1;if(Translucent){Ed->Opacity.Expression=PS;Ed->Opacity.Mask=1;Ed->Opacity.MaskA=1;}

 if(Outline){
  auto* WPO=NewObject<UMaterialExpressionCustom>(Mat);WPO->OutputType=CMOT_Float3;
  auto Copy=[&](const FString& Input,const FString& Target){for(const auto& I:PS->Inputs)if(I.InputName==FName(*Input)){auto& O=WPO->Inputs.AddDefaulted_GetRef();O.InputName=*Target;O.Input=I.Input;return true;}return false;};
  for(const TCHAR* N:{TEXT("UV"),TEXT("Position"),TEXT("CameraPosition"),TEXT("Normal")})if(!Copy(N,N))return false;
  for(const TCHAR* N:{TEXT("__OK_OutlineWidth"),TEXT("__OK_OutlineSkinWidth"),TEXT("_DetailMask_ST"),TEXT("_AlphaMask_ST"),TEXT("_MainTex_ST"),TEXT("_LineMask_ST")}){const int32 U=L.Uniforms.IndexOfByPredicate([&](const auto& V){return V.Name==N;});if(U<0)return false;int32 P=0;for(int32 I=0;I<U;++I)if(!L.Uniforms[I].Rows)++P;if(!Copy(FString::Printf(TEXT("P%d"),P),N))return false;}
  for(const TCHAR* N:{TEXT("_DetailMask"),TEXT("_AlphaMask"),TEXT("_MainTex"),TEXT("_LineMask")}){const auto* T=L.Textures.FindByPredicate([&](const auto& V){return V.Name==N;});if(T&&!Copy(FString::Printf(TEXT("Texture%d"),T->Index),N))return false;}
  const bool Kano=Recipe==EKKRuntimeMaterialRecipe::KoikanoSkin||(int32(Recipe)>=17&&int32(Recipe)<=25);
  const bool Hair=Recipe==EKKRuntimeMaterialRecipe::ForgeHair||Recipe==EKKRuntimeMaterialRecipe::ForgeHairFront||(int32(Recipe)>=27&&int32(Recipe)<=34);
  if(Hair){auto* Pixels=Scalar(TEXT("__OK_OutlineHairPixelsPerWidth"),4.f);Register(Pixels);auto& Input=WPO->Inputs.AddDefaulted_GetRef();Input.InputName=TEXT("HairPixelsPerWidth");Input.Input.Expression=Pixels;}
  // Source UV transforms run in Unity coordinates; exported PNG rows run in
  // UE coordinates. Apply the row conversion after each independent ST,
  // exactly as the fragment program does, including vertex mask samples.
  // __OK_OutlineApprox 0 (default): the installed outline vertex program, translated from its DXBC
  // (OKRuntimeOutlineVertexProgram), run in Unity world space with identity object and view-projection matrices;
  // its output position minus its input is the hull offset. 1 (ok.OutlineApprox): the earlier hand-written width below.
  {auto* Approx=Scalar(TEXT("__OK_OutlineApprox"),0.f);Register(Approx);auto& Input=WPO->Inputs.AddDefaulted_GetRef();Input.InputName=TEXT("OutlineApprox");Input.Input.Expression=Approx;}
  const auto* VL=OKRuntimeOutlineVertexLayout(Recipe);FString VP=OKRuntimeOutlineVertexProgram(Recipe);
  if(!VL||VP.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("No installed outline vertex program: %s"),*Name);return false;}
  // The program's inputs by semantic (OKPosition, OKNormal, OKTangent, OKUV0..3, OKColor; Unity space and UV rows).
  FString Native=TEXT("float3 native=0;{const float UVFlip=1.0;float4 OKPosition=float4(-Position.x,Position.z,Position.y,100.0)*.01;float3 okn=normalize(float3(-Normal.x,Normal.z,Normal.y));float4 OKNormal=float4(okn,0);float4 OKUV0=float4(UV.x,1.0-UV.y,0,0);\n");
  {const TCHAR* Extra[][2]={{TEXT("SecondaryUV"),TEXT("OKUV1")},{TEXT("ThirdUV"),TEXT("OKUV2")},{TEXT("FourthUV"),TEXT("OKUV3")}};
   for(const auto& E:Extra)Native+=Copy(E[0],E[0])?FString::Printf(TEXT("float4 %s=float4(%s.x,1.0-%s.y,0,0);\n"),E[1],E[0],E[0]):FString::Printf(TEXT("float4 %s=0;\n"),E[1]);
   Native+=Copy(TEXT("Tangent"),TEXT("Tangent"))&&Copy(TEXT("Bitangent"),TEXT("Bitangent"))?TEXT("float3 okt=float3(-Tangent.x,Tangent.z,Tangent.y);float3 okb=float3(-Bitangent.x,Bitangent.z,Bitangent.y);float4 OKTangent=float4(okt,sign(dot(cross(okn,okt),okb)));\n"):TEXT("float4 OKTangent=0;\n");
   Native+=Copy(TEXT("VertexColor"),TEXT("VertexColor"))?TEXT("float4 OKColor=VertexColor;\n"):TEXT("float4 OKColor=1;\n");}
  for(int32 I=0;I<VL->Sizes.Num();++I){Native+=FString::Printf(TEXT("float4 cb%d[%d];\n"),I,VL->Sizes[I]);for(int32 J=0;J<VL->Sizes[I];++J)Native+=FString::Printf(TEXT("cb%d[%d]=0.0;\n"),I,J);}
  TSet<FString> VertexInputs;
  for(const auto& U:VL->Uniforms){
   if(U.Rows){
    if(U.Name!=TEXT("unity_ObjectToWorld")&&U.Name!=TEXT("unity_WorldToObject")&&U.Name!=TEXT("unity_MatrixVP")){UE_LOG(LogTemp,Error,TEXT("Outline vertex matrix %s unsupported: %s"),*U.Name,*Name);return false;}
    for(int32 Row=0;Row<FMath::Min(U.Rows,VL->Sizes[U.CB]-U.Offset/16);++Row)Native+=FString::Printf(TEXT("cb%d[%d]=float4(%d,%d,%d,%d);\n"),U.CB,U.Offset/16+Row,Row==0,Row==1,Row==2,Row==3);
    continue;
   }
   FString Source;
   if(U.Name==TEXT("_WorldSpaceCameraPos"))Source=TEXT("(float4(-CameraPosition.x,CameraPosition.z,CameraPosition.y,100.0)*.01)");
   else if(U.Name.StartsWith(TEXT("unity_"))||U.Name==TEXT("_Time")){UE_LOG(LogTemp,Error,TEXT("Outline vertex uniform %s unsupported: %s"),*U.Name,*Name);return false;}
   else{
    const int32 Index=L.Uniforms.IndexOfByPredicate([&](const auto& V){return V.Name==U.Name&&!V.Rows;});if(Index<0){UE_LOG(LogTemp,Error,TEXT("Outline vertex uniform %s not in layout: %s"),*U.Name,*Name);return false;}
    int32 P=0;for(int32 I=0;I<Index;++I)if(!L.Uniforms[I].Rows)++P;
    Source=TEXT("V")+U.Name;if(!VertexInputs.Contains(Source)){if(!Copy(FString::Printf(TEXT("P%d"),P),Source))return false;VertexInputs.Add(Source);}
   }
   for(int32 C=0;C<U.Dim;++C){FString Value=U.Dim==1?Source:Source+TEXT(".")+FString::Chr(TEXT("xyzw")[C]);if(U.Type==1)Value=TEXT("asfloat(uint(int(")+Value+TEXT(")))");if(U.Type==2)Value=TEXT("asfloat(uint(")+Value+TEXT("!=0))");
    Native+=FString::Printf(TEXT("cb%d[%d].%c=%s;\n"),U.CB,(U.Offset/4+C)/4,TEXT("xyzw")[(U.Offset/4+C)%4],*Value);}
  }
  for(const auto& T:VL->Textures){
   const auto* Bound=L.Textures.FindByPredicate([&](const auto& V){return V.Name==T.Name;});if(!Bound){UE_LOG(LogTemp,Error,TEXT("Outline vertex texture %s not in layout: %s"),*T.Name,*Name);return false;}
   const FString Input=FString::Printf(TEXT("VTexture%d"),T.Index);if(!Copy(FString::Printf(TEXT("Texture%d"),Bound->Index),Input))return false;
   VP.ReplaceInline(*FString::Printf(TEXT("Texture%d."),T.Index),*(Input+TEXT(".")));VP.ReplaceInline(*FString::Printf(TEXT("Sampler%d,"),T.Sampler),*(Input+TEXT("Sampler,")));
  }
  Native+=VP+TEXT("\nnative=float3(-(o0.x-OKPosition.x),o0.z-OKPosition.z,o0.y-OKPosition.y)*100.0;}\n");
  WPO->Code=Native+TEXT("float3 approx=0;{float2 uv=float2(UV.x,1-UV.y);float2 detailUV=uv*_DetailMask_ST.xy+_DetailMask_ST.zw;detailUV.y=1-detailUV.y;float coverage=1-Texture2DSampleLevel(_DetailMask,_DetailMaskSampler,detailUV,0).b;float alpha=1;");
  if(Recipe==EKKRuntimeMaterialRecipe::KoikanoClothesItem||Recipe==EKKRuntimeMaterialRecipe::KoikanoClothesItemAlpha||Recipe==EKKRuntimeMaterialRecipe::KoikanoClothesItemTranslucent||Recipe==EKKRuntimeMaterialRecipe::KoikanoClothesItemAlphaTranslucent)WPO->Code+=TEXT("float2 lineUV=uv*_LineMask_ST.xy+_LineMask_ST.zw;lineUV.y=1-lineUV.y;coverage=1-Texture2DSampleLevel(_LineMask,_LineMaskSampler,lineUV,0).b;");
  if(Hair)WPO->Code+=TEXT("float2 mainUV=uv*_MainTex_ST.xy+_MainTex_ST.zw;mainUV.y=1-mainUV.y;float2 alphaUV=uv*_AlphaMask_ST.xy+_AlphaMask_ST.zw;alphaUV.y=1-alphaUV.y;alpha=Texture2DSampleLevel(_MainTex,_MainTexSampler,mainUV,0).a*Texture2DSampleLevel(_AlphaMask,_AlphaMaskSampler,alphaUV,0).r;");
  WPO->Code+=Kano?TEXT("float width=(distance(Position,CameraPosition)*.001+.6)*__OK_OutlineWidth*__OK_OutlineSkinWidth*.3;"):TEXT("float width=(distance(Position,CameraPosition)*.001+.3)*__OK_OutlineWidth*.5;");
  // Native hull widths can fall below one pixel at the street camera's scale.
  // Keep hair strand silhouettes readable across view distance/FOV/resolution.
  // Preserve authored zero width and coverage/alpha taper; only the hull moves.
  if(Hair)WPO->Code+=TEXT("float viewDepth=max(dot(Position-CameraPosition,ResolvedView.ViewForward),0);float pixelWorld=2*viewDepth*ResolvedView.ViewSizeAndInvSize.w/max(abs(ResolvedView.ViewToClip[1][1]),.0001);width=max(width,pixelWorld*HairPixelsPerWidth*__OK_OutlineWidth);");
  WPO->Code+=TEXT("approx=normalize(Normal)*(width*coverage*alpha);}\nreturn OutlineApprox>.5?approx:native;");
  Register(WPO);Ed->WorldPositionOffset.Expression=WPO;
 }
 Mat->UpdateCachedExpressionData();Mat->PostEditChange();
 Mat->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
 // Finish async DDC shader-map retrieval as well as submitted compiler jobs.
 if(auto* Pending=Mat->GetMaterialResource(GMaxRHIShaderPlatform))Pending->FinishCompilation();
 if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
 auto* Resource=Mat->GetMaterialResource(GMaxRHIShaderPlatform);if(!Resource||!Resource->GetCompileErrors().IsEmpty()||!Resource->GetGameThreadShaderMap()){UE_LOG(LogTemp,Error,TEXT("Common shader compilation failed: %s"),*Name);return false;}
 // A complete base material map does not imply that our mesh pass compiled.
 // Require the exact shader pair before any common asset is saved.
 bool MeshShadersReady=false;
 ENQUEUE_RENDER_COMMAND(VerifySourceMaterialMeshShaders)([Mat,&MeshShadersReady](FRHICommandListImmediate&){
  const FMaterial* RenderMaterial=Mat->GetRenderProxy()->GetMaterialNoFallback(GMaxRHIFeatureLevel);
  MeshShadersReady=RenderMaterial!=nullptr;
  for(const auto* VFName:{TEXT("FLocalVertexFactory"),TEXT("TGPUSkinVertexFactoryDefault"),TEXT("TGPUSkinVertexFactoryUnlimited"),TEXT("FOKSourceOracleVF")})
  {
   const auto* VF=FindVertexFactoryType(FName(VFName));
   const bool Ready=RenderMaterial&&OKSourceMaterialMeshShadersAvailable(*RenderMaterial,VF);
   UE_LOG(LogTemp,Display,TEXT("Source mesh shader %s %s: material=%d vertex_factory=%d ready=%d"),*Mat->GetName(),VFName,RenderMaterial!=nullptr,VF!=nullptr,Ready);
   MeshShadersReady=MeshShadersReady&&Ready;
  }
 });
 FlushRenderingCommands();
 if(!MeshShadersReady){UE_LOG(LogTemp,Error,TEXT("Source material mesh shader pair missing: %s"),*Name);return false;}
 }
 TArray<TSharedPtr<FJsonValue>> Saved;for(auto* A:Assets){const FString File=FPackageName::LongPackageNameToFilename(A->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(A->GetOutermost(),A,*File,Args))return false;Saved.Add(MakeShared<FJsonValueString>(A->GetPathName()));}
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),true);Report->SetBoolField(TEXT("source_mesh_shader_permutations_verified"),true);Report->SetNumberField(TEXT("schema"),1);Report->SetStringField(TEXT("root"),Root);Report->SetStringField(TEXT("parent"),Root/TEXT("M_ForgeOpaque.M_ForgeOpaque"));Report->SetStringField(TEXT("alpha_masked_parent"),Root/TEXT("M_ForgeAlphaMasked.M_ForgeAlphaMasked"));Report->SetStringField(TEXT("alpha_translucent_parent"),Root/TEXT("M_ForgeAlphaTranslucent.M_ForgeAlphaTranslucent"));Report->SetStringField(TEXT("item_parent"),Root/TEXT("M_ForgeItem.M_ForgeItem"));Report->SetStringField(TEXT("hair_parent"),Root/TEXT("M_ForgeHair.M_ForgeHair"));Report->SetStringField(TEXT("hair_front_parent"),Root/TEXT("M_ForgeHairFront.M_ForgeHairFront"));Report->SetStringField(TEXT("studio_alpha_parent"),Root/TEXT("M_ForgeStudioAlpha.M_ForgeStudioAlpha"));Report->SetArrayField(TEXT("assets"),Saved);Report->SetBoolField(TEXT("per_card_input_used"),false);Report->SetBoolField(TEXT("runtime_compilation_required"),false);
 Report->SetBoolField(TEXT("runtime_reveal_supported"),true);Report->SetStringField(TEXT("render_style"),HGShading?TEXT("hg"):TEXT("koikatsu"));Report->SetNumberField(TEXT("runtime_reveal_duration_seconds"),3);
 FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));return FFileHelper::SaveStringToFile(Text,*Output);
}

#endif
