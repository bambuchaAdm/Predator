#include "mod_authnz_predator.h"

typedef struct
{
    const char * url;
    int port;
    const char * service;
}
AuthPredatorConfig;

static
void * createModAuthPredatorConfig(apr_pool_t *p, char *d)
{
    AuthPredatorConfig * config = apr_palloc(p,sizeof(*config));
    
    config->url = NULL;     //Gdzie ma się łączyć
    config->port = 8443;    //Standardowy port
    config->service = NULL; //Usługa do której ma być logowany
    
    return config;
}

static const command_rec authn_file_cmds[] =
{
    AP_INIT_TAKE12("AuthUserFile", ap_set_string_slot,
                   (void *)APR_OFFSETOF(AuthPredatorConfig, url),
                   OR_AUTHCFG, "text file containing user IDs and passwords"),
    {NULL}
};

//static const command_rec auth_predator_commands[] =
//{
    
    //AP_INIT_TAKE12("PredatorHost", ap_set_string_slot,
        //(*void)APR_OFFSETOF(AuthPredatorConfig,url),OR_AUTHCFG,
        //"Adres demona Predatora. Bez niego nie można wysyłać mu chikenów."),
    
    //AP_INIT_TAKE1("PredatorService", ap_set_string_slot,
        //(*void)APR_OFFSETOF(AuthPredatorConfig,service),OR_AUTHCFG,
        //"Nazwa usługi pod jaką ma być zautoryzowany"),
    //{NULL}
//};

static
authn_status check_password(request_rec *r, const char *user,
                                   const char *password)
{
    if(strcmp(user,"bambucha"))
       return AUTH_USER_NOT_FOUND; 
    if(strcmp(password,"dupad12"))
       return AUTH_DENIED;
    return AUTH_GRANTED;
}

static const authn_provider auth_predator_provider =
{
    &check_password,
};
 

static void register_hooks(apr_pool_t *p)
{
    ap_register_provider(p, AUTHN_PROVIDER_GROUP, "file", "0",
                         &auth_predator_provider);
}

module AP_MODULE_DECLARE_DATA authn_file_module =
{
    STANDARD20_MODULE_STUFF,
    createModAuthPredatorConfig,     /* dir config creater */
    NULL,                            /* dir merger --- default is to override */
    NULL,                            /* server config */
    NULL,                            /* merge server config */
    authn_file_cmds,                 /* command apr_table_t */
    register_hooks                   /* register hooks */
};

