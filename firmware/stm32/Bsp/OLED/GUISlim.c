
#include <stddef.h>           /* needed for definition of NULL */
#include "GUI.h"
#include "ssd1306.h"

GUI_CONTEXT GUI_Context;
char GUI_DecChar;

char* utf8_string_to_gbk_with_table(const char* utf8_str);
char* mixed_string_to_gbk(const char* str);

int LCD_GetXSize(void)        { return SSD1306_WIDTH; }
int LCD_GetYSize(void)        { return SSD1306_HEIGHT; }
int GUI_GetXSize(void)        { return LCD_GetXSize(); }
int GUI_GetYSize(void)        { return LCD_GetYSize(); }
void LCD_GetRect(LCD_RECT *pRect) {
  pRect->x0 = 0;
  pRect->y0 = 0;
  pRect->x1 = LCD_GetXSize() - 1;
  pRect->y1 = LCD_GetYSize() - 1;
}

int GUI_Init(void) {
  GUI_DecChar = '.';
	GUI_Context.ClipRect.x0 = 0;
	GUI_Context.ClipRect.y0 = 0;
	GUI_Context.ClipRect.x1 = LCD_GetXSize() - 1;
	GUI_Context.ClipRect.y1 = LCD_GetYSize() - 1;
	GUI_Context.DispPosX = 0;
	GUI_Context.DispPosY = 0;
	GUI_Context.pAFont = GUI_DEFAULT_FONT;
	GUI_Context.PenSize = 1;
	GUI_Context.DrawColor = GUI_COLOR_WHITE;
	
	SSD1306_init(); //初始化OLED
	
	return 0;
}

const GUI_FONT GUI_UNI_PTR* GUI_SetFont(const GUI_FONT GUI_UNI_PTR * pNewFont) {
  const GUI_FONT GUI_UNI_PTR* pOldFont = GUI_Context.pAFont;
  GUI_LOCK();
  if (pNewFont)
    GUI_Context.pAFont = pNewFont;
  GUI_UNLOCK();
  return pOldFont;
}
const GUI_FONT GUI_UNI_PTR* GUI_GetFont(void) {
  const GUI_FONT GUI_UNI_PTR * r;
  GUI_LOCK();
  r = GUI_Context.pAFont;
  GUI_UNLOCK();
  return r;
}


typedef struct {
    int utf8_only;       int gbk_only;        int both_valid;       int utf8_invalid;     int gbk_invalid;      int ascii_count;  } EncodingStats;

static int is_valid_utf8_sequence(const unsigned char* s, size_t len, size_t* consumed) {
    *consumed = 1;
        if (s[0] <= 0x7F) {
        return -1;     }
        if ((s[0] & 0x80) == 0x00) {
        return -1;     }
        if ((s[0] & 0xE0) == 0xC0) {
        if (len < 2) return 0;
        if ((s[1] & 0xC0) != 0x80) return 0;
                unsigned int code_point = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        if (code_point < 0x80) return 0;          if (code_point > 0x7FF) return 0;                  *consumed = 2;
        return 1;
    }
        if ((s[0] & 0xF0) == 0xE0) {
        if (len < 3) return 0;
        if ((s[1] & 0xC0) != 0x80) return 0;
        if ((s[2] & 0xC0) != 0x80) return 0;
                unsigned int code_point = ((s[0] & 0x0F) << 12) | 
                                 ((s[1] & 0x3F) << 6) | 
                                 (s[2] & 0x3F);
                if (code_point < 0x800) return 0;          if (code_point >= 0xD800 && code_point <= 0xDFFF) return 0;          if (code_point > 0xFFFF) return 0;                  *consumed = 3;
        return 1;
    }
        if ((s[0] & 0xF8) == 0xF0) {
        if (len < 4) return 0;
        if ((s[1] & 0xC0) != 0x80) return 0;
        if ((s[2] & 0xC0) != 0x80) return 0;
        if ((s[3] & 0xC0) != 0x80) return 0;
                unsigned int code_point = ((s[0] & 0x07) << 18) | 
                                 ((s[1] & 0x3F) << 12) | 
                                 ((s[2] & 0x3F) << 6) | 
                                 (s[3] & 0x3F);
                if (code_point < 0x10000) return 0;          if (code_point > 0x10FFFF) return 0;                  *consumed = 4;
        return 1;
    }
        return 0;
}

static int is_valid_gbk_sequence(const unsigned char* s, size_t len, size_t* consumed) {
    *consumed = 1;
        if (s[0] <= 0x80) {
        return -1;     }
        if (s[0] >= 0x81 && s[0] <= 0xFE) {
        if (len < 2) return 0;                  unsigned char second = s[1];
        if ((second >= 0x40 && second <= 0x7E) || 
            (second >= 0x80 && second <= 0xFE)) {
            *consumed = 2;
            return 1;
        }
        return 0;      }
        return 0;
}

static int get_gbk_char_frequency_score(const unsigned char* s) {
    if (s[0] >= 0xB0 && s[0] <= 0xD7) {
        if (s[1] >= 0xA1 && s[1] <= 0xFE) {
            return 3;         }
    }
    if (s[0] >= 0xD8 && s[0] <= 0xF7) {
        if (s[1] >= 0xA1 && s[1] <= 0xFE) {
            return 3;         }
    }
        if (s[0] >= 0x81 && s[0] <= 0xA0) {
        return 1;     }
    if (s[0] >= 0xAA && s[0] <= 0xFE) {
        return 1;     }
        return 2;
}

static int get_utf8_char_frequency_score(const unsigned char* s, size_t len) {
    if (len == 3 && (s[0] & 0xF0) == 0xE0) {
        unsigned int code_point = ((s[0] & 0x0F) << 12) | 
                         ((s[1] & 0x3F) << 6) | 
                         (s[2] & 0x3F);
                if (code_point >= 0x4E00 && code_point <= 0x9FFF) {
            return 3;         }
        if (code_point >= 0x3400 && code_point <= 0x4DBF) {
            return 2;         }
        if (code_point >= 0x3000 && code_point <= 0x303F) {
            return 2;         }
    }
    return 1;
}

int detect_encoding_comprehensive(const char* str) {
    if (!str) return -1;
    
    const unsigned char* s = (const unsigned char*)str;
    size_t len = strlen((const char*)s);
    if (len == 0) return 0;
    
    EncodingStats stats = {0, 0, 0, 0, 0, 0};
    int utf8_score = 0;
    int gbk_score = 0;
    
    size_t i = 0;
    while (i < len) {
        if (s[i] <= 0x7F) {
            stats.ascii_count++;
            i++;
            continue;
        }
        
        size_t utf8_consumed = 1;
        size_t gbk_consumed = 1;
        int is_utf8 = is_valid_utf8_sequence(s + i, len - i, &utf8_consumed);
        int is_gbk = is_valid_gbk_sequence(s + i, len - i, &gbk_consumed);
        
        if (is_utf8 == 1 && is_gbk == 1) {
                        stats.both_valid++;
                        int utf8_freq = get_utf8_char_frequency_score(s + i, utf8_consumed);
            int gbk_freq = get_gbk_char_frequency_score(s + i);
            utf8_score += utf8_freq;
            gbk_score += gbk_freq;
                        if (utf8_consumed == 3 && gbk_consumed == 2) {
                utf8_score += 2;             }
            i += utf8_consumed;
        }
        else if (is_utf8 == 1 && is_gbk != 1) {
                        stats.utf8_only++;
            utf8_score += 4;             i += utf8_consumed;
        }
        else if (is_gbk == 1 && is_utf8 != 1) {
                        stats.gbk_only++;
            gbk_score += 4;             i += gbk_consumed;
        }
        else if (is_utf8 == 0 && is_gbk == 0) {
                        stats.utf8_invalid++;
            stats.gbk_invalid++;
            i++;
        }
        else if (is_utf8 == 0) {
            stats.utf8_invalid++;
            i++;
        }
        else if (is_gbk == 0) {
            stats.gbk_invalid++;
            i++;
        }
        else {
            i++;
        }
    }
    
    int non_ascii = stats.utf8_only + stats.gbk_only + stats.both_valid;
    if (non_ascii == 0) {
        return 0;     }
    
        if (stats.utf8_only > 0 && stats.gbk_invalid > stats.utf8_invalid) {
        return 1;     }
    if (stats.gbk_only > 0 && stats.utf8_invalid > stats.gbk_invalid) {
        return 2;     }
        if (stats.both_valid > 0 && stats.utf8_only == 0 && stats.gbk_only == 0) {
                if (utf8_score > gbk_score) {
            return 1;
        } else if (gbk_score > utf8_score) {
            return 2;
        }
                return 1;     }
        if (stats.utf8_only > 0 && stats.gbk_only > 0) {
                int mixed_threshold = non_ascii / 5;
        if (stats.utf8_only > mixed_threshold && stats.gbk_only > mixed_threshold) {
            return 3;         }
                if (stats.utf8_only > stats.gbk_only * 2) {
            return 1;
        } else if (stats.gbk_only > stats.utf8_only * 2) {
            return 2;
        }
                return (utf8_score > gbk_score) ? 1 : 2;
    }
        if (stats.utf8_only > 0) {
        return 1;
    }
    if (stats.gbk_only > 0) {
        return 2;
    }
        if (utf8_score > gbk_score) {
        return 1;
    } else if (gbk_score > utf8_score) {
        return 2;
    }
    
    return 1; }

int detect_encoding_by_sequence(const char* str) {
    return detect_encoding_comprehensive(str);
}

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/
/*********************************************************************
*
*       GUIPROP_FindChar
*/
static const GUI_FONT_PROP GUI_UNI_PTR * GUIPROP_FindChar(const GUI_FONT_PROP GUI_UNI_PTR* pProp, U16P c) {
  for (; pProp; pProp = pProp->pNext) {
    if ((c>=pProp->First) && (c<=pProp->Last))
      break;
  }
  return pProp;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/
/*********************************************************************
*
*       GUIPROP_DispChar
*
* Purpose:
*   This is the routine that displays a character. It is used by all
*   other routines which display characters as a subroutine.
*/
void DrawBitLine1BPP(int x, int y, U8 const*p, int Diff, int xsize)
{
  U8 pixels;
	
/*
// Jump to right entry point
*/
  pixels = *p;

	switch (Diff&7) {
	case 0:
		goto WriteBit0;
	case 1:
		goto WriteBit1;
	case 2:
		goto WriteBit2;
	case 3:
		goto WriteBit3;
	case 4:
		goto WriteBit4;
	case 5:
		goto WriteBit5;
	case 6:
		goto WriteBit6;
	case 7:
		goto WriteBit7;
	}

/*
        Write without transparency
*/

  WriteBit0:
    SSD1306_DrawPixel(x+0, y, (pixels&(1<<7)) ? COLOR_F : COLOR_B);
    if (!--xsize)
      return;
  WriteBit1:
    SSD1306_DrawPixel(x+1, y, (pixels&(1<<6)) ? COLOR_F : COLOR_B);
    if (!--xsize)
      return;
  WriteBit2:
    SSD1306_DrawPixel(x+2, y, (pixels&(1<<5)) ? COLOR_F : COLOR_B);
    if (!--xsize)
      return;
  WriteBit3:
    SSD1306_DrawPixel(x+3, y, (pixels&(1<<4)) ? COLOR_F : COLOR_B);
    if (!--xsize)
      return;
  WriteBit4:
    SSD1306_DrawPixel(x+4, y, (pixels&(1<<3)) ? COLOR_F : COLOR_B);
    if (!--xsize)
      return;
  WriteBit5:
    SSD1306_DrawPixel(x+5, y, (pixels&(1<<2)) ? COLOR_F : COLOR_B);
    if (!--xsize)
      return;
  WriteBit6:
    SSD1306_DrawPixel(x+6, y, (pixels&(1<<1)) ? COLOR_F : COLOR_B);
    if (!--xsize)
      return;
  WriteBit7:
    SSD1306_DrawPixel(x+7, y, (pixels&(1<<0)) ? COLOR_F : COLOR_B);
    if (!--xsize)
      return;
    x+=8;
    pixels = *(++p);
    goto WriteBit0;
}

void LCD_L0_DrawBitmap1BPP(	int x0, int y0, int xsize, int ysize,
														int BytesPerLine, const U8* pData, int Diff){
  int i;
	for (i = 0; i < ysize; i++)
	{
		DrawBitLine1BPP(x0, i+y0, pData, Diff, xsize);
		pData += BytesPerLine;
	}
}

void LCD_DrawBitmap1BPP(	int x0, int y0, int xsize, int ysize, 
													int BytesPerLine, const U8 GUI_UNI_PTR * pPixel){
  int x1, y1;
  /* Handle the optional Y-magnification */
  y1 = y0 + ysize - 1;
  x1 = x0 + xsize - 1;
/*  Handle BITMAP without magnification */
	int Diff;
	/*  Clip y0 (top) */
	Diff = GUI_Context.ClipRect.y0 - y0;
	if (Diff > 0) {
		ysize -= Diff;
		if (ysize <= 0) {
			return;
		}
		y0 = GUI_Context.ClipRect.y0;
		pPixel += (unsigned)Diff * (unsigned)BytesPerLine;
	}
	/*  Clip y1 (bottom) */
	Diff = y1 - GUI_Context.ClipRect.y1;
	if (Diff > 0) {
		ysize -= Diff;
		if (ysize <= 0) {
			return;
		}
	}
	/*        Clip right side    */
	Diff = x1 - GUI_Context.ClipRect.x1;
	if (Diff > 0) {
		xsize -= Diff;
	}
	/*        Clip left side ... (The difficult side ...)    */
	Diff = 0;
	if (x0 < GUI_Context.ClipRect.x0) {
		Diff = GUI_Context.ClipRect.x0 - x0;
		xsize -= Diff;
		pPixel += (Diff>>3); 
		x0 += (Diff>>3)<<3; 
		Diff &=7;
	}
	if (xsize <=0) {
		return;
	}
	LCD_L0_DrawBitmap1BPP(x0, y0, xsize, ysize, BytesPerLine, pPixel, Diff);
}

void GUIPROP_DispChar(U16P c) {
  int BytesPerLine;
  const GUI_FONT_PROP GUI_UNI_PTR * pProp = GUIPROP_FindChar(GUI_Context.pAFont->p.pProp, c);
  if (pProp) {
    const GUI_CHARINFO GUI_UNI_PTR * pCharInfo = pProp->paCharInfo+(c-pProp->First);
    BytesPerLine = pCharInfo->BytesPerLine;
    LCD_DrawBitmap1BPP( GUI_Context.DispPosX, GUI_Context.DispPosY,
												pCharInfo->XSize, GUI_Context.pAFont->YSize,
												BytesPerLine, pCharInfo->pData);
    /* Fill empty pixel lines */
    if (GUI_Context.pAFont->YDist > GUI_Context.pAFont->YSize) {
      int YDist = GUI_Context.pAFont->YDist;
      int YSize = GUI_Context.pAFont->YSize;
        SSD1306_DrawFilledRectangle(GUI_Context.DispPosX, 
                     GUI_Context.DispPosY + YSize, 
                     GUI_Context.DispPosX + pCharInfo->XSize, 
                     GUI_Context.DispPosY + YDist, COLOR_B);
    }
    GUI_Context.DispPosX += pCharInfo->XDist * GUI_Context.pAFont->XMag;
  }
}

/*********************************************************************
*
*       GUIPROP_GetCharDistX
*/
int GUIPROP_GetCharDistX(U16P c) {
  const GUI_FONT_PROP GUI_UNI_PTR * pProp = GUIPROP_FindChar(GUI_Context.pAFont->p.pProp, c);
  return (pProp) ? (pProp->paCharInfo+(c-pProp->First))->XSize * GUI_Context.pAFont->XMag : 0;
}

/*********************************************************************
*
*       GUIPROP_GetFontInfo
*/
void GUIPROP_GetFontInfo(const GUI_FONT GUI_UNI_PTR * pFont, GUI_FONTINFO* pfi) {
  pfi->Flags = GUI_FONTINFO_FLAG_PROP;
}

/*********************************************************************
*
*       GUIPROP_IsInFont
*/
char GUIPROP_IsInFont(const GUI_FONT GUI_UNI_PTR * pFont, U16 c) {
  const GUI_FONT_PROP GUI_UNI_PTR * pProp = GUIPROP_FindChar(pFont->p.pProp, c);
  return (pProp==NULL) ? 0 : 1;
}

/*********************************************************************
*
*       GUI_GetFontDistY
*/
int GUI_GetFontDistY(void) {
  int r;
  GUI_LOCK();
  r = GUI_Context.pAFont->YDist;
  GUI_UNLOCK();
  return r;
}

/*********************************************************************
*
*       GUI_GetCharDistX
*/
int GUI_GetCharDistX(U16 c) {
  int r;
  GUI_LOCK();
  r = GUI_Context.pAFont->pfGetCharDistX(c);
  GUI_UNLOCK();
  return r;
}

void GUI_DispNextLine(void) {
  GUI_LOCK();
  GUI_Context.DispPosY += GUI_GetFontDistY();
  GUI_UNLOCK();
}

/*********************************************************************
*
*       GL_DispChar
*/
void GL_DispChar(U16 c) {
  /* check for control characters */
  if (c == '\n') {
    GUI_DispNextLine();
  } else {
    if (c != '\r') {
      GUI_LOCK();
      GUI_Context.pAFont->pfDispChar(c);
      GUI_UNLOCK();
    }
  }
}

/*********************************************************************
*
*       GUI_GetYAdjust
*
* Returns adjustment in vertical (Y) direction
*
* Note: The return value needs to be subtracted from
*       the y-position of the character.
*/
int GUI_GetYAdjust(void) {
  int r = 0;
  GUI_LOCK();
  switch (GUI_Context.TextAlign & GUI_TA_VERTICAL) {
	case GUI_TA_BOTTOM:
		r = GUI_Context.pAFont->YSize - 1;
    break;
	case GUI_TA_VCENTER:
		r = GUI_Context.pAFont->YSize / 2;
    break;
	case GUI_TA_BASELINE:
		r = GUI_Context.pAFont->YSize / 2;
	}
  GUI_UNLOCK();
  return r;
}

/*********************************************************************
*
*       _GotoY
*/
static char _GotoY(int y) {
  GUI_Context.DispPosY = y;
  return 0;
}

/*********************************************************************
*
*       _GotoX
*/
static char _GotoX(int x) {
  GUI_Context.DispPosX = x;
  return 0;
}

/*********************************************************************
*
*       GUI_GotoY
*/
char GUI_GotoY(int y) {
  char r;
  GUI_LOCK();
  r = _GotoY(y);
  GUI_UNLOCK();
  return r;
}

/*********************************************************************
*
*       GUI_GotoX
*/
char GUI_GotoX(int x) {
  char r;
  GUI_LOCK();
  r = _GotoX(x);
  GUI_UNLOCK();
  return r;
}

/*********************************************************************
*
*       GUI_GotoXY
*/
char GUI_GotoXY(int x, int y) {
  char r;
  GUI_LOCK();
  r  = _GotoX(x);
  r |= _GotoY(y);
  GUI_UNLOCK();
  return r;
}

void GUI_ClearRect(int x0, int y0, int x1, int y1) {
  GUI_LOCK();
  SSD1306_DrawFilledRectangle(x0,y0,x1,y1, GUI_COLOR_BLACK);
  GUI_UNLOCK();
}

void GUI_Clear(void) {
  GUI_GotoXY(0,0);     /* Reset text cursor to upper left */
  GUI_ClearRect(0, 0, GUI_GetXSize(), GUI_GetYSize());
}

void GUI_DispCEOL(void) {
  int y = GUI_Context.DispPosY - GUI_GetYAdjust();
  GUI_ClearRect(GUI_Context.DispPosX, y, 4000,             /* Max pos x */
                y + GUI_Context.pAFont->YDist - 1);
}

void GUI_DispChar(U16 c) {
  GUI_LOCK();
  GL_DispChar(c);
  GUI_UNLOCK();
}

void GUI_DispCharAt(U16 c, I16P x, I16P y) {
  GUI_LOCK();
  GUI_Context.DispPosX = x;
  GUI_Context.DispPosY = y;
	GL_DispChar(c);
  GUI_UNLOCK();
}

///////////////////////////////////////////////////////////////////////
/*********************************************************************
*
*       Static code
*
**********************************************************************
*/
/*********************************************************************
*
*       DB2SJIS
*/
static U16 DB2SJIS(U8 Byte0, U8 Byte1) {
  return Byte1 | (((U16)Byte0)<<8);
}

/*********************************************************************
*
*       _GetLineDistX_SJIS
*/
static int _GetLineDistX_SJIS(const char GUI_UNI_PTR *s, int Len) {
  int Dist =0;
  if (s) {
    U8 c0;
    while (((c0=*(const U8*)s) !=0) && Len >=0) {
      s++; Len--;
      if (c0 > 127) {
        U8  c1 = *(const U8*)s++;
        Len--;
        Dist += GUI_GetCharDistX(DB2SJIS(c0, c1));
      } else {
        Dist += GUI_GetCharDistX(c0);
      }
    }
  }
  return Dist;
}

/*********************************************************************
*
*       _GetLineLen_SJIS
* Purpose:
*   Returns the number of characters in a string.
*
* NOTE:
*   The return value can be used as offset into the
*   string, which means that double characters count double
*/
static int _GetLineLen_SJIS(const char GUI_UNI_PTR *s, int MaxLen) {
  int Len =0;
  U8 c0;
  while (((c0=*(const U8*)s) !=0) && Len < MaxLen) {
    s++;
    if (c0 > 127) {
      Len++; s++;
    } else {
      switch (c0) {
      case '\n': return Len;
      }
    }
    Len++;
  }
  return Len;
}

/*********************************************************************
*
*       _DispLine_SJIS
*/
static void _DispLine_SJIS(const char GUI_UNI_PTR *s, int Len) {
  U8 c0;
  while (--Len >=0) {
    c0=*(const U8*)s++;
    if (c0 > 127) {
      U8  c1 = *(const U8*)s++;
      Len--;
      GL_DispChar (DB2SJIS(c0, c1));
    } else {
      GL_DispChar(c0);
    }
  }
}
///////////////////////////////////////////////////////////////////////

static U16 _GetCharCode(const char GUI_UNI_PTR * s) {
  return *(const U8 GUI_UNI_PTR *)s;
}
static int _GetCharSize(const char GUI_UNI_PTR * s) {
  return 1;
}
int GUI_UC_GetCharSize(const char GUI_UNI_PTR * s) {
  int r;
  GUI_LOCK();
  r =  _GetCharSize(s);
  GUI_UNLOCK();
  return r;
}

U16 GUI_UC_GetCharCode(const char GUI_UNI_PTR * s) {
  U16 r;
  GUI_LOCK();
  r =  _GetCharCode(s);
  GUI_UNLOCK();
  return r;
}

U16 GUI_UC__GetCharCodeInc(const char GUI_UNI_PTR ** ps) {
  const char GUI_UNI_PTR * s;
  U16 r;
  s   = *ps;
  r   = _GetCharCode(s);
  s  += _GetCharSize(s);
  *ps = s;
  return r;
}
int GUI_UC__NumChars2NumBytes(const char GUI_UNI_PTR * s, int NumChars) {
  int CharSize, NumBytes = 0;
  while (NumChars--) {
    CharSize = _GetCharSize(s);
    s += CharSize;    
    NumBytes += CharSize;
  }
  return NumBytes;
}
int GUI_UC__NumBytes2NumChars(const char GUI_UNI_PTR * s, int NumBytes) {
  int CharSize, Chars = 0, Bytes = 0;
  while (NumBytes > Bytes) {
    CharSize = _GetCharSize(s + Bytes);
    Bytes += CharSize;
    Chars++;
  }
  return Chars;
}

/*********************************************************************
*
*       _DispLine
*/
static void _DispLine(const char GUI_UNI_PTR *s, int MaxNumChars, const GUI_RECT *pRect) {
  /* Check if we have anything to do at all ... */
	_DispLine_SJIS(s, MaxNumChars);
}

/*********************************************************************
*
*       GUI__GetLineNumChars
*/
int GUI__GetLineNumChars(const char GUI_UNI_PTR *s, int MaxNumChars) {
  return _GetLineLen_SJIS(s, MaxNumChars);;
}

int GUI__GetLineDistX(const char GUI_UNI_PTR *s, int MaxNumChars) {
  return _GetLineDistX_SJIS(s, MaxNumChars);;
}

/*********************************************************************
*
*       GUI__DispLine
*/
void GUI__DispLine(const char GUI_UNI_PTR *s, int MaxNumChars, const GUI_RECT* pr) {
  GUI_RECT r;
  {
    r = *pr;
		GUI_Context.DispPosX = r.x0;
		GUI_Context.DispPosY = r.y0;
		/* Do the actual drawing via routine call. */
		_DispLine(s, MaxNumChars, &r);
  }
}

void GUI_DispString(const char GUI_UNI_PTR *s) {
  int xAdjust, yAdjust, xOrg;
  int FontSizeY;
  if (!s)
    return;
  GUI_LOCK();
  FontSizeY = GUI_GetFontDistY();
  xOrg = GUI_Context.DispPosX;
 /* Adjust vertical position */
  yAdjust = GUI_GetYAdjust();
  GUI_Context.DispPosY -= yAdjust;
  char *ss = (char *)s;
  int butf8 = detect_encoding_by_sequence(s);
  if (butf8 == 1)
	  ss = utf8_string_to_gbk_with_table(s);
  else if (butf8 == 3)
	  ss = mixed_string_to_gbk(s);
  if (!ss)
	  return;
  for (; *ss; ss++) {
    GUI_RECT r;
    int LineNumChars = GUI__GetLineNumChars(ss, 0x7fff);
    int xLineSize    = GUI__GetLineDistX(ss, LineNumChars);
  /* Check if x-position needs to be changed due to h-alignment */
    switch (GUI_Context.TextAlign & GUI_TA_HORIZONTAL) { 
      case GUI_TA_CENTER: xAdjust = xLineSize / 2; break;
      case GUI_TA_RIGHT:  xAdjust = xLineSize; break;
      default:            xAdjust = 0;
    }
    r.x0 = GUI_Context.DispPosX -= xAdjust;
    r.x1 = r.x0 + xLineSize - 1;    
    r.y0 = GUI_Context.DispPosY;
    r.y1 = r.y0 + FontSizeY - 1;    
    GUI__DispLine(ss, LineNumChars, &r);
    GUI_Context.DispPosY = r.y0;
    ss += GUI_UC__NumChars2NumBytes(ss, LineNumChars);
    if ((*ss == '\n') || (*ss == '\r')) {
      switch (GUI_Context.TextAlign & GUI_TA_HORIZONTAL) { 
      case GUI_TA_CENTER:
      case GUI_TA_RIGHT:
        GUI_Context.DispPosX = xOrg;
        break;
      default:
        GUI_Context.DispPosX = 0;
        break;
      }
      if (*ss == '\n')
        GUI_Context.DispPosY += FontSizeY;
    } else {
      GUI_Context.DispPosX = r.x0 + xLineSize;
    }
    if (*ss == 0)    /* end of string (last line) reached ? */
      break;
  }
//  if (butf8 == 1)
//	free(ss);
  GUI_Context.DispPosY += yAdjust;
  GUI_Context.TextAlign &= ~GUI_TA_HORIZONTAL;
  GUI_UNLOCK();
}

void GUI_DispStringAt(const char GUI_UNI_PTR *s, int x, int y) {
  GUI_LOCK();
  GUI_Context.DispPosX = x;
  GUI_Context.DispPosY = y;
  GUI_DispString(s);
  GUI_UNLOCK();
}

int GUI_SetTextAlign(int Align) {
  int r;
  GUI_LOCK();
  r = GUI_Context.TextAlign;
  GUI_Context.TextAlign = Align;
  GUI_UNLOCK();
  return r;
}

void GUI_DispStringHCenterAt(const char GUI_UNI_PTR *s, int x, int y) {
  int Align;
  Align = GUI_SetTextAlign((GUI_Context.TextAlign & ~GUI_TA_LEFT) | GUI_TA_CENTER);
  GUI_DispStringAt(s, x, y);
  GUI_SetTextAlign(Align);
}

int GUI_GetFontSizeY(void) {
  int r;
  GUI_LOCK();
  r = GUI_Context.pAFont->YSize;
  GUI_UNLOCK();
  return r;
}

void GUI_GetClientRect(GUI_RECT* pRect) {
  if (!pRect)
    return;
	pRect->x0 = 0;
	pRect->y0 = 0;
	pRect->x1 = LCD_GetXSize();
	pRect->y1 = LCD_GetYSize();
}

int GUI__HandleEOLine(const char GUI_UNI_PTR **ps) {
  const char GUI_UNI_PTR *s = *ps;
  char c = *s++;
  if (c == 0) {
    return 1;
  }
  if (c == '\n') {
    *ps = s;
  }
  return 0;
}

void GUI__DispStringInRect(const char GUI_UNI_PTR *s, GUI_RECT* pRect, int TextAlign, int MaxNumChars) {
  GUI_RECT r;
  GUI_RECT rLine;
  int y = 0;

  char *ss = (char *)s;
  int butf8 = detect_encoding_by_sequence(s);
  if (butf8 == 1)
	  ss = utf8_string_to_gbk_with_table(s);
  else if (butf8 == 3)
	  ss = mixed_string_to_gbk(s);
  if (!ss)
	  return;
  const char GUI_UNI_PTR *sOrg =ss;
  int FontYSize;
  int xLine = 0;
  int LineLen;
  int NumCharsRem;           /* Number of remaining characters */
  FontYSize = GUI_GetFontSizeY();
  if (pRect) {
    r = *pRect;
  } else {
    GUI_GetClientRect(&r);
  }
  /* handle vertical alignment */
  if ((TextAlign & GUI_TA_VERTICAL) == GUI_TA_TOP) {
		y = r.y0;
  } else {
    int NumLines;
    /* Count the number of lines */
    for (NumCharsRem = MaxNumChars, NumLines = 1; NumCharsRem ;NumLines++) {
      LineLen = GUI__GetLineNumChars(ss, NumCharsRem);
      NumCharsRem -= LineLen;
      ss += GUI_UC__NumChars2NumBytes(ss, LineLen);
      if (GUI__HandleEOLine((const char **)&ss))
        break;
    }
    /* Do the vertical alignment */
    switch (TextAlign & GUI_TA_VERTICAL) {
	  case GUI_TA_BASELINE:
	  case GUI_TA_BOTTOM:
	  y = r.y1 -NumLines * FontYSize+1;
      break;
	  case GUI_TA_VCENTER:
		  y = r.y0+(r.y1-r.y0+1 -NumLines * FontYSize) /2;
      break;
	  }
  }
  /* Output string */
  for (NumCharsRem = MaxNumChars, ss = (char *)sOrg; NumCharsRem;) {
    int xLineSize;
    LineLen = GUI__GetLineNumChars(ss, NumCharsRem);
    NumCharsRem -= LineLen;
    xLineSize = GUI__GetLineDistX(ss, LineLen);
    switch (TextAlign & GUI_TA_HORIZONTAL) {
    case GUI_TA_HCENTER:
      xLine = r.x0+(r.x1-r.x0-xLineSize)/2; break;
    case GUI_TA_LEFT:
      xLine = r.x0; break;
    case GUI_TA_RIGHT:
      xLine = r.x1 -xLineSize + 1;
    }
    rLine.x0 = GUI_Context.DispPosX = xLine;
    rLine.x1 = rLine.x0 + xLineSize-1;
    rLine.y0 = GUI_Context.DispPosY = y;
    rLine.y1 = y + FontYSize-1;
    GUI__DispLine(ss, LineLen, &rLine);
    ss += GUI_UC__NumChars2NumBytes(ss, LineLen);
    y += GUI_GetFontDistY();
    if (GUI__HandleEOLine((const char **)&ss))
      break;
  }
//  if (butf8 == 1)
//	free(ss);
}

#define MIN(v0,v1) ((v0>v1) ? v1 : v0)
#define MAX(v0,v1) ((v0>v1) ? v0 : v1)

int GUI__IntersectRects(GUI_RECT* pDest, const GUI_RECT* pr0, const GUI_RECT* pr1) {
  pDest->x0 = MAX (pr0->x0, pr1->x0);
  pDest->y0 = MAX (pr0->y0, pr1->y0);
  pDest->x1 = MIN (pr0->x1, pr1->x1);
  pDest->y1 = MIN (pr0->y1, pr1->y1);
  if (pDest->x1 < pDest->x0) {
    return 0;
  }
  if (pDest->y1 < pDest->y0) {
    return 0;
  }
  return 1;
}
void LCD_SetClipRectEx(const GUI_RECT* pRect) {
  LCD_RECT r;
  LCD_GetRect(&r);
  GUI__IntersectRects(&GUI_Context.ClipRect, pRect, &r);
}

void GUI_DispStringInRectMax(const char GUI_UNI_PTR *s, GUI_RECT* pRect, int TextAlign, int MaxLen) {
  GUI_RECT Rect_Old, r;
  if (s && pRect) {
    GUI_LOCK();
    Rect_Old = GUI_Context.ClipRect;
    GUI__IntersectRects(&r, pRect, &Rect_Old);
    LCD_SetClipRectEx(&r);
    GUI__DispStringInRect(s, pRect, TextAlign, MaxLen);
    LCD_SetClipRectEx(&Rect_Old);
    GUI_UNLOCK();
  }
}
void GUI_DispStringInRect(const char GUI_UNI_PTR *s, GUI_RECT* pRect, int TextAlign) {
  GUI_DispStringInRectMax(s, pRect, TextAlign, 0x7fff);
}

void GUI_DispStringLen(const char GUI_UNI_PTR *s, int MaxNumChars) {
  U16 Char;
  GUI_LOCK();
  char *ss = (char *)s;
  int butf8 = detect_encoding_by_sequence(s);
  if (butf8 == 1)
	  ss = utf8_string_to_gbk_with_table(s);
  else if (butf8 == 3)
	  ss = mixed_string_to_gbk(s);
  if (!ss)
	  return;
  while (MaxNumChars && ((Char = GUI_UC__GetCharCodeInc((const char **)&ss)) != 0)) {
    GUI_DispChar(Char);
    MaxNumChars--;
  }
  while (MaxNumChars--) {
    GUI_DispChar(' ');
  }
//  if (butf8 == 1)
//	free(ss);
  GUI_UNLOCK();
}
/*********************************************************************
*
*       GUI_GetTextExtend
*/
void GUI_GetTextExtend(GUI_RECT* pRect, const char GUI_UNI_PTR * s, int MaxNumChars) {
  int xMax      = 0;
  int NumLines  = 0;
  int LineSizeX = 0;
  U16 Char;
  pRect->x0 = GUI_Context.DispPosX;
  pRect->y0 = GUI_Context.DispPosY;
  char *ss = (char *)s;
  int butf8 = detect_encoding_by_sequence(s);
  if (butf8 == 1)
	  ss = utf8_string_to_gbk_with_table(s);
  else if (butf8 == 3)
	  ss = mixed_string_to_gbk(s);
  if (!ss)
	  return;
  while (MaxNumChars--) {
    Char = GUI_UC__GetCharCodeInc((const char **)&ss);
    if ((Char == '\n') || (Char == 0)) {
      if (LineSizeX > xMax) {
        xMax = LineSizeX;
      }
      LineSizeX = 0;
      NumLines++;
      if (!Char) {
        break;
      }
    } else {
      LineSizeX += GUI_GetCharDistX(Char);
    }
  }
  if (LineSizeX > xMax) {
    xMax = LineSizeX;
  }
  if (!NumLines) {
    NumLines = 1;
  }
  pRect->x1 = pRect->x0 + xMax - 1;
  pRect->y1 = pRect->y0 + GUI_Context.pAFont->YSize * NumLines - 1;
//  if (butf8 == 1)
//	free(ss);
}

int GUI_GetDispPosX(void) {
  int r;
  GUI_LOCK();
  r = GUI_Context.DispPosX;
  GUI_UNLOCK();
  return r;
}

int GUI_GetDispPosY(void) {
  int r;
  GUI_LOCK();
  r = GUI_Context.DispPosY;
  GUI_UNLOCK();
  return r;
}

int GUI__strlen(const char GUI_UNI_PTR * s) {
  int r = -1;
  if (s) {
    do {
      r++;
    } while (*s++);
  }
  return r;
}

int GUI_GetStringDistX(const char GUI_UNI_PTR * s) {
  char *ss = (char *)s;
  int butf8 = detect_encoding_by_sequence(s);
  if (butf8 == 1)
	  ss = utf8_string_to_gbk_with_table(s);
  else if (butf8 == 3)
	  ss = mixed_string_to_gbk(s);
  if (!ss)
	  return 0;
  int len = GUI__GetLineDistX(s, GUI__strlen(ss));
//  if (butf8 == 1)
//	free(ss);
  return len;
}

void GUI_GetFontInfo(const GUI_FONT GUI_UNI_PTR * pFont, GUI_FONTINFO * pFontInfo) {
  GUI_LOCK();
  if (pFont == NULL) {
    pFont = GUI_Context.pAFont;
  }
  pFont->pfGetFontInfo(pFont, pFontInfo);
  GUI_UNLOCK();
}

int GUI_GetYSizeOfFont(const GUI_FONT GUI_UNI_PTR * pFont) {
  return pFont->YSize;
}

int GUI_GetYDistOfFont(const GUI_FONT GUI_UNI_PTR * pFont) {
  return pFont->YDist;
}

int GUI_GetTextAlign(void) {
  int r;
  GUI_LOCK();
  r = GUI_Context.TextAlign;
  GUI_UNLOCK();
  return r;
}

char GUI_IsInFont(const GUI_FONT GUI_UNI_PTR *pFont, U16 c) {
  if (pFont == NULL) {
    pFont = GUI_Context.pAFont;
  }
  return pFont->pfIsInFont(pFont, c);
}

void GUI_Update(void){
	SSD1306_UpdateScreen();
}

void GL_DrawBitmap(const GUI_BITMAP GUI_UNI_PTR * pBitmap, int x0, int y0) {
    LCD_DrawBitmap1BPP( x0,y0
                    ,pBitmap->XSize ,pBitmap->YSize
                    ,pBitmap->BytesPerLine
                    ,pBitmap->pData);
}

void GUI_DrawBitmap(const GUI_BITMAP GUI_UNI_PTR * pBitmap, int x0, int y0) {
  GUI_LOCK();
  GL_DrawBitmap(pBitmap, x0, y0);
  GUI_UNLOCK();
}

#define RETURN_IF_Y_OUT() \
  if (y < GUI_Context.ClipRect.y0) return;             \
  if (y > GUI_Context.ClipRect.y1) return;

#define RETURN_IF_X_OUT() \
  if (x < GUI_Context.ClipRect.x0) return;             \
  if (x > GUI_Context.ClipRect.x1) return;

#define CLIP_X() \
  if (x0 < GUI_Context.ClipRect.x0) { x0 = GUI_Context.ClipRect.x0; } \
  if (x1 > GUI_Context.ClipRect.x1) { x1 = GUI_Context.ClipRect.x1; }

#define CLIP_Y() \
  if (y0 < GUI_Context.ClipRect.y0) { y0 = GUI_Context.ClipRect.y0; } \
  if (y1 > GUI_Context.ClipRect.y1) { y1 = GUI_Context.ClipRect.y1; }

void LCD_DrawPixel(int x, int y) {
  RETURN_IF_Y_OUT();
  RETURN_IF_X_OUT();
	SSD1306_DrawPixel(x, y, COLOR_F);
}
void LCD_DrawHLine(int x0, int y,  int x1) {
  /* Perform clipping and check if there is something to do */
  RETURN_IF_Y_OUT();
  CLIP_X();
  if (x1<x0)
    return;
  /* Call driver to draw */
	SSD1306_DrawLine(x0, y, x1, y, COLOR_F);
}
void LCD_DrawVLine(int x, int y0,  int y1) {
  /* Perform clipping and check if there is something to do */
  RETURN_IF_X_OUT();
  CLIP_Y();
  if (y1<y0)
    return;
  /* Call driver to draw */
	SSD1306_DrawLine(x, y0, x, y1, COLOR_F);
}

static void Draw8Point(int x0,int y0, int xoff, int yoff) {
  LCD_DrawPixel(x0+xoff,y0+yoff);
  LCD_DrawPixel(x0-xoff,y0+yoff);
  LCD_DrawPixel(x0+yoff,y0+xoff);
  LCD_DrawPixel(x0+yoff,y0-xoff);
  if (yoff) {
    LCD_DrawPixel(x0+xoff,y0-yoff);
    LCD_DrawPixel(x0-xoff,y0-yoff);
    LCD_DrawPixel(x0-yoff,y0+xoff);
    LCD_DrawPixel(x0-yoff,y0-xoff);
  }
}
/*********************************************************************
*
*       GL_DrawCircle
*/
void GL_DrawCircle(int x0, int y0, int r) {
  I32 i;
  int imax = ((I32)((I32)r*707))/1000+1;
  I32 sqmax = (I32)r*(I32)r+(I32)r/2;
  I32 y=r;
  Draw8Point(x0,y0,r,0);
  for (i=1; i<= imax; i++) {
    if ((i*i+y*y) >sqmax) {
      Draw8Point(x0,y0,i,y);
      y--;
    }
    Draw8Point(x0,y0,i,y);
  }
}

/*********************************************************************
*
*       GUI_DrawCircle
*/
void GUI_DrawCircle(int x0, int y0, int r) {
  GUI_LOCK();
    GL_DrawCircle( x0, y0, r);
  GUI_UNLOCK();
}

/*********************************************************************
*
*       GL_FillCircle
*/
void GL_FillCircle(int x0, int y0, int r) {
  I32 i;
  int imax = ((I32)((I32)r*707))/1000+1;
  I32 sqmax = (I32)r*(I32)r+(I32)r/2;
  I32 x=r;
  LCD_DrawHLine(x0-r,y0,x0+r);
  for (i=1; i<= imax; i++) {
    if ((i*i+x*x) >sqmax) {
      /* draw lines from outside */
      if (x>imax) {
        LCD_DrawHLine (x0-i+1,y0+x, x0+i-1);
        LCD_DrawHLine (x0-i+1,y0-x, x0+i-1);
      }
      x--;
    }
    /* draw lines from inside (center) */
    LCD_DrawHLine(x0-x,y0+i, x0+x);
    LCD_DrawHLine(x0-x,y0-i, x0+x);
  }
}

/*********************************************************************
*
*       GUI_FillCircle
*/
void GUI_FillCircle(int x0, int y0, int r) {
  GUI_LOCK();
  GL_FillCircle(x0,y0,r);
  GUI_UNLOCK();
}

/*********************************************************************
*
*       GL_FillEllipse
*/
void GL_FillEllipse(int x0, int y0, int rx, int ry) {
  I32 OutConst, Sum, SumY;
  int x,y;
  U32 _rx = rx;
  U32 _ry = ry;
  OutConst = _rx*_rx*_ry*_ry  /* Constant as explaint above */
            +(_rx*_rx*_ry>>1); /* To compensate for rounding */
  x = rx;
  for (y=0; y<=ry; y++) {
    SumY =((I32)(rx*rx))*((I32)(y*y)); /* Does not change in loop */
    while (Sum = SumY + ((I32)(ry*ry))*((I32)(x*x)),
           (x>0) && (Sum>OutConst))
    {
      x--;
    }
    LCD_DrawHLine(x0-x, y0+y, x0+x);
    if (y)
      LCD_DrawHLine(x0-x, y0-y, x0+x);
  }
}

/*********************************************************************
*
*       GUI_FillEllipse
*/
void GUI_FillEllipse(int x0, int y0, int rx, int ry) {
  GUI_LOCK();
  GL_FillEllipse (x0,y0, rx, ry);
  GUI_UNLOCK();
}

/*********************************************************************
*
*       GL_DrawEllipse
*/
void GL_DrawEllipse(int x0, int y0, int rx, int ry) {
  I32 OutConst, Sum, SumY;
  int x,y;
  int xOld;
  U32 _rx = rx;
  U32 _ry = ry;
  OutConst = _rx*_rx*_ry*_ry  /* Constant as explaint above */
            +(_rx*_rx*_ry>>1); /* To compensate for rounding */
  xOld = x = rx;
  for (y=0; y<=ry; y++) {
    if (y==ry) {
      x=0;
    } else {
      SumY =((I32)(rx*rx))*((I32)(y*y)); /* Does not change in loop */
      while (Sum = SumY + ((I32)(ry*ry))*((I32)(x*x)),
             (x>0) && (Sum>OutConst)) x--;
    }
    /* Since we draw lines, we can not draw on the first
        iteration
    */
    if (y) {
      SSD1306_DrawLine(x0-xOld,y0-y+1,x0-x,y0-y, COLOR_F);
      SSD1306_DrawLine(x0-xOld,y0+y-1,x0-x,y0+y, COLOR_F);
      SSD1306_DrawLine(x0+xOld,y0-y+1,x0+x,y0-y, COLOR_F);
      SSD1306_DrawLine(x0+xOld,y0+y-1,x0+x,y0+y, COLOR_F);
    }
    xOld = x;
  }
}

/*********************************************************************
*
*       GUI_DrawEllipse
*/
void GUI_DrawEllipse(int x0, int y0, int rx, int ry) {
  GUI_LOCK();
  GL_DrawEllipse(x0, y0, rx, ry);
  GUI_UNLOCK();
}

void GUI_DrawHLine(int y0, int x0, int x1) {
  GUI_LOCK();
  LCD_DrawHLine(x0, y0, x1);
  GUI_UNLOCK();
}

void GUI_DrawVLine(int x0, int y0, int y1) {
  GUI_LOCK();
  LCD_DrawVLine(x0, y0, y1);
  GUI_UNLOCK();
}

void GUI_DrawLine (int x0, int y0, int x1, int y1){
  GUI_LOCK();
  SSD1306_DrawLine(x0, y0, x1, y1, COLOR_F);
  GUI_UNLOCK();
}

void GUI_DrawPixel (int x, int y){
	LCD_DrawPixel(x, y);
}
void GUI_DrawPoint (int x, int y, GUI_COLOR c){
  RETURN_IF_Y_OUT();
  RETURN_IF_X_OUT();
	SSD1306_DrawPixel(x, y, c);
}

static void _DrawRect(int x0, int y0, int x1, int y1) {
  LCD_DrawHLine(x0, y0, x1);
  LCD_DrawHLine(x0, y1, x1);
  LCD_DrawVLine(x0, y0 + 1, y1 - 1);
  LCD_DrawVLine(x1, y0 + 1, y1 - 1);
}

/*********************************************************************
*
*       GUI_DrawRect
*/
void GUI_DrawRect(int x0, int y0, int x1, int y1) {
  GUI_LOCK();
  _DrawRect(x0, y0, x1, y1);
  GUI_UNLOCK();
}

void GUI_FillRect(int x0, int y0, int x1, int y1) {
  GUI_LOCK();
	int y;
	for (y = y0; y < y1; ++y)
		LCD_DrawHLine(x0, y, x1);

//  SSD1306_DrawFilledRectangle(x0,y0,x1-x0,y1-y0, c);
  GUI_UNLOCK();
}

void GUI_SetColor(GUI_COLOR color) {
  GUI_LOCK(); {
    if (GUI_Context.DrawColor != color)
			GUI_Context.DrawColor = color;
  } GUI_UNLOCK();
}

GUI_COLOR GUI_GetColor(void) {
  GUI_COLOR r;
  GUI_LOCK();
  r = GUI_Context.DrawColor;
  GUI_UNLOCK();
  return r;
}

char* utf8_string_to_gbk_with_table(const char* utf8_str) {
		static char output_buf[1024] = {0};
    if (!utf8_str) return NULL;

    size_t input_len = strlen(utf8_str);
    if (input_len > 256)
        return NULL;
    
    char* out_ptr = output_buf;
    const unsigned char* in = (const unsigned char*)utf8_str;
    size_t i = 0;

    size_t map_size = GUI_Context.pAFont->gbkMapCnt;
    const Utf8ToGbkMap* pMap = GUI_Context.pAFont->pfUtf2GBKMap;
    
    while (i < input_len) {
        unsigned char c = in[i];

        // ASCII 字符（0x00 ~ 0x7F）
        if (c <= 0x7F) {
            *out_ptr++ = c;
            i++;
        }
        // 多字节 UTF-8 字符
        else {
            size_t utf8_len = 0;
            // 判断 UTF-8 字符长度
            if ((c & 0xE0) == 0xC0) utf8_len = 2;
            else if ((c & 0xF0) == 0xE0) utf8_len = 3;
            else if ((c & 0xF8) == 0xF0) utf8_len = 4;
            else {
                // 非法 UTF-8，跳过或报错
                i++;
                continue;
            }

            if (i + utf8_len > input_len) {
                // 不完整字符，跳过
                break;
            }

            // 使用二分查找在映射表中查找
            int found = 0;
            int left = 0;
            int right = map_size - 1;
            
            while (left <= right) {
                int mid = left + (right - left) / 2;
                
                // 首先比较长度，如果长度不同，可以直接判断大小关系
                if (pMap[mid].utf8_len < utf8_len) {
                    // 如果当前项的utf8长度小于目标长度，说明目标在右侧
                    left = mid + 1;
                } 
                else if (pMap[mid].utf8_len > utf8_len) {
                    // 如果当前项的utf8长度大于目标长度，说明目标在左侧
                    right = mid - 1;
                }
                else {
                    // 长度相同，比较UTF-8编码内容
                    int cmp = memcmp(pMap[mid].utf8, &in[i], utf8_len);
                    
                    if (cmp < 0) {
                        // 当前项小于目标，目标在右侧
                        left = mid + 1;
                    }
                    else if (cmp > 0) {
                        // 当前项大于目标，目标在左侧
                        right = mid - 1;
                    }
                    else {
                        // 找到匹配项
                        memcpy(out_ptr, pMap[mid].gbk, pMap[mid].gbk_len);
                        out_ptr += pMap[mid].gbk_len;
                        found = 1;
                        break;
                    }
                }
            }

            if (!found) {
                // 未在表中，可选择跳过、替换为 '?'，或报错
                *out_ptr++ = '?'; // 简单处理
            }

            i += utf8_len;
        }
    }

    *out_ptr = '\0';
    return output_buf;
}

char* mixed_string_to_gbk(const char* str) {
    static char output_buf[1024] = {0};
    if (!str) return NULL;

    size_t input_len = strlen(str);
    if (input_len > 512)
        return NULL;
    
    char* out_ptr = output_buf;
    const unsigned char* in = (const unsigned char*)str;
    size_t i = 0;

    size_t map_size = GUI_Context.pAFont->gbkMapCnt;
    const Utf8ToGbkMap* pMap = GUI_Context.pAFont->pfUtf2GBKMap;
    
    while (i < input_len) {
        unsigned char c = in[i];

        if (c <= 0x7F) {
            *out_ptr++ = c;
            i++;
            continue;
        }
        
        size_t utf8_consumed = 1;
        size_t gbk_consumed = 1;
        int is_utf8 = is_valid_utf8_sequence(in + i, input_len - i, &utf8_consumed);
        int is_gbk = is_valid_gbk_sequence(in + i, input_len - i, &gbk_consumed);
        
        if (is_utf8 == 1 && is_gbk != 1) {
                        size_t utf8_len = utf8_consumed;
            int found = 0;
            int left = 0;
            int right = map_size - 1;
            
            while (left <= right) {
                int mid = left + (right - left) / 2;
                
                if (pMap[mid].utf8_len < utf8_len) {
                    left = mid + 1;
                } 
                else if (pMap[mid].utf8_len > utf8_len) {
                    right = mid - 1;
                }
                else {
                    int cmp = memcmp(pMap[mid].utf8, &in[i], utf8_len);
                    
                    if (cmp < 0) {
                        left = mid + 1;
                    }
                    else if (cmp > 0) {
                        right = mid - 1;
                    }
                    else {
                        memcpy(out_ptr, pMap[mid].gbk, pMap[mid].gbk_len);
                        out_ptr += pMap[mid].gbk_len;
                        found = 1;
                        break;
                    }
                }
            }
            
            if (!found) {
                *out_ptr++ = '?';
            }
            i += utf8_len;
        }
        else if (is_gbk == 1 && is_utf8 != 1) {
                        *out_ptr++ = in[i];
            *out_ptr++ = in[i + 1];
            i += 2;
        }
        else if (is_utf8 == 1 && is_gbk == 1) {
                        size_t utf8_len = utf8_consumed;
            int found = 0;
            int left = 0;
            int right = map_size - 1;
            
            while (left <= right) {
                int mid = left + (right - left) / 2;
                
                if (pMap[mid].utf8_len < utf8_len) {
                    left = mid + 1;
                } 
                else if (pMap[mid].utf8_len > utf8_len) {
                    right = mid - 1;
                }
                else {
                    int cmp = memcmp(pMap[mid].utf8, &in[i], utf8_len);
                    
                    if (cmp < 0) {
                        left = mid + 1;
                    }
                    else if (cmp > 0) {
                        right = mid - 1;
                    }
                    else {
                        memcpy(out_ptr, pMap[mid].gbk, pMap[mid].gbk_len);
                        out_ptr += pMap[mid].gbk_len;
                        found = 1;
                        break;
                    }
                }
            }
            
            if (!found) {
                *out_ptr++ = '?';
            }
            i += utf8_len;
        }
        else {
                        *out_ptr++ = '?';
            i++;
        }
    }

    *out_ptr = '\0';
    return output_buf;
}

/*************************** End of file ****************************/
