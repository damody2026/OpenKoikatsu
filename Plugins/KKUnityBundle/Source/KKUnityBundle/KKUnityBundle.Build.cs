using UnrealBuildTool;
using System;
using System.Diagnostics;
using System.IO;

public class KKUnityBundle : ModuleRules
{
	public KKUnityBundle(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
            "KKImportedAssets",
            "KKRuntimePose",
            "KKCrowdGpuPacking",
            "AudioExtensions",
			"RenderCore",
			"RHI",
            "PhysicsCore",
			"ProceduralMeshComponent",
			"Json",
			"JsonUtilities",
			"ImageWrapper"
		});
		PrivateDependencyModuleNames.AddRange(new string[]{"OKPhysXSecondary","VorbisAudioDecoder","AssetRegistry","KKCard","KKSideloader"});
        PrivateIncludePathModuleNames.Add("TargetPlatform");
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]{"AssetRegistry","UnrealEd","MeshDescription","StaticMeshDescription","SkeletalMeshDescription","MeshBuilder","SkeletalMeshUtilitiesCommon","AnimationCore","KKCard","KKSideloader"});
		}


		string ExtractRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "Tools", "ok-extract"));
		// OK_EXTRACT_ROOT: build and link ok_extract.lib from another copy of Tools/ok-extract (an exported tree), so a
		// packaging run in a shared checkout does not pick up Rust edits someone else is still making there.
		string ExtractOverride = Environment.GetEnvironmentVariable("OK_EXTRACT_ROOT");
		if (!string.IsNullOrEmpty(ExtractOverride))
		{
			ExtractRoot = Path.GetFullPath(ExtractOverride);
			Console.WriteLine("KKUnityBundle: ok-extract from OK_EXTRACT_ROOT " + ExtractRoot);
		}
		string BuiltinRoot = Path.Combine(Path.GetDirectoryName(Target.ProjectFile.FullName), "Content", "IndependentResources", "v1");
		if (Directory.Exists(BuiltinRoot))
		{
			ExternalDependencies.Add(Path.Combine(BuiltinRoot, "catalog.json"));
			foreach (string Asset in Directory.GetFiles(BuiltinRoot, "*", SearchOption.AllDirectories))
			{
				if (Asset.EndsWith(".okasset") || Path.GetFileName(Asset) == "catalog.json")
				{
					RuntimeDependencies.Add(Asset, StagedFileType.NonUFS);
				}
			}
		}
		PublicIncludePaths.Add(Path.Combine(ExtractRoot, "include"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("bcrypt.lib");
			EnsureRustLib(ExtractRoot);
			string Lib = Path.Combine(ExtractRoot, "target", "release", "ok_extract.lib");
			PublicAdditionalLibraries.Add(Lib);
			// Rust sources are not C++ inputs. Listing them here invalidates the
			// UBT makefile so Development_Editor rebuilds and relinks the new .lib.
			ExternalDependencies.Add(Path.Combine(ExtractRoot, "Cargo.toml"));
            ExternalDependencies.Add(Path.Combine(ExtractRoot, "build.rs"));
            ExternalDependencies.Add(Path.Combine(ExtractRoot, "src", "clothes_gpu.cpp"));
			ExternalDependencies.Add(Path.Combine(ExtractRoot, "build-release.cmd"));
			ExternalDependencies.Add(Lib);
			string Stamp = Path.Combine(ModuleDirectory, "Private", "OKExtractBuildStamp.h");
			if (File.Exists(Stamp))
			{
				ExternalDependencies.Add(Stamp);
			}
			string Src = Path.Combine(ExtractRoot, "src");
			if (Directory.Exists(Src))
			{
				foreach (string Rs in Directory.GetFiles(Src, "*.rs", SearchOption.AllDirectories))
				{
					ExternalDependencies.Add(Rs);
				}
			}
			PublicSystemLibraries.AddRange(new string[]
			{
				"ws2_32.lib",
				"userenv.lib",
				"ntdll.lib",
				"dbghelp.lib",
                "d3d11.lib", "d3dcompiler.lib", "dxgi.lib"
			});
		}
	}

	static void EnsureRustLib(string ExtractRoot)
	{
		string Script = Path.Combine(ExtractRoot, "build-release.cmd");
		if (!File.Exists(Script))
		{
			throw new BuildException("missing " + Script);
		}
		ProcessStartInfo Info = new ProcessStartInfo
		{
			FileName = "cmd.exe",
			Arguments = "/C \"" + Script + "\"",
			WorkingDirectory = ExtractRoot,
			UseShellExecute = false,
			RedirectStandardOutput = true,
			RedirectStandardError = true,
			CreateNoWindow = true
		};
		using (Process Proc = Process.Start(Info))
		{
			Proc.OutputDataReceived += (Sender, Args) =>
			{
				if (Args.Data != null)
				{
					Console.WriteLine(Args.Data);
				}
			};
			Proc.ErrorDataReceived += (Sender, Args) =>
			{
				if (Args.Data != null)
				{
					Console.WriteLine(Args.Data);
				}
			};
			Proc.BeginOutputReadLine();
			Proc.BeginErrorReadLine();
			Proc.WaitForExit();
			if (Proc.ExitCode != 0)
			{
				throw new BuildException("cargo build --release failed for Tools/ok-extract");
			}
		}
	}
}
