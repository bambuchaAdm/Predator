#include "mod_predator.h"

typedef struct
{
    const char * url;
    int port;
    const char * service;
}
ModAuthPredatorConfig;

static
void * createModAuthPredatorConfig(apr_pool_t *p, char *d)
{
    ModAuthPredatorConfig * config = apr_palloc(p,sizeof(*config));
    
    config->url = NULL;     //Gdzie ma się łączyć
    config->port = 8443;    //Standardowy port
    config->service = NULL; //Usługa do której ma być logowany
    
    return config;
}
