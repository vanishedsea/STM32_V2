#include "debug_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "board.h"

void debug_log(const char *format, ...)
{
    char buffer[192];
    va_list args;
    if (format == NULL) {
        return;
    }
    va_start(args, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length <= 0) {
        return;
    }
    size_t output = (size_t)length;
    if (output >= sizeof(buffer)) {
        output = sizeof(buffer) - 1U;
    }
    board_uart_write((const uint8_t *)buffer, output);
}

