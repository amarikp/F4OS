#include <stdint.h>
#include <kernel/fault.h>
#include <dev/registers.h>

#include "usbdev_internals.h"
#include "usbdev_desc.h"
#include "usbdev_class.h"
#include <dev/hw/usbdev.h>

static void std_setup_packet(struct usbdev_setup_packet *setup);
static void cdc_setup_packet(struct usbdev_setup_packet *setup);
static void cdc_set_configuration(uint16_t configuration);

void usbdev_setup(uint32_t *packet, uint32_t len) {
    struct usbdev_setup_packet *setup = (struct usbdev_setup_packet *) packet;

    switch (setup->type) {
    case USB_SETUP_REQUEST_TYPE_TYPE_STD:
        std_setup_packet(setup);
        break;
    case USB_SETUP_REQUEST_TYPE_TYPE_CLASS:
        cdc_setup_packet(setup);
        break;
    default:
        printk("Unhandled SETUP packet, type %d. ", setup->type);
    }
}

static void std_setup_packet(struct usbdev_setup_packet *setup) {
    switch (setup->request) {
    case USB_SETUP_REQUEST_GET_DESCRIPTOR:
        printk("GET_DESCRIPTOR ");
        switch (setup->value >> 8) {
        case USB_SETUP_DESCRIPTOR_DEVICE:
            printk("DEVICE ");
            usbdev_write(endpoints[0], (uint32_t *) &usb_device_descriptor, sizeof(struct usb_device_descriptor));
            break;
        case USB_SETUP_DESCRIPTOR_CONFIG:
            printk("CONFIGURATION ");
            if (setup->length <= sizeof(usbdev_configuration1_descriptor)) {
                usbdev_write(endpoints[0], (uint32_t *) &usbdev_configuration1_descriptor, sizeof(usbdev_configuration1_descriptor));
            }
            else {
                usbdev_write(endpoints[0], (uint32_t *) &usbdev_configuration1, sizeof(usbdev_configuration1));
            }
            break;
        default:
            printk("OTHER DESCRIPTOR %d ", setup->value >> 8);
        }
        break;
    case USB_SETUP_REQUEST_SET_ADDRESS:
        printk("SET_ADDRESS %d ", setup->value);
        *USB_FS_DCFG |= USB_FS_DCFG_DAD(setup->value);
        usbdev_status_in_packet();
        break;
    case USB_SETUP_REQUEST_SET_CONFIGURATION:
        printk("SET_CONFIGURATION %d ", setup->value);
        cdc_set_configuration(setup->value);
        usbdev_status_in_packet();
        break;
    case USB_SETUP_REQUEST_GET_STATUS:
        printk("GET_STATUS ");
        if (setup->recipient == USB_SETUP_REQUEST_TYPE_RECIPIENT_DEVICE) {
            printk("DEVICE ");
            uint32_t buf = 0x11; /* Self powered and remote wakeup */
            usbdev_write(endpoints[0], &buf, sizeof(buf));
        }
        else {
            printk("OTHER ");
        }
        break;
    default:
        printk("STD: OTHER_REQUEST %d ", setup->request);
    }
}

static void cdc_setup_packet(struct usbdev_setup_packet *setup) {
    switch (setup->request) {
    case USB_SETUP_REQUEST_CDC_SET_CONTROL_LINE_STATE:
        printk("CDC: SET_CONTROL_LINE_STATE Warning: Not handled ");
        usbdev_status_in_packet();
        break;
    case USB_SETUP_REQUEST_CDC_SET_LINE_CODING:
        printk("CDC: SET_LINE_CODING Warning: Not handled ");
        usbdev_status_in_packet();
        break;
    default:
        printk("CDC: OTHER_REQUEST %d ", setup->request);
    }
}

static void cdc_set_configuration(uint16_t configuration) {
    if (configuration != 1) {
        printk("Warning: Cannot set configuration %u. ", configuration);
    }

    printk("Setting configuration %u. ", configuration);

    /* ACM Endpoint */
    *USB_FS_DIEPCTL(USB_CDC_ACM_ENDPOINT) |= USB_FS_DIEPCTLx_MPSIZE(USB_CDC_ACM_MPSIZE) | USB_FS_DIEPCTLx_EPTYP_INT | USB_FS_DIEPCTLx_TXFNUM(USB_CDC_ACM_ENDPOINT) | USB_FS_DIEPCTLx_USBAEP;

    /* RX Endpoint */
    *USB_FS_DOEPCTL(USB_CDC_RX_ENDPOINT) |= USB_FS_DOEPCTLx_MPSIZE(USB_CDC_RX_MPSIZE) | USB_FS_DOEPCTLx_EPTYP_BLK | USB_FS_DOEPCTLx_SD0PID | USB_FS_DOEPCTLx_EPENA | USB_FS_DOEPCTLx_USBAEP;

    /* TX Endpoint */
    *USB_FS_DIEPCTL(USB_CDC_TX_ENDPOINT) |= USB_FS_DIEPCTLx_MPSIZE(USB_CDC_TX_MPSIZE) | USB_FS_DIEPCTLx_EPTYP_BLK | USB_FS_DIEPCTLx_SD0PID | USB_FS_DIEPCTLx_TXFNUM(USB_CDC_TX_ENDPOINT) | USB_FS_DIEPCTLx_USBAEP;

    /* Flush TX FIFOs */
    *USB_FS_GRSTCTL |= USB_FS_GRSTCTL_TXFNUM(USB_CDC_TX_ENDPOINT) | USB_FS_GRSTCTL_TXFNUM(USB_CDC_ACM_ENDPOINT) | USB_FS_GRSTCTL_TXFFLSH;
    while (*USB_FS_GRSTCTL & USB_FS_GRSTCTL_TXFFLSH);

    /* Unmask interrupts */
    *USB_FS_DAINTMSK |= USB_FS_DAINT_IEPM(USB_CDC_ACM_ENDPOINT) | USB_FS_DAINT_IEPM(USB_CDC_TX_ENDPOINT) | USB_FS_DAINT_OEPM(USB_CDC_RX_ENDPOINT);
    *USB_FS_DIEPMSK |= USB_FS_DIEPMSK_XFRCM;

    endpoints[USB_CDC_ACM_ENDPOINT] = &ep_acm;
    endpoints[USB_CDC_RX_ENDPOINT] = &ep_rx;
    endpoints[USB_CDC_TX_ENDPOINT] = &ep_tx;

    usbdev_enable_receive(&ep_rx);
}
