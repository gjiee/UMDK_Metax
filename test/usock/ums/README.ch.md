# 介绍
UMS基础功能验证用例

# 查询内核是否支持smc协议
cat /boot/config-$(uname -r) | grep CONFIG_SMC
显示CONFIG_SMC=m 表示当前内核版本是支持smc协议的，确认环境支持以后，再按照下面描述的步骤进行环境部署

# 依赖配置
UMS依赖URMA组件功能，使用前需保证URMA组件安装成功且正常配置。用例实现依赖googletest，请先安装googletest软件包。UMS为内核模块，如需查看代码覆盖率请在内核中打开CONFIG_GCOV_KERNEL相关选项

# 编译验证
## 使用ums提供脚本文件
1. 进入UMDK/test/usock/ums目录
2. （可选）清理环境:bash script/ut_cov.sh -c 
3. server端执行: bash script/ut_cov.sh -S -l ${SERVER_IP} -r ${CLIENT_IP} -L ${SERVER_IPV6} -R ${CLIENT_IPV6}
   client端执行: bash script/ut_cov.sh -C -l ${CLIENT_IP} -r ${SERVER_IP} -L ${CLIENT_IPV6} -R ${SERVER_IPV6}
   脚本参数详情见ut_cov.sh usage部分

注意：如果需要查看代码覆盖率请在执行用例之前执行如下命令清理历史结果
        echo 1 > /sys/kernel/debug/gcov/reset