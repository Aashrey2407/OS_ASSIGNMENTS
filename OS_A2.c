#include <stdio.h>
#include <unistd.h> 
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#define ELEVATOR_MAX_CAP 20
#define MAX_NEW_REQUESTS 30
#define ACTUAL_MAX_CAP 5

//This is the code for the second assignment in OS course for 3-1 at Bits Hyderabad
typedef struct ElevatorPassenger{
    int requestId;
    int startFloor;
    int targetFloor;
    int onElevator;
}ElevatorPassenger;
typedef struct ElevatorWiseInfo{
    int currentFloor;
    int passengerCount;
    ElevatorPassenger elevatorPassengers[ACTUAL_MAX_CAP];
}ElevatorWiseInfo;
typedef struct PassengerRequest{
    int requestId; //Unique integer representing the request
    int startFloor; //The floor the passenger is waiting on
    int requestedFloor; //The floor the passenger requests to go to
} PassengerRequest;
typedef struct MainSharedMemory{
    char authStrings[100][ELEVATOR_MAX_CAP + 1]; //The ith element is the auth string for elevator i
    char elevatorMovementInstructions[100]; //ith element tells elevator i where to move
    PassengerRequest newPassengerRequests[MAX_NEW_REQUESTS]; //New requests this turn
    int elevatorFloors[100]; //Floor each elevator is on at turn start
    int droppedPassengers[1000]; //Request IDs for each passenger to be dropped this turn
    int pickedUpPassengers[1000][2]; //Request IDs and elevator numbers for passengers to be picked up this turn
} MainSharedMemory;
//These are the messages sent by the student program to the solvers
//mtype 2 for setting target elevator and mtype 3 for guessing the authorisation string
typedef struct SolverRequest{
    long mtype; 
    int elevatorNumber;
    char authStringGuess[ELEVATOR_MAX_CAP+1];
} SolverRequest;
typedef struct SolverResponse{
    long mtype;
    int guessIsCorrect; // Is nonzero if the guess is correct
} SolverResponse;
//These are the messages the helper responds with when a new turn starts
typedef struct TurnChangeResponse{
    long mtype; //always 2
    int turnNumber; //the current turn number(starts at 2)
    int newPassengerRequestCount; //the number of new requests this turn
    int errorOccured; //if 1, an error has occured
    int finished; //if 1, the testcase is done
} TurnChangeResponse;
//These are the messages sent by the student program to the helper
typedef struct TurnChangeRequest{
    long mtype; //must be 1
    int droppedPassengersCount;
    int pickedUpPassengersCount; //number of passengers dropped from their passengers this turn
} TurnChangeRequest;
char * makeAGuess(int length){
    char * template = "abcdef";
    char * guess = (char *)malloc((length+1)*sizeof(char));
    for(int i = 0;i<length;i++){
        int index = rand() % 6;
        guess[i] = template[index];
    }
    guess[length] = '\0';
    return guess;
}
bool checkGuess(int elevatorNumber,int solverKeys[],char * guess){
    
   int solverKey = solverKeys[0];
   SolverRequest solverRequest;
   solverRequest.mtype = 2;
   solverRequest.elevatorNumber = elevatorNumber;
   int solverMsgId = msgget(solverKey,0666 | IPC_CREAT);
   if(msgsnd(solverMsgId,&solverRequest,sizeof(solverRequest)-sizeof(solverRequest.mtype),0)==-1){
        perror("Error in sending request to the solver to set elevator number");
        return 1;
   }
   solverRequest.mtype = 3;
   strcpy(solverRequest.authStringGuess,guess);
   if(msgsnd(solverMsgId,&solverRequest,sizeof(solverRequest)-sizeof(solverRequest.mtype),0)==-1){
        perror("Error in sending the guess to the solver");
        return 1;
   }
    SolverResponse solverResponse;
    solverResponse.mtype = 4;
   if(msgrcv(solverMsgId,&solverResponse,sizeof(solverResponse)-sizeof(solverResponse.mtype),4,0)==-1){
        perror("Error in receiving response from the solver");
        return 1;
   }
    return solverResponse.guessIsCorrect;
}
void handlePendingPassengers(ElevatorWiseInfo * elevators,ElevatorPassenger * pendingPassengerQueue,int * pendingQueueStart,int * pendingQueueEnd,int * pickedUpPassengersCount,MainSharedMemory * mainShmPtr,int N){
    while(*pendingQueueStart!=*pendingQueueEnd){
        int minDistance = INT32_MAX;
        int indexOfNearestElevator = -1;
        ElevatorPassenger currentPendingPassenger = pendingPassengerQueue[*pendingQueueStart];
        for(int i = 0;i<N;i++){
            int distance = abs(elevators[i].currentFloor-currentPendingPassenger.startFloor);
            if(distance<minDistance && elevators[i].passengerCount<ACTUAL_MAX_CAP){
                minDistance = distance;
                indexOfNearestElevator = i;
            }
        }
        if(indexOfNearestElevator!=-1){
            int countInNearestElevator = elevators[indexOfNearestElevator].passengerCount;
            ElevatorWiseInfo * nearestElevator = &elevators[indexOfNearestElevator];
            nearestElevator->elevatorPassengers[countInNearestElevator] = currentPendingPassenger;
            if(nearestElevator->currentFloor == currentPendingPassenger.startFloor){
                nearestElevator->elevatorPassengers[countInNearestElevator].onElevator = 1;
                mainShmPtr->pickedUpPassengers[*pickedUpPassengersCount][0] = currentPendingPassenger.requestId;
                mainShmPtr->pickedUpPassengers[*pickedUpPassengersCount][1] = indexOfNearestElevator;
                *pickedUpPassengersCount = *pickedUpPassengersCount + 1;
            }
            else{
                nearestElevator->elevatorPassengers[countInNearestElevator].onElevator = 0;
            }
            nearestElevator->passengerCount++;
        }
        else{
            break;
        }
        *pendingQueueStart = *pendingQueueStart + 1;
    }
    return;
}
void handleNewPassengers(ElevatorWiseInfo * elevators,ElevatorPassenger * pendingPassengerQueue,int * pendingQueueStart,int * pendingQueueEnd,int * pickedUpPassengersCount,MainSharedMemory * mainShmPtr,int N,int newPassengerRequestCount){
    
        for(int i = 0;i<N;i++){
            ElevatorWiseInfo * currentElevator = &elevators[i];
            for(int j = 0;j<currentElevator->passengerCount;j++){
                ElevatorPassenger * passenger = &currentElevator->elevatorPassengers[j];
                if(passenger->onElevator==0 && passenger->startFloor==currentElevator->currentFloor){
                    passenger->onElevator = 1;
                    mainShmPtr->pickedUpPassengers[*pickedUpPassengersCount][0] = passenger->requestId;
                    mainShmPtr->pickedUpPassengers[*pickedUpPassengersCount][1] = i;
                    *pickedUpPassengersCount = *pickedUpPassengersCount+1;
                }
            }
        }
    
    for(int i = 0;i<newPassengerRequestCount;i++){
        int minDistance = INT32_MAX;
        int indexOfNearestElevator = -1;
        PassengerRequest newPassengerRequest = mainShmPtr->newPassengerRequests[i];
        ElevatorPassenger newPassengerInElevator;
        newPassengerInElevator.requestId = newPassengerRequest.requestId;
        newPassengerInElevator.startFloor = newPassengerRequest.startFloor;
        newPassengerInElevator.targetFloor = newPassengerRequest.requestedFloor;
        for(int j = 0;j<N;j++){
            int distance = abs(newPassengerRequest.startFloor-elevators[j].currentFloor);
            if(distance<minDistance && elevators[j].passengerCount<ACTUAL_MAX_CAP){
                minDistance = distance;
                indexOfNearestElevator = j;
            }
        }
        if(indexOfNearestElevator!=-1){
            int countInNearestElevator = elevators[indexOfNearestElevator].passengerCount;
            ElevatorWiseInfo * nearestElevator = &elevators[indexOfNearestElevator];
            nearestElevator->elevatorPassengers[countInNearestElevator] = newPassengerInElevator;
            if(nearestElevator->currentFloor == newPassengerInElevator.startFloor){
                nearestElevator->elevatorPassengers[countInNearestElevator].onElevator = 1;
                mainShmPtr->pickedUpPassengers[*pickedUpPassengersCount][0] = newPassengerInElevator.requestId;
                mainShmPtr->pickedUpPassengers[*pickedUpPassengersCount][1] = indexOfNearestElevator;
                *pickedUpPassengersCount = *pickedUpPassengersCount + 1;
            }
            else{
                 nearestElevator->elevatorPassengers[countInNearestElevator].onElevator = 0;
            }
            nearestElevator->passengerCount++;
        }
        else{
            pendingPassengerQueue[*pendingQueueEnd].requestId = newPassengerRequest.requestId;
            pendingPassengerQueue[*pendingQueueEnd].startFloor = newPassengerRequest.startFloor;
            pendingPassengerQueue[*pendingQueueEnd].targetFloor = newPassengerRequest.requestedFloor;
            pendingPassengerQueue[*pendingQueueEnd].onElevator = -1;
            *pendingQueueEnd = *pendingQueueEnd+1;
        }
    }
    return;
}
void moveTheElevators(ElevatorWiseInfo * elevators,MainSharedMemory * mainShmPtr,int N){
    for(int i = 0;i<N;i++){
        ElevatorWiseInfo currentElevator = elevators[i];
        int numberOfPassengers = currentElevator.passengerCount;
        int currentFloor = currentElevator.currentFloor;
        int minDistance =  INT32_MAX;
        int targetFloor = -1;
        for(int j = 0;j<numberOfPassengers;j++){
            ElevatorPassenger currentPassenger = currentElevator.elevatorPassengers[j];
            int distance;
            if(currentPassenger.onElevator==1){
                distance = abs(currentPassenger.targetFloor-currentElevator.currentFloor);
                if(distance<minDistance){
                    minDistance = distance;
                    targetFloor = currentPassenger.targetFloor;
                }
            }
            else if(currentPassenger.onElevator==0){
                distance = abs(currentPassenger.startFloor-currentElevator.currentFloor);
                if(distance<minDistance){
                    minDistance = distance;
                    targetFloor = currentPassenger.startFloor;
                }
            }
        }
        if(targetFloor==-1){
            mainShmPtr->elevatorMovementInstructions[i] = 's';
        }
        else if(targetFloor>currentElevator.currentFloor){
            mainShmPtr->elevatorMovementInstructions[i] = 'u';
        }
        else if(targetFloor<currentElevator.currentFloor){
            mainShmPtr->elevatorMovementInstructions[i] = 'd';
        }
    }
    return;
}
int main()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_sec*1000 + tv.tv_usec);
    FILE * inputFile = fopen("input.txt","r");
    if(inputFile==NULL){
        perror("Error in opening the input file\n");
        return 1;
    }
    int N,K,M,T;
    //N is number of elevators,K is number of floors,M is number of solvers,T is turn number of last request
    key_t shmKey,mainMsgQueueKey;
    fscanf(inputFile,"%d\n%d\n%d\n%d\n%d\n%d",&N,&K,&M,&T,&shmKey,&mainMsgQueueKey);
    int solverKeys[M];
    for(int i = 0;i<M;i++){
        fscanf(inputFile,"%d",&solverKeys[i]);
    }
    // printf("%d %d %d %d %d %d\n",N,K,M,T,shmKey,mainMsgQueueKey);
    // for(int i = 0;i<M;i++){
    //     printf("%d ",solverKeys[i]);
    // }
    // printf("\n");

    //Connect to the main shared memory using the following statements
    MainSharedMemory * mainShmPtr;
    int shmId;
    size_t size = sizeof(MainSharedMemory);
    shmId = shmget(shmKey,size,0666);
    mainShmPtr = shmat(shmId,NULL,0);

    ElevatorWiseInfo elevators[100];
    for(int i = 0;i<N;i++){
        elevators[i].currentFloor = mainShmPtr->elevatorFloors[i];
        elevators[i].passengerCount = 0;
    }

    ElevatorPassenger pendingPassengers[3000];
    int pendingQueueStart = 0;
    int pendingQueueEnd = 0;

    while(true){
        TurnChangeResponse helperResponse;
        int msgId = msgget(mainMsgQueueKey,0666 | IPC_CREAT);
        if(msgrcv(msgId,&helperResponse,sizeof(helperResponse)-sizeof(helperResponse.mtype),2,0)==-1){
            perror("Error in receiving the response from helper process");
            return 1;
        }
        if(helperResponse.finished){
            break;
        }
        
        int newPassengerRequestCount = helperResponse.newPassengerRequestCount;
        int droppedPassengersCount = 0;
        int pickedUpPassengersCount = 0;
        //Making guesses for the elevators and setting the auth strings in main memory
        for(int i = 0;i<N;i++){
            int count = 0;
            for(int j = 0;j<elevators[i].passengerCount;j++){
                    if(elevators[i].elevatorPassengers[j].onElevator==1){
                            count++;
                    }
            }
            while(true){
                    if(count){
                    char * guess = makeAGuess(count);
                        // printf("%s\n",guess);
                        // printf("%d\n",count);
                        printf("%d\n",helperResponse.turnNumber);
                        if(checkGuess(i,solverKeys,guess)){
                            printf("Guessed correctly\n");
                            strcpy(mainShmPtr->authStrings[i],guess);
                            break;
                        }
                        free(guess);
                    }
                    else{
                        break;
                    }
            }
        //Logic for dropping passengers
            elevators[i].currentFloor = mainShmPtr->elevatorFloors[i];
            for(int j = 0;j<elevators[i].passengerCount;j++){
                if(elevators[i].elevatorPassengers[j].targetFloor==mainShmPtr->elevatorFloors[i]&&elevators[i].elevatorPassengers[j].onElevator==1){
                    mainShmPtr->droppedPassengers[droppedPassengersCount] = elevators[i].elevatorPassengers[j].requestId;
                    droppedPassengersCount++;
                    for(int k = j;k<elevators[i].passengerCount-1;k++){
                        elevators[i].elevatorPassengers[k] = elevators[i].elevatorPassengers[k+1];
                    }
                    elevators[i].passengerCount--;
                    j--;
                }
            }
        }

        //Assign the pending passengers to the lifts
        handlePendingPassengers(elevators,pendingPassengers,&pendingQueueStart,&pendingQueueEnd,&pickedUpPassengersCount,mainShmPtr,N);

        //Assign the new passengers to the lifts now
        handleNewPassengers(elevators,pendingPassengers,&pendingQueueStart,&pendingQueueEnd,&pickedUpPassengersCount,mainShmPtr,N,newPassengerRequestCount);

        // for(int i = 0;i<N;i++){
        //     mainShmPtr->elevatorMovementInstructions[i] = 'u';
        // }

        //Function to move the elevators accordingly
        moveTheElevators(elevators,mainShmPtr,N);

        TurnChangeRequest turnChangeRequest;
        turnChangeRequest.mtype = 1;
        turnChangeRequest.droppedPassengersCount = droppedPassengersCount;
        turnChangeRequest.pickedUpPassengersCount = pickedUpPassengersCount;
        int turnChangeMsgId = msgget(mainMsgQueueKey,0666 | IPC_CREAT);
        if(msgsnd(turnChangeMsgId,&turnChangeRequest,sizeof(turnChangeRequest)-sizeof(turnChangeRequest.mtype),0)==-1){
            perror("Error in sending turn change request to the helper process");
            return 1;
        }
    }
    if(shmdt(mainShmPtr)==-1){
        perror("Shared memory detach failed");
        return 1;
    }
}