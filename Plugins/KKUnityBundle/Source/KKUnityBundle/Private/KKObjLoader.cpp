#include "KKObjLoader.h"
#include "Misc/FileHelper.h"
#include "Misc/DefaultValueHelper.h"

bool LoadObjFile(const FString& Path, FKKObjMesh& OutMesh, FString& OutError)
{
	OutMesh = FKKObjMesh();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		OutError = FString::Printf(TEXT("cannot read %s"), *Path);
		return false;
	}

	TArray<FVector> Positions;
	TArray<FVector2D> Tex;
	TArray<FVector2D> VertUV;
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	for (const FString& RawLine : Lines)
	{
		const FString Line = RawLine.TrimStartAndEnd();
		if (Line.StartsWith(TEXT("v ")))
		{
			TArray<FString> Parts;
			Line.ParseIntoArrayWS(Parts);
			if (Parts.Num() >= 4)
			{
				float X = 0, Y = 0, Z = 0;
				FDefaultValueHelper::ParseFloat(Parts[1], X);
				FDefaultValueHelper::ParseFloat(Parts[2], Y);
				FDefaultValueHelper::ParseFloat(Parts[3], Z);
				Positions.Add(FVector(-X * 100.0f, Z * 100.0f, Y * 100.0f));
			}
		}
		else if (Line.StartsWith(TEXT("vt ")))
		{
			TArray<FString> Parts;
			Line.ParseIntoArrayWS(Parts);
			if (Parts.Num() >= 3)
			{
				float U = 0, V = 0;
				FDefaultValueHelper::ParseFloat(Parts[1], U);
				FDefaultValueHelper::ParseFloat(Parts[2], V);
				Tex.Add(FVector2D(U, 1.0f - V));
			}
		}
		else if (Line.StartsWith(TEXT("f ")))
		{
			TArray<FString> Parts;
			Line.ParseIntoArrayWS(Parts);
			TArray<int32> Idx;
			TArray<int32> UvIdx;
			for (int32 i = 1; i < Parts.Num(); ++i)
			{
				TArray<FString> Corner;
				Parts[i].ParseIntoArray(Corner, TEXT("/"), true);
				int32 V = 0;
				FDefaultValueHelper::ParseInt(Corner[0], V);
				if (V < 0)
				{
					V = Positions.Num() + V + 1;
				}
				Idx.Add(V - 1);
				if (Corner.Num() >= 2 && !Corner[1].IsEmpty())
				{
					int32 T = 0;
					FDefaultValueHelper::ParseInt(Corner[1], T);
					UvIdx.Add(T - 1);
				}
				else
				{
					UvIdx.Add(INDEX_NONE);
				}
			}
			if (VertUV.Num() != Positions.Num())
			{
				VertUV.SetNumZeroed(Positions.Num());
			}
			for (int32 i = 0; i < Idx.Num(); ++i)
			{
				if (UvIdx.IsValidIndex(i) && UvIdx[i] != INDEX_NONE && Tex.IsValidIndex(UvIdx[i]) && Positions.IsValidIndex(Idx[i]))
				{
					VertUV[Idx[i]] = Tex[UvIdx[i]];
				}
			}
			for (int32 i = 1; i + 1 < Idx.Num(); ++i)
			{
				// Reverse winding: the OBJ (Unity, left handed) winding is the
				// opposite of UE's, and parts are now single sided.
				OutMesh.Triangles.Add(Idx[i + 1]);
				OutMesh.Triangles.Add(Idx[i]);
				OutMesh.Triangles.Add(Idx[0]);
			}
		}
	}

	OutMesh.Vertices = Positions;
	if (VertUV.Num() == Positions.Num() && Positions.Num() > 0)
	{
		OutMesh.UVs = VertUV;
	}
	else if (Tex.Num() == Positions.Num())
	{
		OutMesh.UVs = Tex;
	}
	if (OutMesh.Vertices.Num() == 0 || OutMesh.Triangles.Num() < 3)
	{
		OutError = TEXT("obj had no triangles");
		return false;
	}
	return true;
}
