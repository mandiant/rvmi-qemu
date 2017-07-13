#!/usr/bin/env python

###############################################################################
# QEMU start script
#
# Copyright (C) 2017 FireEye, Inc. All Rights Reserved.
#
# Authors:
#  Jonas Pfoh      <jonas.pfoh@fireeye.com>
#  Sebastian Vogl  <sebastian.vogl@fireeye.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation.
# Version 2 of the License.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.
# See the COPYING file in the top-level directory.
###############################################################################

from __future__ import print_function

import sys
import os

from pprint import pprint

from argparse import ArgumentParser

import subprocess
import errno
import time
import random
import tempfile

import qmp

QEMU_BIN = "/usr/local/bin/qemu-system-x86_64"
DEFAULT_MEMORY = 2048

class KVM(object):
    def __init__(self,disk_image=None,cpu_cores=2,memory=DEFAULT_MEMORY,qmp_path=None):
        if(disk_image is None):
            raise ValueError("must specifiy disk_image")

        self.cpu_cores = int(cpu_cores)
        self.memory = int(memory)
        self.disk_image = disk_image

        if qmp_path != None:
            self.qmp_path = qmp_path
        else:
            self.qmp_path = "/tmp/qmp."+str(os.getpid())+"."+str(random.randint(0,10000))

        self.child = None
        self.memfile = tempfile.mkstemp(prefix="vmi_", suffix=".mem")[1]
        self.qmp = None

    def get_qmp(self):
        if(self.qmp is None):
            t = time.time()
            to = 5.0
            self.qmp = qmp.QEMUMonitorProtocol(self.qmp_path)

            while True:
                try:
                    self.qmp.connect()
                except socket.error as e:
                    if(e.errno == errno.ENOENT):
                        if(time.time() - t >= to):
                            raise
                        time.sleep(0.2)
                        continue
                    else:
                        raise
                break
        return self.qmp

    def start(self,gdb=False,wait=True,qemu_args=[],vnc_display=None,snapshot=None,stdin=None,
              stdout=None,stderr=None,monitor=None,network=True,mem_filebacked=False,verbose=False):
        if(self.child is not None):
            raise RuntimeError("child process already running")

        if(gdb):
            cmd =  ["gdb","--args",QEMU_BIN,"-enable-kvm"]
        else:
            cmd =  [QEMU_BIN,"-enable-kvm"]
        cmd += ["-hda",self.disk_image]
        cmd += ["-smp",str(self.cpu_cores)]
        cmd += ["-m",str(self.memory)]

        if mem_filebacked:
            cmd += ["-object", "memory-backend-file,id=vmi,size="+str(self.memory * 1024 * 1024)+",mem-path="+str(self.memfile)+",share=on", "-mem-prealloc"]
            cmd += ["-numa", "node,memdev=vmi"]

        cmd += ["-qmp","unix:"+self.qmp_path+",server,nowait"]

        if(not network):
            cmd += ["-net", "none"]

        #use 'savevm NAME' from monitor to save snapshot
        if(snapshot is not None):
            cmd += ["-loadvm",snapshot]

        if(vnc_display is not None):
            cmd += ["-display","vnc=:"+str(vnc_display)]

        if(monitor is not None):
            cmd += ["-monitor",monitor]

        cmd += qemu_args

        if verbose:
            print("Executing qemu with the following parameters:")
            print(cmd)

        self.child = subprocess.Popen(cmd,stdin=stdin,stdout=stdout,stderr=stderr)

        if(wait):
            self.child.wait()
            if(self.qmp is not None):
                self.qmp.close()
            self.qmp = None
            self.child = None

    def stop(self):
        if(self.child is not None):
            try:
                self.get_qmp()
            except:
                self.child.terminate()
            else:
                self.qmp.cmd("quit")
                self.qmp.close()
                t = time.time()
                while self.child.poll() is None:
                    if(time.time() - t >= 5.0):
                        self.child.terminate()
                        break
                    time.sleep(0.2)
        self.qmp = None
        self.child = None
        os.remove(self.memfile)

def main(argv):
    aparser = ArgumentParser(description="Qemu/KVM start script")
    aparser.add_argument("disk_image", metavar="DISK_IMG", help="The disk image you want to load")
    aparser.add_argument("--memory", "-m", dest="memory", type=int, default=DEFAULT_MEMORY, help="The amount of RAM to llocate to the VM")
    aparser.add_argument("--not-filebacked", "-f", dest="mem_filebacked", default=True, action='store_false', help="Whether to create filebacked memory. This will be faster for local debugging. Enabled by default.")
    aparser.add_argument("--gdb", "-g", dest="gdb", default=False, action='store_true', help="This flag will allow you to start QEMU in a gdb session.")
    aparser.add_argument("--snapshot", "-s", dest="snapshot", default=None, help="This option allows you to start from a snapshot stred in the image.")
    aparser.add_argument("--network", "-n", dest="network", default=False, action='store_true', help="Enable NATed network.")
    aparser.add_argument("--path", "-p", dest="qmp_path", default=None, help="Specify the name of the QMP socekt that will be created.")
    aparser.add_argument("--verbose", "-v", dest="verbose", default=False, action='store_true', help="Enable verbose mode.")
    args, qemu_args = aparser.parse_known_args()

    kvm = KVM(args.disk_image,memory=args.memory,qmp_path=args.qmp_path)
    print("\nQMP socket created @ {0}".format(kvm.qmp_path))
    print("To connect use: rekal -f {0}\n".format(kvm.qmp_path))
    kvm.start(gdb=args.gdb,wait=True,snapshot=args.snapshot,network=args.network,mem_filebacked=args.mem_filebacked,monitor="stdio",verbose=args.verbose,qemu_args=qemu_args)
    kvm.stop()

if __name__ == "__main__":
    sys.exit(main(sys.argv))

