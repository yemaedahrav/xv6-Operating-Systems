#include "types.h"
#include "stat.h"
#include "user.h"
 
int
main(void)
{
  printf(1, "Number of active processes = %d\n",getNumProc());
  exit();
}
