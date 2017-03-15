#pragma once
#include <windows.h>

#include "plugin.h"
#include "enc-vfw.h"

//////////////////////////////////////////////////////////////////////////
// Code                                                                 //
//////////////////////////////////////////////////////////////////////////

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
	return TRUE;
}

//////////////////////////////////////////////////////////////////////////
// Open Broadcaster Software Studio                                     //
//////////////////////////////////////////////////////////////////////////

OBS_DECLARE_MODULE();
OBS_MODULE_AUTHOR("Michael Fabian Dirks");
OBS_MODULE_USE_DEFAULT_LOCALE("enc-amf", "en-US");

/**
* Required: Called when the module is loaded.  Use this function to load all
* the sources/encoders/outputs/services for your module, or anything else that
* may need loading.
*
* @return           Return true to continue loading the module, otherwise
*                   false to indicate failure and unload the module
*/
MODULE_EXPORT bool obs_module_load(void) {
	VFW::Initialize();
	return true;
}

/** Optional: Called when the module is unloaded.  */
MODULE_EXPORT void obs_module_unload(void) {
	VFW::Finalize();
}

/** Optional: Returns the full name of the module */
MODULE_EXPORT const char* obs_module_name() {
	return PLUGIN_NAME;
}

/** Optional: Returns a description of the module */
MODULE_EXPORT const char* obs_module_description() {
	return PLUGIN_NAME;
}
