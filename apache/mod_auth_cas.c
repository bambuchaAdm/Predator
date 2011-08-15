/*
 *
 * Copyright 2010 Phillip Ames / Matt Smith
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 *
 * mod_auth_cas.c
 * Apache CAS Authentication Module
 * Version 1.0.9.1
 *
 * Author:
 * Phil Ames       <modauthcas [at] gmail [dot] com>
 * Designers:
 * Phil Ames       <modauthcas [at] gmail [dot] com>
 * Matt Smith      <matt [dot] smith [at] uconn [dot] edu>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>

#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <curl/curl.h>

#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_protocol.h"
#include "http_request.h"
#include "http_log.h"
#include "util_md5.h"
#include "ap_config.h"
#include "ap_release.h"
#include "apr_buckets.h"
#include "apr_file_info.h"
#include "apr_md5.h"
#include "apr_thread_mutex.h"
#include "apr_strings.h"
#include "apr_xml.h"

#include "mod_auth_cas.h"

#if defined(OPENSSL_THREADS) && APR_HAS_THREADS
static apr_thread_mutex_t **ssl_locks;
static int ssl_num_locks;	
#endif /* defined(OPENSSL_THREADS) && APR_HAS_THREADS */

/* mod_auth_cas configuration specific functions */
static void *cas_create_server_config(apr_pool_t *pool, server_rec *svr)
{
	cas_cfg *c = apr_pcalloc(pool, sizeof(cas_cfg));

	c->merged = FALSE;
	c->CASVersion = CAS_DEFAULT_VERSION;
	c->CASDebug = CAS_DEFAULT_DEBUG;
	c->CASValidateServer = CAS_DEFAULT_VALIDATE_SERVER;
	c->CASValidateDepth = CAS_DEFAULT_VALIDATE_DEPTH;
	c->CASAllowWildcardCert = CAS_DEFAULT_ALLOW_WILDCARD_CERT;
	c->CASCertificatePath = CAS_DEFAULT_CA_PATH;
	c->CASCookiePath = CAS_DEFAULT_COOKIE_PATH;
	c->CASCookieEntropy = CAS_DEFAULT_COOKIE_ENTROPY;
	c->CASTimeout = CAS_DEFAULT_COOKIE_TIMEOUT;
	c->CASIdleTimeout = CAS_DEFAULT_COOKIE_IDLE_TIMEOUT;
	c->CASCacheCleanInterval = CAS_DEFAULT_CACHE_CLEAN_INTERVAL;
	c->CASCookieDomain = CAS_DEFAULT_COOKIE_DOMAIN;
	c->CASCookieHttpOnly = CAS_DEFAULT_COOKIE_HTTPONLY;
	c->CASSSOEnabled = CAS_DEFAULT_SSO_ENABLED;
	c->CASValidateSAML = CAS_DEFAULT_VALIDATE_SAML;
	c->CASAttributeDelimiter = CAS_DEFAULT_ATTRIBUTE_DELIMITER;
	c->CASAttributePrefix = CAS_DEFAULT_ATTRIBUTE_PREFIX;

	cas_setURL(pool, &(c->CASLoginURL), CAS_DEFAULT_LOGIN_URL);
	cas_setURL(pool, &(c->CASValidateURL), CAS_DEFAULT_VALIDATE_URL);
	cas_setURL(pool, &(c->CASProxyValidateURL), CAS_DEFAULT_PROXY_VALIDATE_URL);
	cas_setURL(pool, &(c->CASRootProxiedAs), CAS_DEFAULT_ROOT_PROXIED_AS_URL);

	return c;
}

static void *cas_merge_server_config(apr_pool_t *pool, void *BASE, void *ADD)
{
	cas_cfg *c = apr_pcalloc(pool, sizeof(cas_cfg));
	cas_cfg *base = BASE;
	cas_cfg *add = ADD;
	apr_uri_t test;
	memset(&test, '\0', sizeof(apr_uri_t));

	c->merged = TRUE;
	c->CASVersion = (add->CASVersion != CAS_DEFAULT_VERSION ? add->CASVersion : base->CASVersion);
	c->CASDebug = (add->CASDebug != CAS_DEFAULT_DEBUG ? add->CASDebug : base->CASDebug);
	c->CASValidateServer = (add->CASValidateServer != CAS_DEFAULT_VALIDATE_SERVER ? add->CASValidateServer : base->CASValidateServer);
	c->CASValidateDepth = (add->CASValidateDepth != CAS_DEFAULT_VALIDATE_DEPTH ? add->CASValidateDepth : base->CASValidateDepth);
	c->CASAllowWildcardCert = (add->CASAllowWildcardCert != CAS_DEFAULT_ALLOW_WILDCARD_CERT ? add->CASAllowWildcardCert : base->CASAllowWildcardCert);
	c->CASCertificatePath = (apr_strnatcasecmp(add->CASCertificatePath,CAS_DEFAULT_CA_PATH) != 0 ? add->CASCertificatePath : base->CASCertificatePath);
	c->CASCookiePath = (apr_strnatcasecmp(add->CASCookiePath, CAS_DEFAULT_COOKIE_PATH) != 0 ? add->CASCookiePath : base->CASCookiePath);
	c->CASCookieEntropy = (add->CASCookieEntropy != CAS_DEFAULT_COOKIE_ENTROPY ? add->CASCookieEntropy : base->CASCookieEntropy);
	c->CASTimeout = (add->CASTimeout != CAS_DEFAULT_COOKIE_TIMEOUT ? add->CASTimeout : base->CASTimeout);
	c->CASIdleTimeout = (add->CASIdleTimeout != CAS_DEFAULT_COOKIE_IDLE_TIMEOUT ? add->CASIdleTimeout : base->CASIdleTimeout);
	c->CASCacheCleanInterval = (add->CASCacheCleanInterval != CAS_DEFAULT_CACHE_CLEAN_INTERVAL ? add->CASCacheCleanInterval : base->CASCacheCleanInterval);
	c->CASCookieDomain = (add->CASCookieDomain != CAS_DEFAULT_COOKIE_DOMAIN ? add->CASCookieDomain : base->CASCookieDomain);
	c->CASCookieHttpOnly = (add->CASCookieHttpOnly != CAS_DEFAULT_COOKIE_HTTPONLY ? add->CASCookieHttpOnly : base->CASCookieHttpOnly);
	c->CASSSOEnabled = (add->CASSSOEnabled != CAS_DEFAULT_SSO_ENABLED ? add->CASSSOEnabled : base->CASSSOEnabled);
	c->CASValidateSAML = (add->CASValidateSAML != CAS_DEFAULT_VALIDATE_SAML ? add->CASValidateSAML : base->CASValidateSAML);
	c->CASAttributeDelimiter = (apr_strnatcasecmp(add->CASAttributeDelimiter, CAS_DEFAULT_ATTRIBUTE_DELIMITER) != 0 ? add->CASAttributeDelimiter : base->CASAttributeDelimiter);
	c->CASAttributePrefix = (apr_strnatcasecmp(add->CASAttributePrefix, CAS_DEFAULT_ATTRIBUTE_PREFIX) != 0 ? add->CASAttributePrefix : base->CASAttributePrefix);

	/* if add->CASLoginURL == NULL, we want to copy base -- otherwise, copy the one from add, and so on and so forth */
	if(memcmp(&add->CASLoginURL, &test, sizeof(apr_uri_t)) == 0)
		memcpy(&c->CASLoginURL, &base->CASLoginURL, sizeof(apr_uri_t));
	else
		memcpy(&c->CASLoginURL, &add->CASLoginURL, sizeof(apr_uri_t));

	if(memcmp(&add->CASValidateURL, &test, sizeof(apr_uri_t)) == 0)
		memcpy(&c->CASValidateURL, &base->CASValidateURL, sizeof(apr_uri_t));
	else
		memcpy(&c->CASValidateURL, &add->CASValidateURL, sizeof(apr_uri_t));

	if(memcmp(&add->CASProxyValidateURL, &test, sizeof(apr_uri_t)) == 0)
		memcpy(&c->CASProxyValidateURL, &base->CASProxyValidateURL, sizeof(apr_uri_t));
	else
		memcpy(&c->CASProxyValidateURL, &add->CASProxyValidateURL, sizeof(apr_uri_t));

	if(memcmp(&add->CASRootProxiedAs, &test, sizeof(apr_uri_t)) == 0)
		memcpy(&c->CASRootProxiedAs, &base->CASRootProxiedAs, sizeof(apr_uri_t));
	else
		memcpy(&c->CASRootProxiedAs, &add->CASRootProxiedAs, sizeof(apr_uri_t));


	return c;
}

static void *cas_create_dir_config(apr_pool_t *pool, char *path)
{
	cas_dir_cfg *c = apr_pcalloc(pool, sizeof(cas_dir_cfg));
	c->CASScope = CAS_DEFAULT_SCOPE;
	c->CASRenew = CAS_DEFAULT_RENEW;
	c->CASGateway = CAS_DEFAULT_GATEWAY;
	c->CASCookie = CAS_DEFAULT_COOKIE;
	c->CASSecureCookie = CAS_DEFAULT_SCOOKIE;
	c->CASGatewayCookie = CAS_DEFAULT_GATEWAY_COOKIE;
	c->CASAuthNHeader = CAS_DEFAULT_AUTHN_HEADER;
	c->CASScrubRequestHeaders = CAS_DEFAULT_SCRUB_REQUEST_HEADERS;
	return(c);
}

static void *cas_merge_dir_config(apr_pool_t *pool, void *BASE, void *ADD)
{
	cas_dir_cfg *c = apr_pcalloc(pool, sizeof(cas_dir_cfg));
	cas_dir_cfg *base = BASE;
	cas_dir_cfg *add = ADD;

	/* inherit the previous directory's setting if applicable */
	c->CASScope = (add->CASScope != CAS_DEFAULT_SCOPE ? add->CASScope : base->CASScope);
	if(add->CASScope != NULL && strcasecmp(add->CASScope, "Off") == 0)
		c->CASScope = NULL;

	c->CASRenew = (add->CASRenew != CAS_DEFAULT_RENEW ? add->CASRenew : base->CASRenew);
	if(add->CASRenew != NULL && strcasecmp(add->CASRenew, "Off") == 0)
		c->CASRenew = NULL;

	c->CASGateway = (add->CASGateway != CAS_DEFAULT_GATEWAY ? add->CASGateway : base->CASGateway);
	if(add->CASGateway != NULL && strcasecmp(add->CASGateway, "Off") == 0)
		c->CASGateway = NULL;

	c->CASCookie = (apr_strnatcasecmp(add->CASCookie, CAS_DEFAULT_COOKIE) != 0 ? add->CASCookie : base->CASCookie);
	c->CASSecureCookie = (apr_strnatcasecmp(add->CASSecureCookie, CAS_DEFAULT_SCOOKIE) != 0 ? add->CASSecureCookie : base->CASSecureCookie);
	c->CASGatewayCookie = (apr_strnatcasecmp(add->CASGatewayCookie, CAS_DEFAULT_GATEWAY_COOKIE) != 0 ? add->CASGatewayCookie : base->CASGatewayCookie);
	
	c->CASAuthNHeader = (apr_strnatcasecmp(add->CASAuthNHeader, CAS_DEFAULT_AUTHN_HEADER) != 0 ? add->CASAuthNHeader : base->CASAuthNHeader);

	c->CASScrubRequestHeaders = (add->CASScrubRequestHeaders != CAS_DEFAULT_SCRUB_REQUEST_HEADERS ? add->CASScrubRequestHeaders : base->CASScrubRequestHeaders);
	if(add->CASScrubRequestHeaders != NULL && strcasecmp(add->CASScrubRequestHeaders, "Off") == 0)
		c->CASScrubRequestHeaders = NULL;

	return(c);
}

static const char *cfg_readCASParameter(cmd_parms *cmd, void *cfg, const char *value)
{
	cas_cfg *c = (cas_cfg *) ap_get_module_config(cmd->server->module_config, &auth_cas_module);
	apr_finfo_t f;
	int i;
	char d;

	/* cases determined from valid_cmds in mod_auth_cas.h - the config at this point is initialized to default values */
	switch((size_t) cmd->info) {
		case cmd_version:
			i = atoi(value);
			if(i > 0)
				c->CASVersion = i;
			else
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Invalid CAS version (%s) specified", value));
		break;
		case cmd_debug:
			/* if atoi() is used on value here with AP_INIT_FLAG, it works but results in a compile warning, so we use TAKE1 to avoid it */
			if(apr_strnatcasecmp(value, "On") == 0)
				c->CASDebug = TRUE;
			else if(apr_strnatcasecmp(value, "Off") == 0)
				c->CASDebug = FALSE;
			else
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Invalid argument to CASDebug - must be 'On' or 'Off'"));
		break;
		case cmd_validate_server:
			/* if atoi() is used on value here with AP_INIT_FLAG, it works but results in a compile warning, so we use TAKE1 to avoid it */
			if(apr_strnatcasecmp(value, "On") == 0)
				c->CASValidateServer = TRUE;
			else if(apr_strnatcasecmp(value, "Off") == 0)
				c->CASValidateServer = FALSE;
			else
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Invalid argument to CASValidateServer - must be 'On' or 'Off'"));
		break;
		case cmd_validate_saml:
			if(apr_strnatcasecmp(value, "On") == 0)
				c->CASValidateSAML = TRUE;
			else if(apr_strnatcasecmp(value, "Off") == 0)
				c->CASValidateSAML = FALSE;
			else
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Invalid argument to CASValidateSAML - must be 'On' or 'Off'"));
		break;
		case cmd_attribute_delimiter:
			c->CASAttributeDelimiter = apr_pstrdup(cmd->pool, value);
		break;
		case cmd_attribute_prefix:
			c->CASAttributePrefix = apr_pstrdup(cmd->pool, value);
		break;
		case cmd_wildcard_cert:
			// XXX this feature is broken now
			/* if atoi() is used on value here with AP_INIT_FLAG, it works but results in a compile warning, so we use TAKE1 to avoid it */
			if(apr_strnatcasecmp(value, "On") == 0)
				c->CASAllowWildcardCert = TRUE;
			else if(apr_strnatcasecmp(value, "Off") == 0)
				c->CASAllowWildcardCert = FALSE;
			else
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Invalid argument to CASAllowWildcardCert - must be 'On' or 'Off'"));
		break;

		case cmd_ca_path:
			if(apr_stat(&f, value, APR_FINFO_TYPE, cmd->temp_pool) == APR_INCOMPLETE)
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Could not find Certificate Authority file '%s'", value));
	
			if(f.filetype != APR_REG && f.filetype != APR_DIR)
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Certificate Authority file '%s' is not a regular file or directory", value));
			c->CASCertificatePath = apr_pstrdup(cmd->pool, value);
		break;
		case cmd_validate_depth:
			i = atoi(value);
			if(i > 0)
				c->CASValidateDepth = i;
			else
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Invalid CASValidateDepth (%s) specified", value));
		break;

		case cmd_cookie_path:
			/* this is probably redundant since the same check is performed in cas_post_config */
			if(apr_stat(&f, value, APR_FINFO_TYPE, cmd->temp_pool) == APR_INCOMPLETE)
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Could not find CASCookiePath '%s'", value));
			
			if(f.filetype != APR_DIR || value[strlen(value)-1] != '/')
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: CASCookiePath '%s' is not a directory or does not end in a trailing '/'!", value));

			c->CASCookiePath = apr_pstrdup(cmd->pool, value);
		break;

		case cmd_loginurl:
			if(cas_setURL(cmd->pool, &(c->CASLoginURL), value) != TRUE)
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Login URL '%s' could not be parsed!", value));
		break;
		case cmd_validateurl:
			if(cas_setURL(cmd->pool, &(c->CASValidateURL), value) != TRUE)
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Validation URL '%s' could not be parsed!", value));
		break;
		case cmd_proxyurl:
			if(cas_setURL(cmd->pool, &(c->CASProxyValidateURL), value) != TRUE)
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Proxy Validation URL '%s' could not be parsed!", value));
		break;
		case cmd_root_proxied_as:
			if(cas_setURL(cmd->pool, &(c->CASRootProxiedAs), value) != TRUE)
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Root Proxy URL '%s' could not be parsed!", value));
		break;
		case cmd_cookie_entropy:
			i = atoi(value);
			if(i > 0)
				c->CASCookieEntropy = i;
			else
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Invalid CASCookieEntropy (%s) specified - must be numeric", value));
		break;
		case cmd_session_timeout:
			i = atoi(value);
			if(i >= 0)
				c->CASTimeout = i;
			else
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Invalid CASTimeout (%s) specified - must be numeric", value));
		break;
		case cmd_idle_timeout:
			i = atoi(value);
			if(i > 0)
				c->CASIdleTimeout = i;
			else
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Invalid CASIdleTimeout (%s) specified - must be numeric", value));
		break;

		case cmd_cache_interval:
			i = atoi(value);
			if(i > 0)
				c->CASCacheCleanInterval = i;
			else
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Invalid CASCacheCleanInterval (%s) specified - must be numeric", value));
		break;
		case cmd_cookie_domain:
			for(i = 0; i < strlen(value); i++) {
				d = value[i];
				if( (d < '0' || d > '9') && 
					(d < 'a' || d > 'z') &&
					(d < 'A' || d > 'Z') &&
					d != '.' && d != '-') {
						return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Invalid character (%c) in CASCookieDomain", d));
				}
			}
			c->CASCookieDomain = apr_pstrdup(cmd->pool, value);
		break;
		case cmd_cookie_httponly:
			if(apr_strnatcasecmp(value, "On") == 0)
				c->CASCookieHttpOnly = TRUE;
			else if(apr_strnatcasecmp(value, "Off") == 0)
				c->CASCookieHttpOnly = FALSE;
			else
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Invalid argument to CASCookieHttpOnly - must be 'On' or 'Off'"));

		break;
		case cmd_sso:
			if(apr_strnatcasecmp(value, "On") == 0)
				c->CASSSOEnabled = TRUE;
			else if(apr_strnatcasecmp(value, "Off") == 0)
				c->CASSSOEnabled = FALSE;
			else
				return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: Invalid argument to CASSSOEnabled - must be 'On' or 'Off'"));
		break;
		default:
			/* should not happen */
			return(apr_psprintf(cmd->pool, "MOD_AUTH_CAS: invalid command '%s'", cmd->directive->directive));
		break;
	}
	return NULL;
}

/* utility functions to set/retrieve values from the configuration */
static apr_byte_t cas_setURL(apr_pool_t *pool, apr_uri_t *uri, const char *url)
{

	if(url == NULL) {
		uri = apr_pcalloc(pool, sizeof(apr_uri_t));
		return FALSE;
	}

	if(apr_uri_parse(pool, url, uri) != APR_SUCCESS)
		return FALSE;
	/* set a default port if none was specified - we need this to perform a connect() to these servers for validation later */
	if(uri->port == 0)
		uri->port = apr_uri_port_of_scheme(uri->scheme);
	if(uri->hostname == NULL)
		return FALSE;


	return TRUE;
}

static apr_byte_t isSSL(request_rec *r)
{

#ifdef APACHE2_0
	if(apr_strnatcasecmp("https", ap_http_method(r)) == 0)
#else
	if(apr_strnatcasecmp("https", ap_http_scheme(r)) == 0)
#endif
		return TRUE;

	return FALSE;
}

/* r->parsed_uri.path will return something like /xyz/index.html - this removes the file portion */
static char *getCASPath(request_rec *r)
{
	char *p = r->parsed_uri.path, *rv;
	size_t i, l = 0;
	for(i = 0; i < strlen(p); i++) {
		if(p[i] == '/')
			l = i;
	}
        rv = apr_pstrndup(r->pool, p, (l+1));
	return(rv);
}

static char *getCASScope(request_rec *r)
{
	char *rv = NULL, *requestPath = getCASPath(r);
	cas_cfg *c = ap_get_module_config(r->server->module_config, &auth_cas_module);
	cas_dir_cfg *d = ap_get_module_config(r->per_dir_config, &auth_cas_module);

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Determining CAS scope (path: %s, CASScope: %s, CASRenew: %s, CASGateway: %s)", requestPath, d->CASScope, d->CASRenew, d->CASGateway);

	if (d->CASGateway != NULL) {
		/* the gateway path should be a subset of the request path */
		if(strncmp(d->CASGateway, requestPath, strlen(d->CASGateway)) == 0)
			rv = d->CASGateway;
		else
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: CASGateway (%s) not a substring of request path, ignoring", d->CASGateway);
	}

	if(d->CASRenew != NULL) {
		if(rv != NULL)
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: CASRenew (%s) and CASGateway (%s) set, CASRenew superseding.", d->CASRenew, d->CASGateway);

		if(strncmp(d->CASRenew, requestPath, strlen(d->CASRenew)) == 0)
			rv = d->CASRenew;
		else
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: CASRenew (%s) not a substring of request path, ignoring", d->CASRenew);

	}

	/* neither gateway nor renew was set, or both were set incorrectly */
	if(rv == NULL) {
		if(d->CASScope != NULL) {
			if(strncmp(d->CASScope, requestPath, strlen(d->CASScope)) == 0)
				rv = d->CASScope;
			else {
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: CASScope (%s) not a substring of request path, using request path (%s) for cookie", d->CASScope, requestPath);
				rv = requestPath;
			}
		}
		else
			rv = requestPath;
	}

	return (rv);
}

static char *getCASGateway(request_rec *r)
{
	char *rv = "";
	cas_cfg *c = ap_get_module_config(r->server->module_config, &auth_cas_module);
	cas_dir_cfg *d = ap_get_module_config(r->per_dir_config, &auth_cas_module);

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering getCASGateway()");

	if(d->CASGateway != NULL && strncmp(d->CASGateway, r->parsed_uri.path, strlen(d->CASGateway)) == 0 && c->CASVersion > 1) { /* gateway not supported in CAS v1 */
		rv = "&gateway=true";
	}
	return rv;
}

static char *getCASRenew(request_rec *r)
{
	char *rv = "";
	cas_dir_cfg *d = ap_get_module_config(r->per_dir_config, &auth_cas_module);
	if(d->CASRenew != NULL && strncmp(d->CASRenew, r->parsed_uri.path, strlen(d->CASRenew)) == 0) {
		rv = "&renew=true";
	}
	return rv;
}

static char *getCASLoginURL(request_rec *r, cas_cfg *c)
{
	apr_uri_t test;

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering getCASLoginURL()");

	memset(&test, '\0', sizeof(apr_uri_t));
	if(memcmp(&c->CASLoginURL, &test, sizeof(apr_uri_t)) == 0) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: CASLoginURL null (not set?)");
		return NULL;
	}
	/* this is used in the 'Location: [LoginURL]...' header context */
	return(apr_uri_unparse(r->pool, &(c->CASLoginURL), APR_URI_UNP_OMITUSERINFO|APR_URI_UNP_OMITQUERY));
}

/*
 * Create the 'service=...' parameter
 * The reason this is not an apr_uri_t based on r->parsed_uri is that Apache does not fill out several things
 * in the apr_uri_t structure...  unimportant things, like 'hostname', and 'scheme', and 'port'...  so we must
 * implement a trimmed down version of apr_uri_unparse
 */
static char *getCASService(request_rec *r, cas_cfg *c)
{
	char *scheme, *service, *unparsedPath = NULL, *queryString = strchr(r->unparsed_uri, '?');
	int len;
	apr_port_t port = r->connection->local_addr->port;
	apr_byte_t printPort = TRUE;

	if(queryString != NULL) { 
		len = strlen(r->unparsed_uri) - strlen(queryString);
		unparsedPath = apr_pcalloc(r->pool, len+1);
		strncpy(unparsedPath, r->unparsed_uri, len);
		unparsedPath[len] = '\0';
	} else {
		unparsedPath = r->unparsed_uri;
	}

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering getCASService()");

	if(c->CASRootProxiedAs.is_initialized) {
		service = apr_psprintf(r->pool, "%s%s%s%s", escapeString(r, apr_uri_unparse(r->pool, &c->CASRootProxiedAs, 0)), escapeString(r, unparsedPath), (r->args != NULL ? "%3f" : ""), escapeString(r, r->args));
	} else {
		if(isSSL(r) && port == 443)
			printPort = FALSE;
		else if(!isSSL(r) && port == 80)
			printPort = FALSE;
#ifdef APACHE2_0
		scheme = (char *) ap_http_method(r);
#else
		scheme = (char *) ap_http_scheme(r);
#endif
		if(printPort == TRUE)
			service = apr_psprintf(r->pool, "%s%%3a%%2f%%2f%s%%3a%u%s%s%s", scheme, r->server->server_hostname, port, escapeString(r, unparsedPath), (r->args != NULL ? "%3f" : ""), escapeString(r, r->args));
		else
			service = apr_psprintf(r->pool, "%s%%3a%%2f%%2f%s%s%s%s", scheme, r->server->server_hostname, escapeString(r, unparsedPath), (r->args != NULL ? "%3f" : ""), escapeString(r, r->args));

		if(c->CASDebug)
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "CAS Service '%s'", service);
	}
	return(service);
}


/* utility functions that relate to request handling */
static void redirectRequest(request_rec *r, cas_cfg *c)
{
	char *destination;
	char *service = getCASService(r, c);
	char *loginURL = getCASLoginURL(r, c);
	char *renew = getCASRenew(r);
	char *gateway = getCASGateway(r);

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering redirectRequest()");

	if(loginURL == NULL) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: Cannot redirect request (no CASLoginURL)");
		return;
	}

	destination = apr_pstrcat(r->pool, loginURL, "?service=", service, renew, gateway, NULL);

	apr_table_add(r->headers_out, "Location", destination);

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Adding outgoing header: Location: %s", destination);

}

static apr_byte_t removeCASParams(request_rec *r)
{
	char *newArgs, *oldArgs, *p;
	apr_byte_t copy = TRUE;
	apr_byte_t changed = FALSE;
	cas_cfg *c = ap_get_module_config(r->server->module_config, &auth_cas_module);

	if(r->args == NULL)
		return changed;

	oldArgs = r->args;
	p = newArgs = apr_pcalloc(r->pool, strlen(oldArgs) + 1); /* add 1 for terminating NULL */
	while(*oldArgs != '\0') {
		/* stop copying when a CAS parameter is encountered */
		if(strncmp(oldArgs, "ticket=", 7) == 0) {
			// only prevent copying if an ST or PT is encountered, leaving PGT and PGTIOU to app layer
			if(strncmp((oldArgs + 7), "ST-", 3) == 0 || strncmp((oldArgs + 7), "PT-", 3) == 0) {
				copy = FALSE;
				changed = TRUE;
			}
		}

		if(copy)
			*p++ = *oldArgs++;
		/* restart copying on a new parameter */
		else if(*oldArgs++ == '&')
			copy = TRUE;
	}

	/* if the last character is a ? or &, strip it */
	if(strlen(newArgs) >= 1 && (*(p-1) == '&' || *(p-1) == '?'))
		p--;
	/* null terminate the string */
	*p = '\0';
	
	if(c->CASDebug && changed == TRUE)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Modified r->args (old '%s', new '%s')", r->args, newArgs);

	if(strlen(newArgs) != 0 && changed == TRUE)
		/* r->args is by definition larger or the same size than newArgs, so strcpy() is safe */
		strcpy(r->args, newArgs);
	else if(strlen(newArgs) == 0)
		r->args = NULL;

	return changed;
}

static char *getCASTicket(request_rec *r)
{
	char *tokenizerCtx, *ticket, *args, *rv = NULL;
	apr_byte_t ticketFound = FALSE;

	if(r->args == NULL || strlen(r->args) == 0)
		return NULL;

	args = apr_pstrndup(r->pool, r->args, strlen(r->args));
	/* tokenize on & to find the 'ticket' parameter */
	ticket = apr_strtok(args, "&", &tokenizerCtx);
	do {
		if(strncmp(ticket, "ticket=", 7) == 0) {
			// tickets must begin with ST- or PT- except for PGT and PGT-IOU's which we are not handling anyway
			if(strncmp((ticket + 7), "ST-", 3) == 0 || strncmp((ticket + 7), "PT-", 3) == 0) {
				ticketFound = TRUE;
				/* skip to the meat of the parameter (the value after the '=') */
				ticket += 7; 
				rv = apr_pstrdup(r->pool, ticket);
				break;
			}
		}
		ticket = apr_strtok(NULL, "&", &tokenizerCtx);
		/* no more parameters */
		if(ticket == NULL)
			break;
	} while (ticketFound == FALSE);

	return rv;
}

static char *getCASCookie(request_rec *r, char *cookieName)
{
	char *cookie, *tokenizerCtx, *rv = NULL;
	apr_byte_t cookieFound = FALSE;
	char *cookies = apr_pstrdup(r->pool, (char *) apr_table_get(r->headers_in, "Cookie"));

	if(cookies != NULL) {
		/* tokenize on ; to find the cookie we want */
		cookie = apr_strtok(cookies, ";", &tokenizerCtx);
		do {
			while (cookie != NULL && *cookie == ' ')
				cookie++;
			if(strncmp(cookie, cookieName, strlen(cookieName)) == 0) {
				cookieFound = TRUE;
				/* skip to the meat of the parameter (the value after the '=') */
				cookie += (strlen(cookieName)+1);
				rv = apr_pstrdup(r->pool, cookie);
			}
			cookie = apr_strtok(NULL, ";", &tokenizerCtx);
		/* no more parameters */
		if(cookie == NULL)
			break;
		} while (cookieFound == FALSE);
	}

	return rv;
}

static void setCASCookie(request_rec *r, char *cookieName, char *cookieValue, apr_byte_t secure)
{
	char *headerString, *currentCookies, *pathPrefix = "";
	cas_cfg *c = ap_get_module_config(r->server->module_config, &auth_cas_module);

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering setCASCookie()");

	if(c->CASRootProxiedAs.is_initialized)
		pathPrefix = urlEncode(r, c->CASRootProxiedAs.path, " ");

	headerString = apr_psprintf(r->pool, "%s=%s%s;Path=%s%s%s%s%s", cookieName, cookieValue, (secure ? ";Secure" : ""), pathPrefix, urlEncode(r, getCASScope(r), " "), (c->CASCookieDomain != NULL ? ";Domain=" : ""), (c->CASCookieDomain != NULL ? c->CASCookieDomain : ""), (c->CASCookieHttpOnly != FALSE ? "; HttpOnly" : ""));

	/* use r->err_headers_out so we always print our headers (even on 302 redirect) - headers_out only prints on 2xx responses */
	apr_table_add(r->err_headers_out, "Set-Cookie", headerString);

	/*
	 * There is a potential problem here.  If CASRenew is on and a user requests 'http://example.com/xyz/'
	 * then they are bounced out to the CAS server and they come back with a ticket.  This ticket is validated
	 * and then this function (setCASCookie) is installed.  However, mod_dir will create a subrequest to
	 * point them to some DirectoryIndex value.  mod_auth_cas will see this new request (with no ticket since
	 * we removed it, but it would be invalid anyway since it was already validated at the CAS server)
	 * and redirect the user back to the CAS server (this time appending 'index.html' or something similar
	 * to the request) requiring two logins.  By adding this cookie to the incoming headers, when the
	 * subrequest is sent, they will use their established session.
	 */
	if((currentCookies = (char *) apr_table_get(r->headers_in, "Cookie")) == NULL)
		apr_table_add(r->headers_in, "Cookie", headerString);
	else
		apr_table_set(r->headers_in, "Cookie", (apr_pstrcat(r->pool, headerString, ";", currentCookies, NULL)));
	
	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Adding outgoing header: Set-Cookie: %s", headerString);


	return;
}

/* 
 * The CAS protocol spec 2.1.1 says the URL value MUST be URL-encoded as described in 2.2 of RFC 1738.
 * The rfc1738 array below represents the 'unsafe' characters from that section.  No encoding is performed
 * on 'control characters' (0x00-0x1F) or characters not used in US-ASCII (0x80-0xFF) - is this a problem?
 * 7/25/2009 - add '+' to list of characters to escape
 */

static char *escapeString(request_rec *r, char *str)
{
	char *rfc1738 = "+ <>\"%{}|\\^~[]`;/?:@=&#";

	return(urlEncode(r, str, rfc1738));

}

static char *urlEncode(request_rec *r, char *str, char *charsToEncode)
{
	char *rv, *p, *q;
	size_t i, j, size;
	char escaped = FALSE;

	if(str == NULL)
		return "";

	size = strlen(str) + 1; /* add 1 for terminating NULL */

	for(i = 0; i < size; i++) {
		for(j = 0; j < strlen(charsToEncode); j++) {
			if(str[i] == charsToEncode[j]) {
				/* allocate 2 extra bytes for the escape sequence (' ' -> '%20') */
				size += 2;
				break;
			}
		}
	}
	/* allocate new memory to return the encoded URL */
	p = rv = apr_pcalloc(r->pool, size);
	q = str;

	do {
		escaped = FALSE;
		for(i = 0; i < strlen(charsToEncode); i++) {
			if(*q == charsToEncode[i]) {
				sprintf(p, "%%%x", charsToEncode[i]);
				p+= 3;
				escaped = TRUE;
				break;
			}
		}
		if(escaped == FALSE) {
			*p++ = *q;
		}

		q++;
	} while (*q != '\0');
	*p = '\0';

	return(rv);
}

/* functions related to the local cache */
static apr_byte_t readCASCacheFile(request_rec *r, cas_cfg *c, char *name, cas_cache_entry *cache)
{
	apr_off_t begin = 0;
	apr_file_t *f;
	apr_finfo_t fi;
	apr_xml_parser *parser;
	apr_xml_doc *doc;
	apr_xml_elem *e;
	apr_status_t rv;
	char errbuf[CAS_MAX_ERROR_SIZE];
	char *path;
	const char *val;
	int i;

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering readCASCacheFile()");

	/* first, validate that cookie looks like an MD5 string */
	if(strlen(name) != APR_MD5_DIGESTSIZE*2) {
		if(c->CASDebug)
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Invalid cache cookie length for '%s', (expecting %d, got %d)", name, APR_MD5_DIGESTSIZE*2, (int) strlen(name));
		return FALSE;
	}

	for(i = 0; i < APR_MD5_DIGESTSIZE*2; i++) {
		if((name[i] < 'a' || name[i] > 'f') && (name[i] < '0' || name[i] > '9')) {
			if(c->CASDebug)
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Invalid character in cache cookie '%s' (%c)", name, name[i]);
			return FALSE;
		}
	}

	/* fix MAS-4 JIRA issue */
	if(apr_stat(&fi, c->CASCookiePath, APR_FINFO_TYPE, r->pool) == APR_INCOMPLETE) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: Could not find Cookie Path '%s'", c->CASCookiePath);
		return FALSE;
	}

	if(fi.filetype != APR_DIR || c->CASCookiePath[strlen(c->CASCookiePath)-1] != '/') {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: Cookie Path '%s' is not a directory or does not end in a trailing '/'!", c->CASCookiePath);
		return FALSE;
	}
	/* end MAS-4 JIRA issue */

	/* open the file if it exists and make sure that the ticket has not expired */
	path = apr_psprintf(r->pool, "%s%s", c->CASCookiePath, name);

	if(apr_file_open(&f, path, APR_FOPEN_READ, APR_OS_DEFAULT, r->pool) != APR_SUCCESS) {
		if(c->CASDebug)
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Cache entry '%s' could not be opened", name);
		return FALSE;
	}

	apr_file_lock(f, APR_FLOCK_SHARED);

	/* read the various values we store */
	apr_file_seek(f, APR_SET, &begin);

	rv = apr_xml_parse_file(r->pool, &parser, &doc, f, CAS_MAX_XML_SIZE);
	if(rv != APR_SUCCESS) {
		if(parser == NULL) {
			/*
			 * apr_xml_parse_file can fail early enough that the parser value is left uninitialized.
			 * In this case, we'll use apr_strerror and avoid calling apr_xml_parser_geterror, which
			 * segfaults with a null parser.
			 * patch to resolve this provided by Chris Adams of Yale
			 */
			apr_strerror(rv, errbuf, sizeof(errbuf));
		} else {
			apr_xml_parser_geterror(parser, errbuf, sizeof(errbuf));
		}

		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: Error parsing XML content for '%s' (%s)", name, errbuf);
		return FALSE;
	}

	e = doc->root->first_child;
	/* XML structure: 
 	 * cacheEntry
	 *	attr
	 *	attr
	 *	...
 	 */

	/* initialize things to sane values */
	cache->user = NULL;
	cache->issued = 0;
	cache->lastactive = 0;
	cache->path = "";
	cache->renewed = FALSE;
	cache->secure = FALSE;
	cache->ticket = NULL;
	cache->attrs = NULL;

	do {
		if(e == NULL)
			continue;

		/* determine textual content of this element */
		apr_xml_to_text(r->pool, e, APR_XML_X2T_INNER, NULL, NULL, &val, NULL);

		if (apr_strnatcasecmp(e->name, "user") == 0)
			cache->user = apr_pstrndup(r->pool, val, strlen(val));
		else if (apr_strnatcasecmp(e->name, "issued") == 0) {
			if(sscanf(val, "%" APR_TIME_T_FMT, &(cache->issued)) != 1)
				return FALSE;
		} else if (apr_strnatcasecmp(e->name, "lastactive") == 0) {
			if(sscanf(val, "%" APR_TIME_T_FMT, &(cache->lastactive)) != 1)
				return FALSE;
		} else if (apr_strnatcasecmp(e->name, "path") == 0)
			cache->path = apr_pstrndup(r->pool, val, strlen(val));
		else if (apr_strnatcasecmp(e->name, "renewed") == 0)
			cache->renewed = TRUE;
		else if (apr_strnatcasecmp(e->name, "secure") == 0)
			cache->secure = TRUE;
		else if (apr_strnatcasecmp(e->name, "ticket") == 0)
			cache->ticket = apr_pstrndup(r->pool, val, strlen(val));
		else if (apr_strnatcasecmp(e->name, "attributes") == 0) {
			apr_xml_elem *attrs = e->first_child;
			cas_saml_attr **attrtail = &cache->attrs;
			while(attrs != NULL) {
				cas_saml_attr *csa =
					apr_pcalloc(r->pool, sizeof(cas_saml_attr));
				apr_xml_attr *a = attrs->attr;
				apr_xml_elem *v = attrs->first_child;
				cas_saml_attr_val **valtail = &csa->values;
				csa->attr = apr_pstrndup(r->pool, a->value, strlen(a->value));
				csa->values = NULL;
				csa->next = NULL;
				while(v != NULL) {
					const char *s = NULL;
					cas_saml_attr_val *csav =
						apr_pcalloc(r->pool, sizeof(cas_saml_attr_val));
					apr_xml_to_text(r->pool, v, APR_XML_X2T_INNER,
					    NULL, NULL, &s, NULL);
					csav->value =
						apr_pstrndup(r->pool, s, strlen(s));
					csav->next = NULL;
					*valtail = csav;
					valtail = &csav->next;
					v = v->next;
				}
				*attrtail = csa;
				attrtail = &csa->next;
				attrs = attrs->next;
			}
		}
		else
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: Unknown cookie attribute '%s'", e->name);
		e = e->next;
	} while (e != NULL);

	apr_file_unlock(f);
	apr_file_close(f);
	return TRUE;
}

static void CASCleanCache(request_rec *r, cas_cfg *c)
{
	apr_time_t lastClean;
	apr_off_t begin = 0;
	char *path;
	apr_file_t *metaFile, *cacheFile;
	char line[64];
	apr_status_t i;
	cas_cache_entry cache;
	apr_dir_t *cacheDir;
	apr_finfo_t fi;

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering CASCleanCache()");

	path = apr_psprintf(r->pool, "%s.metadata", c->CASCookiePath);


	if(apr_file_open(&metaFile, path, APR_FOPEN_READ|APR_FOPEN_WRITE, APR_FPROT_UREAD|APR_FPROT_UWRITE, r->pool) != APR_SUCCESS) {
		/* file does not exist or cannot be opened - try and create it */
		if((i = apr_file_open(&metaFile, path, (APR_FOPEN_WRITE|APR_FOPEN_CREATE), (APR_FPROT_UREAD|APR_FPROT_UWRITE), r->pool)) != APR_SUCCESS) {
			apr_strerror(i, line, sizeof(line));
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "MOD_AUTH_CAS: Could not create cache metadata file '%s': %s", path, line);
			return;
		}
	}

	apr_file_lock(metaFile, APR_FLOCK_EXCLUSIVE);
	apr_file_seek(metaFile, APR_SET, &begin);

	/* if the file was not created on this method invocation (APR_FOPEN_READ is not used above during creation) see if it is time to clean the cache */
	if((apr_file_flags_get(metaFile) & APR_FOPEN_READ) != 0) {
		apr_file_gets(line, sizeof(line), metaFile);
		if(sscanf(line, "%" APR_TIME_T_FMT, &lastClean) != 1) { /* corrupt file */
			apr_file_unlock(metaFile);
			apr_file_close(metaFile);
			apr_file_remove(path, r->pool);
			if(c->CASDebug)
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Cache metadata file is corrupt");
			return;
		}
		if(lastClean > (apr_time_now()-(c->CASCacheCleanInterval*((apr_time_t) APR_USEC_PER_SEC)))) { /* not enough time has elapsed */
			/* release the locks and file descriptors that we no longer need */
			apr_file_unlock(metaFile);
			apr_file_close(metaFile);
			if(c->CASDebug)
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Insufficient time elapsed since last cache clean");
			return;
		}

		apr_file_seek(metaFile, APR_SET, &begin);
		apr_file_trunc(metaFile, begin);
	}

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Beginning cache clean");

	apr_file_printf(metaFile, "%" APR_TIME_T_FMT "\n", apr_time_now());
	apr_file_unlock(metaFile);
	apr_file_close(metaFile);

	/* read all the files in the directory */
	if(apr_dir_open(&cacheDir, c->CASCookiePath, r->pool) != APR_SUCCESS) {
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "MOD_AUTH_CAS: Error opening cache directory '%s' for cleaning", c->CASCookiePath);
	}

	do {
		i = apr_dir_read(&fi, APR_FINFO_NAME, cacheDir);
		if(i == APR_SUCCESS) {
			if(fi.name[0] == '.') /* skip hidden files and parent directories */
				continue;
			path = apr_psprintf(r->pool, "%s%s", c->CASCookiePath, fi.name);
			if(c->CASDebug)
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Processing cache file '%s'", fi.name);

			if(apr_file_open(&cacheFile, path, APR_FOPEN_READ, APR_OS_DEFAULT, r->pool) != APR_SUCCESS) {
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: Unable to clean cache entry '%s'", path);
				continue;
			}
			if(readCASCacheFile(r, c, (char *) fi.name, &cache) == TRUE) {
				if((c->CASTimeout > 0 && (cache.issued < (apr_time_now()-(c->CASTimeout*((apr_time_t) APR_USEC_PER_SEC))))) || cache.lastactive < (apr_time_now()-(c->CASIdleTimeout*((apr_time_t) APR_USEC_PER_SEC)))) {
					/* delete this file since it is no longer valid */
					apr_file_close(cacheFile);
					deleteCASCacheFile(r, (char *) fi.name);
					if(c->CASDebug)
						ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Removing expired cache entry '%s'", fi.name);
				}
			} else {
				if(c->CASDebug)
					ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Removing corrupt cache entry '%s'", fi.name);
				/* corrupt file */
				apr_file_close(cacheFile);
				deleteCASCacheFile(r, (char *) fi.name);
			}
		}
	} while (i == APR_SUCCESS);
	apr_dir_close(cacheDir);

}

static apr_byte_t writeCASCacheEntry(request_rec *r, char *name, cas_cache_entry *cache, apr_byte_t exists)
{
	char *path;
	apr_file_t *f;
	apr_off_t begin = 0;
	int cnt = 0;
	apr_status_t i = APR_EGENERAL;
	apr_byte_t lock = FALSE;
	cas_cfg *c = ap_get_module_config(r->server->module_config, &auth_cas_module);

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering writeCASCacheEntry()");

	path = apr_psprintf(r->pool, "%s%s", c->CASCookiePath, name);

	if(exists == FALSE) {
		if((i = apr_file_open(&f, path, APR_FOPEN_CREATE|APR_FOPEN_WRITE|APR_EXCL, APR_FPROT_UREAD|APR_FPROT_UWRITE, r->pool)) != APR_SUCCESS) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: Cookie file '%s' could not be created: %s", path, apr_strerror(i, name, strlen(name)));
			return FALSE;
		}
	} else {
		for(cnt = 0; ; cnt++) {
			/* gracefully handle broken file system permissions by trying 3 times to create the file, otherwise failing */
			if(cnt >= 3) {
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: Cookie file '%s' could not be opened: %s", path, apr_strerror(i, name, strlen(name)));
				return FALSE;
			}
			if(apr_file_open(&f, path, APR_FOPEN_READ|APR_FOPEN_WRITE, APR_FPROT_UREAD|APR_FPROT_UWRITE, r->pool) == APR_SUCCESS)
				break;
			else
				apr_sleep(1000);
		}

		/* update the file with a new idle time if a write lock can be obtained */
		if(apr_file_lock(f, APR_FLOCK_EXCLUSIVE) != APR_SUCCESS) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: could not obtain an exclusive lock on %s", path);
			apr_file_close(f);
			return FALSE;
		} else
			lock = TRUE;
		apr_file_seek(f, APR_SET, &begin);
		apr_file_trunc(f, begin);
	}

	/* this is ultra-ghetto, but the APR really doesn't provide any facilities for easy DOM-style XML creation. */
	apr_file_printf(f, "<cacheEntry xmlns=\"http://uconn.edu/cas/mod_auth_cas\">\n");
	apr_file_printf(f, "<user>%s</user>\n", apr_xml_quote_string(r->pool, cache->user, TRUE));
	apr_file_printf(f, "<issued>%" APR_TIME_T_FMT "</issued>\n", cache->issued);
	apr_file_printf(f, "<lastactive>%" APR_TIME_T_FMT "</lastactive>\n", cache->lastactive);
	apr_file_printf(f, "<path>%s</path>\n", apr_xml_quote_string(r->pool, cache->path, TRUE));
	apr_file_printf(f, "<ticket>%s</ticket>\n", apr_xml_quote_string(r->pool, cache->ticket, TRUE));
	if(cache->attrs != NULL) {
		cas_saml_attr *a = cache->attrs;
		apr_file_printf(f, "<attributes>\n");
		while(a != NULL) {
			cas_saml_attr_val *av = a->values;
			apr_file_printf(f, "  <attribute name=\"%s\">\n", apr_xml_quote_string(r->pool, a->attr, TRUE));
			while(av != NULL) {
				apr_file_printf(f, "	<value>%s</value>\n", apr_xml_quote_string(r->pool, av->value, TRUE));
				av = av->next;
			}
			apr_file_printf(f, "  </attribute>\n");
			a = a->next;
		}
		apr_file_printf(f, "</attributes>\n");
	}
	if(cache->renewed != FALSE)
		apr_file_printf(f, "<renewed />\n");
	if(cache->secure != FALSE)
		apr_file_printf(f, "<secure />\n");
	apr_file_printf(f, "</cacheEntry>\n");

	if(lock != FALSE)
		apr_file_unlock(f);

	apr_file_close(f);

	return TRUE;
}

static char *createCASCookie(request_rec *r, char *user, cas_saml_attr *attrs, char *ticket)
{
	char *path, *buf, *rv;
	apr_file_t *f;
	cas_cache_entry e;
	int i;
	cas_cfg *c = ap_get_module_config(r->server->module_config, &auth_cas_module);
	cas_dir_cfg *d = ap_get_module_config(r->per_dir_config, &auth_cas_module);
	buf = apr_pcalloc(r->pool, c->CASCookieEntropy);

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering createCASCookie()");

	CASCleanCache(r, c);

	e.user = user;
	e.issued = apr_time_now();
	e.lastactive = apr_time_now();
	e.path = getCASPath(r);
	e.renewed = (d->CASRenew == NULL ? 0 : 1);
	e.secure = (isSSL(r) == TRUE ? 1 : 0);
	e.ticket = ticket;
	e.attrs = attrs;

	/* this may block since this reads from /dev/random - however, it hasn't been a problem in testing */
	apr_generate_random_bytes((unsigned char *) buf, c->CASCookieEntropy);
	rv = (char *) ap_md5_binary(r->pool, (unsigned char *) buf, c->CASCookieEntropy);

	/* 
	 * Associate this text with user for lookups later.  By using files instead of 
	 * shared memory the advantage of NFS shares in a clustered environment or a 
	 * memory based file systems can be used at the expense of potentially some performance
	 */
	if(writeCASCacheEntry(r, rv, &e, FALSE) == FALSE)
		return NULL;

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Cookie '%s' created for user '%s'", rv, user);

	buf = (char *) ap_md5_binary(r->pool, (const unsigned char *) ticket, (int) strlen(ticket));
	path = apr_psprintf(r->pool, "%s.%s", c->CASCookiePath, buf);

	if((i = apr_file_open(&f, path, APR_FOPEN_CREATE|APR_FOPEN_WRITE|APR_EXCL, APR_FPROT_UREAD|APR_FPROT_UWRITE, r->pool)) != APR_SUCCESS) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: Service Ticket to Cookie map file '%s' could not be created: %s", path, apr_strerror(i, buf, strlen(buf)));
		return FALSE;
	} else {
		apr_file_printf(f, "%s", rv);
		apr_file_close(f);
	}
	
	return(apr_pstrdup(r->pool, rv));
}

static void expireCASST(request_rec *r, const char *ticketname)
{
	char *ticket, *path;
	char line[APR_MD5_DIGESTSIZE*2+1];
	apr_file_t *f;
	apr_size_t bytes = APR_MD5_DIGESTSIZE*2;
	cas_cfg *c = ap_get_module_config(r->server->module_config, &auth_cas_module);

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering expireCASST()");

	ticket = (char *) ap_md5_binary(r->pool, (unsigned char *) ticketname, (int) strlen(ticketname));
	line[APR_MD5_DIGESTSIZE*2] = '\0';

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Expiring service ticket '%s' (%s)", ticketname, ticket);

	path = apr_psprintf(r->pool, "%s.%s", c->CASCookiePath, ticket);

	if(apr_file_open(&f, path, APR_FOPEN_READ, APR_OS_DEFAULT, r->pool) != APR_SUCCESS) {
		ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "MOD_AUTH_CAS: Service Ticket mapping to Cache entry '%s' could not be opened (ticket %s - expired already?)", path, ticketname);
		return;
	}
	
	if(apr_file_read(f, &line, &bytes) != APR_SUCCESS) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: Service Ticket mapping to Cache entry '%s' could not be read (ticket %s)", path, ticketname);
		return;
	}
	
	if(bytes != APR_MD5_DIGESTSIZE*2) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: Service Ticket mapping to Cache entry '%s' incomplete (read %" APR_SIZE_T_FMT ", expected %d, ticket %s)", path, bytes, APR_MD5_DIGESTSIZE*2, ticketname);
		return;
	}

	apr_file_close(f);

	deleteCASCacheFile(r, line);
}

static void CASSAMLLogout(request_rec *r, char *body)
{
	apr_xml_doc *doc;
	apr_xml_elem *node;
	char *line;
	apr_xml_parser *parser = apr_xml_parser_create(r->pool);
	cas_cfg *c = ap_get_module_config(r->server->module_config, &auth_cas_module);

	if(body != NULL && strncmp(body, "logoutRequest=", 14) == 0) {
		body += 14;
		line = (char *) body;

		/* convert + to ' ' or else the XML won't parse right */
		do { 
			if(*line == '+')
				*line = ' ';
			line++;
		} while (*line != '\0');

		ap_unescape_url((char *) body);

		if(c->CASDebug)
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "SAML Logout Request: %s", body);

		/* parse the XML response */
		if(apr_xml_parser_feed(parser, body, strlen(body)) != APR_SUCCESS) {
			line = apr_pcalloc(r->pool, 512);
			apr_xml_parser_geterror(parser, line, 512);
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: error parsing SAML logoutRequest: %s (incomplete SAML body?)", line);
			return;
		}
		/* retrieve a DOM object */
		if(apr_xml_parser_done(parser, &doc) != APR_SUCCESS) {
			line = apr_pcalloc(r->pool, 512);
			apr_xml_parser_geterror(parser, line, 512);
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: error retrieving XML document for SAML logoutRequest: %s", line);
			return;
		}

		node = doc->root->first_child;
		while(node != NULL) {
			if(apr_strnatcmp(node->name, "SessionIndex") == 0 && node->first_cdata.first != NULL) {
				expireCASST(r, node->first_cdata.first->text);
				return;
			}
			node = node->next;
		}
	}

	return;
}

static void deleteCASCacheFile(request_rec *r, char *cookieName)
{
	char *path, *ticket;
	cas_cache_entry e;
	cas_cfg *c = ap_get_module_config(r->server->module_config, &auth_cas_module);

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering deleteCASCacheFile()");

	/* we need this to get the ticket */
	readCASCacheFile(r, c, cookieName, &e);

	/* delete their cache entry */
	path = apr_psprintf(r->pool, "%s%s", c->CASCookiePath, cookieName);
	apr_file_remove(path, r->pool);

	/* delete the ticket -> cache entry mapping */
	ticket = (char *) ap_md5_binary(r->pool, (unsigned char *) e.ticket, strlen(e.ticket));
	path = apr_psprintf(r->pool, "%s.%s", c->CASCookiePath, ticket);
	apr_file_remove(path, r->pool);

	return;
}

/* functions related to validation of tickets/cache entries */
static apr_byte_t isValidCASTicket(request_rec *r, cas_cfg *c, char *ticket, char **user, cas_saml_attr **attrs)
{
	char *line;
	apr_xml_doc *doc;
	apr_xml_elem *node;
	apr_xml_attr *attr;
	apr_xml_parser *parser = apr_xml_parser_create(r->pool);
	const char *response = getResponseFromServer(r, c, ticket);
	const char *value = NULL;
	
	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering isValidCASTicket()");

	if(response == NULL)
		return FALSE;

	if(c->CASDebug) {
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "MOD_AUTH_CAS: response = %s", response);
	}

	if(c->CASVersion == 1) {
		do {
			line = ap_getword(r->pool, &response, '\n');
			/* premature response end */
			if(strlen(line) == 0) {
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: premature end of CASv1 response (yes/no not present)");
				return FALSE;
			}

		} while (apr_strnatcmp(line, "no") != 0 && apr_strnatcmp(line, "yes") != 0);
		
		if(apr_strnatcmp(line, "no") == 0) {
			return FALSE;
		}

		line = ap_getword(r->pool, &response, '\n');
		/* premature response end */
		if(line == NULL || strlen(line) == 0) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: premature end of CASv1 response (username not present)");
			return FALSE;
		}

		*user = apr_pstrndup(r->pool, line, strlen(line));
		return TRUE;
	} else if(c->CASVersion == 2) {
		/* parse the XML response */
		if(apr_xml_parser_feed(parser, response, strlen(response)) != APR_SUCCESS) {
			line = apr_pcalloc(r->pool, 512);
			apr_xml_parser_geterror(parser, line, 512);
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: error parsing CASv2 response: %s", line);
			return FALSE;
		}
		/* retrieve a DOM object */
		if(apr_xml_parser_done(parser, &doc) != APR_SUCCESS) {
			line = apr_pcalloc(r->pool, 512);
			apr_xml_parser_geterror(parser, line, 512);
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: error retrieving XML document for CASv2 response: %s", line);
			return FALSE;
		}
		if(c->CASValidateSAML == TRUE) {
			int success = 0;
			node = doc->root->first_child;
			// Header
			if(node != NULL) {
				node = node->next;
				// Body
				if(node != NULL) {
					node = node->first_child;
					// Response
					if(node != NULL) {
						apr_xml_elem *aNode = NULL;
						node = node->first_child;
						aNode = node->next;
						// Status
						if(node != NULL) {
							node = node->first_child;
							// StatusCode
							if(node != NULL) {
								apr_xml_attr *attr = node->attr;
								while(attr != NULL) {
									if(apr_strnatcmp(attr->name, "Value") == 0) {
										if(apr_strnatcmp(attr->value, "samlp:Success") == 0) {
											success = 1;
										}
										break;
									}
									attr = attr->next;
								}
							}
						}
						// Assertion
						if(success && aNode != NULL) {
							aNode = aNode->first_child;
							// Conditions
							if(aNode != NULL) {
								aNode = aNode->next;
								// AttributeStatement
								if(aNode != NULL) {
									apr_xml_elem *as = aNode;
									aNode = aNode->first_child;
									// Subject
									if(aNode != NULL) {
										aNode = aNode->first_child;
										// NameIdentifier
										if(aNode != NULL) {
											apr_xml_to_text(r->pool, aNode, APR_XML_X2T_INNER,
												NULL, NULL, (const char **)user, NULL);
										}
									}
									if(as != NULL) {
										cas_saml_attr **attrtail = attrs;
										as = as->first_child;
										while(as != NULL) {
											if(apr_strnatcmp(as->name, "Attribute") == 0) {
												apr_xml_attr *attr = as->attr;
												while(attr != NULL) {
													if(apr_strnatcmp(attr->name, "AttributeName") == 0) {
														cas_saml_attr *csa = apr_pcalloc(r->pool, sizeof(cas_saml_attr));
														cas_saml_attr_val **valtail = &csa->values;
														apr_xml_elem *a =
															as->first_child;
														csa->attr = apr_pstrndup(r->pool, attr->value, strlen(attr->value));
														csa->values = NULL;
														csa->next = NULL;

														while(a != NULL) {
															cas_saml_attr_val *csav = apr_pcalloc(r->pool, sizeof(cas_saml_attr_val));
															apr_xml_to_text(r->pool, a, APR_XML_X2T_INNER,
																NULL, NULL, (const char **)&csav->value, NULL);
															csav->next = NULL;
															*valtail = csav;
															valtail = &csav->next;
															a = a->next;
														}
														*attrtail = csa;
														attrtail = &csa->next;
													}
													attr = attr->next;
												}
											}
											as = as->next;
										}
									}
								}
							}
						}
					}
				}
			}
			if(success) {
				return TRUE;
			}
		} else {
			/* XML tree:
			 * ServiceResponse
			 *  ->authenticationSuccess
			 *      ->user
			 *      ->proxyGrantingTicket
			 *  ->authenticationFailure (code)
			 */
			node = doc->root->first_child;
			if(apr_strnatcmp(node->name, "authenticationSuccess") == 0) {
				node = node->first_child;
				while(node != NULL && apr_strnatcmp(node->name, "user") != 0)
					node = node->next;
				if(node != NULL) {
					apr_xml_to_text(r->pool, node, APR_XML_X2T_INNER, NULL, NULL, &value, NULL);
					*user = apr_pstrndup(r->pool, value, strlen(value));
					return TRUE;
				}
			} else if (apr_strnatcmp(node->name, "authenticationFailure") == 0) {
				attr = node->attr;
				while(attr != NULL && apr_strnatcmp(attr->name, "code") != 0)
					attr = attr->next;

				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: %s", (attr == NULL ? "Unknown Error" : attr->value));

				return FALSE;
			}
		}
	}
	return FALSE;
}

static apr_byte_t isValidCASCookie(request_rec *r, cas_cfg *c, char *cookie, char **user, cas_saml_attr **attrs)
{
	char *path;
	cas_cache_entry cache;
	cas_dir_cfg *d = ap_get_module_config(r->per_dir_config, &auth_cas_module);

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering isValidCASCookie()");

	/* corrupt or invalid file */
	if(readCASCacheFile(r, c, cookie, &cache) != TRUE) {
		if(c->CASDebug)
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Cookie '%s' is corrupt or invalid", cookie);
		return FALSE;
	}

	path = apr_psprintf(r->pool, "%s%s", c->CASCookiePath, cookie);

	/* 
	 * mitigate session hijacking by not allowing cookies transmitted in the clear to be submitted
	 * for HTTPS URLs and by voiding HTTPS cookies sent in the clear
	 */
	if( (isSSL(r) == TRUE && cache.secure == FALSE) || (isSSL(r) == FALSE && cache.secure == TRUE) ) {
		/* delete this file since it is no longer valid */
		deleteCASCacheFile(r, cookie);
		if(c->CASDebug)
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Cookie '%s' not transmitted via proper HTTP(S) channel, expiring", cookie);
		CASCleanCache(r, c);
		return FALSE;
	}

	if(cache.issued < (apr_time_now()-(c->CASTimeout*((apr_time_t) APR_USEC_PER_SEC))) || cache.lastactive < (apr_time_now()-(c->CASIdleTimeout*((apr_time_t) APR_USEC_PER_SEC)))) {
		/* delete this file since it is no longer valid */
		deleteCASCacheFile(r, cookie);
		if(c->CASDebug)
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Cookie '%s' is expired, deleting", cookie);
		CASCleanCache(r, c);
		return FALSE;
	}

	/* see if this cookie contained 'renewed' credentials if this directory requires it */
	if(cache.renewed == FALSE && d->CASRenew != NULL) {
		if(c->CASDebug)
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Cookie '%s' does not contain renewed credentials", cookie);
		return FALSE;
	} else if(d->CASRenew != NULL && cache.renewed == TRUE) {
		/* make sure the paths match */
		if(strncasecmp(cache.path, getCASScope(r), strlen(getCASScope(r))) != 0) {
			if(c->CASDebug)
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Cookie '%s' does not contain renewed credentials for scope '%s' (path '%s')", cookie, getCASScope(r), getCASPath(r));
			return FALSE;
		}
	}

	/* set the user */
	*user = apr_pstrndup(r->pool, cache.user, strlen(cache.user));

	if(cache.attrs != NULL) {
		cas_saml_attr *ca = cache.attrs;
		cas_saml_attr **attrtail = attrs;
		while(ca != NULL) {
			cas_saml_attr *csa = apr_pcalloc(r->pool , sizeof(cas_saml_attr));
			cas_saml_attr_val **valtail = &csa->values;
			cas_saml_attr_val *vals = ca->values;
			csa->attr = apr_pstrndup(r->pool, ca->attr, strlen(ca->attr));
			csa->values = NULL;
			csa->next = NULL;
			while(vals != NULL) {
				cas_saml_attr_val *csav = apr_pcalloc(r->pool, sizeof(cas_saml_attr_val));
				csav->value = apr_pstrndup(r->pool, vals->value, strlen(vals->value));
				csav->next = NULL;
				*valtail = csav;
				valtail = &csav->next;
				vals = vals->next;
			}
			*attrtail = csa;
			attrtail = &csa->next;
			ca = ca->next;
		}
	}

	cache.lastactive = apr_time_now();
	if(writeCASCacheEntry(r, cookie, &cache, TRUE) == FALSE && c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Could not update cache entry for '%s'", cookie);

	return TRUE;
}


/*
 * from curl_easy_setopt documentation:
 * This function gets called by libcurl as soon as there 
 * is data received that needs to be saved. The size of the
 * data pointed to by ptr is size multiplied with nmemb, it
 * will not be zero terminated. Return the number of bytes
 * actually taken care of. If that amount differs from the 
 * amount passed to your function, it'll signal an error to the 
 * library. This will abort the transfer and return 
 * CURLE_WRITE_ERROR.
 */

static size_t cas_curl_write(void *ptr, size_t size, size_t nmemb, void *stream)
{
	cas_curl_buffer *curlBuffer = (cas_curl_buffer *) stream;

	if((nmemb*size) + curlBuffer->written >= CAS_MAX_RESPONSE_SIZE)
		return 0;

	memcpy((curlBuffer->buf + curlBuffer->written), ptr, (nmemb*size));
	curlBuffer->written += (nmemb*size);

	return (nmemb*size);
}

CURLcode cas_curl_ssl_ctx(CURL *curl, void *sslctx, void *parm)
{
	SSL_CTX *ctx = (SSL_CTX *) sslctx;
	cas_cfg *c = (cas_cfg *)parm;

	if(c->CASValidateServer != FALSE)
		SSL_CTX_set_verify_depth(ctx, c->CASValidateDepth);

	return CURLE_OK;
}

static char *getResponseFromServer (request_rec *r, cas_cfg *c, char *ticket)
{
	char curlError[CURL_ERROR_SIZE];
	apr_finfo_t f;
	apr_uri_t validateURL;
	cas_curl_buffer curlBuffer;
	struct curl_slist *headers = NULL;

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "entering getResponseFromServer()");

	CURL *curl = curl_easy_init();

	curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L); 
	curl_easy_setopt(curl, CURLOPT_HEADER, 0L); 
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP|CURLPROTO_HTTPS);

	curlBuffer.written = 0;
	memset(curlBuffer.buf, '\0', sizeof(curlBuffer.buf));
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &curlBuffer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cas_curl_write);
	curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, cas_curl_ssl_ctx);
	curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, c);

#ifndef LIBCURL_NO_CURLPROTO
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP|CURLPROTO_HTTPS);
#endif

	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, (c->CASValidateServer != FALSE ? 1L : 0L));

	if(apr_stat(&f, c->CASCertificatePath, APR_FINFO_TYPE, r->pool) == APR_INCOMPLETE) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: Could not load CA certificate: %s", c->CASCertificatePath);
		return (NULL);
	}
	if(f.filetype == APR_DIR)
		curl_easy_setopt(curl, CURLOPT_CAPATH, c->CASCertificatePath);
	else if (f.filetype == APR_REG)
		curl_easy_setopt(curl, CURLOPT_CAINFO, c->CASCertificatePath);
	else {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MOD_AUTH_CAS: Could not process Certificate Authority: %s", c->CASCertificatePath);
		return (NULL);
	}

	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, (c->CASValidateServer != FALSE ? 2L : 0L));

	curl_easy_setopt(curl, CURLOPT_USERAGENT, "mod_auth_cas 1.0.9.1");

	if(c->CASValidateSAML == TRUE) {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		char *samlPayload = apr_psprintf(r->pool, "<?xml version=\"1.0\" encoding=\"utf-8\"?><SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\"><SOAP-ENV:Header/><SOAP-ENV:Body><samlp:Request xmlns:samlp=\"urn:oasis:names:tc:SAML:1.0:protocol\"  MajorVersion=\"1\" MinorVersion=\"1\"><samlp:AssertionArtifact>%s%s</samlp:AssertionArtifact></samlp:Request></SOAP-ENV:Body></SOAP-ENV:Envelope>",ticket, getCASRenew(r));
		headers = curl_slist_append(headers, "soapaction: http://www.oasis-open.org/committees/security");
		headers = curl_slist_append(headers, "cache-control: no-cache"); 
		headers = curl_slist_append(headers, "pragma: no-cache");
		headers = curl_slist_append(headers, "accept: text/xml"); 
		headers = curl_slist_append(headers, "content-type: text/xml"); 
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, samlPayload);
	} else
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

	memcpy(&validateURL, &c->CASValidateURL, sizeof(apr_uri_t));
	if(c->CASValidateSAML == FALSE)
		validateURL.query = apr_psprintf(r->pool, "service=%s&ticket=%s%s", getCASService(r, c), ticket, getCASRenew(r));
	else
		validateURL.query = apr_psprintf(r->pool, "TARGET=%s%s", getCASService(r, c), getCASRenew(r));

	curl_easy_setopt(curl, CURLOPT_URL, apr_uri_unparse(r->pool, &validateURL, 0));

	if(curl_easy_perform(curl) != CURLE_OK) {
		if(c->CASDebug)
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "MOD_AUTH_CAS: curl_easy_perform() failed (%s)", curlError);
		return (NULL);
	}

	if(headers != NULL)
		curl_slist_free_all(headers);

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Validation response: %s", curlBuffer.buf);

	curl_easy_cleanup(curl);
	return (apr_pstrndup(r->pool, curlBuffer.buf, strlen(curlBuffer.buf)));
}

/* basic CAS module logic */
static int cas_authenticate(request_rec *r)
{
	char *cookieString = NULL;
	char *ticket = NULL;
	char *remoteUser = NULL;
	cas_saml_attr *attrs = NULL;
	cas_cfg *c;
	cas_dir_cfg *d;
	apr_byte_t ssl;
	apr_byte_t parametersRemoved = FALSE;
	apr_port_t port = r->connection->local_addr->port;
	apr_byte_t printPort = FALSE;
	
	char *newLocation = NULL;

	/* Do nothing if we are not the authenticator */
	if(ap_auth_type(r) == NULL || apr_strnatcasecmp((const char *) ap_auth_type(r), "cas") != 0)
		return DECLINED;

	c = ap_get_module_config(r->server->module_config, &auth_cas_module);
	d = ap_get_module_config(r->per_dir_config, &auth_cas_module);

	/* Safety measure: scrub CAS user/attribute headers from the incoming request. */
	if (ap_is_initial_req(r) && d->CASScrubRequestHeaders) {
		/* CAS-User header can be simply unset. */
		if (d->CASAuthNHeader != NULL) {
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "MOD_AUTH_CAS: Removed inbound CASAuthNHeader! (foul play?)");
			apr_table_unset(r->headers_in, d->CASAuthNHeader);
		}

		/* Filtering the SAML attributes is a bit more laborious: we copy the headers table
		 * into a new table, while filtering out headers that start with CASattributePrefix.
		 */
		if (c->CASValidateSAML) {
			const int attributePrefixLength = strlen(c->CASAttributePrefix);

			/* It's not illegal to set CASAttributePrefix to an empty string,
			 * but don't scrub if such is the case: strncasecmp(string, "", 0)
			 * always matches, it'd scrub ALL the request headers!
			 */
			if (attributePrefixLength > 0) {
				const apr_array_header_t *const h = apr_table_elts(r->headers_in);
				const apr_table_entry_t *const e = (const apr_table_entry_t *)h->elts;
				int i;

				apr_table_t *headers_in = apr_table_make(r->pool, h->nelts);
				for (i = 0; i < h->nelts; i++) {
					if (e[i].key != NULL && 0 != strncasecmp(e[i].key, c->CASAttributePrefix, attributePrefixLength)) {
						apr_table_addn(headers_in, e[i].key, e[i].val);
					} else {
						ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "MOD_AUTH_CAS: Removed inbound request header(%s: %s)", e[i].key, e[i].val);
					}
				}
				r->headers_in = headers_in;
			}
		}
	}

	if(r->method_number == M_POST && c->CASSSOEnabled != FALSE) {
		/* read the POST data here to determine if it is a SAML LogoutRequest and handle accordingly */
		ap_add_input_filter("CAS", NULL, r, r->connection);
	}

	if(c->CASDebug)
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Entering cas_authenticate()");
	ssl = isSSL(r);

	/* the presence of a ticket overrides all */
	ticket = getCASTicket(r);
	cookieString = getCASCookie(r, (ssl ? d->CASSecureCookie : d->CASCookie));
	
	// only remove parameters if a ticket was found (makes no sense to do this otherwise)
	if(ticket != NULL)
		parametersRemoved = removeCASParams(r);

	/* first, handle the gateway case */
	if(d->CASGateway != NULL && strncmp(d->CASGateway, r->parsed_uri.path, strlen(d->CASGateway)) == 0 && ticket == NULL && cookieString == NULL) {
		cookieString = getCASCookie(r, d->CASGatewayCookie);
		if(cookieString == NULL) { /* they have not made a gateway trip yet */
			if(c->CASDebug)
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Gateway initial access (%s)", r->parsed_uri.path);
			setCASCookie(r, d->CASGatewayCookie, "TRUE", ssl);
			redirectRequest(r, c);
			return HTTP_MOVED_TEMPORARILY;
		} else {
			if(c->CASDebug)
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Gateway anonymous authentication (%s)", r->parsed_uri.path);
			/* do not set a user, but still allow anonymous access */
			return OK;
		}
	}

	/* now, handle when a ticket is present (this will also catch gateway users since ticket != NULL on their trip back) */
	if(ticket != NULL) {
		if(isValidCASTicket(r, c, ticket, &remoteUser, &attrs)) {
			cookieString = createCASCookie(r, remoteUser, attrs, ticket);

			/* if there was an error writing the cookie info to the file system */
			if(cookieString == NULL)
				return HTTP_INTERNAL_SERVER_ERROR;

			setCASCookie(r, (ssl ? d->CASSecureCookie : d->CASCookie), cookieString, ssl);
			r->user = remoteUser;
			if(d->CASAuthNHeader != NULL)
				apr_table_set(r->headers_in, d->CASAuthNHeader, remoteUser);
				
			if(parametersRemoved == TRUE) {
				if(ssl == TRUE && port != 443) 
					printPort = TRUE;
				else if(port != 80)
					printPort = TRUE;

				if(c->CASRootProxiedAs.is_initialized) {
						newLocation = apr_psprintf(r->pool, "%s%s%s%s", apr_uri_unparse(r->pool, &c->CASRootProxiedAs, 0), r->uri, ((r->args != NULL) ? "?" : ""), ((r->args != NULL) ? escapeString(r, r->args) : ""));
				} else {
#ifdef APACHE2_0
					if(printPort == TRUE)
						newLocation = apr_psprintf(r->pool, "%s://%s:%u%s%s%s", ap_http_method(r), r->server->server_hostname, r->connection->local_addr->port, r->uri, ((r->args != NULL) ? "?" : ""), ((r->args != NULL) ? r->args : ""));
					else
						newLocation = apr_psprintf(r->pool, "%s://%s%s%s%s", ap_http_method(r), r->server->server_hostname, r->uri, ((r->args != NULL) ? "?" : ""), ((r->args != NULL) ? r->args : ""));
#else
					if(printPort == TRUE)
						newLocation = apr_psprintf(r->pool, "%s://%s:%u%s%s%s", ap_http_scheme(r), r->server->server_hostname, r->connection->local_addr->port, r->uri, ((r->args != NULL) ? "?" : ""), ((r->args != NULL) ? r->args : ""));
					else
						newLocation = apr_psprintf(r->pool, "%s://%s%s%s%s", ap_http_scheme(r), r->server->server_hostname, r->uri, ((r->args != NULL) ? "?" : ""), ((r->args != NULL) ? r->args : ""));
#endif
				}
				apr_table_add(r->headers_out, "Location", newLocation);
				return HTTP_MOVED_TEMPORARILY;
			} else {
				return OK;
			}
		} else {
			/* sometimes, pages that automatically refresh will re-send the ticket parameter, so let's check any cookies presented or return an error if none */
			if(cookieString == NULL)
				return HTTP_UNAUTHORIZED;
		}
	}
	
	if(cookieString == NULL) {
		/* redirect the user to the CAS server since they have no cookie and no ticket */
		redirectRequest(r, c);
		return HTTP_MOVED_TEMPORARILY;
	} else {
		if(!ap_is_initial_req(r) && c->CASValidateSAML == FALSE) {
			/*
			 * MAS-27 fix:  copy the user from the initial request to prevent a hit on the backing
			 * store.  the 'gotcha' here is that we should preserve the SAML attributes, too.
			 * To accomplish this, apr_table_do() needs to be invoked to look for keys that
			 * match the CASAttributePrefix in r->main->headers_in, and then set them in
			 * this subrequest.  in the meantime, we will just accept the performance hit if
			 * validate SAML is on and re-read the cache file.
			 */
			if(r->main != NULL)
				remoteUser = r->main->user;
			else if (r->prev != NULL)
				remoteUser = r->prev->user;
			else {
				redirectRequest(r, c);
				return HTTP_MOVED_TEMPORARILY;
			}

			if (c->CASDebug)
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "recycling user '%s' from initial request for sub request", remoteUser);
		} else if(!isValidCASCookie(r, c, cookieString, &remoteUser, &attrs)) {
			remoteUser = NULL;
		}

		if(remoteUser) {
			r->user = remoteUser;
			if(d->CASAuthNHeader != NULL) {
				apr_table_set(r->headers_in, d->CASAuthNHeader, remoteUser);
 				if(attrs != NULL) {
 					cas_saml_attr *a = attrs;
 					while(a != NULL) {
 						cas_saml_attr_val *av = a->values;
 						char *csvs = NULL;
 						while(av != NULL) {
 							if(csvs != NULL) {
 								csvs = apr_psprintf(r->pool, "%s%s%s", csvs, c->CASAttributeDelimiter, av->value);
 							} else {
 								csvs = apr_psprintf(r->pool, "%s", av->value);
 							}
 							av = av->next;
 						}
 						apr_table_set(r->headers_in, apr_psprintf(r->pool, "%s%s", c->CASAttributePrefix, a->attr), csvs);
 						a = a->next;
 					}
 				}
 			}
			return OK;
		} else {
			/* maybe the cookie expired, have the user get a new service ticket */
			redirectRequest(r, c);
			return HTTP_MOVED_TEMPORARILY;
		}
	}

	return HTTP_UNAUTHORIZED;
}

#if (defined(OPENSSL_THREADS) && APR_HAS_THREADS)

/* shamelessly based on code from mod_ssl */
static void cas_ssl_locking_callback(int mode, int type, const char *file, int line) {
	if(type < ssl_num_locks) {
		if (mode & CRYPTO_LOCK)
			apr_thread_mutex_lock(ssl_locks[type]);
		else
			apr_thread_mutex_unlock(ssl_locks[type]);
	}
}

#ifdef OPENSSL_NO_THREADID
static unsigned long cas_ssl_id_callback(void) {
	return (unsigned long) apr_os_thread_current();
}
#else
static void cas_ssl_id_callback(CRYPTO_THREADID *id)
{
	CRYPTO_THREADID_set_numeric(id, (unsigned long) apr_os_thread_current());
}
#endif /* OPENSSL_NO_THREADID */


#endif /* defined(OPENSSL_THREADS) && APR_HAS_THREADS */

static apr_status_t cas_cleanup(void *data)
{
	server_rec *s = (server_rec *) data;
	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, "entering cas_cleanup()");

#if (defined (OPENSSL_THREADS) && APR_HAS_THREADS)
	if(CRYPTO_get_locking_callback() == cas_ssl_locking_callback)
		CRYPTO_set_locking_callback(NULL);
#ifdef OPENSSL_NO_THREADID
	if(CRYPTO_get_id_callback() == cas_ssl_id_callback)
		CRYPTO_set_id_callback(NULL);
#else
	if(CRYPTO_THREADID_get_callback() == cas_ssl_id_callback)
		CRYPTO_THREADID_set_callback(NULL);
#endif /* OPENSSL_NO_THREADID */

#endif /* defined(OPENSSL_THREADS) && APR_HAS_THREADS */
	curl_global_cleanup();
	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, "exiting cas_cleanup()");
	return APR_SUCCESS;
}

static int check_vhost_config(apr_pool_t *pool, server_rec *s)
{
	cas_cfg *c = ap_get_module_config(s->module_config, &auth_cas_module);
	apr_finfo_t f;
	apr_uri_t nullURL;

	if(c->CASDebug)
		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, "entering check_vhost_config()");

	memset(&nullURL, '\0', sizeof(apr_uri_t));

	if(apr_stat(&f, c->CASCookiePath, APR_FINFO_TYPE, pool) == APR_INCOMPLETE) {
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "MOD_AUTH_CAS: Could not find CASCookiePath '%s'", c->CASCookiePath);
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	if(f.filetype != APR_DIR || c->CASCookiePath[strlen(c->CASCookiePath)-1] != '/') {
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "MOD_AUTH_CAS: CASCookiePath '%s' is not a directory or does not end in a trailing '/'!", c->CASCookiePath);
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	
	if(memcmp(&c->CASLoginURL, &nullURL, sizeof(apr_uri_t)) == 0 || memcmp(&c->CASValidateURL, &nullURL, sizeof(apr_uri_t)) == 0) {
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "MOD_AUTH_CAS: CASLoginURL or CASValidateURL not defined.");
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	if(memcmp(&c->CASValidateURL, &nullURL, sizeof(apr_uri_t)) != 0) {
		if(strncmp(c->CASValidateURL.scheme, "https", 5) != 0) {
			ap_log_error(APLOG_MARK, APLOG_WARNING, 0, s, "MOD_AUTH_CAS: CASValidateURL should be HTTPS.");
		}
	}

	return OK;
}

static int check_merged_vhost_configs(apr_pool_t *pool, server_rec *s)
{
	int status = OK;

	while (s != NULL && status == OK) {
		cas_cfg *c = ap_get_module_config(s->module_config, &auth_cas_module);

		if (c->merged) {
			status = check_vhost_config(pool, s);
		}

		s = s->next;
	}

	return status;
}

/* Do any merged vhost configs exist? */
static int merged_vhost_configs_exist(server_rec *s)
{
	while (s != NULL) {
		cas_cfg *c = ap_get_module_config(s->module_config, &auth_cas_module);

		if (c->merged) {
			return TRUE;
		}

		s = s->next;
	}

	return FALSE;
}

static int cas_post_config(apr_pool_t *pool, apr_pool_t *p1, apr_pool_t *p2, server_rec *s)
{
	const char *userdata_key = "auth_cas_init";
	void *data;
	int i;

	/* Since the post_config hook is invoked twice (once
	 * for 'sanity checking' of the config and once for
	 * the actual server launch, we have to use a hack
	 * to not run twice
	 */
	apr_pool_userdata_get(&data, userdata_key, s->process->pool);

	if(data) {
		curl_global_init(CURL_GLOBAL_ALL);

#if (defined(OPENSSL_THREADS) && APR_HAS_THREADS)
		ssl_num_locks = CRYPTO_num_locks();
		ssl_locks = apr_pcalloc(s->process->pool, ssl_num_locks * sizeof(*ssl_locks));

		for(i = 0; i < ssl_num_locks; i++)
			apr_thread_mutex_create(&(ssl_locks[i]), APR_THREAD_MUTEX_DEFAULT, s->process->pool);

#ifdef OPENSSL_NO_THREADID
		if(CRYPTO_get_locking_callback() == NULL && CRYPTO_get_id_callback() == NULL) {
			CRYPTO_set_locking_callback(cas_ssl_locking_callback);
			CRYPTO_set_id_callback(cas_ssl_id_callback);
		}
#else
		if(CRYPTO_get_locking_callback() == NULL && CRYPTO_THREADID_get_id_callback() == NULL) {
			CRYPTO_set_locking_callback(cas_ssl_locking_callback);
			CRYPTO_THREADID_set_id_callback(cas_ssl_id_callback);
		}
#endif /* OPENSSL_NO_THREADID */
#endif /* defined(OPENSSL_THREADS) && APR_HAS_THREADS */
		apr_pool_cleanup_register(pool, s, cas_cleanup, apr_pool_cleanup_null);
	}

	apr_pool_userdata_set((const void *)1, userdata_key, apr_pool_cleanup_null, s->process->pool);

	/*
	 * Apache has a base vhost that true vhosts derive from.
	 * There are two startup scenarios:
	 *
	 * 1. Only the base vhost contains CAS settings.
	 *    No server configs have been merged.
	 *    Only the base vhost needs to be checked.
	 *
	 * 2. The base vhost contains zero or more CAS settings.
	 *    One or more vhosts override these.
	 *    These vhosts have a merged config.
	 *    All merged configs need to be checked.
	 */
	if (!merged_vhost_configs_exist(s)) {
		/* nothing merged, only check the base vhost */
		return check_vhost_config(pool, s);
	}

	return check_merged_vhost_configs(pool, s);
}

static apr_status_t cas_in_filter(ap_filter_t *f, apr_bucket_brigade *bb, ap_input_mode_t mode, apr_read_type_e block, apr_off_t readbytes) {
	apr_bucket *b;
	apr_status_t rv;
	apr_size_t len = 0, offset = 0;
	char data[1024];
	const char *bucketData;

	memset(data, '\0', sizeof(data));

	rv = ap_get_brigade(f->next, bb, mode, block, readbytes);

	if(rv != APR_SUCCESS) {
		apr_strerror(rv, data, sizeof(data));
		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, f->c->base_server, "unable to retrieve bucket brigade: %s", data);
		return rv;
	}

	for(b = APR_BRIGADE_FIRST(bb); b != APR_BRIGADE_SENTINEL(bb); b = APR_BUCKET_NEXT(b)) {
		if(APR_BUCKET_IS_METADATA(b))
			continue;
		if(apr_bucket_read(b, &bucketData, &len, APR_BLOCK_READ) == APR_SUCCESS) {
			if(offset + len >= sizeof(data)) {
				// hack below casts strlen() to unsigned long to avoid %zu vs. %Iu on Linux vs. Win 
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, f->c->base_server, "bucket brigade contains more than %lu bytes, truncation required (SSOut may fail)", (unsigned long) sizeof(data));
				memcpy(data + offset, bucketData, (sizeof(data) - offset) - 1); // copy what we can into the space remaining
				break;
			} else {
				memcpy(data + offset, bucketData, len);
			}
		}
	}

	// hack below casts strlen() to unsigned long to avoid %zu vs. %Iu on Linux vs. Win 
	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, f->c->base_server, "read %lu bytes (%s) from incoming buckets\n", (unsigned long) strlen(data), data);
	CASSAMLLogout(f->r, data);

	return APR_SUCCESS;
}

static void cas_register_hooks(apr_pool_t *p)
{
	ap_hook_post_config(cas_post_config, NULL, NULL, APR_HOOK_LAST);
	ap_hook_check_user_id(cas_authenticate, NULL, NULL, APR_HOOK_MIDDLE);
	ap_register_input_filter("CAS", cas_in_filter, NULL, AP_FTYPE_RESOURCE); 
}

static const command_rec cas_cmds [] = {
	AP_INIT_TAKE1("CASVersion", cfg_readCASParameter, (void *) cmd_version, RSRC_CONF, "Set CAS Protocol Version (1 or 2)"),
	AP_INIT_TAKE1("CASDebug", cfg_readCASParameter, (void *) cmd_debug, RSRC_CONF, "Enable or disable debug mode (On or Off)"),
	/* cas protocol options */
	AP_INIT_TAKE1("CASScope", ap_set_string_slot, (void *) APR_OFFSETOF(cas_dir_cfg, CASScope), ACCESS_CONF|OR_AUTHCFG, "Define the scope that this CAS sessions is valid for (e.g. /app/ will validate this session for /app/*)"),
	AP_INIT_TAKE1("CASRenew", ap_set_string_slot, (void *) APR_OFFSETOF(cas_dir_cfg, CASRenew), ACCESS_CONF|OR_AUTHCFG, "Force credential renew (/app/secure/ will require renew on /app/secure/*)"),
	AP_INIT_TAKE1("CASGateway", ap_set_string_slot, (void *) APR_OFFSETOF(cas_dir_cfg, CASGateway), ACCESS_CONF|OR_AUTHCFG, "Allow anonymous access if no CAS session is established on this path (e.g. /app/insecure/ will allow gateway access to /app/insecure/*), CAS v2 only"),
	AP_INIT_TAKE1("CASAuthNHeader", ap_set_string_slot, (void *) APR_OFFSETOF(cas_dir_cfg, CASAuthNHeader), ACCESS_CONF|OR_AUTHCFG, "Specify the HTTP header variable to set with the name of the CAS authenticated user.  By default no headers are added."),
	AP_INIT_TAKE1("CASSSOEnabled", cfg_readCASParameter, (void *) cmd_sso, RSRC_CONF, "Enable or disable Single Sign Out functionality (On or Off)"),
	AP_INIT_TAKE1("CASAttributeDelimiter", cfg_readCASParameter, (void *) cmd_attribute_delimiter, RSRC_CONF, "The delimiter to use when setting multi-valued attributes in the HTTP headers"),
	AP_INIT_TAKE1("CASAttributePrefix", cfg_readCASParameter, (void *) cmd_attribute_prefix, RSRC_CONF, "The prefix to use when setting attributes in the HTTP headers"),

	/* ssl related options */
	AP_INIT_TAKE1("CASValidateServer", cfg_readCASParameter, (void *) cmd_validate_server, RSRC_CONF, "Require validation of CAS server SSL certificate for successful authentication (On or Off)"),
	AP_INIT_TAKE1("CASValidateDepth", cfg_readCASParameter, (void *) cmd_validate_depth, RSRC_CONF, "Define the number of chained certificates required for a successful validation"),
	AP_INIT_TAKE1("CASAllowWildcardCert", cfg_readCASParameter, (void *) cmd_wildcard_cert, RSRC_CONF, "Allow wildcards in certificates when performing validation (e.g. *.example.com) (On or Off)"),
	AP_INIT_TAKE1("CASCertificatePath", cfg_readCASParameter, (void *) cmd_ca_path, RSRC_CONF, "Path to the X509 certificate for the CASServer Certificate Authority"),

	/* pertinent CAS urls */
	AP_INIT_TAKE1("CASLoginURL", cfg_readCASParameter, (void *) cmd_loginurl, RSRC_CONF, "Define the CAS Login URL (ex: https://login.example.com/cas/login)"),
	AP_INIT_TAKE1("CASValidateURL", cfg_readCASParameter, (void *) cmd_validateurl, RSRC_CONF, "Define the CAS Ticket Validation URL (ex: https://login.example.com/cas/serviceValidate)"),
	AP_INIT_TAKE1("CASProxyValidateURL", cfg_readCASParameter, (void *) cmd_proxyurl, RSRC_CONF, "Define the CAS Proxy Ticket validation URL relative to CASServer (unimplemented)"),
	AP_INIT_TAKE1("CASValidateSAML", cfg_readCASParameter, (void *) cmd_validate_saml, RSRC_CONF, "Whether the CASLoginURL is for SAML validation (On or Off)"),

	/* cache options */
	AP_INIT_TAKE1("CASCookiePath", cfg_readCASParameter, (void *) cmd_cookie_path, RSRC_CONF, "Path to store the CAS session cookies in (must end in trailing /)"),
	AP_INIT_TAKE1("CASCookieEntropy", cfg_readCASParameter, (void *) cmd_cookie_entropy, RSRC_CONF, "Number of random bytes to use when generating a session cookie (larger values may result in slow cookie generation)"),
	AP_INIT_TAKE1("CASCookieDomain", cfg_readCASParameter, (void *) cmd_cookie_domain, RSRC_CONF, "Specify domain header for mod_auth_cas cookie"),
	AP_INIT_TAKE1("CASCookieHttpOnly", cfg_readCASParameter, (void *) cmd_cookie_httponly, RSRC_CONF, "Enable 'HttpOnly' flag for mod_auth_cas cookie (may break RFC compliance)"),
	AP_INIT_TAKE1("CASCookie", ap_set_string_slot, (void *) APR_OFFSETOF(cas_dir_cfg, CASCookie), ACCESS_CONF|OR_AUTHCFG, "Define the cookie name for HTTP sessions"),
	AP_INIT_TAKE1("CASSecureCookie", ap_set_string_slot, (void *) APR_OFFSETOF(cas_dir_cfg, CASSecureCookie), ACCESS_CONF|OR_AUTHCFG, "Define the cookie name for HTTPS sessions"),
	AP_INIT_TAKE1("CASGatewayCookie", ap_set_string_slot, (void *) APR_OFFSETOF(cas_dir_cfg, CASGatewayCookie), ACCESS_CONF|OR_AUTHCFG, "Define the cookie name for a gateway location"),
	/* cache timeout options */
	AP_INIT_TAKE1("CASTimeout", cfg_readCASParameter, (void *) cmd_session_timeout, RSRC_CONF, "Maximum time (in seconds) a session cookie is valid for, regardless of idle time.  Set to 0 to allow non-idle sessions to never expire"),
	AP_INIT_TAKE1("CASIdleTimeout", cfg_readCASParameter, (void *) cmd_idle_timeout, RSRC_CONF, "Maximum time (in seconds) a session can be idle for"),
	AP_INIT_TAKE1("CASCacheCleanInterval", cfg_readCASParameter, (void *) cmd_cache_interval, RSRC_CONF, "Amount of time (in seconds) between cache cleanups.  This value is checked when a new local ticket is issued or when a ticket expires."),
	AP_INIT_TAKE1("CASRootProxiedAs", cfg_readCASParameter, (void *) cmd_root_proxied_as, RSRC_CONF, "URL used to access the root of the virtual server (only needed when the server is proxied)"),
 	AP_INIT_TAKE1("CASScrubRequestHeaders", ap_set_string_slot, (void *) APR_OFFSETOF(cas_dir_cfg, CASScrubRequestHeaders), ACCESS_CONF, "Scrub CAS user name and SAML attribute headers from the user's request."),
	{NULL}
};

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA auth_cas_module = {
    STANDARD20_MODULE_STUFF, 
    cas_create_dir_config,                  /* create per-dir    config structures */
    cas_merge_dir_config,                  /* merge  per-dir    config structures */
    cas_create_server_config,                  /* create per-server config structures */
    cas_merge_server_config,                  /* merge  per-server config structures */
    cas_cmds,                  /* table of config file commands       */
    cas_register_hooks  /* register hooks                      */
};
