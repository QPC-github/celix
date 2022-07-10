/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *  KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <rsa_json_rpc_endpoint_impl.h>
#include <remote_interceptors_handler.h>
#include <endpoint_description.h>
#include <remote_constants.h>
#include <dfi_utils.h>
#include <json_rpc.h>
#include <celix_api.h>
#include <jansson.h>
#include <assert.h>

struct rsa_json_rpc_endpoint {
    celix_bundle_context_t* ctx;
    celix_log_helper_t *logHelper;
    FILE *callsLogFile;
    endpoint_description_t *endpointDesc;
    remote_interceptors_handler_t *interceptorsHandler;
    long svcTrackerId;
    celix_thread_mutex_t mutex; //protects service
    void *service;
    dyn_interface_type *intfType;
};

static void rsaJsonRpcEndpoint_addSvcWithOwner(void *handle, void *service,
        const celix_properties_t *props, const celix_bundle_t *svcOwner);
static void rsaJsonRpcEndpoint_removeSvcWithOwner(void *handle, void *service,
        const celix_properties_t *props, const celix_bundle_t *svcOwner);

celix_status_t rsaJsonRpcEndpoint_create(celix_bundle_context_t* ctx, celix_log_helper_t *logHelper,
        FILE *logFile, remote_interceptors_handler_t *interceptorsHandler,
        const endpoint_description_t *endpointDesc, rsa_json_rpc_endpoint_t **endpointOut) {
    celix_status_t status = CELIX_SUCCESS;
    rsa_json_rpc_endpoint_t *endpoint = calloc(1, sizeof(*endpoint));
    assert(endpoint != NULL);
    endpoint->ctx = ctx;
    endpoint->logHelper = logHelper;
    endpoint->callsLogFile = logFile;
    //copy endpoint description
    endpoint->endpointDesc = (endpoint_description_t*)calloc(1, sizeof(endpoint_description_t));
    assert(endpoint->endpointDesc != NULL);
    endpoint->endpointDesc->properties = celix_properties_copy(endpointDesc->properties);
    assert(endpoint->endpointDesc->properties != NULL);
    endpoint->endpointDesc->frameworkUUID = (char*)celix_properties_get(endpoint->endpointDesc->properties,
            OSGI_RSA_ENDPOINT_FRAMEWORK_UUID, NULL);
    endpoint->endpointDesc->serviceId = endpointDesc->serviceId;
    endpoint->endpointDesc->id = (char*)celix_properties_get(endpoint->endpointDesc->properties,
            OSGI_RSA_ENDPOINT_ID, NULL);
    endpoint->endpointDesc->service = strdup(endpointDesc->service);
    assert(endpoint->endpointDesc->service != NULL);

    endpoint->interceptorsHandler = interceptorsHandler;
    endpoint->service = NULL;
    endpoint->intfType = NULL;
    status = celixThreadMutex_create(&endpoint->mutex, NULL);
    if (status != CELIX_SUCCESS) {
        celix_logHelper_error(logHelper, "RSA json rpc endpoint: Error initilizing mutex for %s. %d.",
                endpointDesc->service, status);
        goto mutex_err;
    }

    char filter[32] = {0};// It is longer than the size of "service.id" + serviceId
    (void)snprintf(filter, sizeof(filter), "(service.id=%ld)", endpointDesc->serviceId);
    celix_service_tracking_options_t opts = CELIX_EMPTY_SERVICE_TRACKING_OPTIONS;
    opts.filter.filter = filter;
    opts.filter.ignoreServiceLanguage = true;
    opts.callbackHandle = endpoint;
    opts.addWithOwner = rsaJsonRpcEndpoint_addSvcWithOwner;
    opts.removeWithOwner = rsaJsonRpcEndpoint_removeSvcWithOwner;
    endpoint->svcTrackerId = celix_bundleContext_trackServicesWithOptionsAsync(endpoint->ctx, &opts);
    if (endpoint->svcTrackerId < 0) {
        celix_logHelper_error(logHelper, "RSA json rpc endpoint: Error Registering %s tracker.", endpointDesc->service);
        status = CELIX_ILLEGAL_STATE;
        goto service_tracker_err;
    }

    *endpointOut = endpoint;

    return CELIX_SUCCESS;

service_tracker_err:
    (void)celixThreadMutex_destroy(&endpoint->mutex);
mutex_err:
    endpointDescription_destroy(endpoint->endpointDesc);
    free(endpoint);
    return status;
}

static void rsaJsonRpcEndpoint_destroyCallback(void *data) {
    assert(data != NULL);
    rsa_json_rpc_endpoint_t *endpoint = (rsa_json_rpc_endpoint_t *)data;
    (void)celixThreadMutex_destroy(&endpoint->mutex);
    endpointDescription_destroy(endpoint->endpointDesc);
    free(endpoint);
    return;
}

void rsaJsonRpcEndpoint_destroy(rsa_json_rpc_endpoint_t *endpoint) {
    if (endpoint != NULL) {
        celix_bundleContext_stopTrackerAsync(endpoint->ctx, endpoint->svcTrackerId,
                endpoint, rsaJsonRpcEndpoint_destroyCallback);
    }
    return;
}

static void rsaJsonRpcEndpoint_addSvcWithOwner(void *handle, void *service,
        const celix_properties_t *props, const celix_bundle_t *svcOwner) {
    assert(handle != NULL);
    assert(service != NULL);
    assert(svcOwner != NULL);
    celix_status_t status = CELIX_SUCCESS;
    rsa_json_rpc_endpoint_t *endpoint = (rsa_json_rpc_endpoint_t *)handle;

    celixThreadMutex_lock(&endpoint->mutex);

    status = dfi_findAndParseInterfaceDescriptor(endpoint->logHelper,endpoint->ctx,
            svcOwner, endpoint->endpointDesc->service, &endpoint->intfType);
    if (status != CELIX_SUCCESS) {
        celix_logHelper_error(endpoint->logHelper, "Parse service descriptor failed.");
        goto intf_type_err;
    }

// TODO check version

    endpoint->service = service;

    celixThreadMutex_unlock(&endpoint->mutex);

    return;

intf_type_err:
    celixThreadMutex_unlock(&endpoint->mutex);
    return;
}

static void rsaJsonRpcEndpoint_removeSvcWithOwner(void *handle, void *service,
        const celix_properties_t *props, const celix_bundle_t *svcOwner) {
    assert(handle != NULL);
    rsa_json_rpc_endpoint_t *endpoint = (rsa_json_rpc_endpoint_t *)handle;
    celixThreadMutex_lock(&endpoint->mutex);
    if (endpoint->service == service) {
        endpoint->service = NULL;
        dynInterface_destroy(endpoint->intfType);
    }
    celixThreadMutex_unlock(&endpoint->mutex);
    return;
}

celix_status_t rsaJsonRpcEndpoint_handleRequest(void *handle, celix_properties_t **metadata,
        const struct iovec *request, struct iovec *responseOut) {
    celix_status_t status = CELIX_SUCCESS;
    if (handle == NULL || metadata == NULL || request == NULL || request->iov_base == NULL
            || request->iov_len == 0 || responseOut == NULL) {
        return CELIX_ILLEGAL_ARGUMENT;
    }
    responseOut->iov_base = NULL;
    responseOut->iov_len = 0;
    rsa_json_rpc_endpoint_t *endpoint = (rsa_json_rpc_endpoint_t *)handle;

    json_error_t error;
    json_t *jsRequest = json_loads((char *)request->iov_base, 0, &error);
    if (jsRequest == NULL) {
        status = CELIX_ILLEGAL_ARGUMENT;
        celix_logHelper_error(endpoint->logHelper, "Parse request json string failed for %s.", (char *)request->iov_base);
        goto request_err;
    }
    const char *sig;
    int rc = json_unpack(jsRequest, "{s:s}", "m", &sig);
    if (rc != 0) {
        status = CELIX_ILLEGAL_ARGUMENT;
        celix_logHelper_error(endpoint->logHelper, "Request method error for %s.", (char *)request->iov_base);
        goto request_method_err;
    }

    char *szResponse = NULL;
    bool cont = remoteInterceptorHandler_invokePreExportCall(endpoint->interceptorsHandler,
            endpoint->endpointDesc->properties, sig, metadata);
    if (cont) {
        celixThreadMutex_lock(&endpoint->mutex);
        if (endpoint->service != NULL) {
            int rc = jsonRpc_call(endpoint->intfType, endpoint->service, (char *)request->iov_base, &szResponse);
            status = (rc != 0) ? CELIX_SERVICE_EXCEPTION : CELIX_SUCCESS;
        } else {
            status = CELIX_ILLEGAL_STATE;
            celix_logHelper_error(endpoint->logHelper, "service is null, please try again.");
        }
        celixThreadMutex_unlock(&endpoint->mutex);

        remoteInterceptorHandler_invokePostExportCall(endpoint->interceptorsHandler,
                endpoint->endpointDesc->properties, sig, *metadata);
    }
    if (szResponse != NULL) {
        responseOut->iov_base = szResponse;
        responseOut->iov_len = strlen(szResponse) + 1;// make it include '\0'
    }

    if (endpoint->callsLogFile != NULL) {
        fprintf(endpoint->callsLogFile, "ENDPOINT REMOTE CALL:\n\tservice=%s\n\tservice_id=%lu\n\trequest_payload=%s\n\trequest_response=%s\n\tstatus=%i\n",
                endpoint->endpointDesc->service, endpoint->endpointDesc->serviceId, (char *)request->iov_base, szResponse, status);
        fflush(endpoint->callsLogFile);
    }

    json_decref(jsRequest);

    return status;

request_method_err:
    json_decref(jsRequest);
request_err:
    return status;
}