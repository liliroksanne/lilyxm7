#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT.h>

// ============================================================================
// Prueba de pantalla + botones para placa LAFVIN "Retro Game Emulator" (ESP32-S3)
// Pinout oficial de lafvintech/LAFVIN-Retro-Game-Kit (branch dev, esp32-s3-devkit/config.h)
// ============================================================================

// Pines de la pantalla ST7789 (240x320) soldados en la placa Retro Game Emulator
constexpr int TFT_BL   = 6;   // Backlight
constexpr int TFT_CS   = 7;   // Chip Select
constexpr int TFT_DC   = 15;  // Data/Command (RS)
constexpr int TFT_RST  = 16;  // Reset
constexpr int TFT_MOSI = 17;  // SDA / DIN
constexpr int TFT_SCK  = 18;  // SCL / CLK
constexpr int TFT_MISO = -1;  // No conectado en la placa

// Botones soldados en la placa Retro Game Emulator (todos INPUT_PULLUP, activo LOW)
struct BoardButton {
  const char* name;
  int pin;
  bool lastState;
};

BoardButton g_btns[] = {
  { "UP",     11, true },
  { "DOWN",   12, true },
  { "LEFT",   13, true },
  { "RIGHT",  14, true },
  { "A",      21, true },
  { "B",      47, true },
  { "START",  39, true },
  { "SELECT", 40, true },
  { "MENU",    5, true },
  { "OPTION",  4, true },
};

// Driver ST7789 240x320 con la secuencia exacta de inicializacion de LAFVIN
class LafvinST7789 : public Adafruit_SPITFT {
public:
  LafvinST7789(int16_t cs, int16_t dc, int16_t rst)
      : Adafruit_SPITFT(240, 320, cs, dc, rst) {}

  void begin(uint32_t freq = 24000000) override {
    if (!freq) freq = 24000000;
    initSPI(freq, SPI_MODE0);

    // Secuencia oficial LAFVIN Retro Game Kit (ST7789 240x320)
    sendCommand(0x01); // SWRESET
    delay(150);
    sendCommand(0x11); // SLPOUT
    delay(120);

    const uint8_t colmod = 0x55; // 16-bit RGB565
    sendCommand(0x3A, &colmod, 1);
    delay(10);

    invertOnCommand  = 0x21;
    invertOffCommand = 0x20;
    sendCommand(invertOffCommand); // LAFVIN usa 0x20

    const uint8_t madctl = 0x60; // Horizontal 320x240, orden RGB
    sendCommand(0x36, &madctl, 1);

    static const uint8_t porch[]  = { 0x0C, 0x0C, 0x00, 0x33, 0x33 };
    static const uint8_t gate[]   = { 0x35 };
    static const uint8_t vcom[]   = { 0x19 };
    static const uint8_t lcm[]    = { 0x2C };
    static const uint8_t vdvEn[]  = { 0x01 };
    static const uint8_t vrh[]    = { 0x12 };
    static const uint8_t vdv[]    = { 0x20 };
    static const uint8_t frate[]  = { 0x0F };
    static const uint8_t pwctrl[] = { 0xA4, 0xA1 };
    static const uint8_t pgamma[] = { 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
                                      0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23 };
    static const uint8_t ngamma[] = { 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
                                      0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23 };

    sendCommand(0xB2, porch, sizeof(porch));
    sendCommand(0xB7, gate, sizeof(gate));
    sendCommand(0xBB, vcom, sizeof(vcom));
    sendCommand(0xC0, lcm, sizeof(lcm));
    sendCommand(0xC2, vdvEn, sizeof(vdvEn));
    sendCommand(0xC3, vrh, sizeof(vrh));
    sendCommand(0xC4, vdv, sizeof(vdv));
    sendCommand(0xC6, frate, sizeof(frate));
    sendCommand(0xD0, pwctrl, sizeof(pwctrl));
    sendCommand(0xE0, pgamma, sizeof(pgamma));
    sendCommand(0xE1, ngamma, sizeof(ngamma));

    sendCommand(0x13); // NORON
    delay(10);
    sendCommand(0x29); // DISPON
    delay(100);

    setRotation(1);
  }

  void setRotation(uint8_t m) override {
    rotation = m & 3;
    uint8_t madctl = 0x60;
    switch (rotation) {
      case 0: madctl = 0x00; _width = 240; _height = 320; break; // Vertical
      case 1: madctl = 0x60; _width = 320; _height = 240; break; // Horizontal LAFVIN oficial
      case 2: madctl = 0xC0; _width = 240; _height = 320; break; // Vertical 180°
      case 3: madctl = 0xA0; _width = 320; _height = 240; break; // Horizontal 180°
    }
    _xstart = 0;
    _ystart = 0;
    sendCommand(0x36, &madctl, 1);
  }

  void setAddrWindow(uint16_t x1, uint16_t y1, uint16_t w, uint16_t h) override {
    x1 += _xstart;
    y1 += _ystart;
    const uint16_t x2 = x1 + w - 1;
    const uint16_t y2 = y1 + h - 1;
    writeCommand(0x2A); SPI_WRITE16(x1); SPI_WRITE16(x2);
    writeCommand(0x2B); SPI_WRITE16(y1); SPI_WRITE16(y2);
    writeCommand(0x2C);
  }
};

LafvinST7789 pantalla(TFT_CS, TFT_DC, TFT_RST);

uint8_t g_rot = 1;
bool g_inverted = false;

void dibujarPantalla(const char* ultimoBoton = "Ninguno") {
  const uint16_t negro  = pantalla.color565(0, 0, 0);
  const uint16_t rosa   = pantalla.color565(240, 120, 184);
  const uint16_t rosaOk = pantalla.color565(255, 190, 224);
  const uint16_t suave  = pantalla.color565(140, 70, 110);

  pantalla.fillScreen(negro);
  pantalla.drawRect(0, 0, 320, 240, rosa);
  pantalla.drawRect(4, 4, 312, 232, suave);

  pantalla.setTextColor(rosa);
  pantalla.setTextSize(3);
  pantalla.setCursor(32, 34);
  pantalla.print(F("Hola, soy Lily"));

  pantalla.setTextColor(rosaOk);
  pantalla.setTextSize(2);
  pantalla.setCursor(38, 76);
  pantalla.print(F("LAFVIN Retro PCB OK"));

  pantalla.setTextColor(suave);
  pantalla.setTextSize(1);
  pantalla.setCursor(22, 108);
  pantalla.printf("ST7789 320x240 | Rot=%u | Inv=%s", g_rot, g_inverted ? "ON" : "OFF");

  // Caja de prueba en vivo de los 10 botones de la placa
  pantalla.drawRect(18, 128, 284, 52, rosa);
  pantalla.setTextColor(suave);
  pantalla.setCursor(28, 136);
  pantalla.print(F("Ultimo boton presionado:"));
  pantalla.setTextColor(rosaOk);
  pantalla.setTextSize(2);
  pantalla.setCursor(28, 152);
  pantalla.print(ultimoBoton);

  // Atajos utiles en vivo
  pantalla.setTextColor(suave);
  pantalla.setTextSize(1);
  pantalla.setCursor(18, 192);
  pantalla.print(F("[OPTION]=Girar 180   [MENU]=Invertir color"));

  // Cuadritos RGB de verificacion
  pantalla.fillRect(256, 212, 14, 14, pantalla.color565(255, 0, 0)); // Rojo
  pantalla.fillRect(274, 212, 14, 14, pantalla.color565(0, 255, 0)); // Verde
  pantalla.fillRect(292, 212, 14, 14, pantalla.color565(0, 0, 255)); // Azul
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("[PRUEBA] LAFVIN Retro Game Emulator (ST7789 + 10 botones)"));

  for (auto& b : g_btns) {
    pinMode(b.pin, INPUT_PULLUP);
    b.lastState = (digitalRead(b.pin) == HIGH);
  }

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  pantalla.begin();
  pantalla.setRotation(g_rot);
  pantalla.invertDisplay(g_inverted);
  pantalla.setTextWrap(false);

  dibujarPantalla("Listo! Presiona botones");
}

void loop() {
  for (auto& b : g_btns) {
    const bool nowHigh = (digitalRead(b.pin) == HIGH);
    if (b.lastState && !nowHigh) {
      Serial.printf("[BTN] %s (GPIO %d)\n", b.name, b.pin);
      if (b.pin == 4) { // OPTION: alternar horizontal 1 <-> 3
        g_rot = (g_rot == 1) ? 3 : 1;
        pantalla.setRotation(g_rot);
      } else if (b.pin == 5) { // MENU: alternar inversion de color
        g_inverted = !g_inverted;
        pantalla.invertDisplay(g_inverted);
      }
      char msg[32];
      snprintf(msg, sizeof(msg), "%s (GPIO %d)", b.name, b.pin);
      dibujarPantalla(msg);
      delay(160);
    }
    b.lastState = nowHigh;
  }
  delay(15);
}
