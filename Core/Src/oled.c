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
#define OLED_HANZI_BYTES         32

static uint16_t OLED_Address = OLED_I2C_ADDR_DEFAULT;

/* 右侧姓名区用到的 16x16 汉字字模索引。 */
enum
{
  HANZI_CAO = 0,
  HANZI_JUN,
  HANZI_YAN,
  HANZI_RUO,
  HANZI_LIN,
  HANZI_FU,
  HANZI_KAI
};

static const uint8_t HZ16[][OLED_HANZI_BYTES] = {
  {
    0x00, 0x02, 0xFA, 0xFA, 0xAA, 0xAA, 0xFF, 0xAA, 0xAA, 0xFF, 0xAA, 0xAA, 0xAA, 0xFA, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x7E, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x7E, 0x00, 0x00, 0x00
  },
  {
    0x00, 0x60, 0x30, 0xFC, 0x07, 0x80, 0xD8, 0x6C, 0xAA, 0x89, 0x08, 0x28, 0x2E, 0x4C, 0xC8, 0x00,
    0x00, 0x00, 0x00, 0x7F, 0x00, 0x20, 0x64, 0x26, 0x23, 0x3D, 0x19, 0x1D, 0x27, 0x20, 0x60, 0x00
  },
  {
    0x00, 0xF8, 0xF9, 0x06, 0x14, 0x10, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x02, 0x02, 0xFE, 0x00,
    0x00, 0x3F, 0x3F, 0x00, 0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x08, 0x20, 0x20, 0x3F, 0x00
  },
  {
    0x00, 0x44, 0x44, 0x44, 0x44, 0xDF, 0xC4, 0x74, 0x54, 0x44, 0x5F, 0x44, 0x44, 0x44, 0x44, 0x00,
    0x00, 0x04, 0x04, 0x06, 0x7F, 0x7F, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x3E, 0x00, 0x00, 0x00
  },
  {
    0x00, 0xFE, 0x92, 0xFE, 0x93, 0xFE, 0x92, 0xF2, 0x28, 0xAA, 0x18, 0x7F, 0x18, 0xBA, 0x28, 0x00,
    0x00, 0x1F, 0x00, 0x3F, 0x12, 0x1F, 0x1A, 0x40, 0x23, 0x1D, 0x07, 0x0C, 0x09, 0x7F, 0x09, 0x00
  },
  {
    0x00, 0x40, 0x30, 0xFE, 0x03, 0x04, 0xF4, 0xB4, 0xB4, 0xFF, 0xB4, 0xB5, 0xB5, 0xF4, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x3F, 0x00, 0x04, 0x05, 0x1C, 0x14, 0x05, 0x24, 0x24, 0x3E, 0x05, 0x04, 0x00
  },
  {
    0x00, 0x5E, 0x5E, 0x50, 0x5F, 0x50, 0xD0, 0x1E, 0x00, 0xFE, 0x02, 0x02, 0xFE, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x1F, 0x11, 0x11, 0x09, 0x29, 0x30, 0x1C, 0x07, 0x00, 0x00, 0x3F, 0x20, 0x38, 0x00
  }
};

/* 统一的 I2C 发送入口；默认地址失败时，自动尝试另一种常见地址。 */
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

/* 探测 OLED 实际使用的是 0x78 还是 0x7A。 */
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

/* 连续写命令，内部按 I2C 小块发送。 */
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

/* 连续写显示数据，内部按 I2C 小块发送。 */
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

/* SSD1306 的光标由页地址 Y 和列地址 X 组成。 */
static void OLED_SetCursor(uint8_t Y, uint8_t X)
{
  uint8_t Commands[3];

  Commands[0] = 0xB0 | Y;
  Commands[1] = 0x10 | ((X & 0xF0) >> 4);
  Commands[2] = 0x00 | (X & 0x0F);
  OLED_WriteCommandList(Commands, sizeof(Commands));
}

/* 逻辑上的第 1..4 行，每行占 2 个 page，因为字体高 16 像素。 */
static uint8_t OLED_LineTopPage(uint8_t Line)
{
  return (uint8_t)((Line - 1U) * 2U);
}

/* 1 个 8x16 ASCII 字符正好占 8 列像素。 */
static uint8_t OLED_ColumnToX(uint8_t Column)
{
  return (uint8_t)((Column - 1U) * 8U);
}

/* 清除某一页中的一段区域。 */
static void OLED_ClearPageArea(uint8_t Page, uint8_t X, uint8_t Width)
{
  uint8_t EmptyData[16] = {0};
  uint8_t Remaining;
  uint8_t Offset;

  Remaining = Width;
  Offset = 0U;
  while (Remaining > 0U)
  {
    uint8_t ChunkSize;

    ChunkSize = (Remaining > (uint8_t)sizeof(EmptyData)) ? (uint8_t)sizeof(EmptyData) : Remaining;
    OLED_SetCursor(Page, (uint8_t)(X + Offset));
    OLED_WriteDataBlock(EmptyData, ChunkSize);
    Remaining = (uint8_t)(Remaining - ChunkSize);
    Offset = (uint8_t)(Offset + ChunkSize);
  }
}

/* 写单条命令，主要留给外部模块按需使用。 */
void OLED_WriteCommand(uint8_t Command)
{
  OLED_WriteCommandList(&Command, 1);
}

/* 写单字节显示数据。 */
void OLED_WriteData(uint8_t Data)
{
  OLED_WriteDataBlock(&Data, 1);
}

/* 清空整个屏幕。 */
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

/* 清空逻辑上的某一整行。 */
void OLED_ClearLine(uint8_t Line)
{
  uint8_t EmptyLine[OLED_WIDTH] = {0};
  uint8_t TopPage;

  if ((Line == 0U) || (Line > OLED_LINE_COUNT))
  {
    return;
  }

  TopPage = OLED_LineTopPage(Line);
  OLED_SetCursor(TopPage, 0);
  OLED_WriteDataBlock(EmptyLine, OLED_WIDTH);
  OLED_SetCursor((uint8_t)(TopPage + 1U), 0);
  OLED_WriteDataBlock(EmptyLine, OLED_WIDTH);
}

/* 清空某一行中的一个 16 像素高矩形区域。 */
void OLED_ClearArea16(uint8_t Line, uint8_t X, uint8_t Width)
{
  uint8_t TopPage;

  if ((Line == 0U) || (Line > OLED_LINE_COUNT) || (X >= OLED_WIDTH) || (Width == 0U))
  {
    return;
  }
  if ((uint16_t)X + Width > OLED_WIDTH)
  {
    Width = (uint8_t)(OLED_WIDTH - X);
  }

  TopPage = OLED_LineTopPage(Line);
  OLED_ClearPageArea(TopPage, X, Width);
  OLED_ClearPageArea((uint8_t)(TopPage + 1U), X, Width);
}

/* 在指定字符坐标显示 1 个 8x16 ASCII 字符。 */
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
  OLED_SetCursor(OLED_LineTopPage(Line), OLED_ColumnToX(Column));
  OLED_WriteDataBlock(&F8X16[GlyphOffset], 8);
  OLED_SetCursor((uint8_t)(OLED_LineTopPage(Line) + 1U), OLED_ColumnToX(Column));
  OLED_WriteDataBlock(&F8X16[GlyphOffset + 8U], 8);
}

/* 按整行重画 16 个字符，适合启动页这类整行内容。 */
void OLED_ShowLine(uint8_t Line, const char *String)
{
  uint8_t TopData[OLED_WIDTH];
  uint8_t BottomData[OLED_WIDTH];
  uint8_t Column;
  uint8_t CharIndex;
  uint8_t TextEnded;
  uint16_t GlyphOffset;
  char Char;

  if ((Line == 0U) || (Line > OLED_LINE_COUNT))
  {
    return;
  }

  TextEnded = (String == NULL) ? 1U : 0U;
  for (Column = 0; Column < OLED_CHARS_PER_LINE; Column++)
  {
    Char = ' ';
    if (TextEnded == 0U)
    {
      if (String[Column] == '\0')
      {
        TextEnded = 1U;
      }
      else
      {
        Char = String[Column];
      }
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

  OLED_SetCursor(OLED_LineTopPage(Line), 0);
  OLED_WriteDataBlock(TopData, OLED_WIDTH);
  OLED_SetCursor((uint8_t)(OLED_LineTopPage(Line) + 1U), 0);
  OLED_WriteDataBlock(BottomData, OLED_WIDTH);
}

/* 只重画固定宽度的一段字符串，适合局部刷新。 */
void OLED_ShowFixedString(uint8_t Line, uint8_t Column, const char *String, uint8_t CharCount)
{
  uint8_t TopData[OLED_WIDTH];
  uint8_t BottomData[OLED_WIDTH];
  uint8_t CharIndex;
  uint8_t ByteIndex;
  uint8_t PixelWidth;
  uint8_t TextEnded;
  uint16_t GlyphOffset;
  char Char;

  if ((Line == 0U) || (Line > OLED_LINE_COUNT) ||
      (Column == 0U) || (Column > OLED_CHARS_PER_LINE) ||
      (CharCount == 0U))
  {
    return;
  }
  if (CharCount > (uint8_t)(OLED_CHARS_PER_LINE - Column + 1U))
  {
    CharCount = (uint8_t)(OLED_CHARS_PER_LINE - Column + 1U);
  }

  PixelWidth = (uint8_t)(CharCount * 8U);
  (void)memset(TopData, 0, sizeof(TopData));
  (void)memset(BottomData, 0, sizeof(BottomData));
  TextEnded = (String == NULL) ? 1U : 0U;
  for (CharIndex = 0U; CharIndex < CharCount; CharIndex++)
  {
    Char = ' ';
    if (TextEnded == 0U)
    {
      if (String[CharIndex] == '\0')
      {
        TextEnded = 1U;
      }
      else
      {
        Char = String[CharIndex];
      }
    }
    if ((Char < ' ') || (Char > '~'))
    {
      Char = ' ';
    }
    if (Char == ' ')
    {
      continue;
    }

    GlyphOffset = (uint16_t)(uint8_t)(Char - ' ') * OLED_GLYPH_BYTES;
    for (ByteIndex = 0U; ByteIndex < 8U; ByteIndex++)
    {
      TopData[(CharIndex * 8U) + ByteIndex] = F8X16[GlyphOffset + ByteIndex];
      BottomData[(CharIndex * 8U) + ByteIndex] = F8X16[GlyphOffset + 8U + ByteIndex];
    }
  }

  OLED_SetCursor(OLED_LineTopPage(Line), OLED_ColumnToX(Column));
  OLED_WriteDataBlock(TopData, PixelWidth);
  OLED_SetCursor((uint8_t)(OLED_LineTopPage(Line) + 1U), OLED_ColumnToX(Column));
  OLED_WriteDataBlock(BottomData, PixelWidth);
}

/* 从指定字符坐标开始连续写字符串，直到到达行尾。 */
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

/* 显示一个 16x16 点阵字模。 */
static void OLED_ShowGlyph16(uint8_t Line, uint8_t X, const uint8_t *Glyph)
{
  if ((Line == 0U) || (Line > OLED_LINE_COUNT) || (Glyph == NULL) || (X > (OLED_WIDTH - 16U)))
  {
    return;
  }

  OLED_SetCursor(OLED_LineTopPage(Line), X);
  OLED_WriteDataBlock(&Glyph[0], 16);
  OLED_SetCursor((uint8_t)(OLED_LineTopPage(Line) + 1U), X);
  OLED_WriteDataBlock(&Glyph[16], 16);
}

/* 在右侧固定区域显示姓名汉字标签。 */
void OLED_ShowNameLabels(void)
{
  OLED_ClearArea16(1, 80, 48);
  OLED_ClearArea16(2, 80, 48);
  OLED_ClearArea16(3, 80, 48);
  OLED_ClearArea16(4, 80, 48);

  OLED_ShowGlyph16(1, 96, HZ16[HANZI_CAO]);
  OLED_ShowGlyph16(1, 112, HZ16[HANZI_JUN]);

  OLED_ShowGlyph16(2, 80, HZ16[HANZI_YAN]);
  OLED_ShowGlyph16(2, 96, HZ16[HANZI_RUO]);
  OLED_ShowGlyph16(2, 112, HZ16[HANZI_LIN]);

  OLED_ShowGlyph16(3, 96, HZ16[HANZI_FU]);
  OLED_ShowGlyph16(3, 112, HZ16[HANZI_KAI]);
}

/* OLED 上电初始化。 */
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

  /* 有些模块上电后需要稍等一下再配置。 */
  HAL_Delay(100);
  OLED_DetectAddress();
  OLED_WriteCommandList(InitCommands, sizeof(InitCommands));
  OLED_Clear();
}
