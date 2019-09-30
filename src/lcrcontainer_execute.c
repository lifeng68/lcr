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
 * Description: provide container definition
 ******************************************************************************/
/*
 * liblcrapi
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>

#include "constants.h"
#include "lcrcontainer_execute.h"
#include "utils.h"
#include "log.h"
#include "error.h"
#include "securec.h"
#include "oci_runtime_spec.h"

// Cgroup Item Definition
#define CGROUP_BLKIO_WEIGHT "blkio.weight"
#define CGROUP_CPU_SHARES "cpu.shares"
#define CGROUP_CPU_PERIOD "cpu.cfs_period_us"
#define CGROUP_CPU_QUOTA "cpu.cfs_quota_us"
#define CGROUP_CPUSET_CPUS "cpuset.cpus"
#define CGROUP_CPUSET_MEMS "cpuset.mems"
#define CGROUP_MEMORY_LIMIT "memory.limit_in_bytes"
#define CGROUP_MEMORY_SWAP "memory.memsw.limit_in_bytes"
#define CGROUP_MEMORY_RESERVATION "memory.soft_limit_in_bytes"

#define REPORT_SET_CGROUP_ERROR(item, value)                                                          \
    do {                                                                                              \
        SYSERROR("Error updating cgroup %s to %s", (item), (value));                                  \
        lcr_set_error_message(LCR_ERR_RUNTIME, "Error updating cgroup %s to %s: %s", (item), (value), \
                              strerror(errno));                                                       \
    } while (0)

static inline void add_array_elem(char **array, size_t total, size_t *pos, const char *elem)
{
    if (*pos + 1 >= total - 1) {
        return;
    }
    array[*pos] = util_strdup_s(elem);
    *pos += 1;
}

static inline void add_array_kv(char **array, size_t total, size_t *pos, const char *k, const char *v)
{
    if (k == NULL || v == NULL) {
        return;
    }
    add_array_elem(array, total, pos, k);
    add_array_elem(array, total, pos, v);
}

static int make_sure_linux(oci_runtime_spec *oci_spec)
{
    if (oci_spec->linux == NULL) {
        oci_spec->linux = util_common_calloc_s(sizeof(oci_runtime_config_linux));
        if (oci_spec->linux == NULL) {
            return -1;
        }
    }
    return 0;
}

static int make_sure_linux_resources(oci_runtime_spec *oci_spec)
{
    int ret = 0;

    ret = make_sure_linux(oci_spec);
    if (ret < 0) {
        return -1;
    }

    if (oci_spec->linux->resources == NULL) {
        oci_spec->linux->resources = util_common_calloc_s(sizeof(oci_runtime_config_linux_resources));
        if (oci_spec->linux->resources == NULL) {
            return -1;
        }
    }
    return 0;
}

static int make_sure_linux_resources_blkio(oci_runtime_spec *oci_spec)
{
    int ret = 0;

    ret = make_sure_linux_resources(oci_spec);
    if (ret < 0) {
        return -1;
    }

    if (oci_spec->linux->resources->block_io == NULL) {
        oci_spec->linux->resources->block_io =
            util_common_calloc_s(sizeof(oci_runtime_config_linux_resources_block_io));
        if (oci_spec->linux->resources->block_io == NULL) {
            return -1;
        }
    }
    return 0;
}

static int make_sure_linux_resources_cpu(oci_runtime_spec *oci_spec)
{
    int ret = 0;

    ret = make_sure_linux_resources(oci_spec);
    if (ret < 0) {
        return -1;
    }

    if (oci_spec->linux->resources->cpu == NULL) {
        oci_spec->linux->resources->cpu = util_common_calloc_s(sizeof(oci_runtime_config_linux_resources_cpu));
        if (oci_spec->linux->resources->cpu == NULL) {
            return -1;
        }
    }
    return 0;
}

static int make_sure_linux_resources_mem(oci_runtime_spec *oci_spec)
{
    int ret = 0;

    ret = make_sure_linux_resources(oci_spec);
    if (ret < 0) {
        return -1;
    }

    if (oci_spec->linux->resources->memory == NULL) {
        oci_spec->linux->resources->memory = util_common_calloc_s(sizeof(oci_runtime_config_linux_resources_memory));
        if (oci_spec->linux->resources->memory == NULL) {
            return -1;
        }
    }
    return 0;
}

static bool update_resources_cpuset_mems(struct lxc_container *c, const struct lcr_cgroup_resources *cr)
{
    bool ret = false;

    if (cr->cpuset_mems != NULL && strcmp(cr->cpuset_mems, "") != 0) {
        if (!c->set_cgroup_item(c, CGROUP_CPUSET_MEMS, cr->cpuset_mems)) {
            REPORT_SET_CGROUP_ERROR(CGROUP_CPUSET_MEMS, cr->cpuset_mems);
            goto err_out;
        }
    }
    ret = true;
err_out:
    return ret;
}

static bool update_resources_cpuset(struct lxc_container *c, const struct lcr_cgroup_resources *cr)
{
    bool ret = false;
    if (cr->cpuset_cpus != NULL && strcmp(cr->cpuset_cpus, "") != 0) {
        if (!c->set_cgroup_item(c, CGROUP_CPUSET_CPUS, cr->cpuset_cpus)) {
            REPORT_SET_CGROUP_ERROR(CGROUP_CPUSET_CPUS, cr->cpuset_cpus);
            goto err_out;
        }
    }

    ret = true;
err_out:
    return ret;
}

static int update_resources_cpu_shares(struct lxc_container *c, const struct lcr_cgroup_resources *cr)
{
    int ret = 0;
    char numstr[128] = { 0 }; /* max buffer */

    if (cr->cpu_shares != 0) {
        if (sprintf_s(numstr, sizeof(numstr), "%llu", (unsigned long long)(cr->cpu_shares)) < 0) {
            ret = -1;
            goto out;
        }

        if (!c->set_cgroup_item(c, CGROUP_CPU_SHARES, numstr)) {
            REPORT_SET_CGROUP_ERROR(CGROUP_CPU_SHARES, numstr);
            ret = -1;
            goto out;
        }
    }

out:
    return ret;
}

static int update_resources_cpu_period(struct lxc_container *c, const struct lcr_cgroup_resources *cr)
{
    int ret = 0;
    char numstr[128] = { 0 }; /* max buffer */

    if (cr->cpu_period != 0) {
        if (sprintf_s(numstr, sizeof(numstr), "%llu", (unsigned long long)(cr->cpu_period)) < 0) {
            ret = -1;
            goto out;
        }

        if (!c->set_cgroup_item(c, CGROUP_CPU_PERIOD, numstr)) {
            REPORT_SET_CGROUP_ERROR(CGROUP_CPU_PERIOD, numstr);
            ret = -1;
            goto out;
        }
    }

out:
    return ret;
}

static int update_resources_cpu_quota(struct lxc_container *c, const struct lcr_cgroup_resources *cr)
{
    int ret = 0;
    char numstr[128] = { 0 }; /* max buffer */

    if (cr->cpu_quota != 0) {
        if (sprintf_s(numstr, sizeof(numstr), "%llu", (unsigned long long)(cr->cpu_quota)) < 0) {
            ret = -1;
            goto out;
        }

        if (!c->set_cgroup_item(c, CGROUP_CPU_QUOTA, numstr)) {
            REPORT_SET_CGROUP_ERROR(CGROUP_CPU_QUOTA, numstr);
            ret = -1;
            goto out;
        }
    }

out:
    return ret;
}

static bool update_resources_cpu(struct lxc_container *c, const struct lcr_cgroup_resources *cr)
{
    bool ret = false;

    if (update_resources_cpu_shares(c, cr) != 0) {
        goto err_out;
    }

    if (update_resources_cpu_period(c, cr) != 0) {
        goto err_out;
    }

    if (update_resources_cpu_quota(c, cr) != 0) {
        goto err_out;
    }

    if (!update_resources_cpuset(c, cr)) {
        goto err_out;
    }

    if (!update_resources_cpuset_mems(c, cr)) {
        goto err_out;
    }

    ret = true;
err_out:
    return ret;
}

static int update_resources_memory_limit(struct lxc_container *c, const struct lcr_cgroup_resources *cr)
{
    int ret = 0;
    char numstr[128] = { 0 }; /* max buffer */

    if (cr->memory_limit != 0) {
        if (sprintf_s(numstr, sizeof(numstr), "%llu", (unsigned long long)(cr->memory_limit)) < 0) {
            ret = -1;
            goto out;
        }

        if (!c->set_cgroup_item(c, CGROUP_MEMORY_LIMIT, numstr)) {
            REPORT_SET_CGROUP_ERROR(CGROUP_MEMORY_LIMIT, numstr);
            ret = -1;
            goto out;
        }
    }

out:
    return ret;
}

static int update_resources_memory_swap(struct lxc_container *c, const struct lcr_cgroup_resources *cr)
{
    int ret = 0;
    char numstr[128] = { 0 }; /* max buffer */

    if (cr->memory_swap != 0) {
        if (sprintf_s(numstr, sizeof(numstr), "%llu", (unsigned long long)(cr->memory_swap)) < 0) {
            ret = -1;
            goto out;
        }

        if (!c->set_cgroup_item(c, CGROUP_MEMORY_SWAP, numstr)) {
            REPORT_SET_CGROUP_ERROR(CGROUP_MEMORY_SWAP, numstr);
            ret = -1;
            goto out;
        }
    }

out:
    return ret;
}

static int update_resources_memory_reservation(struct lxc_container *c, const struct lcr_cgroup_resources *cr)
{
    int ret = 0;
    char numstr[128] = { 0 }; /* max buffer */

    if (cr->memory_reservation != 0) {
        if (sprintf_s(numstr, sizeof(numstr), "%llu", (unsigned long long)(cr->memory_reservation)) < 0) {
            ret = -1;
            goto out;
        }

        if (!c->set_cgroup_item(c, CGROUP_MEMORY_RESERVATION, numstr)) {
            REPORT_SET_CGROUP_ERROR(CGROUP_MEMORY_RESERVATION, numstr);
            ret = -1;
            goto out;
        }
    }

out:
    return ret;
}

static bool update_resources_mem(struct lxc_container *c, const struct lcr_cgroup_resources *cr)
{
    bool ret = false;

    if (update_resources_memory_limit(c, cr) != 0) {
        goto err_out;
    }

    if (update_resources_memory_swap(c, cr) != 0) {
        goto err_out;
    }

    if (update_resources_memory_reservation(c, cr) != 0) {
        goto err_out;
    }

    ret = true;
err_out:
    return ret;
}

static int update_resources_blkio_weight(struct lxc_container *c, const struct lcr_cgroup_resources *cr)
{
    int ret = 0;
    char numstr[128] = { 0 }; /* max buffer */

    if (cr->blkio_weight != 0) {
        if (sprintf_s(numstr, sizeof(numstr), "%llu", (unsigned long long)(cr->blkio_weight)) < 0) {
            ret = -1;
            goto out;
        }

        if (!c->set_cgroup_item(c, CGROUP_BLKIO_WEIGHT, numstr)) {
            REPORT_SET_CGROUP_ERROR(CGROUP_BLKIO_WEIGHT, numstr);
            ret = -1;
            goto out;
        }
    }

out:
    return ret;
}

static bool update_resources(struct lxc_container *c, const struct lcr_cgroup_resources *cr)
{
    bool ret = false;

    if (c == NULL || cr == NULL) {
        return false;
    }

    if (update_resources_blkio_weight(c, cr) != 0) {
        goto err_out;
    }

    if (!update_resources_cpu(c, cr)) {
        goto err_out;
    }
    if (!update_resources_mem(c, cr)) {
        goto err_out;
    }

    ret = true;
err_out:
    return ret;
}

static bool save_container_to_disk(const struct lxc_container *c, const oci_runtime_spec *container)
{
    bool bret = true;
    char *json_container = NULL;
    struct parser_context ctr = { 0 };
    parser_error err = NULL;

    if (c == NULL || container == NULL) {
        return false;
    }

    ctr.options = OPT_PARSE_STRICT;
    ctr.stderr = stderr;

    json_container = oci_runtime_spec_generate_json(container, &ctr, &err);
    if (json_container == NULL) {
        ERROR("Can not generate json: %s", err);
        bret = false;
        goto err_out;
    }
    if (!translate_spec(c, json_container, NULL)) {
        bret = false;
        goto err_out;
    }
err_out:
    free(json_container);
    free(err);
    return bret;
}

static int merge_cgroup_resources_cpu_shares(oci_runtime_spec *c, const struct lcr_cgroup_resources *cr)
{
    if (cr->cpu_shares != 0) {
        if (make_sure_linux_resources_cpu(c) != 0) {
            return -1;
        }
        c->linux->resources->cpu->shares = cr->cpu_shares;
    }

    return 0;
}

static int merge_cgroup_resources_cpu_period(oci_runtime_spec *c, const struct lcr_cgroup_resources *cr)
{
    if (cr->cpu_period != 0) {
        if (make_sure_linux_resources_cpu(c) != 0) {
            return -1;
        }
        c->linux->resources->cpu->period = cr->cpu_period;
    }

    return 0;
}

static int merge_cgroup_resources_cpu_quota(oci_runtime_spec *c, const struct lcr_cgroup_resources *cr)
{
    if (cr->cpu_quota != 0) {
        if (make_sure_linux_resources_cpu(c) != 0) {
            return -1;
        }
        c->linux->resources->cpu->quota = (int64_t)(cr->cpu_quota);
    }

    return 0;
}

static int merge_cgroup_resources_cpuset_cpus(oci_runtime_spec *c, const struct lcr_cgroup_resources *cr)
{
    if (cr->cpuset_cpus != NULL && strcmp(cr->cpuset_cpus, "") != 0) {
        if (make_sure_linux_resources_cpu(c) != 0) {
            return -1;
        }
        free(c->linux->resources->cpu->cpus);
        c->linux->resources->cpu->cpus = util_strdup_s(cr->cpuset_cpus);
    }

    return 0;
}

static int merge_cgroup_resources_cpuset_mems(oci_runtime_spec *c, const struct lcr_cgroup_resources *cr)
{
    if (cr->cpuset_mems != NULL && strcmp(cr->cpuset_mems, "") != 0) {
        if (make_sure_linux_resources_cpu(c) != 0) {
            return -1;
        }
        free(c->linux->resources->cpu->mems);
        c->linux->resources->cpu->mems = util_strdup_s(cr->cpuset_mems);
    }

    return 0;
}

static bool merge_cgroup_resources_cpu(oci_runtime_spec *c, const struct lcr_cgroup_resources *cr)
{
    bool ret = true;

    if (merge_cgroup_resources_cpu_shares(c, cr) != 0) {
        ret = false;
        goto out;
    }

    if (merge_cgroup_resources_cpu_period(c, cr) != 0) {
        ret = false;
        goto out;
    }

    if (merge_cgroup_resources_cpu_quota(c, cr) != 0) {
        ret = false;
        goto out;
    }

    if (merge_cgroup_resources_cpuset_cpus(c, cr) != 0) {
        ret = false;
        goto out;
    }

    if (merge_cgroup_resources_cpuset_mems(c, cr) != 0) {
        ret = false;
        goto out;
    }

out:
    return ret;
}

static int merge_cgroup_resources_mem_limit(oci_runtime_spec *c, const struct lcr_cgroup_resources *cr)
{
    if (cr->memory_limit != 0) {
        if (make_sure_linux_resources_mem(c) != 0) {
            return -1;
        }
        c->linux->resources->memory->limit = (int64_t)(cr->memory_limit);
    }

    return 0;
}

static int merge_cgroup_resources_mem_reservation(oci_runtime_spec *c, const struct lcr_cgroup_resources *cr)
{
    if (cr->memory_reservation != 0) {
        if (make_sure_linux_resources_mem(c) != 0) {
            return -1;
        }
        c->linux->resources->memory->reservation = (int64_t)(cr->memory_reservation);
    }

    return 0;
}

static int merge_cgroup_resources_mem_swap(oci_runtime_spec *c, const struct lcr_cgroup_resources *cr)
{
    if (cr->memory_swap != 0) {
        if (make_sure_linux_resources_mem(c) != 0) {
            return -1;
        }
        c->linux->resources->memory->swap = (int64_t)(cr->memory_swap);
    }

    return 0;
}

static int merge_cgroup_resources_kernel_memory_limit(oci_runtime_spec *c, const struct lcr_cgroup_resources *cr)
{
    if (cr->kernel_memory_limit != 0) {
        if (make_sure_linux_resources_mem(c) != 0) {
            return -1;
        }
        c->linux->resources->memory->kernel = (int64_t)(cr->kernel_memory_limit);
    }

    return 0;
}

static bool merge_cgroup_resources_mem(oci_runtime_spec *c, const struct lcr_cgroup_resources *cr)
{
    bool ret = true;

    if (merge_cgroup_resources_mem_limit(c, cr) != 0) {
        ret = false;
        goto out;
    }

    if (merge_cgroup_resources_mem_reservation(c, cr) != 0) {
        ret = false;
        goto out;
    }

    if (merge_cgroup_resources_mem_swap(c, cr) != 0) {
        ret = false;
        goto out;
    }

    if (merge_cgroup_resources_kernel_memory_limit(c, cr) != 0) {
        ret = false;
        goto out;
    }

out:
    return ret;
}

static int merge_cgroup_resources_blkio_weight(oci_runtime_spec *c, const struct lcr_cgroup_resources *cr)
{
    if (cr->blkio_weight != 0) {
        if (make_sure_linux_resources_blkio(c) != 0) {
            return -1;
        }
        c->linux->resources->block_io->weight = (int)(cr->blkio_weight);
    }

    return 0;
}

static bool merge_cgroup_resources(oci_runtime_spec *c, const struct lcr_cgroup_resources *cr)
{
    bool ret = true;

    if (c == NULL || cr == NULL) {
        return false;
    }

    if (merge_cgroup_resources_blkio_weight(c, cr) != 0) {
        ret = false;
        goto out;
    }

    ret = merge_cgroup_resources_cpu(c, cr);
    if (!ret) {
        goto out;
    }

    ret = merge_cgroup_resources_mem(c, cr);
    if (!ret) {
        goto out;
    }

out:
    return ret;
}

bool do_update(struct lxc_container *c, const char *name, const char *lcrpath, const struct lcr_cgroup_resources *cr)
{
    int rc = 0;
    bool bret = false;
    bool restore = false;
    char *config_json_file = NULL;
    oci_runtime_spec *container = NULL;
    oci_runtime_spec *backupcontainer = NULL;
    struct parser_context ctr = { OPT_PARSE_STRICT, stderr };
    parser_error err = NULL;

    rc = asprintf(&config_json_file, "%s/%s/%s", lcrpath, name, OCICONFIGFILE);
    if (rc < 0) {
        SYSERROR("Failed to allocated memory");
        return false;
    }

    backupcontainer = oci_runtime_spec_parse_file(config_json_file, &ctr, &err);
    if (backupcontainer == NULL) {
        ERROR("Can not parse %s: %s", OCICONFIGFILE, err);
        goto out_free;
    }
    container = oci_runtime_spec_parse_file(config_json_file, &ctr, &err);
    if (container == NULL) {
        ERROR("Can not parse %s: %s", OCICONFIGFILE, err);
        goto out_free;
    }

    if (!merge_cgroup_resources(container, cr)) {
        ERROR("Failed to merge cgroup resources");
        goto out_free;
    }

    if (!save_container_to_disk(c, container)) {
        ERROR("Failed to save config to disk");
        restore = true;
        goto restore;
    }

    // If container is not running, update config file is enough,
    // resources will be updated when the container is started again.
    // If container is running (including paused), we need to update configs
    // to the real world.
    if (c->is_running(c)) {
        if (!update_resources(c, cr) && c->is_running(c)) {
            ERROR("Filed to update cgroup resources");
            restore = true;
            goto restore;
        }
    }

    bret = true;

restore:
    if (restore && !save_container_to_disk(c, backupcontainer)) {
        ERROR("Failed to restore");
        bret = false;
    }

out_free:
    free_oci_runtime_spec(backupcontainer);
    free_oci_runtime_spec(container);
    free(config_json_file);
    free(err);
    if (bret) {
        clear_error_message(&g_lcr_error);
    }
    return bret;
}

static inline bool is_blk_stat_read(const char *value)
{
    return strcmp(value, "Read") == 0;
}

static inline bool is_blk_stat_write(const char *value)
{
    return strcmp(value, "Write") == 0;
}

static inline bool is_blk_stat_total(const char *value)
{
    return strcmp(value, "Total") == 0;
}

static void stat_get_blk_stats(struct lxc_container *c, const char *item, struct blkio_stats *stats)
{
    char buf[BUFSIZE] = { 0 };
    int i = 0;
    size_t len = 0;
    char **lines = NULL;
    char **cols = NULL;
    errno_t nret = 0;

    len = (size_t)c->get_cgroup_item(c, item, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) {
        DEBUG("unable to read cgroup item %s", item);
        return;
    }

    lines = lcr_string_split_and_trim(buf, '\n');
    if (lines == NULL) {
        return;
    }

    nret = memset_s(stats, sizeof(struct blkio_stats), 0, sizeof(struct blkio_stats));
    if (nret != EOK) {
        goto err_out;
    }

    for (i = 0; lines[i]; i++) {
        cols = lcr_string_split_and_trim(lines[i], ' ');
        if (cols == NULL) {
            goto err_out;
        }
        if (is_blk_stat_read(cols[1])) {
            stats->read += strtoull(cols[2], NULL, 0);
        } else if (is_blk_stat_write(cols[1])) {
            stats->write += strtoull(cols[2], NULL, 0);
        }
        if (is_blk_stat_total(cols[0])) {
            stats->total = strtoull(cols[1], NULL, 0);
        }

        lcr_free_array((void **)cols);
    }
err_out:
    lcr_free_array((void **)lines);
    return;
}

static uint64_t stat_match_get_ull(struct lxc_container *c, const char *item, const char *match, int column)
{
    char buf[BUFSIZE] = { 0 };
    int i = 0;
    int j = 0;
    int len = 0;
    uint64_t val = 0;
    char **lines = NULL;
    char **cols = NULL;
    size_t matchlen = 0;

    len = c->get_cgroup_item(c, item, buf, sizeof(buf));
    if (len <= 0) {
        DEBUG("unable to read cgroup item %s", item);
        goto err_out;
    }

    lines = lcr_string_split_and_trim(buf, '\n');
    if (lines == NULL) {
        goto err_out;
    }

    matchlen = strlen(match);
    for (i = 0; lines[i]; i++) {
        if (strncmp(lines[i], match, matchlen) != 0) {
            continue;
        }

        cols = lcr_string_split_and_trim(lines[i], ' ');
        if (cols == NULL) {
            goto err1;
        }
        for (j = 0; cols[j]; j++) {
            if (j == column) {
                val = strtoull(cols[j], NULL, 0);
                break;
            }
        }
        lcr_free_array((void **)cols);
        break;
    }
err1:
    lcr_free_array((void **)lines);
err_out:
    return val;
}

static uint64_t stat_get_ull(struct lxc_container *c, const char *item)
{
    char buf[80] = { 0 };
    int len = 0;
    uint64_t val = 0;

    len = c->get_cgroup_item(c, item, buf, sizeof(buf));
    if (len <= 0) {
        DEBUG("unable to read cgroup item %s", item);
        return 0;
    }

    val = strtoull(buf, NULL, 0);
    return val;
}

void do_lcr_state(struct lxc_container *c, struct lcr_container_state *lcs)
{
    const char *state = NULL;

    clear_error_message(&g_lcr_error);
    if (memset_s(lcs, sizeof(struct lcr_container_state), 0x00, sizeof(struct lcr_container_state)) != EOK) {
        ERROR("Can not set memory");
    }

    lcs->name = util_strdup_s(c->name);

    state = c->state(c);
    lcs->state = state ? util_strdup_s(state) : util_strdup_s("-");

    if (c->is_running(c)) {
        lcs->init = c->init_pid(c);
    } else {
        lcs->init = -1;
    }

    lcs->cpu_use_nanos = stat_get_ull(c, "cpuacct.usage");
    lcs->pids_current = stat_get_ull(c, "pids.current");

    lcs->cpu_use_user = stat_match_get_ull(c, "cpuacct.stat", "user", 1);
    lcs->cpu_use_sys = stat_match_get_ull(c, "cpuacct.stat", "system", 1);

    // Try to read CFQ stats available on all CFQ enabled kernels first
    stat_get_blk_stats(c, "blkio.io_serviced_recursive", &lcs->io_serviced);
    if (lcs->io_serviced.read == 0 && lcs->io_serviced.write == 0 && lcs->io_serviced.total == 0) {
        stat_get_blk_stats(c, "blkio.throttle.io_service_bytes", &lcs->io_service_bytes);
        stat_get_blk_stats(c, "blkio.throttle.io_serviced", &lcs->io_serviced);
    } else {
        stat_get_blk_stats(c, "blkio.io_service_bytes_recursive", &lcs->io_service_bytes);
    }

    lcs->mem_used = stat_get_ull(c, "memory.usage_in_bytes");
    lcs->mem_limit = stat_get_ull(c, "memory.limit_in_bytes");
    lcs->kmem_used = stat_get_ull(c, "memory.kmem.usage_in_bytes");
    lcs->kmem_limit = stat_get_ull(c, "memory.kmem.limit_in_bytes");
}

#define ExitSignalOffset 128

static void execute_lxc_attach(const char *name, const char *path, const char *logpath, const char *loglevel,
                               const char *console_fifos[], const char * const argv[], const char * const env[],
                               int64_t timeout)
{
    // should check the size of params when add new params.
    char **params = NULL;
    size_t i = 0;
    size_t args_len = PARAM_NUM;
    const char * const *tmp = NULL;

    if (util_check_inherited(true, -1) != 0) {
        COMMAND_ERROR("Close inherited fds failed");
        exit(EXIT_FAILURE);
    }

    for (tmp = env; tmp != NULL && *tmp != NULL; tmp++) {
        args_len++;
    }
    for (tmp = argv; tmp != NULL && *tmp != NULL; tmp++) {
        args_len++;
    }

    if (args_len > (SIZE_MAX / sizeof(char *))) {
        exit(EXIT_FAILURE);
    }

    params = util_common_calloc_s(args_len * sizeof(char *));
    if (params == NULL) {
        COMMAND_ERROR("Out of memory");
        exit(EXIT_FAILURE);
    }
    add_array_elem(params, args_len, &i, "lxc-attach");
    add_array_elem(params, args_len, &i, "-n");
    add_array_elem(params, args_len, &i, name);
    add_array_elem(params, args_len, &i, "-P");
    add_array_elem(params, args_len, &i, path);
    add_array_elem(params, args_len, &i, "--clear-env");
    add_array_kv(params, args_len, &i, "--logfile", logpath);
    add_array_kv(params, args_len, &i, "-l", loglevel);
    add_array_kv(params, args_len, &i, "--in-fifo", console_fifos[0]);
    add_array_kv(params, args_len, &i, "--out-fifo", console_fifos[1]);
    for (tmp = env; tmp && *tmp; tmp++) {
        add_array_elem(params, args_len, &i, "-v");
        add_array_elem(params, args_len, &i, *tmp);
    }

    if (timeout != 0) {
        char timeout_str[LCR_NUMSTRLEN64] = { 0 };
        add_array_elem(params, args_len, &i, "--timeout");
        if (sprintf_s(timeout_str, LCR_NUMSTRLEN64, "%lld", (long long)timeout) < 0) {
            COMMAND_ERROR("Invaild attach timeout value :%lld", (long long)timeout);
            free(params);
            exit(EXIT_FAILURE);
        }
        add_array_elem(params, args_len, &i, timeout_str);
    }

    add_array_elem(params, args_len, &i, "--");
    for (tmp = argv; tmp && *tmp; tmp++) {
        add_array_elem(params, args_len, &i, *tmp);
    }

    execvp("lxc-attach", params);

    COMMAND_ERROR("Failed to exec lxc-attach: %s", strerror(errno));
    free(params);
    exit(EXIT_FAILURE);
}

static int do_attach_get_exit_code(int status)
{
    int exit_code = 0;

    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else {
        exit_code = -1;
    }

    if (WIFSIGNALED(status)) {
        int signal;
        signal = WTERMSIG(status);
        exit_code = ExitSignalOffset + signal;
    }
    return exit_code;
}

bool do_attach(const char *name, const char *path, const char *logpath, const char *loglevel,
               const char *console_fifos[], const char * const argv[], const char * const env[], int64_t timeout,
               pid_t *exec_pid, int *exit_code)
{
    bool ret = false;
    pid_t pid = 0;
    ssize_t size_read = 0;
    char buffer[BUFSIZ] = { 0 };
    int pipefd[2] = { -1, -1 };
    int status = 0;

    if (pipe(pipefd) != 0) {
        ERROR("Failed to create pipe\n");
        return false;
    }

    pid = fork();
    if (pid == (pid_t) - 1) {
        ERROR("Failed to fork()\n");
        close(pipefd[0]);
        close(pipefd[1]);
        goto out;
    }

    if (pid == (pid_t)0) {
        if (util_null_stdfds() < 0) {
            COMMAND_ERROR("Failed to close fds");
            exit(EXIT_FAILURE);
        }
        setsid();

        // child process, dup2 pipefd[1] to stderr
        close(pipefd[0]);
        dup2(pipefd[1], 2);

        execute_lxc_attach(name, path, logpath, loglevel, console_fifos, argv, env, timeout);
    }

    close(pipefd[1]);

    status = wait_for_pid_status(pid);
    if (status < 0) {
        ERROR("Failed to wait lxc-attach");
        goto close_out;
    }

    *exit_code = do_attach_get_exit_code(status);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        size_read = read(pipefd[0], buffer, BUFSIZ);
        /* if we read errmsg means the runtime failed to exec */
        if (size_read > 0) {
            ERROR("Runtime error: %s", buffer);
            lcr_set_error_message(LCR_ERR_RUNTIME, "runtime error: %s", buffer);
            goto close_out;
        }
    }
    ret = true;

close_out:
    close(pipefd[0]);
out:
    return ret;
}

void execute_lxc_start(const char *name, const char *path, const char *logpath, const char *loglevel,
                       bool daemonize, bool tty, bool open_stdin, const char *pidfile,
                       const char *console_fifos[], const char *console_logpath, const char *share_ns[],
                       uint32_t start_timeout, const char *container_pidfile, const char *exit_fifo)
{
    // should check the size of params when add new params.
    char *params[PARAM_NUM] = { NULL };
    size_t i = 0;

    if (util_check_inherited(true, -1) != 0) {
        COMMAND_ERROR("Close inherited fds failed");
    }

    add_array_elem(params, PARAM_NUM, &i, "lxc-start");
    add_array_elem(params, PARAM_NUM, &i, "-n");
    add_array_elem(params, PARAM_NUM, &i, name);
    add_array_elem(params, PARAM_NUM, &i, "-P");
    add_array_elem(params, PARAM_NUM, &i, path);
    add_array_elem(params, PARAM_NUM, &i, "--quiet");
    add_array_kv(params, PARAM_NUM, &i, "--logfile", logpath);
    add_array_kv(params, PARAM_NUM, &i, "-l", loglevel);
    add_array_kv(params, PARAM_NUM, &i, "--in-fifo", console_fifos[0]);
    add_array_kv(params, PARAM_NUM, &i, "--out-fifo", console_fifos[1]);
    add_array_kv(params, PARAM_NUM, &i, "--err-fifo", console_fifos[2]);
    if (!tty) {
        add_array_elem(params, PARAM_NUM, &i, "--disable-pty");
    }
    if (open_stdin) {
        add_array_elem(params, PARAM_NUM, &i, "--open-stdin");
    }
    add_array_elem(params, PARAM_NUM, &i, daemonize ? "-d" : "-F");
    add_array_kv(params, PARAM_NUM, &i, "-p", pidfile);
    add_array_kv(params, PARAM_NUM, &i, "-L", console_logpath);
    add_array_kv(params, PARAM_NUM, &i, "--container-pidfile", container_pidfile);
    add_array_kv(params, PARAM_NUM, &i, "--exit-fifo", exit_fifo);

    if (start_timeout != 0) {
        char start_timeout_str[LCR_NUMSTRLEN64] = { 0 };
        add_array_elem(params, PARAM_NUM, &i, "--start-timeout");
        if (sprintf_s(start_timeout_str, LCR_NUMSTRLEN64, "%u", start_timeout) < 0) {
            COMMAND_ERROR("Invaild start timeout value: %u", start_timeout);
            exit(EXIT_FAILURE);
        }
        add_array_elem(params, PARAM_NUM, &i, start_timeout_str);
    }

    execvp("lxc-start", params);

    COMMAND_ERROR("Failed to exec lxc-start\n");
    exit(EXIT_FAILURE);
}