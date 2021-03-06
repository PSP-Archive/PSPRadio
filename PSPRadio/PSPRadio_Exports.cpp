/*
	PSPRadio / Music streaming client for the PSP. (Initial Release: Sept. 2005)
	Copyright (C) 2005  Rafael Cabezas a.k.a. Raf

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <stdarg.h>
#include <PSPApp.h>
#include "ScreenHandler.h"
#include "PSPRadio_Exports.h"
#include "Main.h"
#include "VisualizerInterface/vis_if.h"
#include "VIS_Plugin.h"

int module_stop(int args, void *argp)
{
	return 0;
}

/* Notify PSPRadio when exiting, so pspradio can unload the plugin */
void PSPRadioExport_PluginExits(plugin_type type)
{
	Log(LOG_LOWLEVEL, "PSPRadioExport_PluginExits(%d): Sending Event.", type);
	gPSPRadio->SendEvent(MID_PLUGINEXITED, (void *)((int)type), SID_PSPRADIO);
	PSPRadioExport_GiveUpExclusiveAccess();
}

int PSPRadioExport_Log(char *file, int line, loglevel_enum LogLevel, char *strFormat, ...)
{
	va_list args;
	char logmessage[1024];

	if (pLogging && LogLevel >= pLogging->GetLevel()) /** Log only if Set() was called and loglevel is correct */
	{
		va_start (args, strFormat);         /* Initialize the argument list. */

		vsprintf(logmessage, strFormat, args);
		
		pLogging->Log_(file, line, LogLevel, logmessage);

		va_end (args);                  /* Clean up. */
	}
	
	return 0;
}

char *PSPRadioExport_GetProgramVersion()
{
	return gPSPRadio->GetProgramVersion();
}

bool PSPRadioExport_IsUSBEnabled()
{
	return false;//gPSPRadio->IsUSBEnabled();
}

char *PSPRadioExport_GetMyIP()
{
	return gPSPRadio->GetMyIP();
}

void PSPRadioExport_RequestExclusiveAccess(plugin_type type)
{
	if (gPSPRadio->m_UI)
	{
		gPSPRadio->m_UI->OnScreenshot(CScreenHandler::PSPRADIO_SCREENSHOT_ACTIVE);
	}
	gPSPRadio->StopKeyLatch(PSP_CTRL_START | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER);
	gPSPRadio->SetExclusiveAccessPluginType(type);
}

void PSPRadioExport_GiveUpExclusiveAccess()
{
	gPSPRadio->SendEvent(MID_GIVEUPEXCLISIVEACCESS, NULL, SID_PSPRADIO);
}

char *PSPRadioExport_GetVersion()
{
	return IF_VERSION;
}

void PSPRadioExport_TakeScreenShot()
{
	gPSPRadio->TakeScreenShot();
}

extern DeviceBuffer *g_PCMBuffer;
DeviceBuffer *PSPRadioExport_GetPCMBuffer()
{
	return g_PCMBuffer;
}

int PSPRadioIF(pspradioexport_types type, pspradioexport_ifdata *Data)
{
	bool ret = false;
	switch(type)
	{
		default:
			ret = false; /* Not implemented */
			break;
		case PSPRADIOIF_SET_BUTTONMAP_CONFIG:
			gPSPRadio->Setup_ButtonMapping((CIniParser *)Data->Pointer);
			ret = true;
			break;
		case PSPRADIOIF_GET_SOUND_OBJECT:
			Data->Pointer = gPSPRadio->GetSoundObject();
			ret = true;
			break;
		case PSPRADIOIF_SET_RENDER_PCM:
			{
				u32*	vram_frame = (u32*)Data->Pointer;
				if (gPSPRadio->m_VisPluginData && gPSPRadio->m_VisPluginData->render_pcm && g_PCMBuffer)
					gPSPRadio->m_VisPluginData->render_pcm(vram_frame, (u16*)g_PCMBuffer);
				if (gPSPRadio->m_VisPluginData && gPSPRadio->m_VisPluginData->render_freq && g_PCMBuffer)
				{
					do_fft();
					gPSPRadio->m_VisPluginData->render_freq(vram_frame, g_FreqData);
				}
			}
			break;
		case PSPRADIOIF_GET_VISUALIZER_CONFIG:
			Data->Pointer = &(gPSPRadio->m_VisPluginConfig);
			break;
		case PSPRADIOIF_SET_VISUALIZER_CONFIG:
			memcpy(&gPSPRadio->m_VisPluginConfig, Data->Pointer, sizeof(VisPluginConfig));
			if (gPSPRadio->m_VisPluginData && gPSPRadio->m_VisPluginData->config_update)
				gPSPRadio->m_VisPluginData->config_update();
			break;

	}	
	
	return ret;
}
