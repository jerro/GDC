/**
 * TypeInfo support code.
 *
 * Copyright: Copyright Digital Mars 2004 - 2009.
 * License:   <a href="http://www.boost.org/LICENSE_1_0.txt">Boost License 1.0</a>.
 * Authors:   Walter Bright
 */

/*          Copyright Digital Mars 2004 - 2009.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
module rt.typeinfo.ti_short;

// short

class TypeInfo_s : TypeInfo
{
    @trusted:
    const:
    pure:
    nothrow:

    override string toString() const pure nothrow @safe { return "short"; }

    override hash_t getHash(in void* p)
    {
        return *cast(short *)p;
    }

    override equals_t equals(in void* p1, in void* p2)
    {
        return *cast(short *)p1 == *cast(short *)p2;
    }

    override int compare(in void* p1, in void* p2)
    {
        return *cast(short *)p1 - *cast(short *)p2;
    }

    @property override size_t tsize() nothrow pure
    {
        return short.sizeof;
    }

    override void swap(void *p1, void *p2)
    {
        short t;

        t = *cast(short *)p1;
        *cast(short *)p1 = *cast(short *)p2;
        *cast(short *)p2 = t;
    }
}
