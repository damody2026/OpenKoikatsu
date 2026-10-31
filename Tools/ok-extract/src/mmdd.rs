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

/// Default MMDD 2.0 character bone definition (`BepInEx/plugins/Console/Lib/mmdd/chara_default_2.0.ini`).
pub const DEFAULT_CHARA_INI: &str = r#"
[General]
Description=Default Char Bone 2.0 (TDA spec)
Type=chara
HeightBone=cf_n_height
AutoRotAdj=0
DefWeightSyncMode=global
AxisScale=0.0832

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
TOE_EX_R=FOOT_D_R:右足先EX:r
UPLEG_D_L=BACK_HIPS_C_L:左足D:rt:tr,1,UPLEG_L
LOWLEG_D_L=UPLEG_D_L:左ひざD:rt:tr,1,LOWLEG_L
FOOT_D_L=LOWLEG_D_L:左足首D:rt:tr,1,FOOT_L
TOE_EX_L=FOOT_D_L:左足先EX:r
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
TOE_EX_R=b:cf_j_toes_R
UPLEG_D_L=b:cf_j_thigh00_L
LOWLEG_D_L=b:cf_j_leg01_L
FOOT_D_L=b:cf_j_leg03_L
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
TOE_EX_R=cf_j_toes_R
UPLEG_D_L=cf_j_thigh00_L
LOWLEG_D_L=cf_j_leg01_L
FOOT_D_L=cf_j_leg03_L
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
        let mut axis_scale = 0.0832f32;
        if let Some(gen) = sections.get("general") {
            for (k, v) in gen {
                match k.as_str() {
                    "heightbone" => height_bone = v.clone(),
                    "autorotadj" => auto_rot_adj = v == "1" || v.eq_ignore_ascii_case("true"),
                    "defweightsyncmode" => {
                        default_sync_local = v.eq_ignore_ascii_case("local") || v.eq_ignore_ascii_case("l")
                    }
                    "axisscale" => axis_scale = v.parse().unwrap_or(0.0832),
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

fn load_standard_kk_skeleton_uncached() -> Option<Skeleton> {
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
            if let Some((&cf_s_head_id, _)) =
                body.tfs.iter().find(|(_, t)| t.name == "cf_s_head")
            {
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
        }
        let _ = CACHED_SKEL.set(Some(body.clone()));
        return Some(body);
    }
    None
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
    pub ik_enable: HashMap<String, bool>,
    pub custom_rot_adj: HashMap<String, V3>,
    pub custom_axis_adj: HashMap<String, V3>,
    pub custom_rot_scale: HashMap<String, f32>,
    pub custom_pos_offset: HashMap<String, V3>,
    pub custom_pos_scale: HashMap<String, f32>,
    pub modified_keyframes: Vec<(String, String, u32, Option<VmdBoneKeyFrame>)>,
}

impl Default for MotionSolveParams {
    fn default() -> Self {
        Self {
            char_scale: 1.0,
            playback_speed: 1.0,
            delay: 0.0,
            interpolate_consecutive: true,
            looping: false,
            ik_enable: HashMap::new(),
            custom_rot_adj: HashMap::new(),
            custom_axis_adj: HashMap::new(),
            custom_rot_scale: HashMap::new(),
            custom_pos_offset: HashMap::new(),
            custom_pos_scale: HashMap::new(),
            modified_keyframes: Vec::new(),
        }
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
    let rest = standard_kk_skeleton().ok_or("standard KK skeleton (studio/base/00.unity3d) unavailable")?;
    let mut prep_body = rest.clone();

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

    // 4. Apply custom rot/axis adjustments, then autoBoneAxisAdjust() and charScale
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
    // `prep_body` is `standard_kk_skeleton()` (unscaled `cf_n_height` = 1.0), and `.okanim`
    // normalizes hip translations by `hips_len` so runtime characters apply their own height scale.
    let global_scale = ini.axis_scale;
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
    let frames = (max_frame_no as usize + 1).max(2);

    // 6. Resort weight bones in `rest` tree depth order (parents before children), skipping `ROOT`
    // so `cf_j_root` stays at identity and `cf_j_hips` captures the full root+center+groove+waist+hips motion.
    let tracks = family_tracks(&rest);
    let hips_id = tracks
        .iter()
        .find(|(_, t)| t.bone == "cf_j_hips")
        .map(|(id, _)| *id)
        .ok_or("no cf_j_hips")?;
    let hips_rest = rest.tfs[&hips_id].pos;
    let hips_len = vlen(hips_rest).max(1e-4);
    let hips_parent_rest_world = rest.tfs[&hips_id]
        .parent
        .map(|p| crate::pose::world_rotation_of(p, &rest.tfs))
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

    let mut rot_out = Vec::with_capacity(frames * tracks.len());
    let mut hips_out = Vec::with_capacity(frames);
    let mut last_rot: Vec<Option<Quat>> = vec![None; tracks.len()];
    let mut work_body = rest.clone();

    // 7. Frame-by-frame evaluation
    for f in 0..frames {
        let frame_no = f as f32;
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
        work_body.restore_from(&rest);
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
        let hips_pos = work_body
            .tfs
            .get(&hips_id)
            .map(|t| t.pos)
            .unwrap_or(hips_rest);
        hips_out.push([
            hips_pos[0] / hips_len,
            hips_pos[1] / hips_len,
            hips_pos[2] / hips_len,
        ]);
    }

    let fps = (30.0 * params.playback_speed).max(1.0);
    let curves = evaluate_vmd_morph_curves(&vmd.morph_keyframes, frames);
    let mut meta = BTreeMap::new();
    meta.insert("source".into(), "mmdd_vmd".into());
    meta.insert("name".into(), source_name.to_string());
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

fn evaluate_vmd_morph_curves(
    morph_kfs: &[VmdMorphKeyFrame],
    frames: usize,
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

    // Morph definitions from chara_default_1.3.ini: (vmd_name, pattern, range_start, range_end)
    const MOUTH_DEFS: &[(&str, i32, f32, f32)] = &[
        ("あ", 26, 0.0, 1.0),
        ("い", 28, 0.0, 1.0),
        ("う", 30, 0.0, 1.0),
        ("え", 31, 0.0, 1.0),
        ("お", 33, 0.0, 1.0),
        ("わ", 2, 0.0, 1.0),
        ("ワ", 2, 0.0, 1.0),
        ("ん", 36, 0.0, 1.0),
        ("あ２", 1, 0.0, 1.0),
        ("にやり", 2, 0.0, 0.0),
        ("にやり２", 2, 0.0, 0.0),
        ("口角上げ", 2, 0.0, 0.0),
        ("にっこり", 19, 0.0, 0.0),
        ("口角下げ", 8, 0.0, 0.0),
        ("ぺろっ", 20, 0.0, 1.0),
        ("てへぺろ", 20, 0.0, 1.0),
        ("てへぺろ２", 20, 0.0, 1.0),
        ("∧", 7, 0.0, 0.0),
        ("▲", 38, 0.0, 1.0),
        ("ω", 37, 0.0, 0.0),
        ("ω□", 37, 1.0, 1.0),
    ];
    const EYE_DEFS: &[(&str, i32, f32, f32)] = &[
        ("まばたき", 0, 1.0, 0.0),
        ("笑い", 3, 1.0, 1.0),
        ("笑い目", 4, 1.0, 1.0),
        ("ウィンク", 6, 1.0, 1.0),
        ("ウィンク２", 6, 1.0, 1.0),
        ("ウィンク右", 5, 1.0, 1.0),
        ("ｳｨﾝｸ２右", 5, 1.0, 1.0),
        ("びっくり", 17, 1.0, 1.0),
        ("じと目", 10, 1.0, 1.0),
        ("ｷﾘｯ", 9, 1.0, 1.0),
        ("なごみ", 31, 1.0, 1.0),
        ("はぅ", 30, 1.0, 1.0),
        ("はちゅ目", 22, 1.0, 1.0),
        ("星目", 24, 1.0, 1.0),
        ("はぁと", 25, 1.0, 1.0),
        ("ハート", 25, 1.0, 1.0),
    ];
    const BROW_DEFS: &[(&str, i32)] = &[
        ("困る", 13),
        ("怒り", 1),
        ("にこり", 12),
        ("真面目", 0),
        ("上", 0),
        ("下", 0),
    ];

    let mut mouth_ptn = Vec::with_capacity(frames);
    let mut mouth_open = Vec::with_capacity(frames);
    let mut eye_ptn = Vec::with_capacity(frames);
    let mut eye_open = Vec::with_capacity(frames);
    let mut brow_ptn = Vec::with_capacity(frames);

    for f in 0..frames {
        let fno = f as f32;

        // 1. Mouth: pick active morph with largest weight > 0.02
        let mut best_mouth: Option<(f32, i32, f32)> = None;
        for &(mname, ptn, r0, r1) in MOUTH_DEFS {
            if let Some(kfs) = by_name.get(mname) {
                let w = sample_vmd_morph_kfs(kfs, fno).clamp(0.0, 1.0);
                if w > 0.02 && best_mouth.map_or(true, |(bw, _, _)| w > bw) {
                    let open = (r0 + (r1 - r0) * w).clamp(0.0, 1.0);
                    best_mouth = Some((w, ptn, open));
                }
            }
        }
        if let Some((_, ptn, open)) = best_mouth {
            mouth_ptn.push(ptn as f32);
            mouth_open.push(open);
        } else {
            mouth_ptn.push(0.0);
            mouth_open.push(0.0);
        }

        // 2. Eye: pick active morph with largest weight > 0.02
        let mut best_eye: Option<(f32, i32, f32)> = None;
        for &(ename, ptn, r0, r1) in EYE_DEFS {
            if let Some(kfs) = by_name.get(ename) {
                let w = sample_vmd_morph_kfs(kfs, fno).clamp(0.0, 1.0);
                if w > 0.02 && best_eye.map_or(true, |(bw, _, _)| w > bw) {
                    let open = (r0 + (r1 - r0) * w).clamp(0.0, 1.0);
                    best_eye = Some((w, ptn, open));
                }
            }
        }
        if let Some((_, ptn, open)) = best_eye {
            eye_ptn.push(ptn as f32);
            eye_open.push(open);
        } else {
            eye_ptn.push(0.0);
            eye_open.push(1.0);
        }

        // 3. Eyebrow: pick active morph with largest weight > 0.05
        let mut best_brow: Option<(f32, i32)> = None;
        for &(bname, ptn) in BROW_DEFS {
            if let Some(kfs) = by_name.get(bname) {
                let w = sample_vmd_morph_kfs(kfs, fno).clamp(0.0, 1.0);
                if w > 0.05 && best_brow.map_or(true, |(bw, _)| w > bw) {
                    best_brow = Some((w, ptn));
                }
            }
        }
        brow_ptn.push(best_brow.map_or(0.0, |(_, ptn)| ptn as f32));
    }

    vec![
        crate::okanim::Curve {
            name: "face.mouth_pattern".into(),
            values: mouth_ptn,
        },
        crate::okanim::Curve {
            name: "face.mouth_open".into(),
            values: mouth_open,
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
            name: "face.brow_pattern".into(),
            values: brow_ptn,
        },
    ]
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
            let mut params = MotionSolveParams {
                char_scale: smap
                    .get("charScale")
                    .map(|v| val_to_f32(v, 1.0))
                    .unwrap_or(1.0),
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
                    if let Some(v) = ao
                        .get("posadj")
                        .or_else(|| ao.get("posoffset"))
                        .and_then(val_to_v3)
                    {
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
                    if let Ok(anim) = solve_vmd_to_okanim(&parsed_vmd, &ini, &params, stem) {
                        frames = anim.frames;
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
                char_scale: params.char_scale,
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
                .map(|v| val_to_f32(v, 0.08))
                .unwrap_or(0.08);
            let dst_scale = smap
                .get("dstScale")
                .map(|v| val_to_f32(v, 0.08))
                .unwrap_or(0.08);
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

            let mut params = MotionSolveParams {
                char_scale: scale_model / 0.76,
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
                        if let Ok(anim) = solve_vmd_to_okanim(&parsed_vmd, &ini, &params, stem) {
                            frames = anim.frames;
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
        assert_eq!(ini.bone_names.len(), 88);
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
}
