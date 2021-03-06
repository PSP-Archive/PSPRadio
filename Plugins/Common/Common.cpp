#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <stdlib.h>
#include <PSPRadio_Exports.h>
#include <pspmoduleinfo.h>
#include <PSPRadio.h>
#include "Common.h"

extern SceModuleInfo module_info;

volatile int g_blocker = 0;
#define BLOCKER_CREATE_AND_BLOCK(x,name) {x=sceKernelCreateSema(name,0,0,1,0);sceKernelWaitSema(x,1,NULL);}
#define BLOCKER_UNBLOCK_AND_DESTROY(x)   {sceKernelDeleteSema(x);sleep(1);}

extern "C" void _init(void);
extern "C" void _fini(void);
extern "C" int __psp_free_heap(void);

int main(int argc, char **argv)
{
	SceSize am = sceKernelTotalFreeMemSize();
	ModuleLog(LOG_INFO, "main(%s): Available memory: %dbytes (%dKB or %dMB)", module_info.modname, am, am/1024, am/1024/1024);
#if 0
	ModuleLog(LOG_INFO, "main: Plugin '%s' PSPRadio Interface Version %s", module_info.modname, IF_VERSION);

	/** PSPRadio version is different than the version this plugin was compiled against: */
	if (strcmp(PSPRadioExport_GetVersion(), IF_VERSION) != 0)
	{
		ModuleLog(LOG_ALWAYS, "**ERROR**: VERSION ERROR: Plugin: %s is for PSPRadio I/F %s, but this PSPRadio complies to I/F %s", module_info.modname, IF_VERSION, PSPRadioExport_GetVersion());
		PSPRadioExport_PluginExits(PLUGIN_TYPE);
	}
#endif

	BLOCKER_CREATE_AND_BLOCK(g_blocker, module_info.modname);
	am = sceKernelTotalFreeMemSize();
	ModuleLog(LOG_INFO, "main(%s): Unblocked. Available memory: %dbytes (%dKB or %dMB). Calling _fini()", 
						module_info.modname, am, am/1024, am/1024/1024);
	_fini();
	__psp_free_heap();
	
	am = sceKernelTotalFreeMemSize();
	ModuleLog(LOG_INFO, "main(%s): _fini() called, blocking again. Available memory: %dbytes (%dKB or %dMB)", 
						module_info.modname, module_info.modname, am, am/1024, am/1024/1024);

	BLOCKER_CREATE_AND_BLOCK(g_blocker, module_info.modname);
	
	ModuleLog(LOG_INFO, "main(%s): Unblocked; exiting.", module_info.modname);
	return 0;
}

void* getModuleInfo(void)
{
	return (void *) &module_info;
}

int module_stop(int args, void *argp)
{
	BLOCKER_UNBLOCK_AND_DESTROY(g_blocker);

	return 0;
}

