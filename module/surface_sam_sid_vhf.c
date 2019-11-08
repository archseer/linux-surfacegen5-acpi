/*
 * Virtual HID Framwork (VHF) driver for input events via SAM.
 * Used for keyboard input events on the Surface Laptops.
 */

#include <linux/acpi.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include "surface_sam_ssh.h"


#define USB_VENDOR_ID_MICROSOFT		0x045e
#define USB_DEVICE_ID_MS_VHF		0xf001

#define SID_VHF_INPUT_NAME		"Microsoft Virtual HID Framework Device"

/*
 * Request ID for VHF events. This value is based on the output of the Surface
 * EC and should not be changed.
 */
#define SAM_EVENT_SID_VHF_RQID		0x0015
#define SAM_EVENT_SID_VHF_TC		0x15

struct sid_vhf_evtctx {
	struct device     *dev;
	struct hid_device *hid;
};

struct sid_vhf_drvdata {
	struct sid_vhf_evtctx event_ctx;
};


static int sid_vhf_hid_start(struct hid_device *hid)
{
	hid_dbg(hid, "%s\n", __func__);
	return 0;
}

static void sid_vhf_hid_stop(struct hid_device *hid)
{
	hid_dbg(hid, "%s\n", __func__);
}

static int sid_vhf_hid_open(struct hid_device *hid)
{
	hid_dbg(hid, "%s\n", __func__);
	return 0;
}

static void sid_vhf_hid_close(struct hid_device *hid)
{
	hid_dbg(hid, "%s\n", __func__);
}

struct surface_sam_sid_vhf_meta_rqst {
	u8 id;
	u32 offset;
	u32 limit;
	u8 end; // 0x01 if end was reached
} __packed;

struct vhf_meta_info_resp {
	u8 _1;
	u8 _2;
	u8 _3;
	u8 _4;
	u8 _5;
	u8 _6;
	u8 _7;
	u16 len;
} __packed;

union vhf_buffer_data {
	struct vhf_meta_info_resp info;
	u8 pld[0x76];
};

struct surface_sam_sid_vhf_meta_resp {
	struct surface_sam_sid_vhf_meta_rqst rqst;
	union vhf_buffer_data data;
} __packed;


static int vhf_get_hid_descriptor(struct hid_device *hid, u8 **desc, int *size)
{
	int status;
	int len;
	u8 *buf;

	struct surface_sam_ssh_rqst rqst = {};
	struct surface_sam_sid_vhf_meta_resp resp = {};
	struct surface_sam_ssh_buf result = {};

	resp.rqst.id = 0;
	resp.rqst.offset = 0;
	resp.rqst.limit = 0x76;
	resp.rqst.end = 0;

	rqst.tc = 0x15;
	rqst.cid = 0x04;
	rqst.iid = 0x03;
	rqst.pri = 0x02;
	rqst.snc = 0x01;
	rqst.cdl = sizeof(struct surface_sam_sid_vhf_meta_rqst);
	rqst.pld = (u8*)&resp.rqst;

	result.cap  = sizeof(struct surface_sam_sid_vhf_meta_resp);
	result.len  = 0;
	result.data = (u8*)&resp;

	// first fetch 01 to get the total length
	status = surface_sam_ssh_rqst(&rqst, &result);
	if (status) {
		return status;
	}

	len = resp.data.info.len;

	// allocate a buffer for the descriptor
	buf = kzalloc(len * sizeof(u8), GFP_KERNEL);

	// then, iterate and write into buffer, copying out resp.rqst.len bytes
	// (it gets set to the length of data read)

	resp.rqst.id = 1;
	resp.rqst.offset = 0;
	resp.rqst.end = 0;

	while (resp.rqst.end != 0x01) {
		status = surface_sam_ssh_rqst(&rqst, &result);
		if (status) {
			kfree(buf);
			return status;
		}
		memcpy(buf + resp.rqst.offset, resp.data.pld, resp.rqst.limit);

		resp.rqst.offset += resp.rqst.limit;
	}

	*desc = buf;
	*size = len;

	return 0;
}

static int sid_vhf_hid_parse(struct hid_device *hid)
{

	int ret = 0, size;
	u8 *buf;

	ret = vhf_get_hid_descriptor(hid, &buf, &size);
	if (ret != 0) {
		hid_dbg(hid, "vhf_get_hid_descriptor ret %d\n", ret);
		return -EIO;
	}
	print_hex_dump_debug("descriptor:", DUMP_PREFIX_OFFSET, 16, 1, buf, size, false);

	ret = hid_parse_report(hid, buf, size);
	kfree(buf);
	return ret;

}

static int sid_vhf_hid_raw_request(struct hid_device *hid, unsigned char reportnum,
			       u8 *buf, size_t len, unsigned char rtype,
			       int reqtype)
{
	hid_dbg(hid, "%s: reportnum %#04x %i %i\n", __func__, reportnum, rtype, reqtype);
	print_hex_dump_debug("report:", DUMP_PREFIX_OFFSET, 16, 1, buf, len, false);

	int status;
	u8 iid = 0x02;
	u8 cid;

	// Byte 0 is the report number. Report data starts at byte 1.
	buf[0] = reportnum;

	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		cid = 0x02;
		break;
	case HID_REQ_SET_REPORT:
		cid = 0x03;
		break;
	default:
		hid_err(hid, "%s: unknown req type 0x%02x\n", __func__, rtype);
		return 0;		// discard everything
	}

	// HAXX: The keyboard and the touchpad should probably be separate drivers.
	// For the time being, re-route keyboard reports to the right address.
	if (reportnum == 1) {
		cid = 0x01;
		iid = 0x01;
	}

	struct surface_sam_ssh_rqst rqst = {
		.tc  = SAM_EVENT_SID_VHF_TC,
		.pri = SURFACE_SAM_PRIORITY_HIGH,
		.iid = iid,
		.cid = cid,
		.snc = HID_REQ_GET_REPORT == reqtype ? 0x01 : 0x00,
		.cdl = len,
		.pld = buf,
	};

	struct surface_sam_ssh_buf result = {
		.cap = len,
		.len = 0,
		.data = buf,
	};

	hid_dbg(hid, "%s: sending iid=%#04x cid=%#04x snc=%#04x\n", __func__, iid, cid, HID_REQ_GET_REPORT == reqtype);

	status = surface_sam_ssh_rqst(&rqst, &result);
	hid_dbg(hid, "%s: status %i\n", __func__, status);

	if (status) {
		return status;
	}

	if (result.len > 0) {
		print_hex_dump_debug("response:", DUMP_PREFIX_OFFSET, 16, 1, buf, result.len, false);
	}

	return result.len;
}

static struct hid_ll_driver sid_vhf_hid_ll_driver = {
	.start         = sid_vhf_hid_start,
	.stop          = sid_vhf_hid_stop,
	.open          = sid_vhf_hid_open,
	.close         = sid_vhf_hid_close,
	.parse         = sid_vhf_hid_parse,
	.raw_request   = sid_vhf_hid_raw_request,
};


static struct hid_device *sid_vhf_create_hid_device(struct platform_device *pdev)
{
	struct hid_device *hid;

	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		return hid;
	}

	hid->dev.parent = &pdev->dev;

	hid->bus     = BUS_VIRTUAL;
	hid->vendor  = USB_VENDOR_ID_MICROSOFT;
	hid->product = USB_DEVICE_ID_MS_VHF;

	hid->ll_driver = &sid_vhf_hid_ll_driver;

	sprintf(hid->name, "%s", SID_VHF_INPUT_NAME);

	return hid;
}

static int sid_vhf_event_handler(struct surface_sam_ssh_event *event, void *data)
{
	struct sid_vhf_evtctx *ctx = (struct sid_vhf_evtctx *)data;

	if (event->tc == SAM_EVENT_SID_VHF_TC && (event->cid == 0x00 || event->cid == 0x03 || event->cid == 0x04)) {
		return hid_input_report(ctx->hid, HID_INPUT_REPORT, event->pld, event->len, 1);
	}

	dev_warn(ctx->dev, "unsupported event (tc = %d, cid = %d)\n", event->tc, event->cid);
	return 0;
}

static int surface_sam_sid_vhf_probe(struct platform_device *pdev)
{
	struct sid_vhf_drvdata *drvdata;
	struct hid_device *hid;
	int status;

	// add device link to EC
	status = surface_sam_ssh_consumer_register(&pdev->dev);
	if (status) {
		return status == -ENXIO ? -EPROBE_DEFER : status;
	}

	drvdata = kzalloc(sizeof(struct sid_vhf_drvdata), GFP_KERNEL);
	if (!drvdata) {
		return -ENOMEM;
	}

	hid = sid_vhf_create_hid_device(pdev);
	if (IS_ERR(hid)) {
		status = PTR_ERR(hid);
		goto err_probe_hid;
	}

	status = hid_add_device(hid);
	if (status) {
		goto err_add_hid;
	}

	drvdata->event_ctx.dev = &pdev->dev;
	drvdata->event_ctx.hid = hid;

	platform_set_drvdata(pdev, drvdata);

	status = surface_sam_ssh_set_event_handler(
			SAM_EVENT_SID_VHF_RQID,
			sid_vhf_event_handler,
			&drvdata->event_ctx);
	if (status) {
		goto err_add_hid;
	}

	status = surface_sam_ssh_enable_event_source(SAM_EVENT_SID_VHF_TC, 0x01, SAM_EVENT_SID_VHF_RQID);
	if (status) {
		goto err_event_source;
	}

	return 0;

err_event_source:
	surface_sam_ssh_remove_event_handler(SAM_EVENT_SID_VHF_RQID);
err_add_hid:
	hid_destroy_device(hid);
	platform_set_drvdata(pdev, NULL);
err_probe_hid:
	kfree(drvdata);
	return status;
}

static int surface_sam_sid_vhf_remove(struct platform_device *pdev)
{
	struct sid_vhf_drvdata *drvdata = platform_get_drvdata(pdev);

	surface_sam_ssh_disable_event_source(SAM_EVENT_SID_VHF_TC, 0x01, SAM_EVENT_SID_VHF_RQID);
	surface_sam_ssh_remove_event_handler(SAM_EVENT_SID_VHF_RQID);

	hid_destroy_device(drvdata->event_ctx.hid);
	kfree(drvdata);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

struct platform_driver surface_sam_sid_vhf = {
	.probe = surface_sam_sid_vhf_probe,
	.remove = surface_sam_sid_vhf_remove,
	.driver = {
		.name = "surface_sam_sid_vhf",
	},
};
