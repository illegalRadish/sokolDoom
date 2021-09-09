#include "doomgeneric.h"

// SOKOL CHANGE
//uint32_t* DG_ScreenBuffer = 0;


void dg_Create()
{
    // SOKOL CHANGE
	//DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);

	DG_Init();
}

