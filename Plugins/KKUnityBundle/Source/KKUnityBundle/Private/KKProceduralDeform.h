#pragma once
#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "KKAffineProceduralMeshComponent.h"

// Bit-exact dirty check before float-to-double conversion. Snapshot size is
// bounded by this renderer; no hashing, tolerance, or accumulated-motion loss.
inline bool OKSyncSourceGeometry(TArray<float>& Snapshot,int32 Count,const float* Positions,
    const float* Normals,const float* Tangents,bool Enabled=true)
{
    // A bypass can update canonical/render data without updating this snapshot.
    // Invalidate it so re-enabling cannot mistake an old pose for current output.
    if(!Enabled){Snapshot.Reset();return true;}
    const int32 NP=Count*3,NN=Normals?Count*3:0,NT=Tangents?Count*4:0;
    const int32 Total=NP+NN+NT;
    bool Changed=Snapshot.Num()!=Total;
    if(!Changed)Changed=FMemory::Memcmp(Snapshot.GetData(),Positions,NP*sizeof(float))!=0
        ||(NN&&FMemory::Memcmp(Snapshot.GetData()+NP,Normals,NN*sizeof(float))!=0)
        ||(NT&&FMemory::Memcmp(Snapshot.GetData()+NP+NN,Tangents,NT*sizeof(float))!=0);
    if(Changed){Snapshot.SetNumUninitialized(Total);FMemory::Memcpy(Snapshot.GetData(),Positions,NP*sizeof(float));
        if(NN)FMemory::Memcpy(Snapshot.GetData()+NP,Normals,NN*sizeof(float));
        if(NT)FMemory::Memcpy(Snapshot.GetData()+NP+NN,Tangents,NT*sizeof(float));}
    return Changed;
}

// A Unity submesh may reference only a small fraction of its shared vertex
// stream. Preserve every authored vertex attribute and triangle, but upload
// only referenced vertices. Canonical morph/source arrays remain unchanged.
inline void OKCompactProceduralSections(UProceduralMeshComponent* Comp,int32 SourceCount,
	TArray<TArray<int32>>& DrawVertices)
{
	if(!Comp)return;
	DrawVertices.SetNum(Comp->GetNumSections());
	for(int32 Index=0;Index<Comp->GetNumSections();++Index){
		const auto* Section=Comp->GetProcMeshSection(Index);
		if(!Section||Section->ProcVertexBuffer.Num()!=SourceCount)continue;
		TArray<int32> Remap;Remap.Init(INDEX_NONE,SourceCount);bool Valid=true;
		for(uint32 Vertex:Section->ProcIndexBuffer){if(Vertex>=uint32(SourceCount)){Valid=false;break;}Remap[Vertex]=0;}
		if(!Valid)continue;
		TArray<int32> Used;for(int32 Vertex=0;Vertex<SourceCount;++Vertex)if(Remap[Vertex]!=INDEX_NONE){Remap[Vertex]=Used.Num();Used.Add(Vertex);}
		if(Used.Num()==SourceCount){DrawVertices[Index].Reset();continue;}
		FProcMeshSection Compact=*Section;Compact.ProcVertexBuffer.Reset(Used.Num());Compact.SectionLocalBox=FBox(ForceInit);
		for(int32 Vertex:Used){Compact.ProcVertexBuffer.Add(Section->ProcVertexBuffer[Vertex]);Compact.SectionLocalBox+=Section->ProcVertexBuffer[Vertex].Position;}
		for(uint32& Vertex:Compact.ProcIndexBuffer)Vertex=uint32(Remap[Vertex]);
		DrawVertices[Index]=MoveTemp(Used);Comp->SetProcMeshSection(Index,Compact);
	}
}

// Unity's passes share one skinned vertex stream. Procedural copies used for
// an overlay/outline must receive the same deformation while retaining their
// own rest offset, UVs, colours, normals and triangle winding.
inline int32 OKUpdateSecondarySections(UProceduralMeshComponent* Comp,
	const TArray<FVector>& Deformed,
	const TArray<TArray<FVector>>& RestOffsets,
	const TArray<TArray<FVector>>& RestNormals)
{
	if (!Comp || RestOffsets.Num() != Comp->GetNumSections()
		|| RestNormals.Num() != RestOffsets.Num()) return INDEX_NONE;
	for (int32 Index = 1; Index < Comp->GetNumSections(); ++Index)
	{
		const FProcMeshSection* Section = Comp->GetProcMeshSection(Index);
		if (!Section || Section->ProcVertexBuffer.Num() != Deformed.Num()
			|| RestOffsets[Index].Num() != Deformed.Num()
			|| RestNormals[Index].Num() != Deformed.Num()) return INDEX_NONE;
	}
	for (int32 Index = 1; Index < Comp->GetNumSections(); ++Index)
	{
		const FProcMeshSection* Section = Comp->GetProcMeshSection(Index);
		TArray<FVector> Vertices; TArray<FVector2D> UVs;
		TArray<FColor> Colors; TArray<FProcMeshTangent> Tangents;
		for (int32 Vertex = 0; Vertex < Deformed.Num(); ++Vertex)
		{
			Vertices.Add(Deformed[Vertex] + RestOffsets[Index][Vertex]);
			const FProcMeshVertex& Source = Section->ProcVertexBuffer[Vertex];
			UVs.Add(Source.UV0); Colors.Add(Source.Color); Tangents.Add(Source.Tangent);
		}
		UKKAffineProceduralMeshComponent::UpdateStreams(Comp, Index, Vertices, RestNormals[Index], UVs, Colors, Tangents);
	}
	return Comp->GetNumSections() - 1;
}
