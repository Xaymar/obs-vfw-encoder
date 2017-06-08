#pragma once
#include "libobs/obs-module.h"
#include <stdint.h>
#include <inttypes.h>

//////////////////////////////////////////////////////////////////////////
// Macro Definitions                                                    //
//////////////////////////////////////////////////////////////////////////

// Utility
#define vstr(s) dstr(s)
#define dstr(s) #s
#define clamp(val,low,high) (val > high ? high : (val < low ? low : val))
#ifndef __FUNCTION_NAME__
#if defined(_WIN32) || defined(_WIN64) //WINDOWS
#define __FUNCTION_NAME__   __FUNCTION__  
#else          //*NIX
#define __FUNCTION_NAME__   __func__ 
#endif
#endif

// Plugin
#define PLUGIN_NAME			"Video For Windows"
#include "Version.h"

// Logging
#define PLOG(level, ...)	blog(level, "[VFW] " __VA_ARGS__);
#define PLOG_ERROR(...)		PLOG(LOG_ERROR,   __VA_ARGS__)
#define PLOG_WARNING(...)	PLOG(LOG_WARNING, __VA_ARGS__)
#define PLOG_INFO(...)		PLOG(LOG_INFO,    __VA_ARGS__)
#define PLOG_DEBUG(...)		PLOG(LOG_DEBUG,   __VA_ARGS__)

// Properties
#define PROP_BITRATE			"Bitrate"
#define PROP_QUALITY			"Quality"
#define PROP_KEYFRAME_INTERVAL	"KeyframeInterval"
#define PROP_MODE				"Mode"
#define PROP_MODE_NORMAL		"Mode.Normal"
#define PROP_MODE_SEQUENTIAL	"Mode.Sequential"

#define PROP_CONFIGURE			"Configure"
#define PROP_ABOUT				"About"