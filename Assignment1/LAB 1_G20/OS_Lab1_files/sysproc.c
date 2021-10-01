#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// return the number of bytes copied into the buffer and prints a ascii art of wolfie on the console.

int
sys_wolfie(void)
{
  void* buf;
  uint size;

  argptr(0, (void*)&buf, sizeof(buf));
  argptr(1, (void*)&size, sizeof(size));

  char wolf[] = "Wolfie: \n\
                                                                            ,aa,       ,aa\n\
                                                                           d'  'b    ,d',`b\n\
                                                                         ,dP a  'b,ad8' 8 8\n\
                                                                         d8' 8  ,88888a 8 8\n\
                                                                        d8baa8ba888888888a8\n\
                                                                     ,ad888888888YYYY888YYY,\n\
                                                                  ,a888888888888'   '8P'  'b\n\
                                                              ,aad8888tt,8888888b (0 `8, 0 8\n\
                          ____________________________,,aadd888ttt8888ttt'8'I  'Yb,   `Ya  8\n\
                    ,aad8888b888888aab8888888888b,     ,aatPt888ttt8888tt 8,`b,   'Ya,. `'aP\n\
                ,ad88tttt8888888888888888888888888ttttt888ttd88888ttt8888tt,t 'ba,.  `'`d888\n\
             ,d888tttttttttttttt888888888888888888888888ttt8888888888ttt888ttt,   'a,   `88'\n\
            a888tttttttttttttttttttttttttt8888888888888ttttt88888ttt888888888tt,    `''8''\n\
           d8P'' ,tttttttttttttttttttttttttttttttttt88tttttt888tttttttt8a'8888ttt,   ,8'\n\
          d8tb  ' ,tt'  ''tttttttttttttttttttttttttttttttttt88ttttttttttt, Y888tt'  ,8'\n\
          88tt)              't' ttttt' '''  '''    '' tttttYttttttttttttt, ' 8ttb,a8'\n\
          88tt                    `'b'                  ''t'ttttttttttt't't   t taP'\n\
          8tP                       `b                       ,tttttt' ' ' 'tt, ,8'\n\
         (8tb  b,                    `b,                 a,  tttttt'        ''dP'\n\
         I88tb `8,                    `b                d'   tttttt        ,aP'\n\
         8888tb `8,                   ,P               d'    'tt 't'    ,a8P'\n\
        I888ttt, 'b                  ,8'              ,8       'tt'  ,d'd''\n\
       ,888tttt'  8b               ,dP''''''''''''''''Y8        tt ,d',d'\n\
     ,d888ttttP  d'8b            ,dP'                  'b,      'ttP' d'\n\
   ,d888ttttPY ,d' dPb,        ,dP'                      'b,     t8'  8\n\
  d888tttt8' ,d' ,d'  8      ,d''                         `b     'P   8\n\
 d888tt88888d' ,d'  ,d'    ,d'                             8      I   8\n\
d888888888P' ,d'  ,d'    ,d'                               8      I   8\n\
88888888P' ,d'   (P'    d'                                 8      8   8\n\
'8P'''8   ,8'    Ib    d'                                  Y      8   8\n\
      8   d'     `8    8                                   `b     8   Y\n\
      8   8       8,   8,                                   8     Y   `b\n\
      8   Y,      `b   `b                                   Y     `b   `b\n\
      Y,   'ba,    `b   `b,                                 `b     8,   `'ba,\n\
       'b,   '8     `b    `''b                               `b     `Yaa,adP'\n\
         ''''''      `baaaaaaP                                `YaaaadP''\n";
  if(sizeof(wolf)>size)
    return -1;

  strncpy((char *)buf, wolf, size);
  return sizeof(wolf);
}
