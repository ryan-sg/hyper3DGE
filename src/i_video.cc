//----------------------------------------------------------------------------
//  EDGE Linux SDL Video Code
//----------------------------------------------------------------------------
// 
//  Copyright (c) 1999-2004  The EDGE Team.
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

#ifdef MACOSX
#include <SDL.h>
#else
#include <SDL/SDL.h>
#endif

#include "i_defs.h"
#include "./i_trans.h"

#include "version.h"
#include "dm_state.h"
#include "dm_defs.h"
#include "dm_type.h"
#include "m_argv.h"
#include "st_stuff.h"
#include "v_res.h"
#include "v_colour.h"

#include "e_main.h"
#include "e_event.h"
#include "w_wad.h"

#include "s_sound.h"


#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <signal.h>


static SDL_Surface *my_vis;

static int graphics_shutdown = 0;

bool use_grab = true;

#if defined(MACOSX) || defined(BeOS)
bool use_warp_mouse = true;
#else
bool use_warp_mouse = false;
#endif

// Possible Screen Modes
static screenmode_t possresmode[] =
{
	// fullscreen modes
	{ 320, 200, 16, false},
	{ 320, 240, 16, false},
	{ 400, 300, 16, false},
	{ 512, 384, 16, false},
	{ 640, 400, 16, false},
	{ 640, 480, 16, false},
	{ 800, 600, 16, false},
	{1024, 768, 16, false},

	{ 320, 200, 24, false}, // TRUECOLOR
	{ 320, 240, 24, false},
	{ 400, 300, 24, false},
	{ 512, 384, 24, false},
	{ 640, 400, 24, false},
	{ 640, 480, 24, false},
	{ 800, 600, 24, false},
	{1024, 768, 24, false},

	// windowed modes
	{ 320, 200, 16, true},
	{ 320, 240, 16, true},
	{ 400, 300, 16, true},
	{ 512, 384, 16, true},
	{ 640, 400, 16, true},
	{ 640, 480, 16, true},
	{ 800, 600, 16, true},
	{1024, 768, 16, true},

	{ 320, 200, 24, true},  // TRUECOLOR
	{ 320, 240, 24, true},
	{ 400, 300, 24, true},
	{ 512, 384, 24, true},
	{ 640, 400, 24, true},
	{ 640, 480, 24, true},
	{ 800, 600, 24, true},
	{1024, 768, 24, true},

	{  -1,  -1, -1}
};


// ====================== INTERNALS ======================

static void VideoModeCommonStuff(void)
{
	// -AJA- turn off cursor -- BIG performance increase.
	//       Plus, the combination of no-cursor + grab gives 
	//       continuous relative mouse motion.
	if (use_grab)
	{
		SDL_ShowCursor(0);
		SDL_WM_GrabInput(SDL_GRAB_ON);
	}

	// -DEL- 2001/01/29 SDL_GrabInput doesn't work on beos so try to
	// stop the mouse leaving the window every frame.
	if (use_warp_mouse)
		SDL_WarpMouse(SCREENWIDTH/2, SCREENHEIGHT/2);

	SDL_WM_SetCaption("Enhanced Doom Gaming Engine", "EDGE");

#ifdef DEVELOPERS
	// override SDL signal handlers (the so-called "parachute").
	signal(SIGFPE,SIG_DFL);
	signal(SIGSEGV,SIG_DFL);
#endif
}

// =================== END OF INTERNALS ===================

//
// I_StartupGraphics
//
void I_StartupGraphics(void)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) < 0)
		I_Error("Couldn't init SDL\n");

	M_CheckBooleanParm("warpmouse", &use_warp_mouse, false);
	M_CheckBooleanParm("grab", &use_grab, false);

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE,     5);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,   5);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,    5);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   16);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

	// -ACB- 2000/03/16 Detect Possible Resolutions
	for (int i = 0; possresmode[i].width != -1; i++)
	{
		screenmode_t *mode = possresmode + i;

#if 0
		if (mode->windowed)
		{
			V_AddAvailableResolution(mode);
			continue;
		}
#endif

		int got_depth = SDL_VideoModeOK(mode->width, mode->height,
				mode->depth, SDL_OPENGL | SDL_DOUBLEBUF |
				(mode->windowed ? 0 : SDL_FULLSCREEN));

		int token = (got_depth * 100 + mode->depth);

		if (got_depth == mode->depth ||
			token == 1516 || token == 1615 ||
			token == 2432 || token == 3224)
		{
			V_AddAvailableResolution(mode);
		}
	}
}

//
// I_SetScreenSize
//
bool I_SetScreenSize(screenmode_t *mode)
{
	if (mode->depth < 15)
		return false;

	my_vis = SDL_SetVideoMode(mode->width, mode->height, mode->depth, 
					SDL_OPENGL | SDL_DOUBLEBUF |
					(mode->windowed ? 0 : SDL_FULLSCREEN));

	if (my_vis == NULL)
		return false;

	if (my_vis->format->BytesPerPixel <= 1)
		return false;

	SCREENWIDTH  = my_vis->w;
	SCREENHEIGHT = my_vis->h;
	SCREENBITS   = (my_vis->format->BytesPerPixel == 2) ? 16 : 24;

	VideoModeCommonStuff();

	SDL_GL_SwapBuffers();

	return true;
}

//
// I_StartFrame
//
void I_StartFrame(void)
{
}

//
// I_FinishFrame
//
void I_FinishFrame(void)
{
	SDL_GL_SwapBuffers();

	if (use_warp_mouse)
		SDL_WarpMouse(SCREENWIDTH/2, SCREENHEIGHT/2);
}

//
// I_RemoveGrab()
//
void I_RemoveGrab(void)
{
	if (my_vis && ! graphics_shutdown)
	{
		SDL_ShowCursor(1);
		SDL_WM_GrabInput(SDL_GRAB_OFF);
	}
}

//
// I_ShutdownGraphics
//
void I_ShutdownGraphics(void)
{
	if (graphics_shutdown)
		return;

	graphics_shutdown = 1;

	fprintf(stderr, "Shutting down graphics...\n");
	SDL_Quit ();
}
