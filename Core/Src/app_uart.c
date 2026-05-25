#include "app_uart.h"

#include <limits.h>
#include <string.h>

/* 发送单个字节，适合 ACK、控制字符这类很短的数据。 */
void AppUart_SendByte(UART_HandleTypeDef *Huart, uint8_t Byte, uint32_t TimeoutMs)
{
  if (Huart == NULL)
  {
    return;
  }

  (void)HAL_UART_Transmit(Huart, &Byte, 1U, TimeoutMs);
}

/* 发送以 '\0' 结尾的字符串。 */
void AppUart_SendText(UART_HandleTypeDef *Huart, const char *Text, uint32_t TimeoutMs)
{
  const char *Cursor;
  size_t Remaining;

  if ((Huart == NULL) || (Text == NULL))
  {
    return;
  }

  Cursor = Text;
  Remaining = strlen(Text);

  while (Remaining > 0U)
  {
    uint16_t ChunkLength;

    /*
     * HAL_UART_Transmit 的长度参数是 uint16_t，而 strlen 返回 size_t。
     * 这里按 65535 字节分块发送，避免超长字符串被截断。
     */
    ChunkLength = (Remaining > (size_t)UINT16_MAX) ? UINT16_MAX : (uint16_t)Remaining;
    (void)HAL_UART_Transmit(Huart, (uint8_t *)(const void *)Cursor, ChunkLength, TimeoutMs);
    Cursor += ChunkLength;
    Remaining -= ChunkLength;
  }
}
