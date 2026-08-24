#include "hydra/gate_c_shim_api.h"

#include <stdio.h>

_Static_assert(sizeof(HydraGateCShimConfigV1) ==
                   HYDRA_GATE_C_SHIM_CONFIG_V1_BYTES,
               "Gate C shim config ABI size changed");
_Static_assert(sizeof(HydraGateCShimConfigV2) ==
                   HYDRA_GATE_C_SHIM_CONFIG_V2_BYTES,
               "Gate C shim v2 config ABI size changed");
_Static_assert(sizeof(HydraGateCShimStatusV1) ==
                   HYDRA_GATE_C_SHIM_STATUS_V1_BYTES,
               "Gate C shim status ABI size changed");

int main(void) {
    if (hydra_gate_c_shim_api_version() != HYDRA_GATE_C_SHIM_API_VERSION) {
        fputs("Gate C shim C ABI version mismatch.\n", stderr);
        return 1;
    }
    HydraGateCShimStatusV1 status = {0};
    status.struct_size = (uint32_t)sizeof(status);
    if (hydra_gate_c_shim_get_status(&status) != HYDRA_GATE_C_SHIM_OK ||
        status.api_version != HYDRA_GATE_C_SHIM_API_VERSION ||
        status.expected_api_mask != HYDRA_GATE_C_SHIM_ALL_API_MASK ||
        status.module_kind != HYDRA_GATE_C_SHIM_MODULE_CURRENT_EXECUTABLE) {
        fputs("Gate C shim C ABI status query failed.\n", stderr);
        return 2;
    }
    puts("Gate C shim pure-C ABI smoke test passed.");
    return 0;
}
