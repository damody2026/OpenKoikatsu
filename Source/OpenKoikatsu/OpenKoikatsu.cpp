#include "OpenKoikatsu.h"
#include "Modules/ModuleManager.h"
#include "NiagaraModule.h"

class FOpenKoikatsuModule final : public FDefaultGameModuleImpl {
public:
    virtual void StartupModule() override {
        FDefaultGameModuleImpl::StartupModule();
        // Gameplay-owned effects need Niagara's template and world lifecycle
        // registrations before any World is created, including minimal runtime stages.
        FModuleManager::LoadModuleChecked<INiagaraModule>(TEXT("Niagara"));
    }
};
// Primary module of this project (OK_Demo had its own primary module and used IMPLEMENT_GAME_MODULE here).
IMPLEMENT_PRIMARY_GAME_MODULE(FOpenKoikatsuModule, OpenKoikatsu, "OpenKoikatsu");
