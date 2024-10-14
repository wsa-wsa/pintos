#ifndef __LIB_KERNEL_CONSOLE_H
#define __LIB_KERNEL_CONSOLE_H

void console_init (void);
void console_panic (void);
void console_print_stats (void);
int console_write (int , const char *, int);
int console_read (int , char *, int);
#endif /**< lib/kernel/console.h */
