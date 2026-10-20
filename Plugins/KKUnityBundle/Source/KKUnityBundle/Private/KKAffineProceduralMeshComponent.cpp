#include "KKAffineProceduralMeshComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void UKKAffineProceduralMeshComponent::ExpandAnimatedRenderBounds(const FBox& Box)
{
    check(IsInGameThread());
    if(!bUseConservativeAnimatedBounds||!UsesAnimatedStreams()||GetCollisionEnabled()!=ECollisionEnabled::NoCollision||!Box.IsValid||Box.Min.ContainsNaN()||Box.Max.ContainsNaN()||Box.Min.X>Box.Max.X||Box.Min.Y>Box.Max.Y||Box.Min.Z>Box.Max.Z)return;
    const FBox Before=AnimatedBoundsEnvelope;AnimatedBoundsEnvelope+=Box;
    if(Before.IsValid!=AnimatedBoundsEnvelope.IsValid||Before.Min!=AnimatedBoundsEnvelope.Min||Before.Max!=AnimatedBoundsEnvelope.Max){UpdateBounds();MarkRenderTransformDirty();}
}

void UKKAffineProceduralMeshComponent::PublishAnimatedStreamBounds()
{
    if (bUseConservativeAnimatedBounds && UsesAnimatedStreams() && GetCollisionEnabled()==ECollisionEnabled::NoCollision)
    {
        const FBox Before=AnimatedBoundsEnvelope;
        for(int32 Section=0;Section<GetNumSections();++Section)
            AnimatedBoundsEnvelope+=GetProcMeshSection(Section)->SectionLocalBox;
        if(Before.IsValid==AnimatedBoundsEnvelope.IsValid&&Before.Min==AnimatedBoundsEnvelope.Min&&Before.Max==AnimatedBoundsEnvelope.Max)return;
        TRACE_CPUPROFILER_EVENT_SCOPE(KK_StreamBoundsExpand);
        UpdateBounds();MarkRenderTransformDirty();return;
    }
    UpdateBounds();MarkRenderTransformDirty();
}

void UKKAffineProceduralMeshComponent::SetRenderResidual(const FMatrix& Matrix)
{
    if (RenderResidual.Equals(Matrix, 1.e-7)) return;
    RenderResidual = Matrix;
    UpdateBounds();
    MarkRenderTransformDirty();
}

FMatrix UKKAffineProceduralMeshComponent::GetRenderMatrix() const
{
    return RenderResidual * Super::GetRenderMatrix();
}

FBoxSphereBounds UKKAffineProceduralMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    if (UsesAnimatedStreams())
    {
        FBox Box(ForceInit);
        auto* MutableThis = const_cast<UKKAffineProceduralMeshComponent*>(this);
        for (int32 Section = 0; Section < GetNumSections(); ++Section)
            Box += MutableThis->GetProcMeshSection(Section)->SectionLocalBox;
        if(bUseConservativeAnimatedBounds&&GetCollisionEnabled()==ECollisionEnabled::NoCollision)Box+=AnimatedBoundsEnvelope;
        const FBoxSphereBounds SectionBounds = Box.IsValid ? FBoxSphereBounds(Box) : FBoxSphereBounds(FVector::ZeroVector,FVector::ZeroVector,0);
        return SectionBounds.TransformBy(RenderResidual * LocalToWorld.ToMatrixWithScale());
    }
    return Super::CalcBounds(FTransform::Identity).TransformBy(RenderResidual * LocalToWorld.ToMatrixWithScale());
}
