#include "KKRuntimeSharedMaterial.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionVertexTangentWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#endif

#if WITH_EDITOR
namespace {
// A Unity default texture ("white", "black", "bump", "gray", "red") as a 1x1 transient texture, kept for the process.
UTexture* OKDefaultOutlineTexture(const FString& Key)
{
	static TMap<FString, UTexture2D*> Made;
	if (UTexture2D** Hit = Made.Find(Key)) return *Hit;
	const FColor Color = Key == TEXT("white") ? FColor::White : Key == TEXT("bump") ? FColor(255, 128, 255, 128)
		: Key == TEXT("gray") ? FColor(128, 128, 128, 128) : Key == TEXT("red") ? FColor(255, 0, 0, 0) : FColor(0, 0, 0, 0);
	UTexture2D* Texture = UTexture2D::CreateTransient(1, 1, PF_B8G8R8A8);
	if (!Texture) return nullptr;
	Texture->SRGB = false;
	Texture->Filter = TF_Nearest;
	if (void* Data = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE)) FMemory::Memcpy(Data, &Color, sizeof(Color));
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();
	Texture->AddToRoot();
	Made.Add(Key, Texture);
	return Texture;
}
FString OKLiteral(float V) { return FMath::IsFinite(V) ? FString::Printf(TEXT("%.9g"), V) : TEXT("0"); }
}
#endif

UMaterialExpression* OKMakeRuntimeOutlineVertexOffset(UMaterial* Mat, const FKKRuntimeMaterialPart& P, const FKKRuntimeMaterialScene& Scene,
	TFunctionRef<UTexture*(const FString&)> Texture, FString& Error)
{
#if WITH_EDITOR
	auto Fail = [&](const FString& Why) -> UMaterialExpression* { Error = Why; return nullptr; };
	if (!Mat) return Fail(TEXT("no material"));
	const EKKRuntimeMaterialRecipe Recipe = OKRuntimeSharedMaterialRecipe(P);
	const FKKRuntimeShaderLayout* VL = OKRuntimeOutlineVertexLayout(Recipe);
	FString VP = OKRuntimeOutlineVertexProgram(Recipe);
	if (Recipe == EKKRuntimeMaterialRecipe::None || !VL || VP.IsEmpty()) return Fail(TEXT("no installed outline vertex program for the part's family"));
	// The outline parent's values (its layout carries the vertex program's material inputs), resolved as for a shared instance.
	FKKRuntimeMaterialBinding B;
	if (!OKPrepareRuntimeSharedMaterialBinding(P, Scene, B, Error, true)) return nullptr;

	auto* WPO = NewObject<UMaterialExpressionCustom>(Mat);
	WPO->OutputType = CMOT_Float3;
	WPO->Description = TEXT("Installed outline vertex program");
	WPO->Inputs.Empty();
	auto Register = [&](UMaterialExpression* E) { E->Material = Mat; Mat->GetExpressionCollection().AddExpression(E); return E; };
	auto Input = [&](const TCHAR* Name, UMaterialExpression* E, int32 Output = 0) { Register(E); FCustomInput& I = WPO->Inputs.AddDefaulted_GetRef(); I.InputName = Name; I.Input.Expression = E; I.Input.OutputIndex = Output; };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(Mat);
		UV->CoordinateIndex = Index;
		static const TCHAR* Names[] = { TEXT("UV"), TEXT("SecondaryUV"), TEXT("ThirdUV"), TEXT("FourthUV") };
		Input(Names[Index], UV);
	}
	Input(TEXT("Position"), NewObject<UMaterialExpressionWorldPosition>(Mat));
	Input(TEXT("CameraPosition"), NewObject<UMaterialExpressionCameraPositionWS>(Mat));
	Input(TEXT("Normal"), NewObject<UMaterialExpressionVertexNormalWS>(Mat));
	Input(TEXT("Tangent"), NewObject<UMaterialExpressionVertexTangentWS>(Mat));
	{
		auto* Y = NewObject<UMaterialExpressionConstant3Vector>(Mat);
		Y->Constant = FLinearColor(0, 1, 0, 1);
		Register(Y);
		auto* Bitangent = NewObject<UMaterialExpressionTransform>(Mat);
		Bitangent->TransformSourceType = TRANSFORMSOURCE_Tangent;
		Bitangent->TransformType = TRANSFORM_World;
		Bitangent->Input.Expression = Y;
		Input(TEXT("Bitangent"), Bitangent);
		auto* Color = NewObject<UMaterialExpressionVertexColor>(Mat);
		Register(Color);
		auto* RGBA = NewObject<UMaterialExpressionAppendVector>(Mat);
		RGBA->A.Expression = Color;
		RGBA->B.Expression = Color;
		RGBA->B.OutputIndex = 4;
		Input(TEXT("VertexColor"), RGBA);
	}

	// The program's inputs by semantic (OKPosition, OKNormal, OKTangent, OKUV0..3, OKColor), in Unity space and UV rows,
	// exactly as the shared outline parents feed it (Tools/GpuPoseAudit PrepareRuntimeSharedMaterial).
	FString Code = TEXT("const float UVFlip=1.0;float4 OKPosition=float4(-Position.x,Position.z,Position.y,100.0)*.01;float3 okn=normalize(float3(-Normal.x,Normal.z,Normal.y));float4 OKNormal=float4(okn,0);\n")
		TEXT("float4 OKUV0=float4(UV.x,1.0-UV.y,0,0);float4 OKUV1=float4(SecondaryUV.x,1.0-SecondaryUV.y,0,0);float4 OKUV2=float4(ThirdUV.x,1.0-ThirdUV.y,0,0);float4 OKUV3=float4(FourthUV.x,1.0-FourthUV.y,0,0);\n")
		TEXT("float3 okt=float3(-Tangent.x,Tangent.z,Tangent.y);float3 okb=float3(-Bitangent.x,Bitangent.z,Bitangent.y);float4 OKTangent=float4(okt,sign(dot(cross(okn,okt),okb)));float4 OKColor=VertexColor;\n");
	for (int32 I = 0; I < VL->Sizes.Num(); ++I)
	{
		Code += FString::Printf(TEXT("float4 cb%d[%d];\n"), I, VL->Sizes[I]);
		for (int32 J = 0; J < VL->Sizes[I]; ++J) Code += FString::Printf(TEXT("cb%d[%d]=0.0;\n"), I, J);
	}
	for (const FKKRuntimeShaderUniform& U : VL->Uniforms)
	{
		if (U.Rows)
		{
			// Object and view-projection matrices are identity: the program runs on world positions and its output
			// position minus its input is the hull offset.
			if (U.Name != TEXT("unity_ObjectToWorld") && U.Name != TEXT("unity_WorldToObject") && U.Name != TEXT("unity_MatrixVP")) return Fail(TEXT("outline vertex matrix unsupported: ") + U.Name);
			for (int32 Row = 0; Row < FMath::Min(U.Rows, VL->Sizes[U.CB] - U.Offset / 16); ++Row)
				Code += FString::Printf(TEXT("cb%d[%d]=float4(%d,%d,%d,%d);\n"), U.CB, U.Offset / 16 + Row, Row == 0, Row == 1, Row == 2, Row == 3);
			continue;
		}
		FString Components[4];
		if (U.Name == TEXT("_WorldSpaceCameraPos"))
		{
			const TCHAR* Camera[] = { TEXT("-CameraPosition.x*.01"), TEXT("CameraPosition.z*.01"), TEXT("CameraPosition.y*.01"), TEXT("1.0") };
			for (int32 C = 0; C < 4; ++C) Components[C] = Camera[C];
		}
		else if (U.Name.StartsWith(TEXT("unity_")) || U.Name == TEXT("_Time")) return Fail(TEXT("outline vertex uniform unsupported: ") + U.Name);
		else
		{
			FLinearColor V = U.Default;
			if (const float* S = B.Scalars.Find(*U.Name)) V = FLinearColor(*S, 0, 0, 0);
			else if (const FLinearColor* Vec = B.Vectors.Find(*U.Name)) V = *Vec;
			else return Fail(TEXT("outline vertex uniform not resolved: ") + U.Name);
			for (int32 C = 0; C < 4; ++C) Components[C] = OKLiteral((&V.R)[C]);
		}
		for (int32 C = 0; C < U.Dim; ++C)
		{
			FString Value = Components[C];
			if (U.Type == 1) Value = TEXT("asfloat(uint(int(") + Value + TEXT(")))");
			if (U.Type == 2) Value = TEXT("asfloat(uint(") + Value + TEXT("!=0))");
			Code += FString::Printf(TEXT("cb%d[%d].%c=%s;\n"), U.CB, (U.Offset / 4 + C) / 4, TEXT("xyzw")[(U.Offset / 4 + C) % 4], *Value);
		}
	}
	for (const FKKRuntimeShaderTexture& T : VL->Textures)
	{
		UTexture* Bound = Texture(T.Name);
		if (!Bound) Bound = OKDefaultOutlineTexture(T.Default.IsEmpty() ? FString(TEXT("white")) : T.Default);
		if (!Bound) return Fail(TEXT("outline vertex texture unavailable: ") + T.Name);
		auto* Object = NewObject<UMaterialExpressionTextureObject>(Mat);
		Object->Texture = Bound;
		Object->SamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture(Bound);
		const FString Name = FString::Printf(TEXT("VTexture%d"), T.Index);
		FCustomInput& I = WPO->Inputs.AddDefaulted_GetRef();
		I.InputName = *Name;
		I.Input.Expression = Register(Object);
		VP.ReplaceInline(*FString::Printf(TEXT("Texture%d."), T.Index), *(Name + TEXT(".")));
		VP.ReplaceInline(*FString::Printf(TEXT("Sampler%d,"), T.Sampler), *(Name + TEXT("Sampler,")));
	}
	WPO->Code = Code + VP + TEXT("\nreturn float3(-(o0.x-OKPosition.x),o0.z-OKPosition.z,o0.y-OKPosition.y)*100.0;");
	Register(WPO);
	Error.Reset();
	return WPO;
#else
	Error = TEXT("outline graphs are authored in the editor only");
	return nullptr;
#endif
}
