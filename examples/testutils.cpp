#include "../src/utils.h"
#include <stdio.h>

int main(int argc, char* argv[])
// test next235even. Barnett 2/9/17
{
  for (BIGINT n=1;n<100;++n)
    printf("next235even(%ld) =\t%ld\n",n,next235even(n));
  return 0;
}
