/* [Archipelago] The chat box (runtime/ap_chat.c). */
#pragma once
#include <stdint.h>

/* the colours of a line's parts */
enum { CHAT_TEXT, CHAT_ME, CHAT_PLAYER, CHAT_PROGRESSION, CHAT_USEFUL, CHAT_FILLER, CHAT_TRAP, CHAT_LOCATION, CHAT_COLORS };
#define CHAT_SHOW_MS 12000                             /* a message keeps the box up this long ... */
#define CHAT_FADE_MS 1500                              /* ... the last part of it fading out */

typedef struct { char text[200]; uint8_t color[200]; unsigned long long t; } ApChatLine;

void ap_chat_add(const char *text, const uint8_t *colors);   /* colors: one per character of text */

/* The panel for a W x H picture (render thread): premultiplied RGBA, w x h, placed at x, y (top left); version
 * changes when the pixels do; alpha is the fade (0 = nothing to show). NULL: no font (the mod lacks it). */
typedef struct { const uint8_t *px; int w, h, x, y; unsigned version; float alpha; } ApChatPanel;
const ApChatPanel *ap_chat_panel(int W, int H);

/* the banner: one line at the top middle (co-op waits), "" = none; its panel as ap_chat_panel's */
void ap_banner_set(const char *text);
const ApChatPanel *ap_banner_panel(int W, int H);
