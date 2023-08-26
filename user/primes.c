// a concurrent version of prime sieve using pipes

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAXNUM 35

void primeSieve(int *parentpipe)
{
   // handle pipe first，close parentpipe[0];
   close(parentpipe[1]);

   // accquire the first num
   int firstNum;
   if (read(parentpipe[0], &firstNum, 4) == 0) // there is no num for parentpipe
   {
      close(parentpipe[0]);
      exit(0); // return point of recursion
   }

   int childpipe[2];
   pipe(childpipe);

   int PID = fork();
   if (PID == 0)
   {
      close(parentpipe[0]); // child process only copy parentpipe[0], parentpipe[1] has been closed.
      primeSieve(childpipe);
   }
   else
   {
      close(childpipe[0]);
      printf("prime %d \n", firstNum);
      int buffer = firstNum;
      while (read(parentpipe[0], &buffer, 4) != 0)
      {
         if (buffer % firstNum != 0)
         {
            write(childpipe[1], &buffer, 4);
         }
      }
      close(parentpipe[0]);
      close(childpipe[1]);
      wait(0);
   }
}

int main()
{

   int mainpipe[2];
   pipe(mainpipe);

   int PID = fork();
   if (PID == 0)
   {
      primeSieve(mainpipe);
   }
   else
   {
      close(mainpipe[0]);
      for (int i = 2; i <= MAXNUM; i++)
      {
         write(mainpipe[1], &i, 4);
      }
      close(mainpipe[1]);
      wait(0);
   }
   exit(0);
}