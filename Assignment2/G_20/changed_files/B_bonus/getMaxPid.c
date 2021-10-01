#include "types.h"
#include "stat.h"
#include "user.h"
 
int
main(void)
{
  printf(1, "MAX ID of the process running = %d\n",getMaxPid());
  exit();
}
