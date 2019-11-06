#include "fcntl.h"
#include "types.h"
#include "user.h"

int main(int argc, char * argv[])
{

    for(int i=0 ; i<10000; i++)
    {
        printf(1," ");
        printf(1,"\b");
    }
    exit();
}