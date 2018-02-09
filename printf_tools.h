
#ifndef	_PRINTF_TOOLS_H
#define	_PRINTF_TOOLS_H	1


#include <stdio.h>

static int uart_putchar(char c, FILE *stream);

static FILE mystdout = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);

static int uart_putchar(char c, FILE *stream)
{
    //loop_until_bit_is_set(UCSR0A, UDRE0);
    while (!( UCSR0A & (1<<UDRE0)));
    UDR0 = c;
    return 0;
}


static void init_printf_tools(void){
	  // init stdout
    stdout = &mystdout;
}


#endif
