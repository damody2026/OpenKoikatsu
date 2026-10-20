// The OK Studio editor's Pose tab: a character's FK bones and IK targets, editing the same document fields as the
// in-game panel (Character.Fk, Character.Ik).
#include "OKStudioEditor.h"
#include "KKPreviewActor.h"
#include "KKRustExtract.h"
#include "OKSceneActor.h"
#include "OKStudioCatalog.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OKStudioEditorPose"

namespace
{
	const TCHAR* IkNames[13] = { TEXT("身體"), TEXT("左肩"), TEXT("右肩"), TEXT("左大腿"), TEXT("右大腿"), TEXT("左手"), TEXT("右手"), TEXT("左腳"), TEXT("右腳"),
		TEXT("左手肘（彎曲）"), TEXT("右手肘（彎曲）"), TEXT("左膝（彎曲）"), TEXT("右膝（彎曲）") };
}

void FOKStudioEditor::EditCharacter(TFunctionRef<void(FOKSceneCharacter&)> Edit)
{
	FOKSceneObject* O = Doc->Find(SelectedId);
	if (!O || O->Kind != EOKSceneObjectKind::Character) return;
	Edit(O->Character);
	// The details view edits a copy: keep it current so its next edit does not undo this one.
	Selection->Object = *O;
	Doc->NotifyChanged();
}

TSharedRef<SWidget> FOKStudioEditor::MakePoseTab()
{
	return SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(PoseBox, SVerticalBox)];
}

void FOKStudioEditor::RebuildPose()
{
	if (!PoseBox) return;
	PoseBox->ClearChildren();
	const FOKSceneObject* O = Doc->Find(SelectedId);
	auto Line = [this](const FString& Text) { PoseBox->AddSlot().AutoHeight().Padding(2.f, 4.f, 2.f, 1.f)[SNew(STextBlock).Text(FText::FromString(Text))]; };
	if (!O || O->Kind != EOKSceneObjectKind::Character) { Line(TEXT("選一個角色來編輯 FK／IK")); return; }
	// A row: label, then X / Y / Z spin boxes reading and writing through the getters / setters.
	auto Row = [this](const FString& Label, TFunction<FVector()> Get, TFunction<void(const FVector&)> Set, float Range, TSharedPtr<SWidget> Leading = nullptr)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		if (Leading) Box->AddSlot().AutoWidth().VAlign(VAlign_Center)[Leading.ToSharedRef()];
		Box->AddSlot().FillWidth(0.4f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Label))];
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Box->AddSlot().FillWidth(0.2f).Padding(1.f, 0.f)
			[
				SNew(SSpinBox<float>).MinValue(-Range).MaxValue(Range).Delta(0.5f)
				.Value_Lambda([Get, Axis] { return float(Get()[Axis]); })
				.OnValueChanged_Lambda([Get, Set, Axis](float V) { FVector E = Get(); E[Axis] = V; Set(E); })
			];
		}
		PoseBox->AddSlot().AutoHeight().Padding(2.f, 1.f)[Box];
	};
	auto Button = [](const FString& Text, TFunction<void()> Do) { return SNew(SButton).Text(FText::FromString(Text)).OnClicked_Lambda([Do] { Do(); return FReply::Handled(); }); };

	// FK: every Studio FK bone; a bone not set yet shows the pose's rotation and is set when edited.
	const FOKStudioCatalog& C = FOKStudioCatalog::Get();
	Line(FString::Printf(TEXT("FK（%d 根骨骼有設定；未設定的顯示目前姿勢）"), O->Character.Fk.Num()));
	PoseBox->AddSlot().AutoHeight().Padding(2.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.f)[Button(TEXT("全部取目前姿勢"), [this]
		{
			AKKPreviewActor* P = SceneActor() ? SceneActor()->PreviewOf(SelectedId) : nullptr;
			if (!P) return;
			EditCharacter([P](FOKSceneCharacter& Ch)
			{
				for (const FOKStudioFkBone& B : FOKStudioCatalog::Get().FkBones)
				{
					if (B.Op != 0) continue;
					FVector E;
					if (!P->Session.BoneEuler(B.Bone, E)) continue;
					Ch.Fk.RemoveAll([&](const FOKSceneFkBone& X) { return X.Bone == B.Bone; });
					Ch.Fk.Add({ B.Bone, E });
				}
			});
		})]
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.f)[Button(TEXT("清除 FK"), [this] { EditCharacter([](FOKSceneCharacter& Ch) { Ch.Fk.Reset(); }); })]
	];
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		Line(Pass == 0 ? TEXT("主要骨架") : TEXT("其他部位（頭髮、裙子、手指…）"));
		for (const FOKStudioFkBone& B : C.FkBones)
		{
			if ((B.Op == 0) != (Pass == 0)) continue;
			const FString Bone = B.Bone;
			auto Get = [this, Bone]() -> FVector
			{
				const FOKSceneObject* X = Doc->Find(SelectedId);
				if (!X) return FVector::ZeroVector;
				if (const FOKSceneFkBone* F = X->Character.Fk.FindByPredicate([&](const FOKSceneFkBone& Y) { return Y.Bone == Bone; })) return F->Euler;
				FVector E(0);
				if (AKKPreviewActor* P = SceneActor() ? SceneActor()->PreviewOf(SelectedId) : nullptr) P->Session.BoneEuler(Bone, E);
				return E;
			};
			Row(B.Name + TEXT(" ") + Bone, Get, [this, Bone](const FVector& E)
			{
				EditCharacter([&](FOKSceneCharacter& Ch)
				{
					if (FOKSceneFkBone* F = Ch.Fk.FindByPredicate([&](const FOKSceneFkBone& Y) { return Y.Bone == Bone; })) F->Euler = E;
					else Ch.Fk.Add({ Bone, E });
				});
			}, 360.f);
		}
	}

	// IK: the 13 Studio IK targets (character space, cm); enabling one starts it where the limb is.
	Line(FString::Printf(TEXT("IK（%d 個目標）"), O->Character.Ik.Num()));
	for (int32 Effector = 0; Effector < 13; ++Effector)
	{
		auto Get = [this, Effector]() -> FVector
		{
			const FOKSceneObject* X = Doc->Find(SelectedId);
			const FOKSceneIkTarget* T = X ? X->Character.Ik.FindByPredicate([&](const FOKSceneIkTarget& Y) { return Y.Effector == Effector; }) : nullptr;
			return T ? T->Position : FVector::ZeroVector;
		};
		TSharedRef<SCheckBox> On = SNew(SCheckBox)
			.IsChecked_Lambda([this, Effector]
			{
				const FOKSceneObject* X = Doc->Find(SelectedId);
				return X && X->Character.Ik.ContainsByPredicate([&](const FOKSceneIkTarget& Y) { return Y.Effector == Effector; }) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, Effector](ECheckBoxState S)
			{
				FVector Start = FVector::ZeroVector;
				if (S == ECheckBoxState::Checked)
				{
					AKKPreviewActor* P = SceneActor() ? SceneActor()->PreviewOf(SelectedId) : nullptr;
					FString Error;
					FVector Unity;
					if (!P || !P->Session.PrepareStudioIk(Error) || !P->Session.IkEffectorPosition(Effector, Unity)) { SetStatus(TEXT("IK 無法啟用：") + Error); return; }
					P->bStudioIkReady = true;
					Start = AKKPreviewActor::UnityToActorLocal(Unity);
				}
				EditCharacter([&](FOKSceneCharacter& Ch)
				{
					Ch.Ik.RemoveAll([&](const FOKSceneIkTarget& Y) { return Y.Effector == Effector; });
					if (S == ECheckBoxState::Checked) Ch.Ik.Add({ Effector, Start });
				});
			});
		Row(IkNames[Effector], Get, [this, Effector](const FVector& P)
		{
			EditCharacter([&](FOKSceneCharacter& Ch) { if (FOKSceneIkTarget* T = Ch.Ik.FindByPredicate([&](const FOKSceneIkTarget& Y) { return Y.Effector == Effector; })) T->Position = P; });
		}, 400.f, On);
	}
}

#undef LOCTEXT_NAMESPACE
