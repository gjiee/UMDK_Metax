#
# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
#

add_library(ums-preload SHARED ums-preload.c)
target_link_libraries(ums-preload dl)

install(TARGETS ums-preload LIBRARY DESTINATION /usr/lib)
install(FILES ums_run 
        DESTINATION /usr/bin
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
        )