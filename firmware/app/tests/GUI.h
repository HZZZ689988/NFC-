#ifndef TEST_GUI_H
#define TEST_GUI_H

#define GUI_COLOR_WHITE 1
#define GUI_FLASH
#define GUI_UNI_PTR

typedef struct GUI_FONT GUI_FONT;

int GUI_Init(void);
void GUI_Clear(void);
void GUI_SetColor(int color);
void GUI_DispCharAt(unsigned short c, int x, int y);
void GUI_DispStringAt(const char *text, int x, int y);
const GUI_FONT *GUI_SetFont(const GUI_FONT *font);
void GUI_Update(void);

extern const GUI_FONT GUI_FontHZ_SimSun_8;

#endif
