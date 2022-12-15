// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "user.h"
// #include "kernel/fcntl.h"

// #define NFORK 10
// #define IO 5

// int main()
// {
//     int n, pid;
//     for (n = 0; n < NFORK; n++)
//     {
//         pid = fork();
//         if (pid < 0)
//             break;
//         if (pid == 0)
//         {
// #if !defined MLFQ && !defined FCFS

//             if (n < IO)
//             {
//                 sleep(200); // IO bound processes
//             }
//             else
//             {
// #endif
//                 for (volatile int i = 0; i < 1000000000; i++)
//                 {
//                 } // CPU bound process
// #if !defined MLFQ && !defined FCFS
//             }
// #endif
//             printf("Process %d finished\n",getpid());
//             exit(0);
//         }
//         else
//         {
// #ifdef PBS
//             setpriority((n+1)*10, pid); // Will only matter for PBS, set lower priority for IO bound processes
// #endif
// #ifdef LBS
//             settickets(n + 1);
// #endif
//         }
//     }
//     exit(0);
// }

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define NFORK 10
#define IO 5

int main()
{
    int n, pid;
    int wtime, rtime;
    int twtime = 0, trtime = 0;
    for (n = 0; n < NFORK; n++)
    {
        pid = fork();
        if (pid < 0)
            break;
        if (pid == 0)
        {
#if !defined FCFS && !defined MLFQ
            if (n < IO)
            {
                sleep(200); // IO bound processes
            }
            else
            {
#endif
                for (volatile int i = 0; i < 1000000000; i++)
                {
                } // CPU bound process
#if !defined FCFS && !defined MLFQ
            }
#endif
         //   printf("Process %d finished\n", n);
            exit(0);
        }
        else
        {
#ifdef PBS
            setpriority((n+1)*10, pid); // Will only matter for PBS, set lower priority for IO bound processes
#endif
#ifdef LBS
             settickets(n + 1);
#endif

        }
    }
    for (; n > 0; n--)
    {
        if (waitx(0, &wtime, &rtime) >= 0)
        {
            trtime += rtime;
            twtime += wtime;
        }
    }
    printf("Average rtime %d,  wtime %d\n", trtime / NFORK, twtime / NFORK);
    exit(0);
}