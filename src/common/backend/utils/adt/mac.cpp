/*
 *	openGauss type definitions for MAC addresses.
 *
 *	src/backend/utils/adt/mac.c
 */

#include "postgres.h"
#include "knl/knl_variable.h"

#include "access/hash.h"
#include "libpq/pqformat.h"
#include "utils/builtins.h"
#include "utils/inet.h"

/*
 *	Utility macros used for sorting and comparing:
 */

#define hibits(addr) ((unsigned long)(((addr)->a << 16) | ((addr)->b << 8) | ((addr)->c)))

#define lobits(addr) ((unsigned long)(((addr)->d << 16) | ((addr)->e << 8) | ((addr)->f)))

/*
 *	MAC address reader.  Accepts several common notations.
 */

Datum macaddr_in(PG_FUNCTION_ARGS)
{
    char* str = PG_GETARG_CSTRING(0);
    const int JUNK_LEN = 2;
    const int EXPECT_PARA_NUMS = 6;
    macaddr* result = NULL;
    unsigned int a, b, c, d, e, f;
    char junk[JUNK_LEN];
    int count;

    int level = fcinfo->can_ignore ? WARNING : ERROR;

    /* %1s matches iff there is trailing non-whitespace garbage */

    count = sscanf_s(str, "%x:%x:%x:%x:%x:%x%1s", &a, &b, &c, &d, &e, &f, junk, JUNK_LEN);
    if (count != EXPECT_PARA_NUMS)
        count = sscanf_s(str, "%x-%x-%x-%x-%x-%x%1s", &a, &b, &c, &d, &e, &f, junk, JUNK_LEN);
    if (count != EXPECT_PARA_NUMS)
        count = sscanf_s(str, "%2x%2x%2x:%2x%2x%2x%1s", &a, &b, &c, &d, &e, &f, junk, JUNK_LEN);
    if (count != EXPECT_PARA_NUMS)
        count = sscanf_s(str, "%2x%2x%2x-%2x%2x%2x%1s", &a, &b, &c, &d, &e, &f, junk, JUNK_LEN);
    if (count != EXPECT_PARA_NUMS)
        count = sscanf_s(str, "%2x%2x.%2x%2x.%2x%2x%1s", &a, &b, &c, &d, &e, &f, junk, JUNK_LEN);
    if (count != EXPECT_PARA_NUMS)
        count = sscanf_s(str, "%2x%2x%2x%2x%2x%2x%1s", &a, &b, &c, &d, &e, &f, junk, JUNK_LEN);
    if (count != EXPECT_PARA_NUMS) {
        ereport(level,
            (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                errmsg("invalid input syntax for type macaddr: \"%s\"", str)));
        
        /* ignore error case: reset to base value */
        a = b = c = d = e = f = 0;
    }

    if ((a > 255) || (b > 255) || (c > 255) || (d > 255) || (e > 255) || (f > 255)) {
        ereport(level,
            (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                errmsg("invalid octet value in \"macaddr\" value: \"%s\"", str)));
        
        /* ignore error case: reset to base value */
        a = b = c = d = e = f = 0;
    }

    result = (macaddr*)palloc(sizeof(macaddr));

    result->a = a;
    result->b = b;
    result->c = c;
    result->d = d;
    result->e = e;
    result->f = f;

    PG_RETURN_MACADDR_P(result);
}

/*
 *	MAC address output function.  Fixed format.
 */

Datum macaddr_out(PG_FUNCTION_ARGS)
{
    macaddr* addr = PG_GETARG_MACADDR_P(0);
    char* result = NULL;
    errno_t ss_rc;
    const int data_len = 32;

    result = (char*)palloc(data_len);

    ss_rc = snprintf_s(
        result, data_len, 31, "%02x:%02x:%02x:%02x:%02x:%02x", addr->a, addr->b, addr->c, addr->d, addr->e, addr->f);
    securec_check_ss(ss_rc, "\0", "\0");

    PG_RETURN_CSTRING(result);
}

/*
 *		macaddr_recv			- converts external binary format to macaddr
 *
 * The external representation is just the six bytes, MSB first.
 */
Datum macaddr_recv(PG_FUNCTION_ARGS)
{
    StringInfo buf = (StringInfo)PG_GETARG_POINTER(0);
    macaddr* addr = NULL;

    addr = (macaddr*)palloc(sizeof(macaddr));

    addr->a = pq_getmsgbyte(buf);
    addr->b = pq_getmsgbyte(buf);
    addr->c = pq_getmsgbyte(buf);
    addr->d = pq_getmsgbyte(buf);
    addr->e = pq_getmsgbyte(buf);
    addr->f = pq_getmsgbyte(buf);

    PG_RETURN_MACADDR_P(addr);
}

/*
 *		macaddr_send			- converts macaddr to binary format
 */
Datum macaddr_send(PG_FUNCTION_ARGS)
{
    macaddr* addr = PG_GETARG_MACADDR_P(0);
    StringInfoData buf;

    pq_begintypsend(&buf);
    pq_sendbyte(&buf, addr->a);
    pq_sendbyte(&buf, addr->b);
    pq_sendbyte(&buf, addr->c);
    pq_sendbyte(&buf, addr->d);
    pq_sendbyte(&buf, addr->e);
    pq_sendbyte(&buf, addr->f);
    PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/*
 *	Comparison function for sorting:
 */

static int32 macaddr_cmp_internal(macaddr* a1, macaddr* a2)
{
    if (hibits(a1) < hibits(a2))
        return -1;
    else if (hibits(a1) > hibits(a2))
        return 1;
    else if (lobits(a1) < lobits(a2))
        return -1;
    else if (lobits(a1) > lobits(a2))
        return 1;
    else
        return 0;
}

Datum macaddr_cmp(PG_FUNCTION_ARGS)
{
    macaddr* a1 = PG_GETARG_MACADDR_P(0);
    macaddr* a2 = PG_GETARG_MACADDR_P(1);

    PG_RETURN_INT32(macaddr_cmp_internal(a1, a2));
}

/*
 *	Boolean comparisons.
 */

Datum macaddr_lt(PG_FUNCTION_ARGS)
{
    macaddr* a1 = PG_GETARG_MACADDR_P(0);
    macaddr* a2 = PG_GETARG_MACADDR_P(1);

    PG_RETURN_BOOL(macaddr_cmp_internal(a1, a2) < 0);
}

Datum macaddr_le(PG_FUNCTION_ARGS)
{
    macaddr* a1 = PG_GETARG_MACADDR_P(0);
    macaddr* a2 = PG_GETARG_MACADDR_P(1);

    PG_RETURN_BOOL(macaddr_cmp_internal(a1, a2) <= 0);
}

Datum macaddr_eq(PG_FUNCTION_ARGS)
{
    macaddr* a1 = PG_GETARG_MACADDR_P(0);
    macaddr* a2 = PG_GETARG_MACADDR_P(1);

    PG_RETURN_BOOL(macaddr_cmp_internal(a1, a2) == 0);
}

Datum macaddr_ge(PG_FUNCTION_ARGS)
{
    macaddr* a1 = PG_GETARG_MACADDR_P(0);
    macaddr* a2 = PG_GETARG_MACADDR_P(1);

    PG_RETURN_BOOL(macaddr_cmp_internal(a1, a2) >= 0);
}

Datum macaddr_gt(PG_FUNCTION_ARGS)
{
    macaddr* a1 = PG_GETARG_MACADDR_P(0);
    macaddr* a2 = PG_GETARG_MACADDR_P(1);

    PG_RETURN_BOOL(macaddr_cmp_internal(a1, a2) > 0);
}

Datum macaddr_ne(PG_FUNCTION_ARGS)
{
    macaddr* a1 = PG_GETARG_MACADDR_P(0);
    macaddr* a2 = PG_GETARG_MACADDR_P(1);

    PG_RETURN_BOOL(macaddr_cmp_internal(a1, a2) != 0);
}

/*
 * Support function for hash indexes on macaddr.
 */
Datum hashmacaddr(PG_FUNCTION_ARGS)
{
    macaddr* key = PG_GETARG_MACADDR_P(0);

    return hash_any((unsigned char*)key, sizeof(macaddr));
}

/*
 * Arithmetic functions: bitwise NOT, AND, OR.
 */
Datum macaddr_not(PG_FUNCTION_ARGS)
{
    macaddr* addr = PG_GETARG_MACADDR_P(0);
    macaddr* result = NULL;

    result = (macaddr*)palloc(sizeof(macaddr));
    result->a = ~addr->a;
    result->b = ~addr->b;
    result->c = ~addr->c;
    result->d = ~addr->d;
    result->e = ~addr->e;
    result->f = ~addr->f;
    PG_RETURN_MACADDR_P(result);
}

Datum macaddr_and(PG_FUNCTION_ARGS)
{
    macaddr* addr1 = PG_GETARG_MACADDR_P(0);
    macaddr* addr2 = PG_GETARG_MACADDR_P(1);
    macaddr* result = NULL;

    result = (macaddr*)palloc(sizeof(macaddr));
    result->a = addr1->a & addr2->a;
    result->b = addr1->b & addr2->b;
    result->c = addr1->c & addr2->c;
    result->d = addr1->d & addr2->d;
    result->e = addr1->e & addr2->e;
    result->f = addr1->f & addr2->f;
    PG_RETURN_MACADDR_P(result);
}

Datum macaddr_or(PG_FUNCTION_ARGS)
{
    macaddr* addr1 = PG_GETARG_MACADDR_P(0);
    macaddr* addr2 = PG_GETARG_MACADDR_P(1);
    macaddr* result = NULL;

    result = (macaddr*)palloc(sizeof(macaddr));
    result->a = addr1->a | addr2->a;
    result->b = addr1->b | addr2->b;
    result->c = addr1->c | addr2->c;
    result->d = addr1->d | addr2->d;
    result->e = addr1->e | addr2->e;
    result->f = addr1->f | addr2->f;
    PG_RETURN_MACADDR_P(result);
}

/*
 *	Truncation function to allow comparing mac manufacturers.
 *	From suggestion by Alex Pilosov <alex@pilosoft.com>
 */
Datum macaddr_trunc(PG_FUNCTION_ARGS)
{
    macaddr* addr = PG_GETARG_MACADDR_P(0);
    macaddr* result = NULL;

    result = (macaddr*)palloc(sizeof(macaddr));

    result->a = addr->a;
    result->b = addr->b;
    result->c = addr->c;
    result->d = 0;
    result->e = 0;
    result->f = 0;

    PG_RETURN_MACADDR_P(result);
}
