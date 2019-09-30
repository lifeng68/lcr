/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2018-2019. All rights reserved.
 * lcr licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan PSL v1.
 * You may obtain a copy of Mulan PSL v1 at:
 *     http://license.coscl.org.cn/MulanPSL
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v1 for more details.
 * Author: wujing
 * Create: 2018-11-08
 * Description: provide container help functions
 ******************************************************************************/
#include <stdio.h>
#include "arguments.h"
#include "help.h"

const char g_lcr_cmd_help_desc[] =
    "show a list of commands or help for one command";
const char g_lcr_cmd_help_long_desc[] =
    "NAME:\n"
    "\tlcr help - show a list of commands or help for one command\n"
    "\n"
    "USAGE:\n"
    "\tlcr help [command]\n";

/* cmd help main */
int cmd_help_main(int argc, char **argv, struct lcr_arguments *lcr_cmd_args)
{
    printf("cmd_list\n");
    return 0;
}