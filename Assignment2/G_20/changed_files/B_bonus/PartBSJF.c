#include "types.h"
#include "stat.h"
#include "user.h"
#include "processInfo.h"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf(1, "PartBSJF <number-of-children>\n");
        exit();
    }
   
    
   
    int N = atoi(argv[1]);
    int use_this;
    
    int btarray[64]={4,20,14,2,3,5,6,2,16,6,4,13,19,19,14,14,12,14,20,13,8,20,6,17,5,20,7,20,10,6,6,14,5,20,6,20,16,3,13,11,8,17,15,18,7,9,11,10,2,3,2,2,2,20,10,18,19,16,9,20,2,7,5,18};//randomly filled array .
    printf(1, "Burst Time of parent process = %d\n", getBurstTime());

    for (int i = 0; i < N; i++)
    {
       
        int Burst_time = btarray[i];

        int return_value = fork();
        
       
        if (return_value == 0)
        {
            setBurstTime(Burst_time);
          if(i%2){
          	for(int x=0;x<100;x++){
           		sleep(1);
           	}
           	int pid=getSelfInfo();
           	printf(1,"Pid = %d Burst time = %d Type = I/O\n",pid,Burst_time);	
           	
           }
           else{
           	int x=0;
           	for(int h=0;h<10;h++){
           		for(int f=0;f<10;f++){
           			x++;
           		}
           	}
           	use_this=x;
           	int pid=getSelfInfo();
           	printf(1,"Pid = %d Burst time = %d Type = CPU\n",pid,Burst_time);	
           }
            use_this++;
            
            exit();
            
        } 
        
    }
    
    while(wait()!=-1);


    exit();
}
