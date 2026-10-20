#include "Modules/ModuleManager.h"
#include "OKStudioEditor.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "OKStudioEditor"

/** Tools menu entry for OK Studio. */
class FOKStudioEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]
		{
			UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
			FToolMenuSection& Section = Menu->FindOrAddSection("OpenKoikatsu");
			Section.AddMenuEntry("OKStudio", LOCTEXT("OKStudio", "OK Studio"),
				LOCTEXT("OKStudioTip", "Scene studio on OK cards and the OK base library"), FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([] { FOKStudioEditor::Open(FString()); })));
		}));
	}
};

IMPLEMENT_MODULE(FOKStudioEditorModule, OKStudioEditor)

#undef LOCTEXT_NAMESPACE
