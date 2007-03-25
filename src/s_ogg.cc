//----------------------------------------------------------------------------
//  EDGE OGG Music Player
//----------------------------------------------------------------------------
// 
//  Copyright (c) 2004-2005  The EDGE Team.
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
// -ACB- 2004/08/18 Written: 
//
// Based on a tutorial at DevMaster.net:
// http://www.devmaster.net/articles/openal-tutorials/lesson8.php
//

#include "i_defs.h"
#include "errorcodes.h"
#include "oggplayer.h"

#include "epi/epi.h"
#include "epi/errors.h"
#include "epi/endianess.h"

#include <string.h>

#include "s_cache.h"
#include "s_blit.h"

#define OGGV_BUFFER_SIZE  16384

//
// oggplayer datalump operation functions
//
size_t oggplayer_dlread(void *ptr, size_t size, size_t nmemb, void *datasource)
{
	oggplayer_c::datalump_s *d = (oggplayer_c::datalump_s *)datasource;
	size_t rb = size*nmemb;

	if (d->pos >= d->size)
		return 0;
	
	if (d->pos + rb > d->size)
		rb = d->size - d->pos;

	memcpy(ptr, d->data + d->pos, rb);
	d->pos += rb;		
	
	return rb / size;
}

int oggplayer_dlseek(void *datasource, ogg_int64_t offset, int whence)
{
	oggplayer_c::datalump_s *d = (oggplayer_c::datalump_s *)datasource;
	size_t newpos;

	switch(whence)
	{
		case 0: { newpos = (int) offset; break; }				// Offset
		case 1: { newpos = d->pos + (int)offset; break; }		// Pos + Offset
        case 2: { newpos = d->size + (int)offset; break; }	    // End + Offset
		default: { return -1; }	// WTF?
	}
	
	if (newpos >= d->size)
		return -1;

	d->pos = newpos;
	return 0;
}

int oggplayer_dlclose(void *datasource)
{
	oggplayer_c::datalump_s *d = (oggplayer_c::datalump_s *)datasource;
	if (d->size)
	{
		delete [] d->data;
        d->pos = 0;
		d->size = 0;
	}
	
	return 0;
}

long oggplayer_dltell(void *datasource)
{
	oggplayer_c::datalump_s *d = (oggplayer_c::datalump_s *)datasource;

	if (d->pos >= d->size)
		return -1;
			
	return d->pos;
}

//
// oggplayer file operation functions
//
size_t oggplayer_fread(void *ptr, size_t size, size_t nmemb, void *datasource)
{
	return fread(ptr, size, nmemb, (FILE*)datasource);
}

int oggplayer_fseek(void *datasource, ogg_int64_t offset, int whence)
{
	return fseek((FILE*)datasource, (int)offset, whence);
}

int oggplayer_fclose(void *datasource)
{
	return fclose((FILE*)datasource);
}

long oggplayer_ftell(void *datasource)
{
	return ftell((FILE*)datasource);
}


oggplayer_c::oggplayer_c()
{
	status = NOT_LOADED;
	vorbis_inf = NULL;
}

oggplayer_c::~oggplayer_c()
{
	Close();
}


epi::string_c oggplayer_c::GetError(int code)
{
    switch(code)
    {
        case OV_EREAD:
            return epi::string_c("Read from media error.");

		case OV_ENOTVORBIS:
            return epi::string_c("Not Vorbis data.");

		case OV_EVERSION:
            return epi::string_c("Vorbis version mismatch.");

		case OV_EBADHEADER:
            return epi::string_c("Invalid Vorbis header.");

		case OV_EFAULT:
            return epi::string_c("Internal error.");

		default:
			break;
    }

	return epi::string_c("Unknown Ogg error.");
}


void oggplayer_c::PostOpenInit()
{
	int result; 
	
    vorbis_inf = ov_info(&ogg_stream, -1);

    if (vorbis_inf->channels == 1)
        format = AL_FORMAT_MONO16;
    else
        format = AL_FORMAT_STEREO16;
    
///---    alGenBuffers(OGG_BUFFERS, buffers);
///---    result = alGetError();
///---	if (result != AL_NO_ERROR)
///---	{
///---		throw epi::error_c(ERR_MUSIC, 
///---			"[oggplayer_c::PostOpenInit] alGenBuffers() Failed");
///---	}

	// Loaded, but not playing
	status = STOPPED;
}

bool oggplayer_c::StreamIntoBuffer(fx_data_c *buf)
{
#if EPI_BYTEORDER == EPI_LIL_ENDIAN
	const int ogg_endian = 0;
#else
	const int ogg_endian = 1;
#endif

    int size = 0;

    while (size < BUFFER_SIZE)
    {
		int section;
        int result = ov_read(&ogg_stream, 
						pcm_buf + size, BUFFER_SIZE - size, 
						ogg_endian, 2 /* bytes per sample */,
						1 /* signed data */, &section);

		if (result == 0)  /* EOF */
			break;

		if (result > 0)
		{
            size += result;
			continue;
		}

		// Construct an error message
		epi::string_c s;
			
		s = "[oggplayer_c::StreamIntoBuffer] Failed: ";
		s += GetError(result);
		
		throw epi::error_c(ERR_MUSIC, s.GetString());
		/* NOT REACHED */
    }

    if (size == 0)
        return false;

    alBufferData(buffer, format, pcm_buf, size, vorbis_inf->rate);
    int al_err = alGetError();

	if (al_err != AL_NO_ERROR)
		throw epi::error_c(ERR_MUSIC,
			"[oggplayer_c::StreamIntoBuffer] alBufferData() Failed");

	return true;
}

//
// oggplayer_c::SetVolume()
//
void oggplayer_c::SetVolume(float gain) // FIXME: remove
{
}

//
// oggplayer_c::Open()
//
// DataLump version
//
void oggplayer_c::Open(const void *data, size_t size)
{
	if (status != NOT_LOADED)
		Close();

	ogg_lump.data = new byte[size];
	ogg_lump.pos = 0;
	ogg_lump.size = size;
	memcpy(ogg_lump.data, data, size);
	
	ov_callbacks CB;

	CB.read_func  = oggplayer_dlread;
	CB.seek_func  = oggplayer_dlseek;
	CB.close_func = oggplayer_dlclose;
	CB.tell_func  = oggplayer_dltell; 

    int result = ov_open_callbacks((void*)&ogg_lump, &ogg_stream, NULL, 0, CB);
    if (result < 0)
    {
		// Only time we have to kill this since OGG will deal with the handle
		// if ov_open_callbacks() succeeded
        oggplayer_dlclose((void*)&ogg_lump);	
  
		// Construct an error message
		epi::string_c s;

		s = "[oggplayer_c::Open](DataLump) Failed: ";
		s += GetError(result);
		
		throw epi::error_c(ERR_MUSIC, s.GetString());
    }

	PostOpenInit();
}

//
// oggplayer_c::Open()
//
// File Version
//
void oggplayer_c::Open(const char *name)
{
	if (status != NOT_LOADED)
		Close();

	ogg_file = fopen(name, "rb");
    if (!ogg_file)
    {
		throw epi::error_c(ERR_MUSIC, 
			"[oggplayer_c::Open](File) Could not open file.");
    }

	ov_callbacks CB;

	CB.read_func  = oggplayer_fread;
	CB.seek_func  = oggplayer_fseek;
	CB.close_func = oggplayer_fclose;
	CB.tell_func  = oggplayer_ftell; 

    int result = ov_open_callbacks((void*)ogg_file, &ogg_stream, NULL, 0, CB);
    if (result < 0)
    {
		// Only time we have to close this since OGG will deal with the handle
		// if ov_open() succeeded
        oggplayer_fclose(ogg_file);	
  
		// Construct an error message
		epi::string_c s;

		s = "[oggplayer_c::Open](File) Failed: ";
		s += GetError(result);
		
		throw epi::error_c(ERR_MUSIC, s.GetString());
    }

	PostOpenInit();
}

//
// oggplayer_c::Close()
//
void oggplayer_c::Close()
{
	if (status == NOT_LOADED)
		return;

	// Stop playback
	Stop();

	// Release Resource
	alDeleteBuffers(OGG_BUFFERS, buffers);
    DeleteMusicSource();

	ov_clear(&ogg_stream);
	
	status = NOT_LOADED;
}

//
// oggplayer_c::Pause()
//
void oggplayer_c::Pause()
{
	if (status != PLAYING)
		return;

	alSourcePause(music_source);
	status = PAUSED;
}

//
// oggplayer_c::Resume()
//
void oggplayer_c::Resume()
{
	if (status != PAUSED)
		return;

	alSourcePlay(music_source);
	status = PLAYING;
}

//
// oggplayer_c::Play()
//
void oggplayer_c::Play(bool loop, float gain)
{
    if (status != NOT_LOADED && status != STOPPED)
        return;
        
	// Load up initial buffer data
	Ticker();
#if 0
	for (;;)
	{
		fx_data_c *buf = S_QueueGetFreeBuffer(OGGV_BUFFER_SIZE, false);
				
		if (! buf)
			break;

		if (StreamIntoBuffer(buf))  // FIXME: !!! messed up
			S_QueuePushBuffer(buf);
		else
			throw epi::error_c(ERR_MUSIC, "[oggplayer_c::Play] Failed to load into buffers");
	}
#endif

	SetVolume(gain);

	looping = loop;
	status = PLAYING;
}

//
// oggplayer_c::Stop()
//
void oggplayer_c::Stop()
{
	if (status != PLAYING && status != PAUSED)
		return;

	// Stop playback
	S_QueueStop();
	
	status = STOPPED;
}

//
// oggplayer_c::Ticker()
//
void oggplayer_c::Ticker()
{
	if (status != PLAYING)
		return;

	int result;
	int processed;
	bool active = true;

	fx_data_c *buf;

	for (;;)
	{
		buf = S_QueueGetFreeBuffer(OGGV_BUFFER_SIZE, false);

		if (! buf)
			break;
		
		active = StreamIntoBuffer(buf);

		if (! active)
		{
			// No more data. Seek to beginning and stop if not looping
			if (! looping)
			{
				Stop();
				break;
			}

			ov_raw_seek(&ogg_stream, 0);
		}
		else
			S_QueuePushBuffer(buf);
	}

}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
