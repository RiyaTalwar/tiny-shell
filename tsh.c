/*
 * tsh - A tiny shell program with job control
 *
 * Name: RIYA TALWAR
 * ID: 201501154
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine
 */
int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
*/
void eval(char *cmdline)
{
  sigset_t sSet;              //Declaring a variable sSet of type sigset_t
  sigemptyset(&sSet);         //Initializing sSet as empty
  sigaddset(&sSet,SIGCHLD);   //Adding the SIGCHLD signal to the signal set
  char* argv[MAXARGS];

  int bg=parseline(cmdline,argv);   //Depending on whether the job should run in background or not, bg gets the value

  if(argv[0]==0)              //no items on command line
    return;

  if(builtin_cmd(argv)==0)                        //If the command is not builtin
  {
    sigprocmask(SIG_BLOCK,&sSet,NULL);            //Block SIGCHLD
    pid_t pid;
    pid=fork();                                   //fork a child

    if(pid==0)                                    //In the child process
    {
       sigprocmask(SIG_UNBLOCK,&sSet, NULL);      //Unblock the SIGCHLD signal
       setpgid(getpid(),0);                       //Child creates a new process group with itself as the group leader
       if(execve((char*)argv[0],argv,environ)<0)  //Creates a process
       {
       printf("%s: Command not found\n",argv[0]);
       exit(0);
       }
    }
    if(bg)                                        //If a background process is created
    {
       addjob(jobs,pid,BG,cmdline);               //Add the child to the joblist, status is BG
       sigprocmask(SIG_UNBLOCK,&sSet,NULL);       //Unblock the SIGCHLD signal

       printf("[%d] (%d) %s", getjobpid(jobs,pid)->jid, getjobpid(jobs,pid)->pid,getjobpid(jobs,pid)->cmdline);
    }

    else                                          //If a foreground process is created
    {
       addjob(jobs,pid,FG,cmdline);               //Add the child to the joblist, status is BG
       sigprocmask(SIG_UNBLOCK,&sSet,NULL);       //Unblock the SIGCHLD signal
       waitfg(pid);                               //Shell waits for the child
    }
  }
  return ;
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 */
int parseline(const char *cmdline, char **argv)
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;

    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 */
int builtin_cmd(char **argv)
{
  if(strcmp(argv[0],"quit")==0)      //If built-in command entered is quit
  {
      int i;
			for(i = 0;i<MAXJOBS;i++)
			{
				if(jobs[i].state == ST)     //If there are stopped jobs
					{
						printf("There are stopped jobs.\n");
						listjobs(jobs);
						return 1;
					}
			}
        exit(0);
    }

  else if(strcmp(argv[0],"jobs")==0)      //If the built-in command entered is jobs
  {
    listjobs(jobs);                      //List the running and stopped background jobs.
    return 1;
  }

  else if(strcmp(argv[0],"bg")==0)      //If the built-in command enterd is bg
  {
      do_bgfg(argv);                    //Execute do_bgfg
      return 1;
  }

  else if(strcmp(argv[0],"fg")==0)      //If the built-in command entered is fg
  {
       do_bgfg(argv);                   //Execute do_bgfg
       return 1;
  }

    return 0;     /* not a builtin command */
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv)
{
  int jid;
  pid_t pid;
  struct job_t *jobid;
  if(argv[1]==NULL)                   //If argument argv[1] is missing
  {
    printf("%s command requires PID or %cjobid argument\n",argv[0],'%');
    return;
  }
  else
  {
  if(argv[1][0]=='%')
  {
    if(isdigit(argv[1][1])==0)        //checking for valid argument type
    {
      printf("%s: argument must be a PID or %cjobid\n",argv[0],'%');
      return;
    }

		jid = atoi(&argv[1][1]);

    if((jobid=getjobjid(jobs,jid))==0)    //checking if the job exists
    {
        printf("%c%d: No such job\n",'%',jid);
        return;
    }

  }

  else
  {
    if(isdigit(argv[1][1])==0)    //checking for valid argument type
    {
      printf("%s: argument must be a PID or %cjobid\n",argv[0],'%');
      return;
    }

    pid= atoi(argv[1]);

    if((jobid=getjobpid(jobs,pid))==0)  //checking if the process exisis
    {
        printf("(%d): No such process\n",pid);
        return;
    }

  }

  if((jobid->state==ST)&&(strcmp( argv[0],"bg" )==0))         //If the state is ST and command is bg
  {
    jobid->state=BG;                                          //Change the state to FG
    kill(-(jobid->pid),SIGCONT);                              //Sending SIGCONT to process group
    printf("[%d] (%d) %s",jobid->jid,jobid->pid,jobid->cmdline);
  }

  else if((jobid->state==ST)&&(strcmp( argv[0],"fg" )==0))    //If the state is ST and command is fg
  {
    jobid->state=FG;                                          //Change the state to FG
    kill(-(jobid->pid),SIGCONT);                              //Sending SIGCONT to process group
    waitfg(jobid->pid );                                      //wait
  }

  else if((jobid->state==BG)&&(strcmp( argv[0],"fg" )==0))    //If the state is BG and command is fg
  {
    jobid->state=FG;                                          //Change state to FG
    waitfg(jobid->pid );                                      //and wait
  }
}
    return;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    int i;                              //Declaring variable i

    for(i=0;i<MAXJOBS;i++)              //Get a pointer to the entry in the job table for the pid passed to waitfg = pid
    {
      if(jobs[i].pid==pid)
        break;
    }

    while(jobs[i].state==FG)            //loop continues till the state of this specific entry is FG
    {
        sleep(1);                       //sleep for 1 sec
    }

    return;                             //state is no longer FG so return
}

/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
void sigchld_handler(int sig)
{

    struct job_t *job;
    int status;
    pid_t pid=waitpid(-1,&status, WNOHANG|WUNTRACED); //waitpid will return the pid of the child that gets stored in pid

    while(pid>0)
    {
        if(WIFEXITED(status))               //If the child had terminated properly
        {
          deletejob(jobs,pid);              //Delete the job from the job table
          return;
        }

        else if(WIFSIGNALED(status))       //If the child was terminated by a signal
        {
          job = getjobpid(jobs,pid);
          //The following message gets printed that includes the signal number that caused the termination
          printf("Job [%d] (%d) terminated by signal %d\n",job->jid,jobs->pid,WTERMSIG(status));
          deletejob(jobs,pid);             //Delete the job from the job table
          return;
        }

        else if(WIFSTOPPED(status))       //If the process was stopped
        {
          job = getjobpid(jobs,pid);
          //The following message gets printed that includes the signal number that stopped the process
          printf("Job [%d] (%d) stopped by signal %d\n", job->jid,job->pid,WSTOPSIG(status));
          job->state = ST;                //Changing the state to ST
          return;
        }

    }
    return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
    if(fgpid(jobs)>0)             //If there is a foreground job
    {
      kill(-fgpid(jobs),SIGINT);  //Sending signal to the process group of the foreground job
      return;
    }

    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
    if(fgpid(jobs)>0)           //If there is a foreground job
    {
      kill(-fgpid(jobs),sig);   //Sending signal to the process group of the foreground job
      return;
    }

    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid)
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid)
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG:
		    printf("Running ");
		    break;
		case FG:
		    printf("Foreground ");
		    break;
		case ST:
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ",
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void)
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler)
{
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig)
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}
