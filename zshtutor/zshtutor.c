#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* ── ANSI ───────────────────────────────────────────────────────────── */
#define RESET "\033[0m"
#define BOLD "\033[1m"
#define DIM "\033[2m"
#define CUR_HIDE "\033[?25l"
#define CUR_SHOW "\033[?25h"
#define CLR "\033[2J\033[H"

#define C_TITLE "\033[38;5;111m"
#define C_KEY "\033[38;5;183m"
#define C_DESC "\033[38;5;252m"
#define C_HEAD "\033[38;5;150m"
#define C_SEP "\033[38;5;240m"
#define C_HINT "\033[38;5;109m"
#define C_CUR "\033[48;5;237m\033[38;5;255m"

/* ── raw terminal ───────────────────────────────────────────────────── */
static struct termios orig_term;

static void term_raw(void) {
  struct termios t;
  tcgetattr(STDIN_FILENO, &orig_term);
  t = orig_term;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static void term_restore(void) {
  tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
  printf(CUR_SHOW);
}

static int read_key(void) {
  unsigned char c;
  if (read(STDIN_FILENO, &c, 1) != 1)
    return -1;
  if (c == 27) {
    unsigned char seq[2];
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return 27;
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return 27;
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
   "T:текст"    — title
   "G:текст"    — group header
   "R:key|desc" — row
   "N:текст"    — note
   "B:"         — blank
   ══════════════════════════════════════════════════════════════════════ */

static const char *sec_cmdline[] = {
    "T:НАВИГАЦИЯ В КОМАНДНОЙ СТРОКЕ",
    "G:Движение по строке (emacs-биндинги, работают в INSERT vi-mode)",
    "R:Ctrl+a|начало строки",
    "R:Ctrl+e|конец строки",
    "R:Alt+b|слово назад",
    "R:Alt+f|слово вперёд",
    "R:Ctrl+b|символ назад",
    "R:Ctrl+f|символ вперёд",
    "B:",
    "G:Удаление",
    "R:Ctrl+w|удалить слово слева",
    "R:Alt+d|удалить слово справа",
    "R:Ctrl+u|удалить всё слева от курсора",
    "R:Ctrl+k|удалить всё справа от курсора",
    "R:Ctrl+h|удалить символ слева (= Backspace)",
    "R:Ctrl+d|удалить символ под курсором",
    "B:",
    "G:Прочее",
    "R:Ctrl+l|очистить экран (аналог clear)",
    "R:Ctrl+c|прервать текущую команду",
    "R:Ctrl+z|приостановить (SIGTSTP), fg — вернуть",
    "R:Ctrl+_|отменить последнее изменение строки",
    "R:Tab|автодополнение (через fzf-tab)",
    "R:Alt+.|вставить последний аргумент предыдущей команды",
    "N:Alt+. можно нажимать несколько раз — идёт по истории аргументов",
    "N:Активен vi-mode (bindkey -v) — см. секцию «Vi-mode в zsh»",
    "N:Ctrl+A/E/W/R явно переопределены в vi-mode INSERT для удобства",
    NULL};

static const char *sec_history[] = {
    "T:ИСТОРИЯ КОМАНД",
    "G:Интерактивный поиск",
    "R:Ctrl+r|поиск по истории (fzf), работает в INSERT и NORMAL",
    "R:Up / Down|history-substring-search — ищет по введённому префиксу",
    "R:k / j (NORMAL)|history-substring-search в vi NORMAL mode",
    "N:Начни вводить команду, потом Up — найдёт только совпадающие",
    "B:",
    "G:Подстановки истории",
    "R:!!|повторить последнюю команду целиком",
    "R:!$|последний аргумент последней команды",
    "R:!^|первый аргумент последней команды",
    "R:!*|все аргументы последней команды",
    "R:!-2|команда два шага назад",
    "R:!git|последняя команда начинающаяся с 'git'",
    "R:!git:p|напечатать без выполнения",
    "R:^old^new|заменить old на new в последней команде",
    "B:",
    "G:Просмотр истории",
    "R:history|вывести историю с номерами",
    "R:history 20|последние 20 команд",
    "R:history | rg git|поиск по истории через ripgrep",
    "R:fc|открыть последнюю команду в $EDITOR (nvim)",
    "R:fc -l|список команд",
    "R:fc 50 60|показать команды 50–60",
    "B:",
    "G:Опции истории (активны в .zshrc)",
    "R:HIST_IGNORE_DUPS|не сохранять дубликаты подряд",
    "R:HIST_IGNORE_SPACE|не сохранять команды с пробелом в начале",
    "R:SHARE_HISTORY|история общая между всеми сессиями zsh в реальном времени",
    "N:HISTSIZE и SAVEHIST = 10000, HISTFILE = ~/.zsh_history",
    "N:Чтобы команда не попала в историю — поставь пробел перед ней",
    NULL};

static const char *sec_aliases[] = {
    "T:АЛИАСЫ",
    "G:Файлы и навигация",
    "R:ls|lsd (замена ls)",
    "R:la|lsd -A (включая скрытые)",
    "R:ll|lsd -l (подробный список)",
    "R:lah|lsd -lAh (подробный + скрытые + human)",
    "R:tree|lsd --tree (дерево)",
    "R:.. / ... / ....|cd .. / ../.. / ../../..",
    "B:",
    "G:Поиск и инструменты",
    "R:fnvim|fzf с превью (bat) → открыть в nvim",
    "R:zi|интерактивный zoxide (zoxide query -i)",
    "B:",
    "G:Редакторы конфигов",
    "R:zshrc|nvim ~/.zshrc",
    "R:aliases|nvim ~/.config/zsh/aliases.zsh",
    "R:nvimconfig|nvim ~/.config/nvim/init.lua",
    "R:hypr|nvim ~/.config/hypr/",
    "R:bashrc|nvim ~/.bashrc",
    "B:",
    "G:Прочее",
    "R:i <pkg>|sudo pacman -S --needed",
    "R:torrent|qbittorrent",
    NULL};

static const char *sec_functions[] = {
    "T:ФУНКЦИИ",
    "G:Файлы и система",
    "R:backup|бэкап текущей папки в ~/raf/backups/",
    "R:pwdcopy|скопировать путь текущей директории в буфер",
    "R:tn [dir]|открыть thunar в фоне (disown); без аргумента — cwd",
    "B:",
    "G:Сеть (network.zsh)",
    "R:ports|список всех LISTEN-портов (lsof)",
    "R:ports <n>|процессы на конкретном порту n",
    "R:killport <n>|завершить процесс на порту n (kill -9)",
    "R:myip|внешний IP (ifconfig.me) + локальный IP",
    "B:",
    "G:Утилиты",
    "R:weather [city]|прогноз погоды wttr.in; по умолчанию Almaty",
    "R:qr 'текст'|сгенерировать QR-код через qr-server.com API",
    "R:cow|вывести текст через cowsay | lolcat",
    "R:ff [more]|fastfetch (с поддержкой доп. конфигов)",
    "B:",
    "G:Quickshell",
    "R:qs --restart|перезапустить quickshell (pkill + nohup)",
    "R:qs <args>|обёртка для command qs",
    NULL};

static const char *sec_plugins[] = {
    "T:ПЛАГИНЫ ZSH (zinit)",
    "G:Менеджер плагинов",
    "N:Используется zinit (вместо oh-my-zsh). Исходник: /usr/share/zinit/zinit.zsh",
    "N:Все плагины загружаются лениво (wait lucid) — не замедляют старт",
    "R:zinit update|обновить все плагины",
    "B:",
    "G:zsh-autosuggestions",
    "N:Серым цветом показывает предсказание из истории",
    "R:→ (стрелка вправо)|принять предложение целиком",
    "R:Ctrl+→|принять одно слово из предложения",
    "B:",
    "G:fzf-tab",
    "N:Заменяет стандартный Tab-completion на fzf-интерфейс",
    "R:Tab|открыть fzf-меню вместо стандартного completion",
    "N:При cd показывает превью папки через lsd",
    "B:",
    "G:zsh-history-substring-search",
    "R:Up Arrow|найти предыдущую команду с текущим префиксом",
    "R:Down Arrow|следующая команда с текущим префиксом",
    "B:",
    "G:zsh-you-should-use",
    "N:Напоминает об алиасах если вводишь полную команду",
    "B:",
    "G:OMZ плагины (через zinit snippet)",
    "R:sudo (Esc Esc)|добавить sudo к текущей или последней команде",
    "N:git — алиасы gco, gcb, gd и т.д. (OMZ git plugin)",
    NULL};

static const char *sec_fzf[] = {
    "T:FZF",
    "G:Горячие клавиши (fzf key-bindings.zsh)",
    "R:Ctrl+r|поиск по истории команд (INSERT и NORMAL mode)",
    "R:Ctrl+t|поиск файлов в cwd, вставить в строку",
    "R:Alt+c|cd в выбранную папку",
    "N:Все три работают из любой точки командной строки",
    "B:",
    "G:Использование в командах",
    "R:cmd **<Tab>|fzf-completion для пути",
    "R:kill **<Tab>|выбрать процесс для kill",
    "R:ssh **<Tab>|выбрать хост из ~/.ssh/config",
    "R:export **<Tab>|выбрать переменную окружения",
    "N:** — триггер fzf completion, работает в большинстве команд",
    "B:",
    "G:Интерфейс fzf",
    "R:Ctrl+j / Ctrl+k|навигация вниз / вверх",
    "R:Enter|подтвердить выбор",
    "R:Ctrl+c / Esc|выйти без выбора",
    "R:Ctrl+Space|отметить элемент (multi-select)",
    "R:Tab|отметить и перейти к следующему",
    "R:Shift+Tab|снять отметку",
    "B:",
    "G:Настройки FZF_DEFAULT_OPTS (из .zshrc)",
    "R:--height=40%|окно занимает 40% терминала",
    "R:--layout=reverse|список снизу, ввод сверху",
    "R:--border=rounded|скруглённая рамка",
    "R:--info=inline|счётчик результатов в строке поиска",
    "N:Цветовая схема: Noctalia palette (мягкие пастельные тона)",
    "B:",
    "G:fzf-tab превью",
    "N:cd <Tab> показывает содержимое папки через lsd",
    "N:fnvim — fzf с bat-превью для быстрого открытия файлов в nvim",
    NULL};

static const char *sec_tools[] = {
    "T:ИНСТРУМЕНТЫ (zoxide / starship / direnv)",
    "G:zoxide — умный cd",
    "R:z <query>|перейти в наиболее частую папку по совпадению",
    "R:zi|интерактивный выбор через fzf",
    "R:z -|вернуться в предыдущую папку",
    "N:zoxide учится: чем чаще заходишь в папку, тем выше ранг",
    "B:",
    "G:starship — prompt",
    "N:Показывает: директорию, git-ветку и статус, язык/версию проекта",
    "N:Палитра: Noctalia (настроена в starship.toml)",
    "R:starship config|открыть конфиг в $EDITOR",
    "B:",
    "G:direnv — env per directory",
    "N:При входе в папку с .envrc автоматически загружает переменные",
    "R:direnv allow|разрешить .envrc в текущей папке",
    "B:",
    "G:bat — замена cat",
    "R:cat <file>|bat с подсветкой синтаксиса (алиас)",
    "R:bat --plain <file>|без декораций (plain output)",
    "B:",
    "G:lsd — замена ls",
    "R:ls / ll / la|алиасы для lsd с разными флагами",
    "R:tree|lsd --tree (дерево)",
    "R:lsd --group-directories-first|папки в начале списка",
    "B:",
    "G:ripgrep (rg) — замена grep",
    "R:rg <pattern>|поиск по текущей директории рекурсивно",
    "N:rg автоматически игнорирует .gitignore",
    "B:",
    "G:fd — замена find",
    "R:fd <name>|быстрый поиск файлов по имени",
    NULL};

static const char *sec_globbing[] = {
    "T:ZSH GLOBBING И ПОДСТАНОВКИ",
    "G:Стандартные wildcards",
    "R:*|любое количество любых символов",
    "R:?|один любой символ",
    "R:[abc]|один из символов a, b, c",
    "R:[a-z]|один символ из диапазона",
    "R:[^abc]|любой символ кроме a, b, c",
    "B:",
    "G:Расширенные glob (zsh)",
    "R:**|рекурсивный поиск во всех подпапках",
    "R:**/*.ts|все .ts файлы рекурсивно",
    "R:*(.)| только обычные файлы (без папок)",
    "R:*(/)| только директории",
    "R:*(x)| только исполняемые файлы",
    "R:*(@)| только симлинки",
    "R:*(om)|отсортировать по дате изменения",
    "R:*(L+1M)|файлы больше 1 МБ",
    "B:",
    "G:Отрицание и объединение",
    "R:^*.log|все файлы кроме .log  (setopt EXTENDED_GLOB)",
    "R:(*.ts|*.js)|файлы .ts или .js",
    "R:*(#i)readme*|без учёта регистра (setopt EXTENDED_GLOB)",
    "N:Для работы ^ и (#i) нужен: setopt EXTENDED_GLOB",
    "B:",
    "G:Подстановки переменных",
    "R:${var:-default}|значение или default если var не задан",
    "R:${var:=default}|присвоить default если var не задан",
    "R:${var:?error}|вывести ошибку если var не задан",
    "R:${#var}|длина строки / массива",
    "R:${var:0:5}|подстрока с 0, длина 5",
    "R:${var/old/new}|замена первого вхождения",
    "R:${var//old/new}|замена всех вхождений",
    "R:${var^^}|перевести в верхний регистр",
    "R:${var,,}|перевести в нижний регистр",
    "B:",
    "G:Подстановка команд и арифметика",
    "R:$(cmd)|подстановка вывода команды",
    "R:$((expr))|арифметика: $((2 + 2))",
    "R:<(cmd)|process substitution — как файл",
    "R:>(cmd)|process substitution — запись в команду",
    "N:Пример: diff <(sort file1) <(sort file2)",
    "B:",
    "G:Управление потоками",
    "R:cmd > file|перенаправить stdout в файл",
    "R:cmd >> file|добавить в файл",
    "R:cmd 2> file|перенаправить stderr",
    "R:cmd &> file|stdout + stderr в файл",
    "R:cmd1 | cmd2|pipe: stdout cmd1 → stdin cmd2",
    "R:cmd1 |& cmd2|pipe stdout + stderr",
    "R:cmd1 && cmd2|cmd2 только если cmd1 успешен",
    "R:cmd1 || cmd2|cmd2 только если cmd1 упал",
    "R:cmd1 ; cmd2|выполнить последовательно",
    NULL};

static const char *sec_jobcontrol[] = {
    "T:JOB CONTROL И ПРОЦЕССЫ",
    "G:Управление задачами",
    "R:cmd &|запустить в фоне",
    "R:Ctrl+z|приостановить текущий процесс (SIGTSTP)",
    "R:jobs|список фоновых задач",
    "R:fg|вернуть последнюю задачу на передний план",
    "R:fg %2|вернуть задачу номер 2",
    "R:bg|продолжить приостановленную задачу в фоне",
    "R:bg %2|продолжить задачу 2 в фоне",
    "R:kill %1|завершить задачу 1",
    "R:disown %1|отвязать задачу от терминала",
    "N:disown используется в функциях nhn(), qs(), obsid — они не падают при "
    "закрытии терминала",
    "B:",
    "G:Процессы",
    "R:ps aux | rg name|найти процесс по имени",
    "R:pgrep name|PID процессов по имени",
    "R:pkill name|завершить процессы по имени",
    "R:kill -9 <pid>|принудительно завершить",
    "R:ports|список LISTEN-портов (функция из network.zsh)",
    "R:killport <n>|завершить процесс на порту n",
    "B:",
    "G:nohup и disown",
    "N:nohup cmd &  — команда продолжает работать после закрытия терминала",
    "N:cmd & disown — то же, но без создания nohup.out",
    "N:Паттерн из конфига: nohup qs ... >/dev/null 2>&1 & disown",
    "B:",
    "G:Полезные комбинации",
    "R:jobs -l|список задач с PID",
    "R:wait|ждать завершения всех фоновых задач",
    "R:wait %1|ждать конкретную задачу",
    "N:Ctrl+c отправляет SIGINT — корректное завершение",
    "N:Ctrl+z отправляет SIGTSTP — пауза, можно продолжить через fg/bg",
    NULL};

static const char *sec_vimode[] = {
    "T:VI-MODE В ZSH",
    "G:Режимы и индикаторы",
    "N:Активируется через bindkey -v. KEYTIMEOUT=1 — быстрый выход по ESC",
    "N:RPROMPT показывает текущий режим:",
    "R:I (синий)|INSERT mode — ввод текста, emacs-биндинги активны",
    "R:N (красный)|NORMAL mode — навигация и редактирование vi",
    "N:Курсор: beam (|) в INSERT, block (█) в NORMAL — меняется автоматически",
    "N:При выходе из zsh курсор сбрасывается в beam",
    "B:",
    "G:Переключение режимов",
    "R:Esc|INSERT → NORMAL",
    "R:i|NORMAL → INSERT (перед курсором)",
    "R:a|NORMAL → INSERT (после курсора)",
    "R:I|NORMAL → INSERT (начало строки)",
    "R:A|NORMAL → INSERT (конец строки)",
    "R:v|NORMAL → VISUAL (выделение)",
    "B:",
    "G:Навигация в NORMAL mode",
    "R:h / l|символ влево / вправо",
    "R:w / b|слово вперёд / назад",
    "R:e|конец следующего слова",
    "R:0 / ^|начало строки / первый непробельный символ",
    "R:$|конец строки",
    "R:f<c> / F<c>|перейти к символу c вперёд / назад",
    "R:t<c> / T<c>|перейти до символа c вперёд / назад",
    "R:; / ,|повторить f/t вперёд / назад",
    "B:",
    "G:Редактирование в NORMAL mode",
    "R:x|удалить символ под курсором",
    "R:d<motion>|удалить по motion: dw, db, d$, d0",
    "R:dd|удалить строку (всю командную строку)",
    "R:c<motion>|удалить по motion + войти в INSERT",
    "R:cc / S|очистить строку и войти в INSERT",
    "R:r<c>|заменить символ на c без выхода в INSERT",
    "R:R|режим замены (перезапись символов)",
    "R:y<motion>|скопировать: yw, y$",
    "R:yy|скопировать всю строку",
    "R:p / P|вставить после / до курсора",
    "R:u|отмена (undo) — в пределах текущей строки",
    "B:",
    "G:Биндинги INSERT mode (явно переопределены)",
    "R:Ctrl+a|начало строки",
    "R:Ctrl+e|конец строки",
    "R:Ctrl+w|удалить слово слева",
    "R:Ctrl+r|поиск по истории (fzf)",
    "R:Backspace / Ctrl+h|удалить символ слева (корректно после ESC)",
    "N:Backspace явно переопределён — без этого в vi-mode он ломается",
    "B:",
    "G:История в NORMAL mode",
    "R:k|предыдущая команда с совпадающим префиксом (substring-search)",
    "R:j|следующая команда с совпадающим префиксом",
    "R:Ctrl+r|поиск по истории (fzf) — работает в обоих режимах",
    "N:k/j переопределены на history-substring-search (не vi-up-line)",
    "N:Биндинги применяются лениво при первом precmd после загрузки плагина",
    "B:",
    "G:Текстовые объекты (VISUAL и operator-pending)",
    "N:Работают в VISUAL mode и после операторов d/c/y в NORMAL:",
    "R:ci\" / ca\"|изменить внутри / включая двойные кавычки",
    "R:ci' / ca'|изменить внутри / включая одинарные кавычки",
    "R:ci` / ca`|изменить внутри / включая обратные кавычки",
    "R:ci( / ca(|изменить внутри / включая скобки ()",
    "R:ci[ / ca[|изменить внутри / включая скобки []",
    "R:ci{ / ca{|изменить внутри / включая скобки {}",
    "R:ci< / ca<|изменить внутри / включая угловые скобки",
    "N:Аналогично di\", yi( и т.д. — удалить / скопировать",
    "N:Реализовано через autoload select-bracketed select-quoted",
    "B:",
    "G:Surround (ys / cs / ds)",
    "N:Реализовано через autoload surround — аналог vim-surround",
    "R:ys<motion><c>|обернуть motion в символ c",
    "R:ysiw\"|обернуть слово под курсором в двойные кавычки",
    "R:ysiw(|обернуть слово в скобки (со пробелами)",
    "R:ysiw)|обернуть слово в скобки (без пробелов)",
    "R:cs\"'|заменить окружающие \" на '",
    "R:cs'\"|заменить окружающие ' на \"",
    "R:ds\"|удалить окружающие двойные кавычки",
    "R:ds(|удалить окружающие скобки",
    "R:S<c> (VISUAL)|обернуть выделение в символ c",
    "N:Привязки: cs / ds / ys в vicmd, S в visual",
    "B:",
    "G:Советы",
    "N:ESC Esc (двойной) — добавляет sudo (OMZ sudo plugin, работает в обоих "
    "режимах)",
    "N:В NORMAL mode набери число перед motion: 3w — три слова вперёд",
    "N:Команда : в NORMAL в zsh не открывает ex — это просто символ",
    "N:Для сложного редактирования строки — fc: откроет nvim с командой",
    NULL};

/* ══════════════════════════════════════════════════════════════════════
   FLAT LINE BUFFER
   ══════════════════════════════════════════════════════════════════════ */

#define FLAT_MAX 1000
#define FLAT_LEN 320

typedef struct {
  char text[FLAT_LEN];
} FlatLine;

static FlatLine flat[FLAT_MAX];
static int flat_total = 0;

static void flat_add(const char *s) {
  if (flat_total >= FLAT_MAX)
    return;
  snprintf(flat[flat_total].text, FLAT_LEN, "%s", s);
  flat_total++;
}

static void flat_build(const char **sec) {
  flat_total = 0;
  char buf[FLAT_LEN];
  for (int i = 0; sec[i]; i++) {
    const char *line = sec[i];
    char type = line[0];
    const char *content = line + 2;
    switch (type) {
    case 'T':
      flat_add(
          C_SEP
          "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" RESET);
      snprintf(buf, FLAT_LEN, C_TITLE BOLD "  %s" RESET, content);
      flat_add(buf);
      flat_add(
          C_SEP
          "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" RESET);
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
        if (klen >= (int)sizeof(key))
          klen = (int)sizeof(key) - 1;
        strncpy(key, content, klen);
        key[klen] = '\0';
        strncpy(desc, pipe + 1, sizeof(desc) - 1);
        desc[sizeof(desc) - 1] = '\0';
      } else {
        strncpy(key, content, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';
        desc[0] = '\0';
      }
      snprintf(buf, FLAT_LEN, "  " C_KEY BOLD "%-22s" RESET C_DESC "  %s" RESET,
               key, desc);
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

  int total = flat_total;
  int rows = term_rows();
  int visible = rows - 3;
  int cursor = 0;
  int offset = 0;
  int last_g = 0;

  printf(CUR_HIDE);

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

    printf(CLR);

    for (int i = offset; i < offset + visible && i < total; i++) {
      if (i == cursor)
        printf(C_CUR "%s" RESET "\n", flat[i].text);
      else
        printf("%s\n", flat[i].text);
    }

    printf(C_SEP
           "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
    printf(C_HINT "  j/k↕  gg начало  G конец  %% край↔край  x/h выход" C_SEP
                  "  [%d/%d]\n" RESET,
           cursor + 1, total);

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
      } else {
        last_g = 1;
      }
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

#define MENU_N 10

static const char *menu_labels[MENU_N] = {
    "Навигация в командной строке",
    "История команд",
    "Алиасы",
    "Функции  (backup / ccmd / network / qs / weather...)",
    "Плагины zinit  (autosuggestions / fzf-tab / sudo / dirhistory...)",
    "fzf  (Ctrl+r / Ctrl+t / Alt+c / completion)",
    "Инструменты  (zoxide / bat / eza / rg / fd / direnv / uv / starship)",
    "Globbing и подстановки",
    "Job control и процессы",
    "Vi-mode в zsh  (режимы / text objects / surround / курсор / RPROMPT)",
};

static const char **menu_sections[MENU_N] = {
    sec_cmdline, sec_history, sec_aliases,  sec_functions,  sec_plugins,
    sec_fzf,     sec_tools,   sec_globbing, sec_jobcontrol, sec_vimode,
};

static void print_menu(int cur) {
  printf(CLR);
  printf(C_TITLE BOLD "\n"
                      "  ███████╗  ██████╗ ██╗  ██╗ ████████╗ ██╗   ██╗ "
                      "████████╗  ██████╗  ██████╗ \n"
                      "     ███╔╝ ██╔════╝ ██║  ██║ ╚══██╔══╝ ██║   ██║ "
                      "╚══██╔══╝ ██╔═══██╗ ██╔══██╗\n"
                      "    ███╔╝  ╚█████╗  ███████║    ██║    ██║   ██║    ██║ "
                      "   ██║   ██║ ██████╔╝\n"
                      "   ███╔╝    ╚═══██╗ ██╔══██║    ██║    ██║   ██║    ██║ "
                      "   ██║   ██║ ██╔══██╗\n"
                      "  ███████╗ ██████╔╝ ██║  ██║    ██║    ╚██████╔╝    ██║ "
                      "   ╚██████╔╝ ██║  ██║\n"
                      "  ╚══════╝ ╚═════╝  ╚═╝  ╚═╝    ╚═╝     ╚═════╝     ╚═╝ "
                      "    ╚═════╝  ╚═╝  ╚═╝\n" RESET);
  printf(C_HINT DIM "  zsh · zinit · vi-mode · fzf · zoxide · starship · eza · "
                    "bat · rg · fd\n" RESET);
  printf(C_SEP
         "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);

  for (int i = 0; i < MENU_N; i++) {
    if (i == cur)
      printf(C_CUR BOLD "  ▶  %s" RESET "\n", menu_labels[i]);
    else
      printf(C_KEY "  [%d]" C_DESC "  %s\n" RESET, i + 1, menu_labels[i]);
  }

  printf(C_SEP
         "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
  printf(C_HINT
         "  j/k выбор   l/Enter открыть   %% край↔край   q выход\n" RESET);
}

int main(void) {
  term_raw();
  atexit(term_restore);
  printf(CUR_HIDE);

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
      } else {
        last_g = 1;
      }
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
