#!/bin/bash

project_path=$(cd `dirname $0`; pwd)
BOARD_DIR="${project_path##*/}"
TOPDIR="../../.."
HOSTOS=$(shell uname -o 2>/dev/null || echo "Other")
HOSTEXEEXT=""
if [ $HOSTOS == "Cygwin" ]
then
	HOSTEXEEXT = ".exe"
fi


OTA_PACKAGE_TARGET=${2:-all}

if [ $# -eq 1 ] || [ $# -eq 2 ]
then
	case "$OTA_PACKAGE_TARGET" in
		all|a|A|b|B)
			;;
		*)
			echo "input: ./make_ota_ab.sh 1.0.0 [a|b|all]"
			exit 1
			;;
	esac

	if [ "$OTA_PACKAGE_TARGET" != "b" ] && [ "$OTA_PACKAGE_TARGET" != "B" ]
	then
		rm -rf "$TOPDIR/build/${BOARD_DIR}_AB_${1}_A"
		make clean
		make REL_V=$1 OTA_CONFIG_TYPE="OTA_AB_UPDATE" all
		rm -rf $TOPDIR/build/$BOARD_DIR/libs $TOPDIR/build/$BOARD_DIR/objs
		cp -r $TOPDIR/build/$BOARD_DIR $TOPDIR/build/$BOARD_DIR"_AB_"$1"_A"
		cp -r ./generated $TOPDIR/build/$BOARD_DIR"_AB_"$1"_A"/.
		echo "* * * * * * * move A OK! * * * * * * * * *"
	fi

	if [ "$OTA_PACKAGE_TARGET" != "a" ] && [ "$OTA_PACKAGE_TARGET" != "A" ]
	then
		grep_str=$(grep -az cpu ref_bin/*Partition*.bin)
		cut_head=${grep_str:4}
		cut_tail=${cut_head%?}
		start_addr_16=${cut_tail%,*}
		len_16=${cut_tail#*,}
		((start_addr=$start_addr_16))
		((len=$len_16))
		B_xip_addr=`expr $len / 2 + $start_addr`
		B_xip_addr_HEX=`printf "0x%x" $B_xip_addr`
		echo "B part XIP_ADDR=$B_xip_addr_HEX"
		rm -rf "$TOPDIR/build/${BOARD_DIR}_AB_${1}_B"
		make clean
		make REL_V=$1 OTA_CONFIG_TYPE="OTA_AB_UPDATE" XIP_ADDR=$B_xip_addr_HEX all
		rm -rf $TOPDIR/build/$BOARD_DIR/libs $TOPDIR/build/$BOARD_DIR/objs
		cp -r $TOPDIR/build/$BOARD_DIR $TOPDIR/build/$BOARD_DIR"_AB_"$1"_B"
		cp -r ./generated $TOPDIR/build/$BOARD_DIR"_AB_"$1"_B"/.
		echo "* * * * * * * move B OK! * * * * * * * * *"
	fi

	echo ""
	echo "* * * * * * Generate AB upgrade package * * * * * * *"
	if [ "$OTA_PACKAGE_TARGET" = "all" ]
	then
		$TOPDIR/scripts/bin/ota_tool/ota_tool$HOSTEXEEXT -m ab -p ref_bin/*Partition*.bin -a $TOPDIR/build/$BOARD_DIR"_AB_"$1"_A"/ECR6600F_*_cpu_0x*.bin -b $TOPDIR/build/$BOARD_DIR"_AB_"$1"_B"/ECR6600F_*_cpu_0x*.bin -o $TOPDIR/build/ECR6600F_AB_ota_package_$1.bin
	fi
	if [ "$OTA_PACKAGE_TARGET" = "all" ] || [ "$OTA_PACKAGE_TARGET" = "a" ] || [ "$OTA_PACKAGE_TARGET" = "A" ]
	then
		$TOPDIR/scripts/bin/ota_tool/ota_tool$HOSTEXEEXT -m abalone -p ref_bin/*Partition*.bin -a $TOPDIR/build/$BOARD_DIR"_AB_"$1"_A"/ECR6600F_*_cpu_0x*.bin -o $TOPDIR/build/ECR6600F_AB_ota_packageA_$1.bin
	fi
	if [ "$OTA_PACKAGE_TARGET" = "all" ] || [ "$OTA_PACKAGE_TARGET" = "b" ] || [ "$OTA_PACKAGE_TARGET" = "B" ]
	then
		$TOPDIR/scripts/bin/ota_tool/ota_tool$HOSTEXEEXT -m abalone -p ref_bin/*Partition*.bin -b $TOPDIR/build/$BOARD_DIR"_AB_"$1"_B"/ECR6600F_*_cpu_0x*.bin -o $TOPDIR/build/ECR6600F_AB_ota_packageB_$1.bin
	fi
else
    echo "input: ./make_ota_ab.sh 1.0.0 [a|b|all]"
fi
