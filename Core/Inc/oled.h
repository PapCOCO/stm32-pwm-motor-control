#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ClearLine(uint8_t Line);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowLine(uint8_t Line, const char *String);
void OLED_ShowString(uint8_t Line, uint8_t Column, const char *String);
void OLED_WriteCommand(uint8_t Command);
void OLED_WriteData(uint8_t Data);

#endif
