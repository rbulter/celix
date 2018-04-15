# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
# 
#   http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

set(SOURCES
        src/main.cc
        src/cxx_Framework_tests.cc
        src/cxx_FrameworkFactory_tests.cc
        src/cxx_BundleContext_tests.cc
        src/cxx_Bundle_tests.cc
)
add_executable(cxx_framework_tests ${SOURCES})
target_link_libraries(cxx_framework_tests PRIVATE gtest Celix::framework)


add_celix_bundle(cxx_fwm_test_bundle1
        VERSION 1.0.0
        NO_ACTIVATOR
)

add_custom_command(TARGET cxx_framework_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_PROPERTY:cxx_fwm_test_bundle1,BUNDLE_FILE> ${CMAKE_CURRENT_BINARY_DIR}/bundle1.zip
)
add_celix_bundle_dependencies(cxx_framework_tests cxx_fwm_test_bundle1)