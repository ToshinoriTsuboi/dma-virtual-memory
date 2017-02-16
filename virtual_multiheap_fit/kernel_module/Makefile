obj-m := vmf_module.o
vmf_module-objs := main.o page_manage.o address_vector.o kvmalloc.o
KERNEL_VERSION=$(shell uname -r)

all:
	make -C /lib/modules/$(KERNEL_VERSION)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(KERNEL_VERSION)/build M=$(PWD) clean

insmod: all
	sudo ./insmod.sh

rmmod:
	sudo rmmod vmf_module
