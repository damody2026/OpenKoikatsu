// Scene proxy adapted from the installed UE5.8 ProceduralMeshComponent.cpp.
// Copyright Epic Games, Inc. All Rights Reserved.
// Project changes: public section access, opt-in routing, and dirty UV/color uploads.
#include "KKAffineProceduralMeshComponent.h"
#include "BodySetupEnums.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "Engine/Engine.h"
#include "RenderUtils.h"
#include "PrimitiveDrawingUtils.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "PhysicsEngine/BodySetup.h"
#include "DynamicMeshBuilder.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "StaticMeshResources.h"
#include "RayTracingInstance.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HAL/IConsoleManager.h"
#include "RenderingThread.h"
#include "Async/ParallelFor.h"
#include "OKResidentGpuDeform.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

static TAutoConsoleVariable<int32> CVarKKStreamParallel(TEXT("ok.Stream.ParallelPacking"),0,TEXT("Pack independent animated vertices in parallel; 0 retains serial packing. Enable only after workload measurement."));
static TAutoConsoleVariable<int32> CVarKKStreamBatch(TEXT("ok.Stream.BatchPacking"),1,TEXT("0 serial oracle; 1 pack large multi-section animation batches; 2 force batch packing for verification."));
// GT-only, process-wide identity. Per-component counters can collide after
// actor replacement and admit a packet belonging to another character.
static uint64 GKKGpuStreamProxyEpoch=0;

// D3D12's installed upload allocator pools allocations <=64KiB separately.
// Static-buffer partial writes retain all other bytes; dynamic buffers can
// rename their entire backing store, so they must always use a full write.
static TAutoConsoleVariable<int32> CVarKKStreamUploadChunkBytes(
    TEXT("ok.Animation.StreamUploadChunkBytes"),65536,
    TEXT("Maximum static vertex-buffer upload packet bytes (0 = one full upload)."));
static void UploadKKStreamBuffer(FRHICommandListBase& Commands,FRHIBuffer* Buffer,const void* Source,uint32 Size)
{
    const int32 Requested=CVarKKStreamUploadChunkBytes.GetValueOnRenderThread();
    const uint32 Chunk=Requested>0&&!EnumHasAnyFlags(Buffer->GetUsage(),EBufferUsageFlags::AnyDynamic)
        ? FMath::Max(4u,uint32(Requested)&~3u) : Size;
    for(uint32 Offset=0;Offset<Size;)
    {
        const uint32 Bytes=FMath::Min(Chunk,Size-Offset);
        void* Destination=Commands.LockBuffer(Buffer,Offset,Bytes,RLM_WriteOnly);
        FMemory::Memcpy(Destination,static_cast<const uint8*>(Source)+Offset,Bytes);
        Commands.UnlockBuffer(Buffer);
        Offset+=Bytes;
    }
}

/** Class representing a single section of the proc mesh */
class FKKStreamProxySection
{
public:
	/** Material applied to this section */
	UMaterialInterface* Material;
	/** Vertex buffer for this section */
	FStaticMeshVertexBuffers VertexBuffers;
	/** Index buffer for this section */
	FDynamicMeshIndexBuffer32 IndexBuffer;
	/** Vertex factory for this section */
	FLocalVertexFactory VertexFactory;
	/** Whether this section is currently visible */
	bool bSectionVisible;
    uint32 UVUploads = 0, ColorUploads = 0;
    TUniquePtr<FOKResidentGpuDeform> GpuDeform;
    TRefCountPtr<FRDGPooledBuffer> GpuPositionTarget,GpuTangentTarget;
    TRefCountPtr<FRDGPooledBuffer> CoatingBindings;
    uint64 CoatingRevision=0,CoatingGeneration=0,CoatingSourceEpoch=0;
    uint32 CoatingSourceCount=0;
    int32 CoatingSourceSection=INDEX_NONE;
    bool bGpuPublished=false;

#if RHI_RAYTRACING
	FRayTracingGeometry RayTracingGeometry;
#endif

	FKKStreamProxySection(ERHIFeatureLevel::Type InFeatureLevel)
	: Material(NULL)
	, VertexFactory(InFeatureLevel, "FKKStreamProxySection")
	, bSectionVisible(true)
	{}
};

/** 
 *	Struct used to send update to mesh data 
 *	Arrays may be empty, in which case no update is performed.
 */
// Match StaticMeshVertexBuffer's default tangent representation exactly.
using FKKPackedStreamTangents = TStaticMeshVertexTangentDatum<FPackedNormal>;
static FKKPackedStreamTangents PackKKAnimatedTangents(const FProcMeshVertex& Source)
{
    FDynamicMeshVertex Vertex;
    Vertex.TangentX = Source.Tangent.TangentX;
    Vertex.TangentZ = Source.Normal;
    Vertex.TangentZ.Vector.W = Source.Tangent.bFlipTangentY ? -127 : 127;
    FKKPackedStreamTangents Packed;
    Packed.SetTangents(Vertex.TangentX.ToFVector3f(), Vertex.GetTangentY(), Vertex.TangentZ.ToFVector3f());
    return Packed;
}
class FKKStreamUpdateData
{
public:
	/** Section to update */
	int32 TargetSection;
	/** New vertex information */
	TArray<FVector3f> Positions;
    TArray<FKKPackedStreamTangents> Tangents;
    TArray<FVector2f> UVs;
    TArray<FColor> Colors;
	// Omitted or unchanged immutable streams must not allocate/upload every
	// animation frame. This packet owns its flags alongside its vertex copy.
	bool bUpdateColors = false;
	bool bUpdateUVs = false;
};

static void ConvertKKStreamVertex(FDynamicMeshVertex& Vert, const FProcMeshVertex& ProcVert)
{
	Vert.Position = (FVector3f)ProcVert.Position;
	Vert.Color = ProcVert.Color;
	Vert.TextureCoordinate[0] = FVector2f(ProcVert.UV0);	// LWC_TODO: Precision loss
	Vert.TextureCoordinate[1] = FVector2f(ProcVert.UV1);	// LWC_TODO: Precision loss
	Vert.TextureCoordinate[2] = FVector2f(ProcVert.UV2);	// LWC_TODO: Precision loss
	Vert.TextureCoordinate[3] = FVector2f(ProcVert.UV3);	// LWC_TODO: Precision loss
	Vert.TangentX = ProcVert.Tangent.TangentX;
	Vert.TangentZ = ProcVert.Normal;
	Vert.TangentZ.Vector.W = ProcVert.Tangent.bFlipTangentY ? -127 : 127;
}

/** Procedural mesh scene proxy */
class FKKStreamSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FKKStreamSceneProxy(UProceduralMeshComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, BodySetup(Component->GetBodySetup())
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetShaderPlatform()))
	{
		// Copy each section
		const int32 NumSections = Component->GetNumSections();
		Sections.AddZeroed(NumSections);
		for (int SectionIdx = 0; SectionIdx < NumSections; SectionIdx++)
		{
			FProcMeshSection& SrcSection = *Component->GetProcMeshSection(SectionIdx);
			if (SrcSection.ProcIndexBuffer.Num() > 0 && SrcSection.ProcVertexBuffer.Num() > 0)
			{
				FKKStreamProxySection* NewSection = new FKKStreamProxySection(GetScene().GetFeatureLevel());

				// Copy data from vertex buffer
				const int32 NumVerts = SrcSection.ProcVertexBuffer.Num();

				// Allocate verts

				TArray<FDynamicMeshVertex> Vertices;
				Vertices.SetNumUninitialized(NumVerts);
				// Copy verts
				for (int VertIdx = 0; VertIdx < NumVerts; VertIdx++)
				{
					const FProcMeshVertex& ProcVert = SrcSection.ProcVertexBuffer[VertIdx];
					FDynamicMeshVertex& Vert = Vertices[VertIdx];
					ConvertKKStreamVertex(Vert, ProcVert);
				}

				// Copy index buffer
				NewSection->IndexBuffer.Indices = SrcSection.ProcIndexBuffer;

				NewSection->VertexBuffers.InitFromDynamicVertex(&NewSection->VertexFactory, Vertices, 4);

				// Enqueue initialization of render resource
				BeginInitResource(&NewSection->VertexBuffers.PositionVertexBuffer);
				BeginInitResource(&NewSection->VertexBuffers.StaticMeshVertexBuffer);
				BeginInitResource(&NewSection->VertexBuffers.ColorVertexBuffer);
				BeginInitResource(&NewSection->IndexBuffer);
				BeginInitResource(&NewSection->VertexFactory);

				// Grab material
				NewSection->Material = Component->GetMaterial(SectionIdx);
				if (NewSection->Material == NULL)
				{
					NewSection->Material = UMaterial::GetDefaultMaterial(MD_Surface);
				}

				// Copy visibility info
				NewSection->bSectionVisible = SrcSection.bSectionVisible;

				// Save ref to new section
				Sections[SectionIdx] = NewSection;

#if RHI_RAYTRACING
				if (IsRayTracingEnabled())
				{
					ENQUEUE_RENDER_COMMAND(InitProceduralMeshRayTracingGeometry)(
						[this, DebugName = Component->GetFName(), NewSection](FRHICommandListImmediate& RHICmdList)
					{
						FRayTracingGeometryInitializer Initializer;
						Initializer.DebugName = DebugName;
						Initializer.IndexBuffer = NewSection->IndexBuffer.IndexBufferRHI;
						Initializer.TotalPrimitiveCount = NewSection->IndexBuffer.Indices.Num() / 3;
						Initializer.GeometryType = RTGT_Triangles;
						Initializer.bFastBuild = true;
						Initializer.bAllowUpdate = false;

						FRayTracingGeometrySegment Segment;
						Segment.VertexBuffer = NewSection->VertexBuffers.PositionVertexBuffer.VertexBufferRHI;
						Segment.MaxVertices = NewSection->VertexBuffers.PositionVertexBuffer.GetNumVertices();
						Segment.NumPrimitives = Initializer.TotalPrimitiveCount;
						
						Initializer.Segments.Add(Segment);

						NewSection->RayTracingGeometry.SetInitializer(Initializer);
						NewSection->RayTracingGeometry.InitResource(RHICmdList);
					});
				}
#endif
			}
		}
	}

	virtual ~FKKStreamSceneProxy()
	{
		for (FKKStreamProxySection* Section : Sections)
		{
			if (Section != nullptr)
			{
				Section->VertexBuffers.PositionVertexBuffer.ReleaseResource();
				Section->VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
				Section->VertexBuffers.ColorVertexBuffer.ReleaseResource();
				Section->IndexBuffer.ReleaseResource();
				Section->VertexFactory.ReleaseResource();

#if RHI_RAYTRACING
				Section->RayTracingGeometry.ReleaseResource();
#endif

				delete Section;
			}
		}
	}

	/** Called on render thread to assign new dynamic data */
	void UpdateSection_RenderThread(FRHICommandListBase& RHICmdList, FKKStreamUpdateData* SectionData)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(KK_StreamUpload);

		// Check we have data 
		if(	SectionData != nullptr) 			
		{
			// Check it references a valid section
			if (SectionData->TargetSection < Sections.Num() &&
				Sections[SectionData->TargetSection] != nullptr)
			{
				FKKStreamProxySection* Section = Sections[SectionData->TargetSection];
                if(Section->bGpuPublished){Section->GpuDeform->Release();Section->bGpuPublished=false;}

				// Lock vertex buffer
				const int32 NumVerts = SectionData->Positions.Num();
			
                // Packing runs on the existing CPU jobs. The render thread only
                // publishes contiguous arrays; immutable UV/color retain their flags.
                auto& Positions = Section->VertexBuffers.PositionVertexBuffer;
                auto& Tangents = Section->VertexBuffers.StaticMeshVertexBuffer;
                check(!Tangents.GetUseHighPrecisionTangentBasis());
                check(Tangents.GetTangentSize() == NumVerts * sizeof(FKKPackedStreamTangents));
                FMemory::Memcpy(Positions.GetVertexData(), SectionData->Positions.GetData(), NumVerts * sizeof(FVector3f));
                FMemory::Memcpy(Tangents.GetTangentData(), SectionData->Tangents.GetData(), Tangents.GetTangentSize());
                if (SectionData->bUpdateUVs || SectionData->bUpdateColors)
                for (int32 i=0; i<NumVerts; ++i)
                {
                    if (SectionData->bUpdateUVs)
                        for (int32 UV=0; UV<4; ++UV)
                            Tangents.SetVertexUV(i, UV, SectionData->UVs[i*4+UV]);
                    if (SectionData->bUpdateColors) Section->VertexBuffers.ColorVertexBuffer.VertexColor(i)=SectionData->Colors[i];
                }

				{
					auto& VertexBuffer = Section->VertexBuffers.PositionVertexBuffer;
					UploadKKStreamBuffer(RHICmdList,VertexBuffer.VertexBufferRHI,VertexBuffer.GetVertexData(),VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
				}

				if (SectionData->bUpdateColors)
				{
                    ++Section->ColorUploads;
					auto& VertexBuffer = Section->VertexBuffers.ColorVertexBuffer;
					UploadKKStreamBuffer(RHICmdList,VertexBuffer.VertexBufferRHI,VertexBuffer.GetVertexData(),VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
				}

				{
					auto& VertexBuffer = Section->VertexBuffers.StaticMeshVertexBuffer;
					UploadKKStreamBuffer(RHICmdList,VertexBuffer.TangentsVertexBuffer.VertexBufferRHI,VertexBuffer.GetTangentData(),VertexBuffer.GetTangentSize());
				}

				if (SectionData->bUpdateUVs)
				{
                    ++Section->UVUploads;
					auto& VertexBuffer = Section->VertexBuffers.StaticMeshVertexBuffer;
					UploadKKStreamBuffer(RHICmdList,VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI,VertexBuffer.GetTexCoordData(),VertexBuffer.GetTexCoordSize());
				}

#if RHI_RAYTRACING
				if (IsRayTracingEnabled())
				{
					Section->RayTracingGeometry.ReleaseResource();

					FRayTracingGeometryInitializer Initializer;
					Initializer.IndexBuffer = Section->IndexBuffer.IndexBufferRHI;
					Initializer.TotalPrimitiveCount = Section->IndexBuffer.Indices.Num() / 3;
					Initializer.GeometryType = RTGT_Triangles;
					Initializer.bFastBuild = true;
					Initializer.bAllowUpdate = false;

					FRayTracingGeometrySegment Segment;
					Segment.VertexBuffer = Section->VertexBuffers.PositionVertexBuffer.VertexBufferRHI;
					Segment.MaxVertices = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices();
					Segment.NumPrimitives = Initializer.TotalPrimitiveCount;

					Initializer.Segments.Add(Segment);

					Section->RayTracingGeometry.SetInitializer(Initializer);
					Section->RayTracingGeometry.InitResource(RHICmdList);
				}
#endif
			}

			// Free data sent from game thread
			delete SectionData;
		}
	}

    bool UpdateGpuSection_RenderThread(FRHICommandListImmediate& Commands,int32 Index,const FKKGpuStreamRequest& Request,uint64& Generation,FString& Error)
    {
        FRDGBuilder Graph(Commands);const bool Ok=AppendGpuSection_RenderThread(Commands,Graph,Index,Request,Generation,Error);Graph.Execute();return Ok;
    }
    bool AppendGpuSection_RenderThread(FRHICommandListImmediate& Commands,FRDGBuilder& Graph,int32 Index,const FKKGpuStreamRequest& Request,uint64& Generation,FString& Error)
    {
        check(IsInRenderingThread());
        if(!Sections.IsValidIndex(Index)||!Sections[Index]){Error=TEXT("GPU section missing");return false;}
#if RHI_RAYTRACING
        if(IsRayTracingEnabled()){Error=TEXT("GPU stream ray tracing publication is not validated; retain CPU renderer");return false;}
#endif
        auto& S=*Sections[Index];
          if(Request.QueryGenerationOnly){Generation=S.GpuDeform?S.GpuDeform->GetStats().Generation:0;Error.Reset();return true;}
        const uint32 Count=S.VertexBuffers.PositionVertexBuffer.GetNumVertices();
        const bool Install=!Request.Source.IsEmpty()||!Request.Draw.IsEmpty();
        if(Install&&(Request.Source.IsEmpty()||Request.Draw.Num()!=int32(Count))){Error=TEXT("GPU source/draw count differs from immutable section");return false;}
        if(!S.GpuDeform)S.GpuDeform=MakeUnique<FOKResidentGpuDeform>();
        if(!S.GpuPositionTarget){
            auto Wrap=[&](FRHIBuffer* Buffer,const TCHAR* Name){
                auto Desc=FRDGBufferDesc::CreateBufferDesc(4,Buffer->GetSize()/4);Desc.Usage=Buffer->GetUsage();
                return TRefCountPtr<FRDGPooledBuffer>(new FRDGPooledBuffer(Commands,Buffer,Desc,Desc.NumElements,Name));
            };
            S.GpuPositionTarget=Wrap(S.VertexBuffers.PositionVertexBuffer.VertexBufferRHI,TEXT("Stream.GpuPositionTarget"));
            S.GpuTangentTarget=Wrap(S.VertexBuffers.StaticMeshVertexBuffer.TangentsVertexBuffer.VertexBufferRHI,TEXT("Stream.GpuTangentTarget"));
        }
        FOKResidentDeformFrame Frame;
        const FMatrix44f* Post=Request.HasPostCorrection?&Request.PostCorrection:nullptr;
        const bool Ok=Install?S.GpuDeform->Install(Graph,Request.Source,Request.Draw,Request.Palette,Request.FallbackSlot,Post,Request.ExpectedGeneration,Frame,Error,512ull*1024*1024,Request.NativeUnitySpace)
            :S.GpuDeform->Update(Graph,Request.Palette,Post,Request.ExpectedGeneration,Frame,Error);
        if(Ok){
            check(Frame.DrawCount==Count);
            auto* Positions=Graph.RegisterExternalBuffer(S.GpuPositionTarget);auto* Tangents=Graph.RegisterExternalBuffer(S.GpuTangentTarget);
            AddCopyBufferPass(Graph,Positions,0,Frame.Positions,0,uint64(Count)*12);
            AddCopyBufferPass(Graph,Tangents,0,Frame.Tangents,0,uint64(Count)*8);
            Graph.SetBufferAccessFinal(Positions,ERHIAccess::VertexOrIndexBuffer|ERHIAccess::SRVGraphics);
            Graph.SetBufferAccessFinal(Tangents,ERHIAccess::VertexOrIndexBuffer|ERHIAccess::SRVGraphics);
        }
        Generation=S.GpuDeform->GetStats().Generation;
        if(Ok)S.bGpuPublished=true;
        return Ok;
    }
    bool CoatingSection_RenderThread(FRHICommandListImmediate& Commands,FRDGBuilder& Graph,int32 Index,FKKStreamSceneProxy* Source,int32 SourceIndex,const FKKGpuCoatingRequest& Request,uint64& Generation,FString& Error,bool& Retryable)
    {
        check(IsInRenderingThread());
        auto Fail=[&](const TCHAR* Message){Error=Message;return false;};
        if(Source==this||!Source||!Sections.IsValidIndex(Index)||!Sections[Index]||!Source->Sections.IsValidIndex(SourceIndex)||!Source->Sections[SourceIndex])return Fail(TEXT("Coating proxy/section invalid"));
#if RHI_RAYTRACING
        if(IsRayTracingEnabled())return Fail(TEXT("Coating ray tracing publication unsupported"));
#endif
        auto& S=*Sections[Index];auto& Origin=*Source->Sections[SourceIndex];
        Generation=S.CoatingGeneration;
        if(Request.ExpectedGeneration!=Generation)return Fail(TEXT("Stale coating generation"));
        const uint32 Count=S.VertexBuffers.PositionVertexBuffer.GetNumVertices(),SourceCount=Origin.VertexBuffers.PositionVertexBuffer.GetNumVertices();
        if(!Count)return Fail(TEXT("Coating target has no vertices"));
        if(!Origin.bGpuPublished||!Origin.GpuDeform){Retryable=true;return Fail(TEXT("Coating source has no published GPU position stream"));}
        const bool Install=!Request.Bindings.IsEmpty();
        if(Install){if(Request.Bindings.Num()!=int32(Count)||uint64(Count)*sizeof(FOKGpuCoatingBinding)>128ull*1024*1024||!Request.Revision)return Fail(TEXT("Coating binding count/revision/budget invalid"));if(!OKValidateGpuCoatingBindings(Request.Bindings,SourceCount,Request.SourceToOverlay,Error))return false;}
        else if(!S.CoatingBindings||S.CoatingRevision!=Request.Revision||S.CoatingSourceEpoch!=Request.SourceEpoch||S.CoatingSourceCount!=SourceCount||S.CoatingSourceSection!=SourceIndex)return Fail(TEXT("Coating resident binding identity changed"));
        FOKResidentDeformFrame SourceFrame;
        if(!Origin.GpuDeform->GetCurrent(Graph,Origin.GpuDeform->GetStats().Generation,SourceFrame))return Fail(TEXT("Coating source resident frame unavailable"));
        auto Wrap=[&](FRHIBuffer* Buffer,const TCHAR* Name){auto Desc=FRDGBufferDesc::CreateBufferDesc(4,Buffer->GetSize()/4);Desc.Usage=Buffer->GetUsage();return TRefCountPtr<FRDGPooledBuffer>(new FRDGPooledBuffer(Commands,Buffer,Desc,Desc.NumElements,Name));};
        if(!S.GpuPositionTarget){S.GpuPositionTarget=Wrap(S.VertexBuffers.PositionVertexBuffer.VertexBufferRHI,TEXT("Coating.PositionTarget"));S.GpuTangentTarget=Wrap(S.VertexBuffers.StaticMeshVertexBuffer.TangentsVertexBuffer.VertexBufferRHI,TEXT("Coating.TangentTarget"));}
        auto* B=Install?CreateStructuredBuffer(Graph,TEXT("Coating.ResidentBindings"),sizeof(FOKGpuCoatingBinding),Count,Request.Bindings.GetData(),uint64(Count)*sizeof(FOKGpuCoatingBinding)):Graph.RegisterExternalBuffer(S.CoatingBindings);
        auto* O=Graph.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FOKGpuDeformOutput),Count),TEXT("Coating.Deformed"));
        auto* Positions=Graph.RegisterExternalBuffer(S.GpuPositionTarget);auto* Tangents=Graph.RegisterExternalBuffer(S.GpuTangentTarget);
        auto PositionDesc=FRDGBufferDesc::CreateByteAddressDesc(Count*12);PositionDesc.Usage|=EBufferUsageFlags::VertexBuffer;
        auto TangentDesc=FRDGBufferDesc::CreateByteAddressDesc(Count*8);TangentDesc.Usage|=EBufferUsageFlags::VertexBuffer;
        auto* PackedPositions=Graph.CreateBuffer(PositionDesc,TEXT("Coating.PackedPositions"));auto* PackedTangents=Graph.CreateBuffer(TangentDesc,TEXT("Coating.PackedTangents"));
        if(!OKAddRuntimeGpuCoatingPass(Graph,SourceFrame.Positions,B,O,SourceCount,Count,Request.SourceToOverlay)||!OKAddRuntimeGpuPackPass(Graph,O,PackedPositions,PackedTangents,Count))return Fail(TEXT("Coating GPU descriptor/transform unsupported"));
        AddCopyBufferPass(Graph,Positions,0,PackedPositions,0,uint64(Count)*12);AddCopyBufferPass(Graph,Tangents,0,PackedTangents,0,uint64(Count)*8);
        if(Install)Graph.QueueBufferExtraction(B,&S.CoatingBindings);
        Graph.SetBufferAccessFinal(Positions,ERHIAccess::VertexOrIndexBuffer|ERHIAccess::SRVGraphics);Graph.SetBufferAccessFinal(Tangents,ERHIAccess::VertexOrIndexBuffer|ERHIAccess::SRVGraphics);
        S.CoatingRevision=Request.Revision;S.CoatingSourceEpoch=Request.SourceEpoch;S.CoatingSourceCount=SourceCount;S.CoatingSourceSection=SourceIndex;Generation=++S.CoatingGeneration;Error.Reset();return true;
    }
	void SetSectionVisibility_RenderThread(int32 SectionIndex, bool bNewVisibility)
	{
		check(IsInRenderingThread());

		if(	SectionIndex < Sections.Num() &&
			Sections[SectionIndex] != nullptr)
		{
			Sections[SectionIndex]->bSectionVisible = bNewVisibility;
		}
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(KK_StreamDraw);


		// Set up wireframe material (if needed)
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

		FColoredMaterialRenderProxy* WireframeMaterialInstance = NULL;
		if (bWireframe)
		{
			WireframeMaterialInstance = new FColoredMaterialRenderProxy(
				GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : NULL,
				FLinearColor(0, 0.5f, 1.f)
				);

			Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
		}

		// Primitive parameters have no section/view inputs. Surface and outline
        // share one transform/bounds buffer for this collector invocation.
        // Never retain it across frames or collectors.
        FDynamicPrimitiveUniformBuffer* PrimitiveUniform = nullptr;
		// Iterate over sections
		for (const FKKStreamProxySection* Section : Sections)
		{
			if (Section != nullptr && Section->bSectionVisible)
			{
				FMaterialRenderProxy* MaterialProxy = bWireframe ? WireframeMaterialInstance : Section->Material->GetRenderProxy();

				// For each view..
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					if (VisibilityMap & (1 << ViewIndex))
					{
						const FSceneView* View = Views[ViewIndex];
						// Draw the mesh.
						FMeshBatch& Mesh = Collector.AllocateMesh();
						FMeshBatchElement& BatchElement = Mesh.Elements[0];
						BatchElement.IndexBuffer = &Section->IndexBuffer;
						Mesh.bWireframe = bWireframe;
						Mesh.VertexFactory = &Section->VertexFactory;
						Mesh.MaterialRenderProxy = MaterialProxy;

                        if (!PrimitiveUniform)
                        {
                            PrimitiveUniform = &Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
                            FPrimitiveUniformShaderParametersBuilder Builder;
                            BuildUniformShaderParameters(Builder);
                            PrimitiveUniform->Set(Collector.GetRHICommandList(), Builder);
                        }
						BatchElement.PrimitiveUniformBufferResource = &PrimitiveUniform->UniformBuffer;

						BatchElement.FirstIndex = 0;
						BatchElement.NumPrimitives = Section->IndexBuffer.Indices.Num() / 3;
						BatchElement.MinVertexIndex = 0;
						BatchElement.MaxVertexIndex = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
						Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
						Mesh.Type = PT_TriangleList;
						Mesh.DepthPriorityGroup = SDPG_World;
						Mesh.bCanApplyViewModeOverrides = false;
						Collector.AddMesh(ViewIndex, Mesh);
					}
				}
			}
		}

		// Draw bounds
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{	 
				// Draw simple collision as wireframe if 'show collision', and collision is enabled, and we are not using the complex as the simple
				if (ViewFamily.EngineShowFlags.Collision && IsCollisionEnabled() && BodySetup->GetCollisionTraceFlag() != ECollisionTraceFlag::CTF_UseComplexAsSimple)
				{
					FTransform GeomTransform(GetLocalToWorld());
					BodySetup->AggGeom.GetAggGeom(GeomTransform, GetSelectionColor(FColor(157, 149, 223, 255), IsSelected(), IsHovered()).ToFColor(true), NULL, false, false, AlwaysHasVelocity(), ViewIndex, Collector);
				}

				// Render bounds
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
			}
		}
#endif
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bDynamicRelevance = true;
		Result.bRenderInMainPass = ShouldRenderInMainPass();
		Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		Result.bRenderCustomDepth = ShouldRenderCustomDepth();
		Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = DrawsVelocity() && Result.bOpaque && Result.bRenderInMainPass;
		return Result;
	}

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint(void) const
	{
		return(sizeof(*this) + GetAllocatedSize());
	}

	uint32 GetAllocatedSize(void) const
	{
		return(FPrimitiveSceneProxy::GetAllocatedSize());
	}


#if RHI_RAYTRACING
	virtual bool IsRayTracingRelevant() const override { return true; }

	virtual bool HasRayTracingRepresentation() const override { return true; }

	virtual void GetDynamicRayTracingInstances(FRayTracingInstanceCollector& Collector) override final
	{
		if (!(IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Geometry.ProceduralMeshes"))->GetInt()))
		{
			return;
		}

		TConstArrayView<const FSceneView*> Views = Collector.GetViews();
		const uint32 VisibilityMap = Collector.GetVisibilityMap();

		// RT geometry will be generated based on first active view and then reused for all other views
		// TODO: Expose a way for developers to control whether to reuse RT geometry or create one per-view
		const int32 FirstActiveViewIndex = FMath::CountTrailingZeros(VisibilityMap);
		checkf(Views.IsValidIndex(FirstActiveViewIndex), TEXT("There should be at least one active view when calling GetDynamicRayTracingInstances(...)."));

		const FSceneView* FirstActiveView = Views[FirstActiveViewIndex];

		for (int32 SegmentIndex = 0; SegmentIndex < Sections.Num(); ++SegmentIndex)
		{
			const FKKStreamProxySection* Section = Sections[SegmentIndex];
			if (Section != nullptr && Section->bSectionVisible)
			{
				FMaterialRenderProxy* MaterialProxy = Section->Material->GetRenderProxy();
				
				if (Section->RayTracingGeometry.IsValid())
				{
					check(Section->RayTracingGeometry.GetInitializer().IndexBuffer.IsValid());

					FRayTracingInstance RayTracingInstance;
					RayTracingInstance.Geometry = &Section->RayTracingGeometry;
					RayTracingInstance.InstanceTransforms.Add(GetLocalToWorld());

					uint32 SectionIdx = 0;
					FMeshBatch MeshBatch;

					MeshBatch.VertexFactory = &Section->VertexFactory;
					MeshBatch.SegmentIndex = 0;
					MeshBatch.MaterialRenderProxy = Section->Material->GetRenderProxy();
					MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
					MeshBatch.Type = PT_TriangleList;
					MeshBatch.DepthPriorityGroup = SDPG_World;
					MeshBatch.bCanApplyViewModeOverrides = false;
					MeshBatch.CastRayTracedShadow = IsShadowCast(FirstActiveView);

					FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
					BatchElement.IndexBuffer = &Section->IndexBuffer;

					FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
					FPrimitiveUniformShaderParametersBuilder Builder;
					BuildUniformShaderParameters(Builder);
					DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), Builder);

					BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

					BatchElement.FirstIndex = 0;
					BatchElement.NumPrimitives = Section->IndexBuffer.Indices.Num() / 3;
					BatchElement.MinVertexIndex = 0;
					BatchElement.MaxVertexIndex = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;

					RayTracingInstance.Materials.Add(MeshBatch);
					
					for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
					{
						if ((VisibilityMap & (1 << ViewIndex)) == 0)
						{
							continue;
						}

						Collector.AddRayTracingInstance(ViewIndex, RayTracingInstance);
					}
				}
			}
		}
	}
	
#endif

#if WITH_DEV_AUTOMATION_TESTS
    bool VerifyGpu(FRHICommandListImmediate& Commands,int32 Index,const TArray<FProcMeshVertex>& Expected,const TArray<uint32>& Indices,UMaterialInterface* Material,uint64 Generation) const
    {
        if(!Sections.IsValidIndex(Index)||!Sections[Index])return false;
        const auto& S=*Sections[Index];const auto& B=S.VertexBuffers;
        if((S.GpuDeform?(!S.bGpuPublished||S.GpuDeform->GetStats().Generation!=Generation):(!S.CoatingBindings||S.CoatingGeneration!=Generation))||S.UVUploads||S.ColorUploads
            ||B.PositionVertexBuffer.GetNumVertices()!=uint32(Expected.Num())||S.Material!=Material||S.IndexBuffer.Indices!=Indices)return false;
        TArray<FVector3f> P;TArray<FKKPackedStreamTangents> T;TArray<FColor> Colors;
        FStaticMeshVertexBuffer UV;UV.Init(Expected.Num(),4);
        for(int32 I=0;I<Expected.Num();++I){FDynamicMeshVertex V;ConvertKKStreamVertex(V,Expected[I]);P.Add(V.Position);T.Add(PackKKAnimatedTangents(Expected[I]));Colors.Add(V.Color);
            for(int32 K=0;K<4;++K)UV.SetVertexUV(I,K,V.TextureCoordinate[K]);}
        auto Match=[&](FRHIBuffer* Buffer,const void* Data,uint32 Bytes){const void* Read=Commands.LockBuffer(Buffer,0,Bytes,RLM_ReadOnly);
            const bool Same=FMemory::Memcmp(Read,Data,Bytes)==0;Commands.UnlockBuffer(Buffer);return Same;};
        return Match(B.PositionVertexBuffer.VertexBufferRHI,P.GetData(),P.Num()*12)
            &&Match(B.StaticMeshVertexBuffer.TangentsVertexBuffer.VertexBufferRHI,T.GetData(),T.Num()*8)
            &&Match(B.StaticMeshVertexBuffer.TexCoordVertexBuffer.VertexBufferRHI,UV.GetTexCoordData(),UV.GetTexCoordSize())
            &&Match(B.ColorVertexBuffer.VertexBufferRHI,Colors.GetData(),Colors.Num()*sizeof(FColor))
            &&Match(S.IndexBuffer.IndexBufferRHI,Indices.GetData(),Indices.Num()*sizeof(uint32));
    }
    bool Verify(FRHICommandListImmediate& Commands,int32 Index,const TArray<FProcMeshVertex>& Expected,uint32 UVUploads,uint32 ColorUploads) const
    {
        if (!Sections.IsValidIndex(Index) || !Sections[Index]) return false;
        const auto& Section = *Sections[Index];
        if (Section.UVUploads != UVUploads || Section.ColorUploads != ColorUploads
            || Section.VertexBuffers.PositionVertexBuffer.GetNumVertices() != uint32(Expected.Num())) return false;
        FStaticMeshVertexBuffer Reference; Reference.Init(Expected.Num(),4);
        for (int32 IndexV = 0; IndexV < Expected.Num(); ++IndexV)
        {
            FDynamicMeshVertex Vertex; ConvertKKStreamVertex(Vertex,Expected[IndexV]);
            Reference.SetVertexTangents(IndexV,Vertex.TangentX.ToFVector3f(),Vertex.GetTangentY(),Vertex.TangentZ.ToFVector3f());
            for (int32 UV = 0; UV < 4; ++UV) Reference.SetVertexUV(IndexV,UV,Vertex.TextureCoordinate[UV]);
            const auto& Buffers = Section.VertexBuffers;
            if (Buffers.PositionVertexBuffer.VertexPosition(IndexV) != Vertex.Position
                || Buffers.ColorVertexBuffer.VertexColor(IndexV) != Vertex.Color
                || Buffers.StaticMeshVertexBuffer.VertexTangentX(IndexV) != Reference.VertexTangentX(IndexV)
                || Buffers.StaticMeshVertexBuffer.VertexTangentZ(IndexV) != Reference.VertexTangentZ(IndexV)) return false;
            for (int32 UV = 0; UV < 4; ++UV)
                if (Buffers.StaticMeshVertexBuffer.GetVertexUV(IndexV,UV) != Reference.GetVertexUV(IndexV,UV)) return false;
        }
        // Independent GPU readback proves partial writes preserve bytes across
        // upload chunk boundaries. Synchronization exists only in this test.
        const auto MatchesGPU=[&](FRHIBuffer* Buffer,const void* Reference,uint32 Size)
        {
            const void* GPU=Commands.LockBuffer(Buffer,0,Size,RLM_ReadOnly);
            const bool Matches=FMemory::Memcmp(GPU,Reference,Size)==0;
            Commands.UnlockBuffer(Buffer);
            return Matches;
        };
        const auto& B=Section.VertexBuffers;
        return MatchesGPU(B.PositionVertexBuffer.VertexBufferRHI,B.PositionVertexBuffer.GetVertexData(),B.PositionVertexBuffer.GetNumVertices()*B.PositionVertexBuffer.GetStride())
            && MatchesGPU(B.ColorVertexBuffer.VertexBufferRHI,B.ColorVertexBuffer.GetVertexData(),B.ColorVertexBuffer.GetNumVertices()*B.ColorVertexBuffer.GetStride())
            && MatchesGPU(B.StaticMeshVertexBuffer.TangentsVertexBuffer.VertexBufferRHI,B.StaticMeshVertexBuffer.GetTangentData(),B.StaticMeshVertexBuffer.GetTangentSize())
            && MatchesGPU(B.StaticMeshVertexBuffer.TexCoordVertexBuffer.VertexBufferRHI,B.StaticMeshVertexBuffer.GetTexCoordData(),B.StaticMeshVertexBuffer.GetTexCoordSize());
    }
#endif

private:
	/** Array of sections */
	TArray<FKKStreamProxySection*> Sections;

	UBodySetup* BodySetup;

	FMaterialRelevance MaterialRelevance;
};


// All callers must route through these wrappers while the owned proxy is active:
// stock PMC updates/visibility use nonvirtual private-proxy casts.
bool UKKAffineProceduralMeshComponent::UsesAnimatedStreams() const
{
    if (!bUseAnimatedStreams) return false;
    auto* MutableThis = const_cast<UKKAffineProceduralMeshComponent*>(this);
    for (int32 Index = 0; Index < GetNumSections(); ++Index)
        if (MutableThis->GetProcMeshSection(Index)->bEnableCollision) return false;
    return true;
}

FPrimitiveSceneProxy* UKKAffineProceduralMeshComponent::CreateSceneProxy()
{
    check(IsInGameThread());GpuStreamProxyEpoch=++GKKGpuStreamProxyEpoch;
    return UsesAnimatedStreams() ? new FKKStreamSceneProxy(this) : Super::CreateSceneProxy();
}

static void FlushCurrentGpuStreamBatch();
static bool ExpandGpuStreamBounds(FBox& Bounds,const FBox& Addition)
{
    const FBox Before=Bounds;Bounds+=Addition;
    return Before.IsValid!=Bounds.IsValid||Before.Min!=Bounds.Min||Before.Max!=Bounds.Max;
}
TSharedRef<FKKGpuStreamReceipt,ESPMode::ThreadSafe> UKKAffineProceduralMeshComponent::SubmitGpuStreams(int32 Index,FKKGpuStreamRequest Request)
{
    FlushCurrentGpuStreamBatch();
    check(IsInGameThread());auto Receipt=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>();
    auto Reject=[&](const TCHAR* Error){Receipt->Complete(false,Request.ExpectedGeneration,GpuStreamProxyEpoch,Error);return Receipt;};
    if(!UsesAnimatedStreams()||!SceneProxy||IsRenderStateDirty()||GetCollisionEnabled()!=ECollisionEnabled::NoCollision)return Reject(TEXT("GPU publication requires a ready collision-free animated proxy"));
    if(!Request.ProxyEpoch||Request.ProxyEpoch!=GpuStreamProxyEpoch)return Reject(TEXT("Stale GPU stream proxy epoch"));
    if(Index<0||Index>=GetNumSections()||!GetProcMeshSection(Index)||GetProcMeshSection(Index)->ProcVertexBuffer.IsEmpty())return Reject(TEXT("GPU section invalid"));
    const auto& Box=Request.ConservativeLocalBounds;
    if(!Box.IsValid||Box.Min.ContainsNaN()||Box.Max.ContainsNaN()||Box.Min.X>Box.Max.X||Box.Min.Y>Box.Max.Y||Box.Min.Z>Box.Max.Z)return Reject(TEXT("GPU publication requires finite conservative bounds"));
    // Expand before enqueueing; a failed/stale publication cannot shrink bounds
    // and cull the last valid GPU/CPU frame. No per-vertex GT walk on updates.
    if(ExpandGpuStreamBounds(GetProcMeshSection(Index)->SectionLocalBox,Box)){UpdateBounds();MarkRenderTransformDirty();}
    auto* Proxy=static_cast<FKKStreamSceneProxy*>(SceneProxy);const uint64 Epoch=GpuStreamProxyEpoch;
    ENQUEUE_RENDER_COMMAND(KK_PublishGpuStreams)([Proxy,Index,Request=MoveTemp(Request),Receipt,Epoch](FRHICommandListImmediate& Commands){
        FString Error;uint64 Generation=Request.ExpectedGeneration;
        const bool Ok=Proxy->UpdateGpuSection_RenderThread(Commands,Index,Request,Generation,Error);
        Receipt->Complete(Ok,Generation,Epoch,MoveTemp(Error));
    });return Receipt;
}

// GPU coating commands flush role publications first, so the source position
// buffer contains this frame rather than the previous frame's pose.
struct FKKGpuCoatingPacket {
 FKKStreamSceneProxy* Target;FKKStreamSceneProxy* Source;int32 Index,SourceIndex;
 FKKGpuCoatingRequest Request;TSharedRef<FKKGpuStreamReceipt,ESPMode::ThreadSafe> Receipt;
 TWeakObjectPtr<UKKAffineProceduralMeshComponent> TargetComponent,SourceComponent;
 uint64 Generation=0;bool Success=false,Retryable=false;FString Error;
};
static void PublishGpuCoatingPackets(TArray<FKKGpuCoatingPacket> Packets){
 check(IsInGameThread());
 for(int32 I=Packets.Num()-1;I>=0;--I){auto& P=Packets[I];auto* Target=P.TargetComponent.Get();auto* Source=P.SourceComponent.Get();
  if(!Target||!Source||Target->IsRenderStateDirty()||Source->IsRenderStateDirty()||Target->GetGpuStreamProxyEpoch()!=P.Request.TargetEpoch||Source->GetGpuStreamProxyEpoch()!=P.Request.SourceEpoch||Target->GetSceneProxy()!=P.Target||Source->GetSceneProxy()!=P.Source){P.Receipt->Complete(false,P.Request.ExpectedGeneration,P.Request.TargetEpoch,TEXT("Coating batch proxy changed before flush"));Packets.RemoveAt(I);}
 }
 if(Packets.IsEmpty())return;
 ENQUEUE_RENDER_COMMAND(KK_PublishGpuCoatingBatch)([Packets=MoveTemp(Packets)](FRHICommandListImmediate& Commands)mutable{
  TRACE_CPUPROFILER_EVENT_SCOPE(KK_GpuCoatingGraph);FRDGBuilder Graph(Commands);
  for(auto& P:Packets){P.Generation=P.Request.ExpectedGeneration;P.Success=P.Target->CoatingSection_RenderThread(Commands,Graph,P.Index,P.Source,P.SourceIndex,P.Request,P.Generation,P.Error,P.Retryable);}
  Graph.Execute();for(auto& P:Packets)P.Receipt->Complete(P.Success,P.Generation,P.Request.TargetEpoch,MoveTemp(P.Error),P.Retryable);
 });
}
struct FKKGpuCoatingBatchImpl {
 FKKGpuCoatingBatchImpl* Previous=nullptr;TArray<FKKGpuCoatingPacket> Packets;TMap<FKKStreamSceneProxy*,TSet<int32>> Targets;
 void Flush(){PublishGpuCoatingPackets(MoveTemp(Packets));Packets.Reset();Targets.Reset();}
 void Append(FKKGpuCoatingPacket Packet){if(const auto* Seen=Targets.Find(Packet.Target);Seen&&Seen->Contains(Packet.Index))Flush();Targets.FindOrAdd(Packet.Target).Add(Packet.Index);Packets.Add(MoveTemp(Packet));}
};
static FKKGpuCoatingBatchImpl* GCurrentGpuCoatingBatch=nullptr;
FKKGpuCoatingBatchScope::FKKGpuCoatingBatchScope(){check(IsInGameThread());Impl=MakeUnique<FKKGpuCoatingBatchImpl>();Impl->Previous=GCurrentGpuCoatingBatch;if(Impl->Previous)Impl->Previous->Flush();GCurrentGpuCoatingBatch=Impl.Get();}
FKKGpuCoatingBatchScope::~FKKGpuCoatingBatchScope(){check(IsInGameThread()&&GCurrentGpuCoatingBatch==Impl.Get());Impl->Flush();GCurrentGpuCoatingBatch=Impl->Previous;}
TSharedRef<FKKGpuStreamReceipt,ESPMode::ThreadSafe> UKKAffineProceduralMeshComponent::SubmitGpuCoating(int32 Index,UKKAffineProceduralMeshComponent* Source,int32 SourceIndex,FKKGpuCoatingRequest Request)
{
    check(IsInGameThread());FlushCurrentGpuStreamBatch();auto Receipt=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>();
    auto Reject=[&](const TCHAR* Error){Receipt->Complete(false,Request.ExpectedGeneration,GpuStreamProxyEpoch,Error);return Receipt;};
    if(!Source||Source==this||!UsesAnimatedStreams()||!Source->UsesAnimatedStreams()||!SceneProxy||!Source->SceneProxy||IsRenderStateDirty()||Source->IsRenderStateDirty()||GetCollisionEnabled()!=ECollisionEnabled::NoCollision||Source->GetCollisionEnabled()!=ECollisionEnabled::NoCollision)return Reject(TEXT("Coating requires distinct ready collision-free animated proxies"));
    if(!Request.SourceEpoch||Request.SourceEpoch!=Source->GpuStreamProxyEpoch||!Request.TargetEpoch||Request.TargetEpoch!=GpuStreamProxyEpoch)return Reject(TEXT("Stale coating proxy epoch"));
    auto* TargetSection=GetProcMeshSection(Index);const auto* OriginSection=Source->GetProcMeshSection(SourceIndex);
    if(!TargetSection||!OriginSection||TargetSection->ProcVertexBuffer.IsEmpty()||OriginSection->ProcVertexBuffer.IsEmpty())return Reject(TEXT("Coating section missing"));
    FString Error;
    if(!Request.Bindings.IsEmpty()&&(Request.Bindings.Num()!=TargetSection->ProcVertexBuffer.Num()||!OKValidateGpuCoatingBindings(Request.Bindings,OriginSection->ProcVertexBuffer.Num(),Request.SourceToOverlay,Error)))return Reject(TEXT("Coating binding invalid"));
    TArray<FVector4f> Rows;if(!Request.Revision||!OKPrepareGpuDeformPalette(MakeArrayView(&Request.SourceToOverlay,1),Rows,Error))return Reject(TEXT("Coating revision/transform invalid"));
    const auto& Box=Request.ConservativeLocalBounds;
    if(!Box.IsValid||Box.Min.ContainsNaN()||Box.Max.ContainsNaN()||Box.Min.X>Box.Max.X||Box.Min.Y>Box.Max.Y||Box.Min.Z>Box.Max.Z)return Reject(TEXT("Coating bounds invalid"));
    if(ExpandGpuStreamBounds(TargetSection->SectionLocalBox,Box)){UpdateBounds();MarkRenderTransformDirty();}
    FKKGpuCoatingPacket Packet{static_cast<FKKStreamSceneProxy*>(SceneProxy),static_cast<FKKStreamSceneProxy*>(Source->SceneProxy),Index,SourceIndex,MoveTemp(Request),Receipt,this,Source};
    if(GCurrentGpuCoatingBatch)GCurrentGpuCoatingBatch->Append(MoveTemp(Packet));else{TArray<FKKGpuCoatingPacket> Packets;Packets.Add(MoveTemp(Packet));PublishGpuCoatingPackets(MoveTemp(Packets));}return Receipt;
}

struct FKKGpuStreamPacket {
 FKKStreamSceneProxy* Proxy;int32 Section;FKKGpuStreamRequest Request;
 TSharedPtr<FKKGpuStreamReceipt,ESPMode::ThreadSafe> Receipt;
 uint64 Generation;bool Success=false;FString Error;
};
static void PublishGpuStreamPackets(TArray<FKKGpuStreamPacket> Packets){
 if(Packets.IsEmpty())return;
 ENQUEUE_RENDER_COMMAND(KK_PublishGpuStreamBatch)([Packets=MoveTemp(Packets)](FRHICommandListImmediate& Commands)mutable{
  TRACE_CPUPROFILER_EVENT_SCOPE(KK_PrivateGpuGraph);
  FRDGBuilder Graph(Commands);
  for(auto& P:Packets)P.Success=P.Proxy->AppendGpuSection_RenderThread(Commands,Graph,P.Section,P.Request,P.Generation,P.Error);
  Graph.Execute();
  for(auto& P:Packets)P.Receipt->Complete(P.Success,P.Generation,P.Request.ProxyEpoch,MoveTemp(P.Error));
 });
}
struct FKKGpuStreamBatchImpl {
 FKKGpuStreamBatchImpl* Previous=nullptr;
 TArray<FKKGpuStreamPacket> Packets;
 TMap<FKKStreamSceneProxy*,TSet<int32>> Targets;
 void Flush(){PublishGpuStreamPackets(MoveTemp(Packets));Packets.Reset();Targets.Reset();}
 void Append(TArray<FKKGpuStreamPacket> Incoming){
  for(const auto& P:Incoming)if(const auto* Seen=Targets.Find(P.Proxy);Seen&&Seen->Contains(P.Section)){Flush();break;}
  for(auto& P:Incoming){Targets.FindOrAdd(P.Proxy).Add(P.Section);Packets.Add(MoveTemp(P));}
 }
};
static FKKGpuStreamBatchImpl* GCurrentGpuStreamBatch=nullptr; // game thread only
static void FlushCurrentGpuStreamBatch(){check(IsInGameThread());if(GCurrentGpuStreamBatch)GCurrentGpuStreamBatch->Flush();}
FKKGpuStreamBatchScope::FKKGpuStreamBatchScope(){
 check(IsInGameThread());Impl=MakeUnique<FKKGpuStreamBatchImpl>();
 Impl->Previous=GCurrentGpuStreamBatch;if(Impl->Previous)Impl->Previous->Flush();GCurrentGpuStreamBatch=Impl.Get();
}
FKKGpuStreamBatchScope::~FKKGpuStreamBatchScope(){
 check(IsInGameThread()&&GCurrentGpuStreamBatch==Impl.Get());Impl->Flush();GCurrentGpuStreamBatch=Impl->Previous;
}
void UKKAffineProceduralMeshComponent::SubmitGpuStreamsBatch(TArray<FKKGpuStreamSectionRequest> Requests)
{
    check(IsInGameThread());
    TArray<FKKGpuStreamPacket> Packets;TMap<UKKAffineProceduralMeshComponent*,TSet<int32>> Targets;
    TSet<UKKAffineProceduralMeshComponent*> DirtyBounds;
    for(auto& Entry:Requests){
        auto* C=Entry.Component;auto& R=Entry.Request;if(!Entry.Receipt)Entry.Receipt=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>();
        const uint64 Epoch=C?C->GpuStreamProxyEpoch:0;
        auto Reject=[&](const TCHAR* Error){Entry.Receipt->Complete(false,R.ExpectedGeneration,Epoch,Error);};
        if(!C||!C->UsesAnimatedStreams()||!C->SceneProxy||C->IsRenderStateDirty()||C->GetCollisionEnabled()!=ECollisionEnabled::NoCollision){Reject(TEXT("GPU batch requires ready collision-free animated proxy"));continue;}
        if(!R.ProxyEpoch||R.ProxyEpoch!=Epoch){Reject(TEXT("Stale GPU batch proxy epoch"));continue;}
        if(Entry.Section<0||Entry.Section>=C->GetNumSections()){Reject(TEXT("GPU batch section invalid"));continue;}
        auto* Section=C->GetProcMeshSection(Entry.Section);
        if(!Section||Section->ProcVertexBuffer.IsEmpty()){Reject(TEXT("GPU batch section invalid"));continue;}
        auto& Seen=Targets.FindOrAdd(C);if(Seen.Contains(Entry.Section)){Reject(TEXT("Duplicate GPU batch section"));continue;}Seen.Add(Entry.Section);
        const auto& Box=R.ConservativeLocalBounds;
        if(!Box.IsValid||Box.Min.ContainsNaN()||Box.Max.ContainsNaN()||Box.Min.X>Box.Max.X||Box.Min.Y>Box.Max.Y||Box.Min.Z>Box.Max.Z){Reject(TEXT("GPU batch bounds invalid"));continue;}
        if(ExpandGpuStreamBounds(Section->SectionLocalBox,Box))DirtyBounds.Add(C);
        const uint64 Generation=R.ExpectedGeneration;
        Packets.Add({static_cast<FKKStreamSceneProxy*>(C->SceneProxy),Entry.Section,MoveTemp(R),Entry.Receipt,Generation});
    }
    for(auto* C:DirtyBounds){C->UpdateBounds();C->MarkRenderTransformDirty();}
    if(GCurrentGpuStreamBatch)GCurrentGpuStreamBatch->Append(MoveTemp(Packets));
    else PublishGpuStreamPackets(MoveTemp(Packets));
}

void UKKAffineProceduralMeshComponent::UpdateStreams(UProceduralMeshComponent* Component, int32 Index,
    const TArray<FVector>& Positions, const TArray<FVector>& Normals,
    const TArray<FVector2D>& UV0, const TArray<FVector2D>& UV1,
    const TArray<FVector2D>& UV2, const TArray<FVector2D>& UV3,
    const TArray<FColor>& Colors, const TArray<FProcMeshTangent>& Tangents)
{
    auto* Owned = Cast<UKKAffineProceduralMeshComponent>(Component);
    if (!Owned || !Owned->UsesAnimatedStreams())
    {
        Component->UpdateMeshSection(Index,Positions,Normals,UV0,UV1,UV2,UV3,Colors,Tangents);
        return;
    }
    if (Index < 0 || Index >= Component->GetNumSections()) return;
    FProcMeshSection& Section = *Component->GetProcMeshSection(Index);
    const int32 Count = Section.ProcVertexBuffer.Num();
    if (Positions.Num() != Count)
    {
        UE_LOG(LogTemp,Error,TEXT("KK animated stream vertex count mismatch: %d != %d"),Positions.Num(),Count);
        return;
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(KK_StreamUpdate);
    bool UVChanged = false, ColorChanged = false;
    // Fill an owned compact render packet while updating the CPU section.
    // Avoid a second copy of all four LWC UV channels and immutable colors.
    FKKStreamUpdateData* Data = nullptr;
    if (Owned->SceneProxy && !Owned->IsRenderStateDirty())
    {
        Data = new FKKStreamUpdateData;
        Data->TargetSection = Index;
        Data->Positions.SetNumUninitialized(Count);Data->Tangents.SetNumUninitialized(Count);
    }
    const TArray<FVector2D>* UVs[] = {&UV0,&UV1,&UV2,&UV3};
    int32 SuppliedUVs[4];
    int32 SuppliedUVCount = 0;
    for (int32 UV = 0; UV < 4; ++UV)
        if (UVs[UV]->Num() == Count) SuppliedUVs[SuppliedUVCount++] = UV;
    const bool HasNormals = Normals.Num() == Count;
    const bool HasTangents = Tangents.Num() == Count;
    const bool HasColors = Colors.Num() == Count;
    // Animated updates normally omit all UV/color channels. Determine the
    // supplied channels once, and build the exact box in the existing pass.
    // Keep the stock double position and accumulation order for CPU consumers.
    constexpr int32 BlockSize=1024;
    const bool Parallel=Count>=4096&&CVarKKStreamParallel.GetValueOnGameThread()!=0;
    const int32 Blocks=Parallel?FMath::DivideAndRoundUp(Count,BlockSize):1;
    struct FBlockResult {FBox Box=FBox(ForceInit);bool UV=false,Color=false;};
    TArray<FBlockResult,TInlineAllocator<32>> Results;Results.SetNum(Blocks);
    // UObject and proxy inspection stays above on the game thread. Workers only
    // write disjoint, already allocated CPU vertices and render-packet elements.
    auto Pack=[&](int32 Block){auto& Result=Results[Block];const int32 Begin=Parallel?Block*BlockSize:0,End=Parallel?FMath::Min(Count,Begin+BlockSize):Count;
    for (int32 VertexIndex = Begin; VertexIndex < End; ++VertexIndex)
    {
        auto& Vertex = Section.ProcVertexBuffer[VertexIndex];
        Vertex.Position = Positions[VertexIndex];
        Result.Box += Vertex.Position;
        if (HasNormals) Vertex.Normal = Normals[VertexIndex];
        if (HasTangents) Vertex.Tangent = Tangents[VertexIndex];
        if (SuppliedUVCount)
        {
            FVector2D* Destination[] = {&Vertex.UV0,&Vertex.UV1,&Vertex.UV2,&Vertex.UV3};
            for (int32 Channel = 0; Channel < SuppliedUVCount; ++Channel)
            {
                const int32 UV = SuppliedUVs[Channel];
                Result.UV |= *Destination[UV] != (*UVs[UV])[VertexIndex];
                *Destination[UV] = (*UVs[UV])[VertexIndex];
            }
        }
        if (HasColors)
        {
            Result.Color |= Vertex.Color != Colors[VertexIndex];
            Vertex.Color = Colors[VertexIndex];
        }
        if (Data)
        {
            Data->Positions[VertexIndex] = FVector3f(Vertex.Position);
            Data->Tangents[VertexIndex] = PackKKAnimatedTangents(Vertex);
        }
    }};
    if(Parallel)ParallelFor(Blocks,Pack);else Pack(0);
    Section.SectionLocalBox=FBox(ForceInit);
    for(const auto& Result:Results){Section.SectionLocalBox+=Result.Box;UVChanged|=Result.UV;ColorChanged|=Result.Color;}
    if (Data)
    {
        Data->bUpdateColors = ColorChanged;
        Data->bUpdateUVs = UVChanged;
        if (UVChanged) Data->UVs.SetNumUninitialized(Count*4);
        if (ColorChanged) Data->Colors.SetNumUninitialized(Count);
        if (UVChanged || ColorChanged) for (int32 I=0;I<Count;++I)
        {
            const auto& Vertex=Section.ProcVertexBuffer[I];
            if (UVChanged)
            {
                Data->UVs[I*4]=FVector2f(Vertex.UV0);Data->UVs[I*4+1]=FVector2f(Vertex.UV1);
                Data->UVs[I*4+2]=FVector2f(Vertex.UV2);Data->UVs[I*4+3]=FVector2f(Vertex.UV3);
            }
            if (ColorChanged) Data->Colors[I]=Vertex.Color;
        }
        auto* Proxy = static_cast<FKKStreamSceneProxy*>(Owned->SceneProxy);
        ENQUEUE_RENDER_COMMAND(KK_UpdateAnimatedStreams)([Proxy,Data](FRHICommandListImmediate& Commands)
        { Proxy->UpdateSection_RenderThread(Commands,Data); });
    }
    Owned->PublishAnimatedStreamBounds();
}

void UKKAffineProceduralMeshComponent::UpdateAnimatedStreamsBatch(TArrayView<const FKKAnimatedStreamRequest> Requests)
{
    check(IsInGameThread());TRACE_CPUPROFILER_EVENT_SCOPE(KK_StreamBatch);
    const TArray<FVector> EmptyVectors;const TArray<FVector2D> EmptyUV;const TArray<FColor> EmptyColors;const TArray<FProcMeshTangent> EmptyTangents;
    auto Serial=[&](){for(const auto& R:Requests)if(R.Component&&R.Positions)UpdateStreams(R.Component,R.Section,*R.Positions,R.Normals?*R.Normals:EmptyVectors,EmptyUV,EmptyColors,R.Tangents?*R.Tangents:EmptyTangents);};
    int64 Total=0;TSet<FProcMeshSection*> Seen;
    for(const auto& R:Requests){if(!R.Component||!R.Positions||R.Section<0||R.Section>=R.Component->GetNumSections())continue;auto* Section=R.Component->GetProcMeshSection(R.Section);if(Section&&Seen.Contains(Section)){Serial();return;}if(Section)Seen.Add(Section);Total+=R.Positions->Num();}
    const int32 Mode=CVarKKStreamBatch.GetValueOnGameThread();if(Mode==0||(Mode!=2&&Total<32768&&(Requests.Num()<4||Total<4096))){Serial();return;}
    struct FWork {UKKAffineProceduralMeshComponent* Owned;FProcMeshSection* CPU;FKKStreamSceneProxy* Proxy;FKKStreamUpdateData* Packet;FKKAnimatedStreamRequest Input;int32 Count;bool Normals,Tangents;};
    struct FJob {int32 Work,Begin,End;FBox Bounds=FBox(ForceInit);};
    TArray<FWork,TInlineAllocator<64>> Work;TArray<FJob,TInlineAllocator<128>> Jobs;
    for(const auto& R:Requests){
        if(!R.Component||!R.Positions||R.Section<0||R.Section>=R.Component->GetNumSections())continue;auto* Owned=Cast<UKKAffineProceduralMeshComponent>(R.Component);
        if(!Owned||!Owned->UsesAnimatedStreams()){UpdateStreams(R.Component,R.Section,*R.Positions,R.Normals?*R.Normals:EmptyVectors,EmptyUV,EmptyColors,R.Tangents?*R.Tangents:EmptyTangents);continue;}
        auto* CPU=Owned->GetProcMeshSection(R.Section);if(!CPU)continue;const int32 Count=CPU->ProcVertexBuffer.Num();
        if(R.Positions->Num()!=Count){UE_LOG(LogTemp,Error,TEXT("KK animated stream vertex count mismatch: %d != %d"),R.Positions->Num(),Count);continue;}
        auto* Proxy=Owned->SceneProxy&&!Owned->IsRenderStateDirty()?static_cast<FKKStreamSceneProxy*>(Owned->SceneProxy):nullptr;
        FKKStreamUpdateData* Packet=nullptr;if(Proxy){Packet=new FKKStreamUpdateData;Packet->TargetSection=R.Section;Packet->Positions.SetNumUninitialized(Count);Packet->Tangents.SetNumUninitialized(Count);}
        const int32 Index=Work.Num();Work.Add({Owned,CPU,Proxy,Packet,R,Count,R.Normals&&R.Normals->Num()==Count,R.Tangents&&R.Tangents->Num()==Count});
        for(int32 Begin=0;Begin<Count;Begin+=1024)Jobs.Add({Index,Begin,FMath::Min(Begin+1024,Count)});
    }
    // Only disjoint preallocated vertices/packets are touched on workers. All
    // UObject, proxy, bounds publication and render commands stay on this thread.
    ParallelFor(Jobs.Num(),[&](int32 Index){auto& Job=Jobs[Index];const auto& W=Work[Job.Work];
        for(int32 I=Job.Begin;I<Job.End;++I){auto& V=W.CPU->ProcVertexBuffer[I];V.Position=(*W.Input.Positions)[I];Job.Bounds+=V.Position;if(W.Normals)V.Normal=(*W.Input.Normals)[I];if(W.Tangents)V.Tangent=(*W.Input.Tangents)[I];
            if(W.Packet){W.Packet->Positions[I]=FVector3f(V.Position);W.Packet->Tangents[I]=PackKKAnimatedTangents(V);}}
    });
    for(auto& W:Work)W.CPU->SectionLocalBox=FBox(ForceInit);
    for(const auto& Job:Jobs)Work[Job.Work].CPU->SectionLocalBox+=Job.Bounds;
    TSet<UKKAffineProceduralMeshComponent*> Updated;
    for(const auto& W:Work){if(W.Packet){auto* Proxy=W.Proxy;auto* Data=W.Packet;ENQUEUE_RENDER_COMMAND(KK_UpdateAnimatedBatch)([Proxy,Data](FRHICommandListImmediate& Commands){Proxy->UpdateSection_RenderThread(Commands,Data);});}Updated.Add(W.Owned);}
    for(auto* Owned:Updated){Owned->PublishAnimatedStreamBounds();}
}

bool UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsBatch(TArrayView<const FKKGeneratedStreamRequest> Requests,
    TFunctionRef<void(int32,int32,FProcMeshVertex&)> Generate)
{
    return UpdateGeneratedStreamsBatchWithSourceJobs(Requests,Generate,0,[](int32){});
}

bool UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsBatchWithSourceJobs(TArrayView<const FKKGeneratedStreamRequest> Requests,
    TFunctionRef<void(int32,int32,FProcMeshVertex&)> Generate,int32 SourceJobCount,TFunctionRef<void(int32)> GenerateSource)
{
    check(IsInGameThread());TRACE_CPUPROFILER_EVENT_SCOPE(KK_StreamGenerateBatch);
    if(SourceJobCount<0)return false;
    struct FWork {UKKAffineProceduralMeshComponent* Owned;FProcMeshSection* CPU;FKKStreamSceneProxy* Proxy;FKKStreamUpdateData* Packet;};
    struct FJob {int32 Work,Begin,End;FBox Bounds=FBox(ForceInit);};
    TArray<FWork,TInlineAllocator<64>> Work;TArray<FJob,TInlineAllocator<128>> Jobs;TSet<FProcMeshSection*> Seen;
    int64 Total=0;
    // Reject unsupported/duplicate requests before touching any section.
    for(const auto& R:Requests){auto* Owned=Cast<UKKAffineProceduralMeshComponent>(R.Component);
        if(!Owned||!Owned->UsesAnimatedStreams()||R.Section<0||R.Section>=Owned->GetNumSections())return false;
        auto* CPU=Owned->GetProcMeshSection(R.Section);if(!CPU||Seen.Contains(CPU))return false;Seen.Add(CPU);
        Work.Add({Owned,CPU,nullptr,nullptr});Total+=CPU->ProcVertexBuffer.Num();}
    for(int32 I=0;I<Work.Num();++I){auto& W=Work[I];const int32 Count=W.CPU->ProcVertexBuffer.Num();
        W.Proxy=W.Owned->SceneProxy&&!W.Owned->IsRenderStateDirty()?static_cast<FKKStreamSceneProxy*>(W.Owned->SceneProxy):nullptr;
        if(W.Proxy){W.Packet=new FKKStreamUpdateData;W.Packet->TargetSection=Requests[I].Section;W.Packet->Positions.SetNumUninitialized(Count);W.Packet->Tangents.SetNumUninitialized(Count);}
        for(int32 Begin=0;Begin<Count;Begin+=1024)Jobs.Add({I,Begin,FMath::Min(Begin+1024,Count)});
    }
    const int32 Mode=CVarKKStreamBatch.GetValueOnGameThread();
    ParallelFor(Jobs.Num()+SourceJobCount,[&](int32 Index){
        if(Index>=Jobs.Num()){GenerateSource(Index-Jobs.Num());return;}
        auto& Job=Jobs[Index];auto& W=Work[Job.Work];
        for(int32 I=Job.Begin;I<Job.End;++I){auto& V=W.CPU->ProcVertexBuffer[I];Generate(Job.Work,I,V);Job.Bounds+=V.Position;
            if(W.Packet){W.Packet->Positions[I]=FVector3f(V.Position);W.Packet->Tangents[I]=PackKKAnimatedTangents(V);}}
    },Mode==0||(Mode!=2&&Total<32768)?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);
    for(auto& W:Work)W.CPU->SectionLocalBox=FBox(ForceInit);
    for(const auto& Job:Jobs)Work[Job.Work].CPU->SectionLocalBox+=Job.Bounds;
    TSet<UKKAffineProceduralMeshComponent*> Updated;
    for(const auto& W:Work){if(W.Packet){auto* Proxy=W.Proxy;auto* Data=W.Packet;ENQUEUE_RENDER_COMMAND(KK_UpdateGeneratedStreams)([Proxy,Data](FRHICommandListImmediate& Commands){Proxy->UpdateSection_RenderThread(Commands,Data);});}Updated.Add(W.Owned);}
    for(auto* Owned:Updated){Owned->PublishAnimatedStreamBounds();}
    return true;
}

static TAutoConsoleVariable<int32> CVarKKSourceRangeWorkers(TEXT("ok.Stream.SourceRangeWorkers"),0,TEXT("Total participants including game thread; 0 uses min(physical cores,8), bounded 1..32."));
static TAutoConsoleVariable<int32> CVarKKSourceRangeUnbalanced(TEXT("ok.Stream.SourceRangeUnbalanced"),1,TEXT("Use dynamic scheduling for source ranges with heterogeneous source/section costs."));

bool UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsBySourceRanges(TArrayView<const FKKGeneratedStreamRequest> Requests,
    int32 JobCount,TFunctionRef<void(int32,FKKGeneratedStreamRange)> GenerateJob,
    TFunctionRef<void(int32,int32,FProcMeshVertex&)> GenerateVertex)
{
    check(IsInGameThread());TRACE_CPUPROFILER_EVENT_SCOPE(KK_StreamSourceRanges);
    if(JobCount<0||(!Requests.IsEmpty()&&JobCount==0))return false;
    struct FWork {UKKAffineProceduralMeshComponent* Owned;FProcMeshSection* CPU;FKKStreamSceneProxy* Proxy;FKKStreamUpdateData* Packet;};
    struct FRangeBounds {int32 Work,Count;FBox Bounds;};
    TArray<FWork,TInlineAllocator<64>> Work;TSet<FProcMeshSection*> Seen;int64 Total=0;
    for(const auto& R:Requests){auto* Owned=Cast<UKKAffineProceduralMeshComponent>(R.Component);
        if(!Owned||!Owned->UsesAnimatedStreams()||R.Section<0||R.Section>=Owned->GetNumSections())return false;
        auto* CPU=Owned->GetProcMeshSection(R.Section);if(!CPU||Seen.Contains(CPU))return false;Seen.Add(CPU);
        Work.Add({Owned,CPU,nullptr,nullptr});Total+=CPU->ProcVertexBuffer.Num();}
    for(int32 I=0;I<Work.Num();++I){auto& W=Work[I];const int32 Count=W.CPU->ProcVertexBuffer.Num();
        W.Proxy=W.Owned->SceneProxy&&!W.Owned->IsRenderStateDirty()?static_cast<FKKStreamSceneProxy*>(W.Owned->SceneProxy):nullptr;
        if(W.Proxy){W.Packet=new FKKStreamUpdateData;W.Packet->TargetSection=Requests[I].Section;W.Packet->Positions.SetNumUninitialized(Count);W.Packet->Tangents.SetNumUninitialized(Count);}}
    TArray<TArray<FRangeBounds,TInlineAllocator<4>>> JobBounds;JobBounds.SetNum(JobCount);
    const int32 Mode=CVarKKStreamBatch.GetValueOnGameThread();
    const EParallelForFlags ParallelFlags=CVarKKSourceRangeUnbalanced.GetValueOnGameThread()?EParallelForFlags::Unbalanced:EParallelForFlags::None;
    const int32 RequestedWorkers=CVarKKSourceRangeWorkers.GetValueOnGameThread();
    const int32 Participants=FMath::Clamp(RequestedWorkers>0?RequestedWorkers:FMath::Min(FPlatformMisc::NumberOfCores(),8),1,32);
    const int32 MinBatch=FMath::Max(1,FMath::DivideAndRoundUp(JobCount,FMath::Max(1,Participants-1)));
    ParallelFor(TEXT("KK_SourceRanges"),JobCount,MinBatch,[&](int32 Job){
        auto Emit=[&](int32 Request,int32 Begin,int32 End){
            check(Work.IsValidIndex(Request));auto& W=Work[Request];check(Begin>=0&&End>=Begin&&End<=W.CPU->ProcVertexBuffer.Num());
            FBox Bounds(ForceInit);
            for(int32 I=Begin;I<End;++I){auto& V=W.CPU->ProcVertexBuffer[I];GenerateVertex(Request,I,V);Bounds+=V.Position;
                if(W.Packet){W.Packet->Positions[I]=FVector3f(V.Position);W.Packet->Tangents[I]=PackKKAnimatedTangents(V);}}
            if(End>Begin)JobBounds[Job].Add({Request,End-Begin,Bounds});
        };
        GenerateJob(Job,Emit);
    },Participants==1||Mode==0||(Mode!=2&&Total<32768)?EParallelForFlags::ForceSingleThread:ParallelFlags);
    TArray<int32,TInlineAllocator<64>> Written;Written.Init(0,Work.Num());
    for(auto& W:Work)W.CPU->SectionLocalBox=FBox(ForceInit);
    for(const auto& Ranges:JobBounds)for(const auto& Range:Ranges){Work[Range.Work].CPU->SectionLocalBox+=Range.Bounds;Written[Range.Work]+=Range.Count;}
    TSet<UKKAffineProceduralMeshComponent*> Updated;
    for(int32 I=0;I<Work.Num();++I){const auto& W=Work[I];checkf(Written[I]==W.CPU->ProcVertexBuffer.Num(),TEXT("Source ranges must cover every draw vertex"));
        if(W.Packet){auto* Proxy=W.Proxy;auto* Data=W.Packet;ENQUEUE_RENDER_COMMAND(KK_UpdateSourceRanges)([Proxy,Data](FRHICommandListImmediate& Commands){Proxy->UpdateSection_RenderThread(Commands,Data);});}Updated.Add(W.Owned);}
    for(auto* Owned:Updated){Owned->PublishAnimatedStreamBounds();}return true;
}

bool UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsExternally(TArrayView<const FKKGeneratedStreamRequest> Requests,
    TFunctionRef<void(FKKGeneratedSectionEmitter)> Schedule)
{
    check(IsInGameThread());TRACE_CPUPROFILER_EVENT_SCOPE(KK_StreamExternalBatch);
    struct FWork {UKKAffineProceduralMeshComponent* Owned;FProcMeshSection* CPU;FKKStreamSceneProxy* Proxy;FKKStreamUpdateData* Packet=nullptr;bool Emitted=false;};
    TArray<FWork,TInlineAllocator<64>> Work;TSet<FProcMeshSection*> Seen;
    for(const auto& R:Requests){auto* Owned=Cast<UKKAffineProceduralMeshComponent>(R.Component);
        if(!Owned||!Owned->UsesAnimatedStreams()||R.Section<0||R.Section>=Owned->GetNumSections())return false;
        auto* CPU=Owned->GetProcMeshSection(R.Section);if(!CPU||Seen.Contains(CPU))return false;Seen.Add(CPU);
        Work.Add({Owned,CPU,nullptr});}
    for(int32 I=0;I<Work.Num();++I){auto& W=Work[I];
        W.Proxy=W.Owned->SceneProxy&&!W.Owned->IsRenderStateDirty()?static_cast<FKKStreamSceneProxy*>(W.Owned->SceneProxy):nullptr;
        if(W.Proxy){W.Packet=new FKKStreamUpdateData;W.Packet->TargetSection=Requests[I].Section;
            W.Packet->Positions.SetNumUninitialized(W.CPU->ProcVertexBuffer.Num());W.Packet->Tangents.SetNumUninitialized(W.CPU->ProcVertexBuffer.Num());}}
    auto Emit=[&](int32 Index,FKKGeneratedSectionWriter Generate){
        check(Work.IsValidIndex(Index));auto& W=Work[Index];check(!W.Emitted);FBox Bounds(ForceInit);
        for(int32 I=0;I<W.CPU->ProcVertexBuffer.Num();++I){auto& V=W.CPU->ProcVertexBuffer[I];Generate(I,V);Bounds+=V.Position;
            if(W.Packet){W.Packet->Positions[I]=FVector3f(V.Position);W.Packet->Tangents[I]=PackKKAnimatedTangents(V);}}
        W.CPU->SectionLocalBox=Bounds;W.Emitted=true;
    };
    Schedule(Emit); // Must join all external workers before returning.
    TSet<UKKAffineProceduralMeshComponent*> Updated;
    for(auto& W:Work){if(!W.Emitted){delete W.Packet;continue;}
        if(W.Packet){auto* Proxy=W.Proxy;auto* Packet=W.Packet;
            ENQUEUE_RENDER_COMMAND(KK_UpdateExternalStreams)([Proxy,Packet](FRHICommandListImmediate& Commands){Proxy->UpdateSection_RenderThread(Commands,Packet);});}
        Updated.Add(W.Owned);}
    for(auto* Owned:Updated){Owned->PublishAnimatedStreamBounds();}return true;
}

void UKKAffineProceduralMeshComponent::SetStreamSectionVisible(UProceduralMeshComponent* Component,int32 Index,bool bVisible)
{
    auto* Owned = Cast<UKKAffineProceduralMeshComponent>(Component);
    if (!Owned || !Owned->UsesAnimatedStreams()) { Component->SetMeshSectionVisible(Index,bVisible); return; }
    if (Index < 0 || Index >= Component->GetNumSections()) return;
    Component->GetProcMeshSection(Index)->bSectionVisible = bVisible;
    if (Owned->SceneProxy && !Owned->IsRenderStateDirty())
    {
        auto* Proxy = static_cast<FKKStreamSceneProxy*>(Owned->SceneProxy);
        ENQUEUE_RENDER_COMMAND(KK_StreamVisibility)([Proxy,Index,bVisible](FRHICommandListImmediate&)
        { Proxy->SetSectionVisibility_RenderThread(Index,bVisible); });
    }
}

#if WITH_DEV_AUTOMATION_TESTS
bool UKKAffineProceduralMeshComponent::VerifyGpuRenderStreams(int32 Index,const TArray<FProcMeshVertex>& Expected,uint64 Generation)
{
    if(!UsesAnimatedStreams()||!SceneProxy||IsRenderStateDirty()||Index<0||Index>=GetNumSections())return false;
    auto* Proxy=static_cast<FKKStreamSceneProxy*>(SceneProxy);auto Indices=GetProcMeshSection(Index)->ProcIndexBuffer;
    auto* Material=GetMaterial(Index);if(!Material)Material=UMaterial::GetDefaultMaterial(MD_Surface);bool Valid=false;
    ENQUEUE_RENDER_COMMAND(KK_VerifyGpuStreams)([Proxy,Index,Expected,Indices,Material,Generation,&Valid](FRHICommandListImmediate& Commands){Valid=Proxy->VerifyGpu(Commands,Index,Expected,Indices,Material,Generation);});
    FlushRenderingCommands();return Valid;
}
bool UKKAffineProceduralMeshComponent::VerifyAnimatedRenderStreams(int32 Index,uint32 UVUploads,uint32 ColorUploads)
{
    if (!UsesAnimatedStreams() || !SceneProxy || Index < 0 || Index >= GetNumSections()) return false;
    const TArray<FProcMeshVertex> Expected = GetProcMeshSection(Index)->ProcVertexBuffer;
    auto* Proxy = static_cast<FKKStreamSceneProxy*>(SceneProxy);
    bool Valid = false;
    ENQUEUE_RENDER_COMMAND(KK_VerifyAnimatedStreams)([Proxy,Index,Expected,UVUploads,ColorUploads,&Valid](FRHICommandListImmediate& Commands)
    { Valid = Proxy->Verify(Commands,Index,Expected,UVUploads,ColorUploads); });
    FlushRenderingCommands();
    return Valid;
}
#endif
