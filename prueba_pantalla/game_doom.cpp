#include "game_doom.h"
#include "board_config.h"
#include "display_driver.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

namespace {

constexpr int DOOM_W = 160; // Render 160x90 escalado 2x -> 320x180 fluido
constexpr int DOOM_H = 90;
constexpr int DOOM_VIEW_Y = 22;
constexpr int DOOM_MAP_W = 16;
constexpr int DOOM_MAP_H = 16;
constexpr int DOOM_MAX_ENEMIES = 6;
constexpr int DOOM_MAX_ITEMS   = 4;

// Mapa Nivel 1 (E1M1 Hangar): 1=Metal tech, 2=Ladrillo/Piedra, 3=Pilar alerta, 4=Puerta EXIT
const uint8_t kDoomMap[DOOM_MAP_H][DOOM_MAP_W] = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,1,0,0,0,0,0,2,0,4,1},
  {1,0,3,0,0,0,1,0,0,3,0,0,2,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,2,0,0,1},
  {1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1},
  {1,1,0,1,1,1,1,2,2,0,2,2,2,2,1,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,2,2,0,0,0,3,0,0,3,0,0,2,0,1},
  {1,0,2,0,0,0,0,0,0,0,0,0,0,2,0,1},
  {1,0,0,0,0,0,0,3,0,0,3,0,0,0,0,1},
  {1,1,1,0,1,1,0,0,0,0,0,0,1,1,1,1},
  {1,0,0,0,0,1,0,0,0,0,0,0,1,0,0,1},
  {1,0,3,0,0,0,0,0,2,0,0,0,0,0,0,1},
  {1,0,0,0,0,1,0,0,2,0,0,0,1,0,0,1},
  {1,0,0,0,0,1,0,0,0,0,0,0,1,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

struct DoomEnemy {
  float x, y;
  int8_t hp;
  bool alive;
  uint8_t hurtFrames;
  uint32_t lastAtkMs;
};

struct DoomItem {
  float x, y;
  uint8_t type; // 1 = Medikit (+25 HP), 2 = Balas (+8)
  bool active;
};

uint16_t g_fb[DOOM_W * DOOM_H];
uint16_t g_rowBuf[SCREEN_W * 2]; // Dos filas de 320 px para blit 2x sin doble swap
float g_zBuf[DOOM_W];

float g_pX = 2.5f, g_pY = 2.5f;
float g_pDirX = 1.0f, g_pDirY = 0.0f;
float g_pPlaneX = 0.0f, g_pPlaneY = 0.66f;
int g_hp = 100;
int g_ammo = 24;
int g_kills = 0;
uint8_t g_gunRecoil = 0;
uint8_t g_hurtFlash = 0;
uint32_t g_lastShotMs = 0;
uint32_t g_lastFrameMs = 0;
uint32_t g_startMs = 0;
bool g_dead = false;
bool g_win = false;
bool g_showMap = true;
bool g_prevFireHigh = true;
DoomEnemy g_enemies[DOOM_MAX_ENEMIES];
DoomItem g_items[DOOM_MAX_ITEMS];

inline uint16_t scaleRgb565(uint16_t c, uint8_t brightness) {
  uint16_t r = (((c >> 11) & 0x1F) * brightness) >> 8;
  uint16_t g = (((c >> 5)  & 0x3F) * brightness) >> 8;
  uint16_t b = ((c & 0x1F)         * brightness) >> 8;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

uint16_t sampleWallTex(uint8_t tile, int tx, int ty, bool darkSide, float dist) {
  uint16_t base = g_theme.mid;
  if (tile == 1) {
    bool seam = (tx == 0 || ty == 0 || ty == 8);
    bool rivet = ((tx == 2 || tx == 13) && (ty == 2 || ty == 13));
    base = seam ? g_theme.dark : (rivet ? g_theme.glow : g_theme.mid);
  } else if (tile == 2) {
    int bx = (tx + ((ty >> 2) & 1) * 4) & 7;
    bool mortar = ((ty & 3) == 0 || bx == 0);
    base = mortar ? g_theme.dark : g_theme.dim;
  } else if (tile == 3) {
    bool stripe = (((tx + ty) & 7) < 3) && (ty >= 4 && ty <= 11);
    base = stripe ? g_theme.glow : g_theme.dim;
  } else {
    bool frame = (tx <= 1 || tx >= 14 || ty <= 1 || ty >= 14);
    bool glowCenter = (tx >= 5 && tx <= 10 && ty >= 5 && ty <= 10);
    base = frame ? g_theme.bright : (glowCenter ? 0x07E0 : g_theme.dark);
  }

  float fog = 1.0f - (dist * 0.085f);
  if (fog < 0.18f) fog = 0.18f;
  if (fog > 1.0f) fog = 1.0f;
  if (darkSide) fog *= 0.68f;
  return scaleRgb565(base, static_cast<uint8_t>(fog * 255.0f));
}

uint8_t impPixel(int x, int y) {
  static const char* kImp[16] = {
    "....3......3....",
    "...33......33...",
    "....11111111....",
    "...1111111111...",
    "...1133113311...",
    "...1111111111...",
    "....11222211....",
    "..111111111111..",
    ".11211111111211.",
    ".1..11222211..1.",
    "....11111111....",
    "....112..211....",
    "...111....111...",
    "...112....211...",
    "..222......222..",
    "................"
  };
  char ch = kImp[y & 15][x & 15];
  if (ch == '1') return 1;
  if (ch == '2') return 2;
  if (ch == '3') return 3;
  return 0;
}

uint8_t itemPixel(int x, int y, uint8_t type) {
  if (x < 3 || x > 12 || y < 5 || y > 13) return 0;
  if (x == 3 || x == 12 || y == 5 || y == 13) return 2;
  if (type == 1) {
    if ((x >= 7 && x <= 8 && y >= 7 && y <= 11) || (y == 9 && x >= 5 && x <= 10)) return 3;
    return 1;
  }
  if (y == 8 || y == 11) return 3;
  return 1;
}

void drawHud() {
  g_tft.fillRect(0, 0, SCREEN_W, 22, g_theme.bg);
  g_tft.drawFastHLine(0, 21, SCREEN_W, g_theme.dim);
  g_tft.setCursor(6, 7);
  printEs("DOOM // E1M1 HANGAR", g_theme.bright, 1);

  char topBuf[32];
  snprintf(topBuf, sizeof(topBuf), "DEMONS:%d/%d", g_kills, DOOM_MAX_ENEMIES);
  g_tft.setCursor(166, 7);
  printEs(topBuf, g_theme.glow, 1);
  g_tft.setCursor(262, 7);
  printEs("[B]Salir", g_theme.dim, 1);

  g_tft.fillRect(0, 202, SCREEN_W, 38, g_theme.bg);
  g_tft.drawFastHLine(0, 202, SCREEN_W, g_theme.bright);

  const uint16_t hpCol = (g_hp <= 30) ? 0xF800 : g_theme.glow;
  char hudBuf[48];
  snprintf(hudBuf, sizeof(hudBuf), "HP:%3d%%", g_hp);
  g_tft.setCursor(8, 208);
  printEs(hudBuf, hpCol, 2);

  snprintf(hudBuf, sizeof(hudBuf), "BALAS:%2d", g_ammo);
  g_tft.setCursor(116, 208);
  printEs(hudBuf, g_ammo == 0 ? 0xF800 : g_theme.bright, 2);

  g_tft.setCursor(236, 207);
  printEs("E1M1", g_theme.mid, 1);
  g_tft.setCursor(236, 217);
  printEs("[A]Dispara", g_theme.dim, 1);

  g_tft.setCursor(6, 229);
  printEs("[↑↓←→]Mover [A]Fuego [ST]Mapa [B]Menú", g_theme.dim, 1);
}

void drawEndOverlay(bool victory) {
  constexpr int16_t bx = 42, by = 60, bw = 236, bh = 108;
  g_tft.fillRect(bx, by, bw, bh, g_theme.bg);
  g_tft.drawRect(bx, by, bw, bh, victory ? g_theme.glow : 0xF800);
  g_tft.fillRect(bx + 2, by + 2, bw - 4, 24, victory ? g_theme.selBg : 0x4000);

  g_tft.setCursor(bx + (victory ? 20 : 54), by + 7);
  printEs(victory ? "NIVEL 1 LISTO!" : "HAS MUERTO", victory ? g_theme.glow : 0xF800, 2);

  const uint32_t sec = (millis() - g_startMs) / 1000;
  char buf[40];
  snprintf(buf, sizeof(buf), "Bajas: %d/%d   Tiempo: %lus",
           g_kills, DOOM_MAX_ENEMIES, static_cast<unsigned long>(sec));
  g_tft.setCursor(bx + 24, by + 40);
  printEs(buf, g_theme.bright, 1);

  g_tft.setCursor(bx + 36, by + 64);
  printEs("[A] Jugar E1M1 otra vez", g_theme.glow, 1);
  g_tft.setCursor(bx + 36, by + 82);
  printEs("[B] Volver al menú", g_theme.mid, 1);
}

void fireShotgun() {
  const uint32_t now = millis();
  if (g_ammo <= 0 || (now - g_lastShotMs) < 260) return;
  g_lastShotMs = now;
  g_ammo--;
  g_gunRecoil = 5;

  int bestIdx = -1;
  float bestDist = 999.0f;
  for (int i = 0; i < DOOM_MAX_ENEMIES; ++i) {
    if (!g_enemies[i].alive) continue;
    const float dx = g_enemies[i].x - g_pX;
    const float dy = g_enemies[i].y - g_pY;
    const float fwd = dx * g_pDirX + dy * g_pDirY;
    if (fwd <= 0.25f || fwd > 9.0f) continue;
    const float side = fabsf(dx * (-g_pDirY) + dy * g_pDirX);
    if (side < 0.45f && fwd < bestDist) {
      if (fwd < g_zBuf[DOOM_W / 2] + 0.15f) {
        bestDist = fwd;
        bestIdx = i;
      }
    }
  }

  if (bestIdx >= 0) {
    g_enemies[bestIdx].hp--;
    g_enemies[bestIdx].hurtFrames = 4;
    const float pushX = g_enemies[bestIdx].x + g_pDirX * 0.22f;
    const float pushY = g_enemies[bestIdx].y + g_pDirY * 0.22f;
    if (kDoomMap[static_cast<int>(pushY)][static_cast<int>(pushX)] == 0) {
      g_enemies[bestIdx].x = pushX;
      g_enemies[bestIdx].y = pushY;
    }
    if (g_enemies[bestIdx].hp <= 0) {
      g_enemies[bestIdx].alive = false;
      g_kills++;
      Serial.printf("[DOOM] Demonio eliminado! (%d/%d)\n", g_kills, DOOM_MAX_ENEMIES);
    }
  }
  drawHud();
}

void drawSprite(float sx, float sy, bool isEnemy, uint8_t param, bool hurtFlash) {
  const float relX = sx - g_pX;
  const float relY = sy - g_pY;
  const float invDet = 1.0f / (g_pPlaneX * g_pDirY - g_pDirX * g_pPlaneY);
  const float transX = invDet * (g_pDirY * relX - g_pDirX * relY);
  const float transY = invDet * (-g_pPlaneY * relX + g_pPlaneX * relY);
  if (transY <= 0.2f) return;

  const int screenX = static_cast<int>((DOOM_W / 2.0f) * (1.0f + transX / transY));
  int spriteH = abs(static_cast<int>(DOOM_H / transY));
  if (spriteH > DOOM_H * 2) spriteH = DOOM_H * 2;
  int spriteW = spriteH;

  int drawStartY = -spriteH / 2 + DOOM_H / 2;
  int drawEndY   =  spriteH / 2 + DOOM_H / 2;
  if (drawStartY < 0) drawStartY = 0;
  if (drawEndY >= DOOM_H) drawEndY = DOOM_H - 1;

  int drawStartX = -spriteW / 2 + screenX;
  int drawEndX   =  spriteW / 2 + screenX;
  if (drawStartX < 0) drawStartX = 0;
  if (drawEndX >= DOOM_W) drawEndX = DOOM_W - 1;

  float fog = 1.0f - (transY * 0.08f);
  if (fog < 0.25f) fog = 0.25f;
  if (fog > 1.0f) fog = 1.0f;
  const uint8_t br = static_cast<uint8_t>(fog * 255.0f);

  for (int stripe = drawStartX; stripe <= drawEndX; ++stripe) {
    if (transY >= g_zBuf[stripe]) continue;
    const int texX = ((stripe - (-spriteW / 2 + screenX)) * 16) / spriteW;
    if (texX < 0 || texX >= 16) continue;

    for (int y = drawStartY; y <= drawEndY; ++y) {
      const int d = y * 256 - DOOM_H * 128 + spriteH * 128;
      const int texY = ((d * 16) / spriteH) / 256;
      if (texY < 0 || texY >= 16) continue;

      const uint8_t pix = isEnemy ? impPixel(texX, texY) : itemPixel(texX, texY, param);
      if (pix == 0) continue;

      uint16_t col = 0;
      if (isEnemy) {
        if (hurtFlash) col = 0xFFFF;
        else if (pix == 3) col = 0xFFE0;
        else if (pix == 2) col = scaleRgb565(0x7800, br);
        else col = scaleRgb565(0xD180, br);
      } else {
        if (param == 1) col = (pix == 3) ? 0xF800 : ((pix == 2) ? g_theme.dim : 0xFFFF);
        else col = (pix == 3) ? 0xFFE0 : ((pix == 2) ? g_theme.dark : g_theme.bright);
      }
      g_fb[y * DOOM_W + stripe] = col;
    }
  }
}

void drawWeaponAndOverlay() {
  const int cx = DOOM_W / 2;
  const int cy = DOOM_H / 2;
  for (int i = -3; i <= 3; ++i) {
    g_fb[cy * DOOM_W + (cx + i)] = g_theme.glow;
    g_fb[(cy + i) * DOOM_W + cx] = g_theme.glow;
  }

  const int kick = (g_gunRecoil > 0) ? (g_gunRecoil * 2) : 0;
  const int gy = DOOM_H - 24 + kick;

  if (g_gunRecoil >= 4) {
    for (int y = gy - 10; y < gy; ++y) {
      for (int x = cx - 8; x <= cx + 8; ++x) {
        if (y >= 0 && y < DOOM_H && ((x + y) & 1)) {
          g_fb[y * DOOM_W + x] = 0xFFE0;
        }
      }
    }
  }

  for (int y = gy; y < DOOM_H; ++y) {
    const int ly = y - gy;
    const int halfW = (ly < 10) ? 5 : 11;
    for (int x = cx - halfW; x <= cx + halfW; ++x) {
      if (y < 0 || y >= DOOM_H || x < 0 || x >= DOOM_W) continue;
      uint16_t c = g_theme.dim;
      if (ly < 10) {
        c = (x == cx) ? g_theme.bg : ((x == cx - 2 || x == cx + 2) ? g_theme.bright : g_theme.mid);
      } else {
        c = (ly == 10 || x == cx - halfW || x == cx + halfW) ? g_theme.bright : g_theme.dark;
      }
      g_fb[y * DOOM_W + x] = c;
    }
  }

  if (g_showMap) {
    const int mx0 = DOOM_W - (DOOM_MAP_W * 2) - 3;
    const int my0 = 2;
    for (int my = 0; my < DOOM_MAP_H; ++my) {
      for (int mx = 0; mx < DOOM_MAP_W; ++mx) {
        const uint8_t tile = kDoomMap[my][mx];
        const uint16_t c = (tile == 4) ? 0x07E0 : (tile ? g_theme.dim : g_theme.bg);
        for (int dy = 0; dy < 2; ++dy) {
          for (int dx = 0; dx < 2; ++dx) {
            g_fb[(my0 + my * 2 + dy) * DOOM_W + (mx0 + mx * 2 + dx)] = c;
          }
        }
      }
    }
    for (int i = 0; i < DOOM_MAX_ENEMIES; ++i) {
      if (!g_enemies[i].alive) continue;
      int ex = mx0 + static_cast<int>(g_enemies[i].x * 2);
      int ey = my0 + static_cast<int>(g_enemies[i].y * 2);
      if (ex >= 0 && ex < DOOM_W && ey >= 0 && ey < DOOM_H) g_fb[ey * DOOM_W + ex] = 0xF800;
    }
    int px = mx0 + static_cast<int>(g_pX * 2);
    int py = my0 + static_cast<int>(g_pY * 2);
    if (px >= 0 && px < DOOM_W && py >= 0 && py < DOOM_H) g_fb[py * DOOM_W + px] = 0xFFFF;
  }

  if (g_hurtFlash > 0) {
    for (int x = 0; x < DOOM_W; ++x) {
      g_fb[x] = 0xF800;
      g_fb[(DOOM_H - 1) * DOOM_W + x] = 0xF800;
    }
    for (int y = 0; y < DOOM_H; ++y) {
      g_fb[y * DOOM_W] = 0xF800;
      g_fb[y * DOOM_W + (DOOM_W - 1)] = 0xF800;
    }
  }
}

void updateAndRenderFrame() {
  const uint32_t now = millis();
  float dt = (now - g_lastFrameMs) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  g_lastFrameMs = now;

  const float moveSpd = 3.2f * dt;
  const float rotSpd  = 2.3f * dt;

  if (digitalRead(BTN_UP) == LOW) {
    const float nx = g_pX + g_pDirX * moveSpd;
    const float ny = g_pY + g_pDirY * moveSpd;
    if (kDoomMap[static_cast<int>(g_pY)][static_cast<int>(nx)] == 0) g_pX = nx;
    if (kDoomMap[static_cast<int>(ny)][static_cast<int>(g_pX)] == 0) g_pY = ny;

    const int fx = static_cast<int>(g_pX + g_pDirX * 0.45f);
    const int fy = static_cast<int>(g_pY + g_pDirY * 0.45f);
    if (fx >= 0 && fx < DOOM_MAP_W && fy >= 0 && fy < DOOM_MAP_H && kDoomMap[fy][fx] == 4) {
      g_win = true;
      drawEndOverlay(true);
      Serial.printf("[DOOM] NIVEL 1 COMPLETADO! Bajas=%d/%d\n", g_kills, DOOM_MAX_ENEMIES);
      return;
    }
  }
  if (digitalRead(BTN_DOWN) == LOW) {
    const float nx = g_pX - g_pDirX * moveSpd;
    const float ny = g_pY - g_pDirY * moveSpd;
    if (kDoomMap[static_cast<int>(g_pY)][static_cast<int>(nx)] == 0) g_pX = nx;
    if (kDoomMap[static_cast<int>(ny)][static_cast<int>(g_pX)] == 0) g_pY = ny;
  }
  if (digitalRead(BTN_LEFT) == LOW) {
    const float oldDirX = g_pDirX;
    const float oldPlX = g_pPlaneX;
    const float c = cosf(-rotSpd), s = sinf(-rotSpd);
    g_pDirX   = g_pDirX * c - g_pDirY * s;
    g_pDirY   = oldDirX * s + g_pDirY * c;
    g_pPlaneX = g_pPlaneX * c - g_pPlaneY * s;
    g_pPlaneY = oldPlX * s + g_pPlaneY * c;
  }
  if (digitalRead(BTN_RIGHT) == LOW) {
    const float oldDirX = g_pDirX;
    const float oldPlX = g_pPlaneX;
    const float c = cosf(rotSpd), s = sinf(rotSpd);
    g_pDirX   = g_pDirX * c - g_pDirY * s;
    g_pDirY   = oldDirX * s + g_pDirY * c;
    g_pPlaneX = g_pPlaneX * c - g_pPlaneY * s;
    g_pPlaneY = oldPlX * s + g_pPlaneY * c;
  }

  const bool fireHigh = (digitalRead(BTN_A) == HIGH);
  if (g_prevFireHigh && !fireHigh) {
    fireShotgun();
  }
  g_prevFireHigh = fireHigh;

  bool hudDirty = false;
  for (int i = 0; i < DOOM_MAX_ITEMS; ++i) {
    if (!g_items[i].active) continue;
    const float dx = g_items[i].x - g_pX;
    const float dy = g_items[i].y - g_pY;
    if ((dx * dx + dy * dy) < 0.45f) {
      if (g_items[i].type == 1 && g_hp < 100) {
        g_hp += 25;
        if (g_hp > 100) g_hp = 100;
        g_items[i].active = false;
        hudDirty = true;
      } else if (g_items[i].type == 2) {
        g_ammo += 8;
        g_items[i].active = false;
        hudDirty = true;
      }
    }
  }

  for (int i = 0; i < DOOM_MAX_ENEMIES; ++i) {
    if (!g_enemies[i].alive) continue;
    if (g_enemies[i].hurtFrames > 0) g_enemies[i].hurtFrames--;
    const float dx = g_pX - g_enemies[i].x;
    const float dy = g_pY - g_enemies[i].y;
    const float distSq = dx * dx + dy * dy;
    if (distSq < 36.0f && distSq > 0.75f) {
      const float invD = 1.0f / sqrtf(distSq);
      const float step = 0.85f * dt;
      const float ex = g_enemies[i].x + dx * invD * step;
      const float ey = g_enemies[i].y + dy * invD * step;
      if (kDoomMap[static_cast<int>(g_enemies[i].y)][static_cast<int>(ex)] == 0) g_enemies[i].x = ex;
      if (kDoomMap[static_cast<int>(ey)][static_cast<int>(g_enemies[i].x)] == 0) g_enemies[i].y = ey;
    } else if (distSq <= 0.85f && (now - g_enemies[i].lastAtkMs) >= 950) {
      g_enemies[i].lastAtkMs = now;
      g_hp -= 12;
      if (g_hp < 0) g_hp = 0;
      g_hurtFlash = 4;
      hudDirty = true;
      if (g_hp == 0) {
        g_dead = true;
        drawHud();
        drawEndOverlay(false);
        Serial.println(F("[DOOM] HAS MUERTO en E1M1."));
        return;
      }
    }
  }

  if (g_gunRecoil > 0) g_gunRecoil--;
  if (g_hurtFlash > 0) g_hurtFlash--;

  // 1. Techo y piso con degradado de profundidad
  for (int y = 0; y < DOOM_H; ++y) {
    uint16_t rowCol = 0x0000;
    if (y < DOOM_H / 2) {
      const uint8_t b = static_cast<uint8_t>((DOOM_H / 2 - y) * 2);
      rowCol = scaleRgb565(g_theme.dark, b);
    } else {
      const uint8_t b = static_cast<uint8_t>((y - DOOM_H / 2) * 3);
      rowCol = scaleRgb565(g_theme.dim, b);
    }
    uint16_t* dst = &g_fb[y * DOOM_W];
    for (int x = 0; x < DOOM_W; ++x) dst[x] = rowCol;
  }

  // 2. Raycasting DDA columna por columna (160 rayos)
  for (int x = 0; x < DOOM_W; ++x) {
    const float camX = 2.0f * x / static_cast<float>(DOOM_W) - 1.0f;
    const float rayDirX = g_pDirX + g_pPlaneX * camX;
    const float rayDirY = g_pDirY + g_pPlaneY * camX;

    int mapX = static_cast<int>(g_pX);
    int mapY = static_cast<int>(g_pY);

    const float deltaDistX = (rayDirX == 0.0f) ? 1e30f : fabsf(1.0f / rayDirX);
    const float deltaDistY = (rayDirY == 0.0f) ? 1e30f : fabsf(1.0f / rayDirY);

    int stepX = 0, stepY = 0;
    float sideDistX = 0.0f, sideDistY = 0.0f;
    if (rayDirX < 0) { stepX = -1; sideDistX = (g_pX - mapX) * deltaDistX; }
    else             { stepX =  1; sideDistX = (mapX + 1.0f - g_pX) * deltaDistX; }
    if (rayDirY < 0) { stepY = -1; sideDistY = (g_pY - mapY) * deltaDistY; }
    else             { stepY =  1; sideDistY = (mapY + 1.0f - g_pY) * deltaDistY; }

    uint8_t hitTile = 0;
    bool side = false;
    for (int guard = 0; guard < 32 && !hitTile; ++guard) {
      if (sideDistX < sideDistY) {
        sideDistX += deltaDistX;
        mapX += stepX;
        side = false;
      } else {
        sideDistY += deltaDistY;
        mapY += stepY;
        side = true;
      }
      if (mapX < 0 || mapX >= DOOM_MAP_W || mapY < 0 || mapY >= DOOM_MAP_H) {
        hitTile = 1;
      } else {
        hitTile = kDoomMap[mapY][mapX];
      }
    }

    float perpDist = side ? (sideDistY - deltaDistY) : (sideDistX - deltaDistX);
    if (perpDist < 0.1f) perpDist = 0.1f;
    g_zBuf[x] = perpDist;

    const int lineH = static_cast<int>(DOOM_H / perpDist);
    int drawStart = -lineH / 2 + DOOM_H / 2;
    int drawEnd   =  lineH / 2 + DOOM_H / 2;
    if (drawStart < 0) drawStart = 0;
    if (drawEnd >= DOOM_H) drawEnd = DOOM_H - 1;

    float wallX = side ? (g_pX + perpDist * rayDirX) : (g_pY + perpDist * rayDirY);
    wallX -= floorf(wallX);
    const int texX = static_cast<int>(wallX * 16.0f) & 15;

    for (int y = drawStart; y <= drawEnd; ++y) {
      const int d = y * 256 - DOOM_H * 128 + lineH * 128;
      const int texY = ((d * 16) / lineH) / 256;
      g_fb[y * DOOM_W + x] = sampleWallTex(hitTile, texX, texY & 15, side, perpDist);
    }
  }

  // 3. Ordenar y dibujar objetos + demonios (de lejos a cerca)
  struct SpriteRef { float dist; bool isEnemy; int idx; };
  SpriteRef list[DOOM_MAX_ENEMIES + DOOM_MAX_ITEMS];
  int count = 0;
  for (int i = 0; i < DOOM_MAX_ITEMS; ++i) {
    if (!g_items[i].active) continue;
    float dx = g_items[i].x - g_pX, dy = g_items[i].y - g_pY;
    list[count++] = { dx * dx + dy * dy, false, i };
  }
  for (int i = 0; i < DOOM_MAX_ENEMIES; ++i) {
    if (!g_enemies[i].alive) continue;
    float dx = g_enemies[i].x - g_pX, dy = g_enemies[i].y - g_pY;
    list[count++] = { dx * dx + dy * dy, true, i };
  }
  for (int i = 0; i < count - 1; ++i) {
    for (int j = i + 1; j < count; ++j) {
      if (list[j].dist > list[i].dist) {
        SpriteRef tmp = list[i]; list[i] = list[j]; list[j] = tmp;
      }
    }
  }
  for (int i = 0; i < count; ++i) {
    if (list[i].isEnemy) {
      const DoomEnemy& e = g_enemies[list[i].idx];
      drawSprite(e.x, e.y, true, 0, e.hurtFrames > 0);
    } else {
      const DoomItem& it = g_items[list[i].idx];
      drawSprite(it.x, it.y, false, it.type, false);
    }
  }

  // 4. Arma, mira y minimapa
  drawWeaponAndOverlay();

  // 5. Blit único escalado 2x (160x90 -> 320x180) enviando 2 filas a la vez (0 parpadeo)
  g_tft.startWrite();
  g_tft.setAddrWindow(0, DOOM_VIEW_Y, SCREEN_W, DOOM_H * 2);
  for (int y = 0; y < DOOM_H; ++y) {
    const uint16_t* src = &g_fb[y * DOOM_W];
    for (int x = 0; x < DOOM_W; ++x) {
      const uint16_t px = src[x];
      const int dx = x * 2;
      g_rowBuf[dx]                = px;
      g_rowBuf[dx + 1]            = px;
      g_rowBuf[SCREEN_W + dx]     = px;
      g_rowBuf[SCREEN_W + dx + 1] = px;
    }
    g_tft.writePixels(g_rowBuf, SCREEN_W * 2);
  }
  g_tft.endWrite();

  if (hudDirty) drawHud();
}

} // namespace

void doomRenderFull() {
  g_tft.fillScreen(g_theme.bg);
  drawHud();
  if (g_dead) drawEndOverlay(false);
  else if (g_win) drawEndOverlay(true);
}

void doomStart() {
  g_pX = 2.5f;
  g_pY = 2.5f;
  g_pDirX = 1.0f;
  g_pDirY = 0.0f;
  g_pPlaneX = 0.0f;
  g_pPlaneY = 0.66f;
  g_hp = 100;
  g_ammo = 24;
  g_kills = 0;
  g_gunRecoil = 0;
  g_hurtFlash = 0;
  g_dead = false;
  g_win = false;
  g_startMs = millis();
  g_lastFrameMs = g_startMs;
  g_prevFireHigh = (digitalRead(BTN_A) == HIGH);

  g_enemies[0] = {  8.5f,  2.5f, 2, true, 0, 0 };
  g_enemies[1] = {  4.5f,  7.5f, 2, true, 0, 0 };
  g_enemies[2] = {  9.5f,  8.5f, 2, true, 0, 0 };
  g_enemies[3] = { 13.5f,  7.5f, 2, true, 0, 0 };
  g_enemies[4] = {  8.5f, 13.5f, 2, true, 0, 0 };
  g_enemies[5] = { 13.5f,  3.5f, 3, true, 0, 0 };

  g_items[0] = {  2.5f, 13.5f, 1, true };
  g_items[1] = { 14.2f, 13.5f, 2, true };
  g_items[2] = {  8.5f,  6.5f, 2, true };
  g_items[3] = { 11.5f,  2.5f, 1, true };

  doomRenderFull();
  Serial.println(F("[DOOM] Nivel E1M1 Hangar iniciado."));
}

bool doomHandleButton(int pin) {
  if (pin == BTN_B) return true;

  if (g_dead || g_win) {
    if (pin == BTN_A || pin == BTN_START) {
      doomStart();
    }
    return false;
  }

  if (pin == BTN_START) {
    g_showMap = !g_showMap;
  }
  return false;
}

void doomTick() {
  if (!g_dead && !g_win) {
    updateAndRenderFrame();
  } else {
    delay(20);
  }
}
