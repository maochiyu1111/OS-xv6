// Exercises using pipes

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
   int parent2child[2];
   int child2parent[2];
   pipe(parent2child);
   pipe(child2parent);

   char byte[1];

   int pid = fork();
   if (pid == 0) // child
   {
      close(child2parent[0]); // close useless port
      close(parent2child[1]);

      read(parent2child[0], byte, 1); // block, waiting parent to write byte
      printf("%d: received ping\n", getpid());
      write(child2parent[1], byte, 1);

      close(parent2child[0]);
      close(child2parent[1]);
      exit(0);
   }
   else //parent
   {
      close(child2parent[1]);
      close(parent2child[0]);

      write(parent2child[1], "q", 1);
      read(child2parent[0], byte, 1);
      printf("%d: received pong\n", getpid());

      close(child2parent[0]);
      close(parent2child[1]);
      exit(0);
   }
}