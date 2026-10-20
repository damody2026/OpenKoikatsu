#include "KKMaterialExposure.h"
#include "KKRustExtract.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionAppendVector.h"

FString OKExposedUniformInput(UMaterial* Mat, UMaterialExpressionCustom* PS, const TSet<FString>* Exposed,
	const FString& Name, int32 Dim, const FLinearColor& Default)
{
#if WITH_EDITOR
	if (!Mat || !PS || !Exposed || !Exposed->Contains(Name)) return FString();
	FString Input = TEXT("OKP");
	for (TCHAR C : Name) Input.AppendChar(FChar::IsAlnum(C) ? C : TEXT('_'));
	for (const FCustomInput& I : PS->Inputs) if (I.InputName == *Input) return Input;
	auto Register = [Mat](UMaterialExpression* E) { E->Material = Mat; Mat->GetExpressionCollection().AddExpression(E); };
	UMaterialExpression* Value = nullptr;
	if (Dim == 1)
	{
		auto* Scalar = NewObject<UMaterialExpressionScalarParameter>(Mat);
		Scalar->ParameterName = *Name; Scalar->DefaultValue = Default.R; Register(Scalar); Value = Scalar;
	}
	else
	{
		// A vector parameter's first output is RGB; append its alpha for the whole float4.
		auto* Vector = NewObject<UMaterialExpressionVectorParameter>(Mat);
		Vector->ParameterName = *Name; Vector->DefaultValue = Default; Register(Vector);
		auto* RGBA = NewObject<UMaterialExpressionAppendVector>(Mat);
		RGBA->A.Expression = Vector; RGBA->B.Expression = Vector; RGBA->B.OutputIndex = 4; Register(RGBA); Value = RGBA;
	}
	FCustomInput& I = PS->Inputs.AddDefaulted_GetRef();
	I.InputName = *Input; I.Input.Expression = Value;
	return Input;
#else
	return FString();
#endif
}

FString OKExposedComponent(const FString& Input, int32 Component, int32 Type)
{
	const FString V = FString::Printf(TEXT("(%s).%c"), *Input, TEXT("xyzw")[Component]);
	if (Type == 1) return FString::Printf(TEXT("asfloat(uint(int(%s)))"), *V);
	if (Type == 2) return FString::Printf(TEXT("asfloat((%s)!=0.0?1u:0u)"), *V);
	return V;
}

UMaterialExpressionTextureBase* OKNewTextureObject(UMaterial* Mat, const TSet<FString>* Exposed, const FString& Name)
{
	if (Exposed && Exposed->Contains(Name))
	{
		auto* Parameter = NewObject<UMaterialExpressionTextureObjectParameter>(Mat);
		Parameter->ParameterName = *Name;
		return Parameter;
	}
	return NewObject<UMaterialExpressionTextureObject>(Mat);
}

UTexture2D* OKExposureWhiteTexture()
{
	static UTexture2D* Texture = nullptr;
	if (!Texture)
	{
		Texture = UTexture2D::CreateTransient(1, 1, PF_B8G8R8A8);
		void* Data = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		const FColor White = FColor::White; FMemory::Memcpy(Data, &White, 4);
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Texture->SRGB = false; Texture->AddressX = Texture->AddressY = TA_Wrap; Texture->UpdateResource(); Texture->AddToRoot();
	}
	return Texture;
}

void OKPartMaterialIdentity(const FOkSession& Session, int32 Part, FString& OutMaterial, FString& OutSourceProperty)
{
	OutMaterial.Reset(); OutSourceProperty.Reset();
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Session.MaterialMetadataJson(Part)), Root) || !Root) return;
	const TSharedPtr<FJsonObject>* Material = nullptr;
	if (Root->TryGetObjectField(TEXT("material"), Material) && Material && *Material) (*Material)->TryGetStringField(TEXT("name"), OutMaterial);
	Root->TryGetStringField(TEXT("source_property"), OutSourceProperty);
}

bool OKIsItemPatternProperty(const FString& Name)
{
	for (int32 Slot = 1; Slot <= 3; ++Slot) if (OKItemPatternNames(Slot).Contains(Name)) return true;
	return false;
}

TArray<FString> OKItemPatternNames(int32 Slot, bool bTexture)
{
	const FString N = FString::FromInt(Slot);
	TArray<FString> Names = {TEXT("_Patternuv") + N, TEXT("_patternrotator") + N, TEXT("_patternclamp") + N, TEXT("_PatternMask") + N + TEXT("_ST"), TEXT("_Color") + N + TEXT("_2")};
	if (bTexture) Names.Add(TEXT("_PatternMask") + N);
	return Names;
}
