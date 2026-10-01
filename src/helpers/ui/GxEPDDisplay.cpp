
#include "GxEPDDisplay.h"
#ifdef THINKNODE_M1
  #include "PolishGlyphs.h"
#endif

#ifdef EXP_PIN_BACKLIGHT
  #include <PCA9557.h>
  extern PCA9557 expander;
#endif

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 3
#endif

#ifdef ESP32
  SPIClass SPI1 = SPIClass(FSPI);
#endif

// Color scheme
ColorVal UIColor::window_bkg = GxEPD_WHITE;
ColorVal UIColor::title_bkg = GxEPD_WHITE;
ColorVal UIColor::title_txt = GxEPD_BLACK;
ColorVal UIColor::primary_txt = GxEPD_BLACK;
ColorVal UIColor::secondary_txt = GxEPD_BLACK;
ColorVal UIColor::warning_txt = GxEPD_BLACK;
ColorVal UIColor::popup_bkg = GxEPD_WHITE;
ColorVal UIColor::popup_txt = GxEPD_BLACK;
ColorVal UIColor::corp_blue = GxEPD_BLACK;


bool GxEPDDisplay::begin() {
  display.epd2.selectSPI(SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
#ifdef ESP32
  SPI1.begin(PIN_DISPLAY_SCLK, PIN_DISPLAY_MISO, PIN_DISPLAY_MOSI, PIN_DISPLAY_CS);
#else
  SPI1.begin();
#endif
  display.init(115200, true, 2, false);
  display.setRotation(DISPLAY_ROTATION);
  setTextSize(1);  // Default to size 1
  display.setPartialWindow(0, 0, display.width(), display.height());

  display.fillScreen(GxEPD_WHITE);
  display.display(true);
  #if DISP_BACKLIGHT
  digitalWrite(DISP_BACKLIGHT, LOW);
  pinMode(DISP_BACKLIGHT, OUTPUT);
  #endif
  _init = true;
  return true;
}

void GxEPDDisplay::turnOn() {
  if (!_init) begin();
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  digitalWrite(DISP_BACKLIGHT, HIGH);
#elif defined(EXP_PIN_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  expander.digitalWrite(EXP_PIN_BACKLIGHT, HIGH);
#endif
  _isOn = true;
}

void GxEPDDisplay::turnOff() {
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  digitalWrite(DISP_BACKLIGHT, LOW);
#elif defined(EXP_PIN_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  expander.digitalWrite(EXP_PIN_BACKLIGHT, LOW);
#endif
  _isOn = false;
}

void GxEPDDisplay::clear() {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display_crc.reset();
}

void GxEPDDisplay::startFrame(ColorVal bkg) {
  display.fillScreen(bkg);
  display.setTextColor(_curr_color = UIColor::primary_txt);
  display_crc.reset();
}

void GxEPDDisplay::setTextSize(int sz) {
  display_crc.update<int>(sz);
  _font_size = sz;
  switch(sz) {
    case 1:  // Small
      display.setFont(&FreeSans9pt7b);
      break;
    case 2:  // Medium Bold
      display.setFont(&FreeSansBold12pt7b);
      break;
    case 3:  // Large
      display.setFont(&FreeSans18pt7b);
      break;
    default:
      display.setFont(&FreeSans9pt7b);
      break;
  }
}

void GxEPDDisplay::setColor(ColorVal c) {
  display_crc.update<ColorVal> (c);
  display.setTextColor(_curr_color = c);
}

void GxEPDDisplay::setCursor(int x, int y) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display.setCursor((x+offset_x)*scale_x, (y+offset_y)*scale_y);
}


#ifdef THINKNODE_M1
static uint16_t decodeUtf8Codepoint(const char* s, size_t len, size_t& i) {
  uint8_t c = (uint8_t)s[i++];
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && i < len) {
    uint8_t c2 = (uint8_t)s[i++];
    if ((c2 & 0xC0) == 0x80) return ((uint16_t)(c & 0x1F) << 6) | (c2 & 0x3F);
    return '?';
  }
  if ((c & 0xF0) == 0xE0 && i + 1 < len) {
    uint8_t c2 = (uint8_t)s[i++];
    uint8_t c3 = (uint8_t)s[i++];
    if ((c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
      return ((uint16_t)(c & 0x0F) << 12) | ((uint16_t)(c2 & 0x3F) << 6) | (c3 & 0x3F);
    }
    return '?';
  }
  while (i < len && (((uint8_t)s[i] & 0xC0) == 0x80)) i++;
  return '?';
}

static bool getPolishGlyphForSize(int fontSize, uint16_t cp, PolishGlyphBitmap& out, const uint8_t*& bitmap) {
  const PolishGlyphBitmap* table = nullptr;
  uint8_t count = 0;
  if (fontSize == 2) {
    table = PolishGlyphs2; count = PolishGlyphCount2; bitmap = PolishBitmap2;
  } else if (fontSize == 3) {
    table = PolishGlyphs3; count = PolishGlyphCount3; bitmap = PolishBitmap3;
  } else {
    table = PolishGlyphs1; count = PolishGlyphCount1; bitmap = PolishBitmap1;
  }
  for (uint8_t i = 0; i < count; i++) {
    PolishGlyphBitmap g = table[i];
    if (g.codepoint == cp) {
      out = g;
      return true;
    }
  }
  return false;
}

static uint8_t baseAsciiForPolish(uint16_t cp) {
  switch (cp) {
    case 0x0104: case 0x0105: return (cp == 0x0104) ? 'A' : 'a';
    case 0x0106: case 0x0107: return (cp == 0x0106) ? 'C' : 'c';
    case 0x0118: case 0x0119: return (cp == 0x0118) ? 'E' : 'e';
    case 0x0141: case 0x0142: return (cp == 0x0141) ? 'L' : 'l';
    case 0x0143: case 0x0144: return (cp == 0x0143) ? 'N' : 'n';
    case 0x00D3: case 0x00F3: return (cp == 0x00D3) ? 'O' : 'o';
    case 0x015A: case 0x015B: return (cp == 0x015A) ? 'S' : 's';
    case 0x0179: case 0x017A: return (cp == 0x0179) ? 'Z' : 'z';
    case 0x017B: case 0x017C: return (cp == 0x017B) ? 'Z' : 'z';
    default: return '?';
  }
}
#endif

void GxEPDDisplay::print(const char* str) {
  if (str == nullptr) return;
  display_crc.update<char>(str, strlen(str));
#ifdef THINKNODE_M1
  const size_t len = strlen(str);
  size_t i = 0;
  while (i < len) {
    uint16_t cp = decodeUtf8Codepoint(str, len, i);
    if (cp < 0x80) {
      display.write((uint8_t)cp);
      continue;
    }

    PolishGlyphBitmap g;
    const uint8_t* bitmap = nullptr;
    if (getPolishGlyphForSize(_font_size, cp, g, bitmap)) {
      int16_t x = display.getCursorX();
      int16_t y = display.getCursorY();
      display.drawBitmap(x + g.xOffset, y + g.yOffset, bitmap + g.offset, g.width, g.height, _curr_color);
      display.setCursor(x + g.xAdvance, y);
    } else {
      display.write((uint8_t)'?');
    }
  }
#else
  display.print(str);
#endif
}

void GxEPDDisplay::printWordWrap(const char* str, int max_width) {
#ifndef THINKNODE_M1
  print(str);
#else
  if (str == nullptr || max_width <= 0) return;

  const int16_t lineStartX = display.getCursorX();
  int16_t lineY = display.getCursorY();
  const int16_t lineHeight = (_font_size == 3) ? 42 : ((_font_size == 2) ? 29 : 22);
  const int16_t rightEdge = lineStartX + max_width;

  char word[128];
  size_t wi = 0;
  const char* p = str;

  auto flushWord = [&]() {
    if (wi == 0) return;
    word[wi] = 0;
    uint16_t ww = getTextWidth(word);
    if (display.getCursorX() != lineStartX && display.getCursorX() + ww > rightEdge) {
      lineY += lineHeight;
      display.setCursor(lineStartX, lineY);
    }
    print(word);
    wi = 0;
  };

  while (*p) {
    if (*p == '\n') {
      flushWord();
      lineY += lineHeight;
      display.setCursor(lineStartX, lineY);
      p++;
      continue;
    }
    if (*p == ' ' || *p == '\t') {
      flushWord();
      uint16_t sw = getTextWidth(" ");
      if (display.getCursorX() + sw > rightEdge) {
        lineY += lineHeight;
        display.setCursor(lineStartX, lineY);
      } else {
        print(" ");
      }
      p++;
      continue;
    }

    uint8_t c = (uint8_t)*p;
    size_t bytes = 1;
    if ((c & 0xE0) == 0xC0) bytes = 2;
    else if ((c & 0xF0) == 0xE0) bytes = 3;
    else if ((c & 0xF8) == 0xF0) bytes = 4;

    if (wi + bytes >= sizeof(word) - 1) flushWord();
    for (size_t k = 0; k < bytes && *p; k++) word[wi++] = *p++;
  }
  flushWord();
#endif
}

void GxEPDDisplay::fillRect(int x, int y, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display.fillRect(x*scale_x, y*scale_y, w*scale_x, h*scale_y, _curr_color);
}

void GxEPDDisplay::drawRect(int x, int y, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display.drawRect(x*scale_x, y*scale_y, w*scale_x, h*scale_y, _curr_color);
}

void GxEPDDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display_crc.update<uint8_t>(bits, w * h / 8);
  // Calculate the base position in display coordinates
  uint16_t startX = x * scale_x;
  uint16_t startY = y * scale_y;
  
  // Width in bytes for bitmap processing
  uint16_t widthInBytes = (w + 7) / 8;
  
  // Process the bitmap row by row
  for (uint16_t by = 0; by < h; by++) {
    // Calculate the target y-coordinates for this logical row
    int y1 = startY + (int)(by * scale_y);
    int y2 = startY + (int)((by + 1) * scale_y);
    int block_h = y2 - y1;
    
    // Scan across the row bit by bit
    for (uint16_t bx = 0; bx < w; bx++) {
      // Calculate the target x-coordinates for this logical column
      int x1 = startX + (int)(bx * scale_x);
      int x2 = startX + (int)((bx + 1) * scale_x);
      int block_w = x2 - x1;
      
      // Get the current bit
      uint16_t byteOffset = (by * widthInBytes) + (bx / 8);
      uint8_t bitMask = 0x80 >> (bx & 7);
      bool bitSet = pgm_read_byte(bits + byteOffset) & bitMask;
      
      // If the bit is set, draw a block of pixels
      if (bitSet) {
        // Draw the block as a filled rectangle
        display.fillRect(x1, y1, block_w, block_h, _curr_color);
      }
    }
  }
}

uint16_t GxEPDDisplay::getTextWidth(const char* str) {
  if (str == nullptr || str[0] == 0) return 0;
#ifndef THINKNODE_M1
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return ceil((w + 1) / scale_x);
#else
  bool hasUtf8 = false;
  for (const uint8_t* p = (const uint8_t*)str; *p; ++p) {
    if (*p >= 0x80) { hasUtf8 = true; break; }
  }
  if (!hasUtf8) {
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
    return w + 1;
  }

  char ascii[256];
  size_t ai = 0;
  int adjustment = 0;
  size_t len = strlen(str), i = 0;
  while (i < len && ai < sizeof(ascii) - 1) {
    uint16_t cp = decodeUtf8Codepoint(str, len, i);
    if (cp < 0x80) {
      ascii[ai++] = (char)cp;
      continue;
    }

    PolishGlyphBitmap g;
    const uint8_t* bitmap = nullptr;
    uint8_t base = baseAsciiForPolish(cp);
    ascii[ai++] = (char)base;
    if (getPolishGlyphForSize(_font_size, cp, g, bitmap)) {
      char one[2] = {(char)base, 0};
      int16_t bx, by; uint16_t bw, bh;
      display.getTextBounds(one, 0, 0, &bx, &by, &bw, &bh);
      adjustment += (int)g.xAdvance - (int)(bw + 1);
    }
  }
  ascii[ai] = 0;

  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(ascii, 0, 0, &x1, &y1, &w, &h);
  int result = (int)w + 1 + adjustment;
  return result > 0 ? (uint16_t)result : 0;
#endif
}

void GxEPDDisplay::endFrame() {
  uint32_t crc = display_crc.finalize();
  if (crc != last_display_crc_value) {
    display.display(true);
    last_display_crc_value = crc;
  }
}
