/*
 * Copyright (c) 2022, Telink Semiconductor (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/hci.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/bluetooth/hci_driver.h>

#include <b9x_bt.h>

#define LOG_LEVEL CONFIG_BT_HCI_DRIVER_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_hci_driver_b9x);

#define HCI_CMD                 0x01
#define HCI_ACL                 0x02
#define HCI_EVT                 0x04

#define HCI_BT_B9X_TIMEOUT K_MSEC(2000)

static K_SEM_DEFINE(hci_send_sem, 1, 1);

static bool is_hci_event_discardable(const uint8_t *evt_data)
{
	uint8_t evt_type = evt_data[0];

	switch (evt_type) {
#if defined(CONFIG_BT_BREDR)
	case BT_HCI_EVT_INQUIRY_RESULT_WITH_RSSI:
	case BT_HCI_EVT_EXTENDED_INQUIRY_RESULT:
		return true;
#endif
	case BT_HCI_EVT_LE_META_EVENT: {
		uint8_t subevt_type = evt_data[sizeof(struct bt_hci_evt_hdr)];

		switch (subevt_type) {
		case BT_HCI_EVT_LE_ADVERTISING_REPORT:
			return true;
		default:
			return false;
		}
	}
	default:
		return false;
	}
}

static struct net_buf *bt_b9x_evt_recv(uint8_t *data, size_t len)
{
	bool discardable;
	struct bt_hci_evt_hdr hdr;
	struct net_buf *buf;
	size_t buf_tailroom;

	if (len < sizeof(hdr)) {
		LOG_ERR("Not enough data for event header");
		return NULL;
	}

	discardable = is_hci_event_discardable(data);

	memcpy((void *)&hdr, data, sizeof(hdr));
	data += sizeof(hdr);
	len -= sizeof(hdr);

	if (len != hdr.len) {
		LOG_ERR("Event payload length is not correct");
		return NULL;
	}
	LOG_DBG("len %u", hdr.len);

	buf = bt_buf_get_evt(hdr.evt, discardable, K_NO_WAIT);
	if (!buf) {
		if (discardable) {
			LOG_DBG("Discardable buffer pool full, ignoring event");
		} else {
			LOG_ERR("No available event buffers!");
		}
		return buf;
	}

	net_buf_add_mem(buf, &hdr, sizeof(hdr));

	buf_tailroom = net_buf_tailroom(buf);
	if (buf_tailroom < len) {
		LOG_ERR("Not enough space in buffer %zu/%zu", len, buf_tailroom);
		net_buf_unref(buf);
		return NULL;
	}

	net_buf_add_mem(buf, data, len);

	return buf;
}

static struct net_buf *bt_b9x_acl_recv(uint8_t *data, size_t len)
{
	struct bt_hci_acl_hdr hdr;
	struct net_buf *buf;
	size_t buf_tailroom;

	if (len < sizeof(hdr)) {
		LOG_ERR("Not enough data for ACL header");
		return NULL;
	}

	buf = bt_buf_get_rx(BT_BUF_ACL_IN, K_NO_WAIT);
	if (buf) {
		memcpy((void *)&hdr, data, sizeof(hdr));
		data += sizeof(hdr);
		len -= sizeof(hdr);
	} else {
		LOG_ERR("No available ACL buffers!");
		return NULL;
	}

	if (len != sys_le16_to_cpu(hdr.len)) {
		LOG_ERR("ACL payload length is not correct");
		net_buf_unref(buf);
		return NULL;
	}

	net_buf_add_mem(buf, &hdr, sizeof(hdr));
	buf_tailroom = net_buf_tailroom(buf);
	if (buf_tailroom < len) {
		LOG_ERR("Not enough space in buffer %zu/%zu", len, buf_tailroom);
		net_buf_unref(buf);
		return NULL;
	}

	LOG_DBG("len %u", len);
	net_buf_add_mem(buf, data, len);

	return buf;
}

static void hci_b9x_host_rcv_pkt(uint8_t *data, uint16_t len)
{
	uint8_t pkt_indicator;
	struct net_buf *buf;

	LOG_HEXDUMP_DBG(data, len, "host packet data:");

	pkt_indicator = *data++;
	len -= sizeof(pkt_indicator);

	switch (pkt_indicator) {
	case HCI_EVT:
		buf = bt_b9x_evt_recv(data, len);
		break;

	case HCI_ACL:
		buf = bt_b9x_acl_recv(data, len);
		break;

	default:
		buf = NULL;
		LOG_ERR("Unknown HCI type %u", pkt_indicator);
	}

	if (buf) {
		LOG_DBG("Calling bt_recv(%p)", buf);
		bt_recv(buf);
	}
}

static void hci_b9x_controller_rcv_pkt_ready(void)
{
	k_sem_give(&hci_send_sem);
}

static b9x_bt_host_callback_t vhci_host_cb = {
	.host_send_available = hci_b9x_controller_rcv_pkt_ready,
	.host_read_packet = hci_b9x_host_rcv_pkt
};

static int bt_b9x_send(struct net_buf *buf)
{
	int err = 0;
	uint8_t type;

	LOG_DBG("buf %p type %u len %u", buf, bt_buf_get_type(buf), buf->len);

	switch (bt_buf_get_type(buf)) {
	case BT_BUF_ACL_OUT:
		type = HCI_ACL;
		break;

	case BT_BUF_CMD:
		type = HCI_CMD;
		break;

	default:
		LOG_ERR("Unknown type %u", bt_buf_get_type(buf));
		goto done;
	}

	LOG_HEXDUMP_DBG(buf->data, buf->len, "Final HCI buffer:");

	if (k_sem_take(&hci_send_sem, HCI_BT_B9X_TIMEOUT) == 0) {
		b9x_bt_host_send_packet(type, buf->data, buf->len);
	} else {
		LOG_ERR("Send packet timeout error");
		err = -ETIMEDOUT;
	}

done:
	net_buf_unref(buf);
	k_sem_give(&hci_send_sem);

	return err;
}

static int hci_b9x_open(void)
{
	int status;

	status = b9x_bt_controller_init();
	if (status) {
		LOG_ERR("Bluetooth controller init failed %d", status);
		return status;
	}

	b9x_bt_host_callback_register(&vhci_host_cb);

#if CONFIG_SOC_RISCV_TELINK_B91
	LOG_DBG("B91 BT started");
#elif CONFIG_SOC_RISCV_TELINK_B92
	LOG_DBG("B92 BT started");
#endif

	return 0;
}

static int hci_b9x_close(void)
{
#if defined(CONFIG_BT_HCI_HOST) && defined(CONFIG_BT_BROADCASTER)
	bt_le_adv_stop();
#endif /* CONFIG_BT_HCI_HOST && CONFIG_BT_BROADCASTER */
	b9x_bt_controller_deinit();

	return 0;
}

static const struct bt_hci_driver drv = {
#if CONFIG_SOC_RISCV_TELINK_B91
	.name   = "BT B91",
#elif CONFIG_SOC_RISCV_TELINK_B92
	.name   = "BT B92",
#endif
	.open   = hci_b9x_open,
	.close	= hci_b9x_close,
	.send   = bt_b9x_send,
	.bus    = BT_HCI_DRIVER_BUS_IPM,
#if defined(CONFIG_BT_DRIVER_QUIRK_NO_AUTO_DLE)
	.quirks = BT_QUIRK_NO_AUTO_DLE,
#endif
};

static int bt_b9x_init(void)
{

	bt_hci_driver_register(&drv);

	return 0;
}

SYS_INIT(bt_b9x_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
