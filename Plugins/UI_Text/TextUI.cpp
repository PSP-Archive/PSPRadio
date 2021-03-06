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
#include <list>
#include <pspdisplay.h>
#include <PSPApp.h>
#include <PSPSound.h>
#include <stdio.h>
#include <unistd.h> 
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <malloc.h>
#include <iniparser.h>
#include <stdarg.h>
#include <Screen.h>
#include <pthreadlite.h>
#include "TextUI.h"
#include <psputility_sysparam.h>
#include "VIS_Plugin.h"
#include <pspgu.h>


#define MAX_ROWS 		m_Screen->GetNumberOfTextRows()
#define MAX_COL 		m_FontEngine->GetNumberOfTextColumns()
#define PIXEL_TO_ROW(y)	((y)/m_FontEngine->GetFontHeight())
#define PIXEL_TO_COL(x) ((x)/m_FontEngine->GetFontWidth())
#define COL_TO_PIXEL(c) ((c)*m_FontEngine->GetFontWidth())
#define ROW_TO_PIXEL(r) ((r)*m_FontEngine->GetFontHeight())

#define BITDEPTH 32

#if (BITDEPTH==32) // (8888 ARGB to ABGR)
	#define RGB2BGR(x) (((x>>16)&0xFF) | (x&0xFF00) | ((x<<16)&0xFF0000))
#else //16 (8888ARGB to 4444ABGR)
	#define RGB2BGR(x) (((x>>16)&0xFF)*15/255) | ((((x>>8)&0xFF)*15/255)<<4) | (((x&0xFF)*15/255)<<8)
#endif
#define SET_DIRTY_BIT(x)    {m_dirtybitmask  |=x; }
#define UNSET_DIRTY_BIT(x)  {m_dirtybitmask  &=~x;}
#define SET_ACTIVE_BIT(x)   {m_activebitmask |=x; }
#define UNSET_ACTIVE_BIT(x) {m_activebitmask &=~x;}

#define BACKGROUND_BUFFER 3
#define OFFLINE_BUFFER    2

#define TextUILog ModuleLog

/* For render thread */
static CTextUI *sThis;
volatile bool s_ExitRenderThread = false;
volatile bool s_RenderThreadRunning = false;

extern UIPlugin textui_vtable;

CTextUI::CTextUI()
{
	TextUILog(LOG_VERYLOW, "CtextUI: Constructor start");
	
	m_Config = NULL;
	m_strCWD = NULL;
	m_strConfigDir = NULL;
	m_ScreenShotState = CScreenHandler::PSPRADIO_SCREENSHOT_NOT_ACTIVE;
	m_CurrentScreen = CScreenHandler::PSPRADIO_SCREEN_PLAYLIST;
#if (BITDEPTH==32)
	m_Screen = new CScreen(false, 480, 272, 512, PSP_DISPLAY_PIXEL_FORMAT_8888);
#else
	m_Screen = new CScreen(false, 480, 272, 512, PSP_DISPLAY_PIXEL_FORMAT_4444);
#endif
	m_Screen->m_Buffer[OFFLINE_BUFFER]    = (u32*)valloc(m_Screen->FRAMESIZE);
	m_Screen->m_Buffer[BACKGROUND_BUFFER] = (u32*)valloc(m_Screen->FRAMESIZE);

	if (m_Screen->m_Buffer[OFFLINE_BUFFER] == NULL)
		m_Screen->m_Buffer[OFFLINE_BUFFER] = (u32*)memalign(16, m_Screen->FRAMESIZE);
	if (m_Screen->m_Buffer[BACKGROUND_BUFFER] == NULL)
		m_Screen->m_Buffer[BACKGROUND_BUFFER] = (u32*)memalign(16, m_Screen->FRAMESIZE);

	m_Screen->SetDrawingMode(CScreen::DRMODE_OPAQUE);

	/* Set up font engine, use some sensible defaults */
	m_FontEngine = new CSFont();
	m_FontEngine->SetSurface(m_Screen->m_Buffer[OFFLINE_BUFFER], 480, 272, 512, BITDEPTH/8);
	
	m_strTitle = strdup("PSPRadio");
	m_strMessage = NULL;

	sThis = this;
	m_dirtybitmask = 0;
	m_activebitmask = ( BITMASK_PCM | BITMASK_MESSAGE );
	m_refreshbitmask = 0;
	m_LastBatteryPercentage = 0;
	sceRtcGetCurrentClockLocalTime(&m_LastLocalTime);
	m_bDisplayFPS = false;

	/* Get some PSPRadio objects */
	pspradioexport_ifdata ifdata;
	memset(&ifdata, 0, sizeof(ifdata));
	ifdata.Pointer = m_Config;

	PSPRadioIF(PSPRADIOIF_GET_SOUND_OBJECT, &ifdata);
	m_Sound = (CPSPSound*)ifdata.Pointer;
	m_Container = NULL;
	m_RenderLock = new CLock("TextUI_RenderLock");
	
	m_PSPRadio = (CPSPRadio*)textui_vtable.PSPRadioObject;

	m_FullscreenWait = m_PSPRadio->GetConfig()->GetInteger("PLUGINS:VISUALIZER_FULLSCREEN_WAIT", 10); /* Default = 10sec */

	m_TimeStartedPlaying = 0;
	m_IsPlaying = false;
	
	m_fs_vis_cfg.sc_width = m_Screen->m_Width;
	m_fs_vis_cfg.sc_height = m_Screen->m_Height;
	m_fs_vis_cfg.sc_pitch = m_Screen->m_Pitch;
	m_fs_vis_cfg.sc_pixel_format = m_Screen->m_PixelFormat;
	m_fs_vis_cfg.x1 = 0;
	m_fs_vis_cfg.y1 = 0;
	m_fs_vis_cfg.x2 = m_Screen->m_Width;
	m_fs_vis_cfg.y2 = m_Screen->m_Height - 1;
	m_fs_vis_cfg.fullscreen = 1;

	/* Start Render Thread */
	{
		pthread_t pthid;
		pthread_attr_t pthattr;
		struct sched_param shdparam;
		pthread_attr_init(&pthattr);
		pthattr.scope = PTHREAD_SCOPE_PROCESS_VFPU;
		shdparam.sched_policy = SCHED_OTHER;
		shdparam.sched_priority = 45;
		pthread_attr_setschedparam(&pthattr, &shdparam);
		pthread_create(&pthid, &pthattr, render_thread, NULL);
	}


	TextUILog(LOG_VERYLOW, "CtextUI: Constructor end.");
}

void CTextUI::Terminate()
{
	s_ExitRenderThread = true;
	m_dirtybitmask = m_activebitmask = 0;
}

CTextUI::~CTextUI()
{
	TextUILog(LOG_VERYLOW, "~CTextUI(): Start");

	s_ExitRenderThread = true;
	for (;;)
	{
		if (s_RenderThreadRunning == false)
			break;
		sceKernelDelayThread(100*1000); /* yield for 100ms*/
	}
	sThis = NULL;
	

	delete(m_Config), m_Config = NULL;
	free(m_strCWD), m_strCWD = NULL;
	free(m_strConfigDir), m_strConfigDir = NULL;
	free(m_strMessage), m_strMessage = NULL;
	delete(m_RenderLock), m_RenderLock = NULL;
	delete(m_Screen), m_Screen = NULL;

	TextUILog(LOG_INFO, "~CTextUI(): End");
}

void CTextUI::NewPCMBuffer(short *pcmbuffer)
{
	SET_DIRTY_BIT(BITMASK_PCM);
}

/* static member */
void CTextUI::render_thread(void *) 
{
	s_RenderThreadRunning = true;
	sThis->RenderLoop();
	s_RenderThreadRunning = false;
}

void CTextUI::RenderLoop()
{
	/* For FPS Calculation: */
	clock_t time1 = 0, time2 = 0, timenow = 0;
	int frame_count = 0;
	int total_time = 0;
	int fps = 0;
	pspradioexport_ifdata ifdata = { 0 };
	char strFPS[16];

	enum _render_mode
	{
		RM_NORMAL,
		RM_FULLSCREEN,
		RM_VISUALIZER_EXCLUSIVE,
	} render_mode = RM_NORMAL;

	for (;;)
	{
		if (s_ExitRenderThread)
			break;

		if (m_ScreenShotState == CScreenHandler::PSPRADIO_SCREENSHOT_ACTIVE)
		{
			sceKernelDelayThread(1); /* yield */
			continue;
		}

		timenow = sceKernelLibcClock();
		if ((timenow - time1) >= 1*CLOCKS_PER_SEC)
			SET_DIRTY_BIT(BITMASK_TIMER_1S);

		if (m_dirtybitmask)
		{
			time1 = timenow;

			switch(render_mode)
			{
			case RM_NORMAL:
				if ( m_IsPlaying && m_FullscreenWait &&
					(((time1 - m_TimeStartedPlaying)/CLOCKS_PER_SEC) >= m_FullscreenWait) &&
					m_PSPRadio->m_VisPluginData )
				{
					ifdata.Pointer = &m_fs_vis_cfg;
					PSPRadioIF(PSPRADIOIF_SET_VISUALIZER_CONFIG, &ifdata);
					m_dirtybitmask = m_refreshbitmask;
					if (m_PSPRadio->m_VisPluginData && m_PSPRadio->m_VisPluginData->type == VIS_TYPE_EXCL)
					{
						render_mode = RM_VISUALIZER_EXCLUSIVE;
					}
					else
					{
						render_mode = RM_FULLSCREEN;
					}
				}
				else
				{
					RenderNormal();
				}
				break;
			case RM_FULLSCREEN:
				if ( m_IsPlaying == false ||
					(((time1 - m_TimeStartedPlaying)/CLOCKS_PER_SEC) < m_FullscreenWait) ||
					(m_PSPRadio->m_VisPluginData == NULL) )
				{
					ifdata.Pointer = &m_vis_cfg;
					PSPRadioIF(PSPRADIOIF_SET_VISUALIZER_CONFIG, &ifdata);
					render_mode = RM_NORMAL;
					m_dirtybitmask = m_refreshbitmask;
				}
				else
				{
					RenderFullscreenVisualizer();
				}
				break;
			case RM_VISUALIZER_EXCLUSIVE:
				if ( m_IsPlaying == false ||
					(((time1 - m_TimeStartedPlaying)/CLOCKS_PER_SEC) < m_FullscreenWait) ||
					(m_PSPRadio->m_VisPluginData == NULL) )
				{
					ifdata.Pointer = &m_vis_cfg;
					PSPRadioIF(PSPRADIOIF_SET_VISUALIZER_CONFIG, &ifdata);
					render_mode = RM_NORMAL;
					m_dirtybitmask = m_refreshbitmask;
				}
				else
				{
					if (m_dirtybitmask & BITMASK_PCM)
					{
						UNSET_DIRTY_BIT(BITMASK_PCM);
						if (m_activebitmask & BITMASK_PCM)
						{
							ifdata.Pointer = m_Screen->m_Buffer[m_Screen->DrawBufferIndex];
							PSPRadioIF(PSPRADIOIF_SET_RENDER_PCM, &ifdata);
						}
					}
					/* We don't render, the Visualizer has exlusive rights */
					sceKernelDelayThread(1); /* yield */
					continue;
				}
				break;
			}
			
			/* FPS Calculation */
			if (++frame_count == 10)
			{
				fps = (frame_count * CLOCKS_PER_SEC) / total_time;
				frame_count = 0;
				total_time = 0;
			}
			if (m_bDisplayFPS)
			{
				sprintf(strFPS, "fps:%03d", fps);
				m_FontEngine->PrintText(m_Screen->m_Buffer[m_Screen->DrawBufferIndex], 10, 262, 0xFFFFFFFF, strFPS);
			}
	
			/* Flip Buffers */
			m_RenderLock->Lock();
			sceDisplayWaitVblankStart();
			m_Screen->SwapBuffers();
			m_RenderLock->Unlock();

			/* fps calculation needs to include wait for vblank and flipping */
			time2 = sceKernelLibcClock();
			total_time += (time2 - time1);

		}
		sceKernelDelayThread(1); /* yield */
	}
}

void CTextUI::RenderNormal()
{
	static bool draw_background = true;
	static pspradioexport_ifdata ifdata = { 0 };
	static bool enable_msgbox = false;
	static clock_t msgbox_start = { 0 };
	
	if (m_dirtybitmask & BITMASK_BACKGROUND)
	{
		UNSET_DIRTY_BIT(BITMASK_BACKGROUND);
		if (m_activebitmask & BITMASK_BACKGROUND)
		{
			////m_RenderLock->Lock();
			sceKernelDcacheWritebackAll(); 
			m_Screen->StartList();
			m_Screen->CopyRectangle(BACKGROUND_BUFFER, OFFLINE_BUFFER, 
				0, 0, m_Screen->m_Width, m_Screen->m_Height);
			m_Screen->EndList();
			PrintProgramVersion(OFFLINE_BUFFER);
			////m_RenderLock->Unlock();
			draw_background  = false;
		}
	}
	else
	{
		draw_background = true;
	}
	
	if (m_dirtybitmask & BITMASK_TIME)
	{
		UNSET_DIRTY_BIT(BITMASK_TIME);
		if (m_activebitmask & BITMASK_TIME)
			PrintTime(OFFLINE_BUFFER, draw_background);
	}
	if (m_dirtybitmask & BITMASK_BATTERY)
	{
		UNSET_DIRTY_BIT(BITMASK_BATTERY);
		if (m_activebitmask & BITMASK_BATTERY)
			PrintBattery(OFFLINE_BUFFER, draw_background);
	}
	if (m_dirtybitmask & BITMASK_BUFFER_PERCENTAGE)
	{
		UNSET_DIRTY_BIT(BITMASK_BUFFER_PERCENTAGE);
		if (m_activebitmask & BITMASK_BUFFER_PERCENTAGE)
			PrintBufferPercentage(OFFLINE_BUFFER, draw_background);
	}
	if (m_dirtybitmask & BITMASK_SONG_DATA)
	{
		UNSET_DIRTY_BIT(BITMASK_SONG_DATA);
		if (m_activebitmask & BITMASK_SONG_DATA)
			PrintSongData(OFFLINE_BUFFER, draw_background);
	}
	if (m_dirtybitmask & BITMASK_STREAM_TIME)
	{
		UNSET_DIRTY_BIT(BITMASK_STREAM_TIME);
		if (m_activebitmask & BITMASK_STREAM_TIME)
			PrintStreamTime(OFFLINE_BUFFER, draw_background);
	}
	if (m_dirtybitmask & BITMASK_CONTAINERS)
	{
		UNSET_DIRTY_BIT(BITMASK_CONTAINERS);
		if (m_activebitmask & BITMASK_CONTAINERS)
			PrintContainers(OFFLINE_BUFFER, draw_background);
	}
	if (m_dirtybitmask & BITMASK_ELEMENTS)
	{
		UNSET_DIRTY_BIT(BITMASK_ELEMENTS);
		if (m_activebitmask & BITMASK_ELEMENTS)
			PrintElements(OFFLINE_BUFFER, draw_background);
	}
	if (m_dirtybitmask & BITMASK_OPTIONS)
	{
		UNSET_DIRTY_BIT(BITMASK_OPTIONS);
		if (m_activebitmask & BITMASK_OPTIONS)
			PrintOptionsScreen(OFFLINE_BUFFER, draw_background);
	}
	if (m_dirtybitmask & BITMASK_ACTIVE_COMMAND)
	{
		UNSET_DIRTY_BIT(BITMASK_ACTIVE_COMMAND);
		if (m_activebitmask & BITMASK_ACTIVE_COMMAND)
			PrintActiveCommand(OFFLINE_BUFFER, draw_background);
	}
	if (m_dirtybitmask & BITMASK_CURRENT_CONTAINER_SIDE_TITLE)
	{
		UNSET_DIRTY_BIT(BITMASK_CURRENT_CONTAINER_SIDE_TITLE);
		if (m_activebitmask & BITMASK_CURRENT_CONTAINER_SIDE_TITLE)
			PrintCurrentContainerSideTitle(OFFLINE_BUFFER, draw_background);
	}
	if (m_dirtybitmask & BITMASK_TIMER_1S)
	{
		UNSET_DIRTY_BIT(BITMASK_TIMER_1S);
	}
		
	/* Copy buffer OFFLINE_BUFFER to back-buffer */
	m_Screen->StartList();
	m_Screen->CopyFromToBuffer(m_Screen->m_Buffer[OFFLINE_BUFFER], m_Screen->m_Buffer[m_Screen->DrawBufferIndex]);
	m_Screen->EndList();

	/* Do effects to back-buffer */
	if (m_dirtybitmask & BITMASK_PCM)
	{
		UNSET_DIRTY_BIT(BITMASK_PCM);
		if (m_activebitmask & BITMASK_PCM)
		{
			ifdata.Pointer = m_Screen->m_Buffer[m_Screen->DrawBufferIndex];
			PSPRadioIF(PSPRADIOIF_SET_RENDER_PCM, &ifdata);
		}
	}
	if (m_dirtybitmask & BITMASK_MESSAGE)
	{
		UNSET_DIRTY_BIT(BITMASK_MESSAGE);
		if (m_activebitmask & BITMASK_MESSAGE)
		{
			enable_msgbox = true;
			msgbox_start = sceKernelLibcClock();
		}
	}
	
	if (enable_msgbox)
	{
		////m_RenderLock->Lock();
		RenderMessage(m_Screen->m_Buffer[m_Screen->DrawBufferIndex]);
		////m_RenderLock->Unlock();
		clock_t now = sceKernelLibcClock();
		if (now - msgbox_start >= 3*CLOCKS_PER_SEC) /* 3 seconds */
		{
			enable_msgbox = false;
		}
	}

}	

void CTextUI::RenderFullscreenVisualizer()
{
	static pspradioexport_ifdata ifdata = { 0 };
	static bool enable_msgbox = false;
	static clock_t msgbox_start = { 0 };
	static char strSongData[1024];
	MetaData *pData = m_Sound->GetCurrentStream()->GetMetaData();
	
	if (m_ScreenShotState == CScreenHandler::PSPRADIO_SCREENSHOT_NOT_ACTIVE)
	{
		char *pURI;
		if (memcmp(pData->strURI, "http", 4)==0)
		{
			pURI="Online";
		}
		else
		{
			pURI = strrchr(pData->strURI, '/');
			if (pURI && strlen(pURI) > 2)
			{
				pURI++; /* skip slash */
			}
			else
			{
				pURI = pData->strURI;
			}
		}
		
		m_Screen->StartList();
		m_Screen->Clear();
		m_Screen->EndList();

		ifdata.Pointer = m_Screen->m_Buffer[m_Screen->DrawBufferIndex];
		PSPRadioIF(PSPRADIOIF_SET_RENDER_PCM, &ifdata);

		sprintf(strSongData, "%02ld:%02ld  %s  %s",
					pData->lCurrentTime / 60, pData->lCurrentTime % 60,
					//pData->lTotalTime / 60, pData->lTotalTime % 60,
					(pData->strArtist && strlen(pData->strArtist))?pData->strArtist:pURI,
					(pData->strTitle && strlen(pData->strTitle))?pData->strTitle:"");
		strSongData[MAX_COL - 1] = 0;
		/** CENTER */
		int x = m_Screen->m_Width/2 - ((strlen(strSongData)/2)*m_FontEngine->GetFontWidth());

		m_FontEngine->PrintText(m_Screen->m_Buffer[m_Screen->DrawBufferIndex], x, 10, 0xFFFFFF, strSongData);

		if (m_dirtybitmask & BITMASK_MESSAGE)
		{
			UNSET_DIRTY_BIT(BITMASK_MESSAGE);
			if (m_activebitmask & BITMASK_MESSAGE)
			{
				enable_msgbox = true;
				msgbox_start = sceKernelLibcClock();
			}
		}
		
		if (enable_msgbox)
		{
			////m_RenderLock->Lock();
			RenderMessage(m_Screen->m_Buffer[m_Screen->DrawBufferIndex]);
			////m_RenderLock->Unlock();
			clock_t now = sceKernelLibcClock();
			if (now - msgbox_start > 3*CLOCKS_PER_SEC) /* 3 seconds */
			{
				enable_msgbox = false;
			}
		}
		m_dirtybitmask = 0;
	}
}

void CTextUI::RenderMessage(u32 *pBuffer)
{
	//sceKernelDcacheWritebackAll(); 
	//m_Screen->SetDrawingMode(CScreen::DRMODE_TRANSPARENT);
	m_Screen->SetDrawingMode(CScreen::DRMODE_OPAQUE);
	m_Screen->Rectangle(pBuffer, 100, 50, m_Screen->m_Width - 100, 100, 0xFFFFFF);//0xAAAAAA);

	m_Screen->SetDrawingMode(CScreen::DRMODE_OPAQUE);
	m_Screen->VertLine(pBuffer, 100, 50, 100, 0x0);
	m_Screen->VertLine(pBuffer, m_Screen->m_Width - 100, 50, 100, 0x0);
	m_Screen->VertLine(pBuffer, 102, 52, 98, 0x0);
	m_Screen->VertLine(pBuffer, m_Screen->m_Width - 102, 52, 98, 0x0);

	m_Screen->HorizLine(pBuffer, 50, 100, m_Screen->m_Width - 100, 0x0);
	m_Screen->HorizLine(pBuffer, 100, 100, m_Screen->m_Width - 100, 0x0);
	m_Screen->HorizLine(pBuffer, 52, 102, m_Screen->m_Width - 102, 0x0);
	m_Screen->HorizLine(pBuffer, 98, 102, m_Screen->m_Width - 102, 0x0);
	//m_Screen->SetDrawingMode(CScreen::DRMODE_TRANSPARENT);

	TextUILog(LOG_INFO, "RenderMessage(): '%s'", m_strMessage);
	//uiPrintf(pBuffer, 110,60, 0xFFFFFF, m_strMessage);
	m_FontEngine->PrintText(pBuffer, 110,60, 0xFFFFFF, m_strMessage);
}

void CTextUI::PrintProgramVersion(int iBuffer)
{
	uiPrintf(iBuffer, m_ScreenConfig.ProgramVersionX, m_ScreenConfig.ProgramVersionY, 
				m_ScreenConfig.ProgramVersionColor, PSPRadioExport_GetProgramVersion());
}

int CTextUI::Initialize(char *strCWD, char *strSkinDir)
{
	TextUILog(LOG_VERYLOW, "CTextUI::Initialize Start");
	TextUILog(LOG_VERYLOW, "CTextUI::strCWD='%s' strSkinDir='%s'", strCWD, strSkinDir);
	
	free(m_strCWD);
	m_strCWD = strdup(strCWD);
	//m_Screen->Init();
	free(m_strConfigDir);
	m_strConfigDir = strdup(strSkinDir);

	TextUILog(LOG_VERYLOW, "CTextUI::Initialize End");
	
	return 0;
}

int CTextUI::GetConfigColor(char *strKey)
{
	int iRet;
	sscanf(m_Config->GetStr(strKey), "%x", &iRet);
	
	return RGB2BGR(iRet);
}

void CTextUI::GetConfigPair(char *strKey, int *x, int *y)
{
	sscanf(m_Config->GetString(strKey, "0,0"), "%d,%d", x, y);
}

void CTextUI::LoadConfigSettings(IScreen *Screen)
{
	char *strCfgFile = NULL;
	VisPluginConfig vis_cfg;
	CIniParser *newconfig = NULL;
	CIniParser *oldconfig = NULL;

	TextUILog(LOG_LOWLEVEL, "LoadConfigSettings() start");

	if (Screen->GetConfigFilename())
	{
		strCfgFile = (char *)malloc(strlen(m_strCWD) + strlen(Screen->GetConfigFilename()) + 64);
		if (strCfgFile == NULL)
			return;

		sprintf(strCfgFile, "%s/%s/%s", m_strCWD, m_strConfigDir, Screen->GetConfigFilename());
	
		TextUILog(LOG_LOWLEVEL, "LoadConfigSettings(): Using '%s' config file", strCfgFile);
		
		newconfig = new CIniParser(strCfgFile);

		if (newconfig != NULL)
		{
			oldconfig = m_Config;
			m_Config = newconfig;
			/* Now we can destroy the old config object */
			if (oldconfig)
			{
				delete(oldconfig);
			}
		}
		else
		{
			TextUILog(LOG_ERROR, "Error creating iniparser. Out of memory?");
			return;
		}

		TextUILog(LOG_VERYLOW, "After instantiating the iniparser");
		
		free (strCfgFile), strCfgFile = NULL;
		
		memset(&m_ScreenConfig, 0, sizeof(m_ScreenConfig));
		
		/** General */
		m_ScreenConfig.ClockFormat = m_Config->GetInteger("GENERAL:CLOCK_FORMAT", 0);
		switch (m_ScreenConfig.ClockFormat)
		{
		case 12:
			m_ScreenConfig.ClockFormat = PSP_SYSTEMPARAM_TIME_FORMAT_12HR;
			break;
		case 24:
			m_ScreenConfig.ClockFormat = PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
			break;
		case 0:
		default:
			sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_TIME_FORMAT, &m_ScreenConfig.ClockFormat);
			break;
		}
		//sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_TIME_FORMAT, &m_ScreenConfig.ClockFormat);
		
		m_ScreenConfig.FontMode   = (CSFont::textmode)m_Config->GetInteger("SCREEN_SETTINGS:FONT_MODE", 0);
		m_ScreenConfig.FontWidth  = m_Config->GetInteger("SCREEN_SETTINGS:FONT_WIDTH", 7);
		m_ScreenConfig.FontHeight = m_Config->GetInteger("SCREEN_SETTINGS:FONT_HEIGHT", 8);
		m_ScreenConfig.strBackground = m_Config->GetString("SCREEN_SETTINGS:BACKGROUND", NULL);
		m_ScreenConfig.BgColor = GetConfigColor("SCREEN_SETTINGS:BG_COLOR");
		m_ScreenConfig.FgColor = GetConfigColor("SCREEN_SETTINGS:FG_COLOR");
		GetConfigPair("SCREEN_SETTINGS:CONTAINERLIST_X_RANGE", 
								&m_ScreenConfig.ContainerListRangeX1, &m_ScreenConfig.ContainerListRangeX2);
		GetConfigPair("SCREEN_SETTINGS:CONTAINERLIST_Y_RANGE", 
								&m_ScreenConfig.ContainerListRangeY1, &m_ScreenConfig.ContainerListRangeY2);
		GetConfigPair("SCREEN_SETTINGS:ENTRIESLIST_X_RANGE", 
								&m_ScreenConfig.EntriesListRangeX1, &m_ScreenConfig.EntriesListRangeX2);
		GetConfigPair("SCREEN_SETTINGS:ENTRIESLIST_Y_RANGE", 
								&m_ScreenConfig.EntriesListRangeY1, &m_ScreenConfig.EntriesListRangeY2);
		GetConfigPair("SCREEN_SETTINGS:BUFFER_PERCENTAGE_XY", 
								&m_ScreenConfig.BufferPercentageX, &m_ScreenConfig.BufferPercentageY);
		m_ScreenConfig.BufferPercentageColor = GetConfigColor("SCREEN_SETTINGS:BUFFER_PERCENTAGE_COLOR");
		m_ScreenConfig.MetadataX1 = m_Config->GetInteger("SCREEN_SETTINGS:METADATA_X", 7);
		m_ScreenConfig.MetadataLength = m_Config->GetInteger("SCREEN_SETTINGS:METADATA_LEN", 50);
		GetConfigPair("SCREEN_SETTINGS:METADATA_Y_RANGE", 
								&m_ScreenConfig.MetadataRangeY1, &m_ScreenConfig.MetadataRangeY2);
		m_ScreenConfig.MetadataColor = GetConfigColor("SCREEN_SETTINGS:METADATA_COLOR");
		m_ScreenConfig.MetadataTitleColor = GetConfigColor("SCREEN_SETTINGS:METADATA_TITLE_COLOR");
		m_ScreenConfig.ListsTitleColor = GetConfigColor("SCREEN_SETTINGS:LISTS_TITLE_COLOR");
		m_ScreenConfig.EntriesListColor = GetConfigColor("SCREEN_SETTINGS:ENTRIESLIST_COLOR");
		m_ScreenConfig.SelectedEntryColor = GetConfigColor("SCREEN_SETTINGS:SELECTED_ENTRY_COLOR");
		m_ScreenConfig.PlayingEntryColor = GetConfigColor("SCREEN_SETTINGS:PLAYING_ENTRY_COLOR");
		GetConfigPair("SCREEN_SETTINGS:PROGRAM_VERSION_XY", 
								&m_ScreenConfig.ProgramVersionX, &m_ScreenConfig.ProgramVersionY);
		m_ScreenConfig.ProgramVersionColor = GetConfigColor("SCREEN_SETTINGS:PROGRAM_VERSION_COLOR");
		GetConfigPair("SCREEN_SETTINGS:STREAM_OPENING_XY", 
								&m_ScreenConfig.StreamOpeningX, &m_ScreenConfig.StreamOpeningY);
		GetConfigPair("SCREEN_SETTINGS:STREAM_OPENING_ERROR_XY", 
								&m_ScreenConfig.StreamOpeningErrorX, &m_ScreenConfig.StreamOpeningErrorY);
		GetConfigPair("SCREEN_SETTINGS:STREAM_OPENING_SUCCESS_XY", 
								&m_ScreenConfig.StreamOpeningSuccessX, &m_ScreenConfig.StreamOpeningSuccessY);
		m_ScreenConfig.StreamOpeningColor = GetConfigColor("SCREEN_SETTINGS:STREAM_OPENING_COLOR");
		m_ScreenConfig.StreamOpeningErrorColor = GetConfigColor("SCREEN_SETTINGS:STREAM_OPENING_ERROR_COLOR");
		m_ScreenConfig.StreamOpeningSuccessColor = GetConfigColor("SCREEN_SETTINGS:STREAM_OPENING_SUCCESS_COLOR");
		GetConfigPair("SCREEN_SETTINGS:CLEAN_ON_NEW_STREAM_Y_RANGE", 
								&m_ScreenConfig.CleanOnNewStreamRangeY1, &m_ScreenConfig.CleanOnNewStreamRangeY2);
		GetConfigPair("SCREEN_SETTINGS:ACTIVE_COMMAND_XY", 
								&m_ScreenConfig.ActiveCommandX, &m_ScreenConfig.ActiveCommandY);
		GetConfigPair("SCREEN_SETTINGS:ERROR_MESSAGE_XY", 
								&m_ScreenConfig.ErrorMessageX, &m_ScreenConfig.ErrorMessageY);
		m_ScreenConfig.ActiveCommandColor = GetConfigColor("SCREEN_SETTINGS:ACTIVE_COMMAND_COLOR");
		m_ScreenConfig.ErrorMessageColor = GetConfigColor("SCREEN_SETTINGS:ERROR_MESSAGE_COLOR");
		GetConfigPair("SCREEN_SETTINGS:NETWORK_ENABLING_XY", 
								&m_ScreenConfig.NetworkEnablingX, &m_ScreenConfig.NetworkEnablingY);
		GetConfigPair("SCREEN_SETTINGS:NETWORK_DISABLING_XY", 
								&m_ScreenConfig.NetworkDisablingX, &m_ScreenConfig.NetworkDisablingY);
		GetConfigPair("SCREEN_SETTINGS:NETWORK_READY_XY", 
								&m_ScreenConfig.NetworkReadyX, &m_ScreenConfig.NetworkReadyY);
		m_ScreenConfig.NetworkEnablingColor = GetConfigColor("SCREEN_SETTINGS:NETWORK_ENABLING_COLOR");
		m_ScreenConfig.NetworkDisablingColor = GetConfigColor("SCREEN_SETTINGS:NETWORK_DISABLING_COLOR");
		m_ScreenConfig.NetworkReadyColor = GetConfigColor("SCREEN_SETTINGS:NETWORK_READY_COLOR");
		GetConfigPair("SCREEN_SETTINGS:CLOCK_XY", 
								&m_ScreenConfig.ClockX, &m_ScreenConfig.ClockY);
		m_ScreenConfig.ClockColor = GetConfigColor("SCREEN_SETTINGS:CLOCK_COLOR");
		GetConfigPair("SCREEN_SETTINGS:BATTERY_XY", 
								&m_ScreenConfig.BatteryX, &m_ScreenConfig.BatteryY);
		m_ScreenConfig.BatteryColor = GetConfigColor("SCREEN_SETTINGS:BATTERY_COLOR");
		GetConfigPair("SCREEN_SETTINGS:CONTAINERLIST_TITLE_XY", 
			&m_ScreenConfig.ContainerListTitleX, &m_ScreenConfig.ContainerListTitleY);
		m_ScreenConfig.ContainerListTitleUnselectedColor =
			GetConfigColor("SCREEN_SETTINGS:CONTAINERLIST_TITLE_UNSELECTED_COLOR");
		m_ScreenConfig.strContainerListTitleUnselected =
			m_Config->GetString("SCREEN_SETTINGS:CONTAINERLIST_TITLE_UNSELECTED_STRING", "List");
		m_ScreenConfig.strContainerListTitleSelected =
			m_Config->GetString("SCREEN_SETTINGS:CONTAINERLIST_TITLE_SELECTED_STRING", "*List*");
		m_ScreenConfig.ContainerListTitleSelectedColor =
			GetConfigColor("SCREEN_SETTINGS:CONTAINERLIST_TITLE_SELECTED_COLOR");
		GetConfigPair("SCREEN_SETTINGS:ENTRIESLIST_TITLE_XY", 
			&m_ScreenConfig.EntriesListTitleX, &m_ScreenConfig.EntriesListTitleY);
		m_ScreenConfig.EntriesListTitleUnselectedColor =
			GetConfigColor("SCREEN_SETTINGS:ENTRIESLIST_TITLE_UNSELECTED_COLOR");
		m_ScreenConfig.strEntriesListTitleUnselected =
			m_Config->GetString("SCREEN_SETTINGS:ENTRIESLIST_TITLE_UNSELECTED_STRING", "Entries");
		m_ScreenConfig.strEntriesListTitleSelected =
			m_Config->GetString("SCREEN_SETTINGS:ENTRIESLIST_TITLE_SELECTED_STRING", "*Entries*");
		m_ScreenConfig.EntriesListTitleSelectedColor =
			GetConfigColor("SCREEN_SETTINGS:ENTRIESLIST_TITLE_SELECTED_COLOR");
			
		GetConfigPair("SCREEN_SETTINGS:TIME_XY", 
			&m_ScreenConfig.TimeX, &m_ScreenConfig.TimeY);
		m_ScreenConfig.TimeColor = GetConfigColor("SCREEN_SETTINGS:TIME_COLOR");
		
		GetConfigPair("VISUALIZER_CONFIG:UPPER_LEFT",
			&vis_cfg.x1, &vis_cfg.y1);
		GetConfigPair("VISUALIZER_CONFIG:LOWER_RIGHT",
			&vis_cfg.x2, &vis_cfg.y2);
		m_bDisplayFPS = (bool)m_Config->GetInteger("VISUALIZER_CONFIG:DISPLAY_FPS", 0);
		/* IF not defined in screen cfg, try PSPRadio.cfg */
		if (m_bDisplayFPS == 0)
		{
			m_bDisplayFPS = m_PSPRadio->GetConfig()->GetInteger("PLUGINS:DISPLAY_FPS", 0);
		}
		m_FullscreenWait = m_PSPRadio->GetConfig()->GetInteger("PLUGINS:VISUALIZER_FULLSCREEN_WAIT", 10); /* Default = 10sec */

		m_ScreenConfig.ContainerListTitleLen = max(strlen(m_ScreenConfig.strContainerListTitleSelected), 
													strlen(m_ScreenConfig.strContainerListTitleUnselected));
		m_ScreenConfig.EntriesListTitleLen = max(strlen(m_ScreenConfig.strEntriesListTitleSelected), 
													strlen(m_ScreenConfig.strEntriesListTitleUnselected));
													

		pspradioexport_ifdata ifdata;
		memset(&ifdata, 0, sizeof(ifdata));
		if (m_Config->GetInteger("BUTTONS:USE_CUSTOMIZED_BUTTONS", 0) == 1)
		{
			ifdata.Pointer = m_Config;
			PSPRadioIF(PSPRADIOIF_SET_BUTTONMAP_CONFIG, &ifdata);
		}
		else /* Tell it to restore defaults from PSPRadio.cfg */
		{
			ifdata.Pointer = NULL;
			PSPRadioIF(PSPRADIOIF_SET_BUTTONMAP_CONFIG, &ifdata);
		}
		
		/* Set visualizer config data */
		if (vis_cfg.x1+vis_cfg.y1+vis_cfg.x2+vis_cfg.y2 > 0)
		{
			m_vis_cfg = vis_cfg;
			m_vis_cfg.sc_width = m_Screen->m_Width;
			m_vis_cfg.sc_height = m_Screen->m_Height;
			m_vis_cfg.sc_pitch = m_Screen->m_Pitch;
			m_vis_cfg.sc_pixel_format = m_Screen->m_PixelFormat;
			m_vis_cfg.fullscreen = 0;

			//memset(&ifdata, 0, sizeof(ifdata));
		}
		ifdata.Pointer = &m_vis_cfg;
		PSPRadioIF(PSPRADIOIF_SET_VISUALIZER_CONFIG, &ifdata);
		
	}
	
	TextUILog(LOG_LOWLEVEL, "LoadConfigSettings() end");
}

void CTextUI::Initialize_Screen(IScreen *Screen)
{
	TextUILog(LOG_LOWLEVEL, "Initialize screen start");

	m_CurrentScreen = (CScreenHandler::Screen)Screen->GetId();

	LoadConfigSettings(Screen);
	TextUILog(LOG_LOWLEVEL, "After LoadConfigSettings");

	////m_RenderLock->Lock(); /* Stop rendering -- after LoadConfigSettings, which also locks.*/

	m_FontEngine->SetTextMode(m_ScreenConfig.FontMode);
	m_FontEngine->SetFontSize(m_ScreenConfig.FontWidth, m_ScreenConfig.FontHeight);
	m_FontEngine->SetBackColor(m_ScreenConfig.BgColor);
	m_FontEngine->SetTextColor(m_ScreenConfig.FgColor);

	////m_RenderLock->Unlock();

	if (m_ScreenConfig.strBackground)
	{
		char strPath[MAXPATHLEN+1];
		
		/** If the background is a path, then look under PSPRadio's directory + path */
		if ((m_ScreenConfig.strBackground) && (m_ScreenConfig.strBackground[0] != 0))
		{
			if (strncmp(m_ScreenConfig.strBackground, "ms0:", strlen("ms0:")) == 0)
			{
				strlcpy(strPath, m_ScreenConfig.strBackground, MAXPATHLEN);
			}
			if (strchr(m_ScreenConfig.strBackground, '/'))
			{
				sprintf(strPath, "%s/%s", m_strCWD, m_ScreenConfig.strBackground);
			}
			else /** It's just a filename, look under the textui config directory */
			{
				sprintf(strPath, "%s/%s/%s", m_strCWD, m_strConfigDir, m_ScreenConfig.strBackground);
			}
			TextUILog(LOG_LOWLEVEL, "Calling LoadBackground '%s'", strPath);

			m_RenderLock->Lock(); /* Don't render while we load the background */
			m_Screen->LoadBuffer(BACKGROUND_BUFFER, strPath);
			m_RenderLock->Unlock(); /* Continue rendering */
		}
	}

	m_RenderLock->Lock(); /* Don't render set masks*/
	m_dirtybitmask  = 0;
	m_activebitmask = 0;
	OnBatteryChange(m_LastBatteryPercentage);
	OnTimeChange(&m_LastLocalTime);

	SET_ACTIVE_BIT(BITMASK_PCM);
	SET_ACTIVE_BIT(BITMASK_TIME);
	SET_ACTIVE_BIT(BITMASK_BATTERY);
	SET_ACTIVE_BIT(BITMASK_STREAM_TIME);
	SET_ACTIVE_BIT(BITMASK_BACKGROUND);
	SET_ACTIVE_BIT(BITMASK_MESSAGE);

	SET_DIRTY_BIT(BITMASK_BACKGROUND);

	switch(m_CurrentScreen)
	{
		case CScreenHandler::PSPRADIO_SCREEN_PLAYLIST:
		case CScreenHandler::PSPRADIO_SCREEN_SHOUTCAST_BROWSER:
		case CScreenHandler::PSPRADIO_SCREEN_LOCALFILES:
			SET_ACTIVE_BIT(BITMASK_BUFFER_PERCENTAGE);
			SET_ACTIVE_BIT(BITMASK_SONG_DATA);
			SET_ACTIVE_BIT(BITMASK_CONTAINERS);
			SET_ACTIVE_BIT(BITMASK_ELEMENTS);
			SET_ACTIVE_BIT(BITMASK_ACTIVE_COMMAND);
			SET_ACTIVE_BIT(BITMASK_CURRENT_CONTAINER_SIDE_TITLE);

			//SET_DIRTY_BIT(BITMASK_CONTAINERS);
			//SET_DIRTY_BIT(BITMASK_ELEMENTS);
			SET_DIRTY_BIT(BITMASK_CURRENT_CONTAINER_SIDE_TITLE);
			break;
		case CScreenHandler::PSPRADIO_SCREEN_OPTIONS:
		case CScreenHandler::PSPRADIO_SCREEN_OPTIONS_PLUGIN_MENU:
			SET_ACTIVE_BIT(BITMASK_OPTIONS);

			//SET_DIRTY_BIT(BITMASK_OPTIONS);
			break;
	}
	
	m_refreshbitmask = m_dirtybitmask;
	
	m_RenderLock->Unlock();
	
	TextUILog(LOG_LOWLEVEL, "Initialize screen end");
}

void CTextUI::UpdateOptionsScreen(list<OptionsScreen::Options> &OptionsList, 
										 list<OptionsScreen::Options>::iterator &CurrentOptionIterator)
{
	m_CurrentOptionIterator = 0;
	int index = 0;
	list<OptionsScreen::Options>::iterator OptionIterator;
	if (OptionsList.size() > 0)
	{
		for (OptionIterator = OptionsList.begin() ; OptionIterator != OptionsList.end() ; OptionIterator++)
		{
			if (OptionIterator == CurrentOptionIterator)
			{
				m_CurrentOptionIterator = index;
			}
			index++;
		}
	}
	m_OptionsList = OptionsList;
	SET_DIRTY_BIT(BITMASK_OPTIONS);
	m_refreshbitmask |= BITMASK_OPTIONS;
}

void CTextUI::PrintOptionsScreen(int iBuffer, bool draw_background)
{
	if (m_OptionsList.empty() == true)
		return;

	int index = 0;
	list<OptionsScreen::Options>::iterator OptionIterator;
	OptionsScreen::Options	Option;
	
	int x=-1,y=m_Config->GetInteger("SCREEN_SETTINGS:FIRST_ENTRY_Y",40),c=0xFFFFFF;
	
	for (OptionIterator = m_OptionsList.begin() ; OptionIterator != m_OptionsList.end() ; OptionIterator++)
	{
		if (index == m_CurrentOptionIterator)
		{
			c = GetConfigColor("SCREEN_SETTINGS:COLOR_OPTION_NAME_TEXT");
		}
		else
		{
			c = GetConfigColor("SCREEN_SETTINGS:COLOR_OPTION_SELECTED_NAME_TEXT");
		}
		
		Option = (*OptionIterator);
		
		if (draw_background)
			m_Screen->CopyRectangle(BACKGROUND_BUFFER, iBuffer, 
								x, 
								y,
								m_Screen->m_Width - x, 
								y + ROW_TO_PIXEL(1));
		PrintOption(iBuffer, x,y,c, Option.strName, Option.strStates, Option.iNumberOfStates, Option.iSelectedState, 
					Option.iActiveState);
		
		y+=m_Config->GetInteger("SCREEN_SETTINGS:Y_INCREMENT",16);
		index++;
	}
}

void CTextUI::PrintOption(int iBuffer, int x, int y, int c, char *strName, char *strStates[], int iNumberOfStates, int iSelectedState, int iActiveState)
{
	int x1 = -100, x2 = -100;
	int color = 0xFFFFFF;
	int iNameLen = strlen(strName);
	int iOptionLen = 0;
	int iArrowColor = 0xFFFFFF;

	GetConfigPair("SCREEN_SETTINGS:X_RANGE", &x1, &x2);
	if (x1 == -100)
	{
		x1 = 28;
	}
	
	if (x2 == -100)
	{
		x2 = 390;
	}
	int iTextPos = PIXEL_TO_COL(x1);

	///ModuleLog(LOG_LOWLEVEL, "PrintOption: x1=%d x2=%d", x1, x2);

	uiPrintf(iBuffer, COL_TO_PIXEL(iTextPos), y, c, "%s", strName);
	iTextPos += iNameLen;
	if (iNumberOfStates > 0)
	{
		uiPrintf(iBuffer, COL_TO_PIXEL(iTextPos), y, c, ": ");
		iTextPos += 2;
		int iInitState = 1;
		if (iSelectedState > 2)
		{
			iInitState = iSelectedState - 1;
			uiPrintf(iBuffer, COL_TO_PIXEL(iTextPos),y,iArrowColor, "< ");
			iTextPos += 2;
		}

		for (int iStates = iInitState; iStates < iNumberOfStates+1 ; iStates++)
		{
			if (iStates == iActiveState)
			{
				color = GetConfigColor("SCREEN_SETTINGS:COLOR_ACTIVE_STATE");
			}
			else if (iStates == iSelectedState) /** 1-based */
			{
				color = GetConfigColor("SCREEN_SETTINGS:COLOR_SELECTED_STATE");
			}
			else
			{
				color = GetConfigColor("SCREEN_SETTINGS:COLOR_NOT_SELECTED_STATE");
			}
			
			if ((iStates == iActiveState) && (iStates == iSelectedState))
			{
				color =  GetConfigColor("SCREEN_SETTINGS:COLOR_ACTIVE_AND_SELECTED_STATE");
			}
				
			iOptionLen = strlen(strStates[iStates-1]);
			if (PIXEL_TO_COL(x2) - iTextPos > iOptionLen)
			{
				uiPrintf(iBuffer, COL_TO_PIXEL(iTextPos),y,color, "%s ", strStates[iStates-1]);
				iTextPos += iOptionLen+1;
			}
			else
			{
				uiPrintf(iBuffer, COL_TO_PIXEL(iTextPos++),y,iArrowColor, ">");
				break;
			}
		}
	}	
}

void CTextUI::uiPrintf(int iBuffer, int x, int y, int color, char *strFormat, ...)
{
	va_list args;
	char msg[70*5/** 5 lines worth of text...*/];

	/** -2  = Don't Print */
	if (x != -2)

	{
		va_start (args, strFormat);         /* Initialize the argument list. */
		
		vsprintf(msg, strFormat, args);
	
		if (msg[strlen(msg)-1] == 0x0A)
			msg[strlen(msg)-1] = 0; /** Remove LF 0D*/
		if (msg[strlen(msg)-1] == 0x0D) 
			msg[strlen(msg)-1] = 0; /** Remove CR 0A*/
	
		if (x == -1) /** CENTER */
		{
			x = m_Screen->m_Width/2 - ((strlen(msg)/2)*m_FontEngine->GetFontWidth());
		}
		m_FontEngine->PrintText(m_Screen->m_Buffer[iBuffer], x, y, color, msg);
		
		va_end (args);                  /* Clean up. */
	}
}

int CTextUI::SetTitle(char *strTitle)
{
//	int x,y;
//	int c;
//	GetConfigPair("TEXT_POS:TITLE", &x, &y);
//	c = GetConfigColor("COLORS:TITLE");
//	uiPrintf(0, x,y, c, strTitle);
	
	if (m_strTitle)
	{
		free(m_strTitle);
	}
	
	m_strTitle = strdup(strTitle);
	
	return 0;
}

void CTextUI::OnBatteryChange(int Percentage)
{
	m_LastBatteryPercentage = Percentage;
	//SET_ACTIVE_BIT(BITMASK_BATTERY);
	SET_DIRTY_BIT(BITMASK_BATTERY);
}

void CTextUI::PrintBattery(int iBuffer, bool draw_background)
{
	if (draw_background)
	m_Screen->CopyRectangle(BACKGROUND_BUFFER, iBuffer, m_ScreenConfig.BatteryX, m_ScreenConfig.BatteryY,
							m_ScreenConfig.BatteryX + COL_TO_PIXEL(6), m_ScreenConfig.BatteryY + ROW_TO_PIXEL(1));
	uiPrintf(iBuffer, m_ScreenConfig.BatteryX, m_ScreenConfig.BatteryY, 
			m_ScreenConfig.BatteryColor, "B:%03d%%", m_LastBatteryPercentage);
}

void CTextUI::OnTimeChange(pspTime *LocalTime)
{
	m_LastLocalTime = *LocalTime;
	//SET_ACTIVE_BIT(BITMASK_BATTERY);
	SET_DIRTY_BIT(BITMASK_TIME);
}

void CTextUI::PrintTime(int iBuffer, bool draw_background)
{
	if (m_ScreenConfig.ClockFormat == PSP_SYSTEMPARAM_TIME_FORMAT_24HR)
	{
		if (draw_background)
			m_Screen->CopyRectangle(BACKGROUND_BUFFER, iBuffer, m_ScreenConfig.ClockX, m_ScreenConfig.ClockY,
								m_ScreenConfig.ClockX + COL_TO_PIXEL(5), m_ScreenConfig.ClockY + ROW_TO_PIXEL(1));
		uiPrintf(iBuffer, m_ScreenConfig.ClockX, m_ScreenConfig.ClockY, 
				m_ScreenConfig.ClockColor, "%02d:%02d", 
				m_LastLocalTime.hour, m_LastLocalTime.minutes);
	}
	else
	{
		bool bIsPM = (m_LastLocalTime.hour)>12;
		if (draw_background)
			m_Screen->CopyRectangle(BACKGROUND_BUFFER, iBuffer, m_ScreenConfig.ClockX, m_ScreenConfig.ClockY,
								m_ScreenConfig.ClockX + COL_TO_PIXEL(7), m_ScreenConfig.ClockY + ROW_TO_PIXEL(1));
		uiPrintf(iBuffer, m_ScreenConfig.ClockX, m_ScreenConfig.ClockY, 	
				m_ScreenConfig.ClockColor, "%02d:%02d%s", 
				bIsPM?(m_LastLocalTime.hour-12):(m_LastLocalTime.hour==0?12:m_LastLocalTime.hour),
				m_LastLocalTime.minutes,
				bIsPM?"PM":"AM");
	}
}

void CTextUI::PrintMessage(char *message)
{
	////m_RenderLock->Lock();
	//memset(m_Message, 0, 512);
	//strlcpy(m_Message, message, MAX_COL - 1);
	free(m_strMessage);
	m_strMessage = strdup(message);
	////m_RenderLock->Unlock();
	TextUILog(LOG_INFO, "PrintMessage() stored '%s' message.", m_strMessage);
	SET_DIRTY_BIT(BITMASK_MESSAGE);
}

int CTextUI::DisplayMessage_EnablingNetwork()
{
	//uiPrintf(0, m_ScreenConfig.NetworkEnablingX, m_ScreenConfig.NetworkEnablingY, m_ScreenConfig.NetworkEnablingColor, "Enabling Network");
	PrintMessage("Enabling Network");
	
	return 0;
}

int CTextUI::DisplayMessage_DisablingNetwork()
{
	//uiPrintf(0, m_ScreenConfig.NetworkDisablingX, m_ScreenConfig.NetworkDisablingY, m_ScreenConfig.NetworkDisablingColor, "Disabling Network");
	PrintMessage("Disabling Network");
	
	return 0;
}

int CTextUI::DisplayMessage_NetworkReady(char *strIP)
{
	//uiPrintf(0, m_ScreenConfig.NetworkReadyX, m_ScreenConfig.NetworkReadyY, m_ScreenConfig.NetworkReadyColor, "Ready, IP %s", strIP);
	char msg[128];
	sprintf(msg, "Ready, IP %s", strIP);
	PrintMessage(msg);
	
	return 0;
}

int CTextUI::DisplayMainCommands()
{
	return 0;
}

int CTextUI::DisplayActiveCommand(CPSPSound::pspsound_state playingstate)
{
	SET_DIRTY_BIT(BITMASK_ACTIVE_COMMAND);
	m_CurrentPlayingState = playingstate;
	switch(m_CurrentPlayingState)
	{
	case CPSPSound::STOP:
		m_IsPlaying = false;
		break;
	case CPSPSound::PLAY:
		m_TimeStartedPlaying = sceKernelLibcClock();
		m_IsPlaying = true;
		break;
	case CPSPSound::PAUSE:
		m_IsPlaying = false;
		break;
	}

	return 0;
}

void CTextUI::OnButtonReleased(int buttonmask)
{
	m_TimeStartedPlaying = sceKernelLibcClock();
}


int CTextUI::PrintActiveCommand(int iBuffer, bool draw_background)
{
	if (draw_background)
		m_Screen->CopyRectangle(BACKGROUND_BUFFER, iBuffer, 
							0, 
							m_ScreenConfig.ActiveCommandY,
							m_Screen->m_Width, 
							m_ScreenConfig.ActiveCommandY + ROW_TO_PIXEL(1));
		
	switch(m_CurrentPlayingState)
	{
	case CPSPSound::STOP:
		uiPrintf(iBuffer, m_ScreenConfig.ActiveCommandX, m_ScreenConfig.ActiveCommandY, 
				m_ScreenConfig.ActiveCommandColor, "STOP");
		break;
	case CPSPSound::PLAY:
		uiPrintf(iBuffer, m_ScreenConfig.ActiveCommandX, m_ScreenConfig.ActiveCommandY, 	
				m_ScreenConfig.ActiveCommandColor, "PLAY");
		break;
	case CPSPSound::PAUSE:
		uiPrintf(iBuffer, m_ScreenConfig.ActiveCommandX, m_ScreenConfig.ActiveCommandY, 
				m_ScreenConfig.ActiveCommandColor, "PAUSE");
		break;
	}
	
	return 0;
}

int CTextUI::DisplayErrorMessage(char *strMsg)
{
	PrintMessage(strMsg);
	
	return 0;
}

int CTextUI::DisplayMessage(char *strMsg)
{

	TextUILog(LOG_INFO, "DisplayMessage('%s') called. Calling PrintMesage()", strMsg);
	PrintMessage(strMsg);
	
	return 0;
}

int CTextUI::ClearErrorMessage()
{
	//ClearRows(m_ScreenConfig.ErrorMessageY);
	return 0;
}

int CTextUI::DisplayBufferPercentage(int iPerc)
{
	m_iBufferPercentage = iPerc;
	if (m_iBufferPercentage >= 95)
		m_iBufferPercentage = 100;
	if (m_iBufferPercentage < 2)
		m_iBufferPercentage = 0;
	SET_DIRTY_BIT(BITMASK_BUFFER_PERCENTAGE);
	return 0;
}

int CTextUI::PrintBufferPercentage(int iBuffer, bool draw_background)
{
	if (CScreenHandler::PSPRADIO_SCREEN_OPTIONS != m_CurrentScreen)
	{
		if (draw_background)
			m_Screen->CopyRectangle(BACKGROUND_BUFFER, iBuffer, 
							m_ScreenConfig.BufferPercentageX, 
							m_ScreenConfig.BufferPercentageY,
							m_ScreenConfig.BufferPercentageX + COL_TO_PIXEL(12), m_ScreenConfig.BufferPercentageY + ROW_TO_PIXEL(1));
		uiPrintf(iBuffer, m_ScreenConfig.BufferPercentageX, m_ScreenConfig.BufferPercentageY, 
				m_ScreenConfig.BufferPercentageColor, "Buffer: %03d%c", m_iBufferPercentage, 37/* 37='%'*/);
	}
	return 0;
}

int CTextUI::OnNewStreamStarted()
{
	return 0;
}

int CTextUI::OnStreamOpening()
{
	PrintMessage("Opening Stream");
	return 0;
}

int CTextUI::OnConnectionProgress()
{
	int x,y,c;
	//GetConfigPair("TEXT_POS:STREAM_OPENING", &x, &y);
	//c = GetConfigColor("COLORS:STREAM_OPENING");
	char *strIndicator[] = {"|", "/", "-", "\\"};
	static int sIndex = -1;
	x = m_ScreenConfig.StreamOpeningX;
	y = m_ScreenConfig.StreamOpeningY;
	c = m_ScreenConfig.StreamOpeningColor;

	sIndex = (sIndex+1)%4;
	
	//if (draw_background)
		m_Screen->CopyRectangle(BACKGROUND_BUFFER, OFFLINE_BUFFER, 
							x, 
							y,
							x + COL_TO_PIXEL(16), 
							y + ROW_TO_PIXEL(1));

	uiPrintf(OFFLINE_BUFFER, x, y, c, "Opening Stream %s", strIndicator[sIndex]);
	

	return 0;
}

int CTextUI::OnStreamOpeningError()
{
	PrintMessage("Error Opening Stream");
	return 0;
}

int CTextUI::OnStreamOpeningSuccess()
{
	int x,y,c;
	x = m_ScreenConfig.StreamOpeningSuccessX;
	y = m_ScreenConfig.StreamOpeningSuccessY;
	c = m_ScreenConfig.StreamOpeningSuccessColor;
	
	//ClearRows(y);
	//uiPrintf(0, x, y, c, "Stream Opened");
	return 0;
}

int CTextUI::OnNewSongData(MetaData *pData)
{
	SET_DIRTY_BIT(BITMASK_SONG_DATA);
	return 0;
}

int CTextUI::PrintSongData(int iBuffer, bool draw_background)
{
	//int r1,r2,x1;
	MetaData *pData = m_Sound->GetCurrentStream()->GetMetaData();

	if (CScreenHandler::PSPRADIO_SCREEN_OPTIONS != m_CurrentScreen)
	{

		//GetConfigPair("TEXT_POS:METADATA_ROW_RANGE", &r1, &r2);
		//x1 = m_Config->GetInteger("TEXT_POS:METADATA_START_COLUMN", 0);
		int y = m_ScreenConfig.MetadataRangeY1;
		int x = m_ScreenConfig.MetadataX1;
		int cTitle = m_ScreenConfig.MetadataTitleColor;
		int c = m_ScreenConfig.MetadataColor;
		int iLen = m_ScreenConfig.MetadataLength;
		if (draw_background)
			m_Screen->CopyRectangle(BACKGROUND_BUFFER, iBuffer, 
							m_ScreenConfig.MetadataX1, 
							y,
							m_Screen->m_Width, 
							m_ScreenConfig.MetadataRangeY2);
		//ClearRows(y, m_ScreenConfig.MetadataRangeY2);

		if (m_ScreenConfig.MetadataX1 != -2)
		{
			
			if (strlen(pData->strTitle) >= (size_t)(m_FontEngine->GetNumberOfTextColumns()-10-PIXEL_TO_COL(x)))
				pData->strTitle[m_FontEngine->GetNumberOfTextColumns()-10-PIXEL_TO_COL(x)] = 0;
				
			if (strlen(pData->strURL) >= (size_t)(m_FontEngine->GetNumberOfTextColumns()-10-PIXEL_TO_COL(x)))
				pData->strURL[m_FontEngine->GetNumberOfTextColumns()-10-PIXEL_TO_COL(x)] = 0;
			
			if (0 != pData->iSampleRate)
			{
				uiPrintf(iBuffer, x, y, cTitle, "%lukbps %dHz (%d channels) stream",
						pData->iBitRate/1000, 
						pData->iSampleRate,
						pData->iNumberOfChannels);
						//pData->strMPEGLayer);
				y+=m_FontEngine->GetFontHeight();
			}
			if (pData->strURL && strlen(pData->strURL))
			{
				uiPrintf(iBuffer, x, y, cTitle,	"URL   : ");
				uiPrintf(iBuffer, x+COL_TO_PIXEL(8), y, c,	"%-*.*s ", iLen, iLen, pData->strURL);
			}
			else
			{
				uiPrintf(iBuffer, x , y,	cTitle,	"Stream: ");
				uiPrintf(iBuffer, x+COL_TO_PIXEL(8), y,	c,		"%-*.*s ", iLen, iLen, pData->strURI);
			}
			y+=m_FontEngine->GetFontHeight();
			
			uiPrintf(iBuffer, x , y,	cTitle,	"Title : ");
			uiPrintf(iBuffer, x+COL_TO_PIXEL(8), y,	c, 	"%-*.*s ", iLen, iLen, pData->strTitle);
			y+=m_FontEngine->GetFontHeight();
			
			if (pData->strArtist && strlen(pData->strArtist))
			{
				uiPrintf(iBuffer, x , y,	cTitle,	"Artist: ");
				uiPrintf(iBuffer, x+COL_TO_PIXEL(8), y,	c, 	"%-*.*s ", iLen, iLen, pData->strArtist);
			}
		}
	}
	return 0;
}

int CTextUI::OnStreamTimeUpdate(MetaData *pData)
{
	SET_DIRTY_BIT(BITMASK_STREAM_TIME);
	m_refreshbitmask |= BITMASK_SONG_DATA;
	return 0;
}
	
int CTextUI::PrintStreamTime(int iBuffer, bool draw_background)
{

	MetaData *pData = m_Sound->GetCurrentStream()->GetMetaData();
	int y = m_ScreenConfig.TimeY;
	int x = m_ScreenConfig.TimeX;
	int c = m_ScreenConfig.TimeColor;
	
	if (pData->lTotalTime > 0)
	{
		if (draw_background)
				m_Screen->CopyRectangle(BACKGROUND_BUFFER, iBuffer, 
									x, 
									y,
									x+COL_TO_PIXEL(13), 
									y+ROW_TO_PIXEL(1));
		uiPrintf(iBuffer, x, y, c, "%02d:%02d / %02d:%02d",
					pData->lCurrentTime / 60, pData->lCurrentTime % 60,
					pData->lTotalTime / 60, pData->lTotalTime % 60);
	}
	else
	{
		if (draw_background)
				m_Screen->CopyRectangle(BACKGROUND_BUFFER, iBuffer, 
									x, 
									y,
									x+COL_TO_PIXEL(5), 
									y+ROW_TO_PIXEL(1));
		uiPrintf(iBuffer, x, y, c, "%02d:%02d",
					pData->lCurrentTime / 60, pData->lCurrentTime % 60);
	}
	return 0;
}

void CTextUI::DisplayContainers(CMetaDataContainer *Container)
{
	m_Container = Container;
	SET_DIRTY_BIT(BITMASK_CONTAINERS);
	m_refreshbitmask |= BITMASK_CONTAINERS;
}

char *basename (char *strPath)
{
        static char *pRet;

        pRet = "";

        if (strPath && strlen(strPath) <  4) /** 4 = length of 'ms0:' */
        {
                pRet = "";
        }
        else
        {
                pRet = strrchr(strPath, '/') + 1;
                if (NULL == pRet)
                {
                        pRet = "";
                }
        }

        return pRet;
}

void CTextUI::PrintContainers(int iBuffer, bool draw_background)
{
	int iColorNormal, iColorSelected, iColorTitle, iColor, iColorPlaying;
	int iNextRow = 0;
	char *strText = NULL;

	if (m_Container == NULL)
		return;

	map< string, list<MetaData>* >::iterator ListIterator;
	map< string, list<MetaData>* >::iterator *CurrentHighlightedElement = m_Container->GetCurrentContainerIterator();
	map< string, list<MetaData>* >::iterator *CurrentPlayingElement = m_Container->GetPlayingContainerIterator();
	map< string, list<MetaData>* > *List = m_Container->GetContainerList();
	iColorNormal   = m_ScreenConfig.EntriesListColor;
	iColorTitle    = m_ScreenConfig.ListsTitleColor;
	iColorSelected = m_ScreenConfig.SelectedEntryColor;
	iColor = iColorNormal;
	iColorPlaying  = m_ScreenConfig.PlayingEntryColor;
	
	bool bShowFileExtension = m_Config->GetInteger("GENERAL:SHOW_FILE_EXTENSION", 0);

//	ClearHalfRows(m_ScreenConfig.ContainerListRangeX1, m_ScreenConfig.ContainerListRangeX2, m_ScreenConfig.ContainerListRangeY1, m_ScreenConfig.ContainerListRangeY2);
	if (draw_background)
		m_Screen->CopyRectangle(BACKGROUND_BUFFER, iBuffer, 
							m_ScreenConfig.ContainerListRangeX1, 
							m_ScreenConfig.ContainerListRangeY1,
							m_ScreenConfig.ContainerListRangeX2, 
							m_ScreenConfig.ContainerListRangeY2 + ROW_TO_PIXEL(1));

	iNextRow = m_ScreenConfig.ContainerListRangeY1;
	
	strText = (char *)malloc (MAXPATHLEN+1);
	
	//TextUILog(LOG_VERYLOW, "DisplayContainers(): populating screen");
	if (List && (List->empty() == false))
	{
		//TextUILog(LOG_VERYLOW, "DisplayContainers(): Setting iterator to middle of the screen");
		ListIterator = *CurrentHighlightedElement;
		for (int i = 0; i < PIXEL_TO_ROW(m_ScreenConfig.ContainerListRangeY2-m_ScreenConfig.ContainerListRangeY1)/2; i++)
		{
			if (ListIterator == List->begin())
				break;
			ListIterator--;
		
		}

		//TextUILog(LOG_VERYLOW, "DisplayPLEntries(): elements: %d", List->size());
		//TextUILog(LOG_VERYLOW, "DisplayContainers(): Populating Screen (total elements %d)", List->size());
		for (; ListIterator != List->end() ; ListIterator++)
		{
			if (iNextRow > m_ScreenConfig.ContainerListRangeY2)
			{
				break;
			}
			
			if (ListIterator == *CurrentHighlightedElement)
			{
				iColor = iColorSelected;
			}
			else if (ListIterator == *CurrentPlayingElement)
			{
				iColor = iColorPlaying;
			}
			else
			{
				iColor = iColorNormal;
			}
			
			strlcpy(strText, ListIterator->first.c_str(), MAXPATHLEN);
			
			if (strlen(strText) > 4 && memcmp(strText, "ms0:", 4) == 0)
			{
				char *pText = basename(strText);
				if (false == bShowFileExtension)
				{
					char *ext = strrchr(pText, '.');
					if(ext)
					{
						ext[0] = 0;
					}
				}
				pText[PIXEL_TO_COL(m_ScreenConfig.ContainerListRangeX2-m_ScreenConfig.ContainerListRangeX1)] = 0;
				uiPrintf(iBuffer, m_ScreenConfig.ContainerListRangeX1, iNextRow, iColor, pText);
			}
			else
			{
				strText[PIXEL_TO_COL(m_ScreenConfig.ContainerListRangeX2-m_ScreenConfig.ContainerListRangeX1)] = 0;
				uiPrintf(iBuffer, m_ScreenConfig.ContainerListRangeX1, iNextRow, iColor, strText);
			}
			iNextRow+=m_FontEngine->GetFontHeight();
		}
	}
	
	free(strText), strText = NULL;
}



void CTextUI::DisplayElements(CMetaDataContainer *Container)
{
	m_Container = Container;
	SET_DIRTY_BIT(BITMASK_ELEMENTS);
	m_refreshbitmask |= BITMASK_ELEMENTS;
}

void CTextUI::PrintElements(int iBuffer, bool draw_background)
{
	int iNextRow;
	int iColorNormal,iColorTitle,iColorSelected, iColor, iColorPlaying;
	char *strText = NULL;

	if (m_Container == NULL)
		return;
	
	list<MetaData>::iterator ListIterator;
	list<MetaData>::iterator *CurrentHighlightedElement = m_Container->GetCurrentElementIterator();
	list<MetaData>::iterator *CurrentPlayingElement = m_Container->GetPlayingElementIterator();
	list<MetaData> *List = m_Container->GetElementList();
	iColorNormal = m_ScreenConfig.EntriesListColor;
	iColorSelected = m_ScreenConfig.SelectedEntryColor;
	iColorPlaying  = m_ScreenConfig.PlayingEntryColor;
	iColorTitle = m_ScreenConfig.ListsTitleColor;
	iColor = iColorNormal;

	bool bShowFileExtension = m_Config->GetInteger("GENERAL:SHOW_FILE_EXTENSION", 0);
	
	//ClearHalfRows(m_ScreenConfig.EntriesListRangeX1,m_ScreenConfig.EntriesListRangeX2,
	//			  m_ScreenConfig.EntriesListRangeY1,m_ScreenConfig.EntriesListRangeY2);
	if (draw_background)
		m_Screen->CopyRectangle(BACKGROUND_BUFFER, iBuffer, 
							m_ScreenConfig.EntriesListRangeX1, 
							m_ScreenConfig.EntriesListRangeY1,
							m_ScreenConfig.EntriesListRangeX2, 
							m_ScreenConfig.EntriesListRangeY2 + ROW_TO_PIXEL(1));
	
	iNextRow = m_ScreenConfig.EntriesListRangeY1;
	
	strText = (char *)malloc (MAXPATHLEN+1);
	memset(strText, 0, MAXPATHLEN);
	
	//TextUILog(LOG_VERYLOW, "DisplayPLEntries(): populating screen");
	if (List && (List->empty() == false))
	{
		ListIterator = *CurrentHighlightedElement;
		for (int i = 0; i < PIXEL_TO_ROW(m_ScreenConfig.EntriesListRangeY2-m_ScreenConfig.EntriesListRangeY1)/2; i++)
		{
			if (ListIterator == List->begin())
				break;
			ListIterator--;
		
		}
		//TextUILog(LOG_VERYLOW, "DisplayPLEntries(): elements: %d", List->size());
		for (; ListIterator != List->end() ; ListIterator++)
		{
			if (iNextRow > m_ScreenConfig.EntriesListRangeY2)
			{
				break;
			}
			
			if (ListIterator == *CurrentHighlightedElement)
			{
				iColor = iColorSelected;
			}
			else if (ListIterator == *CurrentPlayingElement)
			{
				iColor = iColorPlaying;
			}
			else
			{
				iColor = iColorNormal;
			}
			
			
			char *pText = strText;
			if (strlen((*ListIterator).strTitle))
			{
				//TextUILog(LOG_VERYLOW, "DisplayPLEntries(): Using strTitle='%s'", (*ListIterator).strTitle);
				strlcpy(strText, (*ListIterator).strTitle, MAXPATHLEN);
			}
			else
			{
				strlcpy(strText, (*ListIterator).strURI, MAXPATHLEN);
				
				if (strlen(strText) > 4 && memcmp(strText, "ms0:", 4) == 0)
				{
					pText = basename(strText);
					if (false == bShowFileExtension)
					{
						char *ext = strrchr(pText, '.');
						if(ext)
						{
							ext[0] = 0;
						}
					}
				}
			}
		
			pText[PIXEL_TO_COL(m_ScreenConfig.EntriesListRangeX2-m_ScreenConfig.EntriesListRangeX1)] = 0;
			uiPrintf(iBuffer, m_ScreenConfig.EntriesListRangeX1, iNextRow, iColor, pText);
			iNextRow+=m_FontEngine->GetFontHeight();
		}
	}
	
	free(strText), strText = NULL;
}

void CTextUI::OnCurrentContainerSideChange(CMetaDataContainer *Container)
{
	m_Container = Container;
	SET_DIRTY_BIT(BITMASK_CURRENT_CONTAINER_SIDE_TITLE);
}

void CTextUI::PrintCurrentContainerSideTitle(int iBuffer, bool draw_background)
{
	if (m_Container == NULL)
		return;

	if (draw_background)
	{
		m_Screen->CopyRectangle(BACKGROUND_BUFFER, iBuffer, 
							m_ScreenConfig.ContainerListTitleX, 
							m_ScreenConfig.ContainerListTitleY,
							m_ScreenConfig.ContainerListTitleX + COL_TO_PIXEL(m_ScreenConfig.ContainerListTitleLen), 
							m_ScreenConfig.ContainerListTitleY + ROW_TO_PIXEL(1));
		m_Screen->CopyRectangle(BACKGROUND_BUFFER, iBuffer, 
							m_ScreenConfig.EntriesListTitleX, 
							m_ScreenConfig.EntriesListTitleY,
							m_ScreenConfig.EntriesListTitleX + COL_TO_PIXEL(m_ScreenConfig.EntriesListTitleLen), 
							m_ScreenConfig.EntriesListTitleY + ROW_TO_PIXEL(1));
	}

	switch (m_Container->GetCurrentSide())
	{
		case CMetaDataContainer::CONTAINER_SIDE_CONTAINERS:
			uiPrintf(iBuffer, m_ScreenConfig.ContainerListTitleX, m_ScreenConfig.ContainerListTitleY,
					m_ScreenConfig.ContainerListTitleSelectedColor, m_ScreenConfig.strContainerListTitleSelected);
			uiPrintf(iBuffer, m_ScreenConfig.EntriesListTitleX, m_ScreenConfig.EntriesListTitleY, 
					m_ScreenConfig.EntriesListTitleUnselectedColor, m_ScreenConfig.strEntriesListTitleUnselected);
			break;
		
		case CMetaDataContainer::CONTAINER_SIDE_ELEMENTS:
			uiPrintf(iBuffer, m_ScreenConfig.ContainerListTitleX, m_ScreenConfig.ContainerListTitleY, 	
					m_ScreenConfig.ContainerListTitleUnselectedColor, m_ScreenConfig.strContainerListTitleUnselected);
			uiPrintf(iBuffer, m_ScreenConfig.EntriesListTitleX, m_ScreenConfig.EntriesListTitleY, 
					m_ScreenConfig.EntriesListTitleSelectedColor, m_ScreenConfig.strEntriesListTitleSelected);
			break;
	
	}
}
