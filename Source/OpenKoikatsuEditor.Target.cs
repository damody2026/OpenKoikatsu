using UnrealBuildTool;
using System.Collections.Generic;

public class OpenKoikatsuEditorTarget : TargetRules
{
	public OpenKoikatsuEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("OpenKoikatsu");
		// VS project OpenKoikatsu, configuration Development_Editor, runs this
		// before UBT compiles. Cargo must finish before the link sees ok_extract.lib.
		PreBuildSteps.Add("call \"$(ProjectDir)\\Tools\\ok-extract\\build-release.cmd\"");
	}
}
