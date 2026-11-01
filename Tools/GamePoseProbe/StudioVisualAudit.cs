// Read-only native Studio scene inputs, captured after Studio applies its saved settings.
using System;
using System.Collections;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using UnityEngine;

public sealed partial class GamePoseProbe {
    // The main camera rendered into an 8-bit sRGB target (what the screen shows), saved as PNG.
    private static void CaptureMain(Camera main, string file) {
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
            File.WriteAllBytes(file, image.EncodeToPNG());
        } finally {
            main.targetTexture = oldTarget;
            RenderTexture.active = oldActive;
            RenderTexture.ReleaseTemporary(target);
            UnityEngine.Object.Destroy(image);
        }
    }

    // Exercise the installed post shader with known linear-light inputs. This is a
    // numeric oracle, independent of scene geometry, camera pose and screenshots.
    private static void WriteStudioGradeSamples(AmplifyColorBase grade, string output) {
        const int size = 16, width = size * size, height = size;
        var input = new Texture2D(width, height, TextureFormat.RGBAFloat, false, true);
        var source = RenderTexture.GetTemporary(width, height, 0, RenderTextureFormat.ARGBFloat, RenderTextureReadWrite.Linear);
        var target = RenderTexture.GetTemporary(width, height, 0, RenderTextureFormat.ARGBFloat, RenderTextureReadWrite.Linear);
        var readback = new Texture2D(width, height, TextureFormat.RGBAFloat, false, true);
        var active = RenderTexture.active; bool write = GL.sRGBWrite;
        try {
            var colors = new Color[width * height];
            for (int g = 0; g < size; ++g) for (int b = 0; b < size; ++b) for (int r = 0; r < size; ++r)
                colors[g * width + b * size + r] = new Color(r / 15f, g / 15f, b / 15f, 1).linear;
            input.SetPixels(colors); input.Apply(); GL.sRGBWrite = false;
            Graphics.Blit(input, source);
            typeof(AmplifyColorBase).GetMethod("OnRenderImage", BindingFlags.Instance | BindingFlags.NonPublic).Invoke(grade, new object[] { source, target });
            RenderTexture.active = target; readback.ReadPixels(new Rect(0, 0, width, height), 0, 0); readback.Apply();
            File.WriteAllBytes(output + ".grade-linear-input.rgba32f", input.GetRawTextureData());
            File.WriteAllBytes(output + ".grade-linear-output.rgba32f", readback.GetRawTextureData());
        } finally {
            RenderTexture.active = active; GL.sRGBWrite = write;
            RenderTexture.ReleaseTemporary(source); RenderTexture.ReleaseTemporary(target);
            UnityEngine.Object.Destroy(input); UnityEngine.Object.Destroy(readback);
        }
    }
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

    private static void WriteStudioSelectedMesh(Renderer renderer, string output) {
        var skin=renderer as SkinnedMeshRenderer;
        var filter=renderer.GetComponent<MeshFilter>();
        Mesh source=skin!=null?skin.sharedMesh:filter!=null?filter.sharedMesh:null;
        if(source==null)throw new InvalidOperationException("Selected renderer has no mesh: "+renderer.name);
        Mesh baked=source;
        try {
            if(skin!=null){baked=new Mesh();skin.BakeMesh(baked);}
            var world=baked.vertices;
            for(int i=0;i<world.Length;i++)world[i]=renderer.transform.TransformPoint(world[i]);
            var palette=new System.Collections.Generic.List<Bone>();
            if(skin!=null)foreach(var bone in skin.bones)if(bone!=null)palette.Add(PVTransform(bone));
            var snapshot=new RendererMeshSnapshot {
                path=PathOf(renderer.transform),name=source.name,vertices=source.vertices,
                worldVertices=world,rendererWorld=renderer.transform.localToWorldMatrix,
                quality=skin==null?"rigid":skin.quality.ToString(),bindposes=source.bindposes,
                weights=source.boneWeights,bones=palette.ToArray(),
                boneWorlds=skin==null?new Matrix4x4[0]:Array.ConvertAll(skin.bones,delegate(Transform bone){return bone==null?Matrix4x4.identity:bone.localToWorldMatrix;}),
                normals=source.normals,bakedNormals=baked.normals,tangents=source.tangents,
                colors=source.colors,colors32=source.colors32,
                uv0=source.uv,uv1=source.uv2,uv2=source.uv3,uv3=source.uv4,
                blendShapeNames=new string[source.blendShapeCount],blendShapeWeights=new float[source.blendShapeCount],
                blendShapeNormalCounts=new int[source.blendShapeCount],blendShapeNormalMax=new float[source.blendShapeCount]
            };
            for(int i=0;i<source.blendShapeCount;i++){
                snapshot.blendShapeNames[i]=source.GetBlendShapeName(i);
                snapshot.blendShapeWeights[i]=skin==null?0:skin.GetBlendShapeWeight(i);
                for(int frame=0;frame<source.GetBlendShapeFrameCount(i);frame++){
                    var positions=new Vector3[source.vertexCount];var normals=new Vector3[source.vertexCount];var tangents=new Vector3[source.vertexCount];
                    source.GetBlendShapeFrameVertices(i,frame,positions,normals,tangents);
                    for(int vertex=0;vertex<normals.Length;vertex++)if(normals[vertex].sqrMagnitude>0){
                        snapshot.blendShapeNormalCounts[i]++;
                        snapshot.blendShapeNormalMax[i]=Mathf.Max(snapshot.blendShapeNormalMax[i],normals[vertex].magnitude);
                    }
                }
            }
            var cha=renderer.GetComponentInParent<ChaControl>();
            if(cha!=null && cha.fbsCtrl!=null && cha.eyeLookCtrl!=null){
                var calc=cha.eyeLookCtrl.eyeLookScript;
                snapshot.eyeLookRates=new[]{calc.GetAngleHRate(EYE_LR.EYE_L),calc.GetAngleHRate(EYE_LR.EYE_R),calc.GetAngleVRate()};
                snapshot.eyesOpenMax=cha.fileStatus.eyesOpenMax;snapshot.eyesBlink=cha.fileStatus.eyesBlink;
            }
            File.WriteAllText(output+".selected-mesh.json",ProbeJson(snapshot));
        }finally{if(skin!=null)UnityEngine.Object.Destroy(baked);}
    }

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
        // Studio's FK, face updates and attachment/dynamic-bone followers finish
        // in LateUpdate. A manual Camera.Render from a null-yield continuation
        // can observe the Animator's intermediate pose before those followers.
        yield return new WaitForEndOfFrame();
        using (var writer = new StreamWriter(output + ".scene.tsv")) {
            writer.WriteLine("scene\t" + scene);
            writer.WriteLine("capture_phase\tend_of_frame\trunning_animation");
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
            foreach (string key in new[] { "_ambientshadowG", "_LineColorG", "_LightColor0", "_WorldSpaceLightPos0", "unity_Lightmap_HDR", "unity_DynamicLightmap_HDR", "unity_SpecCube0_HDR", "unity_ColorSpaceDouble", "unity_ColorSpaceDielectricSpec", "unity_ColorSpaceLuminance", "glstate_lightmodel_ambient" })
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
            writer.WriteLine("lightmaps_mode\t" + LightmapSettings.lightmapsMode);
            // The legacy ambient (UNITY_LIGHTMODEL_AMBIENT / glstate_lightmodel_ambient comes from these).
            writer.WriteLine("render_ambient\t" + RenderSettings.ambientMode + "\t" + RenderSettings.ambientLight.ToString("R") + "\t" + RenderSettings.ambientSkyColor.ToString("R")
                + "\t" + RenderSettings.ambientEquatorColor.ToString("R") + "\t" + RenderSettings.ambientGroundColor.ToString("R") + "\t" + RenderSettings.ambientIntensity.ToString("R"));
            // Built-in forward fog (applied inside each shader, not a post effect).
            writer.WriteLine("render_fog\t" + RenderSettings.fog + "\t" + RenderSettings.fogMode + "\t" + RenderSettings.fogColor.ToString("R") + "\t"
                + RenderSettings.fogDensity.ToString("R") + "\t" + RenderSettings.fogStartDistance.ToString("R") + "\t" + RenderSettings.fogEndDistance.ToString("R"));
            for (int lm = 0; lm < LightmapSettings.lightmaps.Length; ++lm) {
                var data = LightmapSettings.lightmaps[lm];
                // OK_PROBE_LIGHTMAPS=1: each lightmap texture as the GPU samples it (sRGB colour decoded to linear,
                // RGBM alpha as stored), blitted into a float target: <output>.lightmap<i>.<color|direction>.rgba32f.
                if (Environment.GetEnvironmentVariable("OK_PROBE_LIGHTMAPS") == "1")
                    foreach (var slot in new[] { data.lightmapColor, data.lightmapDir }) {
                        if (slot == null) continue;
                        var rt = RenderTexture.GetTemporary(slot.width, slot.height, 0, RenderTextureFormat.ARGBFloat, RenderTextureReadWrite.Linear);
                        var read = new Texture2D(slot.width, slot.height, TextureFormat.RGBAFloat, false, true);
                        var previous = RenderTexture.active;
                        try {
                            Graphics.Blit(slot, rt);
                            RenderTexture.active = rt;
                            read.ReadPixels(new Rect(0, 0, slot.width, slot.height), 0, 0);
                            read.Apply();
                            File.WriteAllBytes(output + ".lightmap" + lm + "." + (slot == data.lightmapColor ? "color" : "direction") + ".rgba32f", read.GetRawTextureData());
                        } finally {
                            RenderTexture.active = previous;
                            RenderTexture.ReleaseTemporary(rt);
                            UnityEngine.Object.Destroy(read);
                        }
                    }
                foreach (var slot in new[] { data.lightmapColor, data.lightmapDir }) {
                    if (slot == null) continue;
                    var colorSpaceProperty = typeof(Texture).GetProperty("activeTextureColorSpace", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
                    writer.WriteLine("lightmap_texture\t" + lm + "\t" + (slot == data.lightmapColor ? "color" : "direction") + "\t" + slot.name + "\t" + slot.format + "\t" + slot.width + "\t" + slot.height + "\t" + (colorSpaceProperty == null ? "unknown" : colorSpaceProperty.GetValue(slot, null).ToString())
                        + "\tmips=" + slot.mipmapCount + "\tfilter=" + slot.filterMode + "\taniso=" + slot.anisoLevel + "\tbias=" + slot.mipMapBias.ToString("R") + "\twrap=" + slot.wrapMode);
                }
            }
            Texture defaultProbe = ReflectionProbe.defaultTexture;
            writer.WriteLine("reflection_default\t" + (defaultProbe == null ? "null" : defaultProbe.name + ":" + defaultProbe.width + ":" + defaultProbe.height) + "\t" + ReflectionProbe.defaultTextureHDRDecodeValues.ToString("R"));
            for (int c = 0; c < 3; ++c) for (int k = 0; k < 9; ++k)
                writer.WriteLine("ambient_sh\t" + c + "\t" + k + "\t" + ambient[c, k].ToString("R"));
            foreach (Camera camera in Camera.allCameras) {
                var grade = camera.GetComponent<AmplifyColorBase>();
                if (grade != null) {
                    writer.WriteLine("color_grading\t" + PathOf(camera.transform) + "\t" + grade.enabled + "\t" + camera.allowHDR + "\t" + grade.Exposure.ToString("R") + "\t" + grade.Tonemapper + "\t" + grade.BlendAmount.ToString("R") + "\t" + (grade.LutTexture == null ? "null" : grade.LutTexture.name) + "\t" + (grade.LutBlendTexture == null ? "null" : grade.LutBlendTexture.name));
                    if (grade.enabled) WriteStudioGradeSamples(grade, output);
                }
                writer.WriteLine("camera\t" + PathOf(camera.transform) + "\t" + camera.enabled + "\t" + camera.transform.position.ToString("R") + "\t" + camera.transform.rotation.ToString("R") + "\t" + camera.fieldOfView.ToString("R") + "\t" + camera.backgroundColor.ToString("R") + "\t" + camera.clearFlags);
                var dof = camera.GetComponent<UnityStandardAssets.ImageEffects.DepthOfField>();
                if (dof != null) writer.WriteLine("dof\t" + camera.name + "\t" + dof.enabled + "\t" + dof.aperture.ToString("R") + "\t" + dof.focalSize.ToString("R") + "\t" + dof.focalLength.ToString("R") + "\t" + (dof.focalTransform != null ? PathOf(dof.focalTransform) : "null"));
            }
            foreach (Renderer renderer in UnityEngine.Object.FindObjectsOfType<Renderer>()) {
                if (!renderer.enabled || !renderer.gameObject.activeInHierarchy) continue;
                string rendererPath = PathOf(renderer.transform);
                // OK_PROBE_MESH=<path part>: that renderer's mesh vertices, uv0 and triangles (mesh_vertex / mesh_triangle rows).
                {
                    string meshPart = Environment.GetEnvironmentVariable("OK_PROBE_MESH");
                    var filter = renderer.GetComponent<MeshFilter>();
                    Mesh mesh = filter != null ? filter.sharedMesh : renderer is SkinnedMeshRenderer ? ((SkinnedMeshRenderer)renderer).sharedMesh : null;
                    if (!String.IsNullOrEmpty(meshPart) && rendererPath.Contains(meshPart)) {
                        writer.WriteLine("mesh_info\t" + rendererPath + "\t" + renderer.GetType().Name + "\t" + (mesh == null ? "null" : mesh.name + "\treadable=" + mesh.isReadable + "\tbounds=" + mesh.bounds.ToString("R")));
                        // The local transform chain up to the scene root (child first).
                        for (Transform t = renderer.transform; t != null; t = t.parent)
                            writer.WriteLine("mesh_node\t" + rendererPath + "\t" + t.name + "\t" + t.localPosition.ToString("R") + "\t" + t.localRotation.ToString("R") + "\t" + t.localScale.ToString("R"));
                    }
                    if (!String.IsNullOrEmpty(meshPart) && rendererPath.Contains(meshPart) && mesh != null && mesh.isReadable) {
                        Vector3[] verts = mesh.vertices; Vector2[] uvs = mesh.uv; int[] tris = mesh.triangles;
                        writer.WriteLine("mesh\t" + rendererPath + "\t" + mesh.name + "\t" + verts.Length + "\t" + tris.Length / 3 + "\t" + renderer.transform.localToWorldMatrix.ToString("R").Replace("\n", " "));
                        for (int v = 0; v < verts.Length && v < 2000; ++v)
                            writer.WriteLine("mesh_vertex\t" + v + "\t" + verts[v].ToString("R") + "\t" + (v < uvs.Length ? uvs[v].ToString("R") : "-"));
                        for (int t = 0; t + 2 < tris.Length && t < 6000; t += 3)
                            writer.WriteLine("mesh_triangle\t" + tris[t] + "\t" + tris[t + 1] + "\t" + tris[t + 2]);
                    }
                }
                writer.WriteLine("renderer_lighting\t" + rendererPath + "\t" + renderer.lightmapIndex + "\t" + renderer.lightProbeUsage + "\t" + renderer.bounds.center.ToString("R") + "\t" + renderer.lightmapScaleOffset.ToString("R") + "\t" + renderer.realtimeLightmapIndex + "\t" +renderer.realtimeLightmapScaleOffset.ToString("R") + "	" + renderer.gameObject.layer);
                // The reflection probes Unity blends for this renderer (none: the default/skybox reflection).
                {
                    var blend = new System.Collections.Generic.List<UnityEngine.Rendering.ReflectionProbeBlendInfo>();
                    renderer.GetClosestReflectionProbes(blend);
                    var names = new System.Collections.Generic.List<string>();
                    foreach (var entry in blend) names.Add(PathOf(entry.probe.transform) + "@" + entry.weight.ToString("R"));
                    writer.WriteLine("renderer_reflection\t" + rendererPath + "\t" + renderer.reflectionProbeUsage + "\t" + renderer.bounds.min.ToString("R") + "\t" + renderer.bounds.max.ToString("R") + "\t" + String.Join(";", names.ToArray()));
                }
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
                    foreach (string key in new[] { "_Color", "_Color2", "_Color3", "_Color4", "_GlossColor", "_ShadowColor", "_LineColor", "_SpecularColor", "_EmissionColor", "_Tint" })
                        if (material.HasProperty(key)) writer.WriteLine("color\t" + prefix + "\t" + key + "\t" + material.GetColor(key).ToString("R"));
                    foreach (string key in new[] { "_oldhair", "_GlossinShadowonoff", "_SpeclarHeight", "_ShadowExtend", "_rimpower", "_rimV", "_Glossiness", "_GlossMapScale", "_Metallic", "_OcclusionStrength", "_BumpScale", "_Mode", "_SmoothnessTextureChannel", "_Cutoff", "_AlphaCutoff", "_Exposure", "_Rotation", "_LightCancel", "_EmissionPower", "_Specular" })
                        if (material.HasProperty(key)) writer.WriteLine("float\t" + prefix + "\t" + key + "\t" + material.GetFloat(key).ToString("R"));
                    foreach (string key in new[] { "_texcoord", "_texcoord2", "_MainTex", "_ColorMask", "_HairGloss", "_DetailMask", "_NormalMap", "_EmissionMap", "_OcclusionMap", "_MetallicGlossMap", "_DetailAlbedoMap", "_BumpMap", "_Tex" }) {
                        if (!material.HasProperty(key)) continue;
                        Texture texture = material.GetTexture(key);
                        if (texture != null) { var cs = typeof(Texture).GetProperty("activeTextureColorSpace", BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic); var rt = texture as RenderTexture; writer.WriteLine("texture_space\t"+prefix+"\t"+key+"\t"+(cs==null?"unknown":cs.GetValue(texture,null).ToString())+"\trt_srgb="+(rt==null?"n/a":rt.sRGB.ToString())); }
                        writer.WriteLine("texture\t" + prefix + "\t" + key + "\t" + (texture == null ? "null" : texture.name + ":" + texture.width + ":" + texture.height) + "\t" + material.GetTextureScale(key).ToString("R") + "\t" + material.GetTextureOffset(key).ToString("R"));
                    }
                    // OK_PROBE_MATERIAL_PROPS=<parts of shader names separated by |, or all>: every property those materials
                    // have, plus their render queue (the lists above only name the common ones).
                    string allProps = Environment.GetEnvironmentVariable("OK_PROBE_MATERIAL_PROPS");
                    if (!String.IsNullOrEmpty(allProps) && (allProps == "all" || Array.Exists(allProps.Split('|'), w => w.Length > 0 && material.shader.name.IndexOf(w, StringComparison.OrdinalIgnoreCase) >= 0))) {
                        Shader shader = material.shader;
                        writer.WriteLine("prop\t" + prefix + "\t(renderQueue)\tQueue\t" + material.renderQueue);
                        // What decides whether it is in the camera depth texture: its passes, RenderType and shadow casting.
                        var passes = new System.Collections.Generic.List<string>();
                        for (int p = 0; p < material.passCount; ++p) passes.Add(material.GetPassName(p));
                        writer.WriteLine("prop\t" + prefix + "\t(passes)\tPasses\t" + String.Join(",", passes.ToArray()) + "\tRenderType=" + material.GetTag("RenderType", true) + "\tcast=" + renderer.shadowCastingMode);
                        for (int p = 0; p < shader.GetPropertyCount(); ++p) {
                            string name = shader.GetPropertyName(p);
                            var type = shader.GetPropertyType(p);
                            string value;
                            switch (type) {
                                case UnityEngine.Rendering.ShaderPropertyType.Color: value = material.GetColor(name).ToString("R"); break;
                                case UnityEngine.Rendering.ShaderPropertyType.Vector: value = material.GetVector(name).ToString("R"); break;
                                case UnityEngine.Rendering.ShaderPropertyType.Texture: {
                                    Texture t = material.GetTexture(name);
                                    value = (t == null ? "null" : t.name + ":" + t.width + ":" + t.height + ":" + t.GetType().Name) + "\t" + material.GetTextureScale(name).ToString("R") + "\t" + material.GetTextureOffset(name).ToString("R");
                                    break;
                                }
                                default: value = material.GetFloat(name).ToString("R"); break;
                            }
                            writer.WriteLine("prop\t" + prefix + "\t" + name + "\t" + type + "\t" + value);
                        }
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
            if (Environment.GetEnvironmentVariable("OK_PROBE_GPU_MESH_SNAPSHOT") == "1") WriteStudioSelectedMesh(selected,output);
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
            yield return new WaitForEndOfFrame();
            // The main camera's components in execution order (OnRenderImage effects run in this order) and the
            // public fields of each image effect, so the post chain can be rebuilt in the same order.
            var effects = new System.Collections.Generic.List<Behaviour>();
            using (var writer = new StreamWriter(output + ".post.tsv")) {
                int order = 0;
                foreach (Component component in main.GetComponents<Component>()) {
                    var behaviour = component as Behaviour;
                    Type type = component.GetType();
                    // Amplify Color / Occlusion declare OnRenderImage (or their command buffers) on a base class:
                    // private methods are not inherited by GetMethod, so walk the hierarchy.
                    bool imageEffect = false;
                    for (Type t = type; t != null && t != typeof(MonoBehaviour) && !imageEffect; t = t.BaseType)
                        imageEffect = t.GetMethod("OnRenderImage", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly) != null
                            || t.Name.StartsWith("AmplifyOcclusion");
                    writer.WriteLine("component\t" + order++ + "\t" + type.FullName + "\t" + (behaviour == null ? "n/a" : behaviour.enabled.ToString()) + "\t" + imageEffect);
                    if (!imageEffect || behaviour == null) continue;
                    effects.Add(behaviour);
                    foreach (FieldInfo field in type.GetFields(BindingFlags.Instance | BindingFlags.Public)) {
                        object value = field.GetValue(component);
                        if (value == null || value is UnityEngine.Object) continue;
                        writer.WriteLine("effect_field\t" + type.Name + "\t" + field.Name + "\t" + (value is float ? ((float)value).ToString("R") : value.ToString()));
                    }
                }
            }
            CaptureMain(main, output + ".png");
            // OK_PROBE_NORMALS=1: the main camera's view-space normals (Unity's DepthNormals replacement shader, its
            // stereographic encoding kept in a float target) as <output>.normals.rgba32f, and the camera-to-world
            // matrix as <output>.camera.tsv (row-major), to compare surface normals pixel by pixel.
            if (Environment.GetEnvironmentVariable("OK_PROBE_NORMALS") == "1") {
                Shader replacement = Shader.Find("Hidden/Internal-DepthNormalsTexture");
                if (replacement != null) {
                    RenderTexture oldTarget = main.targetTexture, oldActive = RenderTexture.active;
                    CameraClearFlags oldFlags = main.clearFlags; Color oldBack = main.backgroundColor;
                    var target = RenderTexture.GetTemporary(1280, 720, 24, RenderTextureFormat.ARGBFloat, RenderTextureReadWrite.Linear);
                    var read = new Texture2D(1280, 720, TextureFormat.RGBAFloat, false, true);
                    // The image effects (OnRenderImage) would run on the replacement render too: all off meanwhile.
                    var effectsOn = effects.FindAll(e => e.enabled);
                    foreach (Behaviour e in effectsOn) e.enabled = false;
                    try {
                        main.targetTexture = target; main.clearFlags = CameraClearFlags.SolidColor; main.backgroundColor = new Color(0.5f, 0.5f, 1f, 1f);
                        main.RenderWithShader(replacement, "RenderType");
                        RenderTexture.active = target; read.ReadPixels(new Rect(0, 0, 1280, 720), 0, 0); read.Apply();
                        File.WriteAllBytes(output + ".normals.rgba32f", read.GetRawTextureData());
                        Matrix4x4 m = main.cameraToWorldMatrix;
                        var rows = new System.Text.StringBuilder();
                        for (int r = 0; r < 4; ++r) rows.AppendLine(string.Format(System.Globalization.CultureInfo.InvariantCulture, "{0}\t{1}\t{2}\t{3}", m[r, 0], m[r, 1], m[r, 2], m[r, 3]));
                        File.WriteAllText(output + ".camera.tsv", rows.ToString());
                    } finally {
                        foreach (Behaviour e in effectsOn) e.enabled = true;
                        main.targetTexture = oldTarget; RenderTexture.active = oldActive; main.clearFlags = oldFlags; main.backgroundColor = oldBack;
                        RenderTexture.ReleaseTemporary(target); UnityEngine.Object.Destroy(read);
                    }
                }
            }
            // OK_PROBE_SHADOW_MASK=1: the shadowing directional light's screen-space shadow mask (what SHADOWS_SCREEN
            // shaders sample, collected from the camera depth texture) as <output>.shadow-mask.png, and that depth texture
            // (raw device depth) as <output>.depth.r32f, from one render of the main camera.
            if (Environment.GetEnvironmentVariable("OK_PROBE_SHADOW_MASK") == "1") {
                Light shadowLight = null;
                foreach (Light l in UnityEngine.Object.FindObjectsOfType<Light>())
                    if (l.type == LightType.Directional && l.isActiveAndEnabled && l.shadows != LightShadows.None && (l.cullingMask & main.cullingMask) != 0) { shadowLight = l; break; }
                RenderTexture oldTarget = main.targetTexture, oldActive = RenderTexture.active;
                DepthTextureMode oldMode = main.depthTextureMode;
                var mask = RenderTexture.GetTemporary(1280, 720, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
                var depth = RenderTexture.GetTemporary(1280, 720, 0, RenderTextureFormat.RFloat, RenderTextureReadWrite.Linear);
                var target = RenderTexture.GetTemporary(1280, 720, 24, RenderTextureFormat.ARGB32, RenderTextureReadWrite.sRGB);
                var maskRead = new Texture2D(1280, 720, TextureFormat.RGBA32, false, true);
                var depthRead = new Texture2D(1280, 720, TextureFormat.RFloat, false, true);
                var maskCommands = new UnityEngine.Rendering.CommandBuffer { name = "OK shadow mask" };
                maskCommands.Blit(UnityEngine.Rendering.BuiltinRenderTextureType.CurrentActive, mask);
                var depthCommands = new UnityEngine.Rendering.CommandBuffer { name = "OK depth texture" };
                depthCommands.Blit(new UnityEngine.Rendering.RenderTargetIdentifier("_CameraDepthTexture"), depth);
                try {
                    RenderTexture.active = mask; GL.Clear(false, true, new Color(0.5f, 0.5f, 0.5f, 1f));
                    if (shadowLight != null) shadowLight.AddCommandBuffer(UnityEngine.Rendering.LightEvent.AfterScreenspaceMask, maskCommands);
                    main.depthTextureMode |= DepthTextureMode.Depth;
                    main.AddCommandBuffer(UnityEngine.Rendering.CameraEvent.BeforeForwardAlpha, depthCommands);
                    main.targetTexture = target;
                    main.Render();
                    RenderTexture.active = mask; maskRead.ReadPixels(new Rect(0, 0, 1280, 720), 0, 0); maskRead.Apply();
                    File.WriteAllBytes(output + ".shadow-mask.png", maskRead.EncodeToPNG());
                    RenderTexture.active = depth; depthRead.ReadPixels(new Rect(0, 0, 1280, 720), 0, 0); depthRead.Apply();
                    File.WriteAllBytes(output + ".depth.r32f", depthRead.GetRawTextureData());
                    // The camera's depth texture itself (the global _CameraDepthTexture the render left bound), sampled as
                    // a texture: raw device depth in r (reversed Z on D3D11: 1 near, 0 far). The command-buffer copy above
                    // names the texture by id and can resolve to another target.
                    Texture global = Shader.GetGlobalTexture("_CameraDepthTexture");
                    string globalInfo = global == null ? "none" : global.name + ":" + global.width + ":" + global.height + ":" + ((global as RenderTexture) == null ? global.GetType().Name : (global as RenderTexture).format.ToString());
                    if (global != null) {
                        Graphics.Blit(global, depth);
                        RenderTexture.active = depth; depthRead.ReadPixels(new Rect(0, 0, 1280, 720), 0, 0); depthRead.Apply();
                        File.WriteAllBytes(output + ".depth-global.r32f", depthRead.GetRawTextureData());
                    }
                    File.WriteAllText(output + ".depth-global.txt", "texture\t" + globalInfo + "\nreversed_z\t" + SystemInfo.usesReversedZBuffer + "\n");
                    File.WriteAllText(output + ".shadow-mask.txt", "light\t" + (shadowLight == null ? "none" : PathOf(shadowLight.transform) + "\t" + shadowLight.shadows + "\t" + shadowLight.shadowStrength.ToString("R"))
                        + "\nshadow_distance\t" + QualitySettings.shadowDistance.ToString("R") + "\ncascades\t" + QualitySettings.shadowCascades
                        + "\nnear\t" + main.nearClipPlane.ToString("R") + "\nfar\t" + main.farClipPlane.ToString("R") + "\n");
                } finally {
                    if (shadowLight != null) shadowLight.RemoveCommandBuffer(UnityEngine.Rendering.LightEvent.AfterScreenspaceMask, maskCommands);
                    main.RemoveCommandBuffer(UnityEngine.Rendering.CameraEvent.BeforeForwardAlpha, depthCommands);
                    main.depthTextureMode = oldMode; main.targetTexture = oldTarget; RenderTexture.active = oldActive;
                    RenderTexture.ReleaseTemporary(mask); RenderTexture.ReleaseTemporary(depth); RenderTexture.ReleaseTemporary(target);
                    UnityEngine.Object.Destroy(maskRead); UnityEngine.Object.Destroy(depthRead);
                    maskCommands.Release(); depthCommands.Release();
                }
                // OK_PROBE_DEPTH_HIDE=<path part>[,...]: the screen camera's depth texture again with those renderers
                // hidden for two frames (is a surface in the depth texture, and what lies behind it), as
                // <output>.depth-global-hidden.r32f.
                string depthHide = Environment.GetEnvironmentVariable("OK_PROBE_DEPTH_HIDE");
                if (!String.IsNullOrEmpty(depthHide)) {
                    var hidden = new System.Collections.Generic.List<Renderer>();
                    foreach (Renderer renderer in UnityEngine.Object.FindObjectsOfType<Renderer>())
                        if (renderer.enabled && Array.Exists(depthHide.Split(','), p => PathOf(renderer.transform).Contains(p))) { renderer.enabled = false; hidden.Add(renderer); }
                    yield return new WaitForEndOfFrame();
                    yield return new WaitForEndOfFrame();
                    Texture global = Shader.GetGlobalTexture("_CameraDepthTexture");
                    if (global != null) {
                        var hiddenDepth = RenderTexture.GetTemporary(1280, 720, 0, RenderTextureFormat.RFloat, RenderTextureReadWrite.Linear);
                        var hiddenRead = new Texture2D(1280, 720, TextureFormat.RFloat, false, true);
                        RenderTexture wasActive = RenderTexture.active;
                        Graphics.Blit(global, hiddenDepth);
                        RenderTexture.active = hiddenDepth; hiddenRead.ReadPixels(new Rect(0, 0, 1280, 720), 0, 0); hiddenRead.Apply();
                        File.WriteAllBytes(output + ".depth-global-hidden.r32f", hiddenRead.GetRawTextureData());
                        RenderTexture.active = wasActive; RenderTexture.ReleaseTemporary(hiddenDepth); UnityEngine.Object.Destroy(hiddenRead);
                    }
                    File.WriteAllText(output + ".depth-global-hidden.txt", "hidden\t" + hidden.Count + "\n");
                    foreach (Renderer renderer in hidden) renderer.enabled = true;
                }
            }
            // OK_PROBE_CAPTURE_STAGES=1: the same frame with every enabled image effect off, then with each one off alone.
            if (Environment.GetEnvironmentVariable("OK_PROBE_CAPTURE_STAGES") == "1") {
                var enabled = effects.FindAll(e => e.enabled);
                foreach (Behaviour e in enabled) e.enabled = false;
                try {
                    CaptureMain(main, output + ".post-off.png");
                    // The same frame in a float target: the HDR values the image effects receive (bloom's bright pass).
                    RenderTexture oldTarget = main.targetTexture, oldActive = RenderTexture.active;
                    var hdr = RenderTexture.GetTemporary(1280, 720, 24, RenderTextureFormat.ARGBFloat, RenderTextureReadWrite.Linear);
                    var hdrRead = new Texture2D(1280, 720, TextureFormat.RGBAFloat, false, true);
                    try {
                        main.targetTexture = hdr; main.Render();
                        RenderTexture.active = hdr; hdrRead.ReadPixels(new Rect(0, 0, 1280, 720), 0, 0); hdrRead.Apply();
                        File.WriteAllBytes(output + ".post-off.hdr.rgba32f", hdrRead.GetRawTextureData());
                    } finally { main.targetTexture = oldTarget; RenderTexture.active = oldActive; RenderTexture.ReleaseTemporary(hdr); UnityEngine.Object.Destroy(hdrRead); }
                }
                finally { foreach (Behaviour e in enabled) e.enabled = true; }
                foreach (Behaviour e in enabled) {
                    e.enabled = false;
                    try { CaptureMain(main, output + ".no-" + e.GetType().Name + ".png"); }
                    finally { e.enabled = true; }
                }
                // Lighting stages, every image effect off: without the map's own lights (its sun), then without
                // Studio's character light, so baked/indirect light can be told from realtime light.
                foreach (Behaviour e in enabled) e.enabled = false;
                try {
                    foreach (string group in new[] { "Map/", "StudioScene/Light Chara/" }) {
                        var lights = new System.Collections.Generic.List<Light>();
                        foreach (Light light in UnityEngine.Object.FindObjectsOfType<Light>())
                            if (light.enabled && light.gameObject.activeInHierarchy && PathOf(light.transform).StartsWith(group)) lights.Add(light);
                        foreach (Light light in lights) light.enabled = false;
                        try { CaptureMain(main, output + ".post-off.no-" + (group.StartsWith("Map") ? "maplights" : "charalight") + ".png"); }
                        finally { foreach (Light light in lights) light.enabled = true; }
                    }
                    // OK_PROBE_HIDE=<path part>[,...]: those renderers hidden (which pixels they cover).
                    // OK_PROBE_WHITE=<path part>[,...]: their materials' _MainTex and _Color white (the light they receive).
                    foreach (string mode in new[] { "HIDE", "WHITE" }) {
                        string list = Environment.GetEnvironmentVariable("OK_PROBE_" + mode);
                        if (String.IsNullOrEmpty(list)) continue;
                        var parts = list.Split(',');
                        var restore = new System.Collections.Generic.List<Action>();
                        foreach (Renderer renderer in UnityEngine.Object.FindObjectsOfType<Renderer>()) {
                            if (!renderer.enabled || !renderer.gameObject.activeInHierarchy) continue;
                            string rendererPath = PathOf(renderer.transform);
                            if (!Array.Exists(parts, p => rendererPath.Contains(p))) continue;
                            Renderer target = renderer;
                            if (mode == "HIDE") { target.enabled = false; restore.Add(() => target.enabled = true); continue; }
                            Material[] saved = target.sharedMaterials;
                            var white = new Material[saved.Length];
                            for (int m = 0; m < saved.Length; ++m) {
                                if (saved[m] == null) continue;
                                white[m] = new Material(saved[m]);
                                if (white[m].HasProperty("_MainTex")) white[m].SetTexture("_MainTex", Texture2D.whiteTexture);
                                if (white[m].HasProperty("_Color")) white[m].SetColor("_Color", Color.white);
                            }
                            target.sharedMaterials = white;
                            restore.Add(() => { target.sharedMaterials = saved; foreach (Material m in white) if (m != null) UnityEngine.Object.Destroy(m); });
                        }
                        try {
                            CaptureMain(main, output + ".post-off." + mode.ToLowerInvariant() + ".png");
                            // The same without environment reflections (probes off, a black default reflection).
                            if (mode == "WHITE") {
                                var probes = new System.Collections.Generic.List<ReflectionProbe>();
                                foreach (ReflectionProbe probe in UnityEngine.Object.FindObjectsOfType<ReflectionProbe>()) if (probe.enabled) { probe.enabled = false; probes.Add(probe); }
                                var savedMode = RenderSettings.defaultReflectionMode; var savedCube = RenderSettings.customReflection;
                                var black = new Cubemap(4, TextureFormat.RGBA32, false);
                                foreach (CubemapFace face in new[] { CubemapFace.PositiveX, CubemapFace.NegativeX, CubemapFace.PositiveY, CubemapFace.NegativeY, CubemapFace.PositiveZ, CubemapFace.NegativeZ })
                                    black.SetPixels(new Color[16], face);
                                black.Apply();
                                RenderSettings.defaultReflectionMode = UnityEngine.Rendering.DefaultReflectionMode.Custom;
                                RenderSettings.customReflection = black;
                                try {
                                    CaptureMain(main, output + ".post-off.white-noreflect.png");
                                    // Also without the map's own lights: the baked light alone.
                                    var bakedOff = new System.Collections.Generic.List<Light>();
                                    foreach (Light light in UnityEngine.Object.FindObjectsOfType<Light>())
                                        if (light.enabled && light.gameObject.activeInHierarchy && PathOf(light.transform).StartsWith("Map/")) { light.enabled = false; bakedOff.Add(light); }
                                    try {
                                        CaptureMain(main, output + ".post-off.white-baked.png");
                                        // Black lightmap colours (still lightmapped): whatever remains is not from the lightmap.
                                        LightmapData[] savedMaps = LightmapSettings.lightmaps;
                                        var blackMap = new Texture2D(4, 4, TextureFormat.RGBA32, false);
                                        blackMap.SetPixels(new Color[16]); blackMap.Apply();
                                        var blackMaps = new LightmapData[savedMaps.Length];
                                        for (int i = 0; i < savedMaps.Length; ++i)
                                            blackMaps[i] = new LightmapData { lightmapColor = blackMap, lightmapDir = savedMaps[i].lightmapDir, shadowMask = savedMaps[i].shadowMask };
                                        LightmapSettings.lightmaps = blackMaps;
                                        try {
                                            CaptureMain(main, output + ".post-off.white-blacklightmap.png");
                                            // And without light probes and ambient SH: what remains then is neither.
                                            var savedProbes = LightmapSettings.lightProbes; var savedAmbient = RenderSettings.ambientProbe;
                                            LightmapSettings.lightProbes = null; RenderSettings.ambientProbe = new UnityEngine.Rendering.SphericalHarmonicsL2();
                                            try { CaptureMain(main, output + ".post-off.white-blacklightmap-nosh.png"); }
                                            finally { LightmapSettings.lightProbes = savedProbes; RenderSettings.ambientProbe = savedAmbient; }
                                        }
                                        finally { LightmapSettings.lightmaps = savedMaps; UnityEngine.Object.Destroy(blackMap); }
                                    }
                                    finally { foreach (Light light in bakedOff) light.enabled = true; }
                                }
                                finally {
                                    RenderSettings.defaultReflectionMode = savedMode; RenderSettings.customReflection = savedCube;
                                    foreach (ReflectionProbe probe in probes) probe.enabled = true;
                                    UnityEngine.Object.Destroy(black);
                                }
                                // White and without the map's own lights: baked light alone (plus reflections).
                                var mapLights = new System.Collections.Generic.List<Light>();
                                foreach (Light light in UnityEngine.Object.FindObjectsOfType<Light>())
                                    if (light.enabled && light.gameObject.activeInHierarchy && PathOf(light.transform).StartsWith("Map/")) { light.enabled = false; mapLights.Add(light); }
                                try { CaptureMain(main, output + ".post-off.white-nomaplights.png"); }
                                finally { foreach (Light light in mapLights) light.enabled = true; }
                            }
                        }
                        finally { foreach (Action undo in restore) undo(); }
                    }
                } finally { foreach (Behaviour e in enabled) e.enabled = true; }
            }
        }
        File.WriteAllText(output, "PASS: native scene audit completed");
        finished = true;
        Application.Quit();
    }
}
