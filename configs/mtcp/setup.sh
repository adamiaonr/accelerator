# script to setup dpdk-1.8.0 + mTCP + Tor on the a virtual machine running 
# Ubuntu 14.04.2 LTS (Linux Kernel 3.13.0-46-lowlatency)
# *** WARNING: PRELIM. VERSION ***
# steps taken from: 
#	-# https://github.com/eunyoung14/mtcp
#   -# https://wiki.linaro.org/LNG/Engineering/OVSDPDKOnUbuntu
#   -# http://dpdk.org/doc/quick-start
#   -# http://plvision.eu/blog/deploying-intel-dpdk-in-oracle-virtualbox/
#   -# https://gist.github.com/ConradIrwin/9077440
# created by: antonior@andrew.cmu.edu

#!/bin/bash
MTCP_CONFIGS=/home/$USER/workbench/accelerator/configs/mtcp
MTCP_HOME=/home/$USER/workbench/accelerator/lib/mtcp
MTCP_DPDK=$MTCP_HOME/dpdk-1.8.0
MTCP_DPDK_RTE_TARGET_VERSION=x86_64-native-linuxapp-gcc
MTCP_DPDK_RTE_TARGET=$MTCP_DPDK/$MTCP_DPDK_RTE_TARGET_VERSION
MTCP_DPDK_NIC_BIND_TOOL=$MTCP_DPDK/tools/dpdk_nic_bind.py
MTCP_DPDK_BUILD_CONFIG=config

usage () {
    echo "usage: sudo ./setup.sh [[-n value OR --nr_hugepages value] [-d OR --build_dpdk] [-m OR --build_mtcp] ] | [-h]]"
}

HUGETLBFS_DIR="/mnt/huge"
HUGETLBFS_NR=320

BUILD_DPDK=0
BUILD_MTCP=0

while [ "$1" != "" ]; do
    
    case $1 in

        -n | --nr_hugepages )   shift
                                HUGETLBFS_NR=$1
                                ;;
        -d | --build_dpdk )     BUILD_DPDK=1
                                ;;
        -m | --build_mtcp )     BUILD_MTCP=1
                                ;;
        -t | --build_tor )	BUILD_TOR=1
				;;
	-h | --help	)	usage
        			exit
        			;;
        * )                     usage
                                exit 1
    esac
    shift

done

# 1) pre-requesites

# 1.1) check the linux kernel version (should be 3.13.0-46-lowlatency)

# 1.2) install necessary libs
sudo apt-get update
sudo apt-get install libnuma-dev build-essential linux-image-3.13.0-46-lowlatency linux-headers-3.13.0-46-lowlatency libevent-dev libssl-dev autoconf 

# 1.3) check that the accelerator directories exist

# 1.3.1) if the mTCP submodules aren't initialized, do it now
if [ ! -d $MTCP_DPDK ]; then
    
    cd $MTCP_HOME
    
    git submodule init
    git submodule update
fi

# 2) setup DPDK for i686 target (32 bit), w/ debugging activated
cd $MTCP_DPDK

if [ $BUILD_DPDK -eq 1 ]; then
    # 2.1) make sure previous installations are cleaneds
    make clean
    sudo make uninstall

    # 2.2) configure the DPDK build for the target i686 arch + activate debugging
    cp -r $MTCP_CONFIGS/files/dpdk-1.8.0/$MTCP_DPDK_BUILD_CONFIG/* $MTCP_DPDK/$MTCP_DPDK_BUILD_CONFIG/
    make config T=$MTCP_DPDK_RTE_TARGET_VERSION

    make
    sudo make install T=$MTCP_DPDK_RTE_TARGET_VERSION
fi

# 2.3) load dpdk's specialized kernel module to allow userspace apps to control 
# the network card
sudo modprobe uio
sudo insmod $MTCP_DPDK_RTE_TARGET/kmod/igb_uio.ko

# 2.4) mount hugetlbfs (still not entirely sure why this is necessary... 
# apparently to reserve huge pages memory for dpdk...)

# 2.4.1) 
sudo sysctl -w vm.nr_hugepages=$HUGETLBFS_NR
sudo sysctl vm.nr_hugepages

# 2.4.2) 
if [ ! -d "$HUGETLBFS_DIR" ]; then
    
    sudo mkdir -p $HUGETLBFS_DIR
fi

# 2.4.3) 
sudo mount -t hugetlbfs hugetlbfs $HUGETLBFS_DIR

# 2.5) bind eth0 and eth1 ifaces to dpdk

# 2.5.1) show the current 'binding' status of network cards (should be to 
# 'e1000')
$MTCP_DPDK_NIC_BIND_TOOL --status

# 2.5.2) --force the --bind of eth0 and eth1 ifaces to dpdk's igb_uio driver
#sudo $MTCP_DPDK_NIC_BIND_TOOL --force --bind=igb_uio eth0
sudo $MTCP_DPDK_NIC_BIND_TOOL --force --bind=igb_uio eth1

# 2.5.3) show new status of iface bindings
$MTCP_DPDK_NIC_BIND_TOOL --status

# 2.5.4) bring the dpdk-registered interfaces up
sudo $MTCP_DPDK/tools/setup_iface_single_process.sh 1

# 2.6) soft links for include/ and lib/ directories inside empty dpdk/ directory
cd $MTCP_HOME/dpdk 
sudo ln -s $MTCP_DPDK_RTE_TARGET/lib lib
sudo ln -s $MTCP_DPDK_RTE_TARGET/include include

# 3) setup mTCP lib
cd $MTCP_HOME

# 3.1) ./configure
if [ $BUILD_MTCP -eq 1 ]; then

    ./configure --with-dpdk-lib=$MTCP_HOME/dpdk

    # 3.2) copy patched makefiles (basically, for compilation of mTCP in i686, 
    # 32 bit)
    cp $MTCP_CONFIGS/files/mtcp/src/Makefile.src $MTCP_HOME/mtcp/src/Makefile
    cp $MTCP_CONFIGS/files/mtcp/src/dpdk_module.c $MTCP_HOME/mtcp/src/
    cp $MTCP_CONFIGS/files/util/Makefile.util $MTCP_HOME/util/Makefile
    cp $MTCP_CONFIGS/files/apps/example/Makefile.example $MTCP_HOME/apps/example/Makefile

    # 3.3) compile mTCP
    cd $MTCP_HOME/mtcp/src
    make

    cd $MTCP_HOME/util
    make

    cd $MTCP_HOME/apps/example
    make
fi

exit 0
