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
/*
 * example_activator.c
 *
 *  \date       Oct 5, 2011
 *  \author    	<a href="mailto:celix-dev@incubator.apache.org">Apache Celix Project Team</a>
 *  \copyright	Apache License, Version 2.0
 */
#include <stdlib.h>

#include "bundle_activator.h"
#include "bundle_context.h"
#include "service_registration.h"

#include "example_impl.h"
#include "remote_constants.h"

struct activator {
	apr_pool_t *pool;
	SERVICE_REGISTRATION exampleReg;
};

celix_status_t bundleActivator_create(BUNDLE_CONTEXT context, void **userData) {
	celix_status_t status = CELIX_SUCCESS;
	apr_pool_t *parentpool = NULL;
	apr_pool_t *pool = NULL;
	struct activator *activator;

	status = bundleContext_getMemoryPool(context, &parentpool);
	if (status == CELIX_SUCCESS) {
		if (apr_pool_create(&pool, parentpool) != APR_SUCCESS) {
			status = CELIX_BUNDLE_EXCEPTION;
		} else {
			activator = apr_palloc(pool, sizeof(*activator));
			activator->pool = pool;
			activator->exampleReg = NULL;

			*userData = activator;
		}
	}

	return status;
}

celix_status_t bundleActivator_start(void * userData, BUNDLE_CONTEXT context) {
	celix_status_t status = CELIX_SUCCESS;
	struct activator *activator = userData;
	example_t example = NULL;
	example_service_t service = NULL;
	PROPERTIES properties = NULL;

	status = example_create(activator->pool, &example);
	if (status == CELIX_SUCCESS) {
		service = apr_palloc(activator->pool, sizeof(*service));
		if (!service) {
			status = CELIX_ENOMEM;
		} else {
			service->example = example;
			service->add = example_add;
			service->sub = example_sub;
			service->sqrt = example_sqrt;

			properties = properties_create();
			properties_set(properties, (char *) SERVICE_EXPORTED_INTERFACES, (char *) EXAMPLE_SERVICE);

			bundleContext_registerService(context, (char *) EXAMPLE_SERVICE, service, properties, &activator->exampleReg);
		}
	}

	return status;
}

celix_status_t bundleActivator_stop(void * userData, BUNDLE_CONTEXT context) {
	celix_status_t status = CELIX_SUCCESS;
	struct activator *activator = userData;

	serviceRegistration_unregister(activator->exampleReg);

	return status;
}

celix_status_t bundleActivator_destroy(void * userData, BUNDLE_CONTEXT context) {
	celix_status_t status = CELIX_SUCCESS;
	struct activator *activator = userData;



	return status;
}