/*
	PSPApp C++ OO Application Framework. (Initial Release: Sept. 2005)
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
#include <new>
#include <pspdisplay.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>
#include <pspkernel.h>
#include <psphprm.h>
#include <psppower.h>
#include <psprtc.h>
#include "PSPThread.h"
#include "PSPApp.h"

#undef ReportError

class CPSPApp *pPSPApp = NULL; /** Do not access / Internal Use. */

CPSPApp::CPSPApp(char *strProgramName, char *strInterfaceVersion, char *strBuildVersion)
{
	m_Exit = false;
	m_NetworkEnabled = false;
	m_strNetworkProxy = NULL;
	m_EventToPSPApp = NULL;
	m_thRun = NULL; /** Run Thread */
	memset(&m_pad, 0, sizeof (SceCtrlData));
	strcpy(m_strMyIP, "0.0.0.0");
	m_ResolverId = 0;
	m_strProgramName = strdup(strProgramName);
	m_strVersionNumber = (char *)malloc(strlen(strInterfaceVersion) + strlen(strBuildVersion) + 2);
	if (m_strVersionNumber)
	{
		sprintf(m_strVersionNumber, "%s.%s", strInterfaceVersion, strBuildVersion);
	}
	else
	{
		m_strVersionNumber = "MemoryErr";
	}
	pPSPApp = this;
	m_Polling = false;
	m_BatteryStatus = 0x00;
	m_TimeUpdate = 0;

	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

	m_Exit = false;

	m_StopKeyLatch = false;
	m_KeyComboToResumeKeyLatch = 0;
		
	m_EventToPSPApp = new CPSPEventQ("msg_to_pspapp_q");
	
	m_thCallbackSetup = new CPSPThread("update_thread", callbacksetupThread, 
										32, 4*1024, THREAD_ATTR_VFPU);
	if (m_thCallbackSetup)
	{
		m_thCallbackSetup->Start();
	}

	m_KeyHandler = new CPSPKeyHandler();
	
	InitializeNetworkDrivers();
}

int CPSPApp::StartPolling()
{
	if (NULL == m_thRun)
	{
		m_thRun = new CPSPThread("run_thread", runThread, 45, 512*1024);

		/** Start Polling for input */
		m_thRun->Start();
	}
	m_Polling = true;

	return 0;
}

int CPSPApp::StopPolling()
{
	m_Polling = false;
	sceKernelDelayThread(50*1000); /* Wait 50ms */
	return 0;
}

CPSPApp::~CPSPApp()
{
	Log(LOG_VERYLOW, "~CPSPApp(): Destructor Called.");

	Log(LOG_VERYLOW, "~CPSPApp(): Stopping Run Thread.");
	m_Exit = true;
	sceKernelDelayThread(50*1000); /* Wait 50ms */
	
	if (true == IsNetworkEnabled())
	{
		Log(LOG_VERYLOW, "~CPSPApp(): Disabling Network.");
		DisableNetwork();
	}

	Log(LOG_VERYLOW, "~CPSPApp(): Stopping network drivers");
	StopNetworkDrivers();

	if (m_EventToPSPApp)
	{
		Log(LOG_VERYLOW, "~CPSPApp(): deleting eventtopspapp");
		delete(m_EventToPSPApp), m_EventToPSPApp = NULL;
	}
	
	if (m_KeyHandler)
	{
		Log(LOG_VERYLOW, "~CPSPApp(): deleting m_KeyHandler");
		delete(m_KeyHandler), m_KeyHandler = NULL;
	}

	if (m_strNetworkProxy)
	{	
		Log(LOG_VERYLOW, "~CPSPApp(): freeing m_strNetworkProxy");
		free(m_strNetworkProxy), m_strNetworkProxy = NULL;
	}

	Log(LOG_VERYLOW, "~CPSPApp(): freeing program name.");
	free(m_strProgramName);
	Log(LOG_VERYLOW, "~CPSPApp(): freeing version number.");
	free(m_strVersionNumber);

	Log(LOG_INFO, "~CPSPApp(): Bye!.");

	sceKernelExitGame();

	return;
}

int CPSPApp::StopKeyLatch(unsigned int key_combo_to_resume)
{
	m_StopKeyLatch = true;
	m_KeyComboToResumeKeyLatch = key_combo_to_resume;
	return 0;
}

int CPSPApp::StartKeyLatch()
{
	m_StopKeyLatch = false;
	m_KeyComboToResumeKeyLatch = 0;
	return 0;
}

/** This is a thread */
int CPSPApp::Run()
{
	u32 hprmlatch = 0;
	CPSPKeyHandler::KeyEvent event;
	int newBatteryStatus;
	u16 oldMinute;
	bool bHPRMAvailable = true;

	if (sceKernelDevkitVersion() == 0x02060010)
	{
		Log(LOG_INFO, "Run(): Firmware 2.6 detected, disabling HPRM polling.");
		bHPRMAvailable = false;
	}
		
	Log(LOG_INFO, "Run(): Going into main loop.");
	while (false == m_Exit)
	{
		sceDisplayWaitVblankStart();

		if (false == m_Polling)
			continue;

		if (m_StopKeyLatch == true)
		{
			SceCtrlData pad;
		
			sceCtrlPeekBufferPositive(&pad, 1);
			
			/** Key combo done */
			if ( (pad.Buttons & m_KeyComboToResumeKeyLatch) == m_KeyComboToResumeKeyLatch )
			{
				m_StopKeyLatch = false;
				SendEvent(MID_KEY_LATCH_ENABLED_WITH_KEY_COMBO, NULL);
			}
		}
		else
		{
			// If a key event was detected notify the application
			if (m_KeyHandler && (m_KeyHandler->KeyHandler(event) != false))
			{
				/** Note that the state is sent "by value" in a pointer holder. 
				(It has to be sent by-value, as event can become out of scope when the event (which is async) handles it)*/
				SendEvent(event.event, (void *)(event.key_state)); 
				
			}
		}
		// Only read from the RTC once a second
		if (m_TimeUpdate++ == 60)
		{
			m_TimeUpdate = 0;
			oldMinute = m_LocalTime.minutes;

			// Read from the RTC (if possible). return zero on success
			if (!sceRtcGetCurrentClockLocalTime(&m_LocalTime))
			{
				// Only update each minute
				if (oldMinute != m_LocalTime.minutes)
				{
					/**the localtime can be sent safely by reference, as it is a member variable */
					SendEvent(MID_ONTIME_CHANGE, &m_LocalTime);

					// Check to see if the battery status has changed
					newBatteryStatus = scePowerGetBatteryLifePercent();
					if (newBatteryStatus != m_BatteryStatus)
					{
						m_BatteryStatus = newBatteryStatus;
						/**the battery status can be sent safely by reference, as it is a member variable */
						SendEvent(MID_ONBATTERY_CHANGE, &m_BatteryStatus);
					}
				}
			}
		}
/*
		if (oldAnalogue != (short)m_pad.Lx)
		{
			oldAnalogue = (short)m_pad.Lx;
			OnAnalogueStickChange(m_pad.Lx, m_pad.Ly);
		}
*/
		
		if (bHPRMAvailable == true)
		{
			if (sceHprmIsRemoteExist())
			{
				sceHprmReadLatch(&hprmlatch);
				if (hprmlatch != 0x00)
				{
					/**hprm latch needs to be sent by value, if sent by reference, then it can lose scope before it
					can be handled -- this because events are asynchronous */
					SendEvent(MID_ONHPRM_RELEASED, (void*)hprmlatch);
					Log(LOG_VERYLOW, "HPRM latch = %04x\n", hprmlatch);
				}
			}
		}
	}

	Log(LOG_VERYLOW, "Run:: Right before calling sceKernelExitThread.");
	sceKernelExitThread(0);
	return 0;
}

/** When the user selects to exit, this is called */
int CPSPApp::OnAppExit(int arg1, int arg2, void *common)
{
	/** We set m_Exit to true. This causes Run()'s loop to end, and start a controlled shutdown. */
	m_Exit = true;
	/** This notifies The APP to start destruction */
	SendEvent(MID_PSPAPP_EXITING);
	return 0;
}

int CPSPApp::ReportError(char *format, ...)
{
	char *strMessage;
	va_list args;

	/** The pointer needs to be freed when the event is received */
	strMessage   = (char *)malloc(4096);

	va_start (args, format);         /* Initialize the argument list. */

	vsprintf(strMessage, format, args);

	va_end (args);

	return SendEvent(MID_ERROR, strMessage);
}

/* ---statics--- */
/* System Callbacks */
int CPSPApp::exitCallback(int arg1, int arg2, void *common)
{
	//return pPSPApp->OnAppExit(arg1, arg2, common);
	sceKernelExitGame();
	return 0;
}

int CPSPApp::powerCallback(int arg1, int pwrflags, void *common)
{
	pPSPApp->OnPowerEvent(pwrflags);

	if (pwrflags & PSP_POWER_CB_RESUME_COMPLETE)
		pPSPApp->SendEvent(MID_POWER_EVENT_RESUME_COMPLETE, NULL);

	/** Register again (or it won't happen anymore) */
    int cbid = sceKernelCreateCallback("Power Callback", powerCallback, NULL);
    scePowerRegisterCallback(0, cbid);
	return 0;
}

int CPSPApp::callbacksetupThread(SceSize args, void *argp)
{
	int cbid;
	cbid = sceKernelCreateCallback("Exit Callback", exitCallback, NULL);
	sceKernelRegisterExitCallback(cbid);
    cbid = sceKernelCreateCallback("Power Callback", powerCallback, NULL);
    scePowerRegisterCallback(0, cbid);

	sceKernelSleepThreadCB();

	return 0;
}

int CPSPApp::runThread(SceSize args, void *argp)
{
	return pPSPApp->Run();
}

