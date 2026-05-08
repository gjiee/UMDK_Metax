# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2020-2025. All rights reserved.

# Find required packages
find_package(GLIB REQUIRED COMPONENTS gobject)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} DLOCK_SERVER_SRCS)

add_library(dlocks SHARED ${DLOCK_SERVER_SRCS})

set_target_properties(dlocks PROPERTIES CXX_STANDARD 17)

if (PROJECT_VERSION)
    set_target_properties(dlocks PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
endif()

target_link_libraries(dlocks urma urma_common gobject-2.0 glib-2.0 -ldl -lrt dlockm)

install(TARGETS dlocks DESTINATION /usr/lib64)
