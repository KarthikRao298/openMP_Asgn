/*
 * File Name : dynamic_sched.cpp
 *
 * To compile :
 * g++ dynamic_sched.cpp -o dynamic_sched -Wall
 *
 * Sample command line execution :
 * ./dynamic_sched FunctionID LowerBound UpperBound NoOfPoints Intensity NoOfThreads SyncMethod Granularity
 * ./dynamic_sched 1 0 10 10 10 2 iteration 2
 * ./dynamic_sched 1 0 10 10 10 2 thread 2
 * ./dynamic_sched 1 0 10 10 10 2 chunk 2
 *
 */

/* Debug prints will be enabled if set to 1 */
#define DEBUG 0

#include <iostream>
#include <stdio.h>
#include <cmath>
#include <string.h>
#include <cstdlib>
#include <chrono>

#include "CommonHeader.h"

#ifdef __cplusplus
extern "C" {
#endif

float f1(float x, int intensity);
float f2(float x, int intensity);
float f3(float x, int intensity);
float f4(float x, int intensity);

#ifdef __cplusplus
}
#endif

#define LEN_SYNC_METHOD 10 /* character count in 'iteration'*/

typedef float (*Func) (float, int);

typedef struct
{
    int ThreadNo;
    /* starting value of the range of indices a thread is supposed to execute */
    int StartIndex;
    /* stopping value of the range of indices a thread is supposed to execute */
    int StopIndex;
    int Intensity;
    /*   !!!!  should this be long ?   */
    int NoOfPoints;
    float LowerBound, UpperBound;
    int Granularity;
    float * IntegralOutput;
    /* stores the max value of the index upto which the integral has been computed */
    int * CompletedIndex;
    /* function pointer to one of the following functions : f1, f2, f3, f4 */
    Func FuncToIntegrate;

} ThreadData;
/*Reference to thread private structure */
typedef ThreadData * RefThreadData;

/* function to perform the integration in thread method */
static void * ThreadMethodFunc (void * inArg);
/* function to perform the integration in iterative method */
static void * IterativeMethodFunc (void * inArg);
/* function to perform the integration in chunk method */
static void * ChunkMethodFunc (void * inArg);
/* function pointer to switch between iterative, thread & chunk sync methods */
typedef void * (*SyncMethodFunc) (void *);

/* function to check if all the iterations are complete */
bool IsLoopDone (void * inArg);
/* function to get the next loop iteration values */
int GetNextLoop (void * inArg);

pthread_mutex_t IntegralMutex;
pthread_mutex_t LoopMutex;

/*==============================================================================
 *  main
 *=============================================================================*/

int main (int argc, char* argv[]) {

    DLOG (C_VERBOSE, "Enter\n");

    if (argc < 8 ) {
        std::cerr<<"usage: "<<argv[0]<<" <functionID> <a> <b> <n> <Intensity> <nbthreads <SyncMethod> <granularity>"<<std::endl;
        return -1;
    }

    std::chrono::time_point<std::chrono::system_clock> StartTime = std::chrono::system_clock::now();
    std::chrono::time_point<std::chrono::system_clock>  EndTime;
    std::chrono::duration<double> ElapsedTime;

    int i;
    int FunctionID, NoOfPoints, NoOfThreads, Intensity, Granularity, err;
    float IntegralOutput = 0 , LowerBound, UpperBound;
    char SyncMethod[LEN_SYNC_METHOD];
    int CompletedIndex = 0;

    FunctionID  = atoi (argv[1]);
    LowerBound  = atof (argv[2]);
    UpperBound  = atof (argv[3]);
    NoOfPoints  = atoi (argv[4]);
    Intensity   = atoi (argv[5]);
    NoOfThreads = atoi (argv[6]);
    memset (SyncMethod, 0, sizeof(SyncMethod));
    strcpy (SyncMethod, argv[7]);
    Granularity = atoi (argv[8]);

    ThreadData ThreadInfo[NoOfThreads];
    Func FuncToIntegrate;
    SyncMethodFunc SyncFunc;
    
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_t ThreadId[NoOfThreads];
    pthread_mutex_init (&IntegralMutex,NULL);
    pthread_mutex_init (&LoopMutex,NULL);

    DLOG (C_VERBOSE, "The FunctionID = %d\n", FunctionID);
    DLOG (C_VERBOSE, "The LowerBound = %f\n", LowerBound);
    DLOG (C_VERBOSE, "The UpperBound = %f\n", UpperBound);
    DLOG (C_VERBOSE, "The NoOfPoints = %d\n", NoOfPoints);
    DLOG (C_VERBOSE, "The Intensity = %d\n", Intensity);
    DLOG (C_VERBOSE, "The NoOfThreads = %d\n", NoOfThreads);
    DLOG (C_VERBOSE, "The SyncMethod = %s\n", SyncMethod);
    DLOG (C_VERBOSE, "The Granularity = %d\n", Granularity);

    /* based on the input argument, select suitable function to integrate */
    switch (FunctionID)
    {
        case 1:FuncToIntegrate = f1;
               break;
        case 2:FuncToIntegrate = f2;
               break;
        case 3:FuncToIntegrate = f3;
               break;
        case 4:FuncToIntegrate = f4;
               break;

        default: 
               DLOG(C_ERROR, "Invalid function input for integration\n");
               goto Exit;
    }

    /* check whether sync method is 'iterative' or 'thread' and choose function
     * accordingly
     */
    
    if (!(strcmp (SyncMethod,"thread")))
    {
        SyncFunc = ThreadMethodFunc;
    }
    else if (!(strcmp (SyncMethod,"iteration")))
    {
        SyncFunc = IterativeMethodFunc;
    }
    else if (!(strcmp (SyncMethod,"chunk")))
    {
        SyncFunc = ChunkMethodFunc;
    }
    else 
    {
        DLOG(C_ERROR, "Invalid sync method entered\n");
        goto Exit;
    }
    /* Create the required no of threads and pass required info to each thread */
    for (int i = 0; i < NoOfThreads; i++) {

        ThreadInfo[i].ThreadNo = i;
        ThreadInfo[i].Intensity = Intensity;
        ThreadInfo[i].NoOfPoints = NoOfPoints;
        ThreadInfo[i].LowerBound = LowerBound;
        ThreadInfo[i].UpperBound = UpperBound;
        ThreadInfo[i].IntegralOutput = & IntegralOutput;
        ThreadInfo[i].FuncToIntegrate = FuncToIntegrate;
        ThreadInfo[i].CompletedIndex = & CompletedIndex;
        ThreadInfo[i].Granularity = Granularity;

        DLOG (C_VERBOSE, " i = %d\n", i);
        DLOG (C_VERBOSE, "ThreadInfo = %p \n", &(ThreadInfo[i]));

        err = pthread_create (&(ThreadId[i]), &attr, SyncFunc, (void *)&(ThreadInfo[i]));
        CHK_SUCCESS_STR (err, Exit, "pthread create failed\n");

    }

    /* Now that the threads have been created wait for each thread to complete
     * its operation and then join */
    for (i = NoOfThreads-1; i >=0; i--) {

        err = pthread_join(ThreadId[i], NULL);
        CHK_SUCCESS_NOEXIT (err);
        DLOG (C_VERBOSE, "Joined ThreadId[%d]\n",i);
    }

    /* compute the time taken for the integration and display the same */
    EndTime = std::chrono::system_clock::now();
    ElapsedTime = EndTime - StartTime;

    std::cout<<IntegralOutput<<std::endl;
    std::cerr<<ElapsedTime.count()<<std::endl;

Exit:
    
    pthread_mutex_destroy (&IntegralMutex);
    pthread_mutex_destroy (&LoopMutex);
    DLOG (C_VERBOSE, "Exit\n");

    return 0;
}

/*==============================================================================
 *  ThreadMethodFunc
 *=============================================================================*/

static void * ThreadMethodFunc (void * inArg){

    RefThreadData ThreadInfo = (RefThreadData)inArg;
    float x, y, FunOutput, IntegralOutput = 0;

    while (!IsLoopDone(ThreadInfo)) {
        GetNextLoop (ThreadInfo);

        DLOG (C_VERBOSE, "Thread[%d] StartIndex = %d\n", ThreadInfo->ThreadNo, ThreadInfo->StartIndex);
        DLOG (C_VERBOSE, "Thread[%d] StopIndex = %d\n", ThreadInfo->ThreadNo, ThreadInfo->StopIndex);

        /*  y = (a - b)/n */
        y = (ThreadInfo->UpperBound - ThreadInfo->LowerBound)/ThreadInfo->NoOfPoints;

        for (int i=ThreadInfo->StartIndex; i< ThreadInfo->StopIndex; i++) {
            x = (ThreadInfo->LowerBound + ((i + 0.5)* y ));
            FunOutput = ThreadInfo->FuncToIntegrate (x,ThreadInfo->Intensity );
            FunOutput = FunOutput * y ;
            IntegralOutput = IntegralOutput + FunOutput;
        }
    }
    pthread_mutex_lock (&IntegralMutex);
    *(ThreadInfo->IntegralOutput) = *(ThreadInfo->IntegralOutput) + IntegralOutput;
    pthread_mutex_unlock (&IntegralMutex);

    DLOG (C_VERBOSE, "Thread[%d] completed its operation and is exiting."
            "Thread IntegralOutput = %f\n", ThreadInfo->ThreadNo, IntegralOutput);

    return NULL;
}

/*==============================================================================
 *  IterativeMethodFunc
 *=============================================================================*/

static void * IterativeMethodFunc (void * inArg){

    RefThreadData ThreadInfo = (RefThreadData)inArg;
    float x, y, FunOutput;

    while (!IsLoopDone(ThreadInfo)) {
        GetNextLoop (ThreadInfo);

        DLOG (C_VERBOSE, "Thread[%d] StartIndex = %d\n", ThreadInfo->ThreadNo, ThreadInfo->StartIndex);
        DLOG (C_VERBOSE, "Thread[%d] StopIndex = %d\n", ThreadInfo->ThreadNo, ThreadInfo->StopIndex);

        /*  y = (a - b)/n */
        y = (ThreadInfo->UpperBound - ThreadInfo->LowerBound)/ThreadInfo->NoOfPoints;

        for (int i=ThreadInfo->StartIndex; i< ThreadInfo->StopIndex; i++) {
            x = (ThreadInfo->LowerBound + ((i + 0.5)* y ));
            FunOutput = ThreadInfo->FuncToIntegrate (x,ThreadInfo->Intensity );
            FunOutput = FunOutput * y ;

            pthread_mutex_lock (& (IntegralMutex));
            *(ThreadInfo->IntegralOutput) = *(ThreadInfo->IntegralOutput) + FunOutput;
            pthread_mutex_unlock (& (IntegralMutex));
        }
    }

    DLOG (C_VERBOSE, "Thread[%d] completed its operation and is exiting\n", ThreadInfo->ThreadNo);
    return NULL;
}

/*==============================================================================
 *  ChunkMethodFunc
 *=============================================================================*/

static void * ChunkMethodFunc (void * inArg){

    RefThreadData ThreadInfo = (RefThreadData)inArg;
    float x, y, FunOutput, IntegralOutput;

    while (!IsLoopDone(ThreadInfo)) {
        GetNextLoop (ThreadInfo);

        IntegralOutput = 0;

        DLOG (C_VERBOSE, "Thread[%d] StartIndex = %d\n", ThreadInfo->ThreadNo, ThreadInfo->StartIndex);
        DLOG (C_VERBOSE, "Thread[%d] StopIndex = %d\n", ThreadInfo->ThreadNo, ThreadInfo->StopIndex);

        /*  y = (a - b)/n */
        y = (ThreadInfo->UpperBound - ThreadInfo->LowerBound)/ThreadInfo->NoOfPoints;

        for (int i=ThreadInfo->StartIndex; i< ThreadInfo->StopIndex; i++) {
            x = (ThreadInfo->LowerBound + ((i + 0.5)* y ));
            FunOutput = ThreadInfo->FuncToIntegrate (x,ThreadInfo->Intensity );
            FunOutput = FunOutput * y ;
            IntegralOutput = IntegralOutput + FunOutput;

        }
        pthread_mutex_lock (&IntegralMutex);
        DLOG (C_VERBOSE, "Thread[%d] IntegralOutput = %f, *(ThreadInfo->IntegralOutput = %f\n",
                ThreadInfo->ThreadNo,IntegralOutput, *(ThreadInfo->IntegralOutput));
        
        *(ThreadInfo->IntegralOutput) = *(ThreadInfo->IntegralOutput) + IntegralOutput;
        pthread_mutex_unlock (&IntegralMutex);

    }
    DLOG (C_VERBOSE, "Thread[%d] completed its operation and is exiting\n", ThreadInfo->ThreadNo);
    return NULL;
}

/*==============================================================================
 *  IsLoopDone
 *=============================================================================*/

bool IsLoopDone (void * inArg)
{
    bool C_Status;
    RefThreadData ThreadInfo = (RefThreadData)inArg;

    DLOG (C_VERBOSE, "ThreadNo = [%d] Enter\n", ThreadInfo->ThreadNo);
    pthread_mutex_lock (& (LoopMutex));

    DLOG (C_VERBOSE, "ThreadNo = [%d] ThreadInfo->CompletedIndex = %d\n", ThreadInfo->ThreadNo, *(ThreadInfo->CompletedIndex));
    if (*(ThreadInfo->CompletedIndex) == ThreadInfo->NoOfPoints){
        C_Status = true;
    }
    else{
        C_Status = false;
    }
    pthread_mutex_unlock (& (LoopMutex));

    DLOG (C_VERBOSE, "ThreadNo = [%d] Exit\n", ThreadInfo->ThreadNo);

    return C_Status;
}

/*==============================================================================
 *  GetNextLoop
 *=============================================================================*/

int GetNextLoop (void * inArg)
{
    int C_Status = C_SUCCESS; 
    RefThreadData ThreadInfo = (RefThreadData)inArg;

    DLOG (C_VERBOSE, "ThreadNo = [%d] Enter\n", ThreadInfo->ThreadNo);
    DLOG (C_VERBOSE, "ThreadInfo = %p\n", ThreadInfo);

    pthread_mutex_lock (& (LoopMutex));

    ThreadInfo->StartIndex = *(ThreadInfo->CompletedIndex);
    ThreadInfo->StopIndex = *(ThreadInfo->CompletedIndex) + ThreadInfo->Granularity;

    if (ThreadInfo->StopIndex >= ThreadInfo->NoOfPoints) {
        ThreadInfo->StopIndex = ThreadInfo->NoOfPoints;
    }

    *(ThreadInfo->CompletedIndex) = ThreadInfo->StopIndex;
    DLOG (C_VERBOSE, "ThreadNo = [%d] ThreadInfo->CompletedIndex = %d\n", ThreadInfo->ThreadNo, *(ThreadInfo->CompletedIndex));

    pthread_mutex_unlock (& (LoopMutex));

    DLOG (C_VERBOSE, "ThreadNo = [%d] Exit\n", ThreadInfo->ThreadNo);
    return C_Status;
}

