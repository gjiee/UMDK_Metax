#
# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
# Description: Utility functions for UT
# Create: 2026-1-13
# Note:
# History: 2026-1-13s create tool file
#

import os
import numpy as np

def is_torchrun_process():
    # 通过检查环境变量中是否有 torchrun 设置的特定变量判断是否是通过torchrun拉起的进程
    return (
        "RANK" in os.environ
        and "WORLD_SIZE" in os.environ
        and "LOCAL_RANK" in os.environ
        and "LOCAL_WORLD_SIZE" in os.environ
    )

def get_rank():
    if "RANK" in os.environ:
        return int(os.environ["RANK"])
    return -1

def get_local_rank():
    if "LOCAL_RANK" in os.environ:
        return int(os.environ["LOCAL_RANK"])
    return -1

def get_world_size():
    if "WORLD_SIZE" in os.environ:
        return int(os.environ["WORLD_SIZE"])
    return -1

def get_local_world_size():
    if "LOCAL_WORLD_SIZE" in os.environ:
        return int(os.environ["LOCAL_WORLD_SIZE"])
    return -1

def is_run_for_cov():
    if "ENABLE_COV" in os.environ:
        return True
    return False

def _count_unequal_element(data_expect, data_check, rtol, atol, msg=""):
    assert data_expect.shape == data_check.shape
    total_count = len(data_expect.flatten())
    error = np.abs(data_expect - data_check)
    greater = np.greater(error, atol + np.abs(data_check) * rtol)
    loss_count = np.count_nonzero(greater)
    assert (
        loss_count / total_count
    ) < rtol, "\nmsg{0}_data_expect_std:{1}\ndata_check_error:{2}\nloss:{3}".format(
        msg, data_expect[greater], data_check[greater], error[greater]
    )

def allclose_nparray(data_expect, data_check, rtol=1e-4, atol=1e-4, equal_nan=True, msg=""):
    if np.any(np.isnan(data_expect)):
        assert np.allclose(data_expect, data_check, rtol, atol, equal_nan=equal_nan)
    elif not np.allclose(data_expect, data_check, rtol, atol, equal_nan=equal_nan):
        _count_unequal_element(data_expect, data_check, rtol, atol, msg)
    else:
        assert True
