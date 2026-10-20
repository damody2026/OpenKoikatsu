#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "KKCardImporterService.h"
#include "KKAnimationImporter.h"
#include "KKSharedAnimation.h"
#include "KKCardSource.h"
#include "KKImportedCharacter.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/ScopedSlowTask.h"
#define LOCTEXT_NAMESPACE "KKCardImporter"
class SKKCardImport : public SCompoundWidget {
public:
 SLATE_BEGIN_ARGS(SKKCardImport){} SLATE_END_ARGS()
 void Construct(const FArguments& Args){
  ChildSlot[SNew(SBorder).Padding(16)[SNew(SScrollBox)+SScrollBox::Slot()[SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Help","Import KKS cards or independent PNG/APNG as Unreal assets. KKS cards use built-in base resources and adjacent zipmods. Cards inside a detected game installation also use its mods folder, including subfolders. Independent PNG/APNG uses its shared resource pack."))]
   +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("Card","KKS / independent card file"))]
   +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,12)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(CardBox,SEditableTextBox)]
    +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(LOCTEXT("Browse","Browse...")).OnClicked(this,&SKKCardImport::Browse)]]
   +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("Folder","New destination folder (/Game/...)"))]
   +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,12)[SAssignNew(FolderBox,SEditableTextBox).Text(FText::FromString(TEXT("/Game/IndependentImports/MyCharacter")))]
   +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("Outfit","Outfit index: -1 imports all outfits; 0 imports the first outfit"))]
   +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,12)[SNew(SSpinBox<int32>).MinValue(-1).MaxValue(10000).Value(-1).OnValueChanged_Lambda([this](int32 V){Outfit=V;})]
   +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(SButton).Text(LOCTEXT("Import","Import Character Assets")).OnClicked(this,&SKKCardImport::Run)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,12)[SAssignNew(Status,STextBlock).AutoWrapText(true).Text(LOCTEXT("Initial","Import may take several minutes. Existing assets are never overwritten."))]
  ]]];
 }
private:
 TSharedPtr<SEditableTextBox> CardBox,FolderBox;TSharedPtr<STextBlock> Status;int32 Outfit=-1;
 FReply Browse(){auto* Desktop=FDesktopPlatformModule::Get();if(!Desktop){Status->SetText(LOCTEXT("NoDialog","File dialog unavailable. Enter the full card path."));return FReply::Handled();}TArray<FString> Files;
  if(Desktop->OpenFileDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),TEXT("Select independent character card"),TEXT(""),TEXT(""),TEXT("Character cards (*.png;*.apng)|*.png;*.apng"),0,Files)&&!Files.IsEmpty())CardBox->SetText(FText::FromString(Files[0]));return FReply::Handled();}
 FReply Run(){const FString Card=CardBox->GetText().ToString().TrimStartAndEnd(),Folder=FolderBox->GetText().ToString().TrimStartAndEnd();FString Error;
  if(!KKCardImporter::Validate(Card,Folder,Outfit,Error)){Status->SetText(FText::FromString(Error));return FReply::Handled();}
  FScopedSlowTask Task(1.f,LOCTEXT("Working","Importing independent character assets. Please wait..."));Task.MakeDialog(false);
  auto* Manifest=KKCardImporter::Import(Card,Folder,Outfit,Error);Task.EnterProgressFrame(1.f);
  if(Manifest){FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(TArray<UObject*>{Manifest});Status->SetText(FText::FromString(FString::Printf(TEXT("Imported %d outfit(s) to %s. Character selected in Content Browser."),Manifest->Outfits.Num(),*Folder)));}
  else Status->SetText(FText::FromString(Error+TEXT("\nSee Output Log. If resources were partly saved, choose a fresh destination before retrying.")));
  return FReply::Handled();
 }
};
class SKKAnimationImport : public SCompoundWidget {
public:
 SLATE_BEGIN_ARGS(SKKAnimationImport){} SLATE_END_ARGS()
 void Construct(const FArguments& Args){
  auto Form=SNew(SVerticalBox);
  Form->AddSlot().AutoHeight().Padding(0,0,0,12)[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("AnimHelp","Import shared original curve resources for multiple character actors and independent mouth controls, or bake character-specific Unreal Animation Sequences. Use the original KKS card matching the imported Character. Built-in animation bundles need no game installation; mod bundles use the same adjacent / detected game zipmods as card import. Clips use exact source-qualified labels; separate multiple clips with semicolons. Existing assets are never overwritten."))];
  Field(Form,TEXT("Original KKS card path (used to locate resources; shared import does not reference the card)"),Card,TEXT(""));
  Field(Form,TEXT("Resource root (optional when a card locates built-in resources)"),ResourceRoot,TEXT(""));
  Field(Form,TEXT("Imported Character object path"),Character,TEXT("/Game/IndependentImports/MyCharacter/Character.Character"));
  Field(Form,TEXT("Animation source bundle"),Source,TEXT("studio/anime/00.unity3d"));
  Field(Form,TEXT("Exact clip labels (semicolon separated)"),Clips,TEXT("f_aruki_00_00_01"));
  Field(Form,TEXT("New animation destination"),Destination,TEXT("/Game/IndependentImports/MyAnimations"));
  Form->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("AnimOutfit","Outfit: -1 = every imported outfit"))];
  Form->AddSlot().AutoHeight()[SNew(SSpinBox<int32>).MinValue(-1).MaxValue(10000).Value(-1).OnValueChanged_Lambda([this](int32 V){Outfit=V;})];
  Form->AddSlot().AutoHeight().Padding(0,8)[SNew(SButton).Text(LOCTEXT("SharedAnimRun","Import Shared Original Animation Resources")).OnClicked(this,&SKKAnimationImport::RunShared)];
  Form->AddSlot().AutoHeight().Padding(0,8)[SNew(SButton).Text(LOCTEXT("AnimRun","Import Original Skeletal Animations")).OnClicked(this,&SKKAnimationImport::Run)];
  Form->AddSlot().AutoHeight()[SAssignNew(Status,STextBlock).AutoWrapText(true)];
  ChildSlot[SNew(SBorder).Padding(16)[SNew(SScrollBox)+SScrollBox::Slot()[Form]]];
 }
private:
 TSharedPtr<SEditableTextBox> Card,Character,Source,Clips,Destination,ResourceRoot;TSharedPtr<STextBlock> Status;int32 Outfit=-1;
 void Field(TSharedRef<SVerticalBox> Form,const TCHAR* Label,TSharedPtr<SEditableTextBox>& Box,const TCHAR* Initial){
  Form->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Label))];
  Form->AddSlot().AutoHeight().Padding(0,4,0,12)[SAssignNew(Box,SEditableTextBox).Text(FText::FromString(Initial))];
 }
 FReply RunShared(){TArray<FString> Labels;Clips->GetText().ToString().ParseIntoArray(Labels,TEXT(";"),true);for(auto& Label:Labels)Label.TrimStartAndEndInline();
  FString Root=ResourceRoot->GetText().ToString().TrimStartAndEnd(),Error;
  if(Root.IsEmpty()&&!OKResolveCardSource(Card->GetText().ToString().TrimStartAndEnd(),Root,Error)){Status->SetText(FText::FromString(Error));return FReply::Handled();}
  TArray<UKKSharedAnimation*> Assets;FScopedSlowTask Task(1.f,LOCTEXT("SharedAnimWorking","Importing shared authored animation curves..."));Task.MakeDialog(false);
  if(KKAnimationImporter::ImportShared(Root,Source->GetText().ToString().TrimStartAndEnd(),Labels,Destination->GetText().ToString().TrimStartAndEnd(),Assets,Error)){
   TArray<UObject*> Selected;for(auto* Asset:Assets)Selected.Add(Asset);FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(Selected);
   Status->SetText(FText::FromString(FString::Printf(TEXT("Saved %d shared animation resources. Assign one to Shared Animation on each imported character actor; Set Mouth Control remains independent. Reimport older characters to include their animation rig snapshot."),Assets.Num())));
  }else Status->SetText(FText::FromString(Error));return FReply::Handled();
 }
 FReply Run(){TArray<FString> Labels;Clips->GetText().ToString().ParseIntoArray(Labels,TEXT(";"),true);for(auto& Label:Labels)Label.TrimStartAndEndInline();FString Error;
  FScopedSlowTask Task(1.f,LOCTEXT("AnimWorking","Baking original animation tracks..."));Task.MakeDialog(false);
  auto* Library=KKAnimationImporter::Import(Card->GetText().ToString().TrimStartAndEnd(),Character->GetText().ToString().TrimStartAndEnd(),Source->GetText().ToString().TrimStartAndEnd(),Labels,Destination->GetText().ToString().TrimStartAndEnd(),Outfit,60,Error);
  if(Library){FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(TArray<UObject*>{Library});
   TSet<int32> AnimatedOutfits;for(const auto& Entry:Library->Animations)AnimatedOutfits.Add(Entry.Outfit);int32 StaticParts=0;
   for(const auto& O:Library->Character->Outfits)if(AnimatedOutfits.Contains(O.Index))for(const auto& P:O.Parts)if(!P.SkinnedModel)++StaticParts;
   FString Message=FString::Printf(TEXT("Saved %d Animation Sequences. Library selected in Content Browser."),Library->Animations.Num());
   if(StaticParts)Message+=FString::Printf(TEXT("\nPartial character coverage: %d parts still have only static models and will not deform with these animations."),StaticParts);
   Status->SetText(FText::FromString(Message));}
  else Status->SetText(FText::FromString(Error));return FReply::Handled();
 }
};
class FKKCardImporterEditor : public IModuleInterface {
public:
 virtual void StartupModule() override {if(!IsRunningCommandlet())UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this,&FKKCardImporterEditor::RegisterMenus));}
 virtual void ShutdownModule() override {if(auto Existing=Window.Pin())Existing->RequestDestroyWindow();if(auto Existing=AnimationWindow.Pin())Existing->RequestDestroyWindow();UToolMenus::UnRegisterStartupCallback(this);UToolMenus::UnregisterOwner(this);}
 void OpenAnimations(const FToolMenuContext& Context){if(auto Existing=AnimationWindow.Pin()){Existing->BringToFront();return;}auto NewWindow=SNew(SWindow).Title(LOCTEXT("AnimTitle","Import Original KKS Skeletal Animations")).ClientSize(FVector2D(750,650))[SNew(SKKAnimationImport)];AnimationWindow=NewWindow;FSlateApplication::Get().AddWindow(NewWindow);}
 void RegisterMenus(){FToolMenuOwnerScoped Owner(this);for(const FName MenuName:{FName(TEXT("LevelEditor.MainMenu.Tools")),FName(TEXT("ContentBrowser.AddNewContextMenu"))}){
  auto* Menu=UToolMenus::Get()->ExtendMenu(MenuName);Menu->FindOrAddSection(TEXT("OpenKoikatsu")).AddMenuEntry(TEXT("KKImportSkeletalAnimations"),LOCTEXT("AnimEntry","Import Original KKS Skeletal Animations..."),LOCTEXT("AnimTip","Save original skeletal clips as Unreal Animation Sequences."),FSlateIcon(),FToolUIAction(FToolMenuExecuteAction::CreateRaw(this,&FKKCardImporterEditor::OpenAnimations)));Menu->FindOrAddSection(TEXT("OpenKoikatsu")).AddMenuEntry(TEXT("KKImportIndependentCard"),LOCTEXT("Entry","Import KKS / Independent Character Card..."),LOCTEXT("Tip","Import converted independent PNG/APNG cards as models, skeletons, materials and textures."),FSlateIcon(),FToolUIAction(FToolMenuExecuteAction::CreateRaw(this,&FKKCardImporterEditor::OpenWindow)));}}
 void OpenWindow(const FToolMenuContext& Context){if(auto Existing=Window.Pin()){Existing->BringToFront();return;}auto NewWindow=SNew(SWindow).Title(LOCTEXT("Title","Import KKS / Independent Character Card")).ClientSize(FVector2D(700,530)).SupportsMaximize(false).SupportsMinimize(false)[SNew(SKKCardImport)];Window=NewWindow;FSlateApplication::Get().AddWindow(NewWindow);}
private:TWeakPtr<SWindow> Window,AnimationWindow;
};
IMPLEMENT_MODULE(FKKCardImporterEditor,KKCardImporterEditor)
#undef LOCTEXT_NAMESPACE
