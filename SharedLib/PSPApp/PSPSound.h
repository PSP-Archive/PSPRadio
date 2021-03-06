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
#ifndef __PSPSOUND__
	#define __PSPSOUND__
	
	#include "PSPThread.h"
	#include "PSPEventQ.h"
	#include "PSPSoundDecoder.h"
	#include "PSPSoundDeviceBuffer.h"
	#include "PSPSoundBuffer.h"
	#include <list>

	/** Message IDs */
	/** Messages from PSPSound to decode thread */
	enum MessageIDsFromPSPSoundToDecodeThread
	{
		MID_DECODER_START 			= 0x10,
		MID_DECODER_DECODE			= 0x11,
		MID_DECODER_PAUSE			= 0x12,
		MID_DECODER_SEEK			= 0x13,
		MID_DECODER_STOP_NEEDOK		= 0x20,
		MID_DECODER_THREAD_EXIT_NEEDOK = 0x30
	};
	
	enum MessageIDsFromPSPSoundToPlayThread
	{
		MID_PLAY_START = 0x110,
		MID_PLAY_STOP  = 0x120,
		MID_PLAY_THREAD_EXIT_NEEDOK = 0x130
	};
	
	class CPSPSound
	{
	public:
		enum pspsound_state
		{
			PLAY,
			PAUSE,
			STOP,
		};
		
	protected:
		CPSPSoundBuffer   Buffer;
		CPSPStream *m_CurrentStream;
		
	private:
		void Initialize();
		/** Accessors */
		int GetAudioHandle();
		
		CPSPThread *m_thDecode,*m_thPlayAudio;
		int m_audiohandle ;
		pspsound_state m_CurrentState;
		
		CPSPEventQ *m_EventToDecTh, *m_EventToPlayTh;
		
	protected:
		void SuspendDecodingThread(){m_thDecode->Suspend();};
		
	public:
		CPSPSound();
		virtual ~CPSPSound();
		
		/** Accessors */
		int Play();
		int Pause();
		int Stop();

		void Seek(int ilocation);

		pspsound_state GetPlayState() { return m_CurrentState; };
		
		CPSPStream *GetCurrentStream(){ return m_CurrentStream; }
		
		int SendEvent(int iEventId, void *pData = NULL, int iSenderId = SID_PSPSOUND)
		{ 
			CPSPEventQ::QEvent event = { iSenderId, iEventId, pData };
			return (pPSPApp && pPSPApp->m_EventToPSPApp)?pPSPApp->m_EventToPSPApp->Send(event):-1;
		};

		size_t GetBufferFillPercentage() { return Buffer.GetBufferFillPercentage(); };
		void   ChangeBufferSize(size_t size) { Buffer.ChangeBufferSize(size); };
		
		void SetDecodeThreadPriority(int iNewPrio);
		void SetPlayThreadPriority(int iNewPrio);
		
		int GetEventToDecThSize() { return m_EventToDecTh->Size(); }
		int GetEventToPlayThSize() { return m_EventToPlayTh->Size(); }
		
		void SampleRateChange() { Buffer.SampleRateChange(m_CurrentStream->GetSampleRate()); }
		
		/** Threads */
		static int ThPlayAudio(SceSize args, void *argp);
		static int ThDecode(SceSize args, void *argp);
	};
	
	extern CPSPSound *pPSPSound;
	
#endif
