/***************************************************************************
Mephisto Glasgow 3 S chess computer
Dirk V.
sp_rinter@gmx.de

68000 CPU
64 KB ROM
16 KB RAM
4 Digit LC Display

3* 74LS138  Decoder/Multiplexer
1*74LS74    Dual positive edge triggered D Flip Flop
1*74LS139 1of4 Demultiplexer
1*74LS05    HexInverter
1*NE555     R=100K C=10uF
2*74LS04  Hex Inverter
1*74LS164   8 Bit Shift register
1*74121 Monostable Multivibrator with Schmitt Trigger Inputs
1*74LS20 Dual 4 Input NAND GAte
1*74LS367 3 State Hex Buffers


***************************************************************************/

#include "emu.h"
#include "cpu/m68000/m68000.h"
#include "glasgow.lh"
#include "sound/beep.h"

#include "render.h"
#include "rendlay.h"
#include "uiinput.h"

#include "mephistoboard_def.h"

#include "modrs.h"						//MOD RS

#define BM_REPEAT	2					//MOD RS Wiederholung Suche Bestmove

static char saveDisplay[10]="    ";		//MOD RS

static int sendBM=FALSE;				//MOD RS
static int sendBM_delay=0;				//MOD RS	//Z�hler Wartezeit Bis Besmove gesendet werden kann
static int sendBM_repeat=BM_REPEAT;		//MOD RS

static UINT8 key_low,
       key_hi,
       key_select,
       irq_flag,
       lcd_invert,
       key_selector;
static UINT8 board_value;
static UINT16 beeper;

static int irq_edge=0x00;

static void read_display (UINT8 lcd_data);		//MOD RS

// Used by Glasgow and Dallas
static WRITE16_HANDLER ( write_lcd_gg )
{
  UINT8 lcd_data;
  lcd_data = data>>8;
  if (led7==0)						//MOD RS
	  read_display(lcd_data);		//MOD RS

  lcd_shift_counter--;
  lcd_shift_counter&=3;
//  logerror("LCD Offset = %d Data low  = %x \n  ",offset,lcd_data);
}

static WRITE16_HANDLER ( write_beeper )
{
UINT8 beep_flag;
UINT8 data_8;

data_8=data>>8;

if (artwork_view==BOARD_VIEW)
	set_board(m_board,&Line18_LED,data_8);

lcd_invert=1;
beep_flag=data>>8;
//if ((beep_flag &02)== 0) key_selector=0;else key_selector=1;
 //logerror("Write Beeper   = %x \n  ",data);
 beeper=data;
}

static WRITE16_HANDLER ( write_lcd )
{
  UINT8 lcd_data;
  lcd_data = data>>8;
  output_set_digit_value(lcd_shift_counter,lcd_invert&1?lcd_data^0xff:lcd_data);

  if (led7==0)						//MOD RS
	 read_display(lcd_data);		//MOD RS

  lcd_shift_counter--;
  lcd_shift_counter&=3;
  //logerror("LCD Offset = %d Data low  = %x \n  ",offset,lcd_data);
}

static WRITE16_HANDLER ( write_lcd_flag )
{
  UINT8 lcd_flag;
//const device_config *speaker = devtag_get_device(space->machine, "beep");
  lcd_invert=0;
  lcd_flag=data>>8;

//   beep_set_state(speaker, lcd_flag & 1 ? 1 : 0);

  if (lcd_flag == 0) key_selector=1;
 // The key function in the rom expects after writing to
 // the  a value from  the second key row;
  if (lcd_flag!=0) led7=255;else led7=0;
  //logerror("LCD Flag 16  = %x \n  ",data);
}

static WRITE16_HANDLER ( write_lcd_flag_gg )
{
  running_device *speaker = devtag_get_device(space->machine, "beep");
  UINT8 lcd_flag;
  lcd_flag=data>>8;
  beep_set_state(speaker, lcd_flag & 1 ? 1 : 0);
  if (lcd_flag == 0) key_selector=1;
  if (lcd_flag!=0) led7=255;else led7=0;

  if (lcd_flag==1 && g_state==SEARCHING)						//MOD RS	Warten wenn Suche beendet wird
	sendBM=TRUE;												//MOD RS    Bestmove wird in timer routine gesendet
}

static WRITE16_HANDLER ( write_keys )
{
 key_select=data>>8;
 //logerror("Write Key   = %x \n  ",data);
}


static READ16_HANDLER(read_board)
{

UINT16 data;

if (artwork_view==BOARD_VIEW)
{
// Reed schalter auslesen
//
  data=get_board(m_board,&Line18_REED);
  data=data<<8;

  return data;
}else
  return 0xff00;                                   // Mephisto need it for working.
}


static WRITE16_HANDLER(write_board)
{
 UINT8 board;

 Line18_REED=data>>8;
 Line18_LED=data>>8;

 board=data>>8;
 board_value=board;
 if (board==0xff) key_selector=0;
 // The key function in the rom expects after writing to
 // the chess board a value from  the first key row;
  
}



static WRITE16_HANDLER ( write_board_gg )
{
UINT8 beep_flag;

Line18_REED=data>>8;
Line18_LED=data>>8;

lcd_invert=1;
beep_flag=data>>8;
//if ((beep_flag &02)== 0) key_selector=0;else key_selector=1;
//logerror("Write Beeper   = %x \n  ",data);
 beeper=data;
}

static WRITE16_HANDLER ( write_irq_flag )
{
 running_device *speaker = devtag_get_device(space->machine, "beep");
 beep_set_state(speaker, data&0x100);

 if ( g_state==SEARCHING )		//MOD RS
	  sendBM=TRUE;				//MOD RS

 //logerror("Write 0x800004   = %x \n  ",data);
 irq_flag=1;
 beeper=data;
}

static READ16_HANDLER(read_keys) // Glasgow, Dallas
{
  UINT16 data;
 
  data=0x0300;

  key_low = input_port_read(space->machine, "LINE0");
  key_hi =  input_port_read(space->machine, "LINE1");
//logerror("Keyboard Offset = %x Data = %x\n  ",offset,data);

  if (key_select==key_low)
  {
	  data=data&0x100;
  }
  if (key_select==key_hi)
  {
	  data=data&0x200;
  }

	if (data!=0x300 && (g_displayChanged  || 			    //MOD RS R�ckmeldung Tastendruck erkannt (Display hat sich ge�ndert)
		               (g_waitCnt > g_inputTimeout ) ) )	//MOD RS Timeout warten auf R�ckmeldung (z.B. beim Info Befehl zur Abfrage Promo
														    //MOD RS oder LEV 
	{
		g_portIsReady=TRUE;									//MOD RS
		if (key_select==key_low)							//MOD RS
			input_port_clear(space->machine,"LINE0");		//MOD RS
		else												//MOD RS
			input_port_clear(space->machine,"LINE1");		//MOD RS
	}														//MOD RS

  return data;
}

static READ16_HANDLER(read_newkeys16)  //Amsterdam, Roma
{
 UINT16 data;

 if (key_selector==0) 
	 data=input_port_read(space->machine, "LINE0");
 else 
	 data=input_port_read(space->machine, "LINE1");


	if (data && (g_displayChanged || 						//MOD RS R�ckmeldung Tastendruck erkannt (Display hat sich ge�ndert)
		               (g_waitCnt > g_inputTimeout ) ) )	//MOD RS Timeout warten auf R�ckmeldung (z.B. beim Info Befehl zur Abfrage Promo
														    //MOD RS oder LEV 
	{
		g_portIsReady=TRUE;									//MOD RS
		if (key_selector==0)								//MOD RS
			input_port_clear(space->machine,"LINE0");		//MOD RS
		else												//MOD RS
			input_port_clear(space->machine,"LINE1");		//MOD RS
	}														//MOD RS

 //logerror("read Keyboard Offset = %x Data = %x   Select = %x \n  ",offset,data,key_selector);
 data=data<<8;
 return data ;
}

static READ16_HANDLER(read_board_gg)
{
  UINT16 data;

if (artwork_view==BOARD_VIEW)
{
// Reed schalter auslesen
//
  data=get_board(m_board,&Line18_REED);
  data=data<<8;

//logerror("read_board_data = %x \n  ", data);
  return data;
}else
  return 0xff00;                                   // Mephisto need it for working.
}

static WRITE16_HANDLER(write_beeper_gg)				//= write_board_gg
{

	UINT8 data_8;

// LED's ansteuern
//
	data_8=data>>8;

	if (artwork_view==BOARD_VIEW)
		set_board(m_board,&Line18_LED,data_8);

}

/*

    *****           32 Bit Read and write Handler   ***********

*/

static WRITE32_HANDLER ( write_lcd32 )
{
  UINT8 lcd_data;
  lcd_data = data>>8;
  output_set_digit_value(lcd_shift_counter,lcd_invert&1?lcd_data^0xff:lcd_data);

  if (led7==0)						//MOD RS
	 read_display(lcd_data);		//MOD RS

  lcd_shift_counter--;
  lcd_shift_counter&=3;
  //logerror("LCD Offset = %d Data   = %x \n  ",offset,lcd_data);
}
static WRITE32_HANDLER ( write_lcd_flag32 )
{
  UINT8 lcd_flag;
  //UINT8 beep=0;
  lcd_invert=0;
  lcd_flag=data>>24;

  if (lcd_flag == 0) 
	  key_selector=1;
 
  //logerror("LCD Flag 32  = %x \n  ",lcd_flag);
  //beep_set_state(0,lcd_flag&1?1:0);

  if (lcd_flag!=0) 
	  led7=255;
  else 
	  led7=0;
}


static WRITE32_HANDLER ( write_keys32 )
{
 UINT8 data_8;

 lcd_invert=1;
 key_select=data;
 //logerror("Write Key = %x \n  ",key_select);


// LED's are controlled in write_keys32 
// other 8 und 16 bit devices -> write_beeper
// 
 data_8=data>>24;				
 if (artwork_view==BOARD_VIEW)
    	set_board(m_board,&Line18_LED,data_8);


  //logerror("Write_keys32   = %x \n  ",data);
}

static READ32_HANDLER(read_newkeys32) // Dallas 32, Roma 32
{
  UINT32 data;
 
   if (key_selector==0)
	data = input_port_read(space->machine, "LINE0");
   else
	data =  input_port_read(space->machine, "LINE1");

	if (data && (g_displayChanged || 						//MOD RS R�ckmeldung Tastendruck erkannt (Display hat sich ge�ndert)
		               (g_waitCnt > g_inputTimeout ) ) )	//MOD RS Timeout warten auf R�ckmeldung (z.B. beim Info Befehl zur Abfrage Promo
														    //MOD RS oder LEV 
	{
		g_portIsReady=TRUE;									//MOD RS
		if (key_selector==0)								//MOD RS
			input_port_clear(space->machine,"LINE0");		//MOD RS
		else												//MOD RS
			input_port_clear(space->machine,"LINE1");		//MOD RS
	}														//MOD RS


    data=data<<24;

 return data ;
}

static READ32_HANDLER(read_board32)
{

UINT16 data;
UINT32 data_32;

if (artwork_view==BOARD_VIEW)
{
// Reed schalter auslesen
//

  data=get_board(m_board,&Line18_REED);
  data_32=data<<24;

  return data_32;
}else
  return 0x00000000;                         // Mephisto need it for working.
}

static WRITE32_HANDLER(write_board32)
{
 UINT8 board;

 Line18_REED=data>>24;
 Line18_LED=data>>24;

 board=data>>24;
 if (board==0xff) key_selector=0;
 
}


static WRITE32_HANDLER ( write_beeper32 )
{

running_device *speaker = devtag_get_device(space->machine, "beep");

 beep_set_state(speaker,data&0x01000000);
 logerror("Write_beeper32   = %x \n  ",data);
 irq_flag=1;
 beeper=data;

 if ( g_state==SEARCHING )		//MOD RS
	 sendBM=TRUE;				//MOD RS
	
//  Log("Beep out data: %x\n!",data);
}

static TIMER_CALLBACK( update_waitCnt )		//MOD RS
{											//MOD RS
	g_waitCnt++;							//MOD RS   Z�hler f�r Verz�gerung Eingabe
}											//MOD RS


static TIMER_CALLBACK( BM_Check )			//MOD RS
{
	if (sendBM && g_state==SEARCHING)		//MOD RS   Verz�gerung bei Ausgabe bestmove
	{										//MOD RS
		if (sendBM_delay==0)				//MOD RS
		{									//MOD RS
			if (TestMove(g_display) )		//MOD RS
			{								//MOD RS
				g_state=BESTMOVE;			//MOD RS
				sendBM=FALSE;					//MOD RS
				sendBM_repeat=BM_REPEAT;		//MOD RS
				sendBM_delay=g_bestmoveWait;	//MOD RS
			}									//MOD RS
			else if (!strcmp(g_display,"MAT "))	//MOD RS
			{									//MOD RS
				Log("Matt: %s\n",g_display);	//MOD RS
				SendToGUI((char *)"resign\n");	//MOD RS
				g_state=DRIVER_READY;			//MOD RS	
			}
			else if (!strcmp(g_display,"rEuM") || !strcmp(g_display,"rE 3") ||	//MOD RS
				     !strcmp(g_display,"rE50") || !strcmp(g_display,"PATT") )	//MOD RS
			{																//MOD RS
				Log("Draw: %s\n",g_display);								//MOD RS

				SendToGUI((char *)"1/2-1/2\n");								//MOD RS
				g_state=DRIVER_READY;										//MOD RS
			}																//MOD RS
			else									//MOD RS	
			{

				if (sendBM_repeat>0)				//MOD RS			-> Pr�fen ob das �berhaupt notwendig ist !!!
				{									//MOD RS
					sendBM_repeat--;				//MOD RS
					sendBM_delay=g_bestmoveWait;	//MOD RS
					PrintAndLog("No BestMove:   %s try again - wait for %d waitCnt\n",g_display,g_bestmoveWait);	//MOD RS
				}else								//MOD RS
				{
					Log("Resign: %s\n",g_display);								//MOD RS
					SendToGUI((char *)"resign\n");								//MOD RS
					PrintAndLog("No BestMove:   %s\n",g_display);				//MOD RS
					g_state=DRIVER_READY;										//MOD RS
				}
			}

		}else								//MOD RS
			sendBM_delay--;					//MOD RS
	}										//MOD RS
}

static TIMER_CALLBACK( update_artwork )
{
	//artwork_view=get_artwork_view();		//MOD RS
	//if (artwork_view==BOARD_VIEW)			//MOD RS
	//	video_update(machine,0);			//MOD RS	
}

static TIMER_CALLBACK( update_nmi )
{
	cputag_set_input_line_and_vector(machine, "maincpu",  M68K_IRQ_7,ASSERT_LINE, M68K_INT_ACK_AUTOVECTOR);
	cputag_set_input_line_and_vector(machine, "maincpu",  M68K_IRQ_7,CLEAR_LINE, M68K_INT_ACK_AUTOVECTOR);
	irq_edge=~irq_edge;
}

static TIMER_CALLBACK( update_nmi32 )
{
	cputag_set_input_line_and_vector(machine, "maincpu",  M68K_IRQ_7, ASSERT_LINE, M68K_INT_ACK_AUTOVECTOR);
	cputag_set_input_line_and_vector(machine, "maincpu",  M68K_IRQ_7, CLEAR_LINE, M68K_INT_ACK_AUTOVECTOR);
	irq_edge=~irq_edge;
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

static MACHINE_START( glasgow )
{
	running_device *speaker = devtag_get_device(machine, "beep");
	key_selector=0;
	irq_flag=0;
	lcd_shift_counter=3;
	timer_pulse(machine, ATTOTIME_IN_HZ(50), NULL, 0, update_nmi);
	timer_pulse(machine, ATTOTIME_IN_HZ(20), NULL, 0, update_artwork);

	timer_pulse(machine, ATTOTIME_IN_HZ(60), NULL, 0, update_waitCnt);		//MOD RS           //Z�hler f�r Verz�gerung Eingabe
	timer_pulse(machine, ATTOTIME_IN_HZ(60), NULL, 0, BM_Check);			//MOD RS 

	beep_set_frequency(speaker, 44);

	state_save_register_global_array(machine,save_board);
	state_save_register_postload(machine,m_board_postload,NULL);
	state_save_register_presave(machine,m_board_presave,NULL);

	sendBM_delay=g_bestmoveWait;		//MOD RS
	sendBM_repeat=BM_REPEAT;			//MOD RS

}


static MACHINE_START( dallas32 )
{
	running_device *speaker = devtag_get_device(machine, "beep");
	lcd_shift_counter=3;

	timer_pulse(machine, ATTOTIME_IN_HZ(50), NULL, 0, update_nmi32);
	timer_pulse(machine, ATTOTIME_IN_HZ(20), NULL, 0, update_artwork);

	timer_pulse(machine, ATTOTIME_IN_HZ(60), NULL, 0, update_waitCnt);		//MOD RS           //Z�hler f�r Verz�gerung Eingabe
	timer_pulse(machine, ATTOTIME_IN_HZ(60), NULL, 0, BM_Check);			//MOD RS 

	beep_set_frequency(speaker, 44);

	state_save_register_global_array(machine,save_board);
	state_save_register_postload(machine,m_board_postload,NULL);
	state_save_register_presave(machine,m_board_presave,NULL);


	sendBM_delay=g_bestmoveWait;		//MOD RS
	sendBM_repeat=BM_REPEAT;			//MOD RS

}

static MACHINE_RESET( glasgow )
{
	 lcd_shift_counter=3;

	 set_startboard_from_startpos();
	 //clear_layout();							//MOD RS
	 //set_render_board();						//MOD RS
	 //set_status_of_pieces();					//MOD RS

	 video_update(dummy_machine,1);				//MOD RS

	 cpu_set_clock(machine->firstcpu,g_clock);  //MOD RS
}

static VIDEO_UPDATE( dallas32 )
{
 return 0;
}

static ADDRESS_MAP_START(glasgow_mem, ADDRESS_SPACE_PROGRAM, 16)
  AM_RANGE(0x00000000, 0x0000ffff) AM_ROM
  AM_RANGE( 0x00ff0000, 0x00ff0001)  AM_MIRROR ( 0xfe0000 ) AM_WRITE     ( write_lcd_gg )
  AM_RANGE( 0x00ff0002, 0x00ff0003)  AM_MIRROR ( 0xfe0002 ) AM_READWRITE ( read_keys,write_keys )
  AM_RANGE( 0x00ff0004, 0x00ff0005)  AM_MIRROR ( 0xfe0004 ) AM_WRITE     ( write_lcd_flag_gg )
  AM_RANGE( 0x00ff0006, 0x00ff0007)  AM_MIRROR ( 0xfe0006 ) AM_READWRITE ( read_board_gg, write_beeper_gg )
  AM_RANGE( 0x00ff0008, 0x00ff0009)  AM_MIRROR ( 0xfe0008 ) AM_WRITE     ( write_board_gg )
  AM_RANGE( 0x00ffC000, 0x00ffFFFF)  AM_MIRROR ( 0xfeC000 ) AM_RAM      // 16KB
ADDRESS_MAP_END


static ADDRESS_MAP_START(amsterd_mem, ADDRESS_SPACE_PROGRAM, 16)
	// ADDRESS_MAP_GLOBAL_MASK(0x7FFFF)
    AM_RANGE(0x00000000, 0x0000ffff) AM_ROM
    AM_RANGE( 0x00800002, 0x00800003)  AM_WRITE( write_lcd )
    AM_RANGE( 0x00800008, 0x00800009)  AM_WRITE( write_lcd_flag )
    AM_RANGE( 0x00800004, 0x00800005)  AM_WRITE( write_irq_flag )
    AM_RANGE( 0x00800010, 0x00800011)  AM_WRITE( write_board )
    AM_RANGE( 0x00800020, 0x00800021)  AM_READ ( read_board )
    AM_RANGE( 0x00800040, 0x00800041)  AM_READ ( read_newkeys16 )
    AM_RANGE( 0x00800088, 0x00800089)  AM_WRITE( write_beeper )
    AM_RANGE( 0x00ffC000, 0x00ffFFFF)  AM_RAM      // 16KB
ADDRESS_MAP_END


static ADDRESS_MAP_START(dallas32_mem, ADDRESS_SPACE_PROGRAM, 32)
     // ADDRESS_MAP_GLOBAL_MASK(0x1FFFF)
     AM_RANGE(0x00000000, 0x0000ffff) AM_ROM
     AM_RANGE( 0x00800000, 0x00800003) AM_WRITE ( write_lcd32 )
     AM_RANGE( 0x00800004, 0x00800007) AM_WRITE ( write_beeper32 )
     AM_RANGE( 0x00800008, 0x0080000B) AM_WRITE ( write_lcd_flag32 )
     AM_RANGE( 0x00800010, 0x00800013) AM_WRITE ( write_board32 )
     AM_RANGE( 0x00800020, 0x00800023) AM_READ  ( read_board32 )
     AM_RANGE( 0x00800040, 0x00800043) AM_READ  ( read_newkeys32 )
     AM_RANGE( 0x00800088, 0x0080008b) AM_WRITE ( write_keys32 )
     AM_RANGE( 0x0010000, 0x001FFFF)   AM_RAM      // 64KB
ADDRESS_MAP_END

static INPUT_PORTS_START( new_keyboard ) //Amsterdam, Dallas 32, Roma, Roma 32
  PORT_START("LINE0")
  PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("A1")  PORT_CODE(KEYCODE_A ) PORT_CODE(KEYCODE_1 )
  PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("B2")  PORT_CODE(KEYCODE_B ) PORT_CODE(KEYCODE_2 )
  PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("C3")  PORT_CODE(KEYCODE_C ) PORT_CODE(KEYCODE_3 )
  PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("D4")  PORT_CODE(KEYCODE_D ) PORT_CODE(KEYCODE_4 )
  PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("E5")  PORT_CODE(KEYCODE_E ) PORT_CODE(KEYCODE_5 )
  PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F6")  PORT_CODE(KEYCODE_F ) PORT_CODE(KEYCODE_6 )
  PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("0")   PORT_CODE(KEYCODE_9 )
  PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("9")   PORT_CODE(KEYCODE_0 )
  PORT_START("LINE1")
  PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("INF") PORT_CODE(KEYCODE_F1 )
  PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("POS") PORT_CODE(KEYCODE_F2 )
  PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("LEV") PORT_CODE(KEYCODE_F3 )
  PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MEM") PORT_CODE(KEYCODE_F4 )
  PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("CLR") PORT_CODE(KEYCODE_F5 )
  PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("ENT") PORT_CODE(KEYCODE_ENTER )
  PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("G7")  PORT_CODE(KEYCODE_G ) PORT_CODE(KEYCODE_7 )
  PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("H8")  PORT_CODE(KEYCODE_H ) PORT_CODE(KEYCODE_8 )

  PORT_START("MOUSE_X")
  PORT_BIT( 0xffff, 0x00, IPT_MOUSE_X)  PORT_SENSITIVITY(100) PORT_KEYDELTA(1) PORT_MINMAX(0, 65535) 	PORT_PLAYER(1)

  PORT_START("MOUSE_Y")
  PORT_BIT( 0xffff, 0x00, IPT_MOUSE_Y ) PORT_SENSITIVITY(100) PORT_KEYDELTA(1) PORT_MINMAX(0, 65535) 	PORT_PLAYER(1)

  PORT_START("BUTTON_L")
  PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON2) PORT_CODE(MOUSECODE_BUTTON1) PORT_NAME("left button")

  PORT_START("BUTTON_R")
  PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1) PORT_CODE(MOUSECODE_BUTTON2) PORT_NAME("right button")
INPUT_PORTS_END



static INPUT_PORTS_START( old_keyboard )   //Glasgow,Dallas
  PORT_START("LINE0")
    PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("9")   PORT_CODE(KEYCODE_9 )
    PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("CL")  PORT_CODE(KEYCODE_F5 )
    PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("C3")  PORT_CODE(KEYCODE_C ) PORT_CODE(KEYCODE_3 )
    PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("ENT") PORT_CODE(KEYCODE_ENTER )
    PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("D4")  PORT_CODE(KEYCODE_D ) PORT_CODE(KEYCODE_4 )
    PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("A1")  PORT_CODE(KEYCODE_A ) PORT_CODE(KEYCODE_1 )
    PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F6")  PORT_CODE(KEYCODE_F ) PORT_CODE(KEYCODE_6 )
    PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("B2")  PORT_CODE(KEYCODE_B ) PORT_CODE(KEYCODE_2 )
  PORT_START("LINE1")
    PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("E5")  PORT_CODE(KEYCODE_E ) PORT_CODE(KEYCODE_5 )
    PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("INF") PORT_CODE(KEYCODE_F1 )
    PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("0")   PORT_CODE(KEYCODE_0 )
    PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("POS") PORT_CODE(KEYCODE_F2 )
    PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("H8")  PORT_CODE(KEYCODE_H ) PORT_CODE(KEYCODE_8 )
    PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("LEV") PORT_CODE(KEYCODE_F3)
    PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("G7")  PORT_CODE(KEYCODE_G ) PORT_CODE(KEYCODE_7 )
    PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("MEM") PORT_CODE(KEYCODE_F4)

  PORT_START("MOUSE_X")
    PORT_BIT( 0xffff, 0x00, IPT_MOUSE_X)  PORT_SENSITIVITY(100) PORT_KEYDELTA(1) PORT_MINMAX(0, 65535) 	PORT_PLAYER(1)

  PORT_START("MOUSE_Y")
    PORT_BIT( 0xffff, 0x00, IPT_MOUSE_Y ) PORT_SENSITIVITY(100) PORT_KEYDELTA(1) PORT_MINMAX(0, 65535) 	PORT_PLAYER(1)

  PORT_START("BUTTON_L")
    PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON2) PORT_CODE(MOUSECODE_BUTTON1) PORT_NAME("left button")

  PORT_START("BUTTON_R")
    PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1) PORT_CODE(MOUSECODE_BUTTON2) PORT_NAME("right button")
INPUT_PORTS_END



static MACHINE_DRIVER_START(glasgow )
    /* basic machine hardware */
    MDRV_CPU_ADD("maincpu", M68000, 12000000)
	MDRV_CPU_PROGRAM_MAP(glasgow_mem)
	MDRV_MACHINE_START(glasgow)
	MDRV_MACHINE_RESET(glasgow)

    MDRV_DEFAULT_LAYOUT(layout_glasgow)
    
    MDRV_SPEAKER_STANDARD_MONO("mono")
	MDRV_SOUND_ADD("beep", BEEP, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.0)
MACHINE_DRIVER_END


static MACHINE_DRIVER_START(amsterd )
	MDRV_IMPORT_FROM( glasgow )
	/* basic machine hardware */
	MDRV_CPU_MODIFY("maincpu")
	MDRV_CPU_PROGRAM_MAP(amsterd_mem)
//	MDRV_VIDEO_UPDATE(dallas32)
MACHINE_DRIVER_END


static MACHINE_DRIVER_START(dallas32 )
	MDRV_IMPORT_FROM( glasgow )
	/* basic machine hardware */
	MDRV_CPU_REPLACE("maincpu", M68020, 14000000)
	MDRV_CPU_PROGRAM_MAP(dallas32_mem)
	MDRV_MACHINE_START( dallas32 )
  MDRV_VIDEO_UPDATE(dallas32)
MACHINE_DRIVER_END


/***************************************************************************
  ROM definitions
***************************************************************************/

ROM_START( glasgow )
    ROM_REGION( 0x10000, "maincpu", 0 )
    //ROM_LOAD("glasgow.rom", 0x000000, 0x10000, CRC(3e73eff3) )
    ROM_LOAD16_BYTE("me3_3_1u.410",0x00000, 0x04000,CRC(bc8053ba) SHA1(57ea2d5652bfdd77b17d52ab1914de974bd6be12))
    ROM_LOAD16_BYTE("me3_1_1l.410",0x00001, 0x04000,CRC(d5263c39) SHA1(1bef1cf3fd96221eb19faecb6ec921e26ac10ac4))
    ROM_LOAD16_BYTE("me3_4_2u.410",0x08000, 0x04000,CRC(8dba504a) SHA1(6bfab03af835cdb6c98773164d32c76520937efe))
    ROM_LOAD16_BYTE("me3_2_2l.410",0x08001, 0x04000,CRC(b3f27827) SHA1(864ba897d24024592d08c4ae090aa70a2cc5f213))
ROM_END

ROM_START( amsterd )
    ROM_REGION16_BE( 0x1000000, "maincpu", 0 )
    //ROM_LOAD16_BYTE("output.bin", 0x000000, 0x10000, CRC(3e73eff3) )
    ROM_LOAD16_BYTE("amsterda-u.bin",0x00000, 0x05a00,CRC(16cefe29) SHA1(9f8c2896e92fbfd47159a59cb5e87706092c86f4))
    ROM_LOAD16_BYTE("amsterda-l.bin",0x00001, 0x05a00,CRC(c859dfde) SHA1(b0bca6a8e698c322a8c597608db6735129d6cdf0))
ROM_END


ROM_START( dallas )
    ROM_REGION16_BE( 0x1000000, "maincpu", 0 )
    ROM_LOAD16_BYTE("dal_g_pr.dat",0x00000, 0x04000,CRC(66deade9) SHA1(07ec6b923f2f053172737f1fc94aec84f3ea8da1))
    ROM_LOAD16_BYTE("dal_g_pl.dat",0x00001, 0x04000,CRC(c5b6171c) SHA1(663167a3839ed7508ecb44fd5a1b2d3d8e466763))
    ROM_LOAD16_BYTE("dal_g_br.dat",0x08000, 0x04000,CRC(e24d7ec7) SHA1(a936f6fcbe9bfa49bf455f2d8a8243d1395768c1))
    ROM_LOAD16_BYTE("dal_g_bl.dat",0x08001, 0x04000,CRC(144a15e2) SHA1(c4fcc23d55fa5262f5e01dbd000644a7feb78f32))
ROM_END

ROM_START( dallas16 )
    ROM_REGION16_BE( 0x1000000, "maincpu", 0 )
    ROM_LOAD16_BYTE("dallas-u.bin",0x00000, 0x06f00,CRC(8c1462b4) SHA1(8b5f5a774a835446d08dceacac42357b9e74cfe8))
    ROM_LOAD16_BYTE("dallas-l.bin",0x00001, 0x06f00,CRC(f0d5bc03) SHA1(4b1b9a71663d5321820b4cf7da205e5fe5d3d001))
ROM_END

ROM_START( roma )
    ROM_REGION16_BE( 0x1000000, "maincpu", 0 )
    ROM_LOAD("roma32.bin", 0x000000, 0x10000, CRC(587d03bf) SHA1(504e9ff958084700076d633f9c306fc7baf64ffd))
ROM_END

ROM_START( dallas32 )
    ROM_REGION( 0x10000, "maincpu", 0 )
    ROM_LOAD("dallas32.epr", 0x000000, 0x10000, CRC(83b9ff3f) SHA1(97bf4cb3c61f8ec328735b3c98281bba44b30a28) )
ROM_END

ROM_START( roma32 )
    ROM_REGION( 0x10000, "maincpu", 0 )
    ROM_LOAD("roma32.bin", 0x000000, 0x10000, CRC(587d03bf) SHA1(504e9ff958084700076d633f9c306fc7baf64ffd) )
ROM_END

/***************************************************************************
  System config
***************************************************************************/



/***************************************************************************
  Game drivers
***************************************************************************/

/*     YEAR, NAME,     PARENT,   BIOS, COMPAT MACHINE,INPUT,          INIT,    COMPANY,                      FULLNAME,                 FLAGS */
CONS(  1984, glasgow,  0,        0,    glasgow,       old_keyboard,   0,	   "Hegener & Glaser Muenchen",  "Mephisto III S Glasgow", GAME_SUPPORTS_SAVE)
CONS(  1984, amsterd,  0,        0,    amsterd,       new_keyboard,   0,	   "Hegener & Glaser Muenchen",  "Mephisto Amsterdam",     GAME_SUPPORTS_SAVE)
CONS(  1984, dallas,   0,        0,    glasgow,       old_keyboard,   0,	   "Hegener & Glaser Muenchen",  "Mephisto Dallas",        GAME_SUPPORTS_SAVE)
CONS(  1984, roma,     0,        0,    glasgow,       new_keyboard,   0,	   "Hegener & Glaser Muenchen",  "Mephisto Roma",          GAME_NOT_WORKING)
CONS(  1984, dallas32, 0,        0,    dallas32,      new_keyboard,   0,	   "Hegener & Glaser Muenchen",  "Mephisto Dallas 32 Bit", GAME_SUPPORTS_SAVE)
CONS(  1984, roma32,   0,        0,    dallas32,      new_keyboard,   0,	   "Hegener & Glaser Muenchen",  "Mephisto Roma 32 Bit",   GAME_SUPPORTS_SAVE)
CONS(  1984, dallas16, 0,        0,    amsterd,       new_keyboard,   0,	   "Hegener & Glaser Muenchen",  "Mephisto Dallas 16 Bit", GAME_SUPPORTS_SAVE)



static void read_display (UINT8 lcd_data)
{
	output_set_digit_value(lcd_shift_counter,lcd_data);

	assert(lcd_shift_counter<=3);					//MOD RS

	lcd_data=lcd_data&127;

	g_display[lcd_shift_counter]=g_segment[lcd_data]; //MOD RS

	if (lcd_shift_counter==0				&&		//MOD RS
		strstr(g_display,"!") == NULL		&&		//MOD RS   (wegen Amsterdam)
		strstr(g_display,"1888") == NULL	&&		//MOD RS   (wegen Amsterdam)
		strncmp(g_display,"    ",4)			&&		//MOD RS
		strncmp(g_display,saveDisplay,4) )			//MOD RS
	{
		g_display[4]='\0';							//MOD RS
		strncpy(saveDisplay,g_display,4);			//MOD RS

		//if (!(g_state==SEARCHING	&&				//MOD RS		Keine Displayausgabe w�hrend der Suche
		//	  g_unlimited			&&				//MOD RS		Viele Ausgaben besonders bei g_unlimited
		//	  g_xboard_mode) )						//MOD RS
			printf("%s\n",g_display);				//MOD RS

		Log("Display: %s\n",g_display);				//MOD RS

		if (!strncmp(g_display,"Err1",4) ||			//MOD RS
			!strncmp(g_display,"Err2",4) ||			//MOD RS
			!strncmp(g_display,"Err3",4))			//MOD RS
		{											//MOD RS
			g_error=TRUE;							//MOD RS
			g_state=DRIVER_READY;					//MOD RS
		}											//MOD RS

//			printf("g_dispayChanged\n");			//MOD RS

		if (strncmp(g_display,"TIME",4))			//MOD RS Sonderfall Anzeige TIME soll nicht g_displayChanged ausl�sen
			g_displayChanged=TRUE;					//MOD RS
		
	}

}


#include "mephistoboard_func.h"

