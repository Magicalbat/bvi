#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "base_defs.h"
#include "config.h"

typedef enum {
    EM_NORMAL,
    EM_INSERT,
    EM_VISUAL,
    EM_VISUAL_BLOCK,
    EM_COMMAND,
} editor_mode;

typedef struct {
    u32 term_rows;
    u32 term_cols;
    editor_mode mode;
} editor_context;

static editor_context editor = {
    .mode = EM_NORMAL
};

static void win_change(int sig);

typedef struct {
    u8* mem;
    u64 mem_pos;
    u64 commit_size;
    u64 reserve_size;

    u8* file_data;
    u32 file_size;
    u32 file_max_size;

    u32 num_lines;

    i32 row_offset;
    i32 cursor_row;
    i32 cursor_col;
    i32 cursor_max_col;
} file_context;

static void fatal_error(string8 msg);

void* mem_reserve(u64 size);
b32 mem_commit(void* ptr, u64 size);
b32 mem_decommit(void* ptr, u64 size);
b32 mem_release(void* ptr, u64 size);
i32 mem_pagesize(void);

void exit_raw_mode(void);
void enter_raw_mode(void);

file_context* fc_open(const char* file_name);
void fc_close(file_context* fc);
void fc_expand(file_context* fc, u32 amount);
void fc_draw(file_context* fc);
void fc_scroll(file_context* fc, i32 amount);
void fc_move_cursor_y(file_context* fc, i32 amount);
void fc_move_cursor_x(file_context* fc, i32 amount);
void fc_insert_char(file_context* fc, u8 c);

static u8* draw_buf = NULL;

// Appends cursor move ANSI escape code to draw_buf
void set_cursor_pos(u32* draw_buf_pos, i32 row, i32 col);

#define DRAW_BUF_APPEND_POS(data, size, buf_pos) if ((buf_pos) + size <= DRAW_BUF_SIZE) { \
    memcpy(draw_buf + (buf_pos), (data), (size)); \
    (buf_pos) += (size); \
};

#define DRAW_BUF_APPEND(data, size) DRAW_BUF_APPEND_POS(data, size, buf_pos)

#define DRAW_BUF_APPEND_STR(str) DRAW_BUF_APPEND((str), sizeof(str) - 1)

#define CTRL_KEY(k) ((k) & 0x1f)

int main(int argc, char** argv) {
    if (argc <= 1) {
        return 1;
    }

    enter_raw_mode();

    win_change(0);
    signal(SIGWINCH, win_change);

    draw_buf = (u8*)malloc(DRAW_BUF_SIZE);

    file_context* fc = fc_open(argv[1]);

    u8 c = 0;
    b32 running = true;
    while (running) {
        fc_draw(fc);

        if (read(STDIN_FILENO, &c, 1) != 1) {
            break;
        }

        switch (editor.mode) {
            case EM_NORMAL:  {
                switch (c) {
                    case 'i':
                        editor.mode = EM_INSERT;
                        break;

                    case CTRL_KEY('y'):
                        fc_scroll(fc, -1);
                        break;
                    case CTRL_KEY('e'):
                        fc_scroll(fc, 1);
                        break;
                    case CTRL_KEY('u'): fc_scroll(fc, -(i32)editor.term_rows / 2);
                        break;
                    case CTRL_KEY('d'):
                        fc_scroll(fc, editor.term_rows / 2);
                        break;

                    case 'h':
                        fc_move_cursor_x(fc, -1);
                        break;
                    case 'j':
                        fc_move_cursor_y(fc, 1);
                        break;
                    case 'k':
                        fc_move_cursor_y(fc, -1);
                        break;
                    case 'l':
                        fc_move_cursor_x(fc, 1);
                        break;

                    case 'q':
                        running = false;
                        break;
                }
            } break;
            case EM_INSERT: { 
                if (c >= ' ' && c <= '~') {
                    fc_insert_char(fc, c);
                }

                if (c == '\x1b') {
                    editor.mode = EM_NORMAL;
                }
            } break;
            case EM_VISUAL: {
                // TODO
            } break;
            case EM_VISUAL_BLOCK:  {
                // TODO
            } break;
            case EM_COMMAND: { 
                // TODO
            } break;
        }
    }

    fc_close(fc);
    free(draw_buf);
    write(STDOUT_FILENO, "\x1b[0m\x1b[2J\x1b[H", 11);
    exit_raw_mode();

    return 0;
}

static void fatal_error(string8 msg) {
    string8 lines[] = {
        STR8_LIT(""),
        STR8_LIT("Fatal Error"),
        STR8_LIT(""),
        msg,
        STR8_LIT(""),
        STR8_LIT("Press any key to quit"),
        STR8_LIT(""),
    };
    u32 num_lines = sizeof(lines) / sizeof(lines[0]);

    // +1 is because the topleft is (1, 1)
    u32 center_x = editor.term_cols / 2 + 1;
    u32 center_y = editor.term_rows / 2 + 1;
    u32 height = num_lines;

    u32 width = 0;
    for (u32 i = 0; i < num_lines; i++) {
        if (lines[i].size > width) {
            width = lines[i].size;
        }
    }
    width += 2;

    u32 start_row = center_y - height / 2;
    u32 start_col = center_x - width / 2;

    u32 buf_pos = 0;

    // Red bg, white fg
    DRAW_BUF_APPEND_STR("\x1b[41m\x1b[97m");

    for (u32 i = 0; i < num_lines; i++) {
        set_cursor_pos(&buf_pos, start_row + i, start_col);

        u32 pad_left = (width - lines[i].size) / 2;
        u32 pad_right = (width - lines[i].size + 1) / 2;

        u8 c = ' ';
        for (u32 j = 0; j < pad_left; j++) { DRAW_BUF_APPEND(&c, 1); }
        DRAW_BUF_APPEND(lines[i].data, lines[i].size);
        for (u32 j = 0; j < pad_right; j++) { DRAW_BUF_APPEND(&c, 1); }
    }

    write(STDOUT_FILENO, draw_buf, buf_pos);

    // Wait for key
    u8 c = 0;
    read(STDIN_FILENO, &c, 1);

    write(STDOUT_FILENO, "\x1b[0m\x1b[2J\x1b[H", 11);
    exit_raw_mode();
    exit(1);
}

// TODO: automatically update the display when this happens
static void win_change(int sig) {
    (void)sig;

    struct winsize ws = { 0 };

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        fatal_error(STR8_LIT("Failed to get terminal size"));
    }

    editor.term_rows = ws.ws_row;
    editor.term_cols = ws.ws_col;
}

void* mem_reserve(u64 size) {
    void* out = mmap(NULL, size, PROT_NONE, MAP_SHARED | MAP_ANONYMOUS, -1, (off_t)0);
    return out;
}
b32 mem_commit(void* ptr, u64 size) {
    b32 out = (mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0);
    return out;
}
b32 mem_decommit(void* ptr, u64 size) {
    if (mprotect(ptr, size, PROT_NONE) == 0) {
        return false;
    }
    if (madvise(ptr, size, MADV_DONTNEED) == 0) {
        return false;
    }
    return true;
}
b32 mem_release(void* ptr, u64 size) {
    b32 out = (munmap(ptr, size) == 0);
    return out;
}
i32 mem_pagesize(void) {
    return (i32)sysconf(_SC_PAGESIZE);
}

static struct termios orig_termios = { 0 };

void exit_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enter_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(exit_raw_mode);

    struct termios raw = orig_termios;

    raw.c_iflag &= ~(ICRNL | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

u32 _num_digits(u32 n) {
    if (n < 10) return 1;
    if (n < 100) return 2;
    if (n < 1000) return 3;
    if (n < 10000) return 4;
    if (n < 100000) return 5;
    if (n < 1000000) return 6;
    return 7;
}

u32 _get_next_line_size(u8* str, u32 pos, u64 size) {
    if (pos >= size) return 0;

    u32 init_pos = pos;

    while (pos < size && str[pos] != '\n') {
        pos++;
    }

    return pos - init_pos;
}

file_context* fc_open(const char* file_name) {
    // For fail goto
    FILE* f = NULL;
    u8* mem = NULL;

    f = fopen(file_name, "r");

    if (f == NULL) {
        fatal_error(STR8_LIT("Cannot create file_context: failed to open file"));
        goto fail;
    }

    if (fseek(f, 0, SEEK_END) == -1) {
        fatal_error(STR8_LIT("Cannot create file_context: failed to get file size"));
        goto fail;
    }

    i64 file_size = ftell(f);

    if (file_size == -1) {
        fatal_error(STR8_LIT("Cannot create file_context: failed to get file size"));
        goto fail;
    }

    mem = (u8*)mem_reserve(FILE_MAX_SIZE);
    
    if (mem == NULL) {
        fatal_error(STR8_LIT("Cannot create file_context: cannot reserve memory"));
        return NULL;
    }

    u32 commit_size = ROUND_UP_POW2(sizeof(file_context) + file_size, FILE_COMMIT_SIZE);

    if (mem_commit(mem, commit_size) == false) {
        fatal_error(STR8_LIT("Cannot create file_context: cannot commit memory"));
        goto fail;
    }

    memset(mem, 0, commit_size);

    file_context* out = (file_context*)mem;
    out->mem = mem;
    out->mem_pos = sizeof(file_context);
    out->reserve_size = FILE_MAX_SIZE;
    out->commit_size = commit_size;
    out->file_data = out->mem + out->mem_pos;
    out->file_max_size = FILE_MAX_SIZE - out->mem_pos;
    out->file_size = file_size;

    out->row_offset = 0;
    out->cursor_row = 0;
    out->cursor_col = 0;
    out->cursor_max_col = 0;

    if (fseek(f, 0, SEEK_SET) == -1) {
        fatal_error(STR8_LIT("Cannot create file_context: failed to get file size"));
        goto fail;
    }

    // Technically, this would not work for files over the u32 limit, but
    // I am not supporting files that big anyways
    if (fread(out->file_data, 1, out->file_size, f) != out->file_size) {
        fatal_error(STR8_LIT("Cannot create file_context: failed to read file"));
        goto fail;
    }

    fclose(f);

    u32 file_pos = 0;
    while (file_pos < out->file_size) {
        u32 line_size = _get_next_line_size(out->file_data, file_pos, out->file_size);

        out->num_lines++;

        file_pos += line_size + 1;
    }

    return out;

fail:
    if (mem)
        mem_release(mem, FILE_MAX_SIZE);
    if (f == NULL)
        fclose(f);

    return NULL;
}

void fc_close(file_context* fc) {
    mem_release(fc->mem, fc->reserve_size);
}

void fc_expand(file_context* fc, u32 amount) {
    if (fc->mem_pos + amount > fc->commit_size) {
    }

    fc->mem_pos += amount;
}

// TODO: Not always draw the entire screen?
void fc_draw(file_context* fc) {
    u32 buf_pos = 0;
    u32 file_pos = 0;

    // Hides cursor
    DRAW_BUF_APPEND_STR("\x1b[?25l");
    // Clear Screen
    DRAW_BUF_APPEND_STR("\x1b[2J");
    // Put cursor to topleft corner
    DRAW_BUF_APPEND_STR("\x1b[H");

    for (u32 i = 0; i < (u32)MAX(0, fc->row_offset); i++) {
        u32 line_size = _get_next_line_size(fc->file_data, file_pos, fc->file_size);

        file_pos += line_size + 1;
    }

    u32 line_num_cols = 0;
    
#if ENABLE_LINE_NUMBERS == 1

    #define LINE_NUM_COL_STR "\x1b[38;2;99;99;99m" 

    u8 line_num_data[sizeof(LINE_NUM_COL_STR) + 11] = { 0 };
    u32 max_num_chars = _num_digits(fc->num_lines);
    line_num_cols = max_num_chars + 1; // For space after numbers
    u32 line_num_size = sizeof(LINE_NUM_COL_STR) + max_num_chars + 4;

    // Foreground color gray
    memcpy(line_num_data, LINE_NUM_COL_STR, sizeof(LINE_NUM_COL_STR) - 1);
    // Space for number
    memset(line_num_data + sizeof(LINE_NUM_COL_STR) - 1, ' ', max_num_chars + 1);
    // Foreground color back to default
    memcpy(line_num_data + max_num_chars + 17, "\x1b[0m", 4); 

#endif

    for (u32 y = 0; y < editor.term_rows; y++) {
        if (file_pos < fc->file_size) {
#if ENABLE_LINE_NUMBERS == 1
            i32 line = y + 1 + fc->row_offset;

#if RELATIVE_LINE_NUMBERS == 1
            line = line == fc->cursor_row + 1 ? line : ABS(fc->cursor_row + 1 - line);
#endif

            for (u32 i = 0; i < max_num_chars; i++) {
                u32 d = line % 10;
                line_num_data[sizeof(LINE_NUM_COL_STR) - 1 + max_num_chars - 1 - i] =
                    line ? d + '0' : ' ';
                line /= 10;
            }
            DRAW_BUF_APPEND(line_num_data, line_num_size);
#endif

            u32 line_size = _get_next_line_size(fc->file_data, file_pos, fc->file_size);

            DRAW_BUF_APPEND(fc->file_data + file_pos, line_size);

            // +1 for '\n'
            file_pos += line_size + 1;
        } else {
            DRAW_BUF_APPEND_STR("~");
        }

        if (y != editor.term_rows - 1) {
            DRAW_BUF_APPEND_STR("\r\n");
        }
    }

    i32 screen_cursor_row = fc->cursor_row - fc->row_offset + 1;
    i32 screen_cursor_col = fc->cursor_col + line_num_cols + 1;
    set_cursor_pos(&buf_pos, screen_cursor_row, screen_cursor_col);

    // Shows cursor
    DRAW_BUF_APPEND_STR("\x1b[?25h");
    if (editor.mode == EM_INSERT || editor.mode == EM_COMMAND) {
        // Bar cursor
        DRAW_BUF_APPEND_STR("\x1b[6 q");
    } else {
        // Block cursor
        DRAW_BUF_APPEND_STR("\x1b[2 q");
    }

    write(STDOUT_FILENO, draw_buf, buf_pos);
}

u32 _get_cur_row_pos(const file_context* fc) {
    u32 file_pos = 0;
    for (i32 i = 0; i < fc->cursor_row; i++) {
        u32 line_size = _get_next_line_size(fc->file_data, file_pos, fc->file_size);
        file_pos += line_size + 1;
    }

    return file_pos;
}

void fc_scroll(file_context* fc, i32 amount) {
    fc->row_offset = CLAMP(fc->row_offset + amount, 0, (i32)fc->num_lines - 2);
    fc->cursor_row = CLAMP(
        fc->cursor_row, fc->row_offset + 3,
        MIN((i32)fc->num_lines - 1, fc->row_offset + (i32)editor.term_rows - 3)
    );

    // Update cursor x if width of the new line is different
    fc_move_cursor_x(fc, 0);
}
void fc_move_cursor_y(file_context* fc, i32 amount) {
    fc->cursor_row = CLAMP(fc->cursor_row + amount, 0, (i32)fc->num_lines - 1);
    fc->row_offset = CLAMP(fc->row_offset, fc->cursor_row - (i32)editor.term_rows, fc->cursor_row);

    // Update cursor x if width of the new line is different
    fc_move_cursor_x(fc, 0);
}
void fc_move_cursor_x(file_context* fc, i32 amount) {
    i32 file_pos = _get_cur_row_pos(fc);
    i32 line_start = file_pos;
    i32 next_line_size = _get_next_line_size(fc->file_data, file_pos, fc->file_size);
    i32 line_end = file_pos + MAX(0, next_line_size - 1);
    file_pos += fc->cursor_col;

    if (amount == 0 && fc->cursor_col < fc->cursor_max_col) {
        amount = fc->cursor_max_col - fc->cursor_col;
    }

    i32 new_file_pos = CLAMP(file_pos + amount, line_start, line_end);
    i32 col_change = new_file_pos - file_pos;
    fc->cursor_col += col_change;

    if (amount < 0) {
        fc->cursor_max_col = fc->cursor_col;
    } else if (fc->cursor_col > fc->cursor_max_col) {
        fc->cursor_max_col = fc->cursor_col;
    }
}
void fc_insert_char(file_context* fc, u8 c) {
}

void set_cursor_pos(u32* draw_buf_pos, i32 row, i32 col) {
    u8 cursor_str[] = "\x1b[001;001H";

    i64 n = CLAMP(row, 1, (i32)editor.term_rows);
    for (u32 i = 2; i >= 0 && n; i--) {
        cursor_str[2 + i] = (n % 10) + '0';
        n /= 10;
    }
    n = CLAMP(col, 1, (i32)editor.term_cols);
    for (u32 i = 2; i >= 0 && n; i--) {
        cursor_str[6 + i] = (n % 10) + '0';
        n /= 10;
    }
    
    DRAW_BUF_APPEND_POS(cursor_str, sizeof(cursor_str) - 1, *draw_buf_pos);
}

