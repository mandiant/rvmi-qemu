/*
 * QEMU KVM VMI BP support
 *
 * Copyright (C) 2017 FireEye, Inc. All Rights Reserved.
 *
 * Authors:
 *  Jonas Pfoh      <jonas.pfoh@fireeye.com>
 *  Sebastian Vogl  <sebastian.vogl@fireeye.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 * Version 2 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef VMI_BP_H
#define VMI_BP_H

#include "linux/kvm_vmi.h"

void vmi_bp_refresh_bps(CPUState *cpu, uint64_t prev_cr3, uint64_t cur_cr3);
void vmi_rm_all_bps(void);

void vmi_bp_handle_running(CPUState *cpu);
void vmi_bp_handle_debug(CPUState *cpu, struct kvm_vmi_event_debug *event_dbg);
void vmi_bp_handle_mtf(CPUState *cpu);

bool vmi_ss_ready(void);

void vmi_bp_initialize(void);
void vmi_bp_uninitialize(void);
#endif
