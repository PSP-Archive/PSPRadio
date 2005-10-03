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
#include <stdio.h>
#include <unistd.h> 
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <malloc.h>
#include <Tools.h>
#include <PSPApp.h>
#include <Logging.h>
#include "PlayList.h"

#define ReportError pPSPApp->ReportError

#define PLV2_NUMBER_OF_ENTRIES_TAG			"numberofentries="
#define PLV2_NUMBER_OF_ENTRIES_PARSING_STR	"numberofentries=%d"
#define PLV2_FILE_TAG						"FileX="
#define PLV2_FILE_PARSING_STR				"File%d=%s"
#define PLV2_TITLE_TAG						"TitleX="
#define PLV2_TITLE_PARSING_STR				"Title%d=%s"
#define PLV2_LENGTH_TAG						"LengthX="
#define PLV2_LENGTH_PARSING_STR				"Length%d=%d"

CPlayList::CPlayList()
{
	m_songiterator = m_playlist.begin();
};

CPlayList::~CPlayList()
{
	Clear();
};

void CPlayList::Next()
{
	m_songiterator++;
	if (m_songiterator == m_playlist.end())
	{
		m_songiterator = m_playlist.begin();
	}
}

void CPlayList::Prev()
{
	if (m_songiterator == m_playlist.begin())
	{
		m_songiterator = m_playlist.end();
	}
	m_songiterator--;
}

int CPlayList::GetNumberOfSongs()
{
	return m_playlist.size();
}

int CPlayList::GetCurrentSong(songmetadata *pData)
{ 
	if (m_playlist.size())
	{
		*pData = *m_songiterator;
		return 0;
	}
	else
	{
		return -1;
	}
}

void CPlayList::InsertFile(char *strFileName)
{
	songmetadata songdata;
	
	Log(LOG_INFO, "Adding '%s' to the list.", strFileName);
	memset(&songdata, 0, sizeof(songdata));
	strncpy(songdata.strFileName, strFileName, 256);
	m_playlist.push_back(songdata);
	
	m_songiterator = m_playlist.begin();
}

void CPlayList::LoadPlayListFile(char *strFileName)
{
	FILE *fd = NULL;
	char strLine[256];
	int iLines = 0;
	int iFormatVersion = 1;
	songmetadata songdata;
	bool fStopParsing = false;
	
	int iV2_numberofentries = 0;
	int iV2_IgnoredValue = 0; /** Used during parsing */
	int iV2_ParsingTemp = 0;
	char strV2_File[256];
	char strV2_Title[256];
	int  iV2_Length = -1;
	
	enum playlistv2_states
	{
		WAITING_FOR_NUM_OF_ENTRIES,
		WAITING_FOR_FILE,
		WAITING_FOR_TITLE,
		WAITING_FOR_LENGTH,
	} v2_state = WAITING_FOR_FILE;//WAITING_FOR_NUM_OF_ENTRIES;
	
	fd = fopen(strFileName, "r");
	
	if(fd != NULL)
	{
		while ( (!feof(fd)) && (false == fStopParsing) )
		{
			strLine[0] = 0;
			fgets(strLine, 256, fd);
			if (strlen(strLine) == 0 || strLine[0] == '\r' || strLine[0] == '\n')
			{
				continue;
			}
			if ((iLines == 0) && (strLine[0] == '['))
			{
				iFormatVersion = 2;
				
				continue;
			}
			if (strLine[0] == ';')
			{
				/** A comment!, ignore */
				continue;
			}
			
			strLine[strlen(strLine)-1] = 0; /** Remove LF 0D*/
			if (strLine[strlen(strLine)-1] == 0x0D) 
				strLine[strlen(strLine)-1] = 0; /** Remove CR 0A*/
			
			/** We have a line with data here */
			
			switch(iFormatVersion)
			{
				case 1:
					memset(&songdata, 0, sizeof(songmetadata));
					memcpy(songdata.strFileName, strLine, 256);
					m_playlist.push_back(songdata);
					Log(LOG_INFO, "Adding '%s' to the list.", strLine);
					break;
				case 2:
					if ( ('n' == strLine[0]) || ('N' == strLine[0]) ) /** Number of entry entry */
						continue;
					if ( ('v' == strLine[0]) || ('V' == strLine[0]) ) /** Version entry */
						continue;
					//Log(LOG_VERYLOW, "Line=%d strLine='%s' v2_state=%d", iLines, strLine, v2_state);
					switch(v2_state)
					{
						case WAITING_FOR_NUM_OF_ENTRIES: /* ie numberofentries=5 */
							if (strlen(strLine) > strlen(PLV2_NUMBER_OF_ENTRIES_TAG))
							{
								iV2_ParsingTemp = sscanf(strLine, PLV2_NUMBER_OF_ENTRIES_PARSING_STR, &iV2_numberofentries);
								if (1 == iV2_ParsingTemp)
								{
									v2_state = WAITING_FOR_FILE;
								}
								else
								{
									fStopParsing = true;
								}
							}
							else
							{
								fStopParsing = true;
							}
							break;
						case WAITING_FOR_FILE: /* ie File1=http://64.236.34.196:80/stream/1040 */
							if (strlen(strLine) > strlen(PLV2_FILE_TAG))
							{
								strV2_File[0] = 0;
								iV2_ParsingTemp = sscanf(strLine, PLV2_FILE_PARSING_STR, &iV2_IgnoredValue, strV2_File);
								if (2 == iV2_ParsingTemp)
								{
									v2_state = WAITING_FOR_TITLE;
								}
								else
								{
									fStopParsing = true;
								}
							}
							else
							{
								fStopParsing = true;
							}
							break;
						case WAITING_FOR_TITLE: /* ie Title1=(#1 - 524/21672) CLUB 977 The 80s Channel */
							if (strlen(strLine) > strlen(PLV2_TITLE_TAG))
							{
								//iV2_ParsingTemp = sscanf(strLine, PLV2_TITLE_PARSING_STR, &iV2_IgnoredValue, strV2_Title);
								strV2_Title[0] = 0;
								if (strchr(strLine, '='))
								{
									strncpy(strV2_Title, strchr(strLine, '=') + 1, 256);
								}
								if (strlen(strV2_Title))
								{
									v2_state = WAITING_FOR_LENGTH;
								}
								else
								{
									fStopParsing = true;
								}
							}
							else
							{
								fStopParsing = true;
							}
							break;
						case WAITING_FOR_LENGTH: /* Length1=-1 */
							if (strlen(strLine) > strlen(PLV2_LENGTH_TAG))
							{
								iV2_ParsingTemp = sscanf(strLine, PLV2_LENGTH_PARSING_STR, &iV2_IgnoredValue, &iV2_Length);
								if (2 == iV2_ParsingTemp)
								{
									/** Good!, all fields for this entry aquired, let's insert in the list! */
									memset(&songdata, 0, sizeof(songmetadata));
									Log(LOG_INFO, "Adding V2 Entry: File='%s' Title='%s' Length='%i' to the list.", 
										strV2_File, strV2_Title, iV2_Length);
									memcpy(songdata.strFileName,  strV2_File,  256);
									memcpy(songdata.strFileTitle, strV2_Title, 256);
									songdata.iLength = iV2_Length;
									m_playlist.push_back(songdata);
									//Log(LOG_INFO, "Added V2 Entry: File='%s' Title='%s' Length='%s' to the list.", 
									//	strV2_File, strV2_Title, iV2_Length);
									iV2_numberofentries--;
									
									v2_state = WAITING_FOR_FILE;
								}
								else
								{
									fStopParsing = true;
								}
							}
							else
							{
								fStopParsing = true;
							}
							break;
					}
					if ( (true == fStopParsing) )//&& (iV2_numberofentries > 0) )
					{
						Log(LOG_ERROR, "Malformed playlist found. Error on Line %d (%s)", iLines+1, strLine);
					}
					break;
			}
			
			//if ( (2 == iFormatVersion) && (0 == iV2_numberofentries) ) /** Done! */
			//{
			//	break;
			//}
			
			iLines++;
		}
		fclose(fd), fd = NULL;
		
		m_songiterator = m_playlist.begin();
	}
	else
	{
		ReportError("Unable to open playlist '%s'", strFileName);
	}
}

void CPlayList::Clear()
{
	while(m_playlist.size())
	{
		m_playlist.pop_front();
	}
	m_songiterator = m_playlist.begin();
};