#include <errno.h>
#include <sys/stat.h>
#include "board.h"

int _write(int file, char *ptr, int len)
{
    (void)file;
    if ((ptr == NULL) || (len <= 0)) {
        return 0;
    }
    board_uart_write((const uint8_t *)ptr, (size_t)len);
    return len;
}

int _close(int file) { (void)file; return -1; }
int _fstat(int file, struct stat *st) { (void)file; st->st_mode = S_IFCHR; return 0; }
int _isatty(int file) { (void)file; return 1; }
int _lseek(int file, int ptr, int dir) { (void)file; (void)ptr; (void)dir; return 0; }
int _read(int file, char *ptr, int len) { (void)file; (void)ptr; (void)len; return 0; }
void *_sbrk(ptrdiff_t increment) { (void)increment; errno = ENOMEM; return (void *)-1; }

