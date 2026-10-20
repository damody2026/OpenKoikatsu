#include "OKInteractionMode.h"
#include "OKCharacterController.h"
#include "OpenKoikatsuPlayerController.h"
#include "OKPaintSprayer.h"
#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
#include "Components/ComboBoxString.h"
#include "ProceduralMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if !UE_BUILD_SHIPPING
bool AOKInteractionMode::TickFeatureAudit(){
    if(!FParse::Param(FCommandLine::Get(),TEXT("InteractionFeatureAudit")))return true;
    static TWeakObjectPtr<UWorld> CompletedWorld;if(CompletedWorld.Get()==GetWorld())return true;
    auto* C=Controller.Get();auto* PC=Cast<AOpenKoikatsuPlayerController>(C->GetOwningPlayer());
    auto Reject=[&](const FString& Why){if(PC){PC->bScriptedPaintFire=false;PC->ScriptedPaintAim.Reset();}Fail(TEXT("Interaction feature audit: ")+Why);return false;};
    const double Now=FPlatformTime::Seconds();
    if(FeatureAuditAt>0&&Now-FeatureAuditAt>150)return Reject(TEXT("selected motion or watergun timeout"));
    if(FeatureAuditStage==0){
        if(!PC||!PC->IsPaintPreviewActive()||!PC->PaintSprayer)return Reject(TEXT("entry controller has no active watergun"));
        auto* Actor=C->PreviewActor.Get();if(!Actor)return Reject(TEXT("high model unavailable"));
        auto VisibleClothes=[&](){int32 Count=0;TInlineComponentArray<UMeshComponent*> Components;Actor->GetComponents(Components);
            for(const auto& Part:Actor->Parts)if(Part.Role.StartsWith(TEXT("clothes"))){const FName Tag(*FString::Printf(TEXT("KKSessionPart_%d"),Part.SessionIndex));
                for(auto* Component:Components)if((Component==Part.Comp||Component->ComponentHasTag(Tag))&&Component->IsVisible()&&!Component->bHiddenInGame)++Count;}
            return Count;};
        const int32 Worn=VisibleClothes(),Generation=C->LoadGeneration;const float Clock=Actor->AnimPhase;
        if(Worn==0)return Reject(TEXT("clothing fixture unavailable"));
        const auto Body=Actor->Session.ShapeBody();const auto Face=Actor->Session.ShapeFace();double MaxMs=0;
        for(int32 Switch=0;Switch<32;++Switch){const bool Off=Switch%2==0;C->OnClothesState(Off?102:100);MaxMs=FMath::Max(MaxMs,C->LastClothingSwitchMs);
            if(C->PreviewActor!=Actor||C->LoadGeneration!=Generation||C->bLoadInFlight||Actor->bClothingHidden!=Off||VisibleClothes()!=(Off?0:Worn))return Reject(TEXT("clothing visibility rebuilt or failed to restore high model"));
            if(Actor->AnimPhase!=Clock||Actor->Session.ShapeBody()!=Body||Actor->Session.ShapeFace()!=Face)return Reject(TEXT("clothing switch changed animation or shape"));
            if(Off){if(Actor->ClothingMaskMaterials.IsEmpty())return Reject(TEXT("body mask override missing"));
                for(UMaterialInstanceDynamic* Material:Actor->ClothingMaskMaterials){UTexture* Mask=nullptr;Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("_AlphaMask")),Mask);auto* White=Cast<UTexture2D>(Mask);
                    if(!White||White->GetSizeX()!=1||White->GetSizeY()!=1)return Reject(TEXT("neutral body mask dimensions wrong"));
                    auto& Data=White->GetPlatformData()->Mips[0].BulkData;const auto* Pixel=static_cast<const FColor*>(Data.LockReadOnly());const bool Neutral=Pixel&&*Pixel==FColor::White;Data.Unlock();
                    if(!Neutral)return Reject(TEXT("body mask is not neutral white"));}}
        }
        Evidence->SetBoolField(TEXT("clothing_visibility_preserves_high_model"),true);Evidence->SetNumberField(TEXT("clothing_visibility_switches"),32);Evidence->SetNumberField(TEXT("clothing_visibility_max_callback_ms"),MaxMs);
        if(C->NativePairs.Num()<2||C->PairCombo->GetOptionCount()!=C->NativePairs.Num()+1||!C->PairCategoryCombo)return Reject(TEXT("full metadata catalogue is not visible before preparation"));
        TSet<int32> Covered;
        for(int32 Category=1;Category<C->PairCategoryCombo->GetOptionCount();++Category){C->PairCategoryCombo->SetSelectedIndex(Category);for(int32 I:C->PairVisibleIndices)Covered.Add(I);}
        if(Covered.Num()!=C->NativePairs.Num())return Reject(TEXT("categories do not cover every source pose"));
        C->PairCategoryCombo->SetSelectedIndex(0);
        FeatureAuditPair=1;if(C->ReadyPairs[FeatureAuditPair].IsValid())return Reject(TEXT("demand fixture already prepared"));
        Evidence->SetNumberField(TEXT("catalogue_total"),C->NativePairs.Num());Evidence->SetNumberField(TEXT("catalogue_categories"),C->PairCategoryCombo->GetOptionCount()-1);
        C->PairCombo->SetSelectedIndex(FeatureAuditPair+1);
        if(!C->bPairPreparing||!C->PairedPlayback)return Reject(TEXT("unprepared selection blocked or stopped current playback"));
        Evidence->SetBoolField(TEXT("unprepared_selection_accepted"),true);FeatureAuditAt=Now;FeatureAuditStage=1;return false;
    }
    if(FeatureAuditStage==1){
        if(C->bPairPreparing||!C->ReadyPairs[FeatureAuditPair].IsValid())return false;
        const auto Row=C->NativePairs[FeatureAuditPair];const FString Expected=FString::Printf(TEXT("%s:%d"),*Row->GetStringField(TEXT("category")),Row->GetIntegerField(TEXT("id")));
        if(C->ActiveBodyPairKey!=Expected)return false;
        Evidence->SetBoolField(TEXT("demand_motion_played"),true);C->PairedPlayback->SetPaused(true);
        PC->PaintAimHeld=true;PC->UpdatePaintGesture(false,true,true,false);
        if(!PC->UpdatePaintGesture(true,true,true,false))return Reject(TEXT("A plus left gesture rejected"));
        PC->PaintAimHeld=false;PC->UpdatePaintGesture(false,true,true,false);PC->PaintSprayer->ClearPaint();
        FeatureAuditAt=Now;FeatureAuditStage=2;return false;
    }
    if(FeatureAuditStage==2){
        auto* PaintRole=C->PreviewActor.Get();PaintRole->EnsurePrivateGpuQueryGeometry();FVector Target;double Highest=-DBL_MAX;
        for(const auto& Part:PaintRole->Parts)if(Part.PrivateGpuDisplay.IsValid()&&Part.Comp&&Part.Comp->IsVisible()){
            auto* Component=Part.Comp.Get();const auto* Section=Component->GetProcMeshSection(0);if(!Section||!Section->bSectionVisible)continue;
            for(int32 I=0;I+2<Section->ProcIndexBuffer.Num();I+=3){FVector V[3];for(int32 E=0;E<3;++E)V[E]=Component->GetRenderMatrix().TransformPosition(Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+E]].Position);
                const FVector P=(V[0]+V[1]+V[2])/3.;if(P.ContainsNaN()||FVector::CrossProduct(V[1]-V[0],V[2]-V[0]).Length()<.04)continue;
                if(P.Z>Highest){Highest=P.Z;Target=P;}
            }
        }
        if(Highest==-DBL_MAX)return Reject(TEXT("high model paint target unavailable"));
        PC->ScriptedPaintAim=Target;PC->bScriptedPaintFire=true;
        if(Now-FeatureAuditAt<2.)return false;
        PC->bScriptedPaintFire=false;PC->PaintSprayer->StopSpray();FeatureAuditStage=3;FeatureAuditAt=Now;return false;
    }
    if(FeatureAuditStage==3){
        if(Now-FeatureAuditAt<1.)return false;
        auto* Spray=PC->PaintSprayer.Get();if(Spray->HitCount<=0||Spray->Deposited<=0)return Reject(TEXT("no watergun hits or deposited film"));
        Evidence->SetNumberField(TEXT("watergun_hits"),Spray->HitCount);Evidence->SetNumberField(TEXT("watergun_deposited"),Spray->Deposited);
        Spray->ClearPaint();PC->ScriptedPaintAim.Reset();PC->bScriptedPaintFire=false;
        if(Spray->RemainingVolume()!=0||Spray->IsSpraying())return Reject(TEXT("watergun clear failed"));
        Evidence->SetBoolField(TEXT("watergun_clear"),true);C->PairedPlayback->SetPaused(false);CompletedWorld=GetWorld();FeatureAuditStage=4;
        UE_LOG(LogTemp,Display,TEXT("Interaction feature audit passed catalogue=%d categories=%d watergun_hits=%d"),C->NativePairs.Num(),C->PairCategoryCombo->GetOptionCount()-1,Spray->HitCount);
    }
    return FeatureAuditStage==4;
}
#endif
