#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"

void testfunc(long long int num)
{
    long long int testvariable = 0;
    for (volatile long long int i = 0; i < 1000000*num; i++)
        {
            testvariable++;
        }
}

int main(int argc, char *argv[])
{
    //Calling Setpriority
    sleep(10);
    int pid = fork();
    if(pid == 0)
    {
        int pid2 = fork();
        if(pid2 == 0)
        {
            #ifdef PBS
            setpriority(40);
            #endif
            testfunc(100);
            printf(1,"Proc1\n");

        }

        else if(pid2!=0)
        {
            int pid3 = fork();
            if(pid3 == 0)
            {
                #ifdef PBS
                setpriority(60);
                #endif
                testfunc(50);
                printf(1,"Proc2\n");
            }
       
            else
            {
                #ifdef PBS
                setpriority(40);
                #endif
                testfunc(100);
                printf(1,"Proc3\n");
            }
        }
    }
    else if(pid !=0)
    {
           #ifdef PBS
            setpriority(100);
            #endif
            testfunc(80);
            printf(1,"Proc4\n");

    }
    exit();
}