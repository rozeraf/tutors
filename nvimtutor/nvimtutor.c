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

/* ── ANSI ───────────────────────────────────────────────────────────── */
#define RESET "\033[0m"
#define BOLD "\033[1m"
#define DIM "\033[2m"
#define CUR_HIDE "\033[?25l"
#define CUR_SHOW "\033[?25h"
#define CLR "\033[2J\033[H"
#define ALT_ON "\033[?1049h"
#define ALT_OFF "\033[?1049l"

#define C_TITLE "\033[38;5;111m"
#define C_KEY "\033[38;5;183m"
#define C_DESC "\033[38;5;252m"
#define C_HEAD "\033[38;5;150m"
#define C_SEP "\033[38;5;240m"
#define C_HINT "\033[38;5;109m"
#define C_CUR "\033[48;5;237m\033[38;5;255m"

/* ── frame buffer ───────────────────────────────────────────────────── */
static char *fbuf = NULL;
static size_t fbuf_cap = 0;
static size_t fbuf_len = 0;

/* suppress warn_unused_result for terminal write calls */
static void xwrite(const void *buf, size_t n) {
  ssize_t r = write(STDOUT_FILENO, buf, n);
  (void)r;
}

static void fb_reset(void) { fbuf_len = 0; }

static void fb_append(const char *s) {
  size_t n = strlen(s);
  if (fbuf_len + n + 1 > fbuf_cap) {
    size_t nc = fbuf_cap ? fbuf_cap * 2 : 8192;
    while (nc < fbuf_len + n + 1)
      nc *= 2;
    char *tmp = realloc(fbuf, nc);
    if (!tmp)
      return;
    fbuf = tmp;
    fbuf_cap = nc;
  }
  memcpy(fbuf + fbuf_len, s, n);
  fbuf_len += n;
  fbuf[fbuf_len] = '\0';
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
  fbuf_len = 0;
}

/* ── raw terminal ───────────────────────────────────────────────────── */
static struct termios orig_term;
static int term_is_raw = 0;

static void term_restore(void) {
  if (!term_is_raw)
    return;
  tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
  /* показать курсор + вернуть основной буфер */
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
  tcgetattr(STDIN_FILENO, &orig_term);
  t = orig_term;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
  xwrite(ALT_ON, sizeof(ALT_ON) - 1);
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
    return (int)c;

  /* escape: ждём продолжение max 100 мс */
  fd_set fds;
  struct timeval tv;

  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  tv.tv_sec = 0;
  tv.tv_usec = 100000;
  if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0)
    return 27; /* одиночный ESC */

  unsigned char seq0;
  if (read(STDIN_FILENO, &seq0, 1) != 1)
    return 27;

  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  tv.tv_sec = 0;
  tv.tv_usec = 100000;
  if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0)
    return 27;

  unsigned char seq1;
  if (read(STDIN_FILENO, &seq1, 1) != 1)
    return 27;

  (void)seq0;
  (void)seq1;
  return 0; /* стрелка или другая escape-последовательность */
}

static int term_rows(void) {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 4)
    return (int)w.ws_row;
  return 24;
}

/* ══════════════════════════════════════════════════════════════════════
   CONTENT
   Формат строки:
     "T:текст"    — title
     "G:текст"    — group header
     "R:key|desc" — row
     "N:текст"    — note
     "B:"         — blank
   ══════════════════════════════════════════════════════════════════════ */

static const char *sec_navigation[] = {
    "T:НАВИГАЦИЯ",
    "G:Базовые движения",
    "R:h j k l|влево / вниз / вверх / вправо",
    "R:w|начало следующего слова",
    "R:b|начало предыдущего слова",
    "R:e|конец текущего слова",
    "R:W B E|то же, но через пробел (игнор знаков)",
    "R:ge|конец предыдущего слова",
    "G:Строка",
    "R:0|начало строки",
    "R:^|первый непробельный символ",
    "R:$|конец строки",
    "R:f<x>|прыжок к символу x на строке →",
    "R:F<x>|прыжок к символу x на строке ←",
    "R:t<x>|перед символом x →",
    "R:; ,|повторить f/F/t/T вперёд / назад",
    "G:Файл и экран",
    "R:gg|начало файла",
    "R:G|конец файла",
    "R::<n>|перейти на строку n",
    "R:Ctrl+d|вниз на полэкрана",
    "R:Ctrl+u|вверх на полэкрана",
    "R:Ctrl+f|вниз на экран",
    "R:Ctrl+b|вверх на экран",
    "R:zz|центрировать экран на курсоре",
    "R:H M L|начало / середина / конец экрана",
    "R:{ }|прыжок между параграфами",
    "R:%|прыжок к парной скобке",
    "N:Цифра перед движением повторяет его: 5j, 3w, 10l",
    NULL};

static const char *sec_editing[] = {
    "T:РЕДАКТИРОВАНИЕ",
    "G:Режимы вставки",
    "R:i|INSERT перед курсором",
    "R:a|INSERT после курсора",
    "R:I|INSERT в начало строки",
    "R:A|INSERT в конец строки",
    "R:o|новая строка ниже + INSERT",
    "R:O|новая строка выше + INSERT",
    "R:Esc|вернуться в NORMAL",
    "G:Операции со строками",
    "R:dd|вырезать строку",
    "R:yy|скопировать строку",
    "R:p|вставить после курсора",
    "R:P|вставить перед курсором",
    "R:cc|заменить строку (вырезать + INSERT)",
    "R:C|удалить до конца строки + INSERT",
    "R:D|удалить до конца строки",
    "R:J|склеить со следующей строкой",
    "G:Символы",
    "R:x|удалить символ под курсором",
    "R:r<x>|заменить символ на x",
    "R:~|переключить регистр символа",
    "G:Отмена / повтор",
    "R:u|отменить",
    "R:Ctrl+r|повторить отменённое",
    "R:.|повторить последнее изменение",
    "G:Операторы (оператор + движение)",
    "R:d<motion>|удалить:   dw, d$, d3j, d%",
    "R:y<motion>|скопировать: yw, y$, yG",
    "R:c<motion>|заменить:  cw, c$, ciw",
    "R:=<motion>|авто-отступ: gg=G (весь файл)",
    "G:Визуальный режим",
    "R:v|VISUAL посимвольный",
    "R:V|VISUAL LINE",
    "R:Ctrl+v|VISUAL BLOCK",
    "R:o|переключить конец выделения",
    "R:> <|сдвинуть выделение вправо / влево",
    "G:Clipboard (настройки конфига)",
    "R:<leader>y|скопировать в системный буфер обмена",
    "R:<leader>p|вставить из системного буфера обмена",
    "R:<leader>d|вырезать в системный буфер обмена",
    "N:d / x без leader удаляют в никуда (black hole) — не засоряют регистр",
    NULL};

static const char *sec_textobj[] = {
    "T:ТЕКСТОВЫЕ ОБЪЕКТЫ",
    "N:Формат: <оператор> i/a <объект>",
    "N:i = inner (внутри),  a = around (включая ограничители)",
    "B:",
    "R:iw aw|слово / слово с пробелом",
    "R:is as|предложение",
    "R:ip ap|параграф",
    "R:i\" a\"|двойные кавычки",
    "R:i' a'|одинарные кавычки",
    "R:i` a`|обратные кавычки",
    "R:i( a(|скобки ()",
    "R:i[ a[|квадратные скобки []",
    "R:i{ a{|фигурные скобки {}",
    "R:it at|HTML-тег",
    "G:Примеры",
    "N:ci\"  — заменить текст внутри кавычек",
    "N:da(  — удалить скобки вместе с содержимым",
    "N:yi{  — скопировать содержимое фигурных скобок",
    "N:vap  — выделить параграф",
    NULL};

static const char *sec_search[] = {
    "T:ПОИСК И ЗАМЕНА",
    "G:Поиск",
    "R:/текст|поиск вперёд",
    "R:?текст|поиск назад",
    "R:n|следующее совпадение",
    "R:N|предыдущее совпадение",
    "R:*|поиск слова под курсором →",
    "R:#|поиск слова под курсором ←",
    "R::noh|снять подсветку",
    "G:Замена",
    "R::s/old/new/|первое вхождение в строке",
    "R::s/old/new/g|все в строке",
    "R::%s/old/new/g|все в файле",
    "R::%s/old/new/gc|все в файле с подтверждением",
    "R::%s/old/new/gi|все в файле без учёта регистра",
    "G:Регулярные выражения (базово)",
    "R:.|любой символ",
    "R:*|0 и более предыдущего",
    "R:\\+|1 и более предыдущего",
    "R:\\?|0 или 1 предыдущего",
    "R:^|начало строки",
    "R:$|конец строки",
    "R:\\w|буква или цифра",
    "R:[abc]|один из символов",
    NULL};

static const char *sec_files[] = {
    "T:ФАЙЛЫ, БУФЕРЫ, ОКНА",
    "G:Сохранение и выход",
    "R::w|сохранить",
    "R::q|выйти",
    "R::wq  /  ZZ|сохранить и выйти",
    "R::q!|выйти без сохранения",
    "R::wa|сохранить все буферы",
    "R::qa!|выйти из всех без сохранения",
    "R:<leader>xa|сохранить все и выйти (:wa + :qa)",
    "G:Буферы",
    "R::e <файл>|открыть файл",
    "R::bn|следующий буфер",
    "R::bp|предыдущий буфер",
    "R::bd|закрыть буфер",
    "R::ls|список буферов",
    "R::b<n>|перейти к буферу n",
    "G:Окна",
    "R::sp|разделить горизонтально",
    "R::vsp|разделить вертикально",
    "R:Ctrl+w h/j/k/l|навигация между окнами",
    "R:Ctrl+w q|закрыть окно",
    "R:Ctrl+w =|выровнять размеры окон",
    "R:Ctrl+w r|поменять окна местами",
    NULL};

static const char *sec_neotree[] = {
    "T:NEO-TREE — ФАЙЛОВЫЙ ПРОВОДНИК",
    "N:Боковая панель файлов (справа, ширина 35). Открывается/закрывается "
    "через <leader>e.",
    "N:Показывает dotfiles и gitignored файлы. Автоматически следит за текущим "
    "файлом.",
    "B:",
    "G:Открытие и закрытие",
    "R:<leader>e|открыть / закрыть neo-tree (toggle)",
    "N:Панель открывается справа и не вытесняет окна редактора.",
    "N:При открытии курсор остаётся в редакторе — фокус не переходит "
    "автоматически.",
    "B:",
    "G:Навигация",
    "R:j / k|вниз / вверх по дереву",
    "R:h|свернуть узел / подняться к родителю",
    "R:l|развернуть папку или открыть файл",
    "R:Enter|открыть файл в текущем окне",
    "R:Ctrl+w l|перевести фокус в neo-tree из редактора",
    "R:Ctrl+w h|вернуть фокус в редактор из neo-tree",
    "B:",
    "G:Операции с файлами (фокус должен быть в neo-tree)",
    "R:a|создать файл (имя с / на конце — создаст папку)",
    "R:d|удалить файл или папку",
    "R:r|переименовать",
    "R:y|скопировать файл в буфер",
    "R:x|вырезать файл в буфер",
    "R:p|вставить файл из буфера",
    "R:m|переместить файл",
    "R:c|скопировать файл",
    "B:",
    "G:Отображение",
    "R:H|переключить показ скрытых файлов (toggle hidden)",
    "R:R|обновить дерево (refresh)",
    "R:?|показать встроенную помощь neo-tree",
    "B:",
    "G:Открытие файлов в разных режимах",
    "R:Enter  /  l|открыть в текущем окне",
    "R:s|открыть в вертикальном сплите",
    "R:S|открыть в горизонтальном сплите",
    "R:t|открыть в новой вкладке",
    "B:",
    "N:follow_current_file = true — neo-tree автоматически выделяет в дереве",
    "N:файл, который открыт в активном буфере.",
    "N:Фильтрация отключена: hide_dotfiles = false, hide_gitignored = false —",
    "N:все файлы видны без исключений.",
    NULL};

static const char *sec_dirwork[] = {
    "T:РАБОТА С ПАПКОЙ / ПРОЕКТОМ",
    "G:Встроенный проводник (netrw)",
    "R::Ex  /  :Explore|открыть netrw в текущей директории",
    "R::Sex|netrw в горизонтальном сплите",
    "R::Vex|netrw в вертикальном сплите",
    "R:Enter|открыть файл или папку",
    "R:-|подняться на уровень вверх",
    "R:d|создать папку",
    "R:%|создать файл",
    "R:R|переименовать",
    "R:D|удалить",
    "B:",
    "G:oil.nvim — файловая система как буфер",
    "R:-|открыть oil для папки текущего файла",
    "R::Oil|открыть oil в cwd",
    "R::Oil <путь>|открыть oil в указанной папке",
    "R:Enter|открыть файл / войти в папку",
    "R:Backspace  /  h|подняться на уровень вверх",
    "R:_|перейти в cwd из oil",
    "R:a или o|создать файл (имя/ — папка)",
    "R:d или D|пометить на удаление",
    "R:r|переименовать (прямо в буфере)",
    "R:yy + p|скопировать / переместить файл",
    "R::w  /  Ctrl+s|применить все изменения",
    "R:g?|помощь внутри oil",
    "R:gx|открыть файл во внешней программе",
    "N:Переименование = просто отредактируй имя в буфере и :w",
    "N:Перемещение = вырежи строку из одной папки, вставь в другую, :w",
    "B:",
    "G:Навигация по проекту (Telescope)",
    "R:<leader>ff|найти файл по имени в проекте",
    "R:<leader>fg|live_grep — поиск по содержимому всех файлов",
    "R:<leader>fb|список открытых буферов",
    "R:<leader>fr|oldfiles — недавние файлы",
    "N:live_grep использует ripgrep — поиск по всему проекту мгновенно",
    "N:Внутри Telescope: Ctrl+j/k — навигация, Ctrl+v — вертикальный сплит",
    "B:",
    "G:grug-far.nvim — поиск и замена по проекту",
    "R:<leader>sr|открыть grug-far (слово под курсором)",
    "R::GrugFar|открыть вручную",
    "N:Поддерживает ripgrep-флаги: --type lua, --glob '*.ts', -F (literal)",
    "N:Изменения показываются как diff; применяются по подтверждению",
    "B:",
    "G:Рабочая директория",
    "R::cd <путь>|сменить глобальную cwd",
    "R::lcd <путь>|cwd только для текущего окна",
    "R::tcd <путь>|cwd только для текущей вкладки",
    "R::pwd|показать текущую директорию",
    "N:После :cd все команды (:e, Telescope, oil) работают от нового пути",
    "B:",
    "G:Массовые операции по файлам",
    "R::args *.ts|загрузить файлы в список аргументов",
    "R::argdo %s/old/new/g|замена во всех файлах списка",
    "R::argdo w|сохранить все файлы списка",
    "R::bufdo %s/old/new/g|то же по всем открытым буферам",
    "R::bufdo w|сохранить все буферы",
    "N:cfdo / lfdo — то же по quickfix / loclist (удобно после live_grep)",
    NULL};

static const char *sec_plugins[] = {
    "T:ПЛАГИНЫ",
    "G:leap.nvim — прыжки по экрану",
    "R:s|прыжок вперёд (введи 2 символа → метка)",
    "R:S|прыжок назад",
    "R:gs|прыжок в другое окно",
    "N:Работает как оператор: ds<метка>, ys<метка>\" — удалить/обернуть до "
    "точки",
    "B:",
    "G:vim-surround — обёртки",
    "R:cs\"'|заменить \" на '",
    "R:cs({|заменить ( на {",
    "R:ds\"|удалить кавычки",
    "R:ds(|удалить скобки",
    "R:ysiw\"|обернуть слово в \"\"",
    "R:ysiw(|обернуть слово в ( )",
    "R:yss\"|обернуть всю строку",
    "R:S\" (VISUAL)|обернуть выделение",
    "B:",
    "G:vim-commentary — комментарии",
    "R:gcc|закомментировать / раскомментировать строку",
    "R:gc<motion>|комментировать по движению",
    "R:gcap|комментировать параграф",
    "R:gc (VISUAL)|комментировать выделение",
    "N:Примеры: gc3j, gcG, gcip",
    "B:",
    "G:targets.vim — расширенные объекты",
    "R:cin,|change inside next запятую",
    "R:da,|удалить аргумент с запятой",
    "R:cin)|change inside next скобки",
    "R:dil\"|delete inside last кавычки",
    "R:I / A|точный inner / outer (без лишних пробелов)",
    "N:n = next, l = last — работает без нахождения внутри объекта",
    "N:Разделители: , . ; : + - = ~ _ * # / | \\ & $",
    NULL};

static const char *sec_telescope[] = {
    "T:TELESCOPE / FUGITIVE / MASON / NOICE",
    "G:Telescope",
    "R:<leader>ff|find_files",
    "R:<leader>fg|live_grep (поиск по содержимому)",
    "R:<leader>fb|buffers",
    "R:<leader>fh|help_tags",
    "R:<leader>fr|oldfiles (недавние файлы)",
    "R:<leader>fs|lsp_document_symbols",
    "R:<leader>fd|diagnostics по всему проекту (telescope)",
    "N:Внутри: Ctrl+j/k навигация, Enter открыть, Ctrl+v вертикальный сплит",
    "B:",
    "G:vim-fugitive — Git",
    "R::G|статус (git status)",
    "R::G add %|добавить текущий файл",
    "R::G commit|коммит",
    "R::G push/pull|push / pull",
    "R::Gdiff|diff текущего файла",
    "R::Gblame|blame по строкам",
    "N:Внутри :G — s stage, u unstage, = diff, cc commit, Enter открыть файл",
    "B:",
    "G:Mason",
    "R::Mason|открыть UI менеджера",
    "R::MasonInstall|установить пакет вручную",
    "R::MasonUpdate|обновить всё",
    "N:Установлены: lua_ls, pyright, ts_ls, prettier, stylua, black, eslint_d, "
    "ruff",
    "B:",
    "G:conform.nvim — форматирование",
    "N:Форматирование при сохранении — автоматически",
    "N:prettier: js/ts/jsx/tsx/json/html/css  |  stylua: lua  |  black: python",
    "B:",
    "G:noice.nvim",
    "R::Noice|история всех сообщений",
    "R::Noice dismiss|скрыть уведомление",
    "R:K|LSP hover с рамкой",
    "R:Ctrl+k (INSERT)|signature help",
    "B:",
    "G:which-key.nvim",
    "N:Автоматически показывает подсказки после <leader> или любого префикса",
    "N:Настраивать не нужно — работает сам",
    NULL};

static const char *sec_ide[] = {
    "T:IDE-ФУНКЦИИ (LSP / ДИАГНОСТИКА / АВТОДОПОЛНЕНИЕ)",
    "G:LSP — навигация по коду",
    "R:gd|перейти к определению",
    "R:gD|перейти к объявлению",
    "R:gi|перейти к реализации",
    "R:gr|все ссылки на символ",
    "R:K|документация (hover)",
    "N:Keymaps активны только в буферах с подключённым LSP-сервером",
    "B:",
    "G:LSP — рефакторинг и действия",
    "R:<leader>rn|переименовать символ (rename)",
    "R:<leader>ca|code actions (авто-импорт, fix, рефакторинг)",
    "N:code actions зависят от сервера: ts_ls предлагает импорты, extract "
    "function и т.д.",
    "B:",
    "G:Диагностика — текущий файл",
    "R:<leader>de|показать ошибку под курсором (float)",
    "R:[d|предыдущая диагностика",
    "R:]d|следующая диагностика",
    "N:Ошибки отображаются inline (virtual text) и в gutter (иконки)",
    "N:Иконки:  = error,  = warn,  = info,  = hint",
    "B:",
    "G:trouble.nvim — диагностика по всему проекту",
    "R:<leader>xx|все ошибки проекта (все файлы)",
    "R:<leader>xb|ошибки только текущего буфера",
    "R:<leader>xs|символы файла (структура)",
    "R:<leader>xr|все ссылки на символ под курсором",
    "N:Внутри trouble: j/k навигация, Enter — перейти к месту ошибки",
    "N:Обновляется автоматически при изменении файлов",
    "B:",
    "G:nvim-cmp — автодополнение",
    "R:Ctrl+Space|принудительно открыть меню",
    "R:Tab|выбрать следующий вариант / развернуть сниппет",
    "R:Shift+Tab|выбрать предыдущий вариант",
    "R:Enter|подтвердить выбор",
    "R:Ctrl+e|закрыть меню",
    "R:Ctrl+u / Ctrl+d|прокрутить документацию в popup",
    "N:Источники: LSP (приоритет) → Codeium [AI] → сниппеты → пути → буфер",
    "N:friendly-snippets подключены автоматически через LuaSnip",
    "B:",
    "G:nvim-lint — линтинг (независимо от LSP)",
    "N:Запускается автоматически: при сохранении, открытии, выходе из INSERT",
    "N:eslint_d: js / ts / jsx / tsx  |  ruff: python",
    "N:Результаты попадают в общую диагностику — видны в trouble и gutter",
    "N:eslint_d — демон, запускается один раз и остаётся в памяти (быстро)",
    "B:",
    "G:LSP-серверы (mason)",
    "R:ts_ls|TypeScript / JavaScript (Microsoft)",
    "R:pyright|Python (Microsoft)",
    "R:lua_ls|Lua (сфокусирован на Neovim API)",
    "R:emmet_language_server|HTML/CSS сокращения (Emmet)",
    "R:marksman|Markdown (go-to, references)",
    "N:Все серверы устанавливаются автоматически при первом запуске",
    "N:capabilities переданы cmp_nvim_lsp — LSP отдаёт полные данные для "
    "дополнения",
    "B:",
    "G:Типичный рабочий цикл",
    "N:1. Открыть файл — LSP подключается автоматически",
    "N:2. Ошибки сразу видны в gutter и inline",
    "N:3. <leader>xx — обзор всех проблем проекта",
    "N:4. gd / gr / K — навигация и документация",
    "N:5. <leader>ca — исправить / импортировать",
    "N:6. <leader>rn — переименовать символ везде",
    "N:7. :w — форматирование + линтинг автоматически",
    NULL};

static const char *sec_git[] = {
    "T:УПРАВЛЕНИЕ GIT (NEOGIT + DIFFVIEW)",
    "G:Открытие",
    "R:<leader>gg|открыть Neogit (статус репозитория)",
    "R::Neogit|открыть вручную",
    "R::Neogit commit|сразу открыть буфер коммита",
    "R::Neogit log|открыть лог",
    "N:В главном окне видны секции: Untracked, Unstaged, Staged, Recent "
    "commits",
    "B:",
    "G:Навигация по секциям и файлам",
    "R:Tab|раскрыть / свернуть секцию или файл (показать hunks)",
    "R:j / k|перемещение по строкам",
    "R:{  }|прыжок между секциями",
    "R:Enter|открыть файл под курсором в редакторе",
    "R:q|закрыть Neogit",
    "R:?|показать все доступные клавиши в текущем контексте",
    "B:",
    "G:Staging — постановка изменений",
    "R:s|stage файла, hunk или выделения (VISUAL)",
    "R:u|unstage файла, hunk или выделения",
    "R:S|stage all — все изменения сразу",
    "R:U|unstage all",
    "R:x|discard — откатить изменения в файле или hunk",
    "N:Раскрой файл через Tab — увидишь отдельные hunks, можно stage каждый",
    "N:В VISUAL режиме выдели нужные строки и нажми s — stage по строкам",
    "B:",
    "G:Коммиты",
    "R:cc|обычный коммит (открывает буфер сообщения)",
    "R:ca|amend — изменить последний коммит",
    "R:ce|amend без редактирования сообщения (extend)",
    "R:cr|reword — изменить только сообщение последнего коммита",
    "R:cf|fixup — коммит с флагом --fixup",
    "R:cs|squash — коммит с флагом --squash",
    "N:В буфере коммита: :wq или ZZ — сохранить и выполнить коммит",
    "N::q! — отменить коммит",
    "N:Первая строка — subject (до 72 символов), затем пустая строка, затем "
    "body",
    "B:",
    "G:Push / Pull / Fetch",
    "R:Pp|push в upstream (origin/текущая ветка)",
    "R:Po|push с выбором remote через Telescope",
    "R:Pf|push --force-with-lease (безопасный force push)",
    "R:Fl|fetch все remote",
    "R:Fu|fetch upstream",
    "R:Fp|pull (fetch + merge/rebase по настройке)",
    "N:p — нижний регистр = pull, P — верхний = push, F = fetch",
    "B:",
    "G:Ветки",
    "R:b|открыть меню веток",
    "R:bb|checkout существующей ветки (через Telescope)",
    "R:bc|создать новую ветку",
    "R:bn|создать ветку и сразу перейти на неё",
    "R:bd|удалить ветку",
    "R:bm|merge выбранной ветки в текущую",
    "R:br|rebase текущей ветки на выбранную",
    "B:",
    "G:Rebase",
    "R:r|открыть меню rebase",
    "R:ri|interactive rebase (--interactive)",
    "R:ru|rebase на upstream",
    "R:ra|abort — отменить текущий rebase",
    "R:rc|continue — продолжить после разрешения конфликтов",
    "R:rs|skip — пропустить проблемный коммит",
    "N:В интерактивном rebase: p pick, r reword, e edit, s squash, f fixup, d "
    "drop",
    "N:Переставляй строки коммитов прямо в буфере — это обычный vim",
    "B:",
    "G:Stash",
    "R:Z|открыть меню stash",
    "R:Zz|stash всех изменений (с сообщением)",
    "R:Zi|stash index — только staged изменения",
    "R:Zp|stash pop — применить и удалить последний stash",
    "R:Za|stash apply — применить без удаления",
    "R:Zd|stash drop — удалить stash",
    "B:",
    "G:Лог и история",
    "R:ll|открыть лог текущей ветки",
    "R:lL|открыть лог всех веток",
    "R:Enter (в логе)|раскрыть diff коммита",
    "R:Tab (в логе)|раскрыть / свернуть коммит inline",
    "N:Из лога можно нажать Enter на коммите — откроется diffview для него",
    "B:",
    "G:Разрешение конфликтов",
    "N:Конфликтующие файлы видны в секции Unmerged в статусе",
    "R:Enter|открыть файл с конфликтом в редакторе",
    "N:Далее используй diffview.nvim — он специально создан для merge "
    "conflicts",
    "N:После разрешения: s (stage файл) → cc (коммит)",
    "B:",
    "G:Интеграция с diffview",
    "R:d (на файле)|открыть diff этого файла в diffview",
    "R:D (на коммите)|открыть diff коммита в diffview",
    "N:diffview открывается поверх Neogit и закрывается отдельно",
    "N:Все операции копирования работают в diffview как в обычном буфере",
    "B:",
    "G:Diffview — открытие",
    "R:<leader>gd|diff рабочего дерева (все изменённые файлы)",
    "R:<leader>gh|история текущего файла (git log -p %)",
    "R:<leader>gH|история всего репозитория",
    "R:<leader>gq|закрыть diffview",
    "R::DiffviewOpen|открыть diff рабочего дерева",
    "R::DiffviewOpen HEAD~3|diff последних 3 коммитов",
    "R::DiffviewOpen abc123|diff конкретного коммита",
    "R::DiffviewOpen main...feat|diff между ветками",
    "R::DiffviewFileHistory %|история текущего файла",
    "R::DiffviewFileHistory|история всего репо",
    "R::DiffviewFileHistory % -n 20|последние 20 коммитов файла",
    "R::DiffviewClose|закрыть",
    "N:Поддерживает любой git revision: HEAD, HEAD~N, тег, хеш коммита, branch",
    "B:",
    "G:Структура интерфейса",
    "N:Слева — file panel (список файлов), справа — diff панели (old | new)",
    "N:В file history: слева список коммитов, справа diff выбранного коммита",
    "N:Переключение между панелями — Ctrl+w h/l (стандартный vim)",
    "B:",
    "G:File Panel — панель файлов (левая)",
    "R:Tab|переключить фокус: file panel ↔ diff",
    "R:j / k|навигация по файлам",
    "R:Enter|выбрать файл — обновить diff справа",
    "R:o|открыть файл в новом окне",
    "R:s|stage файла (в режиме рабочего дерева)",
    "R:u|unstage файла",
    "R:X|discard изменений в файле",
    "R:R|обновить (refresh) список файлов",
    "R:i|переключить режим листинга (list / tree)",
    "R:f|flip layout — поменять расположение панелей",
    "R:q|закрыть diffview",
    "R:g?|встроенная помощь diffview",
    "B:",
    "G:Diff-буфер — навигация по изменениям",
    "R:[c|предыдущий hunk (изменение)",
    "R:]c|следующий hunk",
    "R:[x|предыдущий конфликт",
    "R:]x|следующий конфликт",
    "N:В diff-буфере работают все стандартные vim движения и операции",
    "N:yy, y$, v+y — копирование текста из diff без ограничений",
    "B:",
    "G:История файла — File History",
    "R:j / k|навигация по коммитам",
    "R:Enter|показать diff выбранного коммита справа",
    "R:y|скопировать хеш коммита",
    "R:L|показать полный лог коммита (сообщение + метаданные)",
    "R:zR|раскрыть все коммиты",
    "R:zM|свернуть все коммиты",
    "B:",
    "G:Разрешение конфликтов (merge conflicts)",
    "R::DiffviewOpen|в состоянии конфликта показывает три панели",
    "N:Три панели: OURS (слева) | RESULT (центр) | THEIRS (справа)",
    "N:Редактируй RESULT напрямую — это обычный буфер, все vim операции "
    "работают",
    "R:<leader>co|принять OURS для hunk под курсором",
    "R:<leader>ct|принять THEIRS для hunk под курсором",
    "R:<leader>cb|принять оба варианта (both)",
    "R:<leader>cO|принять OURS для всего файла",
    "R:<leader>cT|принять THEIRS для всего файла",
    "R:<leader>cB|принять оба для всего файла",
    "R:<leader>cx|удалить hunk (не принимать ничего)",
    "R:[x / ]x|навигация между конфликтами",
    "N:После разрешения всех конфликтов: stage файл через Neogit и коммит",
    "B:",
    "G:Типичный рабочий процесс",
    "N:--- Ревью изменений перед коммитом ---",
    "N:1. <leader>gd — открыть diff рабочего дерева",
    "N:2. j/k по файлам, Enter — смотреть diff каждого",
    "N:3. [c / ]c — прыгать между hunks",
    "N:4. yy / v+y — копировать нужные части",
    "N:5. s — stage нужные файлы прямо из diffview",
    "N:6. <leader>gq — закрыть, открыть <leader>gg — коммит",
    "B:",
    "N:--- Изучение истории ---",
    "N:1. <leader>gh — история текущего файла",
    "N:2. j/k по коммитам, Enter — видеть что изменилось",
    "N:3. y — скопировать хеш если нужен",
    "N:4. :DiffviewOpen abc123 — открыть конкретный коммит",
    "B:",
    "N:--- Разрешение конфликтов ---",
    "N:1. После git merge/rebase с конфликтами: <leader>gd",
    "N:2. Файлы с конфликтами будут сверху списка",
    "N:3. Три-панельный вид: OURS | RESULT | THEIRS",
    "N:4. <leader>co / ct для каждого hunk или вручную в RESULT",
    "N:5. [x / ]x — прыгать между конфликтами",
    "N:6. После разрешения: <leader>gq → <leader>gg → s → cc",
    NULL};

static const char *sec_ui[] = {
    "T:UI-ПЛАГИНЫ (BUFFERLINE / DASHBOARD / WINBAR / ОТСТУПЫ)",
    "G:bufferline.nvim — вкладки файлов",
    "N:Вкладки отображаются вверху, как в VSCode. Иконки LSP-диагностики на "
    "вкладке.",
    "R:<S-l>|перейти к следующему буферу",
    "R:<S-h>|перейти к предыдущему буферу",
    "R:<leader>1..9|прыжок к буферу по номеру",
    "R:<leader>bd|закрыть текущий буфер",
    "R:<leader>bp|закрепить (pin) буфер — не закрывается при bd",
    "N:Закреплённый буфер отображается иначе и остаётся при закрытии остальных",
    "N:separator_style = slant — скошенные вкладки; можно сменить на "
    "thin/slope",
    "B:",
    "G:alpha-nvim — стартовый экран",
    "N:Открывается автоматически при запуске nvim без аргументов.",
    "N:Доступен из любого места через <leader>a.",
    "R:<leader>a|открыть alpha dashboard",
    "R:f|Find File — Telescope find_files",
    "R:r|Recent — Telescope oldfiles",
    "R:g|Grep — Telescope live_grep",
    "R:s|Restore Session — восстановить сессию через persistence.nvim",
    "R:c|Config — открыть init.lua",
    "R:p|Plugins — открыть Lazy",
    "R:m|Mason — открыть Mason UI",
    "R:q|Quit — выйти из nvim",
    "N:Автоматически переоткрывается при закрытии последнего буфера",
    "B:",
    "G:persistence.nvim — управление сессиями",
    "R:<leader>qs|восстановить сессию для текущей директории",
    "R:<leader>ql|восстановить последнюю сессию",
    "R:<leader>qd|не сохранять сессию при выходе",
    "N:Сессии сохраняются автоматически при выходе из nvim",
    "N:Хранятся в stdpath('state')/sessions/ по имени директории",
    "B:",
    "G:render-markdown.nvim — рендер Markdown в буфере",
    "N:Активен автоматически для файлов *.md — ничего запускать не нужно.",
    "N:Заголовки (#, ##, ###) отображаются крупнее и с цветом",
    "N:Кодблоки ``` получают рамку и фоновый цвет (style = full)",
    "N:Списки — иконки вместо дефисов",
    "N:Чекбоксы [ ] и [x] рендерятся как иконки □ / ☑",
    "N:Таблицы выравниваются автоматически",
    "R::RenderMarkdown enable|включить рендер вручную",
    "R::RenderMarkdown disable|отключить рендер (вернуть plain text)",
    "R::RenderMarkdown toggle|переключить",
    "N:marksman LSP: gd/gr/K работают в markdown — go-to заголовку, references",
    "B:",
    "G:barbecue.nvim + nvim-navic — winbar (хлебные крошки)",
    "N:Строка сверху каждого окна показывает путь к символу под курсором.",
    "N:Пример: Component > handleClick > useState",
    "N:Обновляется при движении курсора через LSP — требует подключённого "
    "сервера",
    "N:Отключён для neo-tree, toggleterm, alpha — не мешает",
    "N:Иконки соответствуют типу символа: функция, класс, переменная и т.д.",
    "B:",
    "G:indent-blankline.nvim — вертикальные линии отступов",
    "N:Все линии отступов — приглушённый серый цвет (#313244)",
    "N:Линия ТЕКУЩЕГО блока (ближайшего к курсору) — синяя (#89b4fa)",
    "N:Горизонтальные черты в начале и конце активного блока — его границы",
    "N:Блок определяется через treesitter: функция, if, for, class, объект и "
    "т.д.",
    "N:При движении курсора синяя линия переключается на ближайший "
    "родительский блок",
    "N:Отключён для: alpha, neo-tree, toggleterm, help, lazy, mason",
    NULL};

static const char *sec_tools[] = {
    "T:ТЕРМИНАЛ И АВТОДОПОЛНЕНИЕ (TOGGLETERM / CODEIUM / AUTOPAIRS / EMMET)",
    "G:toggleterm.nvim — встроенный терминал",
    "N:Терминал открывается прямо в nvim, не нужно переключать вкладки "
    "терминала.",
    "R:Ctrl+`|открыть / закрыть горизонтальный терминал (как в VSCode)",
    "R:<leader>tf|открыть float-терминал (плавающее окно)",
    "R:<leader>th|открыть горизонтальный терминал",
    "R:<leader>tv|открыть вертикальный терминал (ширина 60)",
    "N:Повторное нажатие того же сочетания скрывает / показывает терминал",
    "N:persist_mode = true — режим терминала сохраняется между открытиями",
    "B:",
    "G:Навигация в терминале",
    "N:По умолчанию в терминале — terminal mode (INSERT-подобный для shell)",
    "R:Esc|выйти из terminal mode в NORMAL (можно копировать текст)",
    "R:Ctrl+h / l / j / k|переключиться в соседнее окно не выходя из терминала",
    "N:В NORMAL mode внутри терминала работают все vim движения и копирование",
    "N:Для возврата в shell нажми i или a",
    "B:",
    "G:Несколько терминалов",
    "N:Каждый toggleterm — отдельный буфер с номером.",
    "R::ToggleTerm 1|открыть/показать терминал №1",
    "R::ToggleTerm 2|открыть/показать терминал №2",
    "N:Все открытые терминалы независимы — разные shell-сессии",
    "B:",
    "G:Codeium — AI автодополнение",
    "N:Бесплатный AI inline ghost-text. При первом запуске: :Codeium Auth",
    "N:Предложения появляются как серый ghost-text справа от курсора",
    "N:Работает параллельно с nvim-cmp — не конфликтует",
    "R:Ctrl+l|принять предложение целиком",
    "R:Ctrl+j|принять предложение по одному слову",
    "R:Ctrl+]|отклонить предложение",
    "N:В меню cmp Codeium виден как источник [AI]",
    "R::Codeium Auth|авторизация (один раз)",
    "R::Codeium Enable|включить для текущего буфера",
    "R::Codeium Disable|отключить для текущего буфера",
    "R::Codeium Toggle|переключить",
    "B:",
    "G:nvim-autopairs — умное автозакрытие скобок",
    "N:Заменяет ручные биндинги (, [, {, \", ' — теперь через "
    "treesitter-контекст.",
    "N:Внутри строки кавычки не дублируются — autopairs это понимает",
    "N:В конце слова ) не добавляется лишняя скобка",
    "N:После Enter внутри {} — автоматический отступ и позиция курсора",
    "N:Интеграция с cmp: при confirm функции скобка добавляется автоматически",
    "N:Для принудительного ввода без пары — нажми символ дважды быстро",
    "B:",
    "G:Emmet (emmet-language-server) — HTML/CSS сокращения",
    "N:Работает через LSP как источник автодополнения. Активен в "
    "html/css/jsx/tsx.",
    "N:Введи сокращение → Tab (через cmp) или Enter — развернётся в полный код",
    "B:",
    "N:Основные сокращения:",
    "R:!|полный HTML5 шаблон (<!DOCTYPE html>...)",
    "R:div|<div></div>",
    "R:div.class|<div class=\"class\"></div>",
    "R:div#id|<div id=\"id\"></div>",
    "R:ul>li*3|<ul> с тремя <li>",
    "R:div>p+span|div содержащий p и span",
    "R:div.wrap>ul>li.item*5|вложенная структура",
    "R:a[href=#]|<a href=\"#\"></a>",
    "R:input:text|<input type=\"text\">",
    "R:input:email|<input type=\"email\">",
    "R:input:checkbox|<input type=\"checkbox\">",
    "R:btn или button|<button></button>",
    "R:img[src=][alt=]|<img src=\"\" alt=\"\">",
    "R:link:css|<link rel=\"stylesheet\" href=\"style.css\">",
    "R:script:src|<script src=\"\"></script>",
    "B:",
    "N:CSS сокращения (в .css / style атрибутах):",
    "R:m10|margin: 10px",
    "R:p10-20|padding: 10px 20px",
    "R:df|display: flex",
    "R:fz16|font-size: 16px",
    "R:bg#fff|background: #fff",
    "R:w100p|width: 100%",
    "R:h100vh|height: 100vh",
    "R:pos-r|position: relative",
    "R:pos-a|position: absolute",
    "R:jc-c|justify-content: center",
    "R:ai-c|align-items: center",
    "B:",
    "N:Множители и нумерация:",
    "R:li*5|5 элементов li",
    "R:li.item$*3|li.item1, li.item2, li.item3 ($ = номер)",
    "R:li{текст $}*3|li с содержимым 'текст 1', 'текст 2', 'текст 3'",
    "B:",
    "N:В JSX: работают те же сокращения, className вместо class генерируется "
    "автоматически",
    NULL};

/* ══════════════════════════════════════════════════════════════════════
   DYNAMIC FLAT LINE BUFFER
   ══════════════════════════════════════════════════════════════════════ */

typedef struct {
  char *text; /* heap-allocated */
} FlatLine;

static FlatLine *flat = NULL;
static int flat_total = 0;
static int flat_cap = 0;

static void flat_free(void) {
  for (int i = 0; i < flat_total; i++) {
    free(flat[i].text);
    flat[i].text = NULL;
  }
  flat_total = 0;
}

static void flat_add(const char *s) {
  if (flat_total >= flat_cap) {
    int nc = flat_cap ? flat_cap * 2 : 128;
    FlatLine *tmp = realloc(flat, (size_t)nc * sizeof(FlatLine));
    if (!tmp)
      return;
    flat = tmp;
    flat_cap = nc;
  }
  flat[flat_total].text = strdup(s);
  flat_total++;
}

static void flat_build(const char **sec) {
  flat_free();

  char buf[512];
  for (int i = 0; sec[i]; i++) {
    const char *line = sec[i];
    char type = line[0];
    const char *content = line + 2;

    switch (type) {
    case 'T':
      flat_add(
          C_SEP
          "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" RESET);
      snprintf(buf, sizeof(buf), C_TITLE BOLD "  %s" RESET, content);
      flat_add(buf);
      flat_add(
          C_SEP
          "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" RESET);
      break;

    case 'G':
      flat_add("");
      snprintf(buf, sizeof(buf), C_HEAD BOLD "  ## %s" RESET, content);
      flat_add(buf);
      break;

    case 'R': {
      char key[64], desc[256];
      const char *pipe = strchr(content, '|');
      if (pipe) {
        int klen = (int)(pipe - content);
        if (klen >= (int)sizeof(key))
          klen = (int)sizeof(key) - 1;
        memcpy(key, content, (size_t)klen);
        key[klen] = '\0';
        snprintf(desc, sizeof(desc), "%s", pipe + 1);
      } else {
        snprintf(key, sizeof(key), "%s", content);
        desc[0] = '\0';
      }
      snprintf(buf, sizeof(buf),
               "  " C_KEY BOLD "%-18s" RESET C_DESC "  %s" RESET, key, desc);
      flat_add(buf);
      break;
    }

    case 'N':
      snprintf(buf, sizeof(buf), C_HINT DIM "  > %s" RESET, content);
      flat_add(buf);
      break;

    case 'B':
      flat_add("");
      break;

    default:
      snprintf(buf, sizeof(buf), "  %s", line);
      flat_add(buf);
    }
  }
}

/* ══════════════════════════════════════════════════════════════════════
   SECTION VIEWER
   ══════════════════════════════════════════════════════════════════════ */

static void view_section(const char **sec) {
  flat_build(sec);

  int total = flat_total;
  int rows = term_rows();
  int visible = rows - 3;
  int cursor = 0;
  int offset = 0;
  int last_g = 0;

  fb_append(CUR_HIDE);
  fb_flush();

  while (1) {
    if (cursor < 0)
      cursor = 0;
    if (cursor >= total)
      cursor = total - 1;
    if (cursor < offset)
      offset = cursor;
    if (cursor >= offset + visible)
      offset = cursor - visible + 1;
    if (offset < 0)
      offset = 0;

    fb_reset();
    fb_append(CLR);

    for (int i = offset; i < offset + visible && i < total; i++) {
      if (i == cursor)
        fb_appendf(C_CUR "%s" RESET "\n", flat[i].text);
      else
        fb_appendf("%s\n", flat[i].text);
    }

    fb_append(
        C_SEP
        "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
    fb_appendf(C_HINT
               "  j/k↕  gg начало  G конец  %% край↔край  x/h выход" C_SEP
               "  [%d/%d]\n" RESET,
               cursor + 1, total);
    fb_flush();

    int key = read_key();

    if (key == 'j') {
      if (cursor < total - 1)
        cursor++;
      last_g = 0;
    } else if (key == 'k') {
      if (cursor > 0)
        cursor--;
      last_g = 0;
    } else if (key == 'g') {
      if (last_g) {
        cursor = 0;
        offset = 0;
        last_g = 0;
      } else
        last_g = 1;
    } else if (key == 'G') {
      cursor = total - 1;
      last_g = 0;
    } else if (key == '%') {
      cursor = (cursor < total / 2) ? total - 1 : 0;
      last_g = 0;
    } else if (key == 'x' || key == 'h' || key == 'q' || key == 27) {
      break;
    } else {
      last_g = 0;
    }
  }

  fb_append(CUR_SHOW);
  fb_flush();
}

/* ══════════════════════════════════════════════════════════════════════
   MENU
   ══════════════════════════════════════════════════════════════════════ */

#define MENU_N 13

static const char *menu_labels[MENU_N] = {
    "Навигация",
    "Редактирование",
    "Текстовые объекты",
    "Поиск и замена",
    "Файлы, буферы, окна",
    "Neo-tree  (файловый проводник)",
    "Работа с папкой / проектом  (oil / grug-far / telescope)",
    "Плагины  (leap / surround / commentary / targets)",
    "IDE-функции  (LSP / диагностика / автодополнение / линтинг)",
    "Telescope / Fugitive / Mason / Noice",
    "Управление Git  (Neogit + Diffview)",
    "UI-плагины  (bufferline / dashboard / winbar / отступы)",
    "Терминал и дополнение  (toggleterm / codeium / autopairs / emmet)",
};

static const char **menu_sections[MENU_N] = {
    sec_navigation, sec_editing, sec_textobj, sec_search, sec_files,
    sec_neotree,    sec_dirwork, sec_plugins, sec_ide,    sec_telescope,
    sec_git,        sec_ui,      sec_tools,
};

static void print_menu(int cur) {
  fb_reset();
  fb_append(CLR);
  fb_append(C_TITLE BOLD "\n"
                         "  ███╗   ██╗██╗   ██╗██╗███╗   ███╗████████╗██╗   "
                         "██╗████████╗ ██████╗ ██████╗ \n"
                         "  ████╗  ██║██║   ██║██║████╗ ████║╚══██╔══╝██║   "
                         "██║╚══██╔══╝██╔═══██╗██╔══██╗\n"
                         "  ██╔██╗ ██║██║   ██║██║██╔████╔██║   ██║   ██║   "
                         "██║   ██║   ██║   ██║██████╔╝\n"
                         "  ██║╚██╗██║╚██╗ ██╔╝██║██║╚██╔╝██║   ██║   ██║   "
                         "██║   ██║   ██║   ██║██╔══██╗\n"
                         "  ██║ ╚████║ ╚████╔╝ ██║██║ ╚═╝ ██║   ██║   "
                         "╚██████╔╝   ██║   ╚██████╔╝██║  ██║\n"
                         "  ╚═╝  ╚═══╝  ╚═══╝  ╚═╝╚═╝     ╚═╝   ╚═╝    ╚═════╝ "
                         "   ╚═╝    ╚═════╝ ╚═╝  ╚═╝\n" RESET);
  fb_append(C_SEP
            "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);

  for (int i = 0; i < MENU_N; i++) {
    if (i == cur)
      fb_appendf(C_CUR BOLD "  ▶  %s" RESET "\n", menu_labels[i]);
    else
      fb_appendf(C_KEY "  [%d]" C_DESC "  %s\n" RESET, i + 1, menu_labels[i]);
  }

  fb_append(C_SEP
            "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
  fb_append(C_HINT
            "  j/k выбор   l/Enter открыть   % край↔край   q выход\n" RESET);
  fb_flush();
}

int main(void) {
  term_raw();
  atexit(term_restore);
  fb_append(CUR_HIDE);
  fb_flush();

  int cur = 0;
  int last_g = 0;

  while (1) {
    print_menu(cur);
    int key = read_key();

    if (key == 'j') {
      if (cur < MENU_N - 1)
        cur++;
      last_g = 0;
    } else if (key == 'k') {
      if (cur > 0)
        cur--;
      last_g = 0;
    } else if (key == 'g') {
      if (last_g) {
        cur = 0;
        last_g = 0;
      } else
        last_g = 1;
    } else if (key == 'G') {
      cur = MENU_N - 1;
      last_g = 0;
    } else if (key == '%') {
      cur = (cur == 0) ? MENU_N - 1 : 0;
      last_g = 0;
    } else if (key == 'l' || key == '\r' || key == '\n') {
      view_section(menu_sections[cur]);
      last_g = 0;
    } else if (key >= '1' && key <= '0' + MENU_N) {
      cur = key - '1';
      view_section(menu_sections[cur]);
      last_g = 0;
    } else if (key == 'q' || key == 'x') {
      break;
    } else {
      last_g = 0;
    }
  }

  flat_free();
  free(flat);
  free(fbuf);

  fb_reset();
  fb_append(CUR_SHOW CLR);
  fb_appendf(C_HINT "\n  bye\n\n" RESET);
  fb_flush();
  return 0;
}
