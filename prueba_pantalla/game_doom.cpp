#include "game_doom.h"
#include "abyss_sprites.h"
#include "board_config.h"
#include "display_driver.h"
#include "input_buttons.h"
#include "lily_sprites.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

namespace {

constexpr int DOOM_W = 160; // Render 160x90 escalado 2x -> 320x180 fluido
constexpr int DOOM_H = 90;
constexpr int DOOM_VIEW_Y = 22;
constexpr int DOOM_MAP_W = 16;
constexpr int DOOM_MAP_H = 16;
constexpr int DOOM_MAX_ENEMIES = ABYSS_SPECIES_COUNT;
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
  uint32_t deathMs;
  uint32_t windupMs;
  bool winding;
};

struct DoomItem {
  float x, y;
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
int g_kills = 0;
uint8_t g_hurtFlash = 0;
constexpr uint32_t KATANA_SWING_MS = 280;
constexpr uint32_t KATANA_IMPACT_MS = 90;
constexpr uint32_t KATANA_COOLDOWN_MS = 340;
constexpr uint32_t DASH_COOLDOWN_MS = 650;
constexpr uint32_t DEATH_MS = 440;
constexpr uint32_t WINDUP_MS = 560;
constexpr uint16_t TEMPLE_VOID = THEME_PINK.bg;
constexpr uint16_t TEMPLE_STONE = THEME_PINK.dark;
constexpr uint16_t TEMPLE_METAL = THEME_PINK.dim;
constexpr uint16_t TEMPLE_NEON = THEME_PINK.mid;
constexpr uint16_t TEMPLE_LIGHT = THEME_PINK.glow;
constexpr uint16_t ATTACK_WARNING = 0xFDA3;
constexpr uint16_t HAND_OUTLINE = 0x49A6;
constexpr uint16_t HAND_SHADE = 0xA38C;
constexpr uint16_t HAND_SKIN = 0xD555;
constexpr uint16_t HAND_LIGHT = 0xF6BB;
constexpr uint16_t HAND_NAIL = 0xCB55;
constexpr uint32_t PROLOGUE_MS = 14000;

enum class DoomPhase : uint8_t { Title, Prologue, Playing };
DoomPhase g_phase = DoomPhase::Title;
LilyPose g_portraitPose = LilyPose::Guardia;
uint32_t g_prologueMs = 0;
float g_stride = 0.0f;
float g_strideBlend = 0.0f;
uint32_t g_lastSwingMs = 0;
bool g_katanaPending = false;
uint32_t g_lastDashMs = 0;
float g_dashRemaining = 0.0f;
float g_dashX = 0.0f, g_dashY = 0.0f;
bool g_dashReady = true;
uint32_t g_lastFrameMs = 0;
uint32_t g_startMs = 0;
bool g_dead = false;
bool g_win = false;
bool g_showMap = true;
DoomEnemy g_enemies[DOOM_MAX_ENEMIES];
DoomItem g_items[DOOM_MAX_ITEMS];

struct TempleDoor {
  int from, to, x, y;
};

void generateLevel() {
  // Crece un camino principal con ramales. Los sectores sobrantes quedan macizos.
  TempleDoor doors[12];
  int count = 0;
  for (int room = 0; room < 9; ++room) {
    const int x = room % 3 * 5, y = room / 3 * 5;
    if (room % 3 < 2) doors[count++] = {room, room + 1, x + 5, y + 2};
    if (room / 3 < 2) doors[count++] = {room, room + 3, x + 2, y + 5};
  }
  int depth[9] = {1};
  const int roomCount = random(5, 9);
  for (int built = 1; built < roomCount; ++built) {
    int frontier[12], frontierCount = 0;
    for (int i = 0; i < count; ++i) {
      if ((depth[doors[i].from] == 0) != (depth[doors[i].to] == 0)) frontier[frontierCount++] = i;
    }
    const int chosen = frontier[random(frontierCount)];
    const TempleDoor door = doors[chosen];
    const int next = depth[door.from] ? door.to : door.from;
    depth[next] = depth[door.from] + depth[door.to] + 1;
    doors[chosen] = doors[--count];
    doors[count] = door;
  }

  for (auto& row : g_map) {
    for (auto& tile : row) tile = 1;
  }

  // Las formas conservan la cruz central que une los cuatro umbrales.
  constexpr const char* shapes[][4] = {
    {"....", "....", "....", "...."}, // nave
    {"#..#", "....", "....", "#..#"}, // cripta octogonal
    {"...#", "....", "....", "...#"}, // galeria estrecha
    {"#..#", "....", "....", "...."}, // altar con hornacinas
    {"....", "....", "....", "#.##"}, // taller de relicarios
    {"#...", "....", "....", "#.#."}  // capilla irregular
  };
  for (int room = 0; room < 9; ++room) {
    if (!depth[room]) continue;
    const int ox = room % 3 * 5, oy = room / 3 * 5;
    const int style = room == 0 ? 0 : random(6);
    const uint8_t material = 1 + style % 3;
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        g_map[oy + y + 1][ox + x + 1] = shapes[style][y][x] == '.' ? 0 : 3;
      }
    }
    for (int lane = 1; lane <= 4; ++lane) {
      g_map[oy][ox + lane] = g_map[oy + 5][ox + lane] = material;
      g_map[oy + lane][ox] = g_map[oy + lane][ox + 5] = material;
    }
  }

  for (int i = count; i < 12; ++i) g_map[doors[i].y][doors[i].x] = 0;
  if (random(3) == 0) {
    for (int i = 0; i < count; ++i) {
      const TempleDoor& door = doors[i];
      if (depth[door.from] && depth[door.to]) {
        g_map[door.y][door.x] = 0;
        break;
      }
    }
  }

  int portalX = 0, portalY = 2, portalDepth = 0;
  constexpr int sideX[] = {-1, 1, 0, 0};
  constexpr int sideY[] = {0, 0, -1, 1};
  for (int room = 0; room < 9; ++room) {
    if (depth[room] <= portalDepth) continue;
    const int col = room % 3, row = room / 3;
    const int firstSide = random(4);
    for (int j = 0; j < 4; ++j) {
      const int side = (firstSide + j) % 4;
      const int nx = col + sideX[side], ny = row + sideY[side];
      if (nx >= 0 && nx < 3 && ny >= 0 && ny < 3 && depth[ny * 3 + nx]) continue;
      portalX = col * 5 + (sideX[side] < 0 ? 0 : sideX[side] > 0 ? 5 : 2);
      portalY = row * 5 + (sideY[side] < 0 ? 0 : sideY[side] > 0 ? 5 : 2);
      portalDepth = depth[room];
      break;
    }
  }
  g_map[portalY][portalX] = 4;

  // Cada aparicion usa una baldosa libre distinta, fuera de la entrada.
  uint8_t floorCells[128];
  int floorCount = 0;
  for (int y = 1; y < DOOM_MAP_H - 1; ++y) {
    for (int x = 1; x < DOOM_MAP_W - 1; ++x) {
      if ((x > 5 || y > 5) && g_map[y][x] == 0 && x % 5 && y % 5) {
        floorCells[floorCount++] = y * DOOM_MAP_W + x;
      }
    }
  }
  for (int i = 0; i < DOOM_MAX_ENEMIES + DOOM_MAX_ITEMS; ++i) {
    const int pick = random(floorCount);
    const int tile = floorCells[pick];
    floorCells[pick] = floorCells[--floorCount];
    const float x = tile % DOOM_MAP_W + 0.5f, y = tile / DOOM_MAP_W + 0.5f;
    if (i < DOOM_MAX_ENEMIES) {
      const uint8_t species = (i + g_level) % ABYSS_SPECIES_COUNT;
      g_enemies[i] = {x, y, static_cast<int8_t>(species == 1 ? 3 : 2),
                      true, 0, 0, species, 0, 0, false};
    } else {
      g_items[i - DOOM_MAX_ENEMIES] = {x, y, true};
    }
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
  uint16_t base;
  if (tile == 1) {
    const bool trim = tx == 2 || tx == 13;
    const bool panel = tx >= 4 && tx <= 11 && ty >= 2 && ty <= 13;
    const bool trace = (tx == 4 && ty < 10) || (ty == 9 && tx >= 4 && tx < 11);
    base = trace ? TEMPLE_LIGHT : trim ? TEMPLE_METAL : panel ? TEMPLE_STONE : TEMPLE_VOID;
  } else if (tile == 2) {
    const int dx = abs(tx - 7), dy = abs(ty - 7);
    const bool sigil = dx * 2 + dy == 7 || (dx <= 1 && dy <= 1);
    base = sigil ? TEMPLE_NEON : (ty == 1 || ty == 14) ? TEMPLE_METAL : TEMPLE_STONE;
  } else if (tile == 3) {
    const bool conduit = tx == 3 || tx == 12 || (ty == 4 && tx > 3 && tx < 12);
    const bool inset = tx > 4 && tx < 11 && ty > 6 && ty < 13;
    base = conduit ? TEMPLE_NEON : inset ? TEMPLE_VOID : TEMPLE_METAL;
  } else {
    const bool frame = tx <= 2 || tx >= 13 || ty <= 1 || ty >= 14;
    const bool aperture = abs(tx - 7) * 2 + abs(ty - 7) < 9;
    base = frame ? TEMPLE_LIGHT : aperture ? TEMPLE_NEON : TEMPLE_VOID;
  }

  float fog = 1.0f - (dist * 0.085f);
  if (fog < 0.18f) fog = 0.18f;
  if (fog > 1.0f) fog = 1.0f;
  if (darkSide) fog *= 0.68f;
  return scaleRgb565(base, static_cast<uint8_t>(fog * 255.0f));
}

uint8_t itemPixel(int x, int y) {
  if (x < 5 || x > 26 || y < 19 || y > 29) return 0;
  if (x == 5 || x == 26 || y == 19 || y == 29) return 2;
  if ((x >= 14 && x <= 17) || (y >= 23 && y <= 25 && x >= 10 && x <= 21)) return 3;
  return 1;
}

void drawLilySprite(LilyPose pose, int x, int y, int size, uint16_t background) {
  const auto& sprite = LILY_SPRITES[static_cast<uint8_t>(pose)];
  g_tft.startWrite();
  g_tft.setAddrWindow(x, y, size, size);
  for (int row = 0; row < size; ++row) {
    const int source = (row * LILY_SPRITE_SIZE / size) * LILY_SPRITE_SIZE;
    for (int col = 0; col < size; ++col) {
      const uint16_t pixel = sprite[source + col * LILY_SPRITE_SIZE / size];
      g_rowBuf[col] = pixel ? pixel : background;
    }
    g_tft.writePixels(g_rowBuf, size);
  }
  g_tft.endWrite();
}

LilyPose lilyCurrentPose(uint32_t now) {
  if (g_dashRemaining > 0) return LilyPose::Dash;
  if (now - g_lastSwingMs < KATANA_SWING_MS) return LilyPose::Tajo;
  return LilyPose::Guardia;
}

void drawLilyPortrait(LilyPose pose) {
  g_portraitPose = pose;
  drawLilySprite(pose, 200, 203, 25, g_theme.bg);
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
  g_tft.setCursor(232, 7);
  printEs("[SELECT] Salir", g_theme.dim, 1);

  g_tft.fillRect(0, 202, SCREEN_W, 38, g_theme.bg);
  g_tft.drawFastHLine(0, 202, SCREEN_W, g_theme.bright);

  const uint16_t hpCol = (g_hp <= 30) ? 0xF800 : g_theme.glow;
  char hudBuf[48];
  snprintf(hudBuf, sizeof(hudBuf), "HP:%3d%%", g_hp);
  g_tft.setCursor(8, 208);
  printEs(hudBuf, hpCol, 2);

  g_tft.setCursor(105, 210);
  printEs(g_dashReady ? "[B] Dash listo" : "[B] Recargando",
          g_dashReady ? g_theme.bright : g_theme.mid, 1);

  g_tft.setCursor(236, 207);
  snprintf(hudBuf, sizeof(hudBuf), "NIVEL %lu", static_cast<unsigned long>(g_level));
  printEs(hudBuf, g_theme.mid, 1);
  g_tft.setCursor(236, 217);
  printEs("[A] Katana", g_theme.dim, 1);

  drawLilyPortrait(lilyCurrentPose(millis()));

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
  printEs("[SELECT] Volver al menú", g_theme.mid, 1);
}

void strikeKatana(uint32_t now) {
  for (auto& enemy : g_enemies) {
    if (!enemy.alive) continue;
    const float dx = enemy.x - g_pX, dy = enemy.y - g_pY;
    const float forward = dx * g_pDirX + dy * g_pDirY;
    const float side = fabsf(dx * g_pDirY - dy * g_pDirX);
    if (forward <= 0 || side > forward * 0.9f || dx * dx + dy * dy > 1.55f * 1.55f ||
        !clearPath(g_pX, g_pY, enemy.x, enemy.y)) continue;

    --enemy.hp;
    enemy.hurtFrames = 4;
    if (enemy.hp <= 0) {
      enemy.alive = false;
      enemy.deathMs = now;
      ++g_kills;
      Serial.printf("[Lily DOOM] Ente eliminado con katana (%d/%d)\n", g_kills, DOOM_MAX_ENEMIES);
    }
  }
  drawHud();
}

void drawSprite(float sx, float sy, const DoomEnemy* enemy, uint32_t now) {
  const float relX = sx - g_pX;
  const float relY = sy - g_pY;
  const float invDet = 1.0f / (g_pPlaneX * g_pDirY - g_pDirX * g_pPlaneY);
  const float transX = invDet * (g_pDirY * relX - g_pDirX * relY);
  const float transY = invDet * (-g_pPlaneY * relX + g_pPlaneX * relY);
  if (transY <= 0.2f) return;

  const int screenX = static_cast<int>((DOOM_W / 2.0f) * (1.0f + transX / transY));
  const uint32_t deathAge = enemy && !enemy->alive ? now - enemy->deathMs : 0;
  const bool charging = enemy && enemy->alive && enemy->winding;
  const uint32_t strikeAge = enemy && enemy->lastAtkMs ? now - enemy->lastAtkMs : 1000;
  const int phase = enemy ? (now / 90 + enemy->species) & 7 : 0;
  constexpr int wave[] = {0, 2, 3, 2, 0, -2, -3, -2};
  const int pulse = charging ? static_cast<int>(now - enemy->windupMs) * 10 / WINDUP_MS : 0;
  const int lunge = enemy && enemy->alive && strikeAge < 190 ? (190 - strikeAge) / 13 : 0;
  const int scale = 100 + lunge - pulse - static_cast<int>(deathAge / 21);
  const int spriteH = constrain(static_cast<int>(DOOM_H / transY * scale / 100), 1, DOOM_H * 2);
  const int spriteW = spriteH * (100 + pulse + lunge / 2) / 100;
  const bool floating = enemy && (enemy->species == 1 || enemy->species == 4);
  const int bob = enemy && enemy->alive ? spriteH * wave[phase] / (floating ? 29 : 70) : 0;
  const int sink = deathAge ? spriteH * deathAge / (DEATH_MS * 3) : 0;
  int drawStartY = -spriteH / 2 + DOOM_H / 2 - bob + sink;
  int drawEndY   =  spriteH / 2 + DOOM_H / 2 - bob + sink;
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
    const int texX = ((stripe + spriteW / 2 - screenX) * ABYSS_SPRITE_SIZE) / spriteW;
    if (texX < 0 || texX >= ABYSS_SPRITE_SIZE) continue;

    for (int y = drawStartY; y <= drawEndY; ++y) {
      const int texY = ((y + bob - sink + spriteH / 2 - DOOM_H / 2) *
                        ABYSS_SPRITE_SIZE) / spriteH;
      if (texY < 0 || texY >= ABYSS_SPRITE_SIZE) continue;

      uint16_t col;
      if (enemy) {
        int artX = texX;
        if (enemy->alive) {
          const int bend = floating ? wave[(phase + texY / 6) & 7] :
                                     wave[(phase + (texY >= 18 ? 4 : 0)) & 7] / 2;
          artX += bend + (texY < 15 ? lunge / 5 - pulse / 5 : 0);
        }
        if (artX < 0 || artX >= ABYSS_SPRITE_SIZE) continue;
        if (deathAge && (texX * 17 + texY * 29) % DEATH_MS < deathAge) continue;
        const int index = ABYSS_SPRITES[enemy->species][texY][artX] - '0';
        if (!index) continue;
        constexpr uint16_t rose[] = {0, 0x2065, 0x3869, THEME_PINK.dim, 0x9290,
                                     THEME_PINK.bright, THEME_PINK.glow, 0xF6BB};
        const int shade = (charging || lunge) && index < 6 ? index + 2 : index;
        col = enemy->alive && enemy->hurtFrames ? rose[7] : scaleRgb565(rose[shade], br);
        if (charging && index >= 5 && ((now / (pulse < 6 ? 110 : 65)) & 1)) col = rose[7];
      } else {
        const uint8_t pixel = itemPixel(texX, texY);
        if (!pixel) continue;
        col = pixel == 3 ? 0xF800 : (pixel == 2 ? g_theme.dim : 0xFFFF);
      }
      g_fb[y * DOOM_W + stripe] = col;
    }
  }
  if (charging || lunge) {
    const int radius = constrain(spriteW / 3 + lunge, 5, 46);
    const int floorY = constrain(drawEndY - 3, DOOM_H / 2 + 3, DOOM_H - 5);
    const int filled = radius * (charging ? pulse : 10) / 10;
    for (int dx = -radius; dx <= radius; ++dx) {
      const int x = screenX + dx;
      if (x < 0 || x >= DOOM_W || transY >= g_zBuf[x]) continue;
      const int y = floorY - dx * dx / (radius * 3);
      g_fb[y * DOOM_W + x] = abs(dx) <= filled ? TEMPLE_NEON : TEMPLE_METAL;
    }
  }
  if (deathAge || lunge) {
    const int power = deathAge ? deathAge / 28 : 15 - lunge;
    const int coreY = (drawEndY + drawStartY) / 2;
    for (int i = 0; i < 11; ++i) {
      const int dx = ((i * 13) % 11 - 5) * (3 + power) / 3;
      const int dy = ((i * 53) % 17 - 8) * (2 + power) / 3 + power / 2;
      const int x = screenX + dx, y = coreY + dy;
      if (x >= 0 && x < DOOM_W && y >= 0 && y < DOOM_H && transY < g_zBuf[x]) {
        g_fb[y * DOOM_W + x] = i % 3 ? TEMPLE_NEON : HAND_LIGHT;
      }
    }
  }
}

void weaponLine(int x0, int y0, int x1, int y1, uint16_t color) {
  const int dx = abs(x1 - x0), dy = -abs(y1 - y0);
  const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  while (true) {
    if (x0 >= 0 && x0 < DOOM_W && y0 >= 0 && y0 < DOOM_H) g_fb[y0 * DOOM_W + x0] = color;
    if (x0 == x1 && y0 == y1) break;
    const int twice = 2 * error;
    if (twice >= dy) { error += dy; x0 += sx; }
    if (twice <= dx) { error += dx; y0 += sy; }
  }
}

void drawLeftHand() {
  if (g_strideBlend < 0.04f) return;
  constexpr const char* fist[] = {
    ".......222........",
    ".....2244422......",
    "....245544442.....",
    "...24554444442....",
    "..245444444442....",
    "..244443444442....",
    "..24443444442.....",
    "...244344422......",
    "....2444422.......",
    "....244422........",
    "...244422.........",
    "..224422..........",
    "..21212...........",
    ".212122...........",
    "212112............",
    "211122............"
  };
  constexpr uint16_t colors[] = {0, TEMPLE_METAL, HAND_OUTLINE, HAND_SHADE,
                                 HAND_SKIN, HAND_NAIL};
  const int x0 = 14 + lroundf(4.0f * cosf(g_stride));
  const int y0 = DOOM_H - 2 - lroundf((15.0f + 8.0f * sinf(g_stride)) * g_strideBlend);
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 18; ++x) {
      const char pixel = fist[y][x];
      if (pixel != '.') weaponLine(x0 + x, y0 + y, x0 + x, y0 + y, colors[pixel - '0']);
    }
  }
  const int cuffY = y0 + 14;
  for (int w = 0; w < 11; ++w) {
    weaponLine(x0 + w, cuffY, x0 + w - 11, DOOM_H + 2,
               w == 0 || w == 10 ? HAND_OUTLINE : (w < 3 ? TEMPLE_NEON : TEMPLE_METAL));
  }
}

void drawKatana(uint32_t now) {
  // Poses: guardia, anticipacion, corte diagonal, seguimiento y regreso.
  struct Pose { int x, y, angle; };
  constexpr Pose poses[] = {{137, 81, -120}, {147, 71, -83}, {117, 65, -139},
                            {71, 73, -174}, {56, 91, -164}, {137, 81, -120}};
  const uint32_t age = now - g_lastSwingMs;
  const float progress = age < KATANA_SWING_MS ? age * 5.0f / KATANA_SWING_MS : 0;
  const int index = static_cast<int>(progress);
  const float f = progress - index;
  const Pose& a = poses[index];
  const Pose& b = poses[index + 1];
  const int x = lroundf(a.x + (b.x - a.x) * f + 2.0f * cosf(g_stride) * g_strideBlend);
  const int y = lroundf(a.y + (b.y - a.y) * f + 3.0f * sinf(g_stride) * g_strideBlend)
                + (g_dashRemaining > 0 ? 5 : 0);
  const float angle = (a.angle + (b.angle - a.angle) * f) * 0.017453293f;
  const float ux = cosf(angle), uy = sinf(angle);
  const float vx = -uy, vy = ux;
  auto line = [&](int from, int across, int to, int endAcross, uint16_t color) {
    weaponLine(lroundf(x + ux * from + vx * across), lroundf(y + uy * from + vy * across),
               lroundf(x + ux * to + vx * endAcross), lroundf(y + uy * to + vy * endAcross), color);
  };

  if (index >= 1 && index <= 3) {
    for (int i = 0; i < 4; ++i) {
      const float trail = angle + (i + 1) * 0.16f;
      const int endX = lroundf(x + cosf(trail) * (57 + i * 2));
      const int endY = lroundf(y + sinf(trail) * (57 + i * 2));
      weaponLine(endX, endY, lroundf(endX + vx * 10), lroundf(endY + vy * 10),
                 i == 0 ? TEMPLE_LIGHT : i == 1 ? TEMPLE_NEON : TEMPLE_METAL);
    }
  }

  for (int w = -3; w <= 3; ++w) {
    line(-15, w, -2, w, abs(w) == 3 ? TEMPLE_METAL : TEMPLE_VOID);
    line(2, w, 43, w, w == -3 ? HAND_LIGHT : w == -2 ? TEMPLE_LIGHT :
                         w == 2 ? TEMPLE_NEON : w == 3 ? TEMPLE_METAL : THEME_PINK.bright);
    line(43, w, 52, -3, w <= -2 ? HAND_LIGHT : TEMPLE_METAL);
  }
  for (int grip = -13; grip < -3; grip += 4) line(grip, -2, grip + 2, 2, TEMPLE_NEON);
  line(-15, -3, -15, 3, TEMPLE_LIGHT);
  line(-1, -7, 1, 7, TEMPLE_METAL);
  line(0, -7, 2, 7, TEMPLE_NEON);
  line(3, 0, 9, 0, TEMPLE_LIGHT);

  // Mano derecha anclada a la empunadura durante todo el tajo.
  const int gripX = lroundf(x - ux * 8), gripY = lroundf(y - uy * 8);
  for (int w = -5; w <= 5; ++w) {
    const uint16_t sleeve = abs(w) == 5 ? HAND_OUTLINE : w < 0 ? TEMPLE_METAL : TEMPLE_NEON;
    weaponLine(gripX + w, gripY + 4, gripX + 8 + w * 2, DOOM_H + 4, sleeve);
  }
  for (int finger = -12; finger <= -4; finger += 2) {
    line(finger, -5, finger + 1, 4, HAND_OUTLINE);
    line(finger, -4, finger, 3, finger == -12 ? HAND_SHADE : HAND_SKIN);
    line(finger, -4, finger, -2, HAND_LIGHT);
    if (finger < -4) line(finger, 2, finger, 3, HAND_NAIL);
  }
  line(-5, -6, -3, 1, HAND_OUTLINE);
  line(-5, -5, -3, 0, HAND_LIGHT);
  line(-9, 4, -8, 4, THEME_PINK.glow);
}

void drawWeaponAndOverlay(uint32_t now) {
  const int cx = DOOM_W / 2;
  const int cy = DOOM_H / 2;
  for (int i = -3; i <= 3; ++i) {
    g_fb[cy * DOOM_W + (cx + i)] = g_theme.glow;
    g_fb[(cy + i) * DOOM_W + cx] = g_theme.glow;
  }

  drawLeftHand();
  drawKatana(now);
  if (g_dashRemaining > 0) {
    for (int y = 14; y < 80; y += 13) {
      for (int x = 2; x < 13; ++x) {
        g_fb[y * DOOM_W + x] = TEMPLE_NEON;
        g_fb[y * DOOM_W + DOOM_W - 1 - x] = TEMPLE_NEON;
      }
    }
  }

  if (g_showMap) {
    const int mx0 = DOOM_W - (DOOM_MAP_W * 2) - 3;
    const int my0 = 2;
    for (int my = 0; my < DOOM_MAP_H; ++my) {
      for (int mx = 0; mx < DOOM_MAP_W; ++mx) {
        const uint8_t tile = g_map[my][mx];
        const uint16_t c = (tile == 4) ? TEMPLE_LIGHT : (tile ? TEMPLE_VOID : TEMPLE_METAL);
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
      if (ex >= 0 && ex < DOOM_W && ey >= 0 && ey < DOOM_H) g_fb[ey * DOOM_W + ex] = g_enemies[i].winding ? ATTACK_WARNING : TEMPLE_NEON;
    }
    int px = mx0 + static_cast<int>(g_pX * 2);
    int py = my0 + static_cast<int>(g_pY * 2);
    if (px >= 0 && px < DOOM_W && py >= 0 && py < DOOM_H) g_fb[py * DOOM_W + px] = 0xFFFF;
  }

  int warning = -1;
  for (const auto& enemy : g_enemies) {
    if (enemy.alive && enemy.winding) {
      const int progress = (now - enemy.windupMs) * 32 / WINDUP_MS;
      if (progress > warning) warning = progress;
    }
  }
  if (warning >= 0) {
    // Indicador global: se llena al preparar el golpe, incluso fuera de camara.
    for (int x = 62; x <= 97; ++x) {
      g_fb[5 * DOOM_W + x] = TEMPLE_NEON;
      g_fb[9 * DOOM_W + x] = TEMPLE_NEON;
      for (int y = 6; y < 9; ++y) {
        g_fb[y * DOOM_W + x] = x - 63 < warning ? ATTACK_WARNING : TEMPLE_VOID;
      }
    }
    for (int y = 12; y < 25; ++y) {
      if (y < 21 || y > 22) g_fb[y * DOOM_W + 80] = ATTACK_WARNING;
    }
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

  const float previousX = g_pX, previousY = g_pY;
  const bool dashing = g_dashRemaining > 0.0f;
  if (dashing) {
    const float distance = fminf(dt * 10.0f, g_dashRemaining);
    g_dashRemaining -= distance;
    const int steps = 1 + static_cast<int>(distance / 0.12f);
    const float step = distance / steps;
    for (int i = 0; i < steps; ++i) {
      const float x = g_pX + g_dashX * step;
      const float y = g_pY + g_dashY * step;
      if (canWalk(x, g_pY)) g_pX = x;
      if (canWalk(g_pX, y)) g_pY = y;
    }
  }

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
  const float travel = hypotf(g_pX - previousX, g_pY - previousY);
  const float blendTarget = travel > 0.001f ? 1.0f : 0.0f;
  g_strideBlend += (blendTarget - g_strideBlend) * fminf(dt * 14.0f, 1.0f);
  g_stride = fmodf(g_stride + travel * 6.0f, 6.2831853f);
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

  if (g_katanaPending && now - g_lastSwingMs >= KATANA_IMPACT_MS) {
    g_katanaPending = false;
    strikeKatana(now);
  }

  const bool ready = now - g_lastDashMs >= DASH_COOLDOWN_MS;
  bool hudDirty = ready != g_dashReady;
  g_dashReady = ready;
  for (auto& item : g_items) {
    if (!item.active || g_hp >= 100) continue;
    const float dx = item.x - g_pX, dy = item.y - g_pY;
    if (dx * dx + dy * dy < 0.45f) {
      g_hp = g_hp > 75 ? 100 : g_hp + 25;
      item.active = false;
      hudDirty = true;
    }
  }

  for (auto& enemy : g_enemies) {
    if (!enemy.alive) continue;
    if (enemy.hurtFrames) --enemy.hurtFrames;
    const float dx = g_pX - enemy.x, dy = g_pY - enemy.y;
    const float distSq = dx * dx + dy * dy;
    if (enemy.winding) {
      if (now - enemy.windupMs < WINDUP_MS) continue;
      enemy.winding = false;
      enemy.lastAtkMs = now;
      if (dashing || distSq > 1.13f * 1.13f || !clearPath(enemy.x, enemy.y, g_pX, g_pY)) continue;
      g_hp = g_hp > 12 ? g_hp - 12 : 0;
      g_hurtFlash = 4;
      hudDirty = true;
      if (g_hp == 0) {
        g_dead = true;
        drawHud();
        drawEndOverlay(false);
        Serial.println(F("[Lily DOOM] Fin de la partida."));
        return;
      }
    } else if (distSq <= 0.85f && now - enemy.lastAtkMs >= 850 &&
               clearPath(enemy.x, enemy.y, g_pX, g_pY)) {
      enemy.windupMs = now;
      enemy.winding = true;
    } else if (distSq < 36.0f && distSq > 0.85f && now - enemy.lastAtkMs >= 230) {
      const float step = 0.85f * dt / sqrtf(distSq);
      const float ex = enemy.x + dx * step, ey = enemy.y + dy * step;
      if (canWalk(ex, enemy.y)) enemy.x = ex;
      if (canWalk(enemy.x, ey)) enemy.y = ey;
    }
  }

  if (g_hurtFlash > 0) g_hurtFlash--;

  // 1. Techo y piso con degradado de profundidad
  for (int y = 0; y < DOOM_H; ++y) {
    uint16_t rowCol = 0x0000;
    if (y < DOOM_H / 2) {
      const uint8_t b = static_cast<uint8_t>((DOOM_H / 2 - y) * 2);
      rowCol = scaleRgb565(TEMPLE_VOID, b);
    } else {
      const uint8_t b = static_cast<uint8_t>((y - DOOM_H / 2) * 3);
      rowCol = scaleRgb565(TEMPLE_STONE, b);
    }
    uint16_t* dst = &g_fb[y * DOOM_W];
    for (int x = 0; x < DOOM_W; ++x) {
      const int depth = y - DOOM_H / 2;
      const bool seam = depth > 5 && (depth % 12 == 0 || (abs(x - DOOM_W / 2) * 8) % depth < 2);
      dst[x] = seam ? scaleRgb565(TEMPLE_LIGHT, depth + 12) : rowCol;
    }
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
    if (!g_enemies[i].alive && now - g_enemies[i].deathMs >= DEATH_MS) continue;
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
      drawSprite(e.x, e.y, &e, now);
    } else {
      const DoomItem& it = g_items[list[i].idx];
      drawSprite(it.x, it.y, nullptr, now);
    }
  }

  // 4. Arma, mira y minimapa
  drawWeaponAndOverlay(now);

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

  if (hudDirty) {
    drawHud();
  } else {
    const LilyPose pose = lilyCurrentPose(now);
    if (pose != g_portraitPose) drawLilyPortrait(pose);
  }
}

void drawIntro() {
  g_tft.fillScreen(THEME_PINK.bg);
  g_tft.drawRect(12, 12, SCREEN_W - 24, SCREEN_H - 24, THEME_PINK.dim);
  g_tft.drawFastHLine(25, 32, SCREEN_W - 50, THEME_PINK.dark);
  g_tft.drawFastHLine(25, 206, SCREEN_W - 50, THEME_PINK.dark);

  if (g_phase == DoomPhase::Title) {
    drawLilySprite(LilyPose::Guardia, 24, 51, 128, THEME_PINK.bg);
    g_tft.setCursor(162, 76);
    printEs("Lily DOOM", THEME_PINK.glow, 2);
    g_tft.setCursor(162, 102);
    printEs("La katana maldita", THEME_PINK.bright, 1);
    g_tft.drawFastHLine(162, 119, 110, THEME_PINK.dim);
    g_tft.setCursor(162, 140);
    printEs("[START] Iniciar", THEME_PINK.glow, 1);
  } else {
    g_tft.setCursor(34, 46);
    printEs("Año 2027", THEME_PINK.glow, 3);
    constexpr const char* story[] = {
      "Una IA intenta hackear",
      "el mundo. Sus criaturas",
      "salen de un vórtice.",
      "Lily entra al templo",
      "con la katana maldita",
      "para cerrar el vórtice."
    };
    for (int i = 0; i < 6; ++i) {
      g_tft.setCursor(34, 83 + i * 15);
      printEs(story[i], THEME_PINK.bright, 1);
    }
    drawLilySprite(LilyPose::Tajo, 192, 79, 96, THEME_PINK.bg);
    g_tft.setCursor(227, 177);
    printEs("Lily", THEME_PINK.glow, 1);
    g_tft.setCursor(34, 181);
    printEs("[START] Entrar al templo", THEME_PINK.glow, 1);
  }
  g_tft.setCursor(217, 214);
  printEs("[SELECT] Salir", THEME_PINK.mid, 1);
}

} // namespace

void doomRenderFull() {
  if (g_phase != DoomPhase::Playing) {
    drawIntro();
    return;
  }
  g_tft.fillScreen(g_theme.bg);
  drawHud();
  if (g_dead) drawEndOverlay(false);
  else if (g_win) drawEndOverlay(true);
}

static void startLevel() {
  g_phase = DoomPhase::Playing;
  g_stride = 0.0f;
  g_strideBlend = 0.0f;
  g_pX = START_X;
  g_pY = START_Y;
  g_pDirX = 1.0f;
  g_pDirY = 0.0f;
  g_pPlaneX = 0.0f;
  g_pPlaneY = 0.66f;
  g_hp = 100;
  g_kills = 0;
  g_hurtFlash = 0;
  g_dead = false;
  g_win = false;
  g_startMs = millis();
  g_lastFrameMs = g_startMs;

  g_lastSwingMs = g_startMs - KATANA_COOLDOWN_MS;
  g_katanaPending = false;
  g_lastDashMs = g_startMs - DASH_COOLDOWN_MS;
  g_dashRemaining = 0;
  g_dashReady = true;
  generateLevel();
  doomRenderFull();
  Serial.printf("[Lily DOOM] Nivel %lu generado. Avanzar GPIO %d, retroceder GPIO %d.\n",
                static_cast<unsigned long>(g_level), BTN_UP, BTN_DOWN);
}

void doomStart() {
  g_level = 1;
  g_phase = DoomPhase::Title;
  doomRenderFull();
}

bool doomHandleButton(int pin) {
  if (pin == BTN_SELECT) return true;

  if (g_phase == DoomPhase::Title) {
    if (pin == BTN_START) {
      g_phase = DoomPhase::Prologue;
      g_prologueMs = millis();
      doomRenderFull();
    }
    return false;
  }
  if (g_phase == DoomPhase::Prologue) {
    if (pin == BTN_START) startLevel();
    return false;
  }

  if (g_dead || g_win) {
    if (pin == BTN_A || pin == BTN_START) {
      if (g_win) {
        ++g_level;
        startLevel();
      } else {
        g_level = 1;
        startLevel();
      }
    }
    return false;
  }

  if (pin == BTN_A && millis() - g_lastSwingMs >= KATANA_COOLDOWN_MS) {
    g_lastSwingMs = millis();
    g_katanaPending = true;
  }
  if (pin == BTN_B && millis() - g_lastDashMs >= DASH_COOLDOWN_MS) {
    const float direction = inputButtonHeld(BTN_DOWN) ? -1.0f : 1.0f;
    g_dashX = direction * g_pDirX;
    g_dashY = direction * g_pDirY;
    g_dashRemaining = 1.75f;
    g_lastDashMs = millis();
  }
  if (pin == BTN_START) {
    g_showMap = !g_showMap;
  }
  return false;
}

void doomTick() {
  if (g_phase == DoomPhase::Prologue && millis() - g_prologueMs >= PROLOGUE_MS) startLevel();
  if (g_phase != DoomPhase::Playing) {
    delay(15);
    return;
  }
  if (!g_dead && !g_win) {
    updateAndRenderFrame();
  } else {
    delay(20);
  }
}
