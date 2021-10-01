// User program to call the system call wolfie.


#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
	char buf[3500];
	printf(1,"Number of bytes copied on calling wolfie sys call :  %d\n",wolfie((void*) buf,3500));
	printf(1,"%s",buf);
	exit();
}
