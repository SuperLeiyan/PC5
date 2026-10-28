/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's host check of Wine patch 0935: the MOVs through %fs: that Wine's signal handler does
 * on the 32-bit TEB when the kernel didn't apply an FS base repair (patch 0928). tests/test.sh
 * takes the functions from Wine's dlls/ntdll/unix/signal_x86_64.c with prospero-win's
 * patches applied (pw_has_modrm to pw_emulate_fs_mov) into fs_funcs.c, and builds this
 * around them, with a register frame and a TEB of its own. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef int BOOL; typedef uint8_t BYTE; typedef uint32_t DWORD; typedef size_t SIZE_T; typedef long NTSTATUS;
#define TRUE 1
#define FALSE 0
typedef struct { uint64_t rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, rip; } ucontext_t;
#define RAX_sig(c) ((c)->rax)
#define RCX_sig(c) ((c)->rcx)
#define RDX_sig(c) ((c)->rdx)
#define RBX_sig(c) ((c)->rbx)
#define RSP_sig(c) ((c)->rsp)
#define RBP_sig(c) ((c)->rbp)
#define RSI_sig(c) ((c)->rsi)
#define RDI_sig(c) ((c)->rdi)
#define RIP_sig(c) ((c)->rip)
/* The guest's TEB at 0x7ffda000: a page here, read and written as Wine's virtual memory would. */
static BYTE teb[0x1000];
#define TEB 0x7ffda000u
static void *ULongToPtr(DWORD v) { return (void *)(uintptr_t)v; }
static SIZE_T virtual_uninterrupted_read_memory(const void *a, void *b, SIZE_T n)
{ uintptr_t at = (uintptr_t)a; if (at < TEB || at + n > TEB + sizeof(teb)) return 0; memcpy(b, teb + (at - TEB), n); return n; }
static NTSTATUS virtual_uninterrupted_write_memory(void *a, const void *b, SIZE_T n)
{ uintptr_t at = (uintptr_t)a; if (at < TEB || at + n > TEB + sizeof(teb)) return 1; memcpy(teb + (at - TEB), b, n); return 0; }
#include "fs_funcs.c"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
int main(void)
{
    ucontext_t c;
    DWORD v = 0x7ffda000; memcpy(teb + 0x18, &v, 4); v = 0x1234; memcpy(teb + 0x24, &v, 4); teb[0x30] = 0xab;
    /* 64 a1 18000000: mov eax, fs:[0x18] (RtlEnterCriticalSection, NtCurrentTeb) */
    BYTE a1[] = { 0x64, 0xa1, 0x18, 0, 0, 0, 0x8b, 0x40 };
    memset(&c, 0, sizeof(c)); c.rip = 0x7bcb15f0; c.rax = 0xffffffff12345678ull;
    CHECK(pw_emulate_fs_mov(&c, a1, sizeof(a1), TEB) && c.rax == 0x7ffda000 && c.rip == 0x7bcb15f6, "a1: %llx %llx", (unsigned long long)c.rax, (unsigned long long)c.rip);
    /* 64 8b 0d 24000000: mov ecx, fs:[0x24] */
    BYTE m8b[] = { 0x64, 0x8b, 0x0d, 0x24, 0, 0, 0 };
    memset(&c, 0, sizeof(c)); c.rip = 100;
    CHECK(pw_emulate_fs_mov(&c, m8b, sizeof(m8b), TEB) && c.rcx == 0x1234 && c.rip == 107, "8b disp32: %llx %llx", (unsigned long long)c.rcx, (unsigned long long)c.rip);
    /* 64 8b 40 18: mov eax, fs:[eax+0x18] with eax 0 */
    BYTE m8b8[] = { 0x64, 0x8b, 0x40, 0x18 };
    memset(&c, 0, sizeof(c)); c.rip = 100;
    CHECK(pw_emulate_fs_mov(&c, m8b8, sizeof(m8b8), TEB) && c.rax == 0x7ffda000 && c.rip == 104, "8b disp8 %llx", (unsigned long long)c.rax);
    /* 64 8b 04 8d 18000000: mov eax, fs:[ecx*4+0x18], ecx 3 -> 0x24 */
    BYTE sib[] = { 0x64, 0x8b, 0x04, 0x8d, 0x18, 0, 0, 0 };
    memset(&c, 0, sizeof(c)); c.rcx = 3; c.rip = 0;
    CHECK(pw_emulate_fs_mov(&c, sib, sizeof(sib), TEB) && c.rax == 0x1234 && c.rip == 8, "sib %llx %llx", (unsigned long long)c.rax, (unsigned long long)c.rip);
    /* 64 8a 3d 30000000: mov bh, fs:[0x30] */
    BYTE m8a[] = { 0x64, 0x8a, 0x3d, 0x30, 0, 0, 0 };
    memset(&c, 0, sizeof(c)); c.rbx = 0x11223344;
    CHECK(pw_emulate_fs_mov(&c, m8a, sizeof(m8a), TEB) && c.rbx == 0x1122ab44 && c.rip == 7, "bh %llx", (unsigned long long)c.rbx);
    /* 64 89 15 40000000: mov fs:[0x40], edx */
    BYTE m89[] = { 0x64, 0x89, 0x15, 0x40, 0, 0, 0 };
    memset(&c, 0, sizeof(c)); c.rdx = 0xcafef00d;
    CHECK(pw_emulate_fs_mov(&c, m89, sizeof(m89), TEB) && !memcmp(teb + 0x40, "\x0d\xf0\xfe\xca", 4) && c.rip == 7, "89 store");
    /* 64 a3 44000000: mov fs:[0x44], eax */
    BYTE a3[] = { 0x64, 0xa3, 0x44, 0, 0, 0 };
    memset(&c, 0, sizeof(c)); c.rax = 0x01020304;
    CHECK(pw_emulate_fs_mov(&c, a3, sizeof(a3), TEB) && !memcmp(teb + 0x44, "\x04\x03\x02\x01", 4) && c.rip == 6, "a3 store");
    /* Not done here: no fs prefix, a register operand, another instruction, 16-bit, cut short, out of the TEB. */
    BYTE nofs[] = { 0xa1, 0x18, 0, 0, 0 }, regop[] = { 0x64, 0x8b, 0xc0 }, add[] = { 0x64, 0x03, 0x05, 0x18, 0, 0, 0 },
         w16[] = { 0x66, 0x64, 0x8b, 0x05, 0x18, 0, 0, 0 }, cut[] = { 0x64, 0xa1, 0x18, 0 }, far[] = { 0x64, 0xa1, 0, 0x20, 0, 0 };
    memset(&c, 0, sizeof(c));
    CHECK(!pw_emulate_fs_mov(&c, nofs, sizeof(nofs), TEB) && !pw_emulate_fs_mov(&c, regop, sizeof(regop), TEB) &&
          !pw_emulate_fs_mov(&c, add, sizeof(add), TEB) && !pw_emulate_fs_mov(&c, w16, sizeof(w16), TEB) &&
          !pw_emulate_fs_mov(&c, cut, sizeof(cut), TEB) && !pw_emulate_fs_mov(&c, far, sizeof(far), TEB) && c.rip == 0, "refused");
    printf("%s\n", fails ? "test_wine_fs: FAILED"
                         : "test_wine_fs: the %fs: MOVs Wine does when the kernel didn't apply an FS repair: passed");
    return fails != 0;
}
