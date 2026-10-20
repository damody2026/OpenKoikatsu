#include "../OKGalleryWidget.h"
#include "../OKShapeSlider.h"
#include "../OKBoneControls.h"
#include "Components/ComboBoxString.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"
#include "KKCardReader.h"
#include "KKCardWriter.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKEditorModsTest,"OpenKoikatsu.Gallery.EditorMods",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKEditorModsTest::RunTest(const FString&){
 auto* Widget=NewObject<UOKGalleryWidget>();FString Error;
 const FString Fixture=FPaths::ProjectContentDir()/TEXT("CharacterCards/CrowdAPNG/C000.png");
 if(!TestTrue(TEXT("Load real portable card"),UKKCardReader::LoadFromFile(Fixture,Widget->Selected,Error)))return false;
 auto* Slider=NewObject<UOKShapeSlider>(Widget);Slider->Owner=Widget;Slider->ModBone=TEXT("cf_s_bust01_L");Slider->ModRight=TEXT("cf_s_bust01_R");Slider->ModAxis=-1;
 Slider->ModSide=NewObject<UComboBoxString>(Slider);for(const TCHAR* Side:{TEXT("雙側"),TEXT("左"),TEXT("右")})Slider->ModSide->AddOption(Side);Slider->ModSide->SetSelectedIndex(1);
 auto ReadScale=[&](const TCHAR* Name){TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Widget->EditorModsJson()),Root);
  for(const auto& Entry:Root->GetArrayField(TEXT("abm")))if(Entry->AsObject()->GetStringField(TEXT("name"))==Name){return float(Entry->AsObject()->GetArrayField(TEXT("coord"))[0]->AsObject()->GetArrayField(TEXT("scale"))[0]->AsNumber());}return 1.f;};
 const float RightBefore=ReadScale(TEXT("cf_s_bust01_R"));Widget->OnBoneChanged(Slider,1.35f);
 TestEqual(TEXT("Left modifier changes"),ReadScale(TEXT("cf_s_bust01_L")),1.35f);TestEqual(TEXT("Right modifier preserved"),ReadScale(TEXT("cf_s_bust01_R")),RightBefore);
 Slider->ModSide->SetSelectedIndex(0);Widget->OnBoneChanged(Slider,1.15f);
 TestEqual(TEXT("Both sides update left"),ReadScale(TEXT("cf_s_bust01_L")),1.15f);TestEqual(TEXT("Both sides update right"),ReadScale(TEXT("cf_s_bust01_R")),1.15f);
 Slider->OnResetClicked();TestEqual(TEXT("Mod reset is unity scale"),ReadScale(TEXT("cf_s_bust01_L")),1.f);
 Widget->OnBodyPhysicsChanged(TEXT("bustSoftness"),.8f);Widget->OnBodyPhysicsChanged(TEXT("bustWeight"),.3f);
 Widget->OnPregnancyChanged(TEXT("inflationSize"),30.f);Widget->OnPregnancyChanged(TEXT("inflationMoveY"),.05f);
 Widget->OnBoneChanged(Slider,1.25f);
 const FString Output=FPaths::ProjectSavedDir()/TEXT("Tests/EditorMods/roundtrip.png");IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);
 TestTrue(TEXT("Save edited PNG with mod data"),UKKCardWriter::SaveWithEdits(Widget->Selected,Output,Error));
 FKKCharacterFile Reloaded;TestTrue(TEXT("Reload edited PNG"),UKKCardReader::LoadFromFile(Output,Reloaded,Error));
 TestTrue(TEXT("All extended data round-trips"),Reloaded.RawBlocks.FindRef(TEXT("KKEx"))==Widget->Selected.RawBlocks.FindRef(TEXT("KKEx")));
 TSharedPtr<FJsonObject> Mods;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FOkSession::DecodeEditorMods(Reloaded.RawBlocks.FindRef(TEXT("KKEx")))),Mods);
 TestEqual(TEXT("Softness round-trips"),Reloaded.Body.BustSoftness,.8f);TestEqual(TEXT("Weight round-trips"),Reloaded.Body.BustWeight,.3f);
 TestEqual(TEXT("Pregnancy size round-trips"),Mods->GetObjectField(TEXT("pregnancy"))->GetNumberField(TEXT("inflationSize")),30.);
 FOkSession SavedSession;
 TestTrue(TEXT("Edited PNG opens as a real native model in a different folder"),SavedSession.Open(FString(),Output,0,Error));
 TestTrue(TEXT("Saved model remains portable"),SavedSession.IsPortable());
 Widget->OnResetAllShapes();FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Widget->EditorModsJson()),Mods);
 TestEqual(TEXT("Global reset restores pregnancy size"),Mods->GetObjectField(TEXT("pregnancy"))->GetNumberField(TEXT("inflationSize")),0.);
 TestTrue(TEXT("Complete generated mod registry"),UE_ARRAY_COUNT(OKBoneControls)>1000);
 Widget->MarkAsGarbage();return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKEditorCoordinatesTest,"OpenKoikatsu.Gallery.EditorModsCoordinates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKEditorCoordinatesTest::RunTest(const FString&){
 auto* Widget=NewObject<UOKGalleryWidget>();
 auto* Slider=NewObject<UOKShapeSlider>(Widget);Slider->ModBone=TEXT("cf_d_sk_top");Slider->ModAxis=-1;Slider->ModCoordinateSpecific=true;
 Widget->OnBoneChanged(Slider,1.2f);Widget->OutfitIndex=2;Widget->OnBoneChanged(Slider,1.6f);
 TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Widget->EditorModsJson()),Root);
 const auto& Coordinates=Root->GetArrayField(TEXT("abm"))[0]->AsObject()->GetArrayField(TEXT("coord"));
 TestEqual(TEXT("Coordinate count retained"),Coordinates.Num(),7);
 auto Scale=[&](int32 Index){return float(Coordinates[Index]->AsObject()->GetArrayField(TEXT("scale"))[0]->AsNumber());};
 TestEqual(TEXT("Outfit 0 unchanged"),Scale(0),1.2f);TestEqual(TEXT("Outfit 1 retains its initial neutral value"),Scale(1),1.f);TestEqual(TEXT("Only outfit 2 edited"),Scale(2),1.6f);
 Slider->Owner=Widget;Slider->OnResetClicked();FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Widget->EditorModsJson()),Root);
 const auto& After=Root->GetArrayField(TEXT("abm"))[0]->AsObject()->GetArrayField(TEXT("coord"));
 TestEqual(TEXT("Active coordinate resets"),float(After[2]->AsObject()->GetArrayField(TEXT("scale"))[0]->AsNumber()),1.f);
 TestEqual(TEXT("Reset preserves other coordinates"),float(After[0]->AsObject()->GetArrayField(TEXT("scale"))[0]->AsNumber()),1.2f);
 Widget->bEnterInteractionAfterLoad=true;Widget->OnBodyPhysicsChanged(TEXT("bustWeight"),.8f);Widget->OnPregnancyChanged(TEXT("inflationSize"),40.f);
 TestEqual(TEXT("Transition blocks body physics changes"),Widget->Selected.Body.BustWeight,.5f);
 FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Widget->EditorModsJson()),Root);TestEqual(TEXT("Transition blocks pregnancy changes"),Root->GetObjectField(TEXT("pregnancy"))->GetNumberField(TEXT("inflationSize")),0.);
 Widget->PairCatalogueJsonRows.Add(TEXT("{}"));Widget->ResumePairCatalogue();
 TestFalse(TEXT("Single-character editing does not prepare unrelated pair catalogues"),Widget->bPairCataloguePreparing);
 Widget->MarkAsGarbage();return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKEditorClothingTest,"OpenKoikatsu.Gallery.EditorClothing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKEditorClothingTest::RunTest(const FString&){
 auto* Widget=NewObject<UOKGalleryWidget>();Widget->bIndependentCards=true;Widget->SelectedIndex=0;
 FKKCardListEntry Entry;Entry.Path=FPaths::ProjectContentDir()/TEXT("CharacterCards/CrowdAPNG/C000.png");Widget->Entries.Add(Entry);
 Widget->OnClothesState(0);TestEqual(TEXT("Native first half state"),Widget->ClothesStates[0],uint8(1));
 Widget->OnClothesState(0);TestEqual(TEXT("Unsupported second-half state is skipped"),Widget->ClothesStates[0],uint8(3));
 TestEqual(TEXT("Linked one-piece bottom follows full removal"),Widget->ClothesStates[1],uint8(3));
 Widget->OnClothesState(0);TestEqual(TEXT("Individual wear restores top"),Widget->ClothesStates[0],uint8(0));TestEqual(TEXT("Individual wear restores linked bottom"),Widget->ClothesStates[1],uint8(0));
 Widget->OnClothesState(102);for(uint8 State:Widget->ClothesStates)TestEqual(TEXT("Full removal applies all native slots"),State,uint8(3));
 Widget->OnClothesState(100);for(uint8 State:Widget->ClothesStates)TestEqual(TEXT("Wear all restores all slots"),State,uint8(0));
 Widget->MarkAsGarbage();return !HasAnyErrors();
}
#endif
