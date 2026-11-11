# 首启：让用户自定义登录名（家目录固定 /home/j716f）
# 由构建时安装到 /etc/profile.d/，只在 user-setup-pending 存在时触发一次。
if [ -e /etc/j716f/user-setup-pending ] && [ "$(id -u)" = 1000 ] && [ -t 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        sudo -n /usr/local/sbin/j716f-user-setup.sh 2>/dev/null || \
            exec sudo /usr/local/sbin/j716f-user-setup.sh
    else
        exec /usr/local/sbin/j716f-user-setup.sh
    fi
fi
