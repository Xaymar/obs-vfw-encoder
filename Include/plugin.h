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
#define PLUGIN_NAME				"Video For Windows"
#include "Version.h"

// Logging
#define PLOG(level, ...)			blog(level, "[VFW] " __VA_ARGS__);
#define PLOG_ERROR(...)				PLOG(LOG_ERROR,   __VA_ARGS__)
#define PLOG_WARNING(...)			PLOG(LOG_WARNING, __VA_ARGS__)
#define PLOG_INFO(...)				PLOG(LOG_INFO,    __VA_ARGS__)
#define PLOG_DEBUG(...)				PLOG(LOG_DEBUG,   __VA_ARGS__)

// Properties
#define PROP_CONFIGURE				"Configure"
#define PROP_BITRATE				"Bitrate"
#define PROP_QUALITY				"Quality"
#define PROP_INTERVAL_TYPE			"IntervalType"
#define PROP_KEYFRAME_INTERVAL			"KeyframeInterval"
#define PROP_KEYFRAME_INTERVAL2			"KeyframeInterval2"
#define PROP_FORCE_KEYFRAMES			"ForceKeyframes"
#define PROP_MODE				"Mode"
#define PROP_MODE_NORMAL			"Mode.Normal"
#define PROP_MODE_TEMPORAL			"Mode.Temporal"
#define PROP_MODE_SEQUENTIAL			"Mode.Sequential"
#define PROP_ICMODE				"ICMode"
#define PROP_ICMODE_COMPRESS			"ICMode.Normal"
#define PROP_ICMODE_FASTCOMPRESS		"ICMode.Fast"
#define PROP_LATENCY				"Latency"
#define PROP_ABOUT				"About"