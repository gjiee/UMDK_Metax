# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2020-2025. All rights reserved.

# Find required packages
find_package(GLIB REQUIRED COMPONENTS gobject)
find_package(OpenSSL REQUIRED)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} DLOCK_COMMON_SRCS)

add_library(dlockm SHARED ${DLOCK_COMMON_SRCS})

set_target_properties(dlockm PROPERTIES CXX_STANDARD 17)

if (PROJECT_VERSION)
    set_target_properties(dlockm PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
endif()

target_link_libraries(dlockm urma urma_common gobject-2.0 glib-2.0 -ldl -lrt ssl crypto)

install(TARGETS dlockm DESTINATION /usr/lib64)
