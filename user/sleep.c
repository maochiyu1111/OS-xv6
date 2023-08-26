// sleep for a user-specified number of ticks

#include"kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h" 

int main(int argc, char *argv[])
{

   char ERROR1[] = "no parameter \n";
   char ERROR2[] = "one parameter only \n";

   if (argc == 1)
   {
      write(1, ERROR1, strlen(ERROR1));
      exit(1);
   }
   else if (argc > 2)
   {
      write(1, ERROR2, strlen(ERROR2));
      exit(1);
   }
   else
   {
      sleep(atoi(argv[1]));
      exit(0);
   }
}