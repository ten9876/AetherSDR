#include "core/DaxTxPolicy.h"
#include <cstdio>
#include <QString>

using namespace AetherSDR;

static int g_failed = 0;

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) { \
        printf("FAILED: %s\n", msg); \
        g_failed++; \
    }

#define ASSERT_FALSE(cond, msg) ASSERT_TRUE(!(cond), msg)

void testDaxTxPolicy() {
    // Test RadeModemTx (Always allowed on all platforms)
    {
        DaxTxPolicyContext ctx;
        ctx.reason = DaxTxRequestReason::RadeModemTx;
        ctx.platform = DaxTxPlatform::Windows;
        auto [allowed, reason] = evaluateDaxTxPolicy(ctx);
        ASSERT_TRUE(allowed, "RadeModemTx should be allowed on Windows");

        ctx.platform = DaxTxPlatform::MacOS;
        auto [allowed2, reason2] = evaluateDaxTxPolicy(ctx);
        ASSERT_TRUE(allowed2, "RadeModemTx should be allowed on MacOS");
    }

    // Test TciTxAudio (Always allowed on all platforms)
    {
        DaxTxPolicyContext ctx;
        ctx.reason = DaxTxRequestReason::TciTxAudio;
        auto [allowed, reason] = evaluateDaxTxPolicy(ctx);
        ASSERT_TRUE(allowed, "TciTxAudio should be allowed");
    }

    // Test HostedDaxBridge on Windows (Blocked)
    {
        DaxTxPolicyContext ctx;
        ctx.reason = DaxTxRequestReason::HostedDaxBridge;
        ctx.platform = DaxTxPlatform::Windows;
        ctx.mode = DaxTxMode::ExternalDax2;
        auto [allowed, reason] = evaluateDaxTxPolicy(ctx);
        ASSERT_FALSE(allowed, "HostedDaxBridge should be blocked on Windows");
        ASSERT_TRUE(reason.contains("windows_dax_conflict"), "Reason should mention windows_dax_conflict");
    }

    // Test HostedDaxBridge on non-Windows (Allowed if available)
    {
        DaxTxPolicyContext ctx;
        ctx.reason = DaxTxRequestReason::HostedDaxBridge;
        ctx.platform = DaxTxPlatform::MacOS;
        ctx.mode = DaxTxMode::HostedDax;
        ctx.hostedDaxAvailable = true;
        auto [allowed, reason] = evaluateDaxTxPolicy(ctx);
        ASSERT_TRUE(allowed, "HostedDaxBridge should be allowed on MacOS if available");
    }

    // Test ExternalDaxRouteOnly on Windows (Blocked)
    {
        DaxTxPolicyContext ctx;
        ctx.reason = DaxTxRequestReason::ExternalDaxRouteOnly;
        ctx.platform = DaxTxPlatform::Windows;
        ctx.mode = DaxTxMode::ExternalDax2;
        auto [allowed, reason] = evaluateDaxTxPolicy(ctx);
        ASSERT_FALSE(allowed, "ExternalDaxRouteOnly should be blocked on Windows");
        ASSERT_TRUE(reason.contains("windows_dax_conflict"), "Reason should mention windows_dax_conflict");
    }
}

void testReasonNames() {
    ASSERT_TRUE(daxTxRequestReasonName(DaxTxRequestReason::RadeModemTx) == "rade_modem_tx", "Name should be rade_modem_tx");
    ASSERT_TRUE(daxTxRequestReasonName(DaxTxRequestReason::TciTxAudio) == "tci_tx_audio", "Name should be tci_tx_audio");
}

int main() {
    testDaxTxPolicy();
    testReasonNames();

    if (g_failed == 0) {
        printf("All DaxTxPolicy tests passed!\n");
        return 0;
    } else {
        printf("%d tests failed.\n", g_failed);
        return 1;
    }
}
