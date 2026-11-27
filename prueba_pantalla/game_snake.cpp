#include "game_snake.h"
#include "board_config.h"
#include "display_driver.h"

#include <Arduino.h>
#include <stdio.h>

namespace {

constexpr int16_t SNAKE_CELL   = 10;
constexpr int16_t SNAKE_COLS   = 30;  // 30 * 10 = 300 px (x: 10..310)
constexpr int16_t SNAKE_ROWS   = 19;  // 19 * 10 = 190 px (y: 26..216)
constexpr int16_t SNAKE_OX     = 10;
constexpr int16_t SNAKE_OY     = 26;
constexpr int16_t SNAKE_MAXLEN = SNAKE_COLS * SNAKE_ROWS;

int8_t g_snakeX[SNAKE_MAXLEN];
int8_t g_snakeY[SNAKE_MAXLEN];
int16_t g_snakeLen = 0;
int8_t g_dirX = 1;
int8_t g_dirY = 0;
int8_t g_nextDirX = 1;
int8_t g_nextDirY = 0;
int8_t g_foodX = 15;
int8_t g_foodY = 9;
uint16_t g_score = 0;
uint16_t g_best = 0;
bool g_gameOver = false;
bool g_paused = false;
uint32_t g_lastStepMs = 0;

bool isOccupying(int8_t cx, int8_t cy, int16_t ignoreTail = 0) {
  const int16_t limit = g_snakeLen - ignoreTail;
  for (int16_t i = 0; i < limit; ++i) {
    if (g_snakeX[i] == cx && g_snakeY[i] == cy) return true;
  }
  return false;
}

void drawCell(int8_t cx, int8_t cy, uint16_t fillCol, uint16_t borderCol = 0xFFFF) {
  const int16_t px = SNAKE_OX + cx * SNAKE_CELL;
  const int16_t py = SNAKE_OY + cy * SNAKE_CELL;
  g_tft.fillRect(px + 1, py + 1, SNAKE_CELL - 2, SNAKE_CELL - 2, fillCol);
  if (borderCol != 0xFFFF) {
    g_tft.drawRect(px + 1, py + 1, SNAKE_CELL - 2, SNAKE_CELL - 2, borderCol);
  }
}

void eraseCell(int8_t cx, int8_t cy) {
  const int16_t px = SNAKE_OX + cx * SNAKE_CELL;
  const int16_t py = SNAKE_OY + cy * SNAKE_CELL;
  g_tft.fillRect(px, py, SNAKE_CELL, SNAKE_CELL, g_theme.bg);
}

void drawFood() {
  const int16_t px = SNAKE_OX + g_foodX * SNAKE_CELL;
  const int16_t py = SNAKE_OY + g_foodY * SNAKE_CELL;
  g_tft.fillRect(px + 2, py + 2, SNAKE_CELL - 4, SNAKE_CELL - 4, g_theme.glow);
  g_tft.drawRect(px + 1, py + 1, SNAKE_CELL - 2, SNAKE_CELL - 2, g_theme.bright);
}

void spawnFood() {
  for (int tries = 0; tries < 200; ++tries) {
    const int8_t fx = static_cast<int8_t>(random(0, SNAKE_COLS));
    const int8_t fy = static_cast<int8_t>(random(0, SNAKE_ROWS));
    if (!isOccupying(fx, fy)) {
      g_foodX = fx;
      g_foodY = fy;
      return;
    }
  }
  for (int8_t y = 0; y < SNAKE_ROWS; ++y) {
    for (int8_t x = 0; x < SNAKE_COLS; ++x) {
      if (!isOccupying(x, y)) {
        g_foodX = x;
        g_foodY = y;
        return;
      }
    }
  }
}

void drawHud() {
  g_tft.fillRect(0, 0, SCREEN_W, 22, g_theme.bg);
  g_tft.drawFastHLine(0, 21, SCREEN_W, g_theme.dim);

  g_tft.setCursor(8, 7);
  printEs("LILYXM7 // SNAKE", g_theme.bright, 1);

  char buf[40];
  snprintf(buf, sizeof(buf), "PTS:%u  MAX:%u", g_score, g_best);
  g_tft.setCursor(184, 7);
  printEs(buf, g_theme.glow, 1);
}

void drawFooter() {
  g_tft.fillRect(0, 226, SCREEN_W, 14, g_theme.bg);
  g_tft.drawFastHLine(0, 225, SCREEN_W, g_theme.dark);
  g_tft.setCursor(6, 229);
  printEs("[↑↓←→]Mover [ST]Pausa [B]Menú [MENU]Color", g_theme.dim, 1);
}

void drawGameOverBox() {
  constexpr int16_t bx = 56, by = 68, bw = 208, bh = 104;
  g_tft.fillRect(bx, by, bw, bh, g_theme.bg);
  g_tft.drawRect(bx, by, bw, bh, g_theme.glow);
  g_tft.fillRect(bx + 2, by + 2, bw - 4, 24, g_theme.selBg);

  g_tft.setCursor(bx + 50, by + 7);
  printEs("GAME OVER", g_theme.glow, 2);

  char buf[36];
  snprintf(buf, sizeof(buf), "Puntos: %u   Record: %u", g_score, g_best);
  g_tft.setCursor(bx + 28, by + 40);
  printEs(buf, g_theme.bright, 1);

  g_tft.setCursor(bx + 36, by + 64);
  printEs("[A] Jugar de nuevo", g_theme.glow, 1);
  g_tft.setCursor(bx + 36, by + 80);
  printEs("[B] Volver al menú", g_theme.mid, 1);
}

void drawPauseBanner() {
  constexpr int16_t bx = 104, by = 102, bw = 112, bh = 32;
  g_tft.fillRect(bx, by, bw, bh, g_theme.bg);
  g_tft.drawRect(bx, by, bw, bh, g_theme.bright);
  g_tft.setCursor(bx + 26, by + 9);
  printEs("PAUSA", g_theme.glow, 2);
}

void stepSnake() {
  if (g_gameOver || g_paused) return;

  g_dirX = g_nextDirX;
  g_dirY = g_nextDirY;
  const int8_t nx = static_cast<int8_t>(g_snakeX[0] + g_dirX);
  const int8_t ny = static_cast<int8_t>(g_snakeY[0] + g_dirY);

  if (nx < 0 || nx >= SNAKE_COLS || ny < 0 || ny >= SNAKE_ROWS || isOccupying(nx, ny, 1)) {
    g_gameOver = true;
    if (g_score > g_best) g_best = g_score;
    drawHud();
    drawGameOverBox();
    Serial.printf("[SNAKE] GAME OVER | pts=%u max=%u\n", g_score, g_best);
    return;
  }

  const bool ate = (nx == g_foodX && ny == g_foodY);
  const int8_t oldTailX = g_snakeX[g_snakeLen - 1];
  const int8_t oldTailY = g_snakeY[g_snakeLen - 1];
  const int8_t oldHeadX = g_snakeX[0];
  const int8_t oldHeadY = g_snakeY[0];

  if (ate && g_snakeLen < SNAKE_MAXLEN) {
    g_snakeLen++;
    g_score++;
    if (g_score > g_best) g_best = g_score;
  } else {
    eraseCell(oldTailX, oldTailY);
  }

  for (int16_t i = g_snakeLen - 1; i > 0; --i) {
    g_snakeX[i] = g_snakeX[i - 1];
    g_snakeY[i] = g_snakeY[i - 1];
  }
  g_snakeX[0] = nx;
  g_snakeY[0] = ny;

  drawCell(oldHeadX, oldHeadY, g_theme.mid, g_theme.bright);
  drawCell(nx, ny, g_theme.glow, g_theme.bright);

  if (ate) {
    spawnFood();
    drawFood();
    drawHud();
    Serial.printf("[SNAKE] +1 punto! pts=%u\n", g_score);
  }
}

} // namespace

void snakeRenderFull() {
  g_tft.fillScreen(g_theme.bg);
  drawHud();
  g_tft.drawRect(SNAKE_OX - 2, SNAKE_OY - 2,
                 SNAKE_COLS * SNAKE_CELL + 4, SNAKE_ROWS * SNAKE_CELL + 4,
                 g_theme.dim);

  for (int16_t i = g_snakeLen - 1; i >= 0; --i) {
    if (i == 0) drawCell(g_snakeX[0], g_snakeY[0], g_theme.glow, g_theme.bright);
    else drawCell(g_snakeX[i], g_snakeY[i], g_theme.mid, g_theme.bright);
  }
  drawFood();
  drawFooter();

  if (g_gameOver) drawGameOverBox();
  else if (g_paused) drawPauseBanner();
}

void snakeStart() {
  randomSeed(micros());
  g_snakeLen = 4;
  for (int16_t i = 0; i < g_snakeLen; ++i) {
    g_snakeX[i] = static_cast<int8_t>(12 - i);
    g_snakeY[i] = 9;
  }
  g_dirX = 1;
  g_dirY = 0;
  g_nextDirX = 1;
  g_nextDirY = 0;
  g_score = 0;
  g_gameOver = false;
  g_paused = false;
  g_lastStepMs = millis();
  spawnFood();
  snakeRenderFull();
  Serial.println(F("[SNAKE] Nueva partida iniciada."));
}

bool snakeHandleButton(int pin) {
  if (pin == BTN_B) return true;

  if (g_gameOver) {
    if (pin == BTN_A || pin == BTN_START) {
      snakeStart();
    }
    return false;
  }

  if (pin == BTN_START) {
    g_paused = !g_paused;
    if (g_paused) drawPauseBanner();
    else snakeRenderFull();
    return false;
  }

  if (!g_paused) {
    if (pin == BTN_UP && g_dirY != 1)          { g_nextDirX = 0;  g_nextDirY = -1; }
    else if (pin == BTN_DOWN && g_dirY != -1)  { g_nextDirX = 0;  g_nextDirY = 1;  }
    else if (pin == BTN_LEFT && g_dirX != 1)   { g_nextDirX = -1; g_nextDirY = 0;  }
    else if (pin == BTN_RIGHT && g_dirX != -1) { g_nextDirX = 1;  g_nextDirY = 0;  }
  }
  return false;
}

void snakeTick() {
  const uint32_t now = millis();
  uint32_t stepMs = 160;
  const uint32_t speedup = (g_score / 3) * 10;
  if (speedup < 75) stepMs -= speedup;
  else stepMs = 85;

  if (!g_gameOver && !g_paused && (now - g_lastStepMs) >= stepMs) {
    g_lastStepMs = now;
    stepSnake();
  }
  delay(10);
}
