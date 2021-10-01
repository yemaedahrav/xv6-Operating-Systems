#include "types.h"
#include "stat.h"
#include "user.h"
#include "processInfo.h"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf(1, "test-case <number-of-children>\n");
        exit();
    }
    if(argc>10)
    {
    	 printf(1, "Please enter number less than 10:(\n");
        exit();
    }
    
    int mini;
    int N = atoi(argv[1]);
    
    int process_ids[N];
    int output[N];
    int parray[64]={4,20,14,1,3,5,6,2,16,6,4,13,19,19,14,14,12,14,20,13,8,20,6,17,5,20,7,20,10,6,6,14,5,20,6,20,16,3,13,11,8,17,15,18,7,9,11,10,2,3,2,2,2,20,10,18,19,16,9,20,2,7,5,18};//randomly filled array .
    printf(1, "Priority of parent process = %d\n", getBurstTime());

    for (int i = 0; i < N; i++)
    {
        // * Set process priority
        // * Change priority of children in different order
        //   and verify your implementations !!!
        int priority = parray[i];

        int return_value = fork();
        
        if (return_value > 0)
        {
            process_ids[i] = return_value;
        }
        else if (return_value == 0)
        {
            setBurstTime(priority);
           if(i%2){
           	for(int x=0;x<100;x++){
           		sleep(1);
           	}
           	
           }
            
            exit();
            
        } 
        else
        {
            printf(1, "fork error \n");
            exit();
        }
    }
    


    for (int j = 0; j < N; j++)
    {
        output[j] = wait();//this tells when a process gets finished
    }

    printf(1, "\nAll processes have been executed\n");
    for (int i = 0; i < N; i++){
    	if(i%2){
    		printf(1, "pid = %d I/O\n",process_ids[i]);
    	}
    	else
    		printf(1, "pid = %d CPU\n",process_ids[i]);
    }
     
	mini=output[0];
	for(int i=1;i<N;i++){
		if(output[i]<mini)
			mini=output[i];
	}
		
    printf(1, "\nProcesses have been executed in this order: \n");
    for (int i = 0; i < N; i++){
    	
    	
    	if((output[i]-mini)%2==1){
    		 printf(1, "pid = %d Type = I/O Burst Time = %d\n", output[i],parray[output[i]-mini]);
    	}
    	else
    		 printf(1, "pid = %d Type = CPU Burst Time = %d\n", output[i],parray[output[i]-mini]);
    }
        

    exit();
}


