/******************************************************************************
 Mephisto 4 + 5 Chess Computer
 2007 Dirk V.

******************************************************************************/

/*


CPU 65C02 P4
Clock 4.9152 MHz
NMI CLK 600 Hz
IRQ Line is set to VSS
8 KByte  SRAM Sony CXK5864-15l

1-CD74HC4060E   14 Bit Counter
1-CD74HC166E
1-CD74HC251E
1-SN74HC138N TI
1-SN74HC139N TI
1-74HC14AP Toshiba
1-74HC02AP Toshiba
1-74HC00AP Toshiba
1-CD74HC259E


$0000-$1fff   S-RAM
$2000 LCD 4 Byte Shift Register writeonly right to left
every 2nd char xor'd by $FF

2c00-2c07 Keyboard (8to1 Multiplexer) 74HCT251
2*8 Matrix
Adr. 0x3407
==0 !=0
2c00 CL E5
2c01 POS F6
2c02 MEMO G7
2c03 INFO A1
2c04 LEV H8
2c05 ENT B2
2c06 >0 C3
2c07 <9 D4

$3400-$3407 LED 1-6, Buzzer, Keyboard select

$2400 // Chess Board
$2800 // Chess Board
$3000 // Chess Board

$4000-7FFF Opening Modul HG550
$8000-$FFF ROM



*/
#include <ctype.h>

#include "driver.h"
#include "cpu/m6502/m6502.h"
// #include "sound/dac.h"
#include "sound/beep.h"

#include "mephisto.lh"

#include "render.h"
#include "rendlay.h"
#include "uiinput.h"

#include "mephistoboard_def.h"


static int loadFENfile(char *buffer);
static void setboardfromFEN(char * FEN, UINT8* hboard);
static void put_board_to_memory_mm(UINT8* board1[64], UINT8* board2[64], UINT8* hboard);

#define MM4_WR	4
#define MM4_WN	2
#define MM4_WB	3
#define MM4_WQ	5
#define MM4_WK	6
#define MM4_WP	1

#define MM4_BR	10
#define MM4_BN	8
#define MM4_BB	9
#define MM4_BQ	11
#define MM4_BK	12
#define MM4_BP	7

static UINT8 led_status;
static UINT8 *mephisto_ram;
static UINT8 led7;

static UINT8 *p_mm4_board[64];
static UINT8 *p_mm4_board2[64];
static UINT8 *p_mm4_bw;

static UINT8 board_8[64];		//for reading board from memory or FEN


static UINT8 mm4_to_myboard[80] = 
{

	0xff,    0,    8,   16,   24,   32,   40,   48,   56, 0xff,
	0xff,    1,    9,   17,   25,   33,   41,   49,   57, 0xff,
	0xff,    2,   10,   18,   26,   34,   42,   50,   58, 0xff,
	0xff,    3,   11,   19,   27,   35,   43,   51,   59, 0xff,
	0xff,    4,   12,   20,   28,   36,   44,   52,   60, 0xff,
	0xff,    5,   13,   21,   29,   37,   45,   53,   61, 0xff,
	0xff,    6,   14,   22,   30,   38,   46,   54,   62, 0xff,
	0xff,    7,   15,   23,   31,   39,   47,   55,   63, 0xff,

};


static DRIVER_INIT( mephisto )
{
	int i=0;
	lcd_shift_counter = 3;

// initialize graphical chess board
//
	set_startboard_from_startpos();

// initialize array of pointer to chess board in mephisto memory
//
	for (i=0; i<80;i++)
	{
		if ( mm4_to_myboard[i] != 0xff )
		{
			p_mm4_board[mm4_to_myboard[i]]  = (mephisto_ram+0x415+i);   //current pos
			p_mm4_board2[mm4_to_myboard[i]] = (mephisto_ram+0x480+i);	//starting pos
		}
	}

// flag back/white ???
//
			p_mm4_bw = mephisto_ram+0x3f;
}

static WRITE8_HANDLER ( write_lcd )
{
	if (led7==0)output_set_digit_value(lcd_shift_counter,data);    // 0x109 MM IV // 0x040 MM V

	//output_set_digit_value(lcd_shift_counter,data ^ mephisto_ram[0x165]);    // 0x109 MM IV // 0x040 MM V
	lcd_shift_counter--;
	lcd_shift_counter&=3;
}

static READ8_HANDLER(read_keys)
{
    char fen_string[2000];
    UINT8 load_fen;
	static int load_fen_flag;

	UINT8 data;
	static const char *const keynames[2][8] =
			{
				{ "KEY1_0", "KEY1_1", "KEY1_2", "KEY1_3", "KEY1_4", "KEY1_5", "KEY1_6", "KEY1_7" },
				{ "KEY2_0", "KEY2_1", "KEY2_2", "KEY2_3", "KEY2_4", "KEY2_5", "KEY2_6", "KEY2_7" }
			};

	data = 0xff;
	if (((led_status & 0x80) == 0x00))
		data=input_port_read(space->machine, keynames[0][offset]);
	else
		data=input_port_read(space->machine, keynames[1][offset]);

// Check if LOAD_FEN button (F12) is pressed
//
	load_fen=input_port_read(space->machine, "LOAD_FEN");

	if (!load_fen)
	{
		if ( !load_fen_flag)
		{
			load_fen_flag=TRUE;  
 			if (loadFENfile(fen_string))
			{
				setboardfromFEN(fen_string, board_8);						//read FEN string in array
				put_board_to_memory_mm(p_mm4_board, p_mm4_board2, board_8); //change internal board

				if (artwork_view==BOARD_VIEW)
				{
					clear_layout();										//clear artwork layout
					set_startboard_from_array(board_8);					//startposition for layout
					set_render_board();									//change layout
					set_status_of_pieces();								//set or not set pieces
				}
			}
		}

	}else
	{
		load_fen_flag=FALSE; 
	}


//	logerror("Keyboard Port = %s Data = %d\n  ", ((led_status & 0x80) == 0x00) ? keynames[0][offset] : keynames[1][offset], data);
	return data | 0x7f;
}

// Board LED's 
//
static WRITE8_HANDLER(write_board_mm)
{

// Set, clear board LED's 
//
  if (artwork_view==BOARD_VIEW)
	set_board(m_board,&Line18_LED,data);

}

// Get board mask	-> write_board_gg
//
static WRITE8_HANDLER(write_board_mask)
{

//logerror("write_board_mask data = %d\n",data);
	
Line18_REED=data;
Line18_LED=data;

}

static WRITE8_HANDLER(write_unknown)
{
	
}

// Get Reeds
//
static READ8_HANDLER(read_board)
{

  UINT16 data;

  if (artwork_view==BOARD_VIEW)
  {
	data=get_board(m_board,&Line18_REED);
	return data;
  }else
	return 0xff;	// Mephisto needs it for working
}

static WRITE8_HANDLER ( write_led )
{

	UINT8 LED_offset;
	data &= 0x80;

	if (artwork_view==BOARD_VIEW)
	{
		LED_offset=100;
	}else
	{
		LED_offset=0;
	}

	if (data==0)led_status &= 255-(1<<offset) ; else led_status|=1<<offset;
	if (offset<6)output_set_led_value(LED_offset+offset, led_status&1<<offset?1:0);
	if (offset==7) led7=data& 0x80 ? 0x00 :0xff;

	 
//	logerror("LEDs  Offset = %d Data = %d\n",offset,data);
}

static ADDRESS_MAP_START(rebel5_mem , ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE( 0x0000, 0x1fff) AM_RAM AM_BASE(&mephisto_ram)
	AM_RANGE( 0x5000, 0x5000) AM_WRITE( write_lcd )
	AM_RANGE( 0x3000, 0x3007) AM_READ( read_keys )	// Rebel 5.0
	AM_RANGE( 0x2000, 0x2007) AM_WRITE( write_led )	// Status LEDs+ buzzer

	AM_RANGE( 0x3000, 0x4000) AM_READ( read_board)			//Chessboard		reeds

    AM_RANGE( 0x6000, 0x6000) AM_WRITE ( write_board_mm)	//Chessboard		Set LED's 

    AM_RANGE( 0x7000, 0x7000) AM_WRITE ( write_board_mask ) //Chessboard	    mask

	AM_RANGE( 0x8000, 0xffff) AM_ROM
ADDRESS_MAP_END


static ADDRESS_MAP_START(mephisto_mem , ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE( 0x0000, 0x1fff) AM_RAM AM_BASE(&mephisto_ram )//
	AM_RANGE( 0x2000, 0x2000) AM_WRITE( write_lcd )
	AM_RANGE( 0x2c00, 0x2c07) AM_READ( read_keys )
	AM_RANGE( 0x3400, 0x3407) AM_WRITE( write_led )	// Status LEDs+ buzzer

	AM_RANGE( 0x2400, 0x2407) AM_WRITE ( write_board_mm )	// Chessboard		Set LED's  

	AM_RANGE( 0x2800, 0x2800) AM_WRITE ( write_board_mask)	// Chessboard	    mask

    AM_RANGE( 0x3800, 0x3800) AM_WRITE ( write_unknown)		// unknwon write access      Test

	AM_RANGE( 0x3000, 0x3000) AM_READ( read_board )			// Chessboard		 reeds	

	AM_RANGE( 0x4000, 0x7fff) AM_ROM	// Opening Library
	AM_RANGE( 0x8000, 0xffff) AM_ROM
ADDRESS_MAP_END



static INPUT_PORTS_START( mephisto )
	PORT_START("KEY1_0") //Port $2c00
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("CLEAR") PORT_CODE(KEYCODE_F1)
	PORT_START("KEY1_1") //Port $2c01
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("POS") PORT_CODE(KEYCODE_F2)
	PORT_START("KEY1_2") //Port $2c02
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("MEM") PORT_CODE(KEYCODE_F3)
	PORT_START("KEY1_3") //Port $2c03
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("INFO") PORT_CODE(KEYCODE_F4)
	PORT_START("KEY1_4") //Port $2c04
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("LEV") PORT_CODE(KEYCODE_F5)
	PORT_START("KEY1_5") //Port $2c05
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("ENT") PORT_CODE(KEYCODE_ENTER)
	PORT_START("KEY1_6") //Port $2c06
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("0") PORT_CODE(KEYCODE_0)
	PORT_START("KEY1_7") //Port $2c07
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("9") PORT_CODE(KEYCODE_9)

	PORT_START("KEY2_0") //Port $2c08
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("E 5") PORT_CODE(KEYCODE_E) PORT_CODE(KEYCODE_5)
	PORT_START("KEY2_1") //Port $2c09
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F 6") PORT_CODE(KEYCODE_F) PORT_CODE(KEYCODE_6)
	PORT_START("KEY2_2") //Port $2c0a
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("G 7") PORT_CODE(KEYCODE_G) PORT_CODE(KEYCODE_7)
	PORT_START("KEY2_3") //Port $2c0b
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("A 1") PORT_CODE(KEYCODE_A) PORT_CODE(KEYCODE_1)
	PORT_START("KEY2_4") //Port $2c0c
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("H 8") PORT_CODE(KEYCODE_H) PORT_CODE(KEYCODE_8)
	PORT_START("KEY2_5") //Port $2c0d
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("B 2") PORT_CODE(KEYCODE_B) PORT_CODE(KEYCODE_2)
	PORT_START("KEY2_6") //Port $2c0e
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("C 3") PORT_CODE(KEYCODE_C) PORT_CODE(KEYCODE_3)
	PORT_START("KEY2_7") //Port $2c0f
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("D 4") PORT_CODE(KEYCODE_D) PORT_CODE(KEYCODE_4)

 PORT_START("MOUSE_X")
    PORT_BIT( 0xffff, 0x00, IPT_MOUSE_X)  PORT_SENSITIVITY(100) PORT_KEYDELTA(1) PORT_MINMAX(0, 65535) 	PORT_PLAYER(1)

  PORT_START("MOUSE_Y")
    PORT_BIT( 0xffff, 0x00, IPT_MOUSE_Y ) PORT_SENSITIVITY(100) PORT_KEYDELTA(1) PORT_MINMAX(0, 65535) 	PORT_PLAYER(1)

  PORT_START("BUTTON_L")
    PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON2) PORT_CODE(MOUSECODE_BUTTON1) PORT_NAME("left button")

  PORT_START("BUTTON_R")
    PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1) PORT_CODE(MOUSECODE_BUTTON2) PORT_NAME("right button")

  PORT_START("LOAD_FEN")  
	PORT_BIT(0x080, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("fen") PORT_CODE(KEYCODE_F12)


INPUT_PORTS_END

static TIMER_CALLBACK( update_artwork )
{
	artwork_view=get_artwork_view();
	if (artwork_view==BOARD_VIEW)
		video_update(machine,0);
}

static TIMER_CALLBACK( update_nmi )
{
	const device_config *speaker = devtag_get_device(machine, "beep");
	cputag_set_input_line(machine, "maincpu", INPUT_LINE_NMI,PULSE_LINE);
	// dac_data_w(0,led_status&64?128:0);
	beep_set_state(speaker,led_status&64?1:0);
}

// Save state call backs
//
static STATE_PRESAVE( m_board_presave )
{
	set_array_from_current_board(save_board, m_board);	//save state
}

static STATE_POSTLOAD( m_board_postload )
{
	clear_layout();										//clear artwork layout
	set_startboard_from_array(save_board);				//startposition for layout
	set_render_board();									//change layout
	set_status_of_pieces();								//set or not set pieces
}


static MACHINE_START( mephisto )
{
	const device_config *speaker = devtag_get_device(machine, "beep");
	lcd_shift_counter=3;
	// timer_pulse(ATTOTIME_IN_HZ(60), NULL, 0, update_leds);
	timer_pulse(machine, ATTOTIME_IN_HZ(600), NULL, 0, update_nmi);
	timer_pulse(machine, ATTOTIME_IN_HZ(20), NULL, 0, update_artwork);		//4.9 MHZ
//	timer_pulse(machine, ATTOTIME_IN_HZ(75), NULL, 0, update_artwork);		//18 MHZ
	// cpunum_set_input_line(machine, 0, M65C02_IRQ_LINE,CLEAR_LINE);
	//beep_set_frequency(0, 4000);
	beep_set_frequency(speaker, 2800);

	state_save_register_global_array(machine,save_board);
	state_save_register_postload(machine,m_board_postload,NULL);
	state_save_register_presave(machine,m_board_presave,NULL);

}

static MACHINE_RESET( mephisto )
{
	lcd_shift_counter=3;

// initialize graphical chess board
//
     set_startboard_from_startpos();

	 clear_layout();
	 set_render_board();
	 set_status_of_pieces();

	 video_update(dummy_machine,1);
}

static MACHINE_DRIVER_START( mephisto )
	/* basic machine hardware */
	MDRV_CPU_ADD("maincpu",M65C02,4915200)        /* 65C02 */
//  MDRV_CPU_ADD("maincpu",M65C02,18000000)
	MDRV_CPU_PROGRAM_MAP(mephisto_mem)
	MDRV_QUANTUM_TIME(HZ(60))
	MDRV_MACHINE_START( mephisto )
	MDRV_MACHINE_RESET( mephisto )

	/* video hardware */

	MDRV_DEFAULT_LAYOUT(layout_mephisto)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")
	MDRV_SOUND_ADD("beep", BEEP, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.0)
MACHINE_DRIVER_END

static MACHINE_DRIVER_START( rebel5 )
	MDRV_IMPORT_FROM( mephisto )
	MDRV_CPU_MODIFY("maincpu")
	MDRV_CPU_CLOCK(4915200)
	MDRV_CPU_PROGRAM_MAP(rebel5_mem)
	//beep_set_frequency(0, 4000);
MACHINE_DRIVER_END


//static MACHINE_DRIVER_START( mm4_16 )
//
//	MDRV_IMPORT_FROM( mephisto )
//	MDRV_CPU_MODIFY("maincpu")
//	MDRV_CPU_CLOCK(16000000)
//
//	//device->clock=18000000;
//
//MACHINE_DRIVER_END


ROM_START(rebel5)
	ROM_REGION(0x10000,"maincpu",0)
	ROM_LOAD("rebel5.rom", 0x8000, 0x8000, CRC(8d02e1ef) SHA1(9972c75936613bd68cfd3fe62bd222e90e8b1083))
ROM_END

//ROM_START(mm4_16)
//	ROM_REGION(0x10000,"maincpu",0)
//	ROM_LOAD("mephisto4.rom", 0x8000, 0x8000, CRC(f68a4124) SHA1(d1d03a9aacc291d5cb720d2ee2a209eeba13a36c))
//	ROM_SYSTEM_BIOS( 0, "none", "No Opening Library" )
//	ROM_SYSTEM_BIOS( 1, "hg440", "HG440 Opening Library" )
//	ROMX_LOAD( "hg440.rom", 0x4000, 0x4000, CRC(81ffcdfd) SHA1(b0f7bcc11d1e821daf92cde31e3446c8be0bbe19), ROM_BIOS(2))
//ROM_END

ROM_START(mm4)
	ROM_REGION(0x10000,"maincpu",0)
	ROM_LOAD("mephisto4.rom", 0x8000, 0x8000, CRC(f68a4124) SHA1(d1d03a9aacc291d5cb720d2ee2a209eeba13a36c))
	ROM_SYSTEM_BIOS( 0, "none", "No Opening Library" )
	ROM_SYSTEM_BIOS( 1, "hg440", "HG440 Opening Library" )
	ROMX_LOAD( "hg440.rom", 0x4000, 0x4000, CRC(81ffcdfd) SHA1(b0f7bcc11d1e821daf92cde31e3446c8be0bbe19), ROM_BIOS(2))
ROM_END

ROM_START(mm5)
	ROM_REGION(0x10000,"maincpu",0)
	ROM_LOAD("mephisto5.rom", 0x8000, 0x8000, CRC(89c3d9d2) SHA1(77cd6f8eeb03c713249db140d2541e3264328048))
	ROM_SYSTEM_BIOS( 0, "none", "No Opening Library" )
	ROM_SYSTEM_BIOS( 1, "hg550", "HG550 Opening Library" )
	ROMX_LOAD("hg550.rom", 0x4000, 0x4000, CRC(0359f13d) SHA1(833cef8302ad8d283d3f95b1d325353c7e3b8614),ROM_BIOS(2))
ROM_END

ROM_START(mm50)
	ROM_REGION(0x10000,"maincpu",0)
	ROM_LOAD("mm50.rom", 0x8000, 0x8000, CRC(fcfa7e6e) SHA1(afeac3a8c957ba58cefaa27b11df974f6f2066da))
	ROM_SYSTEM_BIOS( 0, "none", "No Opening Library" )
	ROM_SYSTEM_BIOS( 1, "hg550", "HG550 Opening Library" )
	ROMX_LOAD("hg550.rom", 0x4000, 0x4000, CRC(0359f13d) SHA1(833cef8302ad8d283d3f95b1d325353c7e3b8614),ROM_BIOS(2))
ROM_END


/***************************************************************************

	Game driver(s)

***************************************************************************/

/*    YEAR  NAME        PARENT  COMPAT  MACHINE     INPUT       INIT        CONFIG  COMPANY             FULLNAME                            FLAGS */
////CONS( 1983, mephisto,   0,      0,      mephisto,   mephisto,   mephisto,   NULL,   "Hegener & Glaser", "Mephisto Schach Computer",         GAME_NOT_WORKING )
CONS( 1987, mm4,        0,      0,      mephisto,   mephisto,   mephisto,   NULL,   "Hegener & Glaser", "Mephisto 4 Schach Computer",				 GAME_SUPPORTS_SAVE )

//CONS( 1987, mm4_16,     0,      0,      mm4_16,     mephisto,   mephisto,   NULL,   "Hegener & Glaser", "Mephisto 4 (16 MHz) Schach Computer",	     GAME_SUPPORTS_SAVE )

CONS( 1990, mm5,        0,      0,      mephisto,   mephisto,   mephisto,   NULL,   "Hegener & Glaser", "Mephisto 5.1 Schach Computer",				 GAME_SUPPORTS_SAVE )
CONS( 1990, mm50,       0,      0,      mephisto,   mephisto,   mephisto,   NULL,   "Hegener & Glaser", "Mephisto 5.0 Schach Computer",				 GAME_SUPPORTS_SAVE )

//CONS( 1987, mm4,        0,      0,      mephisto,   mephisto,   mephisto,   NULL,   "Hegener & Glaser", "Mephisto 4 (18 MHZ) Schach Computer",       0 )
//CONS( 1990, mm5,        0,      0,      mephisto,   mephisto,   mephisto,   NULL,   "Hegener & Glaser", "Mephisto 5.1 (18 MHZ) Schach Computer",     0 )
//CONS( 1990, mm50,       0,      0,      mephisto,   mephisto,   mephisto,   NULL,   "Hegener & Glaser", "Mephisto 5.0 (18 MHZ) Schach Computer",     0 )

CONS( 1986, rebel5,     0,      0,      rebel5,     mephisto,   mephisto,   NULL,   "Hegener & Glaser", "Mephisto Rebel 5 Schach Computer", GAME_SUPPORTS_SAVE )
// second design sold (same computer/program?)


#include "mephistoboard_func.h"


static void setboardfromFEN(char * FEN, UINT8* hboard)
{
int empty_counter;
char noe[10];
char ep_field[3];

int exit_flag = FALSE;

int fifty=0;
int movecnt=0;
//int bw=0;

int fen_part=1;
int i;
int a=0;
int b=0;

int board_index=0;


do
{

	switch (fen_part)
	{
//-----------------------------------------------
// Part 1 = Positon of pieces
//-----------------------------------------------
	case 1:

		switch (FEN[a]) 
		{

			case 'r':
				hboard[board_index] = BR;
				board_index++;
				break;
			case 'n':
				hboard[board_index] = BN;
				board_index++;
				break;
			case 'b':
				hboard[board_index] = BB;
				board_index++;
				break;
			case 'q':
				hboard[board_index] = BQ;
				board_index++;
				break;
			case 'k':
				hboard[board_index] = BK;
				board_index++;
				break;
			case 'p':
				hboard[board_index] = BP;
				board_index++;
				break;

			case 'R':
				hboard[board_index] = WR;
				board_index++;
				break;
			case 'N':
				hboard[board_index] = WN;
				board_index++;
				break;
			case 'B':
				hboard[board_index] = WB;
				board_index++;
				break;
			case 'Q':
				hboard[board_index] = WQ;
				board_index++;
				break;
			case 'K':
				hboard[board_index] = WK;
				board_index++;
				break;
			case 'P':
				hboard[board_index] = WP;
				board_index++;
				break;

			case '/':
				break;

// empty squares
//
			default:
				empty_counter=1;
				if (isdigit (FEN[a]))        
				{
					empty_counter=atoi((const char *) &FEN[a]);
					for (b=0; b<empty_counter; b=b+1)
					{
						hboard[board_index] = EMPTY;
						board_index++;
					}
			
				} //end if
				break;

//if blank skip to next FEN part
//
			case ' ':
				fen_part=fen_part+1;
				break;

			}// end switch part 1
			break;

//-----------------------------------------------
// Part 2 = white or black
//-----------------------------------------------
		case 2:

			if (FEN[a]=='-')	//no information
			{
				break;
			}		

			if (FEN[a]==' ')	//next FEN part
			{
				fen_part=fen_part+1;
				break;
			}
			
			if (FEN[a]=='w')	//white
			{
				//*p_mm4_bw=1;
			}
			else				//black
			{
				//*p_mm4_bw=0;
			}

			break;

//-----------------------------------------------			
// Part 3 = castle statis
//-----------------------------------------------
		case 3:

			switch (FEN[a]) 
			{
				case 'K':

					break;
				case 'Q':

					break;
				case 'k':

					break;
				case 'q':

					break;

				case '-': //no information
					break;

				case ' ': //next FEN part
					fen_part=fen_part+1;
					break;
			}

			break;

//-----------------------------------------------
// Part 4 = EP field
//-----------------------------------------------
		case 4:

			if (FEN[a]=='-' )	//no information
			{
				break;
			}


			if (FEN[a]==' ')	//next FEN part
			{
				fen_part=fen_part+1;
				break;
			}

			ep_field[0] = FEN[a];
			a=a+1;
			ep_field[1] = FEN[a];
			ep_field[2]= 0; 

			break;

//------------------------------------------------
// Part 5 = moves since last capure or pawn move
//------------------------------------------------
		case 5:

			if (FEN[a]=='-')	// no information
			{
				break;
			}

			i=0;
			while (isdigit(FEN[a]))
			{
				noe[i]=FEN[a];
				a++;
				i++;
			}
			assert (i<10);
			noe[i]=0;

			if(i) fifty=atoi(noe);

			if (FEN[a]==' ')	//next FEN part
			{
				fen_part=fen_part+1;
				break;
			}
										 
			break;

//-----------------------------------------------
// Part 6 = Number of moves
//-----------------------------------------------
		case 6:

		exit_flag=TRUE;
		if (FEN[a]=='-')	//no information
			{
				break;
			}

			i=0;
			while (isdigit(FEN[a]))
			{
				noe[i]=FEN[a];
				a++;
				i++;
			}
			assert (i<10);
			noe[i]=0;

			if(i) movecnt=atoi(noe);

										 
		break;

	}//end switch fen_part

assert(board_index <= 64);
a++;
}while(!exit_flag); // end do


}

static int loadFENfile(char *buffer)
{
	FILE *op;
	int i=0;
	char c;

	op = fopen("fen.txt","r");
	if (op) 
	{
		do
		{
			c=(char)fgetc(op);
			buffer[i] = c;
			i=i+1;
			if (i>2000) 
			{
				popmessage("Too much characters in fen.txt"); 
				return FALSE;
			}
		}while( (!feof(op)) && c != '\n');
		fclose(op);
		buffer[i]=0;
		popmessage("FEN loaded"); 
        return TRUE;
	}else
	{
		popmessage("File fen.txt not found"); 
		return FALSE;
	}

}

static void put_board_to_memory_mm(UINT8* board1[64], UINT8* board2[64], UINT8* hboard)
{
int board_index;


for (board_index =0; board_index<64;board_index++)
{

	switch (hboard[board_index]) 
	{

		case WP:
			*board1[flip[board_index]] = MM4_WP;
			*board2[flip[board_index]] = MM4_WP;
			break;

		case WR:
			*board1[flip[board_index]] = MM4_WR;
			*board2[flip[board_index]] = MM4_WR;
			break;

		case WN:
			*board1[flip[board_index]] = MM4_WN;
			*board2[flip[board_index]] = MM4_WN;
			break;

		case WB:
			*board1[flip[board_index]] = MM4_WB;
			*board2[flip[board_index]] = MM4_WB;
			break;

		case WQ:
			*board1[flip[board_index]] = MM4_WQ;
			*board2[flip[board_index]] = MM4_WQ;
			break;

		case WK:
			*board1[flip[board_index]] = MM4_WK;
			*board2[flip[board_index]] = MM4_WK;
			break;


		case BP:
			*board1[flip[board_index]] = MM4_BP;
			*board2[flip[board_index]] = MM4_BP;
			break;

		case BR:
			*board1[flip[board_index]] = MM4_BR;
			*board2[flip[board_index]] = MM4_BR;
			break;

		case BN:
			*board1[flip[board_index]] = MM4_BN;
			*board2[flip[board_index]] = MM4_BN;
			break;

		case BB:
			*board1[flip[board_index]] = MM4_BB;
			*board2[flip[board_index]] = MM4_BB;
			break;

		case BQ:
			*board1[flip[board_index]] = MM4_BQ;
			*board2[flip[board_index]] = MM4_BQ;
			break;

		case BK:
			*board1[flip[board_index]] = MM4_BK;
			*board2[flip[board_index]] = MM4_BK;
			break;


		case EMPTY:
			*board1[flip[board_index]] = 0;
			*board2[flip[board_index]] = 0;
			break;
	}

}

}

