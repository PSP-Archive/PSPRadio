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
#include "PSPSound.h"
#include "PSPSoundDecoder_MAD.h"
#include "PSPSoundDecoder_OGG.h"

using namespace std;

CPSPSound *pPSPSound = NULL;

/** Accessors */
int CPSPSound::GetAudioHandle()
{
	return m_audiohandle;
}

CPSPSound::CPSPSound()
{
	Log(LOG_VERYLOW, "PSPSound Constructor");
	m_thDecode = NULL;
	m_thPlayAudio  = NULL;
	pPSPSound = this;
	m_audiohandle = -1;
	m_CurrentState = STOP;
	m_EventToDecTh = m_EventToPlayTh = NULL;
	Initialize();
}

void CPSPSound::Initialize()
{
	Log(LOG_VERYLOW, "PSPSound Initialize()");

	Buffer.Empty();

	m_audiohandle = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,
									PSP_BUFFER_SIZE_IN_FRAMES,
									PSP_AUDIO_FORMAT_STEREO);

	if ( m_audiohandle < 0 )
	{
		Log(LOG_ERROR, "Error getting a sound channel!");
		ReportError("Unable to aquire sound channel");
	}

	m_CurrentStream = new CPSPStream();

	m_EventToDecTh  = new CPSPEventQ("eventq2dec_th");
	m_EventToPlayTh = new CPSPEventQ("eventq2play_th");

	m_thDecode = new CPSPThread("decode_thread", ThDecode, 64, 128*1024);
	m_thPlayAudio = new CPSPThread("playaudio_thread", ThPlayAudio, 32, 128*1024);

	if (m_CurrentStream && m_EventToDecTh && m_EventToPlayTh && m_thDecode && m_thPlayAudio)
	{
		m_thPlayAudio->Start();
		m_thDecode->Start();

		CPSPEventQ::QEvent event = { 0, 0x0, NULL };
		event.EventId = MID_PLAY_START;
		m_EventToPlayTh->Send(event);

	}
	else
	{
		Log(LOG_ERROR, "Initialize(): Memory allocation error!");
	}

}

CPSPSound::~CPSPSound()
{
	CPSPEventQ::QEvent event = { 0, 0x0, NULL };
	Log(LOG_VERYLOW, "~CPSPSound(): pPSPApp->m_Exit=%d", pPSPApp->m_Exit);

	if (m_thDecode)
	{
		/** Wake the decoding thread up, so it can exit*/
		Log(LOG_VERYLOW, "~CPSPSound(): Tell decode thread to exit.");
		event.EventId = MID_DECODER_THREAD_EXIT_NEEDOK;
		m_EventToDecTh->SendAndWaitForOK(event);

		Log(LOG_VERYLOW, "~CPSPSound(): Destroying decode thread. ");
		delete(m_thDecode), m_thDecode = NULL;
	}

	if (m_thPlayAudio)
	{
		Log(LOG_VERYLOW, "~CPSPSound(): Tell play thread to exit. ");
		event.EventId = MID_PLAY_THREAD_EXIT_NEEDOK;
		m_EventToPlayTh->SendAndWaitForOK(event);

		Log(LOG_VERYLOW, "~CPSPSound(): Destroying play thread. ");
		delete(m_thPlayAudio), m_thPlayAudio = NULL;
	}

	if (m_EventToDecTh)
	{
		delete(m_EventToDecTh); m_EventToDecTh = NULL;
	}

	if (m_EventToPlayTh)
	{
		delete(m_EventToPlayTh); m_EventToPlayTh = NULL;
	}


	Log(LOG_VERYLOW, "~CPSPSound(): The End.");

}

void CPSPSound::SetDecodeThreadPriority(int iNewPrio)
{
	m_thDecode->SetPriority(iNewPrio);
}

void CPSPSound::SetPlayThreadPriority(int iNewPrio)
{
	m_thPlayAudio->SetPriority(iNewPrio);
}

int CPSPSound::Play()
{
	CPSPEventQ::QEvent event = { 0, 0x0, NULL };
	Log(LOG_LOWLEVEL, "Play('%s'): m_CurrentState=%s",
		m_CurrentStream->GetURI(),
		m_CurrentState==PLAY?"PLAY":(m_CurrentState==STOP?"STOP":"PAUSE"));
	switch(m_CurrentState)
	{
			
		case PAUSE:
			//event.EventId = MID_PLAY_START;
			//m_EventToPlayTh->Send(event);
			event.EventId = MID_DECODER_DECODE;
			m_EventToDecTh->Send(event);
			break;
#if 0
		case PLAY:
			/** Shouldn't get here, let's restart */
			Log(LOG_LOWLEVEL, "Play() called, but playing already. Need to stop current stream first.");
			event.EventId = MID_DECODER_STOP_NEEDOK;
			m_EventToDecTh->SendAndWaitForOK(event);

			Log(LOG_LOWLEVEL, "Play(): After stopping. Now starting decoder.");
			event.EventId = MID_DECODER_START;
			m_EventToDecTh->Send(event);
			//event.EventId = MID_DECODER_START;
			//m_EventToDecTh->Send(event);
			break;
#endif
		default:
			Log(LOG_ERROR, "Play(): unknown current state, but starting decoder anyway.");
		case PLAY:
		case STOP:
			event.EventId = MID_DECODER_START;
			m_EventToDecTh->Send(event);
			break;
			break;
	}
	m_CurrentState = PLAY;
	//pPSPSound->SendEvent(MID_SOUND_STARTED);

	return m_CurrentState;
}

int CPSPSound::Pause()
{
	CPSPEventQ::QEvent event = { 0, 0x0, NULL };
	Log(LOG_LOWLEVEL, "Pause(): Called. m_CurrentState=%s",
		m_CurrentState==PLAY?"PLAY":(m_CurrentState==STOP?"STOP":"PAUSE"));
	switch(m_CurrentState)
	{
		default:
			Log(LOG_ERROR, "Pause(): Unknown current state; pausing anyway.");
		case STOP:
		case PAUSE:
		case PLAY:
			m_CurrentState = PAUSE;
			event.EventId = MID_DECODER_PAUSE;
			m_EventToDecTh->Send(event);
			//event.EventId = MID_PLAY_STOP;
			//m_EventToPlayTh->Send(event);
			break;

			break;
	}

	return m_CurrentState;
}

int CPSPSound::Stop()
{
	CPSPEventQ::QEvent event = { 0, 0x0, NULL };
	Log(LOG_LOWLEVEL, "Stop(): Called. m_CurrentState=%s",
		m_CurrentState==PLAY?"PLAY":(m_CurrentState==STOP?"STOP":"PAUSE"));
	switch(m_CurrentState)
	{
		default:
			Log(LOG_ERROR, "Stop(): Unknown current state; stopping anyway.");
		case STOP:
		case PLAY:
		case PAUSE:
			/** if we were paused, restart threads first! */
			m_CurrentState = STOP;
			event.EventId = MID_DECODER_STOP_NEEDOK;
			Log(LOG_LOWLEVEL, "Stop: Sending message and waiting for OK");
			m_EventToDecTh->SendAndWaitForOK(event);
			Log(LOG_LOWLEVEL, "Stop: OK received");
			//event.EventId = MID_PLAY_START;
			//m_EventToPlayTh->Send(event);
			break;
	}

	pPSPSound->SendEvent(MID_SOUND_STOPPED);

	return m_CurrentState;
}

void CPSPSound::Seek(int iLocation)
{
	CPSPEventQ::QEvent event = { 0, 0x0, NULL };
	Log(LOG_LOWLEVEL, "Seek(): Called. location=%d",iLocation);
	event.EventId = MID_DECODER_SEEK;
	event.pData = (void *)iLocation;
	m_EventToDecTh->Send(event);
}

/** Threads */
int CPSPSound::ThPlayAudio(SceSize args, void *argp)
{
		static size_t s_LastBufferPercentage = 200;

		DeviceBuffer *mybuf = NULL;
		int ah = pPSPSound->GetAudioHandle();
		CPSPEventQ *m_EventToPlayTh = pPSPSound->m_EventToPlayTh;
		CPSPEventQ::QEvent event = { 0, 0, NULL };
		int rret = 0;
		int timeLastPercentEvent = 500; /** Initialize to something so the percentage event is sent the first time */

		Log(LOG_INFO, "Starting Play Thread.");
		pPSPSound->SendEvent(MID_THPLAY_BEGIN);

		for(;;)
		{
			Log(LOG_VERYLOW, "ThPlay::Calling Receive. %d Messages in Queue", m_EventToPlayTh->Size());
			rret = m_EventToPlayTh->Receive(event);
			Log(LOG_VERYLOW, "ThPlay::Receive Ret=%d. eventid=0x%08x.", rret, event.EventId);
			switch (event.EventId)
			{
			case MID_PLAY_THREAD_EXIT_NEEDOK:
				Log(LOG_VERYLOW, "ThPlay:: Thread Exit message received.");
				pPSPSound->SendEvent(MID_THPLAY_END);
				m_EventToPlayTh->SendReceiveOK();
				sceKernelExitThread(0);
				break;

			case MID_PLAY_START:
			{
				Log(LOG_VERYLOW, "ThPlay:: Thread Play message received.");
				pPSPSound->SendEvent(MID_THPLAY_PLAYING);
				while (0 == m_EventToPlayTh->Size())
				{
					mybuf = pPSPSound->Buffer.PopDeviceBuffer();
					pPSPSound->SendEvent(MID_THPLAY_PCMBUFFER, mybuf);
					sceAudioOutputPannedBlocking(ah, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, mybuf);

					if ((clock()*1000/CLOCKS_PER_SEC - timeLastPercentEvent) > 333) /** 3 times per sec */
					{
						if (s_LastBufferPercentage != pPSPSound->GetBufferFillPercentage())
						{
							s_LastBufferPercentage = pPSPSound->GetBufferFillPercentage();
							pPSPSound->SendEvent(MID_BUFF_PERCENT_UPDATE);
							timeLastPercentEvent = clock() * 1000 / CLOCKS_PER_SEC;
						}
					}
				}
				Log(LOG_VERYLOW, "ThPlay:: Thread Play exiting play area - message waiting.");
				break;
			}
			case MID_PLAY_STOP:
				if (STOP == pPSPSound->GetPlayState())
				{
					Log(LOG_VERYLOW, "ThPlay:: Stop message received.");
					pPSPSound->Buffer.Empty();
					pPSPSound->SendEvent(MID_THPLAY_DONE);
				}
				else
				{
					Log(LOG_ERROR, "ThPlay:: Stop message received. (But was stopped/paused)");
				}
				break;
			}
		}
		return 0;
}

int CPSPSound::ThDecode(SceSize args, void *argp)
{
	IPSPSoundDecoder *Decoder = NULL;
	bool bDecoderCreated = false;

	Log(LOG_INFO,"Starting Decoding Thread.");
	pPSPSound->SendEvent(MID_THDECODE_BEGIN);

	CPSPEventQ *m_EventToDecTh = pPSPSound->m_EventToDecTh;
	CPSPEventQ::QEvent event = { 0, 0, NULL };
	int rret = 0;
	for(;;)
	{
		Log(LOG_VERYLOW, "ThDecode::Calling Receive");
		rret = m_EventToDecTh->Receive(event);
		Log(LOG_VERYLOW, "ThDecode::Receive Ret=%d. event=0x%08x.", rret, event.EventId);
		switch (event.EventId)
		{
			case MID_DECODER_START:
				Log(LOG_VERYLOW, "ThDecode:: Start Decoder message received.");

				if (true == bDecoderCreated)
				{
					Log(LOG_VERYLOW, "ThDecode:: Deleting decoder..");
					delete Decoder, Decoder = NULL;
					Log(LOG_VERYLOW, "ThDecode:: Finished decoding. Decoder deleted.");
				}

				pPSPSound->SendEvent(MID_DECODE_STREAM_OPENING);
				Log(LOG_INFO, "ThDecode:: Calling Open For '%s'",
					pPSPSound->m_CurrentStream->GetURI());
				pPSPSound->m_CurrentStream->Open();

				if (true == pPSPSound->m_CurrentStream->IsOpen())
				{
					bDecoderCreated = false;
					switch (pPSPSound->m_CurrentStream->GetContentType())
					{
						case MetaData::CONTENT_NOT_DEFINED:
							Log(LOG_INFO, "ThDecode:: Content type not defined. Defaulting to MPEG.");
							//pPSPSound->SendEvent(MID_DECODE_STREAM_OPEN_ERROR);
							//bDecoderCreated = false;
							//break;
							//fall through (some servers don't define stream type - asume MPEG)
						case MetaData::CONTENT_AUDIO_MPEG:
							Log(LOG_INFO, "ThDecode:: MPEG Stream Opened Successfully.");
							pPSPSound->SendEvent(MID_DECODE_STREAM_OPEN);
							Decoder = new CPSPSoundDecoder_MAD(&pPSPSound->Buffer, pPSPSound->m_CurrentStream);
							bDecoderCreated = true;
							break;
						case MetaData::CONTENT_AUDIO_OGG:
							Log(LOG_INFO, "ThDecode:: OGG Stream Opened Successfully.");
							pPSPSound->SendEvent(MID_DECODE_STREAM_OPEN);
							Decoder = new CPSPSoundDecoder_OGG(&pPSPSound->Buffer, pPSPSound->m_CurrentStream);
							bDecoderCreated = true;
							break;
						case MetaData::CONTENT_AUDIO_AAC:
							Log(LOG_INFO, "ThDecode:: AAC Stream Not supported.");
							pPSPSound->SendEvent(MID_DECODE_STREAM_OPEN_ERROR);
							//pPSPSound->m_CurrentState = STOP;
							bDecoderCreated = false;
							break;

						case MetaData::CONTENT_PLAYLIST:
						{
							Log(LOG_INFO, "ThDecode: This is a playlist.. downloading..");
							CPSPStreamReader *PLReader = new CPSPStreamReader(pPSPSound->m_CurrentStream);
							char strPlayListBuf[256];
							strPlayListBuf[0]=0;
							PLReader->Read((u8*)strPlayListBuf, 256);
							strPlayListBuf[255] =0;
							Log(LOG_INFO, "ThDecode: playlist contents: '%s'", strPlayListBuf);
							char *strURI = "";
							if (strstr(strPlayListBuf, "File1="))
							{
								strURI = strstr(strPlayListBuf, "File1=")+strlen("File1=");
								*strchr(strURI, 0xA) = 0;
								pPSPSound->m_CurrentStream->SetURI(strURI);
								pPSPSound->SendEvent(MID_NEW_METADATA_AVAILABLE);
								/** We send an event to ourselves */
								event.EventId = MID_DECODER_START;
								m_EventToDecTh->Send(event);
							}
							delete (PLReader);
							bDecoderCreated = false;
							break;
						}
						default:
							Log(LOG_ERROR, "ThDecode:: Content type not recognized.");
							bDecoderCreated = false;
							break;

					}

					if (true == bDecoderCreated)
					{
						/** We send an event to ourselves to begin decoding*/
						event.EventId = MID_DECODER_DECODE;
						m_EventToDecTh->Send(event);
					}
				
				}
				else
				{
					Log(LOG_ERROR, "ThDecode:: Unable to open stream '%s'.", pPSPSound->m_CurrentStream->GetURI());
					pPSPSound->SendEvent(MID_DECODE_STREAM_OPEN_ERROR);
				}

				break;
				
			case MID_DECODER_DECODE:
				Log(LOG_VERYLOW, "ThDecode:: MID_DECODER_DECODE message received. (Start/Continue decoding)");
				if (true == bDecoderCreated)
				{
					//Log(LOG_INFO, "ThDecode::(Title='%s') Seding event to start decoding",
					//	pPSPSound->m_CurrentStream->GetTitle());
					pPSPSound->SendEvent(MID_THDECODE_DECODING);
					/** Main decoding loop */
					/* pPSPSound is the decoding loop. */
					while (m_EventToDecTh->Size() == 0)
					{
						if (true == Decoder->Decode())
						{
							Log(LOG_VERYLOW, "ThDecode:: Finished decoding. About to delete decoder.");
							delete Decoder, Decoder = NULL;
							Log(LOG_VERYLOW, "ThDecode:: Finished decoding. Decoder deleted.");
							bDecoderCreated = false;
							//pPSPSound->SendEvent(MID_THDECODE_DECODING_DONE);
							pPSPSound->SendEvent(MID_THDECODE_EOS);
							break;
						}
						sceKernelDelayThread(10); /** 100us */
					}
					Log(LOG_INFO, "Finished decoding (or stopped/paused)");
				}
				else
				{
					Log(LOG_VERYLOW, "ThDecode:: MID_DECODER_DECODE message received. Decoder didn't exitst, will retry STARTING the decoder.");
					/** Close if no decoder instantiated */
					pPSPSound->m_CurrentStream->Close();
					
					/** We send an event to ourselves to retry*/
					event.EventId = MID_DECODER_START;
					m_EventToDecTh->Send(event);
				}
				break;
				
			case MID_DECODER_SEEK:
				{
					int iPosition = (int)event.pData;
					if (bDecoderCreated)
					{
						Log(LOG_VERYLOW, "ThDecode:: Seek to %d", iPosition);
						Decoder->Seek(iPosition);
					}
					break;
				}
			case MID_DECODER_PAUSE:
				Log(LOG_VERYLOW, "ThDecode:: Pause Decoder message received.");
				break;
			
			case MID_DECODER_STOP_NEEDOK:
				Log(LOG_VERYLOW, "ThDecode:: Stop Decoder message received.");
				if (true == bDecoderCreated)
				{
						Log(LOG_VERYLOW, "ThDecode:: Deleting decoder..");
						delete Decoder, Decoder = NULL;
						Log(LOG_VERYLOW, "ThDecode:: Finished decoding. Decoder deleted.");
				}
				//pPSPSound->Buffer.Empty();
				m_EventToDecTh->SendReceiveOK();
				break;
				
			case MID_DECODER_THREAD_EXIT_NEEDOK:
				Log(LOG_VERYLOW, "ThDecode:: Thread Exit message received.");
				pPSPSound->SendEvent(MID_THDECODE_END);
				m_EventToDecTh->SendReceiveOK();
				sceKernelExitThread(0);
				break;
		}
	}

	return 0;
}
