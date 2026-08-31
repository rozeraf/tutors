#define _DEFAULT_SOURCE
#include "tutor.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#define RESET "\033[0m"
#define BOLD "\033[1m"
#define DIM "\033[2m"
#define CUR_HIDE "\033[?25l"
#define CUR_SHOW "\033[?25h"
#define CLR "\033[2J\033[H"
#define ALT_ON "\033[?1049h"
#define ALT_OFF "\033[?1049l"
#define C_KEY "\033[38;5;183m"
#define C_DESC "\033[38;5;252m"
#define C_HEAD "\033[38;5;150m"
#define C_SEP "\033[38;5;240m"
#define C_HINT "\033[38;5;109m"
#define C_CUR "\033[48;5;237m\033[38;5;255m"
#define C_CODE "\033[38;5;222m"

static char *frame;
static size_t frame_cap;
static size_t frame_len;
static struct termios original_term;
static int term_is_raw;

static void write_all(const void *buffer, size_t length) {
  const char *cursor = buffer;
  while (length) {
    ssize_t written = write(STDOUT_FILENO, cursor, length);
    if (written <= 0) return;
    cursor += written;
    length -= (size_t)written;
  }
}

static void frame_reset(void) { frame_len = 0; }

static void frame_append(const char *text) {
  size_t length = strlen(text);
  if (frame_len + length + 1 > frame_cap) {
    size_t next_cap = frame_cap ? frame_cap * 2 : 8192;
    while (next_cap < frame_len + length + 1) next_cap *= 2;
    char *next = realloc(frame, next_cap);
    if (!next) return;
    frame = next;
    frame_cap = next_cap;
  }
  memcpy(frame + frame_len, text, length + 1);
  frame_len += length;
}

static void frame_appendf(const char *format, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  frame_append(buffer);
}

static void frame_flush(void) {
  if (frame_len) write_all(frame, frame_len);
  frame_reset();
}

static void term_restore(void) {
  if (!term_is_raw) return;
  tcsetattr(STDIN_FILENO, TCSANOW, &original_term);
  write_all(CUR_SHOW ALT_OFF, sizeof(CUR_SHOW ALT_OFF) - 1);
  term_is_raw = 0;
}

static void handle_signal(int signal_number) {
  (void)signal_number;
  term_restore();
  _exit(0);
}

static int term_enable(void) {
  if (tcgetattr(STDIN_FILENO, &original_term) != 0) return -1;
  struct termios raw = original_term;
  raw.c_lflag &= ~(ICANON | ECHO);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return -1;
  term_is_raw = 1;

  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = handle_signal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGTERM, &action, NULL);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGHUP, &action, NULL);
  return 0;
}

static int read_byte_with_timeout(unsigned char *value, long microseconds) {
  fd_set descriptors;
  struct timeval timeout = {microseconds / 1000000, microseconds % 1000000};
  FD_ZERO(&descriptors);
  FD_SET(STDIN_FILENO, &descriptors);
  if (select(STDIN_FILENO + 1, &descriptors, NULL, NULL, &timeout) <= 0)
    return 0;
  return read(STDIN_FILENO, value, 1) == 1;
}

static int read_key(void) {
  unsigned char first;
  if (read(STDIN_FILENO, &first, 1) != 1) return -1;
  if (first != 27) return first;

  unsigned char second, third;
  if (!read_byte_with_timeout(&second, 70000)) return 27;
  if (!read_byte_with_timeout(&third, 70000)) return 27;
  if (second == '[' && third == 'A') return 'k';
  if (second == '[' && third == 'B') return 'j';
  return 0;
}

static int menu_choice(int key) {
  if (key < '1' || key > '9') return -1;
  int choice = key - '0';
  if (key == '1' && tutor_config.section_count >= 10) {
    unsigned char second;
    if (read_byte_with_timeout(&second, 250000) && second >= '0' && second <= '9')
      choice = 10 + second - '0';
  }
  return choice > 0 && (size_t)choice <= tutor_config.section_count
             ? choice - 1 : -1;
}

static int terminal_rows(void) {
  struct winsize size;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 6)
    return size.ws_row;
  return 24;
}

typedef struct { char text[1024]; } DisplayLine;
static DisplayLine display[1200];
static int display_count;

static void display_add(const char *text) {
  if (display_count < (int)(sizeof(display) / sizeof(display[0])))
    snprintf(display[display_count++].text, sizeof(display[0].text), "%s", text);
}

static void build_section(const char *const *section) {
  char buffer[1024];
  display_count = 0;
  for (size_t index = 0; section[index]; ++index) {
    char type = section[index][0];
    const char *content = section[index] + 2;
    if (type == 'T') {
      display_add(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" RESET);
      snprintf(buffer, sizeof(buffer), "%s%s  %s%s", tutor_config.title_color,
               BOLD, content, RESET);
      display_add(buffer);
      display_add(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" RESET);
    } else if (type == 'G') {
      display_add("");
      snprintf(buffer, sizeof(buffer), C_HEAD BOLD "  ## %s" RESET, content);
      display_add(buffer);
    } else if (type == 'R') {
      const char *separator = strchr(content, '|');
      char key[256] = "";
      const char *description = "";
      if (separator) {
        size_t length = (size_t)(separator - content);
        if (length >= sizeof(key)) length = sizeof(key) - 1;
        memcpy(key, content, length);
        key[length] = '\0';
        description = separator + 1;
      } else {
        snprintf(key, sizeof(key), "%s", content);
      }
      snprintf(buffer, sizeof(buffer), "  " C_KEY BOLD "%-*s" RESET C_DESC
               "  %s" RESET, (int)tutor_config.key_width, key, description);
      display_add(buffer);
    } else if (type == 'C') {
      snprintf(buffer, sizeof(buffer), C_CODE "  $ %s" RESET, content);
      display_add(buffer);
    } else if (type == 'N') {
      snprintf(buffer, sizeof(buffer), C_HINT DIM "  > %s" RESET, content);
      display_add(buffer);
    } else {
      display_add("");
    }
  }
}

static void show_section(const char *const *section) {
  build_section(section);
  int cursor = 0, offset = 0, last_g = 0;
  while (1) {
    int visible = terminal_rows() - 3;
    if (cursor < 0) cursor = 0;
    if (cursor >= display_count) cursor = display_count - 1;
    if (cursor < offset) offset = cursor;
    if (cursor >= offset + visible) offset = cursor - visible + 1;

    frame_reset();
    frame_append(CLR);
    for (int i = offset; i < offset + visible && i < display_count; ++i)
      frame_appendf(i == cursor ? C_CUR "%s" RESET "\n" : "%s\n", display[i].text);
    frame_append(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
    frame_appendf(C_HINT "  j/k↕  d/u ½ экрана  gg/G края  h/q назад" C_SEP
                  "  [%d/%d]\n" RESET, cursor + 1, display_count);
    frame_flush();

    int key = read_key();
    if (key == 'j') cursor++;
    else if (key == 'k') cursor--;
    else if (key == 'd') cursor += visible / 2;
    else if (key == 'u') cursor -= visible / 2;
    else if (key == 'g' && last_g) { cursor = 0; offset = 0; last_g = 0; }
    else if (key == 'g') last_g = 1;
    else if (key == 'G') { cursor = display_count - 1; last_g = 0; }
    else if (key == '%' ) { cursor = cursor < display_count / 2 ? display_count - 1 : 0; last_g = 0; }
    else if (key == 'h' || key == 'q' || key == 'x' || key == 27) return;
    else last_g = 0;
  }
}

static void show_menu(int cursor) {
  frame_reset();
  frame_append(CLR);
  frame_appendf("%s%s\n  %s\n%s", tutor_config.title_color, BOLD,
                tutor_config.title, RESET);
  frame_appendf(C_HINT DIM "  %s\n" RESET, tutor_config.tagline);
  frame_append(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
  for (size_t i = 0; i < tutor_config.section_count; ++i) {
    if ((int)i == cursor)
      frame_appendf(C_CUR BOLD "  ▶  %s" RESET "\n", tutor_config.sections[i].label);
    else
      frame_appendf(C_KEY "  [%zu]" C_DESC "  %s\n" RESET, i + 1,
                    tutor_config.sections[i].label);
  }
  frame_append(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
  frame_append(C_HINT "  j/k выбор   l/Enter открыть   gg/G края   q выход\n" RESET);
  frame_flush();
}

int main(void) {
  if (term_enable() != 0) {
    fprintf(stderr, "%s requires an interactive terminal\n", tutor_config.title);
    return 1;
  }
  atexit(term_restore);
  write_all(ALT_ON CUR_HIDE, sizeof(ALT_ON CUR_HIDE) - 1);

  int cursor = 0, last_g = 0;
  while (1) {
    show_menu(cursor);
    int key = read_key();
    int choice = menu_choice(key);
    if (key == 'j' && (size_t)(cursor + 1) < tutor_config.section_count) { cursor++; last_g = 0; }
    else if (key == 'k' && cursor > 0) { cursor--; last_g = 0; }
    else if (key == 'g' && last_g) { cursor = 0; last_g = 0; }
    else if (key == 'g') last_g = 1;
    else if (key == 'G') { cursor = (int)tutor_config.section_count - 1; last_g = 0; }
    else if (key == '%') { cursor = cursor ? 0 : (int)tutor_config.section_count - 1; last_g = 0; }
    else if (key == 'l' || key == '\r' || key == '\n') { show_section(tutor_config.sections[cursor].lines); last_g = 0; }
    else if (choice >= 0) { cursor = choice; show_section(tutor_config.sections[cursor].lines); last_g = 0; }
    else if (key == 'q' || key == 'x' || key == -1) break;
    else last_g = 0;
  }

  frame_reset();
  frame_append(CLR C_HINT "\n  bye\n\n" RESET);
  frame_flush();
  free(frame);
  return 0;
}
