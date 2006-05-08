/*
	PSP Keypad handler.
	Copyright (C) 2005  Rafael Cabezas a.k.a. Raf & Jesper Sandberg

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

#include <pspkerneltypes.h>
#include <pspctrl.h>

#ifndef _PSPKEYHANDLER_
	#define _PSPKEYHANDLER_

	/* The number of "repeated" events that must pass before the key press is being
	   considered a long key press */
	#define KEYHANDLER_LONG_PRESS			4

	enum KEY_EVENT
	{
		KEY_EVENT_PRESSED,
		KEY_EVENT_REPEAT,
		KEY_EVENT_RELEASED,
		KEY_EVENT_LONG_PRESS,
	};

	class CPSPKeyHandler
	{
	public:

		struct KeyEvent
		{
			u32		event;
			u32		key_state;
		};

		CPSPKeyHandler();
		~CPSPKeyHandler();

		bool KeyHandler(KeyEvent &event);
		/* delay in milliseconds between repeated events (default is 250 ms) */
		void KeyHandler_Repeat(u32 delay);

	private:

		enum STM_STATE
		{
			STATE_IDLE,
			STATE_PRESSED,
		};

		enum STM_EVENT
		{
			EVENT_PRESSED,
			EVENT_RELEASED,
			EVENT_UPDATE,
		};

		bool KeyHandlerSTM(STM_EVENT key_event, KeyEvent &event);
		void KeyHandlerStore();

		SceCtrlLatch	m_latch;
		u32				m_stored_keystate;
		u64				m_tick;
		u64				m_tick_trigger;
		u32				m_long_press;
		u32				m_repeat_delay;
	};

#endif /* _PSPKEYHANDLER_ */
