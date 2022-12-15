#include "../kernel/param.h"
#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "user.h"

int main(int argc, char* argv[])
{
    char* args_topass[argc-2];

    if (argc<3 || argv[1][0]>='9' || argv[1][0]<='0')
    {
        fprintf(2, "Error!\n");
        exit(1);
    }

    if (trace(atoi(argv[1]))<0)
    {
        fprintf(2, "Strace failure!\n");
        exit(1);
    }

    for (int i = 2; i < argc; i++)
        args_topass[i - 2] = argv[i];

    exec(args_topass[0], args_topass);
    exit(0);
}