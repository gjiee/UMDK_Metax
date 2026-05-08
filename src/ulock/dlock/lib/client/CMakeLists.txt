# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2020-2025. All rights reserved.

# Find required packages
find_package(GLIB REQUIRED COMPONENTS gobject)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} DLOCK_CLIENT_SRCS)

add_library(dlockc SHARED ${DLOCK_CLIENT_SRCS})

set_target_properties(dlockc PROPERTIES CXX_STANDARD 17)

if (PROJECT_VERSION)
    set_target_properties(dlockc PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
endif()

target_link_libraries(dlockc urma urma_common gobject-2.0 glib-2.0 -ldl -lrt dlockm)

install(TARGETS dlockc DESTINATION /usr/lib64)