/** @file
    rtl_fuzz, fuzzing target for the rtl_433 application.

    taken and adapted from rtl_433

    Copyright (C) 2020 by Rick van Schijndel <rol3517@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "rtl_433.h"
#include "r_private.h"
#include "r_device.h"
#include "rtl_433_devices.h"
#include "r_api.h"
#include "pulse_detect.h"
#include "pulse_detect_fsk.h"
#include "pulse_demod.h"
#include "data.h"
#include "optparse.h"
#include "am_analyze.h"
#include "confparse.h"
#include "compat_paths.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#ifdef _MSC_VER
#define F_OK 0
#endif
#endif
#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifndef _MSC_VER
#include <getopt.h>
#else
#include "getopt/getopt.h"
#endif

static void parse_conf_option(r_cfg_t *cfg, int opt, char *arg);

#define OPTSTRING "vf:g:s:R:G"

// these should match the short options exactly
static struct conf_keywords const conf_keywords[] = {
        {"verbose", 'v'},
        {"device", 'd'},
        {"frequency", 'f'},
        {"sample_rate", 's'},
        {"protocol", 'R'},
        {"register_all", 'G'},
        {NULL, 0}};

static void parse_conf_text(r_cfg_t *cfg, char *conf)
{
    int opt;
    char *arg;
    char *p = conf;

    if (!conf || !*conf)
        return;

    while ((opt = getconf(&p, conf_keywords, &arg)) != -1) {
        parse_conf_option(cfg, opt, arg);
    }
}

static void parse_conf_args(r_cfg_t *cfg, int argc, char *argv[])
{
    int opt;

    optind = 1; // reset getopt
    while ((opt = getopt(argc, argv, OPTSTRING)) != -1) {
        if (opt == '?')
            opt = optopt; // allow missing arguments
        parse_conf_option(cfg, opt, optarg);
    }
}

static void parse_conf_option(r_cfg_t *cfg, int opt, char *arg)
{
    int n;

    switch (opt) {
    case 'v':
        if (!arg)
            cfg->verbosity++;
        else
            cfg->verbosity = atobv(arg, 1);
        break;
    case 'f':
        if (cfg->frequencies < MAX_FREQS) {
            uint32_t sr = atouint32_metric(arg, "-f: ");
            /* If the frequency is above 800MHz sample at 1MS/s */
            if ((sr > FSK_PULSE_DETECTOR_LIMIT) && (cfg->samp_rate == DEFAULT_SAMPLE_RATE)) {
                cfg->samp_rate = 1000000;
                fprintf(stderr, "\nNew defaults active, use \"-Y classic -s 250k\" for the old defaults!\n\n");
            }
            cfg->frequency[cfg->frequencies++] = sr;
        }
        else
            fprintf(stderr, "Max number of frequencies reached %d\n", MAX_FREQS);
        break;
    case 'G':
        cfg->no_default_devices = 1;
        register_all_protocols(cfg, 1);
        break;
    case 's':
        cfg->samp_rate = atouint32_metric(arg, "-s: ");
        break;
    case 'R':
        if (!arg) {
            fprintf(stderr, "Missing argument for -R switch\n");
            exit(1);
        }

        n = atoi(arg);
        if (n > cfg->num_r_devices || -n > cfg->num_r_devices) {
            fprintf(stderr, "Protocol number specified (%d) is larger than number of protocols\n\n", n);
            exit(1);
        }
        if ((n > 0 && cfg->devices[n - 1].disabled > 2) || (n < 0 && cfg->devices[-n - 1].disabled > 2)) {
            fprintf(stderr, "Protocol number specified (%d) is invalid\n\n", n);
            exit(1);
        }

        if (n < 0 && !cfg->no_default_devices) {
            register_all_protocols(cfg, 0); // register all defaults
        }
        cfg->no_default_devices = 1;

        if (n >= 1) {
            register_protocol(cfg, &cfg->devices[n - 1], arg_param(arg));
        }
        else if (n <= -1) {
            unregister_protocol(cfg, &cfg->devices[-n - 1]);
        }
        else {
            fprintf(stderr, "Disabling all device decoders.\n");
            list_clear(&cfg->demod->r_devs, (list_elem_free_fn)free_protocol);
        }
        break;
    default:
        exit(1);
        break;
    }
}

int process_test_data(r_cfg_t* cfg, struct dm_state* demod, const char* line)
{
    if (cfg->verbosity)
        fprintf(stderr, "Processing test data: %s\n", line);
    int r = 0;
    // test a single decoder?
    if (*line == '[') {
        char *e    = NULL;
        unsigned d = (unsigned)strtol(&line[1], &e, 10);
        if (!e || *e != ']') {
            fprintf(stderr, "Bad protocol number %.5s.\n", line);
            return -1;
        }
        e++;
        r_device *r_dev = NULL;
        for (void **iter = demod->r_devs.elems; iter && *iter; ++iter) {
            r_device *r_dev_i = *iter;
            if (r_dev_i->protocol_num == d) {
                r_dev = r_dev_i;
                break;
            }
        }
        if (!r_dev) {
            fprintf(stderr, "Unknown protocol number %u.\n", d);
            return -1;
        }
        if (cfg->verbosity)
            fprintf(stderr, "Verifying test data with device %s.\n", r_dev->name);
        r += pulse_demod_string(e, r_dev);
        return r;
    }
    
    // don't allow testing with all decoders
    fprintf(stderr, "Testing with all decoders is not supported\n");
    return -1;
}

static r_cfg_t g_cfg;

int main(int argc, char **argv)
{
    int r = 0;
    unsigned i;
    struct dm_state *demod;
    r_cfg_t *cfg = &g_cfg;

    fprintf(stderr, "rtl_fuzz, a fuzzing target for the rtl_433 decoders\n");

    r_init_cfg(cfg);

    demod = cfg->demod;

    r_device r_devices[] = {
#define DECL(name) name,
            DEVICES
#undef DECL
    };

    cfg->num_r_devices = sizeof(r_devices) / sizeof(*r_devices);
    for (i = 0; i < cfg->num_r_devices; i++) {
        r_devices[i].protocol_num = i + 1;
    }
    cfg->devices = r_devices;

    parse_conf_args(cfg, argc, argv);
    add_json_output(cfg, arg_param(NULL));

    cfg->report_time = REPORT_TIME_OFF;

    // register default decoders if nothing is configured
    if (!cfg->no_default_devices) {
        register_all_protocols(cfg, 0); // register all defaults
    }
    else {
        update_protocols(cfg);
    }

    // Streaming test data
    char line[INPUT_LINE_MAX];
    fprintf(stderr, "Reading test data from stdin\n");
    if (fgets(line, INPUT_LINE_MAX, stdin)) {
        r = process_test_data(cfg, demod, line);
    }
    r_free_cfg(cfg);
    return r < 0 ? 1 : 0;
}
