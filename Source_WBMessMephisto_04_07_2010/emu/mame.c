/***************************************************************************

    mame.c

    Controls execution of the core MAME system.

    Copyright Nicola Salmoria and the MAME Team.
    Visit http://mamedev.org for licensing and usage restrictions.

****************************************************************************

    Since there has been confusion in the past over the order of
    initialization and other such things, here it is, all spelled out
    as of January, 2008:

    main()
        - does platform-specific init
        - calls mame_execute() [mame.c]

        mame_execute() [mame.c]
            - calls mame_validitychecks() [validity.c] to perform validity checks on all compiled drivers
            - begins resource tracking (level 1)
            - calls create_machine [mame.c] to initialize the running_machine structure
            - calls init_machine() [mame.c]

            init_machine() [mame.c]
                - calls fileio_init() [fileio.c] to initialize file I/O info
                - calls config_init() [config.c] to initialize configuration system
                - calls input_init() [input.c] to initialize the input system
                - calls output_init() [output.c] to initialize the output system
                - calls state_init() [state.c] to initialize save state system
                - calls state_save_allow_registration() [state.c] to allow registrations
                - calls palette_init() [palette.c] to initialize palette system
                - calls render_init() [render.c] to initialize the rendering system
                - calls ui_init() [ui.c] to initialize the user interface
                - calls generic_machine_init() [machine/generic.c] to initialize generic machine structures
                - calls generic_video_init() [video/generic.c] to initialize generic video structures
                - calls generic_sound_init() [audio/generic.c] to initialize generic sound structures
                - calls timer_init() [timer.c] to reset the timer system
                - calls osd_init() [osdepend.h] to do platform-specific initialization
                - calls input_port_init() [inptport.c] to set up the input ports
                - calls rom_init() [romload.c] to load the game's ROMs
                - calls memory_init() [memory.c] to process the game's memory maps
                - calls cpuexec_init() [cpuexec.c] to initialize the CPUs
                - calls watchdog_init() [watchdog.c] to initialize the watchdog system
                - calls the driver's DRIVER_INIT callback
                - calls device_list_start() [devintrf.c] to start any devices
                - calls video_init() [video.c] to start the video system
                - calls tilemap_init() [tilemap.c] to start the tilemap system
                - calls crosshair_init() [crsshair.c] to configure the crosshairs
                - calls sound_init() [sound.c] to start the audio system
                - calls debugger_init() [debugger.c] to set up the debugger
                - calls the driver's MACHINE_START, SOUND_START, and VIDEO_START callbacks
                - calls saveload_init() [mame.c] to set up for save/load
                - calls cheat_init() [cheat.c] to initialize the cheat system

            - calls config_load_settings() [config.c] to load the configuration file
            - calls nvram_load [machine/generic.c] to load NVRAM
            - calls ui_display_startup_screens() [ui.c] to display the the startup screens
            - begins resource tracking (level 2)
            - calls soft_reset() [mame.c] to reset all systems

                -------------------( at this point, we're up and running )----------------------

            - calls cpuexec_timeslice() [cpuexec.c] over and over until we exit
            - ends resource tracking (level 2), freeing all auto_mallocs and timers
            - calls the nvram_save() [machine/generic.c] to save NVRAM
            - calls config_save_settings() [config.c] to save the game's configuration
            - calls all registered exit routines [mame.c]
            - ends resource tracking (level 1), freeing all auto_mallocs and timers

        - exits the program

***************************************************************************/

#include "emu.h"
#include "emuopts.h"
#include "osdepend.h"
#include "config.h"
#include "debugger.h"
#include "profiler.h"
#include "render.h"
#include "cheat.h"
#include "ui.h"
#include "uimenu.h"
#include "uiinput.h"
#include "streams.h"
#include "crsshair.h"
#include "validity.h"
#include "debug/debugcon.h"

#include <time.h>

#include <stdarg.h>		//MOD RS
#include <setjmp.h>		//MOD RS
#include <time.h>		//MOD RS

#include <ctype.h>		//MOD RS

/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

typedef struct _callback_item callback_item;
struct _callback_item
{
	callback_item *	next;
	union
	{
		void		(*exit)(running_machine *);
		void		(*reset)(running_machine *);
		void		(*frame)(running_machine *);
		void		(*pause)(running_machine *, int);
		void		(*log)(running_machine *, const char *);
	} func;
};


/* typedef struct _mame_private mame_private; */
struct _mame_private
{
	/* system state */
	int				current_phase;
	UINT8			paused;
	UINT8			hard_reset_pending;
	UINT8			exit_pending;
	const game_driver *new_driver_pending;
	astring			saveload_pending_file;
	const char *	saveload_searchpath;
	emu_timer *		soft_reset_timer;
	mame_file *		logfile;

	/* callbacks */
	callback_item *	frame_callback_list;
	callback_item *	reset_callback_list;
	callback_item *	pause_callback_list;
	callback_item *	exit_callback_list;
	callback_item *	logerror_callback_list;

	/* load/save */
	void			(*saveload_schedule_callback)(running_machine *);
	attotime		saveload_schedule_time;

	/* random number seed */
	UINT32			rand_seed;

	/* base time */
	time_t			base_time;
};



/***************************************************************************
    GLOBAL VARIABLES
***************************************************************************/

/* the active machine */
static running_machine *global_machine;

/* the current options */
static core_options *mame_opts;

/* started empty? */
static UINT8 started_empty;

/* output channels */
static output_callback_func output_cb[OUTPUT_CHANNEL_COUNT];
static void *output_cb_param[OUTPUT_CHANNEL_COUNT];

/* the "disclaimer" that should be printed when run with no parameters */
const char mame_disclaimer[] =
	"MAME is an emulator: it reproduces, more or less faithfully, the behaviour of\n"
	"several arcade machines. But hardware is useless without software, so an image\n"
	"of the ROMs which run on that hardware is required. Such ROMs, like any other\n"
	"commercial software, are copyrighted material and it is therefore illegal to\n"
	"use them if you don't own the original arcade machine. Needless to say, ROMs\n"
	"are not distributed together with MAME. Distribution of MAME together with ROM\n"
	"images is a violation of copyright law and should be promptly reported to the\n"
	"authors so that appropriate legal action can be taken.\n";

/* a giant string buffer for temporary strings */
static char giant_string_buffer[65536] = { 0 };



/***************************************************************************
    FUNCTION PROTOTYPES
***************************************************************************/

static int parse_ini_file(core_options *options, const char *name, int priority);

static void init_machine(running_machine *machine);
static TIMER_CALLBACK( soft_reset );

static void saveload_init(running_machine *machine);
static void handle_save(running_machine *machine);
static void handle_load(running_machine *machine);

static void logfile_callback(running_machine *machine, const char *buffer);



/***************************************************************************
    CORE IMPLEMENTATION
***************************************************************************/

/*-------------------------------------------------
    eat_all_cpu_cycles - eat a ton of cycles on
    all CPUs to force a quick exit
-------------------------------------------------*/

INLINE void eat_all_cpu_cycles(running_machine *machine)
{
	running_device *cpu;

    if(machine->cpuexec_data)
		for (cpu = machine->firstcpu; cpu != NULL; cpu = cpu_next(cpu))
			cpu_eat_cycles(cpu, 1000000000);
}



//--------------------------------------------------------------------------
// Begin of   MOD RS
//--------------------------------------------------------------------------

#include "modrs.h"

static int isPromoInput(char* move);
static int isPromoCmd(char *cmd);
static int isPromoPiece(char p);
static int checkPromo(char *move);
static EMU_KEY_T *GetKeycodeGlasgow(char inp);
static EMU_KEY_T *GetKeycodeGlasgowNew(char inp);
static EMU_KEY_T *GetKeycodeMM(char inp);
static EMU_KEY_T *GetKeycode(int keys, char inp);
static int CmdFromFEN(running_machine *machine, char * FEN, char * cmd, int force);
static void addChar(char *str, char first);
static UINT64 GetTime(void);
static void clearTC(void);
static void TimeControl(TC_T *p_tc);
static int TimeOver(TC_T *p_tc);
static void PrintLevel();
static void LogProfiler(running_machine *machine);
static void ProcessDRIVER_START(running_machine *machine);
static void ProcessSEARCHING(running_machine *machine);
static void ProcessDRIVER_READY(void);
static void ProcessPARSEINPUT(running_machine *machine);
static void ProcessSENDCOMMAND(running_machine *machine);
static void ProcessSPECIALCOMMANDS(void);
static void ProcessBESTMOVE(void);
static void ProcessBESTMOVEPROMO(running_machine *machine);
static void ProcessSPECIALCOMMANDS(void);


// Posix Threads
//
#if defined linux

#include <pthread.h>

#define Thandle pthread_t

#define THREAD_PROC_RET_TYPE void*
#define THREAD_PROC_RET void*

#define BeginThread(thread,function,arg,dwThreadID) dwThreadID=pthread_create(&thread,NULL,function,(void*)arg);

#define DeleteThread(thread)

#define PMutex(x)                   pthread_mutex_t x=PTHREAD_MUTEX_INITIALIZER
#define Ehandle(x)                  pthread_cond_t  x=PTHREAD_COND_INITIALIZER

#define CreateEventOrCond(x)
#define SetEventOrCond(x)			pthread_cond_signal(&x)

#define WaitInputProcessed			(pthread_cond_wait(&HInputProcessed, &MutexProcessed)==0 ? TRUE : FALSE)

#define WaitDisplayChanged			(pthread_cond_wait(&HDisplayChanged, &MutexDisplayChanged)==0 ? TRUE : FALSE)

#define Lock(x)						pthread_mutex_lock(&x)
#define Unlock(x)					pthread_mutex_unlock(&x)

#define LockCondMutex(x)			pthread_mutex_lock(&x)
#define UnLockCondMutex(x)			pthread_mutex_unlock(&x)

#define WaitTime 12

// Thread Events
//
Ehandle(HInputProcessed);
Ehandle(HInputAvailable);
Ehandle(HDisplayChanged);

PMutex(MutexProcessed);
PMutex(MutexAvailable);
PMutex(MutexDisplayChanged);

INLINE int WaitInputAvailable(int millisec)
{
	int ret;
	struct timeval    tp;
	struct timespec   abstime;

	if (millisec >= INFINITE)
		ret=pthread_cond_wait(&HInputAvailable, &MutexAvailable);
	else
	{		
		gettimeofday(&tp, NULL);
		
		abstime.tv_sec   = tp.tv_sec;;
		abstime.tv_nsec  = tp.tv_usec * 1000;
		abstime.tv_nsec +=  millisec * 1000000;
		
		ret = pthread_cond_timedwait(&HInputAvailable, &MutexAvailable, &abstime);
	}

	return (ret==0);
}


// Windows Threads
//
#else

#define Thandle HANDLE

#define THREAD_PROC_RET_TYPE DWORD
#define THREAD_PROC_RET THREAD_PROC_RET_TYPE WINAPI

#define BeginThread(hThread,function,arg,dwThreadID) {\
	hThread=CreateThread(NULL,0,function,(LPVOID)arg,0,(LPDWORD)&dwThreadID);\
	}
#define DeleteThread(thread) CloseHandle(thread)


#define PMutex(x)					int x
#define Ehandle(x)					HANDLE x
#define CreateEventOrCond(x)		x=CreateEvent(NULL,FALSE,FALSE,NULL)			//CreateEvent
#define SetEventOrCond(x)			SetEvent(x)
#define WaitInputAvailable(time)	(WaitForSingleObject(HInputAvailable,time)== WAIT_OBJECT_0 ? TRUE : FALSE)    //WaitForSingleObject
#define WaitInputProcessed			(WaitForSingleObject(HInputProcessed,INFINITE)== WAIT_OBJECT_0 ? TRUE : FALSE)//WaitForSingleObject
#define WaitDisplayChanged			(WaitForSingleObject(HDisplayChanged,INFINITE)== WAIT_OBJECT_0 ? TRUE : FALSE)//WaitForSingleObject



#define Lock(mutex)					EnterCriticalSection(&mutex)
#define Unlock(mutex)				LeaveCriticalSection(&mutex)

#define LockCondMutex(mutex)	
#define UnLockCondMutex(mutex)		

#define WaitTime 1

// Thread Events
//
Ehandle(HInputProcessed);
Ehandle(HInputAvailable);
Ehandle(HDisplayChanged);

PMutex(MutexProcessed);
PMutex(MutexAvailable);
PMutex(MutexDisplayChanged);

#endif

INLINE void InputAvailable(void)
{
	LockCondMutex(MutexAvailable);					 
	SetEventOrCond(HInputAvailable);				 
    UnLockCondMutex(MutexAvailable);				 
}

INLINE void InputProcessed(void)
{
	LockCondMutex(MutexProcessed);					 
	SetEventOrCond(HInputProcessed);				 
    UnLockCondMutex(MutexProcessed);				 
}

#define WHITE	0
#define BLACK	1

#define FEN_WK	0
#define FEN_WQ	1
#define FEN_WR	2
#define FEN_WB	3
#define FEN_WN	4
#define FEN_WP	5
#define FEN_BK	6
#define FEN_BQ	7
#define FEN_BR	8
#define FEN_BB	9
#define FEN_BN	10
#define FEN_BP	11


#define TIMECHECK 100			//Prüfen Zeit abgelaufen

// Timer
//
//timer_private g_exec;

// Globales Debug Flag
//
char g_debug=FALSE;

// 7-Segement Anzeige
//
char g_display[10]= "    ";

// Umsetzung von 7-Segement data in lesbare Ausgabe
//
char g_segment[128] = {

	' ','!','!','!',    '!','!','1','7',   //0   - 7
	'_','!','!','!',    '!','!','!','!',   //8   - 15
	'!','!','!','!',    '!','!','!','!',   //16  - 23
	'!','!','!','!',    '!','!','!','!',   //24  - 31
	'!','!','!','!',    '!','!','!','!',   //32  - 39
	'!','!','!','!',    '!','!','!','!',   //40  - 47
	'I','T','!','!',    '!','!','!','M',   //48  - 55
	'L','C','!','!',    '!','G','U','0',   //56  - 63
	'-','!','!','!',    '!','!','!','!',   //64  - 71
	'!','!','!','!',    '!','!','!','3',   //72  - 79
	'r','!','!','?',    '!','!','!','!',   //80  - 87
	'!','!','!','2',    '!','!','d','!',   //88  - 95
	'!','!','!','!',    '!','!','4','!',   //96  - 103
	'!','!','!','!',    '!','5','y','9',   //104  - 111
	'!','F','!','P',    '!','!','H','A',   //112  - 119
	'5','E','K','!',    'b','6','!','8'    //120  - 127			//Hack Zeichen 120 soll 5 oder S sein 
};


// Statusflag
//
int g_state;

// Fehlerflag (Display zeigt Err)
//
int g_error;

// Globale Variabeln Wartezeiten, abhänig von Modul
//
int g_bestmoveWait;
int g_inputWait;
int g_promoWait;
int g_specialWait;
int g_inputTimeout;

// Zähler Wartezeit (Wartezeit nach Eingabe Befehl,Tastendruck)
//
unsigned int g_waitCnt=0;

// Flags zur Erkennung der Verabeitung der Eingabe
//
int  g_displayChanged=TRUE;
int  g_portIsReady=TRUE;

// Logfile
//
char g_logfile[20]="log_";
int g_mmlog=FALSE;

//Flag, welcher Typ Emulation (z.B. MM, GLASGOW,...)
//
int g_emu;

//Flag, welche Tastenbelegung
//
int g_keys;

// Flag maximale Geschwindigkeit
//
int g_unlimited=TRUE;

// Taktfrequenz mit der die Emulation aktuell läuft (Kann über -mmclock geändert werden
//
int g_clock;

// Orginal Taktfrequenz des jeweiligen Moduls
//
int g_org_clock;

// Profilerausgabe in Logfile
//
int g_profiler;

// Flag Emulation laeuft als Xboard/Enigne
//
int g_xboard_mode;


// Sammel von Daten (Performanceanalyse)
//
//STAT_T g_stat;

// Zeitberechnung (Ermittlung der Geschwindigkeit der Emulation)
//
static int g_option_tc_delay;
static int g_start_time_sc;
static int g_end_time_sc;
static int g_time_per_sec;
static int g_tc_delay;			
static float g_time_corr;		//Faktor für Zeitkorrektur

//Eingabe, Commandostring
//
static char g_input[65536];
static char g_cmd[65536];

static char *cmd1;			//Pointer zum Aufsplitten des GUI Kommandos
static char *nextcmd;

// Länge,index des Commandostrings
//
static int  g_cmd_inx=0;
static int  g_cmd_len=0;

// letzter ausgeführter Befehl
//
static char g_last_cmd;

// Zähler Prüfung ob Consoleneingabe vorhanden
//
static int g_InputCheck;

//  Startwert Zähler Prüfung ob Consoleneingabe vorhanden
//
static int g_InputCheckStart;

// Zähler Prüfung Zeit
//
static int g_TimeCheck=TIMECHECK;

// CPU Takt bei Eingabe
//
static int g_input_clock;

// Speedfaktor bei Eingabe
//
static int g_input_speed;

// Flag Suche soll gestartet werden
//
static int g_start_search;

// Flag Suche soll abgebrochen werden
//
static int g_break_search;

// Flag Level 9 = Unendliche Suche ist eingeschaltet
//
static int g_level9;

// Flag wiederholen Tastendruck gedrückt
//
static int g_repeat_input;

// Name des Moduls
//
static char g_myname[30];

// Zeit fü Performance messung beim Starten
//
static int g_per_wait;

// Feldnamen 0=a8, 63=h1
//
static const char g_fields[64][3] = {	"a8","b8","c8","d8","e8","f8","g8","h8", 
	     			    	 			"a7","b7","c7","d7","e7","f7","g7","h7", 
										"a6","b6","c6","d6","e6","f6","g6","h6", 
										"a5","b5","c5","d5","e5","f5","g5","h5", 
										"a4","b4","c4","d4","e4","f4","g4","h4", 
										"a3","b3","c3","d3","e3","f3","g3","h3", 
										"a2","b2","c2","d2","e2","f2","g2","h2", 
										"a1","b1","c1","d1","e1","f1","g1","h1" };
// gefunder Zug nach Suche
//
static char g_bestmove[10];

// Rückgabewert von WaitInputAvailable
//
static int g_ret;

// Xboard Protokol -> Kommandostrings									
//
static  char feature_string[100]="feature sigint=0 ping=1 setboard=1 color=0 done=1  myname=\"%s\" \n";
static  char feature_string_send[100];
static  char xboardfen[200];
static  char xboardstring[30];
static  int  xboardSTtime;

static  char xcmd_st3[20]; 
static  char xcmd_st5[20]; 
static  char xcmd_st10[20]; 
static  char xcmd_st20[20]; 
static  char xcmd_st60[20]; 
static  char xcmd_st120[20];
static  char xcmd_st360[20];
static  char xcmd_st600[20];
static  char xcmd_st720[20];

static  char xcmd_level40_2[20];
static  char xcmd_level0_5[20];
static  char xcmd_level0_10[20];
static  char xcmd_level0_15[20];
static  char xcmd_level0_30[20];
static  char xcmd_level0_60[20];
static  char xcmd_lev9[10];

static  char xcmd_analyze[10];
static  char xcmd_undo[10];
static  char xcmd_remove[10];
static  char xcmd_setboard[10];
static  char xcmd_setboard_col_w[10];
static  char xcmd_setboard_col_b[10];
static  char xcmd_leave_force[10];
static  char xcmd_roll_diplay[10];
static  char xcmd_show_promo[10];
static  char xcmd_promo_q[2]; 
static  char xcmd_promo_r[2];
static  char xcmd_promo_b[2];
static  char xcmd_promo_n[2];

static  char xcmd_force[10];
static  int xcmd_force_mode;

// Infoanzeige
//
static  XCMD_INFO_T xcmd_info;
static  char xcmd_info_string[50];

static  int ix;
static  char g_info[7][10];
static  char g_save_info[10];
static  int g_info_index=0;
static  int g_info_start=FALSE;
static  int g_send_info=FALSE;
static  int g_rollDisplay=FALSE;
static  int g_rollDisplay_exits=FALSE;

// Struktur Zeitkontrolle
//
static  TC_T g_tc;

// Seite am Zug
//
static  int g_side=WHITE;

// Quit,Exit
//
static int exit_flag=FALSE;

//------------------------------
// Input Thread
//------------------------------
THREAD_PROC_RET ThreadFuncCheckInput(void* data)
{

	char *rc;
	char *last;

	while (!exit_flag )
	{
		rc=fgets(g_input,65536,stdin);
		if (rc==NULL){	
			printf("Error fgets\n");
			break;
		}

			last=strchr(g_input, '\n');
			if (last)
				*last='\0';

		{
			InputAvailable();

			if (!WaitInputProcessed)
				printf("Error WaitInputAProcessed\n");

		}
	}//End while

	return((THREAD_PROC_RET_TYPE)data);
}
//------------------------------
// PrintState
//------------------------------
char *PrintState(int inp)
{
	switch (inp)
	{

	case	DRIVER_START:
		return ((char *)"DRIVER_START");
	case	DRIVER_READY:
		return ((char *)"DRIVER_READY");
	case	SEARCHING:
		return ((char *)"SEARCHING");
	case	PARSEINPUT:
		return ((char *)"PARSEINPUT");
	case	SENDCOMMAND:
		return ((char *)"SENDCOMMAND");
	case	SPECIALCOMMANDS:
		return ((char *)"SPECIALCOMMANDS");
	case	BESTMOVE:
		return ((char *)"BESTMOVE");
	case	BESTMOVEPROMO:
		return ((char *)"BESTMOVEPROMO");
	default:
		return ((char *)"UNKNOWN");
	}
}

//------------------------------
// TestMove
//------------------------------
int TestMove( char* move)
{
	int len;
	char test;

	len=strlen(move);			//Vereinfachte Prüfung maximale Komandolänge bei Promo = 7 Zeichen
	if (len > 7 || len < 4)
		return FALSE;

	test=tolower(move[0]);
	
	if ( !(test=='a' || test=='b' || test=='c' || test=='d' ||
		   test=='e' || test=='f' || test=='g' || test=='h') )
		 return FALSE;

	test=move[1];
	if ( !(test=='1' || test=='2' || test=='3' || test=='4' ||
		   test=='5' || test=='6' || test=='7' || test=='8') )
		 return FALSE;

	test=tolower(move[2]);
	
	if ( !(test=='a' || test=='b' || test=='c' || test=='d' ||
		   test=='e' || test=='f' || test=='g' || test=='h') )
		 return FALSE;

	test=move[3];
	if ( !(test=='1' || test=='2' || test=='3' || test=='4' ||
		   test=='5' || test=='6' || test=='7' || test=='8') )
		 return FALSE;

	return TRUE;
}
//------------------------------
// Log                                           
//------------------------------
void Log(const char *string, ...)
{
	FILE *op;
	time_t t;
	char t_buff[128];
	char buffer[4096];
	va_list ArgList;

	if (!g_mmlog)
		return;

	va_start (ArgList, string);    
	vsprintf (buffer, string, ArgList);
	va_end (ArgList);

	time(&t);
	strftime(t_buff, 128,"%d %b %Y %X",localtime (&t));

	op = fopen(g_logfile, "a");
	fprintf(op,"%s - %s",t_buff,buffer);

	fclose(op);
}
//------------------------------
// PrintAndLog                                           
//------------------------------
void PrintAndLog(const char *string, ...)
{
	FILE *op;
	time_t t;
	char t_buff[128];
	char buffer[4096];
	va_list ArgList;

	va_start (ArgList, string);    
	vsprintf (buffer, string, ArgList);
	va_end (ArgList);

	if (g_mmlog)
	{
		time(&t);
		strftime(t_buff, 128,"%d %b %Y %X",localtime (&t));

		op = fopen(g_logfile, "a");
		fprintf(op,"%s - %s",t_buff,buffer);

		fclose(op);
	}

	printf("%s",buffer);
}

//------------------------------
// SendToGUI 
//------------------------------
void SendToGUI(char* cmd)
{
	printf("%s",cmd);
	Log("ENGINE Output: %s",cmd);
}
//------------------------------
// GetKeycodeGlasgow
//------------------------------
static EMU_KEY_T *GetKeycodeGlasgow(char inp)
{
	static EMU_KEY_T keycode;
	switch (inp)
	{

	case	'a':
		strcpy(keycode.name,"LINE0");
		keycode.data=32;
		return (&keycode);

	case	'b':
		strcpy(keycode.name,"LINE0");
		keycode.data=128;
		return (&keycode);

	case	'c':
		strcpy(keycode.name,"LINE0");
		keycode.data=4;
		return (&keycode);

	case	'd':
		strcpy(keycode.name,"LINE0");
		keycode.data=16;
		return (&keycode);

   	case	'e':
		strcpy(keycode.name,"LINE1");
		keycode.data=1;
		return (&keycode);

   	case	'f':
		strcpy(keycode.name,"LINE0");
		keycode.data=64;
		return (&keycode);

   	case	'g':
		strcpy(keycode.name,"LINE1");
		keycode.data=64;
		return (&keycode);

   	case	'h':
		strcpy(keycode.name,"LINE1");
		keycode.data=16;
		return (&keycode);


	case	'1':
		strcpy(keycode.name,"LINE0");
		keycode.data=32;
		return (&keycode);

	case	'2':
		strcpy(keycode.name,"LINE0");
		keycode.data=128;
		return (&keycode);

	case	'3':
		strcpy(keycode.name,"LINE0");
		keycode.data=4;
		return (&keycode);

	case	'4':
		strcpy(keycode.name,"LINE0");
		keycode.data=16;
		return (&keycode);

   	case	'5':
		strcpy(keycode.name,"LINE1");
		keycode.data=1;
		return (&keycode);

   	case	'6':
		strcpy(keycode.name,"LINE0");
		keycode.data=64;
		return (&keycode);

   	case	'7':
		strcpy(keycode.name,"LINE1");
		keycode.data=64;
		return (&keycode);

   	case	'8':
		strcpy(keycode.name,"LINE1");
		keycode.data=16;
		return (&keycode);


	case	'r':				//CLEAR
		strcpy(keycode.name,"LINE0");
		keycode.data=2;
		return (&keycode);

	case	'p':				//POS
		strcpy(keycode.name,"LINE1");
		keycode.data=8;
		return (&keycode);

	case	'm':				//MEM
		strcpy(keycode.name,"LINE1");
		keycode.data=128;
		return (&keycode);

	case	'i':				//INFO
		strcpy(keycode.name,"LINE1");
		keycode.data=2;
		return (&keycode);

	case	'l':				//LEV
		strcpy(keycode.name,"LINE1");
		keycode.data=32;
		return (&keycode);

	case	's':				//ENT
		strcpy(keycode.name,"LINE0");
		keycode.data=8;
		return (&keycode);

	case	'0':
		strcpy(keycode.name,"LINE1");
		keycode.data=4;
		return (&keycode);

	case	'9':
		strcpy(keycode.name,"LINE0");
		keycode.data=1;
		return (&keycode);


	default:
		{
			return NULL;
		}
	}//End switch


}

//------------------------------
// GetKeycodeGlasgowNew
//------------------------------
static EMU_KEY_T *GetKeycodeGlasgowNew(char inp)
{
	static EMU_KEY_T keycode;
	switch (inp)
	{

	case	'a':
		strcpy(keycode.name,"LINE0");
		keycode.data=1;
		return (&keycode);

	case	'b':
		strcpy(keycode.name,"LINE0");
		keycode.data=2;
		return (&keycode);

	case	'c':
		strcpy(keycode.name,"LINE0");
		keycode.data=4;
		return (&keycode);

	case	'd':
		strcpy(keycode.name,"LINE0");
		keycode.data=8;
		return (&keycode);

   	case	'e':
		strcpy(keycode.name,"LINE0");
		keycode.data=16;
		return (&keycode);

   	case	'f':
		strcpy(keycode.name,"LINE0");
		keycode.data=32;
		return (&keycode);

   	case	'g':
		strcpy(keycode.name,"LINE1");
		keycode.data=64;
		return (&keycode);

   	case	'h':
		strcpy(keycode.name,"LINE1");
		keycode.data=128;
		return (&keycode);


	case	'1':
		strcpy(keycode.name,"LINE0");
		keycode.data=1;
		return (&keycode);

	case	'2':
		strcpy(keycode.name,"LINE0");
		keycode.data=2;
		return (&keycode);

	case	'3':
		strcpy(keycode.name,"LINE0");
		keycode.data=4;
		return (&keycode);

	case	'4':
		strcpy(keycode.name,"LINE0");
		keycode.data=8;
		return (&keycode);

   	case	'5':
		strcpy(keycode.name,"LINE0");
		keycode.data=16;
		return (&keycode);

   	case	'6':
		strcpy(keycode.name,"LINE0");
		keycode.data=32;
		return (&keycode);

   	case	'7':
		strcpy(keycode.name,"LINE1");
		keycode.data=64;
		return (&keycode);

   	case	'8':
		strcpy(keycode.name,"LINE1");
		keycode.data=128;
		return (&keycode);


	case	'r':				//CLEAR
		strcpy(keycode.name,"LINE1");
		keycode.data=16;
		return (&keycode);

	case	'p':				//POS
		strcpy(keycode.name,"LINE1");
		keycode.data=2;
		return (&keycode);

	case	'm':				//MEM
		strcpy(keycode.name,"LINE1");
		keycode.data=8;
		return (&keycode);

	case	'i':				//INFO
		strcpy(keycode.name,"LINE1");
		keycode.data=1;
		return (&keycode);

	case	'l':				//LEV
		strcpy(keycode.name,"LINE1");
		keycode.data=4;
		return (&keycode);

	case	's':				//ENT
		strcpy(keycode.name,"LINE1");
		keycode.data=32;
		return (&keycode);

	case	'0':
		strcpy(keycode.name,"LINE0");
		keycode.data=128;
		return (&keycode);

	case	'9':
		strcpy(keycode.name,"LINE0");
		keycode.data=64;
		return (&keycode);


	default:
		{
			return NULL;
		}
	}//End switch


}

//------------------------------
// GetKeycodeMM
//------------------------------
static EMU_KEY_T *GetKeycodeMM(char inp)
{
	static EMU_KEY_T keycode;
	switch (inp)
	{

	case	'a':
		strcpy(keycode.name,"KEY2_3");
		keycode.data=128;
		return (&keycode);

	case	'b':
		strcpy(keycode.name,"KEY2_5");
		keycode.data=128;
		return (&keycode);

	case	'c':
		strcpy(keycode.name,"KEY2_6");
		keycode.data=128;
		return (&keycode);

	case	'd':
		strcpy(keycode.name,"KEY2_7");
		keycode.data=128;
		return (&keycode);
   
   	case	'e':
		strcpy(keycode.name,"KEY2_0");
		keycode.data=128;
		return (&keycode);

   	case	'f':
		strcpy(keycode.name,"KEY2_1");
		keycode.data=128;
		return (&keycode);

   	case	'g':
		strcpy(keycode.name,"KEY2_2");
		keycode.data=128;
		return (&keycode);

   	case	'h':
		strcpy(keycode.name,"KEY2_4");
		keycode.data=128;
		return (&keycode);


	case	'1':
		strcpy(keycode.name,"KEY2_3");
		keycode.data=128;
		return (&keycode);

	case	'2':
		strcpy(keycode.name,"KEY2_5");
		keycode.data=128;
		return (&keycode);

	case	'3':
		strcpy(keycode.name,"KEY2_6");
		keycode.data=128;
		return (&keycode);

	case	'4':
		strcpy(keycode.name,"KEY2_7");
		keycode.data=128;
		return (&keycode);
   
   	case	'5':
		strcpy(keycode.name,"KEY2_0");
		keycode.data=128;
		return (&keycode);

   	case	'6':
		strcpy(keycode.name,"KEY2_1");
		keycode.data=128;
		return (&keycode);

   	case	'7':
		strcpy(keycode.name,"KEY2_2");
		keycode.data=128;
		return (&keycode);

   	case	'8':
		strcpy(keycode.name,"KEY2_4");
		keycode.data=128;
		return (&keycode);


	case	'r':				//CLEAR
		strcpy(keycode.name,"KEY1_0");
		keycode.data=128;
		return (&keycode);

	case	'p':				//POS
		strcpy(keycode.name,"KEY1_1");
		keycode.data=128;
		return (&keycode);

	case	'm':				//MEM
		strcpy(keycode.name,"KEY1_2");
		keycode.data=128;
		return (&keycode);

	case	'i':				//INFO
		strcpy(keycode.name,"KEY1_3");
		keycode.data=128;
		return (&keycode);

	case	'l':				//LEV
		strcpy(keycode.name,"KEY1_4");
		keycode.data=128;
		return (&keycode);

	case	's':				//ENT
		strcpy(keycode.name,"KEY1_5");
		keycode.data=128;
		return (&keycode);

	case	'0':
		strcpy(keycode.name,"KEY1_6");
		keycode.data=128;
		return (&keycode);

	case	'9':
		strcpy(keycode.name,"KEY1_7");
		keycode.data=128;
		return (&keycode);

	default:
		{
			return NULL;
		}
	}//End switch
}

//------------------------------
// GetKeycode
//------------------------------
static EMU_KEY_T *GetKeycode(int keys, char inp)
{
	switch (keys)
	{
		case	MM_KEYS:
			return (GetKeycodeMM(inp));

		case	GLASGOW_KEYS:
			return (GetKeycodeGlasgow(inp));

		case	GLASGOW_NEW_KEYS:
			return (GetKeycodeGlasgowNew(inp));
			
		default:
			return NULL;

	}

}

//------------------------------
// isPromoInput
//------------------------------
static int isPromoInput(char *move)
{
	if (strlen(move) == 4)
		return FALSE;

	return TRUE;
}

//------------------------------
// isPromoCmd
//------------------------------
static int isPromoCmd(char *cmd)
{
	if (TestMove(cmd) &&
		strlen(cmd) > 5)
		return TRUE;

	return FALSE;
}
//------------------------------
// isPromoPiece
//------------------------------
static int isPromoPiece(char p)
{
	if (p == xcmd_promo_q[0] ||		//Promo
		p == xcmd_promo_r[0] || 
		p == xcmd_promo_b[0] || 
		p == xcmd_promo_n[0] )
		return TRUE;
	else
		return FALSE;
}

//------------------------------
// checkPromo
//------------------------------
static int checkPromo(char *move)
{
	if ( ( move[1]=='7' && move[3]=='8' ) ||
		 ( move[1]=='2' && move[3]=='1' ) ) 
		return TRUE;

	return FALSE;
}

//------------------------------
// SendBestmoveToGUI 
//------------------------------
static void SendBestmoveToGUI(char* cmd)
{
	printf("move %s",cmd);
	Log("ENGINE Output: move %s",cmd);
}

//------------------------------
// addChar 
//------------------------------
static void addChar(char *str, char first)
{
	char tmp[200];

	strncpy(tmp,str,200);

	str[0]=first;
	str[1]='\0';

	strcat(str,tmp);
}

//------------------------------
// CmdFromFEN                                          
//------------------------------
static int CmdFromFEN(running_machine *machine, char * FEN, char * cmd, int force)
{
	int i,k;
	int setboard[12][8];

	int a,emp_cnt;
	int board_index=0;

	char pieces[100];
	char side;
	char castle[5];
	char ep[3];

	char tmp_xcmd_setboard[10];

	//int ep_nr;
	//int load_board[64];

	int fifty=0;
	int move_cnt=1;

	//int bw=0; 
	//short lcastle[2][2]={0,0,0,0};

	int wq_cnt=0;
	int wr_cnt=0;
	int wb_cnt=0;
	int wn_cnt=0;
	int wp_cnt=0;

	int bq_cnt=0;
	int br_cnt=0;
	int bb_cnt=0;
	int bn_cnt=0;
	int bp_cnt=0;


// Intitialisieren Hilfsstruktur
//

	for (i=0;i<12;i++)
		for (k=0;k<8;k++)
			setboard[i][k]=99;

	sscanf(FEN, "%s %c %s %s %d %d", pieces, &side, castle, ep, &fifty, &move_cnt);

//-----------------------------------------------
// Part 1 = Einlesen der Stellungen
//-----------------------------------------------
	for (a=0;pieces[a];a++)
	{
		switch (pieces[a]) 
		{
			case 'K': setboard[FEN_WK][0]        = board_index++; break;
			case 'Q': setboard[FEN_WQ][wq_cnt++] = board_index++; break;
			case 'R': setboard[FEN_WR][wr_cnt++] = board_index++; break;
			case 'B': setboard[FEN_WB][wb_cnt++] = board_index++; break;
			case 'N': setboard[FEN_WN][wn_cnt++] = board_index++; break;
			case 'P': setboard[FEN_WP][wp_cnt++] = board_index++; break;
			
			case 'k': setboard[FEN_BK][0]        = board_index++; break;
			case 'q': setboard[FEN_BQ][bq_cnt++] = board_index++; break;
			case 'r': setboard[FEN_BR][br_cnt++] = board_index++; break;
			case 'b': setboard[FEN_BB][bb_cnt++] = board_index++; break;
			case 'n': setboard[FEN_BN][bn_cnt++] = board_index++; break;
			case 'p': setboard[FEN_BP][bp_cnt++] = board_index++; break;
			
			case '/': break;

			default:						//leere Felder
				if (isdigit (pieces[a]) )
				{
					emp_cnt=pieces[a]-'0';
					board_index=board_index+emp_cnt;
				}
				break;

		}// end switch Part 1: Stellung einlesen
	}// end for

	if ( board_index != 64)
		return FALSE;
//-----------------------------------------------
// Part 2 = Seite  die am Zug ist
//-----------------------------------------------


//-----------------------------------------------			
// Part 3 = Rochadestatus
//-----------------------------------------------


//-----------------------------------------------
// Part 4 = EP Feld
//-----------------------------------------------

// Part 5	= fifty
// Part 6   = move_cnt



// Beim Amsterdam, Roma und Dallas muss immer die Seite Weis eingestellt werden !!
//
	//if (!strcmp(machine->basename,"amsterd")  ||			Beim Amserdam, Roma und Dallas muss immer weis am Zug sein
	//	!strcmp(machine->basename,"dallas")   ||			Umstellen der Farbe zu Begin Figuren aufstellen funktioniert nicht
	//	!strcmp(machine->basename,"dallas16") ||
	//	!strcmp(machine->basename,"dallas32") ||
	//	!strcmp(machine->basename,"roma32") )
	//{
	//	strcpy(tmp_xcmd_setboard,xcmd_setboard_col_w);
	//}else
		strcpy(tmp_xcmd_setboard,xcmd_setboard);

// Commandostring bilden
//
		if (force)		//Zurerst Force modus verlassen
		{
			strcpy(cmd,"r");
			strcat(cmd,tmp_xcmd_setboard);
		}else
			strcpy(cmd,tmp_xcmd_setboard);

		for (i=0;i<12;i++)
		{
			for (k=0;(setboard[i][k]!=99 && k<8);k++)
			{
				strcat(cmd,g_fields[setboard[i][k]]);
				strcat(cmd,"s");
			}
			if (i!=FEN_WK && i!=FEN_BK)
				strcat(cmd,"s");
		}

// Seite am Zug setzen
//
		if (side=='w')
		{
			strcat(cmd,"0s");
		}
		else
		{
			strcat(cmd,"9s");
		}

		strcat(cmd,"r");

		if (force)		//Force modus wieder anschalten
			strcat(cmd,"m");

	return TRUE;
}
//------------------------------
// GetTime                                           
//------------------------------
static UINT64 GetTime(void)
{
	struct TIME_STRUCT Time;

	GetTimeofday(Time);

	return (Getms(Time));
}

/*----------------------------------------------------------------------------*/
/* Zugzeit berechnen                                                          */
/*----------------------------------------------------------------------------*/
static void TimeControl(TC_T *p_tc)
{
	int InOutTime;

	p_tc->movetime=0;


//-----------------------------------------------------//
// Statzeit Suche                                      // 
//-----------------------------------------------------//
	p_tc->starttime=GetTime();	

//-----------------------------------------------------//
// Feste Zeit pro Zug                                  // 
//-----------------------------------------------------//
	if (p_tc->fixmovetime)
	{
		p_tc->movetime=p_tc->fixmovetime;
		Log("Movetime(fix): %d\n",p_tc->movetime);
		return;
	}
//-----------------------------------------------------//
// Zeitvorgabe für Partie-> Zugzeit berechnen          // 
//-----------------------------------------------------//

// Anzahl Züge innerhalb vorgegebener Zeit
//
	if (p_tc->movestogo)
	{

		InOutTime=p_tc->movestogo * g_tc_delay;					

		//PrintAndLog("g_tc_delay: %d, InOutTime: %d, time: %d\n",g_tc_delay,InOutTime,p_tc->time);

		if ( (p_tc->time-InOutTime) > g_tc_delay)
			p_tc->time=p_tc->time - InOutTime;
	
		p_tc->movetime=p_tc->time/p_tc->movestogo;
		Log("Movetime: %d, time: %d, movestogo: %d\n",p_tc->movetime,p_tc->time,p_tc->movestogo);

		if (p_tc->movestogo==1)											//Verhindert Zeitüberschreitung letzter Zug vor Zeitkontrolle
		{
			p_tc->movetime=p_tc->movetime -(p_tc->movetime/5);	       //20 % Zeitreserve
			Log("Last move before TC Movetime: %d\n",p_tc->movetime);
		}

		return;
	}

// Sonst Zeit pro Zug berechnen
// z.B. TIME_MOVE_DIV=40 = 2,25 % pro Zug zuzüglich die Hälfte vom Zeitzuschlag, abzüglich Zetverzug aus Zugein/ausgabe
//
	p_tc->movetime=(p_tc->time/TIME_MOVE_DIV + (p_tc->inc/2)) - g_tc_delay;
	if ( p_tc->movetime >= p_tc->time)				 
		 p_tc->movetime=p_tc->inc-500;

	if (p_tc->movetime<0)													// Falls Zeit aus ist + 100 ms
			p_tc->movetime=100+g_tc_delay;

	Log("Movetime: %d, time: %d, inc: %d g_tc_delay: %d\n",p_tc->movetime,p_tc->time,p_tc->inc,g_tc_delay);
}

//------------------------------
// TimeOver                                          
//------------------------------
static int TimeOver(TC_T *p_tc)
{
	UINT64 endtime;

	endtime=GetTime();

	if (endtime-p_tc->starttime < 100)									//Mindesten 0,1 Sekunden suchen
		return FALSE;

	//Log("Check TimeOver used: %llu   Movetime: %u\n",endtime-p_tc->starttime,p_tc->movetime);

	if ( (endtime-p_tc->starttime) > p_tc->movetime)					 
	{
		Log("Break: time used %llu > Movetime: %u\n",endtime-p_tc->starttime,p_tc->movetime);
		return TRUE;
	}
	
return FALSE;
}
//------------------------------
// clearTC                                          
//------------------------------
static void clearTC(void)
{
	g_tc.fixmovetime		= 0;
	g_tc.wtime				= 0;
	g_tc.winc				= 0;
	g_tc.btime				= 0;
	g_tc.binc				= 0;
	g_tc.movestogo  		= 0;
	g_tc.movestogo_start	= 0;
	g_tc.inc				= 0;
	g_tc.time				= 0;
	g_tc.otim				= 0;
	g_tc.movetime			= 0;
	g_tc.starttime			= 0;
	g_tc.side				= WHITE;
}

//------------------------------
// LogProfiler                                          
//------------------------------
static void LogProfiler(running_machine *machine)
{
	astring profilertext;

	if (g_waitCnt % 10 == 0)												 	
	{																	 
		profiler_get_text(machine, profilertext);

		if (astring_len(&profilertext) > 0)								 
			Log("Profiler: \n\n%s\n",astring_c(&profilertext));			 

		astring_free(&profilertext);										 
	}																	 

}
//------------------------------
// PrintLevel                                         
//------------------------------
static void PrintLevel()
{
	PrintAndLog("Supported Levels:\n\n");
	PrintAndLog("Level 0: 3   seconds/move   -> st 3\n");
	PrintAndLog("Level 1: 5   seconds/move   -> st 5\n");
	PrintAndLog("Level 2: 10  seconds/move   -> st 10\n");
	PrintAndLog("Level 3: 20  seconds/move   -> st 20\n");
	PrintAndLog("Level 4: 60  seconds/move   -> st 60\n");
	PrintAndLog("Level 5: 120 seconds/move   -> st 120\n");
	PrintAndLog("Level 6: 40 move in 2 hours -> level 40 120 0\n\n");
}
//------------------------------
// ProcessDRIVER_START                                          
//------------------------------
static void ProcessDRIVER_START(running_machine *machine)
{

	if (g_start_time_sc==0)
	{
		g_waitCnt=0;	
		g_start_time_sc=GetTime();
	}else if (g_waitCnt>=g_per_wait)
	{
		g_end_time_sc=GetTime();
		g_time_per_sec=(g_end_time_sc-g_start_time_sc)/10;
		g_time_corr=(float) g_time_per_sec/TC_DELAY_REF;

		PrintAndLog("Emulator org. clock : %d\n",g_org_clock);
		PrintAndLog("Emulator curr.clock : %d\n",g_clock);
		PrintAndLog("Speed factor clock  : %2.2f\n\n",(float)g_clock/g_org_clock);

		PrintAndLog("OS ticks_per_second : %d\n\n",(int) osd_ticks_per_second());

		if (g_unlimited)																//Anpassen Zeitkorrektur an die Geswindigkeit des Systems
		{
			if (g_option_tc_delay == 0)
			{
				g_time_corr=(float) g_time_per_sec/TC_DELAY_REF;

				g_tc_delay		= g_tc_delay * g_time_corr;

				PrintAndLog("Time in ms for 1 sec: %d\n",g_time_per_sec);
				PrintAndLog("Speed factor time   : %2.2f\n\n",(float)1000/g_time_per_sec);

				PrintAndLog("Speed factor total  : %2.2f\n\n",(float)1000/g_time_per_sec * (float)g_clock/g_org_clock );

				PrintAndLog("ReferenceTimePerSec : %2d (This system has: %d)\n",TC_DELAY_REF,g_time_per_sec);
				PrintAndLog("Factor time corr.   : %2.2f\n",g_time_corr);
				PrintAndLog("g_tc_delay (ms)     : %2d\n\n",g_tc_delay);
			}
			else
			{
				g_tc_delay=g_option_tc_delay;											// Angabe des Korrekturwerts als parameter
				PrintAndLog("g_tc_delay (ms)     : %2d\n\n",g_tc_delay);
			}																				
		}else	//unlimited
		{
			PrintLevel();
		}



		g_state=DRIVER_READY;
	}
}

//------------------------------
// ProcessSEARCHING                                           
//------------------------------
static void ProcessSEARCHING(running_machine *machine)
{
	EMU_KEY_T *keycode;

	int InfoData=TRUE;
	char valid[] = " -0123456789AbCdEFGH";
	char valid_movecheck[] = "AbCdEFGH";


	if (g_InputCheck <= 0)												//Eingabeprüfung während der Suche
	{
		g_ret=WaitInputAvailable(WaitTime);
		g_InputCheck=g_InputCheckStart;

		if (!g_break_search)
		{
			if (!strcmp(g_input,"?") || !strcmp(g_input,"exit"))		//Sonderfall Suchabbruch durch ?,quit
			{															// Falls ? gesendet wird
				Log("GUI    Input : %s ->Search break\n",g_input);		

				keycode=GetKeycode(g_keys,'s');							// SEARCHING , dann durch drücken 
				input_port_set(machine,keycode->name,keycode->data);	// der <Enter> taste die Suche abbrechen

				g_break_search=TRUE;									//Flag Suchabbruch 

				InputProcessed();

			}else if (!strcmp(g_input,"."))								//Nicht auswerten
				InputProcessed();
			else if (!strcmp(g_input,"quit"))
			{
				Log("GUI    Input : %s ->Search quit\n",g_input);
				machine->mame_data->exit_pending=TRUE;
				InputProcessed();
				g_state=DRIVER_READY;
				return;
			}
			//else if (g_input[0]!='\0')
			//	InputProcessed();

		}////End g_break_search
	}// End if g_InputCheck

	g_InputCheck--;

	if (g_unlimited)
	{
		if (g_TimeCheck <= 0 && !g_break_search)	
		{
			g_TimeCheck=TIMECHECK;
			if (TimeOver(&g_tc) )										//Zeit abgelaufen
			{
				Log(" ->Time Over\n");		 

				keycode=GetKeycode(g_keys,'s');
				input_port_set(machine,keycode->name,keycode->data);

				g_break_search=TRUE;									//Flag Suchabbruch 

			}
		}//End h_TimeCheck
		g_TimeCheck--;
	}//End if unlimited


// Zusaetzliche Prüfungen beim amsted und dallas16
// Displayausgaben sind bei diesem Modulen nicht sauber
// Alternativ zu dieser Loesung die Ursachen dafür im Treiber suchen
//
	if ( !strcmp(machine->basename,"amsterd") ||
		  !strcmp(machine->basename,"dallas16") )		
	{				
		if ( (strpbrk(g_display, valid) == NULL ||			//Nur gültige Zeichen, 8888 ist nicht erlaubt
			!strncmp(g_display,"8888",4) ))	

			InfoData = FALSE;

		if (strpbrk(g_display, valid_movecheck) != NULL &&	//Gueltiger Zugstring
			!TestMove(g_display) )							
				InfoData = FALSE;
		
	}
		 

// Ausgbe Infostring
//
	if (InfoData
		&& !strchr(g_display,'!') 
		&& strncmp(g_display,g_save_info,4) 
		&& strncmp(g_display,"    ",4) )									//Info Anzeige hat gewechselt	
	{

		 strcpy(g_save_info,g_display);


// Start der rolliernden Anzeige herausfinden
//
		switch (g_emu)
		{
			case EMU_MM:
				if (!strcmp(g_display,"0000") || !strcmp(g_display,"8888") )
					g_info_start=TRUE;
				break;
			case EMU_GLASGOW:
				if (g_display[0]==' ' || g_display[0]=='-' || !strcmp(g_display,"0000") )		//Score Anzeige erstes Zeichen ist blank oder minus -> Start rollierende Anzeige
				{
					g_info_start=TRUE;
				}
				break;
		}

// Wenn Info 
//
		if (g_info_start)
		{
			g_info_start=FALSE;
			g_info_index=0;
			g_send_info=TRUE;									//Vermeidet dass ungültige Daten gesendet werden
		}
		
		 if (g_emu==EMU_GLASGOW)
			strcpy(g_info[g_info_index++],g_display);		   //Einsammeln Daten für Infostring (Glasgow)

		 if (g_rollDisplay_exits)								//rollierende Anzeige vorhanden						
		 {
			 if (g_info_index > 4 )
			 {

				xcmd_info.ply[0]	='\0';
				xcmd_info.score[0]	='\0';
				xcmd_info.time[0]	='\0';
				xcmd_info.nodes[0]	='\0';
				xcmd_info.PV[0]		='\0';

				for( ix=0;ix<=(g_info_index-1);ix++)
				{
				
					switch (g_emu)
					{
					case EMU_MM:
						{
							switch (ix)
							{
							case 0:										// 1 Laufende Rechenzeit	00:05			
								strcpy(xcmd_info.time,g_info[ix]);
								break;
							case 1:										// PV 1. Zug
								g_info[ix][0]=tolower(g_info[ix][0]);
								g_info[ix][2]=tolower(g_info[ix][2]);
								strcpy(xcmd_info.PV,g_info[ix]);
								strcat(xcmd_info.PV," ");
								break;
							case 2:										// PV 2. Zug
								g_info[ix][0]=tolower(g_info[ix][0]);
								g_info[ix][2]=tolower(g_info[ix][2]);
								strcat(xcmd_info.PV,g_info[ix]);
								break;
							case 3:										// Suchtiefe				04.02				
								xcmd_info.ply[0]=g_info[ix][0];
								xcmd_info.ply[1]=g_info[ix][1];
								xcmd_info.ply[2]='\0';
								break;
							case 4:
								strcpy(xcmd_info.score,g_info[ix]);		//Stellungsbewertung		 0.85 bzw -0.85	
								break;
							default:
								break;

							}//End switch

							break;

						}//End case EMU_MM

					case EMU_GLASGOW:
						{
							switch (ix)
							{
							case 0:										// Stellungsbewertung		 0.85 bzw -0.85		
								strcpy(xcmd_info.score,g_info[ix]);
								break;
							case 1:										// Suchtiefe				04.02
								if ( !strcmp(machine->basename,"roma32")   || 
									 !strcmp(machine->basename,"dallas")   ||
									 !strcmp(machine->basename,"dallas16") ||
									 !strcmp(machine->basename,"dallas32") ||
									 !strcmp(machine->basename,"amsterd") )
								{
									xcmd_info.ply[0]='0';				// Beim Dallas,Roma und Amsterdam immer nur Suchtiefe = 01 anzeigen
									xcmd_info.ply[1]='1';	

									strcpy(xcmd_info.time,g_info[ix]); // Beim Dallas,Roma und Amsterdam wird hier die laufende Rechenzeit angezeigt

								}else
								{
									xcmd_info.ply[0]=g_info[ix][0];
									xcmd_info.ply[1]=g_info[ix][1];
								}
								xcmd_info.ply[2]='\0';
								break;
							case 2:										// PV 1. Zug
								g_info[ix][0]=tolower(g_info[ix][0]);
								g_info[ix][2]=tolower(g_info[ix][2]);
								strcat(xcmd_info.PV,g_info[ix]);
								strcat(xcmd_info.PV," ");
								break;
							case 3:										// PV 2. Zug
								g_info[ix][0]=tolower(g_info[ix][0]);
								g_info[ix][2]=tolower(g_info[ix][2]);
								strcat(xcmd_info.PV,g_info[ix]);
								strcat(xcmd_info.PV," ");
								break;
							case 4:
								g_info[ix][0]=tolower(g_info[ix][0]);	// PV 3. Zug
								g_info[ix][2]=tolower(g_info[ix][2]);
								strcat(xcmd_info.PV,g_info[ix]);
								break;
							default:
								break;

							}//End switch

							break;

						}//End case EMU_GLASGOW

					}//End switch g_emu
				}//End for

				g_info_index = 0;

				strcpy(xcmd_info.nodes,"0");					//Keine Info  Konten

				if (g_emu==EMU_GLASGOW)
					strcpy(xcmd_info.time,"0");					//Keine Info  Zeit beim Glasgow


				xcmd_info_string[0]='\0';

				strcat(xcmd_info_string,xcmd_info.ply);
				strcat(xcmd_info_string," ");
				strcat(xcmd_info_string,xcmd_info.score);
				strcat(xcmd_info_string," ");
				strcat(xcmd_info_string,xcmd_info.time);
				strcat(xcmd_info_string," ");
				strcat(xcmd_info_string,xcmd_info.nodes);
				strcat(xcmd_info_string," ");
				strcat(xcmd_info_string,xcmd_info.PV);

				xcmd_info_string[strlen(xcmd_info_string)]='\n';
				xcmd_info_string[1+strlen(xcmd_info_string)]='\0';

				if (g_send_info)
					SendToGUI(xcmd_info_string);

			 }//End if g_info_index
			
			 if (g_emu==EMU_MM)
				strcpy(g_info[g_info_index++],g_display);		//Einsammel Daten für Infostring (beim MM)


		 }//End if g_rollDisplay_exits


// keine rollierende Anzeige vorhanden z.B. Rebell 5.0	
//
		 else												
		 {
			if (g_info_index > 3 )
			{
				strcpy(xcmd_info.time,g_display);
				strcpy(xcmd_info.ply,"1");
				strcpy(xcmd_info.score,"0");
				strcpy(xcmd_info.nodes,"0");
				strcpy(xcmd_info.PV,"____");

				xcmd_info_string[0]='\0';

				strcat(xcmd_info_string,xcmd_info.ply);
				strcat(xcmd_info_string," ");
				strcat(xcmd_info_string,xcmd_info.score);
				strcat(xcmd_info_string," ");
				strcat(xcmd_info_string,xcmd_info.time);
				strcat(xcmd_info_string," ");
				strcat(xcmd_info_string,xcmd_info.nodes);
				strcat(xcmd_info_string," ");
				strcat(xcmd_info_string,xcmd_info.PV);

				xcmd_info_string[strlen(xcmd_info_string)]='\n';
				xcmd_info_string[1+strlen(xcmd_info_string)]='\0';

				SendToGUI(xcmd_info_string);

				g_info_index=0;
			}else if (strcmp(g_display,"0000") && g_emu==EMU_MM)		//Zähler beim 0000 nicht hochzählen
				g_info_index++;

			}// End if g_info_index

	}//End if !strchr

}

//------------------------------
// ProcessDRIVER_READY                                           
//------------------------------
static void ProcessDRIVER_READY(void)
{
	int InputWaitTime;

	if (g_error)					//Abfangen von Fehlersituationen -> Vermeidet dass keine Eingabe mehr möglich ist
	{
		InputProcessed();
		g_error=FALSE;
	}

	if (g_unlimited)				//Bei umlimited die Emulation stoppen wenn auf Eingabe gewarted wird
		InputWaitTime=INFINITE;		//Bei Orginalgeschwindigkeit nicht stoppen, wegen Pondern
	else
		InputWaitTime=WaitTime;

	if (g_InputCheck <= 0)
	{
		if ( WaitInputAvailable(InputWaitTime) )
		{
			g_last_cmd='\0';
			g_state=PARSEINPUT;
		}
		g_InputCheck=g_InputCheckStart;
	}
	g_InputCheck--;
}

//------------------------------
// ProcessPARSEINPUT                                          
//------------------------------
static void ProcessPARSEINPUT(running_machine *machine)
{
	g_cmd[0]='\0';
	g_last_cmd='\0';

	Log("GUI    Input : %s\n",g_input);

	cmd1=strtok(g_input, " ");			//1. Teil des Strings
	nextcmd=strtok(NULL, " ");			//2. Teil des Strings

	if (!g_unlimited)										//Beschleunigung der Eingabe bei nommunlimited
	{														//Auch für mmunlimited anwendbar ? -> pruefen
		cpu_set_clock(machine->firstcpu,g_input_clock ); 
		video_set_speed_factor(g_input_speed);
	}

// Konsolenbefehle (Befehlseingabe zum Testen)
//

	if (cmd1==NULL)
	{
		InputProcessed();
		g_state=DRIVER_READY;
		return;
	}

	if (!strcmp(cmd1,"cmd") )
	{
		strcpy(g_cmd,nextcmd);
	}

	else if (!strcmp(cmd1,"get_clock") )
	{
		printf("cpu_get_clock:     %d\n",cpu_get_clock(machine->firstcpu) );
		InputProcessed();
		g_state=DRIVER_READY;
		return;
	}

	else if (!strcmp(cmd1,"set_clock") && nextcmd != NULL )
	{
		cpu_set_clock(machine->firstcpu,atoi(nextcmd) );
		InputProcessed();
		g_state=DRIVER_READY;
		return;
	}


	else if (!strcmp(cmd1,"get_clockscale") )
	{
		printf("cpu_get_clockscale:     %f\n",cpu_get_clockscale(machine->firstcpu) );
		InputProcessed();
		g_state=DRIVER_READY;
		return;
	}

	else if (!strcmp(cmd1,"set_clockscale") && nextcmd != NULL )
	{
		cpu_set_clockscale(machine->firstcpu,atoi(nextcmd) );
		InputProcessed();
		g_state=DRIVER_READY;
		return;
	}


// Xboard Befehle
//

	else if (!strcmp(cmd1,"xboard") )
	{

		InputProcessed();
		g_state=DRIVER_READY;

		g_xboard_mode = TRUE;

		return;
		
	}


	else if(!strcmp(cmd1,"protover") && !strcmp(nextcmd,"2") )
	{
		sprintf(feature_string_send,feature_string,g_myname); 
		SendToGUI(feature_string_send);
		InputProcessed();
		g_state=DRIVER_READY;
		return;

	}
	
	else if(!strcmp(cmd1,"ping"))
	{

		strcpy(xboardstring,"pong ");
		strcat(xboardstring,nextcmd);
		strcat(xboardstring,"\n");
		SendToGUI(xboardstring);
 
		if ( (strlen(xcmd_roll_diplay)!=0) && !g_rollDisplay)	
		{
			strcpy(g_cmd,xcmd_roll_diplay);
			g_rollDisplay=TRUE;
		}		

	}

	else if (!strcmp(cmd1,"new") )
	{

		soft_reset(machine, NULL, 0);
		Log("Softreset\n");	

		if ( strlen(xcmd_roll_diplay)!=0 )
			g_rollDisplay=FALSE;

		xcmd_force_mode=FALSE;
		g_start_search=FALSE;
		g_level9=FALSE;

		clearTC();
		
		InputProcessed();
		g_state=DRIVER_READY;
		return;
		
	}

	else if (!strcmp(cmd1,"post") )
	{
//		if (g_emu==EMU_MM)
//		{
			if ( (strlen(xcmd_roll_diplay)!=0) && !g_rollDisplay)						
			{
				strcpy(g_cmd,xcmd_roll_diplay);
				g_rollDisplay=TRUE;
			}
//		}
	}

	else if (!strcmp(cmd1,"easy") )
	{
		if (g_unlimited && !g_level9)		//LEV 9 = unendlich falls noch nicht passiert
		{
			strcpy(g_cmd,xcmd_lev9); 							
			g_level9=TRUE;
		}
	}

	else if (!strcmp(cmd1,"hard") )
	{
		if (g_unlimited && !g_level9)		//LEV 9 = unendlich falls noch nicht passiert
		{
			strcpy(g_cmd,xcmd_lev9); 							
			g_level9=TRUE;
		}
	}

	else if (!strcmp(cmd1,"force") )
	{
		if (!xcmd_force_mode)
		{
			strcpy(g_cmd,xcmd_force);		
			xcmd_force_mode=TRUE;
			Log("force an xcmd_force_mode: %d\n",xcmd_force_mode);
		}
	}


	else if (!strcmp(cmd1,"setboard") )
	{
		xboardfen[0]='\0';
		if (nextcmd!=NULL)
			strcat(xboardfen,nextcmd);
		while ( (nextcmd=strtok(NULL, " ")) != NULL )
		{
			strcat(xboardfen," ");
			strcat(xboardfen,nextcmd);
		}

		if (!CmdFromFEN(machine,&xboardfen[0],&g_cmd[0],xcmd_force_mode))
		{
			InputProcessed();
			g_state=DRIVER_READY;
			return;
		}
	}

	else if(!strcmp(cmd1,"time"))
		g_tc.time=atoi(nextcmd)*10;

	else if(!strcmp(cmd1,"otim"))
		g_tc.otim=atoi(nextcmd)*10; 


	else if (!strcmp(cmd1,"st") )
	{

		xboardSTtime=atoi(nextcmd);

		if (g_unlimited)
		{
			clearTC();
			g_tc.fixmovetime=xboardSTtime * 1000;
			if (!g_level9)					//LEV 9 = unendlich falls noch nicht passiert
			{
				strcpy(g_cmd,xcmd_lev9); 						
				g_level9=TRUE;
			}
		}
		else
		{
			switch (xboardSTtime)									//LEV 1	= 5 sec = default
			{
				case	3:   { strcpy(g_cmd,xcmd_st3); break; }		//LEV 0 = 3  sec
				case	5:   { strcpy(g_cmd,xcmd_st5); break; }		//LEV 1 = 5  sec	
				case	10:  { strcpy(g_cmd,xcmd_st10); break; }	//LEV 2	= 10 sec
				case	20:  { strcpy(g_cmd,xcmd_st20); break; }	//LEV 3 = 20 sec
				case	60:  { strcpy(g_cmd,xcmd_st60); break; }	//LEV 4 = 60 sec
				case    120: { strcpy(g_cmd,xcmd_st120); break; }	//LEV 5 = 120 sec
				case    600: { strcpy(g_cmd,xcmd_st600); break; }	//LEV 7 = 360 sec
				case    360: { strcpy(g_cmd,xcmd_st360); break; }	//LEV 8 = 360 sec -> 6 Min
				case    720: { strcpy(g_cmd,xcmd_st720); break; }	//LEV 8 = 720 sec -> 12 Min
					
			}// End switch
		}//End if g_unlimited

		if ( strlen(g_cmd) == 0 && !g_unlimited )
		{
			PrintAndLog("Time control not supported - default level used !!!\n");
			PrintLevel();
		}

		if ( strlen(g_cmd) > 0 && xcmd_force_mode )
		{
			addChar(&g_cmd[0],'r');
			strcat(g_cmd,"m");
		}

		
	}//End st

	else if (!strcmp(cmd1,"level") )							//z.B. level 40 120 0 = Turnierstufe = LEV 6
	{
		if (g_unlimited)
		{
			clearTC();
			if (!g_level9)
			{
				strcpy(g_cmd,xcmd_lev9); 						//LEV 9 = unendlich
				g_level9=TRUE;
			}

			g_tc.movestogo=atoi(nextcmd);
			g_tc.movestogo_start=g_tc.movestogo;

			nextcmd=strtok(NULL, " ");
			if (nextcmd!=NULL)									//Nicht ausgewertet wird durch time/otim gebildet
			{
				nextcmd=strtok(NULL, " ");
				if (nextcmd!=NULL)
				{
					g_tc.winc=atoi(nextcmd)*1000;				// Nicht genutzt
					g_tc.binc=atoi(nextcmd)*1000;				// Nicht genutzt
					g_tc.inc=atoi(nextcmd)*1000;
				}
			}
		}
		else
		{
			if (!strcmp(nextcmd,"40") )
			{
				nextcmd=strtok(NULL, " ");
				if (nextcmd!=NULL && !strcmp(nextcmd,"120") )
				{
					strcpy(g_cmd,xcmd_level40_2);				//LEV 6 40 Züge in 2 Stunden
				}
			}else if (!strcmp(nextcmd,"0") )					//z.B. level 0 5 0 = 5 Minuten pro Partie
			{
				nextcmd=strtok(NULL, " ");

				if (nextcmd!=NULL && !strcmp(nextcmd,"9999") )
				{
					strcpy(g_cmd,xcmd_lev9);					//LEV 9 - Arena Einstellung Unendlich
				}

				if (nextcmd!=NULL && !strcmp(nextcmd,"5") )
				{
					strcpy(g_cmd,xcmd_level0_5);				//LEV pb 1 = 5 Minuten
				}
				if (nextcmd!=NULL && !strcmp(nextcmd,"10") )
				{
					strcpy(g_cmd,xcmd_level0_10);				//LEV pb 4 = 10 Minuten
				}
				if (nextcmd!=NULL && !strcmp(nextcmd,"15") )
				{
					strcpy(g_cmd,xcmd_level0_15);				//LEV pb 6 = 15 Minuten
				}
				if (nextcmd!=NULL && !strcmp(nextcmd,"30") )
				{
					strcpy(g_cmd,xcmd_level0_30);				//LEV pb 7 = 30 Minuten
				}
				if (nextcmd!=NULL && !strcmp(nextcmd,"60") )
				{
					strcpy(g_cmd,xcmd_level0_60);				//LEV pb 8 = 60 Minuten
				}
			}
		} //End if g_unlimited

		if ( strlen(g_cmd) == 0 && !g_unlimited )
		{
			PrintAndLog("Time control not supported - default level used !!!\n");
			PrintLevel();
		}

		if ( strlen(g_cmd) > 0 && xcmd_force_mode )				
		{
			addChar(&g_cmd[0],'r');
			strcat(g_cmd,"m");
		}

	}// End level


	else if (!strcmp(cmd1,"analyze") )
	{

		if (g_unlimited)
		{
			clearTC();
			g_tc.fixmovetime=0xffffffff;
			if (!g_level9)
			{
				strcpy(g_cmd,xcmd_lev9); 							//LEV 9 = unendlich
				g_level9=TRUE;
			}
			strcat(g_cmd,"s");
		}
		else
			strcpy(g_cmd,xcmd_analyze); 							//LEV 9 = unendlich


		g_start_search=TRUE;

		if (xcmd_force_mode)
		{
			xcmd_force_mode=FALSE;
			Log("force aus xcmd_force_mode: %d\n",xcmd_force_mode);
			addChar(&g_cmd[0],'r');
		}
	}


	else if (!strcmp(cmd1,"undo") )
	{
		strcpy(g_cmd,xcmd_undo);

		if(!xcmd_force_mode)
			strcpy(g_cmd,xcmd_undo);
		else
			strcpy(g_cmd,"9");

		if (g_tc.movestogo > 0)
			g_tc.movestogo--;

	}

	else if (!strcmp(cmd1,"remove") )
	{
		strcpy(g_cmd,xcmd_remove);

		if(!xcmd_force_mode)
			strcpy(g_cmd,xcmd_remove);		
		else
			strcpy(g_cmd,&xcmd_remove[1]);	

		if (g_tc.movestogo > 1)
			g_tc.movestogo=g_tc.movestogo-2;
		else
			g_tc.movestogo=0;

	}

	else if (!strcmp(cmd1,"go") )
	{
		strcpy(g_cmd,"s");
		g_start_search=TRUE;

		if (xcmd_force_mode)
		{
			xcmd_force_mode=FALSE;
			Log("force aus xcmd_force_mode: %d\n",xcmd_force_mode);
			addChar(&g_cmd[0],'r');
		}
	}


	else if (!strcmp(cmd1,"quit") )
	{
		machine->mame_data->exit_pending=TRUE;	
		InputProcessed();
		g_state=DRIVER_READY;
		return;
		
	}

	else if (TestMove(cmd1))	// Prüfen ob das ein Zug war
	{
		strncpy(g_cmd,cmd1,4);
		g_cmd[4]='\0';

		Log("xcmd_force_mode: %d\n",xcmd_force_mode);
		
		if (isPromoInput(cmd1))			//Promozug
		{
			strcat(g_cmd,"s");
			switch (tolower(cmd1[4]))				
			{
			case 'q': 
				strcat(g_cmd,xcmd_promo_q);
				break;
			case 'r': 
				strcat(g_cmd,xcmd_promo_r);
				break;
			case 'b': 
				strcat(g_cmd,xcmd_promo_b);
				break;
			case 'n': 
				strcat(g_cmd,xcmd_promo_n);
				break;
			default:
				break;
			} 
			
		}
		if (!xcmd_force_mode)
			g_start_search=TRUE;

		strcat(g_cmd,"s");
	}
	
	Log("g_cmd: %s\n",g_cmd);

// Befehlszeile gefunden, dann ausführen
//
	if (g_cmd[0]!=0)
	{
		g_cmd_inx=0;
		g_cmd_len=strlen(g_cmd);

		g_state=SENDCOMMAND;

	}else

// Keine Behlszeile erstellt, dann weiter
//
	{
		InputProcessed();
		g_state=DRIVER_READY;
	}

}

//------------------------------
// ProcessSENDCOMMAND                                          
//------------------------------
static void ProcessSENDCOMMAND(running_machine *machine)
{
	EMU_KEY_T *keycode;

	if (g_cmd_inx >= g_cmd_len)		//Ende Befehlsstring
	{		
		g_state=DRIVER_READY;
		InputProcessed();
		return;
	}
	
	if  (g_portIsReady && 
		 g_waitCnt > g_inputWait )				//Wartezeit

	{

		keycode=GetKeycode(g_keys,g_cmd[g_cmd_inx]);
    	input_port_set(machine,keycode->name,keycode->data);

//		input_port_write(machine, keycode->name, keycode->data, 0xff);


		if (g_start_search==TRUE && g_cmd_inx==(g_cmd_len-1 ))
		{
			g_state=SEARCHING;
			g_start_search=FALSE;
			g_break_search=FALSE;

			TimeControl(&g_tc);
		}

		g_last_cmd=g_cmd[g_cmd_inx];

		g_displayChanged=FALSE;
		g_portIsReady=FALSE;

		g_waitCnt=0;
		
		if (g_cmd[g_cmd_inx]=='m' ||												//Taste MEM
			g_cmd[g_cmd_inx]=='9' ||												//Taste 9 (Prüfen ob noch nötig)
			(g_cmd_inx > 4 && isPromoCmd(g_cmd) && isPromoPiece(g_cmd[g_cmd_inx]) ) )	//Promofigur bei Promozug

			g_state=SPECIALCOMMANDS;

		g_cmd_inx++;

	}

	if (g_cmd_inx >= g_cmd_len &&g_state==SEARCHING )
	{
		InputProcessed();

		if (!g_unlimited)									//Takt und Speed nach Ende der Befehlseingabe wieder auf Normal stellen
		{													 
			cpu_set_clock(machine->firstcpu,g_clock );		 
			video_set_speed_factor(100);					//100 enstpricht Faktor 1
		}
	}

}

//------------------------------
// ProcessSPECIALCOMMANDS                                         
//------------------------------
static void ProcessSPECIALCOMMANDS(void)
{
// Taste m = MEMO
//
	if (g_last_cmd=='m')							//Taste m = memo
	{
		if (g_waitCnt > g_specialWait)
		{
			if (strcmp(g_display,"MEM0") &&			//warten bis MEM0 im Display erscheint
				g_portIsReady)
			{
				PrintAndLog("Repeat command m (MEM)\n");

				g_cmd_inx--;

				g_state=SENDCOMMAND;
				return;

			}else
			{
				g_state=SENDCOMMAND;
				return;
			}
		}//End if g_waitCnt

	}//End if g_last_cmd = 'm'

// Promozug
//
	else if (isPromoPiece(g_last_cmd))					//Promozug Eingabe der Promofigur prüfen
	{	
		if (g_waitCnt > g_specialWait)
		{
			if (!strcmp(g_display,"Pr _") &&		//warten bis Promofigur im Display erscheint 
				g_portIsReady)						//Vor Eingabe der Promofigur Display = Pr _ nach Eingabe z.B. Pr d
			{
				PrintAndLog("Repeat Promo \n");

				g_cmd_inx--;

				g_state=SENDCOMMAND;
				return;

			}else
			{
				g_state=SENDCOMMAND;
				return;
			}
		}//End if g_waitCnt

	}//End if g_last_cmd = 'm'


// Taste 9 = Level 9, Zurück oder Schwarz
//
	else if(g_last_cmd=='9')
	{
		if (g_waitCnt > g_specialWait)
		{
			g_state=SENDCOMMAND;
			return;
		}
	}//End if g_last_cmd = '9'

//Alle anderen Befehle
//
	else 											
	{
		if (g_cmd_inx >= g_cmd_len)					//Letzter Befehl
		{
			InputProcessed();
			g_state=DRIVER_READY;
		}else
			g_state=SENDCOMMAND;	
	}

}

//------------------------------
// ProcessBESTMOVE                                         
//------------------------------
static void ProcessBESTMOVE(void)
{
	g_info_index = 0;								//Index für Infoanzeige zurücksetzen
	g_info_start = FALSE;

	if(g_unlimited)
	{
		if (g_tc.movestogo > 0)
		{
			g_tc.movestogo--;							//Zeitkontrolle 
			if (g_tc.movestogo == 0)						
				g_tc.movestogo=g_tc.movestogo_start;
		}
	}

	strcpy(g_bestmove,g_display);
	g_bestmove[0]=tolower(g_display[0]);							 
	g_bestmove[2]=tolower(g_display[2]);							 												 

	if (checkPromo(g_display) )						//Zug auf letze Reihe, Prüfe ob Bauernumwandlung
	{
		g_state=BESTMOVEPROMO;

		strcpy(g_cmd,xcmd_show_promo);

		g_cmd_len=strlen(g_cmd);
		g_cmd_inx=0;
		g_waitCnt=0;										 
		return;


	}else
	{
		g_bestmove[4]='\n';						
		SendBestmoveToGUI(g_bestmove);
		g_bestmove[0]='\0';
		g_state=DRIVER_READY;		
	}
}

//------------------------------
// ProcessBESTMOVEPROMO                                         
//------------------------------
static void ProcessBESTMOVEPROMO(running_machine *machine)
{
	EMU_KEY_T *keycode;

	assert(strlen(g_cmd)>0);

	if  (g_portIsReady && 
	     g_waitCnt > g_promoWait)			//Wartezeit verlängert, es kann vorkommen, dass in der Infoanzeige 
											//die Promofigur nicht erscheint wenn die Komandos zu scnell abgesezt werden
	{

		if (g_cmd_inx < g_cmd_len)
		{

// InfoAnzeige der Promofigur
//
			if (!strncmp(g_display,"Pr",2))
			{
				switch (tolower(g_display[3]))								//Bauernumwandlung
					{
					case 'd': 
						strcat(g_bestmove,"q");
						break;
					case 'T': 
						strcat(g_bestmove,"r");
						break;
					case 'L': 
						strcat(g_bestmove,"b");
						break;
					case '5': 
						strcat(g_bestmove,"n");
						break;
					default:
						break;
				} //end switch


				g_cmd_inx=g_cmd_len-1;					//letztes Zeichen -> = CLR

			}else if(!strncmp(g_display,"____",4))		//Keine Angaben im Display -EndeMarkierung Infoanzeige
														//Achtung 2 aufeinderfolgende Eingabe der Taste 0 ohne Änderung im Display -> Taste CLR wird nit erkannt
				g_cmd_inx=g_cmd_len-1;					//letztes Zeichen -> = CLR

		//end if strncmp

// Keine Infoanzeige, dann weiter
//
			keycode=GetKeycode(g_keys,g_cmd[g_cmd_inx]);
			input_port_set(machine,keycode->name,keycode->data);

			g_displayChanged=FALSE;
			g_portIsReady=FALSE;
			g_cmd_inx++;
			g_waitCnt=0;					 

		}

// Letztes Kommandozeichen verarbeitet
// -> Bestmove senden
		else
		{
			strcat(g_bestmove,"\n");

			SendBestmoveToGUI(g_bestmove);
			g_bestmove[0]='\0';

			g_state=DRIVER_READY;

		}//end g_cmd_inx

	}//end if g_portIsReady
}

//--------------------------------------------------------------------------
// End of  MOD RS
//--------------------------------------------------------------------------


/***************************************************************************
    CORE IMPLEMENTATION
***************************************************************************/

/*-------------------------------------------------
    mame_execute - run the core emulation
-------------------------------------------------*/

int mame_execute(core_options *options)
{
	int exit_pending = FALSE;
	int error = MAMERR_NONE;
	int firstgame = TRUE;
	int firstrun = TRUE;

//--------------------------------------------------------------------------
// Begin of   MOD RS
//--------------------------------------------------------------------------

	Thandle hThread;			// Handles auf die Threads
	int  dwThreadID;			// IDs der Threads

	setvbuf(stdin,NULL,_IONBF,0);
	setvbuf(stdout,NULL,_IONBF,0);

// Initialisiere Mutex und Condition Vars -> nur Linux
//
    LockCondMutex(MutexAvailable);
	LockCondMutex(MutexProcessed);

//  Events erzeugen
//
	CreateEventOrCond(HInputProcessed);
	CreateEventOrCond(HInputAvailable);

// Thread erzeugen
//
	BeginThread(hThread,ThreadFuncCheckInput,INPUT_TH,dwThreadID)

//--------------------------------------------------------------------------
// End of   MOD RS
//--------------------------------------------------------------------------

	/* loop across multiple hard resets */
	while (error == MAMERR_NONE && !exit_pending)
	{
		const game_driver *driver;
		running_machine *machine;
		mame_private *mame;
		callback_item *cb;
		astring gamename;

		/* specify the mame_options */
		mame_opts = options;

		/* convert the specified gamename to a driver */
		core_filename_extract_base(&gamename, options_get_string(mame_options(), OPTION_GAMENAME), TRUE);
		driver = driver_get_name(gamename);

		/* if no driver, use the internal empty driver */
		if (driver == NULL)
		{

			PrintAndLog("No Module selected\n"); //MOD RS
			return MAMERR_FATALERROR;			 //MOD RS

			//driver = &GAME_NAME(empty);		 //MOD RS
			//if (firstgame)					 //MOD RS
			//	started_empty = TRUE;			 //MOD RS
		}

		/* otherwise, perform validity checks before anything else */
		else if (mame_validitychecks(driver) != 0)
			return MAMERR_FAILED_VALIDITY;
		firstgame = FALSE;

		/* parse any INI files as the first thing */
		if (options_get_bool(options, OPTION_READCONFIG))
		{
			options_revert(mame_options(), OPTION_PRIORITY_INI);
			mame_parse_ini_files(mame_options(), driver);
		}

//--------------------------------------------------------------------------
// Start of   MOD RS
//--------------------------------------------------------------------------

		g_mmlog				=	options_get_bool(mame_options(),"mmlog");				//Logfile an ?
		g_unlimited			=	options_get_bool(mame_options(),"mmunlimited");			//Maximale Geschwindigkeit
		g_option_tc_delay	=	options_get_bool(mame_options(),"mmtcdelay");			//Eingabe Korrekturwert Zeitkontrolle

		if (options_get_int(mame_options(),"mmclock")!=0)								//Taktfrequnez (nur wenn auch eingegben
			g_clock=options_get_int(mame_options(),"mmclock");						

		if (g_unlimited)																
			options_set_float(mame_options(),"speed",100.0,OPTION_PRIORITY_INI);		//Maximale Geschwindigkeit -> speed = 100

		options_set_bool(mame_options(),"sound",0,OPTION_PRIORITY_INI);					//Sound aus

		g_profiler=FALSE;		//Profiler an/ausschalten (im Logfile wird das Profilerergebeniss angezeigt) nur zum Testen, nur bei DEBUG=1

		strcat(g_logfile,driver->name);
		strcat(g_logfile,".txt");

		if (g_unlimited && g_option_tc_delay==0)										//Performancemessung nur wenn mmunlimited und keine Korrekturvoragbe über mmtcdelay
			g_per_wait=600;
		else
			g_per_wait=0;


// Variabeln abhängig vom Modul
//

//-----------------------
// MM V (5.0)
//----------------------

		if (!strcmp(driver->name,"mm50"))
		{
			strcpy(g_myname,"Mephisto MM V (5.0)");

			g_emu=EMU_MM;
			g_keys=MM_KEYS;

			g_org_clock=4915200;
			if (g_clock==0)								//Falls keine Vorgabe über -mmclock
			{
				if (g_unlimited)
					g_clock=1250000;					//Default für beste Geschwindigkeit bei unlimited
				else									
					g_clock=g_org_clock;				//Sonst die Orginal Taktfrequenz
			}

			if (g_unlimited)
			{
				g_bestmoveWait=300;
				g_specialWait=300;
				g_inputWait=30;
				g_promoWait=60;
				g_inputTimeout=200;

				g_tc_delay=1000;						//Korreturwert Zeitkontrolle (Zeit pro Zug, die benötigt wird für Zugein/ausgabe)

				g_InputCheckStart=1000;
			}
			else
			{
				g_bestmoveWait=80;
				g_specialWait=300;
				g_inputWait=30;
				g_promoWait=60;
				g_inputTimeout=200;
				g_InputCheckStart=100;
				g_input_clock=1250000;
				g_input_speed=10000;
			}
			

			strcpy(xcmd_st3   ,"l0s");
			strcpy(xcmd_st5   ,"l1s");
			strcpy(xcmd_st10  ,"l2s");
			strcpy(xcmd_st20  ,"l3s");
			strcpy(xcmd_st60  ,"l4s");
			strcpy(xcmd_st120 ,"l5s");
			strcpy(xcmd_st600 ,"l7s");
			strcpy(xcmd_st360 ,"l8s");
			
			strcpy(xcmd_level40_2,"l6s");

			strcpy(xcmd_lev9,"l9s");

			strcpy(xcmd_level0_5, "ll1s");
			strcpy(xcmd_level0_10,"ll4s");
			strcpy(xcmd_level0_15,"ll6s");
			strcpy(xcmd_level0_30,"ll7s");
			strcpy(xcmd_level0_60,"ll8s");

			strcpy(xcmd_analyze,"l9ss");

			strcpy(xcmd_undo,"m9r");
			strcpy(xcmd_remove,"m99r");
			strcpy(xcmd_setboard,"pss");
			strcpy(xcmd_setboard_col_w,"p0ss");
			strcpy(xcmd_setboard_col_b,"p9ss");

			strcpy(xcmd_show_promo,"ia00r");
			strcpy(xcmd_promo_q,"e");
			strcpy(xcmd_promo_r,"d");
			strcpy(xcmd_promo_b,"c");
			strcpy(xcmd_promo_n,"b");

			strcpy(xcmd_force,"rm");			
			strcpy(xcmd_leave_force,"rs");

			strcpy(xcmd_roll_diplay,"lllsr");
			g_rollDisplay=TRUE;
			g_rollDisplay_exits=TRUE;


//-----------------------
// MM V (5.1)
//----------------------

		}else if (!strcmp(driver->name,"mm5"))
		{
			strcpy(g_myname,"Mephisto MM V (5.1)");

			g_emu=EMU_MM;
			g_keys=MM_KEYS;

			g_org_clock=4915200;
			if (g_clock==0)								//Falls keine Vorgabe über -mmclock
			{
				if (g_unlimited)
					g_clock=1250000;					//Default für beste Geschwindigkeit bei unlimited
				else									
					g_clock=g_org_clock;				//Sonst die Orginal Taktfrequenz
			}

			if (g_unlimited)
			{
				g_bestmoveWait=300;	
				g_specialWait=300;
				g_inputWait=30;
				g_promoWait=60;
				g_inputTimeout=200;

				g_tc_delay=800;

				g_InputCheckStart=1000;
			}
			else
			{
				g_bestmoveWait=80;
				g_specialWait=300;
				g_inputWait=30;
				g_promoWait=60;
				g_inputTimeout=200;
				g_InputCheckStart=100;
				g_input_clock=1250000;
				g_input_speed=10000;
			}

			strcpy(xcmd_st3   ,"l0s");
			strcpy(xcmd_st5   ,"l1s");
			strcpy(xcmd_st10  ,"l2s");
			strcpy(xcmd_st20  ,"l3s");
			strcpy(xcmd_st60  ,"l4s");
			strcpy(xcmd_st120 ,"l5s");
			strcpy(xcmd_st600 ,"l7s");
			strcpy(xcmd_st360 ,"l8s");
			
			strcpy(xcmd_level40_2,"l6s");

			strcpy(xcmd_lev9,"l9s");

			xcmd_level0_5[0]='\0';
			xcmd_level0_10[0]='\0';
			xcmd_level0_15[0]='\0';
			xcmd_level0_30[0]='\0';
			xcmd_level0_60[0]='\0';

			strcpy(xcmd_analyze,"l9ss");

			strcpy(xcmd_undo,"m9r");
			strcpy(xcmd_remove,"m99r");
			strcpy(xcmd_setboard,"pss");
			strcpy(xcmd_setboard_col_w,"p0ss");
			strcpy(xcmd_setboard_col_b,"p9ss");

			strcpy(xcmd_show_promo,"ia00r");
			strcpy(xcmd_promo_q,"e");
			strcpy(xcmd_promo_r,"d");
			strcpy(xcmd_promo_b,"c");
			strcpy(xcmd_promo_n,"b");

			strcpy(xcmd_force,"rm");			
			strcpy(xcmd_leave_force,"rs");

			strcpy(xcmd_roll_diplay,"lllsr");
			g_rollDisplay=TRUE;
			g_rollDisplay_exits=TRUE;

//-----------------------
// MM IV
//----------------------

		}else if (!strcmp(driver->name,"mm4"))
		{
			strcpy(g_myname,"Mephisto MM IV");

			g_emu=EMU_MM;
			g_keys=MM_KEYS;

			g_org_clock=4915200;
			if (g_clock==0)								//Falls keine Vorgabe über -mmclock
			{
				if (g_unlimited)
					g_clock=1250000;					//Default für beste Geschwindigkeit bei unlimited
				else									
					g_clock=g_org_clock;				//Sonst die Orginal Taktfrequenz
			}

			if (g_unlimited)
			{
				g_bestmoveWait=200;	
				g_specialWait=300;
				g_inputWait=10;
				g_promoWait=20;
				g_inputTimeout=50;

				g_tc_delay=600;

				g_InputCheckStart=1000;
			}
			else
			{
				g_bestmoveWait=40;	
				g_specialWait=300;
				g_inputWait=6;
				g_promoWait=20;
				g_inputTimeout=50;
				g_InputCheckStart=100;
				g_input_clock=1250000;
				g_input_speed=10000;
			}

			strcpy(xcmd_st3   ,"l0s");
			strcpy(xcmd_st5   ,"l1s");
			strcpy(xcmd_st10  ,"l2s");
			strcpy(xcmd_st20  ,"l3s");
			strcpy(xcmd_st60  ,"l4s");
			strcpy(xcmd_st120 ,"l5s");
			strcpy(xcmd_st600 ,"l7s");
			strcpy(xcmd_st720 ,"l8s");
			

			strcpy(xcmd_level40_2,"l6s");

			strcpy(xcmd_lev9,"l9s");

			xcmd_level0_5[0]='\0';
			xcmd_level0_10[0]='\0';
			xcmd_level0_15[0]='\0';
			xcmd_level0_30[0]='\0';
			xcmd_level0_60[0]='\0';

			strcpy(xcmd_analyze,"l9ss");

			strcpy(xcmd_undo,"m9r");
			strcpy(xcmd_remove,"m99r");
			strcpy(xcmd_setboard,"pss");
			strcpy(xcmd_setboard_col_w,"p0ss");
			strcpy(xcmd_setboard_col_b,"p9ss");

			strcpy(xcmd_show_promo,"ia00r");
			strcpy(xcmd_promo_q,"e");
			strcpy(xcmd_promo_r,"d");
			strcpy(xcmd_promo_b,"c");
			strcpy(xcmd_promo_n,"b");

			strcpy(xcmd_force,"rm");			
			strcpy(xcmd_leave_force,"rs");

			strcpy(xcmd_roll_diplay,"llsr");
			g_rollDisplay=TRUE;
			g_rollDisplay_exits=TRUE;

//-----------------------
// Rebell 5.0
//----------------------

		}else if (!strcmp(driver->name,"rebel5"))
		{
			strcpy(g_myname,"Mephisto MM Rebell 5.0");

			g_emu=EMU_MM;
			g_keys=MM_KEYS;

			g_org_clock=4915200;
			if (g_clock==0)								//Falls keine Vorgabe über -mmclock
			{
				if (g_unlimited)
					g_clock=g_org_clock;				//Rebell 5.0 muss mit Orginal Taktfrequenz laufen !!!
				else									
					g_clock=g_org_clock;				 
			}

			if (g_unlimited)
			{
				g_bestmoveWait=200;	
				g_specialWait=100;
				g_inputWait=10;
				g_promoWait=20;
				g_inputTimeout=50;

				g_tc_delay=600;

				g_InputCheckStart=1000;
			}
			else
			{
				g_bestmoveWait=40;	
				g_specialWait=100;
				g_inputWait=6;
				g_promoWait=20;
				g_inputTimeout=50;
				g_InputCheckStart=100;
				g_input_clock=g_org_clock;
				g_input_speed=10000;
			}

			strcpy(xcmd_st3   ,"l0s");
			strcpy(xcmd_st5   ,"l1s");
			strcpy(xcmd_st10  ,"l2s");
			strcpy(xcmd_st20  ,"l3s");
			strcpy(xcmd_st60  ,"l4s");
			strcpy(xcmd_st120 ,"l5s");
			strcpy(xcmd_st600 ,"l7s");
			strcpy(xcmd_st720 ,"l8s");

			strcpy(xcmd_level40_2,"l6s");

			strcpy(xcmd_lev9,"l9s");

			strcpy(xcmd_level0_5, "ll1s");
			strcpy(xcmd_level0_10,"ll4s");
			strcpy(xcmd_level0_15,"ll6s");
			strcpy(xcmd_level0_30,"ll7s");
			strcpy(xcmd_level0_60,"ll8s");

			strcpy(xcmd_analyze,"l9ss");

			strcpy(xcmd_undo,"m9r");
			strcpy(xcmd_remove,"m99r");
			strcpy(xcmd_setboard,"pss");
			strcpy(xcmd_setboard_col_w,"p0ss");
			strcpy(xcmd_setboard_col_b,"p9ss");

			strcpy(xcmd_show_promo,"ia00r");
			strcpy(xcmd_promo_q,"e");
			strcpy(xcmd_promo_r,"d");
			strcpy(xcmd_promo_b,"c");
			strcpy(xcmd_promo_n,"b");

			strcpy(xcmd_force,"rm");			
			strcpy(xcmd_leave_force,"rs");

			xcmd_roll_diplay[0]='\0';					

			g_rollDisplay_exits=FALSE;			//Rebell hat keine rollierende Anzeige 
			                                            
//-----------------------
// Glasgow
//----------------------

		}else if (!strcmp(driver->name,"glasgow"))
		{

			strcpy(g_myname,"Mephisto III S Glasgow");

			g_emu=EMU_GLASGOW;
			g_keys=GLASGOW_KEYS;

			g_org_clock=12000000;
			if (g_clock==0)								//Falls keine Vorgabe über -mmclock
			{
				if (g_unlimited)
					g_clock=3000000;					//Default für beste Geschwindigkeit bei unlimited
				else									
					g_clock=g_org_clock;				//Sonst die Orginal Taktfrequenz
			}

			if (g_unlimited)
			{
				g_bestmoveWait=100;	
				g_specialWait=100;
				g_inputWait=10;
				g_promoWait=20;
				g_inputTimeout=50;

				g_tc_delay=2000;

				g_InputCheckStart=1000;
			}
			else
			{
				g_bestmoveWait=150;	
				g_specialWait=100;
				g_inputWait=6;
				g_promoWait=20;
				g_inputTimeout=50;
				g_InputCheckStart=100;
				g_input_clock=3000000;
				g_input_speed=10000;
			}

			strcpy(xcmd_st3   ,"l0s");
			strcpy(xcmd_st5   ,"l1s");
			strcpy(xcmd_st10  ,"l2s");
			strcpy(xcmd_st20  ,"l3s");
			strcpy(xcmd_st60  ,"l4s");
			strcpy(xcmd_st120 ,"l5s");
			strcpy(xcmd_st600 ,"l8s00s10s00s");

			strcpy(xcmd_level40_2,"l6s");

			strcpy(xcmd_lev9,"l9s");

			xcmd_level0_5[0]='\0';
			xcmd_level0_10[0]='\0';
			xcmd_level0_15[0]='\0';
			xcmd_level0_30[0]='\0';
			xcmd_level0_60[0]='\0';

			strcpy(xcmd_analyze,"l9ss");

			strcpy(xcmd_undo,"m9r");
			strcpy(xcmd_remove,"m99r");
			strcpy(xcmd_setboard,"pss");
			strcpy(xcmd_setboard_col_w,"p0ss");
			strcpy(xcmd_setboard_col_b,"p9ss");

			strcpy(xcmd_show_promo,"ia0r");
			strcpy(xcmd_promo_q,"f");
			strcpy(xcmd_promo_r,"e");
			strcpy(xcmd_promo_b,"d");
			strcpy(xcmd_promo_n,"c");

			strcpy(xcmd_force,"rm");			
			strcpy(xcmd_leave_force,"rs");

			xcmd_roll_diplay[0]='\0';			//rollierende Anzeige ist beim glasgow standardmässig an	 
			g_rollDisplay=TRUE;
			g_rollDisplay_exits=TRUE;

//-----------------------
// Dallas
//----------------------

		}else if (!strcmp(driver->name,"dallas"))
		{

			strcpy(g_myname,"Mephisto Dallas");

			g_emu=EMU_GLASGOW;
			g_keys=GLASGOW_KEYS;

			g_org_clock=12000000;
			if (g_clock==0)								//Falls keine Vorgabe über -mmclock
			{
				if (g_unlimited)
					g_clock=3000000;					//Default für beste Geschwindigkeit bei unlimited
				else									
					g_clock=g_org_clock;				//Sonst die Orginal Taktfrequenz
			}

			if (g_unlimited)
			{
				g_bestmoveWait=100;	
				g_specialWait=100;
				g_inputWait=30;
				g_promoWait=20;
				g_inputTimeout=50;

				g_tc_delay=2000;

				g_InputCheckStart=1000;
			}
			else
			{
				g_bestmoveWait=50;	
				g_specialWait=100;
				g_inputWait=30;
				g_promoWait=20;
				g_inputTimeout=50;
				g_InputCheckStart=100;
				g_input_clock=5000000;
				g_input_speed=10000;
			}

			strcpy(xcmd_st3   ,"l0s");
			strcpy(xcmd_st5   ,"l1s");
			strcpy(xcmd_st10  ,"l2s");
			strcpy(xcmd_st20  ,"l3s");
			strcpy(xcmd_st60  ,"l4s");
			strcpy(xcmd_st120 ,"l5s");
			strcpy(xcmd_st600 ,"l8s00s10s00s");

			strcpy(xcmd_level40_2,"l6s");

			strcpy(xcmd_lev9,"l9s");

			strcpy(xcmd_level0_5, "l7s00s05s00s");
			strcpy(xcmd_level0_10,"l7s00s10s00s");
			strcpy(xcmd_level0_15,"l7s00s15s00s");
			strcpy(xcmd_level0_30,"l7s00s30s00s");
			strcpy(xcmd_level0_60,"l7s01s00s00s");

			strcpy(xcmd_analyze,"l9ss");

			strcpy(xcmd_undo,"m9r");
			strcpy(xcmd_remove,"m99r");
			strcpy(xcmd_setboard,"pss");
			strcpy(xcmd_setboard_col_w,"p0ss");
			strcpy(xcmd_setboard_col_b,"p9ss");

			strcpy(xcmd_show_promo,"ia0r");
			strcpy(xcmd_promo_q,"f");
			strcpy(xcmd_promo_r,"e");
			strcpy(xcmd_promo_b,"d");
			strcpy(xcmd_promo_n,"c");

			strcpy(xcmd_force,"rm");			
			strcpy(xcmd_leave_force,"rs");

			xcmd_roll_diplay[0]='\0';			//rollierende Anzeige ist beim glasgow standardmässig an	 
			g_rollDisplay=TRUE;
			g_rollDisplay_exits=TRUE;

//-----------------------
// Amsterdam
//----------------------

		}else if (!strcmp(driver->name,"amsterd"))
		{

			strcpy(g_myname,"Mephisto Amsterdam");

			g_emu=EMU_GLASGOW;
			g_keys=GLASGOW_NEW_KEYS;

			g_org_clock=12000000;
			if (g_clock==0)								//Falls keine Vorgabe über -mmclock
			{
				if (g_unlimited)
					g_clock=3000000;					//Default für beste Geschwindigkeit bei unlimited
				else									
					g_clock=g_org_clock;				//Sonst die Orginal Taktfrequenz
			}

			if (g_unlimited)
			{
				g_bestmoveWait=100;	
				g_specialWait=100;
				g_inputWait=30;
				g_promoWait=20;
				g_inputTimeout=50;

				g_tc_delay=2000;

				g_InputCheckStart=1000;
			}
			else
			{
				g_bestmoveWait=50;	
				g_specialWait=100;
				g_inputWait=30;
				g_promoWait=20;
				g_inputTimeout=50;
				g_InputCheckStart=100;
				g_input_clock=5000000;
				g_input_speed=10000;
			}

			strcpy(xcmd_st3   ,"l0s");
			strcpy(xcmd_st5   ,"l1s");
			strcpy(xcmd_st10  ,"l2s");
			strcpy(xcmd_st20  ,"l3s");
			strcpy(xcmd_st60  ,"l4s");
			strcpy(xcmd_st120 ,"l5s");
			strcpy(xcmd_st600 ,"l8s00s10s00s");

			strcpy(xcmd_level40_2,"l6s");

			strcpy(xcmd_lev9,"l9s");

			xcmd_level0_5[0]='\0';
			xcmd_level0_10[0]='\0';
			xcmd_level0_15[0]='\0';
			xcmd_level0_30[0]='\0';
			xcmd_level0_60[0]='\0';

			strcpy(xcmd_analyze,"l9ss");

			strcpy(xcmd_undo,"m9r");
			strcpy(xcmd_remove,"m99r");
			strcpy(xcmd_setboard,"pss");
			strcpy(xcmd_setboard_col_w,"p0ss");
			strcpy(xcmd_setboard_col_b,"p9ss");

			strcpy(xcmd_show_promo,"ia0r");
			strcpy(xcmd_promo_q,"f");
			strcpy(xcmd_promo_r,"e");
			strcpy(xcmd_promo_b,"d");
			strcpy(xcmd_promo_n,"c");

			strcpy(xcmd_force,"rm");			
			strcpy(xcmd_leave_force,"rs");

			xcmd_roll_diplay[0]='\0';			//rollierende Anzeige ist beim glasgow standardmässig an	 
			g_rollDisplay=TRUE;
			g_rollDisplay_exits=TRUE;

//-----------------------
// Dallas 16 Bit
//----------------------

		}else if (!strcmp(driver->name,"dallas16"))
		{

			strcpy(g_myname,"Mephisto Dallas 16 Bit");

			g_emu=EMU_GLASGOW;
			g_keys=GLASGOW_NEW_KEYS;

			g_org_clock=12000000;
			if (g_clock==0)								//Falls keine Vorgabe über -mmclock
			{
				if (g_unlimited)
					g_clock=3000000;					//Default für beste Geschwindigkeit bei unlimited
				else									
					g_clock=g_org_clock;				//Sonst die Orginal Taktfrequenz
			}

			if (g_unlimited)
			{
				g_bestmoveWait=100;	
				g_specialWait=100;
				g_inputWait=30;
				g_promoWait=20;
				g_inputTimeout=50;

				g_tc_delay=2000;

				g_InputCheckStart=1000;
			}
			else
			{
				g_bestmoveWait=50;	
				g_specialWait=100;
				g_inputWait=30;
				g_promoWait=20;
				g_inputTimeout=50;
				g_InputCheckStart=100;
				g_input_clock=5000000;
				g_input_speed=10000;
			}

			strcpy(xcmd_st3   ,"l0s");
			strcpy(xcmd_st5   ,"l1s");
			strcpy(xcmd_st10  ,"l2s");
			strcpy(xcmd_st20  ,"l3s");
			strcpy(xcmd_st60  ,"l4s");
			strcpy(xcmd_st120 ,"l5s");
			strcpy(xcmd_st600 ,"l8s00s10s00s");

			strcpy(xcmd_level40_2,"l6s");

			strcpy(xcmd_lev9,"l9s");

			strcpy(xcmd_level0_5, "l7s00s05s00s");
			strcpy(xcmd_level0_10,"l7s00s10s00s");
			strcpy(xcmd_level0_15,"l7s00s15s00s");
			strcpy(xcmd_level0_30,"l7s00s30s00s");
			strcpy(xcmd_level0_60,"l7s01s00s00s");

			strcpy(xcmd_analyze,"l9ss");

			strcpy(xcmd_undo,"m9r");
			strcpy(xcmd_remove,"m99r");
			strcpy(xcmd_setboard,"pss");
			strcpy(xcmd_setboard_col_w,"p0ss");
			strcpy(xcmd_setboard_col_b,"p9ss");

			strcpy(xcmd_show_promo,"ia0r");
			strcpy(xcmd_promo_q,"f");
			strcpy(xcmd_promo_r,"e");
			strcpy(xcmd_promo_b,"d");
			strcpy(xcmd_promo_n,"c");

			strcpy(xcmd_force,"rm");			
			strcpy(xcmd_leave_force,"rs");

			xcmd_roll_diplay[0]='\0';			//rollierende Anzeige ist beim glasgow standardmässig an	 
			g_rollDisplay=TRUE;
			g_rollDisplay_exits=TRUE;

//-----------------------
// Dallas 32
//----------------------

		}else if (!strcmp(driver->name,"dallas32") ) 
		{


			strcpy(g_myname,"Mephisto Dallas 32 Bit");

			g_emu=EMU_GLASGOW;
			g_keys=GLASGOW_NEW_KEYS;

			g_org_clock=14000000;
			if (g_clock==0)								//Falls keine Vorgabe über -mmclock
			{
				if (g_unlimited)
					g_clock=8000000;					//Default für beste Geschwindigkeit bei unlimited
				else									
					g_clock=g_org_clock;				//Sonst die Orginal Taktfrequenz
			}

			if (g_unlimited)
			{
				g_bestmoveWait=100;	
				g_specialWait=100;
				g_inputWait=30;
				g_promoWait=20;
				g_inputTimeout=50;

				g_tc_delay=1300;

				g_InputCheckStart=100;
			}
			else
			{
				g_bestmoveWait=50;	
				g_specialWait=100;
				g_inputWait=30;
				g_promoWait=20;
				g_inputTimeout=50;
				g_InputCheckStart=100;
				g_input_clock=5000000;
				g_input_speed=10000;
			}

			strcpy(xcmd_st3   ,"l0s");
			strcpy(xcmd_st5   ,"l1s");
			strcpy(xcmd_st10  ,"l2s");
			strcpy(xcmd_st20  ,"l3s");
			strcpy(xcmd_st60  ,"l4s");
			strcpy(xcmd_st120 ,"l5s");
			strcpy(xcmd_st600 ,"l8s00s10s00s");

			strcpy(xcmd_level40_2,"l6s");

			strcpy(xcmd_level0_5, "l7s00s05s00s");
			strcpy(xcmd_level0_10,"l7s00s10s00s");
			strcpy(xcmd_level0_15,"l7s00s15s00s");
			strcpy(xcmd_level0_30,"l7s00s30s00s");
			strcpy(xcmd_level0_60,"l7s01s00s00s");

			strcpy(xcmd_lev9,"l9s");				

			strcpy(xcmd_analyze,"l9ss");

			strcpy(xcmd_undo,"m9r");
			strcpy(xcmd_remove,"m99r");
			strcpy(xcmd_setboard,"pss");
			strcpy(xcmd_setboard_col_w,"p0ss");
			strcpy(xcmd_setboard_col_b,"p9ss");

			strcpy(xcmd_show_promo,"ia0r");
			strcpy(xcmd_promo_q,"f");
			strcpy(xcmd_promo_r,"e");
			strcpy(xcmd_promo_b,"d");
			strcpy(xcmd_promo_n,"c");

			strcpy(xcmd_force,"rm");			
			strcpy(xcmd_leave_force,"rs");

			xcmd_roll_diplay[0]='\0';			//rollierende Anzeige ist beim dallas standardmässig an
			g_rollDisplay=TRUE;
			g_rollDisplay_exits=TRUE;

//-----------------------
// Roma 32
//----------------------

		}else if (!strcmp(driver->name,"roma32") )
		{

			strcpy(g_myname,"Mephisto Roma 32 Bit");

			g_emu=EMU_GLASGOW;
			g_keys=GLASGOW_NEW_KEYS;

			g_org_clock=14000000;
			if (g_clock==0)								//Falls keine Vorgabe über -mmclock
			{
				if (g_unlimited)
					g_clock=8000000;					//Default für beste Geschwindigkeit bei unlimited
				else									
					g_clock=g_org_clock;				//Sonst die Orginal Taktfrequenz
			}

			if (g_unlimited)
			{
				g_bestmoveWait=200;	
				g_specialWait=100;
				g_inputWait=70;
				g_promoWait=20;
				g_inputTimeout=50;

				g_tc_delay=2000;

				g_InputCheckStart=100;
			}
			else
			{
				g_bestmoveWait=100;	
				g_specialWait=100;
				g_inputWait=70;
				g_promoWait=20;
				g_inputTimeout=50;
				g_InputCheckStart=100;
				g_input_clock=5000000;
				g_input_speed=10000;
			}

			strcpy(xcmd_st3   ,"l02s");
			strcpy(xcmd_st5   ,"l03s");
			strcpy(xcmd_st10  ,"l04s");
			strcpy(xcmd_st20  ,"l50s00s00s20s");
			strcpy(xcmd_st60  ,"l50s00s01s00s");
			strcpy(xcmd_st120 ,"l50s00s02s00s");

			strcpy(xcmd_level40_2,"l40s");

			strcpy(xcmd_lev9,"l99s");				

			strcpy(xcmd_level0_5, "l32s");
			strcpy(xcmd_level0_10,"l34s");
			strcpy(xcmd_level0_15,"l35s");
			strcpy(xcmd_level0_30,"l37s");
			strcpy(xcmd_level0_60,"l38s01s00s00s");

			strcpy(xcmd_analyze,"l9ss");

			strcpy(xcmd_undo,"m9r");
			strcpy(xcmd_remove,"m99r");
			strcpy(xcmd_setboard,"pss");
			strcpy(xcmd_setboard_col_w,"p0ss");
			strcpy(xcmd_setboard_col_b,"p9ss");

			strcpy(xcmd_show_promo,"ia0r");
			strcpy(xcmd_promo_q,"f");
			strcpy(xcmd_promo_r,"e");
			strcpy(xcmd_promo_b,"d");
			strcpy(xcmd_promo_n,"c");

			strcpy(xcmd_force,"rm");			
			strcpy(xcmd_leave_force,"rs");

			strcpy(xcmd_roll_diplay,"llsr");;			//rollierende Anzeige 

			g_rollDisplay_exits=TRUE;

		}

// Initialisierungen
//

		g_error=FALSE;
		xcmd_force_mode=FALSE;
		g_start_search=FALSE;

		g_repeat_input=FALSE;

		g_InputCheck=g_InputCheckStart;
		
		g_state=DRIVER_START;
		g_side=WHITE;

		g_xboard_mode = FALSE;

		g_start_time_sc=0;
		g_end_time_sc=0;
		g_time_per_sec=0;
	
		g_TimeCheck=TIMECHECK;

		g_bestmove[0]='\0';

		//g_stat.cpuexec_timeslice=0;
		//g_stat.timercb=0;
		//g_stat.endtime=0;
		//g_stat.starttime=GetTime();

// Ausgabe Version und Engine
//
		PrintAndLog("Mess Mephisto Version: %s\n\n",build_version);
		PrintAndLog("%s\n\n",g_myname);

		if (g_profiler)	
			profiler_start();

//--------------------------------------------------------------------------
// End of   MOD RS
//--------------------------------------------------------------------------

		/* create the machine structure and driver */
		machine = global_alloc(running_machine(driver));
		mame = machine->mame_data;

		/* start in the "pre-init phase" */
		mame->current_phase = MAME_PHASE_PREINIT;

		/* looooong term: remove this */
		global_machine = machine;

		/* use try/catch for deep error recovery */
		try
		{
			int settingsloaded;

			/* move to the init phase */
			mame->current_phase = MAME_PHASE_INIT;

			/* if we have a logfile, set up the callback */
			mame->logerror_callback_list = NULL;
			if (options_get_bool(mame_options(), OPTION_LOG))
			{
				file_error filerr = mame_fopen(SEARCHPATH_DEBUGLOG, "error.log", OPEN_FLAG_WRITE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_PATHS, &mame->logfile);
				assert_always(filerr == FILERR_NONE, "unable to open log file");
				add_logerror_callback(machine, logfile_callback);
			}

			/* then finish setting up our local machine */
			init_machine(machine);

			/* load the configuration settings and NVRAM */
			settingsloaded = config_load_settings(machine);
			nvram_load(machine);
			sound_mute(machine, FALSE);

			/* display the startup screens */
//			ui_display_startup_screens(machine, firstrun, !settingsloaded);		//MOD RS
			osd_ticks();														//MOD RS Timer init

			firstrun = FALSE;

			/* perform a soft reset -- this takes us to the running phase */
			soft_reset(machine, NULL, 0);

			/* run the CPUs until a reset or exit */
			mame->hard_reset_pending = FALSE;
			while ((!mame->hard_reset_pending && !mame->exit_pending) || mame->saveload_pending_file.len() != 0)
			{
				profiler_mark_start(PROFILER_EXTRA);

//--------------------------------------------------------------------------
// Start of   MOD RS
//--------------------------------------------------------------------------

				if (g_profiler)	
					LogProfiler(machine);

				switch (g_state)
				{

					case DRIVER_START: 
					{
						ProcessDRIVER_START(machine);
						break;
					}

					case DRIVER_READY: 
					{
						ProcessDRIVER_READY();
						break;
					}

					case SEARCHING: 
					{
						ProcessSEARCHING(machine);
						break;
					}

					case PARSEINPUT:
					{
						ProcessPARSEINPUT(machine);
						break;
					}

					case SENDCOMMAND:
					{
						ProcessSENDCOMMAND(machine);
						break;
					}

					case SPECIALCOMMANDS:
					{
						ProcessSPECIALCOMMANDS();
						break;
					}

					case BESTMOVE:
					{
						ProcessBESTMOVE();
						break;
					}


					case BESTMOVEPROMO:
					{
						ProcessBESTMOVEPROMO(machine);
						break;
					}

				}//End switch



//--------------------------------------------------------------------------
// End of   MOD RS
//--------------------------------------------------------------------------

				/* execute CPUs if not paused */
				if (!mame->paused)
					cpuexec_timeslice(machine);

				/* otherwise, just pump video updates through */
				else
					video_frame_update(machine, FALSE);

				/* handle save/load */
				if (mame->saveload_schedule_callback != NULL)
					(*mame->saveload_schedule_callback)(machine);

				profiler_mark_end();
			}

			exit_flag=TRUE;			//MOD RS

			/* and out via the exit phase */
			mame->current_phase = MAME_PHASE_EXIT;

			/* save the NVRAM and configuration */
			sound_mute(machine, TRUE);
			nvram_save(machine);
			//config_save_settings(machine);	//MOD RS
		}
		catch (emu_fatalerror &fatal)
		{
			mame_printf_error("%s\n", fatal.string());
			error = MAMERR_FATALERROR;
			if (fatal.exitcode() != 0)
				error = fatal.exitcode();
		}
		catch (emu_exception &)
		{
			mame_printf_error("Caught unhandled emulator exception\n");
			error = MAMERR_FATALERROR;
		}
		catch (std::bad_alloc &)
		{
			mame_printf_error("Out of memory!\n");
			error = MAMERR_FATALERROR;
		}

		/* call all exit callbacks registered */
		for (cb = mame->exit_callback_list; cb; cb = cb->next)
			(*cb->func.exit)(machine);

		/* close the logfile */
		if (mame->logfile != NULL)
			mame_fclose(mame->logfile);

		/* grab data from the MAME structure before it goes away */
		if (mame->new_driver_pending != NULL)
		{
			options_set_string(mame_options(), OPTION_GAMENAME, mame->new_driver_pending->name, OPTION_PRIORITY_CMDLINE);
			firstrun = TRUE;
		}
		exit_pending = mame->exit_pending;

		/* destroy the machine */
		global_free(machine);

		/* reset the options */
		mame_opts = NULL;
	}

	/* return an error */
	return error;
}


/*-------------------------------------------------
    mame_options - accesses the options for the
    currently running emulation
-------------------------------------------------*/

core_options *mame_options(void)
{
	assert(mame_opts != NULL);
	return mame_opts;
}



/*-------------------------------------------------
    mame_get_phase - return the current program
    phase
-------------------------------------------------*/

int mame_get_phase(running_machine *machine)
{
	mame_private *mame = machine->mame_data;
	return mame->current_phase;
}


/*-------------------------------------------------
    add_frame_callback - request a callback on
    frame update
-------------------------------------------------*/

void add_frame_callback(running_machine *machine, void (*callback)(running_machine *))
{
	mame_private *mame = machine->mame_data;
	callback_item *cb, **cur;

	assert_always(mame_get_phase(machine) == MAME_PHASE_INIT, "Can only call add_frame_callback at init time!");

	/* allocate memory */
	cb = auto_alloc(machine, callback_item);

	/* add us to the end of the list */
	cb->func.frame = callback;
	cb->next = NULL;
	for (cur = &mame->frame_callback_list; *cur; cur = &(*cur)->next) ;
	*cur = cb;
}


/*-------------------------------------------------
    add_reset_callback - request a callback on
    reset
-------------------------------------------------*/

void add_reset_callback(running_machine *machine, void (*callback)(running_machine *))
{
	mame_private *mame = machine->mame_data;
	callback_item *cb, **cur;

	assert_always(mame_get_phase(machine) == MAME_PHASE_INIT, "Can only call add_reset_callback at init time!");

	/* allocate memory */
	cb = auto_alloc(machine, callback_item);

	/* add us to the end of the list */
	cb->func.reset = callback;
	cb->next = NULL;
	for (cur = &mame->reset_callback_list; *cur; cur = &(*cur)->next) ;
	*cur = cb;
}


/*-------------------------------------------------
    add_pause_callback - request a callback on
    pause
-------------------------------------------------*/

void add_pause_callback(running_machine *machine, void (*callback)(running_machine *, int))
{
	mame_private *mame = machine->mame_data;
	callback_item *cb, **cur;

	assert_always(mame_get_phase(machine) == MAME_PHASE_INIT, "Can only call add_pause_callback at init time!");

	/* allocate memory */
	cb = auto_alloc(machine, callback_item);

	/* add us to the end of the list */
	cb->func.pause = callback;
	cb->next = NULL;
	for (cur = &mame->pause_callback_list; *cur; cur = &(*cur)->next) ;
	*cur = cb;
}


/*-------------------------------------------------
    add_exit_callback - request a callback on
    termination
-------------------------------------------------*/

void add_exit_callback(running_machine *machine, void (*callback)(running_machine *))
{
	mame_private *mame = machine->mame_data;
	callback_item *cb;

	assert_always(mame_get_phase(machine) == MAME_PHASE_INIT, "Can only call add_exit_callback at init time!");

	/* allocate memory */
	cb = auto_alloc(machine, callback_item);

	/* add us to the head of the list */
	cb->func.exit = callback;
	cb->next = mame->exit_callback_list;
	mame->exit_callback_list = cb;
}


/*-------------------------------------------------
    mame_frame_update - handle update tasks for a
    frame boundary
-------------------------------------------------*/

void mame_frame_update(running_machine *machine)
{
	callback_item *cb;

	/* call all registered frame callbacks */
	for (cb = machine->mame_data->frame_callback_list; cb; cb = cb->next)
		(*cb->func.frame)(machine);
}


/*-------------------------------------------------
    mame_is_valid_machine - return true if the
    given machine is valid
-------------------------------------------------*/

int mame_is_valid_machine(running_machine *machine)
{
	return (machine != NULL && machine == global_machine);
}



/***************************************************************************
    GLOBAL SYSTEM STATES
***************************************************************************/

/*-------------------------------------------------
    mame_schedule_exit - schedule a clean exit
-------------------------------------------------*/

void mame_schedule_exit(running_machine *machine)
{
	mame_private *mame = machine->mame_data;

	/* if we are in-game but we started with the select game menu, return to that instead */
	if (started_empty && options_get_string(mame_options(), OPTION_GAMENAME)[0] != 0)
	{
		options_set_string(mame_options(), OPTION_GAMENAME, "", OPTION_PRIORITY_CMDLINE);
		ui_menu_force_game_select(machine, render_container_get_ui());
	}

	/* otherwise, exit for real */
	else
		mame->exit_pending = TRUE;

	/* if we're executing, abort out immediately */
	eat_all_cpu_cycles(machine);

	/* if we're autosaving on exit, schedule a save as well */
	if (options_get_bool(mame_options(), OPTION_AUTOSAVE) && (machine->gamedrv->flags & GAME_SUPPORTS_SAVE))
		mame_schedule_save(machine, "auto");
}


/*-------------------------------------------------
    mame_schedule_hard_reset - schedule a hard-
    reset of the system
-------------------------------------------------*/

void mame_schedule_hard_reset(running_machine *machine)
{
	mame_private *mame = machine->mame_data;
	mame->hard_reset_pending = TRUE;

	/* if we're executing, abort out immediately */
	eat_all_cpu_cycles(machine);
}


/*-------------------------------------------------
    mame_schedule_soft_reset - schedule a soft-
    reset of the system
-------------------------------------------------*/

void mame_schedule_soft_reset(running_machine *machine)
{
	mame_private *mame = machine->mame_data;

	timer_adjust_oneshot(mame->soft_reset_timer, attotime_zero, 0);

	/* we can't be paused since the timer needs to fire */
	mame_pause(machine, FALSE);

	/* if we're executing, abort out immediately */
	eat_all_cpu_cycles(machine);
}


/*-------------------------------------------------
    mame_schedule_new_driver - schedule a new game
    to be loaded
-------------------------------------------------*/

void mame_schedule_new_driver(running_machine *machine, const game_driver *driver)
{
	mame_private *mame = machine->mame_data;
	mame->hard_reset_pending = TRUE;
	mame->new_driver_pending = driver;

	/* if we're executing, abort out immediately */
	eat_all_cpu_cycles(machine);
}


/*-------------------------------------------------
    set_saveload_filename - specifies the filename
    for state loading/saving
-------------------------------------------------*/

static void set_saveload_filename(running_machine *machine, const char *filename)
{
	mame_private *mame = machine->mame_data;

	/* free any existing request and allocate a copy of the requested name */
	if (osd_is_absolute_path(filename))
	{
		mame->saveload_searchpath = NULL;
		mame->saveload_pending_file.cpy(filename);
	}
	else
	{
		mame->saveload_searchpath = SEARCHPATH_STATE;
		mame->saveload_pending_file.cpy(machine->basename).cat(PATH_SEPARATOR).cat(filename).cat(".sta");
	}
}


/*-------------------------------------------------
    mame_schedule_save - schedule a save to
    occur as soon as possible
-------------------------------------------------*/

void mame_schedule_save(running_machine *machine, const char *filename)
{
	mame_private *mame = machine->mame_data;

	/* specify the filename to save or load */
	set_saveload_filename(machine, filename);

	/* note the start time and set a timer for the next timeslice to actually schedule it */
	mame->saveload_schedule_callback = handle_save;
	mame->saveload_schedule_time = timer_get_time(machine);

	/* we can't be paused since we need to clear out anonymous timers */
	mame_pause(machine, FALSE);
}


/*-------------------------------------------------
    mame_schedule_load - schedule a load to
    occur as soon as possible
-------------------------------------------------*/

void mame_schedule_load(running_machine *machine, const char *filename)
{
	mame_private *mame = machine->mame_data;

	/* specify the filename to save or load */
	set_saveload_filename(machine, filename);

	/* note the start time and set a timer for the next timeslice to actually schedule it */
	mame->saveload_schedule_callback = handle_load;
	mame->saveload_schedule_time = timer_get_time(machine);

	/* we can't be paused since we need to clear out anonymous timers */
	mame_pause(machine, FALSE);
}


/*-------------------------------------------------
    mame_is_save_or_load_pending - is a save or
    load pending?
-------------------------------------------------*/

int mame_is_save_or_load_pending(running_machine *machine)
{
	/* we can't check for saveload_pending_file here because it will bypass */
	/* required UI screens if a state is queued from the command line */
	mame_private *mame = machine->mame_data;
	return (mame->saveload_pending_file.len() != 0);
}


/*-------------------------------------------------
    mame_is_scheduled_event_pending - is a
    scheduled event pending?
-------------------------------------------------*/

int mame_is_scheduled_event_pending(running_machine *machine)
{
	/* we can't check for saveload_pending_file here because it will bypass */
	/* required UI screens if a state is queued from the command line */
	mame_private *mame = machine->mame_data;
	return mame->exit_pending || mame->hard_reset_pending;
}


/*-------------------------------------------------
    mame_pause - pause or resume the system
-------------------------------------------------*/

void mame_pause(running_machine *machine, int pause)
{
	mame_private *mame = machine->mame_data;
	callback_item *cb;

	/* ignore if nothing has changed */
	if (mame->paused == pause)
		return;
	mame->paused = pause;

	/* call all registered pause callbacks */
	for (cb = mame->pause_callback_list; cb; cb = cb->next)
		(*cb->func.pause)(machine, mame->paused);
}


/*-------------------------------------------------
    mame_is_paused - the system paused?
-------------------------------------------------*/

int mame_is_paused(running_machine *machine)
{
	mame_private *mame = machine->mame_data;
	return (mame->current_phase != MAME_PHASE_RUNNING) || mame->paused;
}



/***************************************************************************
    MEMORY REGIONS
***************************************************************************/

/*-------------------------------------------------
    region_info - constructor for a memory region
-------------------------------------------------*/

region_info::region_info(running_machine *_machine, const char *_name, UINT32 _length, UINT32 _flags)
	: machine(_machine),
	  next(NULL),
	  name(_name),
	  length(_length),
	  flags(_flags)
{
	base.u8 = auto_alloc_array(_machine, UINT8, _length);
}


/*-------------------------------------------------
    ~region_info - memory region destructor
-------------------------------------------------*/

region_info::~region_info()
{
	auto_free(machine, base.v);
}


/*-------------------------------------------------
    memory_region_alloc - allocates memory for a
    region
-------------------------------------------------*/

region_info *memory_region_alloc(running_machine *machine, const char *name, UINT32 length, UINT32 flags)
{
    /* make sure we don't have a region of the same name; also find the end of the list */
    region_info *info = machine->regionlist.find(name);
    if (info != NULL)
		fatalerror("memory_region_alloc called with duplicate region name \"%s\"\n", name);

	/* allocate the region */
	return machine->regionlist.append(name, global_alloc(region_info(machine, name, length, flags)));
}


/*-------------------------------------------------
    memory_region_free - releases memory for a
    region
-------------------------------------------------*/

void memory_region_free(running_machine *machine, const char *name)
{
	machine->regionlist.remove(name);
}


/*-------------------------------------------------
    memory_region - returns pointer to a memory
    region
-------------------------------------------------*/

UINT8 *memory_region(running_machine *machine, const char *name)
{
	const region_info *region = machine->region(name);
	return (region != NULL) ? region->base.u8 : NULL;
}


/*-------------------------------------------------
    memory_region_length - returns length of a
    memory region
-------------------------------------------------*/

UINT32 memory_region_length(running_machine *machine, const char *name)
{
	const region_info *region = machine->region(name);
	return (region != NULL) ? region->length : 0;
}


/*-------------------------------------------------
    memory_region_flags - returns flags for a
    memory region
-------------------------------------------------*/

UINT32 memory_region_flags(running_machine *machine, const char *name)
{
	const region_info *region = machine->region(name);
	return (region != NULL) ? region->flags : 0;
}


/*-------------------------------------------------
    memory_region_next - the name of the next
    memory region (or the first if name == NULL)
-------------------------------------------------*/

const char *memory_region_next(running_machine *machine, const char *name)
{
	if (name == NULL)
		return (machine->regionlist.first() != NULL) ? machine->regionlist.first()->name.cstr() : NULL;
	const region_info *region = machine->region(name);
	return (region != NULL && region->next != NULL) ? region->next->name.cstr() : NULL;
}



/***************************************************************************
    OUTPUT MANAGEMENT
***************************************************************************/

/*-------------------------------------------------
    mame_set_output_channel - configure an output
    channel
-------------------------------------------------*/

void mame_set_output_channel(output_channel channel, output_callback_func callback, void *param, output_callback_func *prevcb, void **prevparam)
{
	assert(channel < OUTPUT_CHANNEL_COUNT);
	assert(callback != NULL);

	/* return the originals if requested */
	if (prevcb != NULL)
		*prevcb = output_cb[channel];
	if (prevparam != NULL)
		*prevparam = output_cb_param[channel];

	/* set the new ones */
	output_cb[channel] = callback;
	output_cb_param[channel] = param;
}


/*-------------------------------------------------
    mame_file_output_callback - default callback
    for file output
-------------------------------------------------*/

void mame_file_output_callback(void *param, const char *format, va_list argptr)
{
	vfprintf((FILE *)param, format, argptr);
}


/*-------------------------------------------------
    mame_null_output_callback - default callback
    for no output
-------------------------------------------------*/

void mame_null_output_callback(void *param, const char *format, va_list argptr)
{
}


/*-------------------------------------------------
    mame_printf_error - output an error to the
    appropriate callback
-------------------------------------------------*/

void mame_printf_error(const char *format, ...)
{
	va_list argptr;

	/* by default, we go to stderr */
	if (output_cb[OUTPUT_CHANNEL_ERROR] == NULL)
	{
		output_cb[OUTPUT_CHANNEL_ERROR] = mame_file_output_callback;
		output_cb_param[OUTPUT_CHANNEL_ERROR] = stderr;
	}

	/* do the output */
	va_start(argptr, format);
	(*output_cb[OUTPUT_CHANNEL_ERROR])(output_cb_param[OUTPUT_CHANNEL_ERROR], format, argptr);
	va_end(argptr);
}


/*-------------------------------------------------
    mame_printf_warning - output a warning to the
    appropriate callback
-------------------------------------------------*/

void mame_printf_warning(const char *format, ...)
{
	va_list argptr;

	/* by default, we go to stderr */
	if (output_cb[OUTPUT_CHANNEL_WARNING] == NULL)
	{
		output_cb[OUTPUT_CHANNEL_WARNING] = mame_file_output_callback;
		output_cb_param[OUTPUT_CHANNEL_WARNING] = stderr;
	}

	/* do the output */
	va_start(argptr, format);
	(*output_cb[OUTPUT_CHANNEL_WARNING])(output_cb_param[OUTPUT_CHANNEL_WARNING], format, argptr);
	va_end(argptr);
}


/*-------------------------------------------------
    mame_printf_info - output info text to the
    appropriate callback
-------------------------------------------------*/

void mame_printf_info(const char *format, ...)
{
	va_list argptr;

	/* by default, we go to stdout */
	if (output_cb[OUTPUT_CHANNEL_INFO] == NULL)
	{
		output_cb[OUTPUT_CHANNEL_INFO] = mame_file_output_callback;
		output_cb_param[OUTPUT_CHANNEL_INFO] = stdout;
	}

	/* do the output */
	va_start(argptr, format);
	(*output_cb[OUTPUT_CHANNEL_INFO])(output_cb_param[OUTPUT_CHANNEL_INFO], format, argptr);
	va_end(argptr);
}


/*-------------------------------------------------
    mame_printf_verbose - output verbose text to
    the appropriate callback
-------------------------------------------------*/

void mame_printf_verbose(const char *format, ...)
{
	va_list argptr;

	/* if we're not verbose, skip it */
	if (mame_opts == NULL || !options_get_bool(mame_options(), OPTION_VERBOSE))
		return;

	/* by default, we go to stdout */
	if (output_cb[OUTPUT_CHANNEL_VERBOSE] == NULL)
	{
		output_cb[OUTPUT_CHANNEL_VERBOSE] = mame_file_output_callback;
		output_cb_param[OUTPUT_CHANNEL_VERBOSE] = stdout;
	}

	/* do the output */
	va_start(argptr, format);
	(*output_cb[OUTPUT_CHANNEL_VERBOSE])(output_cb_param[OUTPUT_CHANNEL_VERBOSE], format, argptr);
	va_end(argptr);
}


/*-------------------------------------------------
    mame_printf_debug - output debug text to the
    appropriate callback
-------------------------------------------------*/

void mame_printf_debug(const char *format, ...)
{
	va_list argptr;

	/* by default, we go to stderr */
	if (output_cb[OUTPUT_CHANNEL_DEBUG] == NULL)
	{
#ifdef MAME_DEBUG
		output_cb[OUTPUT_CHANNEL_DEBUG] = mame_file_output_callback;
		output_cb_param[OUTPUT_CHANNEL_DEBUG] = stdout;
#else
		output_cb[OUTPUT_CHANNEL_DEBUG] = mame_null_output_callback;
		output_cb_param[OUTPUT_CHANNEL_DEBUG] = NULL;
#endif
	}

	/* do the output */
	va_start(argptr, format);
	(*output_cb[OUTPUT_CHANNEL_DEBUG])(output_cb_param[OUTPUT_CHANNEL_DEBUG], format, argptr);
	va_end(argptr);
}


/*-------------------------------------------------
    mame_printf_log - output log text to the
    appropriate callback
-------------------------------------------------*/

#ifdef UNUSED_FUNCTION
void mame_printf_log(const char *format, ...)
{
	va_list argptr;

	/* by default, we go to stderr */
	if (output_cb[OUTPUT_CHANNEL_LOG] == NULL)
	{
		output_cb[OUTPUT_CHANNEL_LOG] = mame_file_output_callback;
		output_cb_param[OUTPUT_CHANNEL_LOG] = stderr;
	}

	/* do the output */
	va_start(argptr, format);
	(*output_cb[OUTPUT_CHANNEL_LOG])(output_cb_param[OUTPUT_CHANNEL_LOG], format, argptr);
	va_end(argptr);
}
#endif



/***************************************************************************
    MISCELLANEOUS
***************************************************************************/

/*-------------------------------------------------
    popmessage - pop up a user-visible message
-------------------------------------------------*/

void CLIB_DECL popmessage(const char *format, ...)
{
	/* if the format is NULL, it is a signal to clear the popmessage */
	if (format == NULL)
		ui_popup_time(0, " ");

	/* otherwise, generate the buffer and call the UI to display the message */
	else
	{
		va_list arg;

		/* dump to the buffer */
		va_start(arg, format);
		vsnprintf(giant_string_buffer, ARRAY_LENGTH(giant_string_buffer), format, arg);
		va_end(arg);

		/* pop it in the UI */
		ui_popup_time((int)strlen(giant_string_buffer) / 40 + 2, "%s", giant_string_buffer);
	}
}


/*-------------------------------------------------
    logerror - log to the debugger and any other
    OSD-defined output streams
-------------------------------------------------*/

void CLIB_DECL logerror(const char *format, ...)
{
	running_machine *machine = global_machine;

	/* currently, we need a machine to do this */
	if (machine != NULL)
	{
		mame_private *mame = machine->mame_data;
		callback_item *cb;

		/* process only if there is a target */
		if (mame->logerror_callback_list != NULL)
		{
			va_list arg;

			profiler_mark_start(PROFILER_LOGERROR);

			/* dump to the buffer */
			va_start(arg, format);
			vsnprintf(giant_string_buffer, ARRAY_LENGTH(giant_string_buffer), format, arg);
			va_end(arg);

			/* log to all callbacks */
			for (cb = mame->logerror_callback_list; cb; cb = cb->next)
				(*cb->func.log)(machine, giant_string_buffer);

			profiler_mark_end();
		}
	}
}


/*-------------------------------------------------
    add_logerror_callback - adds a callback to be
    called on logerror()
-------------------------------------------------*/

void add_logerror_callback(running_machine *machine, void (*callback)(running_machine *, const char *))
{
	mame_private *mame = machine->mame_data;
	callback_item *cb, **cur;

	assert_always(mame_get_phase(machine) == MAME_PHASE_INIT, "Can only call add_logerror_callback at init time!");

	cb = auto_alloc(machine, callback_item);
	cb->func.log = callback;
	cb->next = NULL;

	for (cur = &mame->logerror_callback_list; *cur; cur = &(*cur)->next) ;
	*cur = cb;
}


/*-------------------------------------------------
    logfile_callback - callback for logging to
    logfile
-------------------------------------------------*/

static void logfile_callback(running_machine *machine, const char *buffer)
{
	mame_private *mame = machine->mame_data;
	if (mame->logfile != NULL)
		mame_fputs(mame->logfile, buffer);
}


/*-------------------------------------------------
    mame_rand - standardized random numbers
-------------------------------------------------*/

UINT32 mame_rand(running_machine *machine)
{
	mame_private *mame = machine->mame_data;
	mame->rand_seed = 1664525 * mame->rand_seed + 1013904223;

	/* return rotated by 16 bits; the low bits have a short period
       and are frequently used */
	return (mame->rand_seed >> 16) | (mame->rand_seed << 16);
}



/***************************************************************************
    INTERNAL INITIALIZATION LOGIC
***************************************************************************/

/*-------------------------------------------------
    mame_parse_ini_files - parse the relevant INI
    files and apply their options
-------------------------------------------------*/

void mame_parse_ini_files(core_options *options, const game_driver *driver)
{
	/* parse the INI file defined by the platform (e.g., "mame.ini") */
	/* we do this twice so that the first file can change the INI path */
	parse_ini_file(options, CONFIGNAME, OPTION_PRIORITY_MAME_INI);
	parse_ini_file(options, CONFIGNAME, OPTION_PRIORITY_MAME_INI);

	/* debug mode: parse "debug.ini" as well */
	if (options_get_bool(options, OPTION_DEBUG))
		parse_ini_file(options, "debug", OPTION_PRIORITY_DEBUG_INI);

	/* if we have a valid game driver, parse game-specific INI files */
	if (driver != NULL)
	{
#ifndef MESS
		const game_driver *parent = driver_get_clone(driver);
		const game_driver *gparent = (parent != NULL) ? driver_get_clone(parent) : NULL;
		const device_config *device;
		machine_config *config;

		/* parse "vertical.ini" or "horizont.ini" */
		if (driver->flags & ORIENTATION_SWAP_XY)
			parse_ini_file(options, "vertical", OPTION_PRIORITY_ORIENTATION_INI);
		else
			parse_ini_file(options, "horizont", OPTION_PRIORITY_ORIENTATION_INI);

		/* parse "vector.ini" for vector games */
		config = machine_config_alloc(driver->machine_config);
		for (device = video_screen_first(config); device != NULL; device = video_screen_next(device))
		{
			const screen_config *scrconfig = (const screen_config *)device->inline_config;
			if (scrconfig->type == SCREEN_TYPE_VECTOR)
			{
				parse_ini_file(options, "vector", OPTION_PRIORITY_VECTOR_INI);
				break;
			}
		}
		machine_config_free(config);

		/* next parse "source/<sourcefile>.ini"; if that doesn't exist, try <sourcefile>.ini */
		astring sourcename;
		core_filename_extract_base(&sourcename, driver->source_file, TRUE)->ins(0, "source" PATH_SEPARATOR);
		if (!parse_ini_file(options, sourcename, OPTION_PRIORITY_SOURCE_INI))
		{
			core_filename_extract_base(&sourcename, driver->source_file, TRUE);
			parse_ini_file(options, sourcename, OPTION_PRIORITY_SOURCE_INI);
		}

		/* then parent the grandparent, parent, and game-specific INIs */
		if (gparent != NULL)
			parse_ini_file(options, gparent->name, OPTION_PRIORITY_GPARENT_INI);
		if (parent != NULL)
			parse_ini_file(options, parent->name, OPTION_PRIORITY_PARENT_INI);
#endif	/* MESS */
		parse_ini_file(options, driver->name, OPTION_PRIORITY_DRIVER_INI);
	}
}


/*-------------------------------------------------
    parse_ini_file - parse a single INI file
-------------------------------------------------*/

static int parse_ini_file(core_options *options, const char *name, int priority)
{
	file_error filerr;
	mame_file *file;

	/* don't parse if it has been disabled */
	if (!options_get_bool(options, OPTION_READCONFIG))
		return FALSE;

	/* open the file; if we fail, that's ok */
	astring fname(name, ".ini");
	filerr = mame_fopen_options(options, SEARCHPATH_INI, fname, OPEN_FLAG_READ, &file);
	if (filerr != FILERR_NONE)
		return FALSE;

	/* parse the file and close it */
	mame_printf_verbose("Parsing %s.ini\n", name);
	options_parse_ini_file(options, mame_core_file(file), priority);
	mame_fclose(file);
	return TRUE;
}


/*-------------------------------------------------
    running_machine - create the running machine
    object and initialize it based on options
-------------------------------------------------*/

running_machine::running_machine(const game_driver *driver)
	: config(NULL),
	  firstcpu(NULL),
	  gamedrv(driver),
	  basename(NULL),
	  primary_screen(NULL),
	  palette(NULL),
	  pens(NULL),
	  colortable(NULL),
	  shadow_table(NULL),
	  priority_bitmap(NULL),
	  sample_rate(0),
	  debug_flags(0),
	  mame_data(NULL),
	  cpuexec_data(NULL),
	  timer_data(NULL),
	  state_data(NULL),
	  memory_data(NULL),
	  palette_data(NULL),
	  tilemap_data(NULL),
	  streams_data(NULL),
	  devices_data(NULL),
	  romload_data(NULL),
	  sound_data(NULL),
	  input_data(NULL),
	  input_port_data(NULL),
	  ui_input_data(NULL),
	  cheat_data(NULL),
	  debugcpu_data(NULL),
	  debugvw_data(NULL),
	  generic_machine_data(NULL),
	  generic_video_data(NULL),
	  generic_audio_data(NULL),
#ifdef MESS
	  images_data(NULL),
#endif /* MESS */
	  driver_data(NULL)
{
	try
	{
		memset(gfx, 0, sizeof(gfx));
		memset(&generic, 0, sizeof(generic));

		/* allocate memory for the internal mame_data */
		mame_data = auto_alloc_clear(this, mame_private);

		/* initialize the driver-related variables in the machine */
		basename = mame_strdup(driver->name);
		config = machine_config_alloc(driver->machine_config);

		/* attach this machine to all the devices in the configuration */
		devicelist.import_config_list(config->devicelist, *this);

		/* allocate the driver data (after devices) */
		if (config->driver_data_alloc != NULL)
			driver_data = (*config->driver_data_alloc)(*this);

		/* find devices */
		firstcpu = cpu_first(this);
		primary_screen = video_screen_first(this);

		/* fetch core options */
		sample_rate = options_get_int(mame_options(), OPTION_SAMPLERATE);
		if (options_get_bool(mame_options(), OPTION_DEBUG))
			debug_flags = (DEBUG_FLAG_ENABLED | DEBUG_FLAG_CALL_HOOK) | (options_get_bool(mame_options(), OPTION_DEBUG_INTERNAL) ? 0 : DEBUG_FLAG_OSD_ENABLED);
		else
			debug_flags = 0;
	}
	catch (std::bad_alloc &)
	{
		if (driver_data != NULL)
			auto_free(this, driver_data);
		if (config != NULL)
			machine_config_free((machine_config *)config);
		if (basename != NULL)
			osd_free(basename);
		if (mame_data != NULL)
			auto_free(this, mame_data);
	}
}


/*-------------------------------------------------
    ~running_machine - free the machine data
-------------------------------------------------*/

running_machine::~running_machine()
{
	assert(this == global_machine);

	if (config != NULL)
		machine_config_free((machine_config *)config);
	if (basename != NULL)
		osd_free(basename);

	global_machine = NULL;
}


/*-------------------------------------------------
    init_machine - initialize the emulated machine
-------------------------------------------------*/

static void init_machine(running_machine *machine)
{
	mame_private *mame = machine->mame_data;
	time_t newbase;

	/* initialize basic can't-fail systems here */
	fileio_init(machine);
	config_init(machine);
	input_init(machine);
	output_init(machine);
	state_init(machine);
	state_save_allow_registration(machine, TRUE);
	palette_init(machine);
	render_init(machine);
	ui_init(machine);
	generic_machine_init(machine);
	generic_video_init(machine);
	generic_sound_init(machine);
	mame->rand_seed = 0x9d14abd7;

	/* initialize the timers and allocate a soft_reset timer */
	/* this must be done before cpu_init so that CPU's can allocate timers */
	timer_init(machine);
	mame->soft_reset_timer = timer_alloc(machine, soft_reset, NULL);

	/* init the osd layer */
	//osd_init(machine);	//MOD RS

	/* initialize the base time (needed for doing record/playback) */
	time(&mame->base_time);

	/* initialize the input system and input ports for the game */
	/* this must be done before memory_init in order to allow specifying */
	/* callbacks based on input port tags */
	newbase = input_port_init(machine, machine->gamedrv->ipt);
	if (newbase != 0)
		mame->base_time = newbase;

	/* intialize UI input */
	ui_input_init(machine);

	/* initialize the streams engine before the sound devices start */
	streams_init(machine);

	/* first load ROMs, then populate memory, and finally initialize CPUs */
	/* these operations must proceed in this order */
	rom_init(machine);
	memory_init(machine);
	cpuexec_init(machine);
	watchdog_init(machine);

	/* allocate the gfx elements prior to device initialization */
	gfx_init(machine);

	/* initialize natural keyboard support */
	inputx_init(machine);

#ifdef MESS
	/* first MESS initialization */
	mess_predevice_init(machine);
#endif /* MESS */

	/* start up the devices */
	machine->devicelist.start_all();

	/* call the game driver's init function */
	/* this is where decryption is done and memory maps are altered */
	/* so this location in the init order is important */
	//ui_set_startup_text(machine, "Initializing...", TRUE);	//MOD RS
	if (machine->gamedrv->driver_init != NULL)
		(*machine->gamedrv->driver_init)(machine);

#ifdef MESS
	/* second MESS initialization */
	mess_postdevice_init(machine);
#endif /* MESS */

	/* start the video and audio hardware */
	video_init(machine);
	tilemap_init(machine);
	crosshair_init(machine);

	sound_init(machine);

	/* initialize the debugger */
	if ((machine->debug_flags & DEBUG_FLAG_ENABLED) != 0)
		debugger_init(machine);

	/* call the driver's _START callbacks */
	if (machine->config->machine_start != NULL)
		(*machine->config->machine_start)(machine);
	if (machine->config->sound_start != NULL)
		(*machine->config->sound_start)(machine);
	if (machine->config->video_start != NULL)
		(*machine->config->video_start)(machine);

	/* initialize miscellaneous systems */
	saveload_init(machine);
	if (options_get_bool(mame_options(), OPTION_CHEAT))
		cheat_init(machine);

	/* disallow save state registrations starting here */
	state_save_allow_registration(machine, FALSE);
}


/*-------------------------------------------------
    soft_reset - actually perform a soft-reset
    of the system
-------------------------------------------------*/

static TIMER_CALLBACK( soft_reset )
{
	mame_private *mame = machine->mame_data;
	callback_item *cb;

	logerror("Soft reset\n");

	/* temporarily in the reset phase */
	mame->current_phase = MAME_PHASE_RESET;

	/* call all registered reset callbacks */
	for (cb = machine->mame_data->reset_callback_list; cb; cb = cb->next)
		(*cb->func.reset)(machine);

	/* run the driver's reset callbacks */
	if (machine->config->machine_reset != NULL)
		(*machine->config->machine_reset)(machine);
	if (machine->config->sound_reset != NULL)
		(*machine->config->sound_reset)(machine);
	if (machine->config->video_reset != NULL)
		(*machine->config->video_reset)(machine);

	/* now we're running */
	mame->current_phase = MAME_PHASE_RUNNING;

	/* allow 0-time queued callbacks to run before any CPUs execute */
	timer_execute_timers(machine);
}



/***************************************************************************
    SAVE/RESTORE
***************************************************************************/

/*-------------------------------------------------
    saveload_init - initialize the save/load logic
-------------------------------------------------*/

static void saveload_init(running_machine *machine)
{
	const char *savegame = options_get_string(mame_options(), OPTION_STATE);

	/* if we're coming in with a savegame request, process it now */
	if (savegame[0] != 0)
		mame_schedule_load(machine, savegame);

	/* if we're in autosave mode, schedule a load */
	else if (options_get_bool(mame_options(), OPTION_AUTOSAVE) && (machine->gamedrv->flags & GAME_SUPPORTS_SAVE))
		mame_schedule_load(machine, "auto");
}


/*-------------------------------------------------
    handle_save - attempt to perform a save
-------------------------------------------------*/

static void handle_save(running_machine *machine)
{
	mame_private *mame = machine->mame_data;
	file_error filerr;
	mame_file *file;

	/* if no name, bail */
	if (mame->saveload_pending_file.len() == 0)
	{
		mame->saveload_schedule_callback = NULL;
		return;
	}

	/* if there are anonymous timers, we can't save just yet */
	if (timer_count_anonymous(machine) > 0)
	{
		/* if more than a second has passed, we're probably screwed */
		if (attotime_sub(timer_get_time(machine), mame->saveload_schedule_time).seconds > 0)
		{
			popmessage("Unable to save due to pending anonymous timers. See error.log for details.");
			goto cancel;
		}
		return;
	}

	/* open the file */
	filerr = mame_fopen(mame->saveload_searchpath, mame->saveload_pending_file, OPEN_FLAG_WRITE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_PATHS, &file);
	if (filerr == FILERR_NONE)
	{
		astring fullname(mame_file_full_name(file));
		state_save_error staterr;

		/* write the save state */
		staterr = state_save_write_file(machine, file);

		/* handle the result */
		switch (staterr)
		{
			case STATERR_ILLEGAL_REGISTRATIONS:
				popmessage("Error: Unable to save state due to illegal registrations. See error.log for details.");
				break;

			case STATERR_WRITE_ERROR:
				popmessage("Error: Unable to save state due to a write error. Verify there is enough disk space.");
				break;

			case STATERR_NONE:
				if (!(machine->gamedrv->flags & GAME_SUPPORTS_SAVE))
					popmessage("State successfully saved.\nWarning: Save states are not officially supported for this game.");
				else
					popmessage("State successfully saved.");
				break;

			default:
				popmessage("Error: Unknwon error during state save.");
				break;
		}

		/* close and perhaps delete the file */
		mame_fclose(file);
		if (staterr != STATERR_NONE)
			osd_rmfile(fullname);
	}
	else
		popmessage("Error: Failed to create save state file.");

	/* unschedule the save */
cancel:
	mame->saveload_pending_file.reset();
	mame->saveload_searchpath = NULL;
	mame->saveload_schedule_callback = NULL;
}


/*-------------------------------------------------
    handle_load - attempt to perform a load
-------------------------------------------------*/

static void handle_load(running_machine *machine)
{
	mame_private *mame = machine->mame_data;
	file_error filerr;
	mame_file *file;

	/* if no name, bail */
	if (mame->saveload_pending_file.len() == 0)
	{
		mame->saveload_schedule_callback = NULL;
		return;
	}

	/* if there are anonymous timers, we can't load just yet because the timers might */
	/* overwrite data we have loaded */
	if (timer_count_anonymous(machine) > 0)
	{
		/* if more than a second has passed, we're probably screwed */
		if (attotime_sub(timer_get_time(machine), mame->saveload_schedule_time).seconds > 0)
		{
			popmessage("Unable to load due to pending anonymous timers. See error.log for details.");
			goto cancel;
		}
		return;
	}

	/* open the file */
	filerr = mame_fopen(mame->saveload_searchpath, mame->saveload_pending_file, OPEN_FLAG_READ, &file);
	if (filerr == FILERR_NONE)
	{
		state_save_error staterr;

		/* write the save state */
		staterr = state_save_read_file(machine, file);

		/* handle the result */
		switch (staterr)
		{
			case STATERR_ILLEGAL_REGISTRATIONS:
				popmessage("Error: Unable to load state due to illegal registrations. See error.log for details.");
				break;

			case STATERR_INVALID_HEADER:
				popmessage("Error: Unable to load state due to an invalid header. Make sure the save state is correct for this game.");
				break;

			case STATERR_READ_ERROR:
				popmessage("Error: Unable to load state due to a read error (file is likely corrupt).");
				break;

			case STATERR_NONE:
				if (!(machine->gamedrv->flags & GAME_SUPPORTS_SAVE))
					popmessage("State successfully loaded.\nWarning: Save states are not officially supported for this game.");
				else
					popmessage("State successfully loaded.");
				break;

			default:
				popmessage("Error: Unknwon error during state load.");
				break;
		}

		/* close the file */
		mame_fclose(file);
	}
	else
		popmessage("Error: Failed to open save state file.");

	/* unschedule the load */
cancel:
	mame->saveload_pending_file.reset();
	mame->saveload_schedule_callback = NULL;
}



/***************************************************************************
    SYSTEM TIME
***************************************************************************/

/*-------------------------------------------------
    get_tm_time - converts a MAME
-------------------------------------------------*/

static void get_tm_time(struct tm *t, mame_system_tm *systm)
{
	systm->second	= t->tm_sec;
	systm->minute	= t->tm_min;
	systm->hour		= t->tm_hour;
	systm->mday		= t->tm_mday;
	systm->month	= t->tm_mon;
	systm->year		= t->tm_year + 1900;
	systm->weekday	= t->tm_wday;
	systm->day		= t->tm_yday;
	systm->is_dst	= t->tm_isdst;
}


/*-------------------------------------------------
    fill_systime - fills out a mame_system_time
    structure
-------------------------------------------------*/

static void fill_systime(mame_system_time *systime, time_t t)
{
	systime->time = t;
	get_tm_time(localtime(&t), &systime->local_time);
	get_tm_time(gmtime(&t), &systime->utc_time);
}


/*-------------------------------------------------
    mame_get_base_datetime - retrieve the time of
    the host system; useful for RTC implementations
-------------------------------------------------*/

void mame_get_base_datetime(running_machine *machine, mame_system_time *systime)
{
	mame_private *mame = machine->mame_data;
	fill_systime(systime, mame->base_time);
}


/*-------------------------------------------------
    mame_get_current_datetime - retrieve the current
    time (offsetted by the baes); useful for RTC
    implementations
-------------------------------------------------*/

void mame_get_current_datetime(running_machine *machine, mame_system_time *systime)
{
	mame_private *mame = machine->mame_data;
	fill_systime(systime, mame->base_time + timer_get_time(machine).seconds);
}
