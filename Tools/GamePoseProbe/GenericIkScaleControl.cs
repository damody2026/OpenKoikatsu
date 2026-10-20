using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using ChaLoad;

public sealed partial class GamePoseProbe {
 private static void RunGenericIkScaleControl() {
  var prefab=CommonLib.LoadAsset<GameObject>("chara/oo_base.unity3d","p_cf_body_bone",false);
  if(prefab==null)throw new Exception("Native numerical rig prefab missing");
  var sequences=new List<HSolveSequence>();
  foreach(var scales in new[]{new[]{0.92f,0.92f},new[]{0.504f,1.8f},new[]{1.8f,0.504f}}) {
   var objects=new GameObject[2];var roots=new Transform[2];
   var solvers=new RootMotion.FinalIK.IKSolverFullBodyBiped[2];
   var initial=new HTransform[2][];var transforms=new Transform[2][];
   var positions=new Vector3[2][];var rotations=new Quaternion[2][];
   try {
    for(int role=0;role<2;role++) {
     objects[role]=UnityEngine.Object.Instantiate(prefab);
     foreach(var renderer in objects[role].GetComponentsInChildren<Renderer>(true))renderer.enabled=false;
     foreach(var behaviour in objects[role].GetComponentsInChildren<Behaviour>(true))behaviour.enabled=false;
     var prep=objects[role].GetComponent<ChaPreparationBodyBone>();
     if(prep==null||prep.ObjBoneRoot==null)throw new Exception("Native bone root missing");
     roots[role]=prep.ObjBoneRoot.transform;
     foreach(var t in objects[role].GetComponentsInChildren<Transform>(true))if(t.name=="cf_n_height")t.localScale=Vector3.one*scales[role];
     var ik=objects[role].GetComponentInChildren<RootMotion.FinalIK.FullBodyBipedIK>(true);
     if(ik==null)throw new Exception("Native FullBodyBipedIK prefab component missing");
     solvers[role]=ik.solver;solvers[role].Initiate(ik.transform);
     initial[role]=HSkeleton(roots[role]);
     transforms[role]=roots[role].GetComponentsInChildren<Transform>(true);
     positions[role]=new Vector3[transforms[role].Length];rotations[role]=new Quaternion[transforms[role].Length];
     for(int i=0;i<transforms[role].Length;i++){positions[role][i]=transforms[role][i].localPosition;rotations[role][i]=transforms[role][i].localRotation;}
    }
    var frames=new List<HSolveFrame>();
    for(int frame=0;frame<8;frame++)for(int role=0;role<2;role++) {
     for(int i=0;i<transforms[role].Length;i++){transforms[role][i].localPosition=positions[role][i];transforms[role][i].localRotation=rotations[role][i];}
     var solver=solvers[role];var targets=new List<HSolveTarget>();
     float step=frame/7f;float peerScale=scales[1-role];
     foreach(var e in solver.effectors) {
      e.target=null;e.position=e.bone.position;e.rotation=e.bone.rotation;
      bool end=e.isEndEffector;
      if(Environment.GetEnvironmentVariable("OK_PROBE_GENERIC_IK_HANDS_ONLY")=="1")end=end&&(e==solver.leftHandEffector||e==solver.rightHandEffector);
      e.positionWeight=end?1f:0f;e.rotationWeight=0f;
      if(end)e.position+=new Vector3(0.12f*step,0.08f*step,0.18f*step)*peerScale;
      targets.Add(new HSolveTarget{pos=e.position,rot=e.rotation,positionWeight=e.positionWeight,rotationWeight=e.rotationWeight});
     }
     var bends=new List<HSolveBend>();
     for(int i=1;i<solver.chain.Length;i++){solver.chain[i].bendConstraint.bendGoal=null;solver.chain[i].bendConstraint.weight=0f;bends.Add(new HSolveBend{pos=Vector3.zero,weight=0f});}
     var input=HSkeleton(roots[role]);HChain[] readChains=null;
     RootMotion.FinalIK.IKSolver.UpdateDelegate capture=delegate{readChains=HChains(solver);};
     solver.OnPreSolve+=capture;try{solver.Update();}finally{solver.OnPreSolve-=capture;}
     frames.Add(new HSolveFrame{role=role,time=step,solverEnabled=true,input=input,output=HSkeleton(roots[role]),targets=targets.ToArray(),bends=bends.ToArray(),chains=HChains(solver),readChains=readChains});
    }
    sequences.Add(new HSolveSequence{motion="generic-scale-"+scales[0]+"-"+scales[1],state="GenericReach",initial=initial,frames=frames.ToArray()});
   } finally {foreach(var obj in objects)if(obj!=null)UnityEngine.Object.Destroy(obj);}
  }
  File.WriteAllText(Environment.GetEnvironmentVariable("OK_GAME_POSE_OUTPUT")+".pair-solver.json",ProbeJson(new HSolveDump{
   scope="Generic arm/leg numerical reach on render-disabled native bone prefab; no character card or scene action",automaticInitialization=false,sequences=sequences.ToArray()}));
 }
}
