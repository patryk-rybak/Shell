#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define DEBUG 0
#include "shell.h"

sigset_t sigchld_mask;

static void sigint_handler(int sig) {
  /* No-op handler, we just need break read() call with EINTR. */
  (void)sig;
}

/* Rewrite closed file descriptors to -1,
 * to make sure we don't attempt do close them twice. */
static void MaybeClose(int *fdp) {
  if (*fdp < 0)
    return;
  Close(*fdp);
  *fdp = -1;
}

/* Consume all tokens related to redirection operators.
 * Put opened file descriptors into inputp & output respectively. */
static int do_redir(token_t *token, int ntokens, int *inputp, int *outputp) {
  token_t mode = NULL; /* T_INPUT, T_OUTPUT or NULL */
  int n = 0;           /* number of tokens after redirections are removed */

  for (int i = 0; i < ntokens; i++) {
    /* TODO: Handle tokens and open files as requested. */
#ifdef STUDENT

    // if ze wzgledu na flage -Werror
    if (mode)
      ;

    // znajdujemy token przekierowania wejscia
    if (token[i] == T_INPUT) {
      // zamykamy aktualne wejscie
      MaybeClose(inputp);
      // otwieramy nowe na powstawie kolejnego tokena
      *inputp = Open(token[i + 1], O_RDONLY, S_IRWXU);
      // zerujemy wykorzystane tokeny
      token[i] = T_NULL;
      token[i + 1] = T_NULL;
      // znajdujemy token przekierowania wyjscia
    } else if (token[i] == T_OUTPUT) {
      // zamykamy aktualne wyjscie
      MaybeClose(outputp);
      // otwieramy nowe wyjscie na podstawie kolejnego tokane
      *outputp = Open(token[i + 1], O_WRONLY | O_CREAT | O_APPEND, S_IRWXU);
      // zerujemy wykorzystane tokeny
      token[i] = T_NULL;
      token[i + 1] = T_NULL;
      // pomijamy wykorzystane tokeny
    } else if (token[i] == T_NULL) {
      continue;
      // zliczamy niewykorzystane tokeny
    } else {
      n++;
    }
#endif /* !STUDENT */
  }

  token[n] = NULL;
  return n;
}

/* Execute internal command within shell's process or execute external command
 * in a subprocess. External command can be run in the background. */
static int do_job(token_t *token, int ntokens, bool bg) {
  int input = -1, output = -1;
  int exitcode = 0;

  ntokens = do_redir(token, ntokens, &input, &output);

  if (!bg) {
    if ((exitcode = builtin_command(token)) >= 0)
      return exitcode;
  }

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start a subprocess, create a job and monitor it. */
#ifdef STUDENT
  // tworzymy nowy proces
  pid_t pid = Fork();

  // z poziomu procesu i shell-a usawiamy nowy proces jako lidera swojej wlasnej
  // grupy procesow
  setpgid(pid, pid);

  // jezeli nowy proces
  if (!pid) {

    // jezeli zadanie pierszoplanowe oddajemy terminal grupie tego zadania
    if (!bg) {
      setfgpgrp(getpid());
    }
    // ustawiamy domyslna obsluge sygnalow
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);

    // jezeli do_redir zarejestrowal przekierowanie strumieni wejscia/wyjscia
    // kopiujemy deskryptory na odpowiednie miejsca i zamykamy te wykorzystane
    if (input != -1) {
      dup2(input, STDIN_FILENO);
      MaybeClose(&input);
    }
    if (output != -1) {
      dup2(output, STDOUT_FILENO);
      MaybeClose(&output);
    }

    // przed wykonaniem polecenia przywracamy standarowa maske sygnalow
    // zapobiegajac blokady sygnalu SIGCHLD
    Sigprocmask(SIG_SETMASK, &mask, NULL);
    // wykonujemy polecenie
    external_command(token);
  }
  // jezeli shell

  int j;
  // tworzymy nowe zadanie i dodajemy do niego nowy proces
  addproc(j = addjob(pid, bg), pid, token);

  // zamykamy niepotrzebne deskryptory
  MaybeClose(&input);
  MaybeClose(&output);

  // jezeli zadanie pierwszoplanowe oddajemy terminal grupie nowopowstalego
  // procesu i monitorujemy zadanie
  if (!bg) {
    setfgpgrp(pid);
    exitcode = monitorjob(&mask);
  } // w p.p. informujemy o wykonywaniu zadania
  else {
    msg("[%d] running '%s'\n", j, jobcmd(j));
  }
#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

/* Start internal or external command in a subprocess that belongs to pipeline.
 * All subprocesses in pipeline must belong to the same process group. */
static pid_t do_stage(pid_t pgid, sigset_t *mask, int input, int output,
                      token_t *token, int ntokens, bool bg) {
  ntokens = do_redir(token, ntokens, &input, &output);

  if (ntokens == 0)
    app_error("ERROR: Command line is not well formed!");

  /* TODO: Start a subprocess and make sure it's moved to a process group. */
  pid_t pid = Fork();
#ifdef STUDENT
  // jezeli pgid zadania nie zostal jeszcze ustalony proces staje sie liderem
  // grupy procesow zdania, w p.p. ustawiamy grupe procesu na istniejaca
  setpgid(pid, pgid);

  // jezeli nowy proces
  if (!pid) {
    // jezeli zadanie pierszoplanowe oddajemy terminal grupie tego zadania
    if (!bg) {
      setfgpgrp((!pgid) ? getpid() : pgid);
    }
    // ustawiamy domyslna obsluge sygnalow
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);

    // jezeli do_redir zarejestrowal przekierowanie strumieni wejscia/wyjscia
    // kopiujemy deskryptory na odpowiednie miejsca i zamykamy te wykorzystane
    if (input != -1) {
      dup2(input, STDIN_FILENO);
      MaybeClose(&input);
    }
    if (output != -1) {
      dup2(output, STDOUT_FILENO);
      MaybeClose(&output);
    }

    // przed wykonaniem polecenia przywracamy standarowa maske sygnalow
    // zapobiegajac blokady sygnalu SIGCHLD
    Sigprocmask(SIG_SETMASK, mask, NULL);

    // sprawdzamy czy polecenie nie jest wbudowana komenda
    int exitcode = 0;
    if ((exitcode = builtin_command(token)) >= 0) {
      return exitcode;
    }

    // wykonujemy polecenie
    external_command(token);
  }

  // jezeli shell

  // jezeli zadanie pierwszoplanowe oddajemy terminal grupie procesow tego
  // zadania
  if (!bg) {
    setfgpgrp((!pgid) ? pid : pgid);
  }
#endif /* !STUDENT */

  return pid;
}

static void mkpipe(int *readp, int *writep) {
  int fds[2];
  Pipe(fds);
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  *readp = fds[0];
  *writep = fds[1];
}

/* Pipeline execution creates a multiprocess job. Both internal and external
 * commands are executed in subprocesses. */
static int do_pipeline(token_t *token, int ntokens, bool bg) {
  pid_t pid, pgid = 0;
  int job = -1;
  int exitcode = 0;

  int input = -1, output = -1, next_input = -1;

  mkpipe(&next_input, &output);

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start pipeline subprocesses, create a job and monitor it.
   * Remember to close unused pipe ends! */
#ifdef STUDENT
  // start obslugiwanego polecenia w pipeline
  int start_token = 0;
  // koniec oblugiwanego polecenia w pipeline
  int end_token = 0;

  // dopoki poczatek polecenia nie znajdzie sie poza tablica tokenow
  while (start_token < ntokens) {

    // szukamy drugiego konca polecenia
    while (token[end_token] != T_PIPE && end_token < ntokens) {
      end_token++;
    }

    // jezeli okazalo sie ze nie moznemy znalezc drugiego konca, to oznacza, ze
    // oblugujemy ostatnie polecenie w pipeline - wychodzimy z petli
    if (end_token == ntokens) {
      break;
    }

    // jezeli nie jest to pierwsze polecenie obslugiewane w pipeline
    if (pgid) {
      // tworzymy pipe-a
      mkpipe(&next_input, &output);
    }

    // token "|" oddzielajacy obslugiwane polecenie w pipeline od przyszlych
    // oznaczamy za wykorzystany
    token[end_token] = T_NULL;

    // wykonujemy obslugiwane polecenie
    pid = do_stage(pgid, &mask, input, output, token + start_token,
                   end_token - start_token + 1, bg);

    // jezeli pgid nie zostal jeszcze ustalony (obslugijemy pierwsze polecenie)
    // ustawiamy go i tworzymy nowe zadanie
    if (!pgid) {
      pgid = pid;
      job = addjob(pgid, bg);
    }

    // dodajemy proces do zadania
    addproc(job, pid, token + start_token);

    // zamykamy niepotrzebne deskryptory
    MaybeClose(&input);
    MaybeClose(&output);

    // ustawimy strumien wejscia dla przyszlego polecenie w pipeline
    input = next_input;

    // przesuwamy start na pierwszy token po "|"
    start_token = end_token + 1;
  }
  // oblugujemy ostatnie polecenie w pipeline

  // wykonujemy obslugiwane polecenie
  pid = do_stage(pgid, &mask, input, -1, token + start_token,
                 ntokens - start_token + 1, bg);
  // dodajemy proces do zadania
  addproc(job, pid, token + start_token);
  // zamykamy niepotrzebne deskryptory
  MaybeClose(&input);
  MaybeClose(&output);

  // jezeli zadanie pierwszoplanowe monitorujemy jego stan
  if (!bg) {
    exitcode = monitorjob(&mask);
  } else { // w p.p informujemy o rozpoczeciu zadania
    msg("[%d] running '%s'", job, jobcmd(job));
  }
#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

static bool is_pipeline(token_t *token, int ntokens) {
  for (int i = 0; i < ntokens; i++)
    if (token[i] == T_PIPE)
      return true;
  return false;
}

static void eval(char *cmdline) {
  bool bg = false;
  int ntokens;
  token_t *token = tokenize(cmdline, &ntokens);

  if (ntokens > 0 && token[ntokens - 1] == T_BGJOB) {
    token[--ntokens] = NULL;
    bg = true;
  }

  if (ntokens > 0) {
    if (is_pipeline(token, ntokens)) {
      do_pipeline(token, ntokens, bg);
    } else {
      do_job(token, ntokens, bg);
    }
  }

  free(token);
}

#ifndef READLINE
static char *readline(const char *prompt) {
  static char line[MAXLINE]; /* `readline` is clearly not reentrant! */

  write(STDOUT_FILENO, prompt, strlen(prompt));

  line[0] = '\0';

  ssize_t nread = read(STDIN_FILENO, line, MAXLINE);
  if (nread < 0) {
    if (errno != EINTR)
      unix_error("Read error");
    msg("\n");
  } else if (nread == 0) {
    return NULL; /* EOF */
  } else {
    if (line[nread - 1] == '\n')
      line[nread - 1] = '\0';
  }

  return strdup(line);
}
#endif

int main(int argc, char *argv[]) {
  /* `stdin` should be attached to terminal running in canonical mode */
  if (!isatty(STDIN_FILENO))
    app_error("ERROR: Shell can run only in interactive mode!");

#ifdef READLINE
  rl_initialize();
#endif

  sigemptyset(&sigchld_mask);
  sigaddset(&sigchld_mask, SIGCHLD);

  if (getsid(0) != getpgid(0))
    Setpgid(0, 0);

  initjobs();

  struct sigaction act = {
    .sa_handler = sigint_handler,
    .sa_flags = 0, /* without SA_RESTART read() will return EINTR */
  };
  Sigaction(SIGINT, &act, NULL);

  Signal(SIGTSTP, SIG_IGN);
  Signal(SIGTTIN, SIG_IGN);
  Signal(SIGTTOU, SIG_IGN);

  while (true) {
    char *line = readline("# ");

    if (line == NULL)
      break;

    if (strlen(line)) {
#ifdef READLINE
      add_history(line);
#endif
      eval(line);
    }
    free(line);
    watchjobs(FINISHED);
  }

  msg("\n");
  shutdownjobs();

  return 0;
}
