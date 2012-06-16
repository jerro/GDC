// d-todt.cc -- D frontend for GCC.
// Originally contributed by David Friedman
// Maintained by Iain Buclaw

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

#include "d-gcc-includes.h"
#include "d-lang.h"
#include "d-codegen.h"
#include "dt.h"

dt_t**
ComplexExp::toDt (dt_t** pdt)
{
  return dttree (pdt, toElem (& gen));
}

dt_t**
IntegerExp::toDt (dt_t** pdt)
{
  return dttree (pdt, toElem (& gen));
}

dt_t**
RealExp::toDt (dt_t** pdt)
{
  return dttree (pdt, toElem (& gen));
}
