/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : ssl_connection.h
 * Description   : SSL connection header
 * History       : create file & add functions
 * 1.Date        : 2022-09-15
 * Author        : huying
 * Modification  : Created file
 */

#ifndef __SSL_CONNECTION_H__
#define __SSL_CONNECTION_H__

#include <unistd.h>
#include <string>
#include "openssl/ssl.h"

#include "dlock_types.h"
#include "dlock_common.h"
#include "dlock_connection.h"

namespace dlock {
class ssl_connection : public dlock_connection {
public:
    ssl_connection() = delete;
    explicit ssl_connection(int sockfd);
    ~ssl_connection() override;

    ssize_t send(const void *buf, size_t len, int flags) override;
    ssize_t recv(void *buf, size_t len, int flags) override;

    void set_fd(int fd) override;
    int get_fd() const override;

    bool is_ssl_enabled() const override;

    void tls_callback_register(const tls_cert_verify_callback_func_t cert_verify_cb,
        const tls_prkey_pwd_callback_func_t prkey_pwd_cb,
        const tls_erase_prkey_callback_func_t erase_prkey_cb);

    /**
     * Initialize the SSL lib, and build the TLS, openssl make sure
     * it can be call multi-times, but really do once.
     */
    int ssl_init(bool is_primary, const ssl_init_attr_t &init_attr);

    static int cert_verify_callback_wrapper(X509_STORE_CTX *ctx, void *arg);

private:
    void ssl_load_path_init(const std::string &ca_path, const std::string &crl_path,
        const std::string &cert_path, const std::string &prkey_path);
    int ssl_comm_load(bool is_primary);
    int ssl_verify_cfg(char *prkey_pwd);
    bool check_if_socket_timeout(const struct timeval &tv_start) const;
    ssize_t non_blocking_send(const void *buf, size_t len);
    ssize_t blocking_send(const void *buf, size_t len);
    ssize_t non_blocking_recv(void *buf, size_t len);
    ssize_t blocking_recv(void *buf, size_t len);

    int m_sockfd;
    SSL_CTX *m_ssl_ctx;
    SSL *m_ssl;

    std::string m_ca_path;
    std::string m_crl_path;
    std::string m_cert_path;
    std::string m_prkey_path;

    tls_cert_verify_callback_func_t m_cert_verify_cb;
    tls_prkey_pwd_callback_func_t m_prkey_pwd_cb;
    tls_erase_prkey_callback_func_t m_erase_prkey_cb;

    uint32_t m_socket_timeout;
};
};
#endif /* __SSL_CONNECTION_H__ */
