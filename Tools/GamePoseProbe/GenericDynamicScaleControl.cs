using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using UnityEngine;

public sealed partial class GamePoseProbe {
 [Serializable] private class DScaleNode {public int id,parent;public string name;public Vector3 pos,scale,world,lossyScale;public Quaternion rot,worldRot;}
 [Serializable] private class DScaleParticle {public int parent;public Vector3 pos,prev;}
 [Serializable] private class DScaleFrame {public int frame;public float dt,objectScale;public DScaleNode[] input,output;public DScaleParticle[] particles;}
 [Serializable] private class DScaleSequence {public string name;public int freeze;public bool collision,colliderInside;public float colliderHeight;public DScaleNode[] initial;public DScaleParticle[] initialParticles;public DScaleFrame[] frames;}
 [Serializable] private class DScaleDump {public string scope;public DScaleSequence[] sequences;}
 private static DScaleNode[] DScaleNodes(Transform[] nodes) {
  var rows=new List<DScaleNode>();
  for(int i=0;i<nodes.Length;i++)rows.Add(new DScaleNode{id=i+1,parent=nodes[i].parent==null?0:Array.IndexOf(nodes,nodes[i].parent)+1,
   name=nodes[i].name,pos=nodes[i].localPosition,rot=nodes[i].localRotation,scale=nodes[i].localScale,world=nodes[i].position,lossyScale=nodes[i].lossyScale,worldRot=nodes[i].rotation});
  return rows.ToArray();
 }
 private static DScaleParticle[] DScaleParticles(DynamicBone db) {
  const BindingFlags flags=BindingFlags.Instance|BindingFlags.NonPublic|BindingFlags.Public;
  var rows=new List<DScaleParticle>();
  foreach(object p in (IList)typeof(DynamicBone).GetField("m_Particles",flags).GetValue(db)) {
   var type=p.GetType();rows.Add(new DScaleParticle{parent=(int)type.GetField("m_ParentIndex",flags).GetValue(p),
    pos=(Vector3)type.GetField("m_Position",flags).GetValue(p),prev=(Vector3)type.GetField("m_PrevPosition",flags).GetValue(p)});
  }
  return rows.ToArray();
 }
 private static void RunGenericDynamicScaleControl() {
  const BindingFlags flags=BindingFlags.Instance|BindingFlags.NonPublic|BindingFlags.Public;
  var type=typeof(DynamicBone);var setup=type.GetMethod("SetupParticles",flags);
  var init=type.GetMethod("InitTransforms",flags);var step=type.GetMethod("UpdateDynamicBones",flags);
  var sequences=new List<DScaleSequence>();
  foreach(string mode in new[]{"free","sphere","capsule","freeze_z","nonuniform","rotated_sphere","rotated_capsule","rotated_inside_sphere","rotated_inside_capsule"}) {
   var objects=new List<GameObject>();
   try {
    foreach(string name in new[]{"height","component","root","tip1","tip2","collider"})objects.Add(new GameObject("ok_dynamic_scale_"+name));
    var nodes=objects.ConvertAll(delegate(GameObject g){return g.transform;}).ToArray();
    nodes[1].SetParent(nodes[0],false);nodes[2].SetParent(nodes[1],false);
    nodes[3].SetParent(nodes[2],false);nodes[4].SetParent(nodes[3],false);nodes[5].SetParent(nodes[0],false);
    nodes[2].localPosition=new Vector3(0.04f,0.03f,-0.02f);nodes[2].localRotation=Quaternion.Euler(12f,25f,7f);
    nodes[3].localPosition=new Vector3(0f,-0.3f,0.04f);nodes[4].localPosition=new Vector3(0.03f,-0.25f,0f);
    var c=objects[5].AddComponent<DynamicBoneCollider>();c.m_Radius=0.14f;c.m_Center=new Vector3(0.07f,-0.35f,0.04f);
    bool rotated=mode.StartsWith("rotated_");bool inside=mode.Contains("inside");
    if(rotated)nodes[5].localRotation=Quaternion.Euler(18f,38f,11f);
    c.m_Height=mode.Contains("capsule")?0.45f:0f;c.m_Direction=DynamicBoneCollider.Direction.Y;c.m_Bound=inside?DynamicBoneCollider.Bound.Inside:DynamicBoneCollider.Bound.Outside;
    var db=objects[1].AddComponent<DynamicBone>();db.enabled=false;db.m_Root=nodes[2];
    db.m_UpdateRate=60f;db.m_Damping=0.2f;db.m_Elasticity=0.15f;db.m_Stiffness=0.1f;db.m_Inert=0.3f;db.m_Radius=0.012f;
    db.m_Gravity=new Vector3(0.001f,-0.005f,0.0005f);db.m_Force=new Vector3(0.003f,-0.007f,0.002f);
    db.m_EndOffset=new Vector3(0.02f,-0.12f,0.01f);db.m_Exclusions=new List<Transform>();db.m_notRolls=new List<Transform>();
    db.m_Colliders=new List<DynamicBoneCollider>();if(mode!="free")db.m_Colliders.Add(c);
    db.m_FreezeAxis=mode=="freeze_z"?DynamicBone.FreezeAxis.Z:DynamicBone.FreezeAxis.None;
    setup.Invoke(db,null);type.GetField("m_Time",flags).SetValue(db,0f);
    var sequence=new DScaleSequence{name=mode,collision=mode!="free",freeze=(int)db.m_FreezeAxis,colliderHeight=c.m_Height,colliderInside=inside,
     initial=DScaleNodes(nodes),initialParticles=DScaleParticles(db)};
    var frames=new List<DScaleFrame>();
    for(int f=0;f<180;f++) {
     init.Invoke(db,null);
     float scale=new[]{1f,0.6f,1.8f,1f}[f/45];
     nodes[0].localScale=mode=="nonuniform"||rotated?new Vector3(scale,scale*0.8f,scale*1.2f):Vector3.one*scale;
     nodes[1].localPosition=new Vector3(0.08f*Mathf.Sin(f*0.07f),0.02f*Mathf.Cos(f*0.05f),0.03f*Mathf.Sin(f*0.03f));
     nodes[1].localRotation=Quaternion.Euler(4f*Mathf.Sin(f*0.05f),7f*Mathf.Cos(f*0.04f),0f);
     var input=DScaleNodes(nodes);step.Invoke(db,new object[]{1f/60f});
     frames.Add(new DScaleFrame{frame=f,dt=1f/60f,objectScale=(float)type.GetField("m_ObjectScale",flags).GetValue(db),input=input,output=DScaleNodes(nodes),particles=DScaleParticles(db)});
    }
    sequence.frames=frames.ToArray();sequences.Add(sequence);
   } finally {if(objects.Count>0)UnityEngine.Object.Destroy(objects[0]);}
  }
  File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".dynamic-scale.json",ProbeJson(new DScaleDump{
   scope="Native ordinary DynamicBone generated renderer-free hierarchy; persistent 60Hz particles, scale/move/restore, sphere/capsule/freeze/nonuniform controls; no cards or scene actions",
   sequences=sequences.ToArray()}));
 }
}
