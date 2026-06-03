#ifndef AC_BOAT_H
#define AC_BOAT_H

#include "types.h"
#include "m_actor.h"
#include "c_keyframe.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct boat_s BOAT_ACTOR;

typedef void (*aBT_PROC)(BOAT_ACTOR*, GAME_PLAY*);

struct boat_s {
  ACTOR actor_class;
  int _174;
  cKF_SkeletonInfo_R_c keyframe;
  int _1E8;
  s_xyz work[15];
  s_xyz morph[15];
  aBT_PROC action_proc;
  f32 roll_timer_accum;
  f32 roll_cycle_accum;
  f32 rudder_angle_accum;
  f32 yaw_angle_accum;
  int action;
  int roll_cycle;
  int roll_timer;
  int point;
  int direction;
  f32 _2C8;
  f32 _2CC;
  f32 _2D0;
  f32 rudder;
  f32 draw_up_angle_accum;
};

extern ACTOR_PROFILE Boat_Profile;

#ifdef __cplusplus
}
#endif

#endif

