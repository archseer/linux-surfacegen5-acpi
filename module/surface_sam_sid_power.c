#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/suspend.h>
#include <linux/types.h>

#include "surface_sam_ssh.h"

// TODO: use netlink to send an event when alarms or certain events are
// triggered


/*
 * Common Power-Subsystem Interface.
 */ 

struct sid_power_subsystem {
	struct mutex lock;
	// TODO
};

static struct sid_power_subsystem sid_power_subsystem = {
	.lock   = __MUTEX_INITIALIZER(sid_power_subsystem.lock),
	// TODO
};

// TODO: power-subsystem/event handler interface


/*
 * Battery Driver.
 */

#define SID_BATTERY_VALUE_UNKNOWN 0xFFFFFFFF

#define SID_BATTERY_DEVICE_NAME	"Battery"

/* Battery power unit: 0 means mW, 1 means mA */
#define SID_BATTERY_POWER_UNIT_MA	1

#define SID_BATTERY_STATE_DISCHARGING	0x1
#define SID_BATTERY_STATE_CHARGING	0x2
#define SID_BATTERY_STATE_CRITICAL	0x4

#define SAM_EVENT_DELAY_BAT_STATE	msecs_to_jiffies(5000)

#define SAM_RQST_BAT_TC			0x02
#define SAM_EVENT_BAT_RQID		0x0002

#define SAM_EVENT_BAT_CID_HWCHANGE	0x15
#define SAM_EVENT_BAT_CID_CHARGING	0x16
#define SAM_EVENT_BAT_CID_ADAPTER	0x17
#define SAM_EVENT_BAT_CID_STATE		0x4f

#define SAM_RQST_BAT_CID_STA		0x01
#define SAM_RQST_BAT_CID_BIX		0x02
#define SAM_RQST_BAT_CID_BST		0x03
#define SAM_RQST_BAT_CID_BTP		0x04

#define SAM_RQST_BAT_CID_PMAX		0x0b
#define SAM_RQST_BAT_CID_PSOC		0x0c
#define SAM_RQST_BAT_CID_PSRC		0x0d
#define SAM_RQST_BAT_CID_CHGI		0x0e
#define SAM_RQST_BAT_CID_ARTG		0x0f


/* Equivalent to data returned in ACPI _BIX method */
struct sid_bix {
	u8  revision;
	u32 power_unit;
	u32 design_cap;
	u32 last_full_charge_cap;
	u32 technology;
	u32 design_voltage;
	u32 design_cap_warn;
	u32 design_cap_low;
	u32 cycle_count;
	u32 measurement_accuracy;
	u32 max_sampling_time;
	u32 min_sampling_time;
	u32 max_avg_interval;
	u32 min_avg_interval;
	u32 bat_cap_granularity_1;
	u32 bat_cap_granularity_2;
	u8  model[21];
	u8  serial[11];
	u8  type[5];
	u8  oem_info[21];
} __packed;

/* Equivalent to data returned in ACPI _BST method */
struct sid_bst {
	u32 state;
	u32 present_rate;
	u32 remaining_cap;
	u32 present_voltage;
} __packed;


/* Get battery status (_STA) */
static int sam_psy_get_sta(u8 iid, u32 *sta)
{
	struct surface_sam_ssh_rqst rqst;
	struct surface_sam_ssh_buf result;

	rqst.tc  = SAM_RQST_BAT_TC;
	rqst.cid = SAM_RQST_BAT_CID_STA;
	rqst.iid = iid;
	rqst.pri = SURFACE_SAM_PRIORITY_NORMAL;
	rqst.snc = 0x01;
	rqst.cdl = 0x00;
	rqst.pld = NULL;

	result.cap = sizeof(u32);
	result.len = 0;
	result.data = (u8 *)sta;

	return surface_sam_ssh_rqst(&rqst, &result);
}

/* Get battery static information (_BIX) */
static int sam_psy_get_bix(u8 iid, struct sid_bix *bix)
{
	struct surface_sam_ssh_rqst rqst;
	struct surface_sam_ssh_buf result;

	rqst.tc  = SAM_RQST_BAT_TC;
	rqst.cid = SAM_RQST_BAT_CID_BIX;
	rqst.iid = iid;
	rqst.pri = SURFACE_SAM_PRIORITY_NORMAL;
	rqst.snc = 0x01;
	rqst.cdl = 0x00;
	rqst.pld = NULL;

	result.cap = sizeof(struct sid_bix);
	result.len = 0;
	result.data = (u8 *)bix;

	return surface_sam_ssh_rqst(&rqst, &result);
}

/* Get battery dynamic information (_BST) */
static int sam_psy_get_bst(u8 iid, struct sid_bst *bst)
{
	struct surface_sam_ssh_rqst rqst;
	struct surface_sam_ssh_buf result;

	rqst.tc  = SAM_RQST_BAT_TC;
	rqst.cid = SAM_RQST_BAT_CID_BST;
	rqst.iid = iid;
	rqst.pri = SURFACE_SAM_PRIORITY_NORMAL;
	rqst.snc = 0x01;
	rqst.cdl = 0x00;
	rqst.pld = NULL;

	result.cap = sizeof(struct sid_bst);
	result.len = 0;
	result.data = (u8 *)bst;

	return surface_sam_ssh_rqst(&rqst, &result);
}

/* Set battery trip point (_BTP) */
static int sam_psy_set_btp(u8 iid, u32 btp)
{
	struct surface_sam_ssh_rqst rqst;

	rqst.tc  = SAM_RQST_BAT_TC;
	rqst.cid = SAM_RQST_BAT_CID_BTP;
	rqst.iid = iid;
	rqst.pri = SURFACE_SAM_PRIORITY_NORMAL;
	rqst.snc = 0x00;
	rqst.cdl = sizeof(u32);
	rqst.pld = (u8 *)&btp;

	return surface_sam_ssh_rqst(&rqst, NULL);
}

/* Get maximum platform power for battery (DPTF PMAX) */
static int sam_psy_get_pmax(u8 iid, u32 *pmax)
{
	struct surface_sam_ssh_rqst rqst;
	struct surface_sam_ssh_buf result;

	rqst.tc  = SAM_RQST_BAT_TC;
	rqst.cid = SAM_RQST_BAT_CID_PMAX;
	rqst.iid = iid;
	rqst.pri = SURFACE_SAM_PRIORITY_NORMAL;
	rqst.snc = 0x01;
	rqst.cdl = 0x00;
	rqst.pld = NULL;

	result.cap = sizeof(u32);
	result.len = 0;
	result.data = (u8 *)pmax;

	return surface_sam_ssh_rqst(&rqst, &result);
}

/* Get platform power soruce for battery (DPTF PSRC) */
static int sam_psy_get_psrc(u8 iid, u32 *psrc)
{
	struct surface_sam_ssh_rqst rqst;
	struct surface_sam_ssh_buf result;

	rqst.tc  = SAM_RQST_BAT_TC;
	rqst.cid = SAM_RQST_BAT_CID_PSRC;
	rqst.iid = iid;
	rqst.pri = SURFACE_SAM_PRIORITY_NORMAL;
	rqst.snc = 0x01;
	rqst.cdl = 0x00;
	rqst.pld = NULL;

	result.cap = sizeof(u32);
	result.len = 0;
	result.data = (u8 *)psrc;

	return surface_sam_ssh_rqst(&rqst, &result);
}

/* Get adapter rating (DPTF ARTG) */
static int sam_psy_get_artg(u8 iid, u32 *artg)
{
	struct surface_sam_ssh_rqst rqst;
	struct surface_sam_ssh_buf result;

	rqst.tc  = SAM_RQST_BAT_TC;
	rqst.cid = SAM_RQST_BAT_CID_ARTG;
	rqst.iid = iid;
	rqst.pri = SURFACE_SAM_PRIORITY_NORMAL;
	rqst.snc = 0x01;
	rqst.cdl = 0x00;
	rqst.pld = NULL;

	result.cap = sizeof(u32);
	result.len = 0;
	result.data = (u8 *)artg;

	return surface_sam_ssh_rqst(&rqst, &result);
}


/* Unknown (DPTF PSOC) */
static int sam_psy_get_psoc(u8 iid, u32 *psoc)
{
	struct surface_sam_ssh_rqst rqst;
	struct surface_sam_ssh_buf result;

	rqst.tc  = SAM_RQST_BAT_TC;
	rqst.cid = SAM_RQST_BAT_CID_PSOC;
	rqst.iid = iid;
	rqst.pri = SURFACE_SAM_PRIORITY_NORMAL;
	rqst.snc = 0x01;
	rqst.cdl = 0x00;
	rqst.pld = NULL;

	result.cap = sizeof(u32);
	result.len = 0;
	result.data = (u8 *)psoc;

	return surface_sam_ssh_rqst(&rqst, &result);
}

/* Unknown (DPTF CHGI/ INT3403 SPPC) */
static int sam_psy_set_chgi(u8 iid, u32 chgi)
{
	struct surface_sam_ssh_rqst rqst;

	rqst.tc  = SAM_RQST_BAT_TC;
	rqst.cid = SAM_RQST_BAT_CID_CHGI;
	rqst.iid = iid;
	rqst.pri = SURFACE_SAM_PRIORITY_NORMAL;
	rqst.snc = 0x00;
	rqst.cdl = sizeof(u32);
	rqst.pld = (u8 *)&chgi;

	return surface_sam_ssh_rqst(&rqst, NULL);
}


// To be removed...
static int test(u8 iid)
{
	struct sid_bix bix;
	struct sid_bst bst;
	u32 sta;
	int status;
	int percentage;

	status = sam_psy_get_sta(iid, &sta);
	if (status < 0) {
		printk(KERN_WARNING "sid_psy: sam_psy_get_sta failed with %d\n", status);
		return status;
	}
	printk(KERN_WARNING "sid_psy: sam_psy_get_sta returned 0x%x\n", status);

	status = sam_psy_get_bix(iid, &bix);
	if (status < 0) {
		printk(KERN_WARNING "sid_psy: sam_psy_get_bix failed with %d\n", status);
		return status;
	}

	status = sam_psy_get_bst(iid, &bst);
	if (status < 0) {
		printk(KERN_WARNING "sid_psy: sam_psy_get_bst failed with %d\n", status);
		return status;
	}

	printk(KERN_WARNING "sid_psy[%d]: bix: model: %s\n", iid, bix.model);
	printk(KERN_WARNING "sid_psy[%d]: bix: serial: %s\n", iid, bix.serial);
	printk(KERN_WARNING "sid_psy[%d]: bix: type: %s\n", iid, bix.type);
	printk(KERN_WARNING "sid_psy[%d]: bix: oem_info: %s\n", iid, bix.oem_info);

	printk(KERN_WARNING "sid_psy[%d]: bix: last_full_charge_cap: %d\n", iid, bix.last_full_charge_cap);
	printk(KERN_WARNING "sid_psy[%d]: bix: remaining_cap: %d\n", iid, bst.remaining_cap);

	percentage = (100 * bst.remaining_cap) / bix.last_full_charge_cap;
	printk(KERN_WARNING "sid_psy[%d]: remaining capacity: %d%%\n", iid, percentage);

	return 0;
}

/*
 * [  190.933072] sid_psy[1]: bix: model: M1087273
[  190.933072] sid_psy[1]: bix: serial: 4049103934
[  190.933073] sid_psy[1]: bix: type: LION
[  190.933073] sid_psy[1]: bix: oem_info: SMP
[  190.933074] sid_psy[1]: bix: last_full_charge_cap: 47510
[  190.933074] sid_psy[1]: bix: remaining_cap: 47510
[  190.933075] sid_psy[1]: remaining capacity: 100%
 */

static bool battery_driver_registered;
static unsigned int cache_time = 1000;
module_param(cache_time, uint, 0644);
MODULE_PARM_DESC(cache_time, "cache time in milliseconds");

struct sid_battery {
	struct mutex sysfs_lock;
	struct power_supply *bat;
	struct power_supply_desc bat_desc;
	struct platform_device *device;
	struct notifier_block pm_nb;
	unsigned long update_time;
	int revision;
	int rate_now;
	int capacity_now;
	int voltage_now;
	int design_capacity;
	int full_charge_capacity;
	int technology;
	int design_voltage;
	int design_capacity_warning;
	int design_capacity_low;
	int cycle_count;
	int measurement_accuracy;
	int max_sampling_time;
	int min_sampling_time;
	int max_averaging_interval;
	int min_averaging_interval;
	int capacity_granularity_1;
	int capacity_granularity_2;
	int alarm;
	char model_number[32];
	char serial_number[32];
	char type[32];
	char oem_info[32];
	int state;
	int power_unit;
	unsigned long flags;
};

#define to_sid_battery(x) power_supply_get_drvdata(x)

// TODO: pass this through so we can have BAT0/BAT1
#define psy_device_bid(device) "BAT0"


static inline int sid_battery_present(struct sid_battery *battery)
{
	return battery->device->status.battery_present;
}


static int sid_battery_technology(struct sid_battery *battery)
{
	if (!strcasecmp("NiCd", battery->type))
		return POWER_SUPPLY_TECHNOLOGY_NiCd;
	if (!strcasecmp("NiMH", battery->type))
		return POWER_SUPPLY_TECHNOLOGY_NiMH;
	if (!strcasecmp("LION", battery->type))
		return POWER_SUPPLY_TECHNOLOGY_LION;
	if (!strncasecmp("LI-ION", battery->type, 6))
		return POWER_SUPPLY_TECHNOLOGY_LION;
	if (!strcasecmp("LiP", battery->type))
		return POWER_SUPPLY_TECHNOLOGY_LIPO;
	return POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
}

static int sid_battery_get_state(struct sid_battery *battery);

static int sid_battery_is_charged(struct sid_battery *battery)
{
	/* charging, discharging or critical low */
	if (battery->state != 0)
		return 0;

	/* battery not reporting charge */
	if (battery->capacity_now == SID_BATTERY_VALUE_UNKNOWN ||
	    battery->capacity_now == 0)
		return 0;

	/* good batteries update full_charge as the batteries degrade */
	if (battery->full_charge_capacity == battery->capacity_now)
		return 1;

	/* fallback to using design values for broken batteries */
	if (battery->design_capacity == battery->capacity_now)
		return 1;

	/* we don't do any sort of metric based on percentages */
	return 0;
}

static bool sid_battery_is_degraded(struct sid_battery *battery)
{
	return battery->full_charge_capacity && battery->design_capacity &&
		battery->full_charge_capacity < battery->design_capacity;
}

static int sid_battery_handle_discharging(struct sid_battery *battery)
{
	/*
	 * Some devices wrongly report discharging if the battery's charge level
	 * was above the device's start charging threshold atm the AC adapter
	 * was plugged in and the device thus did not start a new charge cycle.
	 */
	if (power_supply_is_system_supplied() &&
	    battery->rate_now == 0)
		return POWER_SUPPLY_STATUS_NOT_CHARGING;

	return POWER_SUPPLY_STATUS_DISCHARGING;
}

static int sid_battery_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	int ret = 0;
	struct sid_battery *battery = to_sid_battery(psy);

	if (sid_battery_present(battery)) {
		/* run battery update only if it is present */
		sid_battery_get_state(battery);
	} else if (psp != POWER_SUPPLY_PROP_PRESENT)
		return -ENODEV;
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (battery->state & SID_BATTERY_STATE_DISCHARGING)
			val->intval = sid_battery_handle_discharging(battery);
		else if (battery->state & SID_BATTERY_STATE_CHARGING)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (sid_battery_is_charged(battery))
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = sid_battery_present(battery);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = sid_battery_technology(battery);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = battery->cycle_count;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		if (battery->design_voltage == SID_BATTERY_VALUE_UNKNOWN)
			ret = -ENODEV;
		else
			val->intval = battery->design_voltage * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (battery->voltage_now == SID_BATTERY_VALUE_UNKNOWN)
			ret = -ENODEV;
		else
			val->intval = battery->voltage_now * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_POWER_NOW:
		if (battery->rate_now == SID_BATTERY_VALUE_UNKNOWN)
			ret = -ENODEV;
		else
			val->intval = battery->rate_now * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		if (battery->design_capacity == SID_BATTERY_VALUE_UNKNOWN)
			ret = -ENODEV;
		else
			val->intval = battery->design_capacity * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
		if (battery->full_charge_capacity == SID_BATTERY_VALUE_UNKNOWN)
			ret = -ENODEV;
		else
			val->intval = battery->full_charge_capacity * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		if (battery->capacity_now == SID_BATTERY_VALUE_UNKNOWN)
			ret = -ENODEV;
		else
			val->intval = battery->capacity_now * 1000;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (battery->capacity_now && battery->full_charge_capacity)
			val->intval = battery->capacity_now * 100/
					battery->full_charge_capacity;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		if (battery->state & SID_BATTERY_STATE_CRITICAL)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else if (battery->capacity_now <= battery->alarm)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else if (sid_battery_is_charged(battery))
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = battery->model_number;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = battery->oem_info;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = battery->serial_number;
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static enum power_supply_property charge_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

static enum power_supply_property energy_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

/* --------------------------------------------------------------------------
                               Battery Management
   -------------------------------------------------------------------------- */

/* static const struct acpi_offsets state_offsets[] = { */
/* 	{offsetof(struct sid_battery, state), 0}, */
/* 	{offsetof(struct sid_battery, rate_now), 0}, */
/* 	{offsetof(struct sid_battery, capacity_now), 0}, */
/* 	{offsetof(struct sid_battery, voltage_now), 0}, */
/* }; */

/* static const struct acpi_offsets extended_info_offsets[] = { */
/* 	{offsetof(struct sid_battery, revision), 0}, */
/* 	{offsetof(struct sid_battery, power_unit), 0}, */
/* 	{offsetof(struct sid_battery, design_capacity), 0}, */
/* 	{offsetof(struct sid_battery, full_charge_capacity), 0}, */
/* 	{offsetof(struct sid_battery, technology), 0}, */
/* 	{offsetof(struct sid_battery, design_voltage), 0}, */
/* 	{offsetof(struct sid_battery, design_capacity_warning), 0}, */
/* 	{offsetof(struct sid_battery, design_capacity_low), 0}, */
/* 	{offsetof(struct sid_battery, cycle_count), 0}, */
/* 	{offsetof(struct sid_battery, measurement_accuracy), 0}, */
/* 	{offsetof(struct sid_battery, max_sampling_time), 0}, */
/* 	{offsetof(struct sid_battery, min_sampling_time), 0}, */
/* 	{offsetof(struct sid_battery, max_averaging_interval), 0}, */
/* 	{offsetof(struct sid_battery, min_averaging_interval), 0}, */
/* 	{offsetof(struct sid_battery, capacity_granularity_1), 0}, */
/* 	{offsetof(struct sid_battery, capacity_granularity_2), 0}, */
/* 	{offsetof(struct sid_battery, model_number), 1}, */
/* 	{offsetof(struct sid_battery, serial_number), 1}, */
/* 	{offsetof(struct sid_battery, type), 1}, */
/* 	{offsetof(struct sid_battery, oem_info), 1}, */
/* }; */

static int sid_battery_get_status(struct sid_battery *battery)
{
	u32 status;

	if (sam_psy_get_sta(battery->device->id, &status)) {
		dev_err(&battery->device->dev, "Error evaluating _STA");
		return -ENODEV;
	}

	return status;
}

static int sid_battery_get_info(struct sid_battery *battery)
{
	int status = 0;
	struct sid_bix bix;

	if (!sid_battery_present(battery))
		return 0;

	status = sam_psy_get_bix(battery->device->id, &bix);

	if (status) {
		dev_err(&battery->device->dev, "Error evaluating _BIX");
		return -ENODEV;
	}

	// extract information
	battery->revision                = bix.revision;
	battery->power_unit              = bix.power_unit;
	battery->design_capacity         = bix.design_cap;
	battery->full_charge_capacity    = bix.last_full_charge_cap;
	battery->technology              = bix.technology;
	battery->design_voltage          = bix.design_voltage;
	battery->design_capacity_warning = bix.design_cap_warn;
	battery->design_capacity_low     = bix.design_cap_low;
	battery->cycle_count             = bix.cycle_count;
	battery->measurement_accuracy    = bix.measurement_accuracy;
	battery->max_sampling_time       = bix.max_sampling_time;
	battery->min_sampling_time       = bix.min_sampling_time;
	battery->max_averaging_interval  = bix.max_avg_interval;
	battery->min_averaging_interval  = bix.min_avg_interval;
	battery->capacity_granularity_1  = bix.bat_cap_granularity_1;
	battery->capacity_granularity_2  = bix.bat_cap_granularity_2;
	battery->model_number            = bix.model;
	battery->serial_number           = bix.serial;
	battery->type                    = bix.type;
	battery->oem_info                = bix.oem_info;

	return status;
}

static int sid_battery_get_state(struct sid_battery *battery)
{
	int status = 0;
	struct sid_bst bst;

	if (!sid_battery_present(battery))
		return 0;

	if (battery->update_time &&
	    time_before(jiffies, battery->update_time +
			msecs_to_jiffies(cache_time)))
		return 0;

	status = sam_psy_get_bst(battery->device->id, &bst);

	if (status) {
		dev_err(&battery->device->dev, "Error evaluating _BST");
		return -ENODEV;
	}

	// extract information
	battery->state        = bst.state;
	battery->rate_now     = bst.present_rate;
	battery->capacity_now = bst.remaining_cap;
	battery->voltage_now  = bst.present_voltage;
	
	battery->update_time = jiffies;

	return status;
}

static int sid_battery_set_alarm(struct sid_battery *battery)
{
	int status = 0;

	if (!sid_battery_present(battery))
		return -ENODEV;

	status = sam_psy_set_btp(battery->device->id, battery->alarm);

	if (status) {
		dev_err(&battery->device->dev, "Error evaluating _BTP");
		return -ENODEV;
	}

	dev_dbg(&battery->device->dev, "Alarm set to %d", battery->alarm);
	return 0;
}

static int sid_battery_init_alarm(struct sid_battery *battery)
{
	/* Set default alarm */
	if (!battery->alarm)
		battery->alarm = battery->design_capacity_warning;
	return sid_battery_set_alarm(battery);
}

static ssize_t sid_battery_alarm_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct sid_battery *battery = to_sid_battery(dev_get_drvdata(dev));
	return sprintf(buf, "%d\n", battery->alarm * 1000);
}

static ssize_t sid_battery_alarm_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned long x;
	struct sid_battery *battery = to_sid_battery(dev_get_drvdata(dev));
	if (sscanf(buf, "%lu\n", &x) == 1)
		battery->alarm = x/1000;
	if (sid_battery_present(battery))
		sid_battery_set_alarm(battery);
	return count;
}

static const struct device_attribute alarm_attr = {
	.attr = {.name = "alarm", .mode = 0644},
	.show = sid_battery_alarm_show,
	.store = sid_battery_alarm_store,
};

static int sysfs_add_battery(struct sid_battery *battery)
{
	struct power_supply_config psy_cfg = { .drv_data = battery, };

	if (battery->power_unit == SID_BATTERY_POWER_UNIT_MA) {
		battery->bat_desc.properties = charge_battery_props;
		battery->bat_desc.num_properties =
			ARRAY_SIZE(charge_battery_props);
	} else {
		battery->bat_desc.properties = energy_battery_props;
		battery->bat_desc.num_properties =
			ARRAY_SIZE(energy_battery_props);
	}

	battery->bat_desc.name = psy_device_bid(battery->device);
	battery->bat_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	battery->bat_desc.get_property = sid_battery_get_property;

	battery->bat = power_supply_register_no_ws(&battery->device->dev,
				&battery->bat_desc, &psy_cfg);

	if (IS_ERR(battery->bat)) {
		int result = PTR_ERR(battery->bat);

		battery->bat = NULL;
		return result;
	}
	return device_create_file(&battery->bat->dev, &alarm_attr);
}

static void sysfs_remove_battery(struct sid_battery *battery)
{
	mutex_lock(&battery->sysfs_lock);
	if (!battery->bat) {
		mutex_unlock(&battery->sysfs_lock);
		return;
	}
	device_remove_file(&battery->bat->dev, &alarm_attr);
	power_supply_unregister(battery->bat);
	battery->bat = NULL;
	mutex_unlock(&battery->sysfs_lock);
}

static int sid_battery_update(struct sid_battery *battery, bool resume)
{
	int result = sid_battery_get_status(battery);

	if (result)
		return result;

	if (!acpi_battery_present(battery)) {
		sysfs_remove_battery(battery);
		battery->update_time = 0;
		return 0;
	}

	if (resume)
		return 0;

	if (!battery->update_time) {
		result = sid_battery_get_info(battery);
		if (result)
			return result;
		sid_battery_init_alarm(battery);
	}

	result = sid_battery_get_state(battery);
	if (result)
		return result;

	if (!battery->bat) {
		result = sysfs_add_battery(battery);
		if (result)
			return result;
	}

	/*
	 * Wakeup the system if battery is critical low
	 * or lower than the alarm level
	 */
	/* if ((battery->state & SID_BATTERY_STATE_CRITICAL) || */
            /* (battery->capacity_now <= battery->alarm)) */
	/* 	acpi_pm_wakeup_event(&battery->device->dev); */

	return result;
}

static void sid_battery_refresh(struct sid_battery *battery)
{
	int power_unit;

	if (!battery->bat)
		return;

	power_unit = battery->power_unit;

	sid_battery_get_info(battery);

	if (power_unit == battery->power_unit)
		return;

	/* The battery has changed its reporting units. */
	sysfs_remove_battery(battery);
	sysfs_add_battery(battery);
}

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static unsigned long psy_evt_power_delay(struct surface_sam_ssh_event *event, void *data)
{
	switch (event->cid) {
	case SAM_EVENT_BAT_CID_CHARGING:
	case SAM_EVENT_BAT_CID_STATE:
		return SAM_EVENT_DELAY_BAT_STATE;

	case SAM_EVENT_BAT_CID_ADAPTER:
	case SAM_EVENT_BAT_CID_HWCHANGE:
	default:
		return 0;
	}
}

static int psy_evt_power(struct surface_sam_ssh_event *event, void *data)
{
	struct device *dev = (struct device *)data;

	switch (event->cid) {
	case SAM_EVENT_BAT_CID_HWCHANGE:
		enum san_pwr_event evcode;
		int status;

		if (event->iid == 0x02) {
			evcode = SAN_BAT_EVENT_BAT2_INFO;
		} else {
			evcode = SAN_BAT_EVENT_BAT1_INFO;
		}

		status = san_acpi_notify_power_event(dev, evcode);
		if (status) {
			dev_err(&dev, "error handling power event (cid = %x)\n", event->cid);
			return status;
		}

	case SAM_EVENT_BAT_CID_ADAPTER:
		int status;

		status = san_acpi_notify_power_event(dev, SAN_BAT_EVENT_ADP1_STAT);
		if (status) {
			dev_err(&dev, "error handling power event (cid = %x)\n", event->cid);
			return status;
		}

	case SAM_EVENT_BAT_CID_CHARGING:
	case SAM_EVENT_BAT_CID_STATE:
		int status;

		status = san_acpi_notify_power_event(dev, SAN_BAT_EVENT_BAT1_STAT);
		if (status) {
			dev_err(&dev, "error handling power event (cid = %x)\n", event->cid);
			return status;
		}

		status = san_acpi_notify_power_event(dev, SAN_BAT_EVENT_BAT2_STAT);
		if (status) {
			dev_err(dev, "error handling power event (cid = %x)\n", event->cid);
			return status;
		}

	default:
		dev_warn(dev, "unhandled power event (cid = %x)\n", event->cid);
	}

	return 0;
}

static void sid_battery_notify(struct platform_device *device, u32 event)
{
	struct sid_battery *battery = platform_driver_data(device);
	struct power_supply *old;

	if (!battery)
		return;
	old = battery->bat;

	if (event == SID_BATTERY_NOTIFY_INFO)
		sid_battery_refresh(battery);
	sid_battery_update(battery, false);
	/* acpi_bus_generate_netlink_event(device->pnp.device_class, */
	/* 				dev_name(&device->dev), event, */
	/* 				spi_battery_present(battery)); */
	/* sid_battery_update could remove power_supply object */
	if (old && battery->bat)
		power_supply_changed(battery->bat);
}

static int battery_notify(struct notifier_block *nb,
			       unsigned long mode, void *_unused)
{
	struct sid_battery *battery = container_of(nb, struct sid_battery,
						    pm_nb);
	int result;

	switch (mode) {
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		if (!sid_battery_present(battery))
			return 0;

		if (battery->bat) {
			sid_battery_refresh(battery);
		} else {
			result = sid_battery_get_info(battery);
			if (result)
				return result;

			result = sysfs_add_battery(battery);
			if (result)
				return result;
		}

		sid_battery_init_alarm(battery);
		sid_battery_get_state(battery);
		break;
	}

	return 0;
}

static int psy_enable_events(struct device *dev)
{
	int status;

	status = surface_sam_ssh_set_delayed_event_handler(
			SAM_EVENT_BAT_RQID, psy_evt_power,
			psy_evt_power_delay, dev);
	if (status) {
		goto err_handler_power;
	}

	status = surface_sam_ssh_enable_event_source(SAM_EVENT_BAT_TC, 0x01, SAM_EVENT_BAT_RQID);
	if (status) {
		goto err_source_power;
	}

	return 0;

err_source_power:
	surface_sam_ssh_remove_event_handler(SAM_EVENT_BAT_RQID);
err_handler_power:
	return status;
}

static void psy_disable_events(void)
{
	surface_sam_ssh_disable_event_source(SAM_EVENT_BAT_TC, 0x01, SAM_EVENT_BAT_RQID);
	surface_sam_ssh_remove_event_handler(SAM_EVENT_BAT_RQID);
}

static int surface_sam_sid_battery_probe(struct platform_device *device)
{
	int result = 0;
	struct sid_battery *battery = NULL;

	if (!device)
		return -EINVAL;

	battery = kzalloc(sizeof(struct sid_battery), GFP_KERNEL);
	if (!battery)
		return -ENOMEM;
	battery->device = device;
	strcpy(device->name, SID_BATTERY_DEVICE_NAME);
	device->driver_data = battery;
	mutex_init(&battery->sysfs_lock);

	// link to ec
	result = surface_sam_ssh_consumer_register(&pdev->dev);
	if (result) {
		goto fail;
	}
        // TODO: subscribe to events

	result = sid_battery_update(battery, false);
	if (result)
		goto fail;

	dev_info(&pdev->dev, "%s Slot [%s] (battery %s)",
		SID_BATTERY_DEVICE_NAME, psy_device_bid(device),
		device->status.battery_present ? "present" : "absent");

	battery->pm_nb.notifier_call = battery_notify;
	register_pm_notifier(&battery->pm_nb);

	device_init_wakeup(&device->dev, 1);

	return result;

fail:
        // TODO: handler deregistering
	sysfs_remove_battery(battery);
	mutex_destroy(&battery->sysfs_lock);
	kfree(battery);
	return result;
}

static int surface_sam_sid_battery_remove(struct platform_device *device)
{
	struct sid_battery *battery = NULL;

	if (!device || !platform_driver_data(device))
		return -EINVAL;
	device_init_wakeup(&device->dev, 0);
	battery = platform_driver_data(device);
	psy_disable_events();
	unregister_pm_notifier(&battery->pm_nb);
	sysfs_remove_battery(battery);
	mutex_destroy(&battery->sysfs_lock);
	kfree(battery);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
/* this is needed to learn about changes made in suspended state */
static int sid_battery_resume(struct device *dev)
{
	struct sid_battery *battery;

	if (!dev)
		return -EINVAL;

	battery = platform_driver_data(to_platform_device(dev));
	if (!battery)
		return -EINVAL;

	battery->update_time = 0;
	sid_battery_update(battery, true);
	return 0;
}
#else
#define sid_battery_resume NULL
#endif

static SIMPLE_DEV_PM_OPS(sid_battery_pm, NULL, sid_battery_resume);

static struct platform_driver sid_battery_driver = {
	.probe = surface_sam_sid_battery_probe,
	.remove = surface_sam_sid_battery_remove,
	.driver = {
		.name = "surface_sam_sid_battery",
		.pm = &sid_battery_pm,
	},
};

/*
 * AC Driver.
 */

static int surface_sam_sid_ac_probe(struct platform_device *pdev)
{
	int status;

	// link to ec
	status = surface_sam_ssh_consumer_register(&pdev->dev);
	if (status) {
		return status == -ENXIO ? -EPROBE_DEFER : status;
	}

	return 0;	// TODO
}

static int surface_sam_sid_ac_remove(struct platform_device *pdev)
{
	return 0;	// TODO
}

struct platform_driver surface_sam_sid_ac = {
	.probe = surface_sam_sid_ac_probe,
	.remove = surface_sam_sid_ac_remove,
	.driver = {
		.name = "surface_sam_sid_ac",
	},
};

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_AUTHOR("Blaž Hrastnik <blaz@mxxn.io>");
MODULE_DESCRIPTION("Surface Gen7 Battery Driver");
MODULE_LICENSE("GPL");
