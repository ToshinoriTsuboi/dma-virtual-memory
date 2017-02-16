#!/bin/sh
module="vmf_module"
ko_file=${module}.ko
device_name="vmf_module"
device_path=/dev/${device_name}
# readable and writable without administrator authority
mode="666"
device_list=/proc/devices

/sbin/insmod ./${ko_file} $* || exit 1

rm -f ${device_path}?

# get major number of the module
major=$(cat ${device_list} | grep ${module} | awk '{print $1}')

if [ -z "${major}" ]; then
  exit 1
fi

mknod ${device_path}0 c ${major} 0

group="staff"
grep -q '^staff:' /etc/group | group="wheel"

chgrp $group ${device_path}?
chmod $mode  ${device_path}?
