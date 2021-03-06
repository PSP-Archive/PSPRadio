/* framebuffer.c
 * Linux framebuffer code
 * (c) 2002 Petr 'Brain' Kulhavy
 * This file is a part of the Links program, released under GPL.
 */

#include "cfg.h"

#ifdef GRDRV_PSPFB

#ifdef TEXT
#undef TEXT
#endif

#include "links.h"
#include <pthreadlite.h>
#include <pspctrl.h>
#include <pspfb.h>
#include <signal.h>
#include "newarrow.inc"
#include <pspdisplay.h>

#define printf pspDebugScreenPrintf

static volatile int sf_danzeffOn = 0;
static volatile int s_BbRowOffset = 0, s_BbColOffset = 0;
static volatile int s_bbDirty = falsE;
static volatile int s_Zoom = falsE;

int pspfb_console;

struct itrm *pspfb_kbd;

struct graphics_device *pspfb_old_vd;

char *pspfb_mem, *pspfb_vmem, *psp_fb, *psp_fb1, *psp_fb2;
int pspfb_mem_size,pspfb_linesize,pspfb_bits_pp,pspfb_pixelsize;
int pspfb_xsize,pspfb_ysize;
int border_left, border_right, border_top, border_bottom;
int pspfb_colors;

void pspfb_draw_bitmap(struct graphics_device *dev,struct bitmap* hndl, int x, int y);

static unsigned char *pspfb_driver_param;
struct graphics_driver pspfb_driver;
volatile int pspfb_active=1;

static volatile int in_gr_operation;

/* mouse */
static int mouse_x, mouse_y;		/* mouse pointer coordinates */
static int mouse_black, mouse_white;
static int background_x, background_y; /* Where was the mouse background taken from */
static unsigned char *mouse_buffer, *background_buffer, *new_background_buffer;
static struct graphics_device *mouse_graphics_device;
static int global_mouse_hidden;


#define TEST_MOUSE(xl,xh,yl,yh) if (RECTANGLES_INTERSECT(\
					(xl),(xh),\
					background_x,background_x+arrow_width,\
					(yl),(yh),\
					background_y,background_y+arrow_height)\
					&& !global_mouse_hidden){\
					mouse_hidden=1;\
					hide_mouse();\
				}else mouse_hidden=0;

#define END_MOUSE if (mouse_hidden) show_mouse();

#define START_GR in_gr_operation=1;
#define END_GR	\
		END_MOUSE\
		in_gr_operation=0;


#define NUMBER_OF_DEVICES	1



#define TEST_INACTIVITY if (!pspfb_active||dev!=current_virtual_device) return;

#define TEST_INACTIVITY_0 if (!pspfb_active||dev!=current_virtual_device) return 0;

#define RECTANGLES_INTERSECT(xl0, xh0, xl1, xh1, yl0, yh0, yl1, yh1) (\
				   (xl0)<(xh1)\
				&& (xl1)<(xh0)\
				&& (yl0)<(yh1)\
				&& (yl1)<(yh0))

/* This assures that x, y, xs, ys, data will be sane according to clipping
 * rectangle. If nothing lies within this rectangle, the current function
 * returns. The data pointer is automatically advanced by this macro to reflect
 * the right position to start with inside the bitmap. */
#define	CLIP_PREFACE \
	int mouse_hidden;\
	int xs=hndl->x,ys=hndl->y;\
        char *data=hndl->data;\
\
 	TEST_INACTIVITY\
        if (x>=dev->clip.x2||x+xs<=dev->clip.x1) return;\
        if (y>=dev->clip.y2||y+ys<=dev->clip.y1) return;\
        if (x+xs>dev->clip.x2) xs=dev->clip.x2-x;\
        if (y+ys>dev->clip.y2) ys=dev->clip.y2-y;\
        if (dev->clip.x1-x>0){\
                xs-=(dev->clip.x1-x);\
                data+=pspfb_pixelsize*(dev->clip.x1-x);\
                x=dev->clip.x1;\
        }\
        if (dev->clip.y1-y>0){\
                ys-=(dev->clip.y1-y);\
                data+=hndl->skip*(dev->clip.y1-y);\
                y=dev->clip.y1;\
        }\
        /* xs, ys: how much pixels to paint\
         * data: where to start painting from\
         */\
	START_GR\
	TEST_MOUSE (x,x+xs,y,y+ys)


/* fill_area: 5,5,10,10 fills in 25 pixels! */

/* This assures that left, right, top, bottom will be sane according to the
 * clipping rectangle set up by svga_driver->set_clip_area. If empty region
 * results, return from current function occurs. */
#define FILL_CLIP_PREFACE \
	int mouse_hidden;\
	TEST_INACTIVITY\
	if (left>=right||top>=bottom) return;\
	if (left>=dev->clip.x2||right<=dev->clip.x1||top>=dev->clip.y2||bottom<=dev->clip.y1) return;\
	if (left<dev->clip.x1) left=dev->clip.x1;\
	if (right>dev->clip.x2) right=dev->clip.x2;\
	if (top<dev->clip.y1) top=dev->clip.y1;\
	if (bottom>dev->clip.y2) bottom=dev->clip.y2;\
	START_GR\
	TEST_MOUSE(left,right,top,bottom)


#define HLINE_CLIP_PREFACE \
	int mouse_hidden;\
	TEST_INACTIVITY\
	if (y<dev->clip.y1||y>=dev->clip.y2||right<=dev->clip.x1||left>=dev->clip.x2) return;\
	if (left<dev->clip.x1) left=dev->clip.x1;\
	if (right>dev->clip.x2) right=dev->clip.x2;\
	if (left>=right) return;\
	START_GR\
	TEST_MOUSE (left,right,y,y+1)

#define VLINE_CLIP_PREFACE \
	int mouse_hidden;\
	TEST_INACTIVITY\
	if (x<dev->clip.x1||x>=dev->clip.x2||top>=dev->clip.y2||bottom<=dev->clip.y1) return;\
	if (top<dev->clip.y1) top=dev->clip.y1;\
	if (bottom>dev->clip.y2) bottom=dev->clip.y2;\
	if (top>=bottom) return;\
	START_GR\
	TEST_MOUSE(x,x+1,top,bottom)

#define HSCROLL_CLIP_PREFACE \
	int mouse_hidden;\
	TEST_INACTIVITY_0\
	if (!sc) return 0;\
	if (sc>(dev->clip.x2-dev->clip.x1)||-sc>(dev->clip.x2-dev->clip.x1))\
		return 1;\
	START_GR\
	TEST_MOUSE (dev->clip.x1,dev->clip.x2,dev->clip.y1,dev->clip.y2)

#define VSCROLL_CLIP_PREFACE \
	int mouse_hidden;\
	TEST_INACTIVITY_0\
	if (!sc) return 0;\
	if (sc>dev->clip.y2-dev->clip.y1||-sc>dev->clip.y2-dev->clip.y1) return 1;\
	START_GR\
	TEST_MOUSE (dev->clip.x1, dev->clip.x2, dev->clip.y1, dev->clip.y2)\


/* n is in bytes. dest must begin on pixel boundary. If n is not a whole number of pixels, rounding is
 * performed downwards.
 */
static inline void pixel_set(unsigned char *dest, int n,void * pattern)
{
#ifdef PSP_16BPP
	#ifdef t2c
		short v=*(t2c *)pattern;
		int a;
		
		for (a=0;a<(n>>1);a++) ((t2c *)dest)[a]=v;
	#else
		unsigned char a,b;
		int i;
		
		a=*(char*)pattern;
		b=((char*)pattern)[1];
		for (i=0;i<=n-2;i+=2){
			dest[i]=a;
			dest[i+1]=b;
		}
	#endif
#else /* 32bpp */
	unsigned char a,b,c,d;
	int i;

	a=*(char*)pattern;
	b=((char*)pattern)[1];
	c=((char*)pattern)[2];
	d=((char*)pattern)[3];
	for (i=0;i<=n-4;i+=4){
		dest[i]=a;
		dest[i+1]=b;
		dest[i+2]=c;
		dest[i+3]=d;
	}
#endif	
}

static void redraw_mouse(void);

static void pspfb_mouse_move(int dx, int dy, int fl)
{
	struct event ev;
	mouse_x += dx;
	mouse_y += dy;
	ev.ev = EV_MOUSE;
	if (mouse_x >= pspfb_xsize) mouse_x = pspfb_xsize - 1;
	if (mouse_y >= pspfb_ysize) mouse_y = pspfb_ysize - 1;
	if (mouse_x < 0) mouse_x = 0;
	if (mouse_y < 0) mouse_y = 0;
	ev.x = mouse_x;
	ev.y = mouse_y;
	ev.b = B_MOVE;
	if (!current_virtual_device) return;
	if (current_virtual_device->mouse_handler) current_virtual_device->mouse_handler(current_virtual_device, ev.x, ev.y, fl/*ev.b*/);
	redraw_mouse();
}

#define mouse_getscansegment(buf,x,y,w) memcpy(buf,pspfb_vmem+y*pspfb_linesize+x*pspfb_pixelsize,w)
#define mouse_drawscansegment(ptr,x,y,w) memcpy(pspfb_vmem+y*pspfb_linesize+x*pspfb_pixelsize,ptr,w);

/* Flushes the background_buffer onscreen where it was originally taken from. */
static void place_mouse_background(void)
{
	struct bitmap bmp;

	bmp.x=arrow_width;
	bmp.y=arrow_height;
	bmp.skip=arrow_width*pspfb_pixelsize;
	bmp.data=background_buffer;

	{
		struct graphics_device * current_virtual_device_backup;

		current_virtual_device_backup=current_virtual_device;
		current_virtual_device=mouse_graphics_device;
		pspfb_draw_bitmap(mouse_graphics_device, &bmp, background_x,
			background_y);
		current_virtual_device=current_virtual_device_backup;
	}

}

/* Only when the old and new mouse don't interfere. Using it on interfering mouses would
 * cause a flicker.
 */
static void hide_mouse(void)
{

	global_mouse_hidden=1;
	place_mouse_background();
}

/* Gets background from the screen (clipping provided only right and bottom) to the
 * passed buffer.
 */
static void get_mouse_background(unsigned char *buffer_ptr)
{
	int width,height,skip,x,y;

	skip=arrow_width*pspfb_pixelsize;

	x=mouse_x;
	y=mouse_y;

	width=pspfb_pixelsize*(arrow_width+x>pspfb_xsize?pspfb_xsize-x:arrow_width);
	height=arrow_height+y>pspfb_ysize?pspfb_ysize-y:arrow_height;

	for (;height;height--){
		mouse_getscansegment(buffer_ptr,x,y,width);
		buffer_ptr+=skip;
		y++;
	}
}

/* Overlays the arrow's image over the mouse_buffer
 * Doesn't draw anything into the screen
 */
static void render_mouse_arrow(void)
{
	int x,y, reg0, reg1;
	unsigned char *mouse_ptr=mouse_buffer;
	unsigned int *arrow_ptr=arrow;

	for (y=arrow_height;y;y--){
		reg0=*arrow_ptr;
		reg1=arrow_ptr[1];
		arrow_ptr+=2;
		for (x=arrow_width;x;)
		{
			int mask=1<<(--x);

			if (reg0&mask)
				memcpy (mouse_ptr, &mouse_black, pspfb_pixelsize);
			else if (reg1&mask)
				memcpy (mouse_ptr, &mouse_white, pspfb_pixelsize);
			mouse_ptr+=pspfb_pixelsize;
		}
	}
	s_bbDirty = truE;
}

static void place_mouse(void)
{
	struct bitmap bmp;

	bmp.x=arrow_width;
	bmp.y=arrow_height;
	bmp.skip=arrow_width*pspfb_pixelsize;
	bmp.data=mouse_buffer;
	{
		struct graphics_device * current_graphics_device_backup;
		current_graphics_device_backup=current_virtual_device;
		current_virtual_device=mouse_graphics_device;
		pspfb_draw_bitmap(mouse_graphics_device, &bmp, mouse_x, mouse_y);
		current_virtual_device=current_graphics_device_backup;
	}
	global_mouse_hidden=0;
}

/* Only when the old and the new mouse positions do not interfere. Using this routine
 * on interfering positions would cause a flicker.
 */
static void show_mouse(void)
{

	get_mouse_background(background_buffer);
	background_x=mouse_x;
	background_y=mouse_y;
	memcpy(mouse_buffer,background_buffer,pspfb_pixelsize*arrow_area);
	render_mouse_arrow();
	place_mouse();
}

/* Doesn't draw anything into the screen
 */
static void put_and_clip_background_buffer_over_mouse_buffer(void)
{
	unsigned char *bbufptr=background_buffer, *mbufptr=mouse_buffer;
	int left=background_x-mouse_x;
	int top=background_y-mouse_y;
	int right,bottom;
	int bmpixelsizeL=pspfb_pixelsize;
	int number_of_bytes;
	int byte_skip;

	right=left+arrow_width;
	bottom=top+arrow_height;

	if (left<0){
		bbufptr-=left*bmpixelsizeL;
		left=0;
	}
	if (right>arrow_width) right=arrow_width;
	if (top<0){
		bbufptr-=top*bmpixelsizeL*arrow_width;
		top=0;
	}
	if (bottom>arrow_height) bottom=arrow_height;
	mbufptr+=bmpixelsizeL*(left+arrow_width*top);
	byte_skip=arrow_width*bmpixelsizeL;
	number_of_bytes=bmpixelsizeL*(right-left);
	for (;top<bottom;top++){
		memcpy(mbufptr,bbufptr,number_of_bytes);
		mbufptr+=byte_skip;
		bbufptr+=byte_skip;
	}
}

/* This draws both the contents of background_buffer and mouse_buffer in a scan
 * way (left-right, top-bottom), so the flicker is reduced.
 */
static inline void place_mouse_composite(void)
{
	int mouse_left=mouse_x;
	int mouse_top=mouse_y;
	int background_left=background_x;
	int background_top=background_y;
	int mouse_right=mouse_left+arrow_width;
	int mouse_bottom=mouse_top+arrow_height;
	int background_right=background_left+arrow_width;
	int background_bottom=background_top+arrow_height;
	int skip=arrow_width*pspfb_pixelsize;
	int background_length,mouse_length;
	unsigned char *mouse_ptr=mouse_buffer,*background_ptr=background_buffer;
	int yend;

	if (mouse_bottom>pspfb_ysize) mouse_bottom=pspfb_ysize;
	if (background_bottom>pspfb_ysize) background_bottom=pspfb_ysize;

	/* Let's do the top part */
	if (background_top<mouse_top){
		/* Draw the background */
		background_length=background_right>pspfb_xsize?pspfb_xsize-background_left
			:arrow_width;
		for (;background_top<mouse_top;background_top++){
			mouse_drawscansegment(background_ptr,background_left
				,background_top,background_length*pspfb_pixelsize);
			background_ptr+=skip;
		}

	}else if (background_top>mouse_top){
		/* Draw the mouse */
		mouse_length=mouse_right>pspfb_xsize
			?pspfb_xsize-mouse_left:arrow_width;
		for (;mouse_top<background_top;mouse_top++){
			mouse_drawscansegment(mouse_ptr,mouse_left,mouse_top,mouse_length*pspfb_pixelsize);
			mouse_ptr+=skip;
		}
	}

	/* Let's do the middle part */
	yend=mouse_bottom<background_bottom?mouse_bottom:background_bottom;
	if (background_left<mouse_left){
		/* Draw background, mouse */
		mouse_length=mouse_right>pspfb_xsize?pspfb_xsize-mouse_left:arrow_width;
		for (;mouse_top<yend;mouse_top++){
			mouse_drawscansegment(background_ptr,background_left,mouse_top
				,(mouse_left-background_left)*pspfb_pixelsize);
			mouse_drawscansegment(mouse_ptr,mouse_left,mouse_top,mouse_length*pspfb_pixelsize);
			mouse_ptr+=skip;
			background_ptr+=skip;
		}

	}else{
		int l1, l2, l3;

		/* Draw mouse, background */
		mouse_length=mouse_right>pspfb_xsize?pspfb_xsize-mouse_left:arrow_width;
		background_length=background_right-mouse_right;
		if (background_length+mouse_right>pspfb_xsize)
			background_length=pspfb_xsize-mouse_right;
		l1=mouse_length*pspfb_pixelsize;
		l2=(mouse_right-background_left)*pspfb_pixelsize;
		l3=background_length*pspfb_pixelsize;
		for (;mouse_top<yend;mouse_top++){
			mouse_drawscansegment(mouse_ptr,mouse_left,mouse_top,l1);
			if (background_length>0)
				mouse_drawscansegment(
					background_ptr +l2,
				       	mouse_right,mouse_top ,l3);
			mouse_ptr+=skip;
			background_ptr+=skip;
		}
	}

	if (background_bottom<mouse_bottom){
		/* Count over bottoms! tops will be invalid! */
		/* Draw mouse */
		mouse_length=mouse_right>pspfb_xsize?pspfb_xsize-mouse_left
			:arrow_width;
		for (;background_bottom<mouse_bottom;background_bottom++){
			mouse_drawscansegment(mouse_ptr,mouse_left,background_bottom
				,mouse_length*pspfb_pixelsize);
			mouse_ptr+=skip;
		}
	}else{
		/* Draw background */
		background_length=background_right>pspfb_xsize?pspfb_xsize-background_left
			:arrow_width;
		for (;mouse_bottom<background_bottom;mouse_bottom++){
			mouse_drawscansegment(background_ptr,background_left,mouse_bottom
				,background_length*pspfb_pixelsize);
			background_ptr+=skip;
		}
	}
}

/* This moves the mouse a sophisticated way when the old and new position of the
 * cursor overlap.
 */
static inline void redraw_mouse_sophisticated(void)
{
	int new_background_x, new_background_y;

	get_mouse_background(mouse_buffer);
	put_and_clip_background_buffer_over_mouse_buffer();
	memcpy(new_background_buffer,mouse_buffer,pspfb_pixelsize*arrow_area);
	new_background_x=mouse_x;
	new_background_y=mouse_y;
	render_mouse_arrow();
	place_mouse_composite();
	memcpy(background_buffer,new_background_buffer,pspfb_pixelsize*arrow_area);
	background_x=new_background_x;
	background_y=new_background_y;
}

static void redraw_mouse(void){

	if (!pspfb_active) return; /* We are not drawing */
	if (mouse_x!=background_x||mouse_y!=background_y){
		if (RECTANGLES_INTERSECT(
			background_x, background_x+arrow_width,
			mouse_x, mouse_x+arrow_width,
			background_y, background_y+arrow_height,
			mouse_y, mouse_y+arrow_height)){
			redraw_mouse_sophisticated();
		}else{
			/* Do a normal hide/show */
			get_mouse_background(mouse_buffer);
			memcpy(new_background_buffer,
				mouse_buffer,arrow_area*pspfb_pixelsize);
			render_mouse_arrow();
			hide_mouse();
			place_mouse();
			memcpy(background_buffer,new_background_buffer
				,arrow_area*pspfb_pixelsize);
			background_x=mouse_x;
			background_y=mouse_y;
		}
	}
}

static void pspfb_switch_signal(void *data)
{
}

void pspSetMouse(int x, int y)
{
	mouse_x = x;
	mouse_y = y;

	redraw_mouse();

	s_bbDirty = truE;
}

void pspInputThread()
{
	static int oldButtonMask = 0;
	int deltax = 0, deltay = 0;
	static int danzeff_x = PSP_SCREEN_WIDTH/2-(64*3/2), danzeff_y = PSP_SCREEN_HEIGHT/2-(64*3/2);
	SceCtrlData pad;
	SceCtrlLatch latch;
	unsigned short	fl	= 0;

	if ( g_PSPEnableInput == truE )
	{

		if (g_PSPEnableRendering == falsE)
		{
			g_PSPEnableRendering = truE;
			cls_redraw_all_terminals();
		}

		sceCtrlReadBufferPositive(&pad, 1);
		sceCtrlReadLatch(&latch);

		RAND_add(&pad, sizeof(pad), sizeof(pad)); /** Add more randomness to SSL */

		if (sf_danzeffOn)
		{

			if (latch.uiMake)
			{
				// Button Pressed
				oldButtonMask = latch.uiPress;
			}
			else if (latch.uiBreak) /** Button Released */
			{
				if (oldButtonMask & PSP_CTRL_START)
				{
					/* Enter input */
					sf_danzeffOn = 0;
					danzeff_free();
					s_bbDirty = truE;
				}
				else if (oldButtonMask & PSP_CTRL_SELECT)
				{
					g_PSPEnableRendering = falsE;
					TakeScreenShot();
					wait_for_triangle("");
					g_PSPEnableRendering = truE;
					s_bbDirty = truE;
				}
				oldButtonMask = 0;
			}

			if (pad.Buttons & PSP_CTRL_LEFT)
			{
				danzeff_x-=5;
				danzeff_moveTo(danzeff_x, danzeff_y);
			}
			else if (pad.Buttons & PSP_CTRL_RIGHT)
			{
				danzeff_x+=5;
				danzeff_moveTo(danzeff_x, danzeff_y);
			}
			else
			{
				int key = 0;
				key = danzeff_readInput(pad);
				if (key)
				{
					switch (key)
					{
						case '\n':
							key = KBD_ENTER;
							break;
						case 8:
							key = KBD_BS;
							break;
					}
					current_virtual_device->keyboard_handler(current_virtual_device, key, 0);
				}
			}
			if (sf_danzeffOn && danzeff_dirty())
			{
				s_bbDirty = truE;
			}
		}
		else
		{
			if  (pad.Lx < 128)
			{
				deltax = -(128 - pad.Lx)/g_PSPConfig.mouse_speed_factor;//30;
			}
			else
			{
				deltax = (pad.Lx - 128)/g_PSPConfig.mouse_speed_factor;
			}

			if  (pad.Ly < 128)
			{
				deltay = - (128 - pad.Ly)/g_PSPConfig.mouse_speed_factor;
			}
			else
			{
				deltay = (pad.Ly - 128)/g_PSPConfig.mouse_speed_factor;
			}

			if (pad.Buttons & PSP_CTRL_LTRIGGER)
			{
				s_BbRowOffset += deltay;
				s_BbColOffset += deltax;

				if (s_BbRowOffset < 0) s_BbRowOffset = 0;
				if (s_BbColOffset < 0) s_BbColOffset = 0;
				if (s_BbColOffset > (g_PSPConfig.screen_zoom_factor*PSP_SCREEN_WIDTH - PSP_SCREEN_WIDTH))  
					s_BbColOffset = (g_PSPConfig.screen_zoom_factor*PSP_SCREEN_WIDTH - PSP_SCREEN_WIDTH);
				if (s_BbRowOffset > (g_PSPConfig.screen_zoom_factor*PSP_SCREEN_HEIGHT - PSP_SCREEN_HEIGHT)) 
					s_BbRowOffset = (g_PSPConfig.screen_zoom_factor*PSP_SCREEN_HEIGHT - PSP_SCREEN_HEIGHT);

				s_bbDirty = truE;
			}
			else
			{
				fl	= B_MOVE;
				if (pad.Buttons & PSP_CTRL_CROSS)
				{
					fl = B_DRAG | B_LEFT;
				}
				else if (pad.Buttons & PSP_CTRL_TRIANGLE)
				{
					fl = B_DRAG | B_RIGHT;
				}
				/* calls handler */
				pspfb_mouse_move(deltax, deltay, fl);
			}


			if (latch.uiMake)
			{
				// Button Pressed
				oldButtonMask = latch.uiPress;
				if (oldButtonMask & PSP_CTRL_CROSS)
				{
					fl	= B_DOWN | B_LEFT;
					if (current_virtual_device) current_virtual_device->mouse_handler(current_virtual_device, mouse_x, mouse_y, fl);
				}
				else if (oldButtonMask & PSP_CTRL_TRIANGLE)
				{
					fl	= B_DOWN | B_RIGHT;
					if (current_virtual_device) current_virtual_device->mouse_handler(current_virtual_device, mouse_x, mouse_y, fl);
				}
			}
			else if (latch.uiBreak) /** Button Released */
			{
				if (oldButtonMask & PSP_CTRL_SELECT)
				{
					if (oldButtonMask & PSP_CTRL_CROSS)
					{
						pspDebugScreenInit();
						wifiChooseConnect();
						s_bbDirty = truE;
					}
					if (oldButtonMask & PSP_CTRL_SQUARE)
					{
						pspDebugScreenInit();
						cleanup_cookies();
						init_cookies();
						wait_for_triangle("Cookies saved to disk");
					}
				}
				else if (oldButtonMask & PSP_CTRL_DOWN)
				{
					if (oldButtonMask & PSP_CTRL_RTRIGGER)
					{
						if (current_virtual_device) current_virtual_device->keyboard_handler(current_virtual_device, KBD_PAGE_DOWN, fl);
					}
					else
					{
						//if (current_virtual_device) current_virtual_device->keyboard_handler(current_virtual_device, KBD_DEL, fl);
						if (current_virtual_device) current_virtual_device->keyboard_handler(current_virtual_device, KBD_DOWN, fl);
					}
				}
				else if (oldButtonMask & PSP_CTRL_UP)
				{
					if (oldButtonMask & PSP_CTRL_RTRIGGER)
					{
						if (current_virtual_device) current_virtual_device->keyboard_handler(current_virtual_device, KBD_PAGE_UP, fl);
					}
					else
					{
						//if (current_virtual_device) current_virtual_device->keyboard_handler(current_virtual_device, KBD_INS, fl);
						if (current_virtual_device) current_virtual_device->keyboard_handler(current_virtual_device, KBD_UP, fl);
					}
				}
				else if (oldButtonMask & PSP_CTRL_LEFT)
				{
					if (oldButtonMask & PSP_CTRL_RTRIGGER)
					{
						if (current_virtual_device) current_virtual_device->keyboard_handler(current_virtual_device, '[', fl);
					}
					else
					{
						if (current_virtual_device) current_virtual_device->keyboard_handler(current_virtual_device, KBD_LEFT, fl);
					}
				}
				else if (oldButtonMask & PSP_CTRL_RIGHT)
				{
					if (oldButtonMask & PSP_CTRL_RTRIGGER)
					{
						if (current_virtual_device) current_virtual_device->keyboard_handler(current_virtual_device, ']', fl);
					}
					else
					{
						if (current_virtual_device) current_virtual_device->keyboard_handler(current_virtual_device, KBD_RIGHT, fl);
					}
				}
				//else if (oldButtonMask & PSP_CTRL_LTRIGGER)
				//{
				//}
				//else if (oldButtonMask & PSP_CTRL_RTRIGGER)
				//{
				//}
				else if (oldButtonMask & PSP_CTRL_CROSS)
				{
					fl	= B_UP | B_LEFT;
					if (current_virtual_device) current_virtual_device->mouse_handler(current_virtual_device, mouse_x, mouse_y, fl);
				}
				else if (oldButtonMask & PSP_CTRL_SQUARE)
				{
					if (current_virtual_device) current_virtual_device->keyboard_handler(current_virtual_device, KBD_ESC, fl);
				}
				else if (oldButtonMask & PSP_CTRL_TRIANGLE)
				{
					fl	= B_UP | B_RIGHT;
					if (current_virtual_device) current_virtual_device->mouse_handler(current_virtual_device, mouse_x, mouse_y, fl);
				}
				else if (oldButtonMask & PSP_CTRL_CIRCLE)
				{
					if (current_virtual_device) current_virtual_device->keyboard_handler(current_virtual_device, KBD_ENTER, fl);
				}
				else if (oldButtonMask & PSP_CTRL_START)
				{
					if (!danzeff_isinitialized())
						{
						danzeff_load();
						}
					//current_virtual_device->keyboard_handler(current_virtual_device, 'g', fl);
					if (danzeff_isinitialized())
					{
						danzeff_moveTo(danzeff_x, danzeff_y);
						//danzeff_render();
						sf_danzeffOn = 1;
						s_bbDirty = truE;
					}
					else
					{
						wait_for_triangle("Error loading danzeff OSK");
					}
				}
				else if (oldButtonMask & PSP_CTRL_SELECT)
				{
					g_PSPEnableRendering = falsE;
					TakeScreenShot();
					wait_for_triangle("");
					g_PSPEnableRendering = truE;
					s_bbDirty = truE;
					//cls_redraw_all_terminals();
				}

				if (s_Zoom == falsE)
				{
					if (oldButtonMask & PSP_CTRL_LTRIGGER)
					{
						if (oldButtonMask & PSP_CTRL_CROSS)
						{
							s_Zoom = truE;
						}
						s_bbDirty = truE;
					}
				}
				else
				{
					if (oldButtonMask & PSP_CTRL_LTRIGGER)
					{
						if (oldButtonMask & PSP_CTRL_CROSS)
						{
							s_Zoom = falsE;
						}
						s_bbDirty = truE;
					}
				}

				oldButtonMask = 0;
			}
		}
	}
	//ceKernelDelayThread(11*1000); /* Wait 1ms */
	sceKernelDelayThread(1); /* yield */

	// Restart Input Timer
	install_timer(10, pspInputThread, NULL);
}


void render_thread()
{
#ifdef PSP_16BPP
	short *pBb; /* Back buffer */
	short *pFb;/* Front/Frame buffer */
#else /* 32bpp */
	int *pBb; /* Back buffer */
	int *pFb; /* Front/Frame buffer */
#endif
	pBb = pspfb_mem; /* Back buffer */
	SceCtrlData pad;
	int fbRow, fbCol;
	int bbRow, bbCol;
	int bbLineSize = PSP_SCREEN_WIDTH*g_PSPConfig.screen_zoom_factor;
	int fbLineSize = PSP_LINE_SIZE;
	int fbMult, bbMult;
	int bb_to_fb_factor = 1;
	int bb_row_offset = 0, bb_col_offset = 0;
	int one = 1;

	for(;;)
	{
		sceDisplayWaitVblankStart();

		if (g_PSPEnableRendering && s_bbDirty)
		{
			if (one++%2)
			{
				pFb = psp_fb1;    /* Front/Frame buffer */
				psp_fb = psp_fb2;
			}
			else
			{
				pFb = psp_fb2;
				psp_fb = psp_fb1;
			}

			sceCtrlPeekBufferPositive(&pad, 1);

			if ((pad.Buttons & PSP_CTRL_LTRIGGER) || s_Zoom)
			{
				bb_to_fb_factor = 1;
				bb_row_offset = s_BbRowOffset;
				bb_col_offset = s_BbColOffset;
			}
			else
			{
				/** Display the whole backbuffer (shrinking if necessary..) */
				bb_to_fb_factor = g_PSPConfig.screen_zoom_factor;
				bb_row_offset = 0;
				bb_col_offset = 0;
			}

			for (fbRow = 0, bbRow = bb_row_offset; fbRow < PSP_SCREEN_HEIGHT; fbRow++, bbRow+=bb_to_fb_factor)
			{
				fbMult = fbRow*fbLineSize;
				bbMult = bbRow*bbLineSize;
				for (fbCol = 0, bbCol = bb_col_offset; fbCol < PSP_SCREEN_WIDTH; fbCol++, bbCol+=bb_to_fb_factor)
				{
				   pFb[fbMult+fbCol] = pBb[bbMult+bbCol];
				}
			}
	
			if (sf_danzeffOn)
			{
				/* Pass VRAM info to Danzeff */
				danzeff_set_screen(pFb, PSP_LINE_SIZE, pspfb_ysize, pspfb_pixelsize);
				danzeff_render();
			}

			/* flipping */
			sceDisplaySetFrameBuf((void *) pFb, PSP_LINE_SIZE, PSP_PIXEL_FORMAT, 1);


			s_bbDirty = falsE;
		}

		sceKernelDelayThread(1); /* yield */
	}

}

void psp_reset_graphic_mode()
{
	/* (Re)set graphic mode */
	sceDisplaySetMode(0, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);
	sceDisplaySetFrameBuf((void *) psp_fb, PSP_LINE_SIZE, PSP_PIXEL_FORMAT, 1);
}

static unsigned char *pspfb_init_driver(unsigned char *param, unsigned char *ignore)
{
	unsigned char *e;
	struct stat st;
	pspfb_old_vd = NULL;
	ignore=ignore;
	pspfb_driver_param=NULL;
	if(param != NULL)
		pspfb_driver_param=stracpy(param);

	border_left = border_right = border_top = border_bottom = 0;

	pspfb_console = st.st_rdev & 0xff;

	pspfb_xsize=PSP_SCREEN_WIDTH*g_PSPConfig.screen_zoom_factor;
	pspfb_ysize=PSP_SCREEN_HEIGHT*g_PSPConfig.screen_zoom_factor;

#ifdef PSP_16BPP
	pspfb_bits_pp=16;
	pspfb_pixelsize=2;
#else /* 32bpp */
	pspfb_bits_pp=32;
	pspfb_pixelsize=4;
#endif
	pspfb_driver.x=pspfb_xsize;
	pspfb_driver.y=pspfb_ysize;
	pspfb_colors=1<<pspfb_bits_pp;

	pspfb_linesize=PSP_SCREEN_WIDTH*pspfb_pixelsize*g_PSPConfig.screen_zoom_factor;
	pspfb_mem_size=pspfb_linesize * pspfb_ysize;

	if (init_virtual_devices(&pspfb_driver, NUMBER_OF_DEVICES))
	{
		if(pspfb_driver_param) { mem_free(pspfb_driver_param); pspfb_driver_param=NULL; }
		return stracpy("Allocation of virtual devices failed.\n");
	}

	/* Mikulas: nechodi to na sparcu */
	if (pspfb_mem_size < pspfb_linesize * pspfb_ysize)
	{
		shutdown_virtual_devices();
		if(pspfb_driver_param) { mem_free(pspfb_driver_param); pspfb_driver_param=NULL; }
		return stracpy("Nonlinear mapping of graphics memory not supported.\n");
	}

	/* Place vram in uncached memory */
	psp_fb1 = (u32 *) (0x40000000 | (u32) sceGeEdramGetAddr());
	psp_fb2 = (u32 *)((char*)psp_fb1 + FRAMESIZE);
	psp_fb = psp_fb1;

	psp_reset_graphic_mode();

	pspfb_mem = (char *) malloc(pspfb_mem_size);

	pspfb_vmem = pspfb_mem + border_left * pspfb_pixelsize + border_top * pspfb_linesize;

#ifdef PSP_16BPP
	pspfb_driver.depth = 131;
#else /* 32bpp */
	pspfb_driver.depth=pspfb_pixelsize&7;
	pspfb_driver.depth|=(24/*pspfb_bits_pp*/&31)<<3;
	if (htons (0x1234) == 0x1234) pspfb_driver.depth |= 0x100;
#endif

	pspfb_driver.get_color=get_color_fn(pspfb_driver.depth);

	/* Pass VRAM info to Danzeff */
	danzeff_set_screen(psp_fb, PSP_LINE_SIZE, pspfb_ysize, pspfb_pixelsize);

	/* mouse */
	mouse_buffer=mem_alloc(pspfb_pixelsize*arrow_area);
	background_buffer=mem_alloc(pspfb_pixelsize*arrow_area);
	new_background_buffer=mem_alloc(pspfb_pixelsize*arrow_area);
	background_x=mouse_x=pspfb_xsize>>1;
	background_y=mouse_y=pspfb_ysize>>1;
	mouse_black=pspfb_driver.get_color(0);
	mouse_white=pspfb_driver.get_color(0xffffff);
	mouse_graphics_device=pspfb_driver.init_device();
	virtual_devices[0] = NULL;
	global_mouse_hidden=1;

	/*if (border_left | border_top | border_right | border_bottom) */
	memset(pspfb_mem,0,pspfb_mem_size);
	memset(psp_fb1, 0, FRAMESIZE);
	memset(psp_fb2, 0, FRAMESIZE);

	show_mouse();
	s_bbDirty = truE;

	/* Start Render Thread */
	{
		pthread_t pthid;
		pthread_attr_t pthattr;
		struct sched_param shdparam;
		pthread_attr_init(&pthattr);
		shdparam.sched_policy = SCHED_OTHER;
		shdparam.sched_priority = 35;
		pthread_attr_setschedparam(&pthattr, &shdparam);
		pthread_create(&pthid, &pthattr, render_thread, NULL);
	}

	/* Start Input Timer */
	install_timer(10, pspInputThread, NULL);

	return NULL;
}

static void pspfb_shutdown_driver(void)
{
	mem_free(mouse_buffer);
	mem_free(background_buffer);
	mem_free(new_background_buffer);
	pspfb_driver.shutdown_device(mouse_graphics_device);

	memset(pspfb_mem,0,pspfb_mem_size);
	shutdown_virtual_devices();
	if(pspfb_driver_param) mem_free(pspfb_driver_param);
}


static unsigned char *pspfb_get_driver_param(void)
{
    return pspfb_driver_param;
}


/* Return value:        0 alloced on heap
 *                      1 alloced in vidram
 *                      2 alloced in X server shm
 */
static int pspfb_get_empty_bitmap(struct bitmap *dest)
{
	if (dest->x && (unsigned)dest->x * (unsigned)dest->y / (unsigned)dest->x != (unsigned)dest->y) overalloc();
	if ((unsigned)dest->x * (unsigned)dest->y > (unsigned)MAXINT / pspfb_pixelsize) overalloc();
	dest->data=mem_alloc(dest->x*dest->y*pspfb_pixelsize);
	dest->skip=dest->x*pspfb_pixelsize;
	dest->flags=0;
	return 0;
}

static void pspfb_register_bitmap(struct bitmap *bmp)
{
	s_bbDirty = truE;
}

static void pspfb_unregister_bitmap(struct bitmap *bmp)
{
	mem_free(bmp->data);
}

static void *pspfb_prepare_strip(struct bitmap *bmp, int top, int lines)
{
	return ((char *)bmp->data)+bmp->skip*top;
}

static void pspfb_commit_strip(struct bitmap *bmp, int top, int lines)
{
	s_bbDirty = truE;
	return;
}

void pspfb_draw_bitmap(struct graphics_device *dev,struct bitmap* hndl, int x, int y)
{
	unsigned char *scr_start;

	CLIP_PREFACE

	scr_start=pspfb_vmem+y*pspfb_linesize+x*pspfb_pixelsize;
	for(;ys;ys--){
		memcpy(scr_start,data,xs*pspfb_pixelsize);
		data+=hndl->skip;
		scr_start+=pspfb_linesize;
	}
	END_GR
	s_bbDirty = truE;
}

static void pspfb_draw_bitmaps(struct graphics_device *dev, struct bitmap **hndls, int n, int x, int y)
{
	TEST_INACTIVITY

	if (x>=pspfb_xsize||y>pspfb_ysize) return;
	while(x+(*hndls)->x<=0&&n){
		x+=(*hndls)->x;
		n--;
		hndls++;
	}
	while(n&&x<=pspfb_xsize){
		pspfb_draw_bitmap(dev, *hndls, x, y);
		x+=(*hndls)->x;
		n--;
		hndls++;
	}
}

static void pspfb_fill_area(struct graphics_device *dev, int left, int top, int right, int bottom, long color)
{
	unsigned char *dest;
	int y;

	FILL_CLIP_PREFACE

	dest=pspfb_vmem+top*pspfb_linesize+left*pspfb_pixelsize;
	for (y=bottom-top;y;y--){
		pixel_set(dest,(right-left)*pspfb_pixelsize,&color);
		dest+=pspfb_linesize;
	}
	END_GR
	s_bbDirty = truE;
}

static void pspfb_draw_hline(struct graphics_device *dev, int left, int y, int right, long color)
{
	unsigned char *dest;
	HLINE_CLIP_PREFACE

	dest=pspfb_vmem+y*pspfb_linesize+left*pspfb_pixelsize;
	pixel_set(dest,(right-left)*pspfb_pixelsize,&color);
	END_GR
	s_bbDirty = truE;
}

static void pspfb_draw_vline(struct graphics_device *dev, int x, int top, int bottom, long color)
{
	unsigned char *dest;
	int y;
	VLINE_CLIP_PREFACE

	dest=pspfb_vmem+top*pspfb_linesize+x*pspfb_pixelsize;
	for (y=(bottom-top);y;y--){
		memcpy(dest,&color,pspfb_pixelsize);
		dest+=pspfb_linesize;
	}
	END_GR
	s_bbDirty = truE;
}

static int pspfb_hscroll(struct graphics_device *dev, struct rect_set **ignore, int sc)
{
	unsigned char *dest, *src;
	int y;
	int len;
	HSCROLL_CLIP_PREFACE

	ignore=NULL;
	if (sc>0){
		len=(dev->clip.x2-dev->clip.x1-sc)*pspfb_pixelsize;
		src=pspfb_vmem+pspfb_linesize*dev->clip.y1+dev->clip.x1*pspfb_pixelsize;
		dest=src+sc*pspfb_pixelsize;
		for (y=dev->clip.y2-dev->clip.y1;y;y--){
			memmove(dest,src,len);
			dest+=pspfb_linesize;
			src+=pspfb_linesize;
		}
	}else{
		len=(dev->clip.x2-dev->clip.x1+sc)*pspfb_pixelsize;
		dest=pspfb_vmem+pspfb_linesize*dev->clip.y1+dev->clip.x1*pspfb_pixelsize;
		src=dest-sc*pspfb_pixelsize;
		for (y=dev->clip.y2-dev->clip.y1;y;y--){
			memmove(dest,src,len);
			dest+=pspfb_linesize;
			src+=pspfb_linesize;
		}
	}
	END_GR
	s_bbDirty = truE;
	return 1;
}

static int pspfb_vscroll(struct graphics_device *dev, struct rect_set **ignore, int sc)
{
	unsigned char *dest, *src;
	int y;
	int len;

	VSCROLL_CLIP_PREFACE

	ignore=NULL;
	len=(dev->clip.x2-dev->clip.x1)*pspfb_pixelsize;
	if (sc>0){
		/* Down */
		dest=pspfb_vmem+(dev->clip.y2-1)*pspfb_linesize+dev->clip.x1*pspfb_pixelsize;
		src=dest-pspfb_linesize*sc;
		for (y=dev->clip.y2-dev->clip.y1-sc;y;y--){
			memcpy(dest,src,len);
			dest-=pspfb_linesize;
			src-=pspfb_linesize;
		}
	}else{
		/* Up */
		dest=pspfb_vmem+dev->clip.y1*pspfb_linesize+dev->clip.x1*pspfb_pixelsize;
		src=dest-pspfb_linesize*sc;
		for (y=dev->clip.y2-dev->clip.y1+sc;y;y--){
			memcpy(dest,src,len);
			dest+=pspfb_linesize;
			src+=pspfb_linesize;
		}
	}
	END_GR
	s_bbDirty = truE;
	return 1;
}

static void pspfb_set_clip_area(struct graphics_device *dev, struct rect *r)
{
	memcpy(&dev->clip, r, sizeof(struct rect));
	if (dev->clip.x1>=dev->clip.x2||dev->clip.y2<=dev->clip.y1||dev->clip.y2<=0||dev->clip.x2<=0||dev->clip.x1>=pspfb_xsize
			||dev->clip.y1>=pspfb_ysize){
		/* Empty region */
		dev->clip.x1=dev->clip.x2=dev->clip.y1=dev->clip.y2=0;
	}else{
		if (dev->clip.x1<0) dev->clip.x1=0;
		if (dev->clip.x2>pspfb_xsize) dev->clip.x2=pspfb_xsize;
		if (dev->clip.y1<0) dev->clip.y1=0;
		if (dev->clip.y2>pspfb_ysize) dev->clip.y2=pspfb_ysize;
	}
}

static int pspfb_block(struct graphics_device *dev)
{
	if (pspfb_old_vd) return 1;
	pspfb_old_vd = current_virtual_device;
	current_virtual_device=NULL;
	return 0;
}

static void pspfb_unblock(struct graphics_device *dev)
{
	current_virtual_device = pspfb_old_vd;
	pspfb_old_vd = NULL;
	if (border_left | border_top | border_right | border_bottom) memset(pspfb_mem,0,pspfb_mem_size);
	if (current_virtual_device) current_virtual_device->redraw_handler(current_virtual_device
			,&current_virtual_device->size);
}


struct graphics_driver pspfb_driver={
	"pspgu",
	pspfb_init_driver,
	init_virtual_device,
	shutdown_virtual_device,
	pspfb_shutdown_driver,
	pspfb_get_driver_param,
	pspfb_get_empty_bitmap,
	/*pspfb_get_filled_bitmap,*/
	pspfb_register_bitmap,
	pspfb_prepare_strip,
	pspfb_commit_strip,
	pspfb_unregister_bitmap,
	pspfb_draw_bitmap,
	pspfb_draw_bitmaps,
	NULL,	/* pspfb_get_color */
	pspfb_fill_area,
	pspfb_draw_hline,
	pspfb_draw_vline,
	pspfb_hscroll,
	pspfb_vscroll,
	pspfb_set_clip_area,
	pspfb_block,
	pspfb_unblock,
	NULL,	/* set_title */
	NULL, /* exec */
	0,				/* depth (filled in pspfb_init_driver function) */
	0, 0,				/* size (in X is empty) */
	GD_DONT_USE_SCROLL|GD_NEED_CODEPAGE,		/* flags */
	0,				/* codepage */
	NULL,				/* shell */
};

#endif /* GRDRV_PSPGU */
