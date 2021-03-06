// dt.h -- D frontend for GCC.
// Copyright (C) 2011, 2012 Free Software Foundation, Inc.

// GCC is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3, or (at your option) any later
// version.

// GCC is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for more details.

// You should have received a copy of the GNU General Public License
// along with GCC; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

#ifndef DMD_DT_H
#define DMD_DT_H

#include "config.h"
#include "coretypes.h"

#include "symbol.h"

enum DT
{
  DT_azeros,
  DT_common,
  DT_nbytes,
  DT_abytes,
  DT_ibytes,
  DT_xoff,
  DT_1byte,
  DT_tree,
  DT_container
};

struct Type;

struct dt_t
{
  enum DT dt;
  struct dt_t *DTnext;
  union
  {
    dinteger_t DTint;
    dinteger_t DTazeros;
    dinteger_t DTonebyte;
    struct dt_t *DTvalues;
  };
  union
  {
    Symbol *DTsym;
    tree DTtree;
    const void *DTpointer;
    Type *DTtype;
  };
};

extern dt_t **dtval (dt_t **pdt, DT t, dinteger_t i, const void *p);
extern dt_t **dtcat (dt_t **pdt, dt_t *d);
extern tree   dt2tree (dt_t *dt);

// %% should be dinteger_t?, but when used in todt.c, it's assigned to an unsigned
size_t dt_size (dt_t *dt);

// Added for GCC to get correct byte ordering / size
extern dt_t **dtnbits (dt_t **pdt, size_t count, char *pbytes, unsigned unit_size);
extern dt_t **dtnwords (dt_t **pdt, size_t word_count, void *pwords, unsigned word_size);
extern dt_t **dtawords (dt_t **pdt, size_t word_count, void *pwords, unsigned word_size);
extern dt_t **dti32 (dt_t **pdt, unsigned val, int pad_to_word);

// Added for GCC to match types for SRA pass
extern dt_t **dtcontainer (dt_t **pdt, Type *type, dt_t *values);


inline dt_t **
dtnbytes (dt_t **pdt, size_t count, const char *pbytes)
{
  return dtval (pdt, DT_nbytes, count, pbytes);
}

inline dt_t **
dtabytes (dt_t **pdt, int, size_t count, const char *pbytes)
{
  return dtval (pdt, DT_abytes, count, pbytes);
}

inline dt_t **
dtnzeros (dt_t **pdt, size_t count)
{
  return dtval (pdt, DT_azeros, count, 0);
}

inline dt_t **
dtdword (dt_t **pdt, size_t val)
{
  return dti32 (pdt, val, false);
}

inline dt_t **
dtsize_t (dt_t **pdt, size_t val)
{
  return dtval (pdt, DT_ibytes, val, 0);
}

inline dt_t **
dtxoff (dt_t ** pdt, Symbol *sym, size_t offset)
{
  return dtval (pdt, DT_xoff, offset, sym);
}

inline dt_t **
dttree (dt_t **pdt, tree t)
{
  return dtval (pdt, DT_tree, 0, t);
}

inline void
dt_optimize (dt_t *) { }

#endif
