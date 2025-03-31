#pragma once

#include <curl/curl.h>
#include <uw.h>

extern UwTypeId UwTypeId_CurlRequest;
/*
 * type id for CURL request, returned by uw_subtype
 */

extern unsigned UwInterfaceId_Curl;
/*
 * CURL interface id for CurlRequest
 *
 * important: stick to naming conventions for uw_get_interface macro to work
 */

typedef struct {
    size_t (*write_data)(void* data, size_t always_1, size_t size, UwValuePtr self);
    void   (*complete)  (UwValuePtr self);

} UwInterface_Curl;


typedef struct {
    CURL* easy_handle;

    _UwValue url;
    _UwValue proxy;
    _UwValue real_url;

    // Parsed headers, call curl_request_parse_headers for that.
    // Can be nullptr!
    _UwValue media_type;
    _UwValue media_subtype;
    _UwValue media_type_params;  // map
    _UwValue disposition_type;
    _UwValue disposition_params; // values can be strings of maps containing charset, language, and value

    // The content received by default handlers.
    // Always binary, regardless of content-type charset
    _UwValue content;

    struct curl_slist* headers;

    unsigned int status;

} CurlRequestData;

#define uw_curl_request_data_ptr(value)  ((CurlRequestData*) _uw_get_data_ptr((value), UwTypeId_CurlRequest))

// sessions
void* create_curl_session();
bool add_curl_request(void* session, UwValuePtr request);
void delete_curl_session(void* session);

// request
void curl_request_set_url(UwValuePtr request, UwValuePtr url);
void curl_request_set_proxy(UwValuePtr request, UwValuePtr proxy);
void curl_request_set_cookie(UwValuePtr request, UwValuePtr cookie);
void curl_request_set_resume(UwValuePtr request, size_t pos);
bool curl_request_set_headers(UwValuePtr request, char* http_headers[], unsigned num_headers);
void curl_request_verbose(UwValuePtr request, bool verbose);

void curl_update_status(UwValuePtr request);

// runner
bool curl_perform(void* session, int* running_transfers);

// utils
UwResult urljoin_cstr(char* base_url, char* other_url);
UwResult urljoin(UwValuePtr base_url, UwValuePtr other_url);

void curl_request_parse_content_type(CurlRequestData* req);
void curl_request_parse_content_disposition(CurlRequestData* req);
void curl_request_parse_headers(CurlRequestData* req);

UwResult curl_request_get_filename(CurlRequestData* req);
