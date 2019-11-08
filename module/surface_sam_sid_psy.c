/*
 * Surface Battery power supply driver.
 * Translates the EC communication to a battery frontend.
 */

// TODO: AC device

/*
 * TODO:
 * - Make a battery device
 * - Subscribe it to the relevant events 
 * - Keep an updated state in memory
 * - power supply alarm / ALERT
 */

#include <linux/platform_device.h>
#include <linux/power_supply.h>

#include "surface_sam_ssh.h"

#include <linux/async.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/types.h>

#include <asm/unaligned.h>

#include <linux/acpi.h>
#include <linux/power_supply.h>

#include <acpi/battery.h>

#define PREFIX "ACPI: "

#define PSY_BATTERY_VALUE_UNKNOWN 0xFFFFFFFF

#define PSY_BATTERY_DEVICE_NAME	"Battery"

/* Battery power unit: 0 means mW, 1 means mA */
#define PSY_BATTERY_POWER_UNIT_MA	1

#define PSY_BATTERY_STATE_DISCHARGING	0x1
#define PSY_BATTERY_STATE_CHARGING	0x2
#define PSY_BATTERY_STATE_CRITICAL	0x4

#define _COMPONENT		PSY_BATTERY_COMPONENT

MODULE_NAME("surface_sam_psy");

MODULE_AUTHOR("Blaž Hrastnik <blaz@mxxn.io>");
MODULE_DESCRIPTION("Surface Gen7 Battery Driver");
MODULE_LICENSE("GPL");

static async_cookie_t async_cookie;
static bool battery_driver_registered;
static unsigned int cache_time = 1000;
module_param(cache_time, uint, 0644);
MODULE_PARM_DESC(cache_time, "cache time in milliseconds");

static const struct acpi_device_id battery_device_ids[] = {
	{"PNP0C0A", 0},
	{"", 0},
};

MODULE_DEVICE_TABLE(acpi, battery_device_ids);

enum {
	PSY_BATTERY_QUIRK_PERCENTAGE_CAPACITY,
	/* for batteries reporting current capacity with design capacity
	 * on a full charge, but showing degradation in full charge cap.
	 */
	PSY_BATTERY_QUIRK_DEGRADED_FULL_CHARGE,
};

struct psy_battery {
	struct mutex lock;
	struct mutex sysfs_lock;
	struct power_supply *bat;
	struct power_supply_desc bat_desc;
	struct acpi_device *device;
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

#define to_psy_battery(x) power_supply_get_drvdata(x)

static inline int psy_battery_present(struct psy_battery *battery)
{
	return battery->device->status.battery_present;
}

static int psy_battery_technology(struct psy_battery *battery)
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

static int psy_battery_get_state(struct psy_battery *battery);

static int psy_battery_is_charged(struct psy_battery *battery)
{
	/* charging, discharging or critical low */
	if (battery->state != 0)
		return 0;

	/* battery not reporting charge */
	if (battery->capacity_now == PSY_BATTERY_VALUE_UNKNOWN ||
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

static bool psy_battery_is_degraded(struct psy_battery *battery)
{
	return battery->full_charge_capacity && battery->design_capacity &&
		battery->full_charge_capacity < battery->design_capacity;
}

static int psy_battery_handle_discharging(struct psy_battery *battery)
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

static int psy_battery_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	int ret = 0;
	struct psy_battery *battery = to_psy_battery(psy);

	if (psy_battery_present(battery)) {
		/* run battery update only if it is present */
		psy_battery_get_state(battery);
	} else if (psp != POWER_SUPPLY_PROP_PRESENT)
		return -ENODEV;
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (battery->state & PSY_BATTERY_STATE_DISCHARGING)
			val->intval = psy_battery_handle_discharging(battery);
		else if (battery->state & PSY_BATTERY_STATE_CHARGING)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (psy_battery_is_charged(battery))
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = psy_battery_present(battery);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = psy_battery_technology(battery);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = battery->cycle_count;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		if (battery->design_voltage == PSY_BATTERY_VALUE_UNKNOWN)
			ret = -ENODEV;
		else
			val->intval = battery->design_voltage * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (battery->voltage_now == PSY_BATTERY_VALUE_UNKNOWN)
			ret = -ENODEV;
		else
			val->intval = battery->voltage_now * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_POWER_NOW:
		if (battery->rate_now == PSY_BATTERY_VALUE_UNKNOWN)
			ret = -ENODEV;
		else
			val->intval = battery->rate_now * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		if (battery->design_capacity == PSY_BATTERY_VALUE_UNKNOWN)
			ret = -ENODEV;
		else
			val->intval = battery->design_capacity * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
		if (battery->full_charge_capacity == PSY_BATTERY_VALUE_UNKNOWN)
			ret = -ENODEV;
		else
			val->intval = battery->full_charge_capacity * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		if (battery->capacity_now == PSY_BATTERY_VALUE_UNKNOWN)
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
		if (battery->state & PSY_BATTERY_STATE_CRITICAL)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else if (battery->capacity_now <= battery->alarm)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else if (psy_battery_is_charged(battery))
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

static enum power_supply_property energy_battery_full_cap_broken_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

/* --------------------------------------------------------------------------
                               Battery Management
   -------------------------------------------------------------------------- */
struct acpi_offsets {
	size_t offset;		/* offset inside struct psy_battery */
	u8 mode;		/* int or string? */
};

static const struct acpi_offsets state_offsets[] = {
	{offsetof(struct psy_battery, state), 0},
	{offsetof(struct psy_battery, rate_now), 0},
	{offsetof(struct psy_battery, capacity_now), 0},
	{offsetof(struct psy_battery, voltage_now), 0},
};

static const struct acpi_offsets info_offsets[] = {
	{offsetof(struct psy_battery, power_unit), 0},
	{offsetof(struct psy_battery, design_capacity), 0},
	{offsetof(struct psy_battery, full_charge_capacity), 0},
	{offsetof(struct psy_battery, technology), 0},
	{offsetof(struct psy_battery, design_voltage), 0},
	{offsetof(struct psy_battery, design_capacity_warning), 0},
	{offsetof(struct psy_battery, design_capacity_low), 0},
	{offsetof(struct psy_battery, capacity_granularity_1), 0},
	{offsetof(struct psy_battery, capacity_granularity_2), 0},
	{offsetof(struct psy_battery, model_number), 1},
	{offsetof(struct psy_battery, serial_number), 1},
	{offsetof(struct psy_battery, type), 1},
	{offsetof(struct psy_battery, oem_info), 1},
};

static const struct acpi_offsets extended_info_offsets[] = {
	{offsetof(struct psy_battery, revision), 0},
	{offsetof(struct psy_battery, power_unit), 0},
	{offsetof(struct psy_battery, design_capacity), 0},
	{offsetof(struct psy_battery, full_charge_capacity), 0},
	{offsetof(struct psy_battery, technology), 0},
	{offsetof(struct psy_battery, design_voltage), 0},
	{offsetof(struct psy_battery, design_capacity_warning), 0},
	{offsetof(struct psy_battery, design_capacity_low), 0},
	{offsetof(struct psy_battery, cycle_count), 0},
	{offsetof(struct psy_battery, measurement_accuracy), 0},
	{offsetof(struct psy_battery, max_sampling_time), 0},
	{offsetof(struct psy_battery, min_sampling_time), 0},
	{offsetof(struct psy_battery, max_averaging_interval), 0},
	{offsetof(struct psy_battery, min_averaging_interval), 0},
	{offsetof(struct psy_battery, capacity_granularity_1), 0},
	{offsetof(struct psy_battery, capacity_granularity_2), 0},
	{offsetof(struct psy_battery, model_number), 1},
	{offsetof(struct psy_battery, serial_number), 1},
	{offsetof(struct psy_battery, type), 1},
	{offsetof(struct psy_battery, oem_info), 1},
};

static int extract_package(struct psy_battery *battery,
			   union acpi_object *package,
			   const struct acpi_offsets *offsets, int num)
{
	int i;
	union acpi_object *element;
	if (package->type != ACPI_TYPE_PACKAGE)
		return -EFAULT;
	for (i = 0; i < num; ++i) {
		if (package->package.count <= i)
			return -EFAULT;
		element = &package->package.elements[i];
		if (offsets[i].mode) {
			u8 *ptr = (u8 *)battery + offsets[i].offset;
			if (element->type == ACPI_TYPE_STRING ||
			    element->type == ACPI_TYPE_BUFFER)
				strncpy(ptr, element->string.pointer, 32);
			else if (element->type == ACPI_TYPE_INTEGER) {
				strncpy(ptr, (u8 *)&element->integer.value,
					sizeof(u64));
				ptr[sizeof(u64)] = 0;
			} else
				*ptr = 0; /* don't have value */
		} else {
			int *x = (int *)((u8 *)battery + offsets[i].offset);
			*x = (element->type == ACPI_TYPE_INTEGER) ?
				element->integer.value : -1;
		}
	}
	return 0;
}

static int psy_battery_get_status(struct psy_battery *battery)
{
	if (acpi_bus_get_status(battery->device)) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR, "Evaluating _STA"));
		return -ENODEV;
	}
	return 0;
}


static int extract_battery_info(struct psy_battery *battery,
			 const struct acpi_buffer *buffer)
{
	int result = -EFAULT;

	result = extract_package(battery, buffer->pointer,
			extended_info_offsets,
			ARRAY_SIZE(extended_info_offsets));
	if (test_bit(PSY_BATTERY_QUIRK_PERCENTAGE_CAPACITY, &battery->flags))
		battery->full_charge_capacity = battery->design_capacity;
	if (test_bit(PSY_BATTERY_QUIRK_DEGRADED_FULL_CHARGE, &battery->flags) &&
	    battery->capacity_now > battery->full_charge_capacity)
		battery->capacity_now = battery->full_charge_capacity;

	return result;
}

static int psy_battery_get_info(struct psy_battery *battery)
{
	int result = -ENODEV;

	if (!psy_battery_present(battery))
		return 0;


	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status = AE_ERROR;

	mutex_lock(&battery->lock);
	status = acpi_evaluate_object(battery->device->handle, "_BIX",
			NULL, &buffer);
	mutex_unlock(&battery->lock);

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating %s",
					"_BIX"));
	} else {
		result = extract_battery_info(battery, &buffer);

		kfree(buffer.pointer);
		break;
	}

	return result;
}

static int psy_battery_get_state(struct psy_battery *battery)
{
	int result = 0;
	acpi_status status = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	if (!psy_battery_present(battery))
		return 0;

	if (battery->update_time &&
	    time_before(jiffies, battery->update_time +
			msecs_to_jiffies(cache_time)))
		return 0;

	mutex_lock(&battery->lock);
	status = acpi_evaluate_object(battery->device->handle, "_BST",
				      NULL, &buffer);
	mutex_unlock(&battery->lock);

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _BST"));
		return -ENODEV;
	}

	result = extract_package(battery, buffer.pointer,
				 state_offsets, ARRAY_SIZE(state_offsets));
	battery->update_time = jiffies;
	kfree(buffer.pointer);

	/* For buggy DSDTs that report negative 16-bit values for either
	 * charging or discharging current and/or report 0 as 65536
	 * due to bad math.
	 */
	if (battery->power_unit == PSY_BATTERY_POWER_UNIT_MA &&
		battery->rate_now != PSY_BATTERY_VALUE_UNKNOWN &&
		(s16)(battery->rate_now) < 0) {
		battery->rate_now = abs((s16)battery->rate_now);
		pr_warn_once(FW_BUG "battery: (dis)charge rate invalid.\n");
	}

	if (test_bit(PSY_BATTERY_QUIRK_PERCENTAGE_CAPACITY, &battery->flags)
	    && battery->capacity_now >= 0 && battery->capacity_now <= 100)
		battery->capacity_now = (battery->capacity_now *
				battery->full_charge_capacity) / 100;
	if (test_bit(PSY_BATTERY_QUIRK_DEGRADED_FULL_CHARGE, &battery->flags) &&
	    battery->capacity_now > battery->full_charge_capacity)
		battery->capacity_now = battery->full_charge_capacity;

	return result;
}

static int psy_battery_set_alarm(struct psy_battery *battery)
{
	acpi_status status = 0;

	if (!psy_battery_present(battery))
		return -ENODEV;

	mutex_lock(&battery->lock);
	status = acpi_execute_simple_method(battery->device->handle, "_BTP",
					    battery->alarm);
	mutex_unlock(&battery->lock);

	if (ACPI_FAILURE(status))
		return -ENODEV;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Alarm set to %d\n", battery->alarm));
	return 0;
}

static int psy_battery_init_alarm(struct psy_battery *battery)
{
	/* Set default alarm */
	if (!battery->alarm)
		battery->alarm = battery->design_capacity_warning;
	return psy_battery_set_alarm(battery);
}

static ssize_t psy_battery_alarm_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct psy_battery *battery = to_psy_battery(dev_get_drvdata(dev));
	return sprintf(buf, "%d\n", battery->alarm * 1000);
}

static ssize_t psy_battery_alarm_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned long x;
	struct psy_battery *battery = to_psy_battery(dev_get_drvdata(dev));
	if (sscanf(buf, "%lu\n", &x) == 1)
		battery->alarm = x/1000;
	if (psy_battery_present(battery))
		psy_battery_set_alarm(battery);
	return count;
}

static const struct device_attribute alarm_attr = {
	.attr = {.name = "alarm", .mode = 0644},
	.show = psy_battery_alarm_show,
	.store = psy_battery_alarm_store,
};

static int sysfs_add_battery(struct psy_battery *battery)
{
	struct power_supply_config psy_cfg = { .drv_data = battery, };

	if (battery->power_unit == PSY_BATTERY_POWER_UNIT_MA) {
		battery->bat_desc.properties = charge_battery_props;
		battery->bat_desc.num_properties =
			ARRAY_SIZE(charge_battery_props);
	} else if (battery->full_charge_capacity == 0) {
		battery->bat_desc.properties =
			energy_battery_full_cap_broken_props;
		battery->bat_desc.num_properties =
			ARRAY_SIZE(energy_battery_full_cap_broken_props);
	} else {
		battery->bat_desc.properties = energy_battery_props;
		battery->bat_desc.num_properties =
			ARRAY_SIZE(energy_battery_props);
	}

	battery->bat_desc.name = acpi_device_bid(battery->device);
	battery->bat_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	battery->bat_desc.get_property = psy_battery_get_property;

	battery->bat = power_supply_register_no_ws(&battery->device->dev,
				&battery->bat_desc, &psy_cfg);

	if (IS_ERR(battery->bat)) {
		int result = PTR_ERR(battery->bat);

		battery->bat = NULL;
		return result;
	}
	return device_create_file(&battery->bat->dev, &alarm_attr);
}

static void sysfs_remove_battery(struct psy_battery *battery)
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

/*
 * According to the ACPI spec, some kinds of primary batteries can
 * report percentage battery remaining capacity directly to OS.
 * In this case, it reports the Last Full Charged Capacity == 100
 * and BatteryPresentRate == 0xFFFFFFFF.
 *
 * Now we found some battery reports percentage remaining capacity
 * even if it's rechargeable.
 * https://bugzilla.kernel.org/show_bug.cgi?id=15979
 *
 * Handle this correctly so that they won't break userspace.
 */
static void psy_battery_quirks(struct psy_battery *battery)
{
	if (test_bit(PSY_BATTERY_QUIRK_PERCENTAGE_CAPACITY, &battery->flags))
		return;

	if (battery->full_charge_capacity == 100 &&
		battery->rate_now == PSY_BATTERY_VALUE_UNKNOWN &&
		battery->capacity_now >= 0 && battery->capacity_now <= 100) {
		set_bit(PSY_BATTERY_QUIRK_PERCENTAGE_CAPACITY, &battery->flags);
		battery->full_charge_capacity = battery->design_capacity;
		battery->capacity_now = (battery->capacity_now *
				battery->full_charge_capacity) / 100;
	}

	if (test_bit(PSY_BATTERY_QUIRK_DEGRADED_FULL_CHARGE, &battery->flags))
		return;

	if (psy_battery_is_degraded(battery) &&
	    battery->capacity_now > battery->full_charge_capacity) {
		set_bit(PSY_BATTERY_QUIRK_DEGRADED_FULL_CHARGE, &battery->flags);
		battery->capacity_now = battery->full_charge_capacity;
	}
}

static int psy_battery_update(struct psy_battery *battery, bool resume)
{
	int result = psy_battery_get_status(battery);

	if (result)
		return result;

	if (!psy_battery_present(battery)) {
		sysfs_remove_battery(battery);
		battery->update_time = 0;
		return 0;
	}

	if (resume)
		return 0;

	if (!battery->update_time) {
		result = psy_battery_get_info(battery);
		if (result)
			return result;
		psy_battery_init_alarm(battery);
	}

	result = psy_battery_get_state(battery);
	if (result)
		return result;
	psy_battery_quirks(battery);

	if (!battery->bat) {
		result = sysfs_add_battery(battery);
		if (result)
			return result;
	}

	/*
	 * Wakeup the system if battery is critical low
	 * or lower than the alarm level
	 */
	if ((battery->state & PSY_BATTERY_STATE_CRITICAL) ||
            (battery->capacity_now <= battery->alarm))
		acpi_pm_wakeup_event(&battery->device->dev);

	return result;
}

static void psy_battery_refresh(struct psy_battery *battery)
{
	int power_unit;

	if (!battery->bat)
		return;

	power_unit = battery->power_unit;

	psy_battery_get_info(battery);

	if (power_unit == battery->power_unit)
		return;

	/* The battery has changed its reporting units. */
	sysfs_remove_battery(battery);
	sysfs_add_battery(battery);
}

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static void psy_battery_notify(struct acpi_device *device, u32 event)
{
	struct psy_battery *battery = acpi_driver_data(device);
	struct power_supply *old;

	if (!battery)
		return;
	old = battery->bat;

	if (event == PSY_BATTERY_NOTIFY_INFO)
		psy_battery_refresh(battery);
	psy_battery_update(battery, false);
	acpi_bus_generate_netlink_event(device->pnp.device_class,
					dev_name(&device->dev), event,
					psy_battery_present(battery));
	acpi_notifier_call_chain(device, event, psy_battery_present(battery));
	/* psy_battery_update could remove power_supply object */
	if (old && battery->bat)
		power_supply_changed(battery->bat);
}

static int battery_notify(struct notifier_block *nb,
			       unsigned long mode, void *_unused)
{
	struct psy_battery *battery = container_of(nb, struct psy_battery,
						    pm_nb);
	int result;

	switch (mode) {
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		if (!psy_battery_present(battery))
			return 0;

		if (battery->bat) {
			psy_battery_refresh(battery);
		} else {
			result = psy_battery_get_info(battery);
			if (result)
				return result;

			result = sysfs_add_battery(battery);
			if (result)
				return result;
		}

		psy_battery_init_alarm(battery);
		psy_battery_get_state(battery);
		break;
	}

	return 0;
}

static int psy_battery_add(struct acpi_device *device)
{
	int result = 0;
	struct psy_battery *battery = NULL;

	if (!device)
		return -EINVAL;

	if (device->dep_unmet)
		return -EPROBE_DEFER;

	battery = kzalloc(sizeof(struct psy_battery), GFP_KERNEL);
	if (!battery)
		return -ENOMEM;
	battery->device = device;
	strcpy(acpi_device_name(device), PSY_BATTERY_DEVICE_NAME);
	strcpy(acpi_device_class(device), PSY_BATTERY_CLASS);
	device->driver_data = battery;
	mutex_init(&battery->lock);
	mutex_init(&battery->sysfs_lock);

	result = psy_battery_update(battery);
	if (result)
		goto fail;

	pr_info(PREFIX "%s Slot [%s] (battery %s)\n",
		PSY_BATTERY_DEVICE_NAME, acpi_device_bid(device),
		device->status.battery_present ? "present" : "absent");

	battery->pm_nb.notifier_call = battery_notify;
	register_pm_notifier(&battery->pm_nb);

	device_init_wakeup(&device->dev, 1);

	return result;

fail:
	sysfs_remove_battery(battery);
	mutex_destroy(&battery->lock);
	mutex_destroy(&battery->sysfs_lock);
	kfree(battery);
	return result;
}

static int psy_battery_remove(struct acpi_device *device)
{
	struct psy_battery *battery = NULL;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;
	device_init_wakeup(&device->dev, 0);
	battery = acpi_driver_data(device);
	unregister_pm_notifier(&battery->pm_nb);
	sysfs_remove_battery(battery);
	mutex_destroy(&battery->lock);
	mutex_destroy(&battery->sysfs_lock);
	kfree(battery);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
/* this is needed to learn about changes made in suspended state */
static int psy_battery_resume(struct device *dev)
{
	struct psy_battery *battery;

	if (!dev)
		return -EINVAL;

	battery = acpi_driver_data(to_acpi_device(dev));
	if (!battery)
		return -EINVAL;

	battery->update_time = 0;
	psy_battery_update(battery, true);
	return 0;
}
#else
#define psy_battery_resume NULL
#endif

static SIMPLE_DEV_PM_OPS(psy_battery_pm, NULL, psy_battery_resume);

static struct acpi_driver psy_battery_driver = {
	.name = "battery",
	.class = PSY_BATTERY_CLASS,
	.ids = battery_device_ids,
	.flags = ACPI_DRIVER_ALL_NOTIFY_EVENTS,
	.ops = {
		.add = psy_battery_add,
		.remove = psy_battery_remove,
		.notify = psy_battery_notify,
		},
	.drv.pm = &psy_battery_pm,
};

static void __init psy_battery_init_async(void *unused, async_cookie_t cookie)
{
	unsigned int i;
	int result;

	result = acpi_bus_register_driver(&psy_battery_driver);
	battery_driver_registered = (result == 0);
}

static int __init psy_battery_init(void)
{
	if (acpi_disabled)
		return -ENODEV;

	async_cookie = async_schedule(psy_battery_init_async, NULL);
	return 0;
}

static void __exit psy_battery_exit(void)
{
	async_synchronize_cookie(async_cookie + 1);
	if (battery_driver_registered) {
		acpi_bus_unregister_driver(&psy_battery_driver);
	}
}

module_init(psy_battery_init);
module_exit(psy_battery_exit);
