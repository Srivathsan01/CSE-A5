#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main(int argc, char * argv[])
{
    int pid, stat=0;
    int wt, rt;
    if((pid=fork())==0){
        exec(argv[1], argv);
        printf(1, "Exec %s failed\n", argv[1]);
    }

    else {
        stat = waitx(&wt, &rt);
        // sleep(10);
    }

    printf(1, "Wait Time: %d;\n Run Time: %d;\n Status: %d;\n", wt, rt, stat);
    exit();
}