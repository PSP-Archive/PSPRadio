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
#include <list>
#include <PSPApp.h>
#include <stdio.h>
#include <unistd.h> 
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <mad.h>
#include <malloc.h>
#include <errno.h>
#include <sys/socket.h>
#include "PSPSound.h"
#include "PSPStream.h"
#include "ID3Parser.h"

#define METADATA_STREAMURL_TAG "StreamUrl='"
#define METADATA_STREAMTITLE_TAG "StreamTitle='"

/** class CPSPStream */
CPSPStream::CPSPStream()
{
	m_Type = STREAM_TYPE_NONE;
	m_State = STREAM_STATE_CLOSED;
	m_pfd = NULL;
	m_fdSocket = -1;
	m_MetaData = new MetaData;
	ClearMetadata();
	
}

CPSPStream::~CPSPStream()
{
	if (m_MetaData)
	{
		delete(m_MetaData);
	}
}

void CPSPStream::ClearMetadata()
{
	m_MetaData->Clear();
}

/** Accessors */
void CPSPStream::SetURI(char *strFile)
{
	if (strFile)
	{
		size_t length = strlen(strFile);
		if (length > 4)
		{
			/** New URI, clear Metadata */
			ClearMetadata();
			strlcpy(m_MetaData->strURI, strFile, MAXPATHLEN);
			if ((length > strlen("http://")) && (memcmp(m_MetaData->strURI, "http://", strlen("http://")) == 0))
			{
				Log(LOG_LOWLEVEL, "CPSPStream::SetURI(%s) <URL> called", m_MetaData->strURI);
				m_Type = STREAM_TYPE_URL;
			}
			else // It's a file!
			{
				Log(LOG_LOWLEVEL, "CPSPStream::SetURI(%s) <FILE> called", m_MetaData->strURI);
				m_Type = STREAM_TYPE_FILE;
			}
		}
		else
		{
			Log(LOG_ERROR, "CPSPStream::SetURI(%s) BAD.", strFile);
			ReportError("CPSPStream::OpenFile-Invalid filename '%s'", strFile);
		}
		
	}
	else
	{
		Log(LOG_ERROR, "SetURI() called, with no argument!");
	}
}

bool CPSPStream::DownloadToFile(char *strFilename, size_t &bytesDownloaded)
{
	bool success = false;
	
	if (STREAM_TYPE_URL == GetType())
	{
		if (true == IsOpen())
		{
			FILE *fOut = fopen(strFilename, "w");
			if (fOut)
			{
				int iRet = 0;
				bytesDownloaded = 0;
				char *buffer = (char*)malloc(8192);
				memset(buffer, 0, 8192);
				
				/** Get all /r /n before we start download */
				char c = 0;
				do
				{
					iRet = recv(m_fdSocket, &c, 1, 0);
				} while (iRet == 1 && ( (0x0d == c) || (0x0a == c) ));
				
				if (iRet == 1)
				{
					/** The last chr recv'd was diff than /r /n, so it needs to be written... */
					fwrite(&c, 1, 1, fOut);
					
					for(;;)
					{
						iRet = recv(m_fdSocket, buffer, 8192, 0);
						if (0 == iRet)
						{
							fclose(fOut), fOut = NULL;
							success = true;
							break;
						}
						if (iRet > 0)
						{
							bytesDownloaded+= iRet;
							fwrite(buffer, iRet, 1, fOut);
						}
					}
				}
				else
				{
					Log(LOG_ERROR, "Error downloading..");
					success = false;
				}
				free(buffer), buffer = NULL;
			}
			else
			{
				Log(LOG_ERROR, "DownloadToFile(): Error- Couldn't open file for write.");
				success = false;
			}
		}
		else
		{
			Log(LOG_ERROR, "DownloadToFile(): Error- Couldn't connect");
			success = false;
		}
	}
	else
	{
		Log(LOG_ERROR, "DownloadToFile(): This is not a URL");
		success = false;
	}
	
	return success;
}

void CPSPStream::SetSampleRate(int SampleRate)
{ 
	m_MetaData->iSampleRate = SampleRate; 
	pPSPSound->SampleRateChange();
}

void CPSPStream::Close()
{
	if (STREAM_STATE_OPEN == m_State)
	{
		switch(m_Type)
		{
			case STREAM_TYPE_FILE:
				if (m_pfd)
				{
					fclose(m_pfd);
				}
				m_pfd = NULL;
				break;
			case STREAM_TYPE_URL:
				if (m_fdSocket >= 0)
				{
					//sceNetInetClose(m_fdSocket);
					close(m_fdSocket);
				}
				m_fdSocket = -1;
				break;
			case STREAM_TYPE_NONE:
				Log(LOG_ERROR, "SoundStream::Close(): Invalid stream type %d.", m_Type);
				break;
		}
	}
	m_State = STREAM_STATE_CLOSED;
	//Content type is still defined!
	//m_ContentType = MetaData::CONTENT_NOT_DEFINED;
}

int CPSPStream::Open()
{
	Log(LOG_VERYLOW, "Open() Start");

	SetContentType(MetaData::CONTENT_NOT_DEFINED);
	if (STREAM_STATE_OPEN == m_State)
	{
		Log(LOG_ERROR, "Calling OpenFile, but there is a file open already; Closing.");
		Close();
		Log(LOG_ERROR, "Re-Opening now.");
	}
	
	switch(m_Type)
	{
		case STREAM_TYPE_URL:
			//ReportError ("Opening URL '%s'\n", filename);
			Log(LOG_VERYLOW, "Open() URL");
			m_fdSocket = http_open(m_MetaData->strURI);
			Log(LOG_LOWLEVEL, "Back from http_open(): socket=%d", m_fdSocket);
			if (m_fdSocket < 0)
			{
				//Don't report again, because http_open will report.
				//ReportError("CPSPStream::OpenFile-Error opening URL.\n");
				m_State = STREAM_STATE_CLOSED;
			}
			else
			{
				//ReportError("CPSPStream::OpenFile-URL Opened. (handle=%d)\n", m_fdSocket);
				//Log("Opened. MetaData Interval = %d\n", m_iMetaDataInterval);
				m_State = STREAM_STATE_OPEN;
			}
			break;
		
		case STREAM_TYPE_FILE:
			Log(LOG_VERYLOW, "Open() FILE, calling fopen()");
			m_pfd = fopen(m_MetaData->strURI, "rb");
			if(m_pfd)
			{
				Log(LOG_VERYLOW, "Open() FILE, fopen succeeded.");
				char *ext = strrchr(m_MetaData->strURI, '.') + 1;
				if (strlen(ext) >= 3)
				{
					if (0 == strncasecmp(ext, "mp", 2))
					{
						SetContentType(MetaData::CONTENT_AUDIO_MPEG);
						GetID3Data(m_MetaData);
						SetURL("");
						//pPSPSound->SendEvent(MID_NEW_METADATA_AVAILABLE);
						//Log(LOG_INFO, "ID3 got Title='%s'", m_MetaData->strTitle);
					}
					else if (0 == strncasecmp(ext, "ogg", 3))
					{
						SetContentType(MetaData::CONTENT_AUDIO_OGG);
					}
				}
				m_State = STREAM_STATE_OPEN;
			}
			else
			{
				ReportError("Unable to open %s", m_MetaData->strURI);
			}
			break;
			
		case STREAM_TYPE_NONE:
			Log(LOG_ERROR, "Open(): Invalid Filename..");
			ReportError("Calling Open(), but the set filename is invalid '%s'", m_MetaData->strURI);
			break;
	}
	
	Log(LOG_VERYLOW, "Open() End");
	
	return m_State!=STREAM_STATE_CLOSED?0:-1;
}

bool CPSPStream::IsOpen()
{
	return (m_State==STREAM_STATE_CLOSED)?false:true;
}

/** ----------------------------------------------------------------------------------- */
CPSPStreamReader::CPSPStreamReader(CPSPStream *CurrentStream)
{
	m_BstdFile = NULL;
	//m_eof = true;
	m_eof = false;
	m_CurrentStream = CurrentStream;
	m_iMetaDataInterval = m_CurrentStream->GetMetaDataInterval();
	m_iRunningCountModMetadataInterval = 0;
	memset(bMetaData, 0, MAX_METADATA_SIZE);
 	memset(bPrevMetaData, 0, MAX_METADATA_SIZE);
	m_pfd = m_CurrentStream->GetFileDescriptor();
	m_fdSocket = m_CurrentStream->GetSocketDescriptor();
	if(m_pfd)
	{
		m_BstdFile=NewBstdFile(m_pfd);
		if(m_BstdFile)
		{
			//m_CurrentStream->SetState(STREAM_STATE_OPEN);
		}
		else
		{
			ReportError("CPSPStream::OpenFile-Can't create a new bstdfile_t (%s).",
					strerror(errno));
			//m_State = STREAM_STATE_CLOSED;
			m_CurrentStream->Close();
		}
	}
}

CPSPStreamReader::~CPSPStreamReader()
{
	Close();
}

void CPSPStreamReader::Close()
{
	m_CurrentStream->Close();
	//if (CPSPStream::STREAM_STATE_OPEN == m_CurrentStream->GetState())
	{
		if (CPSPStream::STREAM_TYPE_FILE == m_CurrentStream->GetType())
		{
			if (m_BstdFile)
			{
				BstdFileDestroy(m_BstdFile);
				m_BstdFile = NULL;
			}
		}
	}	
	m_iRunningCountModMetadataInterval = 0;
	memset(bMetaData, 0, MAX_METADATA_SIZE);
	memset(bPrevMetaData, 0, MAX_METADATA_SIZE);
}

void CPSPStreamReader::Seek(int iNewPosition)
{
	if (CPSPStream::STREAM_STATE_OPEN == m_CurrentStream->GetState())
	{
		switch(m_CurrentStream->GetType())
		{
			case CPSPStream::STREAM_TYPE_FILE:
				BstdSeek(iNewPosition, m_BstdFile);
				m_CurrentStream->SetBytePosition(iNewPosition);
				Log(LOG_LOWLEVEL, "CPSPStreamReader::Seek(): %d", iNewPosition);
				break;
			default:
				break;
		}
	}

}

int CPSPStreamReader::Tell()
{
	int iCurrentPos = 0;
	if (CPSPStream::STREAM_STATE_OPEN == m_CurrentStream->GetState())
	{
		switch(m_CurrentStream->GetType())
		{
			case CPSPStream::STREAM_TYPE_FILE:
				iCurrentPos = BstdTell(m_BstdFile);
				m_CurrentStream->SetBytePosition(iCurrentPos);
				Log(LOG_LOWLEVEL, "CPSPStreamReader::Tell(): %d", iCurrentPos);
				break;
			default:
				break;
		}
	}

	return 0;

}

size_t CPSPStreamReader::Read(unsigned char *pBuffer, size_t SizeInBytes)
{
	size_t size = 0;
	char bMetaDataSize = 0;
	int iReadRet = -1;
	
	if (CPSPStream::STREAM_STATE_OPEN == m_CurrentStream->GetState())
	{
		switch(m_CurrentStream->GetType())
		{
			case CPSPStream::STREAM_TYPE_FILE:
				size = BstdRead(pBuffer, 1, SizeInBytes, m_BstdFile);
				break;
			case CPSPStream::STREAM_TYPE_URL:
				if (m_iMetaDataInterval)
				{
					m_iRunningCountModMetadataInterval = (m_iRunningCountModMetadataInterval % m_iMetaDataInterval);
				}
				if (SizeInBytes + m_iRunningCountModMetadataInterval > m_iMetaDataInterval)
				{
					size = SocketRead((char*)pBuffer, m_iMetaDataInterval - m_iRunningCountModMetadataInterval);
					if (size != (m_iMetaDataInterval - m_iRunningCountModMetadataInterval))
					{
						Close();
						m_eof = true;
					}
					iReadRet = SocketRead(&bMetaDataSize, 1);
					if (iReadRet > 0)
					{
						iReadRet = SocketRead(bMetaData, bMetaDataSize * 16);
					}
					if (iReadRet != bMetaDataSize * 16)
					{
						Close();
						m_eof = true;
					}
					else
					{
						/** If new data is received */
						if (memcmp(bPrevMetaData, bMetaData, MAX_METADATA_SIZE) != 0)
						{
							Log(LOG_INFO, "MetaData='%s'", bMetaData);
							memcpy(bPrevMetaData, bMetaData, MAX_METADATA_SIZE);
							
							/** GetMetadataValue() modifies the metadata, so call it
							with the last tag first */
							char *tempMData = (char*)malloc(MAX_METADATA_SIZE);
							if (tempMData)
							{
								char *strURL= "", *strTitle= "";
								memcpy(tempMData, bMetaData, MAX_METADATA_SIZE);
								strURL   = GetMetadataValue(tempMData, METADATA_STREAMURL_TAG);
								strTitle = GetMetadataValue(tempMData, METADATA_STREAMTITLE_TAG);
								
								m_CurrentStream->SetURL(strURL);
								m_CurrentStream->SetTitle(strTitle);
								
								free(tempMData), tempMData = NULL;
								
								pPSPSound->SendEvent(MID_NEW_METADATA_AVAILABLE);
							}
						}
					}
				}
				else
				{
					size = SocketRead((char*)pBuffer, SizeInBytes);
					if (size != SizeInBytes)
					{
						Close();
						m_eof = true;
					}
				}
				if (size > 0)
				{
					m_iRunningCountModMetadataInterval+=size;
				}
				break;
			case CPSPStream::STREAM_TYPE_NONE:
				Log(LOG_ERROR, "Read() Called, but no stream set up.");
				break;
		}
	}
	else
	{
		Log(LOG_ERROR, "Read() Called but the stream is not open!");
	}
	
	return size;
}

bool CPSPStreamReader::IsEOF()
{
	int iseof = 0;
	
	if (CPSPStream::STREAM_STATE_OPEN == m_CurrentStream->GetState())
	{
		switch(m_CurrentStream->GetType())
		{
			case CPSPStream::STREAM_TYPE_FILE:
				iseof = BstdFileEofP(m_BstdFile);
				break;
			case CPSPStream::STREAM_TYPE_URL:
				iseof = m_eof;
				break;
			case CPSPStream::STREAM_TYPE_NONE:
				Log(LOG_ERROR, "IsEOF() Called but stream not setup");
				iseof = 1; /** Make them stop! */
				break;
		}
	}
	else
	{
		Log(LOG_ERROR, "IsEOF() Called but stream not open");
		iseof = 1; /** Make them stop! */
	}
	
	return iseof?true:false;
}

int CPSPStreamReader::SocketRead(char *pBuffer, size_t LengthInBytes)
{
	size_t bytesread = 0, bytestoread = 0;
	size_t size = 0;
	for(;;) 
	{
		bytestoread = LengthInBytes-size;
		bytesread = recv(m_fdSocket, pBuffer+size, bytestoread, 0);
		if (bytesread > 0)
			size += bytesread;
		if(bytesread == bytestoread) 
		{
			//done
			break;
		}
		else if (bytesread == 0)
		{
			ReportError("Connection Reset by Peer!\n");
			//Close();
			//m_eof = true;
			break;
		}
		if (pPSPSound->GetPlayState() == CPSPSound::STOP || pPSPApp->IsExiting() == true)
			break;
		//else if(error = sceNetInetGetErrno() && sceNetInetGetErrno() != EINTR) 
		//{
		//	ReportMessage ( "Error reading from socket or unexpected EOF.(0x%x, %d)\n",error, errno);
		//	m_eof = true;
		//	Close();
		//	break;
		//}
	}
	return size;
}

/** Raw metadata looks like this:
 *  "StreamTitle='title of the song';StreamUrl='url address';"
 */
char *CPSPStreamReader::GetMetadataValue(char *strMetadata, char *strTag)
{
	char *ret = "Parse Error";
	
	if (strMetadata && 
		strTag && 
		(strlen(strMetadata) > strlen(strTag)) && 
		strstr(strMetadata, strTag))
	{
		ret = strstr(strMetadata, strTag) + strlen(strTag);
		if (strchr(ret, ';'))
		{
			*(strchr(ret, ';') - 1) = 0;
		}
	}
	
	return ret;
}
