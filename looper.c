#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <signal.h>
#include <string.h>

void handler(int sig)
{
	printf("\nRecieved Signal : %s\n", strsignal(sig));
	if (sig == SIGINT) { 
		printf("Terminating process\n");
		 _exit(0); 
	} 	 
	if (sig == SIGSTOP)
	{
		signal(SIGSTOP, SIG_DFL);
		signal(SIGCONT, handler); // Reinstate custom handler for SIGCONT
	}
	else if (sig == SIGCONT)
	{
		signal(SIGCONT, SIG_DFL);
		signal(SIGSTOP, handler); // Reinstate custom handler for SIGTSTP
	}
	signal(sig, SIG_DFL);
	raise(sig);
}

int main(int argc, char **argv)
{

	printf("Starting the program\n");
	signal(SIGINT, handler);
	signal(SIGSTOP, handler);
	signal(SIGCONT, handler);

	while (1)
	{
		sleep(1);
	}

	return 0;
}