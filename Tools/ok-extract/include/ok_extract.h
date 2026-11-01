#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0 = success. err_buf is UTF-8, may be null. */
int32_t ok_extract_card(
	const char* game_root_utf8,
	const char* card_path_utf8,
	const char* out_dir_utf8,
	char* err_buf,
	int32_t err_cap);

int32_t ok_extract_card_outfit(
	const char* game_root_utf8,
	const char* card_path_utf8,
	const char* out_dir_utf8,
	int32_t outfit_index,
	char* err_buf,
	int32_t err_cap);

typedef struct OkSession OkSession;
/* Independent live role preserving the source editor's unsaved geometry. */
OkSession* ok_session_clone_interaction(const OkSession* source);
/* Shared authored definition; no APNG layout changes. Prepare is transactional.
   Hit forces are count xyz triplets in native Unity world coordinates. */
int32_t ok_body_reaction_prepare(OkSession*,const char* root,const uint8_t* json,int32_t len,char* error,int32_t error_cap);
int32_t ok_body_reaction_hit(OkSession*,int32_t id,const float* forces,int32_t count,float weight);
int32_t ok_body_reaction_hit_with_release(OkSession*,int32_t id,const float* forces,int32_t count,float weight,uint8_t release_mask);
int32_t ok_body_reaction_clear(OkSession*);
int32_t ok_body_reaction_is_playing(OkSession*);
int32_t ok_common_interaction_poses(const char* root,char* out,int32_t capacity);
int32_t ok_clothing_transition(const char* root,const char* card,int32_t outfit,const uint8_t* previous,int32_t kind,uint8_t* out,char* error,int32_t error_cap);
/* OK clothes (.okclothes). op 0 import coordinate card a; 1 export outfit of card a; 2 wear okclothes b as outfit of card a. Writes out; summary or error in msg. */
int32_t ok_clothes_file(int32_t op,const char* a,const char* b,int32_t outfit,const char* out,char* msg,int32_t msg_cap);
int32_t ok_okcard_make(const char* game, const char* card, const char* out, char* msg, int32_t cap);
/* Preserve coordinate numbering and record resources for the explicitly selected outfit. */
int32_t ok_okcard_make_outfit(const char* game, const char* card, const char* out, int32_t outfit, char* msg, int32_t cap);
/* Additive exact private-renderer palette. Matrices are row-major Unity
   world*bind transforms, including live sockets/implants/destroyed slots.
   Query with matrices=null, capacity=0 returns required matrix count.
   A successful copy returns the same count; errors return -1 and touch no
   output. Evaluate a native frame before querying dynamic renderers; an
   uninitialized simulation is rejected. View pointers are borrowed until
   session mutation/close. */
typedef struct OkSkinPaletteView {
 const float* positions;
 const float* normals;
 const float* tangents;
 const float* weights;
 const uint16_t* bone_idx;
 int32_t n_verts;
} OkSkinPaletteView;
int32_t ok_part_skin_palette(OkSession* s,int32_t part,OkSkinPaletteView* out,float* matrices,int32_t capacity);
/* Private GPU streams: skin/normalize first, then post16 placement/rot_normal.
 * Last matrix slot is identity fallback; authored bones are [0,count-1).
 * null matrices/capacity0 queries count; failed reads leave outputs untouched. */
int32_t ok_part_private_skin_stages(OkSession* s,int32_t part,OkSkinPaletteView* out,float* matrices,int32_t capacity,float* post16);
/* Source validation is cached per immutable rest geometry/palette layout.
 * Nonzero process-local revision is not serialized; clone/restore/replacement
 * or slot-count change invalidates it. Copy borrowed streams before session
 * mutation/free. Calls require a live session under the existing C ABI contract. */
uint64_t ok_part_private_skin_source_revision(OkSession* s,int32_t part);
int32_t ok_skin_display_except_private(OkSession* s,const int32_t* parts,const uint64_t* revisions,int32_t count);
/* Parts an owner draws on the GPU from their skin palettes (same validation; 1 = invalid, cleared): playback still
   solves their dynamics but no longer skins their vertices on the CPU. */
int32_t ok_session_set_scene_gpu_parts(OkSession* s,const int32_t* parts,const uint64_t* revisions,int32_t count);
int32_t ok_query_private_skin_geometry(OkSession* s,int32_t part);
int32_t ok_query_private_skin_geometry_batch(OkSession* s,const int32_t* parts,int32_t count);
int32_t ok_part_private_skin_stages_revision(OkSession* s,int32_t part,uint64_t expected_revision,OkSkinPaletteView* out,float* matrices,int32_t capacity,float* post16);
/* Read-only atomic batch, matrices concatenated in request order (counts*16).
   All counts/revisions must match; failure writes neither output buffer. */
int32_t ok_private_skin_stages_batch(const OkSession* s,const int32_t* parts,const uint64_t* revisions,const int32_t* counts,int32_t count,float* matrices,int32_t matrix_count,float* posts);
/* Exact native socket world matrix, row-major, Unity meters; error=-1. */
int32_t ok_set_head_look(OkSession* s,float x,float y,float z,int32_t enabled);
/* Studio FK: count NUL-terminated bone names back to back, 3 floats (Unity local Euler degrees) each; count 0 clears. */
int32_t ok_session_set_fk(OkSession* s,const char* names,const float* euler,int32_t count);
/* Local rotation (Unity Euler degrees) of a body bone in the current pose; -1 when absent. */
int32_t ok_session_bone_euler(OkSession* s,const char* name,float* out3);
/* Studio IK (FullBodyBipedIK): prepare from the shared resources root; targets are effector indexes 0..8 (body, L/R
   shoulder, L/R thigh, L/R hand, L/R foot) with skeleton-space positions (Unity metres); count 0 clears. */
int32_t ok_session_prepare_studio_ik(OkSession* s,const char* root,char* err,int32_t cap);
int32_t ok_session_set_studio_ik(OkSession* s,const int32_t* effectors,const float* positions,int32_t count);
int32_t ok_session_set_studio_ik_pose(OkSession* s,const int32_t* effectors,const float* positions,const float* rotations,const uint8_t* enabled,int32_t count);
/* Studio IK weights 0..1, one per target of the last set_studio_ik(_pose) call (in its order); count 0: all 1. */
int32_t ok_session_set_studio_ik_weights(OkSession* s,const float* weights,int32_t count);
int32_t ok_session_set_studio_fixed_neck(OkSession* s,const float* rotations,int32_t count);
/* SaveAngle order: QL(4), QR(4), H0,H1,V0,V1; accepts only source NO_LOOK patterns. */
int32_t ok_session_set_studio_fixed_eyes(OkSession* s,const float* values,int32_t count,int32_t pattern,float* out_rates);
int32_t ok_session_ik_effector_position(OkSession* s,int32_t effector,float* out3);
/* KK Studio pose files (UserData/Studio/pose/*.dat) as JSON: read 0 ok / 1 error (message in out); write from JSON. */
int32_t ok_studio_pose_read(const char* path,char* out,int32_t cap);
/* KStudio scene import step 1: scene as JSON in out (characters' rebuilt KKS cards written to out_dir); 0 ok / 1 error. */
int32_t ok_studio_scene_export(const char* scene,const char* out_dir,char* out,int32_t cap);
/* Reads the library's shader_states.json again on a background thread when it is missing, older or from other game files (once per library). */
void ok_studio_shader_states_ensure(const char* game,const char* lib);
/* Unity local Euler degrees turned about the bone's own local axis (0 X, 1 Y, 2 Z); result in out3. */
int32_t ok_unity_euler_rotate(const float* euler3,int32_t axis,float degrees,float* out3);
/* Timeline FK: Unity Euler degrees t (0..1) of the way from a3 to b3 along the shortest arc. */
int32_t ok_unity_euler_slerp(const float* a3,const float* b3,float t,float* out3);
int32_t ok_studio_pose_write(const char* path,const char* json,char* err,int32_t cap);
/* PhysX secondary dynamics for this character: 1 = host solver (gallery menu, one-to-one, paired playback), 0 = built-in (default). */
int32_t ok_session_set_physx_secondary(OkSession* s,int32_t enabled);
/* Whether an Unreal animation drives this body: 1 yes, 0 the session samples its game animation, -1 follow OK_UE_ANIM
   (process-wide; another actor loading may change it). */
int32_t ok_session_set_external_animation(OkSession* s,int32_t external);
int32_t ok_head_look_error(OkSession* s,float* out);
/* The end of his penis mesh now (the vertex farthest from cm_J_dan100_00), his skeleton space, Unity metres; out holds 3.
   J_dan109 draws in as he goes in, his penis does not. 1: no penis mesh. */
int32_t ok_penis_tip(OkSession* s,float* out);
/* How far tip (her skeleton space, Unity metres) is outside her skin in her current pose, unswollen: out[0] metres
   (negative inside), out[1] distance to the skin vertex that says so, out[2] how far target (the belly.bulge swell that
   moves that vertex most, written to target) moves it at weight 100. 1: no belly.bulge targets. */
int32_t ok_belly_bulge_past(OkSession* s,const float* tip,float* out,char* target,int32_t cap);
int32_t ok_static_interaction_frame(OkSession* s,float dt);
int32_t ok_apply_shape_live(OkSession* s,const float* body,int32_t n_body,const float* face,int32_t n_face);
int32_t ok_prepare_shape_live(OkSession* s,const float* body,int32_t n_body,const float* face,int32_t n_face);
int32_t ok_bone_world_matrix(OkSession* s,const char* name,float* out16);
int32_t ok_part_skin_source_hash(OkSession* s,int32_t part,uint8_t* out32);
/* Versioned immutable rig/assignment contract; v1 remains unchanged. */
int32_t ok_part_skin_source_hash_v2(OkSession* s,int32_t part,uint8_t* out32);
int32_t ok_part_raw_morph(OkSession* s,int32_t part,int32_t morph,const char** name,uint32_t* indices,float* deltas,int32_t capacity);
typedef struct OkTextureSettings {
    int32_t filter, wrap_u, wrap_v, srgb, mip_policy;
} OkTextureSettings;
/* Runtime texture creation metadata. 0 success; absent/static property = 1.
   Unity filter 0 Point / 1 Bilinear / 2 Trilinear, wrap 0 Repeat / 1 Clamp.
   mip_policy 1 = encoded-byte RenderTexture mips. Existing view/table ABI unchanged. */
int32_t ok_part_texture_settings(OkSession* s, int32_t part, const char* property, OkTextureSettings* out);
int32_t ok_part_outline_enabled(OkSession* s, int32_t part);
/* Additive APIs; old OkMeshView and binding table layout unchanged.
   value writes four gamma-space components; absent/invalid = 1, untouched.
   PNG pointer borrowed until session close/reload. */
/* Keyword returns 1 enabled, 0 disabled, -1 absent/invalid material. */
/* Complete source metadata JSON; query null/0 then copy, -1 invalid/capacity. */
int32_t ok_part_material_metadata(OkSession* s,int32_t part,uint8_t* out,int32_t cap);
// Original expression varying: count query with null out; capacity is vertices.
int32_t ok_part_eye_expression_uvs(OkSession* s,int32_t part,float* out,int32_t capacity);
int32_t ok_part_material_keyword(OkSession* s, int32_t part, const char* keyword);
int32_t ok_part_material_value(OkSession* s, int32_t part, const char* property, float* out4);
int32_t ok_part_shader_texture(OkSession* s, int32_t part, const char* property, const uint8_t** out, int32_t* len);

typedef struct OkMeshView {
	const char* role;
	const char* name;
	const float* positions;
	int32_t n_verts;
	const float* normals;
	const uint32_t* indices;
	int32_t n_idx;
	const float* uvs;
	const float* weights;
	const uint16_t* bone_idx;
	const uint8_t* png;
	int32_t png_len;
	const uint8_t* alpha_mask;
	int32_t alpha_mask_len;
	const uint8_t* hair_gloss;
	int32_t hair_gloss_len;
	const uint8_t* detail_mask;
	int32_t detail_mask_len;
	const uint8_t* line_mask;
	int32_t line_mask_len;
	const uint8_t* normal_map;
	int32_t normal_map_len;
	const uint8_t* normal_mask;
	int32_t normal_mask_len;
	const uint8_t* ramp_png;
	int32_t ramp_png_len;
	float shadow_depth;
	float line_width;
	float line_depth;
	float kano_ver_shift;
	const char* blend;
	int32_t unlit;
	const float* uv2;
	const float* uv4;
	const uint8_t* overtex_png;
	int32_t overtex_png_len;
	float overcolor[4];
	float shadow_color[4];
	float line_color[4];
	float shadow_extend;
	float shadow_extend_another;
	float main_tex_st[4];
	float alpha_mask_st[4];
	float normal_map_st[4];
	float normal_mask_st[4];
	float detail_mask_st[4];
	float line_mask_st[4];
	float normal_detail_scale;
	float face_normal;
	float face_shadow;
	float line_tex_on;
	float hair_gloss_st[4];
	float spec_height;
	float specular_power;
	float specular_power_nail;
	float specular_color[4];
	float no_tex_specular;
	float metallic;
	float smoothness;
	int32_t shader_kind;
	const float* rest_positions;
	const char* parent_bone;
	const char* rigid_follow_bone;
	int32_t skel_kind;
	const float* rest_tangents;
	const float* reference_normals;
	const float* uv3;
	const uint8_t* vertex_colors;
	const uint8_t* overtex2_png;
	int32_t overtex2_png_len;
	float overcolor2[4];
	float nip_size;
	float nip_specular;
	float nip_toggle;
	float tex1_mask;
	float texcoord2_st[4];
	float texcoord3_st[4];
	const uint8_t* normal_detail;
	int32_t normal_detail_len;
	float normal_detail_st[4];
	int32_t render_visible; /* Source GameObject visibility, FBS data retained. */
	int32_t item_draw; /* 0=none, 3=Forge main_item, 4=Koikano main_clothes_item. */
	float item_colors[4][4];
	const uint8_t* color_mask;
	int32_t color_mask_len;
	float color_mask_st[4];
	float skin_line_width; /* Koikano/main_skin _LineWidthS, not global _linewidthG. */
	int32_t eye_draw;
	const uint8_t* eye_textures[2];
	int32_t eye_texture_lens[2];
	float eye_colors[2][4];
	float eye_rotations[3];
	float eye_color[4];
	float eye_highlight;
	const float* eye_sample_uvs;
	int32_t draw_layer; /* Draw order among parts drawing the same surface (0 first); see assign_draw_layers. */
} OkMeshView;

typedef struct OkBoneView {
	const char* name;
	int32_t parent;
	float rest_pos[3];
	float rest_rot[4];
	float rest_scl[3];
	float pose_pos[3];
	float pose_rot[4];
	float pose_scl[3];
	int32_t layer;
} OkBoneView;

typedef struct OkMorphView {
	const char* name;
	const uint32_t* indices;
	const float* deltas;
	int32_t n;
} OkMorphView;

typedef struct OkUnrealBindings {
	void (*log)(int32_t level, const char* msg_utf8);
} OkUnrealBindings;

/* Unity local meters/TRS at Animator output, before helpers, ABMX and physics. */
typedef struct OkBodyPose {
	int32_t bone;
	float position[3];
	float rotation[4];
	float scale[3];
} OkBodyPose;

typedef struct OkRustBindings {
	int32_t (*session_open)(
		const char* game_root_utf8,
		const char* card_path_utf8,
		int32_t outfit,
		OkSession** out,
		char* err_buf,
		int32_t err_cap);
	void (*session_free)(OkSession* s);
	int32_t (*part_count)(OkSession* s);
	int32_t (*part_at)(OkSession* s, int32_t i, OkMeshView* out);
	int32_t (*apply_shape)(
		OkSession* s,
		const float* body,
		int32_t n_body,
		const float* face,
		int32_t n_face);
	int32_t (*skin)(OkSession* s);
	int32_t (*shape_body)(OkSession* s, float* out, int32_t cap);
	int32_t (*shape_face)(OkSession* s, float* out, int32_t cap);
	int32_t (*coord_count)(OkSession* s);
	int32_t (*skel_bone_count)(OkSession* s);
	int32_t (*skel_bone_at)(OkSession* s, int32_t i, OkBoneView* out);
	int32_t (*morph_count)(OkSession* s, int32_t part);
	int32_t (*morph_at)(OkSession* s, int32_t part, int32_t morph, OkMorphView* out);
	int32_t (*mouth_pair)(OkSession* s, int32_t part, int32_t pattern, int32_t* close, int32_t* open);
	int32_t (*mouth_pattern_count)(OkSession* s, int32_t part);
	int32_t (*apply_anim_time)(OkSession* s, float t);
	float (*anim_duration)(OkSession* s);
	int32_t (*clip_count)(OkSession* s);
	int32_t (*clip_name)(OkSession* s, int32_t index, char* out, int32_t cap);
	int32_t (*set_anim_clip)(OkSession* s, const char* name_utf8);
	int32_t (*clip_source_count)(OkSession* s);
	int32_t (*clip_source_name)(OkSession* s, int32_t index, char* out, int32_t cap);
	int32_t (*clip_count_for_source)(OkSession* s, const char* source_utf8);
	/* out=NULL, cap=0 queries required UTF-8 bytes including NUL; otherwise 0=success, 1=error. */
	int32_t (*clip_name_for_source)(OkSession* s, const char* source_utf8, int32_t index, char* out, int32_t cap);
	/* 0=source default, 1=Maker, 2=normal, 3=H/esthetic, 4=Studio (ExpressionBone). */
	int32_t (*set_anim_scene_mode)(OkSession* s, int32_t mode);
	/* Sample time may wrap; dt is the actual elapsed frame time, not its difference. */
	int32_t (*apply_anim_frame)(OkSession* s, float t, float dt);
	/* Explicit controller states hold their last frame when this returns zero. */
	int32_t (*anim_looping)(OkSession* s);
	int32_t (*hand_shape_count)(OkSession* s);
	/* KKS ShapeHandInfo: side 0=left/1=right; late rotation override, t clamped. */
	int32_t (*set_hand_shape)(OkSession* s, int32_t side, int32_t enabled, int32_t a, int32_t b, float blend);
	/* Controller 0=Mouth, 1=Eyes, 2=Eyebrow; source target-specific indices. */
	int32_t (*face_pattern_count)(OkSession* s, int32_t part, int32_t control);
	int32_t (*face_pair)(OkSession* s, int32_t part, int32_t control, int32_t pattern, int32_t* close, int32_t* open);
	float (*face_baseline_weight)(OkSession* s, int32_t part, int32_t morph);
	float (*face_default_rate)(OkSession* s, int32_t control);
	float (*face_runtime_setting)(OkSession* s, int32_t setting);
	float (*eye_material_setting)(OkSession* s, int32_t part, int32_t setting);
	int32_t (*set_eye_gaze)(OkSession* s, float left_h, float right_h, float vertical);
	int32_t (*eye_look_pattern_count)(OkSession* s);
	/* Target uses Unity world meters in the actor's local preview space; rates has 3 floats. */
	int32_t (*step_eye_look)(OkSession* s, float x, float y, float z, int32_t pattern, float dt, int32_t enabled, float* rates);
	int32_t (*external_body_pose_at)(OkSession* s, int32_t bone, OkBodyPose* out);
	/* Partial external body pose; missing joints use card-shaped baseline. Invalid frame is atomic. */
	int32_t (*apply_external_body_frame)(OkSession* s, float t, float dt, const OkBodyPose* poses, int32_t count);
	int32_t (*anim_float_count)(OkSession* s);
	/* ranges receives current value, lowest threshold and highest threshold. */
	int32_t (*anim_float_at)(OkSession* s, int32_t index, char* name, int32_t cap, float* ranges);
	int32_t (*set_anim_float)(OkSession* s, const char* name, float value);
	/* Atomic Studio source graph selection. H options are saved Studio values. */
	int32_t (*set_studio_animation)(OkSession* s, const char* key, int32_t is_h, float height, float breast, int32_t is_motion, float motion);
	int32_t (*set_studio_animation_profile)(OkSession* s, const char* key, int32_t is_h, float height, float breast, int32_t is_motion, float motion, const char* yure_source, const char* yure_asset);
	/* 0..3 dynamic enable, 4..12 left shape, 13..21 right shape; -1 invalid. */
	int32_t (*studio_yure_flag)(OkSession* s, int32_t index);
	int32_t (*set_studio_animation_pv_profile)(OkSession* s, const char* key, int32_t is_h, float height, float breast, int32_t is_motion, float motion, const char* yure_source, const char* yure_asset, int32_t pv_mask);
	/* Four serialized slots. FK only controls PV eligibility, not an FK solver. */
	int32_t (*set_studio_pv)(OkSession* s, int32_t mask, int32_t fk);
	int32_t (*studio_pv_flag)(OkSession* s, int32_t index);
	/* Exported shared palette index for the serialized slot; source 0=destination/1=PV. */
	int32_t (*studio_pv_bone)(OkSession* s, int32_t index, int32_t source);
} OkRustBindings;

int32_t ok_register_bindings(OkUnrealBindings unreal, OkRustBindings* out_rust);
/* Immediate source-authored trigger self re-entry; existing binding table unchanged. */
int32_t ok_animation_trigger_count(OkSession* s);
int32_t ok_animation_trigger_at(OkSession* s, int32_t index, char* name, int32_t cap);
int32_t ok_set_animation_trigger(OkSession* s, const char* name);
int32_t ok_animation_crossfade(OkSession* s, const char* name, float normalized_duration, float normalized_offset);
float ok_animation_elapsed(OkSession* s);
int32_t ok_animation_in_transition(OkSession* s);
/* Base/single child length; returns 0 when no transition or multiple children are active. */
float ok_animation_transition_source_length(OkSession* s);
/* Layer 0 is base. Invalid/inactive indices return 0. Kept outside OkRustBindings. */
int32_t ok_animation_layer_in_transition(OkSession* s, int32_t layer);
float ok_animation_layer_transition_source_length(OkSession* s, int32_t layer);
/* Same-base multi-layer descriptor; changes only the selected independent layer.
   Other child transitions continue. An already transitioning selected layer rejects. */
int32_t ok_animation_crossfade_layer(OkSession* s, int32_t layer, const char* name, float normalized_duration, float normalized_offset);
/* Installed native pairing inventory. Null out/cap0 queries UTF-8 capacity. */
int32_t ok_animation_pair_clip(OkSession* session,const char* key,int32_t layer,char* out,int32_t cap);
int32_t ok_animation_pairs(OkSession* s, char* out, int32_t cap);
int32_t ok_common_animation_pairs(const char* root,char* out,int32_t cap);
/* Builds the per-process memos card opens share for a game folder / OK base library (background, at reader start). 0 = ok. */
int32_t ok_prewarm(const char* root);
/* 1: this thread's opens are background work that yields to foreground opens (fails at a stage boundary while one runs). */
void ok_set_thread_background(int32_t on);
/* Whether opening this OK card (outfit, 9 clothes states or null = all worn) is in the open cache: 1 yes, 0 no, -1 not an OK library card. */
int32_t ok_session_cache_ready(const char* card, int32_t outfit, const uint8_t* clothes);
/* Between scene loads, with no open/prepare job in progress: drop decoded source caches on all workers.
 * Existing sessions keep their owned data. Does not delete disk caches or change library configuration. */
void ok_release_source_caches(void);
int32_t ok_common_animation_pair_clip(const char* root,const char* key,int32_t layer,char* out,int32_t cap);
int32_t ok_select_common_animation(OkSession* s,const char* root,const char* key);
/* Two distinct handles, one shared elapsed clock; returns 0 on success. */
/* Optional profiling only. Register once; callback must outlive the module.
   event identifies a fixed native scope; begin is 1, end is 0. */
int32_t ok_set_cpu_trace_callback(void (*callback)(uint32_t event,int32_t begin));
int32_t ok_animation_pair_frame(OkSession* female, OkSession* male, float time, float dt,
    const char* state, const char* female_motion, const char* male_motion, char* err, int32_t cap);
// 2 = ownership rejected before any solver advancement; 1 = frame failure.
int32_t ok_animation_pair_frame_gpu(OkSession* female,OkSession* male,float time,float dt,
    const char* state,const char* female_motion,const char* male_motion,
    const int32_t* female_parts,const uint64_t* female_revisions,int32_t female_count,
    const int32_t* male_parts,const uint64_t* male_revisions,int32_t male_count,char* err,int32_t cap);
// Callback is borrowed until this blocking call returns; disjoint private parts,
// immutable native arrays, no reentry or retained context on cloneable sessions.
typedef void (*OkPrivateSkinCallback)(void*,int32_t,int32_t,int32_t,const float*,const float*,const float*);
int32_t ok_animation_pair_frame_observed(OkSession* female, OkSession* male,float time,float dt,
    const char* state,const char* female_motion,const char* male_motion,OkPrivateSkinCallback callback,void* context,char* err,int32_t cap);

/* Shared curve/snapshot resources. Null out/cap0 queries byte count. */
int32_t ok_export_shared_clip(const char* root,const char* key,uint8_t* out,int32_t cap);
float ok_shared_clip_duration(const uint8_t* bytes,int32_t len);
/* Same-thread, nestable background preparation scope; releases temporary files on end. */
void ok_begin_animation_preparation(void);
void ok_end_animation_preparation(void);
int32_t ok_session_animation_snapshot(OkSession* s,uint8_t* out,int32_t cap);
OkSession* ok_session_open_animation_snapshot(const uint8_t* bytes,int32_t len);
/* Explicit thread-scoped clothing request; never mutates process environment. */
int32_t ok_session_open_clothes(const char* game,const char* card,int32_t outfit,const uint8_t states[9],OkSession** out,char* err,int32_t cap);
int32_t ok_session_export_shared_clip(OkSession* s,const char* key,uint8_t* out,int32_t cap);
/* Offline deterministic initialization; live install retains private physics. */
int32_t ok_session_prepare_shared_clip(OkSession* s,const uint8_t* bytes,int32_t len);
// GPU rig (gpu_rig/export.rs): the session's body skeleton and palette factorization ("OKGR"), and a shared clip as GPU
// channels ("OKGC"). Size-then-copy: out null and cap 0 answer the size. -1: failed.
int32_t ok_session_gpu_rig(OkSession* s,const uint8_t* clip,int32_t len,uint8_t* out,int32_t cap);
int32_t ok_session_gpu_clip(OkSession* s,const uint8_t* clip,int32_t len,uint8_t* out,int32_t cap);
// The installed clip's pose at t without the frame's dynamics (a native-only pose bank). 0: done.
int32_t ok_session_sample_frame(OkSession* s,float t);
// How fast her walk clip carries her, m/s (gpu_rig/walk.rs: the planted foot in her root space, the mean over the clip);
// < 0: unknown. Leaves the clip installed and posed at its start.
float ok_session_walk_speed(OkSession* s,const uint8_t* clip,int32_t len);
// Her walk through the clip, one frame per 1/60 s from its start, three floats a frame into out (at most cap floats):
// speed m/s, the planted foot's ball x and z (root space, metres). The frame count, or -1. Leaves the clip posed at its start.
int32_t ok_session_walk_curve(OkSession* s,const uint8_t* clip,int32_t len,float* out,int32_t cap);
int32_t ok_session_apply_contact_frame(OkSession* s,float t,float dt,const uint8_t* json,int32_t len);
int32_t ok_session_install_shared_clip(OkSession* s,const uint8_t* bytes,int32_t len);
/* Game sessions: play OKCLIP01 bytes keeping the session's game root. */
int32_t ok_session_install_video_clip(OkSession* s,const uint8_t* bytes,int32_t len);
/* Immutable catalogue preparation, safe without a live session on a worker. */
int32_t ok_session_animation_names(OkSession* s,const char* source,int32_t sources,uint8_t* out,int32_t cap);
int32_t ok_animation_source_labels(const char* root,const char* source,uint8_t* out,int32_t cap);
int32_t ok_prepare_studio_animation(const char* root,const char* key,const char* source,const char* owner);
int32_t ok_prepare_animation_source(const char* root,const char* source);
int32_t ok_prepare_animation_clip(const char* root,const char* key);
int32_t ok_prepare_animation_pair(const char* root,const char* state,const char* female_motion,const char* male_motion);
int32_t ok_animation_has_clip(OkSession* s,const char* key);
/* Copy validated prepared clothing variants when saving an embedded card. */
int32_t ok_copy_portable_clothing_states(const char* source,const char* output,char* err,int32_t cap);
/* Low-poly companion of an exported card (instant-meshes remesh, shared rig,
   binary glTF). Capture on the game thread; export may run on a worker and
   writes <card>.lowpoly.glb and <card>.lowpoly.json. budget<=0: default. */
void* ok_lowpoly_capture(OkSession* s,char* err,int32_t cap);
int32_t ok_lowpoly_export(const void* source,const char* card,int32_t budget,char* summary,int32_t summary_cap,char* err,int32_t cap);
void ok_lowpoly_free(void* source);
int32_t ok_editor_body_physics(OkSession* session,float softness,float weight);
int32_t ok_editor_decode_mods(const uint8_t* data,int32_t len,uint8_t* out,int32_t cap);
int32_t ok_editor_encode_mods(const uint8_t* data,int32_t len,const char* json,uint8_t* out,int32_t cap);
int32_t ok_editor_apply_mods(OkSession* session,const char* json,int32_t coordinate);
/* ok_editor_apply_mods, then the card's sliders (null/0 = the session's), building the shape once when possible. */
int32_t ok_editor_apply_mods_shape(OkSession* session,const char* json,int32_t coordinate,const float* body,int32_t n_body,const float* face,int32_t n_face);

/* PhysX DynamicBone back end (OKPhysXSecondary). Registers the chain solver for
   this module's copy of the library; null pointers restore the built-in solver.
   solve takes const OkPxChainStep* (see OKPhysXSecondary/OKPhysXSecondaryAbi.h). */
void ok_set_physx_dynamic_solver(int32_t (*solve)(const void* step), void (*release)(uint64_t key),
	void (*publish_body)(uint64_t group, const void* capsules, uint32_t count),
	int32_t (*ragdoll)(uint64_t group, void* bones, uint32_t count, float dt),
	float (*head_tilt)(void),
	int32_t (*solve_batch)(uint64_t batch, void* steps, uint32_t count),
	int32_t (*audit)(void));
/* Weight (1..10) and hardness (1..10) of hair/cloth/accessory chains for the built-in Verlet of the one-to-one
   characters, read through get on every step (OKPhysXSecondary.Weight / Hardness). Null: as authored. */
void ok_set_secondary_tuning_source(void (*get)(float* weight, float* hardness));


/* ---- OK animation (skeleton-agnostic animation, groups, blend spaces; src/okanim) ---- */
typedef struct OkAnimPlayer OkAnimPlayer;
int32_t ok_okanim_last_error(char* out, int32_t cap);
int32_t ok_okanim_from_clip(OkSession* s, const char* key, float fps, uint8_t* out, int32_t cap);
int32_t ok_okanim_from_vmd(const OkSession* s, const char* vmd_path, const char* params_json, uint8_t* out, int32_t cap);
int32_t ok_okanim_session_rest(OkSession* s, uint8_t* out, int32_t cap);
int32_t ok_okanim_quality(const uint8_t* bytes, int32_t len, const char* header, uint8_t* out, int32_t cap);
int32_t ok_okanim_set_posture(int32_t on);
int32_t ok_okanim_profile(int32_t reset, uint8_t* out, int32_t cap);
int32_t ok_okanim_set_parallel_finish(int32_t on);
int32_t ok_player_set_walk(OkAnimPlayer* p, const uint8_t* bytes, int32_t len);
int32_t ok_okanim_modify(const uint8_t* bytes, int32_t len, const char* op, const char* header, uint8_t* out, int32_t cap);
typedef struct OkAnimBatch OkAnimBatch;
OkAnimBatch* ok_okanim_batch_start(OkSession* s, const char* items_json, float fps, int32_t threads);
int32_t ok_okanim_batch_poll(const OkAnimBatch* b, int32_t from, uint8_t* out, int32_t cap);
void ok_okanim_batch_cancel(const OkAnimBatch* b);
void ok_okanim_batch_free(OkAnimBatch* b);
int32_t ok_session_dynamic_bones(OkSession* s, uint8_t* out, int32_t cap);
/* Pose recorder (OK Studio timeline -> .okanim): frame() records the session's current body pose; finish() returns the OKANIM bytes (out null / cap 0: size). */
typedef struct OkPoseRecorder OkPoseRecorder;
OkPoseRecorder* ok_okanim_record_start(OkSession* s);
int32_t ok_okanim_record_frame(OkPoseRecorder* r, OkSession* s);
int32_t ok_okanim_record_finish(OkPoseRecorder* r, float fps, int32_t looping, const char* name, uint8_t* out, int32_t cap);
void ok_okanim_record_free(OkPoseRecorder* r);
int32_t ok_okanim_from_locals(const char* header_json, const float* rotations, int32_t rotation_count, const float* hips, int32_t hips_count, uint8_t* out, int32_t cap);
int32_t ok_okanim_info(const uint8_t* bytes, int32_t len, uint8_t* out, int32_t cap);
int32_t ok_okanim_get_tracks(const uint8_t* bytes, int32_t len, float* out, int32_t cap);
int32_t ok_okanim_set_tracks(const uint8_t* bytes, int32_t len, const float* data, int32_t count, uint8_t* out, int32_t cap);
int32_t ok_okanim_target_rest(const char* target_json, float* out, int32_t cap);
int32_t ok_okanim_edit(const uint8_t* bytes, int32_t len, const char* patch_json, uint8_t* out, int32_t cap);
int32_t ok_okanim_sample_onto(const uint8_t* bytes, int32_t len, const char* target_json, float t, float* out, int32_t cap);
int32_t ok_okgroup_from_pair(OkSession* female, OkSession* male, const char* state, const char* female_motion, const char* male_motion, float fps, uint8_t* out, int32_t cap);
OkAnimPlayer* ok_player_create(const char* spec_json, const uint8_t* const* blobs, const int32_t* lengths, int32_t count);
void ok_player_free(OkAnimPlayer* p);
int32_t ok_player_set_param(OkAnimPlayer* p, int32_t index, float value);
float ok_player_duration(const OkAnimPlayer* p);
int32_t ok_player_roles(const OkAnimPlayer* p);
int32_t ok_player_sample_onto(const OkAnimPlayer* p, int32_t role, const char* target_json, float t, float* out, int32_t cap);
int32_t ok_player_root_drift(const OkAnimPlayer* p, int32_t role, float t, float* out);
int32_t ok_player_set_aliases(OkAnimPlayer* p, const char* json);
int32_t ok_player_curves(const OkAnimPlayer* p, int32_t role, float t, uint8_t* out, int32_t cap);
int32_t ok_player_apply(OkAnimPlayer* p, OkSession* const* sessions, int32_t count, float t, float dt, uint8_t* report, int32_t cap);

/* OK base library models (.okmodel): editor preview and edits.
   Text/PNG getters return the byte length (call again with a buffer that large); -1 on error. */
int32_t ok_okmodel_info(const char* library, const char* id_hex, uint8_t* out, int32_t cap);
int32_t ok_okmodel_open_session(const char* library, const char* id_hex, const char* prefab, int32_t include_disabled, OkSession** out, char* err, int32_t cap);
/* Studio item FK: fk_json = [[bone, [x, y, z]], ...] (local Unity Euler degrees) turned before the meshes are baked. */
/* OK Studio character state: status_json = {"tears_lv","hoho_aka_rate","nip_stand_rate","skin_tuya_rate","siru_lv":[5],"show_accessory":[21] (20 slots + one for every MoreAccessories slot)} (each optional); states nullable (clothes as ok_session_open_clothes). */
int32_t ok_session_open_status(const char* game_root, const char* card, int32_t outfit, const uint8_t* states, const char* status_json, OkSession** out, char* err, int32_t cap);
/* Studio item patterns (character list mt_pattern) of the OK base library: JSON [[id, name], ...]; a pattern's mask PNG (-1: none). out null / cap 0: size. */
int32_t ok_studio_patterns(const char* library, uint8_t* out, int32_t cap);
int32_t ok_studio_pattern_png(const char* library, int32_t id, uint8_t* out, int32_t cap);
/* PNG of texture `name` in game bundle `bundle` (e.g. a Studio filter LUT from studio_items.json "filters"); -1: none. */
int32_t ok_studio_texture_png(const char* library, const char* bundle, const char* name, uint8_t* out, int32_t cap);
/* Eye set of eyes pattern `ptn` (character list cha_eyeset) as JSON: eyes_ptn (blend shape pattern), eye_base, hitomi,
 * gag (1..3), textures, changes_parts. Byte count (copied when it fits `cap`); -1: none. */
int32_t ok_eye_set_json(const char* library, int32_t ptn, uint8_t* out, int32_t cap);
/* Prefab-space transform of a node (Unity position m, quaternion xyzw, scale) into out[10]; 0 found, 1 not. */
int32_t ok_okmodel_node_world(const char* library, const char* id, const char* prefab, const char* node, float* out);
int32_t ok_okmodel_open_session_fk(const char* library, const char* id_hex, const char* prefab, int32_t include_disabled, const char* fk_json, OkSession** out, char* err, int32_t cap);
/* Bones the prefab's skinned meshes follow (item FK): JSON array of names; returns the byte length (larger than cap: nothing written). */
int32_t ok_okmodel_fk_bones(const char* library, const char* id_hex, const char* prefab, uint8_t* out, int32_t cap);
/* Render-to-texture cameras of a prefab (a Studio camera item): JSON array of {key, target, width, height, pos[3],
 * rot[4] (prefab space, Unity m / xyzw), fov, near, far, orthographic, ortho_size, clear_flags, background[4]}; returns
 * the byte length (larger than cap: nothing written). */
int32_t ok_okmodel_render_cameras(const char* library, const char* id_hex, const char* prefab, uint8_t* out, int32_t cap);
/* Realtime lights of a prefab (a Studio map's lamps: not baked, not its SunLightInfo sun): JSON array of {node, name, kind
 * (0 spot, 1 directional, 2 point), pos[3], rot[4] (Unity prefab space), color[4], intensity, range, spot_angle, culling_mask,
 * shadows, lightmapping, times (time-of-day mask, 0: always)}; out null / cap 0: size. */
int32_t ok_okmodel_map_lights(const char* library, const char* id_hex, const char* prefab, uint8_t* out, int32_t cap);
int32_t ok_okmodel_edits_get(const char* library, const char* id_hex, uint8_t* out, int32_t cap);
int32_t ok_okmodel_edits_set(const char* library, const char* id_hex, const char* json, char* err, int32_t cap);
int32_t ok_okmodel_first_prefab(const char* library, const char* id_hex, char* out, int32_t cap);
int32_t ok_okmodel_save_as(const char* library, const char* id_hex, char* out, int32_t cap);
int32_t ok_okmodel_texture_png(const char* library, const char* id_hex, const char* name, uint8_t* out, int32_t cap);

/* 1 when the session reads the game installation, 0 when it is self-contained (portable pack, OK base library). */
int32_t ok_session_has_game_root(OkSession* s);
int32_t ok_session_set_blush(OkSession* s, float rate);
int32_t ok_session_set_iris_small(OkSession* s, float rate);

#ifdef __cplusplus
}
#endif
