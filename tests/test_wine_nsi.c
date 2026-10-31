/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's host check of Wine patches 0934 and 0987: without nsiproxy.sys, nsi.dll answers with
 * the loopback interface and a wired adapter with its cable out. tests/test.sh takes the
 * fallback from Wine's dlls/nsi/nsi.c with prospero-win's patches applied into
 * nsi_fallback.c and builds this around it, with Wine's own headers. */
#include "winsock2.h"
#include "winternl.h"
#include "ws2ipdef.h"
#include "iphlpapi.h"
#include "netioapi.h"
#include "iptypes.h"
#include "netiodef.h"
#include "wine/nsi.h"
#include <stdio.h>
#include <string.h>
static size_t test_wcslen(const WCHAR *str) { size_t n = 0; while (str[n]) n++; return n; }
#define wcslen test_wcslen
#define TRACE(...) ((void)0)
#define MESSAGE(...) ((void)0)
#define debugstr_guid(g) ""
NTSTATUS WINAPI NtQuerySystemInformation(SYSTEM_INFORMATION_CLASS c, void *i, ULONG l, ULONG *r) { (void)c; (void)i; (void)l; (void)r; return 1; }
#define GetLastError() 2u
#include "nsi_fallback.c"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("test_wine_nsi: failed: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)
int main(void)
{
    NET_LUID keys[4]; struct nsi_ndis_ifinfo_rw rw[4]; struct nsi_ndis_ifinfo_static st[4]; struct nsi_ndis_ifinfo_dynamic dyn;
    struct nsi_enumerate_all_ex e = {0};
    struct nsi_get_all_parameters_ex g = {0};
    struct nsi_get_parameter_ex p = {0};
    NET_LUID luid; UINT index;

    e.module = &nsi_fallback_ndis_moduleid; e.table = NSI_NDIS_IFINFO_TABLE;
    e.key_data = keys; e.key_size = sizeof(keys[0]); e.rw_data = rw; e.rw_size = sizeof(rw[0]);
    e.static_data = st; e.static_size = sizeof(st[0]);
    e.count = 0; CHECK(nsi_fallback_enumerate_all(&e) == ERROR_MORE_DATA);
    e.count = 1; CHECK(nsi_fallback_enumerate_all(&e) == ERROR_MORE_DATA && e.count == 1);
    memset(st, 0, sizeof(st));
    e.count = 4; CHECK(nsi_fallback_enumerate_all(&e) == ERROR_SUCCESS && e.count == 2);
    CHECK(st[0].type == MIB_IF_TYPE_LOOPBACK && st[0].if_index == 1);
    CHECK(st[1].type == MIB_IF_TYPE_ETHERNET && st[1].if_index == 2 && st[1].conn_present);
    CHECK(rw[1].phys_addr.Length == 6 && rw[1].phys_addr.Address[0] == 2 && rw[0].phys_addr.Length == 0);
    CHECK(keys[1].Info.NetLuidIndex == 2 && keys[1].Info.IfType == MIB_IF_TYPE_ETHERNET);
    CHECK(st[1].descr.Length == 16 * sizeof(WCHAR) && !memcmp(st[1].descr.String, L"Ethernet Adapter", st[1].descr.Length));
    CHECK(rw[1].alias.Length == 8 * sizeof(WCHAR) && !memcmp(rw[1].alias.String, L"Ethernet", rw[1].alias.Length));
    e.key_size = e.rw_size = e.static_size = 0; e.count = 0;
    CHECK(nsi_fallback_enumerate_all(&e) == ERROR_SUCCESS && e.count == 2);

    g.module = &nsi_fallback_ndis_moduleid; g.table = NSI_NDIS_IFINFO_TABLE; g.key = &keys[1]; g.key_size = sizeof(NET_LUID);
    g.dynamic_data = &dyn; g.dynamic_size = sizeof(dyn);
    CHECK(nsi_fallback_get_all_parameters(&g) == ERROR_SUCCESS && dyn.oper_status == IfOperStatusDown
          && dyn.media_conn_state == MediaConnectStateDisconnected);
    g.key = &keys[0]; CHECK(nsi_fallback_get_all_parameters(&g) == ERROR_SUCCESS && dyn.oper_status == IfOperStatusUp);
    luid.Value = 12345; g.key = &luid; CHECK(nsi_fallback_get_all_parameters(&g) == ERROR_FILE_NOT_FOUND);

    p.module = &nsi_fallback_ndis_moduleid; p.table = NSI_NDIS_INDEX_LUID_TABLE; p.key = &index; p.key_size = sizeof(index);
    p.param_type = NSI_PARAM_TYPE_STATIC; p.data = &luid; p.data_size = sizeof(luid);
    index = 2; CHECK(nsi_fallback_get_parameter(&p) == ERROR_SUCCESS && luid.Value == keys[1].Value);
    index = 1; CHECK(nsi_fallback_get_parameter(&p) == ERROR_SUCCESS && luid.Value == keys[0].Value);
    index = 3; CHECK(nsi_fallback_get_parameter(&p) == ERROR_FILE_NOT_FOUND);

    p.table = NSI_NDIS_IFINFO_TABLE; p.key = &keys[1]; p.key_size = sizeof(NET_LUID); p.data = &index; p.data_size = sizeof(index);
    p.data_offset = FIELD_OFFSET(struct nsi_ndis_ifinfo_static, if_index);
    CHECK(nsi_fallback_get_parameter(&p) == ERROR_SUCCESS && index == 2);

    /* the IP tables keep the loopback alone */
    {
        struct nsi_ipv4_unicast_key uk[4];
        struct nsi_enumerate_all_ex u = {0};
        u.module = &nsi_fallback_ipv4_moduleid; u.table = NSI_IP_UNICAST_TABLE; u.key_data = uk; u.key_size = sizeof(uk[0]); u.count = 4;
        CHECK(nsi_fallback_enumerate_all(&u) == ERROR_SUCCESS && u.count == 1 && uk[0].luid.Value == keys[0].Value);
    }
    if (!fails) printf("test_wine_nsi passed: loopback and unplugged adapter rows\n");
    return fails != 0;
}
