#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "OKModelAsset.generated.h"

/** Material values of an OK model (overlay on the converted material). */
USTRUCT(BlueprintType)
struct KKUNITYBUNDLE_API FOKModelMaterialEdit
{
	GENERATED_BODY()

	/** Material name in the model. */
	UPROPERTY(EditAnywhere, Category = "OK Model")
	FString Material;

	/** Shader colour properties (e.g. _Color, _Color2, _ShadowColor). */
	UPROPERTY(EditAnywhere, Category = "OK Model")
	TMap<FString, FLinearColor> Colors;

	/** Shader float properties (e.g. _rimV, _SpecularPower). */
	UPROPERTY(EditAnywhere, Category = "OK Model")
	TMap<FString, float> Floats;

	/** Shader texture slot (e.g. _MainTex, _ColorMask) -> texture name in this model. */
	UPROPERTY(EditAnywhere, Category = "OK Model")
	TMap<FString, FString> Textures;
};

/** A texture of the model replaced by a PNG (copied next to the model's edits when saved). */
USTRUCT(BlueprintType)
struct KKUNITYBUNDLE_API FOKModelTextureEdit
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "OK Model")
	FString Texture;

	UPROPERTY(EditAnywhere, Category = "OK Model", meta = (FilePathFilter = "png"))
	FFilePath Png;
};

/** One mesh of a prefab: hidden, or moved in the model's own (Unity, metre) space. */
USTRUCT(BlueprintType)
struct KKUNITYBUNDLE_API FOKModelMeshEdit
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "OK Model")
	FString Prefab;

	UPROPERTY(EditAnywhere, Category = "OK Model")
	FString Mesh;

	UPROPERTY(EditAnywhere, Category = "OK Model")
	bool bHidden = false;

	/** Metres, model space (X right, Y up, Z forward). */
	UPROPERTY(EditAnywhere, Category = "OK Model")
	FVector Translate = FVector::ZeroVector;

	/** Degrees about the model's X, Y, Z axes (applied in that order). */
	UPROPERTY(EditAnywhere, Category = "OK Model")
	FVector Rotate = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "OK Model")
	FVector Scale = FVector::OneVector;
};

/**
 * An OK base library model (.okmodel: one converted Unity source: prefabs, meshes, materials, textures, rigs).
 * The asset references the model inside its library; edits are an overlay saved next to the model
 * (<id>.okedit.json) and apply wherever the model is used (characters opened from the library).
 */
UCLASS(BlueprintType)
class KKUNITYBUNDLE_API UOKModelAsset : public UObject
{
	GENERATED_BODY()

public:
	/** OK base library directory (contains oklib.json). */
	UPROPERTY(VisibleAnywhere, Category = "Source")
	FString Library;

	/** Model id (32 hex digits; the file is <library>/models/xx/<id>.okmodel). */
	UPROPERTY(VisibleAnywhere, Category = "Source")
	FString ModelId;

	/** Unity sources this model was converted from (bundle paths / zipmod entries). */
	UPROPERTY(VisibleAnywhere, Category = "Source")
	TArray<FString> Sources;

	UPROPERTY(VisibleAnywhere, Category = "Contents")
	TArray<FString> Prefabs;

	UPROPERTY(VisibleAnywhere, Category = "Contents")
	TArray<FString> Materials;

	UPROPERTY(VisibleAnywhere, Category = "Contents")
	TArray<FString> Textures;

	UPROPERTY(EditAnywhere, Category = "Edits")
	TArray<FOKModelMaterialEdit> MaterialEdits;

	UPROPERTY(EditAnywhere, Category = "Edits")
	TArray<FOKModelTextureEdit> TextureEdits;

	UPROPERTY(EditAnywhere, Category = "Edits")
	TArray<FOKModelMeshEdit> MeshEdits;

	/** Full contents of the model (JSON from the library). */
	FString InfoJson;

	/** Path of the model file. */
	FString ModelPath() const;
	/** Reads contents and the saved edits from the library. */
	bool Refresh(FString& OutError);
	/** Writes the edits overlay (an empty set removes it). */
	bool SaveEdits(FString& OutError) const;
	/** Edits as the library's JSON. */
	FString EditsJson() const;
	/** Edits from the library's JSON. */
	void SetEditsFromJson(const FString& Json);
};
