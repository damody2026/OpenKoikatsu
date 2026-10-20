#pragma once

#include "CoreMinimal.h"

struct KKUNITYBUNDLE_API FKKObjMesh
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
};

bool KKUNITYBUNDLE_API LoadObjFile(const FString& Path, FKKObjMesh& OutMesh, FString& OutError);
