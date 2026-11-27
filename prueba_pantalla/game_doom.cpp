#include "game_doom.h"
#include "board_config.h"
#include "display_driver.h"
#include "input_buttons.h"

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

uint8_t g_map[DOOM_MAP_H][DOOM_MAP_W];
uint32_t g_level = 1;

struct DoomEnemy {
  float x, y;
  int8_t hp;
  bool alive;
  uint8_t hurtFrames;
  uint32_t lastAtkMs;
  uint8_t species;
};

struct DoomItem {
  float x, y;
  uint8_t type; // 1 = Medikit (+25 HP), 2 = Balas (+8)
  bool active;
};

uint16_t g_fb[DOOM_W * DOOM_H];
uint16_t g_rowBuf[SCREEN_W * 2]; // Dos filas de 320 px para blit 2x sin doble swap
float g_zBuf[DOOM_W];

constexpr float START_X = 2.5f, START_Y = 2.5f;
float g_pX = START_X, g_pY = START_Y;
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

void generateLevel() {
  // Se conectan nueve salas abriendo puertas en orden aleatorio.
  for (int y = 0; y < DOOM_MAP_H; ++y) {
    for (int x = 0; x < DOOM_MAP_W; ++x) {
      g_map[y][x] = (x % 5 == 0 || y % 5 == 0) ? static_cast<uint8_t>(random(1, 4)) : 0;
    }
  }

  struct Door { int from, to, x, y; };
  Door doors[12];
  int count = 0;
  for (int room = 0; room < 9; ++room) {
    const int x = room % 3 * 5;
    const int y = room / 3 * 5;
    if (room % 3 < 2) doors[count++] = {room, room + 1, x + 5, y + 2};
    if (room / 3 < 2) doors[count++] = {room, room + 3, x + 2, y + 5};
  }

  int group[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
  int loops = 0;
  while (count > 0) {
    const int pick = random(count);
    const Door door = doors[pick];
    doors[pick] = doors[--count];
    const int from = group[door.from], to = group[door.to];
    if (from == to) {
      if (loops++ < 2) g_map[door.y][door.x] = 0;
      continue;
    }
    g_map[door.y][door.x] = 0;
    for (int& id : group) {
      if (id == to) id = from;
    }
  }
  const int portalOffset = random(11, 15);
  if (random(2)) g_map[portalOffset][15] = 4;
  else g_map[15][portalOffset] = 4;

  int rooms[] = {1, 2, 3, 4, 5, 6, 7, 8};
  for (int i = 7; i > 0; --i) {
    const int j = random(i + 1);
    const int saved = rooms[i];
    rooms[i] = rooms[j];
    rooms[j] = saved;
  }
  for (int i = 0; i < DOOM_MAX_ENEMIES; ++i) {
    const int room = rooms[i];
    const uint8_t species = (i + g_level) % 3;
    g_enemies[i] = {room % 3 * 5 + 3.5f, room / 3 * 5 + 3.5f,
                    static_cast<int8_t>(species == 1 ? 3 : 2), true, 0, 0, species};
  }
  for (int i = 0; i < DOOM_MAX_ITEMS; ++i) {
    const int room = rooms[(i + 4) % 8];
    g_items[i] = {room % 3 * 5 + 1.5f, room / 3 * 5 + 1.5f,
                  static_cast<uint8_t>(i % 2 + 1), true};
  }
}

uint8_t tileAt(float x, float y) {
  if (x < 0 || y < 0 || x >= DOOM_MAP_W || y >= DOOM_MAP_H) return 1;
  return g_map[static_cast<int>(y)][static_cast<int>(x)];
}

bool canWalk(float x, float y) {
  constexpr float radius = 0.18f;
  return !tileAt(x - radius, y - radius) && !tileAt(x + radius, y - radius)
      && !tileAt(x - radius, y + radius) && !tileAt(x + radius, y + radius);
}

bool clearPath(float x, float y, float endX, float endY) {
  const float dx = endX - x, dy = endY - y;
  const int steps = 1 + static_cast<int>(fmaxf(fabsf(dx), fabsf(dy)) * 10);
  for (int i = 1; i < steps; ++i) {
    const float t = static_cast<float>(i) / steps;
    if (tileAt(x + dx * t, y + dy * t)) return false;
  }
  return true;
}

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

uint8_t creaturePixel(int x, int y, uint8_t species, bool frame, bool attacking) {
  // El Vigia, la Medusa y la Mascara.
  static const char art[3][16][17] = {
    {
      "......2222......", "....22111122....", "...2111111112...", "..211333311112..",
      "..211333311112..", "..211111111112..", "...2112222112...", "....11222211....",
      "...2112112112...", "..211.2112.112..", "..11..2112..11..", ".11...2112...11.",
      ".12...2..2...21.", "..2..22..22..2..", ".....2....2.....", "................"
    }, {
      ".......22.......", "......2112......", "...2.211112.2...", "..222133312222..",
      ".21111333111112.", "..221111111122..", "...2221111222...", ".22..212212..22.",
      ".1..212..212..1.", "....12....21....", "...12.1221.21...", "...1..2..2..1...",
      "..1..2....2..1..", ".....2....2.....", "................", "................"
    }, {
      "...2...22...2...", "..221221122122..", "..211111111112..", ".21133113311112.",
      ".21133113311112.", ".21111111111112.", "..222133312222..", "...2111111112...",
      "...2112112112...", "..211.2112.112..", ".211..2112..112.", ".11..211112..11.",
      ".2...21..12...2.", ".....12..21.....", "....22....22....", "................"
    }
  };

  if (y >= 8 && frame) {
    if (x < 6) ++x;
    else if (x > 9) --x;
  }
  const char pixel = art[species][y][x];
  if (pixel == '3' && frame && !attacking && y == 4) return 1;
  return pixel == '.' ? 0 : pixel - '0';
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
  printEs("Lily DOOM", g_theme.bright, 1);

  char topBuf[32];
  snprintf(topBuf, sizeof(topBuf), "ENTES:%d/%d", g_kills, DOOM_MAX_ENEMIES);
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
  snprintf(hudBuf, sizeof(hudBuf), "NIVEL %lu", static_cast<unsigned long>(g_level));
  printEs(hudBuf, g_theme.mid, 1);
  g_tft.setCursor(236, 217);
  printEs("[A]Dispara", g_theme.dim, 1);

  g_tft.setCursor(6, 229);
  printEs("Arr/Ab: mover  Izq/Der: girar  START: mapa", g_theme.dim, 1);
}

void drawEndOverlay(bool victory) {
  constexpr int16_t bx = 42, by = 60, bw = 236, bh = 108;
  g_tft.fillRect(bx, by, bw, bh, g_theme.bg);
  g_tft.drawRect(bx, by, bw, bh, victory ? g_theme.glow : 0xF800);
  g_tft.fillRect(bx + 2, by + 2, bw - 4, 24, victory ? g_theme.selBg : 0x4000);

  g_tft.setCursor(bx + (victory ? 20 : 54), by + 7);
  printEs(victory ? "PORTAL ABIERTO" : "HAS MUERTO", victory ? g_theme.glow : 0xF800, 2);

  const uint32_t sec = (millis() - g_startMs) / 1000;
  char buf[40];
  snprintf(buf, sizeof(buf), "Bajas: %d/%d   Tiempo: %lus",
           g_kills, DOOM_MAX_ENEMIES, static_cast<unsigned long>(sec));
  g_tft.setCursor(bx + 24, by + 40);
  printEs(buf, g_theme.bright, 1);

  g_tft.setCursor(bx + 36, by + 64);
  printEs(victory ? "[A] Siguiente nivel" : "[A] Nueva partida", g_theme.glow, 1);
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
    if (side < 0.45f && fwd < bestDist &&
        clearPath(g_pX, g_pY, g_enemies[i].x, g_enemies[i].y)) {
      bestDist = fwd;
      bestIdx = i;
    }
  }

  if (bestIdx >= 0) {
    g_enemies[bestIdx].hp--;
    g_enemies[bestIdx].hurtFrames = 4;
    const float pushX = g_enemies[bestIdx].x + g_pDirX * 0.22f;
    const float pushY = g_enemies[bestIdx].y + g_pDirY * 0.22f;
    if (canWalk(pushX, pushY)) {
      g_enemies[bestIdx].x = pushX;
      g_enemies[bestIdx].y = pushY;
    }
    if (g_enemies[bestIdx].hp <= 0) {
      g_enemies[bestIdx].alive = false;
      g_kills++;
      Serial.printf("[Lily DOOM] Ente eliminado (%d/%d)\n", g_kills, DOOM_MAX_ENEMIES);
    }
  }
  drawHud();
}

void drawSprite(float sx, float sy, bool isEnemy, uint8_t param, bool hurtFlash, bool frame = false, bool attacking = false) {
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

  const int bob = isEnemy && param == 1 && frame ? spriteH / 14 : 0;
  int drawStartY = -spriteH / 2 + DOOM_H / 2 - bob;
  int drawEndY   =  spriteH / 2 + DOOM_H / 2 - bob;
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
      const int d = (y + bob) * 256 - DOOM_H * 128 + spriteH * 128;
      const int texY = ((d * 16) / spriteH) / 256;
      if (texY < 0 || texY >= 16) continue;

      const uint8_t pix = isEnemy ? creaturePixel(texX, texY, param, frame, attacking) : itemPixel(texX, texY, param);
      if (pix == 0) continue;

      uint16_t col = 0;
      if (isEnemy) {
        constexpr uint16_t bodies[] = {0x64B2, 0xB1B9, 0xA54C};
        if (hurtFlash) col = 0xFFFF;
        else if (pix == 3) col = attacking ? 0xF983 : 0xFF53;
        else col = scaleRgb565(bodies[param], pix == 2 ? br / 2 : br);
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
        const uint8_t tile = g_map[my][mx];
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

  const float rotSpd = 2.3f * dt;
  const int move = static_cast<int>(inputButtonHeld(BTN_UP)) - inputButtonHeld(BTN_DOWN);
  if (move != 0) {
    const float step = move * 3.2f * dt;
    const float nx = g_pX + g_pDirX * step;
    const float ny = g_pY + g_pDirY * step;
    if (canWalk(nx, g_pY)) g_pX = nx;
    if (canWalk(g_pX, ny)) g_pY = ny;
    if (move > 0 && tileAt(g_pX + g_pDirX * 0.55f, g_pY + g_pDirY * 0.55f) == 4) {
      g_win = true;
      drawEndOverlay(true);
      Serial.printf("[Lily DOOM] Nivel %lu completado. Bajas=%d/%d\n",
                    static_cast<unsigned long>(g_level), g_kills, DOOM_MAX_ENEMIES);
      return;
    }
  }
  if (inputButtonHeld(BTN_LEFT)) {
    const float oldDirX = g_pDirX;
    const float oldPlX = g_pPlaneX;
    const float c = cosf(-rotSpd), s = sinf(-rotSpd);
    g_pDirX   = g_pDirX * c - g_pDirY * s;
    g_pDirY   = oldDirX * s + g_pDirY * c;
    g_pPlaneX = g_pPlaneX * c - g_pPlaneY * s;
    g_pPlaneY = oldPlX * s + g_pPlaneY * c;
  }
  if (inputButtonHeld(BTN_RIGHT)) {
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
    if (distSq < 36.0f && distSq > 0.85f) {
      const float invD = 1.0f / sqrtf(distSq);
      const float step = 0.85f * dt;
      const float ex = g_enemies[i].x + dx * invD * step;
      const float ey = g_enemies[i].y + dy * invD * step;
      if (canWalk(ex, g_enemies[i].y)) g_enemies[i].x = ex;
      if (canWalk(g_enemies[i].x, ey)) g_enemies[i].y = ey;
    } else if (distSq <= 0.85f && (now - g_enemies[i].lastAtkMs) >= 950 &&
               clearPath(g_enemies[i].x, g_enemies[i].y, g_pX, g_pY)) {
      g_enemies[i].lastAtkMs = now;
      g_hp -= 12;
      if (g_hp < 0) g_hp = 0;
      g_hurtFlash = 4;
      hudDirty = true;
      if (g_hp == 0) {
        g_dead = true;
        drawHud();
        drawEndOverlay(false);
        Serial.println(F("[Lily DOOM] Fin de la partida."));
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
        hitTile = g_map[mapY][mapX];
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
      const bool attacking = e.lastAtkMs != 0 && now - e.lastAtkMs < 220;
      const bool frame = attacking || ((now / 170 + list[i].idx) & 1);
      drawSprite(e.x, e.y, true, e.species, e.hurtFrames > 0, frame, attacking);
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

static void startLevel() {
  g_pX = START_X;
  g_pY = START_Y;
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

  g_lastShotMs = g_startMs - 260;
  generateLevel();
  doomRenderFull();
  Serial.printf("[Lily DOOM] Nivel %lu generado. Avanzar GPIO %d, retroceder GPIO %d.\n",
                static_cast<unsigned long>(g_level), BTN_UP, BTN_DOWN);
}

void doomStart() {
  g_level = 1;
  startLevel();
}

bool doomHandleButton(int pin) {
  if (pin == BTN_B) return true;

  if (g_dead || g_win) {
    if (pin == BTN_A || pin == BTN_START) {
      if (g_win) {
        ++g_level;
        startLevel();
      } else {
        doomStart();
      }
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
