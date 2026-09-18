#ifndef ICONS_H
#define ICONS_H

#include <Arduino.h>

// ================= ИКОНКИ АНИМАЦИИ =================

// Pac-Man (16x16 пикселей)
#include "icons/pacman_open.xbm"
#include "icons/pacman_closed.xbm"

// Велосипедист/Колокольчик (7x7 пикселей)
#include "icons/biker_frame1.xbm"
#include "icons/biker_frame2.xbm"

// Структура для удобного доступа к иконкам
struct Icon {
  const unsigned char* data;
  int width;
  int height;
};

// Глобальные константы иконок
static const Icon ICON_PACMAN_OPEN = {
  pacman_open_bits,
  pacman_open_width,
  pacman_open_height
};

static const Icon ICON_PACMAN_CLOSED = {
  pacman_closed_bits,
  pacman_closed_width,
  pacman_closed_height
};

static const Icon ICON_BIKER_FRAME1 = {
  biker_frame1_bits,
  biker_frame1_width,
  biker_frame1_height
};

static const Icon ICON_BIKER_FRAME2 = {
  biker_frame2_bits,
  biker_frame2_width,
  biker_frame2_height
};

// Утилита для получения пикселя из XBM
inline bool getIconPixel(const Icon& icon, int row, int col) {
  if (row < 0 || row >= icon.height || col < 0 || col >= icon.width) {
    return false;
  }
  int byteIndex = row * ((icon.width + 7) / 8) + (col / 8);
  int bitIndex = col % 8;
  return (icon.data[byteIndex] >> bitIndex) & 1;
}

#endif // ICONS_H
