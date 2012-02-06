/*
 * Copyright 2010, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <unistd.h>

#if defined(__HOST__)
  #if defined(__cplusplus)
    extern "C" {
  #endif
      extern char *TARGET_TRIPLE_STRING;
  #if defined(__cplusplus)
    };
  #endif
#define GETOPT_OPTIONS  "C:R"
#else
#define GETOPT_OPTIONS  "R"
#endif

#include <bcc/bcc.h>

typedef int (*MainPtr)(int, char**);

// This is a separate function so it can easily be set by breakpoint in gdb.
static int run(MainPtr mainFunc, int argc, char** argv) {
  return mainFunc(argc, argv);
}

static void* lookupSymbol(void* pContext, const char* name) {
  return (void*) dlsym(RTLD_DEFAULT, name);
}

const char* inFile = NULL;
bool printListing = false;
bool runResults = false;

extern int opterr;
extern int optind;

static int parseOption(int argc, char** argv)
{
  int c;
  while ((c = getopt (argc, argv, GETOPT_OPTIONS)) != -1) {
    opterr = 0;

    switch(c) {
    #if defined(__HOST__)
      case 'C':
        TARGET_TRIPLE_STRING = optarg;
        break;
    #endif

      case 'R':
        runResults = true;
        break;

      case '?':
        // ignore any error
        break;

      default:
        // Critical error occurs
        return 0;
        break;
    }
  }

  if(optind >= argc) {
    fprintf(stderr, "input file required\n");
    return 0;
  }

  inFile = argv[optind];
  return 1;
}

static BCCScriptRef loadScript() {
  if (!inFile) {
    fprintf(stderr, "input file required\n");
    return NULL;
  }

  struct stat statInFile;
  if (stat(inFile, &statInFile) < 0) {
    fprintf(stderr, "Unable to stat input file: %s\n", strerror(errno));
    return NULL;
  }

  if (!S_ISREG(statInFile.st_mode)) {
    fprintf(stderr, "Input file should be a regular file.\n");
    return NULL;
  }

  FILE *in = fopen(inFile, "r");
  if (!in) {
    fprintf(stderr, "Could not open input file %s\n", inFile);
    return NULL;
  }

  BCCScriptRef script = bccCreateScript();

  if (bccReadFile(script, inFile, /* flags */0) != 0) {
    fprintf(stderr, "bcc: FAILS to read bitcode");
    bccDisposeScript(script);
    return NULL;
  }

  bccRegisterSymbolCallback(script, lookupSymbol, NULL);

  if (bccPrepareExecutable(script, ".", "cache", 0) != 0) {
    fprintf(stderr, "bcc: FAILS to prepare executable.\n");
    bccDisposeScript(script);
    return NULL;
  }

  return script;
}

static int runMain(BCCScriptRef script, int argc, char** argv) {
  MainPtr mainPointer = (MainPtr)bccGetFuncAddr(script, "main");

  if (!mainPointer) {
    mainPointer = (MainPtr)bccGetFuncAddr(script, "root");
  }
  if (!mainPointer) {
    mainPointer = (MainPtr)bccGetFuncAddr(script, "_Z4rootv");
  }
  if (!mainPointer) {
    fprintf(stderr, "Could not find root or main or mangled root.\n");
    return 1;
  }

  fprintf(stderr, "Executing compiled code:\n");

  int argc1 = argc - optind;
  char** argv1 = argv + optind;

  int result = run(mainPointer, argc1, argv1);
  fprintf(stderr, "result: %d\n", result);

  return 1;
}

int main(int argc, char** argv) {
  if(!parseOption(argc, argv)) {
    fprintf(stderr, "failed to parse option\n");
    return 1;
  }

  BCCScriptRef script;

  if((script = loadScript()) == NULL) {
    fprintf(stderr, "failed to load source\n");
    return 2;
  }

  if(runResults && !runMain(script, argc, argv)) {
    fprintf(stderr, "failed to execute\n");
    return 6;
  }

  bccDisposeScript(script);

  return 0;
}
