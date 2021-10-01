#include "types.h"
#include "stat.h"
#include "user.h"
 
int
main(int argc,char* argv[])
{
   //int pid;
   int n;
   //pid=atoi(argv[1]);
   n=atoi(argv[1]);
   int flag=setBurstTime(n);
   int ans=getBurstTime();
   if(ans==-1)
   	printf(1,"Process NOT FOUND \n");
   else
   	printf(1,"Burst Time of Process is = %d\n",ans);
   if(flag==-1)
   	printf(1,"Requested Process Not Found :(\n");
  exit();
}
