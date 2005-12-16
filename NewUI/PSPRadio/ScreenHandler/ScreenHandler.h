#ifndef __SCREEN_HANDLER__
	#define __SCREEN_HANDLER__
	
	#include <PSPSound.h>
	#include <iniparser.h>
	#include <list>
	using namespace std;


	/** Messages from PSPSound to decode thread */
	enum EventIDsFromScreenHandlerToPSPRadio
	{
		EID_NEW_UI_POINTER,
		EID_EXIT_SELECTED
	};

	/** Sender IDs */
	#define SID_SCREENHANDLER		0x00000001

	class IPSPRadio_UI;

	class CScreenHandler;

	enum playstates
	{
		PLAYSTATE_PLAY,
		PLAYSTATE_STOP,
		PLAYSTATE_PAUSE,
		PLAYSTATE_EOS
	};
	
	class IScreen
	{
		public:

			IScreen(int Id, CScreenHandler *ScreenHandler)
				{m_ScreenHandler = ScreenHandler, m_Id = Id; m_UI = NULL;}
			virtual ~IScreen(){}
			
			int GetId(){ return m_Id; }

			virtual void Activate(IPSPRadio_UI *UI);

			virtual void InputHandler(int iButtonMask){};
			
			virtual void OnPlayStateChange(playstates NewPlayState){};

		
		protected:
			int m_Id;
			IPSPRadio_UI *m_UI;
			CScreenHandler *m_ScreenHandler;
	};

	enum playmodes
	{
		PLAYMODE_NORMAL,
		PLAYMODE_SINGLE,
		PLAYMODE_REPEAT,
	};
	
	class CScreenHandler
	{
	public:
		enum Screen
		{
			PSPRADIO_SCREEN_LOCALFILES,
			PSPRADIO_SCREEN_PLAYLIST,
			PSPRADIO_SCREEN_SHOUTCAST_BROWSER,
			/** Add more elements here **/

			PSPRADIO_SCREEN_OPTIONS /* Leave this as the last one */
		};
		/** These point to the first and one-beyond the last elements of the
			list; just like in a sdl list. This to allow for easy iteration
			though the list with a for loop/etc. 
		*/
		#define PSPRADIO_SCREEN_LIST_BEGIN  PSPRADIO_SCREEN_LOCALFILES
		#define PSPRADIO_SCREEN_LIST_END	(PSPRADIO_SCREEN_OPTIONS+1)
		
		enum UIs
		{
			UI_TEXT = 0,
			UI_GRAPHICS = 1,
			UI_TEXT_3D = 2,
		};
		
		enum request_on_play_stop
		{
			NOTHING,
			PLAY_REQUEST,
			STOP_REQUEST,		/** User selected to stop */
		};
		
		
		CScreenHandler(char *strCWD, CIniParser *Config, CPSPSound *Sound, Screen InitialScreen = PSPRADIO_SCREEN_PLAYLIST);
		~CScreenHandler();
		IPSPRadio_UI *StartUI(UIs UI);
		
		void CommonInputHandler(int iButtonMask);
		void OnHPRMReleased(u32 iHPRMMask);

		bool DownloadSHOUTcastDB();

		IScreen *GetCurrentScreen(){return m_CurrentScreen;}
		UIs GetCurrentUI(){return m_CurrentUI;}
		char *GetCWD(){return m_strCWD;}
		CPSPSound *GetSound(){return m_Sound;}
		IScreen *GetScreen(int Id){return Screens[Id];}
		CIniParser *GetConfig() { return m_Config;}

		Screen GetInitialScreen(){ return m_InitialScreen; }
		void SetInitialScreen(Screen screen){m_InitialScreen = screen;}
		
		IScreen *GetStreamOwnerScreen() { return m_StreamOwnerScreen; }
		void SetStreamOwnerScreen(IScreen *screen) { m_StreamOwnerScreen = screen; }
		
		playmodes GetPlayMode() { return m_PlayMode; }
		void SetPlayMode(playmodes mode) { m_PlayMode = mode; }
		
		void OnVBlank();
		
		request_on_play_stop m_RequestOnPlayOrStop;

		/** Screenshot functionality */

		#define SCREEN_WIDTH 480
		#define SCREEN_HEIGHT 272

		enum ScreenShotState
		{
			PSPRADIO_SCREENSHOT_ACTIVE,
			PSPRADIO_SCREENSHOT_NOT_ACTIVE
		};

		void Screenshot();
		char *ScreenshotName(char *path);
		void ScreenshotStore(char *filename);
		
	protected:
		friend class IScreen;
		void SetCurrentScreen(IScreen *pScreen){m_CurrentScreen = pScreen;}

	private:
		IScreen *m_CurrentScreen;
		IScreen *m_PreviousScreen;
		IScreen *m_StreamOwnerScreen;
		Screen   m_InitialScreen;
		UIs m_CurrentUI;
		IPSPRadio_UI *m_UI;
		CIniParser *m_Config;
		CPSPSound *m_Sound;
		char *m_strCWD;
		IScreen *Screens[PSPRADIO_SCREEN_LIST_END];
		playmodes m_PlayMode;
	};
#endif