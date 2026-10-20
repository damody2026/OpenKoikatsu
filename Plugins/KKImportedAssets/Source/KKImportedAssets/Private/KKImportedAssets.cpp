#include "Modules/ModuleManager.h"
#include "KKImportedAnimSequence.h"
#if WITH_EDITOR
#include "KKImportedAnimSequence.h"
#include "Animation/AnimData/AnimDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Features/IModularFeatures.h"
class FKKQuaternionAnimationModel:public UE::Anim::DataModel::IAnimationDataModels {
 virtual UClass* GetModelClass(UAnimSequenceBase* Asset) const override {
  return Asset && Asset->IsA<UKKImportedAnimSequence>()?UAnimDataModel::StaticClass():nullptr;
 }
};
static FKKQuaternionAnimationModel GQuaternionModel;
static bool GQuaternionModelRegistered=false;
static void MakeQuaternionProviderLast(){
 if(GQuaternionModelRegistered){auto& Features=IModularFeatures::Get();
  Features.UnregisterModularFeature(UE::Anim::DataModel::IAnimationDataModels::GetModularFeatureName(),&GQuaternionModel);
  Features.RegisterModularFeature(UE::Anim::DataModel::IAnimationDataModels::GetModularFeatureName(),&GQuaternionModel);
 }
}
#endif
void UKKImportedAnimSequence::PostInitProperties(){
#if WITH_EDITOR
 MakeQuaternionProviderLast();
#endif
 Super::PostInitProperties();
#if WITH_EDITOR
 // UAnimSequenceBase creates the model in its constructor, before our
 // PostInitProperties can reorder providers. Replace that empty model only
 // for newly created imported assets; loaded models retain their source keys.
 if(!HasAnyFlags(RF_ClassDefaultObject|RF_NeedLoad) && DataModelInterface && !DataModelInterface.GetObject()->IsA<UAnimDataModel>()){
  DataModelInterface->GetModifiedEvent().RemoveAll(this);Controller=nullptr;
  DataModelInterface=NewObject<UAnimDataModel>(this,TEXT("QuaternionDataModel"));GetController();BindToModelModificationEvent();
 }
#endif
}
void UKKImportedAnimSequence::PostLoad(){
#if WITH_EDITOR
 MakeQuaternionProviderLast();
#endif
 Super::PostLoad();
}
class FKKImportedAssetsModule:public IModuleInterface {
public:
 virtual void StartupModule() override {
#if WITH_EDITOR
  // Engine AnimationData registers its Sequencer provider at PreDefault.
  // This Default-phase provider overrides only our imported asset subclass.
  FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("AnimationDataController"));
  IModularFeatures::Get().RegisterModularFeature(UE::Anim::DataModel::IAnimationDataModels::GetModularFeatureName(),&GQuaternionModel);GQuaternionModelRegistered=true;
#endif
 }
 virtual void ShutdownModule() override {
#if WITH_EDITOR
  IModularFeatures::Get().UnregisterModularFeature(UE::Anim::DataModel::IAnimationDataModels::GetModularFeatureName(),&GQuaternionModel);GQuaternionModelRegistered=false;
#endif
 }
};
IMPLEMENT_MODULE(FKKImportedAssetsModule,KKImportedAssets)
