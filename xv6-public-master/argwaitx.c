#include "fcntl.h"
#include "types.h"
#include "user.h"

int main(int argc, char * argv[])
{
    int pid;//, x = 0;
    if((pid = fork())==0){
        // printf(1,"run child running....\n");
        for(int i=0; i<2000; i++)
        {
            // x = (x * i)%1024;
            printf(1, " ");
            printf(1, "\b");
        }
        // x = x + 12*21;
        // sleep(5);
    }
    // int x = 0;
    // sleep(4);
    // printf(1,"run parent runnning....\n");
    for(int i=0; i<10000; i++)
    {
        // x = (x * i)%1024;
        printf(1, " ");
        printf(1, "\b");
    }
    printf(1, "Program ended\n");
    exit();
}