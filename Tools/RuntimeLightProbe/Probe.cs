// Temporary BepInEx diagnostic for reading the live Maker lighting state.
// Build against the game's assemblies and remove the compiled DLL after use.
using System;
using System.Collections;
using System.IO;
using System.Globalization;
using System.Reflection;
using BepInEx;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.SceneManagement;

[BepInPlugin("openkoikatsu.runtime-light-probe", "OpenKoikatsu Runtime Light Probe", "1.0.0")]
public sealed class OpenKoikatsuRuntimeLightProbe : BaseUnityPlugin
{
    private static string ProbeCardPath
    {
        get
        {
            string configured = Environment.GetEnvironmentVariable("OK_PROBE_CARD");
            return String.IsNullOrEmpty(configured)
                ? @"C:\portable\KoikatuSunshine\UserData\chara\female\Alice_keiyuu.png"
                : configured;
        }
    }
    private static string ProbePath(string suffix)
    {
        return Path.Combine(@"C:\portable\OpenKoikatsu\Saved\DebugShots",
            Path.GetFileNameWithoutExtension(ProbeCardPath) + suffix);
    }

    private float elapsed;
    private float makerElapsed;
    private bool enteredMaker;
    private bool finished;
    private bool loadedCard;
    private string cardLoadResult = "not attempted";

    private void Update()
    {
        if (finished) return;
        elapsed += Time.unscaledDeltaTime;
        string scene = SceneManager.GetActiveScene().name;
        if (!enteredMaker && elapsed > 5f)
        {
            TitleScene title = UnityEngine.Object.FindObjectOfType<TitleScene>();
            if (title != null)
            {
                enteredMaker = true;
                title.OnCustomFemale();
            }
        }
        if (scene == "CustomScene") makerElapsed += Time.unscaledDeltaTime;
        if (makerElapsed > 10f && !loadedCard)
        {
            loadedCard = true;
            try
            {
                ChaControl[] characters = UnityEngine.Object.FindObjectsOfType<ChaControl>();
                if (characters.Length == 0) cardLoadResult = "no character";
                else
                {
                    string card = ProbeCardPath;
                    bool loaded = characters[0].chaFile.LoadCharaFile(card, 1, true);
                    if (loaded)
                    {
                        characters[0].ChangeCoordinateType();
                        characters[0].Reload();
                        if (Path.GetFileName(card).Equals("KK_091207.png", StringComparison.OrdinalIgnoreCase))
                            StartCoroutine(CaptureSlot10());
                    }
                    cardLoadResult = "loaded=" + loaded;
                }
            }
            catch (Exception ex) { cardLoadResult = ex.ToString(); }
        }
        if (makerElapsed > 28f || elapsed > 160f)
        {
            finished = true;
            Dump(scene);
            Application.Quit();
        }
    }

    private IEnumerator CaptureSlot10()
    {
        string path = ProbePath("Slot10BoneTimeline.tsv");
        using (StreamWriter w = new StreamWriter(path, false))
        {
            w.WriteLine("frame\ttime\tcomponent\tenabled\troot\tbone\tlocalPos\tlocalRot\tworldPos");
            while (!finished)
            {
                yield return new WaitForEndOfFrame();
                foreach (Transform slot in UnityEngine.Object.FindObjectsOfType<Transform>())
                {
                    if (slot.name != "ca_slot10") continue;
                    foreach (DynamicBone db in slot.GetComponentsInChildren<DynamicBone>(true))
                    {
                        Transform root = db.m_Root;
                        if (root == null) continue;
                        foreach (Transform bone in root.GetComponentsInChildren<Transform>(true))
                        {
                            if (bone.name != "joint1" && bone.name != "joint2" && bone.name != "joint3" &&
                                bone.name != "joint4" && bone.name != "joint5" && bone.name != "joint6" &&
                                bone.name != "joint7" && bone.name != "joint8") continue;
                            w.WriteLine(Time.frameCount + "\t" + Time.time.ToString("R", CultureInfo.InvariantCulture) +
                                "\t" + db.transform.name + "\t" + db.enabled + "\t" + root.name +
                                "\t" + bone.name + "\t" + bone.localPosition.ToString("F8") +
                                "\t" + bone.localRotation.ToString("F8") + "\t" + bone.position.ToString("F8"));
                        }
                    }
                }
                w.Flush();
            }
        }
    }

    private void Dump(string scene)
    {
        string path = ProbePath("FaceRuntime.txt");
        using (StreamWriter w = new StreamWriter(path, false))
        {
            w.WriteLine("scene=" + scene + " elapsed=" + elapsed + " makerElapsed=" + makerElapsed);
            w.WriteLine("cardLoad=" + cardLoadResult);
            foreach (ChaCustomHairComponent hair in UnityEngine.Object.FindObjectsOfType<ChaCustomHairComponent>())
            {
                string hierarchy = hair.name;
                for (Transform node = hair.transform.parent; node != null; node = node.parent)
                    hierarchy = node.name + "/" + hierarchy;
                if (!hierarchy.Contains("ca_slot02") && !hierarchy.Contains("ca_slot13") && !hierarchy.Contains("ca_slot07")) continue;
                w.WriteLine("hair_component=" + hierarchy + " rate=" + hair.lengthRate.ToString("R") + " add=" + hair.addLength.ToString("R"));
                if (hair.trfLength != null)
                    for (int j = 0; j < hair.trfLength.Length; ++j)
                        if (hair.trfLength[j] != null)
                            w.WriteLine("hair_length_bone=" + hair.trfLength[j].name + " local=" + hair.trfLength[j].localPosition.ToString("F8") + " base=" + (j < hair.baseLength.Length ? hair.baseLength[j].ToString("R") : "missing"));
                if (hierarchy.Contains("ca_slot02") || hierarchy.Contains("ca_slot13"))
                {
                    foreach (Renderer renderer in hair.GetComponentsInChildren<Renderer>(true))
                    {
                        string mats = "";
                        foreach (Material material in renderer.materials)
                            mats += " [" + material.name + " shader=" + material.shader.name + " queue=" + material.renderQueue + "]";
                        w.WriteLine("hair_renderer=" + renderer.name + " materials=" + mats);
                        SkinnedMeshRenderer smr = renderer as SkinnedMeshRenderer;
                        if (smr != null) DumpBaked(smr, hierarchy.Contains("ca_slot02") ? "slot02" : "slot13", w);
                    }
                    foreach (Transform node in hair.GetComponentsInChildren<Transform>(true))
                        if (node.name == "N_move" || node.name == "N_move2" || node.name == "root" || node.name == "joint1-1" || node.name == "joint1-2" || node == hair.transform)
                            w.WriteLine("hair_transform=" + node.name + " parent=" + (node.parent == null ? "null" : node.parent.name) +
                                " local=" + node.localPosition.ToString("F8") + " world=" + node.position.ToString("F8") +
                                " scale=" + node.localScale.ToString("F8") + " worldScale=" + node.lossyScale.ToString("F8"));
                }
            }
            foreach (SkinnedMeshRenderer smr in UnityEngine.Object.FindObjectsOfType<SkinnedMeshRenderer>())
                if (smr.name == "cf_O_face")
                {
                    DumpBaked(smr, "face", w);
                    foreach (Material mat in smr.materials)
                    {
                        w.WriteLine("face_material=" + mat.name + " shader=" + mat.shader.name + " queue=" + mat.renderQueue +
                            " alphaMask=" + (mat.GetTexture("_AlphaMask") == null ? "null" : mat.GetTexture("_AlphaMask").name) +
                            " alphaA=" + mat.GetFloat("_alpha_a").ToString("R") + " alphaB=" + mat.GetFloat("_alpha_b").ToString("R") +
                            " cutoff=" + mat.GetFloat("_Cutoff").ToString("R"));
                    }
                }
            foreach (Renderer renderer in UnityEngine.Object.FindObjectsOfType<Renderer>())
            {
                if (renderer.name != "cf_hair_07_00" && renderer.name != "O_nosetama" &&
                    renderer.name != "cf_hair_f_14" && renderer.name != "cf_O_face") continue;
                Mesh mesh = null;
                SkinnedMeshRenderer skinned = renderer as SkinnedMeshRenderer;
                if (skinned != null) { mesh = new Mesh(); skinned.BakeMesh(mesh); }
                else
                {
                    MeshFilter filter = renderer.GetComponent<MeshFilter>();
                    if (filter != null) mesh = filter.sharedMesh;
                }
                if (mesh == null) continue;
                Vector3 lo = new Vector3(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity);
                Vector3 hi = new Vector3(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity);
                foreach (Vector3 point in mesh.vertices)
                {
                    Vector3 world = renderer.transform.TransformPoint(point);
                    lo = Vector3.Min(lo, world);
                    hi = Vector3.Max(hi, world);
                }
                w.WriteLine("live_hair_bounds=" + renderer.name + " count=" + mesh.vertexCount +
                    " lo=" + lo.ToString("F8") + " hi=" + hi.ToString("F8") +
                    " renderer=" + renderer.transform.position.ToString("F8"));
                if (skinned != null) UnityEngine.Object.Destroy(mesh);
            }
            CaptureFaceCreate(w);
            CaptureFaceReference(w);
            return;
            w.WriteLine("ambientMode=" + RenderSettings.ambientMode + " intensity=" + RenderSettings.ambientIntensity);
            w.WriteLine("sky=" + RenderSettings.ambientSkyColor + " equator=" + RenderSettings.ambientEquatorColor + " ground=" + RenderSettings.ambientGroundColor);
            w.WriteLine("defaultReflectionMode=" + RenderSettings.defaultReflectionMode + " intensity=" + RenderSettings.reflectionIntensity + " bounces=" + RenderSettings.reflectionBounces);
            w.WriteLine("skybox=" + (RenderSettings.skybox == null ? "null" : RenderSettings.skybox.name + " shader=" + RenderSettings.skybox.shader.name));
            w.WriteLine("customReflection=" + (RenderSettings.customReflection == null ? "null" : RenderSettings.customReflection.name + " type=" + RenderSettings.customReflection.GetType().FullName + " size=" + RenderSettings.customReflection.width + "x" + RenderSettings.customReflection.height));
            Texture globalSpec = Shader.GetGlobalTexture("unity_SpecCube0");
            w.WriteLine("global_SpecCube0=" + (globalSpec == null ? "null" : globalSpec.name + " type=" + globalSpec.GetType().FullName + " size=" + globalSpec.width + "x" + globalSpec.height));
            Texture defaultProbe = ReflectionProbe.defaultTexture;
            w.WriteLine("defaultProbe=" + (defaultProbe == null ? "null" : defaultProbe.name + " type=" + defaultProbe.GetType().FullName + " size=" + defaultProbe.width + "x" + defaultProbe.height));
            w.WriteLine("ambientshadowG=" + Shader.GetGlobalVector("_ambientshadowG"));
            w.WriteLine("global_SHAr=" + Shader.GetGlobalVector("unity_SHAr"));
            w.WriteLine("global_SHAg=" + Shader.GetGlobalVector("unity_SHAg"));
            w.WriteLine("global_SHAb=" + Shader.GetGlobalVector("unity_SHAb"));
            w.WriteLine("character_count=" + UnityEngine.Object.FindObjectsOfType<ChaControl>().Length);
            SphericalHarmonicsL2 sh = RenderSettings.ambientProbe;
            for (int c = 0; c < 3; ++c)
            {
                w.Write("SH[" + c + "]=");
                for (int i = 0; i < 9; ++i) w.Write(" " + sh[c, i].ToString("R"));
                w.WriteLine();
            }
            SphericalHarmonicsL2 interpolated;
            LightProbes.GetInterpolatedProbe(Vector3.zero, null, out interpolated);
            for (int c = 0; c < 3; ++c)
            {
                w.Write("InterpolatedSH[" + c + "]=");
                for (int i = 0; i < 9; ++i) w.Write(" " + interpolated[c, i].ToString("R"));
                w.WriteLine();
            }
            Vector3[] directions = { Vector3.up, Vector3.down, Vector3.right, Vector3.left, Vector3.forward, Vector3.back, new Vector3(1, 1, 1).normalized };
            Color[] evaluated = new Color[directions.Length];
            interpolated.Evaluate(directions, evaluated);
            for (int i = 0; i < directions.Length; ++i)
                w.WriteLine("Evaluate " + directions[i].ToString("F6") + "=" + evaluated[i].ToString("F9"));
            ChaControl[] characters = UnityEngine.Object.FindObjectsOfType<ChaControl>();
            if (characters.Length > 0)
            {
                Renderer[] renderers = characters[0].GetComponentsInChildren<Renderer>(true);
                bool savedAccessoryTexture = false;
                foreach (Renderer renderer in renderers)
                {
                    if (!renderer.enabled || !renderer.gameObject.activeInHierarchy) continue;
                    SphericalHarmonicsL2 rendererSh;
                    LightProbes.GetInterpolatedProbe(renderer.bounds.center, renderer, out rendererSh);
                    w.WriteLine("renderer=" + renderer.name + " probeUsage=" + renderer.lightProbeUsage + " bounds=" + renderer.bounds.center.ToString("F6") + " sh0=" + rendererSh[0, 0].ToString("R") + " sh1=" + rendererSh[0, 1].ToString("R"));
                    break;
                }
                foreach (Renderer renderer in renderers)
                {
                    string name = renderer.name.ToLowerInvariant();
                    if (name != "cf_o_face" && name != "o_body_a" && name != "o_inner_a" && name != "o_top_jacket01_a") continue;
                    foreach (Material material in renderer.sharedMaterials)
                    {
                        w.WriteLine("accessory=" + renderer.name + " layer=" + renderer.gameObject.layer +
                            " reflectionUsage=" + renderer.reflectionProbeUsage + " enabled=" + renderer.enabled +
                            " active=" + renderer.gameObject.activeInHierarchy + " bounds=" + renderer.bounds +
                            " material=" + (material == null ? "null" : material.name) +
                            " shader=" + (material == null || material.shader == null ? "null" : material.shader.name) +
                            " queue=" + (material == null ? 0 : material.renderQueue) +
                            " color=" + (material == null || !material.HasProperty("_Color") ? "none" : material.GetColor("_Color").ToString("F4")) +
                            " mainTex=" + (material == null || material.mainTexture == null ? "none" : material.mainTexture.name));
                        if (name == "cf_o_face" && material != null)
                        {
                            foreach (string property in new [] { "_NormalMask", "_NormalMap", "_DetailMask" })
                            {
                                if (!material.HasProperty(property)) continue;
                                Texture texture = material.GetTexture(property);
                                w.WriteLine("face_texture=" + property + " value=" + (texture == null ? "null" : texture.name) +
                                    " size=" + (texture == null ? "none" : texture.width + "x" + texture.height) +
                                    " scale=" + material.GetTextureScale(property) + " offset=" + material.GetTextureOffset(property));
                            }
                            foreach (string property in new [] { "_FaceNormalG", "_FaceShadowG" })
                                if (material.HasProperty(property)) w.WriteLine("face_float=" + property + " value=" + material.GetFloat(property).ToString("R"));
                            w.WriteLine("face_global=_FaceNormalG " + Shader.GetGlobalFloat("_FaceNormalG").ToString("R") +
                                " _FaceShadowG " + Shader.GetGlobalFloat("_FaceShadowG").ToString("R"));
                        }
                        if (material != null && (name == "cf_o_face" || name == "o_body_a" || name == "o_inner_a" || name == "o_top_jacket01_a"))
                        {
                            string outDir = @"C:\portable\OpenKoikatsu\Saved\DebugShots\GameRuntimeYuuka";
                            Directory.CreateDirectory(outDir);
                            foreach (string property in new [] { "_MainTex", "_overtex1", "_overtex2", "_overtex3", "_DetailMask", "_LineMask" })
                            {
                                if (!material.HasProperty(property)) continue;
                                Texture source = material.GetTexture(property);
                                if (source == null) continue;
                                int width = Math.Min(source.width, 2048);
                                int height = Math.Min(source.height, 2048);
                                RenderTexture temp = RenderTexture.GetTemporary(width, height, 0, RenderTextureFormat.ARGB32);
                                Graphics.Blit(source, temp);
                                RenderTexture previous = RenderTexture.active;
                                RenderTexture.active = temp;
                                Texture2D sample = new Texture2D(width, height, TextureFormat.RGBA32, false);
                                sample.ReadPixels(new Rect(0, 0, width, height), 0, 0);
                                sample.Apply();
                                File.WriteAllBytes(Path.Combine(outDir, name + "_" + property + ".png"), sample.EncodeToPNG());
                                RenderTexture.active = previous;
                                RenderTexture.ReleaseTemporary(temp);
                                UnityEngine.Object.Destroy(sample);
                                w.WriteLine("saved_texture=" + name + " " + property + " " + source.name + " " + width + "x" + height);
                            }
                        }
                        if (name.Contains("nosetama") && material != null)
                        {
                            foreach (string property in new [] { "_Metallic", "_Glossiness", "_rimpower", "_rimV", "_NormalScale", "_alpha" })
                                if (material.HasProperty(property)) w.WriteLine("ibl_float=" + material.name + " property=" + property + " value=" + material.GetFloat(property).ToString("R"));
                            foreach (string property in new [] { "_rimcolor", "_overcolor1", "_overcolor2", "_overcolor3" })
                                if (material.HasProperty(property)) w.WriteLine("ibl_color=" + material.name + " property=" + property + " value=" + material.GetColor(property).ToString("F6"));
                            foreach (string property in new [] { "_NormalMap", "_DetailMask", "_overtex1", "_overtex2", "_overtex3" })
                                if (material.HasProperty(property)) w.WriteLine("ibl_texture=" + material.name + " property=" + property + " value=" + (material.GetTexture(property) == null ? "null" : material.GetTexture(property).name));
                        }
                    }
                    if (name.Contains("nosetama") && !savedAccessoryTexture)
                    {
                        savedAccessoryTexture = true;
                        string outDir = @"C:\portable\OpenKoikatsu\Saved\DebugShots\GameRuntimeAlice";
                        Directory.CreateDirectory(outDir);
                        for (int j = 0; j < renderer.sharedMaterials.Length; ++j)
                        {
                            Material material = renderer.sharedMaterials[j];
                            if (material == null || material.mainTexture == null) continue;
                            RenderTexture temp = RenderTexture.GetTemporary(512, 512, 0, RenderTextureFormat.ARGB32);
                            Graphics.Blit(material.mainTexture, temp);
                            RenderTexture previous = RenderTexture.active;
                            RenderTexture.active = temp;
                            Texture2D sample = new Texture2D(512, 512, TextureFormat.RGBA32, false);
                            sample.ReadPixels(new Rect(0, 0, 512, 512), 0, 0);
                            sample.Apply();
                            File.WriteAllBytes(Path.Combine(outDir, "O_nosetama_material" + j + ".png"), sample.EncodeToPNG());
                            RenderTexture.active = previous;
                            RenderTexture.ReleaseTemporary(temp);
                            UnityEngine.Object.Destroy(sample);
                        }
                    }
                    if (name.Contains("nosetama") || name == "cf_o_face")
                    {
                        MeshFilter filter = renderer.GetComponent<MeshFilter>();
                        Mesh mesh = filter == null ? null : filter.sharedMesh;
                        if (mesh == null && renderer is SkinnedMeshRenderer)
                        {
                            mesh = new Mesh();
                            ((SkinnedMeshRenderer)renderer).BakeMesh(mesh);
                        }
                        if (mesh != null)
                        {
                            Vector3 lo = new Vector3(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity);
                            Vector3 hi = new Vector3(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity);
                            Vector3[] vertices = mesh.vertices;
                            Vector3[] normals = mesh.normals;
                            StreamWriter faceNormals = name == "cf_o_face" ? new StreamWriter(@"C:\portable\OpenKoikatsu\Saved\ShaderDxbc\runtime_yuuka_face_normals.tsv", false) : null;
                            for (int i = 0; i < vertices.Length; ++i)
                            {
                                Vector3 p = renderer.transform.TransformPoint(vertices[i]);
                                Vector3 normal = i < normals.Length ? renderer.transform.TransformDirection(normals[i]).normalized : Vector3.zero;
                                lo = Vector3.Min(lo, p);
                                hi = Vector3.Max(hi, p);
                                if (i < 8) w.WriteLine("accessory_vertex=" + renderer.name + " index=" + i + " world=" + p.ToString("F8") + " uv=" + mesh.uv[i].ToString("F6"));
                                if (faceNormals != null) faceNormals.WriteLine(i + "\t" + p.x.ToString("R") + "\t" + p.y.ToString("R") + "\t" + p.z.ToString("R") + "\t" + normal.x.ToString("R") + "\t" + normal.y.ToString("R") + "\t" + normal.z.ToString("R"));
                            }
                            if (faceNormals != null) faceNormals.Close();
                            w.WriteLine("accessory_mesh=" + renderer.name + " count=" + vertices.Length + " lo=" + lo.ToString("F8") + " hi=" + hi.ToString("F8"));
                        }
                    }
                }
            }
            Light[] lights = UnityEngine.Object.FindObjectsOfType<Light>();
            foreach (Light l in lights)
                w.WriteLine("light=" + l.name + " type=" + l.type + " enabled=" + l.enabled + " active=" + l.gameObject.activeInHierarchy + " color=" + l.color + " intensity=" + l.intensity + " direction=" + l.transform.forward + " mask=" + l.cullingMask);
            ReflectionProbe[] probes = UnityEngine.Object.FindObjectsOfType<ReflectionProbe>();
            foreach (ReflectionProbe p in probes)
                w.WriteLine("probe=" + p.name + " enabled=" + p.enabled + " active=" + p.gameObject.activeInHierarchy + " mode=" + p.mode + " texture=" + (p.texture == null ? "null" : p.texture.name));
        }
    }

    private static void CaptureFaceCreate(StreamWriter log)
    {
        foreach (ChaControl cha in UnityEngine.Object.FindObjectsOfType<ChaControl>())
        {
            if (cha.customTexCtrlFace == null) continue;
            FieldInfo field = typeof(CustomTextureCreate).GetField("matCreate", BindingFlags.Instance | BindingFlags.NonPublic);
            Material mat = field == null ? null : field.GetValue(cha.customTexCtrlFace) as Material;
            log.WriteLine("face_create=" + (mat == null ? "null" : mat.name + " shader=" + mat.shader.name));
            if (mat == null) continue;
            foreach (string property in new [] { "_Color", "_Color2", "_Color3", "_Color4", "_Color5", "_Color6", "_Color7" })
                if (mat.HasProperty(property)) log.WriteLine("face_create_color " + property + "=" + mat.GetColor(property).ToString("F6"));
            foreach (string property in new [] { "_tex4uv", "_tex5uv", "_paint1", "_paint2", "_hokuro" })
                if (mat.HasProperty(property)) log.WriteLine("face_create_vector " + property + "=" + mat.GetVector(property).ToString("F6"));
            string dir = ProbePath("FaceCreateRuntime");
            Directory.CreateDirectory(dir);
            Material draw = cha.customTexCtrlFace.matDraw;
            FieldInfo createField = typeof(CustomTextureCreate).GetField("createTex", BindingFlags.Instance | BindingFlags.NonPublic);
            Texture created = createField == null ? null : createField.GetValue(cha.customTexCtrlFace) as Texture;
            log.WriteLine("face_create_rt=" + (created == null ? "null" : created.name + " " + created.width + "x" + created.height));
            foreach (string label in new [] { "createTex", "matDrawMain" })
            {
                Texture source = label == "createTex" ? created : (draw == null ? null : draw.mainTexture);
                log.WriteLine("face_create_output " + label + "=" + (source == null ? "null" : source.name + " " + source.width + "x" + source.height + " wrap=" + source.wrapMode));
                if (source == null) continue;
                RenderTexture rt = RenderTexture.GetTemporary(source.width, source.height, 0, RenderTextureFormat.ARGB32);
                Graphics.Blit(source, rt);
                RenderTexture previous = RenderTexture.active;
                RenderTexture.active = rt;
                Texture2D sample = new Texture2D(source.width, source.height, TextureFormat.RGBA32, false);
                sample.ReadPixels(new Rect(0, 0, source.width, source.height), 0, 0);
                sample.Apply();
                File.WriteAllBytes(Path.Combine(dir, label + ".png"), sample.EncodeToPNG());
                RenderTexture.active = previous;
                RenderTexture.ReleaseTemporary(rt);
                UnityEngine.Object.Destroy(sample);
            }
            FieldInfo mainField = typeof(CustomTextureCreate).GetField("texMain", BindingFlags.Instance | BindingFlags.NonPublic);
            Texture mainSource = mainField == null ? null : mainField.GetValue(cha.customTexCtrlFace) as Texture;
            log.WriteLine("face_create_texMain=" + (mainSource == null ? "null" : mainSource.name + " " + mainSource.width + "x" + mainSource.height));
            if (mainSource != null)
            {
                RenderTexture rt = RenderTexture.GetTemporary(mainSource.width, mainSource.height, 0, RenderTextureFormat.ARGB32);
                Graphics.Blit(mainSource, rt);
                RenderTexture previous = RenderTexture.active;
                RenderTexture.active = rt;
                Texture2D sample = new Texture2D(mainSource.width, mainSource.height, TextureFormat.RGBA32, false);
                sample.ReadPixels(new Rect(0, 0, mainSource.width, mainSource.height), 0, 0);
                sample.Apply();
                File.WriteAllBytes(Path.Combine(dir, "texMain.png"), sample.EncodeToPNG());
                RenderTexture.active = previous;
                RenderTexture.ReleaseTemporary(rt);
                UnityEngine.Object.Destroy(sample);
            }
            foreach (string property in new [] { "_MainTex", "_ColorMask", "_Texture3", "_Texture4", "_Texture5", "_Texture6", "_Texture7", "_paintmask" })
            {
                if (!mat.HasProperty(property)) continue;
                Texture source = mat.GetTexture(property);
                log.WriteLine("face_create_texture " + property + "=" + (source == null ? "null" : source.name + " " + source.width + "x" + source.height + " wrap=" + source.wrapMode));
                if (source == null) continue;
                int width = Math.Min(source.width, 2048), height = Math.Min(source.height, 2048);
                RenderTexture rt = RenderTexture.GetTemporary(width, height, 0, RenderTextureFormat.ARGB32);
                Graphics.Blit(source, rt);
                RenderTexture previous = RenderTexture.active;
                RenderTexture.active = rt;
                Texture2D sample = new Texture2D(width, height, TextureFormat.RGBA32, false);
                sample.ReadPixels(new Rect(0, 0, width, height), 0, 0);
                sample.Apply();
                File.WriteAllBytes(Path.Combine(dir, property + ".png"), sample.EncodeToPNG());
                RenderTexture.active = previous;
                RenderTexture.ReleaseTemporary(rt);
                UnityEngine.Object.Destroy(sample);
            }
        }
    }

    private static void CaptureFaceReference(StreamWriter log)
    {
        Camera source = Camera.main;
        if (source == null) { log.WriteLine("face_capture=no Camera.main"); return; }
        GameObject cameraObject = new GameObject("OpenKoikatsuDiagnosticCamera");
        Camera camera = cameraObject.AddComponent<Camera>();
        camera.CopyFrom(source);
        camera.enabled = false;
        camera.clearFlags = CameraClearFlags.SolidColor;
        camera.backgroundColor = new Color(0.5f, 0.5f, 0.5f, 1f);
        camera.transform.position = new Vector3(0f, 1.37f, 0.7f);
        camera.transform.rotation = Quaternion.LookRotation(new Vector3(0f, 1.37f, 0f) - camera.transform.position);
        camera.fieldOfView = 28f;
        log.WriteLine("face_camera_rect=" + camera.rect + " aspect=" + camera.aspect +
            " source_rect=" + source.rect + " source_aspect=" + source.aspect);
        camera.rect = new Rect(0f, 0f, 1f, 1f);
        camera.aspect = 900f / 1080f;
        camera.ResetProjectionMatrix();
        log.WriteLine("face_camera_normalized_rect=" + camera.rect + " aspect=" + camera.aspect +
            " hairTop=" + camera.WorldToViewportPoint(new Vector3(0f, 1.460466f, 0.029683f)) +
            " ornamentTop=" + camera.WorldToViewportPoint(new Vector3(0.08f, 1.497510f, -0.078f)));
        camera.nearClipPlane = 0.01f;
        camera.farClipPlane = 20f;
        RenderTexture target = new RenderTexture(900, 1080, 24, RenderTextureFormat.ARGB32);
        camera.targetTexture = target;
        foreach (Renderer renderer in UnityEngine.Object.FindObjectsOfType<Renderer>())
        {
            if (!renderer.enabled || renderer.bounds.center.y < 1.2f) continue;
            SkinnedMeshRenderer skinned = renderer as SkinnedMeshRenderer;
            Mesh mesh = null;
            if (skinned != null) { mesh = new Mesh(); skinned.BakeMesh(mesh); }
            else { MeshFilter filter = renderer.GetComponent<MeshFilter>(); if (filter != null) mesh = filter.sharedMesh; }
            if (mesh == null) continue;
            float highest = float.NegativeInfinity;
            foreach (Vector3 point in mesh.vertices)
                highest = Mathf.Max(highest, camera.WorldToViewportPoint(renderer.transform.TransformPoint(point)).y);
            if (highest > 0.78f)
                log.WriteLine("face_projected_top=" + renderer.name + " enabled=" + renderer.enabled +
                    " viewportY=" + highest.ToString("R") + " pixelY=" + ((1f - highest) * 1080f).ToString("R") +
                    " shader=" + (renderer.sharedMaterial == null ? "null" : renderer.sharedMaterial.shader.name));
            if (skinned != null) UnityEngine.Object.Destroy(mesh);
        }
        camera.Render();
        RenderTexture previous = RenderTexture.active;
        RenderTexture.active = target;
        Texture2D image = new Texture2D(900, 1080, TextureFormat.RGBA32, false);
        image.ReadPixels(new Rect(0, 0, 900, 1080), 0, 0);
        image.Apply();
        string output = ProbePath("MakerFaceFramed.png");
        File.WriteAllBytes(output, image.EncodeToPNG());
        log.WriteLine("face_capture=" + output + " source=" + source.name);
        // Camera.CopyFrom copies only Camera properties. ColorCorrector enables
        // AmplifyColorEffect on the main camera, so the diagnostic camera needs
        // its own component to reproduce the image the player actually sees.
        AmplifyColorEffect sourceColor = source.GetComponent<AmplifyColorEffect>();
        AmplifyColorEffect diagnosticColor = null;
        if (sourceColor != null)
        {
            log.WriteLine("face_source_color enabled=" + sourceColor.enabled +
                " lut=" + (sourceColor.LutTexture == null ? "null" : sourceColor.LutTexture.name) +
                " blend=" + sourceColor.BlendAmount);
            diagnosticColor = cameraObject.AddComponent<AmplifyColorEffect>();
            diagnosticColor.Tonemapper = sourceColor.Tonemapper;
            diagnosticColor.Exposure = sourceColor.Exposure;
            diagnosticColor.LinearWhitePoint = sourceColor.LinearWhitePoint;
            diagnosticColor.ApplyDithering = sourceColor.ApplyDithering;
            diagnosticColor.QualityLevel = sourceColor.QualityLevel;
            diagnosticColor.BlendAmount = sourceColor.BlendAmount;
            diagnosticColor.LutTexture = sourceColor.LutTexture;
            diagnosticColor.LutBlendTexture = sourceColor.LutBlendTexture;
            diagnosticColor.enabled = sourceColor.enabled;
            camera.Render();
            RenderTexture.active = target;
            image.ReadPixels(new Rect(0, 0, 900, 1080), 0, 0);
            image.Apply();
            string filtered = ProbePath("MakerFaceFiltered.png");
            File.WriteAllBytes(filtered, image.EncodeToPNG());
            log.WriteLine("face_capture_filtered=" + filtered);
        }
        var hairRenderers = new System.Collections.Generic.List<Renderer>();
        foreach (Renderer renderer in UnityEngine.Object.FindObjectsOfType<Renderer>())
        {
            if (!renderer.enabled) continue;
            foreach (Material material in renderer.sharedMaterials)
                if (material != null && material.shader != null &&
                    material.shader.name.Contains("main_hair"))
                {
                    log.WriteLine("face_hair_material renderer=" + renderer.name +
                        " material=" + material.name + " shader=" + material.shader.name +
                        " queue=" + material.renderQueue +
                        " line=" + (material.HasProperty("_LineColor") ? material.GetColor("_LineColor").ToString("F5") : "none") +
                        " linewidth=" + Shader.GetGlobalFloat("_linewidthG").ToString("R"));
                    hairRenderers.Add(renderer);
                    renderer.enabled = false;
                    break;
                }
        }
        camera.Render();
        RenderTexture.active = target;
        image.ReadPixels(new Rect(0, 0, 900, 1080), 0, 0);
        image.Apply();
        if (diagnosticColor != null && diagnosticColor.enabled)
        {
            string filteredNoHair = ProbePath("MakerFaceNoHairFiltered.png");
            File.WriteAllBytes(filteredNoHair, image.EncodeToPNG());
            log.WriteLine("face_capture_without_hair_filtered=" + filteredNoHair);
            diagnosticColor.enabled = false;
            camera.Render();
            RenderTexture.active = target;
            image.ReadPixels(new Rect(0, 0, 900, 1080), 0, 0);
            image.Apply();
        }
        string noHair = ProbePath("MakerFaceNoHair.png");
        File.WriteAllBytes(noHair, image.EncodeToPNG());
        log.WriteLine("face_capture_without_hair=" + noHair + " renderers=" + hairRenderers.Count);
        foreach (Renderer renderer in hairRenderers) renderer.enabled = true;
        var frontCopies = new System.Collections.Generic.List<Renderer>();
        var originalMaterials = new System.Collections.Generic.List<Material[]>();
        foreach (Renderer renderer in UnityEngine.Object.FindObjectsOfType<Renderer>())
        {
            if (renderer.name != "cf_hair_f_14") continue;
            Material[] materials = renderer.sharedMaterials;
            if (materials.Length < 2 || materials[1] == null ||
                materials[1].shader.name != "Shader Forge/main_item_studio_alpha") continue;
            int copyIndex = frontCopies.Count;
            for (int materialIndex = 0; materialIndex < 2; ++materialIndex)
            {
                Material liveMaterial = materials[materialIndex];
                Texture liveMask = liveMaterial.GetTexture("_ColorMask");
                log.WriteLine("face_runtime_material=" + copyIndex + ":" + materialIndex +
                    " shader=" + liveMaterial.shader.name +
                    " color=" + liveMaterial.GetColor("_Color") +
                    " color2=" + liveMaterial.GetColor("_Color2") +
                    " color3=" + liveMaterial.GetColor("_Color3") +
                    " mask=" + (liveMask == null ? "null" : liveMask.name + ":" + liveMask.GetType().Name));
                if (copyIndex == 0 && materialIndex == 1)
                {
                    string[] colorKeys = { "_Color1_2", "_Color2_2", "_Color3_2" };
                    foreach (string key in colorKeys)
                        log.WriteLine("face_studio_color=" + key + " value=" + liveMaterial.GetColor(key));
                    string[] vectorKeys = { "_ColorMask_ST", "_PatternMask1_ST", "_PatternMask2_ST", "_PatternMask3_ST", "_Patternuv1", "_Patternuv2", "_Patternuv3" };
                    foreach (string key in vectorKeys)
                        log.WriteLine("face_studio_vector=" + key + " value=" + liveMaterial.GetVector(key));
                    string[] floatKeys = { "_patternrotator1", "_patternrotator2", "_patternrotator3", "_patternclamp1", "_patternclamp2", "_patternclamp3", "_EmissionPower", "_alpha" };
                    foreach (string key in floatKeys)
                        log.WriteLine("face_studio_float=" + key + " value=" + liveMaterial.GetFloat(key).ToString("R"));
                    string[] textureKeys = { "_ColorMask", "_PatternMask1", "_PatternMask2", "_PatternMask3" };
                    foreach (string key in textureKeys)
                    {
                        Texture maskSource = liveMaterial.GetTexture(key);
                        if (maskSource == null) { log.WriteLine("face_studio_texture=" + key + " null"); continue; }
                        RenderTexture temp = RenderTexture.GetTemporary(maskSource.width, maskSource.height, 0, RenderTextureFormat.ARGB32);
                        Graphics.Blit(maskSource, temp);
                        RenderTexture old = RenderTexture.active;
                        RenderTexture.active = temp;
                        Texture2D readback = new Texture2D(maskSource.width, maskSource.height, TextureFormat.RGBA32, false);
                        readback.ReadPixels(new Rect(0, 0, maskSource.width, maskSource.height), 0, 0);
                        readback.Apply();
                        string file = @"C:\portable\OpenKoikatsu\Saved\DebugShots\AliceMakerStudio" + key + ".png";
                        File.WriteAllBytes(file, readback.EncodeToPNG());
                        log.WriteLine("face_studio_texture=" + key + " name=" + maskSource.name + " type=" + maskSource.GetType().Name + " path=" + file);
                        UnityEngine.Object.Destroy(readback);
                        RenderTexture.active = old;
                        RenderTexture.ReleaseTemporary(temp);
                    }
                }
                Texture sourceTexture = materials[materialIndex].mainTexture;
                if (sourceTexture == null) continue;
                RenderTexture copyTarget = RenderTexture.GetTemporary(sourceTexture.width, sourceTexture.height, 0, RenderTextureFormat.ARGB32);
                Graphics.Blit(sourceTexture, copyTarget);
                RenderTexture activeBefore = RenderTexture.active;
                RenderTexture.active = copyTarget;
                Texture2D textureCopy = new Texture2D(sourceTexture.width, sourceTexture.height, TextureFormat.RGBA32, false);
                textureCopy.ReadPixels(new Rect(0, 0, sourceTexture.width, sourceTexture.height), 0, 0);
                textureCopy.Apply();
                string texturePath = @"C:\portable\OpenKoikatsu\Saved\DebugShots\AliceMakerRuntimeHair" + copyIndex + "_mat" + materialIndex + ".png";
                File.WriteAllBytes(texturePath, textureCopy.EncodeToPNG());
                log.WriteLine("face_runtime_texture=" + texturePath + " type=" + sourceTexture.GetType().Name +
                    " size=" + sourceTexture.width + "x" + sourceTexture.height);
                UnityEngine.Object.Destroy(textureCopy);
                RenderTexture.active = activeBefore;
                RenderTexture.ReleaseTemporary(copyTarget);
            }
            frontCopies.Add(renderer);
            originalMaterials.Add(materials);
            log.WriteLine("face_copy=" + renderer.name + " alpha=" + materials[1].GetFloat("_alpha").ToString("R") +
                " main=" + (materials[1].mainTexture == null ? "null" : materials[1].mainTexture.name));
            renderer.sharedMaterials = new Material[] { materials[0] };
        }
        if (frontCopies.Count > 0)
        {
            camera.Render();
            RenderTexture.active = target;
            image.ReadPixels(new Rect(0, 0, 900, 1080), 0, 0);
            image.Apply();
            string withoutCopies = ProbePath("MakerFaceNoMECopies.png");
            File.WriteAllBytes(withoutCopies, image.EncodeToPNG());
            log.WriteLine("face_capture_without_me_copies=" + withoutCopies + " renderers=" + frontCopies.Count);
            for (int i = 0; i < frontCopies.Count; ++i)
                frontCopies[i].sharedMaterials = originalMaterials[i];
        }
        SkinnedMeshRenderer shadowCaster = null;
        foreach (SkinnedMeshRenderer item in UnityEngine.Object.FindObjectsOfType<SkinnedMeshRenderer>())
            if (item.name == "o_shadowcaster") { shadowCaster = item; break; }
        if (shadowCaster != null)
        {
            shadowCaster.enabled = false;
            camera.Render();
            RenderTexture.active = target;
            image.ReadPixels(new Rect(0, 0, 900, 1080), 0, 0);
            image.Apply();
            string without = ProbePath("MakerFaceNoShadowCaster.png");
            File.WriteAllBytes(without, image.EncodeToPNG());
            log.WriteLine("face_capture_without_shadowcaster=" + without);
            shadowCaster.enabled = true;
        }
        RenderTexture.active = previous;
        camera.targetTexture = null;
        UnityEngine.Object.Destroy(target);
        UnityEngine.Object.Destroy(image);
        UnityEngine.Object.Destroy(cameraObject);
    }

    private static void DumpBaked(SkinnedMeshRenderer smr, string tag, StreamWriter log)
    {
        string skinPath = @"C:\portable\OpenKoikatsu\Saved\DebugShots\AliceRuntimeSkin_" + tag + ".tsv";
        using (StreamWriter skin = new StreamWriter(skinPath, false))
        {
            skin.WriteLine("renderer\t" + MatrixText(smr.transform.localToWorldMatrix));
            skin.WriteLine("rootBone\t" + (smr.rootBone == null ? "null" : smr.rootBone.name + "\t" + MatrixText(smr.rootBone.localToWorldMatrix)));
            Matrix4x4[] binds = smr.sharedMesh.bindposes;
            Transform[] bones = smr.bones;
            for (int i = 0; i < bones.Length; ++i)
                skin.WriteLine("bone\t" + i + "\t" + (bones[i] == null ? "null" : bones[i].name) + "\t" + (bones[i] == null ? "null" : MatrixText(bones[i].localToWorldMatrix)) + "\t" + (i < binds.Length ? MatrixText(binds[i]) : "missing"));
            Vector3[] rest = smr.sharedMesh.vertices;
            BoneWeight[] weights = smr.sharedMesh.boneWeights;
            skin.WriteLine("rest0\t" + (rest.Length == 0 ? "missing" : rest[0].ToString("F8")));
            skin.WriteLine("weight0\t" + (weights.Length == 0 ? "missing" : weights[0].boneIndex0 + ":" + weights[0].weight0.ToString("R", CultureInfo.InvariantCulture)));
        }
        Mesh mesh = new Mesh();
        smr.BakeMesh(mesh);
        if (tag == "face")
        {
            string normalPath = ProbePath("FaceNormals.tsv");
            using (StreamWriter normal = new StreamWriter(normalPath, false))
            {
                Matrix4x4 normalMatrix = smr.transform.localToWorldMatrix.inverse.transpose;
                Vector3[] vertices = mesh.vertices;
                Vector3[] normals = mesh.normals;
                for (int i = 0; i < vertices.Length && i < normals.Length; ++i)
                {
                    Vector3 point = smr.transform.TransformPoint(vertices[i]);
                    Vector3 direction = normalMatrix.MultiplyVector(normals[i]).normalized;
                    normal.WriteLine(i.ToString(CultureInfo.InvariantCulture) + "\t" +
                        point.x.ToString("R", CultureInfo.InvariantCulture) + "\t" +
                        point.y.ToString("R", CultureInfo.InvariantCulture) + "\t" +
                        point.z.ToString("R", CultureInfo.InvariantCulture) + "\t" +
                        direction.x.ToString("R", CultureInfo.InvariantCulture) + "\t" +
                        direction.y.ToString("R", CultureInfo.InvariantCulture) + "\t" +
                        direction.z.ToString("R", CultureInfo.InvariantCulture));
                }
            }
        }
        foreach (bool applyRendererScale in new bool[] { false, true })
        {
            string path = @"C:\portable\OpenKoikatsu\Saved\DebugShots\AliceRuntimeMesh_" + tag + (applyRendererScale ? "_fullworld" : "_noscale") + ".obj";
            using (StreamWriter obj = new StreamWriter(path, false))
                foreach (Vector3 vertex in mesh.vertices)
                {
                    Vector3 p = applyRendererScale ? smr.transform.TransformPoint(vertex) : smr.transform.position + smr.transform.rotation * vertex;
                    obj.WriteLine("v " + p.x.ToString("R", CultureInfo.InvariantCulture) + " " + p.y.ToString("R", CultureInfo.InvariantCulture) + " " + p.z.ToString("R", CultureInfo.InvariantCulture));
                }
            log.WriteLine("baked=" + tag + " rendererScale=" + applyRendererScale + " count=" + mesh.vertexCount + " localBounds=" + mesh.bounds + " rendererBounds=" + smr.bounds);
        }
        UnityEngine.Object.Destroy(mesh);
    }

    private static string MatrixText(Matrix4x4 m)
    {
        float[] values = { m.m00, m.m01, m.m02, m.m03, m.m10, m.m11, m.m12, m.m13,
            m.m20, m.m21, m.m22, m.m23, m.m30, m.m31, m.m32, m.m33 };
        string[] text = new string[values.Length];
        for (int i = 0; i < values.Length; ++i) text[i] = values[i].ToString("R", CultureInfo.InvariantCulture);
        return string.Join(",", text);
    }
}
