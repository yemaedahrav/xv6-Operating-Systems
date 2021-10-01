#include "types.h"
#include "stat.h"
#include "user.h"
#include "processInfo.h" 
int
main(int argc,char* argv[])
{
   struct processInfo pinfo;
   int gpid=atoi(argv[1]);
   
   int ifFound=getProcInfo(gpid,&pinfo);
   if(ifFound==-1)
   	printf(1,"No process with given pid found :(\n");
   else{
   	printf(1,"The ID of the parent process is = %d\n",pinfo.ppid);
	printf(1,"The size of the process in bytes is = %d\n", pinfo.psize);
	printf(1,"The number of context switches for given process = %d\n",pinfo.numberContextSwitches);
   }	
  //printf(1, "Number of active processes = %d\n",getProcInfo());
  exit();
}
