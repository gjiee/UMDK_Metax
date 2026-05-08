#
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
#

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ums.conf"
        DESTINATION /etc/modules-load.d/
        PERMISSIONS OWNER_WRITE OWNER_READ)