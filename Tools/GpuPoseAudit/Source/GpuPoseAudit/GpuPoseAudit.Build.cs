using UnrealBuildTool;
public class GpuPoseAudit:ModuleRules {
 public GpuPoseAudit(ReadOnlyTargetRules Target):base(Target){PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","UnrealEd","RenderCore","RHI","KKCrowdGpuPacking","KKImportedAssets","Json","MeshDescription","SkeletalMeshDescription"});
  bool SharedMaterials=Target.ProjectFile!=null&&System.IO.File.Exists(System.IO.Path.Combine(Target.ProjectFile.Directory.FullName,"Plugins/KKRuntimePose/KKRuntimePose.uplugin"));
  PrivateDefinitions.Add("OK_RUNTIME_SHARED_MATERIAL_TOOL="+(SharedMaterials?"1":"0"));if(SharedMaterials)PublicDependencyModuleNames.Add("KKRuntimePose");
  bool HGShading=SharedMaterials&&System.IO.File.Exists(System.IO.Path.Combine(Target.ProjectFile.Directory.FullName,"Plugins/OKHGShading/OKHGShading.uplugin"));
  PrivateDefinitions.Add("OK_HG_SHADING_TOOL="+(HGShading?"1":"0"));if(HGShading)PublicDependencyModuleNames.Add("OKHGShading");
  var Root=new System.IO.DirectoryInfo(ModuleDirectory);while(Root!=null&&!System.IO.File.Exists(System.IO.Path.Combine(Root.FullName,"Tools/ok-extract/target/release/ok_extract.lib")))Root=Root.Parent;
  if(Root==null)throw new BuildException("Existing native extraction library required for offline rigid-bind repair");var Native=System.IO.Path.Combine(Root.FullName,"Tools/ok-extract");PrivateIncludePaths.Add(System.IO.Path.Combine(Native,"include"));PublicAdditionalLibraries.Add(System.IO.Path.Combine(Native,"target/release/ok_extract.lib"));PublicSystemLibraries.AddRange(new[]{"bcrypt.lib","ws2_32.lib","userenv.lib","ntdll.lib","dbghelp.lib","d3d11.lib","d3dcompiler.lib","dxgi.lib"});
 }
}
