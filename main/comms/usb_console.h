#pragma once

#include "storage/factory_reset.h"

void d1l_usb_console_run(void);
void d1l_usb_console_run_factory_reset_recovery(
    const d1l_factory_reset_status_t *boot_status);
