#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"
int main( int argc, char *argv[])
{
    //Calling Setpriority
    int priority;
    priority = atoi(argv[1]);
    int r = setpriority(priority);
}