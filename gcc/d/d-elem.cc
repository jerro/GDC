// d-elem.cc -- D frontend for GCC.
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

#include "d-gcc-includes.h"

#include "id.h"
#include "module.h"
#include "d-lang.h"
#include "d-codegen.h"


elem *
Expression::toElem (IRState *irs)
{
  error ("abstract Expression::toElem called");
  return irs->errorMark (type);
}

elem *
CondExp::toElem (IRState *irs)
{
  tree cn = irs->convertForCondition (econd);
  tree t1 = irs->convertTo (e1->toElemDtor (irs), e1->type, type);
  tree t2 = irs->convertTo (e2->toElemDtor (irs), e2->type, type);
  return build3 (COND_EXPR, type->toCtype(), cn, t1, t2);
}

elem *
IdentityExp::toElem (IRState *irs)
{
  Type *tb1 = e1->type->toBasetype();
  Type *tb2 = e2->type->toBasetype();

  tree_code code = op == TOKidentity ? EQ_EXPR : NE_EXPR;

  if (tb1->ty == Tstruct || tb1->isfloating())
    {
      // Do bit compare.
      tree t_memcmp = irs->buildCall (builtin_decl_explicit (BUILT_IN_MEMCMP), 3,
				      irs->addressOf (e1->toElem (irs)),
				      irs->addressOf (e2->toElem (irs)),
				      irs->integerConstant (e1->type->size()));

      return irs->boolOp (code, t_memcmp, integer_zero_node);
    }
  else if ((tb1->ty == Tsarray || tb1->ty == Tarray)
	   && (tb2->ty == Tsarray || tb2->ty == Tarray))
    {
      return build2 (code, type->toCtype(),
		     irs->toDArray (e1), irs->toDArray (e2));
    }
  else if (tb1->ty == Treference || tb1->ty == Tclass || tb1->ty == Tarray)
    {
      // Assuming types are the same from typeCombine
      return build2 (code, type->toCtype(),
		     e1->toElem (irs), e2->toElem (irs));
    }
  else
    {
      // For operand types other than class objects, static or dynamic
      // arrays, identity is defined as being the same as equality.
      tree t1 = e1->toElem (irs);
      tree t2 = e2->toElem (irs);

      if (type->iscomplex())
	{
	  t1 = irs->maybeMakeTemp (t1);
	  t2 = irs->maybeMakeTemp (t2);
	}

      tree t_cmp = irs->boolOp (code, t1, t2);
      return irs->convertTo (type->toCtype(), t_cmp);
    }
}

elem *
EqualExp::toElem (IRState *irs)
{
  Type *tb1 = e1->type->toBasetype();
  Type *tb2 = e2->type->toBasetype();

  tree_code code = op == TOKequal ? EQ_EXPR : NE_EXPR;

  if (tb1->ty == Tstruct)
    {
      // Do bit compare of struct's
      tree t_memcmp = irs->buildCall (builtin_decl_explicit (BUILT_IN_MEMCMP), 3,
				      irs->addressOf (e1->toElem (irs)),
				      irs->addressOf (e2->toElem (irs)),
				      irs->integerConstant (e1->type->size()));

      return build2 (code, type->toCtype(), t_memcmp, integer_zero_node);
    }
  else if ((tb1->ty == Tsarray || tb1->ty == Tarray)
	   && (tb2->ty == Tsarray || tb2->ty == Tarray))
    {
      // _adEq2 compares each element.
      Type *telem = tb1->nextOf()->toBasetype();
      tree args[3] = {
	  irs->toDArray (e1),
	  irs->toDArray (e2),
	  irs->typeinfoReference (telem->arrayOf())
      };

      tree result = irs->libCall (LIBCALL_ADEQ2, 3, args);
      result = irs->convertTo (type->toCtype(), result);
      if (op == TOKnotequal)
	result = build1 (TRUTH_NOT_EXPR, type->toCtype(), result);

      return result;
    }
  else if (tb1->ty == Taarray && tb2->ty == Taarray)
    {
      TypeAArray *taa1 = (TypeAArray *) tb1;
      tree args[3] = {
	  irs->typeinfoReference (taa1),
	  e1->toElem (irs),
	  e2->toElem (irs)
      };

      tree result = irs->libCall (LIBCALL_AAEQUAL, 3, args);
      result = irs->convertTo (type->toCtype(), result);
      if (op == TOKnotequal)
	result = build1 (TRUTH_NOT_EXPR, type->toCtype(), result);

      return result;
    }
  else
    {
      tree t1 = e1->toElem (irs);
      tree t2 = e2->toElem (irs);

      if (type->iscomplex())
	{
	  t1 = irs->maybeMakeTemp (t1);
	  t2 = irs->maybeMakeTemp (t2);
	}

      tree t_cmp = irs->boolOp (code, t1, t2);
      return irs->convertTo (type->toCtype(), t_cmp);
    }
}

elem *
InExp::toElem (IRState *irs)
{
  Type *e2_base_type = e2->type->toBasetype();
  AddrOfExpr aoe;
  gcc_assert (e2_base_type->ty == Taarray);

  Type *key_type = ((TypeAArray *) e2_base_type)->index->toBasetype();
  tree args[3] = {
      e2->toElem (irs),
      irs->typeinfoReference (key_type),
      aoe.set (irs, irs->convertTo (e1, key_type))
  };
  return convert (type->toCtype(),
		  aoe.finish (irs, irs->libCall (LIBCALL_AAINX, 3, args)));
}

elem *
CmpExp::toElem (IRState *irs)
{
  Type *tb1 = e1->type->toBasetype();
  Type *tb2 = e2->type->toBasetype();

  tree result;
  tree_code code;

  switch (op)
    {
    case TOKue:
      code = tb1->isfloating() && tb2->isfloating() ?
	UNEQ_EXPR : EQ_EXPR;
      break;

    case TOKlg:
      code = tb1->isfloating() && tb2->isfloating() ?
	LTGT_EXPR : NE_EXPR;
      break;

    case TOKule:
      code = tb1->isfloating() && tb2->isfloating() ?
	UNLE_EXPR : LE_EXPR;
      break;

    case TOKul:
      code = tb1->isfloating() && tb2->isfloating() ?
	UNLT_EXPR : LT_EXPR;
      break;

    case TOKuge:
      code = tb1->isfloating() && tb2->isfloating() ?
	UNGE_EXPR : GE_EXPR;
      break;

    case TOKug:
      code = tb1->isfloating() && tb2->isfloating() ?
	UNGT_EXPR : GT_EXPR;
      break;

    case TOKle:
      code = LE_EXPR;
      break;

    case TOKlt:
      code = LT_EXPR;
      break;

    case TOKge:
      code = GE_EXPR;
      break;

    case TOKgt:
      code = GT_EXPR;
      break;

    case TOKleg:
      code = ORDERED_EXPR;
      break;

    case TOKunord:
      code = UNORDERED_EXPR;
      break;

    default:
      gcc_unreachable();
    }

  if ((tb1->ty == Tsarray || tb1->ty == Tarray)
      && (tb2->ty == Tsarray || tb2->ty == Tarray))
    {
      Type *telem = tb1->nextOf()->toBasetype();
      tree args[3] = {
	  irs->toDArray (e1),
	  irs->toDArray (e2),
	  irs->typeinfoReference (telem->arrayOf())
      };

      result = irs->libCall (LIBCALL_ADCMP2, 3, args);

      // %% For float element types, warn that NaN is not taken into account?

      // %% Could do a check for side effects and drop the unused condition
      if (code == ORDERED_EXPR)
	{
	  return irs->boolOp (COMPOUND_EXPR, result,
			      d_truthvalue_conversion (integer_one_node));
	}
      if (code == UNORDERED_EXPR)
	{
	  return irs->boolOp (COMPOUND_EXPR, result,
			      d_truthvalue_conversion (integer_zero_node));
	}

      result = irs->boolOp (code, result, integer_zero_node);
      return irs->convertTo (type->toCtype(), result);
    }
  else
    {
      if (!tb1->isfloating() || !tb2->isfloating())
	{
	  // %% is this properly optimized away?
	  if (code == ORDERED_EXPR)
	    return convert (boolean_type_node, integer_one_node);

	  if (code == UNORDERED_EXPR)
	    return convert (boolean_type_node, integer_zero_node);
	}

      result = irs->boolOp (code, e1->toElem (irs), e2->toElem (irs));
      return irs->convertTo (type->toCtype(), result);
    }
}

elem *
AndAndExp::toElem (IRState *irs)
{
  if (e2->type->toBasetype()->ty != Tvoid)
    {
      tree t1 = irs->convertForCondition (e1);
      tree t2 = irs->convertForCondition (e2);

      if (type->iscomplex())
	{
	  t1 = irs->maybeMakeTemp (t1);
	  t2 = irs->maybeMakeTemp (t2);
	}

      tree t = irs->boolOp (TRUTH_ANDIF_EXPR, t1, t2);
      return irs->convertTo (type->toCtype(), t);
    }
  else
    {
      return build3 (COND_EXPR, type->toCtype(),
		     irs->convertForCondition (e1),
		     e2->toElemDtor (irs), d_void_zero_node);
    }
}

elem *
OrOrExp::toElem (IRState *irs)
{
  if (e2->type->toBasetype()->ty != Tvoid)
    {
      tree t1 = irs->convertForCondition (e1);
      tree t2 = irs->convertForCondition (e2);

      if (type->iscomplex())
	{
	  t1 = irs->maybeMakeTemp (t1);
	  t2 = irs->maybeMakeTemp (t2);
	}

      tree t = irs->boolOp (TRUTH_ORIF_EXPR, t1, t2);
      return irs->convertTo (type->toCtype(), t);
    }
  else
    {
      return build3 (COND_EXPR, type->toCtype(),
		     build1 (TRUTH_NOT_EXPR, boolean_type_node,
			     irs->convertForCondition (e1)),
		     e2->toElemDtor (irs), d_void_zero_node);
    }
}

elem *
XorExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildOp (BIT_XOR_EXPR, type->toCtype(),
		       e1->toElem (irs), e2->toElem (irs));
}

elem *
OrExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildOp (BIT_IOR_EXPR, type->toCtype(),
		       e1->toElem (irs), e2->toElem (irs));
}

elem *
AndExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildOp (BIT_AND_EXPR, type->toCtype(),
		       e1->toElem (irs), e2->toElem (irs));
}

elem *
UshrExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildOp (UNSIGNED_RSHIFT_EXPR, type->toCtype(),
		       e1->toElem (irs), e2->toElem (irs));
}

elem *
ShrExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildOp (RSHIFT_EXPR, type->toCtype(),
		       e1->toElem (irs), e2->toElem (irs));
}

elem *
ShlExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildOp (LSHIFT_EXPR, type->toCtype(),
		       e1->toElem (irs), e2->toElem (irs));
}

elem *
ModExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildOp (e1->type->isfloating() ? FLOAT_MOD_EXPR : TRUNC_MOD_EXPR,
		       type->toCtype(), e1->toElem (irs), e2->toElem (irs));
}

elem *
DivExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildOp (e1->type->isintegral() ? TRUNC_DIV_EXPR : RDIV_EXPR,
		       type->toCtype(), e1->toElem (irs), e2->toElem (irs));
}

elem *
MulExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildOp (MULT_EXPR, type->toCtype(),
		       e1->toElem (irs), e2->toElem (irs));
}

elem *
PowExp::toElem (IRState *irs)
{
  tree e1_t, e2_t;
  tree powtype, powfn = NULL_TREE;
  Type *tb1 = e1->type->toBasetype();

  // Dictates what version of pow() we call.
  powtype = type->toBasetype()->toCtype();
  // If type is int, implicitly convert to double.
  // This allows backend to fold the call into a constant return value.
  if (type->isintegral())
    powtype = double_type_node;

  // Lookup compatible builtin. %% TODO: handle complex types?
  if (TYPE_MAIN_VARIANT (powtype) == double_type_node)
    powfn = builtin_decl_explicit (BUILT_IN_POW);
  else if (TYPE_MAIN_VARIANT (powtype) == float_type_node)
    powfn = builtin_decl_explicit (BUILT_IN_POWF);
  else if (TYPE_MAIN_VARIANT (powtype) == long_double_type_node)
    powfn = builtin_decl_explicit (BUILT_IN_POWL);

  if (powfn == NULL_TREE)
    {
      if (tb1->ty == Tarray || tb1->ty == Tsarray)
	error ("Array operation %s not implemented", toChars());
      else
	error ("%s ^^ %s is not supported", e1->type->toChars(), e2->type->toChars());
      return irs->errorMark (type);
    }

  e1_t = irs->convertTo (powtype, e1->toElem (irs));
  e2_t = irs->convertTo (powtype, e2->toElem (irs));

  return irs->convertTo (type->toCtype(), irs->buildCall (powfn, 2, e1_t, e2_t));
}

elem *
CatExp::toElem (IRState *irs)
{
  Type *elem_type;

  // One of the operands may be an element instead of an array.
  // Logic copied from CatExp::semantic
  Type *tb1 = e1->type->toBasetype();
  Type *tb2 = e2->type->toBasetype();

  if ((tb1->ty == Tsarray || tb1->ty == Tarray)
      && irs->typesCompatible (e2->type, tb1->nextOf()))
    {
      elem_type = tb1->nextOf();
    }
  else if ((tb2->ty == Tsarray || tb2->ty == Tarray)
	   && irs->typesCompatible (e1->type, tb2->nextOf()))
    {
      elem_type = tb2->nextOf();
    }
  else
    {
      elem_type = tb1->nextOf();
    }

  // Flatten multiple concatenations
  unsigned n_operands = 2;
  unsigned n_args;
  tree *args;
  Array elem_vars;
  tree result;

    {
      Expression *e = e1;
      while (e->op == TOKcat)
	{
	  e = ((CatExp *) e)->e1;
	  n_operands += 1;
	}
    }

  n_args = (1 + (n_operands > 2 ? 1 : 0) +
	    n_operands * (n_operands > 2 && flag_split_darrays ? 2 : 1));

  args = new tree[n_args];
  args[0] = irs->typeinfoReference (type);

  if (n_operands > 2)
    args[1] = irs->integerConstant (n_operands, Type::tuns32);

  unsigned ai = n_args - 1;
  CatExp *ce = this;

  while (ce)
    {
      Expression *oe = ce->e2;
      while (1)
	{
	  tree array_exp;
	  if (irs->typesCompatible (oe->type->toBasetype(), elem_type->toBasetype()))
	    {
	      tree elem_var = NULL_TREE;
	      tree expr = irs->maybeExprVar (oe->toElem (irs), &elem_var);
	      array_exp = irs->darrayVal (oe->type->arrayOf(), 1, irs->addressOf (expr));

	      if (elem_var)
		elem_vars.push (elem_var);
	    }
	  else
	    array_exp = irs->toDArray (oe);

	  if (n_operands > 2 && flag_split_darrays)
	    {
	      array_exp = irs->maybeMakeTemp (array_exp);
	      args[ai--] = irs->darrayPtrRef (array_exp); // note: filling array
	      args[ai--] = irs->darrayLenRef (array_exp); // backwards, so ptr 1st
	    }
	  else
	    args[ai--] = array_exp;

	  if (ce)
	    {
	      if (ce->e1->op != TOKcat)
		{
		  oe = ce->e1;
		  ce = NULL;
		  // finish with atomtic lhs
		}
	      else
		{
		  ce = (CatExp *) ce->e1;
		  break;  // continue with lhs CatExp
		}
	    }
	  else
	    goto all_done;
	}
    }
 all_done:

  result = irs->libCall (n_operands > 2 ? LIBCALL_ARRAYCATNT : LIBCALL_ARRAYCATT,
			 n_args, args, type->toCtype());

  for (size_t i = 0; i < elem_vars.dim; ++i)
    {
      tree elem_var = (tree) elem_vars.data[i];
      result = irs->binding (elem_var, result);
    }

  return result;
}

elem *
MinExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  // %% faster: check if result is complex
  if ((e1->type->isreal() && e2->type->isimaginary())
      || (e1->type->isimaginary() && e2->type->isreal()))
    {
      // %%TODO: need to check size/modes
      tree t1 = e1->toElem (irs);
      tree t2 = e2->toElem (irs);

      t2 = build1 (NEGATE_EXPR, TREE_TYPE (t2), t2);

      if (e1->type->isreal())
	return build2 (COMPLEX_EXPR, type->toCtype(), t1, t2);
      else
	return build2 (COMPLEX_EXPR, type->toCtype(), t2, t1);
    }

  // The front end has already taken care of pointer-int and pointer-pointer
  return irs->buildOp (MINUS_EXPR, type->toCtype(),
		       e1->toElem (irs), e2->toElem (irs));
}

elem *
AddExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  // %% faster: check if result is complex
  if ((e1->type->isreal() && e2->type->isimaginary())
      || (e1->type->isimaginary() && e2->type->isreal()))
    {
      // %%TODO: need to check size/modes
      tree t1 = e1->toElem (irs);
      tree t2 = e2->toElem (irs);

      if (e1->type->isreal())
	return build2 (COMPLEX_EXPR, type->toCtype(), t1, t2);
      else
	return build2 (COMPLEX_EXPR, type->toCtype(), t2, t1);
    }

  // The front end has already taken care of (pointer + integer)
  return irs->buildOp (PLUS_EXPR, type->toCtype(),
		       e1->toElem (irs), e2->toElem (irs));
}

elem *
XorAssignExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildAssignOp (BIT_XOR_EXPR, type, e1, e2);
}

elem *
OrAssignExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildAssignOp (BIT_IOR_EXPR, type, e1, e2);
}

elem *
AndAssignExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildAssignOp (BIT_AND_EXPR, type, e1, e2);
}

elem *
UshrAssignExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildAssignOp (UNSIGNED_RSHIFT_EXPR, type, e1, e2);
}

elem *
ShrAssignExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildAssignOp (RSHIFT_EXPR, type, e1, e2);
}

elem *
ShlAssignExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildAssignOp (LSHIFT_EXPR, type, e1, e2);
}

elem *
ModAssignExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildAssignOp (e1->type->isfloating() ?
			     FLOAT_MOD_EXPR : TRUNC_MOD_EXPR,
			     type, e1, e2);
}

elem *
DivAssignExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildAssignOp (e1->type->isintegral() ?
			     TRUNC_DIV_EXPR : RDIV_EXPR,
			     type, e1, e2);
}

elem *
MulAssignExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildAssignOp (MULT_EXPR, type, e1, e2);
}

elem *
PowAssignExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  gcc_unreachable();
}

elem *
CatAssignExp::toElem (IRState *irs)
{
  Type *dest_type = e1->type->toBasetype();
  Type *value_type = e2->type->toBasetype();
  Type *elem_type = dest_type->nextOf()->toBasetype();
  AddrOfExpr aoe;
  tree result;

  if (dest_type->ty == Tarray && value_type->ty == Tdchar
      && (elem_type->ty == Tchar || elem_type->ty == Twchar))
    {
      // append a dchar to a char[] or wchar[]
      tree args[2] = {
	  aoe.set (irs, e1->toElem (irs)),
	  irs->toElemLvalue (e2)
      };

      LibCall libcall = elem_type->ty == Tchar ?
	LIBCALL_ARRAYAPPENDCD : LIBCALL_ARRAYAPPENDWD;

      result = irs->libCall (libcall, 2, args, type->toCtype());
    }
  else
    {
      gcc_assert (dest_type->ty == Tarray || value_type->ty == Tsarray);

      if ((value_type->ty == Tarray || value_type->ty == Tsarray)
	  && irs->typesCompatible (elem_type, value_type->nextOf()->toBasetype()))
	{
	  // append an array
	  tree args[3] = {
	      irs->typeinfoReference (type),
	      irs->addressOf (irs->toElemLvalue (e1)),
	      irs->toDArray (e2)
	  };

	  result = irs->libCall (LIBCALL_ARRAYAPPENDT, 3, args, type->toCtype());
	}
      else
	{
	  // append an element
	  tree args[3] = {
	      irs->typeinfoReference (type),
	      irs->addressOf (irs->toElemLvalue (e1)),
	      size_one_node
	  };

	  result = irs->libCall (LIBCALL_ARRAYAPPENDCTX, 3, args, type->toCtype());
	  result = save_expr (result);

	  // assign e2 to last element
	  tree off_exp = irs->darrayLenRef (result);
	  off_exp = build2 (MINUS_EXPR, TREE_TYPE (off_exp), off_exp, size_one_node);
	  off_exp = irs->maybeMakeTemp (off_exp);

	  tree ptr_exp = irs->darrayPtrRef (result);
	  ptr_exp = irs->pvoidOkay (ptr_exp);
	  ptr_exp = irs->pointerIntSum (ptr_exp, off_exp);

	  // evaluate expression before appending
	  tree e2e = e2->toElem (irs);
	  e2e = save_expr (e2e);

	  result = irs->modify (elem_type->toCtype(), irs->indirect (ptr_exp), e2e);
	  result = irs->compound (elem_type->toCtype(), e2e, result);
	}
    }

  return aoe.finish (irs, result);
}

elem *
MinAssignExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildAssignOp (MINUS_EXPR, type, e1, e2);
}

elem *
AddAssignExp::toElem (IRState *irs)
{
  if (irs->arrayOpNotImplemented (this))
    return irs->errorMark (type);

  return irs->buildAssignOp (PLUS_EXPR, type, e1, e2);
}


// Determine if type is an array of structs that need a postblit.
static StructDeclaration *
needsPostblit (Type *t)
{
  t = t->toBasetype();
  while (t->ty == Tsarray)
    t = t->nextOf()->toBasetype();
  if (t->ty == Tstruct)
    {   StructDeclaration *sd = ((TypeStruct *)t)->sym;
      if (sd->postblit)
	return sd;
    }
  return NULL;
}


elem *
AssignExp::toElem (IRState *irs)
{
  // First, handle special assignment semantics
  if (e1->op == TOKarraylength)
    {
      // Assignment to an array's length property; resize the array.
      ArrayLengthExp *ale = (ArrayLengthExp *) e1;
      // Don't want ->toBasetype() for the element type.
      Type *elem_type = ale->e1->type->toBasetype()->nextOf();
      tree args[3] = {
	  irs->typeinfoReference (ale->e1->type),
	  irs->convertTo (e2, Type::tsize_t),
	  irs->addressOf (ale->e1->toElem (irs))
      };

      LibCall libcall = elem_type->isZeroInit() ?
	LIBCALL_ARRAYSETLENGTHT : LIBCALL_ARRAYSETLENGTHIT;

      tree result = irs->libCall (libcall, 3, args);
      return irs->darrayLenRef (result);
    }
  else if (e1->op == TOKslice)
    {
      Type *elem_type = e1->type->toBasetype()->nextOf()->toBasetype();

      if (irs->typesCompatible (elem_type, e2->type->toBasetype()))
	{
	  // Set a range of elements to one value.
	  // %% This is used for initing on-stack static arrays..
	  // should optimize with memset if possible
	  // %% vararg issues
	  tree dyn_array_exp = irs->maybeMakeTemp (e1->toElem (irs));

	  if (op != TOKblit)
	    {
	      if (needsPostblit (elem_type) != NULL)
		{
		  AddrOfExpr aoe;
		  tree args[4] = {
		      irs->darrayPtrRef (dyn_array_exp),
		      aoe.set (irs, e2->toElem (irs)),
		      irs->darrayLenRef (dyn_array_exp),
		      irs->typeinfoReference (elem_type),
		  };
		  tree t = irs->libCall (op == TOKconstruct ?
					 LIBCALL_ARRAYSETCTOR : LIBCALL_ARRAYSETASSIGN,
					 4, args);
		  return irs->compound (aoe.finish (irs, t), dyn_array_exp);
		}
	    }

	  tree set_exp = irs->arraySetExpr (irs->darrayPtrRef (dyn_array_exp),
					    e2->toElem (irs), irs->darrayLenRef (dyn_array_exp));
	  return irs->compound (set_exp, dyn_array_exp);
	}
      else
	{
	  if (op != TOKblit && needsPostblit (elem_type) != NULL)
	    {
	      tree args[3] = {
		  irs->typeinfoReference (elem_type),
		  irs->toDArray (e1),
		  irs->toDArray (e2)
	      };
	      return irs->libCall (op == TOKconstruct ?
				   LIBCALL_ARRAYCTOR : LIBCALL_ARRAYASSIGN,
				   3, args, type->toCtype());
	    }
	  else
	    if (irs->arrayBoundsCheck())
	      {
		tree args[3] = {
		    irs->integerConstant (elem_type->size(), Type::tsize_t),
		    irs->toDArray (e2),
		    irs->toDArray (e1)
		};
		return irs->libCall (LIBCALL_ARRAYCOPY, 3, args, type->toCtype());
	      }
	    else
	      {
		tree array[2] = {
		    irs->maybeMakeTemp (irs->toDArray (e1)),
		    irs->toDArray (e2)
		};
		tree t_memcpy = builtin_decl_explicit (BUILT_IN_MEMCPY);
		tree result;
		tree size;

		size = fold_build2 (MULT_EXPR, size_type_node,
				    irs->convertTo (size_type_node, irs->darrayLenRef (array[0])),
				    size_int (elem_type->size()));

		result = irs->buildCall (t_memcpy, 3, irs->darrayPtrRef (array[0]),
					 irs->darrayPtrRef (array[1]), size);

		return irs->compound (type->toCtype(), result, array[0]);
	      }
	}
    }
  else if (op == TOKconstruct)
    {
      tree lhs = irs->toElemLvalue (e1);
      tree rhs = irs->convertForAssignment (e2, e1->type);
      Type *tb1 = e1->type->toBasetype();
      tree result = NULL_TREE;

      if (e1->op == TOKvar)
	{
	  Declaration *decl = ((VarExp *)e1)->var;
	  // Look for reference initializations
	  if (decl->storage_class & (STCout | STCref))
	    {
	      // Want reference to lhs, not indirect ref.
	      lhs = TREE_OPERAND (lhs, 0);
	      rhs = irs->addressOf (rhs);
	    }
	}
      result = irs->modify (type->toCtype(), lhs, rhs);

      if (tb1->ty == Tstruct)
	{
	  if (e2->op == TOKstructliteral)
	    {
	      // Initialize all alignment 'holes' to zero.
	      StructLiteralExp *sle = ((StructLiteralExp *)e2);
	      if (sle->fillHoles)
		{
		  unsigned sz = sle->type->size();
		  tree init = irs->buildCall (builtin_decl_explicit (BUILT_IN_MEMSET), 3,
					      irs->addressOf (lhs), size_zero_node, size_int (sz));
		  result = irs->maybeCompound (init, result);
		}
	    }
	  else if (e2->op == TOKint64)
	    {
	      // Maybe set-up hidden pointer to outer scope context.
	      StructDeclaration *sd = ((TypeStruct *)tb1)->sym;
	      if (sd->isNested())
		{
		  tree vthis_field = sd->vthis->toSymbol()->Stree;
		  tree vthis_value = irs->getVThis (sd, this);

		  tree vthis_exp = irs->modify (irs->component (lhs, vthis_field), vthis_value);
		  result = irs->maybeCompound (result, vthis_exp);
		}
	    }
	}
      return result;
    }
  else
    {
      // Simple assignment
      tree lhs = irs->toElemLvalue (e1);
      return irs->modify (type->toCtype(), lhs,
			  irs->convertForAssignment (e2, e1->type));
    }
}

elem *
PostExp::toElem (IRState *irs)
{
  tree result = NULL_TREE;

  if (op == TOKplusplus)
    {
      result = build2 (POSTINCREMENT_EXPR, type->toCtype(),
		       irs->toElemLvalue (e1), e2->toElem (irs));
    }
  else if (op == TOKminusminus)
    {
      result = build2 (POSTDECREMENT_EXPR, type->toCtype(),
		       irs->toElemLvalue (e1), e2->toElem (irs));
    }
  else
    gcc_unreachable();

  TREE_SIDE_EFFECTS (result) = 1;
  return result;
}

elem *
IndexExp::toElem (IRState *irs)
{
  Type *array_type = e1->type->toBasetype();

  if (array_type->ty != Taarray)
    {
      /* arrayElemRef will call aryscp.finish.  This result
	 of this function may be used as an lvalue and we
	 do not want it to be a BIND_EXPR. */
      ArrayScope aryscp (irs, lengthVar, loc);
      return irs->arrayElemRef (this, &aryscp);
    }
  else
    {
      Type *key_type = ((TypeAArray *) array_type)->index->toBasetype();
      AddrOfExpr aoe;
      tree args[4] = {
	  e1->toElem (irs),
	  irs->typeinfoReference (key_type),
	  irs->integerConstant (array_type->nextOf()->size(), Type::tsize_t),
	  aoe.set (irs, irs->convertTo (e2, key_type))
      };
      LibCall libcall = modifiable ? LIBCALL_AAGETX : LIBCALL_AAGETRVALUEX;
      tree t = irs->libCall (libcall, 4, args, type->pointerTo()->toCtype());
      t = aoe.finish (irs, t);

      if (irs->arrayBoundsCheck())
	{
	  t = save_expr (t);
	  t = build3 (COND_EXPR, TREE_TYPE (t), d_truthvalue_conversion (t), t,
		      irs->assertCall (loc, LIBCALL_ARRAY_BOUNDS));
	}
      return irs->indirect (type->toCtype(), t);
    }
}

elem *
CommaExp::toElem (IRState *irs)
{
  tree t1 = e1->toElem (irs);
  tree t2 = e2->toElem (irs);
  tree tt = type ? type->toCtype() : void_type_node;

  return build2 (COMPOUND_EXPR, tt, t1, t2);
}

elem *
ArrayLengthExp::toElem (IRState *irs)
{
  if (e1->type->toBasetype()->ty == Tarray)
    {
      return irs->darrayLenRef (e1->toElem (irs));
    }
  else
    {
      // Tsarray case seems to be handled by front-end
      error ("unexpected type for array length: %s", type->toChars());
      return irs->errorMark (type);
    }
}

elem *
SliceExp::toElem (IRState *irs)
{
  // This function assumes that the front end casts the result to a dynamic array.
  gcc_assert (type->toBasetype()->ty == Tarray);

  // Use convert-to-dynamic-array code if possible
  if (e1->type->toBasetype()->ty == Tsarray && !upr && !lwr)
    return irs->convertTo (e1->toElem (irs), e1->type, type);

  Type *orig_array_type = e1->type->toBasetype();

  tree orig_array_expr, orig_pointer_expr;
  tree final_len_expr, final_ptr_expr;
  tree array_len_expr = NULL_TREE;
  tree lwr_tree = NULL_TREE;
  tree upr_tree = NULL_TREE;

  ArrayScope aryscp (irs, lengthVar, loc);

  orig_array_expr = aryscp.setArrayExp (irs, e1->toElem (irs), e1->type);
  orig_array_expr = irs->maybeMakeTemp (orig_array_expr);
  // specs don't say bounds if are checked for error or clipped to current size

  // Get the data pointer for static and dynamic arrays
  orig_pointer_expr = irs->convertTo (orig_array_expr, orig_array_type,
				      orig_array_type->nextOf()->pointerTo());

  final_ptr_expr = orig_pointer_expr;

  // orig_array_expr is already a save_expr if necessary, so
  // we don't make array_len_expr a save_expr which is, at most,
  // a COMPONENT_REF on top of orig_array_expr.
  if (orig_array_type->ty == Tarray)
    {
      array_len_expr = irs->darrayLenRef (orig_array_expr);
    }
  else if (orig_array_type->ty == Tsarray)
    {
      array_len_expr = ((TypeSArray *) orig_array_type)->dim->toElem (irs);
    }
  else
    {
      // array_len_expr == NULL indicates no bounds check is possible
    }

  if (lwr)
    {
      lwr_tree = lwr->toElem (irs);

      if (!integer_zerop (lwr_tree))
	{
	  lwr_tree = irs->maybeMakeTemp (lwr_tree);
	  // Adjust .ptr offset
	  final_ptr_expr = irs->pointerIntSum (irs->pvoidOkay (final_ptr_expr), lwr_tree);
	  final_ptr_expr = irs->nop (TREE_TYPE (orig_pointer_expr), final_ptr_expr);
	}
      else
	lwr_tree = NULL_TREE;
    }

  if (upr)
    {
      upr_tree = upr->toElem (irs);
      upr_tree = irs->maybeMakeTemp (upr_tree);

      if (irs->arrayBoundsCheck())
	{
	  // %% && ! is zero
	  if (array_len_expr)
	    {
	      final_len_expr = irs->checkedIndex (loc, upr_tree, array_len_expr, true);
	    }
	  else
	    {
	      // Still need to check bounds lwr <= upr for pointers.
	      gcc_assert (orig_array_type->ty == Tpointer);
	      final_len_expr = upr_tree;
	    }
	  if (lwr_tree)
	    {
	      // Enforces lwr <= upr. No need to check lwr <= length as
	      // we've already ensured that upr <= length.
	      tree lwr_bounds_check = irs->checkedIndex (loc, lwr_tree, upr_tree, true);
	      final_len_expr = irs->compound (lwr_bounds_check, final_len_expr);
	    }
	}
      else
	{
	  final_len_expr = upr_tree;
	}
      if (lwr_tree)
	{
	  // %% Need to ensure lwr always gets evaluated first, as it may be a function call.
	  // Does (-lwr + upr) rather than (upr - lwr)
	  final_len_expr = build2 (PLUS_EXPR, TREE_TYPE (final_len_expr),
				   build1 (NEGATE_EXPR, TREE_TYPE (lwr_tree), lwr_tree),
				   final_len_expr);
	}
    }
  else
    {
      // If this is the case, than there is no lower bound specified and
      // there is no need to subtract.
      switch (orig_array_type->ty)
	{
	case Tarray:
	  final_len_expr = irs->darrayLenRef (orig_array_expr);
	  break;
	case Tsarray:
	  final_len_expr = ((TypeSArray *) orig_array_type)->dim->toElem (irs);
	  break;
	default:
	  ::error ("Attempt to take length of something that was not an array");
	  return irs->errorMark (type);
	}
    }

  tree result = irs->darrayVal (type->toCtype(), final_len_expr, final_ptr_expr);
  return aryscp.finish (irs, result);
}

elem *
CastExp::toElem (IRState *irs)
{
  Type *ebtype = e1->type->toBasetype();
  Type *tbtype = to->toBasetype();
  tree t = e1->toElem (irs);

  if (tbtype->ty == Tvoid)
    {
      // Just evaluate e1 if it has any side effects
      return build1 (NOP_EXPR, tbtype->toCtype(), t);
    }

  return irs->convertTo (t, ebtype, tbtype);
}


elem *
DeleteExp::toElem (IRState *irs)
{
  LibCall libcall;
  tree t1 = e1->toElem (irs);
  Type *tb1 = e1->type->toBasetype();

  if (tb1->ty == Tclass)
    {
      if (e1->op == TOKvar)
        {
	  VarDeclaration *v = ((VarExp *)e1)->var->isVarDeclaration();
	  if (v && v->onstack)
	    {
	      libcall = tb1->isClassHandle()->isInterfaceDeclaration() ?
		LIBCALL_CALLINTERFACEFINALIZER : LIBCALL_CALLFINALIZER;
	      return irs->libCall (libcall, 1, &t1);
	    }
	}
      libcall = tb1->isClassHandle()->isInterfaceDeclaration() ?
	LIBCALL_DELINTERFACE : LIBCALL_DELCLASS;

      t1 = irs->addressOf (t1);
      return irs->libCall (libcall, 1, &t1);
    }
  else if (tb1->ty == Tarray)
    {
      // Might need to run destructor on array contents
      Type *next_type = tb1->nextOf()->toBasetype();
      tree ti = d_null_pointer;

      while (next_type->ty == Tsarray)
	next_type = next_type->nextOf()->toBasetype();
      if (next_type->ty == Tstruct)
	{
	  TypeStruct *ts = (TypeStruct *)next_type;
	  if (ts->sym->dtor)
	    ti = tb1->nextOf()->getTypeInfo (NULL)->toElem (irs);
	}
      // call _delarray_t (&t1, ti);
      tree args[2] = {
	  irs->addressOf (t1),
	  ti
      };
      return irs->libCall (LIBCALL_DELARRAYT, 2, args);
    }
  else if (tb1->ty == Tpointer)
    {
      t1 = irs->addressOf (t1);
      return irs->libCall (LIBCALL_DELMEMORY, 1, &t1);
    }
  else
    {
      error ("don't know how to delete %s", e1->toChars());
      return irs->errorMark (type);
    }
}

elem *
RemoveExp::toElem (IRState *irs)
{
  Expression *e_array = e1;
  Expression *e_index = e2;
  // Check that the array is actually an associative array
  if (e_array->type->toBasetype()->ty == Taarray)
    {
      Type *a_type = e_array->type->toBasetype();
      Type *key_type = ((TypeAArray *) a_type)->index->toBasetype();
      AddrOfExpr aoe;

      tree args[3] = {
	  e_array->toElem (irs),
	  irs->typeinfoReference (key_type),
	  aoe.set (irs, irs->convertTo (e_index, key_type)),
      };
      return aoe.finish (irs, irs->libCall (LIBCALL_AADELX, 3, args));
    }
  else
    {
      error ("%s is not an associative array", e_array->toChars());
      return irs->errorMark (type);
    }
}

elem *
BoolExp::toElem (IRState *irs)
{
  if (e1->op == TOKcall && e1->type->toBasetype()->ty == Tvoid)
    {
      /* This could happen as '&&' is allowed as a shorthand for 'if'
	 eg:  (condition) && callexpr();  */
      return e1->toElem (irs);
    }

  return convert (type->toCtype(), irs->convertForCondition (e1));
}

elem *
NotExp::toElem (IRState *irs)
{
  // %% doc: need to convert to boolean type or this will fail.
  tree t = build1 (TRUTH_NOT_EXPR, boolean_type_node,
		   irs->convertForCondition (e1));
  return irs->convertTo (type->toCtype(), t);
}

elem *
ComExp::toElem (IRState *irs)
{
  return build1 (BIT_NOT_EXPR, type->toCtype(), e1->toElem (irs));
}

elem *
NegExp::toElem (IRState *irs)
{
  // %% GCC B.E. won't optimize (NEGATE_EXPR (INTEGER_CST ..))..
  // %% is type correct?
  TY ty1 = e1->type->toBasetype()->ty;

  if (ty1 == Tarray || ty1 == Tsarray)
    {
      error ("Array operation %s not implemented", toChars());
      return irs->errorMark (type);
    }

  return build1 (NEGATE_EXPR, type->toCtype(), e1->toElem (irs));
}

elem *
PtrExp::toElem (IRState *irs)
{
  /* Produce better code by converting * (#rec + n) to
     COMPONENT_REFERENCE.  Otherwise, the variable will always be
     allocated in memory because its address is taken. */
  Type *rec_type = 0;
  size_t the_offset;
  tree rec_tree;

  if (e1->op == TOKadd)
    {
      BinExp *add_exp = (BinExp *) e1;
      if (add_exp->e1->op == TOKaddress
	  && add_exp->e2->isConst() && add_exp->e2->type->isintegral())
	{
	  Expression *rec_exp = ((AddrExp *) add_exp->e1)->e1;
	  rec_type = rec_exp->type->toBasetype();
	  rec_tree = rec_exp->toElem (irs);
	  the_offset = add_exp->e2->toUInteger();
	}
    }
  else if (e1->op == TOKsymoff)
    {
      // is this ever not a VarDeclaration?
      SymOffExp *sym_exp = (SymOffExp *) e1;
      if (!irs->isDeclarationReferenceType (sym_exp->var))
	{
	  rec_type = sym_exp->var->type->toBasetype();
	  rec_tree = irs->var (sym_exp->var);
	  the_offset = sym_exp->offset;
	}
      // otherwise, no real benefit?
    }

  if (rec_type && rec_type->ty == Tstruct)
    {
      StructDeclaration *sd = ((TypeStruct *)rec_type)->sym;
      for (size_t i = 0; i < sd->fields.dim; i++)
	{
	  VarDeclaration *field = sd->fields[i];
	  if (field->offset == the_offset
	      && irs->typesSame (field->type, this->type))
	    {
	      if (irs->isErrorMark (rec_tree))
		return rec_tree; // backend will ICE otherwise
	      return irs->component (rec_tree, field->toSymbol()->Stree);
	    }
	  else if (field->offset > the_offset)
	    {
	      break;
	    }
	}
    }

  tree e = irs->indirect (type->toCtype(), e1->toElem (irs));
  if (irs->inVolatile())
    TREE_THIS_VOLATILE (e) = 1;

  return e;
}

elem *
AddrExp::toElem (IRState *irs)
{
  tree addrexp = irs->addressOf (e1->toElem (irs));
  return irs->nop (type->toCtype(), addrexp);
}

elem *
CallExp::toElem (IRState *irs)
{
  tree call_exp = irs->call (e1, arguments);

  TypeFunction *tf = irs->getFuncType (e1->type->toBasetype());
  if (tf->isref)
    call_exp = irs->indirect (call_exp);

  // Some library calls are defined to return a generic type.
  // this->type is the real type. (See crash2.d)
  if (type->isTypeBasic())
    call_exp = irs->convertTo (type->toCtype(), call_exp);

  return call_exp;
}

/*******************************************
 * Evaluate Expression, then call destructors on any temporaries in it.
 */

elem *
Expression::toElemDtor (IRState *irs)
{
  size_t starti = irs->varsInScope ? irs->varsInScope->dim : 0;
  tree t = toElem (irs);
  size_t endi = irs->varsInScope ? irs->varsInScope->dim : 0;

  // Add destructors
  tree tdtors = NULL_TREE;
  for (size_t i = starti; i != endi; ++i)
    {
      VarDeclaration *vd = irs->varsInScope->tdata()[i];
      if (vd)
	{
	  irs->varsInScope->tdata()[i] = NULL;
	  tree ed = vd->edtor->toElem (irs);
	  tdtors = irs->maybeCompound (ed, tdtors); // execute in reverse order
	}
    }
  if (tdtors != NULL_TREE)
    {
      t = save_expr (t);
      t = irs->compound (irs->compound (t, tdtors), t);
    }
  return t;
}


elem *
DotTypeExp::toElem (IRState *irs)
{
  // Just a pass through to e1.
  tree t = e1->toElem (irs);
  return t;
}

// The result will probably just be converted to a CONSTRUCTOR for a Tdelegate struct
elem *
DelegateExp::toElem (IRState *irs)
{
  Type *t = e1->type->toBasetype();

  if (func->fbody)
    {
      // Add the function as nested function if it belongs to this module
      // ie, it is a member of this module, or it is a template instance.
      Dsymbol *owner = func->toParent();
      while (!owner->isTemplateInstance() && owner->toParent())
	owner = owner->toParent();
      if (owner->isTemplateInstance() || owner == irs->mod)
	{
	  Symbol *s = irs->func->toSymbol();
	  s->deferredNestedFuncs.push (func);
	}
    }

  if (t->ty == Tclass || t->ty == Tstruct)
    {
      // %% Need to see if DotVarExp ever has legitimate
      // <aggregate>.<static method>.  If not, move this test
      // to objectInstanceMethod.
      if (!func->isThis())
	error ("delegates are only for non-static functions");

      return irs->objectInstanceMethod (e1, func, type);
    }
  else
    {
      tree this_tree;
      if (func->isNested())
	{
	  if (e1->op == TOKnull)
	    this_tree = e1->toElem (irs);
	  else
	    this_tree = irs->getFrameForFunction (func);
	}
      else
	{
	  gcc_assert (func->isThis());
	  this_tree = e1->toElem (irs);
	}

      return irs->methodCallExpr (irs->addressOf (func),
				  this_tree, type);
    }
}

elem *
DotVarExp::toElem (IRState *irs)
{
  FuncDeclaration *func_decl;
  VarDeclaration *var_decl;
  Type *obj_basetype = e1->type->toBasetype();

  switch (obj_basetype->ty)
    {
    case Tpointer:
      if (obj_basetype->nextOf()->toBasetype()->ty != Tstruct)
	break;
      // drop through

    case Tstruct:
    case Tclass:
      func_decl = var->isFuncDeclaration();
      var_decl = var->isVarDeclaration();
      if (func_decl)
      {
	// if Tstruct, objInstanceMethod will use the address of e1
	return irs->objectInstanceMethod (e1, func_decl, type);
      }
      else if (var_decl)
	{
	  if (!(var_decl->storage_class & STCfield))
	    return irs->var (var_decl);
	  else
	    {
	      tree this_tree = e1->toElem (irs);
	      if (obj_basetype->ty != Tstruct)
		this_tree = irs->indirect (this_tree);
	      return irs->component (this_tree, var_decl->toSymbol()->Stree);
	    }
	}
      else
	error ("%s is not a field, but a %s", var->toChars(), var->kind());
      break;

    default:
      break;
    }
  ::error ("Don't know how to handle %s", toChars());
  return irs->errorMark (type);
}

elem *
AssertExp::toElem (IRState *irs)
{
  // %% todo: Do we call a Tstruct's invariant if
  // e1 is a pointer to the struct?
  if (global.params.useAssert)
    {
      Type *tb1 = e1->type->toBasetype();
      TY ty = tb1->ty;
      tree assert_call = msg ?
	irs->assertCall (loc, msg) : irs->assertCall (loc);

      if (ty == Tclass)
	{
	  ClassDeclaration *cd = tb1->isClassHandle();
	  tree arg = e1->toElem (irs);
	  if (cd->isCOMclass())
	    {
	      return build3 (COND_EXPR, void_type_node,
			     irs->boolOp (NE_EXPR, arg, d_null_pointer),
			     d_void_zero_node, assert_call);
	    }
	  else if (cd->isInterfaceDeclaration())
	    {
	      arg = irs->convertTo (arg, tb1, irs->getObjectType());
	    }
	  // this does a null pointer check before calling _d_invariant
	  return build3 (COND_EXPR, void_type_node,
			 irs->boolOp (NE_EXPR, arg, d_null_pointer),
			 irs->libCall (LIBCALL_INVARIANT, 1, &arg), assert_call);
	}
      else
	{
	  // build: ((bool) e1  ? (void)0 : _d_assert (...))
	  //    or: (e1 != null ? e1._invariant() : _d_assert (...))
	  tree result;
	  tree invc = NULL_TREE;
	  tree e1_t = e1->toElem (irs);

	  if (ty == Tpointer)
	    {
	      Type *sub_type = tb1->nextOf()->toBasetype();
	      if (sub_type->ty == Tstruct)
		{
		  AggregateDeclaration *agg_decl = ((TypeStruct *) sub_type)->sym;
		  if (agg_decl->inv)
		    {
		      Expressions args;
		      e1_t = irs->maybeMakeTemp (e1_t);
		      invc = irs->call (agg_decl->inv, e1_t, &args);
		    }
		}
	    }
	  result = build3 (COND_EXPR, void_type_node,
			   irs->convertForCondition (e1_t, e1->type),
			   invc ? invc : d_void_zero_node, assert_call);
	  return result;
	}
    }

  return d_void_zero_node;
}

elem *
DeclarationExp::toElem (IRState *irs)
{
  VarDeclaration *vd = declaration->isVarDeclaration();
  if (vd != NULL)
    {
      // Put variable on list of things needing destruction
      if (vd->edtor && !vd->noscope)
	{
	  if (!irs->varsInScope)
	    irs->varsInScope = new VarDeclarations();
	  irs->varsInScope->push (vd);
	}
    }

  // VarDeclaration::toObjFile was modified to call d_gcc_emit_local_variable
  // if needed.  This assumes irs == g.irs
  irs->pushStatementList();
  declaration->toObjFile (false);
  tree t = irs->popStatementList();

  /* Construction of an array for typesafe-variadic function arguments
     can cause an empty STMT_LIST here.  This can causes problems
     during gimplification. */
  if (TREE_CODE (t) == STATEMENT_LIST && !STATEMENT_LIST_HEAD (t))
    return build_empty_stmt (input_location);

  return t;
}


elem *
FuncExp::toElem (IRState *irs)
{
  Type *func_type = type->toBasetype();

  if (func_type->ty == Tpointer)
    {
      // This check is for lambda's, remove 'vthis' as function isn't nested.
      if (fd->tok == TOKreserved && fd->vthis)
	{
	  fd->tok = TOKfunction;
	  fd->vthis = NULL;
	}

      func_type = func_type->nextOf()->toBasetype();
    }

  fd->toObjFile (false);

  // If nested, this will be a trampoline...
  switch (func_type->ty)
    {
    case Tfunction:
      return irs->nop (type->toCtype(), irs->addressOf (fd));

    case Tdelegate:
      return irs->methodCallExpr (irs->addressOf (fd),
				  irs->getFrameForFunction (fd), type);

    default:
      ::error ("Unexpected FuncExp type");
      return irs->errorMark (type);
    }
}

elem *
HaltExp::toElem (IRState *irs)
{
  // Needs improvement.  Avoid library calls if possible..
  tree t_abort = builtin_decl_explicit (BUILT_IN_ABORT);
  return irs->buildCall (t_abort, 0);
}

elem *
SymbolExp::toElem (IRState *irs)
{
  tree exp;

  if (op == TOKvar)
    {
      if (var->needThis())
	{
	  error ("need 'this' to access member %s", var->ident->string);
	  return irs->errorMark (type);
	}

      // __ctfe is always false at runtime
      if (var->ident == Id::ctfe)
	return integer_zero_node;

      exp = irs->var (var);
      TREE_USED (exp) = 1;

      // For variables that are references (currently only out/inout arguments;
      // objects don't count), evaluating the variable means we want what it refers to.
      if (irs->isDeclarationReferenceType (var))
	{
	  exp = irs->indirect (var->type->toCtype(), exp);
	  if (irs->inVolatile())
	    TREE_THIS_VOLATILE (exp) = 1;
	}
      else
	{
	  if (irs->inVolatile())
	    {
	      exp = irs->addressOf (exp);
	      TREE_THIS_VOLATILE (exp) = 1;
	      exp = irs->indirect (exp);
	      TREE_THIS_VOLATILE (exp) = 1;
	    }
	}
      return exp;
    }
  else if (op == TOKsymoff)
    {
      size_t offset = ((SymOffExp *) this)->offset;

      exp = irs->var (var);
      TREE_USED (exp) = 1;

      if (irs->isDeclarationReferenceType (var))
	gcc_assert (POINTER_TYPE_P (TREE_TYPE (exp)));
      else
	exp = irs->addressOf (exp);

      if (!offset)
	return irs->convertTo (type->toCtype(), exp);

      tree b = irs->integerConstant (offset, Type::tsize_t);
      return irs->nop (type->toCtype(), irs->pointerOffset (exp, b));
    }

  gcc_assert (op == TOKvar || op == TOKsymoff);
  return error_mark_node;
}

elem *
NewExp::toElem (IRState *irs)
{
  Type *tb = type->toBasetype();
  LibCall libcall;
  tree result;

  if (allocator)
    gcc_assert (newargs);

  // New'ing a class.
  if (tb->ty == Tclass)
    {
      tb = newtype->toBasetype();
      gcc_assert (tb->ty == Tclass);
      TypeClass *class_type = (TypeClass *) tb;
      ClassDeclaration *class_decl = class_type->sym;

      tree new_call;
      tree setup_exp = NULL_TREE;
      // type->toCtype() is a REFERENCE_TYPE; we want the RECORD_TYPE
      tree rec_type = TREE_TYPE (class_type->toCtype());

      // Call allocator (custom allocator or _d_newclass).
      if (onstack)
	{
	  tree stack_var = irs->localVar (rec_type);
	  irs->expandDecl (stack_var);
	  new_call = irs->addressOf (stack_var);
	  setup_exp = irs->modify (irs->indirect (rec_type, new_call),
				   class_decl->toInitializer()->Stree);
	}
      else if (allocator)
	{
	  new_call = irs->call (allocator, newargs);
	  new_call = irs->maybeMakeTemp (new_call);
	  // copy memory...
	  setup_exp = irs->modify (irs->indirect (rec_type, new_call),
				   class_decl->toInitializer()->Stree);
	}
      else
	{
	  tree arg = irs->addressOf (class_decl->toSymbol()->Stree);
	  new_call = irs->libCall (LIBCALL_NEWCLASS, 1, &arg);
	}
      new_call = irs->nop (tb->toCtype(), new_call);

      // Set vthis for nested classes.
      if (class_decl->isNested())
	{
	  tree vthis_value = NULL_TREE;
	  tree vthis_field = class_decl->vthis->toSymbol()->Stree;
	  if (thisexp)
	    {
	      ClassDeclaration *thisexp_cd = thisexp->type->isClassHandle();
	      Dsymbol *outer = class_decl->toParent2();
	      int offset = 0;

	      vthis_value = thisexp->toElem (irs);
	      if (outer != thisexp_cd)
		{
		  ClassDeclaration *outer_cd = outer->isClassDeclaration();
		  gcc_assert (outer_cd->isBaseOf (thisexp_cd, &offset));
		  // could just add offset
		  vthis_value = irs->convertTo (vthis_value, thisexp->type, outer_cd->type);
		}
	    }
	  else
	    {
	      vthis_value = irs->getVThis (class_decl, this);
	    }

	  if (vthis_value)
	    {
	      new_call = irs->maybeMakeTemp (new_call);
	      vthis_field = irs->component (irs->indirect (rec_type, new_call), vthis_field);
	      setup_exp = irs->maybeCompound (setup_exp, irs->modify (vthis_field, vthis_value));
	    }
	}
      new_call = irs->maybeCompound (setup_exp, new_call);

      // Call constructor.
      if (member)
	result = irs->call (member, new_call, arguments);
      else
	result = new_call;
    }
  // New'ing a struct.
  else if (tb->ty == Tpointer && tb->nextOf()->toBasetype()->ty == Tstruct)
    {
      gcc_assert (!onstack);

      Type * handle_type = newtype->toBasetype();
      gcc_assert (handle_type->ty == Tstruct);
      TypeStruct *struct_type = (TypeStruct *) handle_type;
      StructDeclaration *sd = struct_type->sym;
      Expression *init = struct_type->defaultInit (loc);

      tree new_call;
      tree setup_exp = NULL_TREE;

      if (allocator)
	new_call = irs->call (allocator, newargs);
      else
	{
	  libcall = struct_type->isZeroInit (loc) ? LIBCALL_NEWITEMT : LIBCALL_NEWITEMIT;
	  tree arg = type->getTypeInfo(NULL)->toElem (irs);
	  new_call = irs->libCall (libcall, 1, &arg);
	}
      new_call = irs->nop (tb->toCtype(), new_call);

      // Save the result allocation call.
      new_call = irs->maybeMakeTemp (new_call);
      setup_exp = irs->indirect (new_call);
      setup_exp = irs->modify (setup_exp, irs->convertForAssignment (init, struct_type));
      new_call = irs->compound (setup_exp, new_call);

      // Set vthis for nested structs/classes.
      if (sd->isNested())
	{
	  tree vthis_value = irs->getVThis (sd, this);
	  tree vthis_field;
	  new_call = irs->maybeMakeTemp (new_call);
	  vthis_field = irs->component (irs->indirect (struct_type->toCtype(), new_call),
					sd->vthis->toSymbol()->Stree);
	  new_call = irs->compound (irs->modify (vthis_field, vthis_value), new_call);
	}

      // Call constructor.
      if (member)
	result = irs->call (member, new_call, arguments);
      else
	result = new_call;
    }
  // New'ing a D array.
  else if (tb->ty == Tarray)
    {
      tb = newtype->toBasetype();
      gcc_assert (tb->ty == Tarray);
      TypeDArray *array_type = (TypeDArray *) tb;
      gcc_assert (!allocator);
      gcc_assert (arguments && arguments->dim >= 1);

      if (arguments->dim == 1)
	{
	  // Single dimension array allocations.
	  Expression *arg = (*arguments)[0];
	  libcall = array_type->next->isZeroInit() ? LIBCALL_NEWARRAYT : LIBCALL_NEWARRAYIT;
	  tree args[2] = {
	      type->getTypeInfo(NULL)->toElem (irs),
	      arg->toElem (irs)
	  };
	  result = irs->libCall (libcall, 2, args, tb->toCtype());
	}
      else
	{
	  // Multidimensional array allocations.

	  tree dims_var = irs->exprVar (irs->arrayType (size_type_node, arguments->dim));
	  tree dims_init;
	  CtorEltMaker elms;

	  Type *telem = newtype->toBasetype();
	  for (size_t i = 0; i < arguments->dim; i++)
	    {
	      Expression *arg = (*arguments)[i];
	      elms.cons (irs->integerConstant (i, size_type_node), arg->toElem (irs));
	      dims_init = build_constructor (TREE_TYPE (dims_var), elms.head);
	      DECL_INITIAL (dims_var) = dims_init;

	      gcc_assert (telem->ty == Tarray);
	      telem = telem->toBasetype()->nextOf();
	      gcc_assert (telem);
	    }

	  libcall = telem->isZeroInit() ? LIBCALL_NEWARRAYMTX : LIBCALL_NEWARRAYMITX;
	  tree args[3] = {
	      type->getTypeInfo(NULL)->toElem (irs),
	      irs->integerConstant (arguments->dim, Type::tint32), // The ndims arg is declared as 'int'
	      irs->addressOf (dims_var)
	  };
	  result = irs->libCall (libcall, 3, args, tb->toCtype());
	  result = irs->binding (dims_var, result);
	}
    }
  // New'ing a pointer
  else if (tb->ty == Tpointer)
    {
      TypePointer *pointer_type = (TypePointer *) tb;

      libcall = pointer_type->next->isZeroInit (loc) ? LIBCALL_NEWITEMT : LIBCALL_NEWITEMIT;
      tree arg = type->getTypeInfo(NULL)->toElem (irs);
      result = irs->libCall (libcall, 1, &arg, tb->toCtype());
    }
  else
    gcc_unreachable();

  return irs->convertTo (result, tb, type);
}

elem *
ScopeExp::toElem (IRState *irs)
{
  ::error ("%s is not an expression", toChars());
  return irs->errorMark (type);
}

elem *
TypeExp::toElem (IRState *irs)
{
  ::error ("type %s is not an expression", toChars());
  return irs->errorMark (type);
}

elem *
RealExp::toElem (IRState *irs)
{
  return irs->floatConstant (value, type->toBasetype());
}

elem *
IntegerExp::toElem (IRState *irs)
{
  return irs->integerConstant (value, type->toBasetype());
}

elem *
ComplexExp::toElem (IRState *irs)
{
  TypeBasic *compon_type;
  switch (type->toBasetype()->ty)
    {
    case Tcomplex32:
      compon_type = (TypeBasic *) Type::tfloat32;
      break;

    case Tcomplex64:
      compon_type = (TypeBasic *) Type::tfloat64;
      break;

    case Tcomplex80:
      compon_type = (TypeBasic *) Type::tfloat80;
      break;

    default:
      gcc_unreachable();
    }

  return build_complex (type->toCtype(),
			irs->floatConstant (creall (value), compon_type),
			irs->floatConstant (cimagl (value), compon_type));
}

elem *
StringExp::toElem (IRState *irs)
{
  Type *tb = type->toBasetype();
  TY base_ty = type ? tb->ty : (TY) Tvoid;
  tree value;

  switch (base_ty)
    {
    case Tpointer:
      // Assuming this->string is null terminated
      // .. need to terminate with more nulls for wchar and dchar?
      value = build_string ((len + 1) * sz, (char *) string);
      break;

    case Tsarray:
    case Tarray:
      value = build_string (len * sz, (char *) string);
      break;

    default:
      error ("Invalid type for string constant: %s", type->toChars());
      return irs->errorMark (type);
    }

  TREE_CONSTANT (value) = 1;
  TREE_READONLY (value) = 1;
  // %% array type doesn't match string length if null term'd...
  Type *elem_type = base_ty != Tvoid ? tb->nextOf() : Type::tchar;
  TREE_TYPE (value) = irs->arrayType (elem_type, len);

  switch (base_ty)
    {
    case Tarray:
      value = irs->darrayVal (type, len, irs->addressOf (value));
      break;
    case Tpointer:
      value = irs->addressOf (value);
      break;
    case Tsarray:
      // %% needed?
      TREE_TYPE (value) = type->toCtype();
      break;
    default:
      // nothing
      break;
    }
  return value;
}

elem *
TupleExp::toElem (IRState *irs)
{
  tree result = NULL_TREE;
  if (exps && exps->dim)
    {
      for (size_t i = 0; i < exps->dim; ++i)
	{
	  Expression *e = (*exps)[i];
	  result = irs->maybeVoidCompound (result, e->toElem (irs));
	}
    }
  else
    result = d_void_zero_node;

  return result;
}

elem *
ArrayLiteralExp::toElem (IRState *irs)
{
  Type *typeb = type->toBasetype();
  gcc_assert (typeb->ty == Tarray || typeb->ty == Tsarray || typeb->ty == Tpointer);
  Type *etype = typeb->nextOf();
  tree sa_type = irs->arrayType (etype->toCtype(), elements->dim);
  tree result = NULL_TREE;

  /* Build an expression that assigns the expressions in ELEMENTS to a constructor. */
  CtorEltMaker elms;

  elms.reserve (elements->dim);
  for (size_t i = 0; i < elements->dim; i++)
    {
      elms.cons (irs->integerConstant (i, size_type_node),
		 irs->convertTo ((*elements)[i], etype));
    }
  tree ctor = build_constructor (sa_type, elms.head);

  // Should be ok to skip initialising constant literals on heap.
  if (type->isConst())
    {
      result = irs->addressOf (ctor);
    }
  else
    {
      tree args[2] = {
	  irs->typeinfoReference (etype->arrayOf()),
	  irs->integerConstant (elements->dim, size_type_node)
      };
      // Call _d_arrayliteralTX (ti, dim);
      tree mem = irs->libCall (LIBCALL_ARRAYLITERALTX, 2, args, etype->pointerTo()->toCtype());
      mem = irs->maybeMakeTemp (mem);

      // memcpy (mem, &ctor, size)
      tree size = fold_build2 (MULT_EXPR, size_type_node,
			       size_int (elements->dim), size_int (typeb->nextOf()->size()));

      result = irs->buildCall (builtin_decl_explicit (BUILT_IN_MEMCPY), 3,
			       mem, irs->addressOf (ctor), size);

      // Returns array pointed to by MEM.
      result = irs->maybeCompound (result, mem);
    }

  if (typeb->ty == Tarray)
    result = irs->darrayVal (type, elements->dim, result);
  else if (typeb->ty == Tsarray)
    result = irs->indirect (sa_type, result);

  return result;
}

elem *
AssocArrayLiteralExp::toElem (IRState *irs)
{
  Type *tb = type->toBasetype();
  // %% want mutable type for typeinfo reference.
  tb = tb->mutableOf();

  TypeAArray *aa_type;

  if (tb->ty == Taarray)
    aa_type = (TypeAArray *)tb;
  else
    {
      // It's the AssociativeArray type.
      // Turn it back into a TypeAArray
      aa_type = new TypeAArray ((*values)[0]->type, (*keys)[0]->type);
      aa_type->semantic (loc, NULL);
    }

  Type *index = aa_type->index;
  Type *next = aa_type->next;
  gcc_assert (keys != NULL);
  gcc_assert (values != NULL);

  tree keys_var = irs->exprVar (irs->arrayType (index, keys->dim)); //?
  tree vals_var = irs->exprVar (irs->arrayType (next, keys->dim));
  tree keys_ptr = irs->nop (index->pointerTo()->toCtype(),
			    irs->addressOf (keys_var));
  tree vals_ptr = irs->nop (next->pointerTo()->toCtype(),
			    irs->addressOf (vals_var));
  tree keys_offset = size_zero_node;
  tree vals_offset = size_zero_node;
  tree keys_size = size_int (index->size());
  tree vals_size = size_int (next->size());
  tree result = NULL_TREE;

  for (size_t i = 0; i < keys->dim; i++)
    {
      Expression *e;
      tree elemp_e, assgn_e;

      e = (*keys)[i];
      elemp_e = irs->pointerOffset (keys_ptr, keys_offset);
      assgn_e = irs->vmodify (irs->indirect (elemp_e), e->toElem (irs));
      keys_offset = size_binop (PLUS_EXPR, keys_offset, keys_size);
      result = irs->maybeCompound (result, assgn_e);

      e = (*values)[i];
      elemp_e = irs->pointerOffset (vals_ptr, vals_offset);
      assgn_e = irs->vmodify (irs->indirect (elemp_e), e->toElem (irs));
      vals_offset = size_binop (PLUS_EXPR, vals_offset, vals_size);
      result = irs->maybeCompound (result, assgn_e);
    }

  tree args[3] = {
      irs->typeinfoReference (aa_type),
      irs->darrayVal (index->arrayOf(), keys->dim, keys_ptr),
      irs->darrayVal (next->arrayOf(), keys->dim, vals_ptr)
  };
  result = irs->maybeCompound (result, irs->libCall (LIBCALL_ASSOCARRAYLITERALTX, 3, args));

  CtorEltMaker ce;
  tree aat_type = aa_type->toCtype();
  ce.cons (TYPE_FIELDS (aat_type), result);
  tree ctor = build_constructor (aat_type, ce.head);

  result = irs->binding (keys_var, irs->binding (vals_var, ctor));
  return irs->nop (type->toCtype(), result);
}

elem *
StructLiteralExp::toElem (IRState *irs)
{
  CtorEltMaker ce;
  StructDeclaration *sdecl;
  Type *tb = type->toBasetype();

  gcc_assert (tb->ty == Tstruct);
  sdecl = ((TypeStruct *) tb)->sym;

  if (elements)
    {
      size_t dim = elements->dim;
      gcc_assert (dim <= sd->fields.dim - sd->isnested);

      for (size_t i = 0; i < dim; i++)
	{
	  if (!(*elements)[i])
	    continue;

	  Expression *exp = (*elements)[i];
	  Type *exp_type = exp->type->toBasetype();
	  tree exp_tree = NULL_TREE;
	  tree call_exp = NULL_TREE;

	  VarDeclaration *fld = sd->fields[i];
	  Type *fld_type = fld->type->toBasetype();

	  if (fld_type->ty == Tsarray)
	    {
	      if (irs->typesCompatible (exp_type, fld_type))
		{
		  StructDeclaration *sd = needsPostblit (fld_type);
		  if (sd != NULL)
		    {
		      // Generate _d_arrayctor (ti, from = exp, to = exp_tree)
		      Type *ti = fld_type->nextOf();
		      exp_tree = irs->localVar (exp_type);
		      tree args[3] = {
			  irs->typeinfoReference (ti),
			  irs->toDArray (exp),
			  irs->convertTo (exp_tree, exp_type, ti->arrayOf())
		      };
		      call_exp = irs->libCall (LIBCALL_ARRAYCTOR, 3, args);
		    }
		  else
		    {
		      // %% This would call _d_newarrayT ... use memcpy?
		      exp_tree = irs->convertTo (exp, fld->type);
		    }
		}
	      else
		{
		  // %% Could use memset if is zero init...
		  exp_tree = irs->localVar (fld_type);
		  Type *etype = fld_type;
		  while (etype->ty == Tsarray)
		    etype = etype->nextOf();

		  gcc_assert (fld_type->size() % etype->size() == 0);
		  tree size = fold_build2 (TRUNC_DIV_EXPR, size_type_node,
					   size_int (fld_type->size()), size_int (etype->size()));

		  tree ptr_tree = irs->nop (etype->pointerTo()->toCtype(),
					    irs->addressOf (exp_tree));
		  tree set_exp = irs->arraySetExpr (ptr_tree, exp->toElem (irs), size);
		  exp_tree = irs->compound (set_exp, exp_tree);
		}
	    }
	  else
	    {
	      exp_tree = irs->convertTo (exp, fld->type);
	      StructDeclaration *sd = needsPostblit (fld_type);
	      if (sd && exp->isLvalue())
		{
		  // Call __postblit (&exp_tree)
		  Expressions args;
		  call_exp = irs->call (sd->postblit, irs->addressOf (exp_tree), &args);
		}
	    }

	  if (call_exp)
	    irs->addExp (call_exp);

	  ce.cons (fld->toSymbol()->Stree, exp_tree);

	  // Unions only have one field that gets assigned.
	  if (sdecl->isUnionDeclaration())
	    break;
	}
    }
  if (sd->isNested())
    {
      // Maybe setup hidden pointer to outer scope context.
      tree vthis_field = sd->vthis->toSymbol()->Stree;
      tree vthis_value = irs->getVThis (sd, this);
      ce.cons (vthis_field, vthis_value);
    }
  tree ctor = build_constructor (type->toCtype(), ce.head);
  return ctor;
}

elem *
NullExp::toElem (IRState *irs)
{
  TY base_ty = type->toBasetype()->ty;
  tree null_exp;
  // 0 -> dynamic array.  This is a special case conversion.
  // Move to convert for convertTo if it shows up elsewhere.
  switch (base_ty)
    {
    case Tarray:
	{
	  null_exp = irs->darrayVal (type, 0, NULL);
	  break;
	}
    case Taarray:
	{
	  CtorEltMaker ce;
	  tree ttype = type->toCtype();
	  tree fa = TYPE_FIELDS (ttype);

	  ce.cons (fa, irs->convertTo (TREE_TYPE (fa), integer_zero_node));
	  null_exp = build_constructor (ttype, ce.head);
	  break;
	}
    case Tdelegate:
	{
	  // makeDelegateExpression ?
	  null_exp = irs->delegateVal (d_null_pointer, d_null_pointer, type);
	  break;
	}
    default:
	{
	  null_exp = irs->convertTo (type->toCtype(), integer_zero_node);
	  break;
	}
    }

  return null_exp;
}

elem *
ThisExp::toElem (IRState *irs)
{
  tree this_tree = NULL_TREE;

  if (var)
    {
      gcc_assert(var->isVarDeclaration());
      this_tree = irs->var (var);
    }
  else
    {
      gcc_assert (irs->func && irs->func->vthis);
      this_tree = irs->var (irs->func->vthis);
    }

  if (type->ty == Tstruct)
    this_tree = irs->indirect (this_tree);

  return this_tree;
}

elem *
VectorExp::toElem (IRState *irs)
{
  tree vectype = type->toCtype();
  tree elemtype = TREE_TYPE (vectype);

  // First handle array literal expressions.
  if (e1->op == TOKarrayliteral)
    {
      Expressions *elements = ((ArrayLiteralExp *) e1)->elements;
      CtorEltMaker elms;
      bool constant_p = true;

      elms.reserve (elements->dim);
      for (size_t i = 0; i < elements->dim; i++)
	{
	  Expression *e = (*elements)[i];
	  tree value = irs->convertTo (elemtype, e->toElem (irs));
	  if (!CONSTANT_CLASS_P (value))
	    constant_p = false;

	  elms.cons (irs->integerConstant (i, size_type_node), value);
	}

      // Build a VECTOR_CST from a constant vector constructor.
      if (constant_p)
	return build_vector_from_ctor (vectype, elms.head);

      return build_constructor (vectype, elms.head);
    }
  else
    {
      // Build constructor from single value.
      tree val = irs->convertTo (elemtype, e1->toElem (irs));

      return build_vector_from_val (vectype, val);
    }
}

