# Introduction
UMS Basic Functional Test Cases.

# How to Check if the Kernel Supports the SMC Protocol
cat /boot/config-$(uname -r) | grep CONFIG_SMC
If the command output shows CONFIG_SMC=m, it indicates that the current kernel version supports the SMC protocol. Once the environment compatibility is confirmed, proceed with the deployment steps described below.

# Dependencies
1. UMS relies on the functionality of the URMA component. Before use, ensure that the URMA component is successfully installed and properly configured.
2. The implementation of test cases relies on googletest. Please install the googletest package first.
3. UMS is a kernel module. To enable code coverage analysis, turn on the CONFIG_GCOV_KERNEL option in the kernel configuration.

# Build
1. Navigate to the UMDK/test/usock/ums directory.
2. (Optional) Clean up the environment: bash script/ut_cov.sh -c 
3. Server-side execution: bash script/ut_cov.sh -S -l ${SERVER_IP} -r ${CLIENT_IP} -L ${SERVER_IPV6} -R ${CLIENT_IPV6}
   Client-side execution: bash script/ut_cov.sh -C -l ${CLIENT_IP} -r ${SERVER_IP} -L ${CLIENT_IPV6} -R ${SERVER_IPV6}
   For detailed script parameters, refer to the ut_cov.sh usage section.

Note: If code coverage analysis is required, please run the following command to clear historical results before executing test cases:
   echo 1 > /sys/kernel/debug/gcov/reset