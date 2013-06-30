#include <stddef.h>
#include <ioLib.h>
#include <symLib.h>
#include <sysSymTbl.h>
#include "rlogind.h"

int rlogind_cppentry();

int rlogind_return = rlogind_cppentry();

int
rlogind_cppentry()
{
  FUNCPTR entryptr;
  uint8_t symtype;

  char symbolName[] = "rlogind_entry";
  symFindByName(sysSymTbl, symbolName, (char **)&entryptr, &symtype);
  entryptr();

  return 0;
}

