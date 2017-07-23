static UINT8 lcd_shift_counter;
static UINT8 led7;

  #define BOARD_VIEW	1
  #define MODULE_VIEW	2

  #define MOUSE_X		56
  #define MOUSE_Y		56

  #define MAX_X			530
  #define MAX_Y			660

  #define MOUSE_SPEED	3

  #define MIN_BOARD_X	60.0
  #define MAX_BOARD_X	480.0

  #define MIN_BOARD_Y	60.0
  #define MAX_BOARD_Y	480.0

  #define MIN_CURSOR_X	-10
  #define MAX_CURSOR_X	496

  #define MIN_CURSOR_Y	0
  #define MAX_CURSOR_Y	622

  static UINT16 Line18_LED;
  static UINT16 Line18_REED;

  static const char EMP[4] = "EMP";
  static const char NO_PIECE[4]  = "NOP";


  typedef struct {
       unsigned int field;
	   unsigned int x;
	   unsigned int y;
       unsigned char piece[4];
  } BOARD_FIELD;

	#define WP	11  
	#define WR	12
	#define WB	13
	#define WN	14
	#define WQ	15
	#define WK	16

	#define BP	21
	#define BR	22
	#define BB	23
	#define BN	24
	#define BQ	25
	#define BK	26

    #define EMPTY	0 


static UINT8 save_board[64];					//for save state

static BOARD_FIELD m_board[8][8];		        //current board
static BOARD_FIELD start_board[8][8];
static BOARD_FIELD start_pos[8][8] =
	{
		{ { 7,44,434,"WR1"}, { 6,100,434,"WN1"}, { 5,156,434,"WB1"}, { 4,212,434,"WQ1"}, { 3,268,434,"WK"}, { 2,324,434,"WB2"}, { 1,380,434,"WN2"}, { 0,436,434,"WR2"} },
		{ {15,44,378,"WP1"}, {14,100,378,"WP2"}, {13,156,378,"WP3"}, {12,212,378,"WP4"}, {11,268,378,"WP5"}, {10,324,378,"WP6"}, { 9,380,378,"WP7"}, { 8,436,378,"WP8"} },

		{ {23,44,322, "EMP"}, {22,100,322, "EMP"}, {21,156,322, "EMP"}, {20,212,322, "EMP"}, {19,268,322, "EMP"}, {18,324,322, "EMP"}, {17,380,322, "EMP"}, {16,436,322, "EMP"} },
		{ {31,44,266, "EMP"}, {30,100,266, "EMP"}, {29,156,266, "EMP"}, {28,212,266, "EMP"}, {27,268,266, "EMP"}, {26,324,266, "EMP"}, {25,380,266, "EMP"}, {24,436,266, "EMP"} },
		{ {39,44,210, "EMP"}, {38,100,210, "EMP"}, {37,156,210, "EMP"}, {36,212,210, "EMP"}, {35,268,210, "EMP"}, {34,324,210, "EMP"}, {33,380,210, "EMP"}, {32,436,210, "EMP"} },
		{ {47,44,154, "EMP"}, {46,100,154, "EMP"}, {45,156,154, "EMP"}, {44,212,154, "EMP"}, {43,268,154, "EMP"}, {42,324,154, "EMP"}, {41,380,154, "EMP"}, {40,436,154, "EMP"} },

		{ {55,44,100,"BP1"}, {54,100,100,"BP2"}, {53,156,100,"BP3"}, {52,212,100,"BP4"}, {51,268,100,"BP5"}, {50,324,100,"BP6"}, {49,380,100,"BP7"}, {48,436,100,"BP8"} },
		{ {63,44,44 ,"BR1"}, {62,100,44 ,"BN1"}, {61,156,44 ,"BB1"}, {60,212,44 ,"BQ1"}, {59,268,44 ,"BK"}, {58,324,44 ,"BB2"}, {57,380,44 ,"BN2"}, {56,436,44 ,"BR2"} }

	};


     typedef struct {
       unsigned char piece[4];
	   unsigned int selected;
	   unsigned int set;
  } P_STATUS;


	 static P_STATUS all_pieces[48] =
	 {
		 {"WR1",0,0}, {"WN1",0,0},{"WB1",0,0}, {"WQ1",0,0},  {"WK",0,0},  {"WB2",0,0}, {"WN2",0,0}, {"WR2",0,0},
		 {"WP1",0,0}, {"WP2",0,0},{"WP3",0,0}, {"WP4",0,0}, {"WP5",0,0},  {"WP6",0,0}, {"WP7",0,0}, {"WP8",0,0},


		{"BP1",0,0}, {"BP2",0,0},{"BP3",0,0}, {"BP4",0,0}, {"BP5",0,0},  {"BP6",0,0}, {"BP7",0,0}, {"BP8",0,0},
	    {"BR1",0,0}, {"BN1",0,0},{"BB1",0,0}, {"BQ1",0,0},  {"BK",0,0},  {"BB2",0,0}, {"BN2",0,0}, {"BR2",0,0},


        {"BQ2",0,0}, {"BQ3",0,0}, {"BQ4",0,0}, {"BQ5",0,0}, {"BQ6",0,0}, {"BQ7",0,0},


		{"WQ2",0,0}, {"WQ3",0,0}, {"WQ4",0,0}, {"WQ5",0,0}, {"WQ6",0,0}, {"WQ7",0,0}

	 };

     static UINT8 start_i;


static UINT8 flip[64] =
{
	56, 57, 58, 59, 60, 61, 62, 63,
	48, 49, 50, 51, 52, 53, 54, 55,
	40, 41, 42, 43, 44, 45, 46, 47,
	32, 33, 34, 35, 36, 37, 38, 39,
	24, 25, 26, 27, 28, 29, 30, 31,
	16, 17, 18, 19, 20, 21, 22, 23,
	 8,  9, 10, 11, 12, 13, 14, 15,
	 0,  1,  2,  3,  4,  5,  6,  7
};

#define NUM_PRIMLISTS			3
struct _render_target
{
	render_target *		next;				/* keep a linked list of targets */
	running_machine *	machine;			/* pointer to the machine we are connected with */
	layout_view *		curview;			/* current view */
	layout_file *		filelist;			/* list of layout files */
	UINT32				flags;				/* creation flags */
	render_primitive_list primlist[NUM_PRIMLISTS];/* list of primitives */
	int					listindex;			/* index of next primlist to use */
	INT32				width;				/* width in pixels */
	INT32				height;				/* height in pixels */
	render_bounds		bounds;				/* bounds of the target */
	float				pixel_aspect;		/* aspect ratio of individual pixels */
	float				max_refresh;		/* maximum refresh rate, 0 or if none */
	int					orientation;		/* orientation */
	int					layerconfig;		/* layer configuration */
	layout_view *		base_view;			/* the view at the time of first frame */
	int					base_orientation;	/* the orientation at the time of first frame */
	int					base_layerconfig;	/* the layer configuration at the time of first frame */
	int					maxtexwidth;		/* maximum width of a texture */
	int					maxtexheight;		/* maximum height of a texture */
};

  static running_machine *dummy_machine;

  static UINT8 artwork_view;

  static void set_cursor (running_machine *machine, view_item *view_cursor);

  static view_item *get_view_item(render_target *target, const char *v_name);

  static BOARD_FIELD  get_field( float i_x, float i_y, UINT8 mouse_move);
  static void update_board(render_target *target, BOARD_FIELD board_field, unsigned int update_clear_flag);

  static void calculate_bounds(view_item *view_item, float new_x0, float new_y0, float new_del_x, float new_del_y );

  static void set_render_board(void);
  static void clear_layout(void);
  static void set_status_of_pieces(void);

  static const char * get_non_set_pieces(const char *cur_piece);

  static unsigned int out_of_board( float x0, float y0);

  static void video_update(running_machine *machine, UINT8 reset);
  static UINT16 get_board(BOARD_FIELD board[8][8],UINT16 *p_Line18_REED);
  static void set_board(BOARD_FIELD board[8][8], UINT16 *p_Line18_LED, UINT8 set_data);

  static UINT8 get_artwork_view(void);

  static void set_startboard_from_startpos(void);
  static void set_startboard_from_array(UINT8* hboard);
  static void set_array_from_current_board(UINT8* hboard, BOARD_FIELD cboard[8][8]);

  static int is_piece_set(const char *cur_piece);

  static char * my_itoa(int cnt);


