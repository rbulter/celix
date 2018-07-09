/**
 *Licensed to the Apache Software Foundation (ASF) under one
 *or more contributor license agreements.  See the NOTICE file
 *distributed with this work for additional information
 *regarding copyright ownership.  The ASF licenses this file
 *to you under the Apache License, Version 2.0 (the
 *"License"); you may not use this file except in compliance
 *with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing,
 *software distributed under the License is distributed on an
 *"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 *specific language governing permissions and limitations
 *under the License.
 */

#include <stdio.h>
#include <stdlib.h>

#include "celix_bundle_activator.h"
#include "constants.h"

#include "command.h"

typedef struct activator_data {
    celix_bundle_context_t *ctx;
    command_service_t svc;
    long svcId;
} activator_data_t;

struct useShellServiceHandle {
    activator_data_t* data;
    FILE *out;
    int count;
};

static void useShellService(void *handle, void *_svc __attribute__((unused)), const celix_properties_t *props) {
    struct useShellServiceHandle *useHandle = handle;
    const char *name = celix_properties_getWithDefault(props, OSGI_SHELL_COMMAND_NAME, "Not Found");
    fprintf(useHandle->out, "%i: Command %s found\n", useHandle->count++, name);
}

static celix_status_t executeCommand(void *handle, char * commandLine, FILE *outStream, FILE *errorStream) {
    activator_data_t *data = handle;
    struct useShellServiceHandle useHandle = { .data = data, .out = outStream, .count = 0};

    celix_service_use_options_t opts = CELIX_EMPTY_SERVICE_USE_OPTIONS;
    opts.filter.serviceName = OSGI_SHELL_COMMAND_SERVICE_NAME;
    opts.useWithProperties = useShellService;
    opts.callbackHandle = &useHandle;
    celix_bundleContext_useServicesWithOptions(data->ctx, &opts);
    fprintf(outStream, "Found %i command_service_t services\n", useHandle.count);

    return CELIX_SUCCESS;
}


static celix_status_t activator_start(activator_data_t *data, celix_bundle_context_t *ctx) {
    data->ctx = ctx;
    data->svc.handle = data;
    data->svc.executeCommand = executeCommand;

    data->svcId = -1L;
    celix_properties_t *props = celix_properties_create();
    celix_properties_set(props, OSGI_SHELL_COMMAND_NAME, "c_exmpl");
    data->svcId = celix_bundleContext_registerService(ctx, &data->svc, OSGI_SHELL_COMMAND_SERVICE_NAME, props);
    printf("Registered command service with service id %li\n", data->svcId);

    return CELIX_SUCCESS;
}

static celix_status_t activator_stop(activator_data_t *data, celix_bundle_context_t *ctx) {
    celix_bundleContext_unregisterService(ctx, data->svcId);
    printf("Unregistered command service with service id %li\n", data->svcId);
    return CELIX_SUCCESS;
}

CELIX_GEN_BUNDLE_ACTIVATOR(activator_data_t, activator_start, activator_stop)