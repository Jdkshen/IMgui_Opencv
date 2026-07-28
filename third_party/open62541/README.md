# open62541

This directory contains the open62541 v1.4.17 amalgamation used by the native
OPC UA adapter.

> 项目复核日期：2026-07-27。此文件保留上游构建参数原文，中文接入与安全边界见 [硬件接入说明](../../docs/HARDWARE_INTEGRATION.md#opc-ua)。

- Upstream: https://github.com/open62541/open62541
- Commit: `17b65bc21791c1bcac4836443c37af12f8577882`
- License: Mozilla Public License 2.0 (`LICENSE`)
- Architecture: Win32 socket backend, x64 build
- Security: `UA_ENABLE_ENCRYPTION=OFF`
- Amalgamation: `UA_ENABLE_AMALGAMATION=ON`
- Runtime: `UA_MSVC_FORCE_STATIC_CRT=OFF` (`/MD` Release, `/MDd` Debug)

The checked-in `open62541.c` and `open62541.h` correspond exactly to the static
libraries in `redist/open62541.lib` and `redist/open62541d.lib`. The current
adapter supports anonymous OPC UA TCP endpoints using SecurityPolicy None.
Certificate-based security must use a separately built encryption-enabled
open62541 package and should not silently fall back to an unsecured endpoint.
