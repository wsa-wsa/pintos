#include "devices/input.h"
#include <debug.h>
#include "devices/intq.h"
#include "devices/serial.h"

/** Stores keys from the keyboard and serial port. */
static struct intq buffer;

/** Initializes the input buffer. */
/** 初始化输出缓冲 */
void
input_init (void) 
{
  intq_init (&buffer);
}

/** Adds a key to the input buffer.
   Interrupts must be off and the buffer must not be full. 
   添加key到输入缓冲
*/
void
input_putc (uint8_t key) 
{
  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (!intq_full (&buffer));

  intq_putc (&buffer, key);
  serial_notify ();
}

/** Retrieves a key from the input buffer.
   If the buffer is empty, waits for a key to be pressed.
   从input检索一个key 
   */
uint8_t
input_getc (void) 
{
  enum intr_level old_level;
  uint8_t key;

  old_level = intr_disable ();
  key = intq_getc (&buffer);
  serial_notify ();
  intr_set_level (old_level);
  
  return key;
}

void 
input_delc (void)
{
  serial_putc('\b');  // 光标左移
  serial_putc(' ');   // 清空当前光标位置
  serial_putc('\b');  // 光标再次左移
}
/** Returns true if the input buffer is full,
   false otherwise.
   Interrupts must be off. */
bool
input_full (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);
  return intq_full (&buffer);
}
