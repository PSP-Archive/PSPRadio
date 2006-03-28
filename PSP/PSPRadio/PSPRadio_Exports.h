#ifndef _PSPRADIO_EXPORTS_H_
	#define _PSPRADIO_EXPORTS_H_

	#include <Logging.h>

#ifdef STAND_ALONE_APP
	#define ModuleLog(level, format, args...) pspDebugScreenPrintf("%s@%d(%d): %s", __FILE__, __LINE__, level, format, ## args)
#else
	#define ModuleLog(level, format, args...) PSPRadioExport_Log(__FILE__, __LINE__, level, format, ## args)
#endif

#ifdef __cplusplus	
	extern "C" 
	{
#else
	#define bool int
#endif
		int main(int, char**);
		int module_stop(int args, void *argp);

		int   PSPRadioExport_Log(char *file, int line, loglevel_enum LogLevel, char *strFormat, ...);
		char *PSPRadioExport_GetProgramVersion();
		bool  PSPRadioExport_IsUSBEnabled();
		char *PSPRadioExport_GetMyIP();
		void PSPRadioExport_RequestExclusiveAccess();
		void PSPRadioExport_GiveUpExclusiveAccess();
		char *PSPRadioExport_GetVersion();


#ifdef __cplusplus	
	}
#endif

#endif

