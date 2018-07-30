#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h> //for pid

volatile sig_atomic_t done = 0;

void term(int signum)
{
	done = 1;
    printf("In %s: Received signal: 0x%x\n" , __FUNCTION__ , signum ) ;
}

int main(int argc, char *argv[])
{
	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = term;
	sigaction(SIGTERM, &action, NULL);

    struct sigaction action1;
	memset(&action1, 0, sizeof(struct sigaction));
	action1.sa_handler = term;
	sigaction(SIGINT, &action1, NULL);

    pid_t curr_pid = getpid() ;

    printf("Process ID: %d\n", curr_pid) ;

	int loop = 0;
	while (!done)
	{
		int t = sleep(3);
		/* sleep returns the number of seconds left if
		 * interrupted */
		while (t > 0)
		{
			printf("Loop run was interrupted with %d "
			       "sec to go, finishing...\n", t);
			t = sleep(t);
		}
		printf("Finished loop run %d.\n", loop++);
	}

	printf("done.\n");
	return 0;
}