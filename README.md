![rVMI Logo](/resources/rvmi-qemu.png)

# rVMI - QEMU

This is a fork of QEMU that includes the **rVMI** extensions.

In the following, we will provide a brief overview of rVMI with a focus
on the QEMU extensions. If you are looking for the main rVMI repository
please go to <https://github.com/fireeye/rvmi/>.

If you are interested in QEMU go to <http://qemu-project.org/>
or take a look at the QEMU section below.

## About

rVMI is a debugger on steroids. It leverages Virtual Machine Introspection (VMI)
and memory forensics to provide full system analysis. This means that an analyst
can inspect userspace processes, kernel drivers, and preboot environments in a
single tool.

It was specifially designed for interactive dynamic malware analysis. rVMI isolates
itself from the malware by placing its interactive debugging environment out of the
virtual machine (VM) onto the hypervisor-level. Through the use of VMI the analyst
still has full control of the VM, which allows her to pause the VM at any point in
time and to use typical debugging features such as breakpoints and watchpoints. In
addtion, rVMI provides access to the entire Rekall feature set, which enables an
analyst to inspect the kernel and its data structures with ease.

## Installing QEMU with rVMI

Before installing QEMU with rVMI, we recommend that you remove any previously
installed versions of QEMU.

Begin by cloning the repository:

```
$ git clone https://github.com/fireeye/rvmi-qemu.git rvmi-qemu
```

Then, simply configure, compile, and install.

```
$ cd rvmi-qemu
$ ./configure --target-list=x86_64-softmmu
$ make
$ sudo make install
```

## Using rVMI

To run rVMI please follow the instructions located at <https://github.com/fireeye/rvmi/>.

## QEMU

QEMU is a generic and open source machine & userspace emulator and
virtualizer.

QEMU is capable of emulating a complete machine in software without any
need for hardware virtualization support. By using dynamic translation,
it achieves very good performance. QEMU can also integrate with the Xen
and KVM hypervisors to provide emulated hardware while allowing the
hypervisor to manage the CPU. With hypervisor support, QEMU can achieve
near native performance for CPUs. When QEMU emulates CPUs directly it is
capable of running operating systems made for one machine (e.g. an ARMv7
board) on a different machine (e.g. an x86_64 PC board).

QEMU is also capable of providing userspace API virtualization for Linux
and BSD kernel interfaces. This allows binaries compiled against one
architecture ABI (e.g. the Linux PPC64 ABI) to be run on a host using a
different architecture ABI (e.g. the Linux x86_64 ABI). This does not
involve any hardware emulation, simply CPU and syscall emulation.

QEMU aims to fit into a variety of use cases. It can be invoked directly
by users wishing to have full control over its behaviour and settings.
It also aims to facilitate integration into higher level management
layers, by providing a stable command line interface and monitor API.
It is commonly invoked indirectly via the libvirt library when using
open source applications such as oVirt, OpenStack and virt-manager.

## Licensing and Copyright

Copyright 2017 FireEye, Inc. All Rights Reserved.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation. Version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.

The following points clarify the QEMU licenses:

1. QEMU as a whole is released under the GNU General Public License, version 2.
2. Parts of QEMU have specific licenses which are compatible with the GNU General Public License, version 2. Hence each source file contains its own licensing information. Source files with no licensing information are released under the GNU General Public License, version 2 or (at your option) any later version. As of July 2013, contributions under version 2 of the GNU General Public License (and no later version) are only accepted for the following files or directories: bsd-user/, linux-user/, hw/misc/vfio.c, hw/xen/xen_pt*.
3. The Tiny Code Generator (TCG) is released under the BSD license (see license headers in files).
4. QEMU is a trademark of Fabrice Bellard.


## Bugs and Support

There is no support provided. There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.

If you think you've found a bug particular to rvmi-qemu, please report it at:

https://github.com/fireeye/rvmi-qemu/issues

In order to help us solve your issues as quickly as possible,
please include the following information when filing a bug:

* The version of rvmi-qemu you're using
* The guest operating system you are analyzing
* The complete command line you used to run rvmi-qemu
* The exact steps required to reproduce the issue

If you think you have found a bug in one of the other rvmi components, please report appropriately:

https://github.com/fireeye/rvmi-kvm/issues  
https://github.com/fireeye/rvmi-rekall/issues

If you are not sure or would like to file a general bug, please report here:

https://github.com/fireeye/rvmi/issues

## More documentation

Further documentation is available at
https://github.com/fireeye/rvmi/
