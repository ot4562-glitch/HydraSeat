#include "hydra/gate_c_adapter.h"

#include <stdint.h>
#include <stdio.h>

int main(void) {
    if (hydra_gate_c_adapter_api_version() !=
        HYDRA_GATE_C_ADAPTER_API_VERSION) {
        fputs("Gate C adapter C ABI version mismatch.\n", stderr);
        return 1;
    }

    HydraGateCAdapterHandle handle = hydra_gate_c_adapter_create();
    if (handle == NULL) {
        fputs("Gate C adapter context creation failed.\n", stderr);
        return 2;
    }

    HydraGateCAdapterControlStateV1 control = {0};
    control.struct_size = (uint32_t)sizeof(control);
    control.cursor_x = 12;
    control.cursor_y = 34;
    control.virtual_foreground = 1;
    if (hydra_gate_c_adapter_apply_control(handle, 1, &control) !=
        HYDRA_GATE_C_ADAPTER_OK) {
        hydra_gate_c_adapter_destroy(handle);
        return 3;
    }

    uint16_t key_state = 0xffffu;
    if (hydra_gate_c_adapter_get_key_state(handle, 0x41u, &key_state) !=
            HYDRA_GATE_C_ADAPTER_OK ||
        key_state != 0u) {
        hydra_gate_c_adapter_destroy(handle);
        return 4;
    }

    HydraGateCAdapterControlStateV1 queried = {0};
    queried.struct_size = (uint32_t)sizeof(queried);
    if (hydra_gate_c_adapter_get_control_state(handle, &queried) !=
            HYDRA_GATE_C_ADAPTER_OK ||
        queried.cursor_x != 12 || queried.cursor_y != 34 ||
        queried.virtual_foreground != 1u) {
        hydra_gate_c_adapter_destroy(handle);
        return 5;
    }

    hydra_gate_c_adapter_destroy(handle);
    puts("Gate C adapter pure-C ABI smoke test passed.");
    return 0;
}
