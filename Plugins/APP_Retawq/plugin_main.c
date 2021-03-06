/*
 * PSP Software Development Kit - http://www.pspdev.org
 * -----------------------------------------------------------------------
 * Licensed under the BSD license, see LICENSE in PSPSDK root for details.
 *
 * main.c - Simple PRX example.
 *
 * Copyright (c) 2005 James Forshaw <tyranid@gmail.com>
 *
 * $Id: main.c 1531 2005-12-07 18:27:12Z tyranid $
 */
#include <pspctrl.h>
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <stdlib.h>
#include <PSPRadio_Exports.h>
#include <APP_Exports.h>
#include <Common.h>
#include "psp.h"


#define printf pspDebugScreenPrintf
void app_plugin_main();

#ifdef STAND_ALONE_APP
PSP_MODULE_INFO("Retawq", 0, 1, 1);

int main(int argc, char **argv)
{
	int thid = 0;

	thid = sceKernelCreateThread("app_thread", (void*) app_plugin_main, 0x25, 0xFA0*2, PSP_THREAD_ATTR_USER, 0);
	if(thid >= 0)
	{
		sceKernelStartThread(thid, 0, 0);
	}

	sceKernelSleepThreadCB();
	
	return 0;
}

#else /** PRX */

PSP_MODULE_INFO("APP_Retawq", 0, 1, 1);
PSP_HEAP_SIZE_KB(4*1024); /* 4MB of Heap */

int ModuleStartAPP()
{
	sleep(1);
	
	SceSize am = sceKernelTotalFreeMemSize();
	ModuleLog(LOG_INFO, "ModuleStartApp(): Available memory: %dbytes (%dKB or %dMB)", am, am/1024, am/1024/1024);

	int thid = 0;

 	thid = sceKernelCreateThread("app_thread", (void*) app_plugin_main, 45, 0xFA0*2, PSP_THREAD_ATTR_USER, 0);
	if(thid >= 0)
	{
		sceKernelStartThread(thid, 0, 0);
	}

	//wait_for_button();
	
	return 0;
}
#endif

int ModuleContinueApp()
{
	PSPRadioExport_RequestExclusiveAccess(PLUGIN_APP);
	pspDebugScreenInit();

	g_PSPEnableInput = truE;
	g_PSPEnableRendering = truE;
	window_redraw_all();
	return 0;
}

int main_loop(int argc, const char** argv);

void app_plugin_main()
{
	PSPRadioExport_RequestExclusiveAccess(PLUGIN_APP);

	pspDebugScreenInit();

	main_loop(0, NULL);

	pspDebugScreenInit();
	///PSPRadioExport_GiveUpExclusiveAccess(); /** PluginExits gives up access too */
	PSPRadioExport_PluginExits(PLUGIN_APP); /** Notify PSPRadio, so it can unload the plugin */
}
