#include "OKShapeSlider.h"
#include "OKGalleryWidget.h"
#include "Components/TextBlock.h"

void UOKShapeSlider::OnResetClicked()
{
	if(Owner&&!BodyParameter.IsEmpty()){Owner->OnBodyPhysicsChanged(BodyParameter,.5f);Owner->SyncBoneSliders();return;}
	if(Owner&&!PregnancyParameter.IsEmpty()){Owner->OnPregnancyChanged(PregnancyParameter,0.f);Owner->SyncBoneSliders();return;}
	if(Owner&&!ModBone.IsEmpty()){Owner->OnBoneChanged(this,ModAxis>=4?0.f:1.f);return;}
	if (Owner && AnimationParameter.IsEmpty()) Owner->ResetShape(bBody, Index);
}

void UOKShapeSlider::RefreshValueLabel()
{
	if (ValueLabel)
	{
		ValueLabel->SetText((AnimationParameter.IsEmpty()&&PregnancyParameter.IsEmpty()&&ModAxis<7)?FText::AsNumber(FMath::RoundToInt(GetValue()*100.f)):FText::AsNumber(GetValue()));
	}
}

void UOKShapeSlider::OnSliderMoved(float NewValue)
{
	RefreshValueLabel();
	if (Owner)
	{
		if(!BodyParameter.IsEmpty()) Owner->OnBodyPhysicsChanged(BodyParameter,NewValue);
		else if(!PregnancyParameter.IsEmpty()) Owner->OnPregnancyChanged(PregnancyParameter,NewValue);
		else if(!ModBone.IsEmpty()) Owner->OnBoneChanged(this,NewValue);
		else if(AnimationParameter.IsEmpty()) Owner->OnShapeChanged(bBody,Index,NewValue);
		else Owner->OnGameAnimationParameter(AnimationParameter,NewValue);
	}
}

void UOKShapeSlider::OnModSideChanged(FString SelectedSide,ESelectInfo::Type Type){if(Owner)Owner->SyncBoneSliders();}
