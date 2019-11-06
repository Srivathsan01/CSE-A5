#include "fcntl.h"
#include "types.h"
#include "user.h"

int main(int argc, char * argv[])
{
    int testvariable = 0;
    for (volatile long long int i = 0; i < 1000000*atoi(argv[1]); i++)
        {
            testvariable++;
        }
        printf(1,"%d\n", atoi(argv[1]));
        exit();
}