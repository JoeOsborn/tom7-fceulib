#include "cursor.h"

static constexpr uint8 GunSight[] = {
  0,0,0,0,0,0,1,0,0,0,0,0,0,
  0,0,0,0,0,0,2,0,0,0,0,0,0,
  0,0,0,0,0,0,1,0,0,0,0,0,0,
  0,0,0,0,0,0,2,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,3,0,0,0,0,0,0,
  1,2,1,2,0,3,3,3,0,2,1,2,1,
  0,0,0,0,0,0,3,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,2,0,0,0,0,0,0,
  0,0,0,0,0,0,1,0,0,0,0,0,0,
  0,0,0,0,0,0,2,0,0,0,0,0,0,
  0,0,0,0,0,0,1,0,0,0,0,0,0,
};

static constexpr uint8 FCEUcursor[11*19] = {
 1,0,0,0,0,0,0,0,0,0,0,
 1,1,0,0,0,0,0,0,0,0,0,
 1,2,1,0,0,0,0,0,0,0,0,
 1,2,2,1,0,0,0,0,0,0,0,
 1,2,2,2,1,0,0,0,0,0,0,
 1,2,2,2,2,1,0,0,0,0,0,
 1,2,2,2,2,2,1,0,0,0,0,
 1,2,2,2,2,2,2,1,0,0,0,
 1,2,2,2,2,2,2,2,1,0,0,
 1,2,2,2,2,2,2,2,2,1,0,
 1,2,2,2,2,2,1,1,1,1,1,
 1,2,2,1,2,2,1,0,0,0,0,
 1,2,1,0,1,2,2,1,0,0,0,
 1,1,0,0,1,2,2,1,0,0,0,
 1,0,0,0,0,1,2,2,1,0,0,
 0,0,0,0,0,1,2,2,1,0,0,
 0,0,0,0,0,0,1,2,2,1,0,
 0,0,0,0,0,0,1,2,2,1,0,
 0,0,0,0,0,0,0,1,1,0,0,
};

void FCEU_DrawGunSight(uint8 *buf, int xc, int yc) {
  for (int y = 0; y < 13; y++) {
    for (int x = 0; x < 13; x++) {
      uint8 a = GunSight[y * 13 + x];
      if (a) {
        int c = (yc + y - 6);
        int d = (xc + x - 6);
        if (c >= 0 && d >= 0 && d < 256 && c < 240) {
          if (a == 3)
            buf[c * 256 + d] = 0xBF - (buf[c * 256 + d] & 0x3F);
          else
            buf[c * 256 + d] = a - 1;
        }
      }
    }
  }
}

void FCEU_DrawCursor(uint8 *buf, int xc, int yc) {
  if (xc < 256 && yc < 240) {
    for (int y = 0; y < 19; y++) {
      for (int x = 0; x < 11; x++) {
        uint8 a = FCEUcursor[y * 11 + x];
        if (a) {
          int c = (yc + y);
          int d = (xc + x);
          if (d < 256 && c < 240) {
            buf[c * 256 + d] = a + 127;
          }
        }
      }
    }
  }
}
