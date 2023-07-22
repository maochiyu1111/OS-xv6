
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define MAXLEN 30

int main(int argc, char *argv[])
{
   char bytebuf;
   char argbuf[MAXLEN];              // buffer for single argument
   char param[MAXARG][MAXLEN] = {0}; // argument set, including argv[1:] and param read from standard input
   char *command = argv[1];
   char *argset[MAXARG];

   // move argv[1:] to param
   for (int i = 1; i < argc; i++)
   {
      strcpy(param[i - 1], argv[i]);
   }

   while (1)
   {
      int byteCounter = 0;
      int argCounter = argc - 1;
      int result;
      while ((result = read(0, &bytebuf, 1)) > 0 && bytebuf != '\n')
      {
         if (bytebuf == ' ')
         {
            // an argument has been read Completely
            strcpy(param[argCounter], argbuf);
            argCounter++;
            byteCounter = 0;
            memset(argbuf, 0, sizeof(argbuf));  //clear buffer
         }
         else
         {
            argbuf[byteCounter] = bytebuf;
            byteCounter++;
         }
      }
      strcpy(param[argCounter], argbuf);
      // param[argCounter + 1] = 0;

      // check if done reading or have error
      if (result <= 0)
      {
         break;
      }

      for (int i = 0; i < MAXARG - 1; ++i)
      {
         argset[i] = param[i];
      }

      argset[MAXARG - 1] = 0;  // set null pointer, indicate the end of argset

      // exec a line
      if (fork() == 0)
      {  
         //printf("%s\n%s\n%s\n%s\n%s\n", argset[0], argset[1], argset[2], argset[3], argset[4]);
         exec(command, argset);
         exit(0);
      }
      else
      {
         wait(0);
      }
   }
   exit(0);
}
