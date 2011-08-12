#include "mod_predator.h"

typedef struct
{
    const char * url;
    int port;
    const char * service;
}
ModAuthPredatorConfig;

static
void * create
