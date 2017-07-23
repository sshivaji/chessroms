/******************************************************************************

    tiny.c

    messdriv.c substitute file for "tiny" MESS builds.

    The list of used drivers. Drivers have to be included here to be recognized
    by the executable.

    To save some typing, we use a hack here. This file is recursively #included
    twice, with different definitions of the DRIVER() macro. The first one
    declares external references to the drivers; the second one builds an array
    storing all the drivers.

******************************************************************************/

#include "emu.h"

#ifndef DRIVER_RECURSIVE

#define DRIVER_RECURSIVE

/* step 1: declare all external references */
#define DRIVER(NAME) extern const game_driver driver_##NAME;
#include "tiny.c"

/* step 2: define the drivers[] array */
#undef DRIVER
#define DRIVER(NAME) &driver_##NAME,
const game_driver * const drivers[] =
{
#include "tiny.c"
	0	/* end of array */
};

#else	/* DRIVER_RECURSIVE */

//	DRIVER( mm2 )		/* Mephisto MM2										*/ 
	DRIVER( mm4 )		/* Mephisto 4										*/ 
	DRIVER( mm5 )		/* Mephisto 5.1 ROM									*/ 
	DRIVER( mm50 )		/* Mephisto 5.0 ROM									*/
	DRIVER( rebel5 )		/* Mephisto 5									*/
	DRIVER( glasgow )		/* Glasgow										*/ 
	DRIVER( dallas )		/* Dallas										*/ 
	DRIVER( dallas16 )		/* Dallas 16 bit								*/ 
	DRIVER( dallas32 )		/* Dallas 32 bit								*/
	DRIVER( roma32 )		/* Roma 32 bit	     							*/
	DRIVER( amsterd )		/* Amsterdam	     							*/


#endif	/* DRIVER_RECURSIVE */
