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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "cpu.h"

#include "qemu-common.h"
#include "sysemu/kvm.h"
#include "qapi-types.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "qmp-commands.h"
#include "exec/cpu-common.h"
#include "migration/migration.h"
#include "qom/cpu.h"
#include "qapi-types.h"
#include "qapi-event.h"
#include "exec/gdbstub.h"
#include "exec/exec-all.h"
#include "sysemu/sysemu.h"

#include "vmi.h"
#include "vmi_bp.h"

typedef struct {
    uint64_t dtb;
    uint64_t gva;
    uint64_t length;
    GdbBpType type;
} vmi_bp_t;

static void bp_hashtable_destroy(gpointer p)
{
    GHashTable *hash_table = p;
    g_hash_table_destroy(hash_table);
}

static void destroy_vmi_bp_t(gpointer p){
    vmi_bp_t *vmi_bp = p;
    g_free(vmi_bp);
}

//logical insert
static int vmi_insert_breakpoint(uint64_t dtb, uint64_t gva, uint64_t length, GdbBpType type){
    vmi_bp_t *vmi_bp = NULL;
    GHashTable *gva_ht = NULL;

    gva_ht = g_hash_table_lookup(vmi_state.bp_hashtable,(gconstpointer)dtb);

    if(gva_ht == NULL){
        gva_ht = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, destroy_vmi_bp_t);
        g_hash_table_insert(vmi_state.bp_hashtable,(gpointer)dtb,gva_ht);
    }

    vmi_bp = g_malloc0(sizeof(vmi_bp_t));

    vmi_bp->dtb = dtb;
    vmi_bp->gva = gva;
    vmi_bp->length = length;
    vmi_bp->type = type;

    g_hash_table_insert(gva_ht,(gpointer)gva,vmi_bp);

    return 0;
}

//logical remove
static int vmi_remove_breakpoint(uint64_t dtb, uint64_t gva){
    GHashTable *gva_ht = NULL;

    gva_ht = g_hash_table_lookup(vmi_state.bp_hashtable,(gconstpointer)dtb);
    if(gva_ht == NULL)
        return 0;

    g_hash_table_remove(gva_ht,(gconstpointer)gva);
    if(g_hash_table_size(gva_ht) == 0){
        g_hash_table_remove(vmi_state.bp_hashtable,(gconstpointer)dtb);
    }

    return 0;
}

static vmi_bp_t* vmi_lookup_breakpoint(uint64_t dtb, uint64_t gva){
    GHashTable *gva_ht = NULL;

    gva_ht = g_hash_table_lookup(vmi_state.bp_hashtable,(gconstpointer)dtb);
    if(gva_ht == NULL)
        return NULL;

    return g_hash_table_lookup(gva_ht, (gconstpointer)gva);
}

/*
static void vmi_num_breakpoints_each(gpointer key, gpointer value, gpointer user_data)
{
    GHashTable *gva_ht = value;
    uint32_t *count = user_data;

    *count += g_hash_table_size(gva_ht);
}

static uint32_t vmi_num_breakpoints(void)
{
    uint32_t rv = 0;
    g_hash_table_foreach(vmi_state.bp_hashtable,vmi_num_breakpoints_each,&rv);
    return rv;
}
*/

static vmi_bp_t* vmi_check_breakpoint(uint64_t dtb, uint64_t gva, GdbBpType type)
{
    vmi_bp_t *rv = NULL;

    rv = vmi_lookup_breakpoint(dtb,gva);

    if(rv && rv->type != type)
        rv = NULL;

    if(rv == NULL){
        rv = vmi_lookup_breakpoint(0,gva);

        if(rv && rv->type != type)
            rv = NULL;
    }

    return rv;
}

static int vmi_bp_mtf_ss(CPUState *cpu, bool enabled, bool need_stop){
    if(enabled){
        //do not allow both cpus to be single stepping
        if(vmi_state.ss_pending_cpu && vmi_state.ss_pending_cpu != cpu){
            return -1;
        }
        cpu->vmi_mtf_need_stop = need_stop;
        vmi_state.ss_pending_cpu = cpu;
    }
    else{
        cpu->vmi_mtf_need_stop = false;
        vmi_state.ss_pending_cpu = NULL;
    }
    vmi_state.ss_finished_cpu = NULL;
    cpu_mtf_single_step(cpu,enabled);
    return 0;
}

static uint64_t get_rip(uint32_t cpu_num)
{
    X86VCPUState *state = NULL;
    uint64_t rv = 0;

    state = qmp_vmi_get_cpu_state(cpu_num, NULL);
    if(state == NULL)
        return 0;

    rv = state->rip;
    qapi_free_X86VCPUState(state);
    return rv;
}

static uint64_t get_cr3(uint32_t cpu_num)
{
    X86VCPUState *state = NULL;
    uint64_t rv = 0;

    state = qmp_vmi_get_cpu_state(cpu_num, NULL);
    if(state == NULL)
        return 0;

    rv = state->cr3;
    qapi_free_X86VCPUState(state);
    return rv;
}

static void vmi_add_breakpoints_each(gpointer key, gpointer value, gpointer user_data){
    vmi_bp_t *vmi_bp = value;
    CPUState *cpu = user_data;
    kvm_insert_breakpoint(cpu, vmi_bp->gva, vmi_bp->length, vmi_bp->type);
}

static void vmi_remove_breakpoints_each(gpointer key, gpointer value, gpointer user_data){
    vmi_bp_t *vmi_bp = value;
    CPUState *cpu = user_data;
    kvm_remove_breakpoint(cpu, vmi_bp->gva, vmi_bp->length, vmi_bp->type);
}

static int vmi_add_breakpoints(CPUState *cpu){
    GHashTable *gva_ht = NULL;
    CPUState *cpu_i = NULL;
    uint64_t cr3 = 0;

    gva_ht = g_hash_table_lookup(vmi_state.bp_hashtable,0);
    if(gva_ht){
        g_hash_table_foreach(gva_ht,vmi_add_breakpoints_each,cpu);
    }

    //foreach cpu get cr3
    CPU_FOREACH(cpu_i){
        cr3 = get_cr3(cpu_i->cpu_index);
        if(cr3 == 0)
            continue;

        gva_ht = g_hash_table_lookup(vmi_state.bp_hashtable,(gconstpointer)cr3);
        if(gva_ht){
            g_hash_table_foreach(gva_ht,vmi_add_breakpoints_each,cpu_i);
        }
    }

    return 0;
}

void vmi_rm_all_bps(void){
    CPUState *cpu_i = NULL;

    CPU_FOREACH(cpu_i){
        kvm_remove_all_breakpoints(cpu_i);
    }
}

static bool vmi_bp_dtb_has_set(uint64_t dtb){
    GHashTable *gva_ht = NULL;
    gva_ht = g_hash_table_lookup(vmi_state.bp_hashtable,(gconstpointer)dtb);
    if(gva_ht)
        return true;
    return false;
}

void vmi_bp_refresh_bps(CPUState *cpu, uint64_t prev_cr3, uint64_t cur_cr3)
{
    GHashTable *gva_ht = NULL;

    //remove old
    gva_ht = g_hash_table_lookup(vmi_state.bp_hashtable,(gconstpointer)prev_cr3);
    if(gva_ht){
        cpu_synchronize_state(cpu);
        g_hash_table_foreach(gva_ht,vmi_remove_breakpoints_each,cpu);
    }

    //add new
    gva_ht = g_hash_table_lookup(vmi_state.bp_hashtable,(gconstpointer)cur_cr3);
    if(gva_ht){
        cpu_synchronize_state(cpu);
        g_hash_table_foreach(gva_ht,vmi_add_breakpoints_each,cpu);
    }

}

static void vmi_bp_handle_ss(CPUState *cpu)
{
    if(cpu->vmi_mtf_need_stop){
        qapi_event_send_vmi_ss(cpu->cpu_index,&error_abort);
        vmi_state.vm_state = PAUSED;
    }
    else{
        vmi_state.vm_state = RUNNING;
        vmi_add_breakpoints(cpu);
    }
    vmi_bp_mtf_ss(cpu,false,false);
}

void vmi_bp_handle_mtf(CPUState *cpu)
{
    vmi_bp_handle_ss(cpu);
}

void vmi_bp_handle_debug(CPUState *cpu, struct kvm_vmi_event_debug *event_dbg)
{
    GdbBpType type = 0;
    uint64_t gva = 0;
    uint64_t cr3 = 0;
    vmi_bp_t *vmi_bp = NULL;

    if(event_dbg->single_step){
        assert(false);
    }
    else if(event_dbg->watchpoint){
        switch (event_dbg->watchpoint_flags & BP_MEM_ACCESS) {
        case BP_MEM_READ:
            type = GDB_WATCHPOINT_READ;
            break;
        case BP_MEM_ACCESS:
            type = GDB_WATCHPOINT_ACCESS;
            break;
        default:
            type = GDB_WATCHPOINT_WRITE;
            break;
        }
        cr3 = get_cr3(cpu->cpu_index);
        gva = event_dbg->watchpoint_gva;
        vmi_bp = vmi_check_breakpoint(cr3,gva,type);
        if(vmi_bp){
            qapi_event_send_vmi_bp(cpu->cpu_index,gva,type,&error_abort);
            vmi_state.vm_state = PAUSED;
        }
        else{
            vmi_state.vm_state = RUNNING;
        }
        vmi_bp_mtf_ss(cpu,true,false);
    }
    else{
        tb_flush(cpu);
        cr3 = get_cr3(cpu->cpu_index);
        gva = get_rip(cpu->cpu_index);
        vmi_bp = vmi_check_breakpoint(cr3,gva,GDB_BREAKPOINT_SW);
        if(vmi_bp){
            qapi_event_send_vmi_bp(cpu->cpu_index,gva,GDB_BREAKPOINT_SW,&error_abort);
            vmi_state.vm_state = PAUSED;
        }
        else{
            vmi_state.vm_state = RUNNING;
        }
        vmi_bp_mtf_ss(cpu,true,false);
    }
}


void vmi_bp_handle_running(CPUState *cpu){
    if(vmi_state.vm_state != RUNNING && !vmi_state.ss_pending_cpu){
        vmi_add_breakpoints(cpu);
    }
    else if(vmi_state.ss_pending_cpu){
        vmi_rm_all_bps();
    }
    /*
    if(!vmi_state.ss_pending_cpu){
        vmi_add_breakpoints(cpu);
    }
    */
}

void qmp_vmi_bp(VmiBpAction action, uint64_t dtb, uint64_t gva, uint64_t length, GdbBpType type, Error **errp)
{
    gva = (uint64_t)(((int64_t)gva << 16) >> 16);

    switch(action){
    case VMI_BP_ACTION_ADD:
        if(dtb > 0 && !vmi_bp_dtb_has_set(dtb)){
            vmi_kvm_feature_task_switch(true,dtb,true,true);
        }
        if(vmi_insert_breakpoint(dtb,gva,length,type)){
            error_setg(errp, "insert failed");
            return;
        }
        break;
    case VMI_BP_ACTION_REMOVE:
        if(vmi_remove_breakpoint(dtb,gva)){
            error_setg(errp, "remove failed");
            return;
        }
        if(dtb > 0 && !vmi_bp_dtb_has_set(dtb)){
            vmi_kvm_feature_task_switch(false,dtb,false,false);
        }
        break;
    default:
        error_setg(errp, "action unknown");
        return;
    }
}

void qmp_vmi_ss(uint32_t cpu_id, Error **errp){
    CPUState *cpu = qemu_get_cpu(cpu_id);

    if(vmi_bp_mtf_ss(cpu,true,true)){
        error_setg(errp, "error setting single step");
    }
}

void vmi_bp_initialize(void)
{
    vmi_state.bp_hashtable = g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,bp_hashtable_destroy);
}

void vmi_bp_uninitialize(void)
{
    vmi_rm_all_bps();
    g_hash_table_destroy(vmi_state.bp_hashtable);
    vmi_state.bp_hashtable = NULL;
}
