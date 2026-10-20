using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

public sealed partial class GamePoseProbe {
 [Serializable] private class ColliderSample {public string region;public Vector3 input,output;public float particleRadius;}
 [Serializable] private class ColliderCase {public string name;public int direction;public bool inside;public float radius,height;public Vector3 center,a,b;public DScaleNode[] nodes;public ColliderSample[] samples;}
 [Serializable] private class ColliderDump {public string scope;public ColliderCase[] cases;}
 private static void RunGenericColliderControl() {
  var rows=new List<ColliderCase>();
  var scales=new[]{Vector3.one,new Vector3(1.8f,0.6f,1.2f),new Vector3(-1.8f,0.6f,1.2f),
   new Vector3(1.8f,-0.6f,1.2f),new Vector3(1.8f,0.6f,-1.2f),new Vector3(-1.8f,-0.6f,1.2f),
   new Vector3(-1.8f,-0.6f,-1.2f),Vector3.zero,new Vector3(1.8f,0.6f,0f),Vector3.one};
  for(int config=0;config<scales.Length;config++) {
   var parent=new GameObject("ok_collider_parent");
   try {
    var child=new GameObject("ok_collider_child");var obj=new GameObject("ok_collider_control");
    child.transform.SetParent(parent.transform,false);obj.transform.SetParent(child.transform,false);
    parent.transform.localPosition=new Vector3(0.3f,-0.1f,0.2f);parent.transform.localRotation=Quaternion.Euler(11f,24f,8f);parent.transform.localScale=scales[config];
    child.transform.localPosition=new Vector3(0.07f,-0.02f,0.11f);child.transform.localRotation=Quaternion.Euler(18f,38f,11f);child.transform.localScale=new Vector3(0.9f,1.1f,0.8f);
    obj.transform.localPosition=new Vector3(0.04f,0.03f,-0.02f);obj.transform.localRotation=Quaternion.Euler(12f,25f,7f);obj.transform.localScale=new Vector3(0.85f,1.15f,1.05f);
    if(config==9)foreach(Transform node in new[]{parent.transform,child.transform,obj.transform}) {
     node.localPosition=Vector3.zero;node.localRotation=Quaternion.identity;node.localScale=Vector3.one;
    }
    var c=obj.AddComponent<DynamicBoneCollider>();c.m_Radius=0.14f;c.m_Center=new Vector3(0.07f,-0.35f,0.04f);
    for(int axis=0;axis<3;axis++)for(int bound=0;bound<2;bound++)foreach(float height in new[]{0f,0.14f,0.45f}) {
     c.m_Direction=(DynamicBoneCollider.Direction)axis;c.m_Bound=(DynamicBoneCollider.Bound)bound;c.m_Height=height;
     float half=Mathf.Max(0f,(height-c.m_Radius)*0.5f);Vector3 lo=c.m_Center,hi=c.m_Center;lo[axis]-=half;hi[axis]+=half;
     Vector3 a=obj.transform.TransformPoint(lo),b=obj.transform.TransformPoint(hi);
     Vector3 along=(b-a).sqrMagnitude>0f?(b-a).normalized:obj.transform.rotation*Vector3.up;
     Vector3 radial=Vector3.Cross(along,Vector3.right);if(radial.sqrMagnitude<0.01f)radial=Vector3.Cross(along,Vector3.up);radial.Normalize();
     var samples=new List<ColliderSample>();var anchors=new[]{a-along*0.11f,(a+b)*0.5f,b+along*0.11f};
     foreach(float particleRadius in new[]{0f,0.012f,0.2f})for(int region=0;region<3;region++)foreach(float fraction in new[]{0f,0.25f,1f,1.01f,2f}) {
      float reach=c.m_Radius*Mathf.Abs(obj.transform.lossyScale.z)+particleRadius;
      Vector3 input=anchors[region]+radial*(reach*fraction),output=input;c.Collide(ref output,particleRadius);
      samples.Add(new ColliderSample{region=new[]{"low","interior","high"}[region],particleRadius=particleRadius,input=input,output=output});
     }
     rows.Add(new ColliderCase{name="scale"+config+"_axis"+axis+"_bound"+bound+"_height"+height.ToString("R",System.Globalization.CultureInfo.InvariantCulture),
      direction=axis,inside=bound==1,radius=c.m_Radius,height=height,center=c.m_Center,a=a,b=b,
      nodes=DScaleNodes(new[]{parent.transform,child.transform,obj.transform}),samples=samples.ToArray()});
    }
   } finally {UnityEngine.Object.Destroy(parent);}
  }
  File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".collider.json",ProbeJson(new ColliderDump{
   scope="Native DynamicBoneCollider direct numerical controls; XYZ, sphere threshold/capsule, inside/outside, mirrored/nonuniform/zero scales; no cards, renderers or scene actions",cases=rows.ToArray()}));
 }
}
