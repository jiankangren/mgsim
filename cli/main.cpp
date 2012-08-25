#ifdef HAVE_CONFIG_H
#include "sys_config.h"
#endif

#include "arch/MGSystem.h"
#include "simreadline.h"
#include "commands.h"
#include "sim/config.h"

#ifdef ENABLE_MONITOR
# include "sim/monitor.h"
#endif

#include <sstream>
#include <iostream>
#include <limits>

#ifdef USE_SDL
#include <SDL.h>
#endif

#include <argp.h>

using namespace Simulator;
using namespace std;

struct ProgramConfig
{
    unsigned int                     m_areaTech;
    string                           m_configFile;
    string                           m_symtableFile;
    bool                             m_enableMonitor;
    bool                             m_interactive;
    bool                             m_terminate;
    bool                             m_dumpconf;
    bool                             m_quiet;
    bool                             m_dumpvars;
    vector<string>                   m_printvars;
    bool                             m_earlyquit;
    ConfigMap                        m_overrides;
    vector<string>                   m_extradevs;    
    vector<pair<RegAddr, string> >   m_loads;
    vector<pair<RegAddr, RegValue> > m_regs;
    bool                             m_dumptopo;
    string                           m_topofile;
    bool                             m_dumpnodeprops;
    bool                             m_dumpedgeprops;
    vector<string>                   m_argv;
};

extern "C" 
{
const char *argp_program_version =
    "mgsim " PACKAGE_VERSION "\n"
    "Copyright (C) 2008,2009,2010,2011 Universiteit van Amsterdam.\n"
    "\n"
    "Written by Mike Lankamp. Maintained by the Microgrid project.";

const char *argp_program_bug_address =
    PACKAGE_BUGREPORT;
}

static const char *mgsim_doc =
    "This program simulates Microgrid-based systems."
    "\v" /* separates top and bottom part of --help generated by argp. */
    "The first non-option argument is treated as a file to load as "
    "a bootable ROM. All non-option arguments are also stored as strings "
    "in a data ROM. For more advanced code/data arrangements, use "
    "configuration options to set up ROM devices and memory ranges."
    "\n\n"
    "For more information, see mgsimdoc(1).";

static const struct argp_option mgsim_options[] =
{
    { "interactive", 'i', 0, 0, "Start the simulator in interactive mode.", 0 },

    { 0, 'R', "NUM VALUE", 0, "Store the integer VALUE in the specified register of the initial thread.", 1 },
    { 0, 'F', "NUM VALUE", 0, "Store the float VALUE in the specified FP register of the initial thread.", 1 },
    { 0, 'L', "NUM FILE", 0, "Create an ActiveROM component with the contents of FILE and store the address in the specified register of the initial thread.", 1 },

    { "config", 'c', "FILE", 0, "Read configuration from FILE.", 2 },
    { "dump-configuration", 'd', 0, 0, "Dump configuration to standard error prior to program startup.", 2 },
    { "override", 'o', "NAME=VAL", 0, "Overrides the configuration option NAME with value VAL. Can be specified multiple times.", 2 },
    { "symtable", 's', "FILE", 0, "Read symbol from FILE. (generate with nm -P)\n", 2 },

    { "do-nothing", 'n', 0, 0, "Exit before the program starts, but after the system is configured.", 3 },
    { "quiet", 'q', 0, 0, "Do not print simulation statistics after execution.", 3 },
    { "terminate", 't', 0, 0, "Terminate the simulator upon an exception, instead of dropping to the interactive prompt.", 3 },

#ifdef ENABLE_CACTI
    { "area", 'a', "VAL", 0, "Dump area information prior to program startup using CACTI. Assume technology is VAL nanometers.", 4 },
#endif

    { "list-mvars", 'l', 0, 0, "Dump list of monitor variables prior to program startup.", 5 },
    { "print-final-mvars", 'p', "PATTERN", 0, "Print the value of all monitoring variables matching PATTERN. Can be specified multiple times.", 5 },

    { "dump-topology", 'T', "FILE", 0, "Dump the grid topology to FILE prior to program startup.", 6 },
    { "no-node-properties", 10, 0, 0, "Do not print component properties in the topology dump.", 6 },
    { "no-edge-properties", 11, 0, 0, "Do not print link properties in the topology output.", 6 },

#ifdef ENABLE_MONITOR
    { "monitor", 'm', 0, 0, "Enable asynchronous simulation monitoring (configure with -o MonitorSampleVariables).", 7 },
#endif

    { 0, 0, 0, 0, 0, 0 }
};

static error_t mgsim_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct ProgramConfig &config = *(struct ProgramConfig*)state->input;
    
    switch (key)
    {
    case 'a':
    {
        char* endptr;
        unsigned int tech = strtoul(arg, &endptr, 0);
        if (*endptr != '\0') {
            throw runtime_error("Error: unable to parse technology size");
        } else if (tech < 1) {
            throw runtime_error("Error: technology size must be >= 1 nm");
        } else {
            config.m_areaTech = tech;
        }
    } 
    break;
    case 'c': config.m_configFile = arg; break;
    case 'i': config.m_interactive = true; break;
    case 't': config.m_terminate = true; break;
    case 'q': config.m_quiet = true; break;
    case 's': config.m_symtableFile = arg; break;
    case 'd': config.m_dumpconf = true; break;
    case 'm': config.m_enableMonitor = true; break;
    case 'l': config.m_dumpvars = true; break;
    case 'p': config.m_printvars.push_back(arg); break;
    case 'T': config.m_dumptopo = true; config.m_topofile = arg; break;
    case 10 : config.m_dumpnodeprops = false; break;
    case 11 : config.m_dumpedgeprops = false; break;
    case 'n': config.m_earlyquit = true; break;
    case 'o':
    {
            string sarg = arg;
            string::size_type eq = sarg.find_first_of("=");
            if (eq == string::npos) {
                throw runtime_error("Error: malformed configuration override syntax: " + sarg);
            }
            string name = sarg.substr(0, eq);

            // push overrides in inverse order, so that the latest
            // specified in the command line has higher priority in
            // matching.
            config.m_overrides.insert(name, sarg.substr(eq + 1));
    }
    break;
    case 'L':
    {
        if (state->next == state->argc) {
            throw runtime_error("Error: -L<N> expected filename");
        }
        string filename(state->argv[state->next++]);
        string regnum(arg);
        char* endptr; 
        unsigned long index = strtoul(regnum.c_str(), &endptr, 0); 
        if (*endptr != '\0') { 
            throw runtime_error("Error: invalid register specifier in option: " + regnum); 
        } 
        RegAddr  regaddr = MAKE_REGADDR(RT_INTEGER, index);

        string devname = "file" + regnum;
        config.m_extradevs.push_back(devname);
        string cfgprefix = devname + ":";
        config.m_overrides.append(cfgprefix + "Type", "AROM");
        config.m_overrides.append(cfgprefix + "ROMContentSource", "RAW");
        config.m_overrides.append(cfgprefix + "ROMFileName", filename);
        config.m_loads.push_back(make_pair(regaddr, devname)); 
    }
    break;
    case 'R': case 'F':
    {
        if (state->next == state->argc) {
            throw runtime_error("Error: -R/-F expected register value");
        }

        stringstream value;
        value << state->argv[state->next++];

        RegAddr  addr;
        RegValue val;

        char* endptr;
        unsigned long index = strtoul(arg, &endptr, 0);
        if (*endptr != '\0') {
            throw runtime_error("Error: invalid register specifier in option");
        }
                
        if (key == 'R') {
            value >> *(signed Integer*)&val.m_integer;
            addr = MAKE_REGADDR(RT_INTEGER, index);
        } else {
            double f;
            value >> f;
            val.m_float.fromfloat(f);
            addr = MAKE_REGADDR(RT_FLOAT, index);
        }
        if (value.fail()) {
            throw runtime_error("Error: invalid value for register");
        }
        val.m_state = RST_FULL;
        config.m_regs.push_back(make_pair(addr, val));
    }
    break;
    case ARGP_KEY_ARG: /* extra arguments */
    {
        if (config.m_argv.empty())
        {
            cerr << "Warning: converting first extra argument to -o *:ROMFileName=" << arg << endl;
            config.m_overrides.append("*:ROMFileName", arg);
        }
        config.m_argv.push_back(arg);
    }
    break;
    case ARGP_KEY_NO_ARGS:
        argp_usage (state);
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}


static struct argp argp = {
    mgsim_options /* options */,
    mgsim_parse_opt /* parser */,
    NULL /* args_doc */,
    mgsim_doc /* doc */,
    NULL /* children */,
    NULL /* help filter */,
    NULL /* argp domain */
};

void PrintFinalVariables(const ProgramConfig& cfg)
{
    if (!cfg.m_printvars.empty())
    {
        std::cout << "### begin end-of-simulation variables" << std::endl;
        for (size_t i = 0; i < cfg.m_printvars.size(); ++i)
            ReadSampleVariables(cout, cfg.m_printvars[i]);
        std::cout << "### end end-of-simulation variables" << std::endl;
    }
}

#ifdef USE_SDL
extern "C"
#endif
int main(int argc, char** argv)
{
    srand(time(NULL));
    
    try
    {
        // Parse command line arguments
        ProgramConfig config;
        config.m_areaTech = 0;
        config.m_configFile = MGSIM_CONFIG_PATH;
        config.m_enableMonitor = false;
        config.m_interactive = false;
        config.m_terminate = false;
        config.m_dumpconf = false;
        config.m_quiet = false;
        config.m_dumpvars = false;
        config.m_earlyquit = false;
        config.m_dumptopo = false;
        config.m_dumpnodeprops = true;
        config.m_dumpedgeprops = true;

        argp_parse(&argp, argc, argv, 0, 0, &config);
        
        if (config.m_quiet)
        {
            config.m_overrides.append("*.ROMVerboseLoad", "false");
        }

        if (config.m_interactive)
        {
            // Interactive mode
            std::clog << argp_program_version << std::endl;
        }

        // Read configuration from file
        Config configfile(config.m_configFile, config.m_overrides, config.m_argv);

        if (config.m_dumpconf)
        {
            // Printing the configuration early, in case constructing the
            // system (below) fails.
            std::clog << "### simulator version: " PACKAGE_VERSION << std::endl;
            configfile.dumpConfiguration(std::clog, config.m_configFile);
        }
        
        // Create the system
        MGSystem sys(configfile, 
                     config.m_symtableFile,
                     config.m_regs, 
                     config.m_loads, 
                     config.m_extradevs, 
                     !config.m_interactive, 
                     !config.m_earlyquit);

#ifdef ENABLE_MONITOR
        string mo_mdfile = configfile.getValueOrDefault<string>("MonitorMetadataFile", "mgtrace.md");
        string mo_tfile = configfile.getValueOrDefault<string>("MonitorTraceFile", "mgtrace.out");
        Monitor mo(sys, config.m_enableMonitor, 
                   mo_mdfile, config.m_earlyquit ? "" : mo_tfile, !config.m_interactive);
#endif

        if (config.m_dumpconf)
        {
            // we also print the cache, which expands all effectively
            // looked up configuration values after the system
            // was constructed successfully.
            configfile.dumpConfigurationCache(std::clog);
        }
        if (config.m_dumpvars)
        {
            std::clog << "### begin monitor variables" << std::endl;
            ListSampleVariables(std::clog);
            std::clog << "### end monitor variables" << std::endl;
        }

        if (config.m_areaTech > 0)
        {
            std::clog << "### begin area information" << std::endl;
#ifdef ENABLE_CACTI
            sys.DumpArea(std::cout, config.m_areaTech);
#else
            std::clog << "# Warning: CACTI not enabled; reconfigure with --enable-cacti" << std::endl;
#endif
            std::clog << "### end area information" << std::endl;
        }

        if (config.m_dumptopo)
        {
            ofstream of(config.m_topofile.c_str(), ios::out);
            configfile.dumpComponentGraph(of, config.m_dumpnodeprops, config.m_dumpedgeprops);
            of.close();
        }

        if (config.m_earlyquit)
            exit(0);

        bool interactive = config.m_interactive;
        if (!interactive)
        {
            // Non-interactive mode; run and dump cycle count
            try
            {
#ifdef ENABLE_MONITOR
                mo.start();
#endif
                StepSystem(sys, INFINITE_CYCLES);
#ifdef ENABLE_MONITOR
                mo.stop();
#endif
                
                if (!config.m_quiet)
                {
                    clog << "### begin end-of-simulation statistics" << endl;
                    sys.PrintAllStatistics(clog);
                    clog << "### end end-of-simulation statistics" << endl;
                }
            }
            catch (const exception& e)
            {
#ifdef ENABLE_MONITOR
                mo.stop();
#endif
                if (config.m_terminate) 
                {
                    // We do not want to go to interactive mode,
                    // rethrow so it abort the program.
                    PrintFinalVariables(config);
                    throw;
                }
                
                PrintException(cerr, e);
                
                // When we get an exception in non-interactive mode,
                // jump into interactive mode
                interactive = true;
            }
        }
        
        if (interactive)
        {
            // Command loop
            cout << endl;
            CommandLineReader clr;
            cli_context ctx = { clr, sys
#ifdef ENABLE_MONITOR
                                , mo 
#endif
            };

            while (HandleCommandLine(ctx) == false)
                /* just loop */;
        }

        PrintFinalVariables(config);
    }
    catch (const exception& e)
    {
        PrintException(cerr, e);
        return 1;
    }
    
    return 0;
}
