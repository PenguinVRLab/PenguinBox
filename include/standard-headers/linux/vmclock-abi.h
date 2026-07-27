/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-2-Clause) */

/*
 * This structure provides a vDSO-style clock to VM guests, exposing the
 * relationship (or lack thereof) between the CPU clock (TSC, timebase, arch
 * counter, etc.) and real time. It is designed to address the problem of
 * live migration, which other clock enlightenments do not.
 *
 * When a guest is live migrated, this affects the clock in two ways.
 *
 * First, even between identical hosts the actual frequency of the underlying
 * counter will change within the tolerances of its specification (typically
 * ±50PPM, or 4 seconds a day). This frequency also varies over time on the
 * same host, but can be tracked by NTP as it generally varies slowly. With
 * live migration there is a step change in the frequency, with no warning.
 *
 * Second, there may be a step change in the value of the counter itself, as
 * its accuracy is limited by the precision of the NTP synchronization on the
 * source and destination hosts.
 *
 * So any calibration (NTP, PTP, etc.) which the guest has done on the source
 * host before migration is invalid, and needs to be redone on the new host.
 *
 * In its most basic mode, this structure provides only an indication to the
 * guest that live migration has occurred. This allows the guest to know that
 * its clock is invalid and take remedial action. For applications that need
 * reliable accurate timestamps (e.g. distributed databases), the structure
 * can be mapped all the way to userspace. This allows the application to see
 * directly for itself that the clock is disrupted and take appropriate
 * action, even when using a vDSO-style method to get the time instead of a
 * system call.
 *
 * In its more advanced mode. this structure can also be used to expose the
 * precise relationship of the CPU counter to real time, as calibrated by the
 * host. This means that userspace applications can have accurate time
 * immediately after live migration, rather than having to pause operations
 * and wait for NTP to recover. This mode does, of course, rely on the
 * counter being reliable and consistent across CPUs.
 *
 * Note that this must be true UTC, never with smeared leap seconds. If a
 * guest wishes to construct a smeared clock, it can do so. Presenting a
 * smeared clock through this interface would be problematic because it
 * actually messes with the apparent counter *period*. A linear smearing
 * of 1 ms per second would effectively tweak the counter period by 1000PPM
 * at the start/end of the smearing period, while a sinusoidal smear would
 * basically be impossible to represent.
 *
 * This structure is offered with the intent that it be adopted into the
 * nascent virtio-rtc standard, as a virtio-rtc that does not address the live
 * migration problem seems a little less than fit for purpose. For that
 * reason, certain fields use precisely the same numeric definitions as in
 * the virtio-rtc proposal. The structure can also be exposed through an ACPI
 * device with the CID "VMCLOCK", modelled on the "VMGENID" device except for
 * the fact that it uses a real _CRS to convey the address of the structure
 * (which should be a full page, to allow for mapping directly to userspace).
 */

#ifndef __VMCLOCK_ABI_H__
#define __VMCLOCK_ABI_H__

#include "standard-headers/linux/types.h"

struct vmclock_abi {
	uint32_t magic;
#define VMCLOCK_MAGIC	0x4b4c4356
	uint32_t size;
	uint16_t version;
	uint8_t counter_id;
#define VMCLOCK_COUNTER_ARM_VCNT	0
#define VMCLOCK_COUNTER_X86_TSC		1
#define VMCLOCK_COUNTER_INVALID		0xff
	uint8_t time_type;
#define VMCLOCK_TIME_UTC			0
#define VMCLOCK_TIME_TAI			1
#define VMCLOCK_TIME_MONOTONIC			2
#define VMCLOCK_TIME_INVALID_SMEARED		3
#define VMCLOCK_TIME_INVALID_MAYBE_SMEARED	4

	uint32_t seq_count;
	uint64_t disruption_marker;
	uint64_t flags;
#define VMCLOCK_FLAG_TAI_OFFSET_VALID		(1 << 0)
#define VMCLOCK_FLAG_DISRUPTION_SOON		(1 << 1)
#define VMCLOCK_FLAG_DISRUPTION_IMMINENT	(1 << 2)
#define VMCLOCK_FLAG_PERIOD_ESTERROR_VALID	(1 << 3)
#define VMCLOCK_FLAG_PERIOD_MAXERROR_VALID	(1 << 4)
#define VMCLOCK_FLAG_TIME_ESTERROR_VALID	(1 << 5)
#define VMCLOCK_FLAG_TIME_MAXERROR_VALID	(1 << 6)
#define VMCLOCK_FLAG_TIME_MONOTONIC		(1 << 7)

	uint8_t pad[2];
	uint8_t clock_status;
#define VMCLOCK_STATUS_UNKNOWN		0
#define VMCLOCK_STATUS_INITIALIZING	1
#define VMCLOCK_STATUS_SYNCHRONIZED	2
#define VMCLOCK_STATUS_FREERUNNING	3
#define VMCLOCK_STATUS_UNRELIABLE	4

	uint8_t leap_second_smearing_hint;
#define VMCLOCK_SMEARING_STRICT		0
#define VMCLOCK_SMEARING_NOON_LINEAR	1
#define VMCLOCK_SMEARING_UTC_SLS	2
	uint16_t tai_offset_sec;
	uint8_t leap_indicator;
#define VMCLOCK_LEAP_NONE	0x00
#define VMCLOCK_LEAP_PRE_POS	0x01
#define VMCLOCK_LEAP_PRE_NEG	0x02
#define VMCLOCK_LEAP_POS	0x03
#define VMCLOCK_LEAP_POST_POS	0x04
#define VMCLOCK_LEAP_POST_NEG	0x05

	uint8_t counter_period_shift;
	uint64_t counter_value;
	uint64_t counter_period_frac_sec;
	uint64_t counter_period_esterror_rate_frac_sec;
	uint64_t counter_period_maxerror_rate_frac_sec;

	uint64_t time_sec;
	uint64_t time_frac_sec;
	uint64_t time_esterror_nanosec;
	uint64_t time_maxerror_nanosec;
};

#endif
