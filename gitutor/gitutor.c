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

#define C_TITLE "\033[38;5;214m"
#define C_KEY "\033[38;5;183m"
#define C_DESC "\033[38;5;252m"
#define C_HEAD "\033[38;5;150m"
#define C_SEP "\033[38;5;240m"
#define C_HINT "\033[38;5;109m"
#define C_CUR "\033[48;5;237m\033[38;5;255m"
#define C_CODE "\033[38;5;222m"

/* ── frame buffer ───────────────────────────────────────────────────── */
static char *fbuf = NULL;
static size_t fbuf_cap = 0;
static size_t fbuf_len = 0;

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
  if (fbuf_len > 0)
    xwrite(fbuf, fbuf_len);
  fb_reset();
}

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
  xwrite(CUR_SHOW, sizeof(CUR_SHOW) - 1);
  xwrite(ALT_OFF, sizeof(ALT_OFF) - 1);
}

static int read_key(void) {
  unsigned char c;
  if (read(STDIN_FILENO, &c, 1) != 1)
    return -1;
  if (c == 27) {
    unsigned char seq[3];
    fd_set fds;
    struct timeval tv = {0, 50000};
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0)
      return 27;
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return 27;
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return 27;
    if (seq[0] == '[') {
      if (seq[1] == 'A')
        return 'k';
      if (seq[1] == 'B')
        return 'j';
    }
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
   "R:key|desc" — row (команда | описание)
   "C:код"      — code line (выделяется как код)
   "N:текст"    — note
   "B:"         — blank
   ══════════════════════════════════════════════════════════════════════ */

static const char *sec_basics[] = {
    "T:ОСНОВЫ GIT",
    "G:Инициализация и клонирование",
    "R:git init|инициализировать репозиторий в текущей папке",
    "R:git init <dir>|создать репозиторий в папке <dir>",
    "R:git clone <url>|клонировать удалённый репозиторий",
    "R:git clone <url> <dir>|клонировать в папку <dir>",
    "R:git clone --depth 1 <url>|shallow clone — только последний коммит",
    "B:",
    "G:Статус и просмотр",
    "R:git status|показать статус рабочей директории",
    "R:git status -s|короткий формат статуса",
    "R:git diff|изменения в рабочей директории (не staged)",
    "R:git diff --staged|изменения в staging area",
    "R:git diff HEAD|все изменения с последнего коммита",
    "R:git diff <branch1>..<branch2>|разница между ветками",
    "B:",
    "G:Staging (индекс)",
    "R:git add <file>|добавить файл в staging",
    "R:git add .|добавить все изменения в текущей папке",
    "R:git add -A|добавить все изменения во всём репозитории",
    "R:git add -p|интерактивный выбор кусков для staging",
    "R:git restore --staged <file>|убрать файл из staging (не трогая файл)",
    "R:git rm <file>|удалить файл и поставить в staging",
    "R:git rm --cached <file>|убрать из git, но оставить файл на диске",
    "B:",
    "G:Коммиты",
    "R:git commit -m 'msg'|создать коммит с сообщением",
    "R:git commit -am 'msg'|stage + commit для отслеживаемых файлов",
    "R:git commit --amend|изменить последний коммит (сообщение/файлы)",
    "R:git commit --amend --no-edit|изменить коммит без смены сообщения",
    "N:--amend переписывает историю — не использовать на опубликованных коммитах",
    "B:",
    "G:Соглашения по коммитам (Conventional Commits)",
    "R:feat: ...|новая функциональность",
    "R:fix: ...|исправление бага",
    "R:refactor: ...|рефакторинг без изменения поведения",
    "R:docs: ...|изменения в документации",
    "R:style: ...|форматирование, запятые, пробелы",
    "R:test: ...|добавление или исправление тестов",
    "R:chore: ...|обновление зависимостей, конфигов",
    "R:ci: ...|изменения CI/CD пайплайна",
    "N:Формат: тип(scope): описание  →  feat(auth): add JWT refresh",
    NULL};

static const char *sec_log[] = {
    "T:ИСТОРИЯ И ПРОСМОТР",
    "G:git log",
    "R:git log|полная история",
    "R:git log --oneline|одна строка на коммит",
    "R:git log --oneline --graph|граф веток в ASCII",
    "R:git log --oneline --graph --all|граф всех веток",
    "R:git log -10|последние 10 коммитов",
    "R:git log --author='name'|коммиты конкретного автора",
    "R:git log --since='2 weeks ago'|коммиты за 2 недели",
    "R:git log --grep='feat'|коммиты с 'feat' в сообщении",
    "R:git log -- <file>|история изменений конкретного файла",
    "R:git log -p <file>|история + diff файла",
    "R:git log --stat|статистика изменённых файлов",
    "B:",
    "G:Просмотр конкретных объектов",
    "R:git show <commit>|показать коммит (diff + мета)",
    "R:git show HEAD|последний коммит",
    "R:git show HEAD~2|коммит два шага назад",
    "R:git show <commit>:<file>|файл в конкретном коммите",
    "B:",
    "G:Поиск",
    "R:git grep <pattern>|поиск по рабочей директории",
    "R:git grep <pattern> <commit>|поиск в конкретном коммите",
    "R:git log -S 'string'|коммиты, где строка появилась/исчезла (pickaxe)",
    "R:git log -G 'regex'|коммиты с diff, совпадающим с regex",
    "R:git bisect start|начать бинарный поиск регрессии",
    "R:git bisect good <commit>|отметить коммит как рабочий",
    "R:git bisect bad|отметить текущий коммит как сломанный",
    "R:git bisect reset|закончить bisect",
    "N:git bisect автоматически находит коммит, сломавший функциональность",
    "B:",
    "G:blame",
    "R:git blame <file>|кто и когда изменил каждую строку",
    "R:git blame -L 10,20 <file>|blame для строк 10–20",
    "R:git blame -w <file>|игнорировать изменения пробелов",
    NULL};

static const char *sec_branches[] = {
    "T:ВЕТКИ",
    "G:Создание и переключение",
    "R:git branch|список локальных веток",
    "R:git branch -a|все ветки (локальные + remote)",
    "R:git branch <name>|создать ветку (не переключаться)",
    "R:git switch <name>|переключиться на ветку",
    "R:git switch -c <name>|создать ветку и переключиться",
    "R:git switch -c <name> <base>|создать от конкретного коммита/ветки",
    "R:git checkout <name>|старый синтаксис switch",
    "R:git checkout -b <name>|старый синтаксис switch -c",
    "B:",
    "G:Удаление и переименование",
    "R:git branch -d <name>|удалить ветку (только если merged)",
    "R:git branch -D <name>|удалить ветку принудительно",
    "R:git branch -m <old> <new>|переименовать ветку",
    "R:git branch -m <new>|переименовать текущую ветку",
    "R:git push origin --delete <name>|удалить ветку на remote",
    "B:",
    "G:Merge",
    "R:git merge <branch>|влить ветку в текущую",
    "R:git merge --no-ff <branch>|merge с merge-коммитом (без fast-forward)",
    "R:git merge --squash <branch>|собрать все коммиты ветки в один",
    "R:git merge --abort|отменить merge при конфликте",
    "N:--no-ff сохраняет историю ветки, fast-forward делает историю линейной",
    "B:",
    "G:Rebase",
    "R:git rebase <branch>|перебазировать текущую ветку на <branch>",
    "R:git rebase -i HEAD~3|интерактивный rebase последних 3 коммитов",
    "R:git rebase --continue|продолжить после разрешения конфликта",
    "R:git rebase --abort|отменить rebase",
    "R:git rebase --skip|пропустить текущий коммит при rebase",
    "N:rebase переписывает историю — не использовать на общих ветках",
    "N:pick → squash (s) — склеить коммиты; reword (r) — изменить сообщение",
    "B:",
    "G:Cherry-pick",
    "R:git cherry-pick <commit>|применить конкретный коммит к текущей ветке",
    "R:git cherry-pick <c1>..<c2>|применить диапазон коммитов",
    "R:git cherry-pick --no-commit <c>|применить без создания коммита",
    "R:git cherry-pick --abort|отменить cherry-pick",
    NULL};

static const char *sec_remote[] = {
    "T:РАБОТА С REMOTE",
    "G:Remote-репозитории",
    "R:git remote|список remote",
    "R:git remote -v|список с URL",
    "R:git remote add origin <url>|добавить remote с именем origin",
    "R:git remote remove <name>|удалить remote",
    "R:git remote rename <old> <new>|переименовать remote",
    "R:git remote set-url origin <url>|изменить URL remote",
    "B:",
    "G:Fetch и Pull",
    "R:git fetch|скачать изменения с remote (без merge)",
    "R:git fetch --all|скачать со всех remote",
    "R:git fetch --prune|удалить ссылки на удалённые ветки",
    "R:git pull|fetch + merge текущей ветки",
    "R:git pull --rebase|fetch + rebase (линейная история)",
    "R:git pull origin main|явно указать remote и ветку",
    "B:",
    "G:Push",
    "R:git push|отправить текущую ветку",
    "R:git push origin <branch>|явно указать remote и ветку",
    "R:git push -u origin <branch>|push + установить upstream",
    "R:git push --all|отправить все ветки",
    "R:git push --tags|отправить все теги",
    "R:git push --force-with-lease|force push с проверкой (безопаснее --force)",
    "N:--force-with-lease упадёт если кто-то успел запушить — защита от потери",
    "B:",
    "G:Tracking и upstream",
    "R:git branch -u origin/<branch>|установить upstream для текущей ветки",
    "R:git branch -vv|показать upstream каждой ветки",
    "R:git push -u origin HEAD|push + upstream для ветки с тем же именем",
    NULL};

static const char *sec_stash[] = {
    "T:STASH, RESET, RESTORE",
    "G:git stash — временное хранилище",
    "R:git stash|сохранить рабочие изменения в стек",
    "R:git stash push -m 'msg'|stash с описанием",
    "R:git stash -u|включить untracked файлы",
    "R:git stash list|список stash-записей",
    "R:git stash pop|применить последний stash и удалить его",
    "R:git stash apply stash@{2}|применить конкретный stash",
    "R:git stash drop stash@{1}|удалить конкретный stash",
    "R:git stash clear|удалить все stash",
    "R:git stash show -p|показать diff последнего stash",
    "B:",
    "G:git restore — восстановление файлов",
    "R:git restore <file>|отменить изменения в рабочей директории",
    "R:git restore .|отменить все изменения",
    "R:git restore --staged <file>|убрать из staging (unstage)",
    "R:git restore --source=HEAD~2 <file>|восстановить файл из коммита",
    "B:",
    "G:git reset — сдвиг HEAD",
    "R:git reset HEAD~1|отменить последний коммит, изменения остаются staged",
    "R:git reset --soft HEAD~1|отменить коммит, изменения остаются staged",
    "R:git reset --mixed HEAD~1|отменить коммит, изменения в working dir",
    "R:git reset --hard HEAD~1|отменить коммит, изменения УДАЛЯЮТСЯ",
    "N:--hard уничтожает изменения. Используй осторожно.",
    "B:",
    "G:git revert — безопасная отмена",
    "R:git revert <commit>|создать коммит, отменяющий изменения",
    "R:git revert HEAD|отменить последний коммит (без удаления истории)",
    "R:git revert HEAD~3..HEAD|отменить последние 3 коммита",
    "R:git revert --no-commit HEAD|применить отмену без коммита",
    "N:revert безопасен для общих веток — не переписывает историю",
    "N:reset --hard опасен на общих ветках — переписывает историю",
    NULL};

static const char *sec_tags[] = {
    "T:ТЕГИ И РЕЛИЗЫ",
    "G:Работа с тегами",
    "R:git tag|список тегов",
    "R:git tag <name>|создать lightweight тег на HEAD",
    "R:git tag -a <name> -m 'msg'|создать annotated тег (с мета-данными)",
    "R:git tag -a <name> <commit>|тег на конкретный коммит",
    "R:git tag -d <name>|удалить тег локально",
    "R:git push origin <name>|отправить тег на remote",
    "R:git push --tags|отправить все теги",
    "R:git push origin --delete <name>|удалить тег на remote",
    "R:git show <name>|показать информацию о теге",
    "B:",
    "G:Семантическое версионирование",
    "N:Формат: MAJOR.MINOR.PATCH  →  v1.4.2",
    "N:MAJOR — несовместимые изменения API",
    "N:MINOR — новый функционал с обратной совместимостью",
    "N:PATCH — исправление багов",
    "R:git describe|описать коммит на основе тегов",
    "R:git describe --tags|включая lightweight теги",
    NULL};

static const char *sec_config[] = {
    "T:КОНФИГУРАЦИЯ",
    "G:Базовая настройка",
    "R:git config --global user.name 'Name'|установить имя",
    "R:git config --global user.email 'e@mail'|установить email",
    "R:git config --global core.editor nvim|редактор для коммитов",
    "R:git config --global init.defaultBranch main|ветка по умолчанию",
    "R:git config --global pull.rebase true|pull с rebase по умолчанию",
    "R:git config --list|показать все настройки",
    "R:git config --global -e|открыть ~/.gitconfig в редакторе",
    "B:",
    "G:.gitignore",
    "R:.gitignore|локальный файл игнора (коммитится в репо)",
    "R:~/.config/git/ignore|глобальный gitignore для всех репо",
    "R:git check-ignore -v <file>|узнать почему файл игнорируется",
    "R:git ls-files --others --ignored --exclude-standard|список игнорируемых",
    "N:Шаблоны: *.log, /dist, node_modules/, !important.log",
    "B:",
    "G:Алиасы git",
    "R:git config --global alias.st status|создать алиас: git st",
    "R:git config --global alias.lo 'log --oneline --graph'|алиас с параметрами",
    "R:git config --global alias.undo 'reset HEAD~1'|алиас для отмены",
    "N:После настройки: git st, git lo, git undo",
    "B:",
    "G:Полезные настройки",
    "R:git config --global color.ui auto|цветной вывод",
    "R:git config --global core.autocrlf input|нормализация переносов строк",
    "R:git config --global diff.tool nvim|инструмент для diff",
    "R:git config --global merge.conflictstyle diff3|стиль конфликтов",
    "N:diff3 показывает три версии при конфликте — проще разрешать",
    NULL};

static const char *sec_conflicts[] = {
    "T:КОНФЛИКТЫ",
    "G:Процесс разрешения конфликтов",
    "N:Конфликт возникает при merge/rebase/cherry-pick когда обе ветки",
    "N:изменили одно место файла. Git помечает конфликтные секции:",
    "C:<<<<<<< HEAD",
    "C:  твои изменения",
    "C:=======",
    "C:  их изменения",
    "C:>>>>>>> branch-name",
    "B:",
    "G:Команды при конфликте",
    "R:git status|показать файлы с конфликтами (both modified)",
    "R:git diff|показать конфликтные маркеры",
    "R:git mergetool|открыть визуальный merge-инструмент",
    "R:git add <file>|отметить конфликт как решённый",
    "R:git merge --abort|отменить merge (вернуться до начала)",
    "R:git rebase --abort|отменить rebase",
    "B:",
    "G:Стратегии при merge",
    "R:git merge -X ours <branch>|предпочесть нашу версию при конфликтах",
    "R:git merge -X theirs <branch>|предпочесть их версию при конфликтах",
    "R:git checkout --ours <file>|взять нашу версию файла",
    "R:git checkout --theirs <file>|взять их версию файла",
    "N:После checkout --ours/theirs нужно: git add <file>",
    "B:",
    "G:Советы",
    "N:Настрой merge.conflictstyle diff3 — видно общего предка",
    "N:Используй git log --merge для коммитов, вызвавших конфликт",
    "N:Маленькие частые коммиты = меньше конфликтов",
    "N:Регулярный pull/fetch = конфликты проще разрешать",
    NULL};

static const char *sec_workflow[] = {
    "T:WORKFLOW: DEV + MAIN",
    "G:Базовый рабочий цикл (feature development)",
    "N:Разработка ведётся в dev. В main идут только готовые релизы.",
    "B:",
    "C:git switch dev",
    "C:git add .",
    "C:git commit -m \"feat: add note editor\"",
    "C:git push origin dev",
    "B:",
    "G:Деплой (когда dev готов к продакшену)",
    "N:Мержим dev в main — это тригерит полный пайплайн.",
    "B:",
    "C:git switch main",
    "C:git merge dev",
    "C:git push origin main   # триггерит полный пайплайн",
    "C:git switch dev",
    "B:",
    "G:Хотфиксы (критический баг прямо в main)",
    "N:Фиксишь напрямую в main, потом синхронизируешь dev.",
    "B:",
    "C:# фикс в main",
    "C:git switch main",
    "C:git add .",
    "C:git commit -m \"fix: critical bug\"",
    "C:git push origin main   # триггерит полный пайплайн",
    "B:",
    "C:# синхронизируешь dev",
    "C:git switch dev",
    "C:git merge main",
    "C:git push origin dev    # триггерит install + test",
    "C:git switch dev",
    "B:",
    "G:Правила ветки main",
    "N:main = стабильный продакшен. Никогда не коммить незаконченный код.",
    "N:Каждый push в main = деплой. Убедись что тесты прошли в dev.",
    "N:После мержа в main — сразу возвращайся в dev для продолжения работы.",
    "B:",
    "G:Правила ветки dev",
    "N:dev = рабочая ветка. Коммить часто, мелкими кусками.",
    "N:Перед деплоем убедись что dev актуален (git pull origin dev).",
    "N:При конфликте после хотфикса — разрешай и коммить merge-коммит.",
    "B:",
    "G:Быстрые команды workflow (алиасы из zsh)",
    "R:gst|git status",
    "R:glo|git log --oneline",
    "R:gps|git push",
    "R:gpl|git pull",
    NULL};

static const char *sec_advanced[] = {
    "T:ПРОДВИНУТЫЕ ТЕХНИКИ",
    "G:Reflog — история HEAD",
    "R:git reflog|список всех перемещений HEAD",
    "R:git reflog show <branch>|reflog конкретной ветки",
    "R:git checkout HEAD@{3}|вернуться к состоянию 3 шага назад",
    "N:reflog хранится 90 дней — можно восстановить «удалённые» коммиты",
    "N:После git reset --hard: git reflog → git checkout <потерянный SHA>",
    "B:",
    "G:Worktree — несколько рабочих директорий",
    "R:git worktree add <path> <branch>|создать worktree для ветки",
    "R:git worktree list|список всех worktree",
    "R:git worktree remove <path>|удалить worktree",
    "N:Полезно: работать над hotfix не переключая текущую ветку",
    "B:",
    "G:Submodule",
    "R:git submodule add <url>|добавить submodule",
    "R:git submodule update --init|инициализировать submodule после clone",
    "R:git submodule update --remote|обновить submodule до последнего commit",
    "R:git clone --recurse-submodules <url>|клонировать с submodule",
    "B:",
    "G:Sparse checkout",
    "R:git sparse-checkout init|включить sparse checkout",
    "R:git sparse-checkout set <dir>|чекаутить только <dir>",
    "R:git sparse-checkout disable|выключить sparse checkout",
    "N:Полезно для монорепо — не скачивать весь репозиторий",
    "B:",
    "G:Разное",
    "R:git clean -fd|удалить untracked файлы и папки",
    "R:git clean -n|показать что будет удалено (dry run)",
    "R:git shortlog -sn|количество коммитов по авторам",
    "R:git archive --format=zip HEAD > out.zip|архив текущего состояния",
    "R:git notes add <commit>|добавить заметку к коммиту",
    "R:git bundle create repo.bundle --all|портативный бандл репозитория",
    "B:",
    "G:Signing коммитов",
    "R:git commit -S -m 'msg'|подписать коммит GPG-ключом",
    "R:git config --global commit.gpgSign true|всегда подписывать",
    "R:git log --show-signature|показать подписи в логе",
    NULL};

/* ══════════════════════════════════════════════════════════════════════
   FLAT LINE BUFFER
   ══════════════════════════════════════════════════════════════════════ */

#define FLAT_MAX 1200
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
      flat_add(C_SEP
               "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" RESET);
      snprintf(buf, FLAT_LEN, C_TITLE BOLD "  %s" RESET, content);
      flat_add(buf);
      flat_add(C_SEP
               "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" RESET);
      break;
    case 'G':
      flat_add("");
      snprintf(buf, FLAT_LEN, C_HEAD BOLD "  ## %s" RESET, content);
      flat_add(buf);
      break;
    case 'R': {
      char key[80], desc[256];
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
      snprintf(buf, FLAT_LEN,
               "  " C_KEY BOLD "%-34s" RESET C_DESC "  %s" RESET, key, desc);
      flat_add(buf);
      break;
    }
    case 'C':
      snprintf(buf, FLAT_LEN, C_CODE "  $ %s" RESET, content);
      flat_add(buf);
      break;
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

  fb_reset();
  fb_append(ALT_ON);
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

    fb_append(C_SEP
              "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
    fb_appendf(C_HINT "  j/k↕  gg начало  G конец  %% край↔край  q выход" C_SEP
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
    } else if (key == 'd') {
      cursor += visible / 2;
      last_g = 0;
    } else if (key == 'u') {
      cursor -= visible / 2;
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

  fb_reset();
  fb_append(ALT_OFF);
  fb_append(CUR_SHOW);
  fb_flush();
}

/* ══════════════════════════════════════════════════════════════════════
   MENU
   ══════════════════════════════════════════════════════════════════════ */

#define MENU_N 8

static const char *menu_labels[MENU_N] = {
    "Основы  (init / add / commit / diff / restore)",
    "История  (log / show / grep / bisect / blame)",
    "Ветки  (branch / switch / merge / rebase / cherry-pick)",
    "Remote  (fetch / pull / push / upstream)",
    "Stash, Reset, Restore, Revert",
    "Теги и релизы  (semver)",
    "Конфигурация  (.gitconfig / .gitignore / алиасы)",
    "Конфликты  (разрешение / стратегии)",
};

static const char **menu_sections[MENU_N] = {
    sec_basics,   sec_log,     sec_branches, sec_remote,
    sec_stash,    sec_tags,    sec_config,   sec_conflicts,
};

/* workflow и advanced доступны как отдельные пункты */
#define MENU_TOTAL (MENU_N + 2)

static const char *menu_labels_ext[MENU_TOTAL];
static const char **menu_sections_ext[MENU_TOTAL];

static void menu_init(void) {
  for (int i = 0; i < MENU_N; i++) {
    menu_labels_ext[i] = menu_labels[i];
    menu_sections_ext[i] = menu_sections[i];
  }
  menu_labels_ext[MENU_N]     = "Workflow: dev + main  (деплой / хотфиксы)";
  menu_sections_ext[MENU_N]   = sec_workflow;
  menu_labels_ext[MENU_N + 1] = "Продвинутые техники  (reflog / worktree / sparse)";
  menu_sections_ext[MENU_N + 1] = sec_advanced;
}

static void print_menu(int cur) {
  fb_reset();
  fb_append(CLR);
  fb_append(
      C_TITLE BOLD "\n"
      "   ██████╗ ██╗████████╗████████╗██╗   ██╗████████╗ ██████╗ ██████╗ \n"
      "  ██╔════╝ ██║╚══██╔══╝╚══██╔══╝██║   ██║╚══██╔══╝██╔═══██╗██╔══██╗\n"
      "  ██║  ███╗██║   ██║      ██║   ██║   ██║   ██║   ██║   ██║██████╔╝\n"
      "  ██║   ██║██║   ██║      ██║   ██║   ██║   ██║   ██║   ██║██╔══██╗\n"
      "  ╚██████╔╝██║   ██║      ██║   ╚██████╔╝   ██║   ╚██████╔╝██║  ██║\n"
      "   ╚═════╝ ╚═╝   ╚═╝      ╚═╝    ╚═════╝    ╚═╝    ╚═════╝ ╚═╝  ╚═╝\n" RESET);
  fb_append(C_HINT DIM
            "  git · branches · remote · stash · rebase · workflow · reflog\n" RESET);
  fb_append(C_SEP
            "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);

  char buf[256];
  for (int i = 0; i < MENU_TOTAL; i++) {
    if (i == cur)
      snprintf(buf, sizeof(buf),
               C_CUR BOLD "  ▶  %s" RESET "\n", menu_labels_ext[i]);
    else
      snprintf(buf, sizeof(buf),
               C_KEY "  [%d]" C_DESC "  %s\n" RESET, i + 1, menu_labels_ext[i]);
    fb_append(buf);
  }

  fb_append(C_SEP
            "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" RESET);
  fb_append(C_HINT
            "  j/k выбор   l/Enter открыть   %% край↔край   q выход\n" RESET);
  fb_flush();
}

int main(void) {
  menu_init();
  term_raw();
  atexit(term_restore);

  fb_reset();
  fb_append(ALT_ON);
  fb_append(CUR_HIDE);
  fb_flush();

  int cur = 0;
  int last_g = 0;

  while (1) {
    print_menu(cur);
    int key = read_key();

    if (key == 'j') {
      if (cur < MENU_TOTAL - 1)
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
      cur = MENU_TOTAL - 1;
      last_g = 0;
    } else if (key == '%') {
      cur = (cur == 0) ? MENU_TOTAL - 1 : 0;
      last_g = 0;
    } else if (key == 'l' || key == '\r' || key == '\n') {
      view_section(menu_sections_ext[cur]);
      last_g = 0;
    } else if (key >= '1' && key <= '0' + MENU_TOTAL) {
      cur = key - '1';
      view_section(menu_sections_ext[cur]);
      last_g = 0;
    } else if (key == 'q' || key == 'x') {
      fb_reset();
      fb_append(ALT_OFF);
      fb_append(CUR_SHOW);
      fb_append(CLR);
      fb_append(C_HINT "\n  bye\n\n" RESET);
      fb_flush();
      return 0;
    } else {
      last_g = 0;
    }
  }
}
