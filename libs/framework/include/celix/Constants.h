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


#ifndef CXX_CELIX_CONSTANTS_H
#define CXX_CELIX_CONSTANTS_H

#include "celix_constants.h"

namespace celix {
    class Constants {
    public:
        static constexpr const char *const SERVICE_NAME = OSGI_FRAMEWORK_OBJECTCLASS;
        static constexpr const char *const SERVICE_ID = OSGI_FRAMEWORK_SERVICE_ID;
        static constexpr const char *const SERVICE_PID = OSGI_FRAMEWORK_SERVICE_PID;
        static constexpr const char *const SERVICE_RANKING = OSGI_FRAMEWORK_SERVICE_RANKING;

        static constexpr const char *const SERVICE_VERSION = CELIX_FRAMEWORK_SERVICE_VERSION;
        static constexpr const char *const SERVICE_LANGUAGE = CELIX_FRAMEWORK_SERVICE_LANGUAGE;
        static constexpr const char *const SERVICE_C_LANG = CELIX_FRAMEWORK_SERVICE_C_LANGUAGE;
        static constexpr const char *const SERVICE_CXX_LANG = CELIX_FRAMEWORK_SERVICE_CXX_LANGUAGE;
        static constexpr const char *const SERVICE_SHARED_LANG = CELIX_FRAMEWORK_SERVICE_SHARED_LANGUAGE; //e.g. marker services

        static constexpr const char *const FRAMEWORK_STORAGE = OSGI_FRAMEWORK_FRAMEWORK_STORAGE;
        static constexpr const char *const FRAMEWORK_CLEAN = OSGI_FRAMEWORK_FRAMEWORK_STORAGE_CLEAN;
        static constexpr const char *const FRAMEWORK_CLEAN_ON_FIRST_INIT = OSGI_FRAMEWORK_FRAMEWORK_STORAGE_CLEAN_ONFIRSTINIT;
        static constexpr const char *const FRAMEWORK_UUID = OSGI_FRAMEWORK_FRAMEWORK_UUID;
    };
}

#endif //CXX_CELIX_CONSTANTS_H
