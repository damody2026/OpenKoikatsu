#include "OKModelAsset.h"
#include "KKRustExtract.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString UOKModelAsset::ModelPath() const
{
	return Library / TEXT("models") / ModelId.Left(2) / ModelId + TEXT(".okmodel");
}

bool UOKModelAsset::Refresh(FString& OutError)
{
	if (!FOkModel::Info(Library, ModelId, InfoJson, OutError)) return false;
	TSharedPtr<FJsonObject> Info;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(InfoJson), Info) || !Info)
	{
		OutError = TEXT("unreadable model info");
		return false;
	}
	auto Strings = [](const TArray<TSharedPtr<FJsonValue>>* Rows, const TCHAR* Field)
	{
		TArray<FString> Out;
		if (Rows) for (const TSharedPtr<FJsonValue>& Row : *Rows)
		{
			FString S;
			if (Field && Row->Type == EJson::Object) S = Row->AsObject()->GetStringField(Field); else Row->TryGetString(S);
			Out.Add(S);
		}
		return Out;
	};
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	Sources = Info->TryGetArrayField(TEXT("sources"), Rows) ? Strings(Rows, nullptr) : TArray<FString>();
	Prefabs = Info->TryGetArrayField(TEXT("prefabs"), Rows) ? Strings(Rows, TEXT("name")) : TArray<FString>();
	Materials = Info->TryGetArrayField(TEXT("materials"), Rows) ? Strings(Rows, TEXT("name")) : TArray<FString>();
	Textures = Info->TryGetArrayField(TEXT("textures"), Rows) ? Strings(Rows, TEXT("name")) : TArray<FString>();
	FString Edits;
	if (FOkModel::GetEdits(Library, ModelId, Edits)) SetEditsFromJson(Edits);
	return true;
}

FString UOKModelAsset::EditsJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> MaterialsJson = MakeShared<FJsonObject>();
	for (const FOKModelMaterialEdit& E : MaterialEdits)
	{
		if (E.Material.IsEmpty()) continue;
		TSharedRef<FJsonObject> M = MakeShared<FJsonObject>(), Colors = MakeShared<FJsonObject>(), Floats = MakeShared<FJsonObject>(), Tex = MakeShared<FJsonObject>();
		for (const auto& C : E.Colors)
		{
			Colors->SetArrayField(C.Key, { MakeShared<FJsonValueNumber>(C.Value.R), MakeShared<FJsonValueNumber>(C.Value.G), MakeShared<FJsonValueNumber>(C.Value.B), MakeShared<FJsonValueNumber>(C.Value.A) });
		}
		for (const auto& F : E.Floats) Floats->SetNumberField(F.Key, F.Value);
		for (const auto& T : E.Textures) Tex->SetStringField(T.Key, T.Value);
		M->SetObjectField(TEXT("colors"), Colors);
		M->SetObjectField(TEXT("floats"), Floats);
		M->SetObjectField(TEXT("textures"), Tex);
		MaterialsJson->SetObjectField(E.Material.ToLower(), M);
	}
	TSharedRef<FJsonObject> TexturesJson = MakeShared<FJsonObject>();
	for (const FOKModelTextureEdit& E : TextureEdits)
	{
		if (!E.Texture.IsEmpty() && !E.Png.FilePath.IsEmpty()) TexturesJson->SetStringField(E.Texture.ToLower(), E.Png.FilePath);
	}
	TSharedRef<FJsonObject> Meshes = MakeShared<FJsonObject>();
	auto Vec = [](const FVector& V) { return TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) }; };
	for (const FOKModelMeshEdit& E : MeshEdits)
	{
		if (E.Prefab.IsEmpty() || E.Mesh.IsEmpty()) continue;
		TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetBoolField(TEXT("hidden"), E.bHidden);
		M->SetArrayField(TEXT("translate"), Vec(E.Translate));
		M->SetArrayField(TEXT("rotate"), Vec(E.Rotate));
		M->SetArrayField(TEXT("scale"), Vec(E.Scale));
		Meshes->SetObjectField(E.Prefab.ToLower() + TEXT("/") + E.Mesh.ToLower(), M);
	}
	Root->SetObjectField(TEXT("materials"), MaterialsJson);
	Root->SetObjectField(TEXT("textures"), TexturesJson);
	Root->SetObjectField(TEXT("meshes"), Meshes);
	FString Out;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Out));
	return Out;
}

void UOKModelAsset::SetEditsFromJson(const FString& Json)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) return;
	MaterialEdits.Reset();
	TextureEdits.Reset();
	MeshEdits.Reset();
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	auto Vec = [](const TSharedPtr<FJsonObject>& O, const TCHAR* F, const FVector& Default)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		return O->TryGetArrayField(F, A) && A->Num() == 3 ? FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber()) : Default;
	};
	if (Root->TryGetObjectField(TEXT("materials"), Obj))
	{
		for (const auto& M : (*Obj)->Values)
		{
			FOKModelMaterialEdit E;
			E.Material = FString(*M.Key);
			const TSharedPtr<FJsonObject> O = M.Value->AsObject();
			const TSharedPtr<FJsonObject>* Sub = nullptr;
			if (O->TryGetObjectField(TEXT("colors"), Sub)) for (const auto& C : (*Sub)->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>& A = C.Value->AsArray();
				if (A.Num() == 4) E.Colors.Add(FString(*C.Key), FLinearColor(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber(), A[3]->AsNumber()));
			}
			if (O->TryGetObjectField(TEXT("floats"), Sub)) for (const auto& F : (*Sub)->Values) E.Floats.Add(FString(*F.Key), F.Value->AsNumber());
			if (O->TryGetObjectField(TEXT("textures"), Sub)) for (const auto& T : (*Sub)->Values) E.Textures.Add(FString(*T.Key), T.Value->AsString());
			MaterialEdits.Add(E);
		}
	}
	if (Root->TryGetObjectField(TEXT("textures"), Obj))
	{
		for (const auto& T : (*Obj)->Values)
		{
			FOKModelTextureEdit E;
			E.Texture = FString(*T.Key);
			E.Png.FilePath = T.Value->AsString();
			TextureEdits.Add(E);
		}
	}
	if (Root->TryGetObjectField(TEXT("meshes"), Obj))
	{
		for (const auto& M : (*Obj)->Values)
		{
			FOKModelMeshEdit E;
			FString(*M.Key).Split(TEXT("/"), &E.Prefab, &E.Mesh);
			const TSharedPtr<FJsonObject> O = M.Value->AsObject();
			O->TryGetBoolField(TEXT("hidden"), E.bHidden);
			E.Translate = Vec(O, TEXT("translate"), FVector::ZeroVector);
			E.Rotate = Vec(O, TEXT("rotate"), FVector::ZeroVector);
			E.Scale = Vec(O, TEXT("scale"), FVector::OneVector);
			MeshEdits.Add(E);
		}
	}
}

bool UOKModelAsset::SaveEdits(FString& OutError) const
{
	return FOkModel::SetEdits(Library, ModelId, EditsJson(), OutError);
}
