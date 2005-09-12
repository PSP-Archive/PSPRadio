#ifndef _PSPAPP_
	#define _PSPAPP_
	/* 
	 PSPApp
	*/
	
	#include <pspkernel.h>
	#include <pspdebug.h>
	#include <pspdisplay.h>
	#include <pspctrl.h>
	
	
	/* Define printf, just to make typing easier */
	#define printf	pspDebugScreenPrintf
	
	class CPSPThread;
	
	class CPSPApp
	{
	public:
		CPSPApp();
		virtual ~CPSPApp();
		//virtual int Run() = 0;
	
	protected:
		virtual int CallbackSetupThread(SceSize args, void *argp);
		virtual int OnAppExit(int arg1, int arg2, void *common);
	
		/* System Callbacks */
		static int exitCallback(int arg1, int arg2, void *common);
		/* Callback thread */
		static int callbacksetupThread(SceSize args, void *argp);
	
	private:
		/** Data */
		CPSPThread *m_thCallbackSetup; /** Exit thread */
		
	};
	
	/** Wrapper class around the kernel system calls for thread management */
	class CPSPThread
	{
	/** These macros can be called from inside the thread function */
	#define ThreadSleep() sceKernelSleepThread()
	#define ThreadSleepAndServiceCallbacks() sceKernelSleepThreadCB()
	
	public:
		CPSPThread(const char *strName, SceKernelThreadEntry ThreadEntry, int initPriority = 0x11,
					int stackSize = 0xFA0, SceUInt attr = 0, SceKernelThreadOptParam *option = NULL)
				{ m_thid = sceKernelCreateThread(strName, ThreadEntry, initPriority, stackSize, attr, option);  };
		~CPSPThread()
				{ if (m_thid>=0) sceKernelTerminateDeleteThread(m_thid);  };
				
		int Start(SceSize argc = 0, void *argp = NULL)
				{ return m_thid>=0?sceKernelStartThread(m_thid, argc, argp):-1; };
		int Suspend()
				{ return m_thid>=0?sceKernelSuspendThread(m_thid):-1; };
		int Resume()
				{ return m_thid>=0?sceKernelResumeThread(m_thid):-1; };
		int WakeUp() /** Wakeup a thread that put itself to sleep with ThreadSleep() */
				{ return m_thid>=0?sceKernelWakeupThread(m_thid):-1; };
		int Wait(SceUInt *timeoutInUs) /** Wait until thread exits or timeout */
				{ return m_thid>=0?sceKernelWaitThreadEnd(m_thid, timeoutInUs):-1; };
		int WaitAndServiceCallbacks(SceUInt *timeoutInUs) /** Wait until thread exits(servicing callbacks) or timeout */
				{ return m_thid>=0?sceKernelWaitThreadEndCB(m_thid, timeoutInUs):-1; };
		int SetPriority(int iNewPriority)
				{ return m_thid>=0?sceKernelChangeThreadPriority(m_thid, iNewPriority):-1; };               
		
	private:
		int m_thid;
	};
	
#endif