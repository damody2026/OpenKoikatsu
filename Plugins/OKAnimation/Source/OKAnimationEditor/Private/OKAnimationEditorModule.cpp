#include "UObject/UObjectHash.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "OKAnimThumbnailRenderer.h"
#include "UObject/ObjectSaveContext.h"
#include "HAL/FileManager.h"
#include "OKAnimationLibrary.h"
#include "Modules/ModuleManager.h"
#include "OKAnimationAssets.h"
#include "OKAnimationEditing.h"
#include "Animation/AnimSequence.h"
#include "ContentBrowserMenuContexts.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/Paths.h"
#include "ToolMenus.h"
#include "EditorModeRegistry.h"
#include "OKAnimEdMode.h"
#include "OKAnimationEditorToolkit.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "OKAnimationEditor"

namespace
{
	void Report(int32 Done, int32 Total, const FString& Errors, const FText& What)
	{
		FNotificationInfo Info(FText::Format(LOCTEXT("Report", "{0}: {1} / {2}"), What, Done, Total));
		if (!Errors.IsEmpty()) Info.SubText = FText::FromString(Errors.Left(600));
		Info.ExpireDuration = Errors.IsEmpty() ? 4.f : 10.f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
			Item->SetCompletionState(Done == Total ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}

	template <typename T>
	TArray<T*> Selected(const FToolMenuContext& Context)
	{
		const UContentBrowserAssetContextMenuContext* Menu = Context.FindContext<UContentBrowserAssetContextMenuContext>();
		return Menu ? Menu->LoadSelectedObjects<T>() : TArray<T*>();
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped Owner(TEXT("OKAnimationEditor"));
		if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.AnimSequence"))
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
			Section.AddMenuEntry("OKConvert", LOCTEXT("Convert", "Convert to OK Animation"),
				LOCTEXT("ConvertTip", "Creates skeleton-agnostic OK animations next to the selected animations (built-in presets: UE4/UE5 Mannequin, 3ds Max Biped, Mixamo, KK)."),
				FSlateIcon(), FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& Context)
				{
					const TSoftObjectPtr<UOKRetargetProfile> Default = GetDefault<UOKAnimationSettings>()->DefaultUnrealProfile;
					UOKRetargetProfile* Profile = Default.LoadSynchronous();
					int32 Done = 0;
					FString Errors;
					const TArray<UAnimSequence*> List = Selected<UAnimSequence>(Context);
					for (UAnimSequence* Seq : List)
					{
						FString Error;
						if (OKAnimationEditing::ConvertUnrealAnimation(Seq, Profile, FString(), GetDefault<UOKAnimationSettings>()->ImportFps, Error)) ++Done;
						else Errors += FString::Printf(TEXT("%s: %s\n"), *Seq->GetName(), *Error);
					}
					Report(Done, List.Num(), Errors, LOCTEXT("Converted", "Converted to OK"));
				}));
		}
		for (const TCHAR* Type : { TEXT("ContentBrowser.AssetContextMenu.AnimMontage"), TEXT("ContentBrowser.AssetContextMenu.BlendSpace"), TEXT("ContentBrowser.AssetContextMenu.BlendSpace1D"), TEXT("ContentBrowser.AssetContextMenu.LevelSequence") })
			if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(Type))
			{
				FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
				Section.AddMenuEntry("OKConvertOther", LOCTEXT("ConvertOther", "Convert to OK Animation"),
					LOCTEXT("ConvertOtherTip", "Montage -> OK montage (sections and branches), blend space -> OK blend space, level sequence -> OK montage of its animation sections; the animations they use are converted too."),
					FSlateIcon(), FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& Context)
					{
						UOKRetargetProfile* Profile = GetDefault<UOKAnimationSettings>()->DefaultUnrealProfile.LoadSynchronous();
						int32 Done = 0;
						FString Errors;
						const TArray<UObject*> List = Selected<UObject>(Context);
						for (UObject* Object : List)
						{
							FString Error;
							if (OKAnimationEditing::ConvertUnrealAsset(Object, Profile, FString(), GetDefault<UOKAnimationSettings>()->ImportFps, Error)) ++Done;
							else Errors += FString::Printf(TEXT("%s: %s\n"), *Object->GetName(), *Error);
						}
						Report(Done, List.Num(), Errors, LOCTEXT("ConvertedOther", "Converted to OK"));
					}));
			}
		if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.OKAnimSequence"))
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
			Section.AddMenuEntry("OKExpand", LOCTEXT("ExpandMenu", "Expand to AnimSequence"),
				LOCTEXT("ExpandMenuTip", "Writes an AnimSequence on the family preview mesh's skeleton to edit with Unreal's animation tools."),
				FSlateIcon(), FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& Context)
				{
					int32 Done = 0;
					FString Errors;
					const TArray<UOKAnimSequence*> List = Selected<UOKAnimSequence>(Context);
					for (UOKAnimSequence* Seq : List)
					{
						FString Error;
						if (OKAnimationEditing::ExpandToAnimSequence(Seq, OKAnimationEditing::DefaultExpandSkeleton(Seq), Error)) ++Done;
						else Errors += FString::Printf(TEXT("%s: %s\n"), *Seq->GetName(), *Error);
					}
					Report(Done, List.Num(), Errors, LOCTEXT("ExpandedMenu", "Expanded"));
				}));
			Section.AddMenuEntry("OKWriteBack", LOCTEXT("WriteBackMenu", "Write Back from AnimSequence"),
				LOCTEXT("WriteBackMenuTip", "Reads bones, contacts and curves back from the AnimSequence each OK animation was expanded to."),
				FSlateIcon(), FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& Context)
				{
					int32 Done = 0;
					FString Errors;
					const TArray<UOKAnimSequence*> List = Selected<UOKAnimSequence>(Context);
					for (UOKAnimSequence* Seq : List)
					{
						FString Error;
						if (OKAnimationEditing::WriteBack(Seq, Error)) ++Done;
						else Errors += FString::Printf(TEXT("%s: %s\n"), *Seq->GetName(), *Error);
					}
					Report(Done, List.Num(), Errors, LOCTEXT("WrittenMenu", "Written back"));
				}));
			Section.AddMenuEntry("OKExport", LOCTEXT("ExportMenu", "Export .okanim"),
				LOCTEXT("ExportMenuTip", "Writes the OK animation files to Saved/OKAnimExport."),
				FSlateIcon(), FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& Context)
				{
					int32 Done = 0;
					FString Errors;
					const TArray<UOKAnimSequence*> List = Selected<UOKAnimSequence>(Context);
					for (UOKAnimSequence* Seq : List)
					{
						FString Error;
						if (OKAnimationEditing::ExportFile(Seq, FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OKAnimExport"), Seq->GetName() + TEXT(".okanim")), Error)) ++Done;
						else Errors += Error + TEXT("\n");
					}
					Report(Done, List.Num(), Errors, LOCTEXT("ExportedMenu", "Exported"));
				}));
		}
	}
}

namespace
{
	FAutoConsoleCommand OpenEditorCommand(TEXT("OKAnim.OpenEditor"), TEXT("OKAnim.OpenEditor <asset path>: opens an OK animation asset in the OK animation editor."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			UObject* Asset = Args.Num() ? LoadObject<UObject>(nullptr, *Args[0]) : nullptr;
			if (Asset && GEditor) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
		}));
	FAutoConsoleCommand EditorSelfTestCommand(TEXT("OKAnim.EditorSelfTest"),
		TEXT("OKAnim.EditorSelfTest [exit]: runs the animate tools of the last opened OK animation editor (after it has posed) and logs PASS / FAIL."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bExit = Args.Contains(TEXT("exit"));
			// Wait until the editor tab is laid out and has posed its preview.
			TSharedRef<int32> Tries = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bExit, Tries](float)
			{
				FOKAnimationEditor* Editor = FOKAnimationEditor::LastOpened;
				if ((!Editor || !Editor->CanAnimate()) && ++*Tries < 600) return true;
				if (Editor) Editor->RunSelfTest(bExit);
				else { UE_LOG(LogTemp, Error, TEXT("OK editor selftest: no OK animation editor open")); if (bExit) FPlatformMisc::RequestExit(false); }
				return false;
			}), 0.1f);
		}));
}

class FOKAnimationEditorModule : public IModuleInterface
{
	FDelegateHandle PackageSaved;
public:
	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(&RegisterMenus));
		FEditorModeRegistry::Get().RegisterMode<FOKAnimEdMode>(FOKAnimEdMode::ModeId, LOCTEXT("AnimateMode", "OK Animate"), FSlateIcon(), false);
		UThumbnailManager::Get().RegisterCustomRenderer(UOKAnimSequence::StaticClass(), UOKAnimThumbnailRenderer::StaticClass());
		// Live reload (R3.10): a saved OK animation is written to Saved/OKAnim/Live/<asset name>.okanim, which a running
		// game watches and reloads into whatever plays an animation of that name.
		PackageSaved = UPackage::PackageSavedWithContextEvent.AddLambda([](const FString&, UPackage* Package, FObjectPostSaveContext)
		{
			ForEachObjectWithPackage(Package, [](UObject* Object)
			{
				if (UOKAnimSequence* Seq = Cast<UOKAnimSequence>(Object))
				{
					IFileManager::Get().MakeDirectory(*UOKAnimationLibrary::LiveFolder(), true);
					UOKAnimationLibrary::SaveOkAnimFile(Seq, FPaths::Combine(UOKAnimationLibrary::LiveFolder(), Seq->GetName() + TEXT(".okanim")));
				}
				return true;
			}, false);
		});
	}
	virtual void ShutdownModule() override
	{
		FEditorModeRegistry::Get().UnregisterMode(FOKAnimEdMode::ModeId);
		UPackage::PackageSavedWithContextEvent.Remove(PackageSaved);
		if (UObjectInitialized()) UThumbnailManager::Get().UnregisterCustomRenderer(UOKAnimSequence::StaticClass());
		if (UObjectInitialized())
		{
			UToolMenus::UnRegisterStartupCallback(this);
			UToolMenus::UnregisterOwner(TEXT("OKAnimationEditor"));
		}
	}
};

IMPLEMENT_MODULE(FOKAnimationEditorModule, OKAnimationEditor)

#undef LOCTEXT_NAMESPACE
