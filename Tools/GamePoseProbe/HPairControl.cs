// Compiled-native MotionIK binding/shape oracle. No screenshots or card writes.
using System;using System.Collections.Generic;using System.IO;using UnityEngine;
using Illusion.Component.Correct;using Illusion.Component.Correct.Process;
public sealed partial class GamePoseProbe {
 [Serializable] private class HTarget {public int index;public string bone,bendBone;public float positionWeight,rotationWeight,bendWeight;public Vector3 pos,bendPos;public Quaternion rot,bendRot;}
 [Serializable] private class HFrame {public int index,order;public string bone;public bool enabled;public Vector3 pos;public Quaternion rot;}
 [Serializable] private class HContract {public int sex;public string motion,state;public int[] partnerSex;public float[] ownShape,otherShape;public HTarget[] targets;public HFrame[] frames;}
 [Serializable] private class HContracts {public string scope;public HContract[] contracts;}
 private static readonly Dictionary<RootMotion.FinalIK.IKSolver,HTransform[]> HFirstInit=new Dictionary<RootMotion.FinalIK.IKSolver,HTransform[]>();
 private static readonly Dictionary<RootMotion.FinalIK.IKSolver,HTransform[]> HLatestInit=new Dictionary<RootMotion.FinalIK.IKSolver,HTransform[]>();
 private static readonly Dictionary<RootMotion.FinalIK.IKSolver,int> HInitCount=new Dictionary<RootMotion.FinalIK.IKSolver,int>();
 [Serializable] private class HInitialBend {public Vector3 direction,localDirection,childDirection;}
 private static readonly Dictionary<RootMotion.FinalIK.IKSolver,HInitialBend[]> HInitBends=new Dictionary<RootMotion.FinalIK.IKSolver,HInitialBend[]>();
 private static void HInitiatePostfix(RootMotion.FinalIK.IKSolver __instance){
  var solver=__instance as RootMotion.FinalIK.IKSolverFullBodyBiped;if(solver==null||!solver.initiated)return;HRecordLimbOrtho(solver);var rows=new List<HInitialBend>();
  foreach(var chain in solver.chain){var b=chain.bendConstraint;var flags=System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.Public|System.Reflection.BindingFlags.NonPublic;var type=typeof(RootMotion.FinalIK.IKConstraintBend);rows.Add(new HInitialBend{direction=b.direction,localDirection=(Vector3)type.GetField("defaultLocalDirection",flags).GetValue(b),childDirection=(Vector3)type.GetField("defaultChildDirection",flags).GetValue(b)});}HInitBends[__instance]=rows.ToArray();
 }
 private static void HInitiatePrefix(RootMotion.FinalIK.IKSolver __instance,Transform root){
  if(!(__instance is RootMotion.FinalIK.IKSolverFullBodyBiped)||root==null)return;
  foreach(var t in root.GetComponentsInChildren<Transform>(true))if(t.name=="cf_j_root"){var bones=HSkeleton(t);if(!HFirstInit.ContainsKey(__instance))HFirstInit.Add(__instance,bones);HLatestInit[__instance]=bones;HInitCount[__instance]=HInitCount.ContainsKey(__instance)?HInitCount[__instance]+1:1;return;}
 }
 private static ChaControl HCreatePartner(){
  string path=Environment.GetEnvironmentVariable("OK_PROBE_PAIR_CARD");var card=new ChaFileControl();if(!card.LoadCharaFile(path,byte.MaxValue,noLoadPng:true))throw new Exception("Missing native partner card");
  ChaControl other=card.parameter.sex==0?Manager.Character.CreateMale(card.parameter.exType,Manager.Scene.commonSpace,-1):Manager.Character.CreateFemale(Manager.Scene.commonSpace,-1);
  other.chaFile.LoadCharaFile(path,byte.MaxValue,noLoadPng:true);other.Load(reflectStatus:true);
  return other;
 }
 private System.Collections.IEnumerator RunNativeHPairAutomatic(ChaControl first){
  ChaControl other=null;Exception error=null;try{other=HCreatePartner();}catch(Exception ex){error=ex;}
  if(error==null){yield return new WaitForSecondsRealtime(2f);try{RunNativeHPair(first,other);}catch(Exception ex){error=ex;}}
  Save(new Dump{error=error==null?"":error.ToString(),clip="Native automatic solver initialization"});
 }
 private static void RunNativeHPair(ChaControl first,ChaControl other=null){
  if(other==null)other=HCreatePartner();
  var female=first.chaFile.parameter.sex==1?first:other;var male=first.chaFile.parameter.sex==0?first:other;
  var ik=MotionIK.Setup(new List<ChaControl>{female,male});var rows=new List<HContract>();
  foreach(string prefix in new[]{"khh","khs"}){
   for(int role=0;role<2;role++){string name=prefix+(role==0?"_f_00":"_m_00");var asset=GlobalMethod.LoadAllFolderInOneFile<TextAsset>("h/list/",name);if(asset==null||!ik[role].LoadData(asset))throw new Exception("Native MotionIK missing "+name);}
   foreach(string state in new[]{"Idle","SLoop","OLoop","L_SLoop1"})for(int role=0;role<2;role++){
    var motion=ik[role];if(motion.ik==null||motion.frameCorrect==null||motion.ikTargetPairs==null)throw new Exception("Factory missing native IK role="+role);motion.Calc(state);var targets=new List<HTarget>();int index=0;
    foreach(var pair in motion.ikTargetPairs){var data=pair.effector.target.GetComponent<BaseData>();var bend=pair.bend.bendGoal.GetComponent<BaseData>();targets.Add(new HTarget{index=index++,bone=data.bone==null?"":data.bone.name,bendBone=bend.bone==null?"":bend.bone.name,pos=data.pos,rot=data.rot,bendPos=bend.pos,bendRot=bend.rot,positionWeight=pair.effector.positionWeight,rotationWeight=pair.effector.rotationWeight,bendWeight=pair.bend.weight});}
    var frames=new List<HFrame>();index=0;foreach(var f in motion.frameCorrect.list)frames.Add(new HFrame{index=index++,order=(int)f.type,bone=f.bone==null?"":f.bone.name,enabled=f.enabled,pos=f.pos,rot=f.rot});
    var own=new float[motion.info.chaFile.custom.body.shapeValueBody.Length];var peer=new float[motion.partners[1].info.chaFile.custom.body.shapeValueBody.Length];for(int i=0;i<own.Length;i++){own[i]=motion.info.GetShapeBodyValue(i);peer[i]=motion.partners[1].info.GetShapeBodyValue(i);}
    rows.Add(new HContract{sex=motion.info.chaFile.parameter.sex,motion=prefix+(role==0?"_f_00":"_m_00"),state=state,partnerSex=new[]{(int)motion.partners[0].info.chaFile.parameter.sex,(int)motion.partners[1].info.chaFile.parameter.sex},ownShape=own,otherShape=peer,targets=targets.ToArray(),frames=frames.ToArray()});
   }
  }
  File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".pair-contract.json",ProbeJson(new HContracts{scope="compiled MotionIK.Calc bindings, weights and shape offsets; not complete scene solver parity",contracts=rows.ToArray()}));
  if(Environment.GetEnvironmentVariable("OK_PROBE_H_SOLVER")=="1")RunNativeHSolver(female,male,ik);
 }
 [Serializable] private class HTransform {public string name,path,parent;public Vector3 pos,scale,worldPos;public Quaternion rot,worldRot;}
 [Serializable] private class HSolveTarget {public Vector3 pos;public Quaternion rot;public float positionWeight,rotationWeight;}
 [Serializable] private class HSolveBend {public Vector3 pos;public float weight;}
 [Serializable] private class HChain {public float length;public Vector3[] nodes;}
 [Serializable] private class HMath {public Vector3 from,to;public Quaternion fromTo,look;}
 [Serializable] private class HRotationMath {public Quaternion parent,requested,local,world,inverse,lerp,slerp;public float weight;}
 [Serializable] private class HOrthoMath {public Vector3 normal,tangent,resultNormal,resultTangent;}
 private static readonly List<HOrthoMath> HOrthoRows=new List<HOrthoMath>();
 private static Vector3 HRecordOrtho(Vector3 normal,Vector3 tangent){var n=normal;var t=tangent;Vector3.OrthoNormalize(ref n,ref t);HOrthoRows.Add(new HOrthoMath{normal=normal,tangent=tangent,resultNormal=n,resultTangent=t});return t;}
 private static void HRecordLimbOrtho(RootMotion.FinalIK.IKSolverFullBodyBiped solver){foreach(var chain in solver.chain)if(chain.nodes.Length==3){var a=chain.nodes[0].transform.position;var b=chain.nodes[1].transform.position;var c=chain.nodes[2].transform.position;var t=HRecordOrtho(c-a,b-a);HRecordOrtho(b-a,t);HRecordOrtho(b-a,c-b);}} [Serializable] private class HTriangleMath {public Vector3 direction,bend,result,reconstructed;public Quaternion look;public float magnitude,sqrMag1,sqrMagDif,z,y;}
 private static readonly List<HTriangleMath> HTriangleRows=new List<HTriangleMath>();
 private static bool HTriangleCapture;
 private static void HTrianglePostfix(RootMotion.FinalIK.FBIKChain __instance,Vector3 direction,Vector3 bendDirection,float directionMagnitude,Vector3 __result){if(!HTriangleCapture)return;var flags=System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic;float sq=(float)typeof(RootMotion.FinalIK.FBIKChain).GetField("sqrMag1",flags).GetValue(__instance);float diff=(float)typeof(RootMotion.FinalIK.FBIKChain).GetField("sqrMagDif",flags).GetValue(__instance);float z=(directionMagnitude*directionMagnitude+diff)/2f/directionMagnitude;float y=(float)Math.Sqrt(Mathf.Clamp(sq-z*z,0f,float.PositiveInfinity));var look=Quaternion.LookRotation(direction,bendDirection);HTriangleRows.Add(new HTriangleMath{direction=direction,bend=bendDirection,magnitude=directionMagnitude,sqrMag1=sq,sqrMagDif=diff,z=z,y=y,look=look,reconstructed=look*new Vector3(0f,y,z),result=__result});} [Serializable] private class HVectorMath {public Vector3 from,normal,result,slerp;public float clamp,angle,t;public bool changed;}
 private static readonly List<HVectorMath> HVectorRows=new List<HVectorMath>();
 private static void HMathOracle(){
  var rows=new List<HMath>();foreach(var from in new[]{Vector3.right,new Vector3(0.234f,-0.735f,0.111f),new Vector3(-0.09123f,0.1243f,0.8135f)})foreach(float amount in new[]{1f,0.1f,0.01f,0.001f,0.0003f,0.0001f,0.00001f,0.000001f,0f,-0.001f}){
   var to=from+new Vector3(amount*0.1234f,amount*0.7334f,amount*0.2535f);rows.Add(new HMath{from=from,to=to,fromTo=Quaternion.FromToRotation(from,to),look=Quaternion.LookRotation(from,to)});
  }
  File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".pair-math.json",ProbeJson(rows.ToArray()));
  var rotationRows=new List<HRotationMath>();var parent=new GameObject("HReadonlyMathParent");var child=new GameObject("HReadonlyMathChild");child.transform.SetParent(parent.transform,false);parent.transform.rotation=new Quaternion(0.1234f,-0.5324f,0.3723f,0.7251f);
  foreach(var requested in new[]{new Quaternion(0.2734f,-0.3913f,0.5173f,0.8031f),new Quaternion(0.4325f,0.2391f,-0.7341f,0.9423f)})foreach(float weight in new[]{0f,0.25f,0.3f,0.5f,1f}){
   child.transform.rotation=requested;rotationRows.Add(new HRotationMath{parent=parent.transform.rotation,requested=requested,local=child.transform.localRotation,world=child.transform.rotation,inverse=Quaternion.Inverse(requested),lerp=Quaternion.Lerp(parent.transform.rotation,requested,weight),slerp=Quaternion.Slerp(parent.transform.rotation,requested,weight),weight=weight});
  }
  File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".pair-rotation-math.json",ProbeJson(rotationRows.ToArray()));UnityEngine.Object.Destroy(child);UnityEngine.Object.Destroy(parent);
 }
 private static HChain[] HChains(RootMotion.FinalIK.IKSolverFullBody solver){var rows=new List<HChain>();foreach(var chain in solver.chain){var nodes=new List<Vector3>();foreach(var node in chain.nodes)nodes.Add(node.solverPosition);rows.Add(new HChain{length=(float)typeof(RootMotion.FinalIK.FBIKChain).GetField("length",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic).GetValue(chain),nodes=nodes.ToArray()});}return rows.ToArray();}
 [Serializable] private class HSolveFrame {public int role;public bool solverEnabled;public float time;public HTransform[] sampled,input,output,afterCorrection,afterRead;public HSolveTarget[] targets;public HSolveBend[] bends;public HChain[] chains,readChains;}
 [Serializable] private class HSolveSequence {public string motion,state;public bool nonuniformScale;public HTransform[][] initial;public HSolveFrame[] frames;}
 [Serializable] private class HSolveDump {public string scope;public bool automaticInitialization;public int[] initializationCounts;public HInitialBend[][] initialBends;public HTransform[][] firstInitialization,latestInitialization;public HSolveSequence[] sequences;}
 private static HTransform[] HSkeleton(ChaControl cha){
  return HSkeleton(cha.objBodyBone.transform);
 }
 private static HTransform[] HSkeleton(Transform root){
  var rows=new List<HTransform>();
  foreach(var t in root.GetComponentsInChildren<Transform>(true)){
   rows.Add(new HTransform{name=t.name,path=PathOf(t),parent=t==root?"":PathOf(t.parent),pos=t==root?t.position:t.localPosition,rot=t==root?t.rotation:t.localRotation,scale=t==root?t.lossyScale:t.localScale,worldPos=t.position,worldRot=t.rotation});
  }return rows.ToArray();
 }
 private static void RunNativeHSolver(ChaControl female,ChaControl male,List<MotionIK> ik){
  var roles=new[]{female,male};var sequences=new List<HSolveSequence>();
  bool automatic=Environment.GetEnvironmentVariable("OK_PROBE_H_AUTO_INIT")=="1";
  if(automatic)HMathOracle();
  if(automatic)foreach(var motion in ik)if(!motion.ik.solver.initiated||!HFirstInit.ContainsKey(motion.ik.solver))throw new Exception("Automatic solver initialization was not observed");
  foreach(var cha in roles){foreach(var db in cha.GetComponentsInChildren<DynamicBone>(true))db.enabled=false;foreach(var db in cha.GetComponentsInChildren<DynamicBone_Ver02>(true))db.enabled=false;}
  foreach(string prefix in new[]{"khh","khs"}){
   string folder="h/anim/",bundle=prefix=="khh"?"01_01_00.unity3d":"02_01_00.unity3d";
   for(int role=0;role<2;role++){
    string owner=prefix+(role==0?"_f":"_m");var cha=roles[role];cha.LoadAnimation(folder+(role==0?"female/":"male/")+bundle,owner+"_base",null);
    var overlay=CommonLib.LoadAsset<RuntimeAnimatorController>(folder+(role==0?"female/":"male/")+bundle,owner+"_00");
    if(overlay==null)throw new Exception("Missing solver controller "+owner);cha.animBody.runtimeAnimatorController=Illusion.Utils.Animator.SetupAnimatorOverrideController(cha.animBody.runtimeAnimatorController,overlay);
    cha.animBody.enabled=false;cha.animBody.SetFloat("height",female.GetShapeBodyValue(0));cha.animBody.SetFloat("motion",0f);
    for(int layer=1;layer<cha.animBody.layerCount;layer++)cha.animBody.SetLayerWeight(layer,0f);
    var asset=GlobalMethod.LoadAllFolderInOneFile<TextAsset>("h/list/",owner+"_00");if(!ik[role].LoadData(asset))throw new Exception("Missing solver MotionIK");
    ik[role].ik.enabled=false;cha.animBody.Play("Idle",0,0f);cha.animBody.Update(0f);
   }
   foreach(bool warped in new[]{false,true})foreach(string state in warped?new[]{"Idle"}:new[]{"Idle","SLoop","OLoop","L_SLoop1"}){
   if(!female.animBody.HasState(0,Animator.StringToHash(state))||!male.animBody.HasState(0,Animator.StringToHash(state)))continue;
   for(int role=0;role<2;role++){roles[role].objBodyBone.transform.parent.localScale=warped?new Vector3(1.15f,0.85f,1.05f):Vector3.one;roles[role].animBody.Play(state,0,0f);roles[role].animBody.Update(0f);}
   var initial=automatic?new[]{HFirstInit[ik[0].ik.solver],HFirstInit[ik[1].ik.solver]}:new[]{HSkeleton(female),HSkeleton(male)};
   if(!automatic)for(int role=0;role<2;role++)ik[role].ik.solver.Initiate(ik[role].ik.transform);
   var frames=new List<HSolveFrame>();
   foreach(float time in new[]{0f,0.1f,0.25f,0.61f,1f,1.25f}){
    for(int role=0;role<2;role++){var a=roles[role].animBody;a.Play(state,0,0f);a.Update(0f);float length=a.GetCurrentAnimatorStateInfo(0).length;a.Play(state,0,time/length);a.Update(0f);ik[role].Calc(state);}
    var sampled=new[]{HSkeleton(female),HSkeleton(male)};
    // Invoke compiled correction processors before the solve; their output is
    // recorded as the solver input, isolating solver math from sampling/order.
    foreach(int order in new[]{0,1})foreach(var motion in ik)foreach(var f in motion.frameCorrect.list)if(f.enabled&&(int)f.type==order)typeof(BaseProcess).GetMethod("LateUpdate",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic).Invoke(f.process,null);
    foreach(var motion in ik)foreach(var pair in motion.ikTargetPairs)foreach(var t in new[]{pair.effector.target,pair.bend.bendGoal}){var p=t.GetComponent<BaseProcess>();if(p!=null&&p.enabled)typeof(BaseProcess).GetMethod("LateUpdate",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic).Invoke(p,null);}
    for(int role=0;role<2;role++){
     var solver=ik[role].ik.solver;var targets=new List<HSolveTarget>();foreach(var e in solver.effectors)targets.Add(new HSolveTarget{pos=e.target==null?e.position:e.target.position,rot=e.target==null?e.rotation:e.target.rotation,positionWeight=e.positionWeight,rotationWeight=e.rotationWeight});
     var bends=new List<HSolveBend>();foreach(var pair in ik[role].ikTargetPairs)bends.Add(new HSolveBend{pos=pair.bend.bendGoal.position,weight=pair.bend.weight});
     var input=HSkeleton(roles[role]);bool solverEnabled=ik[role].enabled;HChain[] readChains=null;HTransform[] afterRead=null;
     if(automatic&&solverEnabled)HRecordLimbOrtho(solver);
     if(automatic&&solverEnabled)foreach(var chain in solver.chain)if(chain.nodes.Length==3){var local=chain.bendConstraint.defaultLocalDirection;float clamp=chain.bendConstraint.clampF;var normal=chain.nodes[0].transform.rotation*-local;var from=chain.nodes[2].transform.position-chain.nodes[1].transform.position;bool changed;var result=RootMotion.V3Tools.ClampDirection(from,normal,clamp,0,out changed);float angle=Vector3.Angle(normal,from);float ratio=1f-angle/180f;float t=Mathf.Clamp(ratio/clamp,0f,1f)*Mathf.Clamp(1f-(clamp-ratio)/(1f-ratio),0f,1f);HVectorRows.Add(new HVectorMath{from=from,normal=normal,result=result,clamp=clamp,changed=changed,angle=angle,t=t,slerp=Vector3.Slerp(normal,from,t)});}
     RootMotion.FinalIK.IKSolver.UpdateDelegate capture=delegate{readChains=HChains(solver);afterRead=HSkeleton(roles[role]);};
     if(solverEnabled){solver.OnPreSolve+=capture;try{HTriangleCapture=automatic;solver.Update();}finally{HTriangleCapture=false;solver.OnPreSolve-=capture;}}
     frames.Add(new HSolveFrame{role=role,solverEnabled=solverEnabled,time=time,sampled=sampled[role],input=input,output=HSkeleton(roles[role]),targets=targets.ToArray(),bends=bends.ToArray(),chains=HChains(solver),readChains=readChains,afterRead=afterRead});
    }
    foreach(var motion in ik)foreach(var f in motion.frameCorrect.list)if(f.enabled&&(int)f.type==2)typeof(BaseProcess).GetMethod("LateUpdate",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic).Invoke(f.process,null);
    for(int role=0;role<2;role++)frames[frames.Count-2+role].afterCorrection=HSkeleton(roles[role]);
   }
   sequences.Add(new HSolveSequence{motion=prefix,state=state,nonuniformScale=warped,initial=initial,frames=frames.ToArray()});
   }
  }
  if(automatic){File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".pair-vector-math.json",ProbeJson(HVectorRows.ToArray()));File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".pair-triangle-math.json",ProbeJson(HTriangleRows.ToArray()));File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".pair-ortho-math.json",ProbeJson(HOrthoRows.ToArray()));}
  File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".pair-solver.json",ProbeJson(new HSolveDump{scope="Compiled persistent solver and complete correction/contact outputs; animation sampling inputs recorded separately",automaticInitialization=automatic,initialBends=automatic?new[]{HInitBends[ik[0].ik.solver],HInitBends[ik[1].ik.solver]}:null,initializationCounts=automatic?new[]{HInitCount[ik[0].ik.solver],HInitCount[ik[1].ik.solver]}:null,firstInitialization=automatic?new[]{HFirstInit[ik[0].ik.solver],HFirstInit[ik[1].ik.solver]}:null,latestInitialization=automatic?new[]{HLatestInit[ik[0].ik.solver],HLatestInit[ik[1].ik.solver]}:null,sequences=sequences.ToArray()}));
 }
}
