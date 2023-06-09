/* ----------
 * pg_lzcompress.h -
 *
 *	Definitions for the builtin LZ compressor
 *
 * src/include/utils/pg_lzcompress.h
 * ----------
 */

#ifndef _PG_LZCOMPRESS_H_
#define _PG_LZCOMPRESS_H_

/* ----------
 * PGLZ_Header -
 *
 *		The information at the start of the compressed data.
 * ----------
 */
typedef struct PGLZ_Header {
    int32 vl_len_; /* varlena header (do not touch directly!) */
    int32 rawsize;
} PGLZ_Header;

/* ----------
 * PGLZ_MAX_OUTPUT -
 *
 *		Macro to compute the buffer size required by pglz_compress().
 *		We allow 4 bytes for overrun before detecting compression failure.
 * ----------
 */
#define PGLZ_MAX_OUTPUT(_dlen) ((_dlen) + 4 + sizeof(PGLZ_Header))

/* ----------
 * PGLZ_RAW_SIZE -
 *
 *		Macro to determine the uncompressed data size contained
 *		in the entry.
 * ----------
 */
#define PGLZ_RAW_SIZE(_lzdata) ((_lzdata)->rawsize)

/* ----------
 * PGLZ_Strategy -
 *
 *		Some values that control the compression algorithm.
 *
 *		min_input_size		Minimum input data size to consider compression.
 *
 *		max_input_size		Maximum input data size to consider compression.
 *
 *		min_comp_rate		Minimum compression rate (0-99%) to require.
 *							Regardless of min_comp_rate, the output must be
 *							smaller than the input, else we don't store
 *							compressed.
 *
 *		first_success_by	Abandon compression if we find no compressible
 *							data within the first this-many bytes.
 *
 *		match_size_good		The initial GOOD match size when starting history
 *							lookup. When looking up the history to find a
 *							match that could be expressed as a tag, the
 *							algorithm does not always walk back entirely.
 *							A good match fast is usually better than the
 *							best possible one very late. For each iteration
 *							in the lookup, this value is lowered so the
 *							longer the lookup takes, the smaller matches
 *							are considered good.
 *
 *		match_size_drop		The percentage by which match_size_good is lowered
 *							after each history check. Allowed values are
 *							0 (no change until end) to 100 (only check
 *							latest history entry at all).
 * ----------
 */
typedef struct PGLZ_Strategy {
    int32 min_input_size;
    int32 max_input_size;
    int32 min_comp_rate;
    int32 first_success_by;
    int32 match_size_good;
    int32 match_size_drop;
} PGLZ_Strategy;

/* ----------
 * Local definitions
 * ----------
 */
#define PGLZ_HISTORY_LISTS 8192 /* must be power of 2 */
#define PGLZ_HISTORY_MASK (PGLZ_HISTORY_LISTS - 1)
#define PGLZ_HISTORY_SIZE 4096
#define PGLZ_MAX_MATCH 273

/* ----------
 * PGLZ_HistEntry -
 *
 *		Linked list for the backward history lookup
 *
 * All the entries sharing a hash key are linked in a doubly linked list.
 * This makes it easy to remove an entry when it's time to recycle it
 * (because it's more than 4K positions old).
 * ----------
 */
typedef struct PGLZ_HistEntry {
    struct PGLZ_HistEntry* next; /* links for my hash key's list */
    struct PGLZ_HistEntry* prev;
    int hindex;      /* my current hash key */
    const char* pos; /* my input position */
} PGLZ_HistEntry;

/* ----------
 * The standard strategies
 *
 *		PGLZ_strategy_default		Recommended default strategy for TOAST.
 *
 *		PGLZ_strategy_always		Try to compress inputs of any length.
 *									Fallback to uncompressed storage only if
 *									output would be larger than input.
 * ----------
 */
extern const PGLZ_Strategy* const PGLZ_strategy_default;
extern const PGLZ_Strategy* const PGLZ_strategy_always;

/* ----------
 * Global function declarations
 * ----------
 */
extern bool pglz_compress(const char* source, int32 slen, PGLZ_Header* dest, const PGLZ_Strategy* strategy);
extern int32 pglz_decompress(const PGLZ_Header* source, char* dest);

#endif /* _PG_LZCOMPRESS_H_ */
