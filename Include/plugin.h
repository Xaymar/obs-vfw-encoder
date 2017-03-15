#pragma once
#include "libobs/obs-module.h"
#include <stdint.h>

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
#define PLOG(level, ...)	blog(level, __VA_ARGS__);
#define PLOG_ERROR(...)		PLOG(LOG_ERROR,   __VA_ARGS__)
#define PLOG_WARNING(...)	PLOG(LOG_WARNING, __VA_ARGS__)
#define PLOG_INFO(...)		PLOG(LOG_INFO,    __VA_ARGS__)
#define PLOG_DEBUG(...)		PLOG(LOG_DEBUG,   __VA_ARGS__)
