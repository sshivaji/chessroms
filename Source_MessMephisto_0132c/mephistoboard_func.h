static void set_cursor (running_machine *machine, view_item *view_cursor)
  {

   render_target *my_target;

   INT32 ui_cur_x, ui_cur_y;
   int ui_button;

   static float del_x, del_y;

	//if ( (view_cursor->rawbounds.x0/MAX_X) > 1 )
	//	return;	

	//if ( (view_cursor->rawbounds.y0/MAX_Y) > 1 )
	//	return;	

	//if ( (view_cursor->rawbounds.x1/MAX_X) > 1 )
	//	return;	

	//if ( (view_cursor->rawbounds.y1/MAX_Y) > 1 )
	//	return;	


// Get UI Cursor x,y
//
	my_target=ui_input_find_mouse(machine,&ui_cur_x,&ui_cur_y,&ui_button);

	if (my_target)
	{
// Adjust cursor coordinates
//
		ui_cur_x = ui_cur_x * (float)( (float) MAX_X / (float) my_target->width);
		ui_cur_y = ui_cur_y * (float)( (float) MAX_Y / (float) my_target->height);

		del_x = view_cursor->rawbounds.x1 - view_cursor->rawbounds.x0;
		del_y = view_cursor->rawbounds.y1 - view_cursor->rawbounds.y0;

		view_cursor->rawbounds.x0 = ui_cur_x-(del_x/2);	
		view_cursor->rawbounds.y0 = ui_cur_y-(del_y/2);

		view_cursor->rawbounds.x1 = view_cursor->rawbounds.x0 + del_x;
		view_cursor->rawbounds.y1 = view_cursor->rawbounds.y0 + del_y;

		view_cursor->bounds.x0 = view_cursor->rawbounds.x0 / MAX_X;
		view_cursor->bounds.y0 = view_cursor->rawbounds.y0 / MAX_Y;
		view_cursor->bounds.x1 = view_cursor->rawbounds.x1 / MAX_X;
		view_cursor->bounds.y1 = view_cursor->rawbounds.y1 / MAX_Y;


		//view_cursor->bounds.x0 = view_cursor->rawbounds.x0 / my_target->base_view->bounds.x1;
		//view_cursor->bounds.y0 = view_cursor->rawbounds.y0 / my_target->base_view->bounds.y1;
		//view_cursor->bounds.x1 = view_cursor->rawbounds.x1 / my_target->base_view->bounds.x1;
		//view_cursor->bounds.y1 = view_cursor->rawbounds.y1 / my_target->base_view->bounds.y1;

	}

  }

//render_target *render_target_get_indexed


view_item *get_view_item(render_target *target, const char *v_name)
{

 view_item *cur_view;
 layout_view *layout_view;

 layout_view = target->curview;

  cur_view = target->curview->itemlist[3]->next;

 for (; cur_view != NULL; cur_view = cur_view->next)
 {
	if (!strcmp(cur_view->output_name, v_name))
		{
		return (cur_view);
		}
 }

return (NULL);
}


BOARD_FIELD get_field( float i_x, float i_y, UINT8 mouse_move)
{
UINT8 i_AH, i_18;
float corr_x, corr_y;
 
float f_x, f_y;

BOARD_FIELD null_board  = { 0,0,0,"0"};


// Correction of mouse pointer
//

		corr_x = i_x+26;
		corr_y = i_y+26;


// Loop ?ber alle Spalten
		for ( i_AH = 0; i_AH < 8; i_AH = i_AH + 1)
		{

            for ( i_18 = 0; i_18 < 8; i_18 = i_18 + 1)
				{
				 f_x =	(float) m_board [i_18] [i_AH].x;
				 f_y =	(float) m_board [i_18] [i_AH].y;

                if (   ( corr_x >= f_x  && corr_x <= f_x+56 ) && ( corr_y >= f_y  && corr_y <= f_y+56 ) )
				 {
				 return (m_board [i_18] [i_AH]);
				 }

				}
		}

return (null_board);
}


static unsigned int out_of_board( float x0, float y0)
{

	if ( ( x0 >= MIN_BOARD_X  && x0 <= MAX_BOARD_X ) && ( y0 >= MIN_BOARD_Y  && y0 <= MAX_BOARD_Y ) )
		{
		return (0);
		}
	else
		{
		return (1);
		}
}



static void update_board(render_target *target, BOARD_FIELD board_field, unsigned int update_clear_flag)
{

   UINT8 i_18, i_AH;

   view_item *view_item;

   div_t div_result;

   div_result = div(board_field.field, 8);

   i_18 = div_result.quot;
   i_AH = 7 - div_result.rem;

   if (update_clear_flag)
	{
// there is already a piece on target field - > set it invisible
//
		view_item = get_view_item(target, (char *)m_board[i_18][i_AH].piece);
		if (view_item != NULL)
			{
			view_item->color.a = 0;
			}
		strcpy ((char *)&m_board[i_18][i_AH].piece[0], (char *)&board_field.piece[0]);
	}else
	{
		strcpy ((char *)m_board[i_18][i_AH].piece, EMP);
	}

	set_status_of_pieces();

}

 static void calculate_bounds(view_item *view_item, float new_x0, float new_y0, float new_del_x, float new_del_y )
  {

   static float del_x, del_y;

      if (new_del_x)
		{
		del_x = new_del_x;
		}
	  else
		{
		del_x = view_item->rawbounds.x1 - view_item->rawbounds.x0;
		}

      if (new_del_y)
		{
		del_y = new_del_y;
		}
	  else
		{
		del_y = view_item->rawbounds.y1 - view_item->rawbounds.y0;
		}

      view_item->rawbounds.x0 = new_x0;
      view_item->rawbounds.y0 = new_y0;
      view_item->rawbounds.x1 = view_item->rawbounds.x0 + del_x;
      view_item->rawbounds.y1 = view_item->rawbounds.y0 + del_y;

// Calculate bounds
//
	  view_item->bounds.x0 = view_item->rawbounds.x0 / MAX_X;
	  view_item->bounds.y0 = view_item->rawbounds.y0 / MAX_Y;
	  view_item->bounds.x1 = view_item->rawbounds.x1 / MAX_X;
	  view_item->bounds.y1 = view_item->rawbounds.y1 / MAX_Y;

  }


static void set_render_board()
	{

    render_target *my_target;
    view_item *view_item;
    UINT8 i_AH, i_18;

	my_target = render_get_ui_target();


    for ( i_AH = 0; i_AH < 8; i_AH = i_AH + 1)
		{
         for ( i_18 = 0; i_18 < 8; i_18 = i_18 + 1)
			{

// copy start postition to m_board
//
            m_board[i_18][i_AH] = start_board [i_18] [i_AH];

// Get view item of this piece
//
			if (strcmp((char *)m_board[i_18][i_AH].piece, EMP))
				{
				view_item = get_view_item(my_target, (char *)m_board[i_18][i_AH].piece);

// change all pieces
//
				if (view_item != NULL)
					{
					calculate_bounds(view_item, m_board[i_18][i_AH].x, m_board[i_18][i_AH].y, 0, 0 );
					view_item->color.a = 1.0;
					}//ENDIF view_item

				} // ENDIF strcmp
			}// FOR i_18
		}// FOR i_AH

    render_set_ui_target (my_target);

	}

static void clear_layout()
	{

    render_target *my_target;
    view_item *view_item;
    UINT8 i;

	my_target = render_get_ui_target();

	for ( i = 0; i < 44; i = i + 1)
		{
         view_item = get_view_item(my_target,(char *)all_pieces[i].piece);
		 if (view_item != NULL)
			{
			view_item->color.a = 0.0;
			}
		}

    render_set_ui_target (my_target);

	}



static void set_status_of_pieces()
	{

    UINT8 i_AH, i_18 , i;
    BOARD_FIELD field;


	for ( i = 0; i < 47; i = i + 1)
		{

        all_pieces[i].set = 0;

	    for ( i_AH = 0; i_AH < 8; i_AH = i_AH + 1)
			{
			for ( i_18 = 0; i_18 < 8; i_18 = i_18 + 1)
				{

// set status depending piece is in game or not
//
            field = m_board[i_18][i_AH];

				if (!strcmp((char *)m_board[i_18][i_AH].piece, (char *) all_pieces[i].piece))
					{
					all_pieces[i].set = 1;
					break;
					}

				} // END for i_18=0

			if (all_pieces[i].set) break;

			}// END for i_AH=0
		} // END for i=0

	}


static const char *get_non_set_pieces(const char *cur_piece)
{
	UINT8 i=0;

	do
		{

		start_i = start_i+1;
		if ( start_i ==  44) start_i = 0;


	    if (!all_pieces[start_i].set && ( all_pieces[start_i].piece[0] != cur_piece[0] ||
		                                  all_pieces[start_i].piece[1] != cur_piece[1]) )
  			{
			return ((const char *)&all_pieces[start_i].piece[0]);
			}

		i=i+1;
}
	while (i != 44);

	return (NO_PIECE);
}

static int is_piece_set(const char *cur_piece)
{
	int i;

	for (i=0; i<44;i++)
	{
		if (!strcmp((char *)cur_piece, (char *) all_pieces[i].piece) &&  all_pieces[i].set)
			return TRUE;
	}

	return FALSE;
}

static void video_update(running_machine *machine, UINT8 reset)
{

    const char *piece;

	float save_x,  save_y;

	render_target *my_target;

	static view_item *my_cursor;
	static UINT8 m_button1 , m_button2;
	static UINT8 MOUSE_MOVE = 0;
    static UINT8 MOUSE_BUTTON1_WAIT = 0;
    static UINT8 MOUSE_BUTTON2_WAIT = 0;

	static BOARD_FIELD cursor_field;

	if (reset)
	{
		my_cursor=NULL;
		return;
	}

	my_target = render_get_ui_target();

// Get view item of cursor
//
//  my_cursor = my_target->curview->itemlist[3]->next;
	if (my_cursor == NULL)
	{
		my_cursor = get_view_item(my_target,"CURSOR_1");
		my_cursor->color.a = 0;								//Do not show old cursor
	}

//  my_cursor = my_target->curview->itemlist[2];

// Mouse movement
//
	if (my_cursor != NULL)
		set_cursor (machine, my_cursor);

    m_button1=input_port_read(machine, "BUTTON_L");
    m_button2=input_port_read(machine, "BUTTON_R");

    if ( m_button1) MOUSE_BUTTON1_WAIT = 0;

    if ( m_button1 != 1 && !MOUSE_BUTTON1_WAIT )
		{

			MOUSE_BUTTON1_WAIT = 1;

			if (MOUSE_MOVE == 0)
				{

				cursor_field = get_field(my_cursor->rawbounds.x0,my_cursor->rawbounds.y0, MOUSE_MOVE);
				if ( strcmp((char *)cursor_field.piece, "0") && (strcmp((char *)cursor_field.piece, EMP))  && strcmp(my_cursor->output_name, (char *)cursor_field.piece ) )
					{
					update_board(my_target,cursor_field, 0);

					my_cursor->color.a = 0;

					my_cursor = get_view_item(my_target, (char *)cursor_field.piece);
					my_cursor->color.a = 0.5;

					MOUSE_MOVE = 1;

					}
				} // ENDIF MOUSE_MOVE
			else
				{

				save_x = my_cursor->rawbounds.x0;
				save_y = my_cursor->rawbounds.y0;

				cursor_field = get_field(my_cursor->rawbounds.x0,my_cursor->rawbounds.y0, MOUSE_MOVE);
				if ( strcmp((char *)cursor_field.piece, "0") && strcmp(my_cursor->output_name, "CURSOR_1") )
					{

// Put piece back to board
//
		           strcpy((char *)cursor_field.piece, (char *)my_cursor->output_name);
				   update_board(my_target,cursor_field, 1);
				   my_cursor->color.a = 1;

// Set piece to field
//
					calculate_bounds(my_cursor, (float) cursor_field.x, (float) cursor_field.y, 0 ,0  );


// Get old cursor and set new x,y
//
					my_cursor = get_view_item(my_target,"CURSOR_1");
					my_cursor->color.a = 0;				//Do not show old cursor

					calculate_bounds(my_cursor, save_x, save_y,MOUSE_X ,MOUSE_Y  );

					MOUSE_MOVE = 0;

					}//ENDIF curosr_field

// piece is set out of board -> remove it
//
        if  ( out_of_board(my_cursor->rawbounds.x0,my_cursor->rawbounds.y0) )
			{

			save_x = my_cursor->rawbounds.x0;
			save_y = my_cursor->rawbounds.y0;

            my_cursor->color.a = 0;
			my_cursor = get_view_item(my_target,"CURSOR_1");
			my_cursor->color.a = 0;						//Do not show old cursor
			calculate_bounds(my_cursor, save_x, save_y,MOUSE_X ,MOUSE_X  );

			MOUSE_MOVE = 0;
			}

			}//ELSE MOUSE_MOVE

	    } // ENDIF m_button1


// right mouse button -> set pieces
//
	if ( m_button2) MOUSE_BUTTON2_WAIT = 0;
    if ( m_button2 != 1 && !MOUSE_BUTTON2_WAIT )
		{

		MOUSE_BUTTON2_WAIT = 1;

// save cursor position
//
		save_x = my_cursor->rawbounds.x0;
		save_y = my_cursor->rawbounds.y0;

// cursor off
//
		my_cursor->color.a = 0;

// change cursor to piece
//
        piece = get_non_set_pieces(my_cursor->output_name);

		if (strcmp(piece,NO_PIECE))
			{
			my_cursor = get_view_item(my_target,piece);
			my_cursor->color.a = 0.5;

			calculate_bounds(my_cursor, save_x, save_y, 0 ,0  );

			MOUSE_MOVE = 1;
			}
		else
			{
     		my_cursor = get_view_item(my_target,"CURSOR_1");
			my_cursor->color.a = 1;
            calculate_bounds(my_cursor, save_x, save_y, MOUSE_X ,MOUSE_X  );
			MOUSE_MOVE = 0;
			}

		}

// Put Item back to render
//
    render_set_ui_target (my_target);

}


static UINT16 get_board(BOARD_FIELD board[8][8], UINT16 *p_Line18_REED)
{

	  UINT8 i_18, i_AH;
	  UINT16 data;

	  data = 0xff;

      for ( i_18 = 0; i_18 < 8; i_18 = i_18 + 1)
	   	{

// Looking for cleared bit in Line18 -> current line
//
		if ( !(*p_Line18_REED & (1<<i_18)) )
           {

// if there is a piece on the field -> set bit in data
//
           for ( i_AH = 0; i_AH < 8; i_AH = i_AH + 1)
				{
					if (strcmp ((char *)m_board[i_18][i_AH].piece, EMP))
					{
						data &= ~(1 << i_AH);  // clear bit
					}
				}
		   }
	    }

	   return data;

}

static void set_board(BOARD_FIELD board[8][8], UINT16 *p_Line18_LED, UINT8 set_data)
{
    UINT8 i_AH, i_18;
    UINT16 LineAH = 0;

	UINT8 LED;

 
//  First all LED's off
//
	for ( i_AH = 0; i_AH < 8; i_AH = i_AH + 1)
		{
		for ( i_18 = 0; i_18 < 8; i_18 = i_18 + 1)
			{
                LED = board[i_18][i_AH].field;
                output_set_led_value( LED, 0 );

		    } //End For 18
		} // End For AH

// Now set LED's
//

	LineAH = set_data;

 //logerror("Line18_LED     = %x \n  ",*p_Line18_LED);
 //logerror("set_data       = %x \n  ",set_data);
 //logerror("LineAH         = %x \n  ",LineAH);

	if (LineAH && *p_Line18_LED)
	{

  //logerror("Line18_LED   = %x \n  ",*p_Line18_LED);
  //logerror("LineAH   = %x \n  ",LineAH);

		for ( i_AH = 0; i_AH < 8; i_AH = i_AH + 1)
		{

         if ( LineAH & (1<<i_AH) )
            {
            for ( i_18 = 0; i_18 < 8; i_18 = i_18 + 1)
				{
					if ( !(*p_Line18_LED & (1<<i_18)) )
						{

//                      logerror("i_18   = %d \n  ",i_18);
//                      logerror("i_AH   = %d \n  ",i_AH);
//                      logerror("LED an:   = %d \n  ",board[i_18][i_AH]);

                        LED = board[i_18][i_AH].field;
                        output_set_led_value( LED, 1 );

//  LED on
						}
					else
						{
//  LED off
						LED = board[i_18][i_AH].field;
                        output_set_led_value( LED, 0 );

      					} // End IF 18
				} // End For 18
           } // End IF AH

		} // End For AH

	} // End IF LineAH

}

static UINT8 get_artwork_view(void)
{
	render_target *my_target;

	my_target = render_get_ui_target();

	if ( !strcmp (my_target->curview->name, "Full artwork") )
		return BOARD_VIEW;

	if ( !strcmp (my_target->curview->name, "Modules") )
		return MODULE_VIEW;

	return 0;
}

static void set_startboard_from_startpos(void)
{
int i_AH;
int i_18;
	


for ( i_AH = 0; i_AH < 8; i_AH = i_AH + 1)
	{
     for ( i_18 = 0; i_18 < 8; i_18 = i_18 + 1)
 		{
			start_board[i_18][i_AH]=start_pos[i_18][i_AH];
		}
	}

}

static void set_startboard_from_array(UINT8* hboard)
{

int board_index;

int i_AH;
int i_18;

int br_cnt = 1;
int bn_cnt = 1;
int bb_cnt = 1;
int bq_cnt = 1;
int bp_cnt = 1;

int wr_cnt = 1;
int wn_cnt = 1;
int wb_cnt = 1;
int wq_cnt = 1;
int wp_cnt = 1;

for (board_index =0; board_index<64;board_index++)
{
	i_AH = flip[board_index] % 8;
	i_18 = flip[board_index] / 8;


	switch (hboard[board_index]) 
	{

		case WP:
			strcpy((char *) start_board[i_18][i_AH].piece, "WP");
			strcat((char *)start_board[i_18][i_AH].piece, my_itoa(wp_cnt++));
			assert(wp_cnt<=9);
			break;
		case WR:
			strcpy((char *)start_board[i_18][i_AH].piece, "WR");
			strcat((char *)start_board[i_18][i_AH].piece, my_itoa(wr_cnt++));
			assert(wr_cnt<=3);
			break;
		case WN:
			strcpy((char *)start_board[i_18][i_AH].piece, "WN");
			strcat((char *)start_board[i_18][i_AH].piece, my_itoa(wn_cnt++));
			assert(wn_cnt<=3);
			break;
		case WB:
			strcpy((char *)start_board[i_18][i_AH].piece, "WB");
			strcat((char *)start_board[i_18][i_AH].piece, my_itoa(wb_cnt++));
			assert(wb_cnt<=3);
			break;
		case WQ:
			strcpy((char *)start_board[i_18][i_AH].piece, "WQ");
			strcat((char *)start_board[i_18][i_AH].piece, my_itoa(wq_cnt++));
			assert(wq_cnt<=8);
			break;
		case WK:
			strcpy((char *)start_board[i_18][i_AH].piece, "WK");
			break;

		case BP:
			strcpy((char *)start_board[i_18][i_AH].piece, "BP");
			strcat((char *)start_board[i_18][i_AH].piece, my_itoa(bp_cnt++));
			assert(bp_cnt<=9);
			break;
		case BR:
			strcpy((char *)start_board[i_18][i_AH].piece, "BR");
			strcat((char *)start_board[i_18][i_AH].piece, my_itoa(br_cnt++));
			assert(br_cnt<=3);
			break;
		case BN:
			strcpy((char *)start_board[i_18][i_AH].piece, "BN");
			strcat((char *)start_board[i_18][i_AH].piece, my_itoa(bn_cnt++));
			break;
		case BB:
			strcpy((char *)start_board[i_18][i_AH].piece, "BB");
			strcat((char *)start_board[i_18][i_AH].piece, my_itoa(bb_cnt++));
			assert(bb_cnt<=3);
			break;
		case BQ:
			strcpy((char *)start_board[i_18][i_AH].piece, "BQ");
			strcat((char *)start_board[i_18][i_AH].piece, my_itoa(bq_cnt++));
			assert(bq_cnt<=8);
			break;
		case BK:
			strcpy((char *)start_board[i_18][i_AH].piece, "BK");
			break;

		case EMPTY:
			strcpy((char *)start_board[i_18][i_AH].piece, "EMP");


	}// end switch
} // end for


}


static void set_array_from_current_board(UINT8* hboard, BOARD_FIELD cboard[8][8])
{

int board_index;

char piece[3];

int i_AH;
int i_18;


for (board_index =0; board_index<64;board_index++)
{
	hboard[board_index] = EMPTY;

	i_AH = flip[board_index] % 8;
	i_18 = flip[board_index] / 8;

	if ( !is_piece_set((char *)m_board[i_18][i_AH].piece) )	//only active pieces
		continue;

	piece[0]=m_board[i_18][i_AH].piece[0];
	piece[1]=m_board[i_18][i_AH].piece[1];
	piece[2]=0;

	if (!strcmp (piece, "WP"))
		hboard[board_index] = WP;
	else if (!strcmp (piece, "WR"))
		hboard[board_index] = WR;
	else if (!strcmp (piece, "WN"))
		hboard[board_index] = WN;
	else if (!strcmp (piece, "WB"))
		hboard[board_index] = WB;
	else if (!strcmp (piece, "WQ"))
		hboard[board_index] = WQ;
	else if (!strcmp (piece, "WK"))
		hboard[board_index] = WK;

	else if (!strcmp (piece, "BP"))
		hboard[board_index] = BP;
	else if (!strcmp (piece, "BR"))
		hboard[board_index] = BR;
	else if (!strcmp (piece, "BN"))
		hboard[board_index] = BN;
	else if (!strcmp (piece, "BB"))
		hboard[board_index] = BB;
	else if (!strcmp (piece, "BQ"))
		hboard[board_index] = BQ;
	else if (!strcmp (piece, "BK"))
		hboard[board_index] = BK;

} // end for


}


static char * my_itoa(int cnt)
{
	
switch (cnt)
{
	case 1: return (char *) "1";
	case 2: return (char *) "2";
	case 3: return (char *) "3";
	case 4: return (char *) "4";
	case 5: return (char *) "5";
	case 6: return (char *) "6";
    case 7: return (char *) "7"; 
    case 8: return (char *) "8"; 
    case 9: return (char *) "9"; 
	default: return (char *) "0"; 
}

}
