#include "../OKCharacterMCPRegistry.h"
#include "../OKGalleryWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKCharacterMCPRegistryTest,"OpenKoikatsu.CharacterMCP.Registry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKCharacterMCPRegistryTest::RunTest(const FString&)
{
 auto* G=NewObject<UOKGalleryWidget>();G->WidgetTree=NewObject<UWidgetTree>(G);G->Selected.Body.ShapeValueBody.Init(.5f,44);G->Selected.Face.ShapeValueFace.Init(.5f,52);
 auto Rows=OKCharacterMCPRegistry::Parameters(G);TSet<FString> Keys;int32 Body=0,Face=0,Bones=0;
 for(auto R:Rows){auto P=R->AsObject();const FString Key=P->GetStringField(TEXT("key"));TestFalse(TEXT("Unique parameter keys"),Keys.Contains(Key));Keys.Add(Key);Body+=Key.StartsWith(TEXT("shape.body."));Face+=Key.StartsWith(TEXT("shape.face."));if(Key.StartsWith(TEXT("bone."))){++Bones;TestFalse(TEXT("Missing bones advertised unsupported"),P->GetBoolField(TEXT("supported")));}}
 TestEqual(TEXT("All body parameters"),Body,44);TestEqual(TEXT("All face parameters"),Face,52);TestTrue(TEXT("Full left/right bone catalog"),Bones>1192);
 auto Edit=[](const TCHAR* Key,double Value){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("key"),Key);O->SetNumberField(TEXT("value"),Value);return MakeShared<FJsonValueObject>(O);};FString Error;
 TestFalse(TEXT("Invalid batch rejected"),OKCharacterMCPRegistry::SetParameters(G,{Edit(TEXT("shape.body.0"),.8),Edit(TEXT("shape.face.999"),.4)},Error));TestEqual(TEXT("No partial writes"),G->Selected.Body.ShapeValueBody[0],.5f);
 TestFalse(TEXT("Out of range rejected"),OKCharacterMCPRegistry::SetParameters(G,{Edit(TEXT("shape.body.0"),3)},Error));
 TestFalse(TEXT("Duplicate keys rejected"),OKCharacterMCPRegistry::SetParameters(G,{Edit(TEXT("shape.body.0"),.8),Edit(TEXT("shape.body.0"),.9)},Error));
 TestTrue(TEXT("Valid batch applies"),OKCharacterMCPRegistry::SetParameters(G,{Edit(TEXT("shape.body.0"),.8),Edit(TEXT("shape.face.0"),.2)},Error));TestEqual(TEXT("Body readback"),G->Selected.Body.ShapeValueBody[0],.8f);TestEqual(TEXT("Face readback"),G->Selected.Face.ShapeValueFace[0],.2f);
 return true;
}
#endif
