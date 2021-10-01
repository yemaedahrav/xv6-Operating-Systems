#include "types.h"
#include "stat.h"
#include "user.h"



int
main(int argc, char* argv[]){
	

	for(int i=0;i<20;i++){
		if(!fork()){
			printf(1,"CHILD %d\n",i+1 );
			for(int j=0;j<10;j++){
				char *arr = malloc(4096);
				for(int k=0;k<4096;k++){
					arr[k] = 'a'+k%26;
				}
				int cnt=0;
				for(int k=0;k<4096;k++){
					if(arr[k] == 'a'+k%26)
						cnt++;
				}
				
				printf(1,"Number of bytes matched= %d in iteration %d\n", cnt,j+1);
				
			}
		
			
			exit();
		}
	}

	while(wait()!=-1);
	exit();

}
