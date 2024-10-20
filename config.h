#ifndef CONFIG_H
#define CONFIG_H

#define FILE_MAX_SIZE MiB(1)
// Must be power of two
#define FILE_COMMIT_SIZE KiB(16)

// Size of buffer used to output to screen
#define DRAW_BUF_SIZE KiB(256)

#define ENABLE_LINE_NUMBERS 1
#define RELATIVE_LINE_NUMBERS 0

#endif // CONFIG_H

