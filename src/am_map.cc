//----------------------------------------------------------------------------
//  EDGE Automap Functions
//----------------------------------------------------------------------------
// 
//  Copyright (c) 1999-2001  The EDGE Team.
// 
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//----------------------------------------------------------------------------
//
//  Based on the DOOM source code, released by Id Software under the
//  following copyright:
//
//    Copyright (C) 1993-1996 by id Software, Inc.
//
//----------------------------------------------------------------------------

#include "i_defs.h"
#include "am_map.h"

#include "con_cvar.h"
#include "con_main.h"
#include "dm_defs.h"
#include "dm_state.h"
#include "dstrings.h"
#include "m_argv.h"
#include "m_bbox.h"
#include "m_cheat.h"
#include "m_misc.h"
#include "p_local.h"
#include "r_state.h"
#include "rgl_defs.h"
#include "st_stuff.h"
#include "v_ctx.h"
#include "v_res.h"
#include "v_colour.h"
#include "w_image.h"
#include "w_wad.h"
#include "z_zone.h"

#define DEBUG_TRUEBSP  0

// Automap colours

#define BACK_COL    (BLACK)
#define YOUR_COL    (WHITE)
#define WALL_COL    (RED)
#define FLOOR_COL   (BROWN + BROWN_LEN/2)
#define CEIL_COL    (YELLOW)
#define REGION_COL  (BEIGE)
#define TELE_COL    (RED + RED_LEN/2)
#define SECRET_COL  (CYAN + CYAN_LEN/4)
#define GRID_COL    (GRAY + GRAY_LEN*3/4)
#define XHAIR_COL   (GRAY + GRAY_LEN/3)
#define ALLMAP_COL  (GRAY + GRAY_LEN/2)
#define MINI_COL    (BLUE + BLUE_LEN/2)

#define THING_COL   (BROWN + BROWN_LEN*2/3)
#define MONST_COL   (GREEN + 2)
#define DEAD_COL    (RED + 2)
#define MISSL_COL   (ORANGE)
#define ITEM_COL    (BLUE+1)

// Automap keys
// Ideally these would be configurable...

#define AM_PANDOWNKEY KEYD_DOWNARROW
#define AM_PANUPKEY   KEYD_UPARROW
#define AM_PANRIGHTKEY    KEYD_RIGHTARROW
#define AM_PANLEFTKEY     KEYD_LEFTARROW
#define AM_ZOOMINKEY  '='
#define AM_ZOOMOUTKEY '-'
#define AM_STARTKEY   key_map
#define AM_ENDKEY     key_map
#define AM_GOBIGKEY   '0'
#define AM_FOLLOWKEY  'f'
#define AM_GRIDKEY    'g'
#define AM_MARKKEY    'm'
#define AM_CLEARMARKKEY    'c'

#define AM_NUMMARKPOINTS 10

#define PLAYERRADIUS 16.0

//
// NOTE:
//   `F' in the names here means `Framebuffer', i.e. on-screen coords.
//   `M' in the names means `Map', i.e. coordinates in the level.
//

// scale on entry
#define INITSCALEMTOF (0.2)
// how much the automap moves window per tic in frame-buffer coordinates
// moves 140 pixels in 1 second
#define F_PANINC 4
// how much zoom-in per tic
// goes to 2x in 1 second
#define M_ZOOMIN (1.02)
// how much zoom-out per tic
// pulls out to 0.5x in 1 second
#define M_ZOOMOUT (1/M_ZOOMIN)

// translates between frame-buffer and map distances - moved down with rest of functions
#define FTOM(x) ((flo_t)((x) * scale_ftom))
#define MTOF(x) ((int)((x) * scale_mtof))

// translates between frame-buffer and map coordinates
#define CXMTOF(x)  (f_x + MTOF((x) - m_x))
#define CYMTOF(y)  (f_y + f_h - MTOF((y) - m_y))

#define CXFTOM(x)  (m_x + FTOM((x) - f_x))
#define CYFTOM(y)  (m_y + FTOM(f_y + f_h - (y)))

//
// The vector graphics for the automap.
//
// A line drawing of the player pointing right, starting from the
// middle.

static mline_t player_arrow[] =
{
  {{-0.875, 0}, {1.0, 0}},   // -----
    
  {{1.0, 0}, {0.5,  0.25}},  // ----->
  {{1.0, 0}, {0.5, -0.25}},
     
  {{-0.875, 0}, {-1.125,  0.25}},  // >---->
  {{-0.875, 0}, {-1.125, -0.25}},
     
  {{-0.625, 0}, {-0.875,  0.25}},  // >>--->
  {{-0.625, 0}, {-0.875, -0.25}}
};

#define NUMPLYRLINES (sizeof(player_arrow)/sizeof(mline_t))

static mline_t cheat_player_arrow[] =
{
  {{-0.875, 0}, {1.0, 0}},    // -----
  
  {{1.0, 0}, {0.5,  0.167}},  // ----->
  {{1.0, 0}, {0.5, -0.167}},
  
  {{-0.875, 0}, {-1.125,  0.167}},  // >----->
  {{-0.875, 0}, {-1.125, -0.167}},
  
  {{-0.625, 0}, {-0.875,  0.167}},  // >>----->
  {{-0.625, 0}, {-0.875, -0.167}},
  
  {{-0.5, 0}, {-0.5, -0.167}},      // >>-d--->
  {{-0.5, -0.167}, {-0.5 + 0.167, -0.167}},
  {{-0.5 + 0.167, -0.167}, {-0.5 + 0.167, 0.25}},
  
  {{-0.167, 0}, {-0.167, -0.167}},  // >>-dd-->
  {{-0.167, -0.167}, {0, -0.167}},
  {{0, -0.167}, {0, 0.25}},

  {{0.167, 0.25}, {0.167, -0.143}},  // >>-ddt->
  {{0.167, -0.143}, {0.167 + 0.031, -0.143 - 0.031}},
  {{0.167 + 0.031, -0.143 - 0.031}, {0.167 + 0.1, -0.143}}
};

#define NUMCHEATPLYRLINES (sizeof(cheat_player_arrow)/sizeof(mline_t))

#if 0
// -ES- 1999/10/16 Cos(60) and Sin(60)
#define C (0.5)
#define S (0.8660254038)

static mline_t triangle_guy[] =
{
  {{-S, -C}, {S, -C}},
  {{S, -C}, {0, 1.0}},
  {{0, 1.0}, {-S, -C}}
};

#undef C
#undef S
#define NUMTRIANGLEGUYLINES (sizeof(triangle_guy)/sizeof(mline_t))
#endif

static mline_t thintriangle_guy[] =
{
  {{-0.5, -0.7}, {1.0, 0}},
  {{1.0, 0}, {-0.5, 0.7}},
  {{-0.5, 0.7}, {-0.5, -0.7}}
};

#define NUMTHINTRIANGLEGUYLINES (sizeof(thintriangle_guy)/sizeof(mline_t))

static int cheating = 0;
static int grid = 0;

static int leveljuststarted = 1;  // kludge until LevelInit() is called

int automapactive = 0;
static int finit_width;
static int finit_height;

// location and size of window on screen
static int f_x, f_y;
static int f_w, f_h;

static mpoint_t m_paninc;  // how far the window pans each tic (map coords)

static flo_t mtof_zoommul;  // how far the window zooms in each tic (map coords)
static flo_t ftom_zoommul;  // how far the window zooms in each tic (fb coords)

static flo_t m_x, m_y;  // LL x,y where the window is on the map (map coords)
static flo_t m_x2, m_y2;  // UR x,y where the window is on the map (map coords)

//
// width/height of window on map (map coords)
//
static flo_t m_w;
static flo_t m_h;

// based on level size
static flo_t min_x;
static flo_t min_y;
static flo_t max_x;
static flo_t max_y;

static flo_t max_w;  // max_x-min_x,
static flo_t max_h;  // max_y-min_y

// based on player size
static flo_t min_w;
static flo_t min_h;

static flo_t min_scale_mtof;  // used to tell when to stop zooming out
static flo_t max_scale_mtof;  // used to tell when to stop zooming in

// old stuff for recovery later
static flo_t old_m_w, old_m_h;
static flo_t old_m_x, old_m_y;

// old location used by the Follower routine
static mpoint_t f_oldloc;

// used by MTOF to scale from map-to-frame-buffer coords
static flo_t scale_mtof = INITSCALEMTOF;

// used by FTOM to scale from frame-buffer-to-map coords (=1/scale_mtof)
static flo_t scale_ftom;

// numbers used for marking by the automap
static const image_t *marknums[10];

// where the points are
static mpoint_t markpoints[AM_NUMMARKPOINTS];

// next point to be assigned
static int markpointnum = 0;

// specifies whether to follow the player around
static int followplayer = 1;

cheatseq_t cheat_amap = {0, 0};

static boolean_t stopped = true;

boolean_t newhud = false;

// current am colourmap
static const byte *am_colmap = NULL;

static void ActivateNewScale(void)
{
  m_x += m_w / 2;
  m_y += m_h / 2;
  m_w = FTOM(f_w);
  m_h = FTOM(f_h);
  m_x -= m_w / 2;
  m_y -= m_h / 2;
  m_x2 = m_x + m_w;
  m_y2 = m_y + m_h;
}

static void SaveScaleAndLoc(void)
{
  old_m_x = m_x;
  old_m_y = m_y;
  old_m_w = m_w;
  old_m_h = m_h;
}

static void RestoreScaleAndLoc(void)
{
  m_w = old_m_w;
  m_h = old_m_h;

  if (!followplayer)
  {
    m_x = old_m_x;
    m_y = old_m_y;
  }
  else
  {
    m_x = consoleplayer->mo->x - m_w / 2;
    m_y = consoleplayer->mo->y - m_h / 2;
  }
  m_x2 = m_x + m_w;
  m_y2 = m_y + m_h;

  // Change the scaling multipliers
  scale_mtof = (flo_t)f_w / m_w;
  scale_ftom = 1 / scale_mtof;
}

//
// adds a marker at the current location
//
static void AddMark(void)
{
  markpoints[markpointnum].x = m_x + m_w / 2;
  markpoints[markpointnum].y = m_y + m_h / 2;
  markpointnum = (markpointnum + 1) % AM_NUMMARKPOINTS;
}

//
// Determines bounding box of all vertices,
// sets global variables controlling zoom range.
//
static void FindMinMaxBoundaries(void)
{
  int i;
  flo_t a;
  flo_t b;

  min_x = min_y = INT_MAX;
  max_x = max_y = INT_MIN;

  for (i = 0; i < numvertexes; i++)
  {
    if (vertexes[i].x < min_x)
      min_x = vertexes[i].x;
    else if (vertexes[i].x > max_x)
      max_x = vertexes[i].x;

    if (vertexes[i].y < min_y)
      min_y = vertexes[i].y;
    else if (vertexes[i].y > max_y)
      max_y = vertexes[i].y;
  }

  max_w = max_x - min_x;
  max_h = max_y - min_y;

  min_w = 2 * PLAYERRADIUS;  // const? never changed?

  min_h = 2 * PLAYERRADIUS;

  a = (flo_t)f_w / max_w;
  b = (flo_t)f_h / max_h;

  min_scale_mtof = a < b ? a : b;
  max_scale_mtof = (flo_t)f_h / (2 * PLAYERRADIUS);
}

static void ChangeWindowLoc(void)
{
  if (m_paninc.x != 0 || m_paninc.y != 0)
  {
    followplayer = 0;
    f_oldloc.x = INT_MAX;
  }

  m_x += m_paninc.x;
  m_y += m_paninc.y;

  if (m_x + m_w / 2 > max_x)
    m_x = max_x - m_w / 2;
  else if (m_x + m_w / 2 < min_x)
    m_x = min_x - m_w / 2;

  if (m_y + m_h / 2 > max_y)
    m_y = max_y - m_h / 2;
  else if (m_y + m_h / 2 < min_y)
    m_y = min_y - m_h / 2;

  m_x2 = m_x + m_w;
  m_y2 = m_y + m_h;
}

//
// InitVariables
//
static void InitVariables(void)
{
  DEV_ASSERT2(consoleplayer);

  if (newhud == true)
    automapactive = 1;
  else
    automapactive = 2;

  f_oldloc.x = INT_MAX;

  m_paninc.x = m_paninc.y = 0;
  ftom_zoommul = 1.0;
  mtof_zoommul = 1.0;

  m_w = FTOM(f_w);
  m_h = FTOM(f_h);

  m_x = consoleplayer->mo->x - m_w / 2;
  m_y = consoleplayer->mo->y - m_h / 2;

  ChangeWindowLoc();

  // for saving & restoring
  old_m_x = m_x;
  old_m_y = m_y;
  old_m_w = m_w;
  old_m_h = m_h;

  // inform the status bar of the change
  stbar_update = true;
}

//
// LoadPics
//
static void LoadPics(void)
{
  int i;
  char namebuf[9];

  for (i = 0; i < 10; i++)
  {
    sprintf(namebuf, "AMMNUM%d", i);
    marknums[i] = W_ImageFromPatch(namebuf);
  }
}

static void ClearMarks(void)
{
  int i;

  for (i = 0; i < AM_NUMMARKPOINTS; i++)
    markpoints[i].x = -1;  // means empty

  markpointnum = 0;
}

//
// should be called at the start of every level
// right now, i figure it out myself
//
static void LevelInit(void)
{
  if (!cheat_amap.sequence)
    cheat_amap.sequence = DDF_LanguageLookup("iddt");

  leveljuststarted = 0;

  f_x = f_y = 0;
  f_w = finit_width;
  f_h = finit_height;

  ClearMarks();

  FindMinMaxBoundaries();

  scale_mtof = min_scale_mtof * (10.0/7);

  if (scale_mtof > max_scale_mtof)
    scale_mtof = min_scale_mtof;

  scale_ftom = 1 / scale_mtof;
}

//
// AM_Stop
//
void AM_Stop(void)
{
  automapactive = 0;
  stopped = true;
}

//
// StartAM
//
static void StartAM(void)
{
  // static int lastlevel = -1, lastepisode = -1;

  if (!stopped)
    AM_Stop();

  LevelInit();

  InitVariables();
  LoadPics();

  stopped = false;
}

void AM_InitResolution(void)
{
  finit_width  = SCREENWIDTH;
  finit_height = SCREENHEIGHT - FROM_200(ST_HEIGHT);

  LevelInit();  // -ES- 1998/08/20

  CON_CreateCVarBool("newhud", cf_normal, &newhud);

  if (M_CheckParm("-newmap"))
    newhud = true;
}

//
// Hides the map.
//
static void AM_Hide(void)
{
  automapactive = 0;
  viewactive = true;
}

static void AM_Show(void)
{
  if (stopped)
    StartAM();

  if (newhud == true)
    automapactive = 1;
  else
    automapactive = 2;

  viewactive = false;
}

//
// set the window scale to the maximum size
//
static void MinOutWindowScale(void)
{
  scale_mtof = min_scale_mtof;
  scale_ftom = 1 / scale_mtof;
  ActivateNewScale();
}

//
// set the window scale to the minimum size
//
static void MaxOutWindowScale(void)
{
  scale_mtof = max_scale_mtof;
  scale_ftom = 1 / scale_mtof;
  ActivateNewScale();
}

//
// Handle events (user inputs) in automap mode
//
boolean_t AM_Responder(event_t * ev)
{
  int rc;
  static int bigstate = 0;

  rc = false;

  if (!automapactive)
  {
    if (ev->type == ev_keydown && ((ev->value.key == (AM_STARTKEY >> 16)) || (ev->value.key == (AM_STARTKEY & 0xffff))))
    {
      AM_Show();
      rc = true;
    }
  }
  else if (ev->type == ev_keydown)
  {

    rc = true;
    switch (ev->value.key)
    {
      case AM_PANRIGHTKEY:
        // pan right
        if (!followplayer)
          m_paninc.x = FTOM(F_PANINC);
        else
          rc = false;
        break;
        
      case AM_PANLEFTKEY:
        // pan left
        if (!followplayer)
          m_paninc.x = -FTOM(F_PANINC);
        else
          rc = false;
        break;
        
      case AM_PANUPKEY:
        // pan up
        if (!followplayer)
          m_paninc.y = FTOM(F_PANINC);
        else
          rc = false;
        break;
        
      case AM_PANDOWNKEY:
        // pan down
        if (!followplayer)
          m_paninc.y = -FTOM(F_PANINC);
        else
          rc = false;
        break;
        
      case AM_ZOOMOUTKEY:
        // zoom out
        mtof_zoommul = M_ZOOMOUT;
        ftom_zoommul = M_ZOOMIN;
        break;
        
      case AM_ZOOMINKEY:
        // zoom in
        mtof_zoommul = M_ZOOMIN;
        ftom_zoommul = M_ZOOMOUT;
        break;
        
      case AM_GOBIGKEY:
        bigstate = !bigstate;
        if (bigstate)
        {
          SaveScaleAndLoc();
          MinOutWindowScale();
        }
        else
          RestoreScaleAndLoc();
        break;
        
      case AM_FOLLOWKEY:
        followplayer = !followplayer;
        f_oldloc.x = INT_MAX;
        // -ACB- 1998/08/10 Use DDF Lang Reference
        if (followplayer)
          CON_PlayerMessageLDF(consoleplayer, "AutoMapFollowOn");
        else
          CON_PlayerMessageLDF(consoleplayer, "AutoMapFollowOff");
        break;
        
      case AM_GRIDKEY:
        grid = !grid;
        // -ACB- 1998/08/10 Use DDF Lang Reference
        if (grid)
          CON_PlayerMessageLDF(consoleplayer, "AutoMapGridOn");
        else
          CON_PlayerMessageLDF(consoleplayer, "AutoMapGridOff");
        break;
        
      case AM_MARKKEY:
        // -ACB- 1998/08/10 Use DDF Lang Reference
        CON_PlayerMessage(consoleplayer, "%s %d",
            DDF_LanguageLookup("AutoMapMarkedSpot"),
            markpointnum);
        AddMark();
        break;
        
      case AM_CLEARMARKKEY:
        ClearMarks();
        // -ACB- 1998/08/10 Use DDF Lang Reference
        CON_PlayerMessageLDF(consoleplayer, "AutoMapMarksClear");
        break;
        
      default:
        if (ev->value.key == (AM_ENDKEY >> 16) || 
            ev->value.key == (AM_ENDKEY & 0xffff))
        {
          if (automapactive == 1)
          {
            stbar_update = true;
            automapactive = 2;
          }
          else
          {
            bigstate = 0;
            viewactive = true;
            AM_Hide();
          }
        }
        else
        {
          rc = false;
        }
    }
    // -ACB- 1999/09/28 Proper casting
    if (!deathmatch && M_CheckCheat(&cheat_amap, (char)ev->value.key))
    {
      rc = false;
      cheating = (cheating + 1) % 3;
    }
  }

  else if (ev->type == ev_keyup)
  {
    rc = false;
    switch (ev->value.key)
    {
      case AM_PANRIGHTKEY:
        if (!followplayer)
          m_paninc.x = 0;
        break;
        
      case AM_PANLEFTKEY:
        if (!followplayer)
          m_paninc.x = 0;
        break;
        
      case AM_PANUPKEY:
        if (!followplayer)
          m_paninc.y = 0;
        break;
        
      case AM_PANDOWNKEY:
        if (!followplayer)
          m_paninc.y = 0;
        break;

      case AM_ZOOMOUTKEY:
      case AM_ZOOMINKEY:
        mtof_zoommul = 1.0;
        ftom_zoommul = 1.0;
        break;
    }
  }

  return rc;

}

//
// Zooming
//
static void ChangeWindowScale(void)
{
  // Change the scaling multipliers
  scale_mtof *= mtof_zoommul;
  scale_ftom = 1.0 / scale_mtof;

  if (scale_mtof < min_scale_mtof)
    MinOutWindowScale();
  else if (scale_mtof > max_scale_mtof)
    MaxOutWindowScale();
  else
    ActivateNewScale();
}

static void DoFollowPlayer(void)
{
  if (f_oldloc.x != consoleplayer->mo->x || 
      f_oldloc.y != consoleplayer->mo->y)
  {
    m_x = FTOM(MTOF(consoleplayer->mo->x)) - m_w / 2;
    m_y = FTOM(MTOF(consoleplayer->mo->y)) - m_h / 2;
    m_x2 = m_x + m_w;
    m_y2 = m_y + m_h;
    f_oldloc.x = consoleplayer->mo->x;
    f_oldloc.y = consoleplayer->mo->y;
  }
}


//
// Updates on Game Tick
//
void AM_Ticker(void)
{
  if (!automapactive)
    return;

  if (followplayer)
    DoFollowPlayer();

  // Change the zoom if necessary
  if (ftom_zoommul != 1.0)
    ChangeWindowScale();

  // Change x,y location
  if (m_paninc.x != 0 || m_paninc.y != 0)
    ChangeWindowLoc();
}

//
// Rotation in 2D.
// Used to rotate player arrow line character.
//
static INLINE void Rotate(flo_t * x, flo_t * y, angle_t a)
{
  flo_t tmpx;

  tmpx = *x * M_Cos(a) - *y * M_Sin(a);

  *y = *x * M_Sin(a) + *y * M_Cos(a);

  *x = tmpx;
}

static INLINE void GetRotatedCoords(flo_t sx, flo_t sy,
    flo_t *dx, flo_t *dy)
{
  *dx = sx;
  *dy = sy;

  if (rotatemap)
  {
    // rotate coordinates so they are on the map correctly
    *dx -= consoleplayer->mo->x;
    *dy -= consoleplayer->mo->y;
    
    Rotate(dx, dy, ANG90 - consoleplayer->mo->angle);
    
    *dx += consoleplayer->mo->x;
    *dy += consoleplayer->mo->y;
  }
}

static INLINE angle_t GetRotatedAngle(angle_t src)
{
  if (rotatemap)
    return src + ANG90 - consoleplayer->mo->angle;

  return src;
}


//
// Draw visible parts of lines.
//
static void DrawMline(mline_t * ml, int colour)
{
  int x1, y1, x2, y2;
  int f_x2 = f_x + f_w - 1;
  int f_y2 = f_y + f_h - 1;

  DEV_ASSERT2(am_colmap);
  DEV_ASSERT2(0 <= colour && colour <= 255);

  // transform to frame-buffer coordinates.
  x1 = CXMTOF(ml->a.x);
  y1 = CYMTOF(ml->a.y);
  x2 = CXMTOF(ml->b.x);
  y2 = CYMTOF(ml->b.y);

  // trivial rejects
  if ((x1 < f_x && x2 < f_x) || (x1 > f_x2 && x2 > f_x2) ||
      (y1 < f_y && y2 < f_y) || (y1 > f_y2 && y2 > f_y2))
  {
    return;
  }
  
  vctx.SolidLine(x1, y1, x2, y2, colour);
}

//
// Draws flat (floor/ceiling tile) aligned grid lines.
//
static void DrawGrid(int colour)
{
  flo_t x, y;
  flo_t start, end;
  mline_t ml;

  // Figure out start of vertical gridlines
  start = m_x + fmod(MAPBLOCKUNITS - (m_x - bmaporgx), MAPBLOCKUNITS);
  end = m_x + m_w;

  // draw vertical gridlines
  ml.a.y = m_y;
  ml.b.y = m_y + m_h;
  for (x = start; x < end; x += MAPBLOCKUNITS)
  {
    ml.a.x = x;
    ml.b.x = x;
    DrawMline(&ml, colour);
  }

  // Figure out start of horizontal gridlines
  start = m_y + fmod(MAPBLOCKUNITS - (m_y - bmaporgy), MAPBLOCKUNITS);
  end = m_y + m_h;

  // draw horizontal gridlines
  ml.a.x = m_x;
  ml.b.x = m_x + m_w;
  for (y = start; y < end; y += MAPBLOCKUNITS)
  {
    ml.a.y = y;
    ml.b.y = y;
    DrawMline(&ml, colour);
  }
}

//
// CheckSimiliarRegions
//
// Checks whether the two sectors' regions are similiar.  If they are
// different enough, a line will be drawn on the automap.
//
// -AJA- 1999/12/07: written.
//
static boolean_t CheckSimiliarRegions(sector_t *front, sector_t *back)
{
  extrafloor_t *F, *B;

  if (front->tag == back->tag)
    return true;

  // Note: doesn't worry about liquids

  F = front->bottom_ef;
  B = back->bottom_ef;

  while (F && B)
  {
    if (F->top_h != B->top_h)
      return false;
    
    if (F->bottom_h != B->bottom_h)
      return false;
    
    F = F->higher;
    B = B->higher;
  }

  return (F || B) ? false : true;
}

//
// Determines visible lines, draws them.
//
// -AJA- This is now *lineseg* based, not linedef.
//
static void AM_WalkSeg(seg_t *seg)
{
  mline_t l;
  line_t *line;

  sector_t *front = seg->frontsector;
  sector_t *back  = seg->backsector;

  if (seg->miniseg)
  {
#if (DEBUG_TRUEBSP == 1)
    if (seg->partner && seg > seg->partner)
      return;
#endif

#if (DEBUG_TRUEBSP > 0)
    GetRotatedCoords(seg->v1->x, seg->v1->y, &l.a.x, &l.a.y);
    GetRotatedCoords(seg->v2->x, seg->v2->y, &l.b.x, &l.b.y);

    DrawMline(&l, MINI_COL);
#endif

    return;
  }
  
  line = seg->linedef;
  DEV_ASSERT2(line);

  // only draw segs on the _right_ side of linedefs
#if (DEBUG_TRUEBSP < 2)
  if (line->side[1] == seg->sidedef)
    return;
#endif

  GetRotatedCoords(seg->v1->x, seg->v1->y, &l.a.x, &l.a.y);
  GetRotatedCoords(seg->v2->x, seg->v2->y, &l.b.x, &l.b.y);

  if (cheating || (line->flags & ML_Mapped))
  {
    if ((line->flags & ML_DontDraw) && !cheating)
      return;

    if (!front || !back)
    {
      DrawMline(&l, WALL_COL);
    }
    else
    {
      if (line->special && line->special->singlesided)
      {  
        // teleporters
        DrawMline(&l, TELE_COL);
      }
      else if (line->flags & ML_Secret)
      {  
        // secret door
        if (cheating)
          DrawMline(&l, SECRET_COL);
        else
          DrawMline(&l, WALL_COL);
      }
      else if (back->f_h != front->f_h)
      {
        // floor level change
        DrawMline(&l, FLOOR_COL);
      }
      else if (back->c_h != front->c_h)
      {
        // ceiling level change
        DrawMline(&l, CEIL_COL);
      }
      else if ((front->exfloor_used > 0 || back->exfloor_used > 0) &&
               (front->exfloor_used != back->exfloor_used ||
                ! CheckSimiliarRegions(front, back)))
      {
        // -AJA- 1999/10/09: extra floor change.
        DrawMline(&l, REGION_COL);
      }
      else if (cheating || (DEBUG_TRUEBSP > 1))
      {
        DrawMline(&l, ALLMAP_COL);
      }
    }
  }
  else if (consoleplayer->powers[PW_AllMap])
  {
    if (! (line->flags & ML_DontDraw))
      DrawMline(&l, ALLMAP_COL);
  }
}


// -AJA- for debugging
#if (DEBUG_TRUEBSP == 4)
static void DEBUG_ShowSubSecs(void)
{
  int x, y;

  for (y=0;     y < f_h; y += 3)
  for (x=(y&1); x < f_w; x += 3)
  {
    flo_t mx = CXFTOM(x);
    flo_t my = CYFTOM(y);

    int subsec = R_PointInSubsector(mx, my) - subsectors;

    V_DrawPixel(main_scr, x, y, subsec * 17 + (subsec/256) * 11);
  }
}
#endif


static void DrawLineCharacter(mline_t *lineguy, int lineguylines, 
    flo_t radius, angle_t angle, int colour, flo_t x, flo_t y)
{
  int i;
  mline_t l;
  flo_t ch_x, ch_y;

  if (radius < 2)
    radius = 2;

  GetRotatedCoords(x, y, &ch_x, &ch_y);
  angle = GetRotatedAngle(angle);
  
  for (i = 0; i < lineguylines; i++)
  {
    l.a.x = lineguy[i].a.x * radius;
    l.a.y = lineguy[i].a.y * radius;

    if (angle)
      Rotate(&l.a.x, &l.a.y, angle);

    l.a.x += ch_x;
    l.a.y += ch_y;

    l.b.x = lineguy[i].b.x * radius;
    l.b.y = lineguy[i].b.y * radius;

    if (angle)
      Rotate(&l.b.x, &l.b.y, angle);

    l.b.x += ch_x;
    l.b.y += ch_y;

    DrawMline(&l, colour);
  }
}

static int player_colours[8] =
{ GREEN,  GRAY + GRAY_LEN*2/3, BROWN, RED + RED_LEN/2, 
  ORANGE, GRAY + GRAY_LEN*1/3, RED, PINK };

static void AM_DrawPlayer(mobj_t *mo)
{
  int colour;
  
  DEV_ASSERT2(mo->player->in_game);

  if (!netgame)
  {
    if (cheating)
      DrawLineCharacter(cheat_player_arrow, NUMCHEATPLYRLINES, 
          mo->radius, mo->angle, YOUR_COL, mo->x, mo->y);
    else
      DrawLineCharacter(player_arrow, NUMPLYRLINES, 
          mo->radius, mo->angle, YOUR_COL, mo->x, mo->y);

    return;
  }

  if ((deathmatch && !singledemo) && mo->player != consoleplayer)
    return;

  if (mo->player->powers[PW_PartInvis])
    colour = (DBLUE + DBLUE_LEN - 1);  // *close* to black
  else
    colour = player_colours[mo->player->pnum & 0x07];

  DrawLineCharacter(player_arrow, NUMPLYRLINES, 
      mo->radius, mo->angle, colour, mo->x, mo->y);
}

static void AM_WalkThing(mobj_t *mo)
{
  int colour = THING_COL;

  if (mo->player)
  {
    AM_DrawPlayer(mo);
    return;
  }

  if (cheating != 2)
    return;

  // -AJA- more colourful things
  if (mo->flags & MF_SPECIAL)
    colour = ITEM_COL;
  else if (mo->flags & MF_MISSILE)
    colour = MISSL_COL;
  else if (mo->extendedflags & EF_MONSTER && mo->health <= 0)
    colour = DEAD_COL;
  else if (mo->extendedflags & EF_MONSTER)
    colour = MONST_COL;
  
  DrawLineCharacter(thintriangle_guy, NUMTHINTRIANGLEGUYLINES,
      mo->radius, mo->angle, colour, mo->x, mo->y);
}

//
// AM_WalkSubsector
//
// Visit a subsector and draw everything.
//
static void AM_WalkSubsector(int num)
{
  subsector_t *sub = &subsectors[num];

  seg_t *seg;
  mobj_t *thing;

  // handle each seg
  for (seg=sub->segs; seg; seg=seg->sub_next)
  {
    AM_WalkSeg(seg);
  }

  // handle each thing
  for (thing=sub->thinglist; thing; thing=thing->snext)
  {
    AM_WalkThing(thing);
  }
}

//
// AM_CheckBBox
//
// Checks BSP node/subtree bounding box.
// Returns true if some part of the bbox might be visible.
//
static boolean_t AM_CheckBBox(flo_t *bspcoord)
{
  flo_t xl = bspcoord[BOXLEFT];
  flo_t yt = bspcoord[BOXTOP];
  flo_t xr = bspcoord[BOXRIGHT];
  flo_t yb = bspcoord[BOXBOTTOM];

  if (xr < m_x || xl > m_x2 || yt < m_y || yb > m_y2)
    return false;

  return true;
}

//
// AM_WalkBSPNode
//
// Walks all subsectors below a given node, traversing subtree
// recursively.  Just call with BSP root.
//
static void AM_WalkBSPNode(int bspnum)
{
  node_t *node;
  int side;

  // Found a subsector?
  if (bspnum & NF_SUBSECTOR)
  {
    AM_WalkSubsector(bspnum & (~NF_SUBSECTOR));
    return;
  }

  node = &nodes[bspnum];
  side = 0;

#if (DEBUG_TRUEBSP == 2 || DEBUG_TRUEBSP == 3)
  side = P_PointOnDivlineSide(consoleplayer->mo->x, consoleplayer->mo->y, &node->div);
#endif

  // Recursively divide right space
  if (AM_CheckBBox(node->bbox[0]))
    AM_WalkBSPNode(node->children[side]);

#if (DEBUG_TRUEBSP == 2)
  return;
#elif (DEBUG_TRUEBSP == 3)
  {
    mline_t l;

    flo_t x1 = node->div.x - node->div.dx * 2;
    flo_t y1 = node->div.y - node->div.dy * 2;
    flo_t x2 = node->div.x + node->div.dx * 2;
    flo_t y2 = node->div.y + node->div.dy * 2;

    GetRotatedCoords(x1+3, y1+3, &l.a.x, &l.a.y);
    GetRotatedCoords(x2+3, y2+3, &l.b.x, &l.b.y);

    DrawMline(&l, GREEN+GREEN_LEN*4/5);
    return;
  }
#endif

  // Recursively divide back space
  if (AM_CheckBBox(node->bbox[side ^ 1]))
    AM_WalkBSPNode(node->children[side ^ 1]);
}

static void DrawMarks(void)
{
  int i, sx, sy;

  for (i = 0; i < AM_NUMMARKPOINTS; i++)
  {
    if (markpoints[i].x != -1)
    {
      sx = CXMTOF(markpoints[i].x);
      sy = CYMTOF(markpoints[i].y);

      VCTX_ImageEasy(sx, sy, marknums[i]);
    }
  }
}

static void DrawCrosshair(int colour)
{
#if 0  // FIXME !
  // -AJA- 1999/07/04: now uses V_DrawPixel().

  V_DrawPixel(main_scr, f_w / 2, f_h / 2, colour);  // single point for now
#endif
}

static void AM_RenderScene(void)
{
  // walk the bsp tree
  AM_WalkBSPNode(root_node);
}

//
// AM_Drawer
//
void AM_Drawer(void)
{
  if (!automapactive)
    return;

  if (automapactive == 1)
    am_colmap = am_overlay_colmap;

  if (automapactive == 2)
  {
    am_colmap = am_normal_colmap;

    // clear the framebuffer
    vctx.SolidBox(f_x, f_y, f_w, f_h, BACK_COL, 1.0);
  }

  if (grid && !rotatemap)
    DrawGrid(GRID_COL);

  AM_RenderScene();

  DrawCrosshair(XHAIR_COL);
  DrawMarks();

#ifdef DEVELOPERS
  am_colmap = NULL;
#endif
}
