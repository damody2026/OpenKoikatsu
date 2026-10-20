using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using UnityEngine;

public sealed partial class GamePoseProbe {
    private static string PhysicsFloat(float v) { return v.ToString("R",CultureInfo.InvariantCulture); }
    private static string PhysicsIds(IEnumerable<Transform> values) {
        var ids=new List<string>();if(values!=null)foreach(var tf in values)if(tf!=null)ids.Add(tf.GetInstanceID().ToString());return String.Join(",",ids.ToArray());
    }
    private static void AddPhysicsAncestors(HashSet<Transform> nodes,Transform tf) {
        while(tf!=null) {nodes.Add(tf);tf=tf.parent;}
    }
    private static void RunAccessoryPhysicsControl(ChaControl cha) {
        string requested=Environment.GetEnvironmentVariable("OK_PROBE_RENDERER_MESHES");
        if(String.IsNullOrEmpty(requested))throw new Exception("Accessory physics control requires explicit sharedMesh names");
        var owners=new HashSet<GameObject>();
        foreach(var skin in cha.GetComponentsInChildren<SkinnedMeshRenderer>(true)) {
            if(skin.sharedMesh==null||Array.IndexOf(requested.Split(';'),skin.sharedMesh.name)<0)continue;
            foreach(var accessory in cha.objAccessory)if(accessory!=null&&skin.transform.IsChildOf(accessory.transform))owners.Add(accessory);
        }
        var targets=new List<DynamicBone>();
        foreach(var accessory in cha.objAccessory)if(accessory!=null&&owners.Contains(accessory))
            foreach(var db in accessory.GetComponentsInChildren<DynamicBone>(true))if(db.enabled&&db.gameObject.activeInHierarchy&&db.m_Root!=null)targets.Add(db);
        if(targets.Count==0)throw new Exception("Selected accessory has no active ordinary DynamicBone");
        const BindingFlags flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
        var type=typeof(DynamicBone);var init=type.GetMethod("InitTransforms",flags);var setup=type.GetMethod("SetupParticles",flags);var step=type.GetMethod("UpdateDynamicBones",flags);
        var nodes=new HashSet<Transform>();var colliders=new HashSet<DynamicBoneCollider>();
        foreach(var owner in owners)foreach(var tf in owner.GetComponentsInChildren<Transform>(true))AddPhysicsAncestors(nodes,tf);
        foreach(var db in targets) {
            AddPhysicsAncestors(nodes,db.transform);
            foreach(var tf in db.m_Root.GetComponentsInChildren<Transform>(true))AddPhysicsAncestors(nodes,tf);
            if(db.m_Colliders!=null)foreach(var c in db.m_Colliders)if(c!=null) {colliders.Add(c);AddPhysicsAncestors(nodes,c.transform);}
        }
        var ordered=new List<Transform>(nodes);ordered.Sort(delegate(Transform a,Transform b){return a.GetInstanceID().CompareTo(b.GetInstanceID());});
        var originalPositions=ordered.ConvertAll(delegate(Transform tf){return tf.localPosition;});
        var originalRotations=ordered.ConvertAll(delegate(Transform tf){return tf.localRotation;});
        var originalScales=ordered.ConvertAll(delegate(Transform tf){return tf.localScale;});
        var originalColliders=targets.ConvertAll(delegate(DynamicBone db){return db.m_Colliders;});
        var histories=new List<Dictionary<FieldInfo,object>>();
        foreach(var db in targets) {
            var saved=new Dictionary<FieldInfo,object>();
            foreach(var field in type.GetFields(BindingFlags.Instance|BindingFlags.NonPublic)) {
                object value=field.GetValue(db);var list=value as IList;
                if(list!=null) {var copy=(IList)Activator.CreateInstance(value.GetType());foreach(var item in list)copy.Add(item);value=copy;}
                saved.Add(field,value);
            }
            histories.Add(saved);
        }
        // No yield occurs during these steps. Other components cannot interleave.
        // Restore the installed particle init pose before capturing the baseline.
        foreach(var db in targets)init.Invoke(db,null);
        var positions=ordered.ConvertAll(delegate(Transform tf){return tf.localPosition;});
        var rotations=ordered.ConvertAll(delegate(Transform tf){return tf.localRotation;});
        var scales=ordered.ConvertAll(delegate(Transform tf){return tf.localScale;});
        using(var w=new StreamWriter(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".accessory-physics.tsv"))try {
            w.WriteLine("scope\tactual accessory ordinary DynamicBone, fixed native baseline, installed particle coefficients, 240 steps at 60Hz");
            for(int i=0;i<targets.Count;++i) {
                var particles=(IList)histories[i][type.GetField("m_Particles",flags)];
                for(int j=0;j<particles.Count;++j) {
                    object p=particles[j];var pt=p.GetType();var tf=(Transform)pt.GetField("m_Transform",flags).GetValue(p);
                    var values=new List<string>{"live_particle",i.ToString(),j.ToString(),tf==null?"0":tf.GetInstanceID().ToString()};
                    foreach(string field in new[]{"m_ParentIndex","m_Position","m_PrevPosition","m_EndOffset","m_InitLocalPosition","m_InitLocalRotation","m_Damping","m_Elasticity","m_Stiffness","m_Inert","m_Radius","m_BoneLength"}) {
                        object v=pt.GetField(field,flags).GetValue(p);values.Add(v is float?PhysicsFloat((float)v):v is Vector3?((Vector3)v).ToString("R"):v is Quaternion?((Quaternion)v).ToString("R"):v.ToString());
                    }
                    w.WriteLine(String.Join("\t",values.ToArray()));
                }
                foreach(var field in histories[i])if(!(field.Value is IList)&&!(field.Value is Transform)) {
                    object v=field.Value;w.WriteLine("live_private\t"+i+"\t"+field.Key.Name+"\t"+(v is float?PhysicsFloat((float)v):v is Vector3?((Vector3)v).ToString("R"):v==null?"null":v.ToString()));
                }
            }
            foreach(var tf in ordered)w.WriteLine("node\t"+tf.GetInstanceID()+"\t"+(tf.parent==null?0:tf.parent.GetInstanceID())+"\t"+tf.GetSiblingIndex()+"\t"+tf.name+"\t"+tf.localPosition.ToString("R")+"\t"+tf.localRotation.ToString("R")+"\t"+tf.localScale.ToString("R"));
            foreach(var c in colliders)w.WriteLine("collider\t"+c.GetInstanceID()+"\t"+c.transform.GetInstanceID()+"\t"+c.m_Center.ToString("R")+"\t"+PhysicsFloat(c.m_Radius)+"\t"+PhysicsFloat(c.m_Height)+"\t"+(int)c.m_Direction+"\t"+((int)c.m_Bound==1)+"\t"+(c.enabled&&c.gameObject.activeInHierarchy));
            for(int i=0;i<targets.Count;++i) {
                var db=targets[i];var refs=new List<string>();if(db.m_Colliders!=null)foreach(var c in db.m_Colliders)if(c!=null&&c.enabled&&c.gameObject.activeInHierarchy)refs.Add(c.GetInstanceID().ToString());
                w.WriteLine("db\t"+i+"\t"+db.transform.GetInstanceID()+"\t"+db.m_Root.GetInstanceID()+"\t"+PhysicsFloat(db.m_UpdateRate)+"\t"+PhysicsFloat(db.m_Damping)+"\t"+PhysicsFloat(db.m_Elasticity)+"\t"+PhysicsFloat(db.m_Stiffness)+"\t"+PhysicsFloat(db.m_Inert)+"\t"+PhysicsFloat(db.m_Radius)+"\t"+db.m_Gravity.ToString("R")+"\t"+db.m_Force.ToString("R")+"\t"+PhysicsFloat(db.m_EndLength)+"\t"+db.m_EndOffset.ToString("R")+"\t"+(int)db.m_FreezeAxis+"\t"+PhysicsIds(db.m_Exclusions)+"\t"+PhysicsIds(db.m_notRolls)+"\t"+String.Join(",",refs.ToArray())+"\t"+PhysicsFloat((float)type.GetField("m_Weight",flags).GetValue(db)));
            }
            foreach(string mode in new[]{"installed","no_colliders"}) {
                for(int i=0;i<ordered.Count;++i) {ordered[i].localPosition=positions[i];ordered[i].localRotation=rotations[i];ordered[i].localScale=scales[i];}
                for(int i=0;i<targets.Count;++i) {
                    var db=targets[i];db.m_Colliders=mode=="installed"?originalColliders[i]:new List<DynamicBoneCollider>();setup.Invoke(db,null);type.GetField("m_Time",flags).SetValue(db,0f);
                    var particles=(IList)type.GetField("m_Particles",flags).GetValue(db);
                    for(int j=0;j<particles.Count;++j) {
                        object p=particles[j];var pt=p.GetType();var tf=(Transform)pt.GetField("m_Transform",flags).GetValue(p);
                        var values=new List<string>{"particle",mode,i.ToString(),j.ToString(),tf==null?"0":tf.GetInstanceID().ToString()};
                        foreach(string field in new[]{"m_ParentIndex","m_Position","m_PrevPosition","m_EndOffset","m_InitLocalPosition","m_InitLocalRotation","m_Damping","m_Elasticity","m_Stiffness","m_Inert","m_Radius","m_BoneLength"}) {
                            object v=pt.GetField(field,flags).GetValue(p);values.Add(v is float?PhysicsFloat((float)v):v is Vector3?((Vector3)v).ToString("R"):v is Quaternion?((Quaternion)v).ToString("R"):v.ToString());
                        }
                        w.WriteLine(String.Join("\t",values.ToArray()));
                    }
                }
                for(int frame=0;frame<=240;++frame) {
                    if(frame>0) {foreach(var db in targets)init.Invoke(db,null);foreach(var db in targets)step.Invoke(db,new object[]{1f/60f});}
                    if(frame==0||frame==1||frame==60||frame==240)foreach(var tf in ordered)
                        w.WriteLine("result\t"+mode+"\t"+frame+"\t"+tf.GetInstanceID()+"\t"+tf.localPosition.ToString("R")+"\t"+tf.localRotation.ToString("R")+"\t"+tf.position.ToString("R"));
                }
            }
        } finally {
            for(int i=0;i<ordered.Count;++i) {ordered[i].localPosition=originalPositions[i];ordered[i].localRotation=originalRotations[i];ordered[i].localScale=originalScales[i];}
            for(int i=0;i<targets.Count;++i)targets[i].m_Colliders=originalColliders[i];
            for(int i=0;i<targets.Count;++i)foreach(var field in histories[i])field.Key.SetValue(targets[i],field.Value);
        }
    }
}
