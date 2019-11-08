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


/*
 * These report descriptors have been extracted from a Surface Book 2.
 * They seems to be similar enough to be usable on the Surface Laptop.
 */
static const u8 sid_vhf_hid_desc[] = {
	// keyboard descriptor (event command ID 0x03)
	0x05, 0x01,                     /*  Usage Page (Desktop),                   */
	0x09, 0x06,                     /*  Usage (Keyboard),                       */
	0xA1, 0x01,                     /*  Collection (Application),               */
	0x85, 0x01,                     /*      Report ID (1),                      */
	0x14,                           /*      Logical Minimum (0),                */
	0x25, 0x01,                     /*      Logical Maximum (1),                */
	0x75, 0x01,                     /*      Report Size (1),                    */
	0x95, 0x08,                     /*      Report Count (8),                   */
	0x05, 0x07,                     /*      Usage Page (Keyboard),              */
	0x19, 0xE0,                     /*      Usage Minimum (KB Leftcontrol),     */
	0x29, 0xE7,                     /*      Usage Maximum (KB Right GUI),       */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x75, 0x08,                     /*      Report Size (8),                    */
	0x95, 0x0A,                     /*      Report Count (10),                  */
	0x18,                           /*      Usage Minimum (None),               */
	0x29, 0x91,                     /*      Usage Maximum (KB LANG2),           */
	0x26, 0xFF, 0x00,               /*      Logical Maximum (255),              */
	0x80,                           /*      Input,                              */
	0x05, 0x0C,                     /*      Usage Page (Consumer),              */
	0x0A, 0xC0, 0x02,               /*      Usage (02C0h),                      */
	0xA1, 0x02,                     /*      Collection (Logical),               */
	0x1A, 0xC1, 0x02,               /*          Usage Minimum (02C1h),          */
	0x2A, 0xC6, 0x02,               /*          Usage Maximum (02C6h),          */
	0x95, 0x06,                     /*          Report Count (6),               */
	0xB1, 0x03,                     /*          Feature (Constant, Variable),   */
	0xC0,                           /*      End Collection,                     */
	0x05, 0x08,                     /*      Usage Page (LED),                   */
	0x19, 0x01,                     /*      Usage Minimum (01h),                */
	0x29, 0x03,                     /*      Usage Maximum (03h),                */
	0x75, 0x01,                     /*      Report Size (1),                    */
	0x95, 0x03,                     /*      Report Count (3),                   */
	0x25, 0x01,                     /*      Logical Maximum (1),                */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x95, 0x05,                     /*      Report Count (5),                   */
	0x91, 0x01,                     /*      Output (Constant),                  */
	0xC0,                           /*  End Collection,                         */

	0x05, 0x01,                     /*  Usage Page (Desktop),                   */
	0x09, 0x02,                     /*  Usage (Mouse),                          */
	0xA1, 0x01,                     /*  Collection (Application),               */
	0x85, 0x02,                     /*      Report ID (2),                      */
	0x05, 0x09,                     /*      Usage Page (Button),                */
	0x19, 0x01,                     /*      Usage Minimum (01h),                */
	0x29, 0x05,                     /*      Usage Maximum (05h),                */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x95, 0x01,                     /*      Report Count (1),                   */
	0x75, 0x03,                     /*      Report Size (3),                    */
	0x81, 0x03,                     /*      Input (Constant, Variable),         */
	0x15, 0x81,                     /*      Logical Minimum (-127),             */
	0x25, 0x7F,                     /*      Logical Maximum (127),              */
	0x75, 0x08,                     /*      Report Size (8),                    */
	0x95, 0x02,                     /*      Report Count (2),                   */
	0x05, 0x01,                     /*      Usage Page (Desktop),               */
	0x09, 0x30,                     /*      Usage (X),                          */
	0x09, 0x31,                     /*      Usage (Y),                          */
	0x81, 0x06,                     /*      Input (Variable, Relative),         */
	0xA1, 0x02,                     /*      Collection (Logical),               */
	0x09, 0x48,                     /*          Usage (Resolution Multiplier),  */
	0x14,                           /*          Logical Minimum (0),            */
	0x25, 0x01,                     /*          Logical Maximum (1),            */
	0x35, 0x01,                     /*          Physical Minimum (1),           */
	0x45, 0x10,                     /*          Physical Maximum (16),          */
	0x75, 0x02,                     /*          Report Size (2),                */
	0x95, 0x01,                     /*          Report Count (1),               */
	0xA4,                           /*          Push,                           */
	0xB1, 0x02,                     /*          Feature (Variable),             */
	0x09, 0x38,                     /*          Usage (Wheel),                  */
	0x15, 0x81,                     /*          Logical Minimum (-127),         */
	0x25, 0x7F,                     /*          Logical Maximum (127),          */
	0x34,                           /*          Physical Minimum (0),           */
	0x44,                           /*          Physical Maximum (0),           */
	0x75, 0x08,                     /*          Report Size (8),                */
	0x81, 0x06,                     /*          Input (Variable, Relative),     */
	0xC0,                           /*      End Collection,                     */
	0xA1, 0x02,                     /*      Collection (Logical),               */
	0x09, 0x48,                     /*          Usage (Resolution Multiplier),  */
	0xB4,                           /*          Pop,                            */
	0xB1, 0x02,                     /*          Feature (Variable),             */
	0x34,                           /*          Physical Minimum (0),           */
	0x44,                           /*          Physical Maximum (0),           */
	0x75, 0x04,                     /*          Report Size (4),                */
	0xB1, 0x03,                     /*          Feature (Constant, Variable),   */
	0x05, 0x0C,                     /*          Usage Page (Consumer),          */
	0x0A, 0x38, 0x02,               /*          Usage (AC Pan),                 */
	0x15, 0x81,                     /*          Logical Minimum (-127),         */
	0x25, 0x7F,                     /*          Logical Maximum (127),          */
	0x75, 0x08,                     /*          Report Size (8),                */
	0x81, 0x06,                     /*          Input (Variable, Relative),     */
	0xC0,                           /*      End Collection,                     */
	0xC0,                           /*  End Collection,                         */
	0x05, 0x0C,                     /*  Usage Page (Consumer),                  */

	// media key descriptor (event command ID 0x04)
	0x09, 0x01,                     /*  Usage (Consumer Control),               */
	0xA1, 0x01,                     /*  Collection (Application),               */
	0x85, 0x03,                     /*      Report ID (3),                      */
	0x75, 0x10,                     /*      Report Size (16),                   */
	0x14,                           /*      Logical Minimum (0),                */
	0x26, 0xFF, 0x03,               /*      Logical Maximum (1023),             */
	0x18,                           /*      Usage Minimum (00h),                */
	0x2A, 0xFF, 0x03,               /*      Usage Maximum (03FFh),              */
	0x80,                           /*      Input,                              */
	0xC0,                           /*  End Collection,                         */

	0x06, 0x05, 0xFF,               /*  Usage Page (FF05h),                     */
	0x09, 0x01,                     /*  Usage (01h),                            */
	0xA1, 0x01,                     /*  Collection (Application),               */
	0x85, 0x0D,                     /*      Report ID (13),                     */
	0x25, 0xFF,                     /*      Logical Maximum (-1),               */
	0x95, 0x02,                     /*      Report Count (2),                   */
	0x75, 0x08,                     /*      Report Size (8),                    */
	0x09, 0x20,                     /*      Usage (20h),                        */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x09, 0x22,                     /*      Usage (22h),                        */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x15, 0x81,                     /*      Logical Minimum (-127),             */
	0x25, 0x7F,                     /*      Logical Maximum (127),              */
	0x95, 0x20,                     /*      Report Count (32),                  */
	0x75, 0x08,                     /*      Report Size (8),                    */
	0x09, 0x21,                     /*      Usage (21h),                        */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x09, 0x23,                     /*      Usage (23h),                        */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0xC0,                           /*  End Collection,                         */
	0x09, 0x02,                     /*  Usage (02h),                            */
	0xA1, 0x01,                     /*  Collection (Application),               */
	0x85, 0x0C,                     /*      Report ID (12),                     */
	0x14,                           /*      Logical Minimum (0),                */
	0x25, 0xFF,                     /*      Logical Maximum (-1),               */
	0x95, 0x01,                     /*      Report Count (1),                   */
	0x08,                           /*      Usage (00h),                        */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0xC0,                           /*  End Collection,                         */
	0x05, 0x0D,                     /*  Usage Page (Digitizer),                 */
	0x09, 0x05,                     /*  Usage (Touchpad),                       */
	0xA1, 0x01,                     /*  Collection (Application),               */
	0x85, 0x04,                     /*      Report ID (4),                      */
	0x09, 0x22,                     /*      Usage (Finger),                     */
	0xA1, 0x02,                     /*      Collection (Logical),               */
	0x25, 0x01,                     /*          Logical Maximum (1),            */
	0x09, 0x47,                     /*          Usage (Touch Valid),            */
	0x09, 0x42,                     /*          Usage (Tip Switch),             */
	0x95, 0x02,                     /*          Report Count (2),               */
	0x75, 0x01,                     /*          Report Size (1),                */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x95, 0x01,                     /*          Report Count (1),               */
	0x75, 0x03,                     /*          Report Size (3),                */
	0x25, 0x03,                     /*          Logical Maximum (3),            */
	0x09, 0x51,                     /*          Usage (Contact Identifier),     */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x75, 0x01,                     /*          Report Size (1),                */
	0x95, 0x03,                     /*          Report Count (3),               */
	0x81, 0x03,                     /*          Input (Constant, Variable),     */
	0x05, 0x01,                     /*          Usage Page (Desktop),           */
	0x26, 0xE4, 0x07,               /*          Logical Maximum (2020),         */
	0x75, 0x10,                     /*          Report Size (16),               */
	0x55, 0x0E,                     /*          Unit Exponent (14),             */
	0x65, 0x11,                     /*          Unit (Centimeter),              */
	0x09, 0x30,                     /*          Usage (X),                      */
	0x46, 0xF2, 0x03,               /*          Physical Maximum (1010),        */
	0x95, 0x01,                     /*          Report Count (1),               */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x46, 0x94, 0x02,               /*          Physical Maximum (660),         */
	0x26, 0x29, 0x05,               /*          Logical Maximum (1321),         */
	0x09, 0x31,                     /*          Usage (Y),                      */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x44,                           /*          Physical Maximum (0),           */
	0x54,                           /*          Unit Exponent (0),              */
	0x64,                           /*          Unit,                           */
	0xC0,                           /*      End Collection,                     */
	0x05, 0x0D,                     /*      Usage Page (Digitizer),             */
	0x09, 0x22,                     /*      Usage (Finger),                     */
	0xA1, 0x02,                     /*      Collection (Logical),               */
	0x25, 0x01,                     /*          Logical Maximum (1),            */
	0x09, 0x47,                     /*          Usage (Touch Valid),            */
	0x09, 0x42,                     /*          Usage (Tip Switch),             */
	0x95, 0x02,                     /*          Report Count (2),               */
	0x75, 0x01,                     /*          Report Size (1),                */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x95, 0x01,                     /*          Report Count (1),               */
	0x75, 0x03,                     /*          Report Size (3),                */
	0x25, 0x03,                     /*          Logical Maximum (3),            */
	0x09, 0x51,                     /*          Usage (Contact Identifier),     */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x75, 0x01,                     /*          Report Size (1),                */
	0x95, 0x03,                     /*          Report Count (3),               */
	0x81, 0x03,                     /*          Input (Constant, Variable),     */
	0x05, 0x01,                     /*          Usage Page (Desktop),           */
	0x26, 0xE4, 0x07,               /*          Logical Maximum (2020),         */
	0x75, 0x10,                     /*          Report Size (16),               */
	0x55, 0x0E,                     /*          Unit Exponent (14),             */
	0x65, 0x11,                     /*          Unit (Centimeter),              */
	0x09, 0x30,                     /*          Usage (X),                      */
	0x46, 0xF2, 0x03,               /*          Physical Maximum (1010),        */
	0x95, 0x01,                     /*          Report Count (1),               */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x46, 0x94, 0x02,               /*          Physical Maximum (660),         */
	0x26, 0x29, 0x05,               /*          Logical Maximum (1321),         */
	0x09, 0x31,                     /*          Usage (Y),                      */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x44,                           /*          Physical Maximum (0),           */
	0x54,                           /*          Unit Exponent (0),              */
	0x64,                           /*          Unit,                           */
	0xC0,                           /*      End Collection,                     */
	0x05, 0x0D,                     /*      Usage Page (Digitizer),             */
	0x09, 0x22,                     /*      Usage (Finger),                     */
	0xA1, 0x02,                     /*      Collection (Logical),               */
	0x25, 0x01,                     /*          Logical Maximum (1),            */
	0x09, 0x47,                     /*          Usage (Touch Valid),            */
	0x09, 0x42,                     /*          Usage (Tip Switch),             */
	0x95, 0x02,                     /*          Report Count (2),               */
	0x75, 0x01,                     /*          Report Size (1),                */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x95, 0x01,                     /*          Report Count (1),               */
	0x75, 0x03,                     /*          Report Size (3),                */
	0x25, 0x03,                     /*          Logical Maximum (3),            */
	0x09, 0x51,                     /*          Usage (Contact Identifier),     */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x75, 0x01,                     /*          Report Size (1),                */
	0x95, 0x03,                     /*          Report Count (3),               */
	0x81, 0x03,                     /*          Input (Constant, Variable),     */
	0x05, 0x01,                     /*          Usage Page (Desktop),           */
	0x26, 0xE4, 0x07,               /*          Logical Maximum (2020),         */
	0x75, 0x10,                     /*          Report Size (16),               */
	0x55, 0x0E,                     /*          Unit Exponent (14),             */
	0x65, 0x11,                     /*          Unit (Centimeter),              */
	0x09, 0x30,                     /*          Usage (X),                      */
	0x46, 0xF2, 0x03,               /*          Physical Maximum (1010),        */
	0x95, 0x01,                     /*          Report Count (1),               */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x46, 0x94, 0x02,               /*          Physical Maximum (660),         */
	0x26, 0x29, 0x05,               /*          Logical Maximum (1321),         */
	0x09, 0x31,                     /*          Usage (Y),                      */
	0x81, 0x02,                     /*          Input (Variable),               */
	0xC0,                           /*      End Collection,                     */
	0x05, 0x0D,                     /*      Usage Page (Digitizer),             */
	0x09, 0x22,                     /*      Usage (Finger),                     */
	0xA1, 0x02,                     /*      Collection (Logical),               */
	0x25, 0x01,                     /*          Logical Maximum (1),            */
	0x09, 0x47,                     /*          Usage (Touch Valid),            */
	0x09, 0x42,                     /*          Usage (Tip Switch),             */
	0x95, 0x02,                     /*          Report Count (2),               */
	0x75, 0x01,                     /*          Report Size (1),                */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x95, 0x01,                     /*          Report Count (1),               */
	0x75, 0x03,                     /*          Report Size (3),                */
	0x25, 0x03,                     /*          Logical Maximum (3),            */
	0x09, 0x51,                     /*          Usage (Contact Identifier),     */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x75, 0x01,                     /*          Report Size (1),                */
	0x95, 0x03,                     /*          Report Count (3),               */
	0x81, 0x03,                     /*          Input (Constant, Variable),     */
	0x05, 0x01,                     /*          Usage Page (Desktop),           */
	0x26, 0xE4, 0x07,               /*          Logical Maximum (2020),         */
	0x75, 0x10,                     /*          Report Size (16),               */
	0x55, 0x0E,                     /*          Unit Exponent (14),             */
	0x65, 0x11,                     /*          Unit (Centimeter),              */
	0x09, 0x30,                     /*          Usage (X),                      */
	0x46, 0xF2, 0x03,               /*          Physical Maximum (1010),        */
	0x95, 0x01,                     /*          Report Count (1),               */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x46, 0x94, 0x02,               /*          Physical Maximum (660),         */
	0x26, 0x29, 0x05,               /*          Logical Maximum (1321),         */
	0x09, 0x31,                     /*          Usage (Y),                      */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x44,                           /*          Physical Maximum (0),           */
	0x54,                           /*          Unit Exponent (0),              */
	0x64,                           /*          Unit,                           */
	0xC0,                           /*      End Collection,                     */
	0x05, 0x0D,                     /*      Usage Page (Digitizer),             */
	0x09, 0x22,                     /*      Usage (Finger),                     */
	0xA1, 0x02,                     /*      Collection (Logical),               */
	0x25, 0x01,                     /*          Logical Maximum (1),            */
	0x09, 0x47,                     /*          Usage (Touch Valid),            */
	0x09, 0x42,                     /*          Usage (Tip Switch),             */
	0x95, 0x02,                     /*          Report Count (2),               */
	0x75, 0x01,                     /*          Report Size (1),                */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x95, 0x01,                     /*          Report Count (1),               */
	0x75, 0x03,                     /*          Report Size (3),                */
	0x25, 0x03,                     /*          Logical Maximum (3),            */
	0x09, 0x51,                     /*          Usage (Contact Identifier),     */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x75, 0x01,                     /*          Report Size (1),                */
	0x95, 0x03,                     /*          Report Count (3),               */
	0x81, 0x03,                     /*          Input (Constant, Variable),     */
	0x05, 0x01,                     /*          Usage Page (Desktop),           */
	0x26, 0xE4, 0x07,               /*          Logical Maximum (2020),         */
	0x75, 0x10,                     /*          Report Size (16),               */
	0x55, 0x0E,                     /*          Unit Exponent (14),             */
	0x65, 0x11,                     /*          Unit (Centimeter),              */
	0x09, 0x30,                     /*          Usage (X),                      */
	0x46, 0xF2, 0x03,               /*          Physical Maximum (1010),        */
	0x95, 0x01,                     /*          Report Count (1),               */
	0x81, 0x02,                     /*          Input (Variable),               */
	0x46, 0x94, 0x02,               /*          Physical Maximum (660),         */
	0x26, 0x29, 0x05,               /*          Logical Maximum (1321),         */
	0x09, 0x31,                     /*          Usage (Y),                      */
	0x81, 0x02,                     /*          Input (Variable),               */
	0xC0,                           /*      End Collection,                     */
	0x05, 0x0D,                     /*      Usage Page (Digitizer),             */
	0x55, 0x0C,                     /*      Unit Exponent (12),                 */
	0x66, 0x01, 0x10,               /*      Unit (Seconds),                     */
	0x47, 0xFF, 0xFF, 0x00, 0x00,   /*      Physical Maximum (65535),           */
	0x27, 0xFF, 0xFF, 0x00, 0x00,   /*      Logical Maximum (65535),            */
	0x09, 0x56,                     /*      Usage (Scan Time),                  */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x09, 0x54,                     /*      Usage (Contact Count),              */
	0x25, 0x7F,                     /*      Logical Maximum (127),              */
	0x75, 0x08,                     /*      Report Size (8),                    */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x05, 0x09,                     /*      Usage Page (Button),                */
	0x09, 0x01,                     /*      Usage (01h),                        */
	0x25, 0x01,                     /*      Logical Maximum (1),                */
	0x75, 0x01,                     /*      Report Size (1),                    */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x95, 0x07,                     /*      Report Count (7),                   */
	0x81, 0x03,                     /*      Input (Constant, Variable),         */
	0x05, 0x0D,                     /*      Usage Page (Digitizer),             */
	0x85, 0x04,                     /*      Report ID (4),                      */
	0x09, 0x55,                     /*      Usage (Contact Count Maximum),      */
	0x09, 0x59,                     /*      Usage (Pad Type),                   */
	0x75, 0x04,                     /*      Report Size (4),                    */
	0x95, 0x02,                     /*      Report Count (2),                   */
	0x25, 0x0F,                     /*      Logical Maximum (15),               */
	0xB1, 0x02,                     /*      Feature (Variable),                 */
	0x06, 0x00, 0xFF,               /*      Usage Page (FF00h),                 */
	0x09, 0xC6,                     /*      Usage (C6h),                        */
	0x85, 0x05,                     /*      Report ID (5),                      */
	0x14,                           /*      Logical Minimum (0),                */
	0x25, 0x08,                     /*      Logical Maximum (8),                */
	0x75, 0x08,                     /*      Report Size (8),                    */
	0x95, 0x01,                     /*      Report Count (1),                   */
	0xB1, 0x02,                     /*      Feature (Variable),                 */
	0x09, 0xC7,                     /*      Usage (C7h),                        */
	0x26, 0xFF, 0x00,               /*      Logical Maximum (255),              */
	0x75, 0x08,                     /*      Report Size (8),                    */
	0x95, 0x20,                     /*      Report Count (32),                  */
	0xB1, 0x02,                     /*      Feature (Variable),                 */
	0xC0,                           /*  End Collection,                         */
	0x05, 0x0D,                     /*  Usage Page (Digitizer),                 */
	0x09, 0x0E,                     /*  Usage (Configuration),                  */
	0xA1, 0x01,                     /*  Collection (Application),               */
	0x85, 0x07,                     /*      Report ID (7),                      */
	0x09, 0x22,                     /*      Usage (Finger),                     */
	0xA1, 0x02,                     /*      Collection (Logical),               */
	0x09, 0x52,                     /*          Usage (Device Mode),            */
	0x14,                           /*          Logical Minimum (0),            */
	0x25, 0x0A,                     /*          Logical Maximum (10),           */
	0x75, 0x08,                     /*          Report Size (8),                */
	0x95, 0x01,                     /*          Report Count (1),               */
	0xB1, 0x02,                     /*          Feature (Variable),             */
	0xC0,                           /*      End Collection,                     */
	0x09, 0x22,                     /*      Usage (Finger),                     */
	0xA0,                           /*      Collection (Physical),              */
	0x85, 0x08,                     /*          Report ID (8),                  */
	0x09, 0x57,                     /*          Usage (Surface Switch),         */
	0x09, 0x58,                     /*          Usage (Button Switch),          */
	0x75, 0x01,                     /*          Report Size (1),                */
	0x95, 0x02,                     /*          Report Count (2),               */
	0x25, 0x01,                     /*          Logical Maximum (1),            */
	0xB1, 0x02,                     /*          Feature (Variable),             */
	0x95, 0x06,                     /*          Report Count (6),               */
	0xB1, 0x03,                     /*          Feature (Constant, Variable),   */
	0xC0,                           /*      End Collection,                     */
	0xC0,                           /*  End Collection,                         */
	0x06, 0x07, 0xFF,               /*  Usage Page (FF07h),                     */
	0x09, 0x01,                     /*  Usage (01h),                            */
	0xA1, 0x01,                     /*  Collection (Application),               */
	0x85, 0x0A,                     /*      Report ID (10),                     */
	0x09, 0x02,                     /*      Usage (02h),                        */
	0x26, 0xFF, 0x00,               /*      Logical Maximum (255),              */
	0x75, 0x08,                     /*      Report Size (8),                    */
	0x95, 0x14,                     /*      Report Count (20),                  */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x85, 0x09,                     /*      Report ID (9),                      */
	0x09, 0x03,                     /*      Usage (03h),                        */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x85, 0x0A,                     /*      Report ID (10),                     */
	0x09, 0x04,                     /*      Usage (04h),                        */
	0x95, 0x26,                     /*      Report Count (38),                  */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x85, 0x09,                     /*      Report ID (9),                      */
	0x09, 0x05,                     /*      Usage (05h),                        */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x85, 0x09,                     /*      Report ID (9),                      */
	0x09, 0x06,                     /*      Usage (06h),                        */
	0x95, 0x01,                     /*      Report Count (1),                   */
	0xB1, 0x02,                     /*      Feature (Variable),                 */
	0x85, 0x0B,                     /*      Report ID (11),                     */
	0x09, 0x07,                     /*      Usage (07h),                        */
	0xB1, 0x02,                     /*      Feature (Variable),                 */
	0xC0,                           /*  End Collection,                         */
	0x06, 0x05, 0xFF,               /*  Usage Page (FF05h),                     */
	0x09, 0x04,                     /*  Usage (04h),                            */
	0xA1, 0x01,                     /*  Collection (Application),               */
	0x85, 0x0E,                     /*      Report ID (14),                     */
	0x09, 0x31,                     /*      Usage (31h),                        */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x09, 0x31,                     /*      Usage (31h),                        */
	0x81, 0x03,                     /*      Input (Constant, Variable),         */
	0x09, 0x30,                     /*      Usage (30h),                        */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x09, 0x30,                     /*      Usage (30h),                        */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x95, 0x39,                     /*      Report Count (57),                  */
	0x09, 0x32,                     /*      Usage (32h),                        */
	0x92, 0x02, 0x01,               /*      Output (Variable, Buffered Bytes),  */
	0x09, 0x32,                     /*      Usage (32h),                        */
	0x82, 0x02, 0x01,               /*      Input (Variable, Buffered Bytes),   */
	0xC0,                           /*  End Collection,                         */
	0x06, 0x05, 0xFF,               /*  Usage Page (FF05h),                     */
	0x09, 0x50,                     /*  Usage (50h),                            */
	0xA1, 0x01,                     /*  Collection (Application),               */
	0x85, 0x20,                     /*      Report ID (32),                     */
	0x14,                           /*      Logical Minimum (0),                */
	0x25, 0xFF,                     /*      Logical Maximum (-1),               */
	0x75, 0x08,                     /*      Report Size (8),                    */
	0x95, 0x3C,                     /*      Report Count (60),                  */
	0x09, 0x60,                     /*      Usage (60h),                        */
	0x82, 0x02, 0x01,               /*      Input (Variable, Buffered Bytes),   */
	0x09, 0x61,                     /*      Usage (61h),                        */
	0x92, 0x02, 0x01,               /*      Output (Variable, Buffered Bytes),  */
	0x09, 0x62,                     /*      Usage (62h),                        */
	0xB2, 0x02, 0x01,               /*      Feature (Variable, Buffered Bytes), */
	0x85, 0x21,                     /*      Report ID (33),                     */
	0x09, 0x63,                     /*      Usage (63h),                        */
	0x82, 0x02, 0x01,               /*      Input (Variable, Buffered Bytes),   */
	0x09, 0x64,                     /*      Usage (64h),                        */
	0x92, 0x02, 0x01,               /*      Output (Variable, Buffered Bytes),  */
	0x09, 0x65,                     /*      Usage (65h),                        */
	0xB2, 0x02, 0x01,               /*      Feature (Variable, Buffered Bytes), */
	0x85, 0x22,                     /*      Report ID (34),                     */
	0x25, 0xFF,                     /*      Logical Maximum (-1),               */
	0x75, 0x20,                     /*      Report Size (32),                   */
	0x95, 0x04,                     /*      Report Count (4),                   */
	0x19, 0x66,                     /*      Usage Minimum (66h),                */
	0x29, 0x69,                     /*      Usage Maximum (69h),                */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x19, 0x6A,                     /*      Usage Minimum (6Ah),                */
	0x29, 0x6D,                     /*      Usage Maximum (6Dh),                */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x19, 0x6E,                     /*      Usage Minimum (6Eh),                */
	0x29, 0x71,                     /*      Usage Maximum (71h),                */
	0xB1, 0x02,                     /*      Feature (Variable),                 */
	0x85, 0x23,                     /*      Report ID (35),                     */
	0x19, 0x72,                     /*      Usage Minimum (72h),                */
	0x29, 0x75,                     /*      Usage Maximum (75h),                */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x19, 0x76,                     /*      Usage Minimum (76h),                */
	0x29, 0x79,                     /*      Usage Maximum (79h),                */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x19, 0x7A,                     /*      Usage Minimum (7Ah),                */
	0x29, 0x7D,                     /*      Usage Maximum (7Dh),                */
	0xB1, 0x02,                     /*      Feature (Variable),                 */
	0x85, 0x24,                     /*      Report ID (36),                     */
	0x19, 0x7E,                     /*      Usage Minimum (7Eh),                */
	0x29, 0x81,                     /*      Usage Maximum (81h),                */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x19, 0x82,                     /*      Usage Minimum (82h),                */
	0x29, 0x85,                     /*      Usage Maximum (85h),                */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x19, 0x86,                     /*      Usage Minimum (86h),                */
	0x29, 0x89,                     /*      Usage Maximum (89h),                */
	0xB1, 0x02,                     /*      Feature (Variable),                 */
	0x85, 0x25,                     /*      Report ID (37),                     */
	0x19, 0x8A,                     /*      Usage Minimum (8Ah),                */
	0x29, 0x8D,                     /*      Usage Maximum (8Dh),                */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x19, 0x8E,                     /*      Usage Minimum (8Eh),                */
	0x29, 0x91,                     /*      Usage Maximum (91h),                */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x19, 0x92,                     /*      Usage Minimum (92h),                */
	0x29, 0x95,                     /*      Usage Maximum (95h),                */
	0xB1, 0x02,                     /*      Feature (Variable),                 */
	0x85, 0x26,                     /*      Report ID (38),                     */
	0x19, 0x96,                     /*      Usage Minimum (96h),                */
	0x29, 0x99,                     /*      Usage Maximum (99h),                */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x19, 0x9A,                     /*      Usage Minimum (9Ah),                */
	0x29, 0x9D,                     /*      Usage Maximum (9Dh),                */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x19, 0x9E,                     /*      Usage Minimum (9Eh),                */
	0x29, 0xA1,                     /*      Usage Maximum (A1h),                */
	0xB1, 0x02,                     /*      Feature (Variable),                 */
	0x85, 0x27,                     /*      Report ID (39),                     */
	0x19, 0xA2,                     /*      Usage Minimum (A2h),                */
	0x29, 0xA5,                     /*      Usage Maximum (A5h),                */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x19, 0xA6,                     /*      Usage Minimum (A6h),                */
	0x29, 0xA9,                     /*      Usage Maximum (A9h),                */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x19, 0xAA,                     /*      Usage Minimum (AAh),                */
	0x29, 0xAD,                     /*      Usage Maximum (ADh),                */
	0xB1, 0x02,                     /*      Feature (Variable),                 */
	0x85, 0x28,                     /*      Report ID (40),                     */
	0x19, 0xAE,                     /*      Usage Minimum (AEh),                */
	0x29, 0xB1,                     /*      Usage Maximum (B1h),                */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x19, 0xB2,                     /*      Usage Minimum (B2h),                */
	0x29, 0xB5,                     /*      Usage Maximum (B5h),                */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x19, 0xB6,                     /*      Usage Minimum (B6h),                */
	0x29, 0xB9,                     /*      Usage Maximum (B9h),                */
	0xB1, 0x02,                     /*      Feature (Variable),                 */
	0x85, 0x29,                     /*      Report ID (41),                     */
	0x19, 0xBA,                     /*      Usage Minimum (BAh),                */
	0x29, 0xBD,                     /*      Usage Maximum (BDh),                */
	0x81, 0x02,                     /*      Input (Variable),                   */
	0x19, 0xBE,                     /*      Usage Minimum (BEh),                */
	0x29, 0xC1,                     /*      Usage Maximum (C1h),                */
	0x91, 0x02,                     /*      Output (Variable),                  */
	0x19, 0xC2,                     /*      Usage Minimum (C2h),                */
	0x29, 0xC5,                     /*      Usage Maximum (C5h),                */
	0xB1, 0x02,                     /*      Feature (Variable),                 */
	0xC0,                           /*  End Collection,                         */

	// Firmware update report disabled because linux drivers start poking at it
	/* 0x06, 0x00, 0xFF,               /1*  Usage Page (FF00h),                     *1/ */
	/* 0x0A, 0x00, 0xF9,               /1*  Usage (F900h),                          *1/ */
	/* 0xA1, 0x01,                     /1*  Collection (Application),               *1/ */
	/* 0x85, 0x32,                     /1*      Report ID (50),                     *1/ */
	/* 0x75, 0x10,                     /1*      Report Size (16),                   *1/ */
	/* 0x95, 0x02,                     /1*      Report Count (2),                   *1/ */
	/* 0x14,                           /1*      Logical Minimum (0),                *1/ */
	/* 0x27, 0xFF, 0xFF, 0x00, 0x00,   /1*      Logical Maximum (65535),            *1/ */
	/* 0x0A, 0x01, 0xF9,               /1*      Usage (F901h),                      *1/ */
	/* 0xB1, 0x02,                     /1*      Feature (Variable),                 *1/ */
	/* 0x75, 0x20,                     /1*      Report Size (32),                   *1/ */
	/* 0x95, 0x01,                     /1*      Report Count (1),                   *1/ */
	/* 0x25, 0xFF,                     /1*      Logical Maximum (-1),               *1/ */
	/* 0x0A, 0x02, 0xF9,               /1*      Usage (F902h),                      *1/ */
	/* 0xB1, 0x02,                     /1*      Feature (Variable),                 *1/ */
	/* 0x75, 0x08,                     /1*      Report Size (8),                    *1/ */
	/* 0x95, 0x08,                     /1*      Report Count (8),                   *1/ */
	/* 0x26, 0xFF, 0x00,               /1*      Logical Maximum (255),              *1/ */
	/* 0x0A, 0x03, 0xF9,               /1*      Usage (F903h),                      *1/ */
	/* 0xB2, 0x02, 0x01,               /1*      Feature (Variable, Buffered Bytes), *1/ */
	/* 0x95, 0x10,                     /1*      Report Count (16),                  *1/ */
	/* 0x0A, 0x04, 0xF9,               /1*      Usage (F904h),                      *1/ */
	/* 0xB2, 0x02, 0x01,               /1*      Feature (Variable, Buffered Bytes), *1/ */
	/* 0x0A, 0x05, 0xF9,               /1*      Usage (F905h),                      *1/ */
	/* 0xB2, 0x02, 0x01,               /1*      Feature (Variable, Buffered Bytes), *1/ */
	/* 0x95, 0x01,                     /1*      Report Count (1),                   *1/ */
	/* 0x75, 0x10,                     /1*      Report Size (16),                   *1/ */
	/* 0x27, 0xFF, 0xFF, 0x00, 0x00,   /1*      Logical Maximum (65535),            *1/ */
	/* 0x0A, 0x06, 0xF9,               /1*      Usage (F906h),                      *1/ */
	/* 0x81, 0x02,                     /1*      Input (Variable),                   *1/ */
	/* 0xC0                            /1*  End Collection                          *1/ */
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

	ret = hid_parse_report(hid, (u8 *)sid_vhf_hid_desc, ARRAY_SIZE(sid_vhf_hid_desc));
	/* ret = hid_parse_report(hid, buf, size); */
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
