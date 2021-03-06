/*
 * Copyright (C) 2013, 2014 F4OS Authors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <libfdt.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <dev/fdtparse.h>
#include <dev/hw/i2c.h>
#include <dev/rotary_encoder.h>
#include <kernel/init.h>
#include <kernel/mutex.h>
#include <mm/mm.h>

#define AS5048B_COMPAT  "ams,as5048b"

/* Registers */
#define AS5048B_PROG    (3)
#define AS5048B_ADDR    (21)
#define AS5048B_OTPH    (22)
#define AS5048B_DIAG    (251)
#define AS5048B_ANGLEH  (254)

#define AS5048B_PROG_VERIFY (1 << 6)
#define AS5048B_PROG_BURN   (1 << 3)
#define AS5048B_PROG_ENA    (1 << 0)

struct as5048b {
    struct mutex    lock;
    uint32_t calibration_zero;
    int addr;
};

static int as5048b_init(struct rotary_encoder *rotary_encoder) {
    /* No initialization required */

    return 0;
}

static int as5048b_deinit(struct rotary_encoder *rotary_encoder) {
    /* No deinitialization */

    return 0;
}

static int as5048b_get_raw_data(struct rotary_encoder *rotary_encoder,
                                struct rotary_encoder_raw_data *data) {
    struct i2c *i2c = to_i2c(rotary_encoder->device.parent);
    struct i2c_ops *i2c_ops = (struct i2c_ops *)i2c->obj.ops;
    struct as5048b *ams_rotary_encoder = (struct as5048b *) rotary_encoder->priv;
    uint8_t raw_data[2];
    int ret;

    acquire(&ams_rotary_encoder->lock);

    /* Start reading from ANGLEH */
    raw_data[0] = AS5048B_ANGLEH;
    ret = i2c_ops->write(i2c, ams_rotary_encoder->addr, raw_data, 1);
    if (ret != 1) {
        goto out_err;
    }

    /* Read the two data registers */
    ret = i2c_ops->read(i2c, ams_rotary_encoder->addr, raw_data, 2);
    if (ret != 2) {
        goto out_err;
    }

    release(&ams_rotary_encoder->lock);

    data->angle = (uint32_t) (raw_data[1] << 6) | (raw_data[0] & 0x3F);

    /* Apply calibration */
    data->angle = (data->angle - ams_rotary_encoder->calibration_zero) & 0x3fff;

    return 0;

out_err:
    release(&ams_rotary_encoder->lock);
    return -1;
}

static int as5048b_get_data(struct rotary_encoder *rotary_encoder,
                            struct rotary_encoder_data *data) {
    struct rotary_encoder_ops *rotary_encoder_ops =
        (struct rotary_encoder_ops *) rotary_encoder->obj.ops;
    struct rotary_encoder_raw_data raw_data;
    int ret;

    ret = rotary_encoder_ops->get_raw_data(rotary_encoder, &raw_data);
    if (ret) {
        return ret;
    }

    /* Scale angle from 0..16384 to 0..2pi */
    data->angle = ((float)raw_data.angle)/(1 << 14) * 2 * FLOAT_PI;

    return 0;
}

/**
 * Perform software calibration of AS5048B.
 *
 * This will set the zero position of the encoder to the passed in value.
 * The calibration value will be used to correct raw data read from the
 * encoder.
 *
 * @param value Zero angle value.  This should be the 14-bit raw value from
 *                  get_raw_data() to be used as the center point.
 * @returns zero on success, negative on error
 */
static int as5048b_calibrate(struct rotary_encoder *rotary_encoder,
                             uint32_t value) {
    struct as5048b *ams_rotary_encoder =
        (struct as5048b *) rotary_encoder->priv;

    acquire(&ams_rotary_encoder->lock);

    /* Add new value to current calibration */
    ams_rotary_encoder->calibration_zero =
        (ams_rotary_encoder->calibration_zero + value) % (1 << 14);

    release(&ams_rotary_encoder->lock);
    return 0;
}

/**
 * Get AS5048B status
 *
 * This will return the diagnostic register from the AS5048B.
 *
 * Additional details can be found in the datasheet, but the
 * bits are as follows:
 * Bit 0: OCF - Offset compensation finished
 * Bit 1: COF - Cordic Overflow
 * Bit 2: Comp Low - Weak magnetic field
 * Bit 3: Comp High - High magnetic field
 *
 * @returns contents of diagnostic register, -1 on error
 */
static int as5048b_status(struct rotary_encoder *rotary_encoder) {
    struct i2c *i2c = to_i2c(rotary_encoder->device.parent);
    struct i2c_ops *i2c_ops = (struct i2c_ops *)i2c->obj.ops;
    struct as5048b *ams_rotary_encoder = (struct as5048b *) rotary_encoder->priv;
    uint8_t data;
    int ret;

    acquire(&ams_rotary_encoder->lock);

    /* Start reading from DIAG */
    data = AS5048B_DIAG;
    ret = i2c_ops->write(i2c, ams_rotary_encoder->addr, &data, 1);
    if (ret != 1) {
        goto out_err;
    }

    /* Read the register */
    ret = i2c_ops->read(i2c, ams_rotary_encoder->addr, &data, 1);
    if (ret != 1) {
        goto out_err;
    }

    release(&ams_rotary_encoder->lock);

    return data;

out_err:
    release(&ams_rotary_encoder->lock);
    return -1;
}

struct rotary_encoder_ops as5048b_ops = {
    .init = as5048b_init,
    .deinit = as5048b_deinit,
    .get_data = as5048b_get_data,
    .get_raw_data = as5048b_get_raw_data,
    .calibrate = as5048b_calibrate,
    .status = as5048b_status,
};

/*
 * Attempt to identify the chip by verifying that the I2C address register
 * matches the 5 MSBs of the actual address
 */
static int as5048b_probe(const char *name) {
    const void *blob = fdtparse_get_blob();
    int offset, parent_offset, addr, err;
    char *parent;
    struct obj *i2c_obj;
    struct i2c *i2c;
    struct i2c_ops *i2c_ops;
    uint8_t data;
    int ret = 0;

    offset = fdt_path_offset(blob, name);
    if (offset < 0) {
        return 0;
    }

    if (fdt_node_check_compatible(blob, offset, AS5048B_COMPAT)) {
        return 0;
    }

    parent_offset = fdt_parent_offset(blob, offset);
    if (parent_offset < 0) {
        return 0;
    }

    parent = fdtparse_get_path(blob, parent_offset);
    if (!parent) {
        return 0;
    }

    i2c_obj = device_get(parent);
    if (!i2c_obj) {
        goto out_free_parent;
    }

    i2c = to_i2c(i2c_obj);
    i2c_ops = (struct i2c_ops *)i2c->obj.ops;

    err = fdtparse_get_int(blob, offset, "reg", &addr);
    if (err) {
        goto out;
    }

    /* Attempt read of ADDR register */
    data = AS5048B_ADDR;
    err = i2c_ops->write(i2c, addr, &data, 1);
    if (err != 1) {
        ret = 0;
        goto out;
    }

    err = i2c_ops->read(i2c, addr, &data, 1);
    if (err != 1) {
        ret = 0;
        goto out;
    }

    /*
     * The ADDR register contains the 5 MSBs of the 7-bit I2C address.
     * Bit 4 in the register is inverted (why??)
     * Verify that is matches the 5 MSBs of the address from FDT.
     */
    ret = ((data ^ 0x10) & 0x1f) == (addr >> 2);

out:
    device_put(i2c_obj);
out_free_parent:
    free(parent);
    return ret;
}

static struct obj *as5048b_ctor(const char *name) {
    const void *blob = fdtparse_get_blob();
    int offset, parent_offset, err;
    char *parent;
    struct obj *rotary_encoder_obj;
    struct rotary_encoder *rotary_encoder;
    struct as5048b *ams_rotary_encoder;

    offset = fdt_path_offset(blob, name);
    if (offset < 0) {
        return NULL;
    }

    if (fdt_node_check_compatible(blob, offset, AS5048B_COMPAT)) {
        return NULL;
    }

    parent_offset = fdt_parent_offset(blob, offset);
    if (parent_offset < 0) {
        return NULL;
    }

    parent = fdtparse_get_path(blob, parent_offset);
    if (!parent) {
        return NULL;
    }

    /* Instantiate an rotary_encoder obj with as5048b ops */
    rotary_encoder_obj = instantiate(name, &rotary_encoder_class,
                                     &as5048b_ops, struct rotary_encoder);
    if (!rotary_encoder_obj) {
        goto err_free_parent;
    }

    /* Connect rotary_encoder to its parent I2C device */
    rotary_encoder = to_rotary_encoder(rotary_encoder_obj);
    rotary_encoder->device.parent = device_get(parent);
    if (!rotary_encoder->device.parent) {
        goto err_free_obj;
    }

    /* Set up private data */
    rotary_encoder->priv = kmalloc(sizeof(struct as5048b));
    if (!rotary_encoder->priv) {
        goto err_free_obj;
    }

    ams_rotary_encoder = (struct as5048b *) rotary_encoder->priv;
    init_mutex(&ams_rotary_encoder->lock);
    ams_rotary_encoder->calibration_zero = 0;

    err = fdtparse_get_int(blob, offset, "reg", &ams_rotary_encoder->addr);
    if (err) {
        goto err_free_obj;
    }

    /* Export to the OS */
    class_export_member(rotary_encoder_obj);

    free(parent);

    return rotary_encoder_obj;

err_free_obj:
    class_deinstantiate(rotary_encoder_obj);
err_free_parent:
    free(parent);
    return NULL;
}

static struct mutex as5048b_driver_mut = INIT_MUTEX;

static struct device_driver as5048b_compat_driver = {
    .name = AS5048B_COMPAT,
    .probe = as5048b_probe,
    .ctor = as5048b_ctor,
    .class = &rotary_encoder_class,
    .mut = &as5048b_driver_mut,
};

static int as5048b_register(void) {
    device_compat_driver_register(&as5048b_compat_driver);

    return 0;
}
CORE_INITIALIZER(as5048b_register)
