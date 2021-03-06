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
#include <PSPApp.h>
#include <PSPSound.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <malloc.h>
#include <iniparser.h>
#include <Logging.h>
#include <pspwlan.h>
#include <psppower.h>
#include "PRXLoader.h"
#include <UI_Interface.h>
#include "SHOUTcastScreen.h"
#include "LocalFilesScreen.h"
#include "OptionsPluginMenuScreen.h"
#include "PSPRadio.h"
#include "Main.h"

#define ReportError pPSPApp->ReportError

#include <APP_Exports.h>

enum OptionIDs
{
	OPTION_ID_UI,
	OPTION_ID_FSS,
	OPTION_ID_APP,
	OPTION_ID_GAME,
	OPTION_ID_VIS,
	OPTION_ID_VIS_FS_WAIT,
};

OptionsPluginMenuScreen::Options OptionsPluginMenuData[] =
{
		/* ID						Option Name					Option State List			(active,selected,number-of)-states */
	{	OPTION_ID_UI,				"User Interface",			{""},				0,0,0		},
	{	OPTION_ID_FSS,				"FileSystemServers",		{"Off"},			1,1,1		},
	{	OPTION_ID_APP,				"Applications",				{"Off"},			1,1,1		},
	{	OPTION_ID_GAME,				"Games",					{"Off"},			1,1,1		},
	{	OPTION_ID_VIS,				"Visualizations",			{""},				0,0,0		},
	{	OPTION_ID_VIS_FS_WAIT,		"Vis Fullscreen Wait",		{"Off", "5", "10", "30"},	3,3,4	},
	

	{  -1,  						"",							{""},				0,0,0		}
};

OptionsPluginMenuScreen::OptionsPluginMenuScreen(int Id, CScreenHandler *ScreenHandler):OptionsScreen(Id, ScreenHandler)
{
	Log(LOG_LOWLEVEL, "OptionsPluginMenuScreen(): Id=%d", Id);
	//LoadFromConfig();
}

OptionsPluginMenuScreen::~OptionsPluginMenuScreen()
{
}


/** Activate() is called on screen activation */
void OptionsPluginMenuScreen::Activate()
{
	IScreen::Activate();

	// Update.  This is necesary the first time */
	UpdateOptionsData();
	gPSPRadio->m_UI->UpdateOptionsScreen(m_OptionsList, m_CurrentOptionIterator);
}

void OptionsPluginMenuScreen::LoadFromConfig()
{
	Log(LOG_INFO, "LoadFromConfig");
}

void OptionsPluginMenuScreen::SaveToConfigFile()
{
}

/** This populates and updates the option data */
void OptionsPluginMenuScreen::UpdateOptionsData()
{
	Options Option;

	list<Options>::iterator		OptionIterator;

	while(m_OptionsList.size())
	{
		// Release allocated memory
		OptionIterator = m_OptionsList.begin();
 		if (    ((*OptionIterator).Id == OPTION_ID_UI)
			 || ((*OptionIterator).Id == OPTION_ID_FSS)
			 || ((*OptionIterator).Id == OPTION_ID_APP)
			 || ((*OptionIterator).Id == OPTION_ID_GAME)
			 || ((*OptionIterator).Id == OPTION_ID_VIS)
			)
 		{
 			for (int i = 0; i < (*OptionIterator).iNumberOfStates; i++)
 			{
 				free((*OptionIterator).strStates[i]);
 			}
 		}
		m_OptionsList.pop_front();
	}


	for (int iOptNo=0;; iOptNo++)
	{
		if (-1 == OptionsPluginMenuData[iOptNo].Id)
			break;

		/* Make a copy of the table entry */
		Option.Id = OptionsPluginMenuData[iOptNo].Id;
		sprintf(Option.strName, 	OptionsPluginMenuData[iOptNo].strName);
		memcpy(Option.strStates, OptionsPluginMenuData[iOptNo].strStates,
				sizeof(char*)*OptionsPluginMenuData[iOptNo].iNumberOfStates);
		Option.iActiveState		= OptionsPluginMenuData[iOptNo].iActiveState;
		Option.iSelectedState	= OptionsPluginMenuData[iOptNo].iSelectedState;
		Option.iNumberOfStates	= OptionsPluginMenuData[iOptNo].iNumberOfStates;

		/** Modify from data table */
		switch(iOptNo)
		{
			case OPTION_ID_UI:
				RetrievePlugins(/*option*/Option, /*prefix*/"UI_",
								m_ScreenHandler->GetCurrentUIName());
				Option.iSelectedState = Option.iActiveState;
				break;
			case OPTION_ID_FSS:
				RetrievePlugins(/*option*/Option, /*prefix*/"FSS_",
								gPSPRadio->GetActivePluginName(PLUGIN_FSS), /*insert 'off'*/true);
				Option.iSelectedState = Option.iActiveState;
				break;
			case OPTION_ID_APP:
				RetrievePlugins(/*option*/Option, /*prefix*/"APP_",
								gPSPRadio->GetActivePluginName(PLUGIN_APP), /*insert 'off'*/true);
				Option.iSelectedState = Option.iActiveState;
				break;
			case OPTION_ID_GAME:
				RetrievePlugins(/*option*/Option, /*prefix*/"GAME_",
								gPSPRadio->GetActivePluginName(PLUGIN_GAME), /*insert 'off'*/true);
				Option.iSelectedState = Option.iActiveState;
				break;
			case OPTION_ID_VIS:
				RetrievePlugins(/*option*/Option, /*prefix*/"VIS_",
								gPSPRadio->GetActivePluginName(PLUGIN_VIS), /*insert 'off'*/true);
				Option.iSelectedState = Option.iActiveState;
				break;
			case OPTION_ID_VIS_FS_WAIT:
				{
					int iVal = gPSPRadio->GetConfig()->GetInteger("PLUGINS:VISUALIZER_FULLSCREEN_WAIT", 10);
					if (iVal < 5)
						Option.iActiveState = 1;
					else if(iVal >=5 && iVal < 10)
						Option.iActiveState = 2;
					else if(iVal >=10 && iVal < 30)
						Option.iActiveState = 3;
					else if(iVal >=30)
						Option.iActiveState = 4;

					Option.iSelectedState = Option.iActiveState;
				}
				break;
		}

		m_OptionsList.push_back(Option);
	}

	m_CurrentOptionIterator = m_OptionsList.begin();

}

int OptionsPluginMenuScreen::RetrievePlugins(Options &Option, char *strPrefix, char *strActive, bool bInsertOff)
{
	int dfd = 0;
	char *strPath = m_ScreenHandler->GetCWD();
	int iNumberOfPluginsFound = 0;
	char *strPlugin = NULL;
	SceIoDirent direntry;

	if (bInsertOff == true)
	{
		Option.strStates[0] = strdup(PLUGIN_OFF_STRING);
		iNumberOfPluginsFound++;
	}

	Log(LOG_LOWLEVEL, "RetrievePlugins: Reading '%s' Directory, looking for '%s' files...", strPath, strPrefix);
	dfd = sceIoDopen(strPath);

	Option.iActiveState = 1; /* Initial value */

	/** Get all files */
	if (dfd >= 0)
	{
		/** RC 10-10-2005: The direntry has to be memset! Or else the app will/may crash! */
		memset(&direntry, 0, sizeof(SceIoDirent));
		while(sceIoDread(dfd, &direntry) > 0)
		{
			if(!(direntry.d_stat.st_attr & FIO_SO_IFDIR)) /** It's a file -- FIO_SO_IFREG doesn't work all the time?*/
			{
				if (strcmp(direntry.d_name, ".") == 0)
					continue;
				else if (strcmp(direntry.d_name, "..") == 0)
					continue;

				Log(LOG_LOWLEVEL, "Processing '%s'", direntry.d_name);
				if (strncmp(direntry.d_name, strPrefix, strlen(strPrefix)) == 0)
				{
					Log(LOG_LOWLEVEL, "RetrievePlugins(): Adding '%s' to list. Found %d elements",
						direntry.d_name, iNumberOfPluginsFound+1);

					strPlugin = (char*)malloc(128);
					if (strPlugin)
					{
						char strFormat[64];
						sprintf(strFormat, "%s%%s", strPrefix);

						sscanf(direntry.d_name, strFormat, strPlugin);

						if (strrchr(strPlugin, '.'))
							*strrchr(strPlugin, '.') = 0;
						Log(LOG_LOWLEVEL, "direntry='%s' strPlugin='%s' strActive='%s'", direntry.d_name, strPlugin, strActive);

						Option.strStates[iNumberOfPluginsFound] = strPlugin;

						if (strcmp(direntry.d_name, strActive) == 0)
						{
							Option.iActiveState = iNumberOfPluginsFound + 1;
						}

						iNumberOfPluginsFound++;
					}
					else
					{
						Log(LOG_ERROR, "RetrievePlugins: Memory error!");
						break;
					}
				}
			}
		}
		Option.iNumberOfStates = iNumberOfPluginsFound;
		sceIoDclose(dfd);
	}
	else
	{
		Log(LOG_ERROR, "Retrieve Plugins: Unable to open '%s' Directory! (Error=0x%x)", strPath, dfd);
	}

	return iNumberOfPluginsFound;
}

void OptionsPluginMenuScreen::OnOptionActivation()
{
	bool fOptionActivated = false;
	int iSelectionBase0 = (*m_CurrentOptionIterator).iSelectedState - 1;
	char *strSelection  = (*m_CurrentOptionIterator).strStates[iSelectionBase0];
	char strPluginRealName[128];

	switch ((*m_CurrentOptionIterator).Id)
	{
		case OPTION_ID_UI:
			if ((*m_CurrentOptionIterator).iSelectedState != (*m_CurrentOptionIterator).iActiveState)
			{
				m_ScreenHandler->GetSound()->Stop(); /** Stop stream if playing */

				sprintf(strPluginRealName, "UI_%s.prx", strSelection);
				Log(LOG_INFO, "User selected to load '%s'", strPluginRealName);
				gPSPRadio->m_UI->DisplayMessage("Starting Plugin . . .");

				m_ScreenHandler->StartUI(strPluginRealName, DEFAULT_SKIN);

				gPSPRadio->m_UI->DisplayMessage("Plugin Started");
				fOptionActivated = true;
			}
			break;
		case OPTION_ID_FSS:
			if ((*m_CurrentOptionIterator).iSelectedState != (*m_CurrentOptionIterator).iActiveState)
			{
				sprintf(strPluginRealName, "FSS_%s.prx", strSelection);
				Log(LOG_INFO, "User selected FSS Plugin '%s'.", strPluginRealName);
				if (iSelectionBase0 > 0)
				{
					gPSPRadio->m_UI->DisplayMessage("Starting Plugin . . .");
					//sprintf(gPSPRadio->m_UI->buff, "original data");
					//Log(LOG_INFO, "ui->buff before loading plugin='%s'", gPSPRadio->m_UI->buff);
					int res = gPSPRadio->LoadPlugin(strPluginRealName, PLUGIN_FSS);
					//Log(LOG_INFO, "ui->buff after loading plugin='%s'", gPSPRadio->m_UI->buff);
					switch(res)
					{
						case 0:
							fOptionActivated = true;
							gPSPRadio->m_UI->DisplayMessage("Plugin Started");
							break;
						default:
							gPSPRadio->m_UI->DisplayMessage("Error Starting Plugin . . .");
							break;
					}
				}
				else
				{
					gPSPRadio->m_UI->DisplayMessage("Stopping Plugin. . .");
					gPSPRadio->UnloadPlugin(PLUGIN_FSS);
					fOptionActivated = true;
					gPSPRadio->m_UI->DisplayMessage("Plugin Stopped");
				}
			}
			break;
		case OPTION_ID_APP:
			if ((*m_CurrentOptionIterator).iSelectedState != (*m_CurrentOptionIterator).iActiveState)
			{
				sprintf(strPluginRealName, "APP_%s.prx", strSelection);
				Log(LOG_INFO, "User selected APP Plugin '%s'.", strPluginRealName);
				if (iSelectionBase0 > 0)
				{
					gPSPRadio->m_UI->DisplayMessage("Starting Plugin . . .");
					//sprintf(gPSPRadio->m_UI->buff, "original data");
					//Log(LOG_INFO, "ui->buff before loading plugin='%s'", gPSPRadio->m_UI->buff);
					u32 res = gPSPRadio->LoadPlugin(strPluginRealName, PLUGIN_APP);
					//Log(LOG_INFO, "ui->buff after loading plugin='%s'", gPSPRadio->m_UI->buff);
					if (res == 0)
					{
						fOptionActivated = true;
						gPSPRadio->m_UI->DisplayMessage("Plugin Started");
					}
					else
					{
						if (res == SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED)
						{
							gPSPRadio->m_UI->DisplayMessage("Not Enough Free Memory To Start Plugin. . .");
						}
						else
						{
							gPSPRadio->m_UI->DisplayMessage("Error Starting Plugin . . .");
						}
					}
				}
				else
				{
					gPSPRadio->m_UI->DisplayMessage("Stopping Plugin. . .");
					gPSPRadio->UnloadPlugin(PLUGIN_APP);
					fOptionActivated = true;
					gPSPRadio->m_UI->DisplayMessage("Plugin Stopped");
				}
			}
			else
			{
				if (iSelectionBase0 > 0)
				{
					gPSPRadio->m_UI->DisplayMessage("Continuing Plugin. . .");
					ModuleContinueApp();
				}
			}
			break;
		case OPTION_ID_GAME:
			if ((*m_CurrentOptionIterator).iSelectedState != (*m_CurrentOptionIterator).iActiveState)
			{
				sprintf(strPluginRealName, "GAME_%s.prx", strSelection);
				Log(LOG_INFO, "User selected GAME Plugin '%s'.", strPluginRealName);
				if (iSelectionBase0 > 0)
				{
					gPSPRadio->m_UI->DisplayMessage("Starting Plugin . . .");
					u32 res = gPSPRadio->LoadPlugin(strPluginRealName, PLUGIN_GAME);
					if (res == 0)
					{
						fOptionActivated = true;
						gPSPRadio->m_UI->DisplayMessage("Plugin Started");
					}
					else
					{
						if (res == SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED)
						{
							gPSPRadio->m_UI->DisplayMessage("Not Enough Free Memory To Start Plugin. . .");
						}
						else
						{
							gPSPRadio->m_UI->DisplayMessage("Error Starting Plugin . . .");
						}
					}
				}
				else
				{
					gPSPRadio->m_UI->DisplayMessage("Stopping Plugin. . .");
					gPSPRadio->UnloadPlugin(PLUGIN_GAME);
					fOptionActivated = true;
					gPSPRadio->m_UI->DisplayMessage("Plugin Stopped");
				}
			}
			break;
		case OPTION_ID_VIS:
			if ((*m_CurrentOptionIterator).iSelectedState != (*m_CurrentOptionIterator).iActiveState)
			{
				sprintf(strPluginRealName, "VIS_%s.prx", strSelection);
				Log(LOG_INFO, "User selected VISUALIZER Plugin '%s'.", strPluginRealName);
				if (iSelectionBase0 > 0)
				{
					gPSPRadio->m_UI->DisplayMessage("Starting Plugin . . .");
					u32 res = gPSPRadio->LoadPlugin(strPluginRealName, PLUGIN_VIS);
					switch(res)
					{
						case 0:
							fOptionActivated = true;
							gPSPRadio->m_UI->DisplayMessage("Plugin Started");
							break;
						case -2:
							gPSPRadio->m_UI->DisplayMessage("Plugin not Supported");
							break;
						case SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED:
							gPSPRadio->m_UI->DisplayMessage("Not Enough Free Memory To Start Plugin. . .");
							break;
						default:
							gPSPRadio->m_UI->DisplayMessage("Error Starting Plugin . . .");
							break;
					}
				}
				else
				{
					gPSPRadio->m_UI->DisplayMessage("Stopping Plugin. . .");
					gPSPRadio->UnloadPlugin(PLUGIN_VIS);
					fOptionActivated = true;
					gPSPRadio->m_UI->DisplayMessage("Plugin Stopped");
				}
			}
			break;
		case OPTION_ID_VIS_FS_WAIT:
			if ((*m_CurrentOptionIterator).iSelectedState != (*m_CurrentOptionIterator).iActiveState)
			{
				int iVal = 0;
				if (iSelectionBase0 > 0)
				{
					sscanf(strSelection, "%d", &iVal);
				}
				gPSPRadio->GetConfig()->SetInteger("PLUGINS:VISUALIZER_FULLSCREEN_WAIT", iVal);
				fOptionActivated = true;
				//gPSPRadio->m_UI->DisplayMessage("Changed");
			}
			break;
	}

	if (true == fOptionActivated)
	{
		(*m_CurrentOptionIterator).iActiveState = (*m_CurrentOptionIterator).iSelectedState;
		gPSPRadio->m_UI->UpdateOptionsScreen(m_OptionsList, m_CurrentOptionIterator);
	}
}

