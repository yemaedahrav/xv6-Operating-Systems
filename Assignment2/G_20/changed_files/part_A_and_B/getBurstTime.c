#include "types.h"
#include "stat.h"
#include "user.h"
 
int
main(int argc,char* argv[])
{
   //int pid;
   //pid=atoi(argv[1]);
   int ans=getBurstTime();
   if(ans==-1)
   	printf(1,"Process NOT FOUND \n");
   else
   	printf(1,"Burst Time of Process is = %d\n",ans);	
  exit();
}
