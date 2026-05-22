#include "app.h"
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#  include <direct.h>
#  define chdir _chdir
#else
#  include <unistd.h>
#endif

int main(int argc, char* argv[])
{
  if(argc >= 1 && argv[0] && argv[0][0])
  {
    char dir[1024];
    strncpy(dir, argv[0], sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char* last = dir;
    for(char* p = dir; *p; p++)
    if(*p == '/' || *p == '\\')
    {
      last = p;
    }

   if(last != dir)
    {
      *last = '\0';
      chdir(dir);
    }
  }

  App app;
  if(!app.init(1550, 1080)) {
  fprintf(stderr, "bkpview: init failed\n");
  return 1;
  }
  if(argc >= 2)
  snprintf(app.pendingPath, sizeof(app.pendingPath), "%s", argv[1]);
 app.run();
  app.cleanup();
  return 0;
}
