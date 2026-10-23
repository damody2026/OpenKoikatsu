#include "KKRuntimeMaterial.h"
#include "UObject/StrongObjectPtr.h"
#include "KKRuntimeNativeSession.h"
#include "KKRuntimePoseBankTypes.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ok_raw_texture.h"
#include "Modules/ModuleManager.h"
#include "Misc/SecureHash.h"
#include "Json.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeExit.h"
#include "HAL/Event.h"
#include "Async/ParallelFor.h"

namespace {
// Immutable prepared pixels only; each card retains its own material bindings.
struct FPreparedTextureCacheEntry {TArray<uint8> Encoded;FKKRuntimePreparedTexture Prepared;uint64 Bytes=0;};
FCriticalSection PreparedTextureCacheLock;
TMap<FString,TSharedPtr<const FPreparedTextureCacheEntry,ESPMode::ThreadSafe>> PreparedTextureCache;
TArray<FString> PreparedTextureCacheOrder;uint64 PreparedTextureCacheBytes=0;
struct FTexturePreparationFlight {
 TArray<uint8> Encoded;FKKRuntimePreparedTexture Prepared;FString Error;
 FEventRef Done{EEventMode::ManualReset};bool Passed=false;
};
TMap<FString,TSharedPtr<FTexturePreparationFlight,ESPMode::ThreadSafe>> TexturePreparationFlights;
uint64 TexturePreparationFlightBytes=0;
TAtomic<uint64> PreparedCacheHits{0},PreparedDecodeJobs{0},PreparedJoinedJobs{0};
FString PreparedTextureKey(const FString& Hash,const FKKRuntimeTextureSettings& S){return FString::Printf(TEXT("mips-v4:%s:%d:%d:%d:%d:%d"),*Hash,S.Filter,S.WrapU,S.WrapV,int32(S.SRGB),int32(S.EncodedMips));}
TSharedPtr<const FPreparedTextureCacheEntry,ESPMode::ThreadSafe> FindPreparedTexture(const FString& Key,const uint8* Bytes,int32 Length){FScopeLock Lock(&PreparedTextureCacheLock);const auto Found=PreparedTextureCache.FindRef(Key);return Found&&Found->Encoded.Num()==Length&&!FMemory::Memcmp(Found->Encoded.GetData(),Bytes,Length)?Found:TSharedPtr<const FPreparedTextureCacheEntry,ESPMode::ThreadSafe>{};}
void StorePreparedTexture(const FString& Key,const uint8* Bytes,int32 Length,const FKKRuntimePreparedTexture& T){auto Entry=MakeShared<FPreparedTextureCacheEntry,ESPMode::ThreadSafe>();Entry->Encoded.Append(Bytes,Length);Entry->Prepared=T;Entry->Bytes=Entry->Encoded.GetAllocatedSize();for(const auto& M:T.Mips)Entry->Bytes+=M.BGRA.GetAllocatedSize();constexpr uint64 Limit=256ull*1024*1024;if(Entry->Bytes>Limit)return;FScopeLock Lock(&PreparedTextureCacheLock);if(PreparedTextureCache.Contains(Key))return;while(PreparedTextureCacheBytes+Entry->Bytes>Limit&&!PreparedTextureCacheOrder.IsEmpty()){const FString Old=PreparedTextureCacheOrder[0];PreparedTextureCacheOrder.RemoveAt(0);if(auto Previous=PreparedTextureCache.FindRef(Old))PreparedTextureCacheBytes-=Previous->Bytes;PreparedTextureCache.Remove(Old);}PreparedTextureCacheBytes+=Entry->Bytes;PreparedTextureCacheOrder.Add(Key);PreparedTextureCache.Add(Key,Entry);}
FKKRuntimeTextureMip Resize(const FKKRuntimeTextureMip& A,const FKKRuntimeTextureSettings& S,int32 Width,int32 Height){
 static const TArray<double> SRGBToLinear=[](){TArray<double> L;for(int32 I=0;I<256;++I)L.Add(FLinearColor::FromSRGBColor(FColor(I,I,I,255)).R);return L;}();
 FKKRuntimeTextureMip B;B.Width=Width;B.Height=Height;B.BGRA.SetNumUninitialized(B.Width*B.Height*4);
 for(int32 Y=0;Y<B.Height;++Y)for(int32 X=0;X<B.Width;++X){
  // Area box filter covers all texels, including odd dimensions and 1xN.
  const double X0=double(X)*A.Width/B.Width,X1=double(X+1)*A.Width/B.Width,Y0=double(Y)*A.Height/B.Height,Y1=double(Y+1)*A.Height/B.Height;
  double Sum[4]={};const double Area=(X1-X0)*(Y1-Y0);
  for(int32 SY=FMath::FloorToInt(Y0);SY<FMath::CeilToInt(Y1);++SY)for(int32 SX=FMath::FloorToInt(X0);SX<FMath::CeilToInt(X1);++SX){
   const double Weight=(FMath::Min(X1,double(SX+1))-FMath::Max(X0,double(SX)))*(FMath::Min(Y1,double(SY+1))-FMath::Max(Y0,double(SY)));
   for(int32 C=0;C<4;++C){const uint8 V=A.BGRA[(SY*A.Width+SX)*4+C];Sum[C]+=Weight*(C<3&&S.SRGB&&!S.EncodedMips?SRGBToLinear[V]:double(V)/255.);}
  }
  const int32 Pixel=(Y*B.Width+X)*4;
  if(S.SRGB&&!S.EncodedMips){
   // Convert the three independent color channels once. Repeating a full RGB
   // conversion for each channel produces the same bytes with triple the work.
   const FColor Color=FLinearColor(Sum[2]/Area,Sum[1]/Area,Sum[0]/Area,1).ToFColorSRGB();
   B.BGRA[Pixel]=Color.B;B.BGRA[Pixel+1]=Color.G;B.BGRA[Pixel+2]=Color.R;
  }else for(int32 C=0;C<3;++C)B.BGRA[Pixel+C]=uint8(FMath::Clamp(FMath::FloorToInt(Sum[C]/Area*255.+1e-7),0,255));
  B.BGRA[Pixel+3]=uint8(FMath::Clamp(FMath::FloorToInt(Sum[3]/Area*255.+1e-7),0,255));
 }return B;
}
}

bool OKBuildRuntimeTextureMips(TArray<uint8> BGRA,int32 Width,int32 Height,const FKKRuntimeTextureSettings& Settings,TArray<FKKRuntimeTextureMip>& Out,FString& Error){
 if(Width<=0||Height<=0||int64(Width)*Height*4!=BGRA.Num()||BGRA.Num()>128*1024*1024){Error=TEXT("Invalid runtime texture source pixels");return false;}
 FKKRuntimeTextureMip M;M.Width=Width;M.Height=Height;M.BGRA=MoveTemp(BGRA);
 int32 TargetWidth=Width,TargetHeight=Height;while(TargetWidth>OKRuntimeTextureMaxDimension||TargetHeight>OKRuntimeTextureMaxDimension){TargetWidth=FMath::Max(1,TargetWidth/2);TargetHeight=FMath::Max(1,TargetHeight/2);}
 if(TargetWidth!=Width||TargetHeight!=Height)M=Resize(M,Settings,TargetWidth,TargetHeight);
 TArray<FKKRuntimeTextureMip> Mips;Mips.Add(MoveTemp(M));while(Mips.Last().Width>1||Mips.Last().Height>1)Mips.Add(Resize(Mips.Last(),Settings,FMath::Max(1,Mips.Last().Width/2),FMath::Max(1,Mips.Last().Height/2)));
 Out=MoveTemp(Mips);Error.Reset();return true;
}

namespace {
FString RuntimeTextureIdentity(const FKKRuntimePreparedTexture& T);
bool PrepareSharedRuntimeTexture(const FString& Key,const FString& Hash,const uint8* Bytes,int32 Length,const FKKRuntimeTextureSettings& Settings,FKKRuntimePreparedTexture& Out,FString& Error){
 if(const auto Cached=FindPreparedTexture(Key,Bytes,Length)){++PreparedCacheHits;Out=Cached->Prepared;return true;}
 TSharedPtr<FTexturePreparationFlight,ESPMode::ThreadSafe> Flight;bool Owner=false,Registered=false;
 {
  FScopeLock Lock(&PreparedTextureCacheLock);
  // Recheck under the same lock as in-flight registration, so a just-published
  // immutable result cannot be redundantly decoded by another worker.
  const auto Cached=PreparedTextureCache.FindRef(Key);
  if(Cached&&Cached->Encoded.Num()==Length&&!FMemory::Memcmp(Cached->Encoded.GetData(),Bytes,Length)){++PreparedCacheHits;Out=Cached->Prepared;return true;}
  Flight=TexturePreparationFlights.FindRef(Key);
  if(Flight&&(Flight->Encoded.Num()!=Length||FMemory::Memcmp(Flight->Encoded.GetData(),Bytes,Length)))Flight.Reset();
  if(!Flight){Owner=true;Flight=MakeShared<FTexturePreparationFlight,ESPMode::ThreadSafe>();
   // Bound retained encoded input as well as the completed cache. Hash collisions
   // and budget pressure decode independently; they never share wrong pixels.
   const uint64 RetainedBytes=uint64(DefaultCalculateSlackReserve(Length,1,true));
   if(!TexturePreparationFlights.Contains(Key)&&RetainedBytes<=256ull*1024*1024&&TexturePreparationFlightBytes<=256ull*1024*1024-RetainedBytes){Flight->Encoded.Reserve(Length);Flight->Encoded.Append(Bytes,Length);TexturePreparationFlightBytes+=Flight->Encoded.GetAllocatedSize();TexturePreparationFlights.Add(Key,Flight);Registered=true;}
  }
 }
 if(!Owner){++PreparedJoinedJobs;if(!Flight->Done->Wait(60000)){Error=TEXT("Shared runtime texture preparation timed out");return false;}if(!Flight->Passed){Error=Flight->Error;return false;}Out=Flight->Prepared;Error.Reset();return true;}
 ON_SCOPE_EXIT{
  if(!Flight->Passed)Flight->Error=Error;
  {FScopeLock Lock(&PreparedTextureCacheLock);if(Registered&&TexturePreparationFlights.FindRef(Key)==Flight){TexturePreparationFlightBytes-=Flight->Encoded.GetAllocatedSize();TexturePreparationFlights.Remove(Key);}Flight->Done->Trigger();}
 };
 ++PreparedDecodeJobs;FKKRuntimeTextureMip M;int32 Width=0,Height=0;
 // PNG, or ok-extract's raw LZ4 container (OK_RAW_TEXTURES=1).
 if(OKIsRawTexture(Bytes,Length)){if(!OKDecodeRawTexture(Bytes,Length,M.BGRA,Width,Height)){Error=TEXT("Runtime raw texture invalid");return false;}}
 else{
 auto& Images=FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
 auto W=Images.CreateImageWrapper(EImageFormat::PNG);if(!W||!W->SetCompressed(Bytes,Length)){Error=TEXT("Runtime PNG decode header failed");return false;}
 Width=W->GetWidth();Height=W->GetHeight();if(Width<=0||Height<=0||uint64(Width)*Height*4>128ull*1024*1024){Error=TEXT("Runtime PNG decoded size exceeds budget");return false;}
 // A grayscale PNG is one value per pixel: every colour channel gets it, explicitly rather than through the
 // wrapper's BGRA conversion.
 if(W->GetFormat()==ERGBFormat::Gray){TArray<uint8> Gray;if(!W->GetRaw(ERGBFormat::Gray,8,Gray)||uint64(Gray.Num())!=uint64(Width)*Height){Error=TEXT("Runtime PNG gray pixels invalid");return false;}
  M.BGRA.SetNumUninitialized(Gray.Num()*4);for(int32 I=0;I<Gray.Num();++I){uint8* P=&M.BGRA[I*4];P[0]=P[1]=P[2]=Gray[I];P[3]=255;}}
 else if(!W->GetRaw(ERGBFormat::BGRA,8,M.BGRA)||uint64(M.BGRA.Num())!=uint64(Width)*Height*4){Error=TEXT("Runtime PNG pixels invalid");return false;}
 }
 if(uint64(Width)*Height*4>128ull*1024*1024){Error=TEXT("Runtime PNG decoded size exceeds budget");return false;}
 M.Width=Width;M.Height=Height;
 auto& T=Flight->Prepared;T.SourceHash=Hash;T.Settings=Settings;T.SourceWidth=Width;T.SourceHeight=Height;
 if(!OKBuildRuntimeTextureMips(MoveTemp(M.BGRA),Width,Height,Settings,T.Mips,Error))return false;
 T.PixelIdentity=RuntimeTextureIdentity(T);
 StorePreparedTexture(Key,Bytes,Length,T);Flight->Passed=true;Out=T;Error.Reset();return true;
}
}

// A part's runtime material description (source metadata and the card-resolved values), without its textures.
static bool DescribePart(OkSession* Session,const OkMeshView& V,int32 NativePart,FKKRuntimeMaterialPart& P,TSharedPtr<FJsonObject>& Metadata,FString& Error){
 auto Fail=[&](const FString& Why){Error=Why;return false;};
 P=FKKRuntimeMaterialPart();P.NativePart=NativePart;P.Name=UTF8_TO_TCHAR(V.name?V.name:"");P.Role=UTF8_TO_TCHAR(V.role?V.role:"");P.Blend=UTF8_TO_TCHAR(V.blend?V.blend:"");P.ShaderKind=V.shader_kind;P.Unlit=V.unlit;P.ItemDraw=V.item_draw;P.EyeDraw=V.eye_draw;P.Outline=ok_part_outline_enabled(Session,NativePart)==1;
 // Only the opaque / clothes-colour / item-alpha families (kinds 2, 3, 9) gate their outline pass with _OUTLINE_ON;
 // the others (hair, Forge main_item with its _LineWidthS, ...) always draw it, as the card reader's own graph does
 // (KKPreviewActor bOutlinable). Their legacy materials need not serialize the keyword.
 if(P.ShaderKind!=2&&P.ShaderKind!=3&&P.ShaderKind!=9)P.Outline=true;
 const int32 Size=ok_part_material_metadata(Session,NativePart,nullptr,0);if(Size<=0||Size>1024*1024)return Fail(TEXT("Runtime material metadata size invalid"));
 TArray<uint8> Json;Json.SetNumZeroed(Size+1);if(ok_part_material_metadata(Session,NativePart,Json.GetData(),Size)!=Size)return Fail(TEXT("Runtime material metadata copy failed"));P.SourceMetadataJson=UTF8_TO_TCHAR(reinterpret_cast<const char*>(Json.GetData()));
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Metadata)||Metadata->GetIntegerField(TEXT("schema"))!=1)return Fail(TEXT("Runtime material metadata schema invalid"));
 auto Scalar=[&](const TCHAR* N,float F){P.Values.Add(N,FVector4f(F,0,0,0));};auto Vec=[&](const TCHAR* N,const float* F){P.Values.Add(N,FVector4f(F[0],F[1],F[2],F[3]));};
 if(V.eye_draw)Scalar(TEXT("_OKEyeUVPrepared"),V.eye_sample_uvs&&V.uv2&&V.uv3?1.f:0.f);if(V.eye_draw==1||V.eye_draw==2)Scalar(TEXT("_OKEyeExpressionUVPrepared"),ok_part_eye_expression_uvs(Session,NativePart,nullptr,0)==V.n_verts?1.f:0.f);
 Scalar(TEXT("_ShadowDepth"),V.shadow_depth);Scalar(TEXT("_LineWidth"),V.line_width);Scalar(TEXT("_LineDepth"),V.line_depth);Scalar(TEXT("_VerticalShift"),V.kano_ver_shift);
 Scalar(TEXT("_ShadowExtend"),V.shadow_extend);Scalar(TEXT("_ShadowExtendAnother"),V.shadow_extend_another);Scalar(TEXT("_NormalDetailScale"),V.normal_detail_scale);Scalar(TEXT("_FaceNormal"),V.face_normal);Scalar(TEXT("_FaceShadow"),V.face_shadow);Scalar(TEXT("_LineTexOn"),V.line_tex_on);
 Scalar(TEXT("_SpecularHeight"),V.spec_height);Scalar(TEXT("_SpecularPower"),V.specular_power);Scalar(TEXT("_SpecularPowerNail"),V.specular_power_nail);Scalar(TEXT("_NoTexSpecular"),V.no_tex_specular);Scalar(TEXT("_Metallic"),V.metallic);Scalar(TEXT("_Smoothness"),V.smoothness);
 Scalar(TEXT("_NipSize"),V.nip_size);Scalar(TEXT("_NipSpecular"),V.nip_specular);Scalar(TEXT("_NipToggle"),V.nip_toggle);Scalar(TEXT("_Tex1Mask"),V.tex1_mask);Scalar(TEXT("_SkinLineWidth"),V.skin_line_width);Scalar(TEXT("_EyeHighlight"),V.eye_highlight);
 Vec(TEXT("_OverColor1"),V.overcolor);Vec(TEXT("_OverColor2"),V.overcolor2);Vec(TEXT("_ShadowColor"),V.shadow_color);Vec(TEXT("_LineColor"),V.line_color);Vec(TEXT("_SpecularColor"),V.specular_color);Vec(TEXT("_EyeColor"),V.eye_color);
 Vec(TEXT("_MainTex_ST"),V.main_tex_st);Vec(TEXT("_AlphaMask_ST"),V.alpha_mask_st);Vec(TEXT("_NormalMap_ST"),V.normal_map_st);Vec(TEXT("_NormalMask_ST"),V.normal_mask_st);Vec(TEXT("_DetailMask_ST"),V.detail_mask_st);Vec(TEXT("_LineMask_ST"),V.line_mask_st);Vec(TEXT("_HairGloss_ST"),V.hair_gloss_st);Vec(TEXT("_NormalDetail_ST"),V.normal_detail_st);Vec(TEXT("_ColorMask_ST"),V.color_mask_st);Vec(TEXT("_Texcoord2_ST"),V.texcoord2_st);Vec(TEXT("_Texcoord3_ST"),V.texcoord3_st);
 for(int32 I=0;I<4;++I)Vec(*FString::Printf(TEXT("_ItemColor%d"),I),V.item_colors[I]);for(int32 I=0;I<2;++I)Vec(*FString::Printf(TEXT("_EyeLayerColor%d"),I),V.eye_colors[I]);P.Values.Add(TEXT("_EyeRotations"),FVector4f(V.eye_rotations[0],V.eye_rotations[1],V.eye_rotations[2],0));
 Error.Reset();return true;
}
bool OKDescribeRuntimeMaterialPart(OkSession* Session,int32 NativePart,FKKRuntimeMaterialPart& Out,FString& Error){
 OkMeshView V{};if(!Session||OKRuntimeNativeBindings().part_at(Session,NativePart,&V)!=0){Error=TEXT("Runtime material part unavailable");return false;}
 TSharedPtr<FJsonObject> Metadata;return DescribePart(Session,V,NativePart,Out,Metadata,Error);
}

bool OKCaptureRuntimeMaterials(OkSession* Session,TConstArrayView<FKKRuntimePalettePart> Parts,FKKRuntimeMaterialData& Out,FString& Error){
 const double Start=FPlatformTime::Seconds();FKKRuntimeMaterialData D;D.WorkerThread=!IsInGameThread();
 const uint64 EncodedBudget=256ull*1024*1024,PixelBudget=512ull*1024*1024;
 // OK_FIRSTLOAD=C5: the card's distinct textures are decoded and mipped in parallel after all parts are read
 // (one at a time on the calling worker was ~1.8 s of a packaged card reader's first load).
 static const bool Parallel=[]{TArray<FString> Options;FString Value=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_FIRSTLOAD"));Value.ParseIntoArray(Options,TEXT(","));for(FString& O:Options)O.TrimStartAndEndInline();return Options.Contains(TEXT("C5"))||Options.Contains(TEXT("all"));}();
 struct FPendingTexture{int32 Index;FString Key,Hash;FKKRuntimeTextureSettings Settings;};TArray<FPendingTexture> Pending;
 TArray<TArray<uint8>> Sources;const auto& Bind=OKRuntimeNativeBindings();
 auto Fail=[&](const FString& Why){Error=Why;return false;};
 for(const auto& Part:Parts){OkMeshView V{};if(Bind.part_at(Session,Part.NativePart,&V)!=0)return Fail(TEXT("Runtime material part unavailable"));
  FKKRuntimeMaterialPart P;TSharedPtr<FJsonObject> Metadata;if(!DescribePart(Session,V,Part.NativePart,P,Metadata,Error))return false;
  auto Add=[&](const FString& Property,const uint8* Bytes,int32 Length,bool Linear=false,bool Clamp=false){
   if(!Bytes&&Length==0)return true;if(!Bytes||Length<=0)return Fail(TEXT("Runtime texture stream invalid"));
   for(const auto& Existing:P.Textures)if(Existing.Property==Property)return true;
   FKKRuntimeTextureSettings S;S.SRGB=!Linear;S.WrapU=S.WrapV=Clamp?1:0;OkTextureSettings Raw{};
   // Static source wrap settings live on the material; runtime replacements
   // additionally provide per-axis wrapping, sRGB, filter and mip provenance.
   const TSharedPtr<FJsonObject>* SourceMaterial=nullptr;const TSharedPtr<FJsonObject>* Wraps=nullptr;double StaticWrap=0;
   if(Metadata->TryGetObjectField(TEXT("material"),SourceMaterial)&&(*SourceMaterial)->TryGetObjectField(TEXT("tex_wraps"),Wraps)&&(*Wraps)->TryGetNumberField(Property,StaticWrap))S.WrapU=S.WrapV=int32(StaticWrap);
   if(ok_part_texture_settings(Session,Part.NativePart,TCHAR_TO_UTF8(*Property),&Raw)==0){S.Filter=Raw.filter;S.WrapU=Raw.wrap_u;S.WrapV=Raw.wrap_v;S.SRGB=Raw.srgb!=0;S.EncodedMips=Raw.mip_policy==1;}
   if(S.Filter<0||S.Filter>2||S.WrapU<0||S.WrapU>2||S.WrapV<0||S.WrapV>2)return Fail(TEXT("Runtime texture settings unsupported"));
   const FString Hash=FSHA1::HashBuffer(Bytes,Length).ToString();int32 Index=INDEX_NONE;
   for(int32 I=0;I<D.Textures.Num();++I)if(D.Textures[I].SourceHash==Hash&&D.Textures[I].Settings==S&&Sources[I].Num()==Length&&!FMemory::Memcmp(Sources[I].GetData(),Bytes,Length)){Index=I;break;}
   if(Index==INDEX_NONE){const uint64 EncodedSize=uint64(DefaultCalculateSlackReserve(Length,1,true))*1;if(D.EncodedBytes+EncodedSize>EncodedBudget)return Fail(TEXT("Runtime encoded textures exceed budget"));
    const FString PreparedKey=PreparedTextureKey(Hash,S);FKKRuntimePreparedTexture T;uint64 Pixels=0;
    if(Parallel){T.SourceHash=Hash;T.Settings=S;Pending.Add({D.Textures.Num(),PreparedKey,Hash,S});}
    else{
     if(!PrepareSharedRuntimeTexture(PreparedKey,Hash,Bytes,Length,S,T,Error))return false;
     for(const auto& L:T.Mips)Pixels+=L.BGRA.GetAllocatedSize();if(D.PixelBytes+Pixels>PixelBudget)return Fail(TEXT("Runtime prepared texture pixels exceed budget"));
    }
    Index=D.Textures.Num();D.Textures.Add(MoveTemp(T));TArray<uint8> Encoded;Encoded.Reserve(Length);Encoded.Append(Bytes,Length);D.EncodedBytes+=Encoded.GetAllocatedSize();Sources.Add(MoveTemp(Encoded));D.PixelBytes+=Pixels;
   }P.Textures.Add({Property,Index});return true;
  };
  // The toon ramp is the character's (Koikatsu gives all of her materials one ramp, and the editor's own path always bound
  // it): a material's stale _RampG of its own (a hair accessory's red-only ramp drew black) does not take its place.
  if(V.ramp_png&&V.ramp_png_len>0&&!Add(TEXT("_RampG"),V.ramp_png,V.ramp_png_len,true,true))return false;
  // Exact shader inputs take precedence over the legacy preview streams,
  // which can contain color baking or normal conversion for another renderer.
  for(const auto& Name:Metadata->GetArrayField(TEXT("shader_textures"))){const FString Property=Name->AsString();const uint8* Bytes=nullptr;int32 Length=0;if(ok_part_shader_texture(Session,Part.NativePart,TCHAR_TO_UTF8(*Property),&Bytes,&Length)!=0||!Add(Property,Bytes,Length))return Fail(TEXT("Runtime extra shader texture unavailable"));}
  // Item programs (Forge opaque/item/alpha, MainItemPlus: the editor's native item draw) sample their own _MainTex; the
  // part png can be another image (a hair accessory's came out black-purple). Theirs first, the png only without one.
  if(P.ShaderKind==2||P.ShaderKind==8||P.ShaderKind==9||P.ShaderKind==11){const uint8* ItemBytes=nullptr;int32 ItemLength=0;if(ok_part_shader_texture(Session,Part.NativePart,"_MainTex",&ItemBytes,&ItemLength)==0&&ItemBytes&&ItemLength>0&&!Add(TEXT("_MainTex"),ItemBytes,ItemLength))return false;}
  if(!Add(TEXT("_MainTex"),V.png,V.png_len)||!Add(TEXT("_AlphaMask"),V.alpha_mask,V.alpha_mask_len,true)||!Add(TEXT("_HairGloss"),V.hair_gloss,V.hair_gloss_len,true)||!Add(TEXT("_DetailMask"),V.detail_mask,V.detail_mask_len,true)||!Add(TEXT("_LineMask"),V.line_mask,V.line_mask_len,true)||!Add(TEXT("_NormalMap"),V.normal_map,V.normal_map_len,true)||!Add(TEXT("_NormalMask"),V.normal_mask,V.normal_mask_len,true)||!Add(TEXT("_RampG"),V.ramp_png,V.ramp_png_len,true,true)||!Add(TEXT("_overtex1"),V.overtex_png,V.overtex_png_len)||!Add(TEXT("_overtex2"),V.overtex2_png,V.overtex2_png_len)||!Add(TEXT("_NormalDetail"),V.normal_detail,V.normal_detail_len,true)||!Add(TEXT("_ColorMask"),V.color_mask,V.color_mask_len,true))return false;
  for(int32 I=0;I<2;++I)if(!Add(FString::Printf(TEXT("_EyeLayer%d"),I),V.eye_textures[I],V.eye_texture_lens[I],false,true))return false;
  for(const auto& Value:P.Values)if(Value.Value.ContainsNaN())return Fail(TEXT("Runtime material resolved value nonfinite"));D.Parts.Add(MoveTemp(P));
 }
 if(!Pending.IsEmpty()){
  // Each pending texture has its own key (distinct hash/settings), so no two jobs join one flight here.
  TArray<FString> Errors;Errors.SetNum(Pending.Num());
  ParallelFor(Pending.Num(),[&](int32 J){const auto& Job=Pending[J];const auto& Source=Sources[Job.Index];FKKRuntimePreparedTexture T;
   if(PrepareSharedRuntimeTexture(Job.Key,Job.Hash,Source.GetData(),Source.Num(),Job.Settings,T,Errors[J]))D.Textures[Job.Index]=MoveTemp(T);else if(Errors[J].IsEmpty())Errors[J]=TEXT("Runtime texture preparation failed");},EParallelForFlags::Unbalanced);
  for(const FString& E:Errors)if(!E.IsEmpty())return Fail(E);
  for(const auto& Job:Pending){for(const auto& L:D.Textures[Job.Index].Mips)D.PixelBytes+=L.BGRA.GetAllocatedSize();}
  if(D.PixelBytes>PixelBudget)return Fail(TEXT("Runtime prepared texture pixels exceed budget"));
 }
 D.PrepareMs=(FPlatformTime::Seconds()-Start)*1000;Out=MoveTemp(D);Error.Reset();return true;
}
namespace {
TMap<FString,TWeakObjectPtr<UTexture2D>> RuntimeTextureCache;
// Recently used textures stay alive for a while after their last material is gone: the card reader switching back to an
// outfit or card made them all again. Bounded by their GPU size.
TArray<TPair<TStrongObjectPtr<UTexture2D>,int64>> RecentRuntimeTextures;int64 RecentRuntimeTextureBytes=0;
void KeepRecentRuntimeTexture(UTexture2D* Tex){
 if(!Tex)return;
 const int32 Index=RecentRuntimeTextures.IndexOfByPredicate([Tex](const TPair<TStrongObjectPtr<UTexture2D>,int64>& E){return E.Key.Get()==Tex;});
 if(Index!=INDEX_NONE){auto Entry=MoveTemp(RecentRuntimeTextures[Index]);RecentRuntimeTextures.RemoveAt(Index);RecentRuntimeTextures.Add(MoveTemp(Entry));return;}
 const int64 Bytes=int64(Tex->GetSizeX())*Tex->GetSizeY()*4*4/3;
 RecentRuntimeTextures.Emplace(TStrongObjectPtr<UTexture2D>(Tex),Bytes);RecentRuntimeTextureBytes+=Bytes;
 while(RecentRuntimeTextureBytes>512ll*1024*1024&&RecentRuntimeTextures.Num()>1){RecentRuntimeTextureBytes-=RecentRuntimeTextures[0].Value;RecentRuntimeTextures.RemoveAt(0);}
}
FKKRuntimeTextureCacheStats RuntimeTextureStats;
FString RuntimeTextureIdentity(const FKKRuntimePreparedTexture& T){
 // Hash explicit fields, never struct padding or the source PNG identity alone:
 // two decoders/quality policies can produce different pixels from one source.
 FSHA1 Hash;
 const uint32 Header[]={1,uint32(T.Settings.Filter),uint32(T.Settings.WrapU),uint32(T.Settings.WrapV),uint32(T.Settings.SRGB),uint32(T.Settings.EncodedMips),uint32(T.Mips.Num())};
 Hash.Update(reinterpret_cast<const uint8*>(Header),sizeof(Header));
 for(const auto& M:T.Mips){const uint32 Shape[]={uint32(M.Width),uint32(M.Height),uint32(M.BGRA.Num())};Hash.Update(reinterpret_cast<const uint8*>(Shape),sizeof(Shape));Hash.Update(M.BGRA.GetData(),M.BGRA.Num());}
 Hash.Final();FSHAHash Digest;Hash.GetHash(Digest.Hash);return Digest.ToString();
}
}
FKKRuntimeTextureCacheStats OKRuntimeTextureCacheStats(){check(IsInGameThread());auto Result=RuntimeTextureStats;Result.Entries=RuntimeTextureCache.Num();Result.PreparedCacheHits=PreparedCacheHits.Load();Result.PreparedDecodeJobs=PreparedDecodeJobs.Load();Result.PreparedJoinedJobs=PreparedJoinedJobs.Load();return Result;}
UTexture2D* OKCreateRuntimePreparedTexture(const FKKRuntimePreparedTexture& T,FString& Error){
 check(IsInGameThread());if(T.Settings.Filter<0||T.Settings.Filter>2||T.Settings.WrapU<0||T.Settings.WrapU>2||T.Settings.WrapV<0||T.Settings.WrapV>2){Error=TEXT("Runtime texture sampler invalid");return nullptr;}if(T.Mips.IsEmpty()){Error=TEXT("Runtime texture mips absent");return nullptr;}
 int32 Width=T.Mips[0].Width,Height=T.Mips[0].Height;
 int32 Expected=1;for(int32 W=Width,H=Height;W>1||H>1;W=FMath::Max(1,W/2),H=FMath::Max(1,H/2))++Expected;if(T.Mips.Num()!=Expected){Error=TEXT("Runtime texture mip count invalid");return nullptr;}
 for(const auto& M:T.Mips){if(M.Width!=Width||M.Height!=Height||Width<=0||Height<=0||Width>OKRuntimeTextureMaxDimension||Height>OKRuntimeTextureMaxDimension||int64(Width)*Height*4!=M.BGRA.Num()){Error=TEXT("Runtime texture mip chain invalid");return nullptr;}Width=FMath::Max(1,Width/2);Height=FMath::Max(1,Height/2);}
 if(T.Mips.Last().Width!=1||T.Mips.Last().Height!=1){Error=TEXT("Runtime texture mip chain incomplete");return nullptr;}
 const FString Identity=T.PixelIdentity.IsEmpty()?RuntimeTextureIdentity(T):T.PixelIdentity;++RuntimeTextureStats.Requests;
 if(auto* Existing=RuntimeTextureCache.Find(Identity))if(auto* Texture=Existing->Get()){KeepRecentRuntimeTexture(Texture);++RuntimeTextureStats.Hits;for(const auto& M:T.Mips)RuntimeTextureStats.AvoidedPixelBytes+=M.BGRA.Num();Error.Reset();return Texture;}
 // Weak ownership permits collection after the last material leaves a scene.
 // Amortized pruning bounds stale metadata without a full scan every frame.
 if(RuntimeTextureStats.Requests%256==0)for(auto It=RuntimeTextureCache.CreateIterator();It;++It)if(!It.Value().IsValid())It.RemoveCurrent();
 auto* Tex=UTexture2D::CreateTransient(T.Mips[0].Width,T.Mips[0].Height,PF_B8G8R8A8);if(!Tex){Error=TEXT("Runtime texture allocation failed");return nullptr;}
 for(int32 I=0;I<T.Mips.Num();++I){auto* M=I?new FTexture2DMipMap():&Tex->GetPlatformData()->Mips[0];if(I)Tex->GetPlatformData()->Mips.Add(M);M->SizeX=T.Mips[I].Width;M->SizeY=T.Mips[I].Height;M->SizeZ=1;M->BulkData.Lock(LOCK_READ_WRITE);void* Bytes=M->BulkData.Realloc(T.Mips[I].BGRA.Num());FMemory::Memcpy(Bytes,T.Mips[I].BGRA.GetData(),T.Mips[I].BGRA.Num());M->BulkData.Unlock();}
 auto Wrap=[](int32 V){return V==1?TA_Clamp:V==2?TA_Mirror:TA_Wrap;};Tex->SRGB=T.Settings.SRGB;Tex->Filter=T.Settings.Filter==0?TF_Nearest:T.Settings.Filter==2?TF_Trilinear:TF_Bilinear;Tex->AddressX=Wrap(T.Settings.WrapU);Tex->AddressY=Wrap(T.Settings.WrapV);
 // Transient textures own their complete small mip chain; this is not cooked streaming.
 Tex->NeverStream=true;Tex->UpdateResource();RuntimeTextureCache.Add(Identity,Tex);KeepRecentRuntimeTexture(Tex);++RuntimeTextureStats.Created;Error.Reset();return Tex;
}
