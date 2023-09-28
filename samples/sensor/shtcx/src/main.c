/*
 * Copyright (c) 2018 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include <zephyr/drivers/i2c.h>


#define ALERT_HUMIDITY_LO 50
#define ALERT_HUMIDITY_HI 60


/* defines matching the related enums DT_ENUM_IDX: */
#define CHIP_SHTC1		0
#define CHIP_SHTC3		1
#define MEASURE_MODE_NORMAL	0
#define MEASURE_MODE_LOW_POWER	1

enum shtcx_chip {
	SHTC1 = CHIP_SHTC1,
	SHTC3 = CHIP_SHTC3,
};

enum shtcx_measure_mode {
	NORMAL = MEASURE_MODE_NORMAL,
	LOW_POWER = MEASURE_MODE_LOW_POWER,
};


struct shtcx_config {
	struct i2c_dt_spec i2c;
	enum shtcx_chip chip;
	enum shtcx_measure_mode measure_mode;
	bool clock_stretching;
};

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ONE(sensirion_shtcx);
	int rc;

	printf("dev->name=%s\n",dev->name);
	const struct shtcx_config *shtcx_cfg = dev->config;
	printf("shtcx_cfg->chip=%d\n",shtcx_cfg->chip);
	printf("shtcx_cfg->measure_mode=%d\n",shtcx_cfg->measure_mode);
	printf("shtcx_cfg->clock_stretching=%d\n",shtcx_cfg->clock_stretching);

	struct i2c_dt_spec i2c_spce = shtcx_cfg->i2c;
	printf("i2c_spce->addr=0x%x\n",i2c_spce.addr);

	const struct device *i2c_device = i2c_spce.bus;
	printf("i2c_device->name=%s\n",i2c_device->name);


	printf("dev->state->initialized=%d\n", dev->state->initialized );
	printf("dev->state->init_res=%d\n", dev->state->init_res);

	if (!device_is_ready(dev)) {
		printf("Device %s is not ready\n", dev->name);
		return 0;
	}


	while (true) {
		struct sensor_value temp, hum;

		rc = sensor_sample_fetch(dev);
		if (rc == 0) {
			rc = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP,
						&temp);
		}
		if (rc == 0) {
			rc = sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY,
						&hum);
		}
		if (rc != 0) {
			printf("SHTCX: failed: %d\n", rc);
			break;
		}

		printf("SHTCX: %.2f Cel ; %0.2f %%RH\n",
		       sensor_value_to_double(&temp),
		       sensor_value_to_double(&hum));

		k_sleep(K_MSEC(2000));
	}
	return 0;
}
