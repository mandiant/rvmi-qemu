/*
 * QEMU KVM VMI support
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

#ifndef __LINUX_KVM_VMI_H
#define __LINUX_KVM_VMI_H

#include <linux/types.h>
#include <linux/perf_event.h>

#include <linux/ioctl.h>

#define MAX_LBR_ENTRIES                   16
#define MSR_LBR_FROM                      0x680
#define MSR_LBR_TO                        0x6c0

enum KVM_VMI_FEATURE {
    KVM_VMI_FEATURE_TRAP_TASK_SWITCH = 0,
    KVM_VMI_FEATURE_LBR,
    KVM_VMI_FEATURE_MTF,
    KVM_VMI_FEATURE_MAX
};

struct kvm_vmi_feature_task_switch {
    enum KVM_VMI_FEATURE feature;
    bool enable;
    __u64 dtb;
    bool in;
    bool out;
};

struct kvm_vmi_feature_lbr {
    enum KVM_VMI_FEATURE feature;
    bool enable;
    __u64 lbr_select;
};

struct kvm_vmi_feature_mtf {
    enum KVM_VMI_FEATURE feature;
    bool enable;
};

union kvm_vmi_feature {
    enum KVM_VMI_FEATURE feature;
    struct kvm_vmi_feature_task_switch ts;
    struct kvm_vmi_feature_lbr lbr;
    struct kvm_vmi_feature_mtf mtf;
};

enum KVM_VMI_EVENT {
    KVM_VMI_EVENT_TASK_SWITCH = 0,
    KVM_VMI_EVENT_DEBUG,
    KVM_VMI_EVENT_MTF,
    KVM_VMI_EVENT_MAX
};

struct kvm_vmi_event_task_switch {
    enum KVM_VMI_EVENT type;
    __u64 old_cr3;
    __u64 new_cr3;
};

struct kvm_vmi_event_debug {
    enum KVM_VMI_EVENT type;
    bool single_step;
    bool watchpoint;
    __u64 watchpoint_gva;
    int32_t watchpoint_flags;
    int32_t exception;
};

union kvm_vmi_event {
    enum KVM_VMI_EVENT type;
    struct kvm_vmi_event_task_switch ts;
    struct kvm_vmi_event_debug debug;
};

struct kvm_vmi_lbr_info {
    __u32 entries;
    __u8 tos;
    __u64 from[MAX_LBR_ENTRIES];
    __u64 to[MAX_LBR_ENTRIES];
};

#define KVM_VMI_FEATURE_UPDATE    _IOW(KVMIO, 0xc0, union kvm_vmi_feature)
#define KVM_VMI_GET_LBR           _IOW(KVMIO, 0xc2, struct kvm_vmi_lbr_info)

#define KVM_EXIT_VMI_EVENT        1337



#endif /* __LINUX_KVM_VMI_H */


