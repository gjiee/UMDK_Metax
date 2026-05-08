#
# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
# Description: Run shell script for UT
# Create: 2026-1-13
# Note:
# History: 2026-1-13 create run_shell file
#

import os
import sys
import subprocess

# 获取脚本所在的目录
script_dir = os.path.dirname(os.path.abspath(__file__))

# 构建到run_with_cov.sh的相对路径
relative_path = os.path.join("..", "..", "..", "scripts", "comm_operator", "tool", "run_with_cov.sh")

# 获取run_with_cov.sh的完整路径
script_path = os.path.abspath(os.path.join(script_dir, relative_path))

# 获取所有命令行参数，排除第一个参数（脚本名称）
args = sys.argv[1:]

# 构建命令列表，使用绝对路径调用 run_with_cov.sh
print(f"run_shell {script_path} {args}")
command = ["bash", script_path] + args

# 运行命令
subprocess.run(command)
