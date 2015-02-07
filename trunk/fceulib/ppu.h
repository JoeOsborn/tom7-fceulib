#ifndef __PPU_H
#define __PPU_H

#include "state.h"

void FCEUPPU_Init(void);
void FCEUPPU_Reset(void);
void FCEUPPU_Power(void);
int FCEUPPU_Loop(int skip);

void FCEUPPU_LineUpdate();
void FCEUPPU_SetVideoSystem(int w);

extern void (*PPU_hook)(uint32 A);
extern void (*GameHBIRQHook)(void), (*GameHBIRQHook2)(void);

/* For cart.c and banksw.h, mostly */
extern uint8 NTARAM[0x800],*vnapage[4];
extern uint8 PPUNTARAM;
extern uint8 PPUCHRRAM;

void FCEUPPU_SaveState(void);
void FCEUPPU_LoadState(int version);
uint8* FCEUPPU_GetCHR(uint32 vadr, uint32 refreshaddr);

// 0 to keep 8-sprites limitation, 1 to remove it
void FCEUI_DisableSpriteLimitation(int a);

// void PPU_ResetHooks();
// extern uint8 (*FFCEUX_PPURead)(uint32 A);
// extern void (*FFCEUX_PPUWrite)(uint32 A, uint8 V);
// extern uint8 FFCEUX_PPURead_Default(uint32 A);

extern int scanline;
extern int g_rasterpos;
extern uint8 PPU[4];

extern const SFORMAT FCEUPPU_STATEINFO[];
extern const SFORMAT FCEU_NEWPPU_STATEINFO[];

#endif