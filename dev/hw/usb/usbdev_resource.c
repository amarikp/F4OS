#include <stddef.h>
#include <dev/resource.h>
#include <kernel/semaphore.h>
#include <kernel/fault.h>

#include "usbdev_internals.h"
#include "usbdev_desc.h"
#include <dev/hw/usbdev.h>

struct semaphore usbdev_semaphore = {
    .lock = 0,
    .held_by = NULL,
    .waiting = NULL
};

resource usb_console = {.writer = &usbdev_resource_write,
                        .reader = &usbdev_resource_read,
                        .closer = &usbdev_resource_close,
                        .env    = NULL,
                        .sem    = &usbdev_semaphore};

void usbdev_resource_write(char c, void *env) {
    usbdev_write(&ep_tx, (uint8_t *) &c, 1);
}

char usbdev_resource_read(void *env) {
    while (ring_buf_empty(&ep_rx.rx));

    char c = (char) ep_rx.rx.buf[ep_rx.rx.start];
    ep_rx.rx.start = (ep_rx.rx.start + 1) % ep_rx.rx.len;

    return c;
}

void usbdev_resource_close(struct resource *resource) {
    panic_print("USB is a fundamental resource, it may not be closed.");
}
