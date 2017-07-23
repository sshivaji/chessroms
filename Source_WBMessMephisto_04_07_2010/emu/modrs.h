#ifndef MODRS_H
#define MODRS_H

#define UNLIMITED	1

//#define POSIX 1

#define INPUT_TH	1

#define FALSE 0
#define TRUE 1

#if !defined (INFINITE)
	#define INFINITE            0xFFFFFFFF  
#endif

//Windows
//
#if defined (_WIN32) || defined(_WIN64)
#include <windows.h>
#include <windowsx.h>
#include <time.h>
#include <sys/timeb.h>
//Linux
//
#else

#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/timeb.h>

#define Sleep(ms) usleep(ms*1000)

#endif

#if defined(_MSC_VER)
#define va_start	_crt_va_start
#define va_arg		_crt_va_arg
#define va_end		_crt_va_end
#include <vadefs.h>

#define UINT64 unsigned __int64
#define UINT32 unsigned __int32
#define UINT16 unsigned __int16
#define INT16 __int16
#define UINT8 unsigned __int8
#define INT8 __int8

#endif

#if defined(__GNUC__)
#include <stdarg.h>

//#define UINT64 uint64_t
//#define UINT32 uint32_t
//#define UINT16 uint16_t
//#define INT16  int16_t
//#define UINT8  uint8_t
//#define INT8   int8_t

#endif

// Zeitsteuerung
//
#ifdef linux		//Linux

#define TIME_STRUCT					timeval	
#define GetTimeofday(x)				gettimeofday(&x, 0);
#define GetTimeUsed(x,y)			(int) (((y.tv_sec * 1000000) + y.tv_usec) - 	\
										   ((x.tv_sec * 1000000) + x.tv_usec)) / 1000
#define Getms(x)					(UINT64) ((x.tv_sec * 1000000) + x.tv_usec) / 1000

// einfacher -> x.tv_sec * 1000 + (x.tv_usec / 1000);

#else				//Windows

#define TIME_STRUCT					timeb			
#define GetTimeofday(x)				ftime(&x)
#define GetTimeUsed(x,y)			(int) (((y.time * 1000) + y.millitm) - 	\
										   ((x.time * 1000) + x.millitm)) 
#define Getms(x)					(UINT64) ((x.time * 1000) + x.millitm)

#endif

//typedef struct statistics{ 
//	UINT64 cpuexec_timeslice;
//	UINT64 timercb;
//	UINT64 starttime;
//	UINT64 endtime;
//}STAT_T;

typedef struct timecontrol {
	UINT32 fixmovetime;
	UINT32 wtime;
	UINT32 winc;
	UINT32 btime;
	UINT32 binc;
	UINT32 inc;
	UINT32 time;
	UINT32 otim;
	UINT32 movestogo;
	UINT32 movestogo_start;
	 INT32 movetime;
	UINT64 starttime;
	int side;
}TC_T;

#define	TIME_MOVE_DIV		35				// Zeitkontrolle Vermutete Anzahl Züge
#define TC_DELAY_REF		94				// Referenzwert zur Berechnung Korrekturwert Zeitkontroll (=Emulatorzeit von einer s auf Q6600 mit gcc compiliert) 
											// Sollte eigentlich abhänigig von der Emulatorgeschwindigkeit berechnet werden.
// Ausgabe der Infoanzeige
//
typedef struct xcmd_info_struct
{
	char ply[10];
	char score[10];
	char time[10];
	char nodes[10];
	char PV[20];
}XCMD_INFO_T;

// Daten über Tasten
//
typedef struct emu_key_struct
{
	char name[10];
	int data;
}EMU_KEY_T;

#include <stdio.h>
#include <string.h>

#define DRIVER_START	0
#define DRIVER_READY	1
#define SEARCHING		2
#define PARSEINPUT		3
#define SENDCOMMAND		4
#define SPECIALCOMMANDS 5
#define BESTMOVE		6
#define BESTMOVEPROMO	7

#define MM_KEYS				0
#define GLASGOW_KEYS		1
#define GLASGOW_NEW_KEYS	2

#define EMU_MM			0
#define EMU_GLASGOW		1

extern int g_state;
extern int g_error;
extern int g_displayChanged;
extern int g_portIsReady;

extern unsigned int	g_inputCnt;
extern unsigned int	g_waitCnt;

extern int g_bestmoveWait;
extern int g_inputWait;
extern int g_promoWait;
extern int g_inputTimeout;

extern char g_debug;
extern char g_display[10];
extern char g_segment[128];

extern char g_logfile[20];
extern int g_mmlog;

extern int g_unlimited;
extern int g_clock;

extern int g_perf;
extern int g_profiler;

extern int g_xboard_mode;

////extern STAT_T g_stat;

void Log(const char *string, ...);
void PrintAndLog(const char *string, ...);
int TestMove( char* move);
char *PrintState(int inp);
void SendToGUI(char* cmd);

#endif  //MODRS_H
