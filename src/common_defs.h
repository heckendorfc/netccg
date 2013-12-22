#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

#include <stdio.h>
#include <stdlib.h>

#define INIT_MEM(x,y) if((x=malloc(sizeof(*x)*y))==NULL)exit(22);
#define abs_of(x) ((x)> 0 ? (x) : (-1*(x)))
#define max(a,b) (a>b?a:b)
#define min(a,b) (a<b?a:b)

#endif
