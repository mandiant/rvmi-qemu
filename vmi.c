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
 */

#include "qemu/osdep.h"

#include <linux/kvm_vmi.h>

#include "sysemu/kvm.h"
#include "qemu-common.h"
#include "qapi-types.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "qmp-commands.h"
#include "exec/cpu-common.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "exec/ram_addr.h"
#include "migration/migration.h"
#include "qom/cpu.h"
#include "qapi-types.h"
#include "qapi-event.h"
#include "qemu/rcu_queue.h"

#include "vmi.h"

vmi_state_t vmi_state = {0};

/*
static void print_event_add(CPUState *cpu){
    struct kvm_vmi_event_debug *event_dbg;

    if(cpu->vmi_event->type == KVM_VMI_EVENT_TASK_SWITCH){
        //printf("%s: cpu %d: adding event KVM_VMI_EVENT_TASK_SWITCH\n",__func__,cpu->cpu_index);
    }
    else if(cpu->vmi_event->type == KVM_VMI_EVENT_DEBUG){
        event_dbg = (struct kvm_vmi_event_debug*)cpu->vmi_event;
        if(event_dbg->single_step){
            printf("%s: cpu %d: adding event KVM_VMI_EVENT_DEBUG - SS\n",__func__,cpu->cpu_index);
        }
        else if(event_dbg->watchpoint){
            printf("%s: cpu %d: adding event KVM_VMI_EVENT_DEBUG - WATCHPOINT\n",__func__,cpu->cpu_index);
        }
        else{
            printf("%s: cpu %d: adding event KVM_VMI_EVENT_DEBUG - BREAKPOINT\n",__func__,cpu->cpu_index);
        }
    }
    else if(cpu->vmi_event->type == KVM_VMI_EVENT_MTF){
        printf("%s: cpu %d: adding event KVM_VMI_EVENT_MTF\n",__func__,cpu->cpu_index);
    }
    else{
        printf("%s: unexpected event!\n",__func__);
    }
}
*/

void vmi_add_event(CPUState *cpu)
{
    if(vmi_initialized()){
        qemu_mutex_lock(&(vmi_state.event_mutex));
        if(cpu->vmi_event->type == KVM_VMI_EVENT_MTF){
            assert(vmi_state.ss_pending_cpu == cpu);
            vmi_state.ss_finished_cpu = cpu;
        }
        else{
            g_queue_push_tail(vmi_state.event_queue,cpu);
        }
        qemu_mutex_unlock(&(vmi_state.event_mutex));
    }
}

CPUState* vmi_get_event(void)
{
    CPUState *rv = NULL;

    if(vmi_initialized()){
        qemu_mutex_lock(&(vmi_state.event_mutex));
        if(vmi_state.ss_pending_cpu){
            rv = vmi_state.ss_finished_cpu;
        }
        else{
            rv = g_queue_pop_head(vmi_state.event_queue);
        }
        qemu_mutex_unlock(&(vmi_state.event_mutex));
    }

    return rv;
}

uint64_t vmi_get_num_events(void)
{
    uint64_t rv = 0;

    if(vmi_initialized()){
        qemu_mutex_lock(&(vmi_state.event_mutex));
        if(vmi_state.ss_pending_cpu){
            if(vmi_state.ss_finished_cpu){
                rv = 1;
            }
        }
        else{
            rv = g_queue_get_length(vmi_state.event_queue);
        }
        qemu_mutex_unlock(&(vmi_state.event_mutex));
    }

    return rv;
}

typedef struct {
    uint64_t dtb;
    bool in;
    bool out;
}ts_hashtable_entry_t;

static int vmi_ts_hashtable_add_update(uint64_t dtb, bool in, bool out){
    ts_hashtable_entry_t *entry;

    entry = (ts_hashtable_entry_t*) g_malloc0(sizeof(ts_hashtable_entry_t));
    if(!entry)
        return -1;

    entry->dtb = dtb;
    entry->in = in;
    entry->out = out;

    g_hash_table_insert(vmi_state.ts_hashtable,(gpointer)dtb,entry);

    return 0;
}

static bool vmi_ts_need_stop(uint64_t cr3_out, uint64_t cr3_in){
    ts_hashtable_entry_t *entry;

    entry = g_hash_table_lookup(vmi_state.ts_hashtable,(gconstpointer)cr3_out);
    if(entry && entry->out){
        return true;
    }

    entry = g_hash_table_lookup(vmi_state.ts_hashtable,(gconstpointer)cr3_in);
    if(entry && entry->in){
        return true;
    }

    return false;
}

static void vmi_handle_event(CPUState *cpu)
{
    union kvm_vmi_event *event = cpu->vmi_event;
    struct kvm_vmi_event_task_switch *event_ts;
    struct kvm_vmi_event_debug *event_dbg;

    switch (event->type) {
    case KVM_VMI_EVENT_TASK_SWITCH:
        //printf("%s: recieved KVM_VMI_EVENT_TASK_SWITCH on cpu %d\n",__func__,cpu->cpu_index);
        event_ts = (struct kvm_vmi_event_task_switch *)event;

        if(vmi_ts_need_stop(event_ts->old_cr3, event_ts->new_cr3)){
            qapi_event_send_vmi_task_switch(cpu->cpu_index,event_ts->old_cr3,
                                            event_ts->new_cr3, &error_abort);
            vmi_state.vm_state = PAUSED;
        }
        else{
            vmi_bp_refresh_bps(cpu,event_ts->old_cr3,event_ts->new_cr3);
            vmi_state.vm_state = RUNNING;
        }
        break;
    case KVM_VMI_EVENT_DEBUG:
        //printf("%s: recieved KVM_VMI_EVENT_DEBUG on cpu %d\n",__func__,cpu->cpu_index);
        event_dbg = (struct kvm_vmi_event_debug*)event;
        //vmi_bp handles debug and will set vm_state
        vmi_bp_handle_debug(cpu,event_dbg);
        break;
    case KVM_VMI_EVENT_MTF:
        //printf("%s: recieved KVM_VMI_EVENT_MTF on cpu %d\n",__func__,cpu->cpu_index);
        //vmi_bp handles mtf and will set vm_state
        vmi_bp_handle_mtf(cpu);
        break;
    default:
        assert(false);
        break;
    }
}

void vmi_handle_running(void)
{
    vmi_bp_handle_running(vmi_state.cpu);
    vmi_state.vm_state = RUNNING;
}

void vmi_handle_vmi(void){
    vmi_state.cpu = vmi_get_event();
    vmi_handle_event(vmi_state.cpu);

    if(vmi_state.vm_state != RUNNING){
        vmi_rm_all_bps();
    }
    //vmi_rm_all_bps();
}

static void vmi_vm_state_change(void *opaque, int running, RunState state)
{

    switch (state) {
    case RUN_STATE_DEBUG:
        assert(false);
        //printf("%s: RUN_STATE_DEBUG (%d)\n",__func__,running);
        break;
    case RUN_STATE_RUNNING:
        //printf("%s: RUN_STATE_RUNNING (%d)\n",__func__,running);
        vmi_handle_running();
        break;
    case RUN_STATE_VMI:
        //printf("%s: RUN_STATE_VMI (%d)\n",__func__,running);
        vmi_handle_vmi();
        break;
    case RUN_STATE_PAUSED:
        //printf("%s: RUN_STATE_PAUSED (%d)\n",__func__,running);
        vmi_state.cpu = qemu_get_cpu(0);
    case RUN_STATE_SHUTDOWN:
    case RUN_STATE_IO_ERROR:
    case RUN_STATE_WATCHDOG:
    case RUN_STATE_INTERNAL_ERROR:
    case RUN_STATE_SAVE_VM:
    case RUN_STATE_RESTORE_VM:
    case RUN_STATE_FINISH_MIGRATE:
    default:
        if(!running){
            vmi_state.vm_state = PAUSED;
            vmi_rm_all_bps();
        }
        break;
    }
}



void vmi_initialize(void){
    if(!vmi_initialized()){
        vmi_state.event_queue = g_queue_new();
        qemu_mutex_init(&(vmi_state.event_mutex));

        vmi_state.cpu = qemu_get_cpu(0);

        vmi_state.ts_hashtable = g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,g_free);

        vmi_bp_initialize();

        vmi_state.vm_change_state_entry = qemu_add_vm_change_state_handler(vmi_vm_state_change,NULL);

        vmi_state.initalized = true;
    }
}

void vmi_uninitialize(void){
    if(vmi_initialized()){
        vmi_state.initalized = false;

        qemu_del_vm_change_state_handler(vmi_state.vm_change_state_entry);

        vmi_bp_uninitialize();

        g_hash_table_destroy(vmi_state.ts_hashtable);

        qemu_mutex_destroy(&(vmi_state.event_mutex));
        g_queue_free(vmi_state.event_queue);

        memset(&vmi_state,0,sizeof(vmi_state_t));
    }
}

char *qmp_vmi_read_mem(uint64_t offset, uint64_t size, Error **errp)
{
    uint8_t *buf = NULL;
    char *rv = NULL;

    if (size == 0) {
        rv = g_malloc0(1);
        return rv;
    }

    buf = g_malloc0(size);
    if (buf == NULL) {
        error_setg(errp, "out of memory");
        return NULL;
    }
    cpu_physical_memory_read(offset, buf, size);
    rv = g_base64_encode(buf,size);
    g_free(buf);

    return rv;
}

void qmp_vmi_write_mem(const char *data, uint64_t offset, Error **errp)
{
    char *rv = NULL;
    size_t len;

    rv = (char *)g_base64_decode(data, &len);

    if (rv) {
    	cpu_physical_memory_write(offset, rv, len);
        g_free(rv);
    }
    else {
        error_setg(errp, "could not decode data");
    }
}

int vmi_get_mem_info(MemInfo *mem_info)
{
    RAMBlock *block;
    MemoryRegion *mr;
    char *path = NULL;

    // Is the memory file backed
    rcu_read_lock();
    QLIST_FOREACH_RCU(block, &ram_list.blocks, next) {
        mr = block->mr;

        if (strstr(memory_region_name(mr), "vmi") != NULL) {
            path = object_property_get_str(memory_region_owner(mr), "mem-path", NULL);

            if (path != NULL) {
                mem_info->filebacked = true;
                mem_info->size = memory_region_size(mr);
                mem_info->path = path;

                return 0;
            }
        }
    }
    rcu_read_unlock();

    mem_info->filebacked = false;
    mem_info->size = ram_bytes_total();
    mem_info->path = NULL;

    return 0;
}


VMInfo *qmp_vmi_get_vminfo(Error **errp)
{
    VMInfo *rv = g_malloc0(sizeof(VMInfo));
    MemInfo *mem = g_malloc0(sizeof(MemInfo));
    CpuInfoList *cpu_list = NULL;
    CpuInfoList *cpu = NULL;
    uint32_t num_cpus = 0;

    if (rv == NULL) {
        error_setg(errp, "out of memory");
        return NULL;
    }

    cpu_list = qmp_query_cpus(NULL);
    for (cpu = cpu_list; cpu; cpu = cpu->next) {
        num_cpus++;
    }

    rv->num_cpus = num_cpus;
    rv->mem_info = mem;
    vmi_get_mem_info(rv->mem_info);

    return rv;
}

static inline void free_and_unset_X86VCPUState(X86VCPUState **vcpu_state)
{
    if(*vcpu_state)
        qapi_free_X86VCPUState(*vcpu_state);
    *vcpu_state = NULL;
}

X86VCPUState *qmp_vmi_get_cpu_state(uint32_t cpu_num, Error **errp){
    CPUState *cpu = NULL;
    X86VCPUState *vcpu_state = NULL;
    CPUClass *cc = NULL;

    cpu = qemu_get_cpu(cpu_num);
    if(cpu == NULL){
        error_setg(errp, "unable to get cpu");
        return NULL;
    }

    cpu_synchronize_state(cpu);

    cc = CPU_GET_CLASS(cpu);
    if(cc == NULL){
        error_setg(errp, "unable to get cpu class");
        return NULL;
    }

    if(cc->cpu_get_state == NULL){
        error_setg(errp, "not implemented");
        return NULL;
    }

    vcpu_state = g_malloc0(sizeof(X86VCPUState));
    if(vcpu_state == NULL){
        error_setg(errp, "out of memory");
        return NULL;
    }

    vcpu_state->es = g_malloc0(sizeof(X86Segment));
    if(vcpu_state->es == NULL){
        error_setg(errp, "out of memory");
        free_and_unset_X86VCPUState(&vcpu_state);
        return NULL;
    }

    vcpu_state->cs = g_malloc0(sizeof(X86Segment));
    if(vcpu_state->cs == NULL){
        error_setg(errp, "out of memory");
        free_and_unset_X86VCPUState(&vcpu_state);
        return NULL;
    }

    vcpu_state->ss = g_malloc0(sizeof(X86Segment));
    if(vcpu_state->ss == NULL){
        error_setg(errp, "out of memory");
        free_and_unset_X86VCPUState(&vcpu_state);
        return NULL;
    }

    vcpu_state->ds = g_malloc0(sizeof(X86Segment));
    if(vcpu_state->ds == NULL){
        error_setg(errp, "out of memory");
        free_and_unset_X86VCPUState(&vcpu_state);
        return NULL;
    }

    vcpu_state->fs = g_malloc0(sizeof(X86Segment));
    if(vcpu_state->fs == NULL){
        error_setg(errp, "out of memory");
        free_and_unset_X86VCPUState(&vcpu_state);
        return NULL;
    }

    vcpu_state->gs = g_malloc0(sizeof(X86Segment));
    if(vcpu_state->gs == NULL){
        error_setg(errp, "out of memory");
        free_and_unset_X86VCPUState(&vcpu_state);
        return NULL;
    }

    vcpu_state->ldt = g_malloc0(sizeof(X86Segment));
    if(vcpu_state->ldt == NULL){
        error_setg(errp, "out of memory");
        free_and_unset_X86VCPUState(&vcpu_state);
        return NULL;
    }

    vcpu_state->tr = g_malloc0(sizeof(X86Segment));
    if(vcpu_state->tr == NULL){
        error_setg(errp, "out of memory");
        free_and_unset_X86VCPUState(&vcpu_state);
        return NULL;
    }

    vcpu_state->gdt = g_malloc0(sizeof(X86Segment));
    if(vcpu_state->gdt == NULL){
        error_setg(errp, "out of memory");
        free_and_unset_X86VCPUState(&vcpu_state);
        return NULL;
    }

    vcpu_state->idt = g_malloc0(sizeof(X86Segment));
    if(vcpu_state->idt == NULL){
        error_setg(errp, "out of memory");
        free_and_unset_X86VCPUState(&vcpu_state);
        return NULL;
    }

    cc->cpu_get_state(cpu,vcpu_state);
    return vcpu_state;
}

void qmp_vmi_set_cpu_state(uint32_t cpu_num, X86VCPUState *state, Error **errp){
    CPUState *cpu = NULL;
    CPUClass *cc = NULL;

    cpu = qemu_get_cpu(cpu_num);
    if(cpu == NULL){
        error_setg(errp, "unable to get cpu");
        return;
    }

    cc = CPU_GET_CLASS(cpu);
    if(cc == NULL){
        error_setg(errp, "unable to get cpu class");
        return;
    }

    cc->cpu_set_state(cpu, state);

    cpu->kvm_vcpu_dirty = true;
}



int vmi_kvm_feature_task_switch(bool enable, uint64_t dtb, bool in, bool out)
{
    union kvm_vmi_feature feature;
    feature.feature = KVM_VMI_FEATURE_TRAP_TASK_SWITCH;
    feature.ts.enable = enable;
    feature.ts.dtb = dtb;
    feature.ts.in = in;
    feature.ts.out = out;

    return kvm_vmi_vcpu_ioctl_all(KVM_VMI_FEATURE_UPDATE, &feature);
}

int vmi_kvm_feature_lbr(bool enable, uint64_t select)
{
    union kvm_vmi_feature feature;
    feature.feature = KVM_VMI_FEATURE_LBR;
    feature.lbr.enable = enable;
    feature.lbr.lbr_select = select;

    return kvm_vmi_vcpu_ioctl_all(KVM_VMI_FEATURE_UPDATE, &feature);
}

void qmp_vmi_task_switch(bool enable, uint64_t dtb, bool in, bool out, Error **errp)
{
    int r;

    if ((r = vmi_kvm_feature_task_switch(enable,dtb,in,out))) {
        error_setg(errp, "an error occurred while enabling task switch trap (%d)", r);
        return;
    }

    if(enable){
        if(vmi_ts_hashtable_add_update(dtb,in,out)){
            error_setg(errp, "an error occurred while adding entry to ts hash table");
            return;
        }
    }
    else{
        g_hash_table_remove(vmi_state.ts_hashtable,(gconstpointer)dtb);
    }
}

void qmp_vmi_lbr(bool enable, uint64_t select, Error **errp)
{
    int r;

    if ((r = vmi_kvm_feature_lbr(enable, select))) {
        error_setg(errp,
                   "an error occurred while enabling the lbr (%d)", r);
        return;
    }
}

VMILBRInfo *qmp_vmi_get_lbr(uint32_t cpu_id, Error **errp)
{
    int r;
    unsigned i;
    struct kvm_vmi_lbr_info info;
    VMILBRInfo *rv = NULL;
    uint64List *cur;
    uint64List **last;

    if (!(rv = g_malloc0(sizeof(VMILBRInfo)))) {
        error_setg(errp, "out of memory");
        return NULL;
    }

    if ((r = kvm_vmi_vcpu_ioctl_single(cpu_id, KVM_VMI_GET_LBR, &info))) {
        error_setg(errp,
                   "an error occurred while getting the lbr (%d)", r);
	    g_free(rv);
    }

    rv->entries = info.entries;
    rv->tos = info.tos;

    // Fill in from and to values
    last = &rv->from;
    for (i = 0; i < info.entries; i++) {
        if (!(cur = g_malloc0(sizeof(uint64List)))) {
            error_setg(errp, "out of memory");
	        qapi_free_uint64List(rv->from);
	        g_free(rv);
            return NULL;
        }

	    cur->value = info.from[i];
	    (*last) = cur;
	    last = &cur->next;
    }

    last = &rv->to;
    for (i = 0; i < info.entries; i++) {
        if (!(cur = g_malloc0(sizeof(uint64List)))) {
            error_setg(errp, "out of memory");
	        qapi_free_uint64List(rv->from);
	        qapi_free_uint64List(rv->to);
	        g_free(rv);
            return NULL;
        }

	    cur->value = info.to[i];
	    (*last) = cur;
	    last = &cur->next;
    }

    return rv;
}
