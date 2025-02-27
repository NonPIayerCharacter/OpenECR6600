#!/bin/bash

root_path="/home/gerrit/work/freertos"
obj_path="$root_path/build/standalone/objs"

temp_path="$root_path/temp123"
ld_file="$root_path/Boards/ecr6600/standalone/ls_size.S"

addr_ld=0x10000000

declare -a mdls_array
lib_name=""
lib_path=""
mdl_name=""
process_id=0
sub_process_id=0


declare -a mdls_array


arr_type=("text" "ro" "bss" "data")
arr_type_str=(".text .text.*"  ".rodata .rodata.*"  ".sbss_w .sbss_w.* .bss* COMMON .scommon_*"  ".sdata* .srodata* .data* .version_data")


# genrate_mdl_ld mdl_path lib_name mdl_name
function genrate_mdl_ld(){
    echo "add lib($2)-mdl($3)"
    
    for ((i=0; i<${#arr_type[@]}; i++)); do
        printf "\n  .${arr_type[i]}_$2_$3 :\n  {\n" >> $ld_file
        if [ "$1" = "" ]; then
            printf "    *$2:(${arr_type_str[i]})\n" >> $ld_file
        else
	    for file in `find $1 -name "*.o"`; do
	    	printf "    *$2.a:`basename $file`(${arr_type_str[i]})\n" >> $ld_file
	    done
	fi
	
        printf "  } > v_memory\n" >> $ld_file
    done
}

#scan_lib lib_path lib_name
function scan_lib(){
    for file in `ls $1`; do
        found=0
        if [ -d "$1/${file}" ];then
            for mdl_name in ${lib_mdls[@]};do
                if [ "${file}" == "$mdl_name" ]; then
                    genrate_mdl_ld "$1/${mdl_name}" $2 "${mdl_name}"
                    found=1
                fi
            done
            if [ $found -eq 0 ]; then
                scan_lib "$1/${file}" $2
            fi
        fi
    done
}

function genrate_main(){
    ld_path=$1
    ld_file="$ld_path/mdl.ld"
    obj_path=$2
    
    if [ -f $ld_file ]; then
        echo "found $ld_file, remove"
        rm -rf $ld_file
    fi
    
    if [ ! -d "$obj_path" ]; then
        echo "no obj found, please run make all first"
        return
    fi
    
    ## genrate_mdl_ld mdl_path lib_name mdl_name
    genrate_mdl_ld "" "libapps" 	"apps"
    genrate_mdl_ld "" "libarch" 	"arch"
    genrate_mdl_ld "" "libboard" 	"board"
    genrate_mdl_ld "" "libdrivers" 	"drivers"
    genrate_mdl_ld "" "libos" 		"os"
    genrate_mdl_ld "" "libpsm" 		"psm"    

    #scan_lib lib_path lib_name
    lib_mdls=(at ble_ctrl cjson cli fs health_monitor hostapd_ioctl http_server iperf3 lwip mbedtls mqtt nv ota platform_api pthread smartconfig sntp telnet trs-tls url_parser web_config wifi_ctrl wpa)
    scan_lib "$obj_path/components" "libcomponents"

    lib_mdls=(co ke rf lmac umac dbg fhost mac macif net_al rtos plf)
    scan_lib "$obj_path/PS/wifi" 	"libps_wifi"

    lib_mdls=(ahi app hci hl ll mesh module profiles psm sch thread aes dbg ecc_p256 h4tl hci_adapt nvds rf rwip)
    scan_lib "$obj_path/PS/ble" 	"libps_ble"
}

genrate_main $1 $2
##genrate_main $root_path/Boards/ecr6600/common/mdl_size $root_path/build/standalone/objs




