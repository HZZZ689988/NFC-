#ifndef TEST_GUI_H
#define TEST_GUI_H

#define GUI_COLOR_WHITE 1

int GUI_Init(void);
void GUI_Clear(void);
void GUI_SetColor(int color);
void GUI_DispStringAt(const char *text, int x, int y);
void GUI_Update(void);

#endif
