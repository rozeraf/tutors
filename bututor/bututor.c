#define _DEFAULT_SOURCE
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
#define C_TITLE "\033[38;5;208m"
#define C_KEY "\033[38;5;183m"
#define C_DESC "\033[38;5;252m"
#define C_HEAD "\033[38;5;150m"
#define C_SEP "\033[38;5;240m"
#define C_HINT "\033[38;5;109m"
#define C_CUR "\033[48;5;237m\033[38;5;255m"
#define C_CODE "\033[38;5;222m"

static char *fbuf;
static size_t fbuf_cap;
static size_t fbuf_len;
static struct termios orig_term;
static int term_is_raw;

static void xwrite(const void *buf, size_t n) {
  ssize_t written = write(STDOUT_FILENO, buf, n);
  (void)written;
}

static void fb_reset(void) { fbuf_len = 0; }

static void fb_append(const char *s) {
  size_t n = strlen(s);
  if (fbuf_len + n + 1 > fbuf_cap) {
    size_t cap = fbuf_cap ? fbuf_cap * 2 : 8192;
    while (cap < fbuf_len + n + 1)
      cap *= 2;
    char *next = realloc(fbuf, cap);
    if (!next)
      return;
    fbuf = next;
    fbuf_cap = cap;
  }
  memcpy(fbuf + fbuf_len, s, n + 1);
  fbuf_len += n;
}

static void fb_appendf(const char *fmt, ...) {
  char tmp[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  fb_append(tmp);
}

static void fb_flush(void) {
  if (fbuf_len)
    xwrite(fbuf, fbuf_len);
  fb_reset();
}

static void term_restore(void) {
  if (!term_is_raw)
    return;
  tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
  xwrite(CUR_SHOW ALT_OFF, sizeof(CUR_SHOW ALT_OFF) - 1);
  term_is_raw = 0;
}

static void sig_handler(int sig) {
  (void)sig;
  term_restore();
  _exit(0);
}

static void term_raw(void) {
  struct termios t;
  if (tcgetattr(STDIN_FILENO, &orig_term) != 0)
    return;
  t = orig_term;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
  term_is_raw = 1;

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sig_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGHUP, &sa, NULL);
}

static int read_key(void) {
  unsigned char c;
  if (read(STDIN_FILENO, &c, 1) != 1)
    return -1;
  if (c != 27)
    return c;

  fd_set fds;
  struct timeval tv = {0, 70000};
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0)
    return 27;

  unsigned char seq[2];
  if (read(STDIN_FILENO, &seq[0], 1) != 1 ||
      read(STDIN_FILENO, &seq[1], 1) != 1)
    return 27;
  if (seq[0] == '[' && seq[1] == 'A')
    return 'k';
  if (seq[0] == '[' && seq[1] == 'B')
    return 'j';
  return 0;
}

static int term_rows(void) {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 6)
    return w.ws_row;
  return 24;
}

/* T title, G group, R command|description, C example, N note, B blank. */
static const char *sec_model[] = {
    "T:МОДЕЛЬ GITBUTLER",
    "G:Workspace вместо checkout",
    "N:gitbutler/workspace объединяет все applied-ветки в одной рабочей копии.",
    "N:Независимые задачи живут параллельно; зависимые ветки образуют stack.",
    "R:but status|ветки, stacks, коммиты, конфликты и uncommitted changes",
    "R:but status -fv|подробности файлов, hunks и содержимого коммитов",
    "R:but diff|все незакоммиченные изменения и их CLI IDs",
    "R:but show <id>|подробности одной ветки или коммита",
    "B:",
    "G:Начальная настройка",
    "R:but setup|подключить текущий Git-репозиторий к GitButler",
    "R:but teardown|выйти из GitButler mode",
    "R:but gui|открыть графический интерфейс проекта",
    "R:but tui|открыть live terminal workspace",
    "R:but config|просмотр и изменение настроек CLI",
    "N:Git-команды чтения допустимы, но изменения истории делай через but.",
    NULL};

static const char *sec_ids[] = {
    "T:CLI IDs И ПРОСМОТР",
    "G:Как читать вывод",
    "N:Первый токен строки в but diff/status — ID для следующих команд.",
    "R:ab|ID файла, ветки, stack или коммита — зависит от строки",
    "R:ab:3|hunk 3 файла ab; число после двоеточия не диапазон строк",
    "R:xy:n|файл n внутри уже созданного коммита xy",
    "R:zz|вся незакоммиченная область",
    "B:",
    "G:Правила выбора",
    "N:Копируй IDs из свежего вывода; не придумывай и не разделяй запятыми.",
    "N:Commit change IDs стабильны после move/squash; SHA может устареть.",
    "R:but diff <id>|diff одного файла, коммита, ветки или stack",
    "R:but help cli-ids|полная справка по типам и стабильности IDs",
    "C:but diff",
    "C:but commit -b feat/login -m \"Validate login\" ab:3 cd",
    "B:",
    "G:Чтение файла из существующего коммита",
    "R:but show cl|показать ветку cl (cleanup-zsh-aliases) и её коммиты",
    "R:but diff vqw|показать diff всего коммита vqw",
    "R:but status -fv|найти ID нужного файла внутри коммита",
    "N:В коммите vqw строка файла выглядит: vqw:rq M zsh/.zshrc",
    "C:but diff vqw:rq",
    "N:Формат committed file: <commit-id>:<file-id>. Ветка нужна для поиска,",
    "N:но в but diff не передаётся: commit ID vqw уже однозначно задаёт коммит.",
    NULL};

static const char *sec_commits[] = {
    "T:КОММИТЫ И ИЗМЕНЕНИЯ",
    "G:Создание коммитов",
    "R:but commit -b feat -m \"msg\"|все uncommitted changes в ветку feat",
    "R:but commit -b feat -m \"msg\" ab cd|только выбранные файлы/hunks",
    "R:but commit --empty -b feat -m \"msg\"|пустой коммит",
    "R:but commit --above <id> -m \"msg\" ab|вставить коммит выше цели",
    "R:but commit --below <id> -m \"msg\" ab|вставить коммит ниже цели",
    "N:-b создаёт ветку, если её ещё нет. При нескольких stacks цель обязательна.",
    "B:",
    "G:Исправление и отмена",
    "R:but amend -t <commit> ab ab:3|добавить выбранные changes в коммит",
    "R:but uncommit <commit>|вернуть весь коммит в uncommitted area",
    "R:but uncommit <commit>:<file>|вернуть из коммита только один файл",
    "R:but reword <commit> -m \"new msg\"|изменить сообщение коммита",
    "R:but discard ab|удалить change; восстановление возможно через undo",
    "R:but absorb|распределить changes по подходящим коммитам",
    "N:Нет staging area и stash: изменения назначаются прямо коммитам/веткам.",
    NULL};

static const char *sec_branches[] = {
    "T:ВЕТКИ И STACKS",
    "G:Ветки в workspace",
    "R:but branch|список веток",
    "R:but branch new <name>|создать независимую ветку",
    "R:but branch new <name> -a <base>|создать stacked-ветку над base",
    "R:but apply <branch>|добавить ветку в workspace",
    "R:but unapply <branch>|убрать весь stack из workspace",
    "R:but branch delete <id>|удалить ветку",
    "R:but branch show <id> -f|коммиты и файлы ветки",
    "B:",
    "G:Stacking и перенос",
    "R:but move child --above parent|поставить child поверх parent",
    "R:but move <branch> --unstack|сделать ветку независимой",
    "R:but move <commit> -b <branch>|перенести коммит на вершину ветки",
    "R:but pick <commit> <branch>|cherry-pick в applied-ветку",
    "N:Applied-ветки видны одновременно — переключать checkout не требуется.",
    NULL};

static const char *sec_history[] = {
    "T:РЕДАКТИРОВАНИЕ ИСТОРИИ",
    "G:Squash",
    "R:but squash a b -t c -m \"msg\"|склеить a и b в целевой коммит c",
    "R:but squash <branch> -m \"msg\"|всю ветку превратить в один коммит",
    "R:but squash a -t c --use-target-message|оставить сообщение цели",
    "B:",
    "G:Порядок коммитов",
    "R:but move a --above b|поставить a выше b (новее в status)",
    "R:but move a --below b|поставить a ниже b (старее в status)",
    "R:but move a b --above c|перенести соседний блок, сохранив порядок",
    "N:but status показывает историю сверху вниз: newest → oldest.",
    "B:",
    "G:Безопасная сетка",
    "R:but oplog|журнал всех операций GitButler",
    "R:but undo|отменить последнюю операцию",
    "R:but redo|повторить отменённую операцию",
    "N:После мутации доверяй её результату; --status-after нужен не всегда.",
    NULL};

static const char *sec_remote[] = {
    "T:REMOTE И PULL REQUESTS",
    "G:Синхронизация",
    "R:but pull|fetch + перенос applied stacks на свежую target-ветку",
    "R:but pull --check|предпросмотр pull без изменений",
    "R:but push <branch>|отправить только названную ветку",
    "R:but push <branch> --dry-run|показать будущий push",
    "N:Не запускай голый but push в automation: он может push-нуть все ветки.",
    "B:",
    "G:Pull requests",
    "R:but pr new <branch> -m \"Title\"|push ветки + создание PR",
    "R:but pr new <top> -t|опубликовать stack с default-сообщениями",
    "R:but pr new <branch> --draft -m \"Title\"|создать draft PR",
    "R:but pr set-ready <selector>|перевести review в ready",
    "R:but pr set-draft <selector>|вернуть review в draft",
    "R:but pr auto-merge <selector>|включить auto-merge",
    "N:Для stacked PR используй but pr: он правильно выставляет bases и metadata.",
    NULL};

static const char *sec_conflicts[] = {
    "T:КОНФЛИКТЫ",
    "G:Конфликтный коммит",
    "R:but resolve <commit>|войти в режим разрешения указанного коммита",
    "R:but resolve status|показать оставшиеся конфликтные файлы",
    "R:but resolve finish|завершить разрешение и продолжить rebase",
    "R:but resolve cancel|отменить текущую сессию разрешения",
    "N:Редактируй файлы и удаляй <<<<<<<, |||||||, =======, >>>>>>>.",
    "N:Не используй git add/commit/checkout. Решай коммиты снизу вверх.",
    "B:",
    "G:Uncommitted conflict",
    "N:Сначала оставь нужное содержимое файла, затем отметь его resolved.",
    "R:git add -- <file>|единственное исключение: resolved uncommitted file",
    "B:",
    "G:Dependency conflict",
    "R:but move feature --above dependency|stack поверх единственной зависимости",
    "R:but status -fv|найти несколько зависимостей и выбрать placement",
    NULL};

static const char *sec_workflow[] = {
    "T:ПРАКТИЧЕСКИЕ WORKFLOWS",
    "G:Одна независимая задача",
    "C:but diff",
    "C:but commit -b feat/search -m \"Add search\" ab cd",
    "C:but push feat/search",
    "C:but pr new feat/search -m \"Add search\"",
    "B:",
    "G:Две параллельные задачи",
    "C:but commit -b fix/header -m \"Fix header\" ab",
    "C:but commit -b docs/setup -m \"Document setup\" cd",
    "N:Обе applied-ветки остаются видимыми в одном workspace.",
    "B:",
    "G:Зависимый stack",
    "C:but branch new ui --anchor api",
    "C:but commit -b ui -m \"Build UI on API\" ef",
    "C:but pr new ui -t",
    "B:",
    "G:Разделение старого коммита",
    "C:but uncommit <commit> && but diff",
    "C:but commit -b feat -m \"First concern\" ab",
    "C:but commit -b feat -m \"Second concern\" cd",
    NULL};

static const char *sec_tools[] = {
    "T:ИНСТРУМЕНТЫ И СПРАВКА",
    "G:Интерфейсы",
    "R:but tui|интерактивный terminal workspace",
    "R:but gui|GitButler Desktop для текущего проекта",
    "R:but --json status|машиночитаемый вывод для scripts/agents",
    "R:BUT_OUTPUT_FORMAT=json but status|JSON через переменную окружения",
    "B:",
    "G:AI integration",
    "R:but skill install|установить официальный skill для coding agent",
    "R:but agent|настроить GitButler для AI coding agents",
    "R:but branch show <id> --ai|AI summary изменений ветки",
    "B:",
    "G:Справка и обновление",
    "R:but <command> --help|синтаксис конкретной команды",
    "R:but completions zsh|сгенерировать shell completions",
    "R:but update|управление обновлениями CLI/Desktop",
    "R:but clean|удалить пустые ветки",
    NULL};

#define FLAT_MAX 1000
#define FLAT_LEN 512
typedef struct { char text[FLAT_LEN]; } FlatLine;
static FlatLine flat[FLAT_MAX];
static int flat_total;

static void flat_add(const char *s) {
  if (flat_total >= FLAT_MAX)
    return;
  snprintf(flat[flat_total++].text, FLAT_LEN, "%s", s);
}

static void flat_build(const char **section) {
  char buf[FLAT_LEN];
  flat_total = 0;
  for (int i = 0; section[i]; i++) {
    char type = section[i][0];
    const char *content = section[i] + 2;
    if (type == 'T') {
      flat_add(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" RESET);
      snprintf(buf, sizeof(buf), C_TITLE BOLD "  %s" RESET, content);
      flat_add(buf);
      flat_add(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" RESET);
    } else if (type == 'G') {
      flat_add("");
      snprintf(buf, sizeof(buf), C_HEAD BOLD "  ## %s" RESET, content);
      flat_add(buf);
    } else if (type == 'R') {
      const char *pipe = strchr(content, '|');
      char key[128] = "";
      const char *desc = "";
      if (pipe) {
        size_t len = (size_t)(pipe - content);
        if (len >= sizeof(key))
          len = sizeof(key) - 1;
        memcpy(key, content, len);
        key[len] = '\0';
        desc = pipe + 1;
      } else {
        snprintf(key, sizeof(key), "%s", content);
      }
      snprintf(buf, sizeof(buf), "  " C_KEY BOLD "%-42s" RESET C_DESC "  %s" RESET,
               key, desc);
      flat_add(buf);
    } else if (type == 'C') {
      snprintf(buf, sizeof(buf), C_CODE "  $ %s" RESET, content);
      flat_add(buf);
    } else if (type == 'N') {
      snprintf(buf, sizeof(buf), C_HINT DIM "  > %s" RESET, content);
      flat_add(buf);
    } else {
      flat_add("");
    }
  }
}

static void view_section(const char **section) {
  flat_build(section);
  int cursor = 0;
  int offset = 0;
  int last_g = 0;

  while (1) {
    int visible = term_rows() - 3;
    if (cursor < 0)
      cursor = 0;
    if (cursor >= flat_total)
      cursor = flat_total - 1;
    if (cursor < offset)
      offset = cursor;
    if (cursor >= offset + visible)
      offset = cursor - visible + 1;

    fb_reset();
    fb_append(CLR);
    for (int i = offset; i < offset + visible && i < flat_total; i++) {
      if (i == cursor)
        fb_appendf(C_CUR "%s" RESET "\n", flat[i].text);
      else
        fb_appendf("%s\n", flat[i].text);
    }
    fb_append(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
    fb_appendf(C_HINT "  j/k↕  d/u ½ экрана  gg/G края  h/q назад" C_SEP
                      "  [%d/%d]\n" RESET, cursor + 1, flat_total);
    fb_flush();

    int key = read_key();
    if (key == 'j') cursor++;
    else if (key == 'k') cursor--;
    else if (key == 'd') cursor += visible / 2;
    else if (key == 'u') cursor -= visible / 2;
    else if (key == 'g' && last_g) { cursor = 0; offset = 0; last_g = 0; }
    else if (key == 'g') last_g = 1;
    else if (key == 'G') { cursor = flat_total - 1; last_g = 0; }
    else if (key == 'h' || key == 'q' || key == 'x' || key == 27) break;
    else last_g = 0;
  }
}

#define MENU_N 9
static const char *menu_labels[MENU_N] = {
    "Модель workspace и настройка",
    "CLI IDs, status, diff и show",
    "Коммиты, amend и changes",
    "Ветки и stacks",
    "Редактирование истории и oplog",
    "Remote, push, pull и PR",
    "Конфликты и восстановление",
    "Практические workflows",
    "TUI, GUI, agents и справка",
};
static const char **menu_sections[MENU_N] = {
    sec_model, sec_ids, sec_commits, sec_branches, sec_history,
    sec_remote, sec_conflicts, sec_workflow, sec_tools,
};

static void print_menu(int cur) {
  fb_reset();
  fb_append(CLR C_TITLE BOLD
    "\n"
    "  ██████╗ ██╗   ██╗████████╗██╗   ██╗████████╗ ██████╗ ██████╗ \n"
    "  ██╔══██╗██║   ██║╚══██╔══╝██║   ██║╚══██╔══╝██╔═══██╗██╔══██╗\n"
    "  ██████╔╝██║   ██║   ██║   ██║   ██║   ██║   ██║   ██║██████╔╝\n"
    "  ██╔══██╗██║   ██║   ██║   ██║   ██║   ██║   ██║   ██║██╔══██╗\n"
    "  ██████╔╝╚██████╔╝   ██║   ╚██████╔╝   ██║   ╚██████╔╝██║  ██║\n"
    "  ╚═════╝  ╚═════╝    ╚═╝    ╚═════╝    ╚═╝    ╚═════╝ ╚═╝  ╚═╝\n" RESET);
  fb_append(C_HINT DIM "  GitButler CLI · workspace · stacks · commits · reviews · undo\n" RESET);
  fb_append(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
  for (int i = 0; i < MENU_N; i++) {
    if (i == cur)
      fb_appendf(C_CUR BOLD "  ▶  %s" RESET "\n", menu_labels[i]);
    else
      fb_appendf(C_KEY "  [%d]" C_DESC "  %s\n" RESET, i + 1, menu_labels[i]);
  }
  fb_append(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
  fb_append(C_HINT "  j/k выбор   l/Enter открыть   gg/G края   q выход\n" RESET);
  fb_flush();
}

int main(void) {
  term_raw();
  atexit(term_restore);
  xwrite(ALT_ON CUR_HIDE, sizeof(ALT_ON CUR_HIDE) - 1);

  int cur = 0;
  int last_g = 0;
  while (1) {
    print_menu(cur);
    int key = read_key();
    if (key == 'j' && cur < MENU_N - 1) { cur++; last_g = 0; }
    else if (key == 'k' && cur > 0) { cur--; last_g = 0; }
    else if (key == 'g' && last_g) { cur = 0; last_g = 0; }
    else if (key == 'g') last_g = 1;
    else if (key == 'G') { cur = MENU_N - 1; last_g = 0; }
    else if (key == 'l' || key == '\r' || key == '\n') {
      view_section(menu_sections[cur]);
      last_g = 0;
    } else if (key >= '1' && key <= '9') {
      cur = key - '1';
      view_section(menu_sections[cur]);
      last_g = 0;
    } else if (key == 'q' || key == 'x') {
      fb_reset();
      fb_append(CLR C_HINT "\n  bye\n\n" RESET);
      fb_flush();
      return 0;
    } else {
      last_g = 0;
    }
  }
}
