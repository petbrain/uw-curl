#include <signal.h>
#include <string.h>

#include "uw_curl.h"

// FileRequest type extends CurlRequest with file.

UwTypeId UwTypeId_FileRequest = 0;

typedef struct {
    _UwValue file;  // autocleaned UwValue is not suitable for manually managed data,
                    // using bare structure that starts with underscore
} FileRequestData;

// this macro gets pointer to FileRequestData from UwValue
#define file_request_data_ptr(value)  ((FileRequestData*) _uw_get_data_ptr((value), UwTypeId_FileRequest))


// global parameters from argv
__UWDECL_Null( proxy );
__UWDECL_Bool( verbose, false );


// signal handling

sig_atomic_t pending_sigint = 0;

void sigint_handler(int sig)
{
    puts("\nInterrupted");
    pending_sigint = 1;
}

void create_request(void* session, UwValuePtr url)
/*
 * Helper function to create Curl request of our custom FileRequest type
 */
{
    UwValue request = uw_create(UwTypeId_FileRequest);
    if (uw_error(&request)) {
        uw_print_status(stdout, &request);
        return;
    }

    UW_CSTRING_LOCAL(url_cstr, url);
    printf("Requesting %s\n", url_cstr);

    curl_request_set_url(&request, url);
    curl_request_set_proxy(&request, &proxy);
    if (verbose.bool_value) {
        curl_request_verbose(&request, true);
    }
    add_curl_request(session, &request);

    // request is now held by Curl handle
    // and will be destroyed in curl_perform
}

size_t write_data(void* data, size_t always_1, size_t size, UwValuePtr self)
/*
 * Overloaded method of Curl interface.
 */
{
    if (size == 0) {
        printf("%s is called with size=%zu\n", __func__, size);
        return 0;
    }

    CurlRequestData* curl_req = uw_curl_request_data_ptr(self);
    FileRequestData* file_req = file_request_data_ptr(self);

    // get status from curl request
    curl_update_status(self);

    if(curl_req->status != 200) {
        UW_CSTRING_LOCAL(url_cstr, &curl_req->url);
        printf("FAILED: %u %s\n", curl_req->status, url_cstr);
        return 0;
    }
    if (uw_is_null(&file_req->file)) {

        // the file is not created yet, do that

        // get file name from response headers
        curl_request_parse_headers(curl_req);
        UwValue filename_info = curl_request_get_filename(curl_req);
        UwValue full_name = uw_map_get(&filename_info, "filename");
        UwValue filename = uw_basename(&full_name);
        if (uw_error(&filename) || uw_strlen(&filename) == 0) {
            // get file name from URL
            UwValue parts = uw_string_split_chr(&curl_req->url, '?', 1);
            UwValue url = uw_list_item(&parts, 0);
            uw_destroy(&filename);
            filename = uw_basename(&url);
            if (uw_error(&filename)) {
                uw_print_status(stdout, &filename);
                return 0;
            }
            if (uw_strlen(&filename) == 0) {
                uw_string_append(&filename, "index.html");
            }
        }

        file_req->file = uw_file_open(&filename, O_CREAT | O_RDWR | O_TRUNC, 0644);
        if (uw_error(&file_req->file)) {
            uw_print_status(stdout, &file_req->file);
            return 0;
        }
        UW_CSTRING_LOCAL(url_cstr, &curl_req->url);
        UW_CSTRING_LOCAL(filename_cstr, &filename);
        printf("Downloading %s -> %s\n", url_cstr, filename_cstr);
    }

    // write data to file

    unsigned bytes_written;
    UwValue status = uw_file_write(&file_req->file, data, size, &bytes_written);
    if (uw_error(&status)) {
        uw_print_status(stdout, &status);
        return 0;
    }
    return bytes_written;
}

void request_complete(UwValuePtr self)
/*
 * Overloaded method of Curl interface.
 */
{
    CurlRequestData* curl_req = uw_curl_request_data_ptr(self);
    FileRequestData* file_req = file_request_data_ptr(self);

    if(curl_req->status != 200) {
        UW_CSTRING_LOCAL(url_cstr, &curl_req->url);
        printf("FAILED: %u %s\n", curl_req->status, url_cstr);
        return;
    }

    if (uw_is_null(&file_req->file)) {
        // nothing was written to file
        return;
    }

    uw_file_close(&file_req->file);
}

void fini_file_request(UwValuePtr self)
/*
 * Overloaded method of Struct interface.
 */
{
    FileRequestData* req = file_request_data_ptr(self);

    uw_destroy(&req->file);

    // call super method
    uw_ancestor_of(UwTypeId_FileRequest)->fini(self);
}

int main(int argc, char* argv[])
{
    // global initialization

    init_allocator(&pet_allocator);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // create FileRequest subtype

    // static structure that holds the type
    static UwType file_request_type;

    // custom Curl interface
    static UwInterface_Curl file_curl_interface = {
        .write_data = write_data,
        .complete   = request_complete
    };

    // create subtype, this initializes file_request_type and returns type id
    UwTypeId_FileRequest = uw_subtype(
        &file_request_type, "FileRequest",
        UwTypeId_CurlRequest,  // base type
        FileRequestData,
        // overload CURL interface:
        UwInterfaceId_Curl, &file_curl_interface
    );
    // Default initialization of Struct subtype zeroes allocated data.
    // This makes file UwNull() and we don't need to overload init method.
    // We only need to overload fini for proper cleanup:
    file_request_type.fini = fini_file_request;

    // setup signal handling

    signal(SIGINT, sigint_handler);

    // create session

    void* session = create_curl_session();

    // parse command line arguments
    UwValue urls = UwList();
    UwValue parallel = UwUnsigned(1);
    for (int i = 1; i < argc; i++) {{  // mind double curly brackets for nested scope
        // nested scope makes autocleaning working after each iteration

        UwValue arg = uw_create_string(argv[i]);

        if (uw_startswith(&arg, "http://") || uw_startswith(&arg, "https://")) {
            uw_list_append(&urls, &arg);

        } else if (uw_startswith(&arg, "verbose=")) {
            UwValue v = uw_substr(&arg, strlen("verbose="), uw_strlen(&arg));
            verbose.bool_value = uw_equal(&v, "1");

        } else if (uw_startswith(&arg, "proxy=")) {
            proxy = uw_substr(&arg, strlen("proxy="), uw_strlen(&arg));

        } else if (uw_startswith(&arg, "parallel=")) {
            UwValue s = uw_substr(&arg, strlen("parallel="), uw_strlen(&arg));
            UwValue n = uw_string_to_int(&s);
            if (uw_is_int(&n)) {
                parallel = n;
            }
        }
    }}
    if (uw_list_length(&urls) == 0) {
        printf("Usage: fetch [verbose=1|0] [proxy=<proxy>] [parallel=<n>] url1 url2 ...\n");
        goto out;
    }

    // fetch URLs
    // prepare first request
    {
        UwValue url = uw_list_pop(&urls);
        if (uw_error(&url)) {
            uw_print_status(stdout, &url);
            goto out;
        }
        create_request(session, &url);
    }

    // perform fetching

    while(!pending_sigint) {
        int running_transfers;
        if (!curl_perform(session, &running_transfers)) {
            // failure
            break;
        }
        unsigned i = running_transfers;
        // add more requests
        for(; i < parallel.signed_value; i++) {{
            if (uw_list_length(&urls) == 0) {
                break;
            }
            UwValue url = uw_list_pop(&urls);
            if (uw_error(&url)) {
                uw_print_status(stdout, &url);
                break;
            }
            create_request(session, &url);
        }}
        if (i == 0) {
            // no running transfers and no more URLs were added
            break;
        }
    }

out:

    delete_curl_session(session);

    // global finalization

    uw_destroy(&proxy);  // can be allocated string

    curl_global_cleanup();

    return 0;
}
