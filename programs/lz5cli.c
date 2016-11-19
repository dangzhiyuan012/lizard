/*
  LZ5cli - LZ5 Command Line Interface
  Copyright (C) Yann Collet 2011-2015

  GPL v2 License

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

  You can contact the author at :
  - LZ5 source repository : https://github.com/inikep/lz5
*/
/*
  Note : this is stand-alone program.
  It is not part of LZ5 compression library, it is a user program of the LZ5 library.
  The license of LZ5 library is BSD.
  The license of xxHash library is BSD.
  The license of this compression CLI program is GPLv2.
*/


/**************************************
*  Compiler Options
***************************************/
/* cf. http://man7.org/linux/man-pages/man7/feature_test_macros.7.html */
#define _XOPEN_VERSION 600 /* POSIX.2001, for fileno() within <stdio.h> on unix */


/****************************
*  Includes
*****************************/
#include "util.h"     /* Compiler options, UTIL_HAS_CREATEFILELIST */
#include <stdio.h>    /* fprintf, getchar */
#include <stdlib.h>   /* exit, calloc, free */
#include <string.h>   /* strcmp, strlen */
#include "bench.h"    /* BMK_benchFile, BMK_SetNbIterations, BMK_SetBlocksize, BMK_SetPause */
#include "lz5io.h"    /* LZ5IO_compressFilename, LZ5IO_decompressFilename, LZ5IO_compressMultipleFilenames */
#include "lz5_compress.h" /* LZ5HC_DEFAULT_CLEVEL, LZ5_VERSION_STRING */


/*-************************************
*  OS-specific Includes
**************************************/
#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) || (defined(__APPLE__) && defined(__MACH__)) || defined(__DJGPP__)  /* https://sourceforge.net/p/predef/wiki/OperatingSystems/ */
#  include <unistd.h>   /* isatty */
#  define IS_CONSOLE(stdStream) isatty(fileno(stdStream))
#elif defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__)
#  include <io.h>       /* _isatty */
#  define IS_CONSOLE(stdStream) _isatty(_fileno(stdStream))
#else
#  define IS_CONSOLE(stdStream) 0
#endif


/*****************************
*  Constants
******************************/
#define COMPRESSOR_NAME "LZ5 command line interface"
#define AUTHOR "Y.Collet & P.Skibinski"
#define WELCOME_MESSAGE "%s %i-bit %s by %s (%s)\n", COMPRESSOR_NAME, (int)(sizeof(void*)*8), LZ5_VERSION_STRING, AUTHOR, __DATE__
#define LZ5_EXTENSION ".lz5"
#define LZ5CAT "lz5cat"
#define UNLZ5 "unlz5"

#define KB *(1U<<10)
#define MB *(1U<<20)
#define GB *(1U<<30)

#define LZ5_BLOCKSIZEID_DEFAULT 4


/*-************************************
*  Macros
***************************************/
#define DISPLAY(...)           fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...)   if (displayLevel>=l) { DISPLAY(__VA_ARGS__); }
static unsigned displayLevel = 2;   /* 0 : no display ; 1: errors only ; 2 : downgradable normal ; 3 : non-downgradable normal; 4 : + information */


/*-************************************
*  Exceptions
***************************************/
#define DEBUG 0
#define DEBUGOUTPUT(...) if (DEBUG) DISPLAY(__VA_ARGS__);
#define EXM_THROW(error, ...)                                             \
{                                                                         \
    DEBUGOUTPUT("Error defined at %s, line %i : \n", __FILE__, __LINE__); \
    DISPLAYLEVEL(1, "Error %i : ", error);                                \
    DISPLAYLEVEL(1, __VA_ARGS__);                                         \
    DISPLAYLEVEL(1, "\n");                                                \
    exit(error);                                                          \
}


/*-************************************
*  Version modifiers
***************************************/
#define EXTENDED_ARGUMENTS
#define EXTENDED_HELP
#define EXTENDED_FORMAT
#define DEFAULT_COMPRESSOR   LZ5IO_compressFilename
#define DEFAULT_DECOMPRESSOR LZ5IO_decompressFilename
int LZ5IO_compressFilename_Legacy(const char* input_filename, const char* output_filename, int compressionlevel);   /* hidden function */


/*-***************************
*  Functions
*****************************/
static int usage(const char* exeName)
{
    DISPLAY( "Usage :\n");
    DISPLAY( "      %s [arg] [input] [output]\n", exeName);
    DISPLAY( "\n");
    DISPLAY( "input   : a filename\n");
    DISPLAY( "          with no FILE, or when FILE is - or %s, read standard input\n", stdinmark);
    DISPLAY( "Arguments :\n");
    DISPLAY( " -10...-19 : compression method fastLZ4 = 16-bit bytewise codewords\n");
    DISPLAY( "             higher number == more compression but slower\n");
    DISPLAY( " -20...-29 : compression method LZ5v2 = 24-bit bytewise codewords\n");
    DISPLAY( " -30...-39 : compression method fastLZ4 + Huffman\n");
    DISPLAY( " -40...-49 : compression method LZ5v2 + Huffman\n");
    DISPLAY( " -d     : decompression (default for %s extension)\n", LZ5_EXTENSION);
    DISPLAY( " -z     : force compression\n");
    DISPLAY( " -f     : overwrite output without prompting \n");
    DISPLAY( "--rm    : remove source file(s) after successful de/compression \n");
    DISPLAY( " -h/-H  : display help/long help and exit\n");
    return 0;
}

static int usage_advanced(const char* exeName)
{
    DISPLAY(WELCOME_MESSAGE);
    usage(exeName);
    DISPLAY( "\n");
    DISPLAY( "Advanced arguments :\n");
    DISPLAY( " -V     : display Version number and exit\n");
    DISPLAY( " -v     : verbose mode\n");
    DISPLAY( " -q     : suppress warnings; specify twice to suppress errors too\n");
    DISPLAY( " -c     : force write to standard output, even if it is the console\n");
    DISPLAY( " -t     : test compressed file integrity\n");
    DISPLAY( " -m     : multiple input files (implies automatic output filenames)\n");
#ifdef UTIL_HAS_CREATEFILELIST
    DISPLAY( " -r     : operate recursively on directories (sets also -m)\n");
#endif
    DISPLAY( " -l     : compress using Legacy format (Linux kernel compression)\n");
    DISPLAY( " -B#    : Block size [1-7] = 128KB, 256KB, 1MB, 4MB, 16MB, 64MB, 256MB (default : 4 = 4MB)\n");
    DISPLAY( " -BD    : Block dependency (improve compression ratio)\n");
    /* DISPLAY( " -BX    : enable block checksum (default:disabled)\n");   *//* Option currently inactive */
    DISPLAY( "--no-frame-crc : disable stream checksum (default:enabled)\n");
    DISPLAY( "--content-size : compressed frame includes original size (default:not present)\n");
    DISPLAY( "--[no-]sparse  : sparse mode (default:enabled on file, disabled on stdout)\n");
    DISPLAY( "Benchmark arguments :\n");
    DISPLAY( " -b#    : benchmark file(s), using # compression level (default : 1) \n");
    DISPLAY( " -e#    : test all compression levels from -bX to # (default : 1)\n");
    DISPLAY( " -i#    : minimum evaluation time in seconds (default : 3s)\n");
    DISPLAY( " -B#    : cut file into independent blocks of size # bytes [32+]\n");
    DISPLAY( "                      or predefined block size [1-7] (default: 4)\n");
    EXTENDED_HELP;
    return 0;
}

static int usage_longhelp(const char* exeName)
{
    usage_advanced(exeName);
    DISPLAY( "\n");
    DISPLAY( "****************************\n");
    DISPLAY( "***** Advanced comment *****\n");
    DISPLAY( "****************************\n");
    DISPLAY( "\n");
    DISPLAY( "Which values can [output] have ? \n");
    DISPLAY( "---------------------------------\n");
    DISPLAY( "[output] : a filename \n");
    DISPLAY( "          '%s', or '-' for standard output (pipe mode)\n", stdoutmark);
    DISPLAY( "          '%s' to discard output (test mode) \n", NULL_OUTPUT);
    DISPLAY( "[output] can be left empty. In this case, it receives the following value :\n");
    DISPLAY( "          - if stdout is not the console, then [output] = stdout \n");
    DISPLAY( "          - if stdout is console : \n");
    DISPLAY( "               + for compression, output to filename%s \n", LZ5_EXTENSION);
    DISPLAY( "               + for decompression, output to filename without '%s'\n", LZ5_EXTENSION);
    DISPLAY( "                    > if input filename has no '%s' extension : error \n", LZ5_EXTENSION);
    DISPLAY( "\n");
    DISPLAY( "stdin, stdout and the console : \n");
    DISPLAY( "--------------------------------\n");
    DISPLAY( "To protect the console from binary flooding (bad argument mistake)\n");
    DISPLAY( "%s will refuse to read from console, or write to console \n", exeName);
    DISPLAY( "except if '-c' command is specified, to force output to console \n");
    DISPLAY( "\n");
    DISPLAY( "Simple example :\n");
    DISPLAY( "----------------\n");
    DISPLAY( "1 : compress 'filename' fast, using default output name 'filename.lz5'\n");
    DISPLAY( "          %s filename\n", exeName);
    DISPLAY( "\n");
    DISPLAY( "Short arguments can be aggregated. For example :\n");
    DISPLAY( "----------------------------------\n");
    DISPLAY( "2 : compress 'filename' in high compression mode, overwrite output if exists\n");
    DISPLAY( "          %s -9 -f filename \n", exeName);
    DISPLAY( "    is equivalent to :\n");
    DISPLAY( "          %s -9f filename \n", exeName);
    DISPLAY( "\n");
    DISPLAY( "%s can be used in 'pure pipe mode'. For example :\n", exeName);
    DISPLAY( "-------------------------------------\n");
    DISPLAY( "3 : compress data stream from 'generator', send result to 'consumer'\n");
    DISPLAY( "          generator | %s | consumer \n", exeName);
    return 0;
}

static int badusage(const char* exeName)
{
    DISPLAYLEVEL(1, "Incorrect parameters\n");
    if (displayLevel >= 1) usage(exeName);
    exit(1);
}


static void waitEnter(void)
{
    DISPLAY("Press enter to continue...\n");
    (void)getchar();
}


/*! readU32FromChar() :
    @return : unsigned integer value reach from input in `char` format
    Will also modify `*stringPtr`, advancing it to position where it stopped reading.
    Note : this function can overflow if result > MAX_UINT */
static unsigned readU32FromChar(const char** stringPtr)
{
    unsigned result = 0;
    while ((**stringPtr >='0') && (**stringPtr <='9'))
        result *= 10, result += **stringPtr - '0', (*stringPtr)++ ;
    return result;
}

typedef enum { om_auto, om_compress, om_decompress, om_test, om_bench } operationMode_e;

int main(int argc, const char** argv)
{
    int i,
        cLevel=1,
        cLevelLast=1,
        forceStdout=0,
        main_pause=0,
        multiple_inputs=0,
        operationResult=0;
    operationMode_e mode = om_auto;
    const char* input_filename = NULL;
    const char* output_filename= NULL;
    char* dynNameSpace = NULL;
    const char** inFileNames = (const char**) calloc(argc, sizeof(char*));
    unsigned ifnIdx=0;
    const char nullOutput[] = NULL_OUTPUT;
    const char extension[] = LZ5_EXTENSION;
    size_t blockSize = LZ5IO_setBlockSizeID(LZ5_BLOCKSIZEID_DEFAULT);
    const char* const exeName = argv[0];
#ifdef UTIL_HAS_CREATEFILELIST
    const char** extendedFileList = NULL;
    char* fileNamesBuf = NULL;
    unsigned fileNamesNb, recursive=0;
#endif

    /* Init */
    if (inFileNames==NULL) {
        DISPLAY("Allocation error : not enough memory \n");
        return 1;
    }
    LZ5IO_setOverwrite(0);

    /* lz5cat predefined behavior */
    if (!strcmp(exeName, LZ5CAT)) {
        mode = om_decompress;
        LZ5IO_setOverwrite(1);
        forceStdout=1;
        output_filename=stdoutmark;
        displayLevel=1;
        multiple_inputs=1;
    }
    if (!strcmp(exeName, UNLZ5)) { mode = om_decompress; }

    /* command switches */
    for(i=1; i<argc; i++) {
        const char* argument = argv[i];

        if(!argument) continue;   /* Protection if argument empty */

        /* Short commands (note : aggregated short commands are allowed) */
        if (argument[0]=='-') {
            /* '-' means stdin/stdout */
            if (argument[1]==0) {
                if (!input_filename) input_filename=stdinmark;
                else output_filename=stdoutmark;
                continue;
            }

            /* long commands (--long-word) */
            if (argument[1]=='-') {
                if (!strcmp(argument,  "--compress")) { mode = om_compress; continue; }
                if ((!strcmp(argument, "--decompress"))
                    || (!strcmp(argument, "--uncompress"))) { mode = om_decompress; continue; }
                if (!strcmp(argument,  "--multiple")) { multiple_inputs = 1; continue; }
                if (!strcmp(argument,  "--test")) { mode = om_test; continue; }
                if (!strcmp(argument,  "--force")) { LZ5IO_setOverwrite(1); continue; }
                if (!strcmp(argument,  "--no-force")) { LZ5IO_setOverwrite(0); continue; }
                if ((!strcmp(argument, "--stdout"))
                    || (!strcmp(argument, "--to-stdout"))) { forceStdout=1; output_filename=stdoutmark; continue; }
                if (!strcmp(argument,  "--frame-crc")) { LZ5IO_setStreamChecksumMode(1); continue; }
                if (!strcmp(argument,  "--no-frame-crc")) { LZ5IO_setStreamChecksumMode(0); continue; }
                if (!strcmp(argument,  "--content-size")) { LZ5IO_setContentSize(1); continue; }
                if (!strcmp(argument,  "--no-content-size")) { LZ5IO_setContentSize(0); continue; }
                if (!strcmp(argument,  "--sparse")) { LZ5IO_setSparseFile(2); continue; }
                if (!strcmp(argument,  "--no-sparse")) { LZ5IO_setSparseFile(0); continue; }
                if (!strcmp(argument,  "--verbose")) { displayLevel++; continue; }
                if (!strcmp(argument,  "--quiet")) { if (displayLevel) displayLevel--; continue; }
                if (!strcmp(argument,  "--version")) { DISPLAY(WELCOME_MESSAGE); return 0; }
                if (!strcmp(argument,  "--help")) { usage_advanced(exeName); goto _cleanup; }
                if (!strcmp(argument,  "--keep")) { LZ5IO_setRemoveSrcFile(0); continue; }   /* keep source file (default) */
                if (!strcmp(argument,  "--rm")) { LZ5IO_setRemoveSrcFile(1); continue; }
            }

            while (argument[1]!=0) {
                argument ++;

                if ((*argument>='0') && (*argument<='9')) {
                    cLevel = readU32FromChar(&argument);
                    argument--;
                    continue;
                }


                switch(argument[0])
                {
                    /* Display help */
                case 'V': DISPLAY(WELCOME_MESSAGE); goto _cleanup;   /* Version */
                case 'h': usage_advanced(exeName); goto _cleanup;
                case 'H': usage_longhelp(exeName); goto _cleanup;

                case 'e':
                    argument++;
                    cLevelLast = readU32FromChar(&argument);
                    argument--;
                    break;

                    /* Compression (default) */
                case 'z': mode = om_compress; break;

                    /* Decoding */
                case 'd': mode = om_decompress; break;

                    /* Force stdout, even if stdout==console */
                case 'c': forceStdout=1; output_filename=stdoutmark; break;

                    /* Test integrity */
                case 't': mode = om_test; break;

                    /* Overwrite */
                case 'f': LZ5IO_setOverwrite(1); break;

                    /* Verbose mode */
                case 'v': displayLevel++; break;

                    /* Quiet mode */
                case 'q': if (displayLevel) displayLevel--; break;

                    /* keep source file (default anyway, so useless) (for xz/lzma compatibility) */
                case 'k': LZ5IO_setRemoveSrcFile(0); break;

                    /* Modify Block Properties */
                case 'B':
                    while (argument[1]!=0) {
                        int exitBlockProperties=0;
                        switch(argument[1])
                        {
                        case 'D': LZ5IO_setBlockMode(LZ5IO_blockLinked); argument++; break;
                        case 'X': LZ5IO_setBlockChecksumMode(1); argument ++; break;   /* disabled by default */
                        default :
                            if (argument[1] < '0' || argument[1] > '9') {
                                exitBlockProperties=1;
                                break;
                            } else {
                                unsigned B;
                                argument++;
                                B = readU32FromChar(&argument);
                                argument--;
                                if (B < 1) badusage(exeName);
                                if (B <= 7) {
                                    blockSize = LZ5IO_setBlockSizeID(B);
                                    BMK_SetBlockSize(blockSize);
                                    DISPLAYLEVEL(2, "using blocks of size %u KB \n", (U32)(blockSize>>10));
                                } else {
                                    if (B < 32) badusage(exeName);
                                    BMK_SetBlockSize(B);
                                    if (B >= 1024) {
                                        DISPLAYLEVEL(2, "bench: using blocks of size %u KB \n", (U32)(B>>10));
                                    } else {
                                        DISPLAYLEVEL(2, "bench: using blocks of size %u bytes \n", (U32)(B));
                                    }
                                }
                                break;
                            }
                        }
                        if (exitBlockProperties) break;
                    }
                    break;

                    /* Benchmark */
                case 'b': mode = om_bench; multiple_inputs=1;
                    break;

#ifdef UTIL_HAS_CREATEFILELIST
                        /* recursive */
                case 'r': recursive=1;  /* without break */
#endif
                    /* Treat non-option args as input files.  See https://code.google.com/p/lz5/issues/detail?id=151 */
                case 'm': multiple_inputs=1;
                    break;

                    /* Modify Nb Seconds (benchmark only) */
                case 'i':
                    {   unsigned iters;
                        argument++;
                        iters = readU32FromChar(&argument);
                        argument--;
                        BMK_setNotificationLevel(displayLevel);
                        BMK_SetNbSeconds(iters);
                    }
                    break;

                    /* Pause at the end (hidden option) */
                case 'p': main_pause=1; break;

                    /* Specific commands for customized versions */
                EXTENDED_ARGUMENTS;

                    /* Unrecognised command */
                default : badusage(exeName);
                }
            }
            continue;
        }

        /* Store in *inFileNames[] if -m is used. */
        if (multiple_inputs) { inFileNames[ifnIdx++]=argument; continue; }

        /* Store first non-option arg in input_filename to preserve original cli logic. */
        if (!input_filename) { input_filename=argument; continue; }

        /* Second non-option arg in output_filename to preserve original cli logic. */
        if (!output_filename) {
            output_filename=argument;
            if (!strcmp (output_filename, nullOutput)) output_filename = nulmark;
            continue;
        }

        /* 3rd non-option arg should not exist */
        DISPLAYLEVEL(1, "Warning : %s won't be used ! Do you want multiple input files (-m) ? \n", argument);
    }

    DISPLAYLEVEL(3, WELCOME_MESSAGE);
    if ((mode == om_compress) || (mode == om_bench)) DISPLAYLEVEL(4, "Blocks size : %i KB\n", (U32)(blockSize>>10));

    if (multiple_inputs) {
        input_filename = inFileNames[0];
#ifdef UTIL_HAS_CREATEFILELIST
        if (recursive) {  /* at this stage, filenameTable is a list of paths, which can contain both files and directories */
            extendedFileList = UTIL_createFileList(inFileNames, ifnIdx, &fileNamesBuf, &fileNamesNb);
            if (extendedFileList) {
                unsigned u;
                for (u=0; u<fileNamesNb; u++) DISPLAYLEVEL(4, "%u %s\n", u, extendedFileList[u]);
                free((void*)inFileNames);
                inFileNames = extendedFileList;
                ifnIdx = fileNamesNb;
            }
        }
#endif
    }

    /* benchmark and test modes */
    if (mode == om_bench) {
        BMK_setNotificationLevel(displayLevel);
        operationResult = BMK_benchFiles(inFileNames, ifnIdx, cLevel, cLevelLast);
        goto _cleanup;
    }

    if (mode == om_test) {
        LZ5IO_setTestMode(1);
        output_filename = nulmark;
        mode = om_decompress;   /* defer to decompress */
    }

    /* compress or decompress */
    if (!input_filename) input_filename = stdinmark;
    /* Check if input is defined as console; trigger an error in this case */
    if (!strcmp(input_filename, stdinmark) && IS_CONSOLE(stdin) ) {
        DISPLAYLEVEL(1, "refusing to read from a console\n");
        exit(1);
    }

    /* No output filename ==> try to select one automatically (when possible) */
    while (!output_filename) {
        if (!IS_CONSOLE(stdout)) { output_filename=stdoutmark; break; }   /* Default to stdout whenever possible (i.e. not a console) */
        if (mode == om_auto) {  /* auto-determine compression or decompression, based on file extension */
            size_t const inSize  = strlen(input_filename);
            size_t const extSize = strlen(LZ5_EXTENSION);
            size_t const extStart= (inSize > extSize) ? inSize-extSize : 0;
            if (!strcmp(input_filename+extStart, LZ5_EXTENSION)) mode = om_decompress;
            else mode = om_compress;
        }
        if (mode == om_compress) {   /* compression to file */
            size_t const l = strlen(input_filename);
            dynNameSpace = (char*)calloc(1,l+5);
            if (dynNameSpace==NULL) { perror(exeName); exit(1); }
            strcpy(dynNameSpace, input_filename);
            strcat(dynNameSpace, LZ5_EXTENSION);
            output_filename = dynNameSpace;
            DISPLAYLEVEL(2, "Compressed filename will be : %s \n", output_filename);
            break;
        }
        if (mode == om_decompress) {/* decompression to file (automatic name will work only if input filename has correct format extension) */
            size_t outl;
            size_t const inl = strlen(input_filename);
            dynNameSpace = (char*)calloc(1,inl+1);
            if (dynNameSpace==NULL) { perror(exeName); exit(1); }
            strcpy(dynNameSpace, input_filename);
            outl = inl;
            if (inl>4)
                while ((outl >= inl-4) && (input_filename[outl] ==  extension[outl-inl+4])) dynNameSpace[outl--]=0;
            if (outl != inl-5) { DISPLAYLEVEL(1, "Cannot determine an output filename\n"); badusage(exeName); }
            output_filename = dynNameSpace;
            DISPLAYLEVEL(2, "Decoding file %s \n", output_filename);
        }
        break;
    }

    /* Check if output is defined as console; trigger an error in this case */
    if (!strcmp(output_filename,stdoutmark) && IS_CONSOLE(stdout) && !forceStdout) {
        DISPLAYLEVEL(1, "refusing to write to console without -c\n");
        exit(1);
    }

    /* Downgrade notification level in stdout and multiple file mode */
    if (!strcmp(output_filename,stdoutmark) && (displayLevel==2)) displayLevel=1;
    if ((multiple_inputs) && (displayLevel==2)) displayLevel=1;

    /* IO Stream/File */
    LZ5IO_setNotificationLevel(displayLevel);
    if (mode == om_decompress) {
        if (multiple_inputs)
            operationResult = LZ5IO_decompressMultipleFilenames(inFileNames, ifnIdx, !strcmp(output_filename,stdoutmark) ? stdoutmark : LZ5_EXTENSION);
        else
            operationResult = LZ5IO_decompressFilename(input_filename, output_filename);
    } else {   /* compression is default action */
        {
            if (multiple_inputs)
                operationResult = LZ5IO_compressMultipleFilenames(inFileNames, ifnIdx, LZ5_EXTENSION, cLevel);
            else
                operationResult = LZ5IO_compressFilename(input_filename, output_filename, cLevel);
        }
    }

_cleanup:
    if (main_pause) waitEnter();
    if (dynNameSpace) free(dynNameSpace);
#ifdef UTIL_HAS_CREATEFILELIST
    if (extendedFileList)
        UTIL_freeFileList(extendedFileList, fileNamesBuf);
    else
#endif
        free((void*)inFileNames);
    return operationResult;
}
