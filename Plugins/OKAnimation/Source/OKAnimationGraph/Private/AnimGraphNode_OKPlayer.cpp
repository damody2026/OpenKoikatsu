#include "AnimGraphNode_OKPlayer.h"
#include "AnimGraphNode_OKContactIK.h"
#include "OKAnimationAssets.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "OKAnimationGraph"

FText UAnimGraphNode_OKPlayer::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (Node.Animation && TitleType != ENodeTitleType::MenuTitle)
		return FText::Format(LOCTEXT("TitleAsset", "OK Player\n{0}"), FText::FromString(Node.Animation->GetName()));
	return LOCTEXT("Title", "OK Player");
}

FText UAnimGraphNode_OKPlayer::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Plays an OK animation, blend space, montage or group role on this mesh's own skeleton (retargeted through a profile or a built-in preset).");
}

FString UAnimGraphNode_OKPlayer::GetNodeCategory() const { return TEXT("OK Animation"); }
FLinearColor UAnimGraphNode_OKPlayer::GetNodeTitleColor() const { return FLinearColor(0.85f, 0.35f, 0.55f); }

FText UAnimGraphNode_OKContactIK::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (Node.Animation && TitleType != ENodeTitleType::MenuTitle)
		return FText::Format(LOCTEXT("ContactTitleAsset", "OK Contact IK\n{0}"), FText::FromString(Node.Animation->GetName()));
	return LOCTEXT("ContactTitle", "OK Contact IK");
}

FText UAnimGraphNode_OKContactIK::GetTooltipText() const
{
	return LOCTEXT("ContactTooltip", "Solves the contacts of an OK animation on this mesh with two-bone IK: feet on the ground, hands on the body. Put it after an OK Player playing the same asset.");
}

FText UAnimGraphNode_OKContactIK::GetControllerDescription() const { return LOCTEXT("ContactDescription", "OK Contact IK"); }
FString UAnimGraphNode_OKContactIK::GetNodeCategory() const { return TEXT("OK Animation"); }

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDefaultModuleImpl, OKAnimationGraph)
