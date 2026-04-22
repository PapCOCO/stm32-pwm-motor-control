#include "oled.h"
#include "i2c.h"
#include "oledfont.h"

#include <string.h>

#define OLED_I2C_ADDR_DEFAULT    0x78
#define OLED_I2C_ADDR_ALTERNATE  0x7A
#define OLED_I2C_TIMEOUT         100
#define OLED_CONTROL_COMMAND     0x00
#define OLED_CONTROL_DATA        0x40
#define OLED_WIDTH               128
#define OLED_PAGE_COUNT          8
#define OLED_LINE_COUNT          4
#define OLED_CHARS_PER_LINE      16
#define OLED_GLYPH_BYTES         16

static uint16_t OLED_Address = OLED_I2C_ADDR_DEFAULT;

static HAL_StatusTypeDef OLED_Transmit(const uint8_t *Buffer, uint16_t Size)
{
  HAL_StatusTypeDef Status;
  uint16_t AlternateAddress;

  Status = HAL_I2C_Master_Transmit(&hi2c1, OLED_Address, (uint8_t *)Buffer, Size, OLED_I2C_TIMEOUT);
  if (Status == HAL_OK)
  {
    return Status;
  }

  AlternateAddress = (OLED_Address == OLED_I2C_ADDR_DEFAULT) ? OLED_I2C_ADDR_ALTERNATE : OLED_I2C_ADDR_DEFAULT;
  if (HAL_I2C_IsDeviceReady(&hi2c1, AlternateAddress, 2, OLED_I2C_TIMEOUT) != HAL_OK)
  {
    return Status;
  }

  OLED_Address = AlternateAddress;
  return HAL_I2C_Master_Transmit(&hi2c1, OLED_Address, (uint8_t *)Buffer, Size, OLED_I2C_TIMEOUT);
}

static void OLED_DetectAddress(void)
{
  if (HAL_I2C_IsDeviceReady(&hi2c1, OLED_I2C_ADDR_DEFAULT, 2, OLED_I2C_TIMEOUT) == HAL_OK)
  {
    OLED_Address = OLED_I2C_ADDR_DEFAULT;
    return;
  }

  if (HAL_I2C_IsDeviceReady(&hi2c1, OLED_I2C_ADDR_ALTERNATE, 2, OLED_I2C_TIMEOUT) == HAL_OK)
  {
    OLED_Address = OLED_I2C_ADDR_ALTERNATE;
    return;
  }

  OLED_Address = OLED_I2C_ADDR_DEFAULT;
}

static void OLED_WriteCommandList(const uint8_t *Commands, uint16_t Size)
{
  uint8_t Buffer[17];
  uint16_t ChunkSize;

  while (Size > 0)
  {
    ChunkSize = (Size > 16U) ? 16U : Size;
    Buffer[0] = OLED_CONTROL_COMMAND;
    memcpy(&Buffer[1], Commands, ChunkSize);
    OLED_Transmit(Buffer, ChunkSize + 1U);
    Commands += ChunkSize;
    Size -= ChunkSize;
  }
}

static void OLED_WriteDataBlock(const uint8_t *Data, uint16_t Size)
{
  uint8_t Buffer[17];
  uint16_t ChunkSize;

  while (Size > 0)
  {
    ChunkSize = (Size > 16U) ? 16U : Size;
    Buffer[0] = OLED_CONTROL_DATA;
    memcpy(&Buffer[1], Data, ChunkSize);
    OLED_Transmit(Buffer, ChunkSize + 1U);
    Data += ChunkSize;
    Size -= ChunkSize;
  }
}

static void OLED_SetCursor(uint8_t Y, uint8_t X)
{
  uint8_t Commands[3];

  Commands[0] = 0xB0 | Y;
  Commands[1] = 0x10 | ((X & 0xF0) >> 4);
  Commands[2] = 0x00 | (X & 0x0F);
  OLED_WriteCommandList(Commands, sizeof(Commands));
}

void OLED_WriteCommand(uint8_t Command)
{
  OLED_WriteCommandList(&Command, 1);
}

void OLED_WriteData(uint8_t Data)
{
  OLED_WriteDataBlock(&Data, 1);
}

void OLED_Clear(void)
{
  uint8_t EmptyLine[OLED_WIDTH] = {0};
  uint8_t Page;

  for (Page = 0; Page < OLED_PAGE_COUNT; Page++)
  {
    OLED_SetCursor(Page, 0);
    OLED_WriteDataBlock(EmptyLine, OLED_WIDTH);
  }
}

void OLED_ClearLine(uint8_t Line)
{
  uint8_t EmptyLine[OLED_WIDTH] = {0};

  if ((Line == 0U) || (Line > OLED_LINE_COUNT))
  {
    return;
  }

  OLED_SetCursor((Line - 1U) * 2U, 0);
  OLED_WriteDataBlock(EmptyLine, OLED_WIDTH);
  OLED_SetCursor((Line - 1U) * 2U + 1U, 0);
  OLED_WriteDataBlock(EmptyLine, OLED_WIDTH);
}

void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
  uint16_t GlyphOffset;

  if ((Line == 0U) || (Line > OLED_LINE_COUNT) || (Column == 0U) || (Column > OLED_CHARS_PER_LINE))
  {
    return;
  }

  if ((Char < ' ') || ((uint8_t)(Char - ' ') >= (sizeof(F8X16) / OLED_GLYPH_BYTES)))
  {
    Char = ' ';
  }

  GlyphOffset = (uint16_t)(uint8_t)(Char - ' ') * OLED_GLYPH_BYTES;
  OLED_SetCursor((Line - 1U) * 2U, (Column - 1U) * 8U);
  OLED_WriteDataBlock(&F8X16[GlyphOffset], 8);
  OLED_SetCursor((Line - 1U) * 2U + 1U, (Column - 1U) * 8U);
  OLED_WriteDataBlock(&F8X16[GlyphOffset + 8U], 8);
}

void OLED_ShowLine(uint8_t Line, const char *String)
{
  uint8_t TopData[OLED_WIDTH];
  uint8_t BottomData[OLED_WIDTH];
  uint8_t Column;
  uint8_t CharIndex;
  uint16_t GlyphOffset;
  char Char;

  if ((Line == 0U) || (Line > OLED_LINE_COUNT))
  {
    return;
  }

  for (Column = 0; Column < OLED_CHARS_PER_LINE; Column++)
  {
    Char = ' ';
    if ((String != NULL) && (String[Column] != '\0'))
    {
      Char = String[Column];
    }
    if ((Char < ' ') || (Char > '~'))
    {
      Char = ' ';
    }

    GlyphOffset = (uint16_t)(uint8_t)(Char - ' ') * OLED_GLYPH_BYTES;
    for (CharIndex = 0; CharIndex < 8U; CharIndex++)
    {
      TopData[(Column * 8U) + CharIndex] = F8X16[GlyphOffset + CharIndex];
      BottomData[(Column * 8U) + CharIndex] = F8X16[GlyphOffset + 8U + CharIndex];
    }
  }

  OLED_SetCursor((Line - 1U) * 2U, 0);
  OLED_WriteDataBlock(TopData, OLED_WIDTH);
  OLED_SetCursor((Line - 1U) * 2U + 1U, 0);
  OLED_WriteDataBlock(BottomData, OLED_WIDTH);
}

void OLED_ShowString(uint8_t Line, uint8_t Column, const char *String)
{
  if (String == NULL)
  {
    return;
  }

  while ((*String != '\0') && (Column <= OLED_CHARS_PER_LINE))
  {
    OLED_ShowChar(Line, Column, *String);
    Column++;
    String++;
  }
}

void OLED_Init(void)
{
  static const uint8_t InitCommands[] = {
    0xAE,
    0xD5, 0x80,
    0xA8, 0x3F,
    0xD3, 0x00,
    0x40,
    0x8D, 0x14,
    0x20, 0x02,
    0xA1,
    0xC8,
    0xDA, 0x12,
    0x81, 0xCF,
    0xD9, 0xF1,
    0xDB, 0x30,
    0xA4,
    0xA6,
    0x2E,
    0xAF
  };

  HAL_Delay(100);
  OLED_DetectAddress();
  OLED_WriteCommandList(InitCommands, sizeof(InitCommands));
  OLED_Clear();
}
