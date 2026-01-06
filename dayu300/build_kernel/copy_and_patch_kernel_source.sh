
# Copyright (c) 2021-2026 Phytium Technology Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ROOT_DIR=${1}
KERNEL_SRC_TMP_PATH=${2}
DEVICE_NAME=${3}
KERNEL_VERSION=${4}

NEWIP_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/newip/apply_newip.sh
TZDRIVER_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/tzdriver/apply_tzdriver.sh
XPM_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/xpm/apply_xpm.sh
CED_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/container_escape_detection/apply_ced.sh
HIDEADDR_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/memory_security/apply_hideaddr.sh
QOS_AUTH_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/qos_auth/apply_qos_auth.sh
UNIFIED_COLLECTION_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/ucollection/apply_ucollection.sh
CODE_SIGN_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/code_sign/apply_code_sign.sh
DEC_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/dec/apply_dec.sh


#newip
if [ -f $NEWIP_PATCH_FILE ]; then
    bash $NEWIP_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} ${KERNEL_VERSION}
fi

#tzdriver
if [ -f $TZDRIVER_PATCH_FILE ]; then
    bash $TZDRIVER_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} ${KERNEL_VERSION}
fi

#xpm
if [ -f $XPM_PATCH_FILE ]; then
    bash $XPM_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} ${KERNEL_VERSION}
fi

#ced
if [ -f $CED_PATCH_FILE ]; then
    bash $CED_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} ${KERNEL_VERSION}
fi

#qos_auth
if [ -f $QOS_AUTH_PATCH_FILE ]; then
    bash $QOS_AUTH_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} ${KERNEL_VERSION}
fi

#hideaddr
if [ -f $HIDEADDR_PATCH_FILE ]; then
    bash $HIDEADDR_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} ${KERNEL_VERSION}
fi

#ucollection
if [ -f $UNIFIED_COLLECTION_PATCH_FILE ]; then
    bash $UNIFIED_COLLECTION_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} ${KERNEL_VERSION}
fi

#code_sign
if [ -f $CODE_SIGN_PATCH_FILE ]; then
    bash $CODE_SIGN_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} ${KERNEL_VERSION}
fi

#dec
if [ -f $DEC_PATCH_FILE ]; then
    bash $DEC_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} ${KERNEL_VERSION}
fi
