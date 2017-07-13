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

#ifndef VMI_H
#define VMI_H
#include "qemu/osdep.h"

#include <linux/kvm_vmi.h>

#include "sysemu/kvm.h"
#include "qemu-common.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "qmp-commands.h"
#include "exec/cpu-common.h"
#include "migration/migration.h"
#include "qapi-types.h"
#include "sysemu/sysemu.h"

#include "vmi_bp.h"

typedef enum {
    PAUSED = 0,
    RUNNING,
    MAX_VMI_VM_STATE
} vmi_vm_state_t;

typedef struct {
    bool initalized;
    vmi_vm_state_t vm_state;
    CPUState *cpu;

    VMChangeStateEntry *vm_change_state_entry;

    //event queue
    GQueue *event_queue;
    QemuMutex event_mutex;

    //task switch hashtable
    GHashTable *ts_hashtable;

    //bp related state
    GHashTable *bp_hashtable;

    //ss pending
    CPUState *ss_pending_cpu;
    CPUState *ss_finished_cpu;
} vmi_state_t;

extern vmi_state_t vmi_state;

static inline bool vmi_initialized(void){
    return vmi_state.initalized;
}

static inline vmi_vm_state_t vmi_get_vm_state(void)
{
    return vmi_state.vm_state;
}

static inline CPUState* vmi_pending_ss(void)
{
    return vmi_state.ss_pending_cpu;
}

void vmi_add_event(CPUState *cpu);
CPUState* vmi_get_event(void);
uint64_t vmi_get_num_events(void);

void vmi_handle_vmi(void);
void vmi_handle_running(void);

int vmi_get_mem_info(MemInfo *mem_info);
int vmi_kvm_feature_task_switch(bool enable, uint64_t dtb, bool in, bool out);
int vmi_kvm_feature_lbr(bool enable, uint64_t select);
bool vmi_need_stop(CPUState *cpu, RunState state);
void vmi_initialize(void);
void vmi_uninitialize(void);

#endif
