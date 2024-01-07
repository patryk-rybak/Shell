#include "shell.h"

typedef struct proc {
  pid_t pid;    /* process identifier */
  int state;    /* RUNNING or STOPPED or FINISHED */
  int exitcode; /* -1 if exit status not yet received */
} proc_t;

typedef struct job {
  pid_t pgid;            /* 0 if slot is free */
  proc_t *proc;          /* array of processes running in as a job */
  struct termios tmodes; /* saved terminal modes */
  int nproc;             /* number of processes */
  int state;             /* changes when live processes have same state */
  char *command;         /* textual representation of command line */
} job_t;

static job_t *jobs = NULL;          /* array of all jobs */
static int njobmax = 1;             /* number of slots in jobs array */
static int tty_fd = -1;             /* controlling terminal file descriptor */
static struct termios shell_tmodes; /* saved shell terminal modes */

static void sigchld_handler(int sig) {
  int old_errno = errno;
  pid_t pid;
  int status;
  /* TODO: Change state (FINISHED, RUNNING, STOPPED) of processes and jobs.
   * Bury all children that finished saving their status in jobs. */
#ifdef STUDENT
  // przechodzimy po kazdym zadaniu
  for (int j = 0; j < njobmax; j++) {

    // pomijamy puste zadania
    if (!jobs[j].pgid) {
      continue;
    }

    // przechodzimy po kazdym procesie w zadaniu
    for (int p = 0; p < jobs[j].nproc; p++) {

      // pomijamy ewentualne bledy i brak zmian
      pid = jobs[j].proc[p].pid;
      if (waitpid(pid, &status, WNOHANG | WUNTRACED | WCONTINUED) <= 0) {
        continue;
      }

      // zmieniamy metadane procesu zgodnie z otrzymanym sygnalem
      if (WIFEXITED(status) || WIFSIGNALED(status)) {
        jobs[j].proc[p].state = FINISHED;
        jobs[j].proc[p].exitcode = status;
      } else if (WIFSTOPPED(status)) {
        jobs[j].proc[p].state = STOPPED;
      } else if (WIFCONTINUED(status)) {
        jobs[j].proc[p].state = RUNNING;
      }
    }

    // zmieniamy stan zadania na podstawie ostatniego procesu w tym zadaniu
    jobs[j].state = jobs[j].proc[jobs[j].nproc - 1].state;
  }
#endif /* !STUDENT */
  errno = old_errno;
}

/* When pipeline is done, its exitcode is fetched from the last process. */
static int exitcode(job_t *job) {
  return job->proc[job->nproc - 1].exitcode;
}

static int allocjob(void) {
  /* Find empty slot for background job. */
  for (int j = BG; j < njobmax; j++)
    if (jobs[j].pgid == 0)
      return j;

  /* If none found, allocate new one. */
  jobs = realloc(jobs, sizeof(job_t) * (njobmax + 1));
  memset(&jobs[njobmax], 0, sizeof(job_t));
  return njobmax++;
}

static int allocproc(int j) {
  job_t *job = &jobs[j];
  job->proc = realloc(job->proc, sizeof(proc_t) * (job->nproc + 1));
  return job->nproc++;
}

int addjob(pid_t pgid, int bg) {
  int j = bg ? allocjob() : FG;
  job_t *job = &jobs[j];
  /* Initial state of a job. */
  job->pgid = pgid;
  job->state = RUNNING;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
  job->tmodes = shell_tmodes;
  return j;
}

static void deljob(job_t *job) {
  assert(job->state == FINISHED);
  free(job->command);
  free(job->proc);
  job->pgid = 0;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
}

static void movejob(int from, int to) {
  assert(jobs[to].pgid == 0);
  memcpy(&jobs[to], &jobs[from], sizeof(job_t));
  memset(&jobs[from], 0, sizeof(job_t));
}

static void mkcommand(char **cmdp, char **argv) {
  if (*cmdp)
    strapp(cmdp, " | ");

  for (strapp(cmdp, *argv++); *argv; argv++) {
    strapp(cmdp, " ");
    strapp(cmdp, *argv);
  }
}

void addproc(int j, pid_t pid, char **argv) {
  assert(j < njobmax);
  job_t *job = &jobs[j];

  int p = allocproc(j);
  proc_t *proc = &job->proc[p];
  /* Initial state of a process. */
  proc->pid = pid;
  proc->state = RUNNING;
  proc->exitcode = -1;
  mkcommand(&job->command, argv);
}

/* Returns job's state.
 * If it's finished, delete it and return exitcode through statusp. */
static int jobstate(int j, int *statusp) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  int state = job->state;

  /* TODO: Handle case where job has finished. */
#ifdef STUDENT
  // jezeli zadanie zostalo zakonczone to zapisujemy kod wyjsca i usuwamy
  // metadane zadania
  if (state == FINISHED) {
    *statusp = exitcode(job);
    deljob(job);
  }
#endif /* !STUDENT */

  return state;
}

char *jobcmd(int j) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  return job->command;
}

/* Continues a job that has been stopped. If move to foreground was requested,
 * then move the job to foreground and start monitoring it. */
bool resumejob(int j, int bg, sigset_t *mask) {
  if (j < 0) {
    for (j = njobmax - 1; j > 0 && jobs[j].state == FINISHED; j--)
      continue;
  }

  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;

    /* TODO: Continue stopped job. Possibly move job to foreground slot. */
#ifdef STUDENT
  // informujemy o wznowieniu zadania
  msg("[%d] continue '%s'", j, jobcmd(j));
  if (!bg) {
    // oddajemy termianal grupie procesow odpowiadajacej zadaniu
    setfgpgrp(jobs[j].pgid);

    // przywracamy zmienne srodowiskowe termianala odpowiadajace zadaniu
    Tcsetattr(tty_fd, TCSADRAIN, &jobs[j].tmodes);

    // przenosimy zadanie na miejsce zadania pierwszoplanowego
    movejob(j, FG);

    // wznawiamy procesy z calej grupy pierwszoplanowej
    Kill(-jobs[FG].pgid, SIGCONT);

    // czekamy na zmiane stanu zadania zapobiegajac wyscigu
    while (jobs[FG].state != RUNNING) {
      sigsuspend(mask);
    }
    // monitorujemy zadanie
    monitorjob(mask);
  } else {

    // wznawaimy procesy z calej grupy zadania
    Kill(-jobs[j].pgid, SIGCONT);
  }
#endif /* !STUDENT */

  return true;
}

/* Kill the job by sending it a SIGTERM. */
bool killjob(int j) {
  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;
  debug("[%d] killing '%s'\n", j, jobs[j].command);

  /* TODO: I love the smell of napalm in the morning. */
#ifdef STUDENT
  // wysylamy sygnal o zakonczeniu pracy
  kill(-jobs[j].pgid, SIGTERM);
  // wysylamy sygnal o wznowiemu pracy, powodujac obudzenie uspionych procesow i
  // obsluzenie SIGTERM
  kill(-jobs[j].pgid, SIGCONT);
#endif /* !STUDENT */

  return true;
}

/* Report state of requested background jobs. Clean up finished jobs. */
void watchjobs(int which) {
  for (int j = BG; j < njobmax; j++) {
    if (jobs[j].pgid == 0)
      continue;

      /* TODO: Report job number, state, command and exit code or signal. */
#ifdef STUDENT
    // kopiujemy polecenie zadania
    char *cmd = strdup(jobcmd(j));

    // zapisujemy stan zadania i dla zakonczynych zadan usuwamy metadane
    int exitcode, state = jobstate(j, &exitcode);

    // pomijamy nieiteresujace nas stany zadan
    if (state != which && which != ALL) {
      free(cmd);
      continue;
    }

    switch (state) {
      case RUNNING:
        msg("[%d] running '%s'\n", j, cmd);
        break;
      case STOPPED:
        msg("[%d] suspended '%s'\n", j, cmd);
        break;
      case FINISHED:
        if (WIFEXITED(exitcode))
          msg("[%d] exited '%s', status=%d\n", j, cmd, WEXITSTATUS(exitcode));
        else
          msg("[%d] killed '%s' by signal %d\n", j, cmd, WTERMSIG(exitcode));
        break;
      default:
        break;
    }

    // zwalniamy polecenie zadania
    free(cmd);
#endif /* !STUDENT */
  }
}

/* Monitor job execution. If it gets stopped move it to background.
 * When a job has finished or has been stopped move shell to foreground. */
int monitorjob(sigset_t *mask) {
  int exitcode = 0, state;

  /* TODO: Following code requires use of Tcsetpgrp of tty_fd. */
#ifdef STUDENT
  // czekamy na zmiane stany zadania
  while ((state = jobstate(FG, &exitcode)) == RUNNING) {
    sigsuspend(mask);
  }

  // jezeli zostalo zatrzymane szukamy nowego meijsca dla zadania i zwalniamy
  // index zadania pierwszoplanowego
  if (state == STOPPED) {
    int job_idx = allocjob();
    movejob(FG, job_idx);
    msg("[%d] stopped %s\n", job_idx, jobcmd(job_idx));
  }

  // oddajemy kontrole nad tetrminalem z powrotem do shell-a
  setfgpgrp(getpgid(0));

  // przywracamy zmienne srodowiskowe terminala odpowiadajace shell-owi
  Tcsetattr(tty_fd, TCSADRAIN, &shell_tmodes);
#endif /* !STUDENT */

  return exitcode;
}

/* Called just at the beginning of shell's life. */
void initjobs(void) {
  struct sigaction act = {
    .sa_flags = SA_RESTART,
    .sa_handler = sigchld_handler,
  };

  /* Block SIGINT for the duration of `sigchld_handler`
   * in case `sigint_handler` does something crazy like `longjmp`. */
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, SIGINT);
  Sigaction(SIGCHLD, &act, NULL);

  jobs = calloc(sizeof(job_t), 1);

  /* Assume we're running in interactive mode, so move us to foreground.
   * Duplicate terminal fd, but do not leak it to subprocesses that execve. */
  assert(isatty(STDIN_FILENO));
  tty_fd = Dup(STDIN_FILENO);
  fcntl(tty_fd, F_SETFD, FD_CLOEXEC);

  /* Take control of the terminal. */
  Tcsetpgrp(tty_fd, getpgrp());

  /* Save default terminal attributes for the shell. */
  Tcgetattr(tty_fd, &shell_tmodes);
}

/* Called just before the shell finishes. */
void shutdownjobs(void) {
  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Kill remaining jobs and wait for them to finish. */
#ifdef STUDENT
  // przechodzimy po kazdym zadaniu
  for (int j = 0; j < njobmax; j++) {
    // powodujemy zakonczenie pracy zadania
    killjob(j);
    // czekamy na zmiane stanu zadania
    while (jobs[j].state != FINISHED) {
      sigsuspend(&mask);
    }
  }
#endif /* !STUDENT */

  watchjobs(FINISHED);

  Sigprocmask(SIG_SETMASK, &mask, NULL);

  Close(tty_fd);
}

/* Sets foreground process group to `pgid`. */
void setfgpgrp(pid_t pgid) {
  Tcsetpgrp(tty_fd, pgid);
}
