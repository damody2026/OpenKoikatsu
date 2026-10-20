#include "../OKEditorLabelsZh.h"
#include "../OKBoneControls.h"
#include "../OKPregnancyPresets.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKEditorChineseTest,"OpenKoikatsu.Gallery.ChineseSettings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKEditorChineseTest::RunTest(const FString&)
{
 for(const auto& Def:OKBoneControls){bool Chinese=false;for(TCHAR C:FString(Def.Label)){Chinese|=C>=0x3400&&C<=0x9fff;if((C>='a'&&C<='z')||(C>='A'&&C<='W')){AddError(FString(Def.Label));return false;}}if(!Chinese){AddError(TEXT("骨骼設定缺少中文"));return false;}}
 TSet<FString> Labels;for(const auto& Preset:OKPregnancyPresets){Labels.Add(Preset.Label);TestNotEqual(TEXT("預設顯示與來源識別碼分開"),FString(Preset.Label),FString(Preset.Name));}
 TestEqual(TEXT("所有預設名稱保持唯一"),Labels.Num(),int32(UE_ARRAY_COUNT(OKPregnancyPresets)));
 TestEqual(TEXT("動畫身高校正"),OKAnimationParameterLabel(TEXT("height1"),0),FString(TEXT("身高校正 1")));
 TestEqual(TEXT("動畫層時間"),OKAnimationParameterLabel(TEXT("OK.LayerStartTime.7"),0),FString(TEXT("動畫層開始時間 7")));
 TestEqual(TEXT("自訂控制器的中文備援"),OKAnimationParameterLabel(TEXT("PluginCustomFloat"),4),FString(TEXT("自訂動畫參數 5")));
 return true;
}
#endif
