#ifndef psp_h
	#define psp_h

	#include <stdio.h>
	#include <pspiofilemgr_dirent.h>
	#include <dirent.h>

	#include "stuff.h"

	#include "pspcurses.h"

	void PSPPutch(char ch);

	int pipe_open(int *fdpair);
	int pipe_close(int *fdpair);
	int pipe_read(int fd, void *buf, size_t len);
	int pipe_nonblocking_read(int fd, void *buf, size_t len);
	int pipe_write(int fd, void *buf, size_t len);
	int env_termsize(int *x, int *y);

	void PSPInputHandlerStart();
	void PSPInputHandlerEnd();
	int PSPInputHandler(SceCtrlData pad, char *key);

	extern tBoolean g_InputMethod;
	extern volatile tBoolean g_PSPEnableRendering;
	extern volatile tBoolean g_PSPEnableInput;


	///thread:
	#include <pspkernel.h>
	#include <pspkerneltypes.h>
	
#endif
