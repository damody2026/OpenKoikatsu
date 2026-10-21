// Read-only native Studio scene inputs, captured after Studio applies its saved settings.
using System;
using System.Collections;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using UnityEngine;

public sealed partial class GamePoseProbe {
    [DllImport("kernel32", EntryPoint = "LoadLibraryW", CharSet = CharSet.Unicode)]
    private static extern IntPtr LoadShaderProbeLibrary(string path);
    [DllImport("kernel32", EntryPoint = "GetProcAddress", CharSet = CharSet.Ansi)]
    private static extern IntPtr ShaderProbeProc(IntPtr module, string name);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int ConfigureShaderProbe(IntPtr texture, IntPtr output);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate IntPtr ShaderProbeEvent();

    private IEnumerator AuditStudioScene(Studio.Studio studio, string scene) {
        string output = Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT");
        if (String.IsNullOrEmpty(output)) throw new InvalidOperationException("Scene audit requires OK_GAME_POSE_OUTPUT");
        if (scene == "@empty") studio.InitScene(false);
        else if (!studio.LoadScene(scene)) {
            File.WriteAllText(output + ".error.txt", "Studio.LoadScene failed: " + scene);
            finished = true;
            Application.Quit();
            yield break;
        }
        for (int frame = 0; frame < 120; ++frame) yield return null;
        using (var writer = new StreamWriter(output + ".scene.tsv")) {
            writer.WriteLine("scene\t" + scene);
            foreach (Shader shader in Resources.FindObjectsOfTypeAll<Shader>())
                if (shader.name.StartsWith("Koikano/")) writer.WriteLine("shader_object\t" + shader.name + "\t" + shader.GetInstanceID());
            foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies()) {
                Type api = assembly.GetType("MaterialEditorAPI.MaterialEditorPluginBase", false);
                if (api == null) continue;
                FieldInfo registry = api.GetField("LoadedShaders", BindingFlags.Public | BindingFlags.Static);
                var loaded = registry == null ? null : registry.GetValue(null) as IDictionary;
                if (loaded == null) continue;
                foreach (DictionaryEntry item in loaded) {
                    if (!item.Key.ToString().StartsWith("Koikano/")) continue;
                    FieldInfo field = item.Value.GetType().GetField("Shader");
                    Shader shader = field == null ? null : field.GetValue(item.Value) as Shader;
                    writer.WriteLine("registered_shader\t" + item.Key + "\t" + (shader == null ? 0 : shader.GetInstanceID()));
                }
            }
            foreach (string key in new[] { "_ambientshadowG", "_LineColorG", "_LightColor0", "_WorldSpaceLightPos0" })
                writer.WriteLine("global_color\t" + key + "\t" + Shader.GetGlobalVector(key).ToString("R"));
            foreach (string key in new[] { "_KanoVerShift", "_linewidthG", "_FaceNormalG", "_FaceShadowG" })
                writer.WriteLine("global_float\t" + key + "\t" + Shader.GetGlobalFloat(key).ToString("R"));
            writer.WriteLine("ambient\t" + RenderSettings.ambientMode + "\t" + RenderSettings.ambientIntensity.ToString("R") + "\t" + RenderSettings.ambientLight.ToString("R"));
            Material sky = RenderSettings.skybox;
            if (sky != null) {
                writer.WriteLine("skybox\t" + sky.name + "\t" + sky.shader.name);
                foreach (string key in new[] { "_GradientSkyUpperColor", "_GradientSkyMiddleColor", "_GradientSkyLowerColor", "_Tint", "_SkyTint", "_GroundColor" })
                    if (sky.HasProperty(key)) writer.WriteLine("sky_color\t" + key + "\t" + sky.GetColor(key).ToString("R"));
            }
            var ambient = RenderSettings.ambientProbe;
            for (int c = 0; c < 3; ++c) for (int k = 0; k < 9; ++k)
                writer.WriteLine("ambient_sh\t" + c + "\t" + k + "\t" + ambient[c, k].ToString("R"));
            foreach (Camera camera in Camera.allCameras) {
                writer.WriteLine("camera\t" + PathOf(camera.transform) + "\t" + camera.enabled + "\t" + camera.transform.position.ToString("R") + "\t" + camera.transform.rotation.ToString("R") + "\t" + camera.fieldOfView.ToString("R") + "\t" + camera.backgroundColor.ToString("R") + "\t" + camera.clearFlags);
                var dof = camera.GetComponent<UnityStandardAssets.ImageEffects.DepthOfField>();
                if (dof != null) writer.WriteLine("dof\t" + camera.name + "\t" + dof.enabled + "\t" + dof.aperture.ToString("R") + "\t" + dof.focalSize.ToString("R") + "\t" + dof.focalLength.ToString("R") + "\t" + (dof.focalTransform != null ? PathOf(dof.focalTransform) : "null"));
            }
            foreach (Renderer renderer in UnityEngine.Object.FindObjectsOfType<Renderer>()) {
                if (!renderer.enabled || !renderer.gameObject.activeInHierarchy) continue;
                string rendererPath = PathOf(renderer.transform);
                foreach (Material material in renderer.sharedMaterials) {
                    if (material == null) continue;
                    string prefix = rendererPath + "\t" + material.name;
                    writer.WriteLine("material\t" + prefix + "\t" + material.shader.name + "\t" + String.Join(",", material.shaderKeywords));
                    writer.WriteLine("material_shader_id\t" + prefix + "\t" + material.shader.GetInstanceID());
                    if (material.shader.name.IndexOf("hair", StringComparison.OrdinalIgnoreCase) >= 0) {
                        UnityEngine.Rendering.SphericalHarmonicsL2 sh;
                        LightProbes.GetInterpolatedProbe(renderer.bounds.center, renderer, out sh);
                        for (int c = 0; c < 3; ++c) for (int k = 0; k < 9; ++k)
                            writer.WriteLine("renderer_sh\t" + prefix + "\t" + c + "\t" + k + "\t" + sh[c, k].ToString("R"));
                    }
                    foreach (string key in new[] { "_Color", "_Color2", "_Color3", "_Color4", "_GlossColor", "_ShadowColor", "_LineColor", "_SpecularColor" })
                        if (material.HasProperty(key)) writer.WriteLine("color\t" + prefix + "\t" + key + "\t" + material.GetColor(key).ToString("R"));
                    foreach (string key in new[] { "_oldhair", "_GlossinShadowonoff", "_SpeclarHeight", "_ShadowExtend", "_rimpower", "_rimV" })
                        if (material.HasProperty(key)) writer.WriteLine("float\t" + prefix + "\t" + key + "\t" + material.GetFloat(key).ToString("R"));
                    foreach (string key in new[] { "_texcoord", "_texcoord2", "_MainTex", "_ColorMask", "_HairGloss", "_DetailMask", "_NormalMap" }) {
                        if (!material.HasProperty(key)) continue;
                        Texture texture = material.GetTexture(key);
                        writer.WriteLine("texture\t" + prefix + "\t" + key + "\t" + (texture == null ? "null" : texture.name + ":" + texture.width + ":" + texture.height) + "\t" + material.GetTextureScale(key).ToString("R") + "\t" + material.GetTextureOffset(key).ToString("R"));
                    }
                }
            }
        }
        Camera main = Camera.main;
        string gpuProbe = Environment.GetEnvironmentVariable("OK_PROBE_GPU_LIBRARY");
        if (!String.IsNullOrEmpty(gpuProbe) && main != null) {
            IntPtr module = LoadShaderProbeLibrary(gpuProbe);
            if (module == IntPtr.Zero) throw new InvalidOperationException("Cannot load GPU shader probe: " + gpuProbe);
            var configure = (ConfigureShaderProbe)Marshal.GetDelegateForFunctionPointer(ShaderProbeProc(module, "ok_shader_probe_configure"), typeof(ConfigureShaderProbe));
            var getEvent = (ShaderProbeEvent)Marshal.GetDelegateForFunctionPointer(ShaderProbeProc(module, "ok_shader_probe_render_event"), typeof(ShaderProbeEvent));
            string meshName = Environment.GetEnvironmentVariable("OK_PROBE_GPU_MESH");
            Renderer selected = null;
            foreach (var renderer in UnityEngine.Object.FindObjectsOfType<Renderer>())
                if (renderer.name == meshName && renderer.enabled && renderer.gameObject.activeInHierarchy) { selected = renderer; break; }
            if (selected == null) throw new InvalidOperationException("GPU probe renderer missing: " + meshName);
            var target = RenderTexture.GetTemporary(1280, 720, 24, RenderTextureFormat.ARGBFloat, RenderTextureReadWrite.Linear);
            var commands = new UnityEngine.Rendering.CommandBuffer();
            try {
                target.Create();
                byte[] bytes = System.Text.Encoding.UTF8.GetBytes(output + "\0");
                IntPtr text = Marshal.AllocHGlobal(bytes.Length);
                try {
                    Marshal.Copy(bytes, 0, text, bytes.Length);
                    if (configure(target.GetNativeTexturePtr(), text) != 0) throw new InvalidOperationException("GPU probe configuration failed");
                } finally { Marshal.FreeHGlobal(text); }
                commands.SetRenderTarget(target);
                commands.ClearRenderTarget(true, true, Color.clear);
                commands.DrawRenderer(selected, selected.sharedMaterial, 0, 1);
                commands.IssuePluginEvent(getEvent(), 1);
                commands.SetRenderTarget(UnityEngine.Rendering.BuiltinRenderTextureType.CameraTarget);
                main.AddCommandBuffer(UnityEngine.Rendering.CameraEvent.AfterForwardOpaque, commands);
                main.Render();
                for (int frame = 0; frame < 120 && !File.Exists(output + ".gpu.json"); ++frame) yield return null;
                if (!File.Exists(output + ".gpu.json")) throw new InvalidOperationException("GPU shader probe callback timed out");
            } finally {
                main.RemoveCommandBuffer(UnityEngine.Rendering.CameraEvent.AfterForwardOpaque, commands);
                commands.Release();
                RenderTexture.ReleaseTemporary(target);
            }
        }
        if (main != null) {
            RenderTexture oldTarget = main.targetTexture;
            RenderTexture oldActive = RenderTexture.active;
            var target = RenderTexture.GetTemporary(1280, 720, 24, RenderTextureFormat.ARGB32, RenderTextureReadWrite.sRGB);
            var image = new Texture2D(1280, 720, TextureFormat.RGB24, false);
            try {
                main.targetTexture = target;
                main.Render();
                RenderTexture.active = target;
                image.ReadPixels(new Rect(0, 0, 1280, 720), 0, 0);
                image.Apply();
                File.WriteAllBytes(output + ".png", image.EncodeToPNG());
            } finally {
                main.targetTexture = oldTarget;
                RenderTexture.active = oldActive;
                RenderTexture.ReleaseTemporary(target);
                UnityEngine.Object.Destroy(image);
            }
        }
        File.WriteAllText(output, "PASS: native scene audit completed");
        finished = true;
        Application.Quit();
    }
}
