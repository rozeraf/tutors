#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

/* ── ANSI ───────────────────────────────────────────────────────────── */
#define RESET    "\033[0m"
#define BOLD     "\033[1m"
#define DIM      "\033[2m"
#define CUR_HIDE "\033[?25l"
#define CUR_SHOW "\033[?25h"
#define CLR      "\033[2J\033[H"

#define C_TITLE  "\033[38;5;111m"
#define C_KEY    "\033[38;5;183m"
#define C_DESC   "\033[38;5;252m"
#define C_HEAD   "\033[38;5;150m"
#define C_SEP    "\033[38;5;240m"
#define C_HINT   "\033[38;5;109m"
#define C_CUR    "\033[48;5;237m\033[38;5;255m"

/* ── raw terminal ───────────────────────────────────────────────────── */
static struct termios orig_term;

static void term_raw(void) {
    struct termios t;
    tcgetattr(STDIN_FILENO, &orig_term);
    t = orig_term;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN]  = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static void term_restore(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
    printf(CUR_SHOW);
}

static int read_key(void) {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;
    if (c == 27) {
        unsigned char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return 27;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return 27;
        return 0;
    }
    return (int)c;
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
    NULL
};

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
    NULL
};

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
    NULL
};

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
    NULL
};

static const char *sec_files[] = {
    "T:ФАЙЛЫ, БУФЕРЫ, ОКНА",
    "G:Сохранение и выход",
    "R::w|сохранить",
    "R::q|выйти",
    "R::wq  /  ZZ|сохранить и выйти",
    "R::q!|выйти без сохранения",
    "R::wa|сохранить все буферы",
    "R::qa!|выйти из всех без сохранения",
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
    NULL
};

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
    "R:Backspace|подняться на уровень вверх",
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
    "R:<leader>sR|открыть grug-far (пустой)",
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
    NULL
};

static const char *sec_plugins[] = {
    "T:ПЛАГИНЫ",
    "G:leap.nvim — прыжки по экрану",
    "R:s|прыжок вперёд (введи 2 символа → метка)",
    "R:S|прыжок назад",
    "R:gs|прыжок в другое окно",
    "N:Работает как оператор: ds<метка>, ys<метка>\" — удалить/обернуть до точки",
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
    NULL
};

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
    "N:Установлены: lua_ls, pyright, ts_ls, prettier, stylua, black, eslint_d, ruff",
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
    NULL
};

/* ── НОВЫЙ РАЗДЕЛ: IDE-функции ──────────────────────────────────────── */
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
    "N:code actions зависят от сервера: ts_ls предлагает импорты, extract function и т.д.",
    "B:",
    "G:Диагностика — текущий файл",
    "R:<leader>e|показать ошибку под курсором (float)",
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
    "N:Источники: LSP (приоритет) → сниппеты → пути → буфер",
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
    "N:Все серверы устанавливаются автоматически при первом запуске",
    "N:capabilities переданы cmp_nvim_lsp — LSP отдаёт полные данные для дополнения",
    "B:",
    "G:Типичный рабочий цикл",
    "N:1. Открыть файл — LSP подключается автоматически",
    "N:2. Ошибки сразу видны в gutter и inline",
    "N:3. <leader>xx — обзор всех проблем проекта",
    "N:4. gd / gr / K — навигация и документация",
    "N:5. <leader>ca — исправить / импортировать",
    "N:6. <leader>rn — переименовать символ везде",
    "N:7. :w — форматирование + линтинг автоматически",
    NULL
};

/* ══════════════════════════════════════════════════════════════════════
   FLAT LINE BUFFER
   ══════════════════════════════════════════════════════════════════════ */

#define FLAT_MAX 800
#define FLAT_LEN 320

typedef struct {
    char text[FLAT_LEN];
} FlatLine;

static FlatLine flat[FLAT_MAX];
static int      flat_total = 0;

static void flat_add(const char *s) {
    if (flat_total >= FLAT_MAX) return;
    snprintf(flat[flat_total].text, FLAT_LEN, "%s", s);
    flat_total++;
}

static void flat_build(const char **sec) {
    flat_total = 0;
    char buf[FLAT_LEN];
    for (int i = 0; sec[i]; i++) {
        const char *line    = sec[i];
        char        type    = line[0];
        const char *content = line + 2;
        switch (type) {
            case 'T':
                flat_add(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" RESET);
                snprintf(buf, FLAT_LEN, C_TITLE BOLD "  %s" RESET, content);
                flat_add(buf);
                flat_add(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" RESET);
                break;
            case 'G':
                flat_add("");
                snprintf(buf, FLAT_LEN, C_HEAD BOLD "  ## %s" RESET, content);
                flat_add(buf);
                break;
            case 'R': {
                char key[64], desc[256];
                const char *pipe = strchr(content, '|');
                if (pipe) {
                    int klen = (int)(pipe - content);
                    if (klen >= (int)sizeof(key)) klen = (int)sizeof(key) - 1;
                    strncpy(key, content, klen); key[klen] = '\0';
                    strncpy(desc, pipe + 1, sizeof(desc) - 1);
                    desc[sizeof(desc) - 1] = '\0';
                } else {
                    strncpy(key, content, sizeof(key) - 1);
                    key[sizeof(key) - 1] = '\0'; desc[0] = '\0';
                }
                snprintf(buf, FLAT_LEN,
                    "  " C_KEY BOLD "%-18s" RESET C_DESC "  %s" RESET, key, desc);
                flat_add(buf);
                break;
            }
            case 'N':
                snprintf(buf, FLAT_LEN, C_HINT DIM "  > %s" RESET, content);
                flat_add(buf);
                break;
            case 'B':
                flat_add("");
                break;
            default:
                snprintf(buf, FLAT_LEN, "  %s", line);
                flat_add(buf);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
   SECTION VIEWER
   ══════════════════════════════════════════════════════════════════════ */

static void view_section(const char **sec) {
    flat_build(sec);

    int total   = flat_total;
    int rows    = term_rows();
    int visible = rows - 3;
    int cursor  = 0;
    int offset  = 0;
    int last_g  = 0;

    printf(CUR_HIDE);

    while (1) {
        if (cursor < 0)      cursor = 0;
        if (cursor >= total) cursor = total - 1;

        if (cursor < offset)            offset = cursor;
        if (cursor >= offset + visible) offset = cursor - visible + 1;
        if (offset < 0)                 offset = 0;

        printf(CLR);

        for (int i = offset; i < offset + visible && i < total; i++) {
            if (i == cursor)
                printf(C_CUR "%s" RESET "\n", flat[i].text);
            else
                printf("%s\n", flat[i].text);
        }

        printf(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
        printf(C_HINT
            "  j/k↕  gg начало  G конец  %% край↔край  x/h выход"
            C_SEP "  [%d/%d]\n" RESET,
            cursor + 1, total);

        int key = read_key();

        if (key == 'j') {
            if (cursor < total - 1) cursor++;
            last_g = 0;
        } else if (key == 'k') {
            if (cursor > 0) cursor--;
            last_g = 0;
        } else if (key == 'g') {
            if (last_g) { cursor = 0; offset = 0; last_g = 0; }
            else        { last_g = 1; }
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

    printf(CUR_SHOW);
}

/* ══════════════════════════════════════════════════════════════════════
   MENU
   ══════════════════════════════════════════════════════════════════════ */

#define MENU_N 9

static const char *menu_labels[MENU_N] = {
    "Навигация",
    "Редактирование",
    "Текстовые объекты",
    "Поиск и замена",
    "Файлы, буферы, окна",
    "Работа с папкой / проектом  (oil / grug-far / telescope)",
    "Плагины  (leap / surround / commentary / targets)",
    "IDE-функции  (LSP / диагностика / автодополнение / линтинг)",
    "Telescope / Fugitive / Mason / Noice",
};

static const char **menu_sections[MENU_N] = {
    sec_navigation,
    sec_editing,
    sec_textobj,
    sec_search,
    sec_files,
    sec_dirwork,
    sec_plugins,
    sec_ide,
    sec_telescope,
};

static void print_menu(int cur) {
    printf(CLR);
    printf(C_TITLE BOLD
        "\n"
        "  ███╗   ██╗██╗   ██╗██╗███╗   ███╗████████╗██╗   ██╗████████╗ ██████╗ ██████╗ \n"
        "  ████╗  ██║██║   ██║██║████╗ ████║╚══██╔══╝██║   ██║╚══██╔══╝██╔═══██╗██╔══██╗\n"
        "  ██╔██╗ ██║██║   ██║██║██╔████╔██║   ██║   ██║   ██║   ██║   ██║   ██║██████╔╝\n"
        "  ██║╚██╗██║╚██╗ ██╔╝██║██║╚██╔╝██║   ██║   ██║   ██║   ██║   ██║   ██║██╔══██╗\n"
        "  ██║ ╚████║ ╚████╔╝ ██║██║ ╚═╝ ██║   ██║   ╚██████╔╝   ██║   ╚██████╔╝██║  ██║\n"
        "  ╚═╝  ╚═══╝  ╚═══╝  ╚═╝╚═╝     ╚═╝   ╚═╝    ╚═════╝    ╚═╝    ╚═════╝ ╚═╝  ╚═╝\n"
        RESET);
    printf(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);

    for (int i = 0; i < MENU_N; i++) {
        if (i == cur)
            printf(C_CUR BOLD "  ▶  %s" RESET "\n", menu_labels[i]);
        else
            printf(C_KEY "  [%d]" C_DESC "  %s\n" RESET, i + 1, menu_labels[i]);
    }

    printf(C_SEP "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
    printf(C_HINT "  j/k выбор   l/Enter открыть   %% край↔край   q выход\n" RESET);
}

int main(void) {
    term_raw();
    atexit(term_restore);
    printf(CUR_HIDE);

    int cur    = 0;
    int last_g = 0;

    while (1) {
        print_menu(cur);
        int key = read_key();

        if (key == 'j') {
            if (cur < MENU_N - 1) cur++;
            last_g = 0;
        } else if (key == 'k') {
            if (cur > 0) cur--;
            last_g = 0;
        } else if (key == 'g') {
            if (last_g) { cur = 0; last_g = 0; }
            else        { last_g = 1; }
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
            printf(CUR_SHOW CLR);
            printf(C_HINT "\n  bye\n\n" RESET);
            return 0;
        } else {
            last_g = 0;
        }
    }
}
