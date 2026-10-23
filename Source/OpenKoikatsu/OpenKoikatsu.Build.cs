using UnrealBuildTool;

public class OpenKoikatsu : ModuleRules
{
	public OpenKoikatsu(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDefinitions.Add("OK_CROWD_RUNTIME_APNG=1");
		// Crowd fast paths need the engine patches listed in Tools/EnginePatches/README.md. A missing patch used to
		// turn its define into 0 silently (slower crowd, no error); now the build stops unless OK_ALLOW_UNPATCHED_ENGINE=1.
		bool AllowUnpatched=System.Environment.GetEnvironmentVariable("OK_ALLOW_UNPATCHED_ENGINE")=="1";
		System.Func<string,string,string,string> Patched=(Define,RelativeSource,Marker)=>{
			string Source=System.IO.Path.Combine(EngineDirectory,RelativeSource);
			bool Found=System.IO.File.Exists(Source)&&System.IO.File.ReadAllText(Source).Contains(Marker);
			if(!Found&&!AllowUnpatched)throw new BuildException("Engine patch missing: "+Marker+" not in "+Source+". Apply Tools/EnginePatches (see README.md) or set OK_ALLOW_UNPATCHED_ENGINE=1.");
			return Define+"="+(Found?"1":"0");};
		PrivateDefinitions.Add(Patched("OK_CROWD_COMPONENT_MATRIX_CACHE","Source/Runtime/Engine/Private/SkeletalRender.cpp","GetSkeletalComponentMatrixCacheStats"));
		PrivateDefinitions.Add(Patched("OK_CROWD_FULL_REF_MATRIX_CACHE","Source/Runtime/Engine/Private/SkeletalRender.cpp","GetSkeletalFullRefMatrixCacheStats"));
		PrivateDefinitions.Add(Patched("OK_CROWD_WEIGHTED_POSE_SEED","Source/Runtime/Engine/Private/Components/SkeletalMeshComponent.cpp","SetSkeletalWeightedPoseSeed"));
		PrivateDefinitions.Add(Patched("OK_CROWD_CONSTANT_SCALE_CACHE","Source/Runtime/Engine/Private/Animation/AnimCompress.cpp","PrepareAnimConstantScaleCache"));
		// Coincident clothing layers draw in source order (KKPreviewActor draw_layer -> TranslucencySortPriority).
		PrivateDefinitions.Add(Patched("OK_OPAQUE_SORT_PRIORITY","Source/Runtime/Renderer/Private/BasePassRendering.cpp","OKOpaqueSortPriority"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
            "Core",
            "Json",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"UMG",
			"Slate",
			"SlateCore",
			"ImageWrapper",
			"KKCard",
			"KKSideloader",
			"KKUnityBundle",
            "KKRuntimePose"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI", "Renderer", "KKCrowdGpuPacking", "OKHGShading", "ImageCore", "ProceduralMeshComponent", "Niagara", "NiagaraCore", "NavigationSystem", "AIModule", "HTTP", "HTTPServer", "OKPhysXSecondary", "OKStudio" });
		PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory,"Source/Runtime/Renderer/Private"));
        PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory,"Source/Runtime/Renderer/Internal"));
  PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory,"Source/Runtime/Engine/Internal"));
		if (Target.bBuildEditor) PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory,"Plugins/FX/Niagara/Source/Niagara/Internal"));
		if (Target.bBuildEditor) {PrivateDependencyModuleNames.Add("NiagaraShader");PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory,"Plugins/FX/Niagara/Source/NiagaraShader/Internal"));}
		PublicDependencyModuleNames.Add("KKImportedAssets");
		// Video motion import: native open-file dialog (OKVideoMotionPanel).
		if (Target.Platform == UnrealTargetPlatform.Win64) PublicSystemLibraries.Add("comdlg32.lib");
		if (Target.bBuildEditor) PrivateDependencyModuleNames.AddRange(new string[] {"UnrealEd","MeshDescription","StaticMeshDescription","SkeletalMeshDescription","SkeletalMeshUtilitiesCommon","MeshUtilities","MaterialBaking","NiagaraEditor"});
	}
}
