// Read-only, temporary BepInEx probe. Does not save or modify character cards.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Globalization;
using System.Reflection;
using System.Runtime.InteropServices;
using HarmonyLib;
using BepInEx;
using UnityEngine;
using UnityEngine.SceneManagement;

[BepInPlugin("openkoikatsu.game-pose-probe", "OpenKoikatsu Game Pose Probe", "1.0.0")]
[BepInProcess("CharaStudio")]
public sealed partial class GamePoseProbe : BaseUnityPlugin
{
    [Serializable] public class Bone {
        public string name, path; public int expressionIndex;
        public Vector3 position, scale, worldPosition; public Quaternion rotation;
    }
    [Serializable] public class Flag { public string name; public bool value; }
    [Serializable] public class Dump {
        public string error, controller, clip, renderer;
        public float normalizedTime, stateLength;
        public int fullPathHash;
        public Bone[] bones; public Flag[] flags; public Vector3[] vertices; public int[] triangles;
        public Vector3[] bindVertices; public BoneWeight[] weights; public Matrix4x4[] bindposes;
        public string[] skinBones; public int skinQuality;
    }
    private bool started, finished;
    private float elapsed;
    private void Awake() {
        if(Environment.GetEnvironmentVariable("OK_PROBE_H_AUTO_INIT")=="1")new Harmony("openkoikatsu.game-pose-probe.ik-init").Patch(typeof(RootMotion.FinalIK.IKSolver).GetMethod("Initiate"),prefix:new HarmonyMethod(typeof(GamePoseProbe).GetMethod("HInitiatePrefix",BindingFlags.Static|BindingFlags.NonPublic)),postfix:new HarmonyMethod(typeof(GamePoseProbe).GetMethod("HInitiatePostfix",BindingFlags.Static|BindingFlags.NonPublic)));
        if(Environment.GetEnvironmentVariable("OK_PROBE_H_AUTO_INIT")=="1")new Harmony("openkoikatsu.game-pose-probe.ik-triangle").Patch(typeof(RootMotion.FinalIK.FBIKChain).GetMethod("GetDirToBendPoint",BindingFlags.Instance|BindingFlags.NonPublic),postfix:new HarmonyMethod(typeof(GamePoseProbe).GetMethod("HTrianglePostfix",BindingFlags.Static|BindingFlags.NonPublic)));
        if(Environment.GetEnvironmentVariable("OK_PROBE_FACE_CREATE")=="1")new Harmony("openkoikatsu.game-pose-probe.face-inputs").Patch(typeof(CustomTextureCreate).GetMethod("RebuildTextureAndSetMaterial"),prefix:new HarmonyMethod(typeof(GamePoseProbe).GetMethod("FaceCreatePrefix",BindingFlags.Static|BindingFlags.NonPublic)),postfix:new HarmonyMethod(typeof(GamePoseProbe).GetMethod("FaceCreatePostfix",BindingFlags.Static|BindingFlags.NonPublic)));
        if(!String.IsNullOrEmpty(Environment.GetEnvironmentVariable("OK_PROBE_CLOTHES_CREATE_NAMES")))
            new Harmony("openkoikatsu.game-pose-probe.clothes-inputs").Patch(
                typeof(CustomTextureCreate).GetMethod("RebuildTextureAndSetMaterial"),
                postfix:new HarmonyMethod(typeof(GamePoseProbe).GetMethod("ClothesCreatePostfix",BindingFlags.Static|BindingFlags.NonPublic)));
    }
    private void Update() {
        elapsed += Time.unscaledDeltaTime;
        if (!started && elapsed > 10f) {
            Studio.Studio studio = UnityEngine.Object.FindObjectOfType<Studio.Studio>();
            if (studio != null) { started = true; StartCoroutine(Run()); }
        }
        if (!finished && elapsed > 180f) {
            finished = true; Save(new Dump { error = "timeout before probe completion" }); Application.Quit();
        }
    }
    private static string PathOf(Transform tf) {
        string name = tf.name;
        while (tf.parent != null) { tf = tf.parent; name = tf.name + "/" + name; }
        return name;
    }
    private static void Save(Dump dump) {
        string path = Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT");
        if (String.IsNullOrEmpty(path)) path = @"C:\portable\OpenKoikatsu\Saved\Logs\GamePoseC33.json";
        File.WriteAllText(path, JsonUtility.ToJson(dump, true));
    }
    private static void WriteTransforms(ChaControl cha, string suffix) {
        using (StreamWriter w = new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT") + suffix))
            foreach (Transform tf in cha.transform.GetComponentsInChildren<Transform>(true))
                w.WriteLine("all_transform\t" + tf.name + "\t" + PathOf(tf) + "\t" + tf.localPosition.ToString("R") + "\t" + tf.localRotation.ToString("R") + "\t" + tf.localScale.ToString("R"));
    }
    private static void WritePhysics(ChaControl cha, string suffix = ".physics.tsv") {
        using (StreamWriter w = new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT") + suffix)) {
            w.WriteLine("body_settings\t" + cha.sex + "\t" + cha.fileBody.bustSoftness.ToString("R") + "\t" + cha.fileBody.bustWeight.ToString("R"));
            w.WriteLine("simulation_settings\t" + (Environment.GetEnvironmentVariable("OK_PROBE_BODY_DT") ?? "0.016666667") + "\t" + (Environment.GetEnvironmentVariable("OK_PROBE_BODY_MOVE") ?? "0"));
            Type harmony = Type.GetType("HarmonyLib.Harmony, 0Harmony");
            foreach (Type type in new Type[] { typeof(DynamicBone), typeof(DynamicBone_Ver02) }) {
                MethodInfo method = type.GetMethod("SkipUpdateParticles", BindingFlags.Instance | BindingFlags.NonPublic);
                object patches = harmony == null ? null : harmony.GetMethod("GetPatchInfo").Invoke(null, new object[] { method });
                IEnumerable owners = patches == null ? null : (IEnumerable)patches.GetType().GetProperty("Owners").GetValue(patches, null);
                if (owners != null) foreach (object owner in owners) w.WriteLine("skip_patch_owner\t" + type.Name + "\t" + owner);
            }
            foreach (DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) {
                w.WriteLine("component\t" + PathOf(db.transform) + "\t" + db.Comment + "\t" + db.enabled + "\t" + db.PtnNo + "\t" + PathOf(db.Root) + "\t" + db.UpdateRate + "\t" + db.ReflectSpeed + "\t" + db.HeavyLoopMaxCount + "\t" + db.Gravity.ToString("R"));
                foreach (Transform tf in db.Bones) w.WriteLine("bone\t" + db.Comment + "\t" + PathOf(tf));
                foreach (DynamicBoneCollider c in db.Colliders) if (c != null)
                    w.WriteLine("collider\t" + db.Comment + "\t" + PathOf(c.transform) + "\t" + c.m_Radius.ToString("R") + "\t" + c.m_Height.ToString("R") + "\t" + c.m_Center.ToString("R") + "\t" + (int)c.m_Direction + "\t" + (int)c.m_Bound);
                foreach (string name in new string[] { "UpdateTime", "ObjectPrevPosition", "Weight" }) {
                    object value = typeof(DynamicBone_Ver02).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic).GetValue(db);
                    w.WriteLine("component_state\t" + db.Comment + "\t" + name + "\t" + (value is Vector3 ? ((Vector3)value).ToString("R") : ((float)value).ToString("R")));
                }
                IList particles = (IList)typeof(DynamicBone_Ver02).GetField("Particles", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(db);
                for (int i = 0; i < particles.Count; ++i)
                    foreach (FieldInfo f in particles[i].GetType().GetFields()) {
                        object v = f.GetValue(particles[i]);
                        string value = v is Transform ? PathOf((Transform)v) : v is Vector3 ? ((Vector3)v).ToString("R") : v is Quaternion ? ((Quaternion)v).ToString("R") : v is float ? ((float)v).ToString("R") : v == null ? "null" : v.ToString();
                        w.WriteLine("particle\t" + db.Comment + "\t" + i + "\t" + f.Name + "\t" + value);
                    }
            }
        }
    }
    private static void ApplyColliderFlags(ChaControl cha) {
        foreach (MonoBehaviour component in cha.GetComponentsInChildren<MonoBehaviour>(true)) {
            Type type = component.GetType();
            if (type.FullName != "KK_Plugins.Colliders+ColliderController") continue;
            foreach (string property in new string[] { "BreastCollidersEnabled", "FloorColliderEnabled", "SkirtCollidersEnabled" })
                type.GetProperty(property).SetValue(component, true, null);
            type.GetMethod("ApplyColliders").Invoke(component, null);
            type.GetMethod("LateUpdate", BindingFlags.Instance | BindingFlags.NonPublic).Invoke(component, null);
        }
    }
    private sealed class ClothingControl {
        public Transform frame, shared, removed,tip,prefab;
        public SkinnedMeshRenderer renderer;
        public AssignedAnotherWeights weights;
    }
    private static ClothingControl SetupClothingControl(ChaControl cha) {
        GameObject frame = new GameObject("__ok_clothing_control"); frame.transform.SetParent(cha.objTop.transform, false);
        if (Environment.GetEnvironmentVariable("OK_PROBE_IMPLANT_PHYSICS") == "1") frame.transform.SetParent(null,false);
        GameObject shared = new GameObject("borrowed"); shared.transform.SetParent(frame.transform, false); shared.transform.localPosition = new Vector3(10f, 0f, 0f);
        GameObject prefab = new GameObject("ct_clothesTop"); prefab.transform.SetParent(frame.transform, false); prefab.transform.localPosition = new Vector3(0.25f, 0f, 0f);
        GameObject cloned = new GameObject("cf_j_root"); cloned.transform.SetParent(prefab.transform, false);
        GameObject borrowed = new GameObject("borrowed"); borrowed.transform.SetParent(cloned.transform, false); borrowed.transform.localPosition = Vector3.right;
        GameObject owned = new GameObject("private"); owned.transform.SetParent(prefab.transform, false); owned.transform.localPosition = new Vector3(2f,0f,0f);
        GameObject removed = new GameObject("removed_private"); removed.transform.SetParent(cloned.transform, false); removed.transform.localPosition = new Vector3(3f,0f,0f);
        Transform tip=null;
        if (Environment.GetEnvironmentVariable("OK_PROBE_IMPLANT_PHYSICS") == "1") {
            removed.transform.localPosition=new Vector3(0f,0.2f,0f);
            GameObject node=new GameObject("implanted_tip"); node.transform.SetParent(removed.transform,false);
            node.transform.localPosition=new Vector3(0f,0.2f,0f); tip=node.transform;
        }
        GameObject renderObject = new GameObject("renderer"); renderObject.transform.SetParent(prefab.transform, false);
        SkinnedMeshRenderer renderer = renderObject.AddComponent<SkinnedMeshRenderer>(); renderer.enabled = false;
        Mesh mesh = new Mesh(); mesh.vertices = new Vector3[] { Vector3.zero,Vector3.zero,Vector3.zero,Vector3.zero,Vector3.right,Vector3.up,Vector3.forward };
        mesh.normals = new Vector3[] { Vector3.forward,Vector3.forward,Vector3.forward,Vector3.forward,Vector3.forward,Vector3.forward,Vector3.forward };
        mesh.triangles = new int[] { 0,1,2 }; mesh.bindposes = new Matrix4x4[] { Matrix4x4.identity,Matrix4x4.identity,Matrix4x4.Translate(new Vector3(0.1f,0f,0f)),Matrix4x4.Translate(new Vector3(0.2f,0f,0f)) };
        BoneWeight[] slots = new BoneWeight[7]; for (int i=0;i<slots.Length;++i) { slots[i].weight0=1f; slots[i].boneIndex0=i<3?i:3; }
        mesh.boneWeights = slots; renderer.sharedMesh = mesh; renderer.bones = new Transform[] { borrowed.transform,owned.transform,borrowed.transform,removed.transform };
        if (Environment.GetEnvironmentVariable("OK_PROBE_IMPLANT") == "1") {
            Type type=Type.GetType("ModBoneImplantor.BoneImplantProcess, ModBoneImplantor", true);
            Component marker=prefab.AddComponent(type);
            type.GetField("trfSrc").SetValue(marker,removed.transform);
            type.GetField("trfDst").SetValue(marker,borrowed.transform);
        }
        AssignedAnotherWeights assigned = new AssignedAnotherWeights(); assigned.CreateBoneList(shared, "");
        assigned.AssignedWeightsAndSetBounds(prefab, "cf_j_root", new Bounds(Vector3.zero,Vector3.one*100f), shared.transform);
        return new ClothingControl { frame=frame.transform,shared=shared.transform,removed=removed.transform,tip=tip,prefab=prefab.transform,renderer=renderer,weights=assigned };
    }
    private static void WriteClothingControl(ClothingControl control) {
        using (StreamWriter w = new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT") + ".clothing.tsv")) {
            w.WriteLine("removed_destroyed\t" + (control.removed == null));
            for (int sample=0;sample<2;++sample) {
                control.shared.localPosition = new Vector3(sample==0?10f:20f,0f,0f);
                Transform[] bones = control.renderer.bones;
                for (int i=0;i<bones.Length;++i) w.WriteLine("slot\t"+sample+"\t"+i+"\t"+(bones[i]==null?"null":bones[i].name)+"\t"+(bones[i]==control.shared));
                Mesh baked = new Mesh(); control.renderer.BakeMesh(baked);
                for (int i=0;i<baked.vertices.Length;++i) w.WriteLine("vertex\t"+sample+"\t"+i+"\t"+control.frame.InverseTransformPoint(control.renderer.transform.TransformPoint(baked.vertices[i])).ToString("R"));
                UnityEngine.Object.Destroy(baked);
            }
        }
        if (Environment.GetEnvironmentVariable("OK_PROBE_IMPLANT_PHYSICS") == "1") RunClothingPhysics(control);
    }
    private static void RunClothingPhysics(ClothingControl control) {
        using(StreamWriter w=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".quaternion.tsv")) {
            foreach(float angle in new float[]{0.000001f,0.00001f,0.0001f,0.0005f,0.001f,0.0015f,0.00485f}) {
                Vector3 target=new Vector3((float)Math.Sin(angle),(float)Math.Cos(angle),0f);
                w.WriteLine(angle.ToString("R")+"\t"+target.ToString("R")+"\t"+Quaternion.FromToRotation(Vector3.up,target).ToString("R"));
            }
        }
        control.shared.localPosition=new Vector3(10f,0f,0f); control.shared.localRotation=Quaternion.identity;
        List<DynamicBone> components=new List<DynamicBone>();
        foreach (Transform root in new Transform[] {control.shared,control.removed}) {
            DynamicBone db=control.prefab.gameObject.AddComponent<DynamicBone>(); db.enabled=false; db.m_Root=root;
            db.m_UpdateRate=60f; db.m_Damping=0.2f; db.m_Elasticity=0.1f; db.m_Stiffness=0.05f; db.m_Inert=0f;
            db.m_Radius=0f; db.m_Gravity=Vector3.zero; db.m_Force=root==control.shared?new Vector3(0.01f,0f,0f):new Vector3(0f,0f,0.01f);
            db.m_EndLength=0f; db.m_EndOffset=Vector3.zero; db.m_Exclusions=new List<Transform>(); db.m_notRolls=new List<Transform>();
            db.m_Colliders=new List<DynamicBoneCollider>();
            db.m_DampingDistrib=new AnimationCurve(); db.m_ElasticityDistrib=new AnimationCurve(); db.m_StiffnessDistrib=new AnimationCurve();
            db.m_InertDistrib=new AnimationCurve(); db.m_RadiusDistrib=new AnimationCurve(); components.Add(db);
        }
        BindingFlags flags=BindingFlags.Instance|BindingFlags.NonPublic; Type type=typeof(DynamicBone);
        foreach (DynamicBone db in components) {type.GetMethod("SetupParticles",flags).Invoke(db,null);type.GetField("m_Time",flags).SetValue(db,0f);}
        MethodInfo init=type.GetMethod("InitTransforms",flags),step=type.GetMethod("UpdateDynamicBones",flags);
        for(int frame=0;frame<120;++frame) {
            foreach(DynamicBone db in components) init.Invoke(db,null);
            foreach(DynamicBone db in components) step.Invoke(db,new object[]{1f/60f});
        }
        using(StreamWriter w=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".clothing-physics.tsv")) {
            foreach(Transform tf in new Transform[]{control.shared,control.removed,control.tip})
                w.WriteLine("bone\t"+tf.name+"\t"+tf.localPosition.ToString("R")+"\t"+tf.localRotation.ToString("R")+"\t"+control.frame.InverseTransformPoint(tf.position).ToString("R"));
            Mesh baked=new Mesh(); control.renderer.BakeMesh(baked);
            for(int i=0;i<baked.vertices.Length;++i) w.WriteLine("vertex\t"+i+"\t"+control.frame.InverseTransformPoint(control.renderer.transform.TransformPoint(baked.vertices[i])).ToString("R"));
            UnityEngine.Object.Destroy(baked);
        }
    }
    private static void RunHierarchyControl() {
        BindingFlags flags=BindingFlags.Instance|BindingFlags.NonPublic;
        Type type=typeof(DynamicBone);
        using(StreamWriter w=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".branching.tsv")) {
            foreach(string mode in new string[]{"plain","notroll","excluded"}) {
                GameObject component=new GameObject("__ok_branching_"+mode);
                Dictionary<string,Transform> nodes=new Dictionary<string,Transform>();
                string[] names={"root","branch_a","a_tip","roll_b","b_tip","b_skip","branch_z","z_tip","roll_a","ra_tip","excluded","excluded_tip"};
                string[] parents={"","root","branch_a","root","roll_b","roll_b","root","branch_z","root","roll_a","root","excluded"};
                Vector3[] positions={Vector3.zero,new Vector3(-0.12f,0.2f,0f),new Vector3(0f,0.2f,0f),new Vector3(0.1f,0.12f,0.03f),new Vector3(0f,0.18f,0f),new Vector3(0.08f,0.1f,0f),new Vector3(0.02f,0.24f,-0.04f),new Vector3(0f,0.22f,0f),new Vector3(-0.05f,0.18f,0.02f),new Vector3(0f,0.15f,0f),new Vector3(0.14f,0.15f,0f),new Vector3(0f,0.1f,0f)};
                for(int i=0;i<names.Length;++i) {
                    Transform tf=new GameObject(names[i]).transform;
                    tf.SetParent(parents[i]==""?component.transform:nodes[parents[i]],false);tf.localPosition=positions[i];nodes.Add(names[i],tf);
                }
                string[] siblings={"branch_z","roll_a","branch_a","roll_b","excluded"};
                for(int i=0;i<siblings.Length;++i) nodes[siblings[i]].SetSiblingIndex(i);
                DynamicBone db=component.AddComponent<DynamicBone>();db.enabled=false;db.m_Root=nodes["root"];
                db.m_UpdateRate=60f;db.m_Damping=0.2f;db.m_Elasticity=0.1f;db.m_Stiffness=0.05f;db.m_Inert=0f;
                db.m_Radius=0f;db.m_Gravity=Vector3.zero;db.m_Force=new Vector3(0.007f,-0.001f,0.003f);
                db.m_EndLength=0f;db.m_EndOffset=new Vector3(0f,0.05f,0f);
                db.m_Exclusions=new List<Transform>();db.m_notRolls=new List<Transform>();db.m_Colliders=new List<DynamicBoneCollider>();
                if(mode!="plain") {db.m_notRolls.Add(nodes["roll_a"]);db.m_notRolls.Add(nodes["roll_b"]);}
                if(mode=="excluded") {db.m_Exclusions.Add(nodes["excluded"]);db.m_Exclusions.Add(nodes["b_skip"]);}
                db.m_DampingDistrib=new AnimationCurve();db.m_ElasticityDistrib=new AnimationCurve();db.m_StiffnessDistrib=new AnimationCurve();db.m_InertDistrib=new AnimationCurve();db.m_RadiusDistrib=new AnimationCurve();
                foreach(string name in names) {Transform tf=nodes[name];w.WriteLine("baseline\t"+mode+"\t"+name+"\t"+(tf.parent==component.transform?"":tf.parent.name)+"\t"+tf.GetSiblingIndex()+"\t"+tf.localPosition.ToString("R")+"\t"+tf.localRotation.ToString("R")+"\t"+tf.localScale.ToString("R"));}
                w.Flush();type.GetMethod("SetupParticles",flags).Invoke(db,null);type.GetField("m_Time",flags).SetValue(db,0f);
                IList particles=(IList)type.GetField("m_Particles",flags).GetValue(db);
                for(int i=0;i<particles.Count;++i) {
                    object p=particles[i];Type pt=p.GetType();Transform tf=(Transform)pt.GetField("m_Transform",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic).GetValue(p);
                    w.WriteLine("particle\t"+mode+"\t"+i+"\t"+(tf==null?"null":tf.name)+"\t"+pt.GetField("m_ParentIndex",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic).GetValue(p));
                }
                w.Flush();MethodInfo init=type.GetMethod("InitTransforms",flags),step=type.GetMethod("UpdateDynamicBones",flags);
                for(int frame=0;frame<120;++frame) {init.Invoke(db,null);step.Invoke(db,new object[]{1f/60f});}
                foreach(string name in names) {Transform tf=nodes[name];w.WriteLine("result\t"+mode+"\t"+name+"\t"+tf.localPosition.ToString("R")+"\t"+tf.localRotation.ToString("R"));}
                w.Flush();UnityEngine.Object.Destroy(component);
            }
        }
    }
    private static void RunOrdinaryControl(ChaControl cha) {
        foreach (DynamicBone db in cha.GetComponentsInChildren<DynamicBone>(true)) db.enabled = false;
        GameObject group = new GameObject("__ok_ordinary_test"); group.transform.SetParent(cha.objTop.transform, false);
        GameObject nativePrefab = CommonLib.LoadAsset<GameObject>("chara/oo_base.unity3d", "p_cf_body_bone", false);
        if (nativePrefab == null) throw new Exception("native body collider source missing");
        HashSet<string> nativeNames = new HashSet<string>(Array.ConvertAll(nativePrefab.GetComponentsInChildren<DynamicBoneCollider>(true), delegate(DynamicBoneCollider c) { return c.name; }));
        List<DynamicBone> components = new List<DynamicBone>();
        foreach (string kind in new string[] { "skirt", "hair" }) {
            GameObject component = new GameObject(kind == "skirt" ? "ct_clothesBot" : "ct_hairB");
            component.transform.SetParent(group.transform, false);
            GameObject root = new GameObject(kind == "skirt" ? "cf_J_sk_00_test" : "ok_hair_root"); root.transform.SetParent(component.transform, false);
            root.transform.localPosition = kind == "skirt" ? new Vector3(0.1f, 0.9f, 0.025f) : new Vector3(0.4f, 0.16f, 0.0f);
            Transform previous = root.transform;
            for (int i = 0; i < 2; ++i) {
                GameObject node = new GameObject("ok_" + kind + "_" + i); node.transform.SetParent(previous, false);
                node.transform.localPosition = new Vector3(0.0f, kind == "skirt" ? -0.23f : -0.085f, 0.0f); previous = node.transform;
            }
            DynamicBone db = component.AddComponent<DynamicBone>(); db.enabled = false; db.m_Root = root.transform;
            db.m_Damping = 0.2f; db.m_Elasticity = 0.15f; db.m_Stiffness = 0.05f; db.m_Inert = 0.3f;
            db.m_Radius = 0.012f; db.m_Gravity = new Vector3(0.002f, -0.01f, 0.004f);
            db.m_Force = new Vector3(0.003f, -0.01f, 0.004f);
            db.m_Exclusions = new List<Transform>(); db.m_notRolls = new List<Transform>();
            db.m_DampingDistrib = new AnimationCurve(); db.m_ElasticityDistrib = new AnimationCurve();
            db.m_StiffnessDistrib = new AnimationCurve(); db.m_InertDistrib = new AnimationCurve(); db.m_RadiusDistrib = new AnimationCurve();
            db.m_Colliders = new List<DynamicBoneCollider>();
            foreach (DynamicBoneCollider c in cha.objBodyBone.GetComponentsInChildren<DynamicBoneCollider>(true))
                if (nativeNames.Contains(c.name)) db.m_Colliders.Add(c);
            if (kind == "skirt") {
                DynamicBoneCollider thigh = db.m_Colliders.Find(delegate(DynamicBoneCollider c) { return c.name == "cf_hit_thigh01_L"; });
                if (thigh == null) throw new Exception("native thigh contact target missing");
                root.transform.position = thigh.transform.TransformPoint(thigh.m_Center) + new Vector3(0f, 0.23f, 0.025f);
            } else {
                DynamicBoneCollider floor = Array.Find(cha.objBodyBone.GetComponentsInChildren<DynamicBoneCollider>(true), delegate(DynamicBoneCollider c) { return c.name == "KKS_Colliders_cf_j_root"; });
                if (floor == null) throw new Exception("generated floor contact target missing");
                root.transform.position = floor.transform.TransformPoint(floor.m_Center) + new Vector3(0.4f, floor.m_Radius * Mathf.Abs(floor.transform.lossyScale.z) + 0.13f, 0f);
            }
            components.Add(db);
        }
        ApplyColliderFlags(cha);
        Type dbType = typeof(DynamicBone); BindingFlags fields = BindingFlags.Instance | BindingFlags.NonPublic;
        foreach (DynamicBone db in components) {
            dbType.GetMethod("SetupParticles", fields).Invoke(db, null);
            dbType.GetField("m_Time", fields).SetValue(db, 0f);
        }
        WriteTransforms(cha, ".ordinary-baseline.tsv");
        string output = Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT");
        using (StreamWriter w = new StreamWriter(output + ".ordinary-setup.tsv")) {
            foreach (DynamicBone db in components) {
                w.WriteLine("component\t" + PathOf(db.transform) + "\t" + PathOf(db.m_Root) + "\t" + (int)db.m_FreezeAxis);
                foreach (DynamicBoneCollider c in db.m_Colliders)
                    w.WriteLine("collider\t" + db.name + "\t" + PathOf(c.transform) + "\t" + c.m_Radius.ToString("R") + "\t" + c.m_Height.ToString("R") + "\t" + c.m_Center.ToString("R") + "\t" + (int)c.m_Direction + "\t" + (int)c.m_Bound);
            }
        }
        Transform[] frozen = cha.GetComponentsInChildren<Transform>(true);
        Vector3[] positions = Array.ConvertAll(frozen, delegate(Transform tf) { return tf.localPosition; });
        Quaternion[] rotations = Array.ConvertAll(frozen, delegate(Transform tf) { return tf.localRotation; });
        Vector3[] scales = Array.ConvertAll(frozen, delegate(Transform tf) { return tf.localScale; });
        MethodInfo init = dbType.GetMethod("InitTransforms", fields), step = dbType.GetMethod("UpdateDynamicBones", fields);
        for (int frame = 0; frame < 240; ++frame) {
            for (int i = 0; i < frozen.Length; ++i) { frozen[i].localPosition = positions[i]; frozen[i].localRotation = rotations[i]; frozen[i].localScale = scales[i]; }
            foreach (DynamicBone db in components) init.Invoke(db, null);
            foreach (DynamicBone db in components) step.Invoke(db, new object[] { 1f / 60f });
        }
        WriteTransforms(cha, ".ordinary-result.tsv");
    }
    private static string ExternalScriptSignature(GameObject prefab) {
        List<string> values=new List<string>();
        foreach (MonoBehaviour behaviour in prefab.GetComponentsInChildren<MonoBehaviour>(true)) {
            if (behaviour==null) {values.Add("missing-script");continue;}
            Type type=behaviour.GetType();values.Add(type.FullName+"@"+PathOf(behaviour.transform));
            if (type!=typeof(DynamicBone)&&type!=typeof(DynamicBone_Ver01)&&type!=typeof(DynamicBone_Ver02)&&type!=typeof(DynamicBoneCollider)) continue;
            foreach (FieldInfo field in type.GetFields(BindingFlags.Public|BindingFlags.Instance)) {
                object v=field.GetValue(behaviour);string value;
                if (v is Transform) value=PathOf((Transform)v);
                else if (v is Vector3) value=((Vector3)v).ToString("R");
                else if (v is float) value=((float)v).ToString("R",CultureInfo.InvariantCulture);
                else if (v is bool||v is int||v is Enum) value=v.ToString();
                else if (v is IEnumerable) {
                    List<string> list=new List<string>();
                    foreach (object item in (IEnumerable)v) list.Add(item is Transform?PathOf((Transform)item):item is Component?PathOf(((Component)item).transform):item==null?"null":item.ToString());
                    value=String.Join(",",list.ToArray());
                } else if (v==null) value="null";
                else continue;
                values.Add(type.Name+"."+field.Name+"="+value);
            }
        }
        return String.Join("|",values.ToArray());
    }
    private static void RunExternalScriptControl() {
        string directory=Environment.GetEnvironmentVariable("OK_PROBE_EXTERNAL_SCRIPTS");
        using (StreamWriter writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".external-scripts.tsv")) {
            foreach (string label in new string[]{"body","hair"}) {
                AssetBundle dependency=AssetBundle.LoadFromFile(Path.Combine(directory,label+"_scripts.unity3d"));
                if (dependency==null) throw new Exception("failed loading dependency "+label);
                AssetBundle source=AssetBundle.LoadFromFile(Path.Combine(directory,label+"_source.unity3d"));
                if (source==null) throw new Exception("failed loading source "+label);
                string originalName=label=="body"?"chara/oo_base.unity3d":"chara/ao_hair_00.unity3d";
                AssetBundle original=null;
                foreach(AssetBundle loaded in AssetBundle.GetAllLoadedAssetBundles()) if(loaded.name==originalName) original=loaded;
                bool ownOriginal=original==null;
                if (ownOriginal) original=AssetBundle.LoadFromFile(Path.Combine(@"C:\portable\KoikatuSunshine\abdata",originalName));
                if (original==null) throw new Exception("failed loading original "+label);
                int prefabs=0,ordinary=0,ver01=0,ver02=0,colliders=0;
                foreach(string asset in source.GetAllAssetNames()) {
                    if (!asset.EndsWith(".prefab",StringComparison.OrdinalIgnoreCase)) continue;
                    GameObject actual=source.LoadAsset<GameObject>(asset),expected=original.LoadAsset<GameObject>(asset);
                    if(actual==null||expected==null) throw new Exception("missing prefab "+asset);
                    string a=ExternalScriptSignature(actual),b=ExternalScriptSignature(expected);
                    if(a!=b) throw new Exception("external script signature mismatch "+asset+"\nactual="+a+"\nexpected="+b);
                    ++prefabs;ordinary+=actual.GetComponentsInChildren<DynamicBone>(true).Length;
                    ver01+=actual.GetComponentsInChildren<DynamicBone_Ver01>(true).Length;
                    ver02+=actual.GetComponentsInChildren<DynamicBone_Ver02>(true).Length;
                    colliders+=actual.GetComponentsInChildren<DynamicBoneCollider>(true).Length;
                    writer.WriteLine("prefab\t"+label+"\t"+actual.name+"\t"+a);
                }
                writer.WriteLine("summary\t"+label+"\t"+prefabs+"\t"+ordinary+"\t"+ver01+"\t"+ver02+"\t"+colliders);
                source.Unload(true);dependency.Unload(true);if(ownOriginal) original.Unload(true);
            }
        }
    }
    private static void WriteHandBone(StreamWriter writer, string kind, int index, Transform tf) {
        Vector3 p=tf.localPosition, s=tf.localScale; Quaternion q=tf.localRotation;
        writer.WriteLine(String.Join("\t", new string[] {kind,index.ToString(),tf.name,
            p.x.ToString("R",CultureInfo.InvariantCulture),p.y.ToString("R",CultureInfo.InvariantCulture),p.z.ToString("R",CultureInfo.InvariantCulture),
            q.x.ToString("R",CultureInfo.InvariantCulture),q.y.ToString("R",CultureInfo.InvariantCulture),q.z.ToString("R",CultureInfo.InvariantCulture),q.w.ToString("R",CultureInfo.InvariantCulture),
            s.x.ToString("R",CultureInfo.InvariantCulture),s.y.ToString("R",CultureInfo.InvariantCulture),s.z.ToString("R",CultureInfo.InvariantCulture)}));
    }

    private static void RunHandShapeControl(ChaControl cha) {
        int count=cha.GetShapeIndexCount();
        if(count<=20) throw new Exception("native hand shape count missing");
        cha.animBody.enabled=false; cha.enabled=false;
        if(cha.expression!=null) cha.expression.enabled=false;
        Transform[] all=cha.objBodyBone.GetComponentsInChildren<Transform>(true);
        string[] names=Enum.GetNames(typeof(ShapeHandInfo.DstName));
        List<Transform> selected=new List<Transform>();
        foreach(string name in names) {
            Transform tf=Array.Find(all,delegate(Transform t){return t.name==name;});
            if(tf==null) throw new Exception("missing hand target "+name);
            selected.Add(tf);
        }
        foreach(string name in new string[]{"cf_j_hand_L","cf_j_hand_R","cf_j_forearm01_L","cf_j_forearm01_R"})
            selected.Add(Array.Find(all,delegate(Transform t){return t.name==name;}));
        Quaternion[] seeds=new Quaternion[selected.Count];
        for(int i=0;i<selected.Count;i++) {
            if(selected[i]==null) throw new Exception("nonfinger control target missing");
            seeds[i]=Quaternion.Euler(7f+i*1.7f,3f-i*.4f,8f);
        }
        using(StreamWriter writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".hand-shapes.tsv")) {
            writer.WriteLine("count\t"+count);
            for(int i=0;i<selected.Count;i++){selected[i].localRotation=seeds[i];WriteHandBone(writer,"baseline",-1,selected[i]);}
            int serial=0;
            Action<int,bool,int,int,float> sample=delegate(int side,bool enabled,int a,int b,float blend) {
                for(int i=0;i<selected.Count;i++) selected[i].localRotation=seeds[i];
                cha.SetEnableShapeHand(0,false);cha.SetEnableShapeHand(1,false);
                cha.SetShapeHandValue(side,a,b,blend);cha.SetEnableShapeHand(side,enabled);
                cha.UpdateAlwaysShapeHand();
                writer.WriteLine(String.Join("\t",new string[]{"case",serial.ToString(),side.ToString(),enabled?"1":"0",a.ToString(),b.ToString(),blend.ToString("R",CultureInfo.InvariantCulture)}));
                foreach(Transform tf in selected) WriteHandBone(writer,"bone",serial,tf);
                serial++;
            };
            for(int side=0;side<2;side++) {
                for(int i=0;i<count;i++) sample(side,true,i,0,0f);
                sample(side,true,1,count-1,.3f);sample(side,true,count-1,1,.8f);
                sample(side,true,1,count-1,-1f);sample(side,true,1,count-1,2f);
                sample(side,false,1,0,0f);
            }
            writer.WriteLine("cases\t"+serial);
        }
    }

    private static void CaptureMouthFace(ChaControl cha, string path, bool fullBody=false) {
        Camera sourceCamera=Camera.main;
        if(sourceCamera==null) throw new Exception("no source main camera; cannot capture face overlays");
        // Unity's skinned renderer bounds may cover the whole character. Use
        // the authoritative face target's actual baked geometry for framing.
        SkinnedMeshRenderer face=cha.mouthCtrl.FBSTarget[0].GetSkinnedMeshRenderer();
        Mesh faceBake=new Mesh();face.BakeMesh(faceBake);
        Vector3[] faceVertices=faceBake.vertices;
        if(faceVertices.Length==0) throw new Exception("empty face geometry");
        Bounds bounds=new Bounds(face.transform.TransformPoint(faceVertices[0]),Vector3.zero);
        foreach(Vector3 vertex in faceVertices) bounds.Encapsulate(face.transform.TransformPoint(vertex));
        UnityEngine.Object.Destroy(faceBake);
        if(fullBody) {
            bool found=false;
            foreach(Renderer renderer in cha.GetComponentsInChildren<Renderer>(true)) {
                if(!renderer.enabled || !renderer.gameObject.activeInHierarchy)continue;
                Mesh baked=null;Mesh mesh=null;
                var skin=renderer as SkinnedMeshRenderer;
                if(skin!=null) {baked=new Mesh();skin.BakeMesh(baked);mesh=baked;}
                else {var filter=renderer.GetComponent<MeshFilter>();if(filter!=null)mesh=filter.sharedMesh;}
                if(mesh!=null)foreach(Vector3 vertex in mesh.vertices) {
                    Vector3 world=renderer.transform.TransformPoint(vertex);
                    if(!found){bounds=new Bounds(world,Vector3.zero);found=true;}else bounds.Encapsulate(world);
                }
                if(baked!=null)UnityEngine.Object.Destroy(baked);
            }
            if(!found)throw new Exception("empty visible character geometry");
        }
        GameObject cameraObject=new GameObject("OpenKoikatsu mouth source camera");
        Camera camera=cameraObject.AddComponent<Camera>();
        camera.CopyFrom(sourceCamera);
        camera.enabled=false;
        camera.orthographic=false;camera.fieldOfView=28f;
        camera.rect=new Rect(0f,0f,1f,1f);camera.aspect=900f/1080f;
        camera.ResetProjectionMatrix();
        camera.clearFlags=CameraClearFlags.SolidColor;camera.backgroundColor=Color.gray;
        camera.nearClipPlane=.01f;camera.farClipPlane=20f;
        float distance=bounds.extents.y*1.4f/Mathf.Tan(camera.fieldOfView*.5f*Mathf.Deg2Rad);
        camera.transform.position=bounds.center+new Vector3(0f,0f,distance);
        camera.transform.LookAt(bounds.center);
        using(StreamWriter metadata=new StreamWriter(path+".renderers.tsv")) {
            metadata.WriteLine("camera\t"+camera.actualRenderingPath+"\t"+camera.cullingMask+"\t"+camera.aspect+"\t"+distance);
            metadata.WriteLine("capture_camera\tsource_main\t"+sourceCamera.name);
            foreach(UnityEngine.Rendering.CameraEvent ev in Enum.GetValues(typeof(UnityEngine.Rendering.CameraEvent)))
                foreach(UnityEngine.Rendering.CommandBuffer buffer in sourceCamera.GetCommandBuffers(ev))
                    metadata.WriteLine("source_camera_buffer\t"+ev+"\t"+buffer.name);
            foreach(Component component in sourceCamera.GetComponents<Component>())
                if(component!=null) metadata.WriteLine("source_camera_component\t"+component.GetType().FullName);
            foreach(SkinnedMeshRenderer renderer in cha.objHead.GetComponentsInChildren<SkinnedMeshRenderer>(true)) {
                metadata.WriteLine("renderer\t"+renderer.name+"\t"+renderer.enabled+"\t"+renderer.gameObject.activeInHierarchy+"\t"+renderer.gameObject.layer);
                if(renderer.name.Contains("mayuge") || renderer.name.Contains("eyeline"))
                    foreach(Component component in renderer.GetComponents<Component>())
                        if(component!=null) metadata.WriteLine("renderer_component\t"+renderer.name+"\t"+component.GetType().FullName);
                foreach(Material material in renderer.sharedMaterials)
                    if(material!=null) {
                        metadata.WriteLine("material\t"+renderer.name+"\t"+material.name+"\t"+material.shader.name+"\t"+material.renderQueue);
                        if(renderer.name.Contains("mayuge") || renderer.name.Contains("eyeline")) {
                            metadata.WriteLine("color\t"+renderer.name+"\t"+material.name+"\t"+(material.HasProperty("_Color")?material.GetColor("_Color").ToString("F6"):"none"));
                            metadata.WriteLine("texture\t"+renderer.name+"\t"+material.name+"\t"+(material.mainTexture==null?"null":material.mainTexture.name));
                        }
                    }
                if(renderer.name.Contains("mayuge") || renderer.name.Contains("eyeline"))
                    for(int channel=0;channel<renderer.sharedMesh.blendShapeCount;channel++)
                        metadata.WriteLine("channel\t"+renderer.name+"\t"+renderer.sharedMesh.GetBlendShapeName(channel)+"\t"+renderer.GetBlendShapeWeight(channel).ToString("R",CultureInfo.InvariantCulture));
            }
        }
        RenderTexture target=new RenderTexture(900,1080,24);camera.targetTexture=target;
        RenderTexture previous=RenderTexture.active;RenderTexture.active=target;
        Texture2D capture=new Texture2D(900,1080,TextureFormat.RGB24,false);
        GameObject savedObject=new GameObject("OpenKoikatsu original camera backup");
        Camera saved=savedObject.AddComponent<Camera>();saved.enabled=false;saved.CopyFrom(sourceCamera);
        Vector3 savedPosition=sourceCamera.transform.position;
        Quaternion savedRotation=sourceCamera.transform.rotation;
        RenderTexture savedTarget=sourceCamera.targetTexture;
        try {
            sourceCamera.CopyFrom(camera);
            sourceCamera.transform.position=camera.transform.position;sourceCamera.transform.rotation=camera.transform.rotation;
            sourceCamera.targetTexture=target;sourceCamera.Render();
            RenderTexture.active=target;capture.ReadPixels(new Rect(0,0,900,1080),0,0);capture.Apply();
            File.WriteAllBytes(path,capture.EncodeToPNG());
        } finally {
            sourceCamera.CopyFrom(saved);sourceCamera.transform.position=savedPosition;sourceCamera.transform.rotation=savedRotation;
            sourceCamera.targetTexture=savedTarget;
            RenderTexture.active=previous;camera.targetTexture=null;
            UnityEngine.Object.Destroy(savedObject);UnityEngine.Object.Destroy(capture);
            UnityEngine.Object.Destroy(target);UnityEngine.Object.Destroy(cameraObject);
        }
    }
    private static IEnumerator RunMouthShapeControl(ChaControl cha) {
        cha.animBody.enabled=false;cha.enabled=false;
        if(cha.expression!=null) cha.expression.enabled=false;
        FaceBlendShape fbs=cha.fbsCtrl;
        if(fbs==null) throw new Exception("loaded head has no FaceBlendShape");
        using(StreamWriter metadata=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".fbs-metadata.tsv")) {
            metadata.WriteLine("head\t"+PathOf(cha.objHead.transform));
            metadata.WriteLine("active_fbs\t"+PathOf(fbs.transform)+"\t"+fbs.MouthCtrl.FBSTarget.Length+"\t"+System.Object.ReferenceEquals(cha.mouthCtrl,fbs.MouthCtrl));
            foreach(FaceBlendShape component in cha.GetComponentsInChildren<FaceBlendShape>(true))
                metadata.WriteLine("fbs\t"+PathOf(component.transform)+"\t"+component.enabled+"\t"+component.MouthCtrl.FBSTarget.Length);
            foreach(SkinnedMeshRenderer renderer in cha.GetComponentsInChildren<SkinnedMeshRenderer>(true))
                metadata.WriteLine("renderer\t"+PathOf(renderer.transform)+"\t"+(renderer.sharedMesh==null?"null":renderer.sharedMesh.name)+"\t"+(renderer.sharedMesh==null?0:renderer.sharedMesh.blendShapeCount));
        }
        if(fbs.MouthCtrl.FBSTarget.Length==0 || !System.Object.ReferenceEquals(cha.mouthCtrl,fbs.MouthCtrl))
            throw new Exception("no authoritative FBS mouth targets; metadata exported, no positive control captured");
        fbs.enabled=false;
        // Freeze the loaded card's neutral eyes/brows, rather than whichever
        // random blink happened to be active when this coroutine started.
        fbs.EyesCtrl.ChangePtn(0,false);fbs.EyesCtrl.SetFixedRate(fbs.EyesCtrl.OpenMax);fbs.EyesCtrl.CalcBlend(1f);
        fbs.EyebrowCtrl.ChangePtn(0,false);fbs.EyebrowCtrl.SetFixedRate(fbs.EyebrowCtrl.OpenMax);fbs.EyebrowCtrl.CalcBlend(1f);
        fbs.MouthCtrl.UseAdjustWidthScale(false);
        if(fbs.MouthCtrl.objAdjustWidthScale!=null) fbs.MouthCtrl.objAdjustWidthScale.transform.localScale=Vector3.one;
        string output=Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT");
        WriteTransforms(cha,".mouth-transforms.tsv");
        using(StreamWriter worldWriter=new StreamWriter(output+".mouth-world.tsv")) {
        worldWriter.WriteLine("target_count\t"+fbs.MouthCtrl.FBSTarget.Length);
        using(StreamWriter writer=new StreamWriter(output+".mouth-shapes.tsv")) {
        writer.WriteLine("target_count\t"+fbs.MouthCtrl.FBSTarget.Length);
        for(int index=0;index<fbs.MouthCtrl.FBSTarget.Length;index++) {
            var target=fbs.MouthCtrl.FBSTarget[index];
            SkinnedMeshRenderer renderer=target.GetSkinnedMeshRenderer();
            if(renderer==null || renderer.sharedMesh==null) throw new Exception("missing native mouth target "+index);
            writer.WriteLine("target\t"+index+"\t"+renderer.name+"\t"+renderer.sharedMesh.vertexCount+"\t"+renderer.sharedMesh.blendShapeCount+"\t"+target.PtnSet.Length);
            for(int pattern=0;pattern<target.PtnSet.Length;pattern++)
                writer.WriteLine("pattern\t"+index+"\t"+pattern+"\t"+target.PtnSet[pattern].Close+"\t"+target.PtnSet[pattern].Open);
        }
        int serial=0;
        foreach(int pattern in new int[]{0,8}) foreach(float rate in new float[]{0f,.8f,.61337f,1f,.00999f}) {
            cha.ChangeMouthPtn(pattern,false);fbs.MouthCtrl.UseAdjustWidthScale(false);
            fbs.MouthCtrl.SetFixedRate(rate);fbs.MouthCtrl.CalcBlend(rate);
            // BakeMesh reads weights immediately, while visible skinning is
            // submitted later in the frame. Capture the actual updated renderer.
            yield return new WaitForEndOfFrame();
            writer.WriteLine("case\t"+serial+"\t"+pattern+"\t"+rate.ToString("R",CultureInfo.InvariantCulture));
            for(int index=0;index<fbs.MouthCtrl.FBSTarget.Length;index++) {
                SkinnedMeshRenderer renderer=fbs.MouthCtrl.FBSTarget[index].GetSkinnedMeshRenderer();
                if(renderer==null) throw new Exception("missing native mouth target "+index);
                Mesh source=renderer.sharedMesh;Mesh baked=new Mesh();renderer.BakeMesh(baked);
                // The actual vertex shader skins boneWorld * bindpose. BakeMesh
                // followed by renderer.TransformPoint can apply renderer scale
                // twice, so export direct LBS as a separate world-space control.
                Vector3[] shaped=source.vertices;
                for(int channel=0;channel<source.blendShapeCount;channel++) {
                    float weight=renderer.GetBlendShapeWeight(channel);
                    if(weight==0f) continue;
                    if(source.GetBlendShapeFrameCount(channel)!=1 || source.GetBlendShapeFrameWeight(channel,0)!=100f)
                        throw new Exception("world control requires single authored 100-percent frame: "+source.GetBlendShapeName(channel));
                    Vector3[] dp=new Vector3[shaped.Length],dn=new Vector3[shaped.Length],dt=new Vector3[shaped.Length];
                    source.GetBlendShapeFrameVertices(channel,0,dp,dn,dt);
                    for(int vertex=0;vertex<shaped.Length;vertex++) shaped[vertex]+=dp[vertex]*(weight/100f);
                }
                Transform[] skinBones=renderer.bones;Matrix4x4[] bindposes=source.bindposes;
                BoneWeight[] weights=source.boneWeights;
                worldWriter.WriteLine("renderer\t"+serial+"\t"+index+"\t"+renderer.name+"\t"+renderer.quality+"\t"+renderer.gameObject.activeInHierarchy);
                for(int bone=0;bone<skinBones.Length;bone++) {
                    if(skinBones[bone]==null || bone>=bindposes.Length) continue;
                    Matrix4x4 world=skinBones[bone].localToWorldMatrix,bind=bindposes[bone];
                    string worldLine="bone_world\t"+serial+"\t"+index+"\t"+bone+"\t"+skinBones[bone].name;
                    string bindLine="bind\t"+serial+"\t"+index+"\t"+bone+"\t"+skinBones[bone].name;
                    for(int row=0;row<4;row++) for(int column=0;column<4;column++) {
                        worldLine+="\t"+world[row,column].ToString("R",CultureInfo.InvariantCulture);
                        bindLine+="\t"+bind[row,column].ToString("R",CultureInfo.InvariantCulture);
                    }
                    worldWriter.WriteLine(worldLine);worldWriter.WriteLine(bindLine);
                }
                for(int vertex=0;vertex<shaped.Length;vertex++) {
                    BoneWeight bw=weights[vertex];
                    int[] boneIndices={bw.boneIndex0,bw.boneIndex1,bw.boneIndex2,bw.boneIndex3};
                    float[] amounts={bw.weight0,bw.weight1,bw.weight2,bw.weight3};
                    Vector3 v=Vector3.zero;float sum=0f;
                    for(int slot=0;slot<4;slot++) {
                        if(amounts[slot]==0f) continue;
                        int bone=boneIndices[slot];
                        if(bone>=skinBones.Length || bone>=bindposes.Length || skinBones[bone]==null) throw new Exception("invalid weighted source bone");
                        v+=(skinBones[bone].localToWorldMatrix*bindposes[bone]).MultiplyPoint3x4(shaped[vertex])*amounts[slot];sum+=amounts[slot];
                    }
                    if(Mathf.Abs(sum-1f)>1e-5f) throw new Exception("world control source weights do not sum to one");
                    worldWriter.WriteLine("world_vertex\t"+serial+"\t"+index+"\t"+vertex+"\t"+v.x.ToString("R",CultureInfo.InvariantCulture)+"\t"+v.y.ToString("R",CultureInfo.InvariantCulture)+"\t"+v.z.ToString("R",CultureInfo.InvariantCulture));
                }
                for(int channel=0;channel<source.blendShapeCount;channel++)
                    writer.WriteLine("channel\t"+serial+"\t"+index+"\t"+channel+"\t"+source.GetBlendShapeName(channel)+"\t"+renderer.GetBlendShapeWeight(channel).ToString("R",CultureInfo.InvariantCulture));
                Vector3[] vertices=baked.vertices;
                for(int vertex=0;vertex<vertices.Length;vertex++) {
                    Vector3 v=vertices[vertex];
                    writer.WriteLine("vertex\t"+serial+"\t"+index+"\t"+vertex+"\t"+v.x.ToString("R",CultureInfo.InvariantCulture)+"\t"+v.y.ToString("R",CultureInfo.InvariantCulture)+"\t"+v.z.ToString("R",CultureInfo.InvariantCulture));
                }
                UnityEngine.Object.Destroy(baked);
            }
            string suffix=".mouth-"+pattern+"-"+rate.ToString("R",CultureInfo.InvariantCulture);
            if(rate==.8f) CaptureMouthFace(cha,output+suffix+".png");
            serial++;
        }
        writer.WriteLine("cases\t"+serial);
        worldWriter.WriteLine("cases\t"+serial);
        }
        }
    }

    private static IEnumerator RunEyeBrowControl(ChaControl cha) {
        FaceBlendShape fbs=cha.fbsCtrl;
        if(fbs==null) throw new Exception("loaded head has no FaceBlendShape");
        cha.animBody.enabled=false;cha.enabled=false;fbs.enabled=false;
        if(cha.expression!=null) cha.expression.enabled=false;
        FBSBase[] controls={fbs.MouthCtrl,fbs.EyesCtrl,fbs.EyebrowCtrl};
        Dictionary<string,SkinnedMeshRenderer> renderers=new Dictionary<string,SkinnedMeshRenderer>();
        using(StreamWriter writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".face-controls.tsv")) {
            writer.WriteLine("default	1	"+fbs.EyesCtrl.OpenMax.ToString("R",CultureInfo.InvariantCulture));
            writer.WriteLine("default	2	"+fbs.EyebrowCtrl.OpenMax.ToString("R",CultureInfo.InvariantCulture));
            for(int control=0;control<3;control++) {
                foreach(var target in controls[control].FBSTarget) {
                    SkinnedMeshRenderer renderer=target.GetSkinnedMeshRenderer();
                    if(renderer==null || renderer.sharedMesh==null) throw new Exception("missing expression target");
                    renderers[renderer.name]=renderer;
                    writer.WriteLine("target	"+control+"	"+renderer.name+"	"+target.PtnSet.Length+"	"+renderer.sharedMesh.blendShapeCount);
                    for(int pattern=0;pattern<target.PtnSet.Length;pattern++)
                        writer.WriteLine("pattern	"+control+"	"+renderer.name+"	"+pattern+"	"+target.PtnSet[pattern].Close+"	"+target.PtnSet[pattern].Open);
                }
            }
            foreach(var entry in renderers)
                for(int channel=0;channel<entry.Value.sharedMesh.blendShapeCount;channel++)
                    writer.WriteLine("name	"+entry.Key+"	"+channel+"	"+entry.Value.sharedMesh.GetBlendShapeName(channel));
            int cases=0;
            for(int control=1;control<=2;control++) for(int pattern=0;pattern<controls[control].GetMaxPtn();pattern++)
                foreach(float rate in new float[]{0f,.61337f,1f}) {
                    for(int reset=0;reset<3;reset++) {
                        controls[reset].ChangePtn(0,false);
                        controls[reset].SetFixedRate(reset==0?0f:controls[reset].OpenMax);
                    }
                    controls[control].ChangePtn(pattern,false);controls[control].SetFixedRate(rate);
                    fbs.EyebrowCtrl.CalcBlend(1f);fbs.EyesCtrl.CalcBlend(1f);fbs.MouthCtrl.CalcBlend(0f);
                    yield return new WaitForEndOfFrame();
                    writer.WriteLine("case	"+cases+"	"+control+"	"+pattern+"	"+rate.ToString("R",CultureInfo.InvariantCulture));
                    foreach(var entry in renderers)
                        for(int channel=0;channel<entry.Value.sharedMesh.blendShapeCount;channel++)
                            writer.WriteLine("weight	"+cases+"	"+entry.Key+"	"+channel+"	"+entry.Value.GetBlendShapeWeight(channel).ToString("R",CultureInfo.InvariantCulture));
                    cases++;
                }
            writer.WriteLine("cases	"+cases);
        }
    }

    private static object FbsField(object value, string name) {
        Type type=value.GetType();
        while(type!=null) {
            FieldInfo field=type.GetField(name,BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.DeclaredOnly);
            if(field!=null) return field.GetValue(value);
            type=type.BaseType;
        }
        throw new Exception("missing FBS field "+name);
    }
    private static string Num(float value) { return value.ToString("R",CultureInfo.InvariantCulture); }
    private static float[] EyeSettings(EyeLookMaterialControll eye) {
        Vector2 offset=eye.GetEyeTexOffset(),scale=eye.GetEyeTexScale();
        return new[]{(float)eye.eyeLR,eye.InsideWait,eye.OutsideWait,eye.UpWait,eye.DownWait,
            eye.InsideLimit,eye.OutsideLimit,eye.UpLimit,eye.DownLimit,eye.power,offset.x,offset.y,scale.x,scale.y};
    }
    private static void WriteEyeMaterial(StreamWriter writer,EyeLookMaterialControll eye,string tag,int id) {
        Material mat=(Material)FbsField(eye,"_material");
        Vector2 scale=mat.GetTextureScale("_MainTex"),offset=mat.GetTextureOffset("_MainTex");
        writer.WriteLine(tag+"\t"+id+"\t"+eye._renderer.name+"\t"+Num(scale.x)+"\t"+Num(scale.y)+"\t"+Num(offset.x)+"\t"+Num(offset.y));
    }
    [Serializable] public class LookEye {public string name; public int lr;}
    [Serializable] public class LookPattern {public int kind; public float[] values;}
    [Serializable] public class LookConfig {
        public string root,center;public LookEye[] eyes;public LookPattern[] patterns;
        public float[] look,up;public bool correct;public float separation,away,control_distance;
    }
    [Serializable] public class LookFrame {
        public int pattern;public bool enabled;public float dt;
        public float[] root,rotation,center,center_rotation,positions,parents,input,control,angles,rates,dir_up,rotations,target;
    }
    [Serializable] public class LookTrace {
        public LookConfig config;public float[] reference,up,original;public LookFrame[] frames;
        public int renderers,bone_slots,weighted_eye_slots;
    }
    private static float[] XYZ(Vector3 v) {return new float[]{v.x,v.y,v.z};}
    private static float[] XYZW(Quaternion v) {return new float[]{v.x,v.y,v.z,v.w};}
    private static float[] Flatten(params float[][] vectors) {var result=new List<float>();foreach(var v in vectors) result.AddRange(v);return result.ToArray();}
    private static float[] Matrix(Matrix4x4 m) {var result=new float[16];for(int i=0;i<16;i++)result[i]=m[i/4,i%4];return result;}
    // This game's Unity serializer omits plugin-defined nested object fields.
    // Emit the complete diagnostic tree explicitly and validate frame counts.
    private static string ProbeJson(object value) {
        if(value==null)return "null";
        if(value is string)return "\""+((string)value).Replace("\\","\\\\").Replace("\"","\\\"")+"\"";
        if(value is bool)return (bool)value?"true":"false";
        if(value is float)return Num((float)value);
        if(value is int)return ((int)value).ToString(CultureInfo.InvariantCulture);
        if(value is byte)return ((byte)value).ToString(CultureInfo.InvariantCulture);
        if(value is BoneWeight) {
            var w=(BoneWeight)value;
            return "{\"boneIndex0\":"+w.boneIndex0+",\"boneIndex1\":"+w.boneIndex1+",\"boneIndex2\":"+w.boneIndex2+",\"boneIndex3\":"+w.boneIndex3+",\"weight0\":"+Num(w.weight0)+",\"weight1\":"+Num(w.weight1)+",\"weight2\":"+Num(w.weight2)+",\"weight3\":"+Num(w.weight3)+"}";
        }
        var parts=new List<string>();var array=value as IEnumerable;
        if(array!=null) {foreach(var item in array)parts.Add(ProbeJson(item));return "["+String.Join(",",parts.ToArray())+"]";}
        foreach(var field in value.GetType().GetFields(BindingFlags.Instance|BindingFlags.Public))parts.Add(ProbeJson(field.Name)+":"+ProbeJson(field.GetValue(value)));
        return "{"+String.Join(",",parts.ToArray())+"}";
    }
    private static IEnumerator RunEyeLook(ChaControl cha) {
        WriteTransforms(cha,".eye-look-transforms.tsv");
        var fbs=cha.fbsCtrl;var calc=fbs.EyeLookController.eyeLookScript;
        cha.animBody.enabled=false;cha.enabled=false;fbs.enabled=false;calc.enabled=false;fbs.EyeLookController.enabled=false;
        if(cha.expression!=null)cha.expression.enabled=false;
        foreach(var db in cha.GetComponentsInChildren<DynamicBone>(true))db.enabled=false;
        foreach(var db in cha.GetComponentsInChildren<DynamicBone_Ver02>(true))db.enabled=false;
        BindingFlags flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
        Func<object,string,object> get=(o,n)=>o.GetType().GetField(n,flags).GetValue(o);
        Action<object,string,object> set=(o,n,v)=>o.GetType().GetField(n,flags).SetValue(o,v);
        var eyes=calc.eyeObjs;var center=(Transform)get(calc,"trfCenter");
        var config=new LookConfig{root=calc.rootNode.name,center=center.name,eyes=new LookEye[2],patterns=new LookPattern[calc.eyeTypeStates.Length],
            look=XYZ(calc.headLookVector),up=XYZ(calc.headUpVector),correct=calc.correct,separation=(float)get(calc,"centerEyeLength"),away=calc.sorasiRate,control_distance=calc.targetObjMaxDir};
        var trace=new LookTrace{config=config};
        trace.reference=Flatten(XYZ((Vector3)get(eyes[0],"referenceLookDir")),XYZ((Vector3)get(eyes[1],"referenceLookDir")));
        trace.up=Flatten(XYZ((Vector3)get(eyes[0],"referenceUpDir")),XYZ((Vector3)get(eyes[1],"referenceUpDir")));
        trace.original=Flatten(XYZW((Quaternion)get(eyes[0],"origRotation")),XYZW((Quaternion)get(eyes[1],"origRotation")));
        for(int i=0;i<2;i++) {
            config.eyes[i]=new LookEye{name=eyes[i].eyeTransform.name,lr=(int)eyes[i].eyeLR};
            set(eyes[i],"angleH",0f);set(eyes[i],"angleV",0f);set(eyes[i],"dirUp",(Vector3)get(eyes[i],"referenceUpDir"));
            eyes[i].eyeTransform.localRotation=(Quaternion)get(eyes[i],"origRotation");
        }
        set(calc,"fixAngle",new Quaternion[]{eyes[0].eyeTransform.localRotation,eyes[1].eyeTransform.localRotation});
        for(int i=0;i<config.patterns.Length;i++) {
            var p=calc.eyeTypeStates[i];config.patterns[i]=new LookPattern{kind=(int)p.lookType,values=new float[]{p.thresholdAngleDifference,p.bendingMultiplier,p.maxAngleDifference,p.minBendingAngle,p.maxBendingAngle,p.upBendingAngle,p.downBendingAngle,p.nearDis,p.leapSpeed,p.forntTagDis,p.hAngleLimit,p.vAngleLimit}};
        }
        foreach(var r in cha.GetComponentsInChildren<SkinnedMeshRenderer>(true)) {
            trace.renderers++;var bones=r.bones;trace.bone_slots+=bones.Length;
            foreach(var weights in r.sharedMesh.boneWeights) {
                int[] indices={weights.boneIndex0,weights.boneIndex1,weights.boneIndex2,weights.boneIndex3};float[] values={weights.weight0,weights.weight1,weights.weight2,weights.weight3};
                for(int slot=0;slot<4;slot++)if(values[slot]>0&&indices[slot]<bones.Length&&bones[indices[slot]]!=null)
                    for(int i=0;i<2;i++)if(bones[indices[slot]]==eyes[i].eyeTransform||bones[indices[slot]].IsChildOf(eyes[i].eyeTransform))trace.weighted_eye_slots++;
            }
        }
        var frames=new List<LookFrame>();
        var control=new GameObject("__ok_eye_control");calc.targetObj=control;
        control.transform.position=calc.rootNode.TransformPoint(new Vector3(.3f,.15f,1f));
        Vector3[] targets={new Vector3(0f,0f,2f),new Vector3(.7f,.3f,1f),new Vector3(-.8f,-.6f,1f),new Vector3(.05f,.1f,.1f),new Vector3(.3f,.4f,-2f),new Vector3(0f,2f,0f)};
        // Preserve native Awake references. Exercise all states, then a rotated,
        // nonuniformly scaled head; record actual world bases before every step.
        for(int phase=0;phase<2;phase++) {
            if(phase==1) {calc.rootNode.localRotation*=Quaternion.Euler(13f,-17f,8f);calc.rootNode.localScale=Vector3.Scale(calc.rootNode.localScale,new Vector3(.83f,1.12f,.91f));}
            for(int p=0;p<config.patterns.Length;p++)for(int t=0;t<targets.Length;t++)for(int repeat=0;repeat<2;repeat++) {
                yield return new WaitForEndOfFrame();
                Vector3 input=calc.rootNode.TransformPoint(targets[t]);calc.setEnable(!(p==4&&t==1&&repeat==0));
                var frame=new LookFrame{pattern=p,enabled=!(p==4&&t==1&&repeat==0),dt=Time.deltaTime,
                    root=XYZ(calc.rootNode.position),rotation=XYZW(calc.rootNode.rotation),center=Matrix(center.localToWorldMatrix),center_rotation=XYZW(center.rotation),
                    positions=Flatten(XYZ(eyes[0].eyeTransform.position),XYZ(eyes[1].eyeTransform.position)),parents=Flatten(XYZW(eyes[0].eyeTransform.parent.rotation),XYZW(eyes[1].eyeTransform.parent.rotation)),input=XYZ(input),control=XYZ(control.transform.position)};
                calc.EyeUpdateCalc(input,p);
                frame.angles=new float[]{(float)get(eyes[0],"angleH"),(float)get(eyes[0],"angleV"),(float)get(eyes[1],"angleH"),(float)get(eyes[1],"angleV")};
                frame.rates=new float[]{calc.GetAngleHRate(EYE_LR.EYE_L),calc.GetAngleHRate(EYE_LR.EYE_R),calc.GetAngleVRate()};
                frame.dir_up=Flatten(XYZ((Vector3)get(eyes[0],"dirUp")),XYZ((Vector3)get(eyes[1],"dirUp")));
                frame.rotations=Flatten(XYZW(eyes[0].eyeTransform.localRotation),XYZW(eyes[1].eyeTransform.localRotation));frame.target=XYZ((Vector3)get(calc,"targetPos"));
                frames.Add(frame);
            }
        }
        trace.frames=frames.ToArray();File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".eye-look.json",ProbeJson(trace));
        UnityEngine.Object.Destroy(control);
    }
    private static IEnumerator RunEyeGaze(ChaControl cha) {
        FaceBlendShape fbs=cha.fbsCtrl;
        if(fbs==null||fbs.EyeLookController==null) throw new Exception("loaded head lacks source gaze controller");
        EyeLookCalc calc=fbs.EyeLookController.eyeLookScript;
        cha.animBody.enabled=false;cha.enabled=false;fbs.enabled=false;calc.enabled=false;fbs.EyeLookController.enabled=false;
        if(cha.expression!=null) cha.expression.enabled=false;
        var eyes=cha.objHead.GetComponentsInChildren<EyeLookMaterialControll>(true);
        if(eyes.Length!=2) throw new Exception("expected actual two iris controllers, got "+eyes.Length);
        MethodInfo late=typeof(FaceBlendShape).GetMethod("LateUpdate",BindingFlags.Instance|BindingFlags.NonPublic);
        MethodInfo update=typeof(EyeLookMaterialControll).GetMethod("Update",BindingFlags.Instance|BindingFlags.NonPublic);
        FBSBase[] controls={fbs.MouthCtrl,fbs.EyesCtrl,fbs.EyebrowCtrl};
        Dictionary<string,SkinnedMeshRenderer> renderers=new Dictionary<string,SkinnedMeshRenderer>();
        using(StreamWriter writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".eye-gaze.tsv")) {
            float[] settings={fbs.BlinkCtrl.BlinkFrequency,fbs.BlinkCtrl.BaseSpeed,fbs.EyebrowCtrl.SyncBlink?1f:0f,
                cha.fileStatus.eyesBlink?1f:0f,fbs.EyesCtrl.OpenMin,fbs.EyebrowCtrl.OpenMin,fbs.EyesCtrl.FixedRate,fbs.EyebrowCtrl.FixedRate,
                fbs.EyeLookUpCorrect,fbs.EyeLookDownCorrect,fbs.EyeLookSideCorrect,1f};
            for(int i=0;i<settings.Length;i++) writer.WriteLine("setting\t"+i+"\t"+Num(settings[i]));
            for(int control=0;control<3;control++) foreach(var target in controls[control].FBSTarget) {
                var r=target.GetSkinnedMeshRenderer();renderers[r.name]=r;
                for(int p=0;p<target.PtnSet.Length;p++) writer.WriteLine("pattern\t"+control+"\t"+r.name+"\t"+p+"\t"+target.PtnSet[p].Close+"\t"+target.PtnSet[p].Open);
            }
            foreach(var entry in renderers) for(int c=0;c<entry.Value.sharedMesh.blendShapeCount;c++)
                writer.WriteLine("name\t"+entry.Key+"\t"+c+"\t"+entry.Value.sharedMesh.GetBlendShapeName(c));
            foreach(var eye in eyes) {
                eye.enabled=false;
                if(eye.script!=calc) throw new Exception("eye material not assigned source EyeLookCalc");
                float[] data=EyeSettings(eye);
                for(int i=0;i<data.Length;i++) writer.WriteLine("material-setting\t"+eye._renderer.name+"\t"+i+"\t"+Num(data[i]));
                Mesh mesh=((SkinnedMeshRenderer)eye._renderer).sharedMesh;
                Vector2[] uv=mesh.uv;
                for(int i=0;i<uv.Length;i++) writer.WriteLine("raw-uv\t"+eye._renderer.name+"\t"+i+"\t"+Num(uv[i].x)+"\t"+Num(uv[i].y));
            }
            fbs.BlinkCtrl.SetFixedFlags(1);
            fbs.MouthCtrl.ChangePtn(8,false);fbs.MouthCtrl.SetFixedRate(.8f);
            fbs.EyebrowCtrl.ChangePtn(2,false);fbs.EyebrowCtrl.SetFixedRate(.8f);
            float[] rates={-1f,-.5f,0f,.5f,1f};int cases=0;
            foreach(float h in rates) foreach(float v in rates) foreach(int mirror in new[]{1,-1}) foreach(int mode in new[]{0,1,2,3}) {
                float blink=mode==0?0f:mode==1?.5f:1f,fixedRate=mode==3?.61337f:-.1f;
                calc.angleHRate[0]=h;calc.angleHRate[1]=h*mirror;calc.angleVRate=v;
                int pattern=new[]{0,1,8}[cases%3];fbs.EyesCtrl.ChangePtn(pattern,false);
                fbs.EyesCtrl.SetFixedRate(fixedRate);fbs.EyesCtrl.SetOpenRateForce(blink);
                late.Invoke(fbs,null);
                foreach(var eye in eyes) update.Invoke(eye,null);
                float maximum=(float)FbsField(fbs.EyesCtrl,"correctOpenMax");
                writer.WriteLine("case\t"+cases+"\t"+Num(h)+"\t"+Num(h*mirror)+"\t"+Num(v)+"\t"+Num(blink)+"\t"+Num(fixedRate)+"\t"+pattern+"\t"+Num(fbs.EyesCtrl.OpenMax)+"\t"+Num(maximum));
                foreach(var eye in eyes) WriteEyeMaterial(writer,eye,"st",cases);
                foreach(var entry in renderers) for(int c=0;c<entry.Value.sharedMesh.blendShapeCount;c++)
                    writer.WriteLine("weight\t"+cases+"\t"+entry.Key+"\t"+c+"\t"+Num(entry.Value.GetBlendShapeWeight(c)));
                cases++;
            }
            writer.WriteLine("cases\t"+cases);
            // Native formula controls also cover clamped customScale and nondefault limits.
            int formulas=0;
            foreach(var eye in eyes) foreach(float scale in new[]{-.2f,0f,.8f,1.8f}) foreach(float h in rates) foreach(float v in rates) {
                eye.SetEyeTexScaleX(scale);eye.SetEyeTexScaleY(scale);
                eye.InsideWait=-57;eye.OutsideWait=93;eye.UpWait=-77;eye.DownWait=86;
                eye.InsideLimit=-.08f;eye.OutsideLimit=.12f;eye.UpLimit=-.06f;eye.DownLimit=.09f;eye.power=.0007f;
                calc.angleHRate[0]=calc.angleHRate[1]=h;calc.angleVRate=v;
                update.Invoke(eye,null);
                var data=EyeSettings(eye);string row="formula\t"+formulas+"\t"+Num(h)+"\t"+Num(v);
                foreach(float value in data) row+="\t"+Num(value);
                var mat=(Material)FbsField(eye,"_material");var s=mat.GetTextureScale("_MainTex");var o=mat.GetTextureOffset("_MainTex");
                writer.WriteLine(row+"\t"+Num(s.x)+"\t"+Num(s.y)+"\t"+Num(o.x)+"\t"+Num(o.y));formulas++;
            }
            writer.WriteLine("formulas\t"+formulas);
        }
        yield return null;
    }
    private static IEnumerator RunEyeShader(ChaControl cha) {
        var fbs=cha.fbsCtrl;var calc=fbs.EyeLookController.eyeLookScript;
        cha.animBody.enabled=false;cha.enabled=false;fbs.enabled=false;calc.enabled=false;fbs.EyeLookController.enabled=false;
        foreach(var d in cha.GetComponentsInChildren<DynamicBone>(true))d.enabled=false;
        foreach(var d in cha.GetComponentsInChildren<DynamicBone_Ver02>(true))d.enabled=false;
        var eyes=cha.objHead.GetComponentsInChildren<EyeLookMaterialControll>(true);
        if(eyes.Length!=2)throw new Exception("expected two source iris controllers");
        MethodInfo update=typeof(EyeLookMaterialControll).GetMethod("Update",BindingFlags.Instance|BindingFlags.NonPublic);
        using(var writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".eye-shader.tsv")) {
            foreach(var eye in eyes) {
                eye.enabled=false;var cached=(Material)FbsField(eye,"_material");var r=(SkinnedMeshRenderer)eye._renderer;var mat=r.sharedMaterial;
                writer.WriteLine("material_binding\t"+r.name+"\t"+cached.GetInstanceID()+"\t"+mat.GetInstanceID()+"\t"+cached.name+"\t"+mat.name);
                foreach(string p in new[]{"_overtex1","_overtex2"}) {var t=cached.GetTexture(p);writer.WriteLine("cached_texture\t"+r.name+"\t"+p+"\t"+(t==null?"null":t.name)+"\t"+(t==null?0:t.width)+"\t"+(t==null?0:t.height));}
                writer.WriteLine("shader\t"+r.name+"\t"+mat.shader.name);
                foreach(string p in new[]{"_rotation","_rotation1","_rotation2","_isHighLight"})writer.WriteLine("float\t"+r.name+"\t"+p+"\t"+Num(mat.GetFloat(p)));
                foreach(string p in new[]{"_Color","_overcolor1","_overcolor2"}) {
                    Color c=mat.GetColor(p);writer.WriteLine("color\t"+r.name+"\t"+p+"\t"+Num(c.r)+"\t"+Num(c.g)+"\t"+Num(c.b)+"\t"+Num(c.a));
                }
                writer.WriteLine("offset\t"+r.name+"\t"+Num(eye.GetEyeTexHLUpOffsetX())+"\t"+Num(eye.GetEyeTexHLUpOffsetY())+"\t"+Num(eye.GetEyeTexHLDownOffsetX())+"\t"+Num(eye.GetEyeTexHLDownOffsetY()));
                Vector2[][] uv={r.sharedMesh.uv,r.sharedMesh.uv2,r.sharedMesh.uv3};
                for(int layer=0;layer<3;layer++)for(int i=0;i<r.sharedMesh.vertexCount;i++) {
                    Vector2 v=i<uv[layer].Length?uv[layer][i]:Vector2.zero;
                    writer.WriteLine("uv\t"+r.name+"\t"+layer+"\t"+i+"\t"+Num(v.x)+"\t"+Num(v.y));
                }
                foreach(string p in new[]{"_texcoord2_ST","_texcoord3_ST"}) {
                    Vector4 v=mat.GetVector(p);writer.WriteLine("vector\t"+r.name+"\t"+p+"\t"+Num(v.x)+"\t"+Num(v.y)+"\t"+Num(v.z)+"\t"+Num(v.w));
                }
                foreach(string p in new[]{"_overtex1","_overtex2"}) {
                    Texture t=mat.GetTexture(p);writer.WriteLine("texture\t"+r.name+"\t"+p+"\t"+(t==null?"null":t.name)+"\t"+(t==null?-1:(int)t.wrapMode));
                    if(t!=null) {
                        var rt=RenderTexture.GetTemporary(t.width,t.height,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.sRGB);
                        Graphics.Blit(t,rt);var prev=RenderTexture.active;RenderTexture.active=rt;
                        var png=new Texture2D(t.width,t.height,TextureFormat.RGBA32,false,false);png.ReadPixels(new Rect(0,0,t.width,t.height),0,0);png.Apply();
                        File.WriteAllBytes(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+"."+r.name+p+".png",png.EncodeToPNG());
                        RenderTexture.active=prev;UnityEngine.Object.Destroy(png);RenderTexture.ReleaseTemporary(rt);
                    }
                }
            }
            int cases=0;
            foreach(float h in new[]{-1f,-.5f,0f,.5f,1f})foreach(float v in new[]{-1f,-.5f,0f,.5f,1f}) {
                calc.angleHRate[0]=h;calc.angleHRate[1]=-h;calc.angleVRate=v;
                foreach(var eye in eyes) {
                    update.Invoke(eye,null);var mat=(Material)FbsField(eye,"_material");
                    foreach(string p in new[]{"_MainTex","_overtex1","_overtex2"}) {
                        Vector2 s=mat.GetTextureScale(p),o=mat.GetTextureOffset(p);
                        writer.WriteLine("st\t"+eye._renderer.name+"\t"+cases+"\t"+p+"\t"+Num(h)+"\t"+Num(v)+"\t"+Num(s.x)+"\t"+Num(s.y)+"\t"+Num(o.x)+"\t"+Num(o.y));
                    }
                }
                cases++;
            }
            writer.WriteLine("cases\t"+cases);
        }
        yield return null;
    }
    private static float BlinkFloat(FBSBlinkControl blink,string name) { return Convert.ToSingle(FbsField(blink,name)); }
    private static int BlinkInt(FBSBlinkControl blink,string name) { return Convert.ToInt32(FbsField(blink,name)); }
    private static IEnumerator RunFaceTime(ChaControl cha) {
        FaceBlendShape fbs=cha.fbsCtrl;
        if(fbs==null) throw new Exception("loaded head has no FaceBlendShape");
        cha.animBody.enabled=false;cha.enabled=false;fbs.enabled=false;
        if(cha.expression!=null) cha.expression.enabled=false;
        FBSBase[] controls={fbs.MouthCtrl,fbs.EyesCtrl,fbs.EyebrowCtrl};
        Dictionary<string,SkinnedMeshRenderer> renderers=new Dictionary<string,SkinnedMeshRenderer>();
        int oldCapture=Time.captureFramerate;
        Time.captureFramerate=60;
        try {
        using(StreamWriter writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".face-time.tsv")) {
            float[] settings={fbs.BlinkCtrl.BlinkFrequency,fbs.BlinkCtrl.BaseSpeed,fbs.EyebrowCtrl.SyncBlink?1f:0f,
                cha.fileStatus.eyesBlink?1f:0f,fbs.EyesCtrl.OpenMin,fbs.EyebrowCtrl.OpenMin,fbs.EyesCtrl.FixedRate,fbs.EyebrowCtrl.FixedRate};
            for(int i=0;i<settings.Length;i++) writer.WriteLine("setting\t"+i+"\t"+Num(settings[i]));
            for(int control=0;control<3;control++) foreach(var target in controls[control].FBSTarget) {
                var renderer=target.GetSkinnedMeshRenderer();
                if(renderer==null||renderer.sharedMesh==null) throw new Exception("missing FBS target");
                renderers[renderer.name]=renderer;
                for(int pattern=0;pattern<target.PtnSet.Length;pattern++)
                    writer.WriteLine("pattern\t"+control+"\t"+renderer.name+"\t"+pattern+"\t"+target.PtnSet[pattern].Close+"\t"+target.PtnSet[pattern].Open);
            }
            foreach(var entry in renderers) for(int channel=0;channel<entry.Value.sharedMesh.blendShapeCount;channel++)
                writer.WriteLine("name\t"+entry.Key+"\t"+channel+"\t"+entry.Value.sharedMesh.GetBlendShapeName(channel));
            int serial=0;
            for(int changed=0;changed<3;changed++) {
                for(int control=0;control<3;control++) { controls[control].ChangePtn(0,false);controls[control].SetFixedRate(control==1?.61337f:.8f); }
                for(int step=0;step<24;step++) {
                    yield return new WaitForEndOfFrame();
                    int pattern=-1;bool blend=true;
                    if(step==1) pattern=8;
                    if(step==4) pattern=2;
                    if(step==9) {pattern=3;blend=false;}
                    if(step==10) pattern=3;
                    if(step==11) pattern=1;
                    if(pattern>=0) controls[changed].ChangePtn(pattern,blend);
                    fbs.EyebrowCtrl.CalcBlend(1f);fbs.EyesCtrl.CalcBlend(1f);fbs.MouthCtrl.CalcBlend(.8f);
                    object timer=FbsField(controls[changed],"blendTimeCtrl");
                    float alpha=(float)timer.GetType().GetMethod("GetProgressRate").Invoke(timer,null);
                    writer.WriteLine("frame\t"+serial+"\t"+changed+"\t"+step+"\t"+pattern+"\t"+(blend?1:0)+"\t"+Num(Time.deltaTime)+"\t"+Num(alpha));
                    foreach(var entry in renderers) for(int channel=0;channel<entry.Value.sharedMesh.blendShapeCount;channel++)
                        writer.WriteLine("weight\t"+serial+"\t"+entry.Key+"\t"+channel+"\t"+Num(entry.Value.GetBlendShapeWeight(channel)));
                    serial++;
                }
            }
            writer.WriteLine("frames\t"+serial);
            FBSBlinkControl blink=new FBSBlinkControl();blink.BlinkFrequency=3;
            for(int frame=0;frame<240;frame++) {
                yield return new WaitForEndOfFrame();
                float time=Time.time;string op="none";float commandFloat=0f;int commandInt=0;
                if(frame==60) {blink.SetFixedFlags(1);op="fixed";}
                if(frame==70) {blink.SetFixedFlags(0);op="resume";}
                if(frame==90) {blink.SetForceClose();op="close";commandFloat=BlinkFloat(blink,"calcSpeed")-blink.BaseSpeed;commandInt=BlinkInt(blink,"count")-1;}
                if(frame==110) {blink.SetForceOpen();op="open";commandFloat=BlinkFloat(blink,"calcSpeed")-blink.BaseSpeed;}
                if(frame==140) {blink.SetFrequency(0);op="frequency0";}
                if(frame==180) {blink.SetSpeed(.2f);op="speed";}
                int beforeMode=BlinkInt(blink,"blinkMode");
                blink.CalcBlink();
                int afterMode=BlinkInt(blink,"blinkMode"),count=BlinkInt(blink,"count");
                float speed=BlinkFloat(blink,"calcSpeed"),deadline=BlinkFloat(blink,"blinkTime"),randomFloat=0f;
                int randomInt=0;
                if(beforeMode==0&&afterMode==1) {randomFloat=speed-blink.BaseSpeed;randomInt=count-1;}
                if(beforeMode==1&&afterMode==-1) randomFloat=speed-blink.BaseSpeed;
                if(beforeMode==-1&&afterMode==0) randomInt=Mathf.RoundToInt((deadline-time)/.2f);
                writer.WriteLine("blink\t"+frame+"\t"+Num(time)+"\t"+op+"\t"+Num(commandFloat)+"\t"+commandInt+"\t"+Num(randomFloat)+"\t"+randomInt
                    +"\t"+blink.GetFixedFlags()+"\t"+blink.BlinkFrequency+"\t"+Num(blink.BaseSpeed)+"\t"+afterMode+"\t"+count+"\t"+Num(speed)+"\t"+Num(deadline)+"\t"+Num(blink.GetOpenRate()));
            }
            writer.WriteLine("blinks\t240");
        }
        } finally {Time.captureFramerate=oldCapture;}
    }

    private void WriteHeadMaterials(ChaControl cha, string suffix) {
        using(StreamWriter writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+suffix)) {
            foreach(Renderer renderer in cha.objHead.GetComponentsInChildren<Renderer>(true))
                foreach(Material material in renderer.sharedMaterials) {
                    if(material==null) continue;
                    writer.WriteLine("material\t"+PathOf(renderer.transform)+"\t"+material.name+"\t"+material.shader.name+"\t"+material.renderQueue);
                    foreach(string property in new[]{"_Color","_Color2","_Color3","_Color4","_ShadowColor"})
                        if(material.HasProperty(property)) {
                            Color c=material.GetColor(property);
                            writer.WriteLine("color\t"+renderer.name+"\t"+material.name+"\t"+property+"\t"+c.r.ToString("R",CultureInfo.InvariantCulture)+"\t"+c.g.ToString("R",CultureInfo.InvariantCulture)+"\t"+c.b.ToString("R",CultureInfo.InvariantCulture)+"\t"+c.a.ToString("R",CultureInfo.InvariantCulture));
                        }
                    foreach(string property in new[]{"_MainTex","_ColorMask","_NormalMap"})
                        if(material.HasProperty(property)) {
                            Texture texture=material.GetTexture(property);
                            writer.WriteLine("texture\t"+renderer.name+"\t"+material.name+"\t"+property+"\t"+(texture==null?"null":texture.name+":"+texture.GetType().Name));
                        }
                    foreach(string property in new[]{"_LineWidthS","_linewidthG"})
                        if(material.HasProperty(property))
                            writer.WriteLine("float\t"+renderer.name+"\t"+material.name+"\t"+property+"\t"+material.GetFloat(property).ToString("R",CultureInfo.InvariantCulture));
                }
        }
    }

    [Serializable] private class AnimatorBlendClip { public string name; public float weight, length; }
    [Serializable] private class AnimatorBlendFrame {
        public string state, parameter, parameter2; public float value, value2, phase, length, speed, speedMultiplier;
        public string controller, baseState, parameter3, baseParameter; public int layer;
        public float value3, baseValue, baseLength, basePhase;
        public AnimatorBlendClip[] clips; public Bone[] bones;
    }

    private static void RunAnimatorCartesianControl(ChaControl cha) {
        const string controller="khs3_f_base3",baseState="Base Layer.Idle";
        cha.LoadAnimation("studio/anime/00.unity3d",controller,null);
        Animator animator=cha.animBody;animator.speed=1f;animator.enabled=false;
        foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true)) db.enabled=false;
        foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) db.enabled=false;
        var frames=new List<AnimatorBlendFrame>();
        float[,] points={{0f,0f},{1f,0f},{0f,1f},{1f,1f},{.5f,.5f},{.5f,0f},{0f,.5f},{1f,.5f},{.5f,1f},
            {.17f,.43f},{.31f,.79f},{.74f,.18f},{.68f,.84f},{.43f,.27f},{-.25f,.37f},{1.25f,.61f},{.61f,-.25f},{.37f,1.25f}};
        foreach(string specification in new[]{"17|17:Te_siri_D_momu_L.Te_siri_D_momu_L|siriL_X|siriL_Y",
                "5|05:Te_mune_D_momu_L.Te_mune_D_momu_L|muneR_X|muneR_Y"}) {
            string[] fields=specification.Split('|');int layer=Int32.Parse(fields[0]);
            for(int point=0;point<points.GetLength(0);++point) foreach(float phase in new[]{0f,.17f,.43f,.91f}) {
                animator.Rebind();
                for(int other=1;other<animator.layerCount;++other) animator.SetLayerWeight(other,other==layer?1f:0f);
                animator.SetFloat("height",.37f);animator.SetFloat("Breast",.61f);
                animator.SetFloat(fields[2],points[point,0]);animator.SetFloat(fields[3],points[point,1]);
                animator.Play(baseState,0,0f);animator.Play(fields[1],layer,phase);animator.Update(0f);
                AnimatorStateInfo info=animator.GetCurrentAnimatorStateInfo(layer);
                AnimatorStateInfo baseInfo=animator.GetCurrentAnimatorStateInfo(0);
                float basePhase=phase*info.length/baseInfo.length;
                animator.Play(baseState,0,basePhase);animator.Play(fields[1],layer,phase);animator.Update(0f);
                info=animator.GetCurrentAnimatorStateInfo(layer);
                if(info.fullPathHash!=Animator.StringToHash(fields[1])) throw new Exception("Native Cartesian state mismatch");
                var clips=new List<AnimatorBlendClip>();
                foreach(AnimatorClipInfo c in animator.GetCurrentAnimatorClipInfo(layer)) clips.Add(new AnimatorBlendClip{name=c.clip.name,weight=c.weight,length=c.clip.length});
                var bones=new List<Bone>();var seen=new HashSet<string>();
                foreach(Transform tf in cha.objAnim.GetComponentsInChildren<Transform>(true))
                    if(tf.name.StartsWith("cf_",StringComparison.OrdinalIgnoreCase)&&seen.Add(tf.name))
                        bones.Add(new Bone{name=tf.name,path=PathOf(tf),position=tf.localPosition,rotation=tf.localRotation,scale=tf.localScale});
                frames.Add(new AnimatorBlendFrame{controller=controller,state=fields[1],layer=layer,baseState=baseState,
                    parameter=fields[2],value=points[point,0],parameter2=fields[3],value2=points[point,1],
                    parameter3="Breast",value3=.61f,baseParameter="height",baseValue=.37f,
                    phase=phase,basePhase=basePhase,baseLength=baseInfo.length,length=info.length,
                    speed=info.speed,speedMultiplier=info.speedMultiplier,clips=clips.ToArray(),bones=bones.ToArray()});
            }
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".animator-2d.json",ProbeJson(frames.ToArray()));
    }
    private static void RunAnimatorBlendControl(ChaControl cha) {
        cha.LoadAnimation("action/animator/00.unity3d", "base", null);
        Animator animator=cha.animBody;animator.speed=1f;animator.enabled=false;
        foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true)) db.enabled=false;
        foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) db.enabled=false;
        var frames=new List<AnimatorBlendFrame>();
        foreach(string pair in new string[]{"Base Layer.Main.Locomotion_Anger|Speed","Base Layer.Twoshot.Twoshot_00|height","Base Layer.Lesbian.Lesbian3|height1"}) {
            string[] identity=pair.Split('|');
            foreach(float value in new float[]{0f,.25f,.5f,.75f,1f}) foreach(float phase in new float[]{0f,.17f,.43f,.91f}) {
                animator.Rebind();
                for(int layer=1;layer<animator.layerCount;++layer) animator.SetLayerWeight(layer,0f);
                animator.SetFloat("MotionSpeed",1f);animator.SetFloat(identity[1],value);
                if(identity[1]=="height1") animator.SetFloat("height",.37f);
                animator.Play(identity[0],0,phase);animator.Update(0f);
                AnimatorStateInfo info=animator.GetCurrentAnimatorStateInfo(0);
                if(info.fullPathHash!=Animator.StringToHash(identity[0])) throw new Exception("Native state identity mismatch: "+identity[0]);
                var clips=new List<AnimatorBlendClip>();
                foreach(AnimatorClipInfo c in animator.GetCurrentAnimatorClipInfo(0)) clips.Add(new AnimatorBlendClip{name=c.clip.name,weight=c.weight,length=c.clip.length});
                var bones=new List<Bone>();
                var seen=new HashSet<string>();
                foreach(Transform tf in cha.objAnim.GetComponentsInChildren<Transform>(true))
                    if(tf.name.StartsWith("cf_",StringComparison.OrdinalIgnoreCase)&&seen.Add(tf.name))
                        bones.Add(new Bone{name=tf.name,path=PathOf(tf),position=tf.localPosition,rotation=tf.localRotation,scale=tf.localScale});
                frames.Add(new AnimatorBlendFrame{state=identity[0],parameter=identity[1],value=value,
                    parameter2=identity[1]=="height1"?"height":"",value2=.37f,phase=phase,
                    length=info.length,speed=info.speed,speedMultiplier=info.speedMultiplier,clips=clips.ToArray(),bones=bones.ToArray()});
            }
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".animator-blend.json",ProbeJson(frames.ToArray()));
    }

    [Serializable] private class AnimatorClockFrame {
        public string state, parameter; public float value, elapsed, normalizedTime, length, speed, speedMultiplier;
        public string controller, baseState; public int layer;
        public AnimatorBlendClip[] clips; public Bone[] bones;
    }
    [Serializable] private class StudioFloat {public string name;public float value;}
    [Serializable] private class StudioFrame {
        public string source,owner,overlaySource,overlayOwner,state,baseState,layerState;
        public int group,category,no,layer,layerCount;public bool isH,isMotion;
        public float height,breast,motion,elapsed,length,baseLength;
        public StudioFloat[] parameters;public AnimatorBlendClip[] clips,baseClips;public Bone[] bones;
    }
    private static void RunStudioControl(ChaControl cha) {
        var studio=UnityEngine.Object.FindObjectOfType<Studio.Studio>();Studio.OCIChar oci=null;
        foreach(var item in studio.dicObjectCtrl.Values) {var c=item as Studio.OCIChar;if(c!=null&&c.charInfo==cha) {oci=c;break;}}
        if(oci==null) throw new Exception("Actual Studio OCIChar missing");
        var frames=new List<StudioFrame>();
        foreach(int[] identity in new[]{new[]{0,3,0},new[]{2,128,0},new[]{9,183,0}}) {
            var source=Studio.Info.Instance.dicAnimeLoadInfo[identity[0]][identity[1]][identity[2]];
            var h=source as Studio.Info.HAnimeLoadInfo;int layer=h!=null&&h.isBreastLayer?h.breastLayer:0;
            foreach(float[] option in new[]{new[]{0f,0f,0f},new[]{.37f,.61f,.43f},new[]{.81f,.19f,.77f}})
                foreach(float time in new[]{0f,.23f,.71f}) {
                    // A fresh load-cache isolates the actual OCIChar initialization
                    // from prior selection history without reimplementing LoadAnime.
                    typeof(Studio.OCIChar).GetField("_loadedAnimeInfo",BindingFlags.Instance|BindingFlags.NonPublic)
                        .SetValue(oci,new Studio.OCIChar.LoadedAnimeInfo());
                    typeof(Studio.OCIChar).GetProperty("breastLayer",BindingFlags.Instance|BindingFlags.NonPublic).SetValue(oci,-1,null);
                    oci.oiCharInfo.animeOptionParam[0]=option[0];oci.oiCharInfo.animeOptionParam[1]=option[1];oci.oiCharInfo.animePattern=option[2];
                    oci.LoadAnime(identity[0],identity[1],identity[2],0f);
                    var a=cha.animBody;a.enabled=false;a.speed=1f;
                    foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true)) db.enabled=false;
                    foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) db.enabled=false;
                    a.Update(0f);a.Update(time);
                    var b=a.GetCurrentAnimatorStateInfo(0);var l=a.GetCurrentAnimatorStateInfo(layer);
                    if(b.shortNameHash!=Animator.StringToHash(source.clip)||l.shortNameHash!=Animator.StringToHash(source.clip))
                        throw new Exception("Studio LoadAnime state changed: "+source.clip+" layer "+layer);
                    var floats=new List<StudioFloat>();foreach(var p in a.parameters)
                        if(p.type==AnimatorControllerParameterType.Float) floats.Add(new StudioFloat{name=p.name,value=a.GetFloat(p.name)});
                    var clips=new List<AnimatorBlendClip>();foreach(var c in a.GetCurrentAnimatorClipInfo(layer))
                        clips.Add(new AnimatorBlendClip{name=c.clip.name,weight=c.weight,length=c.clip.length});
                    var baseClips=new List<AnimatorBlendClip>();foreach(var c in a.GetCurrentAnimatorClipInfo(0))
                        baseClips.Add(new AnimatorBlendClip{name=c.clip.name,weight=c.weight,length=c.clip.length});
                    var bones=new List<Bone>();var seen=new HashSet<string>();
                    foreach(Transform tf in cha.objAnim.GetComponentsInChildren<Transform>(true))
                        if(tf.name.StartsWith("cf_",StringComparison.OrdinalIgnoreCase)&&seen.Add(tf.name))
                            bones.Add(new Bone{name=tf.name,path=PathOf(tf),position=tf.localPosition,rotation=tf.localRotation,scale=tf.localScale});
                    frames.Add(new StudioFrame{source=source.bundlePath,owner=source.fileName,
                        overlaySource=h!=null?h.overrideFile.bundlePath:"",overlayOwner=h!=null?h.overrideFile.fileName:"",state=source.clip,
                        baseState=b.fullPathHash.ToString(),layerState=l.fullPathHash.ToString(),group=identity[0],category=identity[1],no=identity[2],
                        layer=layer,layerCount=a.layerCount,isH=h!=null,isMotion=h!=null&&h.isMotion,height=option[0],breast=option[1],motion=option[2],
                        elapsed=time,length=l.length,baseLength=b.length,parameters=floats.ToArray(),clips=clips.ToArray(),baseClips=baseClips.ToArray(),bones=bones.ToArray()});
                }
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".studio.json",ProbeJson(frames.ToArray()));
    }
    [Serializable] private class YureFrame {
        public string source,owner,overlaySource,overlayOwner,state,yureSource,yureOwner;
        public int group,category,no,layer;public bool isH,isMotion;
        public bool[] active,shapeEnabled;public Bone[] shapeBones;
    }
    [Serializable] private class YurePhysicsFrame {public int frame;public bool[] active;public Bone[] bones;}
    private static Bone[] StudioBones(ChaControl cha) {
        var bones=new List<Bone>();var seen=new HashSet<string>();
        foreach(Transform tf in cha.objAnim.GetComponentsInChildren<Transform>(true))
            if(tf.name.StartsWith("cf_",StringComparison.OrdinalIgnoreCase)&&seen.Add(tf.name))
                bones.Add(new Bone{name=tf.name,path=PathOf(tf),position=tf.localPosition,rotation=tf.localRotation,scale=tf.localScale});
        return bones.ToArray();
    }
    private static void RunYureControl(ChaControl cha) {
        var studio=UnityEngine.Object.FindObjectOfType<Studio.Studio>();Studio.OCIChar oci=null;
        foreach(var item in studio.dicObjectCtrl.Values) {var c=item as Studio.OCIChar;if(c!=null&&c.charInfo==cha) {oci=c;break;}}
        if(oci==null) throw new Exception("Actual OCIChar missing");
        cha.animBody.enabled=false;var frames=new List<YureFrame>();
        foreach(var id in new[]{new[]{0,3,0},new[]{2,128,0},new[]{0,3,0},new[]{9,183,0},new[]{0,3,0},
            new[]{2,128,1},new[]{0,3,0},new[]{9,183,0},new[]{0,3,0}}) {
            var source=Studio.Info.Instance.dicAnimeLoadInfo[id[0]][id[1]][id[2]];var h=source as Studio.Info.HAnimeLoadInfo;
            oci.oiCharInfo.animeOptionParam[0]=.37f;oci.oiCharInfo.animeOptionParam[1]=.61f;oci.oiCharInfo.animePattern=.43f;
            oci.LoadAnime(id[0],id[1],id[2]);
            cha.UpdateShapeBody();cha.UpdateAlwaysShapeBody();cha.ReSetupDynamicBoneBust();
            var active=new bool[4];for(int k=0;k<4;++k) active[k]=cha.dictDynamicBoneBust[(ChaInfo.DynamicBoneKind)k].enabled;
            var shape=new bool[18];for(int side=0;side<2;++side) for(int k=0;k<9;++k) shape[side*9+k]=!cha.fileStatus.disableBustShapeMask[side,k];
            frames.Add(new YureFrame{source=source.bundlePath,owner=source.fileName,
                overlaySource=h!=null?h.overrideFile.bundlePath:"",overlayOwner=h!=null?h.overrideFile.fileName:"",state=source.clip,
                yureSource=h!=null?h.yureFile.bundlePath:"",yureOwner=h!=null?h.yureFile.fileName:"",group=id[0],category=id[1],no=id[2],
                layer=h!=null&&h.isBreastLayer?h.breastLayer:0,isH=h!=null,isMotion=h!=null&&h.isMotion,active=active,shapeEnabled=shape,shapeBones=StudioBones(cha)});
        }
        string output=Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT");
        File.WriteAllText(output+".yure.json",ProbeJson(frames.ToArray()));
        // Independent OnDisable/OnEnable control, deliberately asymmetric.
        oci.yureCtrl.ResetShape();cha.UpdateShapeBody();cha.ReSetupDynamicBoneBust();cha.UpdateBustSoftnessAndGravity();
        foreach(var db in cha.dictDynamicBoneBust.Values) {db.enabled=true;db.ResetPosition();db.ResetParticlesPosition();}
        var frozen=cha.objAnim.GetComponentsInChildren<Transform>(true);
        var positions=new Vector3[frozen.Length];var rotations=new Quaternion[frozen.Length];var scales=new Vector3[frozen.Length];
        for(int i=0;i<frozen.Length;++i) {positions[i]=frozen[i].localPosition;rotations[i]=frozen[i].localRotation;scales[i]=frozen[i].localScale;}
        File.WriteAllText(output+".yure-baseline.json",ProbeJson(StudioBones(cha)));
        WriteTransforms(cha,".yure-baseline.tsv");WritePhysics(cha,".yure-setup.tsv");
        var traces=new List<YurePhysicsFrame>();
        var init=typeof(DynamicBone_Ver02).GetMethod("InitTransforms",BindingFlags.Instance|BindingFlags.NonPublic);
        var step=typeof(DynamicBone_Ver02).GetMethod("UpdateDynamicBones",BindingFlags.Instance|BindingFlags.NonPublic);
        for(int frame=0;frame<240;++frame) {
            for(int i=0;i<frozen.Length;++i) {frozen[i].localPosition=positions[i];frozen[i].localRotation=rotations[i];frozen[i].localScale=scales[i];}
            frozen[0].localPosition+=new Vector3(.001f*(frame+1),0,0);
            if(frame==60) cha.playDynamicBoneBust(ChaInfo.DynamicBoneKind.BreastL,false);
            if(frame==90) cha.playDynamicBoneBust(ChaInfo.DynamicBoneKind.BreastR,false);
            if(frame==120) cha.playDynamicBoneBust(ChaInfo.DynamicBoneKind.HipL,false);
            if(frame==150) cha.playDynamicBoneBust(ChaInfo.DynamicBoneKind.BreastL,true);
            if(frame==180) {cha.playDynamicBoneBust(ChaInfo.DynamicBoneKind.BreastR,true);cha.playDynamicBoneBust(ChaInfo.DynamicBoneKind.HipL,true);}
            var actives=new bool[4];var bones=new List<Bone>();var seen=new HashSet<string>();
            for(int k=0;k<4;++k) {
                var db=cha.dictDynamicBoneBust[(ChaInfo.DynamicBoneKind)k];actives[k]=db.enabled;
                if(db.enabled) {init.Invoke(db,null);step.Invoke(db,new object[]{1f/60f});}
                foreach(var tf in db.Bones) if(seen.Add(tf.name)) bones.Add(new Bone{name=tf.name,path=PathOf(tf),position=tf.localPosition,rotation=tf.localRotation,scale=tf.localScale});
            }
            traces.Add(new YurePhysicsFrame{frame=frame,active=actives,bones=bones.ToArray()});
        }
        File.WriteAllText(output+".yure-physics.json",ProbeJson(traces.ToArray()));
    }
    private static void RunStudioSampler(ChaControl cha) {
        cha.LoadAnimation("studio/anime/00.unity3d","khh_f_base",null);
        var a=cha.animBody;var over=CommonLib.LoadAsset<RuntimeAnimatorController>("studio/anime/00.unity3d","khh_f_28");
        a.runtimeAnimatorController=Illusion.Utils.Animator.SetupAnimatorOverrideController(a.runtimeAnimatorController,over);
        a.enabled=false;a.speed=1f;a.SetFloat("height",0f);a.SetFloat("Breast",0f);a.SetFloat("motion",0f);
        for(int i=1;i<a.layerCount;++i) a.SetLayerWeight(i,i==79?1f:0f);
        foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true)) db.enabled=false;
        foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) db.enabled=false;
        a.Play("Stop_Idle",0,0f);a.Play("Stop_Idle",79,0f);a.Update(0f);
        var tf=a.transform.Find("cf_j_root/cf_n_height/cf_j_hips/cf_j_spine01/cf_j_spine02/cf_j_spine03/cf_d_bust00");
        if(tf==null) throw new Exception("Source helper path missing");
        var sb=new System.Text.StringBuilder();sb.AppendLine("stage\troot\tbone\tclip\trotation");
        var c=a.GetCurrentAnimatorClipInfo(0)[0].clip;
        sb.AppendLine("manual-controller\t"+a.gameObject.name+"\t"+tf.name+"\t"+c.name+"\t"+ProbeJson(tf.localRotation));
        var layerClip=a.GetCurrentAnimatorClipInfo(79)[0].clip;
        sb.AppendLine("clip-identity\t"+c.GetInstanceID()+"\t"+layerClip.GetInstanceID()+"\t"+layerClip.name+"\t"+(c==layerClip));
        a.SetLayerWeight(79,0f);a.Update(0f);
        sb.AppendLine("base-only\t"+a.gameObject.name+"\t"+tf.name+"\t"+c.name+"\t"+ProbeJson(tf.localRotation));
        c.SampleAnimation(a.gameObject,0f);
        sb.AppendLine("raw-clip\t"+a.gameObject.name+"\t"+tf.name+"\t"+c.name+"\t"+ProbeJson(tf.localRotation));
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".sampler.tsv",sb.ToString());
    }
    [Serializable] private class MotionTimeScaleFrame {
        public string bundle,controller,state;public int layer;
        public float elapsed,length,normalizedTime,speed,speedMultiplier,baseLength,baseNormalizedTime;
        public StudioFloat[] parameters;public AnimatorBlendClip[] clips,baseClips;public Bone[] bones;
    }
    [Serializable] private class AdditiveFrame {
        public string bundle,controller,state,baseState;public int layer;
        public float elapsed,weight,length,normalizedTime,baseLength,baseNormalizedTime;
        public StudioFloat[] parameters;public AnimatorBlendClip[] clips,baseClips;public Bone[] bones,baseBones,rawBones,referenceBones;
    }
    [Serializable] private class MultiLayerSnapshot {
        public int layer;public string state;public float weight,length,normalizedTime;
        public float startTime,startPhase;public bool started;
        public AnimatorBlendClip[] clips;public Bone[] bones;
    }
    [Serializable] private class MultiLayerFrame {
        public string bundle,controller,baseState;public float elapsed,baseLength,baseNormalizedTime;
        public StudioFloat[] parameters;public Bone[] bones,baseBones;public MultiLayerSnapshot[] layers;
        public Bone[] zeroClockBones;
        public float[] triggerTimes;
    }
    private static void SampleTriggeredLayers(Animator a,string baseState,StudioFloat[] parameters,MultiLayerSnapshot[] layers,float elapsed,int isolated=-1,bool resetBase=false) {
        float first=Math.Min(elapsed,.61f);SampleLayerClocks(a,baseState,parameters,layers,first,isolated);float clock=first;
        foreach(float at in new[]{.61f,1.07f}) if(at<=elapsed) {
            a.Update(at-clock);clock=at;a.SetTrigger("Restart");a.Update(0f);
            if(resetBase) {a.Play(baseState,0,0f);a.Update(0f);}
        }
        a.Update(elapsed-clock);
    }
    private static void RunTriggerRestartControl(ChaControl cha) {
        const string bundle="h/anim/female/05_01_00.unity3d",owner="khr_f_02_00",baseState="Base Layer.OLoop";
        cha.LoadAnimation(bundle,owner,null);Animator a=cha.animBody;a.speed=1f;a.enabled=false;
        foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true)) db.enabled=false;
        foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) db.enabled=false;
        int[][] selections={new[]{7,8},new[]{8,70},new[]{7,8,73,74}};
        float[][] weights={new[]{.63f,.37f},new[]{.37f,.63f},new[]{0f,.37f,.41f,.59f}};
        var frames=new List<MultiLayerFrame>();
        for(int combination=0;combination<selections.Length;++combination) foreach(float elapsed in new[]{.49f,.61f,.73f,1.07f,1.7f}) {
            var snapshots=new List<MultiLayerSnapshot>();
            var parameters=new[]{new StudioFloat{name="height",value=.37f},new StudioFloat{name="height1",value=.61f},new StudioFloat{name="Breast1",value=.37f}};
            for(int i=0;i<selections[combination].Length;++i) {
                int l=selections[combination][i];string state=l>=73?baseState:l==7?"07:Te_kokan_sawari.Te_kokan_sawari":l==8?"08:Te_kokan_C_kosuru.Te_kokan_C_kosuru":"70:Vibe_kokan_sawari.Vibe_kokan_sawari";
                snapshots.Add(new MultiLayerSnapshot{layer=l,state=state,weight=weights[combination][i],startTime=l==8?.11f:l==70?-.29f:l==7?-.23f:0f,startPhase=l==8?.37f:l==70?.61f:l==7?.2f:0f,started=true});
            }
            var layers=snapshots.ToArray();SampleTriggeredLayers(a,baseState,parameters,layers,elapsed);
            var info=a.GetCurrentAnimatorStateInfo(0);if(info.fullPathHash!=Animator.StringToHash(baseState)||a.IsInTransition(0)) throw new Exception("Trigger base changed");
            var bones=AdditiveBones(a);
            foreach(var l in layers) {
                var state=a.GetCurrentAnimatorStateInfo(l.layer);
                if(state.fullPathHash!=Animator.StringToHash(l.state)||a.IsInTransition(l.layer)) throw new Exception("Unsupported trigger transition");
                l.length=state.length;l.normalizedTime=state.normalizedTime;
                var clips=new List<AnimatorBlendClip>();foreach(var c in a.GetCurrentAnimatorClipInfo(l.layer)) clips.Add(new AnimatorBlendClip{name=c.clip.name,weight=c.weight,length=c.clip.length});l.clips=clips.ToArray();
            }
            SampleTriggeredLayers(a,baseState,parameters,new MultiLayerSnapshot[0],elapsed);var baseBones=AdditiveBones(a);
            foreach(var l in layers) {SampleTriggeredLayers(a,baseState,parameters,layers,elapsed,l.layer);l.bones=AdditiveBones(a);}
            SampleTriggeredLayers(a,baseState,parameters,layers,elapsed,-1,true);var wrong=AdditiveBones(a);
            var fired=new List<float>();foreach(float at in new[]{.61f,1.07f}) if(at<=elapsed) fired.Add(at);
            frames.Add(new MultiLayerFrame{bundle=bundle,controller=owner,baseState=baseState,elapsed=elapsed,baseLength=info.length,baseNormalizedTime=info.normalizedTime,parameters=parameters,bones=bones,baseBones=baseBones,layers=layers,zeroClockBones=wrong,triggerTimes=fired.ToArray()});
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".restart.json",ProbeJson(frames.ToArray()));
    }
    private static void SampleLayerClocks(Animator a,string baseState,StudioFloat[] parameters,MultiLayerSnapshot[] layers,float elapsed,int isolated=-1,bool ignoreClocks=false) {
        a.Rebind();for(int l=1;l<a.layerCount;++l) a.SetLayerWeight(l,0f);
        foreach(var p in parameters) a.SetFloat(p.name,p.value);a.Play(baseState,0,0f);
        foreach(var l in layers) if(l.layer<73) a.Play(l.state,l.layer,0f);
        a.Update(0f);
        foreach(var l in layers) {
            l.length=a.GetCurrentAnimatorStateInfo(l.layer).length;
            float start=ignoreClocks?0f:l.startTime,phase=ignoreClocks?0f:l.startPhase;
            if(l.layer>=73||start<=0f) {
                if(l.layer<73) a.Play(l.state,l.layer,phase-start/l.length);
                a.SetLayerWeight(l.layer,isolated<0?l.weight:isolated==l.layer?1f:0f);
            }
        }
        a.Update(0f);float clock=0f;
        var events=new SortedSet<float>();if(!ignoreClocks) foreach(var l in layers) if(l.layer<73&&l.startTime>0f&&l.startTime<=elapsed) events.Add(l.startTime);
        foreach(float at in events) {
            a.Update(at-clock);clock=at;
            foreach(var l in layers) if(l.layer<73&&l.startTime==at) {
                a.Play(l.state,l.layer,l.startPhase);
                a.SetLayerWeight(l.layer,isolated<0?l.weight:isolated==l.layer?1f:0f);
            }
            a.Update(0f);
        }
        a.Update(elapsed-clock);
    }
    private static void RunLayerClockControl(ChaControl cha) {
        const string bundle="h/anim/female/05_01_00.unity3d",owner="khr_f_02_00",baseState="Base Layer.OLoop";
        cha.LoadAnimation(bundle,owner,null);Animator a=cha.animBody;a.speed=1f;a.enabled=false;
        foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true)) db.enabled=false;
        foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) db.enabled=false;
        int[][] selections={new[]{7,8},new[]{7,8},new[]{8,70},new[]{7,8,73},new[]{7,8,73,74}};
        float[][] weights={new[]{.63f,.37f},new[]{1f,1f},new[]{.37f,.63f},new[]{.63f,.37f,.41f},new[]{.63f,.37f,.41f,.59f}};
        var frames=new List<MultiLayerFrame>();
        for(int combination=0;combination<selections.Length;++combination) foreach(float elapsed in new[]{.05f,.17f,.61f,1.7f}) {
            var snapshots=new List<MultiLayerSnapshot>();
            var parameters=new[]{new StudioFloat{name="height",value=.37f},new StudioFloat{name="height1",value=.61f},new StudioFloat{name="Breast1",value=.37f}};
            for(int i=0;i<selections[combination].Length;++i) {
                int l=selections[combination][i];
                string state=l>=73?baseState:l==7?"07:Te_kokan_sawari.Te_kokan_sawari":l==8?"08:Te_kokan_C_kosuru.Te_kokan_C_kosuru":"70:Vibe_kokan_sawari.Vibe_kokan_sawari";
                snapshots.Add(new MultiLayerSnapshot{layer=l,state=state,weight=weights[combination][i],startTime=l==8?.11f:l==70?-.29f:combination==0||l>=73?0f:-.23f,startPhase=l==8&&combination!=0?.37f:l==70?.61f:l==7&&combination!=0?.2f:0f,started=l!=8||elapsed>=.11f});
            }
            var layers=snapshots.ToArray();SampleLayerClocks(a,baseState,parameters,layers,elapsed);
            var info=a.GetCurrentAnimatorStateInfo(0);if(info.fullPathHash!=Animator.StringToHash(baseState)) throw new Exception("Layer clock base transitioned");
            var bones=AdditiveBones(a);
            foreach(var l in layers) {
                var state=a.GetCurrentAnimatorStateInfo(l.layer);
                if(state.fullPathHash!=Animator.StringToHash(l.state)) throw new Exception("Layer clock state transitioned");
                l.length=state.length;l.normalizedTime=state.normalizedTime;
                var clips=new List<AnimatorBlendClip>();foreach(var c in a.GetCurrentAnimatorClipInfo(l.layer)) clips.Add(new AnimatorBlendClip{name=c.clip.name,weight=c.weight,length=c.clip.length});l.clips=clips.ToArray();
            }
            SampleLayerClocks(a,baseState,parameters,new MultiLayerSnapshot[0],elapsed);var baseBones=AdditiveBones(a);
            foreach(var l in layers) {SampleLayerClocks(a,baseState,parameters,layers,elapsed,l.layer);l.bones=AdditiveBones(a);}
            SampleLayerClocks(a,baseState,parameters,layers,elapsed,-1,true);var zero=AdditiveBones(a);
            frames.Add(new MultiLayerFrame{bundle=bundle,controller=owner,baseState=baseState,elapsed=elapsed,baseLength=info.length,baseNormalizedTime=info.normalizedTime,parameters=parameters,bones=bones,baseBones=baseBones,layers=layers,zeroClockBones=zero});
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".layer-clock.json",ProbeJson(frames.ToArray()));
    }
    private static void RunMultiLayerControl(ChaControl cha) {
        const string bundle="h/anim/female/05_01_00.unity3d",owner="khr_f_02_00",baseState="Base Layer.OLoop";
        cha.LoadAnimation(bundle,owner,null);Animator a=cha.animBody;a.speed=1f;a.enabled=false;
        foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true)) db.enabled=false;
        foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) db.enabled=false;
        int[][] selections={new[]{7,8},new[]{7,8},new[]{8,70},new[]{7,8,73},new[]{7,8,73,74}};
        float[][] weights={new[]{.63f,.37f},new[]{1f,1f},new[]{.37f,.63f},new[]{.63f,.37f,.41f},new[]{.63f,.37f,.41f,.59f}};
        var frames=new List<MultiLayerFrame>();
        for(int combination=0;combination<selections.Length;++combination) foreach(float elapsed in new[]{.17f,.61f,1.7f}) {
            int[] selected=selections[combination];var snapshots=new List<MultiLayerSnapshot>();
            var parameters=new[]{new StudioFloat{name="height",value=.37f},new StudioFloat{name="height1",value=.61f},new StudioFloat{name="Breast1",value=.37f}};
            // Rebind for every sample, then explicitly start independent states.
            a.Rebind();for(int l=1;l<a.layerCount;++l) a.SetLayerWeight(l,0f);
            foreach(var p in parameters) a.SetFloat(p.name,p.value);a.Play(baseState,0,0f);
            foreach(int l in selected) if(l<73) a.Play(l==7?"07:Te_kokan_sawari.Te_kokan_sawari":l==8?"08:Te_kokan_C_kosuru.Te_kokan_C_kosuru":"70:Vibe_kokan_sawari.Vibe_kokan_sawari",l,0f);
            for(int i=0;i<selected.Length;++i) a.SetLayerWeight(selected[i],weights[combination][i]);
            a.Update(0f);a.Update(elapsed);var info=a.GetCurrentAnimatorStateInfo(0);
            if(info.fullPathHash!=Animator.StringToHash(baseState)) throw new Exception("Multi-layer base transitioned");
            var bones=AdditiveBones(a);
            for(int i=0;i<selected.Length;++i) {
                int l=selected[i];var s=a.GetCurrentAnimatorStateInfo(l);string state=l>=73?baseState:l==7?"07:Te_kokan_sawari.Te_kokan_sawari":l==8?"08:Te_kokan_C_kosuru.Te_kokan_C_kosuru":"70:Vibe_kokan_sawari.Vibe_kokan_sawari";
                if(s.fullPathHash!=Animator.StringToHash(state)) throw new Exception("Multi-layer selected state transitioned");
                var clips=new List<AnimatorBlendClip>();foreach(var c in a.GetCurrentAnimatorClipInfo(l)) clips.Add(new AnimatorBlendClip{name=c.clip.name,weight=c.weight,length=c.clip.length});
                snapshots.Add(new MultiLayerSnapshot{layer=l,state=state,weight=weights[combination][i],length=s.length,normalizedTime=s.normalizedTime,clips=clips.ToArray()});
            }
            a.Rebind();for(int l=1;l<a.layerCount;++l) a.SetLayerWeight(l,0f);foreach(var p in parameters) a.SetFloat(p.name,p.value);
            a.Play(baseState,0,0f);a.Update(0f);a.Update(elapsed);var baseBones=AdditiveBones(a);
            foreach(var snapshot in snapshots) {
                a.Rebind();for(int l=1;l<a.layerCount;++l) a.SetLayerWeight(l,l==snapshot.layer?1f:0f);
                foreach(var p in parameters) a.SetFloat(p.name,p.value);a.Play(baseState,0,0f);
                if(snapshot.layer<73) a.Play(snapshot.state,snapshot.layer,0f);
                a.Update(0f);a.Update(elapsed);snapshot.bones=AdditiveBones(a);
            }
            frames.Add(new MultiLayerFrame{bundle=bundle,controller=owner,baseState=baseState,elapsed=elapsed,baseLength=info.length,baseNormalizedTime=info.normalizedTime,parameters=parameters,bones=bones,baseBones=baseBones,layers=snapshots.ToArray()});
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".multilayer.json",ProbeJson(frames.ToArray()));
    }
    private static Bone[] AdditiveBones(Animator a) {
        var result=new List<Bone>();var seen=new HashSet<string>();
        foreach(Transform tf in a.GetComponentsInChildren<Transform>(true))
            if(tf.name.StartsWith("cf_",StringComparison.OrdinalIgnoreCase)&&seen.Add(tf.name))
                result.Add(new Bone{name=tf.name,path=PathOf(tf),position=tf.localPosition,rotation=tf.localRotation,scale=tf.localScale});
        return result.ToArray();
    }
    private static void RunAdditiveControl(ChaControl cha) {
        const string bundle="h/anim/female/05_01_00.unity3d",owner="khr_f_02_00",baseState="Base Layer.OLoop";
        cha.LoadAnimation(bundle,owner,null);Animator a=cha.animBody;a.speed=1f;a.enabled=false;
        foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true)) db.enabled=false;
        foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) db.enabled=false;
        var frames=new List<AdditiveFrame>();
        foreach(int layer in new[]{7,8,70}) foreach(float weight in new[]{0f,.37f,1f}) foreach(float elapsed in new[]{0f,.17f,.61f,1.7f}) {
            string state=layer==7?"07:Te_kokan_sawari.Te_kokan_sawari":layer==8?"08:Te_kokan_C_kosuru.Te_kokan_C_kosuru":"70:Vibe_kokan_sawari.Vibe_kokan_sawari";
            var parameters=new[]{new StudioFloat{name="height",value=.37f},new StudioFloat{name="height1",value=.61f},new StudioFloat{name="Breast1",value=.37f}};
            a.Rebind();for(int l=1;l<a.layerCount;++l) a.SetLayerWeight(l,l==layer?weight:0f);
            foreach(var p in parameters) a.SetFloat(p.name,p.value);
            a.Play(baseState,0,0f);a.Play(state,layer,0f);a.Update(0f);a.Update(elapsed);
            var info=a.GetCurrentAnimatorStateInfo(layer);var baseInfo=a.GetCurrentAnimatorStateInfo(0);
            if(info.fullPathHash!=Animator.StringToHash(state)||baseInfo.fullPathHash!=Animator.StringToHash(baseState)) throw new Exception("Additive control transitioned");
            var clips=new List<AnimatorBlendClip>();foreach(var c in a.GetCurrentAnimatorClipInfo(layer)) clips.Add(new AnimatorBlendClip{name=c.clip.name,weight=c.weight,length=c.clip.length});
            var baseClips=new List<AnimatorBlendClip>();foreach(var c in a.GetCurrentAnimatorClipInfo(0)) baseClips.Add(new AnimatorBlendClip{name=c.clip.name,weight=c.weight,length=c.clip.length});
            var bones=AdditiveBones(a);AnimationClip clip=a.GetCurrentAnimatorClipInfo(layer)[0].clip;
            a.Rebind();for(int l=1;l<a.layerCount;++l) a.SetLayerWeight(l,0f);foreach(var p in parameters) a.SetFloat(p.name,p.value);
            a.Play(baseState,0,0f);a.Update(0f);a.Update(elapsed);var baseBones=AdditiveBones(a);
            clip.SampleAnimation(a.gameObject,0f);var reference=AdditiveBones(a);
            float phase=info.normalizedTime;float sample=info.loop?Mathf.Repeat(phase,1f):Mathf.Clamp01(phase);
            clip.SampleAnimation(a.gameObject,sample*clip.length);var raw=AdditiveBones(a);
            frames.Add(new AdditiveFrame{bundle=bundle,controller=owner,state=state,baseState=baseState,layer=layer,weight=weight,elapsed=elapsed,length=info.length,normalizedTime=info.normalizedTime,baseLength=baseInfo.length,baseNormalizedTime=baseInfo.normalizedTime,parameters=parameters,clips=clips.ToArray(),baseClips=baseClips.ToArray(),bones=bones,baseBones=baseBones,rawBones=raw,referenceBones=reference});
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".additive.json",ProbeJson(frames.ToArray()));
    }
    private static void RunMotionTimeScaleControl(ChaControl cha) {
        var frames=new List<MotionTimeScaleFrame>();
        foreach(string owner in new[]{"player","khr_f_02_00"}) {
            string bundle=owner=="player"?"action/animator/00.unity3d":"h/anim/female/05_01_00.unity3d";
            string state=owner=="player"?"Base Layer.Main.Locomotion":"Base Layer.OLoop";
            cha.LoadAnimation(bundle,owner,null);
            Animator a=cha.animBody;a.speed=1f;a.enabled=false;
            foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true)) db.enabled=false;
            foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) db.enabled=false;
            foreach(int layer in owner=="player"?new[]{0}:new[]{73,74})
                foreach(float value in owner=="player"?new[]{0f,.37f,1f}:new[]{0f,.25f,.37f,.5f,.75f,1f})
                    foreach(float multiplier in owner=="player"?new[]{1f,1.6f}:new[]{1f})
                        foreach(float elapsed in owner=="player"?new[]{0f,.17f,.61f,.91f}:new[]{0f,.17f,.61f}) {
                a.Rebind();for(int l=1;l<a.layerCount;++l) a.SetLayerWeight(l,l==layer?1f:0f);
                var parameters=owner=="player"?
                    new[]{new StudioFloat{name="Speed",value=value},new StudioFloat{name="MotionSpeed",value=multiplier}}:
                    new[]{new StudioFloat{name="height",value=.37f},new StudioFloat{name="height1",value=.61f},new StudioFloat{name="Breast1",value=value}};
                foreach(var p in parameters) a.SetFloat(p.name,p.value);
                a.Play(state,0,0f);a.Update(0f);a.Update(elapsed);
                AnimatorStateInfo info=a.GetCurrentAnimatorStateInfo(layer),baseInfo=a.GetCurrentAnimatorStateInfo(0);
                if(baseInfo.fullPathHash!=Animator.StringToHash(state)) throw new Exception("Motion time scale transitioned from "+state);
                var clips=new List<AnimatorBlendClip>();foreach(var c in a.GetCurrentAnimatorClipInfo(layer)) clips.Add(new AnimatorBlendClip{name=c.clip.name,weight=c.weight,length=c.clip.length});
                var baseClips=new List<AnimatorBlendClip>();foreach(var c in a.GetCurrentAnimatorClipInfo(0)) baseClips.Add(new AnimatorBlendClip{name=c.clip.name,weight=c.weight,length=c.clip.length});
                var bones=new List<Bone>();var seen=new HashSet<string>();
                foreach(Transform tf in a.GetComponentsInChildren<Transform>(true))
                    if(tf.name.StartsWith("cf_",StringComparison.OrdinalIgnoreCase)&&seen.Add(tf.name))
                        bones.Add(new Bone{name=tf.name,path=PathOf(tf),position=tf.localPosition,rotation=tf.localRotation,scale=tf.localScale});
                frames.Add(new MotionTimeScaleFrame{bundle=bundle,controller=owner,state=state,layer=layer,elapsed=elapsed,
                    length=info.length,normalizedTime=info.normalizedTime,speed=info.speed,speedMultiplier=info.speedMultiplier,
                    baseLength=baseInfo.length,baseNormalizedTime=baseInfo.normalizedTime,parameters=parameters,
                    clips=clips.ToArray(),baseClips=baseClips.ToArray(),bones=bones.ToArray()});
            }
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".motion-time-scale.json",ProbeJson(frames.ToArray()));
    }
    [Serializable] private class ParallelFadeEvent {
        public int layer;public string state;public float start,blend,offset,sourceLength,fromNormalized,toNormalized,transitionNormalized;public bool transitioning;
        public Bone[] fromBones,toBones;
    }
    [Serializable] private class ParallelFadeFrame {
        public string bundle,controller,baseState;public int scenario;public float elapsed,baseLength,baseNormalizedTime;
        public StudioFloat[] parameters;public MultiLayerSnapshot[] layers;public ParallelFadeEvent[] events;public Bone[] bones,baseBones;
    }
    private static void SampleParallelFades(Animator a,string baseState,StudioFloat[] parameters,MultiLayerSnapshot[] layers,ParallelFadeEvent[] events,float elapsed,int isolated=-1) {
        float first=Math.Min(elapsed,events[0].start);SampleLayerClocks(a,baseState,parameters,layers,first,isolated);float clock=first;
        foreach(var e in events) {
            if(e.start>elapsed)break;
            a.Update(e.start-clock);clock=e.start;
            a.CrossFade(e.state,e.blend,e.layer,e.offset);a.Update(0f);
        }
        a.Update(elapsed-clock);
    }
    private static void RunParallelFadeControl(ChaControl cha) {
        const string bundle="h/anim/female/05_01_00.unity3d",owner="khr_f_02_00",baseState="Base Layer.OLoop";
        cha.LoadAnimation(bundle,owner,null);Animator a=cha.animBody;a.speed=1f;a.enabled=false;
        foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true))db.enabled=false;
        foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true))db.enabled=false;
        var parameters=new[]{new StudioFloat{name="height",value=.37f},new StudioFloat{name="height1",value=.61f},new StudioFloat{name="Breast1",value=.37f}};
        var frames=new List<ParallelFadeFrame>();
        for(int scenario=0;scenario<4;++scenario) {
            int[] indices=scenario==1?new[]{8,70}:new[]{7,8,73};var selected=new List<MultiLayerSnapshot>();
            foreach(int l in indices)selected.Add(new MultiLayerSnapshot{layer=l,state=l==7?"07:Te_kokan_sawari.Te_kokan_sawari":l==8?"08:Te_kokan_C_kosuru.Te_kokan_C_kosuru":l==70?"70:Vibe_kokan_sawari.Vibe_kokan_sawari":baseState,weight=l==7?(scenario==2?0f:.63f):l==8?.37f:.41f,startTime=l==8?.17f:l==70?.11f:0f,startPhase=l==7?.23f:l==8?.31f:l==70?.47f:0f,started=true});
            var layers=selected.ToArray();int first=scenario==1?8:7,second=scenario==1?70:8;
            var events=new[]{new ParallelFadeEvent{layer=first,state=selected.Find(l=>l.layer==first).state,start=.61f,blend=.4f,offset=.67f},new ParallelFadeEvent{layer=second,state=selected.Find(l=>l.layer==second).state,start=scenario==3?.61f:.77f,blend=.35f,offset=.43f}};
            if(scenario==3)Array.Reverse(events);
            SampleLayerClocks(a,baseState,parameters,layers,.61f);foreach(var e in events)e.sourceLength=a.GetCurrentAnimatorStateInfo(e.layer).length;
            float[] times=scenario==1?new[]{.77f,1.04f,1.69f,1.75f,2.25f}:scenario==3?new[]{.61f,.91f,1.25f,1.49f,1.82f}:new[]{.77f,1.04f,1.25f,1.65f,1.92f};
            foreach(float elapsed in times) {
                SampleParallelFades(a,baseState,parameters,layers,events,elapsed);var bones=AdditiveBones(a);var baseInfo=a.GetCurrentAnimatorStateInfo(0);
                foreach(var e in events) {
                    var source=a.GetCurrentAnimatorStateInfo(e.layer);e.transitioning=a.IsInTransition(e.layer);var target=e.transitioning?a.GetNextAnimatorStateInfo(e.layer):source;
                    if(source.fullPathHash!=Animator.StringToHash(e.state)||target.fullPathHash!=Animator.StringToHash(e.state))throw new Exception("Parallel fade state mismatch");
                    var l=selected.Find(x=>x.layer==e.layer);e.fromNormalized=(elapsed-l.startTime)/e.sourceLength+l.startPhase;
                    e.toNormalized=target.normalizedTime;e.transitionNormalized=e.transitioning?a.GetAnimatorTransitionInfo(e.layer).normalizedTime:1f;
                }
                SampleLayerClocks(a,baseState,parameters,new MultiLayerSnapshot[0],elapsed);var baseBones=AdditiveBones(a);
                foreach(var l in layers) {
                    SampleParallelFades(a,baseState,parameters,layers,events,elapsed,l.layer);l.bones=AdditiveBones(a);var info=a.GetCurrentAnimatorStateInfo(l.layer);l.length=info.length;l.normalizedTime=info.normalizedTime;
                }
                foreach(var e in events) {
                    SampleLayerClocks(a,baseState,parameters,layers,elapsed,e.layer);a.Play(e.state,e.layer,e.fromNormalized);a.Update(0f);e.fromBones=AdditiveBones(a);
                    SampleLayerClocks(a,baseState,parameters,layers,elapsed,e.layer);a.Play(e.state,e.layer,e.toNormalized);a.Update(0f);e.toBones=AdditiveBones(a);
                }
                var layerCopies=new List<MultiLayerSnapshot>();foreach(var l in layers)layerCopies.Add(new MultiLayerSnapshot{layer=l.layer,state=l.state,weight=l.weight,length=l.length,normalizedTime=l.normalizedTime,startTime=l.startTime,startPhase=l.startPhase,started=true,bones=l.bones});
                var eventCopies=new List<ParallelFadeEvent>();foreach(var e in events)eventCopies.Add(new ParallelFadeEvent{layer=e.layer,state=e.state,start=e.start,blend=e.blend,offset=e.offset,sourceLength=e.sourceLength,fromNormalized=e.fromNormalized,toNormalized=e.toNormalized,transitionNormalized=e.transitionNormalized,transitioning=e.transitioning,fromBones=e.fromBones,toBones=e.toBones});
                frames.Add(new ParallelFadeFrame{bundle=bundle,controller=owner,baseState=baseState,scenario=scenario,elapsed=elapsed,baseLength=baseInfo.length,baseNormalizedTime=baseInfo.normalizedTime,parameters=parameters,layers=layerCopies.ToArray(),events=eventCopies.ToArray(),bones=bones,baseBones=baseBones});
            }
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".parallel-fade.json",ProbeJson(frames.ToArray()));
    }
    [Serializable] private class LayerFadeFrame {
        public string bundle,controller,baseState;public int layer;
        public float start,elapsed,blend,offset,sourceLength,fromNormalized,toNormalized,transitionNormalized,baseLength,baseNormalizedTime;
        public bool transitioning;public StudioFloat[] parameters;public MultiLayerSnapshot[] layers;
        public Bone[] bones,baseBones,fromBones,toBones,finalFromBones,finalToBones;
    }
    private static void RunLayerFadeControl(ChaControl cha) {
        const string bundle="h/anim/female/05_01_00.unity3d",owner="khr_f_02_00",baseState="Base Layer.OLoop";
        cha.LoadAnimation(bundle,owner,null);Animator a=cha.animBody;a.speed=1f;a.enabled=false;
        foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true))db.enabled=false;
        foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true))db.enabled=false;
        var parameters=new[]{new StudioFloat{name="height",value=.37f},new StudioFloat{name="height1",value=.61f},new StudioFloat{name="Breast1",value=.37f}};
        var frames=new List<LayerFadeFrame>();
        for(int scenario=0;scenario<4;++scenario) {
            int fading=scenario==1?8:scenario==2?70:7;
            int[] indices=scenario==2?new[]{8,70}:new[]{7,8,73};
            var layers=new List<MultiLayerSnapshot>();
            foreach(int l in indices)layers.Add(new MultiLayerSnapshot{layer=l,state=l==7?"07:Te_kokan_sawari.Te_kokan_sawari":l==8?"08:Te_kokan_C_kosuru.Te_kokan_C_kosuru":l==70?"70:Vibe_kokan_sawari.Vibe_kokan_sawari":baseState,weight=l==7?(scenario==3?0f:.63f):l==8?.37f:.41f,startTime=l==8?.17f:l==70?.11f:0f,startPhase=l==7?.23f:l==8?.31f:l==70?.47f:0f,started=true});
            var selected=layers.ToArray();var active=layers.Find(l=>l.layer==fading);float start=.61f,blend=.4f,offset=.67f;
            SampleLayerClocks(a,baseState,parameters,selected,start);float length=a.GetCurrentAnimatorStateInfo(fading).length;
            foreach(float delta in new[]{0f,length*blend*.25f,length*blend*.75f,length*blend*1.3f,length*blend*2f}) {
                float elapsed=start+delta;
                SampleLayerClocks(a,baseState,parameters,selected,start);a.CrossFade(active.state,blend,fading,offset);a.Update(0f);a.Update(delta);
                bool transitioning=a.IsInTransition(fading);var src=a.GetCurrentAnimatorStateInfo(fading);var dst=transitioning?a.GetNextAnimatorStateInfo(fading):src;
                if(src.fullPathHash!=Animator.StringToHash(active.state)||dst.fullPathHash!=Animator.StringToHash(active.state))throw new Exception("Layer CrossFade unexpected state");
                var baseInfo=a.GetCurrentAnimatorStateInfo(0);var bones=AdditiveBones(a);float alpha=transitioning?a.GetAnimatorTransitionInfo(fading).normalizedTime:1f;
                float fromNorm=(elapsed-active.startTime)/length+active.startPhase,toNorm=dst.normalizedTime;
                SampleLayerClocks(a,baseState,parameters,new MultiLayerSnapshot[0],elapsed);var baseBones=AdditiveBones(a);
                foreach(var l in selected) {
                    SampleLayerClocks(a,baseState,parameters,selected,l.layer==fading?start:elapsed,l.layer);
                    if(l.layer==fading){a.CrossFade(l.state,blend,l.layer,offset);a.Update(0f);a.Update(delta);}
                    var state=a.GetCurrentAnimatorStateInfo(l.layer);l.length=state.length;
                    l.normalizedTime=l.layer==fading?toNorm:state.normalizedTime;l.bones=AdditiveBones(a);
                }
                Action<float,int> isolated=(phase,only)=>{SampleLayerClocks(a,baseState,parameters,selected,elapsed,only);a.Play(active.state,fading,phase);a.Update(0f);};
                isolated(fromNorm,fading);var fromBones=AdditiveBones(a);isolated(toNorm,fading);var toBones=AdditiveBones(a);
                isolated(fromNorm,-1);var finalFrom=AdditiveBones(a);isolated(toNorm,-1);var finalTo=AdditiveBones(a);
                // Per-frame copies: later samples must not mutate snapshots.
                var copies=new List<MultiLayerSnapshot>();foreach(var l in selected)copies.Add(new MultiLayerSnapshot{layer=l.layer,state=l.state,weight=l.weight,length=l.length,normalizedTime=l.normalizedTime,startTime=l.startTime,startPhase=l.startPhase,started=true,bones=l.bones});
                frames.Add(new LayerFadeFrame{bundle=bundle,controller=owner,baseState=baseState,layer=fading,start=start,elapsed=elapsed,blend=blend,offset=offset,sourceLength=length,fromNormalized=fromNorm,toNormalized=toNorm,transitionNormalized=alpha,transitioning=transitioning,baseLength=baseInfo.length,baseNormalizedTime=baseInfo.normalizedTime,parameters=parameters,layers=copies.ToArray(),bones=bones,baseBones=baseBones,fromBones=fromBones,toBones=toBones,finalFromBones=finalFrom,finalToBones=finalTo});
            }
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".layer-fade.json",ProbeJson(frames.ToArray()));
    }
    [Serializable] private class CrossFadeFrame {
        public string bundle,controller,fromState,toState;public float speed,start,elapsed,blend,offset,fromLength,toLength,fromNormalized,toNormalized,transitionNormalized;
        public bool transitioning;public Bone[] bones,fromBones,toBones;
    }
    private static void RunCrossFadeControl(ChaControl cha) {
        const string bundle="action/animator/00.unity3d",owner="base";cha.LoadAnimation(bundle,owner,null);
        Animator a=cha.animBody;a.speed=1f;a.enabled=false;
        foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true)) db.enabled=false;
        foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) db.enabled=false;
        var frames=new List<CrossFadeFrame>();
        for(int scenario=0;scenario<3;++scenario) {
            string from=scenario==1?"Base Layer.Main.Locomotion_Anger":"Base Layer.Main.Idle",to=scenario==1?"Base Layer.Main.Idle":"Base Layer.Main.Locomotion_Anger";
            float speed=scenario==1?1.6f:1f,blend=scenario==2?0f:.4f,offset=scenario==1?.2f:.37f,start=.61f;
            Action reset=()=>{a.Rebind();for(int l=1;l<a.layerCount;++l)a.SetLayerWeight(l,0f);a.SetFloat("MotionSpeed",speed);a.SetFloat("Speed",.37f);};
            reset();a.Play(from,0,0f);a.Update(0f);a.Update(start);float length=a.GetCurrentAnimatorStateInfo(0).length;
            float nominal=length*.4f;
            foreach(float delta in new[]{0f,nominal*.25f,nominal*.75f,nominal*1.3f,nominal*2f}) {
                reset();a.Play(from,0,0f);a.Update(0f);a.Update(start);a.CrossFade(to,blend,0,offset);a.Update(0f);a.Update(delta);
                bool transitioning=a.IsInTransition(0);var src=a.GetCurrentAnimatorStateInfo(0);var dst=transitioning?a.GetNextAnimatorStateInfo(0):src;
                if(src.fullPathHash!=Animator.StringToHash(transitioning?from:to)||dst.fullPathHash!=Animator.StringToHash(to))throw new Exception("Unexpected automatic transition");
                var bones=AdditiveBones(a);float transition=transitioning?a.GetAnimatorTransitionInfo(0).normalizedTime:1f;
                float fromNorm=(start+delta)/length,toNorm=dst.normalizedTime;
                reset();a.Play(from,0,fromNorm);a.Update(0f);var fromBones=AdditiveBones(a);
                reset();a.Play(to,0,toNorm);a.Update(0f);var toBones=AdditiveBones(a);
                frames.Add(new CrossFadeFrame{bundle=bundle,controller=owner,fromState=from,toState=to,speed=speed,start=start,elapsed=start+delta,blend=blend,offset=offset,fromLength=length,toLength=dst.length,fromNormalized=fromNorm,toNormalized=toNorm,transitionNormalized=transition,transitioning=transitioning,bones=bones,fromBones=fromBones,toBones=toBones});
            }
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".crossfade.json",ProbeJson(frames.ToArray()));
    }
    private static void RunAnimatorClockControl(ChaControl cha) {
        if(Environment.GetEnvironmentVariable("OK_PROBE_ANIMATOR_CLOCK_LAYER")=="1") {RunAnimatorClockLayerControl(cha);return;}
        cha.LoadAnimation("action/animator/00.unity3d","base",null);
        Animator animator=cha.animBody;animator.speed=1f;animator.enabled=false;
        foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true)) db.enabled=false;
        foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) db.enabled=false;
        var frames=new List<AnimatorClockFrame>();
        foreach(string state in new[]{"Base Layer.Main.Idle","Base Layer.Main.Locomotion_Anger"})
            foreach(float value in new[]{.4f,1f,1.6f}) foreach(float elapsed in new[]{0f,.17f,.43f,.91f}) {
                animator.Rebind();
                for(int layer=1;layer<animator.layerCount;++layer) animator.SetLayerWeight(layer,0f);
                animator.SetFloat("MotionSpeed",value);animator.SetFloat("Speed",.37f);
                animator.Play(state,0,0f);animator.Update(0f);animator.Update(elapsed);
                AnimatorStateInfo info=animator.GetCurrentAnimatorStateInfo(0);
                if(info.fullPathHash!=Animator.StringToHash(state)) throw new Exception("Clock control transitioned from "+state);
                var clips=new List<AnimatorBlendClip>();
                foreach(AnimatorClipInfo c in animator.GetCurrentAnimatorClipInfo(0)) clips.Add(new AnimatorBlendClip{name=c.clip.name,weight=c.weight,length=c.clip.length});
                var bones=new List<Bone>();var seen=new HashSet<string>();
                foreach(Transform tf in cha.objAnim.GetComponentsInChildren<Transform>(true))
                    if(tf.name.StartsWith("cf_",StringComparison.OrdinalIgnoreCase)&&seen.Add(tf.name))
                        bones.Add(new Bone{name=tf.name,path=PathOf(tf),position=tf.localPosition,rotation=tf.localRotation,scale=tf.localScale});
                frames.Add(new AnimatorClockFrame{controller="base",baseState="",layer=0,state=state,parameter="MotionSpeed",value=value,elapsed=elapsed,
                    normalizedTime=info.normalizedTime,length=info.length,speed=info.speed,speedMultiplier=info.speedMultiplier,
                    clips=clips.ToArray(),bones=bones.ToArray()});
            }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".animator-clock.json",ProbeJson(frames.ToArray()));
    }
    private static void RunAnimatorClockLayerControl(ChaControl cha) {
        const string controller="khs3_f_base3",state="05:Te_mune_D_momu_L.Te_mune_D_momu_L",baseState="Base Layer.Idle";
        cha.LoadAnimation("studio/anime/00.unity3d",controller,null);
        Animator animator=cha.animBody;animator.speed=1f;animator.enabled=false;
        foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true)) db.enabled=false;
        foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) db.enabled=false;
        var frames=new List<AnimatorClockFrame>();
        foreach(float value in new[]{.2f,.5f,.8f}) foreach(float elapsed in new[]{0f,.17f,.43f,.91f}) {
            animator.Rebind();
            for(int layer=1;layer<animator.layerCount;++layer) animator.SetLayerWeight(layer,layer==5?1f:0f);
            animator.SetFloat("height",.37f);animator.SetFloat("Breast",.61f);
            animator.SetFloat("muneR_X",.31f);animator.SetFloat("muneR_Y",.79f);animator.SetFloat("speedHand",value);
            animator.Play(baseState,0,0f);animator.Play(state,5,0f);animator.Update(0f);animator.Update(elapsed);
            AnimatorStateInfo info=animator.GetCurrentAnimatorStateInfo(5);
            if(info.fullPathHash!=Animator.StringToHash(state)) throw new Exception("Clock layer control transitioned");
            var clips=new List<AnimatorBlendClip>();
            foreach(AnimatorClipInfo c in animator.GetCurrentAnimatorClipInfo(5)) clips.Add(new AnimatorBlendClip{name=c.clip.name,weight=c.weight,length=c.clip.length});
            var bones=new List<Bone>();var seen=new HashSet<string>();
            foreach(Transform tf in cha.objAnim.GetComponentsInChildren<Transform>(true))
                if(tf.name.StartsWith("cf_",StringComparison.OrdinalIgnoreCase)&&seen.Add(tf.name))
                    bones.Add(new Bone{name=tf.name,path=PathOf(tf),position=tf.localPosition,rotation=tf.localRotation,scale=tf.localScale});
            frames.Add(new AnimatorClockFrame{controller=controller,baseState=baseState,layer=5,state=state,parameter="speedHand",value=value,
                elapsed=elapsed,normalizedTime=info.normalizedTime,length=info.length,speed=info.speed,speedMultiplier=info.speedMultiplier,clips=clips.ToArray(),bones=bones.ToArray()});
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".animator-clock.json",ProbeJson(frames.ToArray()));
    }

    [Serializable] private class PVFrame {
        public int stage,sample;public bool fk;public bool[] enabled;
        public string[] pv,bone;public string callback;public Bone[] before,after;
    }
    private static Bone PVTransform(Transform tf) {
        return new Bone{name=tf.name,path=PathOf(tf),position=tf.localPosition,rotation=tf.localRotation,scale=tf.localScale,worldPosition=tf.position};
    }
    private static void RunPVCopyControl(ChaControl cha) {
        var studio=UnityEngine.Object.FindObjectOfType<Studio.Studio>();Studio.OCIChar oci=null;
        foreach(var item in studio.dicObjectCtrl.Values) {var c=item as Studio.OCIChar;if(c!=null&&c.charInfo==cha) {oci=c;break;}}
        if(oci==null) throw new Exception("Actual OCIChar missing");
        var copy=oci.preparation.pvCopy;var type=copy.GetType();
        var pv=(GameObject[])type.GetField("pv",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(copy);
        var bone=(GameObject[])type.GetField("bone",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(copy);
        MethodInfo callback=null;
        foreach(var m in type.GetMethods(BindingFlags.Instance|BindingFlags.NonPublic))
            if(m.Name.Contains("<Start>")&&m.ReturnType==typeof(void)&&m.GetParameters().Length==1) {
                if(callback!=null) throw new Exception("Ambiguous compiled PVCopy callback");callback=m;
            }
        if(callback==null||pv.Length!=4||bone.Length!=4) throw new Exception("Actual PVCopy callback/references missing");
        var frames=new List<PVFrame>();int[][] identities={new[]{0,3,0},new[]{2,128,0},new[]{2,128,0},new[]{2,128,0},new[]{0,3,0}};
        for(int stage=0;stage<identities.Length;++stage) {
            // Actual kinematic-mode setter assigns the saved FK and PV gates.
            oci.ActiveKinematicMode(Studio.OICharInfo.KinematicMode.FK,stage==2,true);var id=identities[stage];oci.LoadAnime(id[0],id[1],id[2],0f);
            cha.animBody.enabled=false;cha.animBody.speed=1f;
            foreach(DynamicBone db in cha.objAnim.GetComponentsInChildren<DynamicBone>(true)) db.enabled=false;
            foreach(DynamicBone_Ver02 db in cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true)) db.enabled=false;
            for(int sample=0;sample<3;++sample) {
                cha.animBody.Play(Studio.Info.Instance.dicAnimeLoadInfo[id[0]][id[1]][id[2]].clip,0,0f);cha.animBody.Update(0f);cha.animBody.Update(sample*.23f);
                // Deliberately distinguish targets from Animator joints. Include
                // parent rotation and nonuniform scale in the independent oracle.
                for(int k=0;k<4;++k) {
                    pv[k].transform.localPosition+=new Vector3(.013f*(k+1),-.017f*(sample+1),.011f);
                    pv[k].transform.localRotation=Quaternion.Euler(13f+k*7f,23f+sample*11f,-17f)*pv[k].transform.localRotation;
                    pv[k].transform.localScale=new Vector3(.91f+k*.07f,1.17f-sample*.03f,.83f+k*.04f);
                    bone[k].transform.parent.localScale=new Vector3(.87f+k*.03f,1.13f,.93f);
                }
                var all=new List<Bone>();var seen=new HashSet<string>();
                foreach(var tf in cha.objAnim.GetComponentsInChildren<Transform>(true)) if(seen.Add(PathOf(tf))) all.Add(PVTransform(tf));
                for(var tf=cha.objAnim.transform.parent;tf!=null;tf=tf.parent) if(seen.Add(PathOf(tf))) all.Add(PVTransform(tf));
                var flags=new bool[4];var sources=new string[4];var destinations=new string[4];
                for(int k=0;k<4;++k) {flags[k]=copy[k];sources[k]=PathOf(pv[k].transform);destinations[k]=PathOf(bone[k].transform);}
                callback.Invoke(copy,new[]{Activator.CreateInstance(callback.GetParameters()[0].ParameterType)});
                var after=new Bone[4];for(int k=0;k<4;++k) after[k]=PVTransform(bone[k].transform);
                frames.Add(new PVFrame{stage=stage,sample=sample,fk=oci.oiCharInfo.enableFK,enabled=flags,pv=sources,bone=destinations,callback=callback.Name,before=all.ToArray(),after=after});
            }
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".pv.json",ProbeJson(frames.ToArray()));
    }
    [Serializable] private class RendererSnapshot {
        public string path,type,mesh,rootBone,shadowCasting;public string[] inactiveAncestors;public bool active,activeSelf,enabled;
        public Vector3 min,max;public int vertexCount;public string[] bones;
        public string[] materials,shaders,components;public int[] queues;
    }
    [Serializable] private class RendererMeshSnapshot {
        public string path,name;public Vector3[] vertices,worldVertices;public Matrix4x4[] bindposes;public BoneWeight[] weights;
        public Vector3[] shaderWorldVertices;public Matrix4x4 rendererWorld;public string quality;
        public Bone[] bones;public Matrix4x4[] boneWorlds;
        // Actual post-plugin sharedMesh attributes; an empty colour array is
        // evidence of a missing stream, not evidence of the GPU default COLOR.
        public Vector3[] normals;public Vector4[] tangents;public Color[] colors;
        public Color32[] colors32;public Vector2[] uv0,uv1,uv2,uv3;
    }
    [StructLayout(LayoutKind.Sequential)] private struct D3DTexture2DDesc {
        public uint width,height,mipLevels,arraySize,format,sampleCount,sampleQuality,usage,bindFlags,cpuAccessFlags,miscFlags;
    }
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate void D3DGetDesc(IntPtr resource,out D3DTexture2DDesc desc);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate void D3DGetObject(IntPtr obj,out IntPtr result);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate uint D3DRelease(IntPtr obj);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int D3DCreateTexture(IntPtr device,ref D3DTexture2DDesc desc,IntPtr initial,out IntPtr texture);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate void D3DCopy(IntPtr context,IntPtr dst,uint dstSub,uint x,uint y,uint z,IntPtr src,uint srcSub,IntPtr box);
    [StructLayout(LayoutKind.Sequential)] private struct D3DMapped {public IntPtr data;public uint rowPitch,depthPitch;}
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int D3DMap(IntPtr context,IntPtr resource,uint sub,uint type,uint flags,out D3DMapped mapped);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate void D3DUnmap(IntPtr context,IntPtr resource,uint sub);
    private static Delegate D3DMethod(IntPtr obj,int index,Type type) {
        return Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(Marshal.ReadIntPtr(obj),index*IntPtr.Size),type);
    }
    private static void WriteDirectMip(Texture texture,string suffix,int level) {
        if(SystemInfo.graphicsDeviceType.ToString()!="Direct3D11")throw new NotSupportedException("Direct mip control requires D3D11");
        IntPtr source=texture.GetNativeTexturePtr(),device=IntPtr.Zero,context=IntPtr.Zero,staging=IntPtr.Zero;
        var get=(D3DGetDesc)D3DMethod(source,10,typeof(D3DGetDesc));D3DTexture2DDesc desc;get(source,out desc);
        uint format=desc.format;if(format!=27&&format!=28&&format!=29&&format!=87&&format!=90&&format!=91)throw new NotSupportedException("Control format "+format);
        // Unity uses typeless resources with separate linear/sRGB views.
        // Staging needs no shader view; a compatible UNORM format permits Copy.
        if(format==27)desc.format=28;else if(format==90)desc.format=87;
        desc.width=Math.Max(1,desc.width>>level);desc.height=Math.Max(1,desc.height>>level);desc.mipLevels=1;desc.arraySize=1;
        desc.usage=3;desc.bindFlags=0;desc.cpuAccessFlags=0x20000;desc.miscFlags=0;
        try {
            ((D3DGetObject)D3DMethod(source,3,typeof(D3DGetObject)))(source,out device);
            ((D3DGetObject)D3DMethod(device,40,typeof(D3DGetObject)))(device,out context);
            int hr=((D3DCreateTexture)D3DMethod(device,5,typeof(D3DCreateTexture)))(device,ref desc,IntPtr.Zero,out staging);
            if(hr<0)Marshal.ThrowExceptionForHR(hr);
            ((D3DCopy)D3DMethod(context,46,typeof(D3DCopy)))(context,staging,0,0,0,0,source,(uint)level,IntPtr.Zero);
            D3DMapped mapped;hr=((D3DMap)D3DMethod(context,14,typeof(D3DMap)))(context,staging,0,1,0,out mapped);
            if(hr<0)Marshal.ThrowExceptionForHR(hr);
            var bytes=new byte[desc.width*desc.height*4];
            try {for(int y=0;y<desc.height;y++)Marshal.Copy(new IntPtr(mapped.data.ToInt64()+y*mapped.rowPitch),bytes,(int)(y*desc.width*4),(int)(desc.width*4));}
            finally {((D3DUnmap)D3DMethod(context,15,typeof(D3DUnmap)))(context,staging,0);}
            if(format==87||format==90||format==91)for(int i=0;i<bytes.Length;i+=4){byte r=bytes[i];bytes[i]=bytes[i+2];bytes[i+2]=r;}
            File.WriteAllBytes(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+suffix+".direct.mip"+level+".rgba8",bytes);
        } finally {foreach(var obj in new[]{staging,context,device})if(obj!=IntPtr.Zero)((D3DRelease)D3DMethod(obj,2,typeof(D3DRelease)))(obj);}
    }
    private static int NativeMipCount(Texture texture,int fallback) {
        if(SystemInfo.graphicsDeviceType.ToString()!="Direct3D11")return fallback;
        IntPtr resource=texture.GetNativeTexturePtr();if(resource==IntPtr.Zero)return fallback;
        // ID3D11Texture2D::GetDesc follows IUnknown(3), DeviceChild(4), Resource(3).
        IntPtr method=Marshal.ReadIntPtr(Marshal.ReadIntPtr(resource),10*IntPtr.Size);
        var get=(D3DGetDesc)Marshal.GetDelegateForFunctionPointer(method,typeof(D3DGetDesc));
        D3DTexture2DDesc desc;get(resource,out desc);
        if(desc.width!=(uint)texture.width||desc.height!=(uint)texture.height||desc.mipLevels<1||desc.mipLevels>32)
            throw new InvalidDataException("Native texture descriptor mismatch");
        return (int)desc.mipLevels;
    }
    private static void WriteTextureSampling(Texture texture,string suffix,StreamWriter writer,string property,bool dumpMips=false) {
        if(texture==null)return;
        var rt=texture as RenderTexture;var td=texture as Texture2D;
        int count=td!=null?td.mipmapCount:rt!=null&&rt.useMipMap?1+(int)Mathf.Floor(Mathf.Log(Mathf.Max(rt.width,rt.height),2)):1;
        if(rt!=null&&dumpMips)count=NativeMipCount(texture,count);
        var biasProperty=texture.GetType().GetProperty("mipMapBias");float bias=biasProperty==null?0:(float)biasProperty.GetValue(texture,null);
        var colorSpaceProperty=texture.GetType().GetProperty("activeTextureColorSpace",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic);
        string colorSpace=rt!=null?rt.sRGB.ToString():colorSpaceProperty!=null?colorSpaceProperty.GetValue(texture,null).ToString():"unknown";
        writer.WriteLine("sampling\t"+suffix+"\t"+property+"\t"+texture.GetType().Name+"\t"+count+"\t"+texture.filterMode+"\t"+texture.wrapModeU+"\t"+texture.wrapModeV+"\t"+texture.anisoLevel+"\t"+(biasProperty==null?"unknown":Num(bias))+"\t"+colorSpace);
        if(rt==null||!dumpMips)return;
        for(int level=0;level<count;level++) {
            if(suffix.Contains(".mip-control."))WriteDirectMip(texture,suffix,level);
            int width=Mathf.Max(1,rt.width>>level),height=Mathf.Max(1,rt.height>>level);
            var desc=rt.descriptor;desc.width=width;desc.height=height;desc.depthBufferBits=0;desc.useMipMap=false;desc.autoGenerateMips=false;desc.mipCount=1;
            var copy=new RenderTexture(desc);copy.Create();
            var cpu=new Texture2D(width,height,TextureFormat.RGBA32,false,true);var previous=RenderTexture.active;
            try {
                Graphics.CopyTexture(rt,0,level,copy,0,0);RenderTexture.active=copy;
                cpu.ReadPixels(new Rect(0,0,width,height),0,0);cpu.Apply();
                File.WriteAllBytes(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+suffix+".mip"+level+".rgba8",cpu.GetRawTextureData());
            } finally {RenderTexture.active=previous;UnityEngine.Object.Destroy(cpu);UnityEngine.Object.Destroy(copy);}
        }
    }

    private static void WriteTexture(Texture texture,string suffix) {
        if(texture==null)return;
        var previous=RenderTexture.active;bool previousWrite=GL.sRGBWrite;var target=texture as RenderTexture;bool temporary=target==null;Texture2D png=null;
        try {
            if(temporary){target=RenderTexture.GetTemporary(texture.width,texture.height,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.sRGB);GL.sRGBWrite=true;Graphics.Blit(texture,target);}
            RenderTexture.active=target;png=new Texture2D(texture.width,texture.height,TextureFormat.RGBA32,false,false);
            png.ReadPixels(new Rect(0,0,texture.width,texture.height),0,0);png.Apply();
            File.WriteAllBytes(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+suffix+".png",png.EncodeToPNG());
        } finally {RenderTexture.active=previous;GL.sRGBWrite=previousWrite;if(png!=null)UnityEngine.Object.Destroy(png);if(temporary&&target!=null)RenderTexture.ReleaseTemporary(target);}
    }
    // Sample native mask RGB/sRGB views at authored mesh UVs on the GPU.
    // Unlit/Texture forces output alpha=1: do not use this for packed normals.
    // Constant UV quads force mip0; ReadPixels rows correspond to sample index.
    private static void WriteRendererUVSamples(Mesh source,Material material,string property,string suffix,bool coverage=false) {
        var texture=material.GetTexture(property);if(texture==null||source.uv.Length==0)return;
        var shader=Shader.Find(coverage?"Unlit/Transparent":"Unlit/Texture");if(shader==null)throw new Exception("Native texture sampling shader absent");
        var coordinates=new List<Vector2>(source.uv);
        if(coverage)for(int y=0;y<64;y++)for(int x=0;x<64;x++)coordinates.Add(new Vector2((x+.5f)/64,(y+.5f)/64));
        var uv=coordinates.ToArray();int width=64,height=(uv.Length+width-1)/width;
        var scale=material.GetTextureScale(property);var offset=material.GetTextureOffset(property);
        var sample=new Material(shader);sample.mainTexture=texture;
        var rt=new RenderTexture(width,height,0,RenderTextureFormat.ARGBFloat,RenderTextureReadWrite.Linear);rt.Create();
        var cpu=new Texture2D(width,height,TextureFormat.RGBAFloat,false,true);var active=RenderTexture.active;bool write=GL.sRGBWrite;
        try {
            RenderTexture.active=rt;GL.sRGBWrite=false;GL.Clear(true,true,coverage?Color.clear:new Color(-7,-7,-7,-7));GL.PushMatrix();
            try {GL.LoadOrtho();if(!sample.SetPass(0))throw new Exception("Native UV sampling pass failed");
                GL.Begin(GL.QUADS);GL.Color(Color.white);
                for(int i=0;i<uv.Length;i++) {
                    int x=i%width,y=i/width;float left=(float)x/width,right=(float)(x+1)/width,bottom=(float)y/height,top=(float)(y+1)/height;
                    var coord=Vector2.Scale(uv[i],scale)+offset;
                    // Render both windings; only one survives this source shader's cull.
                    foreach(var v in new[]{new Vector3(left,bottom,.5f),new Vector3(left,top,.5f),new Vector3(right,top,.5f),new Vector3(right,bottom,.5f),new Vector3(left,bottom,.5f),new Vector3(right,bottom,.5f),new Vector3(right,top,.5f),new Vector3(left,top,.5f)}){GL.TexCoord2(coord.x,coord.y);GL.Vertex(v);}
                }GL.End();}
            finally{GL.PopMatrix();}
            cpu.ReadPixels(new Rect(0,0,width,height),0,0);cpu.Apply();var pixels=cpu.GetRawTextureData();
            var bytes=new byte[uv.Length*16];Array.Copy(pixels,bytes,bytes.Length);
            for(int i=0;i<uv.Length*4;i++)if(BitConverter.ToSingle(bytes,i*4)<-1)throw new Exception("Native UV GPU sample was not drawn at channel "+i);
            var path=Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+suffix;
            File.WriteAllBytes(path+".uv-samples.rgba32f",bytes);
            File.WriteAllText(path+".uv-samples.json",coverage?
                "{\"scope\":\"native Unlit/Transparent RGB coverage over black, mip0; dense 64x64 UV grid appended\",\"alpha\":\"blended output; not source alpha\",\"mesh_count\":"+source.uv.Length+",\"count\":"+uv.Length+"}":
                "{\"scope\":\"native mask RGB at mesh UV, mip0\",\"alpha\":\"shader output one; not source alpha\",\"count\":"+uv.Length+"}");
            using(var writer=new StreamWriter(path+".uv-samples.tsv")){writer.WriteLine("index\tu\tv");for(int i=0;i<uv.Length;i++){var p=Vector2.Scale(uv[i],scale)+offset;writer.WriteLine(i+"\t"+Num(p.x)+"\t"+Num(p.y));}}
        } finally {RenderTexture.active=active;GL.sRGBWrite=write;UnityEngine.Object.Destroy(cpu);UnityEngine.Object.Destroy(rt);UnityEngine.Object.Destroy(sample);}
    }
    [Serializable] public class MaterialTextureControl {
        public string scope,property,material,inputPng,textureType,filter;
        public int previousWrap,wrapU,wrapV,mips,sourceSamples,stressSamples;public bool srgb,declared=true,accepted=true;
    }
    // Invoke the installed plugin's real TextureContainer/SetTexture API on a
    // temporary material. No card, scene, config or original material is saved.
    private static void WriteMaterialTextureControls(Renderer renderer,Mesh mesh,int index,int slot) {
        Type container=null,api=null;
        foreach(var assembly in AppDomain.CurrentDomain.GetAssemblies()) {
            if(container==null)container=assembly.GetType("KK_Plugins.TextureContainer");
            if(api==null)api=assembly.GetType("MaterialEditorAPI.MaterialAPI");
        }
        if(container==null||api==null)throw new Exception("Installed MaterialEditor texture API absent");
        var originals=renderer.sharedMaterials;var clone=new Material(originals[slot]);clone.name="OpenKoikatsuMeSamplerControl";
        var owned=new List<Material>();owned.Add(clone);
        var sampleMesh=new Mesh();var coordinates=mesh.uv;int sourceCount=coordinates.Length;
        var stress=new[]{new Vector2(-.75f,-.25f),new Vector2(1.75f,.25f),new Vector2(2.25f,.75f),new Vector2(-1.25f,1.75f),
            new Vector2(.25f,-1.75f),new Vector2(.75f,2.75f),new Vector2(3.25f,-2.25f),new Vector2(-2.75f,3.75f)};
        Array.Resize(ref coordinates,sourceCount+stress.Length);Array.Copy(stress,0,coordinates,sourceCount,stress.Length);
        sampleMesh.vertices=new Vector3[coordinates.Length];sampleMesh.uv=coordinates;
        var hierarchy=new Dictionary<Renderer,Material[]>();var originalSet=new HashSet<Material>();
        // The installed API traverses children and accesses their materials
        // too. Preserve the whole traversed hierarchy, including new instances.
        foreach(var child in renderer.GetComponentsInChildren<Renderer>(true)) {
            var values=child.sharedMaterials;hierarchy.Add(child,values);foreach(var value in values)originalSet.Add(value);
        }
        var temporary=new Texture2D(2,2,TextureFormat.RGBA32,false,true);
        temporary.SetPixels32(new[]{new Color32(128,64,32,128),new Color32(32,128,64,192),new Color32(64,32,128,64),new Color32(192,128,64,255)});temporary.Apply();
        var bytes=temporary.EncodeToPNG();var input=Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".renderer"+index+".me"+slot+".input.png";File.WriteAllBytes(input,bytes);
        try {
            var materials=(Material[])originals.Clone();materials[slot]=clone;renderer.sharedMaterials=materials;
            // MaterialAPI.GetMaterials accesses renderer.materials, which can
            // instantiate a shared clone. Operate on the actual bound instance.
            foreach(var material in renderer.materials)if(Array.IndexOf(originals,material)<0&&!owned.Contains(material))owned.Add(material);
            clone=renderer.materials[slot];clone.name="OpenKoikatsuMeSamplerControl";
            foreach(var property in new[]{"_MainTex","_ColorMask","_DetailMask","_LineMask","_AlphaMask","_AnotherRamp","_EmissionMask","_ReflectionMapCap","_ReflectMapDetail"}) {
                if(!clone.HasProperty(property)) {
                    var instance=Activator.CreateInstance(container,new object[]{bytes});
                    try {
                        var texture=(Texture)container.GetProperty("Texture").GetValue(instance,null);
                        var accepted=(bool)api.GetMethod("SetTexture").Invoke(null,new object[]{renderer.gameObject,"OpenKoikatsuMeSamplerControl",property.Substring(1),texture});
                        if(accepted)throw new Exception("Native ME accepted an undeclared property: "+property);
                        var ignored=new MaterialTextureControl{scope="installed MaterialAPI HasProperty rejection; no GPU sampling",property=property,
                            material=originals[slot].name,inputPng=input,declared=false,accepted=false};
                        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".renderer"+index+".me"+slot+".ignored."+property+".material-texture-control.json",JsonUtility.ToJson(ignored,true));
                    } finally {((IDisposable)instance).Dispose();}
                    continue;
                }
                foreach(int wrap in new[]{-1,0,1,2}) {
                    // A prior runtime binding must take precedence over the
                    // original prefab sampler; null uses source API Repeat.
                    temporary.wrapMode=(TextureWrapMode)Mathf.Max(0,wrap);clone.SetTexture(property,wrap<0?null:temporary);
                    var instance=Activator.CreateInstance(container,new object[]{bytes});
                    try {
                        var texture=(Texture)container.GetProperty("Texture").GetValue(instance,null);
                        var success=(bool)api.GetMethod("SetTexture").Invoke(null,new object[]{renderer.gameObject,"OpenKoikatsuMeSamplerControl",property.Substring(1),texture});
                        if(!success||clone.GetTexture(property)!=texture)throw new Exception("Native ME SetTexture failed: "+property);
                        var rt=texture as RenderTexture;if(rt==null)throw new Exception("Native ME factory did not create RenderTexture");
                        var suffix=".renderer"+index+".me"+slot+".wrap"+wrap+"."+property;
                        WriteRendererUVSamples(sampleMesh,clone,property,suffix);
                        var row=new MaterialTextureControl{scope="installed TextureContainer/SetTexture; source/stress UV mip0 RGB, alpha and normal conversion excluded",
                            property=property,material=originals[slot].name,inputPng=input,textureType=texture.GetType().Name,
                            previousWrap=wrap,wrapU=(int)texture.wrapModeU,wrapV=(int)texture.wrapModeV,filter=texture.filterMode.ToString(),srgb=rt.sRGB,mips=rt.mipmapCount,
                            sourceSamples=sourceCount,stressSamples=stress.Length};
                        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+suffix+".material-texture-control.json",JsonUtility.ToJson(row,true));
                    } finally {((IDisposable)instance).Dispose();}
                }
            }
        } finally {
            foreach(var entry in hierarchy) {
                foreach(var material in entry.Key.sharedMaterials)if(material!=null&&!originalSet.Contains(material)&&!owned.Contains(material))owned.Add(material);
                entry.Key.sharedMaterials=entry.Value;
            }
            foreach(var material in owned)UnityEngine.Object.Destroy(material);UnityEngine.Object.Destroy(temporary);UnityEngine.Object.Destroy(sampleMesh);
        }
    }
    // Opt-in source shader controls, framed from the actual palette-space mesh.
    // Keep every change on a cloned material and restore camera/renderer state.
    private static void CaptureRendererMaterialControls(Renderer renderer,Vector3[] world,int index) {
        Camera camera=Camera.main;if(camera==null||world.Length==0)throw new Exception("Missing renderer capture inputs");
        Bounds bounds=new Bounds(world[0],Vector3.zero);foreach(var p in world)bounds.Encapsulate(p);
        var output=Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".renderer"+index;
        var backupObject=new GameObject("OpenKoikatsu renderer camera backup");var backup=backupObject.AddComponent<Camera>();backup.enabled=false;backup.CopyFrom(camera);
        var position=camera.transform.position;var rotation=camera.transform.rotation;var target=camera.targetTexture;var active=RenderTexture.active;
        var ambient=Shader.GetGlobalVector("_ambientshadowG");
        var verticalShift=Shader.GetGlobalFloat("_KanoVerShift");
        var disabledCameraEffects=new List<Behaviour>();
        if(Environment.GetEnvironmentVariable("OK_PROBE_RAW_MATERIAL_CAPTURE")=="1")
            foreach(var effect in camera.GetComponents<MonoBehaviour>())if(effect.enabled){disabledCameraEffects.Add(effect);effect.enabled=false;}
        var original=renderer.sharedMaterials;var controls=Array.ConvertAll(original,delegate(Material m){return m==null?null:new Material(m);});
        int scale=Environment.GetEnvironmentVariable("OK_PROBE_MATERIAL_CAPTURE_2X")=="1"?2:1;
        int width=900*scale,height=1080*scale;
        var rt=new RenderTexture(width,height,24);var cpu=new Texture2D(width,height,TextureFormat.RGB24,false);
        try {
            camera.orthographic=true;camera.orthographicSize=Mathf.Max(bounds.extents.y,bounds.extents.x/(900f/1080f))*1.15f;
            camera.rect=new Rect(0,0,1,1);camera.aspect=900f/1080f;camera.nearClipPlane=.01f;camera.farClipPlane=20f;camera.ResetProjectionMatrix();
            camera.transform.position=bounds.center+new Vector3(0,0,2);camera.transform.LookAt(bounds.center);camera.clearFlags=CameraClearFlags.SolidColor;camera.backgroundColor=Color.gray;camera.targetTexture=rt;
            Action<string> capture=delegate(string label){camera.Render();RenderTexture.active=rt;cpu.ReadPixels(new Rect(0,0,width,height),0,0);cpu.Apply();File.WriteAllBytes(output+"."+label+".png",cpu.EncodeToPNG());};
            capture("source");
            if(Environment.GetEnvironmentVariable("OK_PROBE_RAW_MATERIAL_CAPTURE")=="1"){
                var display=camera.GetComponent<AmplifyColorBase>();
                if(display!=null){bool wasEnabled=display.enabled;try{
                    display.enabled=true;capture("native-display");
                    // Character.UpdateGlobalShader / ChaShader use different globals
                    // from Studio.SetAmbientShadow. Isolate that difference without
                    // changing the scene, card, materials or source configuration.
                    try {
                        ChaShader.ChangeAmbientShaodwColor(Manager.Config.GraphicData.shadowDepth);
                        capture("character-display");
                        File.WriteAllText(output+".character-globals.tsv","ambient\t"+Shader.GetGlobalVector("_ambientshadowG").ToString("R")+"\nshader_type\t"+Manager.Config.GraphicData.shaderType+"\n");
                        Shader.SetGlobalFloat("_KanoVerShift",0);capture("character-display-mode0");
                    } finally { Shader.SetGlobalVector("_ambientshadowG",ambient);Shader.SetGlobalFloat("_KanoVerShift",verticalShift); }
                }finally{display.enabled=wasEnabled;}}
            }
            if(scale==2){
                camera.transform.position=bounds.center-new Vector3(0,0,2);camera.transform.LookAt(bounds.center);capture("source-back");
                var skinned=renderer as SkinnedMeshRenderer;
                if(skinned!=null&&skinned.sharedMesh!=null&&skinned.sharedMesh.uv2.Length==0){
                    var sourceMesh=skinned.sharedMesh;var copy=UnityEngine.Object.Instantiate(sourceMesh);
                    try{skinned.sharedMesh=copy;copy.uv2=new Vector2[copy.vertexCount];capture("source-back-uv1-zero");copy.uv2=copy.uv;capture("source-back-uv1-uv0");}
                    finally{skinned.sharedMesh=sourceMesh;UnityEngine.Object.Destroy(copy);}
                }
                camera.transform.position=bounds.center+new Vector3(0,0,2);camera.transform.LookAt(bounds.center);
            }
            renderer.enabled=false;try{capture("hidden");}finally{renderer.enabled=true;}
            renderer.sharedMaterials=controls;
            foreach(string label in new[]{"main-white","color-regions","shadow-white","normal-flat","mask-null","ambient-alpha074","ambient-alpha026","ambient-alpha000","preview-ambient","shader-mode0","shader-mode1"}) {
                Shader.SetGlobalFloat("_KanoVerShift",label=="shader-mode0"?0:label=="shader-mode1"?1:verticalShift);
                Shader.SetGlobalVector("_ambientshadowG",label=="ambient-alpha074"?new Vector4(ambient.x,ambient.y,ambient.z,.74f):label=="ambient-alpha026"?new Vector4(ambient.x,ambient.y,ambient.z,.26f):label=="ambient-alpha000"?new Vector4(ambient.x,ambient.y,ambient.z,0):label=="preview-ambient"?new Vector4(.019f,.027f,.039f,ambient.w):ambient);
                for(int slot=0;slot<controls.Length;slot++) {var m=controls[slot];if(m==null)continue;m.CopyPropertiesFromMaterial(original[slot]);
                    if(label=="main-white"&&m.HasProperty("_MainTex"))m.SetTexture("_MainTex",Texture2D.whiteTexture);
                    if(label=="color-regions") {foreach(var key in new[]{"_ShadowColor","_SpecularColor"})if(m.HasProperty(key))m.SetColor(key,Color.white);if(m.HasProperty("_Color"))m.SetColor("_Color",Color.red);if(m.HasProperty("_Color2"))m.SetColor("_Color2",Color.green);if(m.HasProperty("_Color3"))m.SetColor("_Color3",Color.blue);}
                    if(label=="shadow-white"&&m.HasProperty("_ShadowColor"))m.SetColor("_ShadowColor",Color.white);
                    if(label=="normal-flat")foreach(var key in new[]{"_NormalMapScale","_DetailNormalMapScale"})if(m.HasProperty(key))m.SetFloat(key,0);
                    if(label=="mask-null"&&m.HasProperty("_ColorMask"))m.SetTexture("_ColorMask",null);
                }capture(label);
            }
            using(var writer=new StreamWriter(output+".controls.tsv")){writer.WriteLine("renderer\t"+PathOf(renderer.transform));writer.WriteLine("bounds\t"+bounds);writer.WriteLine("slots\t"+original.Length);writer.WriteLine("camera\t"+camera.transform.position+"\t"+camera.orthographicSize);writer.WriteLine("source_ambient\t"+ambient.ToString("R"));}
        } finally {Shader.SetGlobalFloat("_KanoVerShift",verticalShift);foreach(var effect in disabledCameraEffects)effect.enabled=true;Shader.SetGlobalVector("_ambientshadowG",ambient);renderer.sharedMaterials=original;camera.CopyFrom(backup);camera.transform.position=position;camera.transform.rotation=rotation;camera.targetTexture=target;RenderTexture.active=active;foreach(var m in controls)if(m!=null)UnityEngine.Object.Destroy(m);UnityEngine.Object.Destroy(cpu);UnityEngine.Object.Destroy(rt);UnityEngine.Object.Destroy(backupObject);}
    }
    private static void WriteBodyCreateSnapshot(ChaControl cha) {
        if(Environment.GetEnvironmentVariable("OK_PROBE_BODY_CREATE")!="1")return;
        using(var writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".body-overlays.tsv")) {
            foreach(var component in cha.GetComponents<MonoBehaviour>()) {
                var t=component.GetType();if(t.FullName!="KoiSkinOverlayX.KoiSkinOverlayController")continue;
                var method=t.GetMethod("GetOverlayTextures",BindingFlags.Instance|BindingFlags.NonPublic);var type=method.GetParameters()[0].ParameterType;
                foreach(int kind in new[]{1,3}) {int i=0;foreach(Texture texture in (System.Collections.IEnumerable)method.Invoke(component,new object[]{Enum.ToObject(type,kind)})) {WriteTexture(texture,".body-overlay"+kind+"."+i);writer.WriteLine(kind+"\t"+i+"\t"+texture.name+"\t"+texture.width+"\t"+texture.height);i++;}}
            }
        }
        cha.releaseCustomInputTexture=false;
        foreach(var name in new[]{"updateCMBodyTex","updateCMBodyColor","updateCMBodyLayout"}) {
            var property=typeof(ChaControl).GetProperty(name,BindingFlags.Instance|BindingFlags.NonPublic|BindingFlags.Public);
            var values=property==null?null:property.GetValue(cha,null) as bool[];
            if(values!=null)for(int i=0;i<values.Length;i++)values[i]=true;
        }
        cha.CreateBodyTexture();
        var create=cha.customTexCtrlBody;var flags=BindingFlags.Instance|BindingFlags.NonPublic;
        var material=(Material)typeof(CustomTextureCreate).GetField("matCreate",flags).GetValue(create);
        var main=(Texture)typeof(CustomTextureCreate).GetField("texMain",flags).GetValue(create);
        using(var writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".body-create.tsv")) {
            writer.WriteLine("shader\t"+material.shader.name);
            foreach(var p in new[]{"_Color","_Color2","_Color3","_Color4","_Color5","_Color6"})if(material.HasProperty(p))writer.WriteLine("color\t"+p+"\t"+material.GetColor(p).ToString("R"));
            foreach(var p in new[]{"_paint1","_paint2"})if(material.HasProperty(p))writer.WriteLine("vector\t"+p+"\t"+material.GetVector(p).ToString("R"));
            foreach(var p in new[]{"_MainTex","_ColorMask","_Texture3","_Texture4","_Texture6","_paintmask"}) {
                var texture=p=="_MainTex"?main:material.HasProperty(p)?material.GetTexture(p):null;
                WriteTexture(texture,".body-create."+p);WriteTextureSampling(texture,".body-create."+p,writer,p);
            }
            var output=new RenderTexture(create.baseW,create.baseH,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.sRGB);bool write=GL.sRGBWrite;
            try {GL.sRGBWrite=true;Graphics.Blit(main,output,material,0);WriteTexture(output,".body-create.pure");}
            finally {GL.sRGBWrite=write;UnityEngine.Object.Destroy(output);}
        }
    }
    private static void WriteEyeCreateSnapshot(ChaControl cha) {
        for(int i=0;i<2;i++)WriteTexture(cha.rendEye[i].sharedMaterial.GetTexture("_MainTex"),".eye"+i+".original");
        cha.releaseCustomInputTexture=false;cha.ChangeSettingEye(true,true,true);
        using(var writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".eye-create.tsv")) {
            writer.WriteLine("colorSpace\t"+QualitySettings.activeColorSpace);
            writer.WriteLine("renderTargetSRGB\t"+((RenderTexture)cha.rendEye[0].sharedMaterial.GetTexture("_MainTex")).sRGB);
            var controllers=new[]{cha.ctCreateEyeL,cha.ctCreateEyeR};
            for(int i=0;i<controllers.Length;i++) {
                var controller=controllers[i];var material=(Material)typeof(CustomTextureCreate).GetField("matCreate",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(controller);
                var main=(Texture)typeof(CustomTextureCreate).GetField("texMain",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(controller);
                writer.WriteLine("shader\t"+i+"\t"+material.shader.name);
                var space=typeof(Texture).GetProperty("activeTextureColorSpace",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic);
                writer.WriteLine("mainColorSpace\t"+i+"\t"+(space==null?"unknown":space.GetValue(main,null).ToString()));
                var source2D=main as Texture2D;
                if(source2D!=null && source2D.isReadable) {
                    var copy=new Texture2D(main.width,main.height,TextureFormat.RGBA32,false,true);
                    try {copy.SetPixels32(source2D.GetPixels32());copy.Apply();File.WriteAllBytes(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".eye"+i+".source.png",copy.EncodeToPNG());}
                    finally {UnityEngine.Object.Destroy(copy);}
                }
                var linearRT=new RenderTexture(main.width,main.height,0,RenderTextureFormat.ARGBFloat,RenderTextureReadWrite.Linear);
                var linearCPU=new Texture2D(main.width,main.height,TextureFormat.RGBAFloat,false,true);var priorRT=RenderTexture.active;bool priorSRGB=GL.sRGBWrite;
                try {GL.sRGBWrite=false;Graphics.Blit(main,linearRT);RenderTexture.active=linearRT;linearCPU.ReadPixels(new Rect(0,0,main.width,main.height),0,0);linearCPU.Apply();File.WriteAllBytes(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".eye"+i+".linear.rgba32f",linearCPU.GetRawTextureData());}
                finally {RenderTexture.active=priorRT;GL.sRGBWrite=priorSRGB;UnityEngine.Object.Destroy(linearCPU);UnityEngine.Object.Destroy(linearRT);}
                WriteTexture(main,".eye"+i+".input");WriteTexture(material.GetTexture("_ColorMask"),".eye"+i+".gradation");
                WriteTexture(cha.rendEye[i].sharedMaterial.GetTexture("_MainTex"),".eye"+i+".rebuilt");
                var target=new RenderTexture(512,512,0,RenderTextureFormat.ARGB32);bool previousWrite=GL.sRGBWrite;
                try{GL.sRGBWrite=true;Graphics.Blit(main,target,material,0);WriteTexture(target,".eye"+i+".pure");}
                finally{GL.sRGBWrite=previousWrite;UnityEngine.Object.Destroy(target);}
                foreach(var p in new[]{"_Color","_Color2","_Color3"})if(material.HasProperty(p))writer.WriteLine("color\t"+i+"\t"+p+"\t"+material.GetColor(p).ToString("R"));
                foreach(var p in new[]{"_Blend","_rotation2"})if(material.HasProperty(p))writer.WriteLine("float\t"+i+"\t"+p+"\t"+Num(material.GetFloat(p)));
                foreach(var p in new[]{"_grad","_uv2"})if(material.HasProperty(p))writer.WriteLine("vector\t"+i+"\t"+p+"\t"+material.GetVector(p).ToString("R"));
                foreach(var p in new[]{"_MainTex","_ColorMask","_Texture2"}) {
                    if(!material.HasProperty(p))continue;var t=p=="_MainTex"?main:material.GetTexture(p);
                    writer.WriteLine("texture\t"+i+"\t"+p+"\t"+(t==null?"null":t.name)+"\t"+(t==null?"":t.filterMode+"/"+t.wrapMode)+"\t"+material.GetTextureScale(p).ToString("R")+"\t"+material.GetTextureOffset(p).ToString("R"));
                    if(p=="_Texture2")WriteTexture(t,".eye"+i+".overlay");
                }
                if(i==0) {
                    var oracle=new Material(material);var input=new Texture2D(1,1,TextureFormat.RGBA32,false,true);var mask=new Texture2D(1,1,TextureFormat.RGBA32,false,true);var output=new RenderTexture(4,4,0,RenderTextureFormat.ARGB32);
                    bool oldWrite=GL.sRGBWrite;
                    try {
                        input.SetPixel(0,0,new Color(.75f,.5f,.25f,.8f));input.Apply();mask.SetPixel(0,0,new Color(.25f,0,0,1));mask.Apply();
                        oracle.SetColor("_Color",new Color(.5f,.25f,.75f,1));oracle.SetColor("_Color2",new Color(.1f,.9f,.4f,1));oracle.SetFloat("_Blend",.6f);oracle.SetVector("_grad",Vector4.zero);oracle.SetTexture("_ColorMask",mask);oracle.SetTexture("_Texture2",null);
                        GL.sRGBWrite=true;Graphics.Blit(input,output,oracle,0);WriteTexture(output,".eye-oracle");writer.WriteLine("oracleTargetSRGB\t"+output.sRGB);
                    } finally {GL.sRGBWrite=oldWrite;UnityEngine.Object.Destroy(output);UnityEngine.Object.Destroy(input);UnityEngine.Object.Destroy(mask);UnityEngine.Object.Destroy(oracle);}
                }
            }
        }
    }
    private static int faceCreateCall;
    private static void FaceCreatePostfix(CustomTextureCreate __instance,Texture __result) {
        var f=typeof(CustomTextureCreate).GetField("matCreate",BindingFlags.Instance|BindingFlags.NonPublic);var m=f.GetValue(__instance) as Material;
        if(m!=null&&m.shader.name=="Koikano/create_skinface"&&__result!=null)WriteTexture(__result,".face-create.call"+faceCreateCall+".postfix");
    }
    private static void CaptureFaceOverlays(ChaControl cha) {
        WriteTexture(cha.rendFace.sharedMaterial.mainTexture,".face-before-probe");
        using(var w=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".face-overlays.tsv")) {
            foreach(var component in cha.GetComponents<MonoBehaviour>()) {
                var t=component.GetType();if(t.FullName!="KoiSkinOverlayX.KoiSkinOverlayController")continue;
                var getData=t.GetMethod("GetExtendedData",Type.EmptyTypes);
                var data=getData==null?null:getData.Invoke(component,null);if(data!=null)w.WriteLine("version\t"+data.GetType().GetField("version").GetValue(data));
                var method=t.GetMethod("GetOverlayTextures",BindingFlags.Instance|BindingFlags.NonPublic);var type=method.GetParameters()[0].ParameterType;
                foreach(int kind in new[]{2,4}) {int i=0;foreach(Texture tex in (System.Collections.IEnumerable)method.Invoke(component,new object[]{Enum.ToObject(type,kind)})) {WriteTexture(tex,".face-overlay"+kind+"."+i);w.WriteLine(kind+"\t"+i+"\t"+tex.name+"\t"+tex.width+"\t"+tex.height);i++;}}
            }
        }
    }
    private static void FaceCreatePrefix(CustomTextureCreate __instance) {CaptureFaceCreate(__instance);}
    private static void CaptureFaceCreate(CustomTextureCreate create) {
        if(Environment.GetEnvironmentVariable("OK_PROBE_FACE_CREATE")!="1")return;
        var flags=BindingFlags.Instance|BindingFlags.NonPublic;
        if(create==null)return;
        var mat=(Material)typeof(CustomTextureCreate).GetField("matCreate",flags).GetValue(create);
        if(mat==null||mat.shader.name!="Koikano/create_skinface")return;
        var main=(Texture)typeof(CustomTextureCreate).GetField("texMain",flags).GetValue(create);
        if(main==null)return;
        string captureSuffix=".face-create.call"+(++faceCreateCall);
        using(var w=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+captureSuffix+".tsv")) {
            w.WriteLine("shader\t"+mat.shader.name);
            foreach(string p in new[]{"_Color","_Color2","_Color3","_Color4","_Color5","_Color6","_Color7"})if(mat.HasProperty(p))w.WriteLine("color\t"+p+"\t"+mat.GetColor(p).ToString("R"));
            foreach(string p in new[]{"_tex4uv","_tex5uv","_paint1","_paint2","_hokuro"})if(mat.HasProperty(p))w.WriteLine("vector\t"+p+"\t"+mat.GetVector(p).ToString("R"));
            foreach(string p in new[]{"_MainTex","_ColorMask","_Texture3","_Texture4","_Texture5","_Texture6","_Texture7","_paintmask"}) {
                var t=p=="_MainTex"?main:mat.HasProperty(p)?mat.GetTexture(p):null;if(t==null)continue;
                var suffix=captureSuffix+"."+p;WriteTexture(t,suffix);WriteTextureSampling(t,suffix,w,p);
                w.WriteLine("st\t"+p+"\t"+mat.GetTextureScale(p).ToString("R")+"\t"+mat.GetTextureOffset(p).ToString("R"));
            }
            var clone=new Material(mat);var rt=new RenderTexture(create.baseW,create.baseH,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.sRGB);rt.Create();bool write=GL.sRGBWrite;var active=RenderTexture.active;
            try {
                foreach(string label in new[]{"original","no-paint","layout"}) {
                    clone.CopyPropertiesFromMaterial(mat);
                    if(label=="no-paint")foreach(string p in new[]{"_Color3","_Color7"}) {var c=clone.GetColor(p);c.a=0;clone.SetColor(p,c);}
                    if(label=="layout") {clone.SetVector("_paint1",new Vector4(.11f,-.13f,.17f,-.35f));clone.SetVector("_paint2",new Vector4(-.09f,.08f,-.23f,-.6f));}
                    GL.sRGBWrite=true;Graphics.Blit(main,rt,clone,0);WriteTexture(rt,captureSuffix+"."+label);
                }
                // Independent create-pass positive controls. Keep these on the
                // clone: no renderer material or source card is modified.
                var pattern=new Texture2D(8,8,TextureFormat.RGBA32,false,false);
                try {
                    var pixels=new Color32[64];
                    for(int y=0;y<8;y++)for(int x=0;x<8;x++)pixels[y*8+x]=new Color32((byte)(x*31),(byte)(y*31),128,(byte)((x+y)*17));
                    pattern.SetPixels32(pixels);pattern.Apply();pattern.wrapMode=TextureWrapMode.Clamp;pattern.filterMode=FilterMode.Bilinear;
                    WriteTexture(pattern,captureSuffix+".pattern");
                    foreach(string label in new[]{"color-only","null-mask","null-main","cheek","lipline","paint1","paint2","mole","combined"}) {
                        clone.CopyPropertiesFromMaterial(mat);
                        foreach(string slot in new[]{"_Texture3","_Texture4","_Texture5","_Texture6","_Texture7"})clone.SetTexture(slot,null);
                        for(int n=3;n<=7;n++)clone.SetColor("_Color"+n,new Color(.31f,.63f,.84f,.67f));
                        if(label=="null-mask")clone.SetTexture("_ColorMask",null);
                        if(label=="cheek"||label=="combined")clone.SetTexture("_Texture4",pattern);
                        if(label=="lipline"||label=="combined")clone.SetTexture("_Texture5",pattern);
                        if(label=="paint1"||label=="combined")clone.SetTexture("_Texture3",pattern);
                        if(label=="paint2"||label=="combined")clone.SetTexture("_Texture7",pattern);
                        if(label=="mole"||label=="combined")clone.SetTexture("_Texture6",pattern);
                        GL.sRGBWrite=true;Graphics.Blit(label=="null-main"?null:main,rt,clone,0);WriteTexture(rt,captureSuffix+"."+label);
                    }
                    var authored=new Texture2D(128,128,TextureFormat.RGBA32,false,false);
                    var loaded=new Texture2D(2,2,TextureFormat.RGBA32,true,false);
                    try {
                        var dense=new Color32[128*128];
                        for(int y=0;y<128;y++)for(int x=0;x<128;x++)dense[y*128+x]=new Color32((byte)((x*71+y*19)%256),(byte)((y*83+x*7)%256),128,(byte)((x*29+y*37)%256));
                        authored.SetPixels32(dense);authored.Apply();ImageConversion.LoadImage(loaded,ImageConversion.EncodeToPNG(authored));
                        loaded.wrapMode=authored.wrapMode=TextureWrapMode.Clamp;loaded.filterMode=authored.filterMode=FilterMode.Bilinear;
                        WriteTexture(loaded,captureSuffix+".png-source");
                        for(int mip=0;mip<loaded.mipmapCount;mip++)WriteDirectMip(loaded,captureSuffix+".png-source",mip);
                        clone.CopyPropertiesFromMaterial(mat);
                        foreach(string slot in new[]{"_Texture3","_Texture4","_Texture5","_Texture6","_Texture7"})clone.SetTexture(slot,null);
                        clone.SetColor("_Color3",new Color(.31f,.63f,.84f,.67f));
                        clone.SetVector("_paint1",new Vector4(.11f,-.13f,.17f,-8f));
                        clone.SetTexture("_Texture3",loaded);GL.sRGBWrite=true;Graphics.Blit(main,rt,clone,0);WriteTexture(rt,captureSuffix+".mipped-paint");
                        clone.SetTexture("_Texture3",authored);Graphics.Blit(main,rt,clone,0);WriteTexture(rt,captureSuffix+".mipped-paint-l0");
                    } finally {UnityEngine.Object.Destroy(authored);UnityEngine.Object.Destroy(loaded);}
                } finally {UnityEngine.Object.Destroy(pattern);}
            } finally {RenderTexture.active=active;GL.sRGBWrite=write;UnityEngine.Object.Destroy(clone);UnityEngine.Object.Destroy(rt);}
        }
    }

    private static void ClothesCreatePostfix(CustomTextureCreate __instance) {
        string requested=Environment.GetEnvironmentVariable("OK_PROBE_CLOTHES_CREATE_NAMES");
        if(String.IsNullOrEmpty(requested))return;
        var flags=System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic;
        var create=__instance;
                var mat=(Material)typeof(CustomTextureCreate).GetField("matCreate",flags).GetValue(create);
                var main=(Texture)typeof(CustomTextureCreate).GetField("texMain",flags).GetValue(create);
                if(main==null||(requested!="ALL"&&Array.IndexOf(requested.Split(';'),main.name)<0))return;
                if(requested=="ALL"&&mat.shader.name!="Koikano/create_topN")return;
                string suffix=".clothes-create."+main.name;
                using(var writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+suffix+".tsv")) {
                    writer.WriteLine("shader\t"+mat.shader.name);
                    writer.WriteLine("quality\tanisotropicFiltering\t"+QualitySettings.anisotropicFiltering);
                    writer.WriteLine("quality\tactiveColorSpace\t"+QualitySettings.activeColorSpace);
                    foreach(string p in new[]{"_Color","_Color2","_Color3","_Color1_2","_Color2_2","_Color3_2"})if(mat.HasProperty(p))writer.WriteLine("color\t"+p+"\t"+mat.GetColor(p).ToString("R"));
                    foreach(string p in new[]{"_PatternScale1u","_PatternScale1v","_PatternOffset1u","_PatternOffset1v","_PatternRotator1","_PatternScale2u","_PatternScale2v","_PatternOffset2u","_PatternOffset2v","_PatternRotator2","_PatternScale3u","_PatternScale3v","_PatternOffset3u","_PatternOffset3v","_PatternRotator3"})if(mat.HasProperty(p))writer.WriteLine("float\t"+p+"\t"+mat.GetFloat(p).ToString("R",CultureInfo.InvariantCulture));
                    foreach(string p in new[]{"_MainTex","_ColorMask","_PatternMask1","_PatternMask2","_PatternMask3"}) {
                        var t=p=="_MainTex"?main:mat.HasProperty(p)?mat.GetTexture(p):null;if(t==null)continue;
                        WriteTexture(t,suffix+"."+p);WriteTextureSampling(t,suffix+"."+p,writer,p);
                        var readable=t as Texture2D;
                        if(readable!=null)writer.WriteLine("format\t"+p+"\t"+readable.format+"\t"+readable.isReadable);
                        if(readable!=null&&p.StartsWith("_PatternMask")&&readable.format==TextureFormat.RGBA32)
                            for(int mip=0;mip<readable.mipmapCount;mip++)WriteDirectMip(t,suffix+"."+p,mip);
                        if(readable!=null&&readable.isReadable) {
                            File.WriteAllBytes(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+suffix+"."+p+".raw",readable.GetRawTextureData());
                            writer.WriteLine("raw\t"+p+"\t"+readable.format+"\t"+readable.mipmapCount);
                        }
                        writer.WriteLine("texture\t"+p+"\t"+t.name+"\t"+t.width+"\t"+t.height+"\t"+t.filterMode+"\t"+t.wrapMode);
                        var target=new RenderTexture(create.baseW,create.baseH,0,RenderTextureFormat.ARGBFloat,RenderTextureReadWrite.Linear);
                        var cpu=new Texture2D(create.baseW,create.baseH,TextureFormat.RGBAFloat,false,true);
                        var previous=RenderTexture.active;bool previousWrite=GL.sRGBWrite;
                        try {GL.sRGBWrite=false;Graphics.Blit(t,target);RenderTexture.active=target;cpu.ReadPixels(new Rect(0,0,create.baseW,create.baseH),0,0);cpu.Apply();File.WriteAllBytes(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+suffix+"."+p+".rgba32f",cpu.GetRawTextureData());}
                        finally {RenderTexture.active=previous;GL.sRGBWrite=previousWrite;UnityEngine.Object.Destroy(cpu);UnityEngine.Object.Destroy(target);}
                    }
                }
                var clone=new Material(mat);
                var control=new RenderTexture(create.baseW,create.baseH,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.sRGB);
                control.Create();bool oldWrite=GL.sRGBWrite;
                try {
                    foreach(string label in new[]{"original","shift","alpha","combined"}) {
                        clone.CopyPropertiesFromMaterial(mat);
                        if(label=="shift"||label=="combined") {
                            clone.SetFloat("_PatternScale1u",.08f);clone.SetFloat("_PatternScale1v",.32f);
                            clone.SetFloat("_PatternOffset1u",.37f);clone.SetFloat("_PatternOffset1v",.68f);
                            clone.SetFloat("_PatternRotator1",.29f);
                        }
                        if(label=="alpha"||label=="combined") {var c=clone.GetColor("_Color1_2");c.a=.4f;clone.SetColor("_Color1_2",c);}
                        GL.sRGBWrite=true;Graphics.Blit(main,control,clone,0);WriteTexture(control,suffix+".control-"+label);
                    }
                    var originalPattern=mat.GetTexture("_PatternMask1") as Texture2D;
                    if(originalPattern!=null) {
                        var colorSpaceProperty=typeof(Texture2D).GetProperty("activeTextureColorSpace",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic);
                        bool linear=colorSpaceProperty!=null&&colorSpaceProperty.GetValue(originalPattern,null).ToString()=="Linear";
                        var copy=new Texture2D(originalPattern.width,originalPattern.height,originalPattern.format,originalPattern.mipmapCount>1,linear);
                        copy.Apply(false,false);Graphics.CopyTexture(originalPattern,copy);
                        WriteTexture(copy,suffix+".control-pattern-copy");var oldAniso=QualitySettings.anisotropicFiltering;
                        try {foreach(string label in new[]{"point0","point1","bilinear0","disable","enable"}) {
                            clone.CopyPropertiesFromMaterial(mat);clone.SetTexture("_PatternMask1",copy);
                            clone.SetFloat("_PatternScale1u",.08f);clone.SetFloat("_PatternScale1v",.32f);
                            clone.SetFloat("_PatternOffset1u",.37f);clone.SetFloat("_PatternOffset1v",.68f);clone.SetFloat("_PatternRotator1",.29f);
                            copy.filterMode=label.StartsWith("point")?FilterMode.Point:FilterMode.Bilinear;
                            copy.anisoLevel=label=="point0"||label=="bilinear0"?0:1;
                            QualitySettings.anisotropicFiltering=label=="disable"?AnisotropicFiltering.Disable:label=="enable"?AnisotropicFiltering.Enable:oldAniso;
                            GL.sRGBWrite=true;Graphics.Blit(main,control,clone,0);WriteTexture(control,suffix+".control-"+label);
                        }}finally {QualitySettings.anisotropicFiltering=oldAniso;UnityEngine.Object.Destroy(copy);}
                    }
                } finally {GL.sRGBWrite=oldWrite;UnityEngine.Object.Destroy(clone);UnityEngine.Object.Destroy(control);}
    }

    private static void WriteSceneLightSnapshot() {
        string output=Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".lights.tsv";
        using(var writer=new StreamWriter(output)) {
            writer.WriteLine("kind\tname\tenabled\tactive\ttype\tforward\tposition\trotation\tintensity\tcolor\tcullingMask");
            foreach(var light in UnityEngine.Object.FindObjectsOfType<Light>()) {
                var t=light.transform;
                writer.WriteLine("light\t"+light.name+"\t"+light.enabled+"\t"+light.gameObject.activeInHierarchy+"\t"+light.type+"\t"+
                    t.forward.ToString("R")+"\t"+t.position.ToString("R")+"\t"+t.rotation.ToString("R")+"\t"+
                    light.intensity.ToString("R",CultureInfo.InvariantCulture)+"\t"+light.color.ToString("R")+"\t"+light.cullingMask);
            }
            var camera=Camera.main;
            if(camera!=null) {var t=camera.transform;writer.WriteLine("camera\t"+camera.name+"\t"+camera.enabled+"\t"+
                camera.gameObject.activeInHierarchy+"\tCamera\t"+t.forward.ToString("R")+"\t"+t.position.ToString("R")+"\t"+
                t.rotation.ToString("R")+"\t0\t(0,0,0,0)\t"+camera.cullingMask);}
        }
        var displayCamera=Camera.main;
        var displayEffect=displayCamera==null?null:displayCamera.GetComponent<AmplifyColorBase>();
        if(displayEffect!=null){
            using(var writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".display.tsv")){
                writer.WriteLine("enabled\t"+displayEffect.enabled);writer.WriteLine("linear\t"+QualitySettings.activeColorSpace);
                writer.WriteLine("hdr\t"+displayCamera.allowHDR);writer.WriteLine("tonemapper\t"+displayEffect.Tonemapper);
                writer.WriteLine("exposure\t"+displayEffect.Exposure);writer.WriteLine("blend\t"+displayEffect.BlendAmount);
                WriteTextureSampling(displayEffect.LutTexture,".display-lut",writer,"LutTexture");
            }
            WriteTexture(displayEffect.LutTexture,".display-lut");
        }
        // These are the actual shader inputs, not values inferred from the
        // scene light transform or the config defaults. Keep their capture in
        // the same frame as the renderer snapshot.
        using(var writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".shader-globals.tsv")) {
            writer.WriteLine("property\tvalue");
            foreach(var name in new[]{"_ambientshadowG","_LightColor0","_WorldSpaceLightPos0","_LineColorG","unity_SHAr","unity_SHAg","unity_SHAb"})
                writer.WriteLine(name+"\t"+Shader.GetGlobalVector(name).ToString("R"));
            foreach(var name in new[]{"_KanoVerShift","_linewidthG"})writer.WriteLine(name+"\t"+Shader.GetGlobalFloat(name).ToString("R",CultureInfo.InvariantCulture));
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_RAMP")=="1") {
            var ramp=Shader.GetGlobalTexture("_RampG");
            if(ramp==null)throw new InvalidDataException("Native _RampG global texture absent");
            using(var writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".ramp.tsv"))
                WriteTextureSampling(ramp,".ramp",writer,"_RampG");
            WriteTexture(ramp,".ramp");
        }
    }

    private static void WriteBodyMorphControls(ChaControl cha) {
        string path=Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".body-morph.tsv";
        using(var output=new StreamWriter(path)) {
            output.WriteLine("renderer\tmorph\tweight\tmax_displacement\tlinear_error\trestored_error");
            foreach(var source in cha.GetComponentsInChildren<SkinnedMeshRenderer>(true)) {
                if(source.name!="o_body_a" || source.sharedMesh==null)continue;
                var mesh=source.sharedMesh;
                for(int channel=0;channel<mesh.blendShapeCount;channel++) {
                    var go=new GameObject("OpenKoikatsu numerical morph control");
                    try {
                        go.transform.SetParent(source.transform.parent,false);
                        go.transform.localPosition=source.transform.localPosition;
                        go.transform.localRotation=source.transform.localRotation;
                        go.transform.localScale=source.transform.localScale;
                        var clone=go.AddComponent<SkinnedMeshRenderer>();clone.enabled=false;
                        clone.sharedMesh=mesh;clone.bones=source.bones;clone.rootBone=source.rootBone;clone.quality=source.quality;
                        for(int index=0;index<mesh.blendShapeCount;index++)clone.SetBlendShapeWeight(index,source.GetBlendShapeWeight(index));
                        Vector3[][] samples=new Vector3[6][];float[] weights={0,25,50,75,100,0};
                        for(int step=0;step<weights.Length;step++) {
                            clone.SetBlendShapeWeight(channel,weights[step]);var baked=new Mesh();
                            try{clone.BakeMesh(baked);samples[step]=baked.vertices;}finally{UnityEngine.Object.Destroy(baked);}
                        }
                        for(int step=0;step<weights.Length;step++) {
                            float maximum=0,error=0,restored=0;
                            for(int vertex=0;vertex<samples[step].Length;vertex++) {
                                Vector3 delta=samples[step][vertex]-samples[0][vertex];
                                maximum=Mathf.Max(maximum,delta.magnitude);
                                error=Mathf.Max(error,(delta-(samples[4][vertex]-samples[0][vertex])*(weights[step]/100f)).magnitude);
                                restored=Mathf.Max(restored,(samples[5][vertex]-samples[0][vertex]).magnitude);
                            }
                            output.WriteLine(source.name+"\t"+mesh.GetBlendShapeName(channel)+"\t"+weights[step]+"\t"+maximum.ToString("R",CultureInfo.InvariantCulture)+"\t"+error.ToString("R",CultureInfo.InvariantCulture)+"\t"+restored.ToString("R",CultureInfo.InvariantCulture));
                        }
                    }finally{UnityEngine.Object.Destroy(go);}
                }
            }
        }
    }
    private static void WriteRendererSnapshot(ChaControl cha) {
        using(var w=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".clothes-load.tsv")) {
            var clothes=cha.nowCoordinate.clothes;
            for(int kind=0;kind<clothes.parts.Length;kind++) {
                w.WriteLine("part\t"+kind+"\t"+clothes.parts[kind].id);
                for(int c=0;c<clothes.parts[kind].colorInfo.Length;c++)w.WriteLine("baseColor\t"+kind+"\t"+c+"\t"+clothes.parts[kind].colorInfo[c].baseColor.ToString("R"));
            }
            for(int kind=0;kind<clothes.subPartsId.Length;kind++) {
                int category=(cha.infoClothes[0].Kind==1?200:210)+kind;
                int id=clothes.subPartsId[kind];
                var entry=cha.lstCtrl.GetInfo((ChaListDefine.CategoryNo)category,id);
                w.WriteLine("subpart\t"+category+"\t"+id);
                if(entry!=null)foreach(var key in new[]{ChaListDefine.KeyType.MainAB,ChaListDefine.KeyType.MainData,ChaListDefine.KeyType.MainTexAB,ChaListDefine.KeyType.MainTex,ChaListDefine.KeyType.ColorMaskAB,ChaListDefine.KeyType.ColorMaskTex})w.WriteLine("entry\t"+category+"\t"+key+"\t"+entry.GetInfo(key));
            }
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_TEXTURE_MIPS")=="1") {
            using(var controlWriter=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".mip-controls.tsv")) {
                foreach(var size in new[]{new[]{5,3},new[]{1,7},new[]{7,1},new[]{4,4},new[]{8,4}}) {
                    int w=size[0],h=size[1];var src=new Texture2D(w,h,TextureFormat.RGBA32,false,false);
                    var pixels=new Color32[w*h];for(int p=0;p<pixels.Length;p++)pixels[p]=new Color32((byte)((p*83)%256),(byte)((p*37)%256),(byte)((p*149)%256),(byte)((p*61)%256));
                    src.SetPixels32(pixels);src.Apply();src.filterMode=FilterMode.Point;
                    var rt=new RenderTexture(w,h,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.sRGB);rt.useMipMap=true;rt.autoGenerateMips=true;rt.Create();
                    bool oldWrite=GL.sRGBWrite;
                    try {GL.sRGBWrite=true;Graphics.Blit(src,rt);rt.GenerateMips();
                        controlWriter.WriteLine("descriptor\t"+w+"x"+h+"\tmipCount="+rt.descriptor.mipCount+"\tuseMipMap="+rt.useMipMap);
                        WriteTextureSampling(rt,".mip-control."+w+"x"+h,controlWriter,"_MainTex",true);}
                    finally {GL.sRGBWrite=oldWrite;UnityEngine.Object.Destroy(src);UnityEngine.Object.Destroy(rt);}
                }
            }
        }
        var rows=new List<RendererSnapshot>();
        using(var writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".materials.tsv")) {
            foreach(var r in cha.GetComponentsInChildren<Renderer>(true)) {
                var skin=r as SkinnedMeshRenderer;var filter=r.GetComponent<MeshFilter>();
                var mesh=skin!=null?skin.sharedMesh:filter!=null?filter.sharedMesh:null;
                var row=new RendererSnapshot{path=PathOf(r.transform),type=r.GetType().Name,mesh=mesh==null?"":mesh.name,
                    active=r.gameObject.activeInHierarchy,activeSelf=r.gameObject.activeSelf,shadowCasting=r.shadowCastingMode.ToString(),enabled=r.enabled,min=r.bounds.min,max=r.bounds.max,vertexCount=mesh==null?0:mesh.vertexCount,
                    rootBone=skin==null||skin.rootBone==null?"":PathOf(skin.rootBone),bones=skin==null?new string[0]:Array.ConvertAll(skin.bones,delegate(Transform t){return t==null?"null":PathOf(t);})};
                var inactive=new List<string>();for(var tf=r.transform;tf!=null;tf=tf.parent)if(!tf.gameObject.activeSelf)inactive.Add(PathOf(tf));row.inactiveAncestors=inactive.ToArray();
                var components=new List<string>();
                for(var tf=r.transform;tf!=null&&tf!=cha.transform;tf=tf.parent)
                    foreach(var component in tf.GetComponents<MonoBehaviour>()) if(component!=null)components.Add(tf.name+":"+component.GetType().FullName);
                row.components=components.ToArray();
                var requested=Environment.GetEnvironmentVariable("OK_PROBE_RENDERER_MESHES");
                if(mesh!=null&&!String.IsNullOrEmpty(requested)&&Array.IndexOf(requested.Split(';'),mesh.name)>=0) {
                    Mesh baked=mesh;if(skin!=null){baked=new Mesh();skin.BakeMesh(baked);}
                    var world=baked.vertices;for(int v=0;v<world.Length;v++)world[v]=r.transform.TransformPoint(world[v]);
                    var palette=new List<Bone>();if(skin!=null)foreach(var bone in skin.bones)if(bone!=null)palette.Add(PVTransform(bone));
                    var matrices=skin==null?new Matrix4x4[0]:Array.ConvertAll(skin.bones,delegate(Transform bone){return bone==null?Matrix4x4.identity:bone.localToWorldMatrix;});
                    var shaderWorld=mesh.vertices;var weights=mesh.boneWeights;var binds=mesh.bindposes;
                    if(skin!=null) for(int v=0;v<shaderWorld.Length;v++) {
                        var w=weights[v];var position=shaderWorld[v];var p=Vector3.zero;var indices=new[]{w.boneIndex0,w.boneIndex1,w.boneIndex2,w.boneIndex3};var amounts=new[]{w.weight0,w.weight1,w.weight2,w.weight3};
                        for(int k=0;k<4;k++)if(amounts[k]>0)p+=(matrices[indices[k]]*binds[indices[k]]).MultiplyPoint3x4(position)*amounts[k];shaderWorld[v]=p;
                    }
                    var dump=new RendererMeshSnapshot{path=row.path,name=mesh.name,vertices=mesh.vertices,worldVertices=world,shaderWorldVertices=shaderWorld,rendererWorld=r.transform.localToWorldMatrix,quality=skin==null?"rigid":skin.quality.ToString(),bindposes=binds,weights=weights,bones=palette.ToArray(),boneWorlds=matrices};
                    dump.normals=mesh.normals;dump.tangents=mesh.tangents;
                    dump.colors=mesh.colors;dump.colors32=mesh.colors32;
                    dump.uv0=mesh.uv;dump.uv1=mesh.uv2;dump.uv2=mesh.uv3;dump.uv3=mesh.uv4;
                    File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".mesh"+rows.Count+".json",ProbeJson(dump));
                    if(Environment.GetEnvironmentVariable("OK_PROBE_RENDERER_CONTROLS")=="1"&&r.enabled&&r.gameObject.activeInHierarchy)
                        CaptureRendererMaterialControls(r,skin==null?world:shaderWorld,rows.Count);
                    if(skin!=null)UnityEngine.Object.Destroy(baked);
                }
                var mats=new List<string>();var shaders=new List<string>();var queues=new List<int>();
                foreach(var mat in r.sharedMaterials) {
                    if(mat==null) {mats.Add("null");shaders.Add("");queues.Add(0);continue;}
                    mats.Add(mat.name);shaders.Add(mat.shader.name);queues.Add(mat.renderQueue);
                    writer.WriteLine("material\t"+row.path+"\t"+mat.name+"\t"+mat.shader.name+"\t"+mat.renderQueue);
                    foreach(var p in new[]{"_MainTex","_ColorMask","_DetailMask","_NormalMap","_NormalMapDetail","_AnotherRamp","_HairGloss","_EmissionMask","_ReflectionMapCap","_ReflectMapDetail","_DetailMainTex","_DetailNormalMap","_overtex1","_overtex2","_overtex3","_overtex4","_AlphaMask","_alphamask","_LineMask"}) {
                        if(!mat.HasProperty(p))continue;var texture=mat.GetTexture(p);var scale=mat.GetTextureScale(p);var offset=mat.GetTextureOffset(p);
                        writer.WriteLine("texture\t"+row.path+"\t"+mat.name+"\t"+p+"\t"+(texture==null?"null":texture.name+":"+texture.width+":"+texture.height)+"\t"+scale.ToString("R")+"\t"+offset.ToString("R"));
                        string textureSuffix=".renderer"+rows.Count+".mat"+(mats.Count-1)+"."+p;
                        WriteTextureSampling(texture,textureSuffix,writer,p);
                        if(mesh!=null&&!String.IsNullOrEmpty(requested)&&Array.IndexOf(requested.Split(';'),mesh.name)>=0) {
                            WriteTexture(texture,textureSuffix);
                            if(Environment.GetEnvironmentVariable("OK_PROBE_TEXTURE_COVERAGE")=="1"&&texture!=null&&p=="_MainTex")
                                WriteRendererUVSamples(mesh,mat,p,textureSuffix+".coverage",true);
                            if(Environment.GetEnvironmentVariable("OK_PROBE_RENDERER_CONTROLS")=="1"&&texture!=null&&(p=="_ColorMask"||p=="_DetailMask"))
                                WriteRendererUVSamples(mesh,mat,p,textureSuffix);
                            if(Environment.GetEnvironmentVariable("OK_PROBE_TEXTURE_MIPS")=="1") {
                                var sampleWriter=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+textureSuffix+".sampling.tsv");
                                try{WriteTextureSampling(texture,textureSuffix,sampleWriter,p,true);}finally{sampleWriter.Dispose();}
                            }
                        }
                    }
                    foreach(var p in new[]{"_Color","_Color2","_Color3","_Color4","_ShadowColor","_LineColor","_overcolor1","_overcolor2"})
                        if(mat.HasProperty(p))writer.WriteLine("color\t"+row.path+"\t"+mat.name+"\t"+p+"\t"+mat.GetColor(p).ToString("R"));
                    foreach(var p in new[]{"_Cutoff","_Cull","_ZWrite","_ZTest","_alpha_a","_alpha_b","_LineWidthS","_linewidthG","_ShadowExtend","_ShadowExtendAnother","_SpecularPower","_NormalScale","_BumpScale"})
                        if(mat.HasProperty(p))writer.WriteLine("float\t"+row.path+"\t"+mat.name+"\t"+p+"\t"+mat.GetFloat(p).ToString("R",CultureInfo.InvariantCulture));
                    if(Environment.GetEnvironmentVariable("OK_PROBE_MATERIAL_TEXTURE_CONTROLS")=="1"&&mesh!=null&&r.enabled&&r.gameObject.activeInHierarchy
                        &&mat.shader!=null&&mat.shader.name=="xukmi/MainItemPlus"&&!String.IsNullOrEmpty(requested)&&Array.IndexOf(requested.Split(';'),mesh.name)>=0)
                        WriteMaterialTextureControls(r,mesh,rows.Count,mats.Count-1);
                }
                row.materials=mats.ToArray();row.shaders=shaders.ToArray();row.queues=queues.ToArray();rows.Add(row);
            }
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".renderers.json",ProbeJson(rows.ToArray()));
        if(Environment.GetEnvironmentVariable("OK_PROBE_STATE_ONLY")=="1")return;
        WriteTransforms(cha,".transforms.tsv");
        WriteBodyCreateSnapshot(cha);
        WriteEyeCreateSnapshot(cha);
        if(Environment.GetEnvironmentVariable("OK_PROBE_RENDERER_FACE")=="1") {
            var output=Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT");
            CaptureMouthFace(cha,output+".face.png");
            if(Environment.GetEnvironmentVariable("OK_PROBE_RENDERER_FULL_BODY")=="1")CaptureMouthFace(cha,output+".body.png",true);
            var colorEffect=Camera.main.GetComponent<AmplifyColorEffect>();
            if(colorEffect!=null) {
                using(var map=new StreamWriter(output+".face-color.tsv")) {
                    map.WriteLine("enabled\t"+colorEffect.enabled);
                    map.WriteLine("activeColorSpace\t"+QualitySettings.activeColorSpace);
                    map.WriteLine("lut\t"+(colorEffect.LutTexture==null?"null":colorEffect.LutTexture.name));
                    map.WriteLine("blendLut\t"+(colorEffect.LutBlendTexture==null?"null":colorEffect.LutBlendTexture.name));
                    map.WriteLine("blendAmount\t"+colorEffect.BlendAmount.ToString("R",CultureInfo.InvariantCulture));
                    map.WriteLine("exposure\t"+colorEffect.Exposure.ToString("R",CultureInfo.InvariantCulture));
                    map.WriteLine("quality\t"+colorEffect.QualityLevel);
                    map.WriteLine("tonemapper\t"+colorEffect.Tonemapper);
                }
                if(colorEffect.LutTexture!=null)WriteTexture(colorEffect.LutTexture,".face-lut");
                if(colorEffect.LutBlendTexture!=null)WriteTexture(colorEffect.LutBlendTexture,".face-blend-lut");
            }
            var rimMeshes=Environment.GetEnvironmentVariable("OK_PROBE_HAIR_RIM_MESHES");
            if(!String.IsNullOrEmpty(rimMeshes)) {
                var names=rimMeshes.Split(';');
                using(var map=new StreamWriter(output+".face-rim.tsv"))
                foreach(var renderer in cha.transform.GetComponentsInChildren<SkinnedMeshRenderer>(true)) {
                    if(renderer.sharedMesh==null||Array.IndexOf(names,renderer.sharedMesh.name)<0)continue;
                    foreach(var material in renderer.sharedMaterials) {
                        if(material==null||!material.HasProperty("_rimV"))continue;
                        float original=material.GetFloat("_rimV");
                        try {
                            material.SetFloat("_rimV",0f);
                            CaptureMouthFace(cha,output+".face-rim-zero-"+renderer.sharedMesh.name+".png");
                        } finally {material.SetFloat("_rimV",original);}
                        map.WriteLine(renderer.sharedMesh.name+"\t"+original.ToString("R",CultureInfo.InvariantCulture));
                    }
                }
            }
            if(Environment.GetEnvironmentVariable("OK_PROBE_RENDERER_EFFECTS")=="1") {
                using(var map=new StreamWriter(output+".face-effects.tsv"))
                foreach(var effect in Camera.main.GetComponents<Behaviour>()) {
                    if(effect==null||!effect.enabled)continue;
                    var name=effect.GetType().Name;
                    if(name!="AmplifyColorEffect"&&name!="AmplifyOcclusionEffect")continue;
                    try {
                        effect.enabled=false;
                        CaptureMouthFace(cha,output+".face-without-"+name+".png");
                    } finally {effect.enabled=true;}
                    map.WriteLine(name);
                }
            }
            if(Environment.GetEnvironmentVariable("OK_PROBE_RENDERER_ISOLATIONS")=="1") {
                var selected=Environment.GetEnvironmentVariable("OK_PROBE_ISOLATION_MESHES")
                    ??Environment.GetEnvironmentVariable("OK_PROBE_RENDERER_MESHES")??"";
                var names=selected.Split(';');
                var renderers=cha.transform.GetComponentsInChildren<Renderer>(true);
                using(var map=new StreamWriter(output+".face-isolations.tsv"))
                for(int i=0;i<renderers.Length;i++) {
                    var skin=renderers[i] as SkinnedMeshRenderer;
                    var filter=renderers[i].GetComponent<MeshFilter>();
                    var mesh=skin!=null?skin.sharedMesh:filter!=null?filter.sharedMesh:null;
                    if(mesh==null||Array.IndexOf(names,mesh.name)<0||!renderers[i].enabled)continue;
                    bool enabled=renderers[i].enabled;
                    try {
                        renderers[i].enabled=false;
                        CaptureMouthFace(cha,output+".face-minus-renderer"+i+".png");
                    } finally {renderers[i].enabled=enabled;}
                    map.WriteLine(i+"\t"+PathOf(renderers[i].transform)+"\t"+mesh.name);
                }
            }
        }
    }
    private IEnumerator Run() {
        yield return new WaitForSecondsRealtime(10f);
        if (Environment.GetEnvironmentVariable("OK_PROBE_GENERIC_COLLIDER") == "1") {
            try { RunGenericColliderControl(); Save(new Dump { clip = "Generic native collider numerical controls" }); }
            catch (Exception ex) { Save(new Dump { error = ex.ToString() }); }
            finished = true; Application.Quit(); yield break;
        }
        if (Environment.GetEnvironmentVariable("OK_PROBE_GENERIC_DYNAMIC_SCALE") == "1") {
            try { RunGenericDynamicScaleControl(); Save(new Dump { clip = "Generic numerical DynamicBone scale controls" }); }
            catch (Exception ex) { Save(new Dump { error = ex.ToString() }); }
            finished = true; Application.Quit(); yield break;
        }
        if (Environment.GetEnvironmentVariable("OK_PROBE_GENERIC_IK_SCALE") == "1") {
            try { RunGenericIkScaleControl(); Save(new Dump { clip = "Generic numerical IK scale controls" }); }
            catch (Exception ex) { Save(new Dump { error = ex.ToString() }); }
            finished = true; Application.Quit(); yield break;
        }
        string card = Environment.GetEnvironmentVariable("OK_PROBE_CARD");
        if (String.IsNullOrEmpty(card)) card = @"C:\portable\KoikatuSunshine\UserData\chara\female\KK_091207.png";
        ChaControl cha;
        try {
            var sourceCard=new ChaFileControl();
            if(!sourceCard.LoadCharaFile(card,byte.MaxValue,noLoadPng:true))throw new Exception("Cannot read source character card: "+card);
            bool male=sourceCard.parameter.sex==0;
            if (Environment.GetEnvironmentVariable("OK_PROBE_FACTORY") == "plain") {
                cha = male ? Manager.Character.CreateMale(sourceCard.parameter.exType,Manager.Scene.commonSpace,-1) : Manager.Character.CreateFemale(Manager.Scene.commonSpace, -1);
                cha.chaFile.LoadCharaFile(card, byte.MaxValue, noLoadPng: true);
                cha.Load(reflectStatus: true);
            } else { cha = male ? Studio.AddObjectMale.Add(card).charInfo : Studio.AddObjectFemale.Add(card).charInfo; }
        }
        catch (Exception ex) { finished = true; Save(new Dump { error = ex.ToString() }); Application.Quit(); yield break; }
        if (Environment.GetEnvironmentVariable("OK_PROBE_EXPRESSION") == "0" && cha.expression != null)
            cha.expression.enabled = false;
        if(Environment.GetEnvironmentVariable("OK_PROBE_MATERIALS")=="1") WriteHeadMaterials(cha,".head-materials-initial.tsv");
        yield return new WaitForSecondsRealtime(8f);
        if(Environment.GetEnvironmentVariable("OK_PROBE_RENDERERS")=="1") {
            string outfitText=Environment.GetEnvironmentVariable("OK_PROBE_OUTFIT");
            if(!String.IsNullOrEmpty(outfitText)) {
                try {
                    int outfit=Int32.Parse(outfitText,CultureInfo.InvariantCulture);
                    if(outfit<0||outfit>=cha.chaFile.coordinate.Length)throw new Exception("Unavailable outfit "+outfit);
                    if(cha.fileStatus.coordinateType!=outfit && !cha.ChangeCoordinateTypeAndReload((ChaFileDefine.CoordinateType)outfit))throw new Exception("Outfit reload rejected");
                }catch(Exception ex){finished=true;Save(new Dump{error=ex.ToString()});Application.Quit();yield break;}
                float deadline=Time.realtimeSinceStartup+60;
                while(!cha.loadEnd && Time.realtimeSinceStartup<deadline)yield return null;
                if(!cha.loadEnd){finished=true;Save(new Dump{error="Outfit reload timeout"});Application.Quit();yield break;}
                // Let native post-reload plugin coroutines and material updates finish.
                yield return new WaitForSecondsRealtime(8f);
                using(var writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".state.tsv")) {
                    writer.WriteLine("coordinate\t"+cha.fileStatus.coordinateType);
                    writer.WriteLine("loadEnd\t"+cha.loadEnd);
                    for(int i=0;i<cha.fileStatus.clothesState.Length;i++)writer.WriteLine("clothesState\t"+i+"\t"+cha.fileStatus.clothesState[i]);
                }
            }
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_H_PAIR")=="1") {
            if(Environment.GetEnvironmentVariable("OK_PROBE_H_AUTO_INIT")=="1")yield return RunNativeHPairAutomatic(cha);
            else try{RunNativeHPair(cha);Save(new Dump{error="",clip="Native MotionIK pair contract"});}catch(Exception ex){Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_PARALLEL_FADE")=="1") {
            try {RunParallelFadeControl(cha);Save(new Dump{error="",clip="Native parallel independent layer CrossFade"});}
            catch(Exception ex){Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_LAYER_FADE")=="1") {
            try {RunLayerFadeControl(cha);Save(new Dump{error="",clip="Native independent layer CrossFade"});}
            catch(Exception ex){Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_CROSSFADE")=="1") {
            try {RunCrossFadeControl(cha);Save(new Dump{error="",clip="Native Animator.CrossFade base states"});}
            catch(Exception ex){Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_TRIGGER_RESTART")=="1") {
            try {RunTriggerRestartControl(cha);Save(new Dump{error="",clip="Native source trigger self transitions"});}
            catch(Exception ex) {Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_LAYER_CLOCK")=="1") {
            try {RunLayerClockControl(cha);Save(new Dump{error="",clip="Native scheduled independent layer clocks"});}
            catch(Exception ex) {Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_MULTILAYER")=="1") {
            try {RunMultiLayerControl(cha);Save(new Dump{error="",clip="Native multi-layer Animator control"});}
            catch(Exception ex) {Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_ADDITIVE")=="1") {
            try {RunAdditiveControl(cha);Save(new Dump{error="",clip="Native additive layer control"});}
            catch(Exception ex) {Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_MOTION_TIME_SCALE")=="1") {
            try {RunMotionTimeScaleControl(cha);Save(new Dump{error="",clip="Authored motion duration Animator control"});}
            catch(Exception ex){Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_PV")=="1") {
            try {RunPVCopyControl(cha);Save(new Dump{error="",clip="Actual compiled Studio.PVCopy callback"});}
            catch(Exception ex){Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_YURE")=="1") {
            try {RunYureControl(cha);Save(new Dump{error="",controller=cha.animBody.runtimeAnimatorController.name,clip="YureCtrl.LoadExcel/Proc and Ver02 enable transitions"});}
            catch(Exception ex){Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_STUDIO")=="1") {
            try {RunStudioControl(cha);if(Environment.GetEnvironmentVariable("OK_PROBE_STUDIO_SAMPLE")=="1") RunStudioSampler(cha);
                Save(new Dump{error="",controller=cha.animBody.runtimeAnimatorController.name,clip="OCIChar.LoadAnime"});}
            catch(Exception ex){Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_ANIMATOR_CLOCK")=="1") {
            try {RunAnimatorClockControl(cha);Save(new Dump{error="",controller=cha.animBody.runtimeAnimatorController.name,clip="parameterized state clock"});}
            catch(Exception ex){Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_ANIMATOR_2D")=="1") {
            try {RunAnimatorCartesianControl(cha);Save(new Dump{error="",controller="khs3_f_base3",clip="Cartesian override layer control"});}
            catch(Exception ex){Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_ANIMATOR_BLEND")=="1") {
            try {RunAnimatorBlendControl(cha);Save(new Dump{error="",controller="base",clip="1D controller blend control"});}
            catch(Exception ex){Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_EYE_LOOK")=="1") {
            IEnumerator control=RunEyeLook(cha);string error="";
            while(true) {bool next=false;try {next=control.MoveNext();}catch(Exception ex){error=ex.ToString();}if(!next)break;yield return control.Current;}
            Save(new Dump{error=error,controller="KKS EyeLookCalc",clip="all eye look states"});finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_EYE_SHADER")=="1") {
            IEnumerator control=RunEyeShader(cha);string error="";
            while(true) {bool next=false;try {next=control.MoveNext();}catch(Exception ex){error=ex.ToString();}if(!next)break;yield return control.Current;}
            Save(new Dump{error=error,controller="KKS main_eye",clip="three texture transforms"});finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_EYE_GAZE")=="1") {
            IEnumerator control=RunEyeGaze(cha);string error="";
            while(true) {bool next=false;try {next=control.MoveNext();}catch(Exception ex){error=ex.ToString();}
                if(!next) break;yield return control.Current;}
            Save(new Dump{error=error,controller="KKS FaceBlendShape/EyeLookMaterialControll",clip="normalized gaze rates"});
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_FACE_TIME")=="1") {
            IEnumerator control=RunFaceTime(cha);string controlError="";
            while(true) {
                bool next=false;
                try {next=control.MoveNext();} catch(Exception ex) {controlError=ex.ToString();}
                if(!next) break;
                yield return control.Current;
            }
            Save(new Dump{error=controlError,controller="KKS FBSBase/TimeProgressCtrl/FBSBlinkControl",clip="timed FBS and blink"});
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_FACE_CONTROLS")=="1") {
            IEnumerator control=RunEyeBrowControl(cha);string controlError="";
            while(true) {
                bool next=false;
                try {next=control.MoveNext();} catch(Exception ex) {controlError=ex.ToString();}
                if(!next) break;
                yield return control.Current;
            }
            Save(new Dump{error=controlError,controller="KKS FBSBase",clip="all Eyes/Eyebrow patterns"});
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_RENDERERS")=="1") {
            var clothesState=Environment.GetEnvironmentVariable("OK_PROBE_CLOTHES_STATE");
            if(!String.IsNullOrEmpty(clothesState)){
                byte state;
                if(!Byte.TryParse(clothesState,out state)||state>3){Save(new Dump{error="Invalid material capture clothes state"});finished=true;Application.Quit();yield break;}
                cha.SetClothesStateAll(state);
                // ChaControl.UpdateVisible applies fileStatus on its next update.
                yield return null;yield return new WaitForEndOfFrame();
            }
            try {
                using(var writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".ramps.tsv")) {
                    writer.WriteLine("selected\t"+Manager.Config.GraphicData.rampId);
                    foreach(var entry in cha.lstCtrl.GetCategoryInfo(ChaListDefine.CategoryNo.mt_ramp)) {
                        string bundle,asset;cha.lstCtrl.GetFilePath(ChaListDefine.CategoryNo.mt_ramp,entry.Key,ChaListDefine.KeyType.MainTexAB,ChaListDefine.KeyType.MainTex,out bundle,out asset);
                        writer.WriteLine(entry.Key+"\t"+entry.Value.Name+"\t"+bundle+"\t"+asset);
                    }
                }
                var filterName=Environment.GetEnvironmentVariable("OK_PROBE_FILTER_NAME");
                if(!String.IsNullOrEmpty(filterName)) {
                    var filters=Singleton<Studio.Info>.Instance.dicFilterLoadInfo;
                    using(var writer=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".filters.tsv"))
                        foreach(var entry in filters)writer.WriteLine(entry.Key+"\t"+entry.Value.name+"\t"+entry.Value.bundlePath+"\t"+entry.Value.fileName);
                    var matches=new List<KeyValuePair<int,Studio.Info.LoadCommonInfo>>();
                    foreach(var entry in filters)if(String.Equals(entry.Value.name,filterName,StringComparison.OrdinalIgnoreCase))matches.Add(entry);
                    if(matches.Count!=1)throw new Exception("Expected one native filter named "+filterName+", found "+matches.Count);
                    var selected=matches[0];Singleton<Studio.Studio>.Instance.SetACE(selected.Key);
                    File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".selected-filter.tsv",selected.Key+"\t"+selected.Value.name+"\t"+selected.Value.bundlePath+"\t"+selected.Value.fileName);
                }
                var depthText=Environment.GetEnvironmentVariable("OK_STUDIO_SHADOW_DEPTH");
                if(!String.IsNullOrEmpty(depthText)) {float depth;if(!Single.TryParse(depthText,NumberStyles.Float,CultureInfo.InvariantCulture,out depth)||Single.IsNaN(depth)||depth<0||depth>1)throw new Exception("Invalid Studio scene shadow depth");var ambient=Shader.GetGlobalVector("_ambientshadowG");ambient.w=1-depth;Shader.SetGlobalVector("_ambientshadowG",ambient);}
                if(Environment.GetEnvironmentVariable("OK_PROBE_LIGHTS")=="1")WriteSceneLightSnapshot();
                if(Environment.GetEnvironmentVariable("OK_PROBE_ACCESSORY_PHYSICS")=="1")RunAccessoryPhysicsControl(cha);
                if(Environment.GetEnvironmentVariable("OK_PROBE_BODY_MORPH")=="1")WriteBodyMorphControls(cha);
                if(Environment.GetEnvironmentVariable("OK_PROBE_FACE_CREATE")=="1")CaptureFaceOverlays(cha);WriteRendererSnapshot(cha);Save(new Dump{error="",clip="Native Renderer/Material snapshot"});}
            catch(Exception ex){Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_MATERIALS")=="1") {
            WriteHeadMaterials(cha,".head-materials-final.tsv");
            Save(new Dump{error="",controller="Native head materials",clip="post Studio load"});
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_HAND_SHAPES")=="1") {
            try {RunHandShapeControl(cha);Save(new Dump{error="",controller="KKS ShapeHandInfo",clip="cf_anmShapeHand"});}
            catch(Exception ex){Save(new Dump{error=ex.ToString()});}
            finished=true;Application.Quit();yield break;
        }
        if(Environment.GetEnvironmentVariable("OK_PROBE_MOUTH_SHAPES")=="1") {
            IEnumerator control=RunMouthShapeControl(cha);
            string controlError="";
            while(true) {
                bool next=false;
                try {next=control.MoveNext();} catch(Exception ex) {controlError=ex.ToString();}
                if(!next) break;
                yield return control.Current;
            }
            Save(new Dump{error=controlError,controller="KKS FBSBase",clip="mouth 0/8"});
            finished=true;Application.Quit();yield break;
        }
        if (Environment.GetEnvironmentVariable("OK_PROBE_COLLIDERS") == "1") {
            ApplyColliderFlags(cha);
        }
        cha.LoadAnimation("action/animator/00.unity3d", "c33", null);
        cha.animBody.SetFloat("MotionSpeed", 1f);
        cha.animBody.speed = 0f;
        cha.animBody.Play("Base Layer.Main.Idle", 0, 0.2625f);
        cha.animBody.Update(0f);
        Manager.Character.enableCorrectArmSize = false;
        Manager.Character.enableCorrectHandSize = true;
        for (int i = 0; i < 4; ++i) yield return new WaitForEndOfFrame();
        WritePhysics(cha);
        string physics = Environment.GetEnvironmentVariable("OK_PROBE_BODY_PHYSICS");
        if (physics == "0" || physics == "fixed") {
            DynamicBone_Ver02[] components = cha.objAnim.GetComponentsInChildren<DynamicBone_Ver02>(true);
            foreach (DynamicBone_Ver02 db in components) db.enabled = false;
            cha.animBody.Update(0f);
            if (Environment.GetEnvironmentVariable("OK_PROBE_COLLIDER_CONTACT") == "1") {
                Transform[] all = cha.objAnim.GetComponentsInChildren<Transform>(true);
                foreach (string side in new string[] { "L", "R" }) {
                    Transform arm = Array.Find(all, delegate(Transform tf) { return tf.name == "cf_s_forearm02_" + side; });
                    Transform breast = Array.Find(all, delegate(Transform tf) { return tf.name == "cf_j_bust02_" + side; });
                    if (arm == null || breast == null) throw new Exception("contact bones missing");
                    arm.position = breast.position + new Vector3(0.0f, 0.0f, -0.025f);
                }
            }
            // Read the actual Animator/ref-bone pose before any body simulation.
            WriteTransforms(cha, ".physics-baseline.tsv");
            if (physics == "fixed") {
                Transform[] frozen = cha.objAnim.GetComponentsInChildren<Transform>(true);
                Vector3[] positions = Array.ConvertAll(frozen, delegate(Transform tf) { return tf.localPosition; });
                Quaternion[] rotations = Array.ConvertAll(frozen, delegate(Transform tf) { return tf.localRotation; });
                Vector3[] scales = Array.ConvertAll(frozen, delegate(Transform tf) { return tf.localScale; });
                foreach (DynamicBone_Ver02 db in components) { db.ResetPosition(); db.ResetParticlesPosition(); }
                WritePhysics(cha, ".physics-setup.tsv");
                MethodInfo init = typeof(DynamicBone_Ver02).GetMethod("InitTransforms", BindingFlags.Instance | BindingFlags.NonPublic);
                MethodInfo step = typeof(DynamicBone_Ver02).GetMethod("UpdateDynamicBones", BindingFlags.Instance | BindingFlags.NonPublic);
                float dt = float.Parse(Environment.GetEnvironmentVariable("OK_PROBE_BODY_DT") ?? "0.016666667", CultureInfo.InvariantCulture);
                float move = float.Parse(Environment.GetEnvironmentVariable("OK_PROBE_BODY_MOVE") ?? "0", CultureInfo.InvariantCulture);
                for (int frame = 0; frame < 240; ++frame) {
                    for (int i = 0; i < frozen.Length; ++i) { frozen[i].localPosition = positions[i]; frozen[i].localRotation = rotations[i]; frozen[i].localScale = scales[i]; }
                    frozen[0].localPosition += new Vector3(move * (frame + 1), 0f, 0f);
                    foreach (DynamicBone_Ver02 db in components) { init.Invoke(db, null); step.Invoke(db, new object[] { dt }); }
                }
            }
        }
        if (Environment.GetEnvironmentVariable("OK_PROBE_ORDINARY_PHYSICS") == "fixed") {
            try { RunOrdinaryControl(cha); }
            catch (Exception ex) { finished = true; Save(new Dump { error = ex.ToString() }); Application.Quit(); yield break; }
        }
        if (Environment.GetEnvironmentVariable("OK_PROBE_CLOTHING_WEIGHTS") == "1") {
            ClothingControl control;
            try { control = SetupClothingControl(cha); }
            catch (Exception ex) { finished=true; Save(new Dump { error=ex.ToString() }); Application.Quit(); yield break; }
            yield return new WaitForEndOfFrame();
            try { WriteClothingControl(control); }
            catch (Exception ex) { finished=true; Save(new Dump { error=ex.ToString() }); Application.Quit(); yield break; }
        }
        if (Environment.GetEnvironmentVariable("OK_PROBE_HIERARCHY") == "1") {
            try { RunHierarchyControl(); }
            catch (Exception ex) { finished=true; Save(new Dump { error=ex.ToString() }); Application.Quit(); yield break; }
        }
        if (!String.IsNullOrEmpty(Environment.GetEnvironmentVariable("OK_PROBE_EXTERNAL_SCRIPTS"))) {
            try { RunExternalScriptControl(); }
            catch (Exception ex) { finished=true; Save(new Dump { error=ex.ToString() }); Application.Quit(); yield break; }
        }
        Dump dump = new Dump();
        try {
            AnimatorStateInfo state = cha.animBody.GetCurrentAnimatorStateInfo(0);
            dump.controller = cha.animBody.runtimeAnimatorController.name;
            dump.fullPathHash = state.fullPathHash; dump.normalizedTime = state.normalizedTime; dump.stateLength = state.length;
            AnimatorClipInfo[] clips = cha.animBody.GetCurrentAnimatorClipInfo(0);
            dump.clip = clips.Length > 0 ? clips[0].clip.name : "no clip";
            List<Bone> bones = new List<Bone>();
            foreach (Transform tf in cha.transform.GetComponentsInChildren<Transform>(true)) {
                if (tf.name.Contains("elbo") || tf.name.Contains("forearm01") || tf.name.Contains("arm00")) {
                    bones.Add(new Bone { name = tf.name, path = PathOf(tf), expressionIndex = -1,
                        position = tf.localPosition, rotation = tf.localRotation, scale = tf.localScale, worldPosition = tf.position });
                }
            }
            ExpressionBone expression = cha.expression;
            if (expression != null) {
                foreach (KeyValuePair<int, Transform> pair in expression.dictBone) {
                    if (pair.Key < 2 || pair.Key > 7 || pair.Value == null) continue;
                    Transform tf = pair.Value;
                    bones.Add(new Bone { name = tf.name, path = PathOf(tf), expressionIndex = pair.Key,
                        position = tf.localPosition, rotation = tf.localRotation, scale = tf.localScale, worldPosition = tf.position });
                }
                List<Flag> flags = new List<Flag>();
                foreach (FieldInfo field in typeof(ExpressionBone).GetFields(BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public))
                    if (field.FieldType == typeof(bool)) flags.Add(new Flag { name = field.Name, value = (bool)field.GetValue(expression) });
                flags.Add(new Flag { name = "component.enabled", value = expression.enabled });
                dump.flags = flags.ToArray();
            }
            dump.bones = bones.ToArray();
            using (StreamWriter w = new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT") + ".bones.tsv")) {
                w.WriteLine("source\tname\tpath\tlocalPosition\tlocalRotation\tlocalScale");
                foreach (Bone b in bones) w.WriteLine(b.expressionIndex + "\t" + b.name + "\t" + b.path + "\t" + b.position.ToString("R") + "\t" + b.rotation.ToString("R") + "\t" + b.scale.ToString("R"));
                w.WriteLine("expression_exists\t" + (expression != null));
                foreach (Transform tf in cha.transform.GetComponentsInChildren<Transform>(true))
                    w.WriteLine("all_transform\t" + tf.name + "\t" + PathOf(tf) + "\t" + tf.localPosition.ToString("R") + "\t" + tf.localRotation.ToString("R") + "\t" + tf.localScale.ToString("R"));
                if (expression != null && dump.flags != null)
                    foreach (Flag f in dump.flags) w.WriteLine("flag\t" + f.name + "\t" + f.value);
                foreach (ExpressionBone e in UnityEngine.Object.FindObjectsOfType<ExpressionBone>())
                    w.WriteLine("expression_component\t" + PathOf(e.transform) + "\t" + e.enable + "\t" + e.enabled);
            }
            foreach (SkinnedMeshRenderer renderer in cha.transform.GetComponentsInChildren<SkinnedMeshRenderer>(true)) {
                if (renderer.name != "o_body_a") continue;
                Mesh mesh = new Mesh(); renderer.BakeMesh(mesh);
                Vector3[] vertices = mesh.vertices;
                for (int i = 0; i < vertices.Length; ++i) vertices[i] = renderer.transform.TransformPoint(vertices[i]);
                dump.renderer = PathOf(renderer.transform); dump.vertices = vertices; dump.triangles = mesh.triangles;
                dump.bindVertices = renderer.sharedMesh.vertices;
                dump.weights = renderer.sharedMesh.boneWeights;
                dump.bindposes = renderer.sharedMesh.bindposes;
                dump.skinBones = Array.ConvertAll(renderer.bones, delegate(Transform tf) { return PathOf(tf); });
                dump.skinQuality = (int)renderer.quality;
                UnityEngine.Object.Destroy(mesh); break;
            }
            Bounds bounds = new Bounds(cha.transform.position, Vector3.zero);
            foreach (Renderer r in cha.transform.GetComponentsInChildren<Renderer>())
                if (r.enabled && r.gameObject.activeInHierarchy) bounds.Encapsulate(r.bounds);
            GameObject cameraObject = new GameObject("OpenKoikatsu probe camera");
            Camera camera = cameraObject.AddComponent<Camera>();
            if (Camera.main != null) camera.CopyFrom(Camera.main);
            camera.orthographic = true; camera.orthographicSize = bounds.size.y * 0.55f;
            camera.clearFlags = CameraClearFlags.SolidColor; camera.backgroundColor = Color.gray;
            camera.nearClipPlane = 0.01f; camera.farClipPlane = 100f;
            camera.transform.position = bounds.center + new Vector3(0f, 0f, 3f);
            camera.transform.LookAt(bounds.center);
            RenderTexture target = new RenderTexture(900, 1080, 24);
            camera.targetTexture = target; camera.Render();
            RenderTexture previous = RenderTexture.active; RenderTexture.active = target;
            Texture2D capture = new Texture2D(900, 1080, TextureFormat.RGB24, false);
            capture.ReadPixels(new Rect(0, 0, 900, 1080), 0, 0); capture.Apply();
            File.WriteAllBytes(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT") + ".png", capture.EncodeToPNG());
            RenderTexture.active = previous; camera.targetTexture = null;
            UnityEngine.Object.Destroy(capture); UnityEngine.Object.Destroy(target); UnityEngine.Object.Destroy(cameraObject);
        } catch (Exception ex) { dump.error = ex.ToString(); }
        Save(dump); finished = true; Application.Quit();
    }
}
