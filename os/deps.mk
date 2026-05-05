# deps.mk - Dependency registry. Add new items here for easier maintenance.
# Include from Makefile.
#
# Dependency order (add lower layers first, like: HTTPS needs TCP/UDP first):
#
#   NIC/driver (rtl8139, drfl)
#       |
#   net.c (IP, UDP, TCP)
#       |
#   HTTPS: lard_tls 자체 구현 예정 (현재 disabled)
#
# New feature 추가 시: 상위 기능은 하위 의존성을 먼저 추가해야 함.

# ---------------------------------------------------------------------------
# Kernel libs (kernel/*.c) - add name only when adding a new lib
# ---------------------------------------------------------------------------
KERNEL_LIBS := hash ringbuf base64

# ---------------------------------------------------------------------------
# LDLL libraries - add name after implementing in mkldll.c
# 미리 추가 (의존성 지옥 방지): hash, base64, fs
# ---------------------------------------------------------------------------
LDLL_LIBS := lard gui lafillo ldll hash base64 fs

# ---------------------------------------------------------------------------
# Third-party (add in dependency order; 하위 의존성 먼저)
# ---------------------------------------------------------------------------
THIRD_PARTY_INCLUDES := third_party/mbedtls/include
