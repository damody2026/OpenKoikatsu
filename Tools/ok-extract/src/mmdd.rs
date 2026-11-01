//! MMD Director (`mmddirector.py`, `vmdlib.py`) & `KKVMDPlayExtSave` compatibility for OK Studio.
//!
//! Parses MMDD v1 (`-mmddirector` scene folders), MMDD v2 (`mmddirectorData` + `mmddirectorExtData`),
//! and `KKVMDPlayExtSave` XML, resolves `.vmd` and audio files, solves character VMD animations
//! (including `[PrepareBoneBeforeCreateDummyBone]`, `[BonePosition]`, `[BoneRotAxisAdjust]`,
//! `autoBoneAxisAdjust`, `MMDIKSolverLeg`, `MMDIKSolverNoLimit`, track bones, and twist disperse)
//! into `.okanim` (`OKANIM02`) files, and evaluates VMD camera keyframes into Studio camera tracks.

use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};
use std::sync::{Mutex, OnceLock};

use serde::{Deserialize, Serialize};

use crate::finalik::{
    qdot, qfrom_to_rotation as qfrom_to, qinv, qmul, qnorm, qrot, qslerp as quat_slerp, vadd, vlen,
    vmul as vscale, vsub, Quat, V3,
};
use crate::okanim::convert::{family_tracks, FAMILY};
use crate::okanim::OkAnim;
use crate::pose::{euler_deg_to_quat, mat_pos, quat_mul, quat_to_unity_euler, Skeleton};
use crate::studio_scene::{Object, Scene};
use crate::vmd::{
    mmd_cubic_bezier, VmdBoneKeyFrame, VmdFile, VmdIkVisibleKeyFrame, VmdMorphKeyFrame,
};
use crate::vnge::{parse_py_or_json, unwrap_scene_decoder, VngeSceneData};

/// Default MMDD 2.0 character bone definition (`BepInEx/plugins/Console/Lib/mmdd/chara_default_2.0.ini`,
/// with `AxisScale=0.079` matching `mmddirector.ini` / `MotionController.DEFAULT_AXIS_SCALE`).
pub const DEFAULT_CHARA_INI: &str = r#"
[General]
Description=Default Char Bone 2.0 (TDA spec)
Type=chara
HeightBone=cf_n_height
AutoRotAdj=0
DefWeightSyncMode=global
AxisScale=0.079

[BoneDefine]
ROOT=:全ての親:rp
CENTER=ROOT:センター:rp
GROOVE=CENTER:グルーブ:rp
BACK_HIPS=GROOVE:腰:r
HIPS=BACK_HIPS:体の重心:rp
SPINE1=HIPS:上半身:r
SPINE2=SPINE1:上半身2:r
SPINE3=SPINE2:上半身3:
NECK=SPINE3:首:r
HEAD=NECK:頭:r
WAIST=HIPS:下半身:r
SHOULDER_P_R=SPINE3:右肩P:r
SHOULDER_R=SHOULDER_P_R:右肩:r
SHOULDER_C_R=SHOULDER_R:右肩C:t:tr,-1,SHOULDER_P_R
UPARM_R=SHOULDER_C_R:右腕:r
UPARM_TWIST_R=UPARM_R:右腕捩:rl:l,0.7941262,-0.6076475,-0.01131865,1,0,0
LOWARM_R=UPARM_TWIST_R:右ひじ:r
LOWARM_TWIST_R=LOWARM_R:右手捩:rl:l,0.7962407,-0.6047074,-0.01815981,1,0,0
HAND_R=LOWARM_TWIST_R:右手首:r
HAND_DUMMY_R=HAND_R:右ダミー:rp
SHOULDER_P_L=SPINE3:左肩P:r
SHOULDER_L=SHOULDER_P_L:左肩:r
SHOULDER_C_L=SHOULDER_L:左肩C:t:tr,-1,SHOULDER_P_L
UPARM_L=SHOULDER_C_L:左腕:r
UPARM_TWIST_L=UPARM_L:左腕捩:rl:l,-0.7941262,-0.6076475,-0.01131865,-1,0,0
LOWARM_L=UPARM_TWIST_L:左ひじ:r
LOWARM_TWIST_L=LOWARM_L:左手捩:rl:l,-0.7962407,-0.6047074,-0.01815981,-1,0,0
HAND_L=LOWARM_TWIST_L:左手首:r
HAND_DUMMY_L=HAND_L:左ダミー:rp
BACK_HIPS_C_R=WAIST:腰キャンセル右:t:tr,-1,BACK_HIPS
UPLEG_R=BACK_HIPS_C_R:右足:r
LOWLEG_R=UPLEG_R:右ひざ:r
FOOT_R=LOWLEG_R:右足首:r
TOE_R=FOOT_R:右つま先:
BACK_HIPS_C_L=WAIST:腰キャンセル左:t:tr,-1,BACK_HIPS
UPLEG_L=BACK_HIPS_C_L:左足:r
LOWLEG_L=UPLEG_L:左ひざ:r
FOOT_L=LOWLEG_L:左足首:r
TOE_L=FOOT_L:左つま先:
FIG_THUMB01_R=HAND_R:右親指０:r
FIG_THUMB02_R=FIG_THUMB01_R:右親指１:r
FIG_THUMB03_R=FIG_THUMB02_R:右親指２:r
FIG_INDEX01_R=HAND_R:右人指１:r
FIG_INDEX02_R=FIG_INDEX01_R:右人指２:r
FIG_INDEX03_R=FIG_INDEX02_R:右人指３:r
FIG_MIDDLE01_R=HAND_R:右中指１:r
FIG_MIDDLE02_R=FIG_MIDDLE01_R:右中指２:r
FIG_MIDDLE03_R=FIG_MIDDLE02_R:右中指３:r
FIG_RING01_R=HAND_R:右薬指１:r
FIG_RING02_R=FIG_RING01_R:右薬指２:r
FIG_RING03_R=FIG_RING02_R:右薬指３:r
FIG_LITTLE01_R=HAND_R:右小指１:r
FIG_LITTLE02_R=FIG_LITTLE01_R:右小指２:r
FIG_LITTLE03_R=FIG_LITTLE02_R:右小指３:r
FIG_THUMB01_L=HAND_L:左親指０:r
FIG_THUMB02_L=FIG_THUMB01_L:左親指１:r
FIG_THUMB03_L=FIG_THUMB02_L:左親指２:r
FIG_INDEX01_L=HAND_L:左人指１:r
FIG_INDEX02_L=FIG_INDEX01_L:左人指２:r
FIG_INDEX03_L=FIG_INDEX02_L:左人指３:r
FIG_MIDDLE01_L=HAND_L:左中指１:r
FIG_MIDDLE02_L=FIG_MIDDLE01_L:左中指２:r
FIG_MIDDLE03_L=FIG_MIDDLE02_L:左中指３:r
FIG_RING01_L=HAND_L:左薬指１:r
FIG_RING02_L=FIG_RING01_L:左薬指２:r
FIG_RING03_L=FIG_RING02_L:左薬指３:r
FIG_LITTLE01_L=HAND_L:左小指１:r
FIG_LITTLE02_L=FIG_LITTLE01_L:左小指２:r
FIG_LITTLE03_L=FIG_LITTLE02_L:左小指３:r
EYES=HEAD:両目:r
EYE_R=HEAD:右目:rt:tr,1,EYES
EYE_L=HEAD:左目:rt:tr,1,EYES
IK_ROOT_R=ROOT:右足IK親:rp
IK_FOOT_R=IK_ROOT_R:右足ＩＫ:rpi:i,FOOT_R,LOWLEG_R,UPLEG_R
IK_TOE_R=IK_FOOT_R:右つま先ＩＫ:rpi:i,TOE_R,FOOT_R
IK_ROOT_L=ROOT:左足IK親:rp
IK_FOOT_L=IK_ROOT_L:左足ＩＫ:rpi:i,FOOT_L,LOWLEG_L,UPLEG_L
IK_TOE_L=IK_FOOT_L:左つま先ＩＫ:rpi:i,TOE_L,FOOT_L
UPLEG_D_R=BACK_HIPS_C_R:右足D:rt:tr,1,UPLEG_R
LOWLEG_D_R=UPLEG_D_R:右ひざD:rt:tr,1,LOWLEG_R
FOOT_D_R=LOWLEG_D_R:右足首D:rt:tr,1,FOOT_R
HEEL_EX_R=FOOT_D_R:右踵EX:r
TOE_EX_R=HEEL_EX_R:右足先EX:r
UPLEG_D_L=BACK_HIPS_C_L:左足D:rt:tr,1,UPLEG_L
LOWLEG_D_L=UPLEG_D_L:左ひざD:rt:tr,1,LOWLEG_L
FOOT_D_L=LOWLEG_D_L:左足首D:rt:tr,1,FOOT_L
HEEL_EX_L=FOOT_D_L:左踵EX:r
TOE_EX_L=HEEL_EX_L:左足先EX:r
MOUTH_BASE=HEAD:口ベース:rp
EYEBROW_BASE=HEAD:眉ベース:rp

[BonePosition]
ROOT=b:cf_j_root
CENTER=bbs:cf_j_root:cf_j_hips:0.6
GROOVE=bbs:cf_j_root:cf_j_hips:0.61
BACK_HIPS=bbs:cf_j_siri_L_01:cf_j_siri_R_01:0.5
HIPS=b:cf_j_hips
SPINE1=b:cf_j_spine01
SPINE2=b:cf_j_spine02
SPINE3=b:cf_j_spine03
NECK=b:cf_j_neck
HEAD=b:cf_j_head
WAIST=b:cf_j_waist01
SHOULDER_P_R=b:cf_j_shoulder_R
SHOULDER_R=b:cf_j_shoulder_R
SHOULDER_C_R=b:cf_j_arm00_R
UPARM_R=b:cf_j_arm00_R
UPARM_TWIST_R=bbs:cf_j_arm00_R:cf_j_forearm01_R:0.5
LOWARM_R=b:cf_j_forearm01_R
LOWARM_TWIST_R=bbs:cf_j_forearm01_R:cf_j_hand_R:0.5
HAND_R=b:cf_j_hand_R
HAND_DUMMY_R=bo:cf_j_hand_R:0,0,0
SHOULDER_P_L=b:cf_j_shoulder_L
SHOULDER_L=b:cf_j_shoulder_L
SHOULDER_C_L=b:cf_j_arm00_L
UPARM_L=b:cf_j_arm00_L
UPARM_TWIST_L=bbs:cf_j_arm00_L:cf_j_forearm01_L:0.5
LOWARM_L=b:cf_j_forearm01_L
LOWARM_TWIST_L=bbs:cf_j_forearm01_L:cf_j_hand_L:0.5
HAND_L=b:cf_j_hand_L
HAND_DUMMY_L=bo:cf_j_hand_L:0,0,0
BACK_HIPS_C_R=b:cf_j_thigh00_R
UPLEG_R=b:cf_j_thigh00_R
LOWLEG_R=b:cf_j_leg01_R
FOOT_R=b:cf_j_leg03_R
TOE_R=bo:cf_j_toes_R:0,0,0
BACK_HIPS_C_L=b:cf_j_thigh00_L
UPLEG_L=b:cf_j_thigh00_L
LOWLEG_L=b:cf_j_leg01_L
FOOT_L=b:cf_j_leg03_L
TOE_L=bo:cf_j_toes_L:0,0,0
FIG_THUMB01_R=b:cf_j_thumb01_R
FIG_THUMB02_R=b:cf_j_thumb02_R
FIG_THUMB03_R=b:cf_j_thumb03_R
FIG_INDEX01_R=b:cf_j_index01_R
FIG_INDEX02_R=b:cf_j_index02_R
FIG_INDEX03_R=b:cf_j_index03_R
FIG_MIDDLE01_R=b:cf_j_middle01_R
FIG_MIDDLE02_R=b:cf_j_middle02_R
FIG_MIDDLE03_R=b:cf_j_middle03_R
FIG_RING01_R=b:cf_j_ring01_R
FIG_RING02_R=b:cf_j_ring02_R
FIG_RING03_R=b:cf_j_ring03_R
FIG_LITTLE01_R=b:cf_j_little01_R
FIG_LITTLE02_R=b:cf_j_little02_R
FIG_LITTLE03_R=b:cf_j_little03_R
FIG_THUMB01_L=b:cf_j_thumb01_L
FIG_THUMB02_L=b:cf_j_thumb02_L
FIG_THUMB03_L=b:cf_j_thumb03_L
FIG_INDEX01_L=b:cf_j_index01_L
FIG_INDEX02_L=b:cf_j_index02_L
FIG_INDEX03_L=b:cf_j_index03_L
FIG_MIDDLE01_L=b:cf_j_middle01_L
FIG_MIDDLE02_L=b:cf_j_middle02_L
FIG_MIDDLE03_L=b:cf_j_middle03_L
FIG_RING01_L=b:cf_j_ring01_L
FIG_RING02_L=b:cf_j_ring02_L
FIG_RING03_L=b:cf_j_ring03_L
FIG_LITTLE01_L=b:cf_j_little01_L
FIG_LITTLE02_L=b:cf_j_little02_L
FIG_LITTLE03_L=b:cf_j_little03_L
EYES=bbs:cf_J_hitomi_tx_R:cf_J_hitomi_tx_L:0.5
EYE_R=b:cf_J_hitomi_tx_R
EYE_L=b:cf_J_hitomi_tx_L
IK_ROOT_R=bo:cf_j_leg03_R:0,-0.08,0
IK_FOOT_R=b:cf_j_leg03_R
IK_TOE_R=bbs:cf_j_leg03_R:cf_j_toes_R:1.4
IK_ROOT_L=bo:cf_j_leg03_L:0,-0.08,0
IK_FOOT_L=b:cf_j_leg03_L
IK_TOE_L=bbs:cf_j_leg03_L:cf_j_toes_L:1.4
UPLEG_D_R=b:cf_j_thigh00_R
LOWLEG_D_R=b:cf_j_leg01_R
FOOT_D_R=b:cf_j_leg03_R
HEEL_EX_R=b:cf_j_foot_R
TOE_EX_R=b:cf_j_toes_R
UPLEG_D_L=b:cf_j_thigh00_L
LOWLEG_D_L=b:cf_j_leg01_L
FOOT_D_L=b:cf_j_leg03_L
HEEL_EX_L=b:cf_j_foot_L
TOE_EX_L=b:cf_j_toes_L
MOUTH_BASE=b:cf_J_MouthBase_ty
EYEBROW_BASE=b:cf_J_Mayu_ty

[BoneWeight]
ROOT=cf_j_root
HIPS=cf_j_hips
SPINE1=cf_j_spine01
SPINE2=cf_j_spine02
SPINE3=cf_j_spine03
NECK=cf_j_neck
HEAD=cf_j_head
WAIST=cf_j_waist01
SHOULDER_R=cf_j_shoulder_R
UPARM_R=cf_j_arm00_R
LOWARM_R=cf_j_forearm01_R
HAND_R=cf_j_hand_R
SHOULDER_L=cf_j_shoulder_L
UPARM_L=cf_j_arm00_L
LOWARM_L=cf_j_forearm01_L
HAND_L=cf_j_hand_L
UPLEG_D_R=cf_j_thigh00_R
LOWLEG_D_R=cf_j_leg01_R
FOOT_D_R=cf_j_leg03_R
HEEL_EX_R=cf_j_foot_R
TOE_EX_R=cf_j_toes_R
UPLEG_D_L=cf_j_thigh00_L
LOWLEG_D_L=cf_j_leg01_L
FOOT_D_L=cf_j_leg03_L
HEEL_EX_L=cf_j_foot_L
TOE_EX_L=cf_j_toes_L
FIG_THUMB01_R=cf_j_thumb01_R
FIG_THUMB02_R=cf_j_thumb02_R
FIG_THUMB03_R=cf_j_thumb03_R
FIG_INDEX01_R=cf_j_index01_R
FIG_INDEX02_R=cf_j_index02_R
FIG_INDEX03_R=cf_j_index03_R
FIG_MIDDLE01_R=cf_j_middle01_R
FIG_MIDDLE02_R=cf_j_middle02_R
FIG_MIDDLE03_R=cf_j_middle03_R
FIG_RING01_R=cf_j_ring01_R
FIG_RING02_R=cf_j_ring02_R
FIG_RING03_R=cf_j_ring03_R
FIG_LITTLE01_R=cf_j_little01_R
FIG_LITTLE02_R=cf_j_little02_R
FIG_LITTLE03_R=cf_j_little03_R
FIG_THUMB01_L=cf_j_thumb01_L
FIG_THUMB02_L=cf_j_thumb02_L
FIG_THUMB03_L=cf_j_thumb03_L
FIG_INDEX01_L=cf_j_index01_L
FIG_INDEX02_L=cf_j_index02_L
FIG_INDEX03_L=cf_j_index03_L
FIG_MIDDLE01_L=cf_j_middle01_L
FIG_MIDDLE02_L=cf_j_middle02_L
FIG_MIDDLE03_L=cf_j_middle03_L
FIG_RING01_L=cf_j_ring01_L
FIG_RING02_L=cf_j_ring02_L
FIG_RING03_L=cf_j_ring03_L
FIG_LITTLE01_L=cf_j_little01_L
FIG_LITTLE02_L=cf_j_little02_L
FIG_LITTLE03_L=cf_j_little03_L
EYE_R=cf_J_hitomi_tx_R
EYE_L=cf_J_hitomi_tx_L
MOUTH_BASE=cf_J_MouthBase_ty
EYEBROW_BASE=cf_J_Mayu_ty

[BoneIKSetOverview]
IK_SET_NUMBER=2

[BoneIKSet_1]
NAME=FOOT IK
ENABLE=1
IKBONE=IK_FOOT_R,IK_FOOT_L
SOLVER=MMDIKSolverLeg
ITERATIONS=6
STEP_ANGLE=114.5916
MIN_KNEE_ROT=1

[BoneIKSet_2]
NAME=TOE IK
ENABLE=1
IKBONE=IK_TOE_R,IK_TOE_L
SOLVER=MMDIKSolverNoLimit
ITERATIONS=3
STEP_ANGLE=229.1831

[BoneTwistDisperse]
ARMTWIST_R=ArmTwist:uparm_twist_r:uparm_r:0.6
ARMTWIST_L=ArmTwist:uparm_twist_l:uparm_l:0.6
HANDTWIST_R=HandTwist:lowarm_twist_r:lowarm_r:0.4
HANDTWIST_L=HandTwist:lowarm_twist_l:lowarm_l:0.4

[PrepareBoneBeforeCreateDummyBone]
SETTING_ENABLE_GET=0
SETTING_ENABLE_SET=1
SETTING_SET_LISTED_ONLY=0
cf_j_arm00_R=(0.000,2.426,0.000)
cf_j_arm00_L=(0.000,357.479,0.000)
cf_j_forearm01_R=(0.005,357.572,-0.001)
cf_j_forearm01_L=(0.000,2.519,0.000)
cf_j_thumb01_R=(280.000,90.000,235.000)
cf_j_index01_R=(356.450,174.671,184.991)
cf_j_middle01_R=(2.989,180.262,185.007)
cf_j_ring01_R=(7.621,184.684,185.032)
cf_j_little01_R=(15.244,189.383,185.132)
cf_j_thumb01_L=(80.000,90.000,55.000)
cf_j_index01_L=(3.550,5.329,4.991)
cf_j_middle01_L=(357.011,359.738,5.007)
cf_j_ring01_L=(352.379,355.316,5.032)
cf_j_little01_L=(344.756,350.617,5.132)
cf_J_hitomi_tx_L=(1.363,2.174,7.074)
cf_j_thigh00_R=(1.000,0.000,0.000)
cf_j_thigh00_L=(1.000,0.001,0.000)
cf_j_leg01_R=(1.000,0.000,0.000)
cf_j_leg01_L=(1.000,0.000,0.000)

[BoneRotAxisAdjust]
SHOULDER_R=r,0,0,-10
SHOULDER_L=r,0,0,10
UPARM_R=r,0,0,-32
UPARM_L=r,0,0,32
FIG_THUMB01_R=r,-80,90,-125
FIG_THUMB01_L=r,80,90,55
FIG_THUMB02_R=r,0,0,10
FIG_THUMB02_L=r,0,0,10
FIG_INDEX01_R=r,0,180,180
FIG_MIDDLE01_R=r,0,180,180
FIG_RING01_R=r,0,180,180
FIG_LITTLE01_R=r,0,180,180
EYES=s,1.5
EYE_R=r,11,9,5,s,1.5
EYE_L=r,11,-7,-2,s,1.5
"#;

// ---------------------------------------------------------------------------
// Data structures serialized into `scene.json["mmdd"]`
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct MmddSceneData {
    pub version: i32,
    pub total_frame_len: f32,
    pub fps: f32,
    pub loop_play: bool,
    pub motions: Vec<MmddMotionTrack>,
    pub morphs: Vec<MmddMorphTrack>,
    pub cameras: Vec<MmddCameraTrack>,
    pub sounds: Vec<MmddSoundTrack>,
    pub clips: Vec<MmddClipTrack>,
    pub binds: Vec<serde_json::Value>,
    pub parents: Vec<serde_json::Value>,
    pub follow_cameras: Vec<serde_json::Value>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct MmddMotionTrack {
    pub char_key: i32,
    pub alias: String,
    pub source: String,
    pub vmd_file: String,
    pub resolved_vmd: String,
    pub okanim: String,
    pub delay: f32,
    pub playback_speed: f32,
    pub char_scale: f32,
    pub frames: u32,
    pub duration: f32,
    pub looping: bool,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct MmddMorphTrack {
    pub char_key: i32,
    pub alias: String,
    pub vmd_file: String,
    pub resolved_vmd: String,
    pub delay: f32,
    pub playback_speed: f32,
    pub morph_count: usize,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct MmddCameraKeyEval {
    pub time: f32,
    pub frame_no: u32,
    /// Camera target pivot in Unity world space (`cpos`).
    pub target_pos: [f32; 3],
    /// Camera distance vector (`(0, 0, dist * dstScale)`).
    pub distance: [f32; 3],
    /// Actual camera world position (`rotQ * distance + target_pos`).
    pub world_pos: [f32; 3],
    /// Unity Euler angles in degrees (`crot`).
    pub rot: [f32; 3],
    pub fov: f32,
    pub x_curve: [f32; 4],
    pub y_curve: [f32; 4],
    pub z_curve: [f32; 4],
    pub r_curve: [f32; 4],
    pub d_curve: [f32; 4],
    pub v_curve: [f32; 4],
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct MmddCameraTrack {
    pub enable: bool,
    pub vmd_file: String,
    pub resolved_vmd: String,
    pub delay: f32,
    pub playback_speed: f32,
    pub pos_scale: f32,
    pub dst_scale: f32,
    pub off_pos: [f32; 3],
    pub off_rot: [f32; 3],
    pub keyframes: Vec<MmddCameraKeyEval>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct MmddSoundTrack {
    pub enable: bool,
    pub sound_file: String,
    pub resolved_sound: String,
    pub delay: f32,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct MmddClipTrack {
    pub enable: bool,
    pub clip_name: String,
    pub start_time: f32,
    pub end_time: f32,
    pub loop_clip: bool,
    pub speed: f32,
}

// ---------------------------------------------------------------------------
// File Resolver for VMD & Audio paths
// ---------------------------------------------------------------------------

static ACTION_FILE_INDEX: OnceLock<Mutex<Option<HashMap<String, Vec<PathBuf>>>>> = OnceLock::new();

fn build_file_index() -> HashMap<String, Vec<PathBuf>> {
    let mut map: HashMap<String, Vec<PathBuf>> = HashMap::new();
    let mut roots = vec![
        PathBuf::from(r"E:\02_Mocap_Animation\action"),
        PathBuf::from(r"E:\action"),
        PathBuf::from(r"E:\Koikatu\UserData"),
        PathBuf::from(r"E:\Koikatu\BepInEx\plugins"),
    ];
    if let Ok(extra) = std::env::var("OK_MMDD_SEARCH_DIR") {
        for part in extra.split(';').filter(|s| !s.trim().is_empty()) {
            roots.push(PathBuf::from(part.trim()));
        }
    }
    fn walk(dir: &Path, depth: usize, out: &mut HashMap<String, Vec<PathBuf>>) {
        if depth > 8 {
            return;
        }
        let Ok(rd) = std::fs::read_dir(dir) else {
            return;
        };
        for entry in rd.flatten() {
            let p = entry.path();
            if p.is_dir() {
                walk(&p, depth + 1, out);
            } else if let Some(ext) = p.extension().and_then(|e| e.to_str()) {
                if matches!(
                    ext.to_ascii_lowercase().as_str(),
                    "vmd" | "wav" | "ogg" | "mp3" | "ini"
                ) {
                    if let Some(name) = p.file_name().and_then(|n| n.to_str()) {
                        out.entry(name.to_lowercase()).or_default().push(p);
                    }
                }
            }
        }
    }
    for r in roots {
        if r.is_dir() {
            walk(&r, 0, &mut map);
        }
    }
    map
}

pub fn resolve_external_file(raw_path: &str, scene_path: Option<&Path>) -> Option<PathBuf> {
    let trimmed = raw_path.trim().trim_matches('"');
    if trimmed.is_empty() || trimmed.eq_ignore_ascii_case("InitPose") {
        return None;
    }
    let p = Path::new(trimmed);
    if p.is_file() {
        return Some(p.to_path_buf());
    }
    let norm = trimmed.replace('/', "\\");
    // Common relocated action directory: E:\action\... -> E:\02_Mocap_Animation\action\...
    for prefix in [r"E:\action\", r"e:\action\", r"D:\action\", r"C:\action\"] {
        if let Some(rest) = norm.strip_prefix(prefix) {
            let candidate = Path::new(r"E:\02_Mocap_Animation\action").join(rest);
            if candidate.is_file() {
                return Some(candidate);
            }
        }
    }
    if let Some(idx) = norm.to_ascii_lowercase().find(r"\action\") {
        let rest = &norm[idx + r"\action\".len()..];
        let candidate = Path::new(r"E:\02_Mocap_Animation\action").join(rest);
        if candidate.is_file() {
            return Some(candidate);
        }
    }
    if let Some(sp) = scene_path {
        if let Some(parent) = sp.parent() {
            if let Some(fname) = p.file_name() {
                let beside = parent.join(fname);
                if beside.is_file() {
                    return Some(beside);
                }
            }
        }
    }
    if let Some(fname) = p.file_name().and_then(|n| n.to_str()) {
        let lock = ACTION_FILE_INDEX.get_or_init(|| Mutex::new(None));
        let mut guard = lock.lock().unwrap();
        let index = guard.get_or_insert_with(build_file_index);
        if let Some(candidates) = index.get(&fname.to_lowercase()) {
            if candidates.len() == 1 {
                return Some(candidates[0].clone());
            }
            // Extract directory parts of raw_path (nearest parent first)
            let parts: Vec<String> = norm
                .split('\\')
                .map(|s| s.trim().to_lowercase())
                .filter(|s| !s.is_empty() && !s.ends_with(':'))
                .collect();
            let dir_parts: Vec<&str> = if parts.len() > 1 {
                parts[..parts.len() - 1]
                    .iter()
                    .rev()
                    .map(String::as_str)
                    .collect()
            } else {
                Vec::new()
            };
            let raw_parent = dir_parts.first().copied().unwrap_or("");
            let is_generic_dir = |d: &str| {
                matches!(
                    d,
                    "action" | "[vmd]" | "vmd" | "userdata" | "koikatu" | "studio" | "scene"
                )
            };
            let mut best_score = i32::MIN;
            let mut best_path: Option<&PathBuf> = None;
            for cand in candidates {
                let cand_str = cand.to_string_lossy().to_lowercase();
                let mut score = 0i32;
                if cand_str.contains("_整理报告") || cand_str.contains("backup") {
                    score -= 100;
                }
                let cand_parent = cand
                    .parent()
                    .and_then(|cp| cp.file_name())
                    .and_then(|n| n.to_str())
                    .map(|s| s.to_lowercase())
                    .unwrap_or_default();
                if !raw_parent.is_empty() && !is_generic_dir(raw_parent) {
                    if cand_parent == raw_parent {
                        score += 1000;
                    } else if raw_parent.len() >= 3
                        && (cand_parent.contains(raw_parent) || raw_parent.contains(&cand_parent))
                        && !cand_parent.is_empty()
                    {
                        score += 500;
                    }
                }
                for (depth, &dp) in dir_parts.iter().enumerate() {
                    if !is_generic_dir(dp) && dp.len() >= 2 && cand_str.contains(dp) {
                        score += (200i32 - (depth as i32) * 30).max(20);
                    }
                }
                if score > best_score {
                    best_score = score;
                    best_path = Some(cand);
                }
            }
            if let Some(chosen) = best_path {
                return Some(chosen.clone());
            }
        }
    }
    None
}

// ---------------------------------------------------------------------------
// Quaternion & Vector Helpers matching Unity / vmdlib.py
// ---------------------------------------------------------------------------

#[inline]
fn vdot(a: V3, b: V3) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

#[inline]
fn vnorm(v: V3) -> V3 {
    let l = vlen(v);
    if l > 1e-7 {
        [v[0] / l, v[1] / l, v[2] / l]
    } else {
        [0.0, 0.0, 0.0]
    }
}

#[inline]
fn quat_from_angle_axis_deg(angle_deg: f32, axis: V3) -> Quat {
    let n = vnorm(axis);
    let half = (angle_deg * 0.5).to_radians();
    let (s, c) = half.sin_cos();
    qnorm([n[0] * s, n[1] * s, n[2] * s, c])
}

#[inline]
fn quat_to_angle_axis_deg(q: Quat) -> (f32, V3) {
    let mut nq = qnorm(q);
    if nq[3] < 0.0 {
        nq = [-nq[0], -nq[1], -nq[2], -nq[3]];
    }
    let w = nq[3].clamp(-1.0, 1.0);
    let angle_rad = 2.0 * w.acos();
    let s = (1.0 - w * w).max(0.0).sqrt();
    if s < 1e-6 {
        (0.0, [1.0, 0.0, 0.0])
    } else {
        (
            angle_rad.to_degrees(),
            [nq[0] / s, nq[1] / s, nq[2] / s],
        )
    }
}

#[inline]
fn quat_lerp(a: Quat, mut b: Quat, t: f32) -> Quat {
    if qdot(a, b) < 0.0 {
        b = [-b[0], -b[1], -b[2], -b[3]];
    }
    qnorm([
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
        a[3] + (b[3] - a[3]) * t,
    ])
}

#[inline]
fn quat_slerp_unclamped(a: Quat, mut b: Quat, t: f32) -> Quat {
    let mut dot = qdot(a, b);
    if dot < 0.0 {
        dot = -dot;
        b = [-b[0], -b[1], -b[2], -b[3]];
    }
    if dot > 0.9995 {
        return quat_lerp(a, b, t);
    }
    let theta = dot.clamp(-1.0, 1.0).acos();
    let sin_theta = theta.sin();
    if sin_theta.abs() < 1e-6 {
        return a;
    }
    let w0 = ((1.0 - t) * theta).sin() / sin_theta;
    let w1 = (t * theta).sin() / sin_theta;
    qnorm([
        a[0] * w0 + b[0] * w1,
        a[1] * w0 + b[1] * w1,
        a[2] * w0 + b[2] * w1,
        a[3] * w0 + b[3] * w1,
    ])
}

// ---------------------------------------------------------------------------
// BoneAdjustment (matches vmdlib.py lines 1134-1290)
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
pub struct BoneAdjustment {
    pub name: String,
    pub rot_adj_auto: bool,
    pub rot_adjustment_vec: V3,
    pub rot_adjustment: Quat,
    pub rot_adjusted: bool,
    pub rot_axis_auto: bool,
    pub rot_axis_adjustment_vec: V3,
    pub rot_axis_adjustment: Quat,
    pub rot_axis_adjusted: bool,
    pub rotation_scale: f32,
    pub base_position: V3,
    pub position_offset: V3,
    pub position_global_scale: f32,
    pub position_local_scale: f32,
    pub position_scale: f32,
}

impl BoneAdjustment {
    pub fn new(name: &str, auto_rot_adj: bool) -> Self {
        Self {
            name: name.to_string(),
            rot_adj_auto: auto_rot_adj,
            rot_adjustment_vec: [0.0; 3],
            rot_adjustment: [0.0, 0.0, 0.0, 1.0],
            rot_adjusted: false,
            rot_axis_auto: true,
            rot_axis_adjustment_vec: [0.0; 3],
            rot_axis_adjustment: [0.0, 0.0, 0.0, 1.0],
            rot_axis_adjusted: false,
            rotation_scale: 1.0,
            base_position: [0.0; 3],
            position_offset: [0.0; 3],
            position_global_scale: 0.08,
            position_local_scale: 1.0,
            position_scale: 0.08,
        }
    }

    pub fn set_rot_adjustment(&mut self, rot_adj: V3) {
        self.rot_adjustment_vec = rot_adj;
        self.rot_adjustment = euler_deg_to_quat(rot_adj);
        self.rot_adjusted = self.rot_adjustment != [0.0, 0.0, 0.0, 1.0];
    }

    pub fn set_axis_adjustment(&mut self, axis_adj: V3) {
        self.rot_axis_adjustment_vec = axis_adj;
        self.rot_axis_adjustment = euler_deg_to_quat(axis_adj);
        self.rot_axis_adjusted = self.rot_axis_adjustment != [0.0, 0.0, 0.0, 1.0];
    }

    pub fn set_pos_global_scale(&mut self, global_scale: f32) {
        self.position_global_scale = global_scale;
        self.position_scale = self.position_global_scale * self.position_local_scale;
    }

    pub fn set_pos_local_scale(&mut self, local_scale: f32) {
        self.position_local_scale = local_scale;
        self.position_scale = self.position_global_scale * self.position_local_scale;
    }

    pub fn get_adjusted_rotation(&self, rot: Quat) -> Quat {
        let mut r = if rot != [0.0, 0.0, 0.0, 1.0] {
            let mut q = [-rot[0], rot[1], -rot[2], rot[3]];
            if self.rot_axis_adjusted {
                let adj = self.rot_axis_adjustment;
                q = quat_mul(quat_mul(adj, q), qinv(adj));
            }
            q
        } else {
            rot
        };
        if self.rot_adjusted {
            r = quat_mul(r, self.rot_adjustment);
        }
        if (self.rotation_scale - 1.0).abs() > 1e-6 {
            r = quat_slerp_unclamped([0.0, 0.0, 0.0, 1.0], r, self.rotation_scale);
        }
        r
    }

    pub fn get_adjusted_position(&self, pos: V3) -> V3 {
        let mut p = if pos != [0.0, 0.0, 0.0] {
            [
                -pos[0] * self.position_scale,
                pos[1] * self.position_scale,
                -pos[2] * self.position_scale,
            ]
        } else {
            pos
        };
        if self.position_offset != [0.0, 0.0, 0.0] {
            p = vadd(p, self.position_offset);
        }
        vadd(p, self.base_position)
    }
}

// ---------------------------------------------------------------------------
// INI Bone Definition Parser
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
pub enum BonePosSpec {
    Bone(String),
    Between(String, String, f32),
    Offset(String, V3),
    Reset(String, [Option<f32>; 3]),
    BonePosRot(String),
}

#[derive(Debug, Clone)]
pub struct BoneDefine {
    pub name: String,
    pub parent: Option<String>,
    pub vmd_bone: String,
    pub is_rotation: bool,
    pub is_position: bool,
    pub is_ik: bool,
    pub ik_data: Vec<String>,
    pub is_track: bool,
    pub track_ptn: String,
    pub track_scale: f32,
    pub track_src: String,
    pub is_limit_axis: bool,
    pub limit_ref_axis: V3,
    pub limit_axis: V3,
    pub pos_spec: BonePosSpec,
    pub weight_bone: Option<String>,
    pub weight_sync_local: bool,
}

#[derive(Debug, Clone)]
pub struct IkSetConfig {
    pub name: String,
    pub enable: bool,
    pub ik_bones: Vec<String>,
    pub solver: String,
    pub iterations: usize,
    pub step_angle: f32,
    pub min_knee_rot: f32,
}

#[derive(Debug, Clone)]
pub struct TwistDisperseConfig {
    pub name: String,
    pub group: String,
    pub twist_bone: String,
    pub disperse_bone: String,
    pub disperse_rate: f32,
    pub enable: bool,
}

#[derive(Debug, Clone)]
pub struct CharaIniConfig {
    pub height_bone: String,
    pub auto_rot_adj: bool,
    pub default_sync_local: bool,
    pub axis_scale: f32,
    pub bone_names: Vec<String>,
    pub bones: HashMap<String, BoneDefine>,
    pub vmd_to_bone: HashMap<String, String>,
    pub ik_sets: Vec<IkSetConfig>,
    pub twist_disperse: Vec<TwistDisperseConfig>,
    pub prepare_enable_set: bool,
    pub prepare_listed_only: bool,
    pub prepare_rotations: HashMap<String, Quat>,
    pub bone_adjusts: HashMap<String, BoneAdjustment>,
}

impl CharaIniConfig {
    pub fn parse(ini_text: &str) -> Result<Self, String> {
        let mut sections: HashMap<String, Vec<(String, String)>> = HashMap::new();
        let mut current = String::new();
        for raw_line in ini_text.lines() {
            let line = raw_line.trim();
            if line.is_empty() || line.starts_with(';') || line.starts_with('#') {
                continue;
            }
            if line.starts_with('[') && line.ends_with(']') {
                current = line[1..line.len() - 1].trim().to_ascii_lowercase();
                sections.entry(current.clone()).or_default();
                continue;
            }
            if let Some((k, v)) = line.split_once('=') {
                sections
                    .entry(current.clone())
                    .or_default()
                    .push((k.trim().to_ascii_lowercase(), v.trim().to_string()));
            }
        }

        let mut height_bone = "cf_n_height".to_string();
        let mut auto_rot_adj = false;
        let mut default_sync_local = false;
        // `vmdlib.py::_populateRuntimeState` always sets `self.axisScale = self.DEFAULT_AXIS_SCALE` (`0.079` from `mmddirector.ini`).
        let axis_scale = 0.079f32;
        if let Some(gen) = sections.get("general") {
            for (k, v) in gen {
                match k.as_str() {
                    "heightbone" => height_bone = v.clone(),
                    "autorotadj" => auto_rot_adj = v == "1" || v.eq_ignore_ascii_case("true"),
                    "defweightsyncmode" => {
                        default_sync_local = v.eq_ignore_ascii_case("local") || v.eq_ignore_ascii_case("l")
                    }
                    _ => {}
                }
            }
        }

        let mut bone_names = Vec::new();
        let mut bones = HashMap::new();
        let mut vmd_to_bone = HashMap::new();
        if let Some(defs) = sections.get("bonedefine") {
            for (bname, val) in defs {
                let parts: Vec<&str> = val.split(':').collect();
                if parts.len() < 3 {
                    continue;
                }
                let parent = if parts[0].trim().is_empty() {
                    None
                } else {
                    Some(parts[0].trim().to_ascii_lowercase())
                };
                let vmd_bone = parts[1].trim().to_string();
                let attrs = parts[2].trim().to_ascii_lowercase();
                let is_rotation = attrs.contains('r');
                let is_position = attrs.contains('p');
                let is_ik = attrs.contains('i');
                let is_track = attrs.contains('t');
                let is_limit_axis = attrs.contains('l');
                let mut nn = 3usize;
                let mut ik_data = Vec::new();
                if is_ik {
                    if let Some(s) = parts.get(nn) {
                        let items: Vec<&str> = s.split(',').map(str::trim).collect();
                        if items.first().copied() == Some("i") {
                            ik_data = items[1..].iter().map(|x| x.to_ascii_lowercase()).collect();
                        }
                    }
                    nn += 1;
                }
                let (mut track_ptn, mut track_scale, mut track_src) = (String::new(), 1.0f32, String::new());
                if is_track {
                    if let Some(s) = parts.get(nn) {
                        let items: Vec<&str> = s.split(',').map(str::trim).collect();
                        if items.len() == 3 && items[0].to_ascii_lowercase().starts_with('t') {
                            track_ptn = items[0][1..].to_ascii_lowercase();
                            track_scale = items[1].parse().unwrap_or(1.0);
                            track_src = items[2].to_ascii_lowercase();
                        }
                    }
                    nn += 1;
                }
                let (mut limit_ref_axis, mut limit_axis) = ([0.0; 3], [1.0, 0.0, 0.0]);
                if is_limit_axis {
                    if let Some(s) = parts.get(nn) {
                        let items: Vec<&str> = s.split(',').map(str::trim).collect();
                        if items.len() == 7 {
                            limit_ref_axis = [
                                items[1].parse().unwrap_or(0.0),
                                items[2].parse().unwrap_or(0.0),
                                items[3].parse().unwrap_or(0.0),
                            ];
                            limit_axis = [
                                items[4].parse().unwrap_or(1.0),
                                items[5].parse().unwrap_or(0.0),
                                items[6].parse().unwrap_or(0.0),
                            ];
                        }
                    }
                }
                bone_names.push(bname.clone());
                vmd_to_bone.insert(vmd_bone.clone(), bname.clone());
                bones.insert(
                    bname.clone(),
                    BoneDefine {
                        name: bname.clone(),
                        parent,
                        vmd_bone,
                        is_rotation,
                        is_position,
                        is_ik,
                        ik_data,
                        is_track,
                        track_ptn,
                        track_scale,
                        track_src,
                        is_limit_axis,
                        limit_ref_axis,
                        limit_axis,
                        pos_spec: BonePosSpec::Bone("cf_j_root".into()),
                        weight_bone: None,
                        weight_sync_local: default_sync_local,
                    },
                );
            }
        }

        if let Some(pos_list) = sections.get("boneposition") {
            for (bname, val) in pos_list {
                let Some(bd) = bones.get_mut(bname) else {
                    continue;
                };
                let pdata: Vec<&str> = val.split(':').map(str::trim).collect();
                match pdata.first().copied() {
                    Some("b") if pdata.len() == 2 => {
                        bd.pos_spec = BonePosSpec::Bone(pdata[1].to_string());
                    }
                    Some("bbs") if pdata.len() == 4 => {
                        bd.pos_spec = BonePosSpec::Between(
                            pdata[1].to_string(),
                            pdata[2].to_string(),
                            pdata[3].parse().unwrap_or(0.5),
                        );
                    }
                    Some("bo") if pdata.len() == 3 => {
                        let offs: Vec<f32> = pdata[2]
                            .split(',')
                            .map(|s| s.trim().parse().unwrap_or(0.0))
                            .collect();
                        if offs.len() == 3 {
                            bd.pos_spec =
                                BonePosSpec::Offset(pdata[1].to_string(), [offs[0], offs[1], offs[2]]);
                        }
                    }
                    Some("br") if pdata.len() == 3 => {
                        let rsts: Vec<Option<f32>> = pdata[2]
                            .split(',')
                            .map(|s| s.trim().parse::<f32>().ok().filter(|f| f.is_finite()))
                            .collect();
                        if rsts.len() == 3 {
                            bd.pos_spec =
                                BonePosSpec::Reset(pdata[1].to_string(), [rsts[0], rsts[1], rsts[2]]);
                        }
                    }
                    Some("bpr") if pdata.len() == 2 => {
                        bd.pos_spec = BonePosSpec::BonePosRot(pdata[1].to_string());
                    }
                    _ => {}
                }
            }
        }

        if let Some(weights) = sections.get("boneweight") {
            for (bname, val) in weights {
                let Some(bd) = bones.get_mut(bname) else {
                    continue;
                };
                let wdata: Vec<&str> = val.split(':').map(str::trim).collect();
                if let Some(&wb) = wdata.first() {
                    if !wb.is_empty() {
                        bd.weight_bone = Some(wb.to_string());
                    }
                }
                if let Some(&wm) = wdata.get(1) {
                    bd.weight_sync_local =
                        wm.eq_ignore_ascii_case("local") || wm.eq_ignore_ascii_case("l");
                }
            }
        }

        let mut ik_set_count = 0usize;
        if let Some(ov) = sections.get("boneiksetoverview") {
            for (k, v) in ov {
                if k == "ik_set_number" {
                    ik_set_count = v.parse().unwrap_or(0);
                }
            }
        }
        let mut ik_sets = Vec::new();
        for i in 1..=ik_set_count {
            let sec_name = format!("boneikset_{i}");
            if let Some(items) = sections.get(&sec_name) {
                let mut cfg = IkSetConfig {
                    name: String::new(),
                    enable: true,
                    ik_bones: Vec::new(),
                    solver: "MMDIKSolverLeg".into(),
                    iterations: 6,
                    step_angle: 114.5916,
                    min_knee_rot: 1.0,
                };
                for (k, v) in items {
                    match k.as_str() {
                        "name" => cfg.name = v.clone(),
                        "enable" => cfg.enable = v == "1" || v.eq_ignore_ascii_case("true"),
                        "ikbone" => {
                            cfg.ik_bones = v
                                .split(',')
                                .map(|s| s.trim().to_ascii_lowercase())
                                .collect()
                        }
                        "solver" => cfg.solver = v.clone(),
                        "iterations" => cfg.iterations = v.parse().unwrap_or(6),
                        "step_angle" => cfg.step_angle = v.parse().unwrap_or(114.5916),
                        "min_knee_rot" => cfg.min_knee_rot = v.parse().unwrap_or(1.0),
                        _ => {}
                    }
                }
                ik_sets.push(cfg);
            }
        }

        let mut twist_disperse = Vec::new();
        if let Some(td_sec) = sections.get("bonetwistdisperse") {
            for (k, v) in td_sec {
                let parts: Vec<&str> = v.split(':').map(str::trim).collect();
                if parts.len() == 4 {
                    let rate: f32 = parts[3].parse().unwrap_or(0.0);
                    twist_disperse.push(TwistDisperseConfig {
                        name: k.clone(),
                        group: parts[0].to_string(),
                        twist_bone: parts[1].to_ascii_lowercase(),
                        disperse_bone: parts[2].to_ascii_lowercase(),
                        disperse_rate: rate,
                        enable: rate > 0.0,
                    });
                }
            }
        }

        let mut prepare_enable_set = false;
        let mut prepare_listed_only = false;
        let mut prepare_rotations = HashMap::new();
        if let Some(prep) = sections.get("preparebonebeforecreatedummybone") {
            for (k, v) in prep {
                match k.as_str() {
                    "setting_enable_get" => {}
                    "setting_enable_set" => {
                        prepare_enable_set = v == "1" || v.eq_ignore_ascii_case("true")
                    }
                    "setting_set_listed_only" => {
                        prepare_listed_only = v == "1" || v.eq_ignore_ascii_case("true")
                    }
                    _ => {
                        let nums: Vec<f32> = v
                            .trim_matches(|c| c == '(' || c == ')')
                            .split(',')
                            .filter_map(|s| s.trim().parse().ok())
                            .collect();
                        if nums.len() == 3 {
                            prepare_rotations.insert(
                                k.to_ascii_lowercase(),
                                euler_deg_to_quat([nums[0], nums[1], nums[2]]),
                            );
                        }
                    }
                }
            }
        }

        let mut bone_adjusts: HashMap<String, BoneAdjustment> = bone_names
            .iter()
            .map(|n| (n.clone(), BoneAdjustment::new(n, auto_rot_adj)))
            .collect();
        if let Some(adjs) = sections.get("bonerotaxisadjust") {
            for (bname, val) in adjs {
                let Some(adj) = bone_adjusts.get_mut(bname) else {
                    continue;
                };
                let tokens: Vec<&str> = val.split(',').map(str::trim).collect();
                let mut i = 0;
                while i < tokens.len() {
                    match tokens[i].to_ascii_lowercase().as_str() {
                        "r" if i + 3 < tokens.len() => {
                            let x = tokens[i + 1].parse().unwrap_or(0.0);
                            let y = tokens[i + 2].parse().unwrap_or(0.0);
                            let z = tokens[i + 3].parse().unwrap_or(0.0);
                            adj.set_rot_adjustment([x, y, z]);
                            adj.rot_adj_auto = false;
                            i += 4;
                        }
                        "a" if i + 3 < tokens.len() => {
                            let x = tokens[i + 1].parse().unwrap_or(0.0);
                            let y = tokens[i + 2].parse().unwrap_or(0.0);
                            let z = tokens[i + 3].parse().unwrap_or(0.0);
                            adj.set_axis_adjustment([x, y, z]);
                            adj.rot_axis_auto = false;
                            i += 4;
                        }
                        "s" if i + 1 < tokens.len() => {
                            adj.rotation_scale = tokens[i + 1].parse().unwrap_or(1.0);
                            i += 2;
                        }
                        _ => break,
                    }
                }
            }
        }

        Ok(Self {
            height_bone,
            auto_rot_adj,
            default_sync_local,
            axis_scale,
            bone_names,
            bones,
            vmd_to_bone,
            ik_sets,
            twist_disperse,
            prepare_enable_set,
            prepare_listed_only,
            prepare_rotations,
            bone_adjusts,
        })
    }
}

// ---------------------------------------------------------------------------
// Dummy Bone Tree & MMD IK Solvers (MMDIKSolverLeg, MMDIKSolverNoLimit)
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
struct DummyNode {
    name: String,
    parent: Option<usize>,
    children: Vec<usize>,
    local_pos: V3,
    local_rot: Quat,
    world_pos: V3,
    world_rot: Quat,
}

#[derive(Debug, Clone)]
struct DummyTree {
    nodes: Vec<DummyNode>,
    by_name: HashMap<String, usize>,
}

impl DummyTree {
    fn new(cfg: &CharaIniConfig) -> Self {
        let mut nodes = Vec::with_capacity(cfg.bone_names.len());
        let mut by_name = HashMap::new();
        for (idx, name) in cfg.bone_names.iter().enumerate() {
            by_name.insert(name.clone(), idx);
            nodes.push(DummyNode {
                name: name.clone(),
                parent: None,
                children: Vec::new(),
                local_pos: [0.0; 3],
                local_rot: [0.0, 0.0, 0.0, 1.0],
                world_pos: [0.0; 3],
                world_rot: [0.0, 0.0, 0.0, 1.0],
            });
        }
        for (idx, name) in cfg.bone_names.iter().enumerate() {
            if let Some(p_name) = cfg.bones.get(name).and_then(|b| b.parent.as_ref()) {
                if let Some(&p_idx) = by_name.get(p_name) {
                    nodes[idx].parent = Some(p_idx);
                    nodes[p_idx].children.push(idx);
                }
            }
        }
        Self { nodes, by_name }
    }

    fn update_from(&mut self, start_idx: usize) {
        let (p_pos, p_rot) = match self.nodes[start_idx].parent {
            Some(p) => (self.nodes[p].world_pos, self.nodes[p].world_rot),
            None => ([0.0; 3], [0.0, 0.0, 0.0, 1.0]),
        };
        self.nodes[start_idx].world_pos = vadd(p_pos, qrot(p_rot, self.nodes[start_idx].local_pos));
        self.nodes[start_idx].world_rot = qnorm(quat_mul(p_rot, self.nodes[start_idx].local_rot));
        let children = self.nodes[start_idx].children.clone();
        for c in children {
            self.update_from(c);
        }
    }

    fn set_local_pos(&mut self, idx: usize, pos: V3) {
        self.nodes[idx].local_pos = pos;
        self.update_from(idx);
    }

    fn set_local_rot(&mut self, idx: usize, rot: Quat) {
        self.nodes[idx].local_rot = qnorm(rot);
        self.update_from(idx);
    }

    fn set_world_pos(&mut self, idx: usize, pos: V3) {
        let (p_pos, p_rot) = match self.nodes[idx].parent {
            Some(p) => (self.nodes[p].world_pos, self.nodes[p].world_rot),
            None => ([0.0; 3], [0.0, 0.0, 0.0, 1.0]),
        };
        let local = qrot(qinv(p_rot), vsub(pos, p_pos));
        self.set_local_pos(idx, local);
    }

    fn set_world_rot(&mut self, idx: usize, rot: Quat) {
        let p_rot = match self.nodes[idx].parent {
            Some(p) => self.nodes[p].world_rot,
            None => [0.0, 0.0, 0.0, 1.0],
        };
        let local = quat_mul(qinv(p_rot), rot);
        self.set_local_rot(idx, local);
    }
}

#[derive(Debug, Clone)]
enum IkSolver {
    Leg {
        ik_bone: usize,
        ik_tgt: usize,
        chain0_knee: usize,
        chain1_hip: usize,
        iterations: usize,
        step_angle: f32,
        min_knee_q: Quat,
        leg_sum: f32,
        leg_mul2: f32,
        leg_sqr_sum: f32,
        base_inv_q: Quat,
    },
    NoLimit {
        ik_bone: usize,
        ik_tgt: usize,
        chain: Vec<usize>,
        iterations: usize,
        step_angle: f32,
    },
}

impl IkSolver {
    fn create_leg(
        tree: &mut DummyTree,
        ik_bone: usize,
        ik_tgt: usize,
        chain0_knee: usize,
        chain1_hip: usize,
        iterations: usize,
        step_angle: f32,
        min_knee_rot: f32,
    ) -> Self {
        let min_knee_q = euler_deg_to_quat([min_knee_rot, 0.0, 0.0]);
        tree.set_local_rot(chain0_knee, min_knee_q);
        let hip_pos = tree.nodes[chain1_hip].world_pos;
        let knee_pos = tree.nodes[chain0_knee].world_pos;
        let foot_pos = tree.nodes[ik_tgt].world_pos;
        let thigh2knee = vsub(knee_pos, hip_pos);
        let knee2foot = vsub(foot_pos, knee_pos);
        let thigh2knee_len = vlen(thigh2knee);
        let knee2foot_len = vlen(knee2foot);
        let leg_sum = vlen(vsub(hip_pos, foot_pos));
        let leg_mul2 = thigh2knee_len * knee2foot_len * 2.0;
        let leg_sqr_sum = thigh2knee_len * thigh2knee_len + knee2foot_len * knee2foot_len;
        let base_q = qfrom_to(thigh2knee, knee2foot);
        let base_inv_q = qinv(base_q);
        Self::Leg {
            ik_bone,
            ik_tgt,
            chain0_knee,
            chain1_hip,
            iterations,
            step_angle,
            min_knee_q,
            leg_sum,
            leg_mul2,
            leg_sqr_sum,
            base_inv_q,
        }
    }

    fn solve(&self, tree: &mut DummyTree) {
        match *self {
            Self::Leg {
                ik_bone,
                ik_tgt,
                chain0_knee,
                chain1_hip,
                iterations,
                step_angle,
                min_knee_q,
                leg_sum,
                leg_mul2,
                leg_sqr_sum,
                base_inv_q,
            } => {
                // Matches vmdlib.py::MMDIKSolverLeg.Solve (lines 1494-1533)
                let tgt_pos = tree.nodes[ik_bone].world_pos;
                let thigh2tgt_len = vlen(vsub(tgt_pos, tree.nodes[chain1_hip].world_pos));
                if thigh2tgt_len < leg_sum && leg_mul2 > 1e-6 {
                    let cos_a = ((leg_sqr_sum - thigh2tgt_len * thigh2tgt_len) / leg_mul2)
                        .clamp(-1.0, 1.0);
                    let knee_angle = 180.0 - cos_a.acos().to_degrees();
                    tree.set_local_rot(
                        chain0_knee,
                        quat_mul(euler_deg_to_quat([knee_angle, 0.0, 0.0]), base_inv_q),
                    );
                } else {
                    tree.set_local_rot(chain0_knee, min_knee_q);
                }

                // Turn upper leg to target via CCD without clobbering VMD thigh twist
                for _ in 0..iterations {
                    let end_pos = tree.nodes[ik_tgt].world_pos;
                    let bone_pos = tree.nodes[chain1_hip].world_pos;
                    if vlen(vsub(end_pos, tgt_pos)) < 0.001 {
                        break;
                    }
                    let from_v = vsub(end_pos, bone_pos);
                    let to_v = vsub(tgt_pos, bone_pos);
                    let (mut angle, axis) = quat_to_angle_axis_deg(qfrom_to(from_v, to_v));
                    if angle.abs() < 1e-5 {
                        break;
                    }
                    if angle > step_angle {
                        angle = step_angle;
                    }
                    let delta_q = quat_from_angle_axis_deg(angle, axis);
                    let result_q = quat_mul(delta_q, tree.nodes[chain1_hip].world_rot);
                    tree.set_world_rot(chain1_hip, result_q);
                }
            }
            Self::NoLimit {
                ik_bone,
                ik_tgt,
                ref chain,
                iterations,
                step_angle,
            } => {
                // Matches vmdlib.py::MMDIKSolverNoLimit.Solve (lines 1295-1345)
                let tgt_pos = tree.nodes[ik_bone].world_pos;
                for n in 0..iterations {
                    let end_pos = tree.nodes[ik_tgt].world_pos;
                    if vlen(vsub(end_pos, tgt_pos)) < 0.001 {
                        break;
                    }
                    for (cb_i, &chain_bone) in chain.iter().enumerate() {
                        let end_pos = if n == 0 && cb_i == 0 {
                            end_pos
                        } else {
                            tree.nodes[ik_tgt].world_pos
                        };
                        let bone_pos = tree.nodes[chain_bone].world_pos;
                        let from_v = vsub(end_pos, bone_pos);
                        let to_v = vsub(tgt_pos, bone_pos);
                        let (mut angle, axis) = quat_to_angle_axis_deg(qfrom_to(from_v, to_v));
                        if angle.abs() < 1e-5 {
                            continue;
                        }
                        if angle > step_angle {
                            angle = step_angle;
                        }
                        let delta_q = quat_from_angle_axis_deg(angle, axis);
                        let result_q = quat_mul(delta_q, tree.nodes[chain_bone].world_rot);
                        tree.set_world_rot(chain_bone, result_q);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Standard Skeleton Loader (p_cf_body_bone + p_cf_head_bone)
// ---------------------------------------------------------------------------

static STANDARD_SKELETON: OnceLock<Option<Skeleton>> = OnceLock::new();

pub fn standard_kk_skeleton() -> Option<Skeleton> {
    STANDARD_SKELETON
        .get_or_init(load_standard_kk_skeleton_uncached)
        .clone()
}

fn mmdd_resource_candidates() -> Vec<PathBuf> {
    let mut candidates = Vec::new();
    if let Some(active) = crate::oklib::active_dir() {
        candidates.push(active);
    }
    if let Ok(env_lib) = std::env::var("OK_BASE_LIBRARY") {
        candidates.push(PathBuf::from(env_lib));
    }
    if let Ok(exe) = std::env::current_exe() {
        for anc in exe.ancestors().take(6) {
            let p = anc.join("Content/IndependentResources/base_v1");
            if p.is_dir() {
                candidates.push(p);
                break;
            }
        }
    }
    let manifest_lib =
        Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
    if manifest_lib.is_dir() {
        candidates.push(manifest_lib);
    }
    if let Ok(game) = std::env::var("OK_GAME_ROOT") {
        candidates.push(PathBuf::from(game));
    }
    candidates.push(PathBuf::from(r"E:\Koikatu"));
    candidates.push(PathBuf::from(r"E:\01_Games\KKS\KoikatuSunshine"));
    candidates
}

fn graft_head_onto_body(body: &mut Skeleton, head: &Skeleton) {
    let Some((&cf_s_head_id, _)) = body.tfs.iter().find(|(_, t)| t.name == "cf_s_head") else {
        return;
    };
    let mut id_map = HashMap::new();
    for (i, &hid) in head.tfs.keys().enumerate() {
        id_map.insert(hid, -10_000 - (i as i64));
    }
    for (&hid, htf) in &head.tfs {
        let mut cloned = htf.clone();
        cloned.parent = match htf.parent {
            Some(p) if id_map.contains_key(&p) => Some(id_map[&p]),
            _ => Some(cf_s_head_id),
        };
        body.tfs.insert(id_map[&hid], cloned);
    }
    body.rebuild_worlds();
}

fn load_standard_kk_skeleton_uncached() -> Option<Skeleton> {
    let candidates = mmdd_resource_candidates();

    static CACHED_SKEL: OnceLock<Option<Skeleton>> = OnceLock::new();
    if let Some(cached) = CACHED_SKEL.get() {
        return cached.clone();
    }

    for root in candidates {
        if root.join("oklib.json").is_file() {
            let _ = crate::oklib::ensure_library(&root);
        }
        let studio_base = root.join("abdata/studio/base/00.unity3d");
        let oo_base = root.join("abdata/chara/oo_base.unity3d");
        let parsed_studio = crate::gamefs::read_source(&studio_base)
            .ok()
            .map(crate::unity::parse_bundle_cached);
        let parsed_oo = crate::gamefs::read_source(&oo_base)
            .ok()
            .map(crate::unity::parse_bundle_cached);
        let Some(mut body) = parsed_studio
            .as_ref()
            .and_then(|p| p.skeleton_for_prefab_name("p_cf_body_bone"))
            .or_else(|| {
                parsed_oo
                    .as_ref()
                    .and_then(|p| p.skeleton_for_prefab_name("p_cf_body_bone"))
            })
        else {
            continue;
        };
        body.include_tree_bones();
        crate::pose::apply_low_poly_body_references(&mut body);
        body.rebuild_worlds();
        if let Some(head) = parsed_studio
            .as_ref()
            .and_then(|p| p.skeleton_for_prefab_name("p_cf_head_bone"))
            .or_else(|| {
                parsed_oo
                    .as_ref()
                    .and_then(|p| p.skeleton_for_prefab_name("p_cf_head_bone"))
            })
        {
            graft_head_onto_body(&mut body, &head);
        }
        let _ = CACHED_SKEL.set(Some(body.clone()));
        return Some(body);
    }
    None
}

struct MmddShapeTemplate {
    female_body_rest: Skeleton,
    male_body_rest: Option<Skeleton>,
    head_rest: Option<Skeleton>,
    body_keys: HashMap<String, Vec<crate::shape::Keyframe>>,
    head_keys: HashMap<String, Vec<crate::shape::Keyframe>>,
    correct: Vec<crate::shape::CorrectInfo>,
}

fn mmdd_shape_template() -> Option<&'static MmddShapeTemplate> {
    static TEMPLATE: OnceLock<Option<MmddShapeTemplate>> = OnceLock::new();
    TEMPLATE
        .get_or_init(|| {
            for root in mmdd_resource_candidates() {
                if root.join("oklib.json").is_file() {
                    let _ = crate::oklib::ensure_library(&root);
                }
                let studio_base = root.join("abdata/studio/base/00.unity3d");
                let oo_base = root.join("abdata/chara/oo_base.unity3d");
                let bo_head = root.join("abdata/chara/bo_head_00.unity3d");
                let parsed_studio = crate::gamefs::read_source(&studio_base)
                    .ok()
                    .map(crate::unity::parse_bundle_cached);
                let Some(parsed_oo) = crate::gamefs::read_source(&oo_base)
                    .ok()
                    .map(crate::unity::parse_bundle_cached)
                else {
                    continue;
                };
                let body_keys =
                    crate::shape::load_keys_named(&root, "chara/oo_base.unity3d", "cf_anmShapeBody");
                if body_keys.is_empty() {
                    continue;
                }
                let oo_bone_prefab = parsed_oo.skeleton_for_prefab_name("p_cf_body_bone");
                let studio_bone_prefab = parsed_studio
                    .as_ref()
                    .and_then(|p| p.skeleton_for_prefab_name("p_cf_body_bone"));

                let prep_body_rest = |mut sk: Skeleton| -> Skeleton {
                    if let Some(ref bp) = oo_bone_prefab {
                        sk.merge_missing_tree_transforms(bp);
                    }
                    if let Some(ref sp) = studio_bone_prefab {
                        sk.merge_missing_tree_transforms(sp);
                    }
                    sk.include_tree_bones();
                    crate::pose::apply_low_poly_body_references(&mut sk);
                    sk.rebuild_worlds();
                    sk
                };

                let (female_meshes, _) = parsed_oo.meshes_for_maindata("p_cf_body_00");
                let Some(female_raw) = female_meshes
                    .iter()
                    .find(|m| m.name.eq_ignore_ascii_case("o_body_a"))
                    .and_then(|m| m.smr_skel.clone())
                    .or_else(|| studio_bone_prefab.clone())
                    .or_else(|| oo_bone_prefab.clone())
                else {
                    continue;
                };
                let female_body_rest = prep_body_rest(female_raw);

                let (male_meshes, _) = parsed_oo.meshes_for_maindata("p_cm_body_00");
                let male_body_rest = male_meshes
                    .iter()
                    .find(|m| m.name.eq_ignore_ascii_case("o_body_a"))
                    .and_then(|m| m.smr_skel.clone())
                    .map(prep_body_rest);

                let parsed_head = crate::gamefs::read_source(&bo_head)
                    .ok()
                    .map(crate::unity::parse_bundle_cached);
                let mut head_rest = parsed_studio
                    .as_ref()
                    .and_then(|p| p.skeleton_for_prefab_name("p_cf_head_bone"))
                    .or_else(|| parsed_oo.skeleton_for_prefab_name("p_cf_head_bone"));
                if let Some(ref mut h) = head_rest {
                    h.include_tree_bones();
                    if let Some(mesh_skel) = parsed_head
                        .as_ref()
                        .and_then(|p| p.skeleton_for_prefab_name("p_cf_head_00"))
                    {
                        crate::pose::copy_same_name_transform(h, &mesh_skel);
                    }
                    h.rebuild_worlds();
                }
                let head_keys = crate::shape::load_head_keys_named(&bo_head, "cf_anmShapeHead_00");
                let correct = crate::shape::load_correct_info(&root);

                return Some(MmddShapeTemplate {
                    female_body_rest,
                    male_body_rest,
                    head_rest,
                    body_keys,
                    head_keys,
                    correct,
                });
            }
            None
        })
        .as_ref()
}

/// Default MMDD high-heel angles (`mmddheelsext.py` `getDefaultHeelSetting()`: `[foot, heel, toes]` in degrees).
pub const DEFAULT_HIGH_HEEL_ROT: [f32; 3] = [25.0, 15.0, -25.0];

/// High-heel compensation angles (degrees around +X) and root Y height lift (metres),
/// matching `mmddheelsext.py` `getNormalizedHeelInfo` / `vmdlib.py` `setHeelzRotation` + `setShoesOffset`.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct MmddHeelPreset {
    /// `[foot_adj, heel_adj, toes_adj]` in degrees applied to `foot_d_l/r`, `heel_ex_l/r`, `toe_ex_l/r`.
    pub rot: [f32; 3],
    /// Root Y offset in metres (`ShoesOffset[1]`).
    pub height: f32,
}

/// Active coordinate shoe status for MMDD `ShoesDetect` (`mmddheelsext.py` `HasShoes` / `IsShoesActive` / `getHeelInfo`).
#[derive(Debug, Clone, Default, PartialEq)]
pub struct MmddShoeState {
    /// Whether the outfit's active shoe slot (`shoesType`: 0 indoor -> 7, 1 outdoor -> 8) has a non-dummy shoe assigned.
    pub has_shoes: bool,
    /// Whether shoes are currently worn (`clothesState[7] == 0` / `clothes_shoes` visible).
    pub shoes_active: bool,
    /// Resolved shoe display name (empty when barefoot or unresolved third-party mod).
    pub shoe_name: String,
    /// Auto-detected high-heel preset when the assigned shoe is a high-heel (`None` for flat shoes or barefoot).
    pub detected_heel: Option<MmddHeelPreset>,
}

fn normalize_signed_deg(deg: f32) -> f32 {
    let mut d = deg.rem_euclid(360.0);
    if d > 180.0 {
        d -= 360.0;
    }
    d
}

fn parse_stiletto_txt(text: &str) -> Option<Option<MmddHeelPreset>> {
    let mut height = 0.0f32;
    let mut angle_leg = 0.0f32;
    let mut angle_ankle = 0.0f32;
    let mut angle_toes: Option<f32> = None;
    let mut any_field = false;
    for raw_line in text.lines() {
        let line = raw_line.trim().trim_start_matches('\u{feff}');
        if line.is_empty() || line.starts_with('#') || line.starts_with(';') || line.starts_with("//") {
            continue;
        }
        let Some((k, v)) = line.split_once('=') else {
            continue;
        };
        let key = k.trim().to_ascii_lowercase();
        let Ok(val) = v.trim().parse::<f32>() else {
            continue;
        };
        if !val.is_finite() {
            continue;
        }
        match key.as_str() {
            "height" => {
                height = val;
                any_field = true;
            }
            "angleleg" | "foot" => {
                angle_leg = normalize_signed_deg(val);
                any_field = true;
            }
            "angleankle" | "heel" => {
                angle_ankle = normalize_signed_deg(val);
                any_field = true;
            }
            "angletoes" | "toes" => {
                angle_toes = Some(normalize_signed_deg(val));
                any_field = true;
            }
            _ => {}
        }
    }
    if !any_field {
        return None;
    }
    if angle_leg.abs() <= 3.5 && angle_ankle.abs() <= 3.5 && height.abs() <= 0.012 {
        // Stiletto `.txt` exists and explicitly marks this shoe as flat (e.g. vanilla ローファー / 上履き).
        return Some(None);
    }
    let toes = angle_toes.unwrap_or_else(|| if angle_ankle.abs() > 3.5 { -angle_ankle } else { 0.0 });
    Some(Some(MmddHeelPreset {
        rot: [angle_leg, angle_ankle, toes],
        height,
    }))
}

fn load_stiletto_txt_preset(shoe_name: &str, search_roots: &[&Path]) -> Option<Option<MmddHeelPreset>> {
    let trimmed = shoe_name.trim();
    if trimmed.is_empty() {
        return None;
    }
    let mut candidate_dirs: Vec<PathBuf> = Vec::new();
    if let Ok(custom_dir) = std::env::var("OK_STILETTO_DIR") {
        let p = PathBuf::from(custom_dir.trim());
        if !p.as_os_str().is_empty() {
            candidate_dirs.push(p.clone());
            candidate_dirs.push(p.join("Heels"));
        }
    }
    let mut roots: Vec<PathBuf> = search_roots
        .iter()
        .filter(|p| !p.as_os_str().is_empty())
        .map(|p| p.to_path_buf())
        .collect();
    if let Ok(env_root) = std::env::var("OK_GAME_ROOT") {
        let p = PathBuf::from(env_root.trim());
        if !p.as_os_str().is_empty() {
            roots.push(p);
        }
    }
    for root in roots {
        let mut cur: Option<&Path> = Some(root.as_path());
        for _ in 0..5 {
            let Some(dir) = cur else { break };
            for rel in [
                "BepInEx/config/Stiletto/Heels",
                "BepInEx/config/Stiletto",
                "BepInEx/Stiletto/Heels",
                "BepInEx/Stiletto",
            ] {
                let c = dir.join(rel);
                if !candidate_dirs.contains(&c) {
                    candidate_dirs.push(c);
                }
            }
            cur = dir.parent();
        }
    }
    let filenames = if trimmed == shoe_name {
        vec![format!("{trimmed}.txt"), format!("{trimmed} .txt")]
    } else {
        vec![
            format!("{shoe_name}.txt"),
            format!("{trimmed}.txt"),
            format!("{trimmed} .txt"),
        ]
    };
    for dir in candidate_dirs {
        for fname in &filenames {
            let p = dir.join(fname);
            if let Ok(text) = std::fs::read_to_string(&p) {
                if let Some(res) = parse_stiletto_txt(&text) {
                    return Some(res);
                }
            }
        }
    }
    None
}

/// Resolves a high-heel preset for a shoe item from Stiletto `.txt` configs or built-in Stiletto / high-heel mod presets.
/// Returns `None` for vanilla KK/KKS flat shoes (`ローファー`, `上履き`, `運動靴`, `ショートブーツ`, vanilla `パンプス`, etc.).
pub fn detect_high_heel_preset(
    shoe_name: &str,
    main_data: &str,
    mod_guid: &str,
    search_roots: &[&Path],
) -> Option<MmddHeelPreset> {
    if let Some(from_txt) = load_stiletto_txt_preset(shoe_name, search_roots) {
        return from_txt;
    }
    let n = shoe_name.trim().to_lowercase();
    let m = main_data.trim().to_ascii_lowercase();
    let g = mod_guid.trim().to_ascii_lowercase();

    // Explicit flat-shoe guard when not tagged Stiletto / high-heel
    let has_stiletto_tag = n.contains("stiletto") || g.contains("stiletto") || m.contains("stiletto");
    if has_stiletto_tag {
        if n.contains("platform") || n.contains("厚底") {
            return Some(MmddHeelPreset {
                rot: [28.0, 20.0, -28.0],
                height: 0.085,
            });
        }
        if n.contains("sandal") || n.contains("サンダル") || n.contains("mule") || n.contains("ミュール") {
            return Some(MmddHeelPreset {
                rot: [25.0, 18.0, -26.0],
                height: 0.068,
            });
        }
        if n.contains("boot") || n.contains("ブーツ") {
            return Some(MmddHeelPreset {
                rot: [24.0, 16.0, -24.0],
                height: 0.065,
            });
        }
        return Some(MmddHeelPreset {
            rot: [24.5, 19.5, -26.3],
            height: 0.062,
        });
    }
    if n.contains("vanilla heels a") {
        return Some(MmddHeelPreset {
            rot: [20.0, 15.0, -20.0],
            height: 0.055,
        });
    }
    if n.contains("vanilla heels b") {
        return Some(MmddHeelPreset {
            rot: [25.0, 18.0, -25.0],
            height: 0.065,
        });
    }
    if n.contains("vanilla heels c") {
        return Some(MmddHeelPreset {
            rot: [30.0, 20.0, -28.0],
            height: 0.075,
        });
    }
    let sep_heel = n.contains("(sep)")
        && (n.contains("heel")
            || n.contains("pump")
            || n.contains("パンプス")
            || n.contains("ヒール")
            || n.contains("ミュール"));
    let high_heel_kw = n.contains("high heel")
        || n.contains("highheel")
        || n.contains("high_heel")
        || n.contains("ハイヒール")
        || n.contains("ピンヒール")
        || n.contains("(ヒール)")
        || n.contains("（ヒール）")
        || n.contains("ヒールパンプス")
        || n.contains("ヒールブーツ")
        || n.contains("ヒールサンダル")
        || n.contains("高跟")
        || g.contains("highheel")
        || g.contains("high_heel")
        || m.contains("highheel")
        || m.contains("high_heel");
    if sep_heel || high_heel_kw {
        return Some(MmddHeelPreset {
            rot: DEFAULT_HIGH_HEEL_ROT,
            height: 0.060,
        });
    }
    None
}

fn candidate_catalog_roots(extra_roots: &[&Path]) -> Vec<PathBuf> {
    let mut out: Vec<PathBuf> = Vec::new();
    let mut push_unique = |p: PathBuf| {
        if !p.as_os_str().is_empty() && !out.contains(&p) {
            out.push(p);
        }
    };
    for r in extra_roots {
        push_unique(r.to_path_buf());
    }
    if let Ok(env_root) = std::env::var("OK_GAME_ROOT") {
        push_unique(PathBuf::from(env_root.trim()));
    }
    let manifest_dir = Path::new(env!("CARGO_MANIFEST_DIR"));
    if let Some(proj_root) = manifest_dir.parent().and_then(Path::parent) {
        push_unique(proj_root.join("Content/IndependentResources/base_v1"));
        push_unique(proj_root.join("Content/IndependentResources/native_v1"));
    }
    out
}

/// Resolves `(has_non_dummy_slot, entry_name, main_data, mod_guid)` for `card`'s active shoe slot in `outfit`.
pub fn resolve_card_shoe_meta(
    card: &crate::card::CardInfo,
    outfit: i32,
    shoes_type: u8,
    search_roots: &[&Path],
) -> Option<(String, String, String)> {
    let kind = if shoes_type == 0 || card.clothes_ids.len() <= 8 {
        7usize
    } else {
        8usize
    };
    let cid = card.clothes_ids.get(kind).copied().unwrap_or(0);
    let prop_name = crate::accessories::CLOTHES_PROP.get(kind).copied()?;
    let prop = format!("outfit{}.ChaFileClothes.{prop_name}", outfit.max(0));
    let resolve = card.resolves.iter().find(|r| r.property == prop);
    if resolve.is_none() && cid == 0 {
        return None;
    }
    let guid = resolve.map(|r| r.mod_id.clone()).unwrap_or_default();
    let roots = candidate_catalog_roots(search_roots);
    for root in &roots {
        let chara_dir = root.join("abdata/list/characustom");
        let mods_dir = root.join("mods");
        if !chara_dir.exists() && !mods_dir.exists() {
            continue;
        }
        let vanilla = crate::list::load_characustom_dir_shared(&chara_dir);
        let mod_catalog = crate::list::load_mods_catalog_shared(&mods_dir);
        let guid_index = if guid.is_empty() || !mods_dir.exists() {
            HashMap::new()
        } else {
            let mut wanted = std::collections::HashSet::new();
            wanted.insert(guid.clone());
            crate::mods::find_guids(&mods_dir, Some(&wanted))
        };
        let slots = crate::accessories::select_card_clothes(
            card,
            outfit.max(0),
            &vanilla,
            &mod_catalog,
            &guid_index,
        );
        if let Some(Some((entry, resolved_guid, _))) = slots.get(kind) {
            if crate::is_dummy_part(&entry.main_data, Some(&entry.name)) {
                return None;
            }
            return Some((entry.name.clone(), entry.main_data.clone(), resolved_guid.clone()));
        }
    }
    // Unresolved mod shoe (e.g. third-party zipmod absent on this machine): still has a shoe item assigned.
    Some((String::new(), String::new(), guid))
}

/// Populates `session.active_shoe_meta` when opening via cached/OK-card paths where `SessionBlob` did not store it.
pub(crate) fn populate_session_shoe_meta(
    session: &mut crate::session::OkSession,
    card_path: &Path,
    outfit: i32,
) {
    let Ok(bytes) = crate::gamefs::read(card_path).or_else(|_| std::fs::read(card_path)) else {
        return;
    };
    let eff_outfit = outfit.max(0);
    let Ok(info) = crate::card::parse_card_bytes(&bytes, eff_outfit) else {
        return;
    };
    let shoes_type = crate::unity::requested_shoes_type();
    let mut roots: Vec<&Path> = Vec::new();
    if !session.game_root.as_os_str().is_empty() {
        roots.push(session.game_root.as_path());
    }
    if let Some(parent) = card_path.parent() {
        roots.push(parent);
    }
    session.active_shoe_meta = resolve_card_shoe_meta(&info, eff_outfit, shoes_type, &roots);
}

/// Per-character shaped skeleton and scale context matching KKS `vmdlib.py` `bindChara` / `createVmdBone`.
#[derive(Clone)]
pub struct MmddCharacterRig {
    /// Pre-ABMX shaped body skeleton (matches `OkSession::body_shaped`, without grafted head nodes).
    pub body_shaped: Skeleton,
    /// Live shaped skeleton in `BodyTop`/`ChaControl` space (post-Studio limb corrections + ABMX + grafted head).
    pub live_rest: Skeleton,
    pub abm: Vec<crate::abmx::AbmBone>,
    pub sex: i32,
    /// `cf_n_height.localScale.y` on `live_rest`.
    pub height_scale: f32,
    /// `oiCharInfo.changeAmount.scale.y` (defaults to `1.0`).
    pub obj_scale_y: f32,
    /// Active shoe & `ShoesDetect` state for this character.
    pub shoe_state: MmddShoeState,
}

impl MmddCharacterRig {
    pub fn build(
        body_shaped: Skeleton,
        head_posed: Option<&Skeleton>,
        abm: Vec<crate::abmx::AbmBone>,
        sex: i32,
        obj_scale_y: f32,
    ) -> Self {
        let mut live_rest = body_shaped.clone();
        if sex == 1 {
            // Studio clips (`anim_scene_mode == 4`) apply `(arm=false, hand=true)` limb size corrections.
            crate::pose::apply_scene_limb_size_corrections(&mut live_rest, false, true);
        }
        if !abm.is_empty() {
            let base = crate::pose::capture_abm_baseline(&live_rest, &abm);
            crate::pose::apply_abm(&mut live_rest, &abm, 0, &base);
        }
        let height_scale = find_skel_bone_ci(&live_rest, "cf_n_height")
            .or_else(|| find_skel_bone_ci(&live_rest, "cm_n_height"))
            .map(|(_, tf)| tf.scale[1])
            .filter(|s| s.is_finite() && *s > 1e-4)
            .unwrap_or(1.0);
        let obj_scale_y = if obj_scale_y.is_finite() && obj_scale_y > 1e-4 {
            obj_scale_y
        } else {
            1.0
        };
        if let Some(head) = head_posed {
            graft_head_onto_body(&mut live_rest, head);
        }
        Self {
            body_shaped,
            live_rest,
            abm,
            sex,
            height_scale,
            obj_scale_y,
            shoe_state: MmddShoeState::default(),
        }
    }

    /// Builds a rig from a live `OkSession` (e.g. OK Studio runtime VMD load), including `ShoesDetect` shoe state.
    pub fn from_session(session: &crate::session::OkSession, obj_scale_y: f32) -> Option<Self> {
        let body_shaped = if let Some(bs) = session.body_shaped.as_ref() {
            bs.clone()
        } else if let Some(mut shaped) = session.body_rest.clone() {
            let shapes = session.yure_policy.map_or([[true; 9]; 2], |p| p.shapes);
            crate::shape::apply_body_shape_keys_masked(
                &mut shaped,
                &session.body_keys,
                &session.shape_body,
                &session.correct,
                session.sex,
                session.type_bone,
                session.nip_stand_rate,
                shapes,
            );
            crate::pose::assigned_weights_remap(&mut shaped);
            shaped
        } else {
            session.body_skel.as_ref()?.clone()
        };
        let head_posed = session
            .head_skel
            .as_ref()
            .or(session.head_full_posed.as_ref())
            .or(session.head_rest.as_ref());
        let mut rig = Self::build(
            body_shaped,
            head_posed,
            session.abm.clone(),
            session.sex,
            obj_scale_y,
        );
        let shoes_visible = session
            .parts
            .iter()
            .any(|p| p.role == "clothes_shoes" && p.render_visible);
        let (has_shoes, shoe_name, main_data, guid) = if let Some((name, main, guid)) =
            session.active_shoe_meta.as_ref()
        {
            (true, name.clone(), main.clone(), guid.clone())
        } else if let Some(p) = session.parts.iter().find(|p| p.role == "clothes_shoes") {
            (true, p.name.clone(), String::new(), String::new())
        } else {
            (false, String::new(), String::new(), String::new())
        };
        let mut roots: Vec<&Path> = Vec::new();
        if !session.game_root.as_os_str().is_empty() {
            roots.push(session.game_root.as_path());
        }
        let detected_heel = if has_shoes {
            detect_high_heel_preset(&shoe_name, &main_data, &guid, &roots)
        } else {
            None
        };
        rig.shoe_state = MmddShoeState {
            has_shoes,
            shoes_active: has_shoes && shoes_visible,
            shoe_name,
            detected_heel,
        };
        Some(rig)
    }

    /// Builds a rig from parsed KKS `CardInfo` + optional Studio nipple override + scene object Y scale.
    pub fn from_card_info(
        card: &crate::card::CardInfo,
        nip_override: Option<f32>,
        obj_scale_y: f32,
    ) -> Option<Self> {
        Self::from_card_info_with_roots(
            card,
            card.status_coordinate.max(0),
            nip_override,
            obj_scale_y,
            &[],
        )
    }

    /// Builds a rig from parsed `CardInfo` at `outfit`, resolving active shoe metadata across `search_roots`.
    pub fn from_card_info_with_roots(
        card: &crate::card::CardInfo,
        outfit: i32,
        nip_override: Option<f32>,
        obj_scale_y: f32,
        search_roots: &[&Path],
    ) -> Option<Self> {
        let tpl = mmdd_shape_template()?;
        let mut body_shaped = if card.sex == 0 {
            tpl.male_body_rest
                .clone()
                .unwrap_or_else(|| tpl.female_body_rest.clone())
        } else {
            tpl.female_body_rest.clone()
        };
        let nip = nip_override
            .filter(|v| *v >= 0.0)
            .unwrap_or(card.nip_stand_rate);
        let mut body_head_scale = [1.0f32; 3];
        if !card.shape_body.is_empty() {
            body_head_scale = crate::shape::apply_body_shape_keys_masked(
                &mut body_shaped,
                &tpl.body_keys,
                &card.shape_body,
                &tpl.correct,
                card.sex,
                card.type_bone,
                nip,
                [[true; 9]; 2],
            );
        }
        crate::pose::assigned_weights_remap(&mut body_shaped);

        let mut head_posed = tpl.head_rest.clone();
        if let Some(ref mut h) = head_posed {
            if !card.shape_face.is_empty() {
                let head_correct = 1.0 / (1.0 + tpl.correct.get(2).map(|c| c.scl[1]).unwrap_or(0.0));
                crate::shape::apply_head_shape_keys(
                    h,
                    &tpl.head_keys,
                    &card.shape_face,
                    body_head_scale,
                    head_correct,
                    card.type_bone,
                );
            }
            crate::pose::assigned_weights_remap(h);
            if !card.abm.is_empty() {
                let base = crate::pose::capture_abm_baseline(h, &card.abm);
                crate::pose::apply_abm(h, &card.abm, 0, &base);
            }
        }
        let mut rig = Self::build(
            body_shaped,
            head_posed.as_ref(),
            card.abm.clone(),
            card.sex,
            obj_scale_y,
        );
        let shoes_type = card.status_shoes_type;
        let state_idx = if card.status_clothes.len() >= 9 && shoes_type == 1 {
            8usize
        } else {
            7usize
        };
        let shoes_state_on = card.status_clothes.get(state_idx).copied().unwrap_or(0) == 0;
        if let Some((shoe_name, main_data, guid)) =
            resolve_card_shoe_meta(card, outfit, shoes_type, search_roots)
        {
            let detected_heel = detect_high_heel_preset(&shoe_name, &main_data, &guid, search_roots);
            rig.shoe_state = MmddShoeState {
                has_shoes: true,
                shoes_active: shoes_state_on,
                shoe_name,
                detected_heel,
            };
        }
        Some(rig)
    }

    /// Matches `vmdlib.py` `autoCharScale()`: `heightBone.localScale.y * oiCharInfo.changeAmount.scale.y`.
    pub fn auto_char_scale(&self) -> f32 {
        self.height_scale * self.obj_scale_y
    }
}

// ---------------------------------------------------------------------------
// MotionController VMD -> OkAnim Solver
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
pub struct MotionSolveParams {
    pub char_scale: f32,
    pub playback_speed: f32,
    pub delay: f32,
    pub interpolate_consecutive: bool,
    pub looping: bool,
    pub enable_high_heelz: Option<bool>,
    pub shoes_detect: bool,
    pub high_heels_rot: Option<[f32; 3]>,
    /// `ShoesOffset` `[enable, shoes_on_y, shoes_off_y]` (`vmdlib.py:3487`).
    pub shoes_offset: Option<(bool, f32, f32)>,
    /// Optional explicit override for whether shoes are currently active (`None` reads `rig.shoe_state.shoes_active`).
    pub shoes_active_override: Option<bool>,
    pub ik_enable: HashMap<String, bool>,
    pub custom_rot_adj: HashMap<String, V3>,
    pub custom_axis_adj: HashMap<String, V3>,
    pub custom_rot_scale: HashMap<String, f32>,
    /// Replaces `adj.base_position` (`customizedBoneAdjust` `"posadj"` in `vmdlib.py:2776`).
    pub custom_base_pos: HashMap<String, V3>,
    pub custom_pos_offset: HashMap<String, V3>,
    pub custom_pos_scale: HashMap<String, f32>,
    pub modified_keyframes: Vec<(String, String, u32, Option<VmdBoneKeyFrame>)>,
}

impl Default for MotionSolveParams {
    fn default() -> Self {
        Self {
            // `<= 0.0` selects `autoCharScale()` (`height_scale * obj_scale_y`, or `1.0` when `rig` is `None`).
            char_scale: 0.0,
            playback_speed: 1.0,
            delay: 0.0,
            interpolate_consecutive: true,
            looping: false,
            enable_high_heelz: None,
            shoes_detect: true,
            high_heels_rot: None,
            shoes_offset: None,
            shoes_active_override: None,
            ik_enable: HashMap::new(),
            custom_rot_adj: HashMap::new(),
            custom_axis_adj: HashMap::new(),
            custom_rot_scale: HashMap::new(),
            custom_base_pos: HashMap::new(),
            custom_pos_offset: HashMap::new(),
            custom_pos_scale: HashMap::new(),
            modified_keyframes: Vec::new(),
        }
    }
}

fn parse_shoes_offset_val(v: &serde_json::Value) -> Option<(bool, f32, f32)> {
    if let Some(arr) = v.as_array() {
        if arr.len() >= 3 {
            return Some((
                val_to_bool(&arr[0], false),
                val_to_f32(&arr[1], 0.0),
                val_to_f32(&arr[2], 0.0),
            ));
        } else if arr.len() == 2 {
            return Some((
                val_to_bool(&arr[0], false),
                val_to_f32(&arr[1], 0.0),
                0.0,
            ));
        }
    } else if v.is_number() {
        let y = val_to_f32(v, 0.0);
        return Some((true, y, 0.0));
    }
    None
}

/// Resolves effective `(heel_rot, root_y_offset)` from `MotionSolveParams` and character `MmddShoeState`,
/// matching `vmdlib.py` (`setFrame` `ShoesDetect` loop + `enableHeelzCtrl` + `setHeelzRotation` + `setShoesOffset`)
/// and `mmddheelsext.py` (`HasShoes`, `IsShoesActive`, `getNormalizedHeelInfo`).
pub fn resolve_high_heels_and_offset(
    params: &MotionSolveParams,
    rig: Option<&MmddCharacterRig>,
) -> (Option<[f32; 3]>, Option<f32>) {
    let shoe_state = rig.map(|r| &r.shoe_state);
    let shoes_active = params
        .shoes_active_override
        .or_else(|| shoe_state.map(|s| s.shoes_active))
        .unwrap_or(true);
    let has_shoes = params
        .shoes_active_override
        .or_else(|| shoe_state.map(|s| s.has_shoes))
        .unwrap_or(true);

    let heel_rot = if params.shoes_detect {
        if !has_shoes || !shoes_active {
            None
        } else if params.enable_high_heelz == Some(false)
            && shoe_state.and_then(|s| s.detected_heel).is_none()
        {
            None
        } else if let Some(rot) = params.high_heels_rot.filter(|r| {
            params.enable_high_heelz != Some(false)
                && (r[0].abs() > 1e-4 || r[1].abs() > 1e-4 || r[2].abs() > 1e-4)
        }) {
            let known_flat = shoe_state
                .is_some_and(|s| !s.shoe_name.is_empty() && s.detected_heel.is_none());
            if known_flat {
                None
            } else {
                Some(rot)
            }
        } else if let Some(preset) = shoe_state.and_then(|s| s.detected_heel) {
            Some(preset.rot)
        } else if params.enable_high_heelz == Some(true) {
            let known_flat = shoe_state
                .is_some_and(|s| !s.shoe_name.is_empty() && s.detected_heel.is_none());
            if known_flat {
                None
            } else {
                Some(DEFAULT_HIGH_HEEL_ROT)
            }
        } else {
            None
        }
    } else if params.enable_high_heelz == Some(false) {
        None
    } else if let Some(rot) = params.high_heels_rot {
        Some(rot)
    } else if params.enable_high_heelz == Some(true) {
        Some(
            shoe_state
                .and_then(|s| s.detected_heel)
                .map(|p| p.rot)
                .unwrap_or(DEFAULT_HIGH_HEEL_ROT),
        )
    } else {
        None
    };

    let root_y_offset = if let Some((enabled, on_y, off_y)) = params.shoes_offset {
        if enabled {
            Some(if !params.shoes_detect || (has_shoes && shoes_active) {
                on_y
            } else {
                off_y
            })
        } else {
            None
        }
    } else if params.shoes_detect && has_shoes && shoes_active {
        shoe_state
            .and_then(|s| s.detected_heel)
            .filter(|_| heel_rot.is_some())
            .map(|p| p.height)
            .filter(|h| h.abs() > 1e-5)
    } else {
        None
    };

    (heel_rot, root_y_offset)
}

impl MotionSolveParams {
    /// Parses optional JSON solver options (empty string -> defaults), returning `(params, obj_scale_y)`.
    pub fn from_json_with_obj_scale(json: &str) -> Result<(Self, f32), String> {
        let trimmed = json.trim();
        if trimmed.is_empty() {
            return Ok((Self::default(), 1.0));
        }
        let val: serde_json::Value = serde_json::from_str(trimmed).map_err(|e| e.to_string())?;
        let Some(map) = val.as_object() else {
            return Err("params_json must be a JSON object".into());
        };
        let mut params = Self::default();
        if let Some(v) = map.get("char_scale").or_else(|| map.get("charScale")) {
            params.char_scale = val_to_f32(v, 0.0);
        }
        let obj_scale_y = map
            .get("obj_scale_y")
            .or_else(|| map.get("objScale"))
            .map(|v| val_to_f32(v, 1.0))
            .unwrap_or(1.0);
        if let Some(v) = map.get("playback_speed").or_else(|| map.get("playbackSpeed")) {
            params.playback_speed = val_to_f32(v, 1.0);
        }
        if let Some(v) = map.get("delay") {
            params.delay = val_to_f32(v, 0.0);
        }
        if let Some(v) = map
            .get("interpolate_consecutive")
            .or_else(|| map.get("interCons"))
        {
            params.interpolate_consecutive = val_to_bool(v, true);
        }
        if let Some(v) = map.get("looping") {
            params.looping = val_to_bool(v, false);
        }
        let explicit_shoes_detect =
            map.get("shoes_detect").is_some() || map.get("shoesDetect").is_some();
        if let Some(v) = map
            .get("enable_high_heelz")
            .or_else(|| map.get("enableHighHeelz"))
        {
            params.enable_high_heelz = Some(val_to_bool(v, false));
            if !explicit_shoes_detect {
                params.shoes_detect = false;
            }
        }
        if let Some(v) = map
            .get("shoes_detect")
            .or_else(|| map.get("shoesDetect"))
        {
            params.shoes_detect = val_to_bool(v, true);
        }
        if let Some(v) = map
            .get("shoes_active")
            .or_else(|| map.get("shoesActive"))
        {
            params.shoes_active_override = Some(val_to_bool(v, true));
        }
        if let Some(v) = map
            .get("shoes_offset")
            .or_else(|| map.get("shoesOffset"))
        {
            params.shoes_offset = parse_shoes_offset_val(v);
        }
        if let Some(v) = map
            .get("high_heels_rot")
            .or_else(|| map.get("highHeels"))
            .or_else(|| map.get("heelzRotation"))
            .or_else(|| map.get("heelzRot"))
        {
            if let Some(v3) = val_to_v3(v) {
                params.high_heels_rot = Some(v3);
                if !explicit_shoes_detect {
                    params.shoes_detect = false;
                    if params.enable_high_heelz.is_none() {
                        params.enable_high_heelz = Some(true);
                    }
                }
            } else if v.is_boolean() {
                if !explicit_shoes_detect {
                    params.shoes_detect = false;
                }
                if val_to_bool(v, false) {
                    params.high_heels_rot = Some(DEFAULT_HIGH_HEEL_ROT);
                    if params.enable_high_heelz.is_none() {
                        params.enable_high_heelz = Some(true);
                    }
                } else {
                    params.enable_high_heelz = Some(false);
                }
            }
        }
        if let Some(serde_json::Value::Object(iko)) =
            map.get("ik_enable").or_else(|| map.get("ikEnable"))
        {
            for (k, v) in iko {
                params.ik_enable.insert(k.clone(), val_to_bool(v, true));
            }
        }
        if let Some(serde_json::Value::Object(ro)) = map.get("custom_rot_adj") {
            for (k, v) in ro {
                if let Some(v3) = val_to_v3(v) {
                    params.custom_rot_adj.insert(k.clone(), v3);
                }
            }
        }
        if let Some(serde_json::Value::Object(ao)) = map.get("custom_axis_adj") {
            for (k, v) in ao {
                if let Some(v3) = val_to_v3(v) {
                    params.custom_axis_adj.insert(k.clone(), v3);
                }
            }
        }
        if let Some(serde_json::Value::Object(so)) = map.get("custom_rot_scale") {
            for (k, v) in so {
                params.custom_rot_scale.insert(k.clone(), val_to_f32(v, 1.0));
            }
        }
        if let Some(serde_json::Value::Object(bo)) = map.get("custom_base_pos") {
            for (k, v) in bo {
                if let Some(v3) = val_to_v3(v) {
                    params.custom_base_pos.insert(k.clone(), v3);
                }
            }
        }
        if let Some(serde_json::Value::Object(po)) = map.get("custom_pos_offset") {
            for (k, v) in po {
                if let Some(v3) = val_to_v3(v) {
                    params.custom_pos_offset.insert(k.clone(), v3);
                }
            }
        }
        if let Some(serde_json::Value::Object(ps)) = map.get("custom_pos_scale") {
            for (k, v) in ps {
                params.custom_pos_scale.insert(k.clone(), val_to_f32(v, 1.0));
            }
        }
        Ok((params, obj_scale_y))
    }
}

fn find_skel_bone_ci<'a>(skel: &'a Skeleton, name: &str) -> Option<(i64, &'a crate::pose::Tf)> {
    skel.tfs
        .iter()
        .find(|(_, t)| t.name == name)
        .or_else(|| skel.tfs.iter().find(|(_, t)| t.name.eq_ignore_ascii_case(name)))
        .map(|(&id, tf)| (id, tf))
}

fn skel_world_pos_rot(skel: &Skeleton, name: &str) -> (V3, Quat) {
    if let Some((id, _)) = find_skel_bone_ci(skel, name) {
        let m = crate::pose::world_of_id(id, &skel.tfs);
        let r = crate::pose::world_rotation_of(id, &skel.tfs);
        (mat_pos(&m), r)
    } else {
        ([0.0; 3], [0.0, 0.0, 0.0, 1.0])
    }
}

pub fn solve_vmd_to_okanim(
    vmd: &VmdFile,
    ini: &CharaIniConfig,
    params: &MotionSolveParams,
    source_name: &str,
) -> Result<OkAnim, String> {
    solve_vmd_to_okanim_for_rig(vmd, ini, params, source_name, None)
}

pub fn solve_vmd_to_okanim_for_rig(
    vmd: &VmdFile,
    ini: &CharaIniConfig,
    params: &MotionSolveParams,
    source_name: &str,
    rig: Option<&MmddCharacterRig>,
) -> Result<OkAnim, String> {
    let fallback_rest;
    let track_rest: &Skeleton = match rig {
        Some(r) => &r.body_shaped,
        None => {
            fallback_rest = standard_kk_skeleton()
                .ok_or("standard KK skeleton (studio/base/00.unity3d) unavailable")?;
            &fallback_rest
        }
    };
    let mut prep_body = match rig {
        Some(r) => r.live_rest.clone(),
        None => track_rest.clone(),
    };
    let inv_hips_parent_live = find_skel_bone_ci(&prep_body, "cf_j_hips")
        .and_then(|(id, _)| prep_body.tfs.get(&id).and_then(|t| t.parent))
        .and_then(|pid| {
            crate::pose::mat_inverse_checked(&crate::pose::world_of_id(pid, &prep_body.tfs))
        });
    let hips_abm = rig
        .and_then(|r| r.abm.iter().find(|b| b.name == "cf_j_hips").map(|b| b.for_coord(0)))
        .filter(|d| !d.is_empty());

    // 1. Apply [PrepareBoneBeforeCreateDummyBone] onto prep_body
    if ini.prepare_enable_set {
        let mut updates = Vec::new();
        for bname in &ini.bone_names {
            let Some(wname) = ini.bones.get(bname).and_then(|b| b.weight_bone.as_ref()) else {
                continue;
            };
            let wkey = wname.to_ascii_lowercase();
            let rot = if let Some(&r) = ini.prepare_rotations.get(&wkey) {
                r
            } else if !ini.prepare_listed_only {
                [0.0, 0.0, 0.0, 1.0]
            } else {
                continue;
            };
            if let Some((id, _)) = find_skel_bone_ci(&prep_body, wname) {
                updates.push((id, rot));
            }
        }
        for (id, rot) in updates {
            if let Some(tf) = prep_body.tfs.get_mut(&id) {
                tf.rot = rot;
            }
        }
        prep_body.rebuild_worlds();
    }

    // 2. Create and locate dummy bones
    let mut tree = DummyTree::new(ini);
    let mut bone_adjusts = ini.bone_adjusts.clone();
    for (idx, bname) in ini.bone_names.iter().enumerate() {
        let bdef = &ini.bones[bname];
        match &bdef.pos_spec {
            BonePosSpec::Bone(target) => {
                let (wpos, _) = skel_world_pos_rot(&prep_body, target);
                tree.set_world_pos(idx, wpos);
            }
            BonePosSpec::Between(t1, t2, scale) => {
                let (p1, _) = skel_world_pos_rot(&prep_body, t1);
                let (p2, _) = skel_world_pos_rot(&prep_body, t2);
                let wpos = [
                    p1[0] + (p2[0] - p1[0]) * scale,
                    p1[1] + (p2[1] - p1[1]) * scale,
                    p1[2] + (p2[2] - p1[2]) * scale,
                ];
                tree.set_world_pos(idx, wpos);
            }
            BonePosSpec::Offset(target, offset) => {
                let (p, _) = skel_world_pos_rot(&prep_body, target);
                tree.set_world_pos(idx, vadd(p, *offset));
            }
            BonePosSpec::Reset(target, rst) => {
                let (mut p, _) = skel_world_pos_rot(&prep_body, target);
                for axis in 0..3 {
                    if let Some(v) = rst[axis] {
                        p[axis] = v;
                    }
                }
                tree.set_world_pos(idx, p);
            }
            BonePosSpec::BonePosRot(target) => {
                let (wpos, wrot) = skel_world_pos_rot(&prep_body, target);
                tree.set_world_pos(idx, wpos);
                tree.set_world_rot(idx, wrot);
            }
        }
        if let Some(adj) = bone_adjusts.get_mut(bname) {
            adj.base_position = tree.nodes[idx].local_pos;
        }
    }

    // 3. Create IK solvers at the located dummy bone pose
    let mut ik_solvers: HashMap<usize, IkSolver> = HashMap::new();
    for ik_set in &ini.ik_sets {
        let enabled = params
            .ik_enable
            .get(&ik_set.name)
            .copied()
            .unwrap_or(ik_set.enable);
        if !enabled {
            continue;
        }
        for ik_bname in &ik_set.ik_bones {
            let Some(&ik_idx) = tree.by_name.get(ik_bname) else {
                continue;
            };
            let bdef = &ini.bones[ik_bname];
            if bdef.ik_data.len() < 2 {
                continue;
            }
            let Some(&tgt_idx) = tree.by_name.get(&bdef.ik_data[0]) else {
                continue;
            };
            if ik_set.solver.eq_ignore_ascii_case("MMDIKSolverLeg") && bdef.ik_data.len() >= 3 {
                let (Some(&c0), Some(&c1)) = (
                    tree.by_name.get(&bdef.ik_data[1]),
                    tree.by_name.get(&bdef.ik_data[2]),
                ) else {
                    continue;
                };
                ik_solvers.insert(
                    ik_idx,
                    IkSolver::create_leg(
                        &mut tree,
                        ik_idx,
                        tgt_idx,
                        c0,
                        c1,
                        ik_set.iterations,
                        ik_set.step_angle,
                        ik_set.min_knee_rot,
                    ),
                );
            } else {
                let chain: Vec<usize> = bdef.ik_data[1..]
                    .iter()
                    .filter_map(|n| tree.by_name.get(n).copied())
                    .collect();
                if !chain.is_empty() {
                    ik_solvers.insert(
                        ik_idx,
                        IkSolver::NoLimit {
                            ik_bone: ik_idx,
                            ik_tgt: tgt_idx,
                            chain,
                            iterations: ik_set.iterations,
                            step_angle: ik_set.step_angle,
                        },
                    );
                }
            }
        }
    }

    // 4. Apply customizedBoneAdjust, then autoBoneAxisAdjust(), then high-heels & ShoesOffset (`vmdlib.py:2776-2878, 3428-3495`), and charScale
    for (bname, rot_adj) in &params.custom_rot_adj {
        if let Some(adj) = bone_adjusts.get_mut(&bname.to_ascii_lowercase()) {
            adj.set_rot_adjustment(*rot_adj);
            adj.rot_adj_auto = false;
        }
    }
    for (bname, axis_adj) in &params.custom_axis_adj {
        if let Some(adj) = bone_adjusts.get_mut(&bname.to_ascii_lowercase()) {
            adj.set_axis_adjustment(*axis_adj);
            adj.rot_axis_auto = false;
        }
    }
    for bname in &ini.bone_names {
        let rot_axis_auto = bone_adjusts.get(bname).map_or(false, |a| a.rot_axis_auto);
        if rot_axis_auto {
            if let Some(pname) = ini.bones.get(bname).and_then(|b| b.parent.as_ref()) {
                if let Some(p_adj) = bone_adjusts.get(pname) {
                    let auto_adj = quat_mul(qinv(p_adj.rot_adjustment), p_adj.rot_axis_adjustment);
                    let euler = quat_to_unity_euler(auto_adj);
                    if let Some(adj) = bone_adjusts.get_mut(bname) {
                        adj.set_axis_adjustment(euler);
                    }
                }
            }
        }
    }
    for (bname, &rscale) in &params.custom_rot_scale {
        if let Some(adj) = bone_adjusts.get_mut(&bname.to_ascii_lowercase()) {
            adj.rotation_scale = rscale;
        }
    }
    for (bname, &bpos) in &params.custom_base_pos {
        if let Some(adj) = bone_adjusts.get_mut(&bname.to_ascii_lowercase()) {
            adj.base_position = bpos;
        }
    }
    for (bname, &poff) in &params.custom_pos_offset {
        if let Some(adj) = bone_adjusts.get_mut(&bname.to_ascii_lowercase()) {
            adj.position_offset = poff;
        }
    }
    for (bname, &pscale) in &params.custom_pos_scale {
        if let Some(adj) = bone_adjusts.get_mut(&bname.to_ascii_lowercase()) {
            adj.set_pos_local_scale(pscale);
        }
    }
    // `setHeelzRotation` + `setShoesOffset` run AFTER `autoBoneAxisAdjust()` in `vmdlib.py` without re-running axis propagation.
    let (eff_heel_rot, eff_root_y_offset) = resolve_high_heels_and_offset(params, rig);
    let has_custom_rot = |target: &str| {
        params
            .custom_rot_adj
            .keys()
            .any(|k| k.eq_ignore_ascii_case(target))
    };
    if let Some([foot_adj, heel_adj, toes_adj]) = eff_heel_rot {
        for bname in ["foot_d_l", "foot_d_r"] {
            if !has_custom_rot(bname) {
                if let Some(adj) = bone_adjusts.get_mut(bname) {
                    adj.set_rot_adjustment([foot_adj, 0.0, 0.0]);
                    adj.rot_adj_auto = false;
                }
            }
        }
        for bname in ["heel_ex_l", "heel_ex_r"] {
            if !has_custom_rot(bname) {
                if let Some(adj) = bone_adjusts.get_mut(bname) {
                    adj.set_rot_adjustment([heel_adj, 0.0, 0.0]);
                    adj.rot_adj_auto = false;
                }
            }
        }
        for bname in ["toe_ex_l", "toe_ex_r"] {
            if !has_custom_rot(bname) {
                if let Some(adj) = bone_adjusts.get_mut(bname) {
                    adj.set_rot_adjustment([toes_adj, 0.0, 0.0]);
                    adj.rot_adj_auto = false;
                }
            }
        }
    }
    if let Some(offset_y) = eff_root_y_offset {
        if let Some(adj) = bone_adjusts.get_mut("root") {
            adj.base_position = [0.0, offset_y, 0.0];
        }
    }
    let effective_char_scale = if params.char_scale > 0.0 {
        params.char_scale
    } else {
        rig.map_or(1.0, MmddCharacterRig::auto_char_scale)
    };
    let global_scale = ini.axis_scale * effective_char_scale;
    for adj in bone_adjusts.values_mut() {
        adj.set_pos_global_scale(global_scale);
    }

    // 5. Group VMD keyframes by dummy bone (starting from setInitFrame default keyframe)
    let mut vmd_to_bone = ini.vmd_to_bone.clone();
    // Wildcard matching (if any vmd_bone ends with '?')
    let wildcards: Vec<(String, String)> = vmd_to_bone
        .iter()
        .filter(|(k, _)| k.ends_with('?'))
        .map(|(k, v)| (k.clone(), v.clone()))
        .collect();
    for (wc_vmd, bname) in wildcards {
        let prefix = &wc_vmd[..wc_vmd.len() - 1];
        if let Some(matched) = vmd
            .bone_keyframes
            .iter()
            .find(|kf| kf.name.starts_with(prefix))
            .map(|kf| kf.name.clone())
        {
            vmd_to_bone.remove(&wc_vmd);
            vmd_to_bone.insert(matched, bname);
        }
    }

    let mut bone_kfs: HashMap<usize, Vec<VmdBoneKeyFrame>> = HashMap::new();
    for (idx, _) in ini.bone_names.iter().enumerate() {
        bone_kfs.insert(idx, vec![VmdBoneKeyFrame::default()]);
    }
    let mut seen_bones: HashMap<usize, std::collections::HashSet<u32>> = HashMap::new();
    for kf in &vmd.bone_keyframes {
        let Some(bname) = vmd_to_bone.get(&kf.name) else {
            continue;
        };
        let Some(&idx) = tree.by_name.get(bname) else {
            continue;
        };
        let set = seen_bones.entry(idx).or_default();
        if set.is_empty() {
            bone_kfs.insert(idx, Vec::new());
        }
        if set.insert(kf.frame_no) {
            bone_kfs.get_mut(&idx).unwrap().push(kf.clone());
        }
    }
    for kfs in bone_kfs.values_mut() {
        kfs.sort_by_key(|k| k.frame_no);
    }

    // Apply modifiedKeyframes from MMDDv2 if any
    for (op, vmd_name, fno, new_kf) in &params.modified_keyframes {
        let Some(bname) = vmd_to_bone.get(vmd_name) else {
            continue;
        };
        let Some(&idx) = tree.by_name.get(bname) else {
            continue;
        };
        let list = bone_kfs.entry(idx).or_default();
        if op == "del" {
            list.retain(|k| k.frame_no != *fno);
            if list.is_empty() {
                list.push(VmdBoneKeyFrame::default());
            }
        } else if let Some(kf) = new_kf {
            if let Some(existing) = list.iter_mut().find(|k| k.frame_no == *fno) {
                *existing = kf.clone();
            } else {
                list.push(kf.clone());
                list.sort_by_key(|k| k.frame_no);
            }
        }
    }

    let mut max_frame_no = bone_kfs
        .values()
        .filter_map(|v| v.last().map(|k| k.frame_no))
        .max()
        .unwrap_or(0);
    if let Some(last_ikv) = vmd.ik_keyframes.last() {
        max_frame_no = max_frame_no.max(last_ikv.frame_no);
    }
    if let Some(last_mkf) = vmd.morph_keyframes.iter().map(|k| k.frame_no).max() {
        max_frame_no = max_frame_no.max(last_mkf);
    }
    const SUB_STEPS: usize = 2;
    let delay_frames = params.delay * 30.0;
    let total_vmd_frames = (max_frame_no as usize + 1 + delay_frames.max(0.0).ceil() as usize).max(2);
    let frames = (total_vmd_frames - 1) * SUB_STEPS + 1;

    // 6. Resort weight bones in `track_rest` tree depth order (parents before children), skipping `ROOT`
    // so `cf_j_root` stays at identity and `cf_j_hips` captures the full root+center+groove+waist+hips motion.
    let tracks = family_tracks(track_rest);
    let hips_id = tracks
        .iter()
        .find(|(_, t)| t.bone == "cf_j_hips")
        .map(|(id, _)| *id)
        .ok_or("no cf_j_hips")?;
    let hips_rest = track_rest.tfs[&hips_id].pos;
    let hips_len = vlen(hips_rest).max(1e-4);
    let hips_parent_rest_world = track_rest.tfs[&hips_id]
        .parent
        .map(|p| crate::pose::world_rotation_of(p, &track_rest.tfs))
        .unwrap_or([0.0, 0.0, 0.0, 1.0]);

    // Map each `cf_j_*` track to its driving dummy bone (if any)
    let mut weight_bone_to_dummy: HashMap<String, (usize, bool, bool, bool)> = HashMap::new();
    for (idx, bname) in ini.bone_names.iter().enumerate() {
        if bname == "root" {
            continue;
        }
        let bdef = &ini.bones[bname];
        if let Some(wb) = &bdef.weight_bone {
            let sync_pos = bdef.is_position || (bdef.is_track && bdef.track_ptn.contains('p'));
            let sync_rot = bdef.is_rotation || (bdef.is_track && bdef.track_ptn.contains('r'));
            weight_bone_to_dummy.insert(
                wb.to_ascii_lowercase(),
                (idx, sync_pos, sync_rot, bdef.weight_sync_local),
            );
        }
    }
    let hips_dummy = weight_bone_to_dummy.get("cf_j_hips").copied();

    let eye_l_idx = tree.by_name.get("eye_l").copied();
    let eye_r_idx = tree.by_name.get("eye_r").copied();
    let mut gaze_lh_out = Vec::with_capacity(frames);
    let mut gaze_rh_out = Vec::with_capacity(frames);
    let mut gaze_v_out = Vec::with_capacity(frames);

    let mut rot_out = Vec::with_capacity(frames * tracks.len());
    let mut hips_out = Vec::with_capacity(frames);
    let mut last_rot: Vec<Option<Quat>> = vec![None; tracks.len()];
    let mut work_body = track_rest.clone();

    // 7. Sub-frame (60fps) evaluation with MMD cubic Bezier interpolation & per-step CCD IK
    for s in 0..frames {
        let frame_no = (s as f32 / SUB_STEPS as f32 - delay_frames).max(0.0);
        for (idx, bname) in ini.bone_names.iter().enumerate() {
            let bdef = &ini.bones[bname];
            let adj = &bone_adjusts[bname];
            let kfs = &bone_kfs[&idx];
            let (raw_pos, raw_rot) = sample_vmd_bone_kfs(kfs, frame_no, params.interpolate_consecutive);

            if bdef.is_rotation || (bdef.is_track && bdef.track_ptn.contains('r')) {
                let mut brot = adj.get_adjusted_rotation(raw_rot);
                if bdef.is_limit_axis {
                    let (mut angle, axis) = quat_to_angle_axis_deg(brot);
                    if vlen(vadd(axis, bdef.limit_ref_axis)) < 1.0 {
                        angle = -angle;
                    }
                    brot = quat_from_angle_axis_deg(angle, bdef.limit_axis);
                }
                tree.set_local_rot(idx, brot);
            }
            if bdef.is_position || (bdef.is_track && bdef.track_ptn.contains('p')) {
                let bpos = adj.get_adjusted_position(raw_pos);
                tree.set_local_pos(idx, bpos);
            }
            if bdef.is_ik {
                if let Some(solver) = ik_solvers.get(&idx) {
                    if is_ik_enabled_at_frame(&vmd.ik_keyframes, &bdef.vmd_bone, frame_no) {
                        solver.solve(&mut tree);
                    }
                }
            }
            if bdef.is_track {
                if let Some(&src_idx) = tree.by_name.get(&bdef.track_src) {
                    if bdef.track_ptn.contains('r') {
                        let rot_base = tree.nodes[idx].local_rot;
                        let rot_src = tree.nodes[src_idx].local_rot;
                        let scaled =
                            quat_slerp_unclamped([0.0, 0.0, 0.0, 1.0], rot_src, bdef.track_scale);
                        tree.set_local_rot(idx, quat_mul(scaled, rot_base));
                    }
                    if bdef.track_ptn.contains('p') {
                        let pos_base = tree.nodes[idx].local_pos;
                        let pos_src = tree.nodes[src_idx].local_pos;
                        tree.set_local_pos(
                            idx,
                            vadd(pos_base, vscale(pos_src, bdef.track_scale)),
                        );
                    }
                }
            }
        }

        // Extract eye gaze rates from `EYE_L` and `EYE_R` (which inherit `EYES` via TrackBone)
        let wrap180 = |deg: f32| if deg > 180.0 { deg - 360.0 } else { deg };
        let (lh, vl) = eye_l_idx.map_or((0.0, 0.0), |idx| {
            let e = quat_to_unity_euler(tree.nodes[idx].local_rot);
            (
                (wrap180(e[1]) / 15.0).clamp(-1.0, 1.0),
                (-wrap180(e[0]) / 12.0).clamp(-1.0, 1.0),
            )
        });
        let (rh, vr) = eye_r_idx.map_or((0.0, 0.0), |idx| {
            let e = quat_to_unity_euler(tree.nodes[idx].local_rot);
            (
                (wrap180(e[1]) / 15.0).clamp(-1.0, 1.0),
                (-wrap180(e[0]) / 12.0).clamp(-1.0, 1.0),
            )
        });
        gaze_lh_out.push(lh);
        gaze_rh_out.push(rh);
        gaze_v_out.push(0.5 * (vl + vr));

        // Twist disperse
        for td in &ini.twist_disperse {
            if !td.enable {
                continue;
            }
            let (Some(&t_idx), Some(&d_idx)) = (
                tree.by_name.get(&td.twist_bone),
                tree.by_name.get(&td.disperse_bone),
            ) else {
                continue;
            };
            let org_q = tree.nodes[t_idx].local_rot;
            if org_q != [0.0, 0.0, 0.0, 1.0] {
                let remain_q = quat_lerp([0.0, 0.0, 0.0, 1.0], org_q, 1.0 - td.disperse_rate);
                let disperse_q = quat_mul(org_q, qinv(remain_q));
                tree.set_local_rot(t_idx, remain_q);
                let d_rot = quat_mul(tree.nodes[d_idx].local_rot, disperse_q);
                tree.set_local_rot(d_idx, d_rot);
            }
        }

        // Sync dummy bones to `work_body` in parent-before-child (`tracks`) order
        work_body.restore_from(track_rest);
        for &(id, ref track) in &tracks {
            let key = track.bone.to_ascii_lowercase();
            let Some(&(d_idx, sync_pos, sync_rot, sync_local)) = weight_bone_to_dummy.get(&key)
            else {
                continue;
            };
            if sync_local {
                if let Some(tf) = work_body.tfs.get_mut(&id) {
                    if sync_pos {
                        tf.pos = tree.nodes[d_idx].local_pos;
                    }
                    if sync_rot {
                        tf.rot = tree.nodes[d_idx].local_rot;
                    }
                }
            } else {
                let (p_pos, p_rot) = match work_body.tfs.get(&id).and_then(|t| t.parent) {
                    Some(pid) if work_body.tfs.contains_key(&pid) => {
                        let m = crate::pose::world_of_id(pid, &work_body.tfs);
                        let r = crate::pose::world_rotation_of(pid, &work_body.tfs);
                        (mat_pos(&m), r)
                    }
                    _ => ([0.0; 3], [0.0, 0.0, 0.0, 1.0]),
                };
                if let Some(tf) = work_body.tfs.get_mut(&id) {
                    if sync_pos {
                        tf.pos = qrot(qinv(p_rot), vsub(tree.nodes[d_idx].world_pos, p_pos));
                    }
                    if sync_rot {
                        tf.rot = qnorm(quat_mul(qinv(p_rot), tree.nodes[d_idx].world_rot));
                    }
                }
            }
        }

        // Record OkAnim frame
        for (k, &(id, ref track)) in tracks.iter().enumerate() {
            let local = work_body
                .tfs
                .get(&id)
                .map(|t| t.rot)
                .unwrap_or(track.rest_local);
            let mut d = qnorm(qmul(qinv(track.rest_local), local));
            if let Some(prev) = last_rot[k] {
                if qdot(prev, d) < 0.0 {
                    d = [-d[0], -d[1], -d[2], -d[3]];
                }
            }
            last_rot[k] = Some(d);
            rot_out.push(d);
        }
        let hips_pos = if let Some((d_idx, true, _, false)) = hips_dummy {
            let w_hips = tree.nodes[d_idx].world_pos;
            let pos_after_abm = inv_hips_parent_live
                .as_ref()
                .map_or(w_hips, |inv| crate::pose::mat_vec(inv, w_hips));
            match &hips_abm {
                Some(d) if d.length.abs() > 1e-6 => {
                    std::array::from_fn(|i| (pos_after_abm[i] - d.pos[i]) / d.length)
                }
                _ => pos_after_abm,
            }
        } else {
            work_body
                .tfs
                .get(&hips_id)
                .map(|t| t.pos)
                .unwrap_or(hips_rest)
        };
        hips_out.push([
            hips_pos[0] / hips_len,
            hips_pos[1] / hips_len,
            hips_pos[2] / hips_len,
        ]);
    }

    let fps = (30.0 * SUB_STEPS as f32 * params.playback_speed).max(1.0);
    let mut curves = evaluate_vmd_morph_curves(&vmd.morph_keyframes, frames, SUB_STEPS, delay_frames);
    if gaze_lh_out
        .iter()
        .chain(&gaze_rh_out)
        .chain(&gaze_v_out)
        .any(|v| v.abs() > 1e-4)
    {
        curves.push(crate::okanim::Curve {
            name: "face.eye_gaze_lh".into(),
            values: gaze_lh_out,
        });
        curves.push(crate::okanim::Curve {
            name: "face.eye_gaze_rh".into(),
            values: gaze_rh_out,
        });
        curves.push(crate::okanim::Curve {
            name: "face.eye_gaze_v".into(),
            values: gaze_v_out,
        });
    }
    let mut meta = BTreeMap::new();
    meta.insert("source".into(), "mmdd_vmd".into());
    meta.insert("name".into(), source_name.to_string());
    meta.insert("char_scale".into(), format!("{effective_char_scale:.6}"));
    let anim = OkAnim {
        family: FAMILY.into(),
        fps,
        frames: frames as u32,
        looping: params.looping,
        scene: 4,
        hips_bone: "cf_j_hips".into(),
        hips_rest,
        hips_parent_rest_world,
        tracks: tracks.into_iter().map(|(_, t)| t).collect(),
        rot: rot_out,
        hips: hips_out,
        curves,
        contacts: Vec::new(),
        meta,
    };
    anim.validate()?;
    Ok(anim)
}

fn sample_vmd_morph_kfs(kfs: &[VmdMorphKeyFrame], frame_no: f32) -> f32 {
    if kfs.is_empty() {
        return 0.0;
    }
    let first = &kfs[0];
    if frame_no <= first.frame_no as f32 {
        return first.weight;
    }
    let last = kfs.last().unwrap();
    if frame_no >= last.frame_no as f32 {
        return last.weight;
    }
    let idx = kfs.partition_point(|k| (k.frame_no as f32) <= frame_no);
    let prev = &kfs[idx.saturating_sub(1)];
    if (prev.frame_no as f32 - frame_no).abs() < 1e-5 || idx >= kfs.len() {
        return prev.weight;
    }
    let next = &kfs[idx];
    let span = (next.frame_no - prev.frame_no).max(1) as f32;
    let t = ((frame_no - prev.frame_no as f32) / span).clamp(0.0, 1.0);
    prev.weight + (next.weight - prev.weight) * t
}

struct MmddFbsTables {
    eyeset_to_fbs: HashMap<i32, i32>,
    sinken_brow_ptn: i32,
}

fn mmdd_fbs_tables() -> &'static MmddFbsTables {
    static TABLES: OnceLock<MmddFbsTables> = OnceLock::new();
    TABLES.get_or_init(|| {
        let mut eyeset_to_fbs: HashMap<i32, i32> =
            HashMap::from([(22, 0), (24, 0), (25, 0), (30, 15), (31, 16)]);
        let mut sinken_brow_ptn = 0;
        for root in mmdd_resource_candidates() {
            if root.join("oklib.json").is_file() {
                let _ = crate::oklib::ensure_library(&root);
            }
            let mut got_any = false;
            for &id in &[22, 24, 25, 30, 31] {
                if let Some(set) = crate::eye_set::eye_set(&root, id) {
                    if set.id == id {
                        eyeset_to_fbs.insert(id, set.eyes_ptn.max(0));
                        got_any = true;
                    }
                }
            }
            let bo_head = root.join("abdata/chara/bo_head_00.unity3d");
            if let Ok(bytes) = crate::gamefs::read_source(&bo_head) {
                let parsed = crate::unity::parse_bundle_cached(bytes);
                let brow_map = parsed.face_control_patterns("p_cf_head_00", "EyebrowCtrl");
                if let Some((_, (_, brow_ptns))) = brow_map
                    .iter()
                    .find(|(k, _)| k.contains("mayuge") || k.contains("face"))
                {
                    let (meshes, _) = parsed.meshes_for_maindata("p_cf_head_00");
                    if let Some(m) = meshes
                        .iter()
                        .find(|m| m.name.to_ascii_lowercase().contains("mayuge"))
                    {
                        for (ptn_idx, &(c, o)) in brow_ptns.iter().enumerate() {
                            let check = |bi: i32| -> bool {
                                bi >= 0
                                    && m.blend_shapes
                                        .get(bi as usize)
                                        .is_some_and(|(n, _)| {
                                            let nl = n.to_ascii_lowercase();
                                            nl.contains("sinken") || nl.contains("majime")
                                        })
                            };
                            if check(c) || check(o) {
                                sinken_brow_ptn = ptn_idx as i32;
                                break;
                            }
                        }
                    }
                }
            }
            if got_any {
                break;
            }
        }
        MmddFbsTables {
            eyeset_to_fbs,
            sinken_brow_ptn,
        }
    })
}

fn evaluate_vmd_morph_curves(
    morph_kfs: &[VmdMorphKeyFrame],
    frames: usize,
    sub_steps: usize,
    delay_frames: f32,
) -> Vec<crate::okanim::Curve> {
    if morph_kfs.is_empty() || frames == 0 {
        return Vec::new();
    }
    let mut by_name: HashMap<String, Vec<VmdMorphKeyFrame>> = HashMap::new();
    for kf in morph_kfs {
        if kf.name.is_empty() {
            continue;
        }
        by_name.entry(kf.name.clone()).or_default().push(kf.clone());
    }
    for list in by_name.values_mut() {
        list.sort_by_key(|k| k.frame_no);
        list.dedup_by_key(|k| k.frame_no);
        if list.first().map_or(true, |k| k.frame_no > 0) {
            list.insert(
                0,
                VmdMorphKeyFrame {
                    name: String::new(),
                    frame_no: 0,
                    weight: 0.0,
                },
            );
        }
    }

    let tables = mmdd_fbs_tables();
    let eyeset_ptn = |id: i32| -> i32 { tables.eyeset_to_fbs.get(&id).copied().unwrap_or(0) };

    // Primary mouth shapes (vowels, tongue, stylized shapes): (vmd_name, pattern, open_at_0, open_at_1)
    const MOUTH_PRIMARY_DEFS: &[(&str, i32, f32, f32)] = &[
        ("あ", 26, 0.0, 1.0),
        ("い", 28, 0.0, 1.0),
        ("う", 30, 0.0, 1.0),
        ("え", 31, 0.0, 1.0),
        ("お", 33, 0.0, 1.0),
        ("わ", 2, 0.0, 1.0),
        ("ワ", 2, 0.0, 1.0),
        ("ん", 36, 0.0, 1.0),
        ("あ２", 1, 0.0, 1.0),
        ("あ2", 1, 0.0, 1.0),
        ("ぺろっ", 20, 0.0, 1.0),
        ("てへぺろ", 20, 0.0, 1.0),
        ("てへぺろ２", 20, 0.0, 1.0),
        ("てへぺろ2", 20, 0.0, 1.0),
        ("∧", 7, 0.0, 0.0),
        ("▲", 38, 0.0, 1.0),
        ("ω", 37, 0.0, 0.0),
        ("ω□", 37, 1.0, 1.0),
    ];

    let sym_eye_defs: [(&str, i32); 10] = [
        ("じと目", 10),
        ("ｷﾘｯ", 9),
        ("キリッ", 9),
        ("なごみ", eyeset_ptn(31)),
        ("はぅ", eyeset_ptn(30)),
        ("はちゅ目", eyeset_ptn(22)),
        ("星目", eyeset_ptn(24)),
        ("はぁと", eyeset_ptn(25)),
        ("ハート", eyeset_ptn(25)),
        ("びっくり", 17),
    ];

    let brow_defs: [(&str, i32); 4] = [
        ("困る", 13),
        ("怒り", 1),
        ("にこり", 12),
        ("真面目", tables.sinken_brow_ptn),
    ];

    let has_any = |names: &[&str]| names.iter().any(|&n| by_name.contains_key(n));
    let max_morph = |fno: f32, names: &[&str]| -> f32 {
        let mut m = 0.0f32;
        for &n in names {
            if let Some(kfs) = by_name.get(n) {
                m = m.max(sample_vmd_morph_kfs(kfs, fno).clamp(0.0, 1.0));
            }
        }
        m
    };

    let emit_blush = has_any(&["照れ", "赤面", "照れ２", "照れ2"]);
    let emit_iris_small = has_any(&["瞳小", "瞳大"]);
    let emit_brow_offset = has_any(&["上", "下"]);
    let emit_mouth_wider = has_any(&["口横広げ"]);

    let mut mouth_ptn = Vec::with_capacity(frames);
    let mut mouth_open = Vec::with_capacity(frames);
    let mut mouth_weight = Vec::with_capacity(frames);
    let mut mouth_corner_up = Vec::with_capacity(frames);
    let mut mouth_corner_down = Vec::with_capacity(frames);
    let mut mouth_wider = Vec::with_capacity(if emit_mouth_wider { frames } else { 0 });

    let mut eye_ptn = Vec::with_capacity(frames);
    let mut eye_open = Vec::with_capacity(frames);
    let mut eye_open_l = Vec::with_capacity(frames);
    let mut eye_open_r = Vec::with_capacity(frames);
    let mut eye_wink_l_happy = Vec::with_capacity(frames);
    let mut eye_wink_r_happy = Vec::with_capacity(frames);
    let mut eye_expr_weight = Vec::with_capacity(frames);
    let mut eye_wide = Vec::with_capacity(frames);

    let mut brow_ptn = Vec::with_capacity(frames);
    let mut brow_weight = Vec::with_capacity(frames);
    let mut brow_offset_y = Vec::with_capacity(if emit_brow_offset { frames } else { 0 });

    let mut blush_vals = Vec::with_capacity(if emit_blush { frames } else { 0 });
    let mut iris_small_vals = Vec::with_capacity(if emit_iris_small { frames } else { 0 });

    let step_div = sub_steps.max(1) as f32;

    for f in 0..frames {
        let fno = (f as f32 / step_div - delay_frames).max(0.0);

        // 1. Mouth: primary vowel/shape + additive corner smile/frown (`にやり`/`口角上げ` + `口角下げ`)
        let mut best_mouth: Option<(f32, i32, f32)> = None;
        for &(mname, ptn, r0, r1) in MOUTH_PRIMARY_DEFS {
            if let Some(kfs) = by_name.get(mname) {
                let w = sample_vmd_morph_kfs(kfs, fno).clamp(0.0, 1.0);
                if w > 0.001 && best_mouth.map_or(true, |(bw, _, _)| w > bw) {
                    let open = (r0 + (r1 - r0) * w).clamp(0.0, 1.0);
                    best_mouth = Some((w, ptn, open));
                }
            }
        }
        let w_nikkori = max_morph(fno, &["にっこり"]);
        let w_niyari = max_morph(fno, &["にやり", "にやり２", "にやり2", "口角上げ"]);
        let w_frown = max_morph(fno, &["口角下げ"]);

        if let Some((w, ptn, open)) = best_mouth {
            mouth_ptn.push(ptn as f32);
            mouth_open.push(open);
            // Closed-shape primary morphs (`∧`, `ω`) blend smoothly via `mouth_weight`; open vowels scale via `open`.
            let mw = if open > 1e-4 { 1.0 } else { w };
            mouth_weight.push(mw);
            mouth_corner_up.push(w_niyari.max(w_nikkori));
            mouth_corner_down.push(w_frown);
        } else if w_nikkori > w_niyari && w_nikkori > 0.001 {
            mouth_ptn.push(19.0);
            mouth_open.push(0.0);
            mouth_weight.push(w_nikkori);
            mouth_corner_up.push(w_niyari);
            mouth_corner_down.push(w_frown);
        } else {
            mouth_ptn.push(0.0);
            mouth_open.push(0.0);
            mouth_weight.push(1.0);
            mouth_corner_up.push(w_niyari);
            mouth_corner_down.push(w_frown);
        }
        if emit_mouth_wider {
            mouth_wider.push(max_morph(fno, &["口横広げ"]));
        }

        // 2. Eyes: independent L/R openness, normal (`wink*_op`) vs happy (`gyu*_op`) wink, `笑い` closing to `egao_cl`, `びっくり` widening
        let w_blink = max_morph(fno, &["まばたき"]);
        let w_wink_l_norm = max_morph(fno, &["ウィンク", "ｳｨﾝｸ"]);
        let w_wink_r_norm = max_morph(fno, &["ウィンク右", "ｳｨﾝｸ右"]);
        let w_wink_l_hap = max_morph(fno, &["ウィンク２", "ｳｨﾝｸ２", "ウィンク2", "ｳｨﾝｸ2"]);
        let w_wink_r_hap = max_morph(fno, &["ｳｨﾝｸ２右", "ウィンク２右", "ｳｨﾝｸ2右", "ウィンク2右"]);
        let w_smile1 = max_morph(fno, &["笑い"]);
        let w_smile2 = max_morph(fno, &["笑い目"]);
        let w_surprise = max_morph(fno, &["びっくり"]);

        let close_l_norm = w_blink.max(w_wink_l_norm);
        let close_l_hap = w_wink_l_hap.max(w_smile1).max(w_smile2);
        let close_l = close_l_norm.max(close_l_hap).clamp(0.0, 1.0);
        let happy_l = if close_l_hap <= 1e-5 {
            0.0
        } else if close_l_norm <= 1e-5 {
            1.0
        } else {
            (close_l_hap / (close_l_hap + close_l_norm)).clamp(0.0, 1.0)
        };

        let close_r_norm = w_blink.max(w_wink_r_norm);
        let close_r_hap = w_wink_r_hap.max(w_smile1).max(w_smile2);
        let close_r = close_r_norm.max(close_r_hap).clamp(0.0, 1.0);
        let happy_r = if close_r_hap <= 1e-5 {
            0.0
        } else if close_r_norm <= 1e-5 {
            1.0
        } else {
            (close_r_hap / (close_r_hap + close_r_norm)).clamp(0.0, 1.0)
        };

        let open_l = (1.0 - close_l).clamp(0.0, 1.0);
        let open_r = (1.0 - close_r).clamp(0.0, 1.0);

        let mut best_sym_eye: Option<(f32, i32)> = None;
        for &(ename, ptn) in &sym_eye_defs {
            if ptn <= 0 {
                continue;
            }
            if let Some(kfs) = by_name.get(ename) {
                let w = sample_vmd_morph_kfs(kfs, fno).clamp(0.0, 1.0);
                if w > 0.001 && best_sym_eye.map_or(true, |(bw, _)| w > bw) {
                    best_sym_eye = Some((w, ptn));
                }
            }
        }
        // `笑い` (3) and `笑い目` (4) also provide open-half expression shaping at partial weights
        for (w_sm, ptn_sm) in [(w_smile1, 3), (w_smile2, 4)] {
            if w_sm > 0.001 && best_sym_eye.map_or(true, |(bw, _)| w_sm > bw) {
                best_sym_eye = Some((w_sm, ptn_sm));
            }
        }

        if let Some((w_expr, ptn)) = best_sym_eye {
            eye_ptn.push(ptn as f32);
            eye_expr_weight.push(w_expr);
        } else {
            eye_ptn.push(0.0);
            eye_expr_weight.push(0.0);
        }
        eye_open.push(0.5 * (open_l + open_r));
        eye_open_l.push(open_l);
        eye_open_r.push(open_r);
        eye_wink_l_happy.push(happy_l);
        eye_wink_r_happy.push(happy_r);
        eye_wide.push(w_surprise);

        // 3. Eyebrow: smooth weight interpolation (`face.brow_weight`) + optional vertical bone offset (`上`/`下`)
        let mut best_brow: Option<(f32, i32)> = None;
        for &(bname, ptn) in &brow_defs {
            if ptn <= 0 {
                continue;
            }
            if let Some(kfs) = by_name.get(bname) {
                let w = sample_vmd_morph_kfs(kfs, fno).clamp(0.0, 1.0);
                if w > 0.001 && best_brow.map_or(true, |(bw, _)| w > bw) {
                    best_brow = Some((w, ptn));
                }
            }
        }
        if let Some((w_b, ptn)) = best_brow {
            brow_ptn.push(ptn as f32);
            brow_weight.push(w_b);
        } else {
            brow_ptn.push(0.0);
            brow_weight.push(0.0);
        }
        if emit_brow_offset {
            let w_up = max_morph(fno, &["上"]);
            let w_down = max_morph(fno, &["下"]);
            // chara_default_KKS.json: eyebrow_base translation [0, ±0.060, 0] * axisScale (0.083)
            brow_offset_y.push((w_up - w_down) * (0.06 * 0.083));
        }

        // 4. Cheek blush (`照れ` / `赤面`) & iris shrink (`瞳小` / `瞳大`)
        if emit_blush {
            blush_vals.push(max_morph(fno, &["照れ", "赤面", "照れ２", "照れ2"]));
        }
        if emit_iris_small {
            let w_small = max_morph(fno, &["瞳小"]);
            let w_large = max_morph(fno, &["瞳大"]);
            iris_small_vals.push((w_small - w_large).clamp(-1.0, 1.0));
        }
    }

    let mut out = vec![
        crate::okanim::Curve {
            name: "face.mouth_pattern".into(),
            values: mouth_ptn,
        },
        crate::okanim::Curve {
            name: "face.mouth_open".into(),
            values: mouth_open,
        },
        crate::okanim::Curve {
            name: "face.mouth_weight".into(),
            values: mouth_weight,
        },
        crate::okanim::Curve {
            name: "face.mouth_corner_up".into(),
            values: mouth_corner_up,
        },
        crate::okanim::Curve {
            name: "face.mouth_corner_down".into(),
            values: mouth_corner_down,
        },
        crate::okanim::Curve {
            name: "face.eye_pattern".into(),
            values: eye_ptn,
        },
        crate::okanim::Curve {
            name: "face.eye_open".into(),
            values: eye_open,
        },
        crate::okanim::Curve {
            name: "face.eye_open_l".into(),
            values: eye_open_l,
        },
        crate::okanim::Curve {
            name: "face.eye_open_r".into(),
            values: eye_open_r,
        },
        crate::okanim::Curve {
            name: "face.eye_wink_l_happy".into(),
            values: eye_wink_l_happy,
        },
        crate::okanim::Curve {
            name: "face.eye_wink_r_happy".into(),
            values: eye_wink_r_happy,
        },
        crate::okanim::Curve {
            name: "face.eye_expr_weight".into(),
            values: eye_expr_weight,
        },
        crate::okanim::Curve {
            name: "face.eye_wide".into(),
            values: eye_wide,
        },
        crate::okanim::Curve {
            name: "face.brow_pattern".into(),
            values: brow_ptn,
        },
        crate::okanim::Curve {
            name: "face.brow_weight".into(),
            values: brow_weight,
        },
    ];
    if emit_brow_offset {
        out.push(crate::okanim::Curve {
            name: "face.brow_offset_y".into(),
            values: brow_offset_y,
        });
    }
    if emit_mouth_wider {
        out.push(crate::okanim::Curve {
            name: "face.mouth_wider".into(),
            values: mouth_wider,
        });
    }
    if emit_blush {
        out.push(crate::okanim::Curve {
            name: "face.blush".into(),
            values: blush_vals,
        });
    }
    if emit_iris_small {
        out.push(crate::okanim::Curve {
            name: "face.iris_small".into(),
            values: iris_small_vals,
        });
    }
    out
}

fn sample_vmd_bone_kfs(
    kfs: &[VmdBoneKeyFrame],
    frame_no: f32,
    interpolate_consecutive: bool,
) -> (V3, Quat) {
    if kfs.is_empty() {
        return ([0.0; 3], [0.0, 0.0, 0.0, 1.0]);
    }
    let first = &kfs[0];
    if frame_no <= first.frame_no as f32 {
        return (first.pos, first.rot);
    }
    let last = kfs.last().unwrap();
    if frame_no >= last.frame_no as f32 {
        return (last.pos, last.rot);
    }
    let idx = kfs.partition_point(|k| (k.frame_no as f32) <= frame_no);
    let prev = &kfs[idx.saturating_sub(1)];
    if (prev.frame_no as f32 - frame_no).abs() < 1e-5 || idx >= kfs.len() {
        return (prev.pos, prev.rot);
    }
    let next = &kfs[idx];
    if !interpolate_consecutive && prev.frame_no + 1 == next.frame_no {
        return (prev.pos, prev.rot);
    }
    let span = (next.frame_no - prev.frame_no).max(1) as f32;
    let t = ((frame_no - prev.frame_no as f32) / span).clamp(0.0, 1.0);
    let tx = mmd_cubic_bezier(&next.x_curve, t);
    let ty = mmd_cubic_bezier(&next.y_curve, t);
    let tz = mmd_cubic_bezier(&next.z_curve, t);
    let tr = mmd_cubic_bezier(&next.r_curve, t);
    let pos = [
        prev.pos[0] + (next.pos[0] - prev.pos[0]) * tx,
        prev.pos[1] + (next.pos[1] - prev.pos[1]) * ty,
        prev.pos[2] + (next.pos[2] - prev.pos[2]) * tz,
    ];
    let rot = quat_slerp(prev.rot, next.rot, tr);
    (pos, rot)
}

fn is_ik_enabled_at_frame(ik_kfs: &[VmdIkVisibleKeyFrame], vmd_bone: &str, frame_no: f32) -> bool {
    let mut enabled = true;
    for kf in ik_kfs {
        if (kf.frame_no as f32) <= frame_no {
            if let Some(&val) = kf.ik_bones.get(vmd_bone) {
                enabled = val;
            }
        } else {
            break;
        }
    }
    enabled
}

// ---------------------------------------------------------------------------
// CameraController VMD -> Studio Camera Keyframes
// ---------------------------------------------------------------------------

pub fn evaluate_camera_vmd(
    vmd: &VmdFile,
    delay: f32,
    playback_speed: f32,
    pos_scale: f32,
    dst_scale: f32,
    off_pos: V3,
    off_rot: V3,
) -> Vec<MmddCameraKeyEval> {
    if vmd.camera_keyframes.is_empty() {
        return Vec::new();
    }
    let speed = if playback_speed > 1e-4 {
        playback_speed
    } else {
        1.0
    };
    let has_off_rot = off_rot.iter().any(|v| v.abs() > 1e-5);
    let off_rot_q = euler_deg_to_quat(off_rot);

    // Sort and deduplicate camera keyframes by frame_no (matching VmdLib.loadVmd)
    let mut kfs = vmd.camera_keyframes.clone();
    kfs.sort_by_key(|k| k.frame_no);
    kfs.dedup_by_key(|k| k.frame_no);

    let make_eval = |frame_no: u32, time: f32, pos: V3, rot: V3, dist: f32, fov: f32| -> MmddCameraKeyEval {
        // Matches vmdlib.py::CameraController.applyOffsetScale (lines 7380-7394)
        let mut cpos = [
            -pos[0] * pos_scale,
            pos[1] * pos_scale,
            -pos[2] * pos_scale,
        ];
        let cdst = [0.0, 0.0, dist * dst_scale];
        let mut crot = [-rot[0], -rot[1] + 180.0, -rot[2]];
        if has_off_rot {
            cpos = qrot(off_rot_q, cpos);
            crot = quat_to_unity_euler(quat_mul(off_rot_q, euler_deg_to_quat(crot)));
        }
        cpos = vadd(cpos, off_pos);
        let cam_rot_q = euler_deg_to_quat(crot);
        let world_pos = vadd(qrot(cam_rot_q, cdst), cpos);
        MmddCameraKeyEval {
            time,
            frame_no,
            target_pos: cpos,
            distance: cdst,
            world_pos,
            rot: crot,
            fov,
            x_curve: [0.25, 0.25, 0.75, 0.75],
            y_curve: [0.25, 0.25, 0.75, 0.75],
            z_curve: [0.25, 0.25, 0.75, 0.75],
            r_curve: [0.25, 0.25, 0.75, 0.75],
            d_curve: [0.25, 0.25, 0.75, 0.75],
            v_curve: [0.25, 0.25, 0.75, 0.75],
        }
    };

    let max_frame = kfs.last().map(|k| k.frame_no).unwrap_or(0);
    let mut out = Vec::with_capacity(max_frame as usize + 16);
    let mut seg = 0usize;
    let fps = 30.0 * speed;

    for f in 0..=max_frame {
        while seg + 1 < kfs.len() && kfs[seg + 1].frame_no <= f {
            seg += 1;
        }
        let time = delay + (f as f32) / fps;
        if f <= kfs[0].frame_no {
            let kf = &kfs[0];
            out.push(make_eval(f, time, kf.pos, kf.rot, kf.dist, kf.view_angle));
        } else if seg + 1 >= kfs.len() || f == kfs[seg].frame_no {
            let kf = &kfs[seg];
            out.push(make_eval(f, time, kf.pos, kf.rot, kf.dist, kf.view_angle));
            // Camera cut: prevFrame.frameNo + 1 == nextFrame.frameNo
            // Emit a hold keyframe right before nextFrame so >30fps playback snaps cleanly without smearing
            if seg + 1 < kfs.len() && kf.frame_no + 1 == kfs[seg + 1].frame_no {
                let cut_time = delay + ((kfs[seg + 1].frame_no as f32) - 0.005) / fps;
                out.push(make_eval(f, cut_time, kf.pos, kf.rot, kf.dist, kf.view_angle));
            }
        } else {
            let prev = &kfs[seg];
            let next = &kfs[seg + 1];
            let span = (next.frame_no - prev.frame_no) as f32;
            let t = ((f - prev.frame_no) as f32 / span).clamp(0.0, 1.0);
            let tx = mmd_cubic_bezier(&next.x_curve, t);
            let ty = mmd_cubic_bezier(&next.y_curve, t);
            let tz = mmd_cubic_bezier(&next.z_curve, t);
            let tr = mmd_cubic_bezier(&next.r_curve, t);
            let td = mmd_cubic_bezier(&next.d_curve, t);
            let tv = mmd_cubic_bezier(&next.v_curve, t);
            let pos = [
                prev.pos[0] + (next.pos[0] - prev.pos[0]) * tx,
                prev.pos[1] + (next.pos[1] - prev.pos[1]) * ty,
                prev.pos[2] + (next.pos[2] - prev.pos[2]) * tz,
            ];
            let rot = [
                prev.rot[0] + (next.rot[0] - prev.rot[0]) * tr,
                prev.rot[1] + (next.rot[1] - prev.rot[1]) * tr,
                prev.rot[2] + (next.rot[2] - prev.rot[2]) * tr,
            ];
            let dist = prev.dist + (next.dist - prev.dist) * td;
            let fov = prev.view_angle + (next.view_angle - prev.view_angle) * tv;
            out.push(make_eval(f, time, pos, rot, dist, fov));
        }
    }
    out
}

// ---------------------------------------------------------------------------
// Scene MMDD & KKVMDPlay Extraction
// ---------------------------------------------------------------------------

fn parse_vec3_str(s: &str) -> Option<V3> {
    let nums: Vec<f32> = s
        .trim()
        .trim_matches(|c| c == '(' || c == ')' || c == '[' || c == ']')
        .split(',')
        .filter_map(|x| x.trim().parse().ok())
        .collect();
    if nums.len() == 3 {
        Some([nums[0], nums[1], nums[2]])
    } else {
        None
    }
}

fn val_to_v3(v: &serde_json::Value) -> Option<V3> {
    if let Some(s) = v.as_str() {
        return parse_vec3_str(s);
    }
    if let Some(a) = v.as_array() {
        if a.len() >= 3 {
            return Some([
                a[0].as_f64()? as f32,
                a[1].as_f64()? as f32,
                a[2].as_f64()? as f32,
            ]);
        }
    }
    if let Some(m) = v.as_object() {
        return Some([
            m.get("x")?.as_f64()? as f32,
            m.get("y")?.as_f64()? as f32,
            m.get("z")?.as_f64()? as f32,
        ]);
    }
    None
}

fn val_to_f32(v: &serde_json::Value, default: f32) -> f32 {
    v.as_f64()
        .map(|f| f as f32)
        .or_else(|| v.as_str().and_then(|s| s.trim().parse().ok()))
        .unwrap_or(default)
}

fn val_to_bool(v: &serde_json::Value, default: bool) -> bool {
    v.as_bool()
        .or_else(|| {
            v.as_str().map(|s| {
                s.eq_ignore_ascii_case("true") || s == "1" || s.eq_ignore_ascii_case("yes")
            })
        })
        .or_else(|| v.as_i64().map(|i| i != 0))
        .unwrap_or(default)
}

fn rmpv_to_json(v: &rmpv::Value) -> serde_json::Value {
    match v {
        rmpv::Value::Nil => serde_json::Value::Null,
        rmpv::Value::Boolean(b) => serde_json::Value::Bool(*b),
        rmpv::Value::Integer(i) => {
            if let Some(n) = i.as_i64() {
                serde_json::Value::Number(n.into())
            } else if let Some(u) = i.as_u64() {
                serde_json::Value::Number(u.into())
            } else {
                serde_json::Value::Null
            }
        }
        rmpv::Value::F32(f) => serde_json::Number::from_f64(*f as f64)
            .map(serde_json::Value::Number)
            .unwrap_or(serde_json::Value::Null),
        rmpv::Value::F64(f) => serde_json::Number::from_f64(*f)
            .map(serde_json::Value::Number)
            .unwrap_or(serde_json::Value::Null),
        rmpv::Value::String(s) => {
            serde_json::Value::String(s.as_str().unwrap_or_default().to_string())
        }
        rmpv::Value::Binary(b) => {
            if let Ok(val) = rmpv::decode::read_value(&mut &b[..]) {
                rmpv_to_json(&val)
            } else if let Ok(s) = std::str::from_utf8(b) {
                serde_json::Value::String(s.to_string())
            } else {
                serde_json::Value::Null
            }
        }
        rmpv::Value::Array(arr) => {
            serde_json::Value::Array(arr.iter().map(rmpv_to_json).collect())
        }
        rmpv::Value::Map(map) => {
            let mut out = serde_json::Map::new();
            for (k, val) in map {
                if let Some(ks) = k.as_str() {
                    out.insert(ks.to_string(), rmpv_to_json(val));
                } else {
                    out.insert(format!("{k}"), rmpv_to_json(val));
                }
            }
            serde_json::Value::Object(out)
        }
        rmpv::Value::Ext(_, _) => serde_json::Value::Null,
    }
}

fn parse_mmdd_folder_children(children: &[Object]) -> serde_json::Value {
    let mut map = serde_json::Map::new();
    for child in children {
        if let Object::Folder {
            name,
            children: sub,
            ..
        } = child
        {
            if let Some(rest) = name.strip_prefix('-') {
                if let Some(list_name) = rest.strip_suffix(':') {
                    let mut arr = Vec::new();
                    for item in sub {
                        if let Object::Folder { name: item_name, .. } = item {
                            arr.push(serde_json::Value::String(item_name.clone()));
                        }
                    }
                    map.insert(list_name.to_string(), serde_json::Value::Array(arr));
                } else {
                    map.insert(rest.to_string(), parse_mmdd_folder_children(sub));
                }
            } else if let Some((pname, pvalue)) = name.split_once(':') {
                map.insert(
                    pname.to_string(),
                    serde_json::Value::String(pvalue.to_string()),
                );
            }
        }
    }
    serde_json::Value::Object(map)
}

fn find_mmdd_folder_v1(objects: &[Object]) -> Option<serde_json::Value> {
    for o in objects {
        match o {
            Object::Folder { name, children, .. } => {
                if name == "-mmddirector" {
                    return Some(parse_mmdd_folder_children(children));
                }
                if let Some(found) = find_mmdd_folder_v1(children) {
                    return Some(found);
                }
            }
            Object::Item { children, .. } | Object::Route { children, .. } => {
                if let Some(found) = find_mmdd_folder_v1(children) {
                    return Some(found);
                }
            }
            _ => {}
        }
    }
    None
}

fn select_mmdd_mode_setting(root: serde_json::Value) -> Option<serde_json::Map<String, serde_json::Value>> {
    let serde_json::Value::Object(mut obj) = unwrap_scene_decoder(root) else {
        return None;
    };
    if obj.contains_key("motion")
        || obj.contains_key("motions")
        || obj.contains_key("camera")
        || obj.contains_key("cameras")
        || obj.contains_key("general")
    {
        return Some(obj);
    }
    let pick_key = if obj.contains_key("save") {
        Some("save".to_string())
    } else if obj.contains_key("backup") {
        Some("backup".to_string())
    } else {
        obj.keys()
            .find(|k| !k.starts_with('$'))
            .cloned()
    };
    let key = pick_key?;
    let mode_val = obj.remove(&key)?;
    match mode_val {
        serde_json::Value::Object(m) => Some(m),
        serde_json::Value::String(s) => parse_py_or_json(&s)
            .ok()
            .map(unwrap_scene_decoder)
            .and_then(|v| match v {
                serde_json::Value::Object(m) => Some(m),
                _ => None,
            }),
        _ => None,
    }
}

fn val_to_quat_xyzw(v: &serde_json::Value) -> Option<Quat> {
    if let Some(s) = v.as_str() {
        let nums: Vec<f32> = s
            .trim()
            .trim_matches(|c| c == '(' || c == ')' || c == '[' || c == ']')
            .split(',')
            .filter_map(|x| x.trim().parse().ok())
            .collect();
        if nums.len() == 4 {
            return Some([nums[0], nums[1], nums[2], nums[3]]);
        }
        return None;
    }
    if let Some(a) = v.as_array() {
        if a.len() >= 4 {
            return Some([
                a[0].as_f64()? as f32,
                a[1].as_f64()? as f32,
                a[2].as_f64()? as f32,
                a[3].as_f64()? as f32,
            ]);
        }
    }
    if let Some(m) = v.as_object() {
        return Some([
            m.get("x")?.as_f64()? as f32,
            m.get("y")?.as_f64()? as f32,
            m.get("z")?.as_f64()? as f32,
            m.get("w")?.as_f64()? as f32,
        ]);
    }
    None
}

fn parse_embedded_motion_vmd(smap: &serde_json::Map<String, serde_json::Value>) -> Option<VmdFile> {
    let kfs = smap.get("keyframes")?.as_array()?;
    if kfs.is_empty() {
        return None;
    }
    let mut bone_keyframes = Vec::with_capacity(kfs.len());
    for kf in kfs {
        let ko = if let Some(s) = kf.as_str() {
            parse_py_or_json(s).ok()
        } else {
            Some(kf.clone())
        }?;
        let km = ko.as_object()?;
        let name = km.get("name")?.as_str()?.to_string();
        let frame_no = km.get("frameNo").map(|v| val_to_f32(v, 0.0) as u32).unwrap_or(0);
        let pos = km.get("position").and_then(val_to_v3).unwrap_or([0.0; 3]);
        let rot = km
            .get("rotation")
            .and_then(val_to_quat_xyzw)
            .unwrap_or([0.0, 0.0, 0.0, 1.0]);
        let parse_c4 = |key: &str| -> [f32; 4] {
            if let Some(arr) = km.get(key).and_then(|v| v.as_array()) {
                if arr.len() >= 4 {
                    let mut c = [
                        val_to_f32(&arr[0], 0.25),
                        val_to_f32(&arr[1], 0.25),
                        val_to_f32(&arr[2], 0.75),
                        val_to_f32(&arr[3], 0.75),
                    ];
                    if c.iter().any(|&v| v > 1.0) {
                        for v in &mut c {
                            *v = (*v / 127.0).clamp(0.0, 1.0);
                        }
                    }
                    return c;
                }
            }
            [0.25, 0.25, 0.75, 0.75]
        };
        bone_keyframes.push(crate::vmd::VmdBoneKeyFrame {
            name,
            frame_no,
            pos,
            rot,
            x_curve: parse_c4("xCurve"),
            y_curve: parse_c4("yCurve"),
            z_curve: parse_c4("zCurve"),
            r_curve: parse_c4("rCurve"),
        });
    }
    let mut ik_keyframes = Vec::new();
    if let Some(ikvs) = smap.get("ikvKeyframes").and_then(|v| v.as_array()) {
        for ikv in ikvs {
            if let Some(arr) = ikv.as_array() {
                if arr.len() >= 3 {
                    let frame_no = val_to_f32(&arr[0], 0.0) as u32;
                    let visible = val_to_bool(&arr[1], true);
                    let mut ik_bones = BTreeMap::new();
                    if let Some(obj) = arr[2].as_object() {
                        for (k, v) in obj {
                            ik_bones.insert(k.clone(), val_to_bool(v, true));
                        }
                    }
                    ik_keyframes.push(crate::vmd::VmdIkVisibleKeyFrame {
                        frame_no,
                        visible,
                        ik_bones,
                    });
                }
            }
        }
    }
    Some(VmdFile {
        model_name: "embedded".into(),
        bone_keyframes,
        morph_keyframes: Vec::new(),
        camera_keyframes: Vec::new(),
        ik_keyframes,
    })
}

fn parse_embedded_camera_vmd(smap: &serde_json::Map<String, serde_json::Value>) -> Option<VmdFile> {
    let kfs = smap.get("keyframes")?.as_array()?;
    if kfs.is_empty() {
        return None;
    }
    let mut camera_keyframes = Vec::with_capacity(kfs.len());
    for kf in kfs {
        let Some(arr) = kf.as_array() else {
            continue;
        };
        if arr.len() < 11 {
            continue;
        }
        let frame_no = val_to_f32(&arr[0], 0.0) as u32;
        let dist = val_to_f32(&arr[1], 0.0);
        let pos = [
            val_to_f32(&arr[2], 0.0),
            val_to_f32(&arr[3], 0.0),
            val_to_f32(&arr[4], 0.0),
        ];
        let rot = [
            val_to_f32(&arr[5], 0.0),
            val_to_f32(&arr[6], 0.0),
            val_to_f32(&arr[7], 0.0),
        ];
        let view_angle = val_to_f32(&arr[9], 30.0);
        let orthographic = val_to_f32(&arr[10], 0.0) as u8;
        camera_keyframes.push(crate::vmd::VmdCameraKeyFrame {
            frame_no,
            dist,
            pos,
            rot,
            view_angle,
            orthographic,
            ..Default::default()
        });
    }
    if camera_keyframes.is_empty() {
        return None;
    }
    Some(VmdFile {
        model_name: "Camera".into(),
        bone_keyframes: Vec::new(),
        morph_keyframes: Vec::new(),
        camera_keyframes,
        ik_keyframes: Vec::new(),
    })
}

fn collect_character_keys(objects: &[Object], out: &mut Vec<i32>) {
    for o in objects {
        match o {
            Object::Character { key, children, .. } => {
                out.push(*key);
                for (_, c) in children {
                    collect_character_keys(c, out);
                }
            }
            Object::Item { children, .. }
            | Object::Folder { children, .. }
            | Object::Route { children, .. } => collect_character_keys(children, out),
            _ => {}
        }
    }
}

fn collect_character_rigs(
    scene_bytes: &[u8],
    objects: &[Object],
    search_roots: &[&Path],
    out: &mut HashMap<i32, MmddCharacterRig>,
) {
    for o in objects {
        match o {
            Object::Character {
                key,
                change,
                nipple,
                children,
                ..
            } => {
                if let Ok(card) = crate::studio_scene::card_bytes(scene_bytes, o) {
                    if let Ok(info0) = crate::card::parse_card_bytes(&card, 0) {
                        let eff_outfit = info0.status_coordinate.max(0);
                        let info = if eff_outfit > 0 {
                            crate::card::parse_card_bytes(&card, eff_outfit)
                                .unwrap_or(info0)
                        } else {
                            info0
                        };
                        if let Some(rig) = MmddCharacterRig::from_card_info_with_roots(
                            &info,
                            eff_outfit,
                            Some(*nipple),
                            change.scale[1],
                            search_roots,
                        ) {
                            out.insert(*key, rig);
                        }
                    }
                }
                for (_, c) in children {
                    collect_character_rigs(scene_bytes, c, search_roots, out);
                }
            }
            Object::Item { children, .. }
            | Object::Folder { children, .. }
            | Object::Route { children, .. } => {
                collect_character_rigs(scene_bytes, children, search_roots, out)
            }
            _ => {}
        }
    }
}

pub fn extract_mmdd(
    bytes: &[u8],
    scene: &Scene,
    vnge: Option<&VngeSceneData>,
    scene_path: Option<&Path>,
    out_dir: &Path,
) -> Option<MmddSceneData> {
    let blocks = crate::studio_scene::scene_plugins(bytes);
    let mut mode_map: Option<serde_json::Map<String, serde_json::Value>> = None;
    let mut version = 0i32;

    if let Some(ref b) = blocks {
        if let Some(plugin) = crate::card::map_get(b, "mmddirectorData") {
            let data = plugin.as_array().and_then(|a| a.get(1)).unwrap_or(plugin);
            version = plugin
                .as_array()
                .and_then(|a| a.first())
                .and_then(rmpv::Value::as_i64)
                .unwrap_or(2) as i32;
            if let Some(param_str) =
                crate::card::map_get(data, "param").and_then(rmpv::Value::as_str)
            {
                mode_map = parse_py_or_json(param_str)
                    .ok()
                    .and_then(select_mmdd_mode_setting);
            } else {
                mode_map = select_mmdd_mode_setting(rmpv_to_json(data));
            }
        }
    }
    if mode_map.is_none() {
        if let Some(folder_val) = find_mmdd_folder_v1(&scene.objects) {
            version = 1;
            mode_map = select_mmdd_mode_setting(folder_val);
        }
    }

    let vmd_ext = crate::studio_scene::scene_vmd(bytes);
    if mode_map.is_none() && vmd_ext.is_none() {
        return None;
    }

    let ini = CharaIniConfig::parse(DEFAULT_CHARA_INI).ok()?;
    let mut out = MmddSceneData {
        version: version.max(1),
        total_frame_len: 0.0,
        fps: 30.0,
        loop_play: false,
        ..Default::default()
    };

    let mut char_keys = Vec::new();
    collect_character_keys(&scene.objects, &mut char_keys);
    let mut scene_roots: Vec<&Path> = Vec::new();
    if let Some(sp) = scene_path {
        if let Some(parent) = sp.parent() {
            scene_roots.push(parent);
        }
    }
    scene_roots.push(out_dir);
    let mut char_rigs = HashMap::new();
    collect_character_rigs(bytes, &scene.objects, &scene_roots, &mut char_rigs);

    let resolve_actor_key = |alias: &str| -> i32 {
        if let Some(v) = vnge {
            if let Some(&k) = v.registry.actors.get(alias) {
                return k;
            }
            if let Some(&k) = v.registry.props.get(alias) {
                return k;
            }
        }
        // Standard VNGE auto-naming: f1, f2.. (1-based) or act0, act1.. (0-based)
        if let Some(num) = alias
            .strip_prefix('f')
            .or_else(|| alias.strip_prefix('m'))
            .and_then(|s| s.parse::<usize>().ok())
        {
            if num >= 1 {
                if let Some(&k) = char_keys.get(num - 1) {
                    return k;
                }
            }
            if let Some(&k) = char_keys.get(num) {
                return k;
            }
        }
        if let Some(num) = alias
            .strip_prefix("act")
            .and_then(|s| s.parse::<usize>().ok())
        {
            if let Some(&k) = char_keys.get(num) {
                return k;
            }
        }
        char_keys.first().copied().unwrap_or(-1)
    };

    // 1. Parse MMDD (`mmddirectorData` / `-mmddirector`)
    if let Some(map) = mode_map {
        let general = map
            .get("general")
            .and_then(|v| {
                if let Some(s) = v.as_str() {
                    parse_py_or_json(s).ok()
                } else {
                    Some(v.clone())
                }
            })
            .and_then(|v| v.as_object().cloned())
            .unwrap_or_default();

        let custom_end = general
            .get("customEndFrame")
            .or_else(|| map.get("totalFrameLen"))
            .map(|v| val_to_f32(v, -1.0))
            .unwrap_or(-1.0);
        if custom_end > 0.0 {
            out.total_frame_len = custom_end;
        }
        out.fps = map.get("fps").map(|v| val_to_f32(v, 30.0)).unwrap_or(30.0);
        out.loop_play = general
            .get("loop")
            .or_else(|| map.get("loop"))
            .map(|v| val_to_bool(v, false))
            .unwrap_or(false);

        // Morph controllers (`morph` dict or `morphs` array) — parse first so separate morph VMDs merge into motion solve
        let mut morph_entries: Vec<(String, serde_json::Value)> = Vec::new();
        if let Some(morph_val) = map.get("morph") {
            let m_obj = if let Some(s) = morph_val.as_str() {
                parse_py_or_json(s).ok()
            } else {
                Some(morph_val.clone())
            };
            if let Some(serde_json::Value::Object(mo)) = m_obj {
                for (k, v) in mo {
                    morph_entries.push((k, v));
                }
            }
        } else if let Some(serde_json::Value::Array(morphs)) = map.get("morphs") {
            for m_item in morphs {
                if let Some(arr) = m_item.as_array() {
                    if arr.len() >= 2 {
                        let alias = arr[0].as_str().unwrap_or("").to_string();
                        morph_entries.push((alias, arr[1].clone()));
                    }
                }
            }
        }
        let mut extra_morphs_by_char: HashMap<i32, Vec<VmdMorphKeyFrame>> = HashMap::new();
        for (alias, raw_setting) in morph_entries {
            let char_key = resolve_actor_key(&alias);
            let setting = if let Some(s) = raw_setting.as_str() {
                parse_py_or_json(s)
                    .ok()
                    .map(unwrap_scene_decoder)
                    .unwrap_or(serde_json::Value::Null)
            } else {
                raw_setting
            };
            let Some(smap) = setting.as_object() else {
                continue;
            };
            let enable = smap
                .get("enable")
                .map(|v| val_to_bool(v, true))
                .unwrap_or(true);
            let vmd_file = smap
                .get("filename")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            let resolved = resolve_external_file(&vmd_file, scene_path);
            let parsed_mkfs: Vec<VmdMorphKeyFrame> = resolved
                .as_ref()
                .and_then(|p| std::fs::read(p).ok())
                .and_then(|b| VmdFile::parse(&b).ok())
                .map(|v| v.morph_keyframes)
                .unwrap_or_default();
            let morph_count = if !parsed_mkfs.is_empty() {
                parsed_mkfs.len()
            } else {
                smap.get("keyframes")
                    .and_then(|v| v.as_array())
                    .map(|a| a.len())
                    .unwrap_or(0)
            };
            if enable && !parsed_mkfs.is_empty() {
                extra_morphs_by_char
                    .entry(char_key)
                    .or_default()
                    .extend(parsed_mkfs);
            }
            out.morphs.push(MmddMorphTrack {
                char_key,
                alias,
                vmd_file,
                resolved_vmd: resolved
                    .as_ref()
                    .map(|p| p.display().to_string())
                    .unwrap_or_default(),
                delay: smap.get("delay").map(|v| val_to_f32(v, 0.0)).unwrap_or(0.0),
                playback_speed: smap
                    .get("playbackSpeed")
                    .map(|v| val_to_f32(v, 1.0))
                    .unwrap_or(1.0),
                morph_count,
            });
        }

        // Collect motion entries from either dict `motion: {aid: setting}` or array `motions: [[aid, setting], ...]`
        let mut motion_entries: Vec<(String, serde_json::Value)> = Vec::new();
        if let Some(motion_val) = map.get("motion") {
            let m_obj = if let Some(s) = motion_val.as_str() {
                parse_py_or_json(s).ok()
            } else {
                Some(motion_val.clone())
            };
            if let Some(serde_json::Value::Object(mo)) = m_obj {
                // Respect general["motion_order"] if present
                let mut ordered_keys: Vec<String> = general
                    .get("motion_order")
                    .and_then(|v| {
                        if let Some(s) = v.as_str() {
                            parse_py_or_json(s).ok()
                        } else {
                            Some(v.clone())
                        }
                    })
                    .and_then(|v| v.as_array().cloned())
                    .map(|arr| {
                        arr.into_iter()
                            .filter_map(|x| x.as_str().map(str::to_string))
                            .collect()
                    })
                    .unwrap_or_default();
                for k in mo.keys() {
                    if !ordered_keys.contains(k) {
                        ordered_keys.push(k.clone());
                    }
                }
                for k in ordered_keys {
                    if let Some(v) = mo.get(&k) {
                        motion_entries.push((k, v.clone()));
                    }
                }
            }
        } else if let Some(serde_json::Value::Array(motions)) = map.get("motions") {
            for m_item in motions {
                if let Some(arr) = m_item.as_array() {
                    if arr.len() >= 2 {
                        let alias = arr[0].as_str().unwrap_or("").to_string();
                        motion_entries.push((alias, arr[1].clone()));
                    }
                }
            }
        }

        for (alias, raw_setting) in motion_entries {
            let char_key = resolve_actor_key(&alias);
            let setting = if let Some(s) = raw_setting.as_str() {
                parse_py_or_json(s)
                    .ok()
                    .map(unwrap_scene_decoder)
                    .unwrap_or(serde_json::Value::Null)
            } else {
                raw_setting
            };
            let Some(smap) = setting.as_object() else {
                continue;
            };
            let enable = smap
                .get("enable")
                .map(|v| val_to_bool(v, true))
                .unwrap_or(true);
            let vmd_file = smap
                .get("filename")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            let rig = char_rigs.get(&char_key);
            let auto_char_scale = smap
                .get("autoCharScale")
                .map(|v| val_to_bool(v, false))
                .unwrap_or(false);
            let saved_char_scale = smap
                .get("charScale")
                .map(|v| val_to_f32(v, 0.0))
                .unwrap_or(0.0);
            let mut params = MotionSolveParams {
                char_scale: if auto_char_scale && rig.is_some() {
                    0.0
                } else {
                    saved_char_scale
                },
                playback_speed: smap
                    .get("playbackSpeed")
                    .map(|v| val_to_f32(v, 1.0))
                    .unwrap_or(1.0),
                delay: smap.get("delay").map(|v| val_to_f32(v, 0.0)).unwrap_or(0.0),
                interpolate_consecutive: smap
                    .get("interCons")
                    .map(|v| val_to_bool(v, true))
                    .unwrap_or(true),
                looping: out.loop_play,
                ..Default::default()
            };
            if let Some(eh_val) = smap
                .get("enableHighHeelz")
                .or_else(|| smap.get("enableHeels"))
            {
                params.enable_high_heelz = Some(val_to_bool(eh_val, false));
            }
            if let Some(hh_val) = smap
                .get("heelzRotation")
                .or_else(|| smap.get("highHeels"))
                .or_else(|| smap.get("heelzRot"))
            {
                if let Some(v3) = val_to_v3(hh_val) {
                    params.high_heels_rot = Some(v3);
                } else if hh_val.is_boolean() {
                    if val_to_bool(hh_val, false) {
                        params.high_heels_rot = Some(DEFAULT_HIGH_HEEL_ROT);
                        if params.enable_high_heelz.is_none() {
                            params.enable_high_heelz = Some(true);
                        }
                    } else if params.enable_high_heelz.is_none() {
                        params.enable_high_heelz = Some(false);
                    }
                }
            }
            params.shoes_detect = smap
                .get("shoesDetect")
                .map(|v| val_to_bool(v, true))
                .unwrap_or_else(|| params.enable_high_heelz != Some(false));
            if let Some(so_val) = smap.get("shoesOffset") {
                let parsed_so = if let Some(s) = so_val.as_str() {
                    parse_py_or_json(s).ok().and_then(|v| parse_shoes_offset_val(&v))
                } else {
                    parse_shoes_offset_val(so_val)
                };
                params.shoes_offset = parsed_so;
            }
            if let Some(ik_val) = smap.get("ikEnable") {
                let ik_obj = if let Some(s) = ik_val.as_str() {
                    parse_py_or_json(s).ok()
                } else {
                    Some(ik_val.clone())
                };
                if let Some(serde_json::Value::Object(iko)) = ik_obj {
                    for (k, v) in iko {
                        params.ik_enable.insert(k, val_to_bool(&v, true));
                    }
                }
            }
            if let Some(serde_json::Value::Array(adj_list)) = smap.get("customizedBoneAdjust") {
                for adj in adj_list {
                    let adj_val = if let Some(s) = adj.as_str() {
                        parse_py_or_json(s).ok()
                    } else {
                        Some(adj.clone())
                    };
                    let Some(ao) = adj_val.as_ref().and_then(|v| v.as_object()) else {
                        continue;
                    };
                    let Some(bname) = ao.get("name").and_then(|v| v.as_str()) else {
                        continue;
                    };
                    if let Some(v) = ao.get("rotadj").and_then(val_to_v3) {
                        params.custom_rot_adj.insert(bname.to_string(), v);
                    }
                    if let Some(v) = ao.get("axisadj").and_then(val_to_v3) {
                        params.custom_axis_adj.insert(bname.to_string(), v);
                    }
                    if let Some(v) = ao.get("rotscale") {
                        params
                            .custom_rot_scale
                            .insert(bname.to_string(), val_to_f32(v, 1.0));
                    }
                    if let Some(v) = ao.get("posadj").and_then(val_to_v3) {
                        params.custom_base_pos.insert(bname.to_string(), v);
                    }
                    if let Some(v) = ao.get("posoffset").and_then(val_to_v3) {
                        params.custom_pos_offset.insert(bname.to_string(), v);
                    }
                    if let Some(v) = ao.get("posscale") {
                        params
                            .custom_pos_scale
                            .insert(bname.to_string(), val_to_f32(v, 1.0));
                    }
                }
            }
            // v1 customizedBoneRot / customizedBonePos
            if let Some(serde_json::Value::Array(rot_list)) = smap.get("customizedBoneRot") {
                for item in rot_list {
                    let val = item
                        .as_str()
                        .and_then(|s| parse_py_or_json(s).ok())
                        .or_else(|| Some(item.clone()));
                    let Some(ao) = val.as_ref().and_then(|v| v.as_object()) else {
                        continue;
                    };
                    let Some(bname) = ao.get("name").and_then(|v| v.as_str()) else {
                        continue;
                    };
                    if let Some(v) = ao.get("rotadj").and_then(val_to_v3) {
                        params.custom_rot_adj.insert(bname.to_string(), v);
                    }
                    if let Some(v) = ao.get("axisadj").and_then(val_to_v3) {
                        params.custom_axis_adj.insert(bname.to_string(), v);
                    }
                    if let Some(v) = ao.get("scale") {
                        params
                            .custom_rot_scale
                            .insert(bname.to_string(), val_to_f32(v, 1.0));
                    }
                }
            }
            if let Some(serde_json::Value::Array(pos_list)) = smap.get("customizedBonePos") {
                for item in pos_list {
                    let val = item
                        .as_str()
                        .and_then(|s| parse_py_or_json(s).ok())
                        .or_else(|| Some(item.clone()));
                    let Some(ao) = val.as_ref().and_then(|v| v.as_object()) else {
                        continue;
                    };
                    let Some(bname) = ao.get("name").and_then(|v| v.as_str()) else {
                        continue;
                    };
                    if let Some(v) = ao.get("posoffset").and_then(val_to_v3) {
                        params.custom_pos_offset.insert(bname.to_string(), v);
                    }
                    if let Some(v) = ao.get("scale") {
                        params
                            .custom_pos_scale
                            .insert(bname.to_string(), val_to_f32(v, 1.0));
                    }
                }
            }

            let resolved = resolve_external_file(&vmd_file, scene_path);
            let mut okanim_path = String::new();
            let mut frames = 0u32;
            let mut duration = 0.0f32;
            if enable {
                let parsed_vmd_opt = resolved
                    .as_ref()
                    .and_then(|p| std::fs::read(p).ok())
                    .and_then(|b| VmdFile::parse(&b).ok())
                    .or_else(|| parse_embedded_motion_vmd(smap));
                if let Some(mut parsed_vmd) = parsed_vmd_opt {
                    if parsed_vmd.morph_keyframes.is_empty() {
                        if let Some(extra_mkfs) = extra_morphs_by_char.get(&char_key) {
                            parsed_vmd.morph_keyframes = extra_mkfs.clone();
                        }
                    } else if !out.morphs.iter().any(|m| m.char_key == char_key && m.morph_count > 0) {
                        out.morphs.push(MmddMorphTrack {
                            char_key,
                            alias: alias.clone(),
                            vmd_file: vmd_file.clone(),
                            resolved_vmd: resolved
                                .as_ref()
                                .map(|p| p.display().to_string())
                                .unwrap_or_default(),
                            delay: params.delay,
                            playback_speed: params.playback_speed,
                            morph_count: parsed_vmd.morph_keyframes.len(),
                        });
                    }
                    let stem = resolved
                        .as_ref()
                        .and_then(|p| p.file_stem())
                        .and_then(|s| s.to_str())
                        .unwrap_or("mmdd");
                    if let Ok(anim) = solve_vmd_to_okanim_for_rig(&parsed_vmd, &ini, &params, stem, rig) {
                        frames = ((anim.frames.saturating_sub(1)) / 2 + 1).max(1);
                        duration = anim.duration();
                        if let Ok(anim_bytes) = anim.to_bytes() {
                            let target = out_dir.join(format!("mmdd_{char_key}.okanim"));
                            if std::fs::write(&target, anim_bytes).is_ok() {
                                okanim_path = target.display().to_string();
                            }
                        }
                    }
                }
            }

            let effective_scale = if params.char_scale > 0.0 {
                params.char_scale
            } else {
                rig.map_or(1.0, MmddCharacterRig::auto_char_scale)
            };
            out.motions.push(MmddMotionTrack {
                char_key,
                alias,
                source: "mmdd".into(),
                vmd_file,
                resolved_vmd: resolved
                    .as_ref()
                    .map(|p| p.display().to_string())
                    .unwrap_or_default(),
                okanim: okanim_path,
                delay: params.delay,
                playback_speed: params.playback_speed,
                char_scale: effective_scale,
                frames,
                duration,
                looping: params.looping,
            });
        }

        // Camera controllers (`camera` array/object or `cameras` array)
        let cam_list: Vec<serde_json::Value> = map
            .get("camera")
            .or_else(|| map.get("cameras"))
            .map(|v| {
                if let Some(arr) = v.as_array() {
                    arr.clone()
                } else {
                    vec![v.clone()]
                }
            })
            .unwrap_or_default();
        for c_item in cam_list {
            let setting = if let Some(s) = c_item.as_str() {
                parse_py_or_json(s)
                    .ok()
                    .map(unwrap_scene_decoder)
                    .unwrap_or(serde_json::Value::Null)
            } else {
                c_item
            };
            let Some(smap) = setting.as_object() else {
                continue;
            };
            let enable = smap
                .get("enable")
                .map(|v| val_to_bool(v, true))
                .unwrap_or(true);
            let vmd_file = smap
                .get("filename")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            let delay = smap.get("delay").map(|v| val_to_f32(v, 0.0)).unwrap_or(0.0);
            let playback_speed = smap
                .get("playbackSpeed")
                .map(|v| val_to_f32(v, 1.0))
                .unwrap_or(1.0);
            let pos_scale = smap
                .get("posScale")
                .map(|v| val_to_f32(v, 0.079))
                .unwrap_or(0.079);
            let dst_scale = smap
                .get("dstScale")
                .map(|v| val_to_f32(v, 0.079))
                .unwrap_or(0.079);
            let off_pos = smap
                .get("offPos")
                .and_then(val_to_v3)
                .unwrap_or([0.0, 0.0, 0.0]);
            let off_rot = smap
                .get("offRot")
                .and_then(val_to_v3)
                .unwrap_or([0.0, 0.0, 0.0]);
            let resolved = resolve_external_file(&vmd_file, scene_path);
            let keyframes = resolved
                .as_ref()
                .and_then(|p| std::fs::read(p).ok())
                .and_then(|b| VmdFile::parse(&b).ok())
                .or_else(|| parse_embedded_camera_vmd(smap))
                .map(|vmd| {
                    evaluate_camera_vmd(
                        &vmd,
                        delay,
                        playback_speed,
                        pos_scale,
                        dst_scale,
                        off_pos,
                        off_rot,
                    )
                })
                .unwrap_or_default();
            if vmd_file.is_empty() && keyframes.is_empty() {
                continue;
            }
            out.cameras.push(MmddCameraTrack {
                enable,
                vmd_file,
                resolved_vmd: resolved
                    .as_ref()
                    .map(|p| p.display().to_string())
                    .unwrap_or_default(),
                delay,
                playback_speed,
                pos_scale,
                dst_scale,
                off_pos,
                off_rot,
                keyframes,
            });
        }

        // Sound controllers (`sound` array/object or `sounds` array)
        let snd_list: Vec<serde_json::Value> = map
            .get("sound")
            .or_else(|| map.get("sounds"))
            .map(|v| {
                if let Some(arr) = v.as_array() {
                    arr.clone()
                } else {
                    vec![v.clone()]
                }
            })
            .unwrap_or_default();
        for s_item in snd_list {
            let setting = if let Some(s) = s_item.as_str() {
                parse_py_or_json(s)
                    .ok()
                    .map(unwrap_scene_decoder)
                    .unwrap_or(serde_json::Value::Null)
            } else {
                s_item
            };
            let Some(smap) = setting.as_object() else {
                continue;
            };
            let sound_file = smap
                .get("ss_setting")
                .and_then(|v| {
                    if let Some(s) = v.as_str() {
                        parse_py_or_json(s).ok()
                    } else {
                        Some(v.clone())
                    }
                })
                .and_then(|v| v.get("audioFilePath").and_then(|p| p.as_str()).map(str::to_string))
                .or_else(|| {
                    smap.get("filename")
                        .and_then(|v| v.as_str())
                        .map(str::to_string)
                })
                .unwrap_or_default();
            if sound_file.is_empty() {
                continue;
            }
            let resolved = resolve_external_file(&sound_file, scene_path);
            out.sounds.push(MmddSoundTrack {
                enable: smap
                    .get("enable")
                    .map(|v| val_to_bool(v, true))
                    .unwrap_or(true),
                sound_file,
                resolved_sound: resolved
                    .as_ref()
                    .map(|p| p.display().to_string())
                    .unwrap_or_default(),
                delay: smap.get("delay").map(|v| val_to_f32(v, 0.0)).unwrap_or(0.0),
            });
        }

        // Clip controllers (`clip` or `clips` linking VNGE KFA clips)
        let clip_list: Vec<serde_json::Value> = map
            .get("clip")
            .or_else(|| map.get("clips"))
            .and_then(|v| v.as_array().cloned())
            .unwrap_or_default();
        for c_item in clip_list {
            let setting = if let Some(s) = c_item.as_str() {
                parse_py_or_json(s)
                    .ok()
                    .map(unwrap_scene_decoder)
                    .unwrap_or(serde_json::Value::Null)
            } else {
                c_item
            };
            let Some(smap) = setting.as_object() else {
                continue;
            };
            let clip_name = smap
                .get("clipname")
                .or_else(|| smap.get("clipName"))
                .or_else(|| smap.get("name"))
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            if clip_name.is_empty() {
                continue;
            }
            let start_time = smap
                .get("linkframe")
                .map(|v| val_to_f32(v, 0.0) / 30.0)
                .or_else(|| smap.get("startTime").or_else(|| smap.get("delay")).map(|v| val_to_f32(v, 0.0)))
                .unwrap_or(0.0);
            let loop_count = smap
                .get("loopcount")
                .map(|v| val_to_f32(v, 1.0))
                .unwrap_or(1.0);
            out.clips.push(MmddClipTrack {
                enable: smap
                    .get("enable")
                    .map(|v| val_to_bool(v, true))
                    .unwrap_or(true),
                clip_name,
                start_time,
                end_time: smap
                    .get("endTime")
                    .map(|v| val_to_f32(v, 0.0))
                    .unwrap_or(0.0),
                loop_clip: loop_count > 1.0
                    || smap
                        .get("loop")
                        .map(|v| val_to_bool(v, false))
                        .unwrap_or(false),
                speed: smap
                    .get("playspeed")
                    .or_else(|| smap.get("speed"))
                    .or_else(|| smap.get("playbackSpeed"))
                    .map(|v| val_to_f32(v, 1.0))
                    .unwrap_or(1.0),
            });
        }

        if let Some(serde_json::Value::Array(binds)) = map.get("binds") {
            out.binds = binds.clone();
        }
        if let Some(serde_json::Value::Array(parents)) = map.get("parents") {
            out.parents = parents.clone();
        }
        if let Some(serde_json::Value::Array(fcams)) = map.get("followCameras") {
            out.follow_cameras = fcams.clone();
        }
    }

    // 2. Also handle `KKVMDPlayExtSave` characters and camera when enabled
    if let Some((vmd_scene, vmd_chars)) = vmd_ext {
        if let Some(cam_vmd) = vmd_scene
            .get("Camera.VMDFilename")
            .and_then(|v| v.as_str())
            .filter(|s| !s.trim().is_empty())
        {
            let speed = vmd_scene
                .get("Camera.Speed")
                .map(|v| val_to_f32(v, 1.0))
                .unwrap_or(1.0);
            let model_scale = vmd_scene
                .get("Camera.ModelScale")
                .map(|v| val_to_f32(v, 1.0))
                .unwrap_or(1.0);
            let off_pos = vmd_scene
                .get("Camera.PosAdjust")
                .and_then(val_to_v3)
                .unwrap_or([0.0; 3]);
            let rot_y = vmd_scene
                .get("Camera.RotAdjustY")
                .map(|v| val_to_f32(v, 0.0))
                .unwrap_or(0.0);
            let resolved = resolve_external_file(cam_vmd, scene_path);
            let keyframes = resolved
                .as_ref()
                .and_then(|p| std::fs::read(p).ok())
                .and_then(|b| VmdFile::parse(&b).ok())
                .map(|vmd| {
                    evaluate_camera_vmd(
                        &vmd,
                        0.0,
                        speed,
                        0.08 * model_scale,
                        0.08 * model_scale,
                        off_pos,
                        [0.0, rot_y, 0.0],
                    )
                })
                .unwrap_or_default();
            out.cameras.push(MmddCameraTrack {
                enable: true,
                vmd_file: cam_vmd.to_string(),
                resolved_vmd: resolved
                    .as_ref()
                    .map(|p| p.display().to_string())
                    .unwrap_or_default(),
                delay: 0.0,
                playback_speed: speed,
                pos_scale: 0.08 * model_scale,
                dst_scale: 0.08 * model_scale,
                off_pos,
                off_rot: [0.0, rot_y, 0.0],
                keyframes,
            });
        }
        if let Some(snd) = vmd_scene
            .get("Sound.Filename")
            .and_then(|v| v.as_str())
            .filter(|s| !s.trim().is_empty())
        {
            let resolved = resolve_external_file(snd, scene_path);
            out.sounds.push(MmddSoundTrack {
                enable: true,
                sound_file: snd.to_string(),
                resolved_sound: resolved
                    .as_ref()
                    .map(|p| p.display().to_string())
                    .unwrap_or_default(),
                delay: 0.0,
            });
        }
        for (char_key, attrs) in vmd_chars {
            let enabled = attrs
                .get("VMDAnimEnabled")
                .map(|v| val_to_bool(v, false))
                .unwrap_or(false);
            let vmd_file = attrs
                .get("lastLoadedVMD")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            if !enabled || vmd_file.trim().is_empty() {
                continue;
            }
            // Skip if MMDD already solved this character
            if out
                .motions
                .iter()
                .any(|m| m.char_key == char_key && !m.okanim.is_empty())
            {
                continue;
            }
            let shoulder = attrs
                .get("adjustShoulder")
                .map(|v| val_to_f32(v, 10.0))
                .unwrap_or(10.0);
            let arm_up = attrs
                .get("adjustArmUp")
                .map(|v| val_to_f32(v, 32.0))
                .unwrap_or(32.0);
            let arm_low = attrs
                .get("adjustArmLow")
                .map(|v| val_to_f32(v, 0.0))
                .unwrap_or(0.0);
            let scale_model = attrs
                .get("adjustScaleModel")
                .map(|v| val_to_f32(v, 0.76))
                .unwrap_or(0.76);
            let speed = attrs
                .get("Anim.speed")
                .map(|v| val_to_f32(v, 1.0))
                .unwrap_or(1.0);
            let loop_play = attrs
                .get("Anim.Loop")
                .map(|v| val_to_bool(v, false))
                .unwrap_or(false);
            let ik_en = attrs
                .get("IK.enable")
                .map(|v| val_to_bool(v, true))
                .unwrap_or(true);
            let disable_toe = attrs
                .get("IK.disableToeIK")
                .map(|v| val_to_bool(v, false))
                .unwrap_or(false);

            let rig = char_rigs.get(&char_key);
            let auto_scale = rig.map_or(1.0, MmddCharacterRig::auto_char_scale);
            let mut params = MotionSolveParams {
                char_scale: (scale_model / 0.76) * auto_scale,
                playback_speed: speed,
                delay: 0.0,
                interpolate_consecutive: true,
                looping: loop_play,
                ..Default::default()
            };
            params.ik_enable.insert("FOOT IK".into(), ik_en);
            params
                .ik_enable
                .insert("TOE IK".into(), ik_en && !disable_toe);
            params
                .custom_rot_adj
                .insert("shoulder_r".into(), [0.0, 0.0, -shoulder]);
            params
                .custom_rot_adj
                .insert("shoulder_l".into(), [0.0, 0.0, shoulder]);
            params
                .custom_rot_adj
                .insert("uparm_r".into(), [0.0, 0.0, -arm_up]);
            params
                .custom_rot_adj
                .insert("uparm_l".into(), [0.0, 0.0, arm_up]);
            if arm_low.abs() > 1e-4 {
                params
                    .custom_rot_adj
                    .insert("lowarm_r".into(), [0.0, 0.0, -arm_low]);
                params
                    .custom_rot_adj
                    .insert("lowarm_l".into(), [0.0, 0.0, arm_low]);
            }

            let resolved = resolve_external_file(&vmd_file, scene_path);
            let mut okanim_path = String::new();
            let mut frames = 0u32;
            let mut duration = 0.0f32;
            if let Some(ref vmd_path) = resolved {
                if let Ok(vmd_bytes) = std::fs::read(vmd_path) {
                    if let Ok(parsed_vmd) = VmdFile::parse(&vmd_bytes) {
                        let stem = vmd_path
                            .file_stem()
                            .and_then(|s| s.to_str())
                            .unwrap_or("kkvmd");
                        if let Ok(anim) = solve_vmd_to_okanim_for_rig(&parsed_vmd, &ini, &params, stem, rig) {
                            frames = ((anim.frames.saturating_sub(1)) / 2 + 1).max(1);
                            duration = anim.duration();
                            if let Ok(anim_bytes) = anim.to_bytes() {
                                let target = out_dir.join(format!("mmdd_{char_key}.okanim"));
                                if std::fs::write(&target, anim_bytes).is_ok() {
                                    okanim_path = target.display().to_string();
                                }
                            }
                        }
                    }
                }
            }
            out.motions.push(MmddMotionTrack {
                char_key,
                alias: format!("dicKey_{char_key}"),
                source: "KKVMDPlayExtSave".into(),
                vmd_file,
                resolved_vmd: resolved
                    .as_ref()
                    .map(|p| p.display().to_string())
                    .unwrap_or_default(),
                okanim: okanim_path,
                delay: 0.0,
                playback_speed: speed,
                char_scale: params.char_scale,
                frames,
                duration,
                looping: loop_play,
            });
        }
    }

    if out.total_frame_len <= 0.0 {
        let max_motion = out
            .motions
            .iter()
            .map(|m| m.frames as f32)
            .fold(0.0f32, f32::max);
        let max_cam = out
            .cameras
            .iter()
            .flat_map(|c| c.keyframes.iter().map(|k| k.frame_no as f32))
            .fold(0.0f32, f32::max);
        out.total_frame_len = max_motion.max(max_cam);
    }

    if out.motions.is_empty()
        && out.morphs.is_empty()
        && out.cameras.is_empty()
        && out.sounds.is_empty()
        && out.clips.is_empty()
    {
        return None;
    }
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_chara_ini_parses_completely() {
        let ini = CharaIniConfig::parse(DEFAULT_CHARA_INI).expect("parse default chara ini");
        assert_eq!(ini.bone_names.len(), 90);
        assert_eq!(ini.ik_sets.len(), 2);
        assert_eq!(ini.ik_sets[0].name, "FOOT IK");
        assert_eq!(ini.ik_sets[1].name, "TOE IK");
        assert!(ini.bones.values().any(|b| b.is_track));
        assert!(!ini.twist_disperse.is_empty());
    }

    #[test]
    fn real_mmdd_and_kkvmdplay_scenes_export_okanim_and_camera() {
        let mmdd_scene = Path::new(r"E:\Koikatu\UserData\Studio\scene\2025_0113_0056_34_115.png");
        if mmdd_scene.is_file() {
            let out_dir = std::env::temp_dir().join("ok_mmdd_scene_test");
            let _ = std::fs::remove_dir_all(&out_dir);
            let json = crate::studio_scene::export(mmdd_scene, &out_dir).expect("export mmdd scene");
            assert!(json.get("vnge").is_some(), "vnge missing in exported json");
            let mmdd = json.get("mmdd").expect("mmdd missing in exported json");
            println!(
                "MMDD scene exported: version={} total_frame_len={} motions={} cameras={} morphs={} sounds={} clips={}",
                mmdd["version"],
                mmdd["total_frame_len"],
                mmdd["motions"].as_array().map(|a| a.len()).unwrap_or(0),
                mmdd["cameras"].as_array().map(|a| a.len()).unwrap_or(0),
                mmdd["morphs"].as_array().map(|a| a.len()).unwrap_or(0),
                mmdd["sounds"].as_array().map(|a| a.len()).unwrap_or(0),
                mmdd["clips"].as_array().map(|a| a.len()).unwrap_or(0),
            );
            let motions = mmdd["motions"].as_array().unwrap();
            assert!(!motions.is_empty());
            for m in motions {
                let okanim_path = m["okanim"].as_str().unwrap_or("");
                println!(
                    "  motion alias={} char_key={} vmd={} resolved={} okanim={} frames={} duration={}",
                    m["alias"], m["char_key"], m["vmd_file"], m["resolved_vmd"], okanim_path, m["frames"], m["duration"]
                );
                assert!(!okanim_path.is_empty(), "okanim path empty for motion {:?}", m);
                let anim_bytes = std::fs::read(okanim_path).expect("read solved okanim");
                let anim = OkAnim::from_bytes(&anim_bytes).expect("parse solved okanim");
                assert!(anim.frames > 10, "expected >10 frames, got {}", anim.frames);
                assert!(!anim.tracks.is_empty());
            }
            let cameras = mmdd["cameras"].as_array().unwrap();
            assert!(!cameras.is_empty());
            let cam_kfs = cameras[0]["keyframes"].as_array().unwrap();
            println!("  camera keyframes count={}", cam_kfs.len());
            assert!(!cam_kfs.is_empty());
        }

        let kkvmd_scene = Path::new(r"E:\Koikatu\UserData\Studio\scene\2025_1004_1936_39_768.png");
        if kkvmd_scene.is_file() {
            let out_dir = std::env::temp_dir().join("ok_kkvmd_scene_test");
            let _ = std::fs::remove_dir_all(&out_dir);
            let json = crate::studio_scene::export(kkvmd_scene, &out_dir).expect("export kkvmd scene");
            let mmdd = json.get("mmdd").expect("mmdd missing for KKVMDPlayExtSave scene");
            let motions = mmdd["motions"].as_array().unwrap();
            assert!(!motions.is_empty());
            let okanim_path = motions[0]["okanim"].as_str().unwrap_or("");
            println!(
                "KKVMDPlay scene exported: char_key={} vmd={} resolved={} okanim={} frames={} duration={}",
                motions[0]["char_key"],
                motions[0]["vmd_file"],
                motions[0]["resolved_vmd"],
                okanim_path,
                motions[0]["frames"],
                motions[0]["duration"]
            );
            assert!(!okanim_path.is_empty());
            let anim = OkAnim::from_bytes(&std::fs::read(okanim_path).unwrap()).unwrap();
            assert!(anim.frames > 100);
        }
    }

    fn make_shaped_test_session(
        shape_body: Vec<f32>,
        shape_face: Vec<f32>,
        abm: Vec<crate::abmx::AbmBone>,
    ) -> Option<crate::session::OkSession> {
        let tpl = mmdd_shape_template()?;
        let body_rest = Some(tpl.female_body_rest.clone());
        let head_rest = tpl.head_rest.clone();
        let mut sess = crate::session::OkSession {
            accessory_context: None,
            editor_pregnancy: Default::default(),
            pv_config: None,
            pv_mask: 15,
            pv_fk: false,
            eye_look_config: None,
            eye_look_runtime: None,
            head_look: crate::head_look::HeadLook::default(),
            physx_secondary: false,
            external_animation: None,
            scene_gpu_parts: Vec::new(),
            physics: Default::default(),
            body_interaction: None,
            game_root: PathBuf::new(),
            ramp_png: None,
            shadow_depth: 0.26,
            line_width: 1.0,
            line_depth: 1.0,
            kano_ver_shift: 0.0,
            body_rest: body_rest.clone(),
            body_dynamics: Default::default(),
            body_dynamic_runtime: None,
            yure_policy: None,
            body_colliders: Default::default(),
            clothes_dynamics: Default::default(),
            clothes_dynamic_runtime: None,
            bust_softness: 0.5,
            bust_weight: 0.5,
            collider_settings: Default::default(),
            collider_overrides: Default::default(),
            head_rest: head_rest.clone(),
            body_skel: body_rest,
            head_skel: head_rest.clone(),
            body_keys: tpl.body_keys.clone(),
            head_keys: tpl.head_keys.clone(),
            hand_shapes: Default::default(),
            user_fk: Vec::new(),
            user_fk_ids: Default::default(),
            hand_shape_ids: Default::default(),
            studio_ik_solver: None,
            studio_ik: Vec::new(),
            studio_ik_rotations: Vec::new(),
            studio_ik_weights: Vec::new(),
            correct: tpl.correct.clone(),
            sex: 1,
            type_bone: 0,
            nip_stand_rate: 0.5,
            shape_body: shape_body.clone(),
            shape_face: shape_face.clone(),
            abm,
            parts: Vec::new(),
            coordinate_count: 1,
            skel_export: None,
            anim_time: 0.0,
            paired_ik_runtime: None,
            anim_clip: String::new(),
            anim_scene_mode: 4,
            mmdd_world_lock: false,
            mmdd_face_bone_dirty: false,
            active_shoe_meta: None,
            face_runtime: Default::default(),
            anim_parameters: HashMap::new(),
            anim_clock: Default::default(),
            anim_crossfade: None,
            anim_layer_crossfades: Default::default(),
            body_shaped: None,
            shape_reference_scratch: None,
            head_shaped: None,
            head_full_rest: head_rest.clone(),
            head_full_posed: head_rest,
        };
        sess.apply_shape(&shape_body, &shape_face);
        Some(sess)
    }

    fn evaluate_reference_dummy_tree(
        vmd: &VmdFile,
        ini: &CharaIniConfig,
        params: &MotionSolveParams,
        rig: &MmddCharacterRig,
        frame_no: f32,
    ) -> DummyTree {
        let mut prep_body = rig.live_rest.clone();
        if ini.prepare_enable_set {
            let mut updates = Vec::new();
            for bname in &ini.bone_names {
                let Some(wname) = ini.bones.get(bname).and_then(|b| b.weight_bone.as_ref()) else {
                    continue;
                };
                let wkey = wname.to_ascii_lowercase();
                let rot = if let Some(&r) = ini.prepare_rotations.get(&wkey) {
                    r
                } else if !ini.prepare_listed_only {
                    [0.0, 0.0, 0.0, 1.0]
                } else {
                    continue;
                };
                if let Some((id, _)) = find_skel_bone_ci(&prep_body, wname) {
                    updates.push((id, rot));
                }
            }
            for (id, rot) in updates {
                if let Some(tf) = prep_body.tfs.get_mut(&id) {
                    tf.rot = rot;
                }
            }
            prep_body.rebuild_worlds();
        }
        let mut tree = DummyTree::new(ini);
        let mut bone_adjusts = ini.bone_adjusts.clone();
        for (idx, bname) in ini.bone_names.iter().enumerate() {
            let bdef = &ini.bones[bname];
            match &bdef.pos_spec {
                BonePosSpec::Bone(target) => {
                    let (wpos, _) = skel_world_pos_rot(&prep_body, target);
                    tree.set_world_pos(idx, wpos);
                }
                BonePosSpec::Between(t1, t2, scale) => {
                    let (p1, _) = skel_world_pos_rot(&prep_body, t1);
                    let (p2, _) = skel_world_pos_rot(&prep_body, t2);
                    tree.set_world_pos(
                        idx,
                        [
                            p1[0] + (p2[0] - p1[0]) * scale,
                            p1[1] + (p2[1] - p1[1]) * scale,
                            p1[2] + (p2[2] - p1[2]) * scale,
                        ],
                    );
                }
                BonePosSpec::Offset(target, offset) => {
                    let (p, _) = skel_world_pos_rot(&prep_body, target);
                    tree.set_world_pos(idx, vadd(p, *offset));
                }
                BonePosSpec::Reset(target, rst) => {
                    let (mut p, _) = skel_world_pos_rot(&prep_body, target);
                    for axis in 0..3 {
                        if let Some(v) = rst[axis] {
                            p[axis] = v;
                        }
                    }
                    tree.set_world_pos(idx, p);
                }
                BonePosSpec::BonePosRot(target) => {
                    let (wpos, wrot) = skel_world_pos_rot(&prep_body, target);
                    tree.set_world_pos(idx, wpos);
                    tree.set_world_rot(idx, wrot);
                }
            }
            if let Some(adj) = bone_adjusts.get_mut(bname) {
                adj.base_position = tree.nodes[idx].local_pos;
            }
        }
        let mut ik_solvers: HashMap<usize, IkSolver> = HashMap::new();
        for ik_set in &ini.ik_sets {
            for ik_bname in &ik_set.ik_bones {
                let Some(&ik_idx) = tree.by_name.get(ik_bname) else { continue };
                let bdef = &ini.bones[ik_bname];
                let Some(&tgt_idx) = tree.by_name.get(&bdef.ik_data[0]) else { continue };
                if ik_set.solver.eq_ignore_ascii_case("MMDIKSolverLeg") && bdef.ik_data.len() >= 3 {
                    let (c0, c1) = (tree.by_name[&bdef.ik_data[1]], tree.by_name[&bdef.ik_data[2]]);
                    ik_solvers.insert(
                        ik_idx,
                        IkSolver::create_leg(
                            &mut tree,
                            ik_idx,
                            tgt_idx,
                            c0,
                            c1,
                            ik_set.iterations,
                            ik_set.step_angle,
                            ik_set.min_knee_rot,
                        ),
                    );
                } else {
                    let chain: Vec<usize> = bdef.ik_data[1..]
                        .iter()
                        .filter_map(|n| tree.by_name.get(n).copied())
                        .collect();
                    ik_solvers.insert(
                        ik_idx,
                        IkSolver::NoLimit {
                            ik_bone: ik_idx,
                            ik_tgt: tgt_idx,
                            chain,
                            iterations: ik_set.iterations,
                            step_angle: ik_set.step_angle,
                        },
                    );
                }
            }
        }
        for bname in &ini.bone_names {
            if bone_adjusts.get(bname).map_or(false, |a| a.rot_axis_auto) {
                if let Some(pname) = ini.bones.get(bname).and_then(|b| b.parent.as_ref()) {
                    if let Some(p_adj) = bone_adjusts.get(pname) {
                        let auto_adj = quat_mul(qinv(p_adj.rot_adjustment), p_adj.rot_axis_adjustment);
                        let euler = quat_to_unity_euler(auto_adj);
                        if let Some(adj) = bone_adjusts.get_mut(bname) {
                            adj.set_axis_adjustment(euler);
                        }
                    }
                }
            }
        }
        let eff = if params.char_scale > 0.0 {
            params.char_scale
        } else {
            rig.auto_char_scale()
        };
        for adj in bone_adjusts.values_mut() {
            adj.set_pos_global_scale(ini.axis_scale * eff);
        }
        let mut bone_kfs: HashMap<usize, Vec<VmdBoneKeyFrame>> = HashMap::new();
        for (idx, _) in ini.bone_names.iter().enumerate() {
            bone_kfs.insert(idx, vec![VmdBoneKeyFrame::default()]);
        }
        let mut seen: HashMap<usize, std::collections::HashSet<u32>> = HashMap::new();
        for kf in &vmd.bone_keyframes {
            let Some(bname) = ini.vmd_to_bone.get(&kf.name) else { continue };
            let Some(&idx) = tree.by_name.get(bname) else { continue };
            let set = seen.entry(idx).or_default();
            if set.is_empty() {
                bone_kfs.insert(idx, Vec::new());
            }
            if set.insert(kf.frame_no) {
                bone_kfs.get_mut(&idx).unwrap().push(kf.clone());
            }
        }
        for kfs in bone_kfs.values_mut() {
            kfs.sort_by_key(|k| k.frame_no);
        }
        for (idx, bname) in ini.bone_names.iter().enumerate() {
            let bdef = &ini.bones[bname];
            let adj = &bone_adjusts[bname];
            let (raw_pos, raw_rot) = sample_vmd_bone_kfs(&bone_kfs[&idx], frame_no, true);
            if bdef.is_rotation || (bdef.is_track && bdef.track_ptn.contains('r')) {
                let mut brot = adj.get_adjusted_rotation(raw_rot);
                if bdef.is_limit_axis {
                    let (mut angle, axis) = quat_to_angle_axis_deg(brot);
                    if vlen(vadd(axis, bdef.limit_ref_axis)) < 1.0 {
                        angle = -angle;
                    }
                    brot = quat_from_angle_axis_deg(angle, bdef.limit_axis);
                }
                tree.set_local_rot(idx, brot);
            }
            if bdef.is_position || (bdef.is_track && bdef.track_ptn.contains('p')) {
                tree.set_local_pos(idx, adj.get_adjusted_position(raw_pos));
            }
            if bdef.is_ik {
                if let Some(solver) = ik_solvers.get(&idx) {
                    solver.solve(&mut tree);
                }
            }
            if bdef.is_track {
                if let Some(&src_idx) = tree.by_name.get(&bdef.track_src) {
                    if bdef.track_ptn.contains('r') {
                        let scaled = quat_slerp_unclamped(
                            [0.0, 0.0, 0.0, 1.0],
                            tree.nodes[src_idx].local_rot,
                            bdef.track_scale,
                        );
                        tree.set_local_rot(idx, quat_mul(scaled, tree.nodes[idx].local_rot));
                    }
                    if bdef.track_ptn.contains('p') {
                        tree.set_local_pos(
                            idx,
                            vadd(
                                tree.nodes[idx].local_pos,
                                vscale(tree.nodes[src_idx].local_pos, bdef.track_scale),
                            ),
                        );
                    }
                }
            }
        }
        for td in &ini.twist_disperse {
            if !td.enable {
                continue;
            }
            let (Some(&t_idx), Some(&d_idx)) = (
                tree.by_name.get(&td.twist_bone),
                tree.by_name.get(&td.disperse_bone),
            ) else {
                continue;
            };
            let org_q = tree.nodes[t_idx].local_rot;
            if org_q != [0.0, 0.0, 0.0, 1.0] {
                let remain_q = quat_lerp([0.0, 0.0, 0.0, 1.0], org_q, 1.0 - td.disperse_rate);
                let disperse_q = quat_mul(org_q, qinv(remain_q));
                tree.set_local_rot(t_idx, remain_q);
                tree.set_local_rot(d_idx, quat_mul(tree.nodes[d_idx].local_rot, disperse_q));
            }
        }
        tree
    }

    fn quat_angle_err_rad(a: Quat, b: Quat) -> f32 {
        let dot = qdot(qnorm(a), qnorm(b)).abs().clamp(0.0, 1.0);
        2.0 * dot.acos()
    }

    #[test]
    fn shaped_rig_vmd_solve_hits_ik_foot_targets_without_double_scaling() {
        let Some(_) = mmdd_shape_template() else {
            eprintln!("skipping: shared base_v1 globals not present");
            return;
        };
        let ini = CharaIniConfig::parse(DEFAULT_CHARA_INI).expect("parse ini");
        let make_kf = |name: &str, frame_no: u32, pos: V3, rot: Quat| VmdBoneKeyFrame {
            name: name.to_string(),
            frame_no,
            pos,
            rot,
            ..Default::default()
        };
        let vmd = VmdFile {
            model_name: "TestModel".into(),
            bone_keyframes: vec![
                make_kf("センター", 0, [0.0; 3], [0.0, 0.0, 0.0, 1.0]),
                make_kf("センター", 5, [0.6, -2.4, 0.5], [0.0, 0.0, 0.0, 1.0]),
                make_kf("左足ＩＫ", 0, [0.0; 3], [0.0, 0.0, 0.0, 1.0]),
                make_kf("左足ＩＫ", 5, [0.4, 0.0, -0.35], [0.0, 0.0, 0.0, 1.0]),
                make_kf("右足ＩＫ", 0, [0.0; 3], [0.0, 0.0, 0.0, 1.0]),
                make_kf("右足ＩＫ", 5, [-0.4, 0.0, 0.35], [0.0, 0.0, 0.0, 1.0]),
                make_kf("両目", 5, [0.0; 3], euler_deg_to_quat([6.0, -9.0, 0.0])),
            ],
            morph_keyframes: Vec::new(),
            camera_keyframes: Vec::new(),
            ik_keyframes: Vec::new(),
        };

        // Case A: Short character (height = 0.0, custom leg sliders)
        let mut short_body = vec![0.5f32; 44];
        short_body[0] = 0.0;
        for s in &mut short_body[29..34] {
            *s = 0.15;
        }
        let mut short_sess =
            make_shaped_test_session(short_body, vec![0.5; 52], Vec::new()).unwrap();
        let short_rig = MmddCharacterRig::from_session(&short_sess, 1.0).unwrap();
        assert!(short_rig.height_scale < 0.92, "expected short height_scale, got {}", short_rig.height_scale);

        // Case B: Tall character (height = 1.0, custom leg sliders + ABMX on cf_n_height & cf_j_hips)
        let mut tall_body = vec![0.5f32; 44];
        tall_body[0] = 1.0;
        for s in &mut tall_body[29..34] {
            *s = 0.85;
        }
        let tall_abm = vec![
            crate::abmx::AbmBone {
                name: "cf_n_height".into(),
                location: 0,
                coord: vec![crate::abmx::AbmData {
                    scale: [1.0, 1.12, 1.0],
                    length: 1.0,
                    pos: [0.0; 3],
                    rot: [0.0; 3],
                }],
            },
            crate::abmx::AbmBone {
                name: "cf_j_hips".into(),
                location: 0,
                coord: vec![crate::abmx::AbmData {
                    scale: [1.0; 3],
                    length: 1.08,
                    pos: [0.0, 0.015, -0.005],
                    rot: [0.0; 3],
                }],
            },
            crate::abmx::AbmBone {
                name: "cf_j_leg01_L".into(),
                location: 0,
                coord: vec![crate::abmx::AbmData {
                    scale: [1.0; 3],
                    length: 1.15,
                    pos: [0.0; 3],
                    rot: [0.0; 3],
                }],
            },
        ];
        let mut tall_sess = make_shaped_test_session(tall_body, vec![0.5; 52], tall_abm).unwrap();
        let tall_rig = MmddCharacterRig::from_session(&tall_sess, 1.0).unwrap();
        // KKS height=1.0 gives cf_n_height.scale.y == 1.0 (vs ~0.95 at 0.5, ~0.88 at 0.0), times ABMX 1.12 == 1.12
        assert!(
            (tall_rig.height_scale - 1.12).abs() < 1e-3,
            "expected tall(1.0)*ABMX(1.12) height_scale ~= 1.12, got {}",
            tall_rig.height_scale
        );

        // Verify obj_scale_y feeds auto_char_scale without mutating ChaControl-local rig rest
        let scaled_obj_rig = MmddCharacterRig::from_session(&tall_sess, 1.25).unwrap();
        assert!((scaled_obj_rig.auto_char_scale() - tall_rig.height_scale * 1.25).abs() < 1e-5);

        let params = MotionSolveParams::default();
        let mut unshaped_foot_err_on_tall = 0.0f32;

        for (label, sess, rig) in [
            ("short", &mut short_sess, &short_rig),
            ("tall_abmx", &mut tall_sess, &tall_rig),
        ] {
            let ref_tree = evaluate_reference_dummy_tree(&vmd, &ini, &params, rig, 5.0);
            // Ground-truth KKS `vmdlib.py` `DoBoneSync` (`DefWeightSyncMode=global`) on `rig.live_rest`
            let mut kks_body = rig.live_rest.clone();
            let kks_tracks = family_tracks(&kks_body);
            for &(id, ref track) in &kks_tracks {
                let Some(bname) = ini.bones.iter().find_map(|(dn, bd)| {
                    bd.weight_bone
                        .as_ref()
                        .filter(|wb| wb.eq_ignore_ascii_case(&track.bone))
                        .map(|_| dn.as_str())
                }) else {
                    continue;
                };
                if bname == "root" {
                    continue;
                }
                let bdef = &ini.bones[bname];
                let d_idx = ref_tree.by_name[bname];
                let sync_pos = bdef.is_position || (bdef.is_track && bdef.track_ptn.contains('p'));
                let sync_rot = bdef.is_rotation || (bdef.is_track && bdef.track_ptn.contains('r'));
                if bdef.weight_sync_local {
                    if let Some(tf) = kks_body.tfs.get_mut(&id) {
                        if sync_pos {
                            tf.pos = ref_tree.nodes[d_idx].local_pos;
                        }
                        if sync_rot {
                            tf.rot = ref_tree.nodes[d_idx].local_rot;
                        }
                    }
                } else {
                    let parent = kks_body.tfs.get(&id).and_then(|t| t.parent);
                    let p_rot = parent
                        .map(|pid| crate::pose::world_rotation_of(pid, &kks_body.tfs))
                        .unwrap_or([0.0, 0.0, 0.0, 1.0]);
                    let local_pos = if sync_pos {
                        parent
                            .and_then(|pid| {
                                crate::pose::mat_inverse_checked(&crate::pose::world_of_id(
                                    pid,
                                    &kks_body.tfs,
                                ))
                            })
                            .map(|inv| crate::pose::mat_vec(&inv, ref_tree.nodes[d_idx].world_pos))
                    } else {
                        None
                    };
                    if let Some(tf) = kks_body.tfs.get_mut(&id) {
                        if let Some(lp) = local_pos {
                            tf.pos = lp;
                        }
                        if sync_rot {
                            tf.rot = qnorm(quat_mul(qinv(p_rot), ref_tree.nodes[d_idx].world_rot));
                        }
                    }
                }
            }
            kks_body.rebuild_worlds();
            let exp_hips = ref_tree.nodes[ref_tree.by_name["hips"]].world_pos;
            let (exp_foot_l, exp_foot_l_rot) = skel_world_pos_rot(&kks_body, "cf_j_leg03_L");
            let (exp_foot_r, exp_foot_r_rot) = skel_world_pos_rot(&kks_body, "cf_j_leg03_R");

            let anim = solve_vmd_to_okanim_for_rig(&vmd, &ini, &params, label, Some(rig))
                .expect("solve shaped rig");
            assert!(
                anim.curves.iter().any(|c| c.name == "face.eye_gaze_lh"),
                "{label}: grafted head should emit eye gaze curves"
            );
            let anim_bytes = anim.to_bytes().unwrap();
            let mut player =
                crate::okanim::Player::new(r#"{"type":"anim","anim":0}"#, &[&anim_bytes]).unwrap();
            crate::okanim::solve::apply(&mut player, &mut [sess], 5.0 / 30.0, 0.0).unwrap();

            let body = sess.body_skel.as_ref().unwrap();
            let (got_hips, _) = skel_world_pos_rot(body, "cf_j_hips");
            let (got_foot_l, got_foot_l_rot) = skel_world_pos_rot(body, "cf_j_leg03_L");
            let (got_foot_r, got_foot_r_rot) = skel_world_pos_rot(body, "cf_j_leg03_R");

            let hips_err = vlen(vsub(got_hips, exp_hips));
            let foot_l_err = vlen(vsub(got_foot_l, exp_foot_l));
            let foot_r_err = vlen(vsub(got_foot_r, exp_foot_r));
            assert!(
                hips_err < 1e-4,
                "{label}: hips world pos mismatch (double-scaling check): got={got_hips:?} exp={exp_hips:?} err={hips_err}"
            );
            assert!(
                foot_l_err < 1e-4 && foot_r_err < 1e-4,
                "{label}: foot KKS DoBoneSync pos mismatch: L err={foot_l_err}, R err={foot_r_err}"
            );
            assert!(
                quat_angle_err_rad(got_foot_l_rot, exp_foot_l_rot) < 5e-4
                    && quat_angle_err_rad(got_foot_r_rot, exp_foot_r_rot) < 5e-4,
                "{label}: foot KKS DoBoneSync rot mismatch"
            );

            if label == "tall_abmx" {
                let unshaped = solve_vmd_to_okanim(&vmd, &ini, &params, "unshaped")
                    .expect("solve unshaped");
                let unshaped_bytes = unshaped.to_bytes().unwrap();
                let mut u_player =
                    crate::okanim::Player::new(r#"{"type":"anim","anim":0}"#, &[&unshaped_bytes])
                        .unwrap();
                crate::okanim::solve::apply(&mut u_player, &mut [sess], 5.0 / 30.0, 0.0).unwrap();
                let u_body = sess.body_skel.as_ref().unwrap();
                let (u_foot_l, _) = skel_world_pos_rot(u_body, "cf_j_leg03_L");
                unshaped_foot_err_on_tall = vlen(vsub(u_foot_l, exp_foot_l));
            }
        }
        assert!(
            unshaped_foot_err_on_tall > 0.015,
            "expected unshaped standard skeleton to have significant foot IK error on tall+ABMX rig, got {unshaped_foot_err_on_tall}"
        );
    }

    #[test]
    fn mmdd_world_lock_preserves_global_cf_j_rotations_across_expression_bone_and_abmx() {
        let Some(_) = mmdd_shape_template() else {
            eprintln!("skipping: shared base_v1 globals not present");
            return;
        };
        let ini = CharaIniConfig::parse(DEFAULT_CHARA_INI).expect("parse ini");
        let make_kf = |name: &str, frame_no: u32, pos: V3, euler_deg: V3| VmdBoneKeyFrame {
            name: name.to_string(),
            frame_no,
            pos,
            rot: euler_deg_to_quat(euler_deg),
            ..Default::default()
        };
        let vmd = VmdFile {
            model_name: "TestWorldLock".into(),
            bone_keyframes: vec![
                make_kf("センター", 3, [0.0, -1.8, 0.2], [4.0, -8.0, 2.0]),
                make_kf("上半身", 3, [0.0; 3], [12.0, -15.0, 8.0]),
                make_kf("上半身2", 3, [0.0; 3], [8.0, 10.0, -6.0]),
                make_kf("首", 3, [0.0; 3], [-5.0, 18.0, 4.0]),
                make_kf("頭", 3, [0.0; 3], [7.0, -10.0, 3.0]),
                make_kf("左肩", 3, [0.0; 3], [0.0, 0.0, 12.0]),
                make_kf("左腕", 3, [0.0; 3], [15.0, -20.0, 52.0]),
                make_kf("左腕捩", 3, [0.0; 3], [32.0, 0.0, 0.0]),
                make_kf("左ひじ", 3, [0.0; 3], [0.0, -48.0, 0.0]),
                make_kf("左手捩", 3, [0.0; 3], [-28.0, 0.0, 0.0]),
                make_kf("左手首", 3, [0.0; 3], [10.0, 15.0, -12.0]),
                make_kf("右肩", 3, [0.0; 3], [0.0, 0.0, -10.0]),
                make_kf("右腕", 3, [0.0; 3], [-10.0, 18.0, -46.0]),
                make_kf("右ひじ", 3, [0.0; 3], [0.0, 42.0, 0.0]),
                make_kf("左足ＩＫ", 3, [0.2, 0.4, -0.3], [0.0; 3]),
                make_kf("右足ＩＫ", 3, [-0.2, 0.2, 0.25], [0.0; 3]),
            ],
            morph_keyframes: Vec::new(),
            camera_keyframes: Vec::new(),
            ik_keyframes: Vec::new(),
        };

        let abm = vec![
            crate::abmx::AbmBone {
                name: "cf_d_shoulder_L".into(),
                location: 0,
                coord: vec![crate::abmx::AbmData {
                    scale: [1.0; 3],
                    length: 1.0,
                    pos: [0.0; 3],
                    rot: [8.0, -12.0, 15.0],
                }],
            },
            crate::abmx::AbmBone {
                name: "cf_d_shoulder_R".into(),
                location: 0,
                coord: vec![crate::abmx::AbmData {
                    scale: [1.0; 3],
                    length: 1.0,
                    pos: [0.0; 3],
                    rot: [-6.0, 10.0, -14.0],
                }],
            },
            crate::abmx::AbmBone {
                name: "cf_j_arm00_L".into(),
                location: 0,
                coord: vec![crate::abmx::AbmData {
                    scale: [1.0; 3],
                    length: 1.0,
                    pos: [0.0; 3],
                    rot: [0.0, 11.0, -9.0],
                }],
            },
        ];

        let mut sess = make_shaped_test_session(vec![0.5; 44], vec![0.5; 52], abm).unwrap();
        let rig = MmddCharacterRig::from_session(&sess, 1.0).unwrap();
        let params = MotionSolveParams::default();
        let ref_tree = evaluate_reference_dummy_tree(&vmd, &ini, &params, &rig, 3.0);

        let mut anim = solve_vmd_to_okanim_for_rig(&vmd, &ini, &params, "lock_test", Some(&rig))
            .expect("solve lock_test");
        let anim_bytes = anim.to_bytes().unwrap();
        let mut player =
            crate::okanim::Player::new(r#"{"type":"anim","anim":0}"#, &[&anim_bytes]).unwrap();
        crate::okanim::solve::apply(&mut player, &mut [&mut sess], 3.0 / 30.0, 0.0).unwrap();
        assert!(sess.mmdd_world_lock, "mmdd_world_lock should be active for source=mmdd_vmd");

        let body = sess.body_skel.as_ref().unwrap();
        for (dummy_name, cf_j_name) in [
            ("hips", "cf_j_hips"),
            ("waist", "cf_j_waist01"),
            ("spine1", "cf_j_spine01"),
            ("spine2", "cf_j_spine02"),
            ("spine3", "cf_j_spine03"),
            ("neck", "cf_j_neck"),
            ("head", "cf_j_head"),
            ("shoulder_l", "cf_j_shoulder_L"),
            ("uparm_l", "cf_j_arm00_L"),
            ("lowarm_l", "cf_j_forearm01_L"),
            ("hand_l", "cf_j_hand_L"),
            ("shoulder_r", "cf_j_shoulder_R"),
            ("uparm_r", "cf_j_arm00_R"),
            ("lowarm_r", "cf_j_forearm01_R"),
            ("hand_r", "cf_j_hand_R"),
            ("upleg_d_l", "cf_j_thigh00_L"),
            ("lowleg_d_l", "cf_j_leg01_L"),
            ("foot_d_l", "cf_j_leg03_L"),
            ("upleg_d_r", "cf_j_thigh00_R"),
            ("lowleg_d_r", "cf_j_leg01_R"),
            ("foot_d_r", "cf_j_leg03_R"),
        ] {
            let exp_rot = ref_tree.nodes[ref_tree.by_name[dummy_name]].world_rot;
            let (_, got_rot) = skel_world_pos_rot(body, cf_j_name);
            let err = quat_angle_err_rad(got_rot, exp_rot);
            assert!(
                err < 5e-4,
                "bone {cf_j_name} ({dummy_name}) world_rot drifted by {err:.6} rad (got={got_rot:?}, exp={exp_rot:?})"
            );
        }

        // Verify leaf corrective twist helpers were recomputed from the final locked cf_j_* pose
        let (_, arm01_local) = find_skel_bone_ci(body, "cf_d_arm01_L")
            .map(|(id, tf)| (id, tf.rot))
            .unwrap();
        assert!(
            quat_angle_err_rad(arm01_local, [0.0, 0.0, 0.0, 1.0]) > 0.02,
            "expected leaf twist helper cf_d_arm01_L to carry corrective twist rotation, got {arm01_local:?}"
        );

        // Regression guard: non-MMDD Studio animations (`source != "mmdd_vmd"`) do NOT lock over ExpressionBone/ABMX
        anim.meta.remove("source");
        let non_mmdd_bytes = anim.to_bytes().unwrap();
        let mut normal_player =
            crate::okanim::Player::new(r#"{"type":"anim","anim":0}"#, &[&non_mmdd_bytes]).unwrap();
        crate::okanim::solve::apply(&mut normal_player, &mut [&mut sess], 3.0 / 30.0, 0.0).unwrap();
        assert!(!sess.mmdd_world_lock);
        let unlocked_body = sess.body_skel.as_ref().unwrap();
        let (_, unlocked_arm_l) = skel_world_pos_rot(unlocked_body, "cf_j_arm00_L");
        let exp_arm_l = ref_tree.nodes[ref_tree.by_name["uparm_l"]].world_rot;
        assert!(
            quat_angle_err_rad(unlocked_arm_l, exp_arm_l) > 0.1,
            "non-MMDD Studio clip should allow ExpressionBone + ABMX to rotate cf_j_arm00_L normally"
        );
    }

    fn open_vanilla_head_session() -> Option<crate::session::OkSession> {
        for root in mmdd_resource_candidates() {
            if root.join("oklib.json").is_file() {
                let _ = crate::oklib::ensure_library(&root);
            }
            if let Ok(sess) = crate::extract_session(&root, None, 0, None) {
                if sess
                    .parts
                    .iter()
                    .any(|p| p.name.to_ascii_lowercase().contains("cf_o_face"))
                {
                    return Some(sess);
                }
            }
        }
        None
    }

    fn curve_map_at(anim: &OkAnim, vmd_frame: usize) -> std::collections::BTreeMap<String, f32> {
        let sub = (anim.fps / 30.0).round().max(1.0) as usize;
        let idx = vmd_frame * sub;
        anim.curves
            .iter()
            .filter_map(|c| c.values.get(idx).map(|&v| (c.name.clone(), v)))
            .collect()
    }

    #[test]
    fn vmd_wink_asymmetry_and_head_mesh_deformation() {
        let Some(sess) = open_vanilla_head_session() else {
            eprintln!("skipping: vanilla head session not available");
            return;
        };
        let ini = CharaIniConfig::parse(DEFAULT_CHARA_INI).expect("parse ini");
        let params = MotionSolveParams::default();
        let mk = |name: &str, frame_no: u32, weight: f32| VmdMorphKeyFrame {
            name: name.to_string(),
            frame_no,
            weight,
        };
        // Frame 0: neutral open
        // Frame 2: left normal wink (`ウィンク` = 1.0)
        // Frame 4: left half-width normal wink (`ｳｨﾝｸ` = 1.0)
        // Frame 6: right normal wink (`ｳｨﾝｸ右` = 1.0)
        // Frame 8: left happy wink (`ウィンク２` = 1.0)
        // Frame 10: right happy wink (`ｳｨﾝｸ２右` = 1.0)
        let vmd = VmdFile {
            model_name: "WinkTest".into(),
            bone_keyframes: vec![VmdBoneKeyFrame {
                name: "センター".into(),
                frame_no: 12,
                pos: [0.0; 3],
                rot: [0.0, 0.0, 0.0, 1.0],
                ..Default::default()
            }],
            morph_keyframes: vec![
                mk("ウィンク", 0, 0.0),
                mk("ウィンク", 2, 1.0),
                mk("ウィンク", 3, 0.0),
                mk("ｳｨﾝｸ", 3, 0.0),
                mk("ｳｨﾝｸ", 4, 1.0),
                mk("ｳｨﾝｸ", 5, 0.0),
                mk("ｳｨﾝｸ右", 5, 0.0),
                mk("ｳｨﾝｸ右", 6, 1.0),
                mk("ｳｨﾝｸ右", 7, 0.0),
                mk("ウィンク２", 7, 0.0),
                mk("ウィンク２", 8, 1.0),
                mk("ウィンク２", 9, 0.0),
                mk("ｳｨﾝｸ２右", 9, 0.0),
                mk("ｳｨﾝｸ２右", 10, 1.0),
                mk("ｳｨﾝｸ２右", 11, 0.0),
            ],
            camera_keyframes: Vec::new(),
            ik_keyframes: Vec::new(),
        };
        let anim = solve_vmd_to_okanim(&vmd, &ini, &params, "wink_test").expect("solve wink_test");

        let c_neutral = curve_map_at(&anim, 0);
        let c_wink_l = curve_map_at(&anim, 2);
        let c_wink_l_hw = curve_map_at(&anim, 4);
        let c_wink_r = curve_map_at(&anim, 6);
        let c_wink2_l = curve_map_at(&anim, 8);
        let c_wink2_r = curve_map_at(&anim, 10);

        // Curve assertions
        assert!((c_wink_l["face.eye_open_l"] - 0.0).abs() < 1e-4 && (c_wink_l["face.eye_open_r"] - 1.0).abs() < 1e-4);
        assert!((c_wink_l["face.eye_wink_l_happy"] - 0.0).abs() < 1e-4);
        assert!((c_wink_l_hw["face.eye_open_l"] - 0.0).abs() < 1e-4 && (c_wink_l_hw["face.eye_open_r"] - 1.0).abs() < 1e-4);
        assert!((c_wink_r["face.eye_open_l"] - 1.0).abs() < 1e-4 && (c_wink_r["face.eye_open_r"] - 0.0).abs() < 1e-4);
        assert!((c_wink2_l["face.eye_open_l"] - 0.0).abs() < 1e-4 && (c_wink2_l["face.eye_wink_l_happy"] - 1.0).abs() < 1e-4);
        assert!((c_wink2_r["face.eye_open_r"] - 0.0).abs() < 1e-4 && (c_wink2_r["face.eye_wink_r_happy"] - 1.0).abs() < 1e-4);
        assert!(!c_neutral.contains_key("face.blush"), "VMD without blush must not emit face.blush");
        assert!(!c_neutral.contains_key("face.iris_small"), "VMD without iris morph must not emit face.iris_small");

        // Actual 3D vertex displacement check on eyeline + face meshes!
        // Note: in Unity ChaControl local space, character's LEFT eye has X < 0 or X > 0 (opposite signs across X = 0).
        let face_idx = sess
            .parts
            .iter()
            .position(|p| p.name.to_ascii_lowercase().contains("cf_o_eyeline_00_up"))
            .or_else(|| {
                sess.parts
                    .iter()
                    .position(|p| p.name.to_ascii_lowercase().contains("cf_o_face"))
            })
            .expect("head eyeline/face part");

        let v0 = sess.evaluate_part_face_vertices(face_idx, &c_neutral).unwrap();
        let v_wl = sess.evaluate_part_face_vertices(face_idx, &c_wink_l).unwrap();
        let v_wr = sess.evaluate_part_face_vertices(face_idx, &c_wink_r).unwrap();
        let v_w2l = sess.evaluate_part_face_vertices(face_idx, &c_wink2_l).unwrap();
        let v_w2r = sess.evaluate_part_face_vertices(face_idx, &c_wink2_r).unwrap();

        let side_disp = |va: &[[f32; 3]], vb: &[[f32; 3]]| -> (f32, f32) {
            let (mut neg_x, mut pos_x) = (0.0f32, 0.0f32);
            for (p0, p1) in va.iter().zip(vb.iter()) {
                let d = vlen(vsub(*p1, *p0));
                if p0[0] < -0.005 {
                    neg_x = neg_x.max(d);
                } else if p0[0] > 0.005 {
                    pos_x = pos_x.max(d);
                }
            }
            (neg_x, pos_x)
        };

        let (wl_neg, wl_pos) = side_disp(&v0, &v_wl);
        let (wr_neg, wr_pos) = side_disp(&v0, &v_wr);
        println!(
            "Wink 3D vertex disp ({}): wink_L=(negX={wl_neg:.5}, posX={wl_pos:.5}), wink_R=(negX={wr_neg:.5}, posX={wr_pos:.5})",
            sess.parts[face_idx].name
        );
        // Left wink moves ONLY one side (> 3mm) while the opposite eye stays completely open (< 0.1mm), and Right wink moves the OTHER side!
        let (wl_active, wl_idle) = if wl_neg > wl_pos { (wl_neg, wl_pos) } else { (wl_pos, wl_neg) };
        let (wr_active, wr_idle) = if wr_neg > wr_pos { (wr_neg, wr_pos) } else { (wr_pos, wr_neg) };
        assert!(wl_active > 0.003 && wl_idle < 1e-4, "left wink must close only one eye: active={wl_active}, idle={wl_idle}");
        assert!(wr_active > 0.003 && wr_idle < 1e-4, "right wink must close only one eye: active={wr_active}, idle={wr_idle}");
        assert!(
            (wl_neg > wl_pos) != (wr_neg > wr_pos),
            "left wink and right wink must deform opposite X hemispheres!"
        );

        // Verify normal wink (`wink*_op`) vs happy squeezed wink (`gyu*_op`) produce distinct 3D lid geometry
        let diff_l_happy = v_wl
            .iter()
            .zip(v_w2l.iter())
            .map(|(a, b)| vlen(vsub(*a, *b)))
            .fold(0.0f32, f32::max);
        let diff_r_happy = v_wr
            .iter()
            .zip(v_w2r.iter())
            .map(|(a, b)| vlen(vsub(*a, *b)))
            .fold(0.0f32, f32::max);
        println!("Normal vs Happy wink max vertex diff: L={diff_l_happy:.5}, R={diff_r_happy:.5}");
        assert!(diff_l_happy > 0.0005, "expected distinct lid vertices between ウィンク and ウィンク２, got {diff_l_happy}");
        assert!(diff_r_happy > 0.0005, "expected distinct lid vertices between ｳｨﾝｸ右 and ｳｨﾝｸ２右, got {diff_r_happy}");
    }

    #[test]
    fn vmd_blush_iris_shrink_and_compound_morphs() {
        let Some(mut sess) = open_vanilla_head_session() else {
            eprintln!("skipping: vanilla head session not available");
            return;
        };
        let ini = CharaIniConfig::parse(DEFAULT_CHARA_INI).expect("parse ini");
        let params = MotionSolveParams::default();
        let mk = |name: &str, frame_no: u32, weight: f32| VmdMorphKeyFrame {
            name: name.to_string(),
            frame_no,
            weight,
        };
        let vmd = VmdFile {
            model_name: "Stage3Compound".into(),
            bone_keyframes: vec![VmdBoneKeyFrame {
                name: "センター".into(),
                frame_no: 8,
                pos: [0.0; 3],
                rot: [0.0, 0.0, 0.0, 1.0],
                ..Default::default()
            }],
            morph_keyframes: vec![
                // Cheek blush (`照れ`)
                mk("照れ", 0, 0.0),
                mk("照れ", 2, 0.75),
                // Iris shrink (`瞳小`)
                mk("瞳小", 0, 0.0),
                mk("瞳小", 2, 0.6),
                // Happy closed smile eyes (`笑い` 0 -> 0.5 -> 1.0)
                mk("笑い", 0, 0.0),
                mk("笑い", 4, 1.0),
                // Smooth eyebrow (`困る` = 0.4 at frame 2, `上` = 1.0 at frame 6)
                mk("困る", 0, 0.0),
                mk("困る", 2, 0.4),
                mk("困る", 4, 1.0),
                mk("上", 4, 0.0),
                mk("上", 6, 1.0),
                // Compound mouth: `あ` = 0.8 + `にやり` = 0.7 simultaneously at frame 2, plus `口横広げ` = 1.0 at frame 6
                mk("あ", 0, 0.0),
                mk("あ", 2, 0.8),
                mk("にやり", 0, 0.0),
                mk("にやり", 2, 0.7),
                mk("口横広げ", 4, 0.0),
                mk("口横広げ", 6, 1.0),
                // cha_eyeset mapped expression (`なごみ` = 31 -> valid EyesCtrl pattern)
                mk("なごみ", 5, 0.0),
                mk("なごみ", 6, 1.0),
            ],
            camera_keyframes: Vec::new(),
            ik_keyframes: Vec::new(),
        };
        let anim = solve_vmd_to_okanim(&vmd, &ini, &params, "compound_test").expect("solve compound");
        let c0 = curve_map_at(&anim, 0);
        let c2 = curve_map_at(&anim, 2);
        let c4 = curve_map_at(&anim, 4);
        let c6 = curve_map_at(&anim, 6);

        // 1. Blush (`照れ` = 0.75 -> `_overcolor2[3] == 0.15` on head skin, untouched on body)
        let body_over2_before: Vec<f32> = sess
            .parts
            .iter()
            .filter(|p| p.role == "body")
            .map(|p| p.overcolor2[3])
            .collect();
        assert!((c2["face.blush"] - 0.75).abs() < 1e-4);
        assert!(sess.set_blush(c2["face.blush"]));
        assert!((sess.blush_alpha().unwrap() - 0.75 * 0.2).abs() < 1e-5);
        let mut checked_head_skin = 0usize;
        for p in &sess.parts {
            if p.role == "head" && matches!(p.shader_kind, 5 | 7) {
                checked_head_skin += 1;
                assert!(
                    (p.overcolor2[3] - 0.15).abs() < 1e-5,
                    "part {} overcolor2 alpha expected 0.15, got {}",
                    p.name,
                    p.overcolor2[3]
                );
            }
        }
        assert!(checked_head_skin > 0, "expected at least one head skin part");
        let body_over2_after: Vec<f32> = sess
            .parts
            .iter()
            .filter(|p| p.role == "body")
            .map(|p| p.overcolor2[3])
            .collect();
        assert_eq!(body_over2_before, body_over2_after, "body underhair overcolor2 must not be touched by blush");

        // 2. Iris shrink (`瞳小` = 0.6 -> UV scale increases by ~1 / (1 - 0.99*0.6))
        assert!((c2["face.iris_small"] - 0.6).abs() < 1e-4);
        let hitomi_idx = sess
            .parts
            .iter()
            .position(|p| p.iris_runtime.is_some())
            .expect("hitomi part with iris_runtime");
        let base_uv_span = {
            let uvs = &sess.parts[hitomi_idx].uvs;
            let (min_u, max_u) = uvs
                .iter()
                .fold((f32::INFINITY, f32::NEG_INFINITY), |(mn, mx), uv| {
                    (mn.min(uv[0]), mx.max(uv[0]))
                });
            max_u - min_u
        };
        assert!(sess.set_iris_small(c2["face.iris_small"]));
        let shrunk_uv_span = {
            let uvs = &sess.parts[hitomi_idx].uvs;
            let (min_u, max_u) = uvs
                .iter()
                .fold((f32::INFINITY, f32::NEG_INFINITY), |(mn, mx), uv| {
                    (mn.min(uv[0]), mx.max(uv[0]))
                });
            max_u - min_u
        };
        // Larger UV span across the eyeball mesh means the iris texture occupies a smaller area on the eyeball (`瞳小`)!
        let expected_ratio = 1.0 / (1.0 - 0.99 * 0.6);
        println!("Iris UV span: base={base_uv_span:.4} -> shrunk={shrunk_uv_span:.4} (ratio={:.3}, exp~={expected_ratio:.3})", shrunk_uv_span / base_uv_span);
        assert!(
            shrunk_uv_span > base_uv_span * 1.8,
            "expected iris UV span to expand significantly for 瞳小=0.6, got {base_uv_span} -> {shrunk_uv_span}"
        );
        assert!(sess.set_iris_small(0.0));

        // 3. `笑い` at weight 1.0 closes eyes (`open_l == 0`, `open_r == 0`, `happy == 1`) instead of leaving them wide open!
        assert!((c4["face.eye_open_l"] - 0.0).abs() < 1e-4 && (c4["face.eye_open_r"] - 0.0).abs() < 1e-4);
        assert!((c4["face.eye_wink_l_happy"] - 1.0).abs() < 1e-4 && (c4["face.eye_wink_r_happy"] - 1.0).abs() < 1e-4);

        // 4. `なごみ` (cha_eyeset 31) maps into valid `EyesCtrl` pattern bounds (not out-of-range 31!)
        let face_idx = sess
            .parts
            .iter()
            .position(|p| p.name.to_ascii_lowercase().contains("cf_o_face"))
            .expect("cf_o_face");
        let n_eye_ptns = sess.parts[face_idx].eye_patterns.len();
        let nagomi_ptn = c6["face.eye_pattern"].round() as usize;
        println!("なごみ mapped EyesCtrl pattern = {nagomi_ptn} (total EyesCtrl patterns = {n_eye_ptns})");
        assert!(
            nagomi_ptn < n_eye_ptns,
            "cha_eyeset 31 must resolve within EyesCtrl pattern count {n_eye_ptns}, got {nagomi_ptn}"
        );

        // 5. Smooth eyebrow interpolation (`困る` at w=0.4 gives intermediate vertex displacement between w=0.0 and w=1.0)
        let brow_idx = sess
            .parts
            .iter()
            .position(|p| p.name.to_ascii_lowercase().contains("mayuge"))
            .unwrap_or(face_idx);
        let b0 = sess.evaluate_part_face_vertices(brow_idx, &c0).unwrap();
        let b04 = sess.evaluate_part_face_vertices(brow_idx, &c2).unwrap();
        let b10 = sess.evaluate_part_face_vertices(brow_idx, &c4).unwrap();
        let disp_04 = b0.iter().zip(b04.iter()).map(|(a, b)| vlen(vsub(*b, *a))).fold(0.0f32, f32::max);
        let disp_10 = b0.iter().zip(b10.iter()).map(|(a, b)| vlen(vsub(*b, *a))).fold(0.0f32, f32::max);
        assert!(
            disp_04 > disp_10 * 0.2 && disp_04 < disp_10 * 0.65,
            "expected smooth 0.4 eyebrow interpolation: disp_04={disp_04:.5}, disp_10={disp_10:.5}"
        );

        // 6. Simultaneous `あ=0.8` + `にやり=0.7` differs from pure `あ=0.8` alone on `cf_O_face`
        let mut c2_pure_a = c2.clone();
        c2_pure_a.insert("face.mouth_corner_up".into(), 0.0);
        let m_compound = sess.evaluate_part_face_vertices(face_idx, &c2).unwrap();
        let m_pure_a = sess.evaluate_part_face_vertices(face_idx, &c2_pure_a).unwrap();
        let corner_diff = m_compound
            .iter()
            .zip(m_pure_a.iter())
            .map(|(a, b)| vlen(vsub(*a, *b)))
            .fold(0.0f32, f32::max);
        println!("Compound あ(0.8)+にやり(0.7) vs pure あ(0.8) max mouth vertex diff = {corner_diff:.5}");
        assert!(corner_diff > 0.001, "expected にやり corner smile to blend on top of あ, got {corner_diff}");

        // 7. Facial bone morphs (`上` -> `cf_J_Mayu_ty`, `口横広げ` -> `cf_J_MouthBase_ty` X scale) via `okanim::solve::apply`
        let anim_bytes = anim.to_bytes().unwrap();
        let mut player =
            crate::okanim::Player::new(r#"{"type":"anim","anim":0}"#, &[&anim_bytes]).unwrap();
        crate::okanim::solve::apply(&mut player, &mut [&mut sess], 6.0 / 30.0, 0.0).unwrap();
        let (mayu_rest_y, mouth_rest_sx) = {
            let head_posed = sess.head_skel.as_ref().expect("head_skel");
            let head_rest = sess
                .head_shaped
                .as_ref()
                .or(sess.head_rest.as_ref())
                .expect("head_shaped/head_rest");
            let (_, mayu_posed) = find_skel_bone_ci(head_posed, "cf_J_Mayu_ty").expect("cf_J_Mayu_ty");
            let (_, mayu_rest) = find_skel_bone_ci(head_rest, "cf_J_Mayu_ty").expect("cf_J_Mayu_ty rest");
            let (_, mouth_posed) = find_skel_bone_ci(head_posed, "cf_J_MouthBase_ty").expect("cf_J_MouthBase_ty");
            let (_, mouth_rest) = find_skel_bone_ci(head_rest, "cf_J_MouthBase_ty").expect("cf_J_MouthBase_ty rest");
            assert!(
                (mayu_posed.pos[1] - mayu_rest.pos[1] - 0.06 * 0.083).abs() < 1e-4,
                "expected cf_J_Mayu_ty Y offset +0.00498, got {} vs rest {}",
                mayu_posed.pos[1],
                mayu_rest.pos[1]
            );
            assert!(
                (mouth_posed.scale[0] - mouth_rest.scale[0] * 1.2).abs() < 1e-3,
                "expected cf_J_MouthBase_ty X scale ~1.2x rest for 口横広げ=1.0, got {} vs rest {}",
                mouth_posed.scale[0],
                mouth_rest.scale[0]
            );
            (mayu_rest.pos[1], mouth_rest.scale[0])
        };
        // Verify subsequent frame at t=0 resets `head_skel` cleanly without accumulating offsets
        crate::okanim::solve::apply(&mut player, &mut [&mut sess], 0.0, 0.0).unwrap();
        let head_reset = sess.head_skel.as_ref().expect("head_skel reset");
        let (_, mayu_reset) = find_skel_bone_ci(head_reset, "cf_J_Mayu_ty").unwrap();
        let (_, mouth_reset) = find_skel_bone_ci(head_reset, "cf_J_MouthBase_ty").unwrap();
        assert!(
            (mayu_reset.pos[1] - mayu_rest_y).abs() < 1e-5
                && (mouth_reset.scale[0] - mouth_rest_sx).abs() < 1e-5,
            "expected head_skel facial bone morphs to reset cleanly on frame 0"
        );
    }

    #[test]
    fn shoes_detect_high_heels_stiletto_and_shoe_state_suppression() {
        let Some(mut sess) = make_shaped_test_session(vec![0.5; 44], vec![0.5; 52], Vec::new()) else {
            eprintln!("skipping: standard KK template unavailable");
            return;
        };
        let ini = CharaIniConfig::parse(DEFAULT_CHARA_INI).expect("parse ini");
        assert!(
            ini.bones.contains_key("heel_ex_l") && ini.bones.contains_key("heel_ex_r"),
            "DEFAULT_CHARA_INI must define HEEL_EX_L/R mapped to cf_j_foot_L/R"
        );
        assert_eq!(
            ini.bones["toe_ex_r"].parent.as_deref(),
            Some("heel_ex_r"),
            "TOE_EX_R must be parented under HEEL_EX_R"
        );

        // 1. Stiletto `.txt` config parsing (including 0..360 degree wraparound `333.6774 -> -26.3226`) & flat `.txt` guard
        let tmp_dir = std::env::temp_dir().join(format!("ok_stiletto_test_{}", std::process::id()));
        let heels_dir = tmp_dir.join("BepInEx/config/Stiletto/Heels");
        let _ = std::fs::create_dir_all(&heels_dir);
        std::fs::write(
            heels_dir.join("CustomStilettoPump.txt"),
            "height = 0.0618\nangleLeg = 24.5\nangleAnkle = 19.5\nangleToes = 333.6774\n",
        )
        .unwrap();
        std::fs::write(
            heels_dir.join("ローファー.txt"),
            "height = 0\nangleLeg = 0\nangleAnkle = 0\nangleToes = 0\n",
        )
        .unwrap();
        let custom_preset =
            detect_high_heel_preset("CustomStilettoPump", "", "", &[tmp_dir.as_path()])
                .expect("expected Stiletto .txt preset");
        assert!((custom_preset.rot[0] - 24.5).abs() < 1e-3);
        assert!((custom_preset.rot[1] - 19.5).abs() < 1e-3);
        assert!((custom_preset.rot[2] - (-26.3226)).abs() < 1e-3);
        assert!((custom_preset.height - 0.0618).abs() < 1e-4);
        assert_eq!(
            detect_high_heel_preset("ローファー", "", "", &[tmp_dir.as_path()]),
            None,
            "flat vanilla ローファー must not trigger high-heel compensation"
        );
        assert_eq!(
            detect_high_heel_preset("上履き", "", "", &[]),
            None,
            "flat indoor 上履き must not trigger high-heel compensation"
        );
        assert!(
            detect_high_heel_preset("パンプス (Stiletto)", "", "", &[]).is_some(),
            "built-in Stiletto preset must be detected without external .txt"
        );
        assert!(
            detect_high_heel_preset("Vanilla Heels B", "", "", &[]).is_some(),
            "built-in Vanilla Heels B preset must be detected"
        );

        // 2. Solve standing VMD across barefoot / flat shoes / active high heels / shoes-off (`clothesState[7] = 3`)
        let vmd = VmdFile {
            model_name: "HeelsTest".into(),
            bone_keyframes: vec![VmdBoneKeyFrame {
                name: "全ての親".into(),
                frame_no: 0,
                pos: [0.0, 0.0, 0.0],
                rot: [0.0, 0.0, 0.0, 1.0],
                ..Default::default()
            }],
            morph_keyframes: Vec::new(),
            camera_keyframes: Vec::new(),
            ik_keyframes: Vec::new(),
        };
        let track_pose = |anim: &OkAnim, bone: &str| -> ([f32; 3], [f32; 4]) {
            let ti = anim
                .tracks
                .iter()
                .position(|t| t.bone.eq_ignore_ascii_case(bone))
                .unwrap_or_else(|| panic!("missing track {bone}"));
            let tr = &anim.tracks[ti];
            let local_rot = quat_mul(tr.rest_local, anim.rot[ti]);
            let local_pos = if bone.eq_ignore_ascii_case(&anim.hips_bone) && !anim.hips.is_empty() {
                let hlen = vlen(anim.hips_rest).max(1e-4);
                [
                    anim.hips[0][0] * hlen,
                    anim.hips[0][1] * hlen,
                    anim.hips[0][2] * hlen,
                ]
            } else {
                [0.0; 3]
            };
            (local_pos, local_rot)
        };

        let mut rig_flat = MmddCharacterRig::from_session(&sess, 1.0).unwrap();
        rig_flat.shoe_state = MmddShoeState {
            has_shoes: true,
            shoes_active: true,
            shoe_name: "ローファー".into(),
            detected_heel: None,
        };
        let mut rig_heels_on = rig_flat.clone();
        rig_heels_on.shoe_state = MmddShoeState {
            has_shoes: true,
            shoes_active: true,
            shoe_name: "CustomStilettoPump".into(),
            detected_heel: Some(custom_preset),
        };
        let mut rig_heels_off = rig_heels_on.clone();
        rig_heels_off.shoe_state.shoes_active = false;

        let default_params = MotionSolveParams::default();
        let anim_flat =
            solve_vmd_to_okanim_for_rig(&vmd, &ini, &default_params, "flat", Some(&rig_flat))
                .unwrap();
        let anim_on =
            solve_vmd_to_okanim_for_rig(&vmd, &ini, &default_params, "heels_on", Some(&rig_heels_on))
                .unwrap();
        let anim_off = solve_vmd_to_okanim_for_rig(
            &vmd,
            &ini,
            &default_params,
            "heels_off",
            Some(&rig_heels_off),
        )
        .unwrap();

        let eval_world_hips = |s: &mut crate::session::OkSession, anim: &OkAnim| -> [f32; 3] {
            let bytes = anim.to_bytes().unwrap();
            let mut player =
                crate::okanim::Player::new(r#"{"type":"anim","anim":0}"#, &[&bytes]).unwrap();
            crate::okanim::solve::apply(&mut player, &mut [s], 0.0, 0.0).unwrap();
            let body = s.body_skel.as_ref().unwrap();
            skel_world_pos_rot(body, "cf_j_hips").0
        };

        let (hips_flat, thigh_flat) = (
            eval_world_hips(&mut sess, &anim_flat),
            track_pose(&anim_flat, "cf_j_thigh00_R").1,
        );
        let (hips_on, thigh_on) = (
            eval_world_hips(&mut sess, &anim_on),
            track_pose(&anim_on, "cf_j_thigh00_R").1,
        );
        let hips_off = eval_world_hips(&mut sess, &anim_off);

        let (_, leg03_flat) = track_pose(&anim_flat, "cf_j_leg03_R");
        let (_, foot_flat) = track_pose(&anim_flat, "cf_j_foot_R");
        let (_, toes_flat) = track_pose(&anim_flat, "cf_j_toes_R");

        let (_, leg03_on) = track_pose(&anim_on, "cf_j_leg03_R");
        let (_, foot_on) = track_pose(&anim_on, "cf_j_foot_R");
        let (_, toes_on) = track_pose(&anim_on, "cf_j_toes_R");

        let (_, leg03_off) = track_pose(&anim_off, "cf_j_leg03_R");
        let (_, foot_off) = track_pose(&anim_off, "cf_j_foot_R");
        let (_, toes_off) = track_pose(&anim_off, "cf_j_toes_R");

        let euler_leg03 = quat_to_unity_euler(quat_mul(qinv(leg03_flat), leg03_on));
        let euler_foot = quat_to_unity_euler(quat_mul(qinv(foot_flat), foot_on));
        let euler_toes = quat_to_unity_euler(quat_mul(qinv(toes_flat), toes_on));
        let pitch_leg03 = normalize_signed_deg(euler_leg03[0]);
        let pitch_foot = normalize_signed_deg(euler_foot[0]);
        let pitch_toes = normalize_signed_deg(euler_toes[0]);
        println!(
            "High-heels solved deltas: leg03_pitch={pitch_leg03:.2}°, foot_pitch={pitch_foot:.2}°, toes_pitch={pitch_toes:.2}°, hips_dy={:.4}m",
            hips_on[1] - hips_flat[1]
        );
        assert!(
            (pitch_leg03 - 24.5).abs() < 1.0,
            "expected cf_j_leg03_R pitch ~+24.5°, got {pitch_leg03}"
        );
        assert!(
            (pitch_foot - 19.5).abs() < 1.0,
            "expected cf_j_foot_R pitch ~+19.5°, got {pitch_foot}"
        );
        assert!(
            (pitch_toes - (-26.32)).abs() < 1.0,
            "expected cf_j_toes_R counter-pitch ~-26.32°, got {pitch_toes}"
        );
        assert!(
            ((hips_on[1] - hips_flat[1]) - 0.0618).abs() < 1e-3,
            "expected hips Y lifted by +0.0618m, got {}",
            hips_on[1] - hips_flat[1]
        );
        // `upleg_d` (`cf_j_thigh00_R`) must NOT be rotated when IK is at rest and high heels only pitch foot/heel/toes
        assert!(
            qdot(thigh_flat, thigh_on).abs() > 0.9999,
            "high heels must not spuriously rotate cf_j_thigh00_R"
        );
        // When shoes are taken off (`shoes_active == false`), heel rotation and lift are completely suppressed!
        assert!(
            qdot(leg03_flat, leg03_off).abs() > 0.99999
                && qdot(foot_flat, foot_off).abs() > 0.99999
                && qdot(toes_flat, toes_off).abs() > 0.99999
                && (hips_off[1] - hips_flat[1]).abs() < 1e-5,
            "shoes OFF must suppress high-heel angles and lift"
        );

        // 3. `ShoesOffset` `[true, on_y, off_y]` + `custom_base_pos` (`posadj`) no-double-add verification
        let mut so_params = MotionSolveParams {
            shoes_offset: Some((true, 0.065, -0.015)),
            ..Default::default()
        };
        so_params.custom_base_pos.insert("root".into(), [0.0, 0.065, 0.0]);
        let anim_so_on =
            solve_vmd_to_okanim_for_rig(&vmd, &ini, &so_params, "so_on", Some(&rig_heels_on))
                .unwrap();
        let anim_so_off =
            solve_vmd_to_okanim_for_rig(&vmd, &ini, &so_params, "so_off", Some(&rig_heels_off))
                .unwrap();
        let dy_so_on = eval_world_hips(&mut sess, &anim_so_on)[1] - hips_flat[1];
        let dy_so_off = eval_world_hips(&mut sess, &anim_so_off)[1] - hips_flat[1];
        assert!(
            (dy_so_on - 0.065).abs() < 1e-3,
            "expected exact +0.065m (no 2x double-add with posadj), got {dy_so_on}"
        );
        assert!(
            (dy_so_off - (-0.015)).abs() < 1e-3,
            "expected ShoesOffset[2] = -0.015m when shoes are off, got {dy_so_off}"
        );

        // 4. Live `ok_okanim_from_vmd` C ABI shoe detection & cache invalidation when toggling shoe visibility
        let vmd_path = tmp_dir.join("live_shoe_test.vmd");
        {
            // Minimal valid 30-byte header + 20-byte model + 4-byte bone count(1) + 111-byte bone kf + 4-byte morph(0) + 4-byte cam(0) + 4-byte light(0)
            let mut raw = Vec::new();
            let mut hdr = [0u8; 30];
            hdr[..25].copy_from_slice(b"Vocaloid Motion Data 0002");
            raw.extend_from_slice(&hdr);
            raw.extend_from_slice(&[0u8; 20]);
            raw.extend_from_slice(&1u32.to_le_bytes());
            let mut bname = [0u8; 15];
            // Shift-JIS for "全ての親": 0x91, 0x53, 0x82, 0xc4, 0x82, 0xcc, 0x90, 0x65
            let sjis = [0x91u8, 0x53, 0x82, 0xc4, 0x82, 0xcc, 0x90, 0x65];
            bname[..sjis.len()].copy_from_slice(&sjis);
            raw.extend_from_slice(&bname);
            raw.extend_from_slice(&0u32.to_le_bytes());
            for f in [0.0f32, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0] {
                raw.extend_from_slice(&f.to_le_bytes());
            }
            raw.extend_from_slice(&[20u8; 64]);
            for _ in 0..3 {
                raw.extend_from_slice(&0u32.to_le_bytes());
            }
            std::fs::write(&vmd_path, &raw).unwrap();
        }
        sess.game_root = tmp_dir.clone();
        sess.active_shoe_meta = Some(("CustomStilettoPump".into(), String::new(), String::new()));
        let mut shoe_part =
            crate::session::SessionPart::from_mesh("clothes_shoes", &Default::default(), "opaque", false);
        shoe_part.name = "CustomStilettoPump".into();
        shoe_part.render_visible = true;
        sess.parts.push(shoe_part);

        let c_vmd = std::ffi::CString::new(vmd_path.to_str().unwrap()).unwrap();
        let c_params = std::ffi::CString::new("{}").unwrap();
        let solve_via_ffi = |s: &crate::session::OkSession| -> OkAnim {
            unsafe {
                let sz = crate::okanim::ffi::ok_okanim_from_vmd(
                    s,
                    c_vmd.as_ptr(),
                    c_params.as_ptr(),
                    std::ptr::null_mut(),
                    0,
                );
                assert!(sz > 0, "ok_okanim_from_vmd size failed: {sz}");
                let mut buf = vec![0u8; sz as usize];
                let wrote = crate::okanim::ffi::ok_okanim_from_vmd(
                    s,
                    c_vmd.as_ptr(),
                    c_params.as_ptr(),
                    buf.as_mut_ptr(),
                    sz,
                );
                assert_eq!(wrote, sz);
                OkAnim::from_bytes(&buf).unwrap()
            }
        };
        let ffi_on = solve_via_ffi(&sess);
        sess.parts[0].render_visible = false; // Take shoes off live!
        let ffi_off = solve_via_ffi(&sess);
        let ffi_pitch = normalize_signed_deg(
            quat_to_unity_euler(quat_mul(
                qinv(track_pose(&ffi_off, "cf_j_leg03_R").1),
                track_pose(&ffi_on, "cf_j_leg03_R").1,
            ))[0],
        );
        assert!(
            (ffi_pitch - 24.5).abs() < 1.0,
            "live ok_okanim_from_vmd must detect high heels when visible and invalidate cache when hidden, got pitch diff {ffi_pitch}"
        );

        let _ = std::fs::remove_dir_all(&tmp_dir);
    }

    #[test]
    fn vmd_head_bone_morphs_skin_vertices_and_skel_export_end_to_end() {
        let Some(mut sess) = open_vanilla_head_session() else {
            eprintln!("skipping: vanilla head session not available");
            return;
        };
        let _ = sess.ensure_skel_export();
        let ini = CharaIniConfig::parse(DEFAULT_CHARA_INI).expect("parse ini");
        let params = MotionSolveParams::default();
        let mk = |name: &str, frame_no: u32, weight: f32| VmdMorphKeyFrame {
            name: name.to_string(),
            frame_no,
            weight,
        };
        let vmd = VmdFile {
            model_name: "HeadBoneMorphE2E".into(),
            bone_keyframes: vec![VmdBoneKeyFrame {
                name: "センター".into(),
                frame_no: 6,
                pos: [0.0; 3],
                rot: [0.0, 0.0, 0.0, 1.0],
                ..Default::default()
            }],
            morph_keyframes: vec![
                mk("上", 0, 0.0),
                mk("上", 2, 1.0),
                mk("上", 4, 0.0),
                mk("下", 2, 0.0),
                mk("下", 4, 1.0),
                mk("下", 6, 0.0),
                mk("口横広げ", 2, 0.0),
                mk("口横広げ", 4, 1.0),
                mk("口横広げ", 6, 0.0),
            ],
            camera_keyframes: Vec::new(),
            ik_keyframes: Vec::new(),
        };
        let anim = solve_vmd_to_okanim(&vmd, &ini, &params, "head_bone_e2e").unwrap();
        let anim_bytes = anim.to_bytes().unwrap();
        let mut player =
            crate::okanim::Player::new(r#"{"type":"anim","anim":0}"#, &[&anim_bytes]).unwrap();

        let brow_idx = sess
            .parts
            .iter()
            .position(|p| p.name.to_ascii_lowercase().contains("mayuge"))
            .expect("cf_O_mayuge part");
        let face_idx = sess
            .parts
            .iter()
            .position(|p| p.name.to_ascii_lowercase().contains("cf_o_face"))
            .expect("cf_O_face part");

        let step_and_capture = |sess: &mut crate::session::OkSession,
                                player: &mut crate::okanim::Player,
                                t: f32|
         -> (
            [f32; 3],
            [f32; 3],
            Vec<[f32; 3]>,
            Vec<[f32; 3]>,
        ) {
            crate::okanim::solve::apply(player, &mut [sess], t, 0.0).unwrap();
            // Refresh GPU export bones (exact path consumed by UE `AKKPreviewActor::RefreshSkinnedDisplay`)
            let (mayu_export_pos, mouth_export_scl) = {
                let mut ex = sess.skel_export.take().expect("skel_export");
                crate::skel_export::refresh_pose(sess, &mut ex);
                let mayu = ex
                    .bones
                    .iter()
                    .find(|b| b.name.eq_ignore_ascii_case("cf_J_Mayu_ty"))
                    .expect("ExportBone cf_J_Mayu_ty")
                    .pose_pos;
                let mouth = ex
                    .bones
                    .iter()
                    .find(|b| b.name.eq_ignore_ascii_case("cf_J_MouthBase_ty"))
                    .expect("ExportBone cf_J_MouthBase_ty")
                    .pose_scl;
                sess.skel_export = Some(ex);
                (mayu, mouth)
            };
            // Also verify `head_skel` and `head_full_posed` stay synchronized
            if let (Some(hs), Some(hf)) = (sess.head_skel.as_ref(), sess.head_full_posed.as_ref()) {
                let (_, hm) = find_skel_bone_ci(hs, "cf_J_Mayu_ty").unwrap();
                let (_, fm) = find_skel_bone_ci(hf, "cf_J_Mayu_ty").unwrap();
                assert!(
                    (hm.pos[1] - fm.pos[1]).abs() < 1e-6,
                    "head_skel and head_full_posed cf_J_Mayu_ty must stay synchronized"
                );
                let (_, hmo) = find_skel_bone_ci(hs, "cf_J_MouthBase_ty").unwrap();
                let (_, fmo) = find_skel_bone_ci(hf, "cf_J_MouthBase_ty").unwrap();
                assert!(
                    (hmo.scale[0] - fmo.scale[0]).abs() < 1e-6,
                    "head_skel and head_full_posed cf_J_MouthBase_ty must stay synchronized"
                );
            }
            // Also run CPU skinning (`sess.skin_all()`) to verify actual 3D vertex deformation on eyebrow & mouth meshes
            sess.skin_all();
            (
                mayu_export_pos,
                mouth_export_scl,
                sess.parts[brow_idx].positions.clone(),
                sess.parts[face_idx].positions.clone(),
            )
        };

        let (mayu0, mouth_scl0, brow_v0, face_v0) = step_and_capture(&mut sess, &mut player, 0.0);
        let (mayu2, _mouth_scl2, brow_v2, _face_v2) =
            step_and_capture(&mut sess, &mut player, 2.0 / 30.0);
        let (_mayu4, mouth_scl4, brow_v4, face_v4) =
            step_and_capture(&mut sess, &mut player, 4.0 / 30.0);
        let (mayu6, mouth_scl6, brow_v6, face_v6) =
            step_and_capture(&mut sess, &mut player, 6.0 / 30.0);

        // Frame 2 (`上 = 1.0`): GPU ExportBone and CPU-skinned eyebrow vertices shift upward (+Y in Unity / +Z in UE export)
        let export_up_delta = vlen(vsub(mayu2, mayu0));
        let brow_up_dy = brow_v2
            .iter()
            .zip(brow_v0.iter())
            .map(|(p2, p0)| p2[1] - p0[1])
            .fold(0.0f32, f32::max);
        // Frame 4 (`下 = 1.0`, `口横広げ = 1.0`): eyebrow shifts downward (-Y) and mouth widens (`pose_scl` * 1.2 & wider |X| vertices)
        let brow_down_dy = brow_v4
            .iter()
            .zip(brow_v0.iter())
            .map(|(p4, p0)| p4[1] - p0[1])
            .fold(0.0f32, f32::min);
        let max_mouth_widen_dx = face_v4
            .iter()
            .zip(face_v0.iter())
            .map(|(p4, p0)| p4[0].abs() - p0[0].abs())
            .fold(0.0f32, f32::max);
        println!(
            "Head bone E2E: export_mayu_disp={export_up_delta:.5}, brow_up_dy=+{brow_up_dy:.5}m, brow_down_dy={brow_down_dy:.5}m, mouth_export_scl={mouth_scl0:?}->{mouth_scl4:?}, mouth_widen_dx=+{max_mouth_widen_dx:.5}m"
        );
        assert!(export_up_delta > 0.004, "expected ExportBone cf_J_Mayu_ty displacement > 0.004, got {export_up_delta}");
        assert!(brow_up_dy > 0.004, "expected cf_O_mayuge vertices to rise > 4mm on 上=1.0, got {brow_up_dy}");
        assert!(brow_down_dy < -0.004, "expected cf_O_mayuge vertices to drop < -4mm on 下=1.0 without accumulation, got {brow_down_dy}");
        let scl_changed = (0..3).any(|ax| (mouth_scl4[ax] - mouth_scl0[ax] * 1.2).abs() < 1e-3);
        assert!(
            scl_changed,
            "expected ExportBone cf_J_MouthBase_ty lateral scale 1.2x, got {mouth_scl0:?} -> {mouth_scl4:?}"
        );
        assert!(
            max_mouth_widen_dx > 0.001,
            "expected cf_O_face mouth vertices to widen outward along X for 口横広げ=1.0, got {max_mouth_widen_dx}"
        );

        // Frame 6 (weights back to 0.0): exact return to Frame 0 neutral (`< 1e-6` drift)
        assert!(!sess.mmdd_face_bone_dirty, "mmdd_face_bone_dirty must clear when bone morph weights return to 0");
        assert!(vlen(vsub(mayu6, mayu0)) < 1e-6 && vlen(vsub(mouth_scl6, mouth_scl0)) < 1e-6);
        let max_brow_residual = brow_v6
            .iter()
            .zip(brow_v0.iter())
            .map(|(a, b)| vlen(vsub(*a, *b)))
            .fold(0.0f32, f32::max);
        let max_face_residual = face_v6
            .iter()
            .zip(face_v0.iter())
            .map(|(a, b)| vlen(vsub(*a, *b)))
            .fold(0.0f32, f32::max);
        assert!(
            max_brow_residual < 1e-6 && max_face_residual < 1e-6,
            "expected zero residual head mesh drift after bone morph reset, got brow={max_brow_residual}, face={max_face_residual}"
        );
    }
}
