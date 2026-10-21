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
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int ConfigureProbeTexture(IntPtr texture, IntPtr output);

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
            writer.WriteLine("light_pipeline\t" + UnityEngine.Rendering.GraphicsSettings.lightsUseLinearIntensity + "\t" + QualitySettings.activeColorSpace);
            foreach (Light light in UnityEngine.Object.FindObjectsOfType<Light>()) {
                if (!light.enabled || !light.gameObject.activeInHierarchy) continue;
                writer.WriteLine("light\t" + PathOf(light.transform) + "\t" + light.type + "\t" + light.color.ToString("R") + "\t" + light.intensity.ToString("R") + "\t" + light.transform.rotation.ToString("R") + "\t" + light.cullingMask + "\t" + light.shadows + "\t" + light.shadowStrength.ToString("R"));
            }
            foreach(string key in new[]{"_RampG"}){
                Texture texture=Shader.GetGlobalTexture(key);var property=typeof(Texture).GetProperty("activeTextureColorSpace",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic);
                writer.WriteLine("global_texture\t"+key+"\t"+(texture==null?"null":texture.name+":"+texture.width+":"+texture.height+":"+texture.GetType().Name)+"\t"+(texture==null||property==null?"unknown":property.GetValue(texture,null).ToString()));
            }
            Material sky = RenderSettings.skybox;
            if (sky != null) {
                writer.WriteLine("skybox\t" + sky.name + "\t" + sky.shader.name);
                foreach (string key in new[] { "_GradientSkyUpperColor", "_GradientSkyMiddleColor", "_GradientSkyLowerColor", "_Tint", "_SkyTint", "_GroundColor" })
                    if (sky.HasProperty(key)) writer.WriteLine("sky_color\t" + key + "\t" + sky.GetColor(key).ToString("R"));
            }
            var ambient = RenderSettings.ambientProbe;
            Texture defaultProbe = ReflectionProbe.defaultTexture;
            writer.WriteLine("reflection_default\t" + (defaultProbe == null ? "null" : defaultProbe.name + ":" + defaultProbe.width + ":" + defaultProbe.height) + "\t" + ReflectionProbe.defaultTextureHDRDecodeValues.ToString("R"));
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
                writer.WriteLine("renderer_lighting\t" + rendererPath + "\t" + renderer.lightmapIndex + "\t" + renderer.lightProbeUsage + "\t" + renderer.bounds.center.ToString("R"));
                foreach (Material material in renderer.sharedMaterials) {
                    if (material == null) continue;
                    string prefix = rendererPath + "\t" + material.name;
                    writer.WriteLine("material\t" + prefix + "\t" + material.shader.name + "\t" + String.Join(",", material.shaderKeywords));
                    writer.WriteLine("material_shader_id\t" + prefix + "\t" + material.shader.GetInstanceID());
                    if (material.shader.name.IndexOf("hair", StringComparison.OrdinalIgnoreCase) >= 0 || renderer.name == Environment.GetEnvironmentVariable("OK_PROBE_GPU_MESH")) {
                        var closest = new System.Collections.Generic.List<UnityEngine.Rendering.ReflectionProbeBlendInfo>();
                        renderer.GetClosestReflectionProbes(closest);
                        writer.WriteLine("reflection_usage\t" + prefix + "\t" + renderer.reflectionProbeUsage + "\t" + closest.Count);
                        foreach (var entry in closest) {
                            var probe = entry.probe;
                            Texture cube = probe.bakedTexture;
                            writer.WriteLine("reflection_probe\t" + prefix + "\t" + PathOf(probe.transform) + "\t" + entry.weight.ToString("R") + "\t" + probe.intensity.ToString("R") + "\t" + (cube == null ? "null" : cube.name + ":" + cube.width + ":" + cube.height) + "\t" + probe.textureHDRDecodeValues.ToString("R"));
                        }
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
            var addTexture = (ConfigureProbeTexture)Marshal.GetDelegateForFunctionPointer(ShaderProbeProc(module, "ok_shader_probe_add_texture"), typeof(ConfigureProbeTexture));
            string meshName = Environment.GetEnvironmentVariable("OK_PROBE_GPU_MESH");
            Renderer selected = null;
            foreach (var renderer in UnityEngine.Object.FindObjectsOfType<Renderer>())
                if (renderer.name == meshName && renderer.enabled && renderer.gameObject.activeInHierarchy) { selected = renderer; break; }
            if (selected == null) throw new InvalidOperationException("GPU probe renderer missing: " + meshName);
            Vector3 savedPosition=main.transform.position; Quaternion savedRotation=main.transform.rotation;
            float savedNear=main.nearClipPlane;
            if (Environment.GetEnvironmentVariable("OK_PROBE_GPU_FRAME") == "1") {
                var bounds=selected.bounds;
                main.transform.position=bounds.center + new Vector3(0f,0.1f,-1f).normalized * Mathf.Max(bounds.extents.magnitude*3f,0.1f);
                main.transform.LookAt(bounds.center); main.nearClipPlane=0.001f;
            }
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
                var selectedProbes=new System.Collections.Generic.List<UnityEngine.Rendering.ReflectionProbeBlendInfo>();selected.GetClosestReflectionProbes(selectedProbes);
                for(int pi=0;pi<selectedProbes.Count;++pi){
                    var probe=selectedProbes[pi].probe;Texture texture=probe.bakedTexture;
                    if(texture==null)continue;
                    byte[] probeName=System.Text.Encoding.UTF8.GetBytes(output+".selected-probe"+pi+"\0");IntPtr probeText=Marshal.AllocHGlobal(probeName.Length);
                    try{Marshal.Copy(probeName,0,probeText,probeName.Length);if(addTexture(texture.GetNativeTexturePtr(),probeText)!=0)throw new InvalidOperationException("Probe texture capture failed");}finally{Marshal.FreeHGlobal(probeText);}
                }
                Texture globalRamp=Shader.GetGlobalTexture("_RampG");
                if(globalRamp!=null){byte[] rampName=System.Text.Encoding.UTF8.GetBytes(output+".global-ramp\0");IntPtr rampText=Marshal.AllocHGlobal(rampName.Length);try{Marshal.Copy(rampName,0,rampText,rampName.Length);if(addTexture(globalRamp.GetNativeTexturePtr(),rampText)!=0)throw new InvalidOperationException("Global ramp readback failed");}finally{Marshal.FreeHGlobal(rampText);}}
                Texture reflection = ReflectionProbe.defaultTexture;
                if (reflection != null) {
                    byte[] nameBytes = System.Text.Encoding.UTF8.GetBytes(output + ".default-reflection\0");
                    IntPtr nameText = Marshal.AllocHGlobal(nameBytes.Length);
                    try { Marshal.Copy(nameBytes, 0, nameText, nameBytes.Length); if (addTexture(reflection.GetNativeTexturePtr(), nameText) != 0) throw new InvalidOperationException("Reflection texture capture configuration failed"); }
                    finally { Marshal.FreeHGlobal(nameText); }
                }
                foreach (string key in new[] { "@target", "_MainTex", "_ColorMask", "_HairGloss", "_NormalMap", "_DetailMask", "_AnotherRamp" }) {
                    Texture texture = key == "@target" ? target : selected.sharedMaterial.HasProperty(key) ? selected.sharedMaterial.GetTexture(key) : null;
                    if (texture == null) continue;
                    byte[] nameBytes = System.Text.Encoding.UTF8.GetBytes(output + ".selected-" + key.Replace("@", "") + "\0");
                    IntPtr nameText = Marshal.AllocHGlobal(nameBytes.Length);
                    try { Marshal.Copy(nameBytes, 0, nameText, nameBytes.Length); if (addTexture(texture.GetNativeTexturePtr(), nameText) != 0) throw new InvalidOperationException("Selected texture capture configuration failed: " + key); }
                    finally { Marshal.FreeHGlobal(nameText); }
                }
                commands.SetRenderTarget(target);
                commands.ClearRenderTarget(true, true, Color.clear);
                string requestedPass = Environment.GetEnvironmentVariable("OK_PROBE_GPU_PASS");
                int pass = String.IsNullOrEmpty(requestedPass) ? 1 : Int32.Parse(requestedPass);
                commands.DrawRenderer(selected, selected.sharedMaterial, 0, pass);
                commands.IssuePluginEvent(getEvent(), 1);
                commands.SetRenderTarget(UnityEngine.Rendering.BuiltinRenderTextureType.CameraTarget);
                main.AddCommandBuffer(UnityEngine.Rendering.CameraEvent.AfterForwardOpaque, commands);
                main.Render();
                for (int frame = 0; frame < 120 && !File.Exists(output + ".gpu.json"); ++frame) yield return null;
                if (!File.Exists(output + ".gpu.json")) throw new InvalidOperationException("GPU shader probe callback timed out");
            } finally {
                main.RemoveCommandBuffer(UnityEngine.Rendering.CameraEvent.AfterForwardOpaque, commands);
                main.transform.position=savedPosition;main.transform.rotation=savedRotation;main.nearClipPlane=savedNear;
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
