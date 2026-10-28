using UnrealBuildTool;
using System.IO;
using System.Diagnostics;
using System.Linq;
public class KKRuntimePose:ModuleRules {
 public KKRuntimePose(ReadOnlyTargetRules T):base(T){
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","RenderCore","RHI","AudioExtensions"});
  PrivateDependencyModuleNames.AddRange(new[]{"DynamicMesh","GeometryCore","Json","ImageWrapper","VorbisAudioDecoder","KKCrowdGpuPacking","OKPhysXSecondary"});
  var NativeTools=new DirectoryInfo(Path.GetFullPath(Path.Combine(ModuleDirectory,"../../../../Tools")));
  string ToolsRoot=NativeTools.LinkTarget!=null?NativeTools.ResolveLinkTarget(true).FullName:NativeTools.FullName;
  string Root=Path.Combine(ToolsRoot,"ok-extract");
  // OK_EXTRACT_ROOT (as KKUnityBundle): the same exported copy of Tools/ok-extract. Linked from the shared tree here while
  // KKUnityBundle took the copy, the shared tree's ok_extract.lib won and a packaging run still got its unfinished Rust.
  {string Override=System.Environment.GetEnvironmentVariable("OK_EXTRACT_ROOT");if(!string.IsNullOrEmpty(Override)){Root=Path.GetFullPath(Override);System.Console.WriteLine("KKRuntimePose: ok-extract from OK_EXTRACT_ROOT "+Root);}}
  PublicIncludePaths.Add(Path.Combine(Root,"include"));
  // Persistent poses depend on code contents, not compilation wall-clock time.
  // Hash relative names + framed file bytes so unchanged relocations/rebuilds
  // reuse caches, while pose preparation/packing/decoder edits invalidate them.
  using(var CodeBytes=new MemoryStream()){
   using(var Writer=new BinaryWriter(CodeBytes,System.Text.Encoding.UTF8,true)){
    foreach(string CodeRoot in new[]{ModuleDirectory,Path.GetFullPath(Path.Combine(ModuleDirectory,"../../../KKCrowdGpuPacking/Source/KKCrowdGpuPacking"))}){
     if(!Directory.Exists(CodeRoot))throw new BuildException("Runtime GPU cache code dependency missing: "+CodeRoot);
     foreach(string FilePath in Directory.GetFiles(CodeRoot,"*",SearchOption.AllDirectories).Where(F=>new[]{".cpp",".h",".inl",".cs"}.Contains(Path.GetExtension(F))).OrderBy(F=>Path.GetRelativePath(CodeRoot,F),System.StringComparer.Ordinal)){
      ExternalDependencies.Add(FilePath);byte[] Contents=File.ReadAllBytes(FilePath);
      Writer.Write(Path.GetRelativePath(CodeRoot,FilePath).Replace('\\','/'));Writer.Write(Contents.Length);Writer.Write(Contents);
     }
    }
   }
   using(var Hash=System.Security.Cryptography.SHA256.Create())
    PrivateDefinitions.Add("OK_RUNTIME_POSE_CACHE_CODE=\""+System.Convert.ToHexString(Hash.ComputeHash(CodeBytes.ToArray()))+"\"");
  }
  if(T.Platform==UnrealTargetPlatform.Win64){
   var P=new ProcessStartInfo("cmd.exe","/c \"\""+Path.Combine(Root,"build-release.cmd")+"\"\""){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=Root,RedirectStandardOutput=true,RedirectStandardError=true};
   using(var Build=Process.Start(P)){var Out=Build.StandardOutput.ReadToEndAsync();var Err=Build.StandardError.ReadToEndAsync();Build.WaitForExit();if(Build.ExitCode!=0)throw new BuildException("Runtime native extractor build failed: "+Out.GetAwaiter().GetResult()+Err.GetAwaiter().GetResult());}
   PublicAdditionalLibraries.Add(Path.Combine(Root,"target/release/ok_extract.lib"));
   PublicSystemLibraries.AddRange(new[]{"bcrypt.lib","ws2_32.lib","userenv.lib","ntdll.lib","dbghelp.lib","d3d11.lib","d3dcompiler.lib","dxgi.lib"});
   ExternalDependencies.Add(Path.Combine(Root,"target/release/ok_extract.lib"));
   using(var Input=File.OpenRead(Path.Combine(Root,"target/release/ok_extract.lib")))
   using(var Hash=System.Security.Cryptography.SHA256.Create())
    PrivateDefinitions.Add("OK_RUNTIME_NATIVE_CACHE_BUILD=\""+System.Convert.ToHexString(Hash.ComputeHash(Input))+"\"");
  }
 }
}
