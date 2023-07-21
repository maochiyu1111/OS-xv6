// find . b  : find file named b in the directories below "."

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *dirName, char *fileName)
{

   char buf[512], *bufpointer;
   struct dirent de;
   struct stat st;
   int fd;

   if ((fd = open(dirName, 0)) < 0)
   {
      fprintf(2, "find: cannot find %s\n", dirName);
      exit(1);
   }

   if (fstat(fd, &st) < 0)
   {
      fprintf(2, "find: cannot stat %s\n", dirName);
      close(fd);
      exit(1);
   }

   switch (st.type)
   {
      case T_FILE:
         printf("wrong directory\n");
         exit(1);

      case T_DIR:
         if (strlen(dirName) + 1 + DIRSIZ + 1 > sizeof buf)
         {
            printf("find: path too long\n");
            exit(1);
         }

         strcpy(buf, dirName);
         bufpointer = buf + strlen(buf); // move pointer to the end of dir
         *bufpointer++ = '/';            // bufdir -> bufdir/

         while (read(fd, &de, sizeof(de)) == sizeof(de))  //read all file
         {
            if (de.inum == 0) // indicate file is invalid or empty
               continue;
            memmove(bufpointer, de.name, DIRSIZ); // bufdir/ -> bufdir/fileName
            bufpointer[DIRSIZ] = 0;               // add "0" as end signal to bufdir
            if (stat(buf, &st) < 0)
            {
               printf("find: cannot stat %s\n", buf);
               continue;
            }

            switch (st.type)
            {
               case T_FILE:
                  if (strcmp(de.name, fileName) == 0)
                  {
                     printf("%s \n", buf);
                  }
                  break;

               case T_DIR:
                  if ((strcmp(de.name, ".") != 0) && (strcmp(de.name, "..") != 0))
                  {
                     find(buf, fileName);
                  }
                  break;
               }
         }
         break;
   }
   close(fd);
}

int main(int argc, char *argv[])
{

   if (argc != 3)
   {
      fprintf(2, "require 2 parameter\n");
      exit(1);
   }

   char *dirName = argv[1];
   char *fileName = argv[2];

   find(dirName, fileName);
   exit(0);
}
