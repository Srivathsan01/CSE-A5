#include "fcntl.h"
#include "types.h"
#include "user.h"

int main(int argc, char * argv[])
{
    int testvariable=0;
    
    for(int i=0 ; i<100000; i++)
    {
        testvariable++;
    }
    // printf(1,"%d",testvariable);
    exit();
}