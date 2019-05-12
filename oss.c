#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <string.h>
#include <sys/file.h>
#include "sharedMem.h"
#include "queue.h"


void cleanUp();
void addClock(struct time* time, int sec, int ns);
void ossExit(int sig);
int getProcessId();
int allocateResource(int resourceId,int pid);
void attemptResourceGrab(int pid,int verbose);
void startOSS(int verbose);
void deallocateResource(int resourceId, int pid);

FILE *fp;
int lines = 0;
int toChild;
int toOSS;
int shmid;
int takenPids[18];

struct{
	long mtype;
	char msg[10];
}msgbuf;

struct sharedRes *shared;
struct Queue *waiting; 

//Statistics
int granted = 0;
int terminatedByDeadlock = 0;
int terminatedNormal = 0;
int deadlockRun = 0;		
int printTable = 1;

int main(int argc, char *argv[]){
	int v;
	int c;	
	while((c=getopt(argc, argv, "v:h"))!= EOF){
		switch(c){
			case 'h':
				printf("-v: Set v to 1 for verbose on, or to 0 for verbose off.\n");
				exit(0);
				break;
			case 'v':
				v = atoi(optarg);
				if(v == 1){
					printf("Verbose is on for this simulation\n");
				}else if(v == 0){
					printf("Verbose is off for this simulation\n");	
				}
				if(v > 1 || v < 0){
					printf("Invalid option. Setting V to 0. See -h for more details on the v option\n");
					v = 0;
				}
				break;
		}
	}
	int verbose = v;

	
	srand(time(NULL));
	//Attach shared memory	
	key_t key;
	key = ftok(".",'a');
	if((shmid = shmget(key,sizeof(struct sharedRes), IPC_CREAT | 0666)) == -1){		
		perror("shmget");
		exit(1);	
	}
	
	shared = (struct sharedRes*)shmat(shmid,(void*)0,0);
	if(shared == (void*)-1){
		perror("Error on attaching memory");
	}

	//Attach queues for communication between processes
	key_t msgkey;
	if((msgkey = ftok("msgQueue1",925)) == -1){
		perror("ftok");
	}

	if((toChild = msgget(msgkey, 0600 | IPC_CREAT)) == -1){
		perror("msgget");	
	}	
	
	if((msgkey = ftok("msgQueue2",825)) == -1){
		perror("ftok");
	}

	if((toOSS = msgget(msgkey, 0600 | IPC_CREAT)) == -1){
		perror("msgget");	
	}

	fp = fopen("osslog.txt","w");
	//initalize instances and other descriptor structures
	int i, j, index;
	for(i = 0; i < 20; i++){
		shared->resources[i].shareable = 0;
		int temp = (rand() % (10 - 1 + 1)) + 1; //[1,10]
		shared->resources[i].instances = temp;
		shared->resources[i].availableInstances = temp;
		for(j = 0; j <= 18; j++){
			shared->resources[i].request[j] = 0;
			shared->resources[i].release[j] = 0;
			shared->resources[i].allocated[j] = 0;			
		}
	}
	//Mark about 20% of our resource descriptors to be shareable
	int randShareable = (rand() % (5 - 1 + 1)) + 1;
	for(i = 0; i < randShareable; i++){
		//Generate random indexes to be shareable
		index = (rand() % (19 - 0 + 1 )) + 0;
		shared->resources[index].shareable = 0;
	}
	
	printf("OSS will be done momentarily\n");
	printf("Generating ouput in osslog.txt now...\n");
	
	//Setting up signals
	signal(SIGALRM, ossExit);
	alarm(2);
	signal(SIGINT, ossExit);

	startOSS(verbose);
	cleanUp(shmid, shared);
}
void startOSS(int verbose){

	int i = 0;
	struct time randFork;
	struct time deadLockCheck;
	deadLockCheck.seconds = 1;
	int lastDeadLock = 1;

	int nextFork = (rand() % (500000000 - 1000000 + 1)) + 1000000;
	addClock(&randFork,0,nextFork);

	int active = 0;
	int count = 0;
	int maxExecs = 25;
	pid_t pids[18];
	//To store procceses that are waiting on a resource
	waiting = createQueue(100);
	pid_t child;
	while(1){
		//increment clock
		addClock(&shared->time,0,10000);
		
		if(/*(maxExecs > 0)&&*/(active < 18) && ((shared->time.seconds > randFork.seconds) || (shared->time.seconds == randFork.seconds && shared->time.nanoseconds >= randFork.nanoseconds))){
			
			//set time for next fork
			randFork.seconds = shared->time.seconds;
			randFork.nanoseconds = shared->time.nanoseconds;
			nextFork = (rand() % (500000000 - 1000000 + 1)) + 1000000;
			addClock(&randFork,0,nextFork);
			int newProc = getProcessId() + 1;
			if((newProc-1 > -1)){
				pids[newProc - 1] = fork();
				//shared->processes[newProc].pid = newProc;
				if(pids[newProc - 1] == 0){
					char str[10];
					sprintf(str, "%d", newProc);
					execl("./user",str,NULL);
					exit(0);
				}
				maxExecs--;			
				active++;
				if(lines < 1000000){
					fprintf(fp,"OSS has created a new Process P%d at time %d:%d\n",newProc,shared->time.seconds,shared->time.nanoseconds);
					lines++;
				}
			}	
		}

		if(msgrcv(toOSS, &msgbuf, sizeof(msgbuf),0,IPC_NOWAIT) > -1){
			int pid = msgbuf.mtype;
			if (strcmp(msgbuf.msg, "TERMINATED") == 0){
				while(waitpid(pids[pid - 1],NULL, 0) > 0);
				int m;
				terminatedNormal++;
				//Reset process resource information
				for(m = 0; m < 20; m++){
					if(shared->resources[m].shareable == 0){
						if(shared->resources[m].allocated[pid -1] > 0){
							shared->resources[m].availableInstances+=
							shared->resources[m].allocated[pid - 1];
						}
					}
					shared->resources[m].allocated[pid - 1] = 0;
					shared->resources[m].request[pid - 1] = 0; 	
				}	
				active--;
				takenPids[pid - 1] = 0;		
				count++;
				if(lines < 100000){
					fprintf(fp,"OSS has detected Process P%d has terminated at time %d:%d\n",pid,shared->time.seconds,shared->time.nanoseconds);
					lines++;
				}
				
			}
			else if (strcmp(msgbuf.msg, "RELEASE") == 0){
				//Recieve resource to release
				msgrcv(toOSS, &msgbuf,sizeof(msgbuf),pid,0);
				int releasedRes = atoi(msgbuf.msg);
				if(releasedRes > -1){	
					deallocateResource(releasedRes,pid);
					if(lines < 100000 && verbose == 1){
						fprintf(fp,"OSS has detected Process P%d has released resource R%d at time %d:%d\n",pid,releasedRes,shared->time.seconds,shared->time.nanoseconds);
						lines++;
					}			
				}
			}
			else if (strcmp(msgbuf.msg, "REQUEST") == 0){
				//Retrieve resource id
				msgrcv(toOSS, &msgbuf, sizeof(msgbuf),pid,0);
				int requestedRes = atoi(msgbuf.msg);
				
				if(lines < 100000 && verbose == 1){
					fprintf(fp,"OSS has detected Process P%d requesting R%d at time %d:%d\n",pid,requestedRes,shared->time.seconds,shared->time.nanoseconds);
					lines++;
				}
			
				//Retrieve number of instances to request	
				msgrcv(toOSS, &msgbuf, sizeof(msgbuf),pid,0);
				int instances = atoi(msgbuf.msg);
				
				//Set requests			
				shared->resources[requestedRes].request[pid - 1] = instances;
			
				if(allocateResource(requestedRes, pid) == 1){
					granted++;
					if(granted % 20 == 0){
						printTable = 1;
					}
					//Tell the user request has been granted
					strcpy(msgbuf.msg,"GRANTED");
					msgbuf.mtype = pid;
					msgsnd(toChild,&msgbuf,sizeof(msgbuf),IPC_NOWAIT);
					if(lines < 100000 && verbose ==1){
						fprintf(fp,"OSS has granted Process P%d resource R%d at time %d:%d\n",pid,requestedRes,shared->time.seconds,shared->time.nanoseconds);
						lines++;
					}
				}else{
					if(lines < 100000 && verbose == 1){
						fprintf(fp,"OSS has put Process P%d in waiting status for R%d at time %d:%d\n",pid,requestedRes,shared->time.seconds,shared->time.nanoseconds);
						lines++;
					}
					//Insert pid into wait queue since resources could be granted
					enqueue(waiting,pid);
				}
			}
		} 
		
		//Check if waiting processes can be given a resource	
		int k = 0;
		if(isEmpty(waiting) == 0){
			int size = queueSize(waiting);
			while(k < size){
				int pid = dequeue(waiting);
				int requestedRes;
		
				//Find the requeted resource
				int m;
				for(m = 0; m < 20; m++){
					if(shared->resources[m].request[pid - 1] > 0){
						requestedRes = m;
					} 
				}

				if(allocateResource(requestedRes, pid) == 1){
					granted++;
					if(granted % 20 == 0){
						printTable = 1;
					}
					if(lines < 100000 && verbose == 1){
						lines++;
						fprintf(fp,"OSS has detected resource R%d given to process %d while in wait status %d:%d\n",requestedRes,pid,shared->time.seconds,shared->time.nanoseconds);
					}
					//Tell the user request has been granted
					strcpy(msgbuf.msg,"GRANTED");
					msgbuf.mtype = pid;
					msgsnd(toChild,&msgbuf,sizeof(msgbuf),IPC_NOWAIT);
				}else{
					//Insert pid into wait queue since resources could be granted
					enqueue(waiting,pid);
				}	
				k++;
			}
		}

		if(shared->time.seconds == deadLockCheck.seconds){
			deadLockCheck.seconds++;
			int i,k;
			int deadlock;
			deadlockRun++;
			if(isEmpty(waiting) == 0){
				deadlock = 1;
				if(lines < 100000){
					lines++;
					fprintf(fp,"OSS has detected deadlock at %d:%d\n",shared->time.seconds,shared->time.nanoseconds);
				}	
			}else{
				deadlock = 0;
			}
			if(deadlock == 1){
				while(1){
					deadlock = 0;	
					int pidToKill = dequeue(waiting);
					msgbuf.mtype = pidToKill;
					strcpy(msgbuf.msg,"TERM");
					msgbuf.mtype = pidToKill;
					msgsnd(toChild,&msgbuf,sizeof(msgbuf),IPC_NOWAIT);
					while(waitpid(pids[pidToKill - 1],NULL, 0) > 0);	
					terminatedByDeadlock++;

					if(lines < 100000){
						lines++;
						fprintf(fp,"	Process P%d being terminated in deadlock algoritm at time %d:%d\n",pidToKill,shared->time.seconds,shared->time.nanoseconds);
					}				
					int m;
					if(lines < 100000 && verbose == 1){
						lines++;
						fprintf(fp,"		Resources released are:\n");
						fprintf(fp,"			");
					}
					//Reset process resource information
					for(m = 0; m < 20; m++){
						if(shared->resources[m].shareable == 0){
							if(shared->resources[m].allocated[pidToKill - 1] > 0){
								shared->resources[m].availableInstances+=
								shared->resources[m].allocated[pidToKill - 1];
								if(lines < 100000 && verbose == 1){
									lines++;
									fprintf(fp,"R%d ",m);
								}							
	
							}
						}
						shared->resources[m].allocated[pidToKill - 1] = 0;
						shared->resources[m].request[pidToKill - 1] = 0; 	
					}
					if(verbose == 1){
						fprintf(fp,"\n");
					}	
					active--;
					takenPids[pidToKill - 1] = 0;		
					count++;
					k = 0;
					while(k < queueSize(waiting)){
						int temp = dequeue(waiting);
						attemptResourceGrab(temp,verbose);
						k++;
					}
					
					if(isEmpty(waiting) != 0){
						break;
					}	
				}
				if(lines < 100000 && verbose == 1){
					fprintf(fp,"System is no longer in deadlock\n");
				}
			}
		}
	
		if((granted % 20 == 0 && granted != 0) && printTable == 1 && verbose == 1){
			i = 0;
			k = 0;
			fprintf(fp,"\nHere's a look at our resource table:\n");
			for(i = 0; i < 18; i++){
				if(takenPids[i] == 1){
				fprintf(fp,"P:%d  ",i + 1);
					if(i <= 8){
						fprintf(fp," ");
					}
					for(k = 0; k < 20; k++){
						fprintf(fp,"%d ", shared->resources[k].allocated[i]);
					}
					fprintf(fp,"\n");
				}
			}
			fprintf(fp,"\n");
			lines+=18;
			printTable = 0;
		}
	}
	printf("Out of simulation\n");
	int status;
	pid_t wpid;//wait for any children that haven't quite finished yet
	while((wpid = wait(&status)) > 0);
	fprintf(fp,"Requests Granted: %d\n",granted);
	fprintf(fp,"Deadlock detection ran %d times\n",deadlockRun);
	fprintf(fp,"Total processes terminated in deadlock: %d\n", terminatedByDeadlock);
	fprintf(fp,"Total processes terminated naturally: %d\n", terminatedNormal);
	
	printf("Requests Granted: %d\n",granted);
	printf("Deadlock detection ran %d times\n",deadlockRun);
	printf("Total processes terminated in deadlock: %d\n", terminatedByDeadlock);
	printf("Total processes terminated naturally: %d\n", terminatedNormal);
	
}

void attemptResourceGrab(int pid,int verbose){
	//Find the requeted resource
	int m;
	int requestedRes;
	for(m = 0; m < 20; m++){
		if(shared->resources[m].request[pid - 1] > 0){
				requestedRes = m;
			} 
		}

		if(allocateResource(requestedRes, pid) == 1){
			if(lines < 1000000 && verbose == 1){
				lines++;
				fprintf(fp,"OSS has detected resource R%d given to process P%d at time %d:%d\n",requestedRes,pid,shared->time.seconds,shared->time.nanoseconds);
			}
			granted++;
			if(granted % 20 == 0){
				printTable = 1;
			}
			//Tell the user request has been granted
			strcpy(msgbuf.msg,"GRANTED");
			msgbuf.mtype = pid;
			msgsnd(toChild,&msgbuf,sizeof(msgbuf),IPC_NOWAIT);
		}else{
			//Insert pid into wait queue since resources couldnt be granted
			enqueue(waiting,pid);
		}	
}

void deallocateResource(int resourceId, int pid){
	//We can ignore this for shareable processes
	if(shared->resources[resourceId].shareable == 0){
		shared->resources[resourceId].availableInstances += shared->resources[resourceId].allocated[pid - 1];
	}
		
	shared->resources[resourceId].allocated[pid - 1] = 0;
}

int allocateResource(int resourceId, int pid){
	while((shared->resources[resourceId].request[pid - 1] > 0 &&
		shared->resources[resourceId].availableInstances > 0)){
		//We can ignore shareable resources	
		if(shared->resources[resourceId].shareable == 0){
			(shared->resources[resourceId].request[pid - 1])--;
			(shared->resources[resourceId].allocated[pid - 1])++;
			(shared->resources[resourceId].availableInstances)--;
		}else{
			shared->resources[resourceId].request[pid - 1] = 0;
			break;
		}
	}	
	if(shared->resources[resourceId].request[pid - 1] > 0){
		return -1;
	}else{
		return 1;
	}
}

int getProcessId(){
	int i;
	int allReleased = 0;

	for(i = 0; i < 18; i++){
		if(takenPids[i] == 0){
			takenPids[i] = 1;
			return i;
		}
	}
	return -1;
}

//Adds time to the clock structure
void addClock(struct time* time, int sec, int ns){
	time->seconds += sec;
	time->nanoseconds += ns;
	while(time->nanoseconds >= 1000000000){
		time->nanoseconds -=1000000000;
		time->seconds++;
	}
}

//Starts the clean up process for OSS
void ossExit(int sig){
	switch(sig){
		case SIGALRM:
			printf("\n2 seconds  has passed. The program will now terminate.\n");
			break;
		case SIGINT:
			printf("\nctrl+c has been registered. Now exiting.\n");
			break;
	}
	fprintf(fp,"Requests Granted: %d\n",granted);
	fprintf(fp,"Deadlock detection ran %d times\n",deadlockRun);
	fprintf(fp,"Total processes terminated in deadlock: %d\n", terminatedByDeadlock);
	fprintf(fp,"Total processes terminated naturally: %d\n", terminatedNormal);
	
	printf("Requests Granted: %d\n",granted);
	printf("Deadlock detection ran %d times\n",deadlockRun);
	printf("Total processes terminated in deadlock: %d\n", terminatedByDeadlock);
	printf("Total processes terminated naturally: %d\n", terminatedNormal);
	cleanUp();
	kill(0,SIGKILL);
}

//Remove shared structures
void cleanUp(){
	fclose(fp);
	msgctl(toOSS,IPC_RMID,NULL);
	msgctl(toChild,IPC_RMID,NULL);
	shmdt((void*)shared);	
	shmctl(shmid, IPC_RMID, NULL);
}
