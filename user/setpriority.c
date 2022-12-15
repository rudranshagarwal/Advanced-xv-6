#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "user.h"

int main(int argc , char * argv[])
{
    if(argc != 3)
    {
        fprintf(2, "Usage: setpriority priority pid\n");
        exit(1);
    }

    printf("priority set: %d\n", setpriority(atoi(argv[1]), atoi(argv[2])));
    exit(0);
}