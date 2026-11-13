#include "sagui.h"

int principal(int argc, char **argv)
{
    int a = argc;
    typeof(a) b = 2;
    (void)argv;
    return a + b;
}
